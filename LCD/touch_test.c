/**
  ******************************************************************************
  * @file    touch_test.c
  * @brief   XPT2046 触摸 FreeRTOS 测试任务
  *
  *  功能:
  *    1) 启动时调用 TP_Init(), 把校准状态通过 UART 打印
  *    2) 主循环以 20ms 周期扫描触摸点
  *    3) 触摸按下/松开时打印屏幕坐标, 并在 LCD 上画一个色块
  *    4) 长按屏幕右上角 (>2s) 触发重新校准
  *    5) 串口命令支持 (见 TouchTest_ProcessLine):
  *         'c' / 'C'         -> 重新校准
  *         'p' / 'P'         -> 打印当前校准参数
  *         'r' / 'R'         -> 读取一次原始 ADC (物理坐标)
  *         'h' / 'H' / '?'   -> 打印帮助
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "main.h"
#include "lvgl.h"

#include "touch.h"
#include "lcd.h"
#include "display_mode.h"     /* g_display_mode: 用于 indev 坐标方向补偿 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*============================================================================
 *                              配置
 *============================================================================*/
#define TOUCH_TEST_TASK_STACK      512
#define TOUCH_TEST_TASK_PRIO       osPriorityNormal
#define TOUCH_TEST_PERIOD_MS       20      /* 扫描周期 */
#define TOUCH_DOT_SIZE             6       /* LCD 上触摸点色块大小 */

/* 长按触发重新校准的区域: 屏幕右上角 60x60 像素, 按住 2 秒 */
#define RECALIB_AREA_W             60
#define RECALIB_AREA_H             60
#define RECALIB_HOLD_MS            2000

/*============================================================================
 *                              内部状态
 *============================================================================*/
static TaskHandle_t s_touch_test_hdl = NULL;
static volatile uint8_t s_recalib_req = 0;     /* 由其他任务/串口命令置位 */

/*============================================================================
 *  LVGL indev 共享状态 (供 indev_read_cb 读取, 触摸扫描任务周期更新)
 *  - 触摸扫描 20ms 一次, indev_read_cb 频繁调用, 解耦避免重复 ADC 读取
 *============================================================================*/
static volatile uint8_t  s_indev_pressed = 0;
static volatile uint16_t s_indev_x = 0;
static volatile uint16_t s_indev_y = 0;
static lv_indev_t       *s_indev_touchpad = NULL;

/**
 * @brief LVGL indev 读取回调 (LVGL 内部定时调用, 5ms 一次)
 * @note  从共享缓冲读最新一次触摸扫描结果, 避免在 indev 上下文做 ADC
 *
 *  关键: weather / camera 屏幕均使用 LCD_direction(3) 180° 翻转,
 *        触摸硬件输出的物理坐标方向与 LVGL 控件方向相反, 必须反向.
 *        反向: x' = W-1-x, y' = H-1-y
 */
static void lvgl_touchpad_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    lv_coord_t x = (lv_coord_t)s_indev_x;
    lv_coord_t y = (lv_coord_t)s_indev_y;

    /* 两个屏幕都已 180° 翻转, 触摸坐标需反向 */
    x = (lv_coord_t)lcddev.width  - 1 - x;
    y = (lv_coord_t)lcddev.height - 1 - y;

    data->state   = s_indev_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = x;
    data->point.y = y;
}

/**
 * @brief 注册 LVGL pointer 类型输入设备, 指向 XPT2046
 * @note  必须在 TP_Init 之后调用一次, 且必须在 LVGL 初始化之后 (在主任务里)
 */
static void TouchIndev_Register(void)
{
    static lv_indev_drv_t s_indev_drv;     /* 必须 static, 不能放栈上 */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = lvgl_touchpad_read_cb;
    s_indev_touchpad    = lv_indev_drv_register(&s_indev_drv);
    printf("[TP] LVGL indev (pointer) registered: %p\r\n", s_indev_touchpad);
}

/*============================================================================
 *  小工具: 区域填充 (画点)
 *============================================================================*/
static void TouchTest_DrawDot(uint16_t x, uint16_t y, uint16_t color)
{
    /* 画一个 6x6 的实心方块作为触摸点标记 */
    int16_t x0 = (int16_t)x - (TOUCH_DOT_SIZE / 2);
    int16_t y0 = (int16_t)y - (TOUCH_DOT_SIZE / 2);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    LCD_Fill((uint16_t)x0, (uint16_t)y0,
             (uint16_t)(x0 + TOUCH_DOT_SIZE - 1),
             (uint16_t)(y0 + TOUCH_DOT_SIZE - 1),
             color);
}

/*============================================================================
 *  串口命令处理 (可在外部 ISR / 任务中调用)
 *============================================================================*/
void TouchTest_ProcessLine(const char *line)
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
 *  触摸测试主任务
 *============================================================================*/
static void TouchTest_Task(void *argument)
{
    (void)argument;

    uint32_t  scan_cnt    = 0;
    uint32_t  press_cnt   = 0;
    TickType_t xLastWake  = xTaskGetTickCount();

    /* 让 LVGL 等其它 UI 任务先跑起来, 再初始化触摸 (避免启动期 PEN 抖动) */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 直接进入初始化, 自检放到 TP_Init 之后做 (那时 GPIO 才初始化好) */
    printf("[TP] Initializing XPT2046...\r\n");
    uint8_t already_calib = TP_Init();
    printf("[TP] Init done, already calibrated = %s\r\n",
           already_calib ? "YES" : "NO  (just calibrated)");

    /* 把 XPT2046 注册成 LVGL pointer indev,
     * 之后按钮/控件才能被触摸触发 */
    TouchIndev_Register();

    /* ============ 启动后自检 (此时 GPIO 已初始化) ============ */
    printf("[TP] === Post-init self-test ===\r\n");
    /* 读 10 次 PEN, 让你在屏幕上画一根手指看看 PEN 会不会变 */
    printf("[TP] Touch screen now to see PEN change. (Ctrl-C to skip)\r\n");
    for (int i = 0; i < 30; i++) {
        printf("[TP]   t=%dms PEN=%d\r\n", i*100, (int)TP_PEN_READ());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* 试两种命令字顺序, 找出能读到有效 ADC 值的组合 */
    printf("[TP] Trying cmd 0xD0...\r\n");
    for (int i = 0; i < 3; i++) {
        uint16_t v = TP_Read_AD(0xD0);
        printf("[TP]   read cmd=0xD0 -> %u\r\n", (unsigned)v);
    }
    printf("[TP] Trying cmd 0x90...\r\n");
    for (int i = 0; i < 3; i++) {
        uint16_t v = TP_Read_AD(0x90);
        printf("[TP]   read cmd=0x90 -> %u\r\n", (unsigned)v);
    }
    /* 采样 2 秒, 看 ADC 范围 */
    uint16_t min_v = 4095, max_v = 0;
    for (int i = 0; i < 20; i++) {
        uint16_t v = TP_Read_AD(0xD0);
        if (v < min_v) min_v = v; if (v > max_v) max_v = v;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("[TP] cmd=0xD0 range: [%u..%u] delta=%u\r\n",
           (unsigned)min_v, (unsigned)max_v, (unsigned)(max_v - min_v));
    min_v = 4095; max_v = 0;
    for (int i = 0; i < 20; i++) {
        uint16_t v = TP_Read_AD(0x90);
        if (v < min_v) min_v = v; if (v > max_v) max_v = v;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("[TP] cmd=0x90 range: [%u..%u] delta=%u\r\n",
           (unsigned)min_v, (unsigned)max_v, (unsigned)(max_v - min_v));
    if (max_v - min_v < 50) {
        printf("[TP] !!! ADC fixed. Touch module or SPI pin wiring wrong !!!\r\n");
    } else {
        printf("[TP] SPI works.\r\n");
    }
    printf("[TP] === End post-init self-test ===\r\n");
    /* ==================================================== */
    printf("[TP] LCD size: %u x %u\r\n",
           (unsigned)lcddev.width, (unsigned)lcddev.height);
    printf("[TP] CMD_RDX=0x%02X CMD_RDY=0x%02X touchtype=%u\r\n",
           (unsigned)CMD_RDX, (unsigned)CMD_RDY,
           (unsigned)tp_dev.touchtype);
    printf("[TP] Send 'h' for commands. Long-press top-right to recalibrate.\r\n");

    /* 长按识别状态 */
    uint8_t   hold_active     = 0;     /* 触摸点是否落在重新校准区域 */
    TickType_t hold_start     = 0;

    /* 触摸按下沿检测状态 */
    uint8_t prev_pressed = 0;

    for (;;) {
        /* ----------- 处理外部触发的重新校准请求 (串口命令) ----------- */
        if (s_recalib_req) {
            s_recalib_req = 0;
            printf("[TP] >>> Enter re-calibration <<<\r\n");
            TP_Calibrate();
            TP_Get_Calibration();
            printf("[TP] Re-calibration finished. xfac=%.4f yfac=%.4f type=%u\r\n",
                   (double)tp_dev.xfac, (double)tp_dev.yfac,
                   (unsigned)tp_dev.touchtype);
        }

        /* ----------- 扫描触摸 ----------- */
        uint16_t x = 0, y = 0;
        uint8_t  pressed = TP_Get_TouchPoint(&x, &y);
        scan_cnt++;

        /* 同步给 LVGL indev (indev_read_cb 读取这里) */
        s_indev_pressed = pressed;
        s_indev_x = x;
        s_indev_y = y;

        if (pressed && !prev_pressed) {
            /* 按下沿 */
            press_cnt++;
            printf("[TP] DOWN  #%lu  scr=(%u,%u)  [scan=%lu]\r\n",
                   (unsigned long)press_cnt, x, y,
                   (unsigned long)scan_cnt);

            /* 不再画 LCD 色块, 避免遮挡 LVGL 按钮和摄像头画面 */

            /* 判断是否落在视觉右上角重新校准区
             * 注: LCD 翻转 180° 后, 物理 (x, y) 与视觉 (x', y') 的关系:
             *     x' = W-1-x, y' = H-1-y
             * 视觉右上角 (x' 大, y' 小) 对应物理 (x 小, y 大), 即物理左下角 */
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
            /* 不再画跟随点, 避免遮挡摄像头画面 */
        }
        else if (!pressed && prev_pressed) {
            /* 松开沿 */
            printf("[TP] UP             scr=(%u,%u)\r\n", x, y);
            /* 不再画 LCD 色块 */
            hold_active = 0;
        }

        prev_pressed = pressed;

        /* ----------- 周期 20ms ----------- */
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TOUCH_TEST_PERIOD_MS));
    }
}

/*============================================================================
 *  公共 API
 *============================================================================*/
void TouchTest_Start(void)
{
    if (s_touch_test_hdl != NULL) return;
    xTaskCreate(TouchTest_Task, "TP_Test",
                TOUCH_TEST_TASK_STACK, NULL,
                TOUCH_TEST_TASK_PRIO, &s_touch_test_hdl);
}

void TouchTest_RequestRecalibrate(void)
{
    s_recalib_req = 1;
}

TaskHandle_t TouchTest_GetHandle(void)
{
    return s_touch_test_hdl;
}
