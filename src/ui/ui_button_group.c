#include "ui/ui_button_group.h"

static UINT group_bit(int i)
{
	return i >= 0 && i < 32 ? (1u << i) : 0u;
}

static const UiColorRole *group_accent(const UiTheme *t, int variant)
{
	if (variant == UI_BTN_WARNING) return &t->warning;
	if (variant == UI_BTN_SUCCESS) return &t->success;
	if (variant == UI_BTN_DANGER || variant == UI_BTN_ERROR) return &t->error;
	return &t->primary;
}

static UiId group_child_id(const UiButtonGroup *g, int index)
{
	return Ui_IdIndex("ui.btn.group", (int)(uint32_t)(g->id >> 32),
		(int)(uint32_t)g->id, index);
}

static int group_clamp_index(const UiButtonGroup *g, int index)
{
	if (index < 0) return -1;
	if (index >= g->count) return g->count > 0 ? g->count - 1 : -1;
	return index;
}

void UiButtonGroup_SetCount(UiButtonGroup *g, int count)
{
	if (!g) return;
	if (count < 0) count = 0;
	g->count = count;
	g->selected = group_clamp_index(g, g->selected);
	g->focus = group_clamp_index(g, g->focus);
	if (count < 32) g->mask &= (1u << count) - 1u;
	if (count == 0) g->mask = 0;
}

void UiButtonGroup_Init(UiButtonGroup *g, UiId id, int mode, int count)
{
	if (!g) return;
	ZeroMemory(g, sizeof(*g));
	g->id = id;
	g->mode = (mode == UI_BG_SINGLE || mode == UI_BG_MULTI) ? mode : UI_BG_NORMAL;
	g->selected = -1;
	g->focus = -1;
	g->clicked = -1;
	UiButtonGroup_SetCount(g, count);
}

void UiButtonGroup_SetSelected(UiButtonGroup *g, int index)
{
	if (!g) return;
	g->selected = group_clamp_index(g, index);
	if (g->selected >= 0) g->focus = g->selected;
}

void UiButtonGroup_SetMask(UiButtonGroup *g, unsigned mask)
{
	if (!g) return;
	g->mask = mask;
	if (g->count <= 0) g->mask = 0;
	else if (g->count < 32) g->mask &= (1u << g->count) - 1u;
}

static void group_invalidate(UiContext *ui, const UiButtonGroup *g)
{
	RECT rc;
	float s;
	if (!ui || !ui->hwnd || !g || g->rc.w <= 0.0f || g->rc.h <= 0.0f) return;
	s = ui->scale > 0.0f ? ui->scale : 1.0f;
	rc.left = (LONG)((g->rc.x - 2.0f) * s);
	rc.top = (LONG)((g->rc.y - 2.0f) * s);
	rc.right = (LONG)((g->rc.x + g->rc.w + 2.0f) * s + 1.0f);
	rc.bottom = (LONG)((g->rc.y + g->rc.h + 2.0f) * s + 1.0f);
	InvalidateRect(ui->hwnd, &rc, FALSE);
}

static int group_activate(UiButtonGroup *g, int index)
{
	if (!g || index < 0 || index >= g->count) return 0;
	if (g->mode == UI_BG_SINGLE) {
		if (g->selected != index) {
			g->selected = index;
			g->changed = 1;
		}
	} else if (g->mode == UI_BG_MULTI) {
		g->mask ^= group_bit(index);
		g->changed = 1;
	}
	g->clicked = index;
	g->focus = index;
	return 1;
}

int UiButtonGroup_Key(UiContext *ui, UiButtonGroup *g, UINT vk)
{
	int count;
	if (!ui || !g || g->count <= 0 || ui->navId != g->id) return 0;
	count = g->count;
	if (g->focus < 0 || g->focus >= count)
		g->focus = g->selected >= 0 ? g->selected : 0;
	switch (vk) {
	case VK_LEFT:
	case VK_UP:
		g->focus = (g->focus + count - 1) % count;
		group_invalidate(ui, g);
		return 1;
	case VK_RIGHT:
	case VK_DOWN:
		g->focus = (g->focus + 1) % count;
		group_invalidate(ui, g);
		return 1;
	case VK_HOME:
		g->focus = 0;
		group_invalidate(ui, g);
		return 1;
	case VK_END:
		g->focus = count - 1;
		group_invalidate(ui, g);
		return 1;
	case VK_SPACE:
	case VK_RETURN:
		group_activate(g, g->focus);
		group_invalidate(ui, g);
		return 1;
	}
	return 0;
}

int UiButtonGroup_Draw(UiContext *ui, UiButtonGroup *g, UiRect rc,
	const char *const *labels, const float *widths,
	const unsigned char *enabled, int variant)
{
	UiTheme *t;
	Gfx *gfx;
	const UiColorRole *accent;
	float x;
	int i, clicked = -1;

	if (!ui || !ui->theme || !ui->gfx || !g || !labels || g->count <= 0)
		return -1;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return -1;
	t = ui->theme;
	gfx = ui->gfx;
	accent = group_accent(t, variant);
	g->rc = rc;
	g->clicked = -1;
	g->changed = 0;
	if (g->focus >= g->count) g->focus = g->count - 1;
	if (g->selected >= g->count) g->selected = g->count - 1;

	/* 融合外框: 整个组只画一次背景和描边 */
	Gfx_FillRoundRect(gfx, rc, t->buttonRadius, t->neutral.base);
	Gfx_StrokeRoundRect(gfx, rc, t->buttonRadius, t->neutral.border, 1.0f);

	x = rc.x;
	for (i = 0; i < g->count; i++) {
		UiWidgetState st;
		UiRect seg;
		UiRect fill;
		float w;
		int en = enabled ? (enabled[i] != 0) : 1;
		int selected = (g->mode == UI_BG_SINGLE && g->selected == i)
			|| (g->mode == UI_BG_MULTI && (g->mask & group_bit(i)) != 0);
		UiId id = group_child_id(g, i);

		if (widths) {
			w = widths[i] > 0.0f ? widths[i] : 0.0f;
			seg = Ui_Rect(x, rc.y, w, rc.h);
			x += w;
		} else {
			float x0 = rc.x + rc.w * (float)i / (float)g->count;
			float x1 = rc.x + rc.w * (float)(i + 1) / (float)g->count;
			seg = Ui_Rect(x0, rc.y, x1 - x0, rc.h);
		}

		st = Ui_State(ui, id, seg, en);
		if (ui->clicked == id) {
			group_activate(g, i);
			ui->navId = g->id;
			clicked = i;
			group_invalidate(ui, g);
		}

		fill = Ui_Rect(seg.x + 2.0f, seg.y + 2.0f,
			seg.w - 4.0f, seg.h - 4.0f);
		if (fill.w > 0.0f && fill.h > 0.0f) {
			float r = t->buttonRadius - 2.0f;
			if (r < 2.0f) r = 2.0f;
			if (selected) {
				UiColor c = accent->soft;
				if (st.hover > 0.004f)
					c = Ui_ColorMix(c, accent->base, st.hover * 0.14f);
				if (st.press > 0.004f)
					c = Ui_ColorMix(c, accent->base, st.press * 0.18f);
				Gfx_FillRoundRect(gfx, fill, r, c);
			} else {
				if (st.hover > 0.004f)
					Gfx_FillRoundRect(gfx, fill, r,
						Ui_ColorAlpha(t->neutral.hover,
							(int)(st.hover * 255.0f)));
				if (st.press > 0.004f)
					Gfx_FillRoundRect(gfx, fill, r,
						Ui_ColorAlpha(t->neutral.press,
							(int)(st.press * 255.0f)));
			}
			if (ui->navId == g->id && g->focus == i)
				Gfx_StrokeRoundRect(gfx, fill, r,
					Ui_ColorAlpha(accent->base, 180), 1.5f);
		}
		Gfx_Text(gfx, t->fBtn, labels[i], seg,
			en ? (selected ? accent->base : t->neutral.text) : t->btnDisabledText,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}

	/* 分隔线画在填充之后, 让选中/hover 的内缩块不会盖住分隔线 */
	x = rc.x;
	for (i = 0; i < g->count - 1; i++) {
		float w;
		if (widths) w = widths[i] > 0.0f ? widths[i] : 0.0f;
		else w = rc.w * (float)(i + 1) / (float)g->count - rc.w * (float)i / (float)g->count;
		x += w;
		Gfx_Line(gfx, x, rc.y + 4.0f, x, rc.y + rc.h - 4.0f,
			Ui_ColorAlpha(t->neutral.border, 150), 1.0f);
	}

	g->clicked = clicked;
	return clicked;
}
