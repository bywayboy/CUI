/*
 * image.c - UiImage 样例: HTTP URL / 本地文件 / 内存字节 三种来源
 *
 * URL 源是 Bing 每日图: 先请求 HPImageArchive JSON, 解析出图片 url, 再由
 * UiImage 走 HTTPS 加载 (loading 期间显示 GPU spinner)。文件与内存来自运行时
 * 生成的 BMP。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_widgets.h"
#include "ui/ui_image.h"

void *malloc(size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
char *strstr(const char *hay, const char *needle);
char *strchr(const char *s, int c);
int sprintf(char *buf, const char *fmt, ...);

typedef struct App {
	UiApp *app;
	UiNet *net;
	UiImage *imgUrl;
	UiImage *imgFile;
	UiImage *imgMem;
	uint64_t bingReq;        /* HPImageArchive JSON 请求 */
	int fit;
	int anchor;
	int reloads;
	char status[256];
} App;

static unsigned char *make_bmp(int w, int h, int *outLen)
{
	int stride = (w * 3 + 3) & ~3;
	int dataSize = stride * h;
	int total = 54 + dataSize;
	unsigned char *b = (unsigned char *)malloc((size_t)total);
	int x, y;

	if (!b) return NULL;
	memset(b, 0, (size_t)total);
	b[0] = 'B'; b[1] = 'M';
	*(int *)(b + 2) = total;
	*(int *)(b + 10) = 54;
	*(int *)(b + 14) = 40;
	*(int *)(b + 18) = w;
	*(int *)(b + 22) = h;
	*(short *)(b + 26) = 1;
	*(short *)(b + 28) = 24;
	*(int *)(b + 34) = dataSize;
	for (y = 0; y < h; y++) {
		unsigned char *row = b + 54 + (h - 1 - y) * stride;
		for (x = 0; x < w; x++) {
			int border = (x < 6 || y < 6 || x >= w - 6 || y >= h - 6);
			row[x * 3 + 0] = border ? 60 : (unsigned char)(80 + x * 120 / w);
			row[x * 3 + 1] = border ? 40 : (unsigned char)(40 + y * 160 / h);
			row[x * 3 + 2] = border ? 160 : 200;
		}
	}
	*outLen = total;
	return b;
}

static int write_file_(const wchar_t *path, const void *data, int len)
{
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD n = 0;
	if (h == INVALID_HANDLE_VALUE) return 0;
	WriteFile(h, data, (DWORD)len, &n, NULL);
	CloseHandle(h);
	return n == (DWORD)len;
}

static const char *state_text(int st){
	switch (st) {
	case UI_IMG_LOADING: return "加载中";
	case UI_IMG_READY: return "就绪";
	case UI_IMG_ERROR: return "失败";
	default: return "空";
	}
}

static const char *fit_text(int fit)
{
	switch (fit) {
	case UI_IMG_FIT_COVER: return "COVER";
	case UI_IMG_FIT_STRETCH: return "STRETCH";
	case UI_IMG_FIT_NONE: return "NONE";
	default: return "CONTAIN";
	}
}

static const char *anchor_text(int a)
{
	static const char *n[9] = {
		"左上", "中上", "右上", "左中", "居中", "右中", "左下", "中下", "右下"
	};
	if (a < 0 || a > 8) a = 4;
	return n[a];
}

static void apply_style(App *a)
{
	UiImage_SetFit(a->imgUrl, a->fit);
	UiImage_SetFit(a->imgFile, a->fit);
	UiImage_SetFit(a->imgMem, a->fit);
	UiImage_SetAnchor(a->imgUrl, a->anchor);
	UiImage_SetAnchor(a->imgFile, a->anchor);
	UiImage_SetAnchor(a->imgMem, a->anchor);
}

/* 从 JSON 里取 "key":"value" 的 value (足够解析 Bing 的固定结构) */
static int json_str_(const char *json, const char *key, char *out, int cap)
{
	char pat[64];
	const char *p, *e;
	int n = 0, k = 0;

	pat[k++] = '"';
	while (*key && k < (int)sizeof(pat) - 4) pat[k++] = *key++;
	pat[k++] = '"';
	pat[k++] = ':';
	pat[k++] = '"';
	pat[k] = '\0';

	p = strstr(json, pat);
	if (!p) return 0;
	p += k;
	e = strchr(p, '"');
	if (!e) return 0;
	n = (int)(e - p);
	if (n >= cap) n = cap - 1;
	memcpy(out, p, (size_t)n);
	out[n] = '\0';
	return 1;
}

static void on_bing_json(void *user, const UiHttpResponse *r)
{
	App *a = (App *)user;
	char js[4096];
	char path[512];
	char copyright[160];
	char full[640];
	int n;

	a->bingReq = 0;
	if (!r || r->error || r->status < 200 || r->status >= 300 ||
		!r->body || r->bodyLen <= 0) {
		lstrcpynA(a->status, "Bing 每日图获取失败", (int)sizeof(a->status));
		return;
	}
	n = r->bodyLen < (int)sizeof(js) - 1 ? r->bodyLen : (int)sizeof(js) - 1;
	memcpy(js, r->body, (size_t)n);
	js[n] = '\0';

	if (!json_str_(js, "url", path, (int)sizeof(path))) {
		lstrcpynA(a->status, "Bing JSON 解析失败", (int)sizeof(a->status));
		return;
	}
	sprintf(full, "https://www.bing.com%s", path);
	UiImage_LoadUrl(a->imgUrl, full);

	if (json_str_(js, "copyright", copyright, (int)sizeof(copyright)))
		lstrcpynA(a->status, copyright, (int)sizeof(a->status));
	else
		lstrcpynA(a->status, "Bing 每日图", (int)sizeof(a->status));
}

static void load_bing(App *a)
{
	UiHttpRequest req;

	if (!a->net) return;
	if (a->bingReq) { UiHttp_Cancel(a->net, a->bingReq); a->bingReq = 0; }
	memset(&req, 0, sizeof(req));
	req.url = "https://www.bing.com/HPImageArchive.aspx"
		"?format=js&idx=0&n=1&mkt=zh-CN";
	req.timeoutMs = 15000;
	a->bingReq = UiHttp_Send(a->net, &req, on_bing_json, a);
	lstrcpynA(a->status, "正在获取 Bing 每日图...", (int)sizeof(a->status));
}

static void load_all(App *a)
{
	WCHAR tmp[MAX_PATH];
	WCHAR path[MAX_PATH];
	unsigned char *bmp;
	int len = 0;

	a->reloads++;

	GetTempPathW(MAX_PATH, tmp);
	lstrcpyW(path, tmp);
	lstrcatW(path, L"cui_image_demo.bmp");
	bmp = make_bmp(320, 200, &len);
	if (bmp) {
		write_file_(path, bmp, len);
		UiImage_LoadFile(a->imgFile, path);
		UiImage_SetBytes(a->imgMem, bmp, len);
		free(bmp);
	}
	load_bing(a);        /* 先取 HPImageArchive JSON, 再加载图片 */
}

static void on_init(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiNetConfig cfg;

	a->app = app;
	memset(&cfg, 0, sizeof(cfg));
	a->net = UiNet_Create(&cfg);
	a->imgUrl = UiImage_Create(app->gfx, a->net);
	a->imgFile = UiImage_Create(app->gfx, NULL);
	a->imgMem = UiImage_Create(app->gfx, NULL);
	UiImage_SetContext(a->imgUrl, &app->ui);
	UiImage_SetContext(a->imgFile, &app->ui);
	UiImage_SetContext(a->imgMem, &app->ui);
	UiImage_SetBackground(a->imgUrl, 0xFFE9EFF7);
	UiImage_SetBackground(a->imgFile, 0xFFE9EFF7);
	UiImage_SetBackground(a->imgMem, 0xFFE9EFF7);
	a->fit = UI_IMG_FIT_CONTAIN;
	a->anchor = UI_IMG_ANCHOR_MC;
	apply_style(a);
	load_all(a);
}

static void draw_tile(App *a, UiImage *img, const char *label, float x, float y)
{
	UiTheme *t = &a->app->theme;
	char line[128];

	Gfx_Text(a->app->gfx, t->fLabel, label, Ui_Rect(x, y, 280, 20),
		t->text2, GFX_TEXT_VCENTER);
	UiImage_SetRect(img, Ui_Rect(x, y + 26, 280, 220));
	UiImage_Draw(img, t);
	sprintf(line, "%s", state_text(UiImage_State(img)));
	Gfx_Text(a->app->gfx, t->fSmall, line, Ui_Rect(x, y + 254, 190, 18),
		t->text2, GFX_TEXT_VCENTER);
	if (UiImage_State(img) == UI_IMG_ERROR) {
		sprintf(line, "%s", UiImage_StrError(UiImage_LastError(img)));
		Gfx_Text(a->app->gfx, t->fSmall, line, Ui_Rect(x, y + 272, 280, 18),
			t->red, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
}

static void on_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiTheme *t = &app->theme;
	UiRect card = Ui_Rect(20, 20, app->w - 40, app->h - 40);
	float x = card.x + 22, y = card.y + 20;
	char line[160];

	Gfx_FillRoundRectGradV(app->gfx, Ui_Rect(0, 0, app->w, app->h), 14.0f,
		t->page, t->page2);
	Ui_Panel(&app->ui, card);

	Gfx_Text(app->gfx, t->fBig, "CUI Image", Ui_Rect(x, y, 500, 30),
		t->text, GFX_TEXT_VCENTER);
	sprintf(line, "WIC 解码 | GPU loading | %s | 对齐 %s",
		fit_text(a->fit), anchor_text(a->anchor));
	Gfx_Text(app->gfx, t->fSmall, line, Ui_Rect(x, y, card.w - 44, 30),
		t->hint, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);

	y += 56;
	draw_tile(a, a->imgUrl, "Bing 每日图 (HPImageArchive)", x, y);
	draw_tile(a, a->imgFile, "本地文件: %TEMP%\\cui_image_demo.bmp", x + 304, y);
	draw_tile(a, a->imgMem, "内存字节: SetBytes()", x + 608, y);

	y += 300;
	if (Ui_Button(&app->ui, Ui_Id("img.reload", 0, 0),
			Ui_Rect(x, y, 112, 38), "重载全部", UI_BTN_PRIMARY, 1))
		load_all(a);
	if (Ui_Button(&app->ui, Ui_Id("img.clear", 0, 0),
			Ui_Rect(x + 124, y, 84, 38), "清除", UI_BTN_SECONDARY, 1)) {
		UiImage_Clear(a->imgUrl);
		UiImage_Clear(a->imgFile);
		UiImage_Clear(a->imgMem);
		lstrcpynA(a->status, "已清除", (int)sizeof(a->status));
	}
	{
		int bx = (int)x + 220;
		if (Ui_Button(&app->ui, Ui_Id("img.fit.contain", 0, 0),
				Ui_Rect((float)bx, y, 68, 38), "留白",
				a->fit == UI_IMG_FIT_CONTAIN ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1)) {
			a->fit = UI_IMG_FIT_CONTAIN;
			apply_style(a);
		}
		if (Ui_Button(&app->ui, Ui_Id("img.fit.cover", 0, 0),
				Ui_Rect((float)bx + 76, y, 68, 38), "剪裁",
				a->fit == UI_IMG_FIT_COVER ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1)) {
			a->fit = UI_IMG_FIT_COVER;
			apply_style(a);
		}
		if (Ui_Button(&app->ui, Ui_Id("img.fit.stretch", 0, 0),
				Ui_Rect((float)bx + 152, y, 68, 38), "拉伸",
				a->fit == UI_IMG_FIT_STRETCH ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1)) {
			a->fit = UI_IMG_FIT_STRETCH;
			apply_style(a);
		}
		if (Ui_Button(&app->ui, Ui_Id("img.fit.none", 0, 0),
				Ui_Rect((float)bx + 228, y, 68, 38), "原始",
				a->fit == UI_IMG_FIT_NONE ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1)) {
			a->fit = UI_IMG_FIT_NONE;
			apply_style(a);
		}
		if (Ui_Button(&app->ui, Ui_Id("img.anchor", 0, 0),
				Ui_Rect((float)bx + 304, y, 116, 38),
				anchor_text(a->anchor), UI_BTN_SECONDARY, 1)) {
			a->anchor = (a->anchor + 1) % 9;
			apply_style(a);
		}
	}
	Gfx_Text(app->gfx, t->fSmall, a->status, Ui_Rect(x + 650, y, card.w - 700, 38),
		t->text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static int on_animating(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	return UiImage_Busy(a->imgUrl) || UiImage_Busy(a->imgFile) ||
		UiImage_Busy(a->imgMem);
}

static void on_shutdown(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	if (a->net && a->bingReq) {
		UiHttp_Cancel(a->net, a->bingReq);
		a->bingReq = 0;
	}
	if (a->imgUrl) { UiImage_Destroy(a->imgUrl); a->imgUrl = NULL; }
	if (a->imgFile) { UiImage_Destroy(a->imgFile); a->imgFile = NULL; }
	if (a->imgMem) { UiImage_Destroy(a->imgMem); a->imgMem = NULL; }
	if (a->net) { UiNet_Destroy(a->net); a->net = NULL; }
}

static int on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	(void)x; (void)user;
	if (y < (int)(50.0f * app->scale)) { *hit = HTCAPTION; return 1; }
	return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	static App a;
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	memset(&cb, 0, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.animating = on_animating;
	cb.shutdown = on_shutdown;
	cb.nchittest = on_nchittest;
	return UiApp_Run(L"CUI Image", 960, 640, UI_THEME_LIGHT, &cb, &a);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), NULL, NULL, SW_SHOW));
}
#endif
