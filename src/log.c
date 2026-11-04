/*
 * log.c - 调试日志实现（0CRT 兼容）
 * 输出调试通道: OutputDebugStringA(DebugView 里能看到)
 * 文件日志: 默认关闭；开发构建加 /DSC_LOG_FILE 后写 exe 同目录 .log
 * 注意: crtstub 的 vsprintf 不支持 %02d 宽度格式, 时间戳手动补零
 */
#include <windows.h>
#include <stdarg.h>
#include "log.h"

int vsprintf(char *buf, const char *fmt, va_list ap);
size_t strlen(const char *s);

static CRITICAL_SECTION g_lock;

#ifdef SC_LOG_FILE
static HANDLE g_file = INVALID_HANDLE_VALUE;

/* 找最后一个 '.'（用于替换 .exe → .log） */
static const char *last_dot(const char *p)
{
	const char *dot = NULL;
	while (*p) { if (*p == '.') dot = p; p++; }
	return dot;
}
#endif

void Log_Init(void)
{
	InitializeCriticalSection(&g_lock);
#ifdef SC_LOG_FILE
	char path[MAX_PATH];
	if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
		const char *dot = last_dot(path);
		if (dot) {
			int base = (int)(dot - path);
			path[base] = '\0';
			lstrcatA(path, ".log");
		} else {
			lstrcatA(path, ".log");
		}
		g_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (g_file == INVALID_HANDLE_VALUE) g_file = NULL;
	}
	Log_Write("===== SmartControl log start =====");
#endif
}

void Log_Close(void)
{
#ifdef SC_LOG_FILE
	if (g_file) {
		Log_Write("===== SmartControl log end =====");
		CloseHandle(g_file);
		g_file = NULL;
	}
#endif
	DeleteCriticalSection(&g_lock);
}

void Log_Write(const char *fmt, ...)
{
	if (!fmt) return;
	char buf[1024];
	SYSTEMTIME st;
	GetLocalTime(&st);
	/* 手动补零: HH:MM:SS.mmm */
	buf[0] = '[';
	buf[1] = (char)('0' + st.wHour / 10);
	buf[2] = (char)('0' + st.wHour % 10);
	buf[3] = ':';
	buf[4] = (char)('0' + st.wMinute / 10);
	buf[5] = (char)('0' + st.wMinute % 10);
	buf[6] = ':';
	buf[7] = (char)('0' + st.wSecond / 10);
	buf[8] = (char)('0' + st.wSecond % 10);
	buf[9] = '.';
	buf[10] = (char)('0' + (st.wMilliseconds / 100) % 10);
	buf[11] = (char)('0' + (st.wMilliseconds / 10) % 10);
	buf[12] = (char)('0' + st.wMilliseconds % 10);
	buf[13] = ']';
	buf[14] = ' ';
	int len = 15;

	va_list ap;
	va_start(ap, fmt);
	len += vsprintf(buf + len, fmt, ap);
	va_end(ap);

	if (len > (int)sizeof(buf) - 3) len = (int)sizeof(buf) - 3;
	buf[len++] = '\r';
	buf[len++] = '\n';
	buf[len] = '\0';

	EnterCriticalSection(&g_lock);
#ifdef SC_LOG_FILE
	if (g_file) {
		DWORD written = 0;
		WriteFile(g_file, buf, (DWORD)len, &written, NULL);
	}
#endif
	OutputDebugStringA(buf);
	LeaveCriticalSection(&g_lock);
}
