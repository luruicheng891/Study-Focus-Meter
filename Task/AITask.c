/**
  ******************************************************************************
  * @file    AITask.c
  * @brief   AI 推理 FreeRTOS 任务
  *
  *          工作流程:
  *            1. 初始化 CubeAI 模型
  *            2. 创建 LVGL 结果显示标签
  *            3. 循环等待 Camera_task 帧就绪通知
  *            4. 预处理: 240x240 RGB565 -> 32x32 灰度 uint8
  *            5. 推理: 送入模型得到 3 类别概率
  *            6. 结果输出: 串口打印 + LVGL 标签更新
  *
  *          防竞争机制:
  *            - AI 读取 Canvas_Buffer (Camera_task memcpy 后的稳定副本)
  *            - 通过 TaskNotify 保证读取时机在 memcpy 之后
  *            - 推理频率限制为 2fps, 不影响主显示帧率
  ******************************************************************************
  */

#include "AITask.h"
#include "ai_task.h"
#include "ai_preprocess.h"
#include "model_config.h"
#include "Camera.h"
#include "display_mode.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* Canvas_Buffer 地址 (Camera.c 中定义, 240x240 RGB565 稳定副本) */
#define CANVAS_BUF_ADDR   ((const uint16_t *)0x2401C200)

/* AI 任务句柄 */
TaskHandle_t AI_TaskHandle = NULL;

/* LVGL 结果显示标签 */
static lv_obj_t *label_ai_result = NULL;
static lv_obj_t *label_ai_probs  = NULL;

/* 最新推理结果快照，供其他任务 (如 FusionTask) 使用
 * 仅在推理成功后更新；通过临界区保护写者和读者 (不同任务)
 */
static float    s_latest_probs[3]    = {0.0f, 0.0f, 0.0f};
static uint32_t s_latest_probs_tick  = 0;
static uint8_t  s_latest_probs_valid = 0;

/* 预处理中间缓存 */
static float    s_gray_f32[AI_MODEL_INPUT_ELEMENTS];   /* 4096 bytes */
static uint8_t  s_gray_u8[AI_MODEL_INPUT_ELEMENTS];    /* 1024 bytes */

/* ------------------------------------------------------------------ */

/**
 * @brief 创建 AI 结果 LVGL 标签 (覆盖在摄像头画面上方)
 * @note  标签挂在 lv_layer_top() 上, 这样无论当前是摄像头还是天气层,
 *        都能通过 lv_obj_clear_flag/add_flag 控制显隐
 */
static void AI_CreateUI(void)
{
    lv_obj_t *parent = lv_layer_top();

    /* 识别结果大字 */
    label_ai_result = lv_label_create(parent);
    lv_label_set_text(label_ai_result, "AI: ---");
    lv_obj_set_style_text_color(label_ai_result, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(label_ai_result, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(label_ai_result, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(label_ai_result, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(label_ai_result, 2, 0);
    lv_obj_align(label_ai_result, LV_ALIGN_TOP_LEFT, 4, 4);
    /* 初始隐藏, 进入摄像头模式后再显示 */
    lv_obj_add_flag(label_ai_result, LV_OBJ_FLAG_HIDDEN);

    /* 概率详情 */
    label_ai_probs = lv_label_create(parent);
    lv_label_set_text(label_ai_probs, "");
    lv_obj_set_style_text_color(label_ai_probs, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label_ai_probs, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(label_ai_probs, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(label_ai_probs, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(label_ai_probs, 2, 0);
    lv_obj_align(label_ai_probs, LV_ALIGN_TOP_LEFT, 4, 22);
    /* 初始隐藏, 进入摄像头模式后再显示 */
    lv_obj_add_flag(label_ai_probs, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------ */

void AI_InferTask(void *argument)
{
    (void)argument;

    /* 启动确认 */
    printf("[AI] Task started, initializing model...\r\n");

    /* 延迟一小段时间，确保摄像头任务已启动并有第一帧数据 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Step 1: 初始化 AI 模型 */
    if (AI_Task_Init() != 0) {
        printf("[AI] Model init FAILED! Task will retry in loop.\r\n");
        /* 不挂起，持续重试 */
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            printf("[AI] Retrying model init...\r\n");
            if (AI_Task_Init() == 0) break;
        }
    }
    printf("[AI] Model ready. Inference loop starting.\r\n");

    /* Step 2: 创建 LVGL UI */
    AI_CreateUI();

    /* Step 3: 推理循环 */
    float probs[AI_MODEL_OUTPUT_ELEMENTS];
    TickType_t last_infer_tick = 0;
    const TickType_t MIN_INTERVAL = pdMS_TO_TICKS(500);  /* 2fps 推理, 降低CPU占用 */

    while (1)
    {
        /* 等待 Camera_task 帧就绪通知 (超时 1000ms 防止死锁) */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        /* 仅在摄像头模式下做推理 */
        if (g_display_mode != DISP_MODE_CAMERA) {
            /* 非摄像头模式时隐藏标签 */
            if (label_ai_result) lv_obj_add_flag(label_ai_result, LV_OBJ_FLAG_HIDDEN);
            if (label_ai_probs)  lv_obj_add_flag(label_ai_probs, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        /* 摄像头模式时显示标签 */
        if (label_ai_result) lv_obj_clear_flag(label_ai_result, LV_OBJ_FLAG_HIDDEN);
        if (label_ai_probs)  lv_obj_clear_flag(label_ai_probs, LV_OBJ_FLAG_HIDDEN);

        /* 限流: 避免推理过于频繁 */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_infer_tick) < MIN_INTERVAL) {
            continue;
        }
        last_infer_tick = now;

        /* Step 4: 预处理 - 从 Canvas_Buffer 读取稳定副本 */
        AI_Preprocess_RGB565_to_Gray32x32(CANVAS_BUF_ADDR, s_gray_f32);
        AI_Preprocess_Float_to_U8(s_gray_f32, s_gray_u8);

        /* Step 5: 推理 */
        int label = AI_Task_InferProbs(s_gray_u8, probs);

        /* Step 6: 输出结果 */
        if (label >= 0) {
            const char *str = AI_LabelToStr(label);

            /* 发布最新概率供 FusionTask / 其他任务使用 */
            taskENTER_CRITICAL();
            s_latest_probs[0]    = probs[0];
            s_latest_probs[1]    = probs[1];
            s_latest_probs[2]    = probs[2];
            s_latest_probs_tick  = xTaskGetTickCount();
            s_latest_probs_valid = 1;
            taskEXIT_CRITICAL();

            /* 串口打印 */
            printf("[AI] %s (专注:%.1f%% 分心:%.1f%% 疲劳:%.1f%%)\r\n",
                   str,
                   (double)(probs[0] * 100.0f),
                   (double)(probs[1] * 100.0f),
                   (double)(probs[2] * 100.0f));

            /* 更新 LVGL 标签 */
            lv_label_set_text_fmt(label_ai_result, "AI: %s", str);
            lv_label_set_text_fmt(label_ai_probs,
                "专注:%.0f%% 分心:%.0f%% 疲劳:%.0f%%",
                (double)(probs[0] * 100.0f),
                (double)(probs[1] * 100.0f),
                (double)(probs[2] * 100.0f));
        } else {
            printf("[AI] Inference returned error\r\n");
            lv_label_set_text(label_ai_result, "AI: ERR");
        }
    }
}

/* ------------------------------------------------------------------ */
/*                          公共访问接口                                */
/* ------------------------------------------------------------------ */

int AI_GetLatestProbs(float probs_out[3], uint32_t *ts_tick)
{
    int valid;

    taskENTER_CRITICAL();
    valid = (s_latest_probs_valid != 0);
    if (valid)
    {
        if (probs_out)
        {
            probs_out[0] = s_latest_probs[0];
            probs_out[1] = s_latest_probs[1];
            probs_out[2] = s_latest_probs[2];
        }
        if (ts_tick)
        {
            *ts_tick = s_latest_probs_tick;
        }
    }
    taskEXIT_CRITICAL();

    return valid ? 0 : -1;
}