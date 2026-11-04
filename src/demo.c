/*
 * demo.c - CUI 标准演示 (基于 UiApp 壳)
 *
 * 这个文件只保留业务: 布局/状态/动作。
 * 窗口、消息、DPI、IME、鼠标捕获、动画节拍都由 UiApp 处理。
 * 更复杂的合成器 overlay / 高级输入法流程见 samples/advanced.c。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_input.h"
#include "ui/ui_widgets.h"
#include "ui/ui_toast.h"
#include "ui/ui_progress.h"
#include "edit.h"

#define DEMO_W 720.0f
#define DEMO_H 560.0f

typedef struct Demo {
	UiInput form;
	int name, pass;
	UiToast toast;
	UiProgress progress;
	int pending;        /* 1=提交 2=重置 3=关闭 4=动画 5=主题 */
	int themeMode;      /* 0=浅色 1=品牌色 2=深色 */
	int dismissToast;
	char status[192];
} Demo;

static Demo g;

/* 品牌色示例: 5 个语义色即可换肤, 其余 hover/press/soft/文字自动派生. */
static const UiThemeColors kBrand = {
	0xFFE4572E,   /* primary */
	0xFFF2A93B,   /* warning */
	0xFF2FA56B,   /* success */
	0xFFD64545,   /* error */
	0xFFF3F5F9    /* neutral (default) */
};

static void set_status(const char *s)
{
	lstrcpynA(g.status, s, (int)sizeof(g.status));
}

static void on_init(UiApp *app, void *user)
{
	(void)user;
	UiInput_Init(&g.form, &app->ui, &app->ime);
	g.name = UiInput_Add(&g.form, Ui_Id("demo.name", 0, 0),
		63, 0, NULL, L"输入名称");
	g.pass = UiInput_Add(&g.form, Ui_Id("demo.pass", 0, 0),
		63, 1, NULL, L"输入密码");
	Edit_SetImeEnabled(UiInput_Edit(&g.form, g.pass), 0);
	Edit_SetTextUtf8(UiInput_Edit(&g.form, g.name), "CUI Demo");
	UiToast_Init(&g.toast);
	UiToast_Attach(&g.toast, &app->ui);
	UiProgress_Init(&g.progress, &app->ui, 6000);
	set_status("就绪: 支持中文输入法 / 主题切换 / Toast");
}

static void on_frame(UiApp *app, void *user)
{
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect card;
	float x, y, w;

	(void)user;

	/* 背景 + 标题 */
	Gfx_FillRoundRectGradV(gfx, Ui_Rect(0, 0, app->w, app->h), 14.0f,
		t->page, t->page2);
	Gfx_StrokeRoundRect(gfx, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		13.5f, t->frame, 1.0f);
	Gfx_FillRoundRectGradV(gfx, Ui_Rect(24, 26, 4, 26), 2.0f,
		t->blue2, t->blue);
	Gfx_Text(gfx, t->fBig, "CUI Demo",
		Ui_Rect(36, 22, app->w - 140, 32), t->text, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fSmall,
		"UiApp 壳 + 主题化组件 + Toast · 消息/DPI/IME 已在库内",
		Ui_Rect(34, 56, app->w - 120, 20), t->text3, GFX_TEXT_VCENTER);

	/* 关闭键 */
	if (Ui_Button(ui, Ui_Id("demo.close", 0, 0),
			Ui_Rect(app->w - 46, 20, 28, 28), "×",
			UI_BTN_SECONDARY, 1))
		g.pending = 3;

	UiInput_BeginFrame(&g.form);

	/* 卡片与表单 */
	card = Ui_Rect(24, 96, app->w - 48, app->h - 132);
	Ui_Panel(ui, card);
	x = card.x + 28;
	y = card.y + 26;
	w = card.w - 56;

	Gfx_Text(gfx, t->fLabel, "名称", Ui_Rect(x, y, 120, 18),
		t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fSmall, "支持中文输入法 (组合串就地绘制)",
		Ui_Rect(x + 70, y, 380, 18), t->hint, GFX_TEXT_VCENTER);
	UiInput_SetRect(&g.form, g.name, Ui_Rect(x, y + 24, w, 40));
	UiInput_Draw(&g.form, g.name);

	y += 84;
	Gfx_Text(gfx, t->fLabel, "密码", Ui_Rect(x, y, 120, 18),
		t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fSmall, "已禁用输入法 (ImmAssociateContext(NULL))",
		Ui_Rect(x + 70, y, 420, 18), t->hint, GFX_TEXT_VCENTER);
	UiInput_SetRect(&g.form, g.pass, Ui_Rect(x, y + 24, w, 40));
	UiInput_Draw(&g.form, g.pass);

	/* 按钮行 */
	y += 92;
	if (Ui_Button(ui, Ui_Id("demo.submit", 0, 0), Ui_Rect(x, y, 110, 38),
			"提交", UI_BTN_PRIMARY, 1))
		g.pending = 1;
	if (Ui_Button(ui, Ui_Id("demo.reset", 0, 0), Ui_Rect(x + 122, y, 90, 38),
			"重置", UI_BTN_SECONDARY, 1))
		g.pending = 2;
	if (Ui_Button(ui, Ui_Id("demo.anim", 0, 0), Ui_Rect(x + 224, y, 110, 38),
			UiProgress_Paused(&g.progress) ? "播放动画" : "暂停动画",
			UI_BTN_SECONDARY, 1))
		g.pending = 4;
	if (Ui_Button(ui, Ui_Id("demo.theme", 0, 0), Ui_Rect(x + 346, y, 110, 38),
			g.themeMode == 0 ? "品牌主题" : (g.themeMode == 1 ? "深色主题" : "浅色主题"),
			UI_BTN_SECONDARY, 1))
		g.pending = 5;

	/* 进度条 */
	y += 66;
	Gfx_Text(gfx, t->fLabel, "进度 (动画节拍)",
		Ui_Rect(x, y, 200, 18), t->text2, GFX_TEXT_VCENTER);
	UiProgress_Draw(&g.progress, Ui_Rect(x, y + 24, w, 10));

	/* 状态 */
	y += 58;
	Gfx_Text(gfx, t->fBody, g.status, Ui_Rect(x, y, w, 22),
		t->text2, GFX_TEXT_VCENTER);

	/* Toast (含渐变遮罩/z-index) */
	UiToast_Draw(ui, &g.toast, UiFrame_Now(), app->w, app->h);
	if (UiToast_IsVisible(&g.toast) &&
		(ui->clicked == UiToast_BackdropId() || ui->clicked == UiToast_CardId()))
		g.dismissToast = 1;
}

static void on_after_frame(UiApp *app, void *user)
{
	(void)user;
	if (g.dismissToast) {
		g.dismissToast = 0;
		UiToast_Hide(&g.toast, UiFrame_Now());
		UiApp_Invalidate(app);
	}
	if (g.pending) {
		int act = g.pending;
		g.pending = 0;
		if (act == 1) {
			char nm[160];
			char msg[256];
			Edit_TextUtf8(UiInput_Edit(&g.form, g.name), nm, (int)sizeof(nm));
			wsprintfA(msg, "已提交: %s", nm[0] ? nm : "(空)");
			set_status(msg);
			UiToast_Show(&g.toast, msg, UiFrame_Now(), 2200, UI_TOAST_SUCCESS);
		} else if (act == 2) {
			Edit_SetText(UiInput_Edit(&g.form, g.name), L"");
			Edit_SetText(UiInput_Edit(&g.form, g.pass), L"");
			set_status("已重置");
			UiToast_Show(&g.toast, "已重置", UiFrame_Now(), 1800, UI_TOAST_INFO);
		} else if (act == 4) {
			UiProgress_SetPaused(&g.progress, !UiProgress_Paused(&g.progress));
			UiApp_Invalidate(app);
		} else if (act == 5) {
			g.themeMode = (g.themeMode + 1) % 3;
			if (g.themeMode == 1)
				UiTheme_SetColors(&app->theme, UI_THEME_LIGHT, &kBrand);
			else if (g.themeMode == 2)
				UiTheme_SetTheme(&app->theme, UI_THEME_DARK);
			else
				UiTheme_SetTheme(&app->theme, UI_THEME_LIGHT);
			UiApp_Invalidate(app);
		} else if (act == 3) {
			UiApp_Close(app);
		}
	}
	/* Toast 的内容画在合成器 layer 上, 这里同步尺寸/主题并启动淡入.
	 * 回退路径的遮罩/进度重绘统一由 on_tick 发起, 不能在 WM_PAINT 里
	 * Invalidate 自己 (会形成 60fps 的整窗重绘风暴). */
	UiToast_EndFrame(&g.toast, UiFrame_Now(), app->w, app->h);
	UiProgress_EndFrame(&g.progress);
}

static int on_animating(UiApp *app, void *user)
{
	(void)app; (void)user;
	return !UiProgress_Paused(&g.progress) || UiToast_IsVisible(&g.toast);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	double now = UiFrame_Now();
	int redraw, tv;
	(void)user;

	UiProgress_Tick(&g.progress, dtMs);
	redraw = UiToast_Tick(&g.toast, now);
	tv = UiToast_IsVisible(&g.toast);
	if (!UiToast_UsingLayer(&g.toast)) {
		/* ULW / layer 创建失败: 退回主窗画遮罩, 进度改为主窗填充. */
		UiProgress_SetHidden(&g.progress, tv);
		if (tv || redraw) UiApp_Invalidate(app);
	}
	/* layer 模式: Toast 显示/淡出期间主窗内容不变, 不重绘. */

	if (!tv && UiProgress_NeedsMainRedraw(&g.progress) &&
		g.progress.rc.w > 0.0f)
		UiApp_InvalidateRect(app, g.progress.rc, 2.0f);
}

static void on_shutdown(UiApp *app, void *user)
{
	(void)app; (void)user;
	UiToast_Shutdown(&g.toast);
	UiProgress_Shutdown(&g.progress);
}

static void on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
	(void)user;
	if (msg == UI_APP_MOUSE_DOWN)
		UiInput_MouseDown(&g.form, Ui_HitTest(&app->ui, x, y), x);
	else if (msg == UI_APP_MOUSE_MOVE)
		UiInput_MouseMove(&g.form, x);
	else if (msg == UI_APP_MOUSE_UP)
		UiInput_MouseUp(&g.form);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	(void)user;
	if (UiToast_IsVisible(&g.toast)) {
		if (vk == VK_ESCAPE) UiToast_Hide(&g.toast, UiFrame_Now());
		return 1;
	}
	if (UiInput_Key(&g.form, app->hwnd, vk)) return 1;
	if (vk == VK_RETURN) {
		g.pending = 1;
		UiApp_Invalidate(app);
		return 1;
	}
	return 0;
}

static int on_char(UiApp *app, wchar_t ch, void *user)
{
	(void)app; (void)user;
	if (UiToast_IsVisible(&g.toast)) return 1;
	return UiInput_Char(&g.form, ch);
}

static void on_ime(UiApp *app, void *user)
{
	(void)app; (void)user;
	UiInput_OnIme(&g.form);
}

static void on_timer(UiApp *app, void *user)
{
	(void)app; (void)user;
	if (!UiToast_IsVisible(&g.toast)) UiInput_Blink(&g.form);
}

static int on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	(void)user;
	if (y >= 0 && y < (int)(92.0f * app->scale) &&
		x >= 0 && x < (int)(app->w * app->scale - 56.0f * app->scale)) {
		*hit = HTCAPTION;
		return 1;
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.after_frame = on_after_frame;
	cb.shutdown = on_shutdown;
	cb.animating = on_animating;
	cb.tick = on_tick;
	cb.mouse = on_mouse;
	cb.key = on_key;
	cb.chr = on_char;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.nchittest = on_nchittest;
	return UiApp_Run(L"CUI Demo", (int)DEMO_W, (int)DEMO_H,
		UI_THEME_LIGHT, &cb, &g);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
