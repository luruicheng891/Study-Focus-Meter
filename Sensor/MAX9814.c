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

/*============================================================================
 *                          私有函数 - GPIO 初始化
 *============================================================================*/
static void MAX9814_GPIO_Init(void)
{
    /* 使能 GPIOF 时钟 */
    MAX9814_GPIO_CLK_ENABLE();
    __NOP(); __NOP();

    /* PF14 配置为模拟输入 (MODER = 0b11) */
    /* 每位引脚占 2 个 bit, pin14 → bit 28,29 */
    MAX9814_GPIO_PORT->MODER &= ~(0x3U << (MAX9814_GPIO_PIN * 2U));
    MAX9814_GPIO_PORT->MODER |=  (0x3U << (MAX9814_GPIO_PIN * 2U));

    /* 无上拉/下拉 */
    MAX9814_GPIO_PORT->PUPDR &= ~(0x3U << (MAX9814_GPIO_PIN * 2U));

    /* 关闭数字输出 (默认复位后即关闭, 显式再写一次) */
    MAX9814_GPIO_PORT->OTYPER  &= ~(0x1U << MAX9814_GPIO_PIN);
}

/*============================================================================
 *                          私有函数 - 简易延时
 *============================================================================*/
static void MAX9814_Delay(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++)
    {
        __NOP();
    }
}

/*============================================================================
 *                          私有函数 - 等待标志位
 *============================================================================*/
static uint8_t MAX9814_WaitFlag(volatile uint32_t *reg, uint32_t mask, uint8_t state, uint32_t timeout)
{
    while (timeout--)
    {
        if (((*reg) & mask) ? state : !state)
        {
            return 0;  /* 成功 */
        }
    }
    return 1;  /* 超时 */
}

/*============================================================================
 *                          ADC 内部校准
 *============================================================================*/
uint8_t MAX9814_ADC_Calibrate(void)
{
    /* 关闭 ADC 才能进入校准 */
    if (MAX9814_ADC->CR & ADC_CR_ADEN)
    {
        MAX9814_ADC->CR |=  ADC_CR_ADDIS;
        if (MAX9814_WaitFlag(&MAX9814_ADC->CR, ADC_CR_ADDIS, 0, 100000U))
            return 1;
    }

    /* 启动单端模式校准 (ADCALDIF = 0) */
    MAX9814_ADC->CR &= ~ADC_CR_ADCALDIF;
    MAX9814_ADC->CR |=  ADC_CR_ADCAL;

    /* 等待 ADCAL 复位 (校准结束) */
    if (MAX9814_WaitFlag(&MAX9814_ADC->CR, ADC_CR_ADCAL, 0, 1000000U))
        return 2;

    return 0;
}

/*============================================================================
 *                          ADC 初始化
 *============================================================================*/
uint8_t MAX9814_ADC_Init(void)
{
    /* 1. GPIO 初始化 */
    MAX9814_GPIO_Init();

    /* 2. 使能 ADC12 时钟 */
    MAX9814_ADC_CLK_ENABLE();
    __NOP(); __NOP();

    /* 3. 退出深功耗模式, 启动内部电压稳压器 */
    MAX9814_ADC->CR &= ~ADC_CR_DEEPPWD;
    MAX9814_ADC->CR |=  ADC_CR_ADVREGEN;

    /* 4. 等待稳压器稳定 (T_ADCVREG_STUP ≥ 20us @ 550MHz HCLK) */
    MAX9814_Delay(20000);

    /* 5. ADC 内部校准 */
    if (MAX9814_ADC_Calibrate() != 0)
        return 2;

    /* 6. 配置 CFGR: 12-bit 分辨率, 右对齐, 单次转换模式 */
    MAX9814_ADC->CFGR = 0U;  /* RES=0 (12bit), ALIGN=0 (right), CONT=0, EXTEN=00 (SW trigger) */

    /* 7. 配置 CFGR2: 保持默认 (ROMP 路径, BOOST 关闭即可) */
    MAX9814_ADC->CFGR2 = 0U;

    /* 8. 配置采样时间: 把对应通道采样时间设为 6.5 个 ADC clk
     *    SMPR1: 通道 0~9  (3-bit 一组, 偏移 = ch * 3)
     *    SMPR2: 通道 10~19 (3-bit 一组, 偏移 = (ch-10) * 3)
     *    0b001 = 6.5 ADC clock cycles
     */
    {
        const uint32_t ch   = MAX9814_ADC_CHANNEL;
        const uint32_t val  = MAX9814_ADC_SAMPLETIME_VAL;
        const uint32_t pos  = (ch < 10U) ? (ch * 3U) : ((ch - 10U) * 3U);
        volatile uint32_t *smpr = (ch < 10U) ? &MAX9814_ADC->SMPR1
                                             : &MAX9814_ADC->SMPR2;
        *smpr = (*smpr & ~(0x7U << pos)) | (val << pos);
    }

    /* 9. 配置规则组序列: 1 个转换, 通道 = MAX9814_ADC_CHANNEL, 放在第 1 个 rank
     *    SQR1: bit[6:10] = SQ1 (第 1 转换通道), bit[0:3] = L-1 (序列长度-1)
     */
    MAX9814_ADC->SQR1 = (0U << ADC_SQR1_L_Pos) |                       /* 1 conversion */
                         (MAX9814_ADC_CHANNEL << ADC_SQR1_SQ1_Pos);

    /* 10. 使能 ADC (ADEN) */
    MAX9814_ADC->CR |= ADC_CR_ADEN;

    /* 11. 等待 ADRDY = 1 (ADC 就绪) */
    if (MAX9814_WaitFlag(&MAX9814_ADC->ISR, ADC_ISR_ADRDY, 1, 100000U))
        return 3;

    return 0;
}

/*============================================================================
 *                          单次读取
 *============================================================================*/
uint16_t MAX9814_ADC_Read(void)
{
    uint32_t timeout;

    /* 1. 启动规则组转换 */
    MAX9814_ADC->CR |= ADC_CR_ADSTART;

    /* 2. 等待转换结束 (EOC = End Of Conversion) */
    timeout = 100000U;
    while (timeout--)
    {
        if (MAX9814_ADC->ISR & ADC_ISR_EOSMP)
        {
            break;
        }
    }
    if (timeout == 0xFFFFFFFFU)
        return 0xFFFFU;  /* 超时 */

    /* 3. 等待 EOC (实际数据就绪) */
    timeout = 100000U;
    while (timeout--)
    {
        if (MAX9814_ADC->ISR & ADC_ISR_EOC)
        {
            break;
        }
    }
    if (timeout == 0xFFFFFFFFU)
        return 0xFFFFU;

    /* 4. 读取 12-bit 数据, 清除 EOC */
    uint16_t value = (uint16_t)(MAX9814_ADC->DR & 0xFFFFU);
    MAX9814_ADC->ISR = ADC_ISR_EOC;  /* 写 1 清除 */

    return value;
}

/*============================================================================
 *                          多次采样取平均
 *============================================================================*/
uint16_t MAX9814_ADC_ReadAvg(uint8_t times)
{
    uint32_t sum = 0;
    uint8_t  valid_count = 0;
    uint8_t  i;

    if (times == 0) times = 1;

    for (i = 0; i < times; i++)
    {
        uint16_t val = MAX9814_ADC_Read();
        if (val != 0xFFFFU)
        {
            sum += val;
            valid_count++;
        }
    }

    if (valid_count == 0)
        return 0xFFFFU;

    return (uint16_t)(sum / valid_count);
}

/*============================================================================
 *                          读取电压值 (mV)
 *============================================================================*/
uint32_t MAX9814_ADC_ReadVoltage_mV(void)
{
    uint16_t adc = MAX9814_ADC_Read();
    if (adc == 0xFFFFU) return 0xFFFFFFFFU;

    /* V = ADC * Vref / 4095 */
    return (uint32_t)((uint64_t)adc * MAX9814_VREF_MV / MAX9814_ADC_RESOLUTION);
}
