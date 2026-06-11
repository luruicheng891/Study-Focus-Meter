/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include "gui_guider.h"
#include "widgets_init.h"
#include "custom.h"
#include <stdlib.h>
#include <string.h>

/*
 * 文件说明：
 * 1) 本文件主要放“页面小部件行为逻辑”，例如键盘事件、时钟定时更新、日期选择器。
 * 2) 这些函数通常由 setup_scr_screen_1.c / events_init.c 进行注册和调用。
 * 3) 业务数据更新建议：
 *    - 时钟：由 screen_1_digital_clock_1_timer 周期刷新
 *    - 日期：由 screen_1_datetext_1_event_handler + 日历回调更新
 */

/*
 * 键盘事件回调（可选）
 * - 当键盘输入完成（READY）或取消（CANCEL）时，隐藏键盘对象。
 * - 该函数使用 __attribute__((unused))，即便当前未绑定也不会报警。
 */
__attribute__((unused)) void kb_event_cb (lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * 文本框事件回调（可选）
 * - 当文本框获得焦点/点击时：将键盘关联到该文本框并显示。
 * - 当取消/失焦时：隐藏键盘。
 * - 兼容普通键盘与中文键盘（受 LV_USE_KEYBOARD / LV_USE_ZH_KEYBOARD 控制）。
 */
__attribute__((unused)) void ta_event_cb (lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
#if LV_USE_KEYBOARD || LV_USE_ZH_KEYBOARD
    lv_obj_t *ta = lv_event_get_target(e);
#endif
    lv_obj_t *kb = lv_event_get_user_data(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED)
    {
#if LV_USE_ZH_KEYBOARD != 0
        lv_zh_keyboard_set_textarea(kb, ta);
#endif
#if LV_USE_KEYBOARD != 0
        lv_keyboard_set_textarea(kb, ta);
#endif
        lv_obj_move_foreground(kb);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED)
    {

#if LV_USE_ZH_KEYBOARD != 0
        lv_zh_keyboard_set_textarea(kb, ta);
#endif
#if LV_USE_KEYBOARD != 0
        lv_keyboard_set_textarea(kb, ta);
#endif
        lv_obj_move_background(kb);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

#if LV_USE_ANALOGCLOCK != 0
/*
 * 指针钟表的计时累加器（仅在启用 ANALOGCLOCK 时编译）
 * 每调用一次推进 1 秒，处理分/时进位。
 */
void clock_count(int *hour, int *min, int *sec)
{
    (*sec)++;
    if(*sec == 60)
    {
        *sec = 0;
        (*min)++;
    }
    if(*min == 60)
    {
        *min = 0;
        if(*hour < 12)
        {
            (*hour)++;
        } else {
            (*hour)++;
            *hour = *hour %12;
        }
    }
}
#endif


/*
 * 这些变量定义在 setup_scr_screen_1.c 中，作为数字时钟当前状态。
 * 这里通过 extern 引用，以便定时器函数读取并更新显示。
 */
extern int screen_1_digital_clock_1_hour_value;
extern int screen_1_digital_clock_1_min_value;
extern int screen_1_digital_clock_1_sec_value;
extern char screen_1_digital_clock_1_meridiem[];

/*
 * 数字时钟定时器回调（每 1s 调用一次）
 * 流程：
 * 1) 调用 custom.c 的 clock_count_12() 推进时间；
 * 2) 检查时钟对象是否仍然有效；
 * 3) 通过 lv_dclock_set_text_fmt() 刷新到界面。
 */
void screen_1_digital_clock_1_timer(lv_timer_t *timer)
{
    clock_count_12(&screen_1_digital_clock_1_hour_value, &screen_1_digital_clock_1_min_value, &screen_1_digital_clock_1_sec_value, screen_1_digital_clock_1_meridiem);
    if (lv_obj_is_valid(guider_ui.screen_1_digital_clock_1))
    {
        lv_dclock_set_text_fmt(guider_ui.screen_1_digital_clock_1, "%d:%02d %s", screen_1_digital_clock_1_hour_value, screen_1_digital_clock_1_min_value, screen_1_digital_clock_1_meridiem);
    }
}

/*
 * 日期弹窗（日历）对象指针
 * - NULL 表示未创建；
 * - 非 NULL 表示当前已有弹窗，避免重复创建。
 */
static lv_obj_t * screen_1_datetext_1_calendar;

/*
 * 日期文本事件处理
 * - 当日期标签获得焦点时，创建并显示日历弹窗。
 */
void screen_1_datetext_1_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        char * s = lv_label_get_text(btn);
        if(screen_1_datetext_1_calendar == NULL) {
            screen_1_datetext_1_init_calendar(btn, s);
        }
    }
}

/*
 * 创建并初始化日历弹窗
 * 参数：
 * - obj: 触发该行为的日期标签对象
 * - s:   日期字符串，格式 "YYYY/MM/DD"
 *
 * 逻辑：
 * 1) 在顶层 layer 创建日历；
 * 2) 解析当前日期并设置显示月份/高亮日期；
 * 3) 绑定日历事件回调；
 * 4) 创建箭头头部（上一月/下一月）。
 */
void screen_1_datetext_1_init_calendar(lv_obj_t *obj, char * s)
{
    if (screen_1_datetext_1_calendar == NULL) {
        lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
        screen_1_datetext_1_calendar = lv_calendar_create(lv_layer_top());
        lv_obj_t * scr = lv_obj_get_screen(obj);
        lv_coord_t scr_height = lv_obj_get_height(scr);
        lv_coord_t scr_width = lv_obj_get_width(scr);
        lv_obj_set_size(screen_1_datetext_1_calendar, scr_width * 0.8, scr_height * 0.8);

        /*
         * 从 "YYYY/MM/DD" 解析年月日
         * 注意：strtok 会修改原字符串内容，这里沿用生成代码默认实现。
         */
        char * year = strtok(s, "/");
        char * month = strtok(NULL, "/");
        char * day = strtok(NULL, "/");
        lv_calendar_set_showed_date(screen_1_datetext_1_calendar, atoi(year), atoi(month));
        lv_calendar_date_t highlighted_days[1];       /*Only its pointer will be saved so should be static*/
        highlighted_days[0].year = atoi(year);
        highlighted_days[0].month = atoi(month);
        highlighted_days[0].day = atoi(day);
        lv_calendar_set_highlighted_dates(screen_1_datetext_1_calendar, highlighted_days, 1);
        lv_obj_align(screen_1_datetext_1_calendar,LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_event_cb(screen_1_datetext_1_calendar, screen_1_datetext_1_calendar_event_handler, LV_EVENT_ALL,NULL);
        lv_calendar_header_arrow_create(screen_1_datetext_1_calendar);
        lv_obj_update_layout(scr);
    }
}

/*
 * 日历事件回调
 * - 用户选中日期后：
 *   1) 读取被点击日期
 *   2) 格式化为 YYYY/MM/DD 写回日期标签
 *   3) 关闭并销毁日历弹窗
 */
void screen_1_datetext_1_calendar_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_current_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_calendar_date_t date;
        lv_calendar_get_pressed_date(obj,&date);
        char buf[16];
        lv_snprintf(buf,sizeof(buf),"%d/%02d/%02d", date.year, date.month,date.day);
        lv_label_set_text(guider_ui.screen_1_datetext_1, buf);
        lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
        lv_obj_del(screen_1_datetext_1_calendar);
        screen_1_datetext_1_calendar = NULL;
    }
}


