/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "lcd.h"
#include "dcmi_ov2640.h"
#include "led.h"

#define Camera_Buffer   0x24000000    // 摄像头缓冲区地址

void Camera_task(void *argument)
{
    LED_Init();
    LCD_Init();             // ILI9341 LCD初始化 (SPI1: PB3-SCK, PB5-MOSI)
    DCMI_OV2640_Init();     // DCMI接口OV2640初始化
    OV2640_DMA_Transmit_Continuous(Camera_Buffer, OV2640_BufferSize);  // 开启DMA传输数据
    
    while(1){
        if (DCMI_FrameState == 1)   // 采集完成一帧图像
        {
            DCMI_FrameState = 0;
            
            // 使DCache失效确保CPU读取DMA写过的最新数据
            SCB_InvalidateDCache_by_Addr((uint32_t *)Camera_Buffer, Display_Width * Display_Height * 2);
            // 将摄像头图像刷新到LCD
            LCD_DrawBuffer(0, 0, Display_Width - 1, Display_Height - 1, (uint16_t *)Camera_Buffer);
        }
        vTaskDelay(1);
    }
}