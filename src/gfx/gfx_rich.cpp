/*
 * gfx_rich.cpp - 富文本模块 (DirectWrite 范围样式)
 *
 * 可选模块: 不属于 CORE_OBJS; 只链接进使用它的应用。
 * 依赖方向: gfx_rich -> gfx 核心, 经 gfx_internal.h 访问设备。
 * 0CRT 约束: 无 STL/new/异常/RTTI, 只用 HeapAlloc。
 */
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <stdint.h>
#include <new>

#include "gfx/gfx.h"
#include "gfx/gfx_rich.h"
#include "gfx/gfx_internal.h"

#define RICH_MAX_METRICS 64

/* 行内对象只负责占位/行高; 位图由 Gfx_RichDraw 在布局绘制后贴上去。 */
class RichInline : public IDWriteInlineObject {
public:
	LONG ref;
	float w, h;

	RichInline(float width, float height) : ref(1), w(width), h(height) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **pp) override
	{
		if (!pp) return E_POINTER;
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteInlineObject)) {
			*pp = static_cast<IDWriteInlineObject *>(this);
			AddRef();
			return S_OK;
		}
		*pp = NULL;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return (ULONG)InterlockedIncrement(&ref);
	}
	ULONG STDMETHODCALLTYPE Release() override
	{
		LONG v = InterlockedDecrement(&ref);
		if (v == 0) {
			this->~RichInline();
			HeapFree(GetProcessHeap(), 0, this);
		}
		return (ULONG)v;
	}
	HRESULT STDMETHODCALLTYPE GetMetrics(
		DWRITE_INLINE_OBJECT_METRICS *metrics) override
	{
		if (!metrics) return E_POINTER;
		metrics->width = w;
		metrics->height = h;
		metrics->baseline = h * 0.85f;
		metrics->supportsSideways = FALSE;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetOverhangMetrics(
		DWRITE_OVERHANG_METRICS *overhangs) override
	{
		if (!overhangs) return E_POINTER;
		overhangs->left = overhangs->top = overhangs->right = overhangs->bottom = 0.0f;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetBreakConditions(
		DWRITE_BREAK_CONDITION *before, DWRITE_BREAK_CONDITION *after) override
	{
		if (before) *before = DWRITE_BREAK_CONDITION_NEUTRAL;
		if (after) *after = DWRITE_BREAK_CONDITION_NEUTRAL;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Draw(void *clientDrawingContext,
		IDWriteTextRenderer *renderer, FLOAT originX, FLOAT originY,
		BOOL isSideways, BOOL isRightToLeft,
		IUnknown *clientDrawingEffect) override
	{
		(void)clientDrawingContext; (void)renderer;
		(void)originX; (void)originY; (void)isSideways;
		(void)isRightToLeft; (void)clientDrawingEffect;
		return S_OK;
	}
};

typedef struct RichImgRec {
	int start, len;
	GfxBitmap *bmp;
	float w, h;
} RichImgRec;

struct GfxRichText {
	Gfx *g;
	IDWriteTextLayout *layout;
	int len16;
	int baseWeight;   /* 400 / 700, 供粗体关闭时恢复 */
	RichImgRec *imgs;
	int imgCount, imgCap;
};

static void rich_clamp_range(const GfxRichText *rt, int *start, int *len)
{
	int s = *start;
	int n = *len;
	if (s < 0) { n += s; s = 0; }      /* start 为负: 同步收缩长度 */
	if (s > rt->len16) s = rt->len16;
	if (n < 0) n = 0;
	if (s + n > rt->len16) n = rt->len16 - s;
	*start = s;
	*len = n;
}

static DWRITE_TEXT_RANGE rich_range(int start, int len)
{
	DWRITE_TEXT_RANGE r;
	r.startPosition = (UINT32)start;
	r.length = (UINT32)len;
	return r;
}

GfxRichText *Gfx_RichCreate(Gfx *g, const wchar_t *text16, int len16,
	GfxFont *base, float wrapW, float lineSpacing, uint32_t flags)
{
	IDWriteFactory *dw;
	IDWriteTextFormat *fmt;
	GfxRichText *rt;
	HRESULT hr;

	if (!g || !text16 || len16 <= 0 || !base) return NULL;
	dw = Gfx_IntDWrite(g);
	fmt = Gfx_IntFontFormat(base);
	if (!dw || !fmt) return NULL;
	if (wrapW < 1.0f) wrapW = 1.0f;

	rt = (GfxRichText *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		sizeof(GfxRichText));
	if (!rt) return NULL;
	rt->g = g;
	rt->len16 = len16;
	rt->baseWeight = Gfx_IntFontWeight(base) >= 600 ? 700 : 400;

	hr = dw->CreateTextLayout(text16, (UINT32)len16, fmt, wrapW, 100000.0f,
		&rt->layout);
	if (FAILED(hr) || !rt->layout) {
		HeapFree(GetProcessHeap(), 0, rt);
		return NULL;
	}

	rt->layout->SetWordWrapping((flags & GFX_RICH_WRAP)
		? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
	rt->layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	rt->layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	if (lineSpacing > 0.0f) {
		float size = Gfx_IntFontSize(base);
		float lh = size * lineSpacing;
		if (lh < 1.0f) lh = 1.0f;
		rt->layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
			lh, lh * 0.8f);
	}
	return rt;
}

void Gfx_RichDestroy(GfxRichText *rt)
{
	if (!rt) return;
	if (rt->layout) rt->layout->Release();
	if (rt->imgs) HeapFree(GetProcessHeap(), 0, rt->imgs);
	HeapFree(GetProcessHeap(), 0, rt);
}

void Gfx_RichColor(GfxRichText *rt, int start, int len, GfxColor color)
{
	ID2D1SolidColorBrush *br;
	if (!rt || !rt->layout) return;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0) return;
	br = Gfx_IntBrush(rt->g, color);
	if (!br) return;
	rt->layout->SetDrawingEffect(br, rich_range(start, len));
}

void Gfx_RichBold(GfxRichText *rt, int start, int len, int on)
{
	if (!rt || !rt->layout) return;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0) return;
	rt->layout->SetFontWeight(on ? DWRITE_FONT_WEIGHT_BOLD
		: (DWRITE_FONT_WEIGHT)rt->baseWeight, rich_range(start, len));
}

void Gfx_RichItalic(GfxRichText *rt, int start, int len, int on)
{
	if (!rt || !rt->layout) return;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0) return;
	rt->layout->SetFontStyle(on ? DWRITE_FONT_STYLE_ITALIC
		: DWRITE_FONT_STYLE_NORMAL, rich_range(start, len));
}

int Gfx_RichFontFamily(GfxRichText *rt, int start, int len, GfxFont *font)
{
	IDWriteTextFormat *fmt;
	WCHAR fam[64];
	UINT32 n;
	if (!rt || !rt->layout || !font) return 0;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0) return 0;
	fmt = Gfx_IntFontFormat(font);
	if (!fmt) return 0;
	n = fmt->GetFontFamilyNameLength();
	if (n == 0) return 0;
	if (n >= 63) n = 63;
	if (FAILED(fmt->GetFontFamilyName(fam, n + 1))) return 0;
	fam[n] = 0;
	return SUCCEEDED(rt->layout->SetFontFamilyName(fam,
		rich_range(start, len))) ? 1 : 0;
}

void Gfx_RichUnderline(GfxRichText *rt, int start, int len, int on)
{
	if (!rt || !rt->layout) return;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0) return;
	rt->layout->SetUnderline(on ? TRUE : FALSE, rich_range(start, len));
}

void Gfx_RichStrike(GfxRichText *rt, int start, int len, int on)
{
	if (!rt || !rt->layout) return;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0) return;
	rt->layout->SetStrikethrough(on ? TRUE : FALSE, rich_range(start, len));
}

float Gfx_RichWidth(const GfxRichText *rt)
{
	DWRITE_TEXT_METRICS m;
	if (!rt || !rt->layout) return 0.0f;
	ZeroMemory(&m, sizeof(m));
	if (FAILED(rt->layout->GetMetrics(&m))) return 0.0f;
	return m.widthIncludingTrailingWhitespace;
}

float Gfx_RichHeight(const GfxRichText *rt)
{
	DWRITE_TEXT_METRICS m;
	if (!rt || !rt->layout) return 0.0f;
	ZeroMemory(&m, sizeof(m));
	if (FAILED(rt->layout->GetMetrics(&m))) return 0.0f;
	return m.height;
}

void Gfx_RichDraw(GfxRichText *rt, Gfx *g, float x, float y, GfxColor color)
{
	ID2D1RenderTarget *target;
	ID2D1SolidColorBrush *br;
	GfxRect bounds;
	D2D1_POINT_2F origin;

	if (!rt || !rt->layout || !g) return;
	bounds.x = x;
	bounds.y = y;
	bounds.w = Gfx_RichWidth(rt);
	bounds.h = Gfx_RichHeight(rt);
	if (!Gfx_IntVisible(g, bounds)) return;
	target = Gfx_IntRT(g);
	br = Gfx_IntBrush(g, color);
	if (!target || !br) return;
	origin.x = x;
	origin.y = y;
	target->DrawTextLayout(origin, rt->layout, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);

	/* 行内图片: 布局已完成断行, 按范围实测矩形贴位图 */
	for (int i = 0; i < rt->imgCount; i++) {
		DWRITE_HIT_TEST_METRICS m;
		UINT32 actual = 0;
		UINT32 n = 0;
		ZeroMemory(&m, sizeof(m));
		if (FAILED(rt->layout->HitTestTextRange((UINT32)rt->imgs[i].start,
			(UINT32)rt->imgs[i].len, 0.0f, 0.0f, &m, 1, &actual)))
			continue;
		if (actual == 0) continue;
		(void)n;
		{
			GfxRect dst;
			dst.x = x + m.left;
			dst.y = y + m.top;
			dst.w = m.width;
			dst.h = m.height;
			Gfx_IntDrawBitmap(g, rt->imgs[i].bmp, dst);
		}
	}
}

int Gfx_RichInlineImage(GfxRichText *rt, int start, int len,
	GfxBitmap *bmp, float w, float h)
{
	DWRITE_TEXT_RANGE range;
	void *mem;
	RichInline *obj;
	int i;

	if (!rt || !rt->layout || !bmp || len <= 0) return -1;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0 || w < 1.0f || h < 1.0f) return -2;

	if (rt->imgCount >= rt->imgCap) {
		int nc = rt->imgCap ? rt->imgCap * 2 : 4;
		RichImgRec *np = rt->imgs
			? (RichImgRec *)HeapReAlloc(GetProcessHeap(), 0, rt->imgs,
				(SIZE_T)nc * sizeof(RichImgRec))
			: (RichImgRec *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)nc * sizeof(RichImgRec));
		if (!np) return -5;
		rt->imgs = np;
		rt->imgCap = nc;
	}
	mem = HeapAlloc(GetProcessHeap(), 0, sizeof(RichInline));
	if (!mem) return -3;
	obj = new (mem) RichInline(w, h);
	range.startPosition = (UINT32)start;
	range.length = (UINT32)len;
	if (FAILED(rt->layout->SetInlineObject(obj, range))) {
		obj->Release();
		return -4;
	}
	obj->Release();   /* layout 持有引用 */

	/* 同一范围只保留最新记录 */
	for (i = 0; i < rt->imgCount; i++) {
		if (rt->imgs[i].start == start && rt->imgs[i].len == len) {
			rt->imgs[i].bmp = bmp;
			rt->imgs[i].w = w;
			rt->imgs[i].h = h;
			return 1;
		}
	}
	rt->imgs[rt->imgCount].start = start;
	rt->imgs[rt->imgCount].len = len;
	rt->imgs[rt->imgCount].bmp = bmp;
	rt->imgs[rt->imgCount].w = w;
	rt->imgs[rt->imgCount].h = h;
	rt->imgCount++;
	return 1;
}

int Gfx_RichHitTest(const GfxRichText *rt, float x, float y, int *pos16)
{
	DWRITE_TEXT_METRICS tm;
	DWRITE_HIT_TEST_METRICS hm;
	BOOL trailing = FALSE;
	BOOL inside = FALSE;

	if (!rt || !rt->layout || !pos16) return 0;
	ZeroMemory(&tm, sizeof(tm));
	if (FAILED(rt->layout->GetMetrics(&tm))) return 0;
	if (x < 0.0f || y < 0.0f ||
		x > tm.widthIncludingTrailingWhitespace || y > tm.height)
		return 0;
	ZeroMemory(&hm, sizeof(hm));
	if (FAILED(rt->layout->HitTestPoint(x, y, &trailing, &inside, &hm)))
		return 0;
	*pos16 = (int)hm.textPosition;
	return 1;
}

int Gfx_RichRangeRects(const GfxRichText *rt, int start, int len,
	GfxRect *out, int cap)
{
	DWRITE_HIT_TEST_METRICS m[RICH_MAX_METRICS];
	UINT32 got = 0;
	UINT32 ask;
	UINT32 actual = 0;
	int n, i;

	if (!rt || !rt->layout || !out || cap <= 0) return 0;
	rich_clamp_range(rt, &start, &len);
	if (len <= 0) return 0;
	ask = (UINT32)(cap < RICH_MAX_METRICS ? cap : RICH_MAX_METRICS);
	ZeroMemory(m, sizeof(m));
	if (FAILED(rt->layout->HitTestTextRange((UINT32)start, (UINT32)len,
		0.0f, 0.0f, m, ask, &got)))
		return 0;
	actual = got < ask ? got : ask;
	n = (int)actual;
	for (i = 0; i < n; i++) {
		out[i].x = m[i].left;
		out[i].y = m[i].top;
		out[i].w = m[i].width;
		out[i].h = m[i].height;
	}
	return n;
}
