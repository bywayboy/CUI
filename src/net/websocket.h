#ifndef SC_WS_H
#define SC_WS_H

#include <windows.h>

/* 跨线程消息（WS 线程 PostMessage 到 UI 线程） */
#define WM_APP_WS_OPEN  (WM_APP + 1)  /* 连接建立   wParam=0 */
#define WM_APP_WS_CLOSE (WM_APP + 2)  /* 连接断开   wParam=0 */
#define WM_APP_WS_MSG   (WM_APP + 3)  /* 文本消息   lParam=cJSON*（UI 线程负责 cJSON_Delete） */

/* 启动 WS 后台线程（PostMessage 目标 hwnd）。返回 1 成功 */
int WS_Start(HWND hwnd);

/* 停止线程（等待退出） */
void WS_Stop(void);

/* 发送文本帧（主线程调用，CRITICAL_SECTION 保护）。返回 1 成功 0 失败（丢弃） */
int WS_Send(const char *data, int len);

#endif /* SC_WS_H */
