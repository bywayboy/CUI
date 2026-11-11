#include "ui/ui_textarea.h"
#include "ui/ui_paint.h"
#include <windows.h>
#include <string.h>

#define TA_PAD_X 12.0f
#define TA_PAD_Y 8.0f
#define TA_LINE_GAP 4.0f
#define TA_INIT_TEXT 512
#define TA_INIT_LINES 64
#define TA_INIT_U8 2048

/* 布局模式 */
enum {
	TA_LAYOUT_CLEAN = 0,
	TA_LAYOUT_FULL = 1,
	TA_LAYOUT_APPEND = 2,
	TA_LAYOUT_REFLOW = 3
};

static void ta_insert_internal(UiTextArea *ta, const wchar_t *s, int n);

static int ta_sel_start(const UiTextArea *ta)
{
	return ta->anchor < ta->caret ? ta->anchor : ta->caret;
}

static int ta_sel_end(const UiTextArea *ta)
{
	return ta->anchor < ta->caret ? ta->caret : ta->anchor;
}

static int ta_has_sel(const UiTextArea *ta)
{
	return ta->anchor != ta->caret;
}

static int ta_is_high(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
static int ta_is_low(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

static int ta_is_word(wchar_t c)
{
	return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z')
		|| (c >= L'a' && c <= L'z') || c == L'_' || c >= 0x80;
}

/* ── 动态缓冲 ──────────────────────────────────────────────────────── */

static int ta_ensure_text(UiTextArea *ta, int need)
{
	wchar_t *p;
	int cap;
	if (!ta || need <= 0) return 0;
	if (need <= ta->cap) return 1;
	if (ta->maxLen > 0 && need > ta->maxLen + 1) need = ta->maxLen + 1;
	if (need <= ta->cap) return 1;
	cap = ta->cap > 0 ? ta->cap : TA_INIT_TEXT;
	while (cap < need) {
		int next = cap < 4096 ? cap * 2 : cap + cap / 2;
		if (next <= cap) { cap = need; break; }
		cap = next;
	}
	p = ta->text
		? (wchar_t *)HeapReAlloc(GetProcessHeap(), 0, ta->text,
			(SIZE_T)cap * sizeof(wchar_t))
		: (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			(SIZE_T)cap * sizeof(wchar_t));
	if (!p) return 0;
	ta->text = p;
	ta->cap = cap;
	return 1;
}

static int ta_ensure_u8(UiTextArea *ta, int need)
{
	char *p;
	int cap;
	if (!ta || need <= 0) return 0;
	if (need <= ta->u8cap) return 1;
	cap = ta->u8cap > 0 ? ta->u8cap : TA_INIT_U8;
	while (cap < need) {
		int next = cap < 8192 ? cap * 2 : cap + cap / 2;
		if (next <= cap) { cap = need; break; }
		cap = next;
	}
	p = ta->u8
		? (char *)HeapReAlloc(GetProcessHeap(), 0, ta->u8, (SIZE_T)cap)
		: (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cap);
	if (!p) return 0;
	ta->u8 = p;
	ta->u8cap = cap;
	return 1;
}

static int ta_ensure_lines(UiTextArea *ta, int need)
{
	int cap;
	if (!ta || need <= 0) return 0;
	if (need <= ta->lineCap) return 1;
	cap = ta->lineCap > 0 ? ta->lineCap : TA_INIT_LINES;
	while (cap < need) {
		int next = cap < 4096 ? cap * 2 : cap + cap / 2;
		if (next <= cap) { cap = need; break; }
		cap = next;
	}
	{
		int *ps = ta->lineStart
			? (int *)HeapReAlloc(GetProcessHeap(), 0, ta->lineStart,
				(SIZE_T)cap * sizeof(int))
			: (int *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)cap * sizeof(int));
		int *pl;
		float *pw;
		if (!ps) return 0;
		ta->lineStart = ps;
		pl = ta->lineLen
			? (int *)HeapReAlloc(GetProcessHeap(), 0, ta->lineLen,
				(SIZE_T)cap * sizeof(int))
			: (int *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)cap * sizeof(int));
		if (!pl) return 0;
		ta->lineLen = pl;
		pw = ta->lineW
			? (float *)HeapReAlloc(GetProcessHeap(), 0, ta->lineW,
				(SIZE_T)cap * sizeof(float))
			: (float *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)cap * sizeof(float));
		if (!pw) return 0;
		ta->lineW = pw;
	}
	ta->lineCap = cap;
	return 1;
}

static int ta_ensure_scratch(UiTextArea *ta, int need)
{
	int cap;
	if (!ta || need <= 0) return 0;
	if (need <= ta->scratchCap) return 1;
	cap = ta->scratchCap > 0 ? ta->scratchCap : TA_INIT_LINES;
	while (cap < need) {
		int next = cap < 4096 ? cap * 2 : cap + cap / 2;
		if (next <= cap) { cap = need; break; }
		cap = next;
	}
	{
		int *ps = ta->scratchStart
			? (int *)HeapReAlloc(GetProcessHeap(), 0, ta->scratchStart,
				(SIZE_T)cap * sizeof(int))
			: (int *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)cap * sizeof(int));
		int *pl;
		float *pw;
		if (!ps) return 0;
		ta->scratchStart = ps;
		pl = ta->scratchLen
			? (int *)HeapReAlloc(GetProcessHeap(), 0, ta->scratchLen,
				(SIZE_T)cap * sizeof(int))
			: (int *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)cap * sizeof(int));
		if (!pl) return 0;
		ta->scratchLen = pl;
		pw = ta->scratchW
			? (float *)HeapReAlloc(GetProcessHeap(), 0, ta->scratchW,
				(SIZE_T)cap * sizeof(float))
			: (float *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)cap * sizeof(float));
		if (!pw) return 0;
		ta->scratchW = pw;
	}
	ta->scratchCap = cap;
	return 1;
}

static int ta_to_utf8(UiTextArea *ta, int start, int n)
{
	int m, need;
	if (!ta) return 0;
	if (n <= 0 || start < 0 || start + n > ta->len) {
		if (ta_ensure_u8(ta, 1)) ta->u8[0] = 0;
		return 0;
	}
	need = n > (0x7FFFFFFF - 8) / 3 ? 0x7FFFFFFF : n * 3 + 8;
	if (!ta_ensure_u8(ta, need)) {
		if (ta->u8cap > 0) ta->u8[0] = 0;
		return 0;
	}
	m = WideCharToMultiByte(CP_UTF8, 0, ta->text + start, n,
		ta->u8, ta->u8cap - 1, NULL, NULL);
	if (m < 0) m = 0;
	ta->u8[m] = 0;
	return m;
}

static int ta_w_to_utf8(UiTextArea *ta, const wchar_t *s, int n)
{
	int m, need;
	if (!ta || !s || n <= 0) {
		if (ta && ta_ensure_u8(ta, 1)) ta->u8[0] = 0;
		return 0;
	}
	need = n > (0x7FFFFFFF - 8) / 3 ? 0x7FFFFFFF : n * 3 + 8;
	if (!ta_ensure_u8(ta, need)) {
		if (ta->u8cap > 0) ta->u8[0] = 0;
		return 0;
	}
	m = WideCharToMultiByte(CP_UTF8, 0, s, n, ta->u8, ta->u8cap - 1,
		NULL, NULL);
	if (m < 0) m = 0;
	ta->u8[m] = 0;
	return m;
}

static float ta_measure(UiTextArea *ta, Gfx *g, GfxFont *f, int start, int n)
{
	if (n <= 0) return 0.0f;
	ta_to_utf8(ta, start, n);
	return Gfx_TextWidth(g, f, ta->u8);
}

static float ta_measure_w(UiTextArea *ta, Gfx *g, GfxFont *f,
	const wchar_t *s, int n)
{
	if (n <= 0) return 0.0f;
	ta_w_to_utf8(ta, s, n);
	return Gfx_TextWidth(g, f, ta->u8);
}

static void ta_invalidate(UiTextArea *ta, UiContext *ui)
{
	RECT r;
	float sc;
	if (!ta || !ui || !ui->hwnd) return;
	sc = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((ta->rc.x - 4.0f) * sc);
	r.top = (LONG)((ta->rc.y - 4.0f) * sc);
	r.right = (LONG)((ta->rc.x + ta->rc.w + 4.0f) * sc) + 1;
	r.bottom = (LONG)((ta->rc.y + ta->rc.h + 4.0f) * sc) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

/* ── 行布局 ────────────────────────────────────────────────────────── */

static int ta_line_of_arr(const UiTextArea *ta, int pos)
{
	int i, line = 0;
	for (i = 0; i < ta->lineCount; i++) {
		if (ta->lineStart[i] <= pos) line = i;
		else break;
	}
	return line;
}

/* 全文重排 (初始化/宽度变化/换行编辑). */
static void ta_layout_all(UiTextArea *ta, Gfx *g, GfxFont *f, float wrapW)
{
	int pos = 0;
	if (!ta) return;
	if (wrapW < 20.0f) wrapW = 20.0f;
	ta->lineCount = 0;
	if (ta->len <= 0) {
		if (ta_ensure_lines(ta, 1)) {
			ta->lineStart[0] = 0;
			ta->lineLen[0] = 0;
			ta->lineW[0] = 0.0f;
			ta->lineCount = 1;
		}
	} else {
		while (pos <= ta->len) {
			int ls = pos;
			int e = pos;
			float w = 0.0f;
			int step;
			while (e < ta->len && ta->text[e] != L'\n') {
				float cw;
				step = 1;
				if (ta_is_high(ta->text[e]) && e + 1 < ta->len &&
					ta_is_low(ta->text[e + 1]))
					step = 2;
				cw = ta_measure(ta, g, f, e, step);
				if (e > ls && w + cw > wrapW) break;
				w += cw;
				e += step;
			}
			if (!ta_ensure_lines(ta, ta->lineCount + 1)) break;
			ta->lineStart[ta->lineCount] = ls;
			ta->lineLen[ta->lineCount] = e - ls;
			ta->lineW[ta->lineCount] = w;
			ta->lineCount++;
			if (e >= ta->len) break;
			if (ta->text[e] == L'\n') {
				pos = e + 1;
				if (pos == ta->len) {
					if (ta_ensure_lines(ta, ta->lineCount + 1)) {
						ta->lineStart[ta->lineCount] = pos;
						ta->lineLen[ta->lineCount] = 0;
						ta->lineW[ta->lineCount] = 0.0f;
						ta->lineCount++;
					}
					break;
				}
			} else {
				pos = e;
			}
		}
		if (ta->lineCount == 0 && ta_ensure_lines(ta, 1)) {
			ta->lineStart[0] = 0;
			ta->lineLen[0] = 0;
			ta->lineW[0] = 0.0f;
			ta->lineCount = 1;
		}
	}
	ta->lastWrapW = wrapW;
	ta->layoutMode = TA_LAYOUT_CLEAN;
	ta->layoutLen = ta->len;
}

/* 末尾追加: 从基线最后一行继续量新字符, O(新增字符). */
static int ta_layout_append(UiTextArea *ta, Gfx *g, GfxFont *f, float wrapW)
{
	int pos = ta->layoutLen;
	int cur;
	if (ta->layoutMode != TA_LAYOUT_APPEND) return 0;
	if (pos > ta->len) return 0;
	if (ta->lineCount <= 0) return 0;
	cur = ta->lineCount - 1;
	while (pos < ta->len) {
		int step;
		float cw;
		if (ta->text[pos] == L'\n') {
			if (!ta_ensure_lines(ta, ta->lineCount + 1)) return 0;
			ta->lineStart[ta->lineCount] = pos + 1;
			ta->lineLen[ta->lineCount] = 0;
			ta->lineW[ta->lineCount] = 0.0f;
			ta->lineCount++;
			cur = ta->lineCount - 1;
			pos++;
			continue;
		}
		step = 1;
		if (ta_is_high(ta->text[pos]) && pos + 1 < ta->len &&
			ta_is_low(ta->text[pos + 1]))
			step = 2;
		cw = ta_measure(ta, g, f, pos, step);
		if (ta->lineLen[cur] > 0 && ta->lineW[cur] + cw > wrapW) {
			if (!ta_ensure_lines(ta, ta->lineCount + 1)) return 0;
			ta->lineStart[ta->lineCount] = pos;
			ta->lineLen[ta->lineCount] = 0;
			ta->lineW[ta->lineCount] = 0.0f;
			ta->lineCount++;
			cur = ta->lineCount - 1;
		}
		ta->lineLen[cur] += step;
		ta->lineW[cur] += cw;
		pos += step;
	}
	ta->layoutMode = TA_LAYOUT_CLEAN;
	ta->layoutLen = ta->len;
	return 1;
}

/* 段落级重排: 只重排编辑所在段落, 后续段落只平移 lineStart. */
static int ta_layout_reflow(UiTextArea *ta, Gfx *g, GfxFont *f, float wrapW)
{
	int pos, rangeStart, rangeEnd, oldEnd;
	int firstLine, lastOld, tailStart, tailCount;
	int nNew = 0, i, p;
	float w;
	if (ta->layoutMode != TA_LAYOUT_REFLOW) return 0;
	if (ta->lineCount <= 0) return 0;
	pos = ta->reflowPos;
	if (pos < 0) pos = 0;
	if (pos > ta->len) pos = ta->len;
	rangeEnd = pos;
	while (rangeEnd < ta->len && ta->text[rangeEnd] != L'\n') rangeEnd++;
	oldEnd = rangeEnd - ta->reflowDelta;
	if (oldEnd < 0) oldEnd = 0;
	if (oldEnd > ta->layoutLen) oldEnd = ta->layoutLen;

	/* 无换行编辑: 编辑行之前的行不受影响, 只从编辑行开始重排. */
	firstLine = ta_line_of_arr(ta, pos);
	rangeStart = ta->lineStart[firstLine];
	lastOld = ta_line_of_arr(ta, oldEnd);
	if (lastOld < firstLine) return 0;
	tailStart = lastOld + 1;
	tailCount = ta->lineCount - tailStart;
	if (tailCount < 0) tailCount = 0;

	/* 先把新行算到 scratch. */
	if (!ta_ensure_scratch(ta, 16)) return 0;
	p = rangeStart;
	do {
		int ls = p;
		int e = p;
		w = 0.0f;
		int step;
		while (e < rangeEnd) {
			float cw;
			step = 1;
			if (ta_is_high(ta->text[e]) && e + 1 < ta->len &&
				ta_is_low(ta->text[e + 1]))
				step = 2;
			if (e + step > rangeEnd) step = 1;
			cw = ta_measure(ta, g, f, e, step);
			if (e > ls && w + cw > wrapW) break;
			w += cw;
			e += step;
		}
		if (!ta_ensure_scratch(ta, nNew + 1)) return 0;
		ta->scratchStart[nNew] = ls;
		ta->scratchLen[nNew] = e - ls;
		ta->scratchW[nNew] = w;
		nNew++;
		if (e >= rangeEnd) break;
		if (e <= ls) e = ls + 1;   /* 防御: 避免死循环 */
		p = e;
	} while (p < rangeEnd);
	if (rangeStart == rangeEnd && nNew == 0) {
		if (!ta_ensure_scratch(ta, 1)) return 0;
		ta->scratchStart[0] = rangeStart;
		ta->scratchLen[0] = 0;
		ta->scratchW[0] = 0.0f;
		nNew = 1;
	}

	if (!ta_ensure_lines(ta, firstLine + nNew + tailCount)) return 0;
	/* 先把后续段落移到新位置. */
	if (tailCount > 0) {
		memmove(ta->lineStart + firstLine + nNew,
			ta->lineStart + tailStart, (size_t)tailCount * sizeof(int));
		memmove(ta->lineLen + firstLine + nNew,
			ta->lineLen + tailStart, (size_t)tailCount * sizeof(int));
		memmove(ta->lineW + firstLine + nNew,
			ta->lineW + tailStart, (size_t)tailCount * sizeof(float));
		for (i = 0; i < tailCount; i++)
			ta->lineStart[firstLine + nNew + i] += ta->reflowDelta;
	}
	for (i = 0; i < nNew; i++) {
		ta->lineStart[firstLine + i] = ta->scratchStart[i];
		ta->lineLen[firstLine + i] = ta->scratchLen[i];
		ta->lineW[firstLine + i] = ta->scratchW[i];
	}
	ta->lineCount = firstLine + nNew + tailCount;
	ta->layoutMode = TA_LAYOUT_CLEAN;
	ta->layoutLen = ta->len;
	return 1;
}

static void ta_sync_layout(UiTextArea *ta, Gfx *g, GfxFont *f, float wrapW)
{
	int widthChanged;
	if (!ta || !g || !f) return;
	if (wrapW < 20.0f) wrapW = 20.0f;
	widthChanged = wrapW > ta->lastWrapW + 0.01f ||
		wrapW < ta->lastWrapW - 0.01f;
	if (widthChanged || ta->layoutMode == TA_LAYOUT_FULL)
		ta_layout_all(ta, g, f, wrapW);
	else if (ta->layoutMode == TA_LAYOUT_APPEND) {
		if (!ta_layout_append(ta, g, f, wrapW))
			ta_layout_all(ta, g, f, wrapW);
	} else if (ta->layoutMode == TA_LAYOUT_REFLOW) {
		if (!ta_layout_reflow(ta, g, f, wrapW))
			ta_layout_all(ta, g, f, wrapW);
	}
}

/* 编辑后登记布局任务. */
static void ta_note_edit(UiTextArea *ta, int editPos, int newlineTouched,
	int atEndInsert)
{
	if (!ta) return;
	ta->changed = 1;
	ta->ensureCaret = 1;
	ta->hasXPref = 0;
	if (ta->layoutMode == TA_LAYOUT_FULL) return;
	if (newlineTouched) {
		ta->layoutMode = TA_LAYOUT_FULL;
		return;
	}
	if (atEndInsert && ta->layoutMode == TA_LAYOUT_CLEAN) {
		ta->layoutMode = TA_LAYOUT_APPEND;
		ta->appendOldLen = ta->layoutLen;
		return;
	}
	if (atEndInsert && ta->layoutMode == TA_LAYOUT_APPEND)
		return;
	if (ta->layoutMode == TA_LAYOUT_APPEND) {
		ta->layoutMode = TA_LAYOUT_REFLOW;
		ta->reflowPos = ta->appendOldLen < editPos
			? ta->appendOldLen : editPos;
	} else if (ta->layoutMode == TA_LAYOUT_CLEAN) {
		ta->layoutMode = TA_LAYOUT_REFLOW;
		ta->reflowPos = editPos;
	} else if (editPos < ta->reflowPos) {
		ta->reflowPos = editPos;
	}
	ta->reflowDelta = ta->len - ta->layoutLen;
}

/* ── 文本编辑 ──────────────────────────────────────────────────────── */

static void ta_delete_range(UiTextArea *ta, int a, int b)
{
	int i, nl = 0;
	if (!ta) return;
	if (a < 0) a = 0;
	if (b > ta->len) b = ta->len;
	if (b <= a) return;
	for (i = a; i < b; i++)
		if (ta->text[i] == L'\n') { nl = 1; break; }
	memmove(ta->text + a, ta->text + b,
		(size_t)(ta->len - b + 1) * sizeof(wchar_t));
	ta->len -= b - a;
	if (ta->len < 0) ta->len = 0;
	ta->text[ta->len] = 0;
	ta->caret = ta->anchor = a;
	ta_note_edit(ta, a, nl, 0);
}

static void ta_insert_internal(UiTextArea *ta, const wchar_t *s, int n)
{
	int i, room, a, b, oldLen, atEnd, nl = 0;
	if (!ta || !s || n <= 0) return;
	oldLen = ta->len;
	a = ta_sel_start(ta);
	b = ta_sel_end(ta);
	atEnd = !ta_has_sel(ta) && ta->caret == oldLen;
	if (b > a) {
		for (i = a; i < b; i++)
			if (ta->text[i] == L'\n') { nl = 1; break; }
		memmove(ta->text + a, ta->text + b,
			(size_t)(ta->len - b + 1) * sizeof(wchar_t));
		ta->len -= b - a;
		ta->caret = a;
		ta->anchor = a;
	}
	room = ta->maxLen > 0 ? ta->maxLen - ta->len : 0x7FFFFFFF;
	if (room <= 0) { ta->anchor = ta->caret; return; }
	if (n > room) n = room;
	if (!ta_ensure_text(ta, ta->len + n + 1)) {
		if (ta->cap <= 0) { ta->anchor = ta->caret; return; }
		if (n > ta->cap - ta->len - 1) n = ta->cap - ta->len - 1;
		if (n < 0) n = 0;
	}
	for (i = 0; i < n; i++) {
		wchar_t c = s[i];
		if (c == L'\r') continue;
		if (c != L'\n' && c != L'\t' && c < 32) continue;
		if (ta->maxLen > 0 && ta->len >= ta->maxLen) break;
		if (!ta_ensure_text(ta, ta->len + 2)) break;
		memmove(ta->text + ta->caret + 1, ta->text + ta->caret,
			(size_t)(ta->len - ta->caret + 1) * sizeof(wchar_t));
		ta->text[ta->caret++] = c;
		ta->len++;
		room--;
		if (c == L'\n') nl = 1;
	}
	ta->text[ta->len] = 0;
	ta->anchor = ta->caret;
	ta_note_edit(ta, a, nl, atEnd);
}

static void ta_drain_ime(UiTextArea *ta)
{
	if (!ta) return;
	if (ta->imeEdit.len > 0) {
		ta_insert_internal(ta, ta->imeEdit.text, ta->imeEdit.len);
		Edit_SetText(&ta->imeEdit, L"");
	}
}

static void ta_backspace(UiTextArea *ta)
{
	int a = ta_sel_start(ta), b = ta_sel_end(ta);
	int p = ta->caret;
	if (a != b) { ta_delete_range(ta, a, b); return; }
	if (p <= 0) return;
	if (p >= 2 && ta_is_low(ta->text[p - 1]) && ta_is_high(ta->text[p - 2]))
		p -= 2;
	else p -= 1;
	ta_delete_range(ta, p, ta->caret);
}

static void ta_delete_forward(UiTextArea *ta)
{
	int a = ta_sel_start(ta), b = ta_sel_end(ta);
	int p = ta->caret, e;
	if (a != b) { ta_delete_range(ta, a, b); return; }
	if (p >= ta->len) return;
	e = p + 1;
	if (e < ta->len && ta_is_high(ta->text[p]) && ta_is_low(ta->text[e]))
		e++;
	ta_delete_range(ta, p, e);
}

/* ── 光标映射 ──────────────────────────────────────────────────────── */

static int ta_col_of(const UiTextArea *ta, int line, int pos)
{
	int col = pos - ta->lineStart[line];
	if (col < 0) col = 0;
	if (col > ta->lineLen[line]) col = ta->lineLen[line];
	return col;
}

static float ta_x_of(UiTextArea *ta, Gfx *g, GfxFont *f, int line, int col)
{
	int i = ta->lineStart[line];
	int end = i + col;
	float w = 0.0f;
	int step;
	if (end > i + ta->lineLen[line]) end = i + ta->lineLen[line];
	while (i < end) {
		step = 1;
		if (ta_is_high(ta->text[i]) && i + 1 < ta->len &&
			ta_is_low(ta->text[i + 1]))
			step = 2;
		if (i + step > end) step = 1;
		w += ta_measure(ta, g, f, i, step);
		i += step;
	}
	return w;
}

static int ta_col_at_x(UiTextArea *ta, Gfx *g, GfxFont *f, int line,
	float targetX)
{
	int i = ta->lineStart[line];
	int end = i + ta->lineLen[line];
	int best = 0, step;
	float w = 0.0f, bestD = 1.0e9f;
	while (i < end) {
		float d = w > targetX ? w - targetX : targetX - w;
		if (d < bestD) { bestD = d; best = i - ta->lineStart[line]; }
		step = 1;
		if (ta_is_high(ta->text[i]) && i + 1 < ta->len &&
			ta_is_low(ta->text[i + 1]))
			step = 2;
		if (i + step > end) step = 1;
		w += ta_measure(ta, g, f, i, step);
		i += step;
		/* 跨过目标的字符之后也是一个候选列; 绝不能把候选列直接
		 * 写成 lineLen, 否则鼠标在行内也会跳到行末. */
		d = w > targetX ? w - targetX : targetX - w;
		if (d < bestD) { bestD = d; best = i - ta->lineStart[line]; }
		if (w >= targetX) break;
	}
	return best;
}

static void ta_set_caret(UiTextArea *ta, int pos, int shift)
{
	if (!ta) return;
	if (pos < 0) pos = 0;
	if (pos > ta->len) pos = ta->len;
	if (!shift) ta->anchor = pos;
	ta->caret = pos;
	ta->ensureCaret = 1;
	ta->hasXPref = 0;
}

static void ta_move_vertical(UiTextArea *ta, Gfx *g, GfxFont *f,
	int delta, int shift)
{
	int line, target, col;
	float relX;
	if (!ta) return;
	line = ta_line_of_arr(ta, ta->caret);
	if (!ta->hasXPref) {
		ta->xPref = ta_x_of(ta, g, f, line, ta_col_of(ta, line, ta->caret));
		ta->hasXPref = 1;
	}
	relX = ta->xPref;
	target = line + delta;
	if (target < 0) target = 0;
	if (target >= ta->lineCount) target = ta->lineCount - 1;
	col = ta_col_at_x(ta, g, f, target, relX);
	ta->caret = ta->lineStart[target] + col;
	if (ta->caret > ta->len) ta->caret = ta->len;
	if (!shift) ta->anchor = ta->caret;
	ta->ensureCaret = 1;
	ta->hasXPref = 1;
	ta->xPref = relX;
}

static void ta_move_home_end(UiTextArea *ta, int end, int doc, int shift)
{
	int line, pos;
	if (!ta) return;
	line = ta_line_of_arr(ta, ta->caret);
	if (doc) pos = end ? ta->len : 0;
	else pos = end ? ta->lineStart[line] + ta->lineLen[line]
		: ta->lineStart[line];
	ta_set_caret(ta, pos, shift);
}

/* ── 剪贴板 ────────────────────────────────────────────────────────── */

static void ta_clip_copy(UiTextArea *ta, HWND hwnd, int cut)
{
	int a, b, i, n = 0;
	HGLOBAL h;
	wchar_t *p;
	size_t cap;
	if (!ta) return;
	a = ta_sel_start(ta);
	b = ta_sel_end(ta);
	if (a == b) return;
	cap = (size_t)(b - a) * 2 + 1;
	h = GlobalAlloc(GMEM_MOVEABLE, cap * sizeof(wchar_t));
	if (!h) return;
	p = (wchar_t *)GlobalLock(h);
	if (!p) { GlobalFree(h); return; }
	if (OpenClipboard(hwnd)) {
		EmptyClipboard();
		for (i = a; i < b; i++) {
			if (ta->text[i] == L'\n') {
				p[n++] = L'\r';
				p[n++] = L'\n';
			} else {
				p[n++] = ta->text[i];
			}
		}
		p[n] = 0;
		SetClipboardData(CF_UNICODETEXT, h);
		CloseClipboard();
		GlobalUnlock(h);
		if (cut) ta_delete_range(ta, a, b);
		return;
	}
	GlobalUnlock(h);
	GlobalFree(h);
}

static void ta_clip_paste(UiTextArea *ta, HWND hwnd)
{
	HANDLE h;
	const wchar_t *p;
	wchar_t *buf;
	SIZE_T bytes;
	int cap, n = 0, prevCR = 0;
	if (!ta || !OpenClipboard(hwnd)) return;
	h = GetClipboardData(CF_UNICODETEXT);
	if (!h) { CloseClipboard(); return; }
	p = (const wchar_t *)GlobalLock(h);
	if (!p) { CloseClipboard(); return; }
	bytes = GlobalSize(h);
	cap = (int)(bytes / sizeof(wchar_t));
	if (cap < 1) cap = 1;
	buf = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)cap * sizeof(wchar_t));
	if (buf) {
		while (*p && n < cap - 1) {
			wchar_t c = *p++;
			if (c == L'\r') { buf[n++] = L'\n'; prevCR = 1; continue; }
			if (c == L'\n' && prevCR) { prevCR = 0; continue; }
			prevCR = 0;
			if (c == L'\t') {
				int k;
				for (k = 0; k < 4 && n < cap - 1; k++)
					buf[n++] = L' ';
				continue;
			}
			if (c < 32 && c != L'\n') continue;
			buf[n++] = c;
		}
		ta_insert_internal(ta, buf, n);
		HeapFree(GetProcessHeap(), 0, buf);
	}
	GlobalUnlock(h);
	CloseClipboard();
}

/* ── 生命周期 / 文本 API ───────────────────────────────────────────── */

void UiTextArea_Init(UiTextArea *ta, UiId id, int maxLen)
{
	UiScrollConfig cfg;
	UiScrollbarStyle bar;
	UiScrollPadding pad;
	if (!ta) return;
	ZeroMemory(ta, sizeof(*ta));
	ta->id = id;
	ta->maxLen = maxLen > 0 ? maxLen : 0;   /* 0 = 不限长 */
	ta->layoutMode = TA_LAYOUT_FULL;
	ta->layoutLen = 0;
	ta->blinkOn = 1;
	ta->lineCount = 1;
	Edit_Init(&ta->imeEdit, 0, 0, NULL);    /* 代理也不限长 */

	ta_ensure_text(ta, TA_INIT_TEXT);
	ta_ensure_u8(ta, TA_INIT_U8);
	ta_ensure_lines(ta, TA_INIT_LINES);
	if (ta->text) ta->text[0] = 0;
	if (ta->lineStart) {
		ta->lineStart[0] = 0;
		ta->lineLen[0] = 0;
		ta->lineW[0] = 0.0f;
	}

	cfg = UiScrollConfig_Desktop();
	cfg.wheelTauMs = 0.0f;
	cfg.bounce = 0;
	cfg.autoHideBar = 0;
	bar = UiScrollbarStyle_Default();
	bar.width = 6.0f;
	bar.minThumb = 24.0f;
	bar.margin = 2.0f;
	bar.radius = 3.0f;
	bar.track = 0x00FFFFFFu;
	bar.alwaysVisible = 1;
	bar.reserveSpace = 1;
	UiScrollView_Init(&ta->sv, id, &cfg, &bar);
	pad.left = pad.right = TA_PAD_X;
	pad.top = pad.bottom = TA_PAD_Y;
	UiScrollView_SetPadding(&ta->sv, pad);
	ta->sv.barOpacity = 1.0f;
	ta->sv.barTarget = 1.0f;
}

void UiTextArea_Shutdown(UiTextArea *ta)
{
	if (!ta) return;
	if (ta->ime && ta->ime->edit == &ta->imeEdit)
		UiIme_SetFocus(ta->ime, NULL);
	Edit_Shutdown(&ta->imeEdit);
	if (ta->text) HeapFree(GetProcessHeap(), 0, ta->text);
	if (ta->u8) HeapFree(GetProcessHeap(), 0, ta->u8);
	if (ta->lineStart) HeapFree(GetProcessHeap(), 0, ta->lineStart);
	if (ta->lineLen) HeapFree(GetProcessHeap(), 0, ta->lineLen);
	if (ta->lineW) HeapFree(GetProcessHeap(), 0, ta->lineW);
	if (ta->scratchStart) HeapFree(GetProcessHeap(), 0, ta->scratchStart);
	if (ta->scratchLen) HeapFree(GetProcessHeap(), 0, ta->scratchLen);
	if (ta->scratchW) HeapFree(GetProcessHeap(), 0, ta->scratchW);
	ta->text = NULL;
	ta->u8 = NULL;
	ta->lineStart = ta->lineLen = NULL;
	ta->lineW = NULL;
	ta->scratchStart = ta->scratchLen = NULL;
	ta->scratchW = NULL;
	ta->cap = ta->u8cap = ta->lineCap = ta->scratchCap = 0;
	ta->len = ta->lineCount = 0;
}

void UiTextArea_SetText(UiTextArea *ta, const wchar_t *s)
{
	int n;
	if (!ta) return;
	if (!s) s = L"";
	n = lstrlenW(s);
	if (ta->maxLen > 0 && n > ta->maxLen) n = ta->maxLen;
	if (!ta_ensure_text(ta, n + 1)) {
		if (ta->text && ta->cap > 0) ta->text[0] = 0;
		ta->len = 0;
		ta->caret = ta->anchor = 0;
		ta->layoutMode = TA_LAYOUT_FULL;
		return;
	}
	if (n > 0) memcpy(ta->text, s, (size_t)n * sizeof(wchar_t));
	ta->text[n] = 0;
	ta->len = n;
	ta->caret = ta->anchor = n;
	ta->layoutMode = TA_LAYOUT_FULL;
	ta->ensureCaret = 1;
}

void UiTextArea_SetTextUtf8(UiTextArea *ta, const char *s)
{
	int need, n;
	if (!ta) return;
	if (!s || !*s) { UiTextArea_SetText(ta, L""); return; }
	need = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (need <= 0) { UiTextArea_SetText(ta, L""); return; }
	if (ta->maxLen > 0 && need - 1 > ta->maxLen) need = ta->maxLen + 1;
	if (!ta_ensure_text(ta, need)) { UiTextArea_SetText(ta, L""); return; }
	n = MultiByteToWideChar(CP_UTF8, 0, s, -1, ta->text, need);
	if (n <= 0) { UiTextArea_SetText(ta, L""); return; }
	ta->text[need - 1] = 0;
	ta->len = n - 1;
	ta->caret = ta->anchor = ta->len;
	ta->layoutMode = TA_LAYOUT_FULL;
	ta->ensureCaret = 1;
}

void UiTextArea_SetCaret(UiTextArea *ta, int pos)
{
	if (!ta) return;
	if (pos < 0) pos = 0;
	if (pos > ta->len) pos = ta->len;
	ta->caret = ta->anchor = pos;
	ta->ensureCaret = 1;
	ta->hasXPref = 0;
}

void UiTextArea_TextUtf8(const UiTextArea *ta, char *out, int cap)
{
	int m;
	if (!out || cap <= 0) return;
	if (!ta || ta->len <= 0) { out[0] = 0; return; }
	m = WideCharToMultiByte(CP_UTF8, 0, ta->text, ta->len,
		out, cap - 1, NULL, NULL);
	if (m < 0) m = 0;
	out[m] = 0;
}

void UiTextArea_TextW(const UiTextArea *ta, wchar_t *out, int cap)
{
	int n;
	if (!out || cap <= 0) return;
	n = ta ? ta->len : 0;
	if (n > cap - 1) n = cap - 1;
	if (n > 0) memcpy(out, ta->text, (size_t)n * sizeof(wchar_t));
	out[n] = 0;
}

void UiTextArea_SetPlaceholder(UiTextArea *ta, const wchar_t *hint)
{
	if (ta) ta->placeholder = hint;
}

void UiTextArea_SetReadonly(UiTextArea *ta, int readonly)
{
	if (ta) ta->readonly = readonly != 0;
}

void UiTextArea_AttachIme(UiTextArea *ta, UiIme *ime)
{
	if (ta) ta->ime = ime;
}

/* ── 鼠标定位 ──────────────────────────────────────────────────────── */

static void ta_place_from_point(UiTextArea *ta, Gfx *g, GfxFont *f,
	float x, float y, int keepAnchor)
{
	int line, col, pos;
	float lineH = ta->lineH > 0.0f ? ta->lineH : 20.0f;
	if (!ta || ta->lineCount <= 0) return;
	line = (int)((y - ta->textRc.y + ta->sv.offset) / lineH);
	if (line < 0) line = 0;
	if (line >= ta->lineCount) line = ta->lineCount - 1;
	col = ta_col_at_x(ta, g, f, line, x - ta->textRc.x);
	pos = ta->lineStart[line] + col;
	if (pos > ta->len) pos = ta->len;
	if (!keepAnchor) ta->anchor = pos;
	ta->caret = pos;
	/* 鼠标点本身就在可视区内: 不要在这里触发自动滚动. 否则拖选时
	 * offset 被 caret 拉走, 同一鼠标位置会映射到别的行, 来回跳动. */
	ta->ensureCaret = 0;
	ta->hasXPref = 0;
}

/* 拖选到视口边缘/外部时按固定步长自动滚动; 只在鼠标按下时调用. */
static void ta_drag_autoscroll(UiTextArea *ta, UiContext *ui, float contentH)
{
	float top, bottom, y, step = 0.0f, maxOff, n;
	if (!ta || !ui) return;
	top = ta->sv.inner.y;
	bottom = top + ta->sv.inner.h;
	y = ui->mouseY;
	if (y < top)
		step = -8.0f - (top - y) * 0.5f;
	else if (y > bottom)
		step = 8.0f + (y - bottom) * 0.5f;
	else if (y < top + 6.0f)
		step = -6.0f;
	else if (y > bottom - 6.0f)
		step = 6.0f;
	if (step == 0.0f) return;
	if (step < -40.0f) step = -40.0f;
	if (step > 40.0f) step = 40.0f;
	maxOff = contentH - ta->sv.inner.h;
	if (maxOff < 0.0f) maxOff = 0.0f;
	n = ta->sv.offset + step;
	if (n < 0.0f) n = 0.0f;
	if (n > maxOff) n = maxOff;
	if (n != ta->sv.offset) {
		UiScrollView_ScrollTo(&ta->sv, ui, n, 0);
		ta_invalidate(ta, ui);   /* 指针不动时也继续滚 */
	}
}

/* ── 键盘 ──────────────────────────────────────────────────────────── */

int UiTextArea_Key(UiTextArea *ta, UiContext *ui, UINT vk)
{
	int shift, ctrl, handled = 0;
	float lineH, wrapW;
	if (!ta || !ui) return 0;
	ta_drain_ime(ta);
	if (ui->focusId != ta->id) return 0;
	wrapW = ta->rc.w > 0.0f
		? UiScrollView_ContentWidth(&ta->sv, ta->rc) : ta->lastWrapW;
	ta_sync_layout(ta, ui->gfx, ui->theme->fInput, wrapW);
	if (Edit_HasComposition(&ta->imeEdit) &&
		(vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB ||
			vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT))
		return 1;
	shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	lineH = ta->lineH > 0.0f ? ta->lineH : 20.0f;

	switch (vk) {
	case VK_LEFT:
		if (ctrl) {
			int p = ta->caret;
			if (p > 0 && ta_is_word(ta->text[p - 1])) {
				while (p > 0 && ta_is_word(ta->text[p - 1])) p--;
			} else {
				while (p > 0 && !ta_is_word(ta->text[p - 1])) p--;
			}
			ta_set_caret(ta, p, shift);
		} else {
			int p = ta->caret;
			if (p >= 2 && ta_is_low(ta->text[p - 1]) &&
				ta_is_high(ta->text[p - 2])) p -= 2;
			else if (p > 0) p--;
			ta_set_caret(ta, p, shift);
		}
		handled = 1;
		break;
	case VK_RIGHT:
		if (ctrl) {
			int p = ta->caret;
			if (p < ta->len && ta_is_word(ta->text[p])) {
				while (p < ta->len && ta_is_word(ta->text[p])) p++;
			} else {
				while (p < ta->len && !ta_is_word(ta->text[p])) p++;
			}
			ta_set_caret(ta, p, shift);
		} else {
			int p = ta->caret;
			if (p < ta->len && ta_is_high(ta->text[p]) &&
				p + 1 < ta->len && ta_is_low(ta->text[p + 1])) p += 2;
			else if (p < ta->len) p++;
			ta_set_caret(ta, p, shift);
		}
		handled = 1;
		break;
	case VK_UP:
		ta_move_vertical(ta, ui->gfx, ui->theme->fInput, -1, shift);
		handled = 1;
		break;
	case VK_DOWN:
		ta_move_vertical(ta, ui->gfx, ui->theme->fInput, 1, shift);
		handled = 1;
		break;
	case VK_HOME:
		ta_move_home_end(ta, 0, ctrl, shift);
		handled = 1;
		break;
	case VK_END:
		ta_move_home_end(ta, 1, ctrl, shift);
		handled = 1;
		break;
	case VK_PRIOR:
	case VK_NEXT: {
		int step = (int)(ta->sv.inner.h / lineH);
		if (step < 1) step = 1;
		ta_move_vertical(ta, ui->gfx, ui->theme->fInput,
			vk == VK_PRIOR ? -step : step, shift);
		handled = 1;
		break;
	}
	case VK_BACK:
		if (!ta->readonly) ta_backspace(ta);
		handled = 1;
		break;
	case VK_DELETE:
		if (!ta->readonly) ta_delete_forward(ta);
		handled = 1;
		break;
	case VK_RETURN:
		if (!ta->readonly) ta_insert_internal(ta, L"\n", 1);
		handled = 1;
		break;
	case VK_TAB:
	case VK_ESCAPE:
		return 0;
	}

	if (!handled && ctrl) {
		switch (vk) {
		case 'A':
			ta->anchor = 0;
			ta->caret = ta->len;
			ta->ensureCaret = 1;
			ta->hasXPref = 0;
			handled = 1;
			break;
		case 'C':
			ta_clip_copy(ta, ui->hwnd, 0);
			handled = 1;
			break;
		case 'X':
			if (!ta->readonly) ta_clip_copy(ta, ui->hwnd, 1);
			handled = 1;
			break;
		case 'V':
			if (!ta->readonly) ta_clip_paste(ta, ui->hwnd);
			handled = 1;
			break;
		}
	}
	if (handled) {
		ta->blinkOn = 1;
		ta_invalidate(ta, ui);
	}
	return handled;
}

int UiTextArea_Char(UiTextArea *ta, UiContext *ui, wchar_t ch)
{
	if (!ta || !ui) return 0;
	ta_drain_ime(ta);
	if (ui->focusId != ta->id) return 0;
	if (ta->readonly) return 1;
	if (Edit_HasComposition(&ta->imeEdit)) return 1;
	if (ch == L'\r' || ch == L'\n') return 1;
	if (ch == L'\t' || ch < 32) return 1;
	ta_insert_internal(ta, &ch, 1);
	ta->blinkOn = 1;
	ta_invalidate(ta, ui);
	return 1;
}

void UiTextArea_OnIme(UiTextArea *ta, UiContext *ui)
{
	if (!ta || !ui) return;
	ta_drain_ime(ta);
	ta_invalidate(ta, ui);
}

void UiTextArea_Blink(UiTextArea *ta, UiContext *ui)
{
	if (!ta || !ui || ui->focusId != ta->id) return;
	ta->blinkOn = !ta->blinkOn;
	ta_invalidate(ta, ui);
}

/* ── 绘制 ──────────────────────────────────────────────────────────── */

static void ta_draw_line(UiTextArea *ta, Gfx *g, GfxFont *f,
	int line, float y, UiColor col)
{
	int n = ta->lineLen[line];
	if (n <= 0) return;
	ta_to_utf8(ta, ta->lineStart[line], n);
	Gfx_Text(g, f, ta->u8,
		Ui_Rect(ta->textRc.x, y, ta->textRc.w, ta->lineH), col,
		GFX_TEXT_VCENTER);
}

static void ta_draw_selection(UiTextArea *ta, Gfx *g, GfxFont *f,
	int first, int last, UiColor col)
{
	int a = ta_sel_start(ta), b = ta_sel_end(ta);
	int i;
	if (a == b) return;
	for (i = first; i <= last && i < ta->lineCount; i++) {
		int ls = ta->lineStart[i];
		int le = ls + ta->lineLen[i];
		int x0i = a > ls ? a : ls;
		int x1i = b < le ? b : le;
		float y = ta->textRc.y + (float)i * ta->lineH - ta->sv.offset;
		if (x1i > x0i) {
			float x0 = ta->textRc.x + ta_x_of(ta, g, f, i, x0i - ls);
			float x1 = ta->textRc.x + ta_x_of(ta, g, f, i, x1i - ls);
			Gfx_FillRect(g, Ui_Rect(x0, y + 1.0f, x1 - x0,
				ta->lineH - 2.0f), col);
		}
		if (b > le && a <= le && le < ta->len && ta->text[le] == L'\n') {
			float x = ta->textRc.x + ta_x_of(ta, g, f, i, ta->lineLen[i]);
			Gfx_FillRect(g, Ui_Rect(x, y + 1.0f, 4.0f,
				ta->lineH - 2.0f), col);
		}
	}
}

static void ta_draw_composition(UiTextArea *ta, Gfx *g, GfxFont *f,
	int line, float y, UiColor col)
{
	int ls = ta->lineStart[line];
	int colIdx = ta_col_of(ta, line, ta->caret);
	float x = ta->textRc.x + ta_x_of(ta, g, f, line, colIdx);
	float cw;
	if (colIdx > 0) {
		ta_to_utf8(ta, ls, colIdx);
		Gfx_Text(g, f, ta->u8,
			Ui_Rect(ta->textRc.x, y, x - ta->textRc.x, ta->lineH),
			col, GFX_TEXT_VCENTER);
	}
	cw = ta_measure_w(ta, g, f, ta->imeEdit.comp, ta->imeEdit.compLen);
	if (ta->imeEdit.compLen > 0) {
		ta_w_to_utf8(ta, ta->imeEdit.comp, ta->imeEdit.compLen);
		Gfx_Text(g, f, ta->u8,
			Ui_Rect(x, y, cw + 4.0f, ta->lineH), col, GFX_TEXT_VCENTER);
		Gfx_Line(g, x, y + ta->lineH - 4.0f, x + cw,
			y + ta->lineH - 4.0f, col, 1.0f);
	}
	if (colIdx < ta->lineLen[line]) {
		ta_to_utf8(ta, ls + colIdx, ta->lineLen[line] - colIdx);
		Gfx_Text(g, f, ta->u8,
			Ui_Rect(x + cw, y, ta->textRc.w, ta->lineH), col,
			GFX_TEXT_VCENTER);
	}
}

int Ui_TextArea(UiTextArea *ta, UiContext *ui, UiRect rc, int enabled)
{
	UiTheme *t;
	UiSurfaceStyle ss;
	Gfx *g;
	GfxFont *f;
	UiWidgetState st;
	UiRect inner;
	int focused, i, first, last, caretLine, caretCol;
	float wrapW, lineH, contentH, caretX, caretY, compW;
	UiColor textCol, selCol;
	int ch;

	if (!ta || !ui || !ui->theme || !ui->gfx) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	t = ui->theme;
	g = ui->gfx;
	f = t->fInput;
	ta->rc = rc;

	ta_drain_ime(ta);
	if (!enabled && ui->focusId == ta->id) Ui_SetFocusId(ui, UI_ID_NONE);
	focused = enabled && ui->focusId == ta->id;

	if (focused && !ta->wasFocused) {
		Edit_SetText(&ta->imeEdit, L"");
		ta->blinkOn = 1;
		if (ta->ime && !ta->readonly) UiIme_SetFocus(ta->ime, &ta->imeEdit);
	} else if (!focused && ta->wasFocused) {
		if (ta->ime && ta->ime->edit == &ta->imeEdit)
			UiIme_SetFocus(ta->ime, NULL);
		ta->dragging = 0;
	}
	ta->wasFocused = focused;
	if (focused && ta->readonly && ta->ime && ta->ime->edit == &ta->imeEdit)
		UiIme_SetFocus(ta->ime, NULL);

	ZeroMemory(&ss, sizeof(ss));
	ss.bg = t->inputBg;
	ss.border = t->inputBorder;
	ss.hoverBorder = Ui_ColorShift(t->inputBorder, -18);
	ss.focusBorder = t->inputFocus;
	ss.glow = Ui_ColorAlpha(t->inputFocus, 80);
	ss.radius = 6.0f;
	ss.borderW = 1.0f;
	ss.glowW = 4.0f;
	ss.glowMask = UI_GLOW_HOVER | UI_GLOW_FOCUS;
	st = Ui_DrawSurface(ui, ta->id, rc, &ss, enabled);

	lineH = ta->lineH;
	if (lineH <= 0.0f) {
		lineH = Gfx_TextHeight(g, f) + TA_LINE_GAP;
		if (lineH < 14.0f) lineH = 14.0f;
		ta->lineH = lineH;
	}
	ta->sv.cfg.wheelStep = lineH * 3.0f;

	if (!enabled) {
		wrapW = rc.w - TA_PAD_X * 2.0f;
		if (wrapW < 20.0f) wrapW = 20.0f;
		ta_sync_layout(ta, g, f, wrapW);
		ta->sv.offset = 0.0f;
		inner = Ui_Rect(rc.x + TA_PAD_X, rc.y + TA_PAD_Y,
			rc.w - TA_PAD_X * 2.0f, rc.h - TA_PAD_Y * 2.0f);
		ta->textRc = inner;
		Gfx_PushClip(g, inner);
		for (i = 0; i < ta->lineCount; i++) {
			float y = inner.y + (float)i * lineH;
			if (y >= inner.y + inner.h) break;
			ta_draw_line(ta, g, f, i, y, t->btnDisabledText);
		}
		if (ta->len == 0 && ta->placeholder && ta->placeholder[0]) {
			ta_w_to_utf8(ta, ta->placeholder, lstrlenW(ta->placeholder));
			Gfx_Text(g, f, ta->u8,
				Ui_Rect(inner.x, inner.y, inner.w, lineH),
				t->hint, GFX_TEXT_VCENTER);
		}
		Gfx_PopClip(g);
		ch = ta->changed;
		ta->changed = 0;
		return ch;
	}

	wrapW = UiScrollView_ContentWidth(&ta->sv, rc);
	ta_sync_layout(ta, g, f, wrapW);

	contentH = (float)ta->lineCount * lineH;
	if (contentH < lineH) contentH = lineH;
	UiScrollView_Begin(&ta->sv, ui, rc, wrapW, contentH);
	ta->textRc = ta->sv.inner;

	if (ta->ensureCaret) {
		int cl = ta_line_of_arr(ta, ta->caret);
		float cy = (float)cl * lineH;
		float view = ta->sv.inner.h;
		if (view > 0.0f) {
			if (cy < ta->sv.offset)
				UiScrollView_ScrollTo(&ta->sv, ui, cy, 0);
			else if (cy + lineH > ta->sv.offset + view)
				UiScrollView_ScrollTo(&ta->sv, ui, cy + lineH - view, 0);
		}
		ta->ensureCaret = 0;
	}

	/* 鼠标: 点击定位/拖选 (readonly 仍可聚焦/选择/复制).
	 * 放在 Begin 之后: 边缘自动滚动会先更新 offset, 再用新 offset 映射光标. */
	if (ui->down == ta->id) {
		int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		if (ui->focusId != ta->id) Ui_SetFocusId(ui, ta->id);
		focused = 1;
		ta_place_from_point(ta, g, f, ui->mouseX, ui->mouseY, shift);
		ta->dragging = 1;
	} else if (ta->dragging && ui->mouseDown) {
		ta_drag_autoscroll(ta, ui, contentH);
		ta_place_from_point(ta, g, f, ui->mouseX, ui->mouseY, 1);
	}
	/* 拖选在窗口外松开时不要抢走焦点; 只有普通点击空白才清焦点. */
	if (ui->clickedOutside && ui->focusId == ta->id && !ta->dragging)
		Ui_SetFocusId(ui, UI_ID_NONE);
	if (!ui->mouseDown) ta->dragging = 0;

	first = (int)(ta->sv.offset / lineH);
	if (first < 0) first = 0;
	last = (int)((ta->sv.offset + ta->sv.inner.h) / lineH) + 1;
	if (last >= ta->lineCount) last = ta->lineCount - 1;

	textCol = t->text;
	selCol = Ui_ColorAlpha(t->primary.base, 60);
	ta_draw_selection(ta, g, f, first, last, selCol);

	caretLine = ta_line_of_arr(ta, ta->caret);
	caretCol = ta_col_of(ta, caretLine, ta->caret);
	compW = ta_measure_w(ta, g, f, ta->imeEdit.comp, ta->imeEdit.compLen);
	for (i = first; i <= last; i++) {
		float y = ta->textRc.y + (float)i * lineH - ta->sv.offset;
		if (ta->imeEdit.compLen > 0 && i == caretLine)
			ta_draw_composition(ta, g, f, i, y, textCol);
		else
			ta_draw_line(ta, g, f, i, y, textCol);
	}
	if (ta->len == 0 && ta->placeholder && ta->placeholder[0]) {
		ta_w_to_utf8(ta, ta->placeholder, lstrlenW(ta->placeholder));
		Gfx_Text(g, f, ta->u8,
			Ui_Rect(ta->textRc.x, ta->textRc.y, ta->textRc.w, lineH),
			t->hint, GFX_TEXT_VCENTER);
	}

	caretX = ta->textRc.x + ta_x_of(ta, g, f, caretLine, caretCol) + compW;
	caretY = ta->textRc.y + (float)caretLine * lineH - ta->sv.offset;
	if (focused && ta->blinkOn && !ta->readonly) {
		Gfx_Line(g, caretX, caretY + 2.0f, caretX,
			caretY + lineH - 2.0f, t->text, 1.4f);
	}
	ta->imeEdit.hasRect = 1;
	ta->imeEdit.lastRc = rc;
	ta->imeEdit.caretDipX = caretX;
	ta->imeEdit.textTopDip = caretY;
	ta->imeEdit.textHDip = lineH;

	UiScrollView_End(&ta->sv, ui);
	UiScrollView_Tick(&ta->sv, ui, 16.0f);

	ch = ta->changed;
	ta->changed = 0;
	(void)st;
	return ch;
}
