#include "MPU6050.h"
#include "MPU6050_Reg.h"
#include "I2C.h"  

// 写寄存器
// 返回值: 0=成功, 非0=失败
uint8_t MPU6050_WriteReg(uint8_t RegAddress, uint8_t Data)
{
    return I2C_WriteData(MPU6050_ADDRESS, RegAddress, &Data, 1);
}

// 读寄存器
// 返回值: 0=成功, 非0=失败
uint8_t MPU6050_ReadReg(uint8_t RegAddress, uint8_t *Data)
{
    return I2C_ReadData(MPU6050_ADDRESS, RegAddress, Data, 1);
}

// 初始化MPU6050
// 返回值: 0=成功, 非0=失败
uint8_t MPU6050_Init(void)
{
    uint8_t ret;
    
    // 注意：I2C初始化已经在其他地方调用，这里不再重复初始化
    // I2C_Sim_Init();  // 如果还没有初始化I2C，需要添加这行
    
    // 退出睡眠模式，选择时钟源为X轴陀螺仪PLL
    ret = MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x01);
    if(ret) return 1;
    
    // 设置辅助电源管理
    ret = MPU6050_WriteReg(MPU6050_PWR_MGMT_2, 0x00);
    if(ret) return 2;
    
    // 设置采样率分频器（1kHz / (1+9) = 100Hz）
    ret = MPU6050_WriteReg(MPU6050_SMPLRT_DIV, 0x09);
    if(ret) return 3;
    
    // 设置数字低通滤波器（带宽=21.7Hz，延时=8.4ms）
    ret = MPU6050_WriteReg(MPU6050_CONFIG, 0x06);
    if(ret) return 4;
    
    // 设置陀螺仪量程为±2000°/s
    ret = MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x18);
    if(ret) return 5;
    
    // 设置加速度计量程为±16g
    ret = MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x18);
    if(ret) return 6;
    
    return 0;
}

// 获取设备ID
uint8_t MPU6050_GetID(void)
{
    uint8_t id = 0xFF;
    
    MPU6050_ReadReg(MPU6050_WHO_AM_I, &id);
    return id;
}

// 获取所有数据（逐字节读取）
uint8_t MPU6050_GetData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                        int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    uint8_t DataH, DataL;
    uint8_t ret;
    
    // 读取加速度计X轴
    ret = MPU6050_ReadReg(MPU6050_ACCEL_XOUT_H, &DataH);
    if(ret) return 1;
    ret = MPU6050_ReadReg(MPU6050_ACCEL_XOUT_L, &DataL);
    if(ret) return 2;
    *AccX = (DataH << 8) | DataL;
    
    // 读取加速度计Y轴
    ret = MPU6050_ReadReg(MPU6050_ACCEL_YOUT_H, &DataH);
    if(ret) return 3;
    ret = MPU6050_ReadReg(MPU6050_ACCEL_YOUT_L, &DataL);
    if(ret) return 4;
    *AccY = (DataH << 8) | DataL;
    
    // 读取加速度计Z轴
    ret = MPU6050_ReadReg(MPU6050_ACCEL_ZOUT_H, &DataH);
    if(ret) return 5;
    ret = MPU6050_ReadReg(MPU6050_ACCEL_ZOUT_L, &DataL);
    if(ret) return 6;
    *AccZ = (DataH << 8) | DataL;
    
    // 读取陀螺仪X轴
    ret = MPU6050_ReadReg(MPU6050_GYRO_XOUT_H, &DataH);
    if(ret) return 7;
    ret = MPU6050_ReadReg(MPU6050_GYRO_XOUT_L, &DataL);
    if(ret) return 8;
    *GyroX = (DataH << 8) | DataL;
    
    // 读取陀螺仪Y轴
    ret = MPU6050_ReadReg(MPU6050_GYRO_YOUT_H, &DataH);
    if(ret) return 9;
    ret = MPU6050_ReadReg(MPU6050_GYRO_YOUT_L, &DataL);
    if(ret) return 10;
    *GyroY = (DataH << 8) | DataL;
    
    // 读取陀螺仪Z轴
    ret = MPU6050_ReadReg(MPU6050_GYRO_ZOUT_H, &DataH);
    if(ret) return 11;
    ret = MPU6050_ReadReg(MPU6050_GYRO_ZOUT_L, &DataL);
    if(ret) return 12;
    *GyroZ = (DataH << 8) | DataL;
    
    return 0;
}

// 批量读取所有数据（优化版本，减少I2C通信次数）
uint8_t MPU6050_GetDataFast(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                            int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    uint8_t buffer[14];  // 从0x3B到0x48共14个字节
    uint8_t ret;
    
    // 从ACCEL_XOUT_H开始连续读取14个字节
    ret = I2C_ReadData(MPU6050_ADDRESS, MPU6050_ACCEL_XOUT_H, buffer, 14);
    if(ret) return ret;
    
    // 解析数据
    *AccX = (buffer[0] << 8) | buffer[1];
    *AccY = (buffer[2] << 8) | buffer[3];
    *AccZ = (buffer[4] << 8) | buffer[5];
    *GyroX = (buffer[8] << 8) | buffer[9];
    *GyroY = (buffer[10] << 8) | buffer[11];
    *GyroZ = (buffer[12] << 8) | buffer[13];
    
    return 0;
}

