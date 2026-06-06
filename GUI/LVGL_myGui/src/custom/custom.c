/*
* Copyright 2023 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

/*********************
 *      INCLUDES
 *********************/
/* 标准输入输出：用于 vsnprintf 格式化字符串 */
#include <stdio.h>
/* 可变参数支持：用于实现类似 printf 的接口 */
#include <stdarg.h>
/* LVGL 主头文件 */
#include "lvgl.h"
/* 本模块头文件（声明自定义控件与辅助函数） */
#include "custom.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/*
 * 星期字符串表（UTF-8）
 * 说明：
 * 1) 这里使用十六进制 UTF-8 字节序列，避免不同编辑器编码导致中文乱码。
 * 2) 下标约定：0~6 分别表示周一~周日。
 */
const char * week_days[7] = {
    "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xB8\x80", /* 星期一 */
    "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xBA\x8C", /* 星期二 */
    "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xB8\x89", /* 星期三 */
    "\xE6\x98\x9F\xE6\x9C\x9F\xE5\x9B\x9B", /* 星期四 */
    "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xBA\x94", /* 星期五 */
    "\xE6\x98\x9F\xE6\x9C\x9F\xE5\x85\xAD", /* 星期六 */
    "\xE6\x98\x9F\xE6\x9C\x9F\xE6\x97\xA5"  /* 星期日 */
};

/*
 * 自定义初始化入口
 * 调用时机：UI 创建完成后，可在这里挂接自定义事件、动画或初始化业务状态。
 * 参数：
 * - ui: 由 Gui Guider 生成的全局 UI 结构体指针。
 * 当前实现：仅保留扩展点，不做额外逻辑。
 */

void custom_init(lv_ui *ui)
{
    /* 避免未使用参数在某些编译器配置下产生告警 */
    (void)ui;

    /* Add your codes here */
    /* 如果您未来需要添加自定义动画或逻辑，请在此处编写 */
}

/*
 * 创建“数字时钟”对象（当前使用 lv_label 进行轻量实现）
 * 参数：
 * - parent: 父对象，时钟控件将挂在该对象下。
 * - input_time: 初始显示字符串，例如 "11:25 AM"。
 * 返回：
 * - 创建成功返回 label 对象指针。
 * 说明：
 * - 这里没有引入额外自定义类，直接复用 LVGL label，资源占用更小。
 */
lv_obj_t * lv_dclock_create(lv_obj_t * parent, const char * input_time)
{
    /* 创建用于显示时钟文本的标签 */
    lv_obj_t *label = lv_label_create(parent);

    /* 如果外部给了初始时间则使用外部值，否则使用默认值 */
    if(input_time != NULL) {
        lv_label_set_text(label, input_time);
    } else {
        lv_label_set_text(label, "12:00 AM");
    }

    /* 返回创建出的时钟对象 */
    return label;
}

/*
 * 按格式更新数字时钟文本
 * 参数：
 * - obj: 时钟对象（本实现中应为 label）。
 * - fmt: 格式化字符串，例如 "%d:%02d %s"。
 * - ...: 与 fmt 对应的可变参数。
 * 说明：
 * - 使用固定长度缓冲区（32 字节）控制栈占用。
 * - 若格式化结果超出 31 字符，vsnprintf 会自动截断并保证结尾 '\0'。
 */
void lv_dclock_set_text_fmt(lv_obj_t * obj, const char * fmt, ...)
{
    /* 本地格式化缓存：足够容纳常见时间字符串，如 "12:59 PM" */
    char buf[32];

    /* 启动可变参数读取 */
    va_list args;
    va_start(args, fmt);

    /* 格式化输出到缓冲区 */
    vsnprintf(buf, sizeof(buf), fmt, args);

    /* 结束可变参数读取 */
    va_end(args);

    /* 刷新到 LVGL 标签对象 */
    lv_label_set_text(obj, buf);
}

/*
 * 12 小时制时钟累加器（每调用一次，时间前进 1 秒）
 * 参数：
 * - hour/min/sec: 当前时分秒（原地修改）。
 * - meridiem: "AM" / "PM" 字符串缓冲区（至少 3 字节，含 '\0'）。
 * 逻辑：
 * 1) 秒进位到分；分进位到时。
 * 2) 时到 12 时切换 AM/PM。
 * 3) 时超过 12 回到 1。
 */
void clock_count_12(int *hour, int *min, int *sec, char *meridiem)
{
    /* 参数保护：任何指针为空都直接返回，避免非法访问 */
    if(hour == NULL || min == NULL || sec == NULL || meridiem == NULL) {
        return;
    }

    /* 秒 +1 */
    (*sec)++;

    /* 秒满 60，清零并给分钟进位 */
    if(*sec >= 60) {
        *sec = 0;
        (*min)++;
    }

    /* 分钟满 60，清零并给小时进位 */
    if(*min >= 60) {
        *min = 0;
        (*hour)++;

        /*
         * 小时到 12 时，切换 AM/PM
         * 示例：11:59:59 AM -> 12:00:00 PM
         */
        if(*hour == 12) {
            if(meridiem[0] == 'A' && meridiem[1] == 'M') {
                meridiem[0] = 'P';
                meridiem[1] = 'M';
            } else {
                meridiem[0] = 'A';
                meridiem[1] = 'M';
            }

            /* 明确补上字符串结束符，保证后续显示安全 */
            meridiem[2] = '\0';

        /* 超过 12 点后回到 1 点（12 小时制） */
        } else if(*hour > 12) {
            *hour = 1;
        }
    }
}