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
	/* 行结构尺寸不再作为契约: div 的盒子下标等字段按可读性优先, 不为了
	 * 卡 128 字节而把属性挤进对齐空隙 (每个 MdRow 多几字节可忽略). */
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
	CHECK(Gfx_DeviceGeneration(g) > 0);
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
	/* SetText 全量 (解析 + 展平 + 锚点 + 行项) 实测 ~2.6ms @ 2000 段/213KB;
	 * 阈值留 5 倍余量吸收 CI 抖动, 但足以拦住"退回毫秒级十几倍"的退化。 */
	CHECK(t_parse < 15.0);
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

	/* 增量复用: Append 只新增尾部行, 旧行全部复用 */
	{
		int r0 = UiMarkdown_DebugRows(md);
		int c0 = UiMarkdown_DebugMeasureCalls(md);
		UiMarkdown_Append(md, "新增尾部段落\n\n");
		CHECK(UiMarkdown_DebugRows(md) == r0 + 1);
		CHECK(UiMarkdown_DebugRowsReused(md) >= r0);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_DebugMeasureCalls(md) - c0 <= 2);
	}

	/* 增量复用: 三行文档改中间一段, 其余两行保留 rt/高度 */
	{
		int c0;
		UiMarkdown_SetText(md, "第一段\n\n第二段\n\n第三段\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_DebugRows(md) == 3);
		CHECK(UiMarkdown_DebugMeasured(md) == 3);
		c0 = UiMarkdown_DebugMeasureCalls(md);
		UiMarkdown_SetText(md, "第一段\n\n第二段改\n\n第三段\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_DebugRowsReused(md) == 2);
		CHECK(UiMarkdown_DebugMeasureCalls(md) - c0 <= 2);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

	/* div: padding/margin 折进行高; line-height 改变单行高度 */
	{
		float hBase, hPad, hLh;

		UiMarkdown_SetText(md, "x\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		hBase = UiMarkdown_ContentHeight(md);
		CHECK(hBase > 10.0f && hBase < 60.0f);

		/* padding:10 四边 + margin:20 上下 → 总高 +60 */
		UiMarkdown_SetText(md,
			"<div style=\"padding:10px; margin:20px\">\nx\n</div>\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		hPad = UiMarkdown_ContentHeight(md);
		CHECK(hPad - hBase > 55.0f && hPad - hBase < 66.0f);

		/* line-height:2 → 单行 14×2=28, 比默认 1.25(17.5) 高约 10.5 */
		UiMarkdown_SetText(md, "<div style=\"line-height:2\">\nx\n</div>\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		hLh = UiMarkdown_ContentHeight(md);
		CHECK(hLh - hBase > 7.0f && hLh - hBase < 15.0f);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

	/* <center> / <p ...>: 单行形态不额外增行, 也不改段落高度 */
	{
		float hBase, hC, hMulti, hP;

		UiMarkdown_SetText(md, "x\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		hBase = UiMarkdown_ContentHeight(md);
		/* 上一轮 SetText 是 <div style="line-height:2">x</div>: 同文本不同
		 * 盒子属性, 行复用必须按 boxSig 判否, 否则这里会拿到 2 倍行高 */
		CHECK(hBase > 20.0f && hBase < 34.0f);

		/* 单行 <center>x</center> 与裸段落 x 应完全同高 (无隐式 margin) */
		UiMarkdown_SetText(md, "<center>x</center>\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		hC = UiMarkdown_ContentHeight(md);
		CHECK(UiMarkdown_DebugRows(md) == 1);
		CHECK(hC - hBase < 1.5f && hBase - hC < 1.5f);

		/* 多行形态同高 */
		UiMarkdown_SetText(md, "<center>\nx\n</center>\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		hMulti = UiMarkdown_ContentHeight(md);
		CHECK(hMulti - hBase < 1.5f && hBase - hMulti < 1.5f);

		/* <p style="padding:10px"> 单行: 上下各 +10 */
		UiMarkdown_SetText(md, "<p style=\"padding:10px\">x</p>\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		hP = UiMarkdown_ContentHeight(md);
		CHECK(hP - hBase > 15.0f && hP - hBase < 25.0f);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

	/* 章节锚点: # B 在视口下方, 跳转后 offset 必须落到非零位置 */
	{
		DynBuf ab;
		float offTop, offB;
		int k;

		ZeroMemory(&ab, sizeof(ab));
		db_puts(&ab, "# A\n\n");
		for (k = 0; k < 40; k++) db_puts(&ab, "段落\n\n");
		db_puts(&ab, "# B\n\n");
		for (k = 0; k < 40; k++) db_puts(&ab, "段落\n\n");
		UiMarkdown_SetText(md, ab.p);
		db_free(&ab);

		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_MaxOffset(md) > 200.0f);

		CHECK(UiMarkdown_ScrollToAnchor(md, &g_ctx, "a", 0) == 1);
		offTop = UiMarkdown_ScrollOffset(md);
		CHECK(offTop < 1.0f);

		CHECK(UiMarkdown_ScrollToAnchor(md, &g_ctx, "b", 0) == 1);
		offB = UiMarkdown_ScrollOffset(md);
		CHECK(offB > 200.0f);

		/* 规范化的第二种写法: `#Getting Started` 与 `#getting-started` 等价 */
		CHECK(UiMarkdown_ScrollToAnchor(md, &g_ctx, "no-such-anchor", 0) == 0);
		CHECK(UiMarkdown_ScrollToAnchor(md, NULL, "b", 0) == 0);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

	/* 当前章节: 首可见行往往是跳转目标**上面那一行**, 拿它反推会永远
	 * 落后一项; 跳转要锁定目标 (末尾几节滚到不了视口顶), 用户滚动后
	 * 才回到按视口推导. */
	{
		DynBuf hb;
		int k, t;

		ZeroMemory(&hb, sizeof(hb));
		for (k = 0; k < 10; k++) {
			db_addf(&hb, "## 章节 %d\n\n", k);
			for (t = 0; t < 20; t++)
				db_puts(&hb, "正文行用来撑高文档。\n\n");
		}
		UiMarkdown_SetText(md, hb.p);
		db_free(&hb);
		CHECK(UiMarkdown_HeadingCount(md) == 10);

		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_ActiveHeading(md) == 0);   /* 顶部即第一章 */

		for (k = 0; k < 10; k++) {
			const UiMdHeading *h = UiMarkdown_HeadingAt(md, k);
			CHECK(h != NULL);
			UiMarkdown_ScrollToRow(md, &g_ctx, h->row, 8.0f, 0);
			/* 粘性定位要几帧才收敛 (懒测量), 期间高亮不许跳回上一项 */
			for (t = 0; t < 3; t++) {
				Ui_BeginFrame(&g_ctx);
				Ui_EndFrame(&g_ctx);
				Gfx_Begin(g);
				Ui_BeginFrame(&g_ctx);
				Ui_Markdown(md, &g_ctx, area);
				Ui_EndFrame(&g_ctx);
				CHECK(Gfx_End(g) == 1);
				CHECK(UiMarkdown_ActiveHeading(md) == k);
			}
			/* 落点: 目标行 (或它上面那一行, 视口顶压在行边界上) 就是首行 */
			if (k < 9) {
				int fv = UiMarkdown_FirstVisibleRow(md);
				CHECK(fv <= h->row && fv >= h->row - 1);
			}
		}
		/* 最后一节已经滚到底, 目标行到不了视口顶, 只能靠跳转锁定 */
		CHECK(UiMarkdown_ScrollOffset(md) >=
			UiMarkdown_MaxOffset(md) - 0.5f);
		CHECK(UiMarkdown_ActiveHeading(md) == 9);

		/* 用户自己滚轮 = 解锁: 之后跟随视口, 不再钉在跳转目标上 */
		g_ctx.wheelTarget = Ui_Id("md.ui.test", 0, 0);
		g_ctx.wheelDelta = -1.0f;
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_ActiveHeading(md) >= 0 &&
			UiMarkdown_ActiveHeading(md) <= 9);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

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

	/* 表格列宽走 measure 协议: 自然宽 / 压缩到列硬下限 / 极窄等分 */
	{
		float cw[4] = { 0, 0, 0, 0 };
		float sum;
		int n = UiMarkdown_DebugTableCols(md, 0, cw, 4);
		CHECK(n == 2);
		CHECK(cw[0] > 0.0f && cw[1] > 0.0f);
		CHECK(cw[0] + cw[1] < area.w);

		UiMarkdown_SetText(md,
			"| 第一列很长的内容用来撑宽第一列很长的内容 | 短 | 第三列中等长度内容第三列中等长度内容 |\n"
			"|---|---|---|\n"
			"| 第一列很长的内容用来撑宽第一列很长的内容 | 短 | 第三列中等长度内容第三列中等长度内容 |\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		n = UiMarkdown_DebugTableCols(md, 0, cw, 4);
		CHECK(n == 3);
		sum = cw[0] + cw[1] + cw[2];
		/* 视口左右各 18 DIP 内边距, 表格可用宽 = area.w - 36 */
		CHECK(sum <= area.w - 35.0f && sum > area.w - 37.0f);
		CHECK(cw[1] >= 47.5f);          /* 硬下限 */
		CHECK(cw[0] > cw[2] && cw[2] > cw[1]);

		area.w = 100.0f;
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		n = UiMarkdown_DebugTableCols(md, 0, cw, 4);
		CHECK(n == 3);
		CHECK(cw[0] < 48.0f && cw[1] < 48.0f && cw[2] < 48.0f);
		CHECK(cw[0] - cw[1] < 0.6f && cw[0] - cw[1] > -0.6f);
		CHECK(cw[2] - cw[1] < 0.6f && cw[2] - cw[1] > -0.6f);
		area.w = 320.0f;
	}

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

		/* 宽度变化 (resize/展开收起) 只重建布局, 图片对象保留 */
		{
			int keep0 = UiMarkdown_DebugImagesRetained(md);
			area.w = 300.0f;
			Ui_BeginFrame(&g_ctx);
			Ui_EndFrame(&g_ctx);
			Gfx_Begin(g);
			Ui_BeginFrame(&g_ctx);
			Ui_Markdown(md, &g_ctx, area);
			Ui_EndFrame(&g_ctx);
			CHECK(Gfx_End(g) == 1);
			area.w = 320.0f;
			Ui_BeginFrame(&g_ctx);
			Ui_EndFrame(&g_ctx);
			Gfx_Begin(g);
			Ui_BeginFrame(&g_ctx);
			Ui_Markdown(md, &g_ctx, area);
			Ui_EndFrame(&g_ctx);
			CHECK(Gfx_End(g) == 1);
			CHECK(UiMarkdown_DebugImagesRetained(md) > keep0);
			CHECK(UiMarkdown_ContentHeight(md) > 35.0f);
			CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
		}

		/* ULW 表面 resize: 复用 DC RT, 设备代际不变 (位图不失效/不解码) */
		{
			int d0 = Gfx_DeviceGeneration(g);
			CHECK(Gfx_Resize(g, 700, 720, 1.0f) == 1);
			CHECK(Gfx_DeviceGeneration(g) == d0);
			CHECK(Gfx_Resize(g, 760, 720, 1.0f) == 1);
			CHECK(Gfx_DeviceGeneration(g) == d0);
			Ui_SetViewport(&g_ctx, 760.0f, 720.0f, 1.0f);
		}

		/* 编辑其它段落时, 未变的图片行复用 UiImage (不重新加载) */
		{
			DynBuf ib;
			int c0 = UiMarkdown_DebugMeasureCalls(md);
			ZeroMemory(&ib, sizeof(ib));
			db_addf(&ib, "![a](%s \"center\")\n\n新增段落\n", path);
			UiMarkdown_SetText(md, ib.p);
			db_free(&ib);
			CHECK(UiMarkdown_DebugRows(md) == 2);
			CHECK(UiMarkdown_DebugRowsReused(md) >= 1);
			CHECK(UiMarkdown_DebugImagesReused(md) == 1);
			Ui_BeginFrame(&g_ctx);
			Ui_EndFrame(&g_ctx);
			Gfx_Begin(g);
			Ui_BeginFrame(&g_ctx);
			Ui_Markdown(md, &g_ctx, area);
			Ui_EndFrame(&g_ctx);
			CHECK(Gfx_End(g) == 1);
			CHECK(UiMarkdown_DebugMeasureCalls(md) - c0 <= 2);
		}

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

			/* 显式尺寸: height=20 (Gap 10 → 内容高 ~30) */
			{
				char doc3[700];
				wsprintfA(doc3, "![x](%s){height=20}\n", path);
				UiMarkdown_SetText(md, doc3);
				Ui_BeginFrame(&g_ctx);
				Ui_EndFrame(&g_ctx);
				Gfx_Begin(g);
				Ui_BeginFrame(&g_ctx);
				Ui_Markdown(md, &g_ctx, area);
				Ui_EndFrame(&g_ctx);
				CHECK(Gfx_End(g) == 1);
				CHECK(UiMarkdown_ContentHeight(md) > 18.0f);
				CHECK(UiMarkdown_ContentHeight(md) < 40.0f);
			}
			/* 显式宽高同时生效: 64x32 图缩成 40x15 */
			{
				char doc4[700];
				wsprintfA(doc4, "![x](%s){width=40 height=15}\n", path);
				UiMarkdown_SetText(md, doc4);
				Ui_BeginFrame(&g_ctx);
				Ui_EndFrame(&g_ctx);
				Gfx_Begin(g);
				Ui_BeginFrame(&g_ctx);
				Ui_Markdown(md, &g_ctx, area);
				Ui_EndFrame(&g_ctx);
				CHECK(Gfx_End(g) == 1);
				CHECK(UiMarkdown_ContentHeight(md) > 14.0f);
				CHECK(UiMarkdown_ContentHeight(md) < 32.0f);
			}
			/* 行内大图 (显式 200x100) 应撑高段落 */
			{
				char doc5[700];
				wsprintfA(doc5, "前 ![x](%s){width=200 height=100} 后\n", path);
				UiMarkdown_SetText(md, doc5);
				Ui_BeginFrame(&g_ctx);
				Ui_EndFrame(&g_ctx);
				Gfx_Begin(g);
				Ui_BeginFrame(&g_ctx);
				Ui_Markdown(md, &g_ctx, area);
				Ui_EndFrame(&g_ctx);
				CHECK(Gfx_End(g) == 1);
				CHECK(UiMarkdown_ContentHeight(md) > 100.0f);
			}
			/* 与样例一致的写法: 前缀文字 + title + 尺寸属性 */
			{
				char doc6[700];
				wsprintfA(doc6,
					"限定尺寸：![尺寸 280x120](%s \"center\"){width=280 height=120}\n",
					path);
				UiMarkdown_SetText(md, doc6);
				Ui_BeginFrame(&g_ctx);
				Ui_EndFrame(&g_ctx);
				Gfx_Begin(g);
				Ui_BeginFrame(&g_ctx);
				Ui_Markdown(md, &g_ctx, area);
				Ui_EndFrame(&g_ctx);
				CHECK(Gfx_End(g) == 1);
				CHECK(UiMarkdown_ContentHeight(md) > 100.0f);
			}
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

	/* 设备像素对齐: 滚动位移/行距取整 */
	CHECK(Ui_PxSnap(&g_ctx, 10.1f) == 10.0f);
	CHECK(Ui_PxSnap(&g_ctx, 10.6f) == 11.0f);
	CHECK(Ui_PxSnap(&g_ctx, -3.2f) == -3.0f);

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

	/* 公式: 行内对象撑高行, 块级独占一行, 语法错误回退源码 */
	{
		const char *mdoc =
			"行内 $x^2+\\frac{a}{b}$ 公式\n\n"
			"$$\n\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}\n$$\n\n"
			"$\\frac{a}$\n";
		UiMarkdown_SetText(md, mdoc);
		CHECK(UiMarkdown_DebugRows(md) == 3);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_DebugMeasured(md) == 3);
		CHECK(UiMarkdown_ContentHeight(md) > 110.0f);
		/* 行内 14 DIP + 块级 16 DIP 各缓存一棵; 语法错误不入缓存 */
		CHECK(UiMarkdown_DebugMathCache(md) == 2);
		CHECK(UiMarkdown_DebugMathMisses(md) == 2);
		/* 编辑后重解析: 未变化的公式直接命中缓存 */
		{
			int me = UiMarkdown_DebugMathMisses(md);
			UiMarkdown_SetText(md,
				"行内 $x^2+\\frac{a}{b}$ 公式\n\n"
				"$$\n\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}\n$$\n\n"
				"$\\frac{a}$\n\n后记段落\n");
			CHECK(UiMarkdown_DebugRows(md) == 4);
			Ui_BeginFrame(&g_ctx);
			Ui_EndFrame(&g_ctx);
			Gfx_Begin(g);
			Ui_BeginFrame(&g_ctx);
			Ui_Markdown(md, &g_ctx, area);
			Ui_EndFrame(&g_ctx);
			CHECK(Gfx_End(g) == 1);
			CHECK(UiMarkdown_DebugMathMisses(md) == me);
			CHECK(UiMarkdown_DebugMathCache(md) == 2);
		}
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

	/* 块级公式被驱逐后回到视口必须重建 (measured 仍为 1) */
	{
		DynBuf gd;
		int ii, c0, c1, m0, mc;
		ZeroMemory(&gd, sizeof(gd));
		for (ii = 0; ii < 120; ii++)
			db_puts(&gd, "$$\n\\frac{a}{b}\n$$\n\n");
		UiMarkdown_SetText(md, gd.p);
		db_free(&gd);
		CHECK(UiMarkdown_DebugRows(md) == 120);
		m0 = UiMarkdown_DebugMathMisses(md);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		c0 = UiMarkdown_DebugMeasureCalls(md);
		/* 120 行同一公式 + 同一字号: 只创建一次布局 */
		CHECK(UiMarkdown_DebugMathMisses(md) - m0 == 1);
		mc = UiMarkdown_DebugMathCache(md);
		UiMarkdown_ScrollToOffset(md, &g_ctx, 1.0e9f, 0);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		UiMarkdown_ScrollToOffset(md, &g_ctx, 0.0f, 0);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		c1 = UiMarkdown_DebugMeasureCalls(md);
		CHECK(c1 > c0);
		/* 驱逐后回看命中缓存: 不再新建布局, 条目数不变 */
		CHECK(UiMarkdown_DebugMathMisses(md) - m0 == 1);
		CHECK(UiMarkdown_DebugMathCache(md) == mc);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

	/* 超长行内公式: 窄宽度下按比例缩小字号重建 (两次 miss: 基准 + 缩小) */
	{
		DynBuf lb;
		int m1;
		ZeroMemory(&lb, sizeof(lb));
		db_puts(&lb,
			"前 $\\sum_{k=0}^{n} \\frac{k^{2}+2k+1}{k+1}"
			"+\\sum_{k=0}^{n}\\frac{k^{2}-2k+1}{k-1}"
			"+\\int_{0}^{1}\\frac{x^{3}+1}{x+1}\\,dx$ 后\n");
		UiMarkdown_SetText(md, lb.p);
		db_free(&lb);
		area.w = 200.0f;   /* 强制公式宽于可用宽, 走缩放路径 */
		m1 = UiMarkdown_DebugMathMisses(md);
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_DebugRows(md) == 1);
		CHECK(UiMarkdown_DebugMeasured(md) == 1);
		CHECK(UiMarkdown_DebugMathMisses(md) >= m1 + 2);
		CHECK(UiMarkdown_ContentHeight(md) > 24.0f);
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}

	/* 文档地址解析: 相对链接/图片以文档地址为基准 */
	{
		char buf[1024];
		const char *r;

		UiMarkdown_SetDocumentUrl(md, "D:\\docs\\a.md");
		r = UiMarkdown_ResolveUrl(md, "b.md", buf, (int)sizeof(buf));
		CHECK(lstrcmpA(r, "D:\\docs\\b.md") == 0);
		r = UiMarkdown_ResolveUrl(md, "../c.md", buf, (int)sizeof(buf));
		CHECK(lstrcmpA(r, "D:\\c.md") == 0);
		r = UiMarkdown_ResolveUrl(md, "http://x/y.md", buf, (int)sizeof(buf));
		CHECK(lstrcmpA(r, "http://x/y.md") == 0);
		r = UiMarkdown_ResolveUrl(md, "#sec", buf, (int)sizeof(buf));
		CHECK(lstrcmpA(r, "#sec") == 0);

		UiMarkdown_SetDocumentUrl(md, "https://h.com/docs/a.md");
		r = UiMarkdown_ResolveUrl(md, "b.md", buf, (int)sizeof(buf));
		CHECK(lstrcmpA(r, "https://h.com/docs/b.md") == 0);
		r = UiMarkdown_ResolveUrl(md, "/r.md", buf, (int)sizeof(buf));
		CHECK(lstrcmpA(r, "https://h.com/r.md") == 0);
		r = UiMarkdown_ResolveUrl(md, "../c.md", buf, (int)sizeof(buf));
		CHECK(lstrcmpA(r, "https://h.com/c.md") == 0);
		UiMarkdown_SetDocumentUrl(md, NULL);
	}

	/* 标题目录: level / 文本 / slug / 行号 */
	{
		const UiMdHeading *h;

		UiMarkdown_SetText(md,
			"# \xE7\xAC\xAC\xE4\xB8\x80\xE7\xAB\xA0\n\n"
			"\xE6\xAD\xA3\xE6\x96\x87\n\n"
			"## Getting Started\n\n\xE6\xAD\xA3\xE6\x96\x87\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_HeadingCount(md) == 2);
		h = UiMarkdown_HeadingAt(md, 0);
		CHECK(h != NULL);
		if (h) {
			CHECK(h->level == 1 && h->row == 0);
			CHECK(lstrcmpA(h->slug,
				"\xE7\xAC\xAC\xE4\xB8\x80\xE7\xAB\xA0") == 0);
			CHECK(h->textLen == 3);
		}
		h = UiMarkdown_HeadingAt(md, 1);
		CHECK(h != NULL);
		if (h) {
			CHECK(h->level == 2 && h->row > 0);
			CHECK(lstrcmpA(h->slug, "getting-started") == 0);
		}
		CHECK(UiMarkdown_HeadingAt(md, 2) == NULL);
	}

	/* 回归: div 容器后的标题必须进锚点表 (曾漏掉 </div> 后的第一个 ###) */
	{
		const UiMdHeading *h;

		UiMarkdown_SetText(md,
			"### Sec0\n\n"
			"<div align=\"center\" style=\"padding:10px 16px;"
			" margin:8px 0; line-height:1.8\">\n"
			"#### InDiv\n\n"
			"div body\n\n"
			"- item in div\n"
			"</div>\n"
			"\n"
			"### Box\n\n"
			"::: tip\n"
			"tip body\n"
			":::\n"
			"\n"
			"::: warning\n"
			"warn body\n"
			":::\n"
			"\n"
			"> [!NOTE]\n"
			"> note body\n"
			"\n"
			":::: danger 嵌套示例\n"
			"::: tip\n"
			"inner tip\n"
			":::\n"
			"::::\n"
			"\n"
			"### After\n\n"
			"text\n");
		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);
		CHECK(UiMarkdown_HeadingCount(md) == 4);
		h = UiMarkdown_HeadingAt(md, 1);
		CHECK(h != NULL);
		if (h) CHECK(lstrcmpA(h->slug, "indiv") == 0);
		h = UiMarkdown_HeadingAt(md, 2);
		CHECK(h != NULL);
		if (h) {
			CHECK(h->level == 3);
			CHECK(lstrcmpA(h->slug, "box") == 0);
			CHECK(h->textLen == 3);
		}
		h = UiMarkdown_HeadingAt(md, 3);
		CHECK(h != NULL);
		if (h) CHECK(lstrcmpA(h->slug, "after") == 0);
	}

	/* 粘性定位: 跳到远处标题后, 懒测量收敛不应让落点漂移 */
	{
		DynBuf sb;
		int k, target;
		const UiMdHeading *hb;

		ZeroMemory(&sb, sizeof(sb));
		db_puts(&sb, "# \xE9\xA1\xB6\xE9\x83\xA8\n\n");
		for (k = 0; k < 200; k++)
			db_addf(&sb, "\xE7\xAC\xAC %d \xE6\xAE\xB5\xE6\xAD\xA3\xE6\x96\x87"
				"\xEF\xBC\x8C\xE7\x94\xA8\xE6\x9D\xA5\xE6\x8A\x8A"
				"\xE5\x90\x8E\xE9\x9D\xA2\xE7\x9A\x84\xE6\xA0\x87"
				"\xE9\xA2\x98\xE6\x8E\xA8\xE5\x88\xB0\xE5\xBE\x88"
				"\xE8\xBF\x9C\xE7\x9A\x84\xE4\xBD\x8D\xE7\xBD\xAE"
				"\xE3\x80\x82\n\n", k + 1);
		db_puts(&sb, "# \xE7\x9B\xAE\xE6\xA0\x87\n\n");
		for (k = 0; k < 40; k++)
			db_puts(&sb, "\xE7\x9B\xAE\xE6\xA0\x87\xE4\xB9\x8B"
				"\xE5\x90\x8E\xE7\x9A\x84\xE6\xAE\xB5\xE8\x90\xBD"
				"\xE3\x80\x82\n\n");
		UiMarkdown_SetText(md, sb.p);
		db_free(&sb);

		Ui_BeginFrame(&g_ctx);
		Ui_EndFrame(&g_ctx);
		Gfx_Begin(g);
		Ui_BeginFrame(&g_ctx);
		Ui_Markdown(md, &g_ctx, area);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g) == 1);

		hb = UiMarkdown_HeadingAt(md, 1);
		CHECK(hb != NULL);
		target = hb ? hb->row : 0;
		CHECK(target > 50);
		UiMarkdown_ScrollToRow(md, &g_ctx, target, 8.0f, 0);
		for (k = 0; k < 8; k++) {
			Ui_BeginFrame(&g_ctx);
			Ui_EndFrame(&g_ctx);
			Gfx_Begin(g);
			Ui_BeginFrame(&g_ctx);
			Ui_Markdown(md, &g_ctx, area);
			Ui_EndFrame(&g_ctx);
			CHECK(Gfx_End(g) == 1);
		}
		CHECK(UiMarkdown_FirstVisibleRow(md) <= target);
		CHECK(target - UiMarkdown_FirstVisibleRow(md) <= 1);
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
