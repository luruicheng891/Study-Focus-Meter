/**
  ******************************************************************************
  * @file    ai_task.h
  * @brief   AI 推理任务接口
  ******************************************************************************
  */
#ifndef AI_TASK_H_
#define AI_TASK_H_

#include <stdint.h>

/**
 * @brief 初始化 CubeAI 网络模型
 * @return 0=成功, -1=失败
 */
int AI_Task_Init(void);

/**
 * @brief 单次推理
 * @param gray_image  32x32 uint8 灰度图 [0,255]
 * @return 0=专注, 1=分心, 2=疲劳, -1=错误
 */
int AI_Task_Infer(const uint8_t gray_image[32 * 32]);

/**
 * @brief 推理并返回所有类别概率
 * @param gray_image  32x32 uint8 灰度图 [0,255]
 * @param probs_out   输出概率数组 [3], 需调用者分配
 * @return 最大概率类别索引, -1=错误
 */
int AI_Task_InferProbs(const uint8_t gray_image[32 * 32],
                        float probs_out[3]);

/**
 * @brief 标签索引转字符串
 */
const char* AI_LabelToStr(int label);

#endif /* AI_TASK_H_ */
