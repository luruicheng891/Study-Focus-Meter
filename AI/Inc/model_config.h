/**
  ******************************************************************************
  * @file    model_config.h
  * @brief   模型配置 — 注意力分类模型
  *
  *          输入:  32x32 灰度图 (1 channel), NHWC
  *          输出:  3分类 softmax [focused(0), distracted(1), fatigued(2)]
  *          平台:  STM32H723ZGT6 @ 550MHz
  ******************************************************************************
  */
#ifndef MODEL_CONFIG_H_
#define MODEL_CONFIG_H_

/* ---- 模型输入 ---- */
#define AI_MODEL_INPUT_WIDTH       32
#define AI_MODEL_INPUT_HEIGHT      32
#define AI_MODEL_INPUT_CHANNELS    1
#define AI_MODEL_INPUT_ELEMENTS    (32 * 32 * 1)    /* 1024 */

/* ---- 模型输出 ---- */
#define AI_MODEL_OUTPUT_CLASSES    3
#define AI_MODEL_OUTPUT_ELEMENTS   3

/* ---- 标签 ---- */
#define AI_LABEL_FOCUSED           0
#define AI_LABEL_DISTRACTED        1
#define AI_LABEL_FATIGUED          2

#define AI_LABEL_STR_FOCUSED       "Focused"
#define AI_LABEL_STR_DISTRACTED    "Distracted"
#define AI_LABEL_STR_FATIGUED      "Fatigued"

#endif /* MODEL_CONFIG_H_ */
