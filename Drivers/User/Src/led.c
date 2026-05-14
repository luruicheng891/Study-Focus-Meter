/***
	*************************************************************************************************
	*	@version V1.0
	*	@author  鹿小班科技	
   *************************************************************************************************
   *  @description
	*
	*	实验平台：鹿小班STM32H723ZGT6核心板 （型号：LXB723ZG-P1）
	* 客服微信：19949278543
	*
>>>>> 文件说明：
	*
	*	初始化LED的IO口，配置为推挽输出、速度等级2M。
	*
	************************************************************************************************
***/


#include "led.h"  


void LED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_LED1_CLK_ENABLE;		// 初始化LED1 GPIO时钟	


	HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET);		// LED1引脚输出低，即点亮LED1


	GPIO_InitStruct.Pin 		= LED1_PIN;					// LED1引脚
	GPIO_InitStruct.Mode 	= GPIO_MODE_OUTPUT_PP;	// 推挽输出模式
	GPIO_InitStruct.Pull 	= GPIO_NOPULL;				// 不上下拉
	GPIO_InitStruct.Speed 	= GPIO_SPEED_FREQ_LOW;	// 低速模式
	HAL_GPIO_Init(LED1_PORT, &GPIO_InitStruct);


}

