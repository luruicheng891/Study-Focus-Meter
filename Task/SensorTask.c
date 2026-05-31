/**
  ******************************************************************************
  * @file    SensorTask.c
  * @brief   传感器采集任务: AHT20 温湿度 + BH1750 光照强度
  * @note    AHT20 和 BH1750 共享同一 I2C 总线 (PB8=SCL, PB9=SDA)
  *          AHT20 地址: 0x38, BH1750 地址: 0x23 (ADDR=GND)
  *          两个传感器独立初始化, 任一个不在线不影响另一个工作
  ******************************************************************************
  */

#include "SensorTask.h"
#include "AHT20.h"
#include "BH1750.h"
#include "I2C.h"
#include "USART.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>

/* 全局队列句柄 */
QueueHandle_t xSensorDataQueue;
QueueHandle_t xAHT20DataQueue;  /* 兼容旧接口 */

/* BH1750 设备句柄 */
static BH1750_Handle_t bh1750_dev;

/* 传感器在线标志 */
static uint8_t aht20_online = 0;
static uint8_t bh1750_online = 0;

/**
  * @brief  I2C 总线扫描 - 检测设备是否在线
  * @param  addr: 7-bit 设备地址
  * @retval 0=ACK(在线), 1=NACK(不在线)
  */
static uint8_t I2C_Check_Device(uint8_t addr)
{
    uint8_t ack;
    I2C_Start();
    ack = I2C_WriteByte((addr << 1) | 0);
    I2C_Stop();
    return ack;
}

void AHT20_Task(void *pvParameters)
{
    SensorData_t sensor_data;
    AHT20_Data_t aht20_data;
    TickType_t xLastWakeTime;
    uint8_t ret;
    float lux_value = 0.0f;

    (void)pvParameters;

    printf("\r\n========== Sensor Task Start ==========\r\n");

    /* 创建队列 */
    xSensorDataQueue = xQueueCreate(1, sizeof(SensorData_t));
    xAHT20DataQueue = xQueueCreate(5, sizeof(AHT20_Data_t));

    if(xSensorDataQueue == NULL || xAHT20DataQueue == NULL)
    {
        printf("[Sensor] ERROR: Failed to create queue!\r\n");
        vTaskDelete(NULL);
        return;
    }
    printf("[Sensor] Queues created OK\r\n");

    /* 初始化 I2C GPIO */
    I2C_Sim_Init();
    printf("[Sensor] I2C GPIO Init (PB8=SCL, PB9=SDA)\r\n");

    /* 等待上电稳定 */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* ========== 扫描 I2C 总线 ========== */
    ret = I2C_Check_Device(0x38);
    printf("[Sensor] AHT20  (0x38): %s\r\n", (ret == 0) ? "ACK" : "NACK");

    ret = I2C_Check_Device(0x23);
    printf("[Sensor] BH1750 (0x23): %s\r\n", (ret == 0) ? "ACK" : "NACK");

    /* ========== 初始化 AHT20 (可选) ========== */
    printf("[Sensor] Initializing AHT20...\r\n");
    if(AHT20_Init())
    {
        printf("[Sensor] AHT20 Init OK\r\n");
        aht20_online = 1;
    }
    else
    {
        printf("[Sensor] AHT20 Init FAILED (not connected?)\r\n");
        aht20_online = 0;
    }

    /* ========== 初始化 BH1750 (可选) ========== */
    printf("[Sensor] Initializing BH1750...\r\n");
    ret = BH1750_Init(&bh1750_dev, 0);  /* ADDR pin = GND, 地址 0x23 */
    if(ret == BH1750_OK)
    {
        printf("[Sensor] BH1750 Init OK\r\n");
        /* 启动连续高分辨率测量模式 (1lx, 120ms) */
        ret = BH1750_StartMeasurement(&bh1750_dev, BH1750_CMD_CONT_H_RES_MODE);
        if(ret == BH1750_OK)
        {
            printf("[Sensor] BH1750 Continuous H-Res mode started\r\n");
            bh1750_online = 1;
        }
        else
        {
            printf("[Sensor] BH1750 start measurement FAILED (ret=%d)\r\n", ret);
            bh1750_online = 0;
        }
    }
    else
    {
        printf("[Sensor] BH1750 Init FAILED (ret=%d)\r\n", ret);
        bh1750_online = 0;
    }

    /* 检查是否至少有一个传感器在线 */
    if(!aht20_online && !bh1750_online)
    {
        printf("[Sensor] FATAL: No sensor online, task exit.\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* 等待 BH1750 第一次测量完成 (180ms max) */
    if(bh1750_online)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* ========== 开始周期采集 ========== */
    xLastWakeTime = xTaskGetTickCount();
    printf("[Sensor] Starting periodic measurement (1s interval)\r\n");
    printf("[Sensor] AHT20=%s, BH1750=%s\r\n",
           aht20_online ? "ON" : "OFF",
           bh1750_online ? "ON" : "OFF");

    for(;;)
    {
        /* 清零 */
        sensor_data.temperature = 0;
        sensor_data.humidity = 0;
        sensor_data.lux_x100 = 0;

        /* 读取 AHT20 温湿度 */
        if(aht20_online)
        {
            AHT20_Get_Temp_Humidity(&aht20_data.temperature, &aht20_data.humidity);
            aht20_data.timestamp = xTaskGetTickCount();
            sensor_data.temperature = aht20_data.temperature;
            sensor_data.humidity = aht20_data.humidity;

            /* 发送到兼容队列 */
            xQueueSend(xAHT20DataQueue, &aht20_data, 0);
        }

        /* 读取 BH1750 光照 */
        if(bh1750_online)
        {
            ret = BH1750_ReadLux(&bh1750_dev, &lux_value);
            if(ret == BH1750_OK)
            {
                sensor_data.lux_x100 = (uint32_t)(lux_value * 100.0f);
            }
            else
            {
                printf("[Sensor] BH1750 read err=%d\r\n", ret);
            }
        }

        /* 填充时间戳并发送综合数据 */
        sensor_data.timestamp = xTaskGetTickCount();
        xQueueOverwrite(xSensorDataQueue, &sensor_data);

        /* 固定周期 1 秒 */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}
