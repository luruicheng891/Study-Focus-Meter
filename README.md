# AI 学习状态监测仪

基于 STM32H723ZGT6 的多模态边缘 AI 设备，融合摄像头视觉、环境感知、姿态压力检测与 LVGL 触摸交互，实时分析使用者的学习状态。

> 详细架构见 `项目架构.md`，开发笔记见 `开发笔记.md`，AI 模型移植说明见 `AI移植指南.md`。

## 功能模块

- **AI 视觉**：OV2640 (DCMI 8 位) → 240×240 RGB565 → ROI 灰度 32×32 → CubeAI 三分类 (专注 / 分心 / 疲劳)
- **环境监测**：AHT20 温湿度 + BH1750 光照 + MAX9814 麦克风音量
- **姿态/压力 (无线节点)**：ESP32 + MPU6050 + 压力传感器 → BLE (W02 / PB-03F) → USART2 透传到主机
- **网络天气**：ESP-01 (WiFi) → USART3 → cJSON 解析 → LVGL 仪表板
- **GUI**：LVGL v8.3.11 + ILI9341 (320×240 横屏) + XPT2046 触摸
- **运行时模式切换**：USART1 串口或屏幕按钮在「天气仪表板」与「摄像头预览」间切换
- **调试通道**：USART1 (115200, 非阻塞 fputc + 单字节命令)

## 硬件接线

### OV2640 摄像头 (DCMI 8 位 + SCCB)

| 信号 | 引脚 | 备注 |
|------|------|------|
| D0 | PC6 | DCMI_D0 |
| D1 | PC7 | DCMI_D1 |
| D2 | PG10 | DCMI_D2 |
| D3 | PG11 | DCMI_D3 |
| D4 | PE4 | DCMI_D4 |
| D5 | PD3 | DCMI_D5 |
| D6 | PE5 | DCMI_D6 |
| D7 | PE6 | DCMI_D7 |
| VSYNC | PG9 | DCMI_VSYNC |
| HSYNC | PA4 | DCMI_HSYNC |
| PCLK | PA6 | DCMI_PIXCLK |
| SCL | PF14 | SCCB_SCL (软件模拟) |
| SDA | PF15 | SCCB_SDA (软件模拟) |
| PWDN | PF13 | GPIO 输出 |

### ILI9341 LCD (SPI1 + DMA)

| 信号 | 引脚 |
|------|------|
| SCK | PB3 (SPI1_SCK, AF5) |
| MOSI | PB5 (SPI1_MOSI, AF5) |
| CS | PD0 |
| DC/RS | PD1 |
| RST | PD2 |
| LED | PB4 |

### XPT2046 触摸屏

复用 LCD 同一组 SPI，独立 CS。详见 `LCD/touch.c`。

### 软件 I2C 传感器总线 (PB8/PB9)

| 设备 | 7-bit 地址 | 功能 |
|------|-----------|------|
| AHT20 | 0x38 | 温湿度 |
| BH1750FVI | 0x23 | 光照 |

### MAX9814 麦克风 (ADC2 + DMA)

PA7 (ADC2_INP7) → DMA1_Stream2 循环采样到 AXI SRAM 0x24040900 (1KB)。

### UART 链路

| 接口 | 引脚 | 速率 | 用途 |
|------|------|------|------|
| USART1 | PA9 / PA10 | 115200 | 调试 + 命令 (非阻塞 fputc) |
| USART2 | PA2 / PA3 | 115200 | ESP32 BLE 透传 (姿态 / 压力) |
| USART3 | PD8 / PB11 | 115200 | ESP-01 WiFi (天气) |

## 软件结构

```
Core/             freertos.c, main.c, stm32h7xx_it.c
Communication/    I2C.c
                  UART_ESP01.{c,h}   USART3 底层驱动
                  UART_ESP32.{c,h}   USART2 底层驱动
Sensor/           AHT20 / BH1750 / MAX9814
Task/             Camera        DCMI 采集 + canvas
                  SensorTask    本地传感器聚合
                  WeatherTask   天气 RX (USART3) 业务层
                  SlaveRxTask   姿态/压力 RX (USART2) 业务层
                  Display       LVGL 仪表板 + 模式切换
                  AITask        CubeAI 推理
                  TouchTask     XPT2046 + LVGL indev
                  display_mode  模式状态 + USART1 fputc/RX
LCD/              ILI9341 + XPT2046
SYSTEM/           TIM7 LVGL 1ms tick
LVGL/             v8.3.11 + Gui Guider 字体/图标
AI/               CubeAI 模型 + 预处理
Middlewares/ST/AI CubeAI 运行时 (lib + headers)
MDK-ARM/          Keil 工程
```

UART 类业务统一采用 **驱动层 (Communication/) + 业务层 (Task/)** 双文件结构，详见 `项目架构.md` 与 `.kiro/steering/AGENTS.md`。

## 构建

- Keil MDK-ARM V5，ARM Compiler V5
- 工程文件 `MDK-ARM/STM32H723.uvprojx`
- FPU 单精度，MicroLib，C99，`-O1`
- FreeRTOS 堆 40KB

## 注意

- LCD 用 SPI1 而非 SPI4，避免与 DCMI 的 PE5/PE6 冲突
- 摄像头采集分辨率 240×240 (RGB565)，从 OV2640 SVGA 模式裁剪
- 调试串口波特率统一 115200，所有 UART 中断优先级 ≥ 6 (FreeRTOS 安全范围)
- DMA 缓冲必须放 AXI SRAM (DTCM 不可被 DMA 访问)
- 新增 DMA 缓冲从 0x24040E00 起向后排，32 字节对齐，详见 `AGENTS.md`
