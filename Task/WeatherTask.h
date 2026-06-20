/**
  ******************************************************************************
  * @file    WeatherTask.h
  * @brief   天气数据接收业务层 (ESP-01 WiFi → USART3)
  *
  * 数据链路:
  *   ESP-01 (联网获取天气 JSON) → USART3
  *      └── Communication/UART_ESP01.c 完成分帧
  *           └── Weather_RxTask 解析 JSON → 内部队列 → 提供消费 API
  *
  * 注意: 本文件只负责"接收 + 解析 + 提供数据", 不含任何 LVGL/显示逻辑。
  *       天气仪表板的 UI 在 Task/WeatherDisplay.{c,h}。
  *
  * JSON 字段 (来自 ESP-01):
  *   {"city":"Shanghai","temp":29.0,"humi":65,"feel":31.0,
  *    "cond":"Sunny","wind":3.2,"wdir":"NE","pres":1013,"time":"12:34"}
  ******************************************************************************
  */

#ifndef __WEATHERTASK_H
#define __WEATHERTASK_H

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 数据结构 ============================ */

/* 天气数据结构体 (从 JSON 解析) */
typedef struct {
    char     city[20];      /* 城市名                          */
    int16_t  temp_x10;      /* 温度, 单位 0.1°C (290 = 29.0°C)  */
    uint8_t  humi;          /* 湿度, %                          */
    int16_t  feel_x10;      /* 体感温度, 单位 0.1°C             */
    char     cond[32];      /* 天气状况                         */
    uint16_t wind_x10;      /* 风速, 单位 0.1 m/s               */
    char     wdir[4];       /* 风向                             */
    uint16_t pres;          /* 气压, hPa                        */
    char     time[8];       /* 时间字符串                       */
    uint32_t timestamp;     /* 接收时刻 FreeRTOS tick           */
} WeatherInfo_t;

/* ============================ 任务入口 ============================ */

/**
  * @brief  ESP-01 天气数据接收/解析任务
  *         栈建议 ≥ 512 words, 优先级 osPriorityNormal
  */
void Weather_RxTask(void *pvParameters);

/* ============================ 消费 API ============================ */

/**
  * @brief  非阻塞获取最近一帧天气 (xQueuePeek 语义, 数据保留)
  * @retval 0=成功, -1=尚无数据/未初始化
  */
int Weather_GetLatest(WeatherInfo_t *out);

/**
  * @brief  阻塞等待下一帧天气 (xQueueReceive 语义)
  * @param  timeout  超时 tick (portMAX_DELAY 永久等待)
  * @retval 0=成功, -1=超时/未初始化
  */
int Weather_WaitNew(WeatherInfo_t *out, TickType_t timeout);

/**
  * @brief  读取统计信息 (调试用)
  * @param  recv      累计完整帧数, 可传 NULL
  * @param  drop      底层丢帧数, 可传 NULL
  * @param  parse_err JSON 解析失败次数, 可传 NULL
  */
void Weather_GetStats(uint32_t *recv, uint32_t *drop, uint32_t *parse_err);

#ifdef __cplusplus
}
#endif

#endif /* __WEATHERTASK_H */
