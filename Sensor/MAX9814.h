/**
  ******************************************************************************
  * @file    MAX9814.h
  * @brief   MAX9814 麦克风放大器 - ADC2 单端采集驱动
  * @note    MAX9814 输出 OUT 接到 MCU 的 PF14, 经 ADC2 通道 2 采样
  *          用途: 环境噪声 / 声压级检测
  *          模拟前端: MAX9814 (AGC 自动增益, 40dB/50dB/60dB 可选)
  *          - 1.25V 直流偏置 (VCC/2), 最大输出约 2Vrms
  *          - MCU 端采样 12-bit (0~4095), Vref = 3.3V
  ******************************************************************************
  */

#ifndef __MAX9814_H
#define __MAX9814_H

#include "stm32h7xx.h"
#include <stdint.h>

/*============================================================================
 *                            引脚 / 通道映射
 *  说明: PF14 在 STM32H723ZGT6 (LQFP144) 上对应 ADC2_INP6
 *        按用户要求, 此处按 "ADC2 通道 2" 配置; 若实际硬件走线
 *        是 ADC2_INP6, 请把 MAX9814_ADC_CHANNEL 改为 6.
 *============================================================================*/
#define MAX9814_GPIO_PORT            GPIOF
#define MAX9814_GPIO_PIN             GPIO_PIN_14
#define MAX9814_GPIO_CLK_ENABLE()    do { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOFEN; } while(0)

/* ADC 外设 */
#define MAX9814_ADC                  ADC2
#define MAX9814_ADC_CLK_ENABLE()     do { RCC->AHB1ENR |= RCC_AHB1ENR_ADC12EN; } while(0)
#define MAX9814_ADC_CHANNEL          2U            /* ADC2 通道 2 (位于 SMPR1) */
#define MAX9814_ADC_RANK             1U            /* 规则组第 1 个转换 */
/* 采样时间原始值: 0b001 = 6.5 ADC clk (快速轮询够用)
 *  注: 通道 0~9 写入 SMPR1, 通道 10~19 写入 SMPR2 */
#define MAX9814_ADC_SAMPLETIME_VAL   0x1U
#define MAX9814_ADC_RESOLUTION       4095U         /* 12-bit */

/* ADC 参考电压 (mV) - 内部 Vref+ = 3.3V */
#define MAX9814_VREF_MV              3300U

/*============================================================================
 *                               API 函数
 *============================================================================*/

/**
 * @brief  初始化 PF14 为模拟输入, 并配置 ADC2 单端单次转换模式
 * @note   默认使用 ADC clock = per_ck = 64MHz (HSI),
 *         12-bit 分辨率, 单次转换 (需软件触发)
 * @retval 0=成功, 1=GPIO 初始化失败, 2=ADC 校准失败, 3=ADC 使能失败
 */
uint8_t MAX9814_ADC_Init(void);

/**
 * @brief  单次 ADC 转换 (阻塞式, ~1us @ 64MHz)
 * @retval 12-bit ADC 原始值 (0~4095); 若返回 0xFFFF 表示超时
 */
uint16_t MAX9814_ADC_Read(void);

/**
 * @brief  多次采样取平均, 去除偶发毛刺
 * @param  times: 采样次数 (建议 4~16)
 * @retval 平均后的 ADC 值
 */
uint16_t MAX9814_ADC_ReadAvg(uint8_t times);

/**
 * @brief  读取输入电压 (mV)
 * @retval 电压值, 单位 mV
 */
uint32_t MAX9814_ADC_ReadVoltage_mV(void);

/**
 * @brief  启动 ADC 内部校准 (可选, 一般 Init 内已自动校准)
 * @retval 0=成功
 */
uint8_t MAX9814_ADC_Calibrate(void);

#endif /* __MAX9814_H */
