/*
 * scrollview.c - ScrollView 样例
 *
 * 左: 10,000 行固定高度列表 (只绘制可见行)
 * 右: 40 个变高卡片 (measure 缓存 + 展开/收起 + 自动 contentH)
 * 演示: 滚轮平滑、甩动惯性、触摸模式回弹、el-scrollbar 风格自动隐藏滚动条。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/ui_widgets.h"
#include "ui/ui_scroll.h"

#define FIXED_COUNT 10000
#define CARD_COUNT  40
#define ROW_H       34.0f

typedef struct Card {
	uint32_t rev;
	int expanded;
} Card;

typedef struct ScrollDemo {
	UiApp *app;
	UiScrollView fixedSv, varSv;
	UiScrollLayout fixedLayout, varLayout;
	UiScrollItem varItems[CARD_COUNT];
	Card cards[CARD_COUNT];
	char fixedStatus[160];
	char varStatus[160];
	char status[192];
	int pendingToggle;
	int fixedFirst, fixedLast, fixedDrawn;
	int varFirst, varLast, varDrawn;
} ScrollDemo;

static ScrollDemo g_demo;

static const char *const kCardLinesA[4] = {
	"这个卡片的高度由内容测量得到。",
	"点击卡片会展开或收起，",
	"布局缓存只重测这一项，",
	"前缀和与 contentH 随之更新。"
};
static const char *const kCardLinesB[4] = {
	"右侧视图使用触摸配置，允许回弹。",
	"左侧是桌面配置，滚轮平滑。",
	"不可见行不会绘制，",
	"也不会注册命中。"
};
static const char *const kExpandedLines[2] = {
	"展开后追加两行任意高度内容。",
	"ScrollView 不关心子控件类型。"
};

static const char *const *card_lines(const Card *c, int *count)
{
	int n = (c->rev & 1u) ? 3 : 4;
	if (c->expanded) n += 2;
	*count = n;
	return (c->rev & 1u) ? kCardLinesA : kCardLinesB;
}

static float card_measure(const Card *c, float width)
{
	int n;
	float h;
	(void)width;
	card_lines(c, &n);
	h = 48.0f + (float)n * 18.0f;
	if (c->expanded) h += 8.0f;
	return h;
}

static void vm_status(ScrollDemo *d)
{
	wsprintfA(d->status,
		"fixed offset=%d/%d  visible=%d..%d (%d 行)    var offset=%d/%d  visible=%d..%d (%d 卡)",
		(int)d->fixedSv.offset, (int)UiScrollView_MaxOffset(&d->fixedSv),
		d->fixedFirst, d->fixedLast, d->fixedDrawn,
		(int)d->varSv.offset, (int)UiScrollView_MaxOffset(&d->varSv),
		d->varFirst, d->varLast, d->varDrawn);
}

static void draw_header(ScrollDemo *d, UiContext *ui, Gfx *g)
{
	UiTheme *t = &d->app->theme;
	UiRect themeRc = Ui_Rect(d->app->w - 132.0f, 24.0f, 110.0f, 34.0f);
	UiButtonStyle s = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);

	Gfx_Text(g, t->fBig, "CUI ScrollView", Ui_Rect(24, 16, d->app->w - 220.0f, 34),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall,
		"视口裁剪 + 可见区间 + 高度缓存 + 惯性/回弹 + el-scrollbar 风格滚动条",
		Ui_Rect(26, 50, d->app->w - 200.0f, 20), t->text3, GFX_TEXT_VCENTER);
	if (Ui_ButtonEx(ui, Ui_Id("scroll.theme", 0, 0), themeRc, "切换主题", &s, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(d->app);
	}
}

static void draw_fixed_rows(ScrollDemo *d, UiContext *ui, Gfx *g,
	UiRect viewport, float innerW)
{
	UiTheme *t = &d->app->theme;
	UiScrollLayout *l = &d->fixedLayout;
	UiRect content, row;
	int i, first = 0, last = -1, drawn = 0;

	UiScrollView_Begin(&d->fixedSv, ui, viewport, innerW, l->contentH);
	content = UiScrollView_ContentRect(&d->fixedSv);
	UiScrollLayout_VisibleRange(l, d->fixedSv.offset, viewport.h, &first, &last);
	for (i = first; i <= last; i++) {
		UiId id;
		UiWidgetState st;
		if (i < 0 || i >= l->count) continue;
		row = UiScrollLayout_ItemRect(l, i, content);
		if (UiScrollView_Cull(&d->fixedSv, row)) continue;
		id = Ui_IdIndex("scroll.row", 0, 0, i);
		st = Ui_State(ui, id, row, 1);
		if (i & 1)
			Gfx_FillRoundRectCorners(g, Ui_RectInset(row, 1.0f),
				Gfx_CornersAll(6), Ui_ColorAlpha(t->text3, 9));
		if (st.hovered)
			Gfx_FillRoundRectCorners(g, Ui_RectInset(row, 1.0f),
				Gfx_CornersAll(6), Ui_ColorAlpha(t->primary.base, 18));
		Gfx_FillRoundRect(g, Ui_Rect(row.x + 10.0f, row.y + row.h * 0.5f - 2.0f,
			4.0f, 4.0f), 2.0f, i == 0 ? t->primary.base : t->text4);
		{
			char line[64];
			wsprintfA(line, "行 %05d", i);
			Gfx_Text(g, t->fBody, line,
				Ui_Rect(row.x + 24.0f, row.y, row.w - 120.0f, row.h),
				t->text, GFX_TEXT_VCENTER);
			Gfx_Text(g, t->fSmall, "固定高度 34 DIP", 
				Ui_Rect(row.x + row.w - 130.0f, row.y, 118.0f, row.h),
				t->text4, GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
		}
		drawn++;
	}
	UiScrollView_End(&d->fixedSv, ui);
	d->fixedFirst = first;
	d->fixedLast = last;
	d->fixedDrawn = drawn;
}

static void draw_var_cards(ScrollDemo *d, UiContext *ui, Gfx *g,
	UiRect viewport, float innerW)
{
	UiTheme *t = &d->app->theme;
	UiScrollLayout *l = &d->varLayout;
	UiRect content, rc;
	int i, first = 0, last = -1, drawn = 0;

	UiScrollLayout_SetWidth(l, innerW);
	for (i = 0; i < l->count; i++) {
		if (UiScrollLayout_ItemNeedsMeasure(l, i, d->cards[i].rev))
			UiScrollLayout_SetItemHeight(l, i, d->cards[i].rev,
				card_measure(&d->cards[i], innerW));
	}
	UiScrollLayout_Commit(l);

	UiScrollView_Begin(&d->varSv, ui, viewport, innerW, l->contentH);
	content = UiScrollView_ContentRect(&d->varSv);
	UiScrollLayout_VisibleRange(l, d->varSv.offset, viewport.h, &first, &last);
	for (i = first; i <= last; i++) {
		UiId id;
		UiWidgetState st;
		UiColor bg, border;
		if (i < 0 || i >= l->count) continue;
		rc = UiScrollLayout_ItemRect(l, i, content);
		if (UiScrollView_Cull(&d->varSv, rc)) continue;
		id = Ui_IdIndex("scroll.card", 0, 0, i);
		st = Ui_State(ui, id, rc, 1);
		bg = d->cards[i].expanded ? Ui_ColorMix(t->card, t->primary.soft, 0.35f)
			: t->card;
		border = st.hovered ? t->primary.base : t->neutral.border;
		Gfx_FillRoundRectCorners(g, rc, Gfx_CornersAll(10.0f), bg);
		Gfx_StrokeRoundRectCorners(g, rc, Gfx_CornersAll(10.0f),
			Ui_ColorAlpha(border, st.hovered ? 190 : 255), 1.0f);
		{
			char title[64];
			const char *const *lines;
			int n, k;
			lines = card_lines(&d->cards[i], &n);
			wsprintfA(title, "卡片 #%02d  %s", i,
				d->cards[i].expanded ? "已展开" : "点击展开");
			Gfx_Text(g, t->fLabel, title,
				Ui_Rect(rc.x + 14.0f, rc.y + 8.0f, rc.w - 28.0f, 20.0f),
				t->text, GFX_TEXT_VCENTER);
			for (k = 0; k < n; k++) {
				const char *ln = (k < 4) ? lines[k] : kExpandedLines[k - 4];
				Gfx_Text(g, t->fSmall, ln,
					Ui_Rect(rc.x + 14.0f, rc.y + 32.0f + (float)k * 18.0f,
						rc.w - 28.0f, 18.0f),
					k < 4 ? t->text2 : t->text3, GFX_TEXT_VCENTER);
			}
		}
		if (ui->clicked == id) d->pendingToggle = i;
		drawn++;
	}
	UiScrollView_End(&d->varSv, ui);
	d->varFirst = first;
	d->varLast = last;
	d->varDrawn = drawn;
}

static void draw_card_shell(ScrollDemo *d, UiContext *ui, Gfx *g, UiRect rc,
	const char *title, const char *hint, UiRect *viewport, int withButtons)
{
	UiTheme *t = &d->app->theme;
	UiStack col;
	UiRect header, body;
	UiButtonStyle def = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
	UiButtonStyle pri = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);

	Ui_Panel(ui, rc);
	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 16, 14, 16, 14);
	header = Ui_StackTake(&col, 30.0f);
	Ui_StackSkip(&col, 8.0f);
	body = Ui_StackTakeFill(&col);

	Gfx_Text(g, t->fHead, title, Ui_Rect(header.x, header.y,
		header.w - (withButtons ? 200.0f : 140.0f), header.h),
		t->text, GFX_TEXT_VCENTER);
	if (!withButtons)
		Gfx_Text(g, t->fMini, hint, Ui_Rect(header.x + header.w - 136.0f,
			header.y, 136.0f, header.h),
			t->text4, GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
	if (withButtons) {
		UiRect topRc = Ui_Rect(header.x + header.w - 194.0f, header.y + 2.0f, 60.0f, 26.0f);
		UiRect flingRc = Ui_Rect(header.x + header.w - 126.0f, header.y + 2.0f, 60.0f, 26.0f);
		UiRect themeRc = Ui_Rect(header.x + header.w - 58.0f, header.y + 2.0f, 58.0f, 26.0f);
		if (Ui_ButtonEx(ui, Ui_Id("scroll.top", 0, 0), topRc, "顶部", &def, 1))
			UiScrollView_ScrollTo(&d->fixedSv, ui, 0.0f, 1);
		if (Ui_ButtonEx(ui, Ui_Id("scroll.fling", 0, 0), flingRc, "甩动", &pri, 1))
			UiScrollView_Fling(&d->fixedSv, ui, 1500.0f);
		if (Ui_ButtonEx(ui, Ui_Id("scroll.flingup", 0, 0), themeRc, "上甩", &def, 1))
			UiScrollView_Fling(&d->fixedSv, ui, -1500.0f);
	}
	*viewport = body;
}

static void on_frame(UiApp *app, void *user)
{
	ScrollDemo *d = (ScrollDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiRect inner, cards[2], cardView, viewport;
	UiFixedSize fx[2] = { { 0, 0 }, { 0, 0 } };
	UiMeasureItem items[2];
	float innerW;
	UiStack foot;

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	draw_header(d, ui, g);

	inner = Ui_Rect(22, 82, app->w - 44, app->h - 130);
	items[0] = Ui_FixedItem(&fx[0], 3.0f); items[0].minSize = 300.0f;
	items[1] = Ui_FixedItem(&fx[1], 2.0f); items[1].minSize = 260.0f;
	if (!Ui_FlexLayout(inner, UI_ROW, 16.0f, items, 2, cards)) {
		cards[0] = Ui_Rect(inner.x, inner.y, inner.w * 0.6f, inner.h);
		cards[1] = Ui_Rect(inner.x + inner.w * 0.6f + 16.0f, inner.y,
			inner.w * 0.4f - 16.0f, inner.h);
	}

	draw_card_shell(d, ui, g, cards[0],
		"10,000 行固定高度", "10k · 可见裁剪",
		&viewport, 1);
	innerW = UiScrollView_ContentWidth(&d->fixedSv, viewport);
	draw_fixed_rows(d, ui, g, viewport, innerW);

	draw_card_shell(d, ui, g, cards[1],
		"40 个变高卡片", "",
		&viewport, 0);
	{
		UiRect bounceRc = Ui_Rect(cards[1].x + cards[1].w - 80.0f,
			cards[1].y + 16.0f, 64.0f, 26.0f);
		UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_SUCCESS,
			UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
		if (Ui_ButtonEx(ui, Ui_Id("scroll.bounce", 0, 0), bounceRc,
			"回弹", &plain, 1)) {
			UiScrollView_ScrollTo(&d->varSv, ui, 0.0f, 0);
			UiScrollView_Fling(&d->varSv, ui, -3200.0f);
		}
	}
	innerW = UiScrollView_ContentWidth(&d->varSv, viewport);
	draw_var_cards(d, ui, g, viewport, innerW);

	vm_status(d);
	Ui_StackBegin(&foot, Ui_Rect(24, app->h - 42, app->w - 48, 20),
		UI_ROW, 0.0f, 0.0f);
	cardView = Ui_StackTakeFill(&foot);
	Gfx_Text(g, t->fSmall, d->status, cardView, t->text3, GFX_TEXT_VCENTER);
}

static void on_after_frame(UiApp *app, void *user)
{
	ScrollDemo *d = (ScrollDemo *)user;
	if (d->pendingToggle >= 0) {
		Card *c = &d->cards[d->pendingToggle];
		c->expanded = !c->expanded;
		c->rev++;
		d->pendingToggle = -1;
		UiApp_Invalidate(app);
	}
}

static int on_animating(UiApp *app, void *user)
{
	ScrollDemo *d = (ScrollDemo *)user;
	(void)app;
	return UiScrollView_Busy(&d->fixedSv) || UiScrollView_Busy(&d->varSv);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	ScrollDemo *d = (ScrollDemo *)user;
	UiScrollView_Tick(&d->fixedSv, &app->ui, (float)dtMs);
	UiScrollView_Tick(&d->varSv, &app->ui, (float)dtMs);
}

static void on_init(UiApp *app, void *user)
{
	ScrollDemo *d = (ScrollDemo *)user;
	UiScrollConfig desktop = UiScrollConfig_Desktop();
	UiScrollConfig touch = UiScrollConfig_Touch();
	UiScrollbarStyle bar = UiScrollbarStyle_Default();
	UiScrollPadding pad;
	int i;

	d->app = app;
	d->pendingToggle = -1;
	app->minW = 760.0f;
	app->minH = 560.0f;

	bar.reserveSpace = 1;   /* 内容右侧留出滚动条槽, thumb 不压控件 */
	pad.left = 4.0f; pad.top = 4.0f; pad.right = 6.0f; pad.bottom = 6.0f;

	UiScrollView_Init(&d->fixedSv, Ui_Id("scroll.fixed", 0, 0), &desktop, &bar);
	UiScrollView_Init(&d->varSv, Ui_Id("scroll.var", 0, 0), &touch, &bar);
	UiScrollView_SetPadding(&d->fixedSv, pad);
	UiScrollView_SetPadding(&d->varSv, pad);

	UiScrollLayout_Init(&d->fixedLayout, NULL, 0);
	UiScrollLayout_SetPadding(&d->fixedLayout, 6.0f, 8.0f);
	UiScrollLayout_SetFixed(&d->fixedLayout, FIXED_COUNT, ROW_H);
	UiScrollLayout_Commit(&d->fixedLayout);

	UiScrollLayout_Init(&d->varLayout, d->varItems, CARD_COUNT);
	UiScrollLayout_SetPadding(&d->varLayout, 6.0f, 8.0f);
	UiScrollLayout_SetGap(&d->varLayout, 10.0f);
	UiScrollLayout_SetCount(&d->varLayout, CARD_COUNT);
	for (i = 0; i < CARD_COUNT; i++) {
		d->cards[i].rev = (uint32_t)(i + 1);
		d->cards[i].expanded = (i % 7 == 3);
		UiScrollLayout_SetItemFlags(&d->varLayout, i,
			UI_SCROLL_ITEM_WIDTH_DEPENDENT);
	}
	UiScrollLayout_Commit(&d->varLayout);
	wsprintfA(d->status, "滚轮/拖动滚动条；点击右侧卡片展开/收起。");
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
	return UiApp_Run(L"CUI ScrollView", 1080, 720, UI_THEME_LIGHT, &cb, &g_demo);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
