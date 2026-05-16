/* FreeRTOSConfig.h */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* 基本配置 */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      480000000  // H723 最高主频
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                128
#define configTOTAL_HEAP_SIZE                   32768
#define configMAX_TASK_NAME_LEN                 16

/* 可选功能 */
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* 钩子函数 */
#define configUSE_MALLOC_FAILED_HOOK            0

/* 运行时计数器 */
#define configGENERATE_RUN_TIME_STATS           0

/* 协程支持 */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

/* 软件定时器 */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* 中断优先级配置 (Cortex-M7) */
#define configKERNEL_INTERRUPT_PRIORITY         15
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    5

/* 断言 */
#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* 包含 CMSIS RTOS V2 头文件 */
#define configUSE_CMSIS_RTOS_V2                 1

#endif /* FREERTOS_CONFIG_H */