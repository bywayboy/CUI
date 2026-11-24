/*
 * designer_test.c - .cui 模型 / JSON / 布局 / 代码生成 headless 断言
 */
#include <windows.h>
#include <string.h>
#include "ui/ui_design.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);

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

/* 建一个宿主 + 同名视图, 返回视图下标. */
static int mk_view_host(UiDesignDoc *d, int group, int kind, const char *id)
{
	int v = UiDesign_AddView(d, id, id);
	int h = UiDesign_AddHost(d, id, id, kind, group);
	UiDesign_View(d, v)->group = group;
	UiDesign_Host(d, h)->view = v;
	return v;
}

static void test_json_roundtrip(void)
{
	UiDesignDoc a, b;
	char *json;
	int label;
	UiDesign_Init(&a);
	CHECK(UiDesign_DefaultDoc(&a) == 1);
	CHECK(a.groupCount == 1 && a.hostCount == 1 && a.nodeCount >= 3);
	CHECK(UiDesign_Node(&a, a.views[0].root) != NULL);
	label = UiDesign_FindNode(&a, 0, "title");
	CHECK(label >= 0);
	UiDesign_Node(&a, label)->textAlign = UI_DN_ALIGN_END;
	UiDesign_Node(&a, label)->textVAlign = UI_DN_VALIGN_TOP;
	json = UiDesign_ToJson(&a);
	CHECK(json != NULL && strlen(json) > 100);
	UiDesign_Init(&b);
	if (!UiDesign_FromJson(&b, json)) {
		out_("parse failed\n");
		HeapFree(GetProcessHeap(), 0, json);
		UiDesign_Free(&a);
		UiDesign_Free(&b);
		return;
	}
	CHECK(b.groupCount == 1 && b.hostCount == 1);
	CHECK(strcmp(b.groups[0].id, "main") == 0);
	CHECK(b.views[0].root >= 0);
	CHECK(UiDesign_FindNode(&b, 0, "title") >= 0);
	CHECK(strcmp(UiDesign_Node(&b, UiDesign_FindNode(&b, 0, "title"))->text,
		"新建窗口") == 0);
	CHECK(UiDesign_Node(&b, UiDesign_FindNode(&b, 0, "title"))->textAlign ==
		UI_DN_ALIGN_END);
	CHECK(UiDesign_Node(&b, UiDesign_FindNode(&b, 0, "title"))->textVAlign ==
		UI_DN_VALIGN_TOP);
	HeapFree(GetProcessHeap(), 0, json);
	UiDesign_Free(&a);
	UiDesign_Free(&b);
}

static void test_layout(void)
{
	UiDesignDoc d;
	UiRect *rects;
	int g, w, root, a, b, h;
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->padding[0] = 10;
	UiDesign_Node(&d, root)->padding[1] = 10;
	UiDesign_Node(&d, root)->padding[2] = 10;
	UiDesign_Node(&d, root)->padding[3] = 10;
	UiDesign_Node(&d, root)->gap = 8;
	a = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "a", "A");
	UiDesign_Node(&d, a)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->h = 20;
	b = UiDesign_AddNode(&d, w, UI_DN_BUTTON, root, "b", "B");
	UiDesign_Node(&d, b)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, b)->h = 32;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 200, 100), rects, d.nodeCount) == 1);
	CHECK(rects[root].x == 0 && rects[root].w == 200);
	CHECK(rects[a].x == 10 && rects[a].y == 10 && rects[a].w == 180);
	CHECK(rects[b].y == 38);
	CHECK(UiDesign_FindNodeAt(&d, w, rects, 12, 12) == a);
	HeapFree(GetProcessHeap(), 0, rects);
	UiDesign_Free(&d);

	/* Absolute + 右侧锚点 */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_ABSOLUTE, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	h = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "a", "A");
	UiDesign_Node(&d, h)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, h)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, h)->w = 50;
	UiDesign_Node(&d, h)->h = 20;
	UiDesign_Node(&d, h)->anchors = UI_DN_ANCHOR_RIGHT;
	UiDesign_Node(&d, h)->margin[2] = 10;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 200), rects, d.nodeCount) == 1);
	CHECK(rects[h].x == 240 && rects[h].w == 50);
	HeapFree(GetProcessHeap(), 0, rects);
	UiDesign_Free(&d);

	/* Row + grid */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_ROW, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->gap = 10;
	a = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "a", "A");
	UiDesign_Node(&d, a)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->w = 50; UiDesign_Node(&d, a)->h = 20;
	b = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "b", "B");
	UiDesign_Node(&d, b)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, b)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, b)->w = 60; UiDesign_Node(&d, b)->h = 20;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 60), rects, d.nodeCount) == 1);
	/* Grid: 2 列, 0 行=按子节点自动, 间距 10 */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_GRID, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->gridCols = 2;
	UiDesign_Node(&d, root)->gridRows = 0;
	UiDesign_Node(&d, root)->gridColGap = 10;
	UiDesign_Node(&d, root)->gridRowGap = 10;
	a = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "a", "A");
	b = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "b", "B");
	UiDesign_Node(&d, a)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->w = 40; UiDesign_Node(&d, a)->h = 20;
	UiDesign_Node(&d, b)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, b)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, b)->w = 40; UiDesign_Node(&d, b)->h = 20;
	{
		int c3 = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "c", "C");
		UiDesign_Node(&d, c3)->wMode = UI_DN_SIZE_FIXED;
		UiDesign_Node(&d, c3)->hMode = UI_DN_SIZE_FIXED;
		UiDesign_Node(&d, c3)->w = 40; UiDesign_Node(&d, c3)->h = 20;
		rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			(SIZE_T)d.nodeCount * sizeof(UiRect));
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 220, 100), rects, d.nodeCount) == 1);
		CHECK(rects[a].x == 0 && rects[a].y == 12.5f);
		CHECK(rects[b].x == 115 && rects[b].y == 12.5f);
		CHECK(rects[c3].x == 0 && rects[c3].y == 67.5f);
		HeapFree(GetProcessHeap(), 0, rects);
	}
	UiDesign_Free(&d);

	/* Column/Row 交叉轴 FILL: 子控件应铺满容器 */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->padding[0] = 10;
	UiDesign_Node(&d, root)->padding[1] = 10;
	UiDesign_Node(&d, root)->padding[2] = 10;
	UiDesign_Node(&d, root)->padding[3] = 10;
	UiDesign_Node(&d, root)->gap = 8;
	a = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "a", "A");
	UiDesign_Node(&d, a)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, a)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->h = 20;
	b = UiDesign_AddNode(&d, w, UI_DN_INPUT, root, "b", "B");
	UiDesign_Node(&d, b)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, b)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, b)->h = 34;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 120), rects, d.nodeCount) == 1);
	CHECK(rects[a].x == 10 && rects[a].w == 280);
	CHECK(rects[b].x == 10 && rects[b].w == 280);
	CHECK(rects[b].y == 38);
	HeapFree(GetProcessHeap(), 0, rects);
	UiDesign_Free(&d);

	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_ROW, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->padding[0] = 10;
	UiDesign_Node(&d, root)->padding[1] = 10;
	UiDesign_Node(&d, root)->padding[2] = 10;
	UiDesign_Node(&d, root)->padding[3] = 10;
	a = UiDesign_AddNode(&d, w, UI_DN_INPUT, root, "a", "A");
	UiDesign_Node(&d, a)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, a)->w = 80;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 100), rects, d.nodeCount) == 1);
	CHECK(rects[a].x == 10 && rects[a].y == 10 && rects[a].h == 80);
	HeapFree(GetProcessHeap(), 0, rects);
	UiDesign_Free(&d);
}

static int has_sub(const char *s, const char *needle)
{
	int i, k;
	for (i = 0; s[i]; i++) {
		for (k = 0; needle[k] && s[i + k] == needle[k]; k++) {}
		if (!needle[k]) return 1;
	}
	return 0;
}

static int read_all(const wchar_t *path, char *buf, int cap)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD got = 0;
	if (h == INVALID_HANDLE_VALUE) return 0;
	if (!ReadFile(h, buf, (DWORD)cap - 1, &got, NULL)) {
		CloseHandle(h);
		return 0;
	}
	CloseHandle(h);
	buf[got] = 0;
	return 1;
}

static void test_codegen(void)
{
	UiDesignDoc d;
	UiDesignGroup *g;
	char report[256];
	char buf[16384];
	int r, w2, root2, wroot;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	UiDesign_Init(&d);
	UiDesign_DefaultDoc(&d);
	g = &d.groups[0];
	w2 = mk_view_host(&d, 0, UI_DH_DIALOG, "dialog1");
	root2 = UiDesign_AddNode(&d, w2, UI_DN_COLUMN, -1, "droot", "Dialog Root");
	UiDesign_Node(&d, root2)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root2)->hMode = UI_DN_SIZE_FILL;
	wroot = UiDesign_AddNode(&d, w2, UI_DN_BUTTON, root2, "dbtn", "Dialog Button");
	UiDesign_Node(&d, wroot)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, wroot)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, wroot)->w = 100;
	UiDesign_Node(&d, wroot)->h = 32;
	UiDesign_View(&d, w2)->root = root2;
	UiDesign_SetText(g->outputC, UI_DN_PATH_MAX, "designer_test.ui.c");
	UiDesign_SetText(g->outputH, UI_DN_PATH_MAX, "designer_test.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	r = UiDesign_GenerateGroup(&d, 0, L"build\\gen", report, sizeof(report));
	CHECK(r == 2);
	lstrcpyW(pc, L"build\\gen\\designer_test.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_test.ui.h");
	CHECK(GetFileAttributesW(pc) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(ph) != INVALID_FILE_ATTRIBUTES);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "Main_Doc"));
	CHECK(has_sub(buf, "Main_main_Draw"));
	CHECK(has_sub(buf, "Main_dialog1_DialogOpen"));
	CHECK(has_sub(buf, "\"dialog1\""));
	CHECK(read_all(ph, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "Main_HOST_MAIN"));
	CHECK(has_sub(buf, "Main_HOST_DIALOG1"));
	DeleteFileW(pc);
	DeleteFileW(ph);
	UiDesign_Free(&d);
}

static void test_preview(void)
{
	UiDesignDoc d;
	UiDesignGenFile f[4];
	char report[128];
	int n;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	UiDesign_Init(&d);
	UiDesign_DefaultDoc(&d);
	UiDesign_SetText(d.groups[0].outputC, UI_DN_PATH_MAX, "designer_prev.ui.c");
	UiDesign_SetText(d.groups[0].outputH, UI_DN_PATH_MAX, "designer_prev.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_prev.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_prev.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	n = UiDesign_PreviewAll(&d, L"build\\gen", f, 4, report, sizeof(report));
	CHECK(n == 2);
	CHECK(f[0].status == 0 && f[1].status == 0);
	CHECK(strcmp(f[0].name, "designer_prev.ui.c") == 0);
	CHECK(strcmp(f[1].name, "designer_prev.ui.h") == 0);
	CHECK(UiDesign_GenerateGroup(&d, 0, L"build\\gen", report, sizeof(report)) == 2);
	n = UiDesign_PreviewAll(&d, L"build\\gen", f, 4, report, sizeof(report));
	CHECK(n == 2 && f[0].status == 2 && f[1].status == 2);
	UiDesign_SetText(d.hosts[0].title, 128, "Changed Title");
	n = UiDesign_PreviewAll(&d, L"build\\gen", f, 4, report, sizeof(report));
	CHECK(n == 2 && f[0].status == 1);
	CHECK(f[0].added == 1 && f[0].removed == 0);
	DeleteFileW(pc);
	DeleteFileW(ph);
	UiDesign_Free(&d);
}

static void test_window_generate(void)
{
	UiDesignDoc d;
	char report[128];
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	int r;
	UiDesign_Init(&d);
	UiDesign_DefaultDoc(&d);
	UiDesign_SetText(d.hosts[0].id, UI_DN_ID_MAX, "solo_win");
	d.hosts[0].group = -1;      /* 未分组: 全部生成时单独输出 */
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\solo_win.ui.c");
	lstrcpyW(ph, L"build\\gen\\solo_win.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	r = UiDesign_GenerateHost(&d, 0, L"build\\gen", report, sizeof(report));
	CHECK(r == 2);
	CHECK(GetFileAttributesW(pc) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(ph) != INVALID_FILE_ATTRIBUTES);
	DeleteFileW(pc);
	DeleteFileW(ph);
	r = UiDesign_GenerateAll(&d, L"build\\gen", report, sizeof(report));
	CHECK(r == 4);                /* 空生成组 2 + 未分组窗口 2 */
	CHECK(GetFileAttributesW(pc) != INVALID_FILE_ATTRIBUTES);
	DeleteFileW(pc);
	DeleteFileW(ph);
	DeleteFileW(L"build\\gen\\main.ui.c");
	DeleteFileW(L"build\\gen\\main.ui.h");
	UiDesign_Free(&d);
}

static void test_save_load(void)
{
	UiDesignDoc a, b;
	CHECK(UiDesign_DefaultDoc(&a) == 1);
	UiDesign_SetText(a.hosts[0].id, UI_DN_ID_MAX, "saved_win");
	CHECK(UiDesign_SaveFile(&a, L"build\\gen\\designer_test.cui") == 1);
	UiDesign_Init(&b);
	CHECK(UiDesign_LoadFile(&b, L"build\\gen\\designer_test.cui") == 1);
	CHECK(strcmp(b.hosts[0].id, "saved_win") == 0);
	CHECK(b.views[0].root >= 0);
	CHECK(strcmp(b.groups[0].id, "main") == 0);
	UiDesign_Free(&a);
	UiDesign_Free(&b);
	DeleteFileW(L"build\\gen\\designer_test.cui");
}

static void test_symbol(void)
{
	char b[64];
	UiDesign_Symbol("main-window", b, sizeof(b));
	CHECK(strcmp(b, "main_window") == 0);
	UiDesign_Symbol("123abc", b, sizeof(b));
	CHECK(strcmp(b, "Ui_123abc") == 0);
}

static void test_components(void)
{
	UiDesignDoc a, b;
	UiRect *rects;
	char *json;
	char report[128];
	static char buf[32768];
	int g, w, root, tabs, p1, p2, ta, dp, cs, img, hit;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	UiDesignNode *n;

	UiDesign_Init(&a);
	g = UiDesign_AddGroup(&a, "g", "G");
	w = mk_view_host(&a, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&a, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&a, w)->root = root;
	UiDesign_Node(&a, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&a, root)->hMode = UI_DN_SIZE_FILL;
	tabs = UiDesign_AddNode(&a, w, UI_DN_TABS, root, "tabs", "标签页");
	n = UiDesign_Node(&a, tabs);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FIXED;
	n->h = 220.0f;
	n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 10.0f;
	n->selected = 1;
	p1 = UiDesign_AddNode(&a, w, UI_DN_PANEL, tabs, "page1", "页 1");
	p2 = UiDesign_AddNode(&a, w, UI_DN_PANEL, tabs, "page2", "页 2");
	UiDesign_Node(&a, p1)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&a, p1)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&a, p2)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&a, p2)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&a, p1)->radius = 4.0f;
	ta = UiDesign_AddNode(&a, w, UI_DN_TEXTAREA, p1, "ta", "多行");
	UiDesign_NodeSetText(UiDesign_Node(&a, ta), "第一行");
	UiDesign_NodeSetHint(UiDesign_Node(&a, ta), "输入内容");
	UiDesign_Node(&a, ta)->readonly = 1;
	dp = UiDesign_AddNode(&a, w, UI_DN_DATEPICKER, p1, "dp", "日期");
	UiDesign_Node(&a, dp)->mode = 3;
	UiDesign_NodeSetText(UiDesign_Node(&a, dp), "2026-01-02 03:04");
	UiDesign_NodeSetOptions(UiDesign_Node(&a, dp), "2026-02-03 04:05");
	cs = UiDesign_AddNode(&a, w, UI_DN_COLORSEL, p2, "cs", "颜色");
	UiDesign_Node(&a, cs)->color = 0x80336699u;
	img = UiDesign_AddNode(&a, w, UI_DN_IMAGE, p2, "img", "图片");
	UiDesign_Node(&a, img)->radius = 10.0f;
	UiDesign_Node(&a, img)->x = 200.0f;

	CHECK(UiDesign_TypeFromName("tabs") == UI_DN_TABS);
	CHECK(UiDesign_TypeFromName("textarea") == UI_DN_TEXTAREA);
	CHECK(UiDesign_TypeFromName("datepicker") == UI_DN_DATEPICKER);
	CHECK(UiDesign_TypeFromName("colorsel") == UI_DN_COLORSEL);
	CHECK(UiDesign_TypeFromName(UiDesign_TypeName(UI_DN_COLORSEL)) == UI_DN_COLORSEL);

	/* 布局: 所有页共用 header 下方的页区 */
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)a.nodeCount * sizeof(UiRect));
	CHECK(UiDesign_Layout(&a, w, Ui_Rect(0, 0, 300, 300), rects, a.nodeCount) == 1);
	CHECK(rects[p1].x == 10 && rects[p1].w == 280);
	CHECK(rects[p1].y == 48 && rects[p1].h == 162);
	CHECK(rects[p2].x == rects[p1].x && rects[p2].y == rects[p1].y);
	CHECK(rects[p2].w == rects[p1].w && rects[p2].h == rects[p1].h);
	hit = UiDesign_FindNodeAt(&a, w, rects, 20, 20);
	CHECK(hit == tabs);
	hit = UiDesign_FindNodeAt(&a, w, rects, 20, 60);
	CHECK(hit == cs || hit == p2);
	HeapFree(GetProcessHeap(), 0, rects);
	json = UiDesign_ToJson(&a);
	CHECK(json != NULL);
	if (json) CHECK(has_sub(json, "\"mode\":3"));
	CHECK(json && has_sub(json, "\"color\":\"#80336699\""));
	CHECK(json && has_sub(json, "\"readonly\":1"));
	CHECK(json && has_sub(json, "\"type\":\"tabs\""));
	CHECK(json && has_sub(json, "\"radius\":4.000"));
	CHECK(json && has_sub(json, "\"radius\":10.000"));
	UiDesign_Init(&b);
	if (json && UiDesign_FromJson(&b, json)) {
		int bt = UiDesign_FindNode(&b, 0, "tabs");
		int bta = UiDesign_FindNode(&b, 0, "ta");
		int bdp = UiDesign_FindNode(&b, 0, "dp");
		int bcs = UiDesign_FindNode(&b, 0, "cs");
		CHECK(bt >= 0 && UiDesign_Node(&b, bt)->type == UI_DN_TABS);
		CHECK(bt >= 0 && UiDesign_Node(&b, bt)->selected == 1);
		CHECK(bta >= 0 && UiDesign_Node(&b, bta)->readonly == 1);
		CHECK(bdp >= 0 && UiDesign_Node(&b, bdp)->mode == 3);
		CHECK(bdp >= 0 && strcmp(UiDesign_Node(&b, bdp)->options, "2026-02-03 04:05") == 0);
		CHECK(bcs >= 0 && UiDesign_Node(&b, bcs)->color == 0x80336699u);
	} else {
		CHECK(0);
	}
	if (json) HeapFree(GetProcessHeap(), 0, json);

	/* 代码生成包含新字段 */
	UiDesign_SetText(a.groups[g].outputC, UI_DN_PATH_MAX, "designer_comp.ui.c");
	UiDesign_SetText(a.groups[g].outputH, UI_DN_PATH_MAX, "designer_comp.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_comp.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_comp.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&a, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, ", 15, "));
	CHECK(has_sub(buf, "->mode=3"));
	CHECK(has_sub(buf, "->readonly=1"));
	CHECK(has_sub(buf, "->color=0x80336699u"));
	CHECK(has_sub(buf, "->selected=1;"));
	CHECK(has_sub(buf, "p2->radius=4.000f;"));
	CHECK(has_sub(buf, "p7->radius=10.000f;"));
	/* 默认值/该类型用不到的字段不应生成 */
	CHECK(!has_sub(buf, "p0->type="));
	CHECK(!has_sub(buf, "p0->value="));
	CHECK(!has_sub(buf, "p0->checked="));
	CHECK(!has_sub(buf, "p0->color="));
	DeleteFileW(pc);
	DeleteFileW(ph);

	UiDesign_Free(&a);
	UiDesign_Free(&b);
}

static void test_tabs_pages(void)
{
	UiDesignDoc d, m, r;
	UiDesignNode *n;
	char *json;
	char id[32], name[32];
	int g, w, root, tabs, p1, p2, p3, i, count, pageNode;

	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	tabs = UiDesign_AddNode(&d, w, UI_DN_TABS, root, "tabs", "Tabs");
	CHECK(UiDesign_TabsPageCount(&d, tabs) == 0);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == -1);
	CHECK(UiDesign_Node(&d, tabs)->selected == -1);
	CHECK(UiDesign_TabsPageCount(&d, root) == -1);

	p1 = UiDesign_TabsInsertPage(&d, tabs, 0, "page1", "页 1");
	CHECK(p1 >= 0);
	CHECK(UiDesign_TabsPageCount(&d, tabs) == 1);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 0);
	n = UiDesign_Node(&d, p1);
	CHECK(n->type == UI_DN_COLUMN && n->wMode == UI_DN_SIZE_FILL &&
		n->hMode == UI_DN_SIZE_FILL);
	CHECK(n->padding[0] == 12.0f && n->gap == 8.0f);

	p2 = UiDesign_TabsInsertPage(&d, tabs, 1, "page2", "页 2");
	CHECK(p2 >= 0 && UiDesign_TabsPageCount(&d, tabs) == 2);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 1);
	p3 = UiDesign_TabsInsertPage(&d, tabs, 1, "page3", "页 3");
	CHECK(p3 >= 0 && UiDesign_TabsPageCount(&d, tabs) == 3);
	CHECK(UiDesign_TabsPageAt(&d, tabs, 1) == p3);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 1);

	UiDesign_TabsSetSelectedPage(&d, tabs, 2);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 2);
	CHECK(UiDesign_TabsPageAt(&d, tabs, 2) == p2);
	UiDesign_TabsRemovePage(&d, tabs, 0);
	CHECK(UiDesign_TabsPageCount(&d, tabs) == 2);
	CHECK(UiDesign_TabsPageAt(&d, tabs, 0) == p3);
	CHECK(UiDesign_TabsPageAt(&d, tabs, 1) == p2);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 1);
	UiDesign_TabsRemovePage(&d, tabs, 1);
	CHECK(UiDesign_TabsPageCount(&d, tabs) == 1);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 0);
	CHECK(UiDesign_TabsPageAt(&d, tabs, 0) == p3);
	UiDesign_TabsRemovePage(&d, tabs, 0);
	CHECK(UiDesign_TabsPageCount(&d, tabs) == 0);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == -1);

	p1 = UiDesign_TabsInsertPage(&d, tabs, 99, "page1", "页 1");
	CHECK(p1 >= 0 && UiDesign_TabsPageCount(&d, tabs) == 1);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 0);

	/* 越界 selected 在 JSON 读取时归一化 */
	UiDesign_Node(&d, tabs)->selected = 77;
	json = UiDesign_ToJson(&d);
	CHECK(json != NULL);
	UiDesign_Init(&r);
	if (json && UiDesign_FromJson(&r, json)) {
		int bt = UiDesign_FindNode(&r, 0, "tabs");
		CHECK(bt >= 0);
		CHECK(UiDesign_TabsPageCount(&r, bt) == 1);
		CHECK(UiDesign_TabsSelectedPage(&r, bt) == 0);
		CHECK(UiDesign_Node(&r, bt)->selected == 0);
	} else {
		CHECK(0);
	}
	if (json) HeapFree(GetProcessHeap(), 0, json);
	UiDesign_Free(&r);

	/* 32 页上限 */
	UiDesign_TabsRemovePage(&d, tabs, 0);
	for (i = 0; i < UI_DN_TABS_MAX_PAGES; i++) {
		sprintf(id, "page%d", i + 1);
		sprintf(name, "页 %d", i + 1);
		CHECK(UiDesign_TabsInsertPage(&d, tabs, i, id, name) >= 0);
	}
	CHECK(UiDesign_TabsPageCount(&d, tabs) == UI_DN_TABS_MAX_PAGES);
	CHECK(UiDesign_TabsInsertPage(&d, tabs, 0, "over", "超限") == -1);
	CHECK(UiDesign_TabsPageCount(&d, tabs) == UI_DN_TABS_MAX_PAGES);

	UiDesign_TabsSetSelectedPage(&d, tabs, 0);
	pageNode = UiDesign_TabsPageAt(&d, tabs, 0);
	CHECK(UiDesign_TabsMovePage(&d, tabs, 0, UI_DN_TABS_MAX_PAGES - 1) == 1);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == UI_DN_TABS_MAX_PAGES - 1);
	CHECK(UiDesign_TabsPageAt(&d, tabs, UI_DN_TABS_MAX_PAGES - 1) == pageNode);
	UiDesign_Free(&d);

	/* 重排 / 通用删除后当前页跟随页面身份 */
	UiDesign_Init(&m);
	g = UiDesign_AddGroup(&m, "g", "G");
	w = mk_view_host(&m, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&m, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&m, w)->root = root;
	tabs = UiDesign_AddNode(&m, w, UI_DN_TABS, root, "tabs", "Tabs");
	p1 = UiDesign_TabsInsertPage(&m, tabs, 0, "a", "A");
	p2 = UiDesign_TabsInsertPage(&m, tabs, 1, "b", "B");
	p3 = UiDesign_TabsInsertPage(&m, tabs, 2, "c", "C");
	CHECK(p1 >= 0 && p2 >= 0 && p3 >= 0);
	UiDesign_TabsSetSelectedPage(&m, tabs, 0);
	CHECK(UiDesign_TabsMovePage(&m, tabs, 0, 2) == 1);
	CHECK(UiDesign_TabsPageAt(&m, tabs, 0) == p2);
	CHECK(UiDesign_TabsPageAt(&m, tabs, 1) == p3);
	CHECK(UiDesign_TabsPageAt(&m, tabs, 2) == p1);
	CHECK(UiDesign_TabsSelectedPage(&m, tabs) == 2);
	CHECK(UiDesign_NodeMoveUp(&m, p3) == 1);
	CHECK(UiDesign_TabsPageAt(&m, tabs, 0) == p3);
	CHECK(UiDesign_TabsSelectedPage(&m, tabs) == 2);
	CHECK(UiDesign_TabsPageAt(&m, tabs, 2) == p1);
	CHECK(UiDesign_NodeRemove(&m, p2) == 1);
	CHECK(UiDesign_TabsPageCount(&m, tabs) == 2);
	CHECK(UiDesign_TabsSelectedPage(&m, tabs) == 1);
	CHECK(UiDesign_TabsPageAt(&m, tabs, 1) == p1);
	CHECK(UiDesign_NodeRemove(&m, p1) == 1);
	CHECK(UiDesign_TabsPageCount(&m, tabs) == 1);
	CHECK(UiDesign_TabsSelectedPage(&m, tabs) == 0);
	CHECK(UiDesign_TabsPageAt(&m, tabs, 0) == p3);
	UiDesign_Node(&m, tabs)->selected = 9;
	UiDesign_TabsNormalize(&m, tabs);
	CHECK(UiDesign_Node(&m, tabs)->selected == 0);
	CHECK(UiDesign_TabsSetSelectedPage(&m, tabs, 99) == 0);

	count = UiDesign_TabsPageCount(&m, tabs);
	CHECK(count == 1);
	UiDesign_Free(&m);
}

static void test_ringprogress_node(void)
{
	UiDesignDoc a, b;
	char *json;
	char report[128];
	static char buf[32768];
	int g, w, root, r1, r2;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	UiDesignNode *n;

	UiDesign_Init(&a);
	g = UiDesign_AddGroup(&a, "g", "G");
	w = mk_view_host(&a, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&a, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&a, w)->root = root;
	UiDesign_Node(&a, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&a, root)->hMode = UI_DN_SIZE_FILL;
	r1 = UiDesign_AddNode(&a, w, UI_DN_RINGPROGRESS, root, "ring", "圆环");
	r2 = UiDesign_AddNode(&a, w, UI_DN_RINGPROGRESS, root, "ring2", "圆环2");

	CHECK(UiDesign_TypeFromName("ringprogress") == UI_DN_RINGPROGRESS);
	CHECK(UiDesign_TypeFromName(UiDesign_TypeName(UI_DN_RINGPROGRESS)) ==
		UI_DN_RINGPROGRESS);
	n = UiDesign_Node(&a, r1);
	CHECK(n->w == 160.0f && n->h == 160.0f);
	CHECK(n->ringMax == 100.0f && n->ringWidth == 14.0f);
	CHECK(n->checked == 1 && n->color == 0xFF4F6FF3u);
	CHECK(n->value == 0.5f);

	n->value = 0.72f;
	n->ringMax = 10.0f;
	n->ringWidth = 18.0f;
	n->checked = 0;
	n->color = 0x802563EBu;
	UiDesign_NodeSetText(n, "存储空间");
	UiDesign_NodeSetHint(n, "GB");
	json = UiDesign_ToJson(&a);
	CHECK(json != NULL);
	CHECK(json && has_sub(json, "\"type\":\"ringprogress\""));
	CHECK(json && has_sub(json, "\"ringMax\":10"));
	CHECK(json && has_sub(json, "\"ringWidth\":18"));
	CHECK(json && has_sub(json, "\"checked\":0"));
	CHECK(json && has_sub(json, "\"color\":\"#802563eb\""));
	CHECK(json && has_sub(json, "\"text\":\"存储空间\""));
	CHECK(json && has_sub(json, "\"hint\":\"GB\""));
	UiDesign_Init(&b);
	if (json && UiDesign_FromJson(&b, json)) {
		int br1 = UiDesign_FindNode(&b, 0, "ring");
		int br2 = UiDesign_FindNode(&b, 0, "ring2");
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->type == UI_DN_RINGPROGRESS);
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->ringMax == 10.0f);
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->ringWidth == 18.0f);
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->checked == 0);
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->color == 0x802563EBu);
		CHECK(br1 >= 0 && strcmp(UiDesign_Node(&b, br1)->text,
			"存储空间") == 0);
		CHECK(br1 >= 0 && strcmp(UiDesign_Node(&b, br1)->hint, "GB") == 0);
		CHECK(br2 >= 0 && UiDesign_Node(&b, br2)->ringMax == 100.0f);
		CHECK(br2 >= 0 && UiDesign_Node(&b, br2)->ringWidth == 14.0f);
		CHECK(br2 >= 0 && UiDesign_Node(&b, br2)->checked == 1);
		CHECK(br2 >= 0 && UiDesign_Node(&b, br2)->color == 0xFF4F6FF3u);
	} else {
		CHECK(0);
	}
	if (json) HeapFree(GetProcessHeap(), 0, json);

	UiDesign_SetText(a.groups[g].outputC, UI_DN_PATH_MAX, "designer_ring.ui.c");
	UiDesign_SetText(a.groups[g].outputH, UI_DN_PATH_MAX, "designer_ring.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_ring.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_ring.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&a, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, ", 20, "));
	CHECK(has_sub(buf, "p1->value=0.720f;"));
	CHECK(has_sub(buf, "p1->ringMax=10.000f;"));
	CHECK(has_sub(buf, "p1->ringWidth=18.000f;"));
	CHECK(has_sub(buf, "p1->checked=0;"));
	CHECK(has_sub(buf, "p1->color=0x802563ebu;"));
	CHECK(has_sub(buf, "p1, \"存储空间\""));
	CHECK(has_sub(buf, "p1, \"GB\""));
	/* 默认圆环不输出无效/默认字段 */
	CHECK(!has_sub(buf, "p2->value="));
	CHECK(!has_sub(buf, "p2->checked="));
	CHECK(!has_sub(buf, "p2->color="));
	CHECK(!has_sub(buf, "p2->ringMax="));
	CHECK(!has_sub(buf, "p2->ringWidth="));
	DeleteFileW(pc);
	DeleteFileW(ph);

	UiDesign_Free(&a);
	UiDesign_Free(&b);
}

static void test_qrcode_node(void)
{
	UiDesignDoc a, b;
	char *json;
	char report[128];
	static char buf[32768];
	int g, w, root, q1, q2;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	UiDesignNode *n;

	UiDesign_Init(&a);
	g = UiDesign_AddGroup(&a, "g", "G");
	w = mk_view_host(&a, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&a, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&a, w)->root = root;
	UiDesign_Node(&a, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&a, root)->hMode = UI_DN_SIZE_FILL;
	q1 = UiDesign_AddNode(&a, w, UI_DN_QRCODE, root, "qr", "二维码");
	UiDesign_NodeSetText(UiDesign_Node(&a, q1), "https://example.com");
	q2 = UiDesign_AddNode(&a, w, UI_DN_QRCODE, root, "qr2", "二维码2");

	CHECK(UiDesign_TypeFromName("qrcode") == UI_DN_QRCODE);
	CHECK(UiDesign_TypeFromName(UiDesign_TypeName(UI_DN_QRCODE)) == UI_DN_QRCODE);

	/* 默认值: 黑前景/白背景/M; 尺寸 120x120 */
	n = UiDesign_Node(&a, q1);
	CHECK(n->ecc == UI_QR_ECC_M);
	CHECK(n->color == 0xFF000000u && n->bgColor == 0xFFFFFFFFu);
	CHECK(n->w == 120.0f && n->h == 120.0f);
	CHECK(n->logoSrc[0] == 0 && n->logoRatio == UI_QR_LOGO_RATIO_DEFAULT);
	CHECK(n->qrPad == UI_QR_PAD_DEFAULT);

	n->ecc = UI_QR_ECC_H;
	n->bgColor = 0xFFF5F5F5u;
	UiDesign_SetText(n->logoSrc, UI_DN_PATH_MAX, "assets/logo.png");
	n->logoRatio = 0.24f;
	n->qrPad = 2.5f;
	json = UiDesign_ToJson(&a);
	CHECK(json != NULL);
	CHECK(json && has_sub(json, "\"type\":\"qrcode\""));
	CHECK(json && has_sub(json, "\"ecc\":3"));
	CHECK(json && has_sub(json, "\"bgColor\":\"#fff5f5f5\""));
	CHECK(json && has_sub(json, "\"src\":\"assets/logo.png\""));
	CHECK(json && has_sub(json, "\"logoRatio\":0.24"));
	CHECK(json && has_sub(json, "\"qrPad\":2.5"));
	UiDesign_Init(&b);
	if (json && UiDesign_FromJson(&b, json)) {
		int bq = UiDesign_FindNode(&b, 0, "qr");
		int bq2 = UiDesign_FindNode(&b, 0, "qr2");
		CHECK(bq >= 0 && UiDesign_Node(&b, bq)->type == UI_DN_QRCODE);
		CHECK(bq >= 0 && UiDesign_Node(&b, bq)->ecc == UI_QR_ECC_H);
		CHECK(bq >= 0 && UiDesign_Node(&b, bq)->bgColor == 0xFFF5F5F5u);
		CHECK(bq >= 0 && strcmp(UiDesign_Node(&b, bq)->text,
			"https://example.com") == 0);
		CHECK(bq >= 0 && strcmp(UiDesign_Node(&b, bq)->logoSrc,
			"assets/logo.png") == 0);
		CHECK(bq >= 0 && UiDesign_Node(&b, bq)->logoRatio == 0.24f);
		CHECK(bq >= 0 && UiDesign_Node(&b, bq)->qrPad == 2.5f);
		/* 缺省字段回落: M / 黑 / 白 */
		CHECK(bq2 >= 0 && UiDesign_Node(&b, bq2)->ecc == UI_QR_ECC_M);
		CHECK(bq2 >= 0 && UiDesign_Node(&b, bq2)->color == 0xFF000000u);
		CHECK(bq2 >= 0 && UiDesign_Node(&b, bq2)->bgColor == 0xFFFFFFFFu);
		CHECK(bq2 >= 0 && UiDesign_Node(&b, bq2)->logoSrc[0] == 0);
		CHECK(bq2 >= 0 &&
			UiDesign_Node(&b, bq2)->logoRatio == UI_QR_LOGO_RATIO_DEFAULT);
		CHECK(bq2 >= 0 && UiDesign_Node(&b, bq2)->qrPad == UI_QR_PAD_DEFAULT);
	} else {
		CHECK(0);
	}
	if (json) HeapFree(GetProcessHeap(), 0, json);

	/* 生成: 只写非默认字段 */
	UiDesign_SetText(a.groups[g].outputC, UI_DN_PATH_MAX, "designer_qr.ui.c");
	UiDesign_SetText(a.groups[g].outputH, UI_DN_PATH_MAX, "designer_qr.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_qr.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_qr.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&a, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "UiDesign_AddNode(&gDoc, 0, 19,"));
	CHECK(has_sub(buf, "\"https://example.com\""));
	CHECK(has_sub(buf, "->ecc=3"));
	CHECK(has_sub(buf, "->bgColor=0xfff5f5f5u"));
	CHECK(has_sub(buf, "->logoRatio=0.24"));
	CHECK(has_sub(buf, "->qrPad=2.5"));
	CHECK(has_sub(buf, "->logoSrc, UI_DN_PATH_MAX, \"assets/logo.png\""));
	CHECK(!has_sub(buf, "->color="));    /* 前景色为默认黑, 不生成 */
	DeleteFileW(pc);
	DeleteFileW(ph);

	UiDesign_Free(&a);
	UiDesign_Free(&b);
}

static void test_grid_columns(void)
{
	UiDesignDoc d, b;
	UiRect *rects;
	char *json;
	char report[128];
	static char buf[32768];
	int g, w, root, a, c1, c2, sp;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	UiDesignNode *n;

	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_GRID, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	n = UiDesign_Node(&d, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->gridCols = 3;
	n->gridRows = 1;
	n->gridColGap = 10.0f;
	n->gridRowGap = 0.0f;
	UiDesign_SetText(n->gridColTracks, UI_DN_GRID_TRACKS_MAX, "100,1fr,auto");
	a = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "a", "A");
	c1 = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "b", "B");
	c2 = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "c", "C");
	UiDesign_Node(&d, a)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, a)->align = UI_DN_ALIGN_CENTER;
	UiDesign_Node(&d, a)->textAlign = UI_DN_ALIGN_CENTER;
	UiDesign_Node(&d, a)->textVAlign = UI_DN_VALIGN_TOP;
	UiDesign_Node(&d, c1)->wMode = UI_DN_SIZE_FILL;
	n = UiDesign_Node(&d, c2);
	n->wMode = UI_DN_SIZE_FIXED;
	n->w = 60.0f;
	n->gridCol = 2;
	UiDesign_Node(&d, a)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, a)->h = 20.0f;
	UiDesign_Node(&d, c1)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, c1)->h = 20.0f;
	UiDesign_Node(&d, c2)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, c2)->h = 20.0f;

	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 100), rects, d.nodeCount) == 1);
	/* 300 - 2*10 = 280; 固定 100 + auto 60, 剩余 120 给 1fr */
	CHECK(rects[a].x == 0.0f && rects[a].w == 100.0f);
	CHECK(rects[c1].x == 110.0f && rects[c1].w == 120.0f);
	CHECK(rects[c2].x == 240.0f && rects[c2].w == 60.0f);

	/* 跨列单元格宽度 = 两列宽 + 中间 gap */
	sp = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "sp", "Span");
	n = UiDesign_Node(&d, sp);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FIXED;
	n->h = 20.0f;
	n->gridCol = 0;
	n->colSpan = 2;
	{
		UiRect *r2 = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			(SIZE_T)d.nodeCount * sizeof(UiRect));
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 100), r2,
			d.nodeCount) == 1);
		CHECK(r2[sp].x == 0.0f && r2[sp].w == 230.0f);
		HeapFree(GetProcessHeap(), 0, r2);
	}

	/* 空规格 = 三列均分 */
	HeapFree(GetProcessHeap(), 0, rects);
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	UiDesign_SetText(UiDesign_Node(&d, root)->gridColTracks,
		UI_DN_GRID_TRACKS_MAX, "");
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 100), rects, d.nodeCount) == 1);
	CHECK(rects[a].w > 93.2f && rects[a].w < 93.5f);
	CHECK(rects[c1].x > 103.2f && rects[c1].x < 103.5f);
	UiDesign_SetText(UiDesign_Node(&d, root)->gridColTracks,
		UI_DN_GRID_TRACKS_MAX, "100,1fr,auto");
	HeapFree(GetProcessHeap(), 0, rects);

	/* JSON 往返 */
	json = UiDesign_ToJson(&d);
	CHECK(json != NULL);
	CHECK(json && has_sub(json, "\"gridColTracks\":\"100,1fr,auto\""));
	UiDesign_Init(&b);
	if (json && UiDesign_FromJson(&b, json)) {
		int br = UiDesign_FindNode(&b, 0, "root");
		CHECK(br >= 0 && strcmp(UiDesign_Node(&b, br)->gridColTracks,
			"100,1fr,auto") == 0);
	} else {
		CHECK(0);
	}
	if (json) HeapFree(GetProcessHeap(), 0, json);

	/* 代码生成带列宽规格 */
	UiDesign_SetText(d.groups[g].outputC, UI_DN_PATH_MAX, "designer_grid.ui.c");
	UiDesign_SetText(d.groups[g].outputH, UI_DN_PATH_MAX, "designer_grid.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_grid.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_grid.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&d, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "gridColTracks, UI_DN_GRID_TRACKS_MAX, \"100,1fr,auto\""));
	CHECK(has_sub(buf, "p1->align=1;"));
	CHECK(has_sub(buf, "p1->textAlign=1;"));
	CHECK(has_sub(buf, "p1->textVAlign=0;"));
	DeleteFileW(pc);
	DeleteFileW(ph);

	UiDesign_Free(&d);
	UiDesign_Free(&b);
}

static void test_view_host_model(void)
{
	static const char *v1 =
		"{\"format\":\"cui-design\",\"version\":1,\"name\":\"old\","
		"\"groups\":[{\"id\":\"main\",\"name\":\"G\","
		"\"outputC\":\"old.ui.c\",\"outputH\":\"old.ui.h\","
		"\"symbolPrefix\":\"Old\"}],"
		"\"windows\":[{\"id\":\"main\",\"name\":\"W\",\"title\":\"T\","
		"\"kind\":1,\"group\":\"main\",\"width\":400,\"height\":300,"
		"\"minWidth\":0,\"minHeight\":0,\"theme\":0,"
		"\"root\":{\"id\":\"root\",\"name\":\"R\",\"type\":\"column\","
		"\"layout\":{},\"props\":{},\"children\":[]}}]}";
	UiDesignDoc d;
	char report[128];
	static char buf[32768];
	int g, v1i, v2i, h1, h2, r;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];

	/* v1 自动迁移: 窗口 -> 视图 + 宿主 */
	UiDesign_Init(&d);
	CHECK(UiDesign_FromJson(&d, v1) == 1);
	CHECK(d.viewCount == 1 && d.hostCount == 1);
	CHECK(strcmp(d.views[0].id, "main") == 0);
	CHECK(d.views[0].root >= 0);
	CHECK(d.hosts[0].view == 0);
	CHECK(d.hosts[0].kind == UI_DH_DIALOG);
	CHECK(d.nodes[d.views[0].root].type == UI_DN_COLUMN);
	{
		char *j = UiDesign_ToJson(&d);
		CHECK(j && has_sub(j, "\"version\":2"));
		CHECK(j && has_sub(j, "\"views\""));
		CHECK(j && has_sub(j, "\"hosts\""));
		if (j) HeapFree(GetProcessHeap(), 0, j);
	}
	UiDesign_Free(&d);

	/* 同一视图被 Window + Panel 复用 */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	UiDesign_SetText(d.groups[g].symbolPrefix, UI_DN_SYM_MAX, "Main");
	v1i = UiDesign_AddView(&d, "content", "Content");
	UiDesign_View(&d, v1i)->group = g;
	r = UiDesign_AddNode(&d, v1i, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, v1i)->root = r;
	h1 = UiDesign_AddHost(&d, "win1", "Win", UI_DH_WINDOW, g);
	h2 = UiDesign_AddHost(&d, "panel1", "Panel", UI_DH_PANEL, g);
	UiDesign_Host(&d, h1)->view = v1i;
	UiDesign_Host(&d, h2)->view = v1i;
	v2i = UiDesign_AddView(&d, "other", "Other");
	UiDesign_View(&d, v2i)->group = g;
	r = UiDesign_AddNode(&d, v2i, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, v2i)->root = r;
	UiDesign_SetText(d.groups[g].outputC, UI_DN_PATH_MAX, "designer_vh.ui.c");
	UiDesign_SetText(d.groups[g].outputH, UI_DN_PATH_MAX, "designer_vh.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_vh.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_vh.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&d, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "Main_win1_Draw"));
	CHECK(has_sub(buf, "Main_panel1_Draw"));
	CHECK(has_sub(buf, "Main_panel1_Init"));
	/* 视图只构建一次, 两个宿主引用同一视图变量 */
	CHECK(has_sub(buf, "UiDesign_AddView(&gDoc, \"content\", \"Content\")"));
	CHECK(has_sub(buf, "pw0->view=v0;"));
	CHECK(has_sub(buf, "pw1->view=v0;"));
	CHECK(read_all(ph, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "Main_HOST_WIN1"));
	DeleteFileW(pc);
	DeleteFileW(ph);

	/* 删除视图会清除宿主引用并重映射节点 */
	CHECK(UiDesign_ViewRemove(&d, v1i) == 1);
	CHECK(d.viewCount == 1);
	CHECK(UiDesign_Host(&d, h1)->view == -1);
	CHECK(UiDesign_Host(&d, h2)->view == -1);
	CHECK(d.views[0].id[0] && strcmp(d.views[0].id, "other") == 0);
	CHECK(d.views[0].root >= 0 && d.nodes[d.views[0].root].owner == 0);
	UiDesign_Free(&d);
}

static void test_size_mode_normalize(void)
{
	UiDesignDoc d;
	int g, v, root;
	UiDesignNode *n;
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	v = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, v, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, v)->root = root;
	n = UiDesign_Node(&d, UiDesign_AddNode(&d, v, UI_DN_LABEL, root, "a", "A"));
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_GROW;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	n->wMode = UI_DN_SIZE_AUTO;
	n->hMode = UI_DN_SIZE_AUTO;
	n->w = 0.0f;
	n->h = 0.0f;
	UiDesign_NormalizeSizeModes(&d);
	CHECK(UiDesign_Node(&d, root)->wMode == UI_DN_SIZE_FILL);
	CHECK(UiDesign_Node(&d, root)->hMode == UI_DN_SIZE_FILL);
	CHECK(n->wMode == UI_DN_SIZE_FIXED && n->w == UiDesign_DefaultW(UI_DN_LABEL));
	CHECK(n->hMode == UI_DN_SIZE_FIXED && n->h == UiDesign_DefaultH(UI_DN_LABEL));
	UiDesign_Free(&d);
}

void __stdcall startup(void)
{
	char line[160];
	test_json_roundtrip();
	test_layout();
	test_codegen();
	test_preview();
	test_window_generate();
	test_save_load();
	test_symbol();
	test_components();
	test_tabs_pages();
	test_qrcode_node();
	test_ringprogress_node();
	test_grid_columns();
	test_view_host_model();
	test_size_mode_normalize();
	sprintf(line, "designer_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
