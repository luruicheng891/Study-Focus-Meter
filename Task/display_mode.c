/**
  ******************************************************************************
  * @file    display_mode.c
  * @brief   屏幕显示模式运行时切换 + printf 非阻塞 TX
  *
  *          模式切换仅通过 DisplayMode_Request() (任务上下文, 例如按钮回调) 完成.
  *          USART1 RX 命令通道已禁用, 不再响应 'C'/'W' 字符,
  *          避免 RX 引脚悬空时的电气噪声误触发屏幕来回跳转.
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
    /* USART1 RX 命令通道已禁用, 此函数保留为占位, 不再被调用. */
    (void)b;
}

/* ============================ 启动 ============================ */
/**
 * @brief 使能 USART1 NVIC 中断, 供 TXE (printf 非阻塞发送) 使用.
 *        RX 命令通道已移除, 此函数不碰 RX 相关寄存器.
 *        在 main.c 中 USART1_Init() 之后调用.
 */
void DisplayMode_StartUart1Rx(void)
{
    /* 只使能 NVIC, 让 TXE 中断能进入 CPU (printf 环形缓冲依赖此中断)
     * RXNEIE 保持 0 (HAL_UART_Init 默认不使能), 噪声不会触发中断 */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* ============================ USART1 IRQ ============================ */
/**
 * @brief USART1 中断入口: 仅处理 TXE (printf 发送), 不再处理 RX
 *        (DisplayMode_StartUart1Rx 中已关闭 RXNEIE 和 NVIC).
 *        保留这个 handler 是为了让 NVIC 表有合法入口, 万一其他代码
 *        误触发 USART1 中断也不会跑飞.
 */
void USART1_IRQ_Handler(void)
{
    USART_TypeDef *u = USART1;
    uint32_t isr = u->ISR;
    uint32_t cr1 = u->CR1;

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
