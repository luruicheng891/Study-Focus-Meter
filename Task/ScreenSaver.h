/**
  ******************************************************************************
  * @file    ScreenSaver.h
  * @brief   屏保 / 自动息屏 模块
  *
  *          行为:
  *            - 在 IDLE / SUMMARY 状态, 超过 SCREENSAVER_TIMEOUT_MS 没有触摸
  *              就关闭 LCD 背光 (PB4 = LOW), 进入息屏状态
  *            - 在 PICKING / LEARNING / PAUSED / AWAY 状态不会息屏
  *            - 息屏状态下, 触摸按下沿立即唤醒 (PB4 = HIGH), 重置计时
  *            - 进入学习会话 (LEARNING) 时, 重置计时器 (避免学习中间突然息屏)
  *
  *          触摸检测入口:
  *            - TouchTask 在按下沿调用 ScreenSaver_NotifyActivity()
  *            - DisplayTask 主循环每帧调用 ScreenSaver_Tick()
  *
  *          背光控制:
  *            - 使用 LCD_LED_ON() / LCD_LED_OFF() (在 LCD/lcd.h 中定义为 PB4)
  *            - 简单 GPIO 高低电平, 不做 PWM 调光
  *
  *          线程安全:
  *            - 所有函数都是简单变量读写 + GPIO 操作, 在 32 位 ARM 上是原子的
  *            - 不需要互斥锁
  ******************************************************************************
  */

#ifndef __SCREEN_SAVER_H
#define __SCREEN_SAVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ======================== 公开 API ======================== */

/**
  * @brief  初始化屏保模块
  * @note   - 记录当前 tick 作为最近活动时间
  *         - 立即打开背光 (确保正常显示)
  *         - 应在 LCD 初始化之后调用
  */
void ScreenSaver_Init(void);

/**
  * @brief  屏保 tick (周期性调用, 建议每 100-500ms)
  * @note   - 检查是否超时, 若超时且允许息屏 → 关闭背光
  *         - 检查当前状态, 若进入非息屏允许状态 → 立即恢复背光
  */
void ScreenSaver_Tick(void);

/**
  * @brief  通知有用户活动 (触摸按下 / 按钮按下)
  * @note   - 唤醒息屏状态 (若已息屏则恢复背光)
  *         - 重置 3 分钟计时器
  *         - 线程安全, 可从 ISR 或任意任务调用
  */
void ScreenSaver_NotifyActivity(void);

/**
  * @brief  手动唤醒 (同 NotifyActivity, 用于测试)
  */
void ScreenSaver_Wakeup(void);

/**
  * @brief  查询当前是否处于息屏状态
  * @retval 1 = 已息屏 (背光关闭), 0 = 正常显示
  */
int ScreenSaver_IsDimmed(void);

/**
  * @brief  获取息屏已持续时间 (ms)
  * @retval 息屏时长, 未息屏时返回 0
  */
uint32_t ScreenSaver_GetDimmedMs(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCREEN_SAVER_H */
