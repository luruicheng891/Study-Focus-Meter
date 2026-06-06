/***
	*************************************************************************************************
	*	@file  	main.c
	*	@version V1.0
	*
	*	OV2640采集图像显示到ILI9341 LCD屏幕 (LVGL + FreeRTOS)
	*
	************************************************************************************************
***/
#include "main.h"
#include "led.h"
#include "usart.h"
#include "lcd.h"
#include "dcmi_ov2640.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cJSON.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "TIM.h"
#include "display_mode.h"


/*cJSON内存适配FreeRTOS*/
static void* cjson_malloc(size_t size) {
    return pvPortMalloc(size);
}

static void cjson_free(void *ptr) {
    vPortFree(ptr);
}

void CJSON_Init(void)
{
	cJSON_Hooks hooks;
	hooks.malloc_fn = cjson_malloc;
	hooks.free_fn = cjson_free;
	cJSON_InitHooks(&hooks);	
}


/********************************************** 函数声明 *******************************************/

void MPU_Config(void);
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);

/***************************************************************************************************
*	函 数 名: main
****************************************************************************************************/

int main(void)
{
	MPU_Config();
	SCB_EnableICache();
	SCB_EnableDCache();
	HAL_Init();
	
	SystemClock_Config();
	CJSON_Init();
	
	/* 初始化 USART1 (调试串口, printf 输出) */
	USART1_Init();

	/* 启动 USART1 单字节中断接收, 用于命令切换显示模式 ('C'/'W') */
	DisplayMode_StartUart1Rx();
	
	/* 初始化 LCD 硬件 */
	LCD_Init();
	
	/* 初始化 LVGL */
	lv_init();
	lv_port_disp_init();
	
	/* 启动 TIM7 为 LVGL 提供 1ms 心跳 */
	TIM7_Init();
	
	/* 初始化 FreeRTOS 任务 */
	MX_FREERTOS_Init();
	
	/* 开启 FreeRTOS 任务调度 */
	vTaskStartScheduler();
	
	while (1)
	{
	}
}

/****************************************************************************************************/

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 10;
  RCC_OscInitStruct.PLL.PLLN = 220;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct;

  HAL_MPU_Disable();

  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x24000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Error_Handler(void)
{
  __disable_irq();//全局禁用所有可屏蔽中断
  while (1)
  {
  }
}
