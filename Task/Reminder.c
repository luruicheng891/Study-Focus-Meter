/**
  ******************************************************************************
  * @file    Reminder.c
  * @brief   温和提醒模块实现 (详见 Reminder.h)
  ******************************************************************************
  */

#include "Reminder.h"
#include "StateTask.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* ======================== 触发阈值 ======================== */
#define REM_DISTRACT_THRESHOLD_MS  (2 * 60 * 1000)   /* 持续分心 2 min */
#define REM_FATIGUE_THRESHOLD_MS   (5 * 60 * 1000)   /* 持续疲劳 5 min */
#define REM_POSTURE_THRESHOLD_MS   (5 * 60 * 1000)   /* 持续坐姿异常 5 min */
#define REM_45MIN_MARK             (45 * 60)         /* 学满 45min, 一次 */

/* ======================== 动画时长 (温和节奏) ======================== */
#define REM_FADE_IN_MS     800    /* 淡入: 0.8s */
#define REM_HOLD_MS        4000   /* 停留: 4.0s */
#define REM_FADE_OUT_MS    1200   /* 淡出: 1.2s */

/* ======================== 冷却期 (避免重复触发) ======================== */
#define REM_COOLDOWN_MS    (3 * 60 * 1000)  /* 同类提醒 3 分钟内不重发 */

/* ======================== 配色 (全部降饱和, 温柔风) ======================== */
#define REM_BG_SOFT_YELLOW  lv_color_hex(0xFEF3C7)  /* 淡黄 — 分心 */
#define REM_BG_SOFT_ORANGE  lv_color_hex(0xFFEDD5)  /* 暖橙 — 疲劳 */
#define REM_BG_SOFT_PINK    lv_color_hex(0xFCE7F3)  /* 粉橘 — 坐姿 */
#define REM_BG_SOFT_GREEN   lv_color_hex(0xD1FAE5)  /* 柔绿 — 完成 */
#define REM_FG_DARK_TEXT    lv_color_hex(0x1F2937)  /* 主文字色: 暗灰 */

/* ======================== 提醒状态机 ======================== */
typedef enum {
    REM_ST_IDLE = 0,         /* 无提醒 */
    REM_ST_FADE_IN,          /* 淡入中 */
    REM_ST_HOLD,             /* 停留中 */
    REM_ST_FADE_OUT          /* 淡出中 */
} RemState_t;

typedef struct {
    uint8_t         in_use;          /* 1 = 当前显示中 */
    RemState_t      st;              /* 状态机阶段 */
    TickType_t      phase_tick;      /* 当前阶段起点 */
    lv_obj_t       *panel;           /* 浮层面板 */
    lv_obj_t       *label;           /* 文字 label */
} ReminderSlot_t;

static ReminderSlot_t s_rem = {0};

/* 各类型上次触发的时刻 (用于冷却期) */
static TickType_t s_last_distract = 0;
static TickType_t s_last_fatigue  = 0;
static TickType_t s_last_posture  = 0;
static TickType_t s_last_complete = 0;

/* 完成提醒一次性: 0=未触发, 1=已触发 */
static uint8_t s_complete_45min_fired = 0;

/* ======================== 内部工具 ======================== */

/* 在 45min 提醒上, 需要重置触发器, 之后下一次会话重新检测 */
static void reset_completion_fired(void)
{
    s_complete_45min_fired = 0;
}

static lv_obj_t *create_panel(lv_color_t bg)
{
    lv_obj_t *p = lv_obj_create(lv_layer_top());
    /* 居中, y=20 (顶部留状态栏位置) */
    lv_obj_set_size(p, 280, 36);
    lv_obj_set_pos(p, 20, 20);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 18, 0);              /* 圆角胶囊 */
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_shadow_width(p, 8, 0);
    lv_obj_set_style_shadow_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(p, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(p, 2, 0);
    lv_obj_set_style_pad_all(p, 6, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(p, LV_OPA_TRANSP, 0);       /* 默认透明, 淡入 */
    return p;
}

static void start_reminder(const char *text, lv_color_t bg)
{
    /* 销毁旧的 (如果上一次没淡完) */
    if (s_rem.panel) {
        lv_obj_del(s_rem.panel);
        s_rem.panel = NULL;
        s_rem.label = NULL;
    }

    s_rem.panel = create_panel(bg);
    s_rem.label = lv_label_create(s_rem.panel);
    lv_label_set_text(s_rem.label, text);
    lv_obj_set_style_text_color(s_rem.label, REM_FG_DARK_TEXT, 0);
    lv_obj_set_style_text_font(s_rem.label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_rem.label);

    s_rem.in_use    = 1;
    s_rem.st        = REM_ST_FADE_IN;
    s_rem.phase_tick = xTaskGetTickCount();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_rem.panel);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, REM_FADE_IN_MS);
    lv_anim_start(&a);
}

static void end_reminder(void)
{
    if (!s_rem.panel) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_rem.panel);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, REM_FADE_OUT_MS);
    lv_anim_set_ready_cb(&a, NULL);
    lv_anim_start(&a);
    /* 标记进入淡出 */
    s_rem.st = REM_ST_FADE_OUT;
    s_rem.phase_tick = xTaskGetTickCount();
}

static void destroy_reminder(void)
{
    if (s_rem.panel) {
        lv_obj_del(s_rem.panel);
        s_rem.panel = NULL;
    }
    s_rem.label   = NULL;
    s_rem.in_use  = 0;
    s_rem.st      = REM_ST_IDLE;
}

/* 冷却期检查: 同类提醒在冷却期内不重发 */
static int cooldown_elapsed(TickType_t *last_tick)
{
    if (*last_tick == 0) return 1;  /* 从未触发过, 可以发 */
    TickType_t now = xTaskGetTickCount();
    if ((now - *last_tick) > pdMS_TO_TICKS(REM_COOLDOWN_MS)) return 1;
    return 0;
}

/* ======================== 公开 API ======================== */

void Reminder_Init(void)
{
    /* 懒创建: 第一次触发时才建浮层, 减少常驻内存 */
    s_rem.panel = NULL;
    s_rem.label = NULL;
    s_rem.in_use = 0;
    s_rem.st     = REM_ST_IDLE;
}

void Reminder_Poll(void)
{
    /* 只在 LEARNING 状态检查提醒 (其他状态不打扰) */
    LearningState_t s = State_GetCurrent();
    if (s != ST_LEARNING) {
        /* 不在学习: 立即清空当前提醒 */
        if (s_rem.in_use) destroy_reminder();
        /* 离开 LEARNING 时重置 45min 触发器 */
        if (s == ST_IDLE || s == ST_SUMMARY) reset_completion_fired();
        return;
    }

    /* 状态机推进 */
    if (s_rem.in_use) {
        TickType_t now = xTaskGetTickCount();
        uint32_t phase_ms = (uint32_t)((now - s_rem.phase_tick) * 1000U
                                        / configTICK_RATE_HZ);
        switch (s_rem.st) {
        case REM_ST_FADE_IN:
            if (phase_ms >= REM_FADE_IN_MS) {
                s_rem.st = REM_ST_HOLD;
                s_rem.phase_tick = now;
            }
            break;
        case REM_ST_HOLD:
            if (phase_ms >= REM_HOLD_MS) {
                end_reminder();
            }
            break;
        case REM_ST_FADE_OUT:
            if (phase_ms >= REM_FADE_OUT_MS) {
                destroy_reminder();
            }
            break;
        default: break;
        }
    }

    /* 已经显示中, 不检查触发 */
    if (s_rem.in_use) return;

    /* 触发检查 — 按优先级 (完成 > 坐姿 > 疲劳 > 分心) */
    uint32_t elapsed_sec = State_GetElapsedSeconds();
    uint32_t sd_ms  = State_GetSustainedDistract();
    uint32_t sf_ms  = State_GetSustainedFatigue();
    uint32_t sp_ms  = State_GetSustainedPosture();

    /* 1. 完成 45min 庆祝 (一次性, 整轮只一次) */
    if (!s_complete_45min_fired
        && REM_45MIN_MARK > 0
        && elapsed_sec >= REM_45MIN_MARK
        && cooldown_elapsed(&s_last_complete)) {
        s_complete_45min_fired = 1;
        s_last_complete = xTaskGetTickCount();
        start_reminder("Done a 45-min study round.", REM_BG_SOFT_GREEN);
        return;
    }

    /* 2. 持续坐姿异常 5min */
    if (sp_ms >= REM_POSTURE_THRESHOLD_MS
        && cooldown_elapsed(&s_last_posture)) {
        s_last_posture = xTaskGetTickCount();
        start_reminder("Let's adjust your posture.", REM_BG_SOFT_PINK);
        return;
    }

    /* 3. 持续疲劳 5min */
    if (sf_ms >= REM_FATIGUE_THRESHOLD_MS
        && cooldown_elapsed(&s_last_fatigue)) {
        s_last_fatigue = xTaskGetTickCount();
        start_reminder("How about a short break?", REM_BG_SOFT_ORANGE);
        return;
    }

    /* 4. 持续分心 2min (优先级最低, 上面三个先于它) */
    if (sd_ms >= REM_DISTRACT_THRESHOLD_MS
        && cooldown_elapsed(&s_last_distract)) {
        s_last_distract = xTaskGetTickCount();
        start_reminder("Attention seems scattered.", REM_BG_SOFT_YELLOW);
        return;
    }
}
