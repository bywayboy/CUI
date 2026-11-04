#include "ui/ui_widgets.h"
#include "ui/ui_paint.h"

int Ui_Button(UiContext *ui, UiId id, UiRect rc, const char *label,
	int variant, int enabled)
{
	UiTheme *t;
	UiSurfaceStyle ss;
	UiWidgetState st;
	UiColor text;
	UiColor glow;

	if (!ui || !ui->theme || !ui->gfx) return 0;
	t = ui->theme;

	ZeroMemory(&ss, sizeof(ss));
	ss.bg = t->btnBg;
	ss.hoverBg = t->btnHoverBg;
	ss.pressBg = t->btnPressBg;
	ss.border = t->btnBorder;
	ss.hoverBorder = t->btnHoverBorder;
	ss.focusBorder = t->btnBorder;
	text = t->btnText;
	glow = 0;

	if (variant == UI_BTN_PRIMARY) {
		ss.bg = t->btnPrimary;
		ss.bg2 = t->btnPrimary2;
		ss.hoverBg = t->btnPrimaryHover;
		ss.pressBg = t->btnPrimaryPress;
		ss.border = t->btnPrimaryBorder;
		ss.hoverBorder = t->btnPrimaryBorder;
		ss.focusBorder = t->btnPrimaryBorder;
		ss.overlayTint = 1;
		text = t->btnPrimaryText;
		glow = Ui_ColorAlpha(t->blue2, 110);
	} else if (variant == UI_BTN_DANGER) {
		ss.hoverBg = t->dangerBg;
		ss.pressBg = Ui_ColorShift(t->dangerBg, -8);
		ss.border = Ui_ColorAlpha(t->danger, 150);
		ss.hoverBorder = t->danger;
		ss.focusBorder = t->danger;
		text = t->danger;
		glow = Ui_ColorAlpha(t->danger, 80);
	}

	if (!enabled) {
		ss.bg = t->btnDisabledBg;
		ss.bg2 = 0;
		ss.hoverBg = 0;
		ss.pressBg = 0;
		ss.border = t->btnBorder;
		ss.hoverBorder = 0;
		ss.focusBorder = 0;
		ss.overlayTint = 0;
		text = t->btnDisabledText;
		glow = 0;
	}

	ss.radius = t->buttonRadius;
	ss.borderW = 1.0f;
	ss.glowW = 4.0f;
	ss.glowMask = UI_GLOW_HOVER;
	ss.glow = glow;

	st = Ui_DrawSurface(ui, id, rc, &ss, enabled);
	(void)st;
	Gfx_Text(ui->gfx, t->fBtn, label, rc, text,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	return enabled && ui->clicked == id;
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
