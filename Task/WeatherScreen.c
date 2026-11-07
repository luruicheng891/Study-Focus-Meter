/**
  ******************************************************************************
  * @file    WeatherScreen.c
  * @brief   天气预报子界面 — 网络天气详情页
  *
  *          屏幕布局见 WeatherScreen.h 头注释.
  *          数据源: WeatherTask 通过 Weather_GetLatest() 提供.
  *          切屏: 主界面 Weather 按钮 → 本屏; 本屏 Back 按钮 → 主界面.
  ******************************************************************************
  */

#include "WeatherScreen.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* ======================== 颜色定义 ======================== */
#define WS_BG_COLOR     lv_color_hex(0xE6EBEF)
#define WS_TITLE_BG     lv_color_hex(0x6667FF)
#define WS_TITLE_FG     lv_color_hex(0xFFFFFF)
#define WS_TEXT_COLOR   lv_color_hex(0x222222)
#define WS_DIM_COLOR    lv_color_hex(0x607D8B)
#define WS_TEMP_COLOR   lv_color_hex(0xE53935)
#define WS_HUMI_COLOR   lv_color_hex(0x1E88E5)
#define WS_BTN_BG       lv_color_hex(0x6667FF)
#define WS_BTN_BORDER   lv_color_hex(0x4040CC)
#define WS_BACK_BG      lv_color_hex(0x607D8B)
#define WS_BACK_BORDER  lv_color_hex(0x455A64)

/* ======================== 字体定义 ======================== */
#define WS_FONT_TITLE   &lv_font_montserrat_16   /* 顶部标题 */
#define WS_FONT_BIG     &lv_font_montserrat_28   /* 大字温度/湿度 */
#define WS_FONT_TEXT    &lv_font_montserrat_14   /* 正文 */
#define WS_FONT_SMALL   &lv_font_montserrat_12   /* 小字/按钮 */

/* ======================== 控件指针 ======================== */
static lv_obj_t *g_screen        = NULL;
static lv_obj_t *lbl_city        = NULL;
static lv_obj_t *lbl_temp       = NULL;
static lv_obj_t *lbl_humi        = NULL;
static lv_obj_t *lbl_cond       = NULL;
static lv_obj_t *lbl_feel        = NULL;
static lv_obj_t *lbl_wind        = NULL;
static lv_obj_t *lbl_pres       = NULL;
static lv_obj_t *lbl_updated     = NULL;

/* Back 按钮回调 (由 Display.c 注册, 切回主界面) */
static void (*g_back_cb)(void) = NULL;

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

/* ======================== Back 按钮回调 ======================== */
static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (g_back_cb) g_back_cb();
}

/* ======================== UI 构建 ======================== */
static void WeatherScreen_Build(void)
{
    g_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen, WS_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_screen, 0, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* === 顶部状态栏 (320x30) ===
     * 左侧: Back 按钮 (10, 3) 70x24
     * 右侧标题: "Weather Forecast" 居中 */
    lv_obj_t *btn_back = lv_btn_create(g_screen);
    lv_obj_set_size(btn_back, 70, 24);
    lv_obj_set_pos(btn_back, 6, 3);
    lv_obj_set_style_bg_color(btn_back, WS_BACK_BG, 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_border_color(btn_back, WS_BACK_BORDER, 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_set_style_pad_all(btn_back, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_back);
        lv_label_set_text(lbl, "< Back");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, WS_FONT_SMALL, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_back, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* 顶部标题栏背景 + 文字 */
    lv_obj_t *title_bg = lv_obj_create(g_screen);
    lv_obj_set_pos(title_bg, 80, 0);
    lv_obj_set_size(title_bg, 240, 30);
    lv_obj_set_style_bg_color(title_bg, WS_TITLE_BG, 0);
    lv_obj_set_style_bg_opa(title_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_bg, 0, 0);
    lv_obj_set_style_radius(title_bg, 0, 0);
    lv_obj_set_style_pad_all(title_bg, 0, 0);
    lv_obj_clear_flag(title_bg, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *title = lv_label_create(title_bg);
        lv_label_set_text(title, "Weather Forecast");
        lv_obj_set_style_text_color(title, WS_TITLE_FG, 0);
        lv_obj_set_style_text_font(title, WS_FONT_TITLE, 0);
        lv_obj_center(title);
    }

    /* === 数据区 (y=40 起) === */

    /* 城市名 (y=40) */
    lbl_city = make_label(g_screen, "City: ----",
                           WS_FONT_TEXT, WS_TEXT_COLOR, 10, 40);

    /* 温度 / 湿度 (y=68, 大字 28pt) */
    lbl_temp = make_label(g_screen, "-- C",
                           WS_FONT_BIG, WS_TEMP_COLOR, 20, 68);
    lbl_humi = make_label(g_screen, "-- %",
                           WS_FONT_BIG, WS_HUMI_COLOR, 175, 68);

    /* 温度 / 湿度 标签 (小字说明, 在大字上方) */
    make_label(g_screen, "Temp", WS_FONT_SMALL, WS_DIM_COLOR, 20, 60);
    make_label(g_screen, "Humi", WS_FONT_SMALL, WS_DIM_COLOR, 175, 60);

    /* 天气状况 (y=120) */
    lbl_cond = make_label(g_screen, "Condition: ----",
                           WS_FONT_TEXT, WS_TEXT_COLOR, 10, 120);

    /* 体感温度 (y=145) */
    lbl_feel = make_label(g_screen, "Feel: -- C",
                           WS_FONT_TEXT, WS_TEXT_COLOR, 10, 145);

    /* 风速 / 气压 (y=170) */
    lbl_wind = make_label(g_screen, "Wind: -- m/s",
                           WS_FONT_TEXT, WS_TEXT_COLOR, 10, 170);
    lbl_pres = make_label(g_screen, "Pres: -- hPa",
                           WS_FONT_TEXT, WS_TEXT_COLOR, 170, 170);

    /* 更新时间 (y=205, 小字) */
    lbl_updated = make_label(g_screen, "Updated: --",
                              WS_FONT_SMALL, WS_DIM_COLOR, 10, 205);
}

/* ======================== 公开 API ======================== */
void WeatherScreen_Init(void)
{
    if (g_screen != NULL) return;   /* 已构建 */
    WeatherScreen_Build();
}

lv_obj_t *WeatherScreen_GetScreen(void)
{
    return g_screen;
}

void WeatherScreen_SetBackCallback(void (*cb)(void))
{
    g_back_cb = cb;
}

void WeatherScreen_Update(const WeatherInfo_t *w)
{
    char buf[48];

    if (lbl_city == NULL) return;   /* 未初始化 */

    /* 无数据时显示占位 */
    if (w == NULL) {
        lv_label_set_text(lbl_city,     "City: ----");
        lv_label_set_text(lbl_temp,     "-- C");
        lv_label_set_text(lbl_humi,     "-- %");
        lv_label_set_text(lbl_cond,     "Condition: ----");
        lv_label_set_text(lbl_feel,     "Feel: -- C");
        lv_label_set_text(lbl_wind,     "Wind: -- m/s");
        lv_label_set_text(lbl_pres,     "Pres: -- hPa");
        lv_label_set_text(lbl_updated,  "Updated: --");
        return;
    }

    /* 城市 */
    snprintf(buf, sizeof(buf), "City: %s",
             w->city[0] ? w->city : "----");
    lv_label_set_text(lbl_city, buf);

    /* 温度 (temp_x10 单位 0.1°C) */
    {
        int t_int = w->temp_x10 / 10;
        int t_frac = w->temp_x10 % 10;
        if (t_frac < 0) t_frac = -t_frac;
        snprintf(buf, sizeof(buf), "%d.%d C", t_int, t_frac);
        lv_label_set_text(lbl_temp, buf);
    }

    /* 湿度 */
    snprintf(buf, sizeof(buf), "%u %%", (unsigned)w->humi);
    lv_label_set_text(lbl_humi, buf);

    /* 天气状况 */
    snprintf(buf, sizeof(buf), "Condition: %s",
             w->cond[0] ? w->cond : "----");
    lv_label_set_text(lbl_cond, buf);

    /* 体感温度 (feel_x10 单位 0.1°C) */
    {
        int f_int = w->feel_x10 / 10;
        int f_frac = w->feel_x10 % 10;
        if (f_frac < 0) f_frac = -f_frac;
        snprintf(buf, sizeof(buf), "Feel: %d.%d C", f_int, f_frac);
        lv_label_set_text(lbl_feel, buf);
    }

    /* 风速 + 风向 (wind_x10 单位 0.1 m/s) */
    {
        int w_int = w->wind_x10 / 10;
        int w_frac = w->wind_x10 % 10;
        snprintf(buf, sizeof(buf), "Wind: %d.%d m/s %s",
                 w_int, w_frac, w->wdir);
        lv_label_set_text(lbl_wind, buf);
    }

    /* 气压 */
    snprintf(buf, sizeof(buf), "Pres: %u hPa", (unsigned)w->pres);
    lv_label_set_text(lbl_pres, buf);

    /* 更新时间 (HH:MM 字符串) */
    snprintf(buf, sizeof(buf), "Updated: %s",
             w->time[0] ? w->time : "--");
    lv_label_set_text(lbl_updated, buf);
}
