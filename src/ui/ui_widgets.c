#include "ui/ui_widgets.h"
#include "ui/ui_paint.h"

static int btn_luma(UiColor c)
{
	int r = (int)((c >> 16) & 0xFF);
	int g = (int)((c >> 8) & 0xFF);
	int b = (int)(c & 0xFF);
	return (r * 299 + g * 587 + b * 114) / 1000;
}

static UiColor btn_on(UiColor c)
{
	return btn_luma(c) > 145 ? 0xFF10151F : 0xFFFFFFFF;
}

static const UiColorRole *btn_role(const UiTheme *t, int type)
{
	if (type == UI_BTYPE_PRIMARY) return &t->primary;
	if (type == UI_BTYPE_SUCCESS) return &t->success;
	if (type == UI_BTYPE_WARNING) return &t->warning;
	if (type == UI_BTYPE_ERROR) return &t->error;
	return &t->neutral;
}

int Ui_ButtonEx(UiContext *ui, UiId id, UiRect rc, const char *label,
	const UiButtonStyle *style, int enabled)
{
	UiTheme *t;
	UiSurfaceStyle ss;
	UiWidgetState st;
	UiColor text;
	UiColor glow;
	UiColor role_text;
	const UiColorRole *role;
	UiButtonStyle stl;
	float radius;
	int type, appearance, shape;
	int dashed;

	if (!ui || !ui->theme || !ui->gfx) return 0;
	t = ui->theme;
	stl = style ? *style : UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	type = stl.type;
	appearance = stl.appearance;
	shape = stl.shape;
	if (type < UI_BTYPE_DEFAULT || type > UI_BTYPE_ERROR) type = UI_BTYPE_DEFAULT;
	if (appearance < UI_BAPPEAR_SOLID || appearance > UI_BAPPEAR_TEXT)
		appearance = UI_BAPPEAR_SOLID;
	if (shape < UI_BSHAPE_RECT || shape > UI_BSHAPE_CIRCLE)
		shape = UI_BSHAPE_RECT;
	dashed = (stl.flags & UI_BFLAG_DASHED) != 0;
	role = btn_role(t, type);
	role_text = (type == UI_BTYPE_DEFAULT) ? t->neutral.text : role->base;

	ZeroMemory(&ss, sizeof(ss));
	ss.bg = t->neutral.base;
	ss.hoverBg = t->neutral.hover;
	ss.pressBg = t->neutral.press;
	ss.border = t->neutral.border;
	ss.hoverBorder = t->neutral.border;
	ss.focusBorder = t->neutral.border;
	text = t->neutral.text;
	glow = 0;

	if (appearance == UI_BAPPEAR_SOLID) {
		if (type == UI_BTYPE_DEFAULT) {
			ss.bg = t->neutral.base;
			ss.hoverBg = Ui_ColorMix(t->neutral.hover, t->neutral.press, 0.55f);
			ss.pressBg = Ui_ColorShift(t->neutral.press, -8);
			ss.border = t->neutral.border;
			ss.hoverBorder = Ui_ColorMix(t->neutral.border, t->primary.base, 0.35f);
			ss.focusBorder = t->primary.base;
			text = t->neutral.text;
		} else {
			ss.bg = role->base;
			ss.hoverBg = role->hover;
			ss.pressBg = role->press;
			ss.border = role->base;
			ss.hoverBorder = role->base;
			ss.focusBorder = role->base;
			text = btn_on(role->base);
			glow = Ui_ColorAlpha(role->base, 100);
		}
	} else if (appearance == UI_BAPPEAR_PLAIN) {
		if (type == UI_BTYPE_DEFAULT) {
			ss.bg = t->neutral.soft;
			ss.hoverBg = Ui_ColorMix(t->neutral.hover, t->neutral.press, 0.6f);
			ss.pressBg = Ui_ColorShift(t->neutral.press, -6);
			ss.border = t->neutral.border;
			ss.hoverBorder = Ui_ColorMix(t->neutral.border, t->primary.base, 0.35f);
			ss.focusBorder = t->primary.base;
			text = t->neutral.text;
		} else {
			ss.bg = role->soft;
			ss.hoverBg = role->base;
			ss.pressBg = role->press;
			ss.border = Ui_ColorAlpha(role->base, 150);
			ss.hoverBorder = role->base;
			ss.focusBorder = role->base;
			text = role_text;
			glow = Ui_ColorAlpha(role->base, 70);
		}
	} else {
		ss.bg = 0;
		ss.bg2 = 0;
		ss.hoverBg = (type == UI_BTYPE_DEFAULT) ? t->neutral.soft
			: Ui_ColorAlpha(role->soft, 200);
		ss.pressBg = (type == UI_BTYPE_DEFAULT) ? t->neutral.press
			: Ui_ColorAlpha(role->press, 180);
		ss.border = 0;
		ss.hoverBorder = 0;
		ss.focusBorder = 0;
		text = (type == UI_BTYPE_DEFAULT) ? t->text2 : role->base;
		glow = 0;
	}

	if (!enabled) {
		ss.bg = t->neutral.soft;
		ss.bg2 = 0;
		ss.hoverBg = 0;
		ss.pressBg = 0;
		ss.border = t->neutral.border;
		ss.hoverBorder = 0;
		ss.focusBorder = 0;
		ss.overlayTint = 0;
		text = t->btnDisabledText;
		glow = 0;
	}

	radius = t->buttonRadius;
	if (shape == UI_BSHAPE_ROUND) radius = rc.h * 0.5f;
	else if (shape == UI_BSHAPE_CIRCLE)
		radius = (rc.w < rc.h ? rc.w : rc.h) * 0.5f;
	ss.radius = radius;
	ss.borderW = 1.0f;
	ss.glowW = 4.0f;
	ss.glowMask = UI_GLOW_HOVER | UI_GLOW_FOCUS;
	ss.glow = glow;
	ss.dashed = dashed;

	st = Ui_DrawSurface(ui, id, rc, &ss, enabled);
	if (enabled && appearance == UI_BAPPEAR_PLAIN &&
		type != UI_BTYPE_DEFAULT) {
		float k = st.hover > st.press ? st.hover : st.press;
		text = Ui_ColorMix(role_text, btn_on(role->base), k);
	}
	Gfx_Text(ui->gfx, t->fBtn, label, rc, text,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	return enabled && ui->clicked == id;
}

int Ui_Button(UiContext *ui, UiId id, UiRect rc, const char *label,
	int variant, int enabled)
{
	UiButtonStyle s;
	s.type = UI_BTYPE_DEFAULT;
	s.appearance = UI_BAPPEAR_SOLID;
	s.shape = UI_BSHAPE_RECT;
	s.flags = 0;
	if (variant == UI_BTN_PRIMARY) {
		s.type = UI_BTYPE_PRIMARY;
	} else if (variant == UI_BTN_DANGER) {
		s.type = UI_BTYPE_ERROR;
		s.appearance = UI_BAPPEAR_PLAIN;
	} else if (variant == UI_BTN_WARNING) {
		s.type = UI_BTYPE_WARNING;
		s.appearance = UI_BAPPEAR_PLAIN;
	} else if (variant == UI_BTN_SUCCESS) {
		s.type = UI_BTYPE_SUCCESS;
		s.appearance = UI_BAPPEAR_PLAIN;
	}
	return Ui_ButtonEx(ui, id, rc, label, &s, enabled);
}

void Ui_Panel(UiContext *ui, UiRect rc)
{
	UiTheme *t;
	if (!ui || !ui->theme || !ui->gfx) return;
	t = ui->theme;
	Gfx_FillRoundRect(ui->gfx, rc, t->panelRadius, t->panelBg);
	Gfx_StrokeRoundRect(ui->gfx, rc, t->panelRadius, t->panelBorder, 1.0f);
}

void Ui_ProgressBar(UiContext *ui, UiRect rc, float value, int drawFill)
{
	UiTheme *t;
	float p;
	if (!ui || !ui->theme || !ui->gfx) return;
	t = ui->theme;
	p = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
	Gfx_FillRoundRect(ui->gfx, rc, t->progressRadius, t->progressTrack);
	if (drawFill && p > 0.001f) {
		Gfx_FillRoundRectGradH(ui->gfx,
			Ui_Rect(rc.x, rc.y, rc.w * p, rc.h), t->progressRadius,
			t->progressFill, t->progressFill2);
	}
}
