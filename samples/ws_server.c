/*
 * ws_server.c - WebSocket 服务端样例 (ws / wss)
 *
 * 默认监听 0.0.0.0:18082, 路径 /ws; 每个客户端消息原样回显。
 * WSS 按钮使用 build\test_wss.pfx (密码 cui-test, 由 tests\gen_test_cert.ps1 生成)。
 * 所有服务端回调都在 GUI 线程执行。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_input.h"
#include "ui/ui_widgets.h"
#include "edit.h"
#include "net/ui_ws_server.h"

#define LOG_CAP 4096

typedef struct App {
	UiApp *app;
	UiInput form;
	int port;
	int path;
	int msg;
	UiNet *net;
	UiWsServer *srv;
	int wss;
	int portShown;
	char log[LOG_CAP];
	int logLen;
} App;

static void log_add(App *a, const char *s)
{
	int n = (int)lstrlenA(s);
	if (n <= 0) return;
	if (a->logLen + n + 2 > LOG_CAP) {
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

static void log_num(App *a, const char *fmt, const char *s, int v)
{
	char b[512];
	int k = 0;
	const char *p = fmt;
	while (*p && k < (int)sizeof(b) - 24) {
		if (p[0] == '%' && p[1] == 's') {
			int i;
			for (i = 0; s && s[i] && k < (int)sizeof(b) - 2; i++)
				b[k++] = s[i];
			p += 2;
		} else if (p[0] == '%' && p[1] == 'd') {
			char num[24];
			int j = 0, neg = 0, x = v;
			if (x < 0) { neg = 1; x = -x; }
			do { num[j++] = (char)('0' + x % 10); x /= 10; } while (x && j < 20);
			if (neg) b[k++] = '-';
			while (j > 0) b[k++] = num[--j];
			p += 2;
		} else {
			b[k++] = *p++;
		}
	}
	b[k] = '\0';
	log_add(a, b);
}

static void srv_listening(void *user, int port)
{
	App *a = (App *)user;
	a->portShown = port;
	log_num(a, "[srv] 监听端口 %d", NULL, port);
}

static void srv_open(void *user, UiWsConn *conn, const UiWsServerRequest *req)
{
	App *a = (App *)user;
	char line[320];
	wsprintfA(line, "[srv] 新连接 %s %s", req && req->peer ? req->peer : "?",
		req && req->path ? req->path : "");
	log_add(a, line);
	if (req && req->protocol && req->protocol[0]) {
		wsprintfA(line, "[srv]   子协议 %s", req->protocol);
		log_add(a, line);
	}
}

static void srv_message(void *user, UiWsConn *conn, const void *data, int len,
	int opcode)
{
	App *a = (App *)user;
	char head[128];
	int n = len < 120 ? len : 120;
	int i;
	for (i = 0; i < n; i++) {
		char c = ((const char *)data)[i];
		head[i] = (c == '\r' || c == '\n') ? ' ' : c;
	}
	head[n] = '\0';
	{
		char line[256];
		wsprintfA(line, "[srv] 收到 %s %d 字节: %s",
			opcode == 2 ? "binary" : "text", len, head);
		log_add(a, line);
	}
	UiWsServer_Send(conn, data, len, opcode);
}

static void srv_close(void *user, UiWsConn *conn, int code, const char *reason)
{
	App *a = (App *)user;
	char line[220];
	(void)conn;
	wsprintfA(line, "[srv] 连接关闭 code=%d %s", code, reason ? reason : "");
	log_add(a, line);
}

static void srv_error(void *user, int err, const char *message)
{
	App *a = (App *)user;
	char line[256];
	wsprintfA(line, "[srv] 错误 %d: %s", err, message ? message : "");
	log_add(a, line);
}

static UiWsServerCallbacks g_cb = {
	srv_listening, srv_open, srv_message, srv_close, srv_error
};

static int parse_port(const char *s)
{
	int v = 0;
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s++ - '0');
		if (v > 65535) return 0;
	}
	return v;
}

static void do_start(App *a, int wss)
{
	UiWsServerConfig sc;
	char portText[32];
	char pathText[256];
	int port;

	if (a->srv) { log_add(a, "[srv] 已在运行"); return; }
	Edit_TextUtf8(UiInput_Edit(&a->form, a->port), portText, sizeof(portText));
	Edit_TextUtf8(UiInput_Edit(&a->form, a->path), pathText, sizeof(pathText));
	port = parse_port(portText);
	if (port <= 0) { log_add(a, "[srv] 端口无效"); return; }

	ZeroMemory(&sc, sizeof(sc));
	sc.bindHost = "0.0.0.0";
	sc.port = port;
	sc.path = pathText[0] ? pathText : NULL;
	sc.subprotocols = "chat,superchat";
	sc.maxConnections = 64;
	sc.pingIntervalMs = 20000;
	sc.closeTimeoutMs = 2000;
	if (wss) {
		static const char *pfx[] = {
			"build\\test_wss.pfx", "..\\build\\test_wss.pfx"
		};
		int i;
		for (i = 0; i < 2; i++) {
			if (GetFileAttributesA(pfx[i]) != INVALID_FILE_ATTRIBUTES) {
				sc.certPath = pfx[i];
				sc.certPassword = "cui-test";
				break;
			}
		}
		if (!sc.certPath) {
			log_add(a, "[srv] 未找到 build\\test_wss.pfx (先运行 tests\\gen_test_cert.ps1)");
			return;
		}
	}
	a->srv = UiWsServer_Create(a->net, &sc, &g_cb, a);
	if (!a->srv) {
		log_add(a, "[srv] 启动失败");
		return;
	}
	a->wss = wss;
	a->portShown = UiWsServer_Port(a->srv);
	log_num(a, wss ? "[srv] WSS 已启动, 端口 %d" : "[srv] WS 已启动, 端口 %d",
		NULL, a->portShown);
}

static void do_stop(App *a)
{
	if (!a->srv) return;
	UiWsServer_Destroy(a->srv);
	a->srv = NULL;
	a->wss = 0;
	a->portShown = 0;
	log_add(a, "[srv] 已停止");
}

static void do_broadcast(App *a)
{
	char body[1024];
	int n;
	if (!a->srv) { log_add(a, "[srv] 未启动"); return; }
	Edit_TextUtf8(UiInput_Edit(&a->form, a->msg), body, sizeof(body));
	n = (int)lstrlenA(body);
	if (n <= 0) return;
	n = UiWsServer_Broadcast(a->srv, body, n, 1);
	log_num(a, "[srv] 广播到 %d 个连接", NULL, n);
}

static void on_init(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiNetConfig cfg;

	a->app = app;
	UiInput_Init(&a->form, &app->ui, &app->ime);
	a->port = UiInput_Add(&a->form, Ui_Id("srv.port", 0, 0), 31, 0, NULL, L"端口");
	a->path = UiInput_Add(&a->form, Ui_Id("srv.path", 0, 0), 255, 0, NULL, L"路径");
	a->msg = UiInput_Add(&a->form, Ui_Id("srv.msg", 0, 0), 1023, 0, NULL, L"广播内容");
	Edit_SetTextUtf8(UiInput_Edit(&a->form, a->port), "18082");
	Edit_SetTextUtf8(UiInput_Edit(&a->form, a->path), "/ws");
	Edit_SetTextUtf8(UiInput_Edit(&a->form, a->msg), "hello from server");

	ZeroMemory(&cfg, sizeof(cfg));
	a->net = UiNet_Create(&cfg);
	log_add(a, a->net ? "[net] 初始化完成" : "[net] 初始化失败");
}

static void on_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiTheme *t = &app->theme;
	UiRect card;
	float x, y, w;
	char line[160];
	char *p;
	int ly;

	Gfx_FillRoundRectGradV(app->gfx, Ui_Rect(0, 0, app->w, app->h),
		14.0f, t->page, t->page2);
	card = Ui_Rect(20, 20, app->w - 40, app->h - 40);
	Ui_Panel(&app->ui, card);
	x = card.x + 22;
	y = card.y + 18;
	w = card.w - 44;

	Gfx_Text(app->gfx, t->fBig, "CUI WebSocket 服务端", Ui_Rect(x, y, w, 30),
		t->text, GFX_TEXT_VCENTER);
	if (a->srv) {
		wsprintfA(line, "%s :%d  连接 %d", a->wss ? "WSS" : "WS",
			a->portShown, UiWsServer_ConnCount(a->srv));
	} else {
		wsprintfA(line, "已停止");
	}
	Gfx_Text(app->gfx, t->fSmall, line, Ui_Rect(x, y, w, 30),
		t->hint, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
	y += 40;

	UiInput_BeginFrame(&a->form);

	Gfx_Text(app->gfx, t->fSmall, "端口", Ui_Rect(x, y, 40, 22),
		t->text2, GFX_TEXT_VCENTER);
	UiInput_SetRect(&a->form, a->port, Ui_Rect(x + 44, y, 90, 36));
	UiInput_Draw(&a->form, a->port);
	Gfx_Text(app->gfx, t->fSmall, "路径", Ui_Rect(x + 146, y, 40, 22),
		t->text2, GFX_TEXT_VCENTER);
	UiInput_SetRect(&a->form, a->path, Ui_Rect(x + 190, y, w - 190, 36));
	UiInput_Draw(&a->form, a->path);
	y += 46;

	if (Ui_Button(&app->ui, Ui_Id("srv.ws", 0, 0), Ui_Rect(x, y, 110, 36),
			"启动 WS", UI_BTN_PRIMARY, !a->srv)) do_start(a, 0);
	if (Ui_Button(&app->ui, Ui_Id("srv.wss", 0, 0), Ui_Rect(x + 118, y, 110, 36),
			"启动 WSS", UI_BTN_SECONDARY, !a->srv)) do_start(a, 1);
	if (Ui_Button(&app->ui, Ui_Id("srv.stop", 0, 0), Ui_Rect(x + 236, y, 100, 36),
			"停止", UI_BTN_SECONDARY, a->srv != NULL)) do_stop(a);
	y += 46;

	Gfx_Text(app->gfx, t->fSmall, "广播", Ui_Rect(x, y, 40, 22),
		t->text2, GFX_TEXT_VCENTER);
	UiInput_SetRect(&a->form, a->msg, Ui_Rect(x + 44, y, w - 160, 36));
	UiInput_Draw(&a->form, a->msg);
	if (Ui_Button(&app->ui, Ui_Id("srv.bc", 0, 0),
			Ui_Rect(x + w - 110, y, 110, 36), "广播", UI_BTN_PRIMARY,
			a->srv != NULL)) do_broadcast(a);
	y += 48;

	Gfx_FillRect(app->gfx, Ui_Rect(x, y + 4, w, 1), t->border);
	y += 14;

	p = a->log;
	ly = (int)y;
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
	if (vk == VK_RETURN) { do_broadcast(a); return 1; }
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
	if (a->app) UiApp_Invalidate(a->app);
}

static void on_shutdown(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	do_stop(a);
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
	return UiApp_Run(L"CUI WS Server", 860, 620, UI_THEME_LIGHT, &cb, &a);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), NULL, NULL, SW_SHOW));
}
#endif
