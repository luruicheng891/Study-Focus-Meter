/**
  ******************************************************************************
  * @file    WeatherTask.c
  * @brief   LVGL 温湿度显示任务, 从 AHT20 队列读取数据并更新 UI
  ******************************************************************************
  */

#include "WeatherTask.h"
#include "SensorTask.h"
#include "AHT20.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lvgl.h"
#include <stdio.h>

/* LVGL UI 控件 */
static lv_obj_t *label_title = NULL;
static lv_obj_t *label_temp  = NULL;
static lv_obj_t *label_humi  = NULL;

/**
  * @brief  创建温湿度显示 UI
  * @note   在屏幕底部区域显示, 避免与摄像头画面重叠
  *         屏幕分辨率 240x320, 摄像头画面 240x240 居中
  *         底部剩余区域: y=240 ~ y=319 (80像素高)
  */
static void Weather_UI_Create(void)
{
    /* 标题 */
    label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(label_title, LV_SYMBOL_HOME " AHT20");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x00BFFF), 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_LEFT, 5, 245);

    /* 温度标签 */
    label_temp = lv_label_create(lv_scr_act());
    lv_label_set_text(label_temp, "T: --.- C");
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_temp, lv_color_hex(0xFF6347), 0);
    lv_obj_align(label_temp, LV_ALIGN_TOP_LEFT, 5, 265);

    /* 湿度标签 */
    label_humi = lv_label_create(lv_scr_act());
    lv_label_set_text(label_humi, "H: --.-%");
    lv_obj_set_style_text_font(label_humi, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_humi, lv_color_hex(0x32CD32), 0);
    lv_obj_align(label_humi, LV_ALIGN_TOP_LEFT, 5, 290);
}

/**
  * @brief  更新温湿度显示
  * @param  temp_x100: 温度值, 单位 0.01°C (如 2500 = 25.00°C)
  * @param  humi_x100: 湿度值, 单位 0.01% (如 5678 = 56.78%)
  */
static void Weather_UI_Update(int32_t temp_x100, uint32_t humi_x100)
{
    char buf[32];

    /* 温度: 保留1位小数 */
    int32_t temp_int = temp_x100 / 100;
    int32_t temp_dec = (temp_x100 >= 0) ? (temp_x100 % 100) / 10 : ((-temp_x100) % 100) / 10;
    sprintf(buf, "T: %d.%d C", (int)temp_int, (int)temp_dec);
    lv_label_set_text(label_temp, buf);

    /* 湿度: 保留1位小数 */
    uint32_t humi_int = humi_x100 / 100;
    uint32_t humi_dec = (humi_x100 % 100) / 10;
    sprintf(buf, "H: %u.%u%%", (unsigned int)humi_int, (unsigned int)humi_dec);
    lv_label_set_text(label_humi, buf);
}

/**
  * @brief  温湿度显示任务
  * @note   从 xAHT20DataQueue 队列接收数据, 更新 LVGL 标签
  *         注意: LVGL API 非线程安全, 但本项目中 LVGL_Task 只调用
  *         lv_timer_handler(), 而 UI 更新在此任务中进行.
  *         如果出现显示异常, 可考虑加 LVGL 互斥锁.
  */
void Weather_Task(void *pvParameters)
{
    AHT20_Data_t sensor_data;

    (void)pvParameters;

    /* 等待 AHT20 任务创建队列 (最多等 2 秒) */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 创建 UI 控件 */
    Weather_UI_Create();

    for(;;)
    {
        /* 从队列接收数据, 最多等待 2 秒 */
        if(xQueueReceive(xAHT20DataQueue, &sensor_data, pdMS_TO_TICKS(2000)) == pdTRUE)
        {
            Weather_UI_Update(sensor_data.temperature, sensor_data.humidity);
        }
    }
}
