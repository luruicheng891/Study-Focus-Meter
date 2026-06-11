/**
  ******************************************************************************
  * @file    touch.h
  * @brief   XPT2046 resistive touch controller driver for STM32H723
  *          (software SPI, calibration data stored in internal FLASH)
  ******************************************************************************
  * @attention
  *
  * 移植说明 (Porting notes):
  *   1) 软件 SPI GPIO 引脚 — 在下面 "软件 SPI GPIO 配置" 区域修改
  *      - 默认使用 PC0/PC1/PC2/PC3 + PA15 (PEN). 请按实际原理图修改.
  *   2) 内部 FLASH 校准存储 — 在下面 "FLASH 校准配置" 区域修改
  *      - 默认放在 Bank2 Sector7 末尾 (0x080FFF00, 远离代码区, 64KB 扇区)
  *      - 如果工程是单 Bank 模式, 改为 FLASH_BANK_1 / 0x080E0000
  *   3) SPI 时序延时 — 修改 TP_SPI_DELAY_US (单位 us). 1us 大约对应 500kHz SCK
  *   4) LCD 依赖 — TP_Calibrate() 使用 LCD_Clear / LCD_Fill / LCD_DrawPoint /
 *      LCD_DrawLine. 这些函数假定来自 lcd.h (无文本显示, 改用 UART 提示)
  *
  * 软件 SPI 时序 (XPT2046):
  *   - CPOL = 0, CPHA = 0  (SCK 空闲低, 上升沿采样)
  *   - MSB 先发送
  *   - 最大 SCK ~ 2 MHz, 建议软件模拟 100 kHz ~ 1 MHz
  ******************************************************************************
  */
#ifndef __TOUCH_H__
#define __TOUCH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 *                          软件 SPI GPIO 配置 (移植点 1)
 *  XPT2046 接线参考 (沿用原 F407 默认, 请按你板子实际核对):
 *      T_PEN  -> PB1   (输入, 内部上拉, 按下为低)
 *      T_MISO -> PB2   (输入, 浮空)
 *      T_MOSI -> PF11  (推挽输出)
 *      T_SCK  -> PB0   (推挽输出)
 *      T_CS   -> PC5   (推挽输出)
 *  注意: 触摸模块 T_IRQ 一般悬空, 这里使用 PEN 引脚做轮询检测.
 *  如果你板子实际接线不同, 直接改下面 5 个 PORT/PIN 宏即可.
 *============================================================================*/
#define TP_GPIO_CLK_ENABLE()  do { __HAL_RCC_GPIOB_CLK_ENABLE(); \
                                   __HAL_RCC_GPIOC_CLK_ENABLE(); \
                                   __HAL_RCC_GPIOF_CLK_ENABLE(); } while(0)

/* PEN (T_IRQ) */
#define TP_PEN_PORT           GPIOB
#define TP_PEN_PIN            GPIO_PIN_1
#define TP_PEN_READ()         HAL_GPIO_ReadPin(TP_PEN_PORT, TP_PEN_PIN)

/* MISO (T_DO) */
#define TP_MISO_PORT          GPIOB
#define TP_MISO_PIN           GPIO_PIN_2
#define TP_MISO_READ()        HAL_GPIO_ReadPin(TP_MISO_PORT, TP_MISO_PIN)

/* MOSI (T_DIN) */
#define TP_MOSI_PORT          GPIOF
#define TP_MOSI_PIN           GPIO_PIN_11
#define TP_MOSI_HIGH()        HAL_GPIO_WritePin(TP_MOSI_PORT, TP_MOSI_PIN, GPIO_PIN_SET)
#define TP_MOSI_LOW()         HAL_GPIO_WritePin(TP_MOSI_PORT, TP_MOSI_PIN, GPIO_PIN_RESET)

/* SCK (T_CLK) */
#define TP_SCK_PORT           GPIOB
#define TP_SCK_PIN            GPIO_PIN_0
#define TP_SCK_HIGH()         HAL_GPIO_WritePin(TP_SCK_PORT, TP_SCK_PIN, GPIO_PIN_SET)
#define TP_SCK_LOW()          HAL_GPIO_WritePin(TP_SCK_PORT, TP_SCK_PIN, GPIO_PIN_RESET)

/* CS (T_CS) */
#define TP_CS_PORT            GPIOC
#define TP_CS_PIN             GPIO_PIN_5
#define TP_CS_HIGH()          HAL_GPIO_WritePin(TP_CS_PORT, TP_CS_PIN, GPIO_PIN_SET)
#define TP_CS_LOW()           HAL_GPIO_WritePin(TP_CS_PORT, TP_CS_PIN, GPIO_PIN_RESET)

/*============================================================================
 *                              SPI 时序 (移植点 3)
 *  每个 SCK 边沿之间的微秒延时.  1us 对应 ~500kHz SCK.
 *  0 = 不加延时, 仅靠 GPIO 翻转时间, 适合速度优先; 推荐 1~2 适配 100k~500kHz.
 *============================================================================*/
#define TP_SPI_DELAY_US       1U

/*============================================================================
 *                        FLASH 校准配置 (移植点 2)
 *  STM32H723 内部 FLASH (1MB):
 *    - 单 Bank 模式 (默认, 当 DUAL_BANK 未定义): 8 扇区 x 128KB
 *        Sector0=0x08000000, ..., Sector7=0x080E0000
 *    - 双 Bank 模式 (DUAL_BANK 已定义):  Bank1: 0x08000000~0x0807FFFF
 *                                         Bank2: 0x08080000~0x080FFFFF
 *        每 Bank 8 扇区 x 64KB
 *
 *  默认配置: 单 Bank 模式, 把校准数据放在 Sector7 末尾 (0x080EFF00).
 *            如果你的工程 option byte 设为 Dual Bank, 把下面宏改为 1.
 *            注意: 地址必须 32 字节对齐.
 *============================================================================*/
#define TP_FLASH_USE_DUAL_BANK    0     /* 1 = 双 Bank, 0 = 单 Bank */
#if TP_FLASH_USE_DUAL_BANK
  #define TP_FLASH_CALIB_BANK     FLASH_BANK_2
  #define TP_FLASH_CALIB_SECTOR   FLASH_SECTOR_7
  #define TP_FLASH_CALIB_ADDR     (0x080F0000UL + 0xFF00UL)   /* 0x080FFF00 */
#else
  #define TP_FLASH_CALIB_BANK     FLASH_BANK_1
  #define TP_FLASH_CALIB_SECTOR   FLASH_SECTOR_7
  #define TP_FLASH_CALIB_ADDR     (0x080E0000UL + 0xFF00UL)   /* 0x080EFF00 */
#endif
#define TP_FLASH_VOLTAGE_RANGE     FLASH_VOLTAGE_RANGE_3   /* 32-bit 并行 */

/*============================================================================
 *                              类型与状态
 *============================================================================*/
#define TP_PRES_DOWN       0x80U   /* 触笔按下 */
#define TP_CATH_PRES       0x40U   /* 有按键按下 */

/* 触摸设备全局结构 */
typedef struct {
    uint8_t  (*init)(void);   /* 初始化 */
    uint8_t  (*scan)(uint8_t);/* 扫描. 0=屏幕坐标, 1=物理坐标 */
    void     (*adjust)(void); /* 校准 */
    uint16_t x0;              /* 首次按下坐标 */
    uint16_t y0;
    uint16_t x;               /* 当前坐标 */
    uint16_t y;
    uint8_t  sta;             /* 状态: b7=按下/松开, b6=本次有按下 */
    float    xfac;            /* 校准系数 */
    float    yfac;
    int16_t  xoff;
    int16_t  yoff;
    uint8_t  touchtype;       /* 0:X+/Y+, 1:X+/Y- 等 */
} _m_tp_dev;

/* 内部 FLASH 校准数据结构 (32 字节 = 1 个 FLASHWORD) */
typedef struct {
    uint32_t magic;          /* 魔数, 用于识别有效校准 */
    float    xfac;
    float    yfac;
    int16_t  xoff;
    int16_t  yoff;
    uint8_t  touchtype;
    uint8_t  reserved[11];   /* 填充到 32 字节 */
} Calib_t;

#define TP_CALIB_MAGIC     0xAA55AA55UL

/*============================================================================
 *                              函数原型
 *============================================================================*/
uint8_t  TP_Init(void);                                  /* 初始化 */
uint8_t  TP_Scan(uint8_t tp);                            /* 扫描 */
void     TP_Calibrate(void);                             /* 4 点校准 */
uint8_t  TP_Get_Calibration(void);                       /* 从 FLASH 读取校准 */
uint8_t  TP_Save_Calibration(void);                      /* 写校准到 FLASH */
uint16_t TP_Read_AD(uint8_t cmd);                        /* 读一次 ADC */
uint8_t  TP_Read_XY(uint16_t *x, uint16_t *y);           /* 读一次 X/Y */
uint8_t  TP_Read_XY2(uint16_t *x, uint16_t *y);          /* 读两次 (抗抖) */
uint8_t  TP_Get_TouchPoint(uint16_t *x, uint16_t *y);    /* 获取屏幕坐标触摸点 */
void     TP_Drow_Touch_Point(uint16_t x, uint16_t y, uint16_t color);
void     TP_Draw_Big_Point(uint16_t x, uint16_t y, uint16_t color);
void     TP_Adj_Info_Show(uint16_t x0, uint16_t y0,
                          uint16_t x1, uint16_t y1,
                          uint16_t x2, uint16_t y2,
                          uint16_t x3, uint16_t y3,
                          uint16_t fac);

/* 兼容旧名 */
#define TP_Adjust         TP_Calibrate
#define TP_Get_Adjdata    TP_Get_Calibration
#define TP_Save_Adjdata   TP_Save_Calibration

extern _m_tp_dev tp_dev;          /* 触摸设备实例 (在 touch.c 中定义) */
extern uint8_t   CMD_RDX;         /* 读 X 命令字 */
extern uint8_t   CMD_RDY;         /* 读 Y 命令字 */

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_H__ */
