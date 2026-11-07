/**
  ******************************************************************************
  * @file    AITask.c
  * @brief   AI 推理 FreeRTOS 任务 — mailbox queue 分发架构
  *
  *  工作流程:
  *    1. 初始化 CubeAI 模型
  *    2. 创建 LVGL 结果显示标签
  *    3. 创建两个 1-槽 mailbox queue (s_q_fusion / s_q_debug)
  *    4. 循环等待 Camera_task 帧就绪通知
  *    5. 预处理: 240x240 RGB565 -> 32x32 灰度 uint8
  *    6. 推理: 送入模型得到 3 类别概率
  *    7. 结果输出: 串口打印 + LVGL 标签更新
  *    8. 分发: xQueueOverwrite 到两个消费者队列
  *
  *  设计要点:
  *    - 消费者各取所需: FusionTask 阻塞等, Debug 面板 peek 看最新
  *    - 无临界区, 无共享变量, 队列天然线程安全
  *    - 推理节流: 2fps (500ms 最小间隔)
  *    - 即使 DISP_MODE 不是 CAMERA, 推理也持续 (供 LearningScreen 调试面板用)
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

/* 消费者队列 (header 中 extern 声明, 在此定义) */
QueueHandle_t s_q_fusion = NULL;
QueueHandle_t s_q_debug  = NULL;

/* LVGL 结果显示标签 */
static lv_obj_t *label_ai_result = NULL;
static lv_obj_t *label_ai_probs  = NULL;

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

/**
 * @brief 创建消费者 mailbox queue (1-槽, 每个消费者一个)
 * @return 0=成功, -1=失败
 */
static int AI_CreateQueues(void)
{
    /* 每队列 1 槽, 存 AI_Result_t (~28 字节) */
    s_q_fusion = xQueueCreate(1, sizeof(AI_Result_t));
    if (s_q_fusion == NULL) {
        printf("[AI] s_q_fusion create FAILED\r\n");
        return -1;
    }
    s_q_debug = xQueueCreate(1, sizeof(AI_Result_t));
    if (s_q_debug == NULL) {
        printf("[AI] s_q_debug create FAILED\r\n");
        return -1;
    }
    printf("[AI] Mailbox queues created (fusion=%p debug=%p)\r\n",
           (void*)s_q_fusion, (void*)s_q_debug);
    return 0;
}

/**
 * @brief 把推理结果广播到所有消费者队列
 *        使用 xQueueOverwrite (1-槽满时自动覆盖, 无阻塞)
 */
static void AI_BroadcastResult(const AI_Result_t *r)
{
    if (s_q_fusion) (void)xQueueOverwrite(s_q_fusion, r);
    if (s_q_debug)  (void)xQueueOverwrite(s_q_debug,  r);
    /* 以后想加更多消费者 (例如 s_q_logger), 就在这加一行 */
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

    /* Step 3: 创建 mailbox 队列 (供 FusionTask / Debug 面板使用) */
    if (AI_CreateQueues() != 0) {
        /* 队列失败, 任务无法工作 */
        vTaskDelete(NULL);
        return;
    }

    /* Step 4: 推理循环 */
    float       probs[AI_MODEL_OUTPUT_ELEMENTS];
    AI_Result_t result;
    uint32_t    seq = 0;
    TickType_t  last_infer_tick = 0;
    const TickType_t MIN_INTERVAL = pdMS_TO_TICKS(500);  /* 2fps 推理, 降低CPU占用 */

    /* 预填 result 一次 (后续只覆盖字段, 减少栈变量未初始化风险) */
    memset(&result, 0, sizeof(result));
    result.label = -1;

    while (1)
    {
        /* 等待 Camera_task 帧就绪通知 (超时 1000ms 防止死锁) */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        /* 标签显示控制 (仅 Camera 屏幕显示):
         *   - CAMERA 模式: 标签可见
         *   - WEATHER 模式 (含 LearningScreen 期间): 标签隐藏, 但推理继续
         *     把最新结果通过 queue 分发给 FusionTask 和调试面板
         */
        if (g_display_mode == DISP_MODE_CAMERA) {
            if (label_ai_result) lv_obj_clear_flag(label_ai_result, LV_OBJ_FLAG_HIDDEN);
            if (label_ai_probs)  lv_obj_clear_flag(label_ai_probs,  LV_OBJ_FLAG_HIDDEN);
        } else {
            if (label_ai_result) lv_obj_add_flag(label_ai_result, LV_OBJ_FLAG_HIDDEN);
            if (label_ai_probs)  lv_obj_add_flag(label_ai_probs,  LV_OBJ_FLAG_HIDDEN);
        }

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

        /* Step 6: 构造结果并广播 */
        result.seq     = ++seq;
        result.ts_tick = xTaskGetTickCount();
        result.probs[0] = probs[0];
        result.probs[1] = probs[1];
        result.probs[2] = probs[2];
        result.label   = label;

        if (label >= 0) {
            const char *str = AI_LabelToStr(label);
            result.valid = 1;

            /* 广播到所有消费者 (原子, 无临界区) */
            AI_BroadcastResult(&result);

            /* 串口打印 (调试用) */
           /* printf("[AI] #%lu %s (F:%.1f%% D:%.1f%% T:%.1f%%)\r\n",
                   (unsigned long)result.seq, str,
                   (double)(probs[0] * 100.0f),
                   (double)(probs[1] * 100.0f),
                   (double)(probs[2] * 100.0f));
            */
            /* 更新 LVGL 标签 (仅 Camera 模式可见) */
            lv_label_set_text_fmt(label_ai_result, "AI: %s", str);
            lv_label_set_text_fmt(label_ai_probs,
                "专注:%.0f%% 分心:%.0f%% 疲劳:%.0f%%",
                (double)(probs[0] * 100.0f),
                (double)(probs[1] * 100.0f),
                (double)(probs[2] * 100.0f));
        } else {
            result.valid = 0;
            AI_BroadcastResult(&result);  /* 也广播 (valid=0), 让消费者知道 */
            printf("[AI] Inference returned error\r\n");
            lv_label_set_text(label_ai_result, "AI: ERR");
        }
    }
}

/* ------------------------------------------------------------------ */
/*                          公共访问接口 (mailbox 队列版)               */
/* ------------------------------------------------------------------ */

/**
 * @brief 非破坏读: 拿最新一帧的完整结果
 *        用 s_q_debug 队列 (peek, 不消费, 可重复读)
 */
int AI_GetLatestResult(AI_Result_t *out)
{
    if (out == NULL) return -1;
    if (s_q_debug == NULL) return -1;  /* AI 任务尚未初始化队列 */

    /* Peek: 拿到最新但保留在队列里 */
    if (xQueuePeek(s_q_debug, out, 0) == pdPASS) {
        return 0;
    }
    return -1;  /* 队列空, 无任何推理结果 */
}

/**
 * @brief 阻塞等: 拿下一帧结果 (消费)
 *        用 s_q_fusion 队列 (receive, 取出)
 */
int AI_WaitNewResult(AI_Result_t *out, TickType_t timeout)
{
    if (out == NULL) return -1;
    if (s_q_fusion == NULL) return -1;

    if (xQueueReceive(s_q_fusion, out, timeout) == pdPASS) {
        return 0;
    }
    return -1;  /* 超时 */
}

/**
 * @brief 向后兼容: 只取概率 + ts_tick
 *        内部调 AI_GetLatestResult, 拷贝 probs/ts_tick
 *
 *  注意: 如果想拿 seq/label, 请改用 AI_GetLatestResult.
 */
int AI_GetLatestProbs(float probs_out[3], uint32_t *ts_tick)
{
    AI_Result_t r;
    if (AI_GetLatestResult(&r) != 0) return -1;
    if (!r.valid) return -1;  /* 推理失败时不返回数据 (兼容旧行为) */

    if (probs_out) {
        probs_out[0] = r.probs[0];
        probs_out[1] = r.probs[1];
        probs_out[2] = r.probs[2];
    }
    if (ts_tick) {
        *ts_tick = r.ts_tick;
    }
    return 0;
}
