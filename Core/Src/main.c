/***
	*************************************************************************************************
	*	@file  	main.c
	*	@version V1.0
	*	@author  ¹С��Ƽ�
   ************************************************************************************************
   *  @description
	*
	*	ʵ��ƽ̨��¹С��STM32H723ZGT6���İ� ���ͺţ�LXB723ZG-P1��
	* �ͷ�΢�ţ�19949278543
	*
>>>>> ����˵����
	*
	*	OV2640�ɼ�ͼ����ʾ����Ļ
	*
	************************************************************************************************
***/
#include "main.h"
#include "main.h"
#include "led.h"
#include "usart.h"
#include "lcd_spi_154.h"
#include "dcmi_ov2640.h"  

#define Camera_Buffer	0x24000000    // 摄像头图像缓冲区

#define UART_FRAME_HEADER1  0xAA
#define UART_FRAME_HEADER2  0x55
#define UART_FRAME_HEADER3  0xAA
#define UART_FRAME_HEADER4  0x55

/********************************************** 函数声明 *******************************************/

void MPU_Config(void);					// MPU配置
void SystemClock_Config(void);		// 时钟初始化

void OV2640_SendFrameToUART(uint16_t *buffer, uint16_t width, uint16_t height);

/***************************************************************************************************
*	函 数 名: OV2640_SendFrameToUART
*	入口参数: buffer - 图像数据缓冲区指针 (RGB565格式)
*           width  - 图像宽度(像素)
*           height - 图像高度(像素)
*	返 回 值: 无
*	函数功能: 通过DMA将一帧图像数据从USART1发送到串口
*  协议格式: [0x5A 0xA5] [W_H W_L] [H_H H_L] [S3 S2 S1 S0] [RGB565数据...]
*	说    明: 921600波特率下，240x240一帧约需1.25秒
****************************************************************************************************/
void OV2640_SendFrameToUART(uint16_t *buffer, uint16_t width, uint16_t height)
{
	uint32_t total_pixels = (uint32_t)width * height;
	uint32_t total_bytes = total_pixels * 2;
	uint32_t offset;
	uint32_t timeout_tick;
	uint8_t header[12];  // 4字节同步头 + 2宽 + 2高 + 4大小

	SCB_InvalidateDCache_by_Addr((uint32_t *)buffer, total_bytes);

	// 4字节同步头，不容易在RGB565数据中连续出现
	header[0] = UART_FRAME_HEADER1;  // 0xAA
	header[1] = UART_FRAME_HEADER2;  // 0x55
	header[2] = UART_FRAME_HEADER3;  // 0xAA
	header[3] = UART_FRAME_HEADER4;  // 0x55
	header[4] = (width >> 8) & 0xFF;
	header[5] = width & 0xFF;
	header[6] = (height >> 8) & 0xFF;
	header[7] = height & 0xFF;
	header[8] = (total_bytes >> 24) & 0xFF;
	header[9] = (total_bytes >> 16) & 0xFF;
	header[10] = (total_bytes >> 8) & 0xFF;
	header[11] = total_bytes & 0xFF;

	HAL_UART_Transmit(&huart1, header, 12, 100);

	offset = 0;
	while (offset < total_bytes)
	{
		uint32_t remaining = total_bytes - offset;
		uint16_t chunk = (remaining > 32768) ? 32768 : (uint16_t)remaining;

		USART1_TxDone = 0;
		HAL_UART_Transmit_DMA(&huart1, (uint8_t *)buffer + offset, chunk);
		
		// 带超时的等待，防止死锁
		timeout_tick = HAL_GetTick();
		while (USART1_TxDone == 0)
		{
			if (HAL_GetTick() - timeout_tick > 2000)  // 2秒超时
			{
				HAL_UART_AbortTransmit(&huart1);  // 强制中止
				return;
			}
		}
		offset += chunk;
	}
}

/***************************************************************************************************
*	�� �� ��: main
*	��ڲ���: ��
*	�� �� ֵ: ��
*	��������: ����������
*	˵    ��: ��
****************************************************************************************************/

int main(void)
{
	MPU_Config();				// MPU����	
	SCB_EnableICache();		// ʹ��ICache
	SCB_EnableDCache();		// ʹ��DCache
	HAL_Init();					// ��ʼ��HAL��
	SystemClock_Config();	// ����ϵͳʱ�ӣ���Ƶ550MHz
	LED_Init();					// ��ʼ��LED����
	USART1_Init();				// USART1初始化	
	USART1_DMA_Init();			// USART1 TX DMA初始化

	SPI_LCD_Init();			// SPI LCD屏幕初始化	
		
	DCMI_OV2640_Init();     // DCMI以及OV2640初始化
	
	OV2640_DMA_Transmit_Continuous(Camera_Buffer, OV2640_BufferSize);  // 启动DMA连续采集

	while (1)
	{
		if (DCMI_FrameState == 1)	// 采集到新一帧图像
		{		
  			DCMI_FrameState = 0;		// 清除标志位

			// 暂停DCMI采集，防止发送期间buffer被覆盖
			OV2640_DCMI_Suspend();

			OV2640_SendFrameToUART((uint16_t *)Camera_Buffer, Display_Width, Display_Height);

			// 发送完毕，恢复DCMI采集
			OV2640_DCMI_Resume();
		}	
	}
}

/****************************************************************************************************/
/**
  *   ϵͳʱ�����ã�
  *
  *            System Clock source            = PLL (HSE)
  *            SYSCLK(Hz)                     = 550000000 (CPU ��Ƶ 550MHz)
  *            HCLK(Hz)                       = 275000000 (AXI and AHBs Clock)
  *            AHB Prescaler                  = 1 (AHB  Clock  275 MHz)
  *            D1 APB3 Prescaler              = 2 (APB3 Clock  137.5MHz)
  *            D2 APB1 Prescaler              = 2 (APB1 Clock  137.5MHz)
  *            D2 APB2 Prescaler              = 2 (APB2 Clock  137.5MHz)
  *            D3 APB4 Prescaler              = 2 (APB4 Clock  137.5MHz)
  *            HSE Frequency(Hz)              = 25000000  (�ⲿ����Ƶ��)
  *            PLL_M                          = 10
  *            PLL_N                          = 220
  *            PLL_P                          = 1
  *
  *				CPU��Ƶ = HSE Frequency / PLL_M * PLL_N / PLL_P = 25M /10*220/1 = 550M
  */   
/****************************************************************************************************/

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  
  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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

  /** Initializes the CPU, AHB and APB buses clocks
  */
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
 
/***************************************** SPI5�ں�ʱ������ **************************************************/
/*** LXB ***
>>>>> ����˵����
	*
	*	1.	SPI6 ���������������ʱ��Ϊ68.5M��������Բ��ģ�723�����ֲ� ��6.3.37С��  SPI interface characteristics
   *
	*	2. Ϊ�˷������ú��û���ֲ��ѡ��Ĭ�ϵ�137.5M����ʱ�� ��Ϊ SPI6 ���ں�ʱ�ӣ�Ȼ����SPI������������Ϊ2��Ƶ�õ� 68.75M ��SCK����ʱ��
   *
   *	3. ��Ȼ��Ļ������ST7789���������������ʱ��Ϊ62.5M����ʵ�ʲ����У���ʹ����Ϊ68.75MҲ�ǳ��ȶ������û�
  *		��SPIʱ�����ϸ�Ҫ�󣬿����޸�SPI�ں�ʱ����ʵ��
   *
*** LXB ***/  
  
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI6;
    PeriphClkInitStruct.Spi6ClockSelection = RCC_SPI6CLKSOURCE_D3PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }  
}


//	����MPU
//
void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct;
  
  HAL_MPU_Disable(); // ����֮ǰ�Ƚ�ֹMPU

  MPU_InitStruct.Enable 				= MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress 			= 0x24000000;
  MPU_InitStruct.Size 					= MPU_REGION_SIZE_512KB;
  MPU_InitStruct.AccessPermission 	= MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable 		= MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsCacheable 			= MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable 			= MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.Number 				= MPU_REGION_NUMBER0;
  MPU_InitStruct.TypeExtField 		= MPU_TEX_LEVEL0;
  MPU_InitStruct.SubRegionDisable 	= 0x00;
  MPU_InitStruct.DisableExec 			= MPU_INSTRUCTION_ACCESS_ENABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);	// ʹ��MCU

}


/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
