// BH1750.c
#include "BH1750.h"
#include "I2C.h"

// 计算当前灵敏度
static void BH1750_UpdateSensitivity(BH1750_Handle_t *dev)
{
    // 灵敏度 = 默认MTreg值(69) / 当前MTreg值
    // 实际灵敏度系数 = 69.0 / dev->mtreg
    if (dev->mtreg == 0) {
        dev->mtreg = BH1750_MTREG_DEFAULT;
    }
    dev->sensitivity = (float)BH1750_MTREG_DEFAULT / (float)dev->mtreg;
}

// 初始化BH1750
uint8_t BH1750_Init(BH1750_Handle_t *dev, uint8_t addr_pin_state)
{
    if (dev == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    // 设置I2C地址
    if (addr_pin_state == 0) {
        dev->i2c_addr = BH1750_ADDR_L;
    } else {
        dev->i2c_addr = BH1750_ADDR_H;
    }
    
    // 设置默认MTreg值
    dev->mtreg = BH1750_MTREG_DEFAULT;
    BH1750_UpdateSensitivity(dev);
    
    // 执行上电
    if (BH1750_PowerOn(dev) != BH1750_OK) {
        return BH1750_ERROR_I2C_WRITE;
    }
    
    // 重置传感器
    if (BH1750_Reset(dev) != BH1750_OK) {
        return BH1750_ERROR_I2C_WRITE;
    }
    
    return BH1750_OK;
}

// 发送指令
uint8_t BH1750_SendCommand(BH1750_Handle_t *dev, uint8_t cmd)
{
    uint8_t ret;
    
    if (dev == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    // 使用软件I2C直接发送指令
    I2C_Start();
    ret = I2C_WriteByte((dev->i2c_addr << 1) | 0);
    if (ret != 0) {
        I2C_Stop();
        return BH1750_ERROR_I2C_WRITE;
    }
    
    ret = I2C_WriteByte(cmd);
    if (ret != 0) {
        I2C_Stop();
        return BH1750_ERROR_I2C_WRITE;
    }
    
    I2C_Stop();
    return BH1750_OK;
}

// 上电
uint8_t BH1750_PowerOn(BH1750_Handle_t *dev)
{
    return BH1750_SendCommand(dev, BH1750_CMD_POWER_ON);
}

// 断电
uint8_t BH1750_PowerDown(BH1750_Handle_t *dev)
{
    return BH1750_SendCommand(dev, BH1750_CMD_POWER_DOWN);
}

// 重置
uint8_t BH1750_Reset(BH1750_Handle_t *dev)
{
    // 注意：Reset命令在Power Down模式下无效，需要先上电
    uint8_t ret;
    
    ret = BH1750_PowerOn(dev);
    if (ret != BH1750_OK) {
        return ret;
    }
    
    return BH1750_SendCommand(dev, BH1750_CMD_RESET);
}

// 开始测量
uint8_t BH1750_StartMeasurement(BH1750_Handle_t *dev, uint8_t mode)
{
    if (dev == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    // 检查模式是否有效
    switch (mode) {
        case BH1750_CMD_CONT_H_RES_MODE:
        case BH1750_CMD_CONT_H_RES_MODE2:
        case BH1750_CMD_CONT_L_RES_MODE:
        case BH1750_CMD_ONE_H_RES_MODE:
        case BH1750_CMD_ONE_H_RES_MODE2:
        case BH1750_CMD_ONE_L_RES_MODE:
            break;
        default:
            return BH1750_ERROR_INVALID_MODE;
    }
    
    return BH1750_SendCommand(dev, mode);
}

// 读取原始数据 (16位)
uint8_t BH1750_ReadRawData(BH1750_Handle_t *dev, uint16_t *raw_data)
{
    uint8_t high_byte, low_byte;
    uint8_t ret;
    
    if (dev == NULL || raw_data == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    // 读取16位数据
    I2C_Start();
    
    // 发送设备地址 + 读标志位
    ret = I2C_WriteByte((dev->i2c_addr << 1) | 1);
    if (ret != 0) {
        I2C_Stop();
        return BH1750_ERROR_I2C_READ;
    }
    
    // 读取高字节
    high_byte = I2C_ReadByte(0);  // 发送应答
    
    // 读取低字节
    low_byte = I2C_ReadByte(1);   // 发送非应答
    
    I2C_Stop();
    
    // 组合16位数据
    *raw_data = (uint16_t)((high_byte << 8) | low_byte);
    
    return BH1750_OK;
}

// 读取光照强度 (单位: lx)
// 计算公式: Lux = (raw_data * (69 / MTreg)) / 1.2
// 对于H-Resolution Mode2: Lux = (raw_data * (69 / MTreg)) / (1.2 * 2)
uint8_t BH1750_ReadLux(BH1750_Handle_t *dev, float *lux)
{
    uint16_t raw_data;
    uint8_t ret;
    float lux_temp;
    
    if (dev == NULL || lux == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    ret = BH1750_ReadRawData(dev, &raw_data);
    if (ret != BH1750_OK) {
        return ret;
    }
    
    // 标准计算公式: Lux = raw_data / 1.2
    // 但需要根据测量时间调整: Lux = raw_data * (69 / MTreg) / 1.2
    lux_temp = (float)raw_data * dev->sensitivity / 1.2f;
    
    *lux = lux_temp;
    
    return BH1750_OK;
}

// 单次测量
uint8_t BH1750_MeasureOnce(BH1750_Handle_t *dev, uint8_t mode, float *lux)
{
    uint8_t ret;
    uint32_t wait_time;
    
    if (dev == NULL || lux == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    // 根据模式确定等待时间
    switch (mode) {
        case BH1750_CMD_CONT_H_RES_MODE:
        case BH1750_CMD_CONT_H_RES_MODE2:
        case BH1750_CMD_ONE_H_RES_MODE:
        case BH1750_CMD_ONE_H_RES_MODE2:
            // 实际等待时间需要根据MTreg值调整
            wait_time = BH1750_MEASURE_TIME_H_MODE * dev->mtreg / BH1750_MTREG_DEFAULT;
            break;
        case BH1750_CMD_CONT_L_RES_MODE:
        case BH1750_CMD_ONE_L_RES_MODE:
            wait_time = BH1750_MEASURE_TIME_L_MODE * dev->mtreg / BH1750_MTREG_DEFAULT;
            break;
        default:
            return BH1750_ERROR_INVALID_MODE;
    }
    
    // 添加一些余量
    wait_time = wait_time * 110 / 100;
    
    // 开始测量
    ret = BH1750_StartMeasurement(dev, mode);
    if (ret != BH1750_OK) {
        return ret;
    }
    
    // 等待测量完成
    HAL_Delay(wait_time);
    
    // 读取结果
    ret = BH1750_ReadLux(dev, lux);
    
    // 如果是单次测量模式，传感器会自动进入Power Down模式
    // 不需要额外处理
    
    return ret;
}

// 设置测量时间 (用于调整灵敏度/光学窗口补偿)
// mt_value: 31-254, 默认69
// 灵敏度 = 69 / mt_value
// 测量时间也会按比例变化
uint8_t BH1750_SetMeasurementTime(BH1750_Handle_t *dev, uint8_t mt_value)
{
    uint8_t ret;
    uint8_t mt_high, mt_low;
    
    if (dev == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    // 检查范围 (根据数据手册: 最小31, 最大254)
    if (mt_value < 31) {
        mt_value = 31;
    }
    if (mt_value > 254) {
        mt_value = 254;
    }
    
    // 分解为高3位和低5位
    // MTreg格式: [MT7][MT6][MT5][MT4][MT3][MT2][MT1][MT0]
    mt_high = (mt_value >> 5) & 0x07;  // 高3位
    mt_low  = mt_value & 0x1F;         // 低5位
    
    // 发送设置高3位指令 (01000_xxx)
    ret = BH1750_SendCommand(dev, BH1750_CMD_CHANGE_MT_HIGH | mt_high);
    if (ret != BH1750_OK) {
        return ret;
    }
    
    // 发送设置低5位指令 (011_xxxxx)
    ret = BH1750_SendCommand(dev, BH1750_CMD_CHANGE_MT_LOW | mt_low);
    if (ret != BH1750_OK) {
        return ret;
    }
    
    // 更新MTreg值
    dev->mtreg = mt_value;
    BH1750_UpdateSensitivity(dev);
    
    return BH1750_OK;
}

// 获取当前测量时间寄存器值
uint8_t BH1750_GetMeasurementTime(BH1750_Handle_t *dev, uint8_t *mt_value)
{
    if (dev == NULL || mt_value == NULL) {
        return BH1750_ERROR_INVALID_MODE;
    }
    
    *mt_value = dev->mtreg;
    return BH1750_OK;
}

// 获取当前灵敏度倍数
float BH1750_GetSensitivity(BH1750_Handle_t *dev)
{
    if (dev == NULL) {
        return 1.0f;
    }
    return dev->sensitivity;
}