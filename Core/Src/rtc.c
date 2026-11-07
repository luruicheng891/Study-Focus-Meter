/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   STM32H7 内置 RTC 驱动 (LSE 32.768kHz 外部晶振, 脱网走时)
  *
  *          - MX_RTC_Init:  上电初始化 RTC 外设 (不改时间寄存器)
  *          - RTC_SetTime:  WiFi 对时写入 (置 rtc_synced 标志)
  *          - RTC_GetTime:  读取时间; 未对时返回 -1 (UI 显示 --:--)
  ******************************************************************************
  */
#include "rtc.h"
#include "main.h"            /* Error_Handler */
#include "stm32h7xx_hal.h"
#include <stdio.h>

/* 读取 RCC->BDCR 的 RTCSEL 字段: 0=无时钟, 1=LSE, 2=HSE, 3=LSI */
#define RTC_CLKSRC_LSE  1
#define RTC_CLKSRC_LSI  3

RTC_HandleTypeDef hrtc;

/* 0 = 从未对过时 (UI 显示 --:--); 1 = 已对时, 可正常显示 */
static uint8_t rtc_synced = 0;

/* 上次对时时刻 (FreeRTOS tick), 用于诊断时计算"距上次对时过了多久" */
static uint32_t s_last_sync_tick = 0;

/* 自检基准: 首次对时时的 RTC 秒数 + 当时的 SysTick。
 * 之后用 (RTC走过的秒) - (SysTick走过的秒) 直接测 RTC 漂移, 完全不依赖网络。
 * SysTick(HAL_GetTick) 由 550MHz 系统时钟(HSE 晶振)驱动, 精度极高, 可作基准。 */
static uint32_t s_base_rtc_sec  = 0;
static uint32_t s_base_tick     = 0;
static uint8_t  s_base_valid    = 0;

/* HAL_RTC_Init 内部回调: 启用 RTC 时钟 */
void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtcHandle)
{
    if (hrtcHandle->Instance == RTC) {
        __HAL_RCC_RTC_ENABLE();
    }
}

void MX_RTC_Init(void)
{
    /* LSE 32.768kHz 外部晶振: 32768 / (127+1) / (255+1) = 1.0 Hz
     * (LSE 精度优于 LSI, 脱网走时误差更小, 联网时由 WeatherTask 对时校准) */
    hrtc.Instance             = RTC;
    hrtc.Init.HourFormat      = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv    = 127;
    hrtc.Init.SynchPrediv     = 255;
    hrtc.Init.OutPut          = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity  = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType      = RTC_OUTPUT_TYPE_OPENDRAIN;
    hrtc.Init.OutPutRemap     = RTC_OUTPUT_REMAP_NONE;

    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        Error_Handler();
    }
}

int RTC_GetTime(uint8_t *hh, uint8_t *mm, uint8_t *ss)
{
    if (!rtc_synced) return -1;   /* 未对时, UI 应显示 --:-- */

    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    /* 硬件要求: 读 Time 后必须读 Date 才能锁存影子寄存器 */
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    *hh = sTime.Hours;
    *mm = sTime.Minutes;
    *ss = sTime.Seconds;
    return 0;
}

void RTC_SetTime(uint8_t hh, uint8_t mm, uint8_t ss)
{
    RTC_TimeTypeDef sTime = {0};
    sTime.Hours           = hh;
    sTime.Minutes         = mm;
    sTime.Seconds         = ss;
    sTime.DayLightSaving  = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation  = RTC_STOREOPERATION_RESET;

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    rtc_synced = 1;
}

/* 仅当 RTC 与网络时间偏差超过此阈值才强制重设 (秒)。
 * 天气 JSON 的 "time" 是服务器观测/更新时间, 会滞后真实时间几十秒并随机跳变;
 * 而 LSE RTC 走时精度 ~20ppm (0.001s/min) 极准。
 * 因此除"开机首次对时"外, 不再用网络时间频繁回拨 RTC, 只在偏差大到
 * 离谱 (说明 RTC 掉电/从未对时) 时才重设, 避免用脏时间源污染准确的 RTC。 */
#define RTC_RESYNC_THRESHOLD_S   300   /* 5 分钟 */

void RTC_SyncFromNetwork(uint8_t hh, uint8_t mm, uint8_t ss)
{
    uint8_t ch, cm, cs;

    /* 首次对时: 直接以网络时间设基准 (此后信任 RTC 自走时) */
    if (RTC_GetTime(&ch, &cm, &cs) != 0) {
        uint32_t bdcr = RCC->BDCR;
        printf("[RTC] src=%lu(1=LSE,3=LSI) LSERDY=%lu | first sync -> %02u:%02u:%02u\r\n",
               (unsigned long)((bdcr & RCC_BDCR_RTCSEL) >> RCC_BDCR_RTCSEL_Pos),
               (unsigned long)((bdcr & RCC_BDCR_LSERDY) >> RCC_BDCR_LSERDY_Pos),
               hh, mm, ss);
        RTC_SetTime(hh, mm, ss);
        s_last_sync_tick = HAL_GetTick();
        /* 记录自检基准 (重新读一次 RTC, 用刚设进去的值) */
        {
            uint8_t bh, bm, bs;
            if (RTC_GetTime(&bh, &bm, &bs) == 0) {
                s_base_rtc_sec = (uint32_t)bh * 3600 + (uint32_t)bm * 60 + bs;
                s_base_tick    = HAL_GetTick();
                s_base_valid   = 1;
            }
        }
        return;
    }

    /* 已对时: 计算与网络时间的偏差 (处理跨天, 保留符号)
     * diff > 0 : 网络时间比 RTC 快 (RTC 滞后/走慢)
     * diff < 0 : 网络时间比 RTC 慢 (RTC 超前/走快) */
    int32_t net_sec = (int32_t)hh * 3600 + (int32_t)mm * 60 + (int32_t)ss;
    int32_t rtc_sec = (int32_t)ch * 3600 + (int32_t)cm * 60 + (int32_t)cs;
    int32_t diff = net_sec - rtc_sec;
    if (diff > 43200)       diff -= 86400;   /* 跨天取短弧, 保留符号 */
    else if (diff < -43200) diff += 86400;

    uint32_t elapsed_s = (HAL_GetTick() - s_last_sync_tick) / 1000U;

    /* ===== 核心自检: RTC 走时 vs SysTick(HSE) 走时, 不依赖网络 ===== */
    int32_t rtc_adv  = 0, tick_adv = 0, rtc_drift = 0;
    if (s_base_valid) {
        rtc_adv  = rtc_sec - (int32_t)s_base_rtc_sec;
        if (rtc_adv < -43200) rtc_adv += 86400;   /* 跨天 */
        tick_adv = (int32_t)((HAL_GetTick() - s_base_tick) / 1000U);
        rtc_drift = rtc_adv - tick_adv;            /* >0=RTC走快, <0=RTC走慢 */
    }

    /* 诊断:
     * - rtc_drift 随时间单调增长 -> RTC 真的走快/走慢 (硬件问题)
     * - rtc_drift 始终在 0/±1 附近 -> RTC 准, "误差"来自网络时间源或显示源 */
    uint32_t bdcr = RCC->BDCR;
    printf("[RTC] src=%lu LSERDY=%lu | net=%02u:%02u:%02u rtc=%02u:%02u:%02u | net_diff=%+lds | SELFTEST rtc_adv=%lds tick_adv=%lds drift=%+lds\r\n",
           (unsigned long)((bdcr & RCC_BDCR_RTCSEL) >> RCC_BDCR_RTCSEL_Pos),
           (unsigned long)((bdcr & RCC_BDCR_LSERDY) >> RCC_BDCR_LSERDY_Pos),
           hh, mm, ss, ch, cm, cs,
           (long)diff, (long)rtc_adv, (long)tick_adv, (long)rtc_drift);
    (void)elapsed_s;

    /* 只有偏差离谱 (>5min) 才重设 — 正常运行下信任 RTC, 不被脏网络时间回拨 */
    if (diff > RTC_RESYNC_THRESHOLD_S || diff < -RTC_RESYNC_THRESHOLD_S) {
        RTC_SetTime(hh, mm, ss);
        s_last_sync_tick = HAL_GetTick();
        /* 重设后刷新自检基准 */
        s_base_rtc_sec = (uint32_t)hh * 3600 + (uint32_t)mm * 60 + ss;
        s_base_tick    = HAL_GetTick();
        s_base_valid   = 1;
    }
}
