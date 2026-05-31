#ifndef __AHT20_H
#define __AHT20_H

#include "stdint.h"
#include "stdint.h"
#include "FreeRTOS.h"
#include "queue.h"

// 温湿度数据结构体
typedef struct {
    int32_t temperature;  // 温度值，单位0.01°C (如2500表示25.00°C)
    uint32_t humidity;    // 湿度值，单位0.01% (如5678表示56.78%)
    uint32_t timestamp;   // 时间戳（可选，用于知道数据采集时间）
} AHT20_Data_t;



uint8_t AHT20_Init(void);
void AHT20_Read_CTdata(uint32_t *ct);
void AHT20_Get_Temp_Humidity(int32_t *temperature, uint32_t *humidity);
uint8_t AHT20_Read_Status(void);
uint8_t AHT20_Read_Cal_Enable(void);
void AHT20_Soft_Reset(void);
void AHT20_Debug_Print(void);

#endif



