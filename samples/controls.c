/*
 * controls.c - Checkbox / RadioGroup / Switch / Slider + Tooltip 样例
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_controls.h"
#include "ui/ui_icon.h"
#include "ui/ui_layout.h"
#include "ui/ui_popup.h"
#include "ui/ui_tooltip.h"
#include "ui/ui_widgets.h"

typedef struct ControlsDemo {
	UiApp *app;
	UiCheckbox checkA, checkB;
	UiCheckbox checkC;          /* disabled */
	UiCheckboxGroup features;
	UiRadioGroup mode;
	UiSwitch sw;
	UiSlider slider;
	UiTooltip tip;
	UiRect iconRc[4];
	UiId iconId[4];
	char status[192];
} ControlsDemo;

static ControlsDemo g_ctrl;

static const char *const kFeatureLabels[3] = {
	"开机启动", "自动检查更新", "发送使用统计"
};
static const char *const kModeLabels[3] = {
	"标准模式", "节能模式", "高性能模式"
};
static const char *const kIconTips[4] = {
	"设置", "刷新", "信息", "删除"
};
static const uint32_t kIconCodes[4] = {
	0xE713u, 0xE72Cu, 0xE946u, 0xE74Du
};

static void set_status(ControlsDemo *d, const char *s)
{
	int i = 0;
	while (s && s[i] && i < (int)sizeof(d->status) - 1) {
		d->status[i] = s[i];
		i++;
	}
	d->status[i] = 0;
	UiApp_Invalidate(d->app);
}

static void draw_icon_strip(ControlsDemo *d, UiContext *ui, UiRect rc)
{
	UiTheme *t = &d->app->theme;
	UiButtonStyle style = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	int i;
	for (i = 0; i < 4; i++) {
		UiIcon icon = UiIcon_Glyph(t->fIcon, kIconCodes[i]);
		d->iconRc[i] = Ui_Rect(rc.x + (float)i * 46.0f, rc.y, 36.0f, 34.0f);
		d->iconId[i] = Ui_Id("ctrl.icon", i, 0);
		if (Ui_IconButton(ui, d->iconId[i], d->iconRc[i], &icon, &style, 1))
			set_status(d, kIconTips[i]);
	}
}

static void update_tooltip(ControlsDemo *d, UiContext *ui)
{
	UiId hover = ui->hover;
	int i;
	for (i = 0; i < 4; i++) {
		if (hover == d->iconId[i]) {
			UiTooltip_Update(&d->tip, ui, d->iconId[i], d->iconRc[i],
				kIconTips[i], UiFrame_Now());
			return;
		}
	}
	UiTooltip_Update(&d->tip, ui, UI_ID_NONE, Ui_Rect(0, 0, 0, 0),
		"", UiFrame_Now());
}

static void on_frame(UiApp *app, void *user)
{
	ControlsDemo *d = (ControlsDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiRect card, row, footer;
	UiStack col;
	UiRect checks[3];
	UiFixedSize fx[3] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
	UiMeasureItem two[3];
	int i;
	UiButtonStyle iconStyle = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiIcon sun = UiIcon_Glyph(t->fIcon, 0xE706u);

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_Text(g, t->fBig, "CUI Controls", Ui_Rect(24, 14, app->w - 150, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "Checkbox · RadioGroup · Switch · Slider · Tooltip",
		Ui_Rect(26, 46, app->w - 150, 18), t->text3, GFX_TEXT_VCENTER);
	if (Ui_IconButton(ui, Ui_Id("ctrl.theme", 0, 0),
		Ui_Rect(app->w - 66, 20, 36, 34), &sun, &iconStyle, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(app);
	}

	card = Ui_Rect(22, 80, app->w - 44, app->h - 126);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 22, 16, 22, 16);

	Gfx_Text(g, t->fLabel, "图标按钮 + Tooltip", Ui_StackTake(&col, 18),
		t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	draw_icon_strip(d, ui, Ui_StackTake(&col, 36));
	Ui_StackSkip(&col, 16);

	Gfx_Text(g, t->fLabel, "Checkbox / CheckboxGroup", Ui_StackTake(&col, 18),
		t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	row = Ui_StackTake(&col, 28);
	for (i = 0; i < 3; i++) two[i] = Ui_FixedItem(&fx[i], 1.0f);
	if (!Ui_FlexLayout(row, UI_ROW, 20, two, 3, checks)) {
		checks[0] = Ui_Rect(row.x, row.y, row.w / 3.0f, row.h);
		checks[1] = Ui_Rect(row.x + row.w / 3.0f, row.y, row.w / 3.0f, row.h);
		checks[2] = Ui_Rect(row.x + row.w * 2.0f / 3.0f, row.y,
			row.w / 3.0f, row.h);
	}
	if (Ui_Checkbox(&d->checkA, ui, checks[0], "启用通知", 1))
		set_status(d, d->checkA.checked ? "通知已启用" : "通知已关闭");
	if (Ui_Checkbox(&d->checkB, ui, checks[1], "部分应用", 1))
		set_status(d, d->checkB.indeterminate ? "部分选中" :
			(d->checkB.checked ? "全部应用" : "未选择应用"));
	Ui_Checkbox(&d->checkC, ui, checks[2], "禁用项", 0);
	Ui_StackSkip(&col, 4);
	if (UiCheckboxGroup_Draw(&d->features, ui, Ui_StackTake(&col, 84),
		kFeatureLabels, 28, 1))
		wsprintfA(d->status, "功能 mask=0x%X", d->features.mask);
	Ui_StackSkip(&col, 16);

	Gfx_Text(g, t->fLabel, "RadioGroup", Ui_StackTake(&col, 18),
		t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	if (UiRadioGroup_Draw(&d->mode, ui, Ui_StackTake(&col, 84),
		kModeLabels, 28, 1))
		wsprintfA(d->status, "模式：%s", kModeLabels[d->mode.selected]);
	Ui_StackSkip(&col, 16);

	Gfx_Text(g, t->fLabel, "Switch / Slider", Ui_StackTake(&col, 18),
		t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	if (Ui_Switch(&d->sw, ui, Ui_StackTake(&col, 28), "Wi-Fi", 1))
		set_status(d, d->sw.on ? "Wi-Fi 已开启" : "Wi-Fi 已关闭");
	Ui_StackSkip(&col, 8);
	Ui_Slider(&d->slider, ui, Ui_StackTake(&col, 30), "音量", 1);
	if (d->slider.changed)
		wsprintfA(d->status, "音量：%d%%",
			(int)(d->slider.value * 100.0f + 0.5f));

	footer = Ui_Rect(24, app->h - 34, app->w - 48, 18);
	Gfx_Text(g, t->fSmall, d->status, footer, t->text3, GFX_TEXT_VCENTER);
	update_tooltip(d, ui);
	UiTooltip_Draw(&d->tip, ui);
}

static int on_animating(UiApp *app, void *user)
{
	ControlsDemo *d = (ControlsDemo *)user;
	(void)app;
	return UiTooltip_Pending(&d->tip);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	ControlsDemo *d = (ControlsDemo *)user;
	(void)dtMs;
	if (UiTooltip_Pending(&d->tip)) UiApp_Invalidate(app);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	ControlsDemo *d = (ControlsDemo *)user;
	UiId navs[5];
	int i, prev;
	navs[0] = d->checkA.id;
	navs[1] = d->features.id;
	navs[2] = d->mode.id;
	navs[3] = d->sw.id;
	navs[4] = d->slider.id;
	if (vk == VK_TAB) {
		for (i = 0; i < 5; i++) if (app->ui.navId == navs[i]) break;
		Ui_SetNavId(&app->ui, navs[(i + 1) % 5]);
		UiApp_Invalidate(app);
		return 1;
	}
	if (app->ui.navId == d->checkA.id) {
		if (UiCheckbox_Key(&d->checkA, &app->ui, vk)) {
			set_status(d, d->checkA.checked ? "通知已启用" : "通知已关闭");
			return 1;
		}
	}
	if (app->ui.navId == d->features.id) {
		prev = d->features.mask;
		if (UiCheckboxGroup_Key(&d->features, &app->ui, vk)) {
			if (d->features.mask != prev)
				wsprintfA(d->status, "功能 mask=0x%X", d->features.mask);
			UiApp_Invalidate(app);
			return 1;
		}
	}
	if (app->ui.navId == d->mode.id) {
		prev = d->mode.selected;
		if (UiRadioGroup_Key(&d->mode, &app->ui, vk)) {
			if (d->mode.selected != prev)
				wsprintfA(d->status, "模式：%s", kModeLabels[d->mode.selected]);
			UiApp_Invalidate(app);
			return 1;
		}
	}
	if (app->ui.navId == d->sw.id) {
		if (UiSwitch_Key(&d->sw, &app->ui, vk)) {
			set_status(d, d->sw.on ? "Wi-Fi 已开启" : "Wi-Fi 已关闭");
			return 1;
		}
	}
	if (app->ui.navId == d->slider.id) {
		if (UiSlider_Key(&d->slider, &app->ui, vk)) {
			wsprintfA(d->status, "音量：%d%%",
				(int)(d->slider.value * 100.0f + 0.5f));
			UiApp_Invalidate(app);
			return 1;
		}
	}
	return 0;
}

static void on_init(UiApp *app, void *user)
{
	ControlsDemo *d = (ControlsDemo *)user;
	d->app = app;
	app->minW = 760.0f;
	app->minH = 620.0f;
	UiCheckbox_Init(&d->checkA, Ui_Id("ctrl.check.a", 0, 0), 1);
	UiCheckbox_Init(&d->checkB, Ui_Id("ctrl.check.b", 0, 0), 1);
	d->checkB.indeterminate = 1;
	UiCheckbox_Init(&d->checkC, Ui_Id("ctrl.check.c", 0, 0), 1);
	UiCheckboxGroup_Init(&d->features, Ui_Id("ctrl.features", 0, 0), 3);
	UiCheckboxGroup_SetMask(&d->features, 0x1u);
	UiRadioGroup_Init(&d->mode, Ui_Id("ctrl.mode", 0, 0), 3);
	UiRadioGroup_SetSelected(&d->mode, 0);
	UiSwitch_Init(&d->sw, Ui_Id("ctrl.wifi", 0, 0), 1);
	UiSlider_Init(&d->slider, Ui_Id("ctrl.volume", 0, 0), 0.65f);
	UiTooltip_Init(&d->tip, Ui_Id("ctrl.tooltip", 0, 0));
	UiTooltip_SetDelay(&d->tip, 450);
	set_status(d, "Tab 切换焦点；方向键/空格操作。");
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.animating = on_animating;
	cb.tick = on_tick;
	cb.key = on_key;
	return UiApp_Run(L"CUI Controls", 900, 720, UI_THEME_LIGHT, &cb, &g_ctrl);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
