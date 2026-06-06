# STM32 CubeAI 模型移植指南

> 本文档记录将 STM32Cube.AI 生成的神经网络模型移植到现有 FreeRTOS + LVGL + OV2640 项目的完整流程。
> 适用于 STM32H723 + Keil MDK5 (ARMCC v5) 环境。

---

## 一、前置条件

| 项目 | 说明 |
|------|------|
| 目标工程 | 已有 FreeRTOS + DCMI摄像头 + LVGL 显示的完整工程 |
| AI模型工程 | 由 STM32Cube.AI (X-CUBE-AI) 生成的验证工程 |
| 模型输入 | 32×32×1 灰度 float32，归一化 [0,1] |
| 模型输出 | 3 类 float32 概率 |
| 图像源 | OV2640 输出 240×240 RGB565，DCMI+DMA 采集 |

---

## 二、需要从 CubeAI 工程复制的文件

### 2.1 模型网络文件（必须复制）

从 CubeAI 生成工程的 `Src/` 和 `Inc/` 目录：

```
源工程/Inc/network.h           → 目标工程/AI/Inc/network.h
源工程/Inc/network_config.h    → 目标工程/AI/Inc/network_config.h
源工程/Inc/network_data.h      → 目标工程/AI/Inc/network_data.h
源工程/Inc/network_data_params.h → 目标工程/AI/Inc/network_data_params.h

源工程/Src/network.c           → 目标工程/AI/Src/network.c
源工程/Src/network_data.c      → 目标工程/AI/Src/network_data.c       (含权重数组)
源工程/Src/network_data_params.c → 目标工程/AI/Src/network_data_params.c (含激活表)
```

> **注意**: `network_data_params.c` 文件可能很大（包含模型权重数组），这是正常的。

### 2.2 CubeAI 运行时库（必须复制）

```
源工程/Middlewares/ST/AI/Inc/   → 目标工程/Middlewares/ST/AI/Inc/   (约93个头文件)
源工程/Middlewares/ST/AI/Lib/   → 目标工程/Middlewares/ST/AI/Lib/   (.lib 文件)
```

库文件命名格式: `NetworkRuntime{版本}_CM7_Keil.lib`

### 2.3 不需要复制的文件

- `源工程/Src/main.c` — 验证用的 main，不需要
- `源工程/Src/stm32h7xx_hal_msp.c` — 硬件初始化，目标工程自己有
- `源工程/Src/system_stm32h7xx.c` — 系统时钟，目标工程自己有
- `源工程/Drivers/` — HAL 驱动，目标工程自己有

---

## 三、需要自己编写的文件

### 3.1 模型配置头文件 `AI/Inc/model_config.h`

定义模型输入输出参数和标签字符串：

```c
#define AI_MODEL_INPUT_WIDTH       32
#define AI_MODEL_INPUT_HEIGHT      32
#define AI_MODEL_INPUT_CHANNELS    1
#define AI_MODEL_INPUT_ELEMENTS    (32 * 32 * 1)

#define AI_MODEL_OUTPUT_CLASSES    3
#define AI_MODEL_OUTPUT_ELEMENTS   3

#define AI_LABEL_FOCUSED           0
#define AI_LABEL_DISTRACTED        1
#define AI_LABEL_FATIGUED          2
```

### 3.2 推理封装 `AI/Src/ai_task.c` + `AI/Inc/ai_task.h`

关键要点：
- **静态分配激活缓冲**，不依赖 malloc
- 初始化时手动设置 `g_network_activations_table[1]`
- 使用 `AI_NETWORK_DATA_ACTIVATIONS_TABLE_GET()` 和 `AI_NETWORK_DATA_WEIGHTS_TABLE_GET()` 作为参数

```c
// 静态分配激活缓冲 (大小见 network_data_params.h 中的宏)
AI_ALIGNED(4)
static ai_u8 s_ai_activations[AI_NETWORK_DATA_ACTIVATION_1_SIZE];

int AI_Task_Init(void)
{
    // 注册静态缓冲到 activations table
    g_network_activations_table[1] = AI_HANDLE_PTR(s_ai_activations);

    // 创建并初始化网络
    err = ai_network_create_and_init(&ai_net,
              AI_NETWORK_DATA_ACTIVATIONS_TABLE_GET(),
              AI_NETWORK_DATA_WEIGHTS_TABLE_GET());
    ...
}
```

### 3.3 图像预处理 `AI/Src/ai_preprocess.c` + `AI/Inc/ai_preprocess.h`

RGB565 → 灰度 → 降采样 → float32 归一化：
- 取中央 224×224 区域 (偏移 8 像素)
- 7×7 块平均得到 32×32
- BT.601 灰度公式: `Y = (77*R + 150*G + 29*B) >> 8`
- 输出直接归一化到 [0, 1.0]

### 3.4 FreeRTOS 任务 `Task/AITask.c` + `Task/AITask.h`

- 启动后延迟 2 秒等摄像头就绪
- 用 `ulTaskNotifyTake()` 等待 Camera_task 的帧通知
- 限频 500ms 一次推理 (2fps)
- 结果通过 printf 串口打印 + LVGL 标签显示

---

## 四、Keil 工程配置修改

### 4.1 Include Paths

添加两条路径：
```
..\AI\Inc
..\Middlewares\ST\AI\Inc
```

### 4.2 Source Files

在工程中添加：
- `AI/Src/ai_task.c`
- `AI/Src/ai_preprocess.c`
- `AI/Src/network.c`
- `AI/Src/network_data.c`
- `AI/Src/network_data_params.c`
- `Task/AITask.c`

### 4.3 Library

添加库文件（FileType=4）：
```
..\Middlewares\ST\AI\Lib\NetworkRuntime1020_CM7_Keil.lib
```

### 4.4 FPU 设置

Options → Target → Floating Point Hardware → **Use Single Precision**

---

## 五、FreeRTOS 配置修改 (`FreeRTOSConfig.h`)

```c
#define configENABLE_FPU          1       // 必须开启, AI用大量float
#define configTOTAL_HEAP_SIZE     ((size_t)40960)  // 40KB, 原15KB不够
```

堆空间计算：
- AI 任务栈: 3072 × 4 = 12KB
- 其他任务栈: 已有约 14KB
- LVGL/队列等: 约 8KB
- 总计需 ~34KB，设 40KB 留余量

---

## 六、现有代码修改

### 6.1 Camera.c — 添加帧通知

```c
#include "AITask.h"

// 在 memcpy 完成后添加:
if (AI_TaskHandle != NULL) {
    xTaskNotifyGive(AI_TaskHandle);
}
```

### 6.2 freertos.c — 创建 AI 任务

```c
#include "AITask.h"

// 在 MX_FREERTOS_Init() 末尾添加:
xTaskCreate(AI_InferTask, "AI_Infer", 3072, NULL, osPriorityBelowNormal, &AI_TaskHandle);
```

---

## 七、常见问题排查

| 现象 | 原因 | 解决 |
|------|------|------|
| 链接报 Undefined symbol | .c 文件没加入工程 | 在 Keil 中添加源文件 |
| `ai_network_params` 无 `weights` 字段 | CubeAI API 版本不同 | 用 `TABLE_GET()` 宏方式初始化 |
| `lv_font_xxx` 未定义 | lv_conf.h 中未启用该字号 | 改用已启用的字体 |
| 模型初始化失败 | 激活缓冲未分配 | 静态分配 + 注册到 table |
| 任务不运行/死机 | 栈溢出 or 堆不够 | 增大 stack size 和 HEAP |
| 串口无输出 | 任务在初始化阶段崩溃 | 加延迟, 加诊断 printf |
| 推理结果不对 | 预处理不匹配训练时格式 | 确认灰度公式/归一化/尺寸一致 |

---

## 八、内存布局参考 (STM32H723)

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| Flash | 0x08000000 | ~250KB 已用 | 代码 + 模型权重(~96KB const) |
| DTCM RAM | 0x20000000 | 128KB | FreeRTOS 堆 + 全局变量 + 栈 |
| AXI SRAM | 0x24000000 | 320KB | Camera_Buffer(115KB) + Canvas_Buffer(115KB) |
| 静态 BSS | — | ~18KB | 激活缓冲(13KB) + 预处理缓冲(5KB) |

---

## 九、文件目录结构 (最终)

```
STM32-OV2640-AI/
├── AI/
│   ├── Inc/
│   │   ├── ai_preprocess.h          ← 自己写
│   │   ├── ai_task.h                ← 自己写
│   │   ├── model_config.h           ← 自己写/从源工程复制
│   │   ├── network.h                ← 从 CubeAI 工程复制
│   │   ├── network_config.h         ← 从 CubeAI 工程复制
│   │   ├── network_data.h           ← 从 CubeAI 工程复制
│   │   └── network_data_params.h    ← 从 CubeAI 工程复制
│   └── Src/
│       ├── ai_preprocess.c          ← 自己写
│       ├── ai_task.c                ← 自己写
│       ├── network.c                ← 从 CubeAI 工程复制
│       ├── network_data.c           ← 从 CubeAI 工程复制 (含权重)
│       └── network_data_params.c    ← 从 CubeAI 工程复制
├── Middlewares/
│   └── ST/AI/
│       ├── Inc/                     ← 从 CubeAI 工程复制 (93个.h)
│       └── Lib/
│           └── NetworkRuntime1020_CM7_Keil.lib  ← 从 CubeAI 工程复制
├── Task/
│   ├── AITask.c                     ← 自己写
│   ├── AITask.h                     ← 自己写
│   ├── Camera.c                     ← 修改: 添加帧通知
│   └── ...
├── Core/
│   ├── Inc/FreeRTOSConfig.h         ← 修改: FPU + 堆大小
│   └── Src/freertos.c              ← 修改: 创建AI任务
└── MDK-ARM/
    └── STM32H723.uvprojx           ← 修改: 添加文件+路径+库
```

---

## 十、移植检查清单

- [ ] 复制 `network*.c` 和 `network*.h` 到 `AI/` 目录
- [ ] 复制 `Middlewares/ST/AI/Inc/` 所有头文件
- [ ] 复制 `Middlewares/ST/AI/Lib/*.lib` 库文件
- [ ] 编写 `model_config.h` (输入尺寸/标签)
- [ ] 编写 `ai_task.c` (静态激活缓冲 + 初始化 + 推理)
- [ ] 编写 `ai_preprocess.c` (图像格式转换)
- [ ] 编写 `AITask.c` (FreeRTOS 任务逻辑)
- [ ] Keil 添加 Include Paths
- [ ] Keil 添加所有 .c 源文件
- [ ] Keil 添加 .lib 库文件
- [ ] 开启 FPU (Target 设置)
- [ ] 增大 FreeRTOS 堆 (≥40KB)
- [ ] 增大 AI 任务栈 (≥3072 words)
- [ ] Camera.c 添加 xTaskNotifyGive
- [ ] freertos.c 添加 xTaskCreate
- [ ] 编译通过，串口确认 `[AI] Task started`
- [ ] 确认推理结果输出
