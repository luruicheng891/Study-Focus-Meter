/**
  ******************************************************************************
  * @file    UART_ESP01.h
  * @brief   USART3 (ESP-01 WiFi 模块) 底层接收驱动
  *          USART3 + DMA1_Stream1 + IDLE 中断, 仅负责把字节流分帧。
  *          业务层 (天气 JSON 解析 / 队列发布) 见 Task/WeatherTask.{c,h}
  *
  * 接收链路:
  *   ESP-01 → USART3 RX (PB11) → DMA1_Stream1 → AXI SRAM 0x24040800 (256B 循环)
  *      └── IDLE 中断: 计算本帧长度 → DCache invalidate
  *                     → 拷到帧缓冲 → 唤醒注册的任务
  *
  * 与上层解耦方式 (同 UART_ESP32 驱动):
  *   1. 上层 UART_ESP01_BindRxTask(handle) 注册自己的 TaskHandle_t
  *   2. ISR 完帧后 vTaskNotifyGiveFromISR 唤醒该任务
  *   3. 上层 ulTaskNotifyTake 后 UART_ESP01_TakeFrame() 拷出原始字节
  *   4. JSON / 协议解析全部在上层完成, 本驱动一律不参与
  ******************************************************************************
  */

#ifndef __UART_ESP01_H
#define __UART_ESP01_H

#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 配置参数 ============================ */

/* USART3 引脚: PD8 = TX, PB11 = RX (AF7) */
#define USART3_BAUDRATE         115200
#define USART3_RX_BUFFER_SIZE   256

/* DMA RX 缓冲区物理地址 (AXI SRAM)
 *   0x24040000~0x240407FF : LCD ping-pong (2KB)
 *   0x24040800~0x240408FF : USART3 RX (本驱动, 256B)
 *   0x24040900~0x24040CFF : MAX9814 ADC (1KB)
 *   0x24040D00~0x24040DFF : USART2 RX (UART_ESP32, 256B)
 */
#define USART3_RX_DMA_ADDR      0x24040800

/* ============================ 全局对象 ============================ */

extern UART_HandleTypeDef huart3;

/* ============================ 底层 API ============================ */

/**
  * @brief  初始化 USART3 + DMA + IDLE 中断, 启动循环接收
  */
void USART3_Init(void);

/**
  * @brief  USART3 中断分派 (在 stm32h7xx_it.c 的 USART3_IRQHandler 中调用)
  */
void USART3_IRQ_Handler(void);

/**
  * @brief  绑定上层接收任务
  * @param  handle  上层任务句柄, ISR 完帧后会 vTaskNotifyGiveFromISR(handle)
  * @note   传 NULL 可解除绑定。一次只能绑定一个任务。
  */
void UART_ESP01_BindRxTask(TaskHandle_t handle);

/**
  * @brief  从内部帧缓冲拷出最近一帧原始字节
  * @param  buf      用户缓冲区
  * @param  max_len  buf 最大可写字节数 (建议 ≥ USART3_RX_BUFFER_SIZE + 1)
  * @param  out_len  实际拷出的有效字节数 (不含末尾 '\0')
  * @retval 0   成功拷出一帧
  * @retval -1  当前没有就绪帧
  */
int UART_ESP01_TakeFrame(uint8_t *buf, uint16_t max_len, uint16_t *out_len);

/**
  * @brief  读取底层接收统计 (可选, 调试用)
  * @param  recv  累计完整帧数, 可传 NULL
  * @param  drop  上层来不及消费导致丢弃的帧数, 可传 NULL
  */
void UART_ESP01_GetStats(uint32_t *recv, uint32_t *drop);

#ifdef __cplusplus
}
#endif

#endif /* __UART_ESP01_H */
