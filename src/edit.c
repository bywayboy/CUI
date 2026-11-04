#include "edit.h"
#include "ui/ui_paint.h"
#include <string.h>

static int sel_start(const UiEdit *e) { return e->anchor < e->caret ? e->anchor : e->caret; }
static int sel_end(const UiEdit *e)   { return e->anchor < e->caret ? e->caret : e->anchor; }
static int has_sel(const UiEdit *e)   { return e->anchor != e->caret; }

static float measure_utf16(Gfx *g, GfxFont *f, const wchar_t *s, int n)
{
	char buf[EDIT_MAX * 3 + 8];
	int m;
	if (n <= 0) return 0.0f;
	m = WideCharToMultiByte(CP_UTF8, 0, s, n, buf, (int)sizeof(buf) - 1, NULL, NULL);
	if (m <= 0) return 0.0f;
	buf[m] = 0;
	return Gfx_TextWidth(g, f, buf);
}

static float measure_disp(const UiEdit *e, Gfx *g, GfxFont *f, int n)
{
	wchar_t stars[EDIT_MAX];
	int i;
	if (n <= 0) return 0.0f;
	if (!e->password) return measure_utf16(g, f, e->text, n);
	if (n > EDIT_MAX) n = EDIT_MAX;
	for (i = 0; i < n; i++) stars[i] = L'*';
	return measure_utf16(g, f, stars, n);
}

/* 组合串宽度: 密码框按掩码算, 与上屏后的显示保持一致 */
static float measure_comp(const UiEdit *e, Gfx *g, GfxFont *f)
{
	wchar_t buf[EDIT_COMP_MAX];
	int i, n = e->compLen;
	if (n <= 0) return 0.0f;
	if (!e->password) return measure_utf16(g, f, e->comp, n);
	for (i = 0; i < n; i++) buf[i] = L'*';
	return measure_utf16(g, f, buf, n);
}

static void text_to_utf8(const wchar_t *s, int n, char *out, int cap)
{
	int m;
	if (cap <= 0) return;
	if (n <= 0) { out[0] = 0; return; }
	m = WideCharToMultiByte(CP_UTF8, 0, s, n, out, cap - 1, NULL, NULL);
	if (m < 0) m = 0;
	out[m] = 0;
}

static void make_display(const UiEdit *e, wchar_t *out)
{
	int i;
	if (!e->password) {
		memcpy(out, e->text, (size_t)(e->len + 1) * sizeof(wchar_t));
		return;
	}
	for (i = 0; i < e->len; i++) out[i] = L'*';
	out[e->len] = 0;
}

static void ensure_caret_visible(UiEdit *e, Gfx *g, GfxFont *f, float innerW)
{
	/* 组合串画在插入点之后, 光标与总宽都要把它算进去, 否则长拼音会顶出可视区 */
	float compW = measure_comp(e, g, f);
	float cx = measure_disp(e, g, f, e->caret) + compW;
	float total = measure_disp(e, g, f, e->len) + compW;
	if (total <= innerW) { e->scrollX = 0.0f; return; }
	if (cx - e->scrollX > innerW - 1.0f) e->scrollX = cx - innerW + 1.0f;
	if (cx - e->scrollX < 0.0f) e->scrollX = cx;
	if (e->scrollX < 0.0f) e->scrollX = 0.0f;
	if (e->scrollX > total - innerW) e->scrollX = total - innerW;
}

static int is_word(wchar_t c)
{
	return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z')
		|| (c >= L'a' && c <= L'z') || c == L'_' || c >= 0x80;
}

static void move_caret(UiEdit *e, int pos, int shift)
{
	if (pos < 0) pos = 0;
	if (pos > e->len) pos = e->len;
	if (!shift) e->anchor = pos;
	e->caret = pos;
}

static int allow_char(const UiEdit *e, wchar_t ch)
{
	const char *p;
	if (ch < 32 || ch == 127) return 0;
	if (!e->allow) return 1;
	if (ch > 127) return 1;
	for (p = e->allow; *p; p++) if ((wchar_t)(unsigned char)*p == ch) return 1;
	return 0;
}

static void insert_text(UiEdit *e, const wchar_t *s, int n)
{
	int a = sel_start(e), b = sel_end(e);
	int i, room;
	if (b > a) {
		memmove(e->text + a, e->text + b, (size_t)(e->len - b + 1) * sizeof(wchar_t));
		e->len -= b - a;
		e->caret = a;
	}
	if (n <= 0) { e->anchor = e->caret; return; }
	room = e->maxLen - e->len;
	if (room <= 0) { e->anchor = e->caret; return; }
	if (n > room) n = room;
	for (i = 0; i < n; i++) {
		if (!allow_char(e, s[i])) continue;
		if (e->len >= e->maxLen) break;
		memmove(e->text + e->caret + 1, e->text + e->caret,
			(size_t)(e->len - e->caret + 1) * sizeof(wchar_t));
		e->text[e->caret++] = s[i];
		e->len++;
	}
	e->anchor = e->caret;
}

static void delete_sel_or(UiEdit *e, int back)
{
	int a = sel_start(e), b = sel_end(e);
	if (a != b) {
		memmove(e->text + a, e->text + b, (size_t)(e->len - b + 1) * sizeof(wchar_t));
		e->len -= b - a;
		e->caret = e->anchor = a;
		return;
	}
	if (back) {
		if (e->caret <= 0) return;
		memmove(e->text + e->caret - 1, e->text + e->caret,
			(size_t)(e->len - e->caret + 1) * sizeof(wchar_t));
		e->caret--; e->len--;
	} else {
		if (e->caret >= e->len) return;
		memmove(e->text + e->caret, e->text + e->caret + 1,
			(size_t)(e->len - e->caret) * sizeof(wchar_t));
		e->len--;
	}
	e->anchor = e->caret;
}

static int clip_copy(UiEdit *e, HWND hwnd)
{
	int a = sel_start(e), b = sel_end(e);
	HGLOBAL h;
	wchar_t *p;
	if (a == b) return 0;
	if (!OpenClipboard(hwnd)) return 0;
	EmptyClipboard();
	h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(b - a + 1) * sizeof(wchar_t));
	if (h) {
		p = (wchar_t *)GlobalLock(h);
		if (p) {
			memcpy(p, e->text + a, (size_t)(b - a) * sizeof(wchar_t));
			p[b - a] = 0;
		}
		GlobalUnlock(h);
		SetClipboardData(CF_UNICODETEXT, h);
	}
	CloseClipboard();
	return 1;
}

static void clip_paste(UiEdit *e, HWND hwnd)
{
	HANDLE h;
	const wchar_t *p;
	wchar_t buf[EDIT_MAX * 2];
	int n = 0;
	if (!OpenClipboard(hwnd)) return;
	h = GetClipboardData(CF_UNICODETEXT);
	if (h) {
		p = (const wchar_t *)GlobalLock(h);
		if (p) {
			while (*p && n < (int)(sizeof(buf) / sizeof(buf[0])) - 1) {
				wchar_t c = *p++;
				if (c == L'\r' || c == L'\n' || c == L'\t') continue;
				if (c < 32) continue;
				buf[n++] = c;
			}
		}
		GlobalUnlock(h);
	}
	CloseClipboard();
	buf[n] = 0;
	insert_text(e, buf, n);
}

void Edit_Init(UiEdit *e, int maxLen, int password, const char *allow)
{
	memset(e, 0, sizeof(*e));
	if (maxLen <= 0 || maxLen >= EDIT_MAX) maxLen = EDIT_MAX - 1;
	e->maxLen = maxLen;
	e->password = password;
	e->allow = allow;
}

void Edit_SetText(UiEdit *e, const wchar_t *s)
{
	int n;
	if (!s) s = L"";
	n = lstrlenW(s);
	if (n > e->maxLen) n = e->maxLen;
	memcpy(e->text, s, (size_t)n * sizeof(wchar_t));
	e->text[n] = 0;
	e->len = n;
	e->caret = e->anchor = n;
	e->scrollX = 0.0f;
}

void Edit_SetTextUtf8(UiEdit *e, const char *s)
{
	wchar_t buf[EDIT_MAX];
	int n = 0;
	if (s && *s)
		n = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, EDIT_MAX);
	if (n > 0) buf[EDIT_MAX - 1] = 0;
	Edit_SetText(e, n > 0 ? buf : L"");
}

void Edit_TextUtf8(const UiEdit *e, char *out, int cap)
{
	text_to_utf8(e->text, e->len, out, cap);
}

int Edit_Empty(const UiEdit *e) { return e->len == 0; }

void Edit_TextW(const UiEdit *e, wchar_t *out, int cap)
{
	int n = e->len;
	if (cap <= 0) return;
	if (n > cap - 1) n = cap - 1;
	memcpy(out, e->text, (size_t)n * sizeof(wchar_t));
	out[n] = 0;
}

void Edit_GetImePoint(const UiEdit *e, float scale, POINT *pt)
{
	if (!e || !e->hasRect || !pt) return;
	pt->x = (LONG)(e->caretDipX * scale);
	pt->y = (LONG)((e->textTopDip + e->textHDip) * scale);
}

void Edit_Draw(UiContext *ui, UiId id, UiRect rc, UiEdit *e, const wchar_t *hint)
{
	const UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	GfxFont *f = t->fInput;
	UiSurfaceStyle ss;
	UiRect inner;
	float innerW, th, textH, tx, ty, cy;
	wchar_t disp[EDIT_MAX];
	char u8[EDIT_MAX * 3 + 8];
	float selA, selB, total, compW;
	int a, b;

	if (!e) return;
	inner = Ui_Rect(rc.x + 10.0f, rc.y + 1.0f, rc.w - 20.0f, rc.h - 2.0f);
	innerW = inner.w;
	textH = Gfx_TextHeight(g, f);
	th = textH < rc.h ? textH : rc.h;

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
	Ui_DrawSurface(ui, id, rc, &ss, 1);

	/* 脏区外只做注册/状态更新, 跳过昂贵的逐字测量与排版。
	 * 文本/光标/组合串变化时应用会失效字段矩形, 不会被跳过。 */
	if (!Gfx_IsVisible(g, rc)) return;

	make_display(e, disp);
	compW = measure_comp(e, g, f);
	ensure_caret_visible(e, g, f, innerW);

	a = sel_start(e); b = sel_end(e);
	selA = measure_disp(e, g, f, a);
	selB = measure_disp(e, g, f, b);

	Gfx_PushClip(g, inner);
	if (a != b) {
		float x0 = inner.x + selA - e->scrollX;
		float x1 = inner.x + selB - e->scrollX;
		if (x0 < inner.x) x0 = inner.x;
		if (x1 > inner.x + inner.w) x1 = inner.x + inner.w;
		if (x1 > x0)
			Gfx_FillRect(g, Ui_Rect(x0, inner.y + 3.0f, x1 - x0, inner.h - 6.0f), 0x553D8BFD);
	}

	ty = rc.y + (rc.h - th) * 0.5f;
	tx = inner.x - e->scrollX;
	if (e->len > 0 || e->compLen > 0) {
		float wPre = measure_disp(e, g, f, e->caret);
		if (e->compLen > 0) {
			/* 组合串画在插入点, 原文字向右让位; 下划线表示"未上屏" */
			wchar_t cbuf[EDIT_COMP_MAX];
			int i;
			if (e->caret > 0) {
				text_to_utf8(disp, e->caret, u8, (int)sizeof(u8));
				Gfx_Text(g, f, u8, Ui_Rect(tx, ty, wPre + 4.0f, th), t->text, 0);
			}
			for (i = 0; i < e->compLen; i++) cbuf[i] = e->password ? L'*' : e->comp[i];
			text_to_utf8(cbuf, e->compLen, u8, (int)sizeof(u8));
			Gfx_Text(g, f, u8, Ui_Rect(tx + wPre, ty, compW + 4.0f, th), t->text, 0);
			Gfx_Line(g, tx + wPre, ty + th - 1.0f, tx + wPre + compW, ty + th - 1.0f, t->text, 1.0f);
			if (e->caret < e->len) {
				text_to_utf8(disp + e->caret, e->len - e->caret, u8, (int)sizeof(u8));
				Gfx_Text(g, f, u8, Ui_Rect(tx + wPre + compW, ty,
					measure_disp(e, g, f, e->len) - wPre + 4.0f, th), t->text, 0);
			}
		} else {
			total = measure_disp(e, g, f, e->len);
			text_to_utf8(disp, e->len, u8, (int)sizeof(u8));
			Gfx_Text(g, f, u8, Ui_Rect(tx, ty, total + 4.0f, th), t->text, 0);
		}
	} else if (hint && hint[0]) {
		text_to_utf8(hint, lstrlenW(hint), u8, (int)sizeof(u8));
		Gfx_Text(g, f, u8, Ui_Rect(inner.x, ty, innerW, th), t->hint, 0);
	}

	cy = inner.x + measure_disp(e, g, f, e->caret) - e->scrollX;
	if (e->compLen > 0) cy += compW;   /* 组合期间光标落在组合串之后 */
	if (e->focused && e->caretOn && cy >= inner.x - 1.0f && cy <= inner.x + inner.w + 1.0f)
		Gfx_Line(g, cy, ty, cy, ty + th, t->text, 1.4f);
	Gfx_PopClip(g);

	e->hasRect = 1;
	e->lastRc = rc;
	e->caretDipX = cy;
	e->textTopDip = ty;
	e->textHDip = th;
}

void Edit_PlaceCaret(UiEdit *e, Gfx *g, GfxFont *f, UiRect rc, float mouseX)
{
	float innerX = rc.x + 10.0f;
	float innerW = rc.w - 20.0f;
	float target = mouseX - innerX + e->scrollX;
	int i, best = 0;
	float bestD = 1e9f;
	for (i = 0; i <= e->len; i++) {
		float w = measure_disp(e, g, f, i);
		float d = w > target ? (w - target) : (target - w);
		if (d < bestD) { bestD = d; best = i; }
		if (w >= target) break;
	}
	if (best > e->len) best = e->len;
	e->caret = e->anchor = best;
	e->dragging = 1;
	ensure_caret_visible(e, g, f, innerW);
}

void Edit_DragTo(UiEdit *e, Gfx *g, GfxFont *f, UiRect rc, float mouseX)
{
	float innerX = rc.x + 10.0f;
	float innerW = rc.w - 20.0f;
	float target = mouseX - innerX + e->scrollX;
	int i, best = 0;
	float bestD = 1e9f;
	if (!e->dragging) return;
	for (i = 0; i <= e->len; i++) {
		float w = measure_disp(e, g, f, i);
		float d = w > target ? (w - target) : (target - w);
		if (d < bestD) { bestD = d; best = i; }
		if (w >= target) break;
	}
	e->caret = best;
	ensure_caret_visible(e, g, f, innerW);
}

void Edit_StopDrag(UiEdit *e) { if (e) e->dragging = 0; }

int Edit_Key(UiEdit *e, HWND hwnd, UINT vk)
{
	int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	switch (vk) {
	case VK_LEFT:
		if (ctrl) {
			int p = e->caret;
			while (p > 0 && !is_word(e->text[p - 1])) p--;
			while (p > 0 && is_word(e->text[p - 1])) p--;
			move_caret(e, p, shift);
		} else move_caret(e, e->caret - 1, shift);
		return 1;
	case VK_RIGHT:
		if (ctrl) {
			int p = e->caret;
			while (p < e->len && !is_word(e->text[p])) p++;
			while (p < e->len && is_word(e->text[p])) p++;
			move_caret(e, p, shift);
		} else move_caret(e, e->caret + 1, shift);
		return 1;
	case VK_HOME: move_caret(e, 0, shift); return 1;
	case VK_END:  move_caret(e, e->len, shift); return 1;
	case VK_BACK: delete_sel_or(e, 1); return 1;
	case VK_DELETE: delete_sel_or(e, 0); return 1;
	case 'A':
		if (!ctrl) return 0;
		e->anchor = 0; e->caret = e->len; return 1;
	case 'C':
		if (!ctrl) return 0;
		clip_copy(e, hwnd); return 1;
	case 'X':
		if (!ctrl) return 0;
		if (clip_copy(e, hwnd)) delete_sel_or(e, 0);
		return 1;
	case 'V':
		if (!ctrl) return 0;
		clip_paste(e, hwnd); return 1;
	default:
		return 0;
	}
}

int Edit_Char(UiEdit *e, wchar_t ch)
{
	wchar_t one[2];
	if (!allow_char(e, ch)) return 1;
	if (e->len >= e->maxLen && !has_sel(e)) return 1;
	one[0] = ch; one[1] = 0;
	insert_text(e, one, 1);
	return 1;
}

int Edit_InsertString(UiEdit *e, const wchar_t *s, int n)
{
	int before;
	if (!e) return 0;
	if (!s || n <= 0) { e->anchor = e->caret; return 0; }
	before = e->len;
	insert_text(e, s, n);
	return e->len - before;
}

void Edit_SetImeEnabled(UiEdit *e, int on)
{
	if (!e) return;
	e->noIme = on ? 0 : 1;
	if (e->noIme) e->compLen = 0;
}

int Edit_ImeEnabled(const UiEdit *e) { return e && !e->noIme; }

void Edit_SetComposition(UiEdit *e, const wchar_t *s, int n)
{
	int i;
	if (!e) return;
	if (!s || n <= 0) { e->compLen = 0; return; }
	if (n > EDIT_COMP_MAX - 1) n = EDIT_COMP_MAX - 1;
	for (i = 0; i < n; i++) e->comp[i] = s[i];
	e->comp[i] = 0;
	e->compLen = n;
}

void Edit_ClearComposition(UiEdit *e)
{
	if (e) e->compLen = 0;
}

int Edit_HasComposition(const UiEdit *e) { return e && e->compLen > 0; }
