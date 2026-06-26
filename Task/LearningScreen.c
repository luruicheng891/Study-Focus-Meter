/**
  ******************************************************************************
  * @file    LearningScreen.c
  * @brief   学习专注界面 — 现代极简版 (智能台灯式陪伴)
  *
  *          设计原则:
  *            - 数据后台持续跑, 界面只呈现温柔陪伴, 不做实时审判
  *            - 产品气质: 书桌上的智能台灯, 不是监工
  *            - 任何异常 2~5 分钟才浮现提示, 数值变化 2~3 秒平滑过渡
  *            - 配色全部降饱和, 不用刺眼红/绿, 不强制打断
  *            - 现代极简排版, 大量留白, 大字时间, 弱化控件
  *
  *          布局 (320x240):
  *            y=0..14   顶部极简状态条 (状态/时间/摄像头缩略图)
  *            y=24..176 中央: 双环 (外=进度 / 内=专注) + 大字时间 + 目标
  *            y=180..208 环境信息卡 (温度/湿度/光照) — 极淡
  *            y=210..222 噪音波形 — 安静时近静态
  *            y=224..240 极小控制按钮 (Pause/End) — 暗灰不抢眼
  *
  *          颜色: 专注环默认柔绿, 仅在持续异常 5min+ 才缓慢变色
  *                (不被模型瞬时 Distract 结果左右, 体现"陪伴")
  ******************************************************************************
  */

#include "LearningScreen.h"
#include "StateTask.h"        /* LearningState_t, State_GetCurrent, State_PostEvent,
                                 State_GetSustainedDistract/Fatigue/Posture */
#include "WeatherTask.h"      /* WeatherInfo_t, Weather_GetLatest */
#include "FusionTask.h"       /* FusionResult_t, 调试面板用 (posture/score) */
#include "SlaveRxTask.h"      /* SlaveData_t, Slave_GetLatest — 调试面板原始分数/压力 */
#include "AITask.h"           /* AI_GetLatestProbs — 调试面板必须用实时, 不走 Fusion */
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>           /* abs */
#include <math.h>

/* ======================== 字体定义 ========================
 * lv_conf.h 启用了 montserrat_12/14/16/18/20/28/48
 *   时间大字 28pt — 醒目但不刺眼
 *   14pt  — 调试面板分类结果
 *   小字  12pt  — 顶部/环境/按钮/调试细节 */
#define FONT_TIME       &lv_font_montserrat_28
#define FONT_MID        &lv_font_montserrat_14
#define FONT_SMALL      &lv_font_montserrat_12

/* ======================== 配色 (现代深色风, 全部降饱和) ======================== */
#define LR_BG_DEEP      lv_color_hex(0x0E1320)   /* 深蓝黑底 */
#define LR_BG_PANEL     lv_color_hex(0x161C2C)   /* 卡片底 (稍亮) */
#define LR_BG_GLOW      lv_color_hex(0x1F2738)   /* 控件底 */
#define LR_FG_SOFT      lv_color_hex(0xE8ECF1)   /* 柔白主文字 */
#define LR_FG_MID       lv_color_hex(0xA0AABE)   /* 中灰 */
#define LR_FG_DIM       lv_color_hex(0x6B7488)   /* 暗灰 */

/* 主色调: 柔绿 (陪伴) */
#define LR_GREEN_SOFT   lv_color_hex(0x86EFAC)   /* 柔绿 300 */
#define LR_GREEN_DEEP   lv_color_hex(0x4ADE80)   /* 柔绿 400 (深色用) */

/* 状态色 (全部用 100/200/300 浅色) */
#define LR_YELLOW_SOFT  lv_color_hex(0xFDE68A)   /* 柔黄 200 — 分心 */
#define LR_ORANGE_SOFT  lv_color_hex(0xFED7AA)   /* 柔橙 200 — 疲劳 */
#define LR_PINK_SOFT    lv_color_hex(0xFBCFE8)   /* 柔粉 200 — 坐姿异常 */

/* 进度色: 柔青 */
#define LR_PROG_CYAN    lv_color_hex(0x67E8F9)   /* 柔青 300 */
#define LR_PROG_TEAL    lv_color_hex(0x5EEAD4)   /* 柔青 200 */

/* 轨道色 */
#define LR_TRACK        lv_color_hex(0x222A3A)   /* 圆环轨道 (暗) */
#define LR_TRACK_SOFT   lv_color_hex(0x2A3346)   /* 圆环轨道 (稍亮) */

/* ======================== 动画时长 ======================== */
#define LR_BREATH_PERIOD_MS    2000   /* 呼吸: 2s 一周期 (1s 正 + 1s 反) */
#define LR_COLOR_FADE_MS       2500   /* 状态色平滑过渡: 2.5s */
#define LR_PROGRESS_FILL_MS    3000   /* 进度环填充: 3s */

/* ======================== 几何参数 ======================== */
#define LR_FOCUS_RADIUS        58     /* 专注环半径 */
#define LR_PROGRESS_RADIUS     74     /* 进度环半径 (外) */
#define LR_RING_WIDTH          3      /* 圆环线宽 (细, 优雅) */
#define LR_RING_WIDTH_OUT      2      /* 外环线宽 (更细) */

/* 圆环中心 */
#define LR_CENTER_X            160
#define LR_CENTER_Y            105

/* ======================== 控件指针 ======================== */
static lv_obj_t *lr_screen         = NULL;  /* 根对象 */
static lv_obj_t *lr_label_time     = NULL;  /* 中央大字时间 */
static lv_obj_t *lr_label_target   = NULL;  /* 目标提示 */

/* 顶部状态 */
static lv_obj_t *lr_label_state    = NULL;  /* 状态文字 */
static lv_obj_t *lr_label_clock    = NULL;  /* 时钟 */
static lv_obj_t *lr_label_wifi     = NULL;  /* WiFi 状态 */
static lv_obj_t *lr_cam_thumb      = NULL;  /* 摄像头缩略图 */

/* 双环 */
static lv_obj_t *lr_arc_focus      = NULL;  /* 内: 专注度 (呼吸) */
static lv_obj_t *lr_arc_progress   = NULL;  /* 外: 学习进度 (慢填充) */

/* 噪音波形 */
#define LR_WAVE_POINTS  48
static lv_obj_t   *lr_wave_line  = NULL;
static lv_point_t  lr_wave_pts[LR_WAVE_POINTS];
static uint8_t     lr_wave_buf[LR_WAVE_POINTS];
static uint8_t     lr_wave_idx   = 0;
static uint32_t    lr_wave_last_tick = 0;
#define LR_WAVE_SAMPLE_MS  250   /* 250ms 采一次 (波形更"温柔") */

/* 环境信息卡 */
static lv_obj_t *lr_env_panel     = NULL;
static lv_obj_t *lr_env_temp_lbl  = NULL;
static lv_obj_t *lr_env_humi_lbl  = NULL;
static lv_obj_t *lr_env_lux_lbl   = NULL;

/* 控制按钮 — 极小, 暗灰 */
static lv_obj_t *lr_btn_pause     = NULL;
static lv_obj_t *lr_btn_end       = NULL;
static lv_obj_t *lr_pause_lbl     = NULL;

/* 调试面板 (可切换) — 左侧滑出, 显示 AI 模型原始结果 */
static lv_obj_t *lr_btn_dbg       = NULL;   /* 顶部 DBG 按钮 */
static lv_obj_t *lr_dbg_panel     = NULL;   /* 调试面板容器 */
static uint8_t   lr_dbg_visible   = 0;      /* 0=隐藏, 1=显示 */
static lv_obj_t *lr_dbg_lbl_ai    = NULL;   /* "AI" 标题 */
static lv_obj_t *lr_dbg_lbl_class = NULL;   /* ">> Focus" 主分类结果 */
static lv_obj_t *lr_dbg_lbl_f     = NULL;   /* F: 85% */
static lv_obj_t *lr_dbg_lbl_d     = NULL;   /* D: 10% */
static lv_obj_t *lr_dbg_lbl_t     = NULL;   /* T:  5% */
static lv_obj_t *lr_dbg_lbl_s     = NULL;   /* S: 85 */
static lv_obj_t *lr_dbg_lbl_v     = NULL;   /* V: 80 */
static lv_obj_t *lr_dbg_lbl_p     = NULL;   /* P: normal */
static lv_obj_t *lr_dbg_lbl_po    = NULL;   /* Po: 77  (从机原始姿态分数) */
static lv_obj_t *lr_dbg_lbl_pr    = NULL;   /* Pr: 0.0 (从机原始压力值) */
static lv_obj_t *lr_dbg_lbl_seq   = NULL;   /* #123 */

/* 内部状态 */
static uint32_t lr_last_elapsed_sec = 0;

/* ======================== 内部小工具 ======================== */
static lv_obj_t *lr_make_label(lv_obj_t *parent, const char *txt,
                                const lv_font_t *font, lv_color_t color,
                                int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* ======================== 专注环呼吸动画 (2s 周期) ======================== */
static void lr_breath_anim_cb(void *obj, int32_t v)
{
    /* v: 0..100 → 不透明度 60~100, 线宽 2~4 (轻柔) */
    lv_opa_t opa = (lv_opa_t)(LV_OPA_60 + (v * (LV_OPA_100 - LV_OPA_60) / 100));
    lv_obj_set_style_arc_opa((lv_obj_t *)obj, opa, LV_PART_INDICATOR);
    int32_t w = 2 + (v * 2 / 100);
    lv_obj_set_style_arc_width((lv_obj_t *)obj, (uint16_t)w, LV_PART_INDICATOR);
}

static void lr_start_breath_anim(lv_obj_t *arc)
{
    /* play 1s + playback 1s = 2s 一周期 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, lr_breath_anim_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, LR_BREATH_PERIOD_MS / 2);
    lv_anim_set_playback_time(&a, LR_BREATH_PERIOD_MS / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* ======================== 颜色平滑过渡 (2.5s) ======================== */
static lv_color_t lr_focus_cur_color = {0};
static lv_obj_t  *lr_focus_anim_obj  = NULL;
static lv_color_t s_anim_start;
static lv_color_t s_anim_target;

static void lr_arc_color_anim_cb(void *obj, int32_t v)
{
    lv_color_t mix = lv_color_mix(s_anim_start, s_anim_target, (uint16_t)v);
    lv_obj_set_style_arc_color((lv_obj_t *)obj, mix, LV_PART_INDICATOR);
}

static void lr_focus_fade_to(lv_color_t target)
{
    if (lr_focus_anim_obj == NULL) return;
    if (lv_color_to32(lr_focus_cur_color) == lv_color_to32(target)) return;

    s_anim_start  = lr_focus_cur_color;
    s_anim_target = target;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lr_focus_anim_obj);
    lv_anim_set_exec_cb(&a, lr_arc_color_anim_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, LR_COLOR_FADE_MS);
    lv_anim_start(&a);

    lr_focus_cur_color = target;
}

/* ======================== 进度环填充 (3s 慢) ======================== */
static void lr_progress_set_anim_cb(void *obj, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)obj, (int16_t)v);
}

static void lr_progress_anim_to(int32_t new_value)
{
    if (lr_arc_progress == NULL) return;
    int16_t cur = lv_arc_get_value(lr_arc_progress);
    if (cur == (int16_t)new_value) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lr_arc_progress);
    lv_anim_set_exec_cb(&a, lr_progress_set_anim_cb);
    lv_anim_set_values(&a, cur, new_value);
    lv_anim_set_time(&a, LR_PROGRESS_FILL_MS);
    lv_anim_start(&a);
}

/* ======================== 噪音波形 ======================== */
static void lr_wave_init(void)
{
    lr_wave_line = lv_line_create(lr_screen);
    int x0 = 20, x1 = 300, y0 = 213;
    int span = x1 - x0;
    for (int i = 0; i < LR_WAVE_POINTS; i++) {
        lr_wave_pts[i].x = (lv_coord_t)(x0 + (span * i) / (LR_WAVE_POINTS - 1));
        lr_wave_pts[i].y = (lv_coord_t)(y0 + 4);  /* 中心线 */
        lr_wave_buf[i]   = 0;
    }
    lv_line_set_points(lr_wave_line, lr_wave_pts, LR_WAVE_POINTS);
    lv_obj_set_style_line_color(lr_wave_line, LR_FG_DIM, 0);
    lv_obj_set_style_line_width(lr_wave_line, 1, 0);
    lv_obj_set_style_line_opa(lr_wave_line, LV_OPA_50, 0);
    lv_obj_set_style_line_rounded(lr_wave_line, true, 0);
}

/* 安静时近静态, 突响时短暂跳动 */
static void lr_wave_push(uint8_t sound_pct)
{
    if (lr_wave_line == NULL) return;

    lr_wave_buf[lr_wave_idx] = sound_pct;
    lr_wave_idx = (lr_wave_idx + 1) % LR_WAVE_POINTS;

    int yc = 217;
    int amp_max = 6;

    for (int i = 0; i < LR_WAVE_POINTS; i++) {
        uint8_t v = lr_wave_buf[i];
        int amp = (v * amp_max) / 100;
        if (amp < 1) amp = 1;
        int16_t offset = (int16_t)(amp *
            (int16_t)lv_trigo_sin(360 * i / LR_WAVE_POINTS) / 32767);
        lr_wave_pts[i].y = (lv_coord_t)(yc - offset);
    }
    lv_line_set_points(lr_wave_line, lr_wave_pts, LR_WAVE_POINTS);
}

/* ======================== 环境信息卡 ========================
 * 320x240 底部居中, 极淡卡片
 *   温度 °C | 湿度 % | 光照 lx
 *   样式: 圆角小卡片, 暗底, 灰文字
 */
static void lr_env_build(void)
{
    /* 卡片底 (圆角, 微微发亮) */
    lr_env_panel = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_env_panel, 280, 22);
    lv_obj_set_pos(lr_env_panel, 20, 184);
    lv_obj_set_style_bg_color(lr_env_panel, LR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(lr_env_panel, LV_OPA_60, 0);
    lv_obj_set_style_radius(lr_env_panel, 11, 0);
    lv_obj_set_style_border_width(lr_env_panel, 0, 0);
    lv_obj_set_style_pad_all(lr_env_panel, 0, 0);
    lv_obj_clear_flag(lr_env_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 温度 (左) */
    lr_env_temp_lbl = lv_label_create(lr_env_panel);
    lv_label_set_text(lr_env_temp_lbl, "--.- C");
    lv_obj_set_style_text_font(lr_env_temp_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_env_temp_lbl, LR_FG_MID, 0);
    lv_obj_set_pos(lr_env_temp_lbl, 16, 5);

    /* 分隔点 */
    lv_obj_t *dot1 = lv_label_create(lr_env_panel);
    lv_label_set_text(dot1, "/");
    lv_obj_set_style_text_font(dot1, FONT_SMALL, 0);
    lv_obj_set_style_text_color(dot1, LR_FG_DIM, 0);
    lv_obj_set_pos(dot1, 100, 5);

    /* 湿度 (中) */
    lr_env_humi_lbl = lv_label_create(lr_env_panel);
    lv_label_set_text(lr_env_humi_lbl, "-- %");
    lv_obj_set_style_text_font(lr_env_humi_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_env_humi_lbl, LR_FG_MID, 0);
    lv_obj_set_pos(lr_env_humi_lbl, 116, 5);

    /* 分隔点 */
    lv_obj_t *dot2 = lv_label_create(lr_env_panel);
    lv_label_set_text(dot2, "/");
    lv_obj_set_style_text_font(dot2, FONT_SMALL, 0);
    lv_obj_set_style_text_color(dot2, LR_FG_DIM, 0);
    lv_obj_set_pos(dot2, 188, 5);

    /* 光照 (右) */
    lr_env_lux_lbl = lv_label_create(lr_env_panel);
    lv_label_set_text(lr_env_lux_lbl, "-- lx");
    lv_obj_set_style_text_font(lr_env_lux_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_env_lux_lbl, LR_FG_MID, 0);
    lv_obj_set_pos(lr_env_lux_lbl, 200, 5);
}

/* 更新环境显示 — 节流: 5s 一次 (环境数据不需要频繁更新) */
static uint32_t lr_env_last_tick = 0;
#define LR_ENV_INTERVAL_MS  5000
static void lr_env_update(const SensorData_t *d)
{
    if (!d || !lr_env_temp_lbl) return;
    TickType_t now = xTaskGetTickCount();
    if (lr_env_last_tick != 0 &&
        (now - lr_env_last_tick) < pdMS_TO_TICKS(LR_ENV_INTERVAL_MS)) return;
    lr_env_last_tick = now;

    char buf[16];
    /* SensorData_t 字段:
     *   temperature: centi-°C (2450 = 24.50°C)
     *   humidity:    centi-%   (6200 = 62.00%)
     *   lux_x100:    centi-lx  (32000 = 320 lx)
     *   soundIntensity: 0~100 百分比
     */
    int t_x100 = (int)(d->temperature);
    if (t_x100 < -5000) t_x100 = -5000;
    if (t_x100 >  8000) t_x100 =  8000;
    int t_int  = t_x100 / 100;
    int t_frac = abs(t_x100 % 100) / 10;  /* 保留一位小数 */
    if (t_frac < 0) t_frac = -t_frac;
    snprintf(buf, sizeof(buf), "%d.%d C", t_int, t_frac);
    lv_label_set_text(lr_env_temp_lbl, buf);

    int h = (int)(d->humidity / 100);
    if (h < 0)   h = 0;
    if (h > 100) h = 100;
    snprintf(buf, sizeof(buf), "%d %%", h);
    lv_label_set_text(lr_env_humi_lbl, buf);

    int lx = (int)(d->lux_x100 / 100);
    if (lx < 0)     lx = 0;
    if (lx > 99999) lx = 99999;
    snprintf(buf, sizeof(buf), "%d lx", lx);
    lv_label_set_text(lr_env_lux_lbl, buf);
}

/* ======================== 事件回调 ======================== */
static void lr_pause_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    LearningState_t s = State_GetCurrent();
    if (s == ST_LEARNING)      State_PostEvent(STATE_EVT_GUI_PAUSE,  0);
    else if (s == ST_PAUSED)   State_PostEvent(STATE_EVT_GUI_RESUME, 0);
}

static void lr_end_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    State_PostEvent(STATE_EVT_GUI_END, 0);
}

/* ======================== 调试面板 ========================
 *
 *  位置: 屏幕左侧, x=4, y=14, w=80, h=178
 *  默认隐藏; 点击顶部 "DBG" 按钮展开
 *
 *  内容 (14pt 分类结果 + 12pt 细节):
 *
 *    AI Model
 *    >> Focus         ← 主分类结果 (14pt, 跟随概率最大类染色)
 *    ────────
 *    F: 85.2%         ← 专注概率 (绿)
 *    D: 10.1%         ← 分心概率 (黄)
 *    T:  4.7%         ← 疲劳概率 (橙)
 *    ────────
 *    S:  85           ← 综合分
 *    V:  80           ← 视觉子分
 *    P: normal        ← 坐姿状态 (融合)
 *    Po:  77          ← 从机刚收到的原始姿态分数
 *    Pr: 0.0          ← 从机刚收到的原始压力值
 *    #1234            ← 帧序号
 *
 *  颜色用降饱和色, 仍与主界面气质一致.
 *  w=80 不与中央外环 (x=86) 重叠; 作为调试浮层, 底部略压住环境卡 (y=184),
 *  仅在开发者点击 DBG 时出现, 可接受.
 */
static lv_obj_t *lr_dbg_make_line(lv_obj_t *parent, const char *txt,
                                  int x, int y, lv_color_t color,
                                  const lv_font_t *font,
                                  lv_obj_t **out_lbl)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font ? font : FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    if (out_lbl) *out_lbl = l;
    return l;
}

static void lr_dbg_build(void)
{
    /* 面板底 (圆角暗卡) */
    lr_dbg_panel = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_dbg_panel, 80, 178);
    lv_obj_set_pos(lr_dbg_panel, 4, 14);
    lv_obj_set_style_bg_color(lr_dbg_panel, LR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(lr_dbg_panel, LV_OPA_80, 0);
    lv_obj_set_style_radius(lr_dbg_panel, 6, 0);
    lv_obj_set_style_border_color(lr_dbg_panel, LR_GREEN_SOFT, 0);
    lv_obj_set_style_border_width(lr_dbg_panel, 1, 0);
    lv_obj_set_style_border_opa(lr_dbg_panel, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(lr_dbg_panel, 0, 0);
    lv_obj_clear_flag(lr_dbg_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 行布局 (12pt 行高 ~13px, 14pt class 占 ~18px):
     *   y=2   标题 (12pt)
     *   y=15  class (14pt, 结束 y=33)
     *   y=35  divider 1
     *   y=45  F
     *   y=58  D
     *   y=71  T
     *   y=84  divider 2
     *   y=94  S  (融合总分)
     *   y=107 V  (视觉子分)
     *   y=120 P  (坐姿状态字符串)
     *   y=133 Po (从机原始姿态分数)
     *   y=146 Pr (从机原始压力值)
     *   y=159 #seq
     *  末行结束 y=171, 面板高 178 刚好容纳.
     */

    /* 标题 */
    lr_dbg_make_line(lr_dbg_panel, "AI Model", 6, 2,
                     LR_GREEN_SOFT, FONT_SMALL, &lr_dbg_lbl_ai);

    /* 主分类结果 (14pt, 跟随后续染色) */
    lr_dbg_make_line(lr_dbg_panel, ">> ---", 6, 15,
                     LR_FG_DIM, FONT_MID, &lr_dbg_lbl_class);

    /* 分隔线 1 */
    lr_dbg_make_line(lr_dbg_panel, "-------", 6, 35,
                     LR_FG_DIM, FONT_SMALL, NULL);

    /* F / D / T 概率 */
    lr_dbg_make_line(lr_dbg_panel, "F: --", 6, 45,
                     LR_GREEN_SOFT, FONT_SMALL, &lr_dbg_lbl_f);
    lr_dbg_make_line(lr_dbg_panel, "D: --", 6, 58,
                     LR_YELLOW_SOFT, FONT_SMALL, &lr_dbg_lbl_d);
    lr_dbg_make_line(lr_dbg_panel, "T: --", 6, 71,
                     LR_ORANGE_SOFT, FONT_SMALL, &lr_dbg_lbl_t);

    /* 分隔线 2 */
    lr_dbg_make_line(lr_dbg_panel, "-------", 6, 84,
                     LR_FG_DIM, FONT_SMALL, NULL);

    /* S 总分 / V 视觉分 / P 坐姿状态 */
    lr_dbg_make_line(lr_dbg_panel, "S: --", 6, 94,
                     LR_FG_SOFT, FONT_SMALL, &lr_dbg_lbl_s);
    lr_dbg_make_line(lr_dbg_panel, "V: --", 6, 107,
                     LR_FG_MID, FONT_SMALL, &lr_dbg_lbl_v);
    lr_dbg_make_line(lr_dbg_panel, "P: --", 6, 120,
                     LR_FG_MID, FONT_SMALL, &lr_dbg_lbl_p);

    /* 从机 (ESP32) 刚接收到的原始分数 / 压力值 */
    lr_dbg_make_line(lr_dbg_panel, "Po: --", 6, 133,
                     LR_PROG_TEAL, FONT_SMALL, &lr_dbg_lbl_po);
    lr_dbg_make_line(lr_dbg_panel, "Pr: --", 6, 146,
                     LR_PROG_CYAN, FONT_SMALL, &lr_dbg_lbl_pr);

    /* #帧号 */
    lr_dbg_make_line(lr_dbg_panel, "#0",   6, 159,
                     LR_FG_DIM, FONT_SMALL, &lr_dbg_lbl_seq);

    /* 默认隐藏 */
    lv_obj_add_flag(lr_dbg_panel, LV_OBJ_FLAG_HIDDEN);
}

/* DBG 按钮: 顶部小开关, 高亮表示当前显示 */
static void lr_dbg_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lr_dbg_visible = !lr_dbg_visible;
    if (lr_dbg_visible) {
        lv_obj_clear_flag(lr_dbg_panel, LV_OBJ_FLAG_HIDDEN);
        /* 按钮变高亮 */
        lv_obj_set_style_bg_opa(lr_btn_dbg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(lr_btn_dbg, LV_OPA_70, 0);
    } else {
        lv_obj_add_flag(lr_dbg_panel, LV_OBJ_FLAG_HIDDEN);
        /* 按钮恢复暗色 */
        lv_obj_set_style_bg_opa(lr_btn_dbg, LV_OPA_40, 0);
        lv_obj_set_style_border_opa(lr_btn_dbg, LV_OPA_20, 0);
    }
}

/* ======================== 公开 API ======================== */

void LearningScreen_Init(void)
{
    if (lr_screen != NULL) return;

    /* ====== 屏幕根对象 — 现代深色 ====== */
    lr_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(lr_screen, LR_BG_DEEP, 0);
    lv_obj_set_style_bg_opa(lr_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lr_screen, 0, 0);
    lv_obj_clear_flag(lr_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ==================== 顶部极小状态条 (y=0..14) ==================== */
    /* 左侧: 状态 (学习圆点 + 文字) */
    lv_obj_t *state_dot = lv_obj_create(lr_screen);
    lv_obj_set_size(state_dot, 6, 6);
    lv_obj_set_pos(state_dot, 12, 6);
    lv_obj_set_style_radius(state_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state_dot, LR_GREEN_SOFT, 0);
    lv_obj_set_style_bg_opa(state_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_width(state_dot, 0, 0);
    lv_obj_set_style_pad_all(state_dot, 0, 0);

    lr_label_state = lr_make_label(lr_screen, "Learning",
                                    FONT_SMALL, LR_FG_MID, 24, 3);

    /* 中间: 时钟 (偏右, 不抢眼) */
    lr_label_clock = lr_make_label(lr_screen, "--:--:--",
                                    FONT_SMALL, LR_FG_DIM, 168, 3);

    /* 右上: 摄像头缩略图 (极淡) */
    lr_cam_thumb = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_cam_thumb, 36, 14);
    lv_obj_set_pos(lr_cam_thumb, 272, 0);
    lv_obj_set_style_radius(lr_cam_thumb, 3, 0);
    lv_obj_set_style_bg_color(lr_cam_thumb, LR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(lr_cam_thumb, LV_OPA_40, 0);
    lv_obj_set_style_border_color(lr_cam_thumb, LR_FG_DIM, 0);
    lv_obj_set_style_border_width(lr_cam_thumb, 1, 0);
    lv_obj_set_style_border_opa(lr_cam_thumb, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(lr_cam_thumb, 0, 0);
    lv_obj_clear_flag(lr_cam_thumb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cam_lbl = lv_label_create(lr_cam_thumb);
    lv_label_set_text(cam_lbl, "CAM");
    lv_obj_set_style_text_font(cam_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(cam_lbl, LR_FG_DIM, 0);
    lv_obj_set_style_text_opa(cam_lbl, LV_OPA_50, 0);
    lv_obj_center(cam_lbl);

    /* WiFi 文字 (状态点右, 极小) — 简化: 复用 wifi label */
    lr_label_wifi = lr_make_label(lr_screen, "",
                                   FONT_SMALL, LR_FG_DIM, 232, 3);

    /* ==================== 外环: 学习进度 ====================
     * 中心 (160, 105), 半径 74, 跨 270° (起点 135°)
     * 线宽 2, 极淡, 像背景装饰, 不抢眼 */
    lr_arc_progress = lv_arc_create(lr_screen);
    lv_obj_set_size(lr_arc_progress,
                    (lv_coord_t)(LR_PROGRESS_RADIUS * 2),
                    (lv_coord_t)(LR_PROGRESS_RADIUS * 2));
    lv_obj_set_pos(lr_arc_progress,
                   (lv_coord_t)(LR_CENTER_X - LR_PROGRESS_RADIUS),
                   (lv_coord_t)(LR_CENTER_Y - LR_PROGRESS_RADIUS));
    lv_arc_set_range(lr_arc_progress, 0, 100);
    lv_arc_set_value(lr_arc_progress, 0);
    lv_arc_set_rotation(lr_arc_progress, 135);
    lv_arc_set_bg_angles(lr_arc_progress, 0, 270);
    /* 轨道 (极暗) */
    lv_obj_set_style_arc_color(lr_arc_progress, LR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(lr_arc_progress, LR_RING_WIDTH_OUT, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(lr_arc_progress, LV_OPA_50, LV_PART_MAIN);
    /* 指示器 — 柔青 */
    lv_obj_set_style_arc_color(lr_arc_progress, LR_PROG_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(lr_arc_progress, LR_RING_WIDTH_OUT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(lr_arc_progress, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(lr_arc_progress, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_clear_flag(lr_arc_progress, LV_OBJ_FLAG_CLICKABLE);

    /* ==================== 内环: 专注度 (呼吸) ====================
     * 中心 (160, 105), 半径 58, 跨 270°
     * 默认柔绿, 启动呼吸 */
    lr_arc_focus = lv_arc_create(lr_screen);
    lv_obj_set_size(lr_arc_focus,
                    (lv_coord_t)(LR_FOCUS_RADIUS * 2),
                    (lv_coord_t)(LR_FOCUS_RADIUS * 2));
    lv_obj_set_pos(lr_arc_focus,
                   (lv_coord_t)(LR_CENTER_X - LR_FOCUS_RADIUS),
                   (lv_coord_t)(LR_CENTER_Y - LR_FOCUS_RADIUS));
    lv_arc_set_range(lr_arc_focus, 0, 100);
    lv_arc_set_value(lr_arc_focus, 100);
    lv_arc_set_rotation(lr_arc_focus, 135);
    lv_arc_set_bg_angles(lr_arc_focus, 0, 270);
    /* 轨道 (暗) */
    lv_obj_set_style_arc_color(lr_arc_focus, LR_TRACK_SOFT, LV_PART_MAIN);
    lv_obj_set_style_arc_width(lr_arc_focus, LR_RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(lr_arc_focus, LV_OPA_60, LV_PART_MAIN);
    /* 指示器 — 柔绿, 启动呼吸 */
    lv_obj_set_style_arc_color(lr_arc_focus, LR_GREEN_SOFT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(lr_arc_focus, LR_RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(lr_arc_focus, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(lr_arc_focus, LV_OBJ_FLAG_CLICKABLE);

    lr_focus_cur_color = LR_GREEN_SOFT;
    lr_focus_anim_obj  = lr_arc_focus;
    lr_start_breath_anim(lr_arc_focus);

    /* ==================== 中央时间 (大字, 现代化排版) ====================
     * 居中, 28pt 柔白
     * 上方 28pt 时间 (m:ss 格式), 下方 12pt 目标分钟数 */
    lr_label_time = lv_label_create(lr_screen);
    lv_label_set_text(lr_label_time, "0:00");
    lv_obj_set_style_text_font(lr_label_time, FONT_TIME, 0);
    lv_obj_set_style_text_color(lr_label_time, LR_FG_SOFT, 0);
    lv_obj_set_width(lr_label_time, 110);
    lv_obj_set_style_text_align(lr_label_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lr_label_time, LR_CENTER_X - 55, LR_CENTER_Y - 22);

    lr_label_target = lv_label_create(lr_screen);
    lv_label_set_text(lr_label_target, "0 / 30 min");
    lv_obj_set_style_text_font(lr_label_target, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_label_target, LR_FG_DIM, 0);
    lv_obj_set_width(lr_label_target, 110);
    lv_obj_set_style_text_align(lr_label_target, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lr_label_target, LR_CENTER_X - 55, LR_CENTER_Y + 14);

    /* ==================== 环境信息卡 (y=184..206) ==================== */
    lr_env_build();

    /* ==================== 调试面板 (默认隐藏) ==================== */
    lr_dbg_build();

    /* ==================== DBG 按钮 (顶部小开关) ====================
     * 位置: x=88, y=2, w=22, h=11 (在 "Learning" 文字和时钟之间)
     * 暗色小标签, 点击后变高亮表示调试面板已展开
     */
    lr_btn_dbg = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_dbg, 22, 11);
    lv_obj_set_pos(lr_btn_dbg, 88, 2);
    lv_obj_set_style_bg_color(lr_btn_dbg, LR_BG_GLOW, 0);
    lv_obj_set_style_bg_opa(lr_btn_dbg, LV_OPA_40, 0);
    lv_obj_set_style_radius(lr_btn_dbg, 3, 0);
    lv_obj_set_style_border_color(lr_btn_dbg, LR_FG_DIM, 0);
    lv_obj_set_style_border_width(lr_btn_dbg, 1, 0);
    lv_obj_set_style_border_opa(lr_btn_dbg, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(lr_btn_dbg, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_dbg, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(lr_btn_dbg);
        lv_label_set_text(lbl, "DBG");
        lv_obj_set_style_text_color(lbl, LR_FG_MID, 0);
        lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(lr_btn_dbg, lr_dbg_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== 噪音波形 (y=210..222) ==================== */
    lr_wave_init();

    /* ==================== 极小控制按钮 (y=222..240) ====================
     * 右下角两个胶囊小按钮, 暗灰, 不抢眼
     *   Pause: x=178, w=66, h=18
     *   End:   x=248, w=58, h=18
     */
    lr_btn_pause = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_pause, 66, 18);
    lv_obj_set_pos(lr_btn_pause, 178, 220);
    lv_obj_set_style_bg_color(lr_btn_pause, LR_BG_GLOW, 0);
    lv_obj_set_style_bg_opa(lr_btn_pause, LV_OPA_60, 0);
    lv_obj_set_style_radius(lr_btn_pause, 9, 0);
    lv_obj_set_style_border_width(lr_btn_pause, 1, 0);
    lv_obj_set_style_border_color(lr_btn_pause, LR_FG_DIM, 0);
    lv_obj_set_style_border_opa(lr_btn_pause, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(lr_btn_pause, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_pause, 0, 0);
    lr_pause_lbl = lv_label_create(lr_btn_pause);
    lv_label_set_text(lr_pause_lbl, "Pause");
    lv_obj_set_style_text_color(lr_pause_lbl, LR_FG_MID, 0);
    lv_obj_set_style_text_font(lr_pause_lbl, FONT_SMALL, 0);
    lv_obj_center(lr_pause_lbl);
    lv_obj_add_event_cb(lr_btn_pause, lr_pause_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lr_btn_end = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_end, 58, 18);
    lv_obj_set_pos(lr_btn_end, 248, 220);
    lv_obj_set_style_bg_color(lr_btn_end, LR_BG_GLOW, 0);
    lv_obj_set_style_bg_opa(lr_btn_end, LV_OPA_60, 0);
    lv_obj_set_style_radius(lr_btn_end, 9, 0);
    lv_obj_set_style_border_width(lr_btn_end, 1, 0);
    lv_obj_set_style_border_color(lr_btn_end, LR_FG_DIM, 0);
    lv_obj_set_style_border_opa(lr_btn_end, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(lr_btn_end, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_end, 0, 0);
    lv_obj_t *end_lbl = lv_label_create(lr_btn_end);
    lv_label_set_text(end_lbl, "End");
    lv_obj_set_style_text_color(end_lbl, LR_FG_MID, 0);
    lv_obj_set_style_text_font(end_lbl, FONT_SMALL, 0);
    lv_obj_center(end_lbl);
    lv_obj_add_event_cb(lr_btn_end, lr_end_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *LearningScreen_GetScreen(void)
{
    return lr_screen;
}

/* 状态文字 + 暂停/结束按钮 状态同步 */
void LearningScreen_UpdateStatus(void)
{
    if (!lr_label_state) return;
    LearningState_t s = State_GetCurrent();

    /* 顶部状态文字 */
    switch (s)
    {
    case ST_LEARNING: lv_label_set_text(lr_label_state, "Learning"); break;
    case ST_PAUSED:   lv_label_set_text(lr_label_state, "Paused");   break;
    case ST_AWAY:     lv_label_set_text(lr_label_state, "Away");     break;
    default:          lv_label_set_text(lr_label_state, "");         break;
    }

    /* 暂停/继续按钮文字 */
    if (lr_pause_lbl) {
        if (s == ST_PAUSED)      lv_label_set_text(lr_pause_lbl, "Resume");
        else if (s == ST_AWAY)   lv_label_set_text(lr_pause_lbl, "Away");
        else                     lv_label_set_text(lr_pause_lbl, "Pause");
    }
    /* AWAY 状态: 按钮置灰 */
    if (lr_btn_pause) {
        if (s == ST_AWAY) {
            lv_obj_set_style_bg_opa(lr_btn_pause, LV_OPA_20, 0);
            lv_obj_set_style_border_opa(lr_btn_pause, LV_OPA_10, 0);
        } else {
            lv_obj_set_style_bg_opa(lr_btn_pause, LV_OPA_60, 0);
            lv_obj_set_style_border_opa(lr_btn_pause, LV_OPA_30, 0);
        }
    }

    /* ===== 圆环颜色根据持续状态平滑过渡 =====
     * 关键: 阈值拉长 (5min+), 不被模型瞬时 Distract 状态左右
     * 体现"陪伴" — 默认柔绿, 严重异常才缓慢变暖色 */
    uint32_t sp_ms = State_GetSustainedPosture();
    uint32_t sf_ms = State_GetSustainedFatigue();
    uint32_t sd_ms = State_GetSustainedDistract();

    lv_color_t target_color = LR_GREEN_SOFT;
    if (sp_ms >= 5UL * 60UL * 1000UL) {
        target_color = LR_PINK_SOFT;        /* 坐姿异常 5min+ */
    } else if (sf_ms >= 5UL * 60UL * 1000UL) {
        target_color = LR_ORANGE_SOFT;      /* 疲劳 5min+ */
    } else if (sd_ms >= 5UL * 60UL * 1000UL) {
        /* 分心阈值也拉到 5min, 模型误报不会触发 */
        target_color = LR_YELLOW_SOFT;
    }
    lr_focus_fade_to(target_color);
}

/* 室内数据更新 — 推波形 + 更新环境卡 */
void LearningScreen_UpdateSensor(const SensorData_t *d)
{
    if (!d) return;

    /* 节流: 250ms 推一次波形 */
    TickType_t now = xTaskGetTickCount();
    if ((now - lr_wave_last_tick) >= pdMS_TO_TICKS(LR_WAVE_SAMPLE_MS)) {
        lr_wave_last_tick = now;
        uint8_t sound = (d->soundIntensity > 100) ? 100 : (uint8_t)d->soundIntensity;
        lr_wave_push(sound);
    }

    /* 环境卡 — 5s 节流 */
    lr_env_update(d);
}

/* 中央大时间更新
 *   < 60s   -> "0:SS"
 *   >= 60s  -> "M"
 *   同时驱动学习进度环 (按目标时长占比)
 *   数据源: StateTask 累加器 (PAUSED/AWAY 时自动冻结) */
void LearningScreen_UpdateStopwatch(void)
{
    if (!lr_label_time) return;

    uint32_t sec = State_GetElapsedSeconds();
    if (sec == lr_last_elapsed_sec) return;
    lr_last_elapsed_sec = sec;

    char buf[24];
    if (sec < 60) {
        snprintf(buf, sizeof(buf), "0:%02lu", (unsigned long)sec);
    } else {
        uint32_t minutes = sec / 60;
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)minutes);
    }
    lv_label_set_text(lr_label_time, buf);

    /* 目标提示: "M / 30 min" */
    if (lr_label_target) {
        LearningState_t s = State_GetCurrent();
        if (s == ST_LEARNING || s == ST_PAUSED || s == ST_AWAY) {
            uint32_t minutes = sec / 60;
            snprintf(buf, sizeof(buf), "%lu / 30 min", (unsigned long)minutes);
        } else {
            snprintf(buf, sizeof(buf), "0 / 30 min");
        }
        lv_label_set_text(lr_label_target, buf);
    }

    /* 进度环: 按 30min 假目标 */
    {
        const uint32_t tgt = 30UL * 60UL;
        uint32_t pct = (sec * 100UL) / tgt;
        if (pct > 100) pct = 100;
        lr_progress_anim_to((int32_t)pct);
    }
}

/* 顶部 WiFi + 时钟刷新 */
void LearningScreen_RefreshBar(void)
{
    if (lr_label_wifi) {
        if (g_last_remote_tick == 0) {
            lv_label_set_text(lr_label_wifi, "");
        } else if ((xTaskGetTickCount() - g_last_remote_tick) < pdMS_TO_TICKS(30000)) {
            lv_label_set_text(lr_label_wifi, "ON");
        } else {
            lv_label_set_text(lr_label_wifi, "OFF");
        }
    }
    if (lr_label_clock) {
        WeatherInfo_t wi;
        char buf[16];
        if (Weather_GetLatest(&wi) == 0 && wi.time[0] != '\0') {
            snprintf(buf, sizeof(buf), "%s", wi.time);
        } else {
            snprintf(buf, sizeof(buf), "--:--:--");
        }
        lv_label_set_text(lr_label_clock, buf);
    }
}

/* ======================== 调试面板更新 ========================
 * 把 AI 推理的实时结果 + 融合数据 推送到调试面板.
 * 由 DisplayTask 在每次融合结果更新时调用.
 *
 * 关键: F / D / T 概率必须从 AITask 实时取, 不可走 FusionTask
 *       (Fusion 1Hz 轮询, 滞后 0~1s, 与 Picker 屏幕显示不一致)
 *       评分 / 坐姿 从 FusionResult_t 取 (1Hz 足够, 这些数据变化慢)
 */
void LearningScreen_UpdateFusion(const FusionResult_t *f)
{
    if (!lr_dbg_panel) return;

    char buf[24];

    /* ===== F / D / T 概率: 走 AITask 实时通道 ===== */
    float probs[3] = {0.0f, 0.0f, 0.0f};
    uint32_t ai_ts = 0;
    int ai_valid = (AI_GetLatestProbs(probs, &ai_ts) == 0);

    int f_pct = 0, d_pct = 0, t_pct = 0;
    if (ai_valid) {
        f_pct = (int)(probs[0] * 1000.0f + 0.5f);  // 0.0~1.0 → 0~1000
        d_pct = (int)(probs[1] * 1000.0f + 0.5f);
        t_pct = (int)(probs[2] * 1000.0f + 0.5f);
        if (f_pct > 1000) f_pct = 1000; if (f_pct < 0) f_pct = 0;
        if (d_pct > 1000) d_pct = 1000; if (d_pct < 0) d_pct = 0;
        if (t_pct > 1000) t_pct = 1000; if (t_pct < 0) t_pct = 0;
    }

    if (lr_dbg_lbl_f) {
        if (ai_valid) snprintf(buf, sizeof(buf), "F:%3d.%d%%", f_pct / 10, f_pct % 10);
        else          snprintf(buf, sizeof(buf), "F: --");
        lv_label_set_text(lr_dbg_lbl_f, buf);
    }
    if (lr_dbg_lbl_d) {
        if (ai_valid) snprintf(buf, sizeof(buf), "D:%3d.%d%%", d_pct / 10, d_pct % 10);
        else          snprintf(buf, sizeof(buf), "D: --");
        lv_label_set_text(lr_dbg_lbl_d, buf);
    }
    if (lr_dbg_lbl_t) {
        if (ai_valid) snprintf(buf, sizeof(buf), "T:%3d.%d%%", t_pct / 10, t_pct % 10);
        else          snprintf(buf, sizeof(buf), "T: --");
        lv_label_set_text(lr_dbg_lbl_t, buf);
    }

    /* ===== 主分类结果 (取 3 个概率中的最大者) =====
     * 平局时按优先级: F > D > T
     * 用相应颜色染色, 一目了然
     * 类名取短以适配 14pt 字体: Focus / Distract / Tired */
    if (lr_dbg_lbl_class) {
        if (!ai_valid) {
            lv_label_set_text(lr_dbg_lbl_class, ">> ---");
            lv_obj_set_style_text_color(lr_dbg_lbl_class, LR_FG_DIM, 0);
        } else {
            int max_pct = f_pct;
            if (d_pct > max_pct) max_pct = d_pct;
            if (t_pct > max_pct) max_pct = t_pct;

            const char *class_name;
            lv_color_t  class_color;
            if (max_pct == f_pct) {
                class_name = "Focus";     class_color = LR_GREEN_SOFT;
            } else if (max_pct == d_pct) {
                class_name = "Distract";  class_color = LR_YELLOW_SOFT;
            } else {
                class_name = "Tired";     class_color = LR_ORANGE_SOFT;
            }

            snprintf(buf, sizeof(buf), ">> %s", class_name);
            lv_label_set_text(lr_dbg_lbl_class, buf);
            lv_obj_set_style_text_color(lr_dbg_lbl_class, class_color, 0);
        }
    }

    /* ===== S / V / P 走 Fusion (1Hz 足够, 这些数据变化慢) ===== */
    if (f) {
        snprintf(buf, sizeof(buf), "S:%3d", f->total_score);
        if (lr_dbg_lbl_s) lv_label_set_text(lr_dbg_lbl_s, buf);

        snprintf(buf, sizeof(buf), "V:%3d", f->vision_score);
        if (lr_dbg_lbl_v) lv_label_set_text(lr_dbg_lbl_v, buf);

        /* P 坐姿状态 (字符串来自从机, 截前 8 字符防溢出) */
        char p_buf[12];
        strncpy(p_buf, f->posture_state, sizeof(p_buf) - 1);
        p_buf[sizeof(p_buf) - 1] = '\0';
        snprintf(buf, sizeof(buf), "P:%.8s", p_buf);
        if (lr_dbg_lbl_p) lv_label_set_text(lr_dbg_lbl_p, buf);
    }

    /* ===== Po / Pr: 从机 (ESP32) 刚接收到的原始姿态分数 + 压力值 =====
     * 直接走 Slave_GetLatest 取最新一帧, 不经融合, 反映"刚收到"的值。
     * 无数据时显示 "--" 占位。 */
    {
        SlaveData_t sd;
        if (Slave_GetLatest(&sd) == 0) {
            snprintf(buf, sizeof(buf), "Po:%3ld", (long)sd.score);
            if (lr_dbg_lbl_po) lv_label_set_text(lr_dbg_lbl_po, buf);

            snprintf(buf, sizeof(buf), "Pr:%.1f", (double)sd.pressure);
            if (lr_dbg_lbl_pr) lv_label_set_text(lr_dbg_lbl_pr, buf);
        } else {
            if (lr_dbg_lbl_po) lv_label_set_text(lr_dbg_lbl_po, "Po: --");
            if (lr_dbg_lbl_pr) lv_label_set_text(lr_dbg_lbl_pr, "Pr: --");
        }
    }

    /* ===== 帧号: 用 AI_Result_t.seq (真实推理序号, 每完成一次推理 +1) =====
     * 这样能直接看到 AI 是否在跑 (数字应稳定增长, 不会回退)
     * 通过 AI_GetLatestResult 拿完整结果, 而非包装的 AI_GetLatestProbs
     * (后者不带 seq 信息) */
    if (lr_dbg_lbl_seq) {
        AI_Result_t ai_full;
        if (AI_GetLatestResult(&ai_full) == 0) {
            /* 真实 seq, mod 10000 显示 4 位数 */
            snprintf(buf, sizeof(buf), "#%04lu", (unsigned long)(ai_full.seq % 10000u));
        } else {
            snprintf(buf, sizeof(buf), "#----");
        }
        lv_label_set_text(lr_dbg_lbl_seq, buf);
    }
}
