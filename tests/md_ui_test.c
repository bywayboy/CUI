/*
 * md_ui_test.c - UiMarkdown 行模型/懒测量/滚动断言 (隐藏窗口, 0CRT 控制台)
 *
 * 覆盖: 10k 行文档首帧成本、可见行懒测量、滚轮增量测量、宽度变化重排。
 * 构建: bin\cui_md_ui_test.exe; 退出码 0=全通过。
 */
#include <windows.h>
#include <stdarg.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_md.h"
#include "log.h"

int sprintf(char *buf, const char *fmt, ...);
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
size_t strlen(const char *s);
void *memcpy(void *d, const void *s, size_t n);

static int g_pass, g_fail;

/* UiContext/UiTheme 较大: 放静态区, 避免 startup 栈帧 >32KB 触发 __chkstk */
static UiTheme g_theme;
static unsigned char g_guard1[64];
static UiContext g_ctx;
static unsigned char g_guard2[64];

static int guard_ok(void)
{
	int i;
	for (i = 0; i < 64; i++)
		if (g_guard1[i] || g_guard2[i]) return 0;
	return g_ctx.theme == &g_theme;
}

/* 动态文本缓冲: 先按 vsnprintf 需求量算, 不够就扩容; 无固定内容上限 */
typedef struct DynBuf { char *p; int n, cap; } DynBuf;

static void db_free(DynBuf *b)
{
	if (b->p) HeapFree(GetProcessHeap(), 0, b->p);
	b->p = NULL;
	b->n = b->cap = 0;
}

static int db_reserve(DynBuf *b, int need)
{
	int nc;
	char *np;
	if (need <= b->cap) return 1;
	nc = b->cap > 0 ? b->cap : 256;
	while (nc < need) nc *= 2;
	np = b->p
		? (char *)HeapReAlloc(GetProcessHeap(), 0, b->p, (SIZE_T)nc)
		: (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)nc);
	if (!np) return 0;
	b->p = np;
	b->cap = nc;
	return 1;
}

static void db_addf(DynBuf *b, const char *fmt, ...)
{
	int space, n;
	va_list ap;
	for (;;) {
		space = b->cap - b->n;
		if (space < 2) {
			if (!db_reserve(b, b->cap > 0 ? b->cap * 2 : 256)) return;
			continue;
		}
		va_start(ap, fmt);
		n = vsnprintf(b->p + b->n, (size_t)space, fmt, ap);
		va_end(ap);
		if (n < 0) return;
		/* crtstub 的 vsnprintf 返回已写长度: 写满即视为截断, 扩容重试 */
		if (n < space - 1) { b->n += n; return; }
		if (!db_reserve(b, b->cap * 2)) return;
	}
}

static void db_puts(DynBuf *b, const char *s)
{
	int n = (int)strlen(s);
	if (!db_reserve(b, b->n + n + 1)) return;
	memcpy(b->p + b->n, s, (size_t)n);
	b->n += n;
	b->p[b->n] = 0;
}

static void out_(const char *s)
{
	DWORD n = 0;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &n, NULL);
}

#define CHECK(c) do { \
	if (c) g_pass++; \
	else { \
		char b_[320]; \
		g_fail++; \
		sprintf(b_, "FAIL line %d: %s\n", __LINE__, #c); \
		out_(b_); \
	} \
} while (0)

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProcW(h, m, w, l);
}

static double now_ms(void)
{
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static int build_doc(char **out, int paragraphs)
{
	DynBuf b;
	int i;
	ZeroMemory(&b, sizeof(b));
	for (i = 0; i < paragraphs; i++) {
		db_addf(&b, "## 段落 %d\n\n这是第 %d 段的正文，用于验证懒测量与滚动收敛。"
			"每段两行文本，Markdown 文档总规模约一万行。\n\n", i + 1, i + 1);
	}
	*out = b.p;
	return b.n;
}

static int make_small_bmp(const char *path)
{
	enum { W = 64, H = 32 };
	int stride = (W * 3 + 3) & ~3;
	int size = 54 + stride * H;
	unsigned char *b = (unsigned char *)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY, size);
	BITMAPFILEHEADER *fh;
	BITMAPINFOHEADER *ih;
	HANDLE h;
	DWORD wrote = 0;
	int x, y, ok;
	if (!b) return 0;
	fh = (BITMAPFILEHEADER *)b;
	ih = (BITMAPINFOHEADER *)(b + sizeof(BITMAPFILEHEADER));
	fh->bfType = 0x4D42;
	fh->bfSize = (DWORD)size;
	fh->bfOffBits = 54;
	ih->biSize = 40;
	ih->biWidth = W;
	ih->biHeight = H;
	ih->biPlanes = 1;
	ih->biBitCount = 24;
	ih->biSizeImage = (DWORD)(stride * H);
	for (y = 0; y < H; y++) {
		unsigned char *row = b + 54 + (H - 1 - y) * stride;
		for (x = 0; x < W; x++) {
			row[x * 3 + 0] = (unsigned char)(200 - x * 2);
			row[x * 3 + 1] = (unsigned char)(60 + y * 4);
			row[x * 3 + 2] = (unsigned char)(40 + x * 2);
		}
	}
	h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	ok = h != INVALID_HANDLE_VALUE &&
		WriteFile(h, b, (DWORD)size, &wrote, NULL) && wrote == (DWORD)size;
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	HeapFree(GetProcessHeap(), 0, b);
	return ok;
}

void __stdcall startup(void)
{
	WNDCLASSEXW wc;
	HWND hwnd = NULL;
	Gfx *g = NULL;
	UiMarkdown *md = NULL;
	char *doc = NULL;
	UiRect area = { 24.0f, 20.0f, 712.0f, 680.0f };
	int docLen, rows, measured1, calls1, rows2;
	double t0, t_parse, t_frame, t_frame2;

	Log_Init();
	SetEnvironmentVariableA("SCV2_ULW", "1");
	CHECK(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED) >= 0);

	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"cui_md_ui_test";
	CHECK(RegisterClassExW(&wc) != 0);
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_md_ui_test", L"", WS_POPUP, 0, 0, 760, 720,
		NULL, NULL, wc.hInstance, NULL);
	CHECK(hwnd != NULL);
	if (!hwnd) goto done;

	CHECK(Gfx_Init(&g, hwnd) == 1);
	if (!g) goto done;
	CHECK(Gfx_Resize(g, 760, 720, 1.0f) == 1);
	UiTheme_Init(&g_theme, g);
	Ui_ContextInit(&g_ctx, hwnd, g, &g_theme);
	Ui_SetViewport(&g_ctx, 760.0f, 720.0f, 1.0f);
	Ui_BeginFrame(&g_ctx);
	Ui_EndFrame(&g_ctx);

	docLen = build_doc(&doc, 2000);   /* 2000 段 ≈ 10000 行 */
	CHECK(docLen > 0);

	md = UiMarkdown_Create(g, Ui_Id("md.ui.test", 0, 0), NULL);
	CHECK(md != NULL);
	if (!md) goto done;

	t0 = now_ms();
	UiMarkdown_SetText(md, doc);
	t_parse = now_ms() - t0;
	rows = UiMarkdown_DebugRows(md);
	CHECK(rows >= 2000);
	CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);

	/* 空帧: SetText 之后、首帧之前不应依赖未提交布局;
	 * guard 同时验证静态 ctx/theme 没有被越界写破坏。 */
	CHECK(guard_ok());
	Ui_BeginFrame(&g_ctx);
	Ui_EndFrame(&g_ctx);

	t0 = now_ms();
	Gfx_Begin(g);
	Ui_BeginFrame(&g_ctx);
	Ui_Markdown(md, &g_ctx, area);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g) == 1);
	t_frame = now_ms() - t0;

	measured1 = UiMarkdown_DebugMeasured(md);
	calls1 = UiMarkdown_DebugMeasureCalls(md);
	CHECK(measured1 > 0);
	CHECK(measured1 < 80);             /* 只实测可见行 */
	CHECK(calls1 <= measured1 + 4);
	CHECK(t_parse < 50.0);
	CHECK(t_frame < 50.0);
	CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);

	t0 = now_ms();
	Gfx_Begin(g);
	Ui_BeginFrame(&g_ctx);
	Ui_Markdown(md, &g_ctx, area);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g) == 1);
	t_frame2 = now_ms() - t0;
	CHECK(t_frame2 < 50.0);
	{
		char line[240];
		sprintf(line,
			"md_ui_test: rows=%d measured=%d calls=%d parse=%.1fms "
			"frame1=%.1fms frame2=%.1fms\n",
			rows, measured1, calls1, t_parse, t_frame, t_frame2);
		out_(line);
	}

	/* 滚轮向下: 只新增可见行附近的实测 */
	Ui_OnMouseWheel(&g_ctx, 380.0f, 400.0f, -3.0f);
	Gfx_Begin(g);
	Ui_BeginFrame(&g_ctx);
	Ui_Markdown(md, &g_ctx, area);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g) == 1);
	rows2 = UiMarkdown_DebugRows(md);
	CHECK(rows2 == rows);
	CHECK(UiMarkdown_DebugMeasured(md) >= measured1);
	CHECK(UiMarkdown_DebugMeasured(md) < measured1 + 120);

	/* 宽度变化: 全部回退为估算, 仅可见重新实测 */
	area.w = 320.0f;
	Gfx_Begin(g);
	Ui_BeginFrame(&g_ctx);
	Ui_Markdown(md, &g_ctx, area);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g) == 1);
	CHECK(UiMarkdown_DebugRows(md) == rows);
	CHECK(UiMarkdown_DebugMeasured(md) < 80);

	/* 回到顶部后仍只保留可见行实测 */
	UiMarkdown_ScrollToTop(md);
	Gfx_Begin(g);
	Ui_BeginFrame(&g_ctx);
	Ui_Markdown(md, &g_ctx, area);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g) == 1);
	CHECK(UiMarkdown_ContentHeight(md) > 1000.0f);
	CHECK(UiMarkdown_DebugMeasured(md) < 80);

	/* 真实表格: 1 个块, 高度由单元格布局决定 */
	{
		DynBuf tb;
		ZeroMemory(&tb, sizeof(tb));
		db_puts(&tb, "| A | B |\n|:--|--:|\n| 1 | 2 |\n");
		UiMarkdown_SetText(md, tb.p);
		db_free(&tb);
	}
	CHECK(UiMarkdown_DebugRows(md) == 1);
	Ui_BeginFrame(&g_ctx);
	Ui_EndFrame(&g_ctx);
	Gfx_Begin(g);
	Ui_BeginFrame(&g_ctx);
	Ui_Markdown(md, &g_ctx, area);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g) == 1);
	CHECK(UiMarkdown_DebugMeasured(md) == 1);
	CHECK(UiMarkdown_ContentHeight(md) > 40.0f);
	CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);

	/* 真实图片: 本地 BMP, 解码后按像素比例回填高度 */
	{
		char path[MAX_PATH];
		DynBuf db;
		DWORD n = GetTempPathA((DWORD)sizeof(path), path);
		if (n == 0 || n >= sizeof(path) - 24)
			lstrcpynA(path, "C:\\Windows\\Temp\\", (int)sizeof(path));
		lstrcatA(path, "cui_md_ui.bmp");
		CHECK(make_small_bmp(path) != 0);
		{
			char *p = path;
			while (*p) { if (*p == '\\') *p = '/'; p++; }
		}
		ZeroMemory(&db, sizeof(db));
		db_addf(&db, "![a](%s \"center\")\n", path);
		UiMarkdown_SetText(md, db.p);
		db_free(&db);
		CHECK(UiMarkdown_DebugRows(md) == 1);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_DebugMeasured(md) == 1);
		CHECK(UiMarkdown_ContentHeight(md) > 35.0f);
		CHECK(UiMarkdown_ContentHeight(md) < 80.0f);

		/* 行内图片: 段落行高应被图片撑高 */
		{
			char doc2[700];
			wsprintfA(doc2, "前 ![x](%s) 后\n", path);
			UiMarkdown_SetText(md, doc2);
			Ui_BeginFrame(&g_ctx);
			Ui_EndFrame(&g_ctx);
			Gfx_Begin(g);
			Ui_BeginFrame(&g_ctx);
			Ui_Markdown(md, &g_ctx, area);
			Ui_EndFrame(&g_ctx);
			CHECK(Gfx_End(g) == 1);
			CHECK(UiMarkdown_DebugRows(md) == 1);
			CHECK(UiMarkdown_DebugMeasured(md) == 1);
			CHECK(UiMarkdown_ContentHeight(md) > 30.0f);
		}
	}

	/* 实时光标解析 (WM_SETCURSOR 直接调用): 与帧/重绘无关 */
	{
		DynBuf cb;
		int i, x, y, hand = 0, cx = 0, cy = 0, yEnd;
		ZeroMemory(&cb, sizeof(cb));
		for (i = 0; i < 10; i++)
			db_addf(&cb, "段落 %d 用于把链接推到可视区中部。\n\n", i + 1);
		db_puts(&cb, "链接：[点我](https://example.com) 结束\n\n");
		for (i = 0; i < 30; i++)
			db_addf(&cb, "尾部段落 %d。\n\n", i + 1);
		UiMarkdown_SetText(md, cb.p);
		db_free(&cb);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(Ui_HitTest(&g_ctx, area.x + 40.0f, area.y + 40.0f)
			!= UI_ID_NONE);
		yEnd = (int)(area.y + area.h) - 4;
		/* 事件路径: 逐点投递鼠标移动, 控件在移动事件内更新光标 */
		for (y = (int)area.y + 12; y < yEnd && !hand; y += 6)
			for (x = (int)area.x + 4; x < (int)(area.x + area.w) - 4; x += 6) {
				Ui_OnMouseMove(&g_ctx, (float)x, (float)y);
				if (Ui_CursorAt(&g_ctx, (float)x, (float)y)
					== UI_CURSOR_HAND) {
					hand = 1;
					cx = x;
					cy = y;
					break;
				}
			}
		CHECK(hand == 1);
		if (hand) {
			/* 链接仍可见但指针在右侧空白: 移动事件后应为箭头 */
			Ui_OnMouseMove(&g_ctx, area.x + area.w - 10.0f, (float)cy);
			CHECK(Ui_CursorAt(&g_ctx, area.x + area.w - 10.0f,
				(float)cy) != UI_CURSOR_HAND);
			/* 滚动改变内容相对指针位置: 原点不再命中, 新位置仍可命中 */
			Ui_OnMouseMove(&g_ctx, (float)cx, (float)cy);
			CHECK(Ui_CursorAt(&g_ctx, (float)cx, (float)cy)
				== UI_CURSOR_HAND);
			Ui_OnMouseWheel(&g_ctx, (float)cx, (float)cy, -3.0f);
			for (i = 0; i < 6; i++) {
				Sleep(25);
				Gfx_Begin(g);
				Ui_BeginFrame(&g_ctx);
				Ui_Markdown(md, &g_ctx, area);
				Ui_EndFrame(&g_ctx);
				CHECK(Gfx_End(g) == 1);
			}
			CHECK(Ui_CursorAt(&g_ctx, (float)cx, (float)cy)
				!= UI_CURSOR_HAND);
			{
				int h2 = 0;
				for (y = (int)area.y + 12; y < yEnd && !h2; y += 6)
					for (x = (int)area.x + 4;
						x < (int)(area.x + area.w) - 4; x += 6) {
						Ui_OnMouseMove(&g_ctx, (float)x, (float)y);
						if (Ui_CursorAt(&g_ctx, (float)x,
							(float)y) == UI_CURSOR_HAND) {
							h2 = 1;
							break;
						}
					}
				CHECK(h2 == 1);
			}
		}
	}

	/* 容器: 标题区抬高首行, 子块照常分行 */
	{
		const char *cdoc =
			"::: warning 注意\n第一段正文\n\n第二段正文\n:::\n\n"
			"> [!NOTE]\n> 警报正文\n";
		UiMarkdown_SetText(md, cdoc);
		CHECK(UiMarkdown_DebugRows(md) == 3);
		/* 两帧: 首帧从旧滚动位置测量并回到顶部, 次帧补齐首行 */
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_DebugMeasured(md) == 3);
		CHECK(UiMarkdown_ContentHeight(md) > 90.0f);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}


done:
	if (md) UiMarkdown_Destroy(md);
	if (doc) HeapFree(GetProcessHeap(), 0, doc);
	if (g) {
		UiTheme_Shutdown(&g_theme, g);
		Gfx_Shutdown(g);
	}
	if (hwnd) DestroyWindow(hwnd);
	CoUninitialize();
	{
		char line[120];
		sprintf(line, "md_ui_test: %d passed, %d failed\n", g_pass, g_fail);
		out_(line);
	}
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
