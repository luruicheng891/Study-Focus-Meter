#ifndef __SENSORTASK_H
#define __SENSORTASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "AHT20.h"

/* 传感器数据结构体 (包含温湿度 + 光照) */
typedef struct {
    int32_t  temperature;  // 温度, 单位 0.01°C (如 2500 = 25.00°C)
    uint32_t humidity;     // 湿度, 单位 0.01% (如 5678 = 56.78%)
    uint32_t lux_x100;    // 光照强度, 单位 0.01 lx (如 10050 = 100.50 lx)
		uint32_t soundIntensity;	//麦克风音量大小
    uint32_t timestamp;   // 时间戳 (tick)
} SensorData_t;

/* 传感器数据队列句柄 */
extern QueueHandle_t xSensorDataQueue;



void Sensor_Task(void *pvParameters);

#endif
