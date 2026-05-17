/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "lcd.h"
#include "dcmi_ov2640.h"
#include "led.h"
#define Camera_Buffer	0x24000000    // ����ͷͼ�񻺳���



void Camera_task(void *argument)
{
	
	LED_Init();
	LCD_Init();             // ILI9341 LCD��ʼ�� (SPI1: PB3-SCK, PB5-MOSI)
	DCMI_OV2640_Init();     // DCMI�Լ�OV2640��ʼ��
	OV2640_DMA_Transmit_Continuous(Camera_Buffer, OV2640_BufferSize);  // ����DMA�����ɼ�
	
	while(1){
		if (DCMI_FrameState == 1)   // �ɼ�����һ֡ͼ��
		{
			DCMI_FrameState = 0;

			// ʹDCache��Ч��ȷ��CPU����DMAд�����������
			SCB_InvalidateDCache_by_Addr((uint32_t *)Camera_Buffer, Display_Width * Display_Height * 2);
			// ������ͷͼ��ˢ��LCD
			LCD_DrawBuffer(0, 0, Display_Width - 1, Display_Height - 1, (uint16_t *)Camera_Buffer);
		}
		vTaskDelay(1);
	}
}

