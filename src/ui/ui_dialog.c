#include "ui/ui_dialog.h"
#include "ui/ui_widgets.h"

static UiId dialog_child(const UiDialog *d, int index)
{
	return Ui_IdIndex("ui.dialog", (int)d->id, (int)(d->id >> 32), index);
}

static void dialog_invalidate(UiContext *ui)
{
	if (ui->hwnd) InvalidateRect(ui->hwnd, NULL, FALSE);
}

static void dialog_clear_input(UiContext *ui)
{
	ui->hover = ui->pressed = ui->active = UI_ID_NONE;
	ui->clicked = ui->down = ui->released = UI_ID_NONE;
	ui->mouseDown = ui->clickedOutside = ui->drag.active = 0;
	ui->wheelTarget = UI_ID_NONE;
	ui->wheelDelta = 0.0f;
	if (ui->hwnd && GetCapture() == ui->hwnd) ReleaseCapture();
}

static UiId dialog_focus_at(const UiDialog *d, int index)
{
	return index < d->focusCount ? d->focusOrder[index] : dialog_child(d, 0);
}

static int dialog_can_focus(const UiDialog *d, const UiContext *ui, UiId id)
{
	int i;
	for (i = 0; i < ui->widgetCount; i++) {
		const UiWidget *w = &ui->widgets[i];
		if (w->scope == d->id && w->id == id && w->clip.w >= 0.0f)
			return 1;
	}
	return 0;
}

static void dialog_finish(UiDialog *d, UiContext *ui)
{
	d->visible = d->closing = 0;
	d->progress = 0.0f;
	d->result = d->pendingResult;
	d->pendingResult = UI_DIALOG_NONE;
	ui->inputOwner = UI_ID_NONE;
	ui->widgetCount = ui->wheelTargetCount = 0;
	dialog_clear_input(ui);
	ui->navId = d->savedNav;
	ui->focusId = d->savedFocus;
	ui->navFocusVisible = d->savedFocusVisible;
	if (d->ime) UiIme_SetFocus(d->ime, d->savedEdit);
	d->savedEdit = NULL;
	dialog_invalidate(ui);
}

void UiDialog_Init(UiDialog *d, UiId id)
{
	if (!d) return;
	ZeroMemory(d, sizeof(*d));
	d->id = id;
	d->actions[0] = dialog_child(d, 1);
	d->actions[1] = dialog_child(d, 2);
}

void UiDialog_AttachIme(UiDialog *d, UiIme *ime)
{
	if (d && !d->visible) d->ime = ime;
}

void UiDialog_SetFocusOrder(UiDialog *d, const UiId *ids, int count)
{
	if (!d) return;
	d->focusOrder = ids;
	d->focusCount = ids && count > 0 ? count : 0;
}

int UiDialog_Open(UiDialog *d, UiContext *ui, UiId owner)
{
	if (!d || !ui || !d->id || d->visible || ui->inputOwner ||
		ui->modal || ui->popupOpen) return 0;
	d->savedNav = owner ? owner : ui->navId;
	d->savedFocus = ui->focusId;
	d->savedFocusVisible = ui->navFocusVisible;
	if (d->ime) {
		d->savedEdit = d->ime->edit;
		UiIme_SetFocus(d->ime, NULL);
	}
	d->visible = 1;
	d->closing = 0;
	d->confirmMode = 0;
	d->pendingResult = d->result = UI_DIALOG_NONE;
	d->progress = ui->animReduced ? 1.0f : 0.0f;
	ui->inputOwner = d->id;
	dialog_clear_input(ui);
	ui->navId = ui->focusId = UI_ID_NONE;
	dialog_invalidate(ui);
	return 1;
}

void UiDialog_Close(UiDialog *d, UiContext *ui, int result)
{
	if (!d || !ui || !d->visible || d->closing || ui->popupOpen) return;
	d->closing = 1;
	if (d->begun && !d->inputBlocked) {
		Ui_PushInputBlock(ui);
		d->inputBlocked = 1;
	}
	d->pendingResult = result == UI_DIALOG_ACCEPT ? UI_DIALOG_ACCEPT : UI_DIALOG_CANCEL;
	if (d->ime) UiIme_SetFocus(d->ime, NULL);
	ui->widgetCount = ui->wheelTargetCount = 0;
	dialog_clear_input(ui);
	ui->focusId = UI_ID_NONE;
	dialog_invalidate(ui);
}

int UiDialog_IsOpen(const UiDialog *d)
{
	return d && d->visible;
}

int UiDialog_Busy(const UiDialog *d)
{
	return d && d->visible && (d->closing || d->progress < 1.0f);
}

void UiDialog_Tick(UiDialog *d, UiContext *ui, double dtMs)
{
	float step;
	if (!d || !ui || !UiDialog_Busy(d)) return;
	step = (float)(dtMs > 0.0 ? dtMs : 0.0) / (d->closing ? 140.0f : 180.0f);
	if (ui->animReduced) step = 1.0f;
	if (d->closing) {
		d->progress -= step;
		if (d->progress <= 0.0f) {
			dialog_finish(d, ui);
			return;
		}
	} else {
		d->progress += step;
		if (d->progress > 1.0f) d->progress = 1.0f;
	}
	dialog_invalidate(ui);
}

int UiDialog_TakeResult(UiDialog *d)
{
	int result;
	if (!d) return UI_DIALOG_NONE;
	result = d->result;
	d->result = UI_DIALOG_NONE;
	return result;
}

int UiDialog_Begin(UiDialog *d, UiContext *ui, const char *title,
	float width, float height, UiRect *body)
{
	UiTheme *t;
	UiRect view, closeRc;
	UiWidgetState st;
	UiColor color;
	float alpha, q, w, h, cx, cy;
	UiId closeId;
	if (!d || !ui || !d->visible || d->begun || !ui->gfx || !ui->theme) return 0;
	t = ui->theme;
	view = Ui_Rect(0, 0, ui->width, ui->height);
	w = Ui_Clampf(width, 160.0f, ui->width > 32.0f ? ui->width - 32.0f : 1.0f);
	h = Ui_Clampf(height, 120.0f, ui->height > 32.0f ? ui->height - 32.0f : 1.0f);
	q = 1.0f - d->progress;
	alpha = 1.0f - q * q * q;
	d->rect = Ui_Rect((ui->width - w) * 0.5f,
		(ui->height - h) * 0.5f + 12.0f * (1.0f - alpha), w, h);
	d->body = Ui_Rect(d->rect.x + 24.0f, d->rect.y + 66.0f,
		w > 48.0f ? w - 48.0f : 0.0f, h > 90.0f ? h - 90.0f : 0.0f);
	d->savedScope = ui->inputScope;
	ui->inputScope = d->id;
	Ui_PushLayer(ui, UI_Z_BASE);
	d->begun = 1;
	Ui_RegisterWidget(ui, d->id, view);
	d->inputBlocked = d->closing;
	if (d->inputBlocked) Ui_PushInputBlock(ui);
	Gfx_FillRoundRect(ui->gfx, view, t->windowRadius,
		Ui_ColorAlpha(0xFF000000, (int)(alpha * 112.0f)));
	d->alphaPushed = Gfx_PushAlpha(ui->gfx, alpha);
	Gfx_FillRoundRect(ui->gfx, d->rect, 12.0f, t->toastBg);
	Gfx_StrokeRoundRect(ui->gfx, Ui_RectInset(d->rect, 0.5f), 11.5f,
		t->toastBorder, 1.0f);
	Ui_RegisterWidget(ui, dialog_child(d, 3), d->rect);
	closeId = dialog_child(d, 0);
	closeRc = Ui_Rect(d->rect.x + w - 48.0f, d->rect.y + 16.0f, 32.0f, 32.0f);
	st = Ui_State(ui, closeId, closeRc, !d->closing);
	if (st.hover > 0.0f || st.focus > 0.0f) {
		float k = st.hover > st.focus ? st.hover : st.focus;
		Gfx_FillRoundRect(ui->gfx, closeRc, 7.0f,
			Ui_ColorAlpha(t->primary.base, (int)(k * 28.0f)));
	}
	color = Ui_ColorMix(t->text3, t->primary.base, st.hover);
	cx = closeRc.x + closeRc.w * 0.5f;
	cy = closeRc.y + closeRc.h * 0.5f;
	Gfx_Line(ui->gfx, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f, color, 1.6f);
	Gfx_Line(ui->gfx, cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f, color, 1.6f);
	Gfx_Text(ui->gfx, t->fTitle, title ? title : "",
		Ui_Rect(d->rect.x + 24.0f, d->rect.y + 16.0f, w - 80.0f, 32.0f),
		t->text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	if (!d->closing && (ui->clicked == closeId ||
		(d->closeOnBackdrop && ui->clicked == d->id)))
		UiDialog_Close(d, ui, UI_DIALOG_CANCEL);
	if (body) *body = d->body;
	return 1;
}

void UiDialog_End(UiDialog *d, UiContext *ui)
{
	int i, valid = 0;
	UiId first = UI_ID_NONE;
	if (!d || !ui || !d->begun) return;
	if (d->alphaPushed) Gfx_PopAlpha(ui->gfx);
	if (d->inputBlocked) Ui_PopInputBlock(ui);
	Ui_PopLayer(ui);
	ui->inputScope = d->savedScope;
	d->begun = 0;
	if (d->closing || ui->popupOpen) return;
	for (i = 0; i <= d->focusCount; i++) {
		UiId id = dialog_focus_at(d, i);
		if (!dialog_can_focus(d, ui, id)) continue;
		if (!first) first = id;
		if (ui->down == id || ui->clicked == id) {
			ui->navId = id;
			Ui_SetFocusId(ui, id);
		}
		if (ui->navId == id) valid = 1;
	}
	if (!valid && first) {
		Ui_SetNavId(ui, first);
		Ui_SetFocusId(ui, first);
		dialog_invalidate(ui);
	}
}

int UiDialog_Key(UiDialog *d, UiContext *ui, UINT vk)
{
	int i, start = -1, dir;
	if (!d || !ui || !d->visible) return 0;
	if (d->closing) return 1;
	if (ui->popupOpen) return 0;
	if (d->ime && d->ime->edit && Edit_HasComposition(d->ime->edit)) return 1;
	if (vk == VK_ESCAPE) {
		UiDialog_Close(d, ui, UI_DIALOG_CANCEL);
		return 1;
	}
	if (vk == VK_TAB) {
		dir = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
		for (i = 0; i <= d->focusCount; i++)
			if (dialog_focus_at(d, i) == ui->navId) { start = i; break; }
		if (start < 0) start = dir > 0 ? d->focusCount : 0;
		for (i = 1; i <= d->focusCount + 1; i++) {
			int index = (start + dir * i + d->focusCount + 1) % (d->focusCount + 1);
			UiId id = dialog_focus_at(d, index);
			if (dialog_can_focus(d, ui, id)) {
				Ui_SetNavId(ui, id);
				ui->focusId = id;
				dialog_invalidate(ui);
				break;
			}
		}
		return 1;
	}
	if (vk == VK_RETURN || vk == VK_SPACE) {
		if (ui->navId == dialog_child(d, 0) ||
			(d->confirmMode && ui->navId == d->actions[0])) {
			UiDialog_Close(d, ui, UI_DIALOG_CANCEL);
			return 1;
		}
		if (d->confirmMode && ui->navId == d->actions[1]) {
			UiDialog_Close(d, ui, UI_DIALOG_ACCEPT);
			return 1;
		}
	}
	return 0;
}

void Ui_ConfirmDialog(UiDialog *d, UiContext *ui, const char *title,
	const char *message, const char *acceptText, const char *cancelText, int danger)
{
	UiRect body, accept, cancel, text;
	float bw;
	if (!d || !ui || !d->visible) return;
	d->confirmMode = 1;
	UiDialog_SetFocusOrder(d, d->actions, 2);
	if (!UiDialog_Begin(d, ui, title, 460.0f, 260.0f, &body)) return;
	bw = body.w > 200.0f ? 96.0f : (body.w - 12.0f) * 0.5f;
	accept = Ui_Rect(body.x + body.w - bw, body.y + body.h - 36.0f, bw, 36.0f);
	cancel = Ui_Rect(accept.x - 12.0f - bw, accept.y, bw, 36.0f);
	text = Ui_Rect(body.x, body.y, body.w, body.h - 58.0f);
	Gfx_Text(ui->gfx, ui->theme->fBody, message ? message : "", text,
		ui->theme->text2, GFX_TEXT_WRAP);
	if (Ui_Button(ui, d->actions[0], cancel, cancelText ? cancelText : "取消",
		UI_BTN_SECONDARY, 1))
		UiDialog_Close(d, ui, UI_DIALOG_CANCEL);
	if (Ui_Button(ui, d->actions[1], accept, acceptText ? acceptText : "确认",
		danger ? UI_BTN_DANGER : UI_BTN_PRIMARY, 1))
		UiDialog_Close(d, ui, UI_DIALOG_ACCEPT);
	UiDialog_End(d, ui);
}
