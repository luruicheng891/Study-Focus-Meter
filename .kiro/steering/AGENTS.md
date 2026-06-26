---
inclusion: auto
---

# 项目约束与开发规范

## 项目概述

- 产品: AI 学习状态监测仪 (摄像头 + 环境传感器 + 蓝牙姿态/压力)
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
  - SPI1 TX DMA: DMA1_Stream0 (DMAMUX request 38), 中断优先级 (1,0)
  - SPI1 中断: 优先级 (1,1), 用于 DMA 错误处理
- **DCMI (OV2640)**: PE4=D4, PE5=D6, PE6=D7, PA4/PC6=D0, PA6=PIXCLK, PC7=D1, PD3=D5, PG9=VSYNC, PG10=D2, PG11=D3
  - DCMI DMA: DMA2_Stream7, 中断优先级 (0,0)
- **SCCB (OV2640 I2C)**: PF14=SCL, PF15=SDA (软件模拟, 地址 0x60)
- **I2C 传感器总线**: PB8=SCL, PB9=SDA (软件模拟 GPIO bit-bang, 开漏+上拉, ~100kHz)
  - AHT20 温湿度: 地址 0x38
  - BH1750FVI 光照: 地址 0x23 (ADDR=GND)
- **USART1 (调试 + 命令)**: PA9=TX, PA10=RX, 115200 baud
  - TX: 非阻塞环形缓冲 (2KB) + TXE 中断, fputc 重定向 (位于 `Task/display_mode.c`)
  - RX: 单字节 RXNE 中断, 接收 'C'/'W' 命令切换显示模式
  - NVIC 优先级 (6, 0)
- **USART3 (ESP-01 WiFi)**: PD8=TX, PB11=RX, 115200 baud
  - DMA 接收: DMA1_Stream1 (DMAMUX request 45)
  - 中断优先级: USART3=(6,1), DMA1_Stream1=(6,0)
  - DMA 缓冲区: 0x24040800 (AXI SRAM, 256 字节)
  - 驱动: `Communication/UART_ESP01.c`
- **USART2 (ESP32 BLE 透传 W02 / PB-03F)**: PA2=TX, PA3=RX, 115200 baud
  - DMA 接收: DMA1_Stream3 (DMAMUX request USART2_RX)
  - 中断优先级: USART2=(6,1), DMA1_Stream3=(6,0)
  - DMA 缓冲区: 0x24040D00 (AXI SRAM, 256 字节)
  - 驱动: `Communication/UART_ESP32.c`
- **MAX9814 麦克风 (ADC2)**: PA7 (ADC2_INP7), DMA1_Stream2 循环采集
  - DMA 缓冲区: 0x24040900 (AXI SRAM, 1024 字节)
  - 驱动: `Sensor/MAX9814.c` (LL 寄存器实现)
- **TIM7 (LVGL Tick)**: 基本定时器, 1ms 中断, 中断优先级 (3,0), 直接寄存器操作 (未启用 HAL_TIM_MODULE)
- **XPT2046 触摸屏**: 走 LCD 同一 SPI，独立 CS，详见 `LCD/touch.c`
- **LED**: PG7 (低电平点亮)
- **OV2640 PWDN**: PF13

### I2C 总线设备 (PB8/PB9)

| 设备 | 7-bit 地址 | 说明 |
|------|-----------|------|
| AHT20 | 0x38 | 温湿度传感器 |
| BH1750FVI | 0x23 | 光照传感器 (ADDR=GND) |

> 两设备在同一任务中顺序访问，无需互斥锁。
> MPU6050 不在本机 I2C 总线上 (从机端通过 ESP32 + W02/PB-03F BLE 透传)。

### DMA 资源占用总表

| Stream | 用途 | 优先级 |
|--------|------|--------|
| DMA1_Stream0 | SPI1 TX (LCD) | (1, 0) |
| DMA1_Stream1 | USART3 RX (ESP-01 WiFi) | (6, 0) |
| DMA1_Stream2 | ADC2 (MAX9814 麦克风) | LL 配置 |
| DMA1_Stream3 | USART2 RX (ESP32 BLE 透传) | (6, 0) |
| DMA2_Stream7 | DCMI (OV2640 摄像头) | (0, 0) |

### 内存布局 (Scatter File: MDK-ARM/STM32H723/STM32H723.sct)

| 区域 | 起始地址 | 大小 | 用途 | DMA 可访问 |
|------|----------|------|------|-----------|
| Flash | 0x08000000 | 1MB | 代码 + 只读数据 | - |
| DTCM | 0x20000000 | 128KB | 栈、堆、普通变量 | ? 不可 |
| AXI SRAM | 0x24000000 | 320KB | DMA 缓冲区 | ? 可以 |

**AXI SRAM 已用区段:**

| 起始地址 | 结束地址 | 大小 | 用途 |
|----------|----------|------|------|
| 0x24000000 | 0x2401C1FF | 115.2KB | Camera DCMI 帧缓冲 |
| 0x2401C200 | 0x2403FFFF | ~144KB | LVGL canvas / 业务缓冲 |
| 0x24040000 | 0x240407FF | 2KB | LCD SPI ping-pong (2 × 1KB) |
| 0x24040800 | 0x240408FF | 256B | USART3 RX (ESP-01) |
| 0x24040900 | 0x24040CFF | 1KB | MAX9814 ADC 循环采集 |
| 0x24040D00 | 0x24040DFF | 256B | USART2 RX (ESP32 BLE 透传) |

> 新增 DMA 缓冲必须从 0x24040E00 起, 32 字节对齐, 通过 `__attribute__((section(".ARM.__at_..."), aligned(32)))` 强制定位。

### MPU 配置
- Region 0: 0x24000000, 512KB, Full Access, Cacheable+Bufferable+Shareable

## LCD 显示

- **物理面板**: ILI9341, 240×320 原始分辨率
- **使用方向**: 横屏 320×240 (`USE_HORIZONTAL = 1`)
- **运行时方向切换**: 由 `Weather_ApplyMode()` (在 `Task/Display.c` 内) 控制, 切换 `LCD_direction()`
  - 模式 1 (横屏 90°): 摄像头模式
  - 模式 3 (横屏 270°, 即 180° 翻转): 天气仪表板模式
- **LVGL 显示**: `MY_DISP_HOR_RES=320`, `MY_DISP_VER_RES=240`

## 显示模式切换

通过 USART1 接收单字符命令运行时切换:
- `'C'` / `'c'` → `DISP_MODE_CAMERA` (摄像头, 240×240 居中显示)
- `'W'` / `'w'` → `DISP_MODE_WEATHER` (天气仪表板, 320×240 全屏, 180° 翻转)

实现细节:
- `Task/display_mode.c` 提供 `g_display_mode` 全局变量、`DisplayMode_Request/TakePending()` API
- ISR 只设标志位, 真正切换在 `DisplayTask` (`Task/Display.c`) 主循环中调用 `Weather_ApplyMode()`
- DCMI/DMA 始终运行, 通过 `LV_OBJ_FLAG_HIDDEN` 切换 canvas 与 weather panel 可见性
- 切换后 `lv_obj_invalidate(lv_scr_act())` 强制整屏重绘
- LVGL 触摸 indev 由 `Task/TouchTask.c` 注册, 不论哪个模式都能响应屏幕按钮

## 代码结构

```
Core/Src/          - main.c, freertos.c, stm32h7xx_it.c (中断), HAL MSP
Drivers/User/      - led, usart, sccb, dcmi_ov2640 驱动 (注: usart.c 中旧 fputc 已禁用)
LCD/               - lcd.c, lcd_spi.c (SPI1 + DMA), lcd.h, lcd_spi.h, touch.c (XPT2046)
Communication/     - I2C.c       软件模拟 PB8/PB9 总线
                     UART_ESP01  USART3 底层驱动 (ESP-01 WiFi 接收)
                     UART_ESP32  USART2 底层驱动 (ESP32 BLE 透传接收)
Sensor/            - AHT20.c/.h, BH1750.c/.h (软件 I2C 设备)
                     MAX9814.c/.h (麦克风 ADC2 + DMA 循环采集)
Task/              - Camera        DCMI 采集 + LVGL canvas
                     SensorTask    AHT20 + BH1750 + MAX9814 聚合
                     WeatherTask   ESP-01 接收 + cJSON 解析 (业务层)
                     SlaveRxTask   ESP32 BLE 接收 + IMU/压力 JSON 解析 (业务层)
                     FusionTask    多模态融合评分 (视觉40+坐姿30+环境20+时长10), 1s
                     StateTask     学习会话状态机 (IDLE/PICKING/LEARNING/AWAY/PAUSED/SUMMARY)
                     Display       三屏切换编排 (主界面/学习/报告) + 模式切换主循环
                     LearningScreen 学习专注界面 (秒表 + 双环 + 环境卡 + DBG 面板)
                     LearningReport 学习结束报告页 (时长/平均分/占比/次数)
                     Reminder      温和提醒浮层 (持续分心/疲劳/坐姿/45min)
                     ScreenSaver   自动息屏 / 背光管理
                     AITask        CubeAI 推理
                     TouchTask     XPT2046 + LVGL indev 注册 + 校准
                     display_mode  模式切换状态 + 非阻塞 fputc + USART1 RX IRQ
SYSTEM/            - TIM.c, TIM.h (TIM7 基本定时器, 为 LVGL 提供 1ms 心跳)
LVGL/              - LVGL v8.3.11 源码 (启用 Montserrat 14/16/20/28 字体)
LVGL/examples/porting/ - lv_port_disp.c (320×240 横屏配置), lv_port_indev.c (空, indev 由 TouchTask 注册)
Freertos/          - FreeRTOS 内核源码
AI/                - CubeAI 模型 + 预处理 + 推理封装
Middlewares/ST/AI/ - CubeAI 运行时库 (lib + 头文件)
MDK-ARM/           - Keil 工程文件, 启动文件, scatter file
```

## 分层原则 (重要)

UART 类业务统一采用 **驱动层 + 业务层** 双文件结构:

| 接收链路 | 驱动层 (`Communication/`) | 业务层 (`Task/`) |
|----------|--------------------------|------------------|
| ESP-01 WiFi (USART3) | `UART_ESP01.{c,h}` 分帧 | `WeatherTask.{c,h}` 解析+队列+消费 API |
| ESP32 BLE (USART2)   | `UART_ESP32.{c,h}` 分帧 | `SlaveRxTask.{c,h}` 解析+队列+消费 API |

驱动层 API 模板:
```c
void  USARTx_Init(void);
void  USARTx_IRQ_Handler(void);
void  UART_xxx_BindRxTask(TaskHandle_t handle);
int   UART_xxx_TakeFrame(uint8_t *buf, uint16_t max_len, uint16_t *out_len);
void  UART_xxx_GetStats(uint32_t *recv, uint32_t *drop);
/* 可选: 需要向对端下发指令的链路 (如 ESP32) 增加发送 API */
int   UART_xxx_SendBytes(const uint8_t *data, uint16_t len);
int   UART_xxx_SendByte(uint8_t cmd);
```

> `UART_ESP32` 已实现上述发送 API (TX 与 RX DMA 互不干扰); 业务层 `SlaveRxTask` 在其上封装了
> `Slave_SendCmd()` / `Slave_StartReceive()` / `Slave_StopReceive()` / `Slave_Sleep()` 及
> `SLAVE_CMD_START/STOP/SLEEP` ('A'/'Z'/'S') 协议宏。
> 调试: `UART_ESP32.c` / `UART_ESP01.c` 内有 `ESP32_RX_DEBUG` / `ESP01_RX_DEBUG` 开关, 置 1 时
> 每收到一帧即通过 USART1 (printf) 打印原始内容, 用于排查"收不到/解析不到"问题, 排查完置 0。

业务层暴露的消费 API 风格:
```c
int  Xxx_GetLatest(XxxData_t *out);                       /* 非阻塞 peek, 0=有数据 */
int  Xxx_WaitNew(XxxData_t *out, TickType_t timeout);    /* 阻塞 receive */
void Xxx_GetStats(uint32_t *recv, uint32_t *drop, uint32_t *parse_err);
```

新增其它 UART 设备时按此模板复制即可。

## FreeRTOS 任务

| 任务名 | 函数 | 栈大小 | 优先级 | 功能 |
|--------|------|--------|--------|------|
| LVGL_Task | LVGL_Task() | 512 words | AboveNormal | lv_timer_handler() 5ms 周期 |
| Camera_task | Camera_task() | 1024 words | Normal | DCMI 采集 + LVGL canvas (始终运行) |
| Sensor_Task | Sensor_Task() | 512 words | Normal | AHT20 + BH1750 + MAX9814 聚合, 1s 周期 |
| DisplayTask | DisplayTask() | 512 words | Normal | LVGL UI 渲染 + 模式切换 (100ms 轮询) |
| Weather_RxTask | Weather_RxTask() | 512 words | Normal | ESP-01 USART3 帧 → cJSON → 队列 |
| Slave_RxTask | Slave_RxTask() | 512 words | Normal | ESP32 USART2 帧 → cJSON → 队列 |
| AI_Infer | AI_InferTask() | 3072 words | BelowNormal | CubeAI 推理 |
| Touch_Task | Touch_Task() | 512 words | Normal | XPT2046 扫描 + LVGL indev + 校准命令 |
| Fusion_Task | Fusion_Task() | 1024 words | Low | 多模态融合评分, 1s 周期 (视觉40+坐姿30+环境20+时长10) |
| State_Task | State_Task() | 512 words | Normal | 学习会话状态机, 200ms 周期 (IDLE/PICKING/LEARNING/AWAY/PAUSED/SUMMARY) |

> `LearningScreen` / `LearningReport` / `Reminder` / `ScreenSaver` 是纯 UI/逻辑模块, 无独立任务, 由 `DisplayTask` 主循环驱动。

## 界面流程与数据流 (主界面 → 学习状态 → 学习结束)

`DisplayTask` 按 `State_GetCurrent()` 在三块屏幕间切换 (统一 `LCD_direction(3)`):

| 状态 | 屏幕 | 说明 |
|------|------|------|
| `ST_IDLE` | 天气仪表板 (`g_weather_screen`) | **主界面**, 控制条 "Start" 入口 |
| `ST_PICKING` | 学习界面 + 时长 Picker | 选 5~120min 目标时长 |
| `ST_LEARNING` / `ST_PAUSED` / `ST_AWAY` | 学习界面 (`LearningScreen`) | **学习状态**, 实时监测 (PAUSED/AWAY 秒表冻结) |
| `ST_SUMMARY` | 学习报告 (`LearningReport`) | **学习结束**, 统计页 |

事件驱动: GUI 按钮/Picker 经 `State_PostEvent()` 投递 `STATE_EVT_GUI_*`; `StateTask`
内部轮询压力/计时投递 `PRESSURE_LOST/BACK`、`AWAY_TIMEOUT`、`TIME_COMPLETE`。

端到端数据流:
```
AITask(视觉) / Sensor_Task(温湿光声) / Slave_RxTask(姿态压力) / Weather_RxTask(天气)
   │ (各自非阻塞 GetLatest / 队列 peek)
   ▼
Fusion_Task (1s)  视觉40%+坐姿30%+环境20%+时长10% → FusionResult_t (长度1队列)
   │ Fusion_GetLatest                            │ Slave_GetLatest
   ▼                                              ▼
State_Task (200ms)  事件处理 + 压力监测 + 倒计时 + 累加 SessionSummary_t
   │ 状态切换时 USART2 向从机发单字符指令 A/Z/S (确认时长进入LEARNING→A, 暂停/结束→Z, 休眠预留S)
   ▼ State_GetCurrent / State_GetSummary / State_GetElapsedSeconds
DisplayTask (100ms)  切屏 + LearningScreen_Update* + 进入SUMMARY→LearningReport_Update
                     每帧 Reminder_Poll + ScreenSaver_Tick
```

关键阈值 (StateTask 宏): 压力阈值 5.0; 压力消失 30s → AWAY; AWAY 5min → 自动结束(auto=1);
时长达成 → 自动结束(auto=2); 手动 End → auto=0。

主机 → 从机控制指令 (USART2, **单字符协议**, 由 `SlaveRxTask` 业务层 `Slave_SendCmd()` 封装,
底层 `UART_ESP32_SendByte()` 阻塞发送):
- `'A'` (`SLAVE_CMD_START`): 开始采集/上报。在**确认时长进入 LEARNING** 时发送 (PICKING→LEARNING,
  以及 PAUSED/AWAY 回到 LEARNING 时重发)。
- `'Z'` (`SLAVE_CMD_STOP`): 停止采集/上报。在**暂停 (PAUSED)** 与**学习结束 (SUMMARY)** 时发送。
- `'S'` (`SLAVE_CMD_SLEEP`): 休眠, 预留接口 (`Slave_Sleep()`), 触发时机由上层决定。

在位校验时机 (重要): 点 Start **不**校验是否有人, 直接进入 PICKING 选时长; 真正的"椅子上有没有人"
判断放在**确认时长 (CONFIRM_DURATION)** 时——压力 > 5.0 或无 BLE 数据(调试模式)才进入 LEARNING,
否则提示 "Please sit on chair" 并退回 IDLE。CONFIRM_DURATION 还会应用 Picker 选中的分钟数到
`s_target_minutes`。

姿态评分 (FusionTask `score_posture`): 综合从机 `score` + `state` + `pressure` 三项——压力 < 5.0
判离座直接 0 分; `state` 含 absent/away→0, distract→×0.6, fatigue/tired→×0.5, 其余用原分;
结果计入总分的坐姿 30% 权重。

## 数据流

### 本地传感器 (Sensor_Task, 1s 周期)
```
AHT20 (温湿度) ─┐
BH1750 (光照) ─┼─→ SensorData_t ─→ xQueueOverwrite(xSensorDataQueue) ─→ DisplayTask
MAX9814 (音量) ─┘
```

### 远程天气 (Weather_RxTask, ESP-01 / USART3)
```
ESP-01 → USART3 RX (PB11) → DMA1_Stream1 → AXI SRAM 0x24040800 (256B)
   └── IDLE 中断 → SCB_InvalidateDCache → 任务通知
        └── UART_ESP01_TakeFrame() → cJSON 解析 → WeatherInfo_t
             └── 内部队列 (长度 1, xQueueOverwrite)
                  └── DisplayTask 通过 Weather_GetLatest() 获取
```

### 从机数据 (Slave_RxTask, ESP32 BLE / USART2)
```
ESP32 + MPU6050 + 压力 → BLE 广播 → W02/PB-03F → USART2 RX (PA3)
   └── DMA1_Stream3 → AXI SRAM 0x24040D00 (256B)
        └── IDLE 中断 → SCB_InvalidateDCache → 任务通知
             └── UART_ESP32_TakeFrame() → cJSON 解析 → SlaveData_t
                  └── 内部队列 (长度 1, xQueueOverwrite)
                       └── 通过 Slave_GetLatest() / Slave_WaitNew() 获取
```

### USART1 命令通道
```
PC 串口助手 → PA10 → USART1 RXNE 中断
   └── 'C'/'W' → DisplayMode_Request() (设置 pending flag)
        └── DisplayTask 主循环 → Weather_ApplyMode()
             ├── LCD_direction(1 或 3)
             ├── 切换 canvas / weather_panel hidden flag
             └── lv_obj_invalidate(lv_scr_act())
```

### 关键数据结构

```c
/* 本地环境数据 (Sensor_Task → DisplayTask) */
typedef struct {
    int32_t  temperature;     // 0.01°C (2500 = 25.00°C)
    uint32_t humidity;        // 0.01%  (5678 = 56.78%)
    uint32_t lux_x100;        // 0.01 lx
    uint16_t soundIntensity;  // 0~100 麦克风音量百分比
    uint32_t timestamp;       // FreeRTOS tick
} SensorData_t;

/* 远程天气 (WeatherTask) */
typedef struct {
    char     city[20];
    int16_t  temp_x10;     // 0.1°C
    uint8_t  humi;
    int16_t  feel_x10;     // 0.1°C 体感
    char     cond[32];
    uint16_t wind_x10;     // 0.1 m/s
    char     wdir[4];
    uint16_t pres;         // hPa
    char     time[8];
    uint32_t timestamp;
} WeatherInfo_t;

/* 从机姿态 + 压力 (SlaveRxTask, 新 JSON 协议)
 * {"ts":..,"state":"focused","score":85,"pressure":45.2} */
typedef struct {
    uint64_t ts;                 // ESP32 端毫秒时间戳
    char     state[16];          // 姿态状态字符串 ("focused"/"distracted"/...)
    int32_t  score;              // 姿态评分 0..100
    float    pressure;           // 座椅/应变压力值
    uint32_t local_tick;         // 本机接收时刻
    uint32_t seq;                // 累计帧序号
} SlaveData_t;

/* 多模态融合结果 (FusionTask, 1s 周期, 长度1队列) */
typedef struct {
    uint8_t  total_score;        // 加权总分 0..100
    uint8_t  vision_score, posture_score, env_score, duration_score;
    uint8_t  env_temp_score, env_humi_score, env_lux_score, env_noise_score;
    float    vision_prob_focus, vision_prob_distract, vision_prob_fatigue;
    char     posture_state[16];  float pressure;
    int32_t  temperature_c100;   uint32_t humidity_x100, lux_x100;
    uint16_t sound_pct;          uint32_t study_minutes;
    uint32_t flags;              char advice[64];
    uint32_t timestamp, seq;
} FusionResult_t;

/* 学习会话统计 (StateTask 结束时填好 → LearningReport) */
typedef struct {
    uint32_t total_seconds;          // 实际学习时长
    uint16_t target_seconds;         // 目标时长 (0=无目标)
    uint8_t  avg_total_score;        // 平均总分
    uint8_t  focus_pct, distract_pct, fatigue_pct;
    uint8_t  posture_abnormal_count; // 坐姿异常次数 (边沿计数)
    uint32_t away_count, pause_count;
    uint8_t  auto_ended;             // 0=手动 1=5min离开 2=时长达成
} SessionSummary_t;                  // == LearningReport 的 LRSessionData_t
```

## 编码规范

- 语言: C99
- 编译器选项: `--c99 --cpu Cortex-M7.fp.dp -D__MICROLIB -g -O1 --apcs=interwork --split_sections`
- 使用 MicroLib (轻量 C 库)
- HAL 库风格: 使用 `HAL_xxx` API, 不直接操作寄存器 (除非性能关键路径或 H7 特有 FIFO 位)
- 命名: 模块前缀 (LCD_, OV2640_, AHT20_, BH1750_, I2C_, SCCB_, TIM7_, UART_ESP01_, UART_ESP32_, Weather_, Slave_, Touch_, Display_, Fusion_, State_, LearningScreen_, LearningReport_, Reminder_, ScreenSaver_)
- 注释: 中文注释为主, 函数头用 Doxygen 风格
- 文件统一 UTF-8 编码 (避免 GBK 在多平台下乱码)

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
10. **传感器独立初始化**: AHT20 / BH1750 / MAX9814 各自初始化, 任一不在线不影响另一个工作。
11. **FreeRTOS 任务**: 新增任务注意栈大小和优先级。当前 8 个任务 (含 LVGL/AI) 共用 ~40KB FreeRTOS 堆。
12. **Keil 工程**: 修改源文件列表需编辑 `.uvprojx` XML; Include Path 包含 `../Sensor`, `../Task`, `../Communication`, `../LCD`, `../AI/Inc`, `../Middlewares/ST/AI/Inc`。
13. **中断优先级**: DCMI/DMA2_Stream7 (0,x) > SPI/DMA1_Stream0 (1,x) > TIM7 (3,0) > 所有 UART/USART 相关 (6,x)。
14. **TIM7 LVGL 心跳**: PSC=274, ARR=999, 1ms 中断。`TIM7_IRQHandler` 在 `SYSTEM/TIM.c` 中定义。
15. **MPU6050 在从机端**: 通过 ESP32 + W02/PB-03F BLE 模块 UART 透传到主机, 不占用本机 I2C 总线。主机用 `Slave_RxTask` 接收 JSON。
16. **UART DMA 缓冲区位置**: 必须在 AXI SRAM (0x24040800 / 0x24040D00 等), 不能在 DTCM, 否则 DMA 接收无效, buffer 始终为 0。使用 `__attribute__((section(".ARM.__at_<addr>"), aligned(32)))` 强制定位。
17. **UART IDLE 中断**: HAL_UART_Receive_DMA() 后必须手动 `__HAL_UART_ENABLE_IT(&huartx, UART_IT_IDLE)`。每帧 IDLE 触发后用 `HAL_UART_DMAStop` + `HAL_UART_Receive_DMA` 重启重置 DMA 计数器。
18. **DMA RX DCache 失效**: AXI SRAM 配置为 Cacheable, DMA 写入后必须 `SCB_InvalidateDCache_by_Addr()` 才能让 CPU 读到新数据。
19. **cJSON FreeRTOS 适配**: main.c 中 `cJSON_InitHooks()` 注册 `pvPortMalloc/vPortFree`, 使解析使用 FreeRTOS 堆 (heap_4)。
20. **中断优先级与 FreeRTOS**: `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`, 调用 FromISR API 的中断必须 ≥5 (数值越大优先级越低)。建议用 6 留出余量。
21. **USART1 printf 必须非阻塞**: 旧版 `HAL_UART_Transmit` 阻塞实现在多任务并发 printf 时会卡死 (`huart1->gState` 状态机被打断后无法恢复)。`Task/display_mode.c` 中重写了 fputc 为环形缓冲 + TXE 中断驱动。`Drivers/User/Src/usart.c` 里旧 fputc 已 `#if 0` 注释禁用。新代码若需要修改 fputc, 改 `display_mode.c`, 不要恢复 usart.c 里的旧版本。
22. **USART1 RX 不走 HAL 状态机**: 直接寄存器级使能 `USART_CR1_RXNEIE_RXFNEIE`, 自定义 `USART1_IRQ_Handler()` 直接读 `USART1->RDR`。**不要**用 `HAL_UART_Receive_IT` 接管 huart1, 否则会和 fputc 状态机冲突。
23. **STM32H7 USART 寄存器位**: H7 是 FIFO 架构, 用 `USART_CR1_RXNEIE_RXFNEIE` 和 `USART_ISR_RXNE_RXFNE`, 不是 F1/F4 的 `USART_CR1_RXNEIE` / `USART_ISR_RXNE`。错误标志 (ORE/FE/NE/PE) 必须在 ISR 中通过 `ICR` 清除, 否则反复触发中断。
24. **LCD 横屏方向**: `USE_HORIZONTAL = 1`, 横屏 320×240。模式切换时 `LCD_direction(1)` (摄像头) 或 `LCD_direction(3)` (天气, 180° 翻转)。切换后必须 `lv_obj_invalidate(lv_scr_act())` 强制 LVGL 重绘。
25. **LVGL 字体**: 启用了 Montserrat 14/16/20/28 (lv_conf.h)。Montserrat 默认只含 ASCII (0x20–0x7F), 不要在标签里写 `°` 等扩展字符, 用 `C` 代替 `°C`。中文用 Gui Guider 导出的 SourceHanSerif 字体。
26. **显示模式切换的线程安全**: ISR 只调 `DisplayMode_Request()` 设标志, **绝对不**在 ISR 中调用 LVGL API。真正的 `lv_obj_add_flag/clear_flag` + `LCD_direction` 在 `DisplayTask` 主循环里 `Weather_ApplyMode()` 中执行。
27. **UART 驱动业务分层**: 接收类 UART (USART2/USART3) 拆成驱动层 (`Communication/UART_ESP*`) 和业务层 (`Task/*RxTask`)。驱动只做分帧, 业务做协议解析。新增链路按此模板。
28. **DMA 缓冲区不可冲突**: 新增 DMA 必须挑空闲 stream (Stream0/1/2/3 已占用), 缓冲区从 0x24040E00 起向后排, 32 字节对齐, 同步更新本文件 AXI SRAM 表。
29. **触摸坐标 180° 翻转**: 屏幕物理坐标和 LVGL 视觉坐标方向相反, `Task/TouchTask.c` 的 `lvgl_touchpad_read_cb` 内做 `x' = W-1-x, y' = H-1-y` 反向。新增屏幕方向时同步检查。
30. **任务命名风格**: Task 文件以 `<功能名>Task.{c,h}` 命名 (例: `WeatherTask.c`、`SlaveRxTask.c`、`TouchTask.c`); 任务函数前缀与文件对应 (例: `Weather_RxTask`、`Slave_RxTask`、`Touch_Task`)。`DisplayTask` / `Camera_task` 是历史遗留命名, 新模块按上述新规范走。
