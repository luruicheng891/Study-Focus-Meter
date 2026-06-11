/**
  ******************************************************************************
  * @file    Camera.c
  * @brief   摄像头采集任务, 使用 LVGL canvas 显示 OV2640 画面
  *          画面显示/隐藏由 display_mode 模块通过 LVGL hidden flag 控制,
  *          DCMI/DMA 始终连续运行 (避免重启硬件采集的代价)
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "task.h"
#include "Camera.h"
#include "AITask.h"
#include "display_mode.h"
#include "lcd.h"
#include "dcmi_ov2640.h"
#include "led.h"
#include "lvgl.h"
#include <string.h>

/* 摄像头 DMA 帧缓冲区地址 (AXI SRAM 起始) */
#define Camera_Buffer       ((uint32_t)0x24000000)

/* LVGL canvas 像素缓冲区 (位于 AXI SRAM, 紧跟摄像头 buffer 之后)
 * 摄像头 buffer 大小: 240*240*2 = 115200 bytes = 0x1C200
 * canvas buffer 起始: 0x24000000 + 0x1C200 = 0x2401C200
 */
#define Canvas_Buffer       ((uint16_t *)0x2401C200)
#define Frame_Bytes         (Display_Width * Display_Height * 2)

/* LVGL canvas 全局对象 (供 Weather_Task 控制 hidden) */
lv_obj_t *g_cam_canvas = NULL;

/*============================================================================
 *  LVGL 按钮: 切换到天气仪表板
 *  放在摄像头画布右侧空白区, 不影响画面
 *============================================================================*/
static void cam_switch_btn_event_cb(lv_event_t *e)
{
    /* 仅在非天气模式时才请求切换, 避免重复触发 */
    if(g_display_mode != DISP_MODE_WEATHER) {
        DisplayMode_Request(DISP_MODE_WEATHER);
    }
}

static void cam_create_switch_button(lv_obj_t *parent)
{
    /* 按钮放在屏幕右下角 (cam 画布 240x240 居中, 右边有 80px 空白, 下方 0) */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 60, 30);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_add_event_cb(btn, cam_switch_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* 按钮样式: 紫色背景, 圆角 */
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x6667FF), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x4040CC), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    /* 按钮标签 */
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Weather");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
}

/**
  * @brief  摄像头采集任务 (FreeRTOS)
  */
void Camera_task(void *argument)
{
    LED_Init();

    /* 初始化 OV2640 摄像头 (DCMI + SCCB) */
    DCMI_OV2640_Init();

    /* 创建 LVGL canvas, 横屏居中 (320x240 屏幕, canvas 240x240, 左右各 40px 留白) */
    g_cam_canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(g_cam_canvas, (void *)Canvas_Buffer,
                         Display_Width, Display_Height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(g_cam_canvas);

    /* 添加"切到天气"按钮 */
    cam_create_switch_button(lv_scr_act());

    /* 启动 DMA 连续采集 */
    OV2640_DMA_Transmit_Continuous(Camera_Buffer, OV2640_BufferSize);

    while(1)
    {
        if (DCMI_FrameState == 1)
        {
            DCMI_FrameState = 0;

            /* 失效 DCache, 让 CPU 读到 DMA 写入的最新帧 */
            SCB_InvalidateDCache_by_Addr((uint32_t *)Camera_Buffer, Frame_Bytes);

            /* 仅在摄像头模式下拷贝并触发重绘, 减少不必要的 LVGL 工作量 */
            if(g_display_mode == DISP_MODE_CAMERA)
            {
                memcpy((void *)Canvas_Buffer, (void *)Camera_Buffer, Frame_Bytes);
                lv_obj_invalidate(g_cam_canvas);

                /* 通知 AI 推理任务: 有新帧可处理 (非阻塞) */
                if (AI_TaskHandle != NULL) {
                    xTaskNotifyGive(AI_TaskHandle);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
