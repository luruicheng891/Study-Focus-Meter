/**
  ******************************************************************************
  * @file    WeatherTask.c
  * @brief   320x240 横屏天气仪表板 (复用 Gui Guider 字体 + 小图标)
  *
  *          布局 (320x240):
  *          +--------------------------------------------+
  *          | [WiFi 20x20] ON         Time: 12:34:56    |  顶部状态条
  *          +-----------------------+--------------------+
  *          |        室内           |       室外         |
  *          |                       |                    |
  *          | [温40x40] 25 [°25x25] | [温40x40] 27 [°25] |
  *          | [湿30x30] 56 [%26x26] | [湿30x30] 65 [%26] |
  *          | City: Shanghai        | Feel 26 / Sunny    |
  *          | Lux: 100              |                    |
  *          +-----------------------+--------------------+
  *
  *          说明:
  *          1) Gui Guider 默认设计稿 480x320, 直接 setup_ui 会被裁掉, 故手工
  *             搭 320x240 布局, 仅复用其字体与小图标.
  *          2) 480x320 大背景图 (_Background00) 不引用, ARM linker 会丢弃.
  ******************************************************************************
  */

#include "WeatherTask.h"
#include "SensorTask.h"
#include "Camera.h"
#include "display_mode.h"
#include "UART.h"            /* WeatherInfo_t / xWeatherQueue */
#include "lcd.h"             /* LCD_direction */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lvgl.h"

/* Gui Guider 头文件 (字体 + 图片 LV_IMG_DECLARE) */
#include "gui_guider.h"

#include <stdio.h>
#include <string.h>

/* ======================== 配色 ======================== */
#define BG_COLOR        lv_color_hex(0xE6EBEF)
#define CARD_BG         lv_color_hex(0xFFFFFF)
#define BORDER_COLOR    lv_color_hex(0x6667FF)
#define TITLE_BG        lv_color_hex(0x6667FF)
#define TITLE_FG        lv_color_hex(0xFFFFFF)
#define TEMP_COLOR      lv_color_hex(0xE53935)
#define HUMI_COLOR      lv_color_hex(0x1E88E5)
#define TEXT_COLOR      lv_color_hex(0x222222)
#define DIM_COLOR       lv_color_hex(0x607D8B)

/* ======================== 字体 ======================== */
#define FONT_TITLE      &lv_font_SourceHanSerifSC_Regular_17  /* 中文 室内/室外 */
#define FONT_BIG        &lv_font_Alatsi_Regular_50            /* 大数字 */
#define FONT_MED        &lv_font_Alatsi_Regular_25            /* 单位/中等 */
#define FONT_SMALL      &lv_font_montserrat_14                /* ASCII 小字 */
#define FONT_TIME       &lv_font_montserrat_18                /* 时间专用字体 18px */

/* ======================== 全局对象 ======================== */
/* Gui Guider widgets_init.c 通过 extern 引用 guider_ui, 提供定义 */
lv_ui guider_ui;

/* 屏幕 */
static lv_obj_t *g_weather_screen = NULL;
static lv_obj_t *g_cam_screen     = NULL;

/* 数据控件 */
static lv_obj_t *label_wifi      = NULL;
static lv_obj_t *label_time      = NULL;      /* 顶部时间显示 */
static lv_obj_t *label_in_temp   = NULL;
static lv_obj_t *label_in_humi   = NULL;
static lv_obj_t *label_in_city   = NULL;      /* 室内城市名 */
static lv_obj_t *label_in_lux    = NULL;
static lv_obj_t *label_out_temp  = NULL;
static lv_obj_t *label_out_humi  = NULL;
static lv_obj_t *label_out_feel  = NULL;
static lv_obj_t *label_out_cond  = NULL;

/* 远端最新数据时间, 用于 WiFi 状态判定 */
static uint32_t last_remote_tick = 0;

/* ======================== 工具 ======================== */
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, BORDER_COLOR, 0);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
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
    lv_obj_t *t = make_label(card, txt, FONT_TITLE, TITLE_FG, 0, 0);
    lv_obj_set_size(t, w, 24);
    lv_obj_set_style_bg_color(t, TITLE_BG, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(t, 2, 0);
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
    /* 独立屏幕 */
    g_weather_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_weather_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(g_weather_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_weather_screen, 0, 0);
    lv_obj_clear_flag(g_weather_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* === 顶部状态条 (320x30) === */
    /* WiFi 图标 (靠左) */
    make_icon(g_weather_screen, &_WIFI_alpha_20x20, 6, 5);
    /* WiFi 状态文字 */
    label_wifi = make_label(g_weather_screen, "--",
                             FONT_SMALL, TEXT_COLOR, 32, 8);
    
    /* 时间显示 (靠右，使用更大字体) */
    label_time = make_label(g_weather_screen, "Time: --",
                            FONT_TIME, TEXT_COLOR, 210, 5);  // 位置左移，留出空间

    /* === 室内卡 (5,40) ~ (158,245), 153x200 === */
    lv_obj_t *card_in = make_card(g_weather_screen, 5, 40, 153, 200);
    make_card_title(card_in, "\xE5\xAE\xA4\xE5\x86\x85", 149);  /* 室内 UTF-8 */

    /* 温度行: [温度计图标] 25 [°C图标] */
    make_icon(card_in, &_temperature_alpha_40x40, 4, 30);
    label_in_temp = make_label(card_in, "--",
                                FONT_BIG, TEMP_COLOR, 50, 28);
    make_icon(card_in, &_Celsius_alpha_25x25, 118, 42);

    /* 湿度行: [湿度图标] 56 [%图标] */
    make_icon(card_in, &_humidity_alpha_30x30, 8, 92);
    label_in_humi = make_label(card_in, "--",
                                FONT_BIG, HUMI_COLOR, 50, 88);
    make_icon(card_in, &_percentage_alpha_26x26, 118, 100);

    /* 城市名 - 放在湿度下方 */
    label_in_city = make_label(card_in, "City: --",
                                FONT_SMALL, TEXT_COLOR, 6, 152);
    
    /* 光照强度 - 放在城市名下方 */
    label_in_lux = make_label(card_in, "Lux: --",
                               FONT_SMALL, TEXT_COLOR, 6, 170);

    /* === 室外卡 (162,40) ~ (315,245), 153x200 === */
    lv_obj_t *card_out = make_card(g_weather_screen, 162, 40, 153, 200);
    make_card_title(card_out, "\xE5\xAE\xA4\xE5\xA4\x96", 149);  /* 室外 UTF-8 */

    /* 温度行 */
    make_icon(card_out, &_temperature_alpha_40x40, 4, 30);
    label_out_temp = make_label(card_out, "--",
                                 FONT_BIG, TEMP_COLOR, 50, 28);
    make_icon(card_out, &_Celsius_alpha_25x25, 118, 42);

    /* 湿度行 */
    make_icon(card_out, &_humidity_alpha_30x30, 8, 92);
    label_out_humi = make_label(card_out, "--",
                                 FONT_BIG, HUMI_COLOR, 50, 88);
    make_icon(card_out, &_percentage_alpha_26x26, 118, 100);

    /* 天气状况 + 体感温度 */
    label_out_cond = make_label(card_out, "----",
                                 FONT_SMALL, DIM_COLOR, 6, 152);
    label_out_feel = make_label(card_out, "Feel --",
                                 FONT_SMALL, DIM_COLOR, 6, 170);
}

/* ======================== UI 数据更新 ======================== */
static void Weather_UI_UpdateLocal(const SensorData_t *d)
{
    char buf[32];

    /* 更新温度 */
    int t = (int)(d->temperature / 100);
    snprintf(buf, sizeof(buf), "%d", t);
    if(label_in_temp) lv_label_set_text(label_in_temp, buf);

    /* 更新湿度 */
    snprintf(buf, sizeof(buf), "%u", (unsigned)(d->humidity / 100));
    if(label_in_humi) lv_label_set_text(label_in_humi, buf);

    /* 更新光照强度 */
    snprintf(buf, sizeof(buf), "Lux: %u", (unsigned)(d->lux_x100 / 100));
    if(label_in_lux) lv_label_set_text(label_in_lux, buf);
}

static void Weather_UI_UpdateRemote(const WeatherInfo_t *w)
{
    char buf[40];

    /* 更新室内卡的城市名 */
    if(label_in_city) {
        snprintf(buf, sizeof(buf), "%s", w->city[0] ? w->city : "----");
        lv_label_set_text(label_in_city, buf);
    }

    /* 更新顶部时间显示（使用更大字体） */
    if(label_time) {
        if(w->time[0] != '\0') {
            snprintf(buf, sizeof(buf), "%s", w->time);  // 只显示时间，不加 "Time:" 前缀节省空间
        } else {
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)w->timestamp);
        }
        lv_label_set_text(label_time, buf);
    }

    /* 更新室外温度 */
    int t = w->temp_x10 / 10;
    snprintf(buf, sizeof(buf), "%d", t);
    if(label_out_temp) lv_label_set_text(label_out_temp, buf);

    /* 更新室外湿度 */
    snprintf(buf, sizeof(buf), "%u", (unsigned)w->humi);
    if(label_out_humi) lv_label_set_text(label_out_humi, buf);

    /* 更新天气状况 */
    if(label_out_cond) {
        lv_label_set_text(label_out_cond, w->cond[0] ? w->cond : "----");
    }

    /* 更新体感温度 */
    if(label_out_feel) {
        int f = w->feel_x10 / 10;
        snprintf(buf, sizeof(buf), "Feel %d C", f);
        lv_label_set_text(label_out_feel, buf);
    }
}

/* WiFi 在线状态: 30 秒内收到过远程数据 = ON */
static void Weather_UI_RefreshWifi(void)
{
    if(label_wifi == NULL) return;
    if(last_remote_tick == 0) {
        lv_label_set_text(label_wifi, "--");
    } else if((xTaskGetTickCount() - last_remote_tick) < pdMS_TO_TICKS(30000)) {
        lv_label_set_text(label_wifi, "ON");
    } else {
        lv_label_set_text(label_wifi, "OFF");
    }
}

/* ======================== 屏幕模式切换 ======================== */
static void Weather_ApplyMode(DisplayMode_t mode)
{
    if(mode == DISP_MODE_CAMERA) {
        LCD_direction(1);
        if(g_cam_screen) lv_scr_load(g_cam_screen);
    } else { /* DISP_MODE_WEATHER */
        LCD_direction(3);
        if(g_weather_screen) lv_scr_load(g_weather_screen);
    }
    lv_obj_invalidate(lv_scr_act());
    g_display_mode = mode;
}

/* ======================== 任务主体 ======================== */
void Weather_Task(void *pvParameters)
{
    SensorData_t  sd;
    WeatherInfo_t wi;
    uint32_t last_sd_ts = 0;
    uint32_t last_wi_ts = 0;
    DisplayMode_t pending;

    (void)pvParameters;

    /* 等 Camera_task 把 canvas 创建好 */
    for(int i = 0; i < 30 && g_cam_canvas == NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    g_cam_screen = (g_cam_canvas != NULL) ?
                    lv_obj_get_screen(g_cam_canvas) : lv_scr_act();

    /* 创建天气屏幕 */
    Weather_UI_Build();

    /* 默认进入摄像头模式 */
    Weather_ApplyMode(DISP_MODE_WEATHER);

    for(;;)
    {
        /* 模式切换请求 */
        if(DisplayMode_TakePending(&pending)) {
            Weather_ApplyMode(pending);
        }

        /* 本地传感器更新 */
        if(xSensorDataQueue != NULL &&
           xQueuePeek(xSensorDataQueue, &sd, 0) == pdTRUE)
        {
            if(sd.timestamp != last_sd_ts) {
                last_sd_ts = sd.timestamp;
                Weather_UI_UpdateLocal(&sd);
            }
        }

        /* 远程天气更新 */
        if(xWeatherQueue != NULL &&
           xQueuePeek(xWeatherQueue, &wi, 0) == pdTRUE)
        {
            if(wi.timestamp != last_wi_ts) {
                last_wi_ts = wi.timestamp;
                last_remote_tick = xTaskGetTickCount();
                Weather_UI_UpdateRemote(&wi);
            }
        }

        Weather_UI_RefreshWifi();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}