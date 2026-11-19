#include "ui/ui_color_select.h"
#include "ui/ui_paint.h"
#include "gfx/gfx_fx.h"

#define CS_PAD 12.0f
#define CS_W 240.0f
#define CS_SV_RATIO 0.63f
#define CS_BAR_H 14.0f
#define CS_GAP_SV 10.0f
#define CS_GAP_BAR 8.0f
#define CS_RADIUS 10.0f
#define CS_SWATCH 22.0f
#define CS_ERRS 1.0e-4f

static float cs_clamp_(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static float cs_abs_(float v)
{
	return v < 0.0f ? -v : v;
}

UiColor UiColor_HsvToRgb(float h, float s, float v, float a)
{
	int i, r, g, b;
	float f, p, q, t, al;
	while (h < 0.0f) h += 360.0f;
	while (h >= 360.0f) h -= 360.0f;
	s = cs_clamp_(s, 0.0f, 1.0f);
	v = cs_clamp_(v, 0.0f, 1.0f);
	al = cs_clamp_(a, 0.0f, 1.0f);
	h /= 60.0f;
	i = (int)h;
	f = h - (float)i;
	p = v * (1.0f - s);
	q = v * (1.0f - s * f);
	t = v * (1.0f - s * (1.0f - f));
	switch (i) {
	case 0: r = (int)(v * 255.0f + 0.5f); g = (int)(t * 255.0f + 0.5f);
		b = (int)(p * 255.0f + 0.5f); break;
	case 1: r = (int)(q * 255.0f + 0.5f); g = (int)(v * 255.0f + 0.5f);
		b = (int)(p * 255.0f + 0.5f); break;
	case 2: r = (int)(p * 255.0f + 0.5f); g = (int)(v * 255.0f + 0.5f);
		b = (int)(t * 255.0f + 0.5f); break;
	case 3: r = (int)(p * 255.0f + 0.5f); g = (int)(q * 255.0f + 0.5f);
		b = (int)(v * 255.0f + 0.5f); break;
	case 4: r = (int)(t * 255.0f + 0.5f); g = (int)(p * 255.0f + 0.5f);
		b = (int)(v * 255.0f + 0.5f); break;
	default: r = (int)(v * 255.0f + 0.5f); g = (int)(p * 255.0f + 0.5f);
		b = (int)(q * 255.0f + 0.5f); break;
	}
	return ((UiColor)(int)(al * 255.0f + 0.5f) << 24)
		| ((UiColor)r << 16) | ((UiColor)g << 8) | (UiColor)b;
}

void UiColor_RgbToHsv(UiColor c, float *h, float *s, float *v, float *a)
{
	float r = (float)((c >> 16) & 0xFF) / 255.0f;
	float g = (float)((c >> 8) & 0xFF) / 255.0f;
	float b = (float)(c & 0xFF) / 255.0f;
	float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
	float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
	float d = mx - mn;
	float hh = 0.0f;
	if (d > 0.0f) {
		if (mx == r) hh = (g - b) / d;
		else if (mx == g) hh = (b - r) / d + 2.0f;
		else hh = (r - g) / d + 4.0f;
		hh *= 60.0f;
		if (hh < 0.0f) hh += 360.0f;
	}
	if (h) *h = hh;
	if (s) *s = mx > 0.0f ? d / mx : 0.0f;
	if (v) *v = mx;
	if (a) *a = (float)((c >> 24) & 0xFF) / 255.0f;
}

void UiColorSelect_Init(UiColorSelect *cs, UiId id, UiColor initial)
{
	if (!cs) return;
	ZeroMemory(cs, sizeof(*cs));
	cs->id = id ? id : 1;
	UiColor_RgbToHsv(initial, &cs->h, &cs->s, &cs->v, &cs->a);
	cs->arrowTarget = -1;
}

void UiColorSelect_Shutdown(UiColorSelect *cs)
{
	if (!cs) return;
	if (cs->arrowPart) {
		Gfx_PartDestroy((GfxPart *)cs->arrowPart);
		cs->arrowPart = NULL;
	}
}

void UiColorSelect_SetColor(UiColorSelect *cs, UiColor c)
{
	float h, s, v, a;
	if (!cs) return;
	UiColor_RgbToHsv(c, &h, &s, &v, &a);
	/* 灰/黑没有色相信息: 保留组件内原有 h, 避免拖动跳变 */
	if (s > 1.0e-6f && v > 1.0e-6f) cs->h = h;
	cs->s = s;
	cs->v = v;
	cs->a = a;
}

void UiColorSelect_SetHSVA(UiColorSelect *cs, float h, float s, float v,
	float a)
{
	if (!cs) return;
	while (h < 0.0f) h += 360.0f;
	while (h >= 360.0f) h -= 360.0f;
	cs->h = h;
	cs->s = cs_clamp_(s, 0.0f, 1.0f);
	cs->v = cs_clamp_(v, 0.0f, 1.0f);
	cs->a = cs_clamp_(a, 0.0f, 1.0f);
}

UiColor UiColorSelect_GetColor(const UiColorSelect *cs)
{
	if (!cs) return 0xFF000000u;
	return UiColor_HsvToRgb(cs->h, cs->s, cs->v, cs->a);
}

void UiColorSelect_GetHSVA(const UiColorSelect *cs, float *h, float *s,
	float *v, float *a)
{
	if (!cs) return;
	if (h) *h = cs->h;
	if (s) *s = cs->s;
	if (v) *v = cs->v;
	if (a) *a = cs->a;
}

int UiColorSelect_IsOpen(const UiPopupManager *pm, const UiColorSelect *cs)
{
	return pm && cs && UiPopupManager_IsOpen(pm, cs->id);
}

/* ── 小工具 ────────────────────────────────────────────────────────── */

static UiId cs_child_(UiId id, const char *scope, int sub)
{
	return Ui_IdIndex(scope, (int)(uint32_t)(id >> 32),
		(int)(uint32_t)id, sub);
}

static void cs_hex_(UiColor c, char *buf)
{
	static const char kHex[] = "0123456789ABCDEF";
	int i;
	buf[0] = '#';
	for (i = 0; i < 8; i++) {
		unsigned v = ((unsigned)c >> (28 - i * 4)) & 0xFu;
		buf[1 + i] = kHex[v];
	}
	buf[9] = 0;
}

/* 棋盘格 (透明度底), 只画在给定矩形内 */
static void cs_checker_(Gfx *g, UiRect rc, float cell)
{
	int x, y, nx, ny;
	if (!g || rc.w <= 0.0f || rc.h <= 0.0f || cell < 2.0f) return;
	nx = (int)(rc.w / cell) + 1;
	ny = (int)(rc.h / cell) + 1;
	for (y = 0; y < ny; y++) {
		for (x = 0; x < nx; x++) {
			UiColor c = ((x + y) & 1) ? 0xFFFFFFFFu : 0xFFE6E9EFu;
			float x0 = rc.x + (float)x * cell;
			float y0 = rc.y + (float)y * cell;
			float w = cell, h = cell;
			if (x0 + w > rc.x + rc.w) w = rc.x + rc.w - x0;
			if (y0 + h > rc.y + rc.h) h = rc.y + rc.h - y0;
			if (w <= 0.0f || h <= 0.0f) continue;
			Gfx_FillRect(g, Ui_Rect(x0, y0, w, h), c);
		}
	}
}

static void cs_arrow_draw_(Gfx *g, void *user)
{
	UiColorSelect *cs = (UiColorSelect *)user;
	Gfx_Line(g, 4.0f, 6.6f, 9.0f, 11.8f, cs->arrowColor, 1.6f);
	Gfx_Line(g, 9.0f, 11.8f, 14.0f, 6.6f, cs->arrowColor, 1.6f);
}

static void cs_draw_arrow_(UiContext *ui, UiRect cr, float t, UiColor col)
{
	Gfx *g = ui->gfx;
	float cx = cr.x + cr.w * 0.5f;
	float cy = cr.y + cr.h * 0.5f;
	Gfx_PushRotate(g, cx, cy, 180.0f * t);
	Gfx_Line(g, cx - 5.0f, cy - 2.4f, cx, cy + 2.8f, col, 1.6f);
	Gfx_Line(g, cx, cy + 2.8f, cx + 5.0f, cy - 2.4f, col, 1.6f);
	Gfx_PopTransform(g);
}

/* ── 触发器 ────────────────────────────────────────────────────────── */

int UiColorSelect_Draw(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UiRect rc, int enabled)
{
	UiTheme *t;
	UiWidgetState st;
	UiRect sw, ar, tr;
	UiColor color, arrowCol;
	char hex[12];
	int open, open0, target;

	if (!cs || !ui || !ui->theme || !ui->gfx) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	t = ui->theme;
	cs->changed = 0;
	cs->committed = 0;
	cs->anchor = rc;

	open = UiColorSelect_IsOpen(pm, cs);
	open0 = open;
	if (!open && cs->wasOpen) {
		if (cs->dirty) {
			cs->committed = 1;
			cs->dirty = 0;
		}
		cs->panelFx = 0;
		Ui_FxChange(ui, cs->id, Ui_AnimTweenMs(UI_AN_TWEEN_PANEL));
	}
	cs->wasOpen = open;

	st = Ui_State(ui, cs->id, rc, enabled);
	if (enabled && ui->clicked == cs->id) {
		ui->navId = cs->id;
		if (open) {
			UiPopupManager_Close(pm, ui);
			open = 0;
		} else if (pm) {
			UiPopupManager_Open(pm, ui, cs->id, cs->id,
				ui->navFocusVisible ? 1 : 0);
			open = UiColorSelect_IsOpen(pm, cs);
			if (open) cs->panelFx = 1;
		}
		cs->wasOpen = open;
		if (open != open0)
			Ui_FxChange(ui, cs->id,
				Ui_AnimTweenMs(UI_AN_TWEEN_PANEL));
	}
	if (!enabled && open) {
		UiPopupManager_Close(pm, ui);
		cs->wasOpen = 0;
		if (cs->dirty) {
			cs->committed = 1;
			cs->dirty = 0;
		}
	}

	if (enabled && st.glow > 0.004f)
		Ui_DrawGlowFx(ui, rc, 6.0f, 4.0f,
			Ui_ColorAlpha(t->primary.base, 80), st.glow);

	Gfx_FillRoundRect(ui->gfx, rc, 6.0f,
		enabled ? t->neutral.base : t->neutral.soft);
	if (enabled && st.hover > 0.004f)
		Gfx_FillRoundRect(ui->gfx, rc, 6.0f,
			Ui_ColorAlpha(t->primary.base, (int)(st.hover * 16.0f)));
	{
		UiColor border = t->neutral.border;
		if (enabled) {
			float k = st.hover * 0.6f;
			if (cs->wasOpen) k = 1.0f;
			border = Ui_ColorMix(t->neutral.border, t->primary.base, k);
		}
		Gfx_StrokeRoundRect(ui->gfx, rc, 6.0f, border, 1.0f);
	}

	/* 色块 (棋盘底 + 当前色) */
	{
		float sz = rc.h - 12.0f;
		if (sz > CS_SWATCH) sz = CS_SWATCH;
		if (sz < 10.0f) sz = 10.0f;
		sw = Ui_Rect(rc.x + 8.0f, rc.y + (rc.h - sz) * 0.5f, sz, sz);
		Gfx_FillRoundRect(ui->gfx, sw, 6.0f, t->neutral.soft);
		cs_checker_(ui->gfx, Ui_RectInset(sw, 2.0f), 5.0f);
		color = UiColorSelect_GetColor(cs);
		Gfx_FillRoundRect(ui->gfx, sw, 6.0f, color);
		Gfx_StrokeRoundRect(ui->gfx, sw, 6.0f, t->neutral.border, 1.0f);
	}

	/* hex 文本 (窄触发器省略) */
	ar = Ui_Rect(rc.x + rc.w - 24.0f, rc.y + (rc.h - 18.0f) * 0.5f,
		18.0f, 18.0f);
	tr = Ui_Rect(sw.x + sw.w + 10.0f, rc.y,
		ar.x - (sw.x + sw.w + 10.0f) - 4.0f, rc.h);
	if (tr.w >= 60.0f) {
		cs_hex_(color, hex);
		Gfx_Text(ui->gfx, t->fBody, hex, tr,
			enabled ? t->text : t->btnDisabledText,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}

	/* chevron: 合成器路径旋转交给 part, 否则本地 180ms 补间 */
	target = cs->wasOpen ? 1 : 0;
	if (Ui_FxActive(ui)) {
		GfxPart *part;
		unsigned col = !enabled ? t->btnDisabledText
			: (target ? t->primary.base : t->text3);
		cs->arrowAngle = (float)target;
		cs->arrowAnim = 0;
		cs->arrowTarget = target;
		if (!cs->arrowPart) {
			cs->arrowColor = col;
			cs->arrowPart = Gfx_PartCreate(ui->gfx, ar.w, ar.h,
				cs_arrow_draw_, cs);
			if (cs->arrowPart) {
				Gfx_PartUpdate((GfxPart *)cs->arrowPart);
				Ui_PartPlace(ui, cs->arrowPart, ar.x, ar.y, ar.w, ar.h);
				Gfx_PartSetRotation((GfxPart *)cs->arrowPart,
					180.0f * (float)target);
				cs->arrowPartTarget = target;
			}
		} else if (cs->arrowColor != col) {
			cs->arrowColor = col;
			Gfx_PartUpdate((GfxPart *)cs->arrowPart);
		}
		part = (GfxPart *)cs->arrowPart;
		if (part) {
			Ui_PartPlace(ui, cs->arrowPart, ar.x, ar.y, ar.w, ar.h);
			if (cs->arrowPartTarget != target) {
				Gfx_PartAnimateRotation(part, 180.0f * (float)target,
					Ui_AnimTweenMs(UI_AN_TWEEN_ARROW));
				cs->arrowPartTarget = target;
			}
		}
		return cs->changed;
	}
	if (cs->arrowTarget != target) {
		cs->arrowTarget = target;
		cs->arrowFrom = cs->arrowAngle;
		cs->arrowT0 = (double)GetTickCount64();
		cs->arrowAnim = ui->animReduced ? 0 : 1;
		if (ui->animReduced) cs->arrowAngle = (float)target;
	}
	if (cs->arrowAnim) {
		double now = (double)GetTickCount64();
		float p = (float)((now - cs->arrowT0) /
			(double)Ui_AnimTweenMs(UI_AN_TWEEN_ARROW));
		if (p >= 1.0f) {
			cs->arrowAngle = (float)target;
			cs->arrowAnim = 0;
		} else {
			float q = 1.0f - p;
			cs->arrowAngle = cs->arrowFrom +
				((float)target - cs->arrowFrom) * (1.0f - q * q * q);
		}
		Ui_InvalidateRectPad(ui, rc, 4.0f);
	}
	arrowCol = !enabled ? t->btnDisabledText
		: Ui_ColorMix(t->text3, t->primary.base, cs->arrowAngle);
	cs_draw_arrow_(ui, ar, cs->arrowAngle, arrowCol);
	return cs->changed;
}

/* ── 弹层 ──────────────────────────────────────────────────────────── */

static void cs_touch_(UiColorSelect *cs, UiContext *ui)
{
	cs->changed = 1;
	cs->dirty = 1;
	if (cs->panelRc.w > 0.0f)
		Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
}

static void cs_popup_input_(UiColorSelect *cs, UiContext *ui,
	UiId svId, UiId hueId, UiId alphaId)
{
	if (!ui->mouseDown) {
		if (cs->drag) {
			cs->drag = 0;
			if (cs->dirty) {
				cs->committed = 1;
				cs->dirty = 0;
			}
		}
		return;
	}
	if (!cs->drag) {
		if (ui->down == svId) cs->drag = 1;
		else if (ui->down == hueId) cs->drag = 2;
		else if (ui->down == alphaId) cs->drag = 3;
	}
	if (!cs->drag) return;

	if (cs->drag == 1) {
		float s = cs_clamp_((ui->mouseX - cs->svRc.x) / cs->svRc.w, 0.0f, 1.0f);
		float v = 1.0f - cs_clamp_((ui->mouseY - cs->svRc.y) / cs->svRc.h,
			0.0f, 1.0f);
		if (cs_abs_(s - cs->s) > CS_ERRS || cs_abs_(v - cs->v) > CS_ERRS) {
			cs->s = s;
			cs->v = v;
			cs_touch_(cs, ui);
		}
	} else if (cs->drag == 2) {
		float h = cs_clamp_((ui->mouseX - cs->hueRc.x) / cs->hueRc.w,
			0.0f, 0.9999f) * 360.0f;
		if (cs_abs_(h - cs->h) > 0.05f) {
			cs->h = h;
			cs_touch_(cs, ui);
		}
	} else {
		float a = cs_clamp_((ui->mouseX - cs->alphaRc.x) / cs->alphaRc.w,
			0.0f, 1.0f);
		if (cs_abs_(a - cs->a) > CS_ERRS) {
			cs->a = a;
			cs_touch_(cs, ui);
		}
	}
}

void UiColorSelect_DrawPopup(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui)
{
	UiTheme *t;
	UiRect panel, sv, hue, alpha;
	UiColor color, hueCol;
	UiId svId, hueId, alphaId;
	float w, h, svH, r, marker;
	int i, n;

	if (!cs || !pm || !ui || !ui->theme || !ui->gfx) return;
	if (!UiColorSelect_IsOpen(pm, cs)) {
		cs->drag = 0;
		return;
	}
	t = ui->theme;
	w = CS_W;
	if (ui->width > 0.0f && w > ui->width - 16.0f) w = ui->width - 16.0f;
	if (w < 120.0f) w = 120.0f;
	svH = (w - CS_PAD * 2.0f) * CS_SV_RATIO;
	h = CS_PAD + svH + CS_GAP_SV + CS_BAR_H + CS_GAP_BAR + CS_BAR_H
		+ CS_PAD;
	panel = UiPopup_Place(ui, cs->anchor, w, h, UI_POPUP_BELOW,
		UI_POPUP_ALIGN_START, 6.0f);
	cs->panelRc = panel;
	UiPopupManager_SetVisualRect(pm, panel);
	Ui_OccludeRect(ui, Ui_Rect(panel.x - 12.0f, panel.y - 12.0f,
		panel.w + 24.0f, panel.h + 24.0f));

	if (cs->panelFx && Ui_FxActive(ui)) {
		cs->panelFx = 0;
		Ui_FxChangeRect(ui, panel, Ui_AnimTweenMs(UI_AN_TWEEN_PANEL));
		return;
	}

	sv = Ui_Rect(panel.x + CS_PAD, panel.y + CS_PAD,
		panel.w - CS_PAD * 2.0f, svH);
	hue = Ui_Rect(sv.x, sv.y + sv.h + CS_GAP_SV, sv.w, CS_BAR_H);
	alpha = Ui_Rect(sv.x, hue.y + hue.h + CS_GAP_BAR, sv.w, CS_BAR_H);
	cs->svRc = sv;
	cs->hueRc = hue;
	cs->alphaRc = alpha;
	r = CS_RADIUS;

	svId = cs_child_(cs->id, "ui.color.sv", 0);
	hueId = cs_child_(cs->id, "ui.color.hue", 0);
	alphaId = cs_child_(cs->id, "ui.color.alpha", 0);

	UiPopupManager_BeginDraw(pm, ui);
	Ui_PushLayer(ui, UI_Z_OVERLAY);

	/* 先登记命中并处理拖拽, 再按新值绘制 (无 1 帧滞后) */
	Ui_StateInstant(ui, svId, sv, 1);
	Ui_StateInstant(ui, hueId, hue, 1);
	Ui_StateInstant(ui, alphaId, alpha, 1);
	cs_popup_input_(cs, ui, svId, hueId, alphaId);

	Gfx_FillRoundRect(ui->gfx, panel, r, t->toastBg);
	Gfx_StrokeRoundRect(ui->gfx, panel, r, t->toastBorder, 1.0f);

	color = UiColorSelect_GetColor(cs);
	hueCol = UiColor_HsvToRgb(cs->h, 1.0f, 1.0f, 1.0f);

	/* SV: 白→纯色相, 再叠透明→黑 */
	Gfx_FillRoundRectGradH(ui->gfx, sv, 8.0f, 0xFFFFFFFFu, hueCol);
	Gfx_FillRoundRectGradV(ui->gfx, sv, 8.0f, 0x00000000u, 0xFF000000u);
	Gfx_StrokeRoundRect(ui->gfx, sv, 8.0f,
		Ui_ColorAlpha(t->text3, 60), 1.0f);

	/* 色相条: 红色胶囊底 + 54 段实色片, 两端圆头保持纯红 (0/360) */
	{
		float barR = CS_BAR_H * 0.5f;
		Gfx_FillRoundRect(ui->gfx, hue, barR,
			UiColor_HsvToRgb(0.0f, 1.0f, 1.0f, 1.0f));
		n = 54;
		for (i = 0; i < n; i++) {
			float x0 = hue.x + hue.w * (float)i / (float)n;
			float x1 = hue.x + hue.w * (float)(i + 1) / (float)n;
			float hh = ((x0 + x1) * 0.5f - hue.x) / hue.w * 360.0f;
			Gfx_FillRect(ui->gfx, Ui_Rect(x0, hue.y + 1.0f,
				x1 - x0 + 0.5f, hue.h - 2.0f),
				UiColor_HsvToRgb(hh, 1.0f, 1.0f, 1.0f));
		}
		Gfx_StrokeRoundRect(ui->gfx, hue, barR,
			Ui_ColorAlpha(t->text3, 60), 1.0f);
	}

	/* 透明度条: 棋盘 + 透明→不透明渐变 */
	{
		float barR = CS_BAR_H * 0.5f;
		cs_checker_(ui->gfx, Ui_RectInset(alpha, 2.0f), 5.0f);
		Gfx_FillRoundRectGradH(ui->gfx, alpha, barR,
			Ui_ColorAlpha(color, 0), Ui_ColorAlpha(color, 255));
		Gfx_StrokeRoundRect(ui->gfx, alpha, barR,
			Ui_ColorAlpha(t->text3, 80), 1.0f);
	}

	/* 指示标 */
	{
		float cx = sv.x + cs->s * sv.w;
		float cy = sv.y + (1.0f - cs->v) * sv.h;
		Gfx_StrokeEllipse(ui->gfx, cx, cy, 7.0f, 0x90000000u, 2.0f);
		Gfx_StrokeEllipse(ui->gfx, cx, cy, 6.0f, 0xFFFFFFFFu, 2.0f);
	}
	marker = (CS_BAR_H + 6.0f) * 0.5f;
	{
		float hx = hue.x + (cs->h / 360.0f) * hue.w;
		UiRect mk = Ui_Rect(hx - 3.0f, hue.y + hue.h * 0.5f - marker,
			6.0f, marker * 2.0f);
		Gfx_FillRoundRect(ui->gfx, mk, 3.0f, 0xFFFFFFFFu);
		Gfx_StrokeRoundRect(ui->gfx, mk, 3.0f, 0x90000000u, 1.0f);
	}
	{
		float ax = alpha.x + cs->a * alpha.w;
		UiRect mk = Ui_Rect(ax - 3.0f, alpha.y + alpha.h * 0.5f - marker,
			6.0f, marker * 2.0f);
		Gfx_FillRoundRect(ui->gfx, mk, 3.0f, 0xFFFFFFFFu);
		Gfx_StrokeRoundRect(ui->gfx, mk, 3.0f, 0x90000000u, 1.0f);
	}

	Ui_PopLayer(ui);
	UiPopupManager_EndDraw(pm, ui);
}

int UiColorSelect_Key(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UINT vk)
{
	if (!cs || !pm || !ui) return 0;
	if (!UiColorSelect_IsOpen(pm, cs)) return 0;
	return UiPopupManager_Key(pm, ui, vk);
}
