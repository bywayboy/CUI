#include <windows.h>
#include <imm.h>
#include <string.h>
#include "ui/ui_app.h"
#include "log.h"

static int g_classReady;

static void app_apply_dpi(UiApp *app)
{
	UINT dpi = 96;
	HMODULE u32 = GetModuleHandleW(L"user32.dll");
	if (u32) {
		typedef UINT(WINAPI *FnGetDpiForWindow)(HWND);
		FnGetDpiForWindow f = (FnGetDpiForWindow)GetProcAddress(u32, "GetDpiForWindow");
		if (app->hwnd && f) {
			UINT d = f(app->hwnd);
			if (d > 0) dpi = d;
		}
	}
	app->scale = (float)dpi / 96.0f;
}

static void app_place_window(UiApp *app, int widthDip, int heightDip)
{
	RECT wa;
	int w = (int)(widthDip * app->scale);
	int h = (int)(heightDip * app->scale);
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
	SetWindowPos(app->hwnd, NULL,
		wa.left + ((wa.right - wa.left) - w) / 2,
		wa.top + ((wa.bottom - wa.top) - h) / 2,
		w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void app_invalidate_rect(UiApp *app, UiRect rc, float pad)
{
	RECT r;
	float s;
	if (!app || !app->hwnd || rc.w <= 0.0f || rc.h <= 0.0f) return;
	s = app->scale > 0.0f ? app->scale : 1.0f;
	r.left = (LONG)((rc.x - pad) * s);
	r.top = (LONG)((rc.y - pad) * s);
	r.right = (LONG)((rc.x + rc.w + pad) * s + 1.0f);
	r.bottom = (LONG)((rc.y + rc.h + pad) * s + 1.0f);
	InvalidateRect(app->hwnd, &r, FALSE);
}

void UiApp_InvalidateRect(UiApp *app, UiRect rc, float pad)
{
	app_invalidate_rect(app, rc, pad);
}

void UiApp_Invalidate(UiApp *app)
{
	if (!app || !app->hwnd) return;
	app->fullRedraw = 1;
	InvalidateRect(app->hwnd, NULL, FALSE);
}

void UiApp_Close(UiApp *app)
{
	if (app && app->hwnd) PostMessageW(app->hwnd, WM_CLOSE, 0, 0);
}

static void app_render(UiApp *app, const RECT *dirty)
{
	RECT rc;
	if (!app || !app->gfx) return;
	GetClientRect(app->hwnd, &rc);
	Gfx_Resize(app->gfx, (int)rc.right, (int)rc.bottom, app->scale);
	app->w = (float)rc.right / app->scale;
	app->h = (float)rc.bottom / app->scale;
	Ui_SetViewport(&app->ui, app->w, app->h, app->scale);

	Gfx_SetDirtyRect(app->gfx, app->fullRedraw ? NULL : dirty);
	Gfx_Begin(app->gfx);
	Gfx_Clear(app->gfx, 0x00000000);

	Ui_BeginFrame(&app->ui);
	if (app->cb && app->cb->frame) app->cb->frame(app, app->user);
	Ui_EndFrame(&app->ui);

	Gfx_End(app->gfx);
	app->fullRedraw = 0;
	if (app->cb && app->cb->after_frame) app->cb->after_frame(app, app->user);
}

static void app_enable_dpi_awareness(void)
{
	HMODULE u32 = GetModuleHandleW(L"user32.dll");
	if (u32) {
		typedef BOOL(WINAPI *FnSetCtx)(HANDLE);
		FnSetCtx f = (FnSetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
		if (f && f((HANDLE)(INT_PTR)-4)) return;   /* PER_MONITOR_AWARE_V2 */
	}
	SetProcessDPIAware();
}

static LRESULT CALLBACK AppWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	UiApp *app = (UiApp *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	switch (msg) {
	case WM_NCCREATE:
		app = (UiApp *)((CREATESTRUCTW *)lp)->lpCreateParams;
		if (app) {
			app->hwnd = hwnd;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
		}
		return TRUE;

	case WM_CREATE:
		if (!app) return -1;
		app->hwnd = hwnd;
		if (!Gfx_Init(&app->gfx, hwnd) || !app->gfx) return -1;
		UiTheme_Init(&app->theme, app->gfx);
		UiTheme_SetTheme(&app->theme, app->themeId);
		Ui_ContextInit(&app->ui, hwnd, app->gfx, &app->theme);
		UiIme_Init(&app->ime, hwnd);
		UiFrame_Init(&app->clock, hwnd);
		app_apply_dpi(app);
		SetTimer(hwnd, 2, 530, NULL);
		if (app->cb && app->cb->init) app->cb->init(app, app->user);
		return 0;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		EndPaint(hwnd, &ps);
		app_render(app, &ps.rcPaint);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;

	case WM_NCHITTEST: {
		LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		LRESULT custom = hit;
		ScreenToClient(hwnd, &pt);
		if (app && app->cb && app->cb->nchittest &&
			app->cb->nchittest(app, pt.x, pt.y, &custom, app->user))
			return custom;
		return hit;
	}

	case WM_MOUSEMOVE: {
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		float x, y;
		if (!app) return 0;
		if (!app->tracking) {
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(tme);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hwnd;
			tme.dwHoverTime = 0;
			if (TrackMouseEvent(&tme)) app->tracking = 1;
		}
		x = pt.x / app->scale;
		y = pt.y / app->scale;
		if (app->cb && app->cb->mouse)
			app->cb->mouse(app, UI_APP_MOUSE_MOVE, x, y, app->user);
		Ui_OnMouseMove(&app->ui, x, y);
		return 0;
	}
	case WM_MOUSELEAVE:
		if (app) {
			app->tracking = 0;
			Ui_OnMouseLeave(&app->ui);
			if (app->cb && app->cb->mouse)
				app->cb->mouse(app, UI_APP_MOUSE_LEAVE, 0.0f, 0.0f, app->user);
		}
		return 0;

	case WM_LBUTTONDOWN: {
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		if (!app) return 0;
		SetCapture(hwnd);
		SetFocus(hwnd);
		Ui_OnMouseDown(&app->ui, pt.x / app->scale, pt.y / app->scale);
		if (app->cb && app->cb->mouse)
			app->cb->mouse(app, UI_APP_MOUSE_DOWN,
				pt.x / app->scale, pt.y / app->scale, app->user);
		return 0;
	}
	case WM_LBUTTONUP: {
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		if (!app) return 0;
		if (app->cb && app->cb->mouse)
			app->cb->mouse(app, UI_APP_MOUSE_UP,
				pt.x / app->scale, pt.y / app->scale, app->user);
		Ui_OnMouseUp(&app->ui, pt.x / app->scale, pt.y / app->scale);
		ReleaseCapture();
		return 0;
	}
	case WM_CAPTURECHANGED:
		if (app) Ui_OnCaptureChanged(&app->ui);
		return 0;

	case WM_KEYDOWN:
		if (app && app->cb && app->cb->key)
			app->cb->key(app, (UINT)wp, app->user);
		return 0;
	case WM_CHAR:
		if (app && app->cb && app->cb->chr)
			app->cb->chr(app, (wchar_t)wp, app->user);
		return 0;

	case WM_SETFOCUS:
		if (app) UiIme_Apply(&app->ime);
		return 0;
	case WM_KILLFOCUS:
		if (app) UiIme_Cancel(&app->ime);
		return 0;
	case WM_IME_SETCONTEXT:
		if (app) UiIme_Pos(&app->ime, app->scale);
		return DefWindowProcW(hwnd, msg, wp,
			(LPARAM)((DWORD)lp & ~(DWORD)ISC_SHOWUICOMPOSITIONWINDOW));
	case WM_IME_STARTCOMPOSITION:
		if (app) {
			UiIme_Start(&app->ime);
			UiIme_Pos(&app->ime, app->scale);
			if (app->cb && app->cb->ime) app->cb->ime(app, app->user);
		}
		return 0;
	case WM_IME_COMPOSITION:
		if (app) {
			UiIme_Comp(&app->ime, lp);
			UiIme_Pos(&app->ime, app->scale);
			if (app->cb && app->cb->ime) app->cb->ime(app, app->user);
		}
		return 0;
	case WM_IME_ENDCOMPOSITION:
		if (app) {
			UiIme_End(&app->ime);
			if (app->cb && app->cb->ime) app->cb->ime(app, app->user);
		}
		return 0;
	case WM_IME_CHAR:
		if (app) {
			UiIme_Char(&app->ime, (wchar_t)wp);
			if (app->cb && app->cb->ime) app->cb->ime(app, app->user);
		}
		return 0;

	case WM_TIMER:
		if (wp == 2 && app && app->cb && app->cb->timer)
			app->cb->timer(app, app->user);
		return 0;

	case WM_SIZE:
		if (app) {
			app->minimized = (wp == SIZE_MINIMIZED) ? 1 : 0;
			if (!app->minimized) {
				UiFrame_ResetTick(&app->clock);
				InvalidateRect(hwnd, NULL, FALSE);
			}
		}
		return 0;

	case WM_DPICHANGED: {
		const RECT *sugg = (const RECT *)lp;
		if (app) {
			app_apply_dpi(app);
			SetWindowPos(hwnd, NULL, sugg->left, sugg->top,
				sugg->right - sugg->left, sugg->bottom - sugg->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	}

	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, 2);
		if (app) {
			if (app->cb && app->cb->shutdown) app->cb->shutdown(app, app->user);
			UiFrame_Shutdown(&app->clock);
			UiTheme_Shutdown(&app->theme, app->gfx);
			if (app->gfx) {
				Gfx_Shutdown(app->gfx);
				app->gfx = NULL;
			}
		}
		PostQuitMessage(0);
		return 0;
	case WM_NCDESTROY:
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

int UiApp_Run(const wchar_t *title, int widthDip, int heightDip,
	UiThemeId theme, const UiAppCallbacks *cb, void *user)
{
	UiApp *app;
	MSG msg;
	HWND hwnd;
	int quit = 0;
	HINSTANCE inst = GetModuleHandleW(NULL);

	/* UiApp 含 UiContext (控件表/动画池), 放栈上会触发 0CRT 的 __chkstk */
	app = (UiApp *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(UiApp));
	if (!app) return 1;
	app->cb = cb;
	app->user = user;
	app->themeId = theme;
	app->scale = 1.0f;

	app_enable_dpi_awareness();
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	Log_Init();

	if (!g_classReady) {
		WNDCLASSW wc;
		ZeroMemory(&wc, sizeof(wc));
		wc.lpfnWndProc = AppWndProc;
		wc.hInstance = inst;
		wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
		wc.lpszClassName = L"CuiAppWnd";
		if (!RegisterClassW(&wc)) {
			Log_Write("[app] RegisterClass failed");
			Log_Close();
			CoUninitialize();
			HeapFree(GetProcessHeap(), 0, app);
			return 1;
		}
		g_classReady = 1;
	}

	hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
		L"CuiAppWnd", title ? title : L"CUI",
		WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, app);
	app->hwnd = hwnd;
	if (!hwnd) {
		Log_Write("[app] CreateWindow failed");
		Log_Close();
		CoUninitialize();
		HeapFree(GetProcessHeap(), 0, app);
		return 1;
	}

	app_apply_dpi(app);
	app_place_window(app, widthDip, heightDip);
	ShowWindow(app->hwnd, SW_SHOW);
	UpdateWindow(app->hwnd);
	UiFrame_ResetTick(&app->clock);

	while (!quit) {
		int anim = !app->minimized &&
			(Ui_AnimBusy(&app->ui) ||
				(app->cb && app->cb->animating && app->cb->animating(app, app->user)));
		if (anim) {
			double t0 = UiFrame_Now();
			double dt = UiFrame_Tick(&app->clock);
			Ui_AnimAdvance(&app->ui, (float)dt);
			if (app->cb && app->cb->tick) app->cb->tick(app, dt, app->user);
			{
				UiRect d;
				int kind = Ui_AnimTakeDirty(&app->ui, &d);
				if (kind == 2) InvalidateRect(app->hwnd, NULL, FALSE);
				else if (kind == 1) app_invalidate_rect(app, d, 0.0f);
			}
			if (app->hwnd) {
				UpdateWindow(app->hwnd);
				UiFrame_Delay(&app->clock,
					app->clock.periodMs - (UiFrame_Now() - t0));
			}
		} else {
			MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT,
				MWMO_INPUTAVAILABLE);
		}

		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) { quit = 1; break; }
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	Log_Close();
	CoUninitialize();
	HeapFree(GetProcessHeap(), 0, app);
	return 0;
}
