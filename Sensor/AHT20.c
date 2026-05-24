#include "AHT20.h"
#include "I2C.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stdio.h"
/*使用I2C时要使用互斥，目前还未实现*/
/*使用I2C时要使用互斥，目前还未实现*/
/*使用I2C时要使用互斥，目前还未实现*/
/*使用I2C时要使用互斥，目前还未实现*/




// AHT20 寄存器地址和命令
#define AHT20_SLAVE_ADDRESS    0x38

// 命令定义
#define AHT20_INIT_CMD          0xBE    // 初始化命令
#define AHT20_START_MEASURE     0xAC    // 开始测量命令
#define AHT20_SOFT_RESET        0xBA    // 软件复位命令

// 状态寄存器位定义
#define AHT20_STATUS_BUSY       0x80
#define AHT20_STATUS_CAL_EN     0x08

/**
 * @brief 读取AHT20状态寄存器
 */
uint8_t AHT20_Read_Status(void)
{
    uint8_t status;
    I2C_ReadData(AHT20_SLAVE_ADDRESS, 0x00, &status, 1);
    return status;
}

/**
 * @brief 检查校准使能位
 */
uint8_t AHT20_Read_Cal_Enable(void)
{
    uint8_t val = AHT20_Read_Status();
    return ((val & AHT20_STATUS_CAL_EN) == AHT20_STATUS_CAL_EN) ? 1 : 0;
}

/**
 * @brief 读取温度和湿度数据
 */
void AHT20_Read_CTdata(uint32_t *ct)
{
    uint32_t RetuData = 0;
    uint16_t cnt = 0;
    uint8_t Data[7] = {0};
    
    // 发送测量命令
    uint8_t measure_cmd[2] = {0x33, 0x00};
    I2C_WriteData(AHT20_SLAVE_ADDRESS, AHT20_START_MEASURE, measure_cmd, 2);
    
    // 等待测量完成（AHT20测量时间典型值75ms）
    vTaskDelay(pdMS_TO_TICKS(75));
    
    // 等待忙状态结束，最多等待100ms
    cnt = 0;
    while((AHT20_Read_Status() & AHT20_STATUS_BUSY) == AHT20_STATUS_BUSY)
    {
        vTaskDelay(pdMS_TO_TICKS(1));  // 每1ms检查一次
        if(cnt++ >= 100)  // 100ms超时
        {
            break;
        }
    }
    
    // 读取7字节数据
    I2C_ReadData(AHT20_SLAVE_ADDRESS, 0x00, Data, 7);
    
    // 解析湿度数据 (20位)
    RetuData = ((uint32_t)Data[1] << 12) | ((uint32_t)Data[2] << 4) | ((uint32_t)Data[3] >> 4);
    ct[0] = RetuData;
    
    // 解析温度数据 (20位)
    RetuData = ((uint32_t)(Data[3] & 0x0F) << 16) | ((uint32_t)Data[4] << 8) | (uint32_t)Data[5];
    ct[1] = RetuData;
}

/**
 * @brief 获取实际的温度和湿度值
 */
void AHT20_Get_Temp_Humidity(int32_t *temperature, uint32_t *humidity)
{
    uint32_t ct[2] = {0};
    
    AHT20_Read_CTdata(ct);
    
    // 湿度转换: Humidity = (ct[0] * 100 * 100) / 2^20
    *humidity = (uint32_t)((ct[0] * 10000UL) / 1048576UL);
    
    // 温度转换: Temperature = (ct[1] * 200 * 100) / 2^20 - 5000
    *temperature = (int32_t)(((ct[1] * 20000UL) / 1048576UL) - 5000);
}

/**
 * @brief 软件复位AHT20
 */
void AHT20_Soft_Reset(void)
{
    I2C_Start();
    I2C_WriteByte((AHT20_SLAVE_ADDRESS << 1) | 0);
    I2C_WriteByte(AHT20_SOFT_RESET);
    I2C_Stop();
    
    vTaskDelay(pdMS_TO_TICKS(20));  // 复位后等待20ms
}

/**
 * @brief 初始化AHT20传感器
 */
uint8_t AHT20_Init(void)
{
    uint8_t retry_count = 0;
    uint8_t init_cmd[2] = {0x08, 0x00};
    
    // 上电后等待40ms
    vTaskDelay(pdMS_TO_TICKS(40));
    
    // 发送初始化命令
    if(I2C_WriteData(AHT20_SLAVE_ADDRESS, AHT20_INIT_CMD, init_cmd, 2) != 0)
    {
        return 0;
    }
    
    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 检查校准位
    while(AHT20_Read_Cal_Enable() == 0)
    {
        AHT20_Soft_Reset();
        
        if(I2C_WriteData(AHT20_SLAVE_ADDRESS, AHT20_INIT_CMD, init_cmd, 2) != 0)
        {
            return 0;
        }
        
        retry_count++;
        if(retry_count >= 10)
        {
            return 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    return 1;
}

