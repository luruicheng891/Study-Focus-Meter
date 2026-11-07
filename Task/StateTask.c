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
#include "SlaveRxTask.h"      /* Slave_GetLatest / Slave_SendCmd / SLAVE_CMD_* */

#include "FreeRTOS.h"
#include "queue.h"

#include <stdio.h>
#include <string.h>

/* ======================== 内部常量 ======================== */

#define STATE_EVT_QUEUE_LEN        16

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
static uint32_t           s_phone_prob_sum_x100  = 0;

/* 持续状态计时 (供 Reminder 模块查询: 持续走神/玩手机/坐姿异常多久了)
 * 0 = 当前未处于该状态, 非零 = 进入该状态的 FreeRTOS tick */
static TickType_t         s_distract_start_tick = 0;
static TickType_t         s_phone_start_tick  = 0;
static TickType_t         s_posture_bad_start_tick = 0;

/* UI 秒表: 仅在 LEARNING 状态递增, PAUSED/AWAY 冻结 */
static TickType_t         s_learning_resume_tick = 0;   /* 当前 LEARNING 段起点 (0=未在 LEARNING) */
static uint32_t           s_learned_seconds      = 0;   /* 累计已学习秒数 (不含 PAUSED/AWAY) */

/* 时长选择 (PICKING 状态) */
static uint8_t            s_target_minutes     = STATE_DEFAULT_DURATION_MIN;  /* 用户选择的目标时长 */
static uint8_t            s_picking            = 0;        /* 1=在选时长 (秒表不跑, 不统计) */

/* ======================== 连续不专注扣分 ======================== */
/* 仅在 ST_LEARNING 期间累计, 由 monitor_focus() 维护.
 * 退出 LEARNING (进入 IDLE/SUMMARY/PAUSED) 时由 reset_session_stats 清零.
 * 读端: UI 显示 (减去扣分) / SUMMARY 平均分 (减去扣分). */
static uint8_t            s_consecutive_not_focused = 0;  /* 当前连续不专注帧计数 */
static uint8_t            s_score_penalty           = 0;  /* 累计扣分 (0..N)         */

/* ======================== 内部函数声明 ======================== */

static void     reset_session_stats(void);
static void     state_enter_idle(uint8_t reset_target);
static void     state_enter_picking(uint16_t minutes);
static void     state_enter_learning(void);
static void     state_enter_away(void);
static void     state_enter_paused(void);
static void     state_enter_summary(uint8_t auto_ended);
static void     handle_event(const StateEvent_t *e);
static void     monitor_pressure(void);
static void     monitor_focus(void);
static void     check_away_timeout(void);
static void     check_time_complete(void);
static void     update_session_stats(void);
static void     slave_send_cmd(uint8_t cmd);

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
#define STATE_MSG_TIMEOUT_MS  3000   /* 默认消息自动过期时间 */
static char                s_state_msg[64]   = {0};
static TickType_t          s_state_msg_tick  = 0;
static uint32_t            s_state_msg_ttl   = STATE_MSG_TIMEOUT_MS; /* 当前消息的超时 (ms),
                                                                      0 = 持久 (不过期) */

void State_SetMessage(const char *msg)
{
    State_SetMessageEx(msg, STATE_MSG_TIMEOUT_MS);
}

void State_SetMessageEx(const char *msg, uint32_t timeout_ms)
{
    if (msg == NULL) {
        s_state_msg[0] = '\0';
        s_state_msg_ttl = STATE_MSG_TIMEOUT_MS;
        return;
    }
    strncpy(s_state_msg, msg, sizeof(s_state_msg) - 1);
    s_state_msg[sizeof(s_state_msg) - 1] = '\0';
    s_state_msg_tick = xTaskGetTickCount();
    s_state_msg_ttl  = timeout_ms;  /* 0 = 持久 (用于 AWAY 状态的"人已离开") */
}

const char *State_GetMessage(uint32_t *age_ms)
{
    if (s_state_msg[0] == '\0') return NULL;
    /* 持久消息 (ttl=0) 永不过期, 由调用方在状态切换时清空 */
    if (s_state_msg_ttl == 0) {
        if (age_ms) *age_ms = 0;
        return s_state_msg;
    }
    TickType_t now = xTaskGetTickCount();
    uint32_t age  = (uint32_t)((now - s_state_msg_tick) * 1000U
                                / configTICK_RATE_HZ);
    if (age >= s_state_msg_ttl) {
        s_state_msg[0] = '\0';
        s_state_msg_ttl = STATE_MSG_TIMEOUT_MS;
        return NULL;
    }
    if (age_ms) *age_ms = age;
    return s_state_msg;
}

uint8_t State_GetScorePenalty(void)
{
    return s_score_penalty;
}

uint8_t State_GetTargetMinutes(void)
{
    return s_target_minutes;
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

uint32_t State_GetTargetSeconds(void)
{
    /* 用户在 PICKING 选中并在 CONFIRM_DURATION 时应用的目标时长 (秒)。
     * 进入 LEARNING 时由 state_enter_learning 写入 s_summary.target_seconds,
     * 供学习界面倒计时/进度环使用。0 = 无目标。
     * PICKING 阶段返回 s_target_minutes (尚未写入 summary), 便于预览。 */
    if (s_cur_state == ST_PICKING) {
        return (uint32_t)s_target_minutes * 60U;
    }
    return (uint32_t)s_summary.target_seconds;
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

uint32_t State_GetSustainedPhone(void)
{
    return sustained_ms(s_phone_start_tick);
}

uint32_t State_GetSustainedPosture(void)
{
    return sustained_ms(s_posture_bad_start_tick);
}

/* ======================== 从机控制指令发送 ======================== */

/**
  * @brief  通过 USART2 向 ESP32 (从机) 发送单字符控制指令
  * @param  cmd  SLAVE_CMD_START('A') / SLAVE_CMD_STOP('Z') / SLAVE_CMD_SLEEP('S')
  * @note   实际发送在 SlaveRxTask 业务层封装 (Slave_SendCmd), 本任务只决定时机。
  *         主循环每 200ms 才进一次, 阻塞发送单字节不影响其他任务。
  */
static void slave_send_cmd(uint8_t cmd)
{
    Slave_SendCmd(cmd);
}

/* ======================== 会话统计 ======================== */

static void reset_session_stats(void)
{
    memset(&s_summary, 0, sizeof(s_summary));
    s_total_score_sum          = 0;
    s_fusion_sample_count      = 0;
    s_focus_prob_sum_x100      = 0;
    s_distract_prob_sum_x100   = 0;
    s_phone_prob_sum_x100      = 0;
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
    s_phone_start_tick         = 0;
    s_posture_bad_start_tick   = 0;
    s_consecutive_not_focused  = 0;
    s_score_penalty            = 0;
    /* 清空屏幕消息 (新会话无消息) */
    s_state_msg[0]   = '\0';
    s_state_msg_ttl  = STATE_MSG_TIMEOUT_MS;
}

/* ======================== 状态转移 ======================== */

/**
  * @brief  进入 IDLE 状态
  * @param  reset_target: 1=清零 s_target_minutes (从 SUMMARY 退回, 全新会话);
  *                       0=保留 s_target_minutes (PICKING→IDLE "无人在座" 重试)
  *                       让用户坐下后再次点 Start 时, Picker 直接显示上次的非默认选择。
  */
static void state_enter_idle(uint8_t reset_target)
{
    s_cur_state = ST_IDLE;
    reset_session_stats();
    if (reset_target)
    {
        s_target_minutes = STATE_DEFAULT_DURATION_MIN;
    }
    /* else: s_target_minutes 由 PICKING 阶段写入, 保留以便重试 */
    printf("[State] -> IDLE (reset_target=%u)\r\n", (unsigned)reset_target);
}

static void state_enter_picking(uint16_t minutes)
{
    /* 从 IDLE 进入: 全新会话, 清统计 */
    if (s_cur_state == ST_IDLE)
    {
        reset_session_stats();
        s_session_start_tick = xTaskGetTickCount();
    }
    /* 用户选择的目标时长 (0 = 沿用当前 s_target_minutes, 实现"重试不丢选择") */
    if (minutes == 0)
    {
        if (s_target_minutes == 0)
        {
            s_target_minutes = STATE_DEFAULT_DURATION_MIN;
        }
        /* else: 保留 s_target_minutes (重试流程) */
    }
    else
    {
        s_target_minutes = (minutes > 255) ? 255 : (uint8_t)minutes;
    }
    s_picking        = 1;
    s_cur_state      = ST_PICKING;

    /* 注意: 不在此处发送 'A'。按需求, 'A' (开始接收) 在用户确认时长
     * 进入 LEARNING 时才发送 (见 state_enter_learning)。 */
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

    /* 离开 AWAY/PAUSED 返回 LEARNING: 清除"AWAY 期间"显示的持久消息,
     * 避免回到正常界面后仍挂着 "Person left" 提示 */
    if (s_state_msg_ttl == 0) {
        State_SetMessage(NULL);
    }

    /* 通知子机开始接收/上报数据 (发送 'A')
     * 触发场景: 确认时长 (PICKING→LEARNING)、暂停后继续 (PAUSED→LEARNING)、
     *           离开后回座 (AWAY→LEARNING)。AWAY 期间未停流, 重发 'A' 也无害。 */
    slave_send_cmd(SLAVE_CMD_START);
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

    /* 在专注检测界面显示"人已离开"提示 (持续到返回 LEARNING)
     * 使用 ttl=0 (持久) 使消息在 AWAY 期间一直可见, 离开 AWAY 时由
     * state_enter_learning 显式清空, 避免 3 秒后被自动过期 */
    State_SetMessageEx("Person left - session paused", 0);
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
    /* 暂停 = 停止接收数据 (发送 'Z'); 继续时 state_enter_learning 会重发 'A'。
     * 如希望暂停时从机仍上报数据, 删除此行即可。 */
    slave_send_cmd(SLAVE_CMD_STOP);
    printf("[State] -> PAUSED (count=%lu)\r\n",
           (unsigned long)s_summary.pause_count);
}

static void state_enter_summary(uint8_t auto_ended)
{
    s_cur_state    = ST_SUMMARY;
    s_summary.auto_ended = auto_ended;

    /* 学习结束: 通知子机停止接收/上报数据 (发送 'Z') */
    slave_send_cmd(SLAVE_CMD_STOP);

    /* 计算总时长: 累加已学习秒数 + 当前 LEARNING 段未累加的时间
     * (PAUSED/AWAY 期间已由 state_enter_away/paused 累加到 s_learned_seconds,
     *  这里只需把"如果当前还在 LEARNING"的那段也补上) */
    TickType_t now = xTaskGetTickCount();
    uint32_t learned = s_learned_seconds;
    if (s_cur_state == ST_LEARNING && s_learning_resume_tick != 0)
    {
        learned += (uint32_t)((now - s_learning_resume_tick) / configTICK_RATE_HZ);
    }
    s_summary.total_seconds = learned;

    /* 兼容保留: 若 s_learned_seconds 因异常未累加(例如从未进入 LEARNING),
     * 退回到用会话起始 tick 计算, 避免上传 0。 */
    if (s_summary.total_seconds == 0 && s_session_start_tick != 0)
    {
        s_summary.total_seconds =
            (uint32_t)((now - s_session_start_tick) / configTICK_RATE_HZ);
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
        s_summary.phone_pct  =
            (uint8_t)((s_phone_prob_sum_x100  / s_fusion_sample_count) > 100 ?
                      100 : (s_phone_prob_sum_x100  / s_fusion_sample_count));
    }

    /* 应用"连续不专注"扣分到平均总分 (夹紧到 0).
     * 实时显示扣分由调用方 (UI) 在读取 FusionResult_t.total_score 时
     * 单独减一次 (State_GetScorePenalty), 这里仅作用于 SUMMARY 报告. */
    if (s_score_penalty > 0) {
        if (s_score_penalty < s_summary.avg_total_score) {
            s_summary.avg_total_score =
                (uint8_t)(s_summary.avg_total_score - s_score_penalty);
        } else {
            s_summary.avg_total_score = 0;
        }
        printf("[State] Apply score penalty: -%u (avg after=%u)\r\n",
               (unsigned)s_score_penalty, (unsigned)s_summary.avg_total_score);
    }

    printf("[State] -> SUMMARY (auto=%u, dur=%lus, avg=%u, focus=%u, "
           "distract=%u, phone=%u, bad_posture=%u, away=%lu, pause=%lu)\r\n",
           (unsigned)auto_ended,
           (unsigned long)s_summary.total_seconds,
           (unsigned)s_summary.avg_total_score,
           (unsigned)s_summary.focus_pct,
           (unsigned)s_summary.distract_pct,
           (unsigned)s_summary.phone_pct,
           (unsigned)s_summary.posture_abnormal_count,
           (unsigned long)s_summary.away_count,
           (unsigned long)s_summary.pause_count);
}

/* ======================== 事件分发 ======================== */

static void handle_event(const StateEvent_t *e)
{
    switch (s_cur_state)
    {
    /* ============== IDLE: 响应开始 → 检查是否有人 → PICKING ============== */
    case ST_IDLE:
        if (e->kind == STATE_EVT_GUI_START)
        {
            /* 点击 Start → 先检查椅子上是否有人, 没人就提示并留在主界面,
             * 避免用户走完 PICKING 流程才被退回。
             * 异常处理: 从机掉线 / 无数据视为"调试模式"放行 (与 PICKING→LEARNING
             * 检查保持一致), 仅在有数据且压力 <= 阈值时拦截。 */
            SlaveData_t sd;
            int   has_data = (Slave_GetLatest(&sd) == 0);
            float p        = has_data ? sd.pressure : 0.0f;
            if (has_data && p <= STATE_PRESSURE_THRESHOLD)
            {
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "Please sit on chair (p=%.1f)", (double)p);
                State_SetMessage(msg);
                printf("[State] GUI_START refused: no one on chair "
                       "(p=%.1f)\r\n", (double)p);
                /* 留在 IDLE (主界面), 用户坐下后再次点 Start */
                break;
            }

            /* 有人 (或 无从机数据调试模式) → 进入时长选择界面 (PICKING)。
             * 传入当前 s_target_minutes: 若用户因"无人在座"刚被退回 IDLE,
             * 该值仍是上次选的非默认时长, 此次进入 PICKING 时沿用;
             * 若是从 SUMMARY 返回后的全新会话, 该值已被 state_enter_idle(1) 重置为 30。 */
            printf("[State] GUI_START -> PICKING (carry target=%u min)\r\n",
                   (unsigned)s_target_minutes);
            state_enter_picking(s_target_minutes);
        }
        break;

    /* ============== PICKING: 选时长 → 确认时判断是否有人 ============== */
    case ST_PICKING:
        switch (e->kind)
        {
        case STATE_EVT_GUI_CONFIRM_DURATION:
        {
            /* 应用用户选择的时长 (param = 分钟数, 0 表示沿用默认) */
            if (e->param > 0)
            {
                s_target_minutes = (e->param > 255) ? 255 : (uint8_t)e->param;
            }

            /* 确认时长后, 再判断椅子上是否有人 */
            SlaveData_t sd;
            int   has_data = (Slave_GetLatest(&sd) == 0);
            float p        = has_data ? sd.pressure : 0.0f;

            if (!has_data || p > STATE_PRESSURE_THRESHOLD)
            {
                /* 有人, 或 无从机数据(调试模式) → 进入学习 */
                state_enter_learning();
            }
            else
            {
                /* 椅子上没人: 提示并退回主界面, 用户坐下后可重新点 Start。
                 * 保留 s_target_minutes (用户已选非默认时长, 重试时不丢选择) */
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "Please sit on chair (p=%.1f)", (double)p);
                State_SetMessage(msg);
                printf("[State] CONFIRM refused: %s\r\n", msg);
                state_enter_idle(0);
            }
            break;
        }
        case STATE_EVT_GUI_END:
            /* 取消选时长, 回到 IDLE (保留 s_target_minutes, 下次 Picker 仍显示上次的非默认选择) */
            state_enter_idle(0);
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
            /* SUMMARY 结束后回到 IDLE: 全新会话, 重置 s_target_minutes = 30 min */
            state_enter_idle(1);
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
  * @brief  判断从机 state 字符串是否代表"人在座位但不专注"
  *         (distracted / tired / phone 等).
  * @retval 1 = 在座但不专注, 0 = 在座专注 或 离开座位 (absent) 或 未知
  * @note   "离开座位" 由 30s 压力检测处理 (monitor_pressure), 此处不计入
  *         不专注, 避免 AWAY 期间每 3 秒重复扣分.
  */
static int is_not_focused_state(const char *state, float pressure)
{
    /* 压力过低视为离座 → 0 (由 30s 离开机制处理) */
    if (pressure <= STATE_PRESSURE_THRESHOLD) return 0;

    if (state == NULL || state[0] == '\0') {
        /* 字符串缺失: 保守视为"不专注", 触发扣分 */
        return 1;
    }
    /* explicit 离开类 (从机 state 直接报) — 视为离座, 不计入不专注 */
    if (strstr(state, "absent") != NULL) return 0;
    if (strstr(state, "away")   != NULL) return 0;
    if (strstr(state, "none")   != NULL) return 0;

    /* 显式专注类 */
    if (strstr(state, "focus")  != NULL) return 0;
    if (strstr(state, "normal") != NULL) return 0;
    if (strstr(state, "good")   != NULL) return 0;

    /* 其余 (distract / tired / phone / drowsy / fatigue 等) → 视为不专注 */
    return 1;
}

/**
  * @brief  跟踪连续不专注帧计数: 连续 STATE_NF_PENALTY_THRESHOLD 帧不专注
  *         → 累加 STATE_NF_PENALTY_AMOUNT 点扣分, 重置计数.
  * @note   仅在 ST_LEARNING 启用; PICKING/AWAY/PAUSED 不计.
  *         扣分通过 State_GetScorePenalty() 暴露给 UI 和 SUMMARY.
  *         无从机数据时清零计数, 避免连接抖动导致"卡在 2 次"状态.
  */
static void monitor_focus(void)
{
    if (s_cur_state != ST_LEARNING) return;
    if (s_picking) return;

    SlaveData_t sd;
    if (Slave_GetLatest(&sd) != 0) {
        /* 无从机数据: 重置计数, 避免漏判 */
        s_consecutive_not_focused = 0;
        return;
    }

    if (is_not_focused_state(sd.state, sd.pressure)) {
        if (s_consecutive_not_focused < 255) {
            s_consecutive_not_focused++;
        }
        if (s_consecutive_not_focused >= STATE_NF_PENALTY_THRESHOLD) {
            uint16_t new_pen = (uint16_t)s_score_penalty + STATE_NF_PENALTY_AMOUNT;
            if (new_pen > STATE_NF_PENALTY_MAX) new_pen = STATE_NF_PENALTY_MAX;
            s_score_penalty = (uint8_t)new_pen;
            s_consecutive_not_focused = 0;  /* 触发后重置, 等待下一轮 */
            printf("[State] Penalty: 3 consecutive not focused, "
                   "-%u (total -%u)\r\n",
                   (unsigned)STATE_NF_PENALTY_AMOUNT,
                   (unsigned)s_score_penalty);
        }
    } else {
        /* 专注或离座 → 重置计数 */
        s_consecutive_not_focused = 0;
    }
}

/**
  * @brief  检查 AWAY 状态是否超过 3 min, 是则触发自动结束
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
        s_phone_start_tick       = 0;
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
            s_phone_prob_sum_x100    += (uint32_t)(fr.vision_prob_phone    * 100.0f);
        }
        else
        {
            /* AI 暂无数据: 假设 "分心" (无法检测=最坏情况),
             * 避免 SUMMARY 显示三栏全 0% 误导用户 */
            s_focus_prob_sum_x100    += 0;
            s_distract_prob_sum_x100 += 100;
            s_phone_prob_sum_x100    += 0;
        }
        s_fusion_sample_count++;

        /* 2. 持续走神 / 玩手机 计时 (>0.5 即视为激活该状态) */
        if (fr.vision_prob_distract > 0.5f) {
            if (s_distract_start_tick == 0) s_distract_start_tick = now;
        } else {
            s_distract_start_tick = 0;
        }
        if (fr.vision_prob_phone > 0.5f) {
            if (s_phone_start_tick == 0) s_phone_start_tick = now;
        } else {
            s_phone_start_tick = 0;
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

        /* ----- 2.5 连续不专注扣分 (专注检测界面需求) ----- */
        monitor_focus();

        /* ----- 3. AWAY 超时检查 ----- */
        check_away_timeout();

        /* ----- 4. 倒计时检查 (到 0 触发 TIME_COMPLETE) ----- */
        check_time_complete();

        /* ----- 5. 累加会话统计 ----- */
        update_session_stats();
    }
}
