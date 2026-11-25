#include "ui/ui_ring_progress.h"
#include "ui/ui_widgets.h"
#include <stdint.h>

int snprintf(char *buf, size_t cap, const char *fmt, ...);
void *memset(void *dst, int value, size_t size);

static int ring_finite(double v)
{
	/* NaN 与 +/-Inf 都会让 v-v 变成 NaN. */
	return v == v && v - v == 0.0;
}

static double ring_clamp(double v, double lo, double hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

/* 按完整 UTF-8 序列截断, 避免 DWrite 收到半个汉字. */
static void ring_copy(char *dst, size_t cap, const char *src)
{
	size_t n = 0, len;
	if (!dst || cap == 0) return;
	if (src) {
		while (src[n]) {
			unsigned char c = (unsigned char)src[n];
			len = c < 0x80u ? 1 :
				(c & 0xE0u) == 0xC0u ? 2 :
				(c & 0xF0u) == 0xE0u ? 3 :
				(c & 0xF8u) == 0xF0u ? 4 : 1;
			if (n + len >= cap) break;
			n += len;
		}
	}
	while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) n--;
	dst[n] = 0;
	for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

static double ring_ratio(const UiRingProgress *p)
{
	double d = p->maxValue - p->minValue;
	if (!(d > 0.0)) return 0.0;
	return ring_clamp((p->value - p->minValue) / d, 0.0, 1.0);
}

static uint32_t ring_color(const UiRingProgress *p)
{
	if (p->color != UI_RING_THEME_COLOR) return p->color;
	if (p->ui && p->ui->theme) return p->ui->theme->primary.base;
	return 0xFF4F6FF3u;
}

static void ring_trim_zeros(char *buf)
{
	size_t n = 0;
	while (buf[n]) n++;
	while (n > 0 && buf[n - 1] == '0') buf[--n] = 0;
	if (n > 0 && buf[n - 1] == '.') buf[--n] = 0;
	if (n == 2 && buf[0] == '-' && buf[1] == '0') buf[1] = 0;
}

static void ring_format_value(const UiRingProgress *p, double value,
	char *buf, size_t cap)
{
	const char *s;
	if (!buf || cap == 0) return;
	buf[0] = 0;
	if (p->valueText[0]) {
		ring_copy(buf, cap, p->valueText);
		return;
	}
	if (p->formatter) {
		s = p->formatter(buf, cap, value, p->formatterUser);
		if (s && s[0]) {
			ring_copy(buf, cap, s);
			return;
		}
	}
	snprintf(buf, cap, "%.2f", value);
	ring_trim_zeros(buf);
}

static void ring_request_paint(UiRingProgress *p, UiRect r)
{
	RECT rc;
	float s;
	if (!p || !p->ui || !p->ui->hwnd || r.w <= 0.0f || r.h <= 0.0f) return;
	s = p->ui->scale > 0.0f ? p->ui->scale : 1.0f;
	rc.left = (LONG)(r.x * s) - 1;
	rc.top = (LONG)(r.y * s) - 1;
	rc.right = (LONG)((r.x + r.w) * s) + 1;
	rc.bottom = (LONG)((r.y + r.h) * s) + 1;
	InvalidateRect(p->ui->hwnd, &rc, FALSE);
}

static void ring_destroy(UiRingProgress *p)
{
	if (!p) return;
	if (p->ring) {
		Gfx_RingDestroy(p->ring);
		p->ring = NULL;
	}
	p->overlayRect.x = p->overlayRect.y = 0.0f;
	p->overlayRect.w = p->overlayRect.h = 0.0f;
	p->ringDirty = 0;
}

static void ring_stop_timeline(UiRingProgress *p)
{
	if (!p || !p->ui) return;
	Ui_AnimSet(p->ui, p->id, UI_AN_USER, (float)ring_ratio(p));
	p->cpuAnimating = 0;
	p->ringDirty = 0;
}

static void ring_animate_target(UiRingProgress *p, float target)
{
	if (!p) return;
	p->ringDirty = 1;
	if (!p->ui) return;
	if (p->durationMs > 0) {
		Ui_AnimTo(p->ui, p->id, UI_AN_USER, target,
			p->durationMs, UI_AN_EASE_OUT);
		p->cpuAnimating = 1;
	} else {
		Ui_AnimSet(p->ui, p->id, UI_AN_USER, target);
		p->cpuAnimating = 0;
	}
}

void UiRingProgress_Init(UiRingProgress *p, UiContext *ui)
{
	if (!p) return;
	memset(p, 0, sizeof(*p));
	p->ui = ui;
	p->id = (UiId)(uintptr_t)p;
	if (!p->id) p->id = 1;
	p->minValue = 0.0;
	p->maxValue = 100.0;
	p->value = 0.0;
	p->ringWidth = 14.0f;
	p->color = UI_RING_THEME_COLOR;
	p->roundCaps = 1;
	p->durationMs = Ui_AnimFxMs(UI_AN_PROGRESS);
	p->themeRev = (ui && ui->theme) ? ui->theme->themeRev : 0;
	p->gfxGen = (ui && ui->gfx) ? Gfx_Generation(ui->gfx) : 0;
	if (ui) Ui_AnimSet(ui, p->id, UI_AN_USER, 0.0f);
}

void UiRingProgress_Shutdown(UiRingProgress *p)
{
	if (!p) return;
	ring_destroy(p);
	p->ui = NULL;
}

void UiRingProgress_SetValue(UiRingProgress *p, double value)
{
	if (!p || !ring_finite(value)) return;
	value = ring_clamp(value, p->minValue, p->maxValue);
	if (p->value == value) return;
	p->value = value;
	ring_animate_target(p, (float)ring_ratio(p));
}

void UiRingProgress_SetRange(UiRingProgress *p, double minValue, double maxValue)
{
	double value;
	if (!p || !ring_finite(minValue) || !ring_finite(maxValue) ||
		maxValue <= minValue)
		return;
	value = ring_clamp(p->value, minValue, maxValue);
	p->minValue = minValue;
	p->maxValue = maxValue;
	if (value == p->value)
		ring_animate_target(p, (float)ring_ratio(p));
	else
		UiRingProgress_SetValue(p, value);
}

void UiRingProgress_SetDurationMs(UiRingProgress *p, int durationMs)
{
	if (!p) return;
	if (durationMs < 0) durationMs = 0;
	if (durationMs > 60000) durationMs = 60000;
	p->durationMs = durationMs;
	if (durationMs == 0 && p->cpuAnimating)
		ring_stop_timeline(p);
}

void UiRingProgress_SetUnit(UiRingProgress *p, const char *unit)
{
	if (!p) return;
	ring_copy(p->unit, sizeof(p->unit), unit);
}

void UiRingProgress_SetTitle(UiRingProgress *p, const char *title)
{
	if (!p) return;
	ring_copy(p->title, sizeof(p->title), title);
}

void UiRingProgress_SetValueText(UiRingProgress *p, const char *text)
{
	if (!p) return;
	ring_copy(p->valueText, sizeof(p->valueText), text);
}

void UiRingProgress_SetFormatter(UiRingProgress *p, UiRingProgressFormatFn fn,
	void *user)
{
	if (!p) return;
	p->formatter = fn;
	p->formatterUser = user;
}

void UiRingProgress_SetStyle(UiRingProgress *p, float width, uint32_t color,
	int roundCaps)
{
	if (!p) return;
	if (width < 2.0f) width = 2.0f;
	if (width > 64.0f) width = 64.0f;
	if (p->ringWidth != width || p->color != color ||
		p->roundCaps != (roundCaps ? 1 : 0)) {
		p->ringWidth = width;
		p->color = color;
		p->roundCaps = roundCaps ? 1 : 0;
		p->overlayDirty = 1;
		p->overlayUnavailable = 0;
	}
}

void UiRingProgress_SetHidden(UiRingProgress *p, int hidden)
{
	if (!p) return;
	hidden = hidden ? 1 : 0;
	if (p->hidden == hidden) return;
	p->hidden = hidden;
	if (hidden) {
		ring_stop_timeline(p);
		ring_destroy(p);
	} else {
		p->overlayUnavailable = 0;
	}
}

void UiRingProgress_Draw(UiRingProgress *p, UiRect rc)
{
	UiTheme *t;
	Gfx *g;
	GfxFont *valueFont;
	GfxRect ringRc, titleRc, valueRc, unitRc;
	char text[UI_RING_TEXT_MAX];
	float side, titleH, titleGap, cx, cy, ratio, valueH, unitH, totalH;
	float stroke, inner, opening, valueTop, displayValue;
	int overlayOn;
	GfxStrokeStyle arcStyle;

	if (!p || !p->ui || !p->ui->gfx || !p->ui->theme) return;
	p->drew = 1;
	p->rc = rc;
	Ui_AnimSetRect(p->ui, p->id, rc);
	if (rc.w <= 0.0f || rc.h <= 0.0f) {
		p->ringRect.x = p->ringRect.y = 0.0f;
		p->ringRect.w = p->ringRect.h = 0.0f;
		return;
	}
	t = p->ui->theme;
	g = p->ui->gfx;
	arcStyle.cap = p->roundCaps ? GFX_CAP_ROUND : GFX_CAP_BUTT;
	arcStyle.join = GFX_JOIN_ROUND;
	arcStyle.miterLimit = 4.0f;
	arcStyle.dashes = NULL;
	arcStyle.dashCount = 0;
	titleH = p->title[0] ? Gfx_TextHeight(g, t->fSmall) : 0.0f;
	titleGap = p->title[0] ? 6.0f : 0.0f;
	side = rc.w < rc.h - titleH - titleGap
		? rc.w : rc.h - titleH - titleGap;
	if (side < 0.0f) side = 0.0f;
	ringRc.x = rc.x + (rc.w - side) * 0.5f;
	ringRc.y = rc.y;
	ringRc.w = ringRc.h = side;
	p->ringRect = ringRc;
	titleRc.x = rc.x;
	titleRc.y = ringRc.y + side + titleGap;
	titleRc.w = rc.w;
	titleRc.h = titleH;

	/* 弧只由合成器一层画. 主表面与合成器之间只要发生交接, 就会有一帧
	 * 两侧都在画弧 (union) 或都不在画 (缺口), 这是 Present/Commit 两条
	 * 异步路径无法消除的窗口. 所以补间结束不摘 overlay, 静止态继续由
	 * 合成器持有弧; 只在生命周期变化时同帧重建. 静止帧 ratio 取
	 * Gfx_RingValue, 与文字/合成器同源. */
	overlayOn = p->ring && Gfx_RingVisible(p->ring);
	ratio = overlayOn ? Gfx_RingValue(p->ring) :
		Ui_AnimGet(p->ui, p->id, UI_AN_USER, (float)ring_ratio(p));
	ratio = ring_clamp(ratio, 0.0, 1.0);
	displayValue = (float)(p->minValue + (p->maxValue - p->minValue) * ratio);

	stroke = p->ringWidth;
	if (stroke > side * 0.5f) stroke = side * 0.5f;
	if (side > 0.0f && stroke > 0.0f) {
		float radius = (side - stroke) * 0.5f;
		float arcCx = ringRc.x + side * 0.5f;
		float arcCy = ringRc.y + side * 0.5f;
		Gfx_StrokeEllipse(g, arcCx, arcCy, radius, t->progressTrack, stroke);
		if (!overlayOn && ratio > 0.0f) {
			if (ratio >= 0.99999f)
				Gfx_StrokeEllipse(g, arcCx, arcCy, radius, ring_color(p), stroke);
			else
				Gfx_StrokeArcStyle(g, arcCx, arcCy, radius, -90.0f,
					ratio * 360.0f, ring_color(p), stroke, &arcStyle);
		}
	}

	ring_format_value(p, displayValue, text, sizeof(text));
	opening = side - stroke * 2.0f;
	inner = opening - 10.0f;
	if (inner < opening * 0.5f) inner = opening * 0.5f;
	if (inner < 1.0f) inner = 1.0f;
	if (side >= 150.0f) valueFont = t->fTempBig;
	else if (side >= 100.0f) valueFont = t->fTemp;
	else valueFont = t->fBody;
	valueH = Gfx_TextHeight(g, valueFont);
	unitH = p->unit[0] ? Gfx_TextHeight(g, t->fSmall) : 0.0f;
	totalH = valueH + unitH + (unitH > 0.0f ? 2.0f : 0.0f);
	cx = ringRc.x + side * 0.5f;
	cy = ringRc.y + side * 0.5f;
	valueTop = cy - totalH * 0.5f;
	valueRc.x = ringRc.x + (side - inner) * 0.5f;
	valueRc.y = valueTop;
	valueRc.w = inner;
	valueRc.h = valueH;
	Gfx_Text(g, valueFont, text, valueRc, t->text,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	if (unitH > 0.0f) {
		unitRc = valueRc;
		unitRc.y = valueTop + valueH + 2.0f;
		unitRc.h = unitH;
		Gfx_Text(g, t->fSmall, p->unit, unitRc, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	if (titleH > 0.0f)
		Gfx_Text(g, t->fSmall, p->title, titleRc, t->text2,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

void UiRingProgress_EndFrame(UiRingProgress *p)
{
	UiTheme *t;
	Gfx *g;
	int drew, gen, request = 0;

	if (!p) return;
	if (!p->ui || !p->ui->gfx || !p->ui->theme) {
		p->drew = 0;
		return;
	}
	drew = p->drew;
	p->drew = 0;
	g = p->ui->gfx;
	t = p->ui->theme;
	gen = Gfx_Generation(g);

	if (p->ring && (!Gfx_RingAlive(p->ring) || p->gfxGen != gen)) {
		ring_destroy(p);
		p->ringDirty = p->cpuAnimating;
	}
	if (p->gfxGen != gen) p->overlayUnavailable = 0;
	if (p->ring && p->themeRev != t->themeRev) {
		ring_destroy(p);
		p->ringDirty = p->cpuAnimating;
	}
	p->themeRev = t->themeRev;
	p->gfxGen = gen;

	if (p->overlayDirty) {
		if (p->ring) {
			request = Gfx_RingVisible(p->ring);
			ring_destroy(p);
			p->ringDirty = p->cpuAnimating;
		}
		p->overlayDirty = 0;
	}
	if (p->hidden || !drew || p->ringRect.w <= 0.0f ||
		p->ringRect.h <= 0.0f) {
		if (p->cpuAnimating)
			ring_stop_timeline(p);
		else
			p->ringDirty = 0;
		ring_destroy(p);
		return;
	}
	if (p->cpuAnimating &&
		Ui_AnimGet(p->ui, p->id, UI_AN_USER, (float)ring_ratio(p)) ==
			(float)ring_ratio(p)) {
		p->cpuAnimating = 0;
		/* 收尾补一帧: 合成器弧与文字同源, 这一帧把中心文字落到终值.
		 * 不在这里摘 overlay, 也不改画主表面弧. */
		request = 1;
	}
	if (p->ring && (p->overlayRect.x != p->ringRect.x ||
			p->overlayRect.y != p->ringRect.y ||
			p->overlayRect.w != p->ringRect.w ||
			p->overlayRect.h != p->ringRect.h)) {
		request = Gfx_RingVisible(p->ring);
		ring_destroy(p);
		p->ringDirty = p->cpuAnimating;
	}
	if (request) ring_request_paint(p, p->ringRect);

	/* 用过合成器弧的控件, 生命周期重建 (样式/主题/尺寸/设备) 后立刻
	 * 重新挂上, 不退回主表面画弧 —— 任何一次来回都意味着交接窗口. */
	if (!p->ring && !p->overlayUnavailable &&
		(p->cpuAnimating || p->ringPreferred)) {
		float cur = Ui_AnimGet(p->ui, p->id, UI_AN_USER,
			(float)ring_ratio(p));
		p->ring = Gfx_CreateRing(g, p->ringRect, p->ringWidth,
			ring_color(p), p->roundCaps, cur);
		if (!p->ring) {
			p->overlayUnavailable = 1;
			return;
		}
		p->overlayRect = p->ringRect;
		p->ringPreferred = 1;
		p->ringDirty = 1;
	}
	if (!p->ring) return;
	if (!Gfx_RingVisible(p->ring)) {
		if (!Gfx_RingSetVisible(p->ring, 1)) return;
		/* 视觉已 Commit: 等它进入合成, 下一帧主表面才能停止自绘圆弧,
		 * 否则新主帧可能先于视觉上屏, 圆弧闪掉一帧. 挂上后一路到
		 * 生命周期重建才摘, 补间结束不再来回交接. */
		Gfx_WaitComposited(g);
	}
	if (p->ringDirty) {
		int ms = p->cpuAnimating && p->durationMs > 0 ? p->durationMs : 0;
		Gfx_RingSetValue(p->ring, (float)ring_ratio(p), ms, GFX_EASE_OUT);
		p->ringDirty = 0;
	}
}

int UiRingProgress_Animating(const UiRingProgress *p)
{
	if (!p || p->hidden) return 0;
	if (p->ring && Gfx_RingAnimating(p->ring)) return 1;
	if (!p->cpuAnimating || !p->ui) return 0;
	return Ui_AnimGet(p->ui, p->id, UI_AN_USER, (float)ring_ratio(p)) !=
		(float)ring_ratio(p);
}

int UiRingProgress_OverlayAlive(const UiRingProgress *p)
{
	return p && p->ring && Gfx_RingVisible(p->ring) ? 1 : 0;
}

double UiRingProgress_Value(const UiRingProgress *p)
{
	return p ? p->value : 0.0;
}
