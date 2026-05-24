#include "I2C.h"

static void I2C_Delay(void)
{
    volatile uint32_t delay = I2C_DELAY_US * 20; // 粗略延时，需要根据系统时钟调整
    while(delay--);
    // 更精确的延时可以用HAL_Delay或者定时器
    // HAL_Delay(I2C_DELAY_US / 1000); // 如果微秒延时不够精确
}

// 初始化I2C引脚
void I2C_Sim_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // 使能GPIO时钟
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // 配置SCL和SDA为开漏输出模式
    GPIO_InitStruct.Pin = I2C_SCL_GPIO_PIN | I2C_SDA_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;  // 开漏输出
    GPIO_InitStruct.Pull = GPIO_PULLUP;          // 上拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // 初始状态：SCL和SDA都为高电平
    I2C_SCL_HIGH();
    I2C_SDA_HIGH();
    I2C_Delay();
}

// 起始信号：SCL高电平时，SDA由高变低
void I2C_Start(void)
{
    I2C_SDA_HIGH();
    I2C_SCL_HIGH();
    I2C_Delay();
    I2C_SDA_LOW();
    I2C_Delay();
    I2C_SCL_LOW();
    I2C_Delay();
}

// 停止信号：SCL高电平时，SDA由低变高
void I2C_Stop(void)
{
    I2C_SDA_LOW();
    I2C_SCL_HIGH();
    I2C_Delay();
    I2C_SDA_HIGH();
    I2C_Delay();
}

// 发送一个字节，返回从机应答位（0=应答，1=非应答）
uint8_t I2C_WriteByte(uint8_t data)
{
    uint8_t i;
    uint8_t ack;
    
    // 发送8位数据
    for(i = 0; i < 8; i++)
    {
        if(data & 0x80)
            I2C_SDA_HIGH();
        else
            I2C_SDA_LOW();
        data <<= 1;
        I2C_Delay();
        I2C_SCL_HIGH();
        I2C_Delay();
        I2C_SCL_LOW();
        I2C_Delay();
    }
    
    // 释放SDA，准备接收应答
    I2C_SDA_HIGH();
    I2C_Delay();
    I2C_SCL_HIGH();
    I2C_Delay();
    ack = I2C_SDA_READ();  // 读取应答位
    I2C_SCL_LOW();
    I2C_Delay();
    
    return ack;
}

// 读取一个字节，ack=0发送应答，ack=1发送非应答
uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0;
    
    // 释放SDA，让从机控制
    I2C_SDA_HIGH();
    
    // 读取8位数据
    for(i = 0; i < 8; i++)
    {
        data <<= 1;
        I2C_Delay();
        I2C_SCL_HIGH();
        I2C_Delay();
        if(I2C_SDA_READ())
            data |= 0x01;
        I2C_SCL_LOW();
        I2C_Delay();
    }
    
    // 发送应答/非应答信号
    if(ack)
        I2C_SDA_HIGH();  // 非应答
    else
        I2C_SDA_LOW();   // 应答
    I2C_Delay();
    I2C_SCL_HIGH();
    I2C_Delay();
    I2C_SCL_LOW();
    I2C_Delay();
    
    return data;
}

// 向从机设备写数据
// dev_addr: 从机地址（7位，不含读写位）
// reg_addr: 寄存器地址
// data: 要发送的数据指针
// len: 数据长度
// 返回值: 0=成功, 1=失败
uint8_t I2C_WriteData(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    uint8_t ret = 0;
    uint16_t i;
    
    I2C_Start();
    
    // 发送设备地址 + 写标志位（0）
    if(I2C_WriteByte((dev_addr << 1) | 0))
    {
        ret = 1;
        goto stop;
    }
    
    // 发送寄存器地址
    if(I2C_WriteByte(reg_addr))
    {
        ret = 2;
        goto stop;
    }
    
    // 发送数据
    for(i = 0; i < len; i++)
    {
        if(I2C_WriteByte(data[i]))
        {
            ret = 3;
            goto stop;
        }
    }
    
stop:
    I2C_Stop();
    return ret;
}

// 从从机设备读数据
// dev_addr: 从机地址（7位，不含读写位）
// reg_addr: 寄存器地址
// buffer: 接收数据缓冲区
// len: 要读取的数据长度
// 返回值: 0=成功, 1=失败
uint8_t I2C_ReadData(uint8_t dev_addr, uint8_t reg_addr, uint8_t *buffer, uint16_t len)
{
    uint8_t ret = 0;
    uint16_t i;
    
    // 先发送寄存器地址
    I2C_Start();
    
    // 发送设备地址 + 写标志位
    if(I2C_WriteByte((dev_addr << 1) | 0))
    {
        ret = 1;
        goto stop;
    }
    
    // 发送寄存器地址
    if(I2C_WriteByte(reg_addr))
    {
        ret = 2;
        goto stop;
    }
    
    // 重新开始条件
    I2C_Start();
    
    // 发送设备地址 + 读标志位（1）
    if(I2C_WriteByte((dev_addr << 1) | 1))
    {
        ret = 3;
        goto stop;
    }
    
    // 读取数据
    for(i = 0; i < len; i++)
    {
        // 最后一个字节发送非应答，之前字节发送应答
        buffer[i] = I2C_ReadByte((i == len - 1) ? 1 : 0);
    }
    
stop:
    I2C_Stop();
    return ret;
}

