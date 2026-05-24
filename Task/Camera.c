/**
  ******************************************************************************
  * @file    Camera.c
  * @brief   摄像头采集任务, 使用 LVGL canvas 显示 OV2640 画面
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "task.h"
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
 * 与 LCD DMA buffer (0x24040000) 不冲突
 */
#define Canvas_Buffer       ((uint16_t *)0x2401C200)
#define Frame_Bytes         (Display_Width * Display_Height * 2)

/* LVGL canvas 对象 */
static lv_obj_t *cam_canvas = NULL;

/**
  * @brief  摄像头采集任务 (FreeRTOS)
  * @note   使用 LVGL canvas 控件显示摄像头画面
  *         OV2640 + DCMI 输出的数据已经是标准 RGB565 LE 格式,
  *         直接 memcpy 到 canvas buffer 即可, 无需 byte swap
  */
void Camera_task(void *argument)
{
    LED_Init();

    /* 初始化 OV2640 摄像头 (DCMI + SCCB) */
    DCMI_OV2640_Init();

    /* 创建 LVGL canvas, 使用独立 canvas buffer (避免 LVGL 渲染时被 DMA 覆盖) */
    cam_canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(cam_canvas, (void *)Canvas_Buffer,
                         Display_Width, Display_Height, LV_IMG_CF_TRUE_COLOR);
    /* 居中显示 */
    lv_obj_center(cam_canvas);

    /* 启动 DMA 连续采集 */
    OV2640_DMA_Transmit_Continuous(Camera_Buffer, OV2640_BufferSize);

    while(1)
    {
        if (DCMI_FrameState == 1)
        {
            DCMI_FrameState = 0;

            /* 使 DCache 失效, 确保 CPU 读取到 DMA 写入的最新数据 */
            SCB_InvalidateDCache_by_Addr((uint32_t *)Camera_Buffer, Frame_Bytes);

            /* 直接拷贝, 无需字节序转换
             * (DCMI + DMA 输出已经是 LE 标准 RGB565 格式) */
            memcpy((void *)Canvas_Buffer, (void *)Camera_Buffer, Frame_Bytes);

            /* 通知 LVGL canvas 数据已更新, 触发重绘 */
            lv_obj_invalidate(cam_canvas);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
