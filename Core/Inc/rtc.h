/**
  ******************************************************************************
  * @file    rtc.h
  * @brief   STM32H7 内置 RTC 驱动 (脱网走时)
  *
  *          时钟源: LSE (外部 32.768kHz 晶振)
  *          走时: 全程脱网可走; 联网时由 WeatherTask 用网络 HH:MM 对时校准
  *          精度: LSE 优于 LSI, 联网对时后误差清零
  *
  *          对外暴露:
  *            MX_RTC_Init()           - 初始化 RTC (上电调用一次)
  *            RTC_GetTime(hh,mm,ss)   - 读取当前时间 (BIN)
  *            RTC_SetTime(hh,mm,ss)   - 设置时间 (WiFi 对时用)
  ******************************************************************************
  */
#ifndef __RTC_H
#define __RTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 RTC (在 SystemClock_Config 之后调用一次) */
void MX_RTC_Init(void);

/* 读取当前时间. 输出参数 hh/mm/ss 为 BIN 值. 返回 0=成功, -1=未对时 */
int RTC_GetTime(uint8_t *hh, uint8_t *mm, uint8_t *ss);

/* 设置时间 (WiFi 联网对时用). hh/mm/ss 为 BIN 值 */
void RTC_SetTime(uint8_t hh, uint8_t mm, uint8_t ss);

/* 用网络时间对时 (智能: 首次直接设; 之后仅当偏差>30s 才校准, 避免频繁重置秒)
 * hh/mm/ss 为网络 "HH:MM:SS" 解析值 */
void RTC_SyncFromNetwork(uint8_t hh, uint8_t mm, uint8_t ss);

#ifdef __cplusplus
}
#endif

#endif /* __RTC_H */
