/**
  ******************************************************************************
  * @file    UART.c
  * @brief   ESP-01 UART 接收驱动 + JSON 解析
  *          USART3 (PD8=TX, PB11=RX) 连接 ESP-01
  *          USART1 用于调试输出 (printf)
  *
  * 接收流程:
  *   ESP-01 → USART3 RX (PB11) → DMA 写入 ring buffer
  *   IDLE 中断触发 → 计算本次接收长度 → 通知任务
  *   任务从 buffer 中拷贝数据 → cJSON 解析 → 队列发送 → 调试打印
  *
  * 重要: STM32H7 DMA 不能访问 DTCM (0x20000000), DMA 缓冲区必须放在
  *       AXI SRAM (0x24000000+), 通过 __attribute__((section)) 强制指定。
  ******************************************************************************
  */

#include "UART.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

/* ========================== 全局变量 ========================== */

UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_rx;

QueueHandle_t xWeatherQueue = NULL;

/* DMA 接收缓冲区 - 必须在 AXI SRAM (DTCM 不可被 DMA 访问!)
 * 紧跟 LCD DMA buffer (0x24040000~0x240407FF) 之后, 起始 0x24040800
 * 大小 256 字节, 32 字节对齐 */
static uint8_t rx_dma_buf[USART3_RX_BUFFER_SIZE]
    __attribute__((section(".ARM.__at_0x24040800"), aligned(32)));

/* 帧缓冲区 (任务读取用, 可以放 DTCM) */
static uint8_t rx_frame_buf[USART3_RX_BUFFER_SIZE + 1];
static volatile uint16_t rx_frame_len = 0;
static volatile uint8_t  rx_frame_ready = 0;

/* 任务句柄 (供中断通知任务) */
static TaskHandle_t weather_task_handle = NULL;

/* ========================== USART3 + DMA 初始化 ========================== */

/**
  * @brief  USART3 MSP 初始化 (GPIO + DMA + NVIC)
  * @note   引脚: PD8=TX (AF7), PB11=RX (AF7)
  */
static void USART3_MspInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能时钟 */
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

    /* DMA1_Stream1 用于 USART3_RX (避开 LCD 占用的 Stream0) */
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
    HAL_DMA_Init(&hdma_usart3_rx);

    __HAL_LINKDMA(&huart3, hdmarx, hdma_usart3_rx);

    /* DMA1_Stream1 中断 (低优先级, FreeRTOS 安全范围: ≥configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5) */
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

    /* USART3 中断 (用于 IDLE 检测) */
    HAL_NVIC_SetPriority(USART3_IRQn, 6, 1);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

/**
  * @brief  初始化 USART3 + DMA + 空闲中断
  */
void USART3_Init(void)
{
    USART3_MspInit();

    huart3.Instance        = USART3;
    huart3.Init.BaudRate   = USART3_BAUDRATE;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits   = UART_STOPBITS_1;
    huart3.Init.Parity     = UART_PARITY_NONE;
    huart3.Init.Mode       = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if(HAL_UART_Init(&huart3) != HAL_OK)
    {
        printf("[UART3] Init FAILED!\r\n");
        return;
    }

    HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_DisableFifoMode(&huart3);

    /* 清空缓冲区 */
    memset(rx_dma_buf, 0, USART3_RX_BUFFER_SIZE);

    /* 启动 DMA 循环接收 */
    HAL_UART_Receive_DMA(&huart3, rx_dma_buf, USART3_RX_BUFFER_SIZE);

    /* 使能 IDLE 中断 (帧结束检测) */
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);

    printf("[UART3] Init OK (PD8=TX, PB11=RX, %d bps)\r\n", USART3_BAUDRATE);
    printf("[UART3] DMA buffer @ 0x%08X\r\n", (unsigned int)rx_dma_buf);
}

/* ========================== 中断处理 ========================== */

/**
  * @brief  USART3 中断 (主要处理 IDLE 中断)
  */
void USART3_IRQ_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if(__HAL_UART_GET_FLAG(&huart3, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart3);

        /* 计算本次 DMA 接收的字节数 */
        uint16_t dma_remain = __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
        uint16_t recv_len = USART3_RX_BUFFER_SIZE - dma_remain;

        if(recv_len > 0 && recv_len <= USART3_RX_BUFFER_SIZE && !rx_frame_ready)
        {
            /* DCache 失效, 确保 CPU 读取到 DMA 写入的最新数据
             * (DMA 缓冲区在 AXI SRAM 且 MPU 配置为 Cacheable) */
            SCB_InvalidateDCache_by_Addr((uint32_t *)rx_dma_buf, USART3_RX_BUFFER_SIZE);

            /* 拷贝到帧缓冲区 */
            memcpy(rx_frame_buf, rx_dma_buf, recv_len);
            rx_frame_buf[recv_len] = '\0';
            rx_frame_len = recv_len;
            rx_frame_ready = 1;

            /* 重启 DMA: 停止再启动以重置指针 */
            HAL_UART_DMAStop(&huart3);
            HAL_UART_Receive_DMA(&huart3, rx_dma_buf, USART3_RX_BUFFER_SIZE);

            /* 通知任务 */
            if(weather_task_handle != NULL)
            {
                vTaskNotifyGiveFromISR(weather_task_handle, &xHigherPriorityTaskWoken);
            }
        }
    }

    /* HAL UART 标准错误处理 */
    HAL_UART_IRQHandler(&huart3);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ========================== JSON 解析 ========================== */

/**
  * @brief  解析天气 JSON 字符串 (使用 cJSON)
  * @retval 0=成功, -1=失败
  */
static int parse_weather_json(const char *json_str, WeatherInfo_t *info)
{
    cJSON *root, *item;

    if(json_str == NULL || info == NULL) return -1;

    /* cJSON 解析 (内存通过 FreeRTOS pvPortMalloc 分配) */
    root = cJSON_Parse(json_str);
    if(root == NULL)
    {
        printf("[Weather] JSON parse error near: %s\r\n", cJSON_GetErrorPtr());
        return -1;
    }

    memset(info, 0, sizeof(WeatherInfo_t));

    /* city */
    item = cJSON_GetObjectItem(root, "city");
    if(item && cJSON_IsString(item))
        strncpy(info->city, item->valuestring, sizeof(info->city) - 1);

    /* temp (float → 0.1°C) */
    item = cJSON_GetObjectItem(root, "temp");
    if(item && cJSON_IsNumber(item))
        info->temp_x10 = (int16_t)(item->valuedouble * 10.0);

    /* humi */
    item = cJSON_GetObjectItem(root, "humi");
    if(item && cJSON_IsNumber(item))
        info->humi = (uint8_t)item->valueint;

    /* feel */
    item = cJSON_GetObjectItem(root, "feel");
    if(item && cJSON_IsNumber(item))
        info->feel_x10 = (int16_t)(item->valuedouble * 10.0);

    /* cond */
    item = cJSON_GetObjectItem(root, "cond");
    if(item && cJSON_IsString(item))
        strncpy(info->cond, item->valuestring, sizeof(info->cond) - 1);

    /* wind */
    item = cJSON_GetObjectItem(root, "wind");
    if(item && cJSON_IsNumber(item))
        info->wind_x10 = (uint16_t)(item->valuedouble * 10.0);

    /* wdir */
    item = cJSON_GetObjectItem(root, "wdir");
    if(item && cJSON_IsString(item))
        strncpy(info->wdir, item->valuestring, sizeof(info->wdir) - 1);

    /* pres */
    item = cJSON_GetObjectItem(root, "pres");
    if(item && cJSON_IsNumber(item))
        info->pres = (uint16_t)item->valueint;

    /* time */
    item = cJSON_GetObjectItem(root, "time");
    if(item && cJSON_IsString(item))
        strncpy(info->time, item->valuestring, sizeof(info->time) - 1);

    cJSON_Delete(root);  /* 释放 cJSON 内存 (调用 vPortFree) */
    return 0;
}

/**
  * @brief  调试打印解析结果到 USART1
  */
static void print_weather_info(const WeatherInfo_t *info)
{
    printf("\r\n========== Weather Info ==========\r\n");
    printf("  City : %s\r\n", info->city);
    printf("  Temp : %d.%d C (feel %d.%d C)\r\n",
           info->temp_x10 / 10, info->temp_x10 % 10,
           info->feel_x10 / 10, info->feel_x10 % 10);
    printf("  Humi : %u%%\r\n", info->humi);
    printf("  Cond : %s\r\n", info->cond);
    printf("  Wind : %u.%u m/s %s\r\n",
           info->wind_x10 / 10, info->wind_x10 % 10, info->wdir);
    printf("  Pres : %u hPa\r\n", info->pres);
    printf("  Time : %s\r\n", info->time);
    printf("==================================\r\n");
}

/* ========================== 接收任务 ========================== */

void Weather_RxTask(void *pvParameters)
{
    WeatherInfo_t info;
    static char local_buf[USART3_RX_BUFFER_SIZE + 1];
    uint16_t local_len;

    (void)pvParameters;

    /* 创建天气数据队列 */
    xWeatherQueue = xQueueCreate(1, sizeof(WeatherInfo_t));
    if(xWeatherQueue == NULL)
    {
        printf("[Weather] Queue create FAILED\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* 保存任务句柄供中断通知 */
    weather_task_handle = xTaskGetCurrentTaskHandle();

    /* 初始化 USART3 */
    USART3_Init();

    printf("[Weather] RxTask started, waiting for JSON data...\r\n");

    for(;;)
    {
        /* 阻塞等待中断通知 (帧结束) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if(rx_frame_ready)
        {
            /* 安全地拷贝帧数据 (避免与中断竞争) */
            taskENTER_CRITICAL();
            local_len = rx_frame_len;
            if(local_len > USART3_RX_BUFFER_SIZE) local_len = USART3_RX_BUFFER_SIZE;
            memcpy(local_buf, (const void *)rx_frame_buf, local_len);
            local_buf[local_len] = '\0';
            rx_frame_ready = 0;
            taskEXIT_CRITICAL();

            /* 调试打印原始数据 */
            printf("\r\n[UART3 RX %u bytes] %s\r\n", (unsigned int)local_len, local_buf);

            /* 解析 JSON */
            if(parse_weather_json(local_buf, &info) == 0)
            {
                info.timestamp = xTaskGetTickCount();

                /* 发送到队列供 UI 任务消费 */
                xQueueOverwrite(xWeatherQueue, &info);

                /* 调试打印 */
                print_weather_info(&info);
            }
        }
    }
}
