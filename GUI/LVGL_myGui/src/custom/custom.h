/*
* Copyright 2023 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/


#ifndef __CUSTOM_H_
#define __CUSTOM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"

void custom_init(lv_ui *ui);
lv_obj_t * lv_dclock_create(lv_obj_t * parent, const char * input_time);
void lv_dclock_set_text_fmt(lv_obj_t * obj, const char * fmt, ...);
void clock_count_12(int *hour, int *min, int *sec, char *meridiem);

extern const char * week_days[7];

#ifdef __cplusplus
}
#endif
#endif /* EVENT_CB_H_ */
