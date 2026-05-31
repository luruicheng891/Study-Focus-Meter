/**
  ******************************************************************************
  * @file    display_mode.c
  * @brief   屏幕显示模式运行时切换 + USART1 命令接收 + printf 非阻塞 TX
  *
  *          USART1 (PA9 TX / PA10 RX, 115200) 命令字符:
  *            'C' / 'c' → 切换到摄像头模式 (LCD 方向 1, 横屏)
  *            'W' / 'w' → 切换到天气仪表板 (LCD 方向 3, 横屏 180° 翻转)
  *
  *          为防止多任务并发 printf 导致 HAL_UART_Transmit 阻塞死锁,
  *          这里接管了 fputc, 改用环形缓冲 + TXE 中断的非阻塞实现:
  *            - fputc 把字节塞进 tx_ring, 立即返回 (不等待硬件)
  *            - USART1 TXE 中断逐字节从 tx_ring 取出送 TDR
  *            - 多任务 printf 安全 (短临界区保护写指针)
  ******************************************************************************
  */

#include "display_mode.h"
#include "stm32h7xx_hal.h"
#include "usart.h"          /* huart1 */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ============================ 状态变量 ============================ */
volatile DisplayMode_t g_display_mode = DISP_MODE_CAMERA;

static volatile DisplayMode_t s_pending_mode = DISP_MODE_CAMERA;
static volatile uint8_t       s_pending_flag = 0;

/* ============================ TX 环形缓冲 ============================ */
#define TX_RING_SIZE   2048   /* 2KB, 应付突发的 print_weather_info 等大块输出 */

static volatile uint8_t  tx_ring[TX_RING_SIZE];
static volatile uint16_t tx_head = 0;   /* 写入位置 (任务) */
static volatile uint16_t tx_tail = 0;   /* 读出位置 (ISR) */

/**
 * @brief 把一个字节放进环形缓冲, 启动 TXE 中断
 * @retval 1=成功 0=缓冲区满
 */
static int tx_ring_put(uint8_t b)
{
    /* 短临界区: 操作 head/tail 时禁中断, 防止 ISR 穿插 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint16_t next = (uint16_t)((tx_head + 1) & (TX_RING_SIZE - 1));
    if(next == tx_tail) {
        /* 满, 直接丢弃 (避免阻塞调用者) */
        if(!primask) __enable_irq();
        return 0;
    }

    tx_ring[tx_head] = b;
    tx_head = next;

    /* 使能 TXE 中断, 让 ISR 接管发送 */
    USART1->CR1 |= USART_CR1_TXEIE_TXFNFIE;

    if(!primask) __enable_irq();
    return 1;
}

/* ============================ fputc 重定向 ============================ */
/**
 * @brief 接管 printf 输出: 非阻塞写环形缓冲
 * @note  覆盖了 Drivers/User/Src/usart.c 里旧的 fputc 实现.
 *        Keil ARM Compiler 会优先使用最后链接到的 fputc,
 *        把这里的实现编译进工程后, 旧的 HAL_UART_Transmit 阻塞版本就不会被调用.
 *        旧 fputc 留在 usart.c 里也没影响 (符号被覆盖).
 *        若编译器报多重定义, 可把 usart.c 中的 fputc 删掉.
 */
int fputc(int ch, FILE *f)
{
    (void)f;
    /* 自动 \n → \r\n 转换 (printf 习惯) */
    if(ch == '\n') {
        tx_ring_put('\r');
    }
    tx_ring_put((uint8_t)ch);
    return ch;
}

/* ============================ 模式切换 API ============================ */

void DisplayMode_Request(DisplayMode_t mode)
{
    s_pending_mode = mode;
    s_pending_flag = 1;
}

int DisplayMode_TakePending(DisplayMode_t *out_mode)
{
    __disable_irq();
    int has = s_pending_flag;
    DisplayMode_t m = s_pending_mode;
    s_pending_flag = 0;
    __enable_irq();

    if(has && out_mode) *out_mode = m;
    return has;
}

static void process_rx_byte(uint8_t b)
{
    char ch = (char)b;
    switch(ch) {
        case 'C': case 'c':
            DisplayMode_Request(DISP_MODE_CAMERA);
            break;
        case 'W': case 'w':
            DisplayMode_Request(DISP_MODE_WEATHER);
            break;
        default:
            break;
    }
}

/* ============================ 启动 ============================ */
void DisplayMode_StartUart1Rx(void)
{
    /* 确保 USART1 接收使能并清错误标志 */
    USART1->CR1 |= USART_CR1_RE;
    USART1->ICR  = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF |
                   USART_ICR_PECF  | USART_ICR_IDLECF;

    /* 使能 RXNE 中断 */
    USART1->CR1 |= USART_CR1_RXNEIE_RXFNEIE;

    /* 配置 NVIC (优先级 6, 在 FreeRTOS 安全范围内) */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    printf("[CMD] USART1 ready. 'C'=camera, 'W'=weather.\r\n");
}

/* ============================ USART1 IRQ ============================ */
void USART1_IRQ_Handler(void)
{
    USART_TypeDef *u = USART1;
    uint32_t isr = u->ISR;
    uint32_t cr1 = u->CR1;

    /* === RX === */
    if((isr & USART_ISR_RXNE_RXFNE) && (cr1 & USART_CR1_RXNEIE_RXFNEIE))
    {
        uint8_t b = (uint8_t)(u->RDR & 0xFFU);
        process_rx_byte(b);
    }

    /* === TX (TXE_TXFNF) === */
    if((isr & USART_ISR_TXE_TXFNF) && (cr1 & USART_CR1_TXEIE_TXFNFIE))
    {
        if(tx_head != tx_tail) {
            /* 还有字节要发 */
            u->TDR = tx_ring[tx_tail];
            tx_tail = (uint16_t)((tx_tail + 1) & (TX_RING_SIZE - 1));
        } else {
            /* 缓冲空, 关闭 TXE 中断 */
            u->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
        }
    }

    /* === 错误清除 === */
    if(isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
    {
        u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }
}
