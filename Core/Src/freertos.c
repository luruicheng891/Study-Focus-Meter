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

    /* AHT20 传感器任务 */
    xTaskCreate(AHT20_Task, "AHT20_Task", 512, NULL, osPriorityNormal, NULL);
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
