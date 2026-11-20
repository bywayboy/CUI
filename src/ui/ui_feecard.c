/*
 * ui_feecard.c - 费率卡片: 标量表单 + 7x24 小时价网格
 */
#include <windows.h>
#include "ui/ui_feecard.h"
#include "ui/ui_widgets.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);

#define FC_HEAD_H     50.0f
#define FC_PAD        14.0f
#define FC_SCALAR_H   34.0f
#define FC_GRID_HEAD  30.0f
#define FC_GRID_ROW   40.0f
#define FC_GRID_H     (FC_GRID_HEAD + UI_FEE_CARD_DAYS * FC_GRID_ROW)
#define FC_LABEL_H    32.0f
#define FC_TAG_H      20.0f
#define FC_GAP        8.0f
#define FC_SEC_GAP    6.0f

static const char *const kDayNames[UI_FEE_CARD_DAYS] = {
	"周一", "周二", "周三", "周四", "周五", "周六", "周日"
};
static const char *const kFieldLabels[5] = {
	"起步价", "最小收费", "忽略时间", "计费单位", "折扣"
};
static const char *const kFieldUnits[5] = { "元", "元", "分钟", "", "%" };
static const int kUnitChoices[] = { 1, 5, 10, 15, 20, 30, 60 };

static UiId fc_id(UiFeeCard *c, int kind, int idx)
{
	return Ui_IdIndex("cui.feecard", (int)(c->id & 0x7FFFFFFF),
		(int)((c->id >> 32) & 0x7FFFFFFF), kind * 16 + idx);
}

static void fc_copy(char *d, int cap, const char *s)
{
	int i = 0;
	if (!d || cap <= 0) return;
	if (s) {
		while (s[i] && i < cap - 1) {
			d[i] = s[i];
			i++;
		}
	}
	d[i] = 0;
}

/* ── 数值/格式 ─────────────────────────────────────────────────────── */

static int fc_parse_money(const char *s)
{
	int whole = 0, frac = 0, i = 0, n = 0, dot = 0, round = 0;
	if (!s) return 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	while (s[i] >= '0' && s[i] <= '9') {
		whole = whole * 10 + (s[i] - '0');
		if (whole > 10000000) whole = 10000000;
		i++;
	}
	if (s[i] == '.') {
		dot = 1;
		i++;
	}
	if (dot) {
		if (s[i] >= '0' && s[i] <= '9') { frac += (s[i] - '0') * 10; i++; n++; }
		if (s[i] >= '0' && s[i] <= '9') { frac += (s[i] - '0'); i++; n++; }
		if (s[i] >= '5' && s[i] <= '9') round = 1;
	}
	(void)n;
	return whole * 100 + frac + round;
}

static int fc_parse_int(const char *s)
{
	int v = 0, i = 0;
	if (!s) return 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	while (s[i] >= '0' && s[i] <= '9') {
		v = v * 10 + (s[i] - '0');
		if (v > 1000000) v = 1000000;
		i++;
	}
	return v;
}

static void fc_fmt_input(int fen, char *out, int cap)
{
	if (fen < 0) fen = 0;
	sprintf(out, "%d.%02d", fen / 100, fen % 100);
	(void)cap;
}

static void fc_fmt_grid(int fen, char *out, int cap)
{
	if (fen < 0) fen = 0;
	if (fen % 100 == 0) sprintf(out, "%d", fen / 100);
	else if (fen % 10 == 0) sprintf(out, "%d.%d", fen / 100, (fen % 100) / 10);
	else sprintf(out, "%d.%02d", fen / 100, fen % 100);
	(void)cap;
}

/* ── 模型 ───────────────────────────────────────────────────────────── */

static void fc_defaults(UiFeeModel *m)
{
	int i;
	ZeroMemory(m, sizeof(*m));
	m->minPrice = 100;
	m->ignoreTime = 3;
	m->timeUnit = 1;
	m->discount = 100;
	for (i = 0; i < UI_FEE_CARD_LEN; i++) m->prices[i].price = 0;
}

static int fc_same(const UiFeeModel *a, const UiFeeModel *b)
{
	int i;
	if (!a || !b) return 0;
	if (a->startPrice != b->startPrice || a->minPrice != b->minPrice ||
	    a->ignoreTime != b->ignoreTime || a->timeUnit != b->timeUnit ||
	    a->discount != b->discount)
		return 0;
	for (i = 0; i < UI_FEE_CARD_LEN; i++) {
		if (a->prices[i].price != b->prices[i].price ||
		    a->prices[i].pack != b->prices[i].pack ||
		    a->prices[i].packPrice != b->prices[i].packPrice)
			return 0;
	}
	return 1;
}

static void fc_build_unit_options(UiFeeCard *c)
{
	int vals[9], n = 0, i, j;
	for (i = 0; i < (int)(sizeof(kUnitChoices) / sizeof(kUnitChoices[0])); i++)
		vals[n++] = kUnitChoices[i];
	if (c->form.timeUnit >= 1) {
		for (i = 0; i < n; i++)
			if (vals[i] == c->form.timeUnit) break;
		if (i == n && n < 9) vals[n++] = c->form.timeUnit;
	}
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (vals[j] < vals[i]) { int t = vals[i]; vals[i] = vals[j]; vals[j] = t; }
	for (i = 0; i < n; i++) {
		sprintf(c->unitLabels[i], "%d 分钟", vals[i]);
		c->unitItems[i] = c->unitLabels[i];
		c->unitValues[i] = vals[i];
	}
	c->unitCount = n;
	UiSelect_SetItems(&c->unit, c->unitItems, c->unitCount);
	for (i = 0; i < n; i++)
		if (vals[i] == c->form.timeUnit) break;
	UiSelect_SetSelected(&c->unit, i < n ? i : 0);
}

static void fc_sync_money_fields(UiFeeCard *c)
{
	char b[32];
	if (!c->inited) return;
	fc_fmt_input(c->form.startPrice, b, (int)sizeof(b));
	Edit_SetTextUtf8(UiInput_Edit(&c->fields, 0), b);
	fc_fmt_input(c->form.minPrice, b, (int)sizeof(b));
	Edit_SetTextUtf8(UiInput_Edit(&c->fields, 1), b);
	sprintf(b, "%d", c->form.ignoreTime > 0 ? c->form.ignoreTime : 0);
	Edit_SetTextUtf8(UiInput_Edit(&c->fields, 2), b);
	sprintf(b, "%d", c->form.discount);
	Edit_SetTextUtf8(UiInput_Edit(&c->fields, 3), b);
}

static void fc_sync_inputs(UiFeeCard *c)
{
	if (!c->inited) return;
	fc_build_unit_options(c);
	fc_sync_money_fields(c);
}

/* 读回四个输入框; 返回 1 = 表单发生变化. */
static int fc_commit_inputs(UiFeeCard *c)
{
	char b[32];
	int v, changed = 0;
	if (!c->inited) return 0;
	Edit_TextUtf8(UiInput_Edit(&c->fields, 0), b, (int)sizeof(b));
	v = fc_parse_money(b);
	if (v < 0) v = 0;
	if (v != c->form.startPrice) { c->form.startPrice = v; changed = 1; }
	Edit_TextUtf8(UiInput_Edit(&c->fields, 1), b, (int)sizeof(b));
	v = fc_parse_money(b);
	if (v < 1) v = 1;
	if (v != c->form.minPrice) { c->form.minPrice = v; changed = 1; }
	Edit_TextUtf8(UiInput_Edit(&c->fields, 2), b, (int)sizeof(b));
	v = fc_parse_int(b);
	if (v < 0) v = 0;
	if (v > 1440) v = 1440;
	if (v != c->form.ignoreTime) { c->form.ignoreTime = v; changed = 1; }
	Edit_TextUtf8(UiInput_Edit(&c->fields, 3), b, (int)sizeof(b));
	v = fc_parse_int(b);
	if (v < 1) v = 1;
	if (v > 100) v = 100;
	if (v != c->form.discount) { c->form.discount = v; changed = 1; }
	if (changed) c->dirty = 1;
	return changed;
}

static void fc_ensure(UiFeeCard *c, UiContext *ui)
{
	if (!c || !ui || c->inited) return;
	UiInput_Init(&c->fields, ui, c->ime);
	UiInput_Add(&c->fields, c->fieldId[0], 12, 0, "0123456789.", L"0.00");
	UiInput_Add(&c->fields, c->fieldId[1], 12, 0, "0123456789.", L"0.00");
	UiInput_Add(&c->fields, c->fieldId[2], 6, 0, "0123456789", L"0");
	UiInput_Add(&c->fields, c->fieldId[3], 4, 0, "0123456789", L"100");

	UiInput_Init(&c->gridFields, ui, c->ime);
	UiInput_Add(&c->gridFields, c->gridFieldId[0], 12, 0, "0123456789.", L"0.00");
	UiInput_Add(&c->gridFields, c->gridFieldId[1], 12, 0, "0123456789.", L"0.00");

	c->inited = 1;
	fc_sync_inputs(c);
}

/* ── 公共状态 ───────────────────────────────────────────────────────── */

void UiFeeCard_Init(UiFeeCard *c, UiId id, UiPopupManager *pm, UiIme *ime)
{
	if (!c) return;
	ZeroMemory(c, sizeof(*c));
	c->id = id ? id : Ui_Id("cui.feecard", 0, 0);
	c->pm = pm;
	c->ime = ime;
	fc_copy(c->title, (int)sizeof(c->title), "");
	c->fieldId[0] = fc_id(c, 1, 0);
	c->fieldId[1] = fc_id(c, 1, 1);
	c->fieldId[2] = fc_id(c, 1, 2);
	c->fieldId[3] = fc_id(c, 1, 3);
	c->unitId = fc_id(c, 2, 0);
	c->gridId = fc_id(c, 3, 0);
	c->saveId = fc_id(c, 4, 0);
	c->delId = fc_id(c, 5, 0);
	c->gridFieldId[0] = fc_id(c, 6, 0);
	c->gridFieldId[1] = fc_id(c, 6, 1);
	c->packId = fc_id(c, 7, 0);
	c->gridOkId = fc_id(c, 8, 0);
	c->gridCancelId = fc_id(c, 8, 1);
	UiSelect_Init(&c->unit, c->unitId);
	UiDialog_Init(&c->gridDlg, fc_id(c, 9, 0));
	UiDialog_AttachIme(&c->gridDlg, ime);
	UiDialog_SetFocusOrder(&c->gridDlg, c->gridFieldId, 2);
	UiDialog_Init(&c->confirmDlg, fc_id(c, 10, 0));
	UiSwitch_Init(&c->pack, c->packId, 0);
	fc_defaults(&c->form);
	fc_defaults(&c->src);
}

void UiFeeCard_Shutdown(UiFeeCard *c)
{
	if (!c) return;
	if (c->inited) {
		UiInput_Shutdown(&c->fields);
		UiInput_Shutdown(&c->gridFields);
		UiSelect_Shutdown(&c->unit);
		c->inited = 0;
	}
}

void UiFeeCard_SetInfo(UiFeeCard *c, const UiFeeCardInfo *info)
{
	if (!c || !info) return;
	fc_copy(c->title, (int)sizeof(c->title), info->title);
	fc_copy(c->updatedAt, (int)sizeof(c->updatedAt), info->updatedAt);
	c->hasFee = info->hasFee;
	c->active = info->active;
	c->refCount = info->refCount;
	c->canCreate = info->canCreate;
	c->canUpdate = info->canUpdate;
	c->canDelete = info->canDelete;
	c->saving = info->saving;
}

void UiFeeCard_SetCallbacks(UiFeeCard *c, const UiFeeCardCallbacks *cb, void *user)
{
	if (!c) return;
	if (cb) c->cb = *cb;
	else ZeroMemory(&c->cb, sizeof(c->cb));
	c->cbUser = user;
}

void UiFeeCard_SetFee(UiFeeCard *c, const UiFeeModel *fee)
{
	if (!c) return;
	c->hasSrc = fee != NULL;
	if (fee) c->src = *fee;
	else fc_defaults(&c->src);
	if (!fee || !c->dirty || fc_same(&c->form, fee)) {
		c->form = fee ? *fee : c->src;
		c->dirty = 0;
		fc_sync_inputs(c);
	}
}

void UiFeeCard_Reset(UiFeeCard *c)
{
	if (!c) return;
	if (c->hasSrc) {
		c->form = c->src;
	} else {
		fc_defaults(&c->src);
		c->form = c->src;
	}
	c->dirty = 0;
	fc_sync_inputs(c);
}

const UiFeeModel *UiFeeCard_Form(const UiFeeCard *c)
{
	return c ? &c->form : NULL;
}

int UiFeeCard_IsDirty(const UiFeeCard *c)
{
	return c ? c->dirty : 0;
}

int UiFeeCard_Validate(const UiFeeCard *c, const char **message)
{
	int i;
	const char *msg = NULL;
	if (!c) return 0;
	if (c->form.minPrice < 1) msg = "最小收费至少 1 分";
	else if (c->form.discount < 1 || c->form.discount > 100)
		msg = "折扣须在 1~100 之间";
	else if (c->form.timeUnit < 1) msg = "计费单位至少 1 分钟";
	else if (c->form.ignoreTime < 0) msg = "忽略时间不能为负";
	else {
		for (i = 0; i < UI_FEE_CARD_LEN; i++) {
			if (c->form.prices[i].price < 0) { msg = "小时单价不能为负"; break; }
			if (c->form.prices[i].pack && c->form.prices[i].packPrice < 0) {
				msg = "封顶价格不能为负";
				break;
			}
		}
	}
	if (message) *message = msg;
	return msg == NULL;
}

float UiFeeCard_MeasureHeight(float width)
{
	float scalar = width < 720.0f ? FC_SCALAR_H * 2.0f + FC_GAP : FC_SCALAR_H;
	return FC_HEAD_H + FC_PAD + scalar + FC_SEC_GAP + FC_LABEL_H + FC_GRID_H +
		10.0f + 21.0f + FC_PAD;
}

/* ── 绘制辅助 ───────────────────────────────────────────────────────── */

static float fc_tag(Gfx *g, UiTheme *t, float x, float y, const char *text,
	int kind)
{
	float tw = Gfx_TextWidth(g, t->fMini, text);
	float w = tw + 14.0f;
	UiRect rc = Ui_Rect(x, y, w, FC_TAG_H);
	UiColor bg = 0, border = t->neutral.border, fg = t->text2;
	if (kind == 1) {
		bg = t->success.soft;
		border = t->success.border;
		fg = t->success.text;
	} else if (kind == 2) {
		bg = t->neutral.soft;
		border = t->neutral.border;
		fg = t->neutral.text;
	} else if (kind == 3) {
		border = t->warning.border;
		fg = t->warning.base;
	}
	if (bg) Gfx_FillRoundRect(g, rc, 4.0f, bg);
	Gfx_StrokeRoundRect(g, rc, 4.0f, border, 1.0f);
	Gfx_Text(g, t->fMini, text, rc, fg,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	return w;
}

static void fc_draw_header(UiFeeCard *c, UiContext *ui, UiRect rc,
	int *saveClicked)
{
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	UiRect head = Ui_Rect(rc.x, rc.y, rc.w, FC_HEAD_H);
	float x = rc.x + FC_PAD;
	float right = rc.x + rc.w - FC_PAD;
	float saveW = 120.0f, delW = 48.0f;
	float saveX = right - saveW;
	float delX = saveX - FC_GAP - delW;
	float tagRight = (c->hasFee && c->canDelete) ? delX - FC_GAP : saveX - FC_GAP;
	float titleW, tagW = 0.0f, tx;
	int canSave = c->hasFee ? c->canUpdate : c->canCreate;

	Gfx_FillRect(g, head, Ui_ColorMix(t->card, t->frame, 0.55f));
	Gfx_FillRect(g, Ui_Rect(rc.x, rc.y + FC_HEAD_H - 1.0f, rc.w, 1.0f),
		t->border);

	/* 标签宽度先算, 标题最多占剩余空间 */
	tagW += Gfx_TextWidth(g, t->fMini, c->hasFee
		? (c->active ? "已配置" : "已归档") : "未配置") + 14.0f;
	if (c->hasFee && c->refCount > 0)
		tagW += 8.0f + Gfx_TextWidth(g, t->fMini, "被订单引用") + 14.0f;
	titleW = tagRight - x - (tagW > 0.0f ? tagW + FC_GAP : 0.0f);
	if (titleW < 24.0f) titleW = 24.0f;
	Gfx_Text(g, t->fHead, c->title, Ui_Rect(x, rc.y, titleW, FC_HEAD_H),
		t->text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	tx = x + titleW + (tagW > 0.0f ? FC_GAP : 0.0f);
	if (tx < tagRight) {
		tx += fc_tag(g, t, tx, rc.y + (FC_HEAD_H - FC_TAG_H) * 0.5f,
			c->hasFee ? (c->active ? "已配置" : "已归档") : "未配置",
			c->hasFee ? (c->active ? 1 : 2) : 2) + FC_GAP;
		if (c->hasFee && c->refCount > 0 && tx < tagRight)
			fc_tag(g, t, tx, rc.y + (FC_HEAD_H - FC_TAG_H) * 0.5f,
				"被订单引用", 3);
	}

	if (c->hasFee && c->canDelete) {
		UiButtonStyle st = UiButtonStyle_Make(UI_BTYPE_ERROR,
			UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
		if (Ui_ButtonEx(ui, c->delId, Ui_Rect(delX, rc.y + 11.0f,
				delW, 28.0f), "删除", &st, !c->saving))
			UiDialog_Open(&c->confirmDlg, ui, c->delId);
	}
	if (canSave) {
		UiButtonStyle st = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
			UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
		const char *label = c->hasFee
			? (c->saving ? "保存中…" : "保存")
			: (c->saving ? "创建中…" : "创建费率");
		if (Ui_ButtonEx(ui, c->saveId, Ui_Rect(saveX, rc.y + 11.0f,
			saveW, 28.0f), label, &st, !c->saving))
			*saveClicked = 1;
	}
}

static int fc_draw_field(UiFeeCard *c, UiContext *ui, float x, float y,
	float w, int idx)
{
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	float lw = Gfx_TextWidth(g, t->fSmall, kFieldLabels[idx]);
	float uw = kFieldUnits[idx][0]
		? Gfx_TextWidth(g, t->fSmall, kFieldUnits[idx]) : 0.0f;
	float ix = x + lw + 6.0f;
	float iw = w - lw - 6.0f - (uw > 0.0f ? uw + 4.0f : 0.0f);
	UiRect ir = Ui_Rect(ix, y + 1.0f, iw > 16.0f ? iw : 16.0f, 30.0f);
	if (iw < 16.0f) iw = 16.0f;

	Gfx_Text(g, t->fSmall, kFieldLabels[idx],
		Ui_Rect(x, y, lw, 32.0f), t->text2, GFX_TEXT_VCENTER);
	if (idx == 3) {
		if (Ui_Select(&c->unit, c->pm, ui, ir, NULL, 1)) {
			int si = c->unit.selected;
			if (si >= 0 && si < c->unitCount &&
			    c->unitValues[si] >= 1 &&
			    c->unitValues[si] != c->form.timeUnit) {
				c->form.timeUnit = c->unitValues[si];
				c->dirty = 1;
				return 1;
			}
		}
	} else {
		int fi = idx < 3 ? idx : 3;   /* 折扣映射到第 4 个编辑框 */
		UiInput_SetRect(&c->fields, fi, ir);
		UiInput_Draw(&c->fields, fi);
	}
	if (uw > 0.0f)
		Gfx_Text(g, t->fSmall, kFieldUnits[idx],
			Ui_Rect(ir.x + ir.w + 4.0f, y, uw, 32.0f),
			t->text4, GFX_TEXT_VCENTER);
	return 0;
}

static float fc_draw_scalars(UiFeeCard *c, UiContext *ui, float x, float y,
	float w)
{
	int two = w < 720.0f;
	int row, fi;
	float yy = y;
	float gap = 10.0f;
	int counts[2] = { 3, 2 };

	if (!UiDialog_IsOpen(&c->gridDlg) && !UiDialog_IsOpen(&c->confirmDlg))
		UiInput_BeginFrame(&c->fields);

	for (row = 0; row < (two ? 2 : 1); row++) {
		int count = two ? counts[row] : 5;
		float cw = (w - gap * (count - 1)) / count;
		int first = two ? (row == 0 ? 0 : 3) : 0;
		int i;
		for (i = 0; i < count; i++) {
			fi = first + i;
			if (fc_draw_field(c, ui, x + i * (cw + gap), yy, cw, fi))
				c->changedThisFrame = 1;
		}
		yy += FC_SCALAR_H + gap;
	}
	return two ? FC_SCALAR_H * 2.0f + gap : FC_SCALAR_H;
}

/* ── 网格 ───────────────────────────────────────────────────────────── */

static void fc_select_one(UiFeeCard *c, int idx)
{
	ZeroMemory(c->selected, sizeof(c->selected));
	c->selected[idx] = 1;
	c->selCount = 1;
	c->selMin = c->selMax = idx;
}

static void fc_select_rect(UiFeeCard *c, int a, int b)
{
	int r1 = a / UI_FEE_CARD_HOURS, c1 = a % UI_FEE_CARD_HOURS;
	int r2 = b / UI_FEE_CARD_HOURS, c2 = b % UI_FEE_CARD_HOURS;
	int r, col, r0, rn, c0, cn;
	ZeroMemory(c->selected, sizeof(c->selected));
	c->selCount = 0;
	r0 = r1 < r2 ? r1 : r2;
	rn = r1 > r2 ? r1 : r2;
	c0 = c1 < c2 ? c1 : c2;
	cn = c1 > c2 ? c1 : c2;
	for (r = r0; r <= rn; r++) {
		for (col = c0; col <= cn; col++) {
			c->selected[r * UI_FEE_CARD_HOURS + col] = 1;
			c->selCount++;
		}
	}
	c->selMin = r0 * UI_FEE_CARD_HOURS + c0;
	c->selMax = rn * UI_FEE_CARD_HOURS + cn;
}

static int fc_cell_at(UiFeeCard *c, float x, float y)
{
	float gx = c->gridRect.x + c->gridDayW;
	float gy = c->gridRect.y + c->gridHeadH;
	int col, row;
	if (x < gx || y < gy || x >= c->gridRect.x + c->gridRect.w ||
	    y >= c->gridRect.y + c->gridRect.h)
		return -1;
	col = (int)((x - gx) / c->gridCellW);
	row = (int)((y - gy) / c->gridRowH);
	if (col < 0) col = 0;
	if (col >= UI_FEE_CARD_HOURS) col = UI_FEE_CARD_HOURS - 1;
	if (row < 0) row = 0;
	if (row >= UI_FEE_CARD_DAYS) row = UI_FEE_CARD_DAYS - 1;
	return row * UI_FEE_CARD_HOURS + col;
}

static void fc_open_grid_dialog(UiFeeCard *c, UiContext *ui)
{
	int idx;
	char b[32];
	if (!c || !c->selCount || UiDialog_IsOpen(&c->gridDlg)) return;
	idx = c->selMin;
	fc_fmt_input(c->form.prices[idx].price, b, (int)sizeof(b));
	Edit_SetTextUtf8(UiInput_Edit(&c->gridFields, 0), b);
	fc_fmt_input(c->form.prices[idx].packPrice, b, (int)sizeof(b));
	Edit_SetTextUtf8(UiInput_Edit(&c->gridFields, 1), b);
		c->pack.on = c->form.prices[idx].pack;
		c->pack.changed = 0;
		if (UiDialog_Open(&c->gridDlg, ui, c->gridId)) {
			c->lastClickCell = 0;
			c->lastClickMs = 0;
			c->gridFields.focus = 0;
			c->gridFields.blinkOn = 1;
			if (c->ime) UiIme_SetFocus(c->ime, UiInput_Edit(&c->gridFields, 0));
			Ui_SetFocusId(ui, c->gridFieldId[0]);
		}
	}

static void fc_single_cell_click(UiFeeCard *c, UiContext *ui, int idx)
{
	uint64_t now = GetTickCount64();
	int key = idx + 1;
	if (c->lastClickCell == key && now - c->lastClickMs <= 450) {
		c->lastClickCell = 0;
		c->lastClickMs = 0;
		fc_open_grid_dialog(c, ui);
		return;
	}
	c->lastClickCell = key;
	c->lastClickMs = now;
}

static void fc_draw_grid(UiFeeCard *c, UiContext *ui, float x, float y,
	float w)
{
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	UiWidgetState st;
	UiRect gr = Ui_Rect(x, y, w, FC_GRID_H);
	float gx, gy, cw, rh;
	int row, col;
	int hoverCell = -1;

	c->gridRect = gr;
	c->gridDayW = 40.0f;
	c->gridHeadH = FC_GRID_HEAD;
	c->gridRowH = FC_GRID_ROW;
	c->gridCellW = (w - c->gridDayW) / UI_FEE_CARD_HOURS;
	gx = x + c->gridDayW;
	gy = y + FC_GRID_HEAD;
	cw = c->gridCellW;
	rh = FC_GRID_ROW;

	st = Ui_State(ui, c->gridId, gr, 1);
	Ui_SetPressPoll(ui, c->gridId, 1);
	Ui_SetCursor(ui, c->gridId, UI_CURSOR_HAND);

	if (!UiDialog_IsOpen(&c->gridDlg) && st.hovered)
		hoverCell = fc_cell_at(c, ui->mouseX, ui->mouseY);

	if (!UiDialog_IsOpen(&c->gridDlg) && ui->down == c->gridId &&
	    ui->mouseDown) {
		int idx = fc_cell_at(c, ui->mouseX, ui->mouseY);
		if (idx >= 0) {
			c->dragActive = 1;
			c->dragMoved = 0;
			c->dragStart = c->dragCur = idx;
			fc_select_one(c, idx);
		}
	} else if (!UiDialog_IsOpen(&c->gridDlg) && !c->dragActive &&
	    ui->clicked == c->gridId) {
		/* 按下/抬起落在同一帧的快速单击 */
		int idx = fc_cell_at(c, ui->mouseX, ui->mouseY);
		if (idx >= 0) {
			fc_select_one(c, idx);
			fc_single_cell_click(c, ui, idx);
		}
	}
	if (c->dragActive) {
		if (ui->mouseDown) {
			int idx = fc_cell_at(c, ui->mouseX, ui->mouseY);
			if (idx >= 0 && idx != c->dragCur) {
				c->dragCur = idx;
				c->dragMoved = 1;
				fc_select_rect(c, c->dragStart, idx);
			}
		} else {
			c->dragActive = 0;
			if (c->dragMoved || c->selCount > 1)
				fc_open_grid_dialog(c, ui);
			else
				fc_single_cell_click(c, ui, c->dragStart);
		}
	}

	/* 表头 */
	for (col = 0; col < UI_FEE_CARD_HOURS + 1; col++) {
		UiRect hr;
		char b[8];
		if (col == 0) hr = Ui_Rect(x, y, c->gridDayW, FC_GRID_HEAD);
		else hr = Ui_Rect(gx + (col - 1) * cw, y, cw, FC_GRID_HEAD);
		Gfx_FillRect(g, hr, col == 0 ? t->primary.press : t->primary.base);
		Gfx_StrokeRoundRect(g, hr, 0.0f, t->border, 1.0f);
		if (col == 0) {
			Gfx_Text(g, t->fMini, "周/时", hr, t->primary.text,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		} else {
			sprintf(b, "%d", col - 1);
			Gfx_Text(g, t->fSmall, b, hr, t->primary.text,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		}
	}
	for (row = 0; row < UI_FEE_CARD_DAYS; row++) {
		UiRect dr = Ui_Rect(x, gy + row * rh, c->gridDayW, rh);
		Gfx_FillRect(g, dr, t->primary.press);
		Gfx_StrokeRoundRect(g, dr, 0.0f, t->border, 1.0f);
		Gfx_Text(g, t->fSmall, kDayNames[row], dr, t->primary.text,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}

	/* 单元格 */
	for (row = 0; row < UI_FEE_CARD_DAYS; row++) {
		for (col = 0; col < UI_FEE_CARD_HOURS; col++) {
			int idx = row * UI_FEE_CARD_HOURS + col;
			UiFeePrice *p = &c->form.prices[idx];
			UiRect cr = Ui_Rect(gx + col * cw, gy + row * rh, cw, rh);
			UiColor bg = Ui_ColorMix(t->card, t->primary.soft, 0.55f);
			UiColor fg = t->text;
			char up[32], dn[32];
			if (p->pack) {
				bg = t->warning.base;
				fg = t->btnPrimaryText;
			}
			if (idx == hoverCell) bg = Ui_ColorMix(bg, t->primary.base, 0.12f);
			if (c->selected[idx]) {
				if (p->pack) {
					/* 选中不覆盖语义色: 封顶保持橙色, 只做明暗一档 */
					bg = t->themeId == UI_THEME_DARK
						? Ui_ColorMix(bg, 0xFFFFFFFFu, 0.12f)
						: Ui_ColorMix(bg, 0xFF000000u, 0.16f);
					fg = t->btnPrimaryText;
				} else {
					bg = Ui_ColorMix(bg, t->primary.base, 0.30f);
					fg = t->text;
				}
			}
			Gfx_FillRect(g, cr, bg);
			Gfx_StrokeRoundRect(g, cr, 0.0f, t->border, 1.0f);
			if (p->pack) {
				fc_fmt_grid(p->packPrice, up, (int)sizeof(up));
				fc_fmt_grid(p->price, dn, (int)sizeof(dn));
				Gfx_Text(g, t->fMini, up,
					Ui_Rect(cr.x, cr.y + 3.0f, cr.w, 14.0f), fg,
					GFX_TEXT_CENTER | GFX_TEXT_ELLIPSIS);
				Gfx_Text(g, t->fSmall, dn,
					Ui_Rect(cr.x, cr.y + 19.0f, cr.w, 16.0f), fg,
					GFX_TEXT_CENTER | GFX_TEXT_ELLIPSIS);
			} else {
				fc_fmt_grid(p->price, up, (int)sizeof(up));
				Gfx_Text(g, t->fSmall, up, cr, fg,
					GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			}
		}
	}

	/* 选中范围虚线框: 中性色, 避免蓝框压住橙色封顶格 */
	if (c->selCount > 0) {
		int r0 = c->selMin / UI_FEE_CARD_HOURS, c0 = c->selMin % UI_FEE_CARD_HOURS;
		int r1 = c->selMax / UI_FEE_CARD_HOURS, c1 = c->selMax % UI_FEE_CARD_HOURS;
		UiRect box = Ui_Rect(gx + c0 * cw, gy + r0 * rh,
			(c1 - c0 + 1) * cw, (r1 - r0 + 1) * rh);
		Gfx_StrokeRoundRectCornersDashed(g, box, Gfx_Corners(0, 0, 0, 0),
			Ui_ColorAlpha(t->text, 190), 1.2f);
	}
}

/* ── 对话框 ─────────────────────────────────────────────────────────── */

static void fc_grid_apply(UiFeeCard *c)
{
	char b[32];
	int price, cap, pack, i;
	if (!c->selCount) return;
	Edit_TextUtf8(UiInput_Edit(&c->gridFields, 0), b, (int)sizeof(b));
	price = fc_parse_money(b);
	if (price < 0) price = 0;
	Edit_TextUtf8(UiInput_Edit(&c->gridFields, 1), b, (int)sizeof(b));
	cap = fc_parse_money(b);
	if (cap < 0) cap = 0;
	pack = c->pack.on ? 1 : 0;
	if (!pack) cap = 0;
	for (i = 0; i < UI_FEE_CARD_LEN; i++) {
		if (!c->selected[i]) continue;
		c->form.prices[i].price = price;
		c->form.prices[i].pack = pack;
		c->form.prices[i].packPrice = cap;
	}
	c->dirty = 1;
}

static void fc_draw_grid_dialog(UiFeeCard *c, UiContext *ui)
{
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	UiRect body;
	char hint[96];

	if (UiDialog_Begin(&c->gridDlg, ui, "小时价格设置", 430.0f, 330.0f, &body)) {
		float x = body.x, y = body.y, w = body.w;
		float labelW = 82.0f, unitW = 20.0f;
		float iw = w - labelW - unitW - 10.0f;
		UiRect ir = Ui_Rect(x + labelW, y + 8.0f, iw, 32.0f);
		UiInput_BeginFrame(&c->gridFields);
		Gfx_Text(g, t->fSmall, "小时单价", Ui_Rect(x, y + 8.0f, labelW, 32.0f),
			t->text2, GFX_TEXT_VCENTER);
		UiInput_SetRect(&c->gridFields, 0, ir);
		UiInput_Draw(&c->gridFields, 0);
		Gfx_Text(g, t->fSmall, "元", Ui_Rect(ir.x + ir.w + 4.0f, y + 8.0f,
			unitW, 32.0f), t->text4, GFX_TEXT_VCENTER);

		Gfx_Text(g, t->fSmall, "包时封顶", Ui_Rect(x, y + 58.0f, labelW, 32.0f),
			t->text2, GFX_TEXT_VCENTER);
		if (Ui_Switch(&c->pack, ui, Ui_Rect(x + labelW, y + 58.0f, 56.0f, 28.0f),
			NULL, 1) && !c->pack.on) {
			Edit_SetTextUtf8(UiInput_Edit(&c->gridFields, 1), "0.00");
		}

		ir = Ui_Rect(x + labelW, y + 106.0f, iw, 32.0f);
		Gfx_Text(g, t->fSmall, "封顶价格", Ui_Rect(x, y + 106.0f,
			labelW, 32.0f), c->pack.on ? t->text2 : t->text4,
			GFX_TEXT_VCENTER);
		UiInput_SetRect(&c->gridFields, 1, ir);
		UiInput_Draw(&c->gridFields, 1);
		Gfx_Text(g, t->fSmall, "元", Ui_Rect(ir.x + ir.w + 4.0f, y + 106.0f,
			unitW, 32.0f), t->text4, GFX_TEXT_VCENTER);

		if (c->selCount > 1) {
			sprintf(hint, "当前选中 %d 个时段，保存后统一应用", c->selCount);
			Gfx_Text(g, t->fSmall, hint,
				Ui_Rect(x, y + 156.0f, w, 20.0f), t->warning.base,
				GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		}

		if (Ui_Button(ui, c->gridCancelId,
				Ui_Rect(body.x + body.w - 200.0f,
					body.y + body.h - 38.0f, 88.0f, 34.0f),
				"取消", UI_BTN_SECONDARY, 1))
			UiDialog_Close(&c->gridDlg, ui, UI_DIALOG_CANCEL);
		if (Ui_Button(ui, c->gridOkId,
				Ui_Rect(body.x + body.w - 100.0f,
					body.y + body.h - 38.0f, 100.0f, 34.0f),
				"确定", UI_BTN_PRIMARY, 1))
			UiDialog_Close(&c->gridDlg, ui, UI_DIALOG_ACCEPT);
		UiDialog_End(&c->gridDlg, ui);
	}
}

void UiFeeCard_DrawOverlay(UiFeeCard *c, UiContext *ui)
{
	int r;
	if (!c || !ui) return;
	if (c->pm) UiSelect_DrawPopup(&c->unit, c->pm, ui);

	if (UiDialog_IsOpen(&c->gridDlg))
		fc_draw_grid_dialog(c, ui);
	/* 结果必须在关闭动画结束后仍消费; 此时 IsOpen 已为 0. */
	r = UiDialog_TakeResult(&c->gridDlg);
	if (r != UI_DIALOG_NONE) {
		if (r == UI_DIALOG_ACCEPT) fc_grid_apply(c);
		c->gridFields.focus = -1;
		ZeroMemory(c->selected, sizeof(c->selected));
		c->selCount = 0;
	}

	if (UiDialog_IsOpen(&c->confirmDlg)) {
		sprintf(c->confirmText, "确定删除「%s」吗？\n\n被订单引用的费率无法删除。",
			c->title);
		Ui_ConfirmDialog(&c->confirmDlg, ui, "提示", c->confirmText,
			"确定删除", "取消", 1);
	}
	r = UiDialog_TakeResult(&c->confirmDlg);
	if (r == UI_DIALOG_ACCEPT && c->cb.delete_)
		c->cb.delete_(c->cbUser, c);
}

void UiFeeCard_Tick(UiFeeCard *c, UiContext *ui, double dtMs)
{
	if (!c || !ui) return;
	UiDialog_Tick(&c->gridDlg, ui, dtMs);
	UiDialog_Tick(&c->confirmDlg, ui, dtMs);
}

/* ── 主绘制 ─────────────────────────────────────────────────────────── */

int Ui_FeeCard(UiFeeCard *c, UiContext *ui, UiRect rc)
{
	UiTheme *t;
	Gfx *g;
	float x, y, w;
	int ev = 0;
	int saveClicked = 0;

	if (!c || !ui || !ui->theme || !ui->gfx) return 0;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	fc_ensure(c, ui);
	t = ui->theme;
	g = ui->gfx;
	c->changedThisFrame = 0;

	if (fc_commit_inputs(c)) c->changedThisFrame = 1;

	Gfx_FillRoundRect(g, rc, 10.0f, t->card);
	Gfx_StrokeRoundRect(g, Ui_RectInset(rc, 0.5f), 9.5f, t->border, 1.0f);
	fc_draw_header(c, ui, rc, &saveClicked);

	if (saveClicked && !c->saving) {
		const char *msg = NULL;
		if (!UiFeeCard_Validate(c, &msg)) {
			fc_copy(c->error, (int)sizeof(c->error), msg ? msg : "输入不合法");
			if (c->cb.invalid) c->cb.invalid(c->cbUser, c, c->error);
			ev |= UI_FEE_CARD_EV_INVALID;
		} else {
			if (c->cb.save) c->cb.save(c->cbUser, c, &c->form);
			ev |= UI_FEE_CARD_EV_SAVE;
		}
	}

	x = rc.x + FC_PAD;
	y = rc.y + FC_HEAD_H + FC_PAD;
	w = rc.w - FC_PAD * 2.0f;

	fc_draw_scalars(c, ui, x, y, w);
	y += (w < 720.0f ? FC_SCALAR_H * 2.0f + FC_GAP : FC_SCALAR_H) + FC_SEC_GAP;

	/* 网格说明行 */
	{
		float lw = Gfx_TextWidth(g, t->fSmall, "小时单价（元）");
		UiRect lr = Ui_Rect(x, y, lw + 4.0f, FC_LABEL_H);
		UiRect hint = Ui_Rect(x + lw + 8.0f, y,
			w - lw - 8.0f, FC_LABEL_H);
		Gfx_Text(g, t->fSmall, "小时单价（元）", lr, t->text2,
			GFX_TEXT_VCENTER);
		Gfx_Text(g, t->fSmall,
			"起步价=不足一个计费单位按此收 · 最小收费=低于此按此收 · "
			"忽略时间=上机前N分钟免费 · 计费单位=分钟 · 折扣=100无折扣 · "
			"双击单格/拖动框选设置, 橙色=已封顶",
			hint, t->text4, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}

	y += FC_LABEL_H;
	fc_draw_grid(c, ui, x, y, w);
	y += FC_GRID_H + 10.0f;

	if (c->hasFee) {
		char meta[160];
		if (c->refCount > 0)
			sprintf(meta, "更新: %s · 被 %d 个订单引用",
				c->updatedAt, c->refCount);
		else
			sprintf(meta, "更新: %s", c->updatedAt);
		Gfx_Text(g, t->fSmall, meta, Ui_Rect(x, y, w, 20.0f),
			t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}

	/* 失焦格式化: 只回写上一帧聚焦字段, 不碰新点击字段的光标位置 */
	if (c->fields.focus != c->lastFocus && c->lastFocus >= 0) {
		char b[32];
		if (c->lastFocus == 0) {
			fc_fmt_input(c->form.startPrice, b, (int)sizeof(b));
			Edit_SetTextUtf8(UiInput_Edit(&c->fields, 0), b);
		} else if (c->lastFocus == 1) {
			fc_fmt_input(c->form.minPrice, b, (int)sizeof(b));
			Edit_SetTextUtf8(UiInput_Edit(&c->fields, 1), b);
		} else if (c->lastFocus == 2) {
			sprintf(b, "%d", c->form.ignoreTime);
			Edit_SetTextUtf8(UiInput_Edit(&c->fields, 2), b);
		} else if (c->lastFocus == 3) {
			sprintf(b, "%d", c->form.discount);
			Edit_SetTextUtf8(UiInput_Edit(&c->fields, 3), b);
		}
	}
	c->lastFocus = c->fields.focus;

	if (c->changedThisFrame) ev |= UI_FEE_CARD_EV_CHANGED;
	return ev;
}

/* ── 输入转发 ───────────────────────────────────────────────────────── */

static int fc_dialog_open(UiFeeCard *c)
{
	return c && UiDialog_IsOpen(&c->gridDlg);
}

static int fc_text_after_char(UiEdit *e, wchar_t ch, char *out, int cap)
{
	char b[40];
	int i, n = 0, a, z;
	if (!e || !out || cap <= 0) return 0;
	Edit_TextUtf8(e, b, (int)sizeof(b));
	a = e->anchor < e->caret ? e->anchor : e->caret;
	z = e->anchor < e->caret ? e->caret : e->anchor;
	if (a < 0) a = 0;
	if (z > (int)strlen(b)) z = (int)strlen(b);
	for (i = 0; i < a && n < cap - 1; i++) out[n++] = b[i];
	if (n < cap - 1) out[n++] = (char)ch;
	for (i = z; b[i] && n < cap - 1; i++) out[n++] = b[i];
	out[n] = 0;
	return 1;
}

static int fc_char_money(UiInput *in, int field, wchar_t ch)
{
	char b[40];
	int i, dot = -1, before = 0, after = 0;
	UiEdit *e = UiInput_Edit(in, field);
	if (!e) return 0;
	if (ch != L'.' && (ch < L'0' || ch > L'9')) return 1;
	fc_text_after_char(e, ch, b, (int)sizeof(b));
	for (i = 0; b[i]; i++) {
		if (b[i] == '.') {
			if (dot >= 0) return 1;
			dot = i;
			continue;
		}
		if (b[i] < '0' || b[i] > '9') return 1;
		if (dot < 0) before++;
		else after++;
	}
	if (before > 9 || after > 2) return 1;
	return UiInput_Char(in, ch);
}

static int fc_char_int(UiInput *in, int field, wchar_t ch, int maxVal)
{
	char b[16];
	int i, v = 0;
	UiEdit *e = UiInput_Edit(in, field);
	if (!e) return 0;
	if (ch < L'0' || ch > L'9') return 1;
	fc_text_after_char(e, ch, b, (int)sizeof(b));
	for (i = 0; b[i]; i++) {
		if (b[i] < '0' || b[i] > '9') return 1;
		v = v * 10 + (b[i] - '0');
		if (v > maxVal) return 1;
	}
	return UiInput_Char(in, ch);
}

void UiFeeCard_MouseDown(UiFeeCard *c, UiContext *ui, float x, float y)
{
	UiId hit;
	if (!c || !ui || !c->inited) return;
	hit = Ui_HitTest(ui, x, y);
	if (fc_dialog_open(c)) UiInput_MouseDown(&c->gridFields, hit, x);
	else if (!UiDialog_IsOpen(&c->confirmDlg)) UiInput_MouseDown(&c->fields, hit, x);
}

void UiFeeCard_MouseMove(UiFeeCard *c, float x, float y)
{
	(void)y;
	if (!c) return;
	if (fc_dialog_open(c)) UiInput_MouseMove(&c->gridFields, x);
	else UiInput_MouseMove(&c->fields, x);
}

void UiFeeCard_MouseUp(UiFeeCard *c)
{
	if (!c) return;
	if (fc_dialog_open(c)) UiInput_MouseUp(&c->gridFields);
	else UiInput_MouseUp(&c->fields);
}

int UiFeeCard_Key(UiFeeCard *c, UiContext *ui, UINT vk)
{
	UiEdit *e;
	if (!c || !ui || !c->inited) return 0;
	if (UiDialog_IsOpen(&c->confirmDlg))
		return UiDialog_Key(&c->confirmDlg, ui, vk);
	if (fc_dialog_open(c)) {
		e = c->gridFields.focus >= 0
			? UiInput_Edit(&c->gridFields, c->gridFields.focus) : NULL;
		if (e && Edit_HasComposition(e))
			return UiInput_Key(&c->gridFields, ui->hwnd, vk);
		if (vk == VK_ESCAPE) {
			UiDialog_Close(&c->gridDlg, ui, UI_DIALOG_CANCEL);
			return 1;
		}
		if (vk == VK_RETURN) {
			UiDialog_Close(&c->gridDlg, ui, UI_DIALOG_ACCEPT);
			return 1;
		}
		if (!c->pack.on && c->gridFields.focus == 1 &&
		    vk != VK_TAB) return 1;
		return UiInput_Key(&c->gridFields, ui->hwnd, vk);
	}
	if (c->pm && UiPopupManager_IsOpen(c->pm, c->unitId)) return 0;
	return UiInput_Key(&c->fields, ui->hwnd, vk);
}

int UiFeeCard_Char(UiFeeCard *c, wchar_t ch)
{
	int f;
	if (!c || !c->inited) return 0;
	if (c->pm && UiPopupManager_IsOpen(c->pm, c->unitId)) return 0;
	if (fc_dialog_open(c)) {
		f = c->gridFields.focus;
		if (f == 1 && !c->pack.on) return 1;
		if (f == 0 || f == 1) return fc_char_money(&c->gridFields, f, ch);
		return UiInput_Char(&c->gridFields, ch);
	}
	if (UiDialog_IsOpen(&c->confirmDlg)) return 0;
	f = c->fields.focus;
	if (f == 0 || f == 1) return fc_char_money(&c->fields, f, ch);
	if (f == 2) return fc_char_int(&c->fields, f, ch, 1440);
	if (f == 3) return fc_char_int(&c->fields, f, ch, 100);
	return UiInput_Char(&c->fields, ch);
}

void UiFeeCard_OnIme(UiFeeCard *c)
{
	if (!c || !c->inited) return;
	if (fc_dialog_open(c)) UiInput_OnIme(&c->gridFields);
	else UiInput_OnIme(&c->fields);
}

void UiFeeCard_Blink(UiFeeCard *c)
{
	if (!c || !c->inited) return;
	UiInput_Blink(&c->fields);
	UiInput_Blink(&c->gridFields);
}

int UiFeeCard_Busy(const UiFeeCard *c)
{
	if (!c) return 0;
	return UiSelect_Busy(&c->unit) ||
		UiDialog_Busy(&c->gridDlg) || UiDialog_Busy(&c->confirmDlg);
}
