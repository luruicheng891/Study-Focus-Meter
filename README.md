# OV2640 摄像头采集 + ILI9341 LCD 显示

STM32H723ZGT6 平台，OV2640 通过 DCMI 采集图像，SPI1 驱动 ILI9341 LCD 实时显示。

## 接线说明

### OV2640 摄像头模块 (DCMI 8位)

| 摄像头引脚 | STM32H723 | 功能 |
|-----------|-----------|------|
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
| SCL | PH7 | SCCB_SCL |
| SDA | PH13 | SCCB_SDA |
| PWDN | PF13 | GPIO 输出 |

### ILI9341 LCD 模块 (SPI1)

| LCD 引脚 | STM32H723 | 功能 |
|----------|-----------|------|
| SCK | PB3 | SPI1_SCK (AF5) |
| SDI(MOSI) | PB5 | SPI1_MOSI (AF5) |
| SDO(MISO) | 不接 | 仅写不读 |
| CS | PD0 | GPIO 输出 |
| DC/RS | PD1 | GPIO 输出 |
| RST | PD2 | GPIO 输出 |
| LED | PD3 | GPIO 输出 (背光) |
| VCC | 3.3V | |
| GND | GND | |

### USART1 (调试串口)

| 功能 | STM32H723 |
|------|-----------|
| TX | PA9 |
| RX | PA10 |

## 注意事项

- LCD 使用 SPI1 而非 SPI4，避免与 DCMI 的 PE5/PE6 引脚冲突
- SPI1 时钟频率约 34MHz (APB2 137.5MHz / 4分频)
- 摄像头采集分辨率 240x240 (RGB565)，从 OV2640 SVGA 模式裁剪
- 串口波特率 921600
