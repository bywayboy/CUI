#include "ui/ui_context.h"

void *realloc(void *p, size_t sz);
void free(void *p);

static int rect_intersect(UiRect a, UiRect b, UiRect *out);

static int reserve_widgets(UiContext *ctx, int need)
{
	UiWidget *p;
	int cap;
	if (!ctx || need <= ctx->widgetCap) return ctx ? 1 : 0;
	cap = ctx->widgetCap > 0 ? ctx->widgetCap : UI_WIDGET_INITIAL_CAP;
	while (cap < need) cap *= 2;
	p = (UiWidget *)realloc(ctx->widgets, (size_t)cap * sizeof(*p));
	if (!p) {
		ctx->widgetOom = 1;
		return 0;
	}
	ZeroMemory(p + ctx->widgetCap,
		(size_t)(cap - ctx->widgetCap) * sizeof(*p));
	ctx->widgets = p;
	ctx->widgetCap = cap;
	return 1;
}

static int reserve_wheel_targets(UiContext *ctx, int need)
{
	UiWheelTarget *p;
	int cap;
	if (!ctx || need <= ctx->wheelCap) return ctx ? 1 : 0;
	cap = ctx->wheelCap > 0 ? ctx->wheelCap : UI_WHEEL_INITIAL_CAP;
	while (cap < need) cap *= 2;
	p = (UiWheelTarget *)realloc(ctx->wheelTargets,
		(size_t)cap * sizeof(*p));
	if (!p) return 0;
	ZeroMemory(p + ctx->wheelCap,
		(size_t)(cap - ctx->wheelCap) * sizeof(*p));
	ctx->wheelTargets = p;
	ctx->wheelCap = cap;
	return 1;
}

/* 只失效一个控件矩形；找不到（控件集合变化）时退回整窗，保证正确性。 */
static void InvalidateWidget(UiContext *ctx, UiId id)
{
	if (!ctx || !ctx->hwnd || !id) return;
	for (int i = ctx->widgetCount - 1; i >= 0; i--) {
		if (ctx->widgets[i].id != id) continue;
		UiRect d = ctx->widgets[i].rect;
		float s = ctx->scale > 0.0f ? ctx->scale : 1.0f;
		/* 10 DIP: 覆盖光晕外扩 (glowW 4 + 描边) 与动画脏区的扩边,
		 * 只留 2 DIP 会在光晕淡出后留下旧光圈。 */
		int m = (int)(10.0f * s + 0.5f);
		RECT r;
		r.left = (LONG)(d.x * s) - m;
		r.top = (LONG)(d.y * s) - m;
		r.right = (LONG)((d.x + d.w) * s) + m;
		r.bottom = (LONG)((d.y + d.h) * s) + m;
		InvalidateRect(ctx->hwnd, &r, FALSE);
		return;
	}
	InvalidateRect(ctx->hwnd, NULL, FALSE);
}

void Ui_ContextInvalidate(UiContext *ctx, UiRect rc)
{
	float s;
	int m;
	RECT r;

	if (!ctx || !ctx->hwnd || rc.w <= 0.0f || rc.h <= 0.0f) return;
	s = ctx->scale > 0.0f ? ctx->scale : 1.0f;
	m = (int)(10.0f * s + 0.5f);
	r.left = (LONG)(rc.x * s) - m;
	r.top = (LONG)(rc.y * s) - m;
	r.right = (LONG)((rc.x + rc.w) * s) + m;
	r.bottom = (LONG)((rc.y + rc.h) * s) + m;
	InvalidateRect(ctx->hwnd, &r, FALSE);
}

void Ui_ContextInit(UiContext *ctx, HWND hwnd, Gfx *gfx, UiTheme *theme)
{	ZeroMemory(ctx, sizeof(*ctx));
	ctx->hwnd = hwnd;
	ctx->gfx = gfx;
	ctx->theme = theme;
	if (theme) theme->notifyHwnd = hwnd;   /* 全局换肤后自动失效该窗口 */
	ctx->scale = 1.0f;
	ctx->popupOpen = 0;
	ctx->focusId = UI_ID_NONE;
}

void Ui_ContextShutdown(UiContext *ctx)
{
	if (!ctx) return;
	if (ctx->widgets) {
		free(ctx->widgets);
		ctx->widgets = NULL;
	}
	if (ctx->wheelTargets) {
		free(ctx->wheelTargets);
		ctx->wheelTargets = NULL;
	}
	if (ctx->anim) {
		free(ctx->anim);
		ctx->anim = NULL;
	}
	ctx->widgetCount = ctx->widgetCap = 0;
	ctx->wheelTargetCount = ctx->wheelCap = 0;
	ctx->animCap = 0;
	ctx->widgetOom = 0;
	ctx->inputOwner = UI_ID_NONE;
	ctx->inputOwnerDepth = 0;
	ctx->inputScope = UI_ID_NONE;
}

void Ui_SetFocusId(UiContext *ctx, UiId id)
{
	UiId old;
	if (!ctx || ctx->focusId == id) return;
	old = ctx->focusId;
	ctx->focusId = id;
	/* 焦点切换同样是视觉变化 (边框换色 + 光晕亮起). 合成器路径下
	 * Ui_DrawSurface 直接落终值, 不登记快照过渡就会瞬跳. */
	if (old) Ui_FxChange(ctx, old, Ui_AnimFxMs(UI_AN_FOCUS));
	if (id) Ui_FxChange(ctx, id, Ui_AnimFxMs(UI_AN_FOCUS));
}

void Ui_SetNavId(UiContext *ctx, UiId id)
{
	UiId old;
	if (!ctx) return;
	old = ctx->navId;
	ctx->navId = id;
	ctx->navFocusVisible = id ? 1 : 0;
	if (old == id) return;
	/* 键盘焦点环/控件焦点色的显隐也走同一条快照过渡. */
	if (old) Ui_FxChange(ctx, old, Ui_AnimFxMs(UI_AN_HOVER));
	if (id) Ui_FxChange(ctx, id, Ui_AnimFxMs(UI_AN_HOVER));
}

void Ui_SetModal(UiContext *ctx, UiId id)
{
	if (!ctx || ctx->modal == id) return;
	ctx->modal = id;
	ctx->hover = id ? id : UI_ID_NONE;
	ctx->pressed = UI_ID_NONE;
	ctx->active = UI_ID_NONE;
	ctx->down = UI_ID_NONE;
	ctx->released = UI_ID_NONE;
	ctx->mouseDown = 0;
}

void Ui_SetViewport(UiContext *ctx, float w, float h, float scale)
{
	if (!ctx) return;
	ctx->width = w;
	ctx->height = h;
	ctx->scale = scale > 0.0f ? scale : 1.0f;
}

int Ui_PushClipRect(UiContext *ctx, UiRect rc)
{
	UiRect clipped;
	if (!ctx || rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	if (ctx->hitClipDepth >= UI_MAX_CLIP_DEPTH) return 0;
	if (ctx->hitClip.w < 0.0f) {
		clipped = Ui_Rect(-1, -1, 0, 0);
	} else if (ctx->hitClip.w > 0.0f && ctx->hitClip.h > 0.0f) {
		if (!rect_intersect(ctx->hitClip, rc, &clipped))
			clipped = Ui_Rect(-1, -1, 0, 0);
	} else {
		clipped = rc;
	}
	ctx->hitClipStack[ctx->hitClipDepth++] = ctx->hitClip;
	ctx->hitClip = clipped;
	return 1;
}

void Ui_PopClipRect(UiContext *ctx)
{
	if (!ctx || ctx->hitClipDepth <= 0) return;
	ctx->hitClip = ctx->hitClipStack[--ctx->hitClipDepth];
}

int Ui_PushInputOwner(UiContext *ctx, UiId owner)
{
	if (!ctx || !owner) return 0;
	if (ctx->inputOwnerDepth >= (int)(sizeof(ctx->inputOwnerStack) /
		sizeof(ctx->inputOwnerStack[0]))) return 0;
	ctx->inputOwnerStack[ctx->inputOwnerDepth++] = owner;
	ctx->inputOwner = owner;
	return 1;
}

void Ui_PopInputOwner(UiContext *ctx, UiId owner)
{
	int i;
	if (!ctx) return;
	for (i = ctx->inputOwnerDepth - 1; i >= 0; i--) {
		int j;
		if (ctx->inputOwnerStack[i] != owner) continue;
		for (j = i + 1; j < ctx->inputOwnerDepth; j++)
			ctx->inputOwnerStack[j - 1] = ctx->inputOwnerStack[j];
		ctx->inputOwnerDepth--;
		break;
	}
	ctx->inputOwner = ctx->inputOwnerDepth > 0
		? ctx->inputOwnerStack[ctx->inputOwnerDepth - 1] : UI_ID_NONE;
}

int Ui_InputAllowed(const UiContext *ctx)
{
	return ctx && ctx->inputBlock == 0 &&
		(!ctx->inputOwner || ctx->inputScope == ctx->inputOwner);
}

void Ui_PushInputBlock(UiContext *ctx)
{
	if (ctx) ctx->inputBlock++;
}

void Ui_PopInputBlock(UiContext *ctx)
{
	if (ctx && ctx->inputBlock > 0) ctx->inputBlock--;
}

void Ui_RegisterWheelTarget(UiContext *ctx, UiId id, UiRect rc)
{
	if (!ctx || !id || rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (!Ui_InputAllowed(ctx)) return;
	if (!reserve_wheel_targets(ctx, ctx->wheelTargetCount + 1)) return;
	ctx->wheelTargets[ctx->wheelTargetCount].id = id;
	ctx->wheelTargets[ctx->wheelTargetCount].rect = rc;
	ctx->wheelTargets[ctx->wheelTargetCount].clip =
		(ctx->hitClipDepth > 0 && ctx->hitClip.w > 0.0f)
		? ctx->hitClip : Ui_Rect(0, 0, 0, 0);
	ctx->wheelTargets[ctx->wheelTargetCount].layer = ctx->layer;
	ctx->wheelTargets[ctx->wheelTargetCount].scope = ctx->inputScope;
	ctx->wheelTargetCount++;
}

void Ui_OnMouseWheel(UiContext *ctx, float x, float y, float delta)
{
	UiId target = UI_ID_NONE;
	UiRect tr = { 0, 0, 0, 0 };
	RECT r;
	float s;
	if (!ctx || delta == 0.0f) return;
	ctx->mouseX = x;
	ctx->mouseY = y;
	ctx->mouseOver = 1;
	if (ctx->modal) return;
	for (int i = ctx->wheelTargetCount - 1; i >= 0; i--) {
		UiWheelTarget *wt = &ctx->wheelTargets[i];
		if (ctx->inputOwner && wt->scope != ctx->inputOwner) continue;
		if (ctx->popupOpen && wt->layer != UI_LAYER_OVERLAY) continue;
		if (!Ui_RectContains(wt->rect, x, y)) continue;
		if (wt->clip.w > 0.0f && wt->clip.h > 0.0f &&
			!Ui_RectContains(wt->clip, x, y)) continue;
		target = wt->id;
		tr = wt->rect;
		break;
	}
	if (!target) return;
	/* 内容滚动后过渡矩形会与新内容错位, 只撤销该区域 */
	Ui_FxCancelRect(ctx, tr);
	ctx->wheelTarget = target;
	ctx->wheelDelta += delta;
	if (ctx->hwnd) {
		s = ctx->scale > 0.0f ? ctx->scale : 1.0f;
		r.left = (LONG)((tr.x - 2.0f) * s);
		r.top = (LONG)((tr.y - 2.0f) * s);
		r.right = (LONG)((tr.x + tr.w + 2.0f) * s) + 1;
		r.bottom = (LONG)((tr.y + tr.h + 2.0f) * s) + 1;
		InvalidateRect(ctx->hwnd, &r, FALSE);
	}
}

void Ui_BeginFrame(UiContext *ctx)
{
	if (!ctx) return;
	/* 兜底: WM_MOUSELEAVE 丢失时, 用真实光标位置判断是否已离开客户区,
	 * 避免 hover/自动隐藏滚动条一直停在最后一帧. */
	if (ctx->hwnd) {
		POINT p;
		RECT rc;
		if (GetCursorPos(&p) && GetClientRect(ctx->hwnd, &rc)) {
			ScreenToClient(ctx->hwnd, &p);
			if (p.x < rc.left || p.y < rc.top ||
				p.x >= rc.right || p.y >= rc.bottom)
				ctx->mouseOver = 0;
		}
	}
	ctx->widgetCount = 0;
	ctx->layer = UI_LAYER_BASE;
	ctx->z = UI_Z_BASE;
	ctx->zDepth = 0;
	ctx->hitClip = Ui_Rect(0, 0, 0, 0);
	ctx->hitClipDepth = 0;
	ctx->wheelTargetCount = 0;
	ctx->inputBlock = 0;
	ctx->inputScope = UI_ID_NONE;
}

void Ui_EndFrame(UiContext *ctx)
{
	int i;
	if (!ctx) return;
	if (ctx->released) ctx->drag.active = 0;
	/* 遮挡矩形给下一帧的 part 使用 (弹层面板在本帧后段绘制) */
	for (i = 0; i < ctx->fxCutCount; i++)
		ctx->fxCutPrev[i] = ctx->fxCut[i];
	ctx->fxCutPrevCount = ctx->fxCutCount;
	ctx->fxCutCount = 0;
	ctx->clicked = UI_ID_NONE;
	ctx->down = UI_ID_NONE;
	ctx->released = UI_ID_NONE;
	ctx->clickedOutside = 0;
	ctx->layer = UI_LAYER_BASE;
	ctx->z = UI_Z_BASE;
	ctx->zDepth = 0;
	ctx->hitClip = Ui_Rect(0, 0, 0, 0);
	ctx->hitClipDepth = 0;
	ctx->inputBlock = 0;
	ctx->inputScope = UI_ID_NONE;
	/* 未被任何 ScrollView 消费的滚轮到帧末丢弃. */
	ctx->wheelTarget = UI_ID_NONE;
	ctx->wheelDelta = 0.0f;
}

void Ui_PushLayer(UiContext *ctx, int z)
{
	if (!ctx) return;
	if (ctx->zDepth < (int)(sizeof(ctx->zStack) / sizeof(ctx->zStack[0]))) {
		ctx->zStack[ctx->zDepth] = ctx->z;
		ctx->layerStack[ctx->zDepth] = ctx->layer;
		ctx->zDepth++;
	}
	ctx->z = z;
	ctx->layer = (z >= UI_Z_OVERLAY) ? UI_LAYER_OVERLAY : UI_LAYER_BASE;
}

void Ui_PopLayer(UiContext *ctx)
{
	if (!ctx) return;
	if (ctx->zDepth > 0) {
		ctx->zDepth--;
		ctx->z = ctx->zStack[ctx->zDepth];
		ctx->layer = ctx->layerStack[ctx->zDepth];
	} else {
		ctx->z = UI_Z_BASE;
		ctx->layer = UI_LAYER_BASE;
	}
}

static int rect_intersect(UiRect a, UiRect b, UiRect *out)
{
	float x0 = a.x > b.x ? a.x : b.x;
	float y0 = a.y > b.y ? a.y : b.y;
	float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
	float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
	if (x1 <= x0 || y1 <= y0) return 0;
	out->x = x0; out->y = y0; out->w = x1 - x0; out->h = y1 - y0;
	return 1;
}

void Ui_RegisterWidget(UiContext *ctx, UiId id, UiRect rc)
{
	UiWidget *w;
	if (!ctx || !id || rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (!Ui_InputAllowed(ctx)) return;
	if (ctx->hitClipDepth > 0) {
		/* w<0 = 被裁剪到空, 整个控件不可命中/注册. */
		if (ctx->hitClip.w < 0.0f) return;
	}
	if (!reserve_widgets(ctx, ctx->widgetCount + 1)) return;
	w = &ctx->widgets[ctx->widgetCount++];
	w->id = id;
	w->rect = rc;
	w->clip = (ctx->hitClipDepth > 0 && ctx->hitClip.w > 0.0f)
		? ctx->hitClip : Ui_Rect(0, 0, 0, 0);
	w->layer = ctx->layer;
	w->z = ctx->z;
	w->cursor = UI_CURSOR_ARROW;
	w->pressPoll = 0;
	w->hoverFn = NULL;
	w->hoverUser = NULL;
	w->clickFn = NULL;
	w->clickUser = NULL;
	w->scope = ctx->inputScope;
}

/* 控件是否仍在上一帧的注册表里. 找不到 (控件集合已变) 时不能失效/过渡. */
static int widget_registered(const UiContext *ctx, UiId id)
{
	int i;
	if (!ctx || !id) return 0;
	for (i = ctx->widgetCount - 1; i >= 0; i--)
		if (ctx->widgets[i].id == id) return 1;
	return 0;
}

int Ui_WidgetRect(UiContext *ctx, UiId id, UiRect *out)
{
	int i;
	if (!ctx || !id || !out) return 0;
	for (i = ctx->widgetCount - 1; i >= 0; i--) {
		if (ctx->widgets[i].id != id) continue;
		*out = ctx->widgets[i].rect;
		return 1;
	}
	return 0;
}

void Ui_SetClickFn(UiContext *ctx, UiId id, UiClickFn fn, void *user)
{
	int i;
	if (!ctx || !id) return;
	for (i = ctx->widgetCount - 1; i >= 0; i--) {
		if (ctx->widgets[i].id != id) continue;
		ctx->widgets[i].clickFn = fn;
		ctx->widgets[i].clickUser = user;
		return;
	}
}

/* 命中控件下标; -1 = 无. 从后往前: 同 z 时后注册(后画)的优先. */
static int widget_at(const UiContext *ctx, float x, float y)
{
	int best = -1;
	int bestZ = -2147483647;
	if (!ctx) return -1;
	for (int i = ctx->widgetCount - 1; i >= 0; i--) {
		const UiWidget *w = &ctx->widgets[i];
		if (ctx->inputOwner && w->scope != ctx->inputOwner) continue;
		if (ctx->popupOpen && w->layer != UI_LAYER_OVERLAY) continue;
		if (!Ui_RectContains(w->rect, x, y)) continue;
		if (w->clip.w < 0.0f) continue;
		if (w->clip.w > 0.0f && w->clip.h > 0.0f &&
			!Ui_RectContains(w->clip, x, y)) continue;
		if (w->z > bestZ) {
			bestZ = w->z;
			best = i;
		}
	}
	return best;
}

UiId Ui_HitTest(const UiContext *ctx, float x, float y)
{
	int i;
	if (!ctx) return UI_ID_NONE;
	if (ctx->modal) return ctx->modal;
	i = widget_at(ctx, x, y);
	if (i >= 0) return ctx->widgets[i].id;
	return ctx->inputOwner && !ctx->popupOpen ? ctx->inputOwner : UI_ID_NONE;
}

void Ui_SetCursor(UiContext *ctx, UiId id, int kind)
{
	if (!ctx || !id) return;
	for (int i = 0; i < ctx->widgetCount; i++)
		if (ctx->widgets[i].id == id) ctx->widgets[i].cursor = (uint8_t)kind;
}

void Ui_SetHoverFn(UiContext *ctx, UiId id, UiHoverFn fn, void *user)
{
	if (!ctx || !id) return;
	for (int i = 0; i < ctx->widgetCount; i++)
		if (ctx->widgets[i].id == id) {
			ctx->widgets[i].hoverFn = fn;
			ctx->widgets[i].hoverUser = user;
		}
}

void Ui_SetPressPoll(UiContext *ctx, UiId id, int on)
{
	if (!ctx || !id) return;
	for (int i = 0; i < ctx->widgetCount; i++)
		if (ctx->widgets[i].id == id)
			ctx->widgets[i].pressPoll = on ? 1 : 0;
}

void Ui_InvalidateRect(UiContext *ctx, UiRect rc)
{
	RECT r;
	float s;
	if (!ctx || !ctx->hwnd || rc.w <= 0.0f || rc.h <= 0.0f) return;
	s = ctx->scale > 0.0f ? ctx->scale : 1.0f;
	r.left = (LONG)((rc.x - 1.0f) * s);
	r.top = (LONG)((rc.y - 1.0f) * s);
	r.right = (LONG)((rc.x + rc.w + 1.0f) * s) + 1;
	r.bottom = (LONG)((rc.y + rc.h + 1.0f) * s) + 1;
	InvalidateRect(ctx->hwnd, &r, FALSE);
}

int Ui_CursorAt(const UiContext *ctx, float x, float y)
{
	int i;
	if (!ctx || ctx->modal) return UI_CURSOR_ARROW;
	i = widget_at(ctx, x, y);
	return i >= 0 ? (int)ctx->widgets[i].cursor : UI_CURSOR_ARROW;
}

float Ui_PxSnap(const UiContext *ctx, float dip)
{
	float s = (ctx && ctx->scale > 0.0f) ? ctx->scale : 1.0f;
	float px = dip * s;
	int n = (int)(px >= 0.0f ? px + 0.5f : px - 0.5f);
	return (float)n / s;
}

HCURSOR Ui_CursorHandle(int kind)
{
	LPCWSTR name = IDC_ARROW;
	switch (kind) {
	case UI_CURSOR_IBEAM:   name = IDC_IBEAM; break;
	case UI_CURSOR_HAND:    name = IDC_HAND; break;
	case UI_CURSOR_SIZE_NS: name = IDC_SIZENS; break;
	case UI_CURSOR_SIZE_WE: name = IDC_SIZEWE; break;
	default: break;
	}
	return LoadCursorW(NULL, name);
}

UiWidgetState Ui_State(UiContext *ctx, UiId id, UiRect rc, int enabled)
{
	UiWidgetState st;
	int focused, inputAllowed;
	st.hovered = st.pressed = st.active = st.focused = 0;
	st.hover = st.press = st.focus = st.glow = 0.0f;

	if (!enabled) {
		/* 禁用态: 目标归零, 不注册命中 (保持原有语义) */
		if (Ui_FxActive(ctx)) {
			Ui_AnimSet(ctx, id, UI_AN_HOVER, 0.0f);
			Ui_AnimSet(ctx, id, UI_AN_PRESS, 0.0f);
			Ui_AnimSet(ctx, id, UI_AN_FOCUS, 0.0f);
			Ui_AnimSet(ctx, id, UI_AN_GLOW, 0.0f);
			return st;
		}
		Ui_AnimSmooth(ctx, id, UI_AN_HOVER, 0.0f);
		Ui_AnimSmooth(ctx, id, UI_AN_PRESS, 0.0f);
		Ui_AnimSmooth(ctx, id, UI_AN_FOCUS, 0.0f);
		Ui_AnimSmooth(ctx, id, UI_AN_GLOW, 0.0f);
		return st;
	}
	Ui_RegisterWidget(ctx, id, rc);
	Ui_AnimSetRect(ctx, id, rc);
	inputAllowed = Ui_InputAllowed(ctx);
	st.hovered = inputAllowed && (ctx->hover == id);
	st.pressed = inputAllowed && ctx->mouseDown && ctx->pressed == id;
	st.active = inputAllowed && ctx->drag.active && ctx->drag.id == id;
	focused = inputAllowed && (ctx->focusId != UI_ID_NONE && ctx->focusId == id);
	st.focused = focused;

	/* 合成器路径: 状态直接落最终值, 过渡由 SnapshotFx 覆盖层完成;
	 * CPU 通道只作为 ULW/无合成器时的降级。 */
	if (Ui_FxActive(ctx)) {
		Ui_AnimSet(ctx, id, UI_AN_HOVER, st.hovered ? 1.0f : 0.0f);
		Ui_AnimSet(ctx, id, UI_AN_PRESS, st.pressed ? 1.0f : 0.0f);
		Ui_AnimSet(ctx, id, UI_AN_FOCUS, focused ? 1.0f : 0.0f);
		Ui_AnimSet(ctx, id, UI_AN_GLOW,
			(st.hovered || focused) ? 1.0f : 0.0f);
		st.hover = st.hovered ? 1.0f : 0.0f;
		st.press = st.pressed ? 1.0f : 0.0f;
		st.focus = focused ? 1.0f : 0.0f;
		st.glow = (st.hovered || focused) ? 1.0f : 0.0f;
		return st;
	}

	/* 采集目标; 数值推进由 Ui_AnimAdvance 统一做 */
	Ui_AnimSmooth(ctx, id, UI_AN_HOVER, st.hovered ? 1.0f : 0.0f);
	Ui_AnimSmooth(ctx, id, UI_AN_PRESS, st.pressed ? 1.0f : 0.0f);
	Ui_AnimSmooth(ctx, id, UI_AN_FOCUS, focused ? 1.0f : 0.0f);
	Ui_AnimSmooth(ctx, id, UI_AN_GLOW, (st.hovered || focused) ? 1.0f : 0.0f);

	st.hover = Ui_AnimGet(ctx, id, UI_AN_HOVER, 0.0f);
	st.press = Ui_AnimGet(ctx, id, UI_AN_PRESS, 0.0f);
	st.focus = Ui_AnimGet(ctx, id, UI_AN_FOCUS, 0.0f);
	st.glow  = Ui_AnimGet(ctx, id, UI_AN_GLOW, 0.0f);
	return st;
}

UiWidgetState Ui_StateInstant(UiContext *ctx, UiId id, UiRect rc, int enabled)
{
	UiWidgetState st;
	int focused, inputAllowed;
	st.hovered = st.pressed = st.active = st.focused = 0;
	st.hover = st.press = st.focus = st.glow = 0.0f;
	if (!enabled) return st;
	Ui_RegisterWidget(ctx, id, rc);
	inputAllowed = Ui_InputAllowed(ctx);
	st.hovered = inputAllowed && (ctx->hover == id);
	st.pressed = inputAllowed && ctx->mouseDown && ctx->pressed == id;
	st.active = inputAllowed && ctx->drag.active && ctx->drag.id == id;
	focused = inputAllowed && (ctx->focusId != UI_ID_NONE && ctx->focusId == id);
	st.focused = focused;
	st.hover = st.hovered ? 1.0f : 0.0f;
	st.press = st.pressed ? 1.0f : 0.0f;
	st.focus = focused ? 1.0f : 0.0f;
	st.glow = (st.hovered || focused) ? 1.0f : 0.0f;
	return st;
}

void Ui_OnMouseMove(UiContext *ctx, float x, float y)
{
	ctx->mouseX = x;
	ctx->mouseY = y;
	ctx->mouseOver = 1;
	if (ctx->modal) {
		/* 模态期间 hover 固定在 blocker 上, 不触发任何控件失效/重绘:
		 * Toast 的遮罩和卡片在 DComp layer 上, 主窗无需响应鼠标移动. */
		ctx->hover = ctx->modal;
		return;
	}
	UiId h = UI_ID_NONE;
	if (ctx->drag.active || ctx->mouseDown) {
		/* 拖动/按住时不更新 hover，保证按下态稳定 */
		h = UI_ID_NONE;
	} else {
		h = Ui_HitTest(ctx, x, y);
	}
	UiId oldHover = ctx->hover;
	ctx->hover = h;
	if (h != oldHover) {
		if (widget_registered(ctx, oldHover)) {
			InvalidateWidget(ctx, oldHover);
			Ui_FxChange(ctx, oldHover, Ui_AnimFxMs(UI_AN_HOVER));
		}
		if (widget_registered(ctx, h)) {
			InvalidateWidget(ctx, h);
			Ui_FxChange(ctx, h, Ui_AnimFxMs(UI_AN_HOVER));
		}
	}
	/* 控件内精细命中区: 鼠标移动事件内同步判定, 只在状态变化时重绘 */
	if (h != UI_ID_NONE) {
		int wi = widget_at(ctx, x, y);
		UiRect dirty;
		if (wi >= 0 && ctx->widgets[wi].hoverFn) {
			dirty = Ui_Rect(0, 0, 0, 0);
			if (ctx->widgets[wi].hoverFn(ctx->widgets[wi].hoverUser, ctx,
				x, y, &dirty)) {
				if (dirty.w > 0.0f && dirty.h > 0.0f)
					Ui_InvalidateRect(ctx, dirty);
				else
					InvalidateWidget(ctx, h);
			}
		}
	}
	if (ctx->drag.active) InvalidateWidget(ctx, ctx->drag.id);
	else if (ctx->pressed) {
		/* 只有显式声明“按下跟随指针”的控件才需要逐移动重绘, 否则
		 * 按下期间每次移动都会整控件重绘 (文本/文档等无视觉变化). */
		int pi;
		for (pi = ctx->widgetCount - 1; pi >= 0; pi--)
			if (ctx->widgets[pi].id == ctx->pressed) break;
		if (pi >= 0 && ctx->widgets[pi].pressPoll)
			InvalidateWidget(ctx, ctx->pressed);
	}
}

void Ui_OnMouseDown(UiContext *ctx, float x, float y)
{
	ctx->mouseX = x;
	ctx->mouseY = y;
	ctx->pressX = x;
	ctx->pressY = y;
	ctx->mouseOver = 1;
	ctx->navFocusVisible = 0;
	if (ctx->modal) {
		ctx->pressed = ctx->modal;
		ctx->down = ctx->modal;
		ctx->active = ctx->modal;
		ctx->mouseDown = 1;
		ctx->clicked = UI_ID_NONE;
		ctx->released = UI_ID_NONE;
		ctx->clickedOutside = 0;
		return;
	}
	UiId hit = Ui_HitTest(ctx, x, y);
	/* 未命中也要锁存 mouseDown: 抬起时才能得到 clickedOutside
	 * (弹层外部点击 / 空区域点击). hit 为 0 时 pressed 也是 0. */
	ctx->pressed = hit;
	ctx->down = hit;
	ctx->active = hit;
	ctx->mouseDown = 1;
	ctx->clicked = UI_ID_NONE;
	ctx->released = UI_ID_NONE;
	ctx->clickedOutside = 0;
	if (hit) {
		if (widget_registered(ctx, hit)) {
			InvalidateWidget(ctx, hit);
			Ui_FxChange(ctx, hit, Ui_AnimFxMs(UI_AN_PRESS));   /* hover -> press */
		}
	} else if (ctx->popupOpen && ctx->hwnd) {
		/* 点在弹层外: 弹层管理器在下一帧关闭, 这里先安排一次重绘. */
		InvalidateRect(ctx->hwnd, NULL, FALSE);
	}
}

void Ui_OnMouseUp(UiContext *ctx, float x, float y)
{
	ctx->mouseX = x;
	ctx->mouseY = y;
	if (!ctx->mouseDown) return;
	if (ctx->modal) {
		ctx->released = ctx->modal;
		ctx->clicked = (ctx->pressed == ctx->modal) ? ctx->modal : UI_ID_NONE;
		ctx->clickedOutside = 0;
		ctx->pressed = UI_ID_NONE;
		ctx->active = UI_ID_NONE;
		ctx->mouseDown = 0;
		ctx->hover = ctx->modal;
		return;
	}
	UiId hit = Ui_HitTest(ctx, x, y);
	UiId oldPressed = ctx->pressed;
	UiId oldHover = ctx->hover;
	ctx->released = ctx->pressed;
	ctx->clicked = (hit == ctx->pressed) ? ctx->pressed : UI_ID_NONE;
	ctx->clickedOutside = (hit == UI_ID_NONE);
	if (ctx->clicked) {
		int ci;
		for (ci = ctx->widgetCount - 1; ci >= 0; ci--)
			if (ctx->widgets[ci].id == ctx->clicked) break;
		if (ci >= 0 && ctx->widgets[ci].clickFn)
			ctx->widgets[ci].clickFn(ctx->widgets[ci].clickUser, ctx,
				ctx->clicked);
	}
	ctx->pressed = UI_ID_NONE;
	ctx->active = UI_ID_NONE;
	ctx->mouseDown = 0;
	/* 按下期间 hover 被清掉 (Ui_OnMouseMove 的稳定态处理); 抬起时鼠标通常还在
	 * 原控件上, 必须立刻恢复, 否则要等下一次 mousemove 才亮回来 —— 每点一次
	 * 就闪一下 (真机: "多几次就 hover/focus 来回闪")。 */
	ctx->hover = hit;
	if (widget_registered(ctx, oldPressed)) InvalidateWidget(ctx, oldPressed);
	if (widget_registered(ctx, oldHover)) InvalidateWidget(ctx, oldHover);
	if (widget_registered(ctx, hit)) InvalidateWidget(ctx, hit);
	/* press -> hover 的过渡 */
	if (oldPressed && widget_registered(ctx, oldPressed))
		Ui_FxChange(ctx, oldPressed, Ui_AnimFxMs(UI_AN_PRESS));
	if (hit && widget_registered(ctx, hit)) Ui_FxChange(ctx, hit, Ui_AnimFxMs(UI_AN_PRESS));
	if (oldHover && oldHover != hit && widget_registered(ctx, oldHover))
		Ui_FxChange(ctx, oldHover, Ui_AnimFxMs(UI_AN_HOVER));
}

void Ui_OnMouseLeave(UiContext *ctx)
{
	if (ctx->modal) {
		ctx->mouseX = -10000.0f;
		ctx->mouseY = -10000.0f;
		ctx->mouseOver = 0;
		ctx->hover = ctx->modal;
		return;
	}
	if (widget_registered(ctx, ctx->hover)) {
		InvalidateWidget(ctx, ctx->hover);
		if (ctx->hover) Ui_FxChange(ctx, ctx->hover, Ui_AnimFxMs(UI_AN_HOVER));
	}
	if (widget_registered(ctx, ctx->pressed)) {
		InvalidateWidget(ctx, ctx->pressed);
		if (ctx->pressed && ctx->pressed != ctx->hover)
			Ui_FxChange(ctx, ctx->pressed, Ui_AnimFxMs(UI_AN_PRESS));
	}
	if (ctx->drag.active) InvalidateWidget(ctx, ctx->drag.id);
	ctx->hover = UI_ID_NONE;
	/* 按住/拖动期间窗口已 SetCapture, 指针划出客户区仍会收到移动消息.
	 * 此时如果把坐标清成 -10000, 滑块/滚动条 thumb 会瞬间跳回 0. */
	if (ctx->mouseDown || ctx->drag.active) return;
	ctx->mouseX = -10000.0f;
	ctx->mouseY = -10000.0f;
	ctx->mouseOver = 0;
}

void Ui_OnCaptureChanged(UiContext *ctx)
{
	if (ctx->modal) {
		ctx->pressed = UI_ID_NONE;
		ctx->active = UI_ID_NONE;
		ctx->mouseDown = 0;
		ctx->hover = ctx->modal;
		return;
	}
	InvalidateWidget(ctx, ctx->hover);
	InvalidateWidget(ctx, ctx->pressed);
	InvalidateWidget(ctx, ctx->active);
	if (ctx->drag.active) InvalidateWidget(ctx, ctx->drag.id);
	ctx->pressed = UI_ID_NONE;
	ctx->active = UI_ID_NONE;
	ctx->mouseDown = 0;
	ctx->hover = UI_ID_NONE;
	ctx->drag.active = 0;
}

void Ui_OccludeRect(UiContext *ctx, UiRect rc)
{
	if (!ctx || rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (ctx->fxCutCount >= 8) return;
	ctx->fxCut[ctx->fxCutCount++] = rc;
}

int Ui_Occluded(const UiContext *ctx, UiRect rc)
{
	int i;
	if (!ctx || rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	for (i = 0; i < ctx->fxCutPrevCount; i++) {
		const UiRect *c = &ctx->fxCutPrev[i];
		if (rc.x < c->x + c->w && c->x < rc.x + rc.w &&
			rc.y < c->y + c->h && c->y < rc.y + rc.h)
			return 1;
	}
	return 0;
}
