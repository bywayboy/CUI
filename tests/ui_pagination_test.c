/*
 * ui_pagination_test.c - UiPagination headless 断言
 *
 * 覆盖: 页数/偏移、页码窗口算法(贴左/中/贴右/全显/钳制)、
 * 三种宽度退化(数字/紧凑/极简/不绘制)、点击翻页、当前页不改值、
 * pageSize 保持首条位置、total 收缩钳制、键盘、单页隐藏、200% DPI。
 * 构建: bin\cui_pagination_test.exe; 退出码 0=全通过。
 */
#include <windows.h>
#include <stdarg.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_pagination.h"

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

/* ── 帧/输入 ───────────────────────────────────────────────────────── */

static void frame(UiPagination *p, UiRect rc, const UiPaginationStyle *st)
{
	Gfx_Begin(g_gfx);
	Ui_BeginFrame(&g_ctx);
	UiPagination_Draw(p, &g_ctx, rc, st);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g_gfx) == 1);
}

static void click_at(float x, float y)
{
	Ui_OnMouseMove(&g_ctx, x, y);
	Ui_OnMouseDown(&g_ctx, x, y);
	Ui_OnMouseUp(&g_ctx, x, y);
}

/* 点击后先画一帧处理事件, 再画一帧按新状态刷新命中表 (与真实事件循环一致). */
static void click_pag(UiPagination *p, UiRect rc,
	const UiPaginationStyle *st, float x, float y)
{
	click_at(x, y);
	frame(p, rc, st);
	frame(p, rc, st);
}

static float cell_w(int n)
{
	char buf[24];
	float tw;
	wsprintfA(buf, "%d", n);
	tw = Gfx_TextWidth(g_gfx, g_theme.fBody, buf) + 10.0f;
	return tw > 30.0f ? tw : 30.0f;
}

static float text_w(const char *s)
{
	return Gfx_TextWidth(g_gfx, g_theme.fBody, s);
}

/* 组件内部 id 派生规则 (用于校验收到的命中). */
static UiId pag_id_(const char *scope, UiId base)
{
	return Ui_Id(scope, (int)(uint32_t)(base >> 32), (int)(uint32_t)base);
}

/* ── 页数/偏移 ──────────────────────────────────────────────────────── */

static void test_count_offset(void)
{
	UiPagination p;
	UiId id = Ui_Id("pag.count", 0, 0);

	UiPagination_Init(&p, id, 0, 50);
	CHECK(UiPagination_PageCount(&p) == 0);
	CHECK(UiPagination_Offset(&p) == 0);

	UiPagination_Init(&p, id, 200, 50);
	CHECK(UiPagination_PageCount(&p) == 4);
	UiPagination_Init(&p, id, 201, 50);
	CHECK(UiPagination_PageCount(&p) == 5);
	UiPagination_Init(&p, id, 7, 20);
	CHECK(UiPagination_PageCount(&p) == 1);
	UiPagination_Init(&p, id, 1000, 33);
	CHECK(UiPagination_PageCount(&p) == 31);
	UiPagination_Init(&p, id, 100, 0);   /* 非法 pageSize -> 1 */
	CHECK(UiPagination_PageCount(&p) == 100);
	CHECK(p.pageSize == 1);

	UiPagination_Init(&p, id, 1000, 50);
	UiPagination_SetPage(&p, &g_ctx, 7);
	CHECK(UiPagination_Offset(&p) == 350);
}

/* ── 页码窗口算法 ──────────────────────────────────────────────────── */

static void test_slots(void)
{
	int out[16], n, i;

	n = UiPagination_Slots(0, 3, 9, out);
	CHECK(n == 3);
	CHECK(out[0] == 0 && out[1] == 1 && out[2] == 2);

	n = UiPagination_Slots(0, 7, 9, out);
	CHECK(n == 7);

	n = UiPagination_Slots(0, 20, 7, out);
	CHECK(n == 7);
	CHECK(out[0] == 0 && out[1] == 1 && out[2] == 2 && out[3] == 3 &&
		out[4] == 4 && out[5] == -1 && out[6] == 19);

	n = UiPagination_Slots(4, 20, 7, out);
	CHECK(n == 7);
	CHECK(out[5] == -1 && out[6] == 19);

	n = UiPagination_Slots(5, 20, 7, out);
	CHECK(n == 7);
	CHECK(out[0] == 0 && out[1] == -1);
	CHECK(out[2] == 4 && out[3] == 5 && out[4] == 6);
	CHECK(out[5] == -1 && out[6] == 19);

	n = UiPagination_Slots(14, 20, 7, out);
	CHECK(n == 7);
	CHECK(out[2] == 13 && out[3] == 14 && out[4] == 15);

	n = UiPagination_Slots(15, 20, 7, out);
	CHECK(n == 7);
	CHECK(out[0] == 0 && out[1] == -1 && out[2] == 15 && out[6] == 19);

	n = UiPagination_Slots(10, 20, 5, out);
	CHECK(n == 5);
	CHECK(out[0] == 0 && out[1] == -1 && out[2] == 10 &&
		out[3] == -1 && out[4] == 19);

	n = UiPagination_Slots(10, 20, 9, out);
	CHECK(n == 9);
	CHECK(out[0] == 0 && out[1] == -1);
	CHECK(out[2] == 8 && out[6] == 12);
	CHECK(out[7] == -1 && out[8] == 19);

	/* maxSlots 钳制到 [5,9] */
	n = UiPagination_Slots(0, 20, 2, out);
	CHECK(n == 5);
	CHECK(out[0] == 0 && out[4] == 19);
	n = UiPagination_Slots(0, 20, 20, out);
	CHECK(n == 9);

	/* 每页都必须包含当前页、首尾可达、升序、槽位数固定 */
	for (i = 0; i < 20; i++) {
		int j, prev = -1, seen = 0;
		n = UiPagination_Slots(i, 20, 7, out);
		CHECK(n == 7);
		CHECK(out[0] == 0 && out[n - 1] == 19);
		for (j = 0; j < n; j++) {
			if (out[j] < 0) continue;
			if (out[j] == i) seen = 1;
			CHECK(out[j] > prev || prev < 0);
			prev = out[j];
		}
		CHECK(seen == 1);
	}
}

/* ── 设置器 ────────────────────────────────────────────────────────── */

static void test_setters(void)
{
	UiPagination p;
	UiId id = Ui_Id("pag.set", 0, 0);

	UiPagination_Init(&p, id, 200, 20);   /* 10 页 */
	CHECK(UiPagination_TakeChanged(&p) == 0);
	UiPagination_SetPage(&p, &g_ctx, 5);
	CHECK(p.page == 5);
	CHECK(UiPagination_TakeChanged(&p) == 1);
	CHECK(UiPagination_TakeChanged(&p) == 0);

	/* 6 页首条 = 100, 50/页 -> 第 2 页 (100..149) */
	UiPagination_SetPageSize(&p, &g_ctx, 50);
	CHECK(p.page == 2);
	CHECK(UiPagination_PageCount(&p) == 4);
	CHECK(UiPagination_Offset(&p) == 100);
	CHECK(UiPagination_TakeChanged(&p) == 1);

	/* 30/页: 原首条 100 -> 第 3 页 (90..119)? 页 = 100/30 = 3 */
	UiPagination_SetPageSize(&p, &g_ctx, 30);
	CHECK(p.page == 3);
	CHECK(UiPagination_Offset(&p) <= 100);
	CHECK(UiPagination_Offset(&p) + p.pageSize > 100);
	CHECK(UiPagination_TakeChanged(&p) == 1);

	/* total 收缩: 页钳制并上报 */
	UiPagination_SetTotal(&p, &g_ctx, 35);   /* 2 页 */
	CHECK(UiPagination_PageCount(&p) == 2);
	CHECK(p.page == 1);
	CHECK(UiPagination_TakeChanged(&p) == 1);
	CHECK(UiPagination_Offset(&p) == 30);

	UiPagination_SetTotal(&p, &g_ctx, 0);
	CHECK(p.page == 0);
	CHECK(UiPagination_TakeChanged(&p) == 1);

	UiPagination_SetTotal(&p, &g_ctx, 100);  /* 4 页 */
	CHECK(UiPagination_PageCount(&p) == 4);
	UiPagination_SetPage(&p, &g_ctx, -3);
	CHECK(p.page == 0);
	UiPagination_SetPage(&p, &g_ctx, 99);
	CHECK(p.page == 3);
	CHECK(UiPagination_TakeChanged(&p) == 1);
	UiPagination_TakeChanged(&p);

	/* 同值 set 不动 changed */
	UiPagination_SetPage(&p, &g_ctx, 3);
	CHECK(UiPagination_TakeChanged(&p) == 0);
	UiPagination_SetPageSize(&p, &g_ctx, 0);  /* 钳到 1, 首条 90 */
	CHECK(p.pageSize == 1);
	CHECK(UiPagination_PageCount(&p) == 100);
	CHECK(p.page == 90);
	CHECK(UiPagination_TakeChanged(&p) == 1);
}

/* ── 数字模式点击 ──────────────────────────────────────────────────── */

static void test_numeric_click(void)
{
	UiPagination p;
	UiPaginationStyle st;
	UiId id = Ui_Id("pag.num", 0, 0);
	UiRect rc = { 20, 20, 800, 36 };
	float cw, x, y = 38.0f;

	ZeroMemory(&st, sizeof(st));
	UiPagination_Init(&p, id, 1000, 50);   /* 20 页 */
	UiPagination_SetPage(&p, &g_ctx, 9);
	UiPagination_TakeChanged(&p);
	cw = cell_w(20);

	frame(&p, rc, &st);
	/* 贴中窗口 {0,-1,7,8,9,10,11,-1,19}: next 前有 8 个整格 + 2 个省略号 */
	x = rc.x + 8.0f * (cw + 2.0f) + 2.0f * (16.0f + 2.0f) + cw * 0.5f;
	CHECK(Ui_HitTest(&g_ctx, x, y) == pag_id_("ui.pag.next", id));
	click_pag(&p, rc, &st, x, y);
	CHECK(p.page == 10);
	CHECK(UiPagination_TakeChanged(&p) == 1);

	/* prev */
	click_pag(&p, rc, &st, rc.x + cw * 0.5f, y);
	CHECK(p.page == 9);
	CHECK(UiPagination_TakeChanged(&p) == 1);

	/* 点当前页 (slot 4): prev/page0/省略号/page7/page8 之后 */
	click_pag(&p, rc, &st,
		rc.x + 4.0f * (cw + 2.0f) + (16.0f + 2.0f) + cw * 0.5f, y);
	CHECK(p.page == 9);
	CHECK(UiPagination_TakeChanged(&p) == 0);
	CHECK(g_ctx.navId == p.id);

	/* 首/尾按钮 (firstLast): 先重画注册新布局再点 « */
	st.firstLast = 1;
	frame(&p, rc, &st);
	click_pag(&p, rc, &st, rc.x + cw * 0.5f, y);
	CHECK(p.page == 0);
	CHECK(UiPagination_TakeChanged(&p) == 1);
}

/* ── 宽度退化 ──────────────────────────────────────────────────────── */

static void test_compact_and_minimal(void)
{
	UiPagination p;
	UiPaginationStyle st;
	UiId id = Ui_Id("pag.modes", 0, 0);
	UiRect rc = { 20, 20, 140, 36 };
	float cw, tw, compW, minW, x, y = 38.0f;

	ZeroMemory(&st, sizeof(st));
	UiPagination_Init(&p, id, 1000, 50);   /* 20 页 */
	UiPagination_SetPage(&p, &g_ctx, 9);
	UiPagination_TakeChanged(&p);

	cw = cell_w(20);
	tw = text_w("10 / 20");
	compW = 2.0f * cw + 4.0f + tw + 12.0f;
	minW = 2.0f * cw + 2.0f;
	CHECK(compW <= 140.0f);
	CHECK(minW <= compW);

	frame(&p, rc, &st);
	CHECK(UiPagination_Measure(&p, &g_ctx, &st, 1) <= 140.0f);
	CHECK(UiPagination_Measure(&p, &g_ctx, &st, 0) > 140.0f);

	x = rc.x + cw + 2.0f + (tw + 12.0f) + 2.0f + cw * 0.5f;
	click_pag(&p, rc, &st, x, y);
	CHECK(p.page == 10);
	CHECK(UiPagination_TakeChanged(&p) == 1);

	/* 极简: 只有 ‹ › */
	rc.w = minW + 2.0f;
	frame(&p, rc, &st);
	x = rc.x + cw + 2.0f + cw * 0.5f;
	click_pag(&p, rc, &st, x, y);
	CHECK(p.page == 11);
	CHECK(UiPagination_TakeChanged(&p) == 1);

	/* 再窄就整体不绘制, 也不注册命中 */
	rc.w = minW - 10.0f;
	{
		UiPagination q;
		int r;
		UiPagination_Init(&q, id, 1000, 50);
		UiPagination_SetPage(&q, &g_ctx, 5);
		UiPagination_TakeChanged(&q);
		Gfx_Begin(g_gfx);
		Ui_BeginFrame(&g_ctx);
		r = UiPagination_Draw(&q, &g_ctx, rc, &st);
		Ui_EndFrame(&g_ctx);
		CHECK(Gfx_End(g_gfx) == 1);
		CHECK(r == -1);
		CHECK(q.page == 5);
		click_pag(&q, rc, &st, rc.x + 10.0f, y);
		CHECK(q.page == 5);
		CHECK(UiPagination_TakeChanged(&q) == 0);
	}
}

/* ── 单页 ──────────────────────────────────────────────────────────── */

static void test_single_page(void)
{
	UiPagination p;
	UiPaginationStyle st;
	UiId id = Ui_Id("pag.single", 0, 0);
	UiRect rc = { 20, 20, 400, 36 };
	int r;

	ZeroMemory(&st, sizeof(st));
	UiPagination_Init(&p, id, 30, 50);   /* 1 页 */
	CHECK(UiPagination_PageCount(&p) == 1);

	Gfx_Begin(g_gfx);
	Ui_BeginFrame(&g_ctx);
	r = UiPagination_Draw(&p, &g_ctx, rc, &st);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g_gfx) == 1);
	CHECK(r == -1);

	st.showSingle = 1;
	frame(&p, rc, &st);
	CHECK(UiPagination_Measure(&p, &g_ctx, &st, 0) > 0.0f);
	click_pag(&p, rc, &st, rc.x + 16.0f, 38.0f);
	CHECK(p.page == 0);
	CHECK(UiPagination_TakeChanged(&p) == 0);

	st.showSingle = 0;
	CHECK(UiPagination_Measure(&p, &g_ctx, &st, 0) == 0.0f);
}

/* ── 键盘 ──────────────────────────────────────────────────────────── */

static void test_keyboard(void)
{
	UiPagination p;
	UiId id = Ui_Id("pag.keys", 0, 0);

	UiPagination_Init(&p, id, 1000, 50);   /* 20 页 */
	g_ctx.navId = UI_ID_NONE;
	CHECK(UiPagination_Key(&p, &g_ctx, VK_RIGHT) == 0);
	CHECK(p.page == 0);

	g_ctx.navId = id;
	CHECK(UiPagination_Key(&p, &g_ctx, VK_RIGHT) == 1);
	CHECK(p.page == 1);
	CHECK(p.focusVisible == 1);
	CHECK(UiPagination_TakeChanged(&p) == 1);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_LEFT) == 1);
	CHECK(p.page == 0);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_LEFT) == 1);
	CHECK(p.page == 0);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_END) == 1);
	CHECK(p.page == 19);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_RIGHT) == 1);
	CHECK(p.page == 19);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_PRIOR) == 1);
	CHECK(p.page == 14);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_NEXT) == 1);
	CHECK(p.page == 19);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_HOME) == 1);
	CHECK(p.page == 0);
	CHECK(UiPagination_Key(&p, &g_ctx, VK_TAB) == 0);
	CHECK(UiPagination_TakeChanged(&p) == 1);
	CHECK(UiPagination_TakeChanged(&p) == 0);
	g_ctx.navId = UI_ID_NONE;
}

/* ── 200% DPI ──────────────────────────────────────────────────────── */

static void test_dpi200(void)
{
	UiPagination p;
	UiPaginationStyle st;
	UiId id = Ui_Id("pag.dpi", 0, 0);
	UiRect rc = { 20, 20, 140, 36 };
	float m100, cw, tw, x;

	ZeroMemory(&st, sizeof(st));
	UiPagination_Init(&p, id, 1000, 50);
	UiPagination_SetPage(&p, &g_ctx, 9);
	UiPagination_TakeChanged(&p);
	m100 = UiPagination_Measure(&p, &g_ctx, &st, 1);

	CHECK(Gfx_Resize(g_gfx, 760, 720, 2.0f) == 1);
	Ui_SetViewport(&g_ctx, 760.0f, 720.0f, 2.0f);
	CHECK(UiPagination_Measure(&p, &g_ctx, &st, 1) == m100);

	cw = cell_w(20);
	tw = text_w("10 / 20");
	frame(&p, rc, &st);
	x = rc.x + cw + 2.0f + (tw + 12.0f) + 2.0f + cw * 0.5f;
	click_pag(&p, rc, &st, x, 38.0f);
	CHECK(p.page == 10);
	CHECK(UiPagination_TakeChanged(&p) == 1);
	CHECK(UiPagination_Measure(&p, &g_ctx, &st, 1) == m100);

	UiTheme_SetTheme(&g_theme, UI_THEME_DARK);
	frame(&p, rc, &st);
	UiTheme_SetTheme(&g_theme, UI_THEME_LIGHT);
	CHECK(Gfx_Resize(g_gfx, 760, 720, 1.0f) == 1);
	Ui_SetViewport(&g_ctx, 760.0f, 720.0f, 1.0f);
}

/* ── 入口 ──────────────────────────────────────────────────────────── */

void __stdcall startup(void)
{
	WNDCLASSEXW wc;
	HWND hwnd = NULL;
	char line[160];

	SetEnvironmentVariableA("SCV2_ULW", "1");
	CHECK(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED) >= 0);
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"cui_pagination_test";
	CHECK(RegisterClassExW(&wc) != 0);
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_pagination_test", L"", WS_POPUP, 0, 0, 760, 720,
		NULL, NULL, wc.hInstance, NULL);
	CHECK(hwnd != NULL);
	if (hwnd) {
		CHECK(Gfx_Init(&g_gfx, hwnd) == 1);
		if (g_gfx) {
			CHECK(Gfx_Resize(g_gfx, 760, 720, 1.0f) == 1);
			UiTheme_Init(&g_theme, g_gfx);
			Ui_ContextInit(&g_ctx, hwnd, g_gfx, &g_theme);
			Ui_SetViewport(&g_ctx, 760.0f, 720.0f, 1.0f);
			Ui_BeginFrame(&g_ctx);
			Ui_EndFrame(&g_ctx);

			test_count_offset();
			test_slots();
			test_setters();
			test_numeric_click();
			test_compact_and_minimal();
			test_single_page();
			test_keyboard();
			test_dpi200();

			CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
			UiTheme_Shutdown(&g_theme, g_gfx);
			Gfx_Shutdown(g_gfx);
		}
		DestroyWindow(hwnd);
	}
	CoUninitialize();
	sprintf(line, "ui_pagination_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
