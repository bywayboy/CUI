/*
 * ui_layout_test.c - measure 协议 + Ui_FlexLayout headless 断言
 *
 * 覆盖: 归一化, 容器自然尺寸 (含 min/max 硬限制), 行/列黄金分配,
 * 宽度/高度扫描不变量 (min 溢出, min~pref 压缩, 富余扩张, gap, 无重叠),
 * 交叉轴对齐/拉伸/固定尺寸, 空列表/零尺寸/超大 gap, >32 项堆分配路径.
 * 构建: bin\cui_layout_test.exe; 退出码 0=全通过。
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

/* 合成文本: 自然宽/最长词宽固定, 行数按 maxW 换行.
 * fixedMinH/fixedPrefH > 0 时用固定高度 (列布局测试用块状内容). */
typedef struct FakeText {
	float minW, prefW, lineH;
	float fixedMinH, fixedPrefH;
	float lastMaxW;
	int calls;
	float baseline;
} FakeText;

static int fake_ceil_(float v)
{
	int i = (int)v;
	return (float)i < v ? i + 1 : i;
}

static void fake_measure(void *user, UiMeasure *m)
{
	FakeText *t = (FakeText *)user;
	t->lastMaxW = m->maxW;
	t->calls++;
	m->minW = t->minW;
	m->prefW = t->prefW;
	m->baseline = t->baseline;
	m->minH = t->fixedMinH > 0.0f ? t->fixedMinH : t->lineH;
	if (t->fixedPrefH > 0.0f) {
		m->prefH = t->fixedPrefH;
	} else if (m->maxW < 0.0f || m->maxW >= t->prefW) {
		m->prefH = t->lineH;
	} else {
		float w = m->maxW > t->minW ? m->maxW : t->minW;
		int lines = fake_ceil_(t->prefW / w);
		if (lines < 1) lines = 1;
		m->prefH = t->lineH * (float)lines;
	}
}

static UiMeasureItem item_(FakeText *t, float grow, float minSize,
	float maxSize, int stretch)
{
	UiMeasureItem it;
	it.user = t;
	it.measure = fake_measure;
	it.grow = grow;
	it.minSize = minSize;
	it.maxSize = maxSize;
	it.minCross = 0.0f;
	it.maxCross = 0.0f;
	it.crossSize = 0.0f;
	it.stretch = stretch;
	it.align = UI_ALIGN_START;
	return it;
}

static void test_normalize(void)
{
	UiMeasure m;
	m.maxW = 0; m.minW = -5; m.prefW = -3; m.minH = -1; m.prefH = -2;
	Ui_MeasureNormalize(&m);
	CHECK(m.minW == 0.0f && m.prefW == 0.0f);
	CHECK(m.minH == 0.0f && m.prefH == 0.0f);
	m.minW = 50; m.prefW = 20; m.minH = 30; m.prefH = 10;
	Ui_MeasureNormalize(&m);
	CHECK(m.prefW == 50.0f && m.prefH == 30.0f);
}

static void test_fixed_adapter(void)
{
	UiFixedSize fixedSize = { 70, 0 };
	UiFixedSize spring = { 0, 0 };
	UiMeasureItem items[2];
	UiMeasure nat;
	UiRect out[2];

	items[0] = Ui_FixedItem(&fixedSize, 0.0f);
	items[1] = Ui_FixedItem(&spring, 1.0f);
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 200, 40), UI_ROW, 10, items, 2, out));
	CHECK(out[0].w == 70.0f && out[1].w == 120.0f);
	CHECK(out[0].h == 40.0f && out[1].h == 40.0f);
	Ui_FlexMeasure(UI_ROW, 10, items, 2, &nat);
	CHECK(nat.minW == 80.0f && nat.prefW == 80.0f);
	CHECK(nat.minH == 0.0f && nat.prefH == 0.0f);

	/* grow=0 的零尺寸项保持 0, 不吞剩余空间 */
	items[1].grow = 0.0f;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 200, 40), UI_ROW, 10, items, 2, out));
	CHECK(out[0].w == 70.0f && out[1].w == 0.0f);
}

static void test_intrinsic(void)
{
	FakeText a = { 40, 120, 20, 0, 0 };
	FakeText b = { 60, 180, 20, 0, 0 };
	FakeText c = { 30, 90, 20, 0, 0 };
	UiMeasureItem it[3];
	UiMeasure m;
	it[0] = item_(&a, 1, 0, 0, 0);
	it[1] = item_(&b, 2, 0, 0, 0);
	it[2] = item_(&c, 0, 0, 0, 0);
	Ui_FlexMeasure(UI_ROW, 10, it, 3, &m);
	CHECK(near_(m.minW, 150.0f));
	CHECK(near_(m.prefW, 410.0f));
	CHECK(near_(m.minH, 20.0f));
	CHECK(near_(m.prefH, 20.0f));

	it[1].minSize = 200;
	Ui_FlexMeasure(UI_ROW, 10, it, 3, &m);
	CHECK(near_(m.minW, 290.0f));
	CHECK(near_(m.prefW, 430.0f));
	it[1].minSize = 0;
	it[2].maxSize = 60;
	Ui_FlexMeasure(UI_ROW, 10, it, 3, &m);
	CHECK(near_(m.prefW, 380.0f));

	Ui_FlexMeasure(UI_COLUMN, 10, it, 3, &m);
	CHECK(near_(m.minW, 60.0f));
	CHECK(near_(m.prefW, 180.0f));
	CHECK(near_(m.minH, 80.0f));
	CHECK(near_(m.prefH, 80.0f));
}

static void test_row_golden(void)
{
	FakeText a = { 40, 120, 20, 0, 0 };
	FakeText b = { 60, 180, 20, 0, 0 };
	FakeText c = { 30, 90, 20, 0, 0 };
	UiMeasureItem it[3];
	UiRect rc[3];
	it[0] = item_(&a, 1, 0, 0, 0);
	it[1] = item_(&b, 2, 0, 0, 0);
	it[2] = item_(&c, 0, 0, 0, 0);

	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 800, 200), UI_ROW, 10, it, 3, rc) == 1);
	CHECK(near_(rc[0].x, 0.0f) && near_(rc[0].w, 250.0f));
	CHECK(near_(rc[1].x, 260.0f) && near_(rc[1].w, 440.0f));
	CHECK(near_(rc[2].x, 710.0f) && near_(rc[2].w, 90.0f));
	CHECK(near_(rc[0].h, 20.0f) && near_(rc[1].h, 20.0f) &&
		near_(rc[2].h, 20.0f));
	CHECK(near_(a.lastMaxW, rc[0].w));
	CHECK(near_(b.lastMaxW, rc[1].w));
	CHECK(near_(c.lastMaxW, rc[2].w));

	/* 压缩区: 期望 390 + gap 20 压到 240 可用宽 */
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 260, 200), UI_ROW, 10, it, 3, rc) == 1);
	CHECK(near_(rc[0].w, 73.846f));
	CHECK(near_(rc[1].w, 110.769f));
	CHECK(near_(rc[2].w, 55.385f));
	CHECK(near_(rc[0].x + rc[0].w + 10.0f, rc[1].x));
	CHECK(near_(rc[1].x + rc[1].w + 10.0f, rc[2].x));
	CHECK(near_(rc[2].x + rc[2].w, 260.0f));
	CHECK(near_(rc[0].h, 40.0f) && near_(rc[1].h, 40.0f) &&
		near_(rc[2].h, 40.0f));

	/* 低于 min 总和: 保持 min, 允许溢出 */
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 100, 200), UI_ROW, 10, it, 3, rc) == 1);
	CHECK(near_(rc[0].w, 40.0f) && near_(rc[1].w, 60.0f) &&
		near_(rc[2].w, 30.0f));
	CHECK(near_(rc[0].x, 0.0f) && near_(rc[1].x, 50.0f) &&
		near_(rc[2].x, 120.0f));
	CHECK(near_(rc[0].h, 60.0f) && near_(rc[1].h, 60.0f) &&
		near_(rc[2].h, 60.0f));
}

static void test_row_sweep(void)
{
	FakeText a = { 40, 120, 20, 0, 0 };
	FakeText b = { 60, 180, 20, 0, 0 };
	FakeText c = { 30, 90, 20, 0, 0 };
	UiMeasureItem it[3];
	int w;
	it[0] = item_(&a, 1, 0, 0, 0);
	it[1] = item_(&b, 2, 0, 0, 0);
	it[2] = item_(&c, 0, 0, 0, 0);
	for (w = 5; w <= 1200; w += 5) {
		UiRect rc[3];
		float sum;
		CHECK(Ui_FlexLayout(Ui_Rect(0, 0, (float)w, 200), UI_ROW, 10,
			it, 3, rc) == 1);
		CHECK(rc[0].w >= 40.0f - 0.05f);
		CHECK(rc[1].w >= 60.0f - 0.05f);
		CHECK(rc[2].w >= 30.0f - 0.05f);
		CHECK(rc[0].x + rc[0].w <= rc[1].x + 0.05f);
		CHECK(rc[1].x + rc[1].w <= rc[2].x + 0.05f);
		CHECK(rc[0].h >= 20.0f && rc[1].h >= 20.0f && rc[2].h >= 20.0f);
		CHECK(rc[0].y >= 0.0f && rc[0].y + rc[0].h <= 200.0f);
		CHECK(near_(a.lastMaxW, rc[0].w));
		CHECK(near_(b.lastMaxW, rc[1].w));
		CHECK(near_(c.lastMaxW, rc[2].w));
		sum = rc[0].w + rc[1].w + rc[2].w;
		if (w >= 150) {
			CHECK(near_(sum + 20.0f, (float)w));
			if (w >= 20) CHECK(near_(rc[1].x - (rc[0].x + rc[0].w), 10.0f));
		} else {
			CHECK(near_(rc[0].w, 40.0f) && near_(rc[1].w, 60.0f) &&
				near_(rc[2].w, 30.0f));
		}
		if (w >= 410) {
			CHECK(rc[0].w >= 120.0f - 0.05f);
			CHECK(rc[1].w >= 180.0f - 0.05f);
			CHECK(rc[2].w >= 90.0f - 0.05f);
		}
	}
}

static void test_column(void)
{
	FakeText a = { 40, 120, 20, 40, 120, 0, 0 };
	FakeText b = { 60, 180, 20, 60, 180, 0, 0 };
	FakeText c = { 30, 90, 20, 30, 90, 0, 0 };
	UiMeasureItem it[3];
	UiRect rc[3];
	it[0] = item_(&a, 1, 0, 0, 1);
	it[1] = item_(&b, 2, 0, 0, 1);
	it[2] = item_(&c, 0, 0, 0, 1);

	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 300), UI_COLUMN, 10,
		it, 3, rc) == 1);
	CHECK(near_(rc[0].h, 86.154f));
	CHECK(near_(rc[1].h, 129.231f));
	CHECK(near_(rc[2].h, 64.615f));
	CHECK(near_(rc[0].x, 0.0f) && near_(rc[0].w, 300.0f));
	CHECK(near_(rc[1].x, 0.0f) && near_(rc[1].w, 300.0f));
	CHECK(near_(rc[2].y, rc[0].h + rc[1].h + 20.0f));
	CHECK(near_(rc[2].y + rc[2].h, 300.0f));
	CHECK(near_(a.lastMaxW, 300.0f));

	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 800), UI_COLUMN, 10,
		it, 3, rc) == 1);
	CHECK(near_(rc[0].h, 250.0f));
	CHECK(near_(rc[1].h, 440.0f));
	CHECK(near_(rc[2].h, 90.0f));
	CHECK(near_(rc[2].y + rc[2].h, 800.0f));

	{
		int h;
		for (h = 100; h <= 900; h += 10) {
			float sum;
			CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, (float)h), UI_COLUMN,
				10, it, 3, rc) == 1);
			CHECK(rc[0].h >= 40.0f - 0.05f);
			CHECK(rc[1].h >= 60.0f - 0.05f);
			CHECK(rc[2].h >= 30.0f - 0.05f);
			CHECK(rc[0].y + rc[0].h <= rc[1].y + 0.05f);
			CHECK(rc[1].y + rc[1].h <= rc[2].y + 0.05f);
			sum = rc[0].h + rc[1].h + rc[2].h;
			if (h >= 150) {
				CHECK(near_(sum + 20.0f, (float)h));
			} else {
				CHECK(near_(rc[0].h, 40.0f) && near_(rc[1].h, 60.0f) &&
					near_(rc[2].h, 30.0f));
				CHECK(rc[2].y + rc[2].h > (float)h);
			}
		}
	}
}

static void test_cross_axis(void)
{
	FakeText a = { 10, 40, 20, 0, 0 };
	FakeText b = { 10, 60, 20, 0, 0 };
	UiMeasureItem it[2];
	UiRect rc[2];
	it[0] = item_(&a, 0, 0, 0, 0);
	it[1] = item_(&b, 0, 0, 0, 0);
	it[0].align = UI_ALIGN_CENTER;
	it[1].align = UI_ALIGN_END;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].y, 40.0f) && near_(rc[0].h, 20.0f));
	CHECK(near_(rc[1].y, 80.0f) && near_(rc[1].h, 20.0f));
	it[0].stretch = 1;
	it[1].crossSize = 50.0f;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].h, 100.0f) && near_(rc[0].y, 0.0f));
	CHECK(near_(rc[1].h, 50.0f) && near_(rc[1].y, 50.0f));
}

static void test_edges(void)
{
	FakeText a = { 10, 20, 20, 0, 0 };
	FakeText b = { 10, 20, 20, 0, 0 };
	UiMeasureItem it[2];
	UiRect rc[2];
	UiMeasure m;
	it[0] = item_(&a, 0, 0, 0, 0);
	it[1] = item_(&b, 0, 0, 0, 0);

	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 100, 100), UI_ROW, 0, it, 0, rc) == 0);
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 0, 100), UI_ROW, 0, it, 2, rc) == 0);
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 100, 0), UI_COLUMN, 0, it, 2, rc) == 0);
	Ui_FlexMeasure(UI_ROW, 10, it, 0, &m);
	CHECK(m.minW == 0.0f && m.prefW == 0.0f &&
		m.minH == 0.0f && m.prefH == 0.0f);
	Ui_FlexMeasure(UI_ROW, 10, NULL, 2, &m);
	CHECK(m.prefW == 0.0f);

	/* gap 被容器主轴长度截断 */
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 50, 100), UI_ROW, 100, it, 2, rc) == 1);
	CHECK(near_(rc[0].x, 0.0f) && near_(rc[0].w, 10.0f));
	CHECK(near_(rc[1].x, 60.0f) && near_(rc[1].w, 10.0f));

	/* minSize > maxSize: 以 minSize 为准 */
	it[0].minSize = 50;
	it[0].maxSize = 30;
	Ui_FlexMeasure(UI_ROW, 0, it, 1, &m);
	CHECK(near_(m.minW, 50.0f) && near_(m.prefW, 50.0f));

	/* >32 项走堆分配路径 */
	{
		FakeText many[40];
		UiMeasureItem mit[40];
		UiRect mrc[40];
		int i, ok = 1;
		for (i = 0; i < 40; i++) {
			many[i].minW = 5.0f;
			many[i].prefW = 10.0f;
			many[i].lineH = 20.0f;
			many[i].lastMaxW = 0;
			many[i].calls = 0;
			mit[i] = item_(&many[i], 0, 0, 0, 0);
		}
		CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 800, 100), UI_ROW, 5,
			mit, 40, mrc) == 1);
		for (i = 1; i < 40; i++)
			if (mrc[i].x + 0.05f < mrc[i - 1].x + mrc[i - 1].w) ok = 0;
		CHECK(ok);
		CHECK(near_(mrc[0].w, 10.0f) && near_(mrc[39].x, 39.0f * 15.0f));
		CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0);
	}
}

static void test_cross_limits(void)
{
	FakeText a = { 10, 40, 20, 0, 0 };
	FakeText b = { 20, 60, 20, 0, 0 };
	UiMeasureItem it[2];
	UiRect rc[2];
	UiMeasure m;

	it[0] = item_(&a, 0, 0, 0, 0);
	it[1] = item_(&b, 0, 0, 0, 0);
	it[0].minCross = 30.0f;
	it[0].maxCross = 40.0f;
	it[1].maxCross = 15.0f;
	Ui_FlexMeasure(UI_ROW, 0, it, 2, &m);
	CHECK(near_(m.minH, 30.0f) && near_(m.prefH, 30.0f));

	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].h, 30.0f) && near_(rc[1].h, 15.0f));
	CHECK(near_(rc[0].y, 0.0f) && near_(rc[1].y, 0.0f));

	/* 拉伸被 maxCross 截断; crossSize 同样夹取 */
	it[0].stretch = 1;
	it[0].maxCross = 70.0f;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].h, 70.0f));
	it[0].crossSize = 90.0f;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].h, 70.0f));

	/* minCross 先于 crossSize 生效; maxCross < minCross 归一为 min */
	it[0].maxCross = 0.0f;
	it[0].minCross = 95.0f;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].h, 95.0f));
	it[0].minCross = 50.0f;
	it[0].maxCross = 30.0f;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].h, 50.0f));

	/* 列布局交叉轴 (宽) */
	it[0] = item_(&a, 0, 0, 0, 0);
	it[1] = item_(&b, 0, 0, 0, 0);
	it[0].minCross = 50.0f;
	it[0].maxCross = 120.0f;
	it[1].crossSize = 200.0f;
	it[1].maxCross = 150.0f;
	Ui_FlexMeasure(UI_COLUMN, 0, it, 2, &m);
	CHECK(near_(m.minW, 150.0f) && near_(m.prefW, 150.0f));
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 300), UI_COLUMN, 0, it, 2, rc) == 1);
	CHECK(near_(rc[0].w, 50.0f) && near_(rc[0].h, 20.0f));
	CHECK(near_(rc[1].w, 150.0f) && near_(rc[1].h, 20.0f));

	/* 固定 crossSize 计入自然尺寸 */
	it[0].crossSize = 60.0f;
	it[0].minCross = it[0].maxCross = 0.0f;
	it[1].crossSize = 60.0f;
	it[1].maxCross = 0.0f;
	Ui_FlexMeasure(UI_ROW, 0, it, 2, &m);
	CHECK(near_(m.minH, 60.0f) && near_(m.prefH, 60.0f));
}

static void test_baseline(void)
{
	FakeText a = { 10, 40, 20, 0, 0 };
	FakeText b = { 10, 40, 20, 0, 0 };
	FakeText c = { 10, 40, 20, 0, 0 };
	UiMeasureItem it[3];
	UiRect rc[3];
	UiMeasure m;

	a.baseline = 14.0f;
	b.baseline = 20.0f;
	c.baseline = 0.0f;
	it[0] = item_(&a, 0, 0, 0, 0);
	it[1] = item_(&b, 0, 0, 0, 0);
	it[2] = item_(&c, 0, 0, 0, 0);
	it[0].align = UI_ALIGN_BASELINE;
	it[1].align = UI_ALIGN_BASELINE;
	it[2].align = UI_ALIGN_BASELINE;
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 100), UI_ROW, 0, it, 3, rc) == 1);
	CHECK(near_(rc[0].y, 6.0f));
	CHECK(near_(rc[1].y, 0.0f));
	CHECK(near_(rc[2].y, 0.0f));

	/* 列布局忽略基线, 按起点摆放 */
	CHECK(Ui_FlexLayout(Ui_Rect(0, 0, 300, 300), UI_COLUMN, 0, it, 3, rc) == 1);
	CHECK(near_(rc[0].x, 0.0f) && near_(rc[1].x, 0.0f) &&
		near_(rc[2].x, 0.0f));

	/* 归一化: 负值清零, 超过 prefH 截断 */
	m.maxW = 0; m.minW = 10; m.prefW = 40; m.minH = 20; m.prefH = 20;
	m.baseline = -3.0f;
	Ui_MeasureNormalize(&m);
	CHECK(m.baseline == 0.0f);
	m.baseline = 100.0f;
	Ui_MeasureNormalize(&m);
	CHECK(near_(m.baseline, 20.0f));
}

void __stdcall startup(void)
{
	char line[128];
	test_normalize();
	test_fixed_adapter();
	test_intrinsic();
	test_row_golden();
	test_row_sweep();
	test_column();
	test_cross_axis();
	test_cross_limits();
	test_baseline();
	test_edges();
	sprintf(line, "ui_layout_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
