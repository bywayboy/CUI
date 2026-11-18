/*
 * ui_webview.h - WebView2 组件 (可选模块, 0CRT)
 *
 * 依赖: WebView2 Runtime + WebView2Loader.dll (动态加载, 不链接 CRT/import lib)
 *       + gfx 内部 DComp Visual 宿主接口。
 * 托管: 首选 CompositionController + DComp 子视觉; DComp 不可用 (ULW)
 *       时自动回退窗口模式 (WebView2 自建子 HWND).
 *
 * 应用持有 UiWebView; 每帧 Ui_WebView(..., rc) 只更新视觉矩形/可见性,
 * 不重建 controller。所有 WebView2 回调均在 UI 线程。
 */
#ifndef CUI_UI_WEBVIEW_H
#define CUI_UI_WEBVIEW_H

#include <windows.h>
#include <objbase.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiWebView UiWebView;

/* 配置 flag (与 DuiLib WebView2 属性一一对应; 默认见 UiWebViewConfig_Default) */
enum {
	UI_WVF_SCRIPT          = 1u << 0,
	UI_WVF_DEFAULT_DIALOGS = 1u << 1,
	UI_WVF_WEB_MESSAGE     = 1u << 2,
	UI_WVF_ERROR_PAGE      = 1u << 3,
	UI_WVF_EXTERNAL_DROP   = 1u << 4,
	UI_WVF_CONTEXT_MENUS   = 1u << 5,
	UI_WVF_ACCELERATOR_KEYS= 1u << 6,
	UI_WVF_STATUS_BAR      = 1u << 7,
	UI_WVF_ZOOM_CONTROL    = 1u << 8,
	UI_WVF_AUTO_NAVIGATE   = 1u << 9,
	UI_WVF_MUST_HTTPS      = 1u << 10,
	UI_WVF_DEVTOOLS        = 1u << 11,
	UI_WVF_DISABLE_GPU     = 1u << 12,
	UI_WVF_DISABLE_GPU_COMP= 1u << 13
};

typedef struct UiWebViewConfig {
	const wchar_t *homepage;     /* NULL = about:blank */
	const wchar_t *cacheFolder;  /* NULL = %LOCALAPPDATA%\CUI\WebView2 */
	const wchar_t *browserArgs;  /* 追加 Chromium 参数 (可 NULL) */
	GfxColor background;         /* 0 = 不透明白 */
	double zoom;                 /* 0 = 1.0 */
	unsigned flags;              /* UI_WVF_* */
} UiWebViewConfig;

void UiWebViewConfig_Default(UiWebViewConfig *cfg);

/* 脚本对话框类型 (对应 COREWEBVIEW2_SCRIPT_DIALOG_KIND_*). */
enum {
	UI_WV_DIALOG_ALERT = 0,
	UI_WV_DIALOG_CONFIRM = 1,
	UI_WV_DIALOG_PROMPT = 2,
	UI_WV_DIALOG_BEFOREUNLOAD = 3
};

/* NewWindowRequested: handled=1 后由宿主负责 (可导航当前视图或自建窗口);
 * args 是原始 ICoreWebView2NewWindowRequestedEventArgs*, 可设置 NewWindow. */
typedef struct UiWebViewNewWindow {
	const wchar_t *uri;
	int user_initiated;
	int handled;
	IUnknown *args;
} UiWebViewNewWindow;

/* DownloadStarting: 默认路径可读; 应用可 cancel 或 handled (接管保存 UI).
 * args 是原始 ICoreWebView2DownloadStartingEventArgs*, 可 put_ResultFilePath. */
typedef struct UiWebViewDownload {
	const wchar_t *uri;
	const wchar_t *default_path;
	int cancel;
	int handled;
	IUnknown *args;
} UiWebViewDownload;

/* ScriptDialogOpening: 应用填 handled=1 表示自行处理; prompt 结果写
 * result_text, accept=0 表示取消/否定. */
typedef struct UiWebViewScriptDialog {
	int kind;                       /* UI_WV_DIALOG_* */
	const wchar_t *message;
	const wchar_t *default_text;
	int accept;
	int handled;
	wchar_t result_text[512];
} UiWebViewScriptDialog;

/* 事件 (在 WebView2 回调线程 = UI 线程同步调用; 指针仅回调期间有效) */
typedef struct UiWebViewEvents {
	void (*created)(void *user, UiWebView *wv);
	void (*create_failed)(void *user, UiWebView *wv, int hr);
	void (*navigation_starting)(void *user, UiWebView *wv,
		const wchar_t *uri, int *cancel);
	void (*navigation_completed)(void *user, UiWebView *wv,
		int success, int error);
	void (*frame_navigation_completed)(void *user, UiWebView *wv,
		int success, int error);
	void (*new_window)(void *user, UiWebView *wv, UiWebViewNewWindow *nw);
	void (*source_changed)(void *user, UiWebView *wv,
		const wchar_t *uri, int is_new_document);
	void (*history_changed)(void *user, UiWebView *wv,
		int can_back, int can_forward);
	void (*content_loading)(void *user, UiWebView *wv, int is_error);
	void (*dom_content_loaded)(void *user, UiWebView *wv,
		unsigned long long navigation_id);
	void (*document_title_changed)(void *user, UiWebView *wv,
		const wchar_t *title);
	void (*download_starting)(void *user, UiWebView *wv,
		UiWebViewDownload *dl);
	void (*script_dialog)(void *user, UiWebView *wv,
		UiWebViewScriptDialog *dlg);
	void (*web_message)(void *user, UiWebView *wv, const wchar_t *json);
	void (*process_failed)(void *user, UiWebView *wv, int kind);
	/* allow: 置 1 放行证书/权限; 无回调时权限默认放行 (与 DuiLib 一致) */
	void (*certificate_error)(void *user, UiWebView *wv,
		const wchar_t *uri, int *allow);
	void (*permission_requested)(void *user, UiWebView *wv,
		int kind, int *allow);
	/* 原始 WebView2 COM 参数 (ICoreWebView2WebResourceRequest/EventArgs),
	 * 供应用完全控制; 仅在回调期间有效. */
	void (*web_resource_requested)(void *user, UiWebView *wv,
		IUnknown *request, IUnknown *args);
	void (*focus_changed)(void *user, UiWebView *wv, int focused);
	void (*accelerator_key)(void *user, UiWebView *wv,
		int virtualKey, int keyData, int *handled);
	void *user;
} UiWebViewEvents;

UiWebView *UiWebView_Create(Gfx *gfx, UiId id, const UiWebViewConfig *cfg,
	const UiWebViewEvents *ev);
void UiWebView_Destroy(UiWebView *wv);

/* 每帧: 更新 controller bounds / 视觉 offset+clip / 输入命中区域 (DIP). */
int Ui_WebView(UiWebView *wv, UiContext *ui, UiRect rc);

/* 导航/内容 */
void UiWebView_SetHomepage(UiWebView *wv, const wchar_t *url);
const wchar_t *UiWebView_Homepage(const UiWebView *wv);
void UiWebView_GoHome(UiWebView *wv);
void UiWebView_Navigate(UiWebView *wv, const wchar_t *url);
void UiWebView_Reload(UiWebView *wv);
int  UiWebView_CanGoBack(const UiWebView *wv);
int  UiWebView_CanGoForward(const UiWebView *wv);
void UiWebView_GoBack(UiWebView *wv);
void UiWebView_GoForward(UiWebView *wv);
/* Host -> 页面: 对应 PostWebMessageAsJson / PostWebMessageAsString. */
void UiWebView_PostWebMessageJson(UiWebView *wv, const wchar_t *json);
void UiWebView_PostWebMessageString(UiWebView *wv, const wchar_t *text);
/* 兼容入口, 等同 Json 版本. */
void UiWebView_PostWebMessage(UiWebView *wv, const wchar_t *json);
void UiWebView_AddScriptOnDocumentCreated(UiWebView *wv,
	const wchar_t *script);
typedef void (*UiWebViewScriptDone)(void *user, int hr,
	const wchar_t *resultJson);
void UiWebView_ExecuteScript(UiWebView *wv, const wchar_t *script,
	UiWebViewScriptDone done, void *user);
int UiWebView_AddHostObject(UiWebView *wv, const wchar_t *name,
	IDispatch *obj);   /* obj 可为 NULL 表示移除 */
int UiWebView_SetVirtualHost(UiWebView *wv, const wchar_t *hostName,
	const wchar_t *folderPath);
int UiWebView_AddWebResourceFilter(UiWebView *wv, const wchar_t *pattern,
	int resourceContext);

/* 设置/状态 */
void UiWebView_SetVisible(UiWebView *wv, int visible);
int  UiWebView_Visible(const UiWebView *wv);
void UiWebView_SetSetting(UiWebView *wv, unsigned flag, int on);
void UiWebView_SetZoom(UiWebView *wv, double zoom);
int  UiWebView_State(const UiWebView *wv);      /* 0 idle,1 loading,2 ready,3 failed */
int  UiWebView_LastError(const UiWebView *wv);
int  UiWebView_Focused(const UiWebView *wv);
HCURSOR UiWebView_Cursor(const UiWebView *wv);  /* NULL = 未知/沿用宿主 */

/* 原始 COM (借用, 随 UiWebView 存活) */
IUnknown *UiWebView_Environment(const UiWebView *wv);
IUnknown *UiWebView_Controller(const UiWebView *wv);
IUnknown *UiWebView_Core(const UiWebView *wv);
IUnknown *UiWebView_CompositionController(const UiWebView *wv);

/* 输入: 在宿主 WndProc 的原始消息钩子中调用; 返回 1 = 已消费
 * (鼠标按键/滚轮; MOVE 和 WM_SIZE/WM_SHOWWINDOW 不消费).
 * 鼠标坐标按宿主客户区物理像素, 内部换算为 WebView 本地坐标. */
int UiWebView_Input(UiWebView *wv, UiContext *ui, UINT msg,
	WPARAM wParam, LPARAM lParam);
void UiWebView_SetFocus(UiWebView *wv, int focus);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_WEBVIEW_H */
