#include "ui/ui_context.h"
#include "log.h"
#include <string.h>

void *realloc(void *p, size_t sz);

static UiAnimEntry *anim_reserve(UiContext *ctx, int need)
{
	UiAnimEntry *p;
	int cap;
	if (!ctx) return 0;
	if (need <= ctx->animCap) return ctx->anim;
	cap = ctx->animCap > 0 ? ctx->animCap : UI_AN_INITIAL_ENTRIES;
	while (cap < need) cap *= 2;
	p = (UiAnimEntry *)realloc(ctx->anim, (size_t)cap * sizeof(*p));
	if (!p) return 0;
	ZeroMemory(p + ctx->animCap,
		(size_t)(cap - ctx->animCap) * sizeof(*p));
	ctx->anim = p;
	ctx->animCap = cap;
	return p;
}

/* 各通道的平滑时间常数 (ms)。交互按"按下最快、光晕最慢"排, 手感更自然。 */
float Ui_AnimChanTauMs(int ch)
{
	switch (ch) {
	case UI_AN_HOVER:  return 90.0f;
	case UI_AN_PRESS:  return 55.0f;
	case UI_AN_FOCUS:  return 120.0f;
	case UI_AN_GLOW:   return 160.0f;
	default:           return 140.0f;
	}
}

static float chan_tau(int ch)
{
	return Ui_AnimChanTauMs(ch);
}

/* 3τ ≈ 指数平滑到达 95%: 合成器覆盖层走完这么长, CPU 降级也基本收敛。
 * 两条渲染路径用同一个时长, 不能一边 120ms 一边 250ms。 */
int Ui_AnimFxMs(int ch)
{
	return (int)(Ui_AnimChanTauMs(ch) * 3.0f + 0.5f);
}

int Ui_AnimTweenMs(int kind)
{
	switch (kind) {
	case UI_AN_TWEEN_PANEL:   return 160;
	case UI_AN_TWEEN_CONTENT: return 140;
	case UI_AN_TWEEN_ARROW:   return 180;
	default:                  return 160;
	}
}

static float fabsf_(float v) { return v < 0.0f ? -v : v; }

/* 平滑收敛阈值: 交互强度最终映射到 8bit 颜色, 0.008 ≈ 2 个色阶 —— 再迭代也
 * 画不出肉眼可辨的差异, 只会白白多跑十几帧。进度是 0..1 的位置量, 保留更细的
 * 阈值, 免得进度条收尾时出现一小步跳进。 */
static float snap_eps(int ch)
{
	return ch == UI_AN_PROGRESS ? 0.0015f : 0.008f;
}

/* 动画脏区向外扩: 覆盖光晕外扩 (glowW≤4) + 描边 + 状态文字上滑 (6) + 抗锯齿 */
#define AN_DIRTY_PAD 10.0f

static void dirty_add(UiContext *ctx, UiRect r)
{
	UiRect d;
	if (!ctx) return;
	d.x = r.x - AN_DIRTY_PAD;
	d.y = r.y - AN_DIRTY_PAD;
	d.w = r.w + AN_DIRTY_PAD * 2.0f;
	d.h = r.h + AN_DIRTY_PAD * 2.0f;
	if (!ctx->animDirtyHas) {
		ctx->animDirty = d;
		ctx->animDirtyHas = 1;
		return;
	}
	{
		float x0 = ctx->animDirty.x < d.x ? ctx->animDirty.x : d.x;
		float y0 = ctx->animDirty.y < d.y ? ctx->animDirty.y : d.y;
		float x1 = ctx->animDirty.x + ctx->animDirty.w;
		float x2 = d.x + d.w;
		float y1 = ctx->animDirty.y + ctx->animDirty.h;
		float y2 = d.y + d.h;
		if (x2 > x1) x1 = x2;
		if (y2 > y1) y1 = y2;
		ctx->animDirty.x = x0;
		ctx->animDirty.y = y0;
		ctx->animDirty.w = x1 - x0;
		ctx->animDirty.h = y1 - y0;
	}
}

static float ease_apply(int ease, float p)
{
	if (p <= 0.0f) return 0.0f;
	if (p >= 1.0f) return 1.0f;
	if (ease == UI_AN_EASE_OUT) {
		float q = 1.0f - p;
		return 1.0f - q * q * q;
	}
	if (ease == UI_AN_EASE_INOUT) {
		if (p < 0.5f) return 4.0f * p * p * p;
		{
			float q = 1.0f - p;
			return 1.0f - 4.0f * q * q * q;
		}
	}
	return p;
}

static UiAnimEntry *entry_find(UiContext *ctx, UiId id, int create)
{
	int i, freeIdx = -1;
	UiAnimEntry *pool;
	if (!ctx || !id) return 0;
	for (i = 0; i < ctx->animCap; i++) {
		UiAnimEntry *e = &ctx->anim[i];
		if (e->used && e->id == id) {
			e->seen = ctx->animFrame;
			return e;
		}
		if (!e->used && freeIdx < 0) freeIdx = i;
	}
	if (!create) return 0;
	if (freeIdx < 0) {
		freeIdx = ctx->animCap;
		pool = anim_reserve(ctx, freeIdx + 1);
		if (!pool) return 0;
	}
	{
		UiAnimEntry *e = &ctx->anim[freeIdx];
		memset(e, 0, sizeof(*e));
		e->used = 1;
		e->id = id;
		e->seen = ctx->animFrame;
		return e;
	}
}

void Ui_AnimSetReduced(UiContext *ctx, int reduced)
{
	if (ctx) ctx->animReduced = reduced ? 1 : 0;
}

void Ui_AnimSet(UiContext *ctx, UiId id, int ch, float v)
{
	UiAnimEntry *e;
	if (!ctx || ch < 0 || ch >= UI_AN_CH) return;
	e = entry_find(ctx, id, 1);
	if (!e) return;
	e->ch[ch].cur = v;
	e->ch[ch].target = v;
	e->ch[ch].mode = UI_AN_IDLE;
	e->ch[ch].inited = 1;
}

void Ui_AnimSetRect(UiContext *ctx, UiId id, UiRect rc)
{
	UiAnimEntry *e;
	if (!ctx || !id) return;
	e = entry_find(ctx, id, 1);
	if (!e) return;
	e->rect = rc;
	e->hasRect = 1;
}

int Ui_AnimTakeDirty(UiContext *ctx, UiRect *out)
{
	int kind = 0;
	if (!ctx) return 0;
	if (ctx->animDirtyFull) kind = 2;
	else if (ctx->animDirtyHas) {
		if (out) *out = ctx->animDirty;
		kind = 1;
	}
	ctx->animDirtyHas = 0;
	ctx->animDirtyFull = 0;
	return kind;
}

void Ui_AnimDirtyRect(UiContext *ctx, UiRect r)
{
	dirty_add(ctx, r);
}

void Ui_AnimSmooth(UiContext *ctx, UiId id, int ch, float target)
{
	UiAnimEntry *e;
	UiAnimChan *c;
	if (!ctx || ch < 0 || ch >= UI_AN_CH) return;
	e = entry_find(ctx, id, 1);
	if (!e) return;
	c = &e->ch[ch];
	if (ctx->animReduced) {
		c->cur = c->target = target;
		c->mode = UI_AN_IDLE;
		c->inited = 1;
		return;
	}
	/* 已经到位: 保持 IDLE, 避免每帧重绘都触发一次"动画" (空闲时不该占用帧时钟) */
	if (c->cur == target && c->mode != UI_AN_TWEEN) {
		c->target = target;
		c->mode = UI_AN_IDLE;
		c->inited = 1;
		return;
	}
	/* 目标没变就不重置, 否则每帧都会把进度清零 */
	if (c->mode == UI_AN_SMOOTH && c->target == target) { c->inited = 1; return; }
	c->target = target;
	c->mode = UI_AN_SMOOTH;
	c->inited = 1;
}

void Ui_AnimTo(UiContext *ctx, UiId id, int ch, float target, int ms, int ease)
{
	UiAnimEntry *e;
	UiAnimChan *c;
	if (!ctx || ch < 0 || ch >= UI_AN_CH) return;
	e = entry_find(ctx, id, 1);
	if (!e) return;
	c = &e->ch[ch];
	if (ctx->animReduced || ms <= 0) {
		c->cur = c->target = target;
		c->mode = UI_AN_IDLE;
		c->inited = 1;
		return;
	}
	c->from = c->cur;
	c->to = target;
	c->target = target;
	c->t0 = (uint32_t)ctx->animTimeMs;
	c->dur = (uint32_t)ms;
	c->ease = (uint8_t)ease;
	c->mode = UI_AN_TWEEN;
	c->inited = 1;
}

float Ui_AnimGet(UiContext *ctx, UiId id, int ch, float def)
{
	UiAnimEntry *e;
	if (!ctx || ch < 0 || ch >= UI_AN_CH) return def;
	e = entry_find(ctx, id, 1);
	if (!e) return def;
	if (!e->ch[ch].inited) return def;
	return e->ch[ch].cur;
}

void Ui_AnimAdvance(UiContext *ctx, float dtMs)
{
	int i, ch;
	if (!ctx) return;
	if (dtMs < 0.0f) dtMs = 0.0f;
	if (dtMs > 100.0f) dtMs = 100.0f;   /* 拖动窗口/断点后不要瞬移 */
	ctx->animTimeMs += (double)dtMs;
	ctx->animFrame++;
	/* 不在 Advance 里清脏区: 组件可能在上一帧绘制 (WM_PAINT) 时登记,
	 * 由 Ui_AnimTakeDirty 统一取走, 否则逐帧自绘的 loading 动画会丢帧。 */

	for (i = 0; i < ctx->animCap; i++) {
		UiAnimEntry *e = &ctx->anim[i];
		int moved = 0;
		if (!e->used) continue;
		for (ch = 0; ch < UI_AN_CH; ch++) {
			UiAnimChan *c = &e->ch[ch];
			float before = c->cur;
			if (c->mode == UI_AN_SMOOTH) {
				float tau = chan_tau(ch);
				float a = dtMs / (tau + dtMs);
				c->cur += (c->target - c->cur) * a;
				if (fabsf_(c->target - c->cur) < snap_eps(ch)) {
					c->cur = c->target;
					c->mode = UI_AN_IDLE;
				}
			} else if (c->mode == UI_AN_TWEEN) {
				float p = c->dur
					? (float)((ctx->animTimeMs - (double)c->t0) / (double)c->dur)
					: 1.0f;
				if (p >= 1.0f) {
					c->cur = c->to;
					c->mode = UI_AN_IDLE;
				} else if (p <= 0.0f) {
					c->cur = c->from;
				} else {
					c->cur = c->from + (c->to - c->from) * ease_apply(c->ease, p);
				}
			}
			if (c->cur != before) moved = 1;
		}
		if (moved) {
			if (e->hasRect) dirty_add(ctx, e->rect);
			else ctx->animDirtyFull = 1;
		}
		/* GC: 空闲且 5 秒没被访问的条目回收 (控件消失后不残留) */
		if (ctx->animFrame - e->seen > 300) {
			int busy = 0;
			for (ch = 0; ch < UI_AN_CH; ch++)
				if (e->ch[ch].mode != UI_AN_IDLE) { busy = 1; break; }
			if (!busy) e->used = 0;
		}
	}
}

int Ui_AnimBusy(UiContext *ctx)
{
	int i, ch;
	if (!ctx) return 0;
	for (i = 0; i < ctx->animCap; i++) {
		UiAnimEntry *e = &ctx->anim[i];
		if (!e->used) continue;
		for (ch = 0; ch < UI_AN_CH; ch++)
			if (e->ch[ch].mode != UI_AN_IDLE) return 1;
	}
	return 0;
}

