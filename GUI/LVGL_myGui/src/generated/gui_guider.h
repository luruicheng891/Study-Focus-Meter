/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef GUI_GUIDER_H
#define GUI_GUIDER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef struct
{
  
	lv_obj_t *screen_1;
	bool screen_1_del;
	lv_obj_t *screen_1_img_1;
	lv_obj_t *screen_1_cont_1;
	lv_obj_t *screen_1_spangroup_1;
	lv_span_t *screen_1_spangroup_1_span;
	lv_obj_t *screen_1_img_2;
	lv_obj_t *screen_1_digital_clock_1;
	lv_obj_t *screen_1_datetext_1;
	lv_obj_t *screen_1_spangroup_2;
	lv_span_t *screen_1_spangroup_2_span;
	lv_obj_t *screen_1_spangroup_3;
	lv_span_t *screen_1_spangroup_3_span;
	lv_obj_t *screen_1_spangroup_5;
	lv_span_t *screen_1_spangroup_5_span;
	lv_obj_t *screen_1_spangroup_4;
	lv_span_t *screen_1_spangroup_4_span;
	lv_obj_t *screen_1_spangroup_6;
	lv_span_t *screen_1_spangroup_6_span;
	lv_obj_t *screen_1_spangroup_7;
	lv_span_t *screen_1_spangroup_7_span;
	lv_obj_t *screen_1_img_3;
	lv_obj_t *screen_1_img_4;
	lv_obj_t *screen_1_img_6;
	lv_obj_t *screen_1_img_5;
	lv_obj_t *screen_1_spangroup_8;
	lv_span_t *screen_1_spangroup_8_span;
	lv_obj_t *screen_1_spangroup_9;
	lv_span_t *screen_1_spangroup_9_span;
	lv_obj_t *screen_1_spangroup_11;
	lv_span_t *screen_1_spangroup_11_span;
	lv_obj_t *screen_1_img_8;
	lv_obj_t *screen_1_img_7;
	lv_obj_t *screen_1_spangroup_10;
	lv_span_t *screen_1_spangroup_10_span;
	lv_obj_t *screen_1_img_10;
	lv_obj_t *screen_1_img_9;
	lv_obj_t *screen_1_img_11;
	lv_obj_t *screen_1_spangroup_13;
	lv_span_t *screen_1_spangroup_13_span;
	lv_obj_t *screen_1_img_13;
	lv_obj_t *screen_1_spangroup_14;
	lv_span_t *screen_1_spangroup_14_span;
	lv_obj_t *screen_1_img_15;
	lv_obj_t *screen_1_spangroup_12;
	lv_span_t *screen_1_spangroup_12_span;
	lv_obj_t *screen_1_img_12;
	lv_obj_t *screen_1_img_14;
}lv_ui;

typedef void (*ui_setup_scr_t)(lv_ui * ui);

void ui_init_style(lv_style_t * style);

void ui_load_scr_animation(lv_ui *ui, lv_obj_t ** new_scr, bool new_scr_del, bool * old_scr_del, ui_setup_scr_t setup_scr,
                           lv_scr_load_anim_t anim_type, uint32_t time, uint32_t delay, bool is_clean, bool auto_del);

void ui_animation(void * var, int32_t duration, int32_t delay, int32_t start_value, int32_t end_value, lv_anim_path_cb_t path_cb,
                       uint16_t repeat_cnt, uint32_t repeat_delay, uint32_t playback_time, uint32_t playback_delay,
                       lv_anim_exec_xcb_t exec_cb, lv_anim_start_cb_t start_cb, lv_anim_ready_cb_t ready_cb, lv_anim_deleted_cb_t deleted_cb);


void init_scr_del_flag(lv_ui *ui);

void setup_ui(lv_ui *ui);

void init_keyboard(lv_ui *ui);

extern lv_ui guider_ui;


void setup_scr_screen_1(lv_ui *ui);
LV_IMG_DECLARE(_WIFI_alpha_20x20);
LV_IMG_DECLARE(_temperature_alpha_40x40);
LV_IMG_DECLARE(_Celsius_alpha_25x25);
LV_IMG_DECLARE(_humidity_alpha_30x30);
LV_IMG_DECLARE(_percentage_alpha_26x26);
LV_IMG_DECLARE(_temperature_alpha_40x40);
LV_IMG_DECLARE(_Celsius_alpha_25x25);
LV_IMG_DECLARE(_humidity_alpha_30x30);
LV_IMG_DECLARE(_percentage_alpha_26x26);
LV_IMG_DECLARE(_Sunny_Cloudy_alpha_88x67);
LV_IMG_DECLARE(_Celsius_alpha_10x10);
LV_IMG_DECLARE(_Celsius_alpha_10x10);
LV_IMG_DECLARE(_low_alpha_20x20);
LV_IMG_DECLARE(_high_alpha_20x20);
LV_IMG_DECLARE(_low_alpha_30x50);
LV_IMG_DECLARE(_Background00_alpha_480x320);
LV_IMG_DECLARE(_Celsius_alpha_20x20);
LV_IMG_DECLARE(_high_alpha_30x50);

LV_FONT_DECLARE(lv_font_SourceHanSerifSC_Regular_12)
LV_FONT_DECLARE(lv_font_Antonio_Regular_25)
LV_FONT_DECLARE(lv_font_Alatsi_Regular_25)
LV_FONT_DECLARE(lv_font_SourceHanSerifSC_Regular_17)
LV_FONT_DECLARE(lv_font_Alatsi_Regular_30)
LV_FONT_DECLARE(lv_font_Alatsi_Regular_75)
LV_FONT_DECLARE(lv_font_Alatsi_Regular_50)
LV_FONT_DECLARE(lv_font_SourceHanSerifSC_Regular_30)

#ifdef __cplusplus
}
#endif
#endif
