/**
 * @file    lcd_spi.h
 * @brief   SPI1 hardware initialization for ILI9341 LCD on STM32H723ZGT6
 *
 * SPI1 Pin Mapping (no conflict with DCMI):
 *   SCK  -> PB3  (AF5)
 *   MOSI -> PB5  (AF5)
 */

#ifndef __LCD_SPI_H
#define __LCD_SPI_H

#include "main.h"

extern SPI_HandleTypeDef hlcd_spi;

/**
 * @brief  Initialize SPI1 for LCD communication
 */
void LCD_SPI_Init(void);

#endif /* __LCD_SPI_H */
