#include "ui/ui_nav.h"
#include <string.h>

#define NAV_PAD_EDGE   4.0f
#define NAV_MORE_GAP   2.0f
#define NAV_DIV_H      9.0f
#define NAV_PANEL_PAD  3.0f
#define NAV_ARROW_W    16.0f
#define NAV_SHORTCUT_GAP 18.0f

/* ── 小工具 ─────────────────────────────────────────────────────────── */

static UiRect nav_empty(void)
{
	return Ui_Rect(0, 0, 0, 0);
}

static float nav_maxf(float a, float b)
{
	return a > b ? a : b;
}

static UiColor nav_col(UiColor c, UiColor def)
{
	return c ? c : def;
}

static UiColor nav_alpha_k(UiColor c, float k)
{
	int a = (int)((c >> 24) & 0xFF);
	if (k < 0.0f) k = 0.0f;
	if (k > 1.0f) k = 1.0f;
	a = (int)((float)a * k + 0.5f);
	return Ui_ColorAlpha(c, a);
}

static UiId nav_scope_id(const char *scope, UiId a, UiId b)
{
	return Ui_Id(scope,
		(int)(uint32_t)(a ^ (b >> 32)),
		(int)(uint32_t)((a >> 32) ^ b));
}

static UiId nav_level_id(const char *scope, UiId id, int level)
{
	return Ui_IdIndex(scope, (int)(uint32_t)(id >> 32),
		(int)(uint32_t)id, level);
}

static int nav_is_divider(const UiNavItem *it)
{
	return it && (it->flags & UI_NAV_DIVIDER) != 0;
}

static int nav_has_children(const UiNavItem *it)
{
	return it && it->children && it->childCount > 0;
}

static int nav_item_enabled(const UiNavItem *it)
{
	return it && !nav_is_divider(it) && (it->flags & UI_NAV_DISABLED) == 0;
}

static int nav_selectable(const UiNavItem *it)
{
	return it && (it->flags & UI_NAV_SELECTABLE) != 0;
}

static int nav_first_enabled(const UiNavItem *items, int count, int start)
{
	int i;
	if (!items || count <= 0) return -1;
	if (start < 0) start = 0;
	if (start >= count) start = count - 1;
	for (i = start; i < count; i++)
		if (nav_item_enabled(&items[i])) return i;
	for (i = 0; i < start && i < count; i++)
		if (nav_item_enabled(&items[i])) return i;
	return -1;
}

static int nav_find_index(const UiNavItem *items, int count, UiId id)
{
	int i;
	for (i = 0; i < count; i++)
		if (items[i].id == id) return i;
	return -1;
}

static const UiNavItem *nav_find_global_in(const UiNavItem *items, int count,
	UiId id)
{
	int i;
	for (i = 0; i < count; i++) {
		const UiNavItem *it = &items[i];
		const UiNavItem *found;
		if (it->id == id) return it;
		if (!nav_has_children(it)) continue;
		found = nav_find_global_in(it->children, it->childCount, id);
		if (found) return found;
	}
	return NULL;
}

static const UiNavItem *nav_find_global(const UiNavBar *nav, UiId id)
{
	if (!nav || !nav->items || !id) return NULL;
	return nav_find_global_in(nav->items, nav->count, id);
}

static void nav_style_norm(UiNavStyle *s)
{
	UiNavStyle d;
	if (!s) return;
	d = UiNavStyle_Default();
	if (s->barHeight <= 0.0f) s->barHeight = d.barHeight;
	if (s->itemH <= 0.0f) s->itemH = d.itemH;
	if (s->padX <= 0.0f) s->padX = d.padX;
	if (s->gap < 0.0f) s->gap = d.gap;
	if (s->radius <= 0.0f) s->radius = d.radius;
	if (s->iconSize <= 0.0f) s->iconSize = d.iconSize;
	if (s->iconGap <= 0.0f) s->iconGap = d.iconGap;
	if (s->railWidth <= 0.0f) s->railWidth = d.railWidth;
	if (s->panelMinW <= 0.0f) s->panelMinW = d.panelMinW;
	if (s->panelMaxW <= 0.0f) s->panelMaxW = d.panelMaxW;
	if (s->panelRowH <= 0.0f) s->panelRowH = d.panelRowH;
	if (s->panelMaxH <= 0.0f) s->panelMaxH = d.panelMaxH;
	if (s->panelPadX <= 0.0f) s->panelPadX = d.panelPadX;
	if (s->panelPadY <= 0.0f) s->panelPadY = d.panelPadY;
	if (s->panelMaxW < s->panelMinW) s->panelMaxW = s->panelMinW;
}

UiNavStyle UiNavStyle_Default(void)
{
	UiNavStyle s;
	ZeroMemory(&s, sizeof(s));
	s.barHeight = 42.0f;
	s.itemH = 38.0f;
	s.padX = 14.0f;
	s.gap = 2.0f;
	s.radius = 8.0f;
	s.iconSize = 18.0f;
	s.iconGap = 8.0f;
	s.railWidth = 56.0f;
	s.panelMinW = 180.0f;
	s.panelMaxW = 320.0f;
	s.panelRowH = 32.0f;
	s.panelMaxH = 360.0f;
	s.panelPadX = 10.0f;
	s.panelPadY = 5.0f;
	return s;
}

/* ── 选中路径 ───────────────────────────────────────────────────────── */

static int nav_resolve(const UiNavBar *nav, UiId *out, int cap)
{
	const UiNavItem *items;
	int count, depth, i;
	if (!nav || !out || cap <= 0) return 0;
	items = nav->items;
	count = nav->count;
	depth = 0;
	for (i = 0; i < nav->requestedDepth && i < cap; i++) {
		int idx;
		const UiNavItem *found;
		if (!items || count <= 0) break;
		idx = nav_find_index(items, count, nav->requested[i]);
		if (idx < 0) break;
		out[depth++] = nav->requested[i];
		found = &items[idx];
		items = found->children;
		count = nav_has_children(found) ? found->childCount : 0;
	}
	return depth;
}

static void nav_apply_selected(UiNavBar *nav)
{
	int depth;
	if (!nav) return;
	depth = nav_resolve(nav, nav->selected, UI_NAV_MAX_DEPTH);
	nav->selectedDepth = depth;
}

static void nav_path_copy(UiId *dst, const UiId *src, int depth)
{
	int i;
	if (!dst) return;
	if (depth < 0) depth = 0;
	if (depth > UI_NAV_MAX_DEPTH) depth = UI_NAV_MAX_DEPTH;
	for (i = 0; i < depth; i++) dst[i] = src ? src[i] : UI_ID_NONE;
	for (; i < UI_NAV_MAX_DEPTH; i++) dst[i] = UI_ID_NONE;
}

static int nav_path_equal(const UiId *a, int ad, const UiId *b, int bd)
{
	int i;
	if (ad != bd) return 0;
	for (i = 0; i < ad; i++)
		if (a[i] != b[i]) return 0;
	return 1;
}

static void nav_pick(UiNavBar *nav, const UiId *path, int depth)
{
	int oldDepth;
	UiId old[UI_NAV_MAX_DEPTH];
	if (!nav) return;
	oldDepth = nav->requestedDepth;
	nav_path_copy(old, nav->requested, oldDepth);
	nav_path_copy(nav->requested, path, depth);
	nav->requestedDepth = depth;
	nav_apply_selected(nav);
	nav_path_copy(nav->activated, path, depth);
	nav->activatedDepth = depth;
	if (!nav_path_equal(old, oldDepth, path, depth))
		nav->changed = 1;
	if (depth > 0) nav->keyRoot = path[0];
}

/* ── 打开/关闭 ─────────────────────────────────────────────────────── */

static void nav_panel_reset(UiNavBar *nav, int level)
{
	if (!nav || level < 0 || level >= UI_NAV_MAX_DEPTH) return;
	nav->panelRc[level] = nav_empty();
	nav->panelStart[level] = 0;
	nav->panelVisible[level] = 0;
	nav->panelMore[level] = 0;
	nav->tailOffset[level] = 0;
}

static void nav_reset_panels(UiNavBar *nav)
{
	int i;
	for (i = 0; i < UI_NAV_MAX_DEPTH; i++) {
		nav_panel_reset(nav, i);
		nav->openTail[i] = 0;
	}
}

static void nav_clear_panel_rects_from(UiNavBar *nav, int from)
{
	int i;
	if (!nav) return;
	for (i = from < 0 ? 0 : from; i < UI_NAV_MAX_DEPTH; i++) {
		nav_panel_reset(nav, i);
		nav->openTail[i] = 0;
	}
}

static void nav_hover_clear(UiNavBar *nav)
{
	if (!nav) return;
	nav->hoverId = UI_ID_NONE;
	nav->hoverLevel = 0;
}

static void nav_close_session(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	int focusOnClose)
{
	if (!nav) return;
	if (pm && ui && UiPopupManager_IsOpen(pm, nav->id)) {
		pm->focusOnClose = focusOnClose != 0;
		UiPopupManager_Close(pm, ui);
	}
	nav->openDepth = 0;
	nav->openOverflow = 0;
	nav_clear_panel_rects_from(nav, 0);
	nav_hover_clear(nav);
}

static void nav_open_root(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	const UiNavItem *item, UiRect anchor, int focusOnClose)
{
	int idx;
	if (!nav || !pm || !ui || !item || !nav_has_children(item)) return;
	nav->openOverflow = 0;
	nav->openDepth = 1;
	nav_path_copy(nav->openPath, NULL, UI_NAV_MAX_DEPTH);
	nav->openPath[0] = item->id;
	nav->rootAnchor = anchor;
	nav->keyRoot = item->id;
	nav->keyDepth = 1;
	nav_path_copy(nav->keyItem, NULL, UI_NAV_MAX_DEPTH);
	idx = nav_first_enabled(item->children, item->childCount, 0);
	nav->keyItem[0] = idx >= 0 ? item->children[idx].id : UI_ID_NONE;
	nav->keyFocusVisible = focusOnClose != 0;
	nav_reset_panels(nav);
	nav_panel_reset(nav, 0);
	nav_hover_clear(nav);
	UiPopupManager_Open(pm, ui, nav->id, item->id, focusOnClose);
}

static void nav_open_overflow(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	UiRect anchor, int focusOnClose)
{
	int idx, start;
	const UiNavItem *items;
	int count;
	if (!nav || !pm || !ui || !nav->overflow) return;
	start = nav->visibleStart + nav->visibleCount;
	if (start >= nav->count) return;
	items = nav->items + start;
	count = nav->count - start;
	idx = nav_first_enabled(items, count, 0);
	if (idx < 0) return;
	nav->openOverflow = 1;
	nav->openDepth = 1;
	nav_path_copy(nav->openPath, NULL, UI_NAV_MAX_DEPTH);
	nav->rootAnchor = anchor;
	nav->keyRoot = nav->moreId;
	nav->keyDepth = 1;
	nav_path_copy(nav->keyItem, NULL, UI_NAV_MAX_DEPTH);
	nav->keyItem[0] = items[idx].id;
	nav->keyFocusVisible = focusOnClose != 0;
	nav_reset_panels(nav);
	nav_panel_reset(nav, 0);
	nav_hover_clear(nav);
	UiPopupManager_Open(pm, ui, nav->id, nav->moreId, focusOnClose);
}

static int nav_open_child(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	int level, const UiNavItem *item, int focusOnClose)
{
	int idx;
	if (!nav || !pm || !ui || !item || !nav_has_children(item)) return 0;
	if (!UiPopupManager_IsOpen(pm, nav->id)) return 0;
	if (level < 0 || level + 2 >= UI_NAV_MAX_DEPTH) return 0;
	if (nav->openDepth < level + 1) nav->openDepth = level + 1;
	nav->openPath[level + 1] = item->id;
	nav->openTail[level + 1] = 0;
	nav->openDepth = level + 2;
	nav->keyDepth = nav->openDepth;
	for (idx = level + 2; idx < UI_NAV_MAX_DEPTH; idx++) {
		nav->openPath[idx] = UI_ID_NONE;
		nav->openTail[idx] = 0;
		nav->tailOffset[idx] = 0;
		nav->keyItem[idx] = UI_ID_NONE;
	}
	idx = nav_first_enabled(item->children, item->childCount, 0);
	nav->keyItem[level + 1] = idx >= 0 ? item->children[idx].id : UI_ID_NONE;
	nav->keyFocusVisible = focusOnClose != 0;
	nav_panel_reset(nav, level + 1);
	nav_hover_clear(nav);
	return 1;
}

static void nav_activate_root(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	const UiNavItem *item, UiRect anchor)
{
	UiId path[1];
	if (!nav || !item || !nav_item_enabled(item)) return;
	nav->keyRoot = item->id;
	nav->keyFocusVisible = 0;
	if (!nav_has_children(item)) {
		path[0] = item->id;
		nav_pick(nav, path, 1);
		nav_close_session(nav, pm, ui, 0);
		return;
	}
	if (pm && UiPopupManager_IsOpen(pm, nav->id) && !nav->openOverflow &&
		nav->openDepth > 0 && nav->openPath[0] == item->id) {
		nav_close_session(nav, pm, ui, 0);
		return;
	}
	if (nav_selectable(item)) {
		path[0] = item->id;
		nav_pick(nav, path, 1);
	}
	nav_open_root(nav, pm, ui, item, anchor, 0);
}

/* ── 度量 ───────────────────────────────────────────────────────────── */

static float nav_root_item_w(const UiNavBar *nav, UiContext *ui,
	const UiNavItem *it)
{
	float w;
	if (nav_is_divider(it)) return NAV_DIV_H;
	w = nav->style.padX * 2.0f;
	if (it->icon.codepoint)
		w += nav->style.iconSize + nav->style.iconGap;
	w += Gfx_TextWidth(ui->gfx, ui->theme->fBody,
		it->label ? it->label : "");
	if (nav_has_children(it)) w += NAV_ARROW_W;
	return w;
}

static float nav_more_w(const UiNavBar *nav, UiContext *ui)
{
	(void)ui;
	return nav->style.padX * 2.0f + 22.0f;
}

static float nav_panel_w(const UiNavBar *nav, UiContext *ui,
	const UiNavItem *items, int count)
{
	float w = nav->style.panelMinW;
	int i;
	for (i = 0; i < count; i++) {
		const UiNavItem *it = &items[i];
		float need;
		if (nav_is_divider(it)) continue;
		need = nav->style.panelPadX * 2.0f + 10.0f;
		if (it->icon.codepoint)
			need += nav->style.iconSize + nav->style.iconGap;
		need += Gfx_TextWidth(ui->gfx, ui->theme->fBody,
			it->label ? it->label : "");
		if (it->shortcut && it->shortcut[0])
			need += NAV_SHORTCUT_GAP +
				Gfx_TextWidth(ui->gfx, ui->theme->fSmall, it->shortcut);
		if (nav_has_children(it)) need += NAV_ARROW_W;
		if (need > w) w = need;
	}
	if (w > nav->style.panelMaxW) w = nav->style.panelMaxW;
	if (ui->width > 0.0f && w > ui->width - 16.0f)
		w = nav_maxf(80.0f, ui->width - 16.0f);
	return w;
}

static float nav_panel_max_h(const UiNavBar *nav, const UiContext *ui)
{
	float h = nav->style.panelMaxH;
	float minH = nav->style.panelRowH * 2.0f + nav->style.panelPadY * 2.0f;
	float viewH = ui->height > 0.0f ? ui->height - 16.0f : 0.0f;
	if (viewH > 0.0f && h > viewH) h = viewH;
	if (h < minH && (viewH <= 0.0f || minH <= viewH)) h = minH;
	return h;
}

/* ── 悬停展开 ───────────────────────────────────────────────────────── */

static void nav_hover_fire(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	UiId id, int level, int parentLevel, UiRect anchor)
{
	const UiNavItem *it = nav_find_global(nav, id);
	if (!it) return;
	if (level == 0) nav_open_root(nav, pm, ui, it, anchor, 0);
	else nav_open_child(nav, pm, ui, parentLevel, it, 0);
}

static void nav_note_hover(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	UiId id, int level, int parentLevel, UiRect anchor, int canOpen)
{
	if (!nav) return;
	if (!canOpen) {
		nav_hover_clear(nav);
		return;
	}
	/* 会话已打开时悬停即时切换; 关闭态不因路过而弹菜单, 仍由点击打开. */
	if (nav->hoverId == id && nav->hoverLevel == level) return;
	nav_hover_clear(nav);
	nav_hover_fire(nav, pm, ui, id, level, parentLevel, anchor);
}

/* ── 绘制: 根项 ─────────────────────────────────────────────────────── */

static void nav_icon_draw(UiContext *ui, const UiNavItem *it, UiRect rc,
	UiColor tint, int enabled)
{
	UiIcon icon;
	if (!it || !it->icon.codepoint || !ui->theme) return;
	icon = it->icon;
	if (!icon.font) icon.font = ui->theme->fIcon;
	UiIcon_Draw(ui, &icon, rc, tint, enabled);
}

static void nav_chevron(UiContext *ui, UiRect rc, UiColor col, int enabled)
{
	if (!enabled && ui->theme) col = ui->theme->btnDisabledText;
	Gfx_Text(ui->gfx, ui->theme->fSmall, "\xE2\x80\xBA", rc, col,
		GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
}

/* ">>" 双箭头: 不依赖字体码点, 语义与设计要求一致. */
static void nav_double_chevron(UiContext *ui, UiRect rc, UiColor col)
{
	float cx = rc.x + rc.w * 0.5f;
	float cy = rc.y + rc.h * 0.5f;
	float x = cx - 4.0f;
	Gfx_Line(ui->gfx, x - 3.0f, cy - 4.0f, x + 2.0f, cy, col, 1.6f);
	Gfx_Line(ui->gfx, x - 3.0f, cy + 4.0f, x + 2.0f, cy, col, 1.6f);
	Gfx_Line(ui->gfx, x + 4.0f, cy - 4.0f, x + 9.0f, cy, col, 1.6f);
	Gfx_Line(ui->gfx, x + 4.0f, cy + 4.0f, x + 9.0f, cy, col, 1.6f);
}

static void nav_draw_root_item(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	const UiNavItem *it, UiRect rc, int enabled, int vertical)
{
	UiTheme *t = ui->theme;
	UiNavStyle *s = &nav->style;
	UiId id;
	UiWidgetState st;
	UiColor fill, textCol, accent;
	UiRect ir, tr;
	float iconW = 0.0f, gapW = 0.0f, textW, x, tw;
	int leaf, anc, hot;

	if (nav_is_divider(it)) {
		if (vertical) {
			Gfx_Line(ui->gfx, rc.x + 10.0f, rc.y + rc.h * 0.5f,
				rc.x + rc.w - 10.0f, rc.y + rc.h * 0.5f,
				t->neutral.border, 1.0f);
		} else {
			Gfx_Line(ui->gfx, rc.x + rc.w * 0.5f, rc.y + 7.0f,
				rc.x + rc.w * 0.5f, rc.y + rc.h - 7.0f,
				t->neutral.border, 1.0f);
		}
		return;
	}

	id = nav_scope_id("ui.nav.root", nav->id, it->id);
	st = Ui_State(ui, id, rc, enabled && nav_item_enabled(it));
	hot = enabled && nav_item_enabled(it) && st.hover > 0.004f;

	if (enabled && nav_item_enabled(it) && ui->clicked == id)
		nav_activate_root(nav, pm, ui, it, rc);

	leaf = nav->selectedDepth == 1 && nav->selected[0] == it->id;
	anc = nav->selectedDepth > 1 && nav->selected[0] == it->id;

	if (leaf || anc) {
		accent = nav_col(s->itemSelected, Ui_ColorAlpha(t->primary.base, 42));
		if (anc) accent = nav_alpha_k(accent, 0.62f);
		fill = accent;
		if (vertical)
			Gfx_FillRoundRect(ui->gfx, rc, s->radius, fill);
		else
			Gfx_FillRoundRect(ui->gfx, rc, s->radius, fill);
	}
	if (hot) {
		UiColor hc = nav_col(s->itemHover, Ui_ColorAlpha(t->primary.base, 24));
		Gfx_FillRoundRect(ui->gfx, rc, s->radius,
			nav_alpha_k(hc, st.hover));
	}

	textCol = nav_col(s->text, t->text2);
	if (leaf) textCol = nav_col(s->textSelected, t->primary.base);
	else if (anc) textCol = nav_col(s->textHover, t->text);
	else if (hot) {
		UiColor th = nav_col(s->textHover, t->text);
		textCol = Ui_ColorMix(textCol, th, st.hover);
	}
	if ((it->flags & UI_NAV_DANGER) != 0) textCol = t->error.base;
	if (!enabled || (it->flags & UI_NAV_DISABLED) != 0)
		textCol = t->btnDisabledText;

	if (!nav->rail || vertical) {
		if (leaf || anc) {
			UiColor ind = nav_col(s->indicator, t->primary.base);
			if (vertical) {
				Gfx_FillRoundRect(ui->gfx,
					Ui_Rect(rc.x + 3.0f, rc.y + 8.0f, 3.0f,
						nav_maxf(0.0f, rc.h - 16.0f)),
					1.5f, ind);
			} else {
				Gfx_FillRoundRect(ui->gfx,
					Ui_Rect(rc.x + 10.0f, rc.y + rc.h - 3.0f,
						nav_maxf(0.0f, rc.w - 20.0f), 2.0f),
					1.0f, ind);
			}
		}
	}

	if (it->icon.codepoint) {
		iconW = s->iconSize;
		gapW = s->iconGap;
	}

	if (vertical && nav->rail) {
		/* 有子项的项在右缘显示 ›, 与展开态/水平态的箭头语义一致;
		 * 右侧留 10 DIP 内缩, 不贴按钮边框; 图标整体左移让出标识位. */
		float kidsW = nav_has_children(it) ? 14.0f : 0.0f;
		float areaW = nav_maxf(0.0f, rc.w - kidsW);
		if (iconW > 0.0f) {
			ir = Ui_Rect(rc.x + (areaW - iconW) * 0.5f,
				rc.y + (rc.h - s->iconSize) * 0.5f,
				iconW, s->iconSize);
			nav_icon_draw(ui, it, ir, textCol,
				enabled && nav_item_enabled(it));
		} else {
			tr = Ui_Rect(rc.x + 4.0f, rc.y,
				nav_maxf(0.0f, areaW - 8.0f), rc.h);
			Gfx_Text(ui->gfx, t->fSmall, it->label ? it->label : "",
				tr, textCol, GFX_TEXT_CENTER | GFX_TEXT_VCENTER |
				GFX_TEXT_ELLIPSIS);
		}
		if (kidsW > 0.0f)
			nav_chevron(ui, Ui_Rect(rc.x + rc.w - 24.0f, rc.y,
				14.0f, rc.h), textCol,
				enabled && nav_item_enabled(it));
	} else {
		textW = Gfx_TextWidth(ui->gfx, t->fBody,
			it->label ? it->label : "");
		if (vertical) {
			x = rc.x + s->padX;
		} else {
			tw = iconW + gapW + textW +
				(nav_has_children(it) ? NAV_ARROW_W : 0.0f);
			x = rc.x + nav_maxf(s->padX, (rc.w - tw) * 0.5f);
		}
		if (iconW > 0.0f) {
			ir = Ui_Rect(x, rc.y + (rc.h - s->iconSize) * 0.5f,
				iconW, s->iconSize);
			nav_icon_draw(ui, it, ir, textCol,
				enabled && nav_item_enabled(it));
			x += iconW + gapW;
		}
		tr = Ui_Rect(x, rc.y,
			nav_maxf(0.0f, rc.x + rc.w - s->padX - x -
				(nav_has_children(it) ? NAV_ARROW_W : 0.0f)),
			rc.h);
		Gfx_Text(ui->gfx, t->fBody, it->label ? it->label : "", tr,
			textCol, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (nav_has_children(it)) {
			if (vertical) {
				nav_chevron(ui, Ui_Rect(rc.x + rc.w - 20.0f, rc.y,
					14.0f, rc.h), textCol,
					enabled && nav_item_enabled(it));
			} else {
				Gfx_Text(ui->gfx, t->fSmall, "\xE2\x96\xBE",
					Ui_Rect(rc.x + rc.w - 16.0f, rc.y,
						12.0f, rc.h), textCol,
					GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
			}
		}
	}

	if (nav->keyFocusVisible && nav->keyRoot == it->id) {
		Gfx_StrokeRoundRect(ui->gfx, rc, s->radius,
			Ui_ColorAlpha(t->primary.base, 150), 1.0f);
	}

	if (pm && UiPopupManager_IsOpen(pm, nav->id) && !nav->openOverflow &&
		nav->openDepth > 0 && nav->openPath[0] == it->id)
		nav->rootAnchor = rc;

	if (pm && hot && nav_has_children(it) && UiPopupManager_IsOpen(pm, nav->id) &&
		!nav->openOverflow && nav->openPath[0] != it->id) {
		nav_note_hover(nav, pm, ui, it->id, 0, -1, rc, 1);
	} else if (hot) {
		nav_note_hover(nav, pm, ui, it->id, 0, -1, rc, 0);
	}
}

static int nav_root_hidden(const UiNavBar *nav, UiId id)
{
	int i;
	if (!nav || !id) return 0;
	for (i = nav->visibleStart + nav->visibleCount; i < nav->count; i++)
		if (nav->items[i].id == id) return 1;
	return 0;
}

static void nav_draw_more(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	UiRect rc, int enabled)
{
	UiTheme *t = ui->theme;
	UiNavStyle *s = &nav->style;
	UiWidgetState st;
	UiColor col;
	int hot;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return;
	st = Ui_State(ui, nav->moreId, rc, enabled);
	hot = enabled && st.hover > 0.004f;
	if (enabled && ui->clicked == nav->moreId) {
		if (nav->openOverflow) nav_close_session(nav, pm, ui, 0);
		else nav_open_overflow(nav, pm, ui, rc, 0);
	}
	if (hot) {
		UiColor hc = nav_col(s->itemHover, Ui_ColorAlpha(t->primary.base, 24));
		Gfx_FillRoundRect(ui->gfx, rc, s->radius,
			nav_alpha_k(hc, st.hover));
	}
	if (nav->openOverflow || (nav->selectedDepth > 0 &&
		nav_root_hidden(nav, nav->selected[0]))) {
		UiColor sel = nav_col(s->itemSelected,
			Ui_ColorAlpha(t->primary.base, 42));
		Gfx_FillRoundRect(ui->gfx, rc, s->radius, sel);
	}
	col = nav_col(s->text, t->text2);
	if (hot) col = Ui_ColorMix(col, nav_col(s->textHover, t->text),
		st.hover);
	if (!enabled) col = t->btnDisabledText;
	nav_double_chevron(ui, rc, col);
	if (nav->keyFocusVisible && nav->keyRoot == nav->moreId)
		Gfx_StrokeRoundRect(ui->gfx, rc, s->radius,
			Ui_ColorAlpha(t->primary.base, 150), 1.0f);
}

/* ── 绘制: 子菜单面板 ───────────────────────────────────────────────── */

/* 返回该层完整行列表与起始偏移. 尾部续页复用上一层的完整列表. */
static int nav_panel_source(UiNavBar *nav, int level, const UiNavItem **items,
	int *count, int *start)
{
	const UiNavItem *root;
	if (!nav || !items || !count || !start ||
		level < 0 || level >= UI_NAV_MAX_DEPTH)
		return 0;
	if (nav->openTail[level]) {
		if (level <= 0 ||
			!nav_panel_source(nav, level - 1, items, count, start) ||
			nav->tailOffset[level] >= *count)
			return 0;
		*start = nav->tailOffset[level];
		return 1;
	}
	if (level == 0 && nav->openOverflow) {
		int s = nav->visibleStart + nav->visibleCount;
		if (!nav->overflow || s >= nav->count) return 0;
		*items = nav->items;
		*count = nav->count;
		*start = s;
		return 1;
	}
	root = nav_find_global(nav, nav->openPath[level]);
	if (!nav_has_children(root)) return 0;
	*items = root->children;
	*count = root->childCount;
	*start = 0;
	return *count > 0;
}

static int nav_panel_prefix(UiNavBar *nav, int level, UiId *prefix)
{
	int depth;
	if (!nav || !prefix || level < 0) return 0;
	if (nav->openTail[level])
		return nav_panel_prefix(nav, level - 1, prefix);
	if (level == 0) {
		if (nav->openOverflow) return 0;
		prefix[0] = nav->openPath[0];
		return 1;
	}
	depth = nav_panel_prefix(nav, level - 1, prefix);
	if (depth < UI_NAV_MAX_DEPTH)
		prefix[depth++] = nav->openPath[level];
	return depth;
}

/* 计算本页可显示行数与是否出现 "更多". 高度先按全部行算, 超出时给
 * 更多行预留一行, 再按行高向前装填. */
static int nav_panel_fit(const UiNavBar *nav, const UiNavItem *items,
	int count, int start, float maxH, int *visible, int *more, float *height)
{
	float pad, rh, used = 0.0f;
	int i;
	*visible = 0;
	*more = 0;
	if (!nav || !items || start < 0 || start >= count) {
		*height = 0.0f;
		return 0;
	}
	pad = nav->style.panelPadY * 2.0f;
	rh = nav->style.panelRowH;
	for (i = start; i < count; i++)
		used += nav_is_divider(&items[i]) ? NAV_DIV_H : rh;
	if (pad + used <= maxH + 0.01f) {
		*visible = count - start;
		*height = pad + used;
		return 1;
	}
	used = 0.0f;
	for (i = start; i < count; i++) {
		float h = nav_is_divider(&items[i]) ? NAV_DIV_H : rh;
		if (pad + used + h + rh > maxH + 0.01f) break;
		used += h;
		(*visible)++;
	}
	if (*visible <= 0) {
		/* 视口异常小时至少显示一行, 不生成无法操作的更多行. */
		*visible = 1;
		used = nav_is_divider(&items[start]) ? NAV_DIV_H : rh;
		*height = pad + used;
		return 1;
	}
	if (start + *visible >= count) {
		*height = pad + used;
		return 1;
	}
	*more = 1;
	*height = pad + used + rh;
	return 1;
}

static void nav_mark_row(const UiNavBar *nav, const UiId *prefix, int prefixDepth,
	const UiNavItem *it, int *leaf, int *anc)
{
	*leaf = *anc = 0;
	if (!nav || !it) return;
	if (nav->selectedDepth <= prefixDepth) return;
	if (nav->selected[prefixDepth] != it->id) return;
	if (nav->selectedDepth == prefixDepth + 1) *leaf = 1;
	else *anc = 1;
}

static UiId nav_panel_more_id(const UiNavBar *nav, int level)
{
	return nav_level_id("ui.nav.more.row", nav->id, level);
}

static int nav_open_tail(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	int level, int keyboard)
{
	const UiNavItem *items;
	int count, start, idx, next;
	if (!nav || !pm || !ui) return 0;
	if (!UiPopupManager_IsOpen(pm, nav->id)) return 0;
	if (level < 0 || level + 1 >= UI_NAV_MAX_DEPTH) return 0;
	if (!nav->panelMore[level]) return 0;
	if (!nav_panel_source(nav, level, &items, &count, &start)) return 0;
	start = nav->panelStart[level] + nav->panelVisible[level];
	if (start >= count) return 0;
	next = level + 1;
	nav_panel_reset(nav, next);
	nav->openTail[next] = 1;
	nav->tailOffset[next] = start;
	nav->openPath[next] = UI_ID_NONE;
	for (idx = next + 1; idx < UI_NAV_MAX_DEPTH; idx++) {
		nav->openTail[idx] = 0;
		nav->tailOffset[idx] = 0;
		nav->openPath[idx] = UI_ID_NONE;
		nav->keyItem[idx] = UI_ID_NONE;
	}
	nav->openDepth = next + 1;
	nav->keyDepth = nav->openDepth;
	idx = nav_first_enabled(items, count, start);
	nav->keyItem[next] = idx >= 0 ? items[idx].id : UI_ID_NONE;
	nav->keyFocusVisible = keyboard != 0;
	nav_hover_clear(nav);
	return 1;
}

static void nav_row_activate(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	int level, const UiNavItem *it, const UiId *prefix, int prefixDepth,
	int keyboard)
{
	UiId path[UI_NAV_MAX_DEPTH];
	int depth = prefixDepth;
	int i;
	if (!nav || !it || !nav_item_enabled(it)) return;
	if (depth > UI_NAV_MAX_DEPTH - 1) depth = UI_NAV_MAX_DEPTH - 1;
	for (i = 0; i < depth; i++)
		path[i] = prefix[i];
	path[depth++] = it->id;
	nav->keyFocusVisible = keyboard != 0;
	if (nav_has_children(it)) {
		if (nav_selectable(it))
			nav_pick(nav, path, depth);
		nav_open_child(nav, pm, ui, level, it, keyboard != 0);
		return;
	}
	nav_pick(nav, path, depth);
	nav_close_session(nav, pm, ui, keyboard != 0);
}

static int nav_draw_panel(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	int level, UiRect panel, const UiNavItem *items, int start, int visible,
	int hasMore, const UiId *prefix, int prefixDepth, UiRect *nextAnchor)
{
	UiTheme *t = ui->theme;
	UiNavStyle *s = &nav->style;
	UiRect content, row, ir, tr, sr, ar, moreRc;
	float y, rh, lw, sw;
	UiId keyId;
	int i, leaf, anc, openChild, en;

	*nextAnchor = nav_empty();
	if (!nav || !pm || !ui || !items || visible <= 0) return 0;
	items += start;
	/* 同层 z + 后注册优先: 更深面板在循环里更晚绘制/注册, 仍能压住浅层. */
	Ui_PushLayer(ui, UI_Z_OVERLAY);
	Gfx_FillRoundRect(ui->gfx, panel, 8.0f,
		nav_col(s->panelBg, t->toastBg));
	Gfx_StrokeRoundRect(ui->gfx, panel, 8.0f,
		nav_col(s->panelBorder, t->toastBorder), 1.0f);

	rh = nav->style.panelRowH;
	keyId = nav->keyDepth > level ? nav->keyItem[level] : UI_ID_NONE;
	content = Ui_Rect(panel.x + s->panelPadX, panel.y + s->panelPadY,
		nav_maxf(0.0f, panel.w - s->panelPadX * 2.0f),
		nav_maxf(0.0f, panel.h - s->panelPadY * 2.0f));

	y = 0.0f;
	for (i = 0; i < visible; i++) {
		const UiNavItem *it = &items[i];
		float h = nav_is_divider(it) ? NAV_DIV_H : rh;
		row = Ui_Rect(content.x, content.y + y, content.w, h);
		if (nav_is_divider(it)) {
			Gfx_Line(ui->gfx, row.x + 6.0f, row.y + h * 0.5f,
				row.x + row.w - 6.0f, row.y + h * 0.5f,
				t->neutral.border, 1.0f);
			y += h;
			continue;
		}
		en = nav_item_enabled(it);
		{
			UiId rid = nav_scope_id("ui.nav.row", nav->id,
				it->id ^ ((UiId)(level + 1) << 48));
			UiWidgetState st = Ui_StateInstant(ui, rid, row, en);
			UiColor fill, col;
			int hot = en && st.hover > 0.004f;

			nav_mark_row(nav, prefix, prefixDepth, it, &leaf, &anc);
			openChild = nav->openDepth > level + 1 &&
				nav->openPath[level + 1] == it->id;

			if (en && ui->clicked == rid) {
				nav_row_activate(nav, pm, ui, level, it, prefix,
					prefixDepth, 0);
				if (!UiPopupManager_IsOpen(pm, nav->id))
					break;
			}

			/* 展开子菜单的父项不用底色标记: hover 在兄弟行等待弹出时
			 * 两行会同时像 hover. 改用左侧细线, 与 hover 底色区分. */
			if (leaf || anc) {
				fill = nav_col(s->itemSelected,
					Ui_ColorAlpha(t->primary.base, 42));
				if (anc) fill = nav_alpha_k(fill, 0.62f);
				Gfx_FillRoundRect(ui->gfx, row, s->radius, fill);
			}
			if (hot) {
				UiColor hc = nav_col(s->itemHover,
					Ui_ColorAlpha(t->primary.base, 24));
				Gfx_FillRoundRect(ui->gfx, row, s->radius,
					nav_alpha_k(hc, st.hover));
			}

			if (en && hot && nav_has_children(it) &&
				(nav->openDepth <= level + 1 ||
				 nav->openPath[level + 1] != it->id))
				nav_note_hover(nav, pm, ui, it->id, level + 1, level,
					row, 1);
			else if (hot)
				nav_note_hover(nav, pm, ui, it->id, level + 1, level,
					row, 0);

			col = nav_col(s->text, t->text2);
			if (leaf) col = nav_col(s->textSelected, t->primary.base);
			else if (anc) col = nav_col(s->textHover, t->text);
			else if (hot)
				col = Ui_ColorMix(col,
					nav_col(s->textHover, t->text), st.hover);
			if ((it->flags & UI_NAV_DANGER) != 0) col = t->error.base;
			if (!en) col = t->btnDisabledText;

			lw = 0.0f;
			ir = nav_empty();
			if (it->icon.codepoint) {
				ir = Ui_Rect(row.x + s->panelPadX,
					row.y + (row.h - s->iconSize) * 0.5f,
					s->iconSize, s->iconSize);
				nav_icon_draw(ui, it, ir, col, en);
				lw = s->iconSize + s->iconGap;
			}

			sr = nav_empty();
			if (it->shortcut && it->shortcut[0]) {
				sw = Gfx_TextWidth(ui->gfx, t->fSmall, it->shortcut);
				sr = Ui_Rect(row.x + row.w - s->panelPadX - sw,
					row.y, sw, row.h);
				Gfx_Text(ui->gfx, t->fSmall, it->shortcut, sr,
					en ? t->text3 : t->btnDisabledText,
					GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
			}

			ar = nav_empty();
			if (nav_has_children(it)) {
				ar = Ui_Rect(row.x + row.w - s->panelPadX -
					NAV_ARROW_W, row.y, NAV_ARROW_W - 2.0f, row.h);
				nav_chevron(ui, ar, col, en);
			}

			tr = Ui_Rect(row.x + s->panelPadX + lw, row.y,
				nav_maxf(0.0f, row.w - s->panelPadX * 2.0f - lw -
					(sr.w > 0.0f ? sr.w + NAV_SHORTCUT_GAP : 0.0f) -
					(ar.w > 0.0f ? ar.w : 0.0f)),
				row.h);
			Gfx_Text(ui->gfx, t->fBody, it->label ? it->label : "",
				tr, col, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

			if (leaf || anc || openChild) {
				UiColor ind = nav_col(s->indicator, t->primary.base);
				if (!leaf && !anc) ind = nav_alpha_k(ind, 0.55f);
				Gfx_FillRoundRect(ui->gfx,
					Ui_Rect(row.x + 3.0f, row.y + 7.0f, 3.0f,
						nav_maxf(0.0f, row.h - 14.0f)),
					1.5f, ind);
			}
			if (nav->keyFocusVisible && keyId == it->id)
				Gfx_StrokeRoundRect(ui->gfx, row, s->radius,
					Ui_ColorAlpha(t->primary.base, 150), 1.0f);

			if (nav->openDepth > level + 1 &&
				nav->openPath[level + 1] == it->id &&
				nav_has_children(it))
				*nextAnchor = row;
		}
		y += h;
	}
	if (hasMore) {
		UiId mid = nav_panel_more_id(nav, level);
		UiWidgetState st;
		UiColor col;
		int hot;
		moreRc = Ui_Rect(content.x, content.y + y, content.w, rh);
		st = Ui_StateInstant(ui, mid, moreRc, 1);
		hot = st.hover > 0.004f;
		if (hot)
			Gfx_FillRoundRect(ui->gfx, moreRc, s->radius,
				nav_alpha_k(nav_col(s->itemHover,
					Ui_ColorAlpha(t->primary.base, 24)), st.hover));
		col = hot ? Ui_ColorMix(nav_col(s->text, t->text2),
			nav_col(s->textHover, t->text), st.hover)
			: nav_col(s->text, t->text2);
		Gfx_Text(ui->gfx, t->fBody, "更多",
			Ui_Rect(moreRc.x + s->panelPadX, moreRc.y,
				nav_maxf(0.0f, moreRc.w - s->panelPadX * 2.0f - 28.0f),
				moreRc.h), col, GFX_TEXT_VCENTER);
		nav_double_chevron(ui,
			Ui_Rect(moreRc.x + moreRc.w - s->panelPadX - 22.0f,
				moreRc.y, 22.0f, moreRc.h), col);
		if (nav->keyFocusVisible && keyId == mid)
			Gfx_StrokeRoundRect(ui->gfx, moreRc, s->radius,
				Ui_ColorAlpha(t->primary.base, 150), 1.0f);
		if (ui->clicked == mid)
			nav_open_tail(nav, pm, ui, level, 0);
		if (nav->openDepth > level + 1 && nav->openTail[level + 1])
			*nextAnchor = moreRc;
	}
	Ui_PopLayer(ui);
	return UiPopupManager_IsOpen(pm, nav->id) ? 1 : 0;
}

/* ── 公共 API ───────────────────────────────────────────────────────── */

void UiNavBar_Init(UiNavBar *nav, UiId id)
{
	if (!nav) return;
	ZeroMemory(nav, sizeof(*nav));
	nav->id = id ? id : Ui_Id("ui.nav", 0, 0);
	nav->style = UiNavStyle_Default();
	nav->layout = UI_NAV_HORIZONTAL;
	nav->moreId = Ui_Id("ui.nav.more", (int)(uint32_t)(nav->id >> 32),
		(int)(uint32_t)nav->id);
	nav->keyRoot = UI_ID_NONE;
}

void UiNavBar_SetItems(UiNavBar *nav, const UiNavItem *items, int count)
{
	if (!nav) return;
	if (nav->items != items || nav->count != (count > 0 ? count : 0))
		nav->openDepth = 0;
	nav->items = items;
	nav->count = items && count > 0 ? count : 0;
	if (nav->count <= 0) {
		nav->selectedDepth = 0;
		return;
	}
	nav_apply_selected(nav);
}

void UiNavBar_SetLayout(UiNavBar *nav, int layout)
{
	if (!nav) return;
	layout = layout == UI_NAV_VERTICAL ? UI_NAV_VERTICAL : UI_NAV_HORIZONTAL;
	if (nav->layout == layout) return;
	nav->layout = layout;
	nav->openDepth = 0;
	nav->openOverflow = 0;
}

void UiNavBar_SetRail(UiNavBar *nav, int rail)
{
	if (!nav) return;
	rail = rail ? 1 : 0;
	if (nav->rail == rail) return;
	nav->rail = rail;
}

void UiNavBar_SetStyle(UiNavBar *nav, const UiNavStyle *style)
{
	if (!nav) return;
	nav->style = style ? *style : UiNavStyle_Default();
	nav_style_norm(&nav->style);
}

void UiNavBar_SetSelected(UiNavBar *nav, const UiId *path, int depth)
{
	if (!nav) return;
	if (depth < 0) depth = 0;
	if (depth > UI_NAV_MAX_DEPTH) depth = UI_NAV_MAX_DEPTH;
	nav_path_copy(nav->requested, path, depth);
	nav->requestedDepth = depth;
	nav_apply_selected(nav);
}

int UiNavBar_GetSelected(const UiNavBar *nav, UiId *path, int cap)
{
	int i, depth;
	if (!nav) return 0;
	depth = nav->selectedDepth;
	if (path && cap > 0) {
		for (i = 0; i < depth && i < cap; i++) path[i] = nav->selected[i];
	}
	return depth;
}

int UiNavBar_TakeChanged(UiNavBar *nav, UiId *path, int cap)
{
	int i, depth;
	if (!nav || !nav->changed) return 0;
	nav->changed = 0;
	depth = nav->activatedDepth;
	if (path && cap > 0)
		for (i = 0; i < depth && i < cap; i++) path[i] = nav->activated[i];
	return 1;
}

static void nav_horizontal_measure(UiNavBar *nav, UiContext *ui, UiRect content)
{
	float avail = content.w;
	float total = 0.0f, cursor = 0.0f;
	float moreW;
	int i, visible = 0, overflow = 0;
	if (nav->count <= 0) {
		nav->visibleStart = nav->visibleCount = 0;
		nav->overflow = 0;
		return;
	}
	moreW = nav_more_w(nav, ui);
	if (moreW > avail) moreW = avail;
	/* 单趟: 总量与前缀可容纳数一起算; 全部放下则 visible = count.
	 * (命中 "更多" 预留时在首个放不下的项处停止, 语义同原两趟实现) */
	for (i = 0; i < nav->count; i++) {
		float add = nav_root_item_w(nav, ui, &nav->items[i]) +
			(i ? nav->style.gap : 0.0f);
		total += add;
		if (overflow) continue;
		if (cursor + add + NAV_MORE_GAP + moreW > avail)
			overflow = 1;
		else {
			cursor += add;
			visible++;
		}
	}
	if (total <= avail + 0.01f) {
		visible = nav->count;
		overflow = 0;
	}
	nav->visibleStart = 0;
	nav->visibleCount = visible;
	nav->overflow = overflow;
}

int UiNavBar_Draw(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	UiRect rc, int enabled)
{
	UiTheme *t;
	UiNavStyle *s;
	UiRect content, row;
	float y, h;
	int i, first;

	if (!nav || !ui || !ui->gfx || !ui->theme) return nav ? nav->changed : 0;
	if (!nav->items || nav->count <= 0) {
		nav->visibleCount = nav->overflow = 0;
		return nav->changed;
	}
	if (rc.w <= 0.0f) return nav->changed;
	t = ui->theme;
	s = &nav->style;
	if (s->barHeight <= 0.0f) s->barHeight = 42.0f;
	if (rc.h <= 0.0f) rc.h = s->barHeight;
	nav->rc = rc;
	if (pm && UiPopupManager_IsOpen(pm, nav->id) && nav->openDepth <= 0)
		UiPopupManager_Close(pm, ui);
	if (!pm || !UiPopupManager_IsOpen(pm, nav->id)) {
		nav->openDepth = 0;
		nav->openOverflow = 0;
	}
	nav->rootAnchor = nav_empty();

	Gfx_FillRoundRect(ui->gfx, rc, s->radius,
		nav_col(s->barBg, t->card));
	Gfx_StrokeRoundRect(ui->gfx, rc, s->radius,
		nav_col(s->barBorder, t->border), 1.0f);

	if (nav->layout == UI_NAV_VERTICAL) {
		float avail, used = 0.0f, moreH = s->itemH;
		int visible = 0;
		content = Ui_Rect(rc.x + 4.0f, rc.y + 3.0f,
			nav_maxf(0.0f, rc.w - 8.0f), nav_maxf(0.0f, rc.h - 6.0f));
		avail = content.h;
		for (i = 0; i < nav->count; i++)
			used += (nav_is_divider(&nav->items[i]) ? NAV_DIV_H : s->itemH) +
				(i ? s->gap : 0.0f);
		if (used <= avail + 0.01f) {
			visible = nav->count;
		} else {
			float cursor = 0.0f;
			for (i = 0; i < nav->count; i++) {
				float add = (nav_is_divider(&nav->items[i])
					? NAV_DIV_H : s->itemH) + (i ? s->gap : 0.0f);
				if (cursor + add + s->gap + moreH > avail + 0.01f)
					break;
				cursor += add;
				visible++;
			}
			if (visible <= 0) visible = 1;
		}
		nav->visibleStart = 0;
		nav->visibleCount = visible;
		nav->overflow = visible < nav->count;
		y = 0.0f;
		first = 1;
		for (i = 0; i < visible; i++) {
			const UiNavItem *it = &nav->items[i];
			h = nav_is_divider(it) ? NAV_DIV_H : s->itemH;
			if (!first) y += s->gap;
			row = Ui_Rect(content.x + (nav->rail ? 2.0f : 4.0f),
				content.y + y,
				nav_maxf(0.0f, content.w - (nav->rail ? 4.0f : 8.0f)),
				h);
			nav_draw_root_item(nav, pm, ui, it, row, enabled, 1);
			y += h;
			first = 0;
		}
		if (nav->overflow && visible < nav->count) {
			nav->moreRc = Ui_Rect(content.x + (nav->rail ? 2.0f : 4.0f),
				content.y + content.h - moreH,
				nav_maxf(0.0f, content.w - (nav->rail ? 4.0f : 8.0f)),
				moreH);
			nav_draw_more(nav, pm, ui, nav->moreRc, enabled);
		} else {
			nav->moreRc = nav_empty();
		}
		return nav->changed;
	}

	content = Ui_Rect(rc.x + NAV_PAD_EDGE, rc.y + 3.0f,
		rc.w - NAV_PAD_EDGE * 2.0f, rc.h - 6.0f);
	nav_horizontal_measure(nav, ui, content);
	h = s->itemH;
	if (h > content.h) h = content.h;
	y = content.y + (content.h - h) * 0.5f;
	{
		float x = content.x;
		float moreW = nav_more_w(nav, ui);
		if (moreW > content.w) moreW = content.w;
		for (i = 0; i < nav->visibleCount; i++) {
			float w = nav_root_item_w(nav, ui, &nav->items[i]);
			row = Ui_Rect(x, y, w, h);
			nav_draw_root_item(nav, pm, ui, &nav->items[i], row,
				enabled, 0);
			x += w + s->gap;
		}
		if (nav->overflow) {
			float mx = x;
			if (mx + moreW > content.x + content.w)
				mx = content.x + content.w - moreW;
			if (mx < content.x) mx = content.x;
			nav->moreRc = Ui_Rect(mx, y, moreW, h);
			nav_draw_more(nav, pm, ui, nav->moreRc, enabled);
		} else {
			nav->moreRc = nav_empty();
		}
	}
	return nav->changed;
}

void UiNavBar_DrawPopups(UiNavBar *nav, UiPopupManager *pm, UiContext *ui)
{
	UiRect anchor, next, panel;
	const UiNavItem *items;
	int count, start, visible, more, level, prefixDepth;
	UiId prefix[UI_NAV_MAX_DEPTH];
	float w, h, maxH;

	if (!nav || !pm || !ui || !ui->gfx || !ui->theme) return;
	nav_clear_panel_rects_from(nav, nav->openDepth);
	if (!UiPopupManager_IsOpen(pm, nav->id) || nav->openDepth <= 0) {
		nav->openDepth = 0;
		nav->openOverflow = 0;
		nav_clear_panel_rects_from(nav, 0);
		return;
	}
	if (nav->openOverflow && !nav->overflow) {
		nav_close_session(nav, pm, ui, 0);
		return;
	}
	anchor = nav->openOverflow ? nav->moreRc : nav->rootAnchor;
	if (anchor.w <= 0.0f || anchor.h <= 0.0f) {
		nav_close_session(nav, pm, ui, 0);
		return;
	}
	UiPopupManager_BeginDraw(pm, ui);
	for (level = 0; level < nav->openDepth; level++) {
		UiRect placeAnchor;
		if (!nav_panel_source(nav, level, &items, &count, &start))
			break;
		maxH = nav_panel_max_h(nav, ui);
		if (!nav_panel_fit(nav, items, count, start, maxH,
			&visible, &more, &h) || visible <= 0)
			break;
		prefixDepth = nav_panel_prefix(nav, level, prefix);
		w = nav_panel_w(nav, ui, items, count);
		nav->panelStart[level] = start;
		nav->panelVisible[level] = visible;
		nav->panelMore[level] = more;
		placeAnchor = anchor;
		if (level > 0) {
			/* 子面板以父面板外缘为锚: 向右展开不压住父面板右边框,
			 * 翻到左侧时也不会盖住父面板. */
			placeAnchor = Ui_Rect(nav->panelRc[level - 1].x,
				anchor.y, nav->panelRc[level - 1].w, anchor.h);
		} else if (nav->layout == UI_NAV_HORIZONTAL && nav->rc.h > 0.0f &&
			anchor.h > 0.0f) {
			/* 顶级面板从导航栏底边起算, 不压住导航栏. */
			placeAnchor = Ui_Rect(anchor.x,
				nav->rc.y + nav->rc.h, anchor.w, 0.0f);
		}
		panel = UiPopup_Place(ui, placeAnchor, w, h,
			(level == 0 && nav->layout == UI_NAV_HORIZONTAL)
				? UI_POPUP_BELOW : UI_POPUP_RIGHT,
			UI_POPUP_ALIGN_START, 4.0f);
		nav->panelRc[level] = panel;
		if (!nav_draw_panel(nav, pm, ui, level, panel, items, start, visible,
			more, prefix, prefixDepth, &next))
			break;
		if (level + 1 < nav->openDepth) {
			if (next.w <= 0.0f || next.h <= 0.0f) {
				nav->openDepth = level + 1;
				nav->keyDepth = nav->openDepth;
				break;
			}
			anchor = next;
		}
	}
	if (level > 0) {
		/* 关闭时按所有已绘制面板的并集做一次淡出. */
		UiRect vr = nav->panelRc[0];
		int i;
		for (i = 1; i < level; i++) {
			float x0 = vr.x < nav->panelRc[i].x ? vr.x : nav->panelRc[i].x;
			float y0 = vr.y < nav->panelRc[i].y ? vr.y : nav->panelRc[i].y;
			float x1 = (vr.x + vr.w) > (nav->panelRc[i].x + nav->panelRc[i].w)
				? (vr.x + vr.w) : (nav->panelRc[i].x + nav->panelRc[i].w);
			float y1 = (vr.y + vr.h) > (nav->panelRc[i].y + nav->panelRc[i].h)
				? (vr.y + vr.h) : (nav->panelRc[i].y + nav->panelRc[i].h);
			vr = Ui_Rect(x0, y0, x1 - x0, y1 - y0);
		}
		UiPopupManager_SetVisualRect(pm, vr);
	}
	if (level < nav->openDepth) {
		nav->openDepth = level;
		nav->keyDepth = nav->keyDepth > level ? level : nav->keyDepth;
	}
	if (nav->openDepth <= 0 && UiPopupManager_IsOpen(pm, nav->id))
		nav_close_session(nav, pm, ui, 0);
	nav_clear_panel_rects_from(nav, nav->openDepth);
	UiPopupManager_EndDraw(pm, ui);
}

/* ── 键盘 ───────────────────────────────────────────────────────────── */

static int nav_root_nav_count(const UiNavBar *nav)
{
	int n = nav ? nav->visibleCount : 0;
	if (nav && nav->overflow) n++;
	return n;
}

static UiId nav_root_nav_id(const UiNavBar *nav, int idx)
{
	if (!nav || idx < 0) return UI_ID_NONE;
	if (idx < nav->visibleCount) return nav->items[idx].id;
	if (nav->overflow && idx == nav->visibleCount) return nav->moreId;
	return UI_ID_NONE;
}

static int nav_root_nav_index(const UiNavBar *nav, UiId id)
{
	int i;
	if (!nav || !id) return -1;
	if (id == nav->moreId && nav->overflow) return nav->visibleCount;
	for (i = 0; i < nav->visibleCount; i++)
		if (nav->items[i].id == id) return i;
	return -1;
}

static int nav_root_key_move(UiNavBar *nav, int dir)
{
	int n, start, i;
	if (!nav) return 0;
	n = nav_root_nav_count(nav);
	if (n <= 0) return 0;
	start = nav_root_nav_index(nav, nav->keyRoot);
	if (start < 0) start = dir > 0 ? -1 : 0;
	for (i = 1; i <= n; i++) {
		int idx = (start + dir * i) % n;
		UiId id = nav_root_nav_id(nav, idx);
		if (id && id != nav->moreId) {
			const UiNavItem *it = nav_find_global(nav, id);
			if (!nav_item_enabled(it)) continue;
		}
		nav->keyRoot = id;
		nav->keyFocusVisible = 1;
		return 1;
	}
	return 0;
}

static int nav_root_key_home(UiNavBar *nav, int end)
{
	int n, i;
	if (!nav) return 0;
	n = nav_root_nav_count(nav);
	if (n <= 0) return 0;
	for (i = 0; i < n; i++) {
		int idx = end ? n - 1 - i : i;
		UiId id = nav_root_nav_id(nav, idx);
		if (id && id != nav->moreId) {
			const UiNavItem *it = nav_find_global(nav, id);
			if (!nav_item_enabled(it)) continue;
		}
		nav->keyRoot = id;
		nav->keyFocusVisible = 1;
		return 1;
	}
	return 0;
}

static int nav_panel_view(const UiNavBar *nav, int level, int count,
	int *start, int *visible, int *more)
{
	int s, v;
	if (!nav || level < 0 || level >= UI_NAV_MAX_DEPTH) return 0;
	s = nav->panelStart[level];
	v = nav->panelVisible[level];
	if (s < 0 || s >= count) return 0;
	if (v <= 0 || v > count - s) v = count - s;
	*start = s;
	*visible = v;
	*more = nav->panelMore[level] && s + v < count;
	return 1;
}

static int nav_panel_key_move(UiNavBar *nav, int level, int dir)
{
	const UiNavItem *items;
	int count, start, visible, more, n, pos, i;
	UiId moreId;
	if (!nav || level < 0 || level >= nav->openDepth) return 0;
	if (!nav_panel_source(nav, level, &items, &count, &start)) return 0;
	if (!nav_panel_view(nav, level, count, &start, &visible, &more)) return 0;
	moreId = nav_panel_more_id(nav, level);
	n = visible + more;
	if (n <= 0) return 0;
	pos = -1;
	if (moreId != UI_ID_NONE && nav->keyItem[level] == moreId) {
		pos = visible;
	} else {
		int idx = nav_find_index(items, count, nav->keyItem[level]);
		if (idx >= start && idx < start + visible) pos = idx - start;
	}
	if (pos < 0) pos = dir > 0 ? -1 : 0;
	for (i = 1; i <= n; i++) {
		int p = (pos + dir * i) % n;
		if (p < 0) p += n;
		if (p >= visible) {
			nav->keyItem[level] = moreId;
			nav->keyFocusVisible = 1;
			return 1;
		}
		if (!nav_item_enabled(&items[start + p])) continue;
		nav->keyItem[level] = items[start + p].id;
		nav->keyFocusVisible = 1;
		return 1;
	}
	return 0;
}

static int nav_panel_key_edge(UiNavBar *nav, int level, int end)
{
	const UiNavItem *items;
	int count, start, visible, more, i;
	if (!nav || level < 0 || level >= nav->openDepth) return 0;
	if (!nav_panel_source(nav, level, &items, &count, &start)) return 0;
	if (!nav_panel_view(nav, level, count, &start, &visible, &more)) return 0;
	if (end && more) {
		nav->keyItem[level] = nav_panel_more_id(nav, level);
		nav->keyFocusVisible = 1;
		return 1;
	}
	for (i = 0; i < visible; i++) {
		int idx = start + (end ? visible - 1 - i : i);
		if (!nav_item_enabled(&items[idx])) continue;
		nav->keyItem[level] = items[idx].id;
		nav->keyFocusVisible = 1;
		return 1;
	}
	return 0;
}

static int nav_panel_activate_key(UiNavBar *nav, UiPopupManager *pm, UiContext *ui)
{
	const UiNavItem *items;
	const UiNavItem *it;
	UiId prefix[UI_NAV_MAX_DEPTH];
	int count, depth, level, idx, start, visible, more;
	if (!nav || !pm || !ui) return 0;
	level = nav->openDepth - 1;
	if (level < 0) return 0;
	if (!nav_panel_source(nav, level, &items, &count, &start)) return 0;
	if (!nav_panel_view(nav, level, count, &start, &visible, &more)) return 0;
	if (more && nav->keyItem[level] == nav_panel_more_id(nav, level))
		return nav_open_tail(nav, pm, ui, level, 1);
	idx = nav_find_index(items, count, nav->keyItem[level]);
	if (idx < start || idx >= start + visible) return 0;
	it = &items[idx];
	if (!nav_item_enabled(it)) return 0;
	depth = nav_panel_prefix(nav, level, prefix);
	nav_row_activate(nav, pm, ui, level, it, prefix, depth, 1);
	return 1;
}

int UiNavBar_Key(UiNavBar *nav, UiPopupManager *pm, UiContext *ui, UINT vk)
{
	if (!nav) return 0;
	if (pm && UiPopupManager_AnyOpen(pm) &&
		!UiPopupManager_IsOpen(pm, nav->id)) return 0;
	if (pm && UiPopupManager_IsOpen(pm, nav->id) && nav->openDepth > 0) {
		int level = nav->openDepth - 1;
		switch (vk) {
		case VK_ESCAPE:
		case VK_LEFT:
			if (nav->openDepth > 1) {
				int closing = nav->openDepth - 1;
				int tail = nav->openTail[closing];
				nav_clear_panel_rects_from(nav, closing);
				nav->openDepth = closing;
				nav->keyDepth = closing;
				nav->keyItem[closing] = UI_ID_NONE;
				nav->keyItem[closing - 1] = tail
					? nav_panel_more_id(nav, closing - 1)
					: nav->openPath[closing];
				nav->keyFocusVisible = 1;
			} else {
				UiId root = nav->openOverflow ? nav->moreId : nav->openPath[0];
				nav_close_session(nav, pm, ui, 1);
				nav->keyRoot = root;
				nav->keyFocusVisible = 1;
			}
			return 1;
		case VK_UP:
			return nav_panel_key_move(nav, level, -1);
		case VK_DOWN:
			return nav_panel_key_move(nav, level, 1);
		case VK_HOME:
			return nav_panel_key_edge(nav, level, 0);
		case VK_END:
			return nav_panel_key_edge(nav, level, 1);
		case VK_RETURN:
		case VK_SPACE:
			return nav_panel_activate_key(nav, pm, ui);
		}
		return 0;
	}
	switch (vk) {
	case VK_LEFT:
		return nav_root_key_move(nav, -1);
	case VK_RIGHT:
		return nav_root_key_move(nav, 1);
	case VK_UP:
		return nav->layout == UI_NAV_VERTICAL ? nav_root_key_move(nav, -1) : 0;
	case VK_DOWN:
		return nav->layout == UI_NAV_VERTICAL ? nav_root_key_move(nav, 1) : 0;
	case VK_HOME:
		return nav_root_key_home(nav, 0);
	case VK_END:
		return nav_root_key_home(nav, 1);
	case VK_RETURN:
	case VK_SPACE:
		if (nav->keyRoot == nav->moreId) {
			nav_open_overflow(nav, pm, ui,
				nav->moreRc.w > 0.0f ? nav->moreRc : nav->rc, 1);
			return 1;
		}
		if (!nav->keyRoot) {
			int n = nav_root_nav_count(nav);
			if (n <= 0) return 0;
			nav->keyRoot = nav_root_nav_id(nav, 0);
		}
		{
			const UiNavItem *it = nav_find_global(nav, nav->keyRoot);
			if (it) {
				nav_activate_root(nav, pm, ui, it,
					nav->rootAnchor.w > 0.0f ? nav->rootAnchor
						: nav->rc);
				return 1;
			}
		}
		return 0;
	}
	return 0;
}

void UiNavBar_Shutdown(UiNavBar *nav)
{
	if (nav) ZeroMemory(nav, sizeof(*nav));
}
