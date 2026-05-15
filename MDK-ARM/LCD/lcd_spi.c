/**
 * @file    lcd_spi.c
 * @brief   SPI4 hardware initialization for ILI9341 LCD on STM32H723ZGT6
 *
 * SPI4 Pin Mapping:
 *   SCK  -> PE2  (AF5)
 *   MISO -> PE5  (AF5)
 *   MOSI -> PE6  (AF5)
 *
 * STM32H723 SPI4 is on APB2 bus.
 * At default 275MHz system clock, APB2 = 137.5MHz.
 * Prescaler=4 gives SPI clock ~34MHz (ILI9341 max is ~40MHz for write).
 * Adjust prescaler if your clock tree differs.
 */

#include "lcd_spi.h"

SPI_HandleTypeDef hlcd_spi;

/**
 * @brief  Initialize SPI4 peripheral and GPIO
 */
void LCD_SPI_Init(void)
{
    /* Enable clocks */
    __HAL_RCC_SPI4_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* Configure PE2 (SCK), PE5 (MISO), PE6 (MOSI) as SPI4 Alternate Function */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_5 | GPIO_PIN_6;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI4;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* SPI4 configuration */
    hlcd_spi.Instance               = SPI4;
    hlcd_spi.Init.Mode              = SPI_MODE_MASTER;
    hlcd_spi.Init.Direction         = SPI_DIRECTION_2LINES;
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
        /* Initialization Error - stay here for debug */
        while (1) {}
    }
}
