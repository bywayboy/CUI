/*
 * tabs.c - TabBar + TabPager 样例
 *
 * LINE/PILL 两种 header, 横向/纵向滑动、可选淡入淡出、反向翻页。
 * 页面转场期间页面控件输入被锁定, header 仍可继续点击.
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_icon.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/ui_tab.h"
#include "ui/ui_widgets.h"

typedef struct TabsDemo {
	UiApp *app;
	UiTabBar bar;
	UiTabPager pager;
	UiTabStyle line, pill, card;
	int styleMode;    /* 0=LINE, 1=PILL, 2=CARD */
	int fade;
	int reverse;
	int axisMode;      /* 0=H, 1=V, 2=fade only */
	int netConnected;
	int advancedOn;
	int pendingPage;
	char status[192];
} TabsDemo;

static TabsDemo g_tabs;

static const char *const kLabels[4] = { "概览", "网络", "高级", "关于" };
static const char *const kAxisText[3] = { "横向", "纵向", "淡入" };

static void status_set(TabsDemo *d, const char *text)
{
	int i = 0;
	if (!text) return;
	while (text[i] && i < (int)sizeof(d->status) - 1) {
		d->status[i] = text[i];
		i++;
	}
	d->status[i] = 0;
}

static void draw_metric(UiContext *ui, Gfx *g, UiRect rc, const char *title,
	const char *value, UiColor accent)
{
	UiTheme *t = &g_tabs.app->theme;
	UiRect stripe = Ui_Rect(rc.x, rc.y, 4.0f, rc.h);
	Ui_Panel(ui, rc);
	Gfx_FillRoundRect(g, stripe, 2.0f, accent);
	Gfx_Text(g, t->fSmall, title, Ui_Rect(rc.x + 18.0f, rc.y + 12.0f,
		rc.w - 30.0f, 18.0f), t->text3, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fBig, value, Ui_Rect(rc.x + 18.0f, rc.y + 32.0f,
		rc.w - 30.0f, 34.0f), t->text, GFX_TEXT_VCENTER);
}

static void draw_page_overview(TabsDemo *d, UiContext *ui, Gfx *g, UiRect rc)
{
	UiTheme *t = &d->app->theme;
	UiRect title = Ui_Rect(rc.x, rc.y, rc.w, 30.0f);
	UiRect metrics = Ui_Rect(rc.x, rc.y + 42.0f, rc.w, 92.0f);
	UiRect icons = Ui_Rect(rc.x, rc.y + 146.0f, rc.w, 50.0f);
	UiRect note = Ui_Rect(rc.x, rc.y + 208.0f, rc.w, rc.h - 208.0f);
	UiRect m[3];
	UiFixedSize fx[3] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
	UiMeasureItem items[3] = {
		Ui_FixedItem(&fx[0], 1.0f), Ui_FixedItem(&fx[1], 1.0f),
		Ui_FixedItem(&fx[2], 1.0f) };
	UiButtonStyle pri = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	static const uint32_t iconCodes[5] = {
		0xE713u, 0xE774u, 0xE946u, 0xE72Cu, 0xE713u
	};
	static const char *const iconNames[5] = {
		"设置", "网络", "信息", "刷新", "禁用"
	};
	UiColor iconColors[5];
	int k;

	Gfx_Text(g, t->fBig, "ScrollView · TabBar · Pager", title,
		t->text, GFX_TEXT_VCENTER);
	if (!Ui_FlexLayout(metrics, UI_ROW, 12.0f, items, 3, m)) {
		m[0] = Ui_Rect(metrics.x, metrics.y, 180, 92);
		m[1] = Ui_Rect(metrics.x + 192, metrics.y, 180, 92);
		m[2] = Ui_Rect(metrics.x + 384, metrics.y, 180, 92);
	}
	draw_metric(ui, g, m[0], "页面", "4", t->primary.base);
	draw_metric(ui, g, m[1], "转场", "260 ms", t->success.base);
	draw_metric(ui, g, m[2], "输入", "转场锁定", t->warning.base);

	iconColors[0] = t->primary.base;
	iconColors[1] = t->success.base;
	iconColors[2] = t->text2;
	iconColors[3] = t->warning.base;
	iconColors[4] = t->text4;
	for (k = 0; k < 5; k++) {
		UiRect ir = Ui_Rect(icons.x + (float)k * 66.0f, icons.y, 30.0f, 30.0f);
		UiRect lr = Ui_Rect(ir.x - 10.0f, ir.y + 30.0f, 50.0f, 16.0f);
		UiIcon ic = UiIcon_Glyph(t->fIcon, iconCodes[k]);
		UiIcon_Draw(ui, &ic, ir, iconColors[k], k != 4);
		Gfx_Text(g, t->fMini, iconNames[k], lr,
			k == 4 ? t->text4 : t->text3, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}

	Ui_Panel(ui, note);
	Gfx_Text(g, t->fBody,
		"TabBar 只负责选择与指示条；Pager 负责当前页/目标页/进度。"
		"页面内容仍是即时模式绘制，切换时旧页和新页同时进入转场。",
		Ui_Rect(note.x + 18.0f, note.y + 14.0f, note.w - 36.0f, 40.0f),
		t->text2, 0);
	if (Ui_ButtonEx(ui, Ui_IdIndex("tab.page.cta", 0, 0, 0),
		Ui_Rect(note.x + 18.0f, note.y + note.h - 46.0f, 130.0f, 32.0f),
		"进入网络页", &pri, 1))
		d->pendingPage = 1;
}

static void draw_page_network(TabsDemo *d, UiContext *ui, Gfx *g, UiRect rc)
{
	UiTheme *t = &d->app->theme;
	UiRect title = Ui_Rect(rc.x, rc.y, rc.w, 30.0f);
	UiRect row1 = Ui_Rect(rc.x, rc.y + 64.0f, rc.w, 52.0f);
	UiRect row2 = Ui_Rect(rc.x, rc.y + 124.0f, rc.w, 52.0f);
	UiButtonStyle pri = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle def = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);

	Gfx_Text(g, t->fBig, "网络", title, t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fBody, "页面内控件在转场期间不注册命中；header 仍然可以点。",
		Ui_Rect(rc.x, rc.y + 32.0f, rc.w, 20.0f), t->text3, GFX_TEXT_VCENTER);

	Ui_Panel(ui, row1);
	Gfx_Text(g, t->fLabel, "连接", Ui_Rect(row1.x + 18, row1.y, 120, row1.h),
		t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fBody, d->netConnected ? "已连接 · 192.168.1.20" : "未连接",
		Ui_Rect(row1.x + 130, row1.y, row1.w - 320, row1.h),
		d->netConnected ? t->success.base : t->text3, GFX_TEXT_VCENTER);
	if (Ui_ButtonEx(ui, Ui_IdIndex("tab.page.net", 1, 0, 0),
		Ui_Rect(row1.x + row1.w - 112, row1.y + 10, 94, 32),
		d->netConnected ? "断开" : "连接", &pri, 1)) {
		d->netConnected = !d->netConnected;
		status_set(d, d->netConnected ? "网络已连接" : "网络已断开");
	}

	Ui_Panel(ui, row2);
	Gfx_Text(g, t->fLabel, "代理", Ui_Rect(row2.x + 18, row2.y, 120, row2.h),
		t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fBody, "自动 · 系统设置",
		Ui_Rect(row2.x + 130, row2.y, row2.w - 320, row2.h),
		t->text3, GFX_TEXT_VCENTER);
	if (Ui_ButtonEx(ui, Ui_IdIndex("tab.page.net", 1, 1, 0),
		Ui_Rect(row2.x + row2.w - 112, row2.y + 10, 94, 32),
		"配置", &def, 1))
		status_set(d, "代理配置（示例）");
}

static void draw_page_advanced(TabsDemo *d, UiContext *ui, Gfx *g, UiRect rc)
{
	UiTheme *t = &d->app->theme;
	UiRect title = Ui_Rect(rc.x, rc.y, rc.w, 30.0f);
	UiRect card = Ui_Rect(rc.x, rc.y + 44.0f, rc.w, 120.0f);
	UiRect btn1 = Ui_Rect(card.x + 18, card.y + 72, 130, 32);
	UiRect btn2 = Ui_Rect(card.x + 158, card.y + 72, 130, 32);
	UiButtonStyle on = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle off = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);

	Gfx_Text(g, t->fBig, "高级", title, t->text, GFX_TEXT_VCENTER);
	Ui_Panel(ui, card);
	Gfx_Text(g, t->fLabel, "实验性功能", Ui_Rect(card.x + 18, card.y + 14,
		card.w - 36, 22), t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "关闭后页面内容保持不变；仅影响本示例状态。",
		Ui_Rect(card.x + 18, card.y + 38, card.w - 36, 20), t->text3,
		GFX_TEXT_VCENTER);
	if (Ui_ButtonEx(ui, Ui_IdIndex("tab.page.adv", 2, 0, 0), btn1,
		d->advancedOn ? "已开启" : "开启", d->advancedOn ? &on : &off, 1)) {
		d->advancedOn = 1;
		status_set(d, "实验性功能已开启");
	}
	if (Ui_ButtonEx(ui, Ui_IdIndex("tab.page.adv", 2, 1, 0), btn2,
		"关闭", &off, 1)) {
		d->advancedOn = 0;
		status_set(d, "实验性功能已关闭");
	}
}

static void draw_page_about(TabsDemo *d, UiContext *ui, Gfx *g, UiRect rc)
{
	UiTheme *t = &d->app->theme;
	UiRect title = Ui_Rect(rc.x, rc.y, rc.w, 30.0f);
	UiRect body = Ui_Rect(rc.x, rc.y + 44.0f, rc.w, 96.0f);
	UiButtonStyle def = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);

	Gfx_Text(g, t->fBig, "关于", title, t->text, GFX_TEXT_VCENTER);
	Ui_Panel(ui, body);
	Gfx_Text(g, t->fBody,
		"CUI TabBar / TabPager 示例\n"
		"LINE/PILL、四向滑动、淡入淡出、反向翻页、转场输入锁。",
		Ui_Rect(body.x + 18, body.y + 14, body.w - 36, 62),
		t->text2, 0);
	if (Ui_ButtonEx(ui, Ui_IdIndex("tab.page.about", 3, 0, 0),
		Ui_Rect(body.x + 18, body.y + body.h - 46, 120, 32),
		"检查更新", &def, 1))
		status_set(d, "已是最新版本（示例）");
}

static void draw_page(UiContext *ui, void *user, int index, UiRect rc,
	int interactive)
{
	TabsDemo *d = (TabsDemo *)user;
	(void)interactive;   /* 转场时 UiContext inputBlock 已阻止命中 */
	switch (index) {
	case 0: draw_page_overview(d, ui, ui->gfx, rc); break;
	case 1: draw_page_network(d, ui, ui->gfx, rc); break;
	case 2: draw_page_advanced(d, ui, ui->gfx, rc); break;
	default: draw_page_about(d, ui, ui->gfx, rc); break;
	}
}

static void draw_header(TabsDemo *d, UiContext *ui, Gfx *g)
{
	UiTheme *t = &d->app->theme;
	float y = 22.0f, h = 30.0f, w = 74.0f, gap = 8.0f;
	float x = d->app->w - 22.0f - (w * 5.0f + gap * 4.0f);
	UiButtonStyle def = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle pri = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);

	Gfx_Text(g, t->fBig, "CUI Tabs", Ui_Rect(24, 16, x - 44.0f, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "TabBar 选择 + TabPager 转场；header 与页面输入分离",
		Ui_Rect(26, 46, x - 40.0f, 18), t->text3, GFX_TEXT_VCENTER);

	if (Ui_ButtonEx(ui, Ui_Id("tabs.style", 0, 0), Ui_Rect(x, y, w, h),
		d->styleMode == 0 ? "LINE" : (d->styleMode == 1 ? "PILL" : "CARD"),
		&def, 1)) {
		d->styleMode = (d->styleMode + 1) % 3;
		status_set(d, d->styleMode == 0 ? "TabBar: LINE" :
			(d->styleMode == 1 ? "TabBar: PILL" : "TabBar: CARD"));
	}
	x += w + gap;
	if (Ui_ButtonEx(ui, Ui_Id("tabs.fade", 0, 0), Ui_Rect(x, y, w, h),
		d->fade ? "淡入开" : "淡入关", d->fade ? &pri : &def, 1)) {
		d->fade = !d->fade;
		status_set(d, d->fade ? "淡入淡出开启" : "淡入淡出关闭");
	}
	x += w + gap;
	if (Ui_ButtonEx(ui, Ui_Id("tabs.reverse", 0, 0), Ui_Rect(x, y, w, h),
		d->reverse ? "反向开" : "反向关", d->reverse ? &pri : &def, 1)) {
		d->reverse = !d->reverse;
		status_set(d, d->reverse ? "翻页方向已反转" : "翻页方向正常");
	}
	x += w + gap;
	if (Ui_ButtonEx(ui, Ui_Id("tabs.axis", 0, 0), Ui_Rect(x, y, w, h),
		kAxisText[d->axisMode], &def, 1)) {
		d->axisMode = (d->axisMode + 1) % 3;
		status_set(d, d->axisMode == 0 ? "转场: 横向" :
			(d->axisMode == 1 ? "转场: 纵向" : "转场: 仅淡入"));
	}
	x += w + gap;
	if (Ui_ButtonEx(ui, Ui_Id("tabs.theme", 0, 0), Ui_Rect(x, y, w, h),
		"主题", &def, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(d->app);
	}
}

static void on_frame(UiApp *app, void *user)
{
	TabsDemo *d = (TabsDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiRect card, content, headerRc, footer;
	UiStack col;
	UiTabStyle style;
	int prev, clicked;

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	draw_header(d, ui, g);

	card = Ui_Rect(22, 90, app->w - 44, app->h - 138);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 22, 16, 22, 16);
	style = d->styleMode == 0 ? d->line :
		(d->styleMode == 1 ? d->pill : d->card);
	headerRc = Ui_StackTake(&col, style.height > 0.0f ? style.height : 38.0f);
	Ui_StackSkip(&col, d->styleMode == 2 ? 0.0f : 14.0f);
	content = Ui_StackTakeFill(&col);

	prev = d->bar.selected;
	clicked = UiTabBar_Draw(&d->bar, ui, headerRc, kLabels, &style, 1);
	if (clicked >= 0 && d->bar.selected != prev) {
		UiTabPager_GoTo(&d->pager, ui, d->bar.selected, 1);
		wsprintfA(d->status, "切换到：%s", kLabels[d->bar.selected]);
	}

	if (d->axisMode == 0) d->pager.cfg.axis = UI_TAB_AXIS_HORIZONTAL;
	else if (d->axisMode == 1) d->pager.cfg.axis = UI_TAB_AXIS_VERTICAL;
	else d->pager.cfg.axis = UI_TAB_AXIS_NONE;
	d->pager.cfg.fade = d->fade;
	d->pager.cfg.reverse = d->reverse;
	UiTabPager_Draw(&d->pager, ui, content, draw_page, d);

	if (d->styleMode == 2) {
		/* CARD: 面板顶边只画活动 tab 以外的区间; 活动 tab 最后覆盖接缝. */
		UiRect ar = UiTabBar_ActiveRect(&d->bar);
		UiColor line = Ui_ColorAlpha(t->neutral.border, 220);
		if (ar.w > 0.0f) {
			if (ar.x > content.x)
				Gfx_FillRoundRect(g, Ui_Rect(content.x, content.y - 0.5f,
					ar.x - content.x, 1.0f), 0.0f, line);
			if (ar.x + ar.w < content.x + content.w)
				Gfx_FillRoundRect(g, Ui_Rect(ar.x + ar.w, content.y - 0.5f,
					content.x + content.w - (ar.x + ar.w), 1.0f), 0.0f, line);
		} else {
			Gfx_FillRoundRect(g, Ui_Rect(content.x, content.y - 0.5f,
				content.w, 1.0f), 0.0f, line);
		}
		UiTabBar_DrawActive(&d->bar, ui, kLabels, &style);
	}

	footer = Ui_Rect(24, app->h - 38, app->w - 48, 20);
	{
		char text[320];
		wsprintfA(text, "页面 %d/4 · %s · %s · 进度 %d%%",
			d->pager.to + 1, kLabels[d->pager.to], d->status,
			(int)(d->pager.progress * 100.0f));
		Gfx_Text(g, t->fSmall, text, footer, t->text3, GFX_TEXT_VCENTER);
	}
}

static int on_animating(UiApp *app, void *user)
{
	TabsDemo *d = (TabsDemo *)user;
	(void)app;
	return UiTabPager_Busy(&d->pager);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	TabsDemo *d = (TabsDemo *)user;
	UiTabPager_Tick(&d->pager, &app->ui, (float)dtMs);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	TabsDemo *d = (TabsDemo *)user;
	int prev;
	if (vk == VK_TAB) {
		Ui_SetNavId(&app->ui, d->bar.id);
		UiTabBar_SetFocusVisible(&d->bar, 1);
		UiApp_Invalidate(app);
		return 1;
	}
	if (vk == VK_PRIOR || vk == VK_NEXT) {
		int n = d->bar.count;
		int idx = d->pager.to + (vk == VK_NEXT ? 1 : -1);
		if (idx < 0) idx = n - 1;
		if (idx >= n) idx = 0;
		UiTabBar_SetSelected(&d->bar, idx);
		UiTabPager_GoTo(&d->pager, &app->ui, idx, 1);
		UiApp_Invalidate(app);
		return 1;
	}
	prev = d->bar.selected;
	if (UiTabBar_Key(&d->bar, &app->ui, vk)) {
		if (d->bar.changed && d->bar.selected != prev) {
			UiTabPager_GoTo(&d->pager, &app->ui, d->bar.selected, 1);
			wsprintfA(d->status, "键盘切换到：%s", kLabels[d->bar.selected]);
		}
		UiApp_Invalidate(app);
		return 1;
	}
	return 0;
}

static void on_after_frame(UiApp *app, void *user)
{
	TabsDemo *d = (TabsDemo *)user;
	if (d->pendingPage >= 0) {
		UiTabBar_SetSelected(&d->bar, d->pendingPage);
		UiTabPager_GoTo(&d->pager, &app->ui, d->pendingPage, 1);
		d->pendingPage = -1;
		UiApp_Invalidate(app);
	}
}

static void on_init(UiApp *app, void *user)
{
	TabsDemo *d = (TabsDemo *)user;
	UiTabPagerConfig cfg;
	d->app = app;
	app->minW = 720.0f;
	app->minH = 520.0f;
	d->line = UiTabStyle_Line();
	d->pill = UiTabStyle_Pill();
	d->card = UiTabStyle_Card();
	d->pendingPage = -1;
	d->status[0] = 0;
	UiTabBar_Init(&d->bar, Ui_Id("tabs.bar", 0, 0), 4);
	ZeroMemory(&cfg, sizeof(cfg));
	cfg.axis = UI_TAB_AXIS_HORIZONTAL;
	cfg.durationMs = 260.0f;
	UiTabPager_Init(&d->pager, Ui_Id("tabs.pager", 0, 0), &cfg);
	status_set(&g_tabs, "点击页签或按 Tab 后使用方向键。");
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
	return UiApp_Run(L"CUI Tabs", 980, 680, UI_THEME_LIGHT, &cb, &g_tabs);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
