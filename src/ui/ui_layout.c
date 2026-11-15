#include <windows.h>
#include "ui/ui_layout.h"

#define LAYOUT_EPS 0.01f
#define LAYOUT_BIG 1.0e30f

static float layout_max(float a, float b)
{
	return a > b ? a : b;
}

static float layout_min(float a, float b)
{
	return a < b ? a : b;
}

static float stack_main_len(const UiStack *s)
{
	if (!s) return 0.0f;
	return s->dir == UI_COLUMN ? s->inner.h : s->inner.w;
}

static float stack_cross_len(const UiStack *s)
{
	if (!s) return 0.0f;
	return s->dir == UI_COLUMN ? s->inner.w : s->inner.h;
}

void Ui_StackBeginPad(UiStack *s, UiRect outer, int dir, float gap,
	float left, float top, float right, float bottom)
{
	if (!s) return;
	if (left < 0.0f) left = 0.0f;
	if (top < 0.0f) top = 0.0f;
	if (right < 0.0f) right = 0.0f;
	if (bottom < 0.0f) bottom = 0.0f;
	s->outer = outer;
	s->dir = dir == UI_COLUMN ? UI_COLUMN : UI_ROW;
	s->gap = gap > 0.0f ? gap : 0.0f;
	s->inner.x = outer.x + left;
	s->inner.y = outer.y + top;
	s->inner.w = layout_max(0.0f, outer.w - left - right);
	s->inner.h = layout_max(0.0f, outer.h - top - bottom);
	s->cursor = 0.0f;
	s->count = 0;
	s->fillUsed = 0;
}

void Ui_StackBegin(UiStack *s, UiRect outer, int dir, float gap, float padding)
{
	Ui_StackBeginPad(s, outer, dir, gap, padding, padding, padding, padding);
}

UiRect Ui_StackInner(const UiStack *s)
{
	return s ? s->inner : Ui_Rect(0, 0, 0, 0);
}

float Ui_StackRemaining(const UiStack *s)
{
	float used;
	if (!s) return 0.0f;
	used = s->cursor;
	if (s->count > 0) used += s->gap;
	return layout_max(0.0f, stack_main_len(s) - used);
}

static float stack_gap_before(UiStack *s)
{
	if (s->count > 0) s->cursor += s->gap;
	s->count++;
	return s->cursor;
}

static UiRect stack_place(const UiStack *s, float mainPos, float mainSize,
	float crossSize, int align)
{
	float crossLen = stack_cross_len(s);
	float crossPos;
	if (crossSize <= 0.0f || crossSize > crossLen) crossSize = crossLen;
	crossPos = s->dir == UI_COLUMN ? s->inner.x : s->inner.y;
	if (align == UI_ALIGN_CENTER)
		crossPos += (crossLen - crossSize) * 0.5f;
	else if (align == UI_ALIGN_END)
		crossPos += crossLen - crossSize;
	if (s->dir == UI_COLUMN)
		return Ui_Rect(crossPos, s->inner.y + mainPos, crossSize, mainSize);
	return Ui_Rect(s->inner.x + mainPos, crossPos, mainSize, crossSize);
}

UiRect Ui_StackTake(UiStack *s, float size)
{
	float pos;
	if (!s) return Ui_Rect(0, 0, 0, 0);
	if (size < 0.0f) size = 0.0f;
	pos = stack_gap_before(s);
	s->cursor = pos + size;
	return stack_place(s, pos, size, 0.0f, UI_ALIGN_START);
}

UiRect Ui_StackTakeFill(UiStack *s)
{
	float pos, size;
	if (!s) return Ui_Rect(0, 0, 0, 0);
	pos = stack_gap_before(s);
	if (s->fillUsed) {
		size = 0.0f;
	} else {
		size = layout_max(0.0f, stack_main_len(s) - pos);
		s->fillUsed = 1;
	}
	s->cursor = pos + size;
	return stack_place(s, pos, size, 0.0f, UI_ALIGN_START);
}

UiRect Ui_StackTakeRight(UiStack *s, float size)
{
	float pos;
	if (!s) return Ui_Rect(0, 0, 0, 0);
	if (size < 0.0f) size = 0.0f;
	pos = layout_max(s->cursor, stack_main_len(s) - size);
	s->count++;
	s->cursor = pos + size;
	return stack_place(s, pos, size, 0.0f, UI_ALIGN_START);
}

UiRect Ui_StackTakeAligned(UiStack *s, float mainSize, float crossSize, int align)
{
	float pos;
	if (!s) return Ui_Rect(0, 0, 0, 0);
	if (mainSize < 0.0f) mainSize = 0.0f;
	pos = stack_gap_before(s);
	s->cursor = pos + mainSize;
	return stack_place(s, pos, mainSize, crossSize, align);
}

void Ui_StackSkip(UiStack *s, float amount)
{
	if (!s || amount <= 0.0f) return;
	s->cursor += amount;
}

static float flex_size_get(const UiRect *out, int dir, int i)
{
	return dir == UI_COLUMN ? out[i].h : out[i].w;
}

static void flex_size_set(UiRect *out, int dir, int i, float size)
{
	if (dir == UI_COLUMN) out[i].h = size;
	else out[i].w = size;
}

static int flex_solve(UiRect inner, int dir, float gap,
	const UiFlexItem *items, int count, UiRect *out)
{
	float mainLen, crossLen, totalGap, gapStep, available;
	float freeSpace;
	int i, pass;

	if (!items || !out || count <= 0) return 0;
	mainLen = dir == UI_COLUMN ? inner.h : inner.w;
	crossLen = dir == UI_COLUMN ? inner.w : inner.h;
	if (mainLen <= 0.0f || crossLen <= 0.0f) return 0;

	totalGap = count > 1 ? (gap > 0.0f ? gap * (float)(count - 1) : 0.0f) : 0.0f;
	if (totalGap > mainLen) totalGap = mainLen;
	gapStep = count > 1 ? totalGap / (float)(count - 1) : 0.0f;
	available = layout_max(0.0f, mainLen - totalGap);

	freeSpace = available;
	for (i = 0; i < count; i++) {
		float basis = items[i].basis > 0.0f ? items[i].basis : 0.0f;
		float minSize = items[i].minSize > 0.0f ? items[i].minSize : 0.0f;
		float maxSize = items[i].maxSize > 0.0f ? items[i].maxSize : mainLen;
		float size;
		if (maxSize < minSize) maxSize = minSize;
		size = layout_min(layout_max(basis, minSize), maxSize);
		out[i] = Ui_Rect(0, 0, 0, 0);
		flex_size_set(out, dir, i, size);
		freeSpace -= size;
	}

	/* 把剩余空间按 grow 分配; 触到 max 的项冻结, 余量下一轮再分. */
	for (pass = 0; pass < 4 && freeSpace > 0.01f; pass++) {
		float totalGrow = 0.0f;
		float leftover = 0.0f;
		for (i = 0; i < count; i++) {
			float maxSize = items[i].maxSize > 0.0f ? items[i].maxSize : mainLen;
			float size = flex_size_get(out, dir, i);
			if (items[i].grow > 0.0f && size < maxSize - 0.01f)
				totalGrow += items[i].grow;
		}
		if (totalGrow <= 0.0f) break;
		for (i = 0; i < count; i++) {
			float maxSize, size, add, next;
			if (items[i].grow <= 0.0f) continue;
			maxSize = items[i].maxSize > 0.0f ? items[i].maxSize : mainLen;
			size = flex_size_get(out, dir, i);
			if (size >= maxSize - 0.01f) continue;
			add = freeSpace * items[i].grow / totalGrow;
			next = size + add;
			if (next > maxSize) {
				leftover += next - maxSize;
				next = maxSize;
			}
			flex_size_set(out, dir, i, next);
		}
		freeSpace = leftover;
	}

	{
		float pos = 0.0f;
		for (i = 0; i < count; i++) {
			float size = flex_size_get(out, dir, i);
			if (dir == UI_COLUMN) {
				out[i].x = inner.x;
				out[i].y = inner.y + pos;
				out[i].w = crossLen;
				out[i].h = size;
			} else {
				out[i].x = inner.x + pos;
				out[i].y = inner.y;
				out[i].w = size;
				out[i].h = crossLen;
			}
			pos += size + gapStep;
		}
	}
	return 1;
}

int Ui_FlexRow(UiRect inner, float gap,
	const UiFlexItem *items, int count, UiRect *out)
{
	return flex_solve(inner, UI_ROW, gap, items, count, out);
}

int Ui_FlexCol(UiRect inner, float gap,
	const UiFlexItem *items, int count, UiRect *out)
{
	return flex_solve(inner, UI_COLUMN, gap, items, count, out);
}

/* ── measure 协议 ─────────────────────────────────────────────────── */

void Ui_MeasureNormalize(UiMeasure *m)
{
	if (!m) return;
	if (!(m->minW > 0.0f)) m->minW = 0.0f;
	if (!(m->prefW >= m->minW)) m->prefW = m->minW;
	if (!(m->minH > 0.0f)) m->minH = 0.0f;
	if (!(m->prefH >= m->minH)) m->prefH = m->minH;
	if (!(m->baseline > 0.0f)) m->baseline = 0.0f;
	if (m->baseline > m->prefH) m->baseline = m->prefH;
}

void Ui_MeasureFixed(void *user, UiMeasure *m)
{
	const UiFixedSize *s = (const UiFixedSize *)user;
	if (!s) return;
	m->minW = m->prefW = s->w;
	m->minH = m->prefH = s->h;
}

static void layout_measure_(const UiMeasureItem *it, float maxW, UiMeasure *m)
{
	m->maxW = maxW;
	m->minW = m->prefW = m->minH = m->prefH = 0.0f;
	if (it && it->measure) it->measure(it->user, m);
	Ui_MeasureNormalize(m);
}

static float layout_main_(const UiMeasure *m, int dir)
{
	return dir == UI_COLUMN ? m->prefH : m->prefW;
}

static float layout_main_min_(const UiMeasure *m, int dir)
{
	return dir == UI_COLUMN ? m->minH : m->minW;
}

static float layout_item_min_(const UiMeasureItem *it, const UiMeasure *m, int dir)
{
	float v = layout_main_min_(m, dir);
	if (it->minSize > v) v = it->minSize;
	return v;
}

static float layout_item_max_(const UiMeasureItem *it, float hardMin)
{
	float v = it->maxSize > 0.0f ? it->maxSize : LAYOUT_BIG;
	return v < hardMin ? hardMin : v;
}

static float layout_cross_clamp_(const UiMeasureItem *it, float v)
{
	float lo = it->minCross > 0.0f ? it->minCross : 0.0f;
	float hi = it->maxCross > 0.0f ? it->maxCross : LAYOUT_BIG;
	if (hi < lo) hi = lo;
	if (v < lo) v = lo;
	if (v > hi) v = hi;
	return v;
}

/* 交叉轴自然尺寸: crossSize 固定值优先, 其余取测量值, 再夹 min/max. */
static void layout_item_cross_(const UiMeasureItem *it, const UiMeasure *m,
	int dir, float *outMin, float *outPref)
{
	float mn = dir == UI_COLUMN ? m->minW : m->minH;
	float pref = dir == UI_COLUMN ? m->prefW : m->prefH;
	if (it->crossSize > 0.0f) mn = pref = it->crossSize;
	mn = layout_cross_clamp_(it, mn);
	pref = layout_cross_clamp_(it, pref);
	if (pref < mn) pref = mn;
	*outMin = mn;
	*outPref = pref;
}

/* 摆放阶段交叉轴尺寸: 拉伸/固定/自然值, 再夹 min/max. */
static float layout_arrange_cross_(const UiMeasureItem *it, const UiMeasure *m,
	int dir, float crossLen)
{
	float v;
	if (it->crossSize > 0.0f) v = it->crossSize;
	else if (it->stretch) v = crossLen;
	else if (dir == UI_COLUMN) v = layout_min(m->prefW, crossLen);
	else v = m->prefH;
	return layout_cross_clamp_(it, v);
}

/* 主轴分配: 空间不足按可压缩量等比收缩 (最低到硬下限, 仍不足则溢出),
 * 富余按 grow 分配 (最高到硬上限). */
static void layout_distribute_(float available, int count, const float *grow,
	const float *hardMin, const float *hardMax, float *size)
{
	float total = 0.0f;
	int i, pass;
	for (i = 0; i < count; i++) total += size[i];
	if (total > available + LAYOUT_EPS) {
		float deficit = total - available;
		for (pass = 0; pass < 4 && deficit > LAYOUT_EPS; pass++) {
			float capacity = 0.0f, reduced = 0.0f;
			for (i = 0; i < count; i++) {
				float cap = size[i] - hardMin[i];
				if (cap > LAYOUT_EPS) capacity += cap;
			}
			if (capacity <= LAYOUT_EPS) break;
			for (i = 0; i < count; i++) {
				float cap = size[i] - hardMin[i];
				float cut;
				if (cap <= LAYOUT_EPS) continue;
				cut = deficit * cap / capacity;
				if (cut > cap) cut = cap;
				size[i] -= cut;
				reduced += cut;
			}
			deficit -= reduced;
			if (reduced <= LAYOUT_EPS) break;
		}
	} else if (total < available - LAYOUT_EPS) {
		float spare = available - total;
		for (pass = 0; pass < 4 && spare > LAYOUT_EPS; pass++) {
			float weight = 0.0f, added = 0.0f;
			for (i = 0; i < count; i++) {
				if (grow[i] > 0.0f && size[i] < hardMax[i] - LAYOUT_EPS)
					weight += grow[i];
			}
			if (weight <= 0.0f) break;
			for (i = 0; i < count; i++) {
				float add;
				if (grow[i] <= 0.0f || size[i] >= hardMax[i] - LAYOUT_EPS)
					continue;
				add = spare * grow[i] / weight;
				if (size[i] + add > hardMax[i]) add = hardMax[i] - size[i];
				size[i] += add;
				added += add;
			}
			spare -= added;
			if (added <= LAYOUT_EPS) break;
		}
	}
}

void Ui_FlexMeasure(int dir, float gap,
	const UiMeasureItem *items, int count, UiMeasure *out)
{
	int row, i;
	if (!out) return;
	out->maxW = 0.0f;
	out->minW = out->prefW = out->minH = out->prefH = 0.0f;
	if (!items || count <= 0) return;
	if (gap < 0.0f) gap = 0.0f;
	row = dir != UI_COLUMN;
	for (i = 0; i < count; i++) {
		UiMeasure m;
		float mn, pref;
		layout_measure_(&items[i], -1.0f, &m);
		mn = layout_item_min_(&items[i], &m, dir);
		pref = layout_main_(&m, dir);
		if (pref < mn) pref = mn;
		if (items[i].maxSize > 0.0f && items[i].maxSize < pref)
			pref = items[i].maxSize;
		if (pref < mn) pref = mn;
		{
			float cmn, cpref;
			layout_item_cross_(&items[i], &m, dir, &cmn, &cpref);
			if (row) {
				out->minW += mn;
				out->prefW += pref;
				if (i > 0) {
					out->minW += gap;
					out->prefW += gap;
				}
				if (cmn > out->minH) out->minH = cmn;
				if (cpref > out->prefH) out->prefH = cpref;
			} else {
				out->minH += mn;
				out->prefH += pref;
				if (i > 0) {
					out->minH += gap;
					out->prefH += gap;
				}
				if (cmn > out->minW) out->minW = cmn;
				if (cpref > out->prefW) out->prefW = cpref;
			}
		}
	}
	Ui_MeasureNormalize(out);
}

int Ui_FlexLayout(UiRect inner, int dir, float gap,
	const UiMeasureItem *items, int count, UiRect *out)
{
	float stackbuf[4 * 32];
	float *mem = stackbuf;
	float *size, *hardMin, *hardMax, *grow;
	float mainLen, crossLen, totalGap, gapStep, available, pos, baseline;
	int row, i;
	if (!items || !out || count <= 0) return 0;
	if (!(inner.w > 0.0f) || !(inner.h > 0.0f)) return 0;
	if (gap < 0.0f) gap = 0.0f;
	if (count > 32) {
		mem = (float *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)count * 4 * sizeof(float));
		if (!mem) return 0;
	}
	size = mem;
	hardMin = mem + count;
	hardMax = mem + count * 2;
	grow = mem + count * 3;

	row = dir != UI_COLUMN;
	mainLen = row ? inner.w : inner.h;
	crossLen = row ? inner.h : inner.w;
	totalGap = count > 1 ? gap * (float)(count - 1) : 0.0f;
	if (totalGap > mainLen) totalGap = mainLen;
	gapStep = count > 1 ? totalGap / (float)(count - 1) : 0.0f;
	available = layout_max(0.0f, mainLen - totalGap);

	for (i = 0; i < count; i++) {
		UiMeasure m;
		float pref;
		/* 行: 自然宽测宽; 列: 在容器宽下换行测高 */
		layout_measure_(&items[i], row ? -1.0f : inner.w, &m);
		hardMin[i] = layout_item_min_(&items[i], &m, dir);
		hardMax[i] = layout_item_max_(&items[i], hardMin[i]);
		pref = layout_main_(&m, dir);
		pref = layout_min(layout_max(pref, hardMin[i]), hardMax[i]);
		size[i] = pref;
		grow[i] = items[i].grow > 0.0f ? items[i].grow : 0.0f;
	}

	layout_distribute_(available, count, grow, hardMin, hardMax, size);

	baseline = 0.0f;
	if (row) {
		for (i = 0; i < count; i++) {
			UiMeasure bm;
			if (items[i].align != UI_ALIGN_BASELINE) continue;
			layout_measure_(&items[i], size[i], &bm);
			if (bm.baseline > baseline) baseline = bm.baseline;
		}
	}

	pos = 0.0f;
	for (i = 0; i < count; i++) {
		UiMeasure m;
		if (row) {
			float h, y = inner.y;
			layout_measure_(&items[i], size[i], &m);
			h = layout_arrange_cross_(&items[i], &m, dir, crossLen);
			if (items[i].align == UI_ALIGN_CENTER)
				y += (crossLen - h) * 0.5f;
			else if (items[i].align == UI_ALIGN_END)
				y += crossLen - h;
			else if (items[i].align == UI_ALIGN_BASELINE &&
				baseline > 0.0f && m.baseline > 0.0f)
				y += baseline - m.baseline;
			out[i] = Ui_Rect(inner.x + pos, y, size[i], h);
			pos += size[i] + gapStep;
		} else {
			float w, x = inner.x;
			layout_measure_(&items[i], inner.w, &m);
			w = layout_arrange_cross_(&items[i], &m, dir, crossLen);
			if (items[i].align == UI_ALIGN_CENTER)
				x += (crossLen - w) * 0.5f;
			else if (items[i].align == UI_ALIGN_END)
				x += crossLen - w;
			out[i] = Ui_Rect(x, inner.y + pos, w, size[i]);
			pos += size[i] + gapStep;
		}
	}

	if (mem != stackbuf) HeapFree(GetProcessHeap(), 0, mem);
	return 1;
}
