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
#include "Display.h"
#include "AITask.h"
#include "SlaveRxTask.h"
#include "FusionTask.h"
#include "display_mode.h"
#include "lvgl.h"
#include "TouchTask.h"

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

    /* LVGL 显示任务: 渲染天气仪表板 + 显示模式切换, 栈 2KB */
    xTaskCreate(DisplayTask, "WeatherDisp", 512, NULL, osPriorityNormal, NULL);

    /* ESP-01 天气数据接收+解析任务 (USART3 + DMA + IDLE), 栈 2KB */
    xTaskCreate(Weather_RxTask, "Weather_Rx", 512, NULL, osPriorityNormal, NULL);

    /* ESP32 BLE 透传 (W02/PB-03F) IMU+压力数据接收任务 (USART2 + DMA + IDLE), 栈 2KB */
    xTaskCreate(Slave_RxTask, "Slave_Rx", 512, NULL, osPriorityNormal, NULL);

    /* AI推理任务: 预处理+模型推理+结果显示, 栈 12KB, 优先级低于普通任务 */
    xTaskCreate(AI_InferTask, "AI_Infer", 3072, NULL, osPriorityBelowNormal, &AI_TaskHandle);

    /* XPT2046 触摸任务: 20ms 周期扫描, 注册 LVGL indev, 串口命令支持, 栈 2KB */
    xTaskCreate(Touch_Task, "Touch_Task", 512, NULL, osPriorityNormal, NULL);

    /* Fusion 多模态融合任务: 1s 周期, 视觉40% + 坐姿30% + 环境20% + 时长10% */
    xTaskCreate(Fusion_Task, "Fusion_Task", 1024, NULL, osPriorityLow, NULL);
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
