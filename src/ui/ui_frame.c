#include "ui/ui_frame.h"
#include "log.h"
#include <windows.h>

/* 1ms 系统计时粒度: 高精度 waitable timer 在部分机器上仍按 15.6ms 取整,
 * 动画期间临时打开, 退出时撤销 (installer 只活几秒). */
__declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
__declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int);

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef HRESULT(WINAPI *FnCompWait)(UINT32 count, const HANDLE *handles, DWORD timeoutMs);

static LARGE_INTEGER g_freq;

double UiFrame_Now(void)
{
	LARGE_INTEGER c;
	if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)g_freq.QuadPart;
}

/* 取窗口所在显示器的刷新率; 拿不到就按 60 */
static int detect_refresh(HWND hwnd)
{
	MONITORINFOEXW mi;
	DEVMODEW dm;
	HMONITOR mon;
	int hz = 60;
	mon = MonitorFromWindow(hwnd ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(mon, (MONITORINFO *)&mi)) {
		ZeroMemory(&dm, sizeof(dm));
		dm.dmSize = sizeof(dm);
		if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
			if (dm.dmDisplayFrequency > 1 && dm.dmDisplayFrequency <= 500)
				hz = (int)dm.dmDisplayFrequency;
		}
	}
	if (hz < 24) hz = 24;
	if (hz > 240) hz = 240;
	return hz;
}

void UiFrame_Init(UiFrameClock *c, HWND hwnd)
{
	HMODULE dcomp;
	ZeroMemory(c, sizeof(*c));
	g_freq.QuadPart = 0;
	UiFrame_Now();                       /* 初始化频率 */

	c->refreshHz = detect_refresh(hwnd);
	c->periodMs = 1000.0 / (double)c->refreshHz;

	c->timer = CreateWaitableTimerExW(NULL, NULL,
		CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	c->hiRes = c->timer ? 1 : 0;
	if (!c->timer) {
		/* 老系统 (无 1803 高精度标志): 普通可等待定时器, 粒度差一档但能用 */
		c->timer = CreateWaitableTimerW(NULL, FALSE, NULL);
	}

	dcomp = GetModuleHandleW(L"dcomp.dll");
	if (!dcomp) dcomp = LoadLibraryW(L"dcomp.dll");
	if (dcomp) c->compWait = (void *)GetProcAddress(dcomp, "DCompositionWaitForCompositorClock");
	c->useComp = c->compWait ? 1 : 0;
	Log_Write("[ui] dcomp=%d proc=%d", dcomp ? 1 : 0, c->compWait ? 1 : 0);

	c->lastMs = UiFrame_Now();
	c->nextMs = c->lastMs;
	timeBeginPeriod(1);
	/* 0CRT 的 vsprintf 只认 %d/%u/%s/%x: 小数一律换成整数微秒 */
	Log_Write("[ui] 帧时钟 %d Hz (%d us/帧) compositor=%d hiRes=%d",
		c->refreshHz, (int)(c->periodMs * 1000.0), c->useComp, c->hiRes);
}

void UiFrame_Shutdown(UiFrameClock *c)
{
	if (!c) return;
	if (c->timer) { CloseHandle(c->timer); c->timer = NULL; }
	timeEndPeriod(1);
}

void UiFrame_ResetTick(UiFrameClock *c)
{
	if (!c) return;
	c->lastMs = UiFrame_Now();
	c->nextMs = c->lastMs;
	timeBeginPeriod(1);
	c->frames = 0;
	c->dropped = 0;
	c->sumMs = 0.0;
	c->maxMs = 0.0;
	c->renderSum = c->renderMax = 0.0;
	c->paintPx = 0.0;
	c->fullFrames = 0;
}

void UiFrame_AddPaint(UiFrameClock *c, double px, int full)
{
	if (!c) return;
	if (px > 0.0) c->paintPx += px;
	if (full) c->fullFrames++;
}

void UiFrame_Delay(UiFrameClock *c, double ms)
{
	if (!c || ms <= 0.5) return;
	if (c->timer) {
		LARGE_INTEGER due;
		due.QuadPart = -(LONGLONG)(ms * 10000.0);
		if (SetWaitableTimer(c->timer, &due, 0, NULL, NULL, FALSE)) {
			MsgWaitForMultipleObjectsEx(1, &c->timer, (DWORD)(ms + 4.0),
				QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
			return;
		}
	}
	/* alertable: 让网络层 APC 在动画期间也能归位 */
	SleepEx((DWORD)ms, TRUE);
}

double UiFrame_Tick(UiFrameClock *c)
{
	double now, dt;
	if (!c) return 0.0;
	now = UiFrame_Now();
	dt = now - c->lastMs;
	c->lastMs = now;
	if (dt < 0.0) dt = 0.0;
	if (dt > 100.0) dt = 100.0;
	c->frames++;
	c->sumMs += dt;
	if (dt > c->maxMs) c->maxMs = dt;
	if (dt > c->periodMs * 1.5) c->dropped++;
	return dt;
}

void UiFrame_LogStats(UiFrameClock *c)
{
	if (!c || c->frames <= 0) return;
	Log_Write("[ui] 动画帧 %d, 掉帧 %d (超 %d us), 平均 %d us, 最大 %d us, 渲染 %d us, %d Hz",
		c->frames, c->dropped, (int)(c->periodMs * 1500.0),
		(int)(c->sumMs / (double)c->frames * 1000.0),
		(int)(c->maxMs * 1000.0),
		(int)(c->renderSum / (double)c->frames * 1000.0), c->refreshHz);
	Log_Write("[ui] 绘制面积 平均 %d 千像素/帧, 整窗 %d/%d 帧",
		(int)(c->paintPx / (double)c->frames / 1000.0), c->fullFrames, c->frames);
}
