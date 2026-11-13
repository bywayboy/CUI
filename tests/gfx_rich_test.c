/*
 * gfx_rich_test.c - 富文本模块隐藏窗口断言 (0CRT 控制台)
 *
 * 覆盖: 布局创建/测量(换行)/行距/范围样式/RangeRects/HitTest/实际绘制。
 * 构建: bin\cui_gfx_rich_test.exe; 退出码 0=全通过。
 * 依赖: gfx_rich.obj (可选模块, 不进入 CORE_OBJS)。
 */
#include <windows.h>
#include "gfx/gfx.h"
#include "gfx/gfx_rich.h"
#include "log.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);

static int g_pass, g_fail;

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

void __stdcall startup(void)
{
	static const wchar_t txt[] =
		L"Hello World, 这是一段用于富文本排版测试的中文文本。";
	char line[160];
	WNDCLASSEXW wc;
	HWND hwnd = NULL;
	Gfx *g = NULL;
	GfxFont *body = NULL;
	GfxRichText *narrow = NULL;
	GfxRichText *wide = NULL;
	GfxRichText *spaced = NULL;
	GfxRect rr[8];
	int len, n, pos;

	Log_Init();
	/* 强制 ULW: 隐藏窗口也能完成一帧绘制, 避免测试依赖 DComp 合成。 */
	SetEnvironmentVariableA("SCV2_ULW", "1");

	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"cui_gfx_rich_test";
	CHECK(RegisterClassExW(&wc) != 0);
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_gfx_rich_test", L"", WS_POPUP, 0, 0, 800, 600,
		NULL, NULL, wc.hInstance, NULL);
	CHECK(hwnd != NULL);
	if (!hwnd) goto done;

	CHECK(Gfx_Init(&g, hwnd) == 1);
	if (!g) goto done;
	CHECK(Gfx_Resize(g, 800, 600, 1.0f) == 1);
	CHECK(Gfx_Ready(g) == 1);

	body = Gfx_CreateFont(g, 16.0f, 400);
	CHECK(body != NULL);
	if (!body) goto done;

	len = lstrlenW(txt);
	narrow = Gfx_RichCreate(g, txt, len, body, 180.0f, 0.0f, GFX_RICH_WRAP);
	wide = Gfx_RichCreate(g, txt, len, body, 2000.0f, 0.0f, GFX_RICH_WRAP);
	spaced = Gfx_RichCreate(g, txt, len, body, 2000.0f, 3.0f, GFX_RICH_WRAP);
	CHECK(narrow != NULL);
	CHECK(wide != NULL);
	CHECK(spaced != NULL);

	if (narrow && wide && spaced) {
		float hNarrow = Gfx_RichHeight(narrow);
		float hWide = Gfx_RichHeight(wide);
		float hSpaced = Gfx_RichHeight(spaced);

		CHECK(Gfx_RichWidth(narrow) > 0.0f);
		CHECK(Gfx_RichWidth(narrow) <= 181.0f);
		CHECK(hNarrow > hWide);          /* 窄宽必须换行 */
		CHECK(hWide > 0.0f);
		CHECK(hSpaced > hWide * 1.8f);   /* 行距倍数生效 */

		Gfx_RichBold(wide, 0, 5, 1);
		Gfx_RichItalic(wide, 6, 5, 1);
		Gfx_RichUnderline(wide, 0, 11, 1);
		Gfx_RichStrike(wide, 12, 5, 1);
		Gfx_RichColor(wide, 0, 5, 0xFFC03030);
		Gfx_RichBold(wide, -100, 1000, 1);   /* 越界范围: 裁剪不崩溃 */

		n = Gfx_RichRangeRects(wide, 0, 11, rr, 8);
		CHECK(n >= 1);
		CHECK(rr[0].w > 0.0f && rr[0].h > 0.0f);
		CHECK(Gfx_RichRangeRects(wide, len, 0, rr, 8) == 0);

		pos = -1;
		CHECK(Gfx_RichHitTest(wide, 2.0f, hWide * 0.5f, &pos) == 1);
		CHECK(pos >= 0 && pos <= len);
		CHECK(Gfx_RichHitTest(wide, -1.0f, 0.0f, &pos) == 0);

		/* 行内图片: 应参与行高 */
		{
			unsigned char px[8 * 8 * 4];
			int i;
			GfxBitmap *bmp;
			GfxRichText *ir;
			float h0;
			for (i = 0; i < (int)sizeof(px); i++) px[i] = (unsigned char)(i * 7);
			bmp = Gfx_CreateBitmap(g, 8, 8, px);
			CHECK(bmp != NULL);
			ir = Gfx_RichCreate(g, L"ab", 2, body, 400.0f, 0.0f,
				GFX_RICH_WRAP);
			CHECK(ir != NULL);
			if (ir) {
				int irc;
				h0 = Gfx_RichHeight(ir);
				irc = Gfx_RichInlineImage(ir, 0, 2, bmp, 48.0f, 32.0f);
				CHECK(irc == 1);
				CHECK(Gfx_RichHeight(ir) >= 30.0f);
				CHECK(Gfx_RichHeight(ir) > h0);
				Gfx_RichDestroy(ir);
			}
			if (bmp) Gfx_DestroyBitmap(g, bmp);
		}

		Gfx_Begin(g);
		Gfx_Clear(g, 0xFFF5F5F5);
		Gfx_RichDraw(narrow, g, 20.0f, 20.0f, 0xFF202020);
		Gfx_RichDraw(wide, g, 20.0f, 300.0f, 0xFF202020);
		CHECK(Gfx_End(g) == 1);
	}

done:
	if (narrow) Gfx_RichDestroy(narrow);
	if (wide) Gfx_RichDestroy(wide);
	if (spaced) Gfx_RichDestroy(spaced);
	if (body) Gfx_DestroyFont(g, body);
	if (g) Gfx_Shutdown(g);
	if (hwnd) DestroyWindow(hwnd);

	sprintf(line, "gfx_rich_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
