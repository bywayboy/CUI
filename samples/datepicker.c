/*
 * datepicker.c - DatePicker 四模式样例
 *
 * 纯日期 / 日期+时间 / 日期范围 / 日期+时间范围。
 * 演示: 可编辑触发器键入 (parse/校验回退)、双联动月历范围两阶段预览、
 * 时间滚动列、键盘导航、Esc/外部点击关闭、弹层翻转夹取。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_icon.h"
#include "ui/ui_layout.h"
#include "ui/ui_popup.h"
#include "ui/ui_datepicker.h"
#include "ui/ui_widgets.h"

typedef struct Demo {
	UiApp *app;
	UiPopupManager pm;
	UiDatePicker dpDate;
	UiDatePicker dpDateTime;
	UiDatePicker dpRangeDate;
	UiDatePicker dpRangeDateTime;
	UiId themeBtnId;
	char status[220];
} Demo;

static Demo g_demo;

static void set_status(Demo *d, const char *s)
{
	lstrcpynA(d->status, s, (int)sizeof(d->status));
	UiApp_Invalidate(d->app);
}

static void consume(Demo *d, UiDatePicker *dp, const char *name)
{
	char a[64], b[64];
	if (!dp->changed && !dp->committed) return;
	if (dp->mode >= UI_DP_RANGE_DATE) {
		if (dp->hasValue) UiDate_Format(&dp->value, dp->format, a, (int)sizeof(a));
		else lstrcpynA(a, "?", (int)sizeof(a));
		if (dp->hasEnd) UiDate_Format(&dp->end, dp->format, b, (int)sizeof(b));
		else lstrcpynA(b, "?", (int)sizeof(b));
		wsprintfA(d->status, "%s：%s → %s", name, a, b);
	} else {
		if (dp->hasValue) UiDate_Format(&dp->value, dp->format, a, (int)sizeof(a));
		else lstrcpynA(a, "(未选)", (int)sizeof(a));
		wsprintfA(d->status, "%s：%s", name, a);
	}
	dp->changed = 0;
	dp->committed = 0;
	UiApp_Invalidate(d->app);
}

static void on_init(UiApp *app, void *user)
{
	Demo *d = (Demo *)user;
	UiDateTime now, min, max;
	d->app = app;
	UiPopupManager_Init(&d->pm);

	UiDatePicker_Init(&d->dpDate, Ui_Id("dp.date", 0, 0), UI_DP_DATE);
	UiDatePicker_SetPlaceholder(&d->dpDate, "选择日期");
	UiDatePicker_AttachIme(&d->dpDate, &app->ime);
	UiDate_Now(&now);
	min = now; UiDate_AddDays(&min, -30);
	max = now; UiDate_AddDays(&max, 30);
	UiDatePicker_SetBounds(&d->dpDate, &min, &max);

	UiDatePicker_Init(&d->dpDateTime, Ui_Id("dp.datetime", 0, 0), UI_DP_DATETIME);
	UiDatePicker_SetPlaceholder(&d->dpDateTime, "选择日期时间");
	UiDatePicker_SetShowSeconds(&d->dpDateTime, 1);
	UiDatePicker_AttachIme(&d->dpDateTime, &app->ime);

	UiDatePicker_Init(&d->dpRangeDate, Ui_Id("dp.range", 0, 0), UI_DP_RANGE_DATE);
	UiDatePicker_SetPlaceholder(&d->dpRangeDate, "开始日期");
	UiDatePicker_AttachIme(&d->dpRangeDate, &app->ime);

	UiDatePicker_Init(&d->dpRangeDateTime, Ui_Id("dp.rangedt", 0, 0), UI_DP_RANGE_DATETIME);
	UiDatePicker_SetPlaceholder(&d->dpRangeDateTime, "开始时间");
	UiDatePicker_AttachIme(&d->dpRangeDateTime, &app->ime);

	d->themeBtnId = Ui_Id("dp.btn.theme", 0, 0);
	app->minW = 660.0f;
	app->minH = 620.0f;
	set_status(d, "点触发器键入或点日历图标选择；范围两阶段点选，Esc/点外部关闭。");
}

static void on_frame(UiApp *app, void *user)
{
	Demo *d = (Demo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiIcon sun = UiIcon_Glyph(t->fIcon, 0xE706u);
	UiRect card, row;
	UiStack col;

	UiPopupManager_BeginFrame(&d->pm, ui);

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_Text(g, t->fBig, "CUI DatePicker",
		Ui_Rect(24, 14, app->w - 150, 32), t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "日期 · 日期+时间 · 日期范围 · 日期+时间范围",
		Ui_Rect(26, 46, app->w - 150, 18), t->text3, GFX_TEXT_VCENTER);
	if (Ui_IconButton(ui, d->themeBtnId, Ui_Rect(app->w - 66, 20, 36, 34),
		&sun, &plain, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(app);
	}

	card = Ui_Rect(22, 80, app->w - 44, app->h - 126);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 22, 18, 22, 18);

	Gfx_Text(g, t->fLabel, "纯日期（限定 ±30 天，越界禁用）",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 6);
	row = Ui_StackTake(&col, 38);
	Ui_DatePicker(&d->dpDate, &d->pm, ui, row, NULL, 1);
	consume(d, &d->dpDate, "日期");
	Ui_StackSkip(&col, 14);

	Gfx_Text(g, t->fLabel, "日期 + 时间（含秒，滚动列）",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 6);
	row = Ui_StackTake(&col, 38);
	Ui_DatePicker(&d->dpDateTime, &d->pm, ui, row, NULL, 1);
	consume(d, &d->dpDateTime, "日期时间");
	Ui_StackSkip(&col, 14);

	Gfx_Text(g, t->fLabel, "日期范围（双联动月历，两阶段 + 悬停预览）",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 6);
	row = Ui_StackTake(&col, 38);
	Ui_DatePicker(&d->dpRangeDate, &d->pm, ui, row, NULL, 1);
	consume(d, &d->dpRangeDate, "日期范围");
	Ui_StackSkip(&col, 14);

	Gfx_Text(g, t->fLabel, "日期 + 时间范围（端点各自时间）",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 6);
	row = Ui_StackTake(&col, 38);
	Ui_DatePicker(&d->dpRangeDateTime, &d->pm, ui, row, NULL, 1);
	consume(d, &d->dpRangeDateTime, "时间范围");

	Gfx_Text(g, t->fSmall, d->status,
		Ui_Rect(24, app->h - 34, app->w - 48, 18), t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	/* 帧末按 z 顺序绘制弹层 */
	UiDatePicker_DrawPopup(&d->dpDate, &d->pm, ui);
	consume(d, &d->dpDate, "日期");
	UiDatePicker_DrawPopup(&d->dpDateTime, &d->pm, ui);
	consume(d, &d->dpDateTime, "日期时间");
	UiDatePicker_DrawPopup(&d->dpRangeDate, &d->pm, ui);
	consume(d, &d->dpRangeDate, "日期范围");
	UiDatePicker_DrawPopup(&d->dpRangeDateTime, &d->pm, ui);
	consume(d, &d->dpRangeDateTime, "时间范围");
}

static int on_chr(UiApp *app, wchar_t ch, void *user)
{
	Demo *d = (Demo *)user;
	if (UiDatePicker_Char(&d->dpDate, &app->ui, ch)) return 1;
	if (UiDatePicker_Char(&d->dpDateTime, &app->ui, ch)) return 1;
	if (UiDatePicker_Char(&d->dpRangeDate, &app->ui, ch)) return 1;
	if (UiDatePicker_Char(&d->dpRangeDateTime, &app->ui, ch)) return 1;
	return 0;
}

static void on_ime(UiApp *app, void *user)
{
	Demo *d = (Demo *)user;
	UiDatePicker_OnIme(&d->dpDate, &app->ui);
	UiDatePicker_OnIme(&d->dpDateTime, &app->ui);
	UiDatePicker_OnIme(&d->dpRangeDate, &app->ui);
	UiDatePicker_OnIme(&d->dpRangeDateTime, &app->ui);
}

static void on_timer(UiApp *app, void *user)
{
	Demo *d = (Demo *)user;
	UiDatePicker_Blink(&d->dpDate, &app->ui);
	UiDatePicker_Blink(&d->dpDateTime, &app->ui);
	UiDatePicker_Blink(&d->dpRangeDate, &app->ui);
	UiDatePicker_Blink(&d->dpRangeDateTime, &app->ui);
}

static void on_shutdown(UiApp *app, void *user)
{
	Demo *d = (Demo *)user;
	(void)app;
	UiDatePicker_Shutdown(&d->dpDate);
	UiDatePicker_Shutdown(&d->dpDateTime);
	UiDatePicker_Shutdown(&d->dpRangeDate);
	UiDatePicker_Shutdown(&d->dpRangeDateTime);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	Demo *d = (Demo *)user;
	UiContext *ui = &app->ui;
	UiId navs[5];
	int i;

	if (UiDatePicker_Key(&d->dpDate, &d->pm, ui, vk)) {
		consume(d, &d->dpDate, "日期"); return 1;
	}
	if (UiDatePicker_Key(&d->dpDateTime, &d->pm, ui, vk)) {
		consume(d, &d->dpDateTime, "日期时间"); return 1;
	}
	if (UiDatePicker_Key(&d->dpRangeDate, &d->pm, ui, vk)) {
		consume(d, &d->dpRangeDate, "日期范围"); return 1;
	}
	if (UiDatePicker_Key(&d->dpRangeDateTime, &d->pm, ui, vk)) {
		consume(d, &d->dpRangeDateTime, "时间范围"); return 1;
	}
	if (UiPopupManager_Key(&d->pm, ui, vk)) return 1;
	if (UiPopupManager_AnyOpen(&d->pm)) {
		if (vk == VK_TAB) { UiPopupManager_Close(&d->pm, ui); UiApp_Invalidate(app); return 1; }
		return 0;
	}
	navs[0] = d->dpDate.id;
	navs[1] = d->dpDateTime.id;
	navs[2] = d->dpRangeDate.id;
	navs[3] = d->dpRangeDateTime.id;
	navs[4] = d->themeBtnId;
	if (vk == VK_TAB) {
		int dir = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
		for (i = 0; i < 5; i++) if (ui->navId == navs[i]) break;
		if (i == 5) i = dir > 0 ? 4 : 0;
		Ui_SetNavId(ui, navs[(i + dir + 5) % 5]);
		UiApp_Invalidate(app);
		return 1;
	}
	if (ui->navId == d->themeBtnId && (vk == VK_RETURN || vk == VK_SPACE)) {
		UiTheme_SetTheme(&app->theme, app->theme.themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(app);
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
	cb.key = on_key;
	cb.chr = on_chr;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI DatePicker", 720, 940, UI_THEME_LIGHT, &cb, &g_demo);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif