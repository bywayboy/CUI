#include "ui/ui_layout.h"

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
