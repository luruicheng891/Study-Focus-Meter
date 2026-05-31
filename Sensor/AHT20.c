/**
  ******************************************************************************
  * @file    AHT20.c
  * @brief   AHT20 温湿度传感器驱动 (软件 I2C, PB8=SCL, PB9=SDA)
  * @note    AHT20 I2C 地址: 0x38 (7-bit)
  *          使用 I2C 时要使用互斥, 目前尚未实现 (与 MPU6050 共享总线)
  ******************************************************************************
  */

#include "AHT20.h"
#include "I2C.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

// AHT20 I2C 7-bit 地址
#define AHT20_SLAVE_ADDRESS    0x38

// 命令字
#define AHT20_INIT_CMD          0xBE    // 初始化命令
#define AHT20_START_MEASURE     0xAC    // 触发测量命令
#define AHT20_SOFT_RESET        0xBA    // 软复位命令

// 状态寄存器位定义
#define AHT20_STATUS_BUSY       0x80
#define AHT20_STATUS_CAL_EN     0x08

/**
 * @brief  直接从 AHT20 读取数据 (不发寄存器地址)
 * @note   AHT20 协议: 直接发 [地址+R] 然后读 N 字节
 *         与标准 I2C 寄存器读取不同, 不需要先写寄存器地址
 * @param  buffer: 接收缓冲区
 * @param  len: 要读取的字节数
 * @retval 0=成功, 非0=失败
 */
static uint8_t AHT20_DirectRead(uint8_t *buffer, uint16_t len)
{
    uint16_t i;
    
    I2C_Start();
    
    // 发送设备地址 + 读标志位 (1)
    if(I2C_WriteByte((AHT20_SLAVE_ADDRESS << 1) | 1))
    {
        I2C_Stop();
        return 1;  // NACK, 设备无应答
    }
    
    // 读取数据
    for(i = 0; i < len; i++)
    {
        // 最后一个字节发 NACK, 之前的字节发 ACK
        buffer[i] = I2C_ReadByte((i == len - 1) ? 1 : 0);
    }
    
    I2C_Stop();
    return 0;
}

/**
 * @brief  向 AHT20 发送命令 (带参数)
 * @param  cmd: 命令字节
 * @param  param: 参数数组 (可为 NULL)
 * @param  param_len: 参数长度
 * @retval 0=成功, 非0=失败
 */
static uint8_t AHT20_SendCmd(uint8_t cmd, uint8_t *param, uint16_t param_len)
{
    uint16_t i;
    
    I2C_Start();
    
    // 发送设备地址 + 写标志位 (0)
    if(I2C_WriteByte((AHT20_SLAVE_ADDRESS << 1) | 0))
    {
        I2C_Stop();
        return 1;
    }
    
    // 发送命令
    if(I2C_WriteByte(cmd))
    {
        I2C_Stop();
        return 2;
    }
    
    // 发送参数
    for(i = 0; i < param_len; i++)
    {
        if(I2C_WriteByte(param[i]))
        {
            I2C_Stop();
            return 3;
        }
    }
    
    I2C_Stop();
    return 0;
}

/**
 * @brief 读取 AHT20 状态字节
 */
uint8_t AHT20_Read_Status(void)
{
    uint8_t status = 0xFF;
    
    if(AHT20_DirectRead(&status, 1) != 0)
    {
        printf("[AHT20] Read status FAILED (NACK)\r\n");
        return 0xFF;
    }
    return status;
}

/**
 * @brief 检查校准使能位
 */
uint8_t AHT20_Read_Cal_Enable(void)
{
    uint8_t val = AHT20_Read_Status();
    if(val == 0xFF) return 0;
    return ((val & AHT20_STATUS_CAL_EN) == AHT20_STATUS_CAL_EN) ? 1 : 0;
}

/**
 * @brief 读取温度和湿度原始数据
 * @param ct: ct[0]=湿度原始值, ct[1]=温度原始值
 */
void AHT20_Read_CTdata(uint32_t *ct)
{
    uint32_t RetuData = 0;
    uint16_t cnt = 0;
    uint8_t Data[7] = {0};
    uint8_t ret;
    uint8_t status;
    
    // 发送触发测量命令: 0xAC 0x33 0x00
    uint8_t measure_param[2] = {0x33, 0x00};
    ret = AHT20_SendCmd(AHT20_START_MEASURE, measure_param, 2);
    if(ret != 0)
    {
        printf("[AHT20] Send measure cmd FAILED (ret=%d)\r\n", ret);
        ct[0] = 0;
        ct[1] = 0;
        return;
    }
    
    // 等待测量完成 (AHT20 典型测量时间 75ms)
    vTaskDelay(pdMS_TO_TICKS(80));
    
    // 等待 busy 状态清除, 最多等待 100ms
    cnt = 0;
    while(1)
    {
        status = AHT20_Read_Status();
        if((status & AHT20_STATUS_BUSY) == 0)
            break;
        
        vTaskDelay(pdMS_TO_TICKS(5));
        if(cnt++ >= 20)  // 100ms 超时
        {
            printf("[AHT20] Measure timeout! status=0x%02X\r\n", status);
            ct[0] = 0;
            ct[1] = 0;
            return;
        }
    }
    
    // 直接读取 7 字节数据: [状态][湿度H][湿度M][湿度L|温度H][温度M][温度L][CRC]
    ret = AHT20_DirectRead(Data, 7);
    if(ret != 0)
    {
        printf("[AHT20] Read data FAILED\r\n");
        ct[0] = 0;
        ct[1] = 0;
        return;
    }
    
    // 解析湿度原始数据 (20位)
    RetuData = ((uint32_t)Data[1] << 12) | ((uint32_t)Data[2] << 4) | ((uint32_t)Data[3] >> 4);
    ct[0] = RetuData;
    
    // 解析温度原始数据 (20位)
    RetuData = ((uint32_t)(Data[3] & 0x0F) << 16) | ((uint32_t)Data[4] << 8) | (uint32_t)Data[5];
    ct[1] = RetuData;
}

/**
 * @brief 获取实际的温度和湿度值
 * @param temperature: 温度, 单位 0.01°C (如 2500 = 25.00°C)
 * @param humidity: 湿度, 单位 0.01% (如 5678 = 56.78%)
 */
void AHT20_Get_Temp_Humidity(int32_t *temperature, uint32_t *humidity)
{
    uint32_t ct[2] = {0};
    
    AHT20_Read_CTdata(ct);
    
    // 湿度转换: Humidity = (ct[0] / 2^20) * 100, 放大100倍 => ct[0] * 10000 / 1048576
    // 注意: ct[0] 最大 1048575, 乘 10000 会溢出 uint32, 必须用 uint64 或先除后乘
    *humidity = (uint32_t)(((uint64_t)ct[0] * 10000) / 1048576);
    
    // 温度转换: Temperature = (ct[1] / 2^20) * 200 - 50, 放大100倍
    *temperature = (int32_t)(((uint64_t)ct[1] * 20000) / 1048576 - 5000);
}

/**
 * @brief 软复位 AHT20
 */
void AHT20_Soft_Reset(void)
{
    I2C_Start();
    I2C_WriteByte((AHT20_SLAVE_ADDRESS << 1) | 0);
    I2C_WriteByte(AHT20_SOFT_RESET);
    I2C_Stop();
    
    vTaskDelay(pdMS_TO_TICKS(20));
}

/**
 * @brief 初始化 AHT20 传感器
 * @retval 1=成功, 0=失败
 */
uint8_t AHT20_Init(void)
{
    uint8_t retry_count = 0;
    uint8_t init_param[2] = {0x08, 0x00};
    uint8_t ret;
    
    // 上电等待 40ms (AHT20 datasheet 要求)
    vTaskDelay(pdMS_TO_TICKS(40));
    
    // 发送初始化命令: 0xBE 0x08 0x00
    ret = AHT20_SendCmd(AHT20_INIT_CMD, init_param, 2);
    if(ret != 0)
    {
        printf("[AHT20] Init cmd send FAILED (ret=%d)\r\n", ret);
        return 0;
    }
    
    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 检查校准位
    while(AHT20_Read_Cal_Enable() == 0)
    {
        printf("[AHT20] CAL bit not set, retry %d...\r\n", retry_count);
        AHT20_Soft_Reset();
        
        ret = AHT20_SendCmd(AHT20_INIT_CMD, init_param, 2);
        if(ret != 0)
        {
            printf("[AHT20] Re-init cmd FAILED\r\n");
            return 0;
        }
        
        retry_count++;
        if(retry_count >= 10)
        {
            printf("[AHT20] CAL bit never set after 10 retries\r\n");
            return 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    return 1;
}
