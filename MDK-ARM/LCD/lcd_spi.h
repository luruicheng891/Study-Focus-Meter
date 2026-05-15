/**
 * @file    lcd_spi.h
 * @brief   SPI4 hardware initialization for ILI9341 LCD on STM32H723ZGT6
 *
 * SPI4 Pin Mapping:
 *   SCK  -> PE2  (AF5)
 *   MISO -> PE5  (AF5)
 *   MOSI -> PE6  (AF5)
 *
 * SPI Configuration:
 *   - Master mode
 *   - 8-bit data frame
 *   - CPOL=Low, CPHA=1Edge (Mode 0)
 *   - Software NSS
 *   - MSB first
 *   - Prescaler: 4 (adjust for your clock tree)
 */

#ifndef __LCD_SPI_H
#define __LCD_SPI_H

#include "main.h"

extern SPI_HandleTypeDef hlcd_spi;

/**
 * @brief  Initialize SPI4 for LCD communication
 */
void LCD_SPI_Init(void);

#endif /* __LCD_SPI_H */
