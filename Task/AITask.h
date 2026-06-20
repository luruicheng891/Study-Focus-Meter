/**
  ******************************************************************************
  * @file    AITask.h
  * @brief   AI 推理 FreeRTOS 任务接口
  *          Camera_task 每帧完成后通知本任务执行推理
  ******************************************************************************
  */
#ifndef __AITASK_H
#define __AITASK_H

#include "FreeRTOS.h"
#include "task.h"

/* AI 任务句柄, 供 Camera_task 发送通知 */
extern TaskHandle_t AI_TaskHandle;

/**
 * @brief AI 推理任务入口函数
 *        等待 Camera_task 每帧通知，执行预处理 + 模型推理，
 *        将结果打印到 UART 并更新屏幕上的 LVGL 标签
 */
void AI_InferTask(void *argument);

/**
 * @brief 读取最新的 AI 推理概率结果
 *
 * @param probs_out [输出] 数组[3] = {专注, 分心, 疲劳}，
 *                        可为 NULL
 * @param ts_tick   [输出] 产生该结果的 FreeRTOS 时钟计数，
 *                        可为 NULL
 * @return 0  : 有效数据已复制
 *         -1 : 尚无推理完成
 *
 * @note  可从任意任务安全调用；通过临界区保护，访问时间短
 */
int AI_GetLatestProbs(float probs_out[3], uint32_t *ts_tick);

#endif /* __AITASK_H */