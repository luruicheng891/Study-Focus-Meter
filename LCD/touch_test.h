/**
  ******************************************************************************
  * @file    touch_test.h
  * @brief   XPT2046 触摸测试任务 (FreeRTOS) — 公共 API
  ******************************************************************************
  */
#ifndef __TOUCH_TEST_H
#define __TOUCH_TEST_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动触摸测试任务 (由 MX_FREERTOS_Init 调用, 任务内部再调用 TP_Init) */
void TouchTest_Start(void);

/* 异步触发一次重新校准 (可在串口/按键 ISR 中调用) */
void TouchTest_RequestRecalibrate(void);

/* 获取任务句柄 (用于 vTaskDelete / uxTaskGetStackHighWaterMark 等) */
TaskHandle_t TouchTest_GetHandle(void);

/* 处理一行串口命令 (例如 "c\r\n"), 来自 main.c 串口接收回调 */
void TouchTest_ProcessLine(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_TEST_H */
