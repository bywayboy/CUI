/*
 * svg_edit_test.c - SVG 编辑器 文本转路径 集成断言 (隐藏窗口 + Gfx/DWrite)
 *
 * 覆盖: 文本测量 -> DWrite 字形轮廓 -> 复合路径 (多子路径 closed) -> 导出,
 *       以及转换后一帧绘制不崩溃.
 * 构建: bin\cui_svg_edit_test.exe; 退出码 0 = 全通过.
 */
#include <windows.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_svg_edit.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);

static int g_pass, g_fail;
static UiTheme g_theme;
static UiContext g_ctx;
static Gfx *g_gfx;

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

static int count_(const char *s, const char *sub)
{
	int n = 0, i;
	for (; *s; s++) {
		for (i = 0; sub[i] && s[i] == sub[i]; i++) {}
		if (!sub[i]) n++;
	}
	return n;
}

void __stdcall startup(void)
{
	WNDCLASSEXW wc;
	HWND hwnd = NULL;
	SvgDoc *doc = NULL;
	UiSvgEdit *ed = NULL;
	char line[160];

	SetEnvironmentVariableA("SCV2_ULW", "1");
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"cui_svg_edit_test";
	if (!RegisterClassExW(&wc)) goto done;
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_svg_edit_test", L"", WS_POPUP, 0, 0, 800, 600,
		NULL, NULL, wc.hInstance, NULL);
	if (!hwnd) goto done;
	if (Gfx_Init(&g_gfx, hwnd) != 1 || !g_gfx) goto done;
	if (Gfx_Resize(g_gfx, 800, 600, 1.0f) != 1) goto done;
	UiTheme_Init(&g_theme, g_gfx);
	Ui_ContextInit(&g_ctx, hwnd, g_gfx, &g_theme);
	Ui_SetViewport(&g_ctx, 800.0f, 600.0f, 1.0f);

	doc = SvgDoc_Create(400, 300);
	CHECK(doc != NULL);
	if (!doc) goto done;
	{
		SvgElement *e = SvgDoc_Add(doc, SVG_EL_TEXT);
		CHECK(e != NULL);
		if (!e) goto done;
		e->p.text.x = 20.0f;
		e->p.text.y = 20.0f;
		e->fontSize = 64.0f;
		e->fontWeight = 400;
		e->style.fill.kind = SVG_PAINT_SOLID;
		e->style.fill.color = 0xFF202020u;
		e->style.setMask |= SVG_STYLE_FILL;
		SvgDoc_SetText(doc, 0, "O");
	}
	ed = Ui_SvgEdit_Create(doc, Ui_Id("svg.test", 0, 0));
	CHECK(ed != NULL);
	if (!ed) goto done;

	Gfx_Begin(g_gfx);
	Gfx_Clear(g_gfx, 0xFFFFFFFF);
	Ui_BeginFrame(&g_ctx);
	Ui_SvgEdit_Frame(ed, &g_ctx, Ui_Rect(0.0f, 0.0f, 800.0f, 600.0f));
	Ui_SvgEdit_SelectIndex(ed, 0);

	CHECK(Ui_SvgEdit_TextTarget(ed) == 0);
	CHECK(Ui_SvgEdit_TextToPath(ed, &g_ctx) == 1);
	CHECK(doc->els[0].type == SVG_EL_PATH);
	CHECK(doc->els[0].compound == 1);
	CHECK(doc->els[0].path.closed == 1);
	CHECK(doc->els[0].path.count > 8);
	{
		int i, sub = 0;
		for (i = 0; i < doc->els[0].path.count; i++)
			if (doc->els[0].path.nodes[i].subStart) sub++;
		CHECK(sub >= 1);
	}
	CHECK(Ui_SvgEdit_TextTarget(ed) == -1);
	{
		SvgPoly poly;
		Svg_PolyInit(&poly);
		CHECK(Svg_ElementPoly(&doc->els[0], 0.5f, &poly) == 1);
		CHECK(poly.contourCount >= 2);
		Svg_PolyFree(&poly);
	}
	{
		char *svg = Svg_Export(doc);
		CHECK(svg != NULL);
		if (svg) {
			CHECK(count_(svg, "M ") >= 2);
			CHECK(count_(svg, " Z") >= 2);
			Svg_FreeString(svg);
		}
	}
	/* 对齐 / 微移 / 剪贴板 / 径向渐变 (M11-M12) */
	{
		SvgElement *r1 = SvgDoc_Add(doc, SVG_EL_RECT);
		SvgElement *r2 = SvgDoc_Add(doc, SVG_EL_RECT);
		int i, minx, allmin = 1;
		GfxRect b0;
		int count0;
		if (r1) { r1->p.rect.x = 40.0f; r1->p.rect.y = 20.0f;
			r1->p.rect.w = 60.0f; r1->p.rect.h = 30.0f; }
		if (r2) { r2->p.rect.x = 160.0f; r2->p.rect.y = 90.0f;
			r2->p.rect.w = 40.0f; r2->p.rect.h = 20.0f; }
		Ui_SvgEdit_SelectAll(ed);
		CHECK(Ui_SvgEdit_Align(ed, &g_ctx, 0) == 1);
		minx = 1000000;
		for (i = 0; i < doc->count; i++) {
			GfxRect b = SvgDoc_ElementBounds(doc, i);
			if (b.x < minx) minx = (int)(b.x + 0.5f);
		}
		for (i = 0; i < doc->count; i++) {
			GfxRect b = SvgDoc_ElementBounds(doc, i);
			if ((int)(b.x + 0.5f) != minx) allmin = 0;
		}
		CHECK(allmin);
		b0 = SvgDoc_ElementBounds(doc, 0);
		CHECK(Ui_SvgEdit_Nudge(ed, &g_ctx, 5.0f, 3.0f) == 1);
		{
			GfxRect b1 = SvgDoc_ElementBounds(doc, 0);
			CHECK(NEAR(b1.x, b0.x + 5.0f, 0.01f));
			CHECK(NEAR(b1.y, b0.y + 3.0f, 0.01f));
		}
		CHECK(Ui_SvgEdit_CopySel(ed) == 1);
		count0 = doc->count;
		CHECK(Ui_SvgEdit_Paste(ed, &g_ctx) == 1);
		CHECK(doc->count > count0);
		CHECK(Ui_SvgEdit_SelCount(ed) >= 1);
	}
	{
		int ridx = doc->count - 2;
		SvgStyle st;
		SvgGradStop gs[2];
		char *svg;
		Ui_SvgEdit_SelectIndex(ed, ridx);
		Ui_SvgEdit_GetStyle(ed, &st);
		st.fill.kind = SVG_PAINT_RADIAL;
		st.fill.cx = 0.4f; st.fill.cy = 0.4f; st.fill.r = 0.5f;
		gs[0].offset = 0.0f; gs[0].color = 0xFF2D7FF9u;
		gs[1].offset = 1.0f; gs[1].color = 0xFFFFFFFFu;
		Svg_StyleSetFillStops(&st.fill, gs, 2);
		Ui_SvgEdit_SetStyle(ed, &g_ctx, &st);
		Svg_StyleFree(&st);
		svg = Svg_Export(doc);
		CHECK(svg != NULL);
		if (svg) {
			CHECK(count_(svg, "<radialGradient") >= 1);
			Svg_FreeString(svg);
		}
	}

	/* 转换后绘制 (含绘制孔洞的 winding 填充) 不崩溃 */
	Ui_SvgEdit_Frame(ed, &g_ctx, Ui_Rect(0.0f, 0.0f, 800.0f, 600.0f));
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g_gfx) == 1);

	CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);

done:
	if (ed) Ui_SvgEdit_Destroy(ed);
	if (doc) SvgDoc_Destroy(doc);
	if (g_gfx) {
		UiTheme_Shutdown(&g_theme, g_gfx);
		Gfx_Shutdown(g_gfx);
	}
	if (hwnd) DestroyWindow(hwnd);
	CoUninitialize();
	sprintf(line, "svg_edit_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
