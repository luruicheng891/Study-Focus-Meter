#ifndef __USART_H
#define __USART_H

#include "main.h"
#include "stdio.h"

/*-------------------------------------------- USART���ú� ---------------------------------------*/

#define  USART1_BaudRate  115200

#define  USART1_TX_PIN									GPIO_PIN_9								// TX 引脚
#define	USART1_TX_PORT									GPIOA										// TX 引脚端口
#define 	GPIO_USART1_TX_CLK_ENABLE        	   __HAL_RCC_GPIOA_CLK_ENABLE()	 	// TX 时钟使能


#define  USART1_RX_PIN									GPIO_PIN_10             			// RX 引脚
#define	USART1_RX_PORT									GPIOA                 				// RX 引脚端口
#define 	GPIO_USART1_RX_CLK_ENABLE         	   __HAL_RCC_GPIOA_CLK_ENABLE()		// RX 时钟使能


/*---------------------------------------------- 外部变量声明 ---------------------------------------*/

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern volatile uint8_t USART1_TxDone;

/*---------------------------------------------- 函数声明 ---------------------------------------*/

void USART1_Init(void) ;
void USART1_DMA_Init(void);

#endif //__USART_H





