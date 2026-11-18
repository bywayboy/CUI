/*
 * webview.c - WebView2 组件样例
 *
 * 顶栏由 CUI 立即模式绘制 (主页/刷新/退出), 内容区是 WebView2
 * 视觉托管子视觉. 鼠标/滚轮在壳的 raw input 钩子里先送给 WebView2;
 * 键盘由宿主 HWND 焦点交给 WebView2 (MoveFocus 在点击时完成).
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_widgets.h"
#include "ui/ui_webview.h"

#define WV_BAR 54.0f
#define WV_FOOT 24.0f
#define WV_BTN_W 72.0f
#define WV_BTN_H 34.0f
#define WV_BTN_GAP 8.0f
#define WV_PAD 16.0f

typedef struct App {
	UiWebView *wv;
	UiApp *app;
	int failed;
	int nSource, nHistory, nLoading, nDom, nTitle;
	DWORD t0, tCreated, tNav, tContent, tDom, tDone;
	char msg[256];
} App;

static void set_msg(App *a, const char *s)
{
	lstrcpynA(a->msg, s, (int)sizeof(a->msg));
	if (a->app) UiApp_Invalidate(a->app);
}

static void set_msg_w(App *a, const char *prefix, const wchar_t *w)
{
	char utf8[160];
	int n;
	lstrcpynA(a->msg, prefix, (int)sizeof(a->msg));
	n = WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, utf8,
		(int)sizeof(utf8) - 1, NULL, NULL);
	if (n > 0) {
		utf8[n] = 0;
		lstrcatA(a->msg, utf8);
	}
	if (a->app) UiApp_Invalidate(a->app);
}

static void on_created(void *user, UiWebView *wv)
{
	App *a = (App *)user;
	wchar_t dir[MAX_PATH];
	a->tCreated = GetTickCount();
	DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != L'\\') n--;
	if (n > 0) dir[n - 1] = 0;
	if (!UiWebView_SetVirtualHost(wv, L"cui.example", dir))
		set_msg(a, "虚拟主机映射失败");
	UiWebView_Navigate(wv, L"https://cui.example/webview_test.html");
}

static void on_create_failed(void *user, UiWebView *wv, int hr)
{
	App *a = (App *)user;
	(void)wv;
	wsprintfA(a->msg, "创建失败 hr=0x%08X (检查 WebView2 Runtime/Loader)",
		(unsigned)hr);
	a->failed = 1;
	if (a->app) UiApp_Invalidate(a->app);
}

static void on_nav_starting(void *user, UiWebView *wv,
	const wchar_t *uri, int *cancel)
{
	App *a = (App *)user;
	(void)wv; (void)cancel;
	a->tNav = GetTickCount();
	a->tContent = 0;
	a->tDom = 0;
	a->tDone = 0;
	set_msg_w(a, "载入中: ", uri);
}

static void on_nav_done(void *user, UiWebView *wv, int success, int error)
{
	App *a = (App *)user;
	(void)wv;
	if (success) a->tDone = GetTickCount();
	if (success) {
		set_msg(a, "载入完成");
	} else {
		wsprintfA(a->msg, "载入失败 (error=%d)", error);
		a->failed = 1;
		if (a->app) UiApp_Invalidate(a->app);
	}
}

static void on_new_window(void *user, UiWebView *wv, UiWebViewNewWindow *nw)
{
	App *a = (App *)user;
	nw->handled = 1;
	set_msg_w(a, "新窗口请求(当前视图打开): ", nw->uri);
	UiWebView_Navigate(wv, nw->uri);
}

static void on_source_changed(void *user, UiWebView *wv,
	const wchar_t *uri, int is_new_document)
{
	App *a = (App *)user;
	(void)wv; (void)is_new_document;
	a->nSource++;
	set_msg_w(a, "地址: ", uri);
}

static void on_history_changed(void *user, UiWebView *wv,
	int can_back, int can_forward)
{
	App *a = (App *)user;
	(void)wv;
	a->nHistory++;
	wsprintfA(a->msg, "历史: 后退=%d 前进=%d", can_back, can_forward);
	if (a->app) UiApp_Invalidate(a->app);
}

static void on_content_loading(void *user, UiWebView *wv, int is_error)
{
	App *a = (App *)user;
	(void)wv;
	a->nLoading++;
	if (!a->tContent) a->tContent = GetTickCount();
	set_msg(a, is_error ? "内容加载(错误页)" : "内容开始加载");
}

static void on_dom_ready(void *user, UiWebView *wv,
	unsigned long long navigation_id)
{
	App *a = (App *)user;
	(void)wv;
	a->nDom++;
	if (!a->tDom) a->tDom = GetTickCount();
	wsprintfA(a->msg, "DOMContentLoaded nav=%llu", navigation_id);
	if (a->app) UiApp_Invalidate(a->app);
}

static void on_title_changed(void *user, UiWebView *wv, const wchar_t *title)
{
	App *a = (App *)user;
	(void)wv;
	a->nTitle++;
	set_msg_w(a, "标题: ", title);
}

static void on_download(void *user, UiWebView *wv, UiWebViewDownload *dl)
{
	App *a = (App *)user;
	(void)wv;
	dl->cancel = 1;   /* 样例: 拦截下载, 不写磁盘 */
	dl->handled = 0;
	set_msg_w(a, "下载已拦截: ", dl->uri);
}

static void on_script_dialog(void *user, UiWebView *wv,
	UiWebViewScriptDialog *dlg)
{
	App *a = (App *)user;
	(void)wv;
	dlg->handled = 1;
	dlg->accept = 1;
	if (dlg->kind == UI_WV_DIALOG_PROMPT)
		lstrcpynW(dlg->result_text, L"CUI", 512);
	{
		char prefix[64];
		wsprintfA(prefix, "脚本对话框 kind=%d: ", dlg->kind);
		set_msg_w(a, prefix, dlg->message);
	}
}

static void on_frame_nav_done(void *user, UiWebView *wv,
	int success, int error)
{
	App *a = (App *)user;
	(void)wv;
	if (!success) {
		wsprintfA(a->msg, "子框架导航失败 (error=%d)", error);
		if (a->app) UiApp_Invalidate(a->app);
	}
}

static void on_process_failed(void *user, UiWebView *wv, int kind)
{
	App *a = (App *)user;
	(void)wv;
	wsprintfA(a->msg, "渲染进程失败 kind=%d", kind);
	a->failed = 1;
	if (a->app) UiApp_Invalidate(a->app);
}

static void on_web_message(void *user, UiWebView *wv, const wchar_t *json)
{
	App *a = (App *)user;
	(void)wv;
	set_msg_w(a, "页面消息: ", json);
}

static void on_cert_error(void *user, UiWebView *wv, const wchar_t *uri,
	int *allow)
{
	App *a = (App *)user;
	(void)wv; (void)uri;
	*allow = 0;
	set_msg(a, "证书错误: 已拒绝 (不静默绕过)");
	a->failed = 1;
}

static void on_init(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiWebViewConfig cfg;
	UiWebViewEvents ev;

	a->app = app;
	UiWebViewConfig_Default(&cfg);
	cfg.homepage = L"https://cui.example/webview_test.html";
	cfg.flags &= ~UI_WVF_AUTO_NAVIGATE;
	cfg.flags |= UI_WVF_DEVTOOLS | UI_WVF_CONTEXT_MENUS;

	ZeroMemory(&ev, sizeof(ev));
	ev.created = on_created;
	ev.create_failed = on_create_failed;
	ev.navigation_starting = on_nav_starting;
	ev.navigation_completed = on_nav_done;
	ev.frame_navigation_completed = on_frame_nav_done;
	ev.new_window = on_new_window;
	ev.source_changed = on_source_changed;
	ev.history_changed = on_history_changed;
	ev.content_loading = on_content_loading;
	ev.dom_content_loaded = on_dom_ready;
	ev.document_title_changed = on_title_changed;
	ev.download_starting = on_download;
	ev.script_dialog = on_script_dialog;
	ev.process_failed = on_process_failed;
	ev.web_message = on_web_message;
	ev.certificate_error = on_cert_error;
	ev.user = a;

	a->wv = UiWebView_Create(app->gfx, Ui_Id("wv.demo", 0, 0), &cfg, &ev);
	if (!a->wv) set_msg(a, "UiWebView_Create 返回 NULL");
}

static void on_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiTheme *t = &app->theme;
	float bx, by;
	UiRect content;

	Gfx_FillRoundRectGradV(app->gfx, Ui_Rect(0, 0, app->w, app->h),
		14.0f, t->page, t->page2);

	/* 顶栏: 只圆上两角, 避免方角盖住窗口圆角 */
	Gfx_FillRoundRectCorners(app->gfx, Ui_Rect(0, 0, app->w, WV_BAR),
		Gfx_Corners(14.0f, 14.0f, 0.0f, 0.0f), t->page);
	Gfx_FillRect(app->gfx, Ui_Rect(0, WV_BAR - 1.0f, app->w, 1.0f),
		t->border);
	Gfx_Text(app->gfx, t->fBig, "CUI WebView2",
		Ui_Rect(20, 0, app->w - 300, WV_BAR), t->text,
		GFX_TEXT_VCENTER);

	by = (WV_BAR - WV_BTN_H) * 0.5f;
	bx = app->w - WV_PAD - WV_BTN_W;
	if (Ui_Button(&app->ui, Ui_Id("wv.quit", 0, 0),
			Ui_Rect(bx, by, WV_BTN_W, WV_BTN_H), "退出",
			UI_BTN_DANGER, 1))
		UiApp_Close(app);
	bx -= WV_BTN_W + WV_BTN_GAP;
	if (Ui_Button(&app->ui, Ui_Id("wv.reload", 0, 0),
			Ui_Rect(bx, by, WV_BTN_W, WV_BTN_H), "刷新",
			UI_BTN_SECONDARY, 1))
		UiWebView_Reload(a->wv);
	bx -= WV_BTN_W + WV_BTN_GAP;
	if (Ui_Button(&app->ui, Ui_Id("wv.home", 0, 0),
			Ui_Rect(bx, by, WV_BTN_W, WV_BTN_H), "主页",
			UI_BTN_SECONDARY, 1))
		UiWebView_GoHome(a->wv);
	bx -= WV_BTN_W + WV_BTN_GAP;
	if (Ui_Button(&app->ui, Ui_Id("wv.send", 0, 0),
			Ui_Rect(bx, by, WV_BTN_W, WV_BTN_H), "发消息",
			UI_BTN_PRIMARY, 1))
		UiWebView_PostWebMessageJson(a->wv,
			L"{\"from\":\"cui\",\"text\":\"hello\"}");

	if (a->msg[0]) {
		Gfx_Text(app->gfx, t->fSmall, a->msg,
			Ui_Rect(210, 0, app->w - 556, WV_BAR),
			a->failed ? t->danger : t->hint, GFX_TEXT_VCENTER);
	}

	/* 内容区: 每帧只更新视觉矩形; controller 不重建 */
	content = Ui_Rect(0, WV_BAR, app->w,
		app->h - WV_BAR - WV_FOOT);
	Ui_WebView(a->wv, &app->ui, content);

	/* 底栏: P0 事件计数器 (演示绑定是否生效) */
	{
		char stats[192];
		float fy = app->h - WV_FOOT;
		Gfx_FillRoundRectCorners(app->gfx,
			Ui_Rect(0, fy, app->w, WV_FOOT),
			Gfx_Corners(0.0f, 0.0f, 14.0f, 14.0f), t->page);
		Gfx_FillRect(app->gfx, Ui_Rect(0, fy, app->w, 1.0f), t->border);
		wsprintfA(stats, "事件: Source %d  历史 %d  ContentLoading %d  "
			"DOMContentLoaded %d  标题 %d", a->nSource, a->nHistory,
			a->nLoading, a->nDom, a->nTitle);
		if (a->tDone && a->tNav) {
			char tbuf[128];
			DWORD content = a->tContent ? a->tContent - a->tNav : 0;
			DWORD dom = a->tDom ? a->tDom - a->tNav : 0;
			wsprintfA(tbuf, "  |  启动 %u ms  导航->内容 %u ms  "
				"导航->DOM %u ms  导航->完成 %u ms",
				a->tCreated - a->t0, content, dom,
				a->tDone - a->tNav);
			lstrcatA(stats, tbuf);
		}
		Gfx_Text(app->gfx, t->fSmall, stats,
			Ui_Rect(12, fy, app->w - 24, WV_FOOT), t->hint,
			GFX_TEXT_VCENTER);
	}
}

static int on_input(UiApp *app, UINT msg, WPARAM wp, LPARAM lp,
	LRESULT *result, void *user)
{
	App *a = (App *)user;
	if (!a->wv) return 0;
	if (UiWebView_Input(a->wv, &app->ui, msg, wp, lp)) {
		*result = 0;
		return 1;
	}
	return 0;
}

static int on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	float dx = (float)x / app->scale;
	float dy = (float)y / app->scale;
	float bx;
	(void)user;
	if (dy >= WV_BAR) return 0;
	/* 按钮区域必须保持客户区, 其余顶栏可拖窗 */
	bx = app->w - WV_PAD - WV_BTN_W;
	if (dx >= bx - (WV_BTN_W + WV_BTN_GAP) * 3.0f)
		return 0;
	*hit = HTCAPTION;
	return 1;
}

static void on_shutdown(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	if (a->wv) {
		UiWebView_Destroy(a->wv);
		a->wv = NULL;
	}
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	static App a;
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	a.t0 = GetTickCount();
	ZeroMemory(&cb, sizeof(cb));
	cb.input = on_input;
	cb.init = on_init;
	cb.frame = on_frame;
	cb.nchittest = on_nchittest;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI WebView2", 960, 680, UI_THEME_LIGHT, &cb, &a);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
