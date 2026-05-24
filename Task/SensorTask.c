#include "AHT20.h"
#include "USART.h"
#include "task.h"


// 全局消息队列句柄
QueueHandle_t xAHT20DataQueue;

void AHT20_Task(void *pvParameters)
{
    AHT20_Data_t sensor_data;		//温湿度结构体
    TickType_t xLastWakeTime;		
    
    // 创建消息队列，最多缓存5个数据
    xAHT20DataQueue = xQueueCreate(5, sizeof(AHT20_Data_t));
    
    if(xAHT20DataQueue == NULL)		//如果这个队列创建失败
    {
        printf("Failed to create AHT20 data queue!\r\n");
        vTaskDelete(NULL);		//任务自杀
        return;
    }
    
    // 初始化传感器
    if(AHT20_Init())
    {
        printf("AHT20 Init Success!\r\n");
    }
    else
    {
        printf("AHT20 Init Failed!\r\n");
        vTaskDelete(NULL);
        return;
    }
    
    // 初始化上次唤醒时间
    xLastWakeTime = xTaskGetTickCount();
    
    for(;;)
    {
        // 读取温湿度
        AHT20_Get_Temp_Humidity(&sensor_data.temperature, &sensor_data.humidity);
        sensor_data.timestamp = xTaskGetTickCount();  // 记录时间戳
        
        // 发送数据到队列（如果队列满了，不等待，立即阻塞）
        xQueueSend(xAHT20DataQueue, &sensor_data, 0);
       
        
        // 精确延时，保持固定周期
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

