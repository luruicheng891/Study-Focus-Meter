#include "dcmi_ov2640.h"  
#include "dcmi_ov2640_cfg.h"  

// DCMI句柄结构体，用于管理DCMI外设的状态和配置
DCMI_HandleTypeDef   hdcmi;            
// DMA句柄结构体，用于管理DMA传输的状态和配置
DMA_HandleTypeDef    DMA_Handle_dcmi;  

// DCMI帧状态标志，0表示空闲，1表示接收完一帧数据（在HAL_DCMI_FrameEventCallback()中断回调函数中置1）
volatile uint8_t DCMI_FrameState = 0;  
// OV2640摄像头帧率变量
volatile uint8_t OV2640_FPS ;          

/*************************************************************************************************
*	函数功能: HAL_DCMI_MspInit - DCMI外设底层硬件初始化（MSP: MCU Support Package）
*	输入参数: hdcmi - DCMI句柄指针，包含DCMI外设的配置信息
*	返回值:   无
*	说明:     该函数由HAL_DCMI_Init()自动调用，用于配置DCMI外设所需的GPIO引脚和时钟
*************************************************************************************************/
void HAL_DCMI_MspInit(DCMI_HandleTypeDef* hdcmi)
{
   GPIO_InitTypeDef GPIO_InitStruct = {0};

   if(hdcmi->Instance==DCMI)
   {
      // 使能DCMI外设时钟
      __HAL_RCC_DCMI_CLK_ENABLE();      

      // 使能DCMI数据引脚对应的GPIO端口时钟
      __HAL_RCC_GPIOE_CLK_ENABLE();  
      __HAL_RCC_GPIOA_CLK_ENABLE();
      __HAL_RCC_GPIOC_CLK_ENABLE();
      __HAL_RCC_GPIOD_CLK_ENABLE();
      __HAL_RCC_GPIOG_CLK_ENABLE();

      // 使能PWDN控制引脚对应的GPIO端口时钟
      GPIO_OV2640_PWDN_CLK_ENABLE;     

/****************************************************************************  
   DCMI数据接口引脚分配:
   PG9  ------> DCMI_VSYNC (垂直同步信号)
   PH8  ------> DCMI_HSYNC (水平同步信号，注: 代码中未配置此引脚，可能在其他位置)
   PA6  ------> DCMI_PIXCLK (像素时钟)
   PE4  ------> DCMI_D4 (数据位4)
   PE5  ------> DCMI_D6 (数据位6)
   PE6  ------> DCMI_D7 (数据位7)
   PA4  ------> DCMI_D0 (数据位0，注: 代码注释与配置不符)
   PA6  ------> DCMI_D2 (数据位2)
   PC6  ------> DCMI_D0 (数据位0)
   PC7  ------> DCMI_D1 (数据位1)
   PD3  ------> DCMI_D5 (数据位5)
   PG9  ------> DCMI_VSYNC (垂直同步)
   PG10 ------> DCMI_D1 (数据位1，注: 代码注释与配置不符)
   PG11 ------> DCMI_D2 (数据位2，注: 代码注释与配置不符)

   SCCB控制引脚 (在 camera_sccb.c 文件中初始化):
   PH7  ------> SCCB_SCL
   PH13 ------> SCCB_SDA 

   电源控制引脚:
   PH15 ------> PWDN (掉电控制)
******************************************************************************/ 

   // 配置PE4、PE5、PE6引脚为DCMI数据线(D4, D6, D7)，复用推挽输出，无上下拉，高速
   GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    // 配置PA4、PA6引脚为DCMI数据线(D0, D2)和像素时钟(PIXCLK)
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 配置PC6、PC7引脚为DCMI数据线(D0, D1)
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // 配置PD3引脚为DCMI数据线(D5)
    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    // 配置PG9、PG10、PG11引脚为DCMI VSYNC和数据线(D1, D2)
    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
	 
   // 初始化PWDN掉电控制引脚
   OV2640_PWDN_ON;  // 先输出高电平，使摄像头进入掉电模式，停止工作

   // 配置PWDN引脚为通用推挽输出模式，内部上拉，低速
   GPIO_InitStruct.Pin       = OV2640_PWDN_PIN;        // PWDN引脚
   GPIO_InitStruct.Mode    = GPIO_MODE_OUTPUT_PP;      // 推挽输出模式
   GPIO_InitStruct.Pull    = GPIO_PULLUP;               // 上拉
   GPIO_InitStruct.Speed   = GPIO_SPEED_FREQ_LOW;      // 低速
   HAL_GPIO_Init(OV2640_PWDN_PORT, &GPIO_InitStruct);   // 初始化PWDN引脚
  }
}


/***************************************************************************************************************************************
*	函数功能: MX_DCMI_Init - 初始化DCMI外设
*	说明:     配置为8位数据模式，全帧捕获，硬件同步
*****************************************************************************************************************************************/
void MX_DCMI_Init(void)
{
   // 配置DCMI参数
   hdcmi.Instance                = DCMI;
   hdcmi.Init.SynchroMode        = DCMI_SYNCHRO_HARDWARE;      // 硬件同步模式，使用外部VS和HS信号进行帧同步
   hdcmi.Init.PCKPolarity        = DCMI_PCKPOLARITY_RISING;    // 像素时钟上升沿有效，在上升沿采集数据
   hdcmi.Init.VSPolarity         = DCMI_VSPOLARITY_LOW;        // 垂直同步信号(VS)低电平有效
   hdcmi.Init.HSPolarity         = DCMI_HSPOLARITY_LOW;        // 水平同步信号(HS)低电平有效
   hdcmi.Init.CaptureRate        = DCMI_CR_ALL_FRAME;          // 捕获所有帧，不跳帧
   hdcmi.Init.ExtendedDataMode   = DCMI_EXTEND_DATA_8B;        // 8位数据模式，每次捕获8位数据
   hdcmi.Init.JPEGMode           = DCMI_JPEG_DISABLE;          // 禁用JPEG模式，使用普通模式
   hdcmi.Init.ByteSelectMode     = DCMI_BSM_ALL;               // 字节选择模式：捕获所有字节
   hdcmi.Init.ByteSelectStart    = DCMI_OEBS_ODD;              // 从奇数字节开始捕获（对于8位模式，从第一个字节开始）
   hdcmi.Init.LineSelectMode     = DCMI_LSM_ALL;               // 行选择模式：捕获所有行
   hdcmi.Init.LineSelectStart    = DCMI_OELS_ODD;              // 从奇数行开始捕获（从第一行开始）
   HAL_DCMI_Init(&hdcmi);

   // 设置DCMI中断优先级并启用
   HAL_NVIC_SetPriority(DCMI_IRQn, 0 ,5);    // 设置中断优先级（主优先级0，子优先级5）
   HAL_NVIC_EnableIRQ(DCMI_IRQn);            // 使能DCMI中断
	
   // 注意：JPG模式需要取消注释下面这行来启用帧中断
   // __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME); // 使能FRAME帧完成中断
}

/***************************************************************************************************************************************
*	函数功能: OV2640_DMA_Init - 初始化DMA用于DCMI数据传输
*	说明:     使用DMA2数据流7，外设到存储器模式，32位传输，循环模式
*****************************************************************************************************************************************/
void OV2640_DMA_Init(void)
{
   __HAL_RCC_DMA2_CLK_ENABLE();   // 使能DMA2时钟

   // 配置DMA参数
   DMA_Handle_dcmi.Instance                     = DMA2_Stream7;               // 使用DMA2数据流7
   DMA_Handle_dcmi.Init.Request                 = DMA_REQUEST_DCMI;           // DMA请求源为DCMI
   DMA_Handle_dcmi.Init.Direction               = DMA_PERIPH_TO_MEMORY;       // 传输方向：外设(DCMI)到存储器
   DMA_Handle_dcmi.Init.PeriphInc               = DMA_PINC_DISABLE;           // 外设地址不递增（始终读取DCMI数据寄存器）
   DMA_Handle_dcmi.Init.MemInc                  = DMA_MINC_ENABLE;            // 存储器地址递增（连续存入缓冲区）
   DMA_Handle_dcmi.Init.PeriphDataAlignment     = DMA_PDATAALIGN_WORD;        // 外设数据宽度32位（一个字）
   DMA_Handle_dcmi.Init.MemDataAlignment        = DMA_MDATAALIGN_WORD;        // 存储器数据宽度32位
   DMA_Handle_dcmi.Init.Mode                    = DMA_CIRCULAR;               // 循环模式，用于连续采集
   DMA_Handle_dcmi.Init.Priority                = DMA_PRIORITY_LOW;           // DMA优先级为低
   DMA_Handle_dcmi.Init.FIFOMode                = DMA_FIFOMODE_ENABLE;        // 使能FIFO模式
   DMA_Handle_dcmi.Init.FIFOThreshold           = DMA_FIFO_THRESHOLD_FULL;    // FIFO阈值为满（4个字，即16字节）
   DMA_Handle_dcmi.Init.MemBurst                = DMA_MBURST_SINGLE;          // 存储器突发传输为单次传输
   DMA_Handle_dcmi.Init.PeriphBurst             = DMA_PBURST_SINGLE;          // 外设突发传输为单次传输

   HAL_DMA_Init(&DMA_Handle_dcmi);                        // 初始化DMA
   __HAL_LINKDMA(&hdcmi, DMA_Handle, DMA_Handle_dcmi);    // 将DMA句柄链接到DCMI句柄
   HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 0, 0);         // 设置DMA中断优先级
   HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);                 // 使能DMA中断
}


/***************************************************************************************************************************************
*	函数功能: OV2640_Delay - 简单延时函数（软件延时）
*	输入参数: Delay - 延时常数（基于循环计数，非精确毫秒延时）
*	说明:     用于移植时替换为HAL_Delay或RTOS延时函数
*****************************************************************************************************************************************/
void OV2640_Delay(volatile uint32_t Delay)
{
   volatile uint16_t i;

   // 软件延时，通过嵌套循环消耗时间
   while (Delay --)            
   {
      for (i = 0; i < 20000; i++);
   }   
   // 如果需要精确延时，可替换为 HAL_Delay(Delay);
}

/***************************************************************************************************************************************
*	函数功能: DCMI_OV2640_Init - 初始化OV2640摄像头
*	返回值:   OV2640_Success(0) 成功 / OV2640_Error 失败
*	说明:     依次初始化SCCB、DCMI、DMA，然后复位OV2640并读取ID验证
*****************************************************************************************************************************************/
int8_t DCMI_OV2640_Init(void)
{
   uint16_t Device_ID;  // 存储读取到的摄像头ID

   SCCB_GPIO_Config();                    // 初始化SCCB总线GPIO（I2C模拟）
   MX_DCMI_Init();                        // 初始化DCMI外设
   OV2640_DMA_Init();                     // 初始化DMA传输通道
   OV2640_Reset();                        // 执行摄像头硬件复位
   Device_ID = OV2640_ReadID();           // 通过SCCB读取摄像头ID

   // 检查设备ID是否匹配（OV2640的ID为0x2640或0x2642）
   if( (Device_ID == 0x2640) || (Device_ID == 0x2642) )
   {
      printf ("OV2640 OK,ID:0x%X\r\n",Device_ID);  // 打印成功信息

      // 配置摄像头工作模式
      OV2640_Config( OV2640_SVGA_Config );               // 使用SVGA模式（800*600@30帧）
      // OV2640_Config( OV2640_UXGA_Config );             // 可选UXGA模式（1600*1200@15帧）

      OV2640_Set_Framesize(OV2640_Width,OV2640_Height);  // 设置输出图像尺寸
      OV2640_DCMI_Crop( Display_Width, Display_Height, OV2640_Width, OV2640_Height ); // 设置裁剪区域，使图像适配屏幕

      return OV2640_Success;  // 返回初始化成功标志
   }
   else
   {
      printf ("OV2640 ERROR!!!!!  ID:%X\r\n",Device_ID); // 打印错误信息
      return  OV2640_Error;   // 返回初始化失败标志
   }   
}

/***************************************************************************************************************************************
*	函数功能: OV2640_DMA_Transmit_Continuous - 启动DMA连续传输模式
*	输入参数: DMA_Buffer     - DMA接收缓冲区的基地址（32位地址）
*            DMA_BufferSize - 缓冲区大小（以32位字为单位，即总字节数/4）
*	说明:     1. 启动后DMA会持续循环传输，直到手动停止DCMI
*            2. OV2640使用RGB565模式时，每个像素需要2字节存储
*            3. 因为DMA配置为32位传输，所以DMA_BufferSize需要除以4：
*               例如：要采集240*240的图像，需要240*240*2=115200字节，
*               则DMA_BufferSize = 115200 / 4 = 28800
*****************************************************************************************************************************************/
void OV2640_DMA_Transmit_Continuous(uint32_t DMA_Buffer,uint32_t DMA_BufferSize)
{
   DMA_Handle_dcmi.Init.Mode  = DMA_CIRCULAR;  // 设置为循环模式

   HAL_DMA_Init(&DMA_Handle_dcmi);    // 重新初始化DMA以应用新模式

   // 启动DCMI连续模式DMA传输
   HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS, (uint32_t)DMA_Buffer,DMA_BufferSize);
}

/***************************************************************************************************************************************
*	函数功能: OV2640_DMA_Transmit_Snapshot - 启动DMA单帧快照传输模式
*	输入参数: DMA_Buffer     - DMA接收缓冲区的基地址（32位地址）
*            DMA_BufferSize - 缓冲区大小（以32位字为单位，即总字节数/4）
*	说明:     1. 只采集一帧图像后自动停止DMA传输
*            2. OV2640使用RGB565模式时，每个像素需要2字节存储
*            3. 因为DMA配置为32位传输，所以DMA_BufferSize需要除以4
*            4. 使用此模式后如需再次采集，需调用OV2640_DCMI_Resume()恢复DCMI
*****************************************************************************************************************************************/
void OV2640_DMA_Transmit_Snapshot(uint32_t DMA_Buffer,uint32_t DMA_BufferSize)
{
   DMA_Handle_dcmi.Init.Mode  = DMA_NORMAL;  // 设置为普通模式（单次传输）

   HAL_DMA_Init(&DMA_Handle_dcmi);    // 重新初始化DMA以应用新模式

   // 启动DCMI快照模式DMA传输
   HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)DMA_Buffer,DMA_BufferSize);
}


/***************************************************************************************************************************************
*	函数功能: OV2640_DCMI_Suspend - 暂停DCMI捕获
*	说明:     1. 在连续捕获模式下，调用此函数可暂时停止DCMI捕获
*            2. 之后可调用OV2640_DCMI_Resume()恢复捕获
*            3. 需要注意：暂停期间DMA并未停止，只是DCMI不再产生新的传输请求
*****************************************************************************************************************************************/
void OV2640_DCMI_Suspend(void) 
{
   HAL_DCMI_Suspend(&hdcmi);    // 暂停DCMI捕获
}

/***************************************************************************************************************************************
*	函数功能: OV2640_DCMI_Resume - 恢复DCMI捕获
*	说明:     1. 当DCMI被暂停后，调用此函数可恢复捕获
*            2. 使用OV2640_DMA_Transmit_Snapshot()单帧模式后，DCMI也会被暂停，再次调用此函数可重新启动捕获
*****************************************************************************************************************************************/
void  OV2640_DCMI_Resume(void) 
{
   (&hdcmi)->State = HAL_DCMI_STATE_BUSY;       // 设置DCMI状态为忙碌
   (&hdcmi)->Instance->CR |= DCMI_CR_CAPTURE;   // 直接操作寄存器使能DCMI捕获位
}

/***************************************************************************************************************************************
*	函数功能: OV2640_DCMI_Stop - 停止DCMI和DMA传输，并关闭DCMI时钟
*****************************************************************************************************************************************/
void  OV2640_DCMI_Stop(void) 
{
   HAL_DCMI_Stop(&hdcmi);  // 停止DCMI捕获，并停止关联的DMA传输
}

/***************************************************************************************************************************************
*	函数功能: OV2640_DCMI_Crop - 配置DCMI裁剪窗口，使传感器图像适配显示屏尺寸
*	输入参数: Displey_XSize, Displey_YSize - 显示屏的宽度和高度
*           Sensor_XSize, Sensor_YSize    - 传感器输出图像的宽度和高度
*	说明:     1. 当传感器和显示屏分辨率不一致时，通过DCMI硬件裁剪居中显示
*            2. 注意：图像宽度必须能被4整除，可通过OV2640_Set_Framesize调整
*            3. DCMI的水平有效像素宽度也必须能被4整除
*            4. 该函数为硬件裁剪，几乎不消耗CPU资源
*****************************************************************************************************************************************/
int8_t OV2640_DCMI_Crop(uint16_t Displey_XSize,uint16_t Displey_YSize,uint16_t Sensor_XSize,uint16_t Sensor_YSize )
{
   uint16_t DCMI_X_Offset;  // 水平偏移量（从第几个像素开始捕获）
   uint16_t DCMI_Y_Offset;  // 垂直偏移量（从第几行开始捕获）
   uint16_t DCMI_CAPCNT;    // 水平有效像素计数（每行捕获多少个像素）
   uint16_t DCMI_VLINE;     // 垂直有效行数（总共捕获多少行）

   // 检查输入参数：显示尺寸必须小于传感器输出尺寸
   if( (Displey_XSize>=Sensor_XSize)|| (Displey_YSize>=Sensor_YSize) )
   {
      // printf("实际显示尺寸大于或等于传感器输出尺寸，无法进行DCMI裁剪\r\n");
      return OV2640_Error;  // 返回错误，当前裁剪函数不支持放大，需直接按原尺寸显示
   }
   
   // 计算水平偏移量
   // RGB565模式下，每个像素占2个字节，需要2个PCLK时钟周期
   // 注意：裁剪的偏移量必须以2个像素(4字节)为单位，所以Sensor_XSize - LCD_XSize后要取偶数
   DCMI_X_Offset = Sensor_XSize - Displey_XSize;  // 实际为(Sensor_XSize - LCD_XSize) / 2 * 2，此处简化

   // 计算垂直偏移量
   // 垂直方向居中裁剪，偏移量 = (传感器高度 - 显示高度) / 2
   // DCMI计数从0开始，所以需要减1
   DCMI_Y_Offset = (Sensor_YSize - Displey_YSize)/2 - 1;

   // 计算水平有效像素计数
   // RGB565模式下每个像素需要2个PCLK，所以计数 = 显示宽度 * 2
   // DCMI计数从0开始，所以需要减1；总计数需能被4整除
   DCMI_CAPCNT = Displey_XSize * 2 - 1;
   
   // 计算垂直有效行数
   // DCMI行计数从0开始，所以需要减1
   DCMI_VLINE = Displey_YSize - 1;
   
   // printf("%d  %d  %d  %d\r\n",DCMI_X_Offset,DCMI_Y_Offset,DCMI_CAPCNT,DCMI_VLINE);
   
   HAL_DCMI_ConfigCrop(&hdcmi, DCMI_X_Offset, DCMI_Y_Offset, DCMI_CAPCNT, DCMI_VLINE);  // 配置DCMI裁剪参数
   HAL_DCMI_EnableCrop(&hdcmi);  // 使能DCMI硬件裁剪功能

   return OV2640_Success;   
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Reset - 执行OV2640硬件复位
*	说明:     1. 每次初始化OV2640前都需要执行一次硬件复位，确保寄存器处于默认状态
*****************************************************************************************************************************************/
void OV2640_Reset(void)
{
   OV2640_Delay(5);  // 上电后延时约5ms，等待电源稳定后再拉低PWDN
    
   OV2640_PWDN_OFF;  // PWDN拉低，使摄像头退出掉电模式，开始工作（蓝色LED会亮起）

   // OV2640上电后需要等待至少3ms（规格书要求），但硬件RC复位电路需要约6ms
   // 为确保可靠复位，此处延时5ms
   OV2640_Delay(5);    
   
   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_SENSOR);   // 选择SENSOR寄存器组
   SCCB_WriteReg(OV2640_SENSOR_COM7, 0x80);                  // 向COM7寄存器写入0x80，执行软件复位

   // OV2640软件复位需要至少2ms，且执行SCCB写操作后需额外延时
   // 此处延时10ms确保复位完成
   OV2640_Delay(10);    
}

/***************************************************************************************************************************************
*	函数功能: OV2640_ReadID - 读取OV2640摄像头ID
*	返回值:   16位设备ID（高8位和低8位组合）
*	说明:     实际读取的ID可能是0x2640或0x2642，不同批次可能略有差异
*****************************************************************************************************************************************/
uint16_t OV2640_ReadID(void)
{
   uint8_t PID_H, PID_L;     // 高字节ID和低字节ID
   
   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_SENSOR);   // 选择SENSOR寄存器组

   PID_H = SCCB_ReadReg(OV2640_SENSOR_PIDH);  // 读取ID高字节
   PID_L = SCCB_ReadReg(OV2640_SENSOR_PIDL);  // 读取ID低字节
   
   return (PID_H << 8) | PID_L;  // 组合成16位ID并返回
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Config - 配置OV2640寄存器组
*	输入参数: (*ConfigData)[2] - 配置数组指针，如OV2640_SVGA_Config或OV2640_UXGA_Config
*	说明:     1. 配置数组定义在dcmi_ov2640_cfg.h文件中
*            2. SVGA模式分辨率为800*600，帧率约30帧
*            3. UXGA模式分辨率为1600*1200，帧率约15帧
*            4. 数组每行两个元素：[寄存器地址, 寄存器值]
*****************************************************************************************************************************************/
void OV2640_Config( const uint8_t (*ConfigData)[2] )
{
   uint32_t i;  // 循环计数器

   // 遍历配置数组，直到遇到[0,0]结束标志
   for( i=0; ConfigData[i][0]; i++)
   {
      SCCB_WriteReg(ConfigData[i][0], ConfigData[i][1]);  // 向指定寄存器写入配置值
   } 
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Pixformat - 设置OV2640输出像素格式
*	输入参数: pixformat - 像素格式选择，可选Pixformat_RGB565或Pixformat_JPEG
*****************************************************************************************************************************************/
void OV2640_Set_Pixformat(uint8_t pixformat)
{
   const uint8_t (*ConfigData)[2];  // 指向配置数组的指针
   uint32_t i;  // 循环计数器

   // 根据选择的像素格式，加载对应的配置数组
    switch (pixformat) 
    {
        case Pixformat_RGB565:
            ConfigData = OV2640_RGB565_Config;  // RGB565格式配置
            break;
        case Pixformat_JPEG:
            ConfigData = OV2640_JPEG_Config;    // JPEG格式配置
            break;
        default:  break;
    }

   // 遍历配置数组并写入寄存器
   for( i=0; ConfigData[i][0]; i++)
   {
      SCCB_WriteReg(ConfigData[i][0], ConfigData[i][1]);  // 逐条写入配置
   } 
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Framesize - 设置OV2640实际输出图像尺寸
*	输入参数: width  - 实际输出图像宽度
*           height - 实际输出图像高度
*	说明:     1. OV2640默认为SVGA(800*600)或UXGA(1600*1200)模式，直接缩放会降低画质
*             2. 注意：设置的宽度和高度都必须能被4整除
*             3. 图像尺寸越小帧率越高，但帧率上限受当前工作模式限制（如SVGA模式最大约30帧）
*****************************************************************************************************************************************/
int8_t OV2640_Set_Framesize(uint16_t width,uint16_t height)
{
   // 检查宽度和高度是否能被4整除（DCMI要求）
   if( (width%4) || (height%4) )
   {
       return OV2640_Error;  // 返回错误码
   }

   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_DSP);  // 选择DSP寄存器组

   // 配置实际图像宽度（OUTW寄存器）
   // 0x5A寄存器存放宽度低8位，实际值为 width/4
   SCCB_WriteReg(0X5A, width/4 & 0XFF);
   
   // 配置实际图像高度（OUTH寄存器）
   // 0x5B寄存器存放高度低8位，实际值为 height/4
   SCCB_WriteReg(0X5B, height/4 & 0XFF);
   
   // 配置尺寸高位（ZMHH寄存器）
   // Bit[2:0]存放OUTH的bit[8]和OUTW的bit[9:8]
   SCCB_WriteReg(0X5C, (width/4>>8 & 0X03) | (height/4>>6 & 0x04));

   SCCB_WriteReg(OV2640_DSP_RESET, 0X00);  // 复位DSP使新尺寸生效

   return OV2640_Success;  // 配置成功
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Horizontal_Mirror - 设置图像水平镜像
*	输入参数: ConfigState - OV2640_Enable(1) 使能水平镜像 / 其他值 恢复正常
*	说明:     当ConfigState为1时，图像左右翻转；为0时恢复正常
*****************************************************************************************************************************************/
int8_t OV2640_Set_Horizontal_Mirror( int8_t ConfigState )
{
   uint8_t OV2640_Reg;  // 存储寄存器当前值

   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_SENSOR);  // 选择SENSOR寄存器组
   OV2640_Reg = SCCB_ReadReg(OV2640_SENSOR_REG04);          // 读取0x04寄存器的当前值

   // REG04寄存器的Bit[7]控制水平镜像功能
   // Bit[7] = 1: 使能水平镜像（图像左右翻转）
   // Bit[7] = 0: 恢复正常图像
   if (ConfigState == OV2640_Enable)    // 使能水平镜像
   { 
      OV2640_Reg |= 0X80;  // 将Bit[7]置1
   } 
   else                    // 恢复正常（取消镜像）
   {
      OV2640_Reg &= ~0X80; // 将Bit[7]清零
   }
   
   return SCCB_WriteReg(OV2640_SENSOR_REG04, OV2640_Reg);  // 写回寄存器并返回操作结果
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Vertical_Flip - 设置图像垂直翻转
*	输入参数: ConfigState - OV2640_Enable(1)时图像上下翻转 / 其他值时恢复正常
*	说明:     当ConfigState为1时，图像垂直翻转（上下颠倒）；为0时恢复正常
*****************************************************************************************************************************************/
int8_t OV2640_Set_Vertical_Flip( int8_t ConfigState )
{
   uint8_t OV2640_Reg;  // 存储寄存器当前值

   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_SENSOR);  // 选择SENSOR寄存器组
   OV2640_Reg = SCCB_ReadReg(OV2640_SENSOR_REG04);          // 读取0x04寄存器的当前值

   // REG04寄存器的Bit[6]控制垂直翻转功能
   // Bit[6] = 1: 使能垂直翻转（图像上下颠倒）
   // Bit[6] = 0: 恢复正常图像
   if (ConfigState == OV2640_Enable)   
   { 
      // 参考OpenMV的参数配置
      // Bit[4]也需要同时置1，否则垂直翻转后图像颜色会异常
      OV2640_Reg |= 0X40 | 0x10 ;     // 同时置位Bit[6]和Bit[4]
   } 
   else   // 取消垂直翻转，恢复正常
   {
      OV2640_Reg &= ~(0X40 | 0x10);   // 清零Bit[6]和Bit[4]
   }
   return SCCB_WriteReg(OV2640_SENSOR_REG04, OV2640_Reg);   // 写回寄存器并返回操作结果
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Saturation - 设置图像饱和度
*	输入参数: Saturation - 饱和度级别，可选值为5级：2, 1, 0, -1, -2
*	说明:     1. 该函数直接使用OV2640 DSP内部SDE配置参数，范围从-2到2共5级
*            2. 饱和度越高，色彩越鲜艳浓郁；饱和度越低，色彩越暗淡接近灰色
*****************************************************************************************************************************************/
void OV2640_Set_Saturation(int8_t Saturation)
{
   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_DSP);  // 选择DSP寄存器组

   switch (Saturation)
   {
      case 2:      // 饱和度+2（最高）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);     // 设置SDE地址
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x02);     // 选择饱和度控制参数
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x03);     // 设置SDE参数地址
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x68);     // 饱和度值0x68
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x68);     // 再次写入确保生效
      break;
      
      case 1:      // 饱和度+1（较高）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x02);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x03);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x58);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x58);
      break;

      case 0:      // 饱和度0（默认）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x02);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x03);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x48);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x48);
      break;

      case -1:     // 饱和度-1（较低）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x02);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x03);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x38);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x38);
      break;

      case -2:     // 饱和度-2（最低，接近灰度）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x02);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x03);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x28);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x28);
      break;

      default: break;  // 无效参数，不进行任何操作
   }
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Brightness - 设置图像亮度
*	输入参数: Brightness - 亮度级别，可选值为5级：2, 1, 0, -1, -2
*	说明:     1. 该函数直接使用OV2640 DSP内部SDE配置参数，范围从-2到2共5级
*            2. 亮度越高，画面整体越亮；亮度越低，画面越暗
*****************************************************************************************************************************************/
void OV2640_Set_Brightness(int8_t Brightness)
{
   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_DSP);  // 选择DSP寄存器组

   switch (Brightness)
   {
      case 2:      // 亮度+2（最亮）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);     // 设置SDE地址
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);     // 选择亮度控制参数
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x09);     // 设置SDE参数地址
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x40);     // 亮度值0x40
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x00);     // 亮度微调值0x00
      break;
      
      case 1:      // 亮度+1（较亮）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x09);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x30);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x00);
      break;

      case 0:      // 亮度0（默认）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x09);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x20);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x00);
      break;

      case -1:     // 亮度-1（较暗）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x09);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x10);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x00);
      break;

      case -2:     // 亮度-2（最暗）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x09);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x00);
      break;

      default: break;  // 无效参数，不进行任何操作
   }
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Contrast - 设置图像对比度
*	输入参数: Contrast - 对比度级别，可选值为5级：2, 1, 0, -1, -2
*	说明:     1. 该函数直接使用OV2640 DSP内部SDE配置参数，范围从-2到2共5级
*            2. 对比度越高，明暗反差越大；对比度越低，画面越柔和
*****************************************************************************************************************************************/
void OV2640_Set_Contrast(int8_t Contrast)
{
   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_DSP);  // 选择DSP寄存器组

   switch (Contrast)
   {
      case 2:      // 对比度+2（最高）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);     // 设置SDE地址
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);     // 选择对比度控制参数
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x07);     // 设置SDE参数地址1
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x20);     // 对比度配置值1
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x28);     // 设置SDE参数地址2
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x0c);     // 对比度配置值2
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x06);     // 对比度配置值3
      break;
      
      case 1:      // 对比度+1（较高）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x07);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x20);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x24);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x16);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x06);
      break;

      case 0:      // 对比度0（默认）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x07);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x20);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x20);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x20);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x06);
      break;

      case -1:     // 对比度-1（较低）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x07);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x20);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x1c);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x2a);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x06);
      break;

      case -2:     // 对比度-2（最低）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x04);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x07);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x20);
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x18);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x34);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x06);
      break;

      default: break;  // 无效参数，不进行任何操作
   }
}

/***************************************************************************************************************************************
*	函数功能: OV2640_Set_Effect - 设置图像特效模式
*	输入参数: effect_Mode - 特效模式选择，可选值：
*                          OV2640_Effect_Normal       - 正常模式
*                          OV2640_Effect_Negative     - 负片模式（反色）
*                          OV2640_Effect_BW           - 黑白模式
*                          OV2640_Effect_BW_Negative  - 黑白负片模式（黑白反色）
*	说明:     该函数直接使用OV2640 DSP内部SDE配置参数
*****************************************************************************************************************************************/
void OV2640_Set_Effect(uint8_t effect_Mode)
{
   SCCB_WriteReg(OV2640_SEL_Registers, OV2640_SEL_DSP);  // 选择DSP寄存器组

   switch (effect_Mode)
   {
      case OV2640_Effect_Normal:       // 正常模式
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);     // 设置SDE地址
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x00);     // 特效控制参数：正常
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x05);     // 设置SDE参数地址
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);     // 特效配置值
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);     // 再次写入确保生效
      break;
      
      case OV2640_Effect_Negative:     // 负片模式（颜色全取反）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x40);     // 特效控制参数：负片
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x05);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);
      break;

      case OV2640_Effect_BW:          // 黑白模式
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x18);     // 特效控制参数：黑白
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x05);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);
      break;

      case OV2640_Effect_BW_Negative: // 黑白负片模式（黑白反色）
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x00);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x58);     // 特效控制参数：黑白负片
         SCCB_WriteReg(OV2640_DSP_BPADDR, 0x05);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);
         SCCB_WriteReg(OV2640_DSP_BPDATA, 0x80);
      break;

      default: break;  // 无效参数，不进行任何操作
   }
}

/***************************************************************************************************************************************
*	函数功能: HAL_DCMI_FrameEventCallback - DCMI帧完成中断回调函数
*	说明:     每完整接收到一帧图像数据后由硬件中断调用此函数，用于更新帧率计数和帧完成标志
*****************************************************************************************************************************************/
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
   static uint32_t DCMI_Tick = 0;         // 记录上次计算帧率的时间戳
   static uint8_t DCMI_Frame_Count = 0;   // 帧计数器

   // 每隔1秒计算一次帧率
   if(HAL_GetTick() - DCMI_Tick >= 1000)
   {
      DCMI_Tick = HAL_GetTick();           // 更新当前时间戳
      OV2640_FPS = DCMI_Frame_Count;       // 将帧计数赋值给全局帧率变量
      DCMI_Frame_Count = 0;                // 帧计数器清零
   }
   DCMI_Frame_Count++;    // 每收到一帧中断，帧计数加1

   DCMI_FrameState = 1;  // 设置帧完成标志，通知主程序可以处理新一帧图像
}

/***************************************************************************************************************************************
*	函数功能: HAL_DCMI_ErrorCallback - DCMI错误中断回调函数
*	说明:     当DCMI发生错误时（如DMA传输错误、FIFO溢出等）由硬件中断调用此函数
*****************************************************************************************************************************************/
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
   // 可通过以下代码检测是否为FIFO溢出错误
   // if( HAL_DCMI_GetError(hdcmi) == HAL_DCMI_ERROR_OVR)
   // {
   //    printf("FIFO溢出错误！\r\n");
   // }
   printf("error:0x%x 错误\r\n", HAL_DCMI_GetError(hdcmi));  // 打印DCMI错误码
}


