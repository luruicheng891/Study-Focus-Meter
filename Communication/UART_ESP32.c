/**
  ******************************************************************************
  * @file    UART_ESP32.c
  * @brief   USART2 (ESP32 BLE 透传) 底层接收驱动
  *          USART2 + DMA1_Stream3 + IDLE 中断, 只做"把字节流分成帧"这一件事。
  *          业务层 (JSON 解析 / 队列发布) 见 Task/SlaveRxTask.c
  *
  * 关键约束:
  *   1. STM32H7 DMA 不能访问 DTCM (0x20000000), 缓冲区必须放在 AXI SRAM。
  *   2. AXI SRAM Cacheable, DMA 写入后必须 SCB_InvalidateDCache_by_Addr。
  *   3. DMA 资源占用:
  *        Stream0 = SPI1 TX (LCD), Stream1 = USART3 RX (ESP-01),
  *        Stream2 = ADC2 (MAX9814), 本驱动选 Stream3。
  *   4. 中断优先级 (6, x) 满足 FreeRTOS 安全范围 (≥5)。
  *   5. stm32h7xx_it.c 必须把 USART2_IRQHandler / DMA1_Stream3_IRQHandler
  *      分别派发到 USART2_IRQ_Handler() 与 HAL_DMA_IRQHandler(huart2.hdmarx)。
  ******************************************************************************
  */

#include "UART_ESP32.h"
#include <stdio.h>
#include <string.h>

/* =========================== 调试开关 =========================== */
/* 置 1: 每收到一帧 USART2 (ESP32) 数据, 都把原始内容通过 USART1 (printf) 打印出来,
 *       用于排查"收不到 / 解析不到数据"的问题。排查完置 0 关闭即可。 */
#define ESP32_RX_DEBUG   1

/* =========================== 内部状态 =========================== */

UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_rx;

/* DMA RX 缓冲区: 必须位于 AXI SRAM, 32 字节对齐 */
static uint8_t s_dma_buf[USART2_RX_BUFFER_SIZE]
    __attribute__((section(".ARM.__at_0x24040D00"), aligned(32)));

/* 帧缓冲区 (供上层拷出, 普通 SRAM 即可) */
static uint8_t           s_frame_buf[USART2_RX_BUFFER_SIZE + 1];
static volatile uint16_t s_frame_len   = 0;
static volatile uint8_t  s_frame_ready = 0;

/* 上层任务句柄 (由 UART_ESP32_BindRxTask 设置) */
static TaskHandle_t s_rx_task = NULL;

/* 统计计数器 */
static volatile uint32_t s_stat_recv = 0;
static volatile uint32_t s_stat_drop = 0;

/* =========================== USART2 + DMA 初始化 =========================== */

static void USART2_MspInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* PA2 = USART2_TX (留作向 ESP32 发送指令, 当前未使用) */
    GPIO_InitStruct.Pin       = GPIO_PIN_2;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA3 = USART2_RX */
    GPIO_InitStruct.Pin       = GPIO_PIN_3;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* DMA1_Stream3: USART2_RX
     *   Stream0 = SPI1 TX (LCD)
     *   Stream1 = USART3 RX (ESP-01)
     *   Stream2 = ADC2 (MAX9814)
     *   Stream3 = 本驱动 (USART2 RX)
     */
    hdma_usart2_rx.Instance                 = DMA1_Stream3;
    hdma_usart2_rx.Init.Request             = DMA_REQUEST_USART2_RX;
    hdma_usart2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_usart2_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    hdma_usart2_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
    {
        printf("[UART2] DMA init FAILED!\r\n");
        return;
    }

    __HAL_LINKDMA(&huart2, hdmarx, hdma_usart2_rx);

    /* 优先级 (6, x) 在 FreeRTOS 安全区间, 与 USART3 一致 */
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);

    HAL_NVIC_SetPriority(USART2_IRQn, 6, 1);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_Init(void)
{
    USART2_MspInit();

    huart2.Instance                    = USART2;
    huart2.Init.BaudRate               = USART2_BAUDRATE;
    huart2.Init.WordLength             = UART_WORDLENGTH_8B;
    huart2.Init.StopBits               = UART_STOPBITS_1;
    huart2.Init.Parity                 = UART_PARITY_NONE;
    huart2.Init.Mode                   = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        printf("[UART2] HAL init FAILED!\r\n");
        return;
    }

    HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_DisableFifoMode(&huart2);

    memset(s_dma_buf, 0, USART2_RX_BUFFER_SIZE);
    SCB_CleanDCache_by_Addr((uint32_t *)s_dma_buf, USART2_RX_BUFFER_SIZE);

    if (HAL_UART_Receive_DMA(&huart2, s_dma_buf, USART2_RX_BUFFER_SIZE) != HAL_OK)
    {
        printf("[UART2] HAL_UART_Receive_DMA FAILED!\r\n");
        return;
    }

    /* IDLE 中断需手动开 */
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);

    printf("[UART2] Init OK (PA2=TX, PA3=RX, %d bps)\r\n", USART2_BAUDRATE);
    printf("[UART2] DMA buffer @ 0x%08X (size %u)\r\n",
           (unsigned int)(uintptr_t)s_dma_buf,
           (unsigned int)USART2_RX_BUFFER_SIZE);
}

/* =========================== 中断 =========================== */

void USART2_IRQ_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* H7 错误标志须显式 ICR 清除, 否则反复触发 */
    uint32_t isr = huart2.Instance->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
    {
        huart2.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF
                             | USART_ICR_NECF  | USART_ICR_PECF;
    }

    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart2);

        uint16_t dma_remain = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
        uint16_t recv_len   = USART2_RX_BUFFER_SIZE - dma_remain;

        if (recv_len == 0)
        {
            /* 空 IDLE, 忽略 */
        }
        else
        {
            /* DMA 写入后必须失效 DCache, 否则 CPU 读到旧数据 (全 0) */
            SCB_InvalidateDCache_by_Addr((uint32_t *)s_dma_buf, USART2_RX_BUFFER_SIZE);

            if (recv_len > USART2_RX_BUFFER_SIZE) recv_len = USART2_RX_BUFFER_SIZE;

#if ESP32_RX_DEBUG
            /* 调试: 无论上层是否来得及消费, 每个 IDLE 帧都通过 USART1 打印原始内容。
             * 直接读 s_dma_buf (而非 s_frame_buf), 这样即使帧被丢弃也能看到。
             * 逐字节输出: 可打印字符原样, 其余用 '.' 代替防止终端乱码。 */
            printf("[ESP32-RX %u]: ", (unsigned)recv_len);
            for (uint16_t i = 0; i < recv_len; i++)
            {
                uint8_t c = s_dma_buf[i];
                putchar((c >= 0x20 && c < 0x7F) ? (char)c : '.');
            }
            printf("\r\n");
#endif

            if (s_frame_ready)
            {
                /* 上一帧还没被消费, 丢弃当前帧 (但仍重启 DMA 避免溢出) */
                s_stat_drop++;
            }
            else
            {
                memcpy(s_frame_buf, s_dma_buf, recv_len);
                s_frame_buf[recv_len] = '\0';
                s_frame_len   = recv_len;
                s_frame_ready = 1;
                s_stat_recv++;

                if (s_rx_task != NULL)
                {
                    vTaskNotifyGiveFromISR(s_rx_task, &xHigherPriorityTaskWoken);
                }
            }

            HAL_UART_DMAStop(&huart2);
            HAL_UART_Receive_DMA(&huart2, s_dma_buf, USART2_RX_BUFFER_SIZE);
        }
    }

    /* HAL 的其他事件 (TX 完成等) */
    HAL_UART_IRQHandler(&huart2);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* =========================== 对外 API =========================== */

void UART_ESP32_BindRxTask(TaskHandle_t handle)
{
    s_rx_task = handle;
}

int UART_ESP32_TakeFrame(uint8_t *buf, uint16_t max_len, uint16_t *out_len)
{
    if (buf == NULL || max_len == 0) return -1;

    int ret = -1;

    taskENTER_CRITICAL();
    if (s_frame_ready)
    {
        uint16_t n = s_frame_len;
        if (n >= max_len) n = (uint16_t)(max_len - 1);
        memcpy(buf, (const void *)s_frame_buf, n);
        buf[n] = '\0';
        if (out_len) *out_len = n;
        s_frame_ready = 0;
        ret = 0;
    }
    taskEXIT_CRITICAL();

    return ret;
}

void UART_ESP32_GetStats(uint32_t *recv, uint32_t *drop)
{
    if (recv) *recv = s_stat_recv;
    if (drop) *drop = s_stat_drop;
}

/* =========================== 发送 API =========================== */

int UART_ESP32_SendBytes(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return -1;

    /* 阻塞发送: TX 走独立的 gState 状态机, 不影响 RX DMA 循环接收 */
    if (HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 100) != HAL_OK)
    {
        return -2;
    }
    return 0;
}

int UART_ESP32_SendByte(uint8_t cmd)
{
    return UART_ESP32_SendBytes(&cmd, 1);
}
