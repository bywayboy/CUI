/*
 * net.c - 网络层样例: HTTP(S) / WebSocket / GUI 定时器
 *
 * 默认指向本机测试服务器:
 *   HTTP  http://127.0.0.1:18080/hello
 *   WS    ws://127.0.0.1:18081/ws
 * 可编辑 URL 后点按钮; 回调全部在 GUI 线程。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_input.h"
#include "ui/ui_widgets.h"
#include "edit.h"
#include "net/ui_net.h"

#define LOG_CAP 4096

typedef struct App {
	UiApp *app;
	UiInput form;
	int url, body, wsUrl;
	UiNet *net;
	void *ws;
	uint64_t reqId;
	int ticks;
	char log[LOG_CAP];
	int logLen;
} App;

static void log_add(App *a, const char *s)
{
	int n = (int)lstrlenA(s);
	if (n <= 0) return;
	if (a->logLen + n + 2 > LOG_CAP) {
		/* 丢最早一半行 */
		int cut = a->logLen / 2;
		while (cut < a->logLen && a->log[cut] != '\n') cut++;
		if (cut < a->logLen) cut++;
		memmove(a->log, a->log + cut, (size_t)(a->logLen - cut));
		a->logLen -= cut;
	}
	if (a->logLen + n + 1 > LOG_CAP) n = LOG_CAP - a->logLen - 1;
	memcpy(a->log + a->logLen, s, (size_t)n);
	a->logLen += n;
	a->log[a->logLen++] = '\n';
	a->log[a->logLen] = '\0';
	if (a->app) UiApp_Invalidate(a->app);
}

static void log_fmt(App *a, const char *fmt, const char *x, int n)
{
	char b[512];
	int k = 0;
	const char *p = fmt;
	while (*p && k < (int)sizeof(b) - 1) {
		if (p[0] == '%' && p[1] == 's') {
			int i;
			for (i = 0; i < n && x[i] && k < (int)sizeof(b) - 1; i++) b[k++] = x[i];
			p += 2;
		} else if (p[0] == '%' && p[1] == 'd') {
			char num[24];
			int v = n, j = 0, neg = 0;
			if (v < 0) { neg = 1; v = -v; }
			do { num[j++] = (char)('0' + v % 10); v /= 10; } while (v && j < 20);
			if (neg) b[k++] = '-';
			while (j > 0 && k < (int)sizeof(b) - 1) b[k++] = num[--j];
			p += 2;
		} else {
			b[k++] = *p++;
		}
	}
	b[k] = '\0';
	log_add(a, b);
}

static void on_http_done(void *user, const UiHttpResponse *r)
{
	App *a = (App *)user;
	char line[320];
	char ct[64];
	int cl = UiHttp_Header(r, "content-type", ct, sizeof(ct));
	int shown;

	a->reqId = 0;
	if (r->error) {
		wsprintfA(line, "[http] 失败: %s", UiNet_StrError(r->error));
		log_add(a, line);
		return;
	}
	shown = r->bodyLen < 160 ? r->bodyLen : 160;
	wsprintfA(line, "[http] %d, %d 字节%s%s", r->status, r->bodyLen,
		cl > 0 ? ", " : "", cl > 0 ? ct : "");
	log_add(a, line);
	{
		char body[200];
		int i;
		for (i = 0; i < shown; i++) {
			char c = ((const char *)r->body)[i];
			body[i] = (c == '\r' || c == '\n') ? ' ' : c;
		}
		body[shown] = '\0';
		log_fmt(a, "    %s", body, (int)lstrlenA(body));
	}
}

static void on_ws_open(void *user)
{
	App *a = (App *)user;
	log_add(a, "[ws] 已连接");
}

static void on_ws_msg(void *user, const void *data, int len, int opcode)
{
	App *a = (App *)user;
	char head[160];
	int n = len < 120 ? len : 120;
	int i;
	for (i = 0; i < n; i++) {
		char c = ((const char *)data)[i];
		head[i] = (c == '\r' || c == '\n') ? ' ' : c;
	}
	head[n] = '\0';
	{
		char line[220];
		wsprintfA(line, "[ws] %s %d 字节: %s", opcode == 2 ? "binary" : "text",
			len, head);
		log_add(a, line);
	}
}

static void on_ws_close(void *user, int code, const char *reason)
{
	App *a = (App *)user;
	char line[220];
	a->ws = NULL;
	wsprintfA(line, "[ws] 关闭 code=%d %s", code, reason ? reason : "");
	log_add(a, line);
}

static void do_get(App *a)
{
	UiHttpRequest r;
	char url[1024];
	ZeroMemory(&r, sizeof(r));
	Edit_TextUtf8(UiInput_Edit(&a->form, a->url), url, sizeof(url));
	if (!url[0]) return;
	r.url = url;
	r.timeoutMs = 15000;
	a->reqId = UiHttp_Send(a->net, &r, on_http_done, a);
	log_add(a, a->reqId ? "[http] GET 已发出" : "[http] 发送失败");
}

static void do_post(App *a)
{
	UiHttpRequest r;
	char url[1024];
	char body[1024];
	ZeroMemory(&r, sizeof(r));
	Edit_TextUtf8(UiInput_Edit(&a->form, a->url), url, sizeof(url));
	Edit_TextUtf8(UiInput_Edit(&a->form, a->body), body, sizeof(body));
	if (!url[0]) return;
	r.method = "POST";
	r.url = url;
	r.contentType = "application/json";
	r.body = body;
	r.bodyLen = (int)lstrlenA(body);
	r.timeoutMs = 15000;
	a->reqId = UiHttp_Send(a->net, &r, on_http_done, a);
	log_add(a, a->reqId ? "[http] POST 已发出" : "[http] 发送失败");
}

static void do_ws_open(App *a)
{
	UiWsOptions o;
	UiWsCallbacks cb;
	char url[1024];

	if (a->ws) { log_add(a, "[ws] 已存在连接"); return; }
	Edit_TextUtf8(UiInput_Edit(&a->form, a->wsUrl), url, sizeof(url));
	if (!url[0]) return;
	ZeroMemory(&o, sizeof(o));
	ZeroMemory(&cb, sizeof(cb));
	o.autoReconnect = 1;
	o.reconnectMinMs = 500;
	o.reconnectMaxMs = 8000;
	o.pingIntervalMs = 20000;
	o.handshakeTimeoutMs = 8000;
	cb.open = on_ws_open;
	cb.message = on_ws_msg;
	cb.close = on_ws_close;
	a->ws = UiWs_Open(a->net, url, &o, &cb, a);
	log_add(a, a->ws ? "[ws] 连接中..." : "[ws] 打开失败");
}

static void do_ws_send(App *a)
{
	char body[1024];
	if (!a->ws) { log_add(a, "[ws] 未连接"); return; }
	Edit_TextUtf8(UiInput_Edit(&a->form, a->body), body, sizeof(body));
	if (UiWs_Send(a->ws, body, (int)lstrlenA(body), 1))
		log_add(a, "[ws] 已发送");
	else
		log_add(a, "[ws] 发送失败");
}

static void do_ws_close(App *a)
{
	if (!a->ws) return;
	UiWs_Close(a->ws, 1000, "bye");
	a->ws = NULL;
	log_add(a, "[ws] 正在关闭");
}

static void do_cancel(App *a)
{
	if (a->reqId) {
		UiHttp_Cancel(a->net, a->reqId);
		a->reqId = 0;
		log_add(a, "[http] 已取消");
	}
}

static void on_ui_tick(void *user, uint64_t id)
{
	App *a = (App *)user;
	(void)id;
	a->ticks++;
	if (a->app) UiApp_Invalidate(a->app);
}

static void on_init(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiNetConfig cfg;

	a->app = app;
	UiInput_Init(&a->form, &app->ui, &app->ime);
	a->url = UiInput_Add(&a->form, Ui_Id("net.url", 0, 0), 511, 0, NULL, L"URL");
	a->body = UiInput_Add(&a->form, Ui_Id("net.body", 0, 0), 511, 0, NULL, L"请求体");
	a->wsUrl = UiInput_Add(&a->form, Ui_Id("net.wsurl", 0, 0), 511, 0, NULL, L"WS URL");
	Edit_SetTextUtf8(UiInput_Edit(&a->form, a->url), "http://127.0.0.1:18080/hello");
	Edit_SetTextUtf8(UiInput_Edit(&a->form, a->body), "{\"msg\":\"hi\"}");
	Edit_SetTextUtf8(UiInput_Edit(&a->form, a->wsUrl), "ws://127.0.0.1:18081/ws");

	ZeroMemory(&cfg, sizeof(cfg));
	cfg.trace = 1;
	a->net = UiNet_Create(&cfg);
	log_add(a, a->net ? "[net] 初始化完成" : "[net] 初始化失败");
	UiTimer_Every(app->timer, 1000, on_ui_tick, a);
}

static void on_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiTheme *t = &app->theme;
	UiRect card, rc;
	float x, y, w;
	char line[128];
	char *p;
	int ly;

	Gfx_FillRoundRectGradV(app->gfx, Ui_Rect(0, 0, app->w, app->h),
		14.0f, t->page, t->page2);
	card = Ui_Rect(20, 20, app->w - 40, app->h - 40);
	Ui_Panel(&app->ui, card);
	x = card.x + 22;
	y = card.y + 18;
	w = card.w - 44;

	Gfx_Text(app->gfx, t->fBig, "CUI 网络层", Ui_Rect(x, y, w, 30),
		t->text, GFX_TEXT_VCENTER);
	wsprintfA(line, "IO 线程 + IOCP + SChannel | 定时器 %d s", a->ticks);
	Gfx_Text(app->gfx, t->fSmall, line, Ui_Rect(x, y, w, 30),
		t->hint, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
	y += 40;

	UiInput_BeginFrame(&a->form);

	Gfx_Text(app->gfx, t->fSmall, "URL", Ui_Rect(x, y, 40, 22),
		t->text2, GFX_TEXT_VCENTER);
	UiInput_SetRect(&a->form, a->url, Ui_Rect(x + 44, y, w - 340, 36));
	UiInput_Draw(&a->form, a->url);
	if (Ui_Button(&app->ui, Ui_Id("net.get", 0, 0), Ui_Rect(x + w - 288, y, 84, 36),
			"GET", UI_BTN_PRIMARY, 1)) do_get(a);
	if (Ui_Button(&app->ui, Ui_Id("net.post", 0, 0), Ui_Rect(x + w - 196, y, 90, 36),
			"POST", UI_BTN_SECONDARY, 1)) do_post(a);
	if (Ui_Button(&app->ui, Ui_Id("net.cancel", 0, 0), Ui_Rect(x + w - 98, y, 98, 36),
			"取消", UI_BTN_SECONDARY, 1)) do_cancel(a);
	y += 46;

	Gfx_Text(app->gfx, t->fSmall, "请求体", Ui_Rect(x, y, 52, 22),
		t->text2, GFX_TEXT_VCENTER);
	UiInput_SetRect(&a->form, a->body, Ui_Rect(x + 56, y, w - 180, 36));
	UiInput_Draw(&a->form, a->body);
	if (Ui_Button(&app->ui, Ui_Id("net.wsopen", 0, 0),
			Ui_Rect(x + w - 114, y, 114, 36), "WS 连接", UI_BTN_PRIMARY, 1))
		do_ws_open(a);
	y += 44;

	Gfx_Text(app->gfx, t->fSmall, "WS URL", Ui_Rect(x, y, 52, 22),
		t->text2, GFX_TEXT_VCENTER);
	UiInput_SetRect(&a->form, a->wsUrl, Ui_Rect(x + 56, y, w - 330, 36));
	UiInput_Draw(&a->form, a->wsUrl);
	if (Ui_Button(&app->ui, Ui_Id("net.wssend", 0, 0),
			Ui_Rect(x + w - 264, y, 110, 36), "发送", UI_BTN_SECONDARY, 1))
		do_ws_send(a);
	if (Ui_Button(&app->ui, Ui_Id("net.wsclose", 0, 0),
			Ui_Rect(x + w - 146, y, 146, 36), "断开", UI_BTN_SECONDARY, 1))
		do_ws_close(a);
	y += 48;

	rc = Ui_Rect(x, y + 4, w, 1);
	Gfx_FillRect(app->gfx, rc, t->border);
	y += 14;

	/* 日志 */
	p = a->log;
	ly = (int)(y);
	while (*p && ly < (int)(card.y + card.h - 14)) {
		char *nl = p;
		int n;
		while (*nl && *nl != '\n') nl++;
		n = (int)(nl - p);
		if (n > 220) n = 220;
		memcpy(line, p, (size_t)n);
		line[n] = '\0';
		Gfx_Text(app->gfx, t->fSmall, line, Ui_Rect(x, (float)ly, w, 18),
			t->text2, GFX_TEXT_VCENTER);
		ly += 18;
		p = (*nl == '\n') ? nl + 1 : nl;
	}
}

static void on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
	App *a = (App *)user;
	if (msg == UI_APP_MOUSE_DOWN)
		UiInput_MouseDown(&a->form, Ui_HitTest(&app->ui, x, y), x);
	else if (msg == UI_APP_MOUSE_MOVE)
		UiInput_MouseMove(&a->form, x);
	else if (msg == UI_APP_MOUSE_UP)
		UiInput_MouseUp(&a->form);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	App *a = (App *)user;
	if (UiInput_Key(&a->form, app->hwnd, vk)) return 1;
	if (vk == VK_RETURN) { do_get(a); return 1; }
	return 0;
}

static int on_char(UiApp *app, wchar_t ch, void *user)
{
	App *a = (App *)user;
	(void)app;
	return UiInput_Char(&a->form, ch);
}

static void on_ime(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiInput_OnIme(&a->form);
}

static void on_timer(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	UiInput_Blink(&a->form);
}

static void on_shutdown(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	if (a->ws) { UiWs_Close(a->ws, 1000, "bye"); a->ws = NULL; }
	if (a->net) { UiNet_Destroy(a->net); a->net = NULL; }
	UiInput_Shutdown(&a->form);
}

static int on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	(void)x; (void)user;
	if (y < (int)(50.0f * app->scale)) { *hit = HTCAPTION; return 1; }
	return 0;
}

static void on_tick(UiApp *app, double dt, void *user)
{
	(void)app; (void)dt; (void)user;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	static App a;
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.mouse = on_mouse;
	cb.key = on_key;
	cb.chr = on_char;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.shutdown = on_shutdown;
	cb.nchittest = on_nchittest;
	cb.tick = on_tick;
	return UiApp_Run(L"CUI Net", 860, 620, UI_THEME_LIGHT, &cb, &a);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), NULL, NULL, SW_SHOW));
}
#endif
