#ifndef __MPU6050_H
#define __MPU6050_H


#include <stdint.h>  // 添加这一行，定义uint8_t、int16_t等类型

// MPU6050 7位地址（0x68）
#define MPU6050_ADDRESS        0x68

// 函数声明
uint8_t MPU6050_Init(void);  // 修改：添加返回值类型
uint8_t MPU6050_WriteReg(uint8_t RegAddress, uint8_t Data);
uint8_t MPU6050_ReadReg(uint8_t RegAddress, uint8_t *Data);
uint8_t MPU6050_GetID(void);
uint8_t MPU6050_GetData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                        int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);
uint8_t MPU6050_GetDataFast(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                            int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);

#endif
