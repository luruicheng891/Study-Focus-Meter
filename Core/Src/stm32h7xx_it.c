/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void DebugMon_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();

#if (INCLUDE_xTaskGetSchedulerState == 1)
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    xPortSysTickHandler();
  }
#else
  xPortSysTickHandler();
#endif
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/******************************************************************************/

extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef DMA_Handle_dcmi;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern SPI_HandleTypeDef hlcd_spi;

/**
  * @brief  SPI1中断服务函数 (SPI错误处理)
  */
void SPI1_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&hlcd_spi);
}

/**
  * @brief  DMA中断服务函数 (DMA1_Stream0, SPI1_TX LCD使用)
  */
void DMA1_Stream0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi1_tx);
}

/**
  * @brief  DMA中断服务函数 (DMA2_Stream7, DCMI使用)
  */
void DMA2_Stream7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&DMA_Handle_dcmi);
}

/**
  * @brief  DCMI中断服务函数
  */
void DCMI_PSSI_IRQHandler(void)
{
  HAL_DCMI_IRQHandler(&hdcmi);
}
