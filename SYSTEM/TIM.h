/**
  ******************************************************************************
  * @file    TIM.h
  * @brief   TIM7 基本定时器头文件, 为 LVGL 提供 1ms 心跳
  ******************************************************************************
  */

#ifndef __TIM_H
#define __TIM_H

#include "stm32h7xx_hal.h"

/**
  * @brief  TIM7 初始化 (1ms 中断, 驱动 LVGL tick)
  */
void TIM7_Init(void);

#endif /* __TIM_H */
