/*
 * ui_timer.h - GUI 线程定时器 (最小堆 + waitable timer)
 *
 * 回调在 GUI 线程执行, 无需锁。UiApp 内嵌一份 (app->timer) 并已接入消息循环;
 * 自定义消息循环的用法:
 *
 *   HANDLE h = UiTimer_WaitHandle(tm);          // 无任务返回 NULL
 *   MsgWaitForMultipleObjectsEx(h ? 1 : 0, &h, INFINITE, QS_ALLINPUT,
 *                               MWMO_INPUTAVAILABLE);   // 或 MWMO_ALERTABLE
 *   UiTimer_Dispatch(tm);
 *
 * 动画期间定时器按帧量化 (帧间隔通常 ≤ 8ms), 空闲时由 waitable timer 精确唤醒。
 */
#ifndef CUI_UI_TIMER_H
#define CUI_UI_TIMER_H

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiTimer UiTimer;
typedef void (*UiTimerFn)(void *user, uint64_t id);

UiTimer *UiTimer_Create(void);                 /* GUI 线程 */
void     UiTimer_Destroy(UiTimer *tm);         /* 之后不再回调 */

uint64_t UiTimer_After(UiTimer *tm, int ms, UiTimerFn fn, void *user);
uint64_t UiTimer_Every(UiTimer *tm, int ms, UiTimerFn fn, void *user);
int      UiTimer_Cancel(UiTimer *tm, uint64_t id);  /* 1=已撤销 */

HANDLE UiTimer_WaitHandle(UiTimer *tm);
int    UiTimer_Dispatch(UiTimer *tm);   /* 跑到期回调; 返回下个剩余 ms, -1=无任务 */

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_TIMER_H */
