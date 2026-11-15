/*
 * grid.c - Grid + ScrollView 仪表盘样例
 *
 * 演示:
 *   - 3 列: fixed 112 / auto(min 150 max 260) / 1fr(min 200)
 *   - 跨 3 列段头, rowSpan 2 高卡, colSpan 2 宽卡, 20 张卡片产生隐式行
 *   - dense 回填空洞 / sparse 行优先保留空洞
 *   - ScrollView 承载网格: 内容高度由 Ui_GridMeasure 得到, 滚轮/拖动滚动条
 *   - 点击卡片展开详情, 高度变化后 contentH 与滚动范围立即更新
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/ui_scroll.h"
#include "ui/ui_widgets.h"

#define CARD_COUNT 20
#define ITEM_COUNT (CARD_COUNT + 1)

enum {
	ACC_BLUE, ACC_GREEN, ACC_GOLD, ACC_ORANGE,
	ACC_BLUE2, ACC_RED, ACC_WARN, ACC_OK, ACC_COUNT
};

enum {
	CARD_CELL, CARD_WIDE, CARD_TALL
};

typedef struct GridCard {
	const char *title;
	const char *sub;
	const char *tag;
	int height;
	int minW;
	int accent;
	int kind;
	int expanded;
	Gfx *g;
	GfxFont *font;
} GridCard;

typedef struct GridDemo {
	UiApp *app;
	UiScrollView sv;
	GridCard cards[CARD_COUNT];
	UiFixedSize headerSize;
	int dense;
	int gap;
	int pendingToggle;
	int drawn;
	int contentH;
	int viewportH;
	char status[224];
} GridDemo;

static GridDemo g_grid;

static const char *const kCells[] = {
	"指标", "会话", "延迟", "队列", "命中", "下载",
	"内存", "线程", "错误", "吞吐", "帧率", "磁盘",
	"连接", "重试", "积压", "同步"
};
static const char *const kCellSubs[] = {
	"+12.4% 环比", "1,284 在线", "42 ms P95", "36 个等待",
	"93.2% 命中", "28.6 MB/s", "418 MB", "24 活跃",
	"0.31% 错误", "1.8k req/s", "59.8 fps", "12.4 MB/s",
	"18 / 64", "7 次/分", "3 个任务", "86 ms"
};
static const char *const kTags[] = {
	"CELL", "AUTO", "FR", "CELL"
};

static UiColor accent_color(UiTheme *t, int index)
{
	switch (index % ACC_COUNT) {
	case ACC_GREEN: return t->green;
	case ACC_GOLD: return t->gold;
	case ACC_ORANGE: return t->orange;
	case ACC_BLUE2: return t->blue2;
	case ACC_RED: return t->red;
	case ACC_WARN: return t->warning.base;
	case ACC_OK: return t->success.base;
	default: return t->blue;
	}
}

static void card_measure(void *user, UiMeasure *m)
{
	GridCard *c = (GridCard *)user;
	float w = 0.0f;
	if (c->g && c->font) {
		w = Gfx_TextWidth(c->g, c->font, c->title);
		if (c->sub) {
			float sw = Gfx_TextWidth(c->g, c->font, c->sub);
			if (sw > w) w = sw;
		}
	}
	m->minW = (float)c->minW;
	m->prefW = w + 34.0f;
	if (m->prefW < m->minW) m->prefW = m->minW;
	m->minH = m->prefH = (float)c->height +
		(c->expanded ? (c->kind == CARD_TALL ? 96.0f : 42.0f) : 0.0f);
}

static UiGridItem grid_item_(void *user, UiMeasureFn fn,
	int row, int col, int rowSpan, int colSpan, int stretch, int align)
{
	UiGridItem it;
	it.user = user;
	it.measure = fn;
	it.row = row;
	it.col = col;
	it.rowSpan = rowSpan;
	it.colSpan = colSpan;
	it.stretch = stretch;
	it.align = align;
	return it;
}

static int button_(UiContext *ui, UiId id, UiRect rc, const char *label,
	const UiButtonStyle *style)
{
	return Ui_ButtonEx(ui, id, rc, label, style, 1);
}

static void draw_header(GridDemo *d, UiContext *ui, Gfx *g)
{
	UiTheme *t = &d->app->theme;
	UiButtonStyle primary = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle def = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
	float total = 96.0f + 88.0f + 72.0f + 72.0f + 24.0f;
	float x = d->app->w - 24.0f - total;
	float y = 52.0f, h = 32.0f;

	Gfx_Text(g, t->fBig, "CUI Grid · ScrollView",
		Ui_Rect(24.0f, 14.0f, x - 48.0f, 32.0f), t->text,
		GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall,
		"fixed · auto(minmax) · fr · rowSpan / colSpan · 隐式行 · dense",
		Ui_Rect(26.0f, 53.0f, x - 52.0f, 18.0f), t->text3,
		GFX_TEXT_VCENTER);

	if (button_(ui, Ui_Id("grid.dense", 0, 0), Ui_Rect(x, y, 96.0f, h),
		d->dense ? "Dense 开" : "Sparse 开", &primary))
		d->dense = !d->dense;
	x += 104.0f;
	if (button_(ui, Ui_Id("grid.gap", 0, 0), Ui_Rect(x, y, 88.0f, h),
		d->gap > 8 ? "间距 12" : "间距 6", &def))
		d->gap = d->gap > 8 ? 6 : 12;
	x += 96.0f;
	if (button_(ui, Ui_Id("grid.theme", 0, 0), Ui_Rect(x, y, 72.0f, h),
		"主题", &def)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
	}
	x += 80.0f;
	if (button_(ui, Ui_Id("grid.top", 0, 0), Ui_Rect(x, y, 72.0f, h),
		"回顶", &plain))
		UiScrollView_ScrollTo(&d->sv, ui, 0.0f, 1);
}

static void draw_section_header(Gfx *g, UiTheme *t, UiRect rc)
{
	UiColor line = Ui_ColorAlpha(t->primary.base, 44);
	Gfx_FillRoundRect(g, rc, 10.0f, Ui_ColorAlpha(t->primary.base, 14));
	Gfx_StrokeRoundRect(g, rc, 10.0f, line, 1.0f);
	Gfx_Text(g, t->fLabel, "仪表盘 · 20 张卡片",
		Ui_Rect(rc.x + 18.0f, rc.y, rc.w * 0.5f, rc.h), t->text,
		GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "行优先自动放置 · 滚动查看 · 点击卡片展开",
		Ui_Rect(rc.x + rc.w * 0.5f, rc.y, rc.w * 0.5f - 18.0f, rc.h),
		t->text2, GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
}

static void draw_card(GridDemo *d, UiContext *ui, Gfx *g, UiTheme *t,
	GridCard *c, UiRect rc, int index)
{
	UiId id = Ui_IdIndex("grid.card", 0, 0, index);
	UiWidgetState st = Ui_State(ui, id, rc, 1);
	UiColor accent = accent_color(t, c->accent);
	UiColor bg = Ui_ColorMix(t->card, accent, st.hovered ? 0.14f : 0.06f);
	UiColor border = st.hovered ? accent : t->neutral.border;
	float textX = rc.x + 26.0f;
	float textW = rc.w - 38.0f;

	Gfx_FillRoundRect(g, rc, 10.0f, bg);
	Gfx_StrokeRoundRect(g, rc, 10.0f,
		Ui_ColorAlpha(border, st.hovered ? 210 : 255), 1.0f);
	Gfx_FillRoundRect(g, Ui_Rect(rc.x + 12.0f, rc.y + 12.0f, 4.0f,
		rc.h - 24.0f), 2.0f, accent);

	if (rc.w > 150.0f && c->tag) {
		float tw = Gfx_TextWidth(g, t->fMini, c->tag) + 12.0f;
		UiRect tag = Ui_Rect(rc.x + rc.w - 12.0f - tw, rc.y + 8.0f,
			tw, 18.0f);
		Gfx_FillRoundRect(g, tag, 9.0f, Ui_ColorAlpha(accent, 34));
		Gfx_Text(g, t->fMini, c->tag, tag, accent,
			GFX_TEXT_VCENTER | GFX_TEXT_CENTER);
		textW = rc.w - 38.0f - tw - 8.0f;
	}

	Gfx_Text(g, t->fLabel, c->title,
		Ui_Rect(textX, rc.y + 7.0f, textW, 20.0f), t->text,
		GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, c->sub,
		Ui_Rect(textX, rc.y + 29.0f, rc.w - 38.0f, 16.0f), t->text2,
		GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fMini, c->expanded ? "已展开" : "点击展开详情",
		Ui_Rect(textX, rc.y + 49.0f, rc.w - 38.0f, 14.0f),
		c->expanded ? accent : t->text3, GFX_TEXT_VCENTER);
	if (c->expanded) {
		Gfx_Text(g, t->fMini, "高度已更新",
			Ui_Rect(textX, rc.y + 65.0f, rc.w - 38.0f, 14.0f),
			t->text3, GFX_TEXT_VCENTER);
	}
	if (c->kind == CARD_TALL) {
		float areaTop = rc.y + 98.0f;
		float areaBottom = rc.y + rc.h - 18.0f;
		float maxH = areaBottom - areaTop;
		float gap = 6.0f;
		float bw = (rc.w - 38.0f - gap * 7.0f) / 8.0f;
		int k;
		Gfx_Text(g, t->fMini, "两行轨道合并 · 条形随内容区伸展",
			Ui_Rect(textX, rc.y + 82.0f, rc.w - 38.0f, 14.0f),
			t->text3, GFX_TEXT_VCENTER);
		for (k = 0; k < 8 && maxH > 8.0f; k++) {
			float pct = (float)((k * 29 + index * 17) % 70 + 30) / 100.0f;
			UiRect bar = Ui_Rect(textX + (float)k * (bw + gap),
				areaBottom - maxH * pct, bw, maxH * pct);
			Gfx_FillRoundRect(g, bar, 4.0f,
				Ui_ColorAlpha(accent, 54 + (k % 3) * 26));
		}
	} else if (c->kind == CARD_WIDE) {
		float pct = (float)((index * 37 + 31) % 58 + 34) / 100.0f;
		UiRect track = Ui_Rect(textX, rc.y + rc.h - 16.0f,
			rc.w - 38.0f, 4.0f);
		Gfx_FillRoundRect(g, track, 2.0f, Ui_ColorAlpha(accent, 26));
		Gfx_FillRoundRect(g, Ui_Rect(track.x, track.y,
			track.w * pct, track.h), 2.0f, Ui_ColorAlpha(accent, 150));
	}
	if (ui->clicked == id) d->pendingToggle = index;
}

static void on_frame(UiApp *app, void *user)
{
	GridDemo *d = (GridDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiGridTrack cols[3], rows[1];
	UiGridItem items[ITEM_COUNT];
	UiRect rc[ITEM_COUNT], panel, viewport, content, status;
	UiMeasure m;
	UiGrid grid;
	float innerW;
	int i, first = 0;

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h),
		t->windowRadius, t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f,
		app->h - 1.0f), t->windowRadius - 0.5f, t->frame, 1.0f);
	draw_header(d, ui, g);

	panel = Ui_Rect(20.0f, 104.0f, app->w - 40.0f, app->h - 148.0f);
	Ui_Panel(ui, panel);
	viewport = Ui_Rect(panel.x + 14.0f, panel.y + 14.0f,
		panel.w - 28.0f, panel.h - 28.0f);
	innerW = UiScrollView_ContentWidth(&d->sv, viewport);
	d->viewportH = (int)viewport.h;
	if (innerW <= 0.0f || viewport.h <= 0.0f) return;

	cols[0].kind = UI_GRID_FIXED;
	cols[0].value = 112.0f;
	cols[0].minSize = 0.0f;
	cols[0].maxSize = 0.0f;
	cols[1].kind = UI_GRID_AUTO;
	cols[1].value = 0.0f;
	cols[1].minSize = 150.0f;
	cols[1].maxSize = 260.0f;
	cols[2].kind = UI_GRID_FR;
	cols[2].value = 1.0f;
	cols[2].minSize = 200.0f;
	cols[2].maxSize = 0.0f;
	rows[0].kind = UI_GRID_FIXED;
	rows[0].value = 54.0f;
	rows[0].minSize = 0.0f;
	rows[0].maxSize = 0.0f;

	for (i = 0; i < CARD_COUNT; i++) {
		d->cards[i].g = g;
		d->cards[i].font = t->fLabel;
	}
	d->headerSize.w = 0.0f;
	d->headerSize.h = 54.0f;
	items[0] = grid_item_(&d->headerSize, Ui_MeasureFixed,
		0, 0, 1, 3, 1, UI_ALIGN_START);
	items[1] = grid_item_(&d->cards[0], card_measure, 1, 2, 2, 1, 1,
		UI_ALIGN_START);
	items[2] = grid_item_(&d->cards[1], card_measure, 1, 1, 1, 1, 1,
		UI_ALIGN_START);
	items[3] = grid_item_(&d->cards[2], card_measure, 2, 0, 1, 2, 1,
		UI_ALIGN_START);
	items[4] = grid_item_(&d->cards[3], card_measure, -1, -1, 1, 2, 1,
		UI_ALIGN_START);
	for (i = 4; i < CARD_COUNT; i++)
		items[i + 1] = grid_item_(&d->cards[i], card_measure,
			-1, -1, 1, 1, 1, UI_ALIGN_START);

	grid.rows = 1;
	grid.cols = 3;
	grid.rowGap = (float)d->gap;
	grid.colGap = (float)d->gap;
	grid.dense = d->dense;

	m.maxW = innerW;
	Ui_GridMeasure(&grid, cols, rows, items, ITEM_COUNT, &m);
	d->contentH = (int)m.prefH;

	UiScrollView_Begin(&d->sv, ui, viewport, innerW, m.prefH);
	content = UiScrollView_ContentRect(&d->sv);
	if (Ui_GridLayout(Ui_Rect(content.x, content.y, innerW, m.prefH),
		&grid, cols, rows, items, ITEM_COUNT, rc)) {
		if (!UiScrollView_Cull(&d->sv, rc[0]))
			draw_section_header(g, t, rc[0]);
		for (i = 0; i < CARD_COUNT; i++) {
			if (UiScrollView_Cull(&d->sv, rc[i + 1])) continue;
			draw_card(d, ui, g, t, &d->cards[i], rc[i + 1], i);
			first++;
		}
	}
	UiScrollView_End(&d->sv, ui);
	d->drawn = first;

	wsprintfA(d->status,
		"%s · 间距 %d · 内容 %d / 视口 %d DIP · 滚动 %d/%d · 可见 %d/%d · 点击卡片展开",
		d->dense ? "Dense" : "Sparse", d->gap, d->contentH, d->viewportH,
		(int)d->sv.offset, (int)UiScrollView_MaxOffset(&d->sv),
		d->drawn, CARD_COUNT);
	status = Ui_Rect(24.0f, app->h - 34.0f, app->w - 48.0f, 18.0f);
	Gfx_Text(g, t->fSmall, d->status, status, t->text3,
		GFX_TEXT_VCENTER);
}

static void on_after_frame(UiApp *app, void *user)
{
	GridDemo *d = (GridDemo *)user;
	if (d->pendingToggle >= 0) {
		GridCard *c = &d->cards[d->pendingToggle];
		c->expanded = !c->expanded;
		d->pendingToggle = -1;
		UiApp_Invalidate(app);
	}
}

static int on_animating(UiApp *app, void *user)
{
	GridDemo *d = (GridDemo *)user;
	(void)app;
	return UiScrollView_Busy(&d->sv);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	GridDemo *d = (GridDemo *)user;
	UiScrollView_Tick(&d->sv, &app->ui, (float)dtMs);
}

static void on_init(UiApp *app, void *user)
{
	GridDemo *d = (GridDemo *)user;
	UiScrollConfig cfg = UiScrollConfig_Desktop();
	UiScrollbarStyle bar = UiScrollbarStyle_Default();
	UiScrollPadding pad;
	int i;

	d->app = app;
	d->dense = 1;
	d->gap = 12;
	d->pendingToggle = -1;
	app->minW = 820.0f;
	app->minH = 560.0f;

	bar.reserveSpace = 1;
	pad.left = 2.0f;
	pad.top = 4.0f;
	pad.right = 8.0f;
	pad.bottom = 12.0f;
	UiScrollView_Init(&d->sv, Ui_Id("grid.scroll", 0, 0), &cfg, &bar);
	UiScrollView_SetPadding(&d->sv, pad);

	d->cards[0].title = "高卡片";
	d->cards[0].sub = "rowSpan = 2 · 跨两行";
	d->cards[0].tag = "ROW ×2";
	d->cards[0].height = 170;
	d->cards[0].minW = 120;
	d->cards[0].accent = ACC_BLUE;
	d->cards[0].kind = CARD_TALL;
	d->cards[1].title = "AUTO 轨道";
	d->cards[1].sub = "min 150 / max 260";
	d->cards[1].tag = "AUTO";
	d->cards[1].height = 90;
	d->cards[1].minW = 120;
	d->cards[1].accent = ACC_GREEN;
	d->cards[1].kind = CARD_CELL;
	d->cards[2].title = "跨 2 列";
	d->cards[2].sub = "colSpan = 2 · 固定高度";
	d->cards[2].tag = "COL ×2";
	d->cards[2].height = 112;
	d->cards[2].minW = 220;
	d->cards[2].accent = ACC_GOLD;
	d->cards[2].kind = CARD_WIDE;
	d->cards[3].title = "自动跨 2 列";
	d->cards[3].sub = "sparse 留洞 / dense 回填";
	d->cards[3].tag = "COL ×2";
	d->cards[3].height = 100;
	d->cards[3].minW = 200;
	d->cards[3].accent = ACC_ORANGE;
	d->cards[3].kind = CARD_WIDE;

	for (i = 4; i < CARD_COUNT; i++) {
		int k = i - 4;
		d->cards[i].title = kCells[k];
		d->cards[i].sub = kCellSubs[k];
		d->cards[i].tag = kTags[i % 4];
		d->cards[i].height = (i % 5 == 0) ? 92 : 86;
		d->cards[i].minW = 110;
		d->cards[i].accent = i % ACC_COUNT;
	}
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
	return UiApp_Run(L"CUI Grid", 1000, 760, UI_THEME_LIGHT, &cb, &g_grid);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
