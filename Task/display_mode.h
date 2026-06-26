/**
  ******************************************************************************
  * @file    display_mode.h
  * @brief   屏幕显示模式运行时切换 (摄像头画面 / 天气仪表板)
  *
  *          USART1 RX 命令通道已禁用, 不再响应外部 'C'/'W' 字符.
  *          模式切换仅通过 DisplayMode_Request() API (任务上下文) 完成,
  *          例如: 摄像头画面上的按钮回调, 天气仪表板上的按钮回调.
  ******************************************************************************
  */
#ifndef __DISPLAY_MODE_H
#define __DISPLAY_MODE_H

#include <stdint.h>

typedef enum {
    DISP_MODE_CAMERA = 0,   /* 显示摄像头画面 (隐藏天气 UI) */
    DISP_MODE_WEATHER = 1,  /* 显示天气仪表板 (隐藏摄像头) */
} DisplayMode_t;

/* 当前显示模式 (volatile, 任务共享) */
extern volatile DisplayMode_t g_display_mode;

/**
 * @brief 请求切换显示模式
 * @note  实际 LVGL UI 切换在 DisplayTask 中执行, 保证线程安全
 */
void DisplayMode_Request(DisplayMode_t mode);

/**
 * @brief 获取并清除待处理的模式切换请求
 * @retval 1 = 有新请求, *out_mode 有效; 0 = 无请求
 * @note   仅 LVGL 任务调用
 */
int DisplayMode_TakePending(DisplayMode_t *out_mode);

/**
 * @brief 启动 USART1 (兼容旧接口, 当前实现为关闭 RX 命令通道)
 *        在 main.c 中, USART1_Init() 完成后调用
 */
void DisplayMode_StartUart1Rx(void);

#endif
