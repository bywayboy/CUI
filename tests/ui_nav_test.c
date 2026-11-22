/*
 * ui_nav_test.c - UiNavBar / 动态存储 / 弹层作用域 headless 断言
 *
 * 覆盖: 选中路径持久化、未解析后缀保留、水平溢出 >>、多级面板视口内且不
 * 互相压盖、垂直 rail 底部 >>、长面板 "更多" 续页、键盘逐级 Esc、
 * 动态控件/动画池扩容。
 * 构建: bin\cui_nav_test.exe; 退出码 0=全通过。
 */
#include <windows.h>
#include <stdarg.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_popup.h"
#include "ui/ui_nav.h"

int sprintf(char *buf, const char *fmt, ...);
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
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

/* ── 测试数据 ───────────────────────────────────────────────────────── */

#define NI(id_, label_, kids_, n_, fl_) \
	{ (UiId)(id_), label_, NULL, { 0, 0, 0, 0, 0.0f, 0.0f }, kids_, n_, fl_ }

static const UiNavItem kLeaves3[] = {
	NI(11, "键盘", NULL, 0, 0),
	NI(12, "显示器", NULL, 0, 0),
	NI(13, "音频", NULL, 0, 0)
};
static const UiNavItem kSoft[] = {
	NI(21, "设计工具", kLeaves3, 3, 0),
	NI(22, "开发工具", NULL, 0, 0),
	NI(23, "云服务", NULL, 0, 0)
};
static const UiNavItem kHardware[] = {
	NI(31, "外设", kLeaves3, 3, 0),
	NI(32, "显示器", NULL, 0, 0),
	NI(33, "音频设备", NULL, 0, 0)
};
static const UiNavItem kRootFull[] = {
	NI(10, "产品", kHardware, 3, 0),
	NI(20, "软件", kSoft, 3, 0),
	NI(40, "文档", NULL, 0, 0),
	NI(50, "关于", NULL, 0, 0)
};
static const UiNavItem kShallow[] = {
	NI(10, "产品", NULL, 0, 0),
	NI(20, "软件", NULL, 0, 0),
	NI(40, "文档", NULL, 0, 0),
	NI(50, "关于", NULL, 0, 0)
};

static char g_manyLabels[24][24];
static UiNavItem g_many[24];
static char g_wideLabels[30][24];
static UiNavItem g_wide[30];
static UiNavItem g_longRoot[1];

static void build_many(void)
{
	int i;
	for (i = 0; i < 24; i++) {
		wsprintfA(g_manyLabels[i], "项目 %d", i + 1);
		g_many[i].id = (UiId)(1000 + i);
		g_many[i].label = g_manyLabels[i];
		g_many[i].shortcut = NULL;
		ZeroMemory(&g_many[i].icon, sizeof(g_many[i].icon));
		g_many[i].children = NULL;
		g_many[i].childCount = 0;
		g_many[i].flags = 0;
	}
}

static void build_wide(void)
{
	int i;
	for (i = 0; i < 30; i++) {
		wsprintfA(g_wideLabels[i], "菜单条目 %d", i + 1);
		g_wide[i].id = (UiId)(2000 + i);
		g_wide[i].label = g_wideLabels[i];
		g_wide[i].shortcut = i == 3 ? "Ctrl+K" : NULL;
		ZeroMemory(&g_wide[i].icon, sizeof(g_wide[i].icon));
		g_wide[i].children = NULL;
		g_wide[i].childCount = 0;
		g_wide[i].flags = 0;
	}
	g_longRoot[0].id = 3000;
	g_longRoot[0].label = "长菜单";
	g_longRoot[0].shortcut = NULL;
	ZeroMemory(&g_longRoot[0].icon, sizeof(g_longRoot[0].icon));
	g_longRoot[0].children = g_wide;
	g_longRoot[0].childCount = 30;
	g_longRoot[0].flags = 0;
}

/* ── 帧/输入 ───────────────────────────────────────────────────────── */

static void frame_nav(UiNavBar *nav, UiPopupManager *pm, UiRect rc,
	int drawPopups)
{
	Gfx_Begin(g_gfx);
	Ui_BeginFrame(&g_ctx);
	UiPopupManager_BeginFrame(pm, &g_ctx);
	UiNavBar_Draw(nav, pm, &g_ctx, rc, 1);
	if (drawPopups) UiNavBar_DrawPopups(nav, pm, &g_ctx);
	Ui_EndFrame(&g_ctx);
	CHECK(Gfx_End(g_gfx) == 1);
}

static void click_at(float x, float y)
{
	Ui_OnMouseMove(&g_ctx, x, y);
	Ui_OnMouseDown(&g_ctx, x, y);
	Ui_OnMouseUp(&g_ctx, x, y);
}

static int inside_view(const UiRect *r, float vw, float vh)
{
	return r && r->w > 0.0f && r->h > 0.0f &&
		r->x >= 7.9f && r->y >= 7.9f &&
		r->x + r->w <= vw - 7.9f && r->y + r->h <= vh - 7.9f;
}

/* ── 测试 ───────────────────────────────────────────────────────────── */

static void test_selected_path(void)
{
	UiNavBar nav;
	UiId path[3] = { 10, 31, 11 };
	UiId got[8];
	int d;
	UiNavBar_Init(&nav, Ui_Id("nav.sel", 0, 0));
	UiNavBar_SetItems(&nav, kRootFull,
		(int)(sizeof(kRootFull) / sizeof(kRootFull[0])));
	UiNavBar_SetSelected(&nav, path, 3);
	ZeroMemory(got, sizeof(got));
	d = UiNavBar_GetSelected(&nav, got, 8);
	CHECK(d == 3);
	CHECK(got[0] == 10 && got[1] == 31 && got[2] == 11);
	CHECK(nav.selectedDepth == 3);

	/* 数据暂时缺失: requested 保留 3 段, 解析结果退到最长前缀 1 段 */
	UiNavBar_SetItems(&nav, kShallow,
		(int)(sizeof(kShallow) / sizeof(kShallow[0])));
	CHECK(UiNavBar_GetSelected(&nav, got, 8) == 1);
	CHECK(nav.requestedDepth == 3);
	/* 数据恢复后重新解析到叶子 */
	UiNavBar_SetItems(&nav, kRootFull,
		(int)(sizeof(kRootFull) / sizeof(kRootFull[0])));
	CHECK(UiNavBar_GetSelected(&nav, got, 8) == 3);
	UiNavBar_Shutdown(&nav);
}

static void test_keyboard_levels(void)
{
	UiNavBar nav;
	UiPopupManager pm;
	UiId got[8];
	int d;
	UiRect rc = { 20, 20, 460, 44 };
	UiPopupManager_Init(&pm);
	UiNavBar_Init(&nav, Ui_Id("nav.keys", 0, 0));
	UiNavBar_SetItems(&nav, kRootFull,
		(int)(sizeof(kRootFull) / sizeof(kRootFull[0])));
	UiNavBar_SetSelected(&nav, NULL, 0);
	frame_nav(&nav, &pm, rc, 0);
	CHECK(nav.overflow == 0);
	CHECK(nav.visibleCount == 4);

	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RIGHT) == 1);
	CHECK(nav.keyRoot == 10);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(UiPopupManager_IsOpen(&pm, nav.id) == 1);
	CHECK(nav.openDepth == 1);
	CHECK(nav.keyItem[0] == 31);

	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(nav.openDepth == 2);
	CHECK(nav.openPath[0] == 10 && nav.openPath[1] == 31);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(UiPopupManager_IsOpen(&pm, nav.id) == 1);
	CHECK(inside_view(&nav.panelRc[0], g_ctx.width, g_ctx.height));
	CHECK(inside_view(&nav.panelRc[1], g_ctx.width, g_ctx.height));
	CHECK(nav.panelRc[1].x >= nav.panelRc[0].x + nav.panelRc[0].w);

	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_ESCAPE) == 1);
	CHECK(nav.openDepth == 1);
	CHECK(nav.keyItem[0] == 31);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(nav.panelRc[0].w > 0.0f);
	CHECK(nav.panelRc[1].w == 0.0f);

	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(nav.openDepth == 2);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(UiPopupManager_AnyOpen(&pm) == 0);
	d = UiNavBar_GetSelected(&nav, got, 8);
	CHECK(d == 3);
	CHECK(got[0] == 10 && got[1] == 31 && got[2] == 11);
	got[3] = 0x5A5A5A5A;
	CHECK(UiNavBar_TakeChanged(&nav, got, 8) == 1);
	CHECK(got[0] == 10 && got[1] == 31 && got[2] == 11);
	/* 深度之后的槽位必须清零: 调用方靠 0 判路径末尾 */
	CHECK(got[3] == UI_ID_NONE && got[7] == UI_ID_NONE);
	CHECK(UiNavBar_TakeChanged(&nav, got, 8) == 0);
	UiNavBar_Shutdown(&nav);
}

static void test_panel_flip_no_overlap(void)
{
	UiNavBar nav;
	UiPopupManager pm;
	UiRect rc = { 520, 20, 220, 44 };
	UiPopupManager_Init(&pm);
	UiNavBar_Init(&nav, Ui_Id("nav.flip", 0, 0));
	UiNavBar_SetItems(&nav, kRootFull,
		(int)(sizeof(kRootFull) / sizeof(kRootFull[0])));
	UiNavBar_SetSelected(&nav, NULL, 0);
	frame_nav(&nav, &pm, rc, 0);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RIGHT) == 1);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(nav.openDepth == 2);
	CHECK(inside_view(&nav.panelRc[0], g_ctx.width, g_ctx.height));
	CHECK(inside_view(&nav.panelRc[1], g_ctx.width, g_ctx.height));
	/* 父面板贴右边缘时子面板翻到左侧, 但不允许压住父面板. */
	CHECK(nav.panelRc[1].x + nav.panelRc[1].w <= nav.panelRc[0].x + 0.1f);
	UiNavBar_Shutdown(&nav);
}

static void test_no_focus_enter(void)
{
	UiNavBar nav;
	UiPopupManager pm;
	UiRect rc = { 20, 20, 460, 44 };
	UiPopupManager_Init(&pm);
	UiNavBar_Init(&nav, Ui_Id("nav.enter", 0, 0));
	UiNavBar_SetItems(&nav, kRootFull,
		(int)(sizeof(kRootFull) / sizeof(kRootFull[0])));
	UiNavBar_SetSelected(&nav, NULL, 0);
	frame_nav(&nav, &pm, rc, 0);

	/* 没有键盘焦点: 回车/空格不能自动弹第一个菜单 */
	CHECK(nav.keyFocusVisible == 0);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 0);
	CHECK(UiPopupManager_AnyOpen(&pm) == 0);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_SPACE) == 0);
	CHECK(UiPopupManager_AnyOpen(&pm) == 0);

	/* 方向键取得焦点后, 回车仍可打开 */
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RIGHT) == 1);
	CHECK(nav.keyFocusVisible == 1);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(UiPopupManager_IsOpen(&pm, nav.id) == 1);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_ESCAPE) == 1);
	CHECK(nav.openDepth == 0);

	/* 鼠标在导航栏外按下会释放键盘焦点 */
	Ui_OnMouseMove(&g_ctx, 200, 400);
	Ui_OnMouseDown(&g_ctx, 200, 400);
	frame_nav(&nav, &pm, rc, 0);
	CHECK(nav.keyFocusVisible == 0);
	CHECK(nav.keyRoot == UI_ID_NONE);
	Ui_OnMouseUp(&g_ctx, 200, 400);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 0);
	CHECK(UiPopupManager_AnyOpen(&pm) == 0);
	UiNavBar_Shutdown(&nav);
}

static void test_overflow(void)
{
	UiNavBar nav;
	UiPopupManager pm;
	UiId got[4];
	UiRect rc = { 20, 20, 420, 44 };
	float mx, my;
	UiId firstHidden;
	UiPopupManager_Init(&pm);
	UiNavBar_Init(&nav, Ui_Id("nav.overflow", 0, 0));
	UiNavBar_SetItems(&nav, g_many, 24);
	UiNavBar_SetSelected(&nav, NULL, 0);
	frame_nav(&nav, &pm, rc, 0);
	CHECK(nav.overflow == 1);
	CHECK(nav.visibleCount > 0 && nav.visibleCount < nav.count);
	CHECK(nav.moreRc.w > 0.0f);
	CHECK(nav.moreRc.x >= rc.x && nav.moreRc.x + nav.moreRc.w <= rc.x + rc.w + 0.1f);
	firstHidden = g_many[nav.visibleStart + nav.visibleCount].id;

	mx = nav.moreRc.x + nav.moreRc.w * 0.5f;
	my = nav.moreRc.y + nav.moreRc.h * 0.5f;
	click_at(mx, my);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(UiPopupManager_IsOpen(&pm, nav.id) == 1);
	CHECK(nav.openOverflow == 1);
	CHECK(nav.openDepth == 1);
	CHECK(inside_view(&nav.panelRc[0], g_ctx.width, g_ctx.height));
	CHECK(nav.keyItem[0] == firstHidden);

	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(UiPopupManager_AnyOpen(&pm) == 0);
	CHECK(UiNavBar_GetSelected(&nav, got, 4) == 1);
	CHECK(got[0] == firstHidden);
	UiNavBar_Shutdown(&nav);
}

static void test_vertical_rail(void)
{
	UiNavBar nav;
	UiPopupManager pm;
	UiId path[1] = { 1023 };
	UiId got[4];
	UiRect rc = { 20, 20, 220, 240 };
	UiId firstHidden;
	UiPopupManager_Init(&pm);
	UiNavBar_Init(&nav, Ui_Id("nav.vert", 0, 0));
	UiNavBar_SetLayout(&nav, UI_NAV_VERTICAL);
	UiNavBar_SetItems(&nav, g_many, 24);
	UiNavBar_SetSelected(&nav, path, 1);
	frame_nav(&nav, &pm, rc, 0);
	CHECK(nav.overflow == 1);
	CHECK(nav.visibleCount > 0 && nav.visibleCount < nav.count);
	CHECK(nav.moreRc.w > 0.0f);
	CHECK(nav.moreRc.y + nav.moreRc.h <= rc.y + rc.h + 0.1f);
	CHECK(UiNavBar_GetSelected(&nav, got, 4) == 1 && got[0] == 1023);

	firstHidden = g_many[nav.visibleStart + nav.visibleCount].id;
	click_at(nav.moreRc.x + nav.moreRc.w * 0.5f,
		nav.moreRc.y + nav.moreRc.h * 0.5f);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(UiPopupManager_IsOpen(&pm, nav.id) == 1);
	CHECK(nav.openOverflow == 1);
	CHECK(nav.openDepth == 1);
	CHECK(nav.keyItem[0] == firstHidden);
	CHECK(inside_view(&nav.panelRc[0], g_ctx.width, g_ctx.height));

	UiNavBar_SetRail(&nav, 1);
	frame_nav(&nav, &pm, rc, 0);
	CHECK(nav.rail == 1);
	CHECK(nav.overflow == 1);
	CHECK(UiNavBar_GetSelected(&nav, got, 4) == 1 && got[0] == 1023);
	UiNavBar_Shutdown(&nav);
}

static void test_long_panel(void)
{
	UiNavBar nav;
	UiPopupManager pm;
	UiId got[4];
	UiId moreId;
	UiRect rc = { 20, 20, 500, 44 };
	UiPopupManager_Init(&pm);
	UiNavBar_Init(&nav, Ui_Id("nav.long", 0, 0));
	UiNavBar_SetItems(&nav, g_longRoot, 1);
	UiNavBar_SetSelected(&nav, NULL, 0);
	frame_nav(&nav, &pm, rc, 0);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RIGHT) == 1);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(nav.openDepth == 1);
	CHECK(inside_view(&nav.panelRc[0], g_ctx.width, g_ctx.height));
	CHECK(nav.panelMore[0] == 1);
	CHECK(nav.panelVisible[0] > 0 && nav.panelVisible[0] < 30);
	CHECK(nav.panelRc[0].h <= g_ctx.height - 16.0f + 0.1f);

	moreId = Ui_IdIndex("ui.nav.more.row",
		(int)(uint32_t)(nav.id >> 32), (int)(uint32_t)nav.id, 0);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_END) == 1);
	CHECK(nav.keyItem[0] == moreId);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(nav.openDepth == 2);
	CHECK(nav.openTail[1] == 1);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(nav.panelStart[1] == nav.panelStart[0] + nav.panelVisible[0]);
	CHECK(nav.panelRc[1].w > 0.0f);
	CHECK(inside_view(&nav.panelRc[1], g_ctx.width, g_ctx.height));
	CHECK(nav.panelRc[1].x >= nav.panelRc[0].x + nav.panelRc[0].w);

	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_ESCAPE) == 1);
	CHECK(nav.openDepth == 1);
	CHECK(nav.openTail[1] == 0);
	CHECK(nav.keyItem[0] == moreId);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(nav.panelRc[1].w == 0.0f);

	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	frame_nav(&nav, &pm, rc, 1);
	CHECK(UiNavBar_Key(&nav, &pm, &g_ctx, VK_RETURN) == 1);
	CHECK(UiPopupManager_AnyOpen(&pm) == 0);
	CHECK(UiNavBar_GetSelected(&nav, got, 4) == 2);
	CHECK(got[0] == 3000 && got[1] == g_wide[9].id);
	UiNavBar_Shutdown(&nav);
}

static void test_popup_scope(void)
{
	UiPopupManager pm;
	UiId dialogId = Ui_Id("test.dialog", 0, 0);
	UiId navId = Ui_Id("test.nav.popup", 0, 0);
	UiPopupManager_Init(&pm);
	CHECK(Ui_PushInputOwner(&g_ctx, dialogId) == 1);
	CHECK(g_ctx.inputOwner == dialogId);
	UiPopupManager_Open(&pm, &g_ctx, navId, navId, 0);
	CHECK(pm.scope == dialogId);
	UiPopupManager_BeginDraw(&pm, &g_ctx);
	CHECK(g_ctx.inputScope == dialogId);
	UiPopupManager_EndDraw(&pm, &g_ctx);
	CHECK(g_ctx.inputScope == UI_ID_NONE);
	UiPopupManager_Close(&pm, &g_ctx);
	Ui_PopInputOwner(&g_ctx, dialogId);
	CHECK(g_ctx.inputOwner == UI_ID_NONE);
}

static void test_dynamic_storage(void)
{
	int i, cap0, an0, ok;
	Ui_BeginFrame(&g_ctx);
	for (i = 0; i < 2000; i++)
		Ui_RegisterWidget(&g_ctx, Ui_Id("dyn.widget", i, 0),
			Ui_Rect(0, 0, 1, 1));
	CHECK(g_ctx.widgetCount == 2000);
	CHECK(g_ctx.widgetCap >= 2000);
	CHECK(g_ctx.widgetOom == 0);
	for (i = 0; i < 200; i++)
		Ui_RegisterWheelTarget(&g_ctx, Ui_Id("dyn.wheel", i, 0),
			Ui_Rect(0, 0, 10, 10));
	CHECK(g_ctx.wheelTargetCount == 200);
	CHECK(g_ctx.wheelCap >= 200);
	Ui_EndFrame(&g_ctx);

	cap0 = g_ctx.animCap;
	for (i = 0; i < 320; i++)
		Ui_AnimGet(&g_ctx, Ui_Id("dyn.anim", i, 0), UI_AN_HOVER, 0.0f);
	CHECK(g_ctx.animCap >= 320);
	CHECK(g_ctx.animCap > cap0);
	an0 = g_ctx.animCap;

	ok = Ui_PushInputOwner(&g_ctx, Ui_Id("owner.a", 0, 0));
	ok &= Ui_PushInputOwner(&g_ctx, Ui_Id("owner.b", 0, 0));
	CHECK(ok == 1);
	CHECK(g_ctx.inputOwner == Ui_Id("owner.b", 0, 0));
	Ui_PopInputOwner(&g_ctx, Ui_Id("owner.b", 0, 0));
	CHECK(g_ctx.inputOwner == Ui_Id("owner.a", 0, 0));
	Ui_PopInputOwner(&g_ctx, Ui_Id("owner.a", 0, 0));
	CHECK(g_ctx.inputOwner == UI_ID_NONE);

	/* 乱序移除: 移除栈中间项后栈顶保持不变 */
	Ui_PushInputOwner(&g_ctx, Ui_Id("owner.a", 0, 0));
	Ui_PushInputOwner(&g_ctx, Ui_Id("owner.b", 0, 0));
	Ui_PushInputOwner(&g_ctx, Ui_Id("owner.c", 0, 0));
	Ui_PopInputOwner(&g_ctx, Ui_Id("owner.b", 0, 0));
	CHECK(g_ctx.inputOwner == Ui_Id("owner.c", 0, 0));
	CHECK(g_ctx.inputOwnerDepth == 2);
	Ui_PopInputOwner(&g_ctx, Ui_Id("owner.c", 0, 0));
	CHECK(g_ctx.inputOwner == Ui_Id("owner.a", 0, 0));
	Ui_PopInputOwner(&g_ctx, Ui_Id("owner.a", 0, 0));
	CHECK(g_ctx.inputOwner == UI_ID_NONE);

	Ui_ContextShutdown(&g_ctx);
	CHECK(g_ctx.widgets == NULL && g_ctx.wheelTargets == NULL &&
		g_ctx.anim == NULL);
	CHECK(g_ctx.widgetCount == 0 && g_ctx.wheelTargetCount == 0);
	CHECK(g_ctx.animCap == 0);
	CHECK(an0 >= 320);
}

/* ── 入口 ───────────────────────────────────────────────────────────── */

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
	wc.lpszClassName = L"cui_nav_test";
	CHECK(RegisterClassExW(&wc) != 0);
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_nav_test", L"", WS_POPUP, 0, 0, 760, 720,
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

			build_many();
			build_wide();
			test_selected_path();
			test_keyboard_levels();
			test_no_focus_enter();
			test_panel_flip_no_overlap();
			test_overflow();
			test_vertical_rail();
			test_long_panel();
			test_popup_scope();
			test_dynamic_storage();

			CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
			UiTheme_Shutdown(&g_theme, g_gfx);
			Gfx_Shutdown(g_gfx);
		}
		DestroyWindow(hwnd);
	}
	CoUninitialize();
	sprintf(line, "ui_nav_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
