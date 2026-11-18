#include "ui/ui_datepicker.h"
#include "ui/ui_icon.h"
#include "ui/ui_widgets.h"

#define DP_PAD_X     12.0f
#define DP_TOP       8.0f
#define DP_HDR_H     36.0f
#define DP_WD_H      24.0f
#define DP_CELL      36.0f
#define DP_GRID_W    (7.0f * DP_CELL)
#define DP_MONTH_GAP 12.0f
#define DP_TIME_LBL  20.0f
#define DP_TIME_ROW  28.0f
#define DP_TIME_VIEW 140.0f
#define DP_TIME_COLW 64.0f
#define DP_TIME_GAP  10.0f
#define DP_FOOT_H    40.0f
#define DP_BOTTOM    8.0f
#define DP_GLYPH_W   26.0f
#define DP_ARROW_W   20.0f

enum { DP_FOOT_OK = 0, DP_FOOT_CLEAR, DP_FOOT_TODAY, DP_FOOT_P7, DP_FOOT_P30, DP_FOOT_PMON };

static UiId dp_child(UiId id, const char *scope, int i)
{
	return Ui_IdIndex(scope, (int)(uint32_t)(id >> 32), (int)(uint32_t)id, i);
}

static int dp_is_range(const UiDatePicker *dp)
{
	return dp->mode >= UI_DP_RANGE_DATE;
}

static int dp_has_time(const UiDatePicker *dp)
{
	return dp->mode == UI_DP_DATETIME || dp->mode == UI_DP_RANGE_DATETIME;
}

static int dp_time_cols(const UiDatePicker *dp)
{
	return dp->showSeconds ? 3 : 2;
}

static void dp_apply_default_format(UiDatePicker *dp)
{
	if (dp->formatSet) return;
	if (dp->mode == UI_DP_DATE || dp->mode == UI_DP_RANGE_DATE)
		lstrcpynA(dp->format, "YYYY-MM-DD", (int)sizeof(dp->format));
	else
		lstrcpynA(dp->format,
			dp->showSeconds ? "YYYY-MM-DD HH:mm:ss" : "YYYY-MM-DD HH:mm",
			(int)sizeof(dp->format));
}

static int dp_day_enabled(const UiDatePicker *dp, int dn)
{
	if (dp->hasMin && dn < UiDate_DayNumber(dp->min.y, dp->min.m, dp->min.d)) return 0;
	if (dp->hasMax && dn > UiDate_DayNumber(dp->max.y, dp->max.m, dp->max.d)) return 0;
	if (dp->disabled && dp->disabled(dn, dp->user)) return 0;
	return 1;
}

static void dp_sync_edit(UiDatePicker *dp, int i)
{
	char buf[64];
	const UiDateTime *v = (i == 1) ? &dp->end : &dp->value;
	int has = (i == 1) ? dp->hasEnd : dp->hasValue;
	if (has) {
		UiDate_Format(v, dp->format, buf, (int)sizeof(buf));
		Edit_SetTextUtf8(&dp->edit[i], buf);
	} else {
		Edit_SetTextUtf8(&dp->edit[i], "");
	}
}

static void dp_sync_both(UiDatePicker *dp)
{
	dp_sync_edit(dp, 0);
	if (dp_is_range(dp)) dp_sync_edit(dp, 1);
}

static void dp_invalidate_trigger(UiDatePicker *dp, UiContext *ui)
{
	RECT r;
	float sc;
	UiRect rc = dp->anchor;
	if (!ui || !ui->hwnd || rc.w <= 0.0f) return;
	sc = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((rc.x - 3.0f) * sc);
	r.top = (LONG)((rc.y - 3.0f) * sc);
	r.right = (LONG)((rc.x + rc.w + 3.0f) * sc) + 1;
	r.bottom = (LONG)((rc.y + rc.h + 3.0f) * sc) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

static void dp_invalidate_panel(UiDatePicker *dp, UiContext *ui)
{
	RECT r;
	float sc;
	UiRect rc = dp->panelRc;
	if (!ui || !ui->hwnd || rc.w <= 0.0f) return;
	sc = ui->scale > 0.0f ? ui->scale : 1.0f;
	r.left = (LONG)((rc.x - 4.0f) * sc);
	r.top = (LONG)((rc.y - 4.0f) * sc);
	r.right = (LONG)((rc.x + rc.w + 4.0f) * sc) + 1;
	r.bottom = (LONG)((rc.y + rc.h + 4.0f) * sc) + 1;
	InvalidateRect(ui->hwnd, &r, FALSE);
}

/* ── 值写入 / 提交 ─────────────────────────────────────────────────── */

static void dp_commit(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	int focusOnClose)
{
	dp->committed = 1;
	dp_sync_both(dp);
	if (pm) {
		pm->focusOnClose = focusOnClose != 0;
		UiPopupManager_Close(pm, ui);
	}
}

static void dp_clear(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui)
{
	dp->hasValue = 0;
	dp->hasEnd = 0;
	dp->phase = 0;
	dp->invalid = 0;
	dp->changed = 1;
	Edit_SetTextUtf8(&dp->edit[0], "");
	Edit_SetTextUtf8(&dp->edit[1], "");
	dp_commit(dp, pm, ui, 0);
}

static void dp_order_range(UiDatePicker *dp)
{
	if (dp->hasValue && dp->hasEnd &&
		UiDate_Compare(&dp->end, &dp->value) < 0) {
		UiDateTime tmp = dp->value;
		dp->value = dp->end;
		dp->end = tmp;
	}
}

static void dp_pick_day(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	int y, int m, int d, int dn)
{
	if (!dp_day_enabled(dp, dn)) return;
	dp->focusDay = dn;
	dp->changed = 1;
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	if (!dp_is_range(dp)) {
		dp->value.y = y; dp->value.m = m; dp->value.d = d;
		dp->hasValue = 1;
		dp->viewY = y; dp->viewM = m;
		dp_sync_edit(dp, 0);
		if (!dp_has_time(dp)) dp_commit(dp, pm, ui, 0);
	} else if (dp->phase == 0) {
		dp->value.y = y; dp->value.m = m; dp->value.d = d;
		dp->hasValue = 1;
		dp->hasEnd = 0;
		dp->phase = 1;
		dp->activeEnd = 0;
		dp_sync_edit(dp, 0);
	} else {
		dp->end.y = y; dp->end.m = m; dp->end.d = d;
		dp->hasEnd = 1;
		dp->activeEnd = 1;
		dp->phase = 0;
		dp_order_range(dp);
		dp_sync_both(dp);
		if (!dp_has_time(dp)) dp_commit(dp, pm, ui, 0);
	}
}

static void dp_pick_time(UiDatePicker *dp, int end, int col, int val)
{
	UiDateTime *v = (dp_is_range(dp) && end == 1) ? &dp->end : &dp->value;
	int *has = (dp_is_range(dp) && end == 1) ? &dp->hasEnd : &dp->hasValue;
	if (col == 0) v->hh = val;
	else if (col == 1) v->mm = val;
	else v->ss = val;
	*has = 1;
	dp->changed = 1;
	if (dp_is_range(dp)) dp->activeEnd = end;
	dp_sync_edit(dp, (dp_is_range(dp) && end == 1) ? 1 : 0);
}

static void dp_set_view(UiDatePicker *dp, int y, int m)
{
	dp->viewY = y;
	dp->viewM = m;
}

static void dp_today(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui)
{
	UiDateTime now;
	UiDate_Now(&now);
	dp->changed = 1;
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	if (!dp_is_range(dp)) {
		dp->value = now;
		if (!dp_has_time(dp)) { dp->value.hh = dp->value.mm = dp->value.ss = 0; }
		dp->hasValue = 1;
		dp_set_view(dp, now.y, now.m);
		dp_sync_edit(dp, 0);
		if (!dp_has_time(dp)) dp_commit(dp, pm, ui, 0);
	} else {
		now.hh = now.mm = now.ss = 0;
		dp->value = now;
		dp->end = now;
		if (dp_has_time(dp)) { dp->end.hh = 23; dp->end.mm = 59; dp->end.ss = 59; }
		dp->hasValue = dp->hasEnd = 1;
		dp->phase = 0;
		dp_set_view(dp, now.y, now.m);
		dp_sync_both(dp);
		dp_commit(dp, pm, ui, 0);
	}
}

static void dp_preset_range(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	int startDn, int endDn)
{
	UiDateTime a, b;
	UiDate_FromDayNumber(startDn, &a.y, &a.m, &a.d);
	UiDate_FromDayNumber(endDn, &b.y, &b.m, &b.d);
	a.hh = a.mm = a.ss = 0;
	b.hh = dp_has_time(dp) ? 23 : 0;
	b.mm = dp_has_time(dp) ? 59 : 0;
	b.ss = dp_has_time(dp) ? 59 : 0;
	dp->value = a;
	dp->end = b;
	dp->hasValue = dp->hasEnd = 1;
	dp->phase = 0;
	dp->changed = 1;
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	dp_set_view(dp, a.y, a.m);
	dp_sync_both(dp);
	dp_commit(dp, pm, ui, 0);
}

/* ── 键入提交 / 回退 ───────────────────────────────────────────────── */

static void dp_revert_edit(UiDatePicker *dp)
{
	dp->invalid = 0;
	dp_sync_both(dp);
}

static void dp_commit_edit(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui, int i)
{
	char buf[64];
	UiDateTime v;
	UiEdit *e = &dp->edit[i];
	Edit_TextUtf8(e, buf, (int)sizeof(buf));
	if (!buf[0]) {
		/* 空文本 = 清除该端点 */
		if (i == 1) dp->hasEnd = 0; else dp->hasValue = 0;
		dp->changed = 1;
		dp->invalid = 0;
		return;
	}
	if (!UiDate_Parse(buf, dp->format, &v)) {
		dp->invalid = 1;
		dp_sync_both(dp);
		dp_invalidate_trigger(dp, ui);
		return;
	}
	if (!dp_day_enabled(dp, UiDate_DayNumber(v.y, v.m, v.d))) {
		dp->invalid = 1;
		dp_sync_both(dp);
		dp_invalidate_trigger(dp, ui);
		return;
	}
	dp->invalid = 0;
	dp->changed = 1;
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	if (i == 1) {
		dp->end = v;
		dp->hasEnd = 1;
		dp->activeEnd = 1;
	} else {
		dp->value = v;
		dp->hasValue = 1;
		dp_set_view(dp, v.y, v.m);
	}
	dp_order_range(dp);
	dp_sync_both(dp);
	dp_invalidate_trigger(dp, ui);
	(void)pm;
}

/* ── 焦点 / 开合 ───────────────────────────────────────────────────── */

static void dp_focus_edit(UiDatePicker *dp, UiContext *ui, int i)
{
	dp->editActive = i;
	dp->blinkOn = 1;
	if (dp_is_range(dp)) dp->activeEnd = i;
	Ui_SetFocusId(ui, dp_child(dp->id, "ui.dp.edit", i));
	ui->navId = dp->id;
	if (dp->ime) UiIme_SetFocus(dp->ime, &dp->edit[i]);
}

static void dp_edit_end(UiDatePicker *dp, UiContext *ui)
{
	if (dp->editActive < 0) return;
	if (dp->ime && dp->ime->edit == &dp->edit[dp->editActive])
		UiIme_SetFocus(dp->ime, NULL);
	if (ui->focusId == dp_child(dp->id, "ui.dp.edit", dp->editActive))
		Ui_SetFocusId(ui, UI_ID_NONE);
	dp->editActive = -1;
	dp->blinkOn = 0;
}

static void dp_month_fade(UiDatePicker *dp, UiContext *ui)
{
	int b, n = dp_is_range(dp) ? 2 : 1;
	if (ui->animReduced) return;
	if (Ui_FxActive(ui)) {
		/* 合成器路径: 下一帧由 DrawPopup 登记矩形, 期间保留旧月份像素 */
		dp->gridFx |= (1 << n) - 1;
		for (b = 0; b < n; b++)
			Ui_AnimSet(ui, dp_child(dp->id, "ui.dp.grid", b),
				UI_AN_ALPHA, 1.0f);
		return;
	}
	for (b = 0; b < n; b++) {
		UiId gid = dp_child(dp->id, "ui.dp.grid", b);
		Ui_AnimSet(ui, gid, UI_AN_ALPHA, 0.0f);
		Ui_AnimTo(ui, gid, UI_AN_ALPHA, 1.0f,
			Ui_AnimTweenMs(UI_AN_TWEEN_CONTENT), UI_AN_EASE_OUT);
	}
}

static void dp_open(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	int focusOnClose)
{
	UiDateTime now;
	UiId panelId;
	if (!pm || !ui) return;
	if (UiPopupManager_IsOpen(pm, dp->id)) return;
	if (dp->hasValue) dp_set_view(dp, dp->value.y, dp->value.m);
	else { UiDate_Now(&now); dp_set_view(dp, now.y, now.m); }
	UiDate_Now(&now);
	dp->focusDay = dp->hasValue
		? UiDate_DayNumber(dp->value.y, dp->value.m, dp->value.d)
		: UiDate_DayNumber(now.y, now.m, now.d);
	dp->hoverDay = -1;
	dp->timeFocus = -1;
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	if (dp_is_range(dp)) dp->phase = (dp->hasValue && !dp->hasEnd) ? 1 : 0;
	panelId = dp_child(dp->id, "ui.dp.panel", 0);
	if (!ui->animReduced && Ui_FxActive(ui)) {
		dp->panelFx = 1;
		Ui_AnimSet(ui, panelId, UI_AN_ALPHA, 1.0f);
	} else if (!ui->animReduced) {
		Ui_AnimSet(ui, panelId, UI_AN_ALPHA, 0.0f);
		Ui_AnimTo(ui, panelId, UI_AN_ALPHA, 1.0f,
			Ui_AnimTweenMs(UI_AN_TWEEN_PANEL), UI_AN_EASE_OUT);
	} else {
		Ui_AnimSet(ui, panelId, UI_AN_ALPHA, 1.0f);
	}
	UiPopupManager_Open(pm, ui, dp->id, dp->id, focusOnClose);
}

static void dp_close_cleanup(UiDatePicker *dp, UiContext *ui)
{
	if (dp->invalid) dp_revert_edit(dp);
	dp_edit_end(dp, ui);
	if (dp->hasValue && (!dp_is_range(dp) || dp->hasEnd))
		dp->committed = 1;
	dp->timeFocus = -1;
	dp->panelFx = 0;
	dp->gridFx = 0;
}

/* ── 生命周期 ──────────────────────────────────────────────────────── */

void UiDatePicker_Init(UiDatePicker *dp, UiId id, int mode)
{
	UiScrollConfig cfg;
	UiScrollbarStyle bar;
	UiDateTime now;
	int c, e;
	if (!dp) return;
	ZeroMemory(dp, sizeof(*dp));
	dp->id = id;
	dp->mode = mode;
	dp->weekStart = 1;
	dp->hoverDay = -1;
	dp->focusDay = -1;
	dp->timeFocus = -1;
	dp->editActive = -1;
	UiDate_Now(&now);
	dp->viewY = now.y; dp->viewM = now.m;
	dp->value = now; dp->end = now;
	dp_apply_default_format(dp);
	Edit_Init(&dp->edit[0], 0, 0, NULL);
	Edit_Init(&dp->edit[1], 0, 0, NULL);
	dp->edit[0].flat = 1;
	dp->edit[1].flat = 1;
	cfg = UiScrollConfig_Desktop();
	cfg.wheelStep = DP_TIME_ROW * 3.0f;
	cfg.wheelTauMs = 0.0f;
	cfg.bounce = 0;
	cfg.autoHideBar = 1;
	bar = UiScrollbarStyle_Default();
	bar.width = 5.0f;
	bar.minThumb = 20.0f;
	bar.margin = 1.0f;
	bar.radius = 2.5f;
	bar.track = 0x00FFFFFFu;
	bar.alwaysVisible = 0;
	bar.reserveSpace = 0;
	for (e = 0; e < 2; e++)
		for (c = 0; c < 3; c++)
			UiScrollView_Init(&dp->timeSv[e][c], dp_child(id, "ui.dp.tsv", e * 3 + c), &cfg, &bar);
}

void UiDatePicker_Shutdown(UiDatePicker *dp)
{
	if (!dp) return;
	if (dp->ime &&
		(dp->ime->edit == &dp->edit[0] || dp->ime->edit == &dp->edit[1]))
		UiIme_SetFocus(dp->ime, NULL);
	Edit_Shutdown(&dp->edit[0]);
	Edit_Shutdown(&dp->edit[1]);
}

void UiDatePicker_SetMode(UiDatePicker *dp, int mode)
{
	if (!dp) return;
	dp->mode = mode;
	dp->phase = 0;
	dp->activeEnd = 0;
	dp_apply_default_format(dp);
	dp_sync_both(dp);
}

void UiDatePicker_SetValue(UiDatePicker *dp, const UiDateTime *v)
{
	if (!dp || !v) return;
	dp->value = *v;
	dp->hasValue = 1;
	dp_set_view(dp, v->y, v->m);
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	dp_sync_edit(dp, 0);
}

void UiDatePicker_SetRange(UiDatePicker *dp, const UiDateTime *a, const UiDateTime *b)
{
	if (!dp || !a || !b) return;
	dp->value = *a;
	dp->end = *b;
	dp->hasValue = dp->hasEnd = 1;
	dp_order_range(dp);
	dp_set_view(dp, dp->value.y, dp->value.m);
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	dp_sync_both(dp);
}

void UiDatePicker_SetBounds(UiDatePicker *dp, const UiDateTime *min, const UiDateTime *max)
{
	if (!dp) return;
	dp->hasMin = min != NULL;
	dp->hasMax = max != NULL;
	if (min) dp->min = *min;
	if (max) dp->max = *max;
}

void UiDatePicker_SetDisabledFn(UiDatePicker *dp, int (*fn)(int, void *), void *user)
{
	if (!dp) return;
	dp->disabled = fn;
	dp->user = user;
}

void UiDatePicker_SetFormat(UiDatePicker *dp, const char *pattern)
{
	if (!dp || !pattern) return;
	lstrcpynA(dp->format, pattern, (int)sizeof(dp->format));
	dp->formatSet = 1;
	dp_sync_both(dp);
}

void UiDatePicker_SetWeekStart(UiDatePicker *dp, int ws)
{
	if (dp) dp->weekStart = Ui_Clampi(ws, 0, 6);
}

void UiDatePicker_SetShowSeconds(UiDatePicker *dp, int on)
{
	if (!dp) return;
	dp->showSeconds = on != 0;
	dp_apply_default_format(dp);
	dp->timeCenter = dp_is_range(dp) ? 3 : 1;
	dp_sync_both(dp);
}

void UiDatePicker_SetPlaceholder(UiDatePicker *dp, const char *text)
{
	if (!dp) return;
	lstrcpynA(dp->placeholder, text ? text : "", (int)sizeof(dp->placeholder));
}

void UiDatePicker_AttachIme(UiDatePicker *dp, UiIme *ime)
{
	if (dp) dp->ime = ime;
}

/* ── 触发器 ────────────────────────────────────────────────────────── */

int Ui_DatePicker(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	UiRect rc, const char *placeholder, int enabled)
{
	UiTheme *t;
	UiWidgetState st;
	UiColor bg, border;
	UiRect glyphRc, segRc[2];
	float availW, segW, hv, manual;
	int open, focus, i, nseg;
	const char *ph;
	UiIcon cal;

	if (!dp || !ui || !ui->theme || !ui->gfx) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	t = ui->theme;
	dp->changed = 0;
	dp->committed = 0;
	dp->anchor = rc;
	open = pm && UiPopupManager_IsOpen(pm, dp->id);
	if (!open && dp->wasOpen) dp_close_cleanup(dp, ui);
	dp->wasOpen = open;

	st = Ui_State(ui, dp->id, rc, enabled);
	/* 子键入段/glyph 会抢走 hover, 父触发器按命中归属平滑描边;
	 * 纯几何会在被弹层盖住时误亮 (弹层打开时底层不参与命中). */
	manual = 0.0f;
	if (enabled && ui->mouseOver) {
		UiId h = ui->hover;
		if (h == dp->id || h == dp_child(dp->id, "ui.dp.edit", 0) ||
			h == dp_child(dp->id, "ui.dp.edit", 1) ||
			h == dp_child(dp->id, "ui.dp.glyph", 0))
			manual = 1.0f;
	}
	if (Ui_FxActive(ui)) {
		/* 合成器路径: 直接落最终值, 由 SnapshotFx 覆盖层做过渡 */
		Ui_AnimSet(ui, dp->id, UI_AN_HOVER, manual);
		hv = manual;
	} else {
		Ui_AnimSmooth(ui, dp->id, UI_AN_HOVER, manual);
		hv = Ui_AnimGet(ui, dp->id, UI_AN_HOVER, 0.0f);
	}
	if (st.hover > hv) hv = st.hover;
	focus = ui->navFocusVisible && ui->navId == dp->id;
	if (!enabled) {
		bg = t->neutral.soft;
		border = t->neutral.border;
	} else {
		bg = t->neutral.base;
		border = dp->invalid ? t->error.base
			: (open || focus) ? t->primary.base
			: (hv > 0.004f
				? Ui_ColorMix(t->neutral.border, t->primary.base, hv * 0.6f)
				: t->neutral.border);
	}
	Gfx_FillRoundRect(ui->gfx, rc, 6.0f, bg);
	Gfx_StrokeRoundRect(ui->gfx, rc, 6.0f, border, 1.0f);

	nseg = dp_is_range(dp) ? 2 : 1;
	availW = rc.w - DP_PAD_X * 2.0f - DP_GLYPH_W - (dp_is_range(dp) ? DP_ARROW_W : 0.0f);
	segW = availW / (float)nseg;
	segRc[0] = Ui_Rect(rc.x + DP_PAD_X, rc.y, segW, rc.h);
	if (nseg == 2)
		segRc[1] = Ui_Rect(segRc[0].x + segW + DP_ARROW_W, rc.y, segW, rc.h);

	ph = (placeholder && placeholder[0]) ? placeholder : dp->placeholder;
	for (i = 0; i < nseg; i++) {
		UiId eid = dp_child(dp->id, "ui.dp.edit", i);
		const wchar_t *hint = (i == 1) ? L"结束" : NULL;
		wchar_t ph8[96];
		dp->edit[i].focused = (dp->editActive == i) && enabled;
		dp->edit[i].caretOn = dp->blinkOn;
		if (i == 0 && ph && ph[0]) {
			/* placeholder 是 UTF-8; Edit_Draw 需要 UTF-16 hint */
			MultiByteToWideChar(CP_UTF8, 0, ph, -1, ph8, 96);
			hint = ph8;
		}
		Edit_Draw(ui, eid, segRc[i], &dp->edit[i], hint);
		if (enabled && ui->clicked == eid) {
			dp_focus_edit(dp, ui, i);
			Edit_PlaceCaret(&dp->edit[i], ui->gfx, t->fInput, segRc[i], ui->mouseX);
			if (!open) { dp_open(dp, pm, ui, 0); open = 1; dp->wasOpen = 1; }
			dp_invalidate_trigger(dp, ui);
		}
	}
	if (nseg == 2) {
		Gfx_Text(ui->gfx, t->fBody, "\xE2\x86\x92",
			Ui_Rect(segRc[0].x + segW, rc.y, DP_ARROW_W, rc.h),
			t->text3, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}

	glyphRc = Ui_Rect(rc.x + rc.w - DP_GLYPH_W - 4.0f,
		rc.y + (rc.h - 18.0f) * 0.5f, 18.0f, 18.0f);
	{
		UiId gid = dp_child(dp->id, "ui.dp.glyph", 0);
		UiWidgetState gs = Ui_StateInstant(ui, gid, glyphRc, enabled);
		UiColor gc = !enabled ? t->btnDisabledText
			: Ui_ColorMix(t->text3, t->primary.base, gs.hover);
		if (t->fIcon) {
			cal = UiIcon_Glyph(t->fIcon, 0xE787u);
			UiIcon_Draw(ui, &cal, glyphRc, gc, enabled);
		} else {
			Gfx_StrokeRoundRect(ui->gfx, Ui_RectInset(glyphRc, 1.0f), 2.0f, gc, 1.4f);
			Gfx_Line(ui->gfx, glyphRc.x + 1.0f, glyphRc.y + 5.0f,
				glyphRc.x + glyphRc.w - 1.0f, glyphRc.y + 5.0f, gc, 1.4f);
		}
		if (enabled && ui->clicked == gid) {
			ui->navId = dp->id;
			if (open && pm) UiPopupManager_Close(pm, ui);
			else { dp_edit_end(dp, ui); dp_open(dp, pm, ui, ui->navFocusVisible ? 1 : 0); }
			open = pm && UiPopupManager_IsOpen(pm, dp->id);
			dp->wasOpen = open;
			dp_invalidate_trigger(dp, ui);
		}
	}
	return dp->changed;
}

/* ── 月历面板 ──────────────────────────────────────────────────────── */

static void dp_nav(UiDatePicker *dp, UiContext *ui, int delta)
{
	UiDateTime v;
	v.y = dp->viewY; v.m = dp->viewM; v.d = 1; v.hh = v.mm = v.ss = 0;
	UiDate_AddMonths(&v, delta);
	dp_set_view(dp, v.y, v.m);
	dp_month_fade(dp, ui);
	dp_invalidate_panel(dp, ui);
}

static void dp_draw_month(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	float bx, float by, int viewY, int viewM, int panelIdx, int todayDn)
{
	UiTheme *t = ui->theme;
	float cellW = DP_CELL;
	float gy = by + DP_HDR_H + DP_WD_H;
	int firstDn = UiDate_DayNumber(viewY, viewM, 1);
	int offset = (UiDate_Weekday(firstDn) - dp->weekStart + 7) % 7;
	int gridStart = firstDn - offset;
	int i, isR = dp_is_range(dp);
	char lab[32];
	UiRect hdr = Ui_Rect(bx, by, DP_GRID_W, DP_HDR_H);
	UiId gridId = dp_child(dp->id, "ui.dp.grid", panelIdx);
	float alpha = Ui_AnimGet(ui, gridId, UI_AN_ALPHA, 1.0f);
	int pushed;

	/* 换月: 本帧不画该月块 (保留旧月份像素), 下一帧由覆盖层交叉淡化 */
	if (dp->gridFx & (1 << panelIdx)) {
		UiRect block = Ui_Rect(bx, by, DP_GRID_W,
			DP_HDR_H + DP_WD_H + 6.0f * DP_CELL);
		dp->gridFx &= ~(1 << panelIdx);
		Ui_FxChangeRect(ui, block,
			Ui_AnimTweenMs(UI_AN_TWEEN_CONTENT));
		return;
	}

	/* 头部导航: 单值左右都有; 范围左块只有‹, 右块只有› */
	{
		int showPrev = !isR || panelIdx == 0;
		int showNext = !isR || panelIdx == 1;
		UiButtonStyle ns = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
			UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
		if (showPrev) {
			UiRect pr = Ui_Rect(bx, by + 4.0f, 28.0f, 28.0f);
			UiId nid = dp_child(dp->id, "ui.dp.nav", 0);
			if (t->fIcon) {
				UiIcon ic = UiIcon_Glyph(t->fIcon, 0xE76Bu);
				if (Ui_IconButton(ui, nid, pr, &ic, &ns, 1)) dp_nav(dp, ui, -1);
			} else if (Ui_ButtonEx(ui, nid, pr, "\xE2\x80\xB9", &ns, 1)) {
				dp_nav(dp, ui, -1);
			}
		}
		if (showNext) {
			UiRect nr = Ui_Rect(bx + DP_GRID_W - 28.0f, by + 4.0f, 28.0f, 28.0f);
			UiId nid = dp_child(dp->id, "ui.dp.nav", 1);
			if (t->fIcon) {
				UiIcon ic = UiIcon_Glyph(t->fIcon, 0xE76Cu);
				if (Ui_IconButton(ui, nid, nr, &ic, &ns, 1)) dp_nav(dp, ui, 1);
			} else if (Ui_ButtonEx(ui, nid, nr, "\xE2\x80\xBA", &ns, 1)) {
				dp_nav(dp, ui, 1);
			}
		}
	}
	wsprintfA(lab, "%d\xE5\xB9\xB4 %d\xE6\x9C\x88", viewY, viewM);
	Gfx_Text(ui->gfx, t->fHead, lab, hdr, t->text,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER);

	/* 星期行 */
	{
		static const char *wd[7] = {
			"\xE6\x97\xA5", "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89",
			"\xE5\x9B\x9B", "\xE4\xBA\x94", "\xE5\x85\xAD"
		};
		for (i = 0; i < 7; i++) {
			int w = (dp->weekStart + i) % 7;
			Gfx_Text(ui->gfx, t->fSmall, wd[w],
				Ui_Rect(bx + i * cellW, by + DP_HDR_H, cellW, DP_WD_H),
				t->text3, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		}
	}

	/* 日格 (淡入补间) */
	Ui_AnimSetRect(ui, gridId, Ui_Rect(bx, gy, DP_GRID_W, 6.0f * cellW));
	pushed = Gfx_PushAlpha(ui->gfx, alpha);
	for (i = 0; i < 42; i++) {
		int dn = gridStart + i;
		int y, m, d, en, inMonth, sel, endpoint, inRange, isToday;
		UiRect cell = Ui_Rect(bx + (i % 7) * cellW, gy + (i / 7) * cellW,
			cellW, cellW);
		UiRect in = Ui_RectInset(cell, 2.0f);
		UiId cid = dp_child(dp->id, "ui.dp.day", panelIdx * 42 + i);
		UiWidgetState st;
		UiColor col;
		char num[8];
		UiDate_FromDayNumber(dn, &y, &m, &d);
		inMonth = (y == viewY && m == viewM);
		en = dp_day_enabled(dp, dn);
		st = Ui_StateInstant(ui, cid, cell, en);
		if (en && ui->clicked == cid)
			dp_pick_day(dp, pm, ui, y, m, d, dn);

		endpoint = 0; inRange = 0; sel = 0;
		if (!isR) {
			sel = dp->hasValue && dn == UiDate_DayNumber(dp->value.y, dp->value.m, dp->value.d);
		} else {
			int sDn = dp->hasValue ? UiDate_DayNumber(dp->value.y, dp->value.m, dp->value.d) : -1;
			int eDn = dp->hasEnd ? UiDate_DayNumber(dp->end.y, dp->end.m, dp->end.d)
				: (dp->phase == 1 ? dp->hoverDay : -1);
			int lo = sDn, hi = eDn;
			if (lo > hi) { int tmp = lo; lo = hi; hi = tmp; }
			endpoint = (dn == sDn) || (dn == eDn);
			inRange = (sDn >= 0 && eDn >= 0 && dn > lo && dn < hi);
			sel = endpoint;
		}
		isToday = (dn == todayDn);

		if (inRange)
			Gfx_FillRect(ui->gfx, cell, t->primary.soft);
		if (en && st.hover > 0.004f && !sel)
			Gfx_FillRoundRect(ui->gfx, in, 6.0f,
				Ui_ColorAlpha(t->primary.base, (int)(st.hover * 32.0f)));
		if (sel && en)
			Gfx_FillRoundRect(ui->gfx, in, 6.0f, t->primary.base);
		else if (isToday)
			Gfx_StrokeRoundRect(ui->gfx, in, 6.0f, t->primary.base, 1.0f);
		/* 键盘焦点环: 比 today 环更粗更外圈, 统一 text 色;
		 * 选中格上反色白环会融进浅色背景, 不可见 */
		if (en && ui->navFocusVisible && dp->timeFocus < 0 && dn == dp->focusDay)
			Gfx_StrokeRoundRect(ui->gfx, Ui_RectInset(cell, 1.0f), 7.0f, t->text, 2.0f);

		col = !en ? t->btnDisabledText
			: (sel ? t->primary.text
				: (inMonth ? t->toastText : t->text4));
		wsprintfA(num, "%d", d);
		Gfx_Text(ui->gfx, t->fBody, num, cell, col,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}
	if (pushed) Gfx_PopAlpha(ui->gfx);
}

/* ── 时间列 ────────────────────────────────────────────────────────── */

static void dp_draw_time(UiDatePicker *dp, UiContext *ui, UiRect panel, float ty,
	const float *bx, int isR)
{
	UiTheme *t = ui->theme;
	int ncol = dp_time_cols(dp), ngrp = isR ? 2 : 1, e, c;
	float totalW = ncol * DP_TIME_COLW + (ncol - 1) * DP_TIME_GAP;
	float lblY = ty + (isR ? DP_TIME_LBL : 0.0f);
	static const char *labels[3] = { "\xE6\x97\xB6", "\xE5\x88\x86", "\xE7\xA7\x92" };
	static const char *ends[2] = { "\xE5\xBC\x80\xE5\xA7\x8B", "\xE7\xBB\x93\xE6\x9D\x9F" };

	for (e = 0; e < ngrp; e++) {
		UiDateTime *v = (isR && e == 1) ? &dp->end : &dp->value;
		float baseX = isR ? bx[e] : panel.x;
		float baseW = isR ? DP_GRID_W : panel.w;
		float sx = baseX + (baseW - totalW) * 0.5f;
		if (isR) {
			int act = (dp->timeFocus >= 0) ? (dp->timeFocus / ncol == e)
				: (dp->activeEnd == e);
			Gfx_Text(ui->gfx, t->fSmall, ends[e],
				Ui_Rect(baseX, ty, baseW, DP_TIME_LBL),
				act ? t->primary.base : t->text3,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		}
		for (c = 0; c < ncol; c++) {
			UiScrollView *sv = &dp->timeSv[e][c];
			float cx = sx + c * (DP_TIME_COLW + DP_TIME_GAP);
			UiRect vp = Ui_Rect(cx, lblY + DP_TIME_LBL, DP_TIME_COLW, DP_TIME_VIEW);
			int count = (c == 0) ? 24 : 60;
			int cur = (c == 0) ? v->hh : (c == 1) ? v->mm : v->ss;
			float contentH = count * DP_TIME_ROW;
			UiRect inner, row;
			int first, last, k;
			Gfx_Text(ui->gfx, t->fSmall, labels[c],
				Ui_Rect(cx, lblY, DP_TIME_COLW, DP_TIME_LBL), t->text3,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
			UiScrollView_Begin(sv, ui, vp, 0.0f, contentH);
			if (dp->timeCenter & (1 << e)) {
				float off = cur * DP_TIME_ROW - (DP_TIME_VIEW - DP_TIME_ROW) * 0.5f;
				float maxOff = contentH - DP_TIME_VIEW;
				if (maxOff < 0.0f) maxOff = 0.0f;
				UiScrollView_ScrollTo(sv, ui, Ui_Clampf(off, 0.0f, maxOff), 0);
			}
			inner = sv->inner;
			first = (int)(sv->offset / DP_TIME_ROW);
			last = (int)((sv->offset + inner.h) / DP_TIME_ROW) + 1;
			if (first < 0) first = 0;
			if (last > count) last = count;
			for (k = first; k < last; k++) {
				UiId rid = dp_child(dp->id, "ui.dp.trow", e * 192 + c * 64 + k);
				UiWidgetState st;
				UiColor col;
				char b[8];
				int sel = (k == cur);
				row = Ui_Rect(inner.x, inner.y + k * DP_TIME_ROW - sv->offset,
					inner.w, DP_TIME_ROW);
				st = Ui_StateInstant(ui, rid, row, 1);
				if (ui->clicked == rid) dp_pick_time(dp, e, c, k);
				if (sel)
					Gfx_FillRoundRect(ui->gfx, Ui_RectInset(row, 1.0f), 4.0f, t->primary.soft);
				else if (st.hover > 0.004f)
					Gfx_FillRoundRect(ui->gfx, Ui_RectInset(row, 1.0f), 4.0f,
						Ui_ColorAlpha(t->primary.base, (int)(st.hover * 28.0f)));
				col = sel ? t->primary.base : t->toastText;
				wsprintfA(b, "%02d", k);
				Gfx_Text(ui->gfx, t->fBody, b, row, col,
					GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
			}
			UiScrollView_End(sv, ui);
			UiScrollView_Tick(sv, ui, 16.0f);
		}
	}
	dp->timeCenter = 0;
}

/* ── 页脚 ──────────────────────────────────────────────────────────── */

static void dp_draw_footer(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	UiRect panel, float fy)
{
	UiTheme *t = ui->theme;
	float bh = 28.0f, by = fy + (DP_FOOT_H - bh) * 0.5f;
	float rx = panel.x + panel.w - DP_PAD_X;
	float lx = panel.x + DP_PAD_X;
	UiButtonStyle txt = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	UiButtonStyle pri = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiRect okRc = Ui_Rect(rx - 64.0f, by, 64.0f, bh);
	UiRect clRc = Ui_Rect(okRc.x - 8.0f - 64.0f, by, 64.0f, bh);
	int act = -1, todayDn = 0, firstDn = 0, lastDn = 0;
	(void)t;
	/* 动作必须在所有按钮画完之后执行: dp_clear/dp_commit 会当场关弹层,
	 * 提前 return 会让本帧剩下的页脚按钮缺画 (整块面板被重填后少字). */
	if (Ui_ButtonEx(ui, dp_child(dp->id, "ui.dp.foot", DP_FOOT_OK), okRc,
		"\xE7\xA1\xAE\xE5\xAE\x9A", &pri, 1))
		act = DP_FOOT_OK;
	if (Ui_ButtonEx(ui, dp_child(dp->id, "ui.dp.foot", DP_FOOT_CLEAR), clRc,
		"\xE6\xB8\x85\xE9\x99\xA4", &txt, 1))
		act = DP_FOOT_CLEAR;
	if (!dp_is_range(dp)) {
		const char *lab = dp_has_time(dp) ? "\xE6\xAD\xA4\xE5\x88\xBB" : "\xE4\xBB\x8A\xE5\xA4\xA9";
		if (Ui_ButtonEx(ui, dp_child(dp->id, "ui.dp.foot", DP_FOOT_TODAY),
			Ui_Rect(lx, by, 64.0f, bh), lab, &txt, 1))
			act = DP_FOOT_TODAY;
	} else {
		UiDateTime now;
		UiDate_Now(&now);
		todayDn = UiDate_DayNumber(now.y, now.m, now.d);
		firstDn = UiDate_DayNumber(now.y, now.m, 1);
		lastDn = UiDate_DayNumber(now.y, now.m, UiDate_DaysInMonth(now.y, now.m));
		if (Ui_ButtonEx(ui, dp_child(dp->id, "ui.dp.foot", DP_FOOT_TODAY),
			Ui_Rect(lx, by, 64.0f, bh), "\xE4\xBB\x8A\xE5\xA4\xA9", &txt, 1))
			act = DP_FOOT_TODAY;
		if (Ui_ButtonEx(ui, dp_child(dp->id, "ui.dp.foot", DP_FOOT_P7),
			Ui_Rect(lx + 70.0f, by, 64.0f, bh), "\xE8\xBF\x91" "7\xE5\xA4\xA9", &txt, 1))
			act = DP_FOOT_P7;
		if (Ui_ButtonEx(ui, dp_child(dp->id, "ui.dp.foot", DP_FOOT_P30),
			Ui_Rect(lx + 140.0f, by, 70.0f, bh), "\xE8\xBF\x91" "30\xE5\xA4\xA9", &txt, 1))
			act = DP_FOOT_P30;
		if (Ui_ButtonEx(ui, dp_child(dp->id, "ui.dp.foot", DP_FOOT_PMON),
			Ui_Rect(lx + 216.0f, by, 64.0f, bh), "\xE6\x9C\xAC\xE6\x9C\x88", &txt, 1))
			act = DP_FOOT_PMON;
	}
	switch (act) {
	case DP_FOOT_OK:
		if (dp->hasValue && (!dp_is_range(dp) || dp->hasEnd)) dp_commit(dp, pm, ui, 1);
		else if (pm) UiPopupManager_Close(pm, ui);
		break;
	case DP_FOOT_CLEAR:
		dp_clear(dp, pm, ui);
		break;
	case DP_FOOT_TODAY:
		if (dp_is_range(dp)) dp_preset_range(dp, pm, ui, todayDn, todayDn);
		else dp_today(dp, pm, ui);
		break;
	case DP_FOOT_P7:
		dp_preset_range(dp, pm, ui, todayDn - 6, todayDn);
		break;
	case DP_FOOT_P30:
		dp_preset_range(dp, pm, ui, todayDn - 29, todayDn);
		break;
	case DP_FOOT_PMON:
		dp_preset_range(dp, pm, ui, firstDn, lastDn);
		break;
	default:
		break;
	}
}

/* ── 弹层 ──────────────────────────────────────────────────────────── */

void UiDatePicker_DrawPopup(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui)
{
	UiTheme *t;
	UiRect panel;
	float w, h, gridH, gy, bx[2], ty, fy;
	int nblk, b, pushed, isR, hasT;
	UiId panelId;
	UiDateTime now;
	int todayDn;
	float alpha;

	if (!dp || !pm || !ui || !ui->theme || !ui->gfx) return;
	if (!UiPopupManager_IsOpen(pm, dp->id)) return;
	t = ui->theme;
	isR = dp_is_range(dp);
	hasT = dp_has_time(dp);
	nblk = isR ? 2 : 1;
	gridH = 6.0f * DP_CELL;
	w = isR ? (DP_GRID_W * 2.0f + DP_MONTH_GAP + DP_PAD_X * 2.0f)
		: (DP_GRID_W + DP_PAD_X * 2.0f);
	h = DP_TOP + DP_HDR_H + DP_WD_H + gridH + DP_BOTTOM + DP_FOOT_H + 8.0f;
	if (hasT) h += DP_TIME_LBL + DP_TIME_VIEW + 8.0f;
	if (hasT && isR) h += DP_TIME_LBL; /* 起/止组标题行 */
	if (ui->width > 0.0f && w > ui->width - 16.0f) w = ui->width - 16.0f;

	panel = UiPopup_Place(ui, dp->anchor, w, h, UI_POPUP_BELOW,
		UI_POPUP_ALIGN_START, 6.0f);
	dp->panelRc = panel;
	UiPopupManager_SetVisualRect(pm, panel);
	Ui_OccludeRect(ui, Ui_Rect(panel.x - 12.0f, panel.y - 12.0f,
		panel.w + 24.0f, panel.h + 24.0f));
	/* 合成器淡入: 本帧不画面板 (uiTarget 保留上一帧像素), 下一帧
	 * 正常绘制并由覆盖层从旧画面淡到新面板. */
	if (dp->panelFx) {
		dp->panelFx = 0;
		Ui_FxChangeRect(ui, panel,
			Ui_AnimTweenMs(UI_AN_TWEEN_PANEL));
		return;
	}
	UiDate_Now(&now);
	todayDn = UiDate_DayNumber(now.y, now.m, now.d);

	bx[0] = panel.x + DP_PAD_X;
	if (isR) bx[1] = bx[0] + DP_GRID_W + DP_MONTH_GAP;
	gy = panel.y + DP_TOP;
	ty = gy + DP_HDR_H + DP_WD_H + gridH + 8.0f;
	fy = panel.y + panel.h - DP_BOTTOM - DP_FOOT_H;

	/* 范围预览: 先按几何算出悬停日, 两块共用 */
	dp->hoverDay = -1;
	if (isR && dp->phase == 1 && ui->mouseOver) {
		for (b = 0; b < nblk; b++) {
			int vy = dp->viewY, vm = dp->viewM;
			if (b == 1) {
				UiDateTime nv; nv.y = vy; nv.m = vm; nv.d = 1; nv.hh = nv.mm = nv.ss = 0;
				UiDate_AddMonths(&nv, 1); vy = nv.y; vm = nv.m;
			}
			int firstDn = UiDate_DayNumber(vy, vm, 1);
			int off = (UiDate_Weekday(firstDn) - dp->weekStart + 7) % 7;
			int gs = firstDn - off, i;
			for (i = 0; i < 42; i++) {
				UiRect cell = Ui_Rect(bx[b] + (i % 7) * DP_CELL,
					gy + DP_HDR_H + DP_WD_H + (i / 7) * DP_CELL, DP_CELL, DP_CELL);
				if (Ui_RectContains(cell, ui->mouseX, ui->mouseY)) {
					int dn = gs + i;
					if (dp_day_enabled(dp, dn)) dp->hoverDay = dn;
				}
			}
		}
	}

	panelId = dp_child(dp->id, "ui.dp.panel", 0);
	alpha = Ui_AnimGet(ui, panelId, UI_AN_ALPHA, 1.0f);
	Ui_AnimSetRect(ui, panelId, panel);
	Ui_PushLayer(ui, UI_Z_OVERLAY);
	pushed = Gfx_PushAlpha(ui->gfx, alpha);
	Gfx_FillRoundRect(ui->gfx, panel, 8.0f, t->toastBg);
	Gfx_StrokeRoundRect(ui->gfx, panel, 8.0f, t->toastBorder, 1.0f);

	/* 点选日格可能当场关闭弹层 (单值/范围完成): 仍要把本帧面板画完整,
	 * 关闭淡出抓的就是这一帧的画面, 半帧内容会在淡出里缺格. */
	for (b = 0; b < nblk; b++) {
		int vy = dp->viewY, vm = dp->viewM;
		if (b == 1) {
			UiDateTime nv; nv.y = vy; nv.m = vm; nv.d = 1; nv.hh = nv.mm = nv.ss = 0;
			UiDate_AddMonths(&nv, 1); vy = nv.y; vm = nv.m;
		}
		dp_draw_month(dp, pm, ui, bx[b], gy, vy, vm, b, todayDn);
	}
	if (hasT) dp_draw_time(dp, ui, panel, ty, bx, isR);
	dp_draw_footer(dp, pm, ui, panel, fy);
	if (pushed) Gfx_PopAlpha(ui->gfx);
	Ui_PopLayer(ui);
}

/* ── 键盘 ──────────────────────────────────────────────────────────── */

static void dp_set_focus_day(UiDatePicker *dp, UiContext *ui, int dn)
{
	int y, m, d, vkey, fkey, span;
	UiDate_FromDayNumber(dn, &y, &m, &d);
	dp->focusDay = dn;
	vkey = dp->viewY * 12 + dp->viewM - 1;
	fkey = y * 12 + m - 1;
	span = dp_is_range(dp) ? 2 : 1;
	if (fkey < vkey) {
		dp_set_view(dp, y, m); dp_month_fade(dp, ui);
	} else if (fkey >= vkey + span) {
		if (dp_is_range(dp)) {
			UiDateTime nv; nv.y = y; nv.m = m; nv.d = 1; nv.hh = nv.mm = nv.ss = 0;
			UiDate_AddMonths(&nv, -1);
			dp_set_view(dp, nv.y, nv.m);
		} else {
			dp_set_view(dp, y, m);
		}
		dp_month_fade(dp, ui);
	}
	dp_invalidate_panel(dp, ui);
}

static void dp_focus_month(UiDatePicker *dp, UiContext *ui, int delta)
{
	UiDateTime v;
	int dn;
	if (dp->focusDay < 0) return;
	UiDate_FromDayNumber(dp->focusDay, &v.y, &v.m, &v.d);
	v.hh = v.mm = v.ss = 0;
	UiDate_AddMonths(&v, delta);
	dn = UiDate_DayNumber(v.y, v.m, v.d);
	dp_set_focus_day(dp, ui, dn);
}

int UiDatePicker_Key(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui, UINT vk)
{
	int open;
	if (!dp || !pm || !ui || ui->navId != dp->id) return 0;
	ui->navFocusVisible = 1;
	open = UiPopupManager_IsOpen(pm, dp->id);

	if (vk == VK_ESCAPE) {
		if (open) UiPopupManager_Key(pm, ui, vk);
		else if (dp->editActive >= 0) { dp_revert_edit(dp); dp_edit_end(dp, ui); }
		dp_invalidate_trigger(dp, ui);
		return 1;
	}

	if (!open) {
		if (dp->editActive >= 0) {
			if (vk == VK_RETURN) { dp_commit_edit(dp, pm, ui, dp->editActive); return 1; }
			if (vk == VK_DOWN || vk == VK_SPACE) { dp_open(dp, pm, ui, 1); return 1; }
			if (Edit_Key(&dp->edit[dp->editActive], ui->hwnd, vk)) {
				dp->blinkOn = 1; dp_invalidate_trigger(dp, ui); return 1;
			}
			return 0;
		}
		if (vk == VK_RETURN || vk == VK_SPACE || vk == VK_DOWN || vk == VK_UP) {
			dp_open(dp, pm, ui, 1);
			return 1;
		}
		return 0;
	}

	/* 打开: 键入段优先 */
	if (dp->editActive >= 0) {
		UiEdit *e = &dp->edit[dp->editActive];
		if (Edit_HasComposition(e) &&
			(vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB ||
				vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT))
			return 1;
		if (vk == VK_RETURN) { dp_commit_edit(dp, pm, ui, dp->editActive); return 1; }
		if (vk == VK_TAB) { dp_edit_end(dp, ui); }
		else if (Edit_Key(e, ui->hwnd, vk)) {
			dp->blinkOn = 1; dp_invalidate_trigger(dp, ui); return 1;
		}
	}

	/* 时间列焦点 */
	if (dp->timeFocus >= 0) {
		int ncol = dp_time_cols(dp);
		int ngrp = dp_is_range(dp) ? 2 : 1;
		int total = ncol * ngrp;
		int end = dp->timeFocus / ncol, col = dp->timeFocus % ncol;
		UiDateTime *v = (end == 1) ? &dp->end : &dp->value;
		int *cur = (col == 0) ? &v->hh : (col == 1) ? &v->mm : &v->ss;
		int maxv = (col == 0) ? 23 : 59;
		switch (vk) {
		case VK_UP: *cur = (*cur + 1) % (maxv + 1); dp->changed = 1; dp->timeCenter |= 1 << end; break;
		case VK_DOWN: *cur = (*cur - 1 + maxv + 1) % (maxv + 1); dp->changed = 1; dp->timeCenter |= 1 << end; break;
		case VK_LEFT: dp->timeFocus = (dp->timeFocus - 1 + total) % total; break;
		case VK_RIGHT: dp->timeFocus = (dp->timeFocus + 1) % total; break;
		case VK_RETURN: dp->timeFocus = -1; break;
		case VK_TAB:
			if (end + 1 < ngrp) { dp->timeFocus = (end + 1) * ncol; dp->timeCenter |= 1 << (end + 1); }
			else dp->timeFocus = -1;
			break;
		default: return 0;
		}
		if (dp_is_range(dp) && dp->timeFocus >= 0) dp->activeEnd = dp->timeFocus / ncol;
		dp_sync_edit(dp, end);
		dp_invalidate_panel(dp, ui);
		return 1;
	}

	/* 日历焦点 */
	if (dp->focusDay < 0) {
		UiDateTime now; UiDate_Now(&now);
		dp->focusDay = UiDate_DayNumber(now.y, now.m, now.d);
	}
	switch (vk) {
	case VK_LEFT: dp_set_focus_day(dp, ui, dp->focusDay - 1); return 1;
	case VK_RIGHT: dp_set_focus_day(dp, ui, dp->focusDay + 1); return 1;
	case VK_UP: dp_set_focus_day(dp, ui, dp->focusDay - 7); return 1;
	case VK_DOWN: dp_set_focus_day(dp, ui, dp->focusDay + 7); return 1;
	case VK_PRIOR: dp_focus_month(dp, ui, -1); return 1;
	case VK_NEXT: dp_focus_month(dp, ui, 1); return 1;
	case VK_HOME: {
		int off = (UiDate_Weekday(dp->focusDay) - dp->weekStart + 7) % 7;
		dp_set_focus_day(dp, ui, dp->focusDay - off); return 1;
	}
	case VK_END: {
		int off = (UiDate_Weekday(dp->focusDay) - dp->weekStart + 7) % 7;
		dp_set_focus_day(dp, ui, dp->focusDay + (6 - off)); return 1;
	}
	case VK_RETURN: {
		int y, m, d;
		UiDate_FromDayNumber(dp->focusDay, &y, &m, &d);
		dp_pick_day(dp, pm, ui, y, m, d, dp->focusDay);
		dp_invalidate_panel(dp, ui);
		return 1;
	}
	case VK_TAB:
		if (dp_has_time(dp)) {
			int e0 = dp_is_range(dp) ? dp->activeEnd : 0;
			dp->timeFocus = e0 * dp_time_cols(dp);
			dp->timeCenter |= 1 << e0;
		} else dp_focus_edit(dp, ui, 0);
		dp_invalidate_panel(dp, ui);
		return 1;
	}
	return 0;
}

int UiDatePicker_Char(UiDatePicker *dp, UiContext *ui, wchar_t ch)
{
	if (!dp || !ui || dp->editActive < 0) return 0;
	if (Edit_Char(&dp->edit[dp->editActive], ch)) {
		dp->blinkOn = 1;
		dp_invalidate_trigger(dp, ui);
		return 1;
	}
	return 0;
}

void UiDatePicker_OnIme(UiDatePicker *dp, UiContext *ui)
{
	if (!dp || !ui || dp->editActive < 0) return;
	dp_invalidate_trigger(dp, ui);
}

void UiDatePicker_Blink(UiDatePicker *dp, UiContext *ui)
{
	if (!dp || !ui || dp->editActive < 0) return;
	dp->blinkOn = !dp->blinkOn;
	dp_invalidate_trigger(dp, ui);
}