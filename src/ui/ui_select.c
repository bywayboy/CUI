#include "ui/ui_select.h"
#include "ui/ui_frame.h"
#include "ui/ui_icon.h"
#include <string.h>

#define SEL_PAD_X 12.0f
#define SEL_PAD_Y 4.0f
#define SEL_MIN_W 140.0f
#define SEL_ITEM_PAD 10.0f
#define SEL_SEARCH_H 38.0f
#define SEL_SEARCH_INPUT_H 30.0f

static unsigned sel_bit(int i)
{
	return (i >= 0 && i < 32) ? (1u << i) : 0u;
}

static UiId sel_child(UiId id, const char *scope, int i)
{
	return Ui_IdIndex(scope, (int)(uint32_t)(id >> 32),
		(int)(uint32_t)id, i);
}

static UiId sel_search_id(const UiSelect *s)
{
	return sel_child(s->id, "ui.select.search", 0);
}

static int sel_item_enabled(const UiSelect *s, int i)
{
	if (!s || i < 0 || i >= s->count) return 0;
	return (s->disabledMask & sel_bit(i)) == 0;
}

static float sel_row_h(const UiSelect *s)
{
	return s->rowH > 0.0f ? s->rowH : 32.0f;
}

static float sel_content_h(const UiSelect *s, float rowH)
{
	return (float)s->visCount * rowH;
}

/* ── 过滤 ──────────────────────────────────────────────────────────── */

static int sel_has_filter(const UiSelect *s)
{
	return s && s->filterable && s->searchActive && s->filter[0];
}

static int str_lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int str_contains_ci(const char *hay, const char *needle)
{
	int i, j;
	if (!needle || !needle[0]) return 1;
	if (!hay) return 0;
	for (i = 0; hay[i]; i++) {
		for (j = 0; needle[j]; j++) {
			if (!hay[i + j]) break;
			if (str_lower((unsigned char)hay[i + j]) !=
				str_lower((unsigned char)needle[j])) break;
		}
		if (!needle[j]) return 1;
	}
	return 0;
}

static int sel_match(const UiSelect *s, int i)
{
	if (!s || !s->items || i < 0 || i >= s->count) return 0;
	if (!sel_has_filter(s)) return 1;
	return str_contains_ci(s->items[i], s->filter);
}

static int sel_count_matches(const UiSelect *s)
{
	int i, n = 0;
	for (i = 0; i < s->count; i++)
		if (sel_match(s, i)) n++;
	return n;
}

static int sel_match_at(const UiSelect *s, int k)
{
	int i, n = 0;
	if (k < 0) return -1;
	for (i = 0; i < s->count; i++) {
		if (!sel_match(s, i)) continue;
		if (n == k) return i;
		n++;
	}
	return -1;
}

static int sel_vis_of(const UiSelect *s, int orig)
{
	int i, n = 0;
	if (orig < 0 || orig >= s->count) return -1;
	for (i = 0; i < s->count; i++) {
		if (!sel_match(s, i)) continue;
		if (i == orig) return n;
		n++;
	}
	return -1;
}

static int sel_find(const UiSelect *s, int from, int dir)
{
	int i;
	if (!s || s->count <= 0) return -1;
	for (i = 0; i < s->count; i++) {
		int idx = (from + dir * i) % s->count;
		if (idx < 0) idx += s->count;
		if (sel_item_enabled(s, idx) && sel_match(s, idx)) return idx;
	}
	return -1;
}

static int sel_first(const UiSelect *s)
{
	return sel_find(s, 0, 1);
}

static int sel_last(const UiSelect *s)
{
	return sel_find(s, s->count - 1, -1);
}

/* ── 搜索框焦点/失效 ───────────────────────────────────────────────── */

static void sel_invalidate(UiSelect *s, UiContext *ui)
{
	RECT r;
	UiRect rc;
	float sc;
	if (!s || !ui || !ui->hwnd) return;
	rc = s->listRc;
	if (rc.w <= 0.0f || rc.h <= 0.0f) {
		InvalidateRect(ui->hwnd, NULL, FALSE);
		return;
	}
	sc = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((rc.x - 4.0f) * sc);
	r.top = (LONG)((rc.y - 4.0f) * sc);
	r.right = (LONG)((rc.x + rc.w + 4.0f) * sc) + 1;
	r.bottom = (LONG)((rc.y + rc.h + 4.0f) * sc) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

static void sel_refilter(UiSelect *s, UiContext *ui)
{
	char buf[sizeof(s->filter)];
	Edit_TextUtf8(&s->edit, buf, (int)sizeof(buf));
	if (lstrcmpA(buf, s->filter) != 0) {
		lstrcpynA(s->filter, buf, (int)sizeof(s->filter));
		s->visCount = sel_count_matches(s);
		s->sv.offset = 0.0f;
		s->sv.target = 0.0f;
		s->sv.velocity = 0.0f;
		s->lastTickNow = 0.0;
		if (s->focus < 0 || !sel_item_enabled(s, s->focus) ||
			!sel_match(s, s->focus))
			s->focus = sel_first(s);
		s->ensureFocus = 1;
		sel_invalidate(s, ui);
	}
}

static void sel_search_begin(UiSelect *s, UiContext *ui)
{
	if (!s || !ui || !s->filterable || s->searchActive) return;
	s->searchActive = 1;
	s->blinkOn = 1;
	Edit_SetText(&s->edit, L"");
	s->filter[0] = 0;
	s->visCount = s->count;
	s->focus = (s->selected >= 0 && sel_item_enabled(s, s->selected))
		? s->selected : sel_first(s);
	s->ensureFocus = 1;
	Ui_SetFocusId(ui, sel_search_id(s));
	if (s->ime) UiIme_SetFocus(s->ime, &s->edit);
}

static void sel_search_end(UiSelect *s, UiContext *ui)
{
	UiId sid;
	if (!s || !ui || !s->searchActive) return;
	sid = sel_search_id(s);
	s->searchActive = 0;
	s->blinkOn = 0;
	if (s->ime && s->ime->edit == &s->edit)
		UiIme_SetFocus(s->ime, NULL);
	if (ui->focusId == sid) Ui_SetFocusId(ui, UI_ID_NONE);
	Edit_SetText(&s->edit, L"");
	s->filter[0] = 0;
	s->visCount = s->count;
}

/* ── 多选/摘要 ─────────────────────────────────────────────────────── */

static void str_append(char *out, int cap, const char *src)
{
	int n = (int)strlen(out);
	while (src && *src && n < cap - 1) out[n++] = *src++;
	out[n] = 0;
}

static void sel_summary(const UiSelect *s, char *out, int cap)
{
	int i, n = 0, shown = 0;
	if (cap <= 0) return;
	out[0] = 0;
	if (!s || !s->items || s->count <= 0) return;
	if (!s->multiple) {
		if (s->selected >= 0 && s->selected < s->count)
			str_append(out, cap, s->items[s->selected]);
		return;
	}
	for (i = 0; i < s->count; i++) {
		if ((s->mask & sel_bit(i)) == 0 || !s->items[i]) continue;
		n++;
		if (shown < 2) {
			if (shown) str_append(out, cap, ", ");
			str_append(out, cap, s->items[i]);
			shown++;
		}
	}
	if (n > 2) {
		char tmp[16];
		wsprintfA(tmp, " +%d", n - 2);
		str_append(out, cap, tmp);
	}
}

static int sel_toggle(UiSelect *s, UiContext *ui, int idx)
{
	if (!s || !ui || !sel_item_enabled(s, idx) || idx >= 32) return 0;
	s->mask ^= sel_bit(idx);
	s->selected = idx;
	s->focus = idx;
	s->changed = 1;
	ui->navId = s->id;
	return 1;
}

static void sel_commit(UiSelect *s, UiPopupManager *pm, UiContext *ui, int idx,
	int focusOnClose)
{
	if (!s || !ui || !sel_item_enabled(s, idx)) return;
	s->selected = idx;
	s->focus = idx;
	s->changed = 1;
	s->committed = 1;
	ui->navId = s->id;
	if (pm) {
		pm->focusOnClose = focusOnClose != 0;
		UiPopupManager_Close(pm, ui);
	}
}

/* 必须在 UiScrollView_Begin 之后调用 (inner 已确定). */
static void sel_scroll_focus(UiSelect *s, UiContext *ui)
{
	float top, bottom, view;
	int vis;
	if (!s || !ui || s->focus < 0) return;
	vis = sel_vis_of(s, s->focus);
	if (vis < 0) return;
	view = s->sv.inner.h;
	top = (float)vis * sel_row_h(s);
	bottom = top + sel_row_h(s);
	if (top < s->sv.offset)
		UiScrollView_ScrollTo(&s->sv, ui, top, 0);
	else if (bottom > s->sv.offset + view)
		UiScrollView_ScrollTo(&s->sv, ui, bottom - view, 0);
}

static void sel_open(UiSelect *s, UiPopupManager *pm, UiContext *ui,
	int focusOnClose)
{
	if (!s || !pm || !ui || s->count <= 0) return;
	if (UiPopupManager_IsOpen(pm, s->id)) return;
	s->focus = (s->selected >= 0 && sel_item_enabled(s, s->selected))
		? s->selected : sel_first(s);
	if (s->focus < 0) return;
	s->sv.offset = 0.0f;
	s->sv.target = 0.0f;
	s->sv.velocity = 0.0f;
	s->lastTickNow = 0.0;
	s->ensureFocus = 1;
	UiPopupManager_Open(pm, ui, s->id, s->id, focusOnClose);
}

/* ── 生命周期 ──────────────────────────────────────────────────────── */

void UiSelect_Init(UiSelect *s, UiId id)
{
	UiScrollConfig cfg;
	UiScrollbarStyle bar;
	UiScrollPadding pad;
	if (!s) return;
	ZeroMemory(s, sizeof(*s));
	s->id = id;
	s->selected = -1;
	s->focus = -1;
	s->rowH = 32.0f;
	s->maxListH = 280.0f;
	Edit_Init(&s->edit, 63, 0, NULL);

	cfg = UiScrollConfig_Desktop();
	cfg.wheelStep = s->rowH * 3.0f;   /* 3 行/格 */
	cfg.wheelTauMs = 100.0f;          /* 平滑滚轮 (与 Markdown/TextArea 一致) */
	cfg.bounce = 0;
	cfg.autoHideBar = 0;
	bar = UiScrollbarStyle_Default();
	bar.width = 6.0f;
	bar.minThumb = 24.0f;
	bar.margin = 2.0f;
	bar.radius = 3.0f;
	bar.track = 0x00FFFFFFu;          /* 非 0 但全透明: 只显示 thumb */
	bar.alwaysVisible = 1;
	bar.reserveSpace = 1;             /* 文本不压到滚动条 */
	UiScrollView_Init(&s->sv, sel_child(id, "ui.select.sv", 0), &cfg, &bar);
	pad.left = pad.top = pad.right = pad.bottom = SEL_PAD_Y;
	UiScrollView_SetPadding(&s->sv, pad);
	s->sv.barOpacity = 1.0f;
	s->sv.barTarget = 1.0f;
}

void UiSelect_Shutdown(UiSelect *s)
{
	if (!s) return;
	if (s->ime && s->ime->edit == &s->edit)
		UiIme_SetFocus(s->ime, NULL);
	Edit_Shutdown(&s->edit);   /* 释放动态搜索缓冲 */
}

void UiSelect_SetItems(UiSelect *s, const char *const *items, int count)
{
	if (!s) return;
	s->items = items;
	s->count = count > 0 ? count : 0;
	if (s->count < 32) {
		s->disabledMask &= (1u << s->count) - 1u;
		s->mask &= (1u << s->count) - 1u;
	}
	if (s->selected >= s->count) s->selected = s->count - 1;
	if (s->focus >= s->count) s->focus = s->count - 1;
	s->visCount = s->count;
	s->sv.offset = 0.0f;
	s->sv.target = 0.0f;
	s->sv.velocity = 0.0f;
	s->lastTickNow = 0.0;
}

void UiSelect_SetSelected(UiSelect *s, int index)
{
	if (!s) return;
	if (index < -1) index = -1;
	if (index >= s->count) index = s->count - 1;
	s->selected = index;
	s->focus = index;
	if (s->multiple)
		s->mask = index >= 0 ? sel_bit(index) : 0u;
	s->ensureFocus = 1;
}

void UiSelect_SetDisabledMask(UiSelect *s, unsigned mask)
{
	if (!s) return;
	if (s->count < 32) mask &= (1u << s->count) - 1u;
	s->disabledMask = mask;
}

void UiSelect_SetMultiple(UiSelect *s, int on)
{
	int i;
	if (!s) return;
	s->multiple = on != 0;
	if (s->multiple) {
		s->mask = s->selected >= 0 ? sel_bit(s->selected) : 0u;
	} else {
		s->mask &= s->count < 32 ? (1u << s->count) - 1u : 0xFFFFFFFFu;
		for (i = 0; i < s->count && i < 32; i++)
			if (s->mask & sel_bit(i)) break;
		s->selected = i < s->count ? i : -1;
		s->focus = s->selected;
	}
}

void UiSelect_SetMask(UiSelect *s, unsigned mask)
{
	int i;
	if (!s) return;
	if (s->count < 32) mask &= (1u << s->count) - 1u;
	s->mask = mask;
	if (s->selected < 0 || s->selected >= 32 ||
		(mask & sel_bit(s->selected)) == 0) {
		for (i = 0; i < s->count && i < 32; i++)
			if (mask & sel_bit(i)) break;
		s->selected = i < s->count ? i : -1;
		if (s->focus < 0) s->focus = s->selected;
	}
}

unsigned UiSelect_GetMask(const UiSelect *s)
{
	return s ? s->mask : 0u;
}

void UiSelect_SetFilterable(UiSelect *s, int on)
{
	if (s) s->filterable = on != 0;
}

void UiSelect_SetPlaceholder(UiSelect *s, const char *text)
{
	if (!s) return;
	if (!text) text = "";
	lstrcpynA(s->placeholder, text, (int)sizeof(s->placeholder));
}

void UiSelect_SetSearchHint(UiSelect *s, const wchar_t *hint)
{
	if (s) s->searchHint = hint;
}

void UiSelect_AttachIme(UiSelect *s, UiIme *ime)
{
	if (s) s->ime = ime;
}

int UiSelect_SearchActive(const UiSelect *s)
{
	return s && s->searchActive;
}

int UiSelect_Char(UiSelect *s, UiContext *ui, wchar_t ch)
{
	if (!s || !ui || !s->searchActive) return 0;
	if (Edit_Char(&s->edit, ch)) {
		s->blinkOn = 1;
		sel_refilter(s, ui);
		sel_invalidate(s, ui);
		return 1;
	}
	return 0;
}

void UiSelect_OnIme(UiSelect *s, UiContext *ui)
{
	if (!s || !ui || !s->searchActive) return;
	sel_refilter(s, ui);
	sel_invalidate(s, ui);   /* 组合串变化不改变正文, 但必须重画 */
}

void UiSelect_Blink(UiSelect *s, UiContext *ui)
{
	if (!s || !ui || !s->searchActive) return;
	s->blinkOn = !s->blinkOn;
	sel_invalidate(s, ui);
}

/* ── 触发器 ────────────────────────────────────────────────────────── */

/* 矢量 chevron, t=0 向下, t=1 旋转 180° 向上; 两段圆头线. */
static void sel_draw_arrow(UiContext *ui, UiRect cr, float t, UiColor col)
{
	Gfx *g;
	float cx, cy;
	if (!ui || !ui->gfx) return;
	g = ui->gfx;
	cx = cr.x + cr.w * 0.5f;
	cy = cr.y + cr.h * 0.5f;
	Gfx_PushRotate(g, cx, cy, 180.0f * t);
	Gfx_Line(g, cx - 5.0f, cy - 2.4f, cx, cy + 2.8f, col, 1.6f);
	Gfx_Line(g, cx, cy + 2.8f, cx + 5.0f, cy - 2.4f, col, 1.6f);
	Gfx_PopTransform(g);
}

static void sel_invalidate_trigger(UiContext *ui, UiRect rc)
{
	RECT r;
	float sc;
	if (!ui || !ui->hwnd) return;
	sc = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((rc.x - 3.0f) * sc);
	r.top = (LONG)((rc.y - 3.0f) * sc);
	r.right = (LONG)((rc.x + rc.w + 3.0f) * sc) + 1;
	r.bottom = (LONG)((rc.y + rc.h + 3.0f) * sc) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

int Ui_Select(UiSelect *s, UiPopupManager *pm, UiContext *ui, UiRect rc,
	const char *placeholder, int enabled)
{
	UiTheme *t;
	UiWidgetState st;
	UiColor bg, border, col, arrowCol;
	UiRect tr, ar;
	float arrow;
	int open, focus, target;
	const char *ph;
	char summary[128];

	if (!s || !ui || !ui->theme || !ui->gfx) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	t = ui->theme;
	s->changed = 0;
	s->committed = 0;
	s->anchor = rc;
	open = pm && UiPopupManager_IsOpen(pm, s->id);

	/* 关闭后清理搜索焦点/IME; 多选在关闭时提交一次. */
	if (!open && s->wasOpen) {
		if (s->multiple) s->committed = 1;
		sel_search_end(s, ui);
	}
	s->wasOpen = open;

	st = Ui_State(ui, s->id, rc, enabled);
	if (enabled && ui->clicked == s->id) {
		ui->navId = s->id;
		if (open && pm) UiPopupManager_Close(pm, ui);
		else sel_open(s, pm, ui, ui->navFocusVisible ? 1 : 0);
		open = pm && UiPopupManager_IsOpen(pm, s->id);
		s->wasOpen = open;
	}

	focus = ui->navFocusVisible && ui->navId == s->id;
	if (!enabled) {
		bg = t->neutral.soft;
		border = t->neutral.border;
	} else {
		bg = t->neutral.base;
		border = (open || focus) ? t->primary.base
			: (st.hover > 0.004f
				? Ui_ColorMix(t->neutral.border, t->primary.base,
					st.hover * 0.6f)
				: t->neutral.border);
	}
	Gfx_FillRoundRect(ui->gfx, rc, 6.0f, bg);
	Gfx_StrokeRoundRect(ui->gfx, rc, 6.0f, border, 1.0f);

	tr = Ui_Rect(rc.x + SEL_PAD_X, rc.y,
		rc.w - SEL_PAD_X * 2.0f - 22.0f, rc.h);
	sel_summary(s, summary, (int)sizeof(summary));
	/* 每帧参数优先; 未传则用 SetPlaceholder 的持久文本. */
	ph = (placeholder && placeholder[0]) ? placeholder : s->placeholder;
	col = !enabled ? t->btnDisabledText : (summary[0] ? t->text : t->text4);
	Gfx_Text(ui->gfx, t->fBody,
		summary[0] ? summary : (ph ? ph : ""),
		tr, col, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	ar = Ui_Rect(rc.x + rc.w - 24.0f, rc.y + (rc.h - 18.0f) * 0.5f,
		18.0f, 18.0f);
	/* 180ms 明确补间; 不依赖应用持续的 animating 回调. 动画期间自行
	 * 失效触发器, 由下一次 WM_PAINT 继续推进. */
	target = open ? 1 : 0;
	if (s->arrowTarget != target) {
		s->arrowTarget = target;
		s->arrowFrom = s->arrowAngle;
		s->arrowT0 = (double)GetTickCount64();
		if (ui->animReduced) {
			s->arrowAngle = (float)target;
			s->arrowAnim = 0;
		} else {
			s->arrowAnim = 1;
		}
	}
	if (s->arrowAnim) {
		double now = (double)GetTickCount64();
		float p = (float)((now - s->arrowT0) / 180.0);
		if (p >= 1.0f) {
			s->arrowAngle = (float)target;
			s->arrowAnim = 0;
			sel_invalidate_trigger(ui, rc);
		} else {
			float q = 1.0f - p;
			float e = 1.0f - q * q * q;   /* ease-out */
			s->arrowAngle = s->arrowFrom +
				((float)target - s->arrowFrom) * e;
			sel_invalidate_trigger(ui, rc);
		}
	}
	arrow = s->arrowAngle;
	arrowCol = !enabled ? t->btnDisabledText
		: Ui_ColorMix(t->text3, t->primary.base, arrow);
	sel_draw_arrow(ui, ar, arrow, arrowCol);
	return s->changed;
}

/* ── 弹层 ──────────────────────────────────────────────────────────── */

void UiSelect_DrawPopup(UiSelect *s, UiPopupManager *pm, UiContext *ui)
{
	UiTheme *t;
	UiRect panel, listView, searchRc, inner, row, trc;
	float rowH, viewH, contentH, w, h, tw, maxTw;
	int i, k, first, last;
	float searchH;
	UiId panelId;
	UiWidgetState st;
	UiColor col;
	UiIcon check;

	if (!s || !pm || !ui || !ui->theme || !ui->gfx) return;
	if (!UiPopupManager_IsOpen(pm, s->id)) return;
	if (s->count <= 0 || !s->items) {
		UiPopupManager_Close(pm, ui);
		return;
	}
	t = ui->theme;
	rowH = sel_row_h(s);
	s->sv.cfg.wheelStep = rowH * 3.0f;
	if (s->filterable && !s->searchActive) sel_search_begin(s, ui);
	if (s->searchActive) Ui_SetFocusId(ui, sel_search_id(s));
	s->visCount = sel_count_matches(s);

	maxTw = 0.0f;
	for (i = 0; i < s->count; i++) {
		tw = Gfx_TextWidth(ui->gfx, t->fBody, s->items[i] ? s->items[i] : "");
		if (tw > maxTw) maxTw = tw;
	}
	w = s->listW > 0.0f ? s->listW : s->anchor.w;
	/* 文本 + 勾选槽 + 滚动条预留 + 左右内边距 */
	tw = maxTw + 20.0f + 8.0f + SEL_PAD_X * 2.0f;
	if (tw > w) w = tw;
	if (w < SEL_MIN_W) w = SEL_MIN_W;
	if (ui->width > 0.0f && w > ui->width - 16.0f) w = ui->width - 16.0f;

	searchH = s->filterable ? SEL_SEARCH_H : 0.0f;
	contentH = sel_content_h(s, rowH);
	viewH = contentH;
	if (viewH > s->maxListH) viewH = s->maxListH;
	if (viewH < rowH) viewH = rowH;
	h = viewH + SEL_PAD_Y * 2.0f + searchH;

	panel = UiPopup_Place(ui, s->anchor, w, h, UI_POPUP_BELOW,
		UI_POPUP_ALIGN_START, 6.0f);
	s->listRc = panel;

	UiPopupManager_BeginDraw(pm, ui);
	Ui_PushLayer(ui, UI_Z_OVERLAY);
	panelId = sel_child(s->id, "ui.select.panel", 0);
	Ui_RegisterWidget(ui, panelId, panel);
	Gfx_FillRoundRect(ui->gfx, panel, 8.0f, t->toastBg);
	Gfx_StrokeRoundRect(ui->gfx, panel, 8.0f, t->toastBorder, 1.0f);

	listView = panel;
	if (s->filterable) {
		searchRc = Ui_Rect(panel.x + 6.0f, panel.y + 4.0f,
			panel.w - 12.0f, SEL_SEARCH_INPUT_H);
		s->edit.focused = 1;
		s->edit.caretOn = s->blinkOn;
		Edit_Draw(ui, sel_search_id(s), searchRc, &s->edit,
			s->searchHint ? s->searchHint : L"搜索…");
		if (ui->clicked == sel_search_id(s)) {
			Edit_PlaceCaret(&s->edit, ui->gfx, t->fInput, searchRc,
				ui->mouseX);
			s->blinkOn = 1;
			ui->navId = s->id;
		}
		listView = Ui_Rect(panel.x,
			panel.y + searchH, panel.w, panel.h - searchH);
	}

	/* 列表滚动区复用 UiScrollView: 滚轮/thumb 拖动/轨道翻页/预留槽,
	 * 行仍由 Select 自己按可见区间绘制. */
	UiScrollView_Begin(&s->sv, ui, listView, 0.0f, contentH);
	if (s->ensureFocus) {
		sel_scroll_focus(s, ui);
		s->ensureFocus = 0;
	}
	inner = s->sv.inner;

	if (s->visCount <= 0) {
		Gfx_Text(ui->gfx, t->fBody, "无匹配项",
			Ui_Rect(inner.x + 12.0f, inner.y, inner.w - 24.0f, rowH),
			t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	} else {
		float off = Ui_PxSnap(ui, s->sv.offset);   /* 仅绘制/命中矩形对齐 */
		first = (int)(s->sv.offset / rowH);        /* 可见范围用精确值 */
		last = (int)((s->sv.offset + inner.h) / rowH) + 1;
		if (first < 0) first = 0;
		if (last > s->visCount) last = s->visCount;
		for (k = first; k < last; k++) {
			int orig = sel_match_at(s, k);
			UiId id;
			int en, selected2;
			if (orig < 0) continue;
			id = sel_child(s->id, "ui.select.item", orig);
			en = sel_item_enabled(s, orig);
			row = Ui_Rect(inner.x,
				inner.y + (float)k * rowH - off,
				inner.w, rowH);
			st = Ui_StateInstant(ui, id, row, en);
			if (en && ui->clicked == id) {
				if (s->multiple) sel_toggle(s, ui, orig);
				else sel_commit(s, pm, ui, orig, 0);
			}
			selected2 = s->multiple
				? (s->mask & sel_bit(orig)) != 0 : (s->selected == orig);
			if (en && st.hover > 0.004f)
				Gfx_FillRoundRect(ui->gfx, Ui_RectInset(row, 1.0f), 6.0f,
					Ui_ColorAlpha(t->primary.base,
						(int)(st.hover * 18.0f)));
			col = !en ? t->btnDisabledText
				: (selected2 ? t->primary.base
					: (st.hover > 0.004f
						? Ui_ColorMix(t->toastText, t->text, st.hover)
						: t->toastText));
			trc = Ui_Rect(row.x + SEL_ITEM_PAD, row.y,
				row.w - SEL_ITEM_PAD * 2.0f - (selected2 ? 22.0f : 0.0f),
				row.h);
			Gfx_Text(ui->gfx, t->fBody, s->items[orig] ? s->items[orig] : "",
				trc, col, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			if (selected2) {
				if (t->fIcon) {
					check = UiIcon_Glyph(t->fIcon, 0xE73Eu);
					UiIcon_Draw(ui, &check,
						Ui_Rect(row.x + row.w - 24.0f, row.y + 8.0f,
							16.0f, 16.0f),
						t->primary.base, 1);
				} else {
					Gfx_Text(ui->gfx, t->fSmall, "\xE2\x9C\x93",
						Ui_Rect(row.x, row.y, row.w - 8.0f, row.h),
						t->primary.base,
						GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
				}
			}
			if (!UiPopupManager_IsOpen(pm, s->id)) break; /* 刚点选关闭 */
		}
	}

	UiScrollView_End(&s->sv, ui);
	/* 轨道翻页只写 target; 平滑滚动按真实帧间隔推进并失效下一帧. */
	{
		double now = UiFrame_Now();
		float dt = s->lastTickNow > 0.0
			? (float)(now - s->lastTickNow) : 16.0f;
		s->lastTickNow = now;
		if (dt < 0.0f) dt = 0.0f;
		if (dt > 25.0f) dt = 16.0f;   /* 空闲尖峰不参与平滑行程 */
		UiScrollView_Tick(&s->sv, ui, dt);
	}
	Ui_PopLayer(ui);
	UiPopupManager_EndDraw(pm, ui);
}

/* ── 键盘 ──────────────────────────────────────────────────────────── */

int UiSelect_Key(UiSelect *s, UiPopupManager *pm, UiContext *ui, UINT vk)
{
	int open, idx;
	if (!s || !pm || !ui || ui->navId != s->id) return 0;
	ui->navFocusVisible = 1;
	open = UiPopupManager_IsOpen(pm, s->id);
	if (!open) {
		if (vk == VK_RETURN || vk == VK_SPACE ||
			vk == VK_DOWN || vk == VK_UP) {
			sel_open(s, pm, ui, 1);
			return 1;
		}
		return 0;
	}

	if (s->searchActive && Edit_HasComposition(&s->edit) &&
		(vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB ||
			vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT ||
			vk == VK_RIGHT))
		return 1;   /* 组合期间方向键/确认键交给输入法 */

	switch (vk) {
	case VK_ESCAPE:
		UiPopupManager_Key(pm, ui, vk);
		return 1;
	case VK_UP:
		idx = sel_find(s, s->focus - 1, -1);
		if (idx >= 0) { s->focus = idx; s->ensureFocus = 1; }
		return 1;
	case VK_DOWN:
		idx = sel_find(s, s->focus + 1, 1);
		if (idx >= 0) { s->focus = idx; s->ensureFocus = 1; }
		return 1;
	case VK_PRIOR:
		idx = Ui_Clampi(s->focus - 5, 0, s->count - 1);
		idx = sel_find(s, idx, -1);
		if (idx >= 0) { s->focus = idx; s->ensureFocus = 1; }
		return 1;
	case VK_NEXT:
		idx = Ui_Clampi(s->focus + 5, 0, s->count - 1);
		idx = sel_find(s, idx, 1);
		if (idx >= 0) { s->focus = idx; s->ensureFocus = 1; }
		return 1;
	case VK_RETURN:
		if (s->focus >= 0) {
			if (s->multiple) sel_toggle(s, ui, s->focus);
			else sel_commit(s, pm, ui, s->focus, 1);
		}
		return 1;
	case VK_SPACE:
		if (s->searchActive) return 0;   /* 搜索词里的空格交给 WM_CHAR */
		if (s->focus >= 0) {
			if (s->multiple) sel_toggle(s, ui, s->focus);
			else sel_commit(s, pm, ui, s->focus, 1);
		}
		return 1;
	case VK_HOME:
		if (!s->searchActive) {
			idx = sel_first(s);
			if (idx >= 0) { s->focus = idx; s->ensureFocus = 1; }
			return 1;
		}
		break;
	case VK_END:
		if (!s->searchActive) {
			idx = sel_last(s);
			if (idx >= 0) { s->focus = idx; s->ensureFocus = 1; }
			return 1;
		}
		break;
	}

	if (s->searchActive) {
		if (Edit_Key(&s->edit, ui->hwnd, vk)) {
			s->blinkOn = 1;
			sel_refilter(s, ui);
			sel_invalidate(s, ui);
			return 1;
		}
	}
	return 0;
}

void UiSelect_SetScrollSmoothing(UiSelect *s, float tauMs)
{
	if (!s) return;
	if (tauMs < 0.0f) tauMs = 0.0f;
	s->sv.cfg.wheelTauMs = tauMs;
	if (tauMs <= 0.0f) {
		s->sv.offset = s->sv.target;
		s->sv.velocity = 0.0f;
	}
}

int UiSelect_Busy(const UiSelect *s)
{
	return s ? UiScrollView_Busy(&s->sv) : 0;
}
