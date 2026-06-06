# Keil MDK5 工程配置指南 - AI集成

## 1. 添加 Include Paths

在 Keil Options → C/C++ → Include Paths 中添加:

```
..\AI\Inc
..\Middlewares\ST\AI\Inc
```

## 2. 添加源文件 (Source Groups)

### 新建 Group: "AI"
- `AI/Src/ai_task.c`
- `AI/Src/ai_preprocess.c`
- `AI/Src/network.c`
- `AI/Src/network_data.c`
- `AI/Src/network_data_params.c`

### 在 "Task" Group 中添加:
- `Task/AITask.c`

## 3. 添加库文件

在 Keil Options → Linker → Misc controls 中添加:

```
..\Middlewares\ST\AI\Lib\NetworkRuntime1020_CM7_Keil.lib
```

或者在 Source Group 中直接添加 .lib 文件。

## 4. FPU 配置

Options → Target → Floating Point Hardware → **Use Single Precision**

这对应 ARM Compiler 的 `--fpu=FPv5-SP` 选项，
STM32H723 的 Cortex-M7 内核支持单精度 FPU。

## 5. 优化等级

建议 Options → C/C++ → Optimization → `-O2` 或 `-Otime`
以提升推理性能。

## 6. 链接器 Scatter File

默认配置即可。模型权重 `s_network_weights_array_u64` 是 const 数据，
自动放在 Flash 区域 (~96KB)。

激活缓冲 (activations, ~13KB) 在运行时从堆分配。

## 7. 内存注意事项

- FreeRTOS 堆已从 15KB 增加到 32KB (FreeRTOSConfig.h)
- AXI SRAM (0x24000000) 占用: Camera_Buffer + Canvas_Buffer = ~230KB / 320KB
- Flash 占用增加: 模型权重 ~96KB + runtime library ~50KB
