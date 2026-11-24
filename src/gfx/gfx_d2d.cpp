/*
 * gfx_d2d.cpp - Direct2D + DirectWrite 后端（0CRT）
 *
 * 这是 V2 唯一的 C++ 翻译单元：当前 Windows SDK 的 d2d1.h/dwrite.h 不再提供
 * C 可用的 vtable 定义（C 模式下只剩不完整类型），因此把 COM 调用集中在这里，
 * 对 C 层只暴露 gfx.h 的 extern "C" 接口。
 *
 * 呈现路径:
 *  - 首选 D3D11 + DXGI flip swapchain + DirectComposition（预乘 alpha，
 *    由 DWM 合成，GPU 直接上屏，无 GDI 参与）。
 *  - 窗口必须在 CreateWindowEx 时带 WS_EX_NOREDIRECTIONBITMAP：该样式
 *    只能在创建时指定，运行时 SetWindowLongPtr 增删都无效。
 *  - DComp 初始化/重建失败时回退 ULW：动态追加 WS_EX_LAYERED，绘制到
 *    32bpp top-down DIB，EndDraw 后 UpdateLayeredWindow。
 *
 * 约束:
 *  - 不用 STL / new / 静态局部对象 / 异常 / RTTI；编译器需 /EHs-c- /GR-。
 *  - 文本抗锯齿必须 GRAYSCALE：ClearType 会写不透明像素，破坏透明 alpha。
 */
#define INITGUID
#include <windows.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <d2d1effectauthor.h>
#include <d2d1effecthelpers.h>
#include <dwrite.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <new>
#include <stdint.h>

#include "gfx.h"
#include "gfx/gfx_internal.h"
#include "gfx/gfx_fx.h"
#include "log.h"
#include "grain_cso.h"   /* fxc 生成: g_grainCso / g_grainCsoSize */

extern "C" void *memset(void *d, int c, size_t n);
extern "C" void *memcpy(void *d, const void *s, size_t n);
extern "C" double sqrt(double x);

#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)

struct GfxFont {
	IDWriteTextFormat *fmt;
	float size;
	int weight;
};

struct GfxBitmap {
	ID2D1Bitmap *bmp;
	int w, h;
};

#define GFX_TEXT_CACHE_MAX 128
#define GFX_TEXT_CACHE_CHARS 160
#define GFX_GRAD_CACHE_MAX 24
#define GFX_GRAD_MAX_STOPS 4

typedef struct {
	IDWriteTextLayout *layout;
	GfxFont *font;
	uint64_t hash;
	uint32_t flags;
	int w4, h4;
	uint32_t lastUse;
	char text[GFX_TEXT_CACHE_CHARS];
} GfxTextCache;

typedef struct {
	GfxColor colors[GFX_GRAD_MAX_STOPS];
	float positions[GFX_GRAD_MAX_STOPS];
	int count;
	int horizontal;
	uint32_t lastUse;
	ID2D1LinearGradientBrush *brush;
	ID2D1GradientStopCollection *stops;
} GfxGradientCache;

#define GFX_CORNER_CACHE_MAX 32
typedef struct {
	int wq, hq;          /* 0.25 DIP 量化 */
	int tlq, trq, brq, blq;
	uint32_t lastUse;
	ID2D1PathGeometry *geo;
} GfxCornerCache;

#define GFX_GLOW_CACHE_MAX 24
typedef struct {
	int wq, hq, rq, gq;  /* 0.5 DIP 量化 */
	int scaleq;
	GfxColor rgb;
	uint32_t lastUse;
	GfxBitmap *bmp;
} GfxGlowCache;

#define GFX_STROKE_CACHE_MAX 16
typedef struct {
	int cap, join;
	float miter;
	int dashCount;
	float dashes[8];
	uint32_t lastUse;
	ID2D1StrokeStyle *style;
} GfxStrokeCache;

#define GFX_SVG_GRAD_CACHE_MAX 32
#define GFX_SVG_GRAD_MAX_STOPS 16
typedef struct {
	float bx, by, bw, bh;   /* 元素局部包围盒 (0.5 DIP 量化) */
	float angle;
	int radial;
	float rcx, rcy, rr;     /* 径向: 归一化中心/半径 */
	int count;
	GfxColor colors[GFX_SVG_GRAD_MAX_STOPS];
	float positions[GFX_SVG_GRAD_MAX_STOPS];
	uint32_t lastUse;
	ID2D1Brush *brush;
	ID2D1GradientStopCollection *stops;
} GfxSvgGradCache;

struct GfxFx;
struct GfxFxQ;
struct GfxPart;

struct Gfx {
	HWND hwnd;
	ID2D1Factory1 *d2d;
	IDWriteFactory *dw;
	ID2D1RenderTarget *rt;
	ID2D1DCRenderTarget *dcRt;

	int useDcomp;
	int dcompTried;
	int noVsync;
	int lowLatency;
	ID3D11Device *d3d;
	ID3D11DeviceContext *d3dctx;
	IDXGISwapChain1 *swap;
	ID2D1Device *d2dDevice;
	ID2D1DeviceContext *dc;
	ID2D1Bitmap1 *swapTarget;
	ID2D1Bitmap1 *uiTarget;
	ID3D11Texture2D *uiTex;      /* uiTarget 的底层纹理: 快照从这里取 */
	IDCompositionDevice *compDevice;
	IDCompositionTarget *compTarget;
	IDCompositionVisual *compVisual;
	HDC memDC;
	HBITMAP dib;
	void *bits;
	int pw, ph;
	float scale;
	int generation;
	int deviceGen;
	int inFrame;
	int clipDepth;
	int xformDepth;
	D2D1_MATRIX_3X2_F xformStack[16];
	int dirtyFull;
	int dirtyClip;
	int forceFull;
	GfxRect dirty;
	ID2D1StrokeStyle *roundStroke;
	ID2D1StrokeStyle *buttStroke;
	ID2D1StrokeStyle *dashStroke;
	ID2D1Layer *alphaLayer;
	int alphaDepth;
	IDWriteInlineObject *ellipsis;
	IDWriteTextFormat *ellipsisFmt;

	ID2D1SolidColorBrush *brushes[128];
	GfxColor brushColors[128];
	int brushCount;
	int brushClock;            /* 缓存满时的轮转淘汰指针 */
	int gradCreates;           /* 渐变画刷新建次数 (诊断: 动画期间应接近 0) */

	GfxTextCache textCache[GFX_TEXT_CACHE_MAX];
	uint32_t textClock;

	GfxGradientCache grads[GFX_GRAD_CACHE_MAX];
	uint32_t gradClock;

	GfxCornerCache corners[GFX_CORNER_CACHE_MAX];
	uint32_t cornerClock;

	GfxGlowCache glows[GFX_GLOW_CACHE_MAX];
	uint32_t glowClock;
	int glowGen;

	GfxStrokeCache strokes[GFX_STROKE_CACHE_MAX];
	uint32_t strokeClock;
	GfxSvgGradCache svgGrads[GFX_SVG_GRAD_CACHE_MAX];
	uint32_t svgGradClock;

	GfxOverlay *overlays;
	GfxRing *rings;
	GfxLayer *layers;
	GfxEffect *effects;
	GfxFx *fxList;
	GfxFxQ *fxQueue;
	int fxCount;
	int fxQueueCount;
	int fxBuilt;         /* 本帧新建的过渡数 */
	GfxPart *parts;
	int partFrame;
	ID2D1Image *currentTarget;   /* 当前 Draw 目标 (借用, 不 AddRef) */
	GfxEffect *frameFx[GFX_MAX_FRAME_EFFECTS];
	int frameFxCount;
	int grainFxRegistered;
	int fxDepth;

	/* 帧内 layer 内联绘制: 主表面状态保存 (同一时刻只允许一层) */
	int inlineActive;
	struct {
		GfxLayer *layer;
		ID2D1Image *target;   /* AddRef 持有 */
		ID2D1RenderTarget *rt;
		D2D1_MATRIX_3X2_F xform;
		GfxRect dirty;
		int dirtyFull, forceFull, inFrame;
	} inlineSave;
};

/* 合成器动画 overlay: 独立小 swapchain + 子 visual + 可动画矩形裁剪。
 * 只在创建/改尺寸/设置比例时 Commit, 主窗口不必每帧 Present。 */
struct GfxOverlay {
	Gfx *g;
	IDXGISwapChain1 *swap;
	ID2D1Bitmap1 *target;
	IDCompositionVisual *visual;
	IDCompositionVisual3 *visual3;   /* SetOpacity; 新系统才有 */
	IDCompositionEffectGroup *effect;/* 老系统: effect group 的整体透明度 */
	IDCompositionRectangleClip *clip;
	int pw, ph;
	float x, y, w, h;
	float radius;
	GfxColor c0, c1;
	float reveal;
	float opacity;
	int valid;
	GfxOverlay *next;
};

/* 圆环动画: 一张完整圆环纹理 + 两组半平面相交。
 * 固定半边裁剪与随 group 旋转的半边裁剪相交, 得到从 12/6 点开始扫过的扇区;
 * 圆环纹理旋转对称, 内容无需反向旋转. 端点圆帽随 group 旋转。 */
enum { GFX_RING_HALVES = 2 };
struct GfxRing {
	Gfx *g;
	IDCompositionSurface *surface;
	IDCompositionSurface *capSurface;
	IDCompositionVisual *fixedVis[GFX_RING_HALVES];
	IDCompositionVisual *group[GFX_RING_HALVES];
	IDCompositionVisual *content[GFX_RING_HALVES];
	IDCompositionVisual *capGroup;
	IDCompositionVisual *capVis;
	IDCompositionVisual *startCap;
	IDCompositionRotateTransform *groupRot[GFX_RING_HALVES];
	IDCompositionRotateTransform *capRot;
	IDCompositionRectangleClip *fixedClip[GFX_RING_HALVES];
	IDCompositionRectangleClip *contentClip[GFX_RING_HALVES];
	IDCompositionEffectGroup *capFx;
	IDCompositionEffectGroup *startCapFx;
	int texSize, capSize, radiusPx;
	float capClearP;
	float x, y, w, h, width;
	GfxColor color;
	int roundCaps;
	float fromValue, targetValue, value;
	double startMs;
	int durMs, ease, animating, needRedraw, valid, visible;
	GfxRing *next;
};

/* 通用合成器 layer: 整块内容画在独立 swapchain 上, 由 DComp 子 visual
 * 显示; 内容只重画一次, 透明度交给 DWM 补间。reference=NULL 时
 * AddVisual(FALSE) 才是"置于所有兄弟之上", 因此进度 overlay 在它下面更新。 */
struct GfxLayer {
	Gfx *g;
	IDXGISwapChain1 *swap;
	ID2D1Bitmap1 *target;       /* 每帧从 GetBuffer(0) 临时创建, Present 前释放 */
	IDCompositionVisual *visual;
	IDCompositionVisual3 *visual3;
	IDCompositionEffectGroup *effect;
	int pw, ph;
	float opacity;
	float offsetX;      /* 当前视觉横向位移 (DIP) */
	float offsetY;      /* 当前视觉纵向位移 (DIP) */
	int valid;
	GfxLayer *next;
};

enum {
	GFX_FX_OTHER = 0,
	GFX_FX_COLOR_MATRIX
};

struct GfxEffect {
	Gfx *g;
	ID2D1Effect *fx;
	float pad;          /* 模糊/阴影外扩 (DIP), 离屏内容四周留白 */
	int kind;
	int valid;
	GfxEffect *next;
};

static void LayerRelease(GfxLayer *l);
static ID2D1StrokeStyle *GetPathStroke(Gfx *g, const GfxStrokeStyle *st);
static void RingRelease(GfxRing *r);
static void FxDropAll(Gfx *g);
static void PartsFree(Gfx *g);

/* reference=NULL 时 DComp 的 insertAbove 语义反直觉:
 * FALSE = 放到所有兄弟之上, TRUE = 放到所有兄弟之下.
 * 用命名包装, 避免再把 overlay/layer 的次序写反. */
static HRESULT CompAddTop(IDCompositionVisual *root, IDCompositionVisual *v)
{
	return root->AddVisual(v, FALSE, NULL);
}

static HRESULT CompAddBottom(IDCompositionVisual *root, IDCompositionVisual *v)
{
	return root->AddVisual(v, TRUE, NULL);
}

static D2D1_COLOR_F ToColor(GfxColor c)
{
	D2D1_COLOR_F out;
	out.a = (float)((c >> 24) & 0xFF) / 255.0f;
	out.r = (float)((c >> 16) & 0xFF) / 255.0f;
	out.g = (float)((c >> 8) & 0xFF) / 255.0f;
	out.b = (float)(c & 0xFF) / 255.0f;
	return out;
}

/* 脏区外直接跳过，避免文本排版/渐变/几何创建等 CPU 开销。 */
static int IsVisible(Gfx *g, GfxRect r)
{
	if (!g || r.w <= 0.0f || r.h <= 0.0f) return 0;
	if (g->dirtyFull) return 1;
	float rx = g->dirty.x + g->dirty.w;
	float ry = g->dirty.y + g->dirty.h;
	return r.x < rx && r.x + r.w > g->dirty.x &&
		r.y < ry && r.y + r.h > g->dirty.y;
}

int Gfx_IsVisible(Gfx *g, GfxRect r)
{
	return IsVisible(g, r);
}

static float SinDeg(float d)
{
	while (d >= 360.0f) d -= 360.0f;
	while (d < 0.0f) d += 360.0f;
	if (d > 180.0f) d -= 360.0f;
	if (d > 90.0f) d = 180.0f - d;
	else if (d < -90.0f) d = -180.0f - d;
	const float kPi = 3.14159265358979323846f;
	float x = d * (kPi / 180.0f);
	float x2 = x * x;
	float s = x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f
		+ x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
	return s;
}

static float CosDeg(float d) { return SinDeg(d + 90.0f); }

static int Utf8ToWide(const char *s, WCHAR *out, int cap)
{
	if (!out || cap <= 0) return 0;
	out[0] = 0;
	if (!s || !s[0]) return 0;
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
	if (n <= 0) { out[0] = 0; return 0; }
	out[cap - 1] = 0;
	return n - 1;
}

static void ReleaseGradients(Gfx *g)
{
	if (!g) return;
	for (int i = 0; i < GFX_GRAD_CACHE_MAX; i++) {
		SAFE_RELEASE(g->grads[i].brush);
		SAFE_RELEASE(g->grads[i].stops);
	}
	g->gradClock = 0;
}

/* 路径描边样式与 SVG 渐变画刷都依赖设备/RT, 表面重建时必须一起释放. */
static void ReleasePathCaches(Gfx *g)
{
	if (!g) return;
	for (int i = 0; i < GFX_STROKE_CACHE_MAX; i++)
		SAFE_RELEASE(g->strokes[i].style);
	g->strokeClock = 0;
	for (int i = 0; i < GFX_SVG_GRAD_CACHE_MAX; i++) {
		SAFE_RELEASE(g->svgGrads[i].brush);
		SAFE_RELEASE(g->svgGrads[i].stops);
	}
	g->svgGradClock = 0;
}

static ID2D1SolidColorBrush *GetBrush(Gfx *g, GfxColor c)
{
	int cap = (int)(sizeof(g->brushes) / sizeof(g->brushes[0]));
	int slot;
	D2D1_COLOR_F col;
	ID2D1SolidColorBrush *br = NULL;

	if (!g || !g->rt) return NULL;
	for (int i = 0; i < g->brushCount; i++)
		if (g->brushColors[i] == c) return g->brushes[i];

	/* 动画期间颜色每帧都在变 (hover/press/光晕都是插值色), 缓存必然被占满。
	 * 旧实现满了直接 return NULL: 之后所有"新颜色"静默不画 —— 真机表现就是
	 * 交互几次后某些控件整块消失 (填充/文字是新色, 早缓存的光晕还在)。
	 * 满了就轮转淘汰一个槽。 */
	if (g->brushCount < cap) {
		slot = g->brushCount++;
	} else {
		slot = g->brushClock++ % cap;
		SAFE_RELEASE(g->brushes[slot]);
	}
	col = ToColor(c);
	if (FAILED(g->rt->CreateSolidColorBrush(&col, NULL, &br)) || !br) {
		g->brushes[slot] = NULL;
		g->brushColors[slot] = 0;
		return NULL;
	}
	g->brushes[slot] = br;
	g->brushColors[slot] = c;
	return br;
}

static void ReleaseBrushes(Gfx *g)
{
	for (int i = 0; i < g->brushCount; i++) SAFE_RELEASE(g->brushes[i]);
	g->brushCount = 0;
	g->brushClock = 0;
}

/* ULW 表面: resize 时优先复用同一个 DCRenderTarget, 只重绑新 DIB。
 * D2D 位图资源属于该 RT, 复用 RT 可避免位图失效 (无需重新解码)。 */
static int CreateSurface(Gfx *g)
{
	HDC oldDC;
	HBITMAP oldDib;
	void *oldBits;
	BITMAPINFO bi;
	D2D1_RENDER_TARGET_PROPERTIES props;
	RECT rc;
	HRESULT hr;

	if (!g || !g->d2d || g->pw <= 0 || g->ph <= 0) return 0;
	ReleaseBrushes(g);
	ReleaseGradients(g);
	ReleasePathCaches(g);
	g->rt = NULL;

	oldDC = g->memDC;
	oldDib = g->dib;
	oldBits = g->bits;
	g->memDC = NULL;
	g->dib = NULL;
	g->bits = NULL;

	ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = g->pw;
	bi.bmiHeader.biHeight = -g->ph;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	g->memDC = CreateCompatibleDC(NULL);
	if (!g->memDC) goto restore;
	g->dib = CreateDIBSection(g->memDC, &bi, DIB_RGB_COLORS, &g->bits, NULL, 0);
	if (!g->dib) goto fail_new;
	SelectObject(g->memDC, g->dib);
	rc.left = 0; rc.top = 0; rc.right = g->pw; rc.bottom = g->ph;

	/* 快速路径: 复用已有 DC 渲染目标, 只 rebind */
	if (g->dcRt) {
		hr = g->dcRt->BindDC(g->memDC, &rc);
		if (SUCCEEDED(hr)) {
			if (oldDC) DeleteDC(oldDC);
			if (oldDib) DeleteObject(oldDib);
			g->rt = g->dcRt;
			g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
			g->forceFull = 1;
			g->dirtyFull = 1;
			g->dirtyClip = 0;
			g->generation++;
			Log_Write("[gfx] surface rebind %dx%d scale=%d/1000 gen=%d",
				g->pw, g->ph, (int)(g->scale * 1000.0f), g->generation);
			return 1;
		}
		SAFE_RELEASE(g->dcRt);   /* 失败: 走完整重建, 位图域失效 */
	}
	/* 图层属于旧 RT, 不能挂到新 RT 上; 下次 PushAlpha 会按需重建 */
	SAFE_RELEASE(g->alphaLayer);
	g->alphaDepth = 0;

	ZeroMemory(&props, sizeof(props));
	props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
	props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	props.dpiX = 96.0f;
	props.dpiY = 96.0f;
	props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
	props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
	hr = g->d2d->CreateDCRenderTarget(&props, &g->dcRt);
	if (FAILED(hr) || !g->dcRt) { SAFE_RELEASE(g->dcRt); goto fail_new; }
	hr = g->dcRt->BindDC(g->memDC, &rc);
	if (FAILED(hr)) { SAFE_RELEASE(g->dcRt); goto fail_new; }
	if (oldDC) DeleteDC(oldDC);
	if (oldDib) DeleteObject(oldDib);
	g->rt = g->dcRt;
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->forceFull = 1;
	g->dirtyFull = 1;
	g->dirtyClip = 0;
	g->generation++;
	g->deviceGen++;   /* 新建 DC 渲染目标: 位图域失效 */
	Log_Write("[gfx] surface %dx%d scale=%d/1000 gen=%d", g->pw, g->ph,
		(int)(g->scale * 1000.0f), g->generation);
	return 1;

fail_new:
	if (g->dib) { DeleteObject(g->dib); g->dib = NULL; }
	if (g->memDC) { DeleteDC(g->memDC); g->memDC = NULL; }
	g->bits = NULL;
restore:
	g->memDC = oldDC;
	g->dib = oldDib;
	g->bits = oldBits;
	/* DC 创建失败时旧 dcRt/旧表面原样可用, 必须把 rt 接回去 */
	if (g->dcRt) g->rt = g->dcRt;
	return 0;
}

static int EnvOn(const char *name)
{
	char v[8];
	DWORD n = GetEnvironmentVariableA(name, v, (DWORD)sizeof(v));
	return n > 0 && v[0] != '0';
}

static void SetCompWindowStyle(HWND hwnd, int dcomp)
{
	if (!hwnd) return;
	LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
	/* NOREDIRECTIONBITMAP 只能创建时指定，这里只负责切 LAYERED：
	 * DComp 需要去掉分层样式，ULW 回退需要补上。 */
	if (dcomp)
		ex &= ~WS_EX_LAYERED;
	else
		ex |= WS_EX_LAYERED;
	SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
	SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void ReleaseDcompTargets(Gfx *g, int releaseSwap)
{
	if (!g) return;
	/* DeviceContext 会持有当前 target 的引用；不先解绑，交换链缓冲
	 * 仍然被 D2D 引用，ResizeBuffers 会返回 DXGI_ERROR_INVALID_CALL。 */
	if (g->dc) {
		g->dc->SetTarget(NULL);
		g->dc->Flush();
	}
	if (g->compVisual) g->compVisual->SetContent(NULL);
	SAFE_RELEASE(g->swapTarget);
	SAFE_RELEASE(g->uiTarget);
	SAFE_RELEASE(g->uiTex);
	if (releaseSwap) {
		SAFE_RELEASE(g->swap);
	}
	if (g->rt == (ID2D1RenderTarget *)g->dc) g->rt = NULL;
}

static void ReleaseDcomp(Gfx *g)
{
	if (!g) return;
	SAFE_RELEASE(g->alphaLayer);
	g->alphaDepth = 0;
	ReleaseBrushes(g);
	ReleaseGradients(g);
	ReleasePathCaches(g);
	FxDropAll(g);
	PartsFree(g);
	/* overlay/layer 的 visual/surface 必须在 compDevice/compVisual 之前释放 */
	for (GfxOverlay *o = g->overlays; o; o = o->next) {
		o->valid = 0;
		if (o->visual && g->compVisual) g->compVisual->RemoveVisual(o->visual);
		SAFE_RELEASE(o->clip);
		if (o->visual) o->visual->SetContent(NULL);
		SAFE_RELEASE(o->visual3);
		SAFE_RELEASE(o->effect);
		SAFE_RELEASE(o->visual);
		SAFE_RELEASE(o->target);
		SAFE_RELEASE(o->swap);
	}
	for (GfxRing *r = g->rings; r; r = r->next)
		RingRelease(r);
	for (GfxLayer *l = g->layers; l; l = l->next)
		LayerRelease(l);
	for (GfxEffect *e = g->effects; e; e = e->next) {
		e->valid = 0;
		SAFE_RELEASE(e->fx);
	}
	g->currentTarget = NULL;
	g->frameFxCount = 0;
	ReleaseDcompTargets(g, 1);
	SAFE_RELEASE(g->compVisual);
	SAFE_RELEASE(g->compTarget);
	SAFE_RELEASE(g->compDevice);
	SAFE_RELEASE(g->dc);
	SAFE_RELEASE(g->d2dDevice);
	SAFE_RELEASE(g->d3dctx);
	SAFE_RELEASE(g->d3d);
	g->useDcomp = 0;
}

static int CreateDcompTargets(Gfx *g)
{
	if (!g || !g->swap || !g->dc || g->pw <= 0 || g->ph <= 0) return 0;
	SAFE_RELEASE(g->swapTarget);
	SAFE_RELEASE(g->uiTarget);
	SAFE_RELEASE(g->uiTex);

	IDXGISurface *surface = NULL;
	HRESULT hr = g->swap->GetBuffer(0, IID_PPV_ARGS(&surface));
	if (FAILED(hr) || !surface) return 0;
	D2D1_BITMAP_PROPERTIES1 sp = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0f, 96.0f);
	hr = g->dc->CreateBitmapFromDxgiSurface(surface, &sp, &g->swapTarget);
	SAFE_RELEASE(surface);
	if (FAILED(hr) || !g->swapTarget) return 0;

	/* uiTarget 用自有 D3D 纹理承载: 状态过渡的“旧快照”直接从这块纹理拷贝,
	 * 不需要额外维护一份上一帧副本。 */
	{
		D3D11_TEXTURE2D_DESC td;
		IDXGISurface *us = NULL;
		ZeroMemory(&td, sizeof(td));
		td.Width = (UINT)g->pw;
		td.Height = (UINT)g->ph;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(g->d3d->CreateTexture2D(&td, NULL, &g->uiTex)) || !g->uiTex)
			return 0;
		hr = g->uiTex->QueryInterface(IID_PPV_ARGS(&us));
		if (FAILED(hr) || !us) return 0;
		D2D1_BITMAP_PROPERTIES1 up = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
			96.0f, 96.0f);
		hr = g->dc->CreateBitmapFromDxgiSurface(us, &up, &g->uiTarget);
		us->Release();
		if (FAILED(hr) || !g->uiTarget) return 0;
	}
	return 1;
}

static int InitDcomp(Gfx *g)
{
	if (!g || !g->hwnd || !g->d2d || g->pw <= 0 || g->ph <= 0) return 0;

	D3D_FEATURE_LEVEL levels[3] = {
		D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
	};
	D3D_FEATURE_LEVEL got;
	HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION,
		&g->d3d, &got, &g->d3dctx);
	Log_Write("[gfx] D3D hw=%d fl=%x hr=%x", FAILED(hr) ? 0 : 1, (unsigned)got, (unsigned)hr);
	if (FAILED(hr) || !g->d3d) {
		hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION,
			&g->d3d, &got, &g->d3dctx);
		Log_Write("[gfx] D3D warp hr=%x", (unsigned)hr);
	}
	if (FAILED(hr) || !g->d3d) { ReleaseDcomp(g); return 0; }
	g->deviceGen++;   /* 新设备: 位图/设备资源代际失效 */

	IDXGIDevice *dxgiDev = NULL;
	hr = g->d3d->QueryInterface(IID_PPV_ARGS(&dxgiDev));
	if (FAILED(hr) || !dxgiDev) { ReleaseDcomp(g); return 0; }

	hr = g->d2d->CreateDevice(dxgiDev, &g->d2dDevice);
	if (FAILED(hr) || !g->d2dDevice) {
		dxgiDev->Release();
		ReleaseDcomp(g);
		return 0;
	}
	hr = g->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g->dc);
	if (FAILED(hr) || !g->dc) {
		dxgiDev->Release();
		ReleaseDcomp(g);
		return 0;
	}
	g->dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

	IDXGIFactory2 *xiFactory = NULL;
	hr = CreateDXGIFactory1(IID_PPV_ARGS(&xiFactory));
	if (FAILED(hr) || !xiFactory) {
		dxgiDev->Release();
		ReleaseDcomp(g);
		return 0;
	}
	DXGI_SWAP_CHAIN_DESC1 sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.Width = (UINT32)g->pw;
	sd.Height = (UINT32)g->ph;
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.Stereo = FALSE;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	hr = xiFactory->CreateSwapChainForComposition(g->d3d, &sd, NULL, &g->swap);
	xiFactory->Release();
	if (FAILED(hr) || !g->swap) {
		dxgiDev->Release();
		ReleaseDcomp(g);
		return 0;
	}
	/* composition swap chain 的 Present 是异步的: 只允许排 1 帧,
	 * 否则 hover 会有"隔一帧才跟上鼠标"的迟滞感。 */
	{
		IDXGISwapChain2 *sc2 = NULL;
		if (SUCCEEDED(g->swap->QueryInterface(IID_PPV_ARGS(&sc2))) && sc2) {
			sc2->SetMaximumFrameLatency(1);
			sc2->Release();
		}
	}

	hr = DCompositionCreateDevice(dxgiDev, IID_PPV_ARGS(&g->compDevice));
	dxgiDev->Release();
	if (FAILED(hr) || !g->compDevice) {
		ReleaseDcomp(g);
		return 0;
	}

	SetCompWindowStyle(g->hwnd, 1);
	hr = g->compDevice->CreateTargetForHwnd(g->hwnd, TRUE, &g->compTarget);
	if (FAILED(hr) || !g->compTarget) {
		SetCompWindowStyle(g->hwnd, 0);
		ReleaseDcomp(g);
		return 0;
	}
	hr = g->compDevice->CreateVisual(&g->compVisual);
	if (FAILED(hr) || !g->compVisual) {
		SetCompWindowStyle(g->hwnd, 0);
		ReleaseDcomp(g);
		return 0;
	}
	g->compVisual->SetContent(g->swap);
	g->compTarget->SetRoot(g->compVisual);
	g->compDevice->Commit();

	if (!CreateDcompTargets(g)) {
		SetCompWindowStyle(g->hwnd, 0);
		ReleaseDcomp(g);
		return 0;
	}
	g->rt = (ID2D1RenderTarget *)g->dc;
	g->useDcomp = 1;
	g->forceFull = 1;
	g->dirtyFull = 1;
	Log_Write("[gfx] DComp swapchain %dx%d", g->pw, g->ph);
	return 1;
}

static int ResizeDcomp(Gfx *g)
{
	if (!g || !g->swap || !g->dc) return 0;
	FxDropAll(g);
	ReleaseDcompTargets(g, 0);
	if (g->d3dctx) g->d3dctx->Flush();
	HRESULT hr = g->swap->ResizeBuffers(0, (UINT32)g->pw, (UINT32)g->ph,
		DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) {
		Log_Write("[gfx] DComp ResizeBuffers %dx%d failed 0x%08X",
			g->pw, g->ph, (unsigned)hr);
		return 0;
	}
	if (!CreateDcompTargets(g)) {
		Log_Write("[gfx] DComp recreate targets %dx%d failed", g->pw, g->ph);
		return 0;
	}
	g->compVisual->SetContent(g->swap);
	g->compDevice->Commit();
	g->rt = (ID2D1RenderTarget *)g->dc;
	g->forceFull = 1;
	g->dirtyFull = 1;
	g->generation++;
	return 1;
}

static void FallbackToUlw(Gfx *g)
{
	if (!g) return;
	Log_Write("[gfx] DComp failed, fallback to ULW");
	ReleaseDcomp(g);
	SetCompWindowStyle(g->hwnd, 0);
	g->rt = NULL;
	if (g->pw > 0 && g->ph > 0) CreateSurface(g);
	if (g->hwnd) InvalidateRect(g->hwnd, NULL, FALSE);
}

int Gfx_Init(Gfx **out, HWND hwnd)
{
	if (!out) return 0;
	*out = NULL;
	Gfx *g = (Gfx *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Gfx));
	if (!g) return 0;
	g->hwnd = hwnd;

	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
		__uuidof(ID2D1Factory1), NULL, reinterpret_cast<void **>(&g->d2d));
	if (FAILED(hr)) { Log_Write("[gfx] D2D1CreateFactory failed 0x%08X", (unsigned)hr); HeapFree(GetProcessHeap(), 0, g); return 0; }
	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown **>(&g->dw));
	if (FAILED(hr)) { Log_Write("[gfx] DWriteCreateFactory failed 0x%08X", (unsigned)hr); SAFE_RELEASE(g->d2d); HeapFree(GetProcessHeap(), 0, g); return 0; }

	D2D1_STROKE_STYLE_PROPERTIES sp;
	ZeroMemory(&sp, sizeof(sp));
	sp.startCap = D2D1_CAP_STYLE_ROUND;
	sp.endCap = D2D1_CAP_STYLE_ROUND;
	sp.dashCap = D2D1_CAP_STYLE_ROUND;
	sp.lineJoin = D2D1_LINE_JOIN_ROUND;
	g->d2d->CreateStrokeStyle(&sp, NULL, 0, &g->roundStroke);
	ZeroMemory(&sp, sizeof(sp));
	sp.startCap = D2D1_CAP_STYLE_FLAT;
	sp.endCap = D2D1_CAP_STYLE_FLAT;
	sp.dashCap = D2D1_CAP_STYLE_FLAT;
	sp.lineJoin = D2D1_LINE_JOIN_ROUND;
	g->d2d->CreateStrokeStyle(&sp, NULL, 0, &g->buttStroke);
	ZeroMemory(&sp, sizeof(sp));
	sp.startCap = D2D1_CAP_STYLE_FLAT;
	sp.endCap = D2D1_CAP_STYLE_FLAT;
	sp.dashCap = D2D1_CAP_STYLE_FLAT;
	sp.lineJoin = D2D1_LINE_JOIN_MITER;
	sp.miterLimit = 4.0f;
	sp.dashStyle = D2D1_DASH_STYLE_DASH;
	g->d2d->CreateStrokeStyle(&sp, NULL, 0, &g->dashStroke);

	hr = g->dw->CreateTextFormat(L"Microsoft YaHei", NULL,
		DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
		12.0f, L"zh-CN", &g->ellipsisFmt);
	if (SUCCEEDED(hr) && g->ellipsisFmt) {
		hr = g->dw->CreateEllipsisTrimmingSign(g->ellipsisFmt, &g->ellipsis);
		if (FAILED(hr)) g->ellipsis = NULL;
	}
	g->scale = 1.0f;
	g->dirtyFull = 1;
	g->forceFull = 1;
	g->dcompTried = 0;
	g->useDcomp = 0;
	g->noVsync = EnvOn("SCV2_NOVSYNC");
	Log_Write("[gfx] noVsync=%d", g->noVsync);
	*out = g;
	return 1;
}

void Gfx_Shutdown(Gfx *g)
{
	if (!g) return;
	ReleaseBrushes(g);
	ReleaseGradients(g);
	ReleasePathCaches(g);
	for (int i = 0; i < GFX_TEXT_CACHE_MAX; i++)
		SAFE_RELEASE(g->textCache[i].layout);
	if (g->useDcomp) ReleaseDcomp(g);
	SAFE_RELEASE(g->dcRt);
	g->rt = NULL;
	if (g->dib) { DeleteObject(g->dib); g->dib = NULL; }
	if (g->memDC) { DeleteDC(g->memDC); g->memDC = NULL; }
	SAFE_RELEASE(g->ellipsis);
	SAFE_RELEASE(g->ellipsisFmt);
	SAFE_RELEASE(g->roundStroke);
	SAFE_RELEASE(g->buttStroke);
	SAFE_RELEASE(g->dashStroke);
	SAFE_RELEASE(g->alphaLayer);
	for (int i = 0; i < GFX_CORNER_CACHE_MAX; i++)
		SAFE_RELEASE(g->corners[i].geo);
	for (int i = 0; i < GFX_GLOW_CACHE_MAX; i++) {
		if (g->glows[i].bmp) {
			SAFE_RELEASE(g->glows[i].bmp->bmp);
			HeapFree(GetProcessHeap(), 0, g->glows[i].bmp);
			g->glows[i].bmp = NULL;
		}
	}
	SAFE_RELEASE(g->dw);
	SAFE_RELEASE(g->d2d);
	/* 应用若忘了 Destruction, 这里兜底释放 overlay 结构 (DComp 资源已在
	 * ReleaseDcomp 里释放)。 */
	while (g->overlays) {
		GfxOverlay *o = g->overlays;
		g->overlays = o->next;
		HeapFree(GetProcessHeap(), 0, o);
	}
	while (g->rings) {
		GfxRing *r = g->rings;
		g->rings = r->next;
		HeapFree(GetProcessHeap(), 0, r);
	}
	while (g->layers) {
		GfxLayer *l = g->layers;
		g->layers = l->next;
		HeapFree(GetProcessHeap(), 0, l);
	}
	while (g->effects) {
		GfxEffect *e = g->effects;
		g->effects = e->next;
		SAFE_RELEASE(e->fx);
		HeapFree(GetProcessHeap(), 0, e);
	}
	HeapFree(GetProcessHeap(), 0, g);
}

/* ── 合成器动画 overlay ───────────────────────────────────────────── */

static void OverlayRelease(GfxOverlay *o)
{
	if (!o) return;
	o->valid = 0;
	if (o->visual && o->g && o->g->compVisual)
		o->g->compVisual->RemoveVisual(o->visual);
	SAFE_RELEASE(o->clip);
	if (o->visual) o->visual->SetContent(NULL);
	SAFE_RELEASE(o->visual3);
	SAFE_RELEASE(o->effect);
	SAFE_RELEASE(o->visual);
	SAFE_RELEASE(o->target);
	SAFE_RELEASE(o->swap);
}

/* 画整条渐变到 overlay 自己的 swapchain, 然后 Present 一次。
 * 之后只靠 clip 动画显示左半部分, 不再重新绘制/提交。 */
static int OverlayBuild(GfxOverlay *o)
{
	Gfx *g;
	DXGI_SWAP_CHAIN_DESC1 sd;
	IDXGIFactory2 *fac = NULL;
	IDXGISurface *surf = NULL;
	ID2D1GradientStopCollection *gs = NULL;
	ID2D1LinearGradientBrush *br = NULL;
	D2D1_BITMAP_PROPERTIES1 bp;
	D2D1_GRADIENT_STOP stops[2];
	D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp;
	D2D1_ROUNDED_RECT rr;
	D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
	HRESULT hr;

	if (!o || !o->g) return 0;
	g = o->g;
	if (!g->d3d || !g->dc || !g->compDevice || !g->compVisual) return 0;
	if (o->pw <= 0 || o->ph <= 0) return 0;

	ZeroMemory(&sd, sizeof(sd));
	sd.Width = (UINT32)o->pw;
	sd.Height = (UINT32)o->ph;
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.Stereo = FALSE;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac))) || !fac) return 0;
	hr = fac->CreateSwapChainForComposition(g->d3d, &sd, NULL, &o->swap);
	fac->Release();
	if (FAILED(hr) || !o->swap) return 0;

	hr = o->swap->GetBuffer(0, IID_PPV_ARGS(&surf));
	if (FAILED(hr) || !surf) return 0;
	bp = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0f, 96.0f);
	hr = g->dc->CreateBitmapFromDxgiSurface(surf, &bp, &o->target);
	SAFE_RELEASE(surf);
	if (FAILED(hr) || !o->target) return 0;

	g->dc->SetTarget(o->target);
	g->dc->SetTransform(D2D1::Matrix3x2F::Identity());
	g->dc->BeginDraw();
	g->dc->Clear(&clear);
	stops[0].position = 0.0f;
	stops[0].color = ToColor(o->c0);
	stops[1].position = 1.0f;
	stops[1].color = ToColor(o->c1);
	if (SUCCEEDED(g->dc->CreateGradientStopCollection(stops, 2,
			D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &gs)) && gs) {
		lp.startPoint = D2D1_POINT_2F{ 0.0f, 0.0f };
		lp.endPoint = D2D1_POINT_2F{ (FLOAT)o->pw, 0.0f };
		if (SUCCEEDED(g->dc->CreateLinearGradientBrush(&lp, NULL, gs, &br)) && br) {
			rr.rect.left = 0.0f;
			rr.rect.top = 0.0f;
			rr.rect.right = (FLOAT)o->pw;
			rr.rect.bottom = (FLOAT)o->ph;
			rr.radiusX = rr.radiusY = o->radius * g->scale;
			g->dc->FillRoundedRectangle(&rr, br);
		}
	}
	hr = g->dc->EndDraw();
	g->dc->SetTarget(NULL);
	SAFE_RELEASE(br);
	SAFE_RELEASE(gs);
	if (FAILED(hr)) return 0;
	if (FAILED(o->swap->Present(1, 0))) return 0;

	hr = g->compDevice->CreateVisual(&o->visual);
	if (FAILED(hr) || !o->visual) return 0;
	o->visual->SetContent(o->swap);
	if (SUCCEEDED(o->visual->QueryInterface(IID_PPV_ARGS(&o->visual3))) && o->visual3) {
		o->visual3->SetOpacity(o->opacity);
	} else if (SUCCEEDED(g->compDevice->CreateEffectGroup(&o->effect)) && o->effect) {
		/* Win10 19045 实测没有 IDCompositionVisual3; effect group 从 Win8 就有,
		 * 同样支持 float/animation 透明度. */
		o->effect->SetOpacity(o->opacity);
		o->visual->SetEffect(o->effect);
	}
	/* composition swapchain 的 visual 坐标是物理像素 (与主 swapchain 一致),
	 * 不是 DIP; offset/clip 都要乘 scale。 */
	o->visual->SetOffsetX(o->x * g->scale);
	o->visual->SetOffsetY(o->y * g->scale);

	hr = g->compDevice->CreateRectangleClip(&o->clip);
	if (FAILED(hr) || !o->clip) return 0;
	o->clip->SetLeft(0.0f);
	o->clip->SetTop(0.0f);
	o->clip->SetRight(0.0f);
	o->clip->SetBottom((FLOAT)o->ph);
	o->clip->SetTopLeftRadiusX(o->radius * g->scale);
	o->clip->SetTopLeftRadiusY(o->radius * g->scale);
	o->clip->SetTopRightRadiusX(o->radius * g->scale);
	o->clip->SetTopRightRadiusY(o->radius * g->scale);
	o->clip->SetBottomLeftRadiusX(o->radius * g->scale);
	o->clip->SetBottomLeftRadiusY(o->radius * g->scale);
	o->clip->SetBottomRightRadiusX(o->radius * g->scale);
	o->clip->SetBottomRightRadiusY(o->radius * g->scale);
	/* 重建时直接恢复到当前比例, 避免先闪一帧空条 */
	o->clip->SetRight((float)o->pw * o->reveal);
	o->visual->SetClip(o->clip);
	CompAddBottom(g->compVisual, o->visual);
	g->compDevice->Commit();
	o->valid = 1;
	return 1;
}

GfxOverlay *Gfx_CreateOverlay(Gfx *g, GfxRect rect, float radius,
	GfxColor left, GfxColor right)
{
	GfxOverlay *o;
	if (!g || !g->useDcomp || !g->dc || !g->compDevice || !g->compVisual)
		return NULL;
	if (rect.w <= 0.0f || rect.h <= 0.0f) return NULL;
	o = (GfxOverlay *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxOverlay));
	if (!o) return NULL;
	o->g = g;
	o->x = rect.x;
	o->y = rect.y;
	o->w = rect.w;
	o->h = rect.h;
	o->radius = radius;
	o->c0 = left;
	o->c1 = right;
	o->pw = (int)(rect.w * g->scale + 0.5f);
	o->ph = (int)(rect.h * g->scale + 0.5f);
	o->reveal = 0.0f;
	o->opacity = 1.0f;
	if (!OverlayBuild(o)) {
		OverlayRelease(o);
		HeapFree(GetProcessHeap(), 0, o);
		return NULL;
	}
	o->next = g->overlays;
	g->overlays = o;
	return o;
}

int Gfx_OverlaySetReveal(GfxOverlay *o, float p, int animateMs)
{
	Gfx *g;
	float target;
	if (!o || !o->valid) return 0;
	g = o->g;
	if (!g || !g->compDevice) return 0;
	if (p < 0.0f) p = 0.0f;
	if (p > 1.0f) p = 1.0f;
	target = (float)o->pw * p;
	if (animateMs <= 0) {
		o->clip->SetRight(target);
		o->reveal = p;
	} else {
		IDCompositionAnimation *anim = NULL;
		HRESULT hr = g->compDevice->CreateAnimation(&anim);
		if (SUCCEEDED(hr) && anim) {
			double sec = (double)animateMs / 1000.0;
			float from = (float)o->pw * o->reveal;
			anim->AddCubic(0.0, from, (target - from) / (float)sec,
				0.0f, 0.0f);
			anim->End(sec, target);
			o->clip->SetRight(anim);
			SAFE_RELEASE(anim);
			o->reveal = p;
		} else {
			o->clip->SetRight(target);
			o->reveal = p;
		}
	}
	g->compDevice->Commit();
	return 1;
}

int Gfx_OverlaySetOpacity(GfxOverlay *o, float opacity)
{
	if (!o || !o->valid) return 0;
	if (!o->visual3 && !o->effect) return 0;
	if (opacity < 0.0f) opacity = 0.0f;
	if (opacity > 1.0f) opacity = 1.0f;
	if (o->opacity == opacity) return 1;
	o->opacity = opacity;
	if (o->visual3) o->visual3->SetOpacity(opacity);
	else o->effect->SetOpacity(opacity);
	o->g->compDevice->Commit();
	return 1;
}

int Gfx_OverlayAlive(GfxOverlay *o)
{
	return o && o->valid ? 1 : 0;
}

int Gfx_OverlaySetRect(GfxOverlay *o, GfxRect rect)
{
	int pw, ph;
	if (!o || !o->valid || !o->g || rect.w <= 0.0f || rect.h <= 0.0f) return 0;
	pw = (int)(rect.w * o->g->scale + 0.5f);
	ph = (int)(rect.h * o->g->scale + 0.5f);
	if (pw == o->pw && ph == o->ph &&
		rect.x == o->x && rect.y == o->y &&
		rect.w == o->w && rect.h == o->h)
		return 1;                         /* 未变 */
	OverlayRelease(o);
	o->x = rect.x;
	o->y = rect.y;
	o->w = rect.w;
	o->h = rect.h;
	o->pw = pw;
	o->ph = ph;
	if (!OverlayBuild(o)) return 0;
	o->clip->SetRight((float)o->pw * o->reveal);
	o->g->compDevice->Commit();
	return 2;                             /* 已重建 */
}

void Gfx_OverlayDestroy(GfxOverlay *o)
{
	Gfx *g;
	GfxOverlay **pp;
	if (!o) return;
	g = o->g;
	if (g) {
		for (pp = &g->overlays; *pp; pp = &(*pp)->next) {
			if (*pp == o) {
				*pp = o->next;
				break;
			}
		}
	}
	OverlayRelease(o);
	if (g && g->compDevice) g->compDevice->Commit();
	HeapFree(GetProcessHeap(), 0, o);
}

/* ── 通用合成器 layer ─────────────────────────────────────────────── */

static void LayerRelease(GfxLayer *l)
{
	if (!l) return;
	l->valid = 0;
	if (l->visual && l->g && l->g->compVisual)
		l->g->compVisual->RemoveVisual(l->visual);
	if (l->visual) l->visual->SetContent(NULL);
	SAFE_RELEASE(l->visual3);
	SAFE_RELEASE(l->effect);
	SAFE_RELEASE(l->visual);
	SAFE_RELEASE(l->target);
	SAFE_RELEASE(l->swap);
}

static int LayerBuild(GfxLayer *l)
{
	Gfx *g;
	DXGI_SWAP_CHAIN_DESC1 sd;
	IDXGIFactory2 *fac = NULL;
	HRESULT hr;

	if (!l || !l->g) return 0;
	g = l->g;
	if (!g->d3d || !g->dc || !g->compDevice || !g->compVisual) {
		Log_Write("[layer] no d3d/dc/dev/root %p %p %p %p",
			(void *)g->d3d, (void *)g->dc, (void *)g->compDevice, (void *)g->compVisual);
		return 0;
	}
	if (l->pw <= 0 || l->ph <= 0) return 0;

	ZeroMemory(&sd, sizeof(sd));
	sd.Width = (UINT32)l->pw;
	sd.Height = (UINT32)l->ph;
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.Stereo = FALSE;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac))) || !fac) return 0;
	hr = fac->CreateSwapChainForComposition(g->d3d, &sd, NULL, &l->swap);
	fac->Release();
	if (FAILED(hr) || !l->swap) {
		Log_Write("[layer] swapchain hr=%x %p", (unsigned)hr, (void *)l->swap);
		return 0;
	}
	{
		IDXGISwapChain2 *sc2 = NULL;
		if (SUCCEEDED(l->swap->QueryInterface(IID_PPV_ARGS(&sc2))) && sc2) {
			sc2->SetMaximumFrameLatency(1);
			sc2->Release();
		}
	}

	hr = g->compDevice->CreateVisual(&l->visual);
	if (FAILED(hr) || !l->visual) {
		Log_Write("[layer] visual hr=%x", (unsigned)hr);
		return 0;
	}
	l->visual->SetContent(l->swap);
	hr = l->visual->QueryInterface(IID_PPV_ARGS(&l->visual3));
	if (SUCCEEDED(hr) && l->visual3) {
		l->visual3->SetOpacity(l->opacity);
	} else if (SUCCEEDED(g->compDevice->CreateEffectGroup(&l->effect)) && l->effect) {
		l->effect->SetOpacity(l->opacity);
		l->visual->SetEffect(l->effect);
	} else {
		Log_Write("[layer] no visual3/effect hr=%x", (unsigned)hr);
		return 0;
	}

	/* layer 永远在 overlay 上面, 所以 Toast 卡片能盖住进度条. */
	hr = CompAddTop(g->compVisual, l->visual);
	if (FAILED(hr)) {
		Log_Write("[layer] addvisual hr=%x", (unsigned)hr);
		return 0;
	}
	g->compDevice->Commit();
	l->valid = 1;
	return 1;
}

GfxLayer *Gfx_CreateLayer(Gfx *g, int pixelW, int pixelH)
{
	GfxLayer *l;
	if (!g || !g->useDcomp || !g->dc || !g->compDevice || !g->compVisual)
		return NULL;
	if (pixelW <= 0 || pixelH <= 0) return NULL;
	l = (GfxLayer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxLayer));
	if (!l) return NULL;
	l->g = g;
	l->pw = pixelW;
	l->ph = pixelH;
	l->opacity = 0.0f;          /* 先透明, 应用画完内容再淡入 */
	if (!LayerBuild(l)) {
		LayerRelease(l);
		HeapFree(GetProcessHeap(), 0, l);
		return NULL;
	}
	l->next = g->layers;
	g->layers = l;
	return l;
}

int Gfx_LayerDraw(GfxLayer *l, GfxLayerDrawFn fn, void *user)
{
	Gfx *g;
	IDXGISurface *surf = NULL;
	ID2D1Image *savedTarget = NULL;
	ID2D1RenderTarget *savedRt;
	D2D1_MATRIX_3X2_F savedTransform;
	GfxRect savedDirty;
	int savedInFrame, savedDirtyFull, savedDirtyClip;
	int savedClipDepth, savedAlphaDepth, savedForceFull;
	D2D1_BITMAP_PROPERTIES1 bp;
	D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
	HRESULT hr;

	if (!l || !l->valid || !fn) return 0;
	g = l->g;
	if (!g || !g->dc || !g->compDevice) return 0;
	if (g->inFrame) return 0;   /* 必须在 Gfx_End 之后 */

	hr = l->swap->GetBuffer(0, IID_PPV_ARGS(&surf));
	if (FAILED(hr) || !surf) return 0;
	bp = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0f, 96.0f);
	SAFE_RELEASE(l->target);
	hr = g->dc->CreateBitmapFromDxgiSurface(surf, &bp, &l->target);
	SAFE_RELEASE(surf);
	if (FAILED(hr) || !l->target) return 0;

	savedRt = g->rt;
	savedDirty = g->dirty;
	savedInFrame = g->inFrame;
	savedDirtyFull = g->dirtyFull;
	savedDirtyClip = g->dirtyClip;
	savedClipDepth = g->clipDepth;
	savedAlphaDepth = g->alphaDepth;
	savedForceFull = g->forceFull;
	/* 调用约定: layer 只在 Gfx_End 之后绘制, 此时 DComp 路径的目标是主
	 * swapchain 的后缓冲. 用成员自己 AddRef, 不依赖 GetTarget 的引用语义. */
	savedTarget = g->useDcomp ? g->swapTarget : NULL;
	if (savedTarget) savedTarget->AddRef();
	g->dc->GetTransform(&savedTransform);

	g->dc->SetTarget(l->target);
	g->currentTarget = (ID2D1Image *)l->target;
	g->rt = (ID2D1RenderTarget *)g->dc;
	g->rt->BeginDraw();
	g->rt->Clear(&clear);
	{
		D2D1_MATRIX_3X2_F m = { g->scale, 0.0f, 0.0f, g->scale, 0.0f, 0.0f };
		g->rt->SetTransform(&m);
	}
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->clipDepth = 0;
	g->xformDepth = 0;
	g->alphaDepth = 0;
	g->dirtyClip = 0;
	g->dirtyFull = 1;          /* 回调内所有绘制都可见 */
	g->forceFull = 0;
	g->inFrame = 1;

	fn(g, user);

	while (g->alphaDepth > 0) { g->rt->PopLayer(); g->alphaDepth--; }
	while (g->clipDepth > 0) { g->rt->PopAxisAlignedClip(); g->clipDepth--; }
	if (g->dirtyClip) { g->rt->PopAxisAlignedClip(); g->dirtyClip = 0; }
	hr = g->rt->EndDraw();

	g->dc->SetTarget(savedTarget);
	g->currentTarget = savedTarget;
	if (savedTarget) savedTarget->Release();
	g->dc->SetTransform(&savedTransform);
	g->rt = savedRt;
	g->dirty = savedDirty;
	g->inFrame = savedInFrame;
	g->dirtyFull = savedDirtyFull;
	g->dirtyClip = savedDirtyClip;
	g->clipDepth = savedClipDepth;
	g->alphaDepth = savedAlphaDepth;
	g->forceFull = savedForceFull;

	SAFE_RELEASE(l->target);
	if (FAILED(hr)) {
		l->valid = 0;
		return 0;
	}
	if (FAILED(l->swap->Present(1, 0))) {
		l->valid = 0;
		return 0;
	}
	return 1;
}

/* from 到 to 的三次补间关键帧; ease: 0 线性, 1 cubic-out (减速), 2 cubic-in (加速) */
static void anim_cubic(IDCompositionAnimation *anim, double sec,
	float from, float to, int ease)
{
	float d = to - from;
	float s = (float)sec;
	if (ease == GFX_EASE_OUT) {
		/* p(t)=1-(1-t/d)^3: c1=3D/d, c2=-3D/d^2, c3=D/d^3 */
		anim->AddCubic(0.0, from, 3.0f * d / s,
			-3.0f * d / (s * s), d / (s * s * s));
	} else if (ease == GFX_EASE_IN) {
		/* p(t)=(t/d)^3 */
		anim->AddCubic(0.0, from, 0.0f, 0.0f, d / (s * s * s));
	} else {
		anim->AddCubic(0.0, from, d / s, 0.0f, 0.0f);
	}
	anim->End(sec, to);
}

int Gfx_LayerSetOpacity(GfxLayer *l, float opacity, int animateMs, int ease)
{
	Gfx *g;
	IDCompositionAnimation *anim = NULL;
	HRESULT hr;
	if (!l || !l->valid) return 0;
	if (!l->visual3 && !l->effect) return 0;
	g = l->g;
	if (!g || !g->compDevice) return 0;
	if (opacity < 0.0f) opacity = 0.0f;
	if (opacity > 1.0f) opacity = 1.0f;
	if (animateMs <= 0) {
		l->opacity = opacity;
		if (l->visual3) l->visual3->SetOpacity(opacity);
		else l->effect->SetOpacity(opacity);
		g->compDevice->Commit();
		return 1;
	}
	if (l->opacity == opacity) return 1;
	hr = g->compDevice->CreateAnimation(&anim);
	if (SUCCEEDED(hr) && anim) {
		anim_cubic(anim, (double)animateMs / 1000.0, l->opacity, opacity, ease);
		if (l->visual3) l->visual3->SetOpacity(anim);
		else l->effect->SetOpacity(anim);
		SAFE_RELEASE(anim);
	} else {
		if (l->visual3) l->visual3->SetOpacity(opacity);
		else l->effect->SetOpacity(opacity);
	}
	l->opacity = opacity;
	g->compDevice->Commit();
	return 1;
}

int Gfx_LayerResize(GfxLayer *l, int pixelW, int pixelH)
{
	Gfx *g;
	HRESULT hr;
	if (!l || !l->valid || !l->g) return 0;
	if (pixelW <= 0 || pixelH <= 0) return 0;
	if (pixelW == l->pw && pixelH == l->ph) return 1;
	g = l->g;
	if (g->dc) {
		g->dc->SetTarget(NULL);
		g->dc->Flush();
	}
	SAFE_RELEASE(l->target);
	hr = l->swap->ResizeBuffers(0, (UINT32)pixelW, (UINT32)pixelH,
		DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) {
		Log_Write("[gfx] Layer ResizeBuffers %dx%d failed 0x%08X",
			pixelW, pixelH, (unsigned)hr);
		l->valid = 0;
		return 0;
	}
	l->pw = pixelW;
	l->ph = pixelH;
	l->visual->SetContent(l->swap);
	if (g->compDevice) g->compDevice->Commit();
	return 1;
}

int Gfx_LayerAlive(GfxLayer *l)
{
	return l && l->valid ? 1 : 0;
}

void Gfx_LayerDestroy(GfxLayer *l)
{
	Gfx *g;
	GfxLayer **pp;
	if (!l) return;
	g = l->g;
	if (g) {
		for (pp = &g->layers; *pp; pp = &(*pp)->next) {
			if (*pp == l) {
				*pp = l->next;
				break;
			}
		}
	}
	LayerRelease(l);
	if (g && g->compDevice) g->compDevice->Commit();
	HeapFree(GetProcessHeap(), 0, l);
}

void Gfx_LayerSetPos(GfxLayer *l, float xDip, float yDip)
{
	Gfx *g;
	float s;
	if (!l || !l->valid || !l->visual) return;
	g = l->g;
	if (!g || !g->compDevice) return;
	if (l->offsetX == xDip && l->offsetY == yDip) return;
	s = g->scale > 0.0f ? g->scale : 1.0f;
	l->offsetX = xDip;
	l->offsetY = yDip;
	l->visual->SetOffsetX(xDip * s);
	l->visual->SetOffsetY(yDip * s);
	g->compDevice->Commit();
}

int Gfx_LayerSetOffsetY(GfxLayer *l, float dipY, int animateMs, int ease)
{
	Gfx *g;
	IDCompositionAnimation *anim = NULL;
	float px, fromPx;
	double sec;

	if (!l || !l->valid || !l->visual) return 0;
	g = l->g;
	if (!g || !g->compDevice) return 0;
	px = dipY * (g->scale > 0.0f ? g->scale : 1.0f);
	if (animateMs <= 0) {
		l->offsetY = dipY;
		l->visual->SetOffsetY(px);
		g->compDevice->Commit();
		return 1;
	}
	fromPx = l->offsetY * (g->scale > 0.0f ? g->scale : 1.0f);
	sec = (double)animateMs / 1000.0;
	if (SUCCEEDED(g->compDevice->CreateAnimation(&anim)) && anim) {
		anim_cubic(anim, sec, fromPx, px, ease);
		l->visual->SetOffsetY(anim);
		SAFE_RELEASE(anim);
	} else {
		l->visual->SetOffsetY(px);
	}
	l->offsetY = dipY;
	g->compDevice->Commit();
	return 1;
}

int Gfx_LayerBeginInline(Gfx *g, GfxLayer *l)
{
	IDXGISurface *surf = NULL;
	D2D1_BITMAP_PROPERTIES1 bp;
	D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
	HRESULT hr;

	if (!g || !l || !l->valid || !g->dc || !g->rt) return 0;
	if (!g->inFrame || g->inlineActive) return 0;
	if (g->alphaDepth > 0 || g->clipDepth > 0) return 0;

	/* 结束主表面本帧的绘制, 稍后在 EndInline 里重开 */
	if (g->dirtyClip) { g->rt->PopAxisAlignedClip(); g->dirtyClip = 0; }
	hr = g->rt->EndDraw(NULL, NULL);
	if (FAILED(hr)) {
		Log_Write("[layer] inline EndDraw 0x%08X", (unsigned)hr);
		g->rt->BeginDraw();
		if (!g->dirtyFull) {
			D2D1_RECT_F rc = { g->dirty.x, g->dirty.y,
				g->dirty.x + g->dirty.w, g->dirty.y + g->dirty.h };
			g->rt->PushAxisAlignedClip(&rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			g->dirtyClip = 1;
		}
		return 0;
	}

	g->inlineSave.layer = l;
	g->inlineSave.target = g->currentTarget;
	if (g->inlineSave.target) g->inlineSave.target->AddRef();
	g->inlineSave.rt = g->rt;
	g->dc->GetTransform(&g->inlineSave.xform);
	g->inlineSave.dirty = g->dirty;
	g->inlineSave.dirtyFull = g->dirtyFull;
	g->inlineSave.forceFull = g->forceFull;
	g->inlineSave.inFrame = g->inFrame;

	hr = l->swap->GetBuffer(0, IID_PPV_ARGS(&surf));
	if (FAILED(hr) || !surf) goto fail;
	bp = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0f, 96.0f);
	SAFE_RELEASE(l->target);
	hr = g->dc->CreateBitmapFromDxgiSurface(surf, &bp, &l->target);
	SAFE_RELEASE(surf);
	if (FAILED(hr) || !l->target) goto fail;

	g->dc->SetTarget(l->target);
	g->currentTarget = (ID2D1Image *)l->target;
	g->rt->BeginDraw();
	g->rt->Clear(&clear);
	{
		D2D1_MATRIX_3X2_F m = { g->scale, 0.0f, 0.0f, g->scale, 0.0f, 0.0f };
		g->rt->SetTransform(&m);
	}
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->clipDepth = 0;
	g->xformDepth = 0;
	g->alphaDepth = 0;
	g->dirtyClip = 0;
	g->dirtyFull = 1;
	g->forceFull = 0;
	g->inlineActive = 1;
	return 1;

fail:
	if (g->inlineSave.target) {
		g->inlineSave.target->Release();
		g->inlineSave.target = NULL;
	}
	g->inlineSave.layer = NULL;
	g->rt->BeginDraw();
	if (!g->dirtyFull) {
		D2D1_RECT_F rc = { g->dirty.x, g->dirty.y,
			g->dirty.x + g->dirty.w, g->dirty.y + g->dirty.h };
		g->rt->PushAxisAlignedClip(&rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		g->dirtyClip = 1;
	}
	return 0;
}

int Gfx_LayerEndInline(Gfx *g)
{
	GfxLayer *l;
	HRESULT hr;

	if (!g || !g->inlineActive) return 0;
	l = g->inlineSave.layer;
	while (g->alphaDepth > 0) { g->rt->PopLayer(); g->alphaDepth--; }
	while (g->clipDepth > 0) { g->rt->PopAxisAlignedClip(); g->clipDepth--; }
	if (g->dirtyClip) { g->rt->PopAxisAlignedClip(); g->dirtyClip = 0; }
	hr = g->rt->EndDraw(NULL, NULL);
	SAFE_RELEASE(l->target);

	/* 恢复主表面 */
	if (g->inlineSave.target) {
		g->dc->SetTarget(g->inlineSave.target);
		g->currentTarget = g->inlineSave.target;
		g->inlineSave.target->Release();
		g->inlineSave.target = NULL;
	}
	g->rt = g->inlineSave.rt;
	g->dc->SetTransform(&g->inlineSave.xform);
	g->dirty = g->inlineSave.dirty;
	g->dirtyFull = g->inlineSave.dirtyFull;
	g->forceFull = g->inlineSave.forceFull;
	g->inFrame = g->inlineSave.inFrame;
	g->clipDepth = 0;
	g->xformDepth = 0;
	g->alphaDepth = 0;
	g->dirtyClip = 0;
	g->inlineSave.layer = NULL;
	g->inlineActive = 0;

	if (l) {
		if (FAILED(hr)) l->valid = 0;
		if (!l->valid || FAILED(l->swap->Present(1, 0))) l->valid = 0;
	}
	if (!g->inFrame) return 0;
	g->rt->BeginDraw();
	if (!g->dirtyFull) {
		D2D1_RECT_F rc = { g->dirty.x, g->dirty.y,
			g->dirty.x + g->dirty.w, g->dirty.y + g->dirty.h };
		g->rt->PushAxisAlignedClip(&rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		g->dirtyClip = 1;
	}
	return l && l->valid ? 1 : 0;
}

/* ── SnapshotFx: 状态过渡交给合成器淡出 ──────────────────────────────
 *
 * 覆盖层只放"旧快照": 以不透明度 1 建好后动画到 0, 主表面始终是最新
 * 状态。这样:
 *  - 过渡途中不需要"把当前值烘进旧快照", 也就不会因为按 CPU 时间猜
 *    合成器进度而在快速 hover 时跳变;
 *  - 同区域再次变化时旧覆盖层继续淡出, 主表面的新内容自然接上;
 *  - 覆盖层拆除时它的不透明度已经是 0, 与主帧上屏先后无关。 */

#define GFX_FX_MAX 12

struct GfxFx {
	Gfx *g;
	int px, py, pw, ph;      /* 物理像素 */
	ID3D11Texture2D *texOld;
	ID2D1Bitmap1 *bmpOld;
	IDCompositionSurface *surf;
	IDCompositionVisual *vis;
	IDCompositionEffectGroup *effect;
	double startMs;
	int durMs;
	int started;         /* 已写入合成器动画? */
	int done;            /* 动画已结束, 可以拆除 */
	int replaced;        /* 被新的过渡接管, 等新覆盖层建好后拆 */
	GfxFx *next;
};

struct GfxFxQ {
	int px, py, pw, ph;
	int durMs;
	ID3D11Texture2D *texOld;
	ID2D1Bitmap1 *bmpOld;
	GfxFxQ *next;
};

static double FxNow(void)
{
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	if (f.QuadPart <= 0) return 0.0;
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

/* ── 圆环半平面合成器 ───────────────────────────────────────────── */

#define GFX_RING_PAD 4
#define GFX_RING_ANGLE_EPS 0.05f


static float ring_clamp01(float v)
{
	if (v < 0.0f) return 0.0f;
	if (v > 1.0f) return 1.0f;
	return v;
}

static double ring_cbrt(double x)
{
	double r;
	if (x <= 0.0) return 0.0;
	r = x;
	for (int i = 0; i < 6; i++)
		r = (2.0 * r + x / (r * r)) / 3.0;
	return r;
}

static float ring_ease(float u, int ease)
{
	if (u <= 0.0f) return 0.0f;
	if (u >= 1.0f) return 1.0f;
	if (ease == GFX_EASE_OUT) {
		float v = 1.0f - u;
		return 1.0f - v * v * v;
	}
	if (ease == GFX_EASE_IN) return u * u * u;
	return u;
}

static double ring_inv_ease(double q, int ease)
{
	if (q <= 0.0) return 0.0;
	if (q >= 1.0) return 1.0;
	if (ease == GFX_EASE_OUT) return 1.0 - ring_cbrt(1.0 - q);
	if (ease == GFX_EASE_IN) return ring_cbrt(q);
	return q;
}

/* 第一扇区覆盖 12→6 点, 第二扇区覆盖 6→12 点. 用极小的负角避免在
 * 0%/50% 边界露出一个孤立像素点. */
static float ring_half_angle(int half, float p)
{
	float a = ring_clamp01(p) * 360.0f - 180.0f;
	if (half == 0) {
		if (a <= -179.999f) return -180.0f - GFX_RING_ANGLE_EPS;
		if (a > 0.0f) a = 0.0f;
		return a;
	}
	if (p <= 0.5f) return -GFX_RING_ANGLE_EPS;
	if (a < 0.0f) a = 0.0f;
	if (a > 180.0f) a = 180.0f;
	return a;
}

/* 移动端帽靠近 12 点起止帽时隐藏, 避免半透明颜色叠合成两次 alpha. */
static float ring_cap_opacity(float p, float clear)
{
	return p > clear && p < 1.0f - clear ? 1.0f : 0.0f;
}

static void RingRelease(GfxRing *r)
{
	int i;
	if (!r) return;
	if (r->visible && r->g && r->g->compVisual) {
		for (i = 0; i < GFX_RING_HALVES; i++) {
			if (r->fixedVis[i]) r->g->compVisual->RemoveVisual(r->fixedVis[i]);
		}
		if (r->capGroup) r->g->compVisual->RemoveVisual(r->capGroup);
		if (r->startCap) r->g->compVisual->RemoveVisual(r->startCap);
	}
	r->valid = 0;
	r->visible = 0;
	if (r->capVis) r->capVis->SetContent(NULL);
	if (r->startCap) r->startCap->SetContent(NULL);
	SAFE_RELEASE(r->capFx);
	SAFE_RELEASE(r->startCapFx);
	SAFE_RELEASE(r->capVis);
	SAFE_RELEASE(r->startCap);
	SAFE_RELEASE(r->capRot);
	SAFE_RELEASE(r->capGroup);
	for (i = 0; i < GFX_RING_HALVES; i++) {
		if (r->content[i]) r->content[i]->SetContent(NULL);
		SAFE_RELEASE(r->contentClip[i]);
		SAFE_RELEASE(r->fixedClip[i]);
		SAFE_RELEASE(r->groupRot[i]);
		SAFE_RELEASE(r->content[i]);
		SAFE_RELEASE(r->group[i]);
		SAFE_RELEASE(r->fixedVis[i]);
	}
	SAFE_RELEASE(r->surface);
	SAFE_RELEASE(r->capSurface);
}

typedef struct RingTexture {
	IDCompositionSurface *surface;
	ID2D1Bitmap1 *target;
	int drawing, begun;
	int offsetX, offsetY;
} RingTexture;

static void RingTextureRelease(RingTexture *t)
{
	if (!t) return;
	SAFE_RELEASE(t->target);
	SAFE_RELEASE(t->surface);
	ZeroMemory(t, sizeof(*t));
}

static int RingTextureBegin(Gfx *g, int size, RingTexture *t)
{
	IDXGISurface *surf = NULL;
	POINT offset = { 0, 0 };
	D2D1_BITMAP_PROPERTIES1 bp;
	D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
	HRESULT hr;

	ZeroMemory(t, sizeof(*t));
	if (!g || !g->dc || !g->compDevice || size <= 0) return 0;
	/* 直接创建 composition surface, 用 BeginDraw 取本次更新的 DXGI 面.
	 * 不经过 shared handle/OpenSharedResource, 避免 E_INVALIDARG. */
	hr = g->compDevice->CreateSurface((UINT)size, (UINT)size,
		DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
		&t->surface);
	if (FAILED(hr) || !t->surface) goto fail;
	hr = t->surface->BeginDraw(NULL, __uuidof(IDXGISurface),
		(void **)&surf, &offset);
	if (FAILED(hr) || !surf) goto fail;
	t->begun = 1;
	t->offsetX = offset.x;
	t->offsetY = offset.y;
	bp = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0f, 96.0f);
	hr = g->dc->CreateBitmapFromDxgiSurface(surf, &bp, &t->target);
	SAFE_RELEASE(surf);
	if (FAILED(hr) || !t->target) goto fail;
	g->dc->SetTarget(t->target);
	g->dc->SetTransform(D2D1::Matrix3x2F::Translation(
		(float)offset.x, (float)offset.y));
	g->dc->BeginDraw();
	t->drawing = 1;
	g->dc->Clear(&clear);
	return 1;

fail:
	SAFE_RELEASE(surf);
	if (t->surface && t->begun) t->surface->EndDraw();
	RingTextureRelease(t);
	return 0;
}

static int RingTextureEnd(Gfx *g, RingTexture *t, int ok)
{
	HRESULT hr = E_FAIL, hr2 = E_FAIL;
	if (!t || !t->target || !t->surface) return 0;
	if (t->drawing) {
		hr = g->dc->EndDraw();
		t->drawing = 0;
	}
	g->dc->SetTarget(NULL);
	SAFE_RELEASE(t->target);
	hr2 = t->surface->EndDraw();
	t->begun = 0;
	if (FAILED(hr) || FAILED(hr2)) {
		ok = 0;
	}
	if (!ok) SAFE_RELEASE(t->surface);
	return ok;
}

static int RingDrawFull(Gfx *g, int size, float radius, float widthPx,
	GfxColor color, RingTexture *t)
{
	ID2D1SolidColorBrush *brush = NULL;
	D2D1_ELLIPSE el;
	float c = (float)size * 0.5f;

	if (!RingTextureBegin(g, size, t)) return 0;
	if (FAILED(g->dc->CreateSolidColorBrush(ToColor(color), &brush)) || !brush)
		goto fail;
	el.point = D2D1_POINT_2F{c, c};
	el.radiusX = radius;
	el.radiusY = radius;
	g->dc->DrawEllipse(&el, brush, widthPx, NULL);
	SAFE_RELEASE(brush);
	return RingTextureEnd(g, t, 1);

fail:
	SAFE_RELEASE(brush);
	if (t->drawing) {
		g->dc->EndDraw();
		t->drawing = 0;
	}
	g->dc->SetTarget(NULL);
	RingTextureRelease(t);
	return 0;
}

static int RingDrawCap(Gfx *g, int size, float widthPx, GfxColor color, RingTexture *t)
{
	ID2D1SolidColorBrush *brush = NULL;
	D2D1_ELLIPSE el;
	float c = (float)size * 0.5f;

	if (!RingTextureBegin(g, size, t)) return 0;
	if (FAILED(g->dc->CreateSolidColorBrush(ToColor(color), &brush)) || !brush) {
		if (t->drawing) { g->dc->EndDraw(); t->drawing = 0; }
		g->dc->SetTarget(NULL);
		RingTextureRelease(t);
		return 0;
	}
	el.point = D2D1_POINT_2F{c, c};
	el.radiusX = widthPx * 0.5f;
	el.radiusY = widthPx * 0.5f;
	g->dc->FillEllipse(&el, brush);
	SAFE_RELEASE(brush);
	return RingTextureEnd(g, t, 1);
}

static int RingCreateVisuals(GfxRing *r)
{
	Gfx *g = r->g;
	float scale = g->scale > 0.0f ? g->scale : 1.0f;
	float extent = (float)r->texSize * 0.5f + 2.0f;
	float centerX = (r->x + r->w * 0.5f) * scale;
	float centerY = (r->y + r->h * 0.5f) * scale;
	float texHalf = (float)r->texSize * 0.5f;
	float angle;
	int i;

	for (i = 0; i < GFX_RING_HALVES; i++) {
		angle = ring_half_angle(i, r->value);

		/* 固定半平面: 0 = 右半, 1 = 左半. */
		if (FAILED(g->compDevice->CreateVisual(&r->fixedVis[i])) ||
			!r->fixedVis[i])
			return 0;
		r->fixedVis[i]->SetOffsetX(centerX);
		r->fixedVis[i]->SetOffsetY(centerY);
		if (FAILED(g->compDevice->CreateRectangleClip(&r->fixedClip[i])) ||
			!r->fixedClip[i])
			return 0;
		if (i == 0) {
			r->fixedClip[i]->SetLeft(0.0f);
			r->fixedClip[i]->SetRight(extent);
		} else {
			r->fixedClip[i]->SetLeft(-extent);
			r->fixedClip[i]->SetRight(0.0f);
		}
		r->fixedClip[i]->SetTop(-extent);
		r->fixedClip[i]->SetBottom(extent);
		r->fixedVis[i]->SetClip(r->fixedClip[i]);

		if (FAILED(g->compDevice->CreateVisual(&r->group[i])) || !r->group[i])
			return 0;
		if (FAILED(g->compDevice->CreateRotateTransform(&r->groupRot[i])) ||
			!r->groupRot[i])
			return 0;
		r->groupRot[i]->SetCenterX(0.0f);
		r->groupRot[i]->SetCenterY(0.0f);
		r->groupRot[i]->SetAngle(angle);
		r->group[i]->SetTransform(r->groupRot[i]);
		if (FAILED(r->fixedVis[i]->AddVisual(r->group[i], FALSE, NULL)))
			return 0;

		if (FAILED(g->compDevice->CreateVisual(&r->content[i])) || !r->content[i])
			return 0;
		if (FAILED(r->content[i]->SetContent(r->surface))) return 0;
		r->content[i]->SetOffsetX(-texHalf);
		r->content[i]->SetOffsetY(-texHalf);
		if (FAILED(g->compDevice->CreateRectangleClip(&r->contentClip[i])) ||
			!r->contentClip[i])
			return 0;
		/* 纹理坐标中的右半平面, 随 group 旋转. */
		r->contentClip[i]->SetLeft(texHalf);
		r->contentClip[i]->SetTop(0.0f);
		r->contentClip[i]->SetRight((float)r->texSize);
		r->contentClip[i]->SetBottom((float)r->texSize);
		r->content[i]->SetClip(r->contentClip[i]);
		if (FAILED(r->group[i]->AddVisual(r->content[i], FALSE, NULL)))
			return 0;
	}
	if (r->roundCaps) {
		/* 一个持续旋转的移动端帽 + 一个固定 12 点起始帽. */
		if (FAILED(g->compDevice->CreateVisual(&r->capGroup)) || !r->capGroup)
			return 0;
		r->capGroup->SetOffsetX(centerX);
		r->capGroup->SetOffsetY(centerY);
		if (FAILED(g->compDevice->CreateRotateTransform(&r->capRot)) || !r->capRot)
			return 0;
		r->capRot->SetCenterX(0.0f);
		r->capRot->SetCenterY(0.0f);
		r->capRot->SetAngle(r->value * 360.0f - 180.0f);
		r->capGroup->SetTransform(r->capRot);
		if (FAILED(g->compDevice->CreateVisual(&r->capVis)) || !r->capVis)
			return 0;
		if (FAILED(r->capVis->SetContent(r->capSurface))) return 0;
		r->capVis->SetOffsetX((float)r->capSize * -0.5f);
		r->capVis->SetOffsetY((float)r->radiusPx - (float)r->capSize * 0.5f);
		if (FAILED(g->compDevice->CreateEffectGroup(&r->capFx)) || !r->capFx)
			return 0;
		r->capFx->SetOpacity(ring_cap_opacity(r->value, r->capClearP));
		r->capVis->SetEffect(r->capFx);
		if (FAILED(r->capGroup->AddVisual(r->capVis, FALSE, NULL)))
			return 0;

		if (FAILED(g->compDevice->CreateVisual(&r->startCap)) || !r->startCap)
			return 0;
		if (FAILED(r->startCap->SetContent(r->capSurface))) return 0;
		r->startCap->SetOffsetX(centerX - (float)r->capSize * 0.5f);
		r->startCap->SetOffsetY(centerY - (float)r->radiusPx -
			(float)r->capSize * 0.5f);
		if (FAILED(g->compDevice->CreateEffectGroup(&r->startCapFx)) ||
			!r->startCapFx)
			return 0;
		r->startCapFx->SetOpacity(r->value > 0.0f ? 1.0f : 0.0f);
		r->startCap->SetEffect(r->startCapFx);
	}
	return 1;
}

static int RingBuild(GfxRing *r)
{
	Gfx *g;
	RingTexture t;
	float scale, sideDip, widthPx, radius;
	int sidePx;

	if (!r || !r->g) return 0;
	g = r->g;
	if (!g->useDcomp || !g->d3d || !g->dc || !g->compDevice || !g->compVisual)
		return 0;
	sideDip = r->w < r->h ? r->w : r->h;
	if (sideDip <= 0.0f) return 0;
	scale = g->scale > 0.0f ? g->scale : 1.0f;
	sidePx = (int)(sideDip * scale + 0.5f);
	if (sidePx < 4) return 0;
	widthPx = r->width * scale;
	if (widthPx < 1.0f) widthPx = 1.0f;
	if (widthPx > (float)sidePx * 0.5f) widthPx = (float)sidePx * 0.5f;
	r->texSize = sidePx + GFX_RING_PAD * 2;
	r->radiusPx = (int)((sidePx - widthPx) * 0.5f + 0.5f);
	radius = (float)sidePx * 0.5f - widthPx * 0.5f;

	/* 一张完整圆环纹理; 可见扇区完全由裁剪决定, 不存在半环接缝.
	 * 圆头由独立合成器帽补齐, 半透明颜色不会重叠两次. */
	if (!RingDrawFull(g, r->texSize, radius, widthPx, r->color, &t))
		return 0;
	r->surface = t.surface;

	/* 两个帽圆在圆周上刚好分离所需的最小进度差 (含 10% 余量). */
	{
		float x = radius > 0.5f ? widthPx * 0.5f / radius : 1.0f;
		float ang;
		if (x > 1.0f) x = 1.0f;
		ang = x + x * x * x / 6.0f + 3.0f * x * x * x * x * x / 40.0f;
		r->capClearP = ang * 0.35f;
		if (r->capClearP < 0.001f) r->capClearP = 0.001f;
		if (r->capClearP > 0.05f) r->capClearP = 0.05f;
	}

	if (r->roundCaps) {
		r->capSize = (int)(widthPx + 4.0f + 0.5f);
		if (r->capSize < 4) r->capSize = 4;
		if (!RingDrawCap(g, r->capSize, widthPx, r->color, &t)) return 0;
		r->capSurface = t.surface;
	}
	if (!RingCreateVisuals(r)) return 0;
	g->compDevice->Commit();
	r->valid = 1;
	return 1;
}

GfxRing *Gfx_CreateRing(Gfx *g, GfxRect rect, float width, GfxColor color,
	int roundCaps, float initialValue)
{
	GfxRing *r;
	if (!g || !g->useDcomp || !g->dc || !g->compDevice || !g->compVisual)
		return NULL;
	if (rect.w <= 0.0f || rect.h <= 0.0f) return NULL;
	r = (GfxRing *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxRing));
	if (!r) return NULL;
	r->g = g;
	r->x = rect.x;
	r->y = rect.y;
	r->w = rect.w;
	r->h = rect.h;
	r->width = width > 0.0f ? width : 1.0f;
	r->color = color;
	r->roundCaps = roundCaps ? 1 : 0;
	r->value = r->fromValue = r->targetValue = ring_clamp01(initialValue);
	if (!RingBuild(r)) {
		RingRelease(r);
		HeapFree(GetProcessHeap(), 0, r);
		return NULL;
	}
	r->next = g->rings;
	g->rings = r;
	return r;
}

static float RingCurrent(GfxRing *r)
{
	double u, q;
	if (!r || !r->animating) return r ? r->value : 0.0f;
	u = (FxNow() - r->startMs) / (double)r->durMs;
	if (u >= 1.0) {
		r->value = r->targetValue;
		r->animating = 0;
		return r->value;
	}
	if (u < 0.0) u = 0.0;
	q = ring_ease((float)u, r->ease);
	r->value = r->fromValue + (r->targetValue - r->fromValue) * (float)q;
	return r->value;
}

static void RingApplyStatic(GfxRing *r)
{
	int i;
	for (i = 0; i < GFX_RING_HALVES; i++) {
		float a = ring_half_angle(i, r->value);
		if (r->groupRot[i]) r->groupRot[i]->SetAngle(a);
	}
	if (r->capRot) r->capRot->SetAngle(r->value * 360.0f - 180.0f);
	if (r->capFx) r->capFx->SetOpacity(
		ring_cap_opacity(r->value, r->capClearP));
	if (r->startCapFx) r->startCapFx->SetOpacity(r->value > 0.0f ? 1.0f : 0.0f);
}

/* 角度是进度的分段线性函数, 进度是 cubic-out, 因此每段可精确写成
 * 三次多项式; 在 50% 的饱和折点处切成两段即可, 无需采样. */
static void RingAddAngleSpan(IDCompositionAnimation *anim, double sec,
	float from, float to, double u0, double u1, float constant, int active,
	int ease)
{
	float delta = to - from;
	double len;
	float p, u, f1, f2, f3, d;
	if (u1 <= u0 + 1e-9) return;
	if (!active) {
		anim->AddCubic(sec * u0, constant, 0.0f, 0.0f, 0.0f);
		return;
	}
	u = (float)u0;
	p = from + delta * ring_ease(u, ease);
	if (ease == GFX_EASE_OUT) {
		f1 = 3.0f * (1.0f - u) * (1.0f - u);
		f2 = -6.0f * (1.0f - u);
		f3 = 6.0f;
	} else if (ease == GFX_EASE_IN) {
		f1 = 3.0f * u * u;
		f2 = 6.0f * u;
		f3 = 6.0f;
	} else {
		f1 = 1.0f;
		f2 = f3 = 0.0f;
	}
	len = (double)sec;
	d = 360.0f * delta;
	anim->AddCubic(sec * u0, p * 360.0f - 180.0f,
		d * f1 / (float)len,
		d * f2 * 0.5f / (float)(len * len),
		d * f3 / (6.0f * (float)(len * len * len)));
}

static void RingAnimateAngle(Gfx *g, IDCompositionRotateTransform *rot,
	double sec, float from, float to, int half, int ease)
{
	IDCompositionAnimation *anim = NULL;
	float finalA = ring_half_angle(half, to);
	float startA = ring_half_angle(half, from);
	float constant = half == 0 ? 0.0f : -GFX_RING_ANGLE_EPS;
	int startActive = half == 0 ? from < 0.5f : from > 0.5f;
	int endActive = half == 0 ? to <= 0.5f : to > 0.5f;
	double uc = 0.0;

	if (finalA == startA) {
		rot->SetAngle(finalA);
		return;
	}
	if (sec <= 0.0 || FAILED(g->compDevice->CreateAnimation(&anim)) || !anim) {
		rot->SetAngle(finalA);
		return;
	}
	if (startActive && endActive) {
		RingAddAngleSpan(anim, sec, from, to, 0.0, 1.0, constant, 1, ease);
	} else if (!startActive && !endActive) {
		RingAddAngleSpan(anim, sec, from, to, 0.0, 1.0, constant, 0, ease);
	} else {
		uc = ring_inv_ease(((double)0.5 - (double)from) /
			((double)to - (double)from), ease);
		if (uc <= 0.0) {
			RingAddAngleSpan(anim, sec, from, to, 0.0, 1.0, constant, 1, ease);
		} else if (uc >= 1.0) {
			RingAddAngleSpan(anim, sec, from, to, 0.0, 1.0, constant,
				startActive, ease);
		} else if (startActive) {
			RingAddAngleSpan(anim, sec, from, to, 0.0, uc, constant, 1, ease);
			RingAddAngleSpan(anim, sec, from, to, uc, 1.0, constant, 0, ease);
		} else {
			RingAddAngleSpan(anim, sec, from, to, 0.0, uc, constant, 0, ease);
			RingAddAngleSpan(anim, sec, from, to, uc, 1.0, constant, 1, ease);
		}
	}
	anim->End(sec, finalA);
	rot->SetAngle(anim);
	SAFE_RELEASE(anim);
}

static void RingBindCap(Gfx *g, GfxRing *r, double sec,
	float from, float to, int ease)
{
	IDCompositionEffectGroup *fx = r->capFx;
	float v0, v1;
	double tc[2];
	int nt = 0, i, j;

	if (!fx) return;
	v0 = ring_cap_opacity(from, r->capClearP);
	v1 = ring_cap_opacity(to, r->capClearP);
	if (v0 == v1 || sec <= 0.0) {
		fx->SetOpacity(v1);
		return;
	}
	for (i = 0; i < 2; i++) {
		float t = i == 0 ? r->capClearP : 1.0f - r->capClearP;
		double q = ((double)t - (double)from) /
			((double)to - (double)from);
		if (q <= 0.0 || q >= 1.0) continue;
		tc[nt++] = sec * ring_inv_ease(q, ease);
	}
	if (!nt) {
		fx->SetOpacity(v1);
		return;
	}
	for (i = 1; i < nt; i++) {
		double t = tc[i];
		for (j = i; j > 0 && tc[j - 1] > t; j--) tc[j] = tc[j - 1];
		tc[j] = t;
	}
	{
		IDCompositionAnimation *anim = NULL;
		if (FAILED(g->compDevice->CreateAnimation(&anim)) || !anim) {
			fx->SetOpacity(v1);
			return;
		}
		anim->AddCubic(0.0, v0, 0.0f, 0.0f, 0.0f);
		for (i = 0; i < nt; i++) {
			double u, p;
			float v;
			if (tc[i] <= 0.005 || tc[i] >= sec - 0.005) continue;
			u = tc[i] / sec;
			p = (double)from + ((double)to - (double)from) *
				(double)ring_ease((float)u + 0.00001f, ease);
			v = ring_cap_opacity((float)p, r->capClearP);
			anim->AddCubic(tc[i], v, 0.0f, 0.0f, 0.0f);
		}
		anim->End(sec, v1);
		fx->SetOpacity(anim);
		SAFE_RELEASE(anim);
	}
}

static void RingAnimateCap(Gfx *g, GfxRing *r, double sec,
	float from, float to, int ease)
{
	IDCompositionAnimation *anim = NULL;
	if (!r->capRot) return;
	if (FAILED(g->compDevice->CreateAnimation(&anim)) || !anim) {
		r->capRot->SetAngle(to * 360.0f - 180.0f);
		return;
	}
	anim_cubic(anim, sec, from * 360.0f - 180.0f,
		to * 360.0f - 180.0f, ease);
	r->capRot->SetAngle(anim);
	SAFE_RELEASE(anim);
}

int Gfx_RingSetValue(GfxRing *r, float value, int animateMs, int ease)
{
	Gfx *g;
	float from, target;
	int i;
	double sec;

	if (!r || !r->valid || !r->g || !r->g->compDevice) return 0;
	g = r->g;
	from = RingCurrent(r);
	target = ring_clamp01(value);
	if (animateMs <= 0 || from == target) {
		r->value = r->fromValue = r->targetValue = target;
		r->animating = 0;
		r->needRedraw = 0;
		RingApplyStatic(r);
		g->compDevice->Commit();
		return 1;
	}
	r->fromValue = from;
	r->targetValue = target;
	r->startMs = FxNow();
	r->durMs = animateMs;
	r->ease = ease;
	r->animating = 1;
	r->needRedraw = 1;
	sec = (double)animateMs / 1000.0;
	for (i = 0; i < GFX_RING_HALVES; i++) {
		float a0 = ring_half_angle(i, from);
		float a1 = ring_half_angle(i, target);
		if (a0 == a1) {
			r->groupRot[i]->SetAngle(a1);
		} else {
			RingAnimateAngle(g, r->groupRot[i], sec, from, target,
				i, ease);
		}
	}
	if (r->capRot) RingAnimateCap(g, r, sec, from, target, ease);
	RingBindCap(g, r, sec, from, target, ease);
	if (r->startCapFx)
		r->startCapFx->SetOpacity(from > 0.0f || target > 0.0f ? 1.0f : 0.0f);
	g->compDevice->Commit();
	return 1;
}

int Gfx_RingSetVisible(GfxRing *r, int visible)
{
	Gfx *g;
	int i;
	visible = visible ? 1 : 0;
	if (!r || !r->valid || !r->g) return 0;
	g = r->g;
	if (!g->compVisual || !g->compDevice) return 0;
	if (r->visible == visible) return 1;
	if (visible) {
		for (i = 0; i < GFX_RING_HALVES; i++) {
			if (FAILED(CompAddBottom(g->compVisual, r->fixedVis[i]))) {
				visible = 0;
				break;
			}
		}
		if (visible && r->capGroup &&
			FAILED(CompAddBottom(g->compVisual, r->capGroup)))
			visible = 0;
		if (visible && r->startCap &&
			FAILED(CompAddBottom(g->compVisual, r->startCap)))
			visible = 0;
		if (!visible) {
			for (i = 0; i < GFX_RING_HALVES; i++) {
				if (r->fixedVis[i]) g->compVisual->RemoveVisual(r->fixedVis[i]);
			}
			if (r->capGroup) g->compVisual->RemoveVisual(r->capGroup);
			if (r->startCap) g->compVisual->RemoveVisual(r->startCap);
		}
	} else {
		for (i = 0; i < GFX_RING_HALVES; i++) {
			if (r->fixedVis[i]) g->compVisual->RemoveVisual(r->fixedVis[i]);
		}
		if (r->capGroup) g->compVisual->RemoveVisual(r->capGroup);
		if (r->startCap) g->compVisual->RemoveVisual(r->startCap);
	}
	r->visible = visible;
	g->compDevice->Commit();
	return visible;
}

int Gfx_RingVisible(GfxRing *r)
{
	return r && r->valid && r->visible ? 1 : 0;
}

int Gfx_RingAlive(GfxRing *r)
{
	return r && r->valid ? 1 : 0;
}

int Gfx_RingAnimating(GfxRing *r)
{
	if (!r || !r->valid) return 0;
	RingCurrent(r);
	return r->animating;
}

float Gfx_RingValue(GfxRing *r)
{
	return RingCurrent(r);
}

void Gfx_RingDestroy(GfxRing *r)
{
	Gfx *g;
	GfxRing **pp;
	if (!r) return;
	g = r->g;
	if (g) {
		for (pp = &g->rings; *pp; pp = &(*pp)->next) {
			if (*pp == r) { *pp = r->next; break; }
		}
	}
	RingRelease(r);
	if (g && g->compDevice) g->compDevice->Commit();
	HeapFree(GetProcessHeap(), 0, r);
}

static void RingMarkDone(Gfx *g)
{
	GfxRing *r;
	RECT rc;
	double now;
	if (!g) return;
	now = FxNow();
	for (r = g->rings; r; r = r->next) {
		if (!r->valid || !r->needRedraw) continue;
		RingCurrent(r);
		if (r->animating || now < r->startMs + (double)r->durMs + 16.0)
			continue;
		r->needRedraw = 0;
		if (g->hwnd) {
			rc.left = (LONG)(r->x * g->scale) - 2;
			rc.top = (LONG)(r->y * g->scale) - 2;
			rc.right = (LONG)((r->x + r->w) * g->scale) + 2;
			rc.bottom = (LONG)((r->y + r->h) * g->scale) + 2;
			InvalidateRect(g->hwnd, &rc, FALSE);
		}
	}
}

static ID3D11Texture2D *FxTex(Gfx *g, int w, int h)
{
	D3D11_TEXTURE2D_DESC d;
	ID3D11Texture2D *t = NULL;
	if (!g || !g->d3d || w <= 0 || h <= 0) return NULL;
	ZeroMemory(&d, sizeof(d));
	d.Width = (UINT)w;
	d.Height = (UINT)h;
	d.MipLevels = 1;
	d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(g->d3d->CreateTexture2D(&d, NULL, &t))) return NULL;
	return t;
}

static ID2D1Bitmap1 *FxBmp(Gfx *g, ID3D11Texture2D *tex, int target)
{
	IDXGISurface *s = NULL;
	ID2D1Bitmap1 *b = NULL;
	D2D1_BITMAP_PROPERTIES1 bp;
	if (!g || !g->dc || !tex) return NULL;
	if (FAILED(tex->QueryInterface(IID_PPV_ARGS(&s))) || !s) return NULL;
	bp = D2D1::BitmapProperties1(
		target ? D2D1_BITMAP_OPTIONS_TARGET : D2D1_BITMAP_OPTIONS_NONE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
	g->dc->CreateBitmapFromDxgiSurface(s, &bp, &b);
	s->Release();
	return b;
}

static int FxCaptureFrom(Gfx *g, ID3D11Texture2D *src, int sx, int sy,
	int w, int h, ID3D11Texture2D **outTex, ID2D1Bitmap1 **outBmp)
{
	ID3D11Texture2D *t;
	ID2D1Bitmap1 *b;
	D3D11_BOX box;
	if (!src) return 0;
	t = FxTex(g, w, h);
	if (!t) return 0;
	box.left = (UINT)sx;
	box.top = (UINT)sy;
	box.front = 0;
	box.right = (UINT)(sx + w);
	box.bottom = (UINT)(sy + h);
	box.back = 1;
	g->d3dctx->CopySubresourceRegion(t, 0, 0, 0, 0, src, 0, &box);
	b = FxBmp(g, t, 0);
	if (!b) { t->Release(); return 0; }
	*outTex = t;
	*outBmp = b;
	return 1;
}

/* 旧快照来源: uiTarget 的底层纹理 = 上一帧完整画面 (帧间一直有效) */
static ID3D11Texture2D *FxSourceTex(Gfx *g)
{
	return g ? g->uiTex : NULL;
}

/* 把快照纹理拷进 DComp 表面。设备是 DCompositionCreateDevice(v1), 未与
 * D2D 设备关联, BeginDraw 只能用 IDXGISurface (传 ID2D1DeviceContext 会
 * E_INVALIDARG); 拿到 DXGI 表面后直接做 D3D 纹理拷贝, 全 GPU。 */
static int FxSurfaceDraw(Gfx *g, IDCompositionSurface *s, ID3D11Texture2D *src,
	int w, int h)
{
	IDXGISurface *surf = NULL;
	ID3D11Texture2D *dst = NULL;
	POINT off;
	HRESULT hr;
	(void)w;
	(void)h;
	if (!s || !src) return 0;
	off.x = 0;
	off.y = 0;
	hr = s->BeginDraw(NULL, IID_IDXGISurface, (void **)&surf, &off);
	if (FAILED(hr) || !surf) {
		Log_Write("[fx] surface BeginDraw hr=%08X", (unsigned)hr);
		return 0;
	}
	hr = surf->QueryInterface(IID_PPV_ARGS(&dst));
	if (FAILED(hr) || !dst) {
		Log_Write("[fx] surface QI d3d hr=%08X", (unsigned)hr);
		surf->Release();
		s->EndDraw();
		return 0;
	}
	g->d3dctx->CopySubresourceRegion(dst, 0, (UINT)off.x, (UINT)off.y, 0,
		src, 0, NULL);
	dst->Release();
	surf->Release();
	return SUCCEEDED(s->EndDraw());
}

static void FxRelease(GfxFx *f)
{
	if (!f) return;
	if (f->vis && f->g && f->g->compVisual)
		f->g->compVisual->RemoveVisual(f->vis);
	SAFE_RELEASE(f->effect);
	SAFE_RELEASE(f->vis);
	SAFE_RELEASE(f->surf);
	SAFE_RELEASE(f->bmpOld);
	SAFE_RELEASE(f->texOld);
}

static int FxBuild(Gfx *g, GfxFx *f)
{
	HRESULT hr;
	if (!g || !f) return 0;
	if (!g->compDevice || !g->compVisual) return 0;
	hr = g->compDevice->CreateSurface((UINT)f->pw, (UINT)f->ph,
		DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
		&f->surf);
	if (FAILED(hr) || !f->surf) return 0;
	if (!FxSurfaceDraw(g, f->surf, f->texOld, f->pw, f->ph)) {
		Log_Write("[fx] surface draw failed");
		return 0;
	}
	if (FAILED(g->compDevice->CreateVisual(&f->vis)) || !f->vis) return 0;
	f->vis->SetContent(f->surf);
	f->vis->SetOffsetX((float)f->px);
	f->vis->SetOffsetY((float)f->py);
	if (FAILED(g->compDevice->CreateEffectGroup(&f->effect)) || !f->effect)
		return 0;
	f->effect->SetOpacity(1.0f);
	f->vis->SetEffect(f->effect);
	if (FAILED(CompAddBottom(g->compVisual, f->vis))) return 0;

	/* 先以不透明度 1 提交旧快照覆盖层 (画面 = 旧内容), 再上屏新主帧。
	 * 不论合成器先处理哪一边, 这一块都还是旧画面。 */
	f->startMs = FxNow();
	f->started = 0;
	g->compDevice->Commit();
	return 1;
}

/* 覆盖层 Commit 是异步的: 新主帧上屏前必须保证它已进入合成, 否则那一帧
 * 会先露出新状态再跳回旧覆盖层。等一次合成即可 (Win10 没有
 * DCompositionWaitForCompositorClock, 统一用 DwmFlush)。 */
static void FxWaitComposited(void)
{
	typedef HRESULT (WINAPI *Fn)(void);
	static Fn fn;
	static int tried;
	if (!tried) {
		HMODULE m = LoadLibraryW(L"dwmapi.dll");
		tried = 1;
		if (m) fn = (Fn)GetProcAddress(m, "DwmFlush");
	}
	if (fn) fn();
}

static void FxStartAnim(Gfx *g)
{
	GfxFx *f;
	if (!g || !g->compDevice) return;
	for (f = g->fxList; f; f = f->next) {
		IDCompositionAnimation *anim = NULL;
		double d;
		if (f->started) continue;
		d = (double)f->durMs / 1000.0;
		f->startMs = FxNow();
		f->started = 1;
		if (d > 0.0 &&
			SUCCEEDED(g->compDevice->CreateAnimation(&anim)) && anim) {
			/* 旧快照淡出: opacity(p) = (1-p)^3。新内容可见度 1-(1-p)^3,
			 * 与 CPU 通道 UI_AN_EASE_OUT 完全同曲线; 初速度 -3/d = -1/τ
			 * 也与指数平滑的起点斜率一致 (d = 3τ)。 */
			anim->AddCubic(0.0, 1.0f,
				(float)(-3.0 / d), (float)(3.0 / (d * d)),
				(float)(-1.0 / (d * d * d)));
			anim->End(d, 0.0f);
			f->effect->SetOpacity(anim);
			anim->Release();
		} else {
			f->effect->SetOpacity(0.0f);
		}
	}
	g->compDevice->Commit();
}

/* 动画到时间: 标记 done 并请求一次重绘以回收覆盖层。此时不透明度已是
 * 0, 拆除与主帧上屏的先后不影响画面。 */
static void FxMarkDone(Gfx *g)
{
	GfxFx *f;
	double now;
	RECT r;
	if (!g) return;
	now = FxNow();
	for (f = g->fxList; f; f = f->next) {
		if (f->done) continue;
		if (now >= f->startMs + (double)f->durMs + 16.0) {
			f->done = 1;
			if (g->hwnd) {
				r.left = f->px - 2;
				r.top = f->py - 2;
				r.right = f->px + f->pw + 2;
				r.bottom = f->py + f->ph + 2;
				InvalidateRect(g->hwnd, &r, FALSE);
			}
		}
	}
}

static int FxHasDone(Gfx *g)
{
	GfxFx *f;
	for (f = g ? g->fxList : NULL; f; f = f->next)
		if (f->done) return 1;
	return 0;
}

/* Present 之后调用: 覆盖层已透明, 直接拆除。 */
static void FxRemoveDone(Gfx *g)
{
	GfxFx **pp;
	int removed = 0;
	if (!g) return;
	pp = &g->fxList;
	while (*pp) {
		GfxFx *f = *pp;
		if (!f->done) { pp = &f->next; continue; }
		*pp = f->next;
		FxRelease(f);
		HeapFree(GetProcessHeap(), 0, f);
		g->fxCount--;
		removed = 1;
	}
	if (removed && g->compDevice) g->compDevice->Commit();
}

static void FxFreeQueue(GfxFxQ *q)
{
	while (q) {
		GfxFxQ *n = q->next;
		SAFE_RELEASE(q->bmpOld);
		SAFE_RELEASE(q->texOld);
		HeapFree(GetProcessHeap(), 0, q);
		q = n;
	}
}

/* 抓“旧快照”排队, 由 FxEndFrame 建成淡出覆盖层。
 *
 * 与进行中的过渡重叠时: 旧覆盖层正在淡出, 主表面已经是最新状态, 再叠
 * 一层会把"旧快照"插到画面上 (闪)。所以除非新过渡更长 (代表更大的变化,
 * 如关闭弹层接管按钮 hover), 否则直接丢弃新过渡。 */
int Gfx_FxQueue(Gfx *g, GfxRect rc, int durMs)
{
	GfxFxQ *q;
	GfxFx *f, **pp, *hits[GFX_FX_MAX];
	int nHit = 0, i, supersede = 0;
	int x0, y0, x1, y1;
	int ux0, uy0, ux1, uy1;
	float sc;
	if (!g || !g->useDcomp || !g->compDevice || !g->swap) return 0;
	if (durMs <= 0 || rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	if (!FxSourceTex(g)) return 0;
	sc = g->scale > 0.0f ? g->scale : 1.0f;
	x0 = (int)(rc.x * sc);
	y0 = (int)(rc.y * sc);
	x1 = (int)((rc.x + rc.w) * sc + 0.999f);
	y1 = (int)((rc.y + rc.h) * sc + 0.999f);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > g->pw) x1 = g->pw;
	if (y1 > g->ph) y1 = g->ph;
	if (x1 - x0 < 1 || y1 - y0 < 1) return 0;
	if ((double)(x1 - x0) * (double)(y1 - y0) > 2048.0 * 2048.0) return 0;

	ux0 = x0; uy0 = y0; ux1 = x1; uy1 = y1;
	for (pp = &g->fxList; *pp; pp = &(*pp)->next) {
		f = *pp;
		if (f->px >= x1 || f->py >= y1 ||
			f->px + f->pw <= x0 || f->py + f->ph <= y0) continue;
		if (nHit < GFX_FX_MAX) hits[nHit++] = f;
	}
	if (nHit) {
		for (i = 0; i < nHit; i++)
			if (hits[i]->durMs >= durMs) return 0;
		supersede = 1;
		for (i = 0; i < nHit; i++) {
			if (hits[i]->px < ux0) ux0 = hits[i]->px;
			if (hits[i]->py < uy0) uy0 = hits[i]->py;
			if (hits[i]->px + hits[i]->pw > ux1) ux1 = hits[i]->px + hits[i]->pw;
			if (hits[i]->py + hits[i]->ph > uy1) uy1 = hits[i]->py + hits[i]->ph;
		}
	}

	q = (GfxFxQ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q));
	if (!q) return 0;
	q->px = ux0;
	q->py = uy0;
	q->pw = ux1 - ux0;
	q->ph = uy1 - uy0;
	q->durMs = durMs;

	if (!FxCaptureFrom(g, FxSourceTex(g), ux0, uy0, q->pw, q->ph,
			&q->texOld, &q->bmpOld)) {
		HeapFree(GetProcessHeap(), 0, q);
		return 0;
	}
	/* 被接管的旧层先不拆: 新覆盖层要在 FxEndFrame 里建好后再拆。 */
	if (supersede)
		for (i = 0; i < nHit; i++) hits[i]->replaced = 1;

	if (g->fxQueueCount >= GFX_FX_MAX) {
		/* 队列上限: 丢弃待提交项, 保证不无限增长 (极端连点下最多少一段过渡) */
		FxFreeQueue(g->fxQueue);
		g->fxQueue = NULL;
		g->fxQueueCount = 0;
	}
	q->next = g->fxQueue;
	g->fxQueue = q;
	g->fxQueueCount++;
	return 1;
}

/* 在 Present 绘制之前构建本帧的淡出覆盖层。
 * 旧快照在排队时已经抓自上一帧 uiTarget。 */
static void FxEndFrame(Gfx *g)
{
	int built = 0;
	if (!g || !g->useDcomp || !g->swap || !g->uiTex) return;
	g->fxBuilt = 0;
	while (g->fxQueue) {
		GfxFxQ *q = g->fxQueue;
		GfxFx *f;
		g->fxQueue = q->next;
		g->fxQueueCount--;
		f = (GfxFx *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
		if (!f) {
			SAFE_RELEASE(q->bmpOld);
			SAFE_RELEASE(q->texOld);
			HeapFree(GetProcessHeap(), 0, q);
			continue;
		}
		f->g = g;
		f->px = q->px;
		f->py = q->py;
		f->pw = q->pw;
		f->ph = q->ph;
		f->durMs = q->durMs;
		f->texOld = q->texOld;
		f->bmpOld = q->bmpOld;
		q->texOld = NULL;
		q->bmpOld = NULL;
		HeapFree(GetProcessHeap(), 0, q);
		if (!FxBuild(g, f)) {
			Log_Write("[fx] build failed (%d,%d %dx%d)", f->px, f->py,
				f->pw, f->ph);
			FxRelease(f);
			HeapFree(GetProcessHeap(), 0, f);
			continue;
		}
		f->next = g->fxList;
		g->fxList = f;
		g->fxCount++;
		built++;
	}
	g->fxBuilt = built;
	if (built > 0) {
		/* 新覆盖层已提交: 拆被接管的旧层 */
		GfxFx **pp = &g->fxList;
		while (*pp) {
			GfxFx *f = *pp;
			if (f->replaced) {
				*pp = f->next;
				FxRelease(f);
				HeapFree(GetProcessHeap(), 0, f);
				g->fxCount--;
				continue;
			}
			pp = &f->next;
		}
		if (g->compDevice) g->compDevice->Commit();
	} else {
		GfxFx *f;
		for (f = g->fxList; f; f = f->next) f->replaced = 0;
	}
}

static void FxDropAll(Gfx *g)
{
	GfxFx *f;
	if (!g) return;
	FxFreeQueue(g->fxQueue);
	g->fxQueue = NULL;
	g->fxQueueCount = 0;
	f = g->fxList;
	g->fxList = NULL;
	g->fxCount = 0;
	while (f) {
		GfxFx *n = f->next;
		FxRelease(f);
		HeapFree(GetProcessHeap(), 0, f);
		f = n;
	}
	if (g->compDevice) g->compDevice->Commit();
}

int Gfx_FxEnabled(Gfx *g)
{
	return g && g->useDcomp && g->compDevice && g->swap ? 1 : 0;
}

void Gfx_FxCancelAll(Gfx *g)
{
	if (!g) return;
	FxDropAll(g);
}

void Gfx_FxCancelRect(Gfx *g, GfxRect rc)
{
	GfxFx **pp;
	GfxFxQ **qp;
	int x0, y0, x1, y1, removed = 0;
	float sc;
	if (!g) return;
	if (rc.w <= 0.0f || rc.h <= 0.0f) { return; }
	sc = g->scale > 0.0f ? g->scale : 1.0f;
	x0 = (int)(rc.x * sc);
	y0 = (int)(rc.y * sc);
	x1 = (int)((rc.x + rc.w) * sc + 0.999f);
	y1 = (int)((rc.y + rc.h) * sc + 0.999f);
	pp = &g->fxList;
	while (*pp) {
		GfxFx *f = *pp;
		if (f->px >= x1 || f->py >= y1 ||
			f->px + f->pw <= x0 || f->py + f->ph <= y0) {
			pp = &f->next;
			continue;
		}
		*pp = f->next;
		FxRelease(f);
		HeapFree(GetProcessHeap(), 0, f);
		g->fxCount--;
		removed = 1;
	}
	qp = &g->fxQueue;
	while (*qp) {
		GfxFxQ *q = *qp;
		if (q->px >= x1 || q->py >= y1 ||
			q->px + q->pw <= x0 || q->py + q->ph <= y0) {
			qp = &q->next;
			continue;
		}
		*qp = q->next;
		SAFE_RELEASE(q->bmpOld);
		SAFE_RELEASE(q->texOld);
		HeapFree(GetProcessHeap(), 0, q);
		g->fxQueueCount--;
		removed = 1;
	}
	if (removed && g->compDevice) g->compDevice->Commit();
}
int Gfx_FxBusy(Gfx *g)
{
	GfxRing *r;
	if (!g) return 0;
	for (r = g->rings; r; r = r->next)
		if (r->valid && (r->animating || r->needRedraw)) return 1;
	return (g->fxQueueCount > 0 || g->fxCount > 0) ? 1 : 0;
}

int Gfx_FxIdleMs(Gfx *g)
{
	double now, best = 0.0;
	int found = 0;
	GfxFx *f;
	GfxRing *r;
	if (!g) return -1;
	if (g->fxQueueCount > 0) return 0;
	now = FxNow();
	for (f = g->fxList; f; f = f->next) {
		double end, d;
		if (f->done) return 0;   /* 立刻安排一次绘制来完成拆除 */
		end = f->startMs + (double)f->durMs + 16.0;
		d = end - now;
		if (d < 0.0) d = 0.0;
		if (!found || d < best) { best = d; found = 1; }
	}
	for (r = g->rings; r; r = r->next) {
		double end, d;
		if (!r->valid) continue;
		if (r->needRedraw && !r->animating) return 0;
		if (!r->animating) continue;
		end = r->startMs + (double)r->durMs + 16.0;
		d = end - now;
		if (d < 0.0) d = 0.0;
		if (!found || d < best) { best = d; found = 1; }
	}
	if (!found) return -1;
	if (best > 200.0) best = 200.0;
	return (int)(best + 0.5);
}

void Gfx_FxTick(Gfx *g)
{
	FxMarkDone(g);
	RingMarkDone(g);
}

/* ── PartFx: 独立 visual 的位移部件 ───────────────────────────────── */

struct GfxPart {
	Gfx *g;
	GfxLayer *parent;        /* Gfx_PartCreateOn: 挂在 layer visual 下 */
	int pw, ph;
	float baseX, baseY;
	float x, fromX, toX;     /* DIP */
	float w, fromW, toW;
	double startMs;
	int durMs;
	int animating;
	int visible;
	int seen;
	int needDraw;
	float appliedX, appliedY;
	float appliedW;
	IDCompositionRectangleClip *clip;
	IDCompositionRotateTransform *rot;
	float ang, angFrom, angTo;
	double rotStart;
	int rotDur;
	int rotAnim;
	int spinning;            /* 连续匀速旋转 (Gfx_PartSpin) */
	float spinSpeed;         /* 度/秒 */
	float spinPhase;         /* 重锚时刻相位 (度) */
	double spinStart;        /* 重锚时刻 (ms) */
	ID3D11Texture2D *tex;
	ID2D1Bitmap1 *bmp;
	IDCompositionSurface *surf;
	IDCompositionVisual *vis;
	GfxPartDrawFn fn;
	void *user;
	GfxPart *next;
};

/* cubic-out: 1-(1-p)^3, 纯减速, 不回弹. */
static float PartDecelLerp(float from, float to, double t, int durMs)
{
	double q, u;
	if (durMs <= 0) return to;
	if (t <= 0.0) return from;
	if (t >= (double)durMs) return to;
	q = t / (double)durMs;
	u = 1.0 - q;
	return from + (to - from) * (float)(1.0 - u * u * u);
}

static float PartXNow(GfxPart *p, double now)
{
	if (!p) return 0.0f;
	if (!p->animating) return p->x;
	return PartDecelLerp(p->fromX, p->toX, now - p->startMs, p->durMs);
}

static float PartWNow(GfxPart *p, double now)
{
	if (!p) return 0.0f;
	if (!p->animating) return p->w;
	return PartDecelLerp(p->fromW, p->toW, now - p->startMs, p->durMs);
}

float Gfx_PartX(const GfxPart *p)
{
	return PartXNow((GfxPart *)p, FxNow());
}

float Gfx_PartW(const GfxPart *p)
{
	return PartWNow((GfxPart *)p, FxNow());
}

static float PartSpinNow_(GfxPart *p, double now)
{
	double periodMs, t;
	float deg;
	if (p->spinSpeed <= 0.0f) return p->spinPhase;
	periodMs = 360.0 / (double)p->spinSpeed * 1000.0;
	t = now - p->spinStart;
	if (t >= periodMs && periodMs > 0.0)
		t -= periodMs * (double)(long)(t / periodMs);
	deg = p->spinPhase + (float)(t / 1000.0 * (double)p->spinSpeed);
	while (deg >= 360.0f) deg -= 360.0f;
	while (deg < 0.0f) deg += 360.0f;
	return deg;
}

static float PartRotNow(GfxPart *p, double now)
{
	if (!p) return 0.0f;
	if (p->spinning) return PartSpinNow_(p, now);
	if (!p->rotAnim) return p->ang;
	return PartDecelLerp(p->angFrom, p->angTo, now - p->rotStart, p->rotDur);
}

float Gfx_PartRotation(const GfxPart *p)
{
	return PartRotNow((GfxPart *)p, FxNow());
}

static int PartEnsureRot(GfxPart *p)
{
	Gfx *g;
	if (!p || !p->g || !p->vis) return 0;
	g = p->g;
	if (p->rot) return 1;
	if (FAILED(g->compDevice->CreateRotateTransform(&p->rot)) || !p->rot)
		return 0;
	p->rot->SetCenterX(p->pw * 0.5f);
	p->rot->SetCenterY(p->ph * 0.5f);
	p->rot->SetAngle(0.0f);
	p->vis->SetTransform(p->rot);
	return 1;
}

void Gfx_PartSetRotation(GfxPart *p, float deg)
{
	Gfx *g;
	if (!p || !p->g) return;
	g = p->g;
	if (!PartEnsureRot(p)) return;
	p->spinning = 0;
	p->ang = deg;
	p->rotAnim = 0;
	p->rot->SetAngle(deg);
	if (g->compDevice) g->compDevice->Commit();
}

void Gfx_PartAnimateRotation(GfxPart *p, float toDeg, int durMs)
{
	Gfx *g;
	IDCompositionAnimation *a = NULL;
	double now, d;
	float cur;
	if (!p || !p->g) return;
	g = p->g;
	if (!PartEnsureRot(p)) return;
	now = FxNow();
	cur = PartRotNow(p, now);
	p->spinning = 0;
	p->angFrom = cur;
	p->angTo = toDeg;
	p->rotStart = now;
	p->rotDur = durMs;
	p->rotAnim = 1;
	p->ang = cur;
	if (durMs <= 0 ||
		FAILED(g->compDevice->CreateAnimation(&a)) || !a) {
		p->rotAnim = 0;
		p->ang = toDeg;
		p->rot->SetAngle(toDeg);
		if (g->compDevice) g->compDevice->Commit();
		return;
	}
	d = (double)durMs / 1000.0;
	{
		double dd = (double)toDeg - (double)cur;
		/* cubic-out: 与 CPU 侧 sel_arrow 的 1-(1-p)^3 一致. */
		a->AddCubic(0.0, (float)cur, (float)(dd * 3.0 / d),
			(float)(dd * -3.0 / (d * d)),
			(float)(dd / (d * d * d)));
		a->End(d, toDeg);
	}
	p->rot->SetAngle(a);
	a->Release();
	if (g->compDevice) g->compDevice->Commit();
}

/* 重锚周期: 线性角度在 10 分钟内累计 < 200k 度, float 量化误差 < 0.02°;
 * 更长会让角度精度肉眼可见地抖动。 */
#define GFX_SPIN_REANCHOR_MS 600000.0

void Gfx_PartSpin(GfxPart *p, float degPerSec)
{
	Gfx *g;
	IDCompositionAnimation *a = NULL;
	double now, sec, endDeg;
	float phase;
	if (!p || !p->g) return;
	g = p->g;
	if (degPerSec <= 0.0f) { Gfx_PartSpinStop(p); return; }
	if (!PartEnsureRot(p)) return;
	now = FxNow();
	if (p->spinning && p->spinSpeed == degPerSec &&
		now - p->spinStart < GFX_SPIN_REANCHOR_MS) {
		p->rotAnim = 0;
		return;
	}
	/* 从当前呈现相位续接, 新动画里只保留 [0,360) 之后的 10 分钟航程 */
	phase = PartRotNow(p, now);
	p->spinning = 1;
	p->spinSpeed = degPerSec;
	p->spinPhase = phase;
	p->spinStart = now;
	p->rotAnim = 0;
	p->ang = phase;
	sec = GFX_SPIN_REANCHOR_MS / 1000.0;
	if (FAILED(g->compDevice->CreateAnimation(&a)) || !a) {
		/* 合成器动画不可用: 停在当前相位, 由调用方走回退绘制 */
		p->spinning = 0;
		p->rot->SetAngle(phase);
		if (g->compDevice) g->compDevice->Commit();
		return;
	}
	endDeg = (double)phase + (double)degPerSec * sec;
	/* 匀速: 只有一次项, 二次/三次系数为 0 */
	a->AddCubic(0.0, phase, degPerSec, 0.0f, 0.0f);
	a->End(sec, (float)endDeg);
	p->rot->SetAngle(a);
	a->Release();
	if (g->compDevice) g->compDevice->Commit();
}

void Gfx_PartSpinStop(GfxPart *p)
{
	Gfx *g;
	float ang;
	if (!p || !p->g) return;
	g = p->g;
	ang = PartRotNow(p, FxNow());
	p->spinning = 0;
	p->rotAnim = 0;
	p->ang = ang;
	if (p->rot) {
		p->rot->SetAngle(ang);
		if (g->compDevice) g->compDevice->Commit();
	}
}

void Gfx_PartUpdate(GfxPart *p)
{
	if (p) p->needDraw = 1;
}

/* 内容渲染到自己的纹理, 再拷进 DComp 表面; 只在 needDraw 时执行 */
static void PartDrawContent(Gfx *g, GfxPart *p)
{
	ID2D1Image *savedTarget = NULL;
	ID2D1RenderTarget *savedRt;
	D2D1_MATRIX_3X2_F savedTransform;
	D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
	int sInFrame, sDirtyFull, sDirtyClip, sClipDepth, sAlphaDepth, sForceFull;
	int sXform;
	if (!g || !p || !p->bmp || !p->fn) return;
	savedRt = g->rt;
	g->dc->GetTarget(&savedTarget);
	g->currentTarget = savedTarget;
	g->dc->GetTransform(&savedTransform);
	sInFrame = g->inFrame;
	sDirtyFull = g->dirtyFull;
	sDirtyClip = g->dirtyClip;
	sClipDepth = g->clipDepth;
	sXform = g->xformDepth;
	sAlphaDepth = g->alphaDepth;
	sForceFull = g->forceFull;

	g->dc->SetTarget(p->bmp);
	g->currentTarget = (ID2D1Image *)p->bmp;
	g->rt = (ID2D1RenderTarget *)g->dc;
	g->rt->BeginDraw();
	g->rt->Clear(&clear);
	{
		D2D1_MATRIX_3X2_F m = { g->scale, 0.0f, 0.0f, g->scale, 0.0f, 0.0f };
		g->rt->SetTransform(&m);
	}
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->clipDepth = 0;
	g->xformDepth = 0;
	g->alphaDepth = 0;
	g->dirtyClip = 0;
	g->dirtyFull = 1;
	g->forceFull = 0;
	g->inFrame = 1;
	p->fn(g, p->user);
	while (g->alphaDepth > 0) { g->rt->PopLayer(); g->alphaDepth--; }
	while (g->clipDepth > 0) { g->rt->PopAxisAlignedClip(); g->clipDepth--; }
	if (g->dirtyClip) { g->rt->PopAxisAlignedClip(); g->dirtyClip = 0; }
	g->rt->EndDraw();

	g->dc->SetTarget(savedTarget);
	g->currentTarget = savedTarget;
	g->dc->SetTransform(&savedTransform);
	SAFE_RELEASE(savedTarget);
	g->rt = savedRt;
	g->inFrame = sInFrame;
	g->dirtyFull = sDirtyFull;
	g->dirtyClip = sDirtyClip;
	g->clipDepth = sClipDepth;
	g->xformDepth = sXform;
	g->alphaDepth = sAlphaDepth;
	g->forceFull = sForceFull;

	FxSurfaceDraw(g, p->surf, p->tex, p->pw, p->ph);
	p->needDraw = 0;
}

static int PartsDraw(Gfx *g)
{
	GfxPart *p;
	int drawn = 0;
	if (!g) return 0;
	for (p = g->parts; p; p = p->next) {
		if (!p->needDraw) continue;
		PartDrawContent(g, p);
		drawn++;
	}
	return drawn;
}

static void PartsHideUnseen(Gfx *g)
{
	GfxPart *p;
	int changed = 0;
	if (!g) return;
	for (p = g->parts; p; p = p->next) {
		if (p->seen == g->partFrame) continue;
		if (!p->visible) continue;
		p->visible = 0;
		if (p->vis) p->vis->SetContent(NULL);
		changed = 1;
	}
	if (changed && g->compDevice) g->compDevice->Commit();
}

static void PartsFree(Gfx *g)
{
	GfxPart *p;
	if (!g) return;
	p = g->parts;
	g->parts = NULL;
	while (p) {
		GfxPart *n = p->next;
		if (p->vis) {
			if (p->parent && p->parent->visual)
				p->parent->visual->RemoveVisual(p->vis);
			else if (g->compVisual)
				g->compVisual->RemoveVisual(p->vis);
		}
		SAFE_RELEASE(p->rot);
		SAFE_RELEASE(p->clip);
		SAFE_RELEASE(p->vis);
		SAFE_RELEASE(p->surf);
		SAFE_RELEASE(p->bmp);
		SAFE_RELEASE(p->tex);
		HeapFree(GetProcessHeap(), 0, p);
		p = n;
	}
}

/* 分配纹理/表面/visual 并挂进部件池; 父 visual 由调用方决定. */
static GfxPart *PartCreate_(Gfx *g, float dipW, float dipH,
	GfxPartDrawFn fn, void *user)
{
	GfxPart *p;
	HRESULT hr;
	if (!g || !g->useDcomp || !g->compDevice || !g->compVisual || !g->d3d)
		return NULL;
	if (dipW <= 0.0f || dipH <= 0.0f || !fn) return NULL;
	p = (GfxPart *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
	if (!p) return NULL;
	p->g = g;
	p->pw = (int)(dipW * g->scale + 0.999f);
	p->ph = (int)(dipH * g->scale + 0.999f);
	if (p->pw < 1) p->pw = 1;
	if (p->ph < 1) p->ph = 1;
	p->fn = fn;
	p->user = user;
	p->tex = FxTex(g, p->pw, p->ph);
	if (!p->tex) { HeapFree(GetProcessHeap(), 0, p); return NULL; }
	p->bmp = FxBmp(g, p->tex, 1);
	if (!p->bmp) { SAFE_RELEASE(p->tex); HeapFree(GetProcessHeap(), 0, p); return NULL; }
	hr = g->compDevice->CreateSurface((UINT)p->pw, (UINT)p->ph,
		DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &p->surf);
	if (FAILED(hr) || !p->surf) goto fail;
	if (FAILED(g->compDevice->CreateVisual(&p->vis)) || !p->vis) goto fail;
	p->vis->SetContent(NULL);
	p->vis->SetOffsetX(0.0f);
	p->vis->SetOffsetY(0.0f);
	p->needDraw = 1;
	p->seen = g->partFrame;
	p->next = g->parts;
	g->parts = p;
	return p;
fail:
	if (p->vis) p->vis->Release();
	if (p->surf) p->surf->Release();
	SAFE_RELEASE(p->bmp);
	SAFE_RELEASE(p->tex);
	HeapFree(GetProcessHeap(), 0, p);
	return NULL;
}

GfxPart *Gfx_PartCreate(Gfx *g, float dipW, float dipH,
	GfxPartDrawFn fn, void *user)
{
	GfxPart *p = PartCreate_(g, dipW, dipH, fn, user);
	if (!p) return NULL;
	if (FAILED(CompAddBottom(g->compVisual, p->vis))) {
		Gfx_PartDestroy(p);
		return NULL;
	}
	if (g->compDevice) g->compDevice->Commit();
	return p;
}

GfxPart *Gfx_PartCreateOn(GfxLayer *l, float dipW, float dipH,
	GfxPartDrawFn fn, void *user)
{
	GfxPart *p;
	Gfx *g;
	if (!l || !l->valid || !l->visual) return NULL;
	g = l->g;
	p = PartCreate_(g, dipW, dipH, fn, user);
	if (!p) return NULL;
	/* 子 visual 在 layer 内容之上, 与 layer 一起参与 opacity/位移 */
	if (FAILED(l->visual->AddVisual(p->vis, FALSE, NULL))) {
		Gfx_PartDestroy(p);
		return NULL;
	}
	p->parent = l;
	/* CreateOn 只允许在 Gfx_End 之后调用: 立即把内容画进 surface.
	 * 普通 Gfx_PartCreate 在帧内创建, 内容由本帧 Gfx_End 的 PartsDraw
	 * 上报; 但挂在 layer 上的部件可能伴随"稳定期零重绘", 等不到下一帧,
	 * 表面会一直是空的 (Loading spinner 首次出现要等强制重绘). */
	PartDrawContent(g, p);
	if (g->compDevice) g->compDevice->Commit();
	return p;
}

void Gfx_PartPlace(GfxPart *p, float x, float y)
{
	Gfx *g;
	int changed = 0;
	if (!p || !p->g) return;
	g = p->g;
	p->baseX = x;
	p->baseY = y;
	p->seen = g->partFrame;
	if (p->animating && FxNow() >= p->startMs + (double)p->durMs) {
		p->animating = 0;
		p->x = p->toX;
	}
	if (!p->animating) p->x = x;
	if (p->vis) {
		if (!p->animating && p->appliedX != p->x) {
			p->appliedX = p->x;
			p->vis->SetOffsetX(p->x * g->scale);
			changed = 1;
		}
		if (p->appliedY != y) {
			p->appliedY = y;
			p->vis->SetOffsetY(y * g->scale);
			changed = 1;
		}
	}
	if (!p->visible && p->vis) {
		p->visible = 1;
		p->vis->SetContent(p->surf);
		changed = 1;
	}
	if (changed && g->compDevice) g->compDevice->Commit();
}

void Gfx_PartSetVisible(GfxPart *p, int visible)
{
	Gfx *g;
	int v = visible ? 1 : 0;
	if (!p || !p->g || !p->vis) return;
	g = p->g;
	if (p->visible == v) return;
	p->visible = v;
	p->vis->SetContent(v ? (IUnknown *)p->surf : NULL);
	if (g->compDevice) g->compDevice->Commit();
}

void Gfx_PartAnimateX(GfxPart *p, float toXDip, int durMs)
{
	Gfx *g;
	IDCompositionAnimation *anim = NULL;
	double now, d;
	float cur;
	if (!p || !p->g) return;
	g = p->g;
	now = FxNow();
	cur = PartXNow(p, now);
	p->fromX = cur;
	p->toX = toXDip;
	p->startMs = now;
	p->durMs = durMs;
	p->animating = 1;
	p->x = cur;
	if (!p->vis || durMs <= 0 ||
		FAILED(g->compDevice->CreateAnimation(&anim)) || !anim) {
		p->animating = 0;
		p->x = toXDip;
		if (p->vis) {
			p->appliedX = toXDip;
			p->vis->SetOffsetX(toXDip * g->scale);
		}
		if (g->compDevice) g->compDevice->Commit();
		return;
	}
	d = (double)durMs / 1000.0;
	{
		double c0 = (double)cur * g->scale;
		double c1 = (double)toXDip * g->scale;
		double dd = c1 - c0;
		/* cubic-out: dd * (3 t/d - 3 t^2/d^2 + t^3/d^3). */
		anim->AddCubic(0.0, (float)c0, (float)(dd * 3.0 / d),
			(float)(dd * -3.0 / (d * d)),
			(float)(dd / (d * d * d)));
		anim->End(d, (float)c1);
	}
	p->vis->SetOffsetX(anim);
	anim->Release();
	p->appliedX = toXDip;   /* 动画接管, 避免 Place 再写静态值 */
	if (g->compDevice) g->compDevice->Commit();
}

void Gfx_PartSetClipRadius(GfxPart *p, float radiusDip)
{
	Gfx *g;
	if (!p || !p->g || !p->vis) return;
	g = p->g;
	if (!p->clip) {
		if (FAILED(g->compDevice->CreateRectangleClip(&p->clip)) || !p->clip)
			return;
		p->clip->SetLeft(0.0f);
		p->clip->SetTop(0.0f);
		p->clip->SetRight((float)p->pw);
		p->clip->SetBottom((float)p->ph);
		p->vis->SetClip(p->clip);
		p->w = (float)p->pw / (g->scale > 0.0f ? g->scale : 1.0f);
		p->appliedW = p->w;
	}
	if (radiusDip > 0.0f) {
		float r = radiusDip * g->scale;
		p->clip->SetTopLeftRadiusX(r);
		p->clip->SetTopLeftRadiusY(r);
		p->clip->SetTopRightRadiusX(r);
		p->clip->SetTopRightRadiusY(r);
		p->clip->SetBottomLeftRadiusX(r);
		p->clip->SetBottomLeftRadiusY(r);
		p->clip->SetBottomRightRadiusX(r);
		p->clip->SetBottomRightRadiusY(r);
	}
	g->compDevice->Commit();
}

void Gfx_PartPlaceRect(GfxPart *p, float x, float y, float w)
{
	Gfx *g;
	int changed = 0;
	double now;
	if (!p || !p->g) return;
	g = p->g;
	now = FxNow();
	p->baseX = x;
	p->baseY = y;
	p->seen = g->partFrame;
	if (p->animating && now >= p->startMs + (double)p->durMs) {
		p->animating = 0;
		p->x = p->toX;
		p->w = p->toW;
	}
	if (!p->animating) { p->x = x; p->w = w; }
	if (p->vis) {
		if (!p->animating && p->appliedX != p->x) {
			p->appliedX = p->x;
			p->vis->SetOffsetX(p->x * g->scale);
			changed = 1;
		}
		if (p->appliedY != y) {
			p->appliedY = y;
			p->vis->SetOffsetY(y * g->scale);
			changed = 1;
		}
		if (p->clip && !p->animating && p->appliedW != p->w) {
			p->appliedW = p->w;
			p->clip->SetRight(p->w * g->scale);
			changed = 1;
		}
	}
	if (!p->visible && p->vis) {
		p->visible = 1;
		p->vis->SetContent(p->surf);
		changed = 1;
	}
	if (changed && g->compDevice) g->compDevice->Commit();
}

void Gfx_PartAnimateRect(GfxPart *p, float x, float y, float w, int durMs)
{
	Gfx *g;
	IDCompositionAnimation *ax = NULL;
	IDCompositionAnimation *aw = NULL;
	double now, d;
	float cx, cw;
	if (!p || !p->g) return;
	g = p->g;
	now = FxNow();
	cx = PartXNow(p, now);
	cw = PartWNow(p, now);
	p->fromX = cx;
	p->toX = x;
	p->fromW = cw;
	p->toW = w;
	p->startMs = now;
	p->durMs = durMs;
	p->animating = 1;
	p->x = cx;
	p->w = cw;
	p->baseX = x;
	p->baseY = y;
	p->seen = g->partFrame;
	if (p->vis) {
		p->appliedY = y;
		p->vis->SetOffsetY(y * g->scale);
	}
	if (!p->vis || durMs <= 0 ||
		FAILED(g->compDevice->CreateAnimation(&ax)) || !ax) {
		p->animating = 0;
		p->x = x;
		p->w = w;
		if (p->vis) {
			p->appliedX = x;
			p->vis->SetOffsetX(x * g->scale);
		}
		if (p->clip) {
			p->appliedW = w;
			p->clip->SetRight(w * g->scale);
		}
		if (g->compDevice) g->compDevice->Commit();
		return;
	}
	d = (double)durMs / 1000.0;
	{
		double c0 = (double)cx * g->scale;
		double c1 = (double)x * g->scale;
		double dd = c1 - c0;
		/* cubic-out: dd * (3 t/d - 3 t^2/d^2 + t^3/d^3). */
		ax->AddCubic(0.0, (float)c0, (float)(dd * 3.0 / d),
			(float)(dd * -3.0 / (d * d)),
			(float)(dd / (d * d * d)));
		ax->End(d, (float)c1);
	}
	p->vis->SetOffsetX(ax);
	ax->Release();
	if (p->clip &&
		SUCCEEDED(g->compDevice->CreateAnimation(&aw)) && aw) {
		double w0 = (double)cw * g->scale;
		double w1 = (double)w * g->scale;
		double dw = w1 - w0;
		aw->AddCubic(0.0, (float)w0, (float)(dw * 3.0 / d),
			(float)(dw * -3.0 / (d * d)),
			(float)(dw / (d * d * d)));
		aw->End(d, (float)w1);
		p->clip->SetRight(aw);
		aw->Release();
	} else if (p->clip) {
		p->clip->SetRight(w * g->scale);
	}
	p->appliedX = x;
	p->appliedW = w;
	if (g->compDevice) g->compDevice->Commit();
}

void Gfx_PartDestroy(GfxPart *p)
{
	Gfx *g;
	GfxPart **pp;
	if (!p) return;
	g = p->g;
	if (g) {
		for (pp = &g->parts; *pp; pp = &(*pp)->next) {
			if (*pp == p) { *pp = p->next; break; }
		}
		if (p->vis) {
			if (p->parent && p->parent->visual)
				p->parent->visual->RemoveVisual(p->vis);
			else if (g->compVisual)
				g->compVisual->RemoveVisual(p->vis);
		}
	}
	SAFE_RELEASE(p->rot);
	SAFE_RELEASE(p->clip);
	SAFE_RELEASE(p->vis);
	SAFE_RELEASE(p->surf);
	SAFE_RELEASE(p->bmp);
	SAFE_RELEASE(p->tex);
	HeapFree(GetProcessHeap(), 0, p);
	if (g && g->compDevice) g->compDevice->Commit();
}

/* ── GPU 效果: D2D 内置效果 + 自定义 Grain ────────────────────────── */

static const GUID kCuiGrainEffectId =
	{ 0x8f0d2e61, 0x4b7a, 0x4e2d, { 0x9a, 0x35, 0x6c, 0x11, 0x2f, 0x0d, 0x7e, 0x51 } };
static const GUID kCuiGrainShaderId =
	{ 0xc3b1a9d4, 0x7e62, 0x4f18, { 0x8d, 0x55, 0x2a, 0x91, 0x6c, 0x0e, 0x3f, 0x77 } };

struct alignas(16) GrainConstants {
	float intensity;
	float pad[3];
};

class GrainEffect final : public ID2D1EffectImpl, public ID2D1DrawTransform
{
public:
	GrainEffect() : m_ref(1), m_context(NULL), m_drawInfo(NULL), m_intensity(0.05f) {}

	IFACEMETHOD(QueryInterface)(REFIID riid, void **ppv) override;
	IFACEMETHOD_(ULONG, AddRef)() override;
	IFACEMETHOD_(ULONG, Release)() override;

	IFACEMETHOD(Initialize)(ID2D1EffectContext *context,
		ID2D1TransformGraph *graph) override;
	IFACEMETHOD(PrepareForRender)(D2D1_CHANGE_TYPE changeType) override;
	IFACEMETHOD(SetGraph)(ID2D1TransformGraph *graph) override;

	IFACEMETHOD(SetDrawInfo)(ID2D1DrawInfo *drawInfo) override;
	IFACEMETHOD(MapOutputRectToInputRects)(const D2D1_RECT_L *outputRect,
		D2D1_RECT_L *inputRects, UINT32 inputRectsCount) const override;
	IFACEMETHOD(MapInputRectsToOutputRect)(const D2D1_RECT_L *inputRects,
		const D2D1_RECT_L *inputOpaqueSubRects, UINT32 inputRectCount,
		D2D1_RECT_L *outputRect, D2D1_RECT_L *outputOpaqueSubRect) override;
	IFACEMETHOD(MapInvalidRect)(UINT32 inputIndex, D2D1_RECT_L invalidInputRect,
		D2D1_RECT_L *invalidOutputRect) const override;
	IFACEMETHOD_(UINT32, GetInputCount)() const override { return 1; }

	HRESULT SetIntensity(float value);
	float GetIntensity() const { return m_intensity; }

private:
	LONG m_ref;
	ID2D1EffectContext *m_context;
	ID2D1DrawInfo *m_drawInfo;
	float m_intensity;
};

HRESULT GrainEffect::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv) return E_POINTER;
	if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID2D1EffectImpl)))
		*ppv = static_cast<ID2D1EffectImpl *>(this);
	else if (IsEqualIID(riid, __uuidof(ID2D1DrawTransform)))
		*ppv = static_cast<ID2D1DrawTransform *>(this);
	else if (IsEqualIID(riid, __uuidof(ID2D1Transform)))
		*ppv = static_cast<ID2D1Transform *>(this);
	else if (IsEqualIID(riid, __uuidof(ID2D1TransformNode)))
		*ppv = static_cast<ID2D1TransformNode *>(this);
	else {
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	AddRef();
	return S_OK;
}

ULONG GrainEffect::AddRef()
{
	return (ULONG)InterlockedIncrement(&m_ref);
}

ULONG GrainEffect::Release()
{
	LONG ref = InterlockedDecrement(&m_ref);
	if (ref == 0) {
		this->~GrainEffect();
		HeapFree(GetProcessHeap(), 0, this);
	}
	return (ULONG)ref;
}

HRESULT GrainEffect::Initialize(ID2D1EffectContext *context,
	ID2D1TransformGraph *graph)
{
	if (!context || !graph) return E_POINTER;
	SAFE_RELEASE(m_context);
	m_context = context;
	m_context->AddRef();
	/* 同一个 shader GUID 只加载一次; 多个 effect 实例复用. */
	HRESULT hr = context->LoadPixelShader(kCuiGrainShaderId,
		g_grainCso, (UINT32)sizeof(g_grainCso));
	if (FAILED(hr)) return hr;
	return graph->SetSingleTransformNode(static_cast<ID2D1DrawTransform *>(this));
}

HRESULT GrainEffect::PrepareForRender(D2D1_CHANGE_TYPE changeType)
{
	(void)changeType;
	if (!m_drawInfo) return S_OK;
	GrainConstants cb;
	cb.intensity = m_intensity;
	cb.pad[0] = cb.pad[1] = cb.pad[2] = 0.0f;
	m_drawInfo->SetPixelShader(kCuiGrainShaderId);
	return m_drawInfo->SetPixelShaderConstantBuffer(
		(const BYTE *)&cb, (UINT32)sizeof(cb));
}

HRESULT GrainEffect::SetGraph(ID2D1TransformGraph *graph)
{
	(void)graph;
	return S_OK;
}

HRESULT GrainEffect::SetDrawInfo(ID2D1DrawInfo *drawInfo)
{
	GrainConstants cb;
	m_drawInfo = drawInfo;
	if (!drawInfo) return E_POINTER;
	cb.intensity = m_intensity;
	cb.pad[0] = cb.pad[1] = cb.pad[2] = 0.0f;
	drawInfo->SetPixelShader(kCuiGrainShaderId);
	return drawInfo->SetPixelShaderConstantBuffer(
		(const BYTE *)&cb, (UINT32)sizeof(cb));
}

HRESULT GrainEffect::MapOutputRectToInputRects(const D2D1_RECT_L *outputRect,
	D2D1_RECT_L *inputRects, UINT32 inputRectsCount) const
{
	if (!outputRect || !inputRects || inputRectsCount < 1) return E_INVALIDARG;
	inputRects[0] = *outputRect;
	return S_OK;
}

HRESULT GrainEffect::MapInputRectsToOutputRect(const D2D1_RECT_L *inputRects,
	const D2D1_RECT_L *inputOpaqueSubRects, UINT32 inputRectCount,
	D2D1_RECT_L *outputRect, D2D1_RECT_L *outputOpaqueSubRect)
{
	(void)inputOpaqueSubRects;
	if (!inputRects || !outputRect || !outputOpaqueSubRect || inputRectCount != 1)
		return E_INVALIDARG;
	*outputRect = inputRects[0];
	/* 文档要求: 没改变透明度时直接沿用输入的 opaque 子矩形. */
	*outputOpaqueSubRect = inputOpaqueSubRects[0];
	return S_OK;
}

HRESULT GrainEffect::MapInvalidRect(UINT32 inputIndex,
	D2D1_RECT_L invalidInputRect, D2D1_RECT_L *invalidOutputRect) const
{
	(void)inputIndex;
	if (!invalidOutputRect) return E_POINTER;
	*invalidOutputRect = invalidInputRect;
	return S_OK;
}

HRESULT GrainEffect::SetIntensity(float value)
{
	if (value < 0.0f) value = 0.0f;
	if (value > 1.0f) value = 1.0f;
	m_intensity = value;
	return S_OK;
}

static HRESULT CALLBACK GrainEffectCreate(IUnknown **effect)
{
	if (!effect) return E_POINTER;
	void *mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GrainEffect));
	if (!mem) return E_OUTOFMEMORY;
	GrainEffect *impl = new (mem) GrainEffect();
	*effect = static_cast<ID2D1EffectImpl *>(impl);
	return S_OK;
}

static const WCHAR kGrainXml[] =
	L"<?xml version='1.0'?>"
	L"<Effect>"
	L"<Property name='DisplayName' type='string' value='CUI Grain'/>"
	L"<Property name='Author' type='string' value='CUI'/>"
	L"<Property name='Category' type='string' value='CUI'/>"
	L"<Property name='Description' type='string' value='Film grain overlay'/>"
	L"<Inputs><Input name='Source'/></Inputs>"
	L"<Property name='Intensity' type='float'>"
	L"<Property name='DisplayName' type='string' value='Intensity'/>"
	L"<Property name='Min' type='float' value='0.0'/>"
	L"<Property name='Max' type='float' value='1.0'/>"
	L"<Property name='Default' type='float' value='0.05'/>"
	L"</Property>"
	L"</Effect>";

static const D2D1_PROPERTY_BINDING kGrainBindings[] = {
	D2D1_VALUE_TYPE_BINDING(L"Intensity", &GrainEffect::SetIntensity, &GrainEffect::GetIntensity)
};

static GfxEffect *EffectWrap(Gfx *g, ID2D1Effect *fx, float pad, int kind)
{
	GfxEffect *e;
	if (!g || !fx) return NULL;
	e = (GfxEffect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxEffect));
	if (!e) {
		fx->Release();
		return NULL;
	}
	e->g = g;
	e->fx = fx;
	e->pad = pad;
	e->kind = kind;
	e->valid = 1;
	e->next = g->effects;
	g->effects = e;
	return e;
}

static int FxSetFloat(ID2D1Effect *fx, UINT32 prop, float value)
{
	if (!fx) return 0;
	return SUCCEEDED(fx->SetValue(prop, D2D1_PROPERTY_TYPE_FLOAT,
		(const BYTE *)&value, (UINT32)sizeof(value)));
}

GfxEffect *Gfx_CreateBlur(Gfx *g, float stdDev, int borderMode)
{
	ID2D1Effect *fx = NULL;
	int mode;
	if (!g || !g->useDcomp || !g->dc || stdDev <= 0.0f) return NULL;
	if (FAILED(g->dc->CreateEffect(CLSID_D2D1GaussianBlur, &fx)) || !fx) return NULL;
	FxSetFloat(fx, D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, stdDev);
	mode = borderMode ? (int)D2D1_BORDER_MODE_HARD : (int)D2D1_BORDER_MODE_SOFT;
	fx->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_PROPERTY_TYPE_ENUM,
		(const BYTE *)&mode, (UINT32)sizeof(mode));
	return EffectWrap(g, fx, stdDev * 3.0f, GFX_FX_OTHER);
}

GfxEffect *Gfx_CreateShadow(Gfx *g, float stdDev, GfxColor color)
{
	ID2D1Effect *fx = NULL;
	D2D1_COLOR_F c;
	if (!g || !g->useDcomp || !g->dc || stdDev <= 0.0f) return NULL;
	if (FAILED(g->dc->CreateEffect(CLSID_D2D1Shadow, &fx)) || !fx) return NULL;
	FxSetFloat(fx, D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, stdDev);
	c = ToColor(color);
	fx->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1_PROPERTY_TYPE_VECTOR4,
		(const BYTE *)&c, (UINT32)sizeof(c));
	return EffectWrap(g, fx, stdDev * 3.0f + 4.0f, GFX_FX_OTHER);
}

GfxEffect *Gfx_CreateColorMatrix(Gfx *g, const float *matrix20)
{
	ID2D1Effect *fx = NULL;
	D2D1_MATRIX_5X4_F m;
	if (!g || !g->useDcomp || !g->dc || !matrix20) return NULL;
	if (FAILED(g->dc->CreateEffect(CLSID_D2D1ColorMatrix, &fx)) || !fx) return NULL;
	memcpy(&m, matrix20, sizeof(m));
	fx->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1_PROPERTY_TYPE_MATRIX_5X4,
		(const BYTE *)&m, (UINT32)sizeof(m));
	return EffectWrap(g, fx, 0.0f, GFX_FX_COLOR_MATRIX);
}

GfxEffect *Gfx_CreateGrain(Gfx *g, float intensity)
{
	ID2D1Effect *fx = NULL;
	if (!g || !g->useDcomp || !g->dc || !g->d2d) return NULL;
	if (!g->grainFxRegistered) {
		g->d2d->RegisterEffectFromString(kCuiGrainEffectId, kGrainXml,
			kGrainBindings, 1, GrainEffectCreate);
		g->grainFxRegistered = 1;
	}
	if (FAILED(g->dc->CreateEffect(kCuiGrainEffectId, &fx)) || !fx) return NULL;
	if (intensity < 0.0f) intensity = 0.0f;
	if (intensity > 1.0f) intensity = 1.0f;
	fx->SetValue(0, D2D1_PROPERTY_TYPE_FLOAT,
		(const BYTE *)&intensity, (UINT32)sizeof(intensity));
	return EffectWrap(g, fx, 0.0f, GFX_FX_OTHER);
}

int Gfx_EffectSetColorMatrix(GfxEffect *fx, const float *matrix20)
{
	D2D1_MATRIX_5X4_F m;
	if (!fx || !fx->valid || !fx->fx || fx->kind != GFX_FX_COLOR_MATRIX || !matrix20)
		return 0;
	memcpy(&m, matrix20, sizeof(m));
	return SUCCEEDED(fx->fx->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX,
		D2D1_PROPERTY_TYPE_MATRIX_5X4, (const BYTE *)&m, (UINT32)sizeof(m)));
}

int Gfx_EffectAlive(GfxEffect *fx)
{
	return fx && fx->valid && fx->fx && fx->g && fx->g->useDcomp;
}

static int FrameEffectsReady(Gfx *g)
{
	int i;
	if (!g || g->frameFxCount <= 0) return 0;
	for (i = 0; i < g->frameFxCount; i++)
		if (!Gfx_EffectAlive(g->frameFx[i])) return 0;
	return 1;
}

static ID2D1Image *EffectChainOutput(GfxEffect *const *effects, int count,
	ID2D1Image *input)
{
	ID2D1Effect *prev = NULL;
	ID2D1Image *out = NULL;
	int i;
	if (!effects || count <= 0 || !input) return NULL;
	for (i = 0; i < count; i++) {
		if (!effects[i] || !effects[i]->valid || !effects[i]->fx)
			return NULL;
		if (i == 0)
			effects[i]->fx->SetInput(0, input);
		else
			effects[i]->fx->SetInputEffect(0, prev, TRUE);
		prev = effects[i]->fx;
	}
	if (prev) prev->GetOutput(&out);
	return out;
}

int Gfx_SetFrameEffects(Gfx *g, GfxEffect *const *effects, int count)
{
	int i;
	if (!g || count < 0 || count > GFX_MAX_FRAME_EFFECTS) return 0;
	if (count == 0) {
		g->frameFxCount = 0;
		return 1;
	}
	for (i = 0; i < count; i++) {
		if (!Gfx_EffectAlive(effects[i]) || effects[i]->g != g)
			return 0;
	}
	for (i = 0; i < count; i++) g->frameFx[i] = effects[i];
	g->frameFxCount = count;
	return 1;
}

void Gfx_EffectDestroy(GfxEffect *fx)
{
	Gfx *g;
	GfxEffect **pp;
	if (!fx) return;
	g = fx->g;
	if (g) {
		for (pp = &g->effects; *pp; pp = &(*pp)->next) {
			if (*pp == fx) {
				*pp = fx->next;
				break;
			}
		}
		for (int i = 0; i < g->frameFxCount; i++) {
			if (g->frameFx[i] == fx) {
				for (int j = i; j + 1 < g->frameFxCount; j++)
					g->frameFx[j] = g->frameFx[j + 1];
				g->frameFxCount--;
				break;
			}
		}
	}
	SAFE_RELEASE(fx->fx);
	HeapFree(GetProcessHeap(), 0, fx);
}

int Gfx_DrawWithEffects(Gfx *g, GfxEffect *const *effects, int count,
	GfxEffectDrawFn fn, void *user, GfxRect bounds)
{
	ID2D1Bitmap1 *content = NULL;
	ID2D1Image *out = NULL;
	ID2D1Image *savedTarget;
	D2D1_MATRIX_3X2_F savedTransform;
	D2D1_MATRIX_3X2_F contentTransform;
	D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
	D2D1_BITMAP_PROPERTIES1 bp;
	D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
	GfxRect savedDirty;
	int savedDirtyFull, savedForceFull;
	float scale, pad = 0.0f;
	int padPx, wPx, hPx, i, ok = 0;
	HRESULT hr;

	if (!g || !effects || count <= 0 || !fn) return 0;
	if (!g->useDcomp || !g->dc || !g->inFrame || !g->currentTarget) return 0;
	if (!g->rt || g->clipDepth || g->alphaDepth || g->dirtyClip || g->fxDepth) return 0;
	for (i = 0; i < count; i++) {
		if (!effects[i] || !effects[i]->valid || !effects[i]->fx ||
			effects[i]->g != g)
			return 0;
		if (effects[i]->pad > pad) pad = effects[i]->pad;
	}
	if (bounds.w <= 0.0f || bounds.h <= 0.0f) return 0;
	scale = g->scale > 0.0f ? g->scale : 1.0f;
	padPx = (int)(pad * scale + 0.5f);
	wPx = (int)(bounds.w * scale + 0.5f) + padPx * 2;
	hPx = (int)(bounds.h * scale + 0.5f) + padPx * 2;
	if (wPx <= 0 || hPx <= 0) return 0;

	bp = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0f, 96.0f);
	hr = g->dc->CreateBitmap(D2D1::SizeU((UINT32)wPx, (UINT32)hPx),
		NULL, 0, &bp, &content);
	if (FAILED(hr) || !content) return 0;

	savedTarget = g->currentTarget;
	savedDirty = g->dirty;
	savedDirtyFull = g->dirtyFull;
	savedForceFull = g->forceFull;
	g->rt->GetTransform(&savedTransform);
	contentTransform = D2D1::Matrix3x2F::Scale(scale, scale) *
		D2D1::Matrix3x2F::Translation((float)padPx - bounds.x * scale,
			(float)padPx - bounds.y * scale);

	/* 离屏内容必须另起一帧: D2D 不允许在 BeginDraw 中切换目标. */
	hr = g->rt->EndDraw();
	if (FAILED(hr)) {
		SAFE_RELEASE(content);
		return 0;
	}

	g->dc->SetTarget(content);
	g->currentTarget = (ID2D1Image *)content;
	g->rt = (ID2D1RenderTarget *)g->dc;
	g->rt->BeginDraw();
	g->rt->Clear(&clear);
	g->rt->SetTransform(&contentTransform);
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->clipDepth = 0;
	g->xformDepth = 0;
	g->alphaDepth = 0;
	g->dirtyClip = 0;
	g->dirtyFull = 1;
	g->forceFull = 0;
	g->inFrame = 1;
	g->fxDepth++;
	fn(g, user);
	g->fxDepth--;
	while (g->alphaDepth > 0) { g->rt->PopLayer(); g->alphaDepth--; }
	while (g->clipDepth > 0) { g->rt->PopAxisAlignedClip(); g->clipDepth--; }
	hr = g->rt->EndDraw();

	/* 恢复原目标并重新 BeginDraw, 调用方可以继续画. */
	g->dc->SetTarget(savedTarget);
	g->currentTarget = savedTarget;
	g->rt = (ID2D1RenderTarget *)g->dc;
	g->rt->BeginDraw();
	g->rt->SetTransform(&savedTransform);
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->clipDepth = 0;
	g->xformDepth = 0;
	g->alphaDepth = 0;
	g->dirtyClip = 0;
	g->dirty = savedDirty;
	g->dirtyFull = savedDirtyFull;
	g->forceFull = savedForceFull;
	g->inFrame = 1;

	if (SUCCEEDED(hr)) {
		out = EffectChainOutput(effects, count, (ID2D1Image *)content);
		if (!out) hr = E_FAIL;
		if (SUCCEEDED(hr) && out) {
			D2D1_POINT_2F at;
			at.x = bounds.x * scale - (float)padPx;
			at.y = bounds.y * scale - (float)padPx;
			g->rt->SetTransform(&identity);
			g->dc->DrawImage(out, &at, NULL,
				D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
			g->rt->SetTransform(&savedTransform);
			ok = 1;
		}
	}
	if (!ok) {
		/* 效果链失败: 至少把内容原样合成回去, 不让调用方丢内容. */
		D2D1_RECT_F dst;
		dst.left = bounds.x * scale - (float)padPx;
		dst.top = bounds.y * scale - (float)padPx;
		dst.right = dst.left + (float)wPx;
		dst.bottom = dst.top + (float)hPx;
		g->rt->SetTransform(&identity);
		g->rt->DrawBitmap(content, &dst, 1.0f,
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		g->rt->SetTransform(&savedTransform);
		ok = 1;
	}
	SAFE_RELEASE(out);
	SAFE_RELEASE(content);
	return ok;
}

int Gfx_Resize(Gfx *g, int pixelW, int pixelH, float scale)
{
	if (!g || pixelW <= 0 || pixelH <= 0) return 0;
	if (scale < 0.5f) scale = 0.5f;
	if (scale > 4.0f) scale = 4.0f;
	float oldScale = g->scale;
	g->scale = scale;
	if (!g->dcompTried) {
		g->pw = pixelW;
		g->ph = pixelH;
		g->dcompTried = 1;
		if (EnvOn("SCV2_ULW")) {
			g->useDcomp = 0;
			SetCompWindowStyle(g->hwnd, 0);
		}
		else if (!InitDcomp(g)) {
			g->useDcomp = 0;
			SetCompWindowStyle(g->hwnd, 0);
		}
	}
	if (g->useDcomp) {
		if (g->pw == pixelW && g->ph == pixelH) {
			if (oldScale != scale) { g->forceFull = 1; g->dirtyFull = 1; }
			return 1;
		}
		g->pw = pixelW;
		g->ph = pixelH;
		if (ResizeDcomp(g)) return 1;
		FallbackToUlw(g);
		return g->rt != NULL;
	}
	if (g->dcRt && g->pw == pixelW && g->ph == pixelH) {
		if (oldScale != scale) {
			/* DPI 变化但物理尺寸没变：DIB 里的旧内容按旧缩放绘制，必须整帧重画。 */
			g->forceFull = 1;
			g->dirtyFull = 1;
		}
		return 1;
	}
	g->pw = pixelW;
	g->ph = pixelH;
	return CreateSurface(g);
}

int Gfx_Generation(Gfx *g) { return g ? g->generation : 0; }
int Gfx_DeviceGeneration(Gfx *g) { return g ? g->deviceGen : 0; }

/* ── 外部合成 Visual 宿主 ─────────────────────────────────────────── */

HWND Gfx_IntHwnd(Gfx *g) { return g ? g->hwnd : NULL; }
float Gfx_IntScale(Gfx *g) { return (g && g->scale > 0.0f) ? g->scale : 1.0f; }

int Gfx_IntDcomp(Gfx *g)
{
	return g && g->useDcomp && g->compDevice && g->compVisual;
}

int Gfx_IntRenderMode(Gfx *g)
{
	if (!g || !g->dcompTried) return 0;
	return g->useDcomp ? 1 : 2;
}

void *Gfx_IntCreateChildVisual(Gfx *g, GfxRect rc, int top)
{
	IDCompositionVisual *v = NULL;
	float s, w, h;
	if (!Gfx_IntDcomp(g)) return NULL;
	if (FAILED(g->compDevice->CreateVisual(&v)) || !v) return NULL;
	s = g->scale > 0.0f ? g->scale : 1.0f;
	w = (rc.w > 1.0f ? rc.w : 1.0f) * s;
	h = (rc.h > 1.0f ? rc.h : 1.0f) * s;
	if (FAILED(v->SetOffsetX(rc.x * s)) || FAILED(v->SetOffsetY(rc.y * s)) ||
		FAILED(v->SetClip(D2D1::RectF(0.0f, 0.0f, w, h))) ||
		FAILED(top ? CompAddTop(g->compVisual, v)
			: CompAddBottom(g->compVisual, v)) ||
		FAILED(g->compDevice->Commit())) {
		v->Release();
		return NULL;
	}
	return v;
}

void Gfx_IntUpdateChildVisual(Gfx *g, void *visual, GfxRect rc, int visible)
{
	IDCompositionVisual *v = (IDCompositionVisual *)visual;
	float s, w, h;
	if (!g || !v || !g->compDevice) return;
	s = g->scale > 0.0f ? g->scale : 1.0f;
	w = (rc.w > 1.0f ? rc.w : 1.0f) * s;
	h = (rc.h > 1.0f ? rc.h : 1.0f) * s;
	v->SetOffsetX(rc.x * s);
	v->SetOffsetY(rc.y * s);
	v->SetClip(visible ? D2D1::RectF(0.0f, 0.0f, w, h)
		: D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f));
	g->compDevice->Commit();
}

void Gfx_IntDestroyChildVisual(Gfx *g, void *visual)
{
	IDCompositionVisual *v = (IDCompositionVisual *)visual;
	if (!g || !v) return;
	if (g->compVisual) g->compVisual->RemoveVisual(v);
	if (g->compDevice) g->compDevice->Commit();
	v->Release();
}

void Gfx_IntCommit(Gfx *g)
{
	if (g && g->compDevice) g->compDevice->Commit();
}
int Gfx_Ready(Gfx *g) { return g && g->rt != NULL; }

int Gfx_GradientCreates(Gfx *g)
{
	if (!g) return 0;
	int n = g->gradCreates;
	g->gradCreates = 0;
	return n;
}

void Gfx_SetDirtyRect(Gfx *g, const RECT *rcPx)
{
	if (!g) return;
	if (!rcPx || rcPx->right <= rcPx->left || rcPx->bottom <= rcPx->top ||
		g->forceFull || g->pw <= 0 || g->ph <= 0) {
		g->dirtyFull = 1;
		return;
	}
	RECT rc = *rcPx;
	if (rc.left < 0) rc.left = 0;
	if (rc.top < 0) rc.top = 0;
	if (rc.right > g->pw) rc.right = g->pw;
	if (rc.bottom > g->ph) rc.bottom = g->ph;
	if (rc.right <= rc.left || rc.bottom <= rc.top) {
		g->dirtyFull = 1;
		return;
	}
	if (rc.left == 0 && rc.top == 0 && rc.right == g->pw && rc.bottom == g->ph) {
		g->dirtyFull = 1;
		return;
	}
	float s = g->scale > 0.0f ? g->scale : 1.0f;
	g->dirty.x = (float)rc.left / s;
	g->dirty.y = (float)rc.top / s;
	g->dirty.w = (float)(rc.right - rc.left) / s;
	g->dirty.h = (float)(rc.bottom - rc.top) / s;
	g->dirtyFull = 0;
}

void Gfx_SetLowLatency(Gfx *g, int on)
{
	if (g) g->lowLatency = on ? 1 : 0;
}

void Gfx_Begin(Gfx *g)
{
	if (!g || !g->rt) return;
	g->partFrame++;
	/* 始终画到离屏 uiTarget: 局部重绘要把"上一帧的其余部分"拷回去, 必须有持久层。
	 * 曾试过整帧直画后缓冲, 省下的拷贝被 Present 等 vsync 掩盖 (无收益), 却让
	 * 之后的局部重绘拷到从未画过的离屏内容 → 控件整块消失 (真机)。 */
	if (g->useDcomp && g->dc && g->uiTarget) {
		g->dc->SetTarget(g->uiTarget);
		g->currentTarget = (ID2D1Image *)g->uiTarget;
	} else {
		g->currentTarget = NULL;
	}
	g->rt->BeginDraw();
	if (g->useDcomp && g->dirtyFull) {
		D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
		g->rt->Clear(&clear);
	}
	D2D1_MATRIX_3X2_F m = { g->scale, 0.0f, 0.0f, g->scale, 0.0f, 0.0f };
	g->rt->SetTransform(&m);
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->clipDepth = 0;
	g->xformDepth = 0;
	g->alphaDepth = 0;
	g->dirtyClip = 0;
	if (!g->dirtyFull) {
		D2D1_RECT_F rc = { g->dirty.x, g->dirty.y,
			g->dirty.x + g->dirty.w, g->dirty.y + g->dirty.h };
		g->rt->PushAxisAlignedClip(&rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		g->dirtyClip = 1;
	}
	g->forceFull = 0;
	g->inFrame = 1;
}

int Gfx_End(Gfx *g)
{
	if (!g || !g->rt || !g->inFrame) return 0;
	while (g->alphaDepth > 0) { g->rt->PopLayer(); g->alphaDepth--; }
	while (g->clipDepth > 0) { g->rt->PopAxisAlignedClip(); g->clipDepth--; }
	if (g->dirtyClip) { g->rt->PopAxisAlignedClip(); g->dirtyClip = 0; }
	/* ULW 回退路径要拿本帧的脏区做局部上传; 先保存再复位。 */
	int wasFull = g->dirtyFull;
	GfxRect dirtyDip = g->dirty;
	HRESULT hr = g->rt->EndDraw(NULL, NULL);
	g->inFrame = 0;
	g->dirtyFull = 1; /* 下一帧默认整帧；UI 会在 Begin 前显式设置脏区 */
	if (g->useDcomp) {
		if (FAILED(hr)) {
			Log_Write("[gfx] DComp EndDraw failed 0x%08X", (unsigned)hr);
			FallbackToUlw(g);
			return 0;
		}
		/* 部件内容 (滑块/指示器): 只在内容变化时渲染到自己的表面 */
		int partsDrawn = PartsDraw(g);
		/* 本帧新过渡: 用排队时抓的旧快照建淡出覆盖层 (不透明度 1) */
		FxEndFrame(g);
		FxMarkDone(g);
		if (g->fxBuilt <= 0 && partsDrawn && g->compDevice)
			g->compDevice->Commit();   /* 部件表面内容要 Commit 才上屏 */
		g->dc->SetTarget(g->swapTarget);
		g->currentTarget = (ID2D1Image *)g->swapTarget;
		g->dc->SetTransform(D2D1::Matrix3x2F::Identity());
		g->dc->BeginDraw();
		D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
		g->dc->Clear(&clear);
		{
			ID2D1Image *fxOut = NULL;
			if (FrameEffectsReady(g))
				fxOut = EffectChainOutput(g->frameFx, g->frameFxCount,
					(ID2D1Image *)g->uiTarget);
			if (fxOut) {
				D2D1_POINT_2F at = { 0.0f, 0.0f };
				g->dc->DrawImage(fxOut, &at, NULL,
					D2D1_INTERPOLATION_MODE_LINEAR,
					D2D1_COMPOSITE_MODE_SOURCE_OVER);
				fxOut->Release();
			} else {
				D2D1_RECT_F dst = { 0.0f, 0.0f, (FLOAT)g->pw, (FLOAT)g->ph };
				g->dc->DrawBitmap(g->uiTarget, &dst, 1.0f,
					D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
			}
		}
		HRESULT phr = g->dc->EndDraw();
		if (FAILED(phr)) {
			Log_Write("[gfx] DComp present draw failed 0x%08X", (unsigned)phr);
			FallbackToUlw(g);
			return 0;
		}
		/* 新主帧上屏前确保旧快照覆盖层已进入合成 (绘制期间已发出 Commit)。 */
		if (g->fxBuilt > 0) FxWaitComposited();
		HRESULT sr = g->swap->Present(
			(g->noVsync || g->lowLatency) ? 0 : 1, 0);
		if (FAILED(sr)) {
			Log_Write("[gfx] DComp Present failed 0x%08X", (unsigned)sr);
			FallbackToUlw(g);
			return 0;
		}
		/* 覆盖层已淡出为全透明, 拆除与上屏先后无关 */
		if (g->fxBuilt > 0) FxStartAnim(g);
		if (FxHasDone(g)) FxRemoveDone(g);
		/* 本帧没再声明的部件隐藏 (换页/控件消失) */
		PartsHideUnseen(g);
		return 1;
	}
	if (hr == D2DERR_RECREATE_TARGET) {
		Log_Write("[gfx] EndDraw: recreate target");
		CreateSurface(g);
		if (g->hwnd) InvalidateRect(g->hwnd, NULL, FALSE);
		return 0;
	}
	if (FAILED(hr)) {
		Log_Write("[gfx] EndDraw failed 0x%08X", (unsigned)hr);
		return 0;
	}
	if (!g->memDC || !g->hwnd) return 0;
	RECT wr;
	if (!GetWindowRect(g->hwnd, &wr)) return 0;
	POINT dst = { wr.left, wr.top };
	POINT src = { 0, 0 };
	SIZE sz = { g->pw, g->ph };
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	UPDATELAYEREDWINDOWINFO info;
	RECT dirtyPx;
	ZeroMemory(&info, sizeof(info));
	info.cbSize = sizeof(info);
	info.pptDst = &dst;
	info.psize = &sz;
	info.hdcSrc = g->memDC;
	info.pptSrc = &src;
	info.pblend = &bf;
	info.dwFlags = ULW_ALPHA;
	if (!wasFull) {
		/* 只把本帧真正重绘的区域传给 DWM；DIB 其余部分保持上一帧内容。 */
		float s = g->scale > 0.0f ? g->scale : 1.0f;
		dirtyPx.left = (LONG)(dirtyDip.x * s);
		dirtyPx.top = (LONG)(dirtyDip.y * s);
		dirtyPx.right = (LONG)((dirtyDip.x + dirtyDip.w) * s + 1.0f);
		dirtyPx.bottom = (LONG)((dirtyDip.y + dirtyDip.h) * s + 1.0f);
		if (dirtyPx.left < 0) dirtyPx.left = 0;
		if (dirtyPx.top < 0) dirtyPx.top = 0;
		if (dirtyPx.right > g->pw) dirtyPx.right = g->pw;
		if (dirtyPx.bottom > g->ph) dirtyPx.bottom = g->ph;
		if (dirtyPx.right > dirtyPx.left && dirtyPx.bottom > dirtyPx.top)
			info.prcDirty = &dirtyPx;
	}
	if (!UpdateLayeredWindowIndirect(g->hwnd, &info))
		return 0;
	return 1;
}

int Gfx_PushAlpha(Gfx *g, float alpha)
{
	if (!g || !g->rt || !g->inFrame) return 0;
	if (alpha >= 0.999f) return 0;          /* 不必要压层 */
	if (alpha < 0.0f) alpha = 0.0f;
	if (!g->alphaLayer) {
		if (FAILED(g->rt->CreateLayer(NULL, &g->alphaLayer)) || !g->alphaLayer)
			return 0;
	}
	D2D1_LAYER_PARAMETERS p;
	ZeroMemory(&p, sizeof(p));
	p.contentBounds.left = -1.0e6f;
	p.contentBounds.top = -1.0e6f;
	p.contentBounds.right = 1.0e6f;
	p.contentBounds.bottom = 1.0e6f;
	p.opacity = alpha;
	p.layerOptions = D2D1_LAYER_OPTIONS_NONE;
	g->rt->PushLayer(&p, g->alphaLayer);
	g->alphaDepth++;
	return 1;
}

void Gfx_PopAlpha(Gfx *g)
{
	if (!g || !g->rt || g->alphaDepth <= 0) return;
	g->rt->PopLayer();
	g->alphaDepth--;
}

void Gfx_Clear(Gfx *g, GfxColor c)
{
	if (!g || !g->rt) return;
	if (g->dirtyFull) {
		D2D1_COLOR_F col = ToColor(c);
		g->rt->Clear(&col);
		return;
	}
	Gfx_FillRect(g, g->dirty, c);
}

static void FillRounded(Gfx *g, GfxRect r, float radius, ID2D1Brush *brush)
{
	if (!brush || r.w <= 0.0f || r.h <= 0.0f) return;
	float maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
	if (radius < 0.0f) radius = 0.0f;
	if (radius > maxR) radius = maxR;
	D2D1_ROUNDED_RECT rr;
	rr.rect.left = r.x; rr.rect.top = r.y;
	rr.rect.right = r.x + r.w; rr.rect.bottom = r.y + r.h;
	rr.radiusX = radius; rr.radiusY = radius;
	g->rt->FillRoundedRectangle(&rr, brush);
}

void Gfx_FillRect(Gfx *g, GfxRect r, GfxColor c)
{
	if (!g || !g->rt || !IsVisible(g, r)) return;
	ID2D1SolidColorBrush *br = GetBrush(g, c);
	if (!br) return;
	D2D1_RECT_F rc = { r.x, r.y, r.x + r.w, r.y + r.h };
	g->rt->FillRectangle(&rc, br);
}

void Gfx_FillRoundRect(Gfx *g, GfxRect r, float radius, GfxColor c)
{
	if (!g || !g->rt || !IsVisible(g, r)) return;
	FillRounded(g, r, radius, GetBrush(g, c));
}

/* 渐变画刷按单位空间缓存 (起点 0,0 -> 1,0 / 0,1), 每次使用时用矩阵把它映射到
 * 目标矩形。尺寸变化 (进度条逐帧变宽/布局位移) 不再触发重建 stop collection。 */
static void SetGradTransform(ID2D1LinearGradientBrush *br, GfxRect r)
{
	D2D1_MATRIX_3X2_F m;
	if (!br) return;
	m._11 = r.w; m._12 = 0.0f;
	m._21 = 0.0f; m._22 = r.h;
	m._31 = r.x; m._32 = r.y;
	br->SetTransform(&m);
}

static ID2D1LinearGradientBrush *GetGradientBrush(Gfx *g, GfxRect r,
	const GfxColor *colors, const float *positions, int count, int horizontal,
	ID2D1GradientStopCollection **outStops, int *transient)
{
	*outStops = NULL;
	*transient = 0;
	if (!g || !g->rt || !colors || !positions || count < 2) return NULL;
	if (count > 16) count = 16;

	if (count <= GFX_GRAD_MAX_STOPS) {
		for (int i = 0; i < GFX_GRAD_CACHE_MAX; i++) {
			GfxGradientCache *e = &g->grads[i];
			if (!e->brush) continue;
			if (e->count != count || e->horizontal != horizontal)
				continue;
			int same = 1;
			for (int k = 0; k < count; k++) {
				if (e->colors[k] != colors[k] || e->positions[k] != positions[k]) { same = 0; break; }
			}
			if (!same) continue;
			e->lastUse = ++g->gradClock;
			SetGradTransform(e->brush, r);
			return e->brush;
		}
	}

	D2D1_GRADIENT_STOP stops[16];
	for (int i = 0; i < count; i++) {
		stops[i].position = positions[i];
		stops[i].color = ToColor(colors[i]);
	}
	ID2D1GradientStopCollection *gs = NULL;
	if (FAILED(g->rt->CreateGradientStopCollection(stops, (UINT32)count,
		D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &gs)) || !gs) return NULL;
	D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp;
	lp.startPoint.x = 0.0f; lp.startPoint.y = 0.0f;
	if (horizontal) { lp.endPoint.x = 1.0f; lp.endPoint.y = 0.0f; }
	else            { lp.endPoint.x = 0.0f; lp.endPoint.y = 1.0f; }
	ID2D1LinearGradientBrush *br = NULL;
	HRESULT hr = g->rt->CreateLinearGradientBrush(&lp, NULL, gs, &br);
	if (FAILED(hr) || !br) { SAFE_RELEASE(gs); return NULL; }
	g->gradCreates++;

	if (count > GFX_GRAD_MAX_STOPS) {
		*outStops = gs;
		*transient = 1;
		SetGradTransform(br, r);
		return br;
	}

	GfxGradientCache *slot = NULL;
	uint32_t oldest = 0xFFFFFFFFu;
	for (int i = 0; i < GFX_GRAD_CACHE_MAX; i++) {
		GfxGradientCache *e = &g->grads[i];
		if (!e->brush) { slot = e; break; }
		if (e->lastUse < oldest) { oldest = e->lastUse; slot = e; }
	}
	if (slot->brush) slot->brush->Release();
	if (slot->stops) slot->stops->Release();
	slot->count = count;
	slot->horizontal = horizontal;
	for (int k = 0; k < count; k++) {
		slot->colors[k] = colors[k];
		slot->positions[k] = positions[k];
	}
	slot->brush = br;
	slot->stops = gs;
	slot->lastUse = ++g->gradClock;
	SetGradTransform(br, r);
	return br;
}

void Gfx_FillRoundRectGradV(Gfx *g, GfxRect r, float radius, GfxColor top, GfxColor bottom)
{
	GfxColor colors[2] = { top, bottom };
	float pos[2] = { 0.0f, 1.0f };
	Gfx_FillRoundRectMultiGrad(g, r, radius, colors, pos, 2, 0);
}

void Gfx_FillRoundRectGradH(Gfx *g, GfxRect r, float radius, GfxColor left, GfxColor right)
{
	GfxColor colors[2] = { left, right };
	float pos[2] = { 0.0f, 1.0f };
	Gfx_FillRoundRectMultiGrad(g, r, radius, colors, pos, 2, 1);
}

void Gfx_FillRoundRectMultiGrad(Gfx *g, GfxRect r, float radius,
	const GfxColor *colors, const float *positions, int count, int horizontal)
{
	if (!g || !g->rt || r.w <= 0.0f || r.h <= 0.0f || !IsVisible(g, r)) return;
	ID2D1GradientStopCollection *gs = NULL;
	int transient = 0;
	ID2D1LinearGradientBrush *br = GetGradientBrush(g, r, colors, positions, count,
		horizontal, &gs, &transient);
	if (!br) return;
	FillRounded(g, r, radius, br);
	if (transient) { SAFE_RELEASE(br); SAFE_RELEASE(gs); }
}

/* ── 四角独立圆角 ─────────────────────────────────────────────────── */

static void corner_clamp(GfxRect r, GfxCorners *c)
{
	float maxR;
	if (!c) return;
	maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
	if (maxR < 0.0f) maxR = 0.0f;
	if (c->tl < 0.0f) c->tl = 0.0f;
	if (c->tr < 0.0f) c->tr = 0.0f;
	if (c->br < 0.0f) c->br = 0.0f;
	if (c->bl < 0.0f) c->bl = 0.0f;
	if (c->tl > maxR) c->tl = maxR;
	if (c->tr > maxR) c->tr = maxR;
	if (c->br > maxR) c->br = maxR;
	if (c->bl > maxR) c->bl = maxR;
}

static void corner_add_arc(ID2D1GeometrySink *sink, float x, float y, float r)
{
	D2D1_ARC_SEGMENT a;
	a.point = D2D1::Point2F(x, y);
	a.size = D2D1::SizeF(r, r);
	a.rotationAngle = 0.0f;
	a.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
	a.arcSize = D2D1_ARC_SIZE_SMALL;
	sink->AddArc(&a);
}

static ID2D1PathGeometry *corner_geometry(Gfx *g, GfxRect r, GfxCorners c)
{
	int wq, hq, tlq, trq, brq, blq;
	GfxCornerCache *slot = NULL;
	uint32_t oldest = 0xFFFFFFFFu;
	ID2D1PathGeometry *geo = NULL;
	ID2D1GeometrySink *sink = NULL;
	int i;
	float w, h;

	if (!g || !g->d2d || r.w <= 0.0f || r.h <= 0.0f) return NULL;
	corner_clamp(r, &c);
	wq = (int)(r.w * 4.0f + 0.5f);
	hq = (int)(r.h * 4.0f + 0.5f);
	tlq = (int)(c.tl * 4.0f + 0.5f);
	trq = (int)(c.tr * 4.0f + 0.5f);
	brq = (int)(c.br * 4.0f + 0.5f);
	blq = (int)(c.bl * 4.0f + 0.5f);
	for (i = 0; i < GFX_CORNER_CACHE_MAX; i++) {
		GfxCornerCache *e = &g->corners[i];
		if (e->geo && e->wq == wq && e->hq == hq &&
			e->tlq == tlq && e->trq == trq && e->brq == brq && e->blq == blq) {
			e->lastUse = ++g->cornerClock;
			return e->geo;
		}
	}
	for (i = 0; i < GFX_CORNER_CACHE_MAX; i++) {
		GfxCornerCache *e = &g->corners[i];
		if (!e->geo) { slot = e; break; }
		if (e->lastUse < oldest) { oldest = e->lastUse; slot = e; }
	}
	if (!slot) return NULL;
	if (FAILED(g->d2d->CreatePathGeometry(&geo)) || !geo) return NULL;
	if (FAILED(geo->Open(&sink)) || !sink) {
		SAFE_RELEASE(geo);
		return NULL;
	}
	w = r.w;
	h = r.h;
	sink->BeginFigure(D2D1::Point2F(c.tl, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
	if (c.tr > 0.0f) {
		sink->AddLine(D2D1::Point2F(w - c.tr, 0.0f));
		corner_add_arc(sink, w, c.tr, c.tr);
	} else {
		sink->AddLine(D2D1::Point2F(w, 0.0f));
	}
	if (c.br > 0.0f) {
		sink->AddLine(D2D1::Point2F(w, h - c.br));
		corner_add_arc(sink, w - c.br, h, c.br);
	} else {
		sink->AddLine(D2D1::Point2F(w, h));
	}
	if (c.bl > 0.0f) {
		sink->AddLine(D2D1::Point2F(c.bl, h));
		corner_add_arc(sink, 0.0f, h - c.bl, c.bl);
	} else {
		sink->AddLine(D2D1::Point2F(0.0f, h));
	}
	if (c.tl > 0.0f) {
		sink->AddLine(D2D1::Point2F(0.0f, c.tl));
		corner_add_arc(sink, c.tl, 0.0f, c.tl);
	} else {
		sink->AddLine(D2D1::Point2F(0.0f, 0.0f));
	}
	sink->EndFigure(D2D1_FIGURE_END_CLOSED);
	HRESULT hr = sink->Close();
	SAFE_RELEASE(sink);
	if (FAILED(hr)) { SAFE_RELEASE(geo); return NULL; }

	if (slot->geo) slot->geo->Release();
	slot->wq = wq; slot->hq = hq;
	slot->tlq = tlq; slot->trq = trq; slot->brq = brq; slot->blq = blq;
	slot->lastUse = ++g->cornerClock;
	slot->geo = geo;
	return geo;
}

static void corner_set_origin(Gfx *g, GfxRect r, D2D1_MATRIX_3X2_F *saved)
{
	D2D1_MATRIX_3X2_F cur;
	if (!g || !g->rt || !saved) return;
	g->rt->GetTransform(&cur);
	*saved = cur;
	{
		D2D1::Matrix3x2F base(cur._11, cur._12, cur._21, cur._22, cur._31, cur._32);
		D2D1::Matrix3x2F m = D2D1::Matrix3x2F::Translation(r.x, r.y) * base;
		g->rt->SetTransform(&m);
	}
}

void Gfx_FillRoundRectCorners(Gfx *g, GfxRect r, GfxCorners c, GfxColor color)
{
	ID2D1PathGeometry *geo;
	ID2D1SolidColorBrush *br;
	D2D1_MATRIX_3X2_F saved;
	if (!g || !g->rt || !IsVisible(g, r)) return;
	geo = corner_geometry(g, r, c);
	br = GetBrush(g, color);
	if (!geo || !br) {
		float maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
		Gfx_FillRoundRect(g, r, maxR, color);
		return;
	}
	corner_set_origin(g, r, &saved);
	g->rt->FillGeometry(geo, br);
	g->rt->SetTransform(&saved);
}

void Gfx_FillRoundRectCornersGradV(Gfx *g, GfxRect r, GfxCorners c,
	GfxColor top, GfxColor bottom)
{
	ID2D1PathGeometry *geo;
	ID2D1GradientStopCollection *gs = NULL;
	ID2D1LinearGradientBrush *br;
	GfxColor colors[2];
	float pos[2] = { 0.0f, 1.0f };
	GfxRect local = { 0.0f, 0.0f, r.w, r.h };
	D2D1_MATRIX_3X2_F saved;
	int transient = 0;
	if (!g || !g->rt || !IsVisible(g, r)) return;
	geo = corner_geometry(g, r, c);
	colors[0] = top; colors[1] = bottom;
	br = GetGradientBrush(g, local, colors, pos, 2, 0, &gs, &transient);
	if (!geo || !br) {
		float maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
		if (transient) { SAFE_RELEASE(br); SAFE_RELEASE(gs); }
		Gfx_FillRoundRectGradV(g, r, maxR, top, bottom);
		return;
	}
	corner_set_origin(g, r, &saved);
	g->rt->FillGeometry(geo, br);
	g->rt->SetTransform(&saved);
	if (transient) { SAFE_RELEASE(br); SAFE_RELEASE(gs); }
}

static void corner_stroke(Gfx *g, GfxRect r, GfxCorners c, GfxColor color,
	float width, int dashed)
{
	ID2D1PathGeometry *geo;
	ID2D1SolidColorBrush *br;
	D2D1_MATRIX_3X2_F saved;
	if (!g || !g->rt || !IsVisible(g, r) || width <= 0.0f) return;
	geo = corner_geometry(g, r, c);
	br = GetBrush(g, color);
	if (!geo || !br) {
		float maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
		Gfx_StrokeRoundRect(g, r, maxR, color, width);
		return;
	}
	corner_set_origin(g, r, &saved);
	g->rt->DrawGeometry(geo, br, width, dashed ? g->dashStroke : g->roundStroke);
	g->rt->SetTransform(&saved);
}

void Gfx_StrokeRoundRectCorners(Gfx *g, GfxRect r, GfxCorners c,
	GfxColor color, float width)
{
	corner_stroke(g, r, c, color, width, 0);
}

void Gfx_StrokeRoundRectCornersDashed(Gfx *g, GfxRect r, GfxCorners c,
	GfxColor color, float width)
{
	corner_stroke(g, r, c, color, width, 1);
}

void Gfx_StrokeRoundRectOpen(Gfx *g, GfxRect r, GfxCorners c,
	unsigned edgeMask, GfxColor color, float width)
{
	float maxR, tl, tr, br, bl;
	unsigned top, right, bottom, left;
	if (!g || !g->rt || r.w <= 0.0f || r.h <= 0.0f || width <= 0.0f) return;
	if (edgeMask == 0 || edgeMask == 15u) {
		Gfx_StrokeRoundRectCorners(g, r, c, color, width);
		return;
	}
	maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
	tl = c.tl < 0.0f ? 0.0f : (c.tl > maxR ? maxR : c.tl);
	tr = c.tr < 0.0f ? 0.0f : (c.tr > maxR ? maxR : c.tr);
	br = c.br < 0.0f ? 0.0f : (c.br > maxR ? maxR : c.br);
	bl = c.bl < 0.0f ? 0.0f : (c.bl > maxR ? maxR : c.bl);
	top = edgeMask & 1u;
	right = edgeMask & 2u;
	bottom = edgeMask & 4u;
	left = edgeMask & 8u;

	if (tl > 0.0f && top && left)
		Gfx_StrokeArc(g, r.x + tl, r.y + tl, tl, 180.0f, 90.0f, color, width);
	if (tr > 0.0f && top && right)
		Gfx_StrokeArc(g, r.x + r.w - tr, r.y + tr, tr, 270.0f, 90.0f, color, width);
	if (br > 0.0f && bottom && right)
		Gfx_StrokeArc(g, r.x + r.w - br, r.y + r.h - br, br, 0.0f, 90.0f, color, width);
	if (bl > 0.0f && bottom && left)
		Gfx_StrokeArc(g, r.x + bl, r.y + r.h - bl, bl, 90.0f, 90.0f, color, width);

	if (top) {
		float x0 = (left ? r.x + tl : r.x);
		float x1 = (right ? r.x + r.w - tr : r.x + r.w);
		if (x1 > x0) Gfx_Line(g, x0, r.y, x1, r.y, color, width);
	}
	if (bottom) {
		float x0 = (left ? r.x + bl : r.x);
		float x1 = (right ? r.x + r.w - br : r.x + r.w);
		if (x1 > x0) Gfx_Line(g, x0, r.y + r.h, x1, r.y + r.h, color, width);
	}
	if (left) {
		float y0 = (top ? r.y + tl : r.y);
		float y1 = (bottom ? r.y + r.h - bl : r.y + r.h);
		if (y1 > y0) Gfx_Line(g, r.x, y0, r.x, y1, color, width);
	}
	if (right) {
		float y0 = (top ? r.y + tr : r.y);
		float y1 = (bottom ? r.y + r.h - br : r.y + r.h);
		if (y1 > y0) Gfx_Line(g, r.x + r.w, y0, r.x + r.w, y1, color, width);
	}
}

void Gfx_FillEllipse(Gfx *g, float cx, float cy, float rx, float ry, GfxColor c)
{
	if (!g || !g->rt) return;
	GfxRect bb = { cx - rx, cy - ry, rx * 2.0f, ry * 2.0f };
	if (!IsVisible(g, bb)) return;
	ID2D1SolidColorBrush *br = GetBrush(g, c);
	if (!br) return;
	D2D1_ELLIPSE el = { { cx, cy }, rx, ry };
	g->rt->FillEllipse(&el, br);
}

void Gfx_FillEllipseGradV(Gfx *g, float cx, float cy, float r, GfxColor top, GfxColor bottom)
{
	if (!g || !g->rt || r <= 0.0f) return;
	GfxRect bb = { cx - r, cy - r, r * 2.0f, r * 2.0f };
	if (!IsVisible(g, bb)) return;
	GfxColor colors[2] = { top, bottom };
	float pos[2] = { 0.0f, 1.0f };
	GfxRect rc = { cx - r, cy - r, r * 2.0f, r * 2.0f };
	ID2D1GradientStopCollection *gs = NULL;
	int transient = 0;
	ID2D1LinearGradientBrush *br = GetGradientBrush(g, rc, colors, pos, 2, 0, &gs, &transient);
	if (!br) return;
	D2D1_ELLIPSE el = { { cx, cy }, r, r };
	g->rt->FillEllipse(&el, br);
	if (transient) { SAFE_RELEASE(br); SAFE_RELEASE(gs); }
}

void Gfx_StrokeRoundRect(Gfx *g, GfxRect r, float radius, GfxColor c, float width)
{
	if (!g || !g->rt || r.w <= 0.0f || r.h <= 0.0f || !IsVisible(g, r)) return;
	ID2D1SolidColorBrush *br = GetBrush(g, c);
	if (!br) return;
	float maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
	if (radius < 0.0f) radius = 0.0f;
	if (radius > maxR) radius = maxR;
	D2D1_ROUNDED_RECT rr;
	rr.rect.left = r.x; rr.rect.top = r.y;
	rr.rect.right = r.x + r.w; rr.rect.bottom = r.y + r.h;
	rr.radiusX = radius; rr.radiusY = radius;
	g->rt->DrawRoundedRectangle(&rr, br, width, g->roundStroke);
}

void Gfx_StrokeEllipse(Gfx *g, float cx, float cy, float r, GfxColor c, float width)
{
	if (!g || !g->rt || r <= 0.0f) return;
	GfxRect bb = { cx - r - width, cy - r - width,
		(r + width) * 2.0f, (r + width) * 2.0f };
	if (!IsVisible(g, bb)) return;
	ID2D1SolidColorBrush *br = GetBrush(g, c);
	if (!br) return;
	D2D1_ELLIPSE el = { { cx, cy }, r, r };
	g->rt->DrawEllipse(&el, br, width, g->roundStroke);
}

void Gfx_Line(Gfx *g, float x1, float y1, float x2, float y2, GfxColor c, float width)
{
	if (!g || !g->rt) return;
	float l = (x1 < x2 ? x1 : x2) - width;
	float t = (y1 < y2 ? y1 : y2) - width;
	float rr = (x1 > x2 ? x1 : x2) + width;
	float bb2 = (y1 > y2 ? y1 : y2) + width;
	GfxRect bb = { l, t, rr - l, bb2 - t };
	if (!IsVisible(g, bb)) return;
	ID2D1SolidColorBrush *br = GetBrush(g, c);
	if (!br) return;
	g->rt->DrawLine(D2D1_POINT_2F{ x1, y1 }, D2D1_POINT_2F{ x2, y2 }, br, width, g->roundStroke);
}

void Gfx_StrokeArc(Gfx *g, float cx, float cy, float r, float startDeg, float sweepDeg,
	GfxColor c, float width)
{
	Gfx_StrokeArcStyle(g, cx, cy, r, startDeg, sweepDeg, c, width, NULL);
}

void Gfx_StrokeArcStyle(Gfx *g, float cx, float cy, float r, float startDeg,
	float sweepDeg, GfxColor c, float width, const GfxStrokeStyle *st)
{
	if (!g || !g->rt || r <= 0.0f || sweepDeg == 0.0f) return;
	GfxRect bb = { cx - r - width, cy - r - width,
		(r + width) * 2.0f, (r + width) * 2.0f };
	if (!IsVisible(g, bb)) return;
	ID2D1SolidColorBrush *br = GetBrush(g, c);
	if (!br) return;
	ID2D1PathGeometry *geo = NULL;
	ID2D1GeometrySink *sink = NULL;
	if (FAILED(g->d2d->CreatePathGeometry(&geo))) return;
	if (FAILED(geo->Open(&sink))) { SAFE_RELEASE(geo); return; }

	float absSweep = sweepDeg < 0.0f ? -sweepDeg : sweepDeg;
	int steps = (int)(absSweep / 2.0f);
	if (steps < 2) steps = 2;
	if (steps > 360) steps = 360;
	float step = sweepDeg / (float)steps;
	sink->BeginFigure(D2D1_POINT_2F{ cx + r * CosDeg(startDeg), cy + r * SinDeg(startDeg) },
		D2D1_FIGURE_BEGIN_HOLLOW);
	for (int i = 1; i <= steps; i++) {
		float a = startDeg + step * (float)i;
		sink->AddLine(D2D1_POINT_2F{ cx + r * CosDeg(a), cy + r * SinDeg(a) });
	}
	sink->EndFigure(D2D1_FIGURE_END_OPEN);
	sink->Close();
	g->rt->DrawGeometry(geo, br, width, GetPathStroke(g, st));
	SAFE_RELEASE(sink);
	SAFE_RELEASE(geo);
}

void Gfx_FillTriangle(Gfx *g, float x1, float y1, float x2, float y2, float x3, float y3,
	GfxColor c)
{
	if (!g || !g->rt) return;
	float l = x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3);
	float t = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
	float rr = x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3);
	float bb2 = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);
	GfxRect bb = { l, t, rr - l, bb2 - t };
	if (!IsVisible(g, bb)) return;
	ID2D1SolidColorBrush *br = GetBrush(g, c);
	if (!br) return;
	ID2D1PathGeometry *geo = NULL;
	ID2D1GeometrySink *sink = NULL;
	if (FAILED(g->d2d->CreatePathGeometry(&geo))) return;
	if (FAILED(geo->Open(&sink))) { SAFE_RELEASE(geo); return; }
	sink->BeginFigure(D2D1_POINT_2F{ x1, y1 }, D2D1_FIGURE_BEGIN_FILLED);
	sink->AddLine(D2D1_POINT_2F{ x2, y2 });
	sink->AddLine(D2D1_POINT_2F{ x3, y3 });
	sink->EndFigure(D2D1_FIGURE_END_CLOSED);
	sink->Close();
	g->rt->FillGeometry(geo, br);
	SAFE_RELEASE(sink);
	SAFE_RELEASE(geo);
}

/* ── 路径 / 描边样式 / 画刷 ───────────────────────────────────────── */

enum { PATH_MOVE, PATH_LINE, PATH_CUBIC, PATH_QUAD, PATH_ARC, PATH_CLOSE };

#define GFX_PATH_ARGS 7

typedef struct {
	uint8_t op;
	float a[GFX_PATH_ARGS];
} GfxPathCmd;

struct GfxPath {
	GfxPathCmd *cmds;
	int count, cap;
	ID2D1PathGeometry *geo;
	int built;
	int evenOdd;
	int stage;   /* 构建失败诊断: 1=create 2=open 3=close */
};

struct GfxBrush {
	int kind;
	GfxColor color;
	float bx, by, bw, bh;   /* 渐变框 (元素局部) */
	float angle;
	float rcx, rcy, rr;     /* RADIAL: 归一化中心/半径 */
	GfxGradStop *stops;
	int stopCount;
};

static void PathInvalidate(GfxPath *p)
{
	if (!p || !p->built) return;
	SAFE_RELEASE(p->geo);
	p->built = 0;
}

static GfxPathCmd *PathPush(GfxPath *p, uint8_t op)
{
	GfxPathCmd *c;
	if (!p) return NULL;
	if (p->count >= p->cap) {
		int cap = p->cap ? p->cap * 2 : 32;
		/* HeapReAlloc 不接受 NULL (与 POSIX realloc 不同), 首块单独 HeapAlloc. */
		GfxPathCmd *n = p->cmds
			? (GfxPathCmd *)HeapReAlloc(GetProcessHeap(), 0,
				p->cmds, (SIZE_T)cap * sizeof(GfxPathCmd))
			: (GfxPathCmd *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)cap * sizeof(GfxPathCmd));
		if (!n) return NULL;
		p->cmds = n;
		p->cap = cap;
	}
	PathInvalidate(p);
	c = &p->cmds[p->count++];
	ZeroMemory(c, sizeof(*c));
	c->op = op;
	return c;
}

GfxPath *Gfx_PathCreate(Gfx *g)
{
	(void)g;
	GfxPath *p = (GfxPath *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxPath));
	return p;
}

void Gfx_PathReset(GfxPath *p)
{
	if (!p) return;
	PathInvalidate(p);
	p->count = 0;
}

void Gfx_PathSetFillRule(GfxPath *p, int evenOdd)
{
	if (!p) return;
	evenOdd = evenOdd ? 1 : 0;
	if (p->evenOdd == evenOdd) return;
	p->evenOdd = evenOdd;
	PathInvalidate(p);
}

void Gfx_PathDestroy(Gfx *g, GfxPath *p)
{
	(void)g;
	if (!p) return;
	SAFE_RELEASE(p->geo);
	HeapFree(GetProcessHeap(), 0, p->cmds);
	HeapFree(GetProcessHeap(), 0, p);
}

void Gfx_PathMoveTo(GfxPath *p, float x, float y)
{
	GfxPathCmd *c = PathPush(p, PATH_MOVE);
	if (c) { c->a[0] = x; c->a[1] = y; }
}

void Gfx_PathLineTo(GfxPath *p, float x, float y)
{
	GfxPathCmd *c = PathPush(p, PATH_LINE);
	if (c) { c->a[0] = x; c->a[1] = y; }
}

void Gfx_PathCubicTo(GfxPath *p, float x1, float y1, float x2, float y2,
	float x, float y)
{
	GfxPathCmd *c = PathPush(p, PATH_CUBIC);
	if (c) {
		c->a[0] = x1; c->a[1] = y1;
		c->a[2] = x2; c->a[3] = y2;
		c->a[4] = x;  c->a[5] = y;
	}
}

void Gfx_PathQuadTo(GfxPath *p, float x1, float y1, float x, float y)
{
	GfxPathCmd *c = PathPush(p, PATH_QUAD);
	if (c) {
		c->a[0] = x1; c->a[1] = y1;
		c->a[2] = x;  c->a[3] = y;
	}
}

void Gfx_PathArcTo(GfxPath *p, float rx, float ry, float rotDeg,
	int largeArc, int sweep, float x, float y)
{
	GfxPathCmd *c = PathPush(p, PATH_ARC);
	if (c) {
		c->a[0] = rx; c->a[1] = ry; c->a[2] = rotDeg;
		c->a[3] = largeArc ? 1.0f : 0.0f;
		c->a[4] = sweep ? 1.0f : 0.0f;
		c->a[5] = x; c->a[6] = y;
	}
}

void Gfx_PathClose(GfxPath *p)
{
	PathPush(p, PATH_CLOSE);
}

/* 惰性构建: 只有命令变化后的第一次绘制才重建几何. 空路径返回 NULL. */
static ID2D1PathGeometry *PathBuild(Gfx *g, GfxPath *p)
{
	ID2D1PathGeometry *geo = NULL;
	ID2D1GeometrySink *sink = NULL;
	float cx = 0.0f, cy = 0.0f;
	int open = 0;

	if (!p) return NULL;
	if (p->built) return p->geo;
	p->built = 1;
	p->stage = 0;
	if (!g || !g->d2d || p->count <= 0) return NULL;
	if (FAILED(g->d2d->CreatePathGeometry(&geo)) || !geo) { p->stage = 1; return NULL; }
	if (FAILED(geo->Open(&sink)) || !sink) { p->stage = 2; SAFE_RELEASE(geo); return NULL; }
	sink->SetFillMode(p->evenOdd ? D2D1_FILL_MODE_ALTERNATE
		: D2D1_FILL_MODE_WINDING);

	for (int i = 0; i < p->count; i++) {
		const GfxPathCmd *c = &p->cmds[i];
		switch (c->op) {
		case PATH_MOVE:
			if (open) { sink->EndFigure(D2D1_FIGURE_END_OPEN); open = 0; }
			cx = c->a[0]; cy = c->a[1];
			sink->BeginFigure(D2D1_POINT_2F{ cx, cy }, D2D1_FIGURE_BEGIN_FILLED);
			open = 1;
			break;
		case PATH_LINE:
			if (!open) {
				sink->BeginFigure(D2D1_POINT_2F{ cx, cy }, D2D1_FIGURE_BEGIN_FILLED);
				open = 1;
			}
			cx = c->a[0]; cy = c->a[1];
			sink->AddLine(D2D1_POINT_2F{ cx, cy });
			break;
		case PATH_CUBIC:
			if (!open) {
				sink->BeginFigure(D2D1_POINT_2F{ cx, cy }, D2D1_FIGURE_BEGIN_FILLED);
				open = 1;
			}
			sink->AddBezier(D2D1_BEZIER_SEGMENT{
				D2D1_POINT_2F{ c->a[0], c->a[1] },
				D2D1_POINT_2F{ c->a[2], c->a[3] },
				D2D1_POINT_2F{ c->a[4], c->a[5] } });
			cx = c->a[4]; cy = c->a[5];
			break;
		case PATH_QUAD:
			if (!open) {
				sink->BeginFigure(D2D1_POINT_2F{ cx, cy }, D2D1_FIGURE_BEGIN_FILLED);
				open = 1;
			}
			sink->AddQuadraticBezier(D2D1_QUADRATIC_BEZIER_SEGMENT{
				D2D1_POINT_2F{ c->a[0], c->a[1] },
				D2D1_POINT_2F{ c->a[2], c->a[3] } });
			cx = c->a[2]; cy = c->a[3];
			break;
		case PATH_ARC: {
			float dx = c->a[5] - cx, dy = c->a[6] - cy;
			if (!open) {
				sink->BeginFigure(D2D1_POINT_2F{ cx, cy }, D2D1_FIGURE_BEGIN_FILLED);
				open = 1;
			}
			/* SVG: 端点重合的弧省略; 零半径按直线降级. */
			if (dx * dx + dy * dy < 1e-12f) break;
			if (c->a[0] <= 0.0f || c->a[1] <= 0.0f) {
				sink->AddLine(D2D1_POINT_2F{ c->a[5], c->a[6] });
			} else {
				D2D1_ARC_SEGMENT seg;
				seg.point = D2D1_POINT_2F{ c->a[5], c->a[6] };
				seg.size = D2D1_SIZE_F{ c->a[0], c->a[1] };
				seg.rotationAngle = c->a[2];
				seg.sweepDirection = c->a[4] != 0.0f
					? D2D1_SWEEP_DIRECTION_CLOCKWISE
					: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
				seg.arcSize = c->a[3] != 0.0f
					? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
				sink->AddArc(&seg);
			}
			cx = c->a[5]; cy = c->a[6];
			break;
		}
		case PATH_CLOSE:
			if (open) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); open = 0; }
			break;
		}
	}
	if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
	if (FAILED(sink->Close())) { p->stage = 3; SAFE_RELEASE(sink); SAFE_RELEASE(geo); return NULL; }
	SAFE_RELEASE(sink);
	p->geo = geo;
	return geo;
}

GfxRect Gfx_PathBounds(Gfx *g, GfxPath *p)
{
	GfxRect out = { 0.0f, 0.0f, 0.0f, 0.0f };
	ID2D1PathGeometry *geo;
	D2D1_RECT_F rc;
	if (!p || p->count <= 0) return out;
	geo = PathBuild(g, p);
	if (!geo) return out;
	if (FAILED(geo->GetBounds(NULL, &rc))) return out;
	out.x = rc.left; out.y = rc.top;
	out.w = rc.right - rc.left; out.h = rc.bottom - rc.top;
	return out;
}

static ID2D1StrokeStyle *GetPathStroke(Gfx *g, const GfxStrokeStyle *st)
{
	D2D1_STROKE_STYLE_PROPERTIES sp;
	GfxStrokeCache *slot = NULL;
	uint32_t oldest = 0xFFFFFFFFu;
	int cap, join, dashCount = 0;
	float dashes[8];

	if (!g) return NULL;
	if (!st) return g->roundStroke;
	cap = st->cap;
	if (cap < GFX_CAP_BUTT || cap > GFX_CAP_SQUARE) cap = GFX_CAP_ROUND;
	join = st->join;
	if (join < GFX_JOIN_MITER || join > GFX_JOIN_BEVEL) join = GFX_JOIN_ROUND;
	if (st->dashes && st->dashCount > 0) {
		dashCount = st->dashCount > 8 ? 8 : st->dashCount;
		for (int i = 0; i < dashCount; i++)
			dashes[i] = st->dashes[i] > 0.01f ? st->dashes[i] : 0.01f;
		if (dashCount == 1) { dashes[1] = dashes[0]; dashCount = 2; }
	}

	for (int i = 0; i < GFX_STROKE_CACHE_MAX; i++) {
		GfxStrokeCache *e = &g->strokes[i];
		if (!e->style) { if (!slot) slot = e; continue; }
		if (e->cap != cap || e->join != join || e->dashCount != dashCount)
			continue;
		float miter = st->miterLimit > 0.0f ? st->miterLimit : 4.0f;
		if (e->miter != miter) continue;
		int same = 1;
		for (int k = 0; k < dashCount; k++)
			if (e->dashes[k] != dashes[k]) { same = 0; break; }
		if (!same) continue;
		e->lastUse = ++g->strokeClock;
		return e->style;
	}
	if (!slot) {
		for (int i = 0; i < GFX_STROKE_CACHE_MAX; i++) {
			GfxStrokeCache *e = &g->strokes[i];
			if (!slot || e->lastUse < oldest) { oldest = e->lastUse; slot = e; }
		}
	}
	if (!slot) return g->roundStroke;

	ZeroMemory(&sp, sizeof(sp));
	sp.startCap = cap == GFX_CAP_ROUND ? D2D1_CAP_STYLE_ROUND
		: cap == GFX_CAP_SQUARE ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_FLAT;
	sp.endCap = sp.startCap;
	sp.dashCap = sp.startCap;
	sp.lineJoin = join == GFX_JOIN_ROUND ? D2D1_LINE_JOIN_ROUND
		: join == GFX_JOIN_BEVEL ? D2D1_LINE_JOIN_BEVEL : D2D1_LINE_JOIN_MITER;
	sp.miterLimit = st->miterLimit > 0.0f ? st->miterLimit : 4.0f;
	sp.dashStyle = dashCount > 0 ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID;

	ID2D1StrokeStyle *style = NULL;
	if (FAILED(g->d2d->CreateStrokeStyle(&sp, dashCount ? dashes : NULL,
		(UINT32)dashCount, &style)) || !style)
		return g->roundStroke;
	SAFE_RELEASE(slot->style);
	slot->style = style;
	slot->cap = cap;
	slot->join = join;
	slot->miter = sp.miterLimit;
	slot->dashCount = dashCount;
	for (int k = 0; k < dashCount; k++) slot->dashes[k] = dashes[k];
	slot->lastUse = ++g->strokeClock;
	return style;
}

GfxBrush *Gfx_SolidBrush(Gfx *g, GfxColor c)
{
	GfxBrush *b;
	(void)g;
	b = (GfxBrush *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxBrush));
	if (!b) return NULL;
	b->kind = GFX_BRUSH_SOLID;
	b->color = c;
	return b;
}

GfxBrush *Gfx_LinearGradientBrush(Gfx *g, GfxRect boxLocal, float angleDeg,
	const GfxGradStop *stops, int count)
{
	GfxBrush *b;
	(void)g;
	if (!stops || count < 2) return NULL;
	if (count > GFX_SVG_GRAD_MAX_STOPS) count = GFX_SVG_GRAD_MAX_STOPS;
	b = (GfxBrush *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxBrush));
	if (!b) return NULL;
	b->stops = (GfxGradStop *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)count * sizeof(GfxGradStop));
	if (!b->stops) { HeapFree(GetProcessHeap(), 0, b); return NULL; }
	for (int i = 0; i < count; i++) b->stops[i] = stops[i];
	b->kind = GFX_BRUSH_LINEAR;
	b->stopCount = count;
	b->bx = boxLocal.x; b->by = boxLocal.y;
	b->bw = boxLocal.w; b->bh = boxLocal.h;
	b->angle = angleDeg;
	return b;
}

GfxBrush *Gfx_RadialGradientBrush(Gfx *g, GfxRect boxLocal,
	float cx, float cy, float r, const GfxGradStop *stops, int count)
{
	GfxBrush *b;
	(void)g;
	if (!stops || count < 2) return NULL;
	if (count > GFX_SVG_GRAD_MAX_STOPS) count = GFX_SVG_GRAD_MAX_STOPS;
	b = (GfxBrush *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxBrush));
	if (!b) return NULL;
	b->stops = (GfxGradStop *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)count * sizeof(GfxGradStop));
	if (!b->stops) { HeapFree(GetProcessHeap(), 0, b); return NULL; }
	for (int i = 0; i < count; i++) b->stops[i] = stops[i];
	b->kind = GFX_BRUSH_RADIAL;
	b->stopCount = count;
	b->bx = boxLocal.x; b->by = boxLocal.y;
	b->bw = boxLocal.w; b->bh = boxLocal.h;
	b->rcx = cx; b->rcy = cy; b->rr = r;
	return b;
}

void Gfx_BrushDestroy(Gfx *g, GfxBrush *b)
{
	(void)g;
	if (!b) return;
	HeapFree(GetProcessHeap(), 0, b->stops);
	HeapFree(GetProcessHeap(), 0, b);
}

static float QuantizeF(float v, float step)
{
	return (float)(int)(v / step + (v >= 0.0f ? 0.5f : -0.5f)) * step;
}

/* 线性/径向渐变: 参数量化后缓存. 线性方向由角度决定, 起止点取包围盒
 * 在该方向上的投影; 径向按 objectBoundingBox 归一化中心/半径映射为椭圆. */
static ID2D1Brush *GetSvgGradient(Gfx *g, GfxBrush *b)
{
	GfxSvgGradCache *slot = NULL;
	uint32_t oldest = 0xFFFFFFFFu;
	float bx, by, bw, bh, ang, rcx = 0.0f, rcy = 0.0f, rr = 0.0f;
	int radial;
	D2D1_GRADIENT_STOP stops[GFX_SVG_GRAD_MAX_STOPS];
	GfxGradStop sorted[GFX_SVG_GRAD_MAX_STOPS];
	int count = b->stopCount;

	if (!g || !g->rt || !b->stops || count < 2) return NULL;
	radial = b->kind == GFX_BRUSH_RADIAL;
	bx = QuantizeF(b->bx, 0.5f); by = QuantizeF(b->by, 0.5f);
	bw = QuantizeF(b->bw, 0.5f); bh = QuantizeF(b->bh, 0.5f);
	ang = QuantizeF(b->angle, 0.1f);
	while (ang < 0.0f) ang += 360.0f;
	while (ang >= 360.0f) ang -= 360.0f;
	if (radial) {
		rcx = QuantizeF(b->rcx, 0.001f);
		rcy = QuantizeF(b->rcy, 0.001f);
		rr = QuantizeF(b->rr, 0.001f);
	}

	for (int i = 0; i < GFX_SVG_GRAD_CACHE_MAX; i++) {
		GfxSvgGradCache *e = &g->svgGrads[i];
		if (!e->brush) { if (!slot) slot = e; continue; }
		if (e->bx != bx || e->by != by || e->bw != bw || e->bh != bh ||
			e->angle != ang || e->count != count || e->radial != radial ||
			e->rcx != rcx || e->rcy != rcy || e->rr != rr)
			continue;
		int same = 1;
		for (int k = 0; k < count; k++) {
			if (e->colors[k] != b->stops[k].color ||
				e->positions[k] != b->stops[k].offset) { same = 0; break; }
		}
		if (!same) continue;
		e->lastUse = ++g->svgGradClock;
		return e->brush;
	}
	if (!slot) {
		for (int i = 0; i < GFX_SVG_GRAD_CACHE_MAX; i++) {
			GfxSvgGradCache *e = &g->svgGrads[i];
			if (!slot || e->lastUse < oldest) { oldest = e->lastUse; slot = e; }
		}
	}
	if (!slot) return NULL;

	/* 按 offset 稳定排序 (色标数量很小). */
	for (int i = 0; i < count; i++) sorted[i] = b->stops[i];
	for (int i = 1; i < count; i++) {
		GfxGradStop key = sorted[i];
		int j = i - 1;
		while (j >= 0 && sorted[j].offset > key.offset) {
			sorted[j + 1] = sorted[j];
			j--;
		}
		sorted[j + 1] = key;
	}
	for (int i = 0; i < count; i++) {
		float o = sorted[i].offset;
		if (o < 0.0f) o = 0.0f;
		if (o > 1.0f) o = 1.0f;
		stops[i].position = o;
		stops[i].color = ToColor(sorted[i].color);
	}
	ID2D1GradientStopCollection *gs = NULL;
	if (FAILED(g->rt->CreateGradientStopCollection(stops, (UINT32)count,
		D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &gs)) || !gs)
		return NULL;

	ID2D1Brush *br = NULL;
	if (radial) {
		D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp;
		rp.center = D2D1_POINT_2F{ bx + bw * rcx, by + bh * rcy };
		rp.gradientOriginOffset = D2D1_POINT_2F{ 0.0f, 0.0f };
		rp.radiusX = bw * rr;
		rp.radiusY = bh * rr;
		if (rp.radiusX < 0.01f) rp.radiusX = 0.01f;
		if (rp.radiusY < 0.01f) rp.radiusY = 0.01f;
		ID2D1RadialGradientBrush *rb = NULL;
		if (FAILED(g->rt->CreateRadialGradientBrush(&rp, NULL, gs, &rb)) || !rb) {
			SAFE_RELEASE(gs);
			return NULL;
		}
		br = rb;
	} else {
		float dx = CosDeg(ang), dy = SinDeg(ang);
		float cx = bx + bw * 0.5f, cy = by + bh * 0.5f;
		float half = (bw * (dx < 0.0f ? -dx : dx) + bh * (dy < 0.0f ? -dy : dy)) * 0.5f;
		D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp;
		lp.startPoint = D2D1_POINT_2F{ cx - dx * half, cy - dy * half };
		lp.endPoint = D2D1_POINT_2F{ cx + dx * half, cy + dy * half };
		ID2D1LinearGradientBrush *lb = NULL;
		if (FAILED(g->rt->CreateLinearGradientBrush(&lp, NULL, gs, &lb)) || !lb) {
			SAFE_RELEASE(gs);
			return NULL;
		}
		br = lb;
	}
	SAFE_RELEASE(slot->brush);
	SAFE_RELEASE(slot->stops);
	slot->bx = bx; slot->by = by; slot->bw = bw; slot->bh = bh;
	slot->angle = ang;
	slot->radial = radial;
	slot->rcx = rcx; slot->rcy = rcy; slot->rr = rr;
	slot->count = count;
	for (int k = 0; k < count; k++) {
		slot->colors[k] = b->stops[k].color;
		slot->positions[k] = b->stops[k].offset;
	}
	slot->brush = br;
	slot->stops = gs;
	slot->lastUse = ++g->svgGradClock;
	return br;
}

void Gfx_FillPath(Gfx *g, GfxPath *p, GfxColor c)
{
	ID2D1PathGeometry *geo;
	ID2D1SolidColorBrush *br;
	if (!g || !g->rt) return;
	geo = PathBuild(g, p);
	if (!geo) return;
	br = GetBrush(g, c);
	if (!br) return;
	g->rt->FillGeometry(geo, br);
}

void Gfx_FillPathBrush(Gfx *g, GfxPath *p, GfxBrush *brush)
{
	ID2D1PathGeometry *geo;
	if (!g || !g->rt || !brush) return;
	geo = PathBuild(g, p);
	if (!geo) return;
	if (brush->kind == GFX_BRUSH_LINEAR || brush->kind == GFX_BRUSH_RADIAL) {
		ID2D1Brush *br = GetSvgGradient(g, brush);
		if (br) g->rt->FillGeometry(geo, br);
	} else {
		ID2D1SolidColorBrush *br = GetBrush(g, brush->color);
		if (br) g->rt->FillGeometry(geo, br);
	}
}

void Gfx_StrokePath(Gfx *g, GfxPath *p, GfxBrush *brush, float width,
	const GfxStrokeStyle *st)
{
	ID2D1PathGeometry *geo;
	ID2D1StrokeStyle *style;
	if (!g || !g->rt || width <= 0.0f || !brush) return;
	geo = PathBuild(g, p);
	if (!geo) return;
	style = GetPathStroke(g, st);
	if (brush->kind == GFX_BRUSH_LINEAR || brush->kind == GFX_BRUSH_RADIAL) {
		ID2D1Brush *br = GetSvgGradient(g, brush);
		if (br) g->rt->DrawGeometry(geo, br, width, style);
	} else {
		ID2D1SolidColorBrush *br = GetBrush(g, brush->color);
		if (br) g->rt->DrawGeometry(geo, br, width, style);
	}
}

void Gfx_PushClip(Gfx *g, GfxRect r)
{
	if (!g || !g->rt) return;
	D2D1_RECT_F rc = { r.x, r.y, r.x + r.w, r.y + r.h };
	g->rt->PushAxisAlignedClip(&rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->clipDepth++;
}

void Gfx_PopClip(Gfx *g)
{
	if (!g || !g->rt || g->clipDepth <= 0) return;
	g->rt->PopAxisAlignedClip();
	g->clipDepth--;
}

void Gfx_PushRotate(Gfx *g, float cx, float cy, float deg)
{
	D2D1_MATRIX_3X2_F cur, rot, m;
	if (!g || !g->rt) return;
	if (g->xformDepth >= (int)(sizeof(g->xformStack) / sizeof(g->xformStack[0])))
		return;
	g->rt->GetTransform(&cur);
	g->xformStack[g->xformDepth++] = cur;
	/* 行向量: 先旋转(DIP) 再乘当前变换(含 DPI 缩放). */
	rot = D2D1::Matrix3x2F::Rotation(deg, D2D1::Point2F(cx, cy));
	m = rot * cur;
	g->rt->SetTransform(&m);
}

/* 2×3 仿射 (a b c d e f): x' = a*x + c*y + e; y' = b*x + d*y + f.
 * 与 PushRotate/PushScaleTranslate 共用栈与 Gfx_PopTransform. */
void Gfx_PushMatrix(Gfx *g, const float m[6])
{
	D2D1_MATRIX_3X2_F cur, tm, out;
	if (!g || !g->rt || !m) return;
	if (g->xformDepth >= (int)(sizeof(g->xformStack) / sizeof(g->xformStack[0])))
		return;
	g->rt->GetTransform(&cur);
	g->xformStack[g->xformDepth++] = cur;
	tm._11 = m[0]; tm._12 = m[1];
	tm._21 = m[2]; tm._22 = m[3];
	tm._31 = m[4]; tm._32 = m[5];
	out = tm * cur;
	g->rt->SetTransform(&out);
}

void Gfx_PopTransform(Gfx *g)
{
	if (!g || !g->rt || g->xformDepth <= 0) return;
	g->rt->SetTransform(&g->xformStack[--g->xformDepth]);
}

void Gfx_PushScaleTranslate(Gfx *g, float scale, float tx, float ty)
{
	D2D1_MATRIX_3X2_F cur, st, m;
	if (!g || !g->rt) return;
	if (g->xformDepth >= (int)(sizeof(g->xformStack) / sizeof(g->xformStack[0])))
		return;
	g->rt->GetTransform(&cur);
	g->xformStack[g->xformDepth++] = cur;
	/* 行向量: 先 Scale*Translate (世界 -> DIP) 再乘当前变换 (含 DPI 缩放). */
	st = D2D1::Matrix3x2F::Scale(D2D1::SizeF(scale, scale)) *
		D2D1::Matrix3x2F::Translation(tx, ty);
	m = st * cur;
	g->rt->SetTransform(&m);
}

static GfxFont *font_create(Gfx *g, const wchar_t *family,
	float sizeDip, int weight)
{
	GfxFont *f;
	DWRITE_FONT_WEIGHT w;
	HRESULT hr;
	if (!g || !g->dw || !family || !family[0] || sizeDip <= 0.0f) return NULL;
	f = (GfxFont *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxFont));
	if (!f) return NULL;
	w = weight >= 600 ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
	hr = g->dw->CreateTextFormat(family, NULL, w,
		DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"zh-CN", &f->fmt);
	if (FAILED(hr) || !f->fmt) { HeapFree(GetProcessHeap(), 0, f); return NULL; }
	f->fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	f->fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	f->fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	f->size = sizeDip;
	f->weight = weight;
	return f;
}

GfxFont *Gfx_CreateFontFamily(Gfx *g, const wchar_t *family,
	float sizeDip, int weight)
{
	IDWriteFontCollection *coll = NULL;
	UINT32 index = 0;
	BOOL exists = FALSE;
	HRESULT hr;
	if (!g || !g->dw || !family || !family[0] || sizeDip <= 0.0f) return NULL;
	if (FAILED(g->dw->GetSystemFontCollection(&coll, FALSE)) || !coll)
		return NULL;
	hr = coll->FindFamilyName(family, &index, &exists);
	coll->Release();
	if (FAILED(hr) || !exists) return NULL;
	return font_create(g, family, sizeDip, weight);
}

GfxFont *Gfx_CreateFont(Gfx *g, float sizeDip, int weight)
{
	return font_create(g, L"Microsoft YaHei", sizeDip, weight);
}

void Gfx_DestroyFont(Gfx *g, GfxFont *f)
{
	if (!f) return;
	if (g) {
		for (int i = 0; i < GFX_TEXT_CACHE_MAX; i++) {
			GfxTextCache *e = &g->textCache[i];
			if (e->layout && e->font == f) {
				SAFE_RELEASE(e->layout);
				e->font = NULL;
				e->text[0] = 0;
			}
		}
	}
	SAFE_RELEASE(f->fmt);
	HeapFree(GetProcessHeap(), 0, f);
}

static int TextLen(const char *s)
{
	int n = 0;
	while (s && s[n]) n++;
	return n;
}

static int TextEqual(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

static uint64_t TextHash(const char *s, GfxFont *f, uint32_t flags, int w4, int h4)
{
	uint64_t h = 1469598103934665603ULL;
	while (s && *s) {
		h ^= (uint64_t)(unsigned char)*s++;
		h *= 1099511628211ULL;
	}
	h ^= (uint64_t)(uintptr_t)f;
	h *= 1099511628211ULL;
	h ^= (uint64_t)flags;
	h *= 1099511628211ULL;
	h ^= ((uint64_t)(uint32_t)w4 << 32) | (uint32_t)h4;
	return h;
}

static IDWriteTextLayout *CreateLayout(Gfx *g, GfxFont *f, const char *utf8,
	float maxW, float maxH, uint32_t flags)
{
	if (!g || !g->dw || !f || !f->fmt || !utf8 || !utf8[0]) return NULL;
	WCHAR local[1024];
	int count = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (count <= 1) return NULL;
	WCHAR *text = count <= 1024 ? local : (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
		(size_t)count * sizeof(WCHAR));
	if (!text) return NULL;
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, text, count);
	if (maxW < 1.0f) maxW = 1.0f;
	if (maxH < 1.0f) maxH = 1.0f;
	IDWriteTextLayout *layout = NULL;
	HRESULT hr = g->dw->CreateTextLayout(text, (UINT32)(count - 1), f->fmt, maxW, maxH, &layout);
	if (text != local) HeapFree(GetProcessHeap(), 0, text);
	if (FAILED(hr) || !layout) return NULL;
	if (flags & GFX_TEXT_WRAP) layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
	if (flags & GFX_TEXT_CENTER) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	else if (flags & GFX_TEXT_RIGHT) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
	layout->SetParagraphAlignment((flags & GFX_TEXT_VCENTER)
		? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	if ((flags & GFX_TEXT_ELLIPSIS) && g->ellipsis) {
		DWRITE_TRIMMING trim;
		trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
		trim.delimiter = 0;
		trim.delimiterCount = 0;
		layout->SetTrimming(&trim, g->ellipsis);
	}
	return layout;
}

/* ---- 字形轮廓提取 (文本转路径) ---- */

class GfxOutlineSink_ : public ID2D1SimplifiedGeometrySink {
public:
	GfxPathSink *s;
	float ox, oy;
	int contours;
	LONG ref;
	GfxOutlineSink_(GfxPathSink *out, float x, float y)
		: s(out), ox(x), oy(y), contours(0), ref(1) {}
	STDMETHOD_(ULONG, AddRef)() { return (ULONG)InterlockedIncrement(&ref); }
	STDMETHOD_(ULONG, Release)() { return (ULONG)InterlockedDecrement(&ref); }
	STDMETHOD(QueryInterface)(REFIID iid, void **ppv) {
		if (!ppv) return E_POINTER;
		if (iid == __uuidof(IUnknown) ||
			iid == __uuidof(ID2D1SimplifiedGeometrySink)) {
			*ppv = this; AddRef(); return S_OK;
		}
		*ppv = NULL; return E_NOINTERFACE;
	}
	STDMETHOD_(void, SetFillMode)(D2D1_FILL_MODE) {}
	STDMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT) {}
	STDMETHOD_(void, BeginFigure)(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) {
		if (s && s->move_to) s->move_to(s->user, p.x + ox, p.y + oy);
		contours++;
	}
	STDMETHOD_(void, AddLines)(const D2D1_POINT_2F *pts, UINT32 n) {
		if (!s || !s->line_to) return;
		for (UINT32 i = 0; i < n; i++)
			s->line_to(s->user, pts[i].x + ox, pts[i].y + oy);
	}
	STDMETHOD_(void, AddBeziers)(const D2D1_BEZIER_SEGMENT *bs, UINT32 n) {
		if (!s || !s->cubic_to) return;
		for (UINT32 i = 0; i < n; i++)
			s->cubic_to(s->user, bs[i].point1.x + ox, bs[i].point1.y + oy,
				bs[i].point2.x + ox, bs[i].point2.y + oy,
				bs[i].point3.x + ox, bs[i].point3.y + oy);
	}
	STDMETHOD_(void, EndFigure)(D2D1_FIGURE_END) {
		if (s && s->close) s->close(s->user);
	}
	STDMETHOD(Close)() { return S_OK; }
};

class GfxOutlineRenderer_ : public IDWriteTextRenderer {
public:
	Gfx *g;
	GfxPathSink *s;
	int contours;
	LONG ref;
	GfxOutlineRenderer_(Gfx *gfx, GfxPathSink *sink)
		: g(gfx), s(sink), contours(0), ref(1) {}
	STDMETHOD_(ULONG, AddRef)() { return (ULONG)InterlockedIncrement(&ref); }
	STDMETHOD_(ULONG, Release)() { return (ULONG)InterlockedDecrement(&ref); }
	STDMETHOD(QueryInterface)(REFIID iid, void **ppv) {
		if (!ppv) return E_POINTER;
		if (iid == __uuidof(IUnknown) ||
			iid == __uuidof(IDWriteTextRenderer) ||
			iid == __uuidof(IDWritePixelSnapping)) {
			*ppv = this; AddRef(); return S_OK;
		}
		*ppv = NULL; return E_NOINTERFACE;
	}
	STDMETHOD(IsPixelSnappingDisabled)(void *, BOOL *disabled) {
		if (disabled) *disabled = TRUE;
		return S_OK;
	}
	STDMETHOD(GetCurrentTransform)(void *, DWRITE_MATRIX *m) {
		if (m) {
			m->m11 = 1.0f; m->m12 = 0.0f; m->m21 = 0.0f;
			m->m22 = 1.0f; m->dx = 0.0f; m->dy = 0.0f;
		}
		return S_OK;
	}
	STDMETHOD(GetPixelsPerDip)(void *, FLOAT *ppd) {
		if (ppd) *ppd = 1.0f;
		return S_OK;
	}
	STDMETHOD(DrawGlyphRun)(void *, FLOAT bx, FLOAT by,
		DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN *run,
		const DWRITE_GLYPH_RUN_DESCRIPTION *, IUnknown *) {
		if (!run || !run->fontFace) return S_OK;
		GfxOutlineSink_ sink(s, bx, by);
		run->fontFace->GetGlyphRunOutline(run->fontEmSize,
			run->glyphIndices, run->glyphAdvances, run->glyphOffsets,
			run->glyphCount, run->isSideways, (run->bidiLevel & 1) != 0, &sink);
		contours += sink.contours;
		return S_OK;
	}
	STDMETHOD(DrawUnderline)(void *, FLOAT, FLOAT,
		const DWRITE_UNDERLINE *, IUnknown *) { return S_OK; }
	STDMETHOD(DrawStrikethrough)(void *, FLOAT, FLOAT,
		const DWRITE_STRIKETHROUGH *, IUnknown *) { return S_OK; }
	STDMETHOD(DrawInlineObject)(void *, FLOAT, FLOAT,
		IDWriteInlineObject *, BOOL, BOOL, IUnknown *) { return S_OK; }
};

int Gfx_TextOutline(Gfx *g, GfxFont *f, const char *utf8, float x, float y,
	GfxPathSink *sink)
{
	IDWriteTextLayout *layout;
	GfxOutlineRenderer_ renderer(g, sink);
	if (!g || !g->dw || !f || !sink || !utf8 || !utf8[0]) return 0;
	layout = CreateLayout(g, f, utf8, 65536.0f, 65536.0f, 0);
	if (!layout) return 0;
	layout->Draw(NULL, &renderer, x, y);
	layout->Release();
	return renderer.contours;
}

/* 文本布局缓存: 颜色在绘制时用 brush 设置，布局可跨颜色复用。 */
static IDWriteTextLayout *GetLayout(Gfx *g, GfxFont *f, const char *utf8,
	float maxW, float maxH, uint32_t flags, int *transient)
{
	*transient = 0;
	if (!g || !f || !utf8 || !utf8[0]) return NULL;
	int len = TextLen(utf8);
	int cacheable = len > 0 && len < GFX_TEXT_CACHE_CHARS;
	int w4 = (int)(maxW * 4.0f + 0.5f);
	int h4 = (int)(maxH * 4.0f + 0.5f);
	uint64_t hash = TextHash(utf8, f, flags, w4, h4);

	if (cacheable) {
		for (int i = 0; i < GFX_TEXT_CACHE_MAX; i++) {
			GfxTextCache *e = &g->textCache[i];
			if (!e->layout) continue;
			if (e->hash == hash && e->font == f && e->flags == flags &&
				e->w4 == w4 && e->h4 == h4 && TextEqual(e->text, utf8)) {
				e->lastUse = ++g->textClock;
				return e->layout;
			}
		}
	}

	IDWriteTextLayout *layout = CreateLayout(g, f, utf8,
		(float)w4 / 4.0f, (float)h4 / 4.0f, flags);
	if (!layout) return NULL;
	if (!cacheable) { *transient = 1; return layout; }

	GfxTextCache *slot = NULL;
	uint32_t oldest = 0xFFFFFFFFu;
	for (int i = 0; i < GFX_TEXT_CACHE_MAX; i++) {
		GfxTextCache *e = &g->textCache[i];
		if (!e->layout) { slot = e; break; }
		if (e->lastUse < oldest) { oldest = e->lastUse; slot = e; }
	}
	if (slot->layout) slot->layout->Release();
	slot->layout = layout;
	slot->font = f;
	slot->hash = hash;
	slot->flags = flags;
	slot->w4 = w4;
	slot->h4 = h4;
	slot->lastUse = ++g->textClock;
	memcpy(slot->text, utf8, (size_t)len);
	slot->text[len] = 0;
	return layout;
}

float Gfx_TextWidth(Gfx *g, GfxFont *f, const char *utf8)
{
	if (!g || !utf8 || !utf8[0]) return 0.0f;
	int transient = 0;
	IDWriteTextLayout *layout = GetLayout(g, f, utf8, 10000.0f, 10000.0f, 0, &transient);
	if (!layout) return 0.0f;
	DWRITE_TEXT_METRICS m;
	ZeroMemory(&m, sizeof(m));
	float w = 0.0f;
	/* 编辑控件按逐字符累加宽度, 行尾空格必须计入; m.width 会裁掉
	 * trailing whitespace, 导致行宽/光标 x 比实际渲染短. */
	if (SUCCEEDED(layout->GetMetrics(&m)))
		w = m.widthIncludingTrailingWhitespace;
	if (transient) SAFE_RELEASE(layout);
	return w;
}

float Gfx_TextHeight(Gfx *g, GfxFont *f)
{
	if (!g || !f) return 0.0f;
	int transient = 0;
	IDWriteTextLayout *layout = GetLayout(g, f, "国", 10000.0f, 10000.0f, 0, &transient);
	if (!layout) return f->size * 1.3f;
	DWRITE_TEXT_METRICS m;
	ZeroMemory(&m, sizeof(m));
	float h = f->size * 1.3f;
	if (SUCCEEDED(layout->GetMetrics(&m))) h = m.height;
	if (transient) SAFE_RELEASE(layout);
	return h;
}

void Gfx_Text(Gfx *g, GfxFont *f, const char *utf8, GfxRect rc, GfxColor color, uint32_t flags)
{
	if (!g || !g->rt || !utf8 || !utf8[0] || rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (!IsVisible(g, rc)) return;
	int transient = 0;
	IDWriteTextLayout *layout = GetLayout(g, f, utf8, rc.w, rc.h, flags, &transient);
	if (!layout) return;
	ID2D1SolidColorBrush *br = GetBrush(g, color);
	if (br) g->rt->DrawTextLayout(D2D1_POINT_2F{ rc.x, rc.y }, layout, br,
		D2D1_DRAW_TEXT_OPTIONS_CLIP);
	if (transient) SAFE_RELEASE(layout);
}

GfxBitmap *Gfx_CreateBitmap(Gfx *g, int w, int h, const uint8_t *bgra)
{
	if (!g || !g->rt || w <= 0 || h <= 0 || !bgra) return NULL;
	GfxBitmap *b = (GfxBitmap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxBitmap));
	if (!b) return NULL;
	D2D1_SIZE_U size = { (UINT32)w, (UINT32)h };
	D2D1_BITMAP_PROPERTIES props;
	ZeroMemory(&props, sizeof(props));
	props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	props.dpiX = 96.0f;
	props.dpiY = 96.0f;
	HRESULT hr = g->rt->CreateBitmap(size, bgra, (UINT32)w * 4, &props, &b->bmp);
	if (FAILED(hr) || !b->bmp) { HeapFree(GetProcessHeap(), 0, b); return NULL; }
	b->w = w;
	b->h = h;
	return b;
}

void Gfx_DestroyBitmap(Gfx *g, GfxBitmap *b)
{
	(void)g;
	if (!b) return;
	SAFE_RELEASE(b->bmp);
	HeapFree(GetProcessHeap(), 0, b);
}

void Gfx_DrawBitmap(Gfx *g, GfxBitmap *b, GfxRect dst)
{
	if (!g || !g->rt || !b || !b->bmp || !IsVisible(g, dst)) return;
	D2D1_RECT_F rc = { dst.x, dst.y, dst.x + dst.w, dst.y + dst.h };
	g->rt->DrawBitmap(b->bmp, &rc, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
}

/* ── 预烘焙柔光 ─────────────────────────────────────────────────────── */

static void glow_cache_clear(Gfx *g)
{
	if (!g) return;
	for (int i = 0; i < GFX_GLOW_CACHE_MAX; i++) {
		if (g->glows[i].bmp) {
			Gfx_DestroyBitmap(g, g->glows[i].bmp);
			g->glows[i].bmp = NULL;
		}
		g->glows[i].lastUse = 0;
	}
	g->glowClock = 0;
	g->glowGen = g->generation;
}

static GfxBitmap *glow_texture_make(Gfx *g, float w, float h, float radius,
	float glowW, GfxColor color, float scale)
{
	int W, H, x, y;
	uint8_t *pix;
	GfxBitmap *bmp;
	float halfW, halfH, r, glow, cx, cy;
	int cr = (int)((color >> 16) & 0xFF);
	int cg = (int)((color >> 8) & 0xFF);
	int cb = (int)(color & 0xFF);

	if (!g || w <= 0.0f || h <= 0.0f || glowW <= 0.0f) return NULL;
	r = radius;
	if (r < 0.0f) r = 0.0f;
	if (r > (w < h ? w : h) * 0.5f) r = (w < h ? w : h) * 0.5f;
	W = (int)((w + glowW * 2.0f) * scale + 0.5f);
	H = (int)((h + glowW * 2.0f) * scale + 0.5f);
	if (W < 2 || H < 2 || W > 2048 || H > 2048) return NULL;

	pix = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)W * H * 4);
	if (!pix) return NULL;
	halfW = w * scale * 0.5f;
	halfH = h * scale * 0.5f;
	r *= scale;
	glow = glowW * scale;
	cx = (float)W * 0.5f;
	cy = (float)H * 0.5f;

	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			float px = (float)x + 0.5f - cx;
			float py = (float)y + 0.5f - cy;
			float qx = (px < 0.0f ? -px : px) - (halfW - r);
			float qy = (py < 0.0f ? -py : py) - (halfH - r);
			float mx = qx > 0.0f ? qx : 0.0f;
			float my = qy > 0.0f ? qy : 0.0f;
			float mn = qx > qy ? qx : qy;
			float d, t, a;
			int ai, o = (y * W + x) * 4;
			if (mn > 0.0f) mn = 0.0f;
			d = (float)sqrt((double)(mx * mx + my * my)) + mn - r;
			if (d <= 0.0f || d >= glow) {
				pix[o] = pix[o + 1] = pix[o + 2] = pix[o + 3] = 0;
				continue;
			}
			t = 1.0f - d / glow;
			a = t * t * (3.0f - 2.0f * t) * 0.62f;
			ai = (int)(a * 255.0f + 0.5f);
			if (ai < 0) ai = 0;
			if (ai > 255) ai = 255;
			pix[o + 0] = (uint8_t)((cb * ai + 127) / 255);
			pix[o + 1] = (uint8_t)((cg * ai + 127) / 255);
			pix[o + 2] = (uint8_t)((cr * ai + 127) / 255);
			pix[o + 3] = (uint8_t)ai;
		}
	}
	bmp = Gfx_CreateBitmap(g, W, H, pix);
	HeapFree(GetProcessHeap(), 0, pix);
	return bmp;
}

int Gfx_DrawGlowRect(Gfx *g, GfxRect rc, float radius, float glowW,
	GfxColor color, float intensity)
{
	GfxGlowCache *slot = NULL;
	uint32_t oldest = 0xFFFFFFFFu;
	int wq, hq, rq, gq, sq, i;
	float opacity;
	GfxRect dst;

	if (!g || !g->rt) return 0;
	if (intensity <= 0.004f || glowW <= 0.0f) return 1;
	if (((color >> 24) & 0xFF) == 0) return 1;
	dst.x = rc.x - glowW;
	dst.y = rc.y - glowW;
	dst.w = rc.w + glowW * 2.0f;
	dst.h = rc.h + glowW * 2.0f;
	if (dst.w <= 0.0f || dst.h <= 0.0f) return 1;
	if (!IsVisible(g, dst)) return 1;

	if (g->glowGen != g->generation) glow_cache_clear(g);

	wq = (int)(rc.w * 2.0f + 0.5f);
	hq = (int)(rc.h * 2.0f + 0.5f);
	rq = (int)(radius * 2.0f + 0.5f);
	gq = (int)(glowW * 2.0f + 0.5f);
	sq = (int)(g->scale * 100.0f + 0.5f);
	for (i = 0; i < GFX_GLOW_CACHE_MAX; i++) {
		GfxGlowCache *e = &g->glows[i];
		if (e->bmp && e->wq == wq && e->hq == hq && e->rq == rq &&
			e->gq == gq && e->scaleq == sq && e->rgb == (color & 0x00FFFFFFu)) {
			e->lastUse = ++g->glowClock;
			slot = e;
			break;
		}
	}
	if (!slot) {
		GfxBitmap *bmp;
		for (i = 0; i < GFX_GLOW_CACHE_MAX; i++) {
			GfxGlowCache *e = &g->glows[i];
			if (!e->bmp) { slot = e; break; }
			if (e->lastUse < oldest) { oldest = e->lastUse; slot = e; }
		}
		if (!slot) return 0;
		bmp = glow_texture_make(g, (float)wq * 0.5f, (float)hq * 0.5f,
			(float)rq * 0.5f, (float)gq * 0.5f, color, g->scale);
		if (!bmp) return 0;
		if (slot->bmp) Gfx_DestroyBitmap(g, slot->bmp);
		slot->wq = wq; slot->hq = hq; slot->rq = rq; slot->gq = gq;
		slot->scaleq = sq; slot->rgb = color & 0x00FFFFFFu;
		slot->bmp = bmp;
		slot->lastUse = ++g->glowClock;
	}

	opacity = (float)((color >> 24) & 0xFF) / 255.0f * intensity;
	if (opacity > 1.0f) opacity = 1.0f;
	{
		D2D1_RECT_F dr = { dst.x, dst.y, dst.x + dst.w, dst.y + dst.h };
		g->rt->DrawBitmap(slot->bmp->bmp, &dr, opacity,
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
	}
	return 1;
}

/* ── 可选模块内部接口 (gfx_internal.h) ────────────────────────────────
 * gfx_rich.cpp 等可选模块通过这里访问绘制设备; 核心不反向依赖它们。 */

int Gfx_IntReady(Gfx *g)
{
	return g && g->rt != NULL;
}

IDWriteFactory *Gfx_IntDWrite(Gfx *g)
{
	return g ? g->dw : NULL;
}

ID2D1RenderTarget *Gfx_IntRT(Gfx *g)
{
	return g ? g->rt : NULL;
}

ID2D1SolidColorBrush *Gfx_IntBrush(Gfx *g, GfxColor c)
{
	return GetBrush(g, c);
}

int Gfx_IntVisible(Gfx *g, GfxRect r)
{
	return IsVisible(g, r);
}

IDWriteTextFormat *Gfx_IntFontFormat(GfxFont *f)
{
	return f ? f->fmt : NULL;
}

float Gfx_IntFontSize(GfxFont *f)
{
	return f ? f->size : 0.0f;
}

int Gfx_IntFontWeight(GfxFont *f)
{
	return f ? f->weight : 400;
}

void Gfx_IntDrawBitmap(Gfx *g, GfxBitmap *b, GfxRect dst)
{
	Gfx_DrawBitmap(g, b, dst);
}
