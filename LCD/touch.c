/**
  ******************************************************************************
  * @file    touch.c
  * @brief   XPT2046 触摸驱动 (STM32H723)
  *         - 软件模拟 SPI (CPOL=0, CPHA=0)
  *         - 校准参数保存在内部 FLASH (Bank2 Sector7 末尾 0x080FFF00)
  *         - HAL GPIO 操作
  ******************************************************************************
  */

#include "touch.h"
#include "lcd.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/*============================================================================
 *                          默认 XPT2046 控制字节
 *  bit7   = S  (始终为 1, 表示开始)
 *  bit6-4 = A2-A0  通道选择:
 *               001 = X 坐标
 *               101 = Y 坐标
 *  bit3   = MODE  (12位/8位, 0=12位)
 *  bit2   = SER/DFR (单端/差分, 0=差分, 触摸用差分)
 *  bit1   = PD1, bit0 = PD0  (掉电/省电模式, 00=始终供电)
 *
 *  读 X:  1 001 0 0 00  = 0x90 (某些固件是 0xD0)
 *  读 Y:  1 101 0 0 00  = 0xD0
 *  注: X/Y 命令字的方向与 touchtype 有关, 在 TP_Get_Calibration() 中动态调整.
 *============================================================================*/
uint8_t CMD_RDX = 0xD0;
uint8_t CMD_RDY = 0x90;

/*============================================================================
 *                              触摸设备实例
 *============================================================================*/
_m_tp_dev tp_dev = {
    TP_Init,
    TP_Scan,
    TP_Calibrate,
    0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*============================================================================
 *                              微秒延时 (DWT 周期计数器)
 *  使用 Cortex-M7 内置 DWT->CYCCNT, 精度 = 1 个 CPU 周期.
 *  第一次调用时会自动使能 DWT.  H7 默认不开启, 需要打开 TRCENA + CYCCNTENA.
 *============================================================================*/
static volatile uint8_t s_dwt_inited = 0;

static void TP_DwtInit(void)
{
    if (s_dwt_inited) return;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;     /* 打开 DWT */
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;               /* 启动 CYCCNT */
    s_dwt_inited = 1;
}

static void TP_Delay_Us(uint32_t us)
{
    if (!s_dwt_inited) TP_DwtInit();
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000U);
    /* 处理 CYCCNT 32 位回绕 */
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) { __NOP(); }
}

#if (TP_SPI_DELAY_US == 0U)
  #define TP_SPI_DELAY()  __NOP()
#else
  #define TP_SPI_DELAY()  TP_Delay_Us(TP_SPI_DELAY_US)
#endif

/*============================================================================
 *                              GPIO 初始化
 *============================================================================*/
static void TP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    TP_GPIO_CLK_ENABLE();

    /* PEN: 输入上拉 (默认高 = 未触摸, 按下时拉低) */
    gpio.Pin   = TP_PEN_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(TP_PEN_PORT, &gpio);

    /* MISO: 浮空输入 */
    gpio.Pin   = TP_MISO_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(TP_MISO_PORT, &gpio);

    /* MOSI / SCK / CS: 推挽输出, 高速 */
    gpio.Pin   = TP_MOSI_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TP_MOSI_PORT, &gpio);

    gpio.Pin   = TP_SCK_PIN;
    HAL_GPIO_Init(TP_SCK_PORT, &gpio);

    gpio.Pin   = TP_CS_PIN;
    HAL_GPIO_Init(TP_CS_PORT, &gpio);

    /* 空闲状态: CS 高, SCK 低, MOSI 低 */
    TP_CS_HIGH();
    TP_SCK_LOW();
    TP_MOSI_LOW();
}

/*============================================================================
 *                          软件 SPI 位操作 (XPT2046)
 *  CPOL=0 CPHA=0: SCK 空闲低, 上升沿采样.
 *============================================================================*/
static void TP_SPI_WriteByte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80U) TP_MOSI_HIGH();
        else              TP_MOSI_LOW();
        data <<= 1;
        TP_SCK_LOW();
        TP_SPI_DELAY();
        TP_SCK_HIGH();          /* 上升沿: 从机采样 MOSI */
        TP_SPI_DELAY();
    }
    TP_SCK_LOW();
}

static uint16_t TP_SPI_ReadWord(void)
{
    uint16_t data = 0;
    for (uint8_t i = 0; i < 16; i++) {
        data <<= 1;
        TP_SCK_LOW();
        TP_SPI_DELAY();
        TP_SCK_HIGH();          /* 上升沿: 主机采样 MISO */
        TP_SPI_DELAY();
        if (TP_MISO_READ()) data |= 0x0001U;
    }
    TP_SCK_LOW();
    return data;
}

/*============================================================================
 *  TP_Read_AD: 发送控制字节, 等待 ADC, 读 16 bit 数据 (高 12 位有效)
 *============================================================================*/
uint16_t TP_Read_AD(uint8_t CMD)
{
    uint16_t Num = 0;

    TP_SCK_LOW();
    TP_MOSI_LOW();
    TP_CS_LOW();

    TP_SPI_WriteByte(CMD);
    TP_Delay_Us(6);              /* ADS7846 转换时间最长 6us */

    /* 多给一个时钟, 跳过 BUSY 位 */
    TP_SCK_LOW(); TP_Delay_Us(1);
    TP_SCK_HIGH(); TP_Delay_Us(1);
    TP_SCK_LOW();

    Num = TP_SPI_ReadWord();
    Num >>= 4;                   /* 12 位有效 */
    TP_CS_HIGH();
    return Num;
}

/*============================================================================
 *  多次采样 + 排序去极值 + 求平均 (软件滤波)
 *============================================================================*/
#define READ_TIMES   5
#define LOST_VAL     1

uint16_t TP_Read_XOY(uint8_t xy)
{
    uint16_t i, j, buf[READ_TIMES], sum = 0, tmp;

    for (i = 0; i < READ_TIMES; i++) buf[i] = TP_Read_AD(xy);

    /* 冒泡排序 */
    for (i = 0; i < READ_TIMES - 1; i++) {
        for (j = i + 1; j < READ_TIMES; j++) {
            if (buf[i] > buf[j]) {
                tmp   = buf[i];
                buf[i] = buf[j];
                buf[j] = tmp;
            }
        }
    }
    for (i = LOST_VAL; i < READ_TIMES - LOST_VAL; i++) sum += buf[i];
    return (uint16_t)(sum / (READ_TIMES - 2 * LOST_VAL));
}

uint8_t TP_Read_XY(uint16_t *x, uint16_t *y)
{
    uint16_t xtemp = TP_Read_XOY(CMD_RDX);
    uint16_t ytemp = TP_Read_XOY(CMD_RDY);
    *x = xtemp;
    *y = ytemp;
    return 1;
}

/* 两次采样偏差不能超过 ERR_RANGE, 否则视为抖动丢弃 */
#define ERR_RANGE  50

uint8_t TP_Read_XY2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1, x2, y2;

    if (TP_Read_XY(&x1, &y1) == 0) return 0;
    if (TP_Read_XY(&x2, &y2) == 0) return 0;

    if (((x2 <= x1 && x1 < x2 + ERR_RANGE) || (x1 <= x2 && x2 < x1 + ERR_RANGE)) &&
        ((y2 <= y1 && y1 < y2 + ERR_RANGE) || (y1 <= y2 && y2 < y1 + ERR_RANGE))) {
        *x = (x1 + x2) / 2U;
        *y = (y1 + y2) / 2U;
        return 1;
    }
    return 0;
}

/*============================================================================
 *                          校准屏幕绘图辅助
 *============================================================================*/
void TP_Drow_Touch_Point(uint16_t x, uint16_t y, uint16_t color)
{
    POINT_COLOR = color;
    LCD_DrawLine(x - 12, y, x + 13, y);   /* 横线 */
    LCD_DrawLine(x, y - 12, x, y + 13);   /* 竖线 */
    LCD_DrawPoint(x + 1, y + 1);
    LCD_DrawPoint(x - 1, y + 1);
    LCD_DrawPoint(x + 1, y - 1);
    LCD_DrawPoint(x - 1, y - 1);
    /* 简单圆环: 用 4 个角点代替 gui_circle */
    {
        int16_t r = 6;
        int16_t px, py;
        for (uint8_t a = 0; a < 16; a++) {
            px = (int16_t)x + (int16_t)(r * cos((double)a * 3.14159 / 8.0));
            py = (int16_t)y + (int16_t)(r * sin((double)a * 3.14159 / 8.0));
            LCD_DrawPoint((uint16_t)px, (uint16_t)py);
        }
    }
}

void TP_Draw_Big_Point(uint16_t x, uint16_t y, uint16_t color)
{
    POINT_COLOR = color;
    LCD_DrawPoint(x,     y);
    LCD_DrawPoint(x + 1, y);
    LCD_DrawPoint(x,     y + 1);
    LCD_DrawPoint(x + 1, y + 1);
}

/* 校验失败时, 用纯色块提示用户 (无文本 LCD 驱动时的替代方案).
 * 通过在屏幕中央画一个红色方块 + 几个色点表示"再来一次".
 * 同时通过 UART (printf) 输出详细坐标供调试. */
void TP_Adj_Info_Show(uint16_t x0, uint16_t y0,
                      uint16_t x1, uint16_t y1,
                      uint16_t x2, uint16_t y2,
                      uint16_t x3, uint16_t y3,
                      uint16_t fac)
{
    uint16_t w = lcddev.width;
    uint16_t h = lcddev.height;

    /* 在屏幕中央画一个醒目的红色方块表示"校验失败, 请重新校准" */
    LCD_Fill((uint16_t)(w / 2 - 30), (uint16_t)(h / 2 - 30),
             (uint16_t)(w / 2 + 30), (uint16_t)(h / 2 + 30), RED);

    /* UART 输出校准数据, 便于调试 */
    printf("[TP] Calib FAIL: p1=(%u,%u) p2=(%u,%u) p3=(%u,%u) p4=(%u,%u) fac=%u\r\n",
           (unsigned)x0, (unsigned)y0, (unsigned)x1, (unsigned)y1,
           (unsigned)x2, (unsigned)y2, (unsigned)x3, (unsigned)y3,
           (unsigned)fac);
}

/*============================================================================
 *                       内部 FLASH 校准读写 (移植点 2)
 *  使用 FLASHWORD (32 字节) 编程, 目标地址必须 32 字节对齐;
 *  为避免编译器拒绝 "auto 变量对齐 > 8 字节", 源 buffer 用 static.
 *  H7 内部 FLASH 擦写寿命约 10K 次, 校准只在用户触发时保存.
 *  注意: STM32H7 默认开启 D-Cache, 写 FLASH 后需 invalidate 对应 cache 行.
 *============================================================================*/
/* 32 字节对齐的兼容宏 (GCC / Keil AC5 / Keil AC6 / IAR) — 只对 static/global 变量使用 */
#if defined(__GNUC__) || defined(__clang__)
  #define TP_ALIGN_32   __attribute__((aligned(32)))
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
  #define TP_ALIGN_32   __align(32)
#elif defined(__IAR_SYSTEMS_ICC__)
  #define TP_ALIGN_32   _Pragma("data_alignment=32")
#else
  #define TP_ALIGN_32
#endif

/* 32 字节对齐的编程缓冲 (static 变量, 不受 auto 对齐限制) */
TP_ALIGN_32 static union {
    Calib_t  c;
    uint32_t w[8];
} s_flash_buf;

static HAL_StatusTypeDef TP_FLASH_EraseAndProgram(const uint32_t *data32)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0;

    HAL_FLASH_Unlock();

#if TP_FLASH_USE_DUAL_BANK
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_ALL_ERRORS_BANK2);
#else
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1);
#endif

    /* 整扇区擦除 (单 Bank 下 Sector7 = 128KB) - 校准数据只占其中最后 32 字节 */
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Banks        = TP_FLASH_CALIB_BANK;
    erase.Sector       = TP_FLASH_CALIB_SECTOR;
    erase.NbSectors    = 1U;
    erase.VoltageRange = TP_FLASH_VOLTAGE_RANGE;
    status = HAL_FLASHEx_Erase(&erase, &sector_error);
    if (status != HAL_OK) {
        HAL_FLASH_Lock();
        return status;
    }

    /* 写一个 FLASHWORD (32 字节) - 源 buffer 必须是 32 字节对齐 */
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                               TP_FLASH_CALIB_ADDR,
                               (uint32_t)data32);

    HAL_FLASH_Lock();
    return status;
}

static void TP_FLASH_Read(Calib_t *data)
{
    /* 直接从 FLASH 地址拷贝 (32 字节) */
    memcpy(data, (const void *)TP_FLASH_CALIB_ADDR, sizeof(Calib_t));
}

/*============================================================================
 *  TP_Save_Calibration: 把当前 tp_dev 的校准参数写入 FLASH
 *============================================================================*/
uint8_t TP_Save_Calibration(void)
{
    s_flash_buf.c.magic     = TP_CALIB_MAGIC;
    s_flash_buf.c.xfac      = tp_dev.xfac;
    s_flash_buf.c.yfac      = tp_dev.yfac;
    s_flash_buf.c.xoff      = tp_dev.xoff;
    s_flash_buf.c.yoff      = tp_dev.yoff;
    s_flash_buf.c.touchtype = tp_dev.touchtype;
    memset(s_flash_buf.c.reserved, 0xFF, sizeof(s_flash_buf.c.reserved));

    /* 写之前清空 D-Cache (源 buffer 在 RAM 中) */
    SCB_CleanDCache_by_Addr((uint32_t *)&s_flash_buf, sizeof(s_flash_buf));

    /* 擦写期间关闭中断, 防止 ISR/USB/HAL/调度器踩到 FLASH */
    __disable_irq();
    HAL_StatusTypeDef st = TP_FLASH_EraseAndProgram(s_flash_buf.w);
    __enable_irq();

    /* 写之后 invalidate D-Cache (目标地址在 FLASH) */
    SCB_InvalidateDCache_by_Addr((uint32_t *)TP_FLASH_CALIB_ADDR, sizeof(Calib_t));

    return (st == HAL_OK) ? 1U : 0U;
}

/*============================================================================
 *  TP_Get_Calibration: 从 FLASH 读取校准. 1=成功, 0=失败/未校准.
 *============================================================================*/
uint8_t TP_Get_Calibration(void)
{
    Calib_t calib;
    TP_FLASH_Read(&calib);

    if (calib.magic != TP_CALIB_MAGIC) {
        return 0;   /* 首次上电或数据损坏 */
    }

    tp_dev.xfac      = calib.xfac;
    tp_dev.yfac      = calib.yfac;
    tp_dev.xoff      = calib.xoff;
    tp_dev.yoff      = calib.yoff;
    tp_dev.touchtype = calib.touchtype;

    if (tp_dev.touchtype) {   /* X/Y 方向与屏幕相反 */
        CMD_RDX = 0x90;
        CMD_RDY = 0xD0;
    } else {
        CMD_RDX = 0xD0;
        CMD_RDY = 0x90;
    }
    return 1;
}

/*============================================================================
 *  TP_Calibrate: 4 点校准流程
 *============================================================================*/
void TP_Calibrate(void)
{
    uint16_t pos_temp[4][2];   /* 4 个校准点的物理坐标 */
    uint8_t  cnt = 0;
    uint16_t d1, d2;
    uint32_t tem1, tem2;
    double   fac;
    uint16_t outtime = 0;

    POINT_COLOR = BLUE;
    BACK_COLOR  = WHITE;
    LCD_Clear(WHITE);

    /* 无 LCD 文本函数, 通过 UART 提示用户; 屏幕留白等待触摸校准 */
    POINT_COLOR = RED;
    printf("[TP] === Calibration start ===\r\n");
    printf("[TP] Please tap the 4 cross marks in order.\r\n");
    printf("[TP] Each cross is drawn at one screen corner.\r\n");

    TP_Drow_Touch_Point(20, 20, RED);  /* 第 1 个校准点 */
    tp_dev.sta  = 0;
    tp_dev.xfac = 0.0f;                /* 标记未校准 */

    while (1) {
        tp_dev.scan(1);                /* 物理坐标扫描 */
        if ((tp_dev.sta & 0xC0) == TP_CATH_PRES) {
            outtime = 0;
            tp_dev.sta &= ~(1 << 6);

            pos_temp[cnt][0] = tp_dev.x;
            pos_temp[cnt][1] = tp_dev.y;
            cnt++;

            switch (cnt) {
                case 1:
                    TP_Drow_Touch_Point(20, 20, WHITE);
                    TP_Drow_Touch_Point((uint16_t)(lcddev.width - 20), 20, RED);
                    break;
                case 2:
                    TP_Drow_Touch_Point((uint16_t)(lcddev.width - 20), 20, WHITE);
                    TP_Drow_Touch_Point(20, (uint16_t)(lcddev.height - 20), RED);
                    break;
                case 3:
                    TP_Drow_Touch_Point(20, (uint16_t)(lcddev.height - 20), WHITE);
                    TP_Drow_Touch_Point((uint16_t)(lcddev.width - 20),
                                         (uint16_t)(lcddev.height - 20), RED);
                    break;
                case 4:
                    /* 验证 1-2 与 3-4 边长比 (矩形对边应近似相等) */
                    tem1 = (uint32_t)abs((int)pos_temp[0][0] - (int)pos_temp[1][0]);
                    tem2 = (uint32_t)abs((int)pos_temp[0][1] - (int)pos_temp[1][1]);
                    tem1 = tem1 * tem1; tem2 = tem2 * tem2;
                    d1 = (uint16_t)sqrt((double)(tem1 + tem2));

                    tem1 = (uint32_t)abs((int)pos_temp[2][0] - (int)pos_temp[3][0]);
                    tem2 = (uint32_t)abs((int)pos_temp[2][1] - (int)pos_temp[3][1]);
                    tem1 = tem1 * tem1; tem2 = tem2 * tem2;
                    d2 = (uint16_t)sqrt((double)(tem1 + tem2));

                    fac = (d1 == 0) ? 0.0 : (double)d1 / (double)d2;
                    if (fac < 0.95 || fac > 1.05 || d1 == 0 || d2 == 0) {
                        cnt = 0;
                        TP_Drow_Touch_Point((uint16_t)(lcddev.width - 20),
                                            (uint16_t)(lcddev.height - 20), WHITE);
                        TP_Drow_Touch_Point(20, 20, RED);
                        TP_Adj_Info_Show(pos_temp[0][0], pos_temp[0][1],
                                         pos_temp[1][0], pos_temp[1][1],
                                         pos_temp[2][0], pos_temp[2][1],
                                         pos_temp[3][0], pos_temp[3][1],
                                         (uint16_t)(fac * 100.0));
                        continue;
                    }

                    /* 验证 1-3 与 2-4 边长比 */
                    tem1 = (uint32_t)abs((int)pos_temp[0][0] - (int)pos_temp[2][0]);
                    tem2 = (uint32_t)abs((int)pos_temp[0][1] - (int)pos_temp[2][1]);
                    tem1 = tem1 * tem1; tem2 = tem2 * tem2;
                    d1 = (uint16_t)sqrt((double)(tem1 + tem2));

                    tem1 = (uint32_t)abs((int)pos_temp[1][0] - (int)pos_temp[3][0]);
                    tem2 = (uint32_t)abs((int)pos_temp[1][1] - (int)pos_temp[3][1]);
                    tem1 = tem1 * tem1; tem2 = tem2 * tem2;
                    d2 = (uint16_t)sqrt((double)(tem1 + tem2));

                    fac = (d1 == 0) ? 0.0 : (double)d1 / (double)d2;
                    if (fac < 0.95 || fac > 1.05) {
                        cnt = 0;
                        TP_Drow_Touch_Point((uint16_t)(lcddev.width - 20),
                                            (uint16_t)(lcddev.height - 20), WHITE);
                        TP_Drow_Touch_Point(20, 20, RED);
                        TP_Adj_Info_Show(pos_temp[0][0], pos_temp[0][1],
                                         pos_temp[1][0], pos_temp[1][1],
                                         pos_temp[2][0], pos_temp[2][1],
                                         pos_temp[3][0], pos_temp[3][1],
                                         (uint16_t)(fac * 100.0));
                        continue;
                    }

                    /* 计算校准系数 (仿射变换 x' = a*x + b, y' = c*y + d) */
                    tp_dev.xfac = (float)(lcddev.width - 40) /
                                  (float)(pos_temp[1][0] - pos_temp[0][0]);
                    tp_dev.xoff = (int16_t)((lcddev.width -
                                  tp_dev.xfac * (pos_temp[1][0] + pos_temp[0][0])) / 2);

                    tp_dev.yfac = (float)(lcddev.height - 40) /
                                  (float)(pos_temp[2][1] - pos_temp[0][1]);
                    tp_dev.yoff = (int16_t)((lcddev.height -
                                  tp_dev.yfac * (pos_temp[2][1] + pos_temp[0][1])) / 2);

                    if (fabsf(tp_dev.xfac) > 2.0f || fabsf(tp_dev.yfac) > 2.0f) {
                        cnt = 0;
                        TP_Drow_Touch_Point((uint16_t)(lcddev.width - 20),
                                            (uint16_t)(lcddev.height - 20), WHITE);
                        TP_Drow_Touch_Point(20, 20, RED);
                        /* 翻转 touchtype 重试 */
                        printf("[TP] Need readjust! Flipping touchtype...\r\n");
                        tp_dev.touchtype = !tp_dev.touchtype;
                        if (tp_dev.touchtype) { CMD_RDX = 0x90; CMD_RDY = 0xD0; }
                        else                  { CMD_RDX = 0xD0; CMD_RDY = 0x90; }
                        continue;
                    }

                    POINT_COLOR = BLUE;
                    LCD_Clear(WHITE);
                    /* 校准成功: 屏幕中央画一个绿色方块表示 OK */
                    LCD_Fill((uint16_t)(lcddev.width  / 2 - 20),
                             (uint16_t)(lcddev.height / 2 - 20),
                             (uint16_t)(lcddev.width  / 2 + 20),
                             (uint16_t)(lcddev.height / 2 + 20), GREEN);
                    printf("[TP] === Calibration OK ===\r\n");
                    HAL_Delay(1000);
                    TP_Save_Calibration();
                    LCD_Clear(WHITE);
                    return;
            }
        }
        HAL_Delay(10);
        outtime++;
        if (outtime > 1000) {     /* 10 秒无操作, 退出, 尝试加载已有校准 */
            TP_Get_Calibration();
            break;
        }
    }
}

/*============================================================================
 *  TP_Scan: 扫描触摸状态并更新 tp_dev.x/y
 *  tp = 0 -> 屏幕坐标;  tp = 1 -> 物理 ADC 坐标
 *============================================================================*/
uint8_t TP_Scan(uint8_t tp)
{
    if (TP_PEN_READ() == 0) {                /* PEN 低 = 按下 */
        if (tp) {
            TP_Read_XY2(&tp_dev.x, &tp_dev.y);
        } else if (TP_Read_XY2(&tp_dev.x, &tp_dev.y)) {
            tp_dev.x = (uint16_t)(tp_dev.xfac * (float)tp_dev.x + (float)tp_dev.xoff);
            tp_dev.y = (uint16_t)(tp_dev.yfac * (float)tp_dev.y + (float)tp_dev.yoff);
        }
        if ((tp_dev.sta & TP_PRES_DOWN) == 0) {
            tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;
            tp_dev.x0  = tp_dev.x;
            tp_dev.y0  = tp_dev.y;
        }
    } else {
        if (tp_dev.sta & TP_PRES_DOWN) {
            tp_dev.sta &= ~(1U << 7);         /* 标记松开 */
        } else {
            tp_dev.x0 = 0;
            tp_dev.y0 = 0;
            tp_dev.x  = 0xFFFFU;
            tp_dev.y  = 0xFFFFU;
        }
    }
    return (uint8_t)(tp_dev.sta & TP_PRES_DOWN);
}

/*============================================================================
 *  TP_Get_TouchPoint: 一次调用, 屏幕坐标 (便利函数)
 *  返回 1 = 当前有触摸, *x *y 为屏幕坐标; 0 = 无触摸
 *============================================================================*/
uint8_t TP_Get_TouchPoint(uint16_t *x, uint16_t *y)
{
    if (TP_Scan(0)) {
        *x = tp_dev.x;
        *y = tp_dev.y;
        return 1;
    }
    *x = 0; *y = 0;
    return 0;
}

/*============================================================================
 *  TP_Init: 初始化 GPIO / 软件 SPI / 加载 (或执行) 校准
 *  返回值: 0 = 已有校准, 1 = 已执行过校准 (或刚完成)
 *============================================================================*/
uint8_t TP_Init(void)
{
    TP_GPIO_Init();
    TP_DwtInit();

    /* 默认命令字 */
    CMD_RDX = 0xD0;
    CMD_RDY = 0x90;

    /* 预读一次, 稳定 XPT2046 */
    (void)TP_Read_XY(&tp_dev.x, &tp_dev.y);

    if (TP_Get_Calibration()) {
        return 0;   /* 已有有效校准 */
    }

    /* 首次上电, 进入校准流程 */
    LCD_Clear(WHITE);
    TP_Calibrate();
    TP_Get_Calibration();     /* 重新加载一遍, 同步 CMD_RDX/RDY */
    return 1;
}
