/**
  ******************************************************************************
  * @file    LearningScreen.c
  * @brief   学习专注界面 LVGL 实现 (从 Display.c 抽出)
  *          配色: 纯黑底, 白字, 温度红 / 湿度蓝 / 光照琥珀.
  *          布局 (y 坐标, 320x240):
  *            0..24      [WiFi | 时钟 | 状态]        顶部细条
  *            50..110         0:42 / 25              中央超大时间 (montserrat_48, 白)
  *            138..158  [T][████░░] 25.3 °C         温度条
  *            162..182  [H][██████] 65 %            湿度条
  *            186..206  [L][███░░░] 350 lx          光照条
  *            206..234  [ Pause ]   [  End  ]       底部控制按钮
  ******************************************************************************
  */

#include "LearningScreen.h"
#include "StateTask.h"        /* LearningState_t, State_GetCurrent, State_PostEvent */
#include "WeatherTask.h"      /* WeatherInfo_t, Weather_GetLatest */
#include "lcd.h"              /* LCD_direction */
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* ======================== 字体定义 (与 Display.c 同步) ========================
 * 注: lv_conf.h 只启用了 montserrat_12/14/16/18/20/28/48, Alatsi 不可用,
 *     大字时间用最大的 montserrat_48 */
#define FONT_BIG        &lv_font_montserrat_48
#define FONT_EN12       &lv_font_montserrat_12
#define FONT_EN14       &lv_font_montserrat_14

/* ======================== 配色 ======================== */
#define LR_BG          lv_color_hex(0x000000)   /* 主背景 - 纯黑 */
#define LR_FG          lv_color_hex(0xFFFFFF)   /* 主前景 - 白 */
#define LR_DIM         lv_color_hex(0x6B7280)   /* 副前景 - 中灰 (备用) */
#define LR_TEMP_COLOR  lv_color_hex(0xEF4444)   /* 温度条 - 红 500 */
#define LR_HUMI_COLOR  lv_color_hex(0x3B82F6)   /* 湿度条 - 蓝 500 */
#define LR_LUX_COLOR   lv_color_hex(0xF59E0B)   /* 光照条 - 琥珀 500 */
#define LR_BAR_TRACK   lv_color_hex(0x1F2937)   /* 条形轨道 - 暗灰 */

/* ======================== 节流 ========================
 * 温度/湿度: 1 分钟更新一次 (温度不会突变, 没必要频繁刷)
 * 光照:      3 秒更新一次 (环境光变化较快, 需较实时) */
#define LR_TH_INTERVAL_MS   60000   /* 温湿度条更新间隔: 1 分钟 */
#define LR_LUX_INTERVAL_MS   3000   /* 光照条更新间隔:  3 秒   */

/* ======================== 控件指针 (本文件私有) ======================== */
static lv_obj_t *lr_screen         = NULL;  /* 学习屏幕根对象 */
static lv_obj_t *lr_label_time     = NULL;  /* 中央大字时间 */
static lv_obj_t *lr_label_status   = NULL;  /* 顶部状态文字 */
static lv_obj_t *lr_label_wifi     = NULL;  /* 顶部 WiFi 文字 */
static lv_obj_t *lr_label_clock    = NULL;  /* 顶部时钟 */

static lv_obj_t *lr_bar_temp       = NULL;  /* 温度条 */
static lv_obj_t *lr_bar_humi       = NULL;  /* 湿度条 */
static lv_obj_t *lr_bar_lux        = NULL;  /* 光照条 */
static lv_obj_t *lr_label_temp_v   = NULL;  /* 温度数值文本 */
static lv_obj_t *lr_label_humi_v   = NULL;  /* 湿度数值文本 */
static lv_obj_t *lr_label_lux_v    = NULL;  /* 光照数值文本 */

static lv_obj_t *lr_btn_pause      = NULL;  /* 暂停/继续 按钮 */
static lv_obj_t *lr_btn_end        = NULL;  /* 结束按钮 */
static lv_obj_t *lr_pause_lbl      = NULL;  /* 暂停按钮文字 */

static TickType_t lr_last_th_tick   = 0;  /* 上次刷新温湿度的时刻 */
static TickType_t lr_last_lux_tick  = 0;  /* 上次刷新光照的时刻   */

/* 远端数据时间戳: 定义在 Display.c, extern 声明见 LearningScreen.h
 * (此处不能再写 "uint32_t g_last_remote_tick = 0", 否则链接时重复定义) */

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

/* ======================== 事件回调 ======================== */
static void lr_pause_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    LearningState_t s = State_GetCurrent();
    if (s == ST_LEARNING)      State_PostEvent(STATE_EVT_GUI_PAUSE,  0);
    else if (s == ST_PAUSED)   State_PostEvent(STATE_EVT_GUI_RESUME, 0);
    /* AWAY 不响应 (用户应先坐下, 等自动恢复) */
}

static void lr_end_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    State_PostEvent(STATE_EVT_GUI_END, 0);
}

/* ======================== 公开 API ======================== */

void LearningScreen_Init(void)
{
    if (lr_screen != NULL) return;  /* 防止重复初始化 */

    /* 屏幕根对象 */
    lr_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(lr_screen, LR_BG, 0);
    lv_obj_set_style_bg_opa(lr_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lr_screen, 0, 0);
    lv_obj_clear_flag(lr_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ==================== 顶部状态条 (细条 24px) ==================== */
    lr_label_wifi   = lr_make_label(lr_screen, "--",
                                    FONT_EN12, LR_FG, 4, 6);
    lr_label_clock  = lr_make_label(lr_screen, "00:00:00",
                                    FONT_EN12, LR_FG, 96, 6);
    lr_label_status = lr_make_label(lr_screen, "Learning",
                                    FONT_EN12, LR_FG, 232, 6);

    /* ==================== 中央大时间 (Alatsi 50, 白色) ==================== */
    lr_label_time = lr_make_label(lr_screen, "0:00",
                                  FONT_BIG, LR_FG, 0, 50);
    lv_obj_set_width(lr_label_time, 320);
    lv_obj_set_style_text_align(lr_label_time, LV_TEXT_ALIGN_CENTER, 0);

    /* ==================== 3 条传感器条 (横向, 堆叠) ==================== */
    {
        const int BAR_X        = 28;    /* 条形起点 x */
        const int BAR_W        = 220;   /* 条形宽度 */
        const int BAR_H        = 14;    /* 条形高度 */
        const int VAL_X        = 256;   /* 数值文字起点 x */
        const int ROW_GAP      = 24;    /* 行高 */
        const int ROW_Y0       = 138;   /* 起始 y */

        /* --- 温度 --- */
        lv_obj_t *t_lbl = lr_make_label(lr_screen, "T",
                                        FONT_EN14, LR_TEMP_COLOR, 8, ROW_Y0);
        (void)t_lbl;
        lr_bar_temp = lv_bar_create(lr_screen);
        lv_obj_set_size(lr_bar_temp, BAR_W, BAR_H);
        lv_obj_set_pos(lr_bar_temp, BAR_X, ROW_Y0 + 2);
        lv_bar_set_range(lr_bar_temp, 0, 500);   /* 0-50.0°C * 10 */
        lv_bar_set_value(lr_bar_temp, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(lr_bar_temp, LR_BAR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lr_bar_temp, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(lr_bar_temp, LR_TEMP_COLOR, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(lr_bar_temp, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(lr_bar_temp, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(lr_bar_temp, 3, LV_PART_INDICATOR);
        lr_label_temp_v = lr_make_label(lr_screen, "--",
                                        FONT_EN14, LR_TEMP_COLOR, VAL_X, ROW_Y0);

        /* --- 湿度 --- */
        int y1 = ROW_Y0 + ROW_GAP;
        lv_obj_t *h_lbl = lr_make_label(lr_screen, "H",
                                        FONT_EN14, LR_HUMI_COLOR, 8, y1);
        (void)h_lbl;
        lr_bar_humi = lv_bar_create(lr_screen);
        lv_obj_set_size(lr_bar_humi, BAR_W, BAR_H);
        lv_obj_set_pos(lr_bar_humi, BAR_X, y1 + 2);
        lv_bar_set_range(lr_bar_humi, 0, 100);
        lv_bar_set_value(lr_bar_humi, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(lr_bar_humi, LR_BAR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lr_bar_humi, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(lr_bar_humi, LR_HUMI_COLOR, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(lr_bar_humi, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(lr_bar_humi, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(lr_bar_humi, 3, LV_PART_INDICATOR);
        lr_label_humi_v = lr_make_label(lr_screen, "--",
                                        FONT_EN14, LR_HUMI_COLOR, VAL_X, y1);

        /* --- 光照 --- */
        int y2 = ROW_Y0 + ROW_GAP * 2;
        lv_obj_t *l_lbl = lr_make_label(lr_screen, "L",
                                        FONT_EN14, LR_LUX_COLOR, 8, y2);
        (void)l_lbl;
        lr_bar_lux = lv_bar_create(lr_screen);
        lv_obj_set_size(lr_bar_lux, BAR_W, BAR_H);
        lv_obj_set_pos(lr_bar_lux, BAR_X, y2 + 2);
        lv_bar_set_range(lr_bar_lux, 0, 1000);   /* 0-1000 lx */
        lv_bar_set_value(lr_bar_lux, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(lr_bar_lux, LR_BAR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lr_bar_lux, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(lr_bar_lux, LR_LUX_COLOR, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(lr_bar_lux, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(lr_bar_lux, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(lr_bar_lux, 3, LV_PART_INDICATOR);
        lr_label_lux_v = lr_make_label(lr_screen, "--",
                                       FONT_EN14, LR_LUX_COLOR, VAL_X, y2);
    }

    /* ==================== 底部控制按钮 ==================== */
    /* 暂停/继续 (左) */
    lr_btn_pause = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_pause, 138, 28);
    lv_obj_set_pos(lr_btn_pause, 20, 206);
    lv_obj_set_style_bg_color(lr_btn_pause, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_bg_opa(lr_btn_pause, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lr_btn_pause, 8, 0);
    lv_obj_set_style_shadow_width(lr_btn_pause, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_pause, 0, 0);
    lr_pause_lbl = lv_label_create(lr_btn_pause);
    lv_label_set_text(lr_pause_lbl, "Pause");
    lv_obj_set_style_text_color(lr_pause_lbl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lr_pause_lbl, FONT_EN14, 0);
    lv_obj_center(lr_pause_lbl);
    lv_obj_add_event_cb(lr_btn_pause, lr_pause_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* 结束 (右) */
    lr_btn_end = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_end, 138, 28);
    lv_obj_set_pos(lr_btn_end, 162, 206);
    lv_obj_set_style_bg_color(lr_btn_end, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_bg_opa(lr_btn_end, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lr_btn_end, 8, 0);
    lv_obj_set_style_shadow_width(lr_btn_end, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_end, 0, 0);
    lv_obj_t *end_lbl = lv_label_create(lr_btn_end);
    lv_label_set_text(end_lbl, "End");
    lv_obj_set_style_text_color(end_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(end_lbl, FONT_EN14, 0);
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
    if (!lr_label_status) return;
    LearningState_t s = State_GetCurrent();

    /* 刚进入 LEARNING: 重置两条节流, 让首次读数立即显示 */
    static LearningState_t s_prev = (LearningState_t)-1;
    if (s == ST_LEARNING && s_prev != ST_LEARNING) {
        lr_last_th_tick  = 0;
        lr_last_lux_tick = 0;
    }
    s_prev = s;

    switch (s)
    {
    case ST_LEARNING: lv_label_set_text(lr_label_status, "Learning"); break;
    case ST_PAUSED:   lv_label_set_text(lr_label_status, "Paused");   break;
    case ST_AWAY:     lv_label_set_text(lr_label_status, "Away");     break;
    default:          lv_label_set_text(lr_label_status, "");        break;
    }
    /* 同步更新暂停/继续按钮文字 */
    if (lr_pause_lbl) {
        if (s == ST_PAUSED)      lv_label_set_text(lr_pause_lbl, "Resume");
        else if (s == ST_AWAY)   lv_label_set_text(lr_pause_lbl, "Away");
        else                     lv_label_set_text(lr_pause_lbl, "Pause");
    }
    /* AWAY 状态: 暂停按钮置灰 */
    if (lr_btn_pause) {
        if (s == ST_AWAY) lv_obj_add_state(lr_btn_pause, LV_STATE_DISABLED);
        else              lv_obj_clear_state(lr_btn_pause, LV_STATE_DISABLED);
    }
}

/* 室内数据更新
 *   温度/湿度: LR_TH_INTERVAL_MS  (1 分钟) 节流
 *   光照:      LR_LUX_INTERVAL_MS (3 秒)   节流
 *   第一次 (对应 tick == 0) 立即更新, 之后必须等到下一个间隔 */
void LearningScreen_UpdateSensor(const SensorData_t *d)
{
    if (!d) return;

    TickType_t now = xTaskGetTickCount();
    char buf[16];

    /* ---------- 温度 + 湿度 (1 分钟节流) ---------- */
    if (lr_last_th_tick == 0 ||
        (now - lr_last_th_tick) >= pdMS_TO_TICKS(LR_TH_INTERVAL_MS)) {

        lr_last_th_tick = now;

        /* 温度 (0-50°C, 显示 1 位小数) */
        if (lr_label_temp_v && lr_bar_temp) {
            int t_x10 = (int)(d->temperature / 10);  /* 例 253 = 25.3°C */
            if (t_x10 < 0) t_x10 = 0;
            if (t_x10 > 500) t_x10 = 500;
            lv_bar_set_value(lr_bar_temp, t_x10, LV_ANIM_OFF);
            snprintf(buf, sizeof(buf), "%d.%d C", t_x10 / 10, t_x10 % 10);
            lv_label_set_text(lr_label_temp_v, buf);
        }

        /* 湿度 (0-100%) */
        if (lr_label_humi_v && lr_bar_humi) {
            int h = (int)(d->humidity / 100);
            if (h < 0) h = 0;
            if (h > 100) h = 100;
            lv_bar_set_value(lr_bar_humi, h, LV_ANIM_OFF);
            snprintf(buf, sizeof(buf), "%d %%", h);
            lv_label_set_text(lr_label_humi_v, buf);
        }
    }

    /* ---------- 光照 (3 秒节流) ---------- */
    if (lr_last_lux_tick == 0 ||
        (now - lr_last_lux_tick) >= pdMS_TO_TICKS(LR_LUX_INTERVAL_MS)) {

        lr_last_lux_tick = now;

        if (lr_label_lux_v && lr_bar_lux) {
            int lx = (int)(d->lux_x100 / 100);
            if (lx < 0) lx = 0;
            if (lx > 1000) lx = 1000;
            lv_bar_set_value(lr_bar_lux, lx, LV_ANIM_OFF);
            snprintf(buf, sizeof(buf), "%d lx", lx);
            lv_label_set_text(lr_label_lux_v, buf);
        }
    }
}

/* 中央大时间更新
 *   < 60s   -> "0:SS"
 *   >= 60s  -> "M" */
void LearningScreen_UpdateStopwatch(void)
{
    if (!lr_label_time) return;
    static uint32_t s_last_disp_seconds = (uint32_t)-1;
    uint32_t sec = State_GetElapsedSeconds();
    if (sec == s_last_disp_seconds) return;
    s_last_disp_seconds = sec;

    char buf[20];
    if (sec < 60) {
        snprintf(buf, sizeof(buf), "0:%02lu", (unsigned long)sec);
    } else {
        uint32_t minutes = sec / 60;
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)minutes);
    }
    lv_label_set_text(lr_label_time, buf);
}

/* 顶部 WiFi + 时钟刷新 */
void LearningScreen_RefreshBar(void)
{
    if (lr_label_wifi) {
        if (g_last_remote_tick == 0) {
            lv_label_set_text(lr_label_wifi, "--");
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
