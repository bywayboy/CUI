/*
 * log.h - 调试日志（0CRT 兼容）
 * OutputDebugStringA 始终开启；文件日志仅在 SC_LOG_FILE 宏开启时启用
 * 用法: Log_Init() 应用启动时调用; Log_Write("fmt", ...) 任意线程可调(内部加锁)
 */
#ifndef SC_LOG_H
#define SC_LOG_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void Log_Init(void);                 /* 初始化内部锁；SC_LOG_FILE 时打开 .log 文件 */
void Log_Close(void);                /* 释放锁；SC_LOG_FILE 时关闭文件 */
void Log_Write(const char *fmt, ...);/* [HH:MM:SS.mmm] 前缀，OutputDebugStringA（及可选文件日志） */

#ifdef __cplusplus
}
#endif

#endif /* SC_LOG_H */
