/**
  ******************************************************************************
  * @file    SlaveRxTask.c
  * @brief   从机 (ESP32 + 姿态分析) 数据接收/解析任务
  *
  * 职责:
  *   - 绑定 UART_ESP32 驱动并启动
  *   - 等待 ISR 通知完整帧到达
  *   - 从驱动层拉取原始 JSON 字节
  *   - 解析 JSON 为 SlaveData_t，发布到长度为 1 的队列 (覆盖写入)
  *   - 提供 Slave_GetLatest / Slave_WaitNew 供下游消费者使用
  *     (显示任务、融合任务等)
  *
  * JSON 协议 (新):
  *   {"ts":12345678,"state":"focused","score":85,"pressure":45.2}
  ******************************************************************************
  */

#include "SlaveRxTask.h"
#include "UART_ESP32.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

/* =========================== 内部状态 =========================== */

/* 长度为 1 的队列，始终保留最新一帧
 * (xQueueOverwrite 生产者 + xQueuePeek/xQueueReceive 消费者) */
static QueueHandle_t s_queue = NULL;

/* 统计信息 */
static volatile uint32_t s_stat_parse_err = 0;

/* =========================== JSON 辅助函数 =========================== */

/**
  * @brief  容忍 '}' 或 ']' 前的尾随逗号
  *         严格 JSON 禁止 "{\"a\":1,}" 这种格式，但从机有时因 sprintf
  *         拼接而发出这种格式。在交给 cJSON 前将这些逗号替换为空格
  */
static void sanitize_trailing_comma(char *s)
{
    if (s == NULL) return;
    char *p = s;
    while (*p)
    {
        if (*p == ',')
        {
            char *q = p + 1;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (*q == '}' || *q == ']')
            {
                *p = ' ';
            }
        }
        p++;
    }
}

/**
  * @brief  解析从机发送的一帧 JSON 数据
  * @retval 0 成功，-1 失败
  *
  * 期望字段: ts (数字), state (字符串), score (数字),
  *           pressure (数字)
  * 缺失字段保持默认的零值/空值
  */
static int parse_slave_json(char *json_str, SlaveData_t *out)
{
    if (json_str == NULL || out == NULL) return -1;

    sanitize_trailing_comma(json_str);

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        const char *err = cJSON_GetErrorPtr();
        printf("[从机] JSON 解析错误位置: %s\r\n", err ? err : "(空)");
        return -1;
    }

    memset(out, 0, sizeof(*out));

    cJSON *item = cJSON_GetObjectItem(root, "ts");
    if (cJSON_IsNumber(item)) out->ts = (uint64_t)item->valuedouble;

    item = cJSON_GetObjectItem(root, "state");
    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        strncpy(out->state, item->valuestring, SLAVE_STATE_STR_LEN - 1);
        out->state[SLAVE_STATE_STR_LEN - 1] = '\0';
    }

    item = cJSON_GetObjectItem(root, "score");
    if (cJSON_IsNumber(item))
    {
        int32_t v = (int32_t)item->valuedouble;
        if (v < 0)   v = 0;
        if (v > 100) v = 100;
        out->score = v;
    }

    item = cJSON_GetObjectItem(root, "pressure");
    if (cJSON_IsNumber(item)) out->pressure = (float)item->valuedouble;

    cJSON_Delete(root);
    return 0;
}

/**
  * @brief  美打印一帧数据到 USART1 (使用非阻塞 fputc)
  */
static void debug_print(const SlaveData_t *d)
{
    uint32_t ts_hi = (uint32_t)(d->ts >> 32);
    uint32_t ts_lo = (uint32_t)(d->ts & 0xFFFFFFFFu);
    uint32_t recv = 0, drop = 0;
    UART_ESP32_GetStats(&recv, &drop);

    printf("\r\n========== 从机帧 #%lu ==========\r\n",
           (unsigned long)d->seq);

    if (ts_hi)
        printf("  时间戳   : 0x%08lX%08lX 毫秒\r\n",
               (unsigned long)ts_hi, (unsigned long)ts_lo);
    else
        printf("  时间戳   : %lu 毫秒\r\n", (unsigned long)ts_lo);

    printf("  状态     : %s\r\n", d->state[0] ? d->state : "(空)");
    printf("  评分     : %ld / 100\r\n", (long)d->score);
    printf("  压力值   : %8.3f\r\n", (double)d->pressure);
    printf("  统计     : 接收=%lu 丢弃=%lu 解析错误=%lu\r\n",
           (unsigned long)recv,
           (unsigned long)drop,
           (unsigned long)s_stat_parse_err);
    printf("======================================\r\n");
}

/* =========================== 任务主体 =========================== */

void Slave_RxTask(void *pvParameters)
{
    static char  local_buf[USART2_RX_BUFFER_SIZE + 1];
    SlaveData_t  data;
    uint16_t     local_len = 0;
    uint32_t     seq       = 0;

    (void)pvParameters;

    s_queue = xQueueCreate(1, sizeof(SlaveData_t));
    if (s_queue == NULL)
    {
        printf("[从机] 队列创建失败\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* 向驱动注册自身任务句柄，然后启动 UART
     * 绑定必须在 USART2_Init() 之前完成，以免丢失早期帧 */
    UART_ESP32_BindRxTask(xTaskGetCurrentTaskHandle());
    USART2_Init();

    printf("[从机] 接收任务已启动，等待 ESP32 BLE JSON 数据...\r\n");
    for (;;)
    {
        /* 阻塞等待 ISR 通知完整帧到达 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (UART_ESP32_TakeFrame((uint8_t *)local_buf,
                                 sizeof(local_buf),
                                 &local_len) != 0)
        {
            continue;
        }

        printf("\r\n[UART2 接收 %u 字节] %s\r\n",
               (unsigned int)local_len, local_buf);

        if (parse_slave_json(local_buf, &data) == 0)
        {
            data.local_tick = xTaskGetTickCount();
            data.seq        = ++seq;

            xQueueOverwrite(s_queue, &data);
            debug_print(&data);
        }
        else
        {
            s_stat_parse_err++;
        }
    }
}

/* =========================== 消费者 API =========================== */

int Slave_GetLatest(SlaveData_t *out)
{
    if (out == NULL || s_queue == NULL) return -1;
    /* Peek: 数据保留在队列中，允许多个消费者查看 */
    return (xQueuePeek(s_queue, out, 0) == pdPASS) ? 0 : -1;
}

int Slave_WaitNew(SlaveData_t *out, TickType_t timeout)
{
    if (out == NULL || s_queue == NULL) return -1;
    return (xQueueReceive(s_queue, out, timeout) == pdPASS) ? 0 : -1;
}

void Slave_GetStats(uint32_t *recv, uint32_t *drop, uint32_t *parse_err)
{
    UART_ESP32_GetStats(recv, drop);
    if (parse_err) *parse_err = s_stat_parse_err;
}