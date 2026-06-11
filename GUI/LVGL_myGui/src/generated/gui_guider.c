/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

/*
 * 文件说明：
 * 1) 本文件由 Gui Guider 生成，负责 UI 的基础初始化与页面切换辅助函数。
 * 2) 这里通常包含样式初始化、页面加载动画、通用动画封装等“框架层”逻辑。
 * 3) 业务逻辑建议放在 custom.c / 业务模块中，避免重新生成 UI 时被覆盖。
 */

/* LVGL 主头文件：对象、样式、动画、屏幕切换 API 都在这里 */
#include "lvgl.h"

/* 标准输入输出头：本文件当前未直接使用，保留为生成模板默认内容 */
#include <stdio.h>

/* Gui Guider 生成的 UI 结构体与函数声明 */
#include "gui_guider.h"

/* 小部件辅助函数声明（键盘、日期等扩展组件） */
#include "widgets_init.h"

/*
 * 仅在“仿真器 + FreeMaster”联调场景启用。
 * 板级工程通常不会进入该分支，从而避免不必要依赖。
 */
#if LV_USE_GUIDER_SIMULATOR && LV_USE_FREEMASTER
#include "gg_external_data.h"
#endif

/*
 * 函数：ui_init_style
 * 作用：初始化或重置样式对象。
 * 参数：
 * - style: 需要初始化的样式结构体指针。
 * 说明：
 * - 如果样式对象此前已经设置过属性（prop_cnt > 1），先 reset 再复用；
 * - 否则直接 init。
 */
void ui_init_style(lv_style_t * style)
{
    if (style->prop_cnt > 1)
        lv_style_reset(style);
    else
        lv_style_init(style);
}

/*
 * 函数：ui_load_scr_animation
 * 作用：带动画地切换页面，并处理旧页面清理策略。
 * 关键参数：
 * - new_scr: 目标页面对象地址（双指针，用于通用切页）。
 * - new_scr_del: 是否需要先执行 setup_scr() 重新创建目标页面。
 * - old_scr_del: 输出参数，返回旧页面是否会被自动删除。
 * - setup_scr: 目标页面创建函数。
 * - anim_type/time/delay: 切换动画参数。
 * - is_clean: 是否先清空当前页面对象。
 * - auto_del: 切换后是否自动删除旧页面。
 */
void ui_load_scr_animation(lv_ui *ui, lv_obj_t ** new_scr, bool new_scr_del, bool * old_scr_del, ui_setup_scr_t setup_scr,
                           lv_scr_load_anim_t anim_type, uint32_t time, uint32_t delay, bool is_clean, bool auto_del)
{
    /* 获取当前活动页面 */
    lv_obj_t * act_scr = lv_scr_act();

#if LV_USE_GUIDER_SIMULATOR && LV_USE_FREEMASTER
    /* 仿真场景下，若自动删除旧页，清理与该页关联的外部数据任务 */
    if(auto_del) {
        gg_edata_task_clear(act_scr);
    }
#endif

    /* 如需清空当前页内容，则先清理对象树 */
    if (auto_del && is_clean) {
        lv_obj_clean(act_scr);
    }

    /* 如需重建目标页，则调用对应 setup 函数 */
    if (new_scr_del) {
        setup_scr(ui);
    }

    /* 执行页面动画切换 */
    lv_scr_load_anim(*new_scr, anim_type, time, delay, auto_del);

    /* 反馈旧页删除策略给调用方 */
    *old_scr_del = auto_del;
}

/*
 * 函数：ui_animation
 * 作用：对 LVGL 动画 API 的统一封装，便于可视化工具生成调用。
 * 说明：
 * - 支持起止值、时长、延时、路径函数、重复/回放参数；
 * - 可选注册开始/完成/删除回调。
 */
void ui_animation(void * var, int32_t duration, int32_t delay, int32_t start_value, int32_t end_value, lv_anim_path_cb_t path_cb,
                  uint16_t repeat_cnt, uint32_t repeat_delay, uint32_t playback_time, uint32_t playback_delay,
                  lv_anim_exec_xcb_t exec_cb, lv_anim_start_cb_t start_cb, lv_anim_ready_cb_t ready_cb, lv_anim_deleted_cb_t deleted_cb)
{
    /* 动画对象在栈上构造，启动后由 LVGL 内部管理 */
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, var);
    lv_anim_set_exec_cb(&anim, exec_cb);
    lv_anim_set_values(&anim, start_value, end_value);
    lv_anim_set_time(&anim, duration);
    lv_anim_set_delay(&anim, delay);
    lv_anim_set_path_cb(&anim, path_cb);
    lv_anim_set_repeat_count(&anim, repeat_cnt);
    lv_anim_set_repeat_delay(&anim, repeat_delay);
    lv_anim_set_playback_time(&anim, playback_time);
    lv_anim_set_playback_delay(&anim, playback_delay);
    if (start_cb) {
        lv_anim_set_start_cb(&anim, start_cb);
    }
    if (ready_cb) {
        lv_anim_set_ready_cb(&anim, ready_cb);
    }
    if (deleted_cb) {
        lv_anim_set_deleted_cb(&anim, deleted_cb);
    }

    /* 提交动画到 LVGL 动画系统 */
    lv_anim_start(&anim);
}

/*
 * 函数：init_scr_del_flag
 * 作用：初始化页面删除标志位。
 * 说明：
 * - `screen_1_del = true` 表示 screen_1 初始状态按“可删除重建”策略管理。
 */
void init_scr_del_flag(lv_ui *ui)
{

    ui->screen_1_del = true;
}

/*
 * 函数：setup_ui
 * 作用：UI 总入口，按固定顺序完成页面初始化并加载首屏。
 * 顺序说明：
 * 1) 初始化删除标志；
 * 2) 初始化键盘等通用输入组件；
 * 3) 创建首屏对象；
 * 4) 将首屏加载为当前活动屏幕。
 */
void setup_ui(lv_ui *ui)
{
    init_scr_del_flag(ui);
    init_keyboard(ui);
    setup_scr_screen_1(ui);
    lv_scr_load(ui->screen_1);
}

/*
 * 函数：init_keyboard
 * 作用：键盘相关组件初始化扩展点。
 * 当前：空实现（项目暂未启用屏幕键盘）。
 * 若后续启用 `lv_keyboard` / 中文输入法，可在此集中初始化。
 */
void init_keyboard(lv_ui *ui)
{

    /* 当前未使用参数，防止产生编译告警 */
    (void)ui;

}
