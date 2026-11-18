/*
 * svg_doc.c - SVG 文档模型: 元素/路径/样式/圆弧解算/展平/历史/序列化
 *
 * 纯 C, 只依赖 kernel32 堆与 gfx.h 的 GfxColor/GfxRect 基础类型.
 * 所有三角函数自行实现 (0CRT 的 crtstub 只提供 sqrt/fabs).
 */
#include "svg/svg_doc.h"

#define SVG_PI  3.14159265358979323846f
#define SVG_2PI 6.28318530717958647692f

extern void *memset(void *d, int c, size_t n);
extern void *memcpy(void *d, const void *s, size_t n);

#define SVG_EPS 1e-6f

/* ── 基础数学 ─────────────────────────────────────────────────────── */

static float f_abs(float v) { return v < 0.0f ? -v : v; }
static float f_min(float a, float b) { return a < b ? a : b; }
static float f_max(float a, float b) { return a > b ? a : b; }

static double d_sqrt(double x)
{
	double g;
	int i;
	if (x <= 0.0) return 0.0;
	g = x;
	for (i = 0; i < 64; i++) {
		double ng = (g + x / g) * 0.5;
		if (ng == g) break;
		g = ng;
	}
	return g;
}

static float f_sqrt(float x) { return (float)d_sqrt((double)x); }

/* sin (弧度): 归约到 [-π,π] 后 Taylor. 用 double 累加: 浮点 ulp 误差会在
 * 近直径圆弧的圆心解算里被 sqrt 放大 (1e-7 -> 3e-4), 必须更高精度. */
static float sin_r(float xf)
{
	double x = (double)xf;
	double x2, t, sum;
	int i, k;
	k = (int)(x / 6.28318530717958647692);
	x -= (double)k * 6.28318530717958647692;
	if (x > 3.14159265358979323846) x -= 6.28318530717958647692;
	else if (x < -3.14159265358979323846) x += 6.28318530717958647692;
	x2 = x * x;
	t = x;
	sum = x;
	for (i = 1; i <= 12; i++) {
		t *= -x2 / (double)((2 * i) * (2 * i + 1));
		sum += t;
	}
	return (float)sum;
}

static float cos_r(float x) { return sin_r(x + SVG_PI * 0.5f); }

static float atan_r(float x)
{
	float off = 0.0f, x2, term, sum;
	int i, sign = 1, neg = 0;
	if (x < 0.0f) { neg = 1; x = -x; }
	if (x > 1.0f) {
		float r = atan_r(1.0f / x);
		return neg ? -(SVG_PI * 0.5f - r) : (SVG_PI * 0.5f - r);
	}
	/* |x| <= 1: 用 tan(π/8) 再归约到 |x| <= 0.414, 级数收敛更快 */
	if (x > 0.41421356f) {
		x = (x - 1.0f) / (x + 1.0f);
		off = SVG_PI * 0.25f;
	}
	x2 = x * x;
	term = x;
	sum = 0.0f;
	for (i = 1; i <= 23; i += 2) {
		sum += (float)sign * term / (float)i;
		term *= x2;
		sign = -sign;
	}
	sum += off;
	return neg ? -sum : sum;
}

static float atan2_r(float y, float x)
{
	if (x == 0.0f)
		return y > 0.0f ? SVG_PI * 0.5f : (y < 0.0f ? -SVG_PI * 0.5f : 0.0f);
	if (x > 0.0f) return atan_r(y / x);
	return y >= 0.0f ? atan_r(y / x) + SVG_PI : atan_r(y / x) - SVG_PI;
}

static float deg_r(float d) { return d * (SVG_PI / 180.0f); }
static float rad_d(float r) { return r * (180.0f / SVG_PI); }

/* 归一到 (-π, π] */
static float norm_angle(float a)
{
	while (a > SVG_PI) a -= SVG_2PI;
	while (a <= -SVG_PI) a += SVG_2PI;
	return a;
}

/* ── 小块内存 ─────────────────────────────────────────────────────── */

static void *x_alloc(size_t n)
{
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
}

static void *x_grow(void *old, size_t n)
{
	return old
		? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, old, n)
		: HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n);
}

static void x_free(void *p)
{
	if (p) HeapFree(GetProcessHeap(), 0, p);
}

/* ── 样式 ─────────────────────────────────────────────────────────── */

void Svg_StyleDefault(SvgStyle *s)
{
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->fill.kind = SVG_PAINT_SOLID;
	s->fill.color = 0xFFD9DEE7;
	s->stroke.kind = SVG_PAINT_SOLID;
	s->stroke.color = 0xFF202020;
	s->strokeWidth = 2.0f;
	s->cap = SVG_CAP_ROUND;
	s->join = SVG_JOIN_ROUND;
	s->opacity = 1.0f;
}

static void paint_free(SvgPaint *p)
{
	if (!p) return;
	x_free(p->stops);
	p->stops = NULL;
	p->stopCount = 0;
}

void Svg_StyleFree(SvgStyle *s)
{
	if (!s) return;
	paint_free(&s->fill);
	paint_free(&s->stroke);
}

static int paint_copy(SvgPaint *dst, const SvgPaint *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->kind = src->kind;
	dst->color = src->color;
	dst->angleDeg = src->angleDeg;
	if (src->kind == SVG_PAINT_LINEAR && src->stops && src->stopCount > 0) {
		dst->stops = (SvgGradStop *)x_alloc((size_t)src->stopCount * sizeof(SvgGradStop));
		if (!dst->stops) return 0;
		memcpy(dst->stops, src->stops, (size_t)src->stopCount * sizeof(SvgGradStop));
		dst->stopCount = src->stopCount;
	}
	return 1;
}

int Svg_StyleCopy(SvgStyle *dst, const SvgStyle *src)
{
	if (!dst || !src) return 0;
	memset(dst, 0, sizeof(*dst));
	if (!paint_copy(&dst->fill, &src->fill)) return 0;
	if (!paint_copy(&dst->stroke, &src->stroke)) {
		paint_free(&dst->fill);
		return 0;
	}
	dst->strokeWidth = src->strokeWidth;
	dst->dashCount = src->dashCount;
	memcpy(dst->dash, src->dash, sizeof(dst->dash));
	dst->cap = src->cap;
	dst->join = src->join;
	dst->opacity = src->opacity;
	return 1;
}

int Svg_StyleSetFillStops(SvgPaint *p, const SvgGradStop *stops, int count)
{
	SvgGradStop *n;
	if (!p || !stops || count < 2) return 0;
	if (count > 16) count = 16;
	n = (SvgGradStop *)x_alloc((size_t)count * sizeof(SvgGradStop));
	if (!n) return 0;
	memcpy(n, stops, (size_t)count * sizeof(SvgGradStop));
	x_free(p->stops);
	p->stops = n;
	p->stopCount = count;
	return 1;
}

/* ── 路径 ─────────────────────────────────────────────────────────── */

void Svg_PathInit(SvgPath *p)
{
	if (p) memset(p, 0, sizeof(*p));
}

void Svg_PathFree(SvgPath *p)
{
	if (!p) return;
	x_free(p->nodes);
	x_free(p->seg);
	x_free(p->arc);
	memset(p, 0, sizeof(*p));
}

static int path_seg_count(const SvgPath *p)
{
	if (!p || p->count <= 0) return 0;
	if (p->closed) return p->count;
	return p->count - 1;
}

static int path_reserve_nodes(SvgPath *p, int want)
{
	int cap;
	SvgNode *nn;
	SvgSegType *ns;
	SvgArc *na;
	if (want <= p->cap) return 1;
	cap = p->cap ? p->cap * 2 : 16;
	if (cap < want) cap = want;
	nn = (SvgNode *)x_grow(p->nodes, (size_t)cap * sizeof(SvgNode));
	if (!nn) return 0;
	p->nodes = nn;
	ns = (SvgSegType *)x_grow(p->seg, (size_t)cap * sizeof(SvgSegType));
	if (!ns) return 0;
	p->seg = ns;
	na = (SvgArc *)x_grow(p->arc, (size_t)cap * sizeof(SvgArc));
	if (!na) return 0;
	p->arc = na;
	p->cap = cap;
	return 1;
}

int Svg_PathCopy(SvgPath *dst, const SvgPath *src)
{
	if (!dst || !src) return 0;
	Svg_PathInit(dst);
	if (src->count > 0) {
		int n = src->count;
		int sc = path_seg_count(src);
		if (!path_reserve_nodes(dst, n)) return 0;
		memcpy(dst->nodes, src->nodes, (size_t)n * sizeof(SvgNode));
		if (sc > 0) {
			memcpy(dst->seg, src->seg, (size_t)sc * sizeof(SvgSegType));
			memcpy(dst->arc, src->arc, (size_t)sc * sizeof(SvgArc));
		}
		dst->count = n;
		dst->closed = src->closed;
	}
	return 1;
}

int Svg_PathAddNode(SvgPath *p, float x, float y, SvgNodeType type)
{
	int oldCount, idx;
	if (!p) return -1;
	if (p->count >= SVG_MAX_PATH_NODES) return -1;
	if (!path_reserve_nodes(p, p->count + 1)) return -1;
	oldCount = p->count;
	idx = oldCount;
	memset(&p->nodes[idx], 0, sizeof(SvgNode));
	p->nodes[idx].x = x;
	p->nodes[idx].y = y;
	p->nodes[idx].type = (uint8_t)type;
	p->count = oldCount + 1;
	if (oldCount > 0) {
		if (p->closed) {
			/* 旧闭合段 (末节点 -> 0) 保持原样, 现在指向新节点;
			 * 新节点 -> 0 追加为最后一段. */
			int si = oldCount;
			p->seg[si] = SVG_SEG_LINE;
			memset(&p->arc[si], 0, sizeof(SvgArc));
		} else {
			int si = oldCount - 1;
			p->seg[si] = SVG_SEG_LINE;
			memset(&p->arc[si], 0, sizeof(SvgArc));
		}
	}
	return idx;
}

void Svg_PathSetSeg(SvgPath *p, int segIndex, SvgSegType type)
{
	int sc, j;
	SvgNode *n0, *n1;
	float dx, dy;
	if (!p) return;
	sc = path_seg_count(p);
	if (segIndex < 0 || segIndex >= sc) return;
	j = (segIndex + 1) % p->count;
	n0 = &p->nodes[segIndex];
	n1 = &p->nodes[j];
	p->seg[segIndex] = type;
	if (type == SVG_SEG_CUBIC) {
		dx = n1->x - n0->x;
		dy = n1->y - n0->y;
		if (!n0->hasOut) {
			n0->outX = dx / 3.0f; n0->outY = dy / 3.0f; n0->hasOut = 1;
		}
		if (!n1->hasIn) {
			n1->inX = -dx / 3.0f; n1->inY = -dy / 3.0f; n1->hasIn = 1;
		}
	} else if (type == SVG_SEG_ARC) {
		SvgArc *a = &p->arc[segIndex];
		dx = n1->x - n0->x;
		dy = n1->y - n0->y;
		a->rx = f_sqrt(dx * dx + dy * dy) * 0.5f;
		a->ry = a->rx;
		a->rotDeg = 0.0f;
		a->large = 0;
		a->sweep = 1;
	}
}

/* 在数组 at 位置插入空位 (节点/段/弧同步); sc = 插入前的段数 */
static void path_insert_slot(SvgPath *p, int at, int sc)
{
	int i;
	for (i = p->count; i > at; i--) p->nodes[i] = p->nodes[i - 1];
	for (i = sc; i > at; i--) {
		p->seg[i] = p->seg[i - 1];
		p->arc[i] = p->arc[i - 1];
	}
	p->count++;
}

int Svg_PathInsertNode(SvgPath *p, int segIndex, float t)
{
	int sc, j, at, haveArc = 0;
	SvgNode *n0, *n1, mid;
	SvgSegType type;
	SvgArc arcFirst, arcSecond;
	if (!p) return -1;
	sc = path_seg_count(p);
	if (segIndex < 0 || segIndex >= sc) return -1;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	j = (segIndex + 1) % p->count;
	n0 = &p->nodes[segIndex];
	n1 = &p->nodes[j];
	type = p->seg[segIndex];
	memset(&arcFirst, 0, sizeof(arcFirst));
	memset(&arcSecond, 0, sizeof(arcSecond));

	memset(&mid, 0, sizeof(mid));
	mid.type = SVG_NODE_CORNER;

	if (type == SVG_SEG_LINE) {
		mid.x = n0->x + (n1->x - n0->x) * t;
		mid.y = n0->y + (n1->y - n0->y) * t;
	} else if (type == SVG_SEG_CUBIC) {
		float p0x = n0->x, p0y = n0->y;
		float c1x = n0->x + n0->outX, c1y = n0->y + n0->outY;
		float c2x = n1->x + n1->inX, c2y = n1->y + n1->inY;
		float p1x = n1->x, p1y = n1->y;
		float p01x, p01y, p12x, p12y, p23x, p23y;
		float p012x, p012y, p123x, p123y, mx, my;
		p01x = p0x + (c1x - p0x) * t; p01y = p0y + (c1y - p0y) * t;
		p12x = c1x + (c2x - c1x) * t; p12y = c1y + (c2y - c1y) * t;
		p23x = c2x + (p1x - c2x) * t; p23y = c2y + (p1y - c2y) * t;
		p012x = p01x + (p12x - p01x) * t; p012y = p01y + (p12y - p01y) * t;
		p123x = p12x + (p23x - p12x) * t; p123y = p12y + (p23y - p12y) * t;
		mx = p012x + (p123x - p012x) * t; my = p012y + (p123y - p012y) * t;
		mid.x = mx; mid.y = my;
		/* 左段 P0,P01,P012,M; 右段 M,P123,P23,P1 */
		mid.inX = p012x - mx; mid.inY = p012y - my; mid.hasIn = 1;
		mid.outX = p123x - mx; mid.outY = p123y - my; mid.hasOut = 1;
		mid.type = n0->type == SVG_NODE_SMOOTH ? SVG_NODE_SMOOTH : SVG_NODE_CORNER;
		n0->outX = p01x - p0x; n0->outY = p01y - p0y; n0->hasOut = 1;
		n1->inX = p23x - p1x; n1->inY = p23y - p1y; n1->hasIn = 1;
	} else {
		/* ARC: 在参数 t 处切分, 两段共用圆心/半径/旋转 */
		float cx, cy, th1, dth, thm, xm, ym;
		SvgArc old;
		if (!Svg_ArcGeometry(n0->x, n0->y, n1->x, n1->y,
			&p->arc[segIndex], &cx, &cy, &th1, &dth)) {
			mid.x = n0->x + (n1->x - n0->x) * t;
			mid.y = n0->y + (n1->y - n0->y) * t;
			type = SVG_SEG_LINE;
		} else {
			float phi = deg_r(p->arc[segIndex].rotDeg);
			float cph = cos_r(phi), sph = sin_r(phi);
			float rx = p->arc[segIndex].rx, ry = p->arc[segIndex].ry;
			float ct, st;
			old = p->arc[segIndex];
			thm = th1 + dth * t;
			ct = cos_r(thm); st = sin_r(thm);
			xm = cx + cph * rx * ct - sph * ry * st;
			ym = cy + sph * rx * ct + cph * ry * st;
			mid.x = xm; mid.y = ym;
			arcFirst = old; arcSecond = old;
			arcFirst.large = (f_abs(dth * t) > SVG_PI) ? 1 : 0;
			arcSecond.large = (f_abs(dth * (1.0f - t)) > SVG_PI) ? 1 : 0;
			haveArc = 1;
		}
	}

	at = segIndex + 1;
	if (!path_reserve_nodes(p, p->count + 1)) return -1;
	path_insert_slot(p, at, sc);
	p->nodes[at] = mid;
	p->seg[segIndex] = type;
	p->seg[at] = type;
	if (type == SVG_SEG_ARC && haveArc) {
		p->arc[segIndex] = arcFirst;
		p->arc[at] = arcSecond;
	} else {
		memset(&p->arc[segIndex], 0, sizeof(SvgArc));
		memset(&p->arc[at], 0, sizeof(SvgArc));
	}
	return at;
}

void Svg_PathRemoveNode(SvgPath *p, int nodeIndex)
{
	int sc, i;
	if (!p || nodeIndex < 0 || nodeIndex >= p->count) return;
	if (p->count <= 2) {
		Svg_PathFree(p);
		return;
	}
	sc = path_seg_count(p);
	/* 删除节点 */
	for (i = nodeIndex; i < p->count - 1; i++) p->nodes[i] = p->nodes[i + 1];
	/* 删除一个段: 优先删从该节点出发的段; 末节点删前一段 */
	{
		int rm = nodeIndex < sc ? nodeIndex : sc - 1;
		for (i = rm; i < sc - 1; i++) {
			p->seg[i] = p->seg[i + 1];
			p->arc[i] = p->arc[i + 1];
		}
	}
	p->count--;
	if (p->closed && p->count < 3) p->closed = 0;
}

/* ── 圆弧 ─────────────────────────────────────────────────────────── */

void Svg_ElementCenter(const SvgElement *e, float *cx, float *cy)
{
	GfxRect b;
	if (!e) { if (cx) *cx = 0; if (cy) *cy = 0; return; }
	b = Svg_ElementLocalBounds(e);
	if (cx) *cx = b.x + b.w * 0.5f;
	if (cy) *cy = b.y + b.h * 0.5f;
}

void Svg_WorldToLocal(const SvgElement *e, float wx, float wy,
	float *lx, float *ly)
{
	float cx, cy, a, c, s, dx, dy;
	if (!e || e->rot == 0.0f) {
		if (lx) *lx = wx;
		if (ly) *ly = wy;
		return;
	}
	Svg_ElementCenter(e, &cx, &cy);
	a = deg_r(-e->rot);
	c = cos_r(a); s = sin_r(a);
	dx = wx - cx; dy = wy - cy;
	if (lx) *lx = cx + dx * c - dy * s;
	if (ly) *ly = cy + dx * s + dy * c;
}

void Svg_LocalToWorld(const SvgElement *e, float lx, float ly,
	float *wx, float *wy)
{
	float cx, cy, a, c, s, dx, dy;
	if (!e || e->rot == 0.0f) {
		if (wx) *wx = lx;
		if (wy) *wy = ly;
		return;
	}
	Svg_ElementCenter(e, &cx, &cy);
	a = deg_r(e->rot);
	c = cos_r(a); s = sin_r(a);
	dx = lx - cx; dy = ly - cy;
	if (wx) *wx = cx + dx * c - dy * s;
	if (wy) *wy = cy + dx * s + dy * c;
}

int Svg_ArcFromPoints(float x0, float y0, float xm, float ym,
	float x1, float y1, SvgArc *out)
{
	double d, ux, uy, r;
	float s0x, s0y, s1x, s1y;
	float a;
	if (!out) return 0;
	d = 2.0 * ((double)x0 * ((double)ym - y1) + (double)xm * ((double)y1 - y0)
		+ (double)x1 * ((double)y0 - ym));
	if (f_abs((float)d) < 1e-4f) return 0;
	{
		double x0s = (double)x0 * x0 + (double)y0 * y0;
		double xms = (double)xm * xm + (double)ym * ym;
		double x1s = (double)x1 * x1 + (double)y1 * y1;
		ux = (x0s * (ym - y1) + xms * (y1 - y0) + x1s * (y0 - ym)) / d;
		uy = (x0s * (x1 - xm) + xms * (x0 - x1) + x1s * (xm - x0)) / d;
	}
	r = d_sqrt(((double)x0 - ux) * ((double)x0 - ux)
		+ ((double)y0 - uy) * ((double)y0 - uy));
	if (r < 1e-4) return 0;

	/* 方向: 三点序的叉积 (y 向下, 正 = 屏幕顺时针) */
	s0x = xm - x0; s0y = ym - y0;
	s1x = x1 - xm; s1y = y1 - ym;
	a = s0x * s1y - s0y * s1x;

	{
		float t0 = atan2_r(y0 - (float)uy, x0 - (float)ux);
		float t1 = atan2_r(y1 - (float)uy, x1 - (float)ux);
		float dt = norm_angle(t1 - t0);
		if (a < 0.0f && dt > 0.0f) dt -= SVG_2PI;
		if (a > 0.0f && dt < 0.0f) dt += SVG_2PI;
		out->rx = (float)r;
		out->ry = (float)r;
		out->rotDeg = 0.0f;
		out->sweep = a > 0.0f ? 1 : 0;
		out->large = f_abs(dt) > SVG_PI ? 1 : 0;
	}
	return 1;
}

int Svg_ArcFromTangent(float x0, float y0, float tx, float ty,
	float x1, float y1, SvgArc *out)
{
	float dx = x1 - x0, dy = y1 - y0;
	float len = f_sqrt(tx * tx + ty * ty);
	float nx, ny, den, u, cx, cy, r;
	float t0, t1, dt, cross;
	if (!out || len < 1e-6f) return 0;
	tx /= len; ty /= len;
	/* 端点与切线共线 (沿切线或反向): 退化为直线 */
	cross = tx * dy - ty * dx;
	if (f_abs(cross) < 1e-5f) return 0;
	nx = -ty; ny = tx;
	den = 2.0f * (nx * dx + ny * dy);
	if (f_abs(den) < 1e-6f) return 0;
	u = (dx * dx + dy * dy) / den;
	cx = x0 + nx * u;
	cy = y0 + ny * u;
	r = f_abs(u);
	if (r < 1e-5f) return 0;

	t0 = atan2_r(y0 - cy, x0 - cx);
	t1 = atan2_r(y1 - cy, x1 - cx);
	dt = norm_angle(t1 - t0);
	cross = (x0 - cx) * (y1 - cy) - (y0 - cy) * (x1 - cx);
	if (cross > 0.0f) {
		if (dt < 0.0f) dt += SVG_2PI;
	} else {
		if (dt > 0.0f) dt -= SVG_2PI;
	}
	out->rx = r;
	out->ry = r;
	out->rotDeg = 0.0f;
	out->sweep = cross > 0.0f ? 1 : 0;
	out->large = f_abs(dt) > SVG_PI ? 1 : 0;
	return 1;
}

int Svg_ArcGeometry(float x0, float y0, float x1, float y1, const SvgArc *a,
	float *cx, float *cy, float *theta1, float *dtheta)
{
	float rx, ry, phi, cph, sph;
	float dx2, dy2, xp, yp, lam, coef, cxp, cyp;
	float num, den, sign;
	float ux, uy, vx, vy, dt, cross, dot;
	if (!a) return 0;
	rx = f_abs(a->rx); ry = f_abs(a->ry);
	if (rx < 1e-6f || ry < 1e-6f) return 0;
	if (f_abs(x1 - x0) < 1e-7f && f_abs(y1 - y0) < 1e-7f) return 0;

	phi = deg_r(a->rotDeg);
	cph = cos_r(phi); sph = sin_r(phi);
	dx2 = (x0 - x1) * 0.5f;
	dy2 = (y0 - y1) * 0.5f;
	xp = cph * dx2 + sph * dy2;
	yp = -sph * dx2 + cph * dy2;

	lam = (xp * xp) / (rx * rx) + (yp * yp) / (ry * ry);
	if (lam > 1.0f) {
		float sl = f_sqrt(lam);
		rx *= sl; ry *= sl;
	}
	num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp;
	den = rx * rx * yp * yp + ry * ry * xp * xp;
	if (den < 1e-12f) return 0;
	coef = f_sqrt(f_max(0.0f, num / den));
	sign = (a->large != a->sweep) ? 1.0f : -1.0f;
	coef *= sign;
	cxp = coef * (rx * yp / ry);
	cyp = coef * (-ry * xp / rx);
	if (cx) *cx = cph * cxp - sph * cyp + (x0 + x1) * 0.5f;
	if (cy) *cy = sph * cxp + cph * cyp + (y0 + y1) * 0.5f;

	ux = (xp - cxp) / rx; uy = (yp - cyp) / ry;
	vx = (-xp - cxp) / rx; vy = (-yp - cyp) / ry;
	dot = ux * vx + uy * vy;
	cross = ux * vy - uy * vx;
	dt = atan2_r(cross, dot);
	if (!a->sweep && dt > 0.0f) dt -= SVG_2PI;
	else if (a->sweep && dt < 0.0f) dt += SVG_2PI;
	if (theta1) *theta1 = atan2_r(uy, ux);
	if (dtheta) *dtheta = dt;
	return 1;
}

/* 角度是否在 [θ1, θ1+dθ] 内 (dθ 带符号) */
static int arc_angle_in(float th, float th1, float dth)
{
	float rel = norm_angle(th - th1);
	if (dth >= 0.0f) {
		if (rel < 0.0f) rel += SVG_2PI;
		return rel <= dth + 1e-5f;
	}
	if (rel > 0.0f) rel -= SVG_2PI;
	return rel >= dth - 1e-5f;
}

static void arc_point(float cx, float cy, float rx, float ry, float phi,
	float th, float *x, float *y)
{
	float cph = cos_r(phi), sph = sin_r(phi);
	float ct = cos_r(th), st = sin_r(th);
	*x = cx + cph * rx * ct - sph * ry * st;
	*y = cy + sph * rx * ct + cph * ry * st;
}

GfxRect Svg_ArcBounds(float x0, float y0, float x1, float y1, const SvgArc *a)
{
	GfxRect out = { 0, 0, 0, 0 };
	float cx, cy, th1, dth, phi, rx, ry;
	float xs[6], ys[6];
	int n = 0, i;
	if (!Svg_ArcGeometry(x0, y0, x1, y1, a, &cx, &cy, &th1, &dth)) {
		out.x = f_min(x0, x1); out.y = f_min(y0, y1);
		out.w = f_abs(x1 - x0); out.h = f_abs(y1 - y0);
		return out;
	}
	phi = deg_r(a->rotDeg);
	rx = f_abs(a->rx); ry = f_abs(a->ry);
	xs[n] = x0; ys[n] = y0; n++;
	xs[n] = x1; ys[n] = y1; n++;
	{
		/* x 方向极值: tanθ = -ry sinφ / (rx cosφ); y: tanθ = ry cosφ / (rx sinφ) */
		float tx = atan2_r(-ry * sin_r(phi), rx * cos_r(phi));
		float ty = atan2_r(ry * cos_r(phi), rx * sin_r(phi));
		float cand[4];
		cand[0] = tx; cand[1] = tx + SVG_PI;
		cand[2] = ty; cand[3] = ty + SVG_PI;
		for (i = 0; i < 4; i++) {
			if (arc_angle_in(cand[i], th1, dth)) {
				float px, py;
				arc_point(cx, cy, rx, ry, phi, cand[i], &px, &py);
				xs[n] = px; ys[n] = py; n++;
			}
		}
	}
	out.x = xs[0]; out.y = ys[0];
	{
		float maxx = xs[0], maxy = ys[0];
		for (i = 1; i < n; i++) {
			if (xs[i] < out.x) out.x = xs[i];
			if (ys[i] < out.y) out.y = ys[i];
			if (xs[i] > maxx) maxx = xs[i];
			if (ys[i] > maxy) maxy = ys[i];
		}
		out.w = maxx - out.x;
		out.h = maxy - out.y;
	}
	return out;
}

/* ── 元素包围盒 / 展平 / 命中 ─────────────────────────────────────── */

void Svg_PolyInit(SvgPoly *po)
{
	if (po) memset(po, 0, sizeof(*po));
}

void Svg_PolyFree(SvgPoly *po)
{
	if (!po) return;
	x_free(po->pts);
	x_free(po->starts);
	x_free(po->closed);
	memset(po, 0, sizeof(*po));
}

static int poly_reserve_pts(SvgPoly *po, int want)
{
	int cap;
	float *np;
	if (want <= po->cap) return 1;
	cap = po->cap ? po->cap * 2 : 64;
	if (cap < want) cap = want;
	np = (float *)x_grow(po->pts, (size_t)cap * 2 * sizeof(float));
	if (!np) return 0;
	po->pts = np;
	po->cap = cap;
	return 1;
}

static int poly_reserve_contours(SvgPoly *po, int want)
{
	int cap;
	int *ns;
	uint8_t *nc;
	if (want <= po->contourCap) return 1;
	cap = po->contourCap ? po->contourCap * 2 : 8;
	if (cap < want) cap = want;
	ns = (int *)x_grow(po->starts, (size_t)cap * sizeof(int));
	if (!ns) return 0;
	po->starts = ns;
	nc = (uint8_t *)x_grow(po->closed, (size_t)cap * sizeof(uint8_t));
	if (!nc) return 0;
	po->closed = nc;
	po->contourCap = cap;
	return 1;
}

typedef struct {
	SvgPoly *po;
	float cx, cy, cosr, sinr;
	int curStart;
	int curClosed;
} PolyBuild;

static int pb_contour(PolyBuild *b, int closed)
{
	SvgPoly *po = b->po;
	if (!poly_reserve_contours(po, po->contourCount + 1)) return 0;
	po->starts[po->contourCount] = po->count;
	po->closed[po->contourCount] = (uint8_t)(closed ? 1 : 0);
	po->contourCount++;
	b->curStart = po->count;
	b->curClosed = closed;
	return 1;
}

static int pb_pt(PolyBuild *b, float lx, float ly)
{
	SvgPoly *po = b->po;
	float dx = lx - b->cx, dy = ly - b->cy;
	if (!poly_reserve_pts(po, po->count + 1)) return 0;
	po->pts[po->count * 2] = b->cx + dx * b->cosr - dy * b->sinr;
	po->pts[po->count * 2 + 1] = b->cy + dx * b->sinr + dy * b->cosr;
	po->count++;
	return 1;
}

static int pb_last(PolyBuild *b, float *lx, float *ly)
{
	SvgPoly *po = b->po;
	float wx, wy, dx, dy;
	if (po->count <= b->curStart) return 0;
	wx = po->pts[(po->count - 1) * 2];
	wy = po->pts[(po->count - 1) * 2 + 1];
	dx = wx - b->cx; dy = wy - b->cy;
	*lx = b->cx + dx * b->cosr + dy * b->sinr;
	*ly = b->cy - dx * b->sinr + dy * b->cosr;
	return 1;
}

/* 三次贝塞尔自适应展平 (局部坐标) */
static void flatten_cubic(PolyBuild *b, float x0, float y0, float x1, float y1,
	float x2, float y2, float x3, float y3, float tol, int depth)
{
	float d1, d2, dx, dy, len2;
	if (depth >= 16) {
		pb_pt(b, x3, y3);
		return;
	}
	dx = x3 - x0; dy = y3 - y0;
	len2 = dx * dx + dy * dy;
	if (len2 < 1e-12f) {
		pb_pt(b, x3, y3);
		return;
	}
	/* 控制点到弦的距离估计 */
	d1 = f_abs((x1 - x3) * dy - (y1 - y3) * dx);
	d2 = f_abs((x2 - x3) * dy - (y2 - y3) * dx);
	if ((d1 + d2) * (d1 + d2) <= tol * tol * len2 * 16.0f) {
		pb_pt(b, x3, y3);
		return;
	}
	{
		float a1x = (x0 + x1) * 0.5f, a1y = (y0 + y1) * 0.5f;
		float a2x = (x1 + x2) * 0.5f, a2y = (y1 + y2) * 0.5f;
		float a3x = (x2 + x3) * 0.5f, a3y = (y2 + y3) * 0.5f;
		float b1x = (a1x + a2x) * 0.5f, b1y = (a1y + a2y) * 0.5f;
		float b2x = (a2x + a3x) * 0.5f, b2y = (a2y + a3y) * 0.5f;
		float mx = (b1x + b2x) * 0.5f, my = (b1y + b2y) * 0.5f;
		flatten_cubic(b, x0, y0, a1x, a1y, b1x, b1y, mx, my, tol, depth + 1);
		flatten_cubic(b, mx, my, b2x, b2y, a3x, a3y, x3, y3, tol, depth + 1);
	}
}

static void flatten_arc(PolyBuild *b, float x0, float y0, float x1, float y1,
	const SvgArc *a, float tol)
{
	float cx, cy, th1, dth, phi, rx, ry, rmax, absd;
	int steps, i;
	if (!Svg_ArcGeometry(x0, y0, x1, y1, a, &cx, &cy, &th1, &dth)) {
		pb_pt(b, x1, y1);
		return;
	}
	phi = deg_r(a->rotDeg);
	rx = f_abs(a->rx); ry = f_abs(a->ry);
	rmax = f_max(rx, ry);
	absd = f_abs(dth);
	if (tol <= 0.01f) tol = 0.01f;
	if (rmax <= tol) {
		steps = 2;
	} else {
		/* sagitta ≈ r*(1-cos(step/2)) < tol  =>  step < 2*acos(1 - tol/r) */
		float c = 1.0f - tol / rmax;
		float half = atan2_r(f_sqrt(f_max(0.0f, 1.0f - c * c)), c);
		if (half < 0.01f) half = 0.01f;
		steps = (int)(absd / (half * 2.0f)) + 1;
	}
	if (steps < 2) steps = 2;
	if (steps > 256) steps = 256;
	for (i = 1; i <= steps; i++) {
		float th = th1 + dth * ((float)i / (float)steps);
		float px, py;
		if (i == steps) {
			pb_pt(b, x1, y1);
			break;
		}
		arc_point(cx, cy, rx, ry, phi, th, &px, &py);
		pb_pt(b, px, py);
	}
}

static int flatten_path(PolyBuild *b, const SvgPath *p, float tol)
{
	int i, sc;
	if (!p || p->count <= 0) return 1;
	sc = path_seg_count(p);
	if (!pb_contour(b, p->closed)) return 0;
	if (!pb_pt(b, p->nodes[0].x, p->nodes[0].y)) return 0;
	for (i = 0; i < sc; i++) {
		int j = (i + 1) % p->count;
		const SvgNode *n0 = &p->nodes[i];
		const SvgNode *n1 = &p->nodes[j];
		SvgSegType t = p->seg[i];
		if (t == SVG_SEG_LINE) {
			if (!pb_pt(b, n1->x, n1->y)) return 0;
		} else if (t == SVG_SEG_CUBIC) {
			float c1x = n0->x + (n0->hasOut ? n0->outX : 0.0f);
			float c1y = n0->y + (n0->hasOut ? n0->outY : 0.0f);
			float c2x = n1->x + (n1->hasIn ? n1->inX : 0.0f);
			float c2y = n1->y + (n1->hasIn ? n1->inY : 0.0f);
			flatten_cubic(b, n0->x, n0->y, c1x, c1y, c2x, c2y,
				n1->x, n1->y, tol, 0);
		} else {
			flatten_arc(b, n0->x, n0->y, n1->x, n1->y, &p->arc[i], tol);
		}
	}
	return 1;
}

/* 椭圆用 4 段三次贝塞尔近似 */
static int flatten_ellipse(PolyBuild *b, const SvgElement *e, float tol)
{
	const float k = 0.5522847498f;
	float cx = e->p.ellipse.cx, cy = e->p.ellipse.cy;
	float rx = f_abs(e->p.ellipse.rx), ry = f_abs(e->p.ellipse.ry);
	float kx = rx * k, ky = ry * k;
	if (rx < 1e-6f || ry < 1e-6f) return 1;
	if (!pb_contour(b, 1)) return 0;
	if (!pb_pt(b, cx + rx, cy)) return 0;
	flatten_cubic(b, cx + rx, cy, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry, tol, 0);
	flatten_cubic(b, cx, cy + ry, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy, tol, 0);
	flatten_cubic(b, cx - rx, cy, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry, tol, 0);
	flatten_cubic(b, cx, cy - ry, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy, tol, 0);
	return 1;
}

/* 展平到元素局部坐标 (不含元素旋转) */
static int element_flatten_raw(const SvgElement *e, float tol, SvgPoly *out)
{
	PolyBuild b;
	if (!e || !out) return 0;
	Svg_PolyInit(out);
	if (tol <= 0.0f) tol = 0.25f;
	memset(&b, 0, sizeof(b));
	b.po = out;
	b.cx = 0.0f; b.cy = 0.0f;
	b.cosr = 1.0f; b.sinr = 0.0f;

	switch (e->type) {
	case SVG_EL_RECT: {
		float x = e->p.rect.x, y = e->p.rect.y;
		float w = e->p.rect.w, h = e->p.rect.h;
		if (w < 0.0f) { x += w; w = -w; }
		if (h < 0.0f) { y += h; h = -h; }
		if (!pb_contour(&b, 1)) return 0;
		if (!pb_pt(&b, x, y)) return 0;
		if (!pb_pt(&b, x + w, y)) return 0;
		if (!pb_pt(&b, x + w, y + h)) return 0;
		if (!pb_pt(&b, x, y + h)) return 0;
		break;
	}
	case SVG_EL_ELLIPSE:
		return flatten_ellipse(&b, e, tol);
	case SVG_EL_LINE:
		if (!pb_contour(&b, 0)) return 0;
		if (!pb_pt(&b, e->p.line.x1, e->p.line.y1)) return 0;
		if (!pb_pt(&b, e->p.line.x2, e->p.line.y2)) return 0;
		break;
	default:
		return flatten_path(&b, &e->path, tol);
	}
	return 1;
}

int Svg_ElementPoly(const SvgElement *e, float tol, SvgPoly *out)
{
	float cx, cy, a, c, s;
	int i;
	if (!e || !out) return 0;
	if (!element_flatten_raw(e, tol, out)) return 0;
	if (e->rot == 0.0f) return 1;
	Svg_ElementCenter(e, &cx, &cy);
	a = deg_r(e->rot);
	c = cos_r(a); s = sin_r(a);
	for (i = 0; i < out->count; i++) {
		float dx = out->pts[i * 2] - cx;
		float dy = out->pts[i * 2 + 1] - cy;
		out->pts[i * 2] = cx + dx * c - dy * s;
		out->pts[i * 2 + 1] = cy + dx * s + dy * c;
	}
	return 1;
}

GfxRect Svg_ElementLocalBounds(const SvgElement *e)
{
	GfxRect r = { 0, 0, 0, 0 };
	if (!e) return r;
	switch (e->type) {
	case SVG_EL_RECT: {
		float x = e->p.rect.x, y = e->p.rect.y;
		float w = e->p.rect.w, h = e->p.rect.h;
		if (w < 0.0f) { x += w; w = -w; }
		if (h < 0.0f) { y += h; h = -h; }
		r.x = x; r.y = y; r.w = w; r.h = h;
		break;
	}
	case SVG_EL_ELLIPSE: {
		float rx = f_abs(e->p.ellipse.rx), ry = f_abs(e->p.ellipse.ry);
		r.x = e->p.ellipse.cx - rx;
		r.y = e->p.ellipse.cy - ry;
		r.w = rx * 2.0f;
		r.h = ry * 2.0f;
		break;
	}
	case SVG_EL_LINE:
		r.x = f_min(e->p.line.x1, e->p.line.x2);
		r.y = f_min(e->p.line.y1, e->p.line.y2);
		r.w = f_abs(e->p.line.x2 - e->p.line.x1);
		r.h = f_abs(e->p.line.y2 - e->p.line.y1);
		break;
	default: {
		SvgPoly po;
		int i;
		if (!element_flatten_raw(e, 0.1f, &po)) return r;
		if (po.count <= 0) { Svg_PolyFree(&po); return r; }
		r.x = r.w = po.pts[0];
		r.y = r.h = po.pts[1];
		{
			float maxx = r.x, maxy = r.y;
			for (i = 1; i < po.count; i++) {
				float x = po.pts[i * 2], y = po.pts[i * 2 + 1];
				if (x < r.x) r.x = x;
				if (y < r.y) r.y = y;
				if (x > maxx) maxx = x;
				if (y > maxy) maxy = y;
			}
			r.w = maxx - r.x;
			r.h = maxy - r.y;
		}
		Svg_PolyFree(&po);
		break;
	}
	}
	return r;
}

GfxRect Svg_ElementBounds(const SvgElement *e)
{
	GfxRect lb, out;
	float cx, cy, a, c, s, xs[4], ys[4], maxx, maxy;
	int i;
	if (!e) { out.x = out.y = out.w = out.h = 0.0f; return out; }
	lb = Svg_ElementLocalBounds(e);
	if (e->rot == 0.0f) return lb;
	Svg_ElementCenter(e, &cx, &cy);
	a = deg_r(e->rot);
	c = cos_r(a); s = sin_r(a);
	xs[0] = lb.x; ys[0] = lb.y;
	xs[1] = lb.x + lb.w; ys[1] = lb.y;
	xs[2] = lb.x + lb.w; ys[2] = lb.y + lb.h;
	xs[3] = lb.x; ys[3] = lb.y + lb.h;
	out.x = out.y = maxx = maxy = 0.0f;
	for (i = 0; i < 4; i++) {
		float dx = xs[i] - cx, dy = ys[i] - cy;
		float wx = cx + dx * c - dy * s;
		float wy = cy + dx * s + dy * c;
		if (i == 0) {
			out.x = maxx = wx;
			out.y = maxy = wy;
			continue;
		}
		if (wx < out.x) out.x = wx;
		if (wy < out.y) out.y = wy;
		if (wx > maxx) maxx = wx;
		if (wy > maxy) maxy = wy;
	}
	out.w = maxx - out.x;
	out.h = maxy - out.y;
	return out;
}

static float pt_seg_dist(float px, float py, float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1, dy = y2 - y1;
	float len2 = dx * dx + dy * dy;
	float t, qx, qy;
	if (len2 < 1e-12f) {
		dx = px - x1; dy = py - y1;
		return f_sqrt(dx * dx + dy * dy);
	}
	t = ((px - x1) * dx + (py - y1) * dy) / len2;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	qx = x1 + dx * t;
	qy = y1 + dy * t;
	dx = px - qx; dy = py - qy;
	return f_sqrt(dx * dx + dy * dy);
}

static int poly_inside(const SvgPoly *po, int contour, float px, float py)
{
	int start = po->starts[contour];
	int end = (contour + 1 < po->contourCount)
		? po->starts[contour + 1] : po->count;
	int i, inside = 0;
	for (i = start; i < end; i++) {
		int j = (i + 1 < end) ? i + 1 : start;
		float x1 = po->pts[i * 2], y1 = po->pts[i * 2 + 1];
		float x2 = po->pts[j * 2], y2 = po->pts[j * 2 + 1];
		if (((y1 > py) != (y2 > py))) {
			float xint = x1 + (py - y1) * (x2 - x1) / (y2 - y1);
			if (px < xint) inside = !inside;
		}
	}
	return inside;
}

static float poly_dist(const SvgPoly *po, float px, float py)
{
	int c, best = 1e30f;
	for (c = 0; c < po->contourCount; c++) {
		int start = po->starts[c];
		int end = (c + 1 < po->contourCount) ? po->starts[c + 1] : po->count;
		int i;
		for (i = start; i < end; i++) {
			int j = (i + 1 < end) ? i + 1 : start;
			float d = pt_seg_dist(px, py,
				po->pts[i * 2], po->pts[i * 2 + 1],
				po->pts[j * 2], po->pts[j * 2 + 1]);
			if (d < best) best = d;
		}
	}
	return best;
}

int Svg_ElementHit(const SvgElement *e, float wx, float wy, float tol)
{
	GfxRect bb;
	SvgPoly po;
	float margin, sw;
	int c, hit = 0;
	if (!e || !e->visible) return 0;
	sw = e->style.strokeWidth > 0.0f ? e->style.strokeWidth * 0.5f : 0.0f;
	margin = (tol > 0.0f ? tol : 0.5f) + sw;
	bb = Svg_ElementBounds(e);
	if (wx < bb.x - margin || wy < bb.y - margin ||
		wx > bb.x + bb.w + margin || wy > bb.y + bb.h + margin)
		return 0;
	if (e->type == SVG_EL_LINE) {
		float lx, ly;
		Svg_WorldToLocal(e, wx, wy, &lx, &ly);
		return pt_seg_dist(lx, ly, e->p.line.x1, e->p.line.y1,
			e->p.line.x2, e->p.line.y2) <= margin;
	}
	if (!Svg_ElementPoly(e, f_max(0.1f, margin * 0.2f), &po)) return 0;
	for (c = 0; c < po.contourCount; c++) {
		int start = po.starts[c];
		int end = (c + 1 < po.contourCount) ? po.starts[c + 1] : po.count;
		if (end - start < 2) continue;
		if (po.closed[c] && poly_inside(&po, c, wx, wy)) { hit = 1; break; }
	}
	if (!hit && poly_dist(&po, wx, wy) <= margin) hit = 1;
	Svg_PolyFree(&po);
	return hit;
}

int Svg_DocHit(const SvgDoc *d, float wx, float wy, float tol)
{
	int i;
	if (!d) return -1;
	for (i = d->count - 1; i >= 0; i--) {
		if (d->els[i].locked) continue;
		if (Svg_ElementHit(&d->els[i], wx, wy, tol)) return i;
	}
	return -1;
}

/* ── 元素 / 文档 ──────────────────────────────────────────────────── */

void Svg_ElementTouch(SvgElement *e)
{
	if (e) e->version++;
}

static void element_init(SvgElement *e, SvgElType type)
{
	memset(e, 0, sizeof(*e));
	e->type = type;
	e->visible = 1;
	e->version = 1;
	Svg_PathInit(&e->path);
	Svg_StyleDefault(&e->style);
	switch (type) {
	case SVG_EL_RECT:
		e->p.rect.x = 40.0f; e->p.rect.y = 40.0f;
		e->p.rect.w = 120.0f; e->p.rect.h = 80.0f;
		break;
	case SVG_EL_ELLIPSE:
		e->p.ellipse.cx = 100.0f; e->p.ellipse.cy = 80.0f;
		e->p.ellipse.rx = 60.0f; e->p.ellipse.ry = 40.0f;
		break;
	case SVG_EL_LINE:
		e->p.line.x1 = 40.0f; e->p.line.y1 = 40.0f;
		e->p.line.x2 = 160.0f; e->p.line.y2 = 120.0f;
		break;
	case SVG_EL_POLYGON:
	case SVG_EL_PATH:
		Svg_PathAddNode(&e->path, 60.0f, 40.0f, SVG_NODE_CORNER);
		Svg_PathAddNode(&e->path, 160.0f, 80.0f, SVG_NODE_CORNER);
		Svg_PathAddNode(&e->path, 80.0f, 140.0f, SVG_NODE_CORNER);
		if (type == SVG_EL_POLYGON) e->path.closed = 1;
		break;
	}
}

static void element_free(SvgElement *e)
{
	if (!e) return;
	Svg_PathFree(&e->path);
	Svg_StyleFree(&e->style);
}

static int element_copy(SvgElement *dst, const SvgElement *src)
{
	memset(dst, 0, sizeof(*dst));
	*dst = *src;
	dst->path.nodes = NULL; dst->path.seg = NULL; dst->path.arc = NULL;
	dst->path.count = dst->path.cap = 0;
	dst->style.fill.stops = NULL; dst->style.fill.stopCount = 0;
	dst->style.stroke.stops = NULL; dst->style.stroke.stopCount = 0;
	if (!Svg_PathCopy(&dst->path, &src->path)) return 0;
	if (!Svg_StyleCopy(&dst->style, &src->style)) {
		Svg_PathFree(&dst->path);
		return 0;
	}
	return 1;
}

static int doc_reserve(SvgDoc *d, int want)
{
	int cap;
	SvgElement *n;
	if (want <= d->cap) return 1;
	cap = d->cap ? d->cap * 2 : 16;
	if (cap < want) cap = want;
	n = (SvgElement *)x_grow(d->els, (size_t)cap * sizeof(SvgElement));
	if (!n) return 0;
	d->els = n;
	d->cap = cap;
	return 1;
}

/* ── 历史 ─────────────────────────────────────────────────────────── */

typedef struct {
	SvgElement *els;
	int count;
} SvgSnap;

typedef struct {
	SvgSnap snap[SVG_HISTORY_STEPS];
	int count;
	int at;
} SvgHistory;

static void snap_free(SvgSnap *s)
{
	int i;
	if (!s) return;
	for (i = 0; i < s->count; i++) element_free(&s->els[i]);
	x_free(s->els);
	s->els = NULL;
	s->count = 0;
}

static int snap_take(SvgSnap *s, const SvgDoc *d)
{
	int i;
	s->els = NULL;
	s->count = 0;
	if (d->count <= 0) return 1;
	s->els = (SvgElement *)x_alloc((size_t)d->count * sizeof(SvgElement));
	if (!s->els) return 0;
	for (i = 0; i < d->count; i++) {
		if (!element_copy(&s->els[i], &d->els[i])) {
			int k;
			for (k = 0; k < i; k++) element_free(&s->els[k]);
			x_free(s->els);
			s->els = NULL;
			return 0;
		}
	}
	s->count = d->count;
	return 1;
}

static void doc_clear_elements(SvgDoc *d)
{
	int i;
	for (i = 0; i < d->count; i++) element_free(&d->els[i]);
	d->count = 0;
}

void Svg_History_Commit(SvgDoc *d)
{
	SvgHistory *h;
	SvgSnap ns;
	if (!d) return;
	h = (SvgHistory *)d->history;
	if (!h) return;
	if (!snap_take(&ns, d)) return;
	/* 丢弃 redo 分支 */
	while (h->count > h->at + 1) {
		snap_free(&h->snap[h->count - 1]);
		h->count--;
	}
	if (h->count >= SVG_HISTORY_STEPS) {
		int i;
		snap_free(&h->snap[0]);
		for (i = 1; i < h->count; i++) h->snap[i - 1] = h->snap[i];
		h->count--;
		if (h->at > 0) h->at--;
	}
	h->snap[h->count] = ns;
	h->count++;
	h->at = h->count - 1;
}

static int doc_restore(SvgDoc *d, const SvgSnap *s)
{
	int i;
	doc_clear_elements(d);
	if (!doc_reserve(d, s->count > 0 ? s->count : 1)) return 0;
	for (i = 0; i < s->count; i++) {
		if (!element_copy(&d->els[i], &s->els[i])) {
			d->count = i;
			return 0;
		}
	}
	d->count = s->count;
	return 1;
}

int Svg_History_CanUndo(const SvgDoc *d)
{
	const SvgHistory *h = d ? (const SvgHistory *)d->history : NULL;
	return h && h->at > 0;
}

int Svg_History_CanRedo(const SvgDoc *d)
{
	const SvgHistory *h = d ? (const SvgHistory *)d->history : NULL;
	return h && h->at + 1 < h->count;
}

int Svg_History_Undo(SvgDoc *d)
{
	SvgHistory *h;
	if (!d || !d->history) return 0;
	h = (SvgHistory *)d->history;
	if (h->at <= 0) return 0;
	h->at--;
	return doc_restore(d, &h->snap[h->at]);
}

int Svg_History_Redo(SvgDoc *d)
{
	SvgHistory *h;
	if (!d || !d->history) return 0;
	h = (SvgHistory *)d->history;
	if (h->at + 1 >= h->count) return 0;
	h->at++;
	return doc_restore(d, &h->snap[h->at]);
}

/* ── 文档 API ─────────────────────────────────────────────────────── */

SvgDoc *SvgDoc_Create(float w, float h)
{
	SvgDoc *d = (SvgDoc *)x_alloc(sizeof(SvgDoc));
	SvgHistory *hs;
	if (!d) return NULL;
	d->width = w > 0.0f ? w : 800.0f;
	d->height = h > 0.0f ? h : 600.0f;
	d->background = 0;
	hs = (SvgHistory *)x_alloc(sizeof(SvgHistory));
	if (!hs) { x_free(d); return NULL; }
	d->history = hs;
	hs->count = 0;
	hs->at = -1;
	Svg_History_Commit(d);
	return d;
}

void SvgDoc_Destroy(SvgDoc *d)
{
	SvgHistory *hs;
	int i;
	if (!d) return;
	hs = (SvgHistory *)d->history;
	if (hs) {
		for (i = 0; i < hs->count; i++) snap_free(&hs->snap[i]);
		x_free(hs);
	}
	for (i = 0; i < d->count; i++) element_free(&d->els[i]);
	x_free(d->els);
	x_free(d);
}

SvgElement *SvgDoc_Add(SvgDoc *d, SvgElType type)
{
	if (!d || d->count >= SVG_MAX_ELEMENTS) return NULL;
	if (!doc_reserve(d, d->count + 1)) return NULL;
	element_init(&d->els[d->count], type);
	d->count++;
	return &d->els[d->count - 1];
}

void SvgDoc_Remove(SvgDoc *d, int index)
{
	int i;
	if (!d || index < 0 || index >= d->count) return;
	element_free(&d->els[index]);
	for (i = index; i < d->count - 1; i++) d->els[i] = d->els[i + 1];
	d->count--;
}

int SvgDoc_Duplicate(SvgDoc *d, int index)
{
	SvgElement *e;
	if (!d || index < 0 || index >= d->count) return -1;
	if (d->count >= SVG_MAX_ELEMENTS) return -1;
	if (!doc_reserve(d, d->count + 1)) return -1;
	e = &d->els[d->count];
	if (!element_copy(e, &d->els[index])) return -1;
	e->version++;
	d->count++;
	return d->count - 1;
}

void SvgDoc_MoveZ(SvgDoc *d, int index, int dir)
{
	SvgElement tmp;
	int to;
	if (!d || index < 0 || index >= d->count || dir == 0) return;
	to = index + (dir > 0 ? 1 : -1);
	if (to < 0 || to >= d->count) return;
	tmp = d->els[index];
	d->els[index] = d->els[to];
	d->els[to] = tmp;
}

/* ── 序列化 ───────────────────────────────────────────────────────── */

typedef struct {
	char *s;
	int len, cap;
	int bad;
} SB;

static int sb_need(SB *b, int extra)
{
	int cap;
	char *n;
	if (b->bad) return 0;
	if (b->len + extra + 1 <= b->cap) return 1;
	cap = b->cap ? b->cap * 2 : 512;
	while (cap < b->len + extra + 1) cap *= 2;
	n = (char *)x_grow(b->s, (size_t)cap);
	if (!n) { b->bad = 1; return 0; }
	b->s = n;
	b->cap = cap;
	return 1;
}

static void sb_put(SB *b, const char *s)
{
	int n = 0;
	while (s[n]) n++;
	if (!sb_need(b, n)) return;
	memcpy(b->s + b->len, s, (size_t)n);
	b->len += n;
	b->s[b->len] = 0;
}

static void sb_ch(SB *b, char c)
{
	if (!sb_need(b, 1)) return;
	b->s[b->len++] = c;
	b->s[b->len] = 0;
}

static void fmt_num(char *out, int cap, float v)
{
	char tmp[40];
	int n = 0, m = 0, i;
	long long scaled, ip, fp;
	int neg = v < 0.0f;
	char rev[24];
	if (cap < 2) return;
	if (neg) v = -v;
	scaled = (long long)(v * 100.0f + 0.5f);
	ip = scaled / 100;
	fp = scaled % 100;
	if (neg && scaled == 0) neg = 0;
	if (ip == 0) {
		tmp[n++] = '0';
	} else {
		while (ip > 0 && m < 23) { rev[m++] = (char)('0' + (int)(ip % 10)); ip /= 10; }
		for (i = m - 1; i >= 0; i--) tmp[n++] = rev[i];
	}
	if (fp) {
		tmp[n++] = '.';
		tmp[n++] = (char)('0' + (int)(fp / 10));
		if (fp % 10) tmp[n++] = (char)('0' + (int)(fp % 10));
	}
	if (neg && n < 39) {
		for (i = n; i > 0; i--) tmp[i] = tmp[i - 1];
		tmp[0] = '-';
		n++;
	}
	if (n >= cap) n = cap - 1;
	for (i = 0; i < n; i++) out[i] = tmp[i];
	out[n] = 0;
}

static void sb_num(SB *b, float v)
{
	char buf[40];
	fmt_num(buf, (int)sizeof(buf), v);
	sb_put(b, buf);
}

static void sb_color(SB *b, GfxColor c)
{
	static const char hex[] = "0123456789ABCDEF";
	sb_ch(b, '#');
	sb_ch(b, hex[(c >> 20) & 0xF]);
	sb_ch(b, hex[(c >> 16) & 0xF]);
	sb_ch(b, hex[(c >> 12) & 0xF]);
	sb_ch(b, hex[(c >> 8) & 0xF]);
	sb_ch(b, hex[(c >> 4) & 0xF]);
	sb_ch(b, hex[c & 0xF]);
}

static void sb_attr_num(SB *b, const char *name, float v)
{
	sb_ch(b, ' ');
	sb_put(b, name);
	sb_put(b, "=\"");
	sb_num(b, v);
	sb_ch(b, '"');
}

static void sb_attr_str(SB *b, const char *name, const char *v)
{
	const char *p;
	sb_ch(b, ' ');
	sb_put(b, name);
	sb_put(b, "=\"");
	for (p = v; *p; p++) {
		if (*p == '&') sb_put(b, "&amp;");
		else if (*p == '<') sb_put(b, "&lt;");
		else if (*p == '>') sb_put(b, "&gt;");
		else if (*p == '"') sb_put(b, "&quot;");
		else sb_ch(b, *p);
	}
	sb_ch(b, '"');
}

static const char *cap_name(int cap)
{
	if (cap == SVG_CAP_ROUND) return "round";
	if (cap == SVG_CAP_SQUARE) return "square";
	return "butt";
}

static const char *join_name(int join)
{
	if (join == SVG_JOIN_ROUND) return "round";
	if (join == SVG_JOIN_BEVEL) return "bevel";
	return "miter";
}

static int paint_is_gradient(const SvgPaint *p)
{
	return p->kind == SVG_PAINT_LINEAR && p->stops && p->stopCount >= 2;
}

static int grad_equal(const SvgPaint *a, const SvgPaint *b)
{
	int i;
	if (a->kind != b->kind || a->angleDeg != b->angleDeg ||
		a->stopCount != b->stopCount)
		return 0;
	for (i = 0; i < a->stopCount; i++) {
		if (a->stops[i].offset != b->stops[i].offset ||
			a->stops[i].color != b->stops[i].color)
			return 0;
	}
	return 1;
}

/* 返回渐变在 defs 中的序号; 新参数追加到 defs 列表 */
typedef struct {
	const SvgPaint *paints[32];
	int count;
} GradDefs;

static int grad_id(GradDefs *gd, const SvgPaint *p)
{
	int i;
	if (!paint_is_gradient(p)) return -1;
	for (i = 0; i < gd->count; i++) {
		if (grad_equal(gd->paints[i], p)) return i;
	}
	if (gd->count >= 32) return -1;
	gd->paints[gd->count] = p;
	return gd->count++;
}

static void export_gradient(SB *b, int id, const SvgPaint *p)
{
	float dirx = cos_r(deg_r(p->angleDeg));
	float diry = sin_r(deg_r(p->angleDeg));
	float half = (f_abs(dirx) + f_abs(diry)) * 0.5f;
	int i;
	sb_put(b, "    <linearGradient id=\"g");
	{
		char nb[16];
		fmt_num(nb, (int)sizeof(nb), (float)id);
		sb_put(b, nb);
	}
	sb_put(b, "\" gradientUnits=\"objectBoundingBox\"");
	sb_attr_num(b, " x1", 0.5f - dirx * half);
	sb_attr_num(b, " y1", 0.5f - diry * half);
	sb_attr_num(b, " x2", 0.5f + dirx * half);
	sb_attr_num(b, " y2", 0.5f + diry * half);
	sb_put(b, ">\n");
	for (i = 0; i < p->stopCount; i++) {
		float off = p->stops[i].offset;
		GfxColor c = p->stops[i].color;
		if (off < 0.0f) off = 0.0f;
		if (off > 1.0f) off = 1.0f;
		sb_put(b, "      <stop offset=\"");
		sb_num(b, off);
		sb_put(b, "\" stop-color=\"");
		sb_color(b, c);
		sb_put(b, "\"");
		if ((c >> 24) != 0xFF) sb_attr_num(b, " stop-opacity", (float)((c >> 24) & 0xFF) / 255.0f);
		sb_put(b, "/>\n");
	}
	sb_put(b, "    </linearGradient>\n");
}

static void export_paint_attr(SB *b, const char *name, const SvgPaint *p,
	GradDefs *gd);
static void export_paint_attr(SB *b, const char *name, const SvgPaint *p,
	GradDefs *gd)
{
	char attr[24];
	int i, k = 0;
	const char *prefix = name;
	for (i = 0; prefix[i]; i++) attr[k++] = prefix[i];
	attr[k] = 0;
	if (p->kind == SVG_PAINT_NONE) {
		sb_attr_str(b, attr, "none");
		return;
	}
	if (paint_is_gradient(p)) {
		int id = grad_id(gd, p);
		if (id >= 0) {
			sb_ch(b, ' ');
			sb_put(b, attr);
			sb_put(b, "=\"url(#g");
			{
				char nb[16];
				fmt_num(nb, (int)sizeof(nb), (float)id);
				sb_put(b, nb);
			}
			sb_put(b, ")\"");
			return;
		}
	}
	sb_ch(b, ' ');
	sb_put(b, attr);
	sb_put(b, "=\"");
	sb_color(b, p->color);
	sb_ch(b, '"');
	/* 纯色 alpha -> fill-opacity/stroke-opacity */
	if ((p->color >> 24) != 0xFF) {
		int n = k;
		attr[n++] = '-';
		attr[n++] = 'o';
		attr[n++] = 'p';
		attr[n++] = 'a';
		attr[n++] = 'c';
		attr[n++] = 'i';
		attr[n++] = 't';
		attr[n++] = 'y';
		attr[n] = 0;
		sb_attr_num(b, attr, (float)((p->color >> 24) & 0xFF) / 255.0f);
	}
}

static void export_style(SB *b, const SvgElement *e, GradDefs *gd)
{
	const SvgStyle *st = &e->style;
	int i;
	int hasStroke = st->stroke.kind != SVG_PAINT_NONE && st->strokeWidth > 0.0f;
	if (e->type == SVG_EL_LINE) sb_attr_str(b, "fill", "none");
	else export_paint_attr(b, "fill", &st->fill, gd);
	if (hasStroke) {
		export_paint_attr(b, "stroke", &st->stroke, gd);
		sb_attr_num(b, "stroke-width", st->strokeWidth);
		if (st->cap != SVG_CAP_BUTT) sb_attr_str(b, "stroke-linecap", cap_name(st->cap));
		if (st->join != SVG_JOIN_MITER) sb_attr_str(b, "stroke-linejoin", join_name(st->join));
		if (st->dashCount > 0) {
			sb_put(b, " stroke-dasharray=\"");
			for (i = 0; i < st->dashCount && i < 8; i++) {
				if (i) sb_ch(b, ' ');
				sb_num(b, st->dash[i]);
			}
			sb_ch(b, '"');
		}
	} else {
		sb_attr_str(b, "stroke", "none");
	}
	if (st->opacity < 0.999f) sb_attr_num(b, "opacity", st->opacity);
}

static void export_transform(SB *b, const SvgElement *e)
{
	float cx, cy;
	if (e->rot == 0.0f) return;
	Svg_ElementCenter(e, &cx, &cy);
	sb_put(b, " transform=\"rotate(");
	sb_num(b, e->rot);
	sb_ch(b, ' ');
	sb_num(b, cx);
	sb_ch(b, ' ');
	sb_num(b, cy);
	sb_put(b, ")\"");
}

static void export_path_d(SB *b, const SvgPath *p)
{
	int i, sc;
	if (!p || p->count <= 0) return;
	sc = path_seg_count(p);
	sb_put(b, "M ");
	sb_num(b, p->nodes[0].x);
	sb_ch(b, ' ');
	sb_num(b, p->nodes[0].y);
	for (i = 0; i < sc; i++) {
		int j = (i + 1) % p->count;
		const SvgNode *n0 = &p->nodes[i];
		const SvgNode *n1 = &p->nodes[j];
		SvgSegType t = p->seg[i];
		/* 闭合直线段由 Z 隐含, 不再写 L 到首点 (避免解析回来多一个节点) */
		if (p->closed && i == sc - 1 && t == SVG_SEG_LINE) continue;
		if (t == SVG_SEG_LINE) {
			sb_put(b, " L ");
			sb_num(b, n1->x);
			sb_ch(b, ' ');
			sb_num(b, n1->y);
		} else if (t == SVG_SEG_CUBIC) {
			float c1x = n0->x + (n0->hasOut ? n0->outX : 0.0f);
			float c1y = n0->y + (n0->hasOut ? n0->outY : 0.0f);
			float c2x = n1->x + (n1->hasIn ? n1->inX : 0.0f);
			float c2y = n1->y + (n1->hasIn ? n1->inY : 0.0f);
			sb_put(b, " C ");
			sb_num(b, c1x); sb_ch(b, ' '); sb_num(b, c1y); sb_ch(b, ' ');
			sb_num(b, c2x); sb_ch(b, ' '); sb_num(b, c2y); sb_ch(b, ' ');
			sb_num(b, n1->x); sb_ch(b, ' '); sb_num(b, n1->y);
		} else {
			const SvgArc *a = &p->arc[i];
			sb_put(b, " A ");
			sb_num(b, a->rx); sb_ch(b, ' ');
			sb_num(b, a->ry); sb_ch(b, ' ');
			sb_num(b, a->rotDeg); sb_ch(b, ' ');
			sb_ch(b, a->large ? '1' : '0'); sb_ch(b, ' ');
			sb_ch(b, a->sweep ? '1' : '0'); sb_ch(b, ' ');
			sb_num(b, n1->x); sb_ch(b, ' '); sb_num(b, n1->y);
		}
	}
	if (p->closed) sb_put(b, " Z");
}

static void export_element(SB *b, const SvgElement *e, GradDefs *gd)
{
	if (!e->visible) return;
	sb_put(b, "  ");
	switch (e->type) {
	case SVG_EL_RECT:
		sb_put(b, "<rect");
		sb_attr_num(b, " x", e->p.rect.x);
		sb_attr_num(b, " y", e->p.rect.y);
		sb_attr_num(b, " width", e->p.rect.w);
		sb_attr_num(b, " height", e->p.rect.h);
		break;
	case SVG_EL_ELLIPSE:
		sb_put(b, "<ellipse");
		sb_attr_num(b, " cx", e->p.ellipse.cx);
		sb_attr_num(b, " cy", e->p.ellipse.cy);
		sb_attr_num(b, " rx", e->p.ellipse.rx);
		sb_attr_num(b, " ry", e->p.ellipse.ry);
		break;
	case SVG_EL_LINE:
		sb_put(b, "<line");
		sb_attr_num(b, " x1", e->p.line.x1);
		sb_attr_num(b, " y1", e->p.line.y1);
		sb_attr_num(b, " x2", e->p.line.x2);
		sb_attr_num(b, " y2", e->p.line.y2);
		break;
	default:
		sb_put(b, "<path d=\"");
		export_path_d(b, &e->path);
		sb_ch(b, '"');
		break;
	}
	export_style(b, e, gd);
	export_transform(b, e);
	if (e->name[0]) sb_attr_str(b, " data-name", e->name);
	sb_put(b, "/>\n");
}

char *Svg_Export(const SvgDoc *d)
{
	SB b;
	GradDefs gd;
	int i;
	if (!d) return NULL;
	memset(&b, 0, sizeof(b));
	memset(&gd, 0, sizeof(gd));
	for (i = 0; i < d->count; i++) {
		if (!d->els[i].visible) continue;
		if (paint_is_gradient(&d->els[i].style.fill))
			grad_id(&gd, &d->els[i].style.fill);
		if (paint_is_gradient(&d->els[i].style.stroke))
			grad_id(&gd, &d->els[i].style.stroke);
	}

	sb_put(&b, "<svg xmlns=\"http://www.w3.org/2000/svg\"");
	sb_attr_num(&b, " width", d->width);
	sb_attr_num(&b, " height", d->height);
	sb_put(&b, " viewBox=\"0 0 ");
	sb_num(&b, d->width);
	sb_ch(&b, ' ');
	sb_num(&b, d->height);
	sb_put(&b, "\">\n");
	if (gd.count > 0) {
		sb_put(&b, "  <defs>\n");
		for (i = 0; i < gd.count; i++) export_gradient(&b, i, gd.paints[i]);
		sb_put(&b, "  </defs>\n");
	}
	if ((d->background >> 24) != 0) {
		sb_put(&b, "  <rect x=\"0\" y=\"0\"");
		{
			char tmp[40];
			fmt_num(tmp, (int)sizeof(tmp), d->width);
			sb_put(&b, " width=\"");
			sb_put(&b, tmp);
			sb_put(&b, "\"");
			fmt_num(tmp, (int)sizeof(tmp), d->height);
			sb_put(&b, " height=\"");
			sb_put(&b, tmp);
			sb_put(&b, "\"");
		}
		sb_put(&b, " fill=\"");
		sb_color(&b, d->background);
		sb_put(&b, "\"/>\n");
	}
	for (i = 0; i < d->count; i++) export_element(&b, &d->els[i], &gd);
	sb_put(&b, "</svg>\n");
	if (b.bad) { x_free(b.s); return NULL; }
	if (!b.s) b.s = (char *)x_alloc(1);
	return b.s;
}

void Svg_FreeString(char *s)
{
	x_free(s);
}

/* ── d 解析 (绝对 M/L/C/A/Z) ─────────────────────────────────────── */

static const char *skip_ws(const char *s)
{
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == ',') s++;
	return s;
}

static int read_num(const char **ps, float *out)
{
	const char *s = skip_ws(*ps);
	int neg = 0, any = 0;
	double v = 0.0, frac = 0.0, base = 0.1;
	if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
	while (*s >= '0' && *s <= '9') { v = v * 10.0 + (double)(*s - '0'); s++; any = 1; }
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') {
			frac += (double)(*s - '0') * base;
			base *= 0.1;
			s++;
			any = 1;
		}
	}
	if (!any) return 0;
	*out = (float)(neg ? -(v + frac) : (v + frac));
	*ps = s;
	return 1;
}

int Svg_ImportPathD(const char *d, SvgPath *out)
{
	const char *s = d;
	char cmd = 0;
	int first = 1;
	float lastX = 0.0f, lastY = 0.0f;
	if (!d || !out) return 0;
	Svg_PathInit(out);
	while (1) {
		float a, b, c, e, f, g;
		s = skip_ws(s);
		if (!*s) break;
		if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) {
			if (*s >= 'a' && *s <= 'z') { Svg_PathFree(out); return 0; }
			cmd = *s++;
			if (cmd == 'Z') { out->closed = 1; continue; }
			s = skip_ws(s);
			if (!*s) { Svg_PathFree(out); return 0; }
		} else if (!cmd) {
			Svg_PathFree(out);
			return 0;
		}
		if (cmd == 'M') {
			if (!read_num(&s, &a) || !read_num(&s, &b)) goto bad;
			if (first) {
				Svg_PathAddNode(out, a, b, SVG_NODE_CORNER);
				first = 0;
			} else {
				/* 多子路径不支持: 后续 M 按 L 处理 */
				int idx = Svg_PathAddNode(out, a, b, SVG_NODE_CORNER);
				if (idx < 0) goto bad;
			}
			lastX = a; lastY = b;
			cmd = 'L';
		} else if (cmd == 'L') {
			if (!read_num(&s, &a) || !read_num(&s, &b)) goto bad;
			if (Svg_PathAddNode(out, a, b, SVG_NODE_CORNER) < 0) goto bad;
			lastX = a; lastY = b;
		} else if (cmd == 'C') {
			int idx;
			if (!read_num(&s, &a) || !read_num(&s, &b) ||
				!read_num(&s, &c) || !read_num(&s, &e) ||
				!read_num(&s, &f) || !read_num(&s, &g)) goto bad;
			idx = Svg_PathAddNode(out, f, g, SVG_NODE_CORNER);
			if (idx < 1) goto bad;
			out->nodes[idx - 1].outX = a - lastX;
			out->nodes[idx - 1].outY = b - lastY;
			out->nodes[idx - 1].hasOut = 1;
			out->nodes[idx].inX = c - f;
			out->nodes[idx].inY = e - g;
			out->nodes[idx].hasIn = 1;
			out->seg[idx - 1] = SVG_SEG_CUBIC;
			lastX = f; lastY = g;
		} else if (cmd == 'A') {
			int idx;
			float ex, ey;
			if (!read_num(&s, &a) || !read_num(&s, &b) ||
				!read_num(&s, &c) || !read_num(&s, &e) ||
				!read_num(&s, &f) || !read_num(&s, &ex) ||
				!read_num(&s, &ey)) goto bad;
			idx = Svg_PathAddNode(out, ex, ey, SVG_NODE_CORNER);
			if (idx < 1) goto bad;
			out->seg[idx - 1] = SVG_SEG_ARC;
			out->arc[idx - 1].rx = a;
			out->arc[idx - 1].ry = b;
			out->arc[idx - 1].rotDeg = c;
			out->arc[idx - 1].large = e > 0.5f ? 1 : 0;
			out->arc[idx - 1].sweep = f > 0.5f ? 1 : 0;
			lastX = ex; lastY = ey;
		} else {
			goto bad;
		}
	}
	if (out->closed && out->count >= 2) {
		SvgNode *n0 = &out->nodes[0];
		SvgNode *nl = &out->nodes[out->count - 1];
		if (f_abs(n0->x - nl->x) < 1e-4f && f_abs(n0->y - nl->y) < 1e-4f) {
			/* 末节点与首节点重合 (显式 L/C/A 回起点): 合并, 该段成为闭合段 */
			out->count--;
		} else if (out->count >= 3) {
			out->seg[out->count - 1] = SVG_SEG_LINE;
			memset(&out->arc[out->count - 1], 0, sizeof(SvgArc));
		}
	}
	if (out->closed && out->count < 3) out->closed = 0;
	return out->count >= 1;
bad:
	Svg_PathFree(out);
	return 0;
}
