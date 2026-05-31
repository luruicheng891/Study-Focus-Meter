/**
  ******************************************************************************
  * @file    WeatherTask.c
  * @brief   LVGL 环境数据显示任务 (天气仪表板风格)
  *          - 本地传感器: AHT20 温湿度 + BH1750 光照
  *          - 远程天气:   ESP-01 通过 USART3 推送的 JSON 数据
  *
  *          屏幕显示模式由 display_mode 模块控制, 通过 USART1 命令切换:
  *            'C'/'c' → 显示摄像头画面 (隐藏天气面板)
  *            'W'/'w' → 显示天气仪表板 (隐藏摄像头 canvas)
  *
  *          全部天气 UI 控件挂在 g_weather_panel 容器下,
  *          切换模式时仅 add/clear LV_OBJ_FLAG_HIDDEN, 高效不重建.
  ******************************************************************************
  */

#include "WeatherTask.h"
#include "SensorTask.h"
#include "Camera.h"
#include "display_mode.h"
#include "UART.h"           /* WeatherInfo_t / xWeatherQueue */
#include "lcd.h"             /* LCD_direction */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* ======================== 调色板 (Material Dark Weather) ======================== */
#define COLOR_BG          lv_color_hex(0x1A2332)   /* 深蓝灰背景 */
#define COLOR_CARD        lv_color_hex(0x2A3A52)   /* 卡片底色 */
#define COLOR_CARD_ALT    lv_color_hex(0x35476B)   /* 卡片次色 */
#define COLOR_ACCENT      lv_color_hex(0x4FC3F7)   /* 主强调 (青) */
#define COLOR_TEMP        lv_color_hex(0xFF8A65)   /* 温度 (橙) */
#define COLOR_HUMI        lv_color_hex(0x4DD0E1)   /* 湿度 (青蓝) */
#define COLOR_LUX         lv_color_hex(0xFFD54F)   /* 光照 (黄) */
#define COLOR_WIND        lv_color_hex(0xA5D6A7)   /* 风 (绿) */
#define COLOR_PRES        lv_color_hex(0xCE93D8)   /* 气压 (紫) */
#define COLOR_TEXT        lv_color_hex(0xECEFF1)   /* 主文字 (浅白) */
#define COLOR_TEXT_DIM    lv_color_hex(0x90A4AE)   /* 次文字 (灰) */

/* ======================== UI 控件 ======================== */
static lv_obj_t *g_weather_panel = NULL;   /* 整个天气面板容器 (240x320) */

/* 远程天气 (网络) */
static lv_obj_t *label_w_city = NULL;
static lv_obj_t *label_w_temp = NULL;     /* 大字号 */
static lv_obj_t *label_w_cond = NULL;     /* 天气状况 */
static lv_obj_t *label_w_feel = NULL;     /* 体感 */
static lv_obj_t *label_w_humi = NULL;
static lv_obj_t *label_w_wind = NULL;
static lv_obj_t *label_w_pres = NULL;

/* 本地传感器 */
static lv_obj_t *label_l_temp = NULL;
static lv_obj_t *label_l_humi = NULL;
static lv_obj_t *label_l_lux  = NULL;

/* ======================== 工具函数 ======================== */

static lv_obj_t *card_create(lv_obj_t *parent, lv_color_t bg,
                             lv_coord_t w, lv_coord_t h,
                             lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_align(card, align, x, y);

    lv_obj_set_style_bg_color(card, bg, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    return card;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *txt,
                              const lv_font_t *font, lv_color_t color,
                              lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

/* ======================== UI 创建 (横屏 320x240 天气仪表板) ======================== */
static void Weather_UI_Create(void)
{
    /* 屏幕背景 */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lv_scr_act(), 0, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    /* === 容器: 整个天气面板 (320x240, 可整体 hide/show) === */
    g_weather_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_weather_panel, 320, 240);
    lv_obj_align(g_weather_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(g_weather_panel, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(g_weather_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_weather_panel, 0, 0);
    lv_obj_set_style_radius(g_weather_panel, 0, 0);
    lv_obj_set_style_pad_all(g_weather_panel, 6, 0);
    lv_obj_clear_flag(g_weather_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* === 顶部标题 (整宽) === */
    label_create(g_weather_panel, "Weather Monitor",
                 &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_MID, 0, 0);

    /* ============================================================
     * 横屏布局 (内容区 308x228, 左右两列):
     *   左列 (x=0, w=160):  Hero 卡 (网络天气 + 大温度)
     *   右列 (x=164, w=144):
     *      - 详情卡 (湿度/风/气压)
     *      - 本地室内卡 (温度/湿度/光照)
     * ============================================================ */

    /* === Hero 卡片 (网络天气主区, 156x196, x=0, y=20) === */
    lv_obj_t *hero = card_create(g_weather_panel, COLOR_CARD,
                                  156, 196,
                                  LV_ALIGN_TOP_LEFT, 0, 20);

    label_w_city = label_create(hero, "---",
                                &lv_font_montserrat_16, COLOR_ACCENT,
                                LV_ALIGN_TOP_LEFT, 0, 0);

    label_w_temp = label_create(hero, "--.-",
                                &lv_font_montserrat_28, COLOR_TEMP,
                                LV_ALIGN_TOP_LEFT, 0, 28);
    label_create(hero, "C", &lv_font_montserrat_16, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_LEFT, 100, 36);

    label_w_cond = label_create(hero, "---",
                                &lv_font_montserrat_16, COLOR_TEXT,
                                LV_ALIGN_TOP_LEFT, 0, 80);

    label_w_feel = label_create(hero, "Feels --.-",
                                &lv_font_montserrat_14, COLOR_TEXT_DIM,
                                LV_ALIGN_TOP_LEFT, 0, 110);

    /* hero 底部三列: 湿度/风/气压 (替代之前的 detail 卡) */
    label_create(hero, "Humi", &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_LEFT, 0, 138);
    label_w_humi = label_create(hero, "--%",
                                &lv_font_montserrat_14, COLOR_HUMI,
                                LV_ALIGN_TOP_LEFT, 0, 156);

    label_create(hero, "Wind", &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_MID, -8, 138);
    label_w_wind = label_create(hero, "---",
                                &lv_font_montserrat_14, COLOR_WIND,
                                LV_ALIGN_TOP_MID, -8, 156);

    label_create(hero, "Pres", &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_RIGHT, 0, 138);
    label_w_pres = label_create(hero, "----",
                                &lv_font_montserrat_14, COLOR_PRES,
                                LV_ALIGN_TOP_RIGHT, 0, 156);

    /* === 本地传感器卡 (右列, 144x196, x=164, y=20) === */
    lv_obj_t *local = card_create(g_weather_panel, COLOR_CARD_ALT,
                                   144, 196,
                                   LV_ALIGN_TOP_LEFT, 164, 20);

    label_create(local, "INDOOR",
                 &lv_font_montserrat_14, COLOR_ACCENT,
                 LV_ALIGN_TOP_LEFT, 0, 0);

    /* 三行: 温度 / 湿度 / 光照 */
    /* Temp 行 */
    label_create(local, "Temp", &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_LEFT, 0, 22);
    label_l_temp = label_create(local, "--.-",
                                &lv_font_montserrat_28, COLOR_TEMP,
                                LV_ALIGN_TOP_LEFT, 0, 38);
    label_create(local, "C", &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_LEFT, 100, 46);

    /* Humi 行 */
    label_create(local, "Humi", &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_LEFT, 0, 80);
    label_l_humi = label_create(local, "--%",
                                &lv_font_montserrat_20, COLOR_HUMI,
                                LV_ALIGN_TOP_LEFT, 0, 96);

    /* Lux 行 */
    label_create(local, "Lux", &lv_font_montserrat_14, COLOR_TEXT_DIM,
                 LV_ALIGN_TOP_LEFT, 0, 132);
    label_l_lux  = label_create(local, "----",
                                &lv_font_montserrat_20, COLOR_LUX,
                                LV_ALIGN_TOP_LEFT, 0, 148);
}

/* ======================== UI 更新 ======================== */
static void Weather_UI_UpdateLocal(SensorData_t *data)
{
    char buf[24];

    int32_t t_int = data->temperature / 100;
    int32_t t_dec = (data->temperature >= 0) ?
                    (data->temperature % 100) / 10 :
                    ((-data->temperature) % 100) / 10;
    sprintf(buf, "%d.%d", (int)t_int, (int)t_dec);
    if(label_l_temp) lv_label_set_text(label_l_temp, buf);

    sprintf(buf, "%u%%", (unsigned int)(data->humidity / 100));
    if(label_l_humi) lv_label_set_text(label_l_humi, buf);

    sprintf(buf, "%u", (unsigned int)(data->lux_x100 / 100));
    if(label_l_lux) lv_label_set_text(label_l_lux, buf);
}

static void Weather_UI_UpdateRemote(WeatherInfo_t *info)
{
    char buf[40];

    if(label_w_city) lv_label_set_text(label_w_city, info->city);

    int t = info->temp_x10;
    int t_int = t / 10;
    int t_dec = (t >= 0) ? (t % 10) : ((-t) % 10);
    sprintf(buf, "%d.%d", t_int, t_dec);
    if(label_w_temp) lv_label_set_text(label_w_temp, buf);

    sprintf(buf, "%u%%", (unsigned int)info->humi);
    if(label_w_humi) lv_label_set_text(label_w_humi, buf);

    if(label_w_cond) lv_label_set_text(label_w_cond, info->cond);

    if(label_w_feel) {
        int f = info->feel_x10;
        int f_int = f / 10;
        int f_dec = (f >= 0) ? (f % 10) : ((-f) % 10);
        sprintf(buf, "Feels %d.%d C", f_int, f_dec);
        lv_label_set_text(label_w_feel, buf);
    }
    if(label_w_wind) {
        int w_int = info->wind_x10 / 10;
        int w_dec = info->wind_x10 % 10;
        sprintf(buf, "%s %d.%d", info->wdir, w_int, w_dec);
        lv_label_set_text(label_w_wind, buf);
    }
    if(label_w_pres) {
        sprintf(buf, "%u", (unsigned int)info->pres);
        lv_label_set_text(label_w_pres, buf);
    }
}

/**
 * @brief 应用显示模式: 控制摄像头 canvas / 天气面板可见性, 并切换 LCD 方向
 *        - 摄像头模式: LCD 方向 1 (横屏 90°)
 *        - 天气模式:   LCD 方向 3 (横屏 270°, 即相对摄像头模式 180° 翻转)
 *        每次切换后整屏 invalidate, 让 LVGL 重绘以匹配新方向
 */
static void Weather_ApplyMode(DisplayMode_t mode)
{
    if(mode == DISP_MODE_CAMERA) {
        LCD_direction(1);
        if(g_cam_canvas)    lv_obj_clear_flag(g_cam_canvas, LV_OBJ_FLAG_HIDDEN);
        if(g_weather_panel) lv_obj_add_flag(g_weather_panel, LV_OBJ_FLAG_HIDDEN);
    } else { /* DISP_MODE_WEATHER */
        LCD_direction(3);
        if(g_cam_canvas)    lv_obj_add_flag(g_cam_canvas, LV_OBJ_FLAG_HIDDEN);
        if(g_weather_panel) lv_obj_clear_flag(g_weather_panel, LV_OBJ_FLAG_HIDDEN);
    }
    /* 强制整屏重绘, 让 LVGL 把当前画面按新方向刷一次 */
    lv_obj_invalidate(lv_scr_act());

    g_display_mode = mode;
}

/* ======================== 任务主体 ======================== */
void Weather_Task(void *pvParameters)
{
    SensorData_t sensor_data;
    WeatherInfo_t weather_info;
    uint32_t last_sensor_ts = 0;
    uint32_t last_weather_ts = 0;
    DisplayMode_t pending_mode;

    (void)pvParameters;

    /* 等待传感器/UART/Camera 任务初始化 (Camera 任务里创建 canvas) */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 创建 UI (在摄像头 canvas 之后创建, 保证天气面板位于 canvas 上层) */
    Weather_UI_Create();

    /* 初始默认为摄像头模式: 隐藏天气面板, 显示 canvas */
    Weather_ApplyMode(DISP_MODE_CAMERA);

    for(;;)
    {
        /* === 处理 USART1 命令导致的模式切换 === */
        if(DisplayMode_TakePending(&pending_mode)) {
            Weather_ApplyMode(pending_mode);
        }

        /* === 更新本地传感器数据 (无论是否显示, 标签内容都保持新鲜) === */
        if(xSensorDataQueue != NULL &&
           xQueuePeek(xSensorDataQueue, &sensor_data, 0) == pdTRUE)
        {
            if(sensor_data.timestamp != last_sensor_ts)
            {
                last_sensor_ts = sensor_data.timestamp;
                uint32_t now = xTaskGetTickCount();
                if((now - sensor_data.timestamp) < pdMS_TO_TICKS(3000))
                {
                    Weather_UI_UpdateLocal(&sensor_data);
                }
            }
        }

        /* === 更新远程天气数据 === */
        if(xWeatherQueue != NULL &&
           xQueuePeek(xWeatherQueue, &weather_info, 0) == pdTRUE)
        {
            if(weather_info.timestamp != last_weather_ts)
            {
                last_weather_ts = weather_info.timestamp;
                Weather_UI_UpdateRemote(&weather_info);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
