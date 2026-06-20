/**
  ******************************************************************************
  * @file    FusionTask.h
  * @brief   多模态学习状态融合引擎
  *
  *          融合任务将四个数据源合并为一个 0..100 的 "学习质量" 评分:
  *
  *            模块         权重   数据来源
  *            ----------   ------  ------------------------------------------
  *            视觉          40 %   AITask 专注/分心/疲劳 概率
  *            姿态          30 %   SlaveRxTask 评分 (ESP32 + IMU/压力)
  *            环境          20 %   SensorTask 温度/湿度/光照/声音
  *            时长          10 %   自首次采样以来的系统运行时间
  *            ----------   ------
  *            总计         100 %
  *
  *          融合结果通过长度为 1 的队列发布，并通过 Fusion_GetLatest() /
  *          Fusion_WaitNew() API 供 LVGL 显示任务 (或其他消费者) 获取并渲染
  ******************************************************************************
  */

#ifndef __FUSION_TASK_H
#define __FUSION_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 模块权重 (百分比, 总和 = 100) ---------------- */
#define FUSION_W_VISION          40
#define FUSION_W_POSTURE         30
#define FUSION_W_ENV             20
#define FUSION_W_DURATION        10

/* ---------------- 子标志位 (输入数据可用性) ----------------- */
#define FUSION_FLAG_VISION_OK    (1u << 0)
#define FUSION_FLAG_POSTURE_OK   (1u << 1)
#define FUSION_FLAG_SENSOR_OK    (1u << 2)

/* ============================ 结果结构体 ============================ */

/**
  * @brief  单次融合快照
  *         所有评分范围均为 [0, 100]，数值越高表示 "学习状态越好"
  */
typedef struct
{
    /* 顶层评分 */
    uint8_t  total_score;        /* 最终加权评分，范围 0..100          */

    uint8_t  vision_score;       /* AI 视觉子评分                      */
    uint8_t  posture_score;      /* 姿态子评分 (来自从机)              */
    uint8_t  env_score;          /* 环境综合子评分                     */
    uint8_t  duration_score;     /* 学习时长子评分                     */

    /* 环境细分评分 (各项范围 0..100) */
    uint8_t  env_temp_score;
    uint8_t  env_humi_score;
    uint8_t  env_lux_score;
    uint8_t  env_noise_score;

    /* 用于 UI / 调试的原始数据 */
    float    vision_prob_focus;     /* AI 输出的 "专注" 概率             */
    float    vision_prob_distract;  /* AI 输出的 "分心" 概率             */
    float    vision_prob_fatigue;   /* AI 输出的 "疲劳" 概率             */
    char     posture_state[16];     /* 从机状态字符串                    */
    float    pressure;              /* 从机压力值                       */
    int32_t  temperature_c100;      /* 温度，单位 0.01 C                */
    uint32_t humidity_x100;         /* 湿度，单位 0.01 %                */
    uint32_t lux_x100;              /* 光照强度，单位 0.01 lx           */
    uint16_t sound_pct;             /* 音量百分比，范围 0..100          */
    uint32_t study_minutes;         /* 自首帧有效数据以来的运行分钟数    */

    /* 辅助信息 */
    uint32_t flags;                 /* FUSION_FLAG_* 位图               */
    char     advice[64];            /* 简短建议字符串                    */
    uint32_t timestamp;             /* FreeRTOS 时钟计数                */
    uint32_t seq;                   /* 单调递增帧计数器                  */
} FusionResult_t;

/* ============================ 任务入口 ============================ */

/**
  * @brief  融合任务入口函数。建议栈空间 >= 1024 字
  */
void Fusion_Task(void *pvParameters);

/* ============================ 消费者 API ============================ */

/**
  * @brief  非阻塞获取最新融合结果
  * @retval 0  : 数据已复制
  * @retval -1 : 尚无融合结果
  */
int Fusion_GetLatest(FusionResult_t *out);

/**
  * @brief  阻塞等待下一帧融合结果
  * @param  timeout  超时时间，单位 tick；portMAX_DELAY 表示无限等待
  * @retval 0  : 数据已复制
  * @retval -1 : 超时
  */
int Fusion_WaitNew(FusionResult_t *out, TickType_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* __FUSION_TASK_H */