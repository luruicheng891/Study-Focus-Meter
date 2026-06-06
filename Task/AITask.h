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
 * @brief AI 推理任务入口
 * @note  阻塞等待 Camera_task 的帧就绪通知,
 *        执行预处理+推理, 结果通过串口打印+LVGL显示
 */
void AI_InferTask(void *argument);

#endif /* __AITASK_H */
