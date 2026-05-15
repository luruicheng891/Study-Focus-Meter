/**
 * @file    lcd.c
 * @brief   ILI9341 LCD driver for STM32H723ZGT6 (HAL + Hardware SPI4)
 * @note    Ported from QDtech STM32F407 SPI driver
 *
 * SPI4 Pins:
 *   SCK  -> PE2
 *   MISO -> PE5
 *   MOSI -> PE6
 *
 * Control Pins (GPIO Output):
 *   CS   -> PD0
 *   DC   -> PD1
 *   RST  -> PD2
 *   LED  -> PD3
 */

#include "lcd.h"
#include "lcd_spi.h"

/* SPI handle - defined in lcd_spi.c */
extern SPI_HandleTypeDef hlcd_spi;

/* LCD device parameters */
LCD_Dev_t lcddev;

/* Default colors */
uint16_t POINT_COLOR = 0x0000;  /* Black */
uint16_t BACK_COLOR  = 0xFFFF;  /* White */

/* ======================== Low-level SPI Transmit ======================== */

/**
 * @brief  Send one byte via hardware SPI (blocking)
 * @param  byte: data to send
 */
static void LCD_SPI_SendByte(uint8_t byte)
{
    HAL_SPI_Transmit(&hlcd_spi, &byte, 1, HAL_MAX_DELAY);
}

/**
 * @brief  Send buffer via hardware SPI (blocking)
 * @param  buf: pointer to data
 * @param  len: number of bytes
 */
static void LCD_SPI_SendBytes(uint8_t *buf, uint16_t len)
{
    HAL_SPI_Transmit(&hlcd_spi, buf, len, HAL_MAX_DELAY);
}

/* ======================== LCD Command / Data ======================== */

/**
 * @brief  Write an 8-bit command to LCD
 */
static void LCD_WR_REG(uint8_t reg)
{
    LCD_CS_CLR();
    LCD_DC_CLR();  /* Command mode */
    LCD_SPI_SendByte(reg);
    LCD_CS_SET();
}

/**
 * @brief  Write an 8-bit data to LCD
 */
void LCD_WR_DATA(uint8_t data)
{
    LCD_CS_CLR();
    LCD_DC_SET();  /* Data mode */
    LCD_SPI_SendByte(data);
    LCD_CS_SET();
}

/**
 * @brief  Write register: command + data
 */
void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue)
{
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA((uint8_t)LCD_RegValue);
}

/**
 * @brief  Prepare to write GRAM
 */
void LCD_WriteRAM_Prepare(void)
{
    LCD_WR_REG(lcddev.wramcmd);
}

/**
 * @brief  Write 16-bit pixel data
 */
void Lcd_WriteData_16Bit(uint16_t Data)
{
    uint8_t buf[2];
    buf[0] = Data >> 8;
    buf[1] = Data & 0xFF;

    LCD_CS_CLR();
    LCD_DC_SET();
    LCD_SPI_SendBytes(buf, 2);
    LCD_CS_SET();
}

/* ======================== LCD Drawing Functions ======================== */

/**
 * @brief  Draw a single pixel
 */
void LCD_DrawPoint(uint16_t x, uint16_t y)
{
    LCD_SetCursor(x, y);
    Lcd_WriteData_16Bit(POINT_COLOR);
}

/**
 * @brief  Clear the full screen with a color
 */
void LCD_Clear(uint16_t Color)
{
    uint32_t total = (uint32_t)lcddev.width * lcddev.height;
    uint8_t buf[2];
    buf[0] = Color >> 8;
    buf[1] = Color & 0xFF;

    LCD_SetWindows(0, 0, lcddev.width - 1, lcddev.height - 1);
    LCD_CS_CLR();
    LCD_DC_SET();
    for (uint32_t i = 0; i < total; i++)
    {
        LCD_SPI_SendBytes(buf, 2);
    }
    LCD_CS_SET();
}

/**
 * @brief  Fill a rectangular area with a color
 */
void LCD_Fill(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd, uint16_t color)
{
    uint32_t total = (uint32_t)(xEnd - xStar + 1) * (yEnd - yStar + 1);
    uint8_t buf[2];
    buf[0] = color >> 8;
    buf[1] = color & 0xFF;

    LCD_SetWindows(xStar, yStar, xEnd, yEnd);
    LCD_CS_CLR();
    LCD_DC_SET();
    for (uint32_t i = 0; i < total; i++)
    {
        LCD_SPI_SendBytes(buf, 2);
    }
    LCD_CS_SET();
}

/**
 * @brief  Write a RGB565 pixel buffer to a rectangular area
 * @note   Pixel data is in native uint16_t format, sent as big-endian (MSB first) to LCD
 */
void LCD_DrawBuffer(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd, uint16_t *pBuf)
{
    uint32_t total = (uint32_t)(xEnd - xStar + 1) * (yEnd - yStar + 1);

    LCD_SetWindows(xStar, yStar, xEnd, yEnd);
    LCD_CS_CLR();
    LCD_DC_SET();

    for (uint32_t i = 0; i < total; i++)
    {
        uint8_t buf[2];
        buf[0] = pBuf[i] >> 8;
        buf[1] = pBuf[i] & 0xFF;
        LCD_SPI_SendBytes(buf, 2);
    }
    LCD_CS_SET();
}

/**
 * @brief  Draw a line (Bresenham)
 */
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int16_t dx = (int16_t)x2 - (int16_t)x1;
    int16_t dy = (int16_t)y2 - (int16_t)y1;
    int16_t sx = (dx >= 0) ? 1 : -1;
    int16_t sy = (dy >= 0) ? 1 : -1;
    dx = (dx >= 0) ? dx : -dx;
    dy = (dy >= 0) ? dy : -dy;

    if (dx >= dy)
    {
        int16_t err = dx / 2;
        for (int16_t i = 0; i <= dx; i++)
        {
            LCD_DrawPoint(x1, y1);
            err -= dy;
            if (err < 0)
            {
                err += dx;
                y1 += sy;
            }
            x1 += sx;
        }
    }
    else
    {
        int16_t err = dy / 2;
        for (int16_t i = 0; i <= dy; i++)
        {
            LCD_DrawPoint(x1, y1);
            err -= dx;
            if (err < 0)
            {
                err += dy;
                x1 += sx;
            }
            y1 += sy;
        }
    }
}

/**
 * @brief  Draw a rectangle (outline)
 */
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    LCD_DrawLine(x1, y1, x2, y1);
    LCD_DrawLine(x1, y1, x1, y2);
    LCD_DrawLine(x1, y2, x2, y2);
    LCD_DrawLine(x2, y1, x2, y2);
}

/* ======================== LCD Window / Cursor ======================== */

/**
 * @brief  Set display window
 */
void LCD_SetWindows(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd)
{
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(xStar >> 8);
    LCD_WR_DATA(0x00FF & xStar);
    LCD_WR_DATA(xEnd >> 8);
    LCD_WR_DATA(0x00FF & xEnd);

    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(yStar >> 8);
    LCD_WR_DATA(0x00FF & yStar);
    LCD_WR_DATA(yEnd >> 8);
    LCD_WR_DATA(0x00FF & yEnd);

    LCD_WriteRAM_Prepare();
}

/**
 * @brief  Set cursor position
 */
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos)
{
    LCD_SetWindows(Xpos, Ypos, Xpos, Ypos);
}

/* ======================== LCD Direction ======================== */

/**
 * @brief  Set display direction
 * @param  direction: 0=0°, 1=90°, 2=180°, 3=270°
 */
void LCD_direction(uint8_t direction)
{
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;
    lcddev.wramcmd = 0x2C;

    switch (direction)
    {
    case 0:
        lcddev.width  = LCD_W;
        lcddev.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 6) | (0 << 7));
        break;
    case 1:
        lcddev.width  = LCD_H;
        lcddev.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));
        break;
    case 2:
        lcddev.width  = LCD_W;
        lcddev.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 6) | (1 << 7));
        break;
    case 3:
        lcddev.width  = LCD_H;
        lcddev.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 7) | (1 << 5));
        break;
    default:
        break;
    }
}

/* ======================== LCD Hardware Reset ======================== */

static void LCD_RESET(void)
{
    LCD_RST_CLR();
    HAL_Delay(100);
    LCD_RST_SET();
    HAL_Delay(50);
}

/* ======================== LCD GPIO Init ======================== */

/**
 * @brief  Initialize control GPIOs (CS, DC, RST, LED on PD0-PD3)
 */
static void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIOD clock */
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* Configure PD0 (CS), PD1 (DC), PD2 (RST), PD3 (LED) */
    GPIO_InitStruct.Pin   = LCD_CS_PIN | LCD_DC_PIN | LCD_RST_PIN | LCD_LED_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* Default states */
    LCD_CS_SET();
    LCD_DC_SET();
    LCD_RST_SET();
    LCD_LED_OFF();
}

/* ======================== LCD Initialization (ILI9341) ======================== */

void LCD_Init(void)
{
    /* Initialize SPI4 peripheral */
    LCD_SPI_Init();

    /* Initialize control GPIOs */
    LCD_GPIO_Init();

    /* Hardware reset */
    LCD_RESET();

    /* ILI9341 initialization sequence */
    LCD_WR_REG(0xCF);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0xC9);
    LCD_WR_DATA(0x30);

    LCD_WR_REG(0xED);
    LCD_WR_DATA(0x64);
    LCD_WR_DATA(0x03);
    LCD_WR_DATA(0x12);
    LCD_WR_DATA(0x81);

    LCD_WR_REG(0xE8);
    LCD_WR_DATA(0x85);
    LCD_WR_DATA(0x10);
    LCD_WR_DATA(0x7A);

    LCD_WR_REG(0xCB);
    LCD_WR_DATA(0x39);
    LCD_WR_DATA(0x2C);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x34);
    LCD_WR_DATA(0x02);

    LCD_WR_REG(0xF7);
    LCD_WR_DATA(0x20);

    LCD_WR_REG(0xEA);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0xC0);    /* Power control */
    LCD_WR_DATA(0x1B);

    LCD_WR_REG(0xC1);    /* Power control */
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0xC5);    /* VCM control */
    LCD_WR_DATA(0x30);
    LCD_WR_DATA(0x30);

    LCD_WR_REG(0xC7);    /* VCM control2 */
    LCD_WR_DATA(0xB7);

    LCD_WR_REG(0x36);    /* Memory Access Control */
    LCD_WR_DATA(0x08);

    LCD_WR_REG(0x3A);    /* Pixel Format */
    LCD_WR_DATA(0x55);   /* 16-bit/pixel */

    LCD_WR_REG(0xB1);    /* Frame Rate */
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x1A);

    LCD_WR_REG(0xB6);    /* Display Function Control */
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0xA2);

    LCD_WR_REG(0xF2);    /* 3Gamma Function Disable */
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0x26);    /* Gamma curve selected */
    LCD_WR_DATA(0x01);

    LCD_WR_REG(0xE0);    /* Positive Gamma */
    LCD_WR_DATA(0x0F);
    LCD_WR_DATA(0x2A);
    LCD_WR_DATA(0x28);
    LCD_WR_DATA(0x08);
    LCD_WR_DATA(0x0E);
    LCD_WR_DATA(0x08);
    LCD_WR_DATA(0x54);
    LCD_WR_DATA(0xA9);
    LCD_WR_DATA(0x43);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x0F);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0xE1);    /* Negative Gamma */
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x15);
    LCD_WR_DATA(0x17);
    LCD_WR_DATA(0x07);
    LCD_WR_DATA(0x11);
    LCD_WR_DATA(0x06);
    LCD_WR_DATA(0x2B);
    LCD_WR_DATA(0x56);
    LCD_WR_DATA(0x3C);
    LCD_WR_DATA(0x05);
    LCD_WR_DATA(0x10);
    LCD_WR_DATA(0x0F);
    LCD_WR_DATA(0x3F);
    LCD_WR_DATA(0x3F);
    LCD_WR_DATA(0x0F);

    LCD_WR_REG(0x2B);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x01);
    LCD_WR_DATA(0x3F);

    LCD_WR_REG(0x2A);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0xEF);

    LCD_WR_REG(0x11);    /* Exit Sleep */
    HAL_Delay(120);

    LCD_WR_REG(0x29);    /* Display ON */

    LCD_direction(USE_HORIZONTAL);
    LCD_LED_ON();
    LCD_Clear(WHITE);
}
