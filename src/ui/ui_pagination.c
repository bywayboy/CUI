/*
 * ui_pagination.c - 行内分页器
 *
 * 页码窗口算法见 docs/CUI-分页-设计方案.md。所有尺寸都是 DIP。
 */
#include "ui/ui_pagination.h"
#include "ui/ui_icon.h"

#define PAG_MIN_SLOTS 5
#define PAG_MAX_SLOTS 9
#define PAG_ELLIPSIS_W 16.0f
#define PAG_COMPACT_PAD 12.0f
#define PAG_ICON_SIZE 12.0f
#define PAG_ICON_STEP 3.5f

static int pag_page_count(const UiPagination *p)
{
	int total, size;
	if (!p) return 0;
	total = p->total;
	size = p->pageSize;
	if (total <= 0 || size <= 0) return 0;
	/* 用除法+取余, 避免 total+size-1 在接近 INT_MAX 时溢出. */
	return total / size + (total % size != 0);
}

static void pag_style_resolve(const UiPaginationStyle *in, const UiTheme *t,
	UiPaginationStyle *out)
{
	ZeroMemory(out, sizeof(*out));
	if (in) *out = *in;
	if (out->itemW <= 0.0f) out->itemW = 30.0f;
	if (out->itemH <= 0.0f) out->itemH = 32.0f;
	if (out->gap <= 0.0f) out->gap = 2.0f;
	if (out->padX <= 0.0f) out->padX = 5.0f;
	if (out->radius <= 0.0f)
		out->radius = t ? t->buttonRadius : 7.0f;
	if (out->maxSlots != 0) {
		if (out->maxSlots < PAG_MIN_SLOTS) out->maxSlots = PAG_MIN_SLOTS;
		if (out->maxSlots > PAG_MAX_SLOTS) out->maxSlots = PAG_MAX_SLOTS;
	}
	if (!out->prevText) out->prevText = "\xE2\x80\xB9";
	if (!out->nextText) out->nextText = "\xE2\x80\xBA";
}

static UiId pag_id(const char *scope, UiId base)
{
	return Ui_Id(scope, (int)(uint32_t)(base >> 32),
		(int)(uint32_t)base);
}

static UiId pag_page_id(UiId base, int page)
{
	return Ui_IdIndex("ui.pag.page", (int)(uint32_t)(base >> 32),
		(int)(uint32_t)base, page);
}

/* 统一格宽: 保证最大页码 (显示值 pageCount) 不被截断. */
static float pag_cell_w(const UiPaginationStyle *st, UiContext *ui, int n)
{
	char buf[24];
	float tw;
	wsprintfA(buf, "%d", n > 0 ? n : 1);
	tw = Gfx_TextWidth(ui->gfx, ui->theme->fBody, buf) + 2.0f * st->padX;
	return tw > st->itemW ? tw : st->itemW;
}

/* 保持色相, 只按交互强度缩放 alpha. */
static UiColor pag_fade(UiColor c, float k)
{
	int a = (int)((float)((c >> 24) & 0xFF) * k + 0.5f);
	return Ui_ColorAlpha(c, a);
}

/* 格子底色/交互态; 返回前景色. */
static UiColor pag_cell_bg(UiContext *ui, const UiTheme *t,
	const UiPaginationStyle *st, UiId id, UiRect rc, int current, int enabled)
{
	UiWidgetState ws;
	UiColor fg;

	if (current) {
		Ui_State(ui, id, rc, 1);
		Gfx_FillRoundRect(ui->gfx, rc, st->radius, t->primary.base);
		fg = t->primary.text;
	} else if (!enabled) {
		Ui_State(ui, id, rc, 0);
		fg = t->btnDisabledText;
	} else {
		ws = Ui_State(ui, id, rc, 1);
		/* 与行内文字按钮共用 neutral.hover/press: 亮色下仍能一眼看出悬停. */
		if (ws.hover > 0.002f)
			Gfx_FillRoundRect(ui->gfx, rc, st->radius,
				pag_fade(t->neutral.hover, ws.hover));
		if (ws.press > 0.002f)
			Gfx_FillRoundRect(ui->gfx, rc, st->radius,
				pag_fade(t->neutral.press, ws.press));
		fg = t->text;
	}
	return fg;
}

/* 画一个文字格; 返回 1 = 本帧被点击. */
static int pag_cell(UiContext *ui, const UiTheme *t,
	const UiPaginationStyle *st, UiId id, UiRect rc, const char *label,
	int current, int enabled)
{
	UiColor fg = pag_cell_bg(ui, t, st, id, rc, current, enabled);
	Gfx_Text(ui->gfx, t->fBody, label, rc, fg,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	return ui->clicked == id;
}

/* 画一个方向格: 有图标字体用 chevron (count=2 为「«」双箭头), 否则回退文字. */
static int pag_arrow(UiContext *ui, const UiTheme *t,
	const UiPaginationStyle *st, UiId id, UiRect rc, int dir, int count,
	const char *text, int enabled, int icons)
{
	GfxFont *iconFont = t->fIconS ? t->fIconS : t->fIcon;
	UiColor fg = pag_cell_bg(ui, t, st, id, rc, 0, enabled);
	if (icons && iconFont) {
		UiIcon ic = UiIcon_Glyph(iconFont,
			dir < 0 ? 0xE76Bu : 0xE76Cu);
		float isz = t->fIconS ? PAG_ICON_SIZE : 16.0f;
		float step = t->fIconS ? PAG_ICON_STEP : 4.5f;
		float cx = rc.x + rc.w * 0.5f;
		float cy = rc.y + rc.h * 0.5f;
		int k;
		for (k = 0; k < count; k++) {
			float ox = count > 1
				? (k == 0 ? -step : step) : 0.0f;
			UiIcon_Draw(ui, &ic,
				Ui_Rect(cx + ox - isz * 0.5f, cy - isz * 0.5f,
					isz, isz), fg, 1);
		}
	} else {
		Gfx_Text(ui->gfx, t->fBody, text, rc, fg,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}
	return ui->clicked == id;
}

static void pag_ellipsis(UiContext *ui, const UiTheme *t, UiRect rc)
{
	Gfx_Text(ui->gfx, t->fBody, "\xE2\x80\xA6", rc, t->text3,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
}

static void pag_invalidate(UiPagination *p, UiContext *ui)
{
	if (!p || !ui) return;
	if (p->rc.w > 0.0f && p->rc.h > 0.0f)
		Ui_InvalidateRect(ui, p->rc);
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

void UiPagination_Init(UiPagination *p, UiId id, int total, int pageSize)
{
	if (!p) return;
	ZeroMemory(p, sizeof(*p));
	p->id = id;
	p->total = total > 0 ? total : 0;
	p->pageSize = pageSize > 0 ? pageSize : 1;
}

int UiPagination_PageCount(const UiPagination *p)
{
	return pag_page_count(p);
}

int UiPagination_Offset(const UiPagination *p)
{
	return p ? p->page * p->pageSize : 0;
}

int UiPagination_Slots(int page, int pageCount, int maxSlots, int *out)
{
	int edge, win, lo, i, n = 0;

	if (!out || pageCount <= 0) return 0;
	if (page < 0) page = 0;
	if (page >= pageCount) page = pageCount - 1;
	if (maxSlots < PAG_MIN_SLOTS) maxSlots = PAG_MIN_SLOTS;
	if (maxSlots > PAG_MAX_SLOTS) maxSlots = PAG_MAX_SLOTS;
	if (pageCount <= maxSlots) {
		for (i = 0; i < pageCount; i++) out[n++] = i;
		return n;
	}
	edge = maxSlots - 2;
	win = maxSlots - 4;
	if (page <= edge - 1) {                 /* 贴左 */
		for (i = 0; i < edge; i++) out[n++] = i;
		out[n++] = -1;
		out[n++] = pageCount - 1;
	} else if (page >= pageCount - edge) {  /* 贴右 */
		out[n++] = 0;
		out[n++] = -1;
		for (i = pageCount - edge; i < pageCount; i++) out[n++] = i;
	} else {                                /* 中间窗口 */
		lo = page - win / 2;
		out[n++] = 0;
		out[n++] = -1;
		for (i = 0; i < win; i++) out[n++] = lo + i;
		out[n++] = -1;
		out[n++] = pageCount - 1;
	}
	return n;
}

void UiPagination_SetPage(UiPagination *p, UiContext *ui, int page)
{
	int n;
	if (!p) return;
	n = pag_page_count(p);
	if (n <= 0) page = 0;
	else if (page < 0) page = 0;
	else if (page > n - 1) page = n - 1;
	if (page == p->page) return;
	p->page = page;
	p->changed = 1;
	pag_invalidate(p, ui);
}

void UiPagination_SetTotal(UiPagination *p, UiContext *ui, int total)
{
	int n;
	if (!p) return;
	if (total < 0) total = 0;
	if (total == p->total) return;
	p->total = total;
	n = pag_page_count(p);
	if (n <= 0) {
		if (p->page != 0) {
			p->page = 0;
			p->changed = 1;
		}
	} else if (p->page > n - 1) {
		p->page = n - 1;
		p->changed = 1;
	}
	pag_invalidate(p, ui);
}

void UiPagination_SetPageSize(UiPagination *p, UiContext *ui, int pageSize)
{
	int n, off, np;
	if (!p) return;
	if (pageSize < 1) pageSize = 1;
	if (pageSize == p->pageSize) return;
	off = p->page * p->pageSize;   /* <= total, 不溢出 */
	p->pageSize = pageSize;
	n = pag_page_count(p);
	np = n > 0 ? off / pageSize : 0;
	if (n <= 0) np = 0;
	else if (np > n - 1) np = n - 1;
	if (np != p->page) {
		p->page = np;
		p->changed = 1;
	}
	pag_invalidate(p, ui);
}

int UiPagination_TakeChanged(UiPagination *p)
{
	int ch;
	if (!p) return 0;
	ch = p->changed;
	p->changed = 0;
	return ch;
}

float UiPagination_Measure(const UiPagination *p, UiContext *ui,
	const UiPaginationStyle *style, int compact)
{
	UiPaginationStyle st;
	UiTheme *t;
	int n, base, cells;
	float cellW, textW;
	char buf[40];

	if (!p || !ui || !ui->theme || !ui->gfx) return 0.0f;
	n = pag_page_count(p);
	if (n <= 0) return 0.0f;
	t = ui->theme;
	pag_style_resolve(style, t, &st);
	if (n == 1 && !st.showSingle) return 0.0f;
	cellW = pag_cell_w(&st, ui, n);
	if (compact) {
		wsprintfA(buf, "%d / %d", p->page + 1, n);
		textW = Gfx_TextWidth(ui->gfx, t->fBody, buf);
		return 2.0f * cellW + 2.0f * st.gap + textW + PAG_COMPACT_PAD;
	}
	base = 2 + (st.firstLast ? 2 : 0);
	cells = base + PAG_MIN_SLOTS;
	return (float)cells * cellW + (float)(cells - 1) * st.gap;
}

/* ── 绘制 ───────────────────────────────────────────────────────────── */

int UiPagination_Draw(UiPagination *p, UiContext *ui, UiRect rc,
	const UiPaginationStyle *style)
{
	UiTheme *t;
	UiPaginationStyle st;
	UiRect currRc, compactRc, prevRc, nextRc;
	float cellW, cellH, gap, y, x, textW, numW, compW, minW;
	int slots[PAG_MAX_SLOTS];
	int n, nslots, base, i, target = -1, hit = 0;
	int compactMode = 0, minimalMode = 0;
	int iconPrev, iconNext, iconEdge;
	char buf[24], compactText[40];

	if (!p || !ui || !ui->theme || !ui->gfx) return -1;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return -1;
	p->rc = rc;
	n = pag_page_count(p);
	if (n <= 0) return -1;
	t = ui->theme;
	pag_style_resolve(style, t, &st);
	if (n == 1 && !st.showSingle) return -1;

	/* 自定义 prevText/nextText 优先于图标箭头. */
	iconPrev = (t->fIconS || t->fIcon) && !(style && style->prevText);
	iconNext = (t->fIconS || t->fIcon) && !(style && style->nextText);
	iconEdge = t->fIconS != NULL || t->fIcon != NULL;

	cellW = pag_cell_w(&st, ui, n);
	cellH = st.itemH < rc.h ? st.itemH : rc.h;
	y = rc.y + (rc.h - cellH) * 0.5f;
	gap = st.gap;
	base = 2 + (st.firstLast ? 2 : 0);

	numW = (float)(base + PAG_MIN_SLOTS) * cellW +
		(float)(base + PAG_MIN_SLOTS - 1) * gap;
	wsprintfA(compactText, "%d / %d", p->page + 1, n);
	textW = Gfx_TextWidth(ui->gfx, t->fBody, compactText);
	compW = 2.0f * cellW + 2.0f * gap + textW + PAG_COMPACT_PAD;
	minW = 2.0f * cellW + gap;

	if (rc.w >= numW) {
		int cap = st.maxSlots ? st.maxSlots : PAG_MAX_SLOTS;
		int want = (int)((rc.w + gap) / (cellW + gap)) - base;
		if (want > cap) want = cap;
		if (want < PAG_MIN_SLOTS) want = PAG_MIN_SLOTS;
		nslots = UiPagination_Slots(p->page, n, want, slots);
	} else if (rc.w >= compW) {
		compactMode = 1;
		nslots = 0;
	} else if (rc.w >= minW) {
		minimalMode = 1;
		nslots = 0;
	} else {
		return -1;
	}

	currRc = Ui_Rect(0, 0, 0, 0);
	compactRc = currRc;
	prevRc = currRc;
	nextRc = currRc;

	Gfx_PushClip(ui->gfx, rc);
	Ui_PushClipRect(ui, rc);

	x = rc.x;
	if (compactMode) {
		UiId pid = pag_id("ui.pag.prev", p->id);
		UiId nid = pag_id("ui.pag.next", p->id);
		int cl;
		prevRc = Ui_Rect(x, y, cellW, cellH);
		cl = pag_arrow(ui, t, &st, pid, prevRc, -1, 1, st.prevText,
			p->page > 0, iconPrev);
		if (cl) { hit = 1; if (p->page > 0) target = p->page - 1; }
		x += cellW + gap;
		compactRc = Ui_Rect(x, y, textW + PAG_COMPACT_PAD, cellH);
		Gfx_Text(ui->gfx, t->fBody, compactText, compactRc, t->text2,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		x += compactRc.w + gap;
		nextRc = Ui_Rect(x, y, cellW, cellH);
		cl = pag_arrow(ui, t, &st, nid, nextRc, 1, 1, st.nextText,
			p->page < n - 1, iconNext);
		if (cl) { hit = 1; if (p->page < n - 1) target = p->page + 1; }
	} else {
		if (st.firstLast && !minimalMode) {
			UiId fid = pag_id("ui.pag.first", p->id);
			UiRect fr = Ui_Rect(x, y, cellW, cellH);
			int cl = pag_arrow(ui, t, &st, fid, fr, -1, 2,
				"\xC2\xAB", p->page > 0, iconEdge);
			if (cl) { hit = 1; if (p->page > 0) target = 0; }
			x += cellW + gap;
		}
		{
			UiId pid = pag_id("ui.pag.prev", p->id);
			int cl;
			prevRc = Ui_Rect(x, y, cellW, cellH);
			cl = pag_arrow(ui, t, &st, pid, prevRc, -1, 1, st.prevText,
				p->page > 0, iconPrev);
			if (cl) {
				hit = 1;
				if (p->page > 0) target = p->page - 1;
			}
			x += cellW + gap;
		}
		if (!minimalMode) {
			for (i = 0; i < nslots; i++) {
				if (slots[i] < 0) {
					UiRect er = Ui_Rect(x, y, PAG_ELLIPSIS_W,
						cellH);
					pag_ellipsis(ui, t, er);
					x += PAG_ELLIPSIS_W + gap;
					continue;
				}
				{
					UiRect pr = Ui_Rect(x, y, cellW, cellH);
					int cur = slots[i] == p->page;
					int cl;
					wsprintfA(buf, "%d", slots[i] + 1);
					cl = pag_cell(ui, t, &st,
						pag_page_id(p->id, slots[i]),
						pr, buf, cur, 1);
					if (cur) currRc = pr;
					if (cl) {
						hit = 1;
						if (!cur) target = slots[i];
					}
					x += cellW + gap;
				}
			}
		}
		{
			UiId nid = pag_id("ui.pag.next", p->id);
			int cl;
			nextRc = Ui_Rect(x, y, cellW, cellH);
			cl = pag_arrow(ui, t, &st, nid, nextRc, 1, 1, st.nextText,
				p->page < n - 1, iconNext);
			if (cl) {
				hit = 1;
				if (p->page < n - 1) target = p->page + 1;
			}
			x += cellW + gap;
		}
		if (st.firstLast && !minimalMode) {
			UiId lid = pag_id("ui.pag.last", p->id);
			UiRect lr = Ui_Rect(x, y, cellW, cellH);
			int cl = pag_arrow(ui, t, &st, lid, lr, 1, 2,
				"\xC2\xBB", p->page < n - 1, iconEdge);
			if (cl) {
				hit = 1;
				if (p->page < n - 1) target = n - 1;
			}
		}
	}

	if (p->focusVisible) {
		UiColor ring = Ui_ColorAlpha(t->primary.base, 150);
		UiRect fr = currRc;
		if (compactMode) fr = compactRc;
		else if (minimalMode)
			fr = Ui_Rect(prevRc.x, prevRc.y,
				nextRc.x + nextRc.w - prevRc.x, prevRc.h);
		if (fr.w > 0.0f && fr.h > 0.0f)
			Gfx_StrokeRoundRect(ui->gfx, fr, st.radius, ring, 1.0f);
	}

	Ui_PopClipRect(ui);
	Gfx_PopClip(ui->gfx);

	if (hit) {
		ui->navId = p->id;
		p->focusVisible = 0;
	}
	if (target >= 0 && target != p->page) {
		p->page = target;
		p->changed = 1;
		Ui_InvalidateRect(ui, rc);
		return target;
	}
	return -1;
}

/* ── 键盘 ───────────────────────────────────────────────────────────── */

int UiPagination_Key(UiPagination *p, UiContext *ui, UINT vk)
{
	int n, np;
	if (!p || !ui || ui->navId != p->id) return 0;
	n = pag_page_count(p);
	if (n <= 0) return 0;
	np = p->page;
	switch (vk) {
	case VK_LEFT:
		np--;
		break;
	case VK_RIGHT:
		np++;
		break;
	case VK_HOME:
		np = 0;
		break;
	case VK_END:
		np = n - 1;
		break;
	case VK_PRIOR:
		np -= 5;
		break;
	case VK_NEXT:
		np += 5;
		break;
	default:
		return 0;
	}
	if (np < 0) np = 0;
	if (np > n - 1) np = n - 1;
	p->focusVisible = 1;
	if (np != p->page) {
		p->page = np;
		p->changed = 1;
	}
	pag_invalidate(p, ui);
	return 1;
}
