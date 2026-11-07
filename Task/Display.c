/**
  ******************************************************************************
  * @file    Display.c
  * @brief   320x240 主界面 (时钟 + 室内温湿度) + 状态机控制
  *
  *          屏幕布局 (320x240):
  *          +--------------------------------------------+
  *          | [WiFi 20x20] ON  [state]   [Start][End/Cam] |  顶部状态栏 y=0..30
  *          +--------------------------------------------+
  *          |                                            |
  *          |               12:34:56                     |  中央大字时钟 y=60..130
  *          |              (48pt, RTC 驱动)               |    (未对时显示 --:--:--)
  *          |                                            |
  *          +--------------------------------------------+
  *          |    Temp 25 C            Humi 56 %          |  底部小字 y=185..210
  *          +--------------------------------------------+
  *
  * 数据来源:
  *   - 本地时间: STM32 内置 RTC (WeatherTask 收到网络时间后通过 RTC_SyncFromNetwork 对时)
  *   - 本地传感器: SensorTask 通过 xSensorDataQueue 发送 SensorData_t
  *   - 远程天气:   WeatherTask 通过 Weather_GetLatest() (用于判断 WiFi 在线 + 对时, 室外数据由待建的天气子界面消费)
  ******************************************************************************
  */

#include "Display.h"
#include "WeatherTask.h"      /* WeatherInfo_t / Weather_GetLatest */
#include "SensorTask.h"
#include "Camera.h"
#include "display_mode.h"
#include "rtc.h"              /* RTC_GetTime — 主界面大字时钟数据源 */
#include "StateTask.h"        /* State_GetCurrent / State_PostEvent / State_GetSummary */
#include "FusionTask.h"       /* FusionResult_t, Fusion_GetLatest (调试面板推送) */
#include "LearningScreen.h"   /* 学习界面 (独立模块) */
#include "WeatherScreen.h"   /* 天气预报子界面 (网络天气详情页) */
#include "LearningReport.h"   /* 学习报告屏 (侘寂暖灰调, ST_SUMMARY 状态显示给用户) */
#include "UART_ESP01.h"       /* UART3 -> ESP-01: 学习报告数据上传 (MQTT) */
#include "lcd.h"              /* LCD_direction */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

/* WiFi 图标 (来自原 GUI Guider generated/images, 仅保留此一个) */
LV_IMG_DECLARE(_WIFI_alpha_20x20);

/* ======================== 颜色定义 ======================== */
#define BG_COLOR        lv_color_hex(0xE6EBEF)
#define TEXT_COLOR      lv_color_hex(0x222222)

/* ======================== 字体定义 ======================== */
#define FONT_SMALL      &lv_font_montserrat_14                /* ASCII 小字 */
#define FONT_CLOCK      &lv_font_montserrat_48                /* 主界面中央大字时钟 (RTC 驱动) */
#define FONT_EN12       &lv_font_montserrat_12                /* ASCII 按钮/标签 12px */
#define FONT_EN14       &lv_font_montserrat_14                /* ASCII 正文 14px */
#define FONT_EN16       &lv_font_montserrat_16                /* ASCII 标题 16px */

/* ======================== 全局变量 ======================== */
/* 屏幕对象 (学习界面屏对象由 LearningScreen 模块管理) */
static lv_obj_t *g_weather_screen  = NULL;
static lv_obj_t *g_cam_screen      = NULL;
static lv_obj_t *g_toast           = NULL;   /* 顶部 Toast 提示 (跨屏幕显示) */
static lv_obj_t *g_toast_lbl       = NULL;

/* 界面控件指针 (主界面: 时钟 + 底部三按钮 + 中部环境数据) */
static lv_obj_t *label_wifi      = NULL;
static lv_obj_t *label_clock_big = NULL;      /* 中央大字时钟 HH:MM:SS (RTC 驱动) */
static lv_obj_t *label_in_temp   = NULL;      /* 中部室内温度 */
static lv_obj_t *label_in_humi   = NULL;      /* 中部室内湿度 */
static lv_obj_t *label_in_lux    = NULL;      /* 中部室内光照 */

/* 底部三按钮 (主界面 IDLE 状态入口: Weather / Start / Camera) */
static lv_obj_t *btn_weather     = NULL;      /* 切到天气预报子界面 */
static lv_obj_t *btn_start       = NULL;      /* 启动学习会话 (投递 STATE_EVT_GUI_START) */
static lv_obj_t *btn_cam         = NULL;      /* 切到摄像头画面 */

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
static void weather_btn_event_cb(lv_event_t *e);
static void state_btn1_event_cb(lv_event_t *e);
static void state_btn2_event_cb(lv_event_t *e);
static void learning_report_back_cb(void);   /* 学习报告屏"返回"按钮回调 */
static void weather_forecast_back_cb(void);  /* 天气预报屏"返回"按钮回调 */
static void picker_roller_event_cb(lv_event_t *e);
static void picker_btn_event_cb(lv_event_t *e);
static void ControlBar_Build(void);
static void ControlBar_Update(void);
static void Picker_Build(void);
static void Picker_Show(void);
static void Picker_Hide(void);
static void Picker_Tick(void);

/* ======================== 辅助函数 ======================== */
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

static lv_obj_t *make_icon(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
}

/* ======================== UI 构建 ======================== */
/**
 * @brief  320x240 主界面布局 (仅 IDLE 状态显示):
 *          +--------------------------------------------+
 *          | [WiFi] ON                              |   y=4..24  顶部状态栏 (简化, 仅WiFi)
 *          +--------------------------------------------+
 *          |                                            |
 *          |               12:34:56                     |   y=40..120 中央 48pt 时钟
 *          |                                            |
 *          +--------------------------------------------+
 *          |   Temp 25 C    Humi 56 %    Light 420 lx   |   y=145..170 中部环境数据 (3列均匀)
 *          +--------------------------------------------+
 *          |   [Weather]    [Start]    [Camera]         |   y=190..220 底部三按钮 (IDLE 入口)
 *          +--------------------------------------------+
 */
static void Weather_UI_Build(void)
{
    /* 创建主屏幕 */
    g_weather_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_weather_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(g_weather_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_weather_screen, 0, 0);
    lv_obj_clear_flag(g_weather_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* === 顶部状态栏 (仅 WiFi 图标 + 状态文字) === */
    make_icon(g_weather_screen, &_WIFI_alpha_20x20, 4, 4);
    label_wifi = make_label(g_weather_screen, "--",
                             FONT_SMALL, TEXT_COLOR, 28, 7);

    /* === 中央大字时钟 HH:MM:SS (RTC 驱动) ===
     * 48pt 字体高度约 56px, 屏幕 320x240;
     * 顶部状态栏 30px + 环境行 25px + 底部按钮 30px = 85px 边距,
     * 中部可用 30..210 (180px); 居中 y ≈ 100 */
    label_clock_big = lv_label_create(g_weather_screen);
    lv_label_set_text(label_clock_big, "--:--:--");
    lv_obj_set_style_text_font(label_clock_big, FONT_CLOCK, 0);
    lv_obj_set_style_text_color(label_clock_big, TEXT_COLOR, 0);
    lv_obj_align(label_clock_big, LV_ALIGN_CENTER, 0, 0);

    /* === 中部环境数据: 三列均匀分布 (温度/湿度/光照) === */
    label_in_temp = make_label(g_weather_screen, "Temp -- C",
                                FONT_SMALL, TEXT_COLOR, 30, 152);
    lv_obj_set_style_text_align(label_in_temp, LV_TEXT_ALIGN_CENTER, 0);

    label_in_humi = make_label(g_weather_screen, "Humi -- %",
                                FONT_SMALL, TEXT_COLOR, 130, 152);
    lv_obj_set_style_text_align(label_in_humi, LV_TEXT_ALIGN_CENTER, 0);

    label_in_lux  = make_label(g_weather_screen, "Light -- lx",
                                FONT_SMALL, TEXT_COLOR, 220, 152);
    lv_obj_set_style_text_align(label_in_lux, LV_TEXT_ALIGN_CENTER, 0);
}

/* ======================== UI 数据更新 ======================== */
static void Weather_UI_UpdateLocal(const SensorData_t *d)
{
    char buf[32];

    /* 中部室内温度 */
    int t = (int)(d->temperature / 100);
    snprintf(buf, sizeof(buf), "Temp %d C", t);
    if (label_in_temp) lv_label_set_text(label_in_temp, buf);

    /* 中部室内湿度 */
    snprintf(buf, sizeof(buf), "Humi %u %%", (unsigned)(d->humidity / 100));
    if (label_in_humi) lv_label_set_text(label_in_humi, buf);

    /* 中部室内光照 (lux_x100 单位 0.01 lx, 显示为整数 lx) */
    uint32_t lux_int = d->lux_x100 / 100;
    if (lux_int > 9999) lux_int = 9999;  /* 防溢出 */
    snprintf(buf, sizeof(buf), "Light %u lx", (unsigned)lux_int);
    if (label_in_lux) lv_label_set_text(label_in_lux, buf);
}

/* 主界面大字时钟: 由 RTC 驱动, 每秒刷新一次 (调用方为主循环)
 * - 未对时 (rtc_synced=0): 显示 "--:--:--"
 * - 已对时: 显示 "HH:MM:SS"
 * 用秒级缓存避免每 100ms 重复 snprintf + set_text */
static void Weather_UI_UpdateClock(void)
{
    static uint8_t last_ss = 0xFF;
    uint8_t hh, mm, ss;
    char buf[12];

    if (label_clock_big == NULL) return;

    if (RTC_GetTime(&hh, &mm, &ss) != 0) {
        /* RTC 未对时 */
        if (last_ss != 0xFF) {
            lv_label_set_text(label_clock_big, "--:--:--");
            last_ss = 0xFF;
        }
        return;
    }

    if (ss == last_ss) return;  /* 同秒不刷新 */
    last_ss = ss;

    snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
             (unsigned)hh, (unsigned)mm, (unsigned)ss);
    lv_label_set_text(label_clock_big, buf);
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
/**
  * @brief  主界面 Weather 按钮回调: 切到天气预报子界面
  */
static void weather_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (g_display_mode != DISP_MODE_FORECAST) {
        DisplayMode_Request(DISP_MODE_FORECAST);
    }
}

/**
  * @brief  天气预报屏 Back 按钮回调: 切回主界面
  *         (由 WeatherScreen 模块通过 SetBackCallback 注册)
  */
static void weather_forecast_back_cb(void)
{
    DisplayMode_Request(DISP_MODE_WEATHER);
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
 * @brief  "End" 按钮回调: 保留备用, 当前主界面 (IDLE) 不显示 End 按钮.
 *         实际 End 操作由 LearningScreen 内部的结束按钮调用.
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
 * @brief  底部三按钮构建 (主界面 IDLE 入口):
 *          | [Weather]   [Start]   [Camera]   |
 *          屏幕宽 320px, 每个按钮 90x28, 间隔 10px:
 *            - Weather: (20, 192)  - 切到天气预报
 *            - Start  : (115, 192) - 启动学习 (主操作, 高亮蓝)
 *            - Camera : (210, 192) - 切到摄像头
 *          注: 主界面仅在 IDLE 显示, 进入 PICKING/LEARNING 等状态会切到独立全屏,
 *              所以这三个按钮不需要做互斥切换, 一创建就常驻显示
 */
static void ControlBar_Build(void)
{
    lv_obj_t *parent = g_weather_screen;
    if (parent == NULL) return;

    /* --- Weather 按钮 (青色) --- */
    btn_weather = lv_btn_create(parent);
    lv_obj_set_size(btn_weather, 90, 28);
    lv_obj_set_pos(btn_weather, 20, 192);
    lv_obj_set_style_bg_color(btn_weather, lv_color_hex(0x00897B), 0);
    lv_obj_set_style_bg_opa(btn_weather, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_weather, 4, 0);
    lv_obj_set_style_border_width(btn_weather, 1, 0);
    lv_obj_set_style_border_color(btn_weather, lv_color_hex(0x00695C), 0);
    lv_obj_set_style_shadow_width(btn_weather, 0, 0);
    lv_obj_set_style_pad_all(btn_weather, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_weather);
        lv_label_set_text(lbl, "Weather");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, FONT_EN12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_weather, weather_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* --- Start 按钮 (蓝紫色, 主操作) --- */
    btn_start = lv_btn_create(parent);
    lv_obj_set_size(btn_start, 90, 28);
    lv_obj_set_pos(btn_start, 115, 192);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x6667FF), 0);
    lv_obj_set_style_bg_opa(btn_start, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_start, 4, 0);
    lv_obj_set_style_border_width(btn_start, 1, 0);
    lv_obj_set_style_border_color(btn_start, lv_color_hex(0x4040CC), 0);
    lv_obj_set_style_shadow_width(btn_start, 0, 0);
    lv_obj_set_style_pad_all(btn_start, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_start);
        lv_label_set_text(lbl, "Start");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, FONT_EN12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_start, state_btn1_event_cb, LV_EVENT_CLICKED, NULL);

    /* --- Camera 按钮 (青绿色) --- */
    btn_cam = lv_btn_create(parent);
    lv_obj_set_size(btn_cam, 90, 28);
    lv_obj_set_pos(btn_cam, 210, 192);
    lv_obj_set_style_bg_color(btn_cam, lv_color_hex(0x26A69A), 0);
    lv_obj_set_style_bg_opa(btn_cam, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_cam, 4, 0);
    lv_obj_set_style_border_width(btn_cam, 1, 0);
    lv_obj_set_style_border_color(btn_cam, lv_color_hex(0x00897B), 0);
    lv_obj_set_style_shadow_width(btn_cam, 0, 0);
    lv_obj_set_style_pad_all(btn_cam, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_cam);
        lv_label_set_text(lbl, "Camera");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, FONT_EN12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_cam, cam_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief  保留以兼容旧调用 (主循环中调用).
 *         现在主界面仅 IDLE 状态显示, 三按钮已固定不变, 此函数保留为空操作.
 *         状态文字/按钮切换等逻辑已迁移到 LearningScreen (学习屏) 内
 *         处理 (PICKING/LEARNING/PAUSED/AWAY 各自的全屏界面)
 */
static void ControlBar_Update(void)
{
    /* No-op: 主界面三按钮固定显示, 状态切换在独立子界面处理 */
}

static void Weather_ApplyMode(DisplayMode_t mode)
{
    /* 切换时强制设置屏幕方向。
     * 全屏整体旋转 180°: 原来所有画面用 LCD_direction(3), 现改为
     * LCD_direction(1) (两者互为 180° 翻转关系, 均为 320x240 横屏)。
     * 触摸坐标补偿已同步在 TouchTask.c 中去掉 (原补偿正是为抵消
     * "校准时方向(1) vs 显示方向(3)" 的错位, 现在两者一致, 不再需要)。 */
    LCD_direction(1);

    if (mode == DISP_MODE_CAMERA) {
        if (g_cam_screen) lv_scr_load(g_cam_screen);
    } else if (mode == DISP_MODE_FORECAST) {
        lv_obj_t *s = WeatherScreen_GetScreen();
        if (s) lv_scr_load(s);
    } else { /* DISP_MODE_WEATHER */
        if (g_weather_screen) lv_scr_load(g_weather_screen);
    }
    lv_obj_invalidate(lv_scr_act());
    g_display_mode = mode;
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
  * @note   实时从 lv_roller 读取当前选中值, 不依赖 g_picker_selected_min.
  *         原因: LVGL 的 LV_EVENT_VALUE_CHANGED 事件在滚轮"动画结束"后才
  *         触发; 若用户快速点选 → 立即按 Start, 该事件可能尚未派发,
  *         导致 g_picker_selected_min 仍是旧值 (默认 30 min).
  *         这里直接 lv_roller_get_selected() 读"按下瞬间"的索引,
  *         再用 picker_options[idx] 取分钟数, 彻底规避事件时序问题。
  */
static void picker_btn_event_cb(lv_event_t *e)
{
    (void)e;
    uint8_t min = STATE_DEFAULT_DURATION_MIN;
    if (g_picker_roller) {
        uint16_t idx = lv_roller_get_selected(g_picker_roller);
        if (idx < PICKER_OPTIONS_COUNT) {
            min = (uint8_t)picker_options[idx];
        }
    }
    State_PostEventParam(STATE_EVT_GUI_CONFIRM_DURATION, min);
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

    /* 初始值: 优先使用 StateTask 中已保存的 s_target_minutes (重试流程时,
     * 沿用用户上次选的非默认时长); 若未设置/越界, 回退到默认值。
     * 目的: 用户因"无人在座"被退回 IDLE → 重新点 Start 后, Picker 直接
     * 显示上次选的 45/60/90/120 等, 不用再重选一次。 */
    uint8_t init_min = State_GetTargetMinutes();
    if (init_min == 0) init_min = STATE_DEFAULT_DURATION_MIN;

    g_picker_user_touched = 0;
    g_picker_selected_min = init_min;
    g_picker_show_tick    = xTaskGetTickCount();

    /* 重置滚轮: 若 init_min 命中合法选项, 滚到那一项; 否则保持默认 30 min */
    int set_idx = -1;
    for (int i = 0; i < PICKER_OPTIONS_COUNT; i++) {
        if (picker_options[i] == init_min) { set_idx = i; break; }
    }
    if (set_idx < 0) {
        for (int i = 0; i < PICKER_OPTIONS_COUNT; i++) {
            if (picker_options[i] == STATE_DEFAULT_DURATION_MIN) { set_idx = i; break; }
        }
    }
    if (g_picker_roller && set_idx >= 0) {
        lv_roller_set_selected(g_picker_roller, set_idx, LV_ANIM_OFF);
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

/**
  * @brief  进入 SUMMARY 状态时调用: 把学习报告数据打包成 JSON, 通过 UART3 发给 ESP-01
  *         (ESP-01 上的固件负责把 JSON 转发到 MQTT 服务器)
  *
  * @note   1) 发送频率 ≤ 1次/会话, 完全可接受阻塞发送 (115200, < 30ms)
  *         2) 发送失败不做重试, 不影响 UI 切屏
  *         3) JSON 字段与 LRSessionData_t 一一对应
  *         4) 末尾追加 '\n' 作为帧结束 (匹配 ESP-01 固件期望)
  *         5) auto_ended 含义: 0=手动 End, 1=3min 离开超时, 2=目标时长达成
  */
static void learning_report_upload(const LRSessionData_t *d)
{
    if (d == NULL) return;

    char json[256];
    int n = snprintf(json, sizeof(json),
        "{\"total_sec\":%u,\"target_sec\":%u,"
        "\"avg_score\":%u,\"focus_pct\":%u,\"distract_pct\":%u,\"phone_pct\":%u,"
        "\"posture_bad\":%u,\"away\":%u,\"pause\":%u,\"auto_ended\":%u}\n",
        (unsigned)d->total_seconds,
        (unsigned)d->target_seconds,
        (unsigned)d->avg_total_score,
        (unsigned)d->focus_pct,
        (unsigned)d->distract_pct,
        (unsigned)d->phone_pct,
        (unsigned)d->posture_abnormal_count,
        (unsigned)d->away_count,
        (unsigned)d->pause_count,
        (unsigned)d->auto_ended);

    if (n <= 0 || n >= (int)sizeof(json)) {
        printf("[ESP01-DBG] JSON format overflow (n=%d)\r\n", n);   /* 走 USART1 */
        return;
    }

    int rc = UART_ESP01_SendString(json);
    if (rc == 0) {
        printf("[ESP01-DBG] Report uploaded %d bytes to ESP-01\r\n", n);   /* 走 USART1 */
    } else {
        printf("[ESP01-DBG] Report upload FAILED (HAL err)\r\n");          /* 走 USART1 */
    }
}

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

    /* 构建天气界面 (顶部状态栏 + 中央时钟 + 中部环境 + 底部三按钮) */
    Weather_UI_Build();
    ControlBar_Build();   /* 底部三按钮: Weather / Start / Camera */

    /* 构建学习界面 (秒表+室内数据) */
    LearningScreen_Init();

    /* 构建天气预报子界面 (网络天气详情页) */
    WeatherScreen_Init();
    WeatherScreen_SetBackCallback(weather_forecast_back_cb);

    /* 构建学习报告屏 (侘寂暖灰调, 显示给用户的版本) */
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

        /* 更新本地传感器数据 → 底部室内温湿度小字 */
        if (xSensorDataQueue != NULL &&
            xQueuePeek(xSensorDataQueue, &sd, 0) == pdTRUE)
        {
            if (sd.timestamp != last_sd_ts) {
                last_sd_ts = sd.timestamp;
                Weather_UI_UpdateLocal(&sd);
            }
        }

        /* 远程天气: 收到新数据时更新 WiFi 在线时间戳 + 天气预报子界面 */
        if (Weather_GetLatest(&wi) == 0)
        {
            if (wi.timestamp != last_wi_ts) {
                last_wi_ts = wi.timestamp;
                g_last_remote_tick = xTaskGetTickCount();
                WeatherScreen_Update(&wi);
            }
        }

        /* 主界面大字时钟 (RTC 驱动, 每秒刷新一次) */
        Weather_UI_UpdateClock();

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
                    d.phone_pct              = sm.phone_pct;
                    d.posture_abnormal_count = sm.posture_abnormal_count;
                    d.away_count             = sm.away_count;
                    d.pause_count            = sm.pause_count;
                    d.auto_ended             = sm.auto_ended;
                }

                /* Push real data to the Learning Report screen, then switch to it */
                LearningReport_Update(&d);
                LearningReport_RefreshAnim();
                Picker_Hide();

                /* 上传报告数据到 ESP-01 (再由 ESP-01 转发 MQTT)
                 * 阻塞发送, ≤30ms, 不影响 UI 切屏;
                 * 放在 Picker_Hide 和 lv_scr_load 之间, 失败不重试不报错
                 *
                 * 通道契约: USART3 (PD8/TX, PB11/RX) 只用于发学习报告 JSON,
                 *          所有 printf 调试一律走 USART1, 不要混用. */
                learning_report_upload(&d);

                if (LearningReport_GetScreen()) {
                    LCD_direction(1);
                    lv_scr_load(LearningReport_GetScreen());
                }
            }
            else if (cur_state == ST_PICKING)
            {
                /* 进入 PICKING: 切到学习界面 + 显示时长选择器 */
                LearningScreen_UpdateStatus();
                LearningScreen_UpdateStopwatch();
                if (LearningScreen_GetScreen()) {
                    LCD_direction(1);
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
                    LCD_direction(1);
                    lv_scr_load(LearningScreen_GetScreen());
                }
            }
            else if (cur_state == ST_IDLE)
            {
                /* 回到 IDLE: 切到主仪表板 */
                Picker_Hide();
                if (g_weather_screen) {
                    LCD_direction(1);
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