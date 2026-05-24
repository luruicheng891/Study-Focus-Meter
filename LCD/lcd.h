/**
 * @file    lcd.h
 * @brief   ILI9341 LCD driver for STM32H723ZGT6 (HAL + Hardware SPI4)
 * @note    Ported from QDtech STM32F407 SPI driver
 *
 * Pin Assignment (SPI4):
 *   LCD Module        STM32H723
 *   SDI(MOSI)   ->   PE6  (SPI4_MOSI)
 *   SDO(MISO)   ->   PE5  (SPI4_MISO)
 *   SCK         ->   PE2  (SPI4_SCK)
 *   CS          ->   PD0  (GPIO Output)
 *   DC/RS       ->   PD1  (GPIO Output)
 *   RST         ->   PD2  (GPIO Output)
 *   LED         ->   PD3  (GPIO Output)
 */

#ifndef __LCD_H
#define __LCD_H

#include "main.h"
#include <stdint.h>

/* LCD parameters structure */
typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t id;
    uint8_t  dir;
    uint16_t wramcmd;
    uint16_t setxcmd;
    uint16_t setycmd;
} LCD_Dev_t;

extern LCD_Dev_t lcddev;

/* User configuration */
#define USE_HORIZONTAL  0  // 0-0°, 1-90°, 2-180°, 3-270°

/* LCD size */
#define LCD_W 240
#define LCD_H 320

/* Colors */
extern uint16_t POINT_COLOR;
extern uint16_t BACK_COLOR;

/* -------- GPIO Pin Definitions -------- */
#define LCD_CS_PORT     GPIOD
#define LCD_CS_PIN      GPIO_PIN_0

#define LCD_DC_PORT     GPIOD
#define LCD_DC_PIN      GPIO_PIN_1

#define LCD_RST_PORT    GPIOD
#define LCD_RST_PIN     GPIO_PIN_2

#define LCD_LED_PORT    GPIOB
#define LCD_LED_PIN     GPIO_PIN_4

/* -------- GPIO Control Macros -------- */
#define LCD_CS_SET()    HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET)
#define LCD_CS_CLR()    HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET)

#define LCD_DC_SET()    HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_SET)
#define LCD_DC_CLR()    HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_RESET)

#define LCD_RST_SET()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)
#define LCD_RST_CLR()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)

#define LCD_LED_ON()    HAL_GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, GPIO_PIN_SET)
#define LCD_LED_OFF()   HAL_GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, GPIO_PIN_RESET)

/* -------- Color Definitions -------- */
#define WHITE       0xFFFF
#define BLACK       0x0000
#define BLUE        0x001F
#define BRED        0xF81F
#define GRED        0xFFE0
#define GBLUE       0x07FF
#define RED         0xF800
#define MAGENTA     0xF81F
#define GREEN       0x07E0
#define CYAN        0x7FFF
#define YELLOW      0xFFE0
#define BROWN       0xBC40
#define BRRED       0xFC07
#define GRAY        0x8430
#define DARKBLUE    0x01CF
#define LIGHTBLUE   0x7D7C
#define GRAYBLUE    0x5458
#define LIGHTGREEN  0x841F
#define LIGHTGRAY   0xEF5B
#define LGRAY       0xC618
#define LGRAYBLUE   0xA651
#define LBBLUE      0x2B12

/* -------- Function Prototypes -------- */
void LCD_Init(void);
void LCD_Clear(uint16_t Color);
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos);
void LCD_DrawPoint(uint16_t x, uint16_t y);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_SetWindows(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd);
void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue);
void LCD_WR_DATA(uint8_t data);
void LCD_WriteRAM_Prepare(void);
void Lcd_WriteData_16Bit(uint16_t Data);
void LCD_direction(uint8_t direction);
void LCD_Fill(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd, uint16_t color);
void LCD_DrawBuffer(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd, uint16_t *pBuf);
void LCD_WriteDataBuffer(uint16_t *pBuf, uint32_t pixel_count);
void LCD_DMA_Wait(void);
void LCD_FlushRaw(uint8_t *pData, uint32_t len);
#endif /* __LCD_H */
