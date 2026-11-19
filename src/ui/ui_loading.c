#include "ui/ui_loading.h"
#include "ui/ui_frame.h"
#include "ui/ui_icon.h"

#define UI_LOADING_SPINNER 44.0f
#define UI_LOADING_SPIN_DEG_PER_SEC 327.0f   /* 360° / 1.1s */
#define UI_LOADING_TEXT_GAP 14.0f
#define UI_LOADING_BTN_GAP 16.0f
#define UI_LOADING_BTN 30.0f
#define UI_LOADING_BTN_HIT 38.0f

typedef struct LoadingLayout {
	UiRect spin;
	UiRect text;
	UiRect btn;
	UiRect btnHit;
} LoadingLayout;

static int g_loadingSlot;

static void loading_drop_(UiLoading *ld)
{
	if (!ld) return;
	if (ld->spin) {
		Gfx_PartDestroy(ld->spin);
		ld->spin = NULL;
	}
	if (ld->layer) {
		Gfx_LayerDestroy(ld->layer);
		ld->layer = NULL;
	}
	ld->layerW = ld->layerH = 0;
	ld->layerX = ld->layerY = -1.0e30f;   /* 强制下次创建后 SetPos */
	ld->layerIn = ld->layerOut = 0;
	ld->layerDirty = 0;
	ld->drawnRev = -1;
}

static void loading_copy_(char *dst, const char *src, int cap)
{
	int i = 0;
	if (!dst || cap <= 0) return;
	if (src) {
		while (src[i] && i < cap - 1) {
			dst[i] = src[i];
			i++;
		}
	}
	dst[i] = 0;
}

static void loading_layout_(const UiLoading *ld, Gfx *g, UiTheme *th,
	UiRect area, LoadingLayout *out)
{
	float textH = Gfx_TextHeight(g, th->fBody);
	int hasText = ld->text[0] != 0;
	float total = UI_LOADING_SPINNER;
	float y, cx;

	if (hasText) total += UI_LOADING_TEXT_GAP + textH;
	if (ld->cancelable) total += UI_LOADING_BTN_GAP + UI_LOADING_BTN;
	if (total > area.h) total = area.h;
	y = area.y + (area.h - total) * 0.5f;
	cx = area.x + area.w * 0.5f;

	out->spin = Ui_Rect(cx - UI_LOADING_SPINNER * 0.5f, y,
		UI_LOADING_SPINNER, UI_LOADING_SPINNER);
	y += UI_LOADING_SPINNER;

	out->text = Ui_Rect(0, 0, 0, 0);
	if (hasText) {
		float tw = area.w - 48.0f;
		y += UI_LOADING_TEXT_GAP;
		if (tw < 0.0f) tw = 0.0f;
		out->text = Ui_Rect(area.x + 24.0f, y, tw, textH);
		y += textH;
	}

	out->btn = out->btnHit = Ui_Rect(0, 0, 0, 0);
	if (ld->cancelable) {
		y += UI_LOADING_BTN_GAP;
		out->btn = Ui_Rect(cx - UI_LOADING_BTN * 0.5f, y,
			UI_LOADING_BTN, UI_LOADING_BTN);
		out->btnHit = Ui_Rect(cx - UI_LOADING_BTN_HIT * 0.5f,
			y - (UI_LOADING_BTN_HIT - UI_LOADING_BTN) * 0.5f,
			UI_LOADING_BTN_HIT, UI_LOADING_BTN_HIT);
	}
}

/* 弧头实、弧尾衰减; deg 为整体旋转相位 (回退路径按帧推进). */
static void loading_draw_spinner_(Gfx *g, UiRect rc, float deg, UiTheme *th)
{
	float cx = rc.x + rc.w * 0.5f;
	float cy = rc.y + rc.h * 0.5f;
	float step = 270.0f / 24.0f;
	int i;
	Gfx_StrokeEllipse(g, cx, cy, 16.0f,
		Ui_ColorAlpha(th->primary.base, 36), 4.0f);
	for (i = 0; i < 24; i++) {
		float a0 = -90.0f + deg + (float)i * step;
		int alpha = (int)(255.0f * (1.0f - (float)i / 23.0f * 0.85f));
		Gfx_StrokeArc(g, cx, cy, 16.0f, a0, step + 0.6f,
			Ui_ColorAlpha(th->primary.base, alpha), 4.0f);
	}
}

static void loading_draw_cancel_(UiLoading *ld, Gfx *g, UiRect rc,
	float hover, float press)
{
	UiTheme *th = ld->ui->theme;
	float cx = rc.x + rc.w * 0.5f;
	float cy = rc.y + rc.h * 0.5f;
	float k = hover > press ? hover : press;
	int bgA = (int)(34.0f * hover + 52.0f * press);
	UiColor border = Ui_ColorMix(th->text3, th->primary.base, k);
	UiColor icon = Ui_ColorMix(th->text2, th->primary.base, k);

	if (bgA > 0)
		Gfx_FillEllipse(g, cx, cy, UI_LOADING_BTN * 0.5f,
			UI_LOADING_BTN * 0.5f, Ui_ColorAlpha(th->primary.base, bgA));
	Gfx_StrokeEllipse(g, cx, cy, UI_LOADING_BTN * 0.5f, border, 1.0f);
	if (th->fIcon) {
		UiIcon ic = UiIcon_Glyph(th->fIcon, 0xE711u);
		UiIcon_Draw(ld->ui, &ic,
			Ui_Rect(cx - 7.0f, cy - 7.0f, 14.0f, 14.0f), icon, 1);
	} else {
		Gfx_Line(g, cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f,
			icon, 1.4f);
		Gfx_Line(g, cx - 4.0f, cy + 4.0f, cx + 4.0f, cy - 4.0f,
			icon, 1.4f);
	}
}

static void loading_draw_body_(UiLoading *ld, Gfx *g, UiRect area,
	int drawSpin, float spinDeg, float hover, float press)
{
	UiTheme *th = ld->ui->theme;
	LoadingLayout lay;

	Gfx_FillRoundRect(g, area, ld->radius, Ui_ColorAlpha(th->page, 170));
	loading_layout_(ld, g, th, area, &lay);
	if (drawSpin)
		loading_draw_spinner_(g, lay.spin, spinDeg, th);
	if (ld->text[0])
		Gfx_Text(g, th->fBody, ld->text, lay.text, th->text,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	if (ld->cancelable)
		loading_draw_cancel_(ld, g, lay.btn, hover, press);
}

/* spinner 部件的静态内容: 只画一次, 旋转交给合成器. */
static void loading_spin_draw_(Gfx *g, void *user)
{
	UiLoading *ld = (UiLoading *)user;
	loading_draw_spinner_(g, Ui_Rect(0, 0, UI_LOADING_SPINNER,
		UI_LOADING_SPINNER), 0.0f, ld->ui->theme);
}

static void loading_layer_draw_(Gfx *g, void *user)
{
	UiLoading *ld = (UiLoading *)user;
	loading_draw_body_(ld, g, Ui_Rect(0, 0, ld->rc.w, ld->rc.h),
		0, 0.0f, ld->btnHover, ld->btnPress);
}

static float loading_fade_(const UiLoading *ld, double nowMs)
{
	double f;
	if (ld->phase == UI_LOADING_HIDING) {
		f = (nowMs - ld->hideAt) / (double)UI_LOADING_FADE_MS;
		if (f < 0.0) f = 0.0;
		if (f > 1.0) f = 1.0;
		return (float)(1.0 - f);
	}
	if (ld->phase == UI_LOADING_SHOWN) {
		f = (nowMs - ld->shownAt) / (double)UI_LOADING_FADE_MS;
		if (f < 0.0) f = 0.0;
		if (f > 1.0) f = 1.0;
		return (float)f;
	}
	return 0.0f;
}

static void loading_request_cancel_(UiLoading *ld)
{
	if (!ld->cancelable || ld->cancelReq) return;
	ld->cancelReq = 1;
	ld->pendingRedraw = 1;
}

/* 遮罩出现的瞬间清掉遮罩下的 hover/press, 避免 halo 在遮罩下残留. */
static void loading_clear_hover_(UiLoading *ld, UiRect rc)
{
	UiContext *ui = ld->ui;
	UiId old;
	UiRect wr;
	if (!ui->mouseOver || ui->mouseDown) return;
	if (!Ui_RectContains(rc, ui->mouseX, ui->mouseY)) return;
	old = ui->hover;
	if (old != UI_ID_NONE) {
		ui->hover = UI_ID_NONE;
		if (Ui_WidgetRect(ui, old, &wr)) Ui_InvalidateRect(ui, wr);
	}
	ui->pressed = UI_ID_NONE;
}

void UiLoading_Init(UiLoading *ld, UiContext *ui)
{
	if (!ld) return;
	ZeroMemory(ld, sizeof(*ld));
	ld->ui = ui;
	ld->slot = ++g_loadingSlot;
	ld->delayMs = 150;
	ld->minShowMs = 400;
	ld->drawnRev = -1;
	loading_copy_(ld->text, "载入中...", (int)sizeof(ld->text));
}

void UiLoading_Shutdown(UiLoading *ld)
{
	if (!ld) return;
	loading_drop_(ld);
	ZeroMemory(ld, sizeof(*ld));
	ld->drawnRev = -1;
}

void UiLoading_SetText(UiLoading *ld, const char *utf8)
{
	if (!ld) return;
	loading_copy_(ld->text, utf8, (int)sizeof(ld->text));
	ld->contentRev++;
}

void UiLoading_SetCancelable(UiLoading *ld, int on)
{
	if (!ld) return;
	ld->cancelable = on ? 1 : 0;
	ld->contentRev++;
}

void UiLoading_SetPassThrough(UiLoading *ld, int on)
{
	if (ld) ld->passThrough = on ? 1 : 0;
}

void UiLoading_SetTiming(UiLoading *ld, int delayMs, int minShowMs)
{
	if (!ld) return;
	ld->delayMs = delayMs > 0 ? delayMs : 0;
	ld->minShowMs = minShowMs > 0 ? minShowMs : 0;
}

void UiLoading_SetRadius(UiLoading *ld, float radiusDip)
{
	if (!ld) return;
	ld->radius = radiusDip > 0.0f ? radiusDip : 0.0f;
	ld->contentRev++;
}

void UiLoading_Show(UiLoading *ld)
{
	double now;
	if (!ld) return;
	now = UiFrame_Now();
	ld->cancelReq = 0;
	if (ld->phase == UI_LOADING_SHOWN || ld->phase == UI_LOADING_PENDING)
		return;
	if (ld->phase == UI_LOADING_HIDING) {
		double f = (now - ld->hideAt) / (double)UI_LOADING_FADE_MS;
		if (f < 0.0) f = 0.0;
		if (f > 1.0) f = 1.0;
		ld->phase = UI_LOADING_SHOWN;
		ld->shownAt = now - (1.0 - f) * (double)UI_LOADING_FADE_MS;
		ld->hidePending = 0;
		ld->layerOut = 0;
		ld->pendingRedraw = 1;
		return;
	}
	ld->phase = UI_LOADING_PENDING;
	ld->showAt = now;
	ld->hidePending = 0;
	ld->shownDrawn = 0;
	ld->pendingRedraw = 1;
}

void UiLoading_Hide(UiLoading *ld)
{
	double now;
	if (!ld || ld->phase == UI_LOADING_HIDDEN) return;
	if (ld->phase == UI_LOADING_PENDING) {
		ld->phase = UI_LOADING_HIDDEN;
		ld->hidePending = 0;
		ld->pendingRedraw = 1;
		return;
	}
	if (ld->phase == UI_LOADING_HIDING) return;
	now = UiFrame_Now();
	ld->hidePending = 1;
	if (now >= ld->shownAt + (double)ld->minShowMs) {
		ld->phase = UI_LOADING_HIDING;
		ld->hideAt = now;
		ld->pendingRedraw = 1;
	}
}

int UiLoading_Visible(const UiLoading *ld)
{
	return ld && ld->phase != UI_LOADING_HIDDEN;
}

int UiLoading_Shown(const UiLoading *ld)
{
	return ld && (ld->phase == UI_LOADING_SHOWN ||
		ld->phase == UI_LOADING_HIDING);
}

int UiLoading_UsingLayer(const UiLoading *ld)
{
	return ld && ld->ui && !ld->fallback;
}

int UiLoading_TakeCancel(UiLoading *ld)
{
	int r;
	if (!ld) return 0;
	r = ld->cancelReq;
	ld->cancelReq = 0;
	return r;
}

int UiLoading_Key(UiLoading *ld, int vk)
{
	if (!ld || !ld->cancelable || vk != VK_ESCAPE) return 0;
	if (ld->phase != UI_LOADING_SHOWN && ld->phase != UI_LOADING_HIDING)
		return 0;
	loading_request_cancel_(ld);
	return 1;
}

int UiLoading_Tick(UiLoading *ld, double nowMs)
{
	int redraw = 0;
	if (!ld) return 0;
	if (ld->pendingRedraw) {
		ld->pendingRedraw = 0;
		redraw = 1;
	}
	switch (ld->phase) {
	case UI_LOADING_PENDING:
		if (nowMs >= ld->showAt + (double)ld->delayMs) {
			ld->phase = UI_LOADING_SHOWN;
			ld->shownAt = nowMs;
			ld->shownDrawn = 0;
			redraw = 1;
		}
		break;
	case UI_LOADING_SHOWN:
		if (ld->hidePending &&
			nowMs >= ld->shownAt + (double)ld->minShowMs) {
			ld->phase = UI_LOADING_HIDING;
			ld->hideAt = nowMs;
			redraw = 1;
		}
		break;
	case UI_LOADING_HIDING:
		if (nowMs >= ld->hideAt + (double)UI_LOADING_FADE_MS) {
			ld->phase = UI_LOADING_HIDDEN;
			redraw = 1;
		}
		break;
	default:
		break;
	}
	/* 主窗回退: spinner 只能靠逐帧重绘推进 */
	if (ld->fallback && (ld->phase == UI_LOADING_SHOWN ||
		ld->phase == UI_LOADING_HIDING))
		redraw = 1;
	return redraw;
}

void UiLoading_Draw(UiLoading *ld, UiRect parentRc, double nowMs)
{
	UiContext *ui;
	UiTheme *th;
	UiId cid;
	int shown;
	if (!ld) return;
	ui = ld->ui;
	if (!ui || !ui->gfx || !ui->theme) return;
	ld->rc = parentRc;
	shown = ld->phase == UI_LOADING_SHOWN || ld->phase == UI_LOADING_HIDING;
	if (!shown) return;
	th = ui->theme;
	if (!ld->shownDrawn) {
		ld->shownDrawn = 1;
		loading_clear_hover_(ld, parentRc);
	}

	ld->btnHover = ld->btnPress = 0.0f;
	cid = Ui_Id("loading.cancel", ld->slot, 0);
	if (!ld->passThrough) {
		Ui_PushLayer(ui, UI_Z_OVERLAY);
		Ui_RegisterWidget(ui, Ui_Id("loading.blocker", ld->slot, 0),
			parentRc);
		Ui_RegisterWheelTarget(ui, Ui_Id("loading.blocker", ld->slot, 0),
			parentRc);
	}
	if (ld->cancelable) {
		LoadingLayout lay;
		UiWidgetState st;
		loading_layout_(ld, ui->gfx, th, parentRc, &lay);
		st = Ui_State(ui, cid, lay.btnHit, 1);
		ld->btnHover = st.hover;
		ld->btnPress = st.press;
		if (st.hovered) Ui_SetCursor(ui, cid, UI_CURSOR_HAND);
		if (ui->clicked == cid) loading_request_cancel_(ld);
	}
	if (!ld->passThrough) Ui_PopLayer(ui);

	if (ld->fallback) {
		float op = loading_fade_(ld, nowMs);
		double d = nowMs / 1000.0 * (double)UI_LOADING_SPIN_DEG_PER_SEC;
		long long k;
		int pushed;
		if (op > 0.002f) {
			k = (long long)(d / 360.0);
			d -= 360.0 * (double)k;
			pushed = Gfx_PushAlpha(ui->gfx, op);
			loading_draw_body_(ld, ui->gfx, parentRc, 1, (float)d,
				ld->btnHover, ld->btnPress);
			if (pushed) Gfx_PopAlpha(ui->gfx);
		}
	}
}

void UiLoading_EndFrame(UiLoading *ld, double nowMs)
{
	UiContext *ui;
	Gfx *g;
	float scale;
	int pw, ph;
	(void)nowMs;
	if (!ld) return;
	ui = ld->ui;
	if (!ui || !ui->gfx) return;
	if (ld->phase == UI_LOADING_HIDDEN) {
		loading_drop_(ld);
		return;
	}
	if (ld->phase == UI_LOADING_PENDING || ld->fallback) return;
	if (ld->rc.w <= 0.0f || ld->rc.h <= 0.0f) return;
	g = ui->gfx;
	scale = ui->scale > 0.0f ? ui->scale : 1.0f;
	pw = (int)(ld->rc.w * scale + 0.5f);
	ph = (int)(ld->rc.h * scale + 0.5f);
	if (pw <= 0 || ph <= 0) return;

	if (!ld->layer) {
		ld->layer = Gfx_CreateLayer(g, pw, ph);
		if (!ld->layer) {
			ld->fallback = 1;
			return;
		}
		ld->layerW = pw;
		ld->layerH = ph;
		ld->layerX = ld->layerY = -1.0e30f;
		ld->layerDirty = 1;
		ld->layerIn = 0;
		ld->layerOut = 0;
	} else if (pw != ld->layerW || ph != ld->layerH) {
		if (!Gfx_LayerResize(ld->layer, pw, ph)) {
			loading_drop_(ld);
			ld->fallback = 1;
			return;
		}
		ld->layerW = pw;
		ld->layerH = ph;
		ld->layerDirty = 1;
	}
	if (ld->layerX != ld->rc.x || ld->layerY != ld->rc.y) {
		Gfx_LayerSetPos(ld->layer, ld->rc.x, ld->rc.y);
		ld->layerX = ld->rc.x;
		ld->layerY = ld->rc.y;
	}
	if (ui->theme && ld->themeRev != ui->theme->themeRev) {
		ld->themeRev = ui->theme->themeRev;
		ld->layerDirty = 1;
		if (ld->spin) Gfx_PartUpdate(ld->spin);
	}
	if (ld->contentRev != ld->drawnRev ||
		ld->drawnHover != ld->btnHover ||
		ld->drawnPress != ld->btnPress)
		ld->layerDirty = 1;
	if (ld->layerDirty) {
		if (!Gfx_LayerDraw(ld->layer, loading_layer_draw_, ld)) {
			loading_drop_(ld);
			ld->fallback = 1;
			return;
		}
		ld->layerDirty = 0;
		ld->drawnRev = ld->contentRev;
		ld->drawnHover = ld->btnHover;
		ld->drawnPress = ld->btnPress;
	}
	if (!ld->spin)
		ld->spin = Gfx_PartCreateOn(ld->layer, UI_LOADING_SPINNER,
			UI_LOADING_SPINNER, loading_spin_draw_, ld);
	if (ld->spin) {
		LoadingLayout lay;
		loading_layout_(ld, g, ui->theme,
			Ui_Rect(0, 0, ld->rc.w, ld->rc.h), &lay);
		Gfx_PartPlace(ld->spin, lay.spin.x, lay.spin.y);
		Gfx_PartSpin(ld->spin, UI_LOADING_SPIN_DEG_PER_SEC);
	}
	if (ld->phase == UI_LOADING_SHOWN && !ld->layerIn) {
		Gfx_LayerSetOpacity(ld->layer, 1.0f, UI_LOADING_FADE_MS,
			GFX_EASE_OUT);
		ld->layerIn = 1;
		ld->layerOut = 0;
	} else if (ld->phase == UI_LOADING_HIDING && !ld->layerOut) {
		Gfx_LayerSetOpacity(ld->layer, 0.0f, UI_LOADING_FADE_MS,
			GFX_EASE_OUT);
		ld->layerOut = 1;
		ld->layerIn = 0;
	}
	if (!Gfx_LayerAlive(ld->layer)) {
		loading_drop_(ld);
		ld->fallback = 1;
	}
}
