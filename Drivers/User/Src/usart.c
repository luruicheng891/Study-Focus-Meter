/***
	************************************************************************************************
	*	@version V1.0
	*	@author   小辰科技	
   *************************************************************************************************
   *  @description
	*
	*	实验平台：小辰STM32H723ZGT6核心板（型号：LXB723ZG-P1）
	*   客服微信：19949278543
	*
	*	>>>>> 文件说明：
	*  初始化USART串口，配置参数及DMA等
	*
	************************************************************************************************
***/


#include "usart.h"

// USART1串口句柄，用于管理串口外设的状态和配置
UART_HandleTypeDef huart1;
// USART1 TX DMA句柄，用于管理DMA发送传输
DMA_HandleTypeDef hdma_usart1_tx;
// USART1 DMA发送完成标志，0表示发送中，1表示发送完成（在HAL_UART_TxCpltCallback中断回调函数中置1）
volatile uint8_t USART1_TxDone = 0;


/*************************************************************************************************
*	函数功能: HAL_UART_MspInit - USART外设底层硬件初始化（MSP: MCU Support Package）
*	输入参数: huart - UART句柄指针，包含串口外设的配置信息
*	返回值:   无
*	说明:     该函数由HAL_UART_Init()自动调用，用于配置串口所需的GPIO引脚和时钟
*************************************************************************************************/


void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	if(huart->Instance==USART1)
	{
		// 使能USART1外设时钟
		__HAL_RCC_USART1_CLK_ENABLE();		
		// 使能DMA1时钟（用于USART1 TX DMA传输）
		__HAL_RCC_DMA1_CLK_ENABLE();       

		// 使能USART1 TX引脚对应的GPIO端口时钟
		GPIO_USART1_TX_CLK_ENABLE;				
		// 使能USART1 RX引脚对应的GPIO端口时钟
		GPIO_USART1_RX_CLK_ENABLE;				

		// 配置TX引脚为复用推挽输出，内部上拉，高速，复用功能为USART1
		GPIO_InitStruct.Pin 			= USART1_TX_PIN;					// TX引脚
		GPIO_InitStruct.Mode 		= GPIO_MODE_AF_PP;				// 复用推挽输出模式
		GPIO_InitStruct.Pull 		= GPIO_PULLUP;						// 上拉
		GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;	// 速度等级：极高
		GPIO_InitStruct.Alternate 	= GPIO_AF7_USART1;				// 复用功能为USART1
		HAL_GPIO_Init(USART1_TX_PORT, &GPIO_InitStruct);

		// 配置RX引脚（复用推挽输出，内部上拉，高速，复用功能为USART1）
		GPIO_InitStruct.Pin 			= USART1_RX_PIN;					// RX引脚
		HAL_GPIO_Init(USART1_RX_PORT, &GPIO_InitStruct);

		// 设置DMA1数据流0中断优先级（主优先级0，子优先级1）并使能中断
		HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 1);
		HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	}

}

/*************************************************************************************************
*	函数功能: USART1_Init - 初始化USART1串口
*	输入参数: 无
*	返回值:   无
*	说明:     配置波特率、数据位、停止位、校验位等参数，并禁用FIFO模式
*************************************************************************************************/

void USART1_Init(void)
{
  huart1.Instance = USART1;                                 // 使用USART1外设
  huart1.Init.BaudRate = USART1_BaudRate;                   // 波特率（宏定义在usart.h中）
  huart1.Init.WordLength = UART_WORDLENGTH_8B;              // 数据位：8位
  huart1.Init.StopBits = UART_STOPBITS_1;                   // 停止位：1位
  huart1.Init.Parity = UART_PARITY_NONE;                    // 校验位：无校验
  huart1.Init.Mode = UART_MODE_TX_RX;                       // 模式：发送和接收双工
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;              // 硬件流控制：禁用
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;          // 过采样：16倍
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE; // 单位采样：禁用
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;         // 时钟分频：不分频
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT; // 高级功能：不初始化
  if (HAL_UART_Init(&huart1) != HAL_OK)  // 执行初始化
  {

  }
  // 设置发送FIFO阈值为1/8满
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {

  }
  // 设置接收FIFO阈值为1/8满
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {

  }
  // 禁用FIFO模式（不使用硬件FIFO缓冲）
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {

  }
}

/*************************************************************************************************
*	函数功能: USART1_DMA_Init - 初始化USART1的TX DMA通道
*	输入参数: 无
*	返回值:   无
*	说明:     配置DMA1数据流0用于USART1发送，存储器到外设方向，字节对齐，普通模式
*             此函数应在HAL_UART_Init()之后调用
*************************************************************************************************/

void USART1_DMA_Init(void)
{
	// 使用DMA1数据流0
	hdma_usart1_tx.Instance                 = DMA1_Stream0;
	// DMA请求源为USART1_TX
	hdma_usart1_tx.Init.Request             = DMA_REQUEST_USART1_TX;
	// 传输方向：存储器到外设（发送数据）
	hdma_usart1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
	// 外设地址不递增（始终写入USART数据寄存器）
	hdma_usart1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
	// 存储器地址递增（依次读取发送缓冲区）
	hdma_usart1_tx.Init.MemInc              = DMA_MINC_ENABLE;
	// 外设数据宽度：字节（8位）
	hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	// 存储器数据宽度：字节（8位）
	hdma_usart1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
	// 普通模式（单次传输，传输完成后停止）
	hdma_usart1_tx.Init.Mode                = DMA_NORMAL;
	// DMA优先级：中等
	hdma_usart1_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
	// 禁用FIFO模式
	hdma_usart1_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

	// 初始化DMA
	HAL_DMA_Init(&hdma_usart1_tx);

	// 将DMA句柄链接到USART1句柄的发送DMA
	__HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);
}

/*************************************************************************************************
*	函数功能: HAL_UART_TxCpltCallback - UART DMA发送完成中断回调函数
*	输入参数: huart - UART句柄指针
*	返回值:   无
*	说明:     当USART1的DMA发送完成后由硬件中断调用，设置发送完成标志
*************************************************************************************************/

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if(huart->Instance == USART1)
	{
		USART1_TxDone = 1;  // 设置发送完成标志为1
	}
}

/*************************************************************************************************
*	以下代码用于解决半主机模式问题：
*   LVGL可能需要__aeabi_assert，TouchGFX可能选择microLib，使用printf时需要适配
*	添加以下代码，让标准C库支持重定向fputc
*   根据编译环境选择对应的代码即可
*************************************************************************************************/


// AC5编译器使用如下代码
#pragma import(__use_no_semihosting)    // 禁用半主机模式
int _ttywrch(int ch)                    // 定义_ttywrch函数（AC5要求）
{
    ch=ch;
	return ch;
}         
struct __FILE                           // 定义FILE结构体
{ 
	int handle; 

}; 
FILE __stdout;                          // 定义标准输出文件

void _sys_exit(int x)                   // 定义_sys_exit函数
{ 
	x = x; 
} 



//// AC6编译器使用如下代码
//__asm (".global __use_no_semihosting\n\t");  // 汇编声明：禁用半主机模式
//void _sys_exit(int x)                         // 定义_sys_exit函数（避免链接错误）
//{
//  x = x;
//}
//// 注释：请求了__use_no_semihosting，但_ttywrch被调用了
//void _ttywrch(int ch)                         // 定义_ttywrch函数（AC6要求）
//{
//    ch = ch;
//}

//FILE __stdout;                                // 定义标准输出文件



/*************************************************************************************************
*	函数功能: fputc - 重定向fputc函数，实现printf输出
*	输入参数: ch - 要发送的字符, f - 文件指针（该参数未使用）
*	返回值:   成功时返回字符，失败时返回EOF（-1）
*	说明:     将标准C库的fputc重定向到USART1串口发送，使printf函数可以通过串口输出
*************************************************************************************************/

int fputc(int ch, FILE *f)
{
	// 通过USART1发送单个字符（阻塞模式，超时时间100ms）
	HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
	return (ch);
}
