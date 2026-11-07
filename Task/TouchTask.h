/**
  ******************************************************************************
  * @file    TouchTask.h
  * @brief   XPT2046 触摸任务 (FreeRTOS)
  *
  * 职责:
  *   1) 启动时调用 TP_Init() 完成 XPT2046 初始化 + 自动校准
  *   2) 注册 LVGL pointer 类型 indev, 让 LVGL 控件可被触摸操作
  *   3) 周期 (20ms) 扫描触摸点, 同步给 indev read_cb
  *   4) 长按视觉右上角 60x60 区域 ≥ 2s 触发重新校准
  *   5) 串口命令: 'c' 重新校准 / 'p' 打印参数 / 'r' 读原始 ADC / 'h' 帮助
  *
  * 任务入口由 freertos.c 通过 xTaskCreate(Touch_Task, ...) 启动,
  * 风格与 Sensor_Task / Weather_RxTask / Slave_RxTask 保持一致。
  ******************************************************************************
  */

#ifndef __TOUCH_TASK_H
#define __TOUCH_TASK_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  触摸任务入口
  *         栈建议 ≥ 512 words, 优先级 osPriorityNormal
  */
void Touch_Task(void *pvParameters);

/**
  * @brief  异步请求一次重新校准 (可在串口/按键 ISR 中调用)
  */
void Touch_RequestRecalibrate(void);

/**
  * @brief  获取任务句柄 (用于 vTaskDelete / uxTaskGetStackHighWaterMark 等)
  * @retval 任务尚未启动时返回 NULL
  */
TaskHandle_t Touch_GetHandle(void);

/**
  * @brief  处理一行串口命令 (例如 "c\r\n"), 来自 USART1 接收回调
  */
void Touch_ProcessLine(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_TASK_H */
