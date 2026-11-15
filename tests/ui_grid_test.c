/*
 * ui_grid_test.c - Grid 布局 headless 断言
 *
 * 覆盖: 固定轨道黄金值, auto/minmax/fr, 跨格, 隐式行列, dense/sparse,
 * 收缩溢出, 自然尺寸, >24 项堆路径。退出码 0 = 全通过。
 */
#include <windows.h>
#include "ui/ui_layout.h"

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

static int near_(float a, float b)
{
	float d = a - b;
	return d <= 0.05f && d >= -0.05f;
}

typedef struct FakeBox {
	float minW, prefW, minH, prefH;
	float lastMaxW;
	int calls;
} FakeBox;

static void box_measure(void *user, UiMeasure *m)
{
	FakeBox *b = (FakeBox *)user;
	b->lastMaxW = m->maxW;
	b->calls++;
	m->minW = b->minW;
	m->prefW = b->prefW;
	m->minH = b->minH;
	m->prefH = b->prefH;
}

static UiGridItem box_item_(FakeBox *b, int row, int col,
	int rs, int cs, int stretch)
{
	UiGridItem it;
	it.user = b;
	it.measure = box_measure;
	it.row = row;
	it.col = col;
	it.rowSpan = rs;
	it.colSpan = cs;
	it.stretch = stretch;
	it.align = UI_ALIGN_START;
	return it;
}

static UiGrid grid_(int rows, int cols, float rg, float cg, int dense)
{
	UiGrid g;
	g.rows = rows;
	g.cols = cols;
	g.rowGap = rg;
	g.colGap = cg;
	g.dense = dense;
	return g;
}

static UiGridTrack fixed_track_(float v)
{
	UiGridTrack t;
	t.kind = UI_GRID_FIXED;
	t.value = v;
	t.minSize = 0.0f;
	t.maxSize = 0.0f;
	return t;
}

static UiGridTrack auto_track_(float mn, float mx)
{
	UiGridTrack t;
	t.kind = UI_GRID_AUTO;
	t.value = 0.0f;
	t.minSize = mn;
	t.maxSize = mx;
	return t;
}

static UiGridTrack fr_track_(float weight, float mn, float mx)
{
	UiGridTrack t;
	t.kind = UI_GRID_FR;
	t.value = weight;
	t.minSize = mn;
	t.maxSize = mx;
	return t;
}

static void test_fixed_grid(void)
{
	FakeBox a = { 0, 30, 10, 20 };
	FakeBox b = { 0, 30, 10, 20 };
	FakeBox c = { 0, 30, 10, 20 };
	FakeBox d = { 0, 30, 10, 20 };
	UiGridTrack cols[2] = { fixed_track_(100), fixed_track_(150) };
	UiGridTrack rows[2] = { fixed_track_(40), fixed_track_(60) };
	UiGridItem it[4];
	UiRect rc[4];
	UiGrid g = grid_(2, 2, 12, 10, 0);
	UiMeasure m;

	it[0] = box_item_(&a, -1, -1, 1, 1, 1);
	it[1] = box_item_(&b, -1, -1, 1, 1, 1);
	it[2] = box_item_(&c, -1, -1, 1, 1, 1);
	it[3] = box_item_(&d, -1, -1, 1, 1, 1);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 200), &g, cols, rows, it, 4, rc));
	CHECK(near_(rc[0].x, 0) && near_(rc[0].y, 0));
	CHECK(near_(rc[0].w, 100) && near_(rc[0].h, 40));
	CHECK(near_(rc[1].x, 110) && near_(rc[1].w, 150));
	CHECK(near_(rc[2].x, 0) && near_(rc[2].y, 52));
	CHECK(near_(rc[2].h, 60));
	CHECK(near_(rc[3].x, 110) && near_(rc[3].y, 52));
	CHECK(near_(a.lastMaxW, 100) && near_(b.lastMaxW, 150));

	m.maxW = -1;
	Ui_GridMeasure(&g, cols, rows, it, 4, &m);
	CHECK(near_(m.minW, 260) && near_(m.prefW, 260));
	CHECK(near_(m.minH, 112) && near_(m.prefH, 112));
}

static void test_auto_minmax(void)
{
	FakeBox a = { 30, 80, 20, 20 };
	FakeBox b = { 20, 60, 20, 20 };
	UiGridTrack cols[2];
	UiGridItem it[2];
	UiRect rc[2];
	UiGrid g = grid_(0, 2, 0, 10, 0);
	UiMeasure m;

	cols[0] = auto_track_(0, 0);
	cols[1] = auto_track_(0, 0);
	it[0] = box_item_(&a, -1, -1, 1, 1, 1);
	it[1] = box_item_(&b, -1, -1, 1, 1, 1);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 100), &g, cols, NULL, it, 2, rc));
	CHECK(near_(rc[0].x, 0) && near_(rc[0].w, 80));
	CHECK(near_(rc[1].x, 90) && near_(rc[1].w, 60));

	m.maxW = -1;
	Ui_GridMeasure(&g, cols, NULL, it, 2, &m);
	CHECK(near_(m.minW, 60) && near_(m.prefW, 150));
	CHECK(near_(m.minH, 20) && near_(m.prefH, 20));

	cols[0] = auto_track_(70, 0);
	a.prefW = 30;
	a.minW = 10;
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 100), &g, cols, NULL, it, 2, rc));
	CHECK(near_(rc[0].w, 70));

	cols[0] = auto_track_(0, 50);
	a.prefW = 80;
	a.minW = 30;
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 100), &g, cols, NULL, it, 2, rc));
	CHECK(near_(rc[0].w, 50));
}

static void test_fr(void)
{
	FakeBox a = { 10, 20, 20, 20 };
	FakeBox b = { 10, 20, 20, 20 };
	FakeBox c = { 10, 20, 20, 20 };
	UiGridTrack cols[3];
	UiGridItem it[3];
	UiRect rc[3];
	UiGrid g = grid_(0, 3, 0, 10, 0);

	cols[0] = fixed_track_(50);
	cols[1] = fr_track_(1, 0, 0);
	cols[2] = fr_track_(2, 0, 0);
	it[0] = box_item_(&a, -1, -1, 1, 1, 1);
	it[1] = box_item_(&b, -1, -1, 1, 1, 1);
	it[2] = box_item_(&c, -1, -1, 1, 1, 1);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 100), &g, cols, NULL, it, 3, rc));
	CHECK(near_(rc[0].w, 50));
	CHECK(near_(rc[1].w, 80));
	CHECK(near_(rc[2].w, 150));
	CHECK(near_(rc[2].x + rc[2].w, 300));

	cols[1] = fr_track_(1, 0, 60);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 100), &g, cols, NULL, it, 3, rc));
	CHECK(near_(rc[1].w, 60) && near_(rc[2].w, 170));

	cols[1] = fr_track_(1, 100, 0);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 100), &g, cols, NULL, it, 3, rc));
	CHECK(near_(rc[1].w, 140));
	CHECK(near_(rc[2].w, 90));
}

static void test_span(void)
{
	FakeBox a = { 120, 150, 20, 20 };
	FakeBox b = { 10, 30, 20, 20 };
	UiGridTrack cols[2];
	UiGridItem it[2];
	UiRect rc[2];
	UiGrid g = grid_(0, 2, 0, 10, 0);
	UiMeasure m;

	cols[0] = auto_track_(0, 0);
	cols[1] = auto_track_(0, 0);
	it[0] = box_item_(&a, -1, -1, 1, 2, 1);
	it[1] = box_item_(&b, -1, -1, 1, 1, 1);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 100), &g, cols, NULL, it, 2, rc));
	CHECK(near_(rc[0].w, 150));
	CHECK(near_(rc[1].w, 85));

	m.maxW = -1;
	Ui_GridMeasure(&g, cols, NULL, it, 2, &m);
	CHECK(near_(m.prefW, 150));
	CHECK(near_(m.minW, 120));
}

static void test_implicit(void)
{
	FakeBox box[5];
	UiGridTrack cols[2] = { fixed_track_(50), fixed_track_(50) };
	UiGridTrack rows[1] = { fixed_track_(20) };
	UiGridItem it[5];
	UiRect rc[5];
	UiGrid g = grid_(1, 2, 0, 0, 0);
	UiMeasure m;
	int i;

	for (i = 0; i < 5; i++) {
		box[i].minW = 10; box[i].prefW = 40;
		box[i].minH = 10; box[i].prefH = 30;
		box[i].lastMaxW = 0; box[i].calls = 0;
		it[i] = box_item_(&box[i], -1, -1, 1, 1, 1);
	}
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 300), &g, cols, rows, it, 5, rc));
	CHECK(near_(rc[0].y, 0) && near_(rc[0].h, 20));
	CHECK(near_(rc[1].y, 0));
	CHECK(near_(rc[2].y, 20) && near_(rc[2].h, 30));
	CHECK(near_(rc[3].y, 20));
	CHECK(near_(rc[4].x, 0) && near_(rc[4].y, 50));

	m.maxW = -1;
	Ui_GridMeasure(&g, cols, rows, it, 5, &m);
	CHECK(near_(m.prefW, 100) && near_(m.prefH, 80));

	/* 显式第 4 列创建隐式列 (auto) */
	{
		FakeBox x = { 10, 70, 10, 30 };
		FakeBox y = { 10, 30, 10, 30 };
		UiGridTrack c2[2] = { fixed_track_(50), fixed_track_(50) };
		UiGridTrack r1[1] = { fixed_track_(20) };
		UiGridItem two[2];
		UiRect rr[2];
		UiGrid gg = grid_(1, 2, 0, 0, 0);
		two[0] = box_item_(&x, 0, 3, 1, 1, 1);
		two[1] = box_item_(&y, 0, 0, 1, 1, 1);
		CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 300), &gg, c2, r1,
			two, 2, rr));
		CHECK(near_(rr[1].x, 0) && near_(rr[1].w, 50));
		CHECK(near_(rr[0].x, 100) && near_(rr[0].w, 70));
	}
}

static void test_dense(void)
{
	FakeBox a = { 10, 30, 10, 30 };
	FakeBox b = { 10, 30, 10, 30 };
	FakeBox c = { 10, 30, 10, 30 };
	FakeBox d = { 10, 30, 10, 30 };
	UiGridTrack cols[3] = {
		fixed_track_(50), fixed_track_(50), fixed_track_(50) };
	UiGridItem it[4];
	UiRect rc[4];

	it[0] = box_item_(&a, 0, 1, 1, 1, 1);
	it[1] = box_item_(&b, -1, -1, 1, 2, 1);
	it[2] = box_item_(&c, -1, -1, 1, 1, 1);
	it[3] = box_item_(&d, -1, -1, 1, 1, 1);

	{
		UiGrid g = grid_(0, 3, 0, 0, 0);
		CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 300), &g, cols, NULL,
			it, 4, rc));
		CHECK(near_(rc[0].x, 50) && near_(rc[0].y, 0));
		CHECK(near_(rc[1].x, 0) && near_(rc[1].y, 30));
		CHECK(near_(rc[1].w, 100));
		CHECK(near_(rc[2].x, 100) && near_(rc[2].y, 30));
		CHECK(near_(rc[3].x, 0) && near_(rc[3].y, 60));
	}
	{
		UiGrid g = grid_(0, 3, 0, 0, 1);
		CHECK(Ui_GridLayout(Ui_Rect(0, 0, 300, 300), &g, cols, NULL,
			it, 4, rc));
		CHECK(near_(rc[0].x, 50) && near_(rc[0].y, 0));
		CHECK(near_(rc[1].x, 0) && near_(rc[1].y, 30));
		CHECK(near_(rc[2].x, 0) && near_(rc[2].y, 0));
		CHECK(near_(rc[3].x, 100) && near_(rc[3].y, 0));
	}
}

static void test_shrink(void)
{
	FakeBox a = { 30, 80, 10, 20 };
	FakeBox b = { 20, 100, 10, 20 };
	UiGridTrack cols[3];
	UiGridItem it[2];
	UiRect rc[2];
	UiGrid g = grid_(0, 3, 0, 10, 0);

	cols[0] = fixed_track_(100);
	cols[1] = auto_track_(0, 0);
	cols[2] = fr_track_(1, 20, 0);
	it[0] = box_item_(&a, 0, 1, 1, 1, 1);
	it[1] = box_item_(&b, 0, 2, 1, 1, 1);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 150, 100), &g, cols, NULL, it, 2, rc));
	CHECK(near_(rc[0].w, 30));
	CHECK(near_(rc[1].w, 20));
	CHECK(rc[1].x + rc[1].w > 150.0f);
}

static void test_measure_wrap(void)
{
	FakeBox a = { 30, 80, 10, 20 };
	FakeBox b = { 30, 80, 10, 20 };
	UiGridTrack cols[2];
	UiGridItem it[2];
	UiGrid g = grid_(0, 2, 0, 10, 0);
	UiMeasure m;

	cols[0] = auto_track_(0, 0);
	cols[1] = auto_track_(0, 0);
	it[0] = box_item_(&a, -1, -1, 1, 1, 1);
	it[1] = box_item_(&b, -1, -1, 1, 1, 1);
	m.maxW = 50;
	Ui_GridMeasure(&g, cols, NULL, it, 2, &m);
	CHECK(near_(m.prefW, 70));
	CHECK(near_(m.minW, 70));
	CHECK(near_(m.prefH, 20));
}

static void test_edges(void)
{
	FakeBox a = { 10, 20, 10, 20 };
	UiGridTrack cols[1] = { fixed_track_(50) };
	UiGridItem it[1];
	UiRect rc[1];
	UiGrid g = grid_(1, 1, 0, 0, 0);
	UiMeasure m;

	it[0] = box_item_(&a, -1, -1, 1, 1, 1);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 100, 100), &g, cols, NULL,
		it, 0, rc) == 0);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 0, 100), &g, cols, NULL,
		it, 1, rc) == 0);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 100, 100), NULL, cols, NULL,
		it, 1, rc) == 0);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 100, 100), &g, cols, NULL,
		it, 1, NULL) == 0);

	m.maxW = -1;
	m.minW = 99;
	Ui_GridMeasure(&g, cols, NULL, it, 0, &m);
	CHECK(m.minW == 0.0f && m.prefW == 0.0f);

	/* cols=0: 无显式轨道, 退回隐式 auto 列 */
	g.cols = 0;
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 100, 100), &g, cols, NULL,
		it, 1, rc) == 1);
	CHECK(near_(rc[0].w, 20) && near_(rc[0].h, 20));

	/* 非法 span */
	it[0] = box_item_(&a, 0, 0, 70000, 1, 1);
	CHECK(Ui_GridLayout(Ui_Rect(0, 0, 100, 100), &g, cols, NULL,
		it, 1, rc) == 0);

	/* 40 项 / 32 列: 轨道、item、占位矩阵全部走堆路径 */
	{
		FakeBox many[40];
		UiGridTrack c32[32];
		UiGridItem mit[40];
		UiRect mrc[40];
		UiGrid gg = grid_(0, 32, 2, 2, 1);
		int i, ok = 1;
		for (i = 0; i < 32; i++) c32[i] = fixed_track_(20);
		for (i = 0; i < 40; i++) {
			many[i].minW = 5; many[i].prefW = 10;
			many[i].minH = 5; many[i].prefH = 10;
			many[i].lastMaxW = 0; many[i].calls = 0;
			mit[i] = box_item_(&many[i], -1, -1, 1, 1, 1);
		}
		CHECK(Ui_GridLayout(Ui_Rect(0, 0, 800, 800), &gg, c32, NULL,
			mit, 40, mrc) == 1);
		for (i = 1; i < 32; i++)
			if (mrc[i].x <= mrc[i - 1].x) ok = 0;
		CHECK(ok);
		CHECK(near_(mrc[32].y, 12));
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}
}

void __stdcall startup(void)
{
	char line[128];
	test_fixed_grid();
	test_auto_minmax();
	test_fr();
	test_span();
	test_implicit();
	test_dense();
	test_shrink();
	test_measure_wrap();
	test_edges();
	sprintf(line, "ui_grid_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
