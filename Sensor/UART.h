/**
  ******************************************************************************
  * @file    UART.h
  * @brief   ESP-01 UART 接收驱动 (USART3 + DMA + 空闲中断)
  *          接收 JSON 格式天气数据, 解析后通过队列传递给显示任务
  ******************************************************************************
  */

#ifndef __UART_H
#define __UART_H

#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>

/* USART3 引脚定义: PB10=TX, PB11=RX */
#define USART3_BAUDRATE        115200
#define USART3_RX_BUFFER_SIZE  256

/* 天气数据结构体 (从 JSON 解析) */
typedef struct {
    char     city[20];      // 城市名
    int16_t  temp_x10;     // 温度, 单位 0.1°C (290 = 29.0°C)
    uint8_t  humi;         // 湿度, %
    int16_t  feel_x10;    // 体感温度, 单位 0.1°C
    char     cond[32];     // 天气状况
    uint16_t wind_x10;    // 风速, 单位 0.1 m/s
    char     wdir[4];      // 风向
    uint16_t pres;         // 气压, hPa
    char     time[8];      // 时间字符串
    uint32_t timestamp;   // 接收时刻 tick
} WeatherInfo_t;

/* 全局队列句柄 */
extern QueueHandle_t xWeatherQueue;

/* 全局 UART 句柄 */
extern UART_HandleTypeDef huart3;

/**
  * @brief  初始化 USART3 (用于 ESP-01 接收)
  */
void USART3_Init(void);

/**
  * @brief  ESP-01 数据接收+解析任务
  */
void Weather_RxTask(void *pvParameters);

#endif
