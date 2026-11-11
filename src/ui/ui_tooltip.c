#include "ui/ui_tooltip.h"
#include "ui/ui_popup.h"

static int tooltip_same_anchor(UiRect a, UiRect b)
{
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

void UiTooltip_Init(UiTooltip *tt, UiId id)
{
	if (!tt) return;
	ZeroMemory(tt, sizeof(*tt));
	tt->id = id;
	tt->delayMs = 400;
	tt->side = UI_POPUP_BELOW;
	tt->align = UI_POPUP_ALIGN_CENTER;
}

void UiTooltip_SetDelay(UiTooltip *tt, int delayMs)
{
	if (!tt) return;
	tt->delayMs = delayMs < 0 ? 0 : delayMs;
}

void UiTooltip_Update(UiTooltip *tt, UiContext *ui, UiId target,
	UiRect anchor, const char *text, double nowMs)
{
	int changed;
	if (!tt || !ui) return;
	if (ui->modal || !target || !text || !text[0] || ui->hover != target) {
		tt->open = 0;
		tt->target = UI_ID_NONE;
		tt->hoverStartMs = 0.0;
		return;
	}
	changed = tt->target != target || !tooltip_same_anchor(tt->anchor, anchor) ||
		lstrcmpA(tt->text, text) != 0;
	if (changed) {
		tt->target = target;
		tt->anchor = anchor;
		lstrcpynA(tt->text, text, (int)sizeof(tt->text));
		tt->hoverStartMs = nowMs;
		tt->open = 0;
	}
	if (!tt->open && nowMs - tt->hoverStartMs >= (double)tt->delayMs)
		tt->open = 1;
}

int UiTooltip_Pending(const UiTooltip *tt)
{
	return tt && tt->target && !tt->open;
}

void UiTooltip_Draw(UiTooltip *tt, UiContext *ui)
{
	UiTheme *t;
	float tw, w, h;
	UiRect rc, textRc;

	if (!tt || !tt->open || !ui || !ui->theme || !ui->gfx) return;
	t = ui->theme;
	tw = Gfx_TextWidth(ui->gfx, t->fSmall, tt->text);
	w = tw + 20.0f;
	if (w < 60.0f) w = 60.0f;
	if (w > 340.0f) w = 340.0f;
	h = 28.0f;
	rc = UiPopup_Place(ui, tt->anchor, w, h, tt->side, tt->align, 6.0f);
	textRc = Ui_Rect(rc.x + 10.0f, rc.y, rc.w - 20.0f, rc.h);

	Ui_PushLayer(ui, UI_Z_OVERLAY);
	Gfx_FillRoundRect(ui->gfx, rc, 6.0f, t->toastBg);
	Gfx_StrokeRoundRect(ui->gfx, rc, 6.0f, t->toastBorder, 1.0f);
	Gfx_Text(ui->gfx, t->fSmall, tt->text, textRc, t->toastText,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Ui_PopLayer(ui);
}
