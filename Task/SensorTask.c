/**
  ******************************************************************************
  * @file    SensorTask.c
  * @brief   AHT20 传感器采集任务, 定时读取温湿度并发送到队列
  ******************************************************************************
  */

#include "AHT20.h"
#include "I2C.h"
#include "USART.h"
#include "task.h"
#include <stdio.h>

// 全局消息队列句柄
QueueHandle_t xAHT20DataQueue;

/**
  * @brief  I2C 总线扫描 - 检测 AHT20 是否在线
  * @retval 0=检测到ACK(设备在线), 1=NACK(设备不在线)
  */
static uint8_t AHT20_Check_Online(void)
{
    uint8_t ack;
    
    I2C_Start();
    ack = I2C_WriteByte((0x38 << 1) | 0);  // 发送 AHT20 写地址
    I2C_Stop();
    
    return ack;  // 0=ACK(在线), 1=NACK(不在线)
}

void AHT20_Task(void *pvParameters)
{
    AHT20_Data_t sensor_data;
    TickType_t xLastWakeTime;
    uint8_t ret;
    
    printf("\r\n========== AHT20 Task Start ==========\r\n");
    
    // 创建消息队列
    xAHT20DataQueue = xQueueCreate(5, sizeof(AHT20_Data_t));
    
    if(xAHT20DataQueue == NULL)
    {
        printf("[AHT20] ERROR: Failed to create queue!\r\n");
        vTaskDelete(NULL);
        return;
    }
    printf("[AHT20] Queue created OK\r\n");
    
    // 确保 I2C GPIO 已初始化
    I2C_Sim_Init();
    printf("[AHT20] I2C GPIO Init (PB8=SCL, PB9=SDA)\r\n");
    
    // 延时等待 AHT20 上电稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 扫描 I2C 总线, 检测 AHT20 是否应答
    ret = AHT20_Check_Online();
    printf("[AHT20] I2C scan addr 0x38: %s\r\n", (ret == 0) ? "ACK (online)" : "NACK (offline!)");
    
    if(ret != 0)
    {
        printf("[AHT20] ERROR: Device not found on I2C bus!\r\n");
        printf("[AHT20] Check wiring: PB8->SCL, PB9->SDA, VCC=3.3V, pull-up resistors\r\n");
        // 不立即删除任务, 继续重试
        for(int i = 0; i < 5; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            ret = AHT20_Check_Online();
            printf("[AHT20] Retry %d: %s\r\n", i+1, (ret == 0) ? "ACK" : "NACK");
            if(ret == 0) break;
        }
        if(ret != 0)
        {
            printf("[AHT20] FATAL: Device unreachable, task exit.\r\n");
            vTaskDelete(NULL);
            return;
        }
    }
    
    // 初始化传感器
    printf("[AHT20] Initializing sensor...\r\n");
    if(AHT20_Init())
    {
        printf("[AHT20] Init Success!\r\n");
    }
    else
    {
        printf("[AHT20] Init Failed! (no CAL bit after retries)\r\n");
        vTaskDelete(NULL);
        return;
    }
    
    // 开始周期采集
    xLastWakeTime = xTaskGetTickCount();
    printf("[AHT20] Starting periodic measurement (1s interval)\r\n");
    
    for(;;)
    {
        // 读取温湿度
        AHT20_Get_Temp_Humidity(&sensor_data.temperature, &sensor_data.humidity);
        sensor_data.timestamp = xTaskGetTickCount();
        
        // 串口打印原始数据用于调试
//        printf("[AHT20] T=%d.%02d C, H=%u.%02u%%, tick=%u\r\n",
//               (int)(sensor_data.temperature / 100),
//               (int)(sensor_data.temperature >= 0 ? sensor_data.temperature % 100 : (-sensor_data.temperature) % 100),
//               (unsigned int)(sensor_data.humidity / 100),
//               (unsigned int)(sensor_data.humidity % 100),
//               (unsigned int)sensor_data.timestamp);
        
        // 检查数据合理性
        if(sensor_data.temperature == -5000 && sensor_data.humidity == 0)
        {
            printf("[AHT20] WARNING: Raw data all zero, I2C read may have failed!\r\n");
        }
        
        // 发送数据到队列
        xQueueSend(xAHT20DataQueue, &sensor_data, 0);
        
        // 固定周期延时
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}
