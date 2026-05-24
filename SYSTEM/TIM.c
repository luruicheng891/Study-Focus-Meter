/**
  ******************************************************************************
  * @file    TIM.c
  * @brief   TIM7 基本定时器, 为 LVGL 提供 1ms 心跳
  * @note    TIM7 挂在 APB1 上, 时钟 = 275MHz (APB1 Timer Clock = 2 * APB1 = 275MHz)
  *          STM32H723 当 APB1 分频 != 1 时, 定时器时钟 = 2 * APB1
  *          APB1 = 137.5MHz, Timer Clock = 275MHz
  *          预分频 275-1, 计数周期 1000-1 => 中断频率 = 275MHz / 275 / 1000 = 1kHz
  ******************************************************************************
  */

#include "TIM.h"
#include "lvgl.h"

/**
  * @brief  TIM7 初始化, 配置为 1ms 中断
  * @note   直接操作寄存器, 无需启用 HAL_TIM_MODULE
  */
void TIM7_Init(void)
{
    /* 使能 TIM7 时钟 (APB1L) */
    RCC->APB1LENR |= RCC_APB1LENR_TIM7EN;
    /* 等待时钟稳定 */
    __NOP();
    __NOP();

    /* 停止计数 */
    TIM7->CR1 = 0;

    /* 预分频: 275MHz / 275 = 1MHz */
    TIM7->PSC = 275 - 1;

    /* 自动重装载: 1MHz / 1000 = 1kHz (1ms) */
    TIM7->ARR = 1000 - 1;

    /* 产生更新事件, 使 PSC 和 ARR 立即生效 */
    TIM7->EGR = TIM_EGR_UG;

    /* 清除更新中断标志 */
    TIM7->SR = 0;

    /* 使能更新中断 */
    TIM7->DIER |= TIM_DIER_UIE;

    /* 配置 NVIC: TIM7 中断优先级设为较低 (不影响 DCMI/SPI) */
    HAL_NVIC_SetPriority(TIM7_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);

    /* 启动计数器 */
    TIM7->CR1 |= TIM_CR1_CEN;
}

/**
  * @brief  TIM7 中断服务函数
  * @note   每 1ms 调用一次 lv_tick_inc(1)
  */
void TIM7_IRQHandler(void)
{
    if (TIM7->SR & TIM_SR_UIF)
    {
        TIM7->SR = ~TIM_SR_UIF;  /* 清除中断标志 */
        lv_tick_inc(1);           /* LVGL 心跳 +1ms */
    }
}
