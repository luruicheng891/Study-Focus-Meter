/**
  ******************************************************************************
  * @file    StateTask.c
  * @brief   学习会话状态机实现
  *
  *  详见 StateTask.h 中的状态转移图
  *
  *  主循环每 STATE_MONITOR_PERIOD_MS 唤醒一次, 完成:
  *    1) 处理事件队列 (GUI 操作 / 内部超时)
  *    2) 轮询从机压力, 决定是否触发 AWAY / 恢复
  *    3) 轮询融合结果, 累加 SUMMARY 统计
  ******************************************************************************
  */

#include "StateTask.h"

#include "FusionTask.h"
#include "SlaveRxTask.h"
#include "UART_ESP32.h"        /* huart2 */
#include "stm32h7xx_hal.h"

#include "FreeRTOS.h"
#include "queue.h"

#include <stdio.h>
#include <string.h>

/* ======================== 内部常量 ======================== */

#define STATE_EVT_QUEUE_LEN        16
#define BLE_CMD_BUF_LEN            64

/* ======================== 静态变量 ======================== */

static const char *s_state_names[] = {
    "IDLE", "PICKING", "LEARNING", "AWAY", "PAUSED", "SUMMARY"
};

static QueueHandle_t      s_evt_queue        = NULL;
static volatile LearningState_t s_cur_state   = ST_IDLE;

/* 会话统计 (在 reset_session_stats 中清零) */
static SessionSummary_t   s_summary;
static TickType_t         s_session_start_tick = 0;
static TickType_t         s_away_start_tick    = 0;
static TickType_t         s_last_pressure_tick = 0;
static uint8_t            s_has_pressure       = 0;
static uint8_t            s_posture_was_bad    = 0;   /* 边沿检测 */

/* 融合结果累加器 (用于 SUMMARY 计算平均值) */
static uint64_t           s_total_score_sum     = 0;
static uint32_t           s_fusion_sample_count = 0;
static uint32_t           s_focus_prob_sum_x100 = 0;
static uint32_t           s_distract_prob_sum_x100 = 0;
static uint32_t           s_fatigue_prob_sum_x100  = 0;

/* 持续状态计时 (供 Reminder 模块查询: 持续分心/疲劳/坐姿异常多久了)
 * 0 = 当前未处于该状态, 非零 = 进入该状态的 FreeRTOS tick */
static TickType_t         s_distract_start_tick = 0;
static TickType_t         s_fatigue_start_tick  = 0;
static TickType_t         s_posture_bad_start_tick = 0;

/* UI 秒表: 仅在 LEARNING 状态递增, PAUSED/AWAY 冻结 */
static TickType_t         s_learning_resume_tick = 0;   /* 当前 LEARNING 段起点 (0=未在 LEARNING) */
static uint32_t           s_learned_seconds      = 0;   /* 累计已学习秒数 (不含 PAUSED/AWAY) */

/* 时长选择 (PICKING 状态) */
static uint8_t            s_target_minutes     = STATE_DEFAULT_DURATION_MIN;  /* 用户选择的目标时长 */
static uint8_t            s_picking            = 0;        /* 1=在选时长 (秒表不跑, 不统计) */

/* ======================== 内部函数声明 ======================== */

static void     reset_session_stats(void);
static void     state_enter_idle(void);
static void     state_enter_picking(uint16_t minutes);
static void     state_enter_learning(void);
static void     state_enter_away(void);
static void     state_enter_paused(void);
static void     state_enter_summary(uint8_t auto_ended);
static void     handle_event(const StateEvent_t *e);
static void     monitor_pressure(void);
static void     check_away_timeout(void);
static void     check_time_complete(void);
static void     update_session_stats(void);
static void     ble_send_cmd(const char *cmd);

/* ======================== 工具函数 ======================== */

const char* State_GetName(LearningState_t s)
{
    if ((unsigned)s >= sizeof(s_state_names) / sizeof(s_state_names[0]))
    {
        return "?";
    }
    return s_state_names[s];
}

LearningState_t State_GetCurrent(void)
{
    return s_cur_state;
}

int State_PostEvent(StateEventKind_t kind, float pressure)
{
    if (s_evt_queue == NULL) return -1;

    StateEvent_t evt;
    evt.kind     = kind;
    evt.pressure = pressure;
    evt.tick     = (uint32_t)xTaskGetTickCount();
    evt.param    = 0;
    return (xQueueSend(s_evt_queue, &evt, 0) == pdPASS) ? 0 : -1;
}

int State_PostEventParam(StateEventKind_t kind, uint16_t param)
{
    if (s_evt_queue == NULL) return -1;

    StateEvent_t evt;
    evt.kind     = kind;
    evt.pressure = 0;
    evt.tick     = (uint32_t)xTaskGetTickCount();
    evt.param    = param;
    return (xQueueSend(s_evt_queue, &evt, 0) == pdPASS) ? 0 : -1;
}

int State_GetSummary(SessionSummary_t *out)
{
    if (out == NULL) return -1;
    *out = s_summary;
    return 0;
}

int State_IsPersonPresent(void)
{
    SlaveData_t sd;
    if (Slave_GetLatest(&sd) != 0) return 0;
    return (sd.pressure > STATE_PRESSURE_THRESHOLD) ? 1 : 0;
}

/* UI 屏幕提示消息 (Toast) */
#define STATE_MSG_TIMEOUT_MS  3000   /* 消息自动过期时间 */
static char                s_state_msg[64]   = {0};
static TickType_t          s_state_msg_tick  = 0;

void State_SetMessage(const char *msg)
{
    if (msg == NULL) {
        s_state_msg[0] = '\0';
        return;
    }
    strncpy(s_state_msg, msg, sizeof(s_state_msg) - 1);
    s_state_msg[sizeof(s_state_msg) - 1] = '\0';
    s_state_msg_tick = xTaskGetTickCount();
}

const char *State_GetMessage(uint32_t *age_ms)
{
    if (s_state_msg[0] == '\0') return NULL;
    TickType_t now = xTaskGetTickCount();
    uint32_t age  = (uint32_t)((now - s_state_msg_tick) * 1000U
                                / configTICK_RATE_HZ);
    if (age >= STATE_MSG_TIMEOUT_MS) {
        s_state_msg[0] = '\0';
        return NULL;
    }
    if (age_ms) *age_ms = age;
    return s_state_msg;
}

uint32_t State_GetElapsedSeconds(void)
{
    uint32_t total = s_learned_seconds;
    /* 若当前正在 LEARNING 段, 加上当前段已用时间 */
    if (s_cur_state == ST_LEARNING && s_learning_resume_tick != 0)
    {
        TickType_t now = xTaskGetTickCount();
        total += (uint32_t)((now - s_learning_resume_tick) / configTICK_RATE_HZ);
    }
    return total;
}

/* ======================== 持续状态查询 (供 Reminder 用) ======================== */

/**
  * @brief  计算"已持续 X 状态"的毫秒数, 状态未激活时返回 0
  * @note   读 s_xxx_start_tick 由 update_session_stats 写入, 仅本任务更新;
  *         Reminder 任务在另一上下文读取, FreeRTOS tick 为 32-bit 单调变量,
  *         对齐保证由变量长度 (4 字节) 提供, 单字读在 ARM Cortex-M 上天然原子
  */
static uint32_t sustained_ms(TickType_t start_tick)
{
    if (start_tick == 0) return 0;
    TickType_t now = xTaskGetTickCount();
    return (uint32_t)((now - start_tick) * 1000U / configTICK_RATE_HZ);
}

uint32_t State_GetSustainedDistract(void)
{
    return sustained_ms(s_distract_start_tick);
}

uint32_t State_GetSustainedFatigue(void)
{
    return sustained_ms(s_fatigue_start_tick);
}

uint32_t State_GetSustainedPosture(void)
{
    return sustained_ms(s_posture_bad_start_tick);
}

/* ======================== BLE 命令发送 ======================== */

/**
  * @brief  通过 USART2 向 ESP32 发送 JSON 指令
  * @note   简单阻塞发送, 指令很短 (< 64 字节), 100ms 超时足够
  *         主循环每 200ms 才进一次, 不会影响其他任务
  */
static void ble_send_cmd(const char *cmd)
{
    if (cmd == NULL) return;

    char buf[BLE_CMD_BUF_LEN];
    int n = snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}\n", cmd);
    if (n <= 0 || n >= (int)sizeof(buf)) return;

    HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)n, 100);
    printf("[State] BLE -> %s", buf);
}

/* ======================== 会话统计 ======================== */

static void reset_session_stats(void)
{
    memset(&s_summary, 0, sizeof(s_summary));
    s_total_score_sum          = 0;
    s_fusion_sample_count      = 0;
    s_focus_prob_sum_x100      = 0;
    s_distract_prob_sum_x100   = 0;
    s_fatigue_prob_sum_x100    = 0;
    s_session_start_tick       = 0;
    s_away_start_tick          = 0;
    s_last_pressure_tick       = 0;
    s_has_pressure             = 0;
    s_posture_was_bad          = 0;
    s_learning_resume_tick     = 0;
    s_learned_seconds          = 0;
    s_picking                  = 0;
    s_target_minutes           = STATE_DEFAULT_DURATION_MIN;
    s_distract_start_tick      = 0;
    s_fatigue_start_tick       = 0;
    s_posture_bad_start_tick   = 0;
}

/* ======================== 状态进入函数 ======================== */

static void state_enter_idle(void)
{
    s_cur_state = ST_IDLE;
    reset_session_stats();
    printf("[State] -> IDLE\r\n");
}

static void state_enter_picking(uint16_t minutes)
{
    /* 从 IDLE 进入: 全新会话, 清统计 */
    if (s_cur_state == ST_IDLE)
    {
        reset_session_stats();
        s_session_start_tick = xTaskGetTickCount();
    }
    /* 用户选择的目标时长 (0 = 用默认值) */
    if (minutes == 0) minutes = STATE_DEFAULT_DURATION_MIN;
    s_target_minutes = (minutes > 255) ? 255 : (uint8_t)minutes;
    s_picking        = 1;
    s_cur_state      = ST_PICKING;

    /* 通知子机开始采集 (跟 LEARNING 一样, 因为椅子可能很快有人) */
    ble_send_cmd("start");
    printf("[State] -> PICKING (target=%u min)\r\n", s_target_minutes);
}

static void state_enter_learning(void)
{
    if (s_cur_state == ST_IDLE || s_cur_state == ST_SUMMARY)
    {
        /* 新会话开始: 清统计并记录起始 tick */
        reset_session_stats();
        s_session_start_tick = xTaskGetTickCount();
    }
    /* 从 PICKING 进入: 保留 target_minutes, 启动秒表 */
    s_cur_state              = ST_LEARNING;
    s_away_start_tick        = 0;
    s_learning_resume_tick   = xTaskGetTickCount();   /* 启动/恢复秒表 */
    s_picking                = 0;
    s_summary.target_seconds = (uint16_t)(s_target_minutes * 60);

    /* 进入学习: 通知子机开始采集 (PICKING 已发, 这里再发一次也无害) */
    if (s_session_start_tick != 0)
    {
        /* 首次进入 LEARNING, 不重复发 BLE start (PICKING 已发) */
    }
    else
    {
        ble_send_cmd("start");
    }
    printf("[State] -> LEARNING (target=%u min)\r\n", s_target_minutes);
}

static void state_enter_away(void)
{
    /* 冻结秒表: 累加当前 LEARNING 段时间到 s_learned_seconds */
    if (s_cur_state == ST_LEARNING && s_learning_resume_tick != 0)
    {
        TickType_t now = xTaskGetTickCount();
        s_learned_seconds += (uint32_t)((now - s_learning_resume_tick) / configTICK_RATE_HZ);
        s_learning_resume_tick = 0;
    }

    s_cur_state        = ST_AWAY;
    s_away_start_tick  = xTaskGetTickCount();
    s_summary.away_count++;
    /* 不发 STOP, 保持子节点活跃以检测压力恢复 */
    printf("[State] -> AWAY (count=%lu)\r\n",
           (unsigned long)s_summary.away_count);
}

static void state_enter_paused(void)
{
    /* 冻结秒表: 累加当前 LEARNING 段时间到 s_learned_seconds */
    if (s_cur_state == ST_LEARNING && s_learning_resume_tick != 0)
    {
        TickType_t now = xTaskGetTickCount();
        s_learned_seconds += (uint32_t)((now - s_learning_resume_tick) / configTICK_RATE_HZ);
        s_learning_resume_tick = 0;
    }

    s_cur_state = ST_PAUSED;
    s_summary.pause_count++;
    ble_send_cmd("pause");
    printf("[State] -> PAUSED (count=%lu)\r\n",
           (unsigned long)s_summary.pause_count);
}

static void state_enter_summary(uint8_t auto_ended)
{
    s_cur_state    = ST_SUMMARY;
    s_summary.auto_ended = auto_ended;

    /* 通知子机停止采集 */
    ble_send_cmd("stop");

    /* 计算总时长 */
    TickType_t now = xTaskGetTickCount();
    if (s_session_start_tick != 0)
    {
        s_summary.total_seconds =
            (uint32_t)((now - s_session_start_tick) / configTICK_RATE_HZ);
    }
    else
    {
        s_summary.total_seconds = 0;
    }

    /* 计算平均分与状态占比 */
    if (s_fusion_sample_count > 0)
    {
        s_summary.avg_total_score =
            (uint8_t)((s_total_score_sum + s_fusion_sample_count / 2) /
                       s_fusion_sample_count);
        s_summary.focus_pct    =
            (uint8_t)((s_focus_prob_sum_x100    / s_fusion_sample_count) > 100 ?
                      100 : (s_focus_prob_sum_x100    / s_fusion_sample_count));
        s_summary.distract_pct =
            (uint8_t)((s_distract_prob_sum_x100 / s_fusion_sample_count) > 100 ?
                      100 : (s_distract_prob_sum_x100 / s_fusion_sample_count));
        s_summary.fatigue_pct  =
            (uint8_t)((s_fatigue_prob_sum_x100  / s_fusion_sample_count) > 100 ?
                      100 : (s_fatigue_prob_sum_x100  / s_fusion_sample_count));
    }

    printf("[State] -> SUMMARY (auto=%u, dur=%lus, avg=%u, focus=%u, "
           "distract=%u, fatigue=%u, bad_posture=%u, away=%lu, pause=%lu)\r\n",
           (unsigned)auto_ended,
           (unsigned long)s_summary.total_seconds,
           (unsigned)s_summary.avg_total_score,
           (unsigned)s_summary.focus_pct,
           (unsigned)s_summary.distract_pct,
           (unsigned)s_summary.fatigue_pct,
           (unsigned)s_summary.posture_abnormal_count,
           (unsigned long)s_summary.away_count,
           (unsigned long)s_summary.pause_count);
}

/* ======================== 事件分发 ======================== */

static void handle_event(const StateEvent_t *e)
{
    switch (s_cur_state)
    {
    /* ============== IDLE: 响应开始 → PICKING ============== */
    case ST_IDLE:
        if (e->kind == STATE_EVT_GUI_START)
        {
            /* 二次校验: 椅子上有人 (有 BLE 数据时) */
            SlaveData_t sd;
            int has_data = (Slave_GetLatest(&sd) == 0);
            float p = has_data ? sd.pressure : 0.0f;

            if (!has_data)
            {
                /* 调试模式: 无 BLE 数据, 直接放行 */
                printf("[State] GUI_START (no BLE data, dev mode)\r\n");
                state_enter_picking(STATE_DEFAULT_DURATION_MIN);
            }
            else if (p > STATE_PRESSURE_THRESHOLD)
            {
                state_enter_picking(STATE_DEFAULT_DURATION_MIN);
            }
            else
            {
                /* 椅子上没人: 拒绝 + 屏幕提示 */
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "Please sit on chair (p=%.1f)",
                         (double)p);
                State_SetMessage(msg);
                printf("[State] GUI_START refused: %s\r\n", msg);
            }
        }
        break;

    /* ============== PICKING: 等用户选时长 / 自动超时 ============== */
    case ST_PICKING:
        switch (e->kind)
        {
        case STATE_EVT_GUI_CONFIRM_DURATION:
            /* 用户 (或 5s 超时) 确认了时长, 进入 LEARNING */
            state_enter_learning();
            break;
        case STATE_EVT_GUI_END:
            /* 取消选时长, 回到 IDLE */
            state_enter_idle();
            break;
        default:
            break;
        }
        break;

    /* ============== LEARNING: 暂停 / 结束 / AWAY / 时长达成 ============== */
    case ST_LEARNING:
        switch (e->kind)
        {
        case STATE_EVT_GUI_PAUSE:
            state_enter_paused();
            break;
        case STATE_EVT_GUI_END:
            state_enter_summary(0);
            break;
        case STATE_EVT_PRESSURE_LOST:
            state_enter_away();
            break;
        case STATE_EVT_TIME_COMPLETE:
            /* 倒计时到 0, 自动进入 SUMMARY (auto_ended=2 时长达成) */
            state_enter_summary(2);
            break;
        default:
            break;
        }
        break;

    /* ============== AWAY: 恢复 / 结束 / 超时 ============== */
    case ST_AWAY:
        switch (e->kind)
        {
        case STATE_EVT_PRESSURE_BACK:
            state_enter_learning();
            break;
        case STATE_EVT_GUI_END:
            state_enter_summary(0);
            break;
        case STATE_EVT_AWAY_TIMEOUT:
            state_enter_summary(1);  /* auto_ended = 1 */
            break;
        default:
            break;
        }
        break;

    /* ============== PAUSED: 继续 / 结束 ============== */
    case ST_PAUSED:
        switch (e->kind)
        {
        case STATE_EVT_GUI_RESUME:
            state_enter_learning();
            break;
        case STATE_EVT_GUI_END:
            state_enter_summary(0);
            break;
        default:
            break;
        }
        break;

    /* ============== SUMMARY: 返回主页 ============== */
    case ST_SUMMARY:
        if (e->kind == STATE_EVT_GUI_BACK)
        {
            state_enter_idle();
        }
        break;

    default:
        break;
    }
}

/* ======================== 压力监测 ======================== */

/**
  * @brief  轮询从机压力, 在 LEARNING 状态判断是否触发 AWAY
  *         在 AWAY 状态判断是否恢复
  * @note   无数据时不触发 AWAY (避免子机掉线时误判)
  */
static void monitor_pressure(void)
{
    if (s_cur_state != ST_LEARNING && s_cur_state != ST_AWAY) return;
    if (s_picking) return;  /* PICKING 状态不监测 */

    SlaveData_t sd;
    if (Slave_GetLatest(&sd) != 0)
    {
        /* 没有从机数据, 不触发任何状态变化 */
        return;
    }

    TickType_t now = xTaskGetTickCount();

    if (sd.pressure > STATE_PRESSURE_THRESHOLD)
    {
        /* 有人 */
        s_last_pressure_tick = now;
        s_has_pressure       = 1;

        if (s_cur_state == ST_AWAY)
        {
            State_PostEvent(STATE_EVT_PRESSURE_BACK, sd.pressure);
        }
    }
    else
    {
        /* 没人 */
        if (s_cur_state == ST_LEARNING && s_has_pressure)
        {
            uint32_t elapsed_ms =
                (uint32_t)((now - s_last_pressure_tick) * 1000U /
                            configTICK_RATE_HZ);
            if (elapsed_ms >= STATE_AWAY_DETECT_MS)
            {
                State_PostEvent(STATE_EVT_PRESSURE_LOST, sd.pressure);
            }
        }
        /* AWAY 状态保持不变, 由 check_away_timeout 处理超时 */
    }
}

/**
  * @brief  检查 AWAY 状态是否超过 5min, 是则触发自动结束
  */
static void check_away_timeout(void)
{
    if (s_cur_state != ST_AWAY) return;
    if (s_away_start_tick == 0) return;

    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_ms =
        (uint32_t)((now - s_away_start_tick) * 1000U / configTICK_RATE_HZ);

    if (elapsed_ms >= STATE_AWAY_TIMEOUT_MS)
    {
        State_PostEvent(STATE_EVT_AWAY_TIMEOUT, 0.0f);
    }
}

/**
  * @brief  检查学习时长是否已达成 (LEARNING 状态时)
  *         当 elapsed >= target 时, 触发 STATE_EVT_TIME_COMPLETE
  *         AWAY/PAUSED 时已冻结秒表, 故不触发
  */
static void check_time_complete(void)
{
    if (s_cur_state != ST_LEARNING) return;
    if (s_summary.target_seconds == 0) return;  /* 无目标, 不自动结束 */
    if (s_learning_resume_tick == 0) return;

    /* 计算当前会话总秒数 (历史累计 + 当前段) */
    TickType_t now = xTaskGetTickCount();
    uint32_t current_seg = (uint32_t)((now - s_learning_resume_tick) / configTICK_RATE_HZ);
    uint32_t total_sec   = s_learned_seconds + current_seg;

    if (total_sec >= s_summary.target_seconds)
    {
        State_PostEvent(STATE_EVT_TIME_COMPLETE, 0.0f);
    }
}

/* ======================== 会话数据累加 ======================== */

/**
  * @brief  累加融合结果 + 坐姿异常计数 (仅在 LEARNING 状态)
  * @note   坐姿异常使用边沿检测: 持续异常期间只计 1 次
  */
static void update_session_stats(void)
{
    if (s_cur_state != ST_LEARNING) {
        /* 不在学习: 清空所有持续状态计时 */
        s_distract_start_tick    = 0;
        s_fatigue_start_tick     = 0;
        s_posture_bad_start_tick = 0;
        return;
    }
    if (s_picking) return;  /* PICKING 状态不累加 */

    TickType_t now = xTaskGetTickCount();

    /* 1. 融合结果 */
    FusionResult_t fr;
    if (Fusion_GetLatest(&fr) == 0)
    {
        s_total_score_sum += fr.total_score;

        /* 检查 AI 视觉数据是否有效 */
        if (fr.flags & FUSION_FLAG_VISION_OK)
        {
            /* AI 实际输出: 直接累加 */
            s_focus_prob_sum_x100    += (uint32_t)(fr.vision_prob_focus    * 100.0f);
            s_distract_prob_sum_x100 += (uint32_t)(fr.vision_prob_distract * 100.0f);
            s_fatigue_prob_sum_x100  += (uint32_t)(fr.vision_prob_fatigue  * 100.0f);
        }
        else
        {
            /* AI 暂无数据: 假设 "分心" (无法检测=最坏情况),
             * 避免 SUMMARY 显示三栏全 0% 误导用户 */
            s_focus_prob_sum_x100    += 0;
            s_distract_prob_sum_x100 += 100;
            s_fatigue_prob_sum_x100  += 0;
        }
        s_fusion_sample_count++;

        /* 2. 持续分心 / 疲劳 计时 (>0.5 即视为激活该状态) */
        if (fr.vision_prob_distract > 0.5f) {
            if (s_distract_start_tick == 0) s_distract_start_tick = now;
        } else {
            s_distract_start_tick = 0;
        }
        if (fr.vision_prob_fatigue > 0.5f) {
            if (s_fatigue_start_tick == 0) s_fatigue_start_tick = now;
        } else {
            s_fatigue_start_tick = 0;
        }
    }

    /* 3. 坐姿异常 (边沿检测 + 持续计时) */
    SlaveData_t sd;
    if (Slave_GetLatest(&sd) == 0)
    {
        uint8_t is_bad = (sd.score < STATE_POSTURE_BAD_SCORE) ? 1 : 0;
        if (is_bad && !s_posture_was_bad)
        {
            s_summary.posture_abnormal_count++;
        }
        s_posture_was_bad = is_bad;

        if (is_bad) {
            if (s_posture_bad_start_tick == 0) s_posture_bad_start_tick = now;
        } else {
            s_posture_bad_start_tick = 0;
        }
    }
}

/* ======================== 任务主体 ======================== */

void State_Task(void *pvParameters)
{
    (void)pvParameters;

    s_evt_queue = xQueueCreate(STATE_EVT_QUEUE_LEN, sizeof(StateEvent_t));
    if (s_evt_queue == NULL)
    {
        printf("[State] Event queue create FAILED\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* 等待从机 / 融合任务就绪 (最多等 3 秒) */
    for (int i = 0; i < 30; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("[State] Task started (period=%d ms, away_detect=%d s, "
           "away_timeout=%d s)\r\n",
           STATE_MONITOR_PERIOD_MS,
           STATE_AWAY_DETECT_MS / 1000,
           STATE_AWAY_TIMEOUT_MS / 1000);

    /* 初始状态 */
    s_cur_state = ST_IDLE;

    for (;;)
    {
        /* ----- 1. 事件处理 (最多等一个周期) ----- */
        StateEvent_t evt;
        if (xQueueReceive(s_evt_queue, &evt,
                          pdMS_TO_TICKS(STATE_MONITOR_PERIOD_MS)) == pdPASS)
        {
            handle_event(&evt);
        }

        /* ----- 2. 压力监测 ----- */
        monitor_pressure();

        /* ----- 3. AWAY 超时检查 ----- */
        check_away_timeout();

        /* ----- 4. 倒计时检查 (到 0 触发 TIME_COMPLETE) ----- */
        check_time_complete();

        /* ----- 5. 累加会话统计 ----- */
        update_session_stats();
    }
}
