/**
 * @file    lcd_spi.c
 * @brief   SPI1 hardware initialization for ILI9341 LCD on STM32H723ZGT6
 *
 * SPI1 Pin Mapping (no conflict with DCMI):
 *   SCK  -> PB3  (AF5)
 *   MOSI -> PB5  (AF5)
 *   (MISO not needed - LCD is write-only)
 *
 * STM32H723 SPI1 is on APB2 bus, clock = 137.5MHz.
 * Prescaler=4 gives SPI clock ~34MHz (ILI9341 max write speed ~40MHz).
 */

#include "lcd_spi.h"

SPI_HandleTypeDef hlcd_spi;

/**
 * @brief  Initialize SPI1 peripheral and GPIO for LCD
 */
void LCD_SPI_Init(void)
{
    /* Enable clocks */
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Configure PB3 (SCK), PB5 (MOSI) as SPI1 Alternate Function */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin       = GPIO_PIN_3 | GPIO_PIN_5;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SPI1 configuration - transmit only */
    hlcd_spi.Instance               = SPI1;
    hlcd_spi.Init.Mode              = SPI_MODE_MASTER;
    hlcd_spi.Init.Direction         = SPI_DIRECTION_2LINES_TXONLY;
    hlcd_spi.Init.DataSize          = SPI_DATASIZE_8BIT;
    hlcd_spi.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hlcd_spi.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hlcd_spi.Init.NSS               = SPI_NSS_SOFT;
    hlcd_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    hlcd_spi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hlcd_spi.Init.TIMode            = SPI_TIMODE_DISABLE;
    hlcd_spi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hlcd_spi.Init.CRCPolynomial     = 7;
    hlcd_spi.Init.CRCLength         = SPI_CRC_LENGTH_8BIT;
    hlcd_spi.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
    hlcd_spi.Init.NSSPolarity       = SPI_NSS_POLARITY_LOW;
    hlcd_spi.Init.FifoThreshold     = SPI_FIFO_THRESHOLD_01DATA;
    hlcd_spi.Init.MasterSSIdleness        = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hlcd_spi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hlcd_spi.Init.MasterReceiverAutoSusp  = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hlcd_spi.Init.IOSwap            = SPI_IO_SWAP_DISABLE;
    hlcd_spi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;

    if (HAL_SPI_Init(&hlcd_spi) != HAL_OK)
    {
        while (1) {}
    }
}
