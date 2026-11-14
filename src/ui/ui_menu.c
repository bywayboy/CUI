#include "ui/ui_menu.h"

#define MENU_PAD_X 12.0f
#define MENU_PAD_Y 4.0f
#define MENU_DIV_H 9.0f
#define MENU_MIN_W 160.0f
#define MENU_MAX_W 480.0f
#define MENU_SHORTCUT_GAP 24.0f

static UiId menu_child(UiId id, const char *scope, int i)
{
	return Ui_IdIndex(scope, (int)(uint32_t)(id >> 32),
		(int)(uint32_t)id, i);
}

static float menu_row_h(const UiMenu *m)
{
	return m->rowH > 0.0f ? m->rowH : 30.0f;
}

static int menu_is_divider(const UiMenuItem *it)
{
	return it && (it->flags & UI_MENU_DIVIDER) != 0;
}

static int menu_enabled(const UiMenuItem *it)
{
	return it && !menu_is_divider(it) && (it->flags & UI_MENU_DISABLED) == 0;
}

static int menu_find(const UiMenu *m, int from, int dir)
{
	int i;
	if (!m || m->count <= 0) return -1;
	for (i = 0; i < m->count; i++) {
		int idx = (from + dir * i) % m->count;
		if (idx < 0) idx += m->count;
		if (menu_enabled(&m->items[idx])) return idx;
	}
	return -1;
}

static int menu_first(const UiMenu *m)
{
	return menu_find(m, 0, 1);
}

static int menu_last(const UiMenu *m)
{
	return menu_find(m, m->count - 1, -1);
}

static float menu_height(const UiMenu *m)
{
	float h = MENU_PAD_Y * 2.0f;
	int i;
	for (i = 0; i < m->count; i++)
		h += menu_is_divider(&m->items[i]) ? MENU_DIV_H : menu_row_h(m);
	return h;
}

static void menu_activate(UiMenu *m, UiPopupManager *pm, UiContext *ui,
	int idx, int focusOnClose)
{
	if (!m || !ui || !menu_enabled(&m->items[idx])) return;
	m->item = idx;
	m->focus = idx;
	m->changed = 1;
	if (pm) {
		pm->focusOnClose = focusOnClose != 0;
		UiPopupManager_Close(pm, ui);
	}
}

void UiMenu_Init(UiMenu *m, UiId id)
{
	if (!m) return;
	ZeroMemory(m, sizeof(*m));
	m->id = id;
	m->focus = -1;
	m->item = -1;
	m->rowH = 30.0f;
	m->minW = MENU_MIN_W;
}

void UiMenu_SetItems(UiMenu *m, const UiMenuItem *items, int count)
{
	if (!m) return;
	m->items = items;
	m->count = count > 0 ? count : 0;
	if (m->focus >= m->count) m->focus = m->count - 1;
	if (m->item >= m->count) m->item = m->count - 1;
}

void UiMenu_OpenAt(UiMenu *m, UiPopupManager *pm, UiContext *ui, UiId owner,
	UiRect anchor, int focusOnClose)
{
	if (!m || !pm || !ui || m->count <= 0) return;
	m->anchor = anchor;
	m->focus = menu_first(m);
	m->keyFocus = focusOnClose != 0;
	UiPopupManager_Open(pm, ui, m->id, owner, focusOnClose);
}

void UiMenu_Draw(UiMenu *m, UiPopupManager *pm, UiContext *ui)
{
	UiTheme *t;
	UiRect panel, inner, row, lr, sr;
	float rowH, y, w, lw, sw, need;
	int i, hitClip;
	UiId panelId;

	if (!m || !pm || !ui || !ui->theme || !ui->gfx) return;
	m->changed = 0;
	if (!UiPopupManager_IsOpen(pm, m->id)) return;
	if (m->count <= 0 || !m->items) {
		UiPopupManager_Close(pm, ui);
		return;
	}
	t = ui->theme;
	rowH = menu_row_h(m);

	w = m->minW > 0.0f ? m->minW : MENU_MIN_W;
	for (i = 0; i < m->count; i++) {
		if (menu_is_divider(&m->items[i])) continue;
		lw = Gfx_TextWidth(ui->gfx, t->fBody,
			m->items[i].label ? m->items[i].label : "");
		sw = m->items[i].shortcut
			? Gfx_TextWidth(ui->gfx, t->fSmall, m->items[i].shortcut) : 0.0f;
		need = MENU_PAD_X * 2.0f + lw + (sw > 0.0f ? MENU_SHORTCUT_GAP + sw : 0.0f);
		if (need > w) w = need;
	}
	if (w > MENU_MAX_W) w = MENU_MAX_W;
	if (ui->width > 0.0f && w > ui->width - 16.0f) w = ui->width - 16.0f;

	panel = UiPopup_Place(ui, m->anchor, w, menu_height(m), UI_POPUP_BELOW,
		UI_POPUP_ALIGN_START, 6.0f);
	m->panelRc = panel;

	UiPopupManager_BeginDraw(pm, ui);
	Ui_PushLayer(ui, UI_Z_OVERLAY);
	panelId = menu_child(m->id, "ui.menu.panel", 0);
	Ui_RegisterWidget(ui, panelId, panel);
	Gfx_FillRoundRect(ui->gfx, panel, 8.0f, t->toastBg);
	Gfx_StrokeRoundRect(ui->gfx, panel, 8.0f, t->toastBorder, 1.0f);

	inner = Ui_Rect(panel.x + 3.0f, panel.y + MENU_PAD_Y,
		panel.w - 6.0f, panel.h - MENU_PAD_Y * 2.0f);
	Gfx_PushClip(ui->gfx, inner);
	hitClip = Ui_PushClipRect(ui, inner);

	y = inner.y;
	for (i = 0; i < m->count; i++) {
		const UiMenuItem *it = &m->items[i];
		UiId id;
		UiWidgetState st;
		UiColor col;
		float h = menu_is_divider(it) ? MENU_DIV_H : rowH;
		int en = menu_enabled(it);
		int hot;

		if (menu_is_divider(it)) {
			Gfx_FillRoundRect(ui->gfx,
				Ui_Rect(inner.x + 8.0f, y + 4.0f, inner.w - 16.0f, 1.0f),
				0.5f, t->neutral.border);
			y += h;
			continue;
		}
		id = menu_child(m->id, "ui.menu.item", i);
		row = Ui_Rect(inner.x + 1.0f, y, inner.w - 2.0f, h);
		st = Ui_StateInstant(ui, id, row, en);
		if (en && ui->clicked == id) {
			menu_activate(m, pm, ui, i, 0);
			hot = 0;
			y += h;
			break;
		}
		hot = en && (st.hover > 0.004f ||
			(m->keyFocus && m->focus == i));
		if (hot)
			Gfx_FillRoundRect(ui->gfx, row, 6.0f,
				Ui_ColorAlpha(t->primary.base, 20));
		col = !en ? t->btnDisabledText
			: ((it->flags & UI_MENU_DANGER) ? t->error.base : t->toastText);
		if (en && st.hover > 0.004f && !(it->flags & UI_MENU_DANGER))
			col = Ui_ColorMix(col, t->text, st.hover);
		sr = Ui_Rect(0, 0, 0, 0);
		if (it->shortcut && it->shortcut[0]) {
			sw = Gfx_TextWidth(ui->gfx, t->fSmall, it->shortcut);
			sr = Ui_Rect(row.x + row.w - MENU_PAD_X - sw, row.y,
				sw, row.h);
			Gfx_Text(ui->gfx, t->fSmall, it->shortcut, sr,
				en ? t->text3 : t->btnDisabledText,
				GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
		}
		lr = Ui_Rect(row.x + MENU_PAD_X, row.y,
			row.w - MENU_PAD_X * 2.0f - (sr.w > 0.0f
				? sr.w + MENU_SHORTCUT_GAP : 0.0f), row.h);
		Gfx_Text(ui->gfx, t->fBody, it->label ? it->label : "", lr, col,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		y += h;
		if (!UiPopupManager_IsOpen(pm, m->id)) break;
	}

	if (hitClip) Ui_PopClipRect(ui);
	Gfx_PopClip(ui->gfx);
	Ui_PopLayer(ui);
	UiPopupManager_EndDraw(pm, ui);
}

int UiMenu_Key(UiMenu *m, UiPopupManager *pm, UiContext *ui, UINT vk)
{
	int idx;
	if (!m || !pm || !ui) return 0;
	if (!UiPopupManager_IsOpen(pm, m->id)) return 0;
	if (m->focus < 0 || m->focus >= m->count ||
		!menu_enabled(&m->items[m->focus]))
		m->focus = menu_first(m);
	switch (vk) {
	case VK_UP:
		idx = menu_find(m, m->focus - 1, -1);
		if (idx >= 0) { m->focus = idx; m->keyFocus = 1; }
		return 1;
	case VK_DOWN:
		idx = menu_find(m, m->focus + 1, 1);
		if (idx >= 0) { m->focus = idx; m->keyFocus = 1; }
		return 1;
	case VK_HOME:
		idx = menu_first(m);
		if (idx >= 0) { m->focus = idx; m->keyFocus = 1; }
		return 1;
	case VK_END:
		idx = menu_last(m);
		if (idx >= 0) { m->focus = idx; m->keyFocus = 1; }
		return 1;
	case VK_RETURN:
	case VK_SPACE:
		if (m->focus >= 0) menu_activate(m, pm, ui, m->focus, 1);
		return 1;
	case VK_ESCAPE:
		UiPopupManager_Key(pm, ui, vk);
		return 1;
	}
	return 0;
}
