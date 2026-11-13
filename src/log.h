/*
 * log.h - 调试日志（0CRT 兼容）
 *
 * 总开关 SC_LOG_ENABLE 默认 0 = 完全关闭: 不产生 OutputDebugString,
 * 不写文件, 调用点被宏替换为 no-op (gfx/net/组件内的诊断日志一并消失).
 * 开发排查时在 build 中加 /DSC_LOG_ENABLE=1; 如需文件日志再加 /DSC_LOG_FILE.
 * 用法: Log_Init() 启动时调用; Log_Write("fmt", ...) 任意线程可调(内部加锁)
 */
#ifndef SC_LOG_H
#define SC_LOG_H

#include <windows.h>

#ifndef SC_LOG_ENABLE
#define SC_LOG_ENABLE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if SC_LOG_ENABLE
void Log_Init(void);                 /* 初始化内部锁；SC_LOG_FILE 时打开 .log 文件 */
void Log_Close(void);                /* 释放锁；SC_LOG_FILE 时关闭文件 */
void Log_Write(const char *fmt, ...);/* [HH:MM:SS.mmm] 前缀，OutputDebugStringA（及可选文件日志） */
#else
#define Log_Init()    ((void)0)
#define Log_Close()   ((void)0)
#define Log_Write(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* SC_LOG_H */
