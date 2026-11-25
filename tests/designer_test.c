/*
 * designer_test.c - .cui 模型 / JSON / 布局 / 代码生成 headless 断言
 */
#include <windows.h>
#include <string.h>
#include "gfx/gfx.h"
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

static int g_radio_change_events;

static LRESULT CALLBACK test_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static void radio_change_event(void *user, UiDesignInstance *inst,
	const char *nodeId, const char *action, int kind)
{
	(void)user;
	(void)inst;
	if (!nodeId || !action || kind != 2) return;
	if ((strcmp(nodeId, "radio") == 0 && strcmp(action, "on_radio") == 0) ||
	    (strcmp(nodeId, "empty") == 0 && strcmp(action, "on_empty") == 0) ||
	    (strcmp(nodeId, "disabled") == 0 && strcmp(action, "on_disabled") == 0))
		g_radio_change_events++;
}

/* 建一个宿主 + 同名视图, 返回视图下标. */
static int mk_view_host(UiDesignDoc *d, int group, int kind, const char *id)
{
	int v = UiDesign_AddView(d, id, id);
	int h = UiDesign_AddHost(d, id, id, kind, group);
	UiDesign_View(d, v)->group = group;
	UiDesign_Host(d, h)->view = v;
	return v;
}

/* RadioGroup 只是元数据; Radio 均为 parent 的直接子节点 (方案A). */
static int add_radiogroup(UiDesignDoc *d, int view, int parent,
	const char *id, const char *itemPrefix, int count)
{
	char itemId[64], text[64];
	int group = UiDesign_RadioGroupAdd(d, view, id, id);
	int i, child, first = -1;
	if (group < 0) return -1;
	for (i = 0; i < count; i++) {
		sprintf(itemId, "%s%d", itemPrefix, i + 1);
		sprintf(text, "Radio %d", i + 1);
		child = UiDesign_AddNode(d, view, UI_DN_RADIO, parent,
			itemId, text);
		if (child < 0 || !UiDesign_NodeSetText(UiDesign_Node(d, child), text))
			return -1;
		if (!UiDesign_RadioGroupAssign(d, child, group)) return -1;
		if (first < 0) first = child;
	}
	if (first >= 0)
		UiDesign_RadioGroupSetSelectedNode(d, group, first);
	UiDesign_RadioGroupNormalize(d, group);
	return group;
}

static int find_rg(const UiDesignDoc *d, const char *id)
{
	int i;
	for (i = 0; i < d->radioGroupCount; i++)
		if (strcmp(d->radioGroups[i].id, id) == 0) return i;
	return -1;
}

static int rg_member(const UiDesignDoc *d, int group, int ordinal)
{
	return UiDesign_RadioGroupMemberAt(d, group, ordinal);
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
		CHECK(0);
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

static int find_sub(const char *s, const char *needle)
{
	int i, k;
	for (i = 0; s[i]; i++) {
		for (k = 0; needle[k] && s[i + k] == needle[k]; k++) {}
		if (!needle[k]) return i;
	}
	return -1;
}

static int has_sub(const char *s, const char *needle)
{
	return s && find_sub(s, needle) >= 0;
}

static int range_has_sub(const char *s, int begin, int end, const char *needle)
{
	int i, k;
	if (!s || begin < 0 || end < begin) return 0;
	for (i = begin; i < end; i++) {
		for (k = 0; needle[k] && i + k < end &&
		    s[i + k] == needle[k]; k++) {}
		if (!needle[k]) return 1;
	}
	return 0;
}

static int float_near(float a, float b)
{
	float d = a - b;
	if (d < 0.0f) d = -d;
	return d < 0.01f;
}

static int rect_near(UiRect a, UiRect b)
{
	return float_near(a.x, b.x) && float_near(a.y, b.y) &&
		float_near(a.w, b.w) && float_near(a.h, b.h);
}

static void check_radiogroup_geometry(const UiDesignDoc *d, int view,
	const UiRect *rects, int group, UiRect bounds)
{
	int i, count = UiDesign_RadioGroupMemberCount(d, group);
	CHECK(count > 0);
	for (i = 0; i < count; i++) {
		int ci = UiDesign_RadioGroupMemberAt(d, group, i);
		UiRect r;
		float x, y;
		CHECK(ci >= 0 && d->nodes[ci].type == UI_DN_RADIO);
		if (ci < 0) continue;
		r = rects[ci];
		if (!d->nodes[ci].visible || r.w <= 0.0f || r.h <= 0.0f) continue;
		CHECK(r.x >= bounds.x && r.y >= bounds.y &&
			r.x + r.w <= bounds.x + bounds.w + 0.01f &&
			r.y + r.h <= bounds.y + bounds.h + 0.01f);
		x = r.x + r.w * 0.5f;
		y = r.y + r.h * 0.5f;
		CHECK(UiDesign_FindNodeAt(d, view, rects, x, y) == ci);
	}
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
	UiDesign_SetString(&g->outputC, "designer_test.ui.c");
	UiDesign_SetString(&g->outputH, "designer_test.ui.h");
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
	UiDesign_SetString(&d.groups[0].outputC, "designer_prev.ui.c");
	UiDesign_SetString(&d.groups[0].outputH, "designer_prev.ui.h");
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
	UiDesign_GenFilesFree(f, n);
	UiDesign_SetString(&d.hosts[0].title, "Changed Title");
	n = UiDesign_PreviewAll(&d, L"build\\gen", f, 4, report, sizeof(report));
	CHECK(n == 2 && f[0].status == 1);
	CHECK(f[0].added == 1 && f[0].removed == 0);
	UiDesign_GenFilesFree(f, n);
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
	UiDesign_SetString(&d.hosts[0].id, "solo_win");
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
	UiDesign_SetString(&a.hosts[0].id, "saved_win");
	CHECK(UiDesign_SaveFile(&a, L"build\\gen\\designer_test.cui") == 1);
	UiDesign_Init(&b);
	{
		int loaded = UiDesign_LoadFile(&b, L"build\\gen\\designer_test.cui");
		CHECK(loaded == 1);
		if (loaded) {
			CHECK(strcmp(b.hosts[0].id, "saved_win") == 0);
			CHECK(b.views[0].root >= 0);
			CHECK(strcmp(b.groups[0].id, "main") == 0);
		}
	}
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
	int g, w, root, tabs, p1, p2, ta, dp, cs, img, radio, r1, hit;
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
	radio = UiDesign_RadioGroupAdd(&a, w, "radio", "单选组");
	{
		int r2, r3;
		UiDesignRadioGroup *rg;
		r1 = UiDesign_AddNode(&a, w, UI_DN_RADIO, p2, "radio1", "选项1");
		r2 = UiDesign_AddNode(&a, w, UI_DN_RADIO, p2, "radio2", "选项2");
		r3 = UiDesign_AddNode(&a, w, UI_DN_RADIO, p2, "radio3", "选项3");
		CHECK(radio >= 0 && r1 >= 0 && r2 >= 0 && r3 >= 0);
		CHECK(UiDesign_RadioGroupAssign(&a, r1, radio) == 1);
		CHECK(UiDesign_RadioGroupAssign(&a, r2, radio) == 1);
		CHECK(UiDesign_RadioGroupAssign(&a, r3, radio) == 1);
		UiDesign_NodeSetText(UiDesign_Node(&a, r1), "选项1");
		UiDesign_Node(&a, r1)->wMode = UI_DN_SIZE_FIXED;
		UiDesign_Node(&a, r1)->w = 140.0f;
		UiDesign_Node(&a, r1)->h = 28.0f;
		UiDesign_Node(&a, r1)->margin[1] = 3.0f;
		UiDesign_Node(&a, r2)->wMode = UI_DN_SIZE_FIXED;
		UiDesign_Node(&a, r2)->w = 140.0f;
		UiDesign_Node(&a, r2)->h = 24.0f;
		UiDesign_Node(&a, r2)->x = 0.0f;
		UiDesign_Node(&a, r2)->y = 40.0f;
		UiDesign_Node(&a, r3)->wMode = UI_DN_SIZE_FIXED;
		UiDesign_Node(&a, r3)->w = 140.0f;
		UiDesign_Node(&a, r3)->h = 24.0f;
		UiDesign_Node(&a, r3)->x = 0.0f;
		UiDesign_Node(&a, r3)->y = 80.0f;
		rg = UiDesign_RadioGroup(&a, radio);
		CHECK(rg != NULL);
		UiDesign_SetString(&rg->selectedRadioId, "radio2");
		UiDesign_SetString(&rg->onChange, "on_radio");
		rg->enabled = 0;
	}

	CHECK(UiDesign_TypeFromName("tabs") == UI_DN_TABS);
	CHECK(UiDesign_TypeFromName("textarea") == UI_DN_TEXTAREA);
	CHECK(UiDesign_TypeFromName("datepicker") == UI_DN_DATEPICKER);
	CHECK(UiDesign_TypeFromName("colorsel") == UI_DN_COLORSEL);
	CHECK(UiDesign_TypeFromName("radiogroup") == -1);
	CHECK(UiDesign_TypeFromName("radio") == UI_DN_RADIO);
	CHECK(UiDesign_TypeFromName(UiDesign_TypeName(UI_DN_COLORSEL)) == UI_DN_COLORSEL);
	CHECK(UiDesign_TypeFromName(UiDesign_TypeName(UI_DN_RADIO)) == UI_DN_RADIO);
	CHECK(!UiDesign_IsContainer(UI_DN_RADIO));
	CHECK(UiDesign_DefaultW(UI_DN_RADIO) == 120.0f);
	CHECK(UiDesign_DefaultH(UI_DN_RADIO) == 24.0f);

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
	CHECK(hit == r1);
	HeapFree(GetProcessHeap(), 0, rects);
	json = UiDesign_ToJson(&a);
	CHECK(json != NULL);
	if (json) CHECK(has_sub(json, "\"mode\":3"));
	CHECK(json && has_sub(json, "\"color\":\"#80336699\""));
	CHECK(json && has_sub(json, "\"readonly\":1"));
	CHECK(json && has_sub(json, "\"type\":\"tabs\""));
	CHECK(json && has_sub(json, "\"radioGroups\":["));
	CHECK(json && has_sub(json, "\"selectedRadioId\":\"radio2\""));
	CHECK(json && has_sub(json, "\"onChange\":\"on_radio\""));
	CHECK(json && has_sub(json, "\"radioGroupUid\":"));
	CHECK(json && !has_sub(json, "\"type\":\"radiogroup\""));
	CHECK(json && has_sub(json, "\"radius\":4.000"));
	CHECK(json && has_sub(json, "\"radius\":10.000"));
	UiDesign_Init(&b);
	if (json && UiDesign_FromJson(&b, json)) {
		int bt = UiDesign_FindNode(&b, 0, "tabs");
		int bta = UiDesign_FindNode(&b, 0, "ta");
		int bdp = UiDesign_FindNode(&b, 0, "dp");
		int bcs = UiDesign_FindNode(&b, 0, "cs");
		int br1 = UiDesign_FindNode(&b, 0, "radio1");
		int bg = find_rg(&b, "radio");
		CHECK(bt >= 0 && UiDesign_Node(&b, bt)->type == UI_DN_TABS);
		CHECK(bt >= 0 && UiDesign_Node(&b, bt)->selected == 1);
		CHECK(bta >= 0 && UiDesign_Node(&b, bta)->readonly == 1);
		CHECK(bdp >= 0 && UiDesign_Node(&b, bdp)->mode == 3);
		CHECK(bdp >= 0 && strcmp(UiDesign_Node(&b, bdp)->options, "2026-02-03 04:05") == 0);
		CHECK(bcs >= 0 && UiDesign_Node(&b, bcs)->color == 0x80336699u);
		CHECK(bg >= 0 && bg < b.radioGroupCount);
		CHECK(bg >= 0 && strcmp(b.radioGroups[bg].selectedRadioId, "radio2") == 0);
		CHECK(bg >= 0 && !b.radioGroups[bg].enabled);
		CHECK(bg >= 0 && strcmp(b.radioGroups[bg].onChange, "on_radio") == 0);
		CHECK(bg >= 0 && UiDesign_RadioGroupMemberCount(&b, bg) == 3);
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->type == UI_DN_RADIO);
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->parent ==
			UiDesign_FindNode(&b, 0, "page2"));
		CHECK(br1 >= 0 && strcmp(UiDesign_Node(&b, br1)->text, "选项1") == 0);
		CHECK(br1 >= 0 && UiDesign_Node(&b, br1)->w == 140.0f &&
			UiDesign_Node(&b, br1)->h == 28.0f);
		CHECK(br1 >= 0 && UiDesign_RadioGroupForNode(&b, br1) == bg);
	} else {
		CHECK(0);
	}
	if (json) HeapFree(GetProcessHeap(), 0, json);

	/* 代码生成包含新字段 */
	UiDesign_SetString(&a.groups[g].outputC, "designer_comp.ui.c");
	UiDesign_SetString(&a.groups[g].outputH, "designer_comp.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_comp.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_comp.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&a, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, ", 15, "));
	CHECK(has_sub(buf, ", 21, "));
	CHECK(has_sub(buf, "->mode=3"));
	CHECK(has_sub(buf, "->readonly=1"));
	CHECK(has_sub(buf, "->color=0x80336699u"));
	CHECK(has_sub(buf, "->selected=1;"));
	CHECK(has_sub(buf, "p2->radius=4.000f;"));
	CHECK(has_sub(buf, "p7->radius=10.000f;"));
	CHECK(has_sub(buf, ", 21, n3, \"radio1\""));
	CHECK(has_sub(buf, "UiDesign_NodeSetText(p8, \"选项1\");"));
	CHECK(has_sub(buf, "p8->wMode=0;"));
	CHECK(has_sub(buf, "p8->w=140.000f;"));
	CHECK(has_sub(buf, "p8->h=28.000f;"));
	CHECK(has_sub(buf, "p8->margin[1]=3.000f;"));
	CHECK(has_sub(buf, "UiDesign_RadioGroupAdd(&gDoc, v0, \"radio\", \"单选组\")"));
	CHECK(has_sub(buf, "UiDesignRadioGroup *prg0 = UiDesign_RadioGroup(&gDoc, rg0);"));
	CHECK(has_sub(buf, "UiDesign_SetString(&prg0->selectedRadioId, \"radio2\");"));
	CHECK(has_sub(buf, "UiDesign_SetString(&prg0->onChange, \"on_radio\");"));
	CHECK(has_sub(buf, "prg0->enabled=0;"));
	CHECK(has_sub(buf, "p8->radioGroupUid="));
	CHECK(has_sub(buf, "p9->radioGroupUid="));
	CHECK(has_sub(buf, "p10->radioGroupUid="));
	CHECK(!has_sub(buf, "selectedOptionId"));
	CHECK(!has_sub(buf, "UI_DN_RADIOGROUP"));
	CHECK(!has_sub(buf, "UiDesign_NodeSetOptions(p8,"));
	CHECK(!has_sub(buf, "UiDesign_NodeSetText(p8, \"ignored\");"));
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

static void test_radiogroup_metadata(void)
{
	UiDesignDoc d, b;
	UiDesignRadioGroup *rg;
	char *json, *v2json;
	char report[128];
	static char buf[32768];
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	int g, w, root, gi, gi2, r1, r2, r3, r4, r5, ri[5];
	int inserted, deleted, i, versionAt, bg, solo, other, v2;
	char selected[64], soloId[64];

	CHECK(UI_DN_TABS == 15);
	CHECK(UI_DN_RINGPROGRESS == 20);
	CHECK(UI_DN_RADIO == 21);
	CHECK(UI_DN_COUNT == 22);
	CHECK(!UiDesign_IsContainer(UI_DN_RADIO));
	CHECK(!UiDesign_IsLayoutContainer(UI_DN_RADIO));
	CHECK(UiDesign_DefaultW(UI_DN_RADIO) == 120.0f);
	CHECK(UiDesign_DefaultH(UI_DN_RADIO) == 24.0f);
	CHECK(UiDesign_CanParent(UI_DN_COLUMN, UI_DN_RADIO));
	CHECK(UiDesign_CanParent(UI_DN_ABSOLUTE, UI_DN_RADIO));
	CHECK(UiDesign_CanParent(UI_DN_ROW, UI_DN_RADIO));
	CHECK(UiDesign_CanParent(UI_DN_GRID, UI_DN_RADIO));
	CHECK(UiDesign_CanParent(UI_DN_PANEL, UI_DN_RADIO));
	CHECK(!UiDesign_CanParent(UI_DN_LABEL, UI_DN_RADIO));
	CHECK(!UiDesign_CanParent(UI_DN_RADIO, UI_DN_RADIO));

	UiDesign_Init(&d);
	CHECK(UiDesign_AddNode(&d, 0, UI_DN_RADIO, -1, "bad", "R") == -1);
	CHECK(UiDesign_RadioGroupAdd(&d, 0, "rg", "RG") == -1);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;

	gi = UiDesign_RadioGroupAdd(&d, w, "radio", "Radio");
	CHECK(gi == 0);
	CHECK(UiDesign_RadioGroupAdd(&d, w, "radio", "Dup") == -1);
	rg = UiDesign_RadioGroup(&d, gi);
	CHECK(rg != NULL && rg->uid != 0 && rg->owner == w);
	CHECK(rg != NULL && rg->enabled == 1);
	CHECK(rg != NULL && rg->selectedRadioId[0] == 0 && rg->onChange[0] == 0);
	CHECK(UiDesign_RadioGroupFind(&d, rg->uid) == gi);

	r1 = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, "item1", "Radio 1");
	r2 = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, "item2", "Radio 2");
	r3 = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, "item3", "Radio 3");
	r4 = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, "item4", "Radio 4");
	r5 = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, "item5", "Radio 5");
	ri[0] = r1; ri[1] = r2; ri[2] = r3; ri[3] = r4; ri[4] = r5;
	for (i = 0; i < 5; i++) {
		CHECK(ri[i] >= 0);
		CHECK(UiDesign_RadioGroupForNode(&d, ri[i]) == -1);
		CHECK(UiDesign_RadioGroupAssign(&d, ri[i], gi) == 1);
	}
	CHECK(UiDesign_RadioGroupMemberCount(&d, gi) == 5);
	for (i = 0; i < 5; i++)
		CHECK(UiDesign_RadioGroupMemberAt(&d, gi, i) == ri[i]);
	CHECK(UiDesign_RadioGroupSelectedNode(&d, gi) == r1);
	CHECK(strcmp(UiDesign_RadioGroup(&d, gi)->selectedRadioId, "item1") == 0);

	CHECK(UiDesign_RadioGroupSetSelectedNode(&d, gi, r3) == 1);
	CHECK(UiDesign_RadioGroupSelectedNode(&d, gi) == r3);
	memcpy(selected, UiDesign_Node(&d, r3)->id, strlen(UiDesign_Node(&d, r3)->id) + 1);

	/* 跨视图选项不能加入本组. */
	v2 = UiDesign_AddView(&d, "view2", "V2");
	other = UiDesign_AddNode(&d, v2, UI_DN_COLUMN, -1, "other-root", "O");
	UiDesign_View(&d, v2)->root = other;
	{
		int orr = UiDesign_AddNode(&d, v2, UI_DN_RADIO, other, "other", "Other");
		CHECK(orr >= 0);
		CHECK(UiDesign_RadioGroupAssign(&d, orr, gi) == 0);
		CHECK(UiDesign_RadioGroupSetSelectedNode(&d, gi, orr) == 0);
	}

	/* 插入/删除/重排都不改变稳定选中 ID. */
	inserted = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, "inserted", "Inserted");
	CHECK(inserted >= 0);
	CHECK(UiDesign_RadioGroupAssign(&d, inserted, gi) == 1);
	CHECK(UiDesign_NodeMove(&d, inserted, root, 0) == 1);
	CHECK(strcmp(UiDesign_RadioGroup(&d, gi)->selectedRadioId, selected) == 0);
	CHECK(UiDesign_RadioGroupSelectedNode(&d, gi) == r3);
	deleted = UiDesign_FindNode(&d, w, "item2");
	CHECK(deleted >= 0 && UiDesign_NodeRemove(&d, deleted) == 1);
	CHECK(strcmp(UiDesign_RadioGroup(&d, gi)->selectedRadioId, selected) == 0);
	CHECK(UiDesign_NodeMove(&d, r3, root, 0) == 1);
	CHECK(strcmp(UiDesign_RadioGroup(&d, gi)->selectedRadioId, selected) == 0);
	CHECK(UiDesign_NodeMoveDown(&d, r3) == 1);
	CHECK(strcmp(UiDesign_RadioGroup(&d, gi)->selectedRadioId, selected) == 0);

	/* 删除选中项后自动回退到第一个可见成员. */
	CHECK(UiDesign_NodeRemove(&d, r3) == 1);
	CHECK(UiDesign_RadioGroupSelectedNode(&d, gi) >= 0);
	CHECK(strcmp(UiDesign_RadioGroup(&d, gi)->selectedRadioId, "inserted") == 0);
	CHECK(UiDesign_RadioGroupNormalize(&d, gi) == 1);
	CHECK(UiDesign_Node(&d,
		UiDesign_RadioGroupSelectedNode(&d, gi))->visible);

	/* 取消分组; 删空最后一员时元数据自动移除. */
	CHECK(UiDesign_RadioGroupAssign(&d, r1, -1) == 1);
	CHECK(UiDesign_RadioGroupForNode(&d, r1) == -1);
	CHECK(UiDesign_RadioGroupMemberCount(&d, gi) == 3);
	gi2 = UiDesign_RadioGroupAdd(&d, w, "solo", "Solo");
	CHECK(gi2 > gi);
	solo = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, "solo1", "Solo 1");
	CHECK(solo >= 0 && UiDesign_RadioGroupAssign(&d, solo, gi2) == 1);
	memcpy(soloId, UiDesign_Node(&d, solo)->id, strlen(UiDesign_Node(&d, solo)->id) + 1);
	CHECK(UiDesign_RadioGroupMemberCount(&d, gi2) == 1);
	CHECK(UiDesign_RadioGroupSetSelectedNode(&d, gi2, solo) == 1);
	CHECK(strcmp(UiDesign_RadioGroup(&d, gi2)->selectedRadioId, soloId) == 0);
	CHECK(UiDesign_RadioGroupAssign(&d, solo, -1) == 1);
	CHECK(find_rg(&d, "solo") < 0);

	rg = UiDesign_RadioGroup(&d, gi);
	CHECK(rg != NULL);
	UiDesign_SetString(&rg->onChange, "on_radio");
	rg->enabled = 0;
	rg = UiDesign_RadioGroup(&d, gi);
	CHECK(strcmp(rg->selectedRadioId, "inserted") == 0 ||
		strcmp(rg->selectedRadioId, "item4") == 0 ||
		strcmp(rg->selectedRadioId, "item5") == 0);

	json = UiDesign_ToJson(&d);
	CHECK(json != NULL);
	CHECK(json && has_sub(json, "\"version\":4"));
	CHECK(json && has_sub(json, "\"radioGroups\":["));
	CHECK(json && has_sub(json, "\"selectedRadioId\":"));
	CHECK(json && has_sub(json, "\"radioGroupUid\":"));
	CHECK(json && !has_sub(json, "\"type\":\"radiogroup\""));
	CHECK(json && has_sub(json, "\"onChange\":\"on_radio\""));

	UiDesign_Init(&b);
	CHECK(json && UiDesign_FromJson(&b, json) == 1);
	bg = find_rg(&b, "radio");
	CHECK(bg >= 0 && bg < b.radioGroupCount);
	CHECK(bg >= 0 && b.radioGroups[bg].uid == rg->uid);
	CHECK(bg >= 0 && b.radioGroups[bg].owner == w);
	CHECK(bg >= 0 && strcmp(b.radioGroups[bg].name, "Radio") == 0);
	CHECK(bg >= 0 && strcmp(b.radioGroups[bg].selectedRadioId,
		rg->selectedRadioId) == 0);
	CHECK(bg >= 0 && !b.radioGroups[bg].enabled);
	CHECK(bg >= 0 && strcmp(b.radioGroups[bg].onChange, "on_radio") == 0);
	CHECK(bg >= 0 && UiDesign_RadioGroupMemberCount(&b, bg) == 3);
	{
		int bi = UiDesign_FindNode(&b, w, "item4");
		CHECK(bi >= 0);
		CHECK(UiDesign_Node(&b, bi)->radioGroupUid == b.radioGroups[bg].uid);
	}

	/* 只接受 v4: 改成 3 后必须拒绝. */
	v2json = json ? (char *)HeapAlloc(GetProcessHeap(), 0, strlen(json) + 1) : NULL;
	CHECK(!json || v2json != NULL);
	if (v2json) {
		memcpy(v2json, json, strlen(json) + 1);
		versionAt = find_sub(v2json, "\"version\":4");
		CHECK(versionAt >= 0);
		if (versionAt >= 0) {
			versionAt += (int)strlen("\"version\":");
			v2json[versionAt] = '3';
			CHECK(UiDesign_FromJson(&b, v2json) == 0);
		}
		HeapFree(GetProcessHeap(), 0, v2json);
	}

	/* 代码生成: 组元数据独立, Radio 直接挂在真实布局父容器. */
	UiDesign_SetString(&d.groups[g].outputC, "designer_radio.ui.c");
	UiDesign_SetString(&d.groups[g].outputH, "designer_radio.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_radio.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_radio.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&d, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "UiDesign_RadioGroupAdd(&gDoc, v0, \"radio\", \"Radio\")"));
	CHECK(has_sub(buf, "UiDesignRadioGroup *prg0 = UiDesign_RadioGroup(&gDoc, rg0);"));
	CHECK(has_sub(buf, "UiDesign_SetString(&prg0->onChange, \"on_radio\");"));
	CHECK(has_sub(buf, "prg0->enabled=0;"));
	CHECK(has_sub(buf, ", 21, n0, \"item1\""));
	CHECK(has_sub(buf, "->radioGroupUid="));
	CHECK(!has_sub(buf, "selectedOptionId"));
	CHECK(!has_sub(buf, "UI_DN_RADIOGROUP"));
	DeleteFileW(pc);
	DeleteFileW(ph);

	if (json) HeapFree(GetProcessHeap(), 0, json);
	UiDesign_Free(&b);
	UiDesign_Free(&d);
}
static void test_radiogroup_layout(void)
{
	UiDesignDoc d;
	UiDesignNode *n;
	UiRect *rects;
	int g, w, root, label, group, after, page, tabs, i, ci;

	/* Column: Radio 是普通子项, 与 Label/Button 一起参与线性布局. */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	n = UiDesign_Node(&d, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->gap = 8.0f;
	label = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "label", "Label");
	UiDesign_Node(&d, label)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, label)->h = 20.0f;
	group = add_radiogroup(&d, w, root, "radio", "item", 5);
	CHECK(group >= 0);
	after = UiDesign_AddNode(&d, w, UI_DN_BUTTON, root, "after", "After");
	UiDesign_Node(&d, after)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, after)->h = 32.0f;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(rects != NULL);
	if (rects) {
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 400, 300), rects,
			d.nodeCount) == 1);
		CHECK(rects[label].y == 0.0f && rects[label].h == 20.0f);
		for (i = 0; i < 5; i++) {
			ci = rg_member(&d, group, i);
			CHECK(rects[ci].y == 28.0f + (float)i * 32.0f);
			CHECK(rects[ci].w == 400.0f && rects[ci].h == 24.0f);
		}
		CHECK(rects[after].y == 188.0f && rects[after].h == 32.0f);
		check_radiogroup_geometry(&d, w, rects, group,
			Ui_Rect(0, 0, 400, 300));
		CHECK(UiDesign_FindNodeAt(&d, w, rects, 20.0f, 40.0f) ==
			rg_member(&d, group, 0));
		/* 隐藏 Radio 不占槽, 后续 Radio 前移. */
		ci = rg_member(&d, group, 2);
		UiDesign_Node(&d, ci)->visible = 0;
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 400, 300), rects,
			d.nodeCount) == 1);
		CHECK(rects[ci].w == 0.0f && rects[ci].h == 0.0f);
		CHECK(rects[rg_member(&d, group, 3)].y == 92.0f);
		CHECK(rects[rg_member(&d, group, 4)].y == 124.0f);
		CHECK(rects[after].y == 156.0f);
		CHECK(UiDesign_FindNodeAt(&d, w, rects, 20.0f, 104.0f) ==
			rg_member(&d, group, 3));
		check_radiogroup_geometry(&d, w, rects, group,
			Ui_Rect(0, 0, 400, 300));
		HeapFree(GetProcessHeap(), 0, rects);
	}
	UiDesign_Free(&d);

	/* Row: 5 个 Radio 与 Label/Button 一起线性排列. */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_ROW, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	n = UiDesign_Node(&d, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->gap = 10.0f;
	label = UiDesign_AddNode(&d, w, UI_DN_LABEL, root, "label", "Label");
	n = UiDesign_Node(&d, label);
	n->wMode = UI_DN_SIZE_FIXED;
	n->hMode = UI_DN_SIZE_FIXED;
	n->w = 50.0f;
	n->h = 20.0f;
	group = add_radiogroup(&d, w, root, "radio", "item", 5);
	CHECK(group >= 0);
	for (i = 0; i < 5; i++) {
		n = UiDesign_Node(&d, rg_member(&d, group, i));
		n->wMode = UI_DN_SIZE_FIXED;
		n->w = 80.0f;
		n->h = 24.0f;
	}
	after = UiDesign_AddNode(&d, w, UI_DN_BUTTON, root, "after", "After");
	n = UiDesign_Node(&d, after);
	n->wMode = UI_DN_SIZE_FIXED;
	n->hMode = UI_DN_SIZE_FIXED;
	n->w = 70.0f;
	n->h = 24.0f;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(rects != NULL);
	if (rects) {
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 700, 100), rects,
			d.nodeCount) == 1);
		for (i = 0; i < 5; i++) {
			ci = rg_member(&d, group, i);
			CHECK(rects[ci].x == 60.0f + (float)i * 90.0f);
			CHECK(rects[ci].y == 0.0f && rects[ci].h == 24.0f);
		}
		CHECK(rects[after].x == 510.0f);
		check_radiogroup_geometry(&d, w, rects, group,
			Ui_Rect(0, 0, 700, 100));
		HeapFree(GetProcessHeap(), 0, rects);
	}
	UiDesign_Free(&d);

	/* Row: Radio 的 margin/END/GROW 针对真实父容器生效. */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_ROW, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	n = UiDesign_Node(&d, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->gap = 10.0f;
	group = add_radiogroup(&d, w, root, "radio", "item", 2);
	CHECK(group >= 0);
	n = UiDesign_Node(&d, rg_member(&d, group, 0));
	n->wMode = UI_DN_SIZE_FIXED;
	n->hMode = UI_DN_SIZE_FIXED;
	n->w = 80.0f;
	n->h = 24.0f;
	n->margin[0] = 5.0f;
	n->margin[2] = 7.0f;
	n->margin[1] = 6.0f;
	n->margin[3] = 4.0f;
	n->align = UI_DN_ALIGN_END;
	n = UiDesign_Node(&d, rg_member(&d, group, 1));
	n->wMode = UI_DN_SIZE_GROW;
	n->hMode = UI_DN_SIZE_FIXED;
	n->h = 24.0f;
	n->align = UI_DN_ALIGN_END;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(rects != NULL);
	if (rects) {
		int r0 = rg_member(&d, group, 0);
		int r1 = rg_member(&d, group, 1);
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 700, 100), rects,
			d.nodeCount) == 1);
		CHECK(rects[r0].x == 5.0f && rects[r0].w == 80.0f);
		CHECK(rects[r0].y == 72.0f && rects[r0].h == 24.0f);
		CHECK(rects[r1].x == 102.0f && rects[r1].w == 598.0f);
		CHECK(rects[r1].y == 76.0f && rects[r1].h == 24.0f);
		check_radiogroup_geometry(&d, w, rects, group,
			Ui_Rect(0, 0, 700, 100));
		HeapFree(GetProcessHeap(), 0, rects);
	}
	UiDesign_Free(&d);

	/* Grid: 自动放置. */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_GRID, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	n = UiDesign_Node(&d, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->gridCols = 3;
	n->gridRows = 0;
	n->gridColGap = 10.0f;
	n->gridRowGap = 10.0f;
	UiDesign_SetString(&n->gridColTracks, "auto,1fr,auto");
	group = add_radiogroup(&d, w, root, "radio", "item", 5);
	CHECK(group >= 0);
	for (i = 0; i < 5; i++) {
		n = UiDesign_Node(&d, rg_member(&d, group, i));
		n->wMode = UI_DN_SIZE_FIXED;
		n->w = 80.0f;
		n->h = 24.0f;
	}
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(rects != NULL);
	if (rects) {
		int r0 = rg_member(&d, group, 0);
		int r1 = rg_member(&d, group, 1);
		int r2 = rg_member(&d, group, 2);
		int r3 = rg_member(&d, group, 3);
		int r4 = rg_member(&d, group, 4);
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 500, 300), rects,
			d.nodeCount) == 1);
		CHECK(rects[r0].x < rects[r1].x && rects[r1].x < rects[r2].x);
		CHECK(float_near(rects[r0].y, rects[r1].y) &&
			float_near(rects[r1].y, rects[r2].y));
		CHECK(rects[r3].y > rects[r0].y);
		CHECK(float_near(rects[r3].x, rects[r0].x));
		CHECK(float_near(rects[r4].x, rects[r1].x));
		CHECK(float_near(rects[r3].y, rects[r4].y));
		check_radiogroup_geometry(&d, w, rects, group,
			Ui_Rect(0, 0, 500, 300));
		HeapFree(GetProcessHeap(), 0, rects);
	}
	UiDesign_Free(&d);

	/* Absolute: Radio 使用自己的坐标/锚点. */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_ABSOLUTE, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	group = add_radiogroup(&d, w, root, "radio", "item", 5);
	CHECK(group >= 0);
	for (i = 0; i < 5; i++) {
		n = UiDesign_Node(&d, rg_member(&d, group, i));
		n->wMode = UI_DN_SIZE_FIXED;
		n->hMode = UI_DN_SIZE_FIXED;
		n->w = 70.0f;
		n->h = 24.0f;
		n->x = 20.0f + (float)i * 90.0f;
		n->y = 30.0f + (float)(i % 2) * 40.0f;
	}
	n = UiDesign_Node(&d, rg_member(&d, group, 0));
	n->anchors = UI_DN_ANCHOR_RIGHT | UI_DN_ANCHOR_BOTTOM;
	n->margin[2] = 10.0f;
	n->margin[3] = 5.0f;
	n->x = n->y = 999.0f;
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(rects != NULL);
	if (rects) {
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 500, 200), rects,
			d.nodeCount) == 1);
		for (i = 0; i < 5; i++) {
			ci = rg_member(&d, group, i);
			if (i == 0) {
				CHECK(rects[ci].x == 420.0f && rects[ci].y == 171.0f);
			} else {
				CHECK(rects[ci].x == 20.0f + (float)i * 90.0f);
				CHECK(rects[ci].y == 30.0f + (float)(i % 2) * 40.0f);
			}
			CHECK(rects[ci].w == 70.0f && rects[ci].h == 24.0f);
		}
		check_radiogroup_geometry(&d, w, rects, group,
			Ui_Rect(0, 0, 500, 200));
		HeapFree(GetProcessHeap(), 0, rects);
	}
	UiDesign_Free(&d);

	/* Tabs 页内: 页是合法布局父容器. */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	tabs = UiDesign_AddNode(&d, w, UI_DN_TABS, root, "tabs", "Tabs");
	n = UiDesign_Node(&d, tabs);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FIXED;
	n->h = 260.0f;
	n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 10.0f;
	page = UiDesign_TabsInsertPage(&d, tabs, 0, "page", "Page");
	CHECK(page >= 0);
	UiDesign_Node(&d, page)->gap = 6.0f;
	group = add_radiogroup(&d, w, page, "radio", "item", 5);
	CHECK(group >= 0);
	for (i = 0; i < 5; i++) {
		n = UiDesign_Node(&d, rg_member(&d, group, i));
		n->wMode = UI_DN_SIZE_FIXED;
		n->w = 120.0f;
		n->h = 24.0f;
	}
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(UiRect));
	CHECK(rects != NULL);
	if (rects) {
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 400, 300), rects,
			d.nodeCount) == 1);
		check_radiogroup_geometry(&d, w, rects, group, rects[page]);
		HeapFree(GetProcessHeap(), 0, rects);
	}
	UiDesign_Free(&d);
}
/* 元数据组不受固定槽位限制: 长 ID/长文本经 JSON 原样往返. */
static void test_radiogroup_capacities(void)
{
	UiDesignDoc d, b;
	UiDesignNode *n;
	char *json;
	char id[64], text[256];
	int g, w, root, group, i, ci, len, gb, rb;

	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	group = UiDesign_RadioGroupAdd(&d, w,
		"capacity_group_with_a_long_identifier", "容量组");
	CHECK(group >= 0);
	for (i = 0; i < 24; i++) {
		len = sprintf(id, "radio_item_%02d", i);
		CHECK(len > 0);
		len = sprintf(text, "Radio option %02d ", i);
		while (len < 180) text[len++] = 'x';
		text[len] = 0;
		ci = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, id, text);
		CHECK(ci >= 0);
		if (ci < 0) continue;
		CHECK(UiDesign_NodeSetText(UiDesign_Node(&d, ci), text));
		CHECK(UiDesign_RadioGroupAssign(&d, ci, group));
	}
	CHECK(UiDesign_RadioGroupMemberCount(&d, group) == 24);
	ci = UiDesign_RadioGroupMemberAt(&d, group, 17);
	CHECK(ci >= 0);
	CHECK(UiDesign_RadioGroupSetSelectedNode(&d, group, ci));
	UiDesign_RadioGroupNormalize(&d, group);
	CHECK(UiDesign_RadioGroupSelectedNode(&d, group) >= 0);

	json = UiDesign_ToJson(&d);
	CHECK(json != NULL);
	CHECK(json && has_sub(json, "\"capacity_group_with_a_long_identifier\""));
	CHECK(json && has_sub(json, "\"selectedRadioId\":\"radio_item_17\""));
	CHECK(json && has_sub(json, "\"radioGroupUid\":"));
	UiDesign_Init(&b);
	CHECK(json && UiDesign_FromJson(&b, json));
	gb = find_rg(&b, "capacity_group_with_a_long_identifier");
	CHECK(gb >= 0);
	if (gb >= 0) {
		CHECK(UiDesign_RadioGroupMemberCount(&b, gb) == 24);
		CHECK(strcmp(b.radioGroups[gb].selectedRadioId,
			"radio_item_17") == 0);
		rb = UiDesign_FindNode(&b, 0, "radio_item_23");
		CHECK(rb >= 0);
		if (rb >= 0) {
			n = UiDesign_Node(&b, rb);
			CHECK(n->type == UI_DN_RADIO);
			CHECK(n->radioGroupUid == b.radioGroups[gb].uid);
			CHECK((int)strlen(n->text) == 180);
			CHECK(strncmp(n->text, "Radio option 23 ", 16) == 0);
			CHECK(n->parent == UiDesign_FindNode(&b, 0, "root"));
		}
		for (i = 0; i < 24; i++) {
			int m = UiDesign_RadioGroupMemberAt(&b, gb, i);
			CHECK(m >= 0);
			if (m >= 0) {
				sprintf(id, "radio_item_%02d", i);
				CHECK(strcmp(b.nodes[m].id, id) == 0);
			}
		}
	}
	HeapFree(GetProcessHeap(), 0, json);
	UiDesign_Free(&d);
	UiDesign_Free(&b);
}
static void radio_draw_frame(Gfx *gfx, UiDesignInstance *inst, UiRect rc)
{
	Ui_FxFlush(inst->ui);
	Gfx_Begin(gfx);
	Gfx_Clear(gfx, 0xFF101010u);
	Ui_BeginFrame(inst->ui);
	UiDesign_Draw(inst, rc);
	Ui_EndFrame(inst->ui);
	CHECK(Gfx_End(gfx) == 1);
}

static void radio_click_at(UiDesignInstance *inst, UiId id, float x, float y)
{
	Ui_OnMouseMove(inst->ui, x, y);
	UiDesign_MouseDown(inst, x, y);
	Ui_OnMouseDown(inst->ui, x, y);
	UiDesign_MouseUp(inst);
	Ui_OnMouseUp(inst->ui, x, y);
	CHECK(inst->ui->clicked == id);
}

static UiTheme g_rg_theme;
static UiContext g_rg_ctx;

static int rg_nav_targets(void)
{
	int i, n = 0;
	for (i = 0; i < g_rg_ctx.widgetCount; i++)
		if (g_rg_ctx.widgets[i].navOnly) n++;
	return n;
}

static void test_radiogroup_runtime(void)
{
	UiDesignDoc d;
	UiDesignInstance inst;
	WNDCLASSEXW wc;
	HWND hwnd = NULL;
	Gfx *g = NULL;
	UiRect rc = { 0.0f, 0.0f, 600.0f, 400.0f };
	int grp, w, root, group, empty, r[3], i, ready = 0;
	float x, y;
	UiId id;
	UiDesignRadioGroup *rg;

	SetEnvironmentVariableA("SCV2_ULW", "1");
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	UiDesign_Init(&d);
	grp = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, grp, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_ABSOLUTE, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	UiDesign_Node(&d, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root)->hMode = UI_DN_SIZE_FILL;
	group = UiDesign_RadioGroupAdd(&d, w, "radio", "单选组");
	empty = UiDesign_RadioGroupAdd(&d, w, "empty", "空组");
	CHECK(group >= 0 && empty >= 0);
	rg = &d.radioGroups[group];
	UiDesign_SetString(&rg->onChange, "on_radio");
	rg->enabled = 1;
	rg = &d.radioGroups[empty];
	UiDesign_SetString(&rg->onChange, "on_empty");
	rg->enabled = 1;
	for (i = 0; i < 3; i++) {
		char nid[16];
		sprintf(nid, "radio%d", i + 1);
		r[i] = UiDesign_AddNode(&d, w, UI_DN_RADIO, root, nid, nid);
		CHECK(r[i] >= 0);
		if (r[i] < 0) continue;
		UiDesign_Node(&d, r[i])->wMode = UI_DN_SIZE_FIXED;
		UiDesign_Node(&d, r[i])->hMode = UI_DN_SIZE_FIXED;
		UiDesign_Node(&d, r[i])->w = 160.0f;
		UiDesign_Node(&d, r[i])->h = 24.0f;
		UiDesign_Node(&d, r[i])->x = 20.0f;
		UiDesign_Node(&d, r[i])->y = 20.0f + 40.0f * (float)i;
		CHECK(UiDesign_RadioGroupAssign(&d, r[i], group));
	}
	CHECK(UiDesign_RadioGroupSetSelectedNode(&d, group, r[0]));
	UiDesign_RadioGroupNormalize(&d, group);

	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = test_wnd_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"cui_designer_rg_test";
	RegisterClassExW(&wc);
	hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
		L"cui_designer_rg_test", L"", WS_POPUP, 0, 0, 600, 400,
		NULL, NULL, wc.hInstance, NULL);
	CHECK(hwnd != NULL);
	if (hwnd) {
		CHECK(Gfx_Init(&g, hwnd) == 1);
		if (g) {
			CHECK(Gfx_Resize(g, 600, 400, 1.0f) == 1);
			UiTheme_Init(&g_rg_theme, g);
			Ui_ContextInit(&g_rg_ctx, hwnd, g, &g_rg_theme);
			Ui_SetViewport(&g_rg_ctx, 600.0f, 400.0f, 1.0f);
			CHECK(UiDesignInstance_Init(&inst, &d, 0, &g_rg_ctx,
				NULL, NULL));
			ready = inst.rects != NULL && inst.ui == &g_rg_ctx;
		}
	}
	if (ready) {
		UiDesignInstance_SetEventFn(&inst, radio_change_event, NULL);
		radio_draw_frame(g, &inst, rc);
		CHECK(inst.radioStateCount == d.radioGroupCount);
		CHECK(inst.radioStates[group].radio.count == 3);
		CHECK(inst.radioStates[empty].radio.count == 0);
		CHECK(inst.states[r[0]].radioSlot == 0);
		CHECK(inst.states[r[1]].radioSlot == 1);
		CHECK(inst.states[r[2]].radioSlot == 2);
		/* 一个组只有一个 Tab 停靠点; 空组没有 Radio 就不注册. */
		CHECK(rg_nav_targets() == 1);
		CHECK(g_rg_ctx.navId == 0);
		CHECK(g_radio_change_events == 0);

		/* 点击 radio2: 选中改变, 组级事件 (lastEventNode = -1). */
		x = inst.rects[r[1]].x + inst.rects[r[1]].w * 0.5f;
		y = inst.rects[r[1]].y + inst.rects[r[1]].h * 0.5f;
		id = Ui_HitTest(&g_rg_ctx, x, y);
		CHECK(id != 0);
		radio_click_at(&inst, id, x, y);
		CHECK(g_rg_ctx.navId != 0);
		radio_draw_frame(g, &inst, rc);
		CHECK(inst.lastEventNode == -1);
		CHECK(inst.lastEventKind == 2);
		CHECK(g_radio_change_events == 1);
		CHECK(strcmp(d.radioGroups[group].selectedRadioId, "radio2") == 0);

		/* 方向键只移动焦点; 空格提交选择. */
		CHECK(UiDesign_Key(&inst, VK_RIGHT) == 1);
		CHECK(UiDesign_Key(&inst, VK_SPACE) == 1);
		CHECK(inst.lastEventNode == -1 && inst.lastEventKind == 2);
		CHECK(g_radio_change_events == 2);
		CHECK(strcmp(d.radioGroups[group].selectedRadioId, "radio3") == 0);
		CHECK(UiDesign_Key(&inst, VK_HOME) == 1);
		CHECK(UiDesign_Key(&inst, VK_SPACE) == 1);
		CHECK(g_radio_change_events == 3);
		CHECK(strcmp(d.radioGroups[group].selectedRadioId, "radio1") == 0);

		/* 组禁用: 无停靠点, 不可点, 键盘忽略. */
		d.radioGroups[group].enabled = 0;
		radio_draw_frame(g, &inst, rc);
		CHECK(rg_nav_targets() == 0);
		CHECK(Ui_HitTest(&g_rg_ctx, x, y) == 0);
		CHECK(UiDesign_Key(&inst, VK_RIGHT) == 0);
		CHECK(g_radio_change_events == 3);

		/* 单项禁用: 跳过该项, 其余 Radio 仍可选中. */
		d.radioGroups[group].enabled = 1;
		d.nodes[r[1]].enabled = 0;
		radio_draw_frame(g, &inst, rc);
		CHECK(rg_nav_targets() == 1);
		x = inst.rects[r[1]].x + inst.rects[r[1]].w * 0.5f;
		y = inst.rects[r[1]].y + inst.rects[r[1]].h * 0.5f;
		CHECK(Ui_HitTest(&g_rg_ctx, x, y) == 0);
		CHECK(UiDesign_Key(&inst, VK_RIGHT) == 1);
		CHECK(UiDesign_Key(&inst, VK_SPACE) == 1);
		CHECK(g_radio_change_events == 4);
		CHECK(strcmp(d.radioGroups[group].selectedRadioId, "radio3") == 0);

		/* 空组存在但不产生停靠点/事件. */
		CHECK(g_rg_ctx.navId != 0);
		radio_draw_frame(g, &inst, rc);
		CHECK(g_radio_change_events == 4);
	}
	if (ready) UiDesignInstance_Shutdown(&inst);
	if (g) {
		Ui_ContextShutdown(&g_rg_ctx);
		UiTheme_Shutdown(&g_rg_theme, g);
		Gfx_Shutdown(g);
	}
	if (hwnd) DestroyWindow(hwnd);
	CoUninitialize();
	UiDesign_Free(&d);
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

	/* 页数按需增长, 不受 32 页几何/模型上限约束 */
	UiDesign_TabsRemovePage(&d, tabs, 0);
	for (i = 0; i < 40; i++) {
		sprintf(id, "page%d", i + 1);
		sprintf(name, "页 %d", i + 1);
		CHECK(UiDesign_TabsInsertPage(&d, tabs, i, id, name) >= 0);
	}
	CHECK(UiDesign_TabsPageCount(&d, tabs) == 40);
	CHECK(UiDesign_TabsInsertPage(&d, tabs, 0, "over", "继续新增") >= 0);
	CHECK(UiDesign_TabsPageCount(&d, tabs) == 41);
	UiDesign_TabsRemovePage(&d, tabs, 0);

	UiDesign_TabsSetSelectedPage(&d, tabs, 0);
	pageNode = UiDesign_TabsPageAt(&d, tabs, 0);
	CHECK(UiDesign_TabsMovePage(&d, tabs, 0, 39) == 1);
	CHECK(UiDesign_TabsSelectedPage(&d, tabs) == 39);
	CHECK(UiDesign_TabsPageAt(&d, tabs, 39) == pageNode);
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

	UiDesign_SetString(&a.groups[g].outputC, "designer_ring.ui.c");
	UiDesign_SetString(&a.groups[g].outputH, "designer_ring.ui.h");
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
	UiDesign_SetString(&n->logoSrc, "assets/logo.png");
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
	UiDesign_SetString(&a.groups[g].outputC, "designer_qr.ui.c");
	UiDesign_SetString(&a.groups[g].outputH, "designer_qr.ui.h");
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
	CHECK(has_sub(buf, "UiDesign_SetString(&p1->logoSrc, \"assets/logo.png\");"));
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
	UiDesign_SetString(&n->gridColTracks, "100,1fr,auto");
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
	UiDesign_SetString(&UiDesign_Node(&d, root)->gridColTracks, "");
	CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 300, 100), rects, d.nodeCount) == 1);
	CHECK(rects[a].w > 93.2f && rects[a].w < 93.5f);
	CHECK(rects[c1].x > 103.2f && rects[c1].x < 103.5f);
	UiDesign_SetString(&UiDesign_Node(&d, root)->gridColTracks, "100,1fr,auto");
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
	UiDesign_SetString(&d.groups[g].outputC, "designer_grid.ui.c");
	UiDesign_SetString(&d.groups[g].outputH, "designer_grid.ui.h");
	CreateDirectoryW(L"build\\gen", NULL);
	lstrcpyW(pc, L"build\\gen\\designer_grid.ui.c");
	lstrcpyW(ph, L"build\\gen\\designer_grid.ui.h");
	DeleteFileW(pc);
	DeleteFileW(ph);
	CHECK(UiDesign_GenerateGroup(&d, g, L"build\\gen", report, sizeof(report)) == 2);
	CHECK(read_all(pc, buf, sizeof(buf)) == 1);
	CHECK(has_sub(buf, "UiDesign_SetString(&p0->gridColTracks, \"100,1fr,auto\");"));
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
	static const char *v4 =
		"{\"format\":\"cui-design\",\"version\":4,\"name\":\"old\","
		"\"groups\":[{\"id\":\"main\",\"name\":\"G\","
		"\"outputC\":\"old.ui.c\",\"outputH\":\"old.ui.h\","
		"\"symbolPrefix\":\"Old\"}],"
		"\"views\":[{\"id\":\"main\",\"name\":\"V\",\"width\":400,"
		"\"height\":300,\"group\":\"main\","
		"\"root\":{\"id\":\"root\",\"name\":\"R\",\"type\":\"column\","
		"\"layout\":{},\"props\":{},\"children\":[]}}],"
		"\"hosts\":[{\"id\":\"main\",\"name\":\"W\",\"title\":\"T\","
		"\"kind\":1,\"group\":\"main\",\"width\":400,\"height\":300,"
		"\"minWidth\":0,\"minHeight\":0,\"theme\":0,\"view\":\"main\"}]}";
	UiDesignDoc d;
	char report[128];
	static char buf[32768];
	char old[2048];
	int g, v1i, v2i, h1, h2, r, versionAt;
	WCHAR pc[MAX_PATH], ph[MAX_PATH];

	/* v4 视图/宿主模型往返 */
	UiDesign_Init(&d);
	CHECK(UiDesign_FromJson(&d, v4) == 1);
	if (d.viewCount == 1 && d.hostCount == 1) {
	CHECK(d.viewCount == 1 && d.hostCount == 1);
	CHECK(strcmp(d.views[0].id, "main") == 0);
	CHECK(d.views[0].root >= 0);
	CHECK(d.hosts[0].view == 0);
	CHECK(d.hosts[0].kind == UI_DH_DIALOG);
	CHECK(d.nodes[d.views[0].root].type == UI_DN_COLUMN);
	{
		char *j = UiDesign_ToJson(&d);
		CHECK(j && has_sub(j, "\"version\":4"));
		CHECK(j && has_sub(j, "\"views\""));
		CHECK(j && has_sub(j, "\"hosts\""));
		if (j) HeapFree(GetProcessHeap(), 0, j);
	}
	}
	memcpy(old, v4, strlen(v4) + 1);
	versionAt = find_sub(old, "\"version\":4");
	CHECK(versionAt >= 0);
	if (versionAt >= 0) {
		versionAt += (int)strlen("\"version\":");
		old[versionAt] = '3';
		CHECK(UiDesign_FromJson(&d, old) == 0);
	}
	UiDesign_Free(&d);

	/* 同一视图被 Window + Panel 复用 */
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	UiDesign_SetString(&d.groups[g].symbolPrefix, "Main");
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
	UiDesign_SetString(&d.groups[g].outputC, "designer_vh.ui.c");
	UiDesign_SetString(&d.groups[g].outputH, "designer_vh.ui.h");
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

static void test_dynamic_capacities(void)
{
	UiDesignDoc d, b;
	UiDesignNode *n, *dup;
	UiRect *rects;
	char *json, *longText, *tracks, *symbol;
	char report[128];
	static char buf[131072];
	WCHAR pc[MAX_PATH], ph[MAX_PATH];
	const int textLen = 8192, cols = 40;
	int g, w, root, node, copy, i, loaded;
	UiDesign_Init(&d);
	g = UiDesign_AddGroup(&d, "g", "G");
	w = mk_view_host(&d, g, UI_DH_WINDOW, "win");
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	UiDesign_View(&d, w)->root = root;
	longText = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)textLen + 1);
	tracks = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cols * 4);
	CHECK(longText != NULL && tracks != NULL);
	if (!longText || !tracks) {
		HeapFree(GetProcessHeap(), 0, longText);
		HeapFree(GetProcessHeap(), 0, tracks);
		UiDesign_Free(&d);
		return;
	}
	memset(longText, 'L', textLen);
	longText[textLen] = 0;
	for (i = 0; i < cols; i++) memcpy(tracks + i * 4, "1fr,", 4);
	tracks[cols * 4 - 1] = 0;
	CHECK(UiDesign_SetString(&d.groups[g].name, longText) == 1);
	CHECK(UiDesign_SetString(&d.groups[g].outputC, longText) == 1);
	CHECK(UiDesign_SetString(&d.groups[g].symbolPrefix, longText) == 1);
	CHECK(UiDesign_SetString(&d.hosts[0].title, longText) == 1);
	CHECK(UiDesign_SetString(&d.hosts[0].id, longText) == 1);
	node = UiDesign_AddNode(&d, w, UI_DN_TEXTAREA, root, longText, longText);
	CHECK(node >= 0);
	n = UiDesign_Node(&d, node);
	CHECK(UiDesign_NodeSetText(n, longText) == 1);
	CHECK(UiDesign_NodeSetHint(n, longText) == 1);
	CHECK(UiDesign_SetString(&n->onChange, longText) == 1);
	CHECK(UiDesign_SetString(&n->gridColTracks, tracks) == 1);
	CHECK(strlen(n->id) == (size_t)textLen && strlen(n->text) == (size_t)textLen);
	copy = UiDesign_NodeDuplicate(&d, node);
	CHECK(copy >= 0);
	dup = UiDesign_Node(&d, copy);
	CHECK(dup && dup->id != n->id && dup->name != n->name && dup->text != n->text);
	CHECK(dup && dup->hint != n->hint && dup->onChange != n->onChange);
	CHECK(dup && strcmp(dup->text, longText) == 0);
	json = UiDesign_ToJson(&d);
	CHECK(json != NULL && strlen(json) > (size_t)textLen * 5);
	UiDesign_Init(&b);
	loaded = json && UiDesign_FromJson(&b, json);
	CHECK(loaded == 1);
	if (json) HeapFree(GetProcessHeap(), 0, json);
	if (loaded && b.groupCount > g && b.hostCount > 0) {
		int bn = UiDesign_FindNode(&b, 0, longText);
		CHECK(strlen(b.groups[g].name) == (size_t)textLen);
		CHECK(strlen(b.hosts[0].title) == (size_t)textLen);
		CHECK(bn >= 0 && strcmp(UiDesign_Node(&b, bn)->text, longText) == 0);
	}
	UiDesign_Free(&b);

	{
		char prefix[513];
		memset(prefix, 'P', sizeof(prefix) - 1);
		prefix[sizeof(prefix) - 1] = 0;
		CHECK(UiDesign_SetString(&d.groups[g].symbolPrefix, prefix) == 1);
		CHECK(UiDesign_SetString(&d.hosts[0].id, prefix) == 1);
		CHECK(UiDesign_SetString(&d.groups[g].outputC, "designer_long.ui.c") == 1);
		CHECK(UiDesign_SetString(&d.groups[g].outputH, "designer_long.ui.h") == 1);
		CreateDirectoryW(L"build\\gen", NULL);
		lstrcpyW(pc, L"build\\gen\\designer_long.ui.c");
		lstrcpyW(ph, L"build\\gen\\designer_long.ui.h");
		DeleteFileW(pc);
		DeleteFileW(ph);
		CHECK(UiDesign_GenerateGroup(&d, g, L"build\\gen", report,
			sizeof(report)) == 2);
		CHECK(read_all(pc, buf, sizeof(buf)) == 1);
		CHECK(has_sub(buf, prefix));
		DeleteFileW(pc);
		DeleteFileW(ph);
	}

	UiDesign_NodeRemove(&d, copy);
	UiDesign_NodeRemove(&d, node);
	UiDesign_NodeRemove(&d, root);
	root = UiDesign_AddNode(&d, w, UI_DN_GRID, -1, "grid", "Grid");
	UiDesign_View(&d, w)->root = root;
	n = UiDesign_Node(&d, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->gridCols = cols;
	n->gridColGap = n->gridRowGap = 0.0f;
	CHECK(UiDesign_SetString(&n->gridColTracks, tracks) == 1);
	for (i = 0; i < cols; i++) {
		char id[32];
		sprintf(id, "cell%d", i);
		CHECK(UiDesign_AddNode(&d, w, UI_DN_LABEL, root, id, "Cell") >= 0);
	}
	rects = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)d.nodeCount * sizeof(*rects));
	CHECK(rects != NULL);
	if (rects) {
		CHECK(UiDesign_Layout(&d, w, Ui_Rect(0, 0, 400, 100), rects,
			d.nodeCount) == 1);
		n = UiDesign_Node(&d, root);
		CHECK(rects[n->children[0]].w == 10.0f);
		CHECK(rects[n->children[cols - 1]].x == 390.0f);
		CHECK(rects[n->children[cols - 1]].w == 10.0f);
		HeapFree(GetProcessHeap(), 0, rects);
	}
	CHECK(UiDesign_SetString(&d.hosts[0].id, longText) == 1);
	symbol = UiDesign_SymbolAlloc(d.hosts[0].id);
	CHECK(symbol != NULL && (int)strlen(symbol) == textLen);
	HeapFree(GetProcessHeap(), 0, symbol);
	HeapFree(GetProcessHeap(), 0, longText);
	HeapFree(GetProcessHeap(), 0, tracks);
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
	test_radiogroup_metadata();
	test_radiogroup_layout();
	test_radiogroup_capacities();
	test_radiogroup_runtime();
	test_tabs_pages();
	test_qrcode_node();
	test_ringprogress_node();
	test_grid_columns();
	test_view_host_model();
	test_size_mode_normalize();
	test_dynamic_capacities();
	sprintf(line, "designer_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
