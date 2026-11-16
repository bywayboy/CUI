/*
 * ui_app.h - 可选的高层窗口壳
 *
 * 接管 HWND / Gfx / UiTheme / UiContext / UiIme / UiFrameClock、消息循环、
 * DPI、鼠标/键盘/IME、动画节拍与关闭流程。应用只提供回调:
 *
 *   init       : Gfx/Theme/Context/Ime/Frame 就绪后调用 (建 UiInput/Toast 状态)
 *   frame      : 一帧内绘制 (Ui_BeginFrame/EndFrame 已由壳处理)
 *   after_frame: Gfx_End 之后 (同步 GfxOverlay / 执行模态动作)
 *   tick       : 每次动画帧调用; 壳已调用 Ui_AnimAdvance, 应用只更新业务状态并
 *                通过 UiApp_Invalidate* 失效; 不要自己调 Ui_AnimAdvance/TakeDirty
 *   animating  : 应用自己的持续动画 (进度循环/Toast), 壳还会检查 Ui_AnimBusy
 *   mouse/key/chr/ime/timer/nchittest/shutdown : 可选输入与生命周期钩子
 *
 * 低层 API (UiContext/Gfx/WndProc) 完全保留; 非常规窗口仍可手写消息循环。
 */
#ifndef CUI_UI_APP_H
#define CUI_UI_APP_H

#include <windows.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_ime.h"
#include "ui/ui_frame.h"
#include "ui/ui_timer.h"

typedef struct UiApp UiApp;

enum {
	UI_APP_MOUSE_MOVE = 0,
	UI_APP_MOUSE_DOWN,
	UI_APP_MOUSE_UP,
	UI_APP_MOUSE_LEAVE,
	/* 右键只转给 App (上下文菜单), 不进入 UiContext 命中/点击. */
	UI_APP_MOUSE_RDOWN,
	UI_APP_MOUSE_RUP
};

typedef struct UiAppCallbacks {
	void (*init)(UiApp *app, void *user);
	void (*frame)(UiApp *app, void *user);
	void (*after_frame)(UiApp *app, void *user);
	void (*shutdown)(UiApp *app, void *user);
	int  (*animating)(UiApp *app, void *user);
	void (*tick)(UiApp *app, double dtMs, void *user);
	void (*mouse)(UiApp *app, int msg, float x, float y, void *user);
	/* delta 为滚轮格数 (+ = 向上); x/y 已是 DIP 客户区坐标. */
	void (*wheel)(UiApp *app, float x, float y, float delta, void *user);
	int  (*key)(UiApp *app, UINT vk, void *user);
	int  (*chr)(UiApp *app, wchar_t ch, void *user);
	void (*ime)(UiApp *app, void *user);
	void (*timer)(UiApp *app, void *user);
	/* x/y 是客户区物理像素; 返回 1 表示 *hit 有效 */
	int  (*nchittest)(UiApp *app, int x, int y, LRESULT *hit, void *user);
	/* 文件拖放 (WM_DROPFILES): path 是 UTF-16 绝对路径, 每次一个文件,
	 * 拖入多个时按顺序回调. 返回 1 = 已处理, 后续文件不再回调.
	 * 设置了本回调才会向系统注册窗口接受拖放. */
	int  (*dropfiles)(UiApp *app, const wchar_t *path, void *user);
} UiAppCallbacks;

struct UiApp {
	HWND hwnd;
	Gfx *gfx;
	UiTheme theme;
	UiContext ui;
	UiIme ime;
	UiFrameClock clock;
	UiTimer *timer;          /* GUI 定时器 (已接入本壳消息循环) */
	UiThemeId themeId;
	float scale;
	float w, h;              /* 客户区 DIP */
	float minW, minH;        /* 最小窗口尺寸 (DIP), 默认 320x240 */
	int minimized;
	int fullRedraw;
	int tracking;
	void *user;
	const UiAppCallbacks *cb;
};

/* 阻塞运行直到窗口关闭; 返回进程退出码。title 为 UTF-16。 */
int  UiApp_Run(const wchar_t *title, int widthDip, int heightDip,
	UiThemeId theme, const UiAppCallbacks *cb, void *user);

/* 整窗失效 (下一帧强制 dirtyFull) */
void UiApp_Invalidate(UiApp *app);

/* DIP 矩形 + pad 失效; 自动乘 DPI scale */
void UiApp_InvalidateRect(UiApp *app, UiRect rc, float pad);

void UiApp_Close(UiApp *app);

#endif /* CUI_UI_APP_H */
