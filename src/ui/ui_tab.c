#include "ui/ui_tab.h"

static UINT tab_bit(int i)
{
	return i >= 0 && i < 32 ? (1u << i) : 0u;
}

static UiId tab_child_id(UiId id, int index)
{
	return Ui_IdIndex("ui.tab", (int)(uint32_t)(id >> 32),
		(int)(uint32_t)id, index);
}

static void tab_invalidate(UiContext *ui, const UiRect *rc)
{
	RECT r;
	float s;
	if (!ui || !ui->hwnd || !rc || rc->w <= 0.0f || rc->h <= 0.0f) return;
	s = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((rc->x - 2.0f) * s);
	r.top = (LONG)((rc->y - 2.0f) * s);
	r.right = (LONG)((rc->x + rc->w + 2.0f) * s) + 1;
	r.bottom = (LONG)((rc->y + rc->h + 2.0f) * s) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

UiTabStyle UiTabStyle_Line(void)
{
	UiTabStyle s;
	ZeroMemory(&s, sizeof(s));
	s.variant = UI_TAB_LINE;
	s.height = 38.0f;
	s.padX = 14.0f;
	s.gap = 4.0f;
	s.indicatorW = 2.0f;
	s.stretch = 0;
	s.showDivider = 1;
	return s;
}

UiTabStyle UiTabStyle_Pill(void)
{
	UiTabStyle s;
	ZeroMemory(&s, sizeof(s));
	s.variant = UI_TAB_PILL;
	s.height = 34.0f;
	s.padX = 16.0f;
	s.gap = 2.0f;
	s.radius = 8.0f;
	s.stretch = 1;
	return s;
}

UiTabStyle UiTabStyle_Card(void)
{
	UiTabStyle s;
	ZeroMemory(&s, sizeof(s));
	s.variant = UI_TAB_CARD;
	s.height = 36.0f;
	s.padX = 14.0f;
	s.gap = 2.0f;
	s.radius = 8.0f;
	s.stretch = 0;
	s.mergeContent = 1;
	return s;
}

void UiTabBar_Init(UiTabBar *tb, UiId id, int count)
{
	if (!tb) return;
	ZeroMemory(tb, sizeof(*tb));
	tb->id = id;
	tb->count = count > 0 ? count : 0;
	tb->selected = tb->count > 0 ? 0 : -1;
	tb->focus = tb->selected;
}

void UiTabBar_SetCount(UiTabBar *tb, int count)
{
	if (!tb) return;
	if (count < 0) count = 0;
	tb->count = count;
	if (tb->selected >= count) tb->selected = count - 1;
	if (tb->focus >= count) tb->focus = count - 1;
	if (tb->selected < 0 && count > 0) tb->selected = 0;
	if (tb->focus < 0 && count > 0) tb->focus = tb->selected >= 0 ? tb->selected : 0;
	if (count < 32) tb->disabledMask &= (1u << count) - 1u;
	if (count == 0) tb->disabledMask = 0;
	tb->indicatorInit = 0;
}

void UiTabBar_SetSelected(UiTabBar *tb, int index)
{
	if (!tb) return;
	if (index < 0) index = -1;
	if (index >= tb->count) index = tb->count - 1;
	tb->selected = index;
	if (index >= 0) tb->focus = index;
}

void UiTabBar_SetDisabledMask(UiTabBar *tb, unsigned mask)
{
	if (!tb) return;
	if (tb->count < 32) mask &= (1u << tb->count) - 1u;
	tb->disabledMask = mask;
}

void UiTabBar_SetFocusVisible(UiTabBar *tb, int visible)
{
	if (tb) tb->focusVisible = visible ? 1 : 0;
}

static int tab_item_enabled(const UiTabBar *tb, int i, int enabled)
{
	if (!enabled || i < 0 || i >= tb->count) return 0;
	return (tb->disabledMask & tab_bit(i)) == 0;
}

static int tab_find_next(const UiTabBar *tb, int from, int dir, int enabled)
{
	int i, n = tb->count;
	if (n <= 0) return -1;
	for (i = 1; i <= n; i++) {
		int idx = (from + dir * i) % n;
		if (idx < 0) idx += n;
		if (tab_item_enabled(tb, idx, enabled)) return idx;
	}
	return -1;
}

static int tab_find_first(const UiTabBar *tb, int enabled)
{
	int i;
	for (i = 0; tb && i < tb->count; i++)
		if (tab_item_enabled(tb, i, enabled)) return i;
	return -1;
}

static int tab_find_last(const UiTabBar *tb, int enabled)
{
	int i;
	for (i = tb ? tb->count - 1 : -1; i >= 0; i--)
		if (tab_item_enabled(tb, i, enabled)) return i;
	return -1;
}

static void tab_activate(UiTabBar *tb, int index)
{
	if (!tb || index < 0 || index >= tb->count) return;
	tb->focus = index;
	if (tb->selected != index) {
		tb->selected = index;
		tb->changed = 1;
	}
}

int UiTabBar_Key(UiTabBar *tb, UiContext *ui, UINT vk)
{
	int n, idx;
	if (!tb || !ui || tb->count <= 0 || ui->navId != tb->id) return 0;
	n = tb->count;
	if (tb->focus < 0 || tb->focus >= n) tb->focus = tb->selected >= 0 ? tb->selected : 0;
	switch (vk) {
	case VK_LEFT:
	case VK_UP:
		idx = tab_find_next(tb, tb->focus, -1, 1);
		if (idx >= 0) tb->focus = idx;
		tb->focusVisible = 1;
		tab_invalidate(ui, &tb->rc);
		return 1;
	case VK_RIGHT:
	case VK_DOWN:
		idx = tab_find_next(tb, tb->focus, 1, 1);
		if (idx >= 0) tb->focus = idx;
		tb->focusVisible = 1;
		tab_invalidate(ui, &tb->rc);
		return 1;
	case VK_HOME:
		idx = tab_find_first(tb, 1);
		if (idx >= 0) tb->focus = idx;
		tb->focusVisible = 1;
		tab_invalidate(ui, &tb->rc);
		return 1;
	case VK_END:
		idx = tab_find_last(tb, 1);
		if (idx >= 0) tb->focus = idx;
		tb->focusVisible = 1;
		tab_invalidate(ui, &tb->rc);
		return 1;
	case VK_SPACE:
	case VK_RETURN:
		if (tab_item_enabled(tb, tb->focus, 1)) tab_activate(tb, tb->focus);
		tb->focusVisible = 1;
		tab_invalidate(ui, &tb->rc);
		return 1;
	}
	return 0;
}

int UiTabBar_Draw(UiTabBar *tb, UiContext *ui, UiRect rc,
	const char *const *labels, const UiTabStyle *style, int enabled)
{
	UiTabStyle stl;
	UiTheme *t;
	Gfx *g;
	UiRect header, inner;
	float height, padX, gap, radius, itemRadius, indicatorW;
	int variant, stretch, showDivider;
	int i, clicked = -1, selectedIndex = -1;
	UiRect selectedRc = { 0, 0, 0, 0 };
	UiColor cText, cHover, cActive, cDis, cInd, cHoverBg, cActiveBg, cBorder;
	UiColor cCardBg;

	if (!tb || !ui || !ui->theme || !ui->gfx || !labels || tb->count <= 0) return -1;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return -1;
	t = ui->theme;
	g = ui->gfx;
	stl = style ? *style : UiTabStyle_Line();
	variant = stl.variant == UI_TAB_PILL ? UI_TAB_PILL :
		(stl.variant == UI_TAB_CARD ? UI_TAB_CARD : UI_TAB_LINE);
	tb->changed = 0;
	tb->activeRc = Ui_Rect(0, 0, 0, 0);
	if (tb->selected >= tb->count) tb->selected = tb->count - 1;
	if (tb->selected < 0 && tb->count > 0) tb->selected = 0;
	if (tb->focus >= tb->count) tb->focus = tb->count - 1;
	if (tb->focus < 0) tb->focus = tb->selected;

	height = stl.height > 0.0f ? stl.height
		: (variant == UI_TAB_PILL ? 34.0f : (variant == UI_TAB_CARD ? 36.0f : 38.0f));
	if (height > rc.h) height = rc.h;
	header = Ui_Rect(rc.x, rc.y, rc.w, height);
	tb->rc = header;
	padX = stl.padX > 0.0f ? stl.padX : 14.0f;
	gap = stl.gap >= 0.0f ? stl.gap : (variant == UI_TAB_PILL ? 2.0f : 4.0f);
	radius = stl.radius > 0.0f ? stl.radius : 8.0f;
	itemRadius = variant == UI_TAB_CARD ? radius :
		(radius > 4.0f ? radius - 3.0f : 4.0f);
	indicatorW = stl.indicatorW > 0.0f ? stl.indicatorW : 2.0f;
	stretch = stl.stretch != 0;
	showDivider = stl.showDivider != 0;
	cText = stl.text ? stl.text : t->text2;
	cHover = stl.textHover ? stl.textHover : t->text;
	cActive = stl.textActive ? stl.textActive : t->primary.base;
	cDis = stl.textDisabled ? stl.textDisabled : t->btnDisabledText;
	cInd = stl.indicator ? stl.indicator : t->primary.base;
	cHoverBg = stl.hoverBg ? stl.hoverBg : t->primary.soft;
	cActiveBg = stl.activeBg ? stl.activeBg : t->neutral.base;
	cBorder = stl.activeBorder ? stl.activeBorder : t->neutral.border;
	cCardBg = stl.cardBg ? stl.cardBg : t->neutral.soft;

	Gfx_PushClip(g, header);
	Ui_PushClipRect(ui, header);
	if (variant == UI_TAB_PILL)
		Gfx_FillRoundRect(g, header, radius, t->neutral.soft);

	inner = Ui_Rect(header.x + (variant == UI_TAB_PILL ? 3.0f : 0.0f),
		header.y + (variant == UI_TAB_PILL ? 3.0f : 0.0f),
		header.w - (variant == UI_TAB_PILL ? 6.0f : 0.0f),
		header.h - (variant == UI_TAB_PILL ? 6.0f : 0.0f));
	if (inner.w <= 0.0f || inner.h <= 0.0f) {
		Ui_PopClipRect(ui);
		Gfx_PopClip(g);
		return -1;
	}

	{
		float x = inner.x;
		float equalW = stretch
			? (inner.w - gap * (float)(tb->count - 1)) / (float)tb->count
			: 0.0f;
		if (equalW < 0.0f) equalW = 0.0f;
		for (i = 0; i < tb->count; i++) {
			UiRect item;
			UiWidgetState st;
			UiId id;
			float w = equalW;
			int en = tab_item_enabled(tb, i, enabled);
			UiColor col;
			if (!stretch) {
				float tw = Gfx_TextWidth(g, t->fLabel, labels[i]);
				w = tw + padX * 2.0f;
				if (w < 44.0f) w = 44.0f;
			}
			item = Ui_Rect(x, inner.y, w, inner.h);
			x += w + gap;
			if (item.x >= header.x + header.w) break;
			id = tab_child_id(tb->id, i);
			st = Ui_State(ui, id, item, en);
			if (ui->clicked == id) {
				tab_activate(tb, i);
				tb->focusVisible = 0;
				ui->navId = tb->id;
				clicked = i;
				tab_invalidate(ui, &header);
			}
			if (tb->selected == i) {
				selectedRc = item;
				selectedIndex = i;
			}

			if (variant == UI_TAB_CARD) {
				if (tb->selected == i) {
					tb->activeRc = item;
					tb->activeHover = st.hover;
					tb->activePress = st.press;
					tb->activeFocus = (tb->focusVisible &&
						ui->navId == tb->id && tb->focus == i);
					/* 活动项在 DrawActive 里最后画, 不在这里画文字/底色. */
				} else {
					UiColor fill = cCardBg;
					UiColor edge = cBorder;
					GfxCorners corners = Gfx_Corners(itemRadius, itemRadius, 0, 0);
					if (en && st.hover > 0.004f) {
						fill = Ui_ColorMix(fill, cHoverBg, st.hover);
						edge = Ui_ColorMix(edge, t->primary.base, st.hover * 0.40f);
					}
					if (en && st.press > 0.004f)
						fill = Ui_ColorMix(fill, t->primary.base, st.press * 0.14f);
					Gfx_FillRoundRectCorners(g, item, corners, fill);
					Gfx_StrokeRoundRectOpen(g, item, corners, 1u | 2u | 8u,
						edge, 1.0f);
					if (tb->focusVisible && ui->navId == tb->id && tb->focus == i) {
						UiRect ring = Ui_RectInset(item, 2.0f);
						Gfx_StrokeRoundRect(g, ring, itemRadius > 2.0f ? itemRadius - 2.0f : 2.0f,
							Ui_ColorAlpha(t->primary.base, 200), 1.5f);
					}
					col = cText;
					if (en && st.hover > 0.004f)
						col = Ui_ColorMix(col, cHover, st.hover);
					if (!en) col = cDis;
					Gfx_Text(g, t->fLabel, labels[i], item, col,
						GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
				}
			} else if (variant == UI_TAB_PILL) {
				UiRect ir = Ui_RectInset(item, 2.0f);
				if (tb->selected == i) {
					Gfx_FillRoundRect(g, ir, itemRadius, cActiveBg);
					Gfx_StrokeRoundRect(g, ir, itemRadius, cBorder, 1.0f);
				} else {
					if (st.hover > 0.004f && en) {
						Gfx_FillRoundRect(g, ir, itemRadius,
							Ui_ColorAlpha(cHoverBg, (int)(st.hover * 255.0f)));
						Gfx_StrokeRoundRect(g, ir, itemRadius,
							Ui_ColorAlpha(t->primary.base,
								(int)(st.hover * 110.0f)), 1.0f);
					}
					if (st.press > 0.004f && en) {
						Gfx_FillRoundRect(g, ir, itemRadius,
							Ui_ColorMix(cHoverBg, t->primary.base,
								st.press * 0.16f));
					}
				}
				if (tb->focusVisible && ui->navId == tb->id && tb->focus == i) {
					UiRect ring = Ui_RectInset(item, 2.0f);
					Gfx_StrokeRoundRect(g, ring, itemRadius,
						Ui_ColorAlpha(t->primary.base, 200), 1.5f);
				}
				col = tb->selected == i ? cActive : cText;
				if (en && tb->selected != i && st.hover > 0.004f)
					col = Ui_ColorMix(col, cHover, st.hover);
				if (!en) col = cDis;
				Gfx_Text(g, t->fLabel, labels[i], item, col,
					GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			} else {
				if (en && tb->selected != i && st.hover > 0.004f) {
					UiRect ir = Ui_RectInset(item, 2.0f);
					Gfx_FillRoundRect(g, ir, 6.0f,
						Ui_ColorAlpha(cHoverBg, (int)(st.hover * 220.0f)));
				}
				if (tb->focusVisible && ui->navId == tb->id && tb->focus == i) {
					UiRect ring = Ui_RectInset(item, 2.0f);
					Gfx_StrokeRoundRect(g, ring, 6.0f,
						Ui_ColorAlpha(t->primary.base, 200), 1.5f);
				}
				col = tb->selected == i ? cActive : cText;
				if (en && tb->selected != i && st.hover > 0.004f)
					col = Ui_ColorMix(col, cHover, st.hover);
				if (!en) col = cDis;
				Gfx_Text(g, t->fLabel, labels[i], item, col,
					GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			}
		}
	}

	if (variant == UI_TAB_LINE && showDivider) {
		Gfx_FillRoundRect(g, Ui_Rect(header.x, header.y + header.h - 0.5f,
			header.w, 1.0f), 0.0f, Ui_ColorAlpha(t->neutral.border, 220));
	}
	if (variant == UI_TAB_LINE && selectedIndex >= 0) {
		float tw = Gfx_TextWidth(g, t->fLabel, labels[selectedIndex]);
		float iw = tw + 12.0f;
		float maxW = selectedRc.w - 8.0f;
		float tx, ty, ax, aw;
		if (iw > maxW) iw = maxW;
		if (iw < 8.0f) iw = 8.0f;
		tx = selectedRc.x + (selectedRc.w - iw) * 0.5f;
		ty = header.y + header.h - indicatorW;
		if (!tb->indicatorInit) {
			Ui_AnimSet(ui, tb->id, UI_AN_USER, tx);
			Ui_AnimSet(ui, tb->id, UI_AN_USER + 1, iw);
			tb->indicatorInit = 1;
		} else {
			Ui_AnimSmooth(ui, tb->id, UI_AN_USER, tx);
			Ui_AnimSmooth(ui, tb->id, UI_AN_USER + 1, iw);
		}
		ax = Ui_AnimGet(ui, tb->id, UI_AN_USER, tx);
		aw = Ui_AnimGet(ui, tb->id, UI_AN_USER + 1, iw);
		if (aw > 0.5f)
			Gfx_FillRoundRect(g, Ui_Rect(ax, ty, aw, indicatorW),
				indicatorW * 0.5f, cInd);
	}

	Ui_PopClipRect(ui);
	Gfx_PopClip(g);
	return clicked;
}

UiRect UiTabBar_ActiveRect(const UiTabBar *tb)
{
	return tb ? tb->activeRc : Ui_Rect(0, 0, 0, 0);
}

void UiTabBar_DrawActive(UiTabBar *tb, UiContext *ui,
	const char *const *labels, const UiTabStyle *style)
{
	UiTabStyle stl;
	UiTheme *t;
	Gfx *g;
	UiRect ar, ring;
	float radius;
	UiColor cBg, cBorder, cText, cDis;
	int merge;

	if (!tb || !ui || !ui->theme || !ui->gfx || !labels) return;
	stl = style ? *style : UiTabStyle_Card();
	if (stl.variant != UI_TAB_CARD) return;
	if (tb->activeRc.w <= 0.0f || tb->activeRc.h <= 0.0f) return;
	if (tb->selected < 0 || tb->selected >= tb->count) return;
	t = ui->theme;
	g = ui->gfx;
	radius = stl.radius > 0.0f ? stl.radius : 8.0f;
	merge = stl.mergeContent != 0;
	cBg = stl.contentBg ? stl.contentBg : t->card;
	cBorder = stl.activeBorder ? stl.activeBorder : t->neutral.border;
	cText = stl.textActive ? stl.textActive : t->primary.base;
	cDis = stl.textDisabled ? stl.textDisabled : t->btnDisabledText;

	ar = tb->activeRc;
	if (merge) ar.h += 1.0f;
	{
		GfxCorners corners = Gfx_Corners(radius, radius, 0, 0);
		Gfx_FillRoundRectCorners(g, ar, corners, cBg);
		Gfx_StrokeRoundRectOpen(g, ar, corners, 1u | 2u | 8u, cBorder, 1.0f);
	}
	if (tb->focusVisible && ui->navId == tb->id && tb->focus == tb->selected) {
		ring = Ui_RectInset(tb->activeRc, 2.0f);
		Gfx_StrokeRoundRect(g, ring, radius > 2.0f ? radius - 2.0f : 2.0f,
			Ui_ColorAlpha(t->primary.base, 200), 1.5f);
	}
	Gfx_Text(g, t->fLabel, labels[tb->selected], tb->activeRc,
		tab_item_enabled(tb, tb->selected, 1) ? cText : cDis,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

/* ── TabPager ───────────────────────────────────────────────────────── */

static void pager_invalidate(UiContext *ui, const UiRect *rc)
{
	RECT r;
	float s;
	if (!ui || !ui->hwnd || !rc || rc->w <= 0.0f || rc->h <= 0.0f) return;
	s = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((rc->x - 2.0f) * s);
	r.top = (LONG)((rc->y - 2.0f) * s);
	r.right = (LONG)((rc->x + rc->w + 2.0f) * s) + 1;
	r.bottom = (LONG)((rc->y + rc->h + 2.0f) * s) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

static float pager_ease(float p)
{
	float q;
	if (p <= 0.0f) return 0.0f;
	if (p >= 1.0f) return 1.0f;
	q = 1.0f - p;
	return 1.0f - q * q * q;
}

void UiTabPager_Init(UiTabPager *p, UiId id, const UiTabPagerConfig *cfg)
{
	UiTabPagerConfig c;
	if (!p) return;
	ZeroMemory(p, sizeof(*p));
	p->id = id;
	if (cfg) c = *cfg;
	else {
		ZeroMemory(&c, sizeof(c));
		c.axis = UI_TAB_AXIS_HORIZONTAL;
		c.durationMs = 260.0f;
	}
	if (c.axis < UI_TAB_AXIS_NONE || c.axis > UI_TAB_AXIS_VERTICAL)
		c.axis = UI_TAB_AXIS_HORIZONTAL;
	if (c.axis == UI_TAB_AXIS_NONE) c.fade = 1;   /* 无位移时必须淡入淡出 */
	if (c.durationMs <= 0.0f) c.durationMs = 260.0f;
	p->cfg = c;
	p->from = p->to = 0;
	p->progress = 1.0f;
}

void UiTabPager_GoTo(UiTabPager *p, UiContext *ui, int index, int animate)
{
	if (!p || !ui || index < 0) return;
	if (index == p->to && (p->animating || p->progress >= 1.0f)) return;
	if (!animate || p->cfg.durationMs <= 0.0f || ui->animReduced) {
		p->from = p->to = index;
		p->progress = 1.0f;
		p->animating = 0;
		pager_invalidate(ui, &p->viewport);
		return;
	}
	/* 转场中再次切换: 先 snap 到当前目标, 再从目标开始新转场. */
	p->from = p->to;
	p->to = index;
	p->progress = 0.0f;
	p->animating = 1;
	pager_invalidate(ui, &p->viewport);
}

int UiTabPager_Active(const UiTabPager *p)
{
	return p ? p->to : 0;
}

int UiTabPager_Busy(const UiTabPager *p)
{
	return p && p->animating && p->progress < 1.0f;
}

void UiTabPager_Tick(UiTabPager *p, UiContext *ui, float dtMs)
{
	if (!p || !ui || !p->animating) return;
	if (dtMs < 0.0f) dtMs = 0.0f;
	if (dtMs > 50.0f) dtMs = 50.0f;
	p->progress += dtMs / p->cfg.durationMs;
	if (p->progress >= 1.0f) {
		p->progress = 1.0f;
		p->animating = 0;
		p->from = p->to;
	}
	pager_invalidate(ui, &p->viewport);
}

static void pager_draw_page(UiContext *ui, UiTabPageFn fn, void *user,
	int index, UiRect rc, int interactive, float alpha)
{
	int pushed = 0;
	if (alpha <= 0.004f) return;
	if (alpha < 0.996f) pushed = Gfx_PushAlpha(ui->gfx, alpha);
	fn(ui, user, index, rc, interactive);
	if (pushed) Gfx_PopAlpha(ui->gfx);
}

void UiTabPager_Draw(UiTabPager *p, UiContext *ui, UiRect viewport,
	UiTabPageFn fn, void *user)
{
	float e, dir, ax, ay;
	UiRect outRc, inRc;
	int fade;
	if (!p || !ui || !ui->gfx || !fn) return;
	if (viewport.w <= 0.0f || viewport.h <= 0.0f) return;
	p->viewport = viewport;

	if (!p->animating || p->from == p->to || p->progress >= 1.0f) {
		Gfx_PushClip(ui->gfx, viewport);
		Ui_PushClipRect(ui, viewport);
		fn(ui, user, p->to, viewport, 1);
		Ui_PopClipRect(ui);
		Gfx_PopClip(ui->gfx);
		return;
	}

	e = pager_ease(p->progress);
	dir = p->to > p->from ? 1.0f : -1.0f;
	if (p->cfg.reverse) dir = -dir;
	fade = p->cfg.fade != 0 || p->cfg.axis == UI_TAB_AXIS_NONE;
	ax = (p->cfg.axis == UI_TAB_AXIS_HORIZONTAL) ? dir * e * viewport.w : 0.0f;
	ay = (p->cfg.axis == UI_TAB_AXIS_VERTICAL) ? dir * e * viewport.h : 0.0f;
	outRc = viewport;
	outRc.x -= ax;
	outRc.y -= ay;
	inRc = viewport;
	if (p->cfg.axis == UI_TAB_AXIS_HORIZONTAL)
		inRc.x += dir * (1.0f - e) * viewport.w;
	if (p->cfg.axis == UI_TAB_AXIS_VERTICAL)
		inRc.y += dir * (1.0f - e) * viewport.h;

	Gfx_PushClip(ui->gfx, viewport);
	Ui_PushClipRect(ui, viewport);
	Ui_PushInputBlock(ui);
	pager_draw_page(ui, fn, user, p->from, outRc, 0, fade ? 1.0f - e : 1.0f);
	pager_draw_page(ui, fn, user, p->to, inRc, 0, fade ? e : 1.0f);
	Ui_PopInputBlock(ui);
	Ui_PopClipRect(ui);
	Gfx_PopClip(ui->gfx);
}
