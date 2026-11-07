/**
  ******************************************************************************
  * @file    WeatherScreen.h
  * @brief   天气预报子界面 — 网络天气详情页
  *
  *          320x240 布局:
  *          +--------------------------------------------+
  *          | [< Back]      Weather Forecast             |  顶部状态栏
  *          +--------------------------------------------+
  *          |  City: Shanghai                            |
  *          |    Temp 27 C        Humi 65 %               |  大字
  *          |    Condition: Sunny                        |
  *          |    Feel: 31 C                              |
  *          |    Wind: 3.2 m/s NE    Pres: 1013 hPa      |
  *          |    Updated: 12:34                          |
  *          +--------------------------------------------+
  *
  *          数据源: WeatherTask 通过 Weather_GetLatest() 提供 WeatherInfo_t
  *          入口: 主界面 (Display.c) 点击 Weather 按钮切换到此屏
  *          出口: 本屏左上角 Back 按钮 → 切回主界面
  *
  * 对外暴露:
  *   WeatherScreen_Init()              - 构建界面 (一次)
  *   WeatherScreen_GetScreen()         - 取得屏幕对象 (给 DisplayTask 切屏)
  *   WeatherScreen_Update(w)           - 用最新天气数据刷新界面
  *   WeatherScreen_SetBackCallback(cb) - 注册 Back 按钮回调
  ******************************************************************************
  */
#ifndef __WEATHER_SCREEN_H
#define __WEATHER_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "WeatherTask.h"   /* WeatherInfo_t */

/* 构建天气子界面 (创建屏幕和所有控件, 一次性) */
void WeatherScreen_Init(void);

/* 返回天气屏幕 lv_obj_t, 给 DisplayTask 用 lv_scr_load 切到该屏 */
lv_obj_t *WeatherScreen_GetScreen(void);

/* 用最新一帧天气数据刷新界面所有字段; w 为 NULL 时显示 "--" 占位 */
void WeatherScreen_Update(const WeatherInfo_t *w);

/* 注册 Back 按钮回调 (点击 Back 时调用, 通常切回主界面) */
void WeatherScreen_SetBackCallback(void (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif /* __WEATHER_SCREEN_H */
