/**
  ******************************************************************************
  * @file    WeatherTask.c
  * @brief   天气数据接收业务层 (ESP-01 WiFi → USART3)
  *
  * 职责:
  *   - 启动 USART3 底层驱动 (Communication/UART_ESP01.c)
  *   - 被驱动 ISR 唤醒后取出原始 JSON 字节流
  *   - cJSON 解析 → WeatherInfo_t → 长度 1 队列 (xQueueOverwrite)
  *   - 通过 Weather_GetLatest / Weather_WaitNew 把最新天气暴露给 UI 任务
  *
  * 与底层驱动的边界:
  *   - 本文件不直接操作 USART3 / DMA / 中断, 也不引用驱动内部缓冲。
  *   - 只通过 UART_ESP01_BindRxTask + UART_ESP01_TakeFrame 与驱动对话。
  ******************************************************************************
  */

#include "WeatherTask.h"
#include "UART_ESP01.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

/* =========================== 内部状态 =========================== */

/* 长度 1 的队列, 始终保留最新一帧 */
static QueueHandle_t s_queue = NULL;

/* 统计 */
static volatile uint32_t s_stat_parse_err = 0;

/* =========================== JSON 解析 =========================== */

/**
  * @brief  解析天气 JSON 字符串 (使用 cJSON)
  * @retval 0=成功, -1=失败
  */
static int parse_weather_json(const char *json_str, WeatherInfo_t *info)
{
    cJSON *root, *item;

    if (json_str == NULL || info == NULL) return -1;

    /* cJSON 解析 (内存通过 FreeRTOS pvPortMalloc 分配) */
    root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        const char *err = cJSON_GetErrorPtr();
        printf("[Weather] JSON parse error near: %s\r\n", err ? err : "(null)");
        return -1;
    }

    memset(info, 0, sizeof(WeatherInfo_t));

    item = cJSON_GetObjectItem(root, "city");
    if (item && cJSON_IsString(item))
        strncpy(info->city, item->valuestring, sizeof(info->city) - 1);

    item = cJSON_GetObjectItem(root, "temp");
    if (item && cJSON_IsNumber(item))
        info->temp_x10 = (int16_t)(item->valuedouble * 10.0);

    item = cJSON_GetObjectItem(root, "humi");
    if (item && cJSON_IsNumber(item))
        info->humi = (uint8_t)item->valueint;

    item = cJSON_GetObjectItem(root, "feel");
    if (item && cJSON_IsNumber(item))
        info->feel_x10 = (int16_t)(item->valuedouble * 10.0);

    item = cJSON_GetObjectItem(root, "cond");
    if (item && cJSON_IsString(item))
        strncpy(info->cond, item->valuestring, sizeof(info->cond) - 1);

    item = cJSON_GetObjectItem(root, "wind");
    if (item && cJSON_IsNumber(item))
        info->wind_x10 = (uint16_t)(item->valuedouble * 10.0);

    item = cJSON_GetObjectItem(root, "wdir");
    if (item && cJSON_IsString(item))
        strncpy(info->wdir, item->valuestring, sizeof(info->wdir) - 1);

    item = cJSON_GetObjectItem(root, "pres");
    if (item && cJSON_IsNumber(item))
        info->pres = (uint16_t)item->valueint;

    item = cJSON_GetObjectItem(root, "time");
    if (item && cJSON_IsString(item))
        strncpy(info->time, item->valuestring, sizeof(info->time) - 1);

    cJSON_Delete(root);  /* 释放 cJSON 内存 (调用 vPortFree) */
    return 0;
}

/**
  * @brief  调试打印解析结果到 USART1
  */
static void print_weather_info(const WeatherInfo_t *info)
{
    uint32_t recv = 0, drop = 0;
    UART_ESP01_GetStats(&recv, &drop);

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
    printf("  Stat : recv=%lu drop=%lu err=%lu\r\n",
           (unsigned long)recv, (unsigned long)drop,
           (unsigned long)s_stat_parse_err);
    printf("==================================\r\n");
}

/* =========================== 任务主体 =========================== */

void Weather_RxTask(void *pvParameters)
{
    WeatherInfo_t info;
    static char   local_buf[USART3_RX_BUFFER_SIZE + 1];
    uint16_t      local_len = 0;

    (void)pvParameters;

    s_queue = xQueueCreate(1, sizeof(WeatherInfo_t));
    if (s_queue == NULL)
    {
        printf("[Weather] Queue create FAILED\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* 注册自己给底层驱动, 收到完整帧后被唤醒 */
    UART_ESP01_BindRxTask(xTaskGetCurrentTaskHandle());

    /* 初始化 USART3 (在 Bind 之后, 避免极早期帧丢失) */
    USART3_Init();

    printf("[Weather] RxTask started, waiting for JSON data...\r\n");

    for (;;)
    {
        /* 阻塞等待 ISR 唤醒 (帧结束) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* 从底层取出原始字节 */
        if (UART_ESP01_TakeFrame((uint8_t *)local_buf,
                                 sizeof(local_buf),
                                 &local_len) != 0)
        {
            continue;
        }

        printf("\r\n[UART3 RX %u bytes] %s\r\n",
               (unsigned int)local_len, local_buf);

        if (parse_weather_json(local_buf, &info) == 0)
        {
            info.timestamp = xTaskGetTickCount();
            xQueueOverwrite(s_queue, &info);
            print_weather_info(&info);
        }
        else
        {
            s_stat_parse_err++;
        }
    }
}

/* =========================== 消费 API =========================== */

int Weather_GetLatest(WeatherInfo_t *out)
{
    if (out == NULL || s_queue == NULL) return -1;
    return (xQueuePeek(s_queue, out, 0) == pdPASS) ? 0 : -1;
}

int Weather_WaitNew(WeatherInfo_t *out, TickType_t timeout)
{
    if (out == NULL || s_queue == NULL) return -1;
    return (xQueueReceive(s_queue, out, timeout) == pdPASS) ? 0 : -1;
}

void Weather_GetStats(uint32_t *recv, uint32_t *drop, uint32_t *parse_err)
{
    UART_ESP01_GetStats(recv, drop);
    if (parse_err) *parse_err = s_stat_parse_err;
}
