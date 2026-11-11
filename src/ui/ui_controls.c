#include "ui/ui_controls.h"

static UINT ctrl_bit(int i)
{
	return (i >= 0 && i < 32) ? (1u << i) : 0u;
}

static UiId ctrl_child(UiId id, const char *scope, int i)
{
	return Ui_IdIndex(scope, (int)(uint32_t)(id >> 32),
		(int)(uint32_t)id, i);
}

static float ctrl_clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static void ctrl_focus_bg(UiContext *ui, UiRect rc, int focus)
{
	if (!focus) return;
	Gfx_FillRoundRect(ui->gfx, Ui_RectInset(rc, 2.0f), 6.0f,
		Ui_ColorAlpha(ui->theme->primary.base, 26));
}

/* 状态 0..1 平滑值: 首次绘制直接取目标, 切换时淡入淡出. */
static float ctrl_state_t(UiContext *ui, UiId id, int ch, int on)
{
	Ui_AnimSmooth(ui, id, ch, on ? 1.0f : 0.0f);
	return Ui_AnimGet(ui, id, ch, on ? 1.0f : 0.0f);
}

static void draw_check_box(UiContext *ui, UiRect rc, int enabled,
	float hover, int focused, float checkT, float minusT)
{
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	UiColor base, border, fill, ink;
	float fillT;
	int checkA, minusA, fillA;

	if (checkT < 0.0f) checkT = 0.0f;
	if (checkT > 1.0f) checkT = 1.0f;
	if (minusT < 0.0f) minusT = 0.0f;
	if (minusT > 1.0f) minusT = 1.0f;
	fillT = checkT > minusT ? checkT : minusT;
	checkA = (int)(checkT * 255.0f + 0.5f);
	minusA = (int)(minusT * 255.0f + 0.5f);
	fillA = (int)(fillT * 255.0f + 0.5f);

	base = enabled ? t->neutral.base : t->neutral.soft;
	Gfx_FillRoundRect(g, rc, 4.0f, base);

	if (enabled && fillA > 0) {
		/* 选中底: 在未选中底色上淡入; 取消选中时用残留 alpha 淡出. */
		fill = Ui_ColorMix(t->primary.base, t->primary.hover, hover * 0.25f);
		fill = Ui_ColorAlpha(fill, fillA);
		Gfx_FillRoundRect(g, rc, 4.0f, fill);
	}

	if (!enabled) {
		border = t->neutral.border;
	} else if (focused) {
		border = t->primary.base;
	} else if (fillT > 0.001f) {
		border = Ui_ColorMix(t->neutral.border, t->primary.base, fillT);
	} else {
		border = hover > 0.004f
			? Ui_ColorMix(t->neutral.border, t->primary.base, hover * 0.6f)
			: t->neutral.border;
	}
	Gfx_StrokeRoundRect(g, rc, 4.0f, border, 1.0f);

	/* 对勾/横线分别淡入淡出: indeterminate -> checked 时横线退、对勾进. */
	if (minusA > 0) {
		ink = enabled ? Ui_ColorAlpha(0xFFFFFFFFu, minusA)
			: t->btnDisabledText;
		Gfx_FillRoundRect(g, Ui_Rect(rc.x + 4.5f, rc.y + 8.0f,
			rc.w - 9.0f, 2.0f), 1.0f, ink);
	}
	if (checkA > 0) {
		float x = rc.x, y = rc.y;
		ink = enabled ? Ui_ColorAlpha(0xFFFFFFFFu, checkA)
			: t->btnDisabledText;
		Gfx_Line(g, x + 4.5f, y + 9.0f, x + 7.5f, y + 12.5f, ink, 2.0f);
		Gfx_Line(g, x + 7.5f, y + 12.5f, x + 13.5f, y + 5.5f, ink, 2.0f);
	}
}

static void draw_radio_dot(UiContext *ui, UiRect rc, int selected,
	int enabled, float hover, int focused, float on)
{
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	float cx = rc.x + rc.w * 0.5f, cy = rc.y + rc.h * 0.5f, r = 8.0f;
	UiColor border, dot;
	int alpha;

	if (on < 0.0f) on = 0.0f;
	if (on > 1.0f) on = 1.0f;
	alpha = (int)(on * 255.0f + 0.5f);

	if (!enabled) border = t->neutral.border;
	else if (focused) border = t->primary.base;
	else if (on > 0.001f)
		border = Ui_ColorMix(t->neutral.border, t->primary.base, on);
	else border = hover > 0.004f
		? Ui_ColorMix(t->neutral.border, t->primary.base, hover * 0.6f)
		: t->neutral.border;
	Gfx_FillEllipse(g, cx, cy, r, r, enabled ? t->neutral.base : t->neutral.soft);
	Gfx_StrokeEllipse(g, cx, cy, r, border, 1.0f);
	if (enabled && alpha > 0) {
		dot = Ui_ColorAlpha(t->primary.base, alpha);
		Gfx_FillEllipse(g, cx, cy, 4.0f, 4.0f, dot);
	} else if (!enabled && selected) {
		Gfx_FillEllipse(g, cx, cy, 4.0f, 4.0f, t->btnDisabledText);
	}
}

static int ctrl_item_enabled(const unsigned *mask, int i, int enabled)
{
	if (!enabled || i < 0) return 0;
	if (mask && (*mask & ctrl_bit(i))) return 0;
	return 1;
}

/* ── Checkbox ───────────────────────────────────────────────────────── */

void UiCheckbox_Init(UiCheckbox *c, UiId id, int checked)
{
	if (!c) return;
	ZeroMemory(c, sizeof(*c));
	c->id = id;
	c->checked = checked != 0;
}

int Ui_Checkbox(UiCheckbox *c, UiContext *ui, UiRect rc,
	const char *label, int enabled)
{
	UiTheme *t;
	UiWidgetState st;
	UiRect box;
	UiColor col;
	float checkT, minusT;
	int focus;
	if (!c || !ui || !ui->theme || !ui->gfx) return 0;
	t = ui->theme;
	c->changed = 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	box = Ui_Rect(rc.x, rc.y + (rc.h - 18.0f) * 0.5f, 18.0f, 18.0f);
	st = Ui_State(ui, c->id, rc, enabled);
	if (ui->clicked == c->id && enabled) {
		if (c->indeterminate) {
			c->indeterminate = 0;
			c->checked = 1;
		} else {
			c->checked = !c->checked;
		}
		c->changed = 1;
		ui->navId = c->id;
	}
	if (st.hover > 0.004f && enabled)
		Gfx_FillRoundRect(ui->gfx, Ui_RectInset(rc, 2.0f), 6.0f,
			Ui_ColorAlpha(t->primary.base, (int)(st.hover * 16.0f)));
	/* indeterminate 优先于 checked: 两者同时为真时只画横线; 点击后
	 * indeterminate 清 0, 对勾再从 0 淡入. */
	checkT = ctrl_state_t(ui, c->id, UI_AN_USER,
		c->checked && !c->indeterminate);
	minusT = ctrl_state_t(ui, c->id, UI_AN_USER + 1, c->indeterminate);
	focus = ui->navFocusVisible && ui->navId == c->id;
	ctrl_focus_bg(ui, rc, focus);
	draw_check_box(ui, box, enabled, st.hover, focus, checkT, minusT);
	if (label && label[0]) {
		col = enabled ? (st.hover > 0.004f
			? Ui_ColorMix(t->text2, t->text, st.hover) : t->text2)
			: t->btnDisabledText;
		Gfx_Text(ui->gfx, t->fBody, label,
			Ui_Rect(box.x + 26.0f, rc.y, rc.w - 26.0f, rc.h), col,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	return c->changed;
}

int UiCheckbox_Key(UiCheckbox *c, UiContext *ui, UINT vk)
{
	if (!c || !ui || ui->navId != c->id) return 0;
	ui->navFocusVisible = 1;
	if (vk == VK_SPACE || vk == VK_RETURN) {
		if (c->indeterminate) {
			c->indeterminate = 0;
			c->checked = 1;
		} else {
			c->checked = !c->checked;
		}
		c->changed = 1;
		return 1;
	}
	return 0;
}

/* ── CheckboxGroup ─────────────────────────────────────────────────── */

void UiCheckboxGroup_Init(UiCheckboxGroup *g, UiId id, int count)
{
	if (!g) return;
	ZeroMemory(g, sizeof(*g));
	g->id = id;
	g->count = count > 0 ? count : 0;
	g->focus = g->count > 0 ? 0 : -1;
}

void UiCheckboxGroup_SetMask(UiCheckboxGroup *g, unsigned mask)
{
	if (!g) return;
	if (g->count < 32) mask &= (1u << g->count) - 1u;
	g->mask = mask;
}

void UiCheckboxGroup_SetDisabledMask(UiCheckboxGroup *g, unsigned mask)
{
	if (!g) return;
	if (g->count < 32) mask &= (1u << g->count) - 1u;
	g->disabledMask = mask;
}

static int group_find(const UiCheckboxGroup *g, int from, int dir)
{
	int i;
	for (i = 0; i < g->count; i++) {
		int idx = (from + dir * i) % g->count;
		if (idx < 0) idx += g->count;
		if (ctrl_item_enabled(&g->disabledMask, idx, 1)) return idx;
	}
	return -1;
}

int UiCheckboxGroup_Draw(UiCheckboxGroup *g, UiContext *ui, UiRect rc,
	const char *const *labels, float rowH, int enabled)
{
	UiTheme *t;
	int i, clicked = 0, hitClip;
	if (!g || !ui || !ui->theme || !ui->gfx || !labels || g->count <= 0) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	t = ui->theme;
	g->changed = 0;
	if (rowH <= 0.0f) rowH = 28.0f;
	/* 行内复选框描边中心在 rc.x 上, 左半像素/AA 会被 rc 边界裁掉,
	 * 与上方未裁剪的 Ui_Checkbox 比会少 1px. 绘制裁剪左右各放宽 1 DIP;
	 * 命中裁剪仍用 rc, 不影响点击范围. */
	Gfx_PushClip(ui->gfx,
		Ui_Rect(rc.x - 1.0f, rc.y, rc.w + 2.0f, rc.h));
	hitClip = Ui_PushClipRect(ui, rc);
	for (i = 0; i < g->count; i++) {
		UiRect row = Ui_Rect(rc.x, rc.y + (float)i * rowH, rc.w, rowH);
		UiId id = ctrl_child(g->id, "ui.ctrl.chk", i);
		UiWidgetState st;
		int en = ctrl_item_enabled(&g->disabledMask, i, enabled);
		UiRect box = Ui_Rect(row.x, row.y + (row.h - 18.0f) * 0.5f, 18, 18);
		UiColor col;
		float checkT;
		int focus;
		if (row.y >= rc.y + rc.h) break;
		st = Ui_State(ui, id, row, en);
		if (ui->clicked == id && en) {
			g->mask ^= ctrl_bit(i);
			g->focus = i;
			ui->navId = g->id;
			g->changed = 1;
			clicked = 1;
		}
		checkT = ctrl_state_t(ui, id, UI_AN_USER,
			(g->mask & ctrl_bit(i)) != 0);
		if (st.hover > 0.004f && en)
			Gfx_FillRoundRect(ui->gfx, Ui_RectInset(row, 2.0f), 6.0f,
				Ui_ColorAlpha(t->primary.base, (int)(st.hover * 16.0f)));
		focus = ui->navFocusVisible && ui->navId == g->id && g->focus == i;
		ctrl_focus_bg(ui, row, focus);
		draw_check_box(ui, box, en, st.hover, focus, checkT, 0.0f);
		col = en ? t->text2 : t->btnDisabledText;
		if (en && st.hover > 0.004f) col = Ui_ColorMix(col, t->text, st.hover);
		Gfx_Text(ui->gfx, t->fBody, labels[i],
			Ui_Rect(box.x + 26.0f, row.y, row.w - 26.0f, row.h), col,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	if (hitClip) Ui_PopClipRect(ui);
	Gfx_PopClip(ui->gfx);
	return clicked;
}

int UiCheckboxGroup_Key(UiCheckboxGroup *g, UiContext *ui, UINT vk)
{
	int idx;
	if (!g || !ui || g->count <= 0 || ui->navId != g->id) return 0;
	ui->navFocusVisible = 1;
	if (g->focus < 0 || g->focus >= g->count) g->focus = 0;
	switch (vk) {
	case VK_UP: case VK_LEFT:
		idx = group_find(g, (g->focus + g->count - 1) % g->count, -1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_DOWN: case VK_RIGHT:
		idx = group_find(g, (g->focus + 1) % g->count, 1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_HOME:
		idx = group_find(g, 0, 1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_END:
		idx = group_find(g, g->count - 1, -1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_SPACE: case VK_RETURN:
		if (ctrl_item_enabled(&g->disabledMask, g->focus, 1)) {
			g->mask ^= ctrl_bit(g->focus);
			g->changed = 1;
		}
		return 1;
	}
	return 0;
}

/* ── RadioGroup ────────────────────────────────────────────────────── */

void UiRadioGroup_Init(UiRadioGroup *g, UiId id, int count)
{
	if (!g) return;
	ZeroMemory(g, sizeof(*g));
	g->id = id;
	g->count = count > 0 ? count : 0;
	g->selected = g->count > 0 ? 0 : -1;
	g->focus = g->selected;
}

void UiRadioGroup_SetSelected(UiRadioGroup *g, int index)
{
	if (!g) return;
	if (index < 0) index = -1;
	if (index >= g->count) index = g->count - 1;
	g->selected = index;
	if (index >= 0) g->focus = index;
}

void UiRadioGroup_SetDisabledMask(UiRadioGroup *g, unsigned mask)
{
	if (!g) return;
	if (g->count < 32) mask &= (1u << g->count) - 1u;
	g->disabledMask = mask;
}

static int radio_find(const UiRadioGroup *g, int from, int dir)
{
	int i;
	for (i = 0; i < g->count; i++) {
		int idx = (from + dir * i) % g->count;
		if (idx < 0) idx += g->count;
		if (ctrl_item_enabled(&g->disabledMask, idx, 1)) return idx;
	}
	return -1;
}

int UiRadioGroup_Draw(UiRadioGroup *g, UiContext *ui, UiRect rc,
	const char *const *labels, float rowH, int enabled)
{
	UiTheme *t;
	int i, hitClip;
	if (!g || !ui || !ui->theme || !ui->gfx || !labels || g->count <= 0) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	t = ui->theme;
	g->changed = 0;
	if (rowH <= 0.0f) rowH = 28.0f;
	/* 同 CheckboxGroup: 放宽绘制裁剪, 保住左边描边/AA. */
	Gfx_PushClip(ui->gfx,
		Ui_Rect(rc.x - 1.0f, rc.y, rc.w + 2.0f, rc.h));
	hitClip = Ui_PushClipRect(ui, rc);
	for (i = 0; i < g->count; i++) {
		UiRect row = Ui_Rect(rc.x, rc.y + (float)i * rowH, rc.w, rowH);
		UiId id = ctrl_child(g->id, "ui.ctrl.rad", i);
		UiWidgetState st;
		int en = ctrl_item_enabled(&g->disabledMask, i, enabled);
		UiRect dot = Ui_Rect(row.x, row.y + (row.h - 18.0f) * 0.5f, 18, 18);
		UiColor col;
		float onT;
		int focus;
		if (row.y >= rc.y + rc.h) break;
		st = Ui_State(ui, id, row, en);
		if (ui->clicked == id && en) {
			g->selected = i;
			g->focus = i;
			ui->navId = g->id;
			g->changed = 1;
		}
		onT = ctrl_state_t(ui, id, UI_AN_USER, g->selected == i);
		if (st.hover > 0.004f && en)
			Gfx_FillRoundRect(ui->gfx, Ui_RectInset(row, 2.0f), 6.0f,
				Ui_ColorAlpha(t->primary.base, (int)(st.hover * 16.0f)));
		focus = ui->navFocusVisible && ui->navId == g->id && g->focus == i;
		ctrl_focus_bg(ui, row, focus);
		draw_radio_dot(ui, dot, g->selected == i, en, st.hover, focus, onT);
		col = en ? t->text2 : t->btnDisabledText;
		if (en && st.hover > 0.004f) col = Ui_ColorMix(col, t->text, st.hover);
		Gfx_Text(ui->gfx, t->fBody, labels[i],
			Ui_Rect(dot.x + 26.0f, row.y, row.w - 26.0f, row.h), col,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	if (hitClip) Ui_PopClipRect(ui);
	Gfx_PopClip(ui->gfx);
	return g->changed;
}

int UiRadioGroup_Key(UiRadioGroup *g, UiContext *ui, UINT vk)
{
	int idx;
	if (!g || !ui || g->count <= 0 || ui->navId != g->id) return 0;
	ui->navFocusVisible = 1;
	if (g->focus < 0 || g->focus >= g->count) g->focus = g->selected >= 0 ? g->selected : 0;
	switch (vk) {
	case VK_UP: case VK_LEFT:
		idx = radio_find(g, (g->focus + g->count - 1) % g->count, -1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_DOWN: case VK_RIGHT:
		idx = radio_find(g, (g->focus + 1) % g->count, 1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_HOME:
		idx = radio_find(g, 0, 1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_END:
		idx = radio_find(g, g->count - 1, -1);
		if (idx >= 0) g->focus = idx;
		return 1;
	case VK_SPACE: case VK_RETURN:
		if (ctrl_item_enabled(&g->disabledMask, g->focus, 1)) {
			g->selected = g->focus;
			g->changed = 1;
		}
		return 1;
	}
	return 0;
}

/* ── Switch ────────────────────────────────────────────────────────── */

void UiSwitch_Init(UiSwitch *s, UiId id, int on)
{
	if (!s) return;
	ZeroMemory(s, sizeof(*s));
	s->id = id;
	s->on = on != 0;
}

int Ui_Switch(UiSwitch *s, UiContext *ui, UiRect rc,
	const char *label, int enabled)
{
	UiTheme *t;
	UiWidgetState st;
	UiRect track;
	UiColor trackCol, thumb;
	float pos;
	int focus;
	if (!s || !ui || !ui->theme || !ui->gfx) return 0;
	t = ui->theme;
	s->changed = 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	track = Ui_Rect(rc.x, rc.y + (rc.h - 20.0f) * 0.5f, 38.0f, 20.0f);
	st = Ui_State(ui, s->id, rc, enabled);
	if (ui->clicked == s->id && enabled) {
		s->on = !s->on;
		s->changed = 1;
		ui->navId = s->id;
	}
	focus = ui->navFocusVisible && ui->navId == s->id;
	ctrl_focus_bg(ui, rc, focus);
	Ui_AnimSmooth(ui, s->id, UI_AN_USER, s->on ? 1.0f : 0.0f);
	pos = Ui_AnimGet(ui, s->id, UI_AN_USER, s->on ? 1.0f : 0.0f);
	trackCol = enabled ? Ui_ColorMix(t->switchOff, t->switchOn, pos)
		: t->neutral.soft;
	if (st.hover > 0.004f && enabled)
		trackCol = Ui_ColorMix(trackCol, t->primary.hover, st.hover * 0.12f);
	Gfx_FillRoundRect(ui->gfx, track, 10.0f, trackCol);
	thumb = enabled ? t->neutral.base : t->neutral.base;
	Gfx_FillEllipse(ui->gfx, track.x + 10.0f + pos * 18.0f,
		track.y + 10.0f, 7.0f, 7.0f, thumb);
	if (focus)
		Gfx_StrokeRoundRect(ui->gfx, track, 10.0f,
			Ui_ColorAlpha(t->primary.base, 180), 1.0f);
	if (label && label[0]) {
		UiColor col = enabled ? t->text2 : t->btnDisabledText;
		if (enabled && st.hover > 0.004f) col = Ui_ColorMix(col, t->text, st.hover);
		Gfx_Text(ui->gfx, t->fBody, label,
			Ui_Rect(track.x + 50.0f, rc.y, rc.w - 50.0f, rc.h), col,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	return s->changed;
}

int UiSwitch_Key(UiSwitch *s, UiContext *ui, UINT vk)
{
	if (!s || !ui || ui->navId != s->id) return 0;
	ui->navFocusVisible = 1;
	if (vk == VK_SPACE || vk == VK_RETURN) {
		s->on = !s->on;
		s->changed = 1;
		return 1;
	}
	return 0;
}

/* ── Slider ────────────────────────────────────────────────────────── */

void UiSlider_Init(UiSlider *s, UiId id, float value)
{
	if (!s) return;
	ZeroMemory(s, sizeof(*s));
	s->id = id;
	s->value = ctrl_clamp01(value);
}

void UiSlider_SetValue(UiSlider *s, float value)
{
	if (s) s->value = ctrl_clamp01(value);
}

static void slider_set(UiSlider *s, float value)
{
	float v = ctrl_clamp01(value);
	if (v != s->value) {
		s->value = v;
		s->changed = 1;
	}
}

int Ui_Slider(UiSlider *s, UiContext *ui, UiRect rc,
	const char *label, int enabled)
{
	UiTheme *t;
	UiWidgetState st;
	UiRect track, lr, vr;
	float labelW = label && label[0] ? 96.0f : 0.0f;
	float valW = 46.0f;
	float x0, x1, vx;
	char text[16];
	int focus;
	if (!s || !ui || !ui->theme || !ui->gfx) return 0;
	t = ui->theme;
	s->changed = 0;
	s->committed = 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	lr = Ui_Rect(rc.x, rc.y, labelW, rc.h);
	vr = Ui_Rect(rc.x + rc.w - valW, rc.y, valW, rc.h);
	x0 = rc.x + labelW;
	x1 = rc.x + rc.w - valW;
	if (x1 - x0 < 40.0f) x1 = x0 + 40.0f;
	track = Ui_Rect(x0, rc.y + (rc.h - 4.0f) * 0.5f, x1 - x0, 4.0f);
	st = Ui_State(ui, s->id, rc, enabled);
	if (enabled && (st.pressed || (ui->mouseDown && ui->pressed == s->id))) {
		float t0 = (ui->mouseX - track.x) / track.w;
		slider_set(s, t0);
		ui->navId = s->id;
	}
	if (enabled && ui->released == s->id) s->committed = 1;
	focus = ui->navFocusVisible && ui->navId == s->id;
	ctrl_focus_bg(ui, rc, focus);

	Gfx_FillRoundRect(ui->gfx, track, 2.0f,
		enabled ? t->neutral.border : t->neutral.soft);
	Gfx_FillRoundRect(ui->gfx, Ui_Rect(track.x, track.y,
		track.w * s->value, track.h), 2.0f,
		enabled ? t->primary.base : t->btnDisabledText);
	vx = track.x + track.w * s->value;
	Gfx_FillEllipse(ui->gfx, vx, track.y + 2.0f, 7.0f, 7.0f,
		enabled ? t->neutral.base : t->neutral.soft);
	Gfx_StrokeEllipse(ui->gfx, vx, track.y + 2.0f, 7.0f,
		enabled ? t->primary.base : t->neutral.border, 1.5f);
	if (label && label[0])
		Gfx_Text(ui->gfx, t->fBody, label, lr,
			enabled ? t->text2 : t->btnDisabledText, GFX_TEXT_VCENTER);
	wsprintfA(text, "%d%%", (int)(s->value * 100.0f + 0.5f));
	Gfx_Text(ui->gfx, t->fSmall, text, vr,
		enabled ? t->text3 : t->btnDisabledText,
		GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
	return s->changed;
}

int UiSlider_Key(UiSlider *s, UiContext *ui, UINT vk)
{
	float step = 0.05f;
	if (!s || !ui || ui->navId != s->id) return 0;
	ui->navFocusVisible = 1;
	switch (vk) {
	case VK_LEFT: case VK_DOWN:
		s->value = ctrl_clamp01(s->value - step);
		s->changed = 1;
		s->committed = 1;
		return 1;
	case VK_RIGHT: case VK_UP:
		s->value = ctrl_clamp01(s->value + step);
		s->changed = 1;
		s->committed = 1;
		return 1;
	case VK_HOME:
		s->value = 0.0f; s->changed = 1; s->committed = 1; return 1;
	case VK_END:
		s->value = 1.0f; s->changed = 1; s->committed = 1; return 1;
	}
	return 0;
}
