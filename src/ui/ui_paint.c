#include "ui/ui_paint.h"

/* 光晕: 3 圈逐渐外扩、alpha 递减的描边。比 D2D 高斯模糊便宜得多,
 * 安装器同时最多一两个悬停控件, 开销可以忽略。 */
static void draw_glow(Gfx *g, UiRect rc, const UiSurfaceStyle *s, float intensity)
{
	int base, k;
	if (!g || !s->glow || intensity <= 0.004f) return;
	base = (int)((s->glow >> 24) & 0xFF);
	for (k = 2; k >= 0; k--) {
		/* 随亮度向外扩张: 0 -> 贴着边缘, 1 -> 完整光晕 "从边缘亮起来" */
		float infl = s->glowW * (float)(k + 1) / 3.0f * (0.35f + 0.65f * intensity);
		UiRect r;
		int a = (int)((float)base * intensity * (0.42f - 0.12f * (float)k));
		r.x = rc.x - infl;
		r.y = rc.y - infl;
		r.w = rc.w + infl * 2.0f;
		r.h = rc.h + infl * 2.0f;
		if (a <= 1) continue;
		Gfx_StrokeRoundRect(g, r, s->radius + infl,
			Ui_ColorAlpha(s->glow, a), 1.2f + (float)k);
	}
}

static void fill_flat(Gfx *g, UiRect rc, const UiSurfaceStyle *s, UiColor c)
{
	if (s->useCorners) Gfx_FillRoundRectCorners(g, rc, s->corners, c);
	else Gfx_FillRoundRect(g, rc, s->radius, c);
}

static void fill_grad(Gfx *g, UiRect rc, const UiSurfaceStyle *s,
	UiColor top, UiColor bottom)
{
	if (s->useCorners) Gfx_FillRoundRectCornersGradV(g, rc, s->corners, top, bottom);
	else Gfx_FillRoundRectGradV(g, rc, s->radius, top, bottom);
}

UiWidgetState Ui_DrawSurface(UiContext *ui, UiId id, UiRect rc,
	const UiSurfaceStyle *s, int enabled)
{
	UiWidgetState st = Ui_State(ui, id, rc, enabled);
	Gfx *g = ui ? ui->gfx : 0;
	UiColor fill, border;
	float bw;
	int wantGlow;

	if (!g || !s) return st;

	/* 光晕只由动画值驱动: 进入时从 0 亮起, 离开时渐暗 —— 不能用离散 hovered
	 * 做门闩, 否则离开瞬间直接消失、再进入时残留值让它跳亮 (真机反馈)。 */
	wantGlow = ((s->glowMask & UI_GLOW_HOVER) && st.hovered)
		|| ((s->glowMask & UI_GLOW_FOCUS) && st.focused);
	Ui_AnimSmooth(ui, id, UI_AN_GLOW, wantGlow ? 1.0f : 0.0f);
	st.glow = Ui_AnimGet(ui, id, UI_AN_GLOW, 0.0f);
	if (st.glow > 0.004f) draw_glow(g, rc, s, st.glow);

	/* 渐变面 + 两端同色的 hover/press: 等价于"固定渐变 + 半透明纯色叠加"。
	 * 这样渐变画刷颜色恒定, 悬停动画期间不会再每帧重建 stop collection。 */
	if (s->bg2 && s->overlayTint
		&& (!s->hoverBg || (s->hoverBg >> 24) == 0xFF)
		&& (!s->pressBg || (s->pressBg >> 24) == 0xFF)) {
		fill_grad(g, rc, s, s->bg, s->bg2);
		if (s->hoverBg && st.hover > 0.004f)
			fill_flat(g, rc, s,
				Ui_ColorAlpha(s->hoverBg, (int)(st.hover * 255.0f + 0.5f)));
		if (s->pressBg && st.press > 0.004f)
			fill_flat(g, rc, s,
				Ui_ColorAlpha(s->pressBg, (int)(st.press * 255.0f + 0.5f)));
	} else {
		fill = s->bg;
		if (s->hoverBg) fill = Ui_ColorMix(fill, s->hoverBg, st.hover);
		if (s->pressBg) fill = Ui_ColorMix(fill, s->pressBg, st.press);
		if (s->bg2) {
			/* 端点各自插值 (常见于两个分量不同的高亮色) */
			UiColor lo = s->bg2;
			if (s->hoverBg) lo = Ui_ColorMix(lo, s->hoverBg, st.hover);
			if (s->pressBg) lo = Ui_ColorMix(lo, s->pressBg, st.press);
			fill_grad(g, rc, s, fill, lo);
		} else {
			fill_flat(g, rc, s, fill);
		}
	}

	if (s->border) {
		border = s->border;
		if (s->hoverBorder) border = Ui_ColorMix(border, s->hoverBorder, st.hover);
		if (s->focusBorder) border = Ui_ColorMix(border, s->focusBorder, st.focus);
		bw = s->borderW > 0.0f ? s->borderW : 1.0f;
		bw += 0.6f * st.focus;
		if (s->useCorners) {
			if (s->dashed)
				Gfx_StrokeRoundRectCornersDashed(g, rc, s->corners, border, bw);
			else
				Gfx_StrokeRoundRectCorners(g, rc, s->corners, border, bw);
		} else if (s->dashed) {
			Gfx_StrokeRoundRectCornersDashed(g, rc,
				Gfx_CornersAll(s->radius), border, bw);
		} else {
			Gfx_StrokeRoundRect(g, rc, s->radius, border, bw);
		}
	}
	return st;
}
