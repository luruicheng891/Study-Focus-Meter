#ifndef __I2C_H
#define __I2C_H

#include "stm32h7xx.h" 

// 配置I2C引脚 - 请根据实际硬件修改
#define I2C_SCL_GPIO_PORT      GPIOB
#define I2C_SCL_GPIO_PIN       GPIO_PIN_8
#define I2C_SDA_GPIO_PORT      GPIOB
#define I2C_SDA_GPIO_PIN       GPIO_PIN_9

// 延时时间（微秒）- 根据需要的I2C速度调整
// 100kHz: 5us, 400kHz: 1.25us
#define I2C_DELAY_US           5

// 宏定义操作
#define I2C_SCL_HIGH()         HAL_GPIO_WritePin(I2C_SCL_GPIO_PORT, I2C_SCL_GPIO_PIN, GPIO_PIN_SET)
#define I2C_SCL_LOW()          HAL_GPIO_WritePin(I2C_SCL_GPIO_PORT, I2C_SCL_GPIO_PIN, GPIO_PIN_RESET)
#define I2C_SDA_HIGH()         HAL_GPIO_WritePin(I2C_SDA_GPIO_PORT, I2C_SDA_GPIO_PIN, GPIO_PIN_SET)
#define I2C_SDA_LOW()          HAL_GPIO_WritePin(I2C_SDA_GPIO_PORT, I2C_SDA_GPIO_PIN, GPIO_PIN_RESET)
#define I2C_SDA_READ()         HAL_GPIO_ReadPin(I2C_SDA_GPIO_PORT, I2C_SDA_GPIO_PIN)

// 函数声明
void I2C_Sim_Init(void);
void I2C_Start(void);
void I2C_Stop(void);
uint8_t I2C_WriteByte(uint8_t data);
uint8_t I2C_ReadByte(uint8_t ack);
uint8_t I2C_WriteData(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
uint8_t I2C_ReadData(uint8_t dev_addr, uint8_t reg_addr, uint8_t *buffer, uint16_t len);

#endif





