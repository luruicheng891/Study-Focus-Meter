/**
 * @file    lcd_spi.h
 * @brief   SPI1 hardware initialization for ILI9341 LCD on STM32H723ZGT6
 *          with DMA TX support
 *
 * SPI1 Pin Mapping (no conflict with DCMI):
 *   SCK  -> PB3  (AF5)
 *   MOSI -> PB5  (AF5)
 *
 * DMA: DMA1_Stream0 for SPI1_TX (via DMAMUX)
 */

#ifndef __LCD_SPI_H
#define __LCD_SPI_H

#include "main.h"

extern SPI_HandleTypeDef hlcd_spi;
extern DMA_HandleTypeDef hdma_spi1_tx;

/**
 * @brief  Initialize SPI1 + DMA for LCD communication
 */
void LCD_SPI_Init(void);

#endif /* __LCD_SPI_H */
