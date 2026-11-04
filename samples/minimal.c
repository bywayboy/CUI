/*
 * minimal.c - CUI 最小接入样例
 *
 * 只保留业务: 一个输入框 + 提交/退出按钮 + 状态行。
 * 窗口/消息/DPI/IME/动画节拍全部由 UiApp 壳处理。
 *
 * 0CRT 构建入口与 demo.c 相同 (startup -> WinMain)。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_input.h"
#include "ui/ui_widgets.h"
#include "edit.h"

typedef struct App {
	UiInput form;
	int name;
	char status[160];
} App;

static void submit(App *a)
{
	char nm[128];
	Edit_TextUtf8(UiInput_Edit(&a->form, a->name), nm, (int)sizeof(nm));
	wsprintfA(a->status, "已提交: %s", nm[0] ? nm : "(空)");
}

static void on_init(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiInput_Init(&a->form, &app->ui, &app->ime);
	a->name = UiInput_Add(&a->form, Ui_Id("min.name", 0, 0),
		63, 0, NULL, L"输入名称");
	Edit_SetTextUtf8(UiInput_Edit(&a->form, a->name), "CUI Minimal");
	lstrcpynA(a->status, "就绪: 输入后回车或点提交", (int)sizeof(a->status));
}

static void on_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiTheme *t = &app->theme;
	UiRect card, rc;
	float x, y, w;

	Gfx_FillRoundRectGradV(app->gfx, Ui_Rect(0, 0, app->w, app->h),
		14.0f, t->page, t->page2);

	card = Ui_Rect(24, 24, app->w - 48, app->h - 48);
	Ui_Panel(&app->ui, card);
	x = card.x + 24;
	y = card.y + 24;
	w = card.w - 48;

	Gfx_Text(app->gfx, t->fBig, "CUI Minimal",
		Ui_Rect(x, y, w, 30), t->text, GFX_TEXT_VCENTER);
	y += 40;
	Gfx_Text(app->gfx, t->fSmall,
		"窗口/消息/DPI/IME/动画节拍由 UiApp 处理；这里只有业务 UI",
		Ui_Rect(x, y, w, 20), t->hint, GFX_TEXT_VCENTER);

	y += 32;
	UiInput_BeginFrame(&a->form);
	rc = Ui_Rect(x, y, w, 40);
	UiInput_SetRect(&a->form, a->name, rc);
	UiInput_Draw(&a->form, a->name);

	y += 56;
	if (Ui_Button(&app->ui, Ui_Id("min.submit", 0, 0),
			Ui_Rect(x, y, 110, 38), "提交", UI_BTN_PRIMARY, 1))
		submit(a);
	if (Ui_Button(&app->ui, Ui_Id("min.quit", 0, 0),
			Ui_Rect(x + 122, y, 90, 38), "退出", UI_BTN_DANGER, 1))
		UiApp_Close(app);

	y += 54;
	Gfx_Text(app->gfx, t->fBody, a->status, Ui_Rect(x, y, w, 22),
		t->text2, GFX_TEXT_VCENTER);
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
	if (vk == VK_RETURN) { submit(a); UiApp_Invalidate(app); return 1; }
	return 0;
}

static int on_char(UiApp *app, wchar_t ch, void *user)
{
	App *a = (App *)user;
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
	UiInput_Blink(&a->form);
}

static int on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	(void)x; (void)user;
	if (y < (int)(52.0f * app->scale)) {
		*hit = HTCAPTION;
		return 1;
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	static App a;
	UiAppCallbacks cb;
	(void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.mouse = on_mouse;
	cb.key = on_key;
	cb.chr = on_char;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.nchittest = on_nchittest;
	return UiApp_Run(L"CUI Minimal", 520, 320, UI_THEME_LIGHT, &cb, &a);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
