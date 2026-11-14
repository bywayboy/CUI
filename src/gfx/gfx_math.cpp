/*
 * gfx_math.cpp - 数学公式矢量绘制 (可选模块, D2D + DirectWrite)
 *
 * 布局由 md_math 提供; 本文件只实现两件事:
 *   - 度量回调: 每个文本 run 建 IDWriteTextLayout 并缓存 (文本/字号/样式)
 *   - 绘制回调: DrawTextLayout 到 (x, baseline) + 规则线 FillRectangle
 */
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <stdint.h>

#include "gfx/gfx.h"
#include "gfx/gfx_math.h"
#include "gfx/gfx_internal.h"
#include "ui/md_math.h"

/* CreateTextLayout 的约束盒尺寸: GetOverhangMetrics 的 top/right/bottom
 * 相对这个盒的边, 而不是实际行高/行宽. */
#define GM_LAYOUT_BOX 100000.0f

struct GMEntry {
	wchar_t *s;
	int n;
	float size;
	uint32_t flags;
	IDWriteTextFormat *fmt;
	IDWriteTextLayout *lay;
	float w, h, d;          /* 墨迹: w 含右侧溢出, h/d 为基线上下真实墨迹 */
	float lb;               /* 布局基线 (绘制时定位布局顶 = baseline - lb) */
};

struct GfxMath {
	Gfx *g;
	IDWriteFactory *dw;
	GfxFont *textFont;
	MdMath *m;
	GMEntry *e;
	int en, ecap;
	float w, h, d;
};

extern "C" int memcmp(const void *a, const void *b, size_t n);
extern "C" void *memcpy(void *d, const void *s, size_t n);

static void gm_entry_clear(GMEntry *e)
{
	if (e->lay) { e->lay->Release(); e->lay = NULL; }
	if (e->fmt) { e->fmt->Release(); e->fmt = NULL; }
	if (e->s) { HeapFree(GetProcessHeap(), 0, e->s); e->s = NULL; }
	e->n = 0;
}

static int gm_text_family(GfxMath *gm, wchar_t *out, UINT32 cap)
{
	IDWriteTextFormat *tf = gm->textFont ? Gfx_IntFontFormat(gm->textFont) : NULL;
	UINT32 n;
	if (!tf) {
		lstrcpynW(out, L"Cambria Math", (int)cap);
		return 1;
	}
	n = tf->GetFontFamilyNameLength();
	if (n == 0 || n + 1 > cap) {
		lstrcpynW(out, L"Cambria Math", (int)cap);
		return 1;
	}
	if (FAILED(tf->GetFontFamilyName(out, n + 1))) {
		lstrcpynW(out, L"Cambria Math", (int)cap);
		return 1;
	}
	out[n] = 0;
	return 1;
}

/* 查找/创建文本布局; create=0 时仅查找 */
static GMEntry *gm_entry(GfxMath *gm, const wchar_t *s, int n, float size,
	uint32_t flags, int create)
{
	int i;
	GMEntry *e;
	for (i = 0; i < gm->en; i++) {
		GMEntry *q = &gm->e[i];
		if (q->n == n && q->size == size && q->flags == flags &&
			(q->n == 0 || memcmp(q->s, s, (size_t)n * sizeof(wchar_t)) == 0))
			return q;
	}
	if (!create) return NULL;
	if (gm->en >= gm->ecap) {
		int nc = gm->ecap ? gm->ecap * 2 : 16;
		SIZE_T bytes = (SIZE_T)nc * sizeof(GMEntry);
		GMEntry *ne;
		/* HeapReAlloc(NULL) 在目标环境不可靠: 首次必须 HeapAlloc */
		ne = gm->e
			? (GMEntry *)HeapReAlloc(GetProcessHeap(), 0, gm->e, bytes)
			: (GMEntry *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
		if (!ne) return NULL;
		gm->e = ne;
		gm->ecap = nc;
	}
	e = &gm->e[gm->en];
	ZeroMemory(e, sizeof(*e));
	{
		wchar_t fam[64];
		DWRITE_FONT_STYLE style = (flags & MD_MATHF_ITALIC)
			? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
		/* 数学字形用 Cambria Math; \text{} 用应用提供的正文字体 */
		if ((flags & MD_MATHF_TEXT) && gm->textFont)
			gm_text_family(gm, fam, 64);
		else
			lstrcpynW(fam, L"Cambria Math", 64);
		if (FAILED(gm->dw->CreateTextFormat(fam, NULL,
			DWRITE_FONT_WEIGHT_NORMAL, style, DWRITE_FONT_STRETCH_NORMAL,
			size, L"en-us", &e->fmt)) || !e->fmt)
			return NULL;
		e->fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		e->fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
		e->fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	}
	if (FAILED(gm->dw->CreateTextLayout(s, (UINT32)n, e->fmt, 100000.0f,
		100000.0f, &e->lay)) || !e->lay) {
		gm_entry_clear(e);
		return NULL;
	}
	e->s = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)(n > 0 ? n : 1) * sizeof(wchar_t));
	if (!e->s) { gm_entry_clear(e); return NULL; }
	if (n > 0) memcpy(e->s, s, (size_t)n * sizeof(wchar_t));
	e->n = n;
	e->size = size;
	e->flags = flags;
	{
		DWRITE_TEXT_METRICS tm;
		DWRITE_LINE_METRICS lm;
		DWRITE_OVERHANG_METRICS om;
		UINT32 lineCount = 0;
		float layoutH = 0.0f;
		ZeroMemory(&tm, sizeof(tm));
		ZeroMemory(&lm, sizeof(lm));
		ZeroMemory(&om, sizeof(om));
		e->w = 0.0f;
		e->h = 0.0f;
		e->d = 0.0f;
		e->lb = 0.0f;
		if (SUCCEEDED(e->lay->GetMetrics(&tm))) {
			e->w = tm.widthIncludingTrailingWhitespace;
			layoutH = tm.height;
		}
		if (SUCCEEDED(e->lay->GetLineMetrics(&lm, 1, &lineCount)) &&
			lineCount > 0) {
			e->lb = lm.baseline;
			e->h = lm.baseline;
			e->d = lm.height - lm.baseline;
			layoutH = lm.height;
		}
		/* 行高对单字是字体全局值, 定界符/上下标需要真实墨迹:
		 * GetOverhangMetrics 给出墨迹相对布局盒的溢出量 */
		if (layoutH > 0.0f &&
			SUCCEEDED(e->lay->GetOverhangMetrics(&om))) {
			float top = -om.top;                 /* 墨迹顶相对盒顶 */
			float bot = GM_LAYOUT_BOX + om.bottom; /* 墨迹底相对盒顶 */
			float ir = GM_LAYOUT_BOX + om.right;   /* 墨迹右端相对盒左 */
			float ih = e->lb - top;
			float id = bot - e->lb;
			if (ih < 0.0f) ih = 0.0f;
			if (id < 0.0f) id = 0.0f;
			if (ih > 0.01f || id > 0.01f) {
				e->h = ih;
				e->d = id;
			}
			/* 斜体 f 等右侧墨迹超出 advance, 必须计入盒宽 */
			if (ir > e->w) e->w = ir;
		}
		if (e->w < 0.0f) e->w = 0.0f;
		if (e->h < 0.0f) e->h = 0.0f;
		if (e->d < 0.0f) e->d = 0.0f;
	}
	gm->en++;
	return e;
}

static float gm_measure(void *user, const wchar_t *s, int n, float size,
	uint32_t flags, float *height, float *depth)
{
	GfxMath *gm = (GfxMath *)user;
	GMEntry *e = gm_entry(gm, s, n, size, flags, 1);
	if (!e) return -1.0f;
	if (height) *height = e->h;
	if (depth) *depth = e->d;
	return e->w;
}

typedef struct GMDrawCtx {
	GfxMath *gm;
	Gfx *g;
	ID2D1SolidColorBrush *brush;
} GMDrawCtx;

static void gm_draw_text(void *user, const wchar_t *s, int n, float x,
	float baseline, float size, uint32_t flags, float height)
{
	GMDrawCtx *c = (GMDrawCtx *)user;
	GMEntry *e = gm_entry(c->gm, s, n, size, flags, 1);
	(void)height;
	if (!e || !e->lay || !c->gm->dw) return;
	{
		ID2D1RenderTarget *rt = Gfx_IntRT(c->g);
		if (!rt) return;
		rt->DrawTextLayout(D2D1::Point2F(x, baseline - e->lb), e->lay,
			c->brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
	}
}

static void gm_draw_rule(void *user, float x, float baseline, float w,
	float above, float below)
{
	GMDrawCtx *c = (GMDrawCtx *)user;
	ID2D1RenderTarget *rt = Gfx_IntRT(c->g);
	if (!rt) return;
	rt->FillRectangle(D2D1::RectF(x, baseline - above, x + w,
		baseline + below), c->brush);
}

GfxMath *Gfx_MathCreate(Gfx *g, const char *utf8, int len, float sizeDip,
	GfxFont *textFont, int display)
{
	GfxMath *gm;
	if (!g || !utf8 || len <= 0 || sizeDip <= 0.0f) return NULL;
	gm = (GfxMath *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		sizeof(GfxMath));
	if (!gm) return NULL;
	gm->g = g;
	gm->dw = Gfx_IntDWrite(g);
	gm->textFont = textFont;
	if (!gm->dw) { Gfx_MathDestroy(gm); return NULL; }
	gm->m = MdMath_Layout(utf8, len, sizeDip, display, gm_measure, gm);
	if (!gm->m) { Gfx_MathDestroy(gm); return NULL; }
	gm->w = MdMath_Width(gm->m);
	gm->h = MdMath_Height(gm->m);
	gm->d = MdMath_Depth(gm->m);
	return gm;
}

void Gfx_MathDestroy(GfxMath *gm)
{
	int i;
	if (!gm) return;
	for (i = 0; i < gm->en; i++) gm_entry_clear(&gm->e[i]);
	if (gm->e) HeapFree(GetProcessHeap(), 0, gm->e);
	if (gm->m) MdMath_Free(gm->m);
	HeapFree(GetProcessHeap(), 0, gm);
}

float Gfx_MathWidth(const GfxMath *m) { return m ? m->w : 0.0f; }
float Gfx_MathHeight(const GfxMath *m) { return m ? m->h : 0.0f; }
float Gfx_MathDepth(const GfxMath *m) { return m ? m->d : 0.0f; }

void Gfx_MathDraw(GfxMath *gm, Gfx *g, float x, float baseline, GfxColor color)
{
	GMDrawCtx c;
	MdMathDraw d;
	GfxRect box;
	if (!gm || !g || !gm->m || !Gfx_IntReady(g) || !Gfx_IntRT(g)) return;
	box.x = x;
	box.y = baseline - gm->h;
	box.w = gm->w;
	box.h = gm->h + gm->d;
	if (!Gfx_IntVisible(g, box)) return;
	c.gm = gm;
	c.g = g;
	c.brush = Gfx_IntBrush(g, color);
	if (!c.brush) return;
	ZeroMemory(&d, sizeof(d));
	d.user = &c;
	d.text = gm_draw_text;
	d.rule = gm_draw_rule;
	MdMath_Draw(gm->m, x, baseline, &d);
}
