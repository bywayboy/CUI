/*
 * ui_frame.h - 动画帧时钟 (跟随显示器刷新)
 *
 *   WM_TIMER 的粒度 ~15.6ms 且会被消息队列合并, 做 60/120Hz 动画会抖。
 *   这里优先用合成器时钟 (DCompositionWaitForCompositorClock) 对齐 vsync,
 *   不可用时退回高精度 waitable timer (Win10 1803+), 再退到 1ms 粒度定时器。
 *
 * 用法 (主循环, 动画期间):
 *   double t0 = UiFrame_Now();
 *   App_Advance(UiFrame_Tick(clock));
 *   InvalidateRect()+UpdateWindow();          // Present 自己挡到 vsync
 *   UiFrame_Delay(clock, periodMs - (Now()-t0));   // 只补剩余时间
 */
#ifndef SCV2_UI_FRAME_H
#define SCV2_UI_FRAME_H

#include <windows.h>

typedef struct {
	HANDLE   timer;
	void    *compWait;        /* DCompositionWaitForCompositorClock (动态取) */
	int      useComp;
	int      refreshHz;
	double   periodMs;
	double   nextMs;
	double   lastMs;
	int      frames;
	int      dropped;         /* 帧间隔 > 1.5×周期 */
	double   sumMs;
	double   maxMs;
	int      hiRes;           /* 高精度 waitable timer 创建成功 */
	double   renderSum, renderMax;            /* 渲染耗时 */
	double   paintPx;                         /* 累计实际绘制像素 */
	int      fullFrames;                      /* 其中整窗绘制的帧数 */
} UiFrameClock;

void   UiFrame_Init(UiFrameClock *c, HWND hwnd);
void   UiFrame_Shutdown(UiFrameClock *c);
double UiFrame_Now(void);

/* 记录帧起点并返回 dt (ms, 夹到 0..100) */
double UiFrame_Tick(UiFrameClock *c);

/* 补足剩余节拍 (Present 已经按 vsync 挡过时不需要); 期间消息仍可到达 */
void   UiFrame_Delay(UiFrameClock *c, double ms);

/* 一批动画开始/结束时各调一次: 避免拿上一批的旧时间算 dt */
void   UiFrame_ResetTick(UiFrameClock *c);
void   UiFrame_AddPaint(UiFrameClock *c, double px, int full);
void   UiFrame_LogStats(UiFrameClock *c);

#endif /* SCV2_UI_FRAME_H */
