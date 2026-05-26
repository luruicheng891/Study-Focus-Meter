#ifndef __SENSORTASK_H
#define __SENSORTASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "AHT20.h"

/* AHT20 数据队列句柄 (在 SensorTask.c 中定义) */
extern QueueHandle_t xAHT20DataQueue;

void AHT20_Task(void *pvParameters);

#endif
