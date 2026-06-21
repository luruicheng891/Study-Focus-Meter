/**
  ******************************************************************************
  * @file    TouchTask.c
  * @brief   XPT2046 触摸任务 (FreeRTOS)
  *
  * 功能:
  *   1) 启动时 TP_Init() 完成 XPT2046 初始化 + 自动/已存校准
  *   2) 注册 LVGL pointer 类型 indev, 让按钮/控件可触摸
  *   3) 20ms 周期扫描触摸点, 同步给 LVGL indev 共享变量
  *   4) DOWN/UP 事件通过 USART1 打印
  *   5) 长按视觉右上角 60x60 区域 ≥ 2s 触发重新校准
  *   6) 串口命令支持 (Touch_ProcessLine):
  *        'c' / 'C'        -> 重新校准
  *        'p' / 'P'        -> 打印当前校准参数
  *        'r' / 'R'        -> 读取一次原始 ADC (用于排查接线)
  *        'h' / 'H' / '?'  -> 打印帮助
  ******************************************************************************
  */

#include "TouchTask.h"
#include "ScreenSaver.h"   /* 触摸即唤醒息屏 */
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "main.h"
#include "lvgl.h"

#include "touch.h"
#include "lcd.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*============================================================================
 *                              配置
 *============================================================================*/
#define TOUCH_PERIOD_MS            20      /* 扫描周期 */

/* 长按触发重新校准的区域: 屏幕右上角 60x60 像素, 按住 2 秒 */
#define RECALIB_AREA_W             60
#define RECALIB_AREA_H             60
#define RECALIB_HOLD_MS            2000

/*============================================================================
 *                              内部状态
 *============================================================================*/
static TaskHandle_t      s_touch_hdl   = NULL;
static volatile uint8_t  s_recalib_req = 0;     /* 由其它任务/串口命令置位 */

/*============================================================================
 *  LVGL indev 共享状态
 *  - 触摸扫描 20ms 一次, indev_read_cb 每 5ms 被 LVGL 调用一次
 *  - 这里用三个 volatile 变量做同步, 避免 read_cb 里重复 ADC 读取
 *============================================================================*/
static volatile uint8_t  s_indev_pressed = 0;
static volatile uint16_t s_indev_x       = 0;
static volatile uint16_t s_indev_y       = 0;
static lv_indev_t       *s_indev_touchpad = NULL;

/**
  * @brief LVGL indev 读取回调
  * @note  weather / camera 屏幕都用 LCD_direction(3) 做了 180° 翻转,
  *        触摸硬件输出的物理坐标方向与 LVGL 控件方向相反, 此处反向补偿:
  *          x' = W - 1 - x,  y' = H - 1 - y
  */
static void lvgl_touchpad_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    lv_coord_t x = (lv_coord_t)s_indev_x;
    lv_coord_t y = (lv_coord_t)s_indev_y;

    x = (lv_coord_t)lcddev.width  - 1 - x;
    y = (lv_coord_t)lcddev.height - 1 - y;

    data->state   = s_indev_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = x;
    data->point.y = y;
}

/**
  * @brief 注册 XPT2046 为 LVGL pointer 类型输入设备
  * @note  必须在 TP_Init 与 LVGL 初始化都完成之后调用
  */
static void TouchIndev_Register(void)
{
    static lv_indev_drv_t s_indev_drv;     /* 必须 static, 不能放栈 */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = lvgl_touchpad_read_cb;
    s_indev_touchpad    = lv_indev_drv_register(&s_indev_drv);
    printf("[TP] LVGL indev (pointer) registered: %p\r\n", s_indev_touchpad);
}

/*============================================================================
 *  串口命令处理 (可在外部 ISR / 任务中调用)
 *============================================================================*/
void Touch_ProcessLine(const char *line)
{
    if (line == NULL) return;

    if (line[0] == 'c' || line[0] == 'C') {
        s_recalib_req = 1;
        printf("[TP] Re-calibration requested\r\n");
    } else if (line[0] == 'p' || line[0] == 'P') {
        printf("[TP] Calibration: xfac=%.4f yfac=%.4f xoff=%d yoff=%d type=%u\r\n",
               (double)tp_dev.xfac, (double)tp_dev.yfac,
               (int)tp_dev.xoff, (int)tp_dev.yoff,
               (unsigned)tp_dev.touchtype);
    } else if (line[0] == 'r' || line[0] == 'R') {
        uint16_t rx, ry;
        if (TP_Read_XY(&rx, &ry)) {
            printf("[TP] Raw ADC: x=%u y=%u (cmd RDX=0x%02X RDY=0x%02X)\r\n",
                   rx, ry, (unsigned)CMD_RDX, (unsigned)CMD_RDY);
        }
    } else if (line[0] == 'h' || line[0] == 'H' || line[0] == '?') {
        printf("[TP] Commands: c=calibrate  p=print calib  r=read raw  h=help\r\n");
    }
}

/*============================================================================
 *  启动后自检 (一次性)
 *  - 读 PEN 多次, 让用户在屏上比划判断 PEN 是否会随触摸变化
 *  - 读两种 ADC 命令字 (0xD0 / 0x90) 看 ADC 量程是否合理
 *============================================================================*/
static void TouchTask_PostInitSelfTest(void)
{
    printf("[TP] === Post-init self-test ===\r\n");
    printf("[TP] Touch screen now to see PEN change. (skip if not needed)\r\n");

    for (int i = 0; i < 30; i++) {
        printf("[TP]   t=%dms PEN=%d\r\n", i * 100, (int)TP_PEN_READ());
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("[TP] Trying cmd 0xD0...\r\n");
    for (int i = 0; i < 3; i++) {
        printf("[TP]   read cmd=0xD0 -> %u\r\n", (unsigned)TP_Read_AD(0xD0));
    }
    printf("[TP] Trying cmd 0x90...\r\n");
    for (int i = 0; i < 3; i++) {
        printf("[TP]   read cmd=0x90 -> %u\r\n", (unsigned)TP_Read_AD(0x90));
    }

    /* 采样 2 秒看 ADC 量程, 用于排查 SPI 死值 */
    uint16_t min_v = 4095, max_v = 0;
    for (int i = 0; i < 20; i++) {
        uint16_t v = TP_Read_AD(0xD0);
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("[TP] cmd=0xD0 range: [%u..%u] delta=%u\r\n",
           (unsigned)min_v, (unsigned)max_v, (unsigned)(max_v - min_v));

    min_v = 4095; max_v = 0;
    for (int i = 0; i < 20; i++) {
        uint16_t v = TP_Read_AD(0x90);
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("[TP] cmd=0x90 range: [%u..%u] delta=%u\r\n",
           (unsigned)min_v, (unsigned)max_v, (unsigned)(max_v - min_v));

    if (max_v - min_v < 50) {
        printf("[TP] !!! ADC fixed. Touch module / SPI wiring wrong !!!\r\n");
    } else {
        printf("[TP] SPI works.\r\n");
    }
    printf("[TP] === End post-init self-test ===\r\n");
}

/*============================================================================
 *                              任务主体
 *============================================================================*/
void Touch_Task(void *pvParameters)
{
    (void)pvParameters;

    uint32_t   scan_cnt   = 0;
    uint32_t   press_cnt  = 0;
    TickType_t xLastWake  = xTaskGetTickCount();

    /* 长按识别状态 */
    uint8_t    hold_active = 0;
    TickType_t hold_start  = 0;

    /* 触摸沿检测 */
    uint8_t    prev_pressed = 0;

    /* 保存任务句柄, 供 Touch_GetHandle() 使用 */
    s_touch_hdl = xTaskGetCurrentTaskHandle();

    /* 让 LVGL 等其它 UI 任务先初始化, 再做触摸初始化 (避免启动期 PEN 抖动) */
    vTaskDelay(pdMS_TO_TICKS(200));

    printf("[TP] Initializing XPT2046...\r\n");
    uint8_t already_calib = TP_Init();
    printf("[TP] Init done, already calibrated = %s\r\n",
           already_calib ? "YES" : "NO  (just calibrated)");

    /* 注册到 LVGL, 之后所有控件都能被触摸 */
    TouchIndev_Register();

    /* 一次性自检, 排查 SPI / 接线 */
    TouchTask_PostInitSelfTest();

    printf("[TP] LCD size: %u x %u\r\n",
           (unsigned)lcddev.width, (unsigned)lcddev.height);
    printf("[TP] CMD_RDX=0x%02X CMD_RDY=0x%02X touchtype=%u\r\n",
           (unsigned)CMD_RDX, (unsigned)CMD_RDY,
           (unsigned)tp_dev.touchtype);
    printf("[TP] Send 'h' for commands. Long-press top-right corner to recalibrate.\r\n");

    for (;;) {
        /* ---- 处理外部触发的重新校准 ---- */
        if (s_recalib_req) {
            s_recalib_req = 0;
            printf("[TP] >>> Enter re-calibration <<<\r\n");
            TP_Calibrate();
            TP_Get_Calibration();
            printf("[TP] Re-calibration finished. xfac=%.4f yfac=%.4f type=%u\r\n",
                   (double)tp_dev.xfac, (double)tp_dev.yfac,
                   (unsigned)tp_dev.touchtype);
        }

        /* ---- 扫描触摸 ---- */
        uint16_t x = 0, y = 0;
        uint8_t  pressed = TP_Get_TouchPoint(&x, &y);
        scan_cnt++;

        /* 同步给 LVGL indev (read_cb 里读这三个变量) */
        s_indev_pressed = pressed;
        s_indev_x       = x;
        s_indev_y       = y;

        /* === 唤醒息屏 + 重置 3 分钟计时 ===
         * 只要当前有触摸就调 NotifyActivity, 不用等到 DOWN 沿:
         *   - 长按 / 持续触摸 也会持续重置计时, 不会中途息屏
         *   - 哪怕 DOWN 沿因为什么错过 (抖动/竞态), 后面 iteration 也能唤醒 */
        if (pressed) {
            ScreenSaver_NotifyActivity();
        }

        if (pressed && !prev_pressed) {
            /* 按下沿 */
            press_cnt++;
            printf("[TP] DOWN  #%lu  scr=(%u,%u)  [scan=%lu]\r\n",
                   (unsigned long)press_cnt, x, y,
                   (unsigned long)scan_cnt);

            /* LCD 做了 180° 翻转, 物理 (x,y) 与视觉 (x',y') 关系:
             *   x' = W-1-x,  y' = H-1-y
             * 视觉右上角 (x' 大, y' 小) 对应物理 (x 小, y 大), 即左下角。 */
            if (x <  RECALIB_AREA_W &&
                y >= (uint16_t)(lcddev.height - RECALIB_AREA_H)) {
                hold_active = 1;
                hold_start  = xTaskGetTickCount();
                printf("[TP] Hold top-right corner for re-calib...\r\n");
            } else {
                hold_active = 0;
            }
        }
        else if (pressed && prev_pressed) {
            /* 持续按下: 检查长按 */
            if (hold_active) {
                TickType_t held = xTaskGetTickCount() - hold_start;
                if (held >= pdMS_TO_TICKS(RECALIB_HOLD_MS)) {
                    hold_active = 0;
                    printf("[TP] Long-press detected, recalibrating...\r\n");
                    TP_Calibrate();
                    TP_Get_Calibration();
                    printf("[TP] Re-calibration done.\r\n");
                }
            }
        }
        else if (!pressed && prev_pressed) {
            /* 抬起沿 */
            printf("[TP] UP             scr=(%u,%u)\r\n", x, y);
            hold_active = 0;
        }

        prev_pressed = pressed;

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TOUCH_PERIOD_MS));
    }
}

/*============================================================================
 *                              对外 API
 *============================================================================*/
void Touch_RequestRecalibrate(void)
{
    s_recalib_req = 1;
}

TaskHandle_t Touch_GetHandle(void)
{
    return s_touch_hdl;
}
