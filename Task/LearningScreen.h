/**
  ******************************************************************************
  * @file    LearningScreen.h
  * @brief   学习专注界面 LVGL 模块 (从 Display.c 抽出)
  *          320x240 黑底极简风: 顶部 WiFi/时钟/状态条, 中央大时间,
  *          3 条彩色传感器条 (T/H/L), 底部 Pause/End 按钮.
  *          中央时间格式:
  *            elapsed < 60s   -> "0:SS"   (例 "0:42")
  *            elapsed >= 60s  -> "M"      (例 "25")
  *          传感器条节流:
  *            温度/湿度: 1 分钟更新一次 (温度不会突变)
  *            光照:      3 秒更新一次   (环境光变化较快)
  *
  * 依赖: StateTask (学习状态机), WeatherTask (时钟), SensorTask (T/H/L)
  *       lvgl, FreeRTOS.
  *
  * 对外暴露:
  *   LearningScreen_Init()          - 构建界面 (一次)
  *   LearningScreen_GetScreen()     - 取得屏幕对象 (给 DisplayTask 切屏用)
  *   LearningScreen_UpdateStatus()  - 状态文字 / 按钮文字 / 按钮 enable
  *   LearningScreen_UpdateSensor()  - T/H/L 传感器 (温湿度 1 分钟, 光照 3 秒)
  *   LearningScreen_UpdateStopwatch() - 中央大时间 (按秒)
  *   LearningScreen_RefreshBar()    - 顶部 WiFi / 时钟刷新
  ******************************************************************************
  */

#ifndef __LEARNING_SCREEN_H
#define __LEARNING_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "SensorTask.h"   /* SensorData_t */

/* 共享: 远端数据时间戳 (Display.c 维护, 这里只读) */
extern volatile uint32_t g_last_remote_tick;

/* ======================== 公开 API ======================== */

/* 构建学习界面 (创建屏幕和所有控件, 内部一次性) */
void LearningScreen_Init(void);

/* 返回学习屏幕 lv_obj_t, 给 DisplayTask 用 lv_scr_load 切到该屏 */
lv_obj_t *LearningScreen_GetScreen(void);

/* 状态文字 + 暂停/结束按钮 同步当前学习状态机状态 */
void LearningScreen_UpdateStatus(void);

/* 传感器数据更新 (T/H/L 进度条 + 数值: 温湿度 1 分钟, 光照 3 秒) */
void LearningScreen_UpdateSensor(const SensorData_t *d);

/* 中央大时间更新 (按秒, 状态机驱动, 暂停时自动停) */
void LearningScreen_UpdateStopwatch(void);

/* 顶部 WiFi + 时钟刷新 (基于 g_last_remote_tick + Weather_GetLatest) */
void LearningScreen_RefreshBar(void);

#ifdef __cplusplus
}
#endif

#endif /* __LEARNING_SCREEN_H */
