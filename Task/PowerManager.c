/**
  ******************************************************************************
  * @file    PowerManager.c
  * @brief   低功耗管理 — STM32H723 Stop 模式 + 触摸/RTC 多源唤醒
  *
  *  详见 PowerManager.h 头部说明。
  ******************************************************************************
  */

#include "PowerManager.h"

#include "main.h"               /* stm32h7xx_hal.h, Error_Handler */
#include "ScreenSaver.h"        /* ScreenSaver_IsDimmed / NotifyActivity */
#include "StateTask.h"          /* State_GetCurrent / State_IsPersonPresent */
#include "lcd.h"                /* LCD_LED_ON / LCD_LED_OFF */
#include "dcmi_ov2640.h"        /* OV2640_DCMI_Suspend / Resume */
#include "touch.h"              /* TP_PEN_PORT / TP_PEN_PIN (PB1) */
#include "UART_ESP01.h"         /* UART_ESP01_GetDMABuffer */
#include "UART_ESP32.h"         /* UART_ESP32_GetDMABuffer */

#include <stdio.h>
#include <string.h>

/* RTC 句柄 (定义在 Core/Src/rtc.c) */
extern RTC_HandleTypeDef hrtc;

/* ============================ 可调参数 ============================ */

/* RTC 周期嗅探间隔 (秒): 即使没人触摸, 也每隔这么久醒一次给任务采一轮数据 */
#define POWER_SNIFF_PERIOD_S        15u

/* Power_Task 轮询周期 (ms) */
#define POWER_MONITOR_PERIOD_MS     500u

/* RTC 嗅探唤醒后, 让其它任务运行多久再决定是否继续睡 (ms)
 * 给 Sensor_Task / Slave_RxTask 一个时间片去刷新"有没有人" */
#define POWER_SNIFF_AWAKE_MS        1500u

/* 已息屏后, 还要再稳定这么久才允许进 Stop (ms)
 * 留出 printf 串口 TX 排空 + 避免刚息屏就睡的抖动 */
#define POWER_DIM_SETTLE_MS         3000u

/* 是否启用 USART2 BLE 起始位唤醒 (默认关闭)
 *   开启需要: 把 USART2 内核时钟切到 HSI (RCC_USART234578CLKSOURCE_HSI),
 *   并 HAL_UARTEx_EnableStopMode + StopModeWakeUpSourceConfig(STARTBIT)。
 *   该改动会影响现有 115200 BLE 链路的波特率计算, 必须上硬件实测,
 *   故先留接口、默认不启用。 */
#define POWER_ENABLE_UART_WAKE      0

/* ============================ 内部状态 ============================ */

static volatile int      s_deep_sleep_enabled = 1;
static uint32_t          s_sleep_count        = 0;
static uint32_t          s_total_sleep_s      = 0;
static PowerWakeReason_t s_last_reason        = POWER_WAKE_NONE;

/* ============================ 小工具 ============================== */

/**
  * @brief  读取 RTC 当前"日内秒数" (0..86399)
  * @note   直接读硬件计数器, 不依赖 rtc.c 的 rtc_synced 软件标志;
  *         RTC 由 LSE 驱动, 从上电起一直在走, 用于估算休眠时长。
  */
static uint32_t power_rtc_seconds(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);   /* 必须读 Date 才能解锁影子寄存器 */
    return (uint32_t)t.Hours * 3600u + (uint32_t)t.Minutes * 60u + t.Seconds;
}

/**
  * @brief  当前状态是否允许深度睡眠
  * @retval 1 = 允许 (IDLE / SUMMARY 且已息屏), 0 = 不允许
  */
static int power_sleep_allowed(void)
{
    LearningState_t st = State_GetCurrent();
    if (st != ST_IDLE && st != ST_SUMMARY) return 0;   /* 学习相关态绝不深睡 */
    if (!ScreenSaver_IsDimmed())            return 0;   /* 屏还亮着, 说明刚有活动 */
    if (ScreenSaver_GetDimmedMs() < POWER_DIM_SETTLE_MS) return 0; /* 息屏未稳定 */
    return 1;
}

/* ============================ 唤醒源配置 ========================== */

/**
  * @brief  把触摸 PEN (PB1) 重配为 EXTI1 下降沿, 作为 Stop 唤醒源
  * @note   按下时 PEN 被拉低 → 下降沿。仅在进 Stop 前临时启用,
  *         醒来后立即还原成普通输入, 避免正常运行期每次触摸都触发 EXTI。
  */
static void power_enable_pen_wakeup(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = TP_PEN_PIN;                 /* PB1 */
    g.Mode  = GPIO_MODE_IT_FALLING;       /* 按下=低, 下降沿唤醒 */
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TP_PEN_PORT, &g);

    __HAL_GPIO_EXTI_CLEAR_IT(TP_PEN_PIN);
    NVIC_ClearPendingIRQ(EXTI1_IRQn);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 6, 0);   /* >= configMAX_SYSCALL(5), FreeRTOS 安全档 */
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}

/**
  * @brief  关闭 PEN EXTI, 还原成普通上拉输入 (供 TouchTask 轮询)
  */
static void power_disable_pen_wakeup(void)
{
    HAL_NVIC_DisableIRQ(EXTI1_IRQn);
    __HAL_GPIO_EXTI_CLEAR_IT(TP_PEN_PIN);
    NVIC_ClearPendingIRQ(EXTI1_IRQn);

    GPIO_InitTypeDef g = {0};
    g.Pin  = TP_PEN_PIN;
    g.Mode = GPIO_MODE_INPUT;             /* 还原: 普通输入, 触摸任务轮询读电平 */
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(TP_PEN_PORT, &g);
}

/**
  * @brief  启动 RTC 周期唤醒定时器 (CK_SPRE = 1Hz, 计数 = 秒-1)
  */
static void power_enable_rtc_wakeup(void)
{
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);

    NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

    /* 1Hz 时基: WakeUpCounter = 周期秒数 - 1 */
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc,
                                (POWER_SNIFF_PERIOD_S > 0 ? POWER_SNIFF_PERIOD_S - 1u : 0u),
                                RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
}

/**
  * @brief  关闭 RTC 周期唤醒定时器
  */
static void power_disable_rtc_wakeup(void)
{
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
    HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
    NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
}

/* ============================ 时钟恢复 ============================ */

/**
  * @brief  从 Stop 唤醒后重建 550MHz 系统时钟
  * @note   ⚠ 故意 **不** 调用 main.c 的 SystemClock_Config(): 那个函数会
  *         强制复位备份域 (清掉 RTC 时间) 来重写 LSEDRIVE。这里只重建
  *         VOS0 + HSE + PLL1, 完全不触碰备份域/LSE/RTC, 保住休眠期间的走时。
  *         LSE 在 Stop 下持续运行, 故 RTC 不丢秒。
  */
static void power_restore_clock(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* 恢复内核电压等级 VOS0 (550MHz 必需), 供电模式 LDO 在上电时已配置, 此处不动 */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) { }

    /* HSE + PLL1 (参数与 SystemClock_Config 完全一致) */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 10;
    osc.PLL.PLLN       = 220;
    osc.PLL.PLLP       = 1;
    osc.PLL.PLLQ       = 2;
    osc.PLL.PLLR       = 2;
    osc.PLL.PLLRGE     = RCC_PLL1VCIRANGE_1;
    osc.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN   = 0;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        /* 失败则继续跑 HSI, 至少不死机 */
        return;
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2
                       | RCC_CLOCKTYPE_D3PCLK1| RCC_CLOCKTYPE_D1PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    (void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3);
}

/* ============================ UART DMA 恢复 ======================== */

/**
  * @brief  从 Stop 唤醒后,重启所有 UART 的 DMA 接收状态机
  * @note   Stop 模式下外设时钟关闭, UART CR1 寄存器 (IDLE 使能位) 和
  *         DMA 状态可能丢失或混乱。必须在使能中断后、恢复调度前重启。
  *         ⚠ 此函数在 taskENTER_CRITICAL 区间外调用, 但在挂起调度器期间
  *         执行 (没有任务切换风险), 可以安全访问 HAL 句柄。
  */
static void Power_RestartUART_RX(void)
{
    /* 1. USART3 (ESP-01 WiFi, DMA1_Stream1) */
    {
        /* 停止当前 DMA (可能已处于未知状态) */
        HAL_UART_DMAStop(&huart3);

        /* 清除所有错误标志 (ORE/FE/NE/PE, H7 用 ICR 寄存器清) */
        huart3.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF
                             | USART_ICR_NECF  | USART_ICR_PECF
                             | USART_ICR_IDLECF;

        /* 重启 DMA 循环接收 */
        HAL_UART_Receive_DMA(&huart3, UART_ESP01_GetDMABuffer(), USART3_RX_BUFFER_SIZE);

        /* 重新使能 IDLE 中断 (关键!) */
        __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
    }

    /* 2. USART2 (ESP32 BLE, DMA1_Stream3) */
    {
        HAL_UART_DMAStop(&huart2);
        huart2.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF
                             | USART_ICR_NECF  | USART_ICR_PECF
                             | USART_ICR_IDLECF;

        HAL_UART_Receive_DMA(&huart2, UART_ESP32_GetDMABuffer(), 256);   /* USART2 buf size=256 */
        __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
    }

    /* 如果将来还有其它 UART DMA RX, 按相同模板添加 */
}

/* ============================ Stop 进入/唤醒 ====================== */

/**
  * @brief  执行一次完整的 Stop 进入 + 唤醒序列 (阻塞直到被唤醒)
  * @return 唤醒原因
  *
  *  序列:
  *    暂停摄像头 → 关背光 → 配唤醒源 → 挂起调度器+关中断 → 停 SysTick
  *    → 记录入睡 RTC 秒 → 进 Stop → (唤醒) → 判定唤醒源 → 恢复时钟/SysTick
  *    → 清/撤唤醒源 → 累计休眠时长 → 恢复中断+调度器 → 恢复摄像头
  */
static PowerWakeReason_t power_enter_stop(void)
{
    PowerWakeReason_t reason = POWER_WAKE_OTHER;
    uint32_t t_enter, t_exit, slept;

    printf("\r\n========== [Power] 准备进入低功耗 Stop ==========\r\n");
    printf("[Power] 当前状态=%s  已息屏=%lu ms\r\n",
           State_GetName(State_GetCurrent()),
           (unsigned long)ScreenSaver_GetDimmedMs());
    printf("[Power] 唤醒源: 触摸PEN(PB1/EXTI1) + RTC周期%us\r\n",
           (unsigned)POWER_SNIFF_PERIOD_S);
    printf("[Power] 即将关背光+停摄像头+进Stop, 串口将静默直到被唤醒...\r\n");
    /* 给 USART1 环形缓冲足够时间把上面几行真正发出去 (之后要关中断, TXE 中断不再触发) */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 1) 暂停摄像头 DCMI 捕获 (DMA 配置保留, 醒后 Resume 即可) */
    OV2640_DCMI_Suspend();

    /* 2) 关背光 (确保息屏) */
    LCD_LED_OFF();

    /* 3) 独占系统: 挂起调度 + 关中断 (PRIMASK).
     *    Cortex-M 特性: PRIMASK 屏蔽的中断仍可把 CPU 从 WFI 唤醒,
     *    ISR 不会立即执行, 待 __enable_irq() 后才跑 → 我们手动清标志即可。 */
    vTaskSuspendAll();
    __disable_irq();

    /* 4) 配置唤醒源: 触摸 PEN + RTC 周期 */
    power_enable_pen_wakeup();
    power_enable_rtc_wakeup();

    /* ★ 关键: 关掉所有"业务"NVIC 中断并清 pending.
     *   H7 的 Stop 模式下, 任何处于 pending 状态的 NVIC 中断都会让 WFI 立即返回.
     *   实测现象: 34474 次/0s "其它中断" 唤醒 → 正是 DCMI/DMA2_Stream7(摄像头帧中断)
     *   虽已 OV2640_DCMI_Suspend(), 但 NVIC pending 未清, WFI 一下去就弹回来.
     *   方案: 进 Stop 前关闭所有业务 IRQ + 清 pending, 只留已配好的唤醒源 (EXTI1/RTC_WKUP).
     *   醒后在 __enable_irq() 前重新使能, 让 HAL 正常工作. */
    HAL_NVIC_DisableIRQ(DCMI_PSSI_IRQn);    /* 摄像头帧中断 */
    HAL_NVIC_DisableIRQ(DMA2_Stream7_IRQn); /* DCMI DMA */
    HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn); /* SPI1 TX (LCD) */
    HAL_NVIC_DisableIRQ(SPI1_IRQn);         /* SPI1 错误 */
    HAL_NVIC_DisableIRQ(TIM7_IRQn);         /* LVGL 1ms tick */
    HAL_NVIC_DisableIRQ(DMA1_Stream1_IRQn); /* USART3 RX (ESP-01) */
    HAL_NVIC_DisableIRQ(DMA1_Stream2_IRQn); /* ADC2 (MAX9814) */
    HAL_NVIC_DisableIRQ(DMA1_Stream3_IRQn); /* USART2 RX (ESP32) */
    HAL_NVIC_DisableIRQ(USART1_IRQn);       /* 调试串口 */
    HAL_NVIC_DisableIRQ(USART2_IRQn);       /* ESP32 BLE */
    HAL_NVIC_DisableIRQ(USART3_IRQn);       /* ESP-01 WiFi */
    HAL_NVIC_DisableIRQ(ADC_IRQn);          /* ADC (MAX9814 回调) */

    /* 清所有业务中断的 NVIC pending bit (防止进 WFI 前已积压的中断立刻唤醒) */
    NVIC_ClearPendingIRQ(DCMI_PSSI_IRQn);
    NVIC_ClearPendingIRQ(DMA2_Stream7_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
    NVIC_ClearPendingIRQ(SPI1_IRQn);
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Stream1_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Stream2_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Stream3_IRQn);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(USART2_IRQn);
    NVIC_ClearPendingIRQ(USART3_IRQn);
    NVIC_ClearPendingIRQ(ADC_IRQn);

    /* 5) 停 SysTick, 否则其异常会立刻唤醒 CPU */
    HAL_SuspendTick();

    /* 6) 记录入睡时刻 (RTC 秒) */
    t_enter = power_rtc_seconds();

    /* 7) 进入 Stop (主调压器, WFI, D1 域) —— 阻塞点 */
    HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI, PWR_D1_DOMAIN);

    /* ===================== 被唤醒, 此时跑在 HSI ===================== */

    /* 清 SLEEPDEEP, 避免之后任何 WFI 误入深睡 */
    SCB->SCR &= ~((uint32_t)SCB_SCR_SLEEPDEEP_Msk);

    /* 8) 先重建时钟, 后面 printf / HAL_GetTick 才正常 */
    power_restore_clock();
    HAL_ResumeTick();

    /* 9) 判定唤醒源 (清标志前先读) */
    if (__HAL_GPIO_EXTI_GET_IT(TP_PEN_PIN) != 0u) {
        reason = POWER_WAKE_TOUCH;
    } else if (__HAL_RTC_WAKEUPTIMER_GET_FLAG(&hrtc, RTC_FLAG_WUTF) != 0u) {
        reason = POWER_WAKE_RTC;
    } else {
        reason = POWER_WAKE_OTHER;
    }
    /* 调试: 打印原始 EXTI/RTC 标志, 确认唤醒源判断 */
    printf("[Power] wake raw: EXTI_PR1=0x%08lX  RTC_ISR=0x%08lX\r\n",
           (unsigned long)EXTI->PR1,
           (unsigned long)RTC->ISR);

    /* 10) 撤销/清理唤醒源 */
    power_disable_pen_wakeup();
    power_disable_rtc_wakeup();

    /* 11) 估算并累计休眠时长 (RTC 日内秒, 处理跨日) */
    t_exit = power_rtc_seconds();
    slept  = (t_exit >= t_enter) ? (t_exit - t_enter) : (86400u - t_enter + t_exit);
    s_total_sleep_s += slept;
    s_sleep_count++;
    s_last_reason = reason;

    /* 12) 恢复所有业务中断 (在 __enable_irq 之前先使能, ISR 才能正常服务) */
    HAL_NVIC_EnableIRQ(DCMI_PSSI_IRQn);
    HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    HAL_NVIC_EnableIRQ(ADC_IRQn);

    /* 12.5) 重启 UART DMA 接收状态机 (关键!)
     * Stop 模式下外设时钟关闭, UART IDLE 使能位和 DMA 状态可能丢失.
     * 必须在恢复中断后、恢复调度前显式重启, 否则 RX 接收失效. */
    Power_RestartUART_RX();

    /* 13) 恢复中断 + 调度器 */
    __enable_irq();
    (void)xTaskResumeAll();

    /* 13) 恢复摄像头采集 */
    OV2640_DCMI_Resume();

    printf("[Power] 已唤醒! 原因=%s  本次休眠=%lus  (累计%lu次/%lus)\r\n",
           (reason == POWER_WAKE_TOUCH) ? "触摸PEN" :
           (reason == POWER_WAKE_RTC)   ? "RTC周期" : "其它中断",
           (unsigned long)slept,
           (unsigned long)s_sleep_count, (unsigned long)s_total_sleep_s);
    printf("==================================================\r\n\r\n");

    return reason;
}

/* ============================ 任务主体 ============================ */

void Power_Init(void)
{
    s_deep_sleep_enabled = 1;
    s_sleep_count        = 0;
    s_total_sleep_s      = 0;
    s_last_reason        = POWER_WAKE_NONE;
}

void Power_Task(void *pvParameters)
{
    (void)pvParameters;

    Power_Init();

    /* 等系统稳定 (LCD/Camera/State 初始化完成) 再开始管功耗 */
    vTaskDelay(pdMS_TO_TICKS(5000));
    printf("[Power] Task started (deep sleep %s)\r\n",
           s_deep_sleep_enabled ? "ENABLED" : "DISABLED");

    for (;;)
    {
        if (s_deep_sleep_enabled && power_sleep_allowed())
        {
            PowerWakeReason_t r = power_enter_stop();

            if (r == POWER_WAKE_TOUCH)
            {
                /* 用户点屏: 完整唤醒 (亮屏 + 重置 3min 息屏计时) */
                ScreenSaver_NotifyActivity();
            }
            else
            {
                /* RTC 嗅探唤醒: 不主动亮屏。给任务一个时间片刷新数据,
                 * 若期间检测到有人坐下 (压力恢复), 则亮屏唤醒; 否则继续睡。 */
                vTaskDelay(pdMS_TO_TICKS(POWER_SNIFF_AWAKE_MS));
                if (State_IsPersonPresent())
                {
                    ScreenSaver_NotifyActivity();
                }
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(POWER_MONITOR_PERIOD_MS));
        }
    }
}

/* ============================ 公共 API ============================ */

void Power_SetDeepSleepEnabled(int enable)
{
    s_deep_sleep_enabled = enable ? 1 : 0;
}

int Power_IsDeepSleepEnabled(void)
{
    return s_deep_sleep_enabled;
}

void Power_GetStats(uint32_t *sleep_count,
                    uint32_t *total_sleep_s,
                    PowerWakeReason_t *last_reason)
{
    if (sleep_count)   *sleep_count   = s_sleep_count;
    if (total_sleep_s) *total_sleep_s = s_total_sleep_s;
    if (last_reason)   *last_reason   = s_last_reason;
}
