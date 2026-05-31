// BH1750.h
#ifndef __BH1750_H
#define __BH1750_H

#include "main.h"

// BH1750 I2C 地址 (7位)
#define BH1750_ADDR_L          0x23  // ADDR引脚接低电平 "0100011"
#define BH1750_ADDR_H          0x5C  // ADDR引脚接高电平 "1011100"

// BH1750 指令集
#define BH1750_CMD_POWER_DOWN          0x00  // 断电
#define BH1750_CMD_POWER_ON            0x01  // 上电
#define BH1750_CMD_RESET               0x07  // 重置数据寄存器
#define BH1750_CMD_CONT_H_RES_MODE     0x10  // 连续高分辨率模式 1lx 120ms
#define BH1750_CMD_CONT_H_RES_MODE2    0x11  // 连续高分辨率模式2 0.5lx 120ms
#define BH1750_CMD_CONT_L_RES_MODE     0x13  // 连续低分辨率模式 4lx 16ms
#define BH1750_CMD_ONE_H_RES_MODE      0x20  // 单次高分辨率模式 1lx 120ms
#define BH1750_CMD_ONE_H_RES_MODE2     0x21  // 单次高分辨率模式2 0.5lx 120ms
#define BH1750_CMD_ONE_L_RES_MODE      0x23  // 单次低分辨率模式 4lx 16ms

// 测量时间寄存器设置 (用于灵敏度调节)
#define BH1750_CMD_CHANGE_MT_HIGH      0x40  // 设置MT[7,6,5] 高3位
#define BH1750_CMD_CHANGE_MT_LOW       0x60  // 设置MT[4,3,2,1,0] 低5位

// 默认MTreg值 (0100_0101 = 69)
#define BH1750_MTREG_DEFAULT           69

// 测量时间定义
#define BH1750_MEASURE_TIME_H_MODE     180   // 高分辨率模式测量时间(ms)
#define BH1750_MEASURE_TIME_H_MODE2    180   // 高分辨率模式2测量时间(ms)
#define BH1750_MEASURE_TIME_L_MODE     24    // 低分辨率模式测量时间(ms)

// 返回值定义
#define BH1750_OK                      0
#define BH1750_ERROR_I2C_WRITE         1
#define BH1750_ERROR_I2C_READ          2
#define BH1750_ERROR_TIMEOUT           3
#define BH1750_ERROR_INVALID_MODE      4

// 结构体定义
typedef struct {
    uint8_t i2c_addr;      // I2C设备地址
    uint8_t mtreg;         // 当前测量时间寄存器值
    float   sensitivity;   // 当前灵敏度比例
} BH1750_Handle_t;

// 初始化函数
uint8_t BH1750_Init(BH1750_Handle_t *dev, uint8_t addr_pin_state);

// 断电/上电
uint8_t BH1750_PowerOn(BH1750_Handle_t *dev);
uint8_t BH1750_PowerDown(BH1750_Handle_t *dev);

// 重置传感器
uint8_t BH1750_Reset(BH1750_Handle_t *dev);

// 开始测量
uint8_t BH1750_StartMeasurement(BH1750_Handle_t *dev, uint8_t mode);

// 读取原始数据
uint8_t BH1750_ReadRawData(BH1750_Handle_t *dev, uint16_t *raw_data);

// 读取光照强度 (计算后的值, 单位: lx)
uint8_t BH1750_ReadLux(BH1750_Handle_t *dev, float *lux);

// 单次测量 (自动流程: 开始测量 -> 等待 -> 读取结果)
uint8_t BH1750_MeasureOnce(BH1750_Handle_t *dev, uint8_t mode, float *lux);

// 设置测量时间/灵敏度
// mt_value: MTreg值 (范围: 31-254, 默认69)
uint8_t BH1750_SetMeasurementTime(BH1750_Handle_t *dev, uint8_t mt_value);
uint8_t BH1750_GetMeasurementTime(BH1750_Handle_t *dev, uint8_t *mt_value);

// 获取当前灵敏度比例
float BH1750_GetSensitivity(BH1750_Handle_t *dev);

// 发送指令
uint8_t BH1750_SendCommand(BH1750_Handle_t *dev, uint8_t cmd);

#endif