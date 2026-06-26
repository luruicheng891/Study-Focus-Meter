/**
  ******************************************************************************
  * @file    StateTask.h
  * @brief   学习会话状态机 (IDLE / LEARNING / AWAY / PAUSED / SUMMARY)
  *
  *          设计要点:
  *            - 事件驱动: 任何模块通过 State_PostEvent() 投递事件
  *            - 单任务处理: 避免多任务并发改状态
  *            - 阈值可调: 压力阈值 / AWAY 检测 / AWAY 超时全部宏定义
  *
  *          数据源:
  *            - SlaveRxTask   : 压力值 / 坐姿评分
  *            - FusionTask    : 用于汇总专注度统计
  *            - GUI (Display) : 通过 State_PostEvent() 投递用户操作
  *
  *          输出控制:
  *            - BLE (USART2)  : 状态切换时向从机发送单字符指令
  *                              'A'=开始接收 (确认时长进入 LEARNING),
  *                              'Z'=停止接收 (暂停 / 学习结束),
  *                              'S'=休眠 (预留, 经 Slave_Sleep() 触发)
  *            - SessionSummary : 结束时填好统计, GUI 可读取
  *
  *          状态转移图 (主流程):
  *
  *             [IDLE] --GUI_START--> [PICKING] --GUI_CONFIRM_DURATION(min)--> [LEARNING]
  *                ^                     |                                          |
  *                | GUI_BACK            | GUI_END (cancel)                         | GUI_END / 5min timeout
  *             [SUMMARY] <--- GUI_BACK -+                                       [AWAY] <-- pressure lost 30s
  *                ^                                                                      |
  *                |                                                                      | pressure back
  *                +--------- GUI_BACK -------------------------------------------------+--> [LEARNING]
  *
  *             [LEARNING] --GUI_PAUSE--> [PAUSED] --GUI_RESUME--> [LEARNING]
  ******************************************************************************
  */

#ifndef __STATE_TASK_H
#define __STATE_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 状态枚举 ======================== */

typedef enum {
    ST_IDLE = 0,        /* 空闲, 等待用户点击开始 */
    ST_PICKING,         /* 学习中选时长, 等待用户/超时确认 */
    ST_LEARNING,        /* 学习中, 全系统运行 */
    ST_AWAY,            /* 暂时离开 (压力消失但未超时) */
    ST_PAUSED,          /* 用户主动暂停 */
    ST_SUMMARY          /* 本次学习结束, 展示报告 */
} LearningState_t;

/* ======================== 事件类型 ======================== */

typedef enum {
    STATE_EVT_GUI_START = 0,        /* 用户在屏幕点击"开始学习" */
    STATE_EVT_GUI_PAUSE,            /* 用户点击"暂停" */
    STATE_EVT_GUI_RESUME,           /* 用户点击"继续" */
    STATE_EVT_GUI_END,              /* 用户点击"结束学习" */
    STATE_EVT_GUI_BACK,             /* SUMMARY 页面点击"返回主页" */
    STATE_EVT_GUI_CONFIRM_DURATION, /* PICKING 状态: 用户确认时长 (param = 分钟数) */
    STATE_EVT_PRESSURE_LOST,        /* 内部: 压力传感器超时 */
    STATE_EVT_PRESSURE_BACK,        /* 内部: 压力恢复 */
    STATE_EVT_AWAY_TIMEOUT,         /* 内部: AWAY 状态超时 (5min) */
    STATE_EVT_TIME_COMPLETE,        /* 内部: 倒计时到 0 (时长达成) */
} StateEventKind_t;

/* ======================== 事件结构 ======================== */

typedef struct {
    StateEventKind_t kind;
    float            pressure;   /* 当前压力值, 0 表示未知 */
    uint32_t         tick;       /* 事件产生时间 (FreeRTOS tick) */
    uint16_t         param;      /* 事件参数 (e.g., CONFIRM_DURATION 时 = 分钟数) */
} StateEvent_t;

/* ======================== SUMMARY 数据 ======================== */

typedef struct {
    uint32_t total_seconds;            /* 本次学习总时长 (秒) */
    uint16_t target_seconds;           /* 目标学习时长 (秒, 0 表示无目标) */
    uint8_t  avg_total_score;          /* 平均总分 (0..100) */
    uint8_t  focus_pct;                /* 专注占比 (0..100) */
    uint8_t  distract_pct;             /* 分心占比 (0..100) */
    uint8_t  fatigue_pct;              /* 疲劳占比 (0..100) */
    uint8_t  posture_abnormal_count;   /* 坐姿异常次数 */
    uint32_t away_count;               /* 离开次数 */
    uint32_t pause_count;              /* 暂停次数 */
    uint8_t  auto_ended;               /* 0 = 手动结束, 1 = 5min AWAY, 2 = 时长达成 */
} SessionSummary_t;

/* ======================== 阈值与周期 ======================== */

#define STATE_PRESSURE_THRESHOLD     5.0f       /* 视为"有人"的最小压力值 */
#define STATE_AWAY_DETECT_MS         (30 * 1000) /* 压力 = 0 持续 30s → AWAY */
#define STATE_AWAY_TIMEOUT_MS        (5 * 60 * 1000) /* AWAY 持续 5min → 自动结束 */
#define STATE_MONITOR_PERIOD_MS      200         /* 主循环轮询周期 */
#define STATE_POSTURE_BAD_SCORE      50          /* 坐姿评分低于此值视为异常 */
#define STATE_DEFAULT_DURATION_MIN   30          /* 默认目标时长 (分钟) */
#define STATE_VALID_DURATIONS        {5, 10, 15, 20, 25, 30, 45, 60, 90, 120}  /* 合法时长选项 */

/* ======================== 任务入口 ======================== */

/**
  * @brief  状态机任务入口
  * @note   栈空间建议 >= 512 字, 优先级 osPriorityNormal
  *         创建前必须先创建好 Slave_RxTask 与 Fusion_Task (因为会读它们的 API)
  */
void State_Task(void *pvParameters);

/* ======================== 公共 API ======================== */

/**
  * @brief  投递一个状态机事件 (线程安全, 任意上下文可调用)
  * @param  kind      事件类型
  * @param  pressure  当前压力值 (对 PRESSURE_LOST/BACK 有意义, 其余传 0)
  * @retval 0  成功入队
  * @retval -1 队列未就绪或已满
  */
int State_PostEvent(StateEventKind_t kind, float pressure);

/**
  * @brief  投递带 param 的事件 (扩展版, e.g. CONFIRM_DURATION 携带分钟数)
  * @param  kind  事件类型
  * @param  param 事件参数 (CONFIRM_DURATION 时 = 分钟数, 其余传 0)
  * @retval 同 State_PostEvent
  */
int State_PostEventParam(StateEventKind_t kind, uint16_t param);

/**
  * @brief  获取当前状态
  */
LearningState_t State_GetCurrent(void);

/**
  * @brief  状态名 (用于调试/UI 显示)
  */
const char* State_GetName(LearningState_t s);

/**
  * @brief  复制最近一次 SUMMARY 数据
  * @retval 0  成功
  * @retval -1 参数错误
  * @note   仅在 ST_SUMMARY 状态有有效数据; IDLE/LEARNING 等状态下返回的
  *         是上次会话的残留值, 调用方应结合 State_GetCurrent() 判断
  */
int State_GetSummary(SessionSummary_t *out);

/**
  * @brief  检查椅子上是否有人 (基于最近一次从机数据)
  * @retval 1 有人, 0 无人或无数据
  */
int State_IsPersonPresent(void);

/**
  * @brief  获取当前学习会话已用秒数 (仅 LEARNING 状态递增, PAUSED/AWAY 暂停)
  * @retval 秒数, 0 表示未在学习
  * @note   用于 UI 实时显示秒表, 数值会随时间增加
  */
uint32_t State_GetElapsedSeconds(void);

/**
  * @brief  设置一个屏幕提示消息 (例如 "请先坐下"), 3 秒后自动过期
  * @param  msg: 消息内容 (NULL 清空)
  */
void State_SetMessage(const char *msg);

/**
  * @brief  读取当前未过期的提示消息
  * @param  age_ms: 输出消息已显示时长 (ms), 可为 NULL
  * @retval 消息字符串, 过期或无消息返回 NULL
  */
const char *State_GetMessage(uint32_t *age_ms);

/**
  * @brief  获取"分心"状态已持续的时间 (仅在 ST_LEARNING 累积)
  * @retval 持续毫秒数, 0 = 当前未分心 或 不在学习状态
  * @note   由 Reminder 模块调用, 用于判断"是否该弹分心提醒"
  */
uint32_t State_GetSustainedDistract(void);

/**
  * @brief  获取"疲劳"状态已持续的时间 (仅在 ST_LEARNING 累积)
  * @retval 持续毫秒数, 0 = 当前未疲劳 或 不在学习状态
  */
uint32_t State_GetSustainedFatigue(void);

/**
  * @brief  获取"坐姿异常"已持续的时间 (仅在 ST_LEARNING 累积)
  * @retval 持续毫秒数, 0 = 当前坐姿正常 或 不在学习状态
  */
uint32_t State_GetSustainedPosture(void);

#ifdef __cplusplus
}
#endif

#endif /* __STATE_TASK_H */
