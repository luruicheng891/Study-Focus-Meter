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

/* USART3 (ESP-01) 已迁移到 Communication/UART_ESP01.c
 *   - hdma_usart3_rx 改为驱动内 static, 通过 huart3.hdmarx 访问 */
extern UART_HandleTypeDef huart3;
extern void USART3_IRQ_Handler(void);

/* USART2 (ESP32 BLE 透传) DMA 接收 */
extern UART_HandleTypeDef huart2;
extern void USART2_IRQ_Handler(void);

/* USART1 (调试 + 命令接收) */
extern UART_HandleTypeDef huart1;
extern void USART1_IRQ_Handler(void);

/* RTC (低功耗周期唤醒, PowerManager 使用) */
extern RTC_HandleTypeDef hrtc;

/**
  * @brief  EXTI Line1 中断 (触摸 PEN = PB1, Stop 模式触摸唤醒源)
  * @note   平时由 PowerManager 在进 Stop 前才使能, 醒后立即关闭。
  *         此处只清标志, 不做业务逻辑 (真正的唤醒处理在 Power_Task 中)。
  */
void EXTI1_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
}

/**
  * @brief  RTC 唤醒定时器中断 (Stop 模式周期"嗅探"唤醒源)
  */
void RTC_WKUP_IRQHandler(void)
{
  HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
}

/**
  * @brief  USART1 中断 (单字节命令接收: 'C'/'W' 切换显示模式)
  */
void USART1_IRQHandler(void)
{
  USART1_IRQ_Handler();
}

/**
  * @brief  USART3 中断 (ESP-01 接收, IDLE 检测)
  */
void USART3_IRQHandler(void)
{
  USART3_IRQ_Handler();
}

/**
  * @brief  DMA1_Stream1 中断 (USART3_RX 使用)
  *         DMA 句柄通过 __HAL_LINKDMA 关联到 huart3.hdmarx, 此处直接访问。
  */
void DMA1_Stream1_IRQHandler(void)
{
  if (huart3.hdmarx != NULL)
  {
    HAL_DMA_IRQHandler(huart3.hdmarx);
  }
}

/**
  * @brief  USART2 中断 (ESP32 BLE 透传, IDLE 检测)
  */
void USART2_IRQHandler(void)
{
  USART2_IRQ_Handler();
}

/**
  * @brief  DMA1_Stream3 中断 (USART2_RX 使用)
  *         驱动 huart2.hdmarx 通过 __HAL_LINKDMA 关联,
  *         直接读 huart2.hdmarx 即可避免再 extern 一份句柄。
  */
void DMA1_Stream3_IRQHandler(void)
{
  if (huart2.hdmarx != NULL)
  {
    HAL_DMA_IRQHandler(huart2.hdmarx);
  }
}

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
