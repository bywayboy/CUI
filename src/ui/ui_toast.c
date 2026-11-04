#include <windows.h>
#include "ui/ui_toast.h"

static void toast_drop_effects(UiToast *t)
{
	if (t->fxShadow) {
		Gfx_EffectDestroy(t->fxShadow);
		t->fxShadow = NULL;
	}
	if (t->fxGrain) {
		Gfx_EffectDestroy(t->fxGrain);
		t->fxGrain = NULL;
	}
}

static void toast_drop_layer(UiToast *t)
{
	if (t && t->layer) {
		Gfx_LayerDestroy(t->layer);
		t->layer = NULL;
	}
	if (t) {
		t->layerW = t->layerH = 0;
		t->layerIn = t->layerOut = 0;
	}
}

static void toast_engage_fallback(UiToast *t)
{
	if (!t) return;
	t->fallback = 1;
	toast_drop_layer(t);
	/* modal blocker 保留: 回退绘制完成前也不让点击穿透; 结束时统一解除. */
}

static UiRect toast_card_rect(const UiToast *t)
{
	UiContext *ui = t->ui;
	float viewW = ui->width, viewH = ui->height;
	float w = viewW - 48.0f, h = 60.0f, x, y;
	if (w > 420.0f) w = 420.0f;
	if (w < 140.0f) w = 140.0f;
	x = (viewW - w) * 0.5f;
	y = viewH - 96.0f;
	if (y < 16.0f) y = 16.0f;
	if (y + h > viewH - 8.0f) y = viewH - 8.0f - h;
	return Ui_Rect(x, y, w, h);
}

static void toast_ensure_effects(UiToast *t, Gfx *g)
{
	if (t->fxShadow && !Gfx_EffectAlive(t->fxShadow)) {
		Gfx_EffectDestroy(t->fxShadow);
		t->fxShadow = NULL;
	}
	if (t->fxGrain && !Gfx_EffectAlive(t->fxGrain)) {
		Gfx_EffectDestroy(t->fxGrain);
		t->fxGrain = NULL;
	}
	if (!t->fxShadow) t->fxShadow = Gfx_CreateShadow(g, 10.0f, 0x66000000u);
	if (!t->fxGrain) t->fxGrain = Gfx_CreateGrain(g, 0.055f);
}

/* 软阴影的输入: 只画卡片轮廓, 颜色由 shadow effect 决定. */
static void toast_draw_shadow_shape(Gfx *g, void *user)
{
	UiToast *t = (UiToast *)user;
	UiRect card = toast_card_rect(t);
	Gfx_FillRoundRect(g, card, t->ui->theme->toastRadius, 0xFFFFFFFFu);
}

static void toast_draw_card_bg(Gfx *g, void *user)
{
	UiToast *t = (UiToast *)user;
	UiTheme *th = t->ui->theme;
	UiRect card = toast_card_rect(t);
	UiColor accent = th->blue2;
	if (t->kind == UI_TOAST_SUCCESS) accent = th->green;
	else if (t->kind == UI_TOAST_ERROR) accent = th->danger;
	Gfx_FillRoundRect(g, card, th->toastRadius, th->toastBg);
	Gfx_StrokeRoundRect(g, card, th->toastRadius, th->toastBorder, 1.0f);
	Gfx_FillRoundRect(g, Ui_Rect(card.x + 12.0f, card.y + 14.0f,
		4.0f, card.h - 28.0f), 2.0f, accent);
}

static void toast_draw_card_text(Gfx *g, void *user)
{
	UiToast *t = (UiToast *)user;
	UiTheme *th = t->ui->theme;
	UiRect card = toast_card_rect(t);
	Gfx_Text(g, th->fBody, t->text,
		Ui_Rect(card.x + 28.0f, card.y, card.w - 44.0f, card.h),
		th->toastText, GFX_TEXT_VCENTER);
}

static void toast_layer_draw(Gfx *g, void *user)
{
	UiToast *t = (UiToast *)user;
	UiContext *ui = t->ui;
	UiTheme *th = ui->theme;
	UiRect card;
	GfxEffect *shadow, *grain;

	/* 全窗渐变遮罩: 上透明, 下压暗 */
	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, ui->width, ui->height), 0.0f,
		Ui_ColorAlpha(th->toastOverlay, 0),
		Ui_ColorAlpha(th->toastOverlay, 170));

	card = toast_card_rect(t);
	toast_ensure_effects(t, g);

	/* 阴影只画一次: 输入是卡片轮廓, 输出是卡片外扩的软阴影. */
	shadow = t->fxShadow;
	if (shadow)
		Gfx_DrawWithEffects(g, &shadow, 1, toast_draw_shadow_shape, t, card);

	/* 卡片底板 + 强调条过一次颗粒; 文字随后直接画, 保持清晰. */
	grain = t->fxGrain;
	if (!grain || !Gfx_DrawWithEffects(g, &grain, 1, toast_draw_card_bg, t, card))
		toast_draw_card_bg(g, t);
	toast_draw_card_text(g, t);
}

/* 创建/改尺寸/重画/淡入淡出. 返回 0 表示 layer 不可用, 应退回主窗. */
static int toast_sync_layer(UiToast *t, double nowMs)
{
	UiContext *ui;
	UiTheme *th;
	float scale;
	int pw, ph;

	(void)nowMs;
	if (!t || !t->ui) return 0;
	ui = t->ui;
	if (!ui->gfx || !ui->theme) return 0;
	th = ui->theme;
	scale = ui->scale > 0.0f ? ui->scale : 1.0f;
	pw = (int)(ui->width * scale + 0.5f);
	ph = (int)(ui->height * scale + 0.5f);
	if (pw <= 0 || ph <= 0) return 1;   /* 布局还没就绪, 下一拍再试 */

	if (!t->layer) {
		t->layer = Gfx_CreateLayer(ui->gfx, pw, ph);
		if (!t->layer) return 0;
		t->layerW = pw;
		t->layerH = ph;
		t->layerDirty = 1;
	} else if (pw != t->layerW || ph != t->layerH) {
		if (!Gfx_LayerResize(t->layer, pw, ph)) return 0;
		t->layerW = pw;
		t->layerH = ph;
		t->layerDirty = 1;
	}

	if (t->themeRev != th->themeRev) {
		t->themeRev = th->themeRev;
		t->layerDirty = 1;
	}
	if (t->layerDirty) {
		if (!Gfx_LayerDraw(t->layer, toast_layer_draw, t)) return 0;
		t->layerDirty = 0;
	}

	if (t->hideStartMs) {
		if (!t->layerOut) {
			Gfx_LayerSetOpacity(t->layer, 0.0f, UI_TOAST_FADE_MS);
			t->layerOut = 1;
		}
	} else if (!t->layerIn) {
		Gfx_LayerSetOpacity(t->layer, 1.0f, UI_TOAST_FADE_MS);
		t->layerIn = 1;
	}
	return 1;
}

void UiToast_Init(UiToast *t)
{
	if (!t) return;
	ZeroMemory(t, sizeof(*t));
	t->blockInput = 1;
}

void UiToast_Attach(UiToast *t, UiContext *ui)
{
	if (!t) return;
	if (t->ui != ui) {
		toast_drop_layer(t);
		toast_drop_effects(t);
		t->fallback = 0;
	}
	t->ui = ui;
}

void UiToast_Show(UiToast *t, const char *text, double nowMs,
	int durationMs, int kind)
{
	if (!t) return;
	if (text && text[0])
		lstrcpynA(t->text, text, (int)sizeof(t->text));
	else
		t->text[0] = 0;
	t->startMs = nowMs;
	t->holdUntilMs = nowMs + (durationMs > 0 ? durationMs : 1);
	t->hideStartMs = 0.0;
	t->kind = kind;
	t->visible = 1;
	t->layerDirty = 1;
	t->layerIn = 0;
	t->layerOut = 0;
	if (t->ui) {
		t->fallback = 0;    /* 设备恢复后允许重新尝试 layer */
		if (t->ui->theme) t->themeRev = t->ui->theme->themeRev;
		if (t->blockInput) Ui_SetModal(t->ui, UiToast_BackdropId());
	}
}

void UiToast_Hide(UiToast *t, double nowMs)
{
	if (!t || !t->visible) return;
	if (!t->hideStartMs) {
		t->hideStartMs = nowMs;
		t->layerOut = 0;
	}
}

int UiToast_IsVisible(const UiToast *t)
{
	return t && t->visible;
}

int UiToast_Tick(UiToast *t, double nowMs)
{
	if (!t || !t->visible) return 0;
	if (!t->hideStartMs && nowMs >= t->holdUntilMs)
		UiToast_Hide(t, nowMs);
	if (t->hideStartMs && nowMs - t->hideStartMs >= UI_TOAST_FADE_MS) {
		t->visible = 0;
		if (t->ui && t->ui->modal == UiToast_BackdropId())
			Ui_SetModal(t->ui, UI_ID_NONE);
		toast_drop_layer(t);
		return 1;
	}
	if (t->ui && !t->fallback) {
		if (toast_sync_layer(t, nowMs)) return 0;
		toast_engage_fallback(t);
		return 1;           /* 本帧需要主窗回退绘制 */
	}
	if (t->hideStartMs) return 1;
	return (nowMs - t->startMs < UI_TOAST_FADE_MS) ? 1 : 0;
}

UiId UiToast_BackdropId(void)
{
	return Ui_Id("toast.backdrop", 0, 0);
}

UiId UiToast_CardId(void)
{
	return Ui_Id("toast.card", 0, 0);
}

void UiToast_Draw(UiContext *ui, UiToast *t, double nowMs,
	float viewW, float viewH)
{
	UiTheme *th;
	Gfx *g;
	UiColor accent;
	float fin, fout, a, off, cardW, cardH, x, y;
	UiRect card;
	int pushed;

	if (!ui || !ui->theme || !ui->gfx || !t || !t->visible) return;

	/* layer 模式: 像素都在合成器上, 主窗只登记不可见 blocker */
	if (t->ui && !t->fallback) {
		if (t->blockInput) Ui_SetModal(ui, UiToast_BackdropId());
		return;
	}

	th = ui->theme;
	g = ui->gfx;

	fin = (float)((nowMs - t->startMs) / (double)UI_TOAST_FADE_MS);
	if (fin < 0.0f) fin = 0.0f;
	if (fin > 1.0f) fin = 1.0f;
	fout = 0.0f;
	if (t->hideStartMs) {
		fout = (float)((nowMs - t->hideStartMs) / (double)UI_TOAST_FADE_MS);
		if (fout < 0.0f) fout = 0.0f;
		if (fout > 1.0f) fout = 1.0f;
	}
	a = fin * (1.0f - fout);
	if (a < 0.002f) return;
	off = (1.0f - fin) * 12.0f + fout * 12.0f;

	pushed = Gfx_PushAlpha(g, a);

	/* 全窗渐变遮罩: 上透明, 下压暗; 默认吞掉鼠标 */
	Ui_PushLayer(ui, UI_Z_TOAST - 1);
	if (t->blockInput)
		Ui_RegisterWidget(ui, UiToast_BackdropId(), Ui_Rect(0, 0, viewW, viewH));
	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, viewW, viewH), 0.0f,
		Ui_ColorAlpha(th->toastOverlay, 0),
		Ui_ColorAlpha(th->toastOverlay, 170));
	Ui_PopLayer(ui);

	/* 底部居中卡片 */
	cardW = viewW - 48.0f;
	if (cardW > 420.0f) cardW = 420.0f;
	if (cardW < 140.0f) cardW = 140.0f;
	cardH = 60.0f;
	x = (viewW - cardW) * 0.5f;
	y = viewH - 96.0f + off;
	if (y < 16.0f) y = 16.0f;
	if (y + cardH > viewH - 8.0f) y = viewH - 8.0f - cardH;
	card = Ui_Rect(x, y, cardW, cardH);

	Ui_PushLayer(ui, UI_Z_TOAST);
	Ui_RegisterWidget(ui, UiToast_CardId(), card);
	Gfx_FillRoundRect(g, card, th->toastRadius, th->toastBg);
	Gfx_StrokeRoundRect(g, card, th->toastRadius, th->toastBorder, 1.0f);

	accent = th->blue2;
	if (t->kind == UI_TOAST_SUCCESS) accent = th->green;
	else if (t->kind == UI_TOAST_ERROR) accent = th->danger;
	Gfx_FillRoundRect(g, Ui_Rect(card.x + 12.0f, card.y + 14.0f,
		4.0f, card.h - 28.0f), 2.0f, accent);
	Gfx_Text(g, th->fBody, t->text,
		Ui_Rect(card.x + 28.0f, card.y, card.w - 44.0f, card.h),
		th->toastText, GFX_TEXT_VCENTER);
	Ui_PopLayer(ui);

	if (pushed) Gfx_PopAlpha(g);
}

void UiToast_EndFrame(UiToast *t, double nowMs, float viewW, float viewH)
{
	if (!t) return;
	if (t->ui && viewW > 0.0f && viewH > 0.0f) {
		t->ui->width = viewW;
		t->ui->height = viewH;
	}
	if (!t->visible || !t->ui || t->fallback) return;
	if (!toast_sync_layer(t, nowMs))
		toast_engage_fallback(t);
}

int UiToast_UsingLayer(const UiToast *t)
{
	return t && t->ui && !t->fallback;
}

void UiToast_Shutdown(UiToast *t)
{
	if (!t) return;
	if (t->ui && t->ui->modal == UiToast_BackdropId())
		Ui_SetModal(t->ui, UI_ID_NONE);
	toast_drop_layer(t);
	toast_drop_effects(t);
	t->visible = 0;
}
