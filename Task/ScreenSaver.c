/**
  ******************************************************************************
  * @file    ScreenSaver.c
  * @brief   屏保 / 自动息屏 实现
  *
  *          状态机:
  *            BRIGHT (正常显示) --3min 无活动 + 允许息屏状态--> DIM (息屏)
  *            DIM   --任意触摸按下沿-->  BRIGHT
  *            BRIGHT --进入不允许息屏状态 (PICKING/LEARNING/...)--> 强制 BRIGHT
  *
  *          状态切换时打印日志 (debug 用)
  ******************************************************************************
  */

#include "ScreenSaver.h"
#include "StateTask.h"      /* State_GetCurrent() */
#include "lcd.h"            /* LCD_LED_ON() / LCD_LED_OFF() */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

/* ======================== 配置 ======================== */
#define SCREENSAVER_TIMEOUT_MS   (3 * 60 * 1000)  /* 3 分钟无活动 → 息屏 */

/* ======================== 内部状态 ======================== */
static volatile uint32_t s_last_activity_tick = 0;  /* 最近一次活动时间 (FreeRTOS tick) */
static volatile uint8_t  s_is_dimmed          = 0;  /* 1 = 已息屏, 0 = 正常 */
static volatile uint32_t s_dim_start_tick     = 0;  /* 息屏开始时刻 (用于统计时长) */

/* ======================== 内部小工具 ======================== */

/**
  * @brief  判断当前状态是否允许息屏
  * @retval 1 = 允许 (IDLE / SUMMARY)
  *         0 = 不允许 (PICKING / LEARNING / PAUSED / AWAY)
  */
static int is_dim_allowed(LearningState_t s)
{
    return (s == ST_IDLE || s == ST_SUMMARY) ? 1 : 0;
}

/**
  * @brief  关闭背光 + 标记息屏
  */
static void enter_dim(void)
{
    if (s_is_dimmed) return;
    s_is_dimmed      = 1;
    s_dim_start_tick = xTaskGetTickCount();
    LCD_LED_OFF();
    printf("[ScreenSaver] -> DIM (backlight OFF)\r\n");
}

/**
  * @brief  打开背光 + 清除息屏标记
  */
static void exit_dim(void)
{
    if (!s_is_dimmed) return;
    s_is_dimmed = 0;
    LCD_LED_ON();
    printf("[ScreenSaver] -> BRIGHT (backlight ON, dimmed %lu ms)\r\n",
           (unsigned long)((xTaskGetTickCount() - s_dim_start_tick) * 1000U
                           / configTICK_RATE_HZ));
}

/* ======================== 公开 API ======================== */

void ScreenSaver_Init(void)
{
    s_last_activity_tick = xTaskGetTickCount();
    s_is_dimmed          = 0;
    s_dim_start_tick     = 0;
    LCD_LED_ON();  /* 确保开机时背光开 */
    printf("[ScreenSaver] Init OK, timeout=%u ms\r\n",
           (unsigned)SCREENSAVER_TIMEOUT_MS);
}

void ScreenSaver_NotifyActivity(void)
{
    s_last_activity_tick = xTaskGetTickCount();
    if (s_is_dimmed) {
        exit_dim();  /* 唤醒 */
    }
}

void ScreenSaver_Wakeup(void)
{
    ScreenSaver_NotifyActivity();
}

int ScreenSaver_IsDimmed(void)
{
    return s_is_dimmed;
}

uint32_t ScreenSaver_GetDimmedMs(void)
{
    if (!s_is_dimmed) return 0;
    TickType_t now = xTaskGetTickCount();
    return (uint32_t)((now - s_dim_start_tick) * 1000U / configTICK_RATE_HZ);
}

void ScreenSaver_Tick(void)
{
    LearningState_t s = State_GetCurrent();

    /* 1) 当前状态不允许息屏 → 强制保持亮屏 + 重置计时 */
    if (!is_dim_allowed(s)) {
        s_last_activity_tick = xTaskGetTickCount();
        if (s_is_dimmed) {
            exit_dim();
        }
        return;
    }

    /* 2) 已经在息屏 → 等待触摸唤醒 (NotifyActivity 负责), 这里不做处理 */
    if (s_is_dimmed) {
        return;
    }

    /* 3) 检查是否超时 */
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_ms = (uint32_t)((now - s_last_activity_tick) * 1000U
                                     / configTICK_RATE_HZ);
    if (elapsed_ms >= SCREENSAVER_TIMEOUT_MS) {
        enter_dim();
    }
}
