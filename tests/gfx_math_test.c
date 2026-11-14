/*
 * gfx_math_test.c - 数学公式矢量绘制隐藏窗口断言 (0CRT)
 *
 * 覆盖: 布局尺寸、单帧绘制、字号缩放、矩阵/对齐环境、错误回退.
 * 构建: bin\cui_gfx_math_test.exe; 退出码 0 = 全通过.
 */
#include <windows.h>
#include "gfx/gfx.h"
#include "gfx/gfx_math.h"
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

static void *g_font;

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProcW(h, m, w, l);
}

void __stdcall startup(void)
{
	WNDCLASSEXW wc;
	HWND hwnd = NULL;
	Gfx *g = NULL;
	GfxFont *body = NULL;
	GfxMath *sm = NULL, *big = NULL, *matrix = NULL, *aligned = NULL;
	GfxMath *dsum = NULL;
	char line[160];

	Log_Init();
	SetEnvironmentVariableA("SCV2_ULW", "1");

	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"cui_gfx_math_test";
	CHECK(RegisterClassExW(&wc) != 0);
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_gfx_math_test", L"", WS_POPUP, 0, 0, 640, 480,
		NULL, NULL, wc.hInstance, NULL);
	CHECK(hwnd != NULL);
	if (!hwnd) goto done;

	CHECK(Gfx_Init(&g, hwnd) == 1);
	if (!g) goto done;
	CHECK(Gfx_Resize(g, 640, 480, 1.0f) == 1);
	CHECK(Gfx_Ready(g) == 1);

	body = Gfx_CreateFont(g, 16.0f, 400);
	CHECK(body != NULL);
	g_font = body;
	if (!body) goto done;

	sm = Gfx_MathCreate(g, "x^2+\\frac{a}{b}", 15, 16.0f, body, 0);
	CHECK(sm != NULL);
	if (sm) {
		CHECK(Gfx_MathWidth(sm) > 0.0f);
		CHECK(Gfx_MathHeight(sm) > 0.0f);
		CHECK(Gfx_MathDepth(sm) > 0.0f);
	}

	big = Gfx_MathCreate(g, "x^2+\\frac{a}{b}", 15, 32.0f, body, 0);
	CHECK(big != NULL);
	if (sm && big)
		CHECK(Gfx_MathWidth(big) > Gfx_MathWidth(sm) * 1.8f);

	matrix = Gfx_MathCreate(g,
		"\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}",
		(int)strlen("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}"), 16.0f, body, 1);
	CHECK(matrix != NULL);
	aligned = Gfx_MathCreate(g,
		"\\begin{aligned}f(x)&=x^2\\\\g(x)&=\\sqrt{x}\\end{aligned}",
		(int)strlen("\\begin{aligned}f(x)&=x^2\\\\g(x)&=\\sqrt{x}\\end{aligned}"),
		16.0f, body, 1);
	CHECK(aligned != NULL);
	dsum = Gfx_MathCreate(g, "\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}",
		(int)strlen("\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}"), 16.0f, body, 1);
	CHECK(dsum != NULL);
	if (dsum) CHECK(Gfx_MathHeight(dsum) > 16.0f);

	CHECK(Gfx_MathCreate(g, "\\frac{a}", 8, 16.0f, body, 0) == NULL);
	CHECK(Gfx_MathCreate(g, "\\foo", 4, 16.0f, body, 0) == NULL);
	CHECK(Gfx_MathCreate(g, "", 0, 16.0f, body, 0) == NULL);

	if (sm && matrix && aligned && dsum) {
		Gfx_Begin(g);
		Gfx_Clear(g, 0xFFF5F5F5);
		Gfx_MathDraw(sm, g, 20.0f, 60.0f, 0xFF202020);
		Gfx_MathDraw(matrix, g, 20.0f, 140.0f, 0xFF202020);
		Gfx_MathDraw(aligned, g, 20.0f, 260.0f, 0xFF202020);
		Gfx_MathDraw(dsum, g, 20.0f, 380.0f, 0xFF202020);
		CHECK(Gfx_End(g) == 1);
	}

done:
	if (sm) Gfx_MathDestroy(sm);
	if (big) Gfx_MathDestroy(big);
	if (dsum) Gfx_MathDestroy(dsum);
	if (matrix) Gfx_MathDestroy(matrix);
	if (aligned) Gfx_MathDestroy(aligned);
	if (body) Gfx_DestroyFont(g, body);
	if (g) Gfx_Shutdown(g);
	if (hwnd) DestroyWindow(hwnd);

	sprintf(line, "gfx_math_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
