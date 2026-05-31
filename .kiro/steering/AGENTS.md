---
inclusion: auto
---

# 项目约束与开发规范

## 项目概述

- 产品: AI 学习状态监测仪 (摄像头 + 环境传感器 + 蓝牙姿态)
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
- **I2C 传感器总线**: PB8=SCL, PB9=SDA (软件模拟 GPIO bit-bang, 开漏+上拉, ~100kHz)
  - AHT20 温湿度: 地址 0x38
  - BH1750FVI 光照: 地址 0x23 (ADDR=GND)
- **USART3 (ESP-01)**: PD8=TX, PB11=RX, 115200 baud
  - DMA 接收: DMA1_Stream1 (DMAMUX request 45)
  - 中断优先级: USART3=(6,1), DMA1_Stream1=(6,0)
  - DMA 缓冲区: 0x24040800 (AXI SRAM, 256 字节)
- **TIM7 (LVGL Tick)**: 基本定时器, 1ms 中断, 中断优先级 (3,0), 直接寄存器操作 (未启用 HAL_TIM_MODULE)
- **USART1 (调试 + 命令)**: PA9=TX, PA10=RX, 115200 baud
  - TX: 非阻塞环形缓冲 (2KB) + TXE 中断, fputc 重定向 (位于 `Task/display_mode.c`)
  - RX: 单字节 RXNE 中断, 接收 'C'/'W' 命令切换显示模式
  - NVIC 优先级 (6, 0)
- **蓝牙 (PB-03F)**: UART 透传, 接收从机端 MPU6050 数据
- **LED**: PG7 (低电平点亮)
- **OV2640 PWDN**: PF13

### I2C 总线设备 (PB8/PB9)

| 设备 | 7-bit 地址 | 说明 |
|------|-----------|------|
| AHT20 | 0x38 | 温湿度传感器 |
| BH1750FVI | 0x23 | 光照传感器 (ADDR=GND) |

> 两设备在同一任务中顺序访问，无需互斥锁。
> MPU6050 不在本机 I2C 总线上 (从机端通过 PB-03F 蓝牙透传)。

### 内存布局 (Scatter File: MDK-ARM/STM32H723/STM32H723.sct)

| 区域 | 起始地址 | 大小 | 用途 | DMA 可访问 |
|------|----------|------|------|-----------|
| Flash | 0x08000000 | 1MB | 代码 + 只读数据 | - |
| DTCM | 0x20000000 | 128KB | 栈、堆、普通变量 | ❌ 不可 |
| AXI SRAM | 0x24000000 | 320KB | DMA 缓冲区 | ✅ 可以 |

**AXI SRAM 分配:**
- 0x24000000 ~ 0x2401C1FF: Camera DMA 帧缓冲 (115.2KB)
- 0x2401C200 ~ 0x2403FFFF: LVGL canvas buffer 等
- 0x24040000 ~ 0x240407FF: LCD SPI DMA ping-pong 缓冲 (2KB)
- 0x24040800 ~ 0x240408FF: USART3 RX DMA 缓冲 (256 字节, ESP-01 接收)

### MPU 配置
- Region 0: 0x24000000, 512KB, Full Access, Cacheable+Bufferable+Shareable

## LCD 显示

- **物理面板**: ILI9341, 240×320 原始分辨率
- **使用方向**: 横屏 320×240 (`USE_HORIZONTAL = 1`)
- **运行时方向切换**: 由 `Weather_ApplyMode()` 控制, 切换 `LCD_direction()`
  - 模式 1 (横屏 90°): 摄像头模式
  - 模式 3 (横屏 270°, 即 180° 翻转): 天气仪表板模式
- **LVGL 显示**: `MY_DISP_HOR_RES=320`, `MY_DISP_VER_RES=240`

## 显示模式切换

通过 USART1 接收单字符命令运行时切换:
- `'C'` / `'c'` → `DISP_MODE_CAMERA` (摄像头, 240×240 居中显示)
- `'W'` / `'w'` → `DISP_MODE_WEATHER` (天气仪表板, 320×240 全屏, 180° 翻转)

实现细节:
- `Task/display_mode.c` 提供 `g_display_mode` 全局变量、`DisplayMode_Request/TakePending()` API
- ISR 只设标志位, 真正切换在 `Weather_Task` 主循环中调用 `Weather_ApplyMode()`
- DCMI/DMA 始终运行, 通过 `LV_OBJ_FLAG_HIDDEN` 切换 canvas 与 weather panel 可见性
- 切换后 `lv_obj_invalidate(lv_scr_act())` 强制整屏重绘

## 代码结构

```
Core/Src/          - main.c, freertos.c, stm32h7xx_it.c (中断), HAL MSP
Drivers/User/      - led, usart, sccb, dcmi_ov2640 驱动 (注: usart.c 中旧 fputc 已禁用)
LCD/               - lcd.c, lcd_spi.c (SPI1 + DMA), lcd.h, lcd_spi.h
Communication/     - I2C.c (软件模拟 PB8/PB9), MPU6050.c (保留, 实际在从机端)
Sensor/            - AHT20.c/.h, BH1750.c/.h, UART.c/.h (USART3 + cJSON)
Task/              - Camera.c/.h, SensorTask.c/.h, WeatherTask.c/.h,
                     display_mode.c/.h (模式切换 + 非阻塞 fputc + USART1 IRQ)
SYSTEM/            - TIM.c, TIM.h (TIM7 基本定时器, 为 LVGL 提供 1ms 心跳)
LVGL/              - LVGL v8.3.11 源码 (启用 Montserrat 14/16/20/28 字体)
LVGL/examples/porting/ - lv_port_disp.c (320×240 横屏配置), lv_port_indev.c
Freertos/          - FreeRTOS 内核源码
MDK-ARM/           - Keil 工程文件, 启动文件, scatter file
```

## FreeRTOS 任务

| 任务名 | 函数 | 栈大小 | 优先级 | 功能 |
|--------|------|--------|--------|------|
| LVGL_Task | LVGL_Task() | 512 words | AboveNormal | lv_timer_handler() 5ms 周期 |
| Camera_task | Camera_task() | 1024 words | Normal | DCMI 采集 + LVGL canvas 显示 (始终运行) |
| AHT20_Task | AHT20_Task() | 512 words | Normal | AHT20+BH1750 采集, 1s 周期 |
| Weather_Task | Weather_Task() | 512 words | Normal | LVGL UI 显示 + 模式切换响应 (100ms 轮询) |
| Weather_RxTask | Weather_RxTask() | 512 words | Normal | ESP-01 USART3 接收+JSON 解析 |

## 传感器数据流

```
AHT20_Task (1s 周期, 顺序读取两个传感器)
  ├── AHT20 温湿度 → SensorData_t.temperature / humidity
  ├── BH1750 光照   → SensorData_t.lux_x100
  └── xQueueOverwrite(xSensorDataQueue) → Weather_Task 消费显示

Weather_RxTask (USART3 IDLE 中断驱动)
  ├── ESP-01 → USART3 RX (PB11) → DMA1_Stream1 → AXI SRAM 0x24040800
  ├── IDLE 中断 → SCB_InvalidateDCache → 任务唤醒
  ├── cJSON 解析 (使用 FreeRTOS pvPortMalloc/vPortFree)
  ├── printf 调试到 USART1 (走非阻塞环形缓冲, 不会卡死)
  └── xQueueOverwrite(xWeatherQueue) → Weather_Task 消费显示

USART1 命令通道
  PC 串口助手 → PA10 → USART1 RXNE 中断
    └── 'C'/'W' → DisplayMode_Request() (设置 pending flag)
         └── Weather_Task 主循环 → Weather_ApplyMode()
              ├── LCD_direction(1 或 3)
              ├── 切换 canvas / weather_panel hidden flag
              └── lv_obj_invalidate(lv_scr_act())
```

**关键数据结构:**
```c
typedef struct {
    int32_t  temperature;  // 单位 0.01°C (2500 = 25.00°C)
    uint32_t humidity;     // 单位 0.01% (5678 = 56.78%)
    uint32_t lux_x100;    // 单位 0.01 lx (10050 = 100.50 lx)
    uint32_t timestamp;   // FreeRTOS tick
} SensorData_t;
```

## 编码规范

- 语言: C99
- 编译器选项: `--c99 --cpu Cortex-M7.fp.dp -D__MICROLIB -g -O1 --apcs=interwork --split_sections`
- 使用 MicroLib (轻量 C 库)
- HAL 库风格: 使用 `HAL_xxx` API, 不直接操作寄存器 (除非性能关键路径)
- 命名: 模块前缀 (LCD_, OV2640_, AHT20_, BH1750_, I2C_, SCCB_, TIM7_)
- 注释: 中文注释为主, 函数头用 Doxygen 风格

## 重要注意事项 (踩坑记录)

1. **DTCM 不可被 DMA 访问**: STM32H7 的 DTCM (0x20000000) 只有 CPU 能访问, DMA1/DMA2 无法读写! 所有 DMA 缓冲区必须放在 AXI SRAM (0x24000000+)。
2. **DCache 一致性**: DMA TX 前调用 `SCB_CleanDCache_by_Addr()`, DMA RX 后调用 `SCB_InvalidateDCache_by_Addr()`。缓冲区地址必须 32 字节对齐。
3. **LCD SPI DMA**: 使用 ping-pong 双缓冲, DMA 完成回调清除 busy 标志。`LCD_DMA_Wait()` 带超时保护。
4. **LVGL lv_conf.h**: 位于 `LVGL/lv_conf.h`, 需要定义 `LV_CONF_INCLUDE_SIMPLE` 宏并将 `../LVGL` 加入 Include Path。
5. **软件 I2C 总线**: PB8/PB9 上挂 AHT20 + BH1750, 在同一任务中顺序访问无需互斥。OV2640 SCCB 使用独立的 PF14/PF15。
6. **AHT20 I2C 协议**: 读取时直接发 [地址+R] 读 N 字节, 不需要先写寄存器地址 (与标准 I2C 寄存器读取不同)。
7. **AHT20 湿度计算溢出**: `ct[0] * 10000` 超过 uint32 最大值, 必须用 `(uint64_t)ct[0] * 10000` 防止溢出。
8. **BH1750 命令格式**: 每条命令后必须插入 Stop (不能在一次传输中发多条命令)。
9. **BH1750 使用模式**: 连续高分辨率模式 (0x10), 初始化时启动一次, 之后定时读取。不要调用 `BH1750_MeasureOnce()` (内部用 HAL_Delay, FreeRTOS 中不安全)。
10. **传感器独立初始化**: AHT20 和 BH1750 各自初始化, 任一个不在线不影响另一个工作, 不会导致任务退出。
11. **FreeRTOS 任务**: 新增任务注意栈大小和优先级。当前 4 个任务共用 15KB FreeRTOS 堆。
12. **Keil 工程**: 修改源文件列表需编辑 `.uvprojx` XML; Include Path 包含 `../Sensor`, `../Task`, `../Communication`。
13. **中断优先级**: DCMI/DMA2 (0,x) > SPI/DMA1 (1,x) > TIM7 (3,0)。
14. **TIM7 LVGL 心跳**: PSC=274, ARR=999, 1ms 中断。`TIM7_IRQHandler` 在 `SYSTEM/TIM.c` 中定义。
15. **MPU6050 在从机端**: 通过 PB-03F BLE 蓝牙模块 UART 透传到主机, 不占用本机 I2C 总线。
16. **ESP-01 USART3 DMA 缓冲区位置**: 必须在 AXI SRAM (`0x24040800`), 不能在 DTCM, 否则 DMA 接收无效, buffer 始终为 0。使用 `__attribute__((section(".ARM.__at_0x24040800"), aligned(32)))` 强制定位。
17. **USART3 IDLE 中断**: HAL_UART_Receive_DMA() 后必须手动 `__HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE)`。每帧 IDLE 触发后用 `HAL_UART_DMAStop` + `HAL_UART_Receive_DMA` 重启重置 DMA 计数器。
18. **DMA RX DCache 失效**: AXI SRAM 配置为 Cacheable, DMA 写入后必须 `SCB_InvalidateDCache_by_Addr()` 才能让 CPU 读到新数据。
19. **cJSON FreeRTOS 适配**: main.c 中 `cJSON_InitHooks()` 注册 `pvPortMalloc/vPortFree`, 使解析使用 FreeRTOS 堆 (heap_4)。
20. **中断优先级与 FreeRTOS**: `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`, 调用 FromISR API 的中断必须 ≥5 (数值越大优先级越低)。建议用 6 留出余量。
21. **USART1 printf 必须非阻塞**: 旧版 `HAL_UART_Transmit` 阻塞实现在多任务并发 printf 时会卡死 (`huart1->gState` 状态机被打断后无法恢复)。`Task/display_mode.c` 中重写了 fputc 为环形缓冲 + TXE 中断驱动。`Drivers/User/Src/usart.c` 里旧 fputc 已 `#if 0` 注释禁用。新代码若需要修改 fputc, 改 `display_mode.c`, 不要恢复 usart.c 里的旧版本。
22. **USART1 RX 不走 HAL 状态机**: 直接寄存器级使能 `USART_CR1_RXNEIE_RXFNEIE`, 自定义 `USART1_IRQ_Handler()` 直接读 `USART1->RDR`。**不要**用 `HAL_UART_Receive_IT` 接管 huart1, 否则会和 fputc 状态机冲突。
23. **STM32H7 USART 寄存器位**: H7 是 FIFO 架构, 用 `USART_CR1_RXNEIE_RXFNEIE` 和 `USART_ISR_RXNE_RXFNE`, 不是 F1/F4 的 `USART_CR1_RXNEIE` / `USART_ISR_RXNE`。错误标志 (ORE/FE/NE/PE) 必须在 ISR 中通过 `ICR` 清除, 否则反复触发中断。
24. **LCD 横屏方向**: `USE_HORIZONTAL = 1`, 横屏 320×240。模式切换时 `LCD_direction(1)` (摄像头) 或 `LCD_direction(3)` (天气, 180° 翻转)。切换后必须 `lv_obj_invalidate(lv_scr_act())` 强制 LVGL 重绘。
25. **LVGL 字体**: 启用了 Montserrat 14/16/20/28 (lv_conf.h)。Montserrat 默认只含 ASCII (0x20–0x7F), 不要在标签里写 `°` 等扩展字符, 用 `C` 代替 `°C`。
26. **显示模式切换的线程安全**: ISR 只调 `DisplayMode_Request()` 设标志, **绝对不**在 ISR 中调用 LVGL API。真正的 `lv_obj_add_flag/clear_flag` + `LCD_direction` 在 `Weather_Task` 主循环里 `Weather_ApplyMode()` 中执行。
