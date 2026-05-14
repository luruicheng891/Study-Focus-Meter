/***
	************************************************************************************************
	*	@version V1.0
	*	@author  ¹С��Ƽ�	
   *************************************************************************************************
   *  @description
	*
	*	ʵ��ƽ̨��¹С��STM32H723ZGT6���İ� ���ͺţ�LXB723ZG-P1��
	* �ͷ�΢�ţ�19949278543
	*
>>>>> �ļ�˵����
	*
	*  ��ʼ��usart���ţ����ò����ʵȲ���
	*
	************************************************************************************************
***/


#include "usart.h"

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_tx;
volatile uint8_t USART1_TxDone = 0;


/*************************************************************************************************
*	�� �� ��:	HAL_UART_MspInit
*	��ڲ���:	huart - UART_HandleTypeDef����ı���������ʾ����Ĵ���
*	�� �� ֵ:	��
*	��������:	��ʼ����������
*	˵    ��:	��		
*************************************************************************************************/


void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	if(huart->Instance==USART1)
	{
		__HAL_RCC_USART1_CLK_ENABLE();		// ���� USART1 ʱ��
		__HAL_RCC_DMA1_CLK_ENABLE();       // ʹ�� DMA1 ʱ��

		GPIO_USART1_TX_CLK_ENABLE;				// ���� USART1 TX ���ŵ� GPIO ʱ��
		GPIO_USART1_RX_CLK_ENABLE;				// ���� USART1 RX ���ŵ� GPIO ʱ��

		GPIO_InitStruct.Pin 			= USART1_TX_PIN;					// TX����
		GPIO_InitStruct.Mode 		= GPIO_MODE_AF_PP;				// �����������
		GPIO_InitStruct.Pull 		= GPIO_PULLUP;						// ����
		GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;	// �ٶȵȼ� 
		GPIO_InitStruct.Alternate 	= GPIO_AF7_USART1;				// ����ΪUSART1
		HAL_GPIO_Init(USART1_TX_PORT, &GPIO_InitStruct);

		GPIO_InitStruct.Pin 			= USART1_RX_PIN;					// RX����
		HAL_GPIO_Init(USART1_RX_PORT, &GPIO_InitStruct);

		HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 1);
		HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	}

}

/*************************************************************************************************
*	�� �� ��:	USART1_Init
*	��ڲ���:	��
*	�� �� ֵ:	��
*	��������:	��ʼ����������
*	˵    ��:	��		 
*************************************************************************************************/

void USART1_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = USART1_BaudRate;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {

  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {

  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {

  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {

  }
}

/*************************************************************************************************
*	�� �� ��:	USART1_DMA_Init
*	��ڲ���:	��
*	�� �� ֵ:	��
*	��������:	��ʼ�� USART1 TX DMA (DMA1_Stream0)
*	˵    ��:	�� HAL_UART_Init() ֮�����
*************************************************************************************************/

void USART1_DMA_Init(void)
{
	hdma_usart1_tx.Instance                 = DMA1_Stream0;
	hdma_usart1_tx.Init.Request             = DMA_REQUEST_USART1_TX;
	hdma_usart1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
	hdma_usart1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
	hdma_usart1_tx.Init.MemInc              = DMA_MINC_ENABLE;
	hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	hdma_usart1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
	hdma_usart1_tx.Init.Mode                = DMA_NORMAL;
	hdma_usart1_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
	hdma_usart1_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

	HAL_DMA_Init(&hdma_usart1_tx);

	__HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);
}

/*************************************************************************************************
*	�� �� ��:	HAL_UART_TxCpltCallback
*	��ڲ���:	huart - UART_HandleTypeDef ָ��
*	�� �� ֵ:	��
*	��������:	UART DMA ������ɻص�
*************************************************************************************************/

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if(huart->Instance == USART1)
	{
		USART1_TxDone = 1;
	}
}

/*************************************************************************************************
*	��Щ���ϣ�����LVGL��Ϊ��Ҫ��__aeabi_assert����TouchGFX�����ܹ�ѡ microLib ��ʹ��printf
*	�������´��룬�ñ�׼C��֧���ض���fput
*  ���ݱ�������ѡ���Ӧ�Ĵ��뼴��
*************************************************************************************************/


//// AC5������ʹ����δ���
//#pragma import(__use_no_semihosting)  
//int _ttywrch(int ch)    
//{
//    ch=ch;
//	return ch;
//}         
//struct __FILE 
//{ 
//	int handle; 

//}; 
//FILE __stdout;       

//void _sys_exit(int x) 
//{ 
//	x = x; 
//} 



// AC6������ʹ����δ���
__asm (".global __use_no_semihosting\n\t");
void _sys_exit(int x) 
{
  x = x;
}
//__use_no_semihosting was requested, but _ttywrch was 
void _ttywrch(int ch)
{
    ch = ch;
}

FILE __stdout;



/*************************************************************************************************
*	�� �� ��:	fputc
*	��ڲ���:	ch - Ҫ������ַ� ��  f - �ļ�ָ�루�����ò�����
*	�� �� ֵ:	����ʱ�����ַ�������ʱ���� EOF��-1��
*	��������:	�ض��� fputc ������Ŀ����ʹ�� printf ����
*	˵    ��:	��		
*************************************************************************************************/

int fputc(int ch, FILE *f)
{
	HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);	// ���͵��ֽ�����
	return (ch);
}
