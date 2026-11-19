#include "ui/ui_color_select.h"
#include "ui/ui_paint.h"
#include "gfx/gfx_fx.h"

#define CS_PAD 12.0f
#define CS_W 252.0f
#define CS_HUE_W 16.0f
#define CS_HUE_GAP 10.0f
#define CS_SV_RATIO 0.66f
#define CS_BAR_H 14.0f
#define CS_FIELD_H 26.0f
#define CS_GAP 10.0f
#define CS_AFIELD_W 52.0f
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

static void cs_sync_edits_(UiColorSelect *cs);

void UiColorSelect_Init(UiColorSelect *cs, UiId id, UiColor initial)
{
	if (!cs) return;
	ZeroMemory(cs, sizeof(*cs));
	cs->id = id ? id : 1;
	cs->alphaEnabled = 1;
	Edit_Init(&cs->editHex, 6, 0, "0123456789abcdefABCDEF");
	Edit_Init(&cs->editA, 3, 0, "0123456789");
	UiColor_RgbToHsv(initial, &cs->h, &cs->s, &cs->v, &cs->a);
	cs->arrowTarget = -1;
	cs_sync_edits_(cs);
}

void UiColorSelect_Shutdown(UiColorSelect *cs)
{
	if (!cs) return;
	if (cs->arrowPart) {
		Gfx_PartDestroy((GfxPart *)cs->arrowPart);
		cs->arrowPart = NULL;
	}
	if (cs->layer) {
		Gfx_LayerDestroy((GfxLayer *)cs->layer);
		cs->layer = NULL;
	}
	Edit_Shutdown(&cs->editHex);
	Edit_Shutdown(&cs->editA);
}

void UiColorSelect_SetAlphaEnabled(UiColorSelect *cs, int on)
{
	if (!cs) return;
	cs->alphaEnabled = on ? 1 : 0;
	if (!cs->alphaEnabled) {
		cs->a = 1.0f;
		if (cs->focusField == 2) {
			cs->focusField = 0;
			if (cs->ime) UiIme_SetFocus(cs->ime, NULL);
		}
	}
	cs_sync_edits_(cs);
}

int UiColorSelect_AlphaEnabled(const UiColorSelect *cs)
{
	return cs ? cs->alphaEnabled : 0;
}

void UiColorSelect_AttachIme(UiColorSelect *cs, UiIme *ime)
{
	if (cs) cs->ime = ime;
}

/* 把组件颜色回写到未聚焦的编辑框 (HEX 6 位 / 透明度 0..255). */
static void cs_sync_edits_(UiColorSelect *cs)
{
	char buf[16];
	UiColor c;
	if (!cs) return;
	c = UiColorSelect_GetColor(cs);
	if (cs->focusField != 1) {
		wsprintfA(buf, "%06X", (unsigned)(c & 0x00FFFFFFu));
		Edit_SetTextUtf8(&cs->editHex, buf);
	}
	if (cs->focusField != 2 && cs->alphaEnabled) {
		wsprintfA(buf, "%d", (int)((c >> 24) & 0xFFu));
		Edit_SetTextUtf8(&cs->editA, buf);
	}
}

static int cs_hexval_(wchar_t ch)
{
	if (ch >= L'0' && ch <= L'9') return (int)(ch - L'0');
	if (ch >= L'a' && ch <= L'f') return 10 + (int)(ch - L'a');
	if (ch >= L'A' && ch <= L'F') return 10 + (int)(ch - L'A');
	return -1;
}

static int cs_parse_hex_(const wchar_t *w, UiColor *rgb)
{
	unsigned v = 0;
	int n = 0, d;
	if (!w || !rgb) return 0;
	for (; *w; w++) {
		if (*w == L'#' && n == 0) continue;
		d = cs_hexval_(*w);
		if (d < 0 || n >= 6) return 0;
		v = (v << 4) | (unsigned)d;
		n++;
	}
	if (n != 6) return 0;
	*rgb = 0xFF000000u | v;
	return 1;
}

static int cs_parse_dec_(const wchar_t *w, int *out)
{
	int v = 0, n = 0;
	if (!w || !out) return 0;
	for (; *w; w++) {
		if (*w < L'0' || *w > L'9' || n >= 3) return 0;
		v = v * 10 + (int)(*w - L'0');
		n++;
	}
	if (n == 0 || v > 255) return 0;
	*out = v;
	return 1;
}

static void cs_apply_edits_(UiColorSelect *cs)
{
	wchar_t w[16];
	if (!cs) return;
	if (cs->focusField == 1) {
		UiColor rgb;
		Edit_TextW(&cs->editHex, w, 16);
		if (cs_parse_hex_(w, &rgb)) {
			float h, s, v, a;
			UiColor_RgbToHsv(rgb, &h, &s, &v, &a);
			if (s > 1.0e-6f && v > 1.0e-6f) cs->h = h;
			cs->s = s;
			cs->v = v;
			cs->changed = 1;
			cs->dirty = 1;
		}
	} else if (cs->focusField == 2 && cs->alphaEnabled) {
		int a;
		Edit_TextW(&cs->editA, w, 16);
		if (cs_parse_dec_(w, &a)) {
			cs->a = (float)a / 255.0f;
			cs->changed = 1;
			cs->dirty = 1;
		}
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
	cs->a = cs->alphaEnabled ? a : 1.0f;
	cs_sync_edits_(cs);
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
	cs->a = cs->alphaEnabled ? cs_clamp_(a, 0.0f, 1.0f) : 1.0f;
	cs_sync_edits_(cs);
}

UiColor UiColorSelect_GetColor(const UiColorSelect *cs)
{
	if (!cs) return 0xFF000000u;
	return UiColor_HsvToRgb(cs->h, cs->s, cs->v,
		cs->alphaEnabled ? cs->a : 1.0f);
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

/* SV / 竖直色相 / 底部透明度条 + 指示标; 合成器路径下在 layer 局部坐标绘制. */
static void cs_draw_bars_(UiColorSelect *cs, Gfx *g, UiRect sv, UiRect hue,
	UiRect alpha)
{
	UiTheme *t = cs->ui->theme;
	UiColor color = UiColorSelect_GetColor(cs);
	UiColor hueCol = UiColor_HsvToRgb(cs->h, 1.0f, 1.0f, 1.0f);
	int i;

	Gfx_FillRoundRectGradH(g, sv, 8.0f, 0xFFFFFFFFu, hueCol);
	Gfx_FillRoundRectGradV(g, sv, 8.0f, 0x00000000u, 0xFF000000u);
	Gfx_StrokeRoundRect(g, sv, 8.0f, Ui_ColorAlpha(t->text3, 60), 1.0f);

	/* 竖直色相条: 红色胶囊底 + 内矩形裁剪的 6 段垂直渐变,
	 * 端帽保持纯红 (0°/360°), 不溢出圆角. */
	{
		float barR = CS_HUE_W * 0.5f;
		UiRect inner = Ui_Rect(hue.x, hue.y + barR, hue.w,
			hue.h - barR * 2.0f);
		Gfx_FillRoundRect(g, hue, barR,
			UiColor_HsvToRgb(0.0f, 1.0f, 1.0f, 1.0f));
		if (inner.h > 1.0f) {
			Gfx_PushClip(g, inner);
			for (i = 0; i < 6; i++) {
				float y0 = inner.y + inner.h * (float)i / 6.0f;
				float y1 = inner.y + inner.h * (float)(i + 1) / 6.0f;
				Gfx_FillRoundRectGradV(g,
					Ui_Rect(inner.x, y0, inner.w, y1 - y0 + 0.5f),
					0.0f,
					UiColor_HsvToRgb((float)i * 60.0f, 1.0f, 1.0f,
						1.0f),
					UiColor_HsvToRgb((float)(i + 1) * 60.0f, 1.0f,
						1.0f, 1.0f));
			}
			Gfx_PopClip(g);
		}
		Gfx_StrokeRoundRect(g, hue, barR, Ui_ColorAlpha(t->text3, 60),
			1.0f);
	}

	if (cs->alphaEnabled) {
		float barR = CS_BAR_H * 0.5f;
		cs_checker_(g, Ui_RectInset(alpha, 2.0f), 5.0f);
		Gfx_FillRoundRectGradH(g, alpha, barR,
			Ui_ColorAlpha(color, 0), Ui_ColorAlpha(color, 255));
		Gfx_StrokeRoundRect(g, alpha, barR, Ui_ColorAlpha(t->text3, 80),
			1.0f);
	}

	{
		float cx = sv.x + cs->s * sv.w;
		float cy = sv.y + (1.0f - cs->v) * sv.h;
		Gfx_StrokeEllipse(g, cx, cy, 7.0f, 0x90000000u, 2.0f);
		Gfx_StrokeEllipse(g, cx, cy, 6.0f, 0xFFFFFFFFu, 2.0f);
	}
	{
		float hy = hue.y + (cs->h / 360.0f) * hue.h;
		float half = (CS_HUE_W + 6.0f) * 0.5f;
		UiRect mk = Ui_Rect(hue.x + hue.w * 0.5f - half, hy - 3.0f,
			half * 2.0f, 6.0f);
		Gfx_FillRoundRect(g, mk, 3.0f, 0xFFFFFFFFu);
		Gfx_StrokeRoundRect(g, mk, 3.0f, 0x90000000u, 1.0f);
	}
	if (cs->alphaEnabled) {
		float ax = alpha.x + cs->a * alpha.w;
		float marker = (CS_BAR_H + 6.0f) * 0.5f;
		UiRect mk = Ui_Rect(ax - 3.0f,
			alpha.y + alpha.h * 0.5f - marker, 6.0f, marker * 2.0f);
		Gfx_FillRoundRect(g, mk, 3.0f, 0xFFFFFFFFu);
		Gfx_StrokeRoundRect(g, mk, 3.0f, 0x90000000u, 1.0f);
	}
}

static void cs_layer_draw_(Gfx *g, void *user)
{
	UiColorSelect *cs = (UiColorSelect *)user;
	cs_draw_bars_(cs, g, cs->lSv, cs->lHue, cs->lAlpha);
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
	/* committed 由应用在读取后清 0: 合成器拖动路径可能没有主窗帧,
	 * 在这里清会丢掉松手提交. */
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

static void cs_update_drag_(UiColorSelect *cs, UiContext *ui, float x, float y);

static void cs_touch_(UiColorSelect *cs, UiContext *ui)
{
	cs->changed = 1;
	cs->dirty = 1;
	cs_sync_edits_(cs);
	if (cs->fallback) {
		/* ULW 回退: 拖动态只能主窗逐帧重画 */
		if (cs->panelRc.w > 0.0f)
			Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
		return;
	}
	/* 合成器路径: 只标脏交互条 layer, 主窗零重绘 */
	cs->layerDirty = 1;
}

static void cs_popup_input_(UiColorSelect *cs, UiContext *ui,
	UiId svId, UiId hueId, UiId alphaId)
{
	static const int kDrag[4] = { 0, 1, 2, 3 };
	int kind = 0;
	if (!ui->mouseDown) {
		if (cs->drag) {
			cs->drag = 0;
			cs_sync_edits_(cs);
			if (cs->dirty) {
				cs->committed = 1;
				cs->dirty = 0;
			}
			if (cs->fallback)
				Ui_InvalidateRectPad(ui, cs->anchor, 8.0f);
		}
		return;
	}
	if (cs->drag == 3 && !cs->alphaEnabled) cs->drag = 0;
	/* pressed 在按住期间一直有效; down 只在按下那一帧有效 (帧末清空),
	 * 事件合并时单靠 down 会完全起不了拖. 每次按下都按 pressed 重新锚定,
	 * 前一次拖动若因丢 Up 未结算也能被新按下接管. */
	if (ui->pressed == svId || ui->down == svId) kind = 1;
	else if (ui->pressed == hueId || ui->down == hueId) kind = 2;
	else if (cs->alphaEnabled &&
		(ui->pressed == alphaId || ui->down == alphaId)) kind = 3;
	if (!kind) {
		/* 丢帧兜底: 按下帧没渲染时用按下坐标判条区域 */
		if (Ui_RectContains(cs->svRc, ui->pressX, ui->pressY)) kind = 1;
		else if (Ui_RectContains(cs->hueRc, ui->pressX, ui->pressY))
			kind = 2;
		else if (cs->alphaEnabled && cs->alphaRc.w > 0.0f &&
			Ui_RectContains(cs->alphaRc, ui->pressX, ui->pressY))
			kind = 3;
	}
	if (kind) {
		if (cs->drag != kDrag[kind] && cs->focusField) {
			cs_apply_edits_(cs);
			cs->focusField = 0;
			if (cs->ime) UiIme_SetFocus(cs->ime, NULL);
		}
		cs->drag = kDrag[kind];
	}
	if (!cs->drag) return;
	cs_update_drag_(cs, ui, ui->mouseX, ui->mouseY);
}

void UiColorSelect_DrawPopup(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui)
{
	UiTheme *t;
	UiRect panel, sv, hue, alpha, hexRc, aRc;
	UiId svId, hueId, alphaId, hexId, aId;
	float w, h, svW, svH, innerW, r;

	if (!cs || !pm || !ui || !ui->theme || !ui->gfx) return;
	if (!UiColorSelect_IsOpen(pm, cs)) {
		cs->drag = 0;
		cs->editHex.focused = 0;
		cs->editA.focused = 0;
		if (cs->focusField) {
			cs->focusField = 0;
			if (cs->ime) UiIme_SetFocus(cs->ime, NULL);
		}
		return;
	}
	t = ui->theme;
	w = CS_W;
	if (ui->width > 0.0f && w > ui->width - 16.0f) w = ui->width - 16.0f;
	if (w < 180.0f) w = 180.0f;
	innerW = w - CS_PAD * 2.0f;
	svW = innerW - CS_HUE_W - CS_HUE_GAP;
	svH = svW * CS_SV_RATIO;
	h = CS_PAD + CS_FIELD_H + CS_GAP + svH
		+ (cs->alphaEnabled ? CS_GAP + CS_BAR_H : 0.0f) + CS_PAD;
	panel = UiPopup_Place(ui, cs->anchor, w, h, UI_POPUP_BELOW,
		UI_POPUP_ALIGN_START, 6.0f);
	cs->panelRc = panel;
	UiPopupManager_SetVisualRect(pm, panel);
	Ui_OccludeRect(ui, Ui_Rect(panel.x - 12.0f, panel.y - 12.0f,
		panel.w + 24.0f, panel.h + 24.0f));

	/* 字段行 (顶部): HEX | 透明度 */
	hexRc = Ui_Rect(panel.x + CS_PAD, panel.y + CS_PAD,
		innerW - (cs->alphaEnabled ? CS_AFIELD_W + 8.0f : 0.0f),
		CS_FIELD_H);
	aRc = cs->alphaEnabled
		? Ui_Rect(panel.x + w - CS_PAD - CS_AFIELD_W, panel.y + CS_PAD,
			CS_AFIELD_W, CS_FIELD_H)
		: Ui_Rect(0, 0, 0, 0);
	sv = Ui_Rect(panel.x + CS_PAD, panel.y + CS_PAD + CS_FIELD_H + CS_GAP,
		svW, svH);
	hue = Ui_Rect(sv.x + svW + CS_HUE_GAP, sv.y, CS_HUE_W, svH);
	alpha = cs->alphaEnabled
		? Ui_Rect(panel.x + CS_PAD, sv.y + svH + CS_GAP, innerW, CS_BAR_H)
		: Ui_Rect(0, 0, 0, 0);
	cs->svRc = sv;
	cs->hueRc = hue;
	cs->alphaRc = alpha;
	cs->hexRc = hexRc;
	cs->aEditRc = aRc;
	r = CS_RADIUS;
	cs->ui = ui;
	/* layer 覆盖区域 = SV+色相(+透明度条) + 四周留白, 给滑块手柄
	 * (顶部/底部/左右各伸出 3px) 留出绘制空间, 避免被 layer 边缘剪裁. */
	{
		float pad = 4.0f;
		float barsH = svH + (cs->alphaEnabled ? CS_GAP + CS_BAR_H : 0.0f);
		cs->barsRc = Ui_Rect(sv.x - pad, sv.y - pad,
			innerW + pad * 2.0f, barsH + pad * 2.0f);
		cs->lSv = Ui_Rect(pad, pad, svW, svH);
		cs->lHue = Ui_Rect(pad + svW + CS_HUE_GAP, pad, CS_HUE_W, svH);
		cs->lAlpha = cs->alphaEnabled
			? Ui_Rect(pad, pad + svH + CS_GAP, innerW, CS_BAR_H)
			: Ui_Rect(0, 0, 0, 0);
	}
	cs->lAlphaOn = cs->alphaEnabled;

	if (cs->panelFx && Ui_FxActive(ui)) {
		cs->panelFx = 0;
		Ui_FxChangeRect(ui, panel, Ui_AnimTweenMs(UI_AN_TWEEN_PANEL));
		return;
	}

	svId = cs_child_(cs->id, "ui.color.sv", 0);
	hueId = cs_child_(cs->id, "ui.color.hue", 0);
	alphaId = cs_child_(cs->id, "ui.color.alpha", 0);
	hexId = cs_child_(cs->id, "ui.color.hex", 0);
	aId = cs_child_(cs->id, "ui.color.a", 0);

	UiPopupManager_BeginDraw(pm, ui);
	Ui_PushLayer(ui, UI_Z_OVERLAY);

	/* 合成器路径拖动期不注册条: 避免 hover 变化触发主窗失效;
	 * 拖动本身由 UiColorSelect_Mouse 直接驱动. */
	if (cs->fallback || cs->drag == 0) {
		Ui_StateInstant(ui, svId, sv, 1);
		Ui_StateInstant(ui, hueId, hue, 1);
		if (cs->alphaEnabled) Ui_StateInstant(ui, alphaId, alpha, 1);
	}
	cs_popup_input_(cs, ui, svId, hueId, alphaId);

	/* 输入框焦点: 按下即聚焦 (clicked 在部分事件合并下可能丢失),
	 * 完整点击时再放置光标. 点面板空白处失焦. */
	{
		/* 用按下坐标直接判矩形: 不依赖坑位 id 的注册时序, 事件合并时更稳 */
		int inHex = ui->mouseDown &&
			Ui_RectContains(hexRc, ui->mouseX, ui->mouseY);
		int inA = cs->alphaEnabled && ui->mouseDown &&
			Ui_RectContains(aRc, ui->mouseX, ui->mouseY);
		/* 按下/松开帧的 id 也认: 事件合并时 clicked 可能取不到命中项 */
		int hexHit = inHex || ui->clicked == hexId ||
			ui->down == hexId || ui->released == hexId;
		int aHit = inA || ui->clicked == aId ||
			ui->down == aId || ui->released == aId;
	if (hexHit) {
		if (cs->focusField != 1) {
			cs->focusField = 1;
			cs->blinkOn = 1;
			if (cs->ime) UiIme_SetFocus(cs->ime, &cs->editHex);
			Ui_InvalidateRectPad(ui, panel, 8.0f);
		}
		Edit_PlaceCaret(&cs->editHex, ui->gfx, t->fInput, hexRc,
			ui->mouseX);
	} else if (aHit) {
		if (cs->focusField != 2) {
			cs->focusField = 2;
			cs->blinkOn = 1;
			if (cs->ime) UiIme_SetFocus(cs->ime, &cs->editA);
			Ui_InvalidateRectPad(ui, panel, 8.0f);
		}
		Edit_PlaceCaret(&cs->editA, ui->gfx, t->fInput, aRc, ui->mouseX);
	} else if (ui->mouseDown && ui->down == UI_ID_NONE && cs->focusField) {
		/* 只在下按空白处时失焦; 松开事件 (mouseDown=0) 不能清掉刚点出的焦点 */
		if (Ui_RectContains(panel, ui->mouseX, ui->mouseY)) {
			cs_apply_edits_(cs);
			cs->focusField = 0;
			if (cs->ime) UiIme_SetFocus(cs->ime, NULL);
		}
	}
	}

	Gfx_FillRoundRect(ui->gfx, panel, r, t->toastBg);
	Gfx_StrokeRoundRect(ui->gfx, panel, r, t->toastBorder, 1.0f);

	/* 交互条: 合成器路径画在独立 layer (EndFrame), 回退才画主窗 */
	if (cs->fallback)
		cs_draw_bars_(cs, ui->gfx, sv, hue, alpha);

	/* HEX / 透明度编辑框 (自带主题表面与焦点描边) */
	cs->editHex.focused = (cs->focusField == 1);
	cs->editA.focused = (cs->focusField == 2);
	cs->editHex.caretOn = (cs->focusField == 1) ? cs->blinkOn : 0;
	cs->editA.caretOn = (cs->focusField == 2) ? cs->blinkOn : 0;
	Edit_Draw(ui, hexId, hexRc, &cs->editHex, NULL);
	if (cs->alphaEnabled) Edit_Draw(ui, aId, aRc, &cs->editA, NULL);
	cs_sync_edits_(cs);

	Ui_PopLayer(ui);
	UiPopupManager_EndDraw(pm, ui);
}

/* ── 交互条合成器层 ───────────────────────────────────────────────── */

static void cs_layer_destroy_(UiColorSelect *cs)
{
	if (cs->layer) {
		Gfx_LayerDestroy((GfxLayer *)cs->layer);
		cs->layer = NULL;
	}
	cs->layerW = cs->layerH = 0;
	cs->layerX = cs->layerY = -1.0e30f;
	cs->layerDirty = 0;
	cs->layerValid = 0;
	cs->layerClosing = 0;
}

void UiColorSelect_EndFrame(UiColorSelect *cs, double nowMs)
{
	UiContext *ui;
	Gfx *g;
	float scale;
	int pw, ph, dur;

	if (!cs) return;
	ui = cs->ui;
	if (!ui || !ui->gfx) return;
	g = ui->gfx;
	dur = Ui_AnimTweenMs(UI_AN_TWEEN_PANEL);

	if (!cs->wasOpen) {
		/* 关闭: layer 淡出后销毁 (面板主体由 snapshot 过渡负责) */
		if (cs->layer && !cs->layerClosing) {
			cs->layerClosing = 1;
			cs->closeAt = nowMs;
			Gfx_LayerSetOpacity((GfxLayer *)cs->layer, 0.0f, dur,
				GFX_EASE_OUT);
		} else if (cs->layer && nowMs >= cs->closeAt + (double)dur) {
			cs_layer_destroy_(cs);
		}
		cs->drag = 0;
		cs->layerDirty = 0;
		return;
	}
	if (cs->fallback) return;
	if (cs->layerClosing) {
		cs->layerClosing = 0;
		if (cs->layer)
			Gfx_LayerSetOpacity((GfxLayer *)cs->layer, 1.0f, dur,
				GFX_EASE_OUT);
	}
	if (cs->barsRc.w <= 0.0f || cs->barsRc.h <= 0.0f) return;
	scale = ui->scale > 0.0f ? ui->scale : 1.0f;
	pw = (int)(cs->barsRc.w * scale + 0.5f);
	ph = (int)(cs->barsRc.h * scale + 0.5f);
	if (pw <= 0 || ph <= 0) return;

	if (!cs->layer) {
		cs->layer = Gfx_CreateLayer(g, pw, ph);
		if (!cs->layer) {
			cs->fallback = 1;
			return;
		}
		cs->layerW = pw;
		cs->layerH = ph;
		cs->layerX = cs->layerY = -1.0e30f;
		cs->layerDirty = 1;
	} else if (pw != cs->layerW || ph != cs->layerH) {
		if (!Gfx_LayerResize((GfxLayer *)cs->layer, pw, ph)) {
			cs_layer_destroy_(cs);
			cs->fallback = 1;
			return;
		}
		cs->layerW = pw;
		cs->layerH = ph;
		cs->layerDirty = 1;
	}
	if (cs->layerX != cs->barsRc.x || cs->layerY != cs->barsRc.y) {
		Gfx_LayerSetPos((GfxLayer *)cs->layer, cs->barsRc.x, cs->barsRc.y);
		cs->layerX = cs->barsRc.x;
		cs->layerY = cs->barsRc.y;
	}
	if (cs->layerDirty) {
		if (!Gfx_LayerDraw((GfxLayer *)cs->layer, cs_layer_draw_, cs)) {
			cs_layer_destroy_(cs);
			cs->fallback = 1;
			return;
		}
		cs->layerDirty = 0;
		cs->layerValid = 1;
	}
	Gfx_LayerSetOpacity((GfxLayer *)cs->layer, 1.0f, 0, 0);
	if (!Gfx_LayerAlive((GfxLayer *)cs->layer)) {
		cs_layer_destroy_(cs);
		cs->fallback = 1;
	}
}

/* 拖动值更新 (Mouse 与帧内输入共用) */
static void cs_update_drag_(UiColorSelect *cs, UiContext *ui, float x, float y)
{
	if (cs->drag == 1) {
		float s = cs_clamp_((x - cs->svRc.x) / cs->svRc.w, 0.0f, 1.0f);
		float v = 1.0f - cs_clamp_((y - cs->svRc.y) / cs->svRc.h,
			0.0f, 1.0f);
		if (cs_abs_(s - cs->s) > CS_ERRS || cs_abs_(v - cs->v) > CS_ERRS) {
			cs->s = s;
			cs->v = v;
			cs_touch_(cs, ui);
		}
	} else if (cs->drag == 2) {
		float h = cs_clamp_((y - cs->hueRc.y) / cs->hueRc.h,
			0.0f, 0.9999f) * 360.0f;
		if (cs_abs_(h - cs->h) > 0.05f) {
			cs->h = h;
			cs_touch_(cs, ui);
		}
	} else if (cs->drag == 3 && cs->alphaEnabled) {
		float a = cs_clamp_((x - cs->alphaRc.x) / cs->alphaRc.w, 0.0f, 1.0f);
		if (cs_abs_(a - cs->a) > CS_ERRS) {
			cs->a = a;
			cs_touch_(cs, ui);
		}
	}
}

int UiColorSelect_Mouse(UiColorSelect *cs, UiContext *ui, int msg,
	float x, float y)
{
	int kind = 0;
	if (!cs || !ui) return 0;
	if (!cs->wasOpen || cs->fallback) return 0;
	if (msg == UI_COLOR_MOUSE_DOWN) {
		/* 输入框焦点在原始鼠标事件里落实: 面板帧可能尚未绘制,
		 * 等帧内命中会丢焦点 (尤其是连续点击 HEX/透明度框). */
		if (cs->hexRc.w > 0.0f && Ui_RectContains(cs->hexRc, x, y)) {
			if (cs->focusField && cs->focusField != 1)
				cs_apply_edits_(cs);
			if (cs->focusField != 1) {
				cs->focusField = 1;
				cs->blinkOn = 1;
				if (cs->ime) UiIme_SetFocus(cs->ime, &cs->editHex);
			}
			Edit_PlaceCaret(&cs->editHex, ui->gfx, ui->theme->fInput,
				cs->hexRc, x);
			Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
			return 1;
		}
		if (cs->alphaEnabled && cs->aEditRc.w > 0.0f &&
			Ui_RectContains(cs->aEditRc, x, y)) {
			if (cs->focusField && cs->focusField != 2)
				cs_apply_edits_(cs);
			if (cs->focusField != 2) {
				cs->focusField = 2;
				cs->blinkOn = 1;
				if (cs->ime) UiIme_SetFocus(cs->ime, &cs->editA);
			}
			Edit_PlaceCaret(&cs->editA, ui->gfx, ui->theme->fInput,
				cs->aEditRc, x);
			Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
			return 1;
		}
		if (Ui_RectContains(cs->svRc, x, y)) kind = 1;
		else if (Ui_RectContains(cs->hueRc, x, y)) kind = 2;
		else if (cs->alphaEnabled && cs->alphaRc.w > 0.0f &&
			Ui_RectContains(cs->alphaRc, x, y)) kind = 3;
		if (!kind) return 0;
		if (cs->drag != kind && cs->focusField) {
			cs_apply_edits_(cs);
			cs->focusField = 0;
			if (cs->ime) UiIme_SetFocus(cs->ime, NULL);
		}
		cs->drag = kind;
		cs_update_drag_(cs, ui, x, y);
		return 1;
	}
	if (msg == UI_COLOR_MOUSE_MOVE) {
		if (!cs->drag) return 0;
		if (!ui->mouseDown) {   /* 丢 Up 兜底 */
			cs->drag = 0;
			cs->layerDirty = 1;
			return 1;
		}
		cs_update_drag_(cs, ui, x, y);
		return 1;
	}
	if (msg == UI_COLOR_MOUSE_UP) {
		if (!cs->drag) return 0;
		cs->drag = 0;
		cs->layerDirty = 1;
		if (cs->dirty) {
			cs->committed = 1;
			cs->dirty = 0;
		}
		/* 松手才刷新主窗: 触发器/预览/输入框 */
		Ui_InvalidateRectPad(ui, cs->anchor, 8.0f);
		return 1;
	}
	return 0;
}

static void cs_focus_clear_(UiColorSelect *cs)
{
	if (!cs->focusField) return;
	cs->focusField = 0;
	cs->editHex.focused = 0;
	cs->editA.focused = 0;
	if (cs->ime) UiIme_SetFocus(cs->ime, NULL);
}

int UiColorSelect_Key(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UINT vk)
{
	UiEdit *e;
	if (!cs || !pm || !ui) return 0;
	if (!UiColorSelect_IsOpen(pm, cs)) return 0;
	if (cs->focusField) {
		if (vk == VK_ESCAPE) {
			cs_focus_clear_(cs);
			UiPopupManager_Close(pm, ui);
			return 1;
		}
		if (vk == VK_RETURN) {
			cs_apply_edits_(cs);
			cs_focus_clear_(cs);
			cs_sync_edits_(cs);
			Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
			return 1;
		}
		e = cs->focusField == 1 ? &cs->editHex : &cs->editA;
		if (Edit_Key(e, ui->hwnd, vk)) {
			cs_apply_edits_(cs);
			Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
		}
		return 1;
	}
	return UiPopupManager_Key(pm, ui, vk);
}

int UiColorSelect_Char(UiColorSelect *cs, UiContext *ui, wchar_t ch)
{
	UiEdit *e;
	if (!cs || !ui || !cs->focusField) return 0;
	if (cs->focusField == 2 && !cs->alphaEnabled) return 1;
	e = cs->focusField == 1 ? &cs->editHex : &cs->editA;
	if (Edit_Char(e, ch)) {
		cs_apply_edits_(cs);
		Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
	}
	return 1;
}

void UiColorSelect_OnIme(UiColorSelect *cs, UiContext *ui)
{
	if (!cs || !ui || !cs->focusField) return;
	cs_apply_edits_(cs);
	Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
}

void UiColorSelect_Blink(UiColorSelect *cs, UiContext *ui)
{
	if (!cs || !ui || !cs->focusField) return;
	cs->blinkOn = !cs->blinkOn;
	Ui_InvalidateRectPad(ui, cs->panelRc, 8.0f);
}
