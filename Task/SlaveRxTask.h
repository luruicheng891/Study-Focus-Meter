/**
  ******************************************************************************
  * @file    SlaveRxTask.h
  * @brief   从机 (ESP32 + IMU + 压力传感器) 数据接收任务
  *
  * 数据路径:
  *   ESP32 (从机) -> BLE 桥接 (W02 / PB-03F) -> 主机 USART2
  *      |__ Communication/UART_ESP32.c 进行数据帧封装
  *           |__ Slave_RxTask 解析 JSON -> 内部队列 -> 消费者 API
  *
  * 使用方法:
  *   1. 在 freertos.c 中创建 Slave_RxTask 任务
  *   2. 其他任务调用 Slave_GetLatest() (非阻塞) 或
  *      Slave_WaitNew() (阻塞) 获取最新数据帧
  *
  * ESP32 从机 JSON 数据格式 (新协议):
  *   {"ts":12345678,"state":"focused","score":85,"pressure":45.2}
  *
  *   - ts       : 从机时间戳，单位毫秒 (uint64)
  *   - state    : 姿态/状态字符串 ("focused", "distracted", ...)
  *   - score    : 姿态评分，范围 0..100
  *   - pressure : 座椅/应变压力值
  ******************************************************************************
  */

#ifndef __SLAVE_RX_TASK_H
#define __SLAVE_RX_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 数据结构 ============================ */

#define SLAVE_STATE_STR_LEN     16

/**
  * @brief  单帧从机数据 (新 JSON 协议)
  */
typedef struct
{
    uint64_t ts;                              /* 从机时间戳，单位毫秒          */
    char     state[SLAVE_STATE_STR_LEN];      /* 姿态状态字符串                */
    int32_t  score;                           /* 姿态评分，范围 0..100         */
    float    pressure;                        /* 压力值                       */
    uint32_t local_tick;                      /* 主机接收时的 FreeRTOS 时钟计数 */
    uint32_t seq;                             /* 单调递增帧计数器              */
} SlaveData_t;

/* ============================ 任务入口 ============================ */

/**
  * @brief  从机数据接收/解析任务
  *         栈空间 >= 512 字，建议优先级 osPriorityNormal
  */
void Slave_RxTask(void *pvParameters);

/* ============================ 消费者 API ============================ */

/**
  * @brief  非阻塞获取最新数据帧 (数据保留在队列中)
  * @retval 0  : 成功
  * @retval -1 : 无可用数据 / 任务未就绪
  */
int Slave_GetLatest(SlaveData_t *out);

/**
  * @brief  阻塞等待下一帧新数据 (从队列中取出该帧)
  * @param  timeout  超时时间，单位 tick；portMAX_DELAY 表示无限等待
  * @retval 0  : 成功
  * @retval -1 : 超时 / 任务未就绪
  */
int Slave_WaitNew(SlaveData_t *out, TickType_t timeout);

/**
  * @brief  读取统计信息 (调试辅助函数)
  * @param  recv      接收到的完整帧总数，可为 NULL
  * @param  drop      驱动层丢弃的帧数，可为 NULL
  * @param  parse_err JSON 解析失败次数，可为 NULL
  */
void Slave_GetStats(uint32_t *recv, uint32_t *drop, uint32_t *parse_err);

#ifdef __cplusplus
}
#endif

#endif /* __SLAVE_RX_TASK_H */