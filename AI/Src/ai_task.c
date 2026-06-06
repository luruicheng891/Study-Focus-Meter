/**
  ******************************************************************************
  * @file    ai_task.c
  * @brief   AI 推理任务实现 — 使用 CubeAI Runtime API
  ******************************************************************************
  */

#include <string.h>
#include <stdio.h>
#include "ai_task.h"
#include "model_config.h"

/* CubeAI 生成的头文件 */
#include "network.h"
#include "network_data.h"
#include "network_data_params.h"
#include "ai_platform.h"

/* ---- 全局 AI 句柄 ---- */
static ai_handle ai_net = AI_HANDLE_NULL;
static ai_network_report ai_report;
static ai_buffer* ai_input  = NULL;
static ai_buffer* ai_output = NULL;
static int ai_initialized = 0;

/* 静态分配激活缓冲 (避免运行时 malloc 失败) */
AI_ALIGNED(4)
static ai_u8 s_ai_activations[AI_NETWORK_DATA_ACTIVATION_1_SIZE];


int AI_Task_Init(void)
{
    ai_error err;

    /* 将静态缓冲注册到 activations table */
    g_network_activations_table[1] = AI_HANDLE_PTR(s_ai_activations);

    /* 创建并初始化网络, 使用 activations/weights table 指针 */
    err = ai_network_create_and_init(&ai_net,
              AI_NETWORK_DATA_ACTIVATIONS_TABLE_GET(),
              AI_NETWORK_DATA_WEIGHTS_TABLE_GET());
    if (err.type != AI_ERROR_NONE) {
        printf("[AI] ERROR: Create failed (type=%d, code=%d)\r\n",
               (int)err.type, (int)err.code);
        return -1;
    }

    /* 获取输入/输出 buffer */
    ai_input  = ai_network_inputs_get(ai_net, NULL);
    ai_output = ai_network_outputs_get(ai_net, NULL);

    if (!ai_input || !ai_output) {
        printf("[AI] ERROR: Failed to get I/O buffers\r\n");
        return -1;
    }

    /* 打印模型信息 */
    ai_network_get_report(ai_net, &ai_report);
    printf("[AI] Model loaded successfully\r\n");
    printf("[AI]   Model: %s\r\n", ai_report.model_name);
    printf("[AI]   MACC:  %d\r\n", (int)ai_report.n_macc);

    ai_initialized = 1;
    return 0;
}


int AI_Task_Infer(const uint8_t gray_image[AI_MODEL_INPUT_ELEMENTS])
{
    float probs[3];
    return AI_Task_InferProbs(gray_image, probs);
}


int AI_Task_InferProbs(const uint8_t gray_image[AI_MODEL_INPUT_ELEMENTS],
                        float probs_out[AI_MODEL_OUTPUT_ELEMENTS])
{
    if (!ai_initialized) {
        if (AI_Task_Init() != 0) return -1;
    }

    /* Step 1: uint8 [0,255] → float32 [0,1] 归一化到模型输入buffer */
    float* in_data = (float*)ai_input->data;
    for (int i = 0; i < AI_MODEL_INPUT_ELEMENTS; i++) {
        in_data[i] = (float)gray_image[i] / 255.0f;
    }

    /* Step 2: 运行推理 */
    ai_i32 batch = ai_network_run(ai_net, ai_input, ai_output);
    if (batch != 1) {
        ai_error ai_err = ai_network_get_error(ai_net);
        printf("[AI] ERROR: Inference failed (type=%d, code=%d)\r\n",
               (int)ai_err.type, (int)ai_err.code);
        return -1;
    }

    /* Step 3: 读取输出 */
    float* out_data = (float*)ai_output->data;
    probs_out[0] = out_data[0];
    probs_out[1] = out_data[1];
    probs_out[2] = out_data[2];

    /* 返回最大概率类别 */
    int max_idx = 0;
    if (probs_out[1] > probs_out[max_idx]) max_idx = 1;
    if (probs_out[2] > probs_out[max_idx]) max_idx = 2;
    return max_idx;
}


const char* AI_LabelToStr(int label)
{
    switch (label) {
        case AI_LABEL_FOCUSED:     return AI_LABEL_STR_FOCUSED;
        case AI_LABEL_DISTRACTED:  return AI_LABEL_STR_DISTRACTED;
        case AI_LABEL_FATIGUED:    return AI_LABEL_STR_FATIGUED;
        default:                   return "Unknown";
    }
}
