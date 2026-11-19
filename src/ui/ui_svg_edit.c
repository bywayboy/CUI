/*
 * ui_svg_edit.c - SVG 画布编辑组件实现
 *
 * 视口: screen = world*scale + t。世界坐标原点在画板左上。
 * 交互只在按下/抬起帧落业务状态; 拖动过程中直接改元素几何并逐帧提交
 * Svg_ElementTouch, 抬起时一次性 Svg_History_Commit。
 * 几何/画刷缓存按 (元素指针, version) 失效; 结构性变更后整体清缓存。
 */
#include <windows.h>
#include "ui/ui_svg_edit.h"
#include "ui/ui_theme.h"

extern void *memset(void *d, int c, size_t n);
extern void *memcpy(void *d, const void *s, size_t n);
extern int memcmp(const void *a, const void *b, size_t n);
extern double sqrt(double x);

#define SE_PI 3.14159265358979323846f
#define SE_2PI 6.28318530717958647692f
#define SE_DEG (SE_PI / 180.0f)

#define SE_HANDLE 8.0f
#define SE_NODE_HIT 6.0f
#define SE_ROT_DIST 26.0f
#define SE_ZOOM_MIN 0.05f
#define SE_ZOOM_MAX 32.0f
#define SE_TOL_PX 4.0f
#define SE_FIT_MARGIN 36.0f
#define SE_WORK_PAD 24.0f     /* 画板四周工作区留白 (屏幕 DIP) */
#define SE_SB_W 8.0f          /* 滚动条宽度 */
#define SE_SB_MARGIN 2.0f
#define SE_SB_MIN 24.0f
#define SE_MAX_GRAD_STOPS 16

enum {
	SE_DRAG_NONE = 0,
	SE_DRAG_PAN,
	SE_DRAG_RUBBER,
	SE_DRAG_MOVE,
	SE_DRAG_SCALE,
	SE_DRAG_ROTATE,
	SE_DRAG_CREATE,
	SE_DRAG_PEN_POINT,
	SE_DRAG_SWALLOW,
	SE_DRAG_NODE,
	SE_DRAG_HANDLE,
	SE_DRAG_ARCMID,
	SE_DRAG_GRAD
};

typedef struct SvgCacheEntry {
	SvgElement *el;
	int version;
	GfxPath *path;
	GfxBrush *fill;
	GfxBrush *stroke;
} SvgCacheEntry;

/* 拖动起点保存的局部几何 (节点数组深拷贝) */
typedef struct SvgGeom {
	SvgElType type;
	float p[8];
	SvgPath path;
	float rot;
} SvgGeom;

/* 2D 滚动条几何 (画板+工作区留白 超出视口时出现) */
typedef struct SeSB {
	int hasH, hasV;
	UiRect ht, hth;   /* 水平 track/thumb */
	UiRect vt, vth;   /* 垂直 track/thumb */
} SeSB;

struct UiSvgEdit {
	SvgDoc *doc;
	UiId id;
	UiContext *ctx;
	Gfx *gfx;
	UiRect vp;
	int viewValid;
	SvgView view;
	int sbDrag;          /* 滚动条拖拽: 0=无 1=水平 2=垂直 */
	float sbGrab;        /* 按下时鼠标 - thumb 起点 */

	int tool;
	int gridOn;
	float gridStep;
	SvgStyle defStyle;

	int *sel;
	int selCount, selCap;

	SvgCacheEntry *cache;
	int cacheCap;
	/* 文本字体缓存 (family/size/weight) */
	GfxFont *fonts[8];
	char fontFam[8][64];
	float fontSizes[8];
	int fontWeights[8];
	int fontCount;
	SvgGeom *orig;
	int origCount;
	int origCap;
	SvgClip *clip;

	/* 鼠标 */
	float mx, my, lastMx, lastMy;
	int mouseIn;
	int mouseWorldValid;
	float mouseWorldX, mouseWorldY;

	/* 几何缓存 (跨帧, 键包含 view/文档版本): 滚动条几何 / 选区包围盒 */
	SeSB sbCache;
	float sbKey[9];
	int sbKeyValid;
	uint64_t selBoundsKey;
	GfxRect selBoundsCache;
	int selBoundsValid;
	int selBoundsHas;

	/* 拖动 */
	int drag;
	int dragHandle;
	int dragMoved;
	int gradPart;
	int rubberAdd;
	float pressSX, pressSY;
	float startWX, startWY;
	float pivotX, pivotY, startAngle;
	GfxRect startBox;
	float snapGX, snapGY;   /* 拖动吸附参考线 (世界坐标) */
	int snapGXOn, snapGYOn;

	/* 新建 */
	int createIndex;
	float createPX, createPY;
	int polySides;

	/* 节点 */
	int nodeSel;
	int hoverNode;
	int dragNode;
	int dragSeg;
	float nodeOX, nodeOY;
	float handleOX, handleOY;

	/* 钢笔 */
	int penActive;
	int penIndex;
	int penNext;         /* SVG_SEG_LINE/CUBIC/ARC */
	int penWant3;        /* 1 = 下一段用三点弧 */
	int penArcStage;     /* 1 = 已落下三点弧中间点 */
	float penMidX, penMidY;
	int penJustStarted;
	int penDragging;
	float penDownWX, penDownWY;

	/* 双击 */
	DWORD lastClickTick;
	float lastClickX, lastClickY;

	char hint[192];
	int inputBlock;   /* 弹层/菜单打开时暂停画布输入 */
};

/* ── 基础数学 ─────────────────────────────────────────────────────── */

static float s_abs(float v) { return v < 0.0f ? -v : v; }
static float s_min(float a, float b) { return a < b ? a : b; }
static float s_max(float a, float b) { return a > b ? a : b; }

static float s_sqrt(float x)
{
	return x <= 0.0f ? 0.0f : (float)sqrt((double)x);
}

static float s_atan(float x)
{
	float off = 0.0f, x2, term, sum;
	int i, sign = 1, neg = 0;
	if (x < 0.0f) { neg = 1; x = -x; }
	if (x > 1.0f) {
		float r = s_atan(1.0f / x);
		return neg ? -(SE_PI * 0.5f - r) : (SE_PI * 0.5f - r);
	}
	if (x > 0.41421356f) {
		x = (x - 1.0f) / (x + 1.0f);
		off = SE_PI * 0.25f;
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

static float s_atan2(float y, float x)
{
	if (x == 0.0f)
		return y > 0.0f ? SE_PI * 0.5f : (y < 0.0f ? -SE_PI * 0.5f : 0.0f);
	if (x > 0.0f) return s_atan(y / x);
	return y >= 0.0f ? s_atan(y / x) + SE_PI : s_atan(y / x) - SE_PI;
}

static float s_sin(float xf)
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

static float s_cos(float x) { return s_sin(x + SE_PI * 0.5f); }

static float s_round_grid(float v, float step)
{
	float q;
	if (step <= 0.0f) return v;
	q = v / step;
	q = q >= 0.0f ? (float)(int)(q + 0.5f) : (float)(int)(q - 0.5f);
	return q * step;
}

static void *s_alloc(size_t n)
{
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
}

static void *s_grow(void *old, size_t n)
{
	return old
		? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, old, n ? n : 1)
		: HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
}

static void s_free(void *p)
{
	if (p) HeapFree(GetProcessHeap(), 0, p);
}

static void s_str(char *dst, int cap, const char *src)
{
	int i = 0;
	if (!dst || cap <= 0) return;
	while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
	dst[i] = 0;
}

static int shift_down(void) { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }
static int alt_down(void) { return (GetKeyState(VK_MENU) & 0x8000) != 0; }
static int ctrl_down(void) { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
static int space_down(void) { return (GetKeyState(VK_SPACE) & 0x8000) != 0; }

/* ── 视图 ─────────────────────────────────────────────────────────── */

static void view_clamp(UiSvgEdit *ed);

static void view_fit_view(UiSvgEdit *ed, UiRect vp)
{
	float sw, sh, s;
	if (!ed->doc || !vp.w || !vp.h) return;
	sw = (vp.w - SE_FIT_MARGIN * 2.0f) / ed->doc->width;
	sh = (vp.h - SE_FIT_MARGIN * 2.0f) / ed->doc->height;
	s = sw < sh ? sw : sh;
	if (s <= 0.0f) s = 1.0f;
	if (s < SE_ZOOM_MIN) s = SE_ZOOM_MIN;
	if (s > SE_ZOOM_MAX) s = SE_ZOOM_MAX;
	ed->view.scale = s;
	ed->view.tx = vp.x + (vp.w - ed->doc->width * s) * 0.5f;
	ed->view.ty = vp.y + (vp.h - ed->doc->height * s) * 0.5f;
	ed->viewValid = 1;
	view_clamp(ed);
}

static void view_reset(UiSvgEdit *ed, UiRect vp)
{
	float s = 1.0f;
	ed->view.scale = s;
	ed->view.tx = vp.x + (vp.w - ed->doc->width * s) * 0.5f;
	ed->view.ty = vp.y + (vp.h - ed->doc->height * s) * 0.5f;
	ed->viewValid = 1;
	view_clamp(ed);
}

static void view_zoom_at(UiSvgEdit *ed, float factor, float sx, float sy)
{
	float wx, wy, ns;
	if (!ed->viewValid) return;
	Svg_ViewWorld(&ed->view, sx, sy, &wx, &wy);
	ns = ed->view.scale * factor;
	if (ns < SE_ZOOM_MIN) ns = SE_ZOOM_MIN;
	if (ns > SE_ZOOM_MAX) ns = SE_ZOOM_MAX;
	ed->view.scale = ns;
	ed->view.tx = sx - wx * ns;
	ed->view.ty = sy - wy * ns;
	view_clamp(ed);
}

/* ── 工作区滚动视图 ───────────────────────────────────────────────── */

/* 内容 (画板 + 四周留白) 小于视口时居中, 否则夹紧到滚动范围内,
 * 保证画板不会完全滚出视野。 */
static void view_clamp(UiSvgEdit *ed)
{
	float dsx, dsy, cw, ch, lo, hi;
	if (!ed || !ed->doc || !ed->viewValid) return;
	if (ed->vp.w <= 0.0f || ed->vp.h <= 0.0f) return;
	dsx = ed->doc->width * ed->view.scale;
	dsy = ed->doc->height * ed->view.scale;
	cw = dsx + SE_WORK_PAD * 2.0f;
	ch = dsy + SE_WORK_PAD * 2.0f;
	if (cw <= ed->vp.w) {
		ed->view.tx = ed->vp.x + (ed->vp.w - dsx) * 0.5f;
	} else {
		lo = ed->vp.x + ed->vp.w - SE_WORK_PAD - dsx;
		hi = ed->vp.x + SE_WORK_PAD;
		if (ed->view.tx < lo) ed->view.tx = lo;
		if (ed->view.tx > hi) ed->view.tx = hi;
	}
	if (ch <= ed->vp.h) {
		ed->view.ty = ed->vp.y + (ed->vp.h - dsy) * 0.5f;
	} else {
		lo = ed->vp.y + ed->vp.h - SE_WORK_PAD - dsy;
		hi = ed->vp.y + SE_WORK_PAD;
		if (ed->view.ty < lo) ed->view.ty = lo;
		if (ed->view.ty > hi) ed->view.ty = hi;
	}
}

static void sb_geom(const UiSvgEdit *ed, SeSB *sb)
{
	UiRect vp;
	float dsx, dsy, cw, ch, w, m, span, t, hi, range;
	if (!ed || !ed->doc || !ed->viewValid) {
		memset(sb, 0, sizeof(*sb));
		return;
	}
	memset(sb, 0, sizeof(*sb));
	vp = ed->vp;
	w = SE_SB_W;
	m = SE_SB_MARGIN;
	dsx = ed->doc->width * ed->view.scale;
	dsy = ed->doc->height * ed->view.scale;
	cw = dsx + SE_WORK_PAD * 2.0f;
	ch = dsy + SE_WORK_PAD * 2.0f;
	sb->hasH = cw > vp.w + 0.5f;
	sb->hasV = ch > vp.h + 0.5f;
	if (sb->hasV) {
		sb->vt = Ui_Rect(vp.x + vp.w - w - m, vp.y + m, w,
			vp.h - m * 2.0f - (sb->hasH ? w + m : 0.0f));
		span = sb->vt.h * vp.h / ch;
		if (span < SE_SB_MIN) span = SE_SB_MIN;
		if (span > sb->vt.h) span = sb->vt.h;
		hi = vp.y + SE_WORK_PAD;
		range = ch - vp.h;
		t = range > 0.0f ? (hi - ed->view.ty) / range : 0.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		sb->vth = Ui_Rect(sb->vt.x, sb->vt.y + (sb->vt.h - span) * t,
			w, span);
	}
	if (sb->hasH) {
		sb->ht = Ui_Rect(vp.x + m, vp.y + vp.h - w - m,
			vp.w - m * 2.0f - (sb->hasV ? w + m : 0.0f), w);
		span = sb->ht.w * vp.w / cw;
		if (span < SE_SB_MIN) span = SE_SB_MIN;
		if (span > sb->ht.w) span = sb->ht.w;
		hi = vp.x + SE_WORK_PAD;
		range = cw - vp.w;
		t = range > 0.0f ? (hi - ed->view.tx) / range : 0.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		sb->hth = Ui_Rect(sb->ht.x + (sb->ht.w - span) * t,
			sb->ht.y, span, w);
	}
}

/* t: 0 = 最左/最上, 1 = 最右/最下 */
static void sb_set_frac(UiSvgEdit *ed, int axis, float t)
{
	float ds, range, hi;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	if (axis == 1) {
		ds = ed->doc->width * ed->view.scale;
		range = ds + SE_WORK_PAD * 2.0f - ed->vp.w;
		hi = ed->vp.x + SE_WORK_PAD;
		ed->view.tx = hi - t * range;
	} else {
		ds = ed->doc->height * ed->view.scale;
		range = ds + SE_WORK_PAD * 2.0f - ed->vp.h;
		hi = ed->vp.y + SE_WORK_PAD;
		ed->view.ty = hi - t * range;
	}
	view_clamp(ed);
}

enum {
	SE_SB_NONE = 0,
	SE_SB_HTRACK,
	SE_SB_VTRACK,
	SE_SB_HTHUMB,
	SE_SB_VTHUMB
};

static int sb_hit(const SeSB *sb, float x, float y)
{
	if (sb->hasV) {
		if (Ui_RectContains(sb->vth, x, y)) return SE_SB_VTHUMB;
		if (Ui_RectContains(sb->vt, x, y)) return SE_SB_VTRACK;
	}
	if (sb->hasH) {
		if (Ui_RectContains(sb->hth, x, y)) return SE_SB_HTHUMB;
		if (Ui_RectContains(sb->ht, x, y)) return SE_SB_HTRACK;
	}
	return SE_SB_NONE;
}

static void s2w(const UiSvgEdit *ed, float sx, float sy, float *wx, float *wy)
{
	Svg_ViewWorld(&ed->view, sx, sy, wx, wy);
}

static void w2s(const UiSvgEdit *ed, float wx, float wy, float *sx, float *sy)
{
	Svg_ViewScreen(&ed->view, wx, wy, sx, sy);
}

static float snap_w(const UiSvgEdit *ed, float v)
{
	if (ed->gridOn && !alt_down())
		return s_round_grid(v, ed->gridStep);
	return v;
}

static void ed_invalidate(UiSvgEdit *ed)
{
	if (ed->ctx && ed->vp.w > 0.0f)
		Ui_InvalidateRect(ed->ctx, ed->vp);
}

/* ── 选择 ─────────────────────────────────────────────────────────── */

static int sel_has(const UiSvgEdit *ed, int idx)
{
	int i;
	for (i = 0; i < ed->selCount; i++)
		if (ed->sel[i] == idx) return 1;
	return 0;
}

static void sel_reserve(UiSvgEdit *ed, int n)
{
	int cap;
	if (n <= ed->selCap) return;
	cap = ed->selCap ? ed->selCap : 8;
	while (cap < n) cap *= 2;
	ed->sel = (int *)s_grow(ed->sel, (size_t)cap * sizeof(int));
	ed->selCap = cap;
}

static void sel_clear(UiSvgEdit *ed)
{
	ed->selCount = 0;
}

static void sel_add(UiSvgEdit *ed, int idx)
{
	if (idx < 0 || sel_has(ed, idx)) return;
	sel_reserve(ed, ed->selCount + 1);
	ed->sel[ed->selCount++] = idx;
}

static void sel_toggle(UiSvgEdit *ed, int idx)
{
	int i;
	for (i = 0; i < ed->selCount; i++) {
		if (ed->sel[i] == idx) {
			int k;
			for (k = i; k < ed->selCount - 1; k++)
				ed->sel[k] = ed->sel[k + 1];
			ed->selCount--;
			return;
		}
	}
	sel_add(ed, idx);
}

static void sel_only(UiSvgEdit *ed, int idx)
{
	sel_clear(ed);
	if (idx >= 0) sel_add(ed, idx);
}

static int sel_bounds(const UiSvgEdit *ed, GfxRect *out)
{
	int i, first = 1;
	GfxRect r = { 0, 0, 0, 0 };
	if (!ed->doc || ed->selCount <= 0) return 0;
	for (i = 0; i < ed->selCount; i++) {
		int idx = ed->sel[i];
		GfxRect b;
		if (idx < 0 || idx >= ed->doc->count) continue;
		b = SvgDoc_ElementBounds(ed->doc, idx);
		if (first) { r = b; first = 0; }
		else {
			float x1 = s_max(r.x + r.w, b.x + b.w);
			float y1 = s_max(r.y + r.h, b.y + b.h);
			if (b.x < r.x) r.x = b.x;
			if (b.y < r.y) r.y = b.y;
			r.w = x1 - r.x;
			r.h = y1 - r.y;
		}
	}
	if (first) return 0;
	*out = r;
	return 1;
}

/* 选区包围盒缓存: 键 = 选中索引 + 元素版本; 几何拖动/改样式后自动失效. */
static uint64_t sel_bounds_key(const UiSvgEdit *ed)
{
	uint64_t h = 1469598103934665603ull;
	int i;
	if (!ed->doc) return 0;
	h ^= (uint32_t)(uintptr_t)ed->doc; h *= 1099511628211ull;
	h ^= (uint32_t)ed->doc->count; h *= 1099511628211ull;
	for (i = 0; i < ed->selCount; i++) {
		int idx = ed->sel[i];
		h ^= (uint32_t)idx; h *= 1099511628211ull;
		if (idx >= 0 && idx < ed->doc->count) {
			h ^= (uint32_t)ed->doc->els[idx].version;
			h *= 1099511628211ull;
		}
	}
	return h;
}

static int sel_bounds_cached(const UiSvgEdit *ed, GfxRect *out)
{
	uint64_t k;
	UiSvgEdit *m = (UiSvgEdit *)ed;
	k = sel_bounds_key(ed);
	if (m->selBoundsValid && m->selBoundsKey == k) {
		if (!m->selBoundsHas) return 0;
		*out = m->selBoundsCache;
		return 1;
	}
	m->selBoundsHas = sel_bounds(ed, &m->selBoundsCache);
	m->selBoundsKey = k;
	m->selBoundsValid = 1;
	if (!m->selBoundsHas) return 0;
	*out = m->selBoundsCache;
	return 1;
}

static void sb_geom_cached(UiSvgEdit *ed, SeSB *out)
{
	float k[9];
	k[0] = ed->vp.x;
	k[1] = ed->vp.y;
	k[2] = ed->vp.w;
	k[3] = ed->vp.h;
	k[4] = ed->view.scale;
	k[5] = ed->view.tx;
	k[6] = ed->view.ty;
	k[7] = ed->doc ? ed->doc->width : 0.0f;
	k[8] = ed->doc ? ed->doc->height : 0.0f;
	if (ed->sbKeyValid && memcmp(k, ed->sbKey, sizeof(k)) == 0) {
		*out = ed->sbCache;
		return;
	}
	sb_geom(ed, out);
	ed->sbCache = *out;
	memcpy(ed->sbKey, k, sizeof(k));
	ed->sbKeyValid = 1;
}

/* 优先取选中的路径作为节点编辑对象 */
static int path_edit_index(const UiSvgEdit *ed)
{
	int i;
	if (!ed->doc) return -1;
	for (i = 0; i < ed->selCount; i++) {
		int idx = ed->sel[i];
		if (idx < 0 || idx >= ed->doc->count) continue;
		/* M7: 节点编辑暂只支持根级元素 (嵌套路径 M8+) */
		if (ed->doc->els[idx].parent >= 0) continue;
		if (ed->doc->els[idx].type == SVG_EL_PATH ||
			ed->doc->els[idx].type == SVG_EL_POLYGON) {
			if (ed->doc->els[idx].compound) continue;
			return idx;
		}
	}
	return -1;
}

/* ── 几何快照 (拖动起点) ─────────────────────────────────────────── */

static void geom_free(SvgGeom *g)
{
	Svg_PathFree(&g->path);
}

static void orig_clear(UiSvgEdit *ed)
{
	int i;
	for (i = 0; i < ed->origCount; i++) geom_free(&ed->orig[i]);
	ed->origCount = 0;
}

static void geom_from(SvgGeom *g, const SvgElement *e)
{
	int i;
	memset(g, 0, sizeof(*g));
	g->type = e->type;
	g->rot = e->rot;
	for (i = 0; i < 8; i++)
		((float *)&g->p)[i] = ((const float *)&e->p)[i];
	Svg_PathInit(&g->path);
	if (e->type == SVG_EL_PATH || e->type == SVG_EL_POLYGON)
		Svg_PathCopy(&g->path, &e->path);
}

static void orig_capture(UiSvgEdit *ed)
{
	int i;
	orig_clear(ed);
	if (ed->selCount <= 0) return;
	if (ed->origCap < ed->selCount) {
		int cap = ed->selCount < 8 ? 8 : ed->selCount * 2;
		ed->orig = (SvgGeom *)s_grow(ed->orig, (size_t)cap * sizeof(SvgGeom));
		ed->origCap = cap;
	}
	for (i = 0; i < ed->selCount; i++) {
		int idx = ed->sel[i];
		if (idx < 0 || idx >= ed->doc->count) continue;
		geom_from(&ed->orig[ed->origCount], &ed->doc->els[idx]);
		ed->origCount++;
	}
}

/* 把快照的几何平移/缩放后写回元素 */
static void geom_apply_move(SvgElement *e, const SvgGeom *g, float dx, float dy)
{
	e->type = g->type;
	e->rot = g->rot;
	if (e->type == SVG_EL_RECT) {
		e->p.rect.x = g->p[0] + dx;
		e->p.rect.y = g->p[1] + dy;
		e->p.rect.w = g->p[2];
		e->p.rect.h = g->p[3];
	} else if (e->type == SVG_EL_ELLIPSE) {
		e->p.ellipse.cx = g->p[0] + dx;
		e->p.ellipse.cy = g->p[1] + dy;
		e->p.ellipse.rx = g->p[2];
		e->p.ellipse.ry = g->p[3];
	} else if (e->type == SVG_EL_LINE) {
		e->p.line.x1 = g->p[0] + dx;
		e->p.line.y1 = g->p[1] + dy;
		e->p.line.x2 = g->p[2] + dx;
		e->p.line.y2 = g->p[3] + dy;
	} else {
		int i;
		Svg_PathFree(&e->path);
		Svg_PathCopy(&e->path, &g->path);
		for (i = 0; i < e->path.count; i++) {
			e->path.nodes[i].x += dx;
			e->path.nodes[i].y += dy;
		}
	}
}

static void geom_apply_scale(SvgElement *e, const SvgGeom *g,
	float ax, float ay, float sx, float sy)
{
	e->type = g->type;
	e->rot = g->rot;
	if (e->type == SVG_EL_RECT) {
		float x = ax + (g->p[0] - ax) * sx;
		float y = ay + (g->p[1] - ay) * sy;
		float w = g->p[2] * sx;
		float h = g->p[3] * sy;
		if (w < 0.0f) { x += w; w = -w; }
		if (h < 0.0f) { y += h; h = -h; }
		if (w < 1.0f) w = 1.0f;
		if (h < 1.0f) h = 1.0f;
		e->p.rect.x = x; e->p.rect.y = y;
		e->p.rect.w = w; e->p.rect.h = h;
	} else if (e->type == SVG_EL_ELLIPSE) {
		e->p.ellipse.cx = ax + (g->p[0] - ax) * sx;
		e->p.ellipse.cy = ay + (g->p[1] - ay) * sy;
		e->p.ellipse.rx = s_abs(g->p[2] * sx);
		e->p.ellipse.ry = s_abs(g->p[3] * sy);
		if (e->p.ellipse.rx < 0.5f) e->p.ellipse.rx = 0.5f;
		if (e->p.ellipse.ry < 0.5f) e->p.ellipse.ry = 0.5f;
	} else if (e->type == SVG_EL_LINE) {
		e->p.line.x1 = ax + (g->p[0] - ax) * sx;
		e->p.line.y1 = ay + (g->p[1] - ay) * sy;
		e->p.line.x2 = ax + (g->p[2] - ax) * sx;
		e->p.line.y2 = ay + (g->p[3] - ay) * sy;
	} else {
		int i, n;
		Svg_PathFree(&e->path);
		Svg_PathCopy(&e->path, &g->path);
		n = e->path.count;
		for (i = 0; i < n; i++) {
			SvgNode *nd = &e->path.nodes[i];
			nd->x = ax + (nd->x - ax) * sx;
			nd->y = ay + (nd->y - ay) * sy;
			nd->inX *= sx; nd->inY *= sy;
			nd->outX *= sx; nd->outY *= sy;
		}
		n = e->path.closed ? e->path.count : e->path.count - 1;
		for (i = 0; i < n && i < e->path.count; i++) {
			if (e->path.seg[i] == SVG_SEG_ARC) {
				e->path.arc[i].rx = s_abs(e->path.arc[i].rx * sx);
				e->path.arc[i].ry = s_abs(e->path.arc[i].ry * sy);
			}
		}
	}
}

static void geom_center(const SvgGeom *g, float *cx, float *cy)
{
	SvgElement tmp;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = g->type;
	tmp.rot = g->rot;
	memcpy(&tmp.p, g->p, sizeof(tmp.p));
	if (g->type == SVG_EL_PATH || g->type == SVG_EL_POLYGON) {
		tmp.path = g->path;
		Svg_ElementCenter(&tmp, cx, cy);
		tmp.path.nodes = NULL; /* 借用, 不能释放 */
	} else {
		Svg_ElementCenter(&tmp, cx, cy);
	}
}

/* ── 画刷/路径缓存 ───────────────────────────────────────────────── */

static void cache_entry_free(Gfx *g, SvgCacheEntry *c)
{
	if (!c) return;
	if (c->path) Gfx_PathDestroy(g, c->path);
	if (c->fill) Gfx_BrushDestroy(g, c->fill);
	if (c->stroke) Gfx_BrushDestroy(g, c->stroke);
	c->path = NULL;
	c->fill = NULL;
	c->stroke = NULL;
	c->el = NULL;
	c->version = 0;
}

static void cache_clear(UiSvgEdit *ed)
{
	int i;
	if (!ed->cache) return;
	for (i = 0; i < ed->cacheCap; i++)
		cache_entry_free(ed->gfx, &ed->cache[i]);
}

static GfxColor with_opacity(GfxColor c, float op)
{
	int a = (int)((c >> 24) & 0xFF);
	if (op < 0.0f) op = 0.0f;
	if (op > 1.0f) op = 1.0f;
	a = (int)((float)a * op + 0.5f);
	return (c & 0x00FFFFFFu) | ((GfxColor)(a < 0 ? 0 : (a > 255 ? 255 : a)) << 24);
}

static GfxBrush *make_paint_brush(Gfx *g, const SvgPaint *p,
	const SvgStyle *st, GfxRect box)
{
	if (!p) return NULL;
	if (p->kind == SVG_PAINT_SOLID)
		return Gfx_SolidBrush(g, with_opacity(p->color, st->opacity));
	if ((p->kind == SVG_PAINT_LINEAR || p->kind == SVG_PAINT_RADIAL) &&
		p->stops && p->stopCount >= 2) {
		GfxGradStop stops[SE_MAX_GRAD_STOPS];
		int i, n = p->stopCount;
		if (n > SE_MAX_GRAD_STOPS) n = SE_MAX_GRAD_STOPS;
		for (i = 0; i < n; i++) {
			stops[i].offset = p->stops[i].offset;
			stops[i].color = with_opacity(p->stops[i].color, st->opacity);
		}
		if (box.w <= 0.0f) box.w = 1.0f;
		if (box.h <= 0.0f) box.h = 1.0f;
		if (p->kind == SVG_PAINT_RADIAL)
			return Gfx_RadialGradientBrush(g, box, p->cx, p->cy, p->r,
				stops, n);
		return Gfx_LinearGradientBrush(g, box, p->angleDeg, stops, n);
	}
	return NULL;
}

static void build_element_path(GfxPath *p, const SvgElement *e)
{
	Gfx_PathReset(p);
	switch (e->type) {
	case SVG_EL_RECT: {
		float x = e->p.rect.x, y = e->p.rect.y;
		float w = e->p.rect.w, h = e->p.rect.h;
		Gfx_PathMoveTo(p, x, y);
		Gfx_PathLineTo(p, x + w, y);
		Gfx_PathLineTo(p, x + w, y + h);
		Gfx_PathLineTo(p, x, y + h);
		Gfx_PathClose(p);
		break;
	}
	case SVG_EL_ELLIPSE: {
		float cx = e->p.ellipse.cx, cy = e->p.ellipse.cy;
		float rx = e->p.ellipse.rx, ry = e->p.ellipse.ry;
		float kx = rx * 0.5522847498f, ky = ry * 0.5522847498f;
		Gfx_PathMoveTo(p, cx + rx, cy);
		Gfx_PathCubicTo(p, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
		Gfx_PathCubicTo(p, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
		Gfx_PathCubicTo(p, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
		Gfx_PathCubicTo(p, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
		Gfx_PathClose(p);
		break;
	}
	case SVG_EL_LINE:
		Gfx_PathMoveTo(p, e->p.line.x1, e->p.line.y1);
		Gfx_PathLineTo(p, e->p.line.x2, e->p.line.y2);
		break;
	default: {
		const SvgPath *sp = &e->path;
		int i;
		if (sp->count <= 0) break;
		Gfx_PathMoveTo(p, sp->nodes[0].x, sp->nodes[0].y);
		for (i = 0; i < sp->count - 1; i++) {
			const SvgNode *a = &sp->nodes[i];
			const SvgNode *b = &sp->nodes[i + 1];
			if (b->subStart) {
				Gfx_PathMoveTo(p, b->x, b->y);
				continue;
			}
			if (sp->seg[i] == SVG_SEG_CUBIC) {
				Gfx_PathCubicTo(p, a->x + a->outX, a->y + a->outY,
					b->x + b->inX, b->y + b->inY, b->x, b->y);
			} else if (sp->seg[i] == SVG_SEG_ARC) {
				Gfx_PathArcTo(p, sp->arc[i].rx, sp->arc[i].ry,
					sp->arc[i].rotDeg, sp->arc[i].large,
					sp->arc[i].sweep, b->x, b->y);
			} else {
				Gfx_PathLineTo(p, b->x, b->y);
			}
		}
		if (sp->closed) {
			/* 闭合: 逐子路径处理末段 */
			int start = 0;
			while (start < sp->count) {
				int end = start + 1;
				const SvgNode *a, *b;
				while (end < sp->count && !sp->nodes[end].subStart) end++;
				a = &sp->nodes[end - 1];
				b = &sp->nodes[start];
				if (sp->seg[end - 1] == SVG_SEG_CUBIC) {
					Gfx_PathCubicTo(p, a->x + a->outX, a->y + a->outY,
						b->x + b->inX, b->y + b->inY, b->x, b->y);
				} else if (sp->seg[end - 1] == SVG_SEG_ARC) {
					Gfx_PathArcTo(p, sp->arc[end - 1].rx, sp->arc[end - 1].ry,
						sp->arc[end - 1].rotDeg, sp->arc[end - 1].large,
						sp->arc[end - 1].sweep, b->x, b->y);
				}
				Gfx_PathClose(p);
				start = end;
			}
		}
		break;
	}
	}
}

static SvgCacheEntry *cache_ensure(UiSvgEdit *ed, Gfx *g, int idx, SvgElement *e)
{
	SvgCacheEntry *c;
	GfxRect box;
	if (idx >= ed->cacheCap) {
		int old = ed->cacheCap;
		int cap = old ? old : 16;
		while (cap <= idx) cap *= 2;
		ed->cache = (SvgCacheEntry *)s_grow(ed->cache,
			(size_t)cap * sizeof(SvgCacheEntry));
		ed->cacheCap = cap;
	}
	c = &ed->cache[idx];
	if (c->el == e && c->version == e->version && c->path)
		return c;
	cache_entry_free(g, c);
	c->el = e;
	c->version = e->version;
	c->path = Gfx_PathCreate(g);
	if (!c->path) return c;
	build_element_path(c->path, e);
	box = Svg_ElementLocalBounds(e);
	if (box.w <= 0.0f) box.w = 1.0f;
	if (box.h <= 0.0f) box.h = 1.0f;
	c->fill = make_paint_brush(g, &e->style.fill, &e->style, box);
	c->stroke = make_paint_brush(g, &e->style.stroke, &e->style, box);
	return c;
}

/* ── 绘制 ─────────────────────────────────────────────────────────── */

static GfxFont *text_font_(UiSvgEdit *ed, const SvgElement *e)
{
	const char *fam = e->family[0] ? e->family : "Segoe UI";
	float size = e->fontSize > 0.0f ? e->fontSize : 24.0f;
	int wt = e->fontWeight >= 600 ? 700 : 400;
	int i;
	wchar_t wf[64];
	GfxFont *f;
	for (i = 0; i < ed->fontCount; i++) {
		if (ed->fontWeights[i] == wt &&
			ed->fontSizes[i] == size &&
			lstrcmpA(ed->fontFam[i], fam) == 0)
			return ed->fonts[i];
	}
	for (i = 0; i < 63 && fam[i]; i++) wf[i] = (wchar_t)(unsigned char)fam[i];
	wf[i] = 0;
	f = Gfx_CreateFontFamily(ed->gfx, wf, size, wt);
	if (!f) return NULL;
	if (ed->fontCount >= 8) {
		int k;
		Gfx_DestroyFont(ed->gfx, ed->fonts[0]);
		for (k = 1; k < ed->fontCount; k++) {
			ed->fonts[k - 1] = ed->fonts[k];
			ed->fontSizes[k - 1] = ed->fontSizes[k];
			ed->fontWeights[k - 1] = ed->fontWeights[k];
			memcpy(ed->fontFam[k - 1], ed->fontFam[k], 64);
		}
		ed->fontCount--;
	}
	ed->fonts[ed->fontCount] = f;
	ed->fontSizes[ed->fontCount] = size;
	ed->fontWeights[ed->fontCount] = wt;
	{
		int k;
		for (k = 0; k < 63 && fam[k]; k++) ed->fontFam[ed->fontCount][k] = fam[k];
		ed->fontFam[ed->fontCount][k] = 0;
	}
	ed->fontCount++;
	return f;
}

/* 测量 (按行) 并缓存到元素 */
static void text_measure_(UiSvgEdit *ed, int idx, SvgElement *e)
{
	const char *s = e->text ? e->text : "";
	const char *ls = s, *p;
	float maxW = 0.0f;
	int lines = 0;
	GfxFont *f;
	if (e->measVer == e->version && e->textW > 0.0f) return;
	f = text_font_(ed, e);
	if (!f) return;
	for (p = s; ; p++) {
		if (*p == '\n' || *p == 0) {
			char buf[512];
			int n = (int)(p - ls);
			float w;
			if (n > 511) n = 511;
			memcpy(buf, ls, (size_t)n);
			buf[n] = 0;
			w = Gfx_TextWidth(ed->gfx, f, buf);
			if (w > maxW) maxW = w;
			lines++;
			if (*p == 0) break;
			ls = p + 1;
		}
	}
	{
		float lh = e->fontSize * (e->lineH > 0.0f ? e->lineH : 1.25f);
		SvgDoc_SetTextBounds(ed->doc, idx, maxW, lh * (float)lines, lines);
	}
}

static void draw_text_(UiSvgEdit *ed, Gfx *g, int idx)
{
	SvgElement *e = &ed->doc->els[idx];
	UiTheme *t = ed->ctx->theme;
	GfxFont *f;
	UiColor col;
	const char *s, *ls, *p;
	float axf, af, lh, blockL;
	int line = 0;
	if (!e->text) return;
	text_measure_(ed, idx, e);
	f = text_font_(ed, e);
	if (!f) return;
	axf = e->anchor == 1 ? 0.5f : (e->anchor == 2 ? 1.0f : 0.0f);
	af = e->align == 1 ? 0.5f : (e->align == 2 ? 1.0f : 0.0f);
	lh = e->fontSize * (e->lineH > 0.0f ? e->lineH : 1.25f);
	blockL = e->p.text.x - axf * e->textW;
	col = ((e->style.setMask & SVG_STYLE_FILL) &&
		e->style.fill.kind == SVG_PAINT_SOLID) ? e->style.fill.color
		: (t ? t->text : 0xFF202020u);
	s = e->text;
	ls = s;
	for (p = s; ; p++) {
		if (*p == '\n' || *p == 0) {
			char buf[512];
			int n = (int)(p - ls);
			float w, x, y;
			if (n > 511) n = 511;
			memcpy(buf, ls, (size_t)n);
			buf[n] = 0;
			w = Gfx_TextWidth(g, f, buf);
			x = blockL + (e->textW - w) * af;
			y = e->p.text.y + (float)line * lh;
			Gfx_Text(g, f, buf, Ui_Rect(x, y, w + 4.0f, lh), col, 0);
			line++;
			if (*p == 0) break;
			ls = p + 1;
		}
	}
}

static void draw_element(UiSvgEdit *ed, Gfx *g, int idx)
{
	SvgElement *e = &ed->doc->els[idx];
	SvgCacheEntry *c;
	int fillable;
	int xfPush = 0;
	float savedRot = 0.0f;
	if (!e->visible || e->type == SVG_EL_GROUP) return;
	if (e->type == SVG_EL_TEXT) {
		if (e->xf.has) {
			float m[6];
			SvgDoc_SelfMatrix(ed->doc, idx, m);
			Gfx_PushMatrix(g, m);
			draw_text_(ed, g, idx);
			Gfx_PopTransform(g);
		} else {
			draw_text_(ed, g, idx);
		}
		return;
	}
	if (e->xf.has) {
		float m[6];
		SvgDoc_SelfMatrix(ed->doc, idx, m);
		Gfx_PushMatrix(g, m);
		xfPush = 1;
		savedRot = e->rot;
		e->rot = 0.0f;   /* 路径缓存/绘制用局部几何 */
	}
	c = cache_ensure(ed, g, idx, e);
	if (!c->path) { if (xfPush) { e->rot = savedRot; Gfx_PopTransform(g); } return; }
	Gfx_PathSetFillRule(c->path, e->style.fillRule);
	fillable = e->type != SVG_EL_LINE;
	if (fillable && (e->type == SVG_EL_PATH || e->type == SVG_EL_POLYGON)
		&& !e->path.closed)
		fillable = 0;
	if (!xfPush && e->rot != 0.0f) {
		float cx, cy;
		Svg_ElementCenter(e, &cx, &cy);
		Gfx_PushRotate(g, cx, cy, e->rot);
	}
	if (fillable && e->style.fill.kind != SVG_PAINT_NONE && c->fill)
		Gfx_FillPathBrush(g, c->path, c->fill);
	if (e->style.stroke.kind != SVG_PAINT_NONE &&
		e->style.strokeWidth > 0.0f && c->stroke) {
		GfxStrokeStyle ss;
		ss.cap = e->style.cap;
		ss.join = e->style.join;
		ss.miterLimit = 4.0f;
		ss.dashes = e->style.dashCount > 0 ? e->style.dash : NULL;
		ss.dashCount = e->style.dashCount;
		Gfx_StrokePath(g, c->path, c->stroke, e->style.strokeWidth, &ss);
	}
	if (xfPush) {
		e->rot = savedRot;
		Gfx_PopTransform(g);
	} else if (e->rot != 0.0f) {
		Gfx_PopTransform(g);
	}
}

/* DFS: 组压自身矩阵后递归子元素; 叶子走 draw_element */
static void draw_subtree_(UiSvgEdit *ed, Gfx *g, int idx)
{
	SvgElement *e;
	if (!ed || idx < 0 || idx >= ed->doc->count) return;
	if (!SvgDoc_IsVisible(ed->doc, idx)) return;
	e = &ed->doc->els[idx];
	if (e->type == SVG_EL_GROUP) {
		float m[6];
		if (e->xf.has) {
			SvgDoc_SelfMatrix(ed->doc, idx, m);
			Gfx_PushMatrix(g, m);
			{
				int i;
				for (i = 0; i < e->childCount; i++)
					draw_subtree_(ed, g, e->children[i]);
			}
			Gfx_PopTransform(g);
		} else {
			int i;
			for (i = 0; i < e->childCount; i++)
				draw_subtree_(ed, g, e->children[i]);
		}
		return;
	}
	draw_element(ed, g, idx);
}

static void handle_pos(const GfxRect *box, int i, float *hx, float *hy)
{
	float x0 = box->x, y0 = box->y;
	float x1 = box->x + box->w, y1 = box->y + box->h;
	float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
	switch (i) {
	case 0: *hx = x0; *hy = y0; break;
	case 1: *hx = mx; *hy = y0; break;
	case 2: *hx = x1; *hy = y0; break;
	case 3: *hx = x1; *hy = my; break;
	case 4: *hx = x1; *hy = y1; break;
	case 5: *hx = mx; *hy = y1; break;
	case 6: *hx = x0; *hy = y1; break;
	default: *hx = x0; *hy = my; break;
	}
}

static void handle_anchor(const GfxRect *box, int i, float *ax, float *ay)
{
	float x0 = box->x, y0 = box->y;
	float x1 = box->x + box->w, y1 = box->y + box->h;
	float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
	switch (i) {
	case 0: *ax = x1; *ay = y1; break;
	case 1: *ax = mx; *ay = y1; break;
	case 2: *ax = x0; *ay = y1; break;
	case 3: *ax = x0; *ay = my; break;
	case 4: *ax = x0; *ay = y0; break;
	case 5: *ax = mx; *ay = y0; break;
	case 6: *ax = x1; *ay = y0; break;
	default: *ax = x1; *ay = my; break;
	}
}

static int screen_box(const UiSvgEdit *ed, GfxRect worldBox, GfxRect *out)
{
	float x0, y0, x1, y1;
	if (worldBox.w <= 0.0f && worldBox.h <= 0.0f) return 0;
	w2s(ed, worldBox.x, worldBox.y, &x0, &y0);
	w2s(ed, worldBox.x + worldBox.w, worldBox.y + worldBox.h, &x1, &y1);
	out->x = s_min(x0, x1);
	out->y = s_min(y0, y1);
	out->w = s_abs(x1 - x0);
	out->h = s_abs(y1 - y0);
	return 1;
}

static void draw_selection(UiSvgEdit *ed, UiContext *ui, GfxRect worldBox)
{
	Gfx *g = ui->gfx;
	UiTheme *t = ui->theme;
	GfxColor accent = t->primary.base;
	GfxColor handleBg = t->card;
	GfxRect box;
	int i;
	if (!screen_box(ed, worldBox, &box)) return;
	if (box.w < 2.0f) box.w = 2.0f;
	if (box.h < 2.0f) box.h = 2.0f;
	Gfx_StrokeRoundRect(g, box, 0.0f, accent, 1.0f);
	if (ed->tool != UI_SVG_TOOL_SELECT) return;
	for (i = 0; i < 8; i++) {
		float hx, hy;
		handle_pos(&box, i, &hx, &hy);
		Gfx_FillRoundRect(g, Ui_Rect(hx - SE_HANDLE * 0.5f,
			hy - SE_HANDLE * 0.5f, SE_HANDLE, SE_HANDLE),
			1.5f, handleBg);
		Gfx_StrokeRoundRect(g, Ui_Rect(hx - SE_HANDLE * 0.5f,
			hy - SE_HANDLE * 0.5f, SE_HANDLE, SE_HANDLE),
			1.5f, accent, 1.0f);
	}
	{
		float rx = box.x + box.w * 0.5f, ry = box.y - SE_ROT_DIST;
		Gfx_Line(g, box.x + box.w * 0.5f, box.y, rx, ry, accent, 1.0f);
		Gfx_FillEllipse(g, rx, ry, 4.0f, 4.0f, handleBg);
		Gfx_StrokeEllipse(g, rx, ry, 4.0f, accent, 1.0f);
	}
	if (ed->snapGXOn) {
		float sx, sy;
		w2s(ed, ed->snapGX, ed->snapGY, &sx, &sy);
		Gfx_Line(g, sx, ed->vp.y, sx, ed->vp.y + ed->vp.h,
			Ui_ColorAlpha(accent, 150), 1.0f);
	}
	if (ed->snapGYOn) {
		float sx, sy;
		w2s(ed, ed->snapGX, ed->snapGY, &sx, &sy);
		Gfx_Line(g, ed->vp.x, sy, ed->vp.x + ed->vp.w, sy,
			Ui_ColorAlpha(accent, 150), 1.0f);
	}
}

/* 渐变手柄: 仅选中单元素且自身 fill 为渐变时可用.
 * p0/p1 为屏幕坐标; radialOut=1 表示径向. 返回 1 = 有手柄. */
static int grad_handle_(UiSvgEdit *ed, int idx, float *x0, float *y0,
	float *x1, float *y1, int *radialOut)
{
	SvgElement *e;
	GfxRect b;
	float cx, cy, dx, dy, lx0, ly0, lx1, ly1, wx, wy;
	if (!ed || idx < 0 || idx >= ed->doc->count) return 0;
	e = &ed->doc->els[idx];
	if (e->type == SVG_EL_GROUP) return 0;
	if (e->style.fill.kind != SVG_PAINT_LINEAR &&
		e->style.fill.kind != SVG_PAINT_RADIAL) return 0;
	if (e->style.fill.stopCount < 2) return 0;
	b = Svg_ElementLocalBounds(e);
	if (b.w <= 0.5f || b.h <= 0.5f) return 0;
	cx = b.x + b.w * 0.5f;
	cy = b.y + b.h * 0.5f;
	if (e->style.fill.kind == SVG_PAINT_RADIAL) {
		lx0 = b.x + e->style.fill.cx * b.w;
		ly0 = b.y + e->style.fill.cy * b.h;
		lx1 = lx0 + e->style.fill.r * b.w * 0.7071f;
		ly1 = ly0 + e->style.fill.r * b.h * 0.7071f;
		*radialOut = 1;
	} else {
		float ang = e->style.fill.angleDeg * SE_DEG;
		float half = (b.w * s_abs(s_cos(ang)) + b.h * s_abs(s_sin(ang))) * 0.5f;
		dx = s_cos(ang) * half * 0.4f;
		dy = s_sin(ang) * half * 0.4f;
		lx0 = cx - dx; ly0 = cy - dy;
		lx1 = cx + dx; ly1 = cy + dy;
		*radialOut = 0;
	}
	Svg_DocLocalToWorld(ed->doc, idx, lx0, ly0, &wx, &wy);
	w2s(ed, wx, wy, x0, y0);
	Svg_DocLocalToWorld(ed->doc, idx, lx1, ly1, &wx, &wy);
	w2s(ed, wx, wy, x1, y1);
	return 1;
}

static int grad_handle_hit_(UiSvgEdit *ed, int *part)
{
	float x0, y0, x1, y1, wx, wy;
	int radial = 0;
	int idx = ed->selCount == 1 ? ed->sel[0] : -1;
	GfxRect wb;
	if (!grad_handle_(ed, idx, &x0, &y0, &x1, &y1, &radial)) return -1;
	/* 只允许在选中元素范围内抓渐变手柄: 手柄/半径可能伸出元素外,
	 * 若跨到其它对象上会抢走点选 (用户无法再选中被覆盖的元素). */
	s2w(ed, ed->mx, ed->my, &wx, &wy);
	wb = SvgDoc_ElementBounds(ed->doc, idx);
	if (wx < wb.x || wy < wb.y ||
		wx > wb.x + wb.w || wy > wb.y + wb.h)
		return -1;
	if (pt_dist2(ed->mx, ed->my, x0, y0) <= 100.0f) { *part = 0; return idx; }
	if (pt_dist2(ed->mx, ed->my, x1, y1) <= 100.0f) { *part = 1; return idx; }
	return -1;
}

static void draw_grad_handle_(UiSvgEdit *ed, UiContext *ui)
{
	float x0, y0, x1, y1;
	int radial = 0;
	int idx = ed->selCount == 1 ? ed->sel[0] : -1;
	UiTheme *t = ui->theme;
	if (!grad_handle_(ed, idx, &x0, &y0, &x1, &y1, &radial)) return;
	Gfx_Line(ui->gfx, x0, y0, x1, y1, t->primary.base, 1.0f);
	Gfx_FillEllipse(ui->gfx, x0, y0, 4.0f, 4.0f, t->card);
	Gfx_StrokeEllipse(ui->gfx, x0, y0, 4.0f, t->primary.base, 1.0f);
	Gfx_FillEllipse(ui->gfx, x1, y1, 4.0f, 4.0f, t->warning.base);
	Gfx_StrokeEllipse(ui->gfx, x1, y1, 4.0f, t->warning.base, 1.0f);
	(void)radial;
}

static void grad_drag_update_(UiSvgEdit *ed, int idx, int part)
{
	SvgElement *e;
	GfxRect b;
	float wx, wy, lx, ly, cx, cy;
	if (idx < 0 || idx >= ed->doc->count) return;
	e = &ed->doc->els[idx];
	b = Svg_ElementLocalBounds(e);
	if (b.w <= 0.5f || b.h <= 0.5f) return;
	s2w(ed, ed->mx, ed->my, &wx, &wy);
	Svg_WorldToLocal(e, wx, wy, &lx, &ly);
	cx = b.x + e->style.fill.cx * b.w;
	cy = b.y + e->style.fill.cy * b.h;
	if (e->style.fill.kind == SVG_PAINT_RADIAL) {
		if (part == 0) {
			float ncx = (lx - b.x) / b.w, ncy = (ly - b.y) / b.h;
			e->style.fill.cx = ncx < 0.0f ? 0.0f : (ncx > 1.0f ? 1.0f : ncx);
			e->style.fill.cy = ncy < 0.0f ? 0.0f : (ncy > 1.0f ? 1.0f : ncy);
		} else {
			float nx = (lx - cx) / b.w, ny = (ly - cy) / b.h;
			float r = s_sqrt(nx * nx + ny * ny);
			/* UI 手柄把 r 限制在 1 内, 避免手柄甩到相邻元素上抢点选 */
			e->style.fill.r = r < 0.02f ? 0.02f : (r > 1.0f ? 1.0f : r);
		}
	} else {
		float dx = lx - cx, dy = ly - cy;
		float deg = s_atan2(dy, dx) * (180.0f / SE_PI);
		if (deg < 0.0f) deg += 360.0f;
		e->style.fill.angleDeg = deg;
	}
	Svg_ElementTouch(e);
	cache_clear(ed);
	ed_invalidate(ed);
}
static void seg_point(const SvgElement *e, int si, float t, float *x, float *y)
{
	const SvgPath *p = &e->path;
	const SvgNode *a = &p->nodes[si];
	const SvgNode *b = &p->nodes[(si + 1) % p->count];
	if (p->seg[si] == SVG_SEG_CUBIC) {
		float u = 1.0f - t;
		float w0 = u * u * u, w1 = 3.0f * u * u * t;
		float w2 = 3.0f * u * t * t, w3 = t * t * t;
		*x = a->x * w0 + (a->x + a->outX) * w1
			+ (b->x + b->inX) * w2 + b->x * w3;
		*y = a->y * w0 + (a->y + a->outY) * w1
			+ (b->y + b->inY) * w2 + b->y * w3;
		return;
	}
	if (p->seg[si] == SVG_SEG_ARC) {
		float cx, cy, th, dth;
		if (Svg_ArcGeometry(a->x, a->y, b->x, b->y, &p->arc[si],
			&cx, &cy, &th, &dth)) {
			float phi = p->arc[si].rotDeg * SE_DEG;
			float ang = th + dth * t;
			float cp = s_cos(phi), sp = s_sin(phi);
			float ca = s_cos(ang), sa = s_sin(ang);
			*x = cx + p->arc[si].rx * ca * cp - p->arc[si].ry * sa * sp;
			*y = cy + p->arc[si].rx * ca * sp + p->arc[si].ry * sa * cp;
			return;
		}
	}
	*x = a->x + (b->x - a->x) * t;
	*y = a->y + (b->y - a->y) * t;
}

static float pt_dist2(float x0, float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0;
	return dx * dx + dy * dy;
}

/* 最近的路径段与参数 t */
static int nearest_seg(const SvgElement *e, float lx, float ly,
	int *segOut, float *tOut, float *distOut)
{
	const SvgPath *p = &e->path;
	int sc = p->closed ? p->count : p->count - 1;
	int si, k, best = -1;
	float bestT = 0.0f, bestD = 1e30f;
	for (si = 0; si < sc; si++) {
		int steps = p->seg[si] == SVG_SEG_LINE ? 1 : 18;
		for (k = 0; k <= steps; k++) {
			float t = (float)k / (float)steps;
			float x, y, d;
			seg_point(e, si, t, &x, &y);
			d = pt_dist2(lx, ly, x, y);
			if (d < bestD) { bestD = d; best = si; bestT = t; }
		}
	}
	if (best < 0) return 0;
	if (segOut) *segOut = best;
	if (tOut) *tOut = bestT < 0.05f ? 0.05f : (bestT > 0.95f ? 0.95f : bestT);
	if (distOut) *distOut = s_sqrt(bestD);
	return 1;
}

static int pen_near_first(const UiSvgEdit *ed, const SvgElement *e, float mx, float my)
{
	float sx, sy, wx, wy;
	if (e->path.count < 2) return 0;
	Svg_LocalToWorld(e, e->path.nodes[0].x, e->path.nodes[0].y, &wx, &wy);
	w2s(ed, wx, wy, &sx, &sy);
	return pt_dist2(sx, sy, mx, my) <= 8.0f * 8.0f;
}

static void draw_pen_overlay(UiSvgEdit *ed, UiContext *ui, Gfx *g)
{
	UiTheme *t = ui->theme;
	SvgElement *e;
	int i, n;
	if (!ed->penActive || ed->penIndex < 0 || ed->penIndex >= ed->doc->count)
		return;
	e = &ed->doc->els[ed->penIndex];
	n = e->path.count;
	for (i = 0; i < n; i++) {
		float wx, wy, sx, sy;
		Svg_LocalToWorld(e, e->path.nodes[i].x, e->path.nodes[i].y, &wx, &wy);
		w2s(ed, wx, wy, &sx, &sy);
		Gfx_FillRoundRect(g, Ui_Rect(sx - 3.5f, sy - 3.5f, 7, 7),
			1.5f, t->card);
		Gfx_StrokeRoundRect(g, Ui_Rect(sx - 3.5f, sy - 3.5f, 7, 7),
			1.5f, t->primary.base, 1.0f);
	}
	if (n > 0 && ed->mouseIn) {
		float lx, ly, wx, wy, sx, sy;
		/* 鼠标已是世界坐标, 无需 local→world 往返 */
		w2s(ed, ed->mouseWorldX, ed->mouseWorldY, &sx, &sy);
		Svg_LocalToWorld(e, e->path.nodes[n - 1].x, e->path.nodes[n - 1].y,
			&wx, &wy);
		w2s(ed, wx, wy, &lx, &ly);
		Gfx_Line(g, lx, ly, sx, sy, t->primary.base, 1.0f);
	}
	if (ed->penArcStage == 1) {
		float sx, sy;
		w2s(ed, ed->penMidX, ed->penMidY, &sx, &sy);
		Gfx_FillEllipse(g, sx, sy, 3.5f, 3.5f, t->gold);
	}
	/* 拖动中: 锚点落在按下处, 拖拽向量同时给出切向(角度)与力度. */
	if (ed->drag == SE_DRAG_PEN_POINT && ed->penDragging) {
		float awx, awy, hwx, hwy, alx, aly, hlx, hly;
		float asx, asy, osx, osy, isx, isy, dx, dy;
		awx = ed->penDownWX;
		awy = ed->penDownWY;
		hwx = ed->mouseWorldX;
		hwy = ed->mouseWorldY;
		Svg_WorldToLocal(e, awx, awy, &alx, &aly);
		Svg_WorldToLocal(e, hwx, hwy, &hlx, &hly);
		dx = hlx - alx;
		dy = hly - aly;
		Svg_LocalToWorld(e, alx, aly, &awx, &awy);
		w2s(ed, awx, awy, &asx, &asy);
		if (ed->penNext == SVG_SEG_CUBIC) {
			Svg_LocalToWorld(e, alx + dx, aly + dy, &awx, &awy);
			w2s(ed, awx, awy, &osx, &osy);
			Svg_LocalToWorld(e, alx - dx, aly - dy, &awx, &awy);
			w2s(ed, awx, awy, &isx, &isy);
			Gfx_Line(g, asx, asy, osx, osy, t->text3, 1.0f);
			Gfx_Line(g, asx, asy, isx, isy, t->text3, 1.0f);
			Gfx_FillEllipse(g, osx, osy, 3.0f, 3.0f, t->card);
			Gfx_StrokeEllipse(g, osx, osy, 3.0f, t->primary.base, 1.0f);
			Gfx_FillEllipse(g, isx, isy, 3.0f, 3.0f, t->card);
			Gfx_StrokeEllipse(g, isx, isy, 3.0f, t->primary.base, 1.0f);
		}
		Gfx_FillRoundRect(g, Ui_Rect(asx - 3.5f, asy - 3.5f, 7, 7),
			1.5f, t->card);
		Gfx_StrokeRoundRect(g, Ui_Rect(asx - 3.5f, asy - 3.5f, 7, 7),
			1.5f, t->primary.base, 1.0f);
		if (!ed->penJustStarted && n > 0 && ed->penNext == SVG_SEG_CUBIC) {
			const SvgNode *ln = &e->path.nodes[n - 1];
			float p0x = ln->x, p0y = ln->y;
			float c1x = p0x + (ln->hasOut ? ln->outX : 0.0f);
			float c1y = p0y + (ln->hasOut ? ln->outY : 0.0f);
			float c2x = alx - dx, c2y = aly - dy;
			float lx, ly;
			int k;
			Svg_LocalToWorld(e, p0x, p0y, &awx, &awy);
			w2s(ed, awx, awy, &lx, &ly);
			for (k = 1; k <= 24; k++) {
				float u = (float)k / 24.0f, v = 1.0f - u;
				float bx = v * v * v * p0x + 3 * v * v * u * c1x +
					3 * v * u * u * c2x + u * u * u * alx;
				float by = v * v * v * p0y + 3 * v * v * u * c1y +
					3 * v * u * u * c2y + u * u * u * aly;
				float qx, qy;
				Svg_LocalToWorld(e, bx, by, &awx, &awy);
				w2s(ed, awx, awy, &qx, &qy);
				Gfx_Line(g, lx, ly, qx, qy, t->primary.base, 1.5f);
				lx = qx;
				ly = qy;
			}
		}
	}
	}

static void draw_node_overlay(UiSvgEdit *ed, UiContext *ui, Gfx *g)
{
	UiTheme *t = ui->theme;
	int idx = path_edit_index(ed);
	SvgElement *e;
	int i, sc;
	if (ed->tool != UI_SVG_TOOL_NODE || idx < 0) return;
	e = &ed->doc->els[idx];
	sc = e->path.closed ? e->path.count : e->path.count - 1;

	for (i = 0; i < sc; i++) {
		const SvgNode *a = &e->path.nodes[i];
		const SvgNode *b = &e->path.nodes[(i + 1) % e->path.count];
		float wx0, wy0, wx1, wy1;
		float sx0, sy0, sx1, sy1;
		Svg_LocalToWorld(e, a->x, a->y, &wx0, &wy0);
		Svg_LocalToWorld(e, b->x, b->y, &wx1, &wy1);
		w2s(ed, wx0, wy0, &sx0, &sy0);
		w2s(ed, wx1, wy1, &sx1, &sy1);
		if (e->path.seg[i] == SVG_SEG_ARC) {
			float mx, my, tx, ty;
			seg_point(e, i, 0.5f, &mx, &my);
			Svg_LocalToWorld(e, mx, my, &tx, &ty);
			w2s(ed, tx, ty, &sx1, &sy1);
			Gfx_FillEllipse(g, sx1, sy1, 4.0f, 4.0f, t->orange);
		}
	}

	for (i = 0; i < e->path.count; i++) {
		const SvgNode *nd = &e->path.nodes[i];
		float wx, wy, sx, sy;
		float hx, hy, hsx, hsy;
		Svg_LocalToWorld(e, nd->x, nd->y, &wx, &wy);
		w2s(ed, wx, wy, &sx, &sy);
		if (nd->hasIn) {
			Svg_LocalToWorld(e, nd->x + nd->inX, nd->y + nd->inY, &hx, &hy);
			w2s(ed, hx, hy, &hsx, &hsy);
			Gfx_Line(g, sx, sy, hsx, hsy, t->text3, 1.0f);
			Gfx_FillEllipse(g, hsx, hsy, 3.0f, 3.0f, t->card);
			Gfx_StrokeEllipse(g, hsx, hsy, 3.0f, t->primary.base, 1.0f);
		}
		if (nd->hasOut) {
			Svg_LocalToWorld(e, nd->x + nd->outX, nd->y + nd->outY, &hx, &hy);
			w2s(ed, hx, hy, &hsx, &hsy);
			Gfx_Line(g, sx, sy, hsx, hsy, t->text3, 1.0f);
			Gfx_FillEllipse(g, hsx, hsy, 3.0f, 3.0f, t->card);
			Gfx_StrokeEllipse(g, hsx, hsy, 3.0f, t->primary.base, 1.0f);
		}
		if (nd->type == SVG_NODE_SMOOTH) {
			Gfx_FillEllipse(g, sx, sy, 3.5f, 3.5f, t->card);
			Gfx_StrokeEllipse(g, sx, sy, 3.5f, t->primary.base, 1.0f);
		} else {
			Gfx_FillRoundRect(g, Ui_Rect(sx - 3.5f, sy - 3.5f, 7, 7),
				1.0f, t->card);
			Gfx_StrokeRoundRect(g, Ui_Rect(sx - 3.5f, sy - 3.5f, 7, 7),
				1.0f, t->primary.base, 1.0f);
		}
		if (i == ed->nodeSel || i == ed->hoverNode)
			Gfx_StrokeRoundRect(g, Ui_Rect(sx - 5.5f, sy - 5.5f, 11, 11),
				2.0f, t->primary.base, 1.0f);
	}
}

/* ── 命中测试 ─────────────────────────────────────────────────────── */

typedef struct NodeHit {
	int kind;   /* 0 none, 1 anchor, 2 in, 3 out, 4 arc mid, 5 segment */
	int node;
	int seg;
	float t;
} NodeHit;

static NodeHit hit_node(UiSvgEdit *ed, int idx, float mx, float my)
{
	NodeHit h;
	SvgElement *e = &ed->doc->els[idx];
	int i;
	memset(&h, 0, sizeof(h));
	h.node = -1;
	h.seg = -1;
	for (i = 0; i < e->path.count; i++) {
		const SvgNode *nd = &e->path.nodes[i];
		float wx, wy, sx, sy;
		Svg_LocalToWorld(e, nd->x, nd->y, &wx, &wy);
		w2s(ed, wx, wy, &sx, &sy);
		if (pt_dist2(sx, sy, mx, my) <= SE_NODE_HIT * SE_NODE_HIT) {
			h.kind = 1; h.node = i;
			return h;
		}
	}
	for (i = 0; i < e->path.count; i++) {
		const SvgNode *nd = &e->path.nodes[i];
		float hx, hy, sx, sy;
		if (nd->hasIn) {
			Svg_LocalToWorld(e, nd->x + nd->inX, nd->y + nd->inY, &hx, &hy);
			w2s(ed, hx, hy, &sx, &sy);
			if (pt_dist2(sx, sy, mx, my) <= SE_NODE_HIT * SE_NODE_HIT) {
				h.kind = 2; h.node = i;
				return h;
			}
		}
		if (nd->hasOut) {
			Svg_LocalToWorld(e, nd->x + nd->outX, nd->y + nd->outY, &hx, &hy);
			w2s(ed, hx, hy, &sx, &sy);
			if (pt_dist2(sx, sy, mx, my) <= SE_NODE_HIT * SE_NODE_HIT) {
				h.kind = 3; h.node = i;
				return h;
			}
		}
	}
	{
		int sc = e->path.closed ? e->path.count : e->path.count - 1;
		for (i = 0; i < sc; i++) {
			float lx, ly, px, py, sx, sy;
			if (e->path.seg[i] != SVG_SEG_ARC) continue;
			seg_point(e, i, 0.5f, &lx, &ly);
			Svg_LocalToWorld(e, lx, ly, &px, &py);
			w2s(ed, px, py, &sx, &sy);
			if (pt_dist2(sx, sy, mx, my) <= 7.0f * 7.0f) {
				h.kind = 4; h.seg = i;
				return h;
			}
		}
	}
	{
		float lx, ly, dist;
		int si;
		float t;
		Svg_WorldToLocal(e, ed->mouseWorldX, ed->mouseWorldY, &lx, &ly);
		if (nearest_seg(e, lx, ly, &si, &t, &dist)) {
			float tol = 5.0f / (ed->view.scale > 0.0f ? ed->view.scale : 1.0f);
			if (dist <= tol) {
				h.kind = 5; h.seg = si; h.t = t;
				return h;
			}
		}
	}
	return h;
}

static int hit_handle(const UiSvgEdit *ed, GfxRect worldBox, float mx, float my)
{
	GfxRect box;
	int i;
	if (!screen_box(ed, worldBox, &box)) return -1;
	{
		float rx = box.x + box.w * 0.5f, ry = box.y - SE_ROT_DIST;
		if (pt_dist2(rx, ry, mx, my) <= 7.0f * 7.0f) return 8;
	}
	for (i = 0; i < 8; i++) {
		float hx, hy;
		handle_pos(&box, i, &hx, &hy);
		if (pt_dist2(hx, hy, mx, my) <= 7.0f * 7.0f) return i;
	}
	return -1;
}

/* ── 新建元素 ─────────────────────────────────────────────────────── */

static void style_assign(SvgElement *e, const SvgStyle *s)
{
	Svg_StyleFree(&e->style);
	Svg_StyleCopy(&e->style, s);
}

static void poly_rebuild(SvgElement *e, float cx, float cy, float r, int n)
{
	int i;
	Svg_PathFree(&e->path);
	Svg_PathInit(&e->path);
	for (i = 0; i < n; i++) {
		float a = -SE_PI * 0.5f + SE_2PI * (float)i / (float)n;
		Svg_PathAddNode(&e->path, cx + r * s_cos(a), cy + r * s_sin(a),
			SVG_NODE_CORNER);
	}
	e->path.closed = 1;
	Svg_ElementTouch(e);
}

static void create_update(UiSvgEdit *ed, float wx, float wy, int shift)
{
	SvgElement *e;
	float px = ed->createPX, py = ed->createPY;
	if (ed->createIndex < 0 || ed->createIndex >= ed->doc->count) return;
	e = &ed->doc->els[ed->createIndex];
	if (e->type == SVG_EL_RECT) {
		float x, y, w, h;
		if (alt_down()) {
			w = s_abs(wx - px) * 2.0f;
			h = s_abs(wy - py) * 2.0f;
			if (shift) {
				float m = s_max(w, h);
				w = h = m;
			}
			x = px - w * 0.5f;
			y = py - h * 0.5f;
		} else {
			x = s_min(px, wx);
			y = s_min(py, wy);
			w = s_abs(wx - px);
			h = s_abs(wy - py);
			if (shift) {
				float m = s_max(w, h);
				x = wx < px ? px - m : px;
				y = wy < py ? py - m : py;
				w = h = m;
			}
		}
		if (w < 0.5f) w = 0.5f;
		if (h < 0.5f) h = 0.5f;
		e->p.rect.x = x; e->p.rect.y = y;
		e->p.rect.w = w; e->p.rect.h = h;
	} else if (e->type == SVG_EL_ELLIPSE) {
		float cx, cy, rx, ry;
		if (alt_down()) {
			cx = px; cy = py;
			rx = s_abs(wx - px);
			ry = s_abs(wy - py);
		} else {
			cx = (px + wx) * 0.5f;
			cy = (py + wy) * 0.5f;
			rx = s_abs(wx - px) * 0.5f;
			ry = s_abs(wy - py) * 0.5f;
		}
		if (shift) {
			float m = s_max(rx, ry);
			rx = ry = m;
		}
		if (rx < 0.5f) rx = 0.5f;
		if (ry < 0.5f) ry = 0.5f;
		e->p.ellipse.cx = cx;
		e->p.ellipse.cy = cy;
		e->p.ellipse.rx = rx;
		e->p.ellipse.ry = ry;
	} else if (e->type == SVG_EL_LINE) {
		float x2 = wx, y2 = wy;
		if (shift) {
			float dx = wx - px, dy = wy - py;
			float a = s_atan2(dy, dx);
			float q = SE_PI * 0.25f;
			a = s_round_grid(a, q);
			{
				float len = s_sqrt(dx * dx + dy * dy);
				x2 = px + len * s_cos(a);
				y2 = py + len * s_sin(a);
			}
		}
		if (alt_down()) {
			float dx = x2 - px, dy = y2 - py;
			e->p.line.x1 = px - dx;
			e->p.line.y1 = py - dy;
			e->p.line.x2 = px + dx;
			e->p.line.y2 = py + dy;
		} else {
			e->p.line.x1 = px;
			e->p.line.y1 = py;
			e->p.line.x2 = x2;
			e->p.line.y2 = y2;
		}
	} else if (e->type == SVG_EL_POLYGON) {
		/* 半径即可确定正多边形; 旋转角沿用模板 */
		float dx = wx - px, dy = wy - py;
		float r = s_sqrt(dx * dx + dy * dy);
		if (r < 2.0f) r = 2.0f;
		poly_rebuild(e, px, py, r, ed->polySides);
	} else {
		return;
	}
	Svg_ElementTouch(e);
}

static void create_finish(UiSvgEdit *ed, UiContext *ui, int moved)
{
	int idx = ed->createIndex;
	ed->createIndex = -1;
	ed->drag = SE_DRAG_NONE;
	if (idx < 0 || idx >= ed->doc->count) return;
	{
		SvgElement *e = &ed->doc->els[idx];
		int ok = 1;
		if (!moved) ok = 0;
		if (e->type == SVG_EL_RECT)
			ok = ok && e->p.rect.w >= 1.0f && e->p.rect.h >= 1.0f;
		if (e->type == SVG_EL_ELLIPSE)
			ok = ok && e->p.ellipse.rx >= 0.5f && e->p.ellipse.ry >= 0.5f;
		if (e->type == SVG_EL_LINE) {
			float dx = e->p.line.x2 - e->p.line.x1;
			float dy = e->p.line.y2 - e->p.line.y1;
			ok = ok && dx * dx + dy * dy >= 1.0f;
		}
		if (e->type == SVG_EL_POLYGON) ok = ok && e->path.count >= 3;
		if (!ok) {
			SvgDoc_Remove(ed->doc, idx);
			cache_clear(ed);
			return;
		}
		Svg_ElementTouch(e);
	}
	Svg_History_Commit(ed->doc);
	sel_only(ed, idx);
	ed->tool = UI_SVG_TOOL_SELECT;
	ed_invalidate(ed);
	(void)ui;
}

/* ── 钢笔 ─────────────────────────────────────────────────────────── */

static void pen_set_hint(UiSvgEdit *ed)
{
	if (!ed->penActive) return;
	if (ed->penArcStage == 1) {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"三点弧：点击圆弧落点完成本段  [Esc 取消]");
		return;
	}
	s_str(ed->hint, (int)sizeof(ed->hint),
		"指定下一点  [L 直线] [A 圆弧] [Shift+A 三点弧] "
		"[B 贝塞尔: 拖拽定角度/力度] [C 闭合] [U 撤销节点] "
		"[Enter/双击/右键 完成] [Esc 取消]");
}

static void pen_apply_first_handles(UiSvgEdit *ed, float dx, float dy)
{
	SvgElement *e;
	SvgNode *n;
	if (ed->penIndex < 0 || ed->penIndex >= ed->doc->count) return;
	e = &ed->doc->els[ed->penIndex];
	if (e->path.count < 1) return;
	n = &e->path.nodes[0];
	if (s_abs(dx) < 0.01f && s_abs(dy) < 0.01f) return;
	n->outX = dx;
	n->outY = dy;
	if (alt_down()) {
		n->hasIn = 0;
		n->inX = n->inY = 0.0f;
		n->type = SVG_NODE_CORNER;
	} else {
		n->inX = -dx;
		n->inY = -dy;
		n->hasIn = 1;
		n->type = SVG_NODE_SMOOTH;
	}
	n->hasOut = 1;
	Svg_ElementTouch(e);
}

static void pen_start(UiSvgEdit *ed, float wx, float wy)
{
	SvgDoc *d = ed->doc;
	SvgElement *e;
	if (d->count >= SVG_MAX_ELEMENTS) return;
	e = SvgDoc_Add(d, SVG_EL_PATH);
	if (!e) return;
	style_assign(e, &ed->defStyle);
	/* SvgDoc_Add 给 PATH 预置了 3 个示例节点; 钢笔从空白开始. */
	Svg_PathFree(&e->path);
	Svg_PathInit(&e->path);
	Svg_PathAddNode(&e->path, wx, wy, SVG_NODE_CORNER);
	Svg_ElementTouch(e);
	ed->penActive = 1;
	ed->penIndex = d->count - 1;
	ed->penNext = ed->tool == UI_SVG_TOOL_BEZIER ? SVG_SEG_CUBIC : SVG_SEG_LINE;
	ed->penWant3 = 0;
	ed->penArcStage = 0;
	ed->penJustStarted = 1;
	ed->penDragging = 0;
	ed->drag = SE_DRAG_PEN_POINT;
	cache_clear(ed);
	pen_set_hint(ed);
}

static int last_tangent(const SvgElement *e, float *tx, float *ty)
{
	const SvgPath *p = &e->path;
	const SvgNode *a, *b;
	float dx, dy, len;
	int si;
	if (p->count < 2) return 0;
	si = p->count - 2;
	a = &p->nodes[si];
	b = &p->nodes[si + 1];
	if (p->seg[si] == SVG_SEG_CUBIC && (b->inX || b->inY)) {
		dx = -b->inX;
		dy = -b->inY;
	} else {
		dx = b->x - a->x;
		dy = b->y - a->y;
	}
	len = s_sqrt(dx * dx + dy * dy);
	if (len < 1e-4f) return 0;
	*tx = dx / len;
	*ty = dy / len;
	return 1;
}

static void pen_add_point(UiSvgEdit *ed, float wx, float wy,
	float dwx, float dwy)
{
	SvgElement *e;
	SvgNode *last;
	int segIdx;
	int usedArc = 0;
	int usedCubic = 0;
	SvgArc arc;
	memset(&arc, 0, sizeof(arc));
	if (ed->penIndex < 0 || ed->penIndex >= ed->doc->count) return;
	e = &ed->doc->els[ed->penIndex];
	if (e->path.count < 1) return;
	last = &e->path.nodes[e->path.count - 1];

	if (ed->penNext == SVG_SEG_ARC) {
		int ok = 0;
		if (ed->penArcStage == 1) {
			ok = Svg_ArcFromPoints(last->x, last->y,
				ed->penMidX, ed->penMidY, wx, wy, &arc);
			ed->penArcStage = 0;
			ed->penWant3 = 0;
			usedArc = ok;
		} else {
			float tx, ty;
			if (last_tangent(e, &tx, &ty)) {
				ok = Svg_ArcFromTangent(last->x, last->y, tx, ty,
					wx, wy, &arc);
				usedArc = ok;
			} else {
				/* 首段无法取切线: 退化为三点弧, 等下一次点击 */
				ed->penWant3 = 1;
				ed->penArcStage = 0;
				return;
			}
		}
	}

	if (Svg_PathAddNode(&e->path, wx, wy, SVG_NODE_CORNER) < 0) return;
	segIdx = e->path.count - 2;
	if (usedArc) {
		Svg_PathSetSeg(&e->path, segIdx, SVG_SEG_ARC);
		e->path.arc[segIdx] = arc;
	} else if (ed->penNext == SVG_SEG_CUBIC) {
		SvgNode *nd = &e->path.nodes[e->path.count - 1];
		Svg_PathSetSeg(&e->path, segIdx, SVG_SEG_CUBIC);
		if (dwx != 0.0f || dwy != 0.0f) {
			nd->outX = dwx;
			nd->outY = dwy;
			nd->hasOut = 1;
			if (alt_down()) {
				nd->hasIn = 0;
				nd->type = SVG_NODE_CORNER;
			} else {
				nd->inX = -dwx;
				nd->inY = -dwy;
				nd->hasIn = 1;
				nd->type = SVG_NODE_SMOOTH;
			}
			usedCubic = 1;
		}
	} else {
		Svg_PathSetSeg(&e->path, segIdx, SVG_SEG_LINE);
	}
	(void)usedCubic;
	Svg_ElementTouch(e);
	ed->penDragging = 0;
}

static void pen_finish(UiSvgEdit *ed, UiContext *ui, int closePath, int commit)
{
	(void)ui;
	if (!ed->penActive) return;
	if (ed->penIndex >= 0 && ed->penIndex < ed->doc->count) {
		SvgElement *e = &ed->doc->els[ed->penIndex];
		int idx = ed->penIndex;
		if (closePath && e->path.count >= 3) {
			int si = e->path.count - 1;
			e->path.closed = 1;
			Svg_PathSetSeg(&e->path, si, SVG_SEG_LINE);
		}
		if (e->path.count < 2) {
			SvgDoc_Remove(ed->doc, idx);
			cache_clear(ed);
		} else if (commit) {
			Svg_ElementTouch(e);
			Svg_History_Commit(ed->doc);
			sel_only(ed, idx);
			ed->tool = UI_SVG_TOOL_SELECT;
		} else {
			SvgDoc_Remove(ed->doc, idx);
			cache_clear(ed);
		}
	}
	ed->penActive = 0;
	ed->penIndex = -1;
	ed->penArcStage = 0;
	ed->penWant3 = 0;
	ed->penJustStarted = 0;
	ed->penDragging = 0;
	if (ed->drag == SE_DRAG_PEN_POINT || ed->drag == SE_DRAG_SWALLOW)
		ed->drag = SE_DRAG_NONE;
	ed_invalidate(ed);
}

/* ── 交互 ─────────────────────────────────────────────────────────── */

static int double_click(UiSvgEdit *ed, float mx, float my)
{
	DWORD now = GetTickCount();
	int hit = (now - ed->lastClickTick) <= (DWORD)GetDoubleClickTime()
		&& pt_dist2(mx, my, ed->lastClickX, ed->lastClickY) <= 25.0f;
	ed->lastClickTick = now;
	ed->lastClickX = mx;
	ed->lastClickY = my;
	return hit;
}

static void start_drag(UiSvgEdit *ed, int mode)
{
	ed->drag = mode;
	ed->dragMoved = 0;
	ed->pressSX = ed->mx;
	ed->pressSY = ed->my;
	s2w(ed, ed->mx, ed->my, &ed->startWX, &ed->startWY);
	sel_bounds_cached(ed, &ed->startBox);
}

static void drag_end_commit(UiSvgEdit *ed, UiContext *ui)
{
	int i;
	if (ed->dragMoved && ed->doc) {
		for (i = 0; i < ed->selCount; i++) {
			int idx = ed->sel[i];
			if (idx >= 0 && idx < ed->doc->count)
				Svg_ElementTouch(&ed->doc->els[idx]);
		}
		Svg_History_Commit(ed->doc);
	}
	ed->drag = SE_DRAG_NONE;
	ed->dragMoved = 0;
	orig_clear(ed);
	ed_invalidate(ed);
	(void)ui;
}

static void press_select_tool(UiSvgEdit *ed, UiContext *ui)
{
	GfxRect box;
	int handle = -1;
	int hit;
	float wx, wy;
	int shift = shift_down();
	if (sel_bounds_cached(ed, &box))
		handle = hit_handle(ed, box, ed->mx, ed->my);
	if (handle >= 0 && ed->selCount > 0) {
		orig_capture(ed);
		start_drag(ed, handle == 8 ? SE_DRAG_ROTATE : SE_DRAG_SCALE);
		ed->dragHandle = handle;
		if (ed->drag == SE_DRAG_ROTATE) {
			ed->pivotX = box.x + box.w * 0.5f;
			ed->pivotY = box.y + box.h * 0.5f;
			s2w(ed, ed->mx, ed->my, &wx, &wy);
			ed->startAngle = s_atan2(wy - ed->pivotY, wx - ed->pivotX);
		}
		return;
	}
	{
		int part = 0;
		int gi = grad_handle_hit_(ed, &part);
		if (gi >= 0) {
			ed->gradPart = part;
			start_drag(ed, SE_DRAG_GRAD);
			return;
		}
	}
	s2w(ed, ed->mx, ed->my, &wx, &wy);
	hit = Svg_DocHit(ed->doc, wx, wy, SE_TOL_PX / ed->view.scale);
	if (hit >= 0) {
		if (shift) {
			sel_toggle(ed, hit);
			if (!sel_has(ed, hit)) {
				ed_invalidate(ed);
				return;
			}
		} else if (!sel_has(ed, hit)) {
			sel_only(ed, hit);
		}
		orig_capture(ed);
		start_drag(ed, SE_DRAG_MOVE);
	} else {
		ed->rubberAdd = shift;
		if (!shift) sel_clear(ed);
		start_drag(ed, SE_DRAG_RUBBER);
	}
	ed_invalidate(ed);
}

static void press_node_tool(UiSvgEdit *ed, UiContext *ui)
{
	float wx, wy, lx, ly;
	int idx = path_edit_index(ed);
	int hit;
	int shift = shift_down();
	if (idx < 0) {
		s2w(ed, ed->mx, ed->my, &wx, &wy);
		hit = Svg_DocHit(ed->doc, wx, wy, SE_TOL_PX / ed->view.scale);
		if (hit >= 0) {
			sel_only(ed, hit);
			ed_invalidate(ed);
		} else if (!shift) {
			sel_clear(ed);
			ed_invalidate(ed);
		}
		return;
	}
	{
		NodeHit nh = hit_node(ed, idx, ed->mx, ed->my);
		if (nh.kind == 5 && double_click(ed, ed->mx, ed->my)) {
			SvgElement *e = &ed->doc->els[idx];
			int n = Svg_PathInsertNode(&e->path, nh.seg, nh.t);
			if (n >= 0) {
				ed->nodeSel = n;
				Svg_ElementTouch(e);
				Svg_History_Commit(ed->doc);
				ed_invalidate(ed);
			}
			return;
		}
		if (nh.kind == 1) {
			SvgElement *e = &ed->doc->els[idx];
			ed->dragNode = nh.node;
			ed->nodeSel = nh.node;
			ed->nodeOX = e->path.nodes[nh.node].x;
			ed->nodeOY = e->path.nodes[nh.node].y;
			start_drag(ed, SE_DRAG_NODE);
			return;
		}
		if (nh.kind == 2 || nh.kind == 3) {
			SvgElement *e = &ed->doc->els[idx];
			const SvgNode *nd = &e->path.nodes[nh.node];
			ed->dragNode = nh.node;
			ed->nodeSel = nh.node;
			ed->nodeOX = nd->x;
			ed->nodeOY = nd->y;
			ed->handleOX = nh.kind == 2 ? nd->inX : nd->outX;
			ed->handleOY = nh.kind == 2 ? nd->inY : nd->outY;
			ed->dragHandle = nh.kind == 3 ? 3 : 2;
			start_drag(ed, SE_DRAG_HANDLE);
			return;
		}
		if (nh.kind == 4) {
			ed->dragSeg = nh.seg;
			start_drag(ed, SE_DRAG_ARCMID);
			return;
		}
		s2w(ed, ed->mx, ed->my, &wx, &wy);
		Svg_WorldToLocal(&ed->doc->els[idx], wx, wy, &lx, &ly);
		hit = Svg_DocHit(ed->doc, wx, wy, SE_TOL_PX / ed->view.scale);
		if (hit >= 0 && hit != idx) {
			sel_only(ed, hit);
			ed->nodeSel = -1;
			ed_invalidate(ed);
		} else if (hit < 0) {
			sel_clear(ed);
			ed->nodeSel = -1;
			ed_invalidate(ed);
		}
		(void)lx; (void)ly;
	}
}

/* 组 / 嵌套 / 已启用 xf 的元素: 改变换而不是几何 */
static int elem_needs_xf_(const UiSvgEdit *ed, int idx)
{
	const SvgElement *e;
	if (!ed || idx < 0 || idx >= ed->doc->count) return 0;
	e = &ed->doc->els[idx];
	return e->type == SVG_EL_GROUP || e->parent >= 0 || e->xf.has;
}

static void snap_axis_(UiSvgEdit *ed, int axisY, float p0, float p1,
	float p2, float *adj, float *guide, int *on)
{
	float tol = 6.0f / (ed->view.scale > 0.01f ? ed->view.scale : 1.0f);
	float best = tol, cand[3 * 64 + 6];
	int n = 0, i, r, rc = SvgDoc_RootCount(ed->doc);
	float board = axisY ? ed->doc->height : ed->doc->width;
	cand[n++] = 0.0f;
	cand[n++] = board * 0.5f;
	cand[n++] = board;
	for (r = 0; r < rc && n + 3 <= (int)(sizeof(cand) / sizeof(cand[0])); r++) {
		int idx = SvgDoc_RootAt(ed->doc, r);
		GfxRect b;
		if (idx < 0 || sel_has(ed, idx)) continue;
		b = SvgDoc_ElementBounds(ed->doc, idx);
		cand[n++] = axisY ? b.y : b.x;
		cand[n++] = axisY ? b.y + b.h * 0.5f : b.x + b.w * 0.5f;
		cand[n++] = axisY ? b.y + b.h : b.x + b.w;
	}
	*adj = 0.0f;
	*on = 0;
	for (i = 0; i < n; i++) {
		float c = cand[i];
		float d0 = c - p0, d1 = c - p1, d2 = c - p2;
		float d = d0;
		float ad = s_abs(d0);
		if (s_abs(d1) < ad) { d = d1; ad = s_abs(d1); }
		if (s_abs(d2) < ad) { d = d2; ad = s_abs(d2); }
		if (ad < best) { best = ad; *adj = d; *guide = c; *on = 1; }
	}
}

/* 移动吸附: 选区包围盒的边/中心对齐画板与其它根元素. */
static void snap_move_delta_(UiSvgEdit *ed, float *dx, float *dy)
{
	GfxRect b = ed->startBox;
	float ax, ay, gx, gy;
	int ox, oy;
	b.x += *dx;
	b.y += *dy;
	snap_axis_(ed, 0, b.x, b.x + b.w * 0.5f, b.x + b.w, &ax, &gx, &ox);
	snap_axis_(ed, 1, b.y, b.y + b.h * 0.5f, b.y + b.h, &ay, &gy, &oy);
	if (ox) { *dx += ax; ed->snapGX = gx; }
	if (oy) { *dy += ay; ed->snapGY = gy; }
	ed->snapGXOn = ox;
	ed->snapGYOn = oy;
}

static void update_drag(UiSvgEdit *ed, UiContext *ui)
{
	float wx, wy;
	ed->snapGXOn = 0;
	ed->snapGYOn = 0;
	if (ed->drag == SE_DRAG_PAN) {
		ed->view.tx += ed->mx - ed->lastMx;
		ed->view.ty += ed->my - ed->lastMy;
		view_clamp(ed);
		ed_invalidate(ed);
		return;
	}
	if (ed->drag == SE_DRAG_NONE) return;
	s2w(ed, ed->mx, ed->my, &wx, &wy);
	if (ed->drag == SE_DRAG_GRAD) {
		int idx = ed->selCount == 1 ? ed->sel[0] : -1;
		grad_drag_update_(ed, idx, ed->gradPart);
		ed->dragMoved = 1;
		return;
	}
	if (ed->drag == SE_DRAG_PEN_POINT) {
		if (pt_dist2(ed->mx, ed->my, ed->pressSX, ed->pressSY) > 9.0f) {
			ed->penDragging = 1;
			ed_invalidate(ed);
		}
		return;
	}
	if (ed->drag == SE_DRAG_RUBBER) {
		ed->dragMoved = 1;
		ed_invalidate(ed);
		return;
	}
	if (ed->drag == SE_DRAG_CREATE) {
		create_update(ed, snap_w(ed, wx), snap_w(ed, wy), shift_down());
		ed->dragMoved = 1;
		ed_invalidate(ed);
		return;
	}
	if (ed->drag == SE_DRAG_NODE) {
		int idx = path_edit_index(ed);
		if (idx >= 0 && ed->dragNode >= 0 &&
			ed->dragNode < ed->doc->els[idx].path.count) {
			SvgElement *e = &ed->doc->els[idx];
			SvgNode *nd = &e->path.nodes[ed->dragNode];
			float nx = snap_w(ed, ed->nodeOX + (wx - ed->startWX));
			float ny = snap_w(ed, ed->nodeOY + (wy - ed->startWY));
			if (nx != nd->x || ny != nd->y) {
				nd->x = nx;
				nd->y = ny;
				Svg_ElementTouch(e);
				ed->dragMoved = 1;
				ed_invalidate(ed);
			}
		}
		return;
	}
	if (ed->drag == SE_DRAG_HANDLE) {
		int idx = path_edit_index(ed);
		if (idx >= 0 && ed->dragNode >= 0 &&
			ed->dragNode < ed->doc->els[idx].path.count) {
			SvgElement *e = &ed->doc->els[idx];
			SvgNode *nd = &e->path.nodes[ed->dragNode];
			float lx, ly, dx, dy;
			Svg_WorldToLocal(e, wx, wy, &lx, &ly);
			dx = lx - nd->x;
			dy = ly - nd->y;
			if (ed->dragHandle == 2) {
				nd->inX = dx; nd->inY = dy; nd->hasIn = 1;
				if (!alt_down() && nd->type == SVG_NODE_SMOOTH) {
					nd->outX = -dx; nd->outY = -dy; nd->hasOut = 1;
				} else if (alt_down()) {
					nd->type = SVG_NODE_CORNER;
				}
			} else {
				nd->outX = dx; nd->outY = dy; nd->hasOut = 1;
				if (!alt_down() && nd->type == SVG_NODE_SMOOTH) {
					nd->inX = -dx; nd->inY = -dy; nd->hasIn = 1;
				} else if (alt_down()) {
					nd->type = SVG_NODE_CORNER;
				}
			}
			Svg_ElementTouch(e);
			ed->dragMoved = 1;
			ed_invalidate(ed);
		}
		return;
	}
	if (ed->drag == SE_DRAG_ARCMID) {
		int idx = path_edit_index(ed);
		if (idx >= 0 && ed->dragSeg >= 0) {
			SvgElement *e = &ed->doc->els[idx];
			int si = ed->dragSeg;
			if (si < e->path.count) {
				SvgNode *a = &e->path.nodes[si];
				SvgNode *b = &e->path.nodes[(si + 1) % e->path.count];
				float lx, ly;
				SvgArc arc;
				Svg_WorldToLocal(e, wx, wy, &lx, &ly);
				if (Svg_ArcFromPoints(a->x, a->y, lx, ly, b->x, b->y,
					&arc)) {
					Svg_PathSetSeg(&e->path, si, SVG_SEG_ARC);
					e->path.arc[si] = arc;
					Svg_ElementTouch(e);
					ed->dragMoved = 1;
					ed_invalidate(ed);
				}
			}
		}
		return;
	}
	if (ed->drag == SE_DRAG_MOVE) {
		float dx = wx - ed->startWX;
		float dy = wy - ed->startWY;
		int i;
		if (ed->gridOn && !alt_down()) {
			float gx = snap_w(ed, ed->startWX + dx);
			float gy = snap_w(ed, ed->startWY + dy);
			dx = gx - ed->startWX;
			dy = gy - ed->startWY;
		}
		if (shift_down()) {
			if (s_abs(dx) > s_abs(dy)) dy = 0.0f;
			else dx = 0.0f;
		} else if (!alt_down()) {
			snap_move_delta_(ed, &dx, &dy);
		}
		for (i = 0; i < ed->origCount; i++) {
			int idx = ed->sel[i];
			if (idx < 0 || idx >= ed->doc->count) continue;
			if (elem_needs_xf_(ed, idx))
				SvgDoc_Translate(ed->doc, idx, dx, dy);
			else {
				geom_apply_move(&ed->doc->els[idx], &ed->orig[i], dx, dy);
				Svg_ElementTouch(&ed->doc->els[idx]);
			}
		}
		ed->dragMoved = 1;
		ed_invalidate(ed);
		return;
	}
	if (ed->drag == SE_DRAG_SCALE) {
		GfxRect box = ed->startBox;
		float ax, ay, hx0, hy0, sx = 1.0f, sy = 1.0f;
		int i;
		handle_anchor(&box, ed->dragHandle, &ax, &ay);
		handle_pos(&box, ed->dragHandle, &hx0, &hy0);
		s2w(ed, ed->mx, ed->my, &wx, &wy);
		if (ed->gridOn && !alt_down()) {
			wx = snap_w(ed, wx);
			wy = snap_w(ed, wy);
		}
		if (s_abs(hx0 - ax) > 0.5f)
			sx = (wx - ax) / (hx0 - ax);
		if (s_abs(hy0 - ay) > 0.5f)
			sy = (wy - ay) / (hy0 - ay);
		if (ed->dragHandle == 1 || ed->dragHandle == 5) sx = 1.0f;
		if (ed->dragHandle == 3 || ed->dragHandle == 7) sy = 1.0f;
		if (s_abs(sx) < 0.02f) sx = sx < 0.0f ? -0.02f : 0.02f;
		if (s_abs(sy) < 0.02f) sy = sy < 0.0f ? -0.02f : 0.02f;
		if (shift_down()) {
			float m = s_max(s_abs(sx), s_abs(sy));
			sx = sx < 0.0f ? -m : m;
			sy = sy < 0.0f ? -m : m;
		}
		for (i = 0; i < ed->origCount; i++) {
			int idx = ed->sel[i];
			if (idx < 0 || idx >= ed->doc->count) continue;
			if (elem_needs_xf_(ed, idx))
				SvgDoc_ScaleBy(ed->doc, idx, sx, sy, ax, ay);
			else {
				geom_apply_scale(&ed->doc->els[idx], &ed->orig[i], ax, ay,
					sx, sy);
				Svg_ElementTouch(&ed->doc->els[idx]);
			}
		}
		ed->dragMoved = 1;
		ed_invalidate(ed);
		return;
	}
	if (ed->drag == SE_DRAG_ROTATE) {
		float ang = s_atan2(wy - ed->pivotY, wx - ed->pivotX);
		float delta = (ang - ed->startAngle) / SE_DEG;
		float c, s;
		int i;
		if (shift_down())
			delta = s_round_grid(delta, 15.0f);
		c = s_cos(delta * SE_DEG);
		s = s_sin(delta * SE_DEG);
		if (ed->origCount == 1 && ed->selCount == 1) {
			int idx = ed->sel[0];
			if (idx >= 0 && idx < ed->doc->count) {
				if (elem_needs_xf_(ed, idx))
					SvgDoc_RotateBy(ed->doc, idx, delta, 0.0f, 0.0f);
				else {
					SvgElement *e = &ed->doc->els[idx];
					e->rot = ed->orig[0].rot + delta;
					Svg_ElementTouch(e);
				}
				ed->dragMoved = 1;
				ed_invalidate(ed);
			}
			return;
		}
		for (i = 0; i < ed->origCount; i++) {
			int idx = ed->sel[i];
			float cx, cy, nx, ny;
			if (idx < 0 || idx >= ed->doc->count) continue;
			if (elem_needs_xf_(ed, idx)) {
				GfxRect b = SvgDoc_ElementBounds(ed->doc, idx);
				cx = b.x + b.w * 0.5f;
				cy = b.y + b.h * 0.5f;
				nx = ed->pivotX + (cx - ed->pivotX) * c
					- (cy - ed->pivotY) * s;
				ny = ed->pivotY + (cx - ed->pivotX) * s
					+ (cy - ed->pivotY) * c;
				SvgDoc_RotateBy(ed->doc, idx, delta, 0.0f, 0.0f);
				SvgDoc_Translate(ed->doc, idx, nx - cx, ny - cy);
				continue;
			}
			geom_center(&ed->orig[i], &cx, &cy);
			nx = ed->pivotX + (cx - ed->pivotX) * c - (cy - ed->pivotY) * s;
			ny = ed->pivotY + (cx - ed->pivotX) * s + (cy - ed->pivotY) * c;
			geom_apply_move(&ed->doc->els[idx], &ed->orig[i],
				nx - cx, ny - cy);
			ed->doc->els[idx].rot = ed->orig[i].rot + delta;
			Svg_ElementTouch(&ed->doc->els[idx]);
		}
		ed->dragMoved = 1;
		ed_invalidate(ed);
		return;
	}
}

static void rubber_finish(UiSvgEdit *ed, UiContext *ui)
{
	float wx0, wy0, wx1, wy1;
	GfxRect r;
	int i;
	s2w(ed, ed->pressSX, ed->pressSY, &wx0, &wy0);
	s2w(ed, ed->mx, ed->my, &wx1, &wy1);
	r.x = s_min(wx0, wx1);
	r.y = s_min(wy0, wy1);
	r.w = s_abs(wx1 - wx0);
	r.h = s_abs(wy1 - wy0);
	if (r.w < 1.0f && r.h < 1.0f) {
		ed->drag = SE_DRAG_NONE;
		return;
	}
	if (!ed->rubberAdd) sel_clear(ed);
	for (i = 0; i < ed->doc->count; i++) {
		GfxRect b;
		if (ed->doc->els[i].parent >= 0) continue;
		b = SvgDoc_ElementBounds(ed->doc, i);
		if (b.x < r.x + r.w && b.x + b.w > r.x &&
			b.y < r.y + r.h && b.y + b.h > r.y)
			sel_add(ed, i);
	}
	ed->drag = SE_DRAG_NONE;
	ed_invalidate(ed);
	(void)ui;
}

static void release_left(UiSvgEdit *ed, UiContext *ui)
{
	switch (ed->drag) {
	case SE_DRAG_PEN_POINT: {
		float awx, awy, rwx, rwy, hx = 0.0f, hy = 0.0f;
		s2w(ed, ed->pressSX, ed->pressSY, &awx, &awy);
		s2w(ed, ed->mx, ed->my, &rwx, &rwy);
		awx = snap_w(ed, awx);
		awy = snap_w(ed, awy);
		if (ed->penDragging) {
			hx = rwx - awx;
			hy = rwy - awy;
		}
		if (ed->penJustStarted) {
			ed->penJustStarted = 0;
			if (ed->penDragging) {
				pen_apply_first_handles(ed, hx, hy);
			}
			ed_invalidate(ed);
		} else {
			pen_add_point(ed, awx, awy, hx, hy);
			if (ed->penActive) pen_set_hint(ed);
			ed_invalidate(ed);
		}
		ed->drag = SE_DRAG_NONE;
		ed->penDragging = 0;
		break;
	}
	case SE_DRAG_MOVE:
		ed->dragMoved = ed->dragMoved ||
			pt_dist2(ed->mx, ed->my, ed->pressSX, ed->pressSY) > 4.0f;
		drag_end_commit(ed, ui);
		break;
	case SE_DRAG_SCALE:
	case SE_DRAG_ROTATE:
	case SE_DRAG_GRAD:
		drag_end_commit(ed, ui);
		break;
	case SE_DRAG_NODE:
	case SE_DRAG_HANDLE:
	case SE_DRAG_ARCMID:
		if (ed->dragMoved && ed->doc) Svg_History_Commit(ed->doc);
		ed->drag = SE_DRAG_NONE;
		ed->dragMoved = 0;
		ed_invalidate(ed);
		break;
	case SE_DRAG_RUBBER:
		rubber_finish(ed, ui);
		break;
	case SE_DRAG_CREATE:
		create_finish(ed, ui, ed->dragMoved);
		break;
	case SE_DRAG_PAN:
		ed->drag = SE_DRAG_NONE;
		break;
	case SE_DRAG_SWALLOW:
		ed->drag = SE_DRAG_NONE;
		break;
	default:
		break;
	}
}

static int in_board(const UiSvgEdit *ed, float wx, float wy)
{
	return ed->doc && wx >= 0.0f && wy >= 0.0f &&
		wx <= ed->doc->width && wy <= ed->doc->height;
}

static void press_left(UiSvgEdit *ed, UiContext *ui)
{
	float wx, wy;
	if (ed->tool == UI_SVG_TOOL_PEN || ed->tool == UI_SVG_TOOL_BEZIER) {
		s2w(ed, ed->mx, ed->my, &wx, &wy);
		if (ed->penActive) {
			SvgElement *e = &ed->doc->els[ed->penIndex];
			if (double_click(ed, ed->mx, ed->my) && e->path.count >= 2) {
				pen_finish(ed, ui, 0, 1);
				ed->drag = SE_DRAG_SWALLOW;
				return;
			}
			if (!ed->penArcStage && pen_near_first(ed, e, ed->mx, ed->my)) {
				pen_finish(ed, ui, 1, 1);
				ed->drag = SE_DRAG_SWALLOW;
				return;
			}
			if (ed->penNext == SVG_SEG_ARC &&
				(ed->penWant3 || e->path.count < 2)) {
				if (ed->penArcStage == 0) {
					ed->penMidX = snap_w(ed, wx);
					ed->penMidY = snap_w(ed, wy);
					ed->penArcStage = 1;
					pen_set_hint(ed);
					ed->drag = SE_DRAG_SWALLOW;
					ed_invalidate(ed);
					return;
				}
				/* 第二落点: 在 release 时添加节点 */
			}
			ed->penDownWX = wx;
			ed->penDownWY = wy;
			start_drag(ed, SE_DRAG_PEN_POINT);
			return;
		}
		if (!in_board(ed, wx, wy)) return;
		pen_start(ed, snap_w(ed, wx), snap_w(ed, wy));
		return;
	}
	if (ed->tool == UI_SVG_TOOL_TEXT) {
		SvgElement *e;
		int idx;
		s2w(ed, ed->mx, ed->my, &wx, &wy);
		if (!in_board(ed, wx, wy)) return;
		e = SvgDoc_AddChild(ed->doc, -1, SVG_EL_TEXT);
		if (!e) return;
		idx = ed->doc->count - 1;
		e->p.text.x = snap_w(ed, wx);
		e->p.text.y = snap_w(ed, wy);
		e->style.fill.kind = SVG_PAINT_SOLID;
		e->style.fill.color = 0xFF202020u;
		ed->doc->els[idx].version++;
		sel_clear(ed);
		sel_add(ed, idx);
		cache_clear(ed);
		Svg_History_Commit(ed->doc);
		ed->drag = SE_DRAG_SWALLOW;
		ed->tool = UI_SVG_TOOL_SELECT;
		ed_invalidate(ed);
		return;
	}
	if (ed->tool == UI_SVG_TOOL_SELECT) {
		press_select_tool(ed, ui);
		return;
	}
	if (ed->tool == UI_SVG_TOOL_NODE) {
		press_node_tool(ed, ui);
		return;
	}
	/* 创建工具: 画板外按下不创建 (与设计约定一致). */
	if (ed->doc->count >= SVG_MAX_ELEMENTS) return;
	s2w(ed, ed->mx, ed->my, &wx, &wy);
	if (!in_board(ed, wx, wy)) return;
	{
		SvgElType type = SVG_EL_RECT;
		SvgElement *e;
		if (ed->tool == UI_SVG_TOOL_LINE) type = SVG_EL_LINE;
		else if (ed->tool == UI_SVG_TOOL_ELLIPSE) type = SVG_EL_ELLIPSE;
		else if (ed->tool == UI_SVG_TOOL_POLYGON) type = SVG_EL_POLYGON;
		e = SvgDoc_Add(ed->doc, type);
		if (!e) return;
		style_assign(e, &ed->defStyle);
		cache_clear(ed);
		sel_clear(ed);
		ed->createPX = snap_w(ed, wx);
		ed->createPY = snap_w(ed, wy);
		ed->createIndex = ed->doc->count - 1;
		ed->polySides = 5;
		ed->dragMoved = 0;
		start_drag(ed, SE_DRAG_CREATE);
		create_update(ed, ed->createPX, ed->createPY, shift_down());
		ed_invalidate(ed);
	}
}

static void handle_input(UiSvgEdit *ed, UiContext *ui)
{
	SeSB sb;
	int inCanvas = Ui_RectContains(ed->vp, ed->mx, ed->my);
	int firstDown = (ui->down == ed->id);
	int released = (ui->released == ed->id);
	int mb = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
	int moved = (ed->mx != ed->lastMx || ed->my != ed->lastMy);

	ed->mouseIn = inCanvas;
	ed->mouseWorldValid = ed->viewValid;
	if (ed->inputBlock) {
		ed->lastMx = ed->mx;
		ed->lastMy = ed->my;
		return;
	}
	if (ed->viewValid) {
		float wx, wy;
		s2w(ed, ed->mx, ed->my, &wx, &wy);
		ed->mouseWorldX = wx;
		ed->mouseWorldY = wy;
	}

	sb_geom_cached(ed, &sb);

	/* 滚动条拖拽优先于画布工具 */
	if (ed->sbDrag) {
		if (released || !ui->mouseDown) {
			ed->sbDrag = 0;
		} else {
			if (ed->sbDrag == 2) {
				float span = sb.vt.h - sb.vth.h;
				if (span > 0.5f)
					sb_set_frac(ed, 2,
						(ed->my - ed->sbGrab - sb.vt.y) / span);
			} else {
				float span = sb.ht.w - sb.hth.w;
				if (span > 0.5f)
					sb_set_frac(ed, 1,
						(ed->mx - ed->sbGrab - sb.ht.x) / span);
			}
			ed_invalidate(ed);
		}
		Ui_SetCursor(ui, ed->id, UI_CURSOR_HAND);
		ed->lastMx = ed->mx;
		ed->lastMy = ed->my;
		return;
	}

	/* 中键平移 */
	if (mb && inCanvas && ed->drag == SE_DRAG_NONE) {
		ed->drag = SE_DRAG_PAN;
		ed->dragHandle = -1;
		ed_invalidate(ed);
	}
	if (ed->drag == SE_DRAG_PAN && !mb && !space_down()) {
		ed->drag = SE_DRAG_NONE;
	}

	if (ed->drag != SE_DRAG_NONE && moved)
		update_drag(ed, ui);

	if (released)
		release_left(ed, ui);
	else if (firstDown && inCanvas) {
		int hit = space_down() ? SE_SB_NONE : sb_hit(&sb, ed->mx, ed->my);
		if (space_down()) {
			ed->drag = SE_DRAG_PAN;
		} else if (hit == SE_SB_VTHUMB) {
			ed->sbDrag = 2;
			ed->sbGrab = ed->my - sb.vth.y;
			ed_invalidate(ed);
		} else if (hit == SE_SB_HTHUMB) {
			ed->sbDrag = 1;
			ed->sbGrab = ed->mx - sb.hth.x;
			ed_invalidate(ed);
		} else if (hit == SE_SB_VTRACK) {
			float span = sb.vt.h - sb.vth.h;
			if (span > 0.5f)
				sb_set_frac(ed, 2,
					(ed->my - sb.vt.y - sb.vth.h * 0.5f) / span);
			ed_invalidate(ed);
		} else if (hit == SE_SB_HTRACK) {
			float span = sb.ht.w - sb.hth.w;
			if (span > 0.5f)
				sb_set_frac(ed, 1,
					(ed->mx - sb.ht.x - sb.hth.w * 0.5f) / span);
			ed_invalidate(ed);
		} else {
			press_left(ed, ui);
		}
	}

	/* 光标 */
	if (ed->drag == SE_DRAG_PAN) {
		Ui_SetCursor(ui, ed->id, UI_CURSOR_HAND);
	} else if (ed->tool == UI_SVG_TOOL_SELECT && inCanvas) {
		GfxRect box;
		int kind = UI_CURSOR_ARROW;
		if (sel_bounds_cached(ed, &box)) {
			int h = hit_handle(ed, box, ed->mx, ed->my);
			if (h == 1 || h == 5) kind = UI_CURSOR_SIZE_NS;
			else if (h == 3 || h == 7) kind = UI_CURSOR_SIZE_WE;
			else if (h >= 0) kind = UI_CURSOR_HAND;
			else kind = UI_CURSOR_HAND;
		}
		Ui_SetCursor(ui, ed->id, kind);
	} else if (inCanvas && ed->tool != UI_SVG_TOOL_NODE) {
		Ui_SetCursor(ui, ed->id, UI_CURSOR_HAND);
	}
	/* 滚动条覆盖画布光标 */
	if (ed->sbDrag == 0 && ed->mouseIn) {
		int hit = sb_hit(&sb, ed->mx, ed->my);
		if (hit == SE_SB_HTHUMB || hit == SE_SB_VTHUMB)
			Ui_SetCursor(ui, ed->id, UI_CURSOR_HAND);
		else if (hit != SE_SB_NONE)
			Ui_SetCursor(ui, ed->id, UI_CURSOR_ARROW);
	}

	/* 十字准星/状态栏需要逐移动重绘 */
	if (moved && inCanvas &&
		(ed->tool == UI_SVG_TOOL_LINE || ed->tool == UI_SVG_TOOL_RECT ||
		 ed->tool == UI_SVG_TOOL_ELLIPSE || ed->tool == UI_SVG_TOOL_POLYGON))
		ed_invalidate(ed);

	ed->lastMx = ed->mx;
	ed->lastMy = ed->my;
}

/* ── 提示文本 ─────────────────────────────────────────────────────── */

static void update_hint(UiSvgEdit *ed)
{
	if (ed->tool == UI_SVG_TOOL_SELECT) {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"选择：单击选中，Shift 多选，拖框选；拖动移动，手柄缩放/旋转；"
			"Delete 删除，Ctrl+Z/Y 撤销重做");
	} else if (ed->tool == UI_SVG_TOOL_NODE) {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"节点：拖动锚点/控制点；拖橙色点调圆弧凸度；双击线段插入节点；"
			"Delete 删除节点");
	} else if (ed->tool == UI_SVG_TOOL_PEN || ed->tool == UI_SVG_TOOL_BEZIER) {
		if (ed->penActive) pen_set_hint(ed);
		else s_str(ed->hint, (int)sizeof(ed->hint),
			"钢笔：单击落点开始；拖动调整切线；L/A/Shift+A/B 切换段型");
	} else if (ed->tool == UI_SVG_TOOL_POLYGON) {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"正多边形：拖动绘制，滚轮改边数，Shift 角度吸附，Alt 从中心");
	} else if (ed->tool == UI_SVG_TOOL_LINE) {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"直线：拖动绘制，Shift 45° 吸附，Alt 从中心");
	} else if (ed->tool == UI_SVG_TOOL_ELLIPSE) {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"椭圆：拖动绘制，Shift 正圆，Alt 从中心");
	} else if (ed->tool == UI_SVG_TOOL_TEXT) {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"文本：点击画布创建；在检查器「文本」页改内容/字体/对齐");
	} else {
		s_str(ed->hint, (int)sizeof(ed->hint),
			"矩形：拖动绘制，Shift 正方形，Alt 从中心");
	}
}

/* ── 生命周期 ─────────────────────────────────────────────────────── */

UiSvgEdit *Ui_SvgEdit_Create(SvgDoc *doc, UiId id)
{
	UiSvgEdit *ed = (UiSvgEdit *)s_alloc(sizeof(UiSvgEdit));
	if (!ed) return NULL;
	ed->doc = doc;
	ed->id = id ? id : 1;
	ed->tool = UI_SVG_TOOL_SELECT;
	ed->gridOn = 1;
	ed->gridStep = 8.0f;
	ed->nodeSel = -1;
	ed->hoverNode = -1;
	ed->dragNode = -1;
	ed->dragSeg = -1;
	ed->createIndex = -1;
	ed->penIndex = -1;
	Svg_StyleDefault(&ed->defStyle);
	update_hint(ed);
	return ed;
}

void Ui_SvgEdit_Destroy(UiSvgEdit *ed)
{
	int i;
	if (!ed) return;
	if (ed->gfx) cache_clear(ed);
	if (ed->gfx) {
		for (i = 0; i < ed->fontCount; i++)
			if (ed->fonts[i]) Gfx_DestroyFont(ed->gfx, ed->fonts[i]);
	}
	for (i = 0; i < ed->origCount; i++) geom_free(&ed->orig[i]);
	s_free(ed->orig);
	SvgDoc_ClipFree(ed->clip);
	s_free(ed->cache);
	s_free(ed->sel);
	Svg_StyleFree(&ed->defStyle);
	s_free(ed);
}

/* ── 每帧绘制 ─────────────────────────────────────────────────────── */

static void draw_scrollbars(UiSvgEdit *ed, UiContext *ui, Gfx *g)
{
	SeSB sb;
	UiTheme *t = ui->theme;
	UiColor trackC, thumbC;
	int hit, hovV, hovH;
	sb_geom_cached(ed, &sb);
	if (!sb.hasH && !sb.hasV) return;
	hit = ui->mouseOver ? sb_hit(&sb, ui->mouseX, ui->mouseY) : SE_SB_NONE;
	hovV = hit == SE_SB_VTHUMB || hit == SE_SB_VTRACK || ed->sbDrag == 2;
	hovH = hit == SE_SB_HTHUMB || hit == SE_SB_HTRACK || ed->sbDrag == 1;
	trackC = Ui_ColorAlpha(t->text3, 18);
	if (sb.hasV) {
		thumbC = Ui_ColorAlpha(t->text3, hovV ? 140 : 90);
		Gfx_FillRoundRect(g, sb.vt, SE_SB_W * 0.5f, trackC);
		Gfx_FillRoundRect(g, sb.vth, SE_SB_W * 0.5f, thumbC);
	}
	if (sb.hasH) {
		thumbC = Ui_ColorAlpha(t->text3, hovH ? 140 : 90);
		Gfx_FillRoundRect(g, sb.ht, SE_SB_W * 0.5f, trackC);
		Gfx_FillRoundRect(g, sb.hth, SE_SB_W * 0.5f, thumbC);
	}
}

void Ui_SvgEdit_Frame(UiSvgEdit *ed, UiContext *ui, UiRect viewport)
{
	Gfx *g = ui->gfx;
	UiTheme *t = ui->theme;
	GfxRect board;
	float bx0, by0, bx1, by1;
	int i;

	if (!ed || !ui || !g || !ed->doc) return;
	ed->ctx = ui;
	ed->gfx = g;
	ed->vp = viewport;
	ed->mx = ui->mouseX;
	ed->my = ui->mouseY;
	if (!ed->viewValid) view_fit_view(ed, viewport);
	else view_clamp(ed);

	Ui_RegisterWidget(ui, ed->id, viewport);
	Ui_RegisterWheelTarget(ui, ed->id, viewport);
	Ui_SetPressPoll(ui, ed->id,
		ed->drag != SE_DRAG_NONE || ed->penActive || ed->sbDrag);

	if (ui->wheelTarget == ed->id && ui->wheelDelta != 0.0f) {
		float d = ui->wheelDelta;
		if (ed->drag == SE_DRAG_CREATE && ed->createIndex >= 0 &&
			ed->createIndex < ed->doc->count &&
			ed->doc->els[ed->createIndex].type == SVG_EL_POLYGON) {
			int n = ed->polySides + (d > 0.0f ? 1 : -1) * (int)(s_abs(d) + 0.5f);
			if (n < 3) n = 3;
			if (n > 64) n = 64;
			ed->polySides = n;
			create_update(ed, ed->mouseWorldX, ed->mouseWorldY, shift_down());
			ed_invalidate(ed);
		} else {
			float f = 1.0f;
			float ax = ed->mx, ay = ed->my;
			int step;
			if (!Ui_RectContains(viewport, ax, ay)) {
				ax = viewport.x + viewport.w * 0.5f;
				ay = viewport.y + viewport.h * 0.5f;
			}
			if (d > 10.0f) d = 10.0f;
			if (d < -10.0f) d = -10.0f;
			for (step = 0; step < (int)s_abs(d); step++) f *= 1.2f;
			if (d < 0.0f) f = 1.0f / f;
			f *= 1.0f + 0.2f * (s_abs(d) - (float)(int)s_abs(d));
			view_zoom_at(ed, f, ax, ay);
			ed_invalidate(ed);
		}
		ui->wheelTarget = UI_ID_NONE;
		ui->wheelDelta = 0.0f;
	}

	handle_input(ed, ui);
	update_hint(ed);

	/* 画布内容严格裁到视口, 放大后不得覆盖工具栏/侧栏 */
	Gfx_PushClip(g, viewport);

	/* 背景 */
	Gfx_FillRect(g, viewport,
		Ui_ColorShift(t->page2, t->themeId == UI_THEME_DARK ? -6 : -4));

	/* 画板 */
	w2s(ed, 0.0f, 0.0f, &bx0, &by0);
	w2s(ed, ed->doc->width, ed->doc->height, &bx1, &by1);
	board.x = s_min(bx0, bx1);
	board.y = s_min(by0, by1);
	board.w = s_abs(bx1 - bx0);
	board.h = s_abs(by1 - by0);
	Gfx_FillRoundRect(g, Ui_Rect(board.x + 2, board.y + 3, board.w, board.h),
		2.0f, 0x28000000);
	Gfx_FillRect(g, board, 0xFFFFFFFF);
	Gfx_StrokeRoundRect(g, board, 0.0f, 0x33000000, 1.0f);

	/* 网格 */
	if (ed->gridOn && ed->gridStep > 0.0f &&
		ed->gridStep * ed->view.scale >= 6.0f) {
		GfxColor gc = t->themeId == UI_THEME_DARK
			? 0x20FFFFFF : 0x22000000;
		float x;
		for (x = ed->gridStep; x < ed->doc->width; x += ed->gridStep) {
			float sx, sy0, sy1;
			w2s(ed, x, 0.0f, &sx, &sy0);
			w2s(ed, x, ed->doc->height, &sx, &sy1);
			Gfx_Line(g, sx, sy0, sx, sy1, gc, 1.0f);
		}
		for (x = ed->gridStep; x < ed->doc->height; x += ed->gridStep) {
			float sy, sx0, sx1;
			w2s(ed, 0.0f, x, &sx0, &sy);
			w2s(ed, ed->doc->width, x, &sx1, &sy);
			Gfx_Line(g, sx0, sy, sx1, sy, gc, 1.0f);
		}
	}

	/* 元素 (裁到画板) */
	Gfx_PushClip(g, board);
	Gfx_PushScaleTranslate(g, ed->view.scale, ed->view.tx, ed->view.ty);
	{
		int nr = SvgDoc_RootCount(ed->doc), r;
		for (r = 0; r < nr; r++)
			draw_subtree_(ed, g, SvgDoc_RootAt(ed->doc, r));
	}
	Gfx_PopTransform(g);
	Gfx_PopClip(g);

	/* 选择框 */
	if (ed->tool == UI_SVG_TOOL_SELECT || ed->tool == UI_SVG_TOOL_NODE) {
		GfxRect wb;
		if (!ed->penActive && sel_bounds_cached(ed, &wb) && ed->selCount > 0) {
			draw_selection(ed, ui, wb);
			if (ed->tool == UI_SVG_TOOL_SELECT) draw_grad_handle_(ed, ui);
		}
	}

	/* 节点/钢笔覆盖层 */
	draw_node_overlay(ed, ui, g);
	if (ed->penActive) draw_pen_overlay(ed, ui, g);

	/* 框选 */
	if (ed->drag == SE_DRAG_RUBBER) {
		float x0 = s_min(ed->pressSX, ed->mx);
		float y0 = s_min(ed->pressSY, ed->my);
		GfxRect rr = Ui_Rect(x0, y0, s_abs(ed->mx - ed->pressSX),
			s_abs(ed->my - ed->pressSY));
		Gfx_FillRect(g, rr, 0x1A2D7FF9);
		Gfx_StrokeRoundRect(g, rr, 0.0f, t->primary.base, 1.0f);
	}

	/* 创建十字准星 */
	if (ed->mouseIn && ed->drag != SE_DRAG_PAN &&
		(ed->tool == UI_SVG_TOOL_LINE || ed->tool == UI_SVG_TOOL_RECT ||
		 ed->tool == UI_SVG_TOOL_ELLIPSE || ed->tool == UI_SVG_TOOL_POLYGON)) {
		Gfx_Line(g, viewport.x, ed->my, viewport.x + viewport.w, ed->my,
			0x22000000, 1.0f);
		Gfx_Line(g, ed->mx, viewport.y, ed->mx, viewport.y + viewport.h,
			0x22000000, 1.0f);
	}

	draw_scrollbars(ed, ui, g);

	Gfx_StrokeRoundRect(g, Ui_Rect(viewport.x + 0.5f, viewport.y + 0.5f,
		viewport.w - 1.0f, viewport.h - 1.0f), 2.0f, t->frame, 1.0f);

	Gfx_PopClip(g);
}

/* ── 键盘 ─────────────────────────────────────────────────────────── */

static void zoom_center(UiSvgEdit *ed, float factor)
{
	view_zoom_at(ed, factor,
		ed->vp.x + ed->vp.w * 0.5f, ed->vp.y + ed->vp.h * 0.5f);
	ed_invalidate(ed);
}

int Ui_SvgEdit_Key(UiSvgEdit *ed, UiContext *ui, UINT vk)
{
	int ctrl = ctrl_down();
	int shift = shift_down();

	if (!ed) return 0;
	if (vk == VK_ESCAPE) {
		if (ed->penActive) {
			pen_finish(ed, ui, 0, 0);
			return 1;
		}
		if (ed->drag != SE_DRAG_NONE) {
			ed->drag = SE_DRAG_NONE;
			orig_clear(ed);
			return 1;
		}
		sel_clear(ed);
		ed_invalidate(ed);
		return 1;
	}
	if (vk == VK_DELETE) {
		if (ed->tool == UI_SVG_TOOL_NODE && ed->nodeSel >= 0) {
			int idx = path_edit_index(ed);
			if (idx >= 0) {
				SvgElement *e = &ed->doc->els[idx];
				if (ed->nodeSel < e->path.count) {
					Svg_PathRemoveNode(&e->path, ed->nodeSel);
					ed->nodeSel = -1;
					if (e->path.count < 2) {
						SvgDoc_Remove(ed->doc, idx);
						cache_clear(ed);
						sel_clear(ed);
					} else {
						Svg_ElementTouch(e);
					}
					Svg_History_Commit(ed->doc);
					ed_invalidate(ed);
				}
				return 1;
			}
		}
		Ui_SvgEdit_DeleteSel(ed, ui);
		return 1;
	}
	if (ctrl && vk == 'Z') {
		if (shift) Ui_SvgEdit_Redo(ed, ui);
		else Ui_SvgEdit_Undo(ed, ui);
		return 1;
	}
	if (ctrl && vk == 'Y') {
		Ui_SvgEdit_Redo(ed, ui);
		return 1;
	}
	if (ctrl && vk == 'A') {
		Ui_SvgEdit_SelectAll(ed);
		return 1;
	}
	if (ctrl && vk == 'C') {
		Ui_SvgEdit_CopySel(ed);
		return 1;
	}
	if (ctrl && vk == 'V') {
		Ui_SvgEdit_Paste(ed, ui);
		return 1;
	}
	if (ctrl && vk == 'D') {
		Ui_SvgEdit_DuplicateSel(ed, ui);
		return 1;
	}
	if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN) {
		float step = shift ? 10.0f : 1.0f;
		float dx = vk == VK_LEFT ? -step : (vk == VK_RIGHT ? step : 0.0f);
		float dy = vk == VK_UP ? -step : (vk == VK_DOWN ? step : 0.0f);
		return Ui_SvgEdit_Nudge(ed, ui, dx, dy);
	}
	if (ctrl && vk == 'G') {
		if (shift) Ui_SvgEdit_Ungroup(ed, ui);
		else Ui_SvgEdit_Group(ed, ui);
		return 1;
	}
	if (ctrl && (vk == '0' || vk == VK_NUMPAD0)) {
		Ui_SvgEdit_ZoomFit(ed, ed->vp);
		return 1;
	}
	if (ctrl && (vk == '1' || vk == VK_NUMPAD1)) {
		Ui_SvgEdit_ZoomReset(ed, ed->vp);
		return 1;
	}
	if (vk == VK_OEM_PLUS || vk == VK_ADD) {
		zoom_center(ed, 1.25f);
		return 1;
	}
	if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) {
		zoom_center(ed, 0.8f);
		return 1;
	}

	if (ed->penActive) {
		if (vk == 'L') { ed->penNext = SVG_SEG_LINE; ed->penWant3 = 0;
			ed->penArcStage = 0; pen_set_hint(ed); return 1; }
		if (vk == 'A') {
			ed->penNext = SVG_SEG_ARC;
			ed->penWant3 = shift ? 1 : 0;
			ed->penArcStage = 0;
			pen_set_hint(ed);
			return 1;
		}
		if (vk == 'B') { ed->penNext = SVG_SEG_CUBIC; ed->penWant3 = 0;
			ed->penArcStage = 0; pen_set_hint(ed); return 1; }
		if (vk == 'C') { pen_finish(ed, ui, 1, 1); return 1; }
		if (vk == 'U') {
			if (ed->penIndex >= 0 && ed->penIndex < ed->doc->count) {
				SvgElement *e = &ed->doc->els[ed->penIndex];
				if (e->path.count > 1) {
					Svg_PathRemoveNode(&e->path, e->path.count - 1);
					Svg_ElementTouch(e);
				}
				if (e->path.count < 2)
					pen_finish(ed, ui, 0, 0);
				ed_invalidate(ed);
			}
			return 1;
		}
		if (vk == VK_RETURN) { pen_finish(ed, ui, 0, 1); return 1; }
	}
	if (ctrl) return 0;

	switch (vk) {
	case 'V': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_SELECT); return 1;
	case 'N': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_NODE); return 1;
	case 'T': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_TEXT); return 1;
	case 'L': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_LINE); return 1;
	case 'R': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_RECT); return 1;
	case 'O': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_ELLIPSE); return 1;
	case 'G': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_POLYGON); return 1;
	case 'P': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_PEN); return 1;
	case 'B': Ui_SvgEdit_SetTool(ed, UI_SVG_TOOL_BEZIER); return 1;
	default:
		return 0;
	}
}

void Ui_SvgEdit_RightDown(UiSvgEdit *ed, float x, float y)
{
	if (!ed) return;
	if (!Ui_RectContains(ed->vp, x, y)) return;
	if (ed->penActive) {
		pen_finish(ed, ed->ctx, 0, 1);
	}
}

/* ── 公共 API ─────────────────────────────────────────────────────── */

void Ui_SvgEdit_SetTool(UiSvgEdit *ed, int tool)
{
	if (!ed) return;
	if (ed->penActive) pen_finish(ed, ed->ctx, 0, 1);
	if (tool < 0 || tool > UI_SVG_TOOL_TEXT) return;
	ed->tool = tool;
	ed->nodeSel = -1;
	update_hint(ed);
	ed_invalidate(ed);
}

int Ui_SvgEdit_Tool(const UiSvgEdit *ed)
{
	return ed ? ed->tool : UI_SVG_TOOL_SELECT;
}

void Ui_SvgEdit_ZoomAt(UiSvgEdit *ed, float factor, float sx, float sy)
{
	if (!ed) return;
	if (!ed->viewValid) return;
	view_zoom_at(ed, factor, sx, sy);
	ed_invalidate(ed);
}

void Ui_SvgEdit_ZoomFit(UiSvgEdit *ed, UiRect viewport)
{
	if (!ed) return;
	ed->vp = viewport;
	view_fit_view(ed, viewport);
	ed_invalidate(ed);
}

void Ui_SvgEdit_ZoomReset(UiSvgEdit *ed, UiRect viewport)
{
	if (!ed) return;
	ed->vp = viewport;
	view_reset(ed, viewport);
	ed_invalidate(ed);
}

float Ui_SvgEdit_Zoom(const UiSvgEdit *ed)
{
	return ed ? ed->view.scale : 1.0f;
}

int Ui_SvgEdit_PenActive(const UiSvgEdit *ed)
{
	return ed ? ed->penActive : 0;
}

void Ui_SvgEdit_SetGrid(UiSvgEdit *ed, int on, float step)
{
	if (!ed) return;
	ed->gridOn = on;
	if (step > 0.5f) ed->gridStep = step;
	ed_invalidate(ed);
}

void Ui_SvgEdit_SetInputBlock(UiSvgEdit *ed, int blocked)
{
	if (!ed) return;
	ed->inputBlock = blocked ? 1 : 0;
}

int Ui_SvgEdit_GridOn(const UiSvgEdit *ed)
{
	return ed ? ed->gridOn : 0;
}

int Ui_SvgEdit_MouseWorld(const UiSvgEdit *ed, float *wx, float *wy)
{
	if (!ed || !ed->mouseWorldValid) return 0;
	if (wx) *wx = ed->mouseWorldX;
	if (wy) *wy = ed->mouseWorldY;
	return 1;
}

int Ui_SvgEdit_SelCount(const UiSvgEdit *ed)
{
	return ed ? ed->selCount : 0;
}

int Ui_SvgEdit_SelIndex(const UiSvgEdit *ed, int nth)
{
	if (!ed || nth < 0 || nth >= ed->selCount) return -1;
	return ed->sel[nth];
}

static SvgNode *sel_node(UiSvgEdit *ed)
{
	int idx = path_edit_index(ed);
	if (idx < 0 || ed->nodeSel < 0) return NULL;
	if (ed->nodeSel >= ed->doc->els[idx].path.count) return NULL;
	return &ed->doc->els[idx].path.nodes[ed->nodeSel];
}

int Ui_SvgEdit_NodeIndex(const UiSvgEdit *ed, int *elIndex)
{
	int idx;
	if (!ed) return -1;
	idx = path_edit_index(ed);
	if (idx < 0 || ed->nodeSel < 0 ||
		ed->nodeSel >= ed->doc->els[idx].path.count)
		return -1;
	if (elIndex) *elIndex = idx;
	return ed->nodeSel;
}

int Ui_SvgEdit_NodeParams(const UiSvgEdit *ed, float *angleDeg,
	float *strength, int *symmetric)
{
	SvgNode *n = sel_node((UiSvgEdit *)ed);
	if (!n) return 0;
	Svg_NodeGetTangent(n, angleDeg, strength, symmetric);
	return 1;
}

int Ui_SvgEdit_SetNodeParams(UiSvgEdit *ed, float angleDeg, float strength,
	int symmetric)
{
	SvgNode *n;
	int idx;
	if (!ed) return 0;
	idx = path_edit_index(ed);
	n = sel_node(ed);
	if (!n) return 0;
	Svg_NodeSetTangent(n, angleDeg, strength, symmetric);
	Svg_ElementTouch(&ed->doc->els[idx]);
	ed_invalidate(ed);
	return 1;
}

void Ui_SvgEdit_SelectAll(UiSvgEdit *ed)
{
	int i, n;
	if (!ed || !ed->doc) return;
	sel_clear(ed);
	n = SvgDoc_RootCount(ed->doc);
	for (i = 0; i < n; i++) sel_add(ed, SvgDoc_RootAt(ed->doc, i));
	ed_invalidate(ed);
}

void Ui_SvgEdit_ClearSel(UiSvgEdit *ed)
{
	if (!ed) return;
	sel_clear(ed);
	ed_invalidate(ed);
}

int Ui_SvgEdit_SelectionBounds(const UiSvgEdit *ed, GfxRect *out)
{
	return sel_bounds_cached(ed, out);
}

void Ui_SvgEdit_DeleteSel(UiSvgEdit *ed, UiContext *ui)
{
	int i, j, n;
	int *arr;
	if (!ed || !ed->doc || ed->selCount <= 0) return;
	n = ed->selCount;
	arr = (int *)s_alloc((size_t)n * sizeof(int));
	if (!arr) return;
	memcpy(arr, ed->sel, (size_t)n * sizeof(int));
	/* 从大到小删除, 避免下标前移 */
	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (arr[j] > arr[i]) {
				int t = arr[j];
				arr[j] = arr[i];
				arr[i] = t;
			}
		}
	}
	for (i = 0; i < n; i++)
		SvgDoc_Remove(ed->doc, arr[i]);
	s_free(arr);
	sel_clear(ed);
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
}

void Ui_SvgEdit_DuplicateSel(UiSvgEdit *ed, UiContext *ui)
{
	int i, n;
	int *added;
	if (!ed || !ed->doc || ed->selCount <= 0) return;
	n = ed->selCount;
	added = (int *)s_alloc((size_t)n * sizeof(int));
	if (!added) return;
	for (i = 0; i < n; i++) added[i] = -1;
	for (i = 0; i < n; i++) {
		int idx = ed->sel[i];
		if (idx < 0 || idx >= ed->doc->count) continue;
		added[i] = SvgDoc_Duplicate(ed->doc, idx);
	}
	cache_clear(ed);
	sel_clear(ed);
	for (i = 0; i < n; i++) {
		if (added[i] >= 0) sel_add(ed, added[i]);
	}
	s_free(added);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
}

static int sel_ancestor_selected_(const UiSvgEdit *ed, int idx)
{
	int p = ed->doc->els[idx].parent;
	while (p >= 0) {
		if (sel_has(ed, p)) return 1;
		p = ed->doc->els[p].parent;
	}
	return 0;
}

static int inv_linear_(const float *m, float *inv)
{
	float det = m[0] * m[3] - m[1] * m[2];
	if (det > -1e-6f && det < 1e-6f) return 0;
	inv[0] = m[3] / det;
	inv[1] = -m[1] / det;
	inv[2] = -m[2] / det;
	inv[3] = m[0] / det;
	return 1;
}

/* 世界位移 → 元素父空间位移 (父变换可能含旋转/缩放). */
static void move_world_(UiSvgEdit *ed, int idx, float dx, float dy)
{
	float pm[6], inv[6], lx = dx, ly = dy;
	SvgDoc_ParentMatrix(ed->doc, idx, pm);
	if (inv_linear_(pm, inv)) {
		lx = inv[0] * dx + inv[2] * dy;
		ly = inv[1] * dx + inv[3] * dy;
	}
	SvgDoc_Translate(ed->doc, idx, lx, ly);
}

int Ui_SvgEdit_CopySel(UiSvgEdit *ed)
{
	SvgClip *c;
	if (!ed || !ed->doc || ed->selCount <= 0) return 0;
	c = SvgDoc_ClipCopy(ed->doc, ed->sel, ed->selCount);
	if (!c) return 0;
	SvgDoc_ClipFree(ed->clip);
	ed->clip = c;
	return 1;
}

int Ui_SvgEdit_Paste(UiSvgEdit *ed, UiContext *ui)
{
	int roots[128];
	int n, i, base;
	if (!ed || !ed->clip) return 0;
	n = SvgDoc_ClipPaste(ed->doc, ed->clip, 12.0f, 12.0f, roots, 128);
	if (n <= 0) return 0;
	cache_clear(ed);
	sel_clear(ed);
	base = ed->doc->count - n;
	for (i = base; i < ed->doc->count; i++)
		if (ed->doc->els[i].parent < 0) sel_add(ed, i);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui; (void)roots;
	return 1;
}

int Ui_SvgEdit_Nudge(UiSvgEdit *ed, UiContext *ui, float dx, float dy)
{
	int i, any = 0;
	if (!ed || !ed->doc || ed->selCount <= 0) return 0;
	for (i = 0; i < ed->selCount; i++) {
		int idx = ed->sel[i];
		if (idx < 0 || idx >= ed->doc->count) continue;
		if (sel_ancestor_selected_(ed, idx)) continue;
		move_world_(ed, idx, dx, dy);
		Svg_ElementTouch(&ed->doc->els[idx]);
		any = 1;
	}
	if (!any) return 0;
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
	return 1;
}

typedef struct AlignItem { int idx; float c; } AlignItem;

int Ui_SvgEdit_Align(UiSvgEdit *ed, UiContext *ui, int mode)
{
	AlignItem items[256];
	GfxRect ub;
	int n = 0, i;
	if (!ed || !ed->doc || ed->selCount <= 0) return 0;
	ub = SvgDoc_ElementBounds(ed->doc, ed->sel[0]);
	ub.x = ub.y = 1e9f;
	ub.w = ub.h = -1e9f;
	for (i = 0; i < ed->selCount && n < 256; i++) {
		int idx = ed->sel[i];
		GfxRect b;
		if (idx < 0 || idx >= ed->doc->count) continue;
		if (sel_ancestor_selected_(ed, idx)) continue;
		b = SvgDoc_ElementBounds(ed->doc, idx);
		items[n].idx = idx;
		items[n].c = mode == 6 ? b.x + b.w * 0.5f
			: (mode == 7 ? b.y + b.h * 0.5f : 0.0f);
		n++;
		if (b.x < ub.x) ub.x = b.x;
		if (b.y < ub.y) ub.y = b.y;
		if (b.x + b.w > ub.x + ub.w) ub.w = b.x + b.w - ub.x;
		if (b.y + b.h > ub.y + ub.h) ub.h = b.y + b.h - ub.y;
	}
	if (n <= 0) return 0;
	if (mode == 6 || mode == 7) {
		if (n < 3) return 0;
		/* 按中心排序 (插入排序, 选择集通常很小) */
		for (i = 0; i < n; i++) {
			float f = items[i].c;
			int k = i - 1;
			while (k >= 0 && items[k].c > f) {
				AlignItem t = items[k + 1];
				items[k + 1] = items[k];
				items[k] = t;
				k--;
			}
		}
		{
			float first = items[0].c, last = items[n - 1].c;
			float step = (last - first) / (float)(n - 1);
			for (i = 0; i < n; i++) {
				float d = first + step * (float)i - items[i].c;
				if (mode == 6) move_world_(ed, items[i].idx, d, 0.0f);
				else move_world_(ed, items[i].idx, 0.0f, d);
				Svg_ElementTouch(&ed->doc->els[items[i].idx]);
			}
		}
	} else {
		for (i = 0; i < n; i++) {
			GfxRect b = SvgDoc_ElementBounds(ed->doc, items[i].idx);
			float dx = 0.0f, dy = 0.0f;
			switch (mode) {
			case 0: dx = ub.x - b.x; break;
			case 1: dx = (ub.x + ub.w * 0.5f) - (b.x + b.w * 0.5f); break;
			case 2: dx = (ub.x + ub.w) - (b.x + b.w); break;
			case 3: dy = ub.y - b.y; break;
			case 4: dy = (ub.y + ub.h * 0.5f) - (b.y + b.h * 0.5f); break;
			case 5: dy = (ub.y + ub.h) - (b.y + b.h); break;
			default: break;
			}
			if (dx == 0.0f && dy == 0.0f) continue;
			move_world_(ed, items[i].idx, dx, dy);
			Svg_ElementTouch(&ed->doc->els[items[i].idx]);
		}
	}
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
	return 1;
}

void Ui_SvgEdit_Reorder(UiSvgEdit *ed, UiContext *ui, int dir)
{
	if (!ed || !ed->doc || ed->selCount != 1) return;
	{
		int idx = ed->sel[0];
		uint32_t uid;
		if (idx < 0 || idx >= ed->doc->count) return;
		uid = SvgDoc_UidAt(ed->doc, idx);
		SvgDoc_MoveZ(ed->doc, idx, dir);
		cache_clear(ed);
		ed->sel[0] = SvgDoc_IndexOfUid(ed->doc, uid);
		if (ed->sel[0] < 0) ed->selCount = 0;
		Svg_History_Commit(ed->doc);
		ed_invalidate(ed);
	}
	(void)ui;
}

int Ui_SvgEdit_Group(UiSvgEdit *ed, UiContext *ui)
{
	int g;
	if (!ed || !ed->doc || ed->selCount <= 0) return 0;
	g = SvgDoc_Group(ed->doc, ed->sel, ed->selCount);
	if (g < 0) return 0;
	sel_clear(ed);
	sel_add(ed, g);
	ed->nodeSel = -1;
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
	return 1;
}

int Ui_SvgEdit_Ungroup(UiSvgEdit *ed, UiContext *ui)
{
	int g;
	if (!ed || !ed->doc || ed->selCount != 1) return 0;
	g = ed->sel[0];
	if (g < 0 || g >= ed->doc->count) return 0;
	if (ed->doc->els[g].type != SVG_EL_GROUP) return 0;
	if (SvgDoc_Ungroup(ed->doc, g) < 0) return 0;
	sel_clear(ed);
	ed->nodeSel = -1;
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
	return 1;
}

void Ui_SvgEdit_SelectIndex(UiSvgEdit *ed, int idx)
{
	if (!ed || !ed->doc || idx < 0 || idx >= ed->doc->count) return;
	sel_clear(ed);
	sel_add(ed, idx);
	ed->nodeSel = -1;
	ed_invalidate(ed);
}

void Ui_SvgEdit_SetVisible(UiSvgEdit *ed, int idx, int on)
{
	if (!ed || !ed->doc) return;
	SvgDoc_SetVisible(ed->doc, idx, on);
	ed_invalidate(ed);
}

void Ui_SvgEdit_SetLocked(UiSvgEdit *ed, int idx, int on)
{
	if (!ed || !ed->doc) return;
	SvgDoc_SetLocked(ed->doc, idx, on);
	ed_invalidate(ed);
}

void Ui_SvgEdit_Rename(UiSvgEdit *ed, int idx, const char *utf8)
{
	int i;
	char *dst;
	if (!ed || !ed->doc || idx < 0 || idx >= ed->doc->count || !utf8) return;
	dst = ed->doc->els[idx].name;
	for (i = 0; i < 47 && utf8[i]; i++) dst[i] = utf8[i];
	dst[i] = 0;
	ed_invalidate(ed);
}

int Ui_SvgEdit_MoveOut(UiSvgEdit *ed, UiContext *ui)
{
	int idx, parent, grand;
	if (!ed || !ed->doc || ed->selCount != 1) return 0;
	idx = ed->sel[0];
	if (idx < 0 || idx >= ed->doc->count) return 0;
	parent = ed->doc->els[idx].parent;
	if (parent < 0) return 0;
	grand = ed->doc->els[parent].parent;
	if (!SvgDoc_Reparent(ed->doc, idx, grand, -1)) return 0;
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
	return 1;
}

int Ui_SvgEdit_TextTarget(const UiSvgEdit *ed)
{
	int i;
	if (!ed || !ed->doc) return -1;
	for (i = 0; i < ed->selCount; i++) {
		int idx = ed->sel[i];
		if (idx < 0 || idx >= ed->doc->count) continue;
		if (ed->doc->els[idx].type == SVG_EL_TEXT) return idx;
	}
	return -1;
}

void Ui_SvgEdit_SetTextContent(UiSvgEdit *ed, UiContext *ui, const char *utf8)
{
	int idx = Ui_SvgEdit_TextTarget(ed);
	if (idx < 0) return;
	if (!SvgDoc_SetText(ed->doc, idx, utf8)) return;
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
}

void Ui_SvgEdit_SetTextFont(UiSvgEdit *ed, UiContext *ui,
	const char *family, float size, int weight)
{
	int idx = Ui_SvgEdit_TextTarget(ed);
	SvgElement *e;
	int i;
	if (idx < 0) return;
	e = &ed->doc->els[idx];
	if (family) {
		for (i = 0; i < 63 && family[i]; i++) e->family[i] = family[i];
		e->family[i] = 0;
	}
	if (size > 0.0f) e->fontSize = size;
	e->fontWeight = weight >= 600 ? 700 : 400;
	e->measVer = 0;
	e->version++;
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
}

void Ui_SvgEdit_SetTextAlign(UiSvgEdit *ed, UiContext *ui, int align,
	int anchor)
{
	int idx = Ui_SvgEdit_TextTarget(ed);
	if (idx < 0) return;
	ed->doc->els[idx].align = align < 0 ? 0 : (align > 2 ? 2 : align);
	ed->doc->els[idx].anchor = anchor < 0 ? 0 : (anchor > 2 ? 2 : anchor);
	ed->doc->els[idx].version++;
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
}

typedef struct TextPathBuild_ {
	SvgPath *path;
	int prev;
	int ok;
} TextPathBuild_;

static void tp_move_(void *u, float x, float y)
{
	TextPathBuild_ *b = (TextPathBuild_ *)u;
	int idx = Svg_PathMoveTo(b->path, x, y);
	if (idx < 0) { b->ok = 0; b->prev = -1; return; }
	b->prev = idx;
}

static void tp_line_(void *u, float x, float y)
{
	TextPathBuild_ *b = (TextPathBuild_ *)u;
	int idx;
	if (!b->ok || b->prev < 0) return;
	idx = Svg_PathAddNode(b->path, x, y, SVG_NODE_CORNER);
	if (idx < 0) { b->ok = 0; return; }
	b->path->seg[b->prev] = SVG_SEG_LINE;
	b->path->nodes[b->prev].hasOut = 0;
	b->path->nodes[idx].hasIn = 0;
	b->prev = idx;
}

static void tp_cubic_(void *u, float c1x, float c1y, float c2x, float c2y,
	float x, float y)
{
	TextPathBuild_ *b = (TextPathBuild_ *)u;
	int idx, p;
	if (!b->ok || b->prev < 0) return;
	p = b->prev;
	idx = Svg_PathAddNode(b->path, x, y, SVG_NODE_CORNER);
	if (idx < 0) { b->ok = 0; return; }
	b->path->seg[p] = SVG_SEG_CUBIC;
	b->path->nodes[p].hasOut = 1;
	b->path->nodes[p].outX = c1x - b->path->nodes[p].x;
	b->path->nodes[p].outY = c1y - b->path->nodes[p].y;
	b->path->nodes[idx].hasIn = 1;
	b->path->nodes[idx].inX = c2x - x;
	b->path->nodes[idx].inY = c2y - y;
	b->prev = idx;
}

static void tp_close_(void *u)
{
	(void)u; /* 闭合由最终 closed 标志统一表达 */
}

int Ui_SvgEdit_TextToPath(UiSvgEdit *ed, UiContext *ui)
{
	int idx = Ui_SvgEdit_TextTarget(ed);
	SvgElement *e;
	SvgPath path;
	GfxFont *f;
	GfxPathSink sink;
	TextPathBuild_ b;
	UiTheme *t;
	const char *s, *ls, *p;
	float axf, af, lh, blockL;
	int line = 0;
	if (idx < 0) return 0;
	e = &ed->doc->els[idx];
	text_measure_(ed, idx, e);
	f = text_font_(ed, e);
	if (!f || e->textW <= 0.0f) return 0;
	memset(&path, 0, sizeof(path));
	memset(&sink, 0, sizeof(sink));
	b.path = &path; b.prev = -1; b.ok = 1;
	sink.user = &b;
	sink.move_to = tp_move_;
	sink.line_to = tp_line_;
	sink.cubic_to = tp_cubic_;
	sink.close = tp_close_;
	axf = e->anchor == 1 ? 0.5f : (e->anchor == 2 ? 1.0f : 0.0f);
	af = e->align == 1 ? 0.5f : (e->align == 2 ? 1.0f : 0.0f);
	lh = e->fontSize * (e->lineH > 0.0f ? e->lineH : 1.25f);
	blockL = e->p.text.x - axf * e->textW;
	s = e->text;
	ls = s;
	for (p = s; ; p++) {
		if (*p == '\n' || *p == 0) {
			char buf[512];
			int n = (int)(p - ls);
			float w, x, y;
			if (n > 511) n = 511;
			memcpy(buf, ls, (size_t)n);
			buf[n] = 0;
			w = Gfx_TextWidth(ed->gfx, f, buf);
			x = blockL + (e->textW - w) * af;
			y = e->p.text.y + (float)line * lh;
			if (buf[0]) Gfx_TextOutline(ed->gfx, f, buf, x, y, &sink);
			line++;
			if (*p == 0) break;
			ls = p + 1;
		}
	}
	if (!b.ok || path.count < 2) { Svg_PathFree(&path); return 0; }
	path.closed = 1;
	path.seg[path.count - 1] = SVG_SEG_LINE; /* 末轮廓闭环段 */
	t = ed->ctx->theme;
	SvgDoc_SetText(ed->doc, idx, "");       /* 释放原文本并置空串 */
	Svg_PathFree(&e->path);
	e->path = path;
	e->type = SVG_EL_PATH;
	e->compound = 1;
	if (!(e->style.setMask & SVG_STYLE_FILL) ||
		e->style.fill.kind != SVG_PAINT_SOLID) {
		e->style.fill.kind = SVG_PAINT_SOLID;
		e->style.fill.color = t ? t->text : 0xFF202020u;
		e->style.setMask |= SVG_STYLE_FILL;
	}
	e->version++;
	ed->nodeSel = -1;
	cache_clear(ed);
	Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
	return 1;
}

typedef struct LayerWalk {
	UiSvgLayerRow *out;
	int cap, n;
} LayerWalk;

static int layer_walk_(SvgDoc *d, int idx, void *user)
{
	LayerWalk *lw = (LayerWalk *)user;
	SvgElement *e;
	int i;
	if (lw->n >= lw->cap) return 1;
	e = &d->els[idx];
	{
		UiSvgLayerRow *r = &lw->out[lw->n++];
		r->index = idx;
		r->parent = e->parent;
		r->depth = SvgDoc_DepthOf(d, idx);
		r->uid = e->uid;
		r->isGroup = e->type == SVG_EL_GROUP;
		r->visible = e->visible;
		r->locked = e->locked;
		r->childCount = e->childCount;
		for (i = 0; i < 47 && e->name[i]; i++) r->name[i] = e->name[i];
		r->name[i] = 0;
	}
	return 0;
}

int Ui_SvgEdit_Layers(const UiSvgEdit *ed, UiSvgLayerRow *out, int cap)
{
	LayerWalk lw;
	if (!ed || !ed->doc || !out || cap <= 0) return 0;
	lw.out = out;
	lw.cap = cap;
	lw.n = 0;
	SvgDoc_WalkDfs(ed->doc, 0, layer_walk_, &lw);
	return lw.n;
}

int Ui_SvgEdit_GetStyle(const UiSvgEdit *ed, SvgStyle *out)
{
	if (!ed || !out) return 0;
	memset(out, 0, sizeof(*out));
	if (ed->selCount > 0) {
		int idx = ed->sel[0];
		if (idx >= 0 && idx < ed->doc->count) {
			if (SvgDoc_ResolveStyle(ed->doc, idx, out)) return 1;
			memset(out, 0, sizeof(*out));
		}
	}
	Svg_StyleCopy(out, &ed->defStyle);
	return 1;
}

static void set_style_(UiSvgEdit *ed, UiContext *ui, const SvgStyle *style,
	int commit)
{
	int i;
	if (!ed || !style) return;
	Svg_StyleFree(&ed->defStyle);
	Svg_StyleCopy(&ed->defStyle, style);
	for (i = 0; i < ed->selCount; i++) {
		int idx = ed->sel[i];
		if (idx < 0 || idx >= ed->doc->count) continue;
		style_assign(&ed->doc->els[idx], style);
		Svg_ElementTouch(&ed->doc->els[idx]);
	}
	if (commit && ed->selCount > 0) Svg_History_Commit(ed->doc);
	ed_invalidate(ed);
	(void)ui;
}

void Ui_SvgEdit_SetStyle(UiSvgEdit *ed, UiContext *ui, const SvgStyle *style)
{
	set_style_(ed, ui, style, 1);
}

void Ui_SvgEdit_SetStyleLive(UiSvgEdit *ed, UiContext *ui, const SvgStyle *style)
{
	set_style_(ed, ui, style, 0);
}

void Ui_SvgEdit_Commit(UiSvgEdit *ed)
{
	if (!ed || !ed->doc) return;
	Svg_History_Commit(ed->doc);
}

void Ui_SvgEdit_Undo(UiSvgEdit *ed, UiContext *ui)
{
	if (!ed || !ed->doc) return;
	if (ed->penActive) pen_finish(ed, ui, 0, 0);
	if (!Svg_History_Undo(ed->doc)) return;
	sel_clear(ed);
	ed->nodeSel = -1;
	cache_clear(ed);
	ed_invalidate(ed);
}

void Ui_SvgEdit_Redo(UiSvgEdit *ed, UiContext *ui)
{
	if (!ed || !ed->doc) return;
	if (ed->penActive) pen_finish(ed, ui, 0, 0);
	if (!Svg_History_Redo(ed->doc)) return;
	sel_clear(ed);
	ed->nodeSel = -1;
	cache_clear(ed);
	ed_invalidate(ed);
}

int Ui_SvgEdit_CanUndo(const UiSvgEdit *ed)
{
	return ed && ed->doc ? Svg_History_CanUndo(ed->doc) : 0;
}

int Ui_SvgEdit_CanRedo(const UiSvgEdit *ed)
{
	return ed && ed->doc ? Svg_History_CanRedo(ed->doc) : 0;
}

const char *Ui_SvgEdit_Hint(const UiSvgEdit *ed)
{
	return ed ? ed->hint : "";
}
