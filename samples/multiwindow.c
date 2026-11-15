/*
 * multiwindow.c - 多窗口示例 (低层 API)
 *
 * 每个窗口拥有独立的:
 *   Gfx / UiTheme / UiContext / UiProgress / UiToast
 * 共享一个 UiFrameClock 与消息循环。
 *
 * 演示点:
 *   - 两个窗口不同主题 (亮/暗), 主题独立切换
 *   - 两个独立进度 overlay, 各自 6 秒循环
 *   - 每个窗口独立 Toast, 在一个窗口弹出不会阻塞另一个窗口
 *   - 主窗口打开/聚焦二级窗口; 关闭主窗口会一并退出
 *
 * 这个样例刻意不使用 UiApp (UiApp 是单窗口壳), 展示低层多 HWND 接线。
 */
#include <windows.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_frame.h"
#include "ui/ui_layout.h"
#include "ui/ui_widgets.h"
#include "ui/ui_progress.h"
#include "ui/ui_toast.h"
#include "log.h"

#define MW_COUNT 3
#define MW_MAIN_W 640
#define MW_MAIN_H 430
#define MW_SEC_W  480
#define MW_SEC_H  340
#define MW_DLG_W  420
#define MW_DLG_H  300

typedef struct MWin {
	HWND hwnd;
	Gfx *gfx;
	UiTheme theme;
	UiContext ui;
	UiProgress progress;
	UiToast toast;
	float scale;
	float w, h;
	int id;
	int isDialog;
	HWND ownerHwnd;
	int wantW, wantH;
	int full;
	int pending;      /* 1=主题 2=通知 3=关闭 4=打开/聚焦二级 */
	char status[128];
} MWin;

static MWin g_win[MW_COUNT];
static UiFrameClock g_clock;

static int Mw_CreateSecondary(void);
static int Mw_OpenDialog(MWin *owner);

static void win_set_status(MWin *w, const char *s)
{
	lstrcpynA(w->status, s, (int)sizeof(w->status));
}

static void win_invalidate_rect(MWin *w, UiRect r, float pad)
{
	RECT rc;
	float s;
	if (!w || !w->hwnd || r.w <= 0.0f || r.h <= 0.0f) return;
	s = w->scale > 0.0f ? w->scale : 1.0f;
	rc.left = (LONG)((r.x - pad) * s);
	rc.top = (LONG)((r.y - pad) * s);
	rc.right = (LONG)((r.x + r.w + pad) * s + 1.0f);
	rc.bottom = (LONG)((r.y + r.h + pad) * s + 1.0f);
	InvalidateRect(w->hwnd, &rc, FALSE);
}

static void win_apply_dpi(MWin *w)
{
	UINT dpi = 96;
	HMODULE u32;
	typedef UINT(WINAPI *FnGetDpiForWindow)(HWND);
	FnGetDpiForWindow fn;
	if (!w || !w->hwnd) return;
	u32 = GetModuleHandleW(L"user32.dll");
	fn = u32 ? (FnGetDpiForWindow)GetProcAddress(u32, "GetDpiForWindow") : NULL;
	if (fn) {
		UINT d = fn(w->hwnd);
		if (d > 0) dpi = d;
	}
	w->scale = (float)dpi / 96.0f;
}

static void win_place(MWin *w, int widthDip, int heightDip)
{
	RECT wa;
	int wpx, hpx, x, y;
	if (!w || !w->hwnd) return;
	wpx = (int)(widthDip * w->scale + 0.5f);
	hpx = (int)(heightDip * w->scale + 0.5f);
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
	x = wa.left + (wa.right - wa.left - wpx) / 2;
	y = wa.top + (wa.bottom - wa.top - hpx) / 2;
	if (w->id) {
		x += (int)(70.0f * w->scale);
		y += (int)(50.0f * w->scale);
		if (x + wpx > wa.right) x = wa.right - wpx;
		if (y + hpx > wa.bottom) y = wa.bottom - hpx;
	}
	SetWindowPos(w->hwnd, NULL, x, y, wpx, hpx,
		SWP_NOZORDER | SWP_NOACTIVATE);
}

static void win_tick(MWin *w, double dtMs, double nowMs)
{
	int redraw, visible;
	if (!w || !w->hwnd || !w->gfx) return;
	if (w->isDialog) return;
	UiProgress_Tick(&w->progress, dtMs);
	redraw = UiToast_Tick(&w->toast, nowMs);
	if (!UiToast_UsingLayer(&w->toast)) {
		visible = UiToast_IsVisible(&w->toast);
		UiProgress_SetHidden(&w->progress, visible);
		if (visible || redraw) InvalidateRect(w->hwnd, NULL, FALSE);
	}
	if (!UiToast_IsVisible(&w->toast) &&
		UiProgress_NeedsMainRedraw(&w->progress) &&
		w->progress.rc.w > 0.0f)
		win_invalidate_rect(w, w->progress.rc, 2.0f);
}

static int win_animating(MWin *w)
{
	if (!w || !w->hwnd) return 0;
	if (w->isDialog) return Ui_AnimBusy(&w->ui);
	return Ui_AnimBusy(&w->ui) || !UiProgress_Paused(&w->progress) ||
		UiToast_IsVisible(&w->toast);
}

static void win_draw_dialog(MWin *w)
{
	UiTheme *t = &w->theme;
	UiContext *ui = &w->ui;
	Gfx *g = w->gfx;
	UiRect card;
	UiStack col;
	UiRect line1, line2, rowRc, statusRc;
	UiRect buttons[4] = { { 0, 0, 0, 0 } };

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, w->w, w->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, w->w - 1.0f, w->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_FillRoundRectGradV(g, Ui_Rect(20, 20, 4, 24), 2.0f,
		t->blue2, t->blue);
	Gfx_Text(g, t->fBig, "CUI Modal Dialog", Ui_Rect(32, 16, w->w - 90.0f, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "模态窗口 · 父窗口已禁用",
		Ui_Rect(30, 44, w->w - 60.0f, 20), t->text3, GFX_TEXT_VCENTER);
	if (Ui_Button(ui, Ui_Id("mw.dlgclose", 0, 0),
			Ui_Rect(w->w - 42.0f, 14.0f, 28.0f, 28.0f), "×",
			UI_BTN_SECONDARY, 1))
		w->pending = 3;

	card = Ui_Rect(20, 76, w->w - 40.0f, w->h - 96.0f);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 18, 16, 18, 16);
	line1 = Ui_StackTake(&col, 20.0f);
	Ui_StackSkip(&col, 6.0f);
	line2 = Ui_StackTake(&col, 18.0f);
	Ui_StackSkip(&col, 16.0f);
	rowRc = Ui_StackTake(&col, 38.0f);
	Ui_StackSkip(&col, 12.0f);
	statusRc = Ui_StackTake(&col, 20.0f);

	Gfx_Text(g, t->fBody, "父窗口已禁用；关闭对话框后恢复。",
		line1, t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "全局主题会同步到主窗和二级窗。",
		line2, t->hint, GFX_TEXT_VCENTER);

	{
		UiFixedSize fx[4] = { { 110, 0 }, { 90, 0 }, { 90, 0 }, { 0, 0 } };
		UiMeasureItem items[4] = {
			Ui_FixedItem(&fx[0], 0.0f), Ui_FixedItem(&fx[1], 0.0f),
			Ui_FixedItem(&fx[2], 0.0f), Ui_FixedItem(&fx[3], 1.0f) };
		if (!Ui_FlexLayout(rowRc, UI_ROW, 12.0f, items, 4, buttons)) {
			for (int i = 0; i < 4; i++) buttons[i] = Ui_Rect(0, 0, 0, 0);
		}
	}
	if (Ui_Button(ui, Ui_Id("mw.dlgtheme", 0, 0), buttons[0],
			w->theme.themeId == UI_THEME_LIGHT ? "深色主题" : "浅色主题",
			UI_BTN_SECONDARY, 1))
		w->pending = 1;
	if (Ui_Button(ui, Ui_Id("mw.dlgok", 0, 0), buttons[1],
			"确定", UI_BTN_PRIMARY, 1))
		w->pending = 3;
	if (Ui_Button(ui, Ui_Id("mw.dlgcancel", 0, 0), buttons[2],
			"取消", UI_BTN_SECONDARY, 1))
		w->pending = 3;
	Gfx_Text(g, t->fBody, w->status, statusRc, t->text2, GFX_TEXT_VCENTER);
}

static void win_draw(MWin *w)
{
	UiTheme *t = &w->theme;
	UiContext *ui = &w->ui;
	Gfx *g = w->gfx;
	UiRect card;
	UiStack col;
	UiRect line1, line2, rowRc, progLabel, barRc, statusRc;
	UiRect buttons[6] = { { 0, 0, 0, 0 } };
	const char *title = w->id == 0 ? "CUI Multi Window" : "CUI Window B";
	const char *sub = w->id == 0
		? "两个窗口 · 共享全局主题 · 独立进度 / Toast"
		: "全局主题 · 独立进度与 Toast";

	if (w->isDialog) {
		win_draw_dialog(w);
		return;
	}

	/* 窗口背景 + 标题 */
	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, w->w, w->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, w->w - 1.0f, w->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_FillRoundRectGradV(g, Ui_Rect(22, 22, 4, 26), 2.0f,
		t->blue2, t->blue);
	Gfx_Text(g, t->fBig, title, Ui_Rect(34, 18, w->w - 100.0f, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, sub, Ui_Rect(32, 48, w->w - 60.0f, 20),
		t->text3, GFX_TEXT_VCENTER);

	if (Ui_Button(ui, Ui_Id("mw.close", w->id, 0),
			Ui_Rect(w->w - 42.0f, 16.0f, 28.0f, 28.0f), "×",
			UI_BTN_SECONDARY, 1))
		w->pending = 3;

	card = Ui_Rect(20, 82, w->w - 40.0f, w->h - 102.0f);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 18, 18, 18, 18);

	line1 = Ui_StackTake(&col, 20.0f);
	Ui_StackSkip(&col, 6.0f);
	line2 = Ui_StackTake(&col, 18.0f);
	Ui_StackSkip(&col, w->id == 0 ? 20.0f : 18.0f);
	rowRc = Ui_StackTake(&col, w->id == 0 ? 38.0f : 36.0f);
	Ui_StackSkip(&col, w->id == 0 ? 22.0f : 18.0f);
	progLabel = Ui_StackTake(&col, 18.0f);
	Ui_StackSkip(&col, 6.0f);
	barRc = Ui_StackTake(&col, 10.0f);
	Ui_StackSkip(&col, w->id == 0 ? 18.0f : 14.0f);
	statusRc = Ui_StackTake(&col, 22.0f);

	Gfx_Text(g, t->fBody,
		w->id == 0 ? "主题是全局的；进度、Toast、鼠标和焦点仍各自独立。"
			: "主题切换会同步到所有窗口；进度和 Toast 仍只影响自己。",
		line1, t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall,
		w->id == 0 ? "主窗口关闭时会一并销毁二级窗口。"
			: "关闭本窗口不会影响主窗口。",
		line2, t->hint, GFX_TEXT_VCENTER);

	if (w->id == 0) {
		UiFixedSize fx[6] = {
			{ 100, 0 }, { 100, 0 }, { 100, 0 },
			{ 80, 0 }, { 0, 0 }, { 70, 0 } };
		UiMeasureItem items[6] = {
			Ui_FixedItem(&fx[0], 0.0f), Ui_FixedItem(&fx[1], 0.0f),
			Ui_FixedItem(&fx[2], 0.0f), Ui_FixedItem(&fx[3], 0.0f),
			Ui_FixedItem(&fx[4], 1.0f), Ui_FixedItem(&fx[5], 0.0f) };
		if (!Ui_FlexLayout(rowRc, UI_ROW, 12.0f, items, 6, buttons)) {
			for (int i = 0; i < 6; i++) buttons[i] = Ui_Rect(0, 0, 0, 0);
		}
		if (Ui_Button(ui, Ui_Id("mw.second", 0, 0), buttons[0],
				g_win[1].hwnd ? "聚焦二级" : "打开二级", UI_BTN_PRIMARY, 1))
			w->pending = 4;
		if (Ui_Button(ui, Ui_Id("mw.dialog", 0, 0), buttons[1],
				"模态框", UI_BTN_SECONDARY, 1))
			w->pending = 5;
		if (Ui_Button(ui, Ui_Id("mw.theme", 0, 0), buttons[2],
				w->theme.themeId == UI_THEME_LIGHT ? "深色主题" : "浅色主题",
				UI_BTN_SECONDARY, 1))
			w->pending = 1;
		if (Ui_Button(ui, Ui_Id("mw.toast", 0, 0), buttons[3],
				"通知", UI_BTN_SECONDARY, 1))
			w->pending = 2;
		if (Ui_Button(ui, Ui_Id("mw.quit", 0, 0), buttons[5],
				"退出", UI_BTN_DANGER, 1))
			w->pending = 3;
	} else {
		UiFixedSize fx[4] = { { 110, 0 }, { 90, 0 }, { 0, 0 }, { 80, 0 } };
		UiMeasureItem items[4] = {
			Ui_FixedItem(&fx[0], 0.0f), Ui_FixedItem(&fx[1], 0.0f),
			Ui_FixedItem(&fx[2], 1.0f), Ui_FixedItem(&fx[3], 0.0f) };
		if (!Ui_FlexLayout(rowRc, UI_ROW, 12.0f, items, 4, buttons)) {
			for (int i = 0; i < 4; i++) buttons[i] = Ui_Rect(0, 0, 0, 0);
		}
		if (Ui_Button(ui, Ui_Id("mw.theme", 1, 0), buttons[0],
				w->theme.themeId == UI_THEME_LIGHT ? "深色主题" : "浅色主题",
				UI_BTN_SECONDARY, 1))
			w->pending = 1;
		if (Ui_Button(ui, Ui_Id("mw.toast", 1, 0), buttons[1],
				"通知", UI_BTN_SECONDARY, 1))
			w->pending = 2;
		if (Ui_Button(ui, Ui_Id("mw.close2", 1, 0), buttons[3],
				"关闭", UI_BTN_DANGER, 1))
			w->pending = 3;
	}

	Gfx_Text(g, t->fLabel, "进度 (独立合成器 overlay)",
		progLabel, t->text2, GFX_TEXT_VCENTER);
	UiProgress_Draw(&w->progress, barRc);
	Gfx_Text(g, t->fBody, w->status, statusRc, t->text2, GFX_TEXT_VCENTER);

	UiToast_Draw(ui, &w->toast, UiFrame_Now(), w->w, w->h);
}

static void win_after_frame(MWin *w)
{
	double now = UiFrame_Now();
	if (w->pending) {
		int act = w->pending;
		w->pending = 0;
		if (act == 1) {
			UiThemeId next = w->theme.themeId == UI_THEME_LIGHT
				? UI_THEME_DARK : UI_THEME_LIGHT;
			UiTheme_SetTheme(&w->theme, next);   /* 全局: 所有窗口一起换肤 */
			w->full = 1;
			win_set_status(w, w->isDialog ? "全局主题已切换"
				: (w->id == 0 ? "全局主题已切换 (主窗口)" : "全局主题已切换 (二级窗口)"));
			InvalidateRect(w->hwnd, NULL, FALSE);
		} else if (act == 2 && !w->isDialog) {
			UiToast_Show(&w->toast,
				w->id == 0 ? "来自主窗口的通知" : "来自二级窗口的通知",
				now, 1800, UI_TOAST_SUCCESS);
			if (!UiToast_UsingLayer(&w->toast)) {
				UiProgress_SetHidden(&w->progress, 1);
				InvalidateRect(w->hwnd, NULL, FALSE);
			}
		} else if (act == 4 && w->id == 0) {
			if (g_win[1].hwnd) {
				SetForegroundWindow(g_win[1].hwnd);
			} else {
				Mw_CreateSecondary();
			}
		} else if (act == 5 && w->id == 0) {
			Mw_OpenDialog(w);
		} else if (act == 3) {
			PostMessageW(w->hwnd, WM_CLOSE, 0, 0);
		}
	}
	if (!w->isDialog) {
		UiToast_EndFrame(&w->toast, now, w->w, w->h);
		UiProgress_EndFrame(&w->progress);
	}
}

static void win_render(MWin *w, const RECT *dirty)
{
	RECT rc;
	if (!w || !w->hwnd || !w->gfx) return;
	GetClientRect(w->hwnd, &rc);
	if (rc.right <= 0 || rc.bottom <= 0) return;
	Gfx_Resize(w->gfx, (int)rc.right, (int)rc.bottom, w->scale);
	w->w = (float)rc.right / w->scale;
	w->h = (float)rc.bottom / w->scale;
	Ui_SetViewport(&w->ui, w->w, w->h, w->scale);

	Gfx_SetDirtyRect(w->gfx, w->full ? NULL : dirty);
	Gfx_Begin(w->gfx);
	Gfx_Clear(w->gfx, 0x00000000);
	Ui_BeginFrame(&w->ui);
	win_draw(w);
	Ui_EndFrame(&w->ui);
	Gfx_End(w->gfx);
	w->full = 0;
	win_after_frame(w);
}

static void win_shutdown(MWin *w)
{
	if (!w) return;
	UiToast_Shutdown(&w->toast);
	UiProgress_Shutdown(&w->progress);
	if (w->gfx) {
		UiTheme_Shutdown(&w->theme, w->gfx);
		Gfx_Shutdown(w->gfx);
		w->gfx = NULL;
	}
	w->hwnd = NULL;
}

static void win_init(MWin *w)
{
	ZeroMemory(&w->progress, sizeof(w->progress));
	ZeroMemory(&w->toast, sizeof(w->toast));
	w->scale = 1.0f;
	w->pending = 0;
	w->full = 1;
	Gfx_Init(&w->gfx, w->hwnd);
	if (!w->gfx) return;
	UiTheme_Init(&w->theme, w->gfx);   /* 自动继承当前全局主题 */
	Ui_ContextInit(&w->ui, w->hwnd, w->gfx, &w->theme);
	if (!w->isDialog) {
		UiProgress_Init(&w->progress, &w->ui, w->id ? 4500 : 6000);
		UiToast_Init(&w->toast);
		UiToast_Attach(&w->toast, &w->ui);
	}
	win_set_status(w, w->isDialog ? "模态窗口 · 父窗口已禁用"
		: (w->id == 0 ? "主窗口 · 独立 UiContext / 进度 / Toast"
			: "二级窗口 · 独立进度 / Toast"));
}

static LRESULT CALLBACK MultiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	MWin *w = (MWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	switch (msg) {
	case WM_NCCREATE:
		w = (MWin *)((CREATESTRUCTW *)lp)->lpCreateParams;
		if (w) {
			w->hwnd = hwnd;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
		}
		/* 让默认处理保存 CREATESTRUCT 的窗口标题; 自己 return TRUE 会丢标题. */
		DefWindowProcW(hwnd, msg, wp, lp);
		return TRUE;
	case WM_CREATE:
		if (!w) return -1;
		win_init(w);
		return w->gfx ? 0 : -1;
	case WM_SIZE:
		if (w) {
			w->full = 1;
			InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		EndPaint(hwnd, &ps);
		if (w) win_render(w, &ps.rcPaint);
		return 0;
	}
	case WM_NCHITTEST: {
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		RECT rc;
		if (!w) break;
		ScreenToClient(hwnd, &pt);
		GetClientRect(hwnd, &rc);
		if (pt.y >= 0 && pt.y < (int)(58.0f * w->scale) &&
			pt.x >= 0 && pt.x < rc.right - (int)(46.0f * w->scale))
			return HTCAPTION;
		return HTCLIENT;
	}
	case WM_MOUSEMOVE:
		if (w) {
			Ui_OnMouseMove(&w->ui, (short)LOWORD(lp) / w->scale,
				(short)HIWORD(lp) / w->scale);
		}
		return 0;
	case WM_LBUTTONDOWN:
		if (w) {
			SetCapture(hwnd);
			SetFocus(hwnd);
			Ui_OnMouseDown(&w->ui, (short)LOWORD(lp) / w->scale,
				(short)HIWORD(lp) / w->scale);
		}
		return 0;
	case WM_LBUTTONUP:
		if (w) {
			Ui_OnMouseUp(&w->ui, (short)LOWORD(lp) / w->scale,
				(short)HIWORD(lp) / w->scale);
			if (UiToast_IsVisible(&w->toast))
				UiToast_Hide(&w->toast, UiFrame_Now());
			ReleaseCapture();
		}
		return 0;
	case WM_CAPTURECHANGED:
		if (w) Ui_OnCaptureChanged(&w->ui);
		return 0;
	case WM_KEYDOWN:
		if (w && wp == VK_ESCAPE) {
			if (w->isDialog) {
				PostMessageW(hwnd, WM_CLOSE, 0, 0);
				return 0;
			}
			if (UiToast_IsVisible(&w->toast)) {
				UiToast_Hide(&w->toast, UiFrame_Now());
				return 0;
			}
		}
		return 0;
	case WM_DPICHANGED: {
		const RECT *sugg = (const RECT *)lp;
		if (w) {
			win_apply_dpi(w);
			SetWindowPos(hwnd, NULL, sugg->left, sugg->top,
				sugg->right - sugg->left, sugg->bottom - sugg->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			w->full = 1;
			InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	}
	case WM_CLOSE:
		if (w && w->id == 0 && g_win[2].hwnd)
			DestroyWindow(g_win[2].hwnd);
		if (w && w->id == 0 && g_win[1].hwnd)
			DestroyWindow(g_win[1].hwnd);
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		if (w && w->isDialog) {
			int i;
			for (i = 0; i < 2; i++)
				if (g_win[i].hwnd) EnableWindow(g_win[i].hwnd, TRUE);
			if (g_win[0].hwnd) {
				win_set_status(&g_win[0], "模态对话框已关闭");
				InvalidateRect(g_win[0].hwnd, NULL, FALSE);
				SetForegroundWindow(g_win[0].hwnd);
			}
		}
		if (w) win_shutdown(w);
		if (!g_win[0].hwnd && !g_win[1].hwnd && !g_win[2].hwnd)
			PostQuitMessage(0);
		return 0;
	case WM_NCDESTROY:
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static int Mw_CreateSecondary(void)
{
	MWin *w = &g_win[1];
	HWND hwnd;
	if (w->hwnd) {
		SetForegroundWindow(w->hwnd);
		return 1;
	}
	w->id = 1;
	w->isDialog = 0;
	w->ownerHwnd = NULL;
	w->wantW = MW_SEC_W;
	w->wantH = MW_SEC_H;
	hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
		L"CuiMultiWindow", L"CUI Window B", WS_POPUP,
		0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), w);
	if (!hwnd) return 0;
	win_apply_dpi(w);
	win_place(w, MW_SEC_W, MW_SEC_H);
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	return 1;
}

static int Mw_OpenDialog(MWin *owner)
{
	MWin *w = &g_win[2];
	RECT or, wa;
	HWND hwnd;
	int wpx, hpx, x, y, i;

	if (w->hwnd) {
		SetForegroundWindow(w->hwnd);
		return 1;
	}
	w->id = 2;
	w->isDialog = 1;
	w->ownerHwnd = owner ? owner->hwnd : g_win[0].hwnd;
	w->wantW = MW_DLG_W;
	w->wantH = MW_DLG_H;
	hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
		L"CuiMultiWindow", L"CUI Modal Dialog", WS_POPUP,
		0, 0, 0, 0, w->ownerHwnd, NULL, GetModuleHandleW(NULL), w);
	if (!hwnd) {
		w->isDialog = 0;
		w->ownerHwnd = NULL;
		return 0;
	}
	win_apply_dpi(w);
	wpx = (int)(MW_DLG_W * w->scale + 0.5f);
	hpx = (int)(MW_DLG_H * w->scale + 0.5f);
	GetWindowRect(w->ownerHwnd, &or);
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
	x = or.left + ((or.right - or.left) - wpx) / 2;
	y = or.top + ((or.bottom - or.top) - hpx) / 2;
	if (x < wa.left) x = wa.left;
	if (y < wa.top) y = wa.top;
	if (x + wpx > wa.right) x = wa.right - wpx;
	if (y + hpx > wa.bottom) y = wa.bottom - hpx;
	SetWindowPos(hwnd, HWND_TOP, x, y, wpx, hpx, SWP_NOACTIVATE);
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	/* 真正的模态: 禁用所有其它 CUI 窗口, 只保留对话框可用. */
	for (i = 0; i < 2; i++)
		if (g_win[i].hwnd) EnableWindow(g_win[i].hwnd, FALSE);
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
	return 1;
}

static int Mw_CreateMain(void)
{
	MWin *w = &g_win[0];
	HWND hwnd;
	w->id = 0;
	w->isDialog = 0;
	w->ownerHwnd = NULL;
	w->wantW = MW_MAIN_W;
	w->wantH = MW_MAIN_H;
	hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
		L"CuiMultiWindow", L"CUI Multi Window", WS_POPUP,
		0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), w);
	if (!hwnd) return 0;
	win_apply_dpi(w);
	win_place(w, MW_MAIN_W, MW_MAIN_H);
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	UiFrame_Init(&g_clock, hwnd);
	return 1;
}

static void Mw_EnableDpi(void)
{
	HMODULE u32 = GetModuleHandleW(L"user32.dll");
	typedef BOOL(WINAPI *FnSetCtx)(HANDLE);
	FnSetCtx f = u32 ? (FnSetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext") : NULL;
	if (f && f((HANDLE)(INT_PTR)-4)) return;
	SetProcessDPIAware();
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	MSG msg;
	int quit = 0;
	WNDCLASSW wc;
	(void)prev; (void)cmd; (void)show;

	Mw_EnableDpi();
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	Log_Init();

	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = MultiWndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.lpszClassName = L"CuiMultiWindow";
	if (!RegisterClassW(&wc)) {
		Log_Close();
		CoUninitialize();
		return 1;
	}

	if (!Mw_CreateMain()) {
		Log_Close();
		CoUninitialize();
		return 1;
	}

	while (!quit) {
		int anim = 0;
		int i;
		for (i = 0; i < MW_COUNT; i++)
			if (win_animating(&g_win[i])) anim = 1;
		if (anim) {
			double t0 = UiFrame_Now();
			double dt = UiFrame_Tick(&g_clock);
			for (i = 0; i < MW_COUNT; i++) {
				MWin *w = &g_win[i];
				UiRect d;
				int kind;
				if (!w->hwnd || !w->gfx) continue;
				Ui_AnimAdvance(&w->ui, (float)dt);
				win_tick(w, dt, UiFrame_Now());
				kind = Ui_AnimTakeDirty(&w->ui, &d);
				if (kind == 2) {
					w->full = 1;
					InvalidateRect(w->hwnd, NULL, FALSE);
				} else if (kind == 1) {
					win_invalidate_rect(w, d, 0.0f);
				}
			}
			for (i = 0; i < MW_COUNT; i++)
				if (g_win[i].hwnd) UpdateWindow(g_win[i].hwnd);
			UiFrame_Delay(&g_clock,
				g_clock.periodMs - (UiFrame_Now() - t0));
		} else {
			MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT,
				MWMO_INPUTAVAILABLE);
		}
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				quit = 1;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	UiFrame_Shutdown(&g_clock);
	Log_Close();
	CoUninitialize();
	return 0;
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
