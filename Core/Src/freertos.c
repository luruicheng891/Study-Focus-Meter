/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS 任务初始化与 LVGL 任务
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "Camera.h"
#include "SensorTask.h"
#include "WeatherTask.h"
#include "AITask.h"
#include "UART.h"
#include "display_mode.h"
#include "lvgl.h"

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void LVGL_Task(void *argument);

/* Exported functions --------------------------------------------------------*/

void MX_FREERTOS_Init(void);

/**
  * @brief  FreeRTOS initialization
  */
void MX_FREERTOS_Init(void)
{
    /* LVGL 任务: 负责调用 lv_timer_handler(), 栈 2KB, 优先级较高确保 UI 流畅 */
    xTaskCreate(LVGL_Task, "LVGL_Task", 512, NULL, osPriorityAboveNormal, NULL);

    /* 摄像头任务: 采集 + 通知 LVGL 刷新, 栈 4KB */
    xTaskCreate(Camera_task, "Camera_task", 1024, NULL, osPriorityNormal, NULL);

    /* Sensor 传感器任务 */
    xTaskCreate(Sensor_Task, "Sensor_Task", 512, NULL, osPriorityNormal, NULL);

    /* 温湿度 LVGL 显示任务: 从队列读取 AHT20 数据并更新 UI, 栈 2KB */
    xTaskCreate(Weather_Task, "Weather_Task", 512, NULL, osPriorityNormal, NULL);

    /* ESP-01 天气数据接收+解析任务 (USART3 + DMA + IDLE), 栈 2KB */
    xTaskCreate(Weather_RxTask, "Weather_RxTask", 512, NULL, osPriorityNormal, NULL);

    /* AI推理任务: 预处理+模型推理+结果显示, 栈 12KB, 优先级低于普通任务 */
    xTaskCreate(AI_InferTask, "AI_Infer", 3072, NULL, osPriorityBelowNormal, &AI_TaskHandle);
}

/**
  * @brief  LVGL 定时处理任务
  * @note   必须定期调用 lv_timer_handler() 驱动 LVGL 内部定时器和刷屏
  *         建议周期 5~10ms
  */
static void LVGL_Task(void *argument)
{
    (void)argument;

    while (1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
