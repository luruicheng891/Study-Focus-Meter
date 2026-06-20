#ifndef __DCMI_OV2640_H
#define __DCMI_OV2640_H

#include "stm32h7xx_hal.h"
#include "sccb.h"  
#include "usart.h"
#include "lcd.h"

// DCMI帧状态标志，0表示空闲，1表示接收完一帧数据（在HAL_DCMI_FrameEventCallback()中断回调函数中置1）
extern volatile uint8_t DCMI_FrameState;
// OV2640摄像头帧率变量
extern volatile uint8_t OV2640_FPS;

// 函数返回值定义
#define  OV2640_Success   0            // 通信成功标志
#define  OV2640_Error     -1           // 通信失败

// 功能使能/禁用定义
#define  OV2640_Enable    1
#define  OV2640_Disable   0

// 像素格式定义，用于 OV2640_Set_Pixformat() 函数
#define Pixformat_RGB565   0
#define Pixformat_JPEG     1

// OV2640图像特效模式定义，用于 OV2640_Set_Effect() 函数
#define  OV2640_Effect_Normal       0  // 正常模式
#define  OV2640_Effect_Negative     1  // 负片模式（也叫反色，颜色全取反）
#define  OV2640_Effect_BW           2  // 黑白模式
#define  OV2640_Effect_BW_Negative  3  // 黑白模式+负片模式

/* 
 * 图像尺寸配置说明：
 * 1. 设置OV2640实际输出图像大小，可根据需要修改，但需同时修改DCMI裁剪参数
 * 2. 缩小图像尺寸会影响帧率，但帧率上限受当前工作模式限制
 * 3. SVGA模式下，最大图像尺寸为 800*600, 最大帧率30帧
 * 4. UXGA模式下，最大图像尺寸为 1600*1200, 最大帧率15帧
 * 5. 要使用的图像长宽必须能被4整除
 * 6. 要使用的图像长宽默认比例为4:3，否则图像会被拉伸变形
 */
#define OV2640_Width          400   // 图像宽度 
#define OV2640_Height         300   // 图像高度

/* 
 * 显示尺寸配置说明：
 * 1. 需要显示屏的宽度和高度值，必须能被4整除
 * 2. RGB565格式下，用DCMI将OV2640输出的4:3图像裁剪为适应屏幕的比例
 * 3. 此处分辨率通常小于 OV2640_Width 和 OV2640_Height
 * 4. 分辨率太高时需要降低PCLK时钟速度，详细说明可参考 dcmi_ov2640_cfg.h 中 0xd3 的寄存器描述
 */
#define Display_Width          240
#define Display_Height         240

/* 
 * DMA缓冲区大小计算：
 * 1. RGB565模式下，需要 图像分辨率*2 的大小
 * 2. JPG模式下，需要按实际情况预估大小，一般情况下 640*480分辨率的JPG图像约需30K字节
 *    所以预留200K字节大小，通常足够，实测时可根据需要调整
 * 3. 因为DMA配置为32位传输，所以缓冲区大小需除以4
 */
#define OV2640_BufferSize     Display_Width * Display_Height * 2 / 4   // DMA传输数据大小（以32位字为单位）


// 寄存器组选择
#define  OV2640_SEL_Registers       0xFF   // 寄存器组选择寄存器地址
#define  OV2640_SEL_DSP             0x00   // 写入0x00时，选择 DSP    寄存器组
#define  OV2640_SEL_SENSOR          0x01   // 写入0x01时，选择 SENSOR 寄存器组


// DSP 寄存器组定义 (当0xFF = 0x00时)
#define OV2640_DSP_RESET           0xE0    // 复位选择位：复位SCCB单元、JPEG单元、DVP接口单元等
#define OV2640_DSP_BPADDR          0x7C    // SDE（图像效果引擎）间接寄存器：地址
#define OV2640_DSP_BPDATA          0x7D    // SDE（图像效果引擎）间接寄存器：数据

// SENSOR 寄存器组定义 (当0xFF = 0x01时)
#define OV2640_SENSOR_COM7         0x12    // 通用寄存器7：系统复位、输出格式选择、色彩条测试等
#define OV2640_SENSOR_REG04        0x04    // 寄存器4：控制摄像头扫描方向（水平镜像/垂直翻转）
#define OV2640_SENSOR_PIDH         0x0a    // 产品ID高字节
#define OV2640_SENSOR_PIDL         0x0b    // 产品ID低字节

/*------------------------------------------------------------ 函数声明 ------------------------------------------------*/

int8_t   DCMI_OV2640_Init(void);                                      // 初始化SCCB、DCMI、DMA并配置OV2640

void     OV2640_DMA_Transmit_Continuous(uint32_t DMA_Buffer, uint32_t DMA_BufferSize);  // 启动DMA连续传输模式
void     OV2640_DMA_Transmit_Snapshot(uint32_t DMA_Buffer, uint32_t DMA_BufferSize);    // 启动DMA单帧快照传输模式（仅采集一帧图像后停止）
void     OV2640_DCMI_Suspend(void);                                   // 暂停DCMI，停止捕获图像
void     OV2640_DCMI_Resume(void);                                    // 恢复DCMI，重新开始捕获图像
void     OV2640_DCMI_Stop(void);                                      // 停止DCMI和DMA传输，并关闭DCMI时钟
int8_t   OV2640_DCMI_Crop(uint16_t Displey_XSize, uint16_t Displey_YSize,
                          uint16_t Sensor_XSize, uint16_t Sensor_YSize);  // 配置DCMI硬件裁剪窗口

void     OV2640_Reset(void);                                          // 执行摄像头硬件复位
uint16_t OV2640_ReadID(void);                                         // 读取摄像头ID
void     OV2640_Config(const uint8_t (*ConfigData)[2]);               // 配置摄像头寄存器组
void     OV2640_Set_Pixformat(uint8_t pixformat);                     // 设置输出像素格式（RGB565/JPEG）
int8_t   OV2640_Set_Framesize(uint16_t width, uint16_t height);      // 设置实际输出图像尺寸
int8_t   OV2640_Set_Horizontal_Mirror(int8_t ConfigState);            // 设置图像水平镜像（左右翻转）
int8_t   OV2640_Set_Vertical_Flip(int8_t ConfigState);                // 设置图像垂直翻转（上下颠倒）
void     OV2640_Set_Saturation(int8_t Saturation);                    // 设置图像饱和度（-2到2共5级）
void     OV2640_Set_Brightness(int8_t Brightness);                    // 设置图像亮度（-2到2共5级）
void     OV2640_Set_Contrast(int8_t Contrast);                        // 设置图像对比度（-2到2共5级）
void     OV2640_Set_Effect(uint8_t effect_Mode);                      // 设置图像特效模式（正常/负片/黑白/黑白+负片）

/*-------------------------------------------------------------- 硬件引脚宏定义 ---------------------------------------------*/

// PWDN（掉电控制）引脚定义
#define OV2640_PWDN_PIN                  GPIO_PIN_13                     // PWDN 引脚号
#define OV2640_PWDN_PORT                 GPIOF                           // PWDN 所属GPIO端口
#define GPIO_OV2640_PWDN_CLK_ENABLE      __HAL_RCC_GPIOF_CLK_ENABLE()    // 使能PWDN引脚的GPIO时钟

// PWDN拉低：退出掉电模式，摄像头开始工作
#define OV2640_PWDN_OFF   HAL_GPIO_WritePin(OV2640_PWDN_PORT, OV2640_PWDN_PIN, GPIO_PIN_RESET)

// PWDN拉高：进入掉电模式，摄像头停止工作（用于降低功耗）
#define OV2640_PWDN_ON    HAL_GPIO_WritePin(OV2640_PWDN_PORT, OV2640_PWDN_PIN, GPIO_PIN_SET)
  

#endif //__DCMI_OV2640_H
