/**
  ******************************************************************************
  * @file    WeatherDisplay.c
  * @brief   320x240 分辨率天气显示界面 LVGL 布局实现
  *          (由 WeatherTask.c 提供 LVGL 数据源, 显示室内外传感器数据)
  *
  *          屏幕布局 (320x240):
  *          +--------------------------------------------+
  *          | [WiFi 20x20] ON         Time: 12:34:56    |  顶部状态栏
  *          +-----------------------+--------------------+
  *          |        室内           |        室外         |
  *          |                       |                    |
  *          | [温度40x40] 25 [度25x25] | [温度40x40] 27 [度25] |
  *          | [湿度30x30] 56 [%26x26] | [湿度30x30] 65 [%26] |
  *          | City: Shanghai        | Feel 26 / Sunny    |
  *          | Lux: 100              |                    |
  *          | [声音30x30] Vol: 2048   | (天气图标占位)         |
  *          +-----------------------+--------------------+
  *
  * 数据来源:
  *   - 本地传感器: SensorTask 通过 xSensorDataQueue 发送 SensorData_t
  *   - 远程天气:   WeatherTask 通过 Weather_GetLatest() 获取 WeatherInfo_t
  *
  * 注意事项:
  *   1) Gui Guider 生成代码基于 480x320, setup_ui 函数中包含背景图,
  *      但实际使用 320x240 屏幕布局, 需要删除/禁用背景图引用.
  *   2) 480x320 背景图 (_Background00) 数据较大, ARM linker 会报错.
  ******************************************************************************
  */

#include "Display.h"
#include "WeatherTask.h"      /* WeatherInfo_t / Weather_GetLatest */
#include "SensorTask.h"
#include "Camera.h"
#include "display_mode.h"
#include "StateTask.h"        /* State_GetCurrent / State_PostEvent / State_GetSummary */
#include "FusionTask.h"       /* FusionResult_t, Fusion_GetLatest (调试面板推送) */
#include "LearningScreen.h"   /* 学习界面 (独立模块) */
#include "LearningReport.h"   /* 学习报告屏 (侘寂暖灰调, 替换旧 g_summary_screen) */
#include "lcd.h"              /* LCD_direction */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lvgl.h"

/* Gui Guider 生成的头文件 (包含图标 + 界面函数) */
#include "gui_guider.h"

#include <stdio.h>
#include <string.h>

/* ======================== 颜色定义 (Wabi-sabi warm gray palette) ======================== */
#define BG_TOP           lv_color_hex(0xE8E1D5)   /* warm cream (gradient top) */
#define BG_BOTTOM        lv_color_hex(0xCFC8B8)   /* deep warm gray (gradient bottom) */
#define BG_COLOR         BG_TOP                  /* legacy alias for solid bg fallbacks */
#define CARD_BG          lv_color_hex(0xFFFFFF)  /* pure white card */
#define SHADOW_COLOR     lv_color_hex(0x6E6759)  /* warm gray shadow (cards) */
#define TITLE_BG         lv_color_hex(0xE5DDCB)  /* warm cream title bar (was purple) */
#define TITLE_FG         lv_color_hex(0x1A1815)  /* near-black title text (was white) */
#define TEMP_COLOR       lv_color_hex(0xC54033)  /* temperature (deep muted red) */
#define HUMI_COLOR       lv_color_hex(0x3D6B92)  /* humidity (deep muted blue) */
#define SOUND_COLOR      lv_color_hex(0x7A5F3A)  /* sound indicator (sandalwood) */
#define TEXT_COLOR       lv_color_hex(0x1A1815)  /* primary text (near-black) */
#define DIM_COLOR        lv_color_hex(0x3A3631)  /* dim text (dark warm gray) */
#define ACCENT_GOLD      lv_color_hex(0x7A5F3A)  /* warm gold accent (state label) */
#define BTN_BG           lv_color_hex(0xFFFFFF)  /* button bg (white card-style) */
#define BTN_BG_END       lv_color_hex(0xB05A4A)  /* end button bg (muted clay red) */
#define BTN_BG_DISABLE   lv_color_hex(0xE5DDCB)  /* disabled button bg (warm cream) */

/* ======================== 字体定义 ======================== */
#define FONT_TITLE      &lv_font_SourceHanSerifSC_Regular_17  /* 中文标题 室内/室外 */
#define FONT_BIG        &lv_font_Alatsi_Regular_50            /* 大数字温度/湿度 */
#define FONT_MED        &lv_font_Alatsi_Regular_25            /* 单位符号 */
#define FONT_SMALL      &lv_font_montserrat_14                /* ASCII 小字 */
#define FONT_TIME       &lv_font_montserrat_18                /* 顶部时间显示 18px */
#define FONT_EN12       &lv_font_montserrat_12                /* ASCII 按钮/标签 12px */
#define FONT_EN14       &lv_font_montserrat_14                /* ASCII 正文 14px */
#define FONT_EN16       &lv_font_montserrat_16                /* ASCII 标题 16px */
#define FONT_CN12       &lv_font_SourceHanSerifSC_Regular_12  /* 中文小字 (备用) */
#define FONT_CN17       &lv_font_SourceHanSerifSC_Regular_17  /* 中文正文 (备用) */

/* ======================== 全局变量 ======================== */
/* Gui Guider widgets_init.c 中 extern 声明的 guider_ui, 当前未使用 */
lv_ui guider_ui;

/* 屏幕对象 (学习界面屏对象由 LearningScreen 模块管理) */
static lv_obj_t *g_weather_screen  = NULL;
static lv_obj_t *g_cam_screen      = NULL;
static lv_obj_t *g_summary_screen  = NULL;   /* SUMMARY 报告页 */
static lv_obj_t *g_toast           = NULL;   /* 顶部 Toast 提示 (跨屏幕显示) */
static lv_obj_t *g_toast_lbl       = NULL;

/* 界面控件指针 */
static lv_obj_t *label_wifi      = NULL;
static lv_obj_t *label_time      = NULL;      /* 顶部时间显示 */
static lv_obj_t *label_in_temp   = NULL;
static lv_obj_t *label_in_humi   = NULL;
static lv_obj_t *label_in_city   = NULL;      /* 城市名称 */
static lv_obj_t *label_in_lux    = NULL;
static lv_obj_t *label_in_volume = NULL;      /* 音量百分比显示 */
static lv_obj_t *arc_sound       = NULL;      /* 音量弧形指示器 */
static lv_obj_t *label_out_temp  = NULL;
static lv_obj_t *label_out_humi  = NULL;
static lv_obj_t *label_out_feel  = NULL;
static lv_obj_t *label_out_cond  = NULL;

/* 状态机控制条 (顶部) */
static lv_obj_t *label_state     = NULL;      /* 状态文字 (学习中/暂停中/已离开) */
static lv_obj_t *btn_state1      = NULL;      /* 主操作按钮 (开始/暂停/继续/离开中) */
static lv_obj_t *btn_state2      = NULL;      /* 结束按钮 (LEARNING/PAUSED/AWAY 显示) */
static lv_obj_t *btn_cam         = NULL;      /* 切换摄像头按钮 (IDLE 显示) */

/* SUMMARY 报告页控件 */
static lv_obj_t *lbl_sum_time     = NULL;
static lv_obj_t *lbl_sum_score    = NULL;
static lv_obj_t *lbl_sum_focus    = NULL;
static lv_obj_t *lbl_sum_distract = NULL;
static lv_obj_t *lbl_sum_fatigue  = NULL;
static lv_obj_t *lbl_sum_posture  = NULL;
static lv_obj_t *lbl_sum_away     = NULL;
static lv_obj_t *lbl_sum_pause    = NULL;
static lv_obj_t *lbl_sum_endmode  = NULL;

/* 远程数据接收时间戳, 用于判断 WiFi 是否在线
 *   (LearningScreen 也会读这个值, 通过 LearningScreen.h 中的 extern 访问)
 *   volatile: 跨 task 写入 (DisplayTask 设, Weather/Learning 读), 编译器不应优化 */
volatile uint32_t g_last_remote_tick = 0;

/* ======================== 时长选择器 (PICKING) ======================== */
#define PICKER_AUTO_CONFIRM_MS  5000    /* 5s 无操作自动确认 */
#define PICKER_SCREEN_W         320
#define PICKER_SCREEN_H         240
#define PICKER_OPTIONS_COUNT     10
static const uint16_t picker_options[PICKER_OPTIONS_COUNT] = {5, 10, 15, 20, 25, 30, 45, 60, 90, 120};

static lv_obj_t *g_picker_panel   = NULL;   /* 时长选择器面板 (学习界面浮层) */
static lv_obj_t *g_picker_roller  = NULL;   /* lv_roller 控件 */
static lv_obj_t *g_picker_btn     = NULL;   /* 确认按钮 (仅在用户操作后显示) */
static lv_obj_t *g_picker_hint    = NULL;   /* 倒计时提示文字 */
static lv_obj_t *g_picker_title   = NULL;   /* 标题 */
static uint8_t  g_picker_user_touched = 0;  /* 1 = 用户动了滚轮, 取消自动 */
static uint32_t g_picker_show_tick    = 0;  /* 浮层显示时刻 (用于 5s 倒计时) */
static uint8_t  g_picker_selected_min = STATE_DEFAULT_DURATION_MIN;

/* ======================== 内部函数声明 ======================== */
static void weather_switch_btn_event_cb(lv_event_t *e);
static void state_btn1_event_cb(lv_event_t *e);
static void state_btn2_event_cb(lv_event_t *e);
static void summary_back_btn_event_cb(lv_event_t *e);
static void learning_report_back_cb(void);   /* 学习报告屏"返回"按钮回调 */
static void picker_roller_event_cb(lv_event_t *e);
static void picker_btn_event_cb(lv_event_t *e);
static void Summary_UI_Build(void);
static void Summary_UI_Update(void);
static void ControlBar_Build(void);
static void ControlBar_Update(void);
static void Picker_Build(void);
static void Picker_Show(void);
static void Picker_Hide(void);
static void Picker_Tick(void);

/* ======================== 辅助函数 ======================== */
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    /* Wabi-sabi: white card with soft warm shadow, no border, large radius */
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_color(card, SHADOW_COLOR, 0);
    lv_obj_set_style_shadow_width(card, 14, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_shadow_ofs_y(card, 3, 0);
    lv_obj_set_style_shadow_spread(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt,
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

static lv_obj_t *make_card_title(lv_obj_t *card, const char *txt, int w)
{
    /* Wabi-sabi: warm cream title bar with dark text, no purple */
    lv_obj_t *t = make_label(card, txt, FONT_TITLE, TITLE_FG, 0, 0);
    lv_obj_set_size(t, w, 24);
    lv_obj_set_style_bg_color(t, TITLE_BG, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(t, 2, 0);
    /* Round only the top corners so it integrates with the card */
    lv_obj_set_style_radius(t, 0, 0);
    return t;
}

static lv_obj_t *make_icon(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
}

/* ======================== UI 构建 ======================== */
static void Weather_UI_Build(void)
{
    /* 创建主屏幕 */
    g_weather_screen = lv_obj_create(NULL);
    /* Wabi-sabi: warm cream gradient bg (top -> bottom) */
    lv_obj_set_style_bg_color(g_weather_screen, BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(g_weather_screen, BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(g_weather_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_stop(g_weather_screen, 255, 0);
    lv_obj_set_style_bg_opa(g_weather_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_weather_screen, 0, 0);
    lv_obj_clear_flag(g_weather_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* === 顶部状态栏 (320x30) === */
    /* WiFi 图标 (小图标) */
    make_icon(g_weather_screen, &_WIFI_alpha_20x20, 4, 5);
    /* WiFi 状态文字 */
    label_wifi = make_label(g_weather_screen, "--",
                             FONT_SMALL, TEXT_COLOR, 28, 8);

    /* 时间显示 */
    label_time = make_label(g_weather_screen, "Time: --",
                            FONT_TIME, TEXT_COLOR, 70, 5);

    /* 状态文字: 学习中 / 暂停中 / 已离开 (在 IDLE 状态为空) */
    /* Wabi-sabi: dark gold accent instead of purple */
    label_state = make_label(g_weather_screen, "",
                             FONT_EN12, ACCENT_GOLD, 140, 10);

    /* === 状态机控制条 (由 ControlBar_Build 进一步创建按钮) === */
    ControlBar_Build();

    /* === 室内卡片 (5,35) ~ (158,235), 153x200 === */
    lv_obj_t *card_in = make_card(g_weather_screen, 5, 35, 153, 200);
    make_card_title(card_in, "\xe5\xae\xa4\xe5\x86\x85", 149);  /* Indoor */

    /* 温度图标 */
    make_icon(card_in, &_temperature_alpha_40x40, 4, 26);
    label_in_temp = make_label(card_in, "--",
                                FONT_BIG, TEMP_COLOR, 50, 24);
    make_icon(card_in, &_Celsius_alpha_25x25, 118, 38);

    /* 湿度图标 */
    make_icon(card_in, &_humidity_alpha_30x30, 8, 82);
    label_in_humi = make_label(card_in, "--",
                                FONT_BIG, HUMI_COLOR, 50, 78);
    make_icon(card_in, &_percentage_alpha_26x26, 118, 90);

    /* 城市名称 */
    label_in_city = make_label(card_in, "City: --",
                                FONT_SMALL, TEXT_COLOR, 6, 135);

    /* 光照强度 */
    label_in_lux = make_label(card_in, "Lux: --",
                               FONT_SMALL, TEXT_COLOR, 6, 150);

    /* 音量弧形指示器 + 百分比显示 */
    {
        arc_sound = lv_arc_create(card_in);
        lv_obj_set_size(arc_sound, 44, 44);
        lv_obj_set_pos(arc_sound, 100, 148);
        lv_arc_set_rotation(arc_sound, 270);
        lv_arc_set_bg_angles(arc_sound, 0, 360);
        lv_arc_set_range(arc_sound, 0, 100);
        lv_arc_set_value(arc_sound, 0);
        lv_obj_remove_style(arc_sound, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(arc_sound, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_color(arc_sound, SOUND_COLOR, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc_sound, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_sound, lv_color_hex(0xDDDDDD), LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc_sound, 4, LV_PART_MAIN);

        label_in_volume = lv_label_create(card_in);
        lv_label_set_text(label_in_volume, "0%");
        lv_obj_set_style_text_font(label_in_volume, FONT_SMALL, 0);
        lv_obj_set_style_text_color(label_in_volume, SOUND_COLOR, 0);
        lv_obj_set_style_text_align(label_in_volume, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(label_in_volume, 108, 163);
    }

    /* === 室外卡片 (162,35) ~ (315,235), 153x200 === */
    lv_obj_t *card_out = make_card(g_weather_screen, 162, 35, 153, 200);
    make_card_title(card_out, "\xe5\xae\xa4\xe5\xa4\x96", 149);  /* Outdoor */

    /* 温度图标 */
    make_icon(card_out, &_temperature_alpha_40x40, 4, 26);
    label_out_temp = make_label(card_out, "--",
                                 FONT_BIG, TEMP_COLOR, 50, 24);
    make_icon(card_out, &_Celsius_alpha_25x25, 118, 38);

    /* 湿度图标 */
    make_icon(card_out, &_humidity_alpha_30x30, 8, 82);
    label_out_humi = make_label(card_out, "--",
                                 FONT_BIG, HUMI_COLOR, 50, 78);
    make_icon(card_out, &_percentage_alpha_26x26, 118, 90);

    /* 天气状况 + 体感温度 */
    label_out_cond = make_label(card_out, "----",
                                 FONT_SMALL, DIM_COLOR, 6, 135);
    label_out_feel = make_label(card_out, "Feel --",
                                 FONT_SMALL, DIM_COLOR, 6, 150);
}

/* ======================== UI 数据更新 ======================== */
static void Weather_UI_UpdateLocal(const SensorData_t *d)
{
    char buf[32];

    /* 更新温度 */
    int t = (int)(d->temperature / 100);
    snprintf(buf, sizeof(buf), "%d", t);
    if (label_in_temp) lv_label_set_text(label_in_temp, buf);

    /* 更新湿度 */
    snprintf(buf, sizeof(buf), "%u", (unsigned)(d->humidity / 100));
    if (label_in_humi) lv_label_set_text(label_in_humi, buf);

    /* 更新光照强度 */
    snprintf(buf, sizeof(buf), "Lux: %u", (unsigned)(d->lux_x100 / 100));
    if (label_in_lux) lv_label_set_text(label_in_lux, buf);

    /* 更新音量: soundIntensity 范围 0~100 映射到百分比 */
    {
        uint8_t pct = (uint8_t)d->soundIntensity;
        if (pct > 100) pct = 100;
        if (arc_sound)        lv_arc_set_value(arc_sound, pct);
        snprintf(buf, sizeof(buf), "%u%%", pct);
        if (label_in_volume)  lv_label_set_text(label_in_volume, buf);
    }
}

static void Weather_UI_UpdateRemote(const WeatherInfo_t *w)
{
    char buf[40];

    if (label_in_city) {
        snprintf(buf, sizeof(buf), "%s", w->city[0] ? w->city : "----");
        lv_label_set_text(label_in_city, buf);
    }

    if (label_time) {
        if (w->time[0] != '\0') {
            snprintf(buf, sizeof(buf), "%s", w->time);
        } else {
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)w->timestamp);
        }
        lv_label_set_text(label_time, buf);
    }

    /* 室外温度 */
    int t = w->temp_x10 / 10;
    snprintf(buf, sizeof(buf), "%d", t);
    if (label_out_temp) lv_label_set_text(label_out_temp, buf);

    /* 室外湿度 */
    snprintf(buf, sizeof(buf), "%u", (unsigned)w->humi);
    if (label_out_humi) lv_label_set_text(label_out_humi, buf);

    /* 天气状况 */
    if (label_out_cond) {
        lv_label_set_text(label_out_cond, w->cond[0] ? w->cond : "----");
    }

    /* 体感温度 */
    if (label_out_feel) {
        int f = w->feel_x10 / 10;
        snprintf(buf, sizeof(buf), "Feel %d C", f);
        lv_label_set_text(label_out_feel, buf);
    }
}

/* WiFi 状态刷新: 30秒内收到数据则显示 ON */
static void Weather_UI_RefreshWifi(void)
{
    if (label_wifi == NULL) return;
    if (g_last_remote_tick == 0) {
        lv_label_set_text(label_wifi, "--");
    } else if ((xTaskGetTickCount() - g_last_remote_tick) < pdMS_TO_TICKS(30000)) {
        lv_label_set_text(label_wifi, "ON");
    } else {
        lv_label_set_text(label_wifi, "OFF");
    }
}

/* ======================== 屏幕切换相关 ======================== */
static void weather_switch_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (g_display_mode != DISP_MODE_CAMERA) {
        DisplayMode_Request(DISP_MODE_CAMERA);
    }
}

/* ======================== 状态机按钮回调 ======================== */

/**
  * @brief  主操作按钮回调: 根据当前状态投递不同事件
  *         IDLE   -> GUI_START
  *         LEARNING -> GUI_PAUSE
  *         PAUSED -> GUI_RESUME
  *         AWAY   -> 无操作 (按钮已置灰)
  */
static void state_btn1_event_cb(lv_event_t *e)
{
    (void)e;
    LearningState_t s = State_GetCurrent();
    switch (s)
    {
    case ST_IDLE:      State_PostEvent(STATE_EVT_GUI_START, 0);  break;
    case ST_LEARNING:  State_PostEvent(STATE_EVT_GUI_PAUSE, 0);  break;
    case ST_PAUSED:    State_PostEvent(STATE_EVT_GUI_RESUME, 0); break;
    case ST_AWAY:      /* AWAY 状态不响应主按钮 */ break;
    default: break;
    }
}

/**
  * @brief  "End" 按钮回调: LEARNING/PAUSED/AWAY -> SUMMARY
  */
static void state_btn2_event_cb(lv_event_t *e)
{
    (void)e;
    State_PostEvent(STATE_EVT_GUI_END, 0);
}

/**
  * @brief  摄像头按钮回调 (IDLE 状态显示): 切到摄像头画面
  */
static void cam_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (g_display_mode != DISP_MODE_CAMERA) {
        DisplayMode_Request(DISP_MODE_CAMERA);
    }
}

/**
 * @brief  SUMMARY 页面 "Back" 按钮: 切回 IDLE
 */
static void summary_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    State_PostEvent(STATE_EVT_GUI_BACK, 0);
}

/**
 * @brief  Learning Report "Back" button callback (registered via
 *         LearningReport_SetBackCallback).  Posts STATE_EVT_GUI_BACK so
 *         the state machine returns to ST_IDLE and DisplayTask loads
 *         g_weather_screen.
 */
static void learning_report_back_cb(void)
{
    State_PostEvent(STATE_EVT_GUI_BACK, 0);
}

/* ======================== 状态机控制条 ======================== */

/**
  * @brief  创建顶部状态机控制条 (3 个按钮: state1/state2/cam)
  *         位置 (顶部 y=3):
  *           - state1: x=180, w=64  (主操作)
  *           - state2: x=246, w=64  (结束)
  *           - cam   : x=312, w=???  ← 已无空间, 改为 (180, 3) 单按钮
  *
  *         实际布局:
  *           - state1 (主): (180, 3) 64x24
  *           - state2 (结束): (248, 3) 68x24
  *           - cam (IDLE 显示): 同 state2 位置, 互斥
  */
static void ControlBar_Build(void)
{
    lv_obj_t *parent = g_weather_screen;
    if (parent == NULL) return;

    /* 主操作按钮 (Start / Pause / Resume) */
    /* Wabi-sabi: 白底 + 暖灰阴影 + 大圆角 + 深字, 无紫色 */
    btn_state1 = lv_btn_create(parent);
    lv_obj_set_size(btn_state1, 64, 24);
    lv_obj_set_pos(btn_state1, 180, 3);
    lv_obj_set_style_bg_color(btn_state1, BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn_state1, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_state1, 12, 0);
    lv_obj_set_style_border_width(btn_state1, 0, 0);
    lv_obj_set_style_shadow_color(btn_state1, SHADOW_COLOR, 0);
    lv_obj_set_style_shadow_width(btn_state1, 6, 0);
    lv_obj_set_style_shadow_opa(btn_state1, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(btn_state1, 1, 0);
    lv_obj_set_style_pad_all(btn_state1, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_state1);
        lv_label_set_text(lbl, "Start");
        lv_obj_set_style_text_color(lbl, TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lbl, FONT_EN12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_state1, state_btn1_event_cb, LV_EVENT_CLICKED, NULL);

    /* 结束按钮 (End) */
    /* Wabi-sabi: 沉静的陶土红, 白字, 大圆角, 微阴影 */
    btn_state2 = lv_btn_create(parent);
    lv_obj_set_size(btn_state2, 68, 24);
    lv_obj_set_pos(btn_state2, 248, 3);
    lv_obj_set_style_bg_color(btn_state2, BTN_BG_END, 0);
    lv_obj_set_style_bg_opa(btn_state2, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_state2, 12, 0);
    lv_obj_set_style_border_width(btn_state2, 0, 0);
    lv_obj_set_style_shadow_color(btn_state2, SHADOW_COLOR, 0);
    lv_obj_set_style_shadow_width(btn_state2, 6, 0);
    lv_obj_set_style_shadow_opa(btn_state2, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(btn_state2, 1, 0);
    lv_obj_set_style_pad_all(btn_state2, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_state2);
        lv_label_set_text(lbl, "End");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, FONT_EN12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_state2, state_btn2_event_cb, LV_EVENT_CLICKED, NULL);

    /* 摄像头按钮 (Camera, IDLE 状态显示) */
    /* Wabi-sabi: 与 state1 同样的白底样式 */
    btn_cam = lv_btn_create(parent);
    lv_obj_set_size(btn_cam, 68, 24);
    lv_obj_set_pos(btn_cam, 248, 3);
    lv_obj_set_style_bg_color(btn_cam, BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn_cam, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_cam, 12, 0);
    lv_obj_set_style_border_width(btn_cam, 0, 0);
    lv_obj_set_style_shadow_color(btn_cam, SHADOW_COLOR, 0);
    lv_obj_set_style_shadow_width(btn_cam, 6, 0);
    lv_obj_set_style_shadow_opa(btn_cam, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(btn_cam, 1, 0);
    lv_obj_set_style_pad_all(btn_cam, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_cam);
        lv_label_set_text(lbl, "Camera");
        lv_obj_set_style_text_color(lbl, TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lbl, FONT_EN12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_cam, cam_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* 初始状态 (IDLE): 显示摄像头按钮, 隐藏结束按钮 */
    lv_obj_add_flag(btn_state2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_cam,   LV_OBJ_FLAG_HIDDEN);
}

/**
  * @brief  根据当前状态更新控制条按钮的标签/可见性
  *         在 DisplayTask 主循环中, 状态切换时被调用
  */
static void ControlBar_Update(void)
{
    LearningState_t s = State_GetCurrent();

    /* 主操作按钮 label */
    lv_obj_t *lbl1 = lv_obj_get_child(btn_state1, 0);
    switch (s)
    {
    case ST_IDLE:
        lv_label_set_text(lbl1, "Start");
        lv_obj_clear_state(btn_state1, LV_STATE_DISABLED);
        break;
    case ST_PICKING:
        /* PICKING 状态: 主按钮置灰 (用户用 picker 滚轮选) */
        lv_label_set_text(lbl1, "Picking");
        lv_obj_add_state(btn_state1, LV_STATE_DISABLED);
        break;
    case ST_LEARNING:
        lv_label_set_text(lbl1, "Pause");
        lv_obj_clear_state(btn_state1, LV_STATE_DISABLED);
        break;
    case ST_PAUSED:
        lv_label_set_text(lbl1, "Resume");
        lv_obj_clear_state(btn_state1, LV_STATE_DISABLED);
        break;
    case ST_AWAY:
        lv_label_set_text(lbl1, "Away");
        lv_obj_add_state(btn_state1, LV_STATE_DISABLED);
        break;
    default:
        break;
    }

    /* 结束 / 摄像头按钮的互斥显示 */
    if (s == ST_IDLE)
    {
        lv_obj_add_flag(btn_state2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_cam,   LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(btn_state2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_cam,     LV_OBJ_FLAG_HIDDEN);
    }

    /* 状态文字 */
    if (label_state)
    {
        switch (s)
        {
        case ST_PICKING:   lv_label_set_text(label_state, "Picking"); break;
        case ST_LEARNING:  lv_label_set_text(label_state, "Learning"); break;
        case ST_PAUSED:    lv_label_set_text(label_state, "Paused"); break;
        case ST_AWAY:      lv_label_set_text(label_state, "Away"); break;
        case ST_SUMMARY:   lv_label_set_text(label_state, "Done"); break;
        case ST_IDLE:
        default:           lv_label_set_text(label_state, "");       break;
        }
    }
}

static void Weather_ApplyMode(DisplayMode_t mode)
{
    /* 切换时强制设置屏幕方向 (180度旋转) */
    LCD_direction(3);

    if (mode == DISP_MODE_CAMERA) {
        if (g_cam_screen) lv_scr_load(g_cam_screen);
    } else { /* DISP_MODE_WEATHER */
        if (g_weather_screen) lv_scr_load(g_weather_screen);
    }
    lv_obj_invalidate(lv_scr_act());
    g_display_mode = mode;
}

/* ======================== SUMMARY 报告页 ======================== */

/**
  * @brief  构建 SUMMARY 报告页 (只构建一次)
  *         320x240 布局:
  *           y=0..32  : title "Learning Report" (purple background)
  *           y=40..200: 各项数据
  *           y=205..235: "Back" 按钮
  */
static void Summary_UI_Build(void)
{
    g_summary_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_summary_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(g_summary_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_summary_screen, 0, 0);
    lv_obj_clear_flag(g_summary_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题栏 */
    lv_obj_t *title_bg = lv_obj_create(g_summary_screen);
    lv_obj_set_pos(title_bg, 0, 0);
    lv_obj_set_size(title_bg, 320, 32);
    lv_obj_set_style_bg_color(title_bg, TITLE_BG, 0);
    lv_obj_set_style_bg_opa(title_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_bg, 0, 0);
    lv_obj_set_style_radius(title_bg, 0, 0);
    lv_obj_set_style_pad_all(title_bg, 0, 0);
    lv_obj_clear_flag(title_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_bg);
    lv_label_set_text(title, "Learning Report");
    lv_obj_set_style_text_color(title, TITLE_FG, 0);
    lv_obj_set_style_text_font(title, FONT_EN16, 0);
    lv_obj_center(title);

    /* 数据行 (label + value), 统一 18px 行高 */
    int y = 44;
    lbl_sum_time    = make_label(g_summary_screen, "Duration: --",       FONT_EN12, TEXT_COLOR, 10, y);  y += 18;
    lbl_sum_score   = make_label(g_summary_screen, "Avg Score: --",     FONT_EN12, TEXT_COLOR, 10, y);  y += 18;

    /* 状态占比 */
    lbl_sum_focus    = make_label(g_summary_screen, "Focus: --%",   FONT_EN12, lv_color_hex(0x43A047), 10, y); y += 18;
    lbl_sum_distract = make_label(g_summary_screen, "Distract: --%",   FONT_EN12, lv_color_hex(0xFB8C00), 10, y); y += 18;
    lbl_sum_fatigue  = make_label(g_summary_screen, "Fatigue: --%",   FONT_EN12, lv_color_hex(0xE53935), 10, y); y += 22;

    lbl_sum_posture = make_label(g_summary_screen, "Posture: --", FONT_EN12, TEXT_COLOR, 10, y);  y += 18;
    lbl_sum_away    = make_label(g_summary_screen, "Away Count: --", FONT_EN12, TEXT_COLOR, 10, y);  y += 18;
    lbl_sum_pause   = make_label(g_summary_screen, "Pause Count: --", FONT_EN12, TEXT_COLOR, 10, y);  y += 18;
    lbl_sum_endmode = make_label(g_summary_screen, "End Mode: --",    FONT_EN12, TEXT_COLOR, 10, y);

    /* 返回主页按钮 (底部居中) */
    lv_obj_t *btn_back = lv_btn_create(g_summary_screen);
    lv_obj_set_size(btn_back, 140, 30);
    lv_obj_set_pos(btn_back, 90, 212);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x6667FF), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x4040CC), 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_set_style_pad_all(btn_back, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_back);
        lv_label_set_text(lbl, "Back");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, FONT_EN14, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_back, summary_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

/**
  * @brief  从 StateTask 读取 SUMMARY 数据, 刷新页面
  *         在切换到 ST_SUMMARY 时调用一次
  */
static void Summary_UI_Update(void)
{
    SessionSummary_t s;
    if (State_GetSummary(&s) != 0) return;

    char buf[48];

    /* 时长: 秒 -> mm:ss (实际 / 目标) */
    uint32_t act_mm = s.total_seconds / 60;
    uint32_t act_ss = s.total_seconds % 60;
    uint32_t tgt_mm = s.target_seconds / 60;
    uint32_t tgt_ss = s.target_seconds % 60;
    if (s.target_seconds > 0) {
        snprintf(buf, sizeof(buf), "Duration: %02lu:%02lu / %02lu:%02lu",
                 (unsigned long)act_mm, (unsigned long)act_ss,
                 (unsigned long)tgt_mm, (unsigned long)tgt_ss);
    } else {
        snprintf(buf, sizeof(buf), "Duration: %02lu:%02lu",
                 (unsigned long)act_mm, (unsigned long)act_ss);
    }
    if (lbl_sum_time) lv_label_set_text(lbl_sum_time, buf);

    snprintf(buf, sizeof(buf), "Avg Score: %u", (unsigned)s.avg_total_score);
    if (lbl_sum_score) lv_label_set_text(lbl_sum_score, buf);

    snprintf(buf, sizeof(buf), "Focus: %u%%", (unsigned)s.focus_pct);
    if (lbl_sum_focus) lv_label_set_text(lbl_sum_focus, buf);

    snprintf(buf, sizeof(buf), "Distract: %u%%", (unsigned)s.distract_pct);
    if (lbl_sum_distract) lv_label_set_text(lbl_sum_distract, buf);

    snprintf(buf, sizeof(buf), "Fatigue: %u%%", (unsigned)s.fatigue_pct);
    if (lbl_sum_fatigue) lv_label_set_text(lbl_sum_fatigue, buf);

    snprintf(buf, sizeof(buf), "Posture: %u", (unsigned)s.posture_abnormal_count);
    if (lbl_sum_posture) lv_label_set_text(lbl_sum_posture, buf);

    snprintf(buf, sizeof(buf), "Away Count: %lu", (unsigned long)s.away_count);
    if (lbl_sum_away) lv_label_set_text(lbl_sum_away, buf);

    snprintf(buf, sizeof(buf), "Pause Count: %lu", (unsigned long)s.pause_count);
    if (lbl_sum_pause) lv_label_set_text(lbl_sum_pause, buf);

    /* 结束方式: 0=手动, 1=5min AWAY, 2=时长达成 */
    const char *endmode_str = "Manual";
    switch (s.auto_ended) {
    case 1:  endmode_str = "Auto (5min away)";   break;
    case 2:  endmode_str = "Time Complete!";     break;
    default: endmode_str = "Manual";             break;
    }
    snprintf(buf, sizeof(buf), "End Mode: %s", endmode_str);
    if (lbl_sum_endmode) lv_label_set_text(lbl_sum_endmode, buf);
}

/* ======================== 跨屏 Toast 提示 ======================== */
/**
  * 创建一个悬浮在所有屏幕之上的提示条 (lv_layer_top)
  * 默认隐藏, 通过 Toast_Show() 显示一段时间
  *   位置: y=30, 居中横置
  *   颜色: 浅黄底 + 深字 (警告) / 浅红底 + 白字 (错误)
  */
static void Toast_Build(void)
{
    g_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_toast, 300, 26);
    lv_obj_set_pos(g_toast, 10, 32);
    lv_obj_set_style_bg_color(g_toast, lv_color_hex(0xFFE082), 0);  /* 浅黄 */
    lv_obj_set_style_bg_opa(g_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(g_toast, 4, 0);
    lv_obj_set_style_border_color(g_toast, lv_color_hex(0xFFA000), 0);
    lv_obj_set_style_border_width(g_toast, 1, 0);
    lv_obj_set_style_shadow_width(g_toast, 6, 0);
    lv_obj_set_style_shadow_color(g_toast, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_toast, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(g_toast, 0, 0);
    lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_SCROLLABLE);

    g_toast_lbl = lv_label_create(g_toast);
    lv_obj_set_style_text_color(g_toast_lbl, lv_color_hex(0x5D4037), 0);
    lv_obj_set_style_text_font(g_toast_lbl, FONT_EN12, 0);
    lv_obj_center(g_toast_lbl);

    /* 默认隐藏 */
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
}

/* 主循环里每 100ms 调用一次: 同步 StateTask 的消息 */
static void Toast_Refresh(void)
{
    if (!g_toast) return;
    uint32_t age = 0;
    const char *msg = State_GetMessage(&age);
    if (msg) {
        /* 显示中 */
        if (lv_obj_has_flag(g_toast, LV_OBJ_FLAG_HIDDEN)) {
            lv_label_set_text(g_toast_lbl, msg);
            lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        /* 无消息 -> 隐藏 */
        if (!lv_obj_has_flag(g_toast, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ======================== 时长选择器 (PICKING 状态浮层) ======================== */

/**
  * @brief  滚轮事件回调: 用户动了滚轮 → 取消 5s 自动确认
  */
static void picker_roller_event_cb(lv_event_t *e)
{
    (void)e;
    g_picker_user_touched = 1;
    if (g_picker_roller) {
        uint16_t idx = lv_roller_get_selected(g_picker_roller);
        if (idx < PICKER_OPTIONS_COUNT) {
            g_picker_selected_min = (uint8_t)picker_options[idx];
        }
    }
    if (g_picker_hint) {
        lv_label_set_text(g_picker_hint, "Tap Start to begin");
    }
}

/**
  * @brief  Start 按钮回调: 确认时长, 发事件
  */
static void picker_btn_event_cb(lv_event_t *e)
{
    (void)e;
    State_PostEventParam(STATE_EVT_GUI_CONFIRM_DURATION, g_picker_selected_min);
    Picker_Hide();
}

/**
  * @brief  构建选择器面板 (一次性, 初始隐藏)
  *         父子关系: panel -> (title, roller, hint, btn)
  */
static void Picker_Build(void)
{
    if (g_picker_panel != NULL) return;

    /* 全屏浮层 (320x240) - 覆盖整个学习界面 */
    g_picker_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_picker_panel, PICKER_SCREEN_W, PICKER_SCREEN_H);
    lv_obj_set_pos(g_picker_panel, 0, 0);
    lv_obj_set_style_bg_color(g_picker_panel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(g_picker_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_picker_panel, 0, 0);
    lv_obj_set_style_border_width(g_picker_panel, 0, 0);
    lv_obj_set_style_pad_all(g_picker_panel, 0, 0);
    lv_obj_clear_flag(g_picker_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_picker_panel, LV_OBJ_FLAG_HIDDEN);

    /* 标题 */
    g_picker_title = lv_label_create(g_picker_panel);
    lv_label_set_text(g_picker_title, "Select Duration");
    lv_obj_set_style_text_color(g_picker_title, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(g_picker_title, FONT_EN16, 0);
    lv_obj_align(g_picker_title, LV_ALIGN_TOP_MID, 0, 14);

    /* 提示文字 (倒计时 / 提示点 Start) - 标题正下方 */
    g_picker_hint = lv_label_create(g_picker_panel);
    lv_label_set_text(g_picker_hint, "Auto-start in 5s");
    lv_obj_set_style_text_color(g_picker_hint, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(g_picker_hint, FONT_EN12, 0);
    lv_obj_align(g_picker_hint, LV_ALIGN_TOP_MID, 0, 38);

    /* lv_roller: 大尺寸, 居中, 占满大部分屏幕高度, 易滑 */
    /*   位置: x=20, w=280 (横向 87% 屏幕, 大幅提升左右滑动判断区域)
     *         y=70, h=130 (纵向 54% 屏幕, 占中央可视区)
     *   可见行 5 行, 中心选中行高亮, 上下各有 2 行, 滑动容错大 */
    g_picker_roller = lv_roller_create(g_picker_panel);
    char opts[PICKER_OPTIONS_COUNT * 12 + 1];
    char *p = opts;
    for (int i = 0; i < PICKER_OPTIONS_COUNT; i++) {
        const char *sep = (i < PICKER_OPTIONS_COUNT - 1) ? "\n" : "";
        p += snprintf(p, sizeof(opts) - (p - opts), "%u min%s",
                      (unsigned)picker_options[i], sep);
    }
    lv_roller_set_options(g_picker_roller, opts, LV_ROLLER_MODE_NORMAL);
    /* 默认选中 30 min (索引 5) */
    for (int i = 0; i < PICKER_OPTIONS_COUNT; i++) {
        if (picker_options[i] == STATE_DEFAULT_DURATION_MIN) {
            lv_roller_set_selected(g_picker_roller, i, LV_ANIM_OFF);
            break;
        }
    }
    lv_obj_set_size(g_picker_roller, 280, 130);
    lv_obj_align(g_picker_roller, LV_ALIGN_TOP_MID, 0, 70);
    /* 5 行可见: 选中行在第 3 行 (居中), 上下各 2 行, 滑动时视野更宽 */
    lv_roller_set_visible_row_count(g_picker_roller, 5);
    /* 加大行高, 让中心选中行更突出, 上下箭头区更大 */
    lv_obj_set_style_bg_color(g_picker_roller, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(g_picker_roller, lv_color_hex(0x1E40AF), 0);
    lv_obj_set_style_text_font(g_picker_roller, FONT_EN16, 0);
    lv_obj_set_style_border_color(g_picker_roller, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_border_width(g_picker_roller, 2, 0);
    lv_obj_set_style_radius(g_picker_roller, 10, 0);
    lv_obj_add_event_cb(g_picker_roller, picker_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 确认按钮 (开始) - 底部居中, 初始隐藏, 用户动滚轮后才显示 */
    g_picker_btn = lv_btn_create(g_picker_panel);
    lv_obj_set_size(g_picker_btn, 220, 36);
    lv_obj_align(g_picker_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(g_picker_btn, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_bg_opa(g_picker_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_picker_btn, 8, 0);
    lv_obj_set_style_shadow_width(g_picker_btn, 4, 0);
    lv_obj_set_style_shadow_color(g_picker_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_picker_btn, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(g_picker_btn, 2, 0);
    lv_obj_add_flag(g_picker_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *btn_lbl = lv_label_create(g_picker_btn);
    lv_label_set_text(btn_lbl, "Start");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_lbl, FONT_EN14, 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(g_picker_btn, picker_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

/**
  * @brief  显示选择器, 重置 5s 倒计时
  */
static void Picker_Show(void)
{
    if (g_picker_panel == NULL) Picker_Build();

    g_picker_user_touched = 0;
    g_picker_selected_min = STATE_DEFAULT_DURATION_MIN;
    g_picker_show_tick    = xTaskGetTickCount();

    /* 重置滚轮到默认 30 min */
    for (int i = 0; i < PICKER_OPTIONS_COUNT; i++) {
        if (picker_options[i] == STATE_DEFAULT_DURATION_MIN) {
            lv_roller_set_selected(g_picker_roller, i, LV_ANIM_OFF);
            break;
        }
    }

    /* 提示文字 + 按钮初始状态 */
    if (g_picker_hint) {
        lv_label_set_text(g_picker_hint, "Auto-start in 5s");
    }
    if (g_picker_btn) {
        lv_obj_add_flag(g_picker_btn, LV_OBJ_FLAG_HIDDEN);
    }

    /* 显示浮层 */
    lv_obj_clear_flag(g_picker_panel, LV_OBJ_FLAG_HIDDEN);
}

/**
  * @brief  隐藏选择器
  */
static void Picker_Hide(void)
{
    if (g_picker_panel == NULL) return;
    lv_obj_add_flag(g_picker_panel, LV_OBJ_FLAG_HIDDEN);
    g_picker_user_touched = 0;
}

/**
  * @brief  选择器主循环回调: 每帧调用, 处理 5s 倒计时
  *         当 PICKING 状态持续 5s 无用户操作时, 自动确认默认时长
  */
static void Picker_Tick(void)
{
    if (g_picker_panel == NULL) return;
    if (lv_obj_has_flag(g_picker_panel, LV_OBJ_FLAG_HIDDEN)) return;

    LearningState_t s = State_GetCurrent();

    /* 状态机不再是 PICKING, 自动隐藏 */
    if (s != ST_PICKING) {
        Picker_Hide();
        return;
    }

    if (g_picker_user_touched) {
        /* 用户已动滚轮, 显示按钮, 停止倒计时 */
        if (g_picker_btn && lv_obj_has_flag(g_picker_btn, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(g_picker_btn, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    /* 5s 倒计时 */
    uint32_t elapsed_ms = (uint32_t)((xTaskGetTickCount() - g_picker_show_tick) * 1000U
                                     / configTICK_RATE_HZ);
    if (elapsed_ms >= PICKER_AUTO_CONFIRM_MS) {
        /* 自动确认默认时长 */
        State_PostEventParam(STATE_EVT_GUI_CONFIRM_DURATION,
                             g_picker_selected_min);
        Picker_Hide();
        return;
    }

    /* 更新提示文字 (剩余秒数) */
    if (g_picker_hint) {
        uint32_t remain_ms = PICKER_AUTO_CONFIRM_MS - elapsed_ms;
        uint32_t remain_s  = (remain_ms + 999) / 1000;  /* 向上取整 */
        if (remain_s == 0) remain_s = 1;
        char buf[32];
        snprintf(buf, sizeof(buf), "Auto-start in %lus", (unsigned long)remain_s);
        lv_label_set_text(g_picker_hint, buf);
    }
}

/* ======================== 主任务函数 ======================== */
void DisplayTask(void *pvParameters)
{
    SensorData_t  sd;
    WeatherInfo_t wi;
    uint32_t last_sd_ts = 0;
    uint32_t last_wi_ts = 0;
    DisplayMode_t pending;
    LearningState_t last_state = (LearningState_t)-1;  /* 强制首次刷新 */

    (void)pvParameters;

    /* 等待 Camera_task 创建 canvas 对象 */
    for (int i = 0; i < 30 && g_cam_canvas == NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    g_cam_screen = (g_cam_canvas != NULL) ?
                    lv_obj_get_screen(g_cam_canvas) : lv_scr_act();

    /* 构建天气界面 (含状态机控制条) */
    Weather_UI_Build();

    /* 构建学习界面 (秒表+室内数据) */
    LearningScreen_Init();

    /* 构建 SUMMARY 报告页 (旧的紫色标题页, 暂保留代码但不再被加载) */
    Summary_UI_Build();

    /* 构建学习报告屏 (侘寂暖灰调, 替换旧 SUMMARY 屏, 真正显示给用户的版本) */
    LearningReport_Init();
    LearningReport_SetBackCallback(learning_report_back_cb);

    /* 构建跨屏 Toast (在所有屏幕之上) */
    Toast_Build();

    /* 默认显示天气界面 */
    Weather_ApplyMode(DISP_MODE_WEATHER);

    /* 首次同步控制条 (IDLE 状态) */
    ControlBar_Update();
    last_state = State_GetCurrent();

    for (;;)
    {
        /* 屏保 tick (在 IDLE / SUMMARY 状态 3min 无触摸 → 关背光) */
        ScreenSaver_Tick();

        /* 处理待处理的显示模式切换请求 */
        if (DisplayMode_TakePending(&pending)) {
            Weather_ApplyMode(pending);
        }

        /* 更新本地传感器数据 */
        if (xSensorDataQueue != NULL &&
            xQueuePeek(xSensorDataQueue, &sd, 0) == pdTRUE)
        {
            if (sd.timestamp != last_sd_ts) {
                last_sd_ts = sd.timestamp;
                Weather_UI_UpdateLocal(&sd);
            }
        }

        /* 更新远程天气数据 (通过 WeatherTask 提供的 API, 非阻塞读取) */
        if (Weather_GetLatest(&wi) == 0)
        {
            if (wi.timestamp != last_wi_ts) {
                last_wi_ts = wi.timestamp;
                g_last_remote_tick = xTaskGetTickCount();
                Weather_UI_UpdateRemote(&wi);
            }
        }

        Weather_UI_RefreshWifi();

        /* ----- Toast 提示同步 (跨屏显示) ----- */
        Toast_Refresh();

        /* ----- 状态机 UI 同步 ----- */
        LearningState_t cur_state = State_GetCurrent();
        if (cur_state != last_state)
        {
            if (cur_state == ST_SUMMARY)
            {
                /* Enter SUMMARY: read session data, push to Learning Report, switch screen */
                SessionSummary_t sm;
                LRSessionData_t  d;
                memset(&sm, 0, sizeof(sm));
                memset(&d,  0, sizeof(d));
                if (State_GetSummary(&sm) == 0) {
                    d.total_seconds          = sm.total_seconds;
                    d.target_seconds         = sm.target_seconds;
                    d.avg_total_score        = sm.avg_total_score;
                    d.focus_pct              = sm.focus_pct;
                    d.distract_pct           = sm.distract_pct;
                    d.fatigue_pct            = sm.fatigue_pct;
                    d.posture_abnormal_count = sm.posture_abnormal_count;
                    d.away_count             = sm.away_count;
                    d.pause_count            = sm.pause_count;
                    d.auto_ended             = sm.auto_ended;
                }

                /* Refresh the legacy SUMMARY screen data (code kept, not displayed) */
                Summary_UI_Update();

                /* Push real data to the Learning Report screen, then switch to it */
                LearningReport_Update(&d);
                LearningReport_RefreshAnim();
                Picker_Hide();
                if (LearningReport_GetScreen()) {
                    LCD_direction(3);
                    lv_scr_load(LearningReport_GetScreen());
                }
            }
            else if (cur_state == ST_PICKING)
            {
                /* 进入 PICKING: 切到学习界面 + 显示时长选择器 */
                LearningScreen_UpdateStatus();
                LearningScreen_UpdateStopwatch();
                if (LearningScreen_GetScreen()) {
                    LCD_direction(3);
                    lv_scr_load(LearningScreen_GetScreen());
                }
                Picker_Show();
            }
            else if (cur_state == ST_LEARNING ||
                     cur_state == ST_PAUSED   ||
                     cur_state == ST_AWAY)
            {
                /* 进入 LEARNING/PAUSED/AWAY: 切到学习界面 (隐藏 Picker) */
                LearningScreen_UpdateStatus();
                LearningScreen_UpdateStopwatch();
                Picker_Hide();
                if (LearningScreen_GetScreen()) {
                    LCD_direction(3);
                    lv_scr_load(LearningScreen_GetScreen());
                }
            }
            else if (cur_state == ST_IDLE)
            {
                /* 回到 IDLE: 切到主仪表板 */
                Picker_Hide();
                if (g_weather_screen) {
                    LCD_direction(3);
                    lv_scr_load(g_weather_screen);
                }
            }

            /* 状态变化时刷新控制条 */
            ControlBar_Update();
            last_state = cur_state;
        }

        /* ----- Picker 主循环: 5s 自动确认 ----- */
        Picker_Tick();

        /* ----- 学习界面实时刷新 (1Hz 秒表 + 传感器) ----- */
        if (cur_state == ST_LEARNING || cur_state == ST_PAUSED || cur_state == ST_AWAY)
        {
            LearningScreen_UpdateStopwatch();
            LearningScreen_RefreshBar();
            if (xSensorDataQueue != NULL &&
                xQueuePeek(xSensorDataQueue, &sd, 0) == pdTRUE)
            {
                /* 不再判 timestamp (上面已判过), 直接更新学习屏 */
                LearningScreen_UpdateSensor(&sd);
            }

            /* 推送 AI 融合结果到学习屏调试面板
             * (面板默认隐藏, 但数据仍可被读取 / 推入, 用户点 DBG 即看到) */
            {
                FusionResult_t fr;
                if (Fusion_GetLatest(&fr) == 0) {
                    LearningScreen_UpdateFusion(&fr);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}