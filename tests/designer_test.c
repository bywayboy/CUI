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

static void test_json_roundtrip(void)
{
	UiDesignDoc a, b;
	char *json;
	int label;
	UiDesign_Init(&a);
	CHECK(UiDesign_DefaultDoc(&a) == 1);
	CHECK(a.groupCount == 1 && a.windowCount == 1 && a.nodeCount >= 3);
	CHECK(UiDesign_Node(&a, a.windows[0].root) != NULL);
	label = UiDesign_FindNode(&a, 0, "title");
	CHECK(label >= 0);
	json = UiDesign_ToJson(&a);
	CHECK(json != NULL && strlen(json) > 100);
	if (!UiDesign_FromJson(&b, json)) {
		out_("parse failed\n");
		HeapFree(GetProcessHeap(), 0, json);
		UiDesign_Free(&a);
		UiDesign_Free(&b);
		return;
	}
	CHECK(b.groupCount == 1 && b.windowCount == 1);
	CHECK(strcmp(b.groups[0].id, "main") == 0);
	CHECK(b.windows[0].root >= 0);
	CHECK(UiDesign_FindNode(&b, 0, "title") >= 0);
	CHECK(strcmp(UiDesign_Node(&b, UiDesign_FindNode(&b, 0, "title"))->text,
		"新建窗口") == 0);
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
	w = UiDesign_AddWindow(&d, "win", "W", UI_DW_NATIVE, g);
	root = UiDesign_AddNode(&d, w, UI_DN_COLUMN, -1, "root", "Root");
	d.windows[w].root = root;
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
	w = UiDesign_AddWindow(&d, "win", "W", UI_DW_NATIVE, g);
	root = UiDesign_AddNode(&d, w, UI_DN_ABSOLUTE, -1, "root", "Root");
	d.windows[w].root = root;
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
	w = UiDesign_AddWindow(&d, "win", "W", UI_DW_NATIVE, g);
	root = UiDesign_AddNode(&d, w, UI_DN_ROW, -1, "root", "Root");
	d.windows[w].root = root;
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
	w = UiDesign_AddWindow(&d, "win", "W", UI_DW_NATIVE, g);
	root = UiDesign_AddNode(&d, w, UI_DN_GRID, -1, "root", "Root");
	d.windows[w].root = root;
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
	w2 = UiDesign_AddWindow(&d, "dialog1", "Dialog", UI_DW_DIALOG, 0);
	root2 = UiDesign_AddNode(&d, w2, UI_DN_COLUMN, -1, "droot", "Dialog Root");
	UiDesign_Node(&d, root2)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&d, root2)->hMode = UI_DN_SIZE_FILL;
	wroot = UiDesign_AddNode(&d, w2, UI_DN_BUTTON, root2, "dbtn", "Dialog Button");
	UiDesign_Node(&d, wroot)->wMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, wroot)->hMode = UI_DN_SIZE_FIXED;
	UiDesign_Node(&d, wroot)->w = 100;
	UiDesign_Node(&d, wroot)->h = 32;
	d.windows[w2].root = root2;
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
	CHECK(has_sub(buf, "Main_WIN_MAIN"));
	CHECK(has_sub(buf, "Main_WIN_DIALOG1"));
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
	UiDesign_SetText(d.windows[0].title, 128, "Changed Title");
	n = UiDesign_PreviewAll(&d, L"build\\gen", f, 4, report, sizeof(report));
	CHECK(n == 2 && f[0].status == 1);
	CHECK(f[0].newLines > 0 && f[0].oldLines > 0);
	DeleteFileW(pc);
	DeleteFileW(ph);
	UiDesign_Free(&d);
}

static void test_save_load(void)
{
	UiDesignDoc a, b;
	CHECK(UiDesign_DefaultDoc(&a) == 1);
	UiDesign_SetText(a.windows[0].id, UI_DN_ID_MAX, "saved_win");
	CHECK(UiDesign_SaveFile(&a, L"build\\gen\\designer_test.cui") == 1);
	CHECK(UiDesign_LoadFile(&b, L"build\\gen\\designer_test.cui") == 1);
	CHECK(strcmp(b.windows[0].id, "saved_win") == 0);
	CHECK(b.windows[0].root >= 0);
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

void __stdcall startup(void)
{
	char line[160];
	test_json_roundtrip();
	test_layout();
	test_codegen();
	test_preview();
	test_save_load();
	test_symbol();
	sprintf(line, "designer_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
