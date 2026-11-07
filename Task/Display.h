/**
  ******************************************************************************
  * @file    WeatherDisplay.h
  * @brief   320x240 横屏天气仪表板 LVGL 显示任务
  *          (原 WeatherTask.c 的 LVGL 部分, 已与数据接收解耦)
  *
  * 数据来源:
  *   - 本地传感器: SensorTask 的 xSensorDataQueue (SensorData_t)
  *   - 远程天气:   WeatherTask 的 Weather_GetLatest() (WeatherInfo_t)
  ******************************************************************************
  */

#ifndef __WEATHER_DISPLAY_H
#define __WEATHER_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  天气仪表板 LVGL 显示任务 (含摄像头/天气模式切换)
  */
void DisplayTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* __WEATHER_DISPLAY_H */
