/*
 * svg_test.c - svg_doc 模型隐藏断言 (无需 Gfx 设备)
 *
 * 覆盖: 包围盒 (直线/旋转矩形/三次曲线/椭圆), 命中 (填充/描边容差/旋转/
 *       最上层), 节点插入 (贝塞尔 De Casteljau / 圆弧切分), 圆弧解算
 *       (切向/三点/大弧/半径放大/退化), 弧过拖动点, A 参数导出往返,
 *       20 步 undo/redo 序列化逐字节一致, 视口互逆, d 解析往返.
 * 构建: bin\cui_svg_test.exe; 退出码 0 = 全通过.
 */
#include <windows.h>
#include "svg/svg_doc.h"

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

static int streq_(const char *a, const char *b)
{
	if (!a || !b) return 0;
	while (*a && *a == *b) { a++; b++; }
	return *a == 0 && *b == 0;
}

static const char *find_(const char *s, const char *pat)
{
	const char *p, *q;
	if (!s || !pat) return NULL;
	for (; *s; s++) {
		for (p = s, q = pat; *q && *p == *q; p++, q++) {}
		if (!*q) return s;
	}
	return NULL;
}

/* 提取 "<path d=\"...\"" 的内容; 返回 1 成功 */
static int extract_path_d(const char *svg, char *out, int cap)
{
	const char *p = find_(svg, "<path d=\"");
	int n = 0;
	if (!p) return 0;
	p += 9;
	while (*p && *p != '"' && n < cap - 1) out[n++] = *p++;
	out[n] = 0;
	return n > 0;
}

/* ── 展平辅助 ─────────────────────────────────────────────────────── */

static int path_poly(const SvgPath *src, float tol, SvgPoly *po)
{
	SvgElement e;
	int ok;
	memset(&e, 0, sizeof(e));
	e.type = SVG_EL_PATH;
	e.visible = 1;
	if (!Svg_PathCopy(&e.path, src)) return 0;
	ok = Svg_ElementPoly(&e, tol, po);
	Svg_PathFree(&e.path);
	return ok;
}

static float seg_dist(float px, float py, float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1, dy = y2 - y1;
	float len2 = dx * dx + dy * dy;
	float t, qx, qy;
	if (len2 < 1e-12f) {
		dx = px - x1; dy = py - y1;
		return dx * dx + dy * dy;
	}
	t = ((px - x1) * dx + (py - y1) * dy) / len2;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	qx = x1 + dx * t - px;
	qy = y1 + dy * t - py;
	return qx * qx + qy * qy;
}

/* 点到折线的最小距离 */
static float poly_nearest(const SvgPoly *po, float px, float py)
{
	float best = 1e30f;
	int c, i;
	for (c = 0; c < po->contourCount; c++) {
		int start = po->starts[c];
		int end = (c + 1 < po->contourCount) ? po->starts[c + 1] : po->count;
		for (i = start; i < end; i++) {
			int j = (i + 1 < end) ? i + 1 : (po->closed[c] ? start : i);
			float d;
			if (j == i) break;
			d = seg_dist(px, py, po->pts[i * 2], po->pts[i * 2 + 1],
				po->pts[j * 2], po->pts[j * 2 + 1]);
			if (d < best) best = d;
		}
	}
	if (best >= 1e30f) {
		best = 0.0f;
		for (i = 0; i < po->count; i++) {
			float dx = po->pts[i * 2] - px, dy = po->pts[i * 2 + 1] - py;
			float d = dx * dx + dy * dy;
			if (i == 0 || d < best) best = d;
		}
	}
	return best;
}

/* 一条折线上每个顶点到另一条折线的最大距离 (平方根由调用方判定) */
static float poly_max_dev(const SvgPoly *a, const SvgPoly *b, float *worst_px,
	float *worst_py)
{
	float worst = 0.0f;
	int i;
	for (i = 0; i < a->count; i++) {
		float px = a->pts[i * 2], py = a->pts[i * 2 + 1];
		float d = poly_nearest(b, px, py);
		if (d > worst) {
			worst = d;
			if (worst_px) *worst_px = px;
			if (worst_py) *worst_py = py;
		}
	}
	return worst;
}

static float f_sqrt_test(float x)
{
	float g;
	int i;
	if (x <= 0.0f) return 0.0f;
	g = x > 1.0f ? x : 1.0f;
	for (i = 0; i < 40; i++) g = (g + x / g) * 0.5f;
	return g;
}

static unsigned g_rnd = 0x12345u;

static float rnd_(float lo, float hi)
{
	g_rnd = g_rnd * 1664525u + 1013904223u;
	return lo + (hi - lo) * (float)((g_rnd >> 8) & 0xFFFFFFu) / 16777216.0f;
}

/* ── 用例 ─────────────────────────────────────────────────────────── */

static void test_bounds(void)
{
	SvgDoc *d = SvgDoc_Create(800, 600);
	SvgElement *e;
	GfxRect b;

	CHECK(d != NULL);
	if (!d) return;

	/* 直线 */
	e = SvgDoc_Add(d, SVG_EL_LINE);
	e->p.line.x1 = 40.0f; e->p.line.y1 = 40.0f;
	e->p.line.x2 = 160.0f; e->p.line.y2 = 120.0f;
	b = Svg_ElementLocalBounds(e);
	CHECK(NEAR(b.x, 40.0f, 0.01f) && NEAR(b.y, 40.0f, 0.01f));
	CHECK(NEAR(b.w, 120.0f, 0.01f) && NEAR(b.h, 80.0f, 0.01f));

	/* 旋转矩形: 100x50 转 90 度, 中心 (50,25) */
	e = SvgDoc_Add(d, SVG_EL_RECT);
	e->p.rect.x = 0.0f; e->p.rect.y = 0.0f;
	e->p.rect.w = 100.0f; e->p.rect.h = 50.0f;
	e->rot = 90.0f;
	b = Svg_ElementBounds(e);
	CHECK(NEAR(b.x, 25.0f, 0.02f) && NEAR(b.y, -25.0f, 0.02f));
	CHECK(NEAR(b.w, 50.0f, 0.02f) && NEAR(b.h, 100.0f, 0.02f));

	/* 椭圆 */
	e = SvgDoc_Add(d, SVG_EL_ELLIPSE);
	e->p.ellipse.cx = 100.0f; e->p.ellipse.cy = 80.0f;
	e->p.ellipse.rx = 60.0f; e->p.ellipse.ry = 40.0f;
	b = Svg_ElementLocalBounds(e);
	CHECK(NEAR(b.x, 40.0f, 0.01f) && NEAR(b.y, 40.0f, 0.01f));
	CHECK(NEAR(b.w, 120.0f, 0.01f) && NEAR(b.h, 80.0f, 0.01f));

	/* 三次曲线路径: P0(0,0) C1(0,100) C2(100,100) P3(100,0),
	 * 解析 bbox {0,0,100,75} */
	e = SvgDoc_Add(d, SVG_EL_PATH);
	Svg_PathFree(&e->path);
	Svg_PathInit(&e->path);
	Svg_PathAddNode(&e->path, 0.0f, 0.0f, SVG_NODE_CORNER);
	Svg_PathAddNode(&e->path, 100.0f, 0.0f, SVG_NODE_CORNER);
	e->path.nodes[0].outX = 0.0f; e->path.nodes[0].outY = 100.0f;
	e->path.nodes[0].hasOut = 1;
	e->path.nodes[1].inX = -100.0f; e->path.nodes[1].inY = 100.0f;
	e->path.nodes[1].hasIn = 1;
	e->path.seg[0] = SVG_SEG_CUBIC;
	Svg_ElementTouch(e);
	b = Svg_ElementLocalBounds(e);
	CHECK(NEAR(b.x, 0.0f, 0.02f) && NEAR(b.y, 0.0f, 0.02f));
	CHECK(NEAR(b.w, 100.0f, 0.02f) && NEAR(b.h, 75.0f, 0.02f));

	SvgDoc_Destroy(d);
}

static void test_hit(void)
{
	SvgDoc *d = SvgDoc_Create(800, 600);
	SvgElement *e;
	SvgElement *top;

	CHECK(d != NULL);
	if (!d) return;

	e = SvgDoc_Add(d, SVG_EL_RECT);
	e->p.rect.x = 0.0f; e->p.rect.y = 0.0f;
	e->p.rect.w = 100.0f; e->p.rect.h = 100.0f;
	CHECK(Svg_ElementHit(e, 50.0f, 50.0f, 0.0f) == 1);
	CHECK(Svg_ElementHit(e, 150.0f, 50.0f, 0.0f) == 0);
	/* 描边容差: 边上外 1.5, tol=1.5 + 半宽 1 = 2.5 -> 命中 */
	CHECK(Svg_ElementHit(e, 102.0f, 50.0f, 1.5f) == 1);
	CHECK(Svg_ElementHit(e, 110.0f, 50.0f, 1.5f) == 0);

	/* 旋转命中: 100x20 转 90 度, 世界点 (50,40) 在内部, (80,40) 在外 */
	e = SvgDoc_Add(d, SVG_EL_RECT);
	e->p.rect.x = 0.0f; e->p.rect.y = 0.0f;
	e->p.rect.w = 100.0f; e->p.rect.h = 20.0f;
	e->rot = 90.0f;
	CHECK(Svg_ElementHit(e, 50.0f, 40.0f, 0.0f) == 1);
	CHECK(Svg_ElementHit(e, 80.0f, 40.0f, 0.0f) == 0);

	/* 最上层优先 + locked 跳过 */
	CHECK(Svg_DocHit(d, 50.0f, 10.0f, 0.0f) == 1);
	top = &d->els[1];
	top->locked = 1;
	CHECK(Svg_DocHit(d, 50.0f, 10.0f, 0.0f) == 0);
	top->locked = 0;

	SvgDoc_Destroy(d);
}

static void test_node_insert(void)
{
	SvgPath p, q;
	SvgPoly before, after;
	float dev, worstx = 0.0f, worsty = 0.0f;

	/* 贝塞尔切分 */
	Svg_PathInit(&p);
	Svg_PathAddNode(&p, 0.0f, 0.0f, SVG_NODE_CORNER);
	Svg_PathAddNode(&p, 100.0f, 0.0f, SVG_NODE_CORNER);
	p.nodes[0].outX = 0.0f; p.nodes[0].outY = 100.0f; p.nodes[0].hasOut = 1;
	p.nodes[1].inX = -100.0f; p.nodes[1].inY = 100.0f; p.nodes[1].hasIn = 1;
	p.seg[0] = SVG_SEG_CUBIC;

	CHECK(path_poly(&p, 0.002f, &before) == 1);
	Svg_PathInit(&q);
	CHECK(Svg_PathCopy(&q, &p) == 1);
	CHECK(Svg_PathInsertNode(&q, 0, 0.37f) == 1);
	CHECK(q.count == 3);
	CHECK(q.seg[0] == SVG_SEG_CUBIC && q.seg[1] == SVG_SEG_CUBIC);
	CHECK(path_poly(&q, 0.002f, &after) == 1);
	dev = poly_max_dev(&before, &after, &worstx, &worsty);
	CHECK(f_sqrt_test(dev) < 0.01f);
	dev = poly_max_dev(&after, &before, NULL, NULL);
	CHECK(f_sqrt_test(dev) < 0.01f);
	Svg_PolyFree(&before);
	Svg_PolyFree(&after);
	Svg_PathFree(&q);
	Svg_PathFree(&p);

	/* 圆弧切分 */
	Svg_PathInit(&p);
	Svg_PathAddNode(&p, 0.0f, 0.0f, SVG_NODE_CORNER);
	Svg_PathAddNode(&p, 100.0f, 0.0f, SVG_NODE_CORNER);
	p.seg[0] = SVG_SEG_ARC;
	p.arc[0].rx = 50.0f; p.arc[0].ry = 50.0f;
	p.arc[0].rotDeg = 0.0f; p.arc[0].large = 0; p.arc[0].sweep = 1;

	CHECK(path_poly(&p, 0.002f, &before) == 1);
	Svg_PathInit(&q);
	CHECK(Svg_PathCopy(&q, &p) == 1);
	CHECK(Svg_PathInsertNode(&q, 0, 0.4f) == 1);
	CHECK(q.count == 3);
	CHECK(q.seg[0] == SVG_SEG_ARC && q.seg[1] == SVG_SEG_ARC);
	CHECK(path_poly(&q, 0.002f, &after) == 1);
	dev = poly_max_dev(&before, &after, &worstx, &worsty);
	CHECK(f_sqrt_test(dev) < 0.01f);
	dev = poly_max_dev(&after, &before, NULL, NULL);
	CHECK(f_sqrt_test(dev) < 0.01f);
	Svg_PolyFree(&before);
	Svg_PolyFree(&after);
	Svg_PathFree(&q);
	Svg_PathFree(&p);
}

static void test_arc_solve(void)
{
	SvgArc a;
	float cx, cy, th1, dth;
	SvgElement e;
	SvgPath *pp;
	SvgPoly po;
	float d;
	int ok;

	/* 切向弧: 起点 (0,0) 切线 +x, 终点 (1,1) -> 圆心 (0,1), r=1 */
	memset(&a, 0, sizeof(a));
	CHECK(Svg_ArcFromTangent(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, &a) == 1);
	CHECK(NEAR(a.rx, 1.0f, 1e-3f) && NEAR(a.ry, 1.0f, 1e-3f));
	CHECK(a.sweep == 1 && a.large == 0);
	CHECK(Svg_ArcGeometry(0.0f, 0.0f, 1.0f, 1.0f, &a,
		&cx, &cy, &th1, &dth) == 1);
	CHECK(NEAR(cx, 0.0f, 1e-3f) && NEAR(cy, 1.0f, 1e-3f));

	/* 三点弧: (0,0),(50,50),(100,0) -> 圆心 (50,0), r=50, 过 (50,50) */
	memset(&a, 0, sizeof(a));
	CHECK(Svg_ArcFromPoints(0.0f, 0.0f, 50.0f, 50.0f, 100.0f, 0.0f, &a) == 1);
	CHECK(NEAR(a.rx, 50.0f, 1e-3f) && NEAR(a.ry, 50.0f, 1e-3f));
	CHECK(a.sweep == 0);
	CHECK(Svg_ArcGeometry(0.0f, 0.0f, 100.0f, 0.0f, &a,
		&cx, &cy, &th1, &dth) == 1);
	CHECK(NEAR(cx, 50.0f, 1e-3f) && NEAR(cy, 0.0f, 1e-3f));

	/* 弧过拖动点 (中点) */
	memset(&e, 0, sizeof(e));
	e.type = SVG_EL_PATH;
	e.visible = 1;
	Svg_PathInit(&e.path);
	Svg_PathAddNode(&e.path, 0.0f, 0.0f, SVG_NODE_CORNER);
	Svg_PathAddNode(&e.path, 100.0f, 0.0f, SVG_NODE_CORNER);
	e.path.seg[0] = SVG_SEG_ARC;
	e.path.arc[0] = a;
	Svg_ElementTouch(&e);
	CHECK(Svg_ElementPoly(&e, 0.01f, &po) == 1);
	d = poly_nearest(&po, 50.0f, 50.0f);
	CHECK(f_sqrt_test(d) < 0.02f);
	Svg_PolyFree(&po);
	Svg_PathFree(&e.path);

	/* 大弧: 60 度 -> -60 度, 顺序经过 60 度, |dθ| > π */
	{
		float c60 = 0.5f, s60 = 0.8660254f;
		memset(&a, 0, sizeof(a));
		CHECK(Svg_ArcFromPoints(1.0f, 0.0f, c60, s60, c60, -s60, &a) == 1);
		CHECK(a.large == 1);
		CHECK(NEAR(a.rx, 1.0f, 1e-3f));
	}

	/* 半径过小按 SVG 规范放大: 端点距 2, rx=ry=0.5 -> 实际 r=1 */
	a.rx = 0.5f; a.ry = 0.5f; a.rotDeg = 0.0f;
	a.large = 0; a.sweep = 1;
	CHECK(Svg_ArcGeometry(1.0f, 0.0f, -1.0f, 0.0f, &a,
		&cx, &cy, &th1, &dth) == 1);
	CHECK(NEAR(cx, 0.0f, 1e-3f) && NEAR(cy, 0.0f, 1e-3f));
	CHECK(NEAR(th1, 0.0f, 1e-3f));
	CHECK(NEAR(dth, 3.14159265f, 1e-3f));

	/* 退化: 共线三点 / 终点在切线上 */
	memset(&a, 0, sizeof(a));
	ok = Svg_ArcFromPoints(0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f, &a);
	CHECK(ok == 0);
	ok = Svg_ArcFromTangent(0.0f, 0.0f, 1.0f, 0.0f, 5.0f, 0.0f, &a);
	CHECK(ok == 0);
}

static void test_export_roundtrip(void)
{
	SvgDoc *d = SvgDoc_Create(800, 600);
	SvgElement *e;
	SvgPath p2, back;
	char *svg;
	char dtext[512], dtext2[512];
	char *svg2;
	int ok;

	CHECK(d != NULL);
	if (!d) return;
	d->background = 0xFFFFFFFF;

	/* 闭合路径: L + C + A + 隐式 Z */
	e = SvgDoc_Add(d, SVG_EL_PATH);
	Svg_PathFree(&e->path);
	Svg_PathInit(&e->path);
	Svg_PathAddNode(&e->path, 0.0f, 0.0f, SVG_NODE_CORNER);
	Svg_PathAddNode(&e->path, 100.0f, 0.0f, SVG_NODE_CORNER);
	Svg_PathAddNode(&e->path, 100.0f, 100.0f, SVG_NODE_CORNER);
	e->path.nodes[0].outX = 0.0f; e->path.nodes[0].outY = 50.0f;
	e->path.nodes[0].hasOut = 1;
	e->path.nodes[1].inX = -50.0f; e->path.nodes[1].inY = 0.0f;
	e->path.nodes[1].hasIn = 1;
	e->path.seg[0] = SVG_SEG_CUBIC;
	e->path.seg[1] = SVG_SEG_ARC;
	e->path.arc[1].rx = 60.0f; e->path.arc[1].ry = 30.0f;
	e->path.arc[1].rotDeg = 30.0f;
	e->path.arc[1].large = 0; e->path.arc[1].sweep = 1;
	e->path.closed = 1;
	e->path.seg[2] = SVG_SEG_LINE;
	Svg_ElementTouch(e);

	svg = Svg_Export(d);
	CHECK(svg != NULL);
	if (!svg) { SvgDoc_Destroy(d); return; }
	CHECK(extract_path_d(svg, dtext, (int)sizeof(dtext)) == 1);
	CHECK(find_(dtext, "C ") != NULL);
	CHECK(find_(dtext, "A 60 30 30 0 1 100 100") != NULL);
	CHECK(find_(dtext, "Z") != NULL);
	CHECK(find_(svg, "fill=\"#D9DEE7\"") != NULL);

	/* d -> 解析 -> 属性一致 */
	CHECK(Svg_ImportPathD(dtext, &p2) == 1);
	CHECK(p2.count == 3 && p2.closed == 1);
	CHECK(p2.seg[0] == SVG_SEG_CUBIC);
	CHECK(p2.seg[1] == SVG_SEG_ARC);
	CHECK(p2.seg[2] == SVG_SEG_LINE);
	CHECK(NEAR(p2.arc[1].rx, 60.0f, 0.01f) && NEAR(p2.arc[1].ry, 30.0f, 0.01f));
	CHECK(NEAR(p2.arc[1].rotDeg, 30.0f, 0.01f));
	CHECK(p2.arc[1].large == 0 && p2.arc[1].sweep == 1);
	CHECK(NEAR(p2.nodes[2].x, 100.0f, 0.01f) && NEAR(p2.nodes[2].y, 100.0f, 0.01f));

	/* 再导出 d 文本应逐字节一致 */
	{
		SvgDoc *d2 = SvgDoc_Create(800, 600);
		SvgElement *e2 = SvgDoc_Add(d2, SVG_EL_PATH);
		Svg_PathFree(&e2->path);
		CHECK(Svg_PathCopy(&e2->path, &p2) == 1);
		svg2 = Svg_Export(d2);
		CHECK(svg2 != NULL);
		if (svg2) {
			CHECK(extract_path_d(svg2, dtext2, (int)sizeof(dtext2)) == 1);
			CHECK(streq_(dtext, dtext2));
			Svg_FreeString(svg2);
		}
		SvgDoc_Destroy(d2);
	}
	Svg_PathFree(&p2);

	/* 独立 A 参数 (rx != ry, rot) 往返 */
	Svg_PathInit(&back);
	CHECK(Svg_ImportPathD("M 0 0 A 120 40 30 1 1 100 0", &back) == 1);
	CHECK(back.count == 2 && back.closed == 0);
	CHECK(back.seg[0] == SVG_SEG_ARC);
	CHECK(NEAR(back.arc[0].rx, 120.0f, 0.01f) && NEAR(back.arc[0].ry, 40.0f, 0.01f));
	CHECK(NEAR(back.arc[0].rotDeg, 30.0f, 0.01f));
	CHECK(back.arc[0].large == 1 && back.arc[0].sweep == 1);
	Svg_PathFree(&back);

	ok = Svg_ImportPathD("M 0 0 L 5 5 L 9 9 Z", &back);
	CHECK(ok == 1);
	CHECK(back.closed == 1 && back.count == 3);
	CHECK(back.seg[2] == SVG_SEG_LINE);
	Svg_PathFree(&back);

	Svg_FreeString(svg);
	SvgDoc_Destroy(d);
}

static void test_history(void)
{
	SvgDoc *d = SvgDoc_Create(400, 300);
	char *initial, *final;
	char *now;
	int i, ok = 1;

	CHECK(d != NULL);
	if (!d) return;
	initial = Svg_Export(d);
	CHECK(initial != NULL);

	for (i = 0; i < 20; i++) {
		SvgElement *e = SvgDoc_Add(d, SVG_EL_RECT);
		if (!e) { ok = 0; break; }
		e->p.rect.x = 10.0f * (float)i;
		e->p.rect.y = 5.0f * (float)i;
		e->p.rect.w = 20.0f + (float)i;
		e->p.rect.h = 10.0f;
		Svg_ElementTouch(e);
		Svg_History_Commit(d);
	}
	CHECK(ok && d->count == 20);
	final = Svg_Export(d);
	CHECK(final != NULL);

	for (i = 0; i < 20; i++) CHECK(Svg_History_Undo(d) == 1);
	CHECK(Svg_History_CanUndo(d) == 0);
	CHECK(d->count == 0);
	now = Svg_Export(d);
	CHECK(now != NULL && streq_(now, initial));
	if (now) Svg_FreeString(now);

	for (i = 0; i < 20; i++) CHECK(Svg_History_Redo(d) == 1);
	CHECK(Svg_History_CanRedo(d) == 0);
	CHECK(d->count == 20);
	now = Svg_Export(d);
	CHECK(now != NULL && streq_(now, final));
	if (now) Svg_FreeString(now);

	/* 新操作后 redo 分支被丢弃 */
	CHECK(Svg_History_Undo(d) == 1);
	CHECK(Svg_History_CanRedo(d) == 1);
	{
		SvgElement *e = SvgDoc_Add(d, SVG_EL_ELLIPSE);
		CHECK(e != NULL);
		Svg_History_Commit(d);
	}
	CHECK(Svg_History_CanRedo(d) == 0);

	if (initial) Svg_FreeString(initial);
	if (final) Svg_FreeString(final);
	SvgDoc_Destroy(d);
}

static void test_view(void)
{
	SvgView v;
	int i;
	float maxErr = 0.0f;
	/* 常规缩放 50%..3200%: 往返误差 < 1e-4 世界单位 */
	for (i = 0; i < 1000; i++) {
		float wx = rnd_(-500.0f, 500.0f);
		float wy = rnd_(-500.0f, 500.0f);
		float sx, sy, bx, by, e;
		v.scale = rnd_(0.5f, 32.0f);
		v.tx = rnd_(-100.0f, 100.0f);
		v.ty = rnd_(-100.0f, 100.0f);
		Svg_ViewScreen(&v, wx, wy, &sx, &sy);
		Svg_ViewWorld(&v, sx, sy, &bx, &by);
		e = wx > bx ? wx - bx : bx - wx;
		if (e > maxErr) maxErr = e;
		e = wy > by ? wy - by : by - wy;
		if (e > maxErr) maxErr = e;
	}
	CHECK(maxErr < 1e-4f);

	/* 极限缩小 5%: 屏幕量化误差被 1/scale 放大, 容差放宽到 1e-3 */
	maxErr = 0.0f;
	for (i = 0; i < 1000; i++) {
		float wx = rnd_(-100.0f, 100.0f);
		float wy = rnd_(-100.0f, 100.0f);
		float sx, sy, bx, by, e;
		v.scale = 0.05f;
		v.tx = rnd_(-10.0f, 10.0f);
		v.ty = rnd_(-10.0f, 10.0f);
		Svg_ViewScreen(&v, wx, wy, &sx, &sy);
		Svg_ViewWorld(&v, sx, sy, &bx, &by);
		e = wx > bx ? wx - bx : bx - wx;
		if (e > maxErr) maxErr = e;
		e = wy > by ? wy - by : by - wy;
		if (e > maxErr) maxErr = e;
	}
	CHECK(maxErr < 1e-3f);
}

void __stdcall startup(void)
{
	test_bounds();
	test_hit();
	test_node_insert();
	test_arc_solve();
	test_export_roundtrip();
	test_history();
	test_view();

	{
		char line[160];
		sprintf(line, "svg_test: %d passed, %d failed\n", g_pass, g_fail);
		out_(line);
	}
	ExitProcess(g_fail ? 1u : 0u);
}
