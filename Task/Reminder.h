/**
  ******************************************************************************
  * @file    Reminder.h
  * @brief   温和提醒模块 — 屏幕上方偶尔浮现的小提示
  *
  *          设计原则:
  *            - 4 类提醒: 持续分心 / 持续疲劳 / 完成 45min 一轮 / 持续坐姿异常
  *            - 触发条件 = StateTask 持续秒数 >= 阈值 (阈值内部宏定义)
  *            - 一旦触发, 同类提醒进入冷却期, 防止反复出现
  *            - 显示风格: 淡入 800ms, 停留 4s, 淡出 1.2s, 全程不打断用户
  *            - 配色全部降饱和, 不使用刺眼红/绿/橙
  *
  *          使用方法:
  *            1. DisplayTask 主循环每帧调 Reminder_Poll()
  *            2. Reminder_Poll 内部读 StateTask 持续秒数, 决定是否触发
  *            3. 显示用 lvgl 控件, 在 lv_layer_top() 上自动浮于学习界面之上
  ******************************************************************************
  */

#ifndef __REMINDER_H
#define __REMINDER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  初始化提醒模块 (创建浮层对象, 必须在 LVGL 初始化后调用)
  */
void Reminder_Init(void);

/**
  * @brief  主循环轮询: 读取持续状态, 决定是否触发提醒
  * @note   DisplayTask 每帧调一次
  */
void Reminder_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __REMINDER_H */
