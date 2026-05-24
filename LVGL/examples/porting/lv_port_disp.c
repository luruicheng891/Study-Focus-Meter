/**
 * @file lv_port_disp.c
 * @brief LVGL display porting for ILI9341 (SPI + DMA)
 */

#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include "lcd.h"

/*********************
 *      DEFINES
 *********************/
#define MY_DISP_HOR_RES    240
#define MY_DISP_VER_RES    320

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    disp_init();

    /* 单缓冲, 10行 */
    static lv_disp_draw_buf_t draw_buf_dsc;
    static lv_color_t buf_1[MY_DISP_HOR_RES * 10];
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, NULL, MY_DISP_HOR_RES * 10);

    /* 注册显示驱动 */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf_dsc;

    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void disp_init(void)
{
    /* LCD 已在 main.c 中通过 LCD_Init() 初始化 */
}

volatile bool disp_flush_enabled = true;

void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/**
 * @brief  LVGL flush 回调 - 将渲染缓冲区发送到 LCD
 * @note   LV_COLOR_16_SWAP = 0, LVGL buffer 是小端 RGB565 (CPU 原生序),
 *         LCD_DrawBuffer 内部做 byte swap 转成大端后通过 SPI 发送.
 *         这与之前测试通过的 demo 逻辑一致.
 */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    if(disp_flush_enabled) {
        LCD_DrawBuffer(area->x1, area->y1, area->x2, area->y2, (uint16_t *)color_p);
        LCD_DMA_Wait();
    }

    lv_disp_flush_ready(disp_drv);
}

#else
typedef int keep_pedantic_happy;
#endif
