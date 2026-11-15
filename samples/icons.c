/*
 * icons.c - 图标组件样例
 *
 * 演示:
 *   - 按钮 + MDL2 图标 (Ui_ButtonExIcon)
 *   - Toast 按 kind 自动带信息/成功/错误图标
 *   - TabBar 页签图标 + TabPager 页面大图标
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_icon.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/ui_tab.h"
#include "ui/ui_toast.h"
#include "ui/ui_widgets.h"

#define TAB_COUNT 4

typedef struct IconsDemo {
	UiApp *app;
	UiToast toast;
	UiTabBar bar;
	UiTabPager pager;
	UiTabStyle line, pill, card;
	GfxFont *iconBig;
	int styleMode;
	int pendingToast;   /* 0 info, 1 success, 2 error, -1 = none */
	int pendingHide;
	int pendingPage;
	char status[192];
} IconsDemo;

static IconsDemo g_icons;

static const char *const kTabLabels[TAB_COUNT] = {
	"常规", "网络", "信息", "刷新"
};
static const uint32_t kTabCodes[TAB_COUNT] = {
	0xE713u, 0xE774u, 0xE946u, 0xE72Cu
};

static void set_status(IconsDemo *d, const char *s)
{
	int i = 0;
	while (s && s[i] && i < (int)sizeof(d->status) - 1) {
		d->status[i] = s[i];
		i++;
	}
	d->status[i] = 0;
	UiApp_Invalidate(d->app);
}

static void show_toast(IconsDemo *d, int kind, const char *text)
{
	UiToast_Show(&d->toast, text, UiFrame_Now(), 2200, kind);
	set_status(d, text);
	UiApp_Invalidate(d->app);
}

static void draw_page(UiContext *ui, void *user, int index, UiRect rc,
	int interactive)
{
	IconsDemo *d = (IconsDemo *)user;
	UiTheme *t = &d->app->theme;
	Gfx *g = ui->gfx;
	UiRect hero, title, note, iconRc, badge;
	UiIcon icon;
	char line[64];
	UiColor accent;

	(void)interactive;
	accent = index == 0 ? t->primary.base :
		(index == 1 ? t->success.base :
		 (index == 2 ? t->text2 : t->warning.base));
	hero = Ui_Rect(rc.x, rc.y + 4.0f, rc.w, 58.0f);
	iconRc = Ui_Rect(hero.x, hero.y, 48.0f, 48.0f);
	title = Ui_Rect(hero.x + 62.0f, hero.y, hero.w - 70.0f, 30.0f);
	badge = Ui_Rect(hero.x + 62.0f, hero.y + 30.0f, hero.w - 70.0f, 20.0f);
	note = Ui_Rect(rc.x, rc.y + 76.0f, rc.w, rc.h - 76.0f);

	icon = UiIcon_Glyph(d->iconBig, kTabCodes[index]);
	UiIcon_Draw(ui, &icon, iconRc, accent, 1);
	wsprintfA(line, "%s页", kTabLabels[index]);
	Gfx_Text(g, t->fBig, line, title, t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "TabBar 图标来自 theme.fIcon；页面大图标用 40 DIP 字体。",
		badge, t->text3, GFX_TEXT_VCENTER);

	Ui_Panel(ui, note);
	{
		UiIcon labelIcon = UiIcon_Glyph(t->fIcon, 0xE946u);
		UiRect lr = Ui_Rect(note.x + 18.0f, note.y + 14.0f, 16.0f, 16.0f);
		UiIcon_Draw(ui, &labelIcon, lr, t->primary.base, 1);
	}
	Gfx_Text(g, t->fBody,
		"图标就是字体：颜色由 tint/主题决定，DPI 缩放清晰；"
		"按钮、Toast、TabBar 共用同一套 GLYPH 后端。",
		Ui_Rect(note.x + 42.0f, note.y + 12.0f, note.w - 60.0f, 40.0f),
		t->text2, 0);
	Gfx_Text(g, t->fSmall,
		"按钮图标 16 DIP · Tab 图标 16 DIP · 页面图标 40 DIP",
		Ui_Rect(note.x + 18.0f, note.y + note.h - 30.0f, note.w - 36.0f, 18.0f),
		t->text3, GFX_TEXT_VCENTER);
}

static void draw_header(IconsDemo *d, UiContext *ui, Gfx *g)
{
	UiTheme *t = &d->app->theme;
	float y = 20.0f, h = 32.0f, w = 86.0f, gap = 8.0f;
	float x = d->app->w - 22.0f - (w * 2.0f + gap);
	UiButtonStyle def = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiIcon globe = UiIcon_Glyph(t->fIcon, 0xE774u);
	UiIcon sun = UiIcon_Glyph(t->fIcon, 0xE706u);

	Gfx_Text(g, t->fBig, "CUI Icons", Ui_Rect(24, 14, x - 44, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "MDL2 glyph · button / toast / tabbar",
		Ui_Rect(26, 46, x - 40, 18), t->text3, GFX_TEXT_VCENTER);

	if (Ui_ButtonExIcon(ui, Ui_Id("icons.theme", 0, 0), Ui_Rect(x, y, w, h),
		&sun, "主题", &def, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(d->app);
	}
	x += w + gap;
	if (Ui_ButtonExIcon(ui, Ui_Id("icons.style", 0, 0), Ui_Rect(x, y, w, h),
		&globe, d->styleMode == 0 ? "LINE" :
		(d->styleMode == 1 ? "PILL" : "CARD"), &def, 1)) {
		d->styleMode = (d->styleMode + 1) % 3;
		set_status(d, d->styleMode == 0 ? "TabBar: LINE" :
			(d->styleMode == 1 ? "TabBar: PILL" : "TabBar: CARD"));
	}
}

static void draw_button_demo(IconsDemo *d, UiContext *ui, UiRect row)
{
	UiTheme *t = &d->app->theme;
	UiRect b[4];
	UiFixedSize fx[4] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
	UiMeasureItem items[4] = {
		Ui_FixedItem(&fx[0], 1.0f), Ui_FixedItem(&fx[1], 1.0f),
		Ui_FixedItem(&fx[2], 1.0f), Ui_FixedItem(&fx[3], 1.0f) };
	UiButtonStyle primary = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle def = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle danger = UiButtonStyle_Make(UI_BTYPE_ERROR,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
	UiIcon save = UiIcon_Glyph(t->fIcon, 0xE74Eu);
	UiIcon refresh = UiIcon_Glyph(t->fIcon, 0xE72Cu);
	UiIcon trash = UiIcon_Glyph(t->fIcon, 0xE74Du);
	UiIcon disabled = UiIcon_Glyph(t->fIcon, 0xE713u);

	if (!Ui_FlexLayout(row, UI_ROW, 10.0f, items, 4, b)) return;
	if (Ui_ButtonExIcon(ui, Ui_Id("icons.save", 0, 0), b[0], &save,
		"保存", &primary, 1))
		show_toast(d, UI_TOAST_SUCCESS, "保存成功（图标来自 MDL2）");
	if (Ui_ButtonExIcon(ui, Ui_Id("icons.refresh", 0, 0), b[1], &refresh,
		"刷新", &def, 1))
		show_toast(d, UI_TOAST_INFO, "正在刷新…");
	if (Ui_ButtonExIcon(ui, Ui_Id("icons.delete", 0, 0), b[2], &trash,
		"删除", &danger, 1))
		show_toast(d, UI_TOAST_ERROR, "删除失败（示例错误）");
	Ui_ButtonExIcon(ui, Ui_Id("icons.disabled", 0, 0), b[3], &disabled,
		"禁用", &primary, 0);
}

static void draw_toast_demo(IconsDemo *d, UiContext *ui, UiRect row)
{
	UiTheme *t = &d->app->theme;
	UiRect b[3];
	UiFixedSize fx[3] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
	UiMeasureItem items[3] = {
		Ui_FixedItem(&fx[0], 1.0f), Ui_FixedItem(&fx[1], 1.0f),
		Ui_FixedItem(&fx[2], 1.0f) };
	UiButtonStyle info = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
	UiButtonStyle ok = UiButtonStyle_Make(UI_BTYPE_SUCCESS,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
	UiButtonStyle err = UiButtonStyle_Make(UI_BTYPE_ERROR,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
	UiIcon infoIcon = UiIcon_Glyph(t->fIcon, 0xE946u);
	UiIcon okIcon = UiIcon_Glyph(t->fIcon, 0xE73Eu);
	UiIcon errIcon = UiIcon_Glyph(t->fIcon, 0xE783u);

	if (!Ui_FlexLayout(row, UI_ROW, 10.0f, items, 3, b)) return;
	if (Ui_ButtonExIcon(ui, Ui_Id("icons.toast.info", 0, 0), b[0],
		&infoIcon, "信息 Toast", &info, 1))
		show_toast(d, UI_TOAST_INFO, "这是一条信息提示");
	if (Ui_ButtonExIcon(ui, Ui_Id("icons.toast.ok", 0, 0), b[1],
		&okIcon, "成功 Toast", &ok, 1))
		show_toast(d, UI_TOAST_SUCCESS, "操作成功");
	if (Ui_ButtonExIcon(ui, Ui_Id("icons.toast.err", 0, 0), b[2],
		&errIcon, "错误 Toast", &err, 1))
		show_toast(d, UI_TOAST_ERROR, "操作失败");
}

static void on_frame(UiApp *app, void *user)
{
	IconsDemo *d = (IconsDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiRect card, content, headerRc, footer;
	UiStack col;
	UiTabStyle style;
	UiIcon tabIcons[TAB_COUNT];
	UiRect activeRc;
	int i, prev, clicked;

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	draw_header(d, ui, g);

	card = Ui_Rect(22, 80, app->w - 44, app->h - 128);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 22, 16, 22, 16);

	Gfx_Text(g, t->fLabel, "按钮 + 图标", Ui_StackTake(&col, 18.0f),
		t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8.0f);
	draw_button_demo(d, ui, Ui_StackTake(&col, 38.0f));
	Ui_StackSkip(&col, 16.0f);

	Gfx_Text(g, t->fLabel, "Toast + 图标（按 kind 自动选择）",
		Ui_StackTake(&col, 18.0f), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8.0f);
	draw_toast_demo(d, ui, Ui_StackTake(&col, 34.0f));
	Ui_StackSkip(&col, 16.0f);

	style = d->styleMode == 0 ? d->line :
		(d->styleMode == 1 ? d->pill : d->card);
	headerRc = Ui_StackTake(&col, style.height > 0.0f ? style.height : 38.0f);
	Ui_StackSkip(&col, d->styleMode == 2 ? 0.0f : 12.0f);
	content = Ui_StackTakeFill(&col);

	for (i = 0; i < TAB_COUNT; i++)
		tabIcons[i] = UiIcon_Glyph(t->fIcon, kTabCodes[i]);
	prev = d->bar.selected;
	clicked = UiTabBar_DrawEx(&d->bar, ui, headerRc, kTabLabels, tabIcons,
		&style, 1);
	if (clicked >= 0 && d->bar.selected != prev) {
		UiTabPager_GoTo(&d->pager, ui, d->bar.selected, 1);
		wsprintfA(d->status, "切换到 %s", kTabLabels[d->bar.selected]);
	}
	UiTabPager_Draw(&d->pager, ui, content, draw_page, d);

	if (d->styleMode == 2) {
		activeRc = UiTabBar_ActiveRect(&d->bar);
		{
			UiColor line = Ui_ColorAlpha(t->neutral.border, 220);
			if (activeRc.w > 0.0f) {
				if (activeRc.x > content.x)
					Gfx_FillRoundRect(g, Ui_Rect(content.x, content.y - 0.5f,
						activeRc.x - content.x, 1.0f), 0.0f, line);
				if (activeRc.x + activeRc.w < content.x + content.w)
					Gfx_FillRoundRect(g, Ui_Rect(activeRc.x + activeRc.w,
						content.y - 0.5f,
						content.x + content.w - (activeRc.x + activeRc.w),
						1.0f), 0.0f, line);
			} else {
				Gfx_FillRoundRect(g, Ui_Rect(content.x, content.y - 0.5f,
					content.w, 1.0f), 0.0f, line);
			}
		}
		UiTabBar_DrawActiveEx(&d->bar, ui, kTabLabels, tabIcons, &style);
	}

	UiToast_Draw(ui, &d->toast, UiFrame_Now(), app->w, app->h);
	if (UiToast_IsVisible(&d->toast) &&
		(ui->clicked == UiToast_BackdropId() || ui->clicked == UiToast_CardId()))
		d->pendingHide = 1;

	footer = Ui_Rect(24, app->h - 36, app->w - 48, 18);
	Gfx_Text(g, t->fSmall, d->status, footer, t->text3, GFX_TEXT_VCENTER);
}

static void on_after_frame(UiApp *app, void *user)
{
	IconsDemo *d = (IconsDemo *)user;
	if (d->pendingHide) {
		UiToast_Hide(&d->toast, UiFrame_Now());
		d->pendingHide = 0;
	}
	if (d->pendingPage >= 0) {
		UiTabBar_SetSelected(&d->bar, d->pendingPage);
		UiTabPager_GoTo(&d->pager, &app->ui, d->pendingPage, 1);
		d->pendingPage = -1;
		UiApp_Invalidate(app);
	}
	if (d->pendingToast >= 0) {
		static const char *const msgs[3] = {
			"信息提示", "操作成功", "操作失败"
		};
		UiToast_Show(&d->toast, msgs[d->pendingToast], UiFrame_Now(),
			2200, d->pendingToast);
		d->pendingToast = -1;
	}
	UiToast_EndFrame(&d->toast, UiFrame_Now(), app->w, app->h);
}

static int on_animating(UiApp *app, void *user)
{
	IconsDemo *d = (IconsDemo *)user;
	(void)app;
	return UiToast_IsVisible(&d->toast) || UiTabPager_Busy(&d->pager);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	IconsDemo *d = (IconsDemo *)user;
	int redraw, tv;
	UiTabPager_Tick(&d->pager, &app->ui, (float)dtMs);
	redraw = UiToast_Tick(&d->toast, UiFrame_Now());
	tv = UiToast_IsVisible(&d->toast);
	if (!UiToast_UsingLayer(&d->toast)) {
		if (tv || redraw) UiApp_Invalidate(app);
	}
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	IconsDemo *d = (IconsDemo *)user;
	int prev;
	if (vk == VK_TAB) {
		Ui_SetNavId(&app->ui, d->bar.id);
		UiTabBar_SetFocusVisible(&d->bar, 1);
		UiApp_Invalidate(app);
		return 1;
	}
	if (vk == VK_PRIOR || vk == VK_NEXT) {
		int idx = d->pager.to + (vk == VK_NEXT ? 1 : -1);
		if (idx < 0) idx = d->bar.count - 1;
		if (idx >= d->bar.count) idx = 0;
		UiTabBar_SetSelected(&d->bar, idx);
		UiTabPager_GoTo(&d->pager, &app->ui, idx, 1);
		UiApp_Invalidate(app);
		return 1;
	}
	prev = d->bar.selected;
	if (UiTabBar_Key(&d->bar, &app->ui, vk)) {
		if (d->bar.changed && d->bar.selected != prev) {
			UiTabPager_GoTo(&d->pager, &app->ui, d->bar.selected, 1);
			wsprintfA(d->status, "键盘切换到 %s", kTabLabels[d->bar.selected]);
		}
		UiApp_Invalidate(app);
		return 1;
	}
	return 0;
}

static void on_shutdown(UiApp *app, void *user)
{
	IconsDemo *d = (IconsDemo *)user;
	UiToast_Shutdown(&d->toast);
	if (d->iconBig) {
		Gfx_DestroyFont(app->gfx, d->iconBig);
		d->iconBig = NULL;
	}
}

static void on_init(UiApp *app, void *user)
{
	IconsDemo *d = (IconsDemo *)user;
	UiTabPagerConfig cfg;
	d->app = app;
	app->minW = 760.0f;
	app->minH = 540.0f;
	d->styleMode = 0;
	d->pendingToast = -1;
	d->pendingPage = -1;
	d->line = UiTabStyle_Line();
	d->pill = UiTabStyle_Pill();
	d->card = UiTabStyle_Card();
	d->iconBig = Gfx_CreateFontFamily(app->gfx, L"Segoe MDL2 Assets",
		40.0f, 400);
	UiTabBar_Init(&d->bar, Ui_Id("icons.bar", 0, 0), TAB_COUNT);
	ZeroMemory(&cfg, sizeof(cfg));
	cfg.axis = UI_TAB_AXIS_HORIZONTAL;
	cfg.durationMs = 260.0f;
	UiTabPager_Init(&d->pager, Ui_Id("icons.pager", 0, 0), &cfg);
	UiToast_Init(&d->toast);
	UiToast_Attach(&d->toast, &app->ui);
	set_status(d, "点击按钮触发带图标的 Toast；TabBar 支持图标。");
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.after_frame = on_after_frame;
	cb.animating = on_animating;
	cb.tick = on_tick;
	cb.key = on_key;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI Icons", 900, 640, UI_THEME_LIGHT, &cb, &g_icons);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
