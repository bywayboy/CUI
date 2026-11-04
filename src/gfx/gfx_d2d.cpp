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
#include "log.h"
#include "grain_cso.h"   /* fxc 生成: g_grainCso / g_grainCsoSize */

extern "C" void *memset(void *d, int c, size_t n);
extern "C" void *memcpy(void *d, const void *s, size_t n);

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

struct Gfx {
	HWND hwnd;
	ID2D1Factory1 *d2d;
	IDWriteFactory *dw;
	ID2D1RenderTarget *rt;
	ID2D1DCRenderTarget *dcRt;

	int useDcomp;
	int dcompTried;
	int noVsync;
	ID3D11Device *d3d;
	ID3D11DeviceContext *d3dctx;
	IDXGISwapChain1 *swap;
	ID2D1Device *d2dDevice;
	ID2D1DeviceContext *dc;
	ID2D1Bitmap1 *swapTarget;
	ID2D1Bitmap1 *uiTarget;
	IDCompositionDevice *compDevice;
	IDCompositionTarget *compTarget;
	IDCompositionVisual *compVisual;
	HDC memDC;
	HBITMAP dib;
	void *bits;
	int pw, ph;
	float scale;
	int generation;
	int inFrame;
	int clipDepth;
	int dirtyFull;
	int dirtyClip;
	int forceFull;
	GfxRect dirty;
	ID2D1StrokeStyle *roundStroke;
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

	GfxOverlay *overlays;
	GfxLayer *layers;
	GfxEffect *effects;
	ID2D1Image *currentTarget;   /* 当前 Draw 目标 (借用, 不 AddRef) */
	GfxEffect *frameFx[GFX_MAX_FRAME_EFFECTS];
	int frameFxCount;
	int grainFxRegistered;
	int fxDepth;
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

/* 通用合成器 layer: 整块内容画在独立 swapchain 上, 由 DComp 子 visual
 * 显示; 内容只重画一次, 透明度交给 DWM 补间。z 次序固定在所有 overlay
 * 之上 (AddVisual(TRUE)), 因此进度 overlay 可以继续在它下面更新。 */
struct GfxLayer {
	Gfx *g;
	IDXGISwapChain1 *swap;
	ID2D1Bitmap1 *target;       /* 每帧从 GetBuffer(0) 临时创建, Present 前释放 */
	IDCompositionVisual *visual;
	IDCompositionVisual3 *visual3;
	IDCompositionEffectGroup *effect;
	int pw, ph;
	float opacity;
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

static int CreateSurface(Gfx *g)
{
	if (!g || !g->d2d || g->pw <= 0 || g->ph <= 0) return 0;
	ReleaseBrushes(g);
	ReleaseGradients(g);
	SAFE_RELEASE(g->dcRt);
	g->rt = NULL;
	if (g->dib) { DeleteObject(g->dib); g->dib = NULL; }
	if (g->memDC) { DeleteDC(g->memDC); g->memDC = NULL; }
	g->bits = NULL;

	BITMAPINFO bi;
	ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = g->pw;
	bi.bmiHeader.biHeight = -g->ph;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	g->memDC = CreateCompatibleDC(NULL);
	if (!g->memDC) return 0;
	g->dib = CreateDIBSection(g->memDC, &bi, DIB_RGB_COLORS, &g->bits, NULL, 0);
	if (!g->dib) return 0;
	SelectObject(g->memDC, g->dib);

	D2D1_RENDER_TARGET_PROPERTIES props;
	ZeroMemory(&props, sizeof(props));
	props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
	props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	props.dpiX = 96.0f;
	props.dpiY = 96.0f;
	props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
	props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
	HRESULT hr = g->d2d->CreateDCRenderTarget(&props, &g->dcRt);
	if (FAILED(hr) || !g->dcRt) { SAFE_RELEASE(g->dcRt); return 0; }
	RECT rc = { 0, 0, g->pw, g->ph };
	hr = g->dcRt->BindDC(g->memDC, &rc);
	if (FAILED(hr)) { SAFE_RELEASE(g->dcRt); return 0; }
	g->rt = g->dcRt;
	g->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	g->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g->forceFull = 1;
	g->dirtyFull = 1;
	g->dirtyClip = 0;
	g->generation++;
	Log_Write("[gfx] surface %dx%d scale=%d/1000 gen=%d", g->pw, g->ph,
		(int)(g->scale * 1000.0f), g->generation);
	return 1;
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
	if (releaseSwap) SAFE_RELEASE(g->swap);
	if (g->rt == (ID2D1RenderTarget *)g->dc) g->rt = NULL;
}

static void ReleaseDcomp(Gfx *g)
{
	SAFE_RELEASE(g->alphaLayer);
	g->alphaDepth = 0;
	if (!g) return;
	ReleaseBrushes(g);
	ReleaseGradients(g);
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

	D2D1_BITMAP_PROPERTIES1 up = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0f, 96.0f);
	hr = g->dc->CreateBitmap(D2D1::SizeU((UINT32)g->pw, (UINT32)g->ph),
		NULL, 0, &up, &g->uiTarget);
	if (FAILED(hr) || !g->uiTarget) return 0;
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
	SAFE_RELEASE(g->alphaLayer);
	SAFE_RELEASE(g->dw);
	SAFE_RELEASE(g->d2d);
	/* 应用若忘了 Destruction, 这里兜底释放 overlay 结构 (DComp 资源已在
	 * ReleaseDcomp 里释放)。 */
	while (g->overlays) {
		GfxOverlay *o = g->overlays;
		g->overlays = o->next;
		HeapFree(GetProcessHeap(), 0, o);
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
	g->compVisual->AddVisual(o->visual, FALSE, NULL);
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

	/* insertAbove=TRUE, reference=NULL: 放在所有已有 overlay/layer 之上。
	 * overlay 用 FALSE 插入最底层, 所以进度条始终在 Toast 下面继续动。 */
	hr = g->compVisual->AddVisual(l->visual, TRUE, NULL);
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

int Gfx_LayerSetOpacity(GfxLayer *l, float opacity, int animateMs)
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
		double sec = (double)animateMs / 1000.0;
		anim->AddCubic(0.0, l->opacity,
			(opacity - l->opacity) / (float)sec, 0.0f, 0.0f);
		anim->End(sec, opacity);
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

void Gfx_Begin(Gfx *g)
{
	if (!g || !g->rt) return;
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
		HRESULT sr = g->swap->Present(g->noVsync ? 0 : 1, 0);
		if (FAILED(sr)) {
			Log_Write("[gfx] DComp Present failed 0x%08X", (unsigned)sr);
			FallbackToUlw(g);
			return 0;
		}
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
	g->rt->DrawGeometry(geo, br, width, g->roundStroke);
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

GfxFont *Gfx_CreateFont(Gfx *g, float sizeDip, int weight)
{
	if (!g || !g->dw || sizeDip <= 0.0f) return NULL;
	GfxFont *f = (GfxFont *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GfxFont));
	if (!f) return NULL;
	DWRITE_FONT_WEIGHT w = weight >= 600 ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
	HRESULT hr = g->dw->CreateTextFormat(L"Microsoft YaHei", NULL, w,
		DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"zh-CN", &f->fmt);
	if (FAILED(hr) || !f->fmt) { HeapFree(GetProcessHeap(), 0, f); return NULL; }
	f->fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	f->fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	f->fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	f->size = sizeDip;
	f->weight = weight;
	return f;
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
	WCHAR wbuf[1024];
	if (!Utf8ToWide(utf8, wbuf, 1024)) return NULL;
	if (maxW < 1.0f) maxW = 1.0f;
	if (maxH < 1.0f) maxH = 1.0f;
	IDWriteTextLayout *layout = NULL;
	HRESULT hr = g->dw->CreateTextLayout(wbuf, (UINT32)lstrlenW(wbuf), f->fmt, maxW, maxH, &layout);
	if (FAILED(hr) || !layout) return NULL;
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
	if (SUCCEEDED(layout->GetMetrics(&m))) w = m.width;
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
