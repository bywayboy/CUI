/*
 * ui_grid.c - Grid 布局: 显式/隐式轨道, fixed/auto/fr, minmax, span, dense
 *
 * 纯矩形求解, 不绘制、不注册控件。列先按内容定宽 (bounded 时 fr 吸收剩余),
 * 再以最终列宽测行高, 最后按单元格对齐/拉伸摆放。
 */
#include <windows.h>
#include "ui/ui_layout.h"

#define GRID_EPS 0.01f
#define GRID_BIG 1.0e30f
#define GRID_STACK_TRACKS 24
#define GRID_STACK_ITEMS 24
#define GRID_STACK_CELLS 512

typedef struct GridTrackState {
	int kind;
	float weight;
	float min;
	float pref;
	float max;
	float size;
	float pos;
} GridTrackState;

typedef struct GridScratch {
	GridTrackState colStack[GRID_STACK_TRACKS];
	GridTrackState rowStack[GRID_STACK_TRACKS];
	int itemRowStack[GRID_STACK_ITEMS];
	int itemColStack[GRID_STACK_ITEMS];
	unsigned char occStack[GRID_STACK_CELLS];
} GridScratch;

typedef struct GridCtx {
	int cols;
	int rows;
	int autoCols;
	int maxRows;
	float colGap;
	float rowGap;
	GridTrackState *col;
	GridTrackState *row;
	int *itemRow;
	int *itemCol;
	unsigned char *occ;
} GridCtx;

static float grid_max_(float a, float b)
{
	return a > b ? a : b;
}

static float grid_min_(float a, float b)
{
	return a < b ? a : b;
}

static float grid_clamp_(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static UiGridTrack grid_def_(const UiGridTrack *tracks, int count, int i)
{
	UiGridTrack t;
	if (tracks && i >= 0 && i < count) return tracks[i];
	t.kind = UI_GRID_AUTO;
	t.value = 0.0f;
	t.minSize = 0.0f;
	t.maxSize = 0.0f;
	return t;
}

static void grid_init_state_(GridTrackState *s, const UiGridTrack *def)
{
	float lo = def->minSize > 0.0f ? def->minSize : 0.0f;
	float hi = def->maxSize > 0.0f ? def->maxSize : GRID_BIG;
	float v;
	if (hi < lo) hi = lo;
	s->kind = def->kind;
	if (s->kind != UI_GRID_FIXED && s->kind != UI_GRID_FR)
		s->kind = UI_GRID_AUTO;
	s->weight = def->value > 0.0f ? def->value : 1.0f;
	if (s->kind == UI_GRID_FIXED) {
		v = grid_clamp_(def->value, lo, hi);
		s->min = s->pref = s->max = v;
	} else {
		s->min = s->pref = lo;
		s->max = hi;
	}
	s->size = 0.0f;
	s->pos = 0.0f;
}

static void grid_init_tracks_(GridTrackState *st, int n,
	const UiGridTrack *defs, int defCount)
{
	int i;
	for (i = 0; i < n; i++) {
		UiGridTrack def = grid_def_(defs, defCount, i);
		grid_init_state_(&st[i], &def);
	}
}

static void grid_measure_(const UiGridItem *it, float maxW, UiMeasure *m)
{
	m->maxW = maxW;
	m->minW = m->prefW = m->minH = m->prefH = 0.0f;
	m->baseline = 0.0f;
	if (it->measure) it->measure(it->user, m);
	Ui_MeasureNormalize(m);
}

static int grid_span_(int span, int start, int limit)
{
	if (span <= 0) span = 1;
	if (start < 0) start = 0;
	if (start + span > limit) span = limit - start;
	return span > 0 ? span : 1;
}

static int grid_mark_(unsigned char *occ, int cols, int maxRows,
	int r, int c, int rs, int cs)
{
	int rr, cc;
	if (r < 0 || c < 0 || rs < 1 || cs < 1 ||
		r + rs > maxRows || c + cs > cols) return 0;
	for (rr = 0; rr < rs; rr++)
		for (cc = 0; cc < cs; cc++)
			occ[(r + rr) * cols + c + cc] = 1;
	return 1;
}

static int grid_area_free_(const unsigned char *occ, int cols, int maxRows,
	int r, int c, int rs, int cs)
{
	int rr, cc;
	if (r < 0 || c < 0 || rs < 1 || cs < 1 ||
		r + rs > maxRows || c + cs > cols) return 0;
	for (rr = 0; rr < rs; rr++)
		for (cc = 0; cc < cs; cc++)
			if (occ[(r + rr) * cols + c + cc]) return 0;
	return 1;
}

static int grid_find_free_(const unsigned char *occ, int cols, int maxRows,
	int startR, int startC, int limitCols, int rs, int cs,
	int *outR, int *outC)
{
	int r, c, c0;
	for (r = startR; r + rs <= maxRows; r++) {
		c0 = r == startR ? startC : 0;
		for (c = c0; c + cs <= limitCols; c++) {
			if (grid_area_free_(occ, cols, maxRows, r, c, rs, cs)) {
				*outR = r;
				*outC = c;
				return 1;
			}
		}
	}
	return 0;
}

static int grid_place_(GridCtx *ctx, const UiGrid *g,
	const UiGridItem *items, int count)
{
	int i, r, c, cursorR = 0, cursorC = 0;
	for (i = 0; i < count; i++)
		ctx->itemRow[i] = ctx->itemCol[i] = -1;

	/* 显式行列先占位; 目标区域被跨格项占用时就近回填, 网格内不允许重叠. */
	for (i = 0; i < count; i++) {
		int rs, cs, r, c;
		if (items[i].row < 0 || items[i].col < 0) continue;
		rs = grid_span_(items[i].rowSpan, items[i].row, ctx->maxRows);
		cs = grid_span_(items[i].colSpan, items[i].col, ctx->cols);
		r = items[i].row;
		c = items[i].col;
		if (r + rs > ctx->maxRows || c + cs > ctx->cols) return 0;
		if (!grid_area_free_(ctx->occ, ctx->cols, ctx->maxRows, r, c, rs, cs) &&
			!grid_find_free_(ctx->occ, ctx->cols, ctx->maxRows, r, c,
				ctx->autoCols, rs, cs, &r, &c))
			return 0;
		grid_mark_(ctx->occ, ctx->cols, ctx->maxRows, r, c, rs, cs);
		ctx->itemRow[i] = r;
		ctx->itemCol[i] = c;
	}

	/* 半显式: 固定行找列, 固定列找行. */
	for (i = 0; i < count; i++) {
		int rs, cs;
		if (ctx->itemRow[i] >= 0) continue;
		rs = grid_span_(items[i].rowSpan, items[i].row, ctx->maxRows);
		cs = grid_span_(items[i].colSpan, items[i].col, ctx->cols);
		if (items[i].row >= 0) {
			int found = 0;
			r = items[i].row;
			if (r + rs > ctx->maxRows) return 0;
			for (c = 0; c + cs <= ctx->autoCols; c++) {
				if (grid_area_free_(ctx->occ, ctx->cols, ctx->maxRows,
					r, c, rs, cs)) {
					found = 1;
					break;
				}
			}
			if (!found) return 0;
			grid_mark_(ctx->occ, ctx->cols, ctx->maxRows, r, c, rs, cs);
			ctx->itemRow[i] = r;
			ctx->itemCol[i] = c;
		} else if (items[i].col >= 0) {
			int found = 0;
			c = items[i].col;
			if (c + cs > ctx->cols) return 0;
			for (r = 0; r + rs <= ctx->maxRows; r++) {
				if (grid_area_free_(ctx->occ, ctx->cols, ctx->maxRows,
					r, c, rs, cs)) {
					found = 1;
					break;
				}
			}
			if (!found) return 0;
			grid_mark_(ctx->occ, ctx->cols, ctx->maxRows, r, c, rs, cs);
			ctx->itemRow[i] = r;
			ctx->itemCol[i] = c;
		}
	}

	/* 全自动: sparse 用游标前进, dense 每项从 (0,0) 找最早空位. */
	for (i = 0; i < count; i++) {
		int rs, cs;
		int startR, startC;
		if (ctx->itemRow[i] >= 0) continue;
		rs = grid_span_(items[i].rowSpan, 0, ctx->maxRows);
		cs = grid_span_(items[i].colSpan, 0, ctx->cols);
		startR = g->dense ? 0 : cursorR;
		startC = g->dense ? 0 : cursorC;
		if (!grid_find_free_(ctx->occ, ctx->cols, ctx->maxRows,
			startR, startC, ctx->autoCols, rs, cs, &r, &c)) return 0;
		grid_mark_(ctx->occ, ctx->cols, ctx->maxRows, r, c, rs, cs);
		ctx->itemRow[i] = r;
		ctx->itemCol[i] = c;
		if (!g->dense) {
			cursorR = r;
			cursorC = c + cs;
			if (cursorC >= ctx->autoCols) {
				cursorR++;
				cursorC = 0;
			}
		}
	}
	return 1;
}

static void grid_ctx_free_(GridCtx *ctx, GridScratch *sc)
{
	if (ctx->col && ctx->col != sc->colStack)
		HeapFree(GetProcessHeap(), 0, ctx->col);
	if (ctx->row && ctx->row != sc->rowStack)
		HeapFree(GetProcessHeap(), 0, ctx->row);
	if (ctx->itemRow && ctx->itemRow != sc->itemRowStack)
		HeapFree(GetProcessHeap(), 0, ctx->itemRow);
	if (ctx->itemCol && ctx->itemCol != sc->itemColStack)
		HeapFree(GetProcessHeap(), 0, ctx->itemCol);
	if (ctx->occ && ctx->occ != sc->occStack)
		HeapFree(GetProcessHeap(), 0, ctx->occ);
	ctx->col = ctx->row = NULL;
	ctx->itemRow = ctx->itemCol = NULL;
	ctx->occ = NULL;
}

static int grid_prepare_(GridCtx *ctx, GridScratch *sc, const UiGrid *g,
	const UiGridItem *items, int count)
{
	int i, cols = 0, explicitRows = 0, maxSpanR = 1, boundRows, trackCap;
	SIZE_T cells;
	long long br;
	ctx->col = sc->colStack;
	ctx->row = sc->rowStack;
	ctx->itemRow = sc->itemRowStack;
	ctx->itemCol = sc->itemColStack;
	ctx->occ = sc->occStack;

	if (!ctx->col || !ctx->row || !ctx->itemRow || !ctx->itemCol || !ctx->occ)
		return 0;
	if (!g || !items || count <= 0) return 0;

	for (i = 0; i < count; i++) {
		int rs = items[i].rowSpan > 0 ? items[i].rowSpan : 1;
		int cs = items[i].colSpan > 0 ? items[i].colSpan : 1;
		if (rs > 65536 || cs > 65536) return 0;
		if (rs > maxSpanR) maxSpanR = rs;
		if (items[i].col >= 0 && items[i].col + cs > cols)
			cols = items[i].col + cs;
		if (items[i].row >= 0 && items[i].row + rs > explicitRows)
			explicitRows = items[i].row + rs;
	}
	if (g->cols > cols) cols = g->cols;
	if (cols <= 0) cols = 1;
	if (g->rows > explicitRows) explicitRows = g->rows;
	br = (long long)explicitRows + (long long)count * maxSpanR + 2;
	if (br > 1000000) return 0;
	boundRows = (int)br;
	if (boundRows < 1) boundRows = 1;

	trackCap = cols > boundRows ? cols : boundRows;
	cells = (SIZE_T)cols * (SIZE_T)boundRows;
	if (trackCap > GRID_STACK_TRACKS) {
		ctx->col = (GridTrackState *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)trackCap * sizeof(GridTrackState));
		ctx->row = (GridTrackState *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)trackCap * sizeof(GridTrackState));
		if (!ctx->col || !ctx->row) return 0;
	}
	if (count > GRID_STACK_ITEMS) {
		ctx->itemRow = (int *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)count * sizeof(int));
		ctx->itemCol = (int *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)count * sizeof(int));
		if (!ctx->itemRow || !ctx->itemCol) return 0;
	}
	if (cells > GRID_STACK_CELLS) {
		ctx->occ = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, cells);
		if (!ctx->occ) return 0;
	}

	ctx->cols = cols;
	ctx->autoCols = g->cols > 0 ? g->cols : cols;
	ctx->maxRows = boundRows;
	ctx->colGap = g->colGap > 0.0f ? g->colGap : 0.0f;
	ctx->rowGap = g->rowGap > 0.0f ? g->rowGap : 0.0f;

	for (i = 0; i < (int)cells; i++)
		ctx->occ[i] = 0;
	if (!grid_place_(ctx, g, items, count)) return 0;

	ctx->rows = 0;
	for (i = 0; i < count; i++) {
		int rs = grid_span_(items[i].rowSpan, ctx->itemRow[i], ctx->maxRows);
		if (ctx->itemRow[i] + rs > ctx->rows)
			ctx->rows = ctx->itemRow[i] + rs;
	}
	if (g->rows > ctx->rows) ctx->rows = g->rows;
	if (ctx->rows <= 0) ctx->rows = 1;
	return 1;
}

static void grid_grow_span_(GridTrackState *st, int n, int start, int span,
	float target, int prefField)
{
	int pass, i;
	if (start < 0 || span <= 0 || start + span > n || target <= 0.0f) return;
	for (pass = 0; pass < 4; pass++) {
		float sum = 0.0f, extra, added = 0.0f;
		int cand = 0;
		for (i = start; i < start + span; i++)
			sum += prefField ? st[i].pref : st[i].min;
		if (target <= sum + GRID_EPS) break;
		for (i = start; i < start + span; i++) {
			float v = prefField ? st[i].pref : st[i].min;
			if (st[i].kind != UI_GRID_FIXED && v < st[i].max - GRID_EPS)
				cand++;
		}
		if (cand <= 0) break;
		extra = (target - sum) / (float)cand;
		for (i = start; i < start + span; i++) {
			float v = prefField ? st[i].pref : st[i].min;
			float add;
			if (st[i].kind == UI_GRID_FIXED || v >= st[i].max - GRID_EPS)
				continue;
			add = grid_min_(extra, st[i].max - v);
			if (add <= 0.0f) continue;
			if (prefField) st[i].pref = v + add;
			else st[i].min = v + add;
			added += add;
		}
		if (added <= GRID_EPS) break;
	}
}

static void grid_add_content_(GridTrackState *st, int n, int start, int span,
	float minSize, float prefSize)
{
	if (minSize > 0.0f) grid_grow_span_(st, n, start, span, minSize, 0);
	if (prefSize > 0.0f) grid_grow_span_(st, n, start, span, prefSize, 1);
}

static void grid_finalize_tracks_(GridTrackState *st, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (st[i].max < st[i].min) st[i].max = st[i].min;
		if (st[i].min > st[i].max) st[i].min = st[i].max;
		if (st[i].pref < st[i].min) st[i].pref = st[i].min;
		if (st[i].pref > st[i].max) st[i].pref = st[i].max;
		st[i].size = st[i].pref;
	}
}

static void grid_column_content_(GridCtx *ctx, const UiGrid *g,
	const UiGridItem *items, int count)
{
	int pass, i;
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < count; i++) {
			UiMeasure m;
			int cs, c;
			float gap, minT, prefT;
			if (ctx->itemCol[i] < 0) continue;
			cs = grid_span_(items[i].colSpan, ctx->itemCol[i], ctx->cols);
			if ((pass == 0) != (cs == 1)) continue;
			grid_measure_(&items[i], -1.0f, &m);
			gap = ctx->colGap * (float)(cs - 1);
			minT = m.minW - gap;
			prefT = m.prefW - gap;
			if (minT < 0.0f) minT = 0.0f;
			if (prefT < 0.0f) prefT = 0.0f;
			c = ctx->itemCol[i];
			grid_add_content_(ctx->col, ctx->cols, c, cs, minT, prefT);
		}
	}
}

static void grid_row_content_(GridCtx *ctx, const UiGrid *g,
	const UiGridItem *items, int count)
{
	int pass, i;
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < count; i++) {
			UiMeasure m;
			int rs, cs, r, c, k;
			float cellW = 0.0f, gap, minT, prefT;
			if (ctx->itemRow[i] < 0) continue;
			rs = grid_span_(items[i].rowSpan, ctx->itemRow[i], ctx->rows);
			cs = grid_span_(items[i].colSpan, ctx->itemCol[i], ctx->cols);
			if ((pass == 0) != (rs == 1)) continue;
			c = ctx->itemCol[i];
			for (k = 0; k < cs; k++) cellW += ctx->col[c + k].size;
			cellW += ctx->colGap * (float)(cs - 1);
			grid_measure_(&items[i], cellW, &m);
			gap = ctx->rowGap * (float)(rs - 1);
			minT = m.minH - gap;
			prefT = m.prefH - gap;
			if (minT < 0.0f) minT = 0.0f;
			if (prefT < 0.0f) prefT = 0.0f;
			r = ctx->itemRow[i];
			grid_add_content_(ctx->row, ctx->rows, r, rs, minT, prefT);
		}
	}
}

static float grid_gap_step_(float gap, int n, float limit)
{
	float step;
	if (n <= 1 || gap <= 0.0f) return 0.0f;
	if (limit < 0.0f) return gap;
	step = limit / (float)(n - 1);
	return gap < step ? gap : step;
}

static void grid_distribute_free_(GridTrackState *st, int n, float free)
{
	int pass, i;
	for (pass = 0; pass < 4 && free > GRID_EPS; pass++) {
		float weight = 0.0f, added = 0.0f;
		for (i = 0; i < n; i++)
			if (st[i].kind == UI_GRID_FR && st[i].size < st[i].max - GRID_EPS)
				weight += st[i].weight;
		if (weight <= 0.0f) break;
		for (i = 0; i < n; i++) {
			float add;
			if (st[i].kind != UI_GRID_FR ||
				st[i].size >= st[i].max - GRID_EPS) continue;
			add = free * st[i].weight / weight;
			if (st[i].size + add > st[i].max)
				add = st[i].max - st[i].size;
			st[i].size += add;
			added += add;
		}
		free -= added;
		if (added <= GRID_EPS) break;
	}
}

static void grid_shrink_(GridTrackState *st, int n, float deficit)
{
	int pass, i;
	for (pass = 0; pass < 4 && deficit > GRID_EPS; pass++) {
		float capacity = 0.0f, reduced = 0.0f;
		for (i = 0; i < n; i++) {
			float cap;
			if (st[i].kind == UI_GRID_FIXED) continue;
			cap = st[i].size - st[i].min;
			if (cap > GRID_EPS) capacity += cap;
		}
		if (capacity <= GRID_EPS) break;
		for (i = 0; i < n; i++) {
			float cap, cut;
			if (st[i].kind == UI_GRID_FIXED) continue;
			cap = st[i].size - st[i].min;
			if (cap <= GRID_EPS) continue;
			cut = deficit * cap / capacity;
			if (cut > cap) cut = cap;
			st[i].size -= cut;
			reduced += cut;
		}
		deficit -= reduced;
		if (reduced <= GRID_EPS) break;
	}
}

static void grid_resolve_tracks_(GridTrackState *st, int n, float gap,
	float limit, int bounded, float *outGap)
{
	float gs, avail = 0.0f, total = 0.0f;
	int i;
	gs = grid_gap_step_(gap, n, bounded ? limit : -1.0f);
	for (i = 0; i < n; i++)
		st[i].size = (bounded && st[i].kind == UI_GRID_FR)
			? st[i].min : st[i].pref;
	if (bounded) {
		avail = limit - gs * (float)(n - 1);
		if (avail < 0.0f) avail = 0.0f;
		for (i = 0; i < n; i++) total += st[i].size;
		if (total < avail - GRID_EPS)
			grid_distribute_free_(st, n, avail - total);
		else if (total > avail + GRID_EPS)
			grid_shrink_(st, n, total - avail);
	}
	*outGap = gs;
}

static void grid_positions_(GridTrackState *st, int n, float gap, float origin)
{
	float p = origin;
	int i;
	for (i = 0; i < n; i++) {
		st[i].pos = p;
		p += st[i].size + gap;
	}
}

static float grid_sum_(const GridTrackState *st, int n, int pref)
{
	float v = 0.0f;
	int i;
	for (i = 0; i < n; i++) v += pref ? st[i].size : st[i].min;
	return v;
}

static float grid_span_size_(const GridTrackState *st, int start, int span,
	float gap)
{
	float v = 0.0f;
	int i;
	for (i = start; i < start + span; i++) v += st[i].size;
	v += gap * (float)(span - 1);
	return v;
}

void Ui_GridMeasure(const UiGrid *g,
	const UiGridTrack *colTracks, const UiGridTrack *rowTracks,
	const UiGridItem *items, int count, UiMeasure *out)
{
	GridCtx ctx;
	GridScratch sc;
	float maxW, gap, gapStep;
	if (!out) return;
	maxW = out->maxW;
	out->minW = out->prefW = out->minH = out->prefH = 0.0f;
	out->baseline = 0.0f;
	if (!grid_prepare_(&ctx, &sc, g, items, count)) {
		grid_ctx_free_(&ctx, &sc);
		return;
	}

	grid_init_tracks_(ctx.col, ctx.cols, colTracks, g->cols);
	grid_column_content_(&ctx, g, items, count);
	grid_finalize_tracks_(ctx.col, ctx.cols);
	grid_resolve_tracks_(ctx.col, ctx.cols, grid_max_(ctx.colGap, 0.0f),
		maxW, maxW >= 0.0f, &gapStep);
	gap = grid_max_(g->colGap, 0.0f);
	out->minW = grid_sum_(ctx.col, ctx.cols, 0) + gap * (float)(ctx.cols - 1);
	out->prefW = maxW >= 0.0f ? maxW
		: grid_sum_(ctx.col, ctx.cols, 1) + ctx.colGap * (float)(ctx.cols - 1);

	grid_init_tracks_(ctx.row, ctx.rows, rowTracks, g->rows);
	grid_row_content_(&ctx, g, items, count);
	grid_finalize_tracks_(ctx.row, ctx.rows);
	gap = grid_max_(g->rowGap, 0.0f);
	out->minH = grid_sum_(ctx.row, ctx.rows, 0) + gap * (float)(ctx.rows - 1);
	out->prefH = grid_sum_(ctx.row, ctx.rows, 1) + gap * (float)(ctx.rows - 1);
	Ui_MeasureNormalize(out);
	grid_ctx_free_(&ctx, &sc);
}

int Ui_GridLayout(UiRect inner, const UiGrid *g,
	const UiGridTrack *colTracks, const UiGridTrack *rowTracks,
	const UiGridItem *items, int count, UiRect *out)
{
	GridCtx ctx;
	GridScratch sc;
	int i;
	if (!g || !items || !out || count <= 0) return 0;
	if (!(inner.w > 0.0f) || !(inner.h > 0.0f)) return 0;
	if (!grid_prepare_(&ctx, &sc, g, items, count)) {
		grid_ctx_free_(&ctx, &sc);
		return 0;
	}

	grid_init_tracks_(ctx.col, ctx.cols, colTracks, g->cols);
	grid_column_content_(&ctx, g, items, count);
	grid_finalize_tracks_(ctx.col, ctx.cols);
	grid_resolve_tracks_(ctx.col, ctx.cols, ctx.colGap, inner.w, 1,
		&ctx.colGap);
	grid_positions_(ctx.col, ctx.cols, ctx.colGap, inner.x);

	grid_init_tracks_(ctx.row, ctx.rows, rowTracks, g->rows);
	grid_row_content_(&ctx, g, items, count);
	grid_finalize_tracks_(ctx.row, ctx.rows);
	grid_resolve_tracks_(ctx.row, ctx.rows, ctx.rowGap, inner.h, 1,
		&ctx.rowGap);
	grid_positions_(ctx.row, ctx.rows, ctx.rowGap, inner.y);

	for (i = 0; i < count; i++) {
		UiMeasure m;
		UiRect cell;
		float w, h, x, y;
		int rs, cs;
		if (ctx.itemRow[i] < 0 || ctx.itemCol[i] < 0) {
			out[i] = Ui_Rect(0, 0, 0, 0);
			continue;
		}
		rs = grid_span_(items[i].rowSpan, ctx.itemRow[i], ctx.rows);
		cs = grid_span_(items[i].colSpan, ctx.itemCol[i], ctx.cols);
		cell = Ui_Rect(ctx.col[ctx.itemCol[i]].pos,
			ctx.row[ctx.itemRow[i]].pos,
			grid_span_size_(ctx.col, ctx.itemCol[i], cs, ctx.colGap),
			grid_span_size_(ctx.row, ctx.itemRow[i], rs, ctx.rowGap));
		grid_measure_(&items[i], cell.w, &m);
		if (items[i].stretch) {
			out[i] = cell;
			continue;
		}
		w = grid_min_(m.prefW, cell.w);
		h = grid_min_(m.prefH, cell.h);
		if (w < 0.0f) w = 0.0f;
		if (h < 0.0f) h = 0.0f;
		x = cell.x;
		y = cell.y;
		if (items[i].align == UI_ALIGN_CENTER) {
			x += (cell.w - w) * 0.5f;
			y += (cell.h - h) * 0.5f;
		} else if (items[i].align == UI_ALIGN_END) {
			x += cell.w - w;
			y += cell.h - h;
		}
		out[i] = Ui_Rect(x, y, w, h);
	}
	grid_ctx_free_(&ctx, &sc);
	return 1;
}
