/*
 * ui_webview.c - WebView2 组件 (可选模块, 纯 C / 0CRT)
 *
 * - 动态加载 WebView2Loader.dll, 不链接 import lib/CRT
 * - 手写 COM vtable (无 C++/WRL/wil/STL)
 * - 环境进程内共享; 首选 CompositionController + DComp 子视觉,
 *   DComp 不可用或创建失败时回退窗口模式
 * - 鼠标/滚轮按 Visual hosting 要求转发; 键盘由 WebView2/OS 处理
 * - 所有回调均在 UI 线程
 */
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <stdint.h>
#include <stddef.h>

#include "WebView2.h"
#include "gfx/gfx.h"
#include "gfx/gfx_visual.h"
#include "ui/ui_webview.h"

enum {
	WV_STATE_IDLE = 0,
	WV_STATE_LOADING,
	WV_STATE_READY,
	WV_STATE_FAILED
};

typedef struct WvToken {
	LONG ref;
	LONG alive;
} WvToken;

struct UiWebView;

/* 所有 COM 处理器共用: 接口的首成员就是 vtable 指针, 因此本结构可直接
 * 与任意 ICoreWebView2*Handler* 指针互换 (x64 同一地址). */
typedef struct WvHdr {
	void *vtbl;
	LONG ref;
	WvToken *tok;
	struct UiWebView *wv;
	const IID *iid;
	int aux;                /* 导航处理器: 1 = frame 级 */
} WvHdr;

/* ── 小工具 ───────────────────────────────────────────────────────── */

static wchar_t *wv_strdup_(const wchar_t *s)
{
	int n;
	wchar_t *p;
	if (!s) return NULL;
	n = lstrlenW(s);
	p = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)(n + 1) * sizeof(wchar_t));
	if (!p) return NULL;
	lstrcpyW(p, s);
	return p;
}

static int wv_starts_(const wchar_t *s, const wchar_t *pre)
{
	int i = 0;
	if (!s || !pre) return 0;
	while (pre[i]) {
		if (s[i] != pre[i]) return 0;
		i++;
	}
	return 1;
}

static WvToken *wv_token_new_(void)
{
	WvToken *t = (WvToken *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		sizeof(WvToken));
	if (t) { t->ref = 1; t->alive = 1; }
	return t;
}

static void wv_token_addref_(WvToken *t)
{
	if (t) InterlockedIncrement(&t->ref);
}

static void wv_token_release_(WvToken *t)
{
	if (t && InterlockedDecrement(&t->ref) == 0)
		HeapFree(GetProcessHeap(), 0, t);
}

/* ── 处理器基类 ───────────────────────────────────────────────────── */

static HRESULT wv_h_qi_(WvHdr *h, REFIID iid, void **pp)
{
	if (!pp) return E_POINTER;
	if (IsEqualGUID(iid, &IID_IUnknown) ||
		(h->iid && IsEqualGUID(iid, h->iid))) {
		*pp = h;
		InterlockedIncrement(&h->ref);
		return S_OK;
	}
	*pp = NULL;
	return E_NOINTERFACE;
}

static ULONG wv_h_addref_(WvHdr *h)
{
	return (ULONG)InterlockedIncrement(&h->ref);
}

static ULONG wv_h_release_(WvHdr *h)
{
	LONG v = InterlockedDecrement(&h->ref);
	if (v == 0) {
		wv_token_release_(h->tok);
		HeapFree(GetProcessHeap(), 0, h);
	}
	return (ULONG)v;
}

static int wv_h_alive_(const WvHdr *h)
{
	return h && h->tok && h->tok->alive;
}

#define WV_COM3(NAME, IFACE) \
	static HRESULT STDMETHODCALLTYPE NAME##_QI(IFACE *This, REFIID iid, \
		void **pp) { return wv_h_qi_((WvHdr *)This, iid, pp); } \
	static ULONG STDMETHODCALLTYPE NAME##_AddRef(IFACE *This) \
	{ return wv_h_addref_((WvHdr *)This); } \
	static ULONG STDMETHODCALLTYPE NAME##_Release(IFACE *This) \
	{ return wv_h_release_((WvHdr *)This); }

/* ── Loader ───────────────────────────────────────────────────────── */

typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateEnv)(
	PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
	ICoreWebView2EnvironmentOptions *options,
	ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler);

static HMODULE s_loader;
static PFN_CreateEnv s_createEnv;

static int wv_loader_(void)
{
	if (s_createEnv) return 1;
	if (!s_loader) s_loader = LoadLibraryW(L"WebView2Loader.dll");
	if (!s_loader) return 0;
	s_createEnv = (PFN_CreateEnv)GetProcAddress(s_loader,
		"CreateCoreWebView2EnvironmentWithOptions");
	return s_createEnv != NULL;
}

/* ── UiWebView ────────────────────────────────────────────────────── */

struct UiWebView {
	Gfx *gfx;
	UiId id;
	HWND hwnd;
	WvToken *token;

	ICoreWebView2Environment *env;
	ICoreWebView2Controller *controller;
	ICoreWebView2 *webview;
	ICoreWebView2CompositionController *comp;
	ICoreWebView2Settings *settings;
	void *visual;   /* IDCompositionVisual* (gfx 子视觉, 由本控件持有) */

	wchar_t *homepage;
	wchar_t *cache;
	wchar_t *args;
	unsigned flags;
	GfxColor bg;
	double zoom;

	UiRect rc;              /* DIP */
	float scale;
	int visible;
	int hostShown;          /* 宿主窗口未最小化/隐藏 */
	int focused;
	int windowed;           /* 1 = 窗口模式回退 */
	int compFailed;         /* 视觉托管失败过, 不再重试 */
	int envShared;
	int creating;
	int state;
	int error;
	int tracking;
	int capturing;

	UiWebViewEvents ev;
};

#define WV_ADDREF(p) ((p)->lpVtbl->AddRef(p))
#define WV_RELEASE(p) ((p)->lpVtbl->Release(p))
#define WV_QI(p, iid, out) \
	((p)->lpVtbl->QueryInterface((p), (iid), (void **)(out)))

static WvHdr *wv_hnew_(size_t size, const void *vtbl, const IID *iid,
	struct UiWebView *wv)
{
	WvHdr *h = (WvHdr *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
	if (!h) return NULL;
	h->vtbl = (void *)vtbl;
	h->ref = 1;
	h->tok = wv->token;
	wv_token_addref_(h->tok);
	h->wv = wv;
	h->iid = iid;
	return h;
}

static void wv_fail_(struct UiWebView *wv, int hr);
static void wv_apply_settings_(struct UiWebView *wv);
static void wv_update_bounds_(struct UiWebView *wv);

/* ── 共享环境 ─────────────────────────────────────────────────────── */

static ICoreWebView2Environment *s_env;
static int s_envUsers;
static int s_envCreating;
static struct UiWebView **s_pending;
static int s_pendingN, s_pendingCap;

static int wv_pending_add_(struct UiWebView *wv)
{
	if (s_pendingN >= s_pendingCap) {
		int nc = s_pendingCap ? s_pendingCap * 2 : 8;
		struct UiWebView **np;
		if (s_pending) {
			np = (struct UiWebView **)HeapReAlloc(GetProcessHeap(), 0,
				s_pending, (SIZE_T)nc * sizeof(*np));
		} else {
			np = (struct UiWebView **)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)nc * sizeof(*np));
		}
		if (!np) return 0;
		s_pending = np;
		s_pendingCap = nc;
	}
	s_pending[s_pendingN++] = wv;
	return 1;
}

static void wv_pending_remove_(struct UiWebView *wv)
{
	int i;
	for (i = 0; i < s_pendingN; i++) {
		if (s_pending[i] == wv) {
			s_pending[i] = s_pending[--s_pendingN];
			return;
		}
	}
}

/* ── 事件处理器 ───────────────────────────────────────────────────── */

static void wv_nav_apply_(struct UiWebView *wv, const wchar_t *uri,
	int *cancel)
{
	if ((wv->flags & UI_WVF_MUST_HTTPS) && uri &&
		!wv_starts_(uri, L"https://") && !wv_starts_(uri, L"about:blank"))
		*cancel = 1;
	if (wv->ev.navigation_starting && uri)
		wv->ev.navigation_starting(wv->ev.user, wv, uri, cancel);
}

WV_COM3(wv_navstart, ICoreWebView2NavigationStartingEventHandler)
static HRESULT STDMETHODCALLTYPE wv_navstart_Invoke(
	ICoreWebView2NavigationStartingEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2NavigationStartingEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	wchar_t *uri = NULL;
	int cancel = 0;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) args->lpVtbl->get_Uri(args, &uri);
	wv_nav_apply_(h->wv, uri, &cancel);
	if (args && cancel) args->lpVtbl->put_Cancel(args, TRUE);
	if (uri) CoTaskMemFree(uri);
	return S_OK;
}

static const ICoreWebView2NavigationStartingEventHandlerVtbl
	wv_navstart_vtbl = {
	wv_navstart_QI, wv_navstart_AddRef, wv_navstart_Release,
	wv_navstart_Invoke
};

WV_COM3(wv_navdone, ICoreWebView2NavigationCompletedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_navdone_Invoke(
	ICoreWebView2NavigationCompletedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	struct UiWebView *wv;
	int ok = 0, err = 0;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	wv = h->wv;
	if (args) {
		BOOL succ = FALSE;
		COREWEBVIEW2_WEB_ERROR_STATUS st =
			COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
		args->lpVtbl->get_IsSuccess(args, &succ);
		args->lpVtbl->get_WebErrorStatus(args, &st);
		ok = succ ? 1 : 0;
		err = (int)st;
	}
	if (ok) wv->state = WV_STATE_READY;
	else wv->error = err;
	if (h->aux) {
		if (wv->ev.frame_navigation_completed)
			wv->ev.frame_navigation_completed(wv->ev.user, wv, ok, err);
	} else if (wv->ev.navigation_completed) {
		wv->ev.navigation_completed(wv->ev.user, wv, ok, err);
	}
	return S_OK;
}

static const ICoreWebView2NavigationCompletedEventHandlerVtbl
	wv_navdone_vtbl = {
	wv_navdone_QI, wv_navdone_AddRef, wv_navdone_Release, wv_navdone_Invoke
};

WV_COM3(wv_message, ICoreWebView2WebMessageReceivedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_message_Invoke(
	ICoreWebView2WebMessageReceivedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	LPWSTR json = NULL;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) args->lpVtbl->get_WebMessageAsJson(args, &json);
	if (json && h->wv->ev.web_message)
		h->wv->ev.web_message(h->wv->ev.user, h->wv, json);
	if (json) CoTaskMemFree(json);
	return S_OK;
}

static const ICoreWebView2WebMessageReceivedEventHandlerVtbl
	wv_message_vtbl = {
	wv_message_QI, wv_message_AddRef, wv_message_Release, wv_message_Invoke
};

WV_COM3(wv_procfail, ICoreWebView2ProcessFailedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_procfail_Invoke(
	ICoreWebView2ProcessFailedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2ProcessFailedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	int kind = 0;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) {
		COREWEBVIEW2_PROCESS_FAILED_KIND k =
			COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
		args->lpVtbl->get_ProcessFailedKind(args, &k);
		kind = (int)k;
	}
	if (h->wv->ev.process_failed)
		h->wv->ev.process_failed(h->wv->ev.user, h->wv, kind);
	return S_OK;
}

static const ICoreWebView2ProcessFailedEventHandlerVtbl wv_procfail_vtbl = {
	wv_procfail_QI, wv_procfail_AddRef, wv_procfail_Release,
	wv_procfail_Invoke
};

WV_COM3(wv_perm, ICoreWebView2PermissionRequestedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_perm_Invoke(
	ICoreWebView2PermissionRequestedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2PermissionRequestedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	int kind = 0, allow = 1;   /* DuiLib 默认放行 */
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) {
		COREWEBVIEW2_PERMISSION_KIND k =
			COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
		args->lpVtbl->get_PermissionKind(args, &k);
		kind = (int)k;
	}
	if (h->wv->ev.permission_requested)
		h->wv->ev.permission_requested(h->wv->ev.user, h->wv, kind, &allow);
	if (args)
		args->lpVtbl->put_State(args, allow
			? COREWEBVIEW2_PERMISSION_STATE_ALLOW
			: COREWEBVIEW2_PERMISSION_STATE_DENY);
	return S_OK;
}

static const ICoreWebView2PermissionRequestedEventHandlerVtbl
	wv_perm_vtbl = {
	wv_perm_QI, wv_perm_AddRef, wv_perm_Release, wv_perm_Invoke
};

WV_COM3(wv_cert, ICoreWebView2ServerCertificateErrorDetectedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_cert_Invoke(
	ICoreWebView2ServerCertificateErrorDetectedEventHandler *This,
	ICoreWebView2 *sender,
	ICoreWebView2ServerCertificateErrorDetectedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	LPWSTR uri = NULL;
	int allow = 0;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) args->lpVtbl->get_RequestUri(args, &uri);
	if (h->wv->ev.certificate_error && uri) {
		h->wv->ev.certificate_error(h->wv->ev.user, h->wv, uri, &allow);
		if (args) args->lpVtbl->put_Action(args, allow
			? COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW
			: COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_DEFAULT);
	}
	if (uri) CoTaskMemFree(uri);
	return S_OK;
}

static const ICoreWebView2ServerCertificateErrorDetectedEventHandlerVtbl
	wv_cert_vtbl = {
	wv_cert_QI, wv_cert_AddRef, wv_cert_Release, wv_cert_Invoke
};

WV_COM3(wv_res, ICoreWebView2WebResourceRequestedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_res_Invoke(
	ICoreWebView2WebResourceRequestedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2WebResourceRequestedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	ICoreWebView2WebResourceRequest *req = NULL;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) args->lpVtbl->get_Request(args, &req);
	if (h->wv->ev.web_resource_requested)
		h->wv->ev.web_resource_requested(h->wv->ev.user, h->wv,
			(IUnknown *)req, (IUnknown *)args);
	if (req) WV_RELEASE(req);
	return S_OK;
}

static const ICoreWebView2WebResourceRequestedEventHandlerVtbl
	wv_res_vtbl = {
	wv_res_QI, wv_res_AddRef, wv_res_Release, wv_res_Invoke
};

WV_COM3(wv_newwin, ICoreWebView2NewWindowRequestedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_newwin_Invoke(
	ICoreWebView2NewWindowRequestedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2NewWindowRequestedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	UiWebViewNewWindow nw;
	LPWSTR uri = NULL;
	BOOL userInitiated = FALSE;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	ZeroMemory(&nw, sizeof(nw));
	if (args) {
		args->lpVtbl->get_Uri(args, &uri);
		args->lpVtbl->get_IsUserInitiated(args, &userInitiated);
	}
	nw.uri = uri ? uri : L"";
	nw.user_initiated = userInitiated ? 1 : 0;
	nw.args = (IUnknown *)args;
	if (h->wv->ev.new_window)
		h->wv->ev.new_window(h->wv->ev.user, h->wv, &nw);
	if (nw.handled && args) args->lpVtbl->put_Handled(args, TRUE);
	if (uri) CoTaskMemFree(uri);
	return S_OK;
}

static const ICoreWebView2NewWindowRequestedEventHandlerVtbl
	wv_newwin_vtbl = {
	wv_newwin_QI, wv_newwin_AddRef, wv_newwin_Release, wv_newwin_Invoke
};

WV_COM3(wv_source, ICoreWebView2SourceChangedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_source_Invoke(
	ICoreWebView2SourceChangedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2SourceChangedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	LPWSTR uri = NULL;
	BOOL isNew = FALSE;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) args->lpVtbl->get_IsNewDocument(args, &isNew);
	if (sender) sender->lpVtbl->get_Source(sender, &uri);
	if (h->wv->ev.source_changed)
		h->wv->ev.source_changed(h->wv->ev.user, h->wv,
			uri ? uri : L"", isNew ? 1 : 0);
	if (uri) CoTaskMemFree(uri);
	return S_OK;
}

static const ICoreWebView2SourceChangedEventHandlerVtbl wv_source_vtbl = {
	wv_source_QI, wv_source_AddRef, wv_source_Release, wv_source_Invoke
};

WV_COM3(wv_history, ICoreWebView2HistoryChangedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_history_Invoke(
	ICoreWebView2HistoryChangedEventHandler *This,
	ICoreWebView2 *sender, IUnknown *args)
{
	WvHdr *h = (WvHdr *)This;
	BOOL back = FALSE, fwd = FALSE;
	(void)args;
	if (!wv_h_alive_(h)) return S_OK;
	if (sender) {
		sender->lpVtbl->get_CanGoBack(sender, &back);
		sender->lpVtbl->get_CanGoForward(sender, &fwd);
	}
	if (h->wv->ev.history_changed)
		h->wv->ev.history_changed(h->wv->ev.user, h->wv,
			back ? 1 : 0, fwd ? 1 : 0);
	return S_OK;
}

static const ICoreWebView2HistoryChangedEventHandlerVtbl wv_history_vtbl = {
	wv_history_QI, wv_history_AddRef, wv_history_Release, wv_history_Invoke
};

WV_COM3(wv_content, ICoreWebView2ContentLoadingEventHandler)
static HRESULT STDMETHODCALLTYPE wv_content_Invoke(
	ICoreWebView2ContentLoadingEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2ContentLoadingEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	BOOL isErr = FALSE;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) args->lpVtbl->get_IsErrorPage(args, &isErr);
	if (h->wv->ev.content_loading)
		h->wv->ev.content_loading(h->wv->ev.user, h->wv,
			isErr ? 1 : 0);
	return S_OK;
}

static const ICoreWebView2ContentLoadingEventHandlerVtbl wv_content_vtbl = {
	wv_content_QI, wv_content_AddRef, wv_content_Release, wv_content_Invoke
};

WV_COM3(wv_domready, ICoreWebView2DOMContentLoadedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_domready_Invoke(
	ICoreWebView2DOMContentLoadedEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2DOMContentLoadedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	UINT64 id = 0;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	if (args) args->lpVtbl->get_NavigationId(args, &id);
	if (h->wv->ev.dom_content_loaded)
		h->wv->ev.dom_content_loaded(h->wv->ev.user, h->wv,
			(unsigned long long)id);
	return S_OK;
}

static const ICoreWebView2DOMContentLoadedEventHandlerVtbl
	wv_domready_vtbl = {
	wv_domready_QI, wv_domready_AddRef, wv_domready_Release,
	wv_domready_Invoke
};

WV_COM3(wv_title, ICoreWebView2DocumentTitleChangedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_title_Invoke(
	ICoreWebView2DocumentTitleChangedEventHandler *This,
	ICoreWebView2 *sender, IUnknown *args)
{
	WvHdr *h = (WvHdr *)This;
	LPWSTR title = NULL;
	(void)args;
	if (!wv_h_alive_(h)) return S_OK;
	if (sender) sender->lpVtbl->get_DocumentTitle(sender, &title);
	if (h->wv->ev.document_title_changed)
		h->wv->ev.document_title_changed(h->wv->ev.user, h->wv,
			title ? title : L"");
	if (title) CoTaskMemFree(title);
	return S_OK;
}

static const ICoreWebView2DocumentTitleChangedEventHandlerVtbl
	wv_title_vtbl = {
	wv_title_QI, wv_title_AddRef, wv_title_Release, wv_title_Invoke
};

WV_COM3(wv_download, ICoreWebView2DownloadStartingEventHandler)
static HRESULT STDMETHODCALLTYPE wv_download_Invoke(
	ICoreWebView2DownloadStartingEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2DownloadStartingEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	UiWebViewDownload dl;
	ICoreWebView2DownloadOperation *op = NULL;
	LPWSTR uri = NULL, path = NULL;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	ZeroMemory(&dl, sizeof(dl));
	if (args) {
		args->lpVtbl->get_ResultFilePath(args, &path);
		if (SUCCEEDED(args->lpVtbl->get_DownloadOperation(args, &op)) &&
			op)
			op->lpVtbl->get_Uri(op, &uri);
	}
	dl.uri = uri ? uri : L"";
	dl.default_path = path ? path : L"";
	dl.args = (IUnknown *)args;
	if (h->wv->ev.download_starting)
		h->wv->ev.download_starting(h->wv->ev.user, h->wv, &dl);
	if (args) {
		if (dl.cancel) args->lpVtbl->put_Cancel(args, TRUE);
		if (dl.handled) args->lpVtbl->put_Handled(args, TRUE);
	}
	if (op) WV_RELEASE(op);
	if (uri) CoTaskMemFree(uri);
	if (path) CoTaskMemFree(path);
	return S_OK;
}

static const ICoreWebView2DownloadStartingEventHandlerVtbl
	wv_download_vtbl = {
	wv_download_QI, wv_download_AddRef, wv_download_Release,
	wv_download_Invoke
};

WV_COM3(wv_dialog, ICoreWebView2ScriptDialogOpeningEventHandler)
static HRESULT STDMETHODCALLTYPE wv_dialog_Invoke(
	ICoreWebView2ScriptDialogOpeningEventHandler *This,
	ICoreWebView2 *sender, ICoreWebView2ScriptDialogOpeningEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	UiWebViewScriptDialog dlg;
	LPWSTR msg = NULL, def = NULL;
	(void)sender;
	if (!wv_h_alive_(h)) return S_OK;
	ZeroMemory(&dlg, sizeof(dlg));
	if (args) {
		COREWEBVIEW2_SCRIPT_DIALOG_KIND k =
			COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT;
		args->lpVtbl->get_Kind(args, &k);
		switch (k) {
		case COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM:
			dlg.kind = UI_WV_DIALOG_CONFIRM; break;
		case COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT:
			dlg.kind = UI_WV_DIALOG_PROMPT; break;
		case COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD:
			dlg.kind = UI_WV_DIALOG_BEFOREUNLOAD; break;
		default:
			dlg.kind = UI_WV_DIALOG_ALERT; break;
		}
		args->lpVtbl->get_Message(args, &msg);
		args->lpVtbl->get_DefaultText(args, &def);
	}
	dlg.message = msg ? msg : L"";
	dlg.default_text = def ? def : L"";
	if (def) lstrcpynW(dlg.result_text, def, 512);
	if (h->wv->ev.script_dialog)
		h->wv->ev.script_dialog(h->wv->ev.user, h->wv, &dlg);
	if (args && dlg.handled) {
		if (dlg.kind == UI_WV_DIALOG_PROMPT && dlg.result_text[0])
			args->lpVtbl->put_ResultText(args, dlg.result_text);
		if (dlg.accept) args->lpVtbl->Accept(args);
	}
	if (msg) CoTaskMemFree(msg);
	if (def) CoTaskMemFree(def);
	return S_OK;
}

static const ICoreWebView2ScriptDialogOpeningEventHandlerVtbl
	wv_dialog_vtbl = {
	wv_dialog_QI, wv_dialog_AddRef, wv_dialog_Release, wv_dialog_Invoke
};

WV_COM3(wv_focus, ICoreWebView2FocusChangedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_gotfocus_Invoke(
	ICoreWebView2FocusChangedEventHandler *This,
	ICoreWebView2Controller *sender, IUnknown *args)
{
	WvHdr *h = (WvHdr *)This;
	(void)sender; (void)args;
	if (wv_h_alive_(h)) {
		h->wv->focused = 1;
		if (h->wv->ev.focus_changed)
			h->wv->ev.focus_changed(h->wv->ev.user, h->wv, 1);
	}
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE wv_lostfocus_Invoke(
	ICoreWebView2FocusChangedEventHandler *This,
	ICoreWebView2Controller *sender, IUnknown *args)
{
	WvHdr *h = (WvHdr *)This;
	(void)sender; (void)args;
	if (wv_h_alive_(h)) {
		h->wv->focused = 0;
		if (h->wv->ev.focus_changed)
			h->wv->ev.focus_changed(h->wv->ev.user, h->wv, 0);
	}
	return S_OK;
}

static const ICoreWebView2FocusChangedEventHandlerVtbl wv_gotfocus_vtbl = {
	wv_focus_QI, wv_focus_AddRef, wv_focus_Release, wv_gotfocus_Invoke
};
static const ICoreWebView2FocusChangedEventHandlerVtbl wv_lostfocus_vtbl = {
	wv_focus_QI, wv_focus_AddRef, wv_focus_Release, wv_lostfocus_Invoke
};

WV_COM3(wv_accel, ICoreWebView2AcceleratorKeyPressedEventHandler)
static HRESULT STDMETHODCALLTYPE wv_accel_Invoke(
	ICoreWebView2AcceleratorKeyPressedEventHandler *This,
	ICoreWebView2Controller *sender,
	ICoreWebView2AcceleratorKeyPressedEventArgs *args)
{
	WvHdr *h = (WvHdr *)This;
	int vk = 0, data = 0, handled = 0;
	(void)sender;
	if (!wv_h_alive_(h) || !args) return S_OK;
	{
		UINT v = 0;
		INT32 lp = 0;
		args->lpVtbl->get_VirtualKey(args, &v);
		args->lpVtbl->get_KeyEventLParam(args, &lp);
		vk = (int)v;
		data = (int)lp;
	}
	if (h->wv->ev.accelerator_key)
		h->wv->ev.accelerator_key(h->wv->ev.user, h->wv, vk, data,
			&handled);
	if (handled) args->lpVtbl->put_Handled(args, TRUE);
	return S_OK;
}

static const ICoreWebView2AcceleratorKeyPressedEventHandlerVtbl
	wv_accel_vtbl = {
	wv_accel_QI, wv_accel_AddRef, wv_accel_Release, wv_accel_Invoke
};

typedef struct WvScriptHdr {
	WvHdr h;
	UiWebViewScriptDone done;
	void *doneUser;
} WvScriptHdr;

WV_COM3(wv_script, ICoreWebView2ExecuteScriptCompletedHandler)
static HRESULT STDMETHODCALLTYPE wv_script_Invoke(
	ICoreWebView2ExecuteScriptCompletedHandler *This,
	HRESULT result, LPCWSTR json)
{
	WvScriptHdr *s = (WvScriptHdr *)This;
	if (wv_h_alive_(&s->h) && s->done)
		s->done(s->doneUser, (int)result, json);
	return S_OK;
}

static const ICoreWebView2ExecuteScriptCompletedHandlerVtbl
	wv_script_vtbl = {
	wv_script_QI, wv_script_AddRef, wv_script_Release, wv_script_Invoke
};

WV_COM3(wv_envdone,
	ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
WV_COM3(wv_compdone,
	ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler)
WV_COM3(wv_ctrldone,
	ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)

static void wv_attach_env_(struct UiWebView *wv, ICoreWebView2Environment *env)
{
	if (wv->envShared || !env) return;
	wv->env = env;
	WV_ADDREF(env);
	wv->envShared = 1;
	s_envUsers++;
}

static void wv_detach_env_(struct UiWebView *wv)
{
	if (!wv->envShared) return;
	wv->envShared = 0;
	if (wv->env) { WV_RELEASE(wv->env); wv->env = NULL; }
	if (--s_envUsers <= 0) {
		s_envUsers = 0;
		if (s_env) { WV_RELEASE(s_env); s_env = NULL; }
	}
}

static HRESULT STDMETHODCALLTYPE wv_compdone_Invoke(
	ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler *This,
	HRESULT result, ICoreWebView2CompositionController *controller);

static HRESULT STDMETHODCALLTYPE wv_ctrldone_Invoke(
	ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This,
	HRESULT result, ICoreWebView2Controller *controller);

static const ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandlerVtbl
	wv_compdone_vtbl = {
	wv_compdone_QI, wv_compdone_AddRef, wv_compdone_Release,
	wv_compdone_Invoke
};
static const ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl
	wv_ctrldone_vtbl = {
	wv_ctrldone_QI, wv_ctrldone_AddRef, wv_ctrldone_Release,
	wv_ctrldone_Invoke
};

/* 为指定窗口创建 controller; 返回 1 = 已发起 */
static int wv_create_controller_(struct UiWebView *wv,
	ICoreWebView2Environment *env)
{
	ICoreWebView2Environment3 *e3 = NULL;
	int ok = 0;
	if (!wv || !env || !wv->hwnd) return 0;
	/* 只有 DComp 根视觉存在时才用 CompositionController; 否则窗口模式. */
	if (!wv->compFailed && Gfx_IntDcomp(wv->gfx) && SUCCEEDED(WV_QI(env,
		&IID_ICoreWebView2Environment3, &e3)) && e3) {
		WvHdr *h = wv_hnew_(sizeof(WvHdr), &wv_compdone_vtbl,
			&IID_ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler,
			wv);
		if (h && SUCCEEDED(
			e3->lpVtbl->CreateCoreWebView2CompositionController(e3,
				wv->hwnd,
				(ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler *)h))) {
			wv->creating = 1;
			ok = 1;
		}
		if (h) wv_h_release_(h);
		WV_RELEASE(e3);
	}
	if (!ok) {
		WvHdr *h = wv_hnew_(sizeof(WvHdr), &wv_ctrldone_vtbl,
			&IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
			wv);
		if (h && SUCCEEDED(env->lpVtbl->CreateCoreWebView2Controller(env,
			wv->hwnd,
			(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)h))) {
			wv->creating = 1;
			ok = 1;
		}
		if (h) wv_h_release_(h);
	}
	return ok;
}

/* 环境就绪后不能立刻建 controller: 首帧前 gfx 还没决定 DComp/ULW.
 * 等后端确定后再选 CompositionController 或窗口模式. */
static void wv_maybe_controller_(struct UiWebView *wv)
{
	if (!wv || !wv->env || wv->controller || wv->creating ||
		wv->state == WV_STATE_FAILED)
		return;
	if (!Gfx_IntRenderMode(wv->gfx)) return;
	if (!wv_create_controller_(wv, wv->env))
		wv_fail_(wv, E_FAIL);
}

static void wv_setup_events_(struct UiWebView *wv)
{
	EventRegistrationToken tok;
	if (!wv->webview || !wv->controller) return;
	{
		WvHdr *h = wv_hnew_(sizeof(WvHdr), &wv_navstart_vtbl,
			&IID_ICoreWebView2NavigationStartingEventHandler, wv);
		if (h) {
			wv->webview->lpVtbl->add_NavigationStarting(wv->webview,
				(ICoreWebView2NavigationStartingEventHandler *)h, &tok);
			wv_h_release_(h);
		}
		h = wv_hnew_(sizeof(WvHdr), &wv_navdone_vtbl,
			&IID_ICoreWebView2NavigationCompletedEventHandler, wv);
		if (h) {
			wv->webview->lpVtbl->add_NavigationCompleted(wv->webview,
				(ICoreWebView2NavigationCompletedEventHandler *)h, &tok);
			wv_h_release_(h);
		}
		h = wv_hnew_(sizeof(WvHdr), &wv_navdone_vtbl,
			&IID_ICoreWebView2NavigationCompletedEventHandler, wv);
		if (h) {
			h->aux = 1;
			wv->webview->lpVtbl->add_FrameNavigationCompleted(
				wv->webview,
				(ICoreWebView2NavigationCompletedEventHandler *)h, &tok);
			wv_h_release_(h);
		}
		h = wv_hnew_(sizeof(WvHdr), &wv_message_vtbl,
			&IID_ICoreWebView2WebMessageReceivedEventHandler, wv);
		if (h) {
			wv->webview->lpVtbl->add_WebMessageReceived(wv->webview,
				(ICoreWebView2WebMessageReceivedEventHandler *)h, &tok);
			wv_h_release_(h);
		}
		h = wv_hnew_(sizeof(WvHdr), &wv_procfail_vtbl,
			&IID_ICoreWebView2ProcessFailedEventHandler, wv);
		if (h) {
			wv->webview->lpVtbl->add_ProcessFailed(wv->webview,
				(ICoreWebView2ProcessFailedEventHandler *)h, &tok);
			wv_h_release_(h);
		}
		h = wv_hnew_(sizeof(WvHdr), &wv_perm_vtbl,
			&IID_ICoreWebView2PermissionRequestedEventHandler, wv);
		if (h) {
			wv->webview->lpVtbl->add_PermissionRequested(wv->webview,
				(ICoreWebView2PermissionRequestedEventHandler *)h, &tok);
			wv_h_release_(h);
		}
		{
			ICoreWebView2_14 *w14 = NULL;
			if (SUCCEEDED(WV_QI(wv->webview, &IID_ICoreWebView2_14,
				&w14)) && w14) {
				WvHdr *c = wv_hnew_(sizeof(WvHdr), &wv_cert_vtbl,
					&IID_ICoreWebView2ServerCertificateErrorDetectedEventHandler,
					wv);
				if (c) {
					w14->lpVtbl->add_ServerCertificateErrorDetected(w14,
						(ICoreWebView2ServerCertificateErrorDetectedEventHandler *)c,
						&tok);
					wv_h_release_(c);
				}
				WV_RELEASE(w14);
			}
		}
		h = wv_hnew_(sizeof(WvHdr), &wv_res_vtbl,
			&IID_ICoreWebView2WebResourceRequestedEventHandler, wv);
		if (h) {
			wv->webview->lpVtbl->add_WebResourceRequested(wv->webview,
				(ICoreWebView2WebResourceRequestedEventHandler *)h, &tok);
			wv_h_release_(h);
		}
		if (wv->ev.web_resource_requested)
			wv->webview->lpVtbl->AddWebResourceRequestedFilter(
				wv->webview, L"*",
				COREWEBVIEW2_WEB_RESOURCE_CONTEXT_XML_HTTP_REQUEST);
		if (wv->ev.new_window) {
			h = wv_hnew_(sizeof(WvHdr), &wv_newwin_vtbl,
				&IID_ICoreWebView2NewWindowRequestedEventHandler, wv);
			if (h) {
				wv->webview->lpVtbl->add_NewWindowRequested(
					wv->webview,
					(ICoreWebView2NewWindowRequestedEventHandler *)h,
					&tok);
				wv_h_release_(h);
			}
		}
		if (wv->ev.source_changed) {
			h = wv_hnew_(sizeof(WvHdr), &wv_source_vtbl,
				&IID_ICoreWebView2SourceChangedEventHandler, wv);
			if (h) {
				wv->webview->lpVtbl->add_SourceChanged(wv->webview,
					(ICoreWebView2SourceChangedEventHandler *)h, &tok);
				wv_h_release_(h);
			}
		}
		if (wv->ev.history_changed) {
			h = wv_hnew_(sizeof(WvHdr), &wv_history_vtbl,
				&IID_ICoreWebView2HistoryChangedEventHandler, wv);
			if (h) {
				wv->webview->lpVtbl->add_HistoryChanged(wv->webview,
					(ICoreWebView2HistoryChangedEventHandler *)h, &tok);
				wv_h_release_(h);
			}
		}
		if (wv->ev.content_loading) {
			h = wv_hnew_(sizeof(WvHdr), &wv_content_vtbl,
				&IID_ICoreWebView2ContentLoadingEventHandler, wv);
			if (h) {
				wv->webview->lpVtbl->add_ContentLoading(wv->webview,
					(ICoreWebView2ContentLoadingEventHandler *)h, &tok);
				wv_h_release_(h);
			}
		}
		if (wv->ev.document_title_changed) {
			h = wv_hnew_(sizeof(WvHdr), &wv_title_vtbl,
				&IID_ICoreWebView2DocumentTitleChangedEventHandler, wv);
			if (h) {
				wv->webview->lpVtbl->add_DocumentTitleChanged(
					wv->webview,
					(ICoreWebView2DocumentTitleChangedEventHandler *)h,
					&tok);
				wv_h_release_(h);
			}
		}
		if (wv->ev.script_dialog) {
			h = wv_hnew_(sizeof(WvHdr), &wv_dialog_vtbl,
				&IID_ICoreWebView2ScriptDialogOpeningEventHandler, wv);
			if (h) {
				wv->webview->lpVtbl->add_ScriptDialogOpening(
					wv->webview,
					(ICoreWebView2ScriptDialogOpeningEventHandler *)h,
					&tok);
				wv_h_release_(h);
			}
		}
		if (wv->ev.dom_content_loaded) {
			ICoreWebView2_2 *w2 = NULL;
			if (SUCCEEDED(WV_QI(wv->webview, &IID_ICoreWebView2_2,
				&w2)) && w2) {
				WvHdr *d = wv_hnew_(sizeof(WvHdr),
					&wv_domready_vtbl,
					&IID_ICoreWebView2DOMContentLoadedEventHandler,
					wv);
				if (d) {
					w2->lpVtbl->add_DOMContentLoaded(w2,
						(ICoreWebView2DOMContentLoadedEventHandler *)d,
						&tok);
					wv_h_release_(d);
				}
				WV_RELEASE(w2);
			}
		}
		if (wv->ev.download_starting) {
			ICoreWebView2_4 *w4 = NULL;
			if (SUCCEEDED(WV_QI(wv->webview, &IID_ICoreWebView2_4,
				&w4)) && w4) {
				WvHdr *d = wv_hnew_(sizeof(WvHdr),
					&wv_download_vtbl,
					&IID_ICoreWebView2DownloadStartingEventHandler,
					wv);
				if (d) {
					w4->lpVtbl->add_DownloadStarting(w4,
						(ICoreWebView2DownloadStartingEventHandler *)d,
						&tok);
					wv_h_release_(d);
				}
				WV_RELEASE(w4);
			}
		}
	}
	{
		WvHdr *g = wv_hnew_(sizeof(WvHdr), &wv_gotfocus_vtbl,
			&IID_ICoreWebView2FocusChangedEventHandler, wv);
		WvHdr *l = wv_hnew_(sizeof(WvHdr), &wv_lostfocus_vtbl,
			&IID_ICoreWebView2FocusChangedEventHandler, wv);
		WvHdr *a = wv_hnew_(sizeof(WvHdr), &wv_accel_vtbl,
			&IID_ICoreWebView2AcceleratorKeyPressedEventHandler, wv);
		if (g) {
			wv->controller->lpVtbl->add_GotFocus(wv->controller,
				(ICoreWebView2FocusChangedEventHandler *)g, &tok);
			wv_h_release_(g);
		}
		if (l) {
			wv->controller->lpVtbl->add_LostFocus(wv->controller,
				(ICoreWebView2FocusChangedEventHandler *)l, &tok);
			wv_h_release_(l);
		}
		if (a) {
			wv->controller->lpVtbl->add_AcceleratorKeyPressed(
				wv->controller,
				(ICoreWebView2AcceleratorKeyPressedEventHandler *)a, &tok);
			wv_h_release_(a);
		}
	}
}

static void wv_apply_settings_(struct UiWebView *wv)
{
	ICoreWebView2Settings *s = wv->settings;
	unsigned f = wv->flags;
	if (s) {
		s->lpVtbl->put_IsScriptEnabled(s,
			(f & UI_WVF_SCRIPT) ? TRUE : FALSE);
		/* 提供 script_dialog 回调时关闭内置对话框, 否则事件不会交给宿主. */
		s->lpVtbl->put_AreDefaultScriptDialogsEnabled(s,
			((f & UI_WVF_DEFAULT_DIALOGS) && !wv->ev.script_dialog)
				? TRUE : FALSE);
		s->lpVtbl->put_IsWebMessageEnabled(s,
			(f & UI_WVF_WEB_MESSAGE) ? TRUE : FALSE);
		s->lpVtbl->put_IsBuiltInErrorPageEnabled(s,
			(f & UI_WVF_ERROR_PAGE) ? TRUE : FALSE);
		s->lpVtbl->put_AreDefaultContextMenusEnabled(s,
			(f & UI_WVF_CONTEXT_MENUS) ? TRUE : FALSE);
		s->lpVtbl->put_IsStatusBarEnabled(s,
			(f & UI_WVF_STATUS_BAR) ? TRUE : FALSE);
		s->lpVtbl->put_IsZoomControlEnabled(s,
			(f & UI_WVF_ZOOM_CONTROL) ? TRUE : FALSE);
		s->lpVtbl->put_AreDevToolsEnabled(s,
			(f & UI_WVF_DEVTOOLS) ? TRUE : FALSE);
	}
	if (s) {
		ICoreWebView2Settings3 *s3 = NULL;
		if (SUCCEEDED(WV_QI(s, &IID_ICoreWebView2Settings3, &s3)) && s3) {
			s3->lpVtbl->put_AreBrowserAcceleratorKeysEnabled(s3,
				(f & UI_WVF_ACCELERATOR_KEYS) ? TRUE : FALSE);
			WV_RELEASE(s3);
		}
	}
	if (wv->controller) {
		ICoreWebView2Controller4 *c4 = NULL;
		if (SUCCEEDED(WV_QI(wv->controller,
			&IID_ICoreWebView2Controller4, &c4)) && c4) {
			c4->lpVtbl->put_AllowExternalDrop(c4,
				(f & UI_WVF_EXTERNAL_DROP) ? TRUE : FALSE);
			WV_RELEASE(c4);
		}
	}
}

static void wv_update_bounds_(struct UiWebView *wv)
{
	if (!wv || !wv->controller) return;
	wv->scale = Gfx_IntScale(wv->gfx);
	if (wv->windowed) {
		RECT r;
		r.left = (LONG)(wv->rc.x * wv->scale);
		r.top = (LONG)(wv->rc.y * wv->scale);
		r.right = (LONG)((wv->rc.x + wv->rc.w) * wv->scale);
		r.bottom = (LONG)((wv->rc.y + wv->rc.h) * wv->scale);
		wv->controller->lpVtbl->put_Bounds(wv->controller, r);
	} else {
		RECT r;
		ICoreWebView2Controller3 *c3 = NULL;
		r.left = 0;
		r.top = 0;
		/* Visual hosting: Bounds 是物理像素; 输出视觉尺寸 = Bounds /
		 * RasterizationScale, 与 DComp 视觉的物理像素偏移/裁剪一致. */
		r.right = (LONG)((wv->rc.w > 1.0f ? wv->rc.w : 1.0f) *
			wv->scale);
		r.bottom = (LONG)((wv->rc.h > 1.0f ? wv->rc.h : 1.0f) *
			wv->scale);
		wv->controller->lpVtbl->put_Bounds(wv->controller, r);
		if (SUCCEEDED(WV_QI(wv->controller,
			&IID_ICoreWebView2Controller3, &c3)) && c3) {
			c3->lpVtbl->put_RasterizationScale(c3, wv->scale);
			WV_RELEASE(c3);
		}
		if (wv->visual)
			Gfx_IntUpdateChildVisual(wv->gfx, wv->visual, wv->rc,
				wv->visible && wv->hostShown);
	}
}

static void wv_ready_(struct UiWebView *wv, ICoreWebView2Controller *ctrl,
	int windowed)
{
	ICoreWebView2 *core = NULL;
	wv->windowed = windowed;
	wv->controller = ctrl;
	WV_ADDREF(ctrl);
	ctrl->lpVtbl->get_CoreWebView2(ctrl, &core);
	wv->webview = core;
	if (core) core->lpVtbl->get_Settings(core, &wv->settings);

	if (wv->settings) wv_apply_settings_(wv);
	{
		ICoreWebView2Controller2 *c2 = NULL;
		if (SUCCEEDED(WV_QI(ctrl, &IID_ICoreWebView2Controller2,
			&c2)) && c2) {
			COREWEBVIEW2_COLOR col;
			col.A = (BYTE)((wv->bg >> 24) & 0xFF);
			col.R = (BYTE)((wv->bg >> 16) & 0xFF);
			col.G = (BYTE)((wv->bg >> 8) & 0xFF);
			col.B = (BYTE)(wv->bg & 0xFF);
			c2->lpVtbl->put_DefaultBackgroundColor(c2, col);
			WV_RELEASE(c2);
		}
	}
	if (!windowed && wv->comp) {
		if (!wv->visual)
			wv->visual = Gfx_IntCreateChildVisual(wv->gfx, wv->rc, 1);
		if (wv->visual) {
			wv->comp->lpVtbl->put_RootVisualTarget(wv->comp,
				(IUnknown *)wv->visual);
			Gfx_IntCommit(wv->gfx);
		}
	}
	ctrl->lpVtbl->put_IsVisible(ctrl,
		(wv->visible && wv->hostShown) ? TRUE : FALSE);
	if (wv->zoom != 1.0)
		ctrl->lpVtbl->put_ZoomFactor(ctrl, wv->zoom);
	wv_update_bounds_(wv);
	wv_setup_events_(wv);
	wv->state = WV_STATE_READY;
	wv->creating = 0;
	if (wv->ev.created) wv->ev.created(wv->ev.user, wv);
	if ((wv->flags & UI_WVF_AUTO_NAVIGATE) && wv->webview)
		wv->webview->lpVtbl->Navigate(wv->webview,
			wv->homepage ? wv->homepage : L"about:blank");
}

static HRESULT STDMETHODCALLTYPE wv_compdone_Invoke(
	ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler *This,
	HRESULT result, ICoreWebView2CompositionController *controller)
{
	WvHdr *h = (WvHdr *)This;
	ICoreWebView2Controller *base = NULL;
	if (!wv_h_alive_(h)) {
		if (controller && SUCCEEDED(WV_QI(controller,
			&IID_ICoreWebView2Controller, &base)) && base) {
			base->lpVtbl->Close(base);
			WV_RELEASE(base);
		}
		return S_OK;
	}
	if (FAILED(result) || !controller) {
		/* 视觉托管创建失败时退回窗口模式 (不再重试 composition). */
		h->wv->compFailed = 1;
		if (!wv_create_controller_(h->wv, h->wv->env))
			wv_fail_(h->wv, FAILED(result) ? (int)result : E_FAIL);
		return S_OK;
	}
	h->wv->comp = controller;
	WV_ADDREF(controller);
	if (SUCCEEDED(WV_QI(controller, &IID_ICoreWebView2Controller,
		&base)) && base) {
		wv_ready_(h->wv, base, 0);
		WV_RELEASE(base);
	} else {
		wv_fail_(h->wv, E_NOINTERFACE);
	}
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE wv_ctrldone_Invoke(
	ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This,
	HRESULT result, ICoreWebView2Controller *controller)
{
	WvHdr *h = (WvHdr *)This;
	if (!wv_h_alive_(h)) {
		if (controller) controller->lpVtbl->Close(controller);
		return S_OK;
	}
	if (FAILED(result) || !controller) {
		wv_fail_(h->wv, FAILED(result) ? (int)result : E_FAIL);
		return S_OK;
	}
	wv_ready_(h->wv, controller, 1);
	return S_OK;
}

/* 环境创建完成: 分发给等待队列 */
static HRESULT STDMETHODCALLTYPE wv_envdone_Invoke(
	ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This,
	HRESULT result, ICoreWebView2Environment *env)
{
	WvHdr *h = (WvHdr *)This;
	struct UiWebView **list;
	int n, i;
	(void)h;
	s_envCreating = 0;
	list = s_pending;
	n = s_pendingN;
	s_pending = NULL;
	s_pendingN = s_pendingCap = 0;
	if (SUCCEEDED(result) && env) {
		WV_ADDREF(env);
		s_env = env;
	} else {
		s_env = NULL;
	}
	for (i = 0; i < n; i++) {
		struct UiWebView *w = list[i];
		if (!w || !w->token || !w->token->alive) continue;
		if (!s_env) {
			wv_fail_(w, FAILED(result) ? (int)result : E_FAIL);
			continue;
		}
		wv_attach_env_(w, s_env);
		wv_maybe_controller_(w);
	}
	if (list) HeapFree(GetProcessHeap(), 0, list);
	if (s_env && s_envUsers <= 0) {
		WV_RELEASE(s_env);
		s_env = NULL;
	}
	return S_OK;
}

static const ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl
	wv_envdone_vtbl = {
	wv_envdone_QI, wv_envdone_AddRef, wv_envdone_Release, wv_envdone_Invoke
};

/* ── 启动 ─────────────────────────────────────────────────────────── */

static wchar_t *wv_default_cache_(void)
{
	wchar_t path[MAX_PATH];
	if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path)))
		return NULL;
	lstrcatW(path, L"\\CUI\\WebView2");
	SHCreateDirectoryExW(NULL, path, NULL);
	return wv_strdup_(path);
}

static void wv_fail_(struct UiWebView *wv, int hr)
{
	if (!wv) return;
	wv->state = WV_STATE_FAILED;
	wv->error = hr;
	if (wv->ev.create_failed)
		wv->ev.create_failed(wv->ev.user, wv, hr);
}

static void wv_start_(struct UiWebView *wv)
{
	WvHdr *h;
	wchar_t *args = NULL;
	int alen = 0;

	if (!wv_loader_()) {
		wv_fail_(wv, HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND));
		return;
	}
	if (!wv->hwnd || !Gfx_IntHwnd(wv->gfx)) {
		wv_fail_(wv, E_HANDLE);
		return;
	}
	if (s_env) {
		wv_attach_env_(wv, s_env);
		wv_maybe_controller_(wv);
		return;
	}
	if (!wv_pending_add_(wv)) {
		wv_fail_(wv, E_OUTOFMEMORY);
		return;
	}
	if (s_envCreating) return;
	s_envCreating = 1;
	SetEnvironmentVariableW(L"COREWEBVIEW2_FORCED_HOSTING_MODE",
		L"COREWEBVIEW2_HOSTING_MODE_WINDOW_TO_VISUAL");

	if (wv->args) alen += lstrlenW(wv->args) + 1;
	if (wv->flags & UI_WVF_DISABLE_GPU) alen += 16;
	if (wv->flags & UI_WVF_DISABLE_GPU_COMP) alen += 32;
	args = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)(alen + 2) * sizeof(wchar_t));
	if (args) {
		args[0] = 0;
		if (wv->args && wv->args[0]) {
			lstrcatW(args, wv->args);
			lstrcatW(args, L" ");
		}
		if (wv->flags & UI_WVF_DISABLE_GPU)
			lstrcatW(args, L"--disable-gpu ");
		if (wv->flags & UI_WVF_DISABLE_GPU_COMP)
			lstrcatW(args, L"--disable-gpu-composition ");
	}
	/* 共享环境只有一份, 参数走进程级环境变量; 后续控件复用同一环境. */
	if (args && args[0])
		SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
			args);
	h = wv_hnew_(sizeof(WvHdr), &wv_envdone_vtbl,
		&IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
		wv);
	if (!h) {
		s_envCreating = 0;
		wv_pending_remove_(wv);
		wv_fail_(wv, E_OUTOFMEMORY);
	} else {
		HRESULT hr = s_createEnv(NULL, wv->cache, NULL,
			(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)h);
		if (FAILED(hr)) {
			s_envCreating = 0;
			wv_pending_remove_(wv);
			wv_fail_(wv, (int)hr);
		}
	}
	if (h) wv_h_release_(h);
	if (args) HeapFree(GetProcessHeap(), 0, args);
}

/* ── 公共 API ─────────────────────────────────────────────────────── */

void UiWebViewConfig_Default(UiWebViewConfig *cfg)
{
	if (!cfg) return;
	ZeroMemory(cfg, sizeof(*cfg));
	cfg->flags = UI_WVF_SCRIPT | UI_WVF_DEFAULT_DIALOGS |
		UI_WVF_WEB_MESSAGE | UI_WVF_AUTO_NAVIGATE | UI_WVF_MUST_HTTPS;
	cfg->background = 0xFFFFFFFFu;
	cfg->zoom = 1.0;
}

UiWebView *UiWebView_Create(Gfx *gfx, UiId id, const UiWebViewConfig *cfg,
	const UiWebViewEvents *ev)
{
	UiWebView *wv;
	UiWebViewConfig def;
	if (!cfg) { UiWebViewConfig_Default(&def); cfg = &def; }
	wv = (UiWebView *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		sizeof(UiWebView));
	if (!wv) return NULL;
	wv->gfx = gfx;
	wv->id = id;
	wv->hwnd = Gfx_IntHwnd(gfx);
	wv->flags = cfg->flags;
	wv->bg = cfg->background ? cfg->background : 0xFFFFFFFFu;
	wv->zoom = cfg->zoom > 0.0 ? cfg->zoom : 1.0;
	wv->visible = 1;
	wv->hostShown = 1;
	wv->state = WV_STATE_IDLE;
	wv->rc.x = 0.0f; wv->rc.y = 0.0f;
	wv->rc.w = 1.0f; wv->rc.h = 1.0f;
	wv->token = wv_token_new_();
	wv->homepage = wv_strdup_(cfg->homepage ? cfg->homepage : L"about:blank");
	wv->cache = cfg->cacheFolder ? wv_strdup_(cfg->cacheFolder)
		: wv_default_cache_();
	wv->args = wv_strdup_(cfg->browserArgs);
	if (ev) wv->ev = *ev;
	if (!wv->token) {
		UiWebView_Destroy(wv);
		return NULL;
	}
	wv_start_(wv);
	return wv;
}

void UiWebView_Destroy(UiWebView *wv)
{
	if (!wv) return;
	if (wv->token) wv->token->alive = 0;
	wv_pending_remove_(wv);
	if (wv->comp)
		wv->comp->lpVtbl->put_RootVisualTarget(wv->comp, NULL);

	if (wv->controller)
		wv->controller->lpVtbl->Close(wv->controller);
	if (wv->visual) {
		Gfx_IntDestroyChildVisual(wv->gfx, wv->visual);
		wv->visual = NULL;
	}
	if (wv->settings) { WV_RELEASE(wv->settings); wv->settings = NULL; }
	if (wv->webview) { WV_RELEASE(wv->webview); wv->webview = NULL; }
	if (wv->comp) { WV_RELEASE(wv->comp); wv->comp = NULL; }
	if (wv->controller) {
		WV_RELEASE(wv->controller);
		wv->controller = NULL;
	}
	wv_detach_env_(wv);
	if (wv->homepage) HeapFree(GetProcessHeap(), 0, wv->homepage);
	if (wv->cache) HeapFree(GetProcessHeap(), 0, wv->cache);
	if (wv->args) HeapFree(GetProcessHeap(), 0, wv->args);
	if (wv->token) wv_token_release_(wv->token);
	HeapFree(GetProcessHeap(), 0, wv);
}

int Ui_WebView(UiWebView *wv, UiContext *ui, UiRect rc)
{
	if (!wv) return 0;
	wv_maybe_controller_(wv);
	if (rc.w != wv->rc.w || rc.h != wv->rc.h ||
		rc.x != wv->rc.x || rc.y != wv->rc.y) {
		wv->rc = rc;
		if (wv->controller) wv_update_bounds_(wv);
	}
	(void)ui;
	return 0;
}

void UiWebView_SetHomepage(UiWebView *wv, const wchar_t *url)
{
	if (!wv) return;
	if (wv->homepage) HeapFree(GetProcessHeap(), 0, wv->homepage);
	wv->homepage = wv_strdup_(url ? url : L"about:blank");
}

const wchar_t *UiWebView_Homepage(const UiWebView *wv)
{
	return (wv && wv->homepage) ? wv->homepage : L"about:blank";
}

void UiWebView_GoHome(UiWebView *wv)
{
	if (wv && wv->webview)
		wv->webview->lpVtbl->Navigate(wv->webview,
			wv->homepage ? wv->homepage : L"about:blank");
}

void UiWebView_Navigate(UiWebView *wv, const wchar_t *url)
{
	if (wv && wv->webview && url)
		wv->webview->lpVtbl->Navigate(wv->webview, url);
}

void UiWebView_Reload(UiWebView *wv)
{
	if (wv && wv->webview) wv->webview->lpVtbl->Reload(wv->webview);
}

int UiWebView_CanGoBack(const UiWebView *wv)
{
	BOOL v = FALSE;
	if (wv && wv->webview)
		wv->webview->lpVtbl->get_CanGoBack(wv->webview, &v);
	return v ? 1 : 0;
}

int UiWebView_CanGoForward(const UiWebView *wv)
{
	BOOL v = FALSE;
	if (wv && wv->webview)
		wv->webview->lpVtbl->get_CanGoForward(wv->webview, &v);
	return v ? 1 : 0;
}

void UiWebView_GoBack(UiWebView *wv)
{
	if (wv && wv->webview)
		wv->webview->lpVtbl->GoBack(wv->webview);
}

void UiWebView_GoForward(UiWebView *wv)
{
	if (wv && wv->webview)
		wv->webview->lpVtbl->GoForward(wv->webview);
}

void UiWebView_PostWebMessageJson(UiWebView *wv, const wchar_t *json)
{
	if (wv && wv->webview && json)
		wv->webview->lpVtbl->PostWebMessageAsJson(wv->webview, json);
}

void UiWebView_PostWebMessageString(UiWebView *wv, const wchar_t *text)
{
	if (wv && wv->webview && text)
		wv->webview->lpVtbl->PostWebMessageAsString(wv->webview, text);
}

void UiWebView_PostWebMessage(UiWebView *wv, const wchar_t *json)
{
	UiWebView_PostWebMessageJson(wv, json);
}

void UiWebView_AddScriptOnDocumentCreated(UiWebView *wv,
	const wchar_t *script)
{
	if (wv && wv->webview && script)
		wv->webview->lpVtbl->AddScriptToExecuteOnDocumentCreated(
			wv->webview, script, NULL);
}

void UiWebView_ExecuteScript(UiWebView *wv, const wchar_t *script,
	UiWebViewScriptDone done, void *user)
{
	WvScriptHdr *s;
	if (!wv || !wv->webview || !script) return;
	s = (WvScriptHdr *)wv_hnew_(sizeof(WvScriptHdr), &wv_script_vtbl,
		&IID_ICoreWebView2ExecuteScriptCompletedHandler, wv);
	if (!s) return;
	s->done = done;
	s->doneUser = user;
	wv->webview->lpVtbl->ExecuteScript(wv->webview, script,
		(ICoreWebView2ExecuteScriptCompletedHandler *)s);
	wv_h_release_(&s->h);
}

int UiWebView_AddHostObject(UiWebView *wv, const wchar_t *name, IDispatch *obj)
{
	VARIANT v;
	HRESULT hr;
	if (!wv || !wv->webview || !name) return 0;
	if (!obj) {
		wv->webview->lpVtbl->RemoveHostObjectFromScript(wv->webview, name);
		return 1;
	}
	ZeroMemory(&v, sizeof(v));
	v.vt = VT_DISPATCH;
	v.pdispVal = obj;
	WV_ADDREF(obj);
	hr = wv->webview->lpVtbl->AddHostObjectToScript(wv->webview, name, &v);
	WV_RELEASE(obj);
	return SUCCEEDED(hr) ? 1 : 0;
}

int UiWebView_SetVirtualHost(UiWebView *wv, const wchar_t *hostName,
	const wchar_t *folderPath)
{
	ICoreWebView2_3 *w3 = NULL;
	HRESULT hr;
	if (!wv || !wv->webview || !hostName || !folderPath) return 0;
	if (FAILED(WV_QI(wv->webview, &IID_ICoreWebView2_3, &w3)) || !w3)
		return 0;
	hr = w3->lpVtbl->SetVirtualHostNameToFolderMapping(w3, hostName,
		folderPath, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
	WV_RELEASE(w3);
	return SUCCEEDED(hr) ? 1 : 0;
}

int UiWebView_AddWebResourceFilter(UiWebView *wv, const wchar_t *pattern,
	int resourceContext)
{
	if (!wv || !wv->webview || !pattern) return 0;
	return SUCCEEDED(wv->webview->lpVtbl->AddWebResourceRequestedFilter(
		wv->webview, pattern,
		(COREWEBVIEW2_WEB_RESOURCE_CONTEXT)resourceContext)) ? 1 : 0;
}

void UiWebView_SetSetting(UiWebView *wv, unsigned flag, int on)
{
	if (!wv) return;
	if (on) wv->flags |= flag; else wv->flags &= ~flag;
	wv_apply_settings_(wv);
}

void UiWebView_SetZoom(UiWebView *wv, double zoom)
{
	if (!wv || zoom <= 0.0) return;
	wv->zoom = zoom;
	if (wv->controller)
		wv->controller->lpVtbl->put_ZoomFactor(wv->controller, zoom);
}

int UiWebView_State(const UiWebView *wv) { return wv ? wv->state : 0; }
int UiWebView_LastError(const UiWebView *wv) { return wv ? wv->error : 0; }
int UiWebView_Focused(const UiWebView *wv) { return wv ? wv->focused : 0; }

HCURSOR UiWebView_Cursor(const UiWebView *wv)
{
	HCURSOR cur = NULL;
	if (wv && wv->comp) wv->comp->lpVtbl->get_Cursor(wv->comp, &cur);
	return cur;
}

IUnknown *UiWebView_Environment(const UiWebView *wv)
{
	return (wv && wv->env) ? (IUnknown *)wv->env : NULL;
}
IUnknown *UiWebView_Controller(const UiWebView *wv)
{
	return (wv && wv->controller) ? (IUnknown *)wv->controller : NULL;
}
IUnknown *UiWebView_Core(const UiWebView *wv)
{
	return (wv && wv->webview) ? (IUnknown *)wv->webview : NULL;
}
IUnknown *UiWebView_CompositionController(const UiWebView *wv)
{
	return (wv && wv->comp) ? (IUnknown *)wv->comp : NULL;
}

void UiWebView_SetVisible(UiWebView *wv, int visible)
{
	if (!wv) return;
	wv->visible = visible ? 1 : 0;
	if (wv->controller)
		wv->controller->lpVtbl->put_IsVisible(wv->controller,
			(wv->visible && wv->hostShown) ? TRUE : FALSE);
	if (wv->visual)
		Gfx_IntUpdateChildVisual(wv->gfx, wv->visual, wv->rc,
			wv->visible && wv->hostShown);
}

int UiWebView_Visible(const UiWebView *wv)
{
	return wv ? wv->visible : 0;
}

void UiWebView_SetFocus(UiWebView *wv, int focus)
{
	if (!wv || !wv->controller) return;
	if (focus) {
		wv->controller->lpVtbl->MoveFocus(wv->controller,
			COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
		wv->focused = 1;
	} else {
		wv->focused = 0;
	}
}

int UiWebView_Input(UiWebView *wv, UiContext *ui, UINT msg,
	WPARAM wParam, LPARAM lParam)
{
	POINT pt;
	int inside, consume = 0;
	DWORD data = 0;
	float x, y;
	if (!wv) return 0;
	/* 最小化/隐藏时冻结 WebView2 渲染 (官方建议); 不消费消息. */
	if (msg == WM_SIZE) {
		if (wParam == SIZE_MINIMIZED) {
			wv->hostShown = 0;
			UiWebView_SetVisible(wv, wv->visible);
		} else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
			wv->hostShown = 1;
			UiWebView_SetVisible(wv, wv->visible);
		}
		return 0;
	}
	if (msg == WM_SHOWWINDOW) {
		wv->hostShown = (wParam != 0) ? 1 : 0;
		UiWebView_SetVisible(wv, wv->visible);
		return 0;
	}
	if (msg == WM_CAPTURECHANGED) {
		wv->capturing = 0;
		wv->tracking = 0;
		return 0;
	}
	if (!ui || !wv->comp || !wv->controller || wv->windowed)
		return 0;
	if (msg == WM_SETCURSOR) {
		HCURSOR cur = NULL;
		if (LOWORD(lParam) != HTCLIENT) return 0;
		GetCursorPos(&pt);
		ScreenToClient(wv->hwnd, &pt);
		x = (float)pt.x / ui->scale;
		y = (float)pt.y / ui->scale;
		if (x < wv->rc.x || x >= wv->rc.x + wv->rc.w ||
			y < wv->rc.y || y >= wv->rc.y + wv->rc.h)
			return 0;
		wv->comp->lpVtbl->get_Cursor(wv->comp, &cur);
		if (cur) { SetCursor(cur); return 1; }
		return 0;
	}
	switch (msg) {
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
	case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
	case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
	case WM_MOUSELEAVE:
		break;
	default:
		return 0;
	}
	if (ui->scale <= 0.0f) return 0;
	if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
		POINT s;
		s.x = (short)LOWORD(lParam);
		s.y = (short)HIWORD(lParam);
		ScreenToClient(wv->hwnd, &s);
		pt = s;
	} else if (msg != WM_MOUSELEAVE) {
		pt.x = (short)LOWORD(lParam);
		pt.y = (short)HIWORD(lParam);
	} else {
		pt.x = 0;
		pt.y = 0;
	}
	x = (float)pt.x / ui->scale;
	y = (float)pt.y / ui->scale;
	inside = (msg != WM_MOUSELEAVE) && x >= wv->rc.x &&
		x < wv->rc.x + wv->rc.w && y >= wv->rc.y &&
		y < wv->rc.y + wv->rc.h;
	if (msg == WM_MOUSELEAVE || (!inside && !wv->capturing)) {
		if (wv->tracking || msg == WM_MOUSELEAVE) {
			POINT zero;
			zero.x = 0;
			zero.y = 0;
			wv->comp->lpVtbl->SendMouseInput(wv->comp,
				COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE,
				(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS)0, 0, zero);
		}
		wv->tracking = 0;
		return 0;   /* MOVE/LEAVE 继续交给 CUI 做 hover */
	}
	if (msg == WM_MOUSEMOVE) wv->tracking = inside;
	if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)
		data = (DWORD)(SHORT)GET_WHEEL_DELTA_WPARAM(wParam);
	else if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP ||
		msg == WM_XBUTTONDBLCLK)
		data = GET_XBUTTON_WPARAM(wParam);
	if (inside && (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
		msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN)) {
		if (!wv->capturing) {
			wv->capturing = 1;
			SetCapture(wv->hwnd);
		}
		/* MoveFocus 延迟到 SendMouseInput 之后, 避免同步回调打断点击状态机 */
	} else if (wv->capturing && (msg == WM_LBUTTONUP ||
		msg == WM_RBUTTONUP || msg == WM_MBUTTONUP ||
		msg == WM_XBUTTONUP)) {
		wv->capturing = 0;
		if (GetCapture() == wv->hwnd) ReleaseCapture();
	}
	{
		POINT local;
		/* SendMouseInput 使用 WebView 左上角为原点的物理像素坐标. */
		local.x = pt.x - (LONG)(wv->rc.x * ui->scale);
		local.y = pt.y - (LONG)(wv->rc.y * ui->scale);
		wv->comp->lpVtbl->SendMouseInput(wv->comp,
			(COREWEBVIEW2_MOUSE_EVENT_KIND)msg,
			(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS)GET_KEYSTATE_WPARAM(wParam),
			data, local);
		if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
			msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) {
			wv->controller->lpVtbl->MoveFocus(wv->controller,
				COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
			wv->focused = 1;
		}
	}
	consume = (msg != WM_MOUSEMOVE);
	return consume;
}
