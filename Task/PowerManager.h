/**
  ******************************************************************************
  * @file    PowerManager.h
  * @brief   低功耗管理模块 (STM32H723 Stop 模式 + 多源唤醒)
  *
  *  ── 目标 ───────────────────────────────────────────────────────────────
  *    在"无人学习"的空闲态 (ST_IDLE / ST_SUMMARY) 且屏幕已息屏后, 让 H7 进入
  *    Stop 模式 (PLL/大部分时钟停, SRAM 保留), 把整机功耗从瓦级拉到 mA 级;
  *    在用户需要时秒级唤醒, 恢复 550MHz 满血运行。
  *
  *  ── 唤醒源 ─────────────────────────────────────────────────────────────
  *    1) 触摸 PEN (PB1 / EXTI1, 下降沿)  —— 用户点屏立即唤醒, 完整亮屏
  *    2) RTC 周期唤醒 (WakeUpTimer)      —— 每 POWER_SNIFF_PERIOD_S 秒"嗅探"
  *                                          一次, 不亮屏, 给任务一个时间片去
  *                                          采集传感器/BLE, 若仍无人则继续睡
  *    3) (可选) USART2 BLE 起始位唤醒     —— 见 .c 中 POWER_ENABLE_UART_WAKE,
  *                                          默认关闭 (需把 USART2 内核时钟切到
  *                                          HSI, 会影响现有 BLE 链路, 需硬件验证)
  *
  *  ── 唤醒后恢复序列 (Power_RestoreClock) ────────────────────────────────
  *    HSI(64M) → 重配 VOS0 + HSE + PLL1 → 550MHz;  **不触碰备份域/LSE/RTC**,
  *    因此 RTC 走时在整个休眠期间连续不丢 (LSE 在 Stop 下保持运行)。
  *
  *  ── 与现有模块的关系 ───────────────────────────────────────────────────
  *    - ScreenSaver : 复用其"是否已息屏 + 当前状态是否允许息屏"判据
  *    - StateTask   : 仅 ST_IDLE / ST_SUMMARY 允许深睡; 其余状态强制保持唤醒
  *    - Camera      : 进睡前 OV2640_DCMI_Suspend(), 醒后 OV2640_DCMI_Resume()
  *
  *  ── 线程模型 ───────────────────────────────────────────────────────────
  *    Power_Task (高优先级) 周期轮询条件; 真正进 Stop 时 vTaskSuspendAll() +
  *    __disable_irq() 独占系统, WFI 阻塞直到唤醒中断, 期间其它任务/调度全停。
  *
  *  ⚠ 注意 (踩坑):
  *    - Stop 期间 FreeRTOS tick 冻结 (SysTick 随核心时钟停), 醒后系统 tick
  *      不补偿。这对空闲态无副作用 (无时间敏感任务), 醒后 ScreenSaver 重置。
  *      RTC(LSE) 走时不受影响, 时钟显示依旧准确。
  *    - 进 Stop 前必须 HAL_SuspendTick(), 否则 SysTick 异常会立刻把 CPU 唤醒。
  ******************************************************************************
  */

#ifndef __POWER_MANAGER_H
#define __POWER_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* ======================== 唤醒原因 ======================== */
typedef enum {
    POWER_WAKE_NONE = 0,   /* 尚未休眠 */
    POWER_WAKE_TOUCH,      /* 触摸 PEN (EXTI1) 唤醒 → 完整亮屏 */
    POWER_WAKE_RTC,        /* RTC 周期嗅探唤醒 → 不亮屏, 给任务一个时间片 */
    POWER_WAKE_OTHER       /* 其它中断唤醒 (兜底) */
} PowerWakeReason_t;

/* ======================== 任务入口 ======================== */

/**
  * @brief  低功耗管理任务入口
  * @note   栈建议 >= 256 字; 优先级建议 osPriorityHigh (高于业务任务, 便于
  *         在大家都空闲时抢到 CPU 去执行 Stop 进入序列)。
  *         创建前 ScreenSaver / StateTask / Camera 应已就绪。
  */
void Power_Task(void *pvParameters);

/* ======================== 公共 API ======================== */

/**
  * @brief  初始化低功耗模块 (记录状态, 不立即休眠)
  * @note   在调度器启动前或 Power_Task 入口处调用一次均可
  */
void Power_Init(void);

/**
  * @brief  运行期开关深度睡眠 (例如调试时临时禁用)
  * @param  enable 1=允许进 Stop, 0=禁止 (仅保留 ScreenSaver 息屏)
  */
void Power_SetDeepSleepEnabled(int enable);

/**
  * @brief  查询是否启用了深度睡眠
  */
int Power_IsDeepSleepEnabled(void);

/**
  * @brief  读取统计信息 (用于调试面板/答辩演示)
  * @param  sleep_count    累计进入 Stop 次数 (可为 NULL)
  * @param  total_sleep_s  累计休眠秒数 (可为 NULL)
  * @param  last_reason    最近一次唤醒原因 (可为 NULL)
  */
void Power_GetStats(uint32_t *sleep_count,
                    uint32_t *total_sleep_s,
                    PowerWakeReason_t *last_reason);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_MANAGER_H */
