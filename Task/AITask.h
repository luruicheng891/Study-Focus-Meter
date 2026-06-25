/**
  ******************************************************************************
  * @file    AITask.h
  * @brief   AI 推理 FreeRTOS 任务接口
  *          Camera_task 每帧完成后通知本任务执行推理
  *
  *  数据分发架构 (mailbox queue, 每消费者一个 1-槽队列):
  *
  *      Camera ──xTaskNotify──>  AI_InferTask
  *                                    │
  *                                    │  推理完成
  *                                    │  xQueueOverwrite × N
  *                                    ↓
  *              ┌──────────────────┬──┴──────────────────┬──────────┐
  *              ↓                  ↓                     ↓          ↓
  *        s_q_fusion         s_q_debug              (将来可加)  (将来可加)
  *              │                  │
  *              ↓                  ↓
  *         FusionTask      LearningScreen 调试面板
  *
  *  特点:
  *    - 消费者各取所需, 一个消费者读走不阻塞其他消费者
  *    - xQueueOverwrite 自动覆盖: 永远写最新, 不需清队列
  *    - xQueuePeek (非破坏读): Debug 面板 100ms 一次看最新但不消费
  *    - xQueueReceive (阻塞等): FusionTask 阻塞到有新数据再处理
  *    - 无临界区, 无共享变量, 天然线程安全
  ******************************************************************************
  */
#ifndef __AITASK_H
#define __AITASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* AI 任务句柄, 供 Camera_task 发送通知 */
extern TaskHandle_t AI_TaskHandle;

/* 消费者队列句柄 (由 AI_InferTask 创建, NULL 表示尚未创建) */
extern QueueHandle_t s_q_fusion;   /* 供 FusionTask 使用 */
extern QueueHandle_t s_q_debug;    /* 供 LearningScreen 调试面板使用 */

/**
 * @brief AI 推理结果结构 (整体写入队列, 消费者读走即是稳定副本)
 *
 * 字段顺序紧贴内存对齐, sizeof 在典型 ARM 编译器下 = 24 字节:
 *   - seq(4) + ts_tick(4) + valid(1) + label(1) + _pad(2) = 12 字节头
 *   - probs[3]                                           = 12 字节数据
 *   - 总计 24 字节, 实际 queue item 大小 = 24
 */
typedef struct {
    uint32_t seq;            /*!< 推理序号, 每完成一次推理 +1 */
    uint32_t ts_tick;        /*!< 推理完成时刻 (FreeRTOS tick) */
    uint8_t  valid;          /*!< 1=推理成功, 0=失败 */
    int8_t   label;          /*!< 0=focus, 1=distract, 2=fatigue, -1=err */
    uint8_t  _pad[2];        /*!< 对齐填充 */
    float    probs[3];       /*!< [0]=专注 [1]=分心 [2]=疲劳, 范围 0.0~1.0 */
} AI_Result_t;

/**
 * @brief AI 推理任务入口函数
 *        等待 Camera_task 每帧通知，执行预处理 + 模型推理，
 *        将结果通过 mailbox queue 广播给所有消费者
 */
void AI_InferTask(void *argument);

/* =========================== 消费者 API ========================== */

/**
 * @brief 非破坏读: 拿最新一帧的完整结果 (不消费, 队列内容保留)
 *
 *        适合调试面板 / 显示刷新用, 每次调用都看到 "最新值".
 *        即使没有新推理也会返回上一次结果 (队列有内容时).
 *
 * @param out  [输出] 接收结果 (不能为 NULL)
 * @return 0  : 成功 (至少有一帧历史数据)
 *        -1  : 队列尚未创建, 或无任何推理结果
 */
int AI_GetLatestResult(AI_Result_t *out);

/**
 * @brief 阻塞等: 等待新一帧结果 (消费, 队列清空)
 *
 *        适合事件驱动消费者, 无新数据时阻塞.
 *
 * @param out     [输出] 接收结果 (不能为 NULL)
 * @param timeout [输入] 等待超时 (FreeRTOS tick), 0=不阻塞
 * @return 0  : 成功
 *        -1  : 超时 / 队列未创建
 */
int AI_WaitNewResult(AI_Result_t *out, TickType_t timeout);

/**
 * @brief 向后兼容: 只取概率部分, 不带 seq/label
 *
 *        内部调用 AI_GetLatestResult 拿到完整结果, 再拷贝 probs 出去.
 *        给现有 FusionTask / 老代码使用.
 *
 * @param probs_out [输出] 数组[3] = {专注, 分心, 疲劳}，可为 NULL
 * @param ts_tick   [输出] 产生该结果的 FreeRTOS 时钟计数，可为 NULL
 * @return 0  : 有效数据已复制
 *        -1  : 尚无推理完成
 */
int AI_GetLatestProbs(float probs_out[3], uint32_t *ts_tick);

#endif /* __AITASK_H */
