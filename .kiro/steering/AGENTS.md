---
inclusion: auto
---

# 项目约束与开发规范

## 项目概述

- 产品: OV2640 摄像头采集 + ILI9341 LCD 显示 + MPU6050 六轴传感器
- MCU: STM32H723ZGT6 (Cortex-M7, 550MHz)
- IDE: Keil MDK-ARM V5 (ARM Compiler V5, armcc)
- RTOS: FreeRTOS V10.3.1 + CMSIS-RTOS V2
- GUI: LVGL v8.3.11
- 工程文件: `MDK-ARM/STM32H723.uvprojx`

## 硬件配置

### 时钟
- HSE = 25MHz, SYSCLK = 550MHz (PLL: M=10, N=220, P=1)
- AHB = 275MHz, APB1/APB2/APB3/APB4 = 137.5MHz

### 外设引脚分配
- **SPI1 (LCD)**: PB3=SCK, PB5=MOSI, PD0=CS, PD1=DC, PD2=RST, PB4=LED
- **SPI1 TX DMA**: DMA1_Stream0 (DMAMUX request 38), 中断优先级 (1,0)
- **SPI1 中断**: 优先级 (1,1), 用于 DMA 错误处理
- **DCMI (OV2640)**: PE4=D4, PE5=D6, PE6=D7, PA4/PC6=D0, PA6=PIXCLK, PC7=D1, PD3=D5, PG9=VSYNC, PG10=D2, PG11=D3
- **DCMI DMA**: DMA2_Stream7, 中断优先级 (0,0)
- **SCCB (OV2640 I2C)**: PF14=SCL, PF15=SDA (软件模拟, 地址 0x60)
- **I2C (MPU6050)**: PB8=SCL, PB9=SDA (软件模拟 GPIO bit-bang, 地址 0x68)
- **TIM7 (LVGL Tick)**: 基本定时器, 1ms 中断, 中断优先级 (3,0), 直接寄存器操作 (未启用 HAL_TIM_MODULE)
- **USART1**: PA9=TX, PA10=RX (921600 baud)
- **LED**: PG7 (低电平点亮)
- **OV2640 PWDN**: PF13

### 内存布局 (Scatter File: MDK-ARM/STM32H723/STM32H723.sct)

| 区域 | 起始地址 | 大小 | 用途 | DMA 可访问 |
|------|----------|------|------|-----------|
| Flash | 0x08000000 | 1MB | 代码 + 只读数据 | - |
| DTCM | 0x20000000 | 128KB | 栈、堆、普通变量 | ❌ 不可 |
| AXI SRAM | 0x24000000 | 320KB | DMA 缓冲区 | ✅ 可以 |

**AXI SRAM 分配:**
- 0x24000000 ~ 0x2401C1FF: Camera DMA 帧缓冲 (115.2KB, 代码中宏定义)
- 0x24040000 ~ 0x240407FF: LCD SPI DMA ping-pong 缓冲 (2KB, `__attribute__((section))`)

### MPU 配置
- Region 0: 0x24000000, 512KB, Full Access, Cacheable+Bufferable+Shareable

## 代码结构

```
Core/Src/          - main.c, freertos.c, stm32h7xx_it.c (中断), HAL MSP
Drivers/User/      - led, usart, sccb, dcmi_ov2640 驱动
LCD/               - lcd.c, lcd_spi.c (SPI1 + DMA), lcd.h, lcd_spi.h
Communication/     - I2C.c (软件模拟), MPU6050.c, MPU6050_Reg.h
SYSTEM/            - TIM.c, TIM.h (TIM7 基本定时器, 为 LVGL 提供 1ms 心跳)
Task/              - Camera.c (FreeRTOS 任务)
LVGL/              - LVGL v8.3.11 源码 (src/core, src/draw, src/draw/sw, src/hal, src/misc, src/font, src/widgets, src/extra)
LVGL/examples/porting/ - lv_port_disp.c, lv_port_indev.c
Freertos/          - FreeRTOS 内核源码
MDK-ARM/           - Keil 工程文件, 启动文件, scatter file
MDK-ARM/LCD/       - lcd.h, lcd_spi.h 副本 (Keil Include Path `.\LCD` 指向此处)
```

## 编码规范

- 语言: C99
- 编译器选项: `--c99 --cpu Cortex-M7.fp.dp -D__MICROLIB -g -O1 --apcs=interwork --split_sections`
- 使用 MicroLib (轻量 C 库)
- HAL 库风格: 使用 `HAL_xxx` API, 不直接操作寄存器 (除非性能关键路径)
- 命名: 模块前缀 (LCD_, OV2640_, MPU6050_, I2C_, SCCB_, TIM7_)
- 注释: 中文注释为主, 函数头用 Doxygen 风格

## 重要注意事项 (踩坑记录)

1. **DTCM 不可被 DMA 访问**: STM32H7 的 DTCM (0x20000000) 只有 CPU 能访问, DMA1/DMA2 无法读写! 所有 DMA 缓冲区必须放在 AXI SRAM (0x24000000+), 使用 `__attribute__((section(".ARM.__at_0x240xxxxx")))` 并在 scatter file 中声明对应区域。
2. **DCache 一致性**: DMA TX 前调用 `SCB_CleanDCache_by_Addr()`, DMA RX 后调用 `SCB_InvalidateDCache_by_Addr()`。缓冲区地址必须 32 字节对齐。
3. **LCD SPI DMA**: 使用 ping-pong 双缓冲 (`lcd_dma_buf[2][1024]`), DMA 完成回调在 `HAL_SPI_TxCpltCallback()` 中清除 busy 标志。`LCD_DMA_Wait()` 带超时保护防止死循环。
4. **LVGL 路径问题**: LVGL v8 使用分层目录, 工程文件中的路径必须匹配实际目录结构。
5. **LVGL lv_conf.h**: 位于 `LVGL/lv_conf.h`, 需要定义 `LV_CONF_INCLUDE_SIMPLE` 宏并将 `../LVGL` 加入 Include Path。
6. **软件 I2C**: MPU6050 (PB8/PB9) 和 OV2640 (PF14/PF15) 各自使用独立的软件 I2C, 非硬件 I2C 外设。
7. **FreeRTOS 任务**: Camera_task 栈 4KB, 负责摄像头采集+LCD刷屏; 新增任务注意栈大小和优先级。
8. **Keil 工程**: 修改源文件列表需编辑 `.uvprojx` XML; Include Path 在 `<IncludePath>` 标签中; 修改 LCD 头文件后需同步到 `MDK-ARM/LCD/` 目录。
9. **中断优先级**: DCMI/DMA2 优先级 (0,x) 高于 SPI/DMA1 优先级 (1,x), 确保摄像头采集不被 LCD 刷屏阻塞。
10. **TIM7 LVGL 心跳**: 使用寄存器直接配置 TIM7 (无需 `HAL_TIM_MODULE_ENABLED`), PSC=274, ARR=999, 产生 1ms 中断调用 `lv_tick_inc(1)`。`TIM7_IRQHandler` 定义在 `SYSTEM/TIM.c` 中, 确保 `stm32h7xx_it.c` 中不要重复定义。

## 当前已知问题

- `.uvprojx` 中大部分 LVGL 源文件路径仍为扁平结构 (需修正为 v8 分层路径)
- 需要在 Keil Define 中添加 `LV_CONF_INCLUDE_SIMPLE`
- `Camera_task` 函数签名已修正为 `void Camera_task(void *argument)`
