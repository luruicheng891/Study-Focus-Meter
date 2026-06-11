/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"

/*
 * 文件说明（screen_1 页面构建函数）
 * 1) 本文件由 Gui Guider 生成，负责一次性创建 screen_1 的所有对象和样式。
 * 2) 主要分区：顶部状态栏、左侧室内卡片、中部天气卡片、右侧室外卡片、底部高低温区。
 * 3) 运行期动态更新建议：
 *    - 温湿度数字：通过 screen_1_spangroup_7/8/10/11_span 更新
 *    - 日期文本：通过 screen_1_datetext_1 更新
 *    - 时钟文本：通过 screen_1_digital_clock_1（配合 timer）更新
 */

/*
 * 运行期“常改数据”句柄速查表：
 * -----------------------------------------------------------------
 * 1) 顶部时间：
 *    - 对象: ui->screen_1_digital_clock_1
 *    - 数据源: screen_1_digital_clock_1_hour/min/sec/meridiem
 *    - 刷新: screen_1_digital_clock_1_timer() -> lv_dclock_set_text_fmt()
 *
 * 2) 顶部日期：
 *    - 对象: ui->screen_1_datetext_1 (label)
 *    - 更新: lv_label_set_text(ui->screen_1_datetext_1, "2026/03/24")
 *
 * 3) 室内温湿度：
 *    - 温度: ui->screen_1_spangroup_7_span（默认 "98"）
 *    - 湿度: ui->screen_1_spangroup_8_span（默认 "73"）
 *
 * 4) 室外温湿度：
 *    - 温度: ui->screen_1_spangroup_10_span（默认 "28"）
 *    - 湿度: ui->screen_1_spangroup_11_span（默认 "73"）
 *
 * 5) 当天低/高温：
 *    - 低温: ui->screen_1_spangroup_13_span（默认 "27"）
 *    - 高温: ui->screen_1_spangroup_14_span（默认 "33"）
 * -----------------------------------------------------------------
 * 说明：spangroup 文本更新后，建议调用 lv_spangroup_refr_mode(对应spangroup)
 *       以确保立刻重排显示。
 */


int screen_1_digital_clock_1_min_value = 25;
int screen_1_digital_clock_1_hour_value = 11;
int screen_1_digital_clock_1_sec_value = 50;
char screen_1_digital_clock_1_meridiem[] = "AM";

/*
 * 时钟初始值说明：
 * - 这些全局变量在页面创建时作为初始显示值。
 * - 随后由 widgets_init.c 中的定时器每秒累加。
 * - 如果你要“外部同步RTC/NTP时间”，可在同步成功后直接覆写这四个变量。
 */

/*
 * screen_1 页面搭建入口
 * 注意：
 * - 此函数通常在 setup_ui() 中调用一次。
 * - 若你后续使用 GUI 重新生成代码，这里的手动修改可能被覆盖。
 */
void setup_scr_screen_1(lv_ui *ui)
{
    /* === A. 根屏幕与主容器（禁止滚动拖拽，保留触摸点击） === */
    //Write codes screen_1
    ui->screen_1 = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen_1, 480, 320);
    lv_obj_set_scrollbar_mode(ui->screen_1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->screen_1, LV_OBJ_FLAG_SCROLLABLE);

    //Write style for screen_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->screen_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->screen_1, lv_color_hex(0xE6EBEF), LV_PART_MAIN|LV_STATE_DEFAULT); // 优雅的浅灰蓝色纯色背景

    //Write codes screen_1_cont_1
    ui->screen_1_cont_1 = lv_obj_create(ui->screen_1);
    lv_obj_set_pos(ui->screen_1_cont_1, 0, 0);
    lv_obj_set_size(ui->screen_1_cont_1, 480, 320);
    lv_obj_set_scrollbar_mode(ui->screen_1_cont_1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->screen_1_cont_1, LV_OBJ_FLAG_SCROLLABLE);

    //Write style for screen_1_cont_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_radius(ui->screen_1_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->screen_1_cont_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui->screen_1_cont_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->screen_1_cont_1, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(ui->screen_1_cont_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_1_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    /* === B. 顶部状态栏：wifi 图标 + 时钟 + 日期 === */
    //Write codes screen_1_spangroup_1
    ui->screen_1_spangroup_1 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_1, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_1, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_1, LV_SPAN_MODE_FIXED);
    //create span
    ui->screen_1_spangroup_1_span = lv_spangroup_new_span(ui->screen_1_spangroup_1);
    lv_span_set_text(ui->screen_1_spangroup_1_span, "\n\n    wifi");
    lv_style_set_text_color(&ui->screen_1_spangroup_1_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_1_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_1_span->style, &lv_font_SourceHanSerifSC_Regular_12);
    lv_obj_set_pos(ui->screen_1_spangroup_1, 6, 3);
    lv_obj_set_size(ui->screen_1_spangroup_1, 470, 45);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_1_main_main_default
    static lv_style_t style_screen_1_spangroup_1_main_main_default;
    ui_init_style(&style_screen_1_spangroup_1_main_main_default);

    lv_style_set_radius(&style_screen_1_spangroup_1_main_main_default, 6);
    lv_style_set_border_width(&style_screen_1_spangroup_1_main_main_default, 2);
    lv_style_set_border_opa(&style_screen_1_spangroup_1_main_main_default, 219);
    lv_style_set_border_color(&style_screen_1_spangroup_1_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_border_side(&style_screen_1_spangroup_1_main_main_default, LV_BORDER_SIDE_FULL);
    lv_style_set_bg_opa(&style_screen_1_spangroup_1_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_1_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_1_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_1_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_1_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_1_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_1, &style_screen_1_spangroup_1_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_1);

    //Write codes screen_1_img_2
    ui->screen_1_img_2 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_2, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_2, &_WIFI_alpha_20x20);
    lv_img_set_pivot(ui->screen_1_img_2, 50,50);
    lv_img_set_angle(ui->screen_1_img_2, 0);
    lv_obj_set_pos(ui->screen_1_img_2, 21, 9);
    lv_obj_set_size(ui->screen_1_img_2, 20, 20);

    //Write style for screen_1_img_2, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_radius(ui->screen_1_img_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_2, true, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_digital_clock_1
    /*
     * 数字时钟对象创建：
     * - 这里使用 custom.c 中的 lv_dclock_create()（底层是 label 轻量实现）
     * - 下方 timer 仅创建一次，避免重复进入 setup 导致多定时器叠加。
     */
    static bool screen_1_digital_clock_1_timer_enabled = false;
    ui->screen_1_digital_clock_1 = lv_dclock_create(ui->screen_1_cont_1, "11:25 AM");
    if (!screen_1_digital_clock_1_timer_enabled) {
        lv_timer_create(screen_1_digital_clock_1_timer, 1000, NULL);
        screen_1_digital_clock_1_timer_enabled = true;
    }
    lv_obj_set_pos(ui->screen_1_digital_clock_1, 351, 7);
    lv_obj_set_size(ui->screen_1_digital_clock_1, 103, 33);

    //Write style for screen_1_digital_clock_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_radius(ui->screen_1_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_digital_clock_1, 7, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_1_digital_clock_1, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_1_digital_clock_1, &lv_font_Antonio_Regular_25, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_1_digital_clock_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_1_digital_clock_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_1_digital_clock_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_1_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_datetext_1
    /*
     * 日期文本：
     * - 当前为可点击对象，已绑定 screen_1_datetext_1_event_handler。
     * - 在你的项目里点击后会弹出日历选择（见 widgets_init.c）。
     */
    ui->screen_1_datetext_1 = lv_label_create(ui->screen_1_cont_1);
    lv_label_set_text(ui->screen_1_datetext_1, "2023/07/31");
    lv_obj_set_style_text_align(ui->screen_1_datetext_1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(ui->screen_1_datetext_1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->screen_1_datetext_1, screen_1_datetext_1_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_pos(ui->screen_1_datetext_1, 76, 7);
    lv_obj_set_size(ui->screen_1_datetext_1, 148, 33);

    //Write style for screen_1_datetext_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_radius(ui->screen_1_datetext_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_datetext_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_datetext_1, 7, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_datetext_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_datetext_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_datetext_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_1_datetext_1, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_1_datetext_1, &lv_font_Alatsi_Regular_25, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_1_datetext_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_1_datetext_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);

    /* === C. 左右主卡片框架：左=室内，中=天气概览，右=室外 === */
    //Write codes screen_1_spangroup_2
    ui->screen_1_spangroup_2 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_2, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_2, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_2, LV_SPAN_MODE_FIXED);
    //create span
    lv_obj_set_pos(ui->screen_1_spangroup_2, 6, 50);
    lv_obj_set_size(ui->screen_1_spangroup_2, 135, 264);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_2_main_main_default
    static lv_style_t style_screen_1_spangroup_2_main_main_default;
    ui_init_style(&style_screen_1_spangroup_2_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_2_main_main_default, 2);
    lv_style_set_border_opa(&style_screen_1_spangroup_2_main_main_default, 255);
    lv_style_set_border_color(&style_screen_1_spangroup_2_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_border_side(&style_screen_1_spangroup_2_main_main_default, LV_BORDER_SIDE_FULL);
    lv_style_set_radius(&style_screen_1_spangroup_2_main_main_default, 6);
    lv_style_set_bg_opa(&style_screen_1_spangroup_2_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_2_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_2_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_2_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_2_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_2_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_2, &style_screen_1_spangroup_2_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_2);

    //Write codes screen_1_spangroup_3
    ui->screen_1_spangroup_3 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_3, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_3, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_3, LV_SPAN_MODE_FIXED);
    //create span
    ui->screen_1_spangroup_3_span = lv_spangroup_new_span(ui->screen_1_spangroup_3);
    lv_span_set_text(ui->screen_1_spangroup_3_span, "          室内");
    lv_style_set_text_color(&ui->screen_1_spangroup_3_span->style, lv_color_hex(0xffffff));
    lv_style_set_text_decor(&ui->screen_1_spangroup_3_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_3_span->style, &lv_font_SourceHanSerifSC_Regular_17);
    lv_obj_set_pos(ui->screen_1_spangroup_3, 6, 50);
    lv_obj_set_size(ui->screen_1_spangroup_3, 135, 29);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_3_main_main_default
    static lv_style_t style_screen_1_spangroup_3_main_main_default;
    ui_init_style(&style_screen_1_spangroup_3_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_3_main_main_default, 2);
    lv_style_set_border_opa(&style_screen_1_spangroup_3_main_main_default, 241);
    lv_style_set_border_color(&style_screen_1_spangroup_3_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_border_side(&style_screen_1_spangroup_3_main_main_default, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_radius(&style_screen_1_spangroup_3_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_3_main_main_default, 255);
    lv_style_set_bg_color(&style_screen_1_spangroup_3_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_bg_grad_dir(&style_screen_1_spangroup_3_main_main_default, LV_GRAD_DIR_NONE);
    lv_style_set_pad_top(&style_screen_1_spangroup_3_main_main_default, 3);
    lv_style_set_pad_right(&style_screen_1_spangroup_3_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_3_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_3_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_3_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_3, &style_screen_1_spangroup_3_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_3);

    //Write codes screen_1_spangroup_5
    ui->screen_1_spangroup_5 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_5, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_5, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_5, LV_SPAN_MODE_FIXED);
    //create span
    lv_obj_set_pos(ui->screen_1_spangroup_5, 341, 50);
    lv_obj_set_size(ui->screen_1_spangroup_5, 135, 264);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_5_main_main_default
    static lv_style_t style_screen_1_spangroup_5_main_main_default;
    ui_init_style(&style_screen_1_spangroup_5_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_5_main_main_default, 2);
    lv_style_set_border_opa(&style_screen_1_spangroup_5_main_main_default, 255);
    lv_style_set_border_color(&style_screen_1_spangroup_5_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_border_side(&style_screen_1_spangroup_5_main_main_default, LV_BORDER_SIDE_FULL);
    lv_style_set_radius(&style_screen_1_spangroup_5_main_main_default, 6);
    lv_style_set_bg_opa(&style_screen_1_spangroup_5_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_5_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_5_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_5_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_5_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_5_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_5, &style_screen_1_spangroup_5_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_5);

    //Write codes screen_1_spangroup_4
    ui->screen_1_spangroup_4 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_4, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_4, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_4, LV_SPAN_MODE_FIXED);
    //create span
    ui->screen_1_spangroup_4_span = lv_spangroup_new_span(ui->screen_1_spangroup_4);
    lv_span_set_text(ui->screen_1_spangroup_4_span, "          室外");
    lv_style_set_text_color(&ui->screen_1_spangroup_4_span->style, lv_color_hex(0xffffff));
    lv_style_set_text_decor(&ui->screen_1_spangroup_4_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_4_span->style, &lv_font_SourceHanSerifSC_Regular_17);
    lv_obj_set_pos(ui->screen_1_spangroup_4, 341, 50);
    lv_obj_set_size(ui->screen_1_spangroup_4, 135, 29);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_4_main_main_default
    static lv_style_t style_screen_1_spangroup_4_main_main_default;
    ui_init_style(&style_screen_1_spangroup_4_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_4_main_main_default, 2);
    lv_style_set_border_opa(&style_screen_1_spangroup_4_main_main_default, 241);
    lv_style_set_border_color(&style_screen_1_spangroup_4_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_border_side(&style_screen_1_spangroup_4_main_main_default, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_radius(&style_screen_1_spangroup_4_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_4_main_main_default, 255);
    lv_style_set_bg_color(&style_screen_1_spangroup_4_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_bg_grad_dir(&style_screen_1_spangroup_4_main_main_default, LV_GRAD_DIR_NONE);
    lv_style_set_pad_top(&style_screen_1_spangroup_4_main_main_default, 3);
    lv_style_set_pad_right(&style_screen_1_spangroup_4_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_4_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_4_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_4_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_4, &style_screen_1_spangroup_4_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_4);

    //Write codes screen_1_spangroup_6
    ui->screen_1_spangroup_6 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_6, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_6, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_6, LV_SPAN_MODE_FIXED);
    //create span
    lv_obj_set_pos(ui->screen_1_spangroup_6, 140, 51);
    lv_obj_set_size(ui->screen_1_spangroup_6, 203, 172);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_6_main_main_default
    static lv_style_t style_screen_1_spangroup_6_main_main_default;
    ui_init_style(&style_screen_1_spangroup_6_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_6_main_main_default, 2);
    lv_style_set_border_opa(&style_screen_1_spangroup_6_main_main_default, 255);
    lv_style_set_border_color(&style_screen_1_spangroup_6_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_border_side(&style_screen_1_spangroup_6_main_main_default, LV_BORDER_SIDE_FULL);
    lv_style_set_radius(&style_screen_1_spangroup_6_main_main_default, 6);
    lv_style_set_bg_opa(&style_screen_1_spangroup_6_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_6_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_6_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_6_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_6_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_6_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_6, &style_screen_1_spangroup_6_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_6);

    /* === D. 室内数据区：温度/湿度数字与图标 === */
    //Write codes screen_1_spangroup_7
    /* 室内温度主数字（大号字体） */
    ui->screen_1_spangroup_7 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_7, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_7, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_7, LV_SPAN_MODE_BREAK);
    //create span
    ui->screen_1_spangroup_7_span = lv_spangroup_new_span(ui->screen_1_spangroup_7);
    lv_span_set_text(ui->screen_1_spangroup_7_span, "98");
    lv_style_set_text_color(&ui->screen_1_spangroup_7_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_7_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_7_span->style, &lv_font_Alatsi_Regular_75);
    lv_obj_set_pos(ui->screen_1_spangroup_7, 10, 89);
    lv_obj_set_size(ui->screen_1_spangroup_7, 95, 95);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_7_main_main_default
    static lv_style_t style_screen_1_spangroup_7_main_main_default;
    ui_init_style(&style_screen_1_spangroup_7_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_style_set_radius(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_7_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_7, &style_screen_1_spangroup_7_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_7);

    //Write codes screen_1_img_3
    ui->screen_1_img_3 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_3, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_3, &_temperature_alpha_40x40);
    lv_img_set_pivot(ui->screen_1_img_3, 50,50);
    lv_img_set_angle(ui->screen_1_img_3, 0);
    lv_obj_set_pos(ui->screen_1_img_3, 101, 127);
    lv_obj_set_size(ui->screen_1_img_3, 40, 40);

    //Write style for screen_1_img_3, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_3, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_img_4
    ui->screen_1_img_4 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_4, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_4, &_Celsius_alpha_25x25);
    lv_img_set_pivot(ui->screen_1_img_4, 50,50);
    lv_img_set_angle(ui->screen_1_img_4, 0);
    lv_obj_set_pos(ui->screen_1_img_4, 104, 99);
    lv_obj_set_size(ui->screen_1_img_4, 25, 25);

    //Write style for screen_1_img_4, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_4, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_4, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_img_6
    ui->screen_1_img_6 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_6, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_6, &_humidity_alpha_30x30);
    lv_img_set_pivot(ui->screen_1_img_6, 50,50);
    lv_img_set_angle(ui->screen_1_img_6, 0);
    lv_obj_set_pos(ui->screen_1_img_6, 104, 256);
    lv_obj_set_size(ui->screen_1_img_6, 30, 30);

    //Write style for screen_1_img_6, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_6, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_6, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_img_5
    ui->screen_1_img_5 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_5, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_5, &_percentage_alpha_26x26);
    lv_img_set_pivot(ui->screen_1_img_5, 50,50);
    lv_img_set_angle(ui->screen_1_img_5, 0);
    lv_obj_set_pos(ui->screen_1_img_5, 104, 228);
    lv_obj_set_size(ui->screen_1_img_5, 26, 26);

    //Write style for screen_1_img_5, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_5, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_5, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_spangroup_8
    /* 室内湿度主数字（大号字体） */
    ui->screen_1_spangroup_8 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_8, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_8, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_8, LV_SPAN_MODE_BREAK);
    //create span
    ui->screen_1_spangroup_8_span = lv_spangroup_new_span(ui->screen_1_spangroup_8);
    lv_span_set_text(ui->screen_1_spangroup_8_span, "73");
    lv_style_set_text_color(&ui->screen_1_spangroup_8_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_8_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_8_span->style, &lv_font_Alatsi_Regular_75);
    lv_obj_set_pos(ui->screen_1_spangroup_8, 10, 217);
    lv_obj_set_size(ui->screen_1_spangroup_8, 95, 95);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_8_main_main_default
    static lv_style_t style_screen_1_spangroup_8_main_main_default;
    ui_init_style(&style_screen_1_spangroup_8_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_style_set_radius(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_8_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_8, &style_screen_1_spangroup_8_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_8);

    /* === E. 室外数据区：温度/湿度数字与图标 === */
    //Write codes screen_1_spangroup_9
    ui->screen_1_spangroup_9 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_9, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_9, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_9, LV_SPAN_MODE_FIXED);
    //create span
    lv_obj_set_pos(ui->screen_1_spangroup_9, 140, 224);
    lv_obj_set_size(ui->screen_1_spangroup_9, 203, 89);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_9_main_main_default
    static lv_style_t style_screen_1_spangroup_9_main_main_default;
    ui_init_style(&style_screen_1_spangroup_9_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_9_main_main_default, 2);
    lv_style_set_border_opa(&style_screen_1_spangroup_9_main_main_default, 255);
    lv_style_set_border_color(&style_screen_1_spangroup_9_main_main_default, lv_color_hex(0x6667ff));
    lv_style_set_border_side(&style_screen_1_spangroup_9_main_main_default, LV_BORDER_SIDE_FULL);
    lv_style_set_radius(&style_screen_1_spangroup_9_main_main_default, 6);
    lv_style_set_bg_opa(&style_screen_1_spangroup_9_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_9_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_9_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_9_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_9_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_9_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_9, &style_screen_1_spangroup_9_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_9);

    //Write codes screen_1_spangroup_11
    /* 室外湿度主数字（大号字体） */
    ui->screen_1_spangroup_11 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_11, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_11, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_11, LV_SPAN_MODE_BREAK);
    //create span
    ui->screen_1_spangroup_11_span = lv_spangroup_new_span(ui->screen_1_spangroup_11);
    lv_span_set_text(ui->screen_1_spangroup_11_span, "73");
    lv_style_set_text_color(&ui->screen_1_spangroup_11_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_11_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_11_span->style, &lv_font_Alatsi_Regular_75);
    lv_obj_set_pos(ui->screen_1_spangroup_11, 345, 221);
    lv_obj_set_size(ui->screen_1_spangroup_11, 95, 95);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_11_main_main_default
    static lv_style_t style_screen_1_spangroup_11_main_main_default;
    ui_init_style(&style_screen_1_spangroup_11_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_style_set_radius(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_11_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_11, &style_screen_1_spangroup_11_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_11);

    //Write codes screen_1_img_8
    ui->screen_1_img_8 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_8, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_8, &_temperature_alpha_40x40);
    lv_img_set_pivot(ui->screen_1_img_8, 50,50);
    lv_img_set_angle(ui->screen_1_img_8, 0);
    lv_obj_set_pos(ui->screen_1_img_8, 439, 121);
    lv_obj_set_size(ui->screen_1_img_8, 40, 40);

    //Write style for screen_1_img_8, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_8, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_8, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_8, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_8, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_img_7
    ui->screen_1_img_7 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_7, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_7, &_Celsius_alpha_25x25);
    lv_img_set_pivot(ui->screen_1_img_7, 50,50);
    lv_img_set_angle(ui->screen_1_img_7, 0);
    lv_obj_set_pos(ui->screen_1_img_7, 442, 94);
    lv_obj_set_size(ui->screen_1_img_7, 25, 25);

    //Write style for screen_1_img_7, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_7, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_7, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_spangroup_10
    /* 室外温度主数字（大号字体） */
    ui->screen_1_spangroup_10 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_10, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_10, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_10, LV_SPAN_MODE_BREAK);
    //create span
    ui->screen_1_spangroup_10_span = lv_spangroup_new_span(ui->screen_1_spangroup_10);
    lv_span_set_text(ui->screen_1_spangroup_10_span, "28");
    lv_style_set_text_color(&ui->screen_1_spangroup_10_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_10_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_10_span->style, &lv_font_Alatsi_Regular_75);
    lv_obj_set_pos(ui->screen_1_spangroup_10, 345, 88);
    lv_obj_set_size(ui->screen_1_spangroup_10, 95, 95);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_10_main_main_default
    static lv_style_t style_screen_1_spangroup_10_main_main_default;
    ui_init_style(&style_screen_1_spangroup_10_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_style_set_radius(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_10_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_10, &style_screen_1_spangroup_10_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_10);

    //Write codes screen_1_img_10
    ui->screen_1_img_10 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_10, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_10, &_humidity_alpha_30x30);
    lv_img_set_pivot(ui->screen_1_img_10, 50,50);
    lv_img_set_angle(ui->screen_1_img_10, 0);
    lv_obj_set_pos(ui->screen_1_img_10, 438, 262);
    lv_obj_set_size(ui->screen_1_img_10, 30, 30);

    //Write style for screen_1_img_10, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_10, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_10, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_10, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_10, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_img_9
    ui->screen_1_img_9 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_9, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_9, &_percentage_alpha_26x26);
    lv_img_set_pivot(ui->screen_1_img_9, 50,50);
    lv_img_set_angle(ui->screen_1_img_9, 0);
    lv_obj_set_pos(ui->screen_1_img_9, 442, 229);
    lv_obj_set_size(ui->screen_1_img_9, 26, 26);

    //Write style for screen_1_img_9, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_9, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_9, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_9, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_9, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_img_11
    ui->screen_1_img_11 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_11, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_11, &_Sunny_Cloudy_alpha_88x67);
    lv_img_set_pivot(ui->screen_1_img_11, 50,50);
    lv_img_set_angle(ui->screen_1_img_11, 0);
    lv_obj_set_pos(ui->screen_1_img_11, 195, 61);
    lv_obj_set_size(ui->screen_1_img_11, 88, 67);

    //Write style for screen_1_img_11, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_11, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_11, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_11, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_11, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    /* === F. 底部中间区域：当天低温/高温数字 === */
    //Write codes screen_1_spangroup_13
    /* 当天低温数字 */
    ui->screen_1_spangroup_13 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_13, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_13, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_13, LV_SPAN_MODE_BREAK);
    //create span
    ui->screen_1_spangroup_13_span = lv_spangroup_new_span(ui->screen_1_spangroup_13);
    lv_span_set_text(ui->screen_1_spangroup_13_span, "27");
    lv_style_set_text_color(&ui->screen_1_spangroup_13_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_13_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_13_span->style, &lv_font_Alatsi_Regular_50);
    lv_obj_set_pos(ui->screen_1_spangroup_13, 166, 244);
    lv_obj_set_size(ui->screen_1_spangroup_13, 70, 60);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_13_main_main_default
    static lv_style_t style_screen_1_spangroup_13_main_main_default;
    ui_init_style(&style_screen_1_spangroup_13_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_style_set_radius(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_13_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_13, &style_screen_1_spangroup_13_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_13);

    //Write codes screen_1_img_13
    ui->screen_1_img_13 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_13, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_13, &_Celsius_alpha_20x20);
    lv_img_set_pivot(ui->screen_1_img_13, 50,50);
    lv_img_set_angle(ui->screen_1_img_13, 0);
    lv_obj_set_pos(ui->screen_1_img_13, 217, 241);
    lv_obj_set_size(ui->screen_1_img_13, 20, 20);

    //Write style for screen_1_img_13, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_13, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_13, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_13, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_13, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_spangroup_14
    /* 当天高温数字 */
    ui->screen_1_spangroup_14 = lv_spangroup_create(ui->screen_1_cont_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_14, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_14, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_14, LV_SPAN_MODE_BREAK);
    //create span
    ui->screen_1_spangroup_14_span = lv_spangroup_new_span(ui->screen_1_spangroup_14);
    lv_span_set_text(ui->screen_1_spangroup_14_span, "33");
    lv_style_set_text_color(&ui->screen_1_spangroup_14_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_14_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_14_span->style, &lv_font_Alatsi_Regular_50);
    lv_obj_set_pos(ui->screen_1_spangroup_14, 246, 244);
    lv_obj_set_size(ui->screen_1_spangroup_14, 70, 60);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_14_main_main_default
    static lv_style_t style_screen_1_spangroup_14_main_main_default;
    ui_init_style(&style_screen_1_spangroup_14_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_style_set_radius(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_style_set_shadow_width(&style_screen_1_spangroup_14_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_14, &style_screen_1_spangroup_14_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_14);

    //Write codes screen_1_img_15
    ui->screen_1_img_15 = lv_img_create(ui->screen_1_cont_1);
    lv_obj_add_flag(ui->screen_1_img_15, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_15, &_Celsius_alpha_20x20);
    lv_img_set_pivot(ui->screen_1_img_15, 50,50);
    lv_img_set_angle(ui->screen_1_img_15, 0);
    lv_obj_set_pos(ui->screen_1_img_15, 295, 241);
    lv_obj_set_size(ui->screen_1_img_15, 20, 20);

    //Write style for screen_1_img_15, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_15, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_15, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_15, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_15, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_spangroup_12
    /* 星期文本（如 周日） */
    ui->screen_1_spangroup_12 = lv_spangroup_create(ui->screen_1);
    lv_spangroup_set_align(ui->screen_1_spangroup_12, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(ui->screen_1_spangroup_12, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(ui->screen_1_spangroup_12, LV_SPAN_MODE_BREAK);
    //create span
    ui->screen_1_spangroup_12_span = lv_spangroup_new_span(ui->screen_1_spangroup_12);
    lv_span_set_text(ui->screen_1_spangroup_12_span, "\xE6\x98\x9F\xE6\x9C\x9F\xE6\x97\xA5");
    lv_style_set_text_color(&ui->screen_1_spangroup_12_span->style, lv_color_hex(0x000000));
    lv_style_set_text_decor(&ui->screen_1_spangroup_12_span->style, LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(&ui->screen_1_spangroup_12_span->style, &lv_font_SourceHanSerifSC_Regular_30);
    lv_obj_set_pos(ui->screen_1_spangroup_12, 244, 10);
    lv_obj_set_size(ui->screen_1_spangroup_12, 103, 32);

    //Write style state: LV_STATE_DEFAULT for &style_screen_1_spangroup_12_main_main_default
    static lv_style_t style_screen_1_spangroup_12_main_main_default;
    ui_init_style(&style_screen_1_spangroup_12_main_main_default);

    lv_style_set_border_width(&style_screen_1_spangroup_12_main_main_default, 0);
    lv_style_set_radius(&style_screen_1_spangroup_12_main_main_default, 0);
    lv_style_set_bg_opa(&style_screen_1_spangroup_12_main_main_default, 0);
    lv_style_set_pad_top(&style_screen_1_spangroup_12_main_main_default, 0);
    lv_style_set_pad_right(&style_screen_1_spangroup_12_main_main_default, 2);
    lv_style_set_pad_bottom(&style_screen_1_spangroup_12_main_main_default, 0);
    lv_style_set_pad_left(&style_screen_1_spangroup_12_main_main_default, 8);
    lv_style_set_shadow_width(&style_screen_1_spangroup_12_main_main_default, 0);
    lv_obj_add_style(ui->screen_1_spangroup_12, &style_screen_1_spangroup_12_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_spangroup_refr_mode(ui->screen_1_spangroup_12);

    //Write codes screen_1_img_12
    ui->screen_1_img_12 = lv_img_create(ui->screen_1);
    lv_obj_add_flag(ui->screen_1_img_12, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_12, &_low_alpha_30x50);
    lv_img_set_pivot(ui->screen_1_img_12, 50,50);
    lv_img_set_angle(ui->screen_1_img_12, 0);
    lv_obj_set_pos(ui->screen_1_img_12, 142, 243);
    lv_obj_set_size(ui->screen_1_img_12, 30, 50);

    //Write style for screen_1_img_12, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_12, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_12, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_12, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_12, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_img_14
    ui->screen_1_img_14 = lv_img_create(ui->screen_1);
    lv_obj_add_flag(ui->screen_1_img_14, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_img_14, &_high_alpha_30x50);
    lv_img_set_pivot(ui->screen_1_img_14, 50,50);
    lv_img_set_angle(ui->screen_1_img_14, 0);
    lv_obj_set_pos(ui->screen_1_img_14, 313, 243);
    lv_obj_set_size(ui->screen_1_img_14, 30, 50);

    //Write style for screen_1_img_14, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_img_14, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_img_14, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_img_14, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_img_14, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    /*
     * 自定义扩展区（生成器预留）
     * 可在这里追加不易与上方自动生成片段混淆的逻辑。
     */
    //The custom code of screen_1.


    /* 强制更新一次布局，确保对象尺寸/对齐在首帧前生效 */
    //Update current screen layout.
    lv_obj_update_layout(ui->screen_1);

}
