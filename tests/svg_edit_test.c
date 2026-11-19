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
#include "ui/ui_input.h"
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

static UiRect g_vp;

static void frame_(UiSvgEdit *ed)
{
	Gfx_Begin(g_gfx);
	Gfx_Clear(g_gfx, 0xFFFFFFFF);
	Ui_BeginFrame(&g_ctx);
	Ui_SvgEdit_Frame(ed, &g_ctx, g_vp);
	Ui_EndFrame(&g_ctx);
	Gfx_End(g_gfx);
}

static void press_at(UiSvgEdit *ed, float x, float y)
{
	Ui_OnMouseDown(&g_ctx, x, y);
	frame_(ed);
}

static void release_at(UiSvgEdit *ed, float x, float y)
{
	Ui_OnMouseUp(&g_ctx, x, y);
	frame_(ed);
}

static void test_gradient_selection_reset(void)
{
	UiRect vp = g_vp = Ui_Rect(0.0f, 0.0f, 800.0f, 600.0f);
	SvgDoc *doc = SvgDoc_Create(400.0f, 300.0f);
	UiSvgEdit *ed;
	UiId id = Ui_Id("svg.grad.sel", 0, 0);
	SvgElement *r, *el;
	SvgGradStop gs[2];
	float tx, ty;
	int ridx, eidx;
	if (!doc) return;
	r = SvgDoc_Add(doc, SVG_EL_RECT);
	r->p.rect.x = 40.0f; r->p.rect.y = 40.0f;
	r->p.rect.w = 100.0f; r->p.rect.h = 80.0f;
	gs[0].offset = 0.0f; gs[0].color = 0xFF2D7FF9u;
	gs[1].offset = 1.0f; gs[1].color = 0xFFFFFFFFu;
	r->style.fill.kind = SVG_PAINT_RADIAL;
	r->style.fill.cx = 0.5f; r->style.fill.cy = 0.5f; r->style.fill.r = 0.5f;
	Svg_StyleSetFillStops(&r->style.fill, gs, 2);
	el = SvgDoc_Add(doc, SVG_EL_ELLIPSE);
	el->p.ellipse.cx = 320.0f; el->p.ellipse.cy = 220.0f;
	el->p.ellipse.rx = 50.0f; el->p.ellipse.ry = 40.0f;
	ridx = 0; eidx = 1;
	ed = Ui_SvgEdit_Create(doc, id);
	CHECK(ed != NULL);
	if (!ed) { SvgDoc_Destroy(doc); return; }
	Ui_SvgEdit_ZoomReset(ed, vp);
	frame_(ed);
	/* view_reset: world→screen = world + tx/ty */
	tx = vp.x + (vp.w - doc->width) * 0.5f;
	ty = vp.y + (vp.h - doc->height) * 0.5f;

	press_at(ed, tx + 90.0f, ty + 80.0f);
	release_at(ed, tx + 90.0f, ty + 80.0f);
	CHECK(Ui_SvgEdit_SelIndex(ed, 0) == ridx);

	/* 拖径向手柄: 中心 (90,80) → 边缘 0.707r 方向 */
	press_at(ed, tx + 90.0f + 25.0f, ty + 80.0f + 20.0f);
	frame_(ed);
	Ui_OnMouseMove(&g_ctx, tx + 105.0f, ty + 95.0f);
	frame_(ed);
	release_at(ed, tx + 105.0f, ty + 95.0f);
	CHECK(Ui_SvgEdit_SelIndex(ed, 0) == ridx);

	/* 拖完后应能点选圆 (渐变手柄不得抢走元素外的点选) */
	press_at(ed, tx + 320.0f, ty + 220.0f);
	release_at(ed, tx + 320.0f, ty + 220.0f);
	CHECK(Ui_SvgEdit_SelIndex(ed, 0) == eidx);

	/* 再点回矩形 (渐变填充) 也应可选 */
	press_at(ed, tx + 90.0f, ty + 80.0f);
	release_at(ed, tx + 90.0f, ty + 80.0f);
	CHECK(Ui_SvgEdit_SelIndex(ed, 0) == ridx);

	Ui_SvgEdit_Destroy(ed);
	SvgDoc_Destroy(doc);
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

	/* 表单字段容量: 编辑器检查器用到 14 个输入框, 不能被静默截断 (文本页崩溃根因) */
	{
		UiInput form;
		int i, added = 0;
		UiInput_Init(&form, &g_ctx, NULL);
		for (i = 0; i < 14; i++) {
			if (UiInput_Add(&form, Ui_Id("t.field", i, 0), 64, 0,
				NULL, L"") >= 0)
				added++;
		}
		CHECK(added == 14);
		CHECK(UiInput_Edit(&form, 13) != NULL);
		CHECK(UiInput_Edit(&form, 14) == NULL);
		UiInput_Shutdown(&form);
	}

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

	test_gradient_selection_reset();

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
