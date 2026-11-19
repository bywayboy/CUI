#include "ui/ui_scroll.h"

/* ── 小工具 ─────────────────────────────────────────────────────────── */

static float absf_(float v) { return v < 0.0f ? -v : v; }

static UiId scroll_child_id(UiId id, int which)
{
	return Ui_IdIndex("ui.scroll.bar", (int)(uint32_t)(id >> 32),
		(int)(uint32_t)id, which);
}

static UiColor alpha_scale(UiColor c, float k)
{
	int a = (int)((c >> 24) & 0xFF);
	a = (int)((float)a * k + 0.5f);
	if (a < 0) a = 0;
	if (a > 255) a = 255;
	return ((UiColor)a << 24) | (c & 0x00FFFFFFu);
}

static float sv_bar_w(const UiScrollView *sv)
{
	return sv && sv->bar.width > 0.0f ? sv->bar.width : 6.0f;
}

static float sv_bar_margin(const UiScrollView *sv)
{
	return sv && sv->bar.margin >= 0.0f ? sv->bar.margin : 2.0f;
}

static int sv_reserve_bar(const UiScrollView *sv)
{
	return sv && sv->bar.reserveSpace && sv_bar_w(sv) > 0.0f;
}

static UiRect sv_inner_rect(const UiScrollView *sv, UiRect viewport)
{
	UiRect r;
	float reserve = sv_reserve_bar(sv)
		? sv_bar_w(sv) + sv_bar_margin(sv) : 0.0f;
	r.x = viewport.x + sv->pad.left;
	r.y = viewport.y + sv->pad.top;
	r.w = viewport.w - sv->pad.left - sv->pad.right - reserve;
	r.h = viewport.h - sv->pad.top - sv->pad.bottom;
	if (r.w < 0.0f) r.w = 0.0f;
	if (r.h < 0.0f) r.h = 0.0f;
	return r;
}

static int scrollable(const UiScrollView *sv)
{
	return sv && sv->contentH > sv->inner.h + 0.5f;
}

float UiScrollView_MaxOffset(const UiScrollView *sv)
{
	float m;
	if (!sv) return 0.0f;
	m = sv->contentH - sv->inner.h;
	return m > 0.0f ? m : 0.0f;
}

static void invalidate_rc(UiContext *ui, UiRect rc, float pad)
{
	RECT r;
	float s;
	if (!ui || !ui->hwnd || rc.w <= 0.0f || rc.h <= 0.0f) return;
	s = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((rc.x - pad) * s);
	r.top = (LONG)((rc.y - pad) * s);
	r.right = (LONG)((rc.x + rc.w + pad) * s) + 1;
	r.bottom = (LONG)((rc.y + rc.h + pad) * s) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

static void bar_geom(const UiScrollView *sv, UiRect *track, UiRect *thumb)
{
	float w = sv_bar_w(sv);
	float margin = sv_bar_margin(sv);
	float minThumb = sv->bar.minThumb > 0.0f ? sv->bar.minThumb : 24.0f;
	float maxOff = UiScrollView_MaxOffset(sv);
	float th, y, t;

	track->x = sv->viewport.x + sv->viewport.w - w - margin;
	track->y = sv->inner.y + margin;
	track->w = w;
	track->h = sv->inner.h - margin * 2.0f;
	if (track->h < 0.0f) track->h = 0.0f;

	if (maxOff <= 0.5f || sv->contentH <= 0.0f || track->h <= 0.0f) {
		*thumb = Ui_Rect(track->x, track->y, track->w, 0.0f);
		return;
	}
	th = track->h * sv->inner.h / sv->contentH;
	if (th < minThumb) th = minThumb;
	if (th > track->h) th = track->h;
	t = Ui_Clampf(sv->offset / maxOff, 0.0f, 1.0f);
	y = track->y + (track->h - th) * t;
	*thumb = Ui_Rect(track->x, y, track->w, th);
}

static UiRect bar_rect(const UiScrollView *sv)
{
	UiRect track, thumb;
	float w = sv_bar_w(sv);
	float margin = sv_bar_margin(sv);
	(void)thumb;
	bar_geom(sv, &track, &thumb);
	return Ui_Rect(track.x - 2.0f, sv->inner.y,
		w + margin + 4.0f, sv->inner.h);
}

/* ── 配置/初始化 ───────────────────────────────────────────────────── */

UiScrollConfig UiScrollConfig_Desktop(void)
{
	UiScrollConfig c;
	ZeroMemory(&c, sizeof(c));
	c.wheelStep = 48.0f;
	c.wheelTauMs = 120.0f;
	c.flingDecel = 2600.0f;
	c.minVelocity = 8.0f;
	c.bounce = 0;
	c.overscrollResist = 0.55f;
	c.maxOverscroll = 40.0f;
	c.autoHideBar = 1;
	c.barHideDelayMs = 800.0f;
	c.barFadeMs = 150.0f;
	return c;
}

UiScrollConfig UiScrollConfig_Touch(void)
{
	UiScrollConfig c = UiScrollConfig_Desktop();
	c.wheelStep = 60.0f;
	c.wheelTauMs = 80.0f;
	c.flingDecel = 2100.0f;
	c.bounce = 1;
	c.maxOverscroll = 48.0f;
	return c;
}

UiScrollbarStyle UiScrollbarStyle_Default(void)
{
	UiScrollbarStyle s;
	ZeroMemory(&s, sizeof(s));
	s.width = 6.0f;
	s.minThumb = 24.0f;
	s.margin = 2.0f;
	s.radius = 3.0f;
	return s;
}

void UiScrollView_Init(UiScrollView *sv, UiId id,
	const UiScrollConfig *cfg, const UiScrollbarStyle *style)
{
	if (!sv) return;
	ZeroMemory(sv, sizeof(*sv));
	sv->id = id;
	sv->cfg = cfg ? *cfg : UiScrollConfig_Desktop();
	sv->bar = style ? *style : UiScrollbarStyle_Default();
	/* 容错: 零初始化配置仍能滚动. bounce/autoHideBar 保留调用方语义. */
	if (sv->cfg.wheelStep <= 0.0f) sv->cfg.wheelStep = 48.0f;
	if (sv->cfg.flingDecel <= 0.0f) sv->cfg.flingDecel = 2600.0f;
	if (sv->cfg.minVelocity <= 0.0f) sv->cfg.minVelocity = 8.0f;
	if (sv->cfg.overscrollResist <= 0.0f) sv->cfg.overscrollResist = 0.55f;
	if (sv->cfg.maxOverscroll <= 0.0f) sv->cfg.maxOverscroll = 40.0f;
	if (sv->cfg.barHideDelayMs < 0.0f) sv->cfg.barHideDelayMs = 800.0f;
	if (sv->cfg.barFadeMs < 0.0f) sv->cfg.barFadeMs = 150.0f;
	if (sv->bar.width <= 0.0f) sv->bar.width = 6.0f;
	if (sv->bar.minThumb <= 0.0f) sv->bar.minThumb = 24.0f;
	if (sv->bar.margin < 0.0f) sv->bar.margin = 2.0f;
	if (sv->bar.radius <= 0.0f) sv->bar.radius = sv->bar.width * 0.5f;
	sv->barTarget = 0.0f;
}

void UiScrollView_SetPadding(UiScrollView *sv, UiScrollPadding pad)
{
	if (!sv) return;
	if (pad.left < 0.0f) pad.left = 0.0f;
	if (pad.top < 0.0f) pad.top = 0.0f;
	if (pad.right < 0.0f) pad.right = 0.0f;
	if (pad.bottom < 0.0f) pad.bottom = 0.0f;
	sv->pad = pad;
}

float UiScrollView_ContentWidth(const UiScrollView *sv, UiRect viewport)
{
	UiRect r;
	if (!sv) return 0.0f;
	r = sv_inner_rect(sv, viewport);
	return r.w;
}

/* ── 滚动操作 ───────────────────────────────────────────────────────── */

static void scroll_wheel_apply(UiScrollView *sv, float delta)
{
	float maxOff;
	if (!scrollable(sv) || delta == 0.0f) return;
	maxOff = UiScrollView_MaxOffset(sv);
	sv->target = Ui_Clampf(sv->target - delta * sv->cfg.wheelStep,
		0.0f, maxOff);
	if (sv->cfg.wheelTauMs <= 0.0f) sv->offset = sv->target;
	sv->velocity = 0.0f;
	sv->idleMs = 0.0f;
	sv->barOpacity = 1.0f;
	sv->barTarget = 1.0f;
}

void UiScrollView_ScrollTo(UiScrollView *sv, UiContext *ui,
	float offset, int animate)
{
	float maxOff;
	if (!sv || !ui) return;
	maxOff = UiScrollView_MaxOffset(sv);
	sv->target = Ui_Clampf(offset, 0.0f, maxOff);
	if (!animate) sv->offset = sv->target;
	sv->velocity = 0.0f;
	sv->idleMs = 0.0f;
	sv->barOpacity = 1.0f;
	sv->barTarget = 1.0f;
	invalidate_rc(ui, sv->viewport, 2.0f);
}

void UiScrollView_EnsureVisible(UiScrollView *sv, UiContext *ui,
	float localY, float localH)
{
	float top, bottom;
	if (!sv || !ui) return;
	if (localY < sv->offset)
		UiScrollView_ScrollTo(sv, ui, localY, 1);
	else if (localY + localH > sv->offset + sv->inner.h) {
		top = localY + localH - sv->inner.h;
		bottom = UiScrollView_MaxOffset(sv);
		UiScrollView_ScrollTo(sv, ui, top < bottom ? top : bottom, 1);
	}
}

void UiScrollView_Fling(UiScrollView *sv, UiContext *ui, float velocity)
{
	if (!sv || !ui || !scrollable(sv)) return;
	sv->velocity = velocity;
	sv->idleMs = 0.0f;
	sv->barOpacity = 1.0f;
	sv->barTarget = 1.0f;
	invalidate_rc(ui, sv->viewport, 2.0f);
}

int UiScrollView_Wheel(UiScrollView *sv, UiContext *ui, float delta)
{
	if (!sv || !ui || !scrollable(sv)) return 0;
	scroll_wheel_apply(sv, delta);
	invalidate_rc(ui, sv->viewport, 2.0f);
	invalidate_rc(ui, bar_rect(sv), 2.0f);
	return 1;
}

/* ── 帧流程 ─────────────────────────────────────────────────────────── */

void UiScrollView_Begin(UiScrollView *sv, UiContext *ui,
	UiRect viewport, float contentW, float contentH)
{
	float maxOff;
	if (!sv || !ui || !ui->gfx) return;
	sv->active = 0;
	sv->clipPushed = 0;
	if (viewport.w <= 0.0f || viewport.h <= 0.0f) return;

	sv->viewport = viewport;
	sv->inner = sv_inner_rect(sv, viewport);
	sv->contentW = contentW > 0.0f ? contentW : sv->inner.w;
	sv->contentH = contentH > 0.0f ? contentH : 0.0f;
	maxOff = UiScrollView_MaxOffset(sv);

	/* 内容/视口变矮导致最大偏移变小: 这是布局变化, 不是用户手势,
	 * 立即把 offset 夹到新的底部; 否则 thumb 会停在越界位置, 滚轮也会
	 * 被回弹状态吞掉. */
	if (maxOff + 0.01f < sv->lastMaxOff && sv->offset > maxOff) {
		sv->offset = maxOff;
		sv->velocity = 0.0f;
	}
	sv->lastMaxOff = maxOff;

	if (maxOff <= 0.5f) {
		sv->offset = sv->target = sv->velocity = 0.0f;
		sv->draggingThumb = 0;
	}
	if (sv->target < 0.0f) sv->target = 0.0f;
	if (sv->target > maxOff) sv->target = maxOff;
	if (!sv->cfg.bounce) {
		sv->offset = Ui_Clampf(sv->offset, 0.0f, maxOff);
		if (sv->offset == 0.0f && sv->velocity < 0.0f) sv->velocity = 0.0f;
		if (sv->offset == maxOff && sv->velocity > 0.0f) sv->velocity = 0.0f;
	} else {
		if (sv->offset < -sv->cfg.maxOverscroll)
			sv->offset = -sv->cfg.maxOverscroll;
		if (sv->offset > maxOff + sv->cfg.maxOverscroll)
			sv->offset = maxOff + sv->cfg.maxOverscroll;
	}

	sv->mouseInside = ui->mouseOver &&
		Ui_RectContains(viewport, ui->mouseX, ui->mouseY);
	sv->barHold = sv->mouseInside || sv->draggingThumb;

	/* 滚动条 thumb 拖拽在内容绘制前更新, 避免一帧延迟. */
	if (sv->draggingThumb) {
		if (!ui->mouseDown) {
			sv->draggingThumb = 0;
		} else if (maxOff > 0.0f) {
			UiRect track, thumb;
			float span;
			bar_geom(sv, &track, &thumb);
			span = track.h - thumb.h;
			if (span > 0.5f) {
				float t = (ui->mouseY - sv->dragGrab - track.y) / span;
				sv->offset = Ui_Clampf(t, 0.0f, 1.0f) * maxOff;
				sv->target = sv->offset;
				sv->velocity = 0.0f;
			}
			sv->idleMs = 0.0f;
			sv->barOpacity = 1.0f;
			sv->barTarget = 1.0f;
			invalidate_rc(ui, viewport, 2.0f);
		}
	}

	/* 视口本身作为 hover 目标: 让鼠标进入空白区也能触发滚动条淡入. */
	Ui_RegisterWidget(ui, sv->id, viewport);
	if (scrollable(sv)) Ui_RegisterWheelTarget(ui, sv->id, viewport);

	if (ui->wheelTarget == sv->id && ui->wheelDelta != 0.0f) {
		scroll_wheel_apply(sv, ui->wheelDelta);
		ui->wheelTarget = UI_ID_NONE;
		ui->wheelDelta = 0.0f;
	}

	Gfx_PushClip(ui->gfx, sv->inner);
	sv->clipPushed = Ui_PushClipRect(ui, sv->inner);
	sv->active = 1;
}

void UiScrollView_End(UiScrollView *sv, UiContext *ui)
{
	UiRect track, thumb;
	UiTheme *t;
	UiWidgetState tst, hst;
	UiId trackId, thumbId;
	float opacity, w;
	UiColor trackColor, thumbColor;
	int hover;

	if (!sv || !ui) return;
	if (!sv->active) return;
	if (sv->clipPushed) Ui_PopClipRect(ui);
	if (ui->gfx) Gfx_PopClip(ui->gfx);
	sv->active = 0;
	sv->clipPushed = 0;

	sv->barHold = sv->mouseInside || sv->draggingThumb;
	if (!scrollable(sv)) {
		sv->draggingThumb = 0;
		return;
	}
	opacity = sv->barOpacity;
	if (opacity <= 0.004f) return;

	t = ui->theme;
	bar_geom(sv, &track, &thumb);
	if (thumb.h <= 0.0f) return;
	trackId = scroll_child_id(sv->id, 1);
	thumbId = scroll_child_id(sv->id, 2);

	Ui_PushLayer(ui, UI_Z_OVERLAY);
	tst = Ui_State(ui, trackId, track, 1);
	hst = Ui_State(ui, thumbId, thumb, 1);
	Ui_PopLayer(ui);

	if (ui->clicked == trackId) {
		float page = sv->inner.h * 0.9f;
		float maxOff = UiScrollView_MaxOffset(sv);
		sv->target = Ui_Clampf(sv->target +
			(ui->mouseY < thumb.y ? -page : page), 0.0f, maxOff);
		sv->velocity = 0.0f;
		sv->idleMs = 0.0f;
		sv->barOpacity = 1.0f;
		sv->barTarget = 1.0f;
	}

	if (hst.pressed && ui->mouseDown && !sv->draggingThumb) {
		sv->draggingThumb = 1;
		sv->dragGrab = ui->mouseY - thumb.y;
		sv->velocity = 0.0f;
		sv->idleMs = 0.0f;
		sv->barOpacity = 1.0f;
		sv->barTarget = 1.0f;
	}
	if (sv->draggingThumb && !ui->mouseDown) sv->draggingThumb = 0;

	hover = hst.hovered || sv->draggingThumb;
	w = sv->bar.width > 0.0f ? sv->bar.width : 6.0f;

	trackColor = sv->bar.track;
	if (!trackColor) trackColor = Ui_ColorAlpha(t ? t->text3 : 0xFF888888u, 18);
	thumbColor = sv->bar.thumb;
	if (!thumbColor) thumbColor = Ui_ColorAlpha(t ? t->text3 : 0xFF888888u, 90);
	if (hover) {
		UiColor hc = sv->bar.thumbHover;
		if (!hc) hc = Ui_ColorAlpha(t ? t->text3 : 0xFF888888u, 140);
		thumbColor = hc;
	}
	Gfx_FillRoundRect(ui->gfx, track, sv->bar.radius > 0.0f ? sv->bar.radius : w * 0.5f,
		alpha_scale(trackColor, opacity));
	Gfx_FillRoundRect(ui->gfx, thumb, sv->bar.radius > 0.0f ? sv->bar.radius : w * 0.5f,
		alpha_scale(thumbColor, opacity));
	(void)tst;
}

int UiScrollView_Busy(const UiScrollView *sv)
{
	if (!sv) return 0;
	if (sv->draggingThumb) return 1;
	if (absf_(sv->velocity) > 0.01f) return 1;
	if (absf_(sv->offset - sv->target) > 0.05f) return 1;
	if (absf_(sv->barOpacity - sv->barTarget) > 0.002f) return 1;
	if (scrollable(sv) && sv->barHold && sv->barOpacity < 0.999f) return 1;
	if (sv->cfg.autoHideBar && !sv->bar.alwaysVisible &&
		!sv->barHold && sv->barOpacity > 0.001f &&
		sv->idleMs < sv->cfg.barHideDelayMs)
		return 1;
	if (!sv->cfg.autoHideBar || sv->bar.alwaysVisible) {
		if (scrollable(sv) && sv->barOpacity < 0.999f) return 1;
	}
	return 0;
}

void UiScrollView_Tick(UiScrollView *sv, UiContext *ui, float dtMs)
{
	float dt, oldOffset, oldOpacity, maxOff, targetOp;
	if (!sv || !ui) return;
	if (dtMs < 0.0f) dtMs = 0.0f;
	if (dtMs > 50.0f) dtMs = 50.0f;
	dt = dtMs / 1000.0f;
	oldOffset = sv->offset;
	oldOpacity = sv->barOpacity;

	/* 内容不滚动时不画滚动条, 直接清掉初始的 1.0 透明度. 若按淡出处理,
	 * 这里会在 WM_PAINT 内每帧 InvalidateRect, 排队出下一次 WM_PAINT,
	 * 消息泵回不到动画循环; 首次打开含 TextArea/短 Select 的 Dialog 时,
	 * 开合 Tick 会被饿死数百毫秒. */
	if (!scrollable(sv)) {
		sv->barOpacity = 0.0f;
		sv->barTarget = 0.0f;
		sv->idleMs = 0.0f;
		return;
	}

	sv->barHold = sv->mouseInside || sv->draggingThumb;
	if (sv->barHold) sv->idleMs = 0.0f;
	else sv->idleMs += dtMs;

	maxOff = UiScrollView_MaxOffset(sv);
	if (!sv->draggingThumb) {
		float v = sv->velocity;
		if (absf_(v) > 0.01f) {
			float dec = sv->cfg.flingDecel * dt;
			sv->offset += v * dt;
			if (v > 0.0f) {
				v -= dec;
				if (v < 0.0f) v = 0.0f;
			} else {
				v += dec;
				if (v > 0.0f) v = 0.0f;
			}
			sv->velocity = v;
			/* 惯性停止时才把 target 对齐实际位置; 拖动/滚轮期间已有的
			 * target 不能被每帧覆盖, 否则滚轮输入会被惯性状态吞掉. */
			if (v == 0.0f)
				sv->target = Ui_Clampf(sv->offset, 0.0f, maxOff);
		} else {
			sv->velocity = 0.0f;
			if (sv->offset >= 0.0f && sv->offset <= maxOff) {
				float diff = sv->target - sv->offset;
				if (absf_(diff) < 0.15f) {
					sv->offset = sv->target;
				} else if (sv->cfg.wheelTauMs <= 0.0f) {
					sv->offset = sv->target;
				} else {
					sv->offset += diff * dtMs / (sv->cfg.wheelTauMs + dtMs);
				}
			}
		}

		if (sv->cfg.bounce) {
			if (sv->offset < -sv->cfg.maxOverscroll)
				sv->offset = -sv->cfg.maxOverscroll;
			if (sv->offset > maxOff + sv->cfg.maxOverscroll)
				sv->offset = maxOff + sv->cfg.maxOverscroll;
			if (sv->offset < 0.0f || sv->offset > maxOff) {
				float bound = sv->offset < 0.0f ? 0.0f : maxOff;
				float over = sv->offset - bound;
				/* 小越界直接吸附: 弹簧产生的速度可能小于一帧惯性衰减,
				 * 否则会无限逼近边界却永远落不到 [0,max], 持续请求帧. */
				if (absf_(over) < 0.75f) {
					sv->offset = bound;
					sv->velocity = 0.0f;
				} else {
					if ((over < 0.0f && sv->velocity < 0.0f) ||
						(over > 0.0f && sv->velocity > 0.0f))
						sv->velocity *= 1.0f - 0.5f * sv->cfg.overscrollResist;
					sv->velocity += (-over * 180.0f - sv->velocity * 22.0f) * dt;
				}
			}
		} else {
			if (sv->offset < 0.0f) {
				sv->offset = 0.0f;
				if (sv->velocity < 0.0f) sv->velocity = 0.0f;
				sv->target = 0.0f;
			} else if (sv->offset > maxOff) {
				sv->offset = maxOff;
				if (sv->velocity > 0.0f) sv->velocity = 0.0f;
				sv->target = maxOff;
			}
		}

		if (absf_(sv->velocity) < sv->cfg.minVelocity &&
			absf_(sv->offset - sv->target) < 0.15f &&
			sv->offset >= 0.0f && sv->offset <= maxOff) {
			sv->offset = sv->target;
			sv->velocity = 0.0f;
		}
	}

	if (absf_(sv->offset - oldOffset) > 0.001f) {
		sv->idleMs = 0.0f;
		invalidate_rc(ui, sv->viewport, 2.0f);
		/* 内容在动: 该区域内的过渡快照会与新内容错位, 直接落位 */
		Ui_FxCancelRect(ui, sv->viewport);
	}

	targetOp = 0.0f;
	if (scrollable(sv)) {
		if (sv->bar.alwaysVisible || !sv->cfg.autoHideBar) targetOp = 1.0f;
		else if (sv->barHold) targetOp = 1.0f;
		else if (sv->barOpacity > 0.001f &&
			sv->idleMs < sv->cfg.barHideDelayMs) targetOp = 1.0f;
	}
	sv->barTarget = targetOp;
	if (sv->cfg.barFadeMs <= 0.0f) {
		sv->barOpacity = targetOp;
	} else {
		sv->barOpacity += (targetOp - sv->barOpacity) *
			dtMs / (sv->cfg.barFadeMs + dtMs);
		if (absf_(sv->barOpacity - targetOp) < 0.002f)
			sv->barOpacity = targetOp;
	}
	if (absf_(sv->barOpacity - oldOpacity) > 0.001f)
		invalidate_rc(ui, bar_rect(sv), 3.0f);

}

UiRect UiScrollView_ContentRect(const UiScrollView *sv)
{
	UiRect r = { 0, 0, 0, 0 };
	if (!sv) return r;
	r.x = sv->inner.x;
	r.y = sv->inner.y - sv->offset;
	r.w = sv->contentW;
	r.h = sv->contentH;
	return r;
}

int UiScrollView_Cull(const UiScrollView *sv, UiRect rc)
{
	if (!sv || rc.w <= 0.0f || rc.h <= 0.0f) return 1;
	if (rc.x >= sv->inner.x + sv->inner.w) return 1;
	if (rc.x + rc.w <= sv->inner.x) return 1;
	if (rc.y >= sv->inner.y + sv->inner.h) return 1;
	if (rc.y + rc.h <= sv->inner.y) return 1;
	return 0;
}

/* ── 高度缓存布局 ───────────────────────────────────────────────────── */

void UiScrollLayout_Init(UiScrollLayout *l, UiScrollItem *items, int cap)
{
	if (!l) return;
	ZeroMemory(l, sizeof(*l));
	l->items = items;
	l->cap = cap > 0 ? cap : 0;
	l->revision = 1;
	l->dirty = 1;
}

void UiScrollLayout_SetCount(UiScrollLayout *l, int count)
{
	int old;
	if (!l) return;
	if (count < 0) count = 0;
	if (count > l->cap) count = l->cap;
	old = l->count;
	if (count == old && l->fixedH <= 0.0f) return;
	l->fixedH = 0.0f;
	l->count = count;
	if (count > old) {
		for (int i = old; i < count; i++) {
			ZeroMemory(&l->items[i], sizeof(l->items[i]));
		}
	}
	l->dirty = 1;
}

void UiScrollLayout_SetPadding(UiScrollLayout *l, float top, float bottom)
{
	if (!l) return;
	if (top < 0.0f) top = 0.0f;
	if (bottom < 0.0f) bottom = 0.0f;
	if (l->padTop == top && l->padBottom == bottom) return;
	l->padTop = top;
	l->padBottom = bottom;
	l->dirty = 1;
}

void UiScrollLayout_SetGap(UiScrollLayout *l, float gap)
{
	if (!l) return;
	if (gap < 0.0f) gap = 0.0f;
	if (l->gap == gap) return;
	l->gap = gap;
	l->dirty = 1;
}

void UiScrollLayout_SetFixed(UiScrollLayout *l, int count, float rowH)
{
	if (!l) return;
	if (count < 0) count = 0;
	if (rowH < 0.0f) rowH = 0.0f;
	if (l->fixedH == rowH && l->count == count) return;
	l->fixedH = rowH;
	l->count = count;
	l->dirty = 1;
}

float UiScrollLayout_Width(const UiScrollLayout *l)
{
	return l ? l->width : 0.0f;
}

int UiScrollLayout_SetWidth(UiScrollLayout *l, float width)
{
	int changed = 0;
	if (!l) return 0;
	if (width < 0.0f) width = 0.0f;
	if (l->width == width) return 0;
	l->width = width;
	if (l->fixedH <= 0.0f) {
		for (int i = 0; i < l->count; i++) {
			UiScrollItem *it = &l->items[i];
			if ((it->flags & UI_SCROLL_ITEM_WIDTH_DEPENDENT) && it->measured) {
				it->measured = 0;
				changed = 1;
			}
		}
	}
	if (changed) l->dirty = 1;
	return changed;
}

int UiScrollLayout_ItemNeedsMeasure(const UiScrollLayout *l, int index,
	uint32_t rev)
{
	const UiScrollItem *it;
	if (!l || index < 0 || index >= l->count || l->fixedH > 0.0f) return 0;
	it = &l->items[index];
	if (!it->measured) return 1;
	if (it->rev != rev) return 1;
	if ((it->flags & UI_SCROLL_ITEM_WIDTH_DEPENDENT) &&
		it->measuredW != l->width)
		return 1;
	return 0;
}

void UiScrollLayout_SetItemFlags(UiScrollLayout *l, int index, unsigned flags)
{
	UiScrollItem *it;
	if (!l || index < 0 || index >= l->count || l->fixedH > 0.0f) return;
	it = &l->items[index];
	if (it->flags == flags) return;
	it->flags = flags;
	it->measured = 0;
	l->dirty = 1;
}

void UiScrollLayout_SetItemHeight(UiScrollLayout *l, int index,
	uint32_t rev, float height)
{
	UiScrollItem *it;
	if (!l || index < 0 || index >= l->count || l->fixedH > 0.0f) return;
	if (height < 0.0f) height = 0.0f;
	it = &l->items[index];
	it->rev = rev;
	it->height = height;
	it->measuredW = l->width;
	it->measured = 1;
	l->dirty = 1;
}

void UiScrollLayout_InvalidateItem(UiScrollLayout *l, int index)
{
	if (!l || index < 0 || index >= l->count || l->fixedH > 0.0f) return;
	l->items[index].measured = 0;
	l->dirty = 1;
}

void UiScrollLayout_InvalidateAll(UiScrollLayout *l)
{
	if (!l || l->fixedH > 0.0f) return;
	for (int i = 0; i < l->count; i++) l->items[i].measured = 0;
	l->dirty = 1;
}

int UiScrollLayout_Commit(UiScrollLayout *l)
{
	float oldH, y;
	if (!l || !l->dirty) return 0;
	oldH = l->contentH;
	if (l->fixedH > 0.0f) {
		y = l->count > 0
			? (float)l->count * l->fixedH + (float)(l->count - 1) * l->gap
			: 0.0f;
		l->contentH = l->padTop + y + l->padBottom;
	} else {
		y = l->padTop;
		for (int i = 0; i < l->count; i++) {
			UiScrollItem *it = &l->items[i];
			it->y = y;
			y += it->measured ? it->height : 0.0f;
			if (i + 1 < l->count) y += l->gap;
		}
		l->contentH = y + l->padBottom;
	}
	l->revision++;
	l->dirty = 0;
	return l->contentH != oldH;
}

float UiScrollLayout_ContentHeight(const UiScrollLayout *l)
{
	return l ? l->contentH : 0.0f;
}

float UiScrollLayout_ItemY(const UiScrollLayout *l, int index)
{
	if (!l || index < 0 || index >= l->count) return 0.0f;
	if (l->fixedH > 0.0f)
		return l->padTop + (float)index * (l->fixedH + l->gap);
	return l->items[index].y;
}

void UiScrollLayout_VisibleRange(const UiScrollLayout *l,
	float offset, float viewH, int *first, int *last)
{
	int lo, hi, f, e;
	float top, bottom;
	if (first) *first = 0;
	if (last) *last = -1;
	if (!l || l->count <= 0 || viewH <= 0.0f) return;
	top = offset - l->padTop;
	bottom = offset + viewH - l->padTop;
	if (bottom <= 0.0f) return;

	if (l->fixedH > 0.0f) {
		float stride = l->fixedH + l->gap;
		if (stride <= 0.0f) {
			if (first) *first = 0;
			if (last) *last = l->count - 1;
			return;
		}
		f = top > 0.0f ? (int)(top / stride) : 0;
		/* top 落在 gap 中: 跳过已完全在视口上方的项. */
		if (f < l->count &&
			(float)f * stride + l->fixedH <= top)
			f++;
		e = bottom > 0.0f ? (int)((bottom - 0.001f) / stride) : 0;
		if (f < 0) f = 0;
		if (f >= l->count) f = l->count - 1;
		if (e < 0) e = 0;
		if (e >= l->count) e = l->count - 1;
		if (first) *first = f;
		if (last) *last = e;
		return;
	}

	/* first: 第一个 bottom > top 的项 (item.y + height 非递减). */
	lo = 0; hi = l->count;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		const UiScrollItem *it = &l->items[mid];
		if (it->y + (it->measured ? it->height : 0.0f) > top)
			hi = mid;
		else
			lo = mid + 1;
	}
	f = lo;
	/* last: 最后一个 y < bottom 的项. */
	lo = 0; hi = l->count;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (l->items[mid].y >= bottom)
			hi = mid;
		else
			lo = mid + 1;
	}
	e = lo - 1;
	if (e < 0) return;
	if (f >= l->count) f = l->count - 1;
	if (e >= l->count) e = l->count - 1;
	if (f > e) return;
	if (first) *first = f;
	if (last) *last = e;
}

UiRect UiScrollLayout_ItemRect(const UiScrollLayout *l, int index,
	UiRect contentRc)
{
	UiRect r = { 0, 0, 0, 0 };
	if (!l || index < 0 || index >= l->count) return r;
	r.x = contentRc.x;
	r.y = contentRc.y + UiScrollLayout_ItemY(l, index);
	r.w = contentRc.w;
	r.h = l->fixedH > 0.0f ? l->fixedH : l->items[index].height;
	return r;
}
