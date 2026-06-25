/**
  ******************************************************************************
  * @file    LearningScreen.h
  * @brief   学习专注界面 — 现代极简版 (智能台灯式陪伴)
  *          320x240 柔和深色底, 大量留白, 大字时间, 弱化控件.
  *          元素:
  *            - 顶部极简状态条 (学习圆点 / 状态文字 / 时钟 / 摄像头缩略图 / DBG 按钮)
  *            - 内环: 专注度 (2s 呼吸动画 + 2.5s 状态色渐变)
  *            - 外环: 学习进度 (3s 慢填充, 柔青)
  *            - 中央时间 (28pt 柔白) + 目标分钟 (12pt 暗灰)
  *            - 环境信息卡 (温度 / 湿度 / 光照, 极淡卡片, 5s 节流)
  *            - 噪音波形 (底部细线, 安静时近静态)
  *            - 极小 Pause/End 按钮 (右下角, 暗灰不抢眼)
  *          调试特性:
  *            - 顶部 "DBG" 小按钮: 点击展开 / 隐藏左侧调试面板
  *            - 调试面板显示: F/D/T 概率、综合分、视觉分、坐姿状态、压力、帧号
  *          上层温和提示由 Reminder.c 浮层叠加.
  *
  * 中央时间格式:
  *            elapsed < 60s   -> "0:SS"   (例 "0:42")
  *            elapsed >= 60s  -> "M"      (例 "25")
  *
  * 依赖: StateTask (学习状态机), WeatherTask (时钟), SensorTask (声音波形)
  *       FusionTask (AI 模型结果, 调试用), lvgl, FreeRTOS.
  *
  * 对外暴露:
  *   LearningScreen_Init()          - 构建界面 (一次)
  *   LearningScreen_GetScreen()     - 取得屏幕对象 (给 DisplayTask 切屏用)
  *   LearningScreen_UpdateStatus()  - 状态文字 / 按钮文字 / 圆环颜色
  *   LearningScreen_UpdateSensor()  - 声音波形 + 环境信息卡
  *   LearningScreen_UpdateStopwatch() - 中央秒表 + 进度环
  *   LearningScreen_RefreshBar()    - 顶部 WiFi / 时钟
  *   LearningScreen_UpdateFusion()  - AI 模型结果 → 调试面板
  ******************************************************************************
  */

#ifndef __LEARNING_SCREEN_H
#define __LEARNING_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "SensorTask.h"   /* SensorData_t */
#include "FusionTask.h"    /* FusionResult_t (调试面板用) */

/* 共享: 远端数据时间戳 (Display.c 维护, 这里只读) */
extern volatile uint32_t g_last_remote_tick;

/* ======================== 公开 API ======================== */

/* 构建学习界面 (创建屏幕和所有控件, 内部一次性) */
void LearningScreen_Init(void);

/* 返回学习屏幕 lv_obj_t, 给 DisplayTask 用 lv_scr_load 切到该屏 */
lv_obj_t *LearningScreen_GetScreen(void);

/* 状态文字 + 暂停/结束按钮 同步当前学习状态机状态 */
void LearningScreen_UpdateStatus(void);

/* 传感器数据更新 (噪音波形 + 环境信息卡) */
void LearningScreen_UpdateSensor(const SensorData_t *d);

/* 中央大时间更新 (按秒, 状态机驱动, 暂停时自动停) */
void LearningScreen_UpdateStopwatch(void);

/* 顶部 WiFi + 时钟刷新 (基于 g_last_remote_tick + Weather_GetLatest) */
void LearningScreen_RefreshBar(void);

/* 推送 AI 融合结果 (调试面板展开时显示 F/D/T 概率、评分、坐姿、压力、帧号) */
void LearningScreen_UpdateFusion(const FusionResult_t *f);

#ifdef __cplusplus
}
#endif

#endif /* __LEARNING_SCREEN_H */
