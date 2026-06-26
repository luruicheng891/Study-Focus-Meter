# AI 学习状态监测仪

基于 STM32H723ZGT6 的多模态边缘 AI 设备，融合摄像头视觉、环境感知、姿态压力检测与 LVGL 触摸交互，实时分析使用者的学习状态。

> 详细架构见 `项目架构.md`，开发笔记见 `开发笔记.md`，AI 模型移植说明见 `AI移植指南.md`。

## 功能模块

- **AI 视觉**：OV2640 (DCMI 8 位) → 240×240 RGB565 → ROI 灰度 32×32 → CubeAI 三分类 (专注 / 分心 / 疲劳)
- **环境监测**：AHT20 温湿度 + BH1750 光照 + MAX9814 麦克风音量
- **姿态/压力 (无线节点)**：ESP32 + MPU6050 + 压力传感器 → BLE (W02 / PB-03F) → USART2 透传到主机
- **网络天气**：ESP-01 (WiFi) → USART3 → cJSON 解析 → LVGL 仪表板
- **多模态融合**：视觉 40% + 坐姿 30% + 环境 20% + 时长 10% → 0~100 学习质量评分 (FusionTask, 1s 周期)
- **学习会话状态机**：IDLE / PICKING / LEARNING / AWAY / PAUSED / SUMMARY 六态 (StateTask)
- **三屏交互**：主界面 (天气仪表板) → 学习界面 (LearningScreen) → 学习报告 (LearningReport)
- **温和提醒 + 自动息屏**：Reminder 浮层提示 + ScreenSaver 背光管理
- **GUI**：LVGL v8.3.11 + ILI9341 (320×240 横屏) + XPT2046 触摸
- **运行时模式切换**：USART1 串口或屏幕按钮在「天气仪表板」与「摄像头预览」间切换
- **调试通道**：USART1 (115200, 非阻塞 fputc + 单字节命令)

## 界面流程与数据流 (主界面 → 学习状态 → 学习结束)

```
[主界面 天气仪表板/IDLE] --点Start--> [PICKING 选时长] --确认--> [学习界面 LEARNING]
        ▲                                                              │
        │ 点Back                              暂停/离开/压力恢复 在学习界面内切换
        │                                                              │
   [学习报告 SUMMARY] <--- End / 时长达成 / 离开5min超时 --------------- ┘
```

数据流核心:

- 感知层 (持续运行): `AITask` (视觉概率) / `Sensor_Task` (温湿光声) / `Slave_RxTask` (姿态/压力) / `Weather_RxTask` (天气)
- `Fusion_Task` (1s) 拉取上述数据 → 加权 `FusionResult_t` (total_score + 子分 + advice)
- `State_Task` (200ms) 处理 GUI/超时事件、监测压力、累加 `SessionSummary_t`,状态切换时通过 USART2 向从机发送单字符指令 `A`(开始采集) / `Z`(停止采集) / `S`(休眠,预留)
- `DisplayTask` (100ms) 按 `State_GetCurrent()` 切屏并实时刷新,进入 SUMMARY 时读 `State_GetSummary()` 推给学习报告

> 完整状态转移图、时序与数据结构见 `项目架构.md` 的「界面流程与数据流」一节。

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
                  FusionTask    多模态融合评分 (视觉40+坐姿30+环境20+时长10)
                  StateTask     学习会话状态机 (IDLE/PICKING/LEARNING/AWAY/PAUSED/SUMMARY)
                  Display       三屏切换编排 (主界面/学习/报告) + 模式切换
                  LearningScreen 学习专注界面 (秒表 + 双环 + 环境卡 + DBG 面板)
                  LearningReport 学习结束报告页 (时长/平均分/占比/次数)
                  Reminder      温和提醒浮层 (持续分心/疲劳/坐姿/45min)
                  ScreenSaver   自动息屏 / 背光管理
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
