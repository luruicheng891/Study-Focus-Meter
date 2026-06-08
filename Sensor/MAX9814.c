/**
  ******************************************************************************
  * @file    MAX9814.c
  * @brief   MAX9814 麦克风放大器 - ADC2 单端采集驱动
  * @note    寄存器级操作 (与 TIM7.c 风格一致), 不依赖 HAL_ADC 库
  *          时序 (官方参考手册 RM0468):
  *            1. ADVREGEN = 1           (电压稳压器上电)
  *            2. 等待 T_ADCVREG_STUP    (~20us)
  *            3. ADCAL  = 1             (启动校准)
  *            4. 等待 ADCAL = 0         (校准完成)
  *            5. ADEN   = 1             (使能 ADC)
  *            6. 等待 ADRDY = 1         (ADC 就绪)
  *            7. ADSTART= 1             (启动单次转换)
  *            8. 等待 EOC  = 1          (转换完成)
  *            9. 读取 DR 寄存器
  ******************************************************************************
  */

#include "MAX9814.h"
#include "FreeRTOS.h"
#include "task.h"

/* DMA 循环采集缓冲区: 放在 AXI SRAM (DMA 可访问, DTCM 不可!), 32 字节对齐
 * 地址 0x24040900: 紧接 USART3 RX 缓冲 (0x24040800~0x240408FF) 之后 */
static volatile uint16_t g_adc_dma_buf[MAX9814_DMA_BUF_LEN]
    __attribute__((section(".ARM.__at_0x24040900"), aligned(32)));

static uint8_t g_dma_started = 0;

/*============================================================================
 *                          私有函数 - GPIO 初始化
 *============================================================================*/
static void MAX9814_GPIO_Init(void)
{
    /* 使能 GPIOA 时钟 */
    MAX9814_GPIO_CLK_ENABLE();
    __NOP(); __NOP();

    /* PA7 配置为模拟输入 (MODER = 0b11) */
    /* 每位引脚占 2 个 bit, pin7 → bit 14,15 */
    MAX9814_GPIO_PORT->MODER &= ~(0x3U << (MAX9814_GPIO_PIN_NUM * 2U));
    MAX9814_GPIO_PORT->MODER |=  (0x3U << (MAX9814_GPIO_PIN_NUM * 2U));

    /* 无上拉/下拉 */
    MAX9814_GPIO_PORT->PUPDR &= ~(0x3U << (MAX9814_GPIO_PIN_NUM * 2U));

    /* 关闭数字输出 (默认复位后即关闭, 显式再写一次) */
    MAX9814_GPIO_PORT->OTYPER  &= ~(0x1U << MAX9814_GPIO_PIN_NUM);
}

/*============================================================================
 *                          私有函数 - 等待标志位
 *============================================================================*/
static uint8_t MAX9814_WaitFlag(volatile uint32_t *reg, uint32_t mask, uint8_t state, uint32_t timeout)
{
    uint32_t tick_start = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - tick_start) < timeout)
    {
        if (((*reg) & mask) ? state : !state)
        {
            return 0;  /* 成功 */
        }
        taskYIELD();  /* 让出 CPU，避免死等 */
    }
    return 1;  /* 超时 */
}

/*============================================================================
 *                          ADC 内部校准
 *============================================================================*/
uint8_t MAX9814_ADC_Calibrate(void)
{
    volatile uint32_t timeout;

    /* 确保 ADC 已关闭 (ADEN=0) 才能校准 */
    if (MAX9814_ADC->CR & ADC_CR_ADEN)
    {
        /* 安全写: 只置位 ADDIS, 不触发其他命令位 */
        MAX9814_ADC->CR = (MAX9814_ADC->CR & ~(ADC_CR_ADSTART | ADC_CR_JADSTART |
                           ADC_CR_ADSTP | ADC_CR_JADSTP | ADC_CR_ADCAL)) | ADC_CR_ADDIS;
        /* 忙等 ADEN 清除 */
        timeout = 1000000U;
        while (MAX9814_ADC->CR & ADC_CR_ADEN) {
            if (--timeout == 0U) return 1;
        }
    }

    /* 线性校准位 (ADCALLIN): STM32H723 建议置1 获取更精确校准 */
    /* 清除 ADCALDIF (单端模式校准) */
    /* 安全写 CR: 保持 ADVREGEN + BOOST, 置 ADCAL 启动校准 */
    {
        uint32_t cr_keep = MAX9814_ADC->CR;
        /* 只保留非命令位: ADVREGEN, BOOST */
        cr_keep &= (ADC_CR_ADVREGEN | ADC_CR_BOOST_Msk);
        /* 写入: 保持 + 启动校准 (ADCALDIF=0 → 单端) */
        MAX9814_ADC->CR = cr_keep | ADC_CR_ADCAL;
    }

    /* 忙等 ADCAL 自动清零 (校准完成), 不用 FreeRTOS tick, 校准约 几百us~几ms */
    timeout = 5000000U;  /* 约 10ms @ 550MHz */
    while (MAX9814_ADC->CR & ADC_CR_ADCAL)
    {
        if (--timeout == 0U) return 2;
    }

    return 0;
}

/*============================================================================
 *                       DMA 初始化 (ADC2 → DMA1_Stream2 循环)
 *============================================================================*/
static void MAX9814_DMA_Init(void)
{
    /* 1. 使能 DMA1 时钟 */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    __NOP(); __NOP();

    /* 2. 关闭 DMA Stream (修改配置前必须 EN=0) */
    MAX9814_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    while (MAX9814_DMA_STREAM->CR & DMA_SxCR_EN) { }

    /* 3. 配置 DMAMUX: 选择 ADC2 请求 (request 10) */
    MAX9814_DMAMUX_CHANNEL->CCR = MAX9814_DMAMUX_REQ_ADC2;

    /* 4. 配置外设地址 = ADC2 数据寄存器 */
    MAX9814_DMA_STREAM->PAR = (uint32_t)&MAX9814_ADC->DR;

    /* 5. 配置存储器地址 = DMA 缓冲区 (AXI SRAM) */
    MAX9814_DMA_STREAM->M0AR = (uint32_t)g_adc_dma_buf;

    /* 6. 传输数据个数 */
    MAX9814_DMA_STREAM->NDTR = MAX9814_DMA_BUF_LEN;

    /* 7. 配置 CR:
     *    DIR=00 (外设→存储器)
     *    CIRC=1 (循环模式)
     *    MINC=1 (存储器地址递增), PINC=0 (外设地址固定)
     *    MSIZE=01 (16-bit), PSIZE=01 (16-bit)
     *    PL=10 (高优先级) */
    MAX9814_DMA_STREAM->CR =
          DMA_SxCR_CIRC                       /* 循环模式 */
        | DMA_SxCR_MINC                       /* 存储器递增 */
        | DMA_SxCR_MSIZE_0                    /* MSIZE=01 (16-bit) */
        | DMA_SxCR_PSIZE_0                    /* PSIZE=01 (16-bit) */
        | DMA_SxCR_PL_1;                      /* 高优先级 */

    /* 8. 不使用 FIFO (直接模式) */
    MAX9814_DMA_STREAM->FCR &= ~DMA_SxFCR_DMDIS;

    /* 9. 使能 DMA Stream */
    MAX9814_DMA_STREAM->CR |= DMA_SxCR_EN;
}

/*============================================================================
 *                          ADC 初始化
 *============================================================================*/
uint8_t MAX9814_ADC_Init(void)
{
    volatile uint32_t timeout;

    /* 1. GPIO 初始化 */
    MAX9814_GPIO_Init();

    /* 2. 使能 ADC12 时钟 (总线/寄存器时钟) */
    MAX9814_ADC_CLK_ENABLE();
    __NOP(); __NOP(); __NOP(); __NOP();

    /* 2.5 选择 ADC 内核时钟源 (关键!)
     *     仅使能 ADC12EN 只开了总线时钟, 转换/校准需要 adc_ker_ck.
     *     ADCSEL 默认 = 00 (PLL2_P), 但本工程未开 PLL2 → 无时钟 → 校准超时!
     *     设置 ADCSEL = 10 (per_ck), per_ck 默认 = HSI 64MHz */
    RCC->D3CCIPR = (RCC->D3CCIPR & ~RCC_D3CCIPR_ADCSEL_Msk)
                   | (0x2U << RCC_D3CCIPR_ADCSEL_Pos);  /* 10 = per_ck */

    /* 2.6 配置 ADC 公共寄存器 CCR:
     *     CKMODE = 00 (使用异步内核时钟 adc_ker_ck, 即上面选的 per_ck)
     *     PRESC  = 0000 (不分频, 64MHz 在 BOOST=11 范围内) */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~(ADC_CCR_CKMODE_Msk | ADC_CCR_PRESC_Msk));

    /* 3. 退出深功耗模式: 先确保 DEEPPWD=0
     *    注意: DEEPPWD 位比较特殊, 写 CR 时只保留安全位 */
    MAX9814_ADC->CR = 0U;  /* 清除所有位 (包括 DEEPPWD), 全部归零 */

    /* 4. 使能内部电压稳压器 + 设置 BOOST
     *    ADVREGEN=1, BOOST=11, 其余位保持0 (不触发任何命令) */
    MAX9814_ADC->CR = ADC_CR_ADVREGEN | ADC_CR_BOOST;

    /* 5. 等待稳压器稳定 (T_ADCVREG_STUP ≥ 20us)
     *    用简单循环延时, 不依赖 FreeRTOS tick 精度 */
    {
        volatile uint32_t delay = 20000U;  /* 约 36us @ 550MHz (保守) */
        while (delay--) { __NOP(); }
    }

    /* 5.5 配置 PCSEL: 预选通道 (STM32H7 ADC1/ADC2 特有!)
     *     必须在校准和 ADEN 之前设置!
     *     不设置此位, ADC 输入多路复用器不会连接对应通道, 导致校准超时.
     *     PCSEL 寄存器在 struct 中名为 PCSEL_RES0 (对 ADC3 保留) */
    MAX9814_ADC->PCSEL_RES0 = (1U << MAX9814_ADC_CHANNEL);

    /* 6. ADC 内部校准 */
    if (MAX9814_ADC_Calibrate() != 0)
        return 2;

    /* 7. 配置 PCSEL: 已在步骤 5.5 完成 */

    /* 8. 配置 CFGR: 16-bit, 右对齐, 连续转换模式 + DMA 循环
     *    CONT=1 (连续转换), DMNGT=11 (DMA circular mode), OVRMOD=1 (覆盖旧数据) */
    MAX9814_ADC->CFGR = ADC_CFGR_CONT          /* 连续转换 */
                      | ADC_CFGR_DMNGT_0 | ADC_CFGR_DMNGT_1  /* DMNGT=11: DMA 循环模式 */
                      | ADC_CFGR_OVRMOD;       /* 溢出时覆盖 */

    /* 8. 配置 CFGR2: 保持默认 */
    MAX9814_ADC->CFGR2 = 0U;

    /* 9. 配置采样时间: 通道 7 设为 387.5 ADC clk (0b110)
     *    SMPR1: 通道 0~9 (3-bit 一组, 偏移 = ch * 3) */
    {
        const uint32_t ch   = MAX9814_ADC_CHANNEL;
        const uint32_t val  = MAX9814_ADC_SAMPLETIME_VAL;
        const uint32_t pos  = (ch < 10U) ? (ch * 3U) : ((ch - 10U) * 3U);
        volatile uint32_t *smpr = (ch < 10U) ? &MAX9814_ADC->SMPR1
                                             : &MAX9814_ADC->SMPR2;
        *smpr = (*smpr & ~(0x7U << pos)) | (val << pos);
    }

    /* 10. 配置规则组序列: 1 个转换, 通道 7, 放在 SQ1 */
    MAX9814_ADC->SQR1 = (0U << ADC_SQR1_L_Pos) |
                         (MAX9814_ADC_CHANNEL << ADC_SQR1_SQ1_Pos);

    /* 11. 配置预分频器 (ADCx_CCR): ADC 时钟选择
     *     CKMODE=00 (使用异步时钟 per_ck), PRESC=0000 (不分频)
     *     ADC12_CCR 是 ADC12_COMMON->CCR */
    /* 默认复位值已经是 per_ck, 不需额外设置 */

    /* 12. 清除 ADRDY 标志 (写1清除) 然后使能 ADC */
    MAX9814_ADC->ISR = ADC_ISR_ADRDY;

    /* 使能 ADC: 安全写 CR, 保持 ADVREGEN + BOOST, 加上 ADEN */
    MAX9814_ADC->CR = ADC_CR_ADVREGEN | ADC_CR_BOOST | ADC_CR_ADEN;

    /* 13. 等待 ADRDY=1 (ADC 就绪) */
    timeout = 1000000U;
    while (!(MAX9814_ADC->ISR & ADC_ISR_ADRDY))
    {
        if (--timeout == 0U) return 3;
    }

    /* 14. 初始化 DMA (循环模式) */
    MAX9814_DMA_Init();

    /* 15. 启动连续转换 (ADSTART=1)
     *     连续模式下 ADC 会自动以最高速率不断转换, DMA 自动搬运到缓冲区 */
    MAX9814_ADC->CR = ADC_CR_ADVREGEN | ADC_CR_BOOST | ADC_CR_ADEN | ADC_CR_ADSTART;

    g_dma_started = 1;

    return 0;
}

/*============================================================================
 *               读取 DMA 缓冲区中的瞬时值 (任取一个采样点)
 *============================================================================*/
uint16_t MAX9814_ADC_Read(void)
{
    if (!g_dma_started) return 0xFFFFU;

    /* DMA 缓冲是 Cacheable 区域, 读前失效 D-Cache 以读到最新数据 */
    SCB_InvalidateDCache_by_Addr((uint32_t *)g_adc_dma_buf, sizeof(g_adc_dma_buf));

    return g_adc_dma_buf[0];
}

/*============================================================================
 *                   读取 DMA 缓冲区平均值 (DC 偏置)
 *============================================================================*/
uint16_t MAX9814_ADC_ReadAvg(uint8_t times)
{
    uint32_t sum = 0;
    uint32_t i;
    (void)times;

    if (!g_dma_started) return 0xFFFFU;

    SCB_InvalidateDCache_by_Addr((uint32_t *)g_adc_dma_buf, sizeof(g_adc_dma_buf));

    for (i = 0; i < MAX9814_DMA_BUF_LEN; i++)
        sum += g_adc_dma_buf[i];

    return (uint16_t)(sum / MAX9814_DMA_BUF_LEN);
}

/*============================================================================
 *                          读取电压值 (mV)
 *============================================================================*/
uint32_t MAX9814_ADC_ReadVoltage_mV(void)
{
    uint16_t adc = MAX9814_ADC_ReadAvg(0);
    if (adc == 0xFFFFU) return 0xFFFFFFFFU;

    return (uint32_t)((uint64_t)adc * MAX9814_VREF_MV / MAX9814_ADC_RESOLUTION);
}

/*============================================================================
 *                    读取声音强度百分比 (DMA 缓冲 RMS 法)
 *  说明: ADC 在硬件层面以固定速率连续转换, DMA 自动写入循环缓冲.
 *        采样时序完全不受 RTOS 任务调度影响, 因此 RMS 稳定可靠.
 *============================================================================*/
uint8_t MAX9814_GetSoundPercent(uint8_t samples)
{
    uint32_t sum = 0;
    uint64_t sum_sq = 0;
    uint32_t i;
    uint32_t avg;
    uint32_t mean_sq;
    uint32_t rms;
    uint32_t percent;
    (void)samples;

    if (!g_dma_started) return 0;

    /* 失效 D-Cache, 读取 DMA 最新写入的完整缓冲 */
    SCB_InvalidateDCache_by_Addr((uint32_t *)g_adc_dma_buf, sizeof(g_adc_dma_buf));

    /* 1. 求整个缓冲的平均值 (DC 偏置) */
    for (i = 0; i < MAX9814_DMA_BUF_LEN; i++)
        sum += g_adc_dma_buf[i];
    avg = sum / MAX9814_DMA_BUF_LEN;

    /* 2. 求去直流后的平方和 (用 uint64 防溢出) */
    for (i = 0; i < MAX9814_DMA_BUF_LEN; i++)
    {
        int32_t diff = (int32_t)g_adc_dma_buf[i] - (int32_t)avg;
        sum_sq += (uint64_t)((int64_t)diff * diff);
    }

    /* 3. 均方值 */
    mean_sq = (uint32_t)(sum_sq / MAX9814_DMA_BUF_LEN);

    /* 4. 整数开方得 RMS */
    rms = 0;
    {
        uint32_t temp = mean_sq;
        uint32_t bit = 1UL << 30;
        while (bit > temp) bit >>= 2;
        while (bit != 0) {
            if (temp >= rms + bit) {
                temp -= rms + bit;
                rms = (rms >> 1) + bit;
            } else {
                rms >>= 1;
            }
            bit >>= 2;
        }
    }

    /* 5. 映射到 0~100%
     *    rms 单位为 ADC count (16-bit). 无声时约 50~300 (底噪),
     *    正常说话 1000~6000, 大声更高.
     *    减去底噪后以 FULL_SCALE 为满量程 */
    #define MAX9814_NOISE_FLOOR  200U    /* 底噪 RMS (可调) */
    #define MAX9814_FULL_SCALE   8000U   /* 满量程 RMS (可调) */

    if (rms <= MAX9814_NOISE_FLOOR) return 0;

    percent = (uint32_t)(rms - MAX9814_NOISE_FLOOR) * 100U / MAX9814_FULL_SCALE;
    if (percent > 100U) percent = 100U;

    return (uint8_t)percent;
}