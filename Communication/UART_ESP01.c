/**
  ******************************************************************************
  * @file    UART_ESP01.c
  * @brief   USART3 (ESP-01 WiFi 模块) 底层接收驱动
  *          USART3 (PD8=TX, PB11=RX, 115200 8N1) + DMA1_Stream1 + IDLE 中断
  *          只做"把字节流分成帧"这一件事, 不参与任何协议解析。
  *          业务层见 Task/WeatherTask.c
  *
  * 关键约束:
  *   1. STM32H7 DMA 不能访问 DTCM (0x20000000), 缓冲区必须放在 AXI SRAM。
  *   2. AXI SRAM Cacheable, DMA 写入后必须 SCB_InvalidateDCache_by_Addr。
  *   3. DMA 资源占用:
  *        Stream0 = SPI1 TX (LCD), 本驱动 = Stream1,
  *        Stream2 = ADC2 (MAX9814), Stream3 = USART2 RX (UART_ESP32)。
  *   4. 中断优先级 (6, x) 满足 FreeRTOS 安全范围 (≥5)。
  *   5. stm32h7xx_it.c 必须把 USART3_IRQHandler / DMA1_Stream1_IRQHandler
  *      分别派发到 USART3_IRQ_Handler() 与 HAL_DMA_IRQHandler(huart3.hdmarx)。
  ******************************************************************************
  */

#include "UART_ESP01.h"
#include <stdio.h>
#include <string.h>

/* =========================== 内部状态 =========================== */

UART_HandleTypeDef huart3;
static DMA_HandleTypeDef hdma_usart3_rx;

/* DMA RX 缓冲区: 必须位于 AXI SRAM, 32 字节对齐 */
static uint8_t s_dma_buf[USART3_RX_BUFFER_SIZE]
    __attribute__((section(".ARM.__at_0x24040800"), aligned(32)));

/* 帧缓冲区 (供上层拷出, 普通 SRAM 即可) */
static uint8_t           s_frame_buf[USART3_RX_BUFFER_SIZE + 1];
static volatile uint16_t s_frame_len   = 0;
static volatile uint8_t  s_frame_ready = 0;

/* 上层任务句柄 */
static TaskHandle_t s_rx_task = NULL;

/* 统计 */
static volatile uint32_t s_stat_recv = 0;
static volatile uint32_t s_stat_drop = 0;

/* =========================== USART3 + DMA 初始化 =========================== */

static void USART3_MspInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* PD8 = USART3_TX */
    GPIO_InitStruct.Pin       = GPIO_PIN_8;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PB11 = USART3_RX */
    GPIO_InitStruct.Pin       = GPIO_PIN_11;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* DMA1_Stream1: USART3_RX (避开 LCD 占用的 Stream0) */
    hdma_usart3_rx.Instance                 = DMA1_Stream1;
    hdma_usart3_rx.Init.Request             = DMA_REQUEST_USART3_RX;
    hdma_usart3_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart3_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart3_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart3_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart3_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_usart3_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    hdma_usart3_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart3_rx) != HAL_OK)
    {
        printf("[UART3] DMA init FAILED!\r\n");
        return;
    }

    __HAL_LINKDMA(&huart3, hdmarx, hdma_usart3_rx);

    /* 优先级 (6, x) 在 FreeRTOS 安全区间 */
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

    HAL_NVIC_SetPriority(USART3_IRQn, 6, 1);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void USART3_Init(void)
{
    USART3_MspInit();

    huart3.Instance                    = USART3;
    huart3.Init.BaudRate               = USART3_BAUDRATE;
    huart3.Init.WordLength             = UART_WORDLENGTH_8B;
    huart3.Init.StopBits               = UART_STOPBITS_1;
    huart3.Init.Parity                 = UART_PARITY_NONE;
    huart3.Init.Mode                   = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
        printf("[UART3] HAL init FAILED!\r\n");
        return;
    }

    HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_DisableFifoMode(&huart3);

    memset(s_dma_buf, 0, USART3_RX_BUFFER_SIZE);
    SCB_CleanDCache_by_Addr((uint32_t *)s_dma_buf, USART3_RX_BUFFER_SIZE);

    if (HAL_UART_Receive_DMA(&huart3, s_dma_buf, USART3_RX_BUFFER_SIZE) != HAL_OK)
    {
        printf("[UART3] HAL_UART_Receive_DMA FAILED!\r\n");
        return;
    }

    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);

    printf("[UART3] Init OK (PD8=TX, PB11=RX, %d bps)\r\n", USART3_BAUDRATE);
    printf("[UART3] DMA buffer @ 0x%08X (size %u)\r\n",
           (unsigned int)(uintptr_t)s_dma_buf,
           (unsigned int)USART3_RX_BUFFER_SIZE);
}

/* =========================== 中断 =========================== */

void USART3_IRQ_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* H7 错误标志须显式 ICR 清除, 否则反复触发 */
    uint32_t isr = huart3.Instance->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
    {
        huart3.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF
                             | USART_ICR_NECF  | USART_ICR_PECF;
    }

    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart3);

        uint16_t dma_remain = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
        uint16_t recv_len   = USART3_RX_BUFFER_SIZE - dma_remain;

        if (recv_len == 0)
        {
            /* 空 IDLE, 忽略 */
        }
        else if (s_frame_ready)
        {
            /* 上一帧没消费, 丢弃当前帧, 但 DMA 仍重启避免溢出 */
            s_stat_drop++;
            HAL_UART_DMAStop(&huart3);
            HAL_UART_Receive_DMA(&huart3, s_dma_buf, USART3_RX_BUFFER_SIZE);
        }
        else
        {
            SCB_InvalidateDCache_by_Addr((uint32_t *)s_dma_buf, USART3_RX_BUFFER_SIZE);

            if (recv_len > USART3_RX_BUFFER_SIZE) recv_len = USART3_RX_BUFFER_SIZE;
            memcpy(s_frame_buf, s_dma_buf, recv_len);
            s_frame_buf[recv_len] = '\0';
            s_frame_len   = recv_len;
            s_frame_ready = 1;
            s_stat_recv++;

            HAL_UART_DMAStop(&huart3);
            HAL_UART_Receive_DMA(&huart3, s_dma_buf, USART3_RX_BUFFER_SIZE);

            if (s_rx_task != NULL)
            {
                vTaskNotifyGiveFromISR(s_rx_task, &xHigherPriorityTaskWoken);
            }
        }
    }

    HAL_UART_IRQHandler(&huart3);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* =========================== 对外 API =========================== */

void UART_ESP01_BindRxTask(TaskHandle_t handle)
{
    s_rx_task = handle;
}

int UART_ESP01_TakeFrame(uint8_t *buf, uint16_t max_len, uint16_t *out_len)
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

void UART_ESP01_GetStats(uint32_t *recv, uint32_t *drop)
{
    if (recv) *recv = s_stat_recv;
    if (drop) *drop = s_stat_drop;
}
