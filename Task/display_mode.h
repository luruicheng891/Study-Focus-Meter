/**
  ******************************************************************************
  * @file    display_mode.h
  * @brief   屏幕显示模式运行时切换 (摄像头画面 / 天气仪表板)
  *          通过串口 1 发送字符 'C'/'W' 控制
  ******************************************************************************
  */
#ifndef __DISPLAY_MODE_H
#define __DISPLAY_MODE_H

#include <stdint.h>

typedef enum {
    DISP_MODE_CAMERA = 0,   /* 显示摄像头画面 (隐藏天气 UI) */
    DISP_MODE_WEATHER = 1,  /* 显示天气仪表板 (隐藏摄像头) */
} DisplayMode_t;

/* 当前显示模式 (volatile, ISR/任务共享) */
extern volatile DisplayMode_t g_display_mode;

/**
 * @brief 请求切换显示模式 (可在 ISR 中调用)
 * @note  实际 LVGL UI 切换在 Weather_Task 中执行, 保证线程安全
 */
void DisplayMode_Request(DisplayMode_t mode);

/**
 * @brief 获取并清除待处理的模式切换请求
 * @retval 1 = 有新请求, *out_mode 有效; 0 = 无请求
 * @note   仅 LVGL 任务调用
 */
int DisplayMode_TakePending(DisplayMode_t *out_mode);

/**
 * @brief 启动 USART1 单字节中断接收, 处理外部命令
 *        在 main.c 中, USART1_Init() 完成后调用
 */
void DisplayMode_StartUart1Rx(void);

#endif
