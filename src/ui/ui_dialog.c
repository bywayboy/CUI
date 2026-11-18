#include "ui/ui_dialog.h"
#include "ui/ui_widgets.h"

/* 入场/离场全部交给合成器: 卡片内容画进独立 layer, 透明度与纵向位移由
 * DWM 补间; 主表面只画页面, 模态期间不做快照过渡 (否则整窗重影). */
#define DIALOG_OPEN_MS   180
#define DIALOG_CLOSE_MS  140
#define DIALOG_RISE_DIP  12.0f    /* 入场起点: 卡片在最终位置下方 */
#define DIALOG_EXIT_DIP  (-12.0f) /* 离场终点: 向上飞走 */
#define DIALOG_DIM_ALPHA 112

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

static void dialog_drop_layers(UiDialog *d)
{
	if (d->dimLayer) {
		Gfx_LayerDestroy(d->dimLayer);
		d->dimLayer = NULL;
	}
	if (d->cardLayer) {
		Gfx_LayerDestroy(d->cardLayer);
		d->cardLayer = NULL;
	}
	d->layerPxW = d->layerPxH = 0;
	d->dimDirty = 0;
	d->kicked = 0;
}

static void dialog_release_fx(UiDialog *d, UiContext *ui)
{
	if (!d->fxHeld) return;
	d->fxHeld = 0;
	ui->fxOff = 0;
	Ui_FxCancelAll(ui);
}

static void dialog_finish(UiDialog *d, UiContext *ui)
{
	dialog_drop_layers(d);
	d->visible = d->closing = 0;
	d->stage = 0;
	d->stageMs = 0.0;
	d->result = d->pendingResult;
	d->pendingResult = UI_DIALOG_NONE;
	if (d->ownerPushed) {
		Ui_PopInputOwner(ui, d->id);
		d->ownerPushed = 0;
	}
	ui->widgetCount = ui->wheelTargetCount = 0;
	dialog_clear_input(ui);
	ui->navId = d->savedNav;
	ui->focusId = d->savedFocus;
	ui->navFocusVisible = d->savedFocusVisible;
	if (d->ime) UiIme_SetFocus(d->ime, d->savedEdit);
	d->savedEdit = NULL;
	dialog_release_fx(d, ui);
	dialog_invalidate(ui);
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
	if (!d || !ui || !d->id || d->visible ||
		ui->modal || ui->popupOpen) return 0;
	if (!Ui_PushInputOwner(ui, d->id)) return 0;
	d->ownerPushed = 1;
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
	d->stage = ui->animReduced ? 2 : 1;
	d->stageMs = 0.0;
	d->kicked = 0;
	/* 模态期间控件走自身动画通道, 主表面暂停快照过渡: 避免整窗快照
	 * 把对话框叠成重影, 也避免控件动画被合成器交叉淡化吞掉. */
	d->fxHeld = 1;
	ui->fxOff = 1;
	Ui_FxCancelAll(ui);
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
	if (!d->cardLayer || !Gfx_LayerAlive(d->cardLayer) ||
		!d->dimLayer || (!d->kicked && !d->capture)) {
		dialog_finish(d, ui);
		return;
	}
	d->stage = 3;
	d->stageMs = 0.0;
	/* 离场曲线与旧计时器一致: a(t)=1-(t/d)^3, 位移同步加速 */
	Gfx_LayerSetOpacity(d->cardLayer, 0.0f, DIALOG_CLOSE_MS, GFX_EASE_IN);
	Gfx_LayerSetOffsetY(d->cardLayer, DIALOG_EXIT_DIP, DIALOG_CLOSE_MS, GFX_EASE_IN);
	Gfx_LayerSetOpacity(d->dimLayer, 0.0f, DIALOG_CLOSE_MS, GFX_EASE_IN);
	dialog_invalidate(ui);
}

int UiDialog_IsOpen(const UiDialog *d)
{
	return d && d->visible;
}

int UiDialog_Busy(const UiDialog *d)
{
	return d && d->visible && (d->stage == 1 || d->stage == 3);
}

/* 只推进入场/离场计时 (合成器负责画面), 不重绘. */
void UiDialog_Tick(UiDialog *d, UiContext *ui, double dtMs)
{
	if (!d || !ui || !d->visible) return;
	if (d->stage != 1 && d->stage != 3) return;
	d->stageMs += dtMs > 0.0 ? dtMs : 0.0;
	if (d->stage == 1) {
		if (d->stageMs >= DIALOG_OPEN_MS) d->stage = 2;
	} else if (d->stageMs >= DIALOG_CLOSE_MS) {
		dialog_finish(d, ui);
	}
}

int UiDialog_TakeResult(UiDialog *d)
{
	int result;
	if (!d) return UI_DIALOG_NONE;
	result = d->result;
	d->result = UI_DIALOG_NONE;
	return result;
}

static int dialog_ensure_layers(UiDialog *d, UiContext *ui)
{
	Gfx *g = ui->gfx;
	float scale = ui->scale > 0.0f ? ui->scale : 1.0f;
	int pw = (int)(ui->width * scale + 0.5f);
	int ph = (int)(ui->height * scale + 0.5f);

	if (pw <= 0 || ph <= 0) return 0;
	if (d->dimLayer || d->cardLayer) {
		/* 尺寸变化直接重建: 帧内不做 ResizeBuffers */
		if (!Gfx_LayerAlive(d->dimLayer) || !Gfx_LayerAlive(d->cardLayer) ||
			pw != d->layerPxW || ph != d->layerPxH) {
			dialog_drop_layers(d);
			return 0;
		}
		return 1;
	}
	/* 先遮罩后卡片: 后加的 visual 在上 */
	d->dimLayer = Gfx_CreateLayer(g, pw, ph);
	if (!d->dimLayer) return 0;
	d->cardLayer = Gfx_CreateLayer(g, pw, ph);
	if (!d->cardLayer) {
		dialog_drop_layers(d);
		return 0;
	}
	d->layerPxW = pw;
	d->layerPxH = ph;
	d->dimDirty = 1;
	Gfx_LayerSetOffsetY(d->cardLayer, DIALOG_RISE_DIP, 0, 0);
	if (d->stage == 2) {
		Gfx_LayerSetOffsetY(d->cardLayer, 0.0f, 0, 0);
		Gfx_LayerSetOpacity(d->cardLayer, 1.0f, 0, 0);
		Gfx_LayerSetOpacity(d->dimLayer, 1.0f, 0, 0);
	}
	return 1;
}

static int dialog_draw_dim(UiDialog *d, UiContext *ui)
{
	if (!d->dimDirty) return 1;
	if (!Gfx_LayerBeginInline(ui->gfx, d->dimLayer)) return 0;
	Gfx_FillRoundRect(ui->gfx, Ui_Rect(0, 0, ui->width, ui->height),
		ui->theme->windowRadius, Ui_ColorAlpha(0xFF000000, DIALOG_DIM_ALPHA));
	if (!Gfx_LayerEndInline(ui->gfx)) return 0;
	d->dimDirty = 0;
	return 1;
}

int UiDialog_Begin(UiDialog *d, UiContext *ui, const char *title,
	float width, float height, UiRect *body)
{
	UiTheme *t;
	UiRect view, closeRc;
	UiWidgetState st;
	UiColor color;
	float w, h, cx, cy;
	UiId closeId;
	if (!d || !ui || !d->visible || d->begun || !ui->gfx || !ui->theme) return 0;
	t = ui->theme;
	view = Ui_Rect(0, 0, ui->width, ui->height);
	w = Ui_Clampf(width, 160.0f, ui->width > 32.0f ? ui->width - 32.0f : 1.0f);
	h = Ui_Clampf(height, 120.0f, ui->height > 32.0f ? ui->height - 32.0f : 1.0f);
	d->rect = Ui_Rect((ui->width - w) * 0.5f, (ui->height - h) * 0.5f, w, h);
	d->body = Ui_Rect(d->rect.x + 24.0f, d->rect.y + 66.0f,
		w > 48.0f ? w - 48.0f : 0.0f, h > 90.0f ? h - 90.0f : 0.0f);
	d->savedScope = ui->inputScope;
	ui->inputScope = d->id;
	Ui_PushLayer(ui, UI_Z_BASE);
	d->begun = 1;
	Ui_OccludeRect(ui, view);
	Ui_RegisterWidget(ui, d->id, view);
	d->inputBlocked = d->closing;
	if (d->inputBlocked) Ui_PushInputBlock(ui);

	/* 主表面只画页面; 遮罩与卡片进 layer, 由合成器补间 */
	d->capture = 0;
	if (dialog_ensure_layers(d, ui) && dialog_draw_dim(d, ui))
		d->capture = Gfx_LayerBeginInline(ui->gfx, d->cardLayer);
	if (!d->capture) {
		/* 合成器不可用 (ULW/状态不允许): 主表面直接画最终态 */
		int had = d->dimLayer != NULL || d->cardLayer != NULL;
		dialog_drop_layers(d);
		if (had) dialog_invalidate(ui);   /* 清掉主表面残留的旧对话框像素 */
		if (d->stage == 1) d->stage = 2;
		Gfx_FillRoundRect(ui->gfx, view, t->windowRadius,
			Ui_ColorAlpha(0xFF000000, DIALOG_DIM_ALPHA));
	}

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
	if (d->capture) {
		Gfx_LayerEndInline(ui->gfx);
		d->capture = 0;
		/* 内容已上屏后起跑入场: 淡入 + 从下方减速归位 */
		if (d->stage == 1 && !d->kicked &&
			d->cardLayer && Gfx_LayerAlive(d->cardLayer)) {
			d->kicked = 1;
			/* 入场曲线与旧计时器一致: a=1-(1-p)^3, 位移按同一曲线归位 */
			Gfx_LayerSetOpacity(d->dimLayer, 1.0f, DIALOG_OPEN_MS, GFX_EASE_OUT);
			Gfx_LayerSetOpacity(d->cardLayer, 1.0f, DIALOG_OPEN_MS, GFX_EASE_OUT);
			Gfx_LayerSetOffsetY(d->cardLayer, 0.0f, DIALOG_OPEN_MS, GFX_EASE_OUT);
		}
	}
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
