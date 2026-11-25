#include "ui/ui_tab.h"
#include "gfx/gfx_fx.h"

static UINT tab_bit(int i)
{
	return i >= 0 && i < 32 ? (1u << i) : 0u;
}

/* LINE 指示条内容: 按最大宽度画一条实心条, 圆角由 DComp clip 提供 */
static void tab_ind_draw(Gfx *g, void *user)
{
	UiTabBar *tb = (UiTabBar *)user;
	Gfx_FillRoundRect(g, Ui_Rect(0.0f, 0.0f, tb->indContentW, tb->indH),
		0.0f, tb->indColor);
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
	tb->indSel = -2;
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
	tb->indSel = -2;
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

static float tab_icon_size(const UiIcon *icon)
{
	return (icon && icon->kind == UI_ICON_GLYPH && icon->font) ? 16.0f : 0.0f;
}

/* textW < 0 = 自行测量; 有缓存时传宽度, 避免每帧重复 DWrite 测量. */
static void tab_draw_content(UiContext *ui, const char *label,
	const UiIcon *icon, UiRect item, UiColor color, int enabled, float textW)
{
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	float iconW = tab_icon_size(icon);
	if (textW < 0.0f)
		textW = label ? Gfx_TextWidth(g, t->fLabel, label) : 0.0f;
	float gap = (iconW > 0.0f && textW > 0.0f) ? 6.0f : 0.0f;
	float contentW = iconW + gap + textW;
	float x = item.x + (item.w - contentW) * 0.5f;
	if (iconW > 0.0f) {
		UiRect ir = Ui_Rect(x, item.y + (item.h - iconW) * 0.5f,
			iconW, iconW);
		UiIcon_Draw(ui, icon, ir, color, enabled);
	}
	if (label && label[0]) {
		UiRect tr = Ui_Rect(x + iconW + gap, item.y,
			textW + 2.0f, item.h);
		Gfx_Text(g, t->fLabel, label, tr, color,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
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

void UiTabBarGeometry_Free(UiTabBarGeometry *geo)
{
	if (!geo) return;
	HeapFree(GetProcessHeap(), 0, geo->items);
	HeapFree(GetProcessHeap(), 0, geo->textW);
	geo->items = NULL;
	geo->textW = NULL;
	geo->count = geo->capacity = 0;
}

int UiTabBar_Measure(UiContext *ui, UiRect rc, const char *const *labels,
	const UiIcon *icons, const UiTabStyle *style, int count,
	UiTabBarGeometry *geo)
{
	UiTabStyle stl;
	float height, padX, gap, x, equalW;
	int variant, stretch, i, n = 0;

	if (!ui || !ui->theme || !ui->gfx || !labels || !geo || count <= 0) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	geo->count = 0;
	if (count > geo->capacity) {
		UiRect *items;
		float *textW;
		items = (UiRect *)HeapReAlloc(GetProcessHeap(), 0, geo->items,
			(SIZE_T)count * sizeof(*items));
		if (!items) return 0;
		geo->items = items;
		textW = (float *)HeapReAlloc(GetProcessHeap(), 0, geo->textW,
			(SIZE_T)count * sizeof(*textW));
		if (!textW) return 0;
		geo->textW = textW;
		geo->capacity = count;
	}
	stl = style ? *style : UiTabStyle_Line();
	variant = stl.variant == UI_TAB_PILL ? UI_TAB_PILL :
		(stl.variant == UI_TAB_CARD ? UI_TAB_CARD : UI_TAB_LINE);
	height = stl.height > 0.0f ? stl.height
		: (variant == UI_TAB_PILL ? 34.0f : (variant == UI_TAB_CARD ? 36.0f : 38.0f));
	if (height > rc.h) height = rc.h;
	geo->header = Ui_Rect(rc.x, rc.y, rc.w, height);
	geo->inner = Ui_Rect(geo->header.x + (variant == UI_TAB_PILL ? 3.0f : 0.0f),
		geo->header.y + (variant == UI_TAB_PILL ? 3.0f : 0.0f),
		geo->header.w - (variant == UI_TAB_PILL ? 6.0f : 0.0f),
		geo->header.h - (variant == UI_TAB_PILL ? 6.0f : 0.0f));
	geo->variant = variant;
	geo->count = 0;
	if (geo->inner.w <= 0.0f || geo->inner.h <= 0.0f) return 0;

	padX = stl.padX > 0.0f ? stl.padX : 14.0f;
	gap = stl.gap >= 0.0f ? stl.gap : (variant == UI_TAB_PILL ? 2.0f : 4.0f);
	stretch = stl.stretch != 0;
	x = geo->inner.x;
	equalW = stretch
		? (geo->inner.w - gap * (float)(count - 1)) / (float)count
		: 0.0f;
	if (equalW < 0.0f) equalW = 0.0f;
	for (i = 0; i < count; i++) {
		float w = equalW;
		float tw = labels[i] ? Gfx_TextWidth(ui->gfx, ui->theme->fLabel,
			labels[i]) : 0.0f;
		if (!stretch) {
			float iw = icons ? tab_icon_size(&icons[i]) : 0.0f;
			w = iw + (iw > 0.0f && tw > 0.0f ? 6.0f : 0.0f)
				+ tw + padX * 2.0f;
			if (w < 44.0f) w = 44.0f;
		}
		if (x >= geo->header.x + geo->header.w) break;
		geo->items[n] = Ui_Rect(x, geo->inner.y, w, geo->inner.h);
		geo->textW[n] = tw;
		n++;
		x += w + gap;
	}
	geo->count = n;
	return n;
}

int UiTabBar_DrawEx(UiTabBar *tb, UiContext *ui, UiRect rc,
	const char *const *labels, const UiIcon *icons,
	const UiTabStyle *style, int enabled)
{
	UiTabStyle stl;
	UiTheme *t;
	Gfx *g;
	UiTabBarGeometry geo = { 0 };
	UiRect header;
	float radius, itemRadius, indicatorW;
	int variant, showDivider;
	int i, clicked = -1, selectedIndex = -1;
	UiRect selectedRc = { 0, 0, 0, 0 };
	float selectedTw = 0.0f;
	UiColor cText, cHover, cActive, cDis, cInd, cHoverBg, cActiveBg, cBorder;
	UiColor cCardBg;

	if (!tb || !ui || !ui->theme || !ui->gfx || !labels || tb->count <= 0) return -1;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return -1;
	t = ui->theme;
	g = ui->gfx;
	stl = style ? *style : UiTabStyle_Line();
	tb->changed = 0;
	tb->activeRc = Ui_Rect(0, 0, 0, 0);
	if (tb->selected >= tb->count) tb->selected = tb->count - 1;
	if (tb->selected < 0 && tb->count > 0) tb->selected = 0;
	if (tb->focus >= tb->count) tb->focus = tb->count - 1;
	if (tb->focus < 0) tb->focus = tb->selected;

	if (!UiTabBar_Measure(ui, rc, labels, icons, &stl, tb->count, &geo))
		return -1;
	header = geo.header;
	variant = geo.variant;
	tb->rc = header;
	radius = stl.radius > 0.0f ? stl.radius : 8.0f;
	itemRadius = variant == UI_TAB_CARD ? radius :
		(radius > 4.0f ? radius - 3.0f : 4.0f);
	indicatorW = stl.indicatorW > 0.0f ? stl.indicatorW : 2.0f;
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

	{
		for (i = 0; i < geo.count; i++) {
			UiRect item = geo.items[i];
			UiWidgetState st;
			UiId id;
			int en = tab_item_enabled(tb, i, enabled);
			UiColor col;
			float tw = geo.textW[i];
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
				selectedTw = tw;
			}

			if (variant == UI_TAB_CARD) {
				if (tb->selected == i) {
					tb->activeRc = item;
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
					tab_draw_content(ui, labels[i],
						icons ? &icons[i] : NULL, item, col, en, tw);
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
				tab_draw_content(ui, labels[i],
					icons ? &icons[i] : NULL, item, col, en, tw);
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
				tab_draw_content(ui, labels[i],
					icons ? &icons[i] : NULL, item, col, en, tw);
			}
		}
	}

	if (variant == UI_TAB_LINE && showDivider) {
		Gfx_FillRoundRect(g, Ui_Rect(header.x, header.y + header.h - 0.5f,
			header.w, 1.0f), 0.0f, Ui_ColorAlpha(t->neutral.border, 220));
	}
	if (variant == UI_TAB_LINE && selectedIndex >= 0) {
		float tw = selectedTw;   /* 主循环已测过 */
		float iconW = icons ? tab_icon_size(&icons[selectedIndex]) : 0.0f;
		float iw = tw + 12.0f + iconW +
			(iconW > 0.0f && tw > 0.0f ? 6.0f : 0.0f);
		float maxW = selectedRc.w - 8.0f;
		float tx, ty, ax, aw;
		int partOk = 0;
		if (iw > maxW) iw = maxW;
		if (iw < 8.0f) iw = 8.0f;
		tx = selectedRc.x + (selectedRc.w - iw) * 0.5f;
		ty = header.y + header.h - indicatorW;
		if (Ui_FxActive(ui)) {
			/* 指示条: 独立 visual, x 与宽度 (clip) 由合成器补间 */
			if (tb->indPart &&
				(tb->indContentW + 0.5f < header.w ||
					tb->indH != indicatorW)) {
				Gfx_PartDestroy((GfxPart *)tb->indPart);
				tb->indPart = NULL;
			}
			if (!tb->indPart) {
				tb->indContentW = header.w;
				tb->indH = indicatorW;
				tb->indColor = cInd;
				tb->indPart = Gfx_PartCreate(ui->gfx, header.w,
					indicatorW, tab_ind_draw, tb);
				if (tb->indPart) {
					Gfx_PartSetClipRadius((GfxPart *)tb->indPart,
						indicatorW * 0.5f);
					Gfx_PartUpdate((GfxPart *)tb->indPart);
					Ui_PartPlaceRect(ui, tb->indPart, tx, ty, iw, tb->indH);
					tb->indSel = selectedIndex;
				}
			} else if (tb->indColor != cInd) {
				tb->indColor = cInd;
				Gfx_PartUpdate((GfxPart *)tb->indPart);
			}
			if (tb->indPart) {
				partOk = 1;
				if (tb->indSel != selectedIndex) {
					Gfx_PartAnimateRect((GfxPart *)tb->indPart,
						tx, ty, iw, Ui_AnimFxMs(UI_AN_USER));
					tb->indSel = selectedIndex;
				} else {
					Ui_PartPlaceRect(ui, tb->indPart, tx, ty, iw, tb->indH);
				}
			}
		}
		if (!partOk) {
			/* 合成器部件不可用 (ULW 或创建失败): CPU 通道同样的视觉 */
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
	}

	Ui_PopClipRect(ui);
	Gfx_PopClip(g);
	UiTabBarGeometry_Free(&geo);
	return clicked;
}

UiRect UiTabBar_ActiveRect(const UiTabBar *tb)
{
	return tb ? tb->activeRc : Ui_Rect(0, 0, 0, 0);
}

int UiTabBar_Draw(UiTabBar *tb, UiContext *ui, UiRect rc,
	const char *const *labels, const UiTabStyle *style, int enabled)
{
	return UiTabBar_DrawEx(tb, ui, rc, labels, NULL, style, enabled);
}

void UiTabBar_DrawActiveEx(UiTabBar *tb, UiContext *ui,
	const char *const *labels, const UiIcon *icons, const UiTabStyle *style)
{
	UiTabStyle stl;
	UiTheme *t;
	Gfx *g;
	UiRect ar, ring;
	float radius;
	UiColor cBg, cBorder, cText, cDis;
	int merge, en;

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
	en = tab_item_enabled(tb, tb->selected, 1);

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
	tab_draw_content(ui, labels[tb->selected],
		icons ? &icons[tb->selected] : NULL, tb->activeRc,
		en ? cText : cDis, en, -1.0f);
}

void UiTabBar_DrawActive(UiTabBar *tb, UiContext *ui,
	const char *const *labels, const UiTabStyle *style)
{
	UiTabBar_DrawActiveEx(tb, ui, labels, NULL, style);
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
