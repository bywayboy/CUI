/*
 * ui_fx.c - 合成器过渡的输入侧登记 (SnapshotFx)
 *
 * 输入事件里登记“哪个矩形发生了视觉变化”, 绘制前一次性交给 gfx:
 * 由 gfx 抓旧/新快照, 合成器负责交叉淡化, UI 线程不参与逐帧动画。
 * 只在 DComp 路径生效; ULW/无合成器时退回 ui_anim 的 CPU 通道。
 */
#include "ui/ui_context.h"
#include "gfx/gfx_fx.h"

#define UI_FX_PAD     10.0f   /* 覆盖光晕/描边/抗锯齿外扩 */
#define UI_FX_MAX     8

int Ui_FxActive(UiContext *ctx)
{
	if (!ctx || !ctx->gfx || ctx->fxOff) return 0;
	return Gfx_FxEnabled(ctx->gfx);
}

static int fx_isect(UiRect a, UiRect b)
{
	if (a.w <= 0.0f || a.h <= 0.0f || b.w <= 0.0f || b.h <= 0.0f) return 0;
	return a.x < b.x + b.w && b.x < a.x + a.w &&
		a.y < b.y + b.h && b.y < a.y + a.h;
}

static UiRect fx_union(UiRect a, UiRect b)
{
	float x0 = a.x < b.x ? a.x : b.x;
	float y0 = a.y < b.y ? a.y : b.y;
	float x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
	float y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
	return Ui_Rect(x0, y0, x1 - x0, y1 - y0);
}

static UiRect fx_pad_clip(UiContext *ctx, UiRect r)
{
	r.x -= UI_FX_PAD;
	r.y -= UI_FX_PAD;
	r.w += UI_FX_PAD * 2.0f;
	r.h += UI_FX_PAD * 2.0f;
	if (r.x < 0.0f) { r.w += r.x; r.x = 0.0f; }
	if (r.y < 0.0f) { r.h += r.y; r.y = 0.0f; }
	if (r.x + r.w > ctx->width) r.w = ctx->width - r.x;
	if (r.y + r.h > ctx->height) r.h = ctx->height - r.y;
	if (r.w < 0.0f) r.w = 0.0f;
	if (r.h < 0.0f) r.h = 0.0f;
	return r;
}

void Ui_FxChangeRect(UiContext *ctx, UiRect rc, int durMs)
{
	int i;
	if (!Ui_FxActive(ctx) || durMs <= 0) return;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return;
	rc = fx_pad_clip(ctx, rc);
	if (rc.w <= 0.0f || rc.h <= 0.0f) return;
	/* 同一事件内相交的矩形合成一条过渡 (例如点击同一行既改 hover 又改选中) */
	for (i = 0; i < ctx->fxCount; i++) {
		if (!fx_isect(ctx->fxRect[i], rc)) continue;
		ctx->fxRect[i] = fx_union(ctx->fxRect[i], rc);
		if (durMs > ctx->fxDur[i]) ctx->fxDur[i] = durMs;
		Ui_ContextInvalidate(ctx, ctx->fxRect[i]);
		return;
	}
	if (ctx->fxCount >= UI_FX_MAX) return;   /* 极端情况丢一条, 不无限增长 */
	ctx->fxRect[ctx->fxCount] = rc;
	ctx->fxDur[ctx->fxCount] = durMs;
	ctx->fxCount++;
	Ui_ContextInvalidate(ctx, rc);
}

void Ui_FxChange(UiContext *ctx, UiId id, int durMs)
{
	UiRect rc;
	if (!ctx || !id) return;
	if (!Ui_WidgetRect(ctx, id, &rc)) return;
	Ui_FxChangeRect(ctx, rc, durMs);
}

void Ui_FxFlush(UiContext *ctx)
{
	int i;
	if (!ctx) return;
	if (!Ui_FxActive(ctx)) {
		ctx->fxCount = 0;
		return;
	}
	for (i = 0; i < ctx->fxCount; i++)
		Gfx_FxQueue(ctx->gfx, ctx->fxRect[i], ctx->fxDur[i]);
	ctx->fxCount = 0;
}

void Ui_FxCancelAll(UiContext *ctx)
{
	if (!ctx) return;
	ctx->fxCount = 0;
	if (ctx->gfx) Gfx_FxCancelAll(ctx->gfx);
}

void Ui_FxCancelRect(UiContext *ctx, UiRect rc)
{
	int i, w = 0;
	if (!ctx) return;
	for (i = 0; i < ctx->fxCount; i++) {
		if (fx_isect(ctx->fxRect[i], rc)) continue;
		ctx->fxRect[w] = ctx->fxRect[i];
		ctx->fxDur[w] = ctx->fxDur[i];
		w++;
	}
	ctx->fxCount = w;
	if (ctx->gfx) Gfx_FxCancelRect(ctx->gfx, rc);
}

void Ui_PartPlace(UiContext *ctx, void *part, float x, float y,
	float w, float h)
{
	GfxPart *p = (GfxPart *)part;
	if (!ctx || !p) return;
	if (Ui_Occluded(ctx, Ui_Rect(x, y, w, h))) {
		Gfx_PartSetVisible(p, 0);
		return;
	}
	Gfx_PartPlace(p, x, y);
}

void Ui_PartPlaceRect(UiContext *ctx, void *part, float x, float y,
	float w, float h)
{
	GfxPart *p = (GfxPart *)part;
	if (!ctx || !p) return;
	if (Ui_Occluded(ctx, Ui_Rect(x, y, w, h))) {
		Gfx_PartSetVisible(p, 0);
		return;
	}
	Gfx_PartPlaceRect(p, x, y, w);
}
