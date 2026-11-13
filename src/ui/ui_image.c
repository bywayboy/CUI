/*
 * ui_image.c - 图片组件 (WIC 解码 + Gfx 位图 + GPU loading 动画)
 */
#include "ui/ui_image.h"
#include "ui/ui_frame.h"
#include "log.h"

#include <initguid.h>
#include <wincodec.h>

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void free(void *p);
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
size_t strlen(const char *s);
int sprintf(char *buf, const char *fmt, ...);

#define IMG_MAX_BYTES (32 * 1024 * 1024)
#define IMG_FADE_MS   180.0

struct UiImage {
	Gfx *gfx;
	UiNet *net;
	GfxRect rc;
	int fit;
	int anchor;
	GfxColor bg;
	int state;
	int error;

	GfxBitmap *bmp;
	int bw, bh;
	int gfxGen;

	unsigned char *src;      /* 编码字节 (设备丢失时重建) */
	int srcLen;
	char *source;            /* 最近一次 URL (错误展示/重试) */
	uint64_t reqId;          /* 进行中的 HTTP 请求 */
	uint64_t readyAt;

	UiImageFetchFn fetch;
	void *fetchUser;
	IWICImagingFactory *wic;
	UiContext *ui;
	int needDecode;          /* 首帧前 Gfx 未就绪: 等绘制时再解码 */
};

static char *dupstr_(const char *s)
{
	char *p;
	int n;
	if (!s) return NULL;
	n = (int)strlen(s) + 1;
	p = (char *)malloc((size_t)n);
	if (p) memcpy(p, s, (size_t)n);
	return p;
}

static int starts_with_i_(const char *s, const char *prefix)
{
	int i;
	for (i = 0; prefix[i]; i++) {
		int a = (unsigned char)s[i];
		int b = (unsigned char)prefix[i];
		if (a >= 'A' && a <= 'Z') a += 32;
		if (b >= 'A' && b <= 'Z') b += 32;
		if (a != b) return 0;
		if (!a) return 0;
	}
	return 1;
}

static void img_invalidate_(UiImage *img)
{
	if (img && img->ui) Ui_ContextInvalidate(img->ui, img->rc);
}

/* 绘制中的逐帧动画: 只登记动画脏区, 不直接 InvalidateRect */
static void img_dirty_(UiImage *img)
{
	if (img && img->ui) Ui_AnimDirtyRect(img->ui, img->rc);
}

static void img_reset_(UiImage *img)
{
	if (!img) return;
	if (img->reqId && img->net) {
		UiHttp_Cancel(img->net, img->reqId);
		img->reqId = 0;
	}
	if (img->bmp) {
		Gfx_DestroyBitmap(img->gfx, img->bmp);
		img->bmp = NULL;
	}
	if (img->src) { free(img->src); img->src = NULL; }
	img->srcLen = 0;
	if (img->source) { free(img->source); img->source = NULL; }
	img->bw = img->bh = 0;
	img->error = 0;
	img->state = UI_IMG_IDLE;
	img_invalidate_(img);
}

const char *UiImage_StrError(int err)
{
	switch (err) {
	case 0: return "ok";
	case UI_IMG_E_DECODE: return "image decode failed";
	case UI_IMG_E_FILE: return "file read failed";
	case UI_IMG_E_OOM: return "out of memory";
	case UI_IMG_E_PARAM: return "bad parameter";
	default: return UiNet_StrError(err);
	}
}

/* ── WIC 解码 ───────────────────────────────────────────────────────── */

#define REL_(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

static int img_decode_(UiImage *img, int rebuild)
{
	IWICStream *stream = NULL;
	IWICBitmapDecoder *dec = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICFormatConverter *conv = NULL;
	UINT w = 0, h = 0;
	BYTE *pix = NULL;
	GfxBitmap *bmp = NULL;
	HRESULT hr;
	int ok = 0;

	if (!img || !img->wic || !img->src || img->srcLen <= 0) return 0;
	if (!Gfx_Ready(img->gfx)) {
		img->state = UI_IMG_LOADING;
		img->needDecode = 1;
		return 0;
	}

	hr = img->wic->lpVtbl->CreateStream(img->wic, &stream);
	if (FAILED(hr) || !stream) goto done;
	hr = stream->lpVtbl->InitializeFromMemory(stream, img->src,
		(DWORD)img->srcLen);
	if (FAILED(hr)) goto done;
	hr = img->wic->lpVtbl->CreateDecoderFromStream(img->wic, (IStream *)stream,
		NULL, WICDecodeMetadataCacheOnDemand, &dec);
	if (FAILED(hr) || !dec) goto done;
	hr = dec->lpVtbl->GetFrame(dec, 0, &frame);
	if (FAILED(hr) || !frame) goto done;
	hr = img->wic->lpVtbl->CreateFormatConverter(img->wic, &conv);
	if (FAILED(hr) || !conv) goto done;
	hr = conv->lpVtbl->Initialize(conv, (IWICBitmapSource *)frame,
		&GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0,
		WICBitmapPaletteTypeCustom);
	if (FAILED(hr)) goto done;
	hr = conv->lpVtbl->GetSize(conv, &w, &h);
	if (FAILED(hr) || w == 0 || h == 0) goto done;
	if (w > 8192 || h > 8192 || (uint64_t)w * h > (32u << 20)) goto done;

	pix = (BYTE *)malloc((size_t)w * h * 4);
	if (!pix) goto done;
	hr = conv->lpVtbl->CopyPixels(conv, NULL, w * 4, w * h * 4, pix);
	if (FAILED(hr)) goto done;

	bmp = Gfx_CreateBitmap(img->gfx, (int)w, (int)h, pix);
	if (!bmp) goto done;

	if (img->bmp) Gfx_DestroyBitmap(img->gfx, img->bmp);
	img->bmp = bmp;
	img->bw = (int)w;
	img->bh = (int)h;
	img->gfxGen = Gfx_Generation(img->gfx);
	img->state = UI_IMG_READY;
	img->error = 0;
	img->needDecode = 0;
	if (!rebuild) img->readyAt = (uint64_t)UiFrame_Now();
	ok = 1;
	img_invalidate_(img);

done:
	if (pix) free(pix);
	REL_(conv);
	REL_(frame);
	REL_(dec);
	REL_(stream);
	if (!ok) {
		img->state = UI_IMG_ERROR;
		img->error = UI_IMG_E_DECODE;
		Log_Write("[img] decode failed hr=0x%x", (unsigned)hr);
	}	return ok;
}

static unsigned char *read_file_(const wchar_t *path, int *outLen)
{
	HANDLE h;
	LARGE_INTEGER sz;
	DWORD got = 0;
	unsigned char *buf;

	*outLen = 0;
	h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0,
		NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 ||
		sz.QuadPart > IMG_MAX_BYTES) {
		CloseHandle(h);
		return NULL;
	}
	buf = (unsigned char *)malloc((size_t)sz.QuadPart);
	if (!buf) { CloseHandle(h); return NULL; }
	if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) ||
		got != (DWORD)sz.QuadPart) {
		free(buf);
		CloseHandle(h);
		return NULL;
	}
	CloseHandle(h);
	*outLen = (int)got;
	return buf;
}

/* ── 加载入口 ───────────────────────────────────────────────────────── */

static void img_set_error_(UiImage *img, int err)
{
	img->state = UI_IMG_ERROR;
	img->error = err;
	img_invalidate_(img);
}

static void img_http_cb_(void *user, const UiHttpResponse *r)
{
	UiImage *img = (UiImage *)user;
	if (!img) return;
	img->reqId = 0;
	if (!r || r->error) {
		img_set_error_(img, r ? r->error : UI_NET_E_INTERNAL);
		return;
	}
	if (r->status < 200 || r->status >= 300) {
		img_set_error_(img, UI_NET_E_HTTP);
		return;
	}
	if (!r->body || r->bodyLen <= 0) {
		img_set_error_(img, UI_IMG_E_DECODE);
		return;
	}
	if (r->bodyLen > IMG_MAX_BYTES) {
		img_set_error_(img, UI_IMG_E_DECODE);
		return;
	}
	if (img->src) free(img->src);
	img->src = (unsigned char *)malloc((size_t)r->bodyLen);
	if (!img->src) { img->srcLen = 0; img_set_error_(img, UI_IMG_E_OOM); return; }
	memcpy(img->src, r->body, (size_t)r->bodyLen);
	img->srcLen = r->bodyLen;
	img->state = UI_IMG_LOADING;
	img_decode_(img, 0);
}

int UiImage_LoadUrl(UiImage *img, const char *url)
{
	if (!img || !url || !url[0]) return 0;
	img_reset_(img);
	img->source = dupstr_(url);
	img->state = UI_IMG_LOADING;

	if (img->fetch) {
		void *bytes = NULL;
		int len = 0;
		int r = img->fetch(img->fetchUser, url, &bytes, &len);
		if (r == 1) {
			int ok = 0;
			if (bytes && len > 0 && len <= IMG_MAX_BYTES) {
				img->src = (unsigned char *)malloc((size_t)len);
				if (img->src) {
					memcpy(img->src, bytes, (size_t)len);
					img->srcLen = len;
					ok = img_decode_(img, 0);
				}
			}
			if (!ok && img->state != UI_IMG_READY)
				img_set_error_(img, UI_IMG_E_DECODE);
			if (bytes) free(bytes);
			return ok;
		}
		if (r < 0) {
			img_set_error_(img, UI_IMG_E_DECODE);
			return 0;
		}
		return 1;              /* 异步: 等 UiImage_SetBytes */
	}

	if (starts_with_i_(url, "http://") || starts_with_i_(url, "https://")) {
		UiHttpRequest req;
		if (!img->net) { img_set_error_(img, UI_IMG_E_PARAM); return 0; }
		memset(&req, 0, sizeof(req));
		req.url = url;
		req.timeoutMs = 20000;
		req.extraHeaders = "Accept: image/*\r\n";
		img->reqId = UiHttp_Send(img->net, &req, img_http_cb_, img);
		if (!img->reqId) { img_set_error_(img, UI_IMG_E_PARAM); return 0; }
		return 1;
	}

	/* 其余按本地文件路径处理 */
	{
		WCHAR wpath[MAX_PATH];
		int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, wpath, MAX_PATH);
		if (n <= 0) { img_set_error_(img, UI_IMG_E_PARAM); return 0; }
		return UiImage_LoadFile(img, wpath);
	}
}

int UiImage_LoadFile(UiImage *img, const wchar_t *path)
{
	unsigned char *buf;
	int len = 0;

	if (!img || !path) return 0;
	img_reset_(img);
	buf = read_file_(path, &len);
	if (!buf) { img_set_error_(img, UI_IMG_E_FILE); return 0; }
	img->src = buf;
	img->srcLen = len;
	img->state = UI_IMG_LOADING;
	return img_decode_(img, 0);
}

int UiImage_SetBytes(UiImage *img, const void *bytes, int len)
{
	if (!img || !bytes || len <= 0) return 0;
	if (img->reqId && img->net) { UiHttp_Cancel(img->net, img->reqId); img->reqId = 0; }
	if (img->bmp) { Gfx_DestroyBitmap(img->gfx, img->bmp); img->bmp = NULL; }
	if (img->src) free(img->src);
	img->src = (unsigned char *)malloc((size_t)len);
	if (!img->src) { img->srcLen = 0; img_set_error_(img, UI_IMG_E_OOM); return 0; }
	memcpy(img->src, bytes, (size_t)len);
	img->srcLen = len;
	if (img->source) { free(img->source); img->source = NULL; }
	img->state = UI_IMG_LOADING;
	return img_decode_(img, 0);
}

void UiImage_Clear(UiImage *img)
{
	if (img) img_reset_(img);
}

/* ── 状态 ───────────────────────────────────────────────────────────── */

UiImage *UiImage_Create(Gfx *gfx, UiNet *net)
{
	UiImage *img;
	HRESULT hr;

	if (!gfx) return NULL;
	img = (UiImage *)calloc(1, sizeof(UiImage));
	if (!img) return NULL;
	img->gfx = gfx;
	img->net = net;
	img->fit = UI_IMG_FIT_CONTAIN;
	img->anchor = UI_IMG_ANCHOR_MC;
	img->bg = 0;
	img->state = UI_IMG_IDLE;
	hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		&IID_IWICImagingFactory, (void **)&img->wic);
	if (FAILED(hr) || !img->wic) {
		Log_Write("[img] WIC factory failed 0x%x", (unsigned)hr);
		free(img);
		return NULL;
	}
	return img;
}

void UiImage_Destroy(UiImage *img)
{
	if (!img) return;
	img_reset_(img);
	if (img->wic) { img->wic->lpVtbl->Release(img->wic); img->wic = NULL; }
	free(img);
}

void UiImage_SetRect(UiImage *img, GfxRect rc)
{
	if (img) img->rc = rc;
}

void UiImage_SetFit(UiImage *img, int fit)
{
	if (img) img->fit = fit;
}

void UiImage_SetAnchor(UiImage *img, int anchor)
{
	if (!img) return;
	if (anchor < UI_IMG_ANCHOR_TL || anchor > UI_IMG_ANCHOR_BR)
		anchor = UI_IMG_ANCHOR_MC;
	img->anchor = anchor;
}

void UiImage_SetBackground(UiImage *img, GfxColor color)
{
	if (img) img->bg = color;
}

void UiImage_SetProvider(UiImage *img, UiImageFetchFn fn, void *user)
{
	if (!img) return;
	img->fetch = fn;
	img->fetchUser = user;
}

void UiImage_SetContext(UiImage *img, UiContext *ui)
{
	if (img) img->ui = ui;
}

int UiImage_State(const UiImage *img)
{
	return img ? img->state : UI_IMG_IDLE;
}

int UiImage_LastError(const UiImage *img)
{
	return img ? img->error : UI_IMG_E_PARAM;
}

int UiImage_PixelWidth(const UiImage *img)
{
	return (img && img->state == UI_IMG_READY) ? img->bw : 0;
}

int UiImage_PixelHeight(const UiImage *img)
{
	return (img && img->state == UI_IMG_READY) ? img->bh : 0;
}

GfxBitmap *UiImage_Bitmap(const UiImage *img)
{
	return (img && img->state == UI_IMG_READY) ? img->bmp : NULL;
}

int UiImage_Busy(const UiImage *img)
{
	if (!img) return 0;
	if (img->state == UI_IMG_LOADING) return 1;
	if (img->state == UI_IMG_READY &&
		UiFrame_Now() - (double)img->readyAt < IMG_FADE_MS + 20.0)
		return 1;
	return 0;
}

/* ── 绘制 ───────────────────────────────────────────────────────────── */

static GfxRect fit_rect_(UiImage *img, GfxRect box)
{
	float s;
	float w, h;
	float ex, ey;
	int ax, ay;
	int iw, ih, fit, anchor;
	GfxRect r;

	iw = img->bw;
	ih = img->bh;
	fit = img->fit;
	anchor = img->anchor;
	if (iw <= 0 || ih <= 0) return box;
	if (fit == UI_IMG_FIT_STRETCH) return box;
	if (fit == UI_IMG_FIT_CONTAIN) {
		float sx = box.w / (float)iw;
		float sy = box.h / (float)ih;
		s = sx < sy ? sx : sy;
	} else if (fit == UI_IMG_FIT_COVER) {
		float sx = box.w / (float)iw;
		float sy = box.h / (float)ih;
		s = sx > sy ? sx : sy;
	} else {
		/* NONE: 1 图素 = 1 物理像素 (有 Context 时按 DPI 折算成 DIP) */
		s = (img->ui && img->ui->scale > 0.0f) ? 1.0f / img->ui->scale : 1.0f;
	}
	w = iw * s;
	h = ih * s;
	ex = box.w - w;          /* COVER 时为负: 负边距即裁剪量 */
	ey = box.h - h;
	ax = anchor % 3;         /* 0=左 1=中 2=右 */
	ay = anchor / 3;         /* 0=上 1=中 2=下 */
	if (ax < 0 || ax > 2) ax = 1;
	if (ay < 0 || ay > 2) ay = 1;
	r.x = box.x + (ax == 0 ? 0.0f : ax == 1 ? ex * 0.5f : ex);
	r.y = box.y + (ay == 0 ? 0.0f : ay == 1 ? ey * 0.5f : ey);
	r.w = w;
	r.h = h;
	return r;
}

void UiImage_Draw(UiImage *img, const UiTheme *t)
{
	GfxRect rc;
	double now;
	float alpha;

	if (!img || !img->gfx || !t) return;
	rc = img->rc;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return;

	/* 首帧前加载: Gfx 就绪后补解码 */
	if (img->state == UI_IMG_LOADING && img->needDecode && Gfx_Ready(img->gfx)) {
		img->needDecode = 0;
		img_decode_(img, 0);
	}

	if (img->state == UI_IMG_READY) {
		/* 设备丢失: 用保留的编码字节就地重建 */
		if (img->gfxGen != Gfx_Generation(img->gfx)) {
			if (img->bmp) {
				Gfx_DestroyBitmap(img->gfx, img->bmp);
				img->bmp = NULL;
			}
			if (!img_decode_(img, 1) && img->state != UI_IMG_READY)
				img_set_error_(img, UI_IMG_E_DECODE);
		}
		if (img->state == UI_IMG_READY && img->bmp) {
			GfxRect dst = fit_rect_(img, rc);
			int clip;
			now = UiFrame_Now();
			alpha = (float)((now - (double)img->readyAt) / IMG_FADE_MS);
			if (alpha < 0.0f) alpha = 0.0f;
			if (alpha > 1.0f) alpha = 1.0f;
			/* 留白底色: CONTAIN/NONE 时填充未覆盖区域 */
			if ((img->bg >> 24) != 0)
				Gfx_FillRect(img->gfx, rc, img->bg);
			if (alpha < 0.999f) Gfx_PushAlpha(img->gfx, alpha);
			/* 任何超出组件矩形的模式 (COVER/NONE) 都裁剪, 不允许画出边界 */
			clip = dst.x < rc.x - 0.5f || dst.y < rc.y - 0.5f ||
				dst.x + dst.w > rc.x + rc.w + 0.5f ||
				dst.y + dst.h > rc.y + rc.h + 0.5f;
			if (clip) Gfx_PushClip(img->gfx, rc);
			Gfx_DrawBitmap(img->gfx, img->bmp, dst);
			if (clip) Gfx_PopClip(img->gfx);
			if (alpha < 0.999f) {
				Gfx_PopAlpha(img->gfx);
				img_dirty_(img);        /* 淡入继续出帧 */
			}
			return;
		}
	}

	if (img->state == UI_IMG_LOADING) {
		float cx = rc.x + rc.w * 0.5f;
		float cy = rc.y + rc.h * 0.5f;
		float r = rc.w < rc.h ? rc.w : rc.h;
		double deg;

		Gfx_FillRoundRect(img->gfx, rc, 8.0f, t->frame);
		Gfx_StrokeRoundRect(img->gfx, rc, 8.0f, t->border, 1.0f);
		r = r * 0.12f;
		if (r < 10.0f) r = 10.0f;
		if (r > 26.0f) r = 26.0f;
		Gfx_StrokeEllipse(img->gfx, cx, cy, r, t->border, 2.0f);
		deg = UiFrame_Now() * 0.25;
		while (deg >= 360.0) deg -= 360.0;
		Gfx_StrokeArc(img->gfx, cx, cy, r, (float)deg, 100.0f, t->blue, 2.5f);
		img_dirty_(img);                /* spinner 自驱动出帧 */
		return;
	}

	/* IDLE / ERROR */
	Gfx_FillRoundRect(img->gfx, rc, 8.0f, t->frame);
	Gfx_StrokeRoundRect(img->gfx, rc, 8.0f, t->border, 1.0f);
	if (rc.h >= 36.0f && rc.w >= 60.0f) {
		const char *txt = (img->state == UI_IMG_ERROR) ? "加载失败" : "图片";
		Gfx_Text(img->gfx, t->fSmall, txt, rc, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}
}
