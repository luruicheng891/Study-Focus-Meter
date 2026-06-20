/**
  ******************************************************************************
  * @file    FusionTask.c
  * @brief   多模态数据融合引擎
  *
  *          周期: 1 秒。从以下模块拉取数据:
  *            - AITask        : 专注/分心/疲劳 概率
  *            - SlaveRxTask   : 姿态状态 + 评分 + 压力值
  *            - SensorTask    : 温度 / 湿度 / 光照 / 音量百分比
  *
  *          生成 FusionResult_t 并通过长度为 1 的队列发布
  *
  *          权重 (总和 = 100):
  *            视觉       40 %
  *            姿态       30 %
  *            环境       20 %  (温度 + 湿度 + 光照 + 声音, 内部各占 25 %)
  *            时长       10 %
  ******************************************************************************
  */

#include "FusionTask.h"

#include "AITask.h"
#include "SlaveRxTask.h"
#include "SensorTask.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* =========================== 内部状态 =========================== */

#define FUSION_PERIOD_MS        1000u

/* 长度为 1 的队列: 生产者使用 xQueueOverwrite，消费者使用 peek/recv */
static QueueHandle_t s_queue = NULL;

/* 首个有效样本的时刻 (FreeRTOS 时钟)，用于计算学习分钟数 */
static uint32_t s_session_start_tick = 0;
static uint8_t  s_session_started    = 0;

/* =========================== 数学辅助函数 ============================== */

static inline uint8_t u8_clamp(int v)
{
    if (v < 0)   return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

/* 线性 "梯形" 评分函数:
 *   v <= lo_bad             -> 0
 *   v in [lo_bad, lo_ok]    -> 从 0 线性上升到 100
 *   v in [lo_ok, hi_ok]     -> 100 (舒适区间)
 *   v in [hi_ok, hi_bad]    -> 从 100 线性下降到 0
 *   v >= hi_bad             -> 0
 *
 * 适用于温度 / 湿度 / 光照等有 "舒适窗口" 的指标
 */
static uint8_t score_trapezoid(float v,
                               float lo_bad, float lo_ok,
                               float hi_ok,  float hi_bad)
{
    if (v <= lo_bad || v >= hi_bad) return 0;
    if (v >= lo_ok && v <= hi_ok)   return 100;

    float ratio;
    if (v < lo_ok)
    {
        /* 上升斜率 */
        if (lo_ok <= lo_bad) return 100; /* 防御性处理 */
        ratio = (v - lo_bad) / (lo_ok - lo_bad);
    }
    else
    {
        /* 下降斜率 */
        if (hi_bad <= hi_ok) return 100; /* 防御性处理 */
        ratio = (hi_bad - v) / (hi_bad - hi_ok);
    }
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return (uint8_t)(ratio * 100.0f + 0.5f);
}

/* "越低越好" 线性评分 (用于噪音百分比)
 *   v <= ok_max  -> 100
 *   v >= bad_min -> 0
 *   中间值线性插值
 */
static uint8_t score_lower_better(float v, float ok_max, float bad_min)
{
    if (v <= ok_max)  return 100;
    if (v >= bad_min) return 0;
    float ratio = (bad_min - v) / (bad_min - ok_max);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return (uint8_t)(ratio * 100.0f + 0.5f);
}

/* =========================== 各模块评分 ======================== */

/**
  * @brief  从 AI 概率计算视觉子评分
  *
  *         映射 (每个概率在 [0,1] 范围内):
  *             vision = focus * 100 + distract * 30 + fatigue * 0
  *
  *         "focused" 是理想状态，"distracted" 仍给予部分分数
  *         (短暂走神)，"fatigued" 不得分
  */
static uint8_t score_vision(const float probs[3])
{
    float s = probs[0] * 100.0f + probs[1] * 30.0f + probs[2] * 0.0f;
    if (s < 0.0f)   s = 0.0f;
    if (s > 100.0f) s = 100.0f;
    return (uint8_t)(s + 0.5f);
}

/**
  * @brief  直接使用从机的评分字段作为姿态子评分
  */
static uint8_t score_posture(const SlaveData_t *d)
{
    return u8_clamp((int)d->score);
}

/**
  * @brief  计算环境各子评分及综合环境评分
  *
  *         针对室内学习优化的舒适窗口:
  *           温度      : 舒适 22..26 C，警告 18..30，超出 12..36 为失败
  *           湿度      : 舒适 40..60 %，警告 30..70，超出 20..85 为失败
  *           光照      : 舒适 300..1000，警告 150..1500，<50 或 >2500 为失败
  *           声音      : <=30 % 为完美，>=80 % 得零分
  */
static void score_environment(const SensorData_t *sd,
                              uint8_t *temp_s,
                              uint8_t *humi_s,
                              uint8_t *lux_s,
                              uint8_t *noise_s,
                              uint8_t *combined)
{
    float temp_c   = (float)sd->temperature / 100.0f;
    float humi_pct = (float)sd->humidity    / 100.0f;
    float lux      = (float)sd->lux_x100    / 100.0f;
    float sound    = (float)sd->soundIntensity;

    *temp_s  = score_trapezoid(temp_c,   12.0f, 22.0f, 26.0f, 36.0f);
    *humi_s  = score_trapezoid(humi_pct, 20.0f, 40.0f, 60.0f, 85.0f);
    *lux_s   = score_trapezoid(lux,      50.0f, 300.0f, 1000.0f, 2500.0f);
    *noise_s = score_lower_better(sound, 30.0f, 80.0f);

    /* 四项各占 25% 权重 */
    int sum = (int)(*temp_s) + (int)(*humi_s) + (int)(*lux_s) + (int)(*noise_s);
    *combined = (uint8_t)((sum + 2) / 4);   /* 四舍五入 */
}

/**
  * @brief  基于连续学习时间 (分钟) 计算时长子评分
  *
  *         番茄工作法启发式曲线: 前 25 分钟最佳，之后逐渐下降，
  *         连续学习超过约 3 小时则得分很低
  *
  *           [0, 25)    -> 100
  *           [25, 50)   ->  90
  *           [50, 90)   ->  75
  *           [90, 120)  ->  60
  *           [120, 180) ->  40
  *           [180, +)   ->  20
  */
static uint8_t score_duration(uint32_t minutes)
{
    if (minutes < 25)   return 100;
    if (minutes < 50)   return 90;
    if (minutes < 90)   return 75;
    if (minutes < 120)  return 60;
    if (minutes < 180)  return 40;
    return 20;
}

/* =========================== 建议生成器 ========================== */

static void build_advice(FusionResult_t *r)
{
    /* 在四个顶层模块中找出最低的子评分，生成一条建议 */
    /* 并列时优先顺序: 视觉 > 姿态 > 环境 > 时长 */
    uint8_t v = r->vision_score;
    uint8_t p = r->posture_score;
    uint8_t e = r->env_score;
    uint8_t d = r->duration_score;

    /* 如果所有指标都很好，给出积极鼓励 */
    if (v >= 80 && p >= 80 && e >= 80 && d >= 80)
    {
        snprintf(r->advice, sizeof(r->advice), "Great state, keep it up!");
        return;
    }

    uint8_t  lo = v; const char *who = "vision";
    if (p < lo) { lo = p; who = "posture"; }
    if (e < lo) { lo = e; who = "env"; }
    if (d < lo) { lo = d; who = "duration"; }

    if (who[0] == 'v')
    {
        if (r->vision_prob_fatigue >= 0.5f)
            snprintf(r->advice, sizeof(r->advice),
                     "Looking tired - take a short break.");
        else if (r->vision_prob_distract >= 0.5f)
            snprintf(r->advice, sizeof(r->advice),
                     "Distraction detected - refocus on the task.");
        else
            snprintf(r->advice, sizeof(r->advice),
                     "Try to keep your eyes on the screen.");
    }
    else if (who[0] == 'p')
    {
        snprintf(r->advice, sizeof(r->advice),
                 "Sit up straight, posture score is low.");
    }
    else if (who[0] == 'e')
    {
        /* 找出环境子项中最差的一项 */
        uint8_t te = r->env_temp_score;
        uint8_t hu = r->env_humi_score;
        uint8_t lx = r->env_lux_score;
        uint8_t no = r->env_noise_score;
        uint8_t mn = te; const char *what = "temperature";
        if (hu < mn) { mn = hu; what = "humidity"; }
        if (lx < mn) { mn = lx; what = "light";    }
        if (no < mn) { mn = no; what = "noise";    }
        snprintf(r->advice, sizeof(r->advice),
                 "Adjust %s for a better environment.", what);
    }
    else
    {
        snprintf(r->advice, sizeof(r->advice),
                 "You have studied a while - take a break.");
    }
}

/* =========================== 调试打印 ============================= */

static void debug_print(const FusionResult_t *r)
{
    printf("\r\n========== Fusion #%lu  total=%u ==========\r\n",
           (unsigned long)r->seq, (unsigned)r->total_score);
    printf("  vision  : %3u  (F=%.2f D=%.2f T=%.2f)  [w%d%%]\r\n",
           (unsigned)r->vision_score,
           (double)r->vision_prob_focus,
           (double)r->vision_prob_distract,
           (double)r->vision_prob_fatigue,
           FUSION_W_VISION);
    printf("  posture : %3u  state=\"%s\" press=%.2f       [w%d%%]\r\n",
           (unsigned)r->posture_score,
           r->posture_state[0] ? r->posture_state : "-",
           (double)r->pressure,
           FUSION_W_POSTURE);
    printf("  env     : %3u  T=%ld.%02ld C  H=%lu.%02lu %%  L=%lu.%02lu lx  N=%u%%   [w%d%%]\r\n",
           (unsigned)r->env_score,
           (long)(r->temperature_c100 / 100), (long)(r->temperature_c100 % 100),
           (unsigned long)(r->humidity_x100 / 100),
           (unsigned long)(r->humidity_x100 % 100),
           (unsigned long)(r->lux_x100 / 100),
           (unsigned long)(r->lux_x100 % 100),
           (unsigned)r->sound_pct,
           FUSION_W_ENV);
    printf("           sub-scores: T=%u H=%u L=%u N=%u\r\n",
           (unsigned)r->env_temp_score,
           (unsigned)r->env_humi_score,
           (unsigned)r->env_lux_score,
           (unsigned)r->env_noise_score);
    printf("  duration: %3u  minutes=%lu                       [w%d%%]\r\n",
           (unsigned)r->duration_score,
           (unsigned long)r->study_minutes,
           FUSION_W_DURATION);
    printf("  flags   : 0x%02lX  (V=%c P=%c S=%c)\r\n",
           (unsigned long)r->flags,
           (r->flags & FUSION_FLAG_VISION_OK)  ? 'Y' : 'N',
           (r->flags & FUSION_FLAG_POSTURE_OK) ? 'Y' : 'N',
           (r->flags & FUSION_FLAG_SENSOR_OK)  ? 'Y' : 'N');
    printf("  advice  : %s\r\n", r->advice);
    printf("==============================================\r\n");
}

/* =========================== 任务主体 ================================= */

void Fusion_Task(void *pvParameters)
{
    FusionResult_t r;
    TickType_t     last_wake;
    uint32_t       seq = 0;

    (void)pvParameters;

    s_queue = xQueueCreate(1, sizeof(FusionResult_t));
    if (s_queue == NULL)
    {
        printf("[Fusion] Queue create FAILED\r\n");
        vTaskDelete(NULL);
        return;
    }

    printf("[Fusion] Task started (period=%u ms)\r\n",
           (unsigned)FUSION_PERIOD_MS);

    last_wake = xTaskGetTickCount();

    for (;;)
    {
        memset(&r, 0, sizeof(r));

        /* ----- 1. Vision (AITask) ------------------------------------ */
        float probs[3] = {0.0f, 0.0f, 0.0f};
        if (AI_GetLatestProbs(probs, NULL) == 0)
        {
            r.flags |= FUSION_FLAG_VISION_OK;
            r.vision_score        = score_vision(probs);
            r.vision_prob_focus   = probs[0];
            r.vision_prob_distract= probs[1];
            r.vision_prob_fatigue = probs[2];
        }
        else
        {
            /* 暂无 AI 数据: 假设中性状态 (评分 50) */
            r.vision_score = 50;
        }

        /* ----- 2. Posture (SlaveRxTask) ------------------------------ */
        SlaveData_t sd_slave;
        if (Slave_GetLatest(&sd_slave) == 0)
        {
            r.flags |= FUSION_FLAG_POSTURE_OK;
            r.posture_score = score_posture(&sd_slave);
            r.pressure      = sd_slave.pressure;
            strncpy(r.posture_state, sd_slave.state,
                    sizeof(r.posture_state) - 1);
        }
        else
        {
            r.posture_score = 50;
            strncpy(r.posture_state, "n/a", sizeof(r.posture_state) - 1);
        }

        /* ----- 3. Environment (SensorTask) --------------------------- */
        SensorData_t sd_env;
        if (xSensorDataQueue != NULL &&
            xQueuePeek(xSensorDataQueue, &sd_env, 0) == pdTRUE)
        {
            r.flags |= FUSION_FLAG_SENSOR_OK;
            uint8_t te, hu, lx, no, ev;
            score_environment(&sd_env, &te, &hu, &lx, &no, &ev);
            r.env_temp_score   = te;
            r.env_humi_score   = hu;
            r.env_lux_score    = lx;
            r.env_noise_score  = no;
            r.env_score        = ev;
            r.temperature_c100 = sd_env.temperature;
            r.humidity_x100    = sd_env.humidity;
            r.lux_x100         = sd_env.lux_x100;
            r.sound_pct        = (uint16_t)sd_env.soundIntensity;
        }
        else
        {
            r.env_score        = 50;
            r.env_temp_score   = 50;
            r.env_humi_score   = 50;
            r.env_lux_score    = 50;
            r.env_noise_score  = 50;
        }

        /* ----- 4. Study duration ------------------------------------- */
        TickType_t now_tick = xTaskGetTickCount();
        if (!s_session_started &&
            (r.flags & (FUSION_FLAG_VISION_OK | FUSION_FLAG_SENSOR_OK)))
        {
            s_session_start_tick = now_tick;
            s_session_started    = 1;
        }
        if (s_session_started)
        {
            uint32_t elapsed_ticks = (uint32_t)(now_tick - s_session_start_tick);
            r.study_minutes = elapsed_ticks /
                              (configTICK_RATE_HZ * 60u);
        }
        r.duration_score = score_duration(r.study_minutes);

        /* ----- 5. Weighted total ------------------------------------- */
        uint32_t weighted =
              (uint32_t)r.vision_score   * FUSION_W_VISION
            + (uint32_t)r.posture_score  * FUSION_W_POSTURE
            + (uint32_t)r.env_score      * FUSION_W_ENV
            + (uint32_t)r.duration_score * FUSION_W_DURATION;

        /* 权重总和 == 100，因此 weighted 范围在 0..10000 之间 */
        r.total_score = (uint8_t)((weighted + 50) / 100);

        /* ----- 6. Advice + bookkeeping ------------------------------- */
        build_advice(&r);
        r.timestamp = now_tick;
        r.seq       = ++seq;

        xQueueOverwrite(s_queue, &r);
        debug_print(&r);

        /* ----- 7. Sleep ---------------------------------------------- */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FUSION_PERIOD_MS));
    }
}

/* =========================== 消费者 API ============================== */

int Fusion_GetLatest(FusionResult_t *out)
{
    if (out == NULL || s_queue == NULL) return -1;
    return (xQueuePeek(s_queue, out, 0) == pdPASS) ? 0 : -1;
}

int Fusion_WaitNew(FusionResult_t *out, TickType_t timeout)
{
    if (out == NULL || s_queue == NULL) return -1;
    return (xQueueReceive(s_queue, out, timeout) == pdPASS) ? 0 : -1;
}