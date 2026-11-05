#include "ui/ui_context.h"

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

void Ui_ContextInit(UiContext *ctx, HWND hwnd, Gfx *gfx, UiTheme *theme)
{
	ZeroMemory(ctx, sizeof(*ctx));
	ctx->hwnd = hwnd;
	ctx->gfx = gfx;
	ctx->theme = theme;
	if (theme) theme->notifyHwnd = hwnd;   /* 全局换肤后自动失效该窗口 */
	ctx->scale = 1.0f;
	ctx->popupOpen = 0;
	ctx->focusId = UI_ID_NONE;
}

void Ui_SetFocusId(UiContext *ctx, UiId id)
{
	if (ctx) ctx->focusId = id;
}

void Ui_SetNavId(UiContext *ctx, UiId id)
{
	if (ctx) ctx->navId = id;
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
	ctx->width = w;
	ctx->height = h;
	ctx->scale = scale > 0.0f ? scale : 1.0f;
}

void Ui_BeginFrame(UiContext *ctx)
{
	ctx->widgetCount = 0;
	ctx->layer = UI_LAYER_BASE;
	ctx->z = UI_Z_BASE;
	ctx->zDepth = 0;
}

void Ui_EndFrame(UiContext *ctx)
{
	if (ctx->released) ctx->drag.active = 0;
	ctx->clicked = UI_ID_NONE;
	ctx->down = UI_ID_NONE;
	ctx->released = UI_ID_NONE;
	ctx->clickedOutside = 0;
	ctx->layer = UI_LAYER_BASE;
	ctx->z = UI_Z_BASE;
	ctx->zDepth = 0;
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

void Ui_RegisterWidget(UiContext *ctx, UiId id, UiRect rc)
{
	if (!id || rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (ctx->widgetCount >= UI_MAX_WIDGETS) return;
	UiWidget *w = &ctx->widgets[ctx->widgetCount++];
	w->id = id;
	w->rect = rc;
	w->layer = ctx->layer;
	w->z = ctx->z;
}

UiId Ui_HitTest(const UiContext *ctx, float x, float y)
{
	UiId best = UI_ID_NONE;
	int bestZ = -2147483647;
	if (ctx->modal) return ctx->modal;
	/* 从后往前: 同 z 时后注册(后画)的优先; 跨层取最高 z。 */
	for (int i = ctx->widgetCount - 1; i >= 0; i--) {
		const UiWidget *w = &ctx->widgets[i];
		if (ctx->popupOpen && w->layer != UI_LAYER_OVERLAY) continue;
		if (!Ui_RectContains(w->rect, x, y)) continue;
		if (w->z > bestZ) {
			bestZ = w->z;
			best = w->id;
		}
	}
	return best;
}

UiWidgetState Ui_State(UiContext *ctx, UiId id, UiRect rc, int enabled)
{
	UiWidgetState st;
	int focused;
	st.hovered = st.pressed = st.active = st.focused = 0;
	st.hover = st.press = st.focus = st.glow = 0.0f;

	if (!enabled) {
		/* 禁用态: 目标归零, 不注册命中 (保持原有语义) */
		Ui_AnimSmooth(ctx, id, UI_AN_HOVER, 0.0f);
		Ui_AnimSmooth(ctx, id, UI_AN_PRESS, 0.0f);
		Ui_AnimSmooth(ctx, id, UI_AN_FOCUS, 0.0f);
		Ui_AnimSmooth(ctx, id, UI_AN_GLOW, 0.0f);
		return st;
	}
	Ui_RegisterWidget(ctx, id, rc);
	Ui_AnimSetRect(ctx, id, rc);
	st.hovered = (ctx->hover == id);
	st.pressed = (ctx->mouseDown && ctx->pressed == id);
	st.active = (ctx->drag.active && ctx->drag.id == id);
	focused = (ctx->focusId != UI_ID_NONE && ctx->focusId == id);
	st.focused = focused;

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

void Ui_OnMouseMove(UiContext *ctx, float x, float y)
{
	ctx->mouseX = x;
	ctx->mouseY = y;
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
		InvalidateWidget(ctx, oldHover);
		InvalidateWidget(ctx, h);
	}
	if (ctx->drag.active) InvalidateWidget(ctx, ctx->drag.id);
	else if (ctx->pressed) InvalidateWidget(ctx, ctx->pressed);
}

void Ui_OnMouseDown(UiContext *ctx, float x, float y)
{
	ctx->mouseX = x;
	ctx->mouseY = y;
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
	if (!hit) return;
	ctx->pressed = hit;
	ctx->down = hit;
	ctx->active = hit;
	ctx->mouseDown = 1;
	ctx->clicked = UI_ID_NONE;
	ctx->released = UI_ID_NONE;
	ctx->clickedOutside = 0;
	InvalidateWidget(ctx, hit);
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
	ctx->pressed = UI_ID_NONE;
	ctx->active = UI_ID_NONE;
	ctx->mouseDown = 0;
	/* 按下期间 hover 被清掉 (Ui_OnMouseMove 的稳定态处理); 抬起时鼠标通常还在
	 * 原控件上, 必须立刻恢复, 否则要等下一次 mousemove 才亮回来 —— 每点一次
	 * 就闪一下 (真机: "多几次就 hover/focus 来回闪")。 */
	ctx->hover = hit;
	InvalidateWidget(ctx, oldPressed);
	InvalidateWidget(ctx, oldHover);
	InvalidateWidget(ctx, hit);
}

void Ui_OnMouseLeave(UiContext *ctx)
{
	if (ctx->modal) {
		ctx->mouseX = -10000.0f;
		ctx->mouseY = -10000.0f;
		ctx->hover = ctx->modal;
		return;
	}
	InvalidateWidget(ctx, ctx->hover);
	InvalidateWidget(ctx, ctx->pressed);
	if (ctx->drag.active) InvalidateWidget(ctx, ctx->drag.id);
	ctx->hover = UI_ID_NONE;
	ctx->mouseX = -10000.0f;
	ctx->mouseY = -10000.0f;
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
