/*
 * gfx_path_test.c - 路径 / 描边样式 / 视口变换 / 渐变画刷 隐藏窗口断言
 *
 * 覆盖: 直线/三次贝塞尔包围盒, SVG 弧 (sweep/large 方向), 退化弧,
 *       Reset/空路径, 合成绘制 (填充/虚线描边/渐变/变换栈).
 * 构建: bin\cui_gfx_path_test.exe; 退出码 0 = 全通过.
 */
#include <windows.h>
#include "gfx/gfx.h"
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

#define NEAR(a, b, tol) (((a) > (b) ? (a) - (b) : (b) - (a)) <= (tol))

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProcW(h, m, w, l);
}

void __stdcall startup(void)
{
	WNDCLASSEXW wc;
	HWND hwnd = NULL;
	Gfx *g = NULL;
	GfxPath *p = NULL;
	GfxBrush *solid = NULL, *grad = NULL;
	GfxGradStop stops[2];
	GfxStrokeStyle dash;
	float dashes[2] = { 6.0f, 4.0f };
	GfxRect bb, gbox, clipRc;
	char line[160];

	Log_Init();
	SetEnvironmentVariableA("SCV2_ULW", "1");

	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"cui_gfx_path_test";
	CHECK(RegisterClassExW(&wc) != 0);
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_gfx_path_test", L"", WS_POPUP, 0, 0, 640, 480,
		NULL, NULL, wc.hInstance, NULL);
	CHECK(hwnd != NULL);
	if (!hwnd) goto done;

	CHECK(Gfx_Init(&g, hwnd) == 1);
	if (!g) goto done;
	CHECK(Gfx_Resize(g, 640, 480, 1.0f) == 1);
	CHECK(Gfx_Ready(g) == 1);

	p = Gfx_PathCreate(g);
	CHECK(p != NULL);
	if (!p) goto done;

	/* 空路径 */
	bb = Gfx_PathBounds(g, p);
	CHECK(bb.w == 0.0f && bb.h == 0.0f);

	/* 直线矩形 */
	Gfx_PathMoveTo(p, 100.0f, 50.0f);
	Gfx_PathLineTo(p, 200.0f, 50.0f);
	Gfx_PathLineTo(p, 200.0f, 150.0f);
	Gfx_PathLineTo(p, 100.0f, 150.0f);
	Gfx_PathClose(p);
	bb = Gfx_PathBounds(g, p);
	CHECK(NEAR(bb.x, 100.0f, 0.01f) && NEAR(bb.y, 50.0f, 0.01f));
	CHECK(NEAR(bb.w, 100.0f, 0.01f) && NEAR(bb.h, 100.0f, 0.01f));

	/* 三次贝塞尔: y 最大 = 0.75*100 */
	Gfx_PathReset(p);
	Gfx_PathMoveTo(p, 0.0f, 0.0f);
	Gfx_PathCubicTo(p, 0.0f, 100.0f, 100.0f, 100.0f, 100.0f, 0.0f);
	bb = Gfx_PathBounds(g, p);
	CHECK(NEAR(bb.x, 0.0f, 0.01f) && NEAR(bb.y, 0.0f, 0.01f));
	CHECK(NEAR(bb.w, 100.0f, 0.01f) && NEAR(bb.h, 75.0f, 0.1f));

	/* 圆弧 sweep=1 large=0: D2D 对弧返回整椭圆包围盒 (保守) */
	Gfx_PathReset(p);
	Gfx_PathMoveTo(p, 100.0f, 100.0f);
	Gfx_PathArcTo(p, 50.0f, 50.0f, 0.0f, 0, 1, 150.0f, 150.0f);
	bb = Gfx_PathBounds(g, p);
	CHECK(NEAR(bb.x, 100.0f, 0.05f) && NEAR(bb.y, 100.0f, 0.05f));
	CHECK(NEAR(bb.w, 50.0f, 0.05f) && NEAR(bb.h, 50.0f, 0.05f));

	/* 同端点 sweep=0: 镜像弧, 包围盒同样落在同一整椭圆 */
	Gfx_PathReset(p);
	Gfx_PathMoveTo(p, 100.0f, 100.0f);
	Gfx_PathArcTo(p, 50.0f, 50.0f, 0.0f, 0, 0, 150.0f, 150.0f);
	bb = Gfx_PathBounds(g, p);
	CHECK(NEAR(bb.x, 100.0f, 0.05f) && NEAR(bb.y, 100.0f, 0.05f));
	CHECK(NEAR(bb.w, 50.0f, 0.05f) && NEAR(bb.h, 50.0f, 0.05f));

	/* sweep=1 large=1: 大弧绕另一圆心 (150,100), 整圆包围盒 {100,50,100,100} */
	Gfx_PathReset(p);
	Gfx_PathMoveTo(p, 100.0f, 100.0f);
	Gfx_PathArcTo(p, 50.0f, 50.0f, 0.0f, 1, 1, 150.0f, 150.0f);
	bb = Gfx_PathBounds(g, p);
	CHECK(NEAR(bb.x, 100.0f, 0.05f) && NEAR(bb.y, 50.0f, 0.05f));
	CHECK(NEAR(bb.w, 100.0f, 0.05f) && NEAR(bb.h, 100.0f, 0.05f));

	/* 零半径退化为直线 */
	Gfx_PathReset(p);
	Gfx_PathMoveTo(p, 10.0f, 20.0f);
	Gfx_PathArcTo(p, 0.0f, 0.0f, 0.0f, 0, 1, 60.0f, 80.0f);
	bb = Gfx_PathBounds(g, p);
	CHECK(NEAR(bb.x, 10.0f, 0.01f) && NEAR(bb.y, 20.0f, 0.01f));
	CHECK(NEAR(bb.w, 50.0f, 0.01f) && NEAR(bb.h, 60.0f, 0.01f));

	/* 画刷描述对象 */
	solid = Gfx_SolidBrush(g, 0xFF202020);
	CHECK(solid != NULL);
	stops[0].offset = 0.0f; stops[0].color = 0xFF4C8BF5;
	stops[1].offset = 1.0f; stops[1].color = 0xFF8226F5;
	gbox.x = 0.0f; gbox.y = 0.0f; gbox.w = 200.0f; gbox.h = 100.0f;
	grad = Gfx_LinearGradientBrush(g, gbox, 90.0f, stops, 2);
	CHECK(grad != NULL);
	gbox.w = 10.0f; gbox.h = 10.0f;
	CHECK(Gfx_LinearGradientBrush(g, gbox, 0.0f, stops, 1) == NULL);

	dash.cap = GFX_CAP_ROUND;
	dash.join = GFX_JOIN_ROUND;
	dash.miterLimit = 4.0f;
	dash.dashes = dashes;
	dash.dashCount = 2;

	/* 合成绘制: 填充 + 虚线描边 + 渐变 + 变换栈 */
	clipRc.x = 0.0f; clipRc.y = 0.0f; clipRc.w = 400.0f; clipRc.h = 400.0f;
	Gfx_Begin(g);
	Gfx_Clear(g, 0xFFF5F5F5);
	Gfx_PathReset(p);
	Gfx_PathMoveTo(p, 100.0f, 50.0f);
	Gfx_PathLineTo(p, 200.0f, 50.0f);
	Gfx_PathLineTo(p, 200.0f, 150.0f);
	Gfx_PathLineTo(p, 100.0f, 150.0f);
	Gfx_PathClose(p);
	Gfx_FillPath(g, p, 0xFFE8F0FE);
	Gfx_StrokePath(g, p, solid, 2.0f, NULL);

	Gfx_PathReset(p);
	Gfx_PathMoveTo(p, 0.0f, 0.0f);
	Gfx_PathCubicTo(p, 0.0f, 100.0f, 100.0f, 100.0f, 100.0f, 0.0f);
	Gfx_FillPathBrush(g, p, grad);
	Gfx_StrokePath(g, p, solid, 2.0f, &dash);

	Gfx_PushClip(g, clipRc);
	Gfx_PushScaleTranslate(g, 2.0f, 300.0f, 200.0f);
	Gfx_PushRotate(g, 25.0f, 25.0f, 15.0f);
	Gfx_FillPathBrush(g, p, grad);
	Gfx_PopTransform(g);
	Gfx_PopTransform(g);
	Gfx_PopClip(g);
	CHECK(Gfx_End(g) == 1);

	/* 再次绘制: 几何/样式/渐变应命中缓存 (不崩溃即可) */
	Gfx_Begin(g);
	Gfx_Clear(g, 0xFFFFFFFF);
	Gfx_FillPath(g, p, 0xFF303030);
	CHECK(Gfx_End(g) == 1);

done:
	if (solid) Gfx_BrushDestroy(g, solid);
	if (grad) Gfx_BrushDestroy(g, grad);
	if (p) Gfx_PathDestroy(g, p);
	if (g) Gfx_Shutdown(g);
	if (hwnd) DestroyWindow(hwnd);

	sprintf(line, "gfx_path_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
