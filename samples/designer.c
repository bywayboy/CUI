/*
 * designer.c - CUI 可视化 UI 设计器
 *
 * .cui 工程: 多窗口 / 虚拟 Dialog / 生成分组; 左侧项目树与组件树,
 * 中间画布, 右侧属性/组件表; 生成 .ui.c/.ui.h.
 */
#include <windows.h>
#include <commdlg.h>
#include <stdarg.h>
#include "ui/ui_app.h"
#include "ui/ui_layout.h"
#include "ui/ui_widgets.h"
#include "ui/ui_controls.h"
#include "ui/ui_input.h"
#include "ui/ui_select.h"
#include "ui/ui_popup.h"
#include "ui/ui_dialog.h"
#include "ui/ui_design.h"
#include "ui/ui_frame.h"
#include "ui/ui_nav.h"
#include "ui/ui_button_group.h"
#include "ui/ui_menu.h"
#include "ui/ui_tooltip.h"

int snprintf(char *buf, size_t cap, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);

#define TOP_H       40.0f
#define PALETTE_H   42.0f
#define STATUS_H    24.0f
#define LEFT_W      250.0f
#define RIGHT_W     330.0f
#define GAP         10.0f
#define ROW_H       24.0f
#define PROP_ROW_H  22.0f
#define PROP_SEC_H  24.0f
#define DW_W        1500
#define DH_H        920

typedef enum {
	DSEL_WINDOW = 0,
	DSEL_VIEW,
	DSEL_GROUP,
	DSEL_NODE,
	DSEL_RADIOGROUP   /* 虚拟单选组行 (元数据, 无节点索引) */
} DSelKind;

enum {
	DROW_NODE = 0,
	DROW_RADIOGROUP
};

enum {
	DPK_TEXT = 0,
	DPK_INT,
	DPK_FLOAT,
	DPK_ENUM,
	DPK_BOOL,
	DPK_READONLY,
	DPK_COLOR,
	DPK_MULTILINE
};

enum {
	DK_W_ID = 1, DK_W_NAME, DK_W_TITLE, DK_W_KIND, DK_W_GROUP,
	DK_W_W, DK_W_H, DK_W_MINW, DK_W_MINH, DK_W_THEME, DK_W_VIEW,
	DK_V_ID = 20, DK_V_NAME, DK_V_W, DK_V_H, DK_V_GROUP,
	DK_G_ID = 40, DK_G_NAME, DK_G_OUTC, DK_G_OUTH, DK_G_SYM,
	DK_N_ID = 80, DK_N_NAME, DK_N_TYPE,
	DK_N_WMODE, DK_N_W, DK_N_HMODE, DK_N_H, DK_N_GROW,
	DK_N_ALIGN, DK_N_TEXTALIGN, DK_N_TEXTVALIGN, DK_N_GAP, DK_N_PADL, DK_N_PADT, DK_N_PADR, DK_N_PADB,
	DK_N_X, DK_N_Y, DK_N_TEXT, DK_N_HINT, DK_N_OPTIONS, DK_N_VALUE,
	DK_N_CHECKED, DK_N_ENABLED, DK_N_VARIANT, DK_N_CLICK, DK_N_CHANGE,
	DK_N_GRIDCOLS, DK_N_GRIDROWS, DK_N_GRIDROW, DK_N_GRIDCOL,
	DK_N_ROWSPAN, DK_N_COLSPAN, DK_N_GRIDROWGAP, DK_N_GRIDCOLGAP,
	DK_N_GRIDCOLTRACKS, DK_N_RADIUS,
	DK_N_MODE, DK_N_READONLY, DK_N_COLOR, DK_N_SELECTED,
	DK_N_ECC, DK_N_BGCOLOR, DK_N_LOGOSRC, DK_N_LOGORATIO, DK_N_QRPAD,
	DK_N_RINGMAX, DK_N_RINGWIDTH, DK_N_PAGECOUNT, DK_N_ADDPAGE,
	DK_N_MARGINL, DK_N_MARGINT, DK_N_MARGINR, DK_N_MARGINB,
	DK_N_RADIOGROUP, DK_N_RADIOSELECTED,
	DK_RG_ID = 200, DK_RG_NAME, DK_RG_ENABLED, DK_RG_SELECTED, DK_RG_CHANGE
};

enum {
	DNA_NEW = 1, DNA_OPEN, DNA_SAVE, DNA_SAVEAS, DNA_GENERATE, DNA_EXIT,
	DNA_UNDO, DNA_REDO, DNA_DUP, DNA_DEL, DNA_UP, DNA_DOWN,
	DNA_ADDWIN, DNA_ADDDLG, DNA_ADDPANEL, DNA_ADDGRP, DNA_GENALL, DNA_GENWIN,
	DNA_DESIGN, DNA_PREVIEW
};

typedef struct DProp {
	int key;
	int kind;
	int sec;
	const char *label;
	char value[160];
	const char *valueRef;
} DProp;

typedef struct DDragItem {
	int idx;
	float x, y;
} DDragItem;

typedef struct DTreeRow {
	UiRect rc;
	int idx;             /* DROW_NODE: 节点索引; DROW_RADIOGROUP: -1 */
	int kind;            /* DROW_* */
	int group;           /* DROW_RADIOGROUP: 单选组索引 */
} DTreeRow;

typedef struct DPreviewTab {
	int node, sel;
} DPreviewTab;

typedef struct DPreviewRadio {
	unsigned uid;        /* 单选组元数据 uid */
	char *selectedRadioId;
} DPreviewRadio;

typedef struct Designer Designer;

typedef struct Designer {
	UiApp *app;
	UiPopupManager pm;
	UiDesignDoc doc;

	int selKind;
	int selWin;          /* 选中宿主 */
	int selView;         /* 当前画布/组件树视图 */
	int selGroup;
	int selNode;
	int selRadioGroup;   /* DSEL_RADIOGROUP: 单选组元数据索引, -1 无 */
	int *selSet;
	int selSetCap, selSetN;

	int dragListN, dragListCap;
	DDragItem *dragList;

	DTreeRow *treeRows;
	int treeRowN, treeRowCap;
	int treeDragIdx;
	int treeDropIdx;
	int treeDropMode;

	int marquee;
	float marqueeX0, marqueeY0, marqueeX1, marqueeY1;
	int snapX, snapY;
	float snapLineX, snapLineY;
	float zoom;
	float panX, panY;
	float viewTx, viewTy;
	UiRect winScreen;
	int panning;
	float panStartX, panStartY, panBaseX, panBaseY;

	int preview;
	UiDesignInstance inst;
	int instReady;
	int previewDialogTried;
	DPreviewTab *previewTabs;
	int previewTabN, previewTabCap;
	DPreviewRadio *previewRadios;
	int previewRadioN, previewRadioCap;

	UiInput propForm;
	UiTextArea propOptionsForm;
	int propField;
	int editActive, editKey, editChanged, editMultiline;
	int propDrag, propDragKey;
	float propDragX, propDragVal;
	int propDragIsInt;
	UiColorSelect propColor;   /* 属性面板颜色行 */
	int propColorReady;
	int propColorNode;
	UiColorSelect propColorBg; /* 二维码背景色行 */
	int propColorBgReady;
	int propColorBgNode;
	UiSelect propRgSelect;     /* Radio 的单选组下拉 */
	int propRgReady;
	int propRgNode;            /* 最近一次绘制下拉的 Radio 节点 */
	int propRgOwner;
	char *propRgPool;          /* 标签池 (UiSelect items 指向它) */
	const char **propRgItems;
	int *propRgMap;            /* 项 -> radioGroups 下标; -1 = 未分组 */
	int propRgN;
	unsigned propRgSig;
	int propRgSigValid;
	int propsTab;          /* 0=属性 1=组件表 */
	int palettePage;
	int propScroll;
	int propSection;
	int secCollapsed[6];
	int propContentH;
	int treeScroll;

	UiNavBar nav;
	UiNavItem navRoot[5];
	UiNavItem navFile[6], navEdit[6], navProj[6], navView[2];
	UiId navAct[24];
	int navActKind[24];
	int navActN;
	UiMenu toolbarMenu;
	UiTooltip toolbarTip;
	UiMenuItem toolbarMenuItems[32];
	char toolbarMenuLabels[32][96];
	int toolbarMenuIdx[32];
	int toolbarMenuN;
	UiButtonGroup constraintW, constraintH, constraintAlign, constraintKind;
	UiButtonGroup multiAlign, multiDist;
	int insertType;

	/* 画布缓存 */
	int layoutReady;
	UiRect winRc;
	UiRect canvasRc;
	UiRect *rects;
	int rectCount;
	float canvasX, canvasY;

	/* 拖拽 */
	int dragging;          /* 0=无 1=移动 2=缩放 */
	int dragHandle;
	float dragStartX, dragStartY;
	float dragNodeX, dragNodeY, dragNodeW, dragNodeH;
	int dragMoved;
	int dragClickNode;
	int dragReorderParent, dragReorderPos;

	/* 撤销 */
	char *undo[40];
	int undoN, undoPos;

	char status[320];
	char error[320];
	WCHAR *lastDir;
	WCHAR *startupFile;

	int genPreview;
	UiDesignGenFile *genFiles;
	int genFileN;
	WCHAR *genDir;
	int titleDirty;

	/* 属性/树行 */
	DProp props[48];
	int propN;
	UiRect propRowRc[48];
} Designer;

static Designer g_d;

static int d_reserve(void **ptr, int *cap, int need, size_t elem)
{
	int next;
	void *p;
	if (need <= *cap) return 1;
	next = *cap > 0 ? *cap : 16;
	while (next < need) {
		if (next > 0x3FFFFFFF) { next = need; break; }
		next *= 2;
	}
	p = *ptr ? HeapReAlloc(GetProcessHeap(), 0, *ptr, (SIZE_T)next * elem)
		: HeapAlloc(GetProcessHeap(), 0, (SIZE_T)next * elem);
	if (!p) return 0;
	*ptr = p;
	*cap = next;
	return 1;
}

static int d_sel_add(int idx)
{
	if (!d_reserve((void **)&g_d.selSet, &g_d.selSetCap,
	    g_d.selSetN + 1, sizeof(*g_d.selSet))) return 0;
	g_d.selSet[g_d.selSetN++] = idx;
	return 1;
}

static void d_prop_apply_text(int key, const char *text);
static float fabsf_local(float v);
static void d_preview_shutdown(void);
static void d_sel_clear(void);
static void d_sel_single(int idx);
static void d_sel_toggle(int idx);
static void d_collect_nodes(const UiDesignDoc *doc, int idx, int *out, int cap,
	int *count);
static int d_validate(char *msg, int cap);
static int d_rg_count(void);
static UiDesignRadioGroup *d_rg(int gi);
static int d_node_group_index(int idx);
static void d_sel_radio_group(int gi);
static void d_rg_prune(void);
static void d_layout_current(void);
static float d_round4(float v);

/* ── 通用辅助 ───────────────────────────────────────────────────────── */

static void d_status(const char *s)
{
	UiDesign_SetText(g_d.status, sizeof(g_d.status), s);
	if (g_d.app) UiApp_Invalidate(g_d.app);
}

static void d_error(const char *s)
{
	UiDesign_SetText(g_d.error, sizeof(g_d.error), s);
	d_status(s);
}

static int d_atoi(const char *s)
{
	int v = 0, sign = 1;
	if (!s) return 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { sign = -1; s++; }
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	return v * sign;
}

static double d_atof(const char *s)
{
	int sign = 1;
	double v = 0, f = 0, div = 1;
	if (!s) return 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { sign = -1; s++; }
	else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') { f = f * 10 + (*s++ - '0'); div *= 10; }
	}
	return sign * (v + f / div);
}

static void d_fmt(char *b, int cap, float v)
{
	if (v == (float)(int)v) snprintf(b, (size_t)cap, "%d", (int)v);
	else snprintf(b, (size_t)cap, "%.2f", v);
}

static float d_clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static void d_fmt_color(char *b, int cap, unsigned c)
{
	snprintf(b, (size_t)cap, "#%02X%02X%02X%02X",
		(c >> 16) & 0xFFu, (c >> 8) & 0xFFu, c & 0xFFu, (c >> 24) & 0xFFu);
}

static unsigned d_parse_color(const char *s, unsigned fallback)
{
	unsigned v = 0;
	int n = 0, i;
	if (!s || s[0] != '#') return fallback;
	for (i = 1; s[i] && n < 8; i++, n++) {
		char c = s[i];
		int h;
		if (c >= '0' && c <= '9') h = c - '0';
		else if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') h = c - 'A' + 10;
		else return fallback;
		v = (v << 4) | (unsigned)h;
	}
	if (n == 6) v |= 0xFF000000u;
	else if (n != 8) return fallback;
	return v;
}

static int d_option_count(const char *s)
{
	return UiDesign_OptionCount(s);
}

static int d_track_count(const char *s)
{
	int n = 0;
	if (!s) return 0;
	while (*s) {
		while (*s == ',' || *s == ' ' || *s == '\t') s++;
		if (!*s) break;
		n++;
		while (*s && *s != ',') s++;
	}
	return n;
}

static const char *d_dp_mode_name(int m)
{
	switch (m) {
	case UI_DP_DATETIME: return "DateTime";
	case UI_DP_RANGE_DATE: return "Range Date";
	case UI_DP_RANGE_DATETIME: return "Range DateTime";
	default: return "Date";
	}
}

static float d_tabs_header_h(int variant)
{
	if (variant == UI_TAB_PILL) return 34.0f;
	if (variant == UI_TAB_CARD) return 36.0f;
	return 38.0f;
}

static void d_inset(UiRect r, const float *pad, UiRect *out)
{
	out->x = r.x + pad[0];
	out->y = r.y + pad[1];
	out->w = r.w - pad[0] - pad[2];
	out->h = r.h - pad[1] - pad[3];
	if (out->w < 0) out->w = 0;
	if (out->h < 0) out->h = 0;
}

static void d_wide(const char *utf8, WCHAR *out, int cap)
{
	if (!out || cap <= 0) return;
	out[0] = 0;
	if (utf8) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
}

static void d_utf8(const WCHAR *w, char *out, int cap)
{
	if (!out || cap <= 0) return;
	out[0] = 0;
	if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL);
}

static char *d_strdup(const char *s)
{
	size_t n;
	char *p;
	if (!s) s = "";
	n = strlen(s) + 1;
	p = (char *)HeapAlloc(GetProcessHeap(), 0, n);
	if (p) memcpy(p, s, n);
	return p;
}

static int d_file_exists(const WCHAR *path)
{
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static wchar_t *d_wstrdup(const WCHAR *s)
{
	size_t n;
	wchar_t *p;
	if (!s) s = L"";
	n = (lstrlenW(s) + 1) * sizeof(*p);
	p = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, n);
	if (p) memcpy(p, s, n);
	return p;
}

static WCHAR *d_wide_alloc(const char *utf8)
{
	int len;
	WCHAR *out;
	if (!utf8) utf8 = "";
	len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (len <= 0) return NULL;
	out = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)len * sizeof(*out));
	if (out) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, len);
	return out;
}

static void d_set_dir(WCHAR **dst, const WCHAR *file)
{
	WCHAR *next;
	int i, cut = -1, len;
	if (!file) return;
	for (i = 0; file[i]; i++) if (file[i] == L'\\' || file[i] == L'/') cut = i;
	len = cut >= 0 ? cut + 1 : 0;
	next = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(len + 1) * sizeof(*next));
	if (!next) return;
	for (i = 0; i < len; i++) next[i] = file[i];
	next[len] = 0;
	HeapFree(GetProcessHeap(), 0, *dst);
	*dst = next;
}

static void d_dir_of(const WCHAR *file, WCHAR *out, int cap)
{
	int i, cut = -1;
	if (!out || cap <= 0) return;
	out[0] = 0;
	if (!file) return;
	for (i = 0; file[i]; i++) if (file[i] == L'\\' || file[i] == L'/') cut = i;
	for (i = 0; i <= cut && i < cap - 1; i++) out[i] = file[i];
	out[i] = 0;
}

static void d_doc_path_wide(WCHAR *out, int cap)
{
	d_wide(g_d.doc.path, out, cap);
}

static void d_autosize_window(UiDesignHost *w)
{
	if (!w) return;
	if (w->w < 120) w->w = 800;
	if (w->h < 80) w->h = 540;
}

/* ── 撤销 ───────────────────────────────────────────────────────────── */

static void d_undo_free(void)
{
	int i;
	for (i = 0; i < g_d.undoN; i++) {
		if (g_d.undo[i]) HeapFree(GetProcessHeap(), 0, g_d.undo[i]);
		g_d.undo[i] = NULL;
	}
	g_d.undoN = 0;
	g_d.undoPos = -1;
}

static void d_undo_push(void)
{
	char *j;
	int i;
	if (g_d.undoPos + 1 < g_d.undoN) {
		for (i = g_d.undoPos + 1; i < g_d.undoN; i++) {
			if (g_d.undo[i]) HeapFree(GetProcessHeap(), 0, g_d.undo[i]);
			g_d.undo[i] = NULL;
		}
		g_d.undoN = g_d.undoPos + 1;
	}
	if (g_d.undoN >= (int)(sizeof(g_d.undo) / sizeof(g_d.undo[0]))) {
		if (g_d.undo[0]) HeapFree(GetProcessHeap(), 0, g_d.undo[0]);
		for (i = 1; i < g_d.undoN; i++) g_d.undo[i - 1] = g_d.undo[i];
		g_d.undoN--;
		g_d.undoPos--;
	}
	j = UiDesign_ToJson(&g_d.doc);
	if (!j) return;
	g_d.undo[g_d.undoN++] = j;
	g_d.undoPos = g_d.undoN - 1;
}

static void d_undo_reset(void)
{
	d_undo_free();
	d_undo_push();
}

static char *d_wtext_utf8_alloc(const wchar_t *text, int len)
{
	char *p;
	int n;
	if (!text || len <= 0) {
		p = (char *)HeapAlloc(GetProcessHeap(), 0, 1);
		if (p) p[0] = 0;
		return p;
	}
	n = WideCharToMultiByte(CP_UTF8, 0, text, len, NULL, 0, NULL, NULL);
	if (n <= 0) return NULL;
	p = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)n + 1);
	if (!p) return NULL;
	WideCharToMultiByte(CP_UTF8, 0, text, len, p, n, NULL, NULL);
	p[n] = 0;
	return p;
}

static char *d_textarea_utf8_alloc(const UiTextArea *ta)
{
	return ta ? d_wtext_utf8_alloc(ta->text, ta->len) : NULL;
}

static char *d_edit_utf8_alloc(const UiEdit *e)
{
	return e ? d_wtext_utf8_alloc(e->text, e->len) : NULL;
}

static void d_after_edit(void)
{
	if (!g_d.editActive) return;
	if (g_d.editMultiline) {
		char *text = d_textarea_utf8_alloc(&g_d.propOptionsForm);
		if (text) {
			d_prop_apply_text(g_d.editKey, text);
			HeapFree(GetProcessHeap(), 0, text);
		}
	} else {
		char *text = d_edit_utf8_alloc(UiInput_Edit(&g_d.propForm, 0));
		if (text) {
			d_prop_apply_text(g_d.editKey, text);
			HeapFree(GetProcessHeap(), 0, text);
		}
	}
	g_d.editActive = 0;
	g_d.editMultiline = 0;
	g_d.propForm.focus = -1;
	if (g_d.app) UiIme_SetFocus(&g_d.app->ime, NULL);
	Ui_SetFocusId(&g_d.app->ui, UI_ID_NONE);
	d_undo_push();
}

static int d_view_root(void)
{
	if (g_d.selView >= 0 && g_d.selView < g_d.doc.viewCount)
		return g_d.doc.views[g_d.selView].root;
	return -1;
}

static int d_host_root(int host)
{
	int v;
	if (host < 0 || host >= g_d.doc.hostCount) return -1;
	v = g_d.doc.hosts[host].view;
	if (v < 0 || v >= g_d.doc.viewCount) return -1;
	return g_d.doc.views[v].root;
}

static int d_mode_index(int mode)
{
	return mode == UI_DN_SIZE_FILL || mode == UI_DN_SIZE_GROW ? 1 : 0;
}

static void d_normalize_modes(void)
{
	UiDesign_NormalizeSizeModes(&g_d.doc);
}

static void d_undo_step(int redo)
{
	int np;
	if (redo) {
		if (g_d.undoPos + 1 >= g_d.undoN) return;
		np = g_d.undoPos + 1;
	} else {
		if (g_d.undoPos <= 0) return;
		np = g_d.undoPos - 1;
	}
	if (!g_d.undo[np]) return;
	d_preview_shutdown();
	if (!UiDesign_FromJson(&g_d.doc, g_d.undo[np])) {
		d_error("撤销失败: 模型解析错误");
		return;
	}
	d_normalize_modes();
	g_d.undoPos = np;
	if (g_d.selWin >= g_d.doc.hostCount) g_d.selWin = g_d.doc.hostCount > 0 ? 0 : -1;
	if (g_d.selGroup >= g_d.doc.groupCount) g_d.selGroup = g_d.doc.groupCount > 0 ? 0 : -1;
	if (g_d.selView >= g_d.doc.viewCount) g_d.selView = g_d.doc.viewCount > 0 ? 0 : -1;
	if (g_d.selWin >= 0) g_d.selView = g_d.doc.hosts[g_d.selWin].view;
	g_d.selNode = -1;
	g_d.selRadioGroup = -1;
	g_d.selKind = g_d.selView >= 0 ? DSEL_VIEW : DSEL_WINDOW;
	d_sel_clear();
	if (d_view_root() >= 0) {
		g_d.selNode = d_view_root();
		d_sel_add(g_d.selNode);
	}
	g_d.treeDragIdx = -1;
	g_d.marquee = 0;
	g_d.layoutReady = 0;
	d_status(redo ? "重做" : "撤销");
}

/* ── 文件 ───────────────────────────────────────────────────────────── */

static int d_open_dialog(WCHAR **file)
{
	OPENFILENAMEW ofn;
	WCHAR *buf;
	ZeroMemory(&ofn, sizeof(ofn));
	buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32768 * sizeof(*buf));
	if (!buf) return 0;
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_d.app ? g_d.app->hwnd : NULL;
	ofn.lpstrFilter = L"CUI 设计工程 (*.cui)\0*.cui\0";
	ofn.lpstrFile = buf;
	ofn.nMaxFile = 32768;
	ofn.lpstrDefExt = L"cui";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	if (g_d.lastDir && g_d.lastDir[0]) ofn.lpstrInitialDir = g_d.lastDir;
	if (!GetOpenFileNameW(&ofn)) {
		HeapFree(GetProcessHeap(), 0, buf);
		return 0;
	}
	*file = buf;
	return 1;
}

static int d_save_dialog(WCHAR **file, const WCHAR *initial)
{
	OPENFILENAMEW ofn;
	WCHAR *buf;
	ZeroMemory(&ofn, sizeof(ofn));
	buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32768 * sizeof(*buf));
	if (!buf) return 0;
	if (initial) lstrcpynW(buf, initial, 32768);
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_d.app ? g_d.app->hwnd : NULL;
	ofn.lpstrFilter = L"CUI 设计工程 (*.cui)\0*.cui\0";
	ofn.lpstrFile = buf;
	ofn.nMaxFile = 32768;
	ofn.lpstrDefExt = L"cui";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (g_d.lastDir && g_d.lastDir[0]) ofn.lpstrInitialDir = g_d.lastDir;
	if (!GetSaveFileNameW(&ofn)) {
		HeapFree(GetProcessHeap(), 0, buf);
		return 0;
	}
	*file = buf;
	return 1;
}

static void d_preview_capture_tabs(void)
{
	int i;
	if (g_d.previewTabN > 0) return;
	g_d.previewTabN = 0;
	for (i = 0; i < g_d.doc.nodeCount; i++) {
		UiDesignNode *n = &g_d.doc.nodes[i];
		if (n->type != UI_DN_TABS || n->parent < 0) continue;
		if (!d_reserve((void **)&g_d.previewTabs, &g_d.previewTabCap,
		    g_d.previewTabN + 1, sizeof(*g_d.previewTabs))) break;
		g_d.previewTabs[g_d.previewTabN].node = i;
		g_d.previewTabs[g_d.previewTabN].sel = n->selected;
		g_d.previewTabN++;
	}
}

static void d_preview_restore_tabs(void)
{
	int i;
	for (i = 0; i < g_d.previewTabN; i++) {
		int idx = g_d.previewTabs[i].node;
		if (idx >= 0 && idx < g_d.doc.nodeCount &&
		    g_d.doc.nodes[idx].type == UI_DN_TABS) {
			g_d.doc.nodes[idx].selected = g_d.previewTabs[i].sel;
			UiDesign_TabsNormalize(&g_d.doc, idx);
		}
	}
	HeapFree(GetProcessHeap(), 0, g_d.previewTabs);
	g_d.previewTabs = NULL;
	g_d.previewTabN = g_d.previewTabCap = 0;
}

static void d_preview_capture_radios(void)
{
	int i;
	if (g_d.previewRadioN > 0) return;
	g_d.previewRadioN = 0;
	for (i = 0; i < d_rg_count(); i++) {
		UiDesignRadioGroup *rg = d_rg(i);
		DPreviewRadio *item;
		char *selected;
		if (!rg) continue;
		if (!d_reserve((void **)&g_d.previewRadios, &g_d.previewRadioCap,
		    g_d.previewRadioN + 1, sizeof(*g_d.previewRadios))) break;
		selected = d_strdup(rg->selectedRadioId ? rg->selectedRadioId : "");
		if (!selected) break;
		item = &g_d.previewRadios[g_d.previewRadioN++];
		item->uid = rg->uid;
		item->selectedRadioId = selected;
	}
}

static void d_preview_restore_radios(void)
{
	int i;
	for (i = 0; i < g_d.previewRadioN; i++) {
		int gi = UiDesign_RadioGroupFind(&g_d.doc, g_d.previewRadios[i].uid);
		UiDesignRadioGroup *rg = d_rg(gi);
		if (rg) {
			/* 只恢复元数据, 不影响 dirty/撤销. */
			UiDesign_SetString(&rg->selectedRadioId,
				g_d.previewRadios[i].selectedRadioId);
			UiDesign_RadioGroupNormalize(&g_d.doc, gi);
		}
		HeapFree(GetProcessHeap(), 0, g_d.previewRadios[i].selectedRadioId);
	}
	HeapFree(GetProcessHeap(), 0, g_d.previewRadios);
	g_d.previewRadios = NULL;
	g_d.previewRadioN = g_d.previewRadioCap = 0;
}

static void d_preview_shutdown(void)
{
	if (g_d.instReady) {
		UiDesignInstance_Shutdown(&g_d.inst);
		g_d.instReady = 0;
	}
	d_preview_restore_tabs();
	d_preview_restore_radios();
	g_d.previewDialogTried = 0;
}

static void d_reset_selection(void)
{
	int wi = g_d.doc.hostCount > 0 ? 0 : -1;
	d_preview_shutdown();
	g_d.selWin = wi;
	g_d.selView = wi >= 0 ? g_d.doc.hosts[wi].view : (g_d.doc.viewCount > 0 ? 0 : -1);
	if (g_d.selView < 0 && g_d.doc.viewCount > 0) g_d.selView = 0;
	g_d.selGroup = g_d.doc.groupCount > 0 ? 0 : -1;
	g_d.selKind = g_d.selView >= 0 ? DSEL_VIEW : DSEL_WINDOW;
	g_d.selNode = d_view_root();
	g_d.selRadioGroup = -1;
	d_sel_clear();
	if (g_d.selNode >= 0) d_sel_add(g_d.selNode);
	g_d.treeDragIdx = -1;
	g_d.treeDropIdx = -1;
	g_d.marquee = 0;
	g_d.dragListN = 0;
	g_d.preview = 0;
	g_d.instReady = 0;
	g_d.previewDialogTried = 0;
	g_d.layoutReady = 0;
}

static int d_confirm_discard(void)
{
	if (!g_d.doc.dirty) return 1;
	return MessageBoxW(g_d.app ? g_d.app->hwnd : NULL,
		L"当前工程有未保存的修改，是否放弃？", L"CUI Designer",
		MB_YESNO | MB_ICONWARNING) == IDYES;
}

static void d_new_doc(void)
{
	d_after_edit();
	if (!d_confirm_discard()) return;
	d_preview_shutdown();
	UiDesign_Free(&g_d.doc);
	UiDesign_DefaultDoc(&g_d.doc);
	d_reset_selection();
	d_undo_reset();
	d_status("新建工程");
}

static void d_open_file(void)
{
	WCHAR *file = NULL;
	d_after_edit();
	if (!d_confirm_discard()) return;
	if (!d_open_dialog(&file)) return;
	d_preview_shutdown();
	if (!UiDesign_LoadFile(&g_d.doc, file)) {
		d_error("打开失败: .cui 解析错误");
		HeapFree(GetProcessHeap(), 0, file);
		return;
	}
	d_normalize_modes();
	d_set_dir(&g_d.lastDir, file);
	HeapFree(GetProcessHeap(), 0, file);
	d_reset_selection();
	d_undo_reset();
	d_status("打开工程");
}

static void d_save_file(int saveAs)
{
	WCHAR *file = NULL;
	d_after_edit();
	if ((!g_d.doc.path || !g_d.doc.path[0]) || saveAs) {
		WCHAR *initial = g_d.doc.path ? d_wide_alloc(g_d.doc.path) : NULL;
		if (!d_save_dialog(&file, initial)) {
			HeapFree(GetProcessHeap(), 0, initial);
			return;
		}
		HeapFree(GetProcessHeap(), 0, initial);
	} else {
		file = d_wide_alloc(g_d.doc.path);
		if (!file) { d_error("保存失败: 内存不足"); return; }
	}
	if (!UiDesign_SaveFile(&g_d.doc, file)) {
		d_error("保存失败");
		HeapFree(GetProcessHeap(), 0, file);
		return;
	}
	d_set_dir(&g_d.lastDir, file);
	HeapFree(GetProcessHeap(), 0, file);
	d_undo_reset();
	d_status("已保存 .cui");
}

static void d_generate_window(void)
{
	WCHAR *dir = NULL;
	char report[256], err[256];
	d_after_edit();
	if (g_d.selWin < 0 || g_d.selWin >= g_d.doc.hostCount) {
		d_error("请先选择一个宿主");
		return;
	}
	if (!d_validate(err, sizeof(err))) {
		d_error(err);
		return;
	}
	dir = d_wstrdup(L".");
	if (!dir) { d_error("内存不足"); return; }
	if (g_d.doc.path && g_d.doc.path[0]) {
		WCHAR *file = d_wide_alloc(g_d.doc.path);
		if (!file) { HeapFree(GetProcessHeap(), 0, dir); d_error("内存不足"); return; }
		HeapFree(GetProcessHeap(), 0, dir);
		dir = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
			(lstrlenW(file) + 1) * sizeof(*dir));
		if (dir) d_dir_of(file, dir, (int)lstrlenW(file) + 1);
		HeapFree(GetProcessHeap(), 0, file);
		if (!dir) { d_error("内存不足"); return; }
	}
	if (UiDesign_GenerateHost(&g_d.doc, g_d.selWin, dir, report,
	    sizeof(report)) < 0)
		d_error("单宿主生成失败 (检查输出目录)");
	else if (err[0]) {
		char out[512];
		snprintf(out, sizeof(out), "%s · 警告: %s", report, err);
		d_status(out);
	} else {
		d_status(report);
	}
	HeapFree(GetProcessHeap(), 0, dir);
}

static void d_generate_commit(void)
{
	char report[256];
	int r;
	g_d.genPreview = 0;
	r = UiDesign_GenerateAll(&g_d.doc, g_d.genDir, report, sizeof(report));
	if (r < 0) d_error("生成失败 (检查分组输出路径)");
	else d_status(report);
	if (g_d.app) UiApp_Invalidate(g_d.app);
}

static void d_generate(void)
{
	WCHAR *dir = NULL;
	char report[256], err[256];
	int i, need = 0;
	d_after_edit();
	if (!d_validate(err, sizeof(err))) {
		d_error(err);
		return;
	}
	dir = d_wstrdup(L".");
	if (!dir) { d_error("内存不足"); return; }
	if (g_d.doc.path && g_d.doc.path[0]) {
		WCHAR *file = d_wide_alloc(g_d.doc.path);
		if (!file) { HeapFree(GetProcessHeap(), 0, dir); d_error("内存不足"); return; }
		HeapFree(GetProcessHeap(), 0, dir);
		dir = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
			(lstrlenW(file) + 1) * sizeof(*dir));
		if (dir) d_dir_of(file, dir, (int)lstrlenW(file) + 1);
		HeapFree(GetProcessHeap(), 0, file);
		if (!dir) { d_error("内存不足"); return; }
	}
	{
		need = g_d.doc.groupCount * 2;
		for (i = 0; i < g_d.doc.hostCount; i++)
			if (g_d.doc.hosts[i].group < 0) need += 2;
		if (g_d.genFiles) {
			UiDesign_GenFilesFree(g_d.genFiles, g_d.genFileN > 0 ? g_d.genFileN : 0);
			HeapFree(GetProcessHeap(), 0, g_d.genFiles);
			g_d.genFiles = NULL;
		}
		g_d.genFileN = 0;
		if (need > 0) {
			g_d.genFiles = (UiDesignGenFile *)HeapAlloc(GetProcessHeap(),
				HEAP_ZERO_MEMORY, (SIZE_T)need * sizeof(*g_d.genFiles));
			if (!g_d.genFiles) {
				HeapFree(GetProcessHeap(), 0, dir);
				d_error("生成预览失败: 内存不足");
				return;
			}
		}
		g_d.genFileN = UiDesign_PreviewAll(&g_d.doc, dir, g_d.genFiles, need,
			report, sizeof(report));
	}
	if (g_d.genFileN < 0) {
		if (g_d.genFiles) UiDesign_GenFilesFree(g_d.genFiles, need);
		HeapFree(GetProcessHeap(), 0, g_d.genFiles);
		g_d.genFiles = NULL;
		HeapFree(GetProcessHeap(), 0, dir);
		d_error("生成预览失败 (检查分组输出路径)");
		return;
	}
	HeapFree(GetProcessHeap(), 0, g_d.genDir);
	g_d.genDir = d_wstrdup(dir);
	HeapFree(GetProcessHeap(), 0, dir);
	if (!g_d.genDir) { d_error("生成预览失败: 内存不足"); return; }
	g_d.genPreview = 1;
	if (err[0]) {
		char out[512];
		snprintf(out, sizeof(out), "%s · 警告: %s", report, err);
		d_status(out);
	} else {
		d_status(report);
	}
	UiApp_Invalidate(g_d.app);
}

/* ── 属性模型 ───────────────────────────────────────────────────────── */

static UiDesignNode *d_selnode(void)
{
	return g_d.selNode >= 0 ? UiDesign_Node(&g_d.doc, g_d.selNode) : NULL;
}

static int d_layout_parent(int idx)
{
	return UiDesign_LayoutParent(&g_d.doc, idx);
}

/* ── 单选组元数据辅助 ──────────────────────────────────────────────── */

static int d_rg_count(void)
{
	return g_d.doc.radioGroupCount;
}

static UiDesignRadioGroup *d_rg(int gi)
{
	return gi >= 0 ? UiDesign_RadioGroup(&g_d.doc, gi) : NULL;
}

static int d_sel_radio_group_index(void)
{
	return g_d.selKind == DSEL_RADIOGROUP ? g_d.selRadioGroup : -1;
}

static UiDesignRadioGroup *d_selrg(void)
{
	return d_rg(d_sel_radio_group_index());
}

/* 节点所属单选组索引; 非 Radio 或未分组返回 -1. */
static int d_node_group_index(int idx)
{
	return UiDesign_RadioGroupForNode(&g_d.doc, idx);
}

/* 选中项 Radio 的节点索引 (无选中返回 -1). */
static int d_rg_selected_node(int gi)
{
	return UiDesign_RadioGroupSelectedNode(&g_d.doc, gi);
}

static int d_rg_member_count(int gi)
{
	return UiDesign_RadioGroupMemberCount(&g_d.doc, gi);
}

static int d_rg_member_at(int gi, int ordinal)
{
	return UiDesign_RadioGroupMemberAt(&g_d.doc, gi, ordinal);
}

static int d_rg_member_parent(int gi)
{
	int n = d_rg_member_at(gi, 0);
	return n >= 0 ? g_d.doc.nodes[n].parent : -1;
}

/* ── 单选组下拉 (属性面板) ─────────────────────────────────────────── */

static unsigned d_rg_hash_str(unsigned h, const char *s)
{
	if (s) while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
	return h * 16777619u;
}

static unsigned d_rg_hash_int(unsigned h, unsigned v)
{
	int k;
	for (k = 0; k < 4; k++) { h ^= (v >> (k * 8)) & 0xFFu; h *= 16777619u; }
	return h;
}

/* 组标签重名时附加 ID, 避免下拉出现无法区分的同名项. */
static int d_rg_label_dup(const UiDesignRadioGroup *rg)
{
	const char *a = rg->name[0] ? rg->name : rg->id;
	int i;
	for (i = 0; i < g_d.doc.radioGroupCount; i++) {
		const UiDesignRadioGroup *o = &g_d.doc.radioGroups[i];
		const char *b;
		if (o == rg || o->owner != rg->owner) continue;
		b = o->name[0] ? o->name : o->id;
		if (strcmp(a, b) == 0) return 1;
	}
	return 0;
}

static unsigned d_rg_combo_signature(int owner)
{
	unsigned h = 2166136261u;
	int i;
	h = d_rg_hash_int(h, (unsigned)owner);
	for (i = 0; i < g_d.doc.radioGroupCount; i++) {
		const UiDesignRadioGroup *rg = &g_d.doc.radioGroups[i];
		if (rg->owner != owner) continue;
		h = d_rg_hash_int(h, rg->uid);
		h = d_rg_hash_str(h, rg->id);
		h = d_rg_hash_str(h, rg->name);
	}
	return h;
}

static void d_rg_combo_clear(void)
{
	HeapFree(GetProcessHeap(), 0, g_d.propRgPool);
	HeapFree(GetProcessHeap(), 0, g_d.propRgItems);
	HeapFree(GetProcessHeap(), 0, g_d.propRgMap);
	g_d.propRgPool = NULL;
	g_d.propRgItems = NULL;
	g_d.propRgMap = NULL;
	g_d.propRgN = 0;
	g_d.propRgSigValid = 0;
}

static void d_rg_combo_build(int owner)
{
	UiSelect *s = &g_d.propRgSelect;
	int i, n = 1, total = (int)sizeof("未分组");
	char *p;
	unsigned k;
	d_rg_combo_clear();
	for (i = 0; i < g_d.doc.radioGroupCount; i++) {
		const UiDesignRadioGroup *rg = &g_d.doc.radioGroups[i];
		const char *label;
		if (rg->owner != owner) continue;
		label = rg->name[0] ? rg->name : rg->id;
		n++;
		total += 1 + (int)strlen(label);
		if (d_rg_label_dup(rg))
			total += (int)strlen(" · ") + (int)strlen(rg->id);
	}
	g_d.propRgPool = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)total);
	g_d.propRgItems = (const char **)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)n * sizeof(*g_d.propRgItems));
	g_d.propRgMap = (int *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)n * sizeof(*g_d.propRgMap));
	if (!g_d.propRgPool || !g_d.propRgItems || !g_d.propRgMap) {
		d_rg_combo_clear();
		UiSelect_SetItems(s, NULL, 0);
		return;
	}
	p = g_d.propRgPool;
	memcpy(p, "未分组", sizeof("未分组"));
	g_d.propRgItems[0] = p;
	g_d.propRgMap[0] = -1;
	p += sizeof("未分组");
	k = 1;
	for (i = 0; i < g_d.doc.radioGroupCount; i++) {
		const UiDesignRadioGroup *rg = &g_d.doc.radioGroups[i];
		const char *label;
		size_t len;
		if (rg->owner != owner) continue;
		label = rg->name[0] ? rg->name : rg->id;
		len = strlen(label);
		g_d.propRgItems[k] = p;
		g_d.propRgMap[k] = i;
		memcpy(p, label, len);
		p += len;
		if (d_rg_label_dup(rg)) {
			memcpy(p, " · ", 4);
			p += 4;
			len = strlen(rg->id);
			memcpy(p, rg->id, len);
			p += len;
		}
		*p++ = 0;
		k++;
	}
	g_d.propRgN = n;
	UiSelect_SetItems(s, g_d.propRgItems, n);
}

/* 按 owner 过滤并刷新项; 返回当前组成员对应的项下标. */
static int d_rg_combo_index(int owner, int curGroup)
{
	unsigned sig;
	int i;
	if (!g_d.propRgSigValid || g_d.propRgOwner != owner) {
		d_rg_combo_build(owner);
		g_d.propRgOwner = owner;
		g_d.propRgSig = d_rg_combo_signature(owner);
		g_d.propRgSigValid = 1;
	} else {
		sig = d_rg_combo_signature(owner);
		if (sig != g_d.propRgSig) {
			d_rg_combo_build(owner);
			g_d.propRgSig = sig;
		}
	}
	if (curGroup < 0) return 0;
	for (i = 0; i < g_d.propRgN; i++)
		if (g_d.propRgMap[i] == curGroup) return i;
	return 0;
}

static void d_rg_combo_commit(void)
{
	UiDesignNode *n;
	int node = g_d.propRgNode;
	int idx, gi, old;
	if (!g_d.propRgReady || !g_d.propRgSelect.committed) return;
	g_d.propRgSelect.committed = 0;
	idx = g_d.propRgSelect.selected;
	if (node < 0 || node >= g_d.doc.nodeCount ||
	    g_d.selKind != DSEL_NODE || g_d.selNode != node)
		return;
	n = &g_d.doc.nodes[node];
	if (n->type != UI_DN_RADIO || idx < 0 || idx >= g_d.propRgN) return;
	gi = g_d.propRgMap[idx];
	old = d_node_group_index(node);
	if (gi == old) return;
	if (!UiDesign_RadioGroupAssign(&g_d.doc, node, gi)) {
		d_status("单选组不属于当前窗口");
		return;
	}
	d_undo_push();
	g_d.layoutReady = 0;
	d_status(gi >= 0 ? "已绑定单选组" : "已解除单选组");
	UiApp_Invalidate(g_d.app);
}

/* 组成员并集矩形 (仅可见且属于当前视图); 无可见成员返回 0. */
static int d_rg_union_rect(int gi, UiRect *out)
{
	int i, count = d_rg_member_count(gi), got = 0;
	UiRect u = Ui_Rect(0, 0, 0, 0);
	if (!g_d.layoutReady || !out) return 0;
	for (i = 0; i < count; i++) {
		int ni = d_rg_member_at(gi, i);
		UiRect r;
		if (ni < 0 || ni >= g_d.doc.nodeCount) continue;
		if (g_d.doc.nodes[ni].owner != g_d.selView) continue;
		if (!g_d.doc.nodes[ni].visible) continue;
		r = g_d.rects[ni];
		if (r.w <= 0.0f && r.h <= 0.0f) continue;
		if (!got) { u = r; got = 1; }
		else {
			float x0 = u.x < r.x ? u.x : r.x;
			float y0 = u.y < r.y ? u.y : r.y;
			float x1 = u.x + u.w > r.x + r.w ? u.x + u.w : r.x + r.w;
			float y1 = u.y + u.h > r.y + r.h ? u.y + u.h : r.y + r.h;
			u = Ui_Rect(x0, y0, x1 - x0, y1 - y0);
		}
	}
	if (got && out) *out = u;
	return got;
}

/* 视图中唯一的单选组 ID. */
static char *d_unique_rg_id(int view, const char *base)
{
	int i, n;
	size_t cap = strlen(base) + 32;
	char *out = (char *)HeapAlloc(GetProcessHeap(), 0, cap);
	if (!out) return NULL;
	for (n = 1; n < 10000; n++) {
		int used = 0;
		snprintf(out, cap, "%s%d", base, n);
		for (i = 0; i < d_rg_count(); i++) {
			UiDesignRadioGroup *rg = d_rg(i);
			if (rg && rg->owner == view && strcmp(rg->id, out) == 0) {
				used = 1;
				break;
			}
		}
		if (!used) return out;
	}
	snprintf(out, cap, "%s", base);
	return out;
}

/* 组成员当前父容器中的起始位置与数量 (假设成员连续). */
static int d_rg_block_span(int gi, int *parent, int *start, int *count)
{
	int p = d_rg_member_parent(gi);
	int i, n = d_rg_member_count(gi), s = -1, found = 0;
	if (p < 0 || p >= g_d.doc.nodeCount || n <= 0) return 0;
	for (i = 0; i < g_d.doc.nodes[p].childCount; i++) {
		int ci = g_d.doc.nodes[p].children[i];
		UiDesignNode *cn = UiDesign_Node(&g_d.doc, ci);
		if (!cn || cn->type != UI_DN_RADIO) continue;
		if (d_node_group_index(ci) != gi) continue;
		if (s < 0) s = i;
		found++;
	}
	if (s < 0 || found != n) return 0;   /* 跨父容器时不做块移动 */
	if (parent) *parent = p;
	if (start) *start = s;
	if (count) *count = n;
	return 1;
}

/* 整组移动到父容器 newStart 位置 (保持成员相对顺序). */
static int d_rg_block_move_to(int gi, int newStart)
{
	int parent = -1, start = 0, count = 0, i, total;
	int *members;
	if (!d_rg_block_span(gi, &parent, &start, &count)) return 0;
	total = g_d.doc.nodes[parent].childCount;
	if (newStart > total - count) newStart = total - count;
	if (newStart < 0) newStart = 0;
	if (newStart == start) return 0;
	members = (int *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)count * sizeof(*members));
	if (!members) return 0;
	for (i = 0; i < count; i++) {
		members[i] = d_rg_member_at(gi, i);
		if (members[i] < 0) {
			HeapFree(GetProcessHeap(), 0, members);
			return 0;
		}
	}
	for (i = 0; i < count; i++)
		UiDesign_NodeMove(&g_d.doc, members[i], parent, newStart + i);
	HeapFree(GetProcessHeap(), 0, members);
	return 1;
}

static int d_rg_block_move(int gi, int dir)
{
	int start = 0;
	if (!d_rg_block_span(gi, NULL, &start, NULL)) return 0;
	return d_rg_block_move_to(gi, start + (dir ? 1 : -1));
}

/* 删除所有成员后移除元数据. */
static int d_del_radio_group(int gi)
{
	UiDesignRadioGroup *rg = d_rg(gi);
	int i, n, parent = -1;
	if (!rg) return 0;
	parent = d_rg_member_parent(gi);
	n = d_rg_member_count(gi);
	for (i = 0; i < n; i++) {
		int ni = d_rg_member_at(gi, i);
		if (ni >= 0) UiDesign_NodeRemove(&g_d.doc, ni);
	}
	/* 元数据索引会因删除而前移; 按 uid 重新查找. */
	{
		unsigned uid = rg->uid;
		int at = UiDesign_RadioGroupFind(&g_d.doc, uid);
		if (at >= 0) UiDesign_RadioGroupRemove(&g_d.doc, at);
	}
	g_d.selRadioGroup = -1;
	g_d.selKind = DSEL_NODE;
	g_d.selNode = parent >= 0 ? parent : d_view_root();
	d_sel_clear();
	if (g_d.selNode >= 0) d_sel_add(g_d.selNode);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已删除单选组");
	return 1;
}

static int d_node_set_id(int idx, const char *id)
{
	UiDesignNode *n = UiDesign_Node(&g_d.doc, idx);
	char *old;
	int gi, changed = 0;
	if (!n) return 0;
	old = d_strdup(n->id);
	if (!old) return 0;
	if (!UiDesign_SetString(&n->id, id)) {
		HeapFree(GetProcessHeap(), 0, old);
		return 0;
	}
	/* 单选组选中项使用稳定节点 ID, 重命名时同步. */
	gi = d_node_group_index(idx);
	if (gi >= 0) {
		UiDesignRadioGroup *rg = d_rg(gi);
		if (rg && rg->selectedRadioId && strcmp(rg->selectedRadioId, old) == 0) {
			UiDesign_SetString(&rg->selectedRadioId, n->id);
			changed = 1;
		}
	}
	HeapFree(GetProcessHeap(), 0, old);
	return changed;
}

/* 参与布局的真实子项 (Radio 也是普通子项). */
static int d_layout_item_count(int parent)
{
	const UiDesignNode *p = UiDesign_Node(&g_d.doc, parent);
	int i, count = 0;
	if (!p) return 0;
	for (i = 0; i < p->childCount; i++) {
		const UiDesignNode *n = UiDesign_Node(&g_d.doc, p->children[i]);
		if (n && n->visible) count++;
	}
	return count;
}

static int d_layout_item_at(int parent, int pos)
{
	UiDesignNode *p = UiDesign_Node(&g_d.doc, parent);
	int i, at = 0;
	if (!p || pos < 0) return -1;
	for (i = 0; i < p->childCount; i++) {
		UiDesignNode *n = UiDesign_Node(&g_d.doc, p->children[i]);
		if (!n || !n->visible) continue;
		if (at++ == pos) return p->children[i];
	}
	return -1;
}

static void d_translate_layout_node(int idx, float dx, float dy)
{
	UiDesignNode *n = UiDesign_Node(&g_d.doc, idx);
	int i, gi;
	if (!n) return;
	gi = d_node_group_index(idx);
	if (gi >= 0) {
		int count = d_rg_member_count(gi);
		for (i = 0; i < count; i++) {
			UiDesignNode *r = UiDesign_Node(&g_d.doc, d_rg_member_at(gi, i));
			if (!r) continue;
			r->x += dx;
			r->y += dy;
		}
		return;
	}
	n->x += dx;
	n->y += dy;
}

static int d_sel_has(int idx)
{
	int i;
	for (i = 0; i < g_d.selSetN; i++)
		if (g_d.selSet[i] == idx) return 1;
	return 0;
}

static int d_parent_free(int parent)
{
	if (parent < 0 || parent >= g_d.doc.nodeCount) return 0;
	return g_d.doc.nodes[parent].type == UI_DN_ABSOLUTE ||
		g_d.doc.nodes[parent].type == UI_DN_PANEL;
}

static int d_drag_append(int idx)
{
	UiDesignNode *n = UiDesign_Node(&g_d.doc, idx);
	int k;
	if (!n || n->parent < 0 || !d_parent_free(d_layout_parent(idx))) return 0;
	for (k = 0; k < g_d.dragListN; k++)
		if (g_d.dragList[k].idx == idx) return 1;
	if (!d_reserve((void **)&g_d.dragList, &g_d.dragListCap,
	    g_d.dragListN + 1, sizeof(*g_d.dragList))) return 0;
	g_d.dragList[g_d.dragListN].idx = idx;
	g_d.dragList[g_d.dragListN].x = n->x;
	g_d.dragList[g_d.dragListN].y = n->y;
	g_d.dragListN++;
	return 1;
}

static void d_drag_begin(int primary)
{
	int i;
	g_d.dragListN = 0;
	g_d.dragReorderParent = g_d.dragReorderPos = -1;
	if (g_d.selKind == DSEL_RADIOGROUP && g_d.selRadioGroup >= 0) {
		int count = d_rg_member_count(g_d.selRadioGroup);
		for (i = 0; i < count; i++)
			d_drag_append(d_rg_member_at(g_d.selRadioGroup, i));
	} else {
		for (i = 0; i < g_d.selSetN; i++) {
			int idx = g_d.selSet[i], k;
			int duplicate = 0;
			for (k = 0; k < i; k++)
				if (g_d.selSet[k] == idx) duplicate = 1;
			if (!duplicate) d_drag_append(idx);
		}
	}
	if (g_d.dragListN == 0) d_drag_append(primary);
	if (primary >= 0 && primary < g_d.doc.nodeCount) {
		UiDesignNode *n = &g_d.doc.nodes[primary];
		g_d.dragReorderParent = n->parent;
		if (g_d.dragReorderParent >= 0) {
			UiDesignNode *p = &g_d.doc.nodes[g_d.dragReorderParent];
			for (i = 0; i < p->childCount; i++)
				if (p->children[i] == primary) { g_d.dragReorderPos = i; break; }
		}
	}
}

static void d_sel_clear(void)
{
	g_d.selSetN = 0;
}

static void d_tabs_add_page(void);

static void d_tabs_reveal(int idx)
{
	while (idx >= 0 && idx < g_d.doc.nodeCount) {
		UiDesignNode *n = &g_d.doc.nodes[idx];
		int p = n->parent;
		int i;
		if (p < 0) break;
		if (g_d.doc.nodes[p].type == UI_DN_TABS) {
			UiDesignNode *pn = &g_d.doc.nodes[p];
			for (i = 0; i < pn->childCount; i++)
				if (pn->children[i] == idx) break;
			if (i < pn->childCount && pn->selected != i) {
				pn->selected = i;
				g_d.layoutReady = 0;
				g_d.doc.dirty = 1;
			}
		}
		idx = p;
	}
}

static void d_sel_single(int idx)
{
	d_sel_clear();
	g_d.selRadioGroup = -1;
	if (idx >= 0) {
		d_sel_add(idx);
		g_d.selNode = idx;
		g_d.selKind = DSEL_NODE;
		d_tabs_reveal(idx);
	}
}

static void d_sel_toggle(int idx)
{
	int i;
	g_d.selRadioGroup = -1;
	if (!d_sel_has(idx)) {
		d_sel_add(idx);
		g_d.selNode = idx;
		g_d.selKind = DSEL_NODE;
		d_tabs_reveal(idx);
		return;
	}
	for (i = 0; i < g_d.selSetN; i++)
		if (g_d.selSet[i] == idx) {
			int k;
			for (k = i + 1; k < g_d.selSetN; k++) g_d.selSet[k - 1] = g_d.selSet[k];
			g_d.selSetN--;
			break;
		}
	g_d.selNode = g_d.selSetN > 0 ? g_d.selSet[g_d.selSetN - 1] : -1;
}

/* 选中虚拟单选组行: 不占用 selNode/selSet. */
static void d_sel_radio_group(int gi)
{
	d_sel_clear();
	g_d.selRadioGroup = gi;
	g_d.selNode = -1;
	g_d.selKind = DSEL_RADIOGROUP;
}

static void d_sel_prune(void)
{
	int i, n = 0;
	for (i = 0; i < g_d.selSetN; i++)
		if (g_d.selSet[i] >= 0 && g_d.selSet[i] < g_d.doc.nodeCount)
			g_d.selSet[n++] = g_d.selSet[i];
	g_d.selSetN = n;
	if (g_d.selNode >= g_d.doc.nodeCount) g_d.selNode = n > 0 ? g_d.selSet[n - 1] : -1;
}

static UiDesignHost *d_selwindow(void)
{
	return g_d.selWin >= 0 ? UiDesign_Host(&g_d.doc, g_d.selWin) : NULL;
}

static UiDesignView *d_selview(void)
{
	return g_d.selView >= 0 ? UiDesign_View(&g_d.doc, g_d.selView) : NULL;
}

static UiDesignGroup *d_selgroup(void)
{
	return g_d.selGroup >= 0 ? UiDesign_Group(&g_d.doc, g_d.selGroup) : NULL;
}

static const char *d_size_mode(int m)
{
	switch (m) {
	case UI_DN_SIZE_AUTO: return "Auto";
	case UI_DN_SIZE_FILL: return "Fill";
	case UI_DN_SIZE_GROW: return "Grow";
	default: return "Fixed";
	}
}


static const char *d_group_name(int gi)
{
	if (gi < 0 || gi >= g_d.doc.groupCount) return "未分组";
	return g_d.doc.groups[gi].name;
}

static void d_prop_add(int key, int kind, const char *label, const char *val)
{
	DProp *p;
	if (g_d.propN >= (int)(sizeof(g_d.props) / sizeof(g_d.props[0]))) return;
	p = &g_d.props[g_d.propN++];
	p->key = key;
	p->kind = kind;
	p->sec = g_d.propSection;
	p->label = label;
	p->valueRef = (kind == DPK_TEXT || kind == DPK_MULTILINE) ? val : NULL;
	UiDesign_SetText(p->value, sizeof(p->value), val ? val : "");
}

static const char *d_sec_title(int sec)
{
	static const char *titles[] = { "基本", "布局", "尺寸与位置", "内容", "事件", "输出" };
	if (sec < 0 || sec >= (int)(sizeof(titles) / sizeof(titles[0]))) return "";
	return titles[sec];
}

static void d_build_props(void)
{
	char b[160];
	g_d.propN = 0;
	g_d.propSection = 0;
	if (g_d.selKind == DSEL_WINDOW) {
		UiDesignHost *w = d_selwindow();
		if (!w) return;
		d_prop_add(DK_W_ID, DPK_TEXT, "ID", w->id);
		d_prop_add(DK_W_NAME, DPK_TEXT, "名称", w->name);
		d_prop_add(DK_W_TITLE, DPK_TEXT, "标题", w->title);
		d_prop_add(DK_W_THEME, DPK_ENUM, "主题", w->theme == UI_THEME_DARK ? "Dark" : "Light");
		g_d.propSection = 2;
		d_prop_add(DK_W_KIND, DPK_ENUM, "宿主类型",
			w->kind == UI_DH_DIALOG ? "Dialog" :
			(w->kind == UI_DH_PANEL ? "Panel" : "Window"));
		d_prop_add(DK_W_VIEW, DPK_ENUM, "挂载视图",
			w->view >= 0 && w->view < g_d.doc.viewCount
				? g_d.doc.views[w->view].name : "未挂载");
		d_prop_add(DK_W_GROUP, DPK_ENUM, "生成组", d_group_name(w->group));
		d_fmt(b, sizeof(b), w->w); d_prop_add(DK_W_W, DPK_FLOAT, "宽", b);
		d_fmt(b, sizeof(b), w->h); d_prop_add(DK_W_H, DPK_FLOAT, "高", b);
		d_fmt(b, sizeof(b), w->minW); d_prop_add(DK_W_MINW, DPK_FLOAT, "最小宽", b);
		d_fmt(b, sizeof(b), w->minH); d_prop_add(DK_W_MINH, DPK_FLOAT, "最小高", b);
		return;
	}
	if (g_d.selKind == DSEL_VIEW) {
		UiDesignView *v = d_selview();
		if (!v) return;
		d_prop_add(DK_V_ID, DPK_TEXT, "ID", v->id);
		d_prop_add(DK_V_NAME, DPK_TEXT, "名称", v->name);
		g_d.propSection = 2;
		d_prop_add(DK_V_GROUP, DPK_ENUM, "生成组", d_group_name(v->group));
		d_fmt(b, sizeof(b), v->w); d_prop_add(DK_V_W, DPK_FLOAT, "设计宽", b);
		d_fmt(b, sizeof(b), v->h); d_prop_add(DK_V_H, DPK_FLOAT, "设计高", b);
		return;
	}
	if (g_d.selKind == DSEL_GROUP) {
		UiDesignGroup *gr = d_selgroup();
		if (!gr) return;
		d_prop_add(DK_G_ID, DPK_TEXT, "ID", gr->id);
		d_prop_add(DK_G_NAME, DPK_TEXT, "名称", gr->name);
		g_d.propSection = 5;
		d_prop_add(DK_G_OUTC, DPK_TEXT, "输出 .c", gr->outputC);
		d_prop_add(DK_G_OUTH, DPK_TEXT, "输出 .h", gr->outputH);
		d_prop_add(DK_G_SYM, DPK_TEXT, "符号前缀", gr->symbolPrefix);
		return;
	}
	if (g_d.selKind == DSEL_RADIOGROUP) {
		UiDesignRadioGroup *rg = d_selrg();
		int selNode;
		char selText[160];
		if (!rg) return;
		d_prop_add(DK_RG_ID, DPK_TEXT, "ID", rg->id);
		d_prop_add(DK_RG_NAME, DPK_TEXT, "名称", rg->name);
		d_prop_add(DK_RG_ENABLED, DPK_BOOL, "Enabled", rg->enabled ? "1" : "0");
		selNode = d_rg_selected_node(g_d.selRadioGroup);
		if (selNode >= 0)
			snprintf(selText, sizeof(selText), "%s · %s",
				g_d.doc.nodes[selNode].id, g_d.doc.nodes[selNode].text);
		else
			snprintf(selText, sizeof(selText), "无");
		d_prop_add(DK_RG_SELECTED, DPK_ENUM, "选中项", selText);
		snprintf(b, sizeof(b), "%d", d_rg_member_count(g_d.selRadioGroup));
		d_prop_add(DK_N_PAGECOUNT, DPK_READONLY, "成员数", b);
		g_d.propSection = 4;
		d_prop_add(DK_RG_CHANGE, DPK_TEXT, "OnChange",
			rg->onChange ? rg->onChange : "");
		return;
	}
	{
		UiDesignNode *n = d_selnode();
		int layoutParent, freeParent, gridParent;
		if (!n) return;
		layoutParent = d_layout_parent(g_d.selNode);
		freeParent = layoutParent >= 0 &&
			(g_d.doc.nodes[layoutParent].type == UI_DN_ABSOLUTE ||
			 g_d.doc.nodes[layoutParent].type == UI_DN_PANEL);
		gridParent = layoutParent >= 0 &&
			g_d.doc.nodes[layoutParent].type == UI_DN_GRID;
		d_prop_add(DK_N_ID, DPK_TEXT, "ID", n->id);
		d_prop_add(DK_N_NAME, DPK_TEXT, "名称", n->name);
		d_prop_add(DK_N_TYPE, DPK_READONLY, "类型", UiDesign_TypeName(n->type));
		g_d.propSection = 2;
		if (n->parent >= 0 &&
		    (layoutParent < 0 || g_d.doc.nodes[layoutParent].type != UI_DN_TABS)) {
			d_fmt(b, sizeof(b), n->w); d_prop_add(DK_N_W, DPK_FLOAT, "宽", b);
			d_fmt(b, sizeof(b), n->h); d_prop_add(DK_N_H, DPK_FLOAT, "高", b);
		}
		if (freeParent) {
			d_fmt(b, sizeof(b), n->x); d_prop_add(DK_N_X, DPK_FLOAT, "X", b);
			d_fmt(b, sizeof(b), n->y); d_prop_add(DK_N_Y, DPK_FLOAT, "Y", b);
		}
		if (n->type == UI_DN_RADIO && layoutParent >= 0) {
			d_fmt(b, sizeof(b), n->margin[0]); d_prop_add(DK_N_MARGINL, DPK_FLOAT, "Margin L", b);
			d_fmt(b, sizeof(b), n->margin[1]); d_prop_add(DK_N_MARGINT, DPK_FLOAT, "Margin T", b);
			d_fmt(b, sizeof(b), n->margin[2]); d_prop_add(DK_N_MARGINR, DPK_FLOAT, "Margin R", b);
			d_fmt(b, sizeof(b), n->margin[3]); d_prop_add(DK_N_MARGINB, DPK_FLOAT, "Margin B", b);
		}
		g_d.propSection = 1;
		if (UiDesign_IsLayoutContainer(n->type)) {
			d_fmt(b, sizeof(b), n->padding[0]); d_prop_add(DK_N_PADL, DPK_FLOAT, "Padding L", b);
			d_fmt(b, sizeof(b), n->padding[1]); d_prop_add(DK_N_PADT, DPK_FLOAT, "Padding T", b);
			d_fmt(b, sizeof(b), n->padding[2]); d_prop_add(DK_N_PADR, DPK_FLOAT, "Padding R", b);
			d_fmt(b, sizeof(b), n->padding[3]); d_prop_add(DK_N_PADB, DPK_FLOAT, "Padding B", b);
		}
		if (n->type == UI_DN_COLUMN || n->type == UI_DN_ROW) {
			d_fmt(b, sizeof(b), n->gap); d_prop_add(DK_N_GAP, DPK_FLOAT, "Gap", b);
		}
		if (n->type == UI_DN_PANEL || n->type == UI_DN_IMAGE) {
			d_fmt(b, sizeof(b), n->radius);
			d_prop_add(DK_N_RADIUS, DPK_FLOAT, "圆角", b);
		}
		if (n->type == UI_DN_GRID) {
			snprintf(b, sizeof(b), "%d", n->gridCols);
			d_prop_add(DK_N_GRIDCOLS, DPK_INT, "Grid Cols", b);
			snprintf(b, sizeof(b), "%d", n->gridRows);
			d_prop_add(DK_N_GRIDROWS, DPK_INT, "Grid Rows (0=自动)", b);
			d_fmt(b, sizeof(b), n->gridRowGap);
			d_prop_add(DK_N_GRIDROWGAP, DPK_FLOAT, "Row Gap", b);
			d_fmt(b, sizeof(b), n->gridColGap);
			d_prop_add(DK_N_GRIDCOLGAP, DPK_FLOAT, "Col Gap", b);
			d_prop_add(DK_N_GRIDCOLTRACKS, DPK_TEXT, "列宽",
				n->gridColTracks);
		} else if (gridParent) {
			snprintf(b, sizeof(b), "%d", n->gridRow);
			d_prop_add(DK_N_GRIDROW, DPK_INT, "Grid Row", b);
			snprintf(b, sizeof(b), "%d", n->gridCol);
			d_prop_add(DK_N_GRIDCOL, DPK_INT, "Grid Col", b);
			snprintf(b, sizeof(b), "%d", n->colSpan);
			d_prop_add(DK_N_COLSPAN, DPK_INT, "Col Span", b);
			snprintf(b, sizeof(b), "%d", n->rowSpan);
			d_prop_add(DK_N_ROWSPAN, DPK_INT, "Row Span", b);
		}
		g_d.propSection = 3;
		switch (n->type) {
		case UI_DN_LABEL:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			d_prop_add(DK_N_TEXTALIGN, DPK_ENUM, "水平对齐",
				n->textAlign == UI_DN_ALIGN_CENTER ? "Center" :
				(n->textAlign == UI_DN_ALIGN_END ? "End" : "Start"));
			d_prop_add(DK_N_TEXTVALIGN, DPK_ENUM, "垂直对齐",
				n->textVAlign == UI_DN_VALIGN_CENTER ? "Center" :
				(n->textVAlign == UI_DN_VALIGN_BOTTOM ? "Bottom" : "Top"));
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_BUTTON:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			d_prop_add(DK_N_VARIANT, DPK_ENUM, "Variant",
				n->variant == 2 ? "Danger" : (n->variant == 1 ? "Primary" : "Default"));
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_INPUT:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			d_prop_add(DK_N_HINT, DPK_TEXT, "Hint", n->hint);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_CHECKBOX:
		case UI_DN_SWITCH:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			d_prop_add(DK_N_CHECKED, DPK_BOOL, "Checked", n->checked ? "1" : "0");
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_SLIDER:
			d_fmt(b, sizeof(b), n->value); d_prop_add(DK_N_VALUE, DPK_FLOAT, "Value", b);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_PROGRESS:
			d_fmt(b, sizeof(b), n->value); d_prop_add(DK_N_VALUE, DPK_FLOAT, "Value", b);
			break;
		case UI_DN_SELECT:
			d_prop_add(DK_N_OPTIONS, DPK_MULTILINE, "Options", n->options);
			snprintf(b, sizeof(b), "%d", n->selected);
			d_prop_add(DK_N_SELECTED, DPK_ENUM, "Selected", b);
			d_prop_add(DK_N_HINT, DPK_TEXT, "Hint", n->hint);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_RADIO: {
			int gi = d_node_group_index(g_d.selNode);
			UiDesignRadioGroup *rg = d_rg(gi);
			int selNode = gi >= 0 ? d_rg_selected_node(gi) : -1;
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			d_prop_add(DK_N_RADIOGROUP, DPK_READONLY, "单选组",
				rg ? rg->name : "未分组");
			d_prop_add(DK_N_RADIOSELECTED, DPK_READONLY, "组选中项",
				selNode == g_d.selNode ? "是" : "否");
			break;
		}
		case UI_DN_IMAGE:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			break;
		case UI_DN_TABS:
			if (n->childCount > 0) {
				int sel = UiDesign_TabsSelectedPage(&g_d.doc, g_d.selNode);
				snprintf(b, sizeof(b), "%d/%d · %s", sel + 1, n->childCount,
					g_d.doc.nodes[n->children[sel]].name);
			} else {
				snprintf(b, sizeof(b), "无页");
			}
			d_prop_add(DK_N_SELECTED, DPK_ENUM, "当前页", b);
			snprintf(b, sizeof(b), "%d", n->childCount);
			d_prop_add(DK_N_PAGECOUNT, DPK_READONLY, "页数", b);
			d_prop_add(DK_N_ADDPAGE, DPK_ENUM, "页面操作", "＋ 新增页");
			d_prop_add(DK_N_VARIANT, DPK_ENUM, "页签样式",
				n->variant == 1 ? "Pill" : (n->variant == 2 ? "Card" : "Line"));
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_TEXTAREA:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			d_prop_add(DK_N_HINT, DPK_TEXT, "Hint", n->hint);
			d_prop_add(DK_N_READONLY, DPK_BOOL, "只读", n->readonly ? "1" : "0");
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_DATEPICKER:
			d_prop_add(DK_N_MODE, DPK_ENUM, "模式", d_dp_mode_name(n->mode));
			d_prop_add(DK_N_HINT, DPK_TEXT, "Hint", n->hint);
			d_prop_add(DK_N_TEXT, DPK_TEXT, "起始值", n->text);
			if (n->mode >= 2)
				d_prop_add(DK_N_OPTIONS, DPK_TEXT, "结束值", n->options);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_COLORSEL:
			d_fmt_color(b, sizeof(b), n->color);
			d_prop_add(DK_N_COLOR, DPK_COLOR, "颜色", b);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_RINGPROGRESS:
			d_fmt(b, sizeof(b), n->value);
			d_prop_add(DK_N_VALUE, DPK_FLOAT, "进度 (0-1)", b);
			d_fmt(b, sizeof(b), n->ringMax);
			d_prop_add(DK_N_RINGMAX, DPK_FLOAT, "最大值", b);
			d_fmt(b, sizeof(b), n->ringWidth);
			d_prop_add(DK_N_RINGWIDTH, DPK_FLOAT, "环宽", b);
			d_prop_add(DK_N_TEXT, DPK_TEXT, "标题", n->text);
			d_prop_add(DK_N_HINT, DPK_TEXT, "单位", n->hint);
			d_prop_add(DK_N_CHECKED, DPK_BOOL, "圆角端点", n->checked ? "1" : "0");
			d_fmt_color(b, sizeof(b), n->color);
			d_prop_add(DK_N_COLOR, DPK_COLOR, "强调色", b);
			break;
		case UI_DN_QRCODE:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "内容", n->text);
			d_prop_add(DK_N_ECC, DPK_ENUM, "纠错",
				n->ecc == UI_QR_ECC_L ? "L" : (n->ecc == UI_QR_ECC_Q ? "Q" :
				(n->ecc == UI_QR_ECC_H ? "H" : "M")));
			d_fmt_color(b, sizeof(b), n->color);
			d_prop_add(DK_N_COLOR, DPK_COLOR, "前景色", b);
			d_fmt_color(b, sizeof(b), n->bgColor);
			d_prop_add(DK_N_BGCOLOR, DPK_COLOR, "背景色", b);
			d_prop_add(DK_N_LOGOSRC, DPK_TEXT, "Logo 图片", n->logoSrc);
			d_fmt(b, sizeof(b), n->logoRatio);
			d_prop_add(DK_N_LOGORATIO, DPK_FLOAT, "Logo 比例", b);
			d_fmt(b, sizeof(b), n->qrPad);
			d_prop_add(DK_N_QRPAD, DPK_FLOAT, "留白 (模块)", b);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		default:
			break;
		}
		if (n->type != UI_DN_TABS && n->parent >= 0 &&
		    g_d.doc.nodes[n->parent].type == UI_DN_TABS) {
			UiDesignNode *tn = &g_d.doc.nodes[n->parent];
			int page = 0, i;
			for (i = 0; i < tn->childCount; i++)
				if (tn->children[i] == g_d.selNode) { page = i; break; }
			snprintf(b, sizeof(b), "%s · 第 %d 页", tn->name, page + 1);
			d_prop_add(DK_N_PAGECOUNT, DPK_READONLY, "所属页签", b);
			d_prop_add(DK_N_ADDPAGE, DPK_ENUM, "页面操作", "＋ 新增页");
		}
		g_d.propSection = 4;
		if (n->type == UI_DN_BUTTON)
			d_prop_add(DK_N_CLICK, DPK_TEXT, "OnClick", n->onClick);
		switch (n->type) {
		case UI_DN_INPUT: case UI_DN_CHECKBOX: case UI_DN_SWITCH:
		case UI_DN_SLIDER: case UI_DN_SELECT: case UI_DN_TEXTAREA:
		case UI_DN_DATEPICKER: case UI_DN_COLORSEL: case UI_DN_TABS:
			d_prop_add(DK_N_CHANGE, DPK_TEXT, "OnChange", n->onChange);
			break;
		default:
			break;
		}
	}
}

/* ── 属性修改 ───────────────────────────────────────────────────────── */

static void d_cycle_group(int dir)
{
	UiDesignHost *w = d_selwindow();
	int g;
	if (!w) return;
	g = w->group;
	g += dir;
	if (g < -1) g = g_d.doc.groupCount - 1;
	if (g >= g_d.doc.groupCount) g = -1;
	w->group = g;
	g_d.doc.dirty = 1;
}

static void d_prop_cycle(int key)
{
	UiDesignNode *n;
	UiDesignHost *w;
	if (g_d.preview) return;
	n = d_selnode();
	w = d_selwindow();
	if (g_d.selKind == DSEL_WINDOW && w) {
		if (key == DK_W_KIND) w->kind = (w->kind + 1) % 3;
		else if (key == DK_W_VIEW) {
			int v = w->view + 1;
			if (v >= g_d.doc.viewCount) v = -1;
			w->view = v;
			if (v >= 0) g_d.selView = v;
			g_d.layoutReady = 0;
			d_preview_shutdown();
		} else if (key == DK_W_GROUP) d_cycle_group(1);
		else if (key == DK_W_THEME) w->theme = w->theme == UI_THEME_DARK ? UI_THEME_LIGHT : UI_THEME_DARK;
	} else if (g_d.selKind == DSEL_VIEW && d_selview()) {
		UiDesignView *v = d_selview();
		if (key == DK_V_GROUP) {
			int g = v->group + 1;
			if (g >= g_d.doc.groupCount) g = -1;
			v->group = g;
		}
	} else if (g_d.selKind == DSEL_RADIOGROUP) {
		UiDesignRadioGroup *rg = d_selrg();
		if (!rg) return;
		if (key == DK_RG_ENABLED) {
			rg->enabled = !rg->enabled;
		} else if (key == DK_RG_SELECTED) {
			int count = d_rg_member_count(g_d.selRadioGroup);
			int cur = d_rg_selected_node(g_d.selRadioGroup);
			int ord = -1, i, next = -1;
			for (i = 0; i < count; i++) {
				int mi = d_rg_member_at(g_d.selRadioGroup, i);
				if (cur < 0) { next = mi; break; }
				if (mi == cur) { ord = i; break; }
			}
			if (ord >= 0 && count > 1)
				next = d_rg_member_at(g_d.selRadioGroup, (ord + 1) % count);
			if (next >= 0)
				UiDesign_RadioGroupSetSelectedNode(&g_d.doc,
					g_d.selRadioGroup, next);
		}
		g_d.doc.dirty = 1;
		d_undo_push();
		UiApp_Invalidate(g_d.app);
		return;
	} else if (g_d.selKind == DSEL_NODE && n) {
		if (key == DK_N_ADDPAGE) {
			d_tabs_add_page();
			return;
		}
		if (key == DK_N_WMODE) n->wMode = (n->wMode + 1) % 4;
		else if (key == DK_N_HMODE) n->hMode = (n->hMode + 1) % 4;
		else if (key == DK_N_ALIGN) n->align = (n->align + 1) % 3;
		else if (key == DK_N_TEXTALIGN) n->textAlign = (n->textAlign + 1) % 3;
		else if (key == DK_N_TEXTVALIGN) n->textVAlign = (n->textVAlign + 1) % 3;
		else if (key == DK_N_VARIANT) n->variant = (n->variant + 1) % 3;
		else if (key == DK_N_CHECKED) n->checked = !n->checked;
		else if (key == DK_N_ENABLED) n->enabled = !n->enabled;
		else if (key == DK_N_SELECTED) {
			if (n->type == UI_DN_TABS) {
				int cnt = UiDesign_TabsPageCount(&g_d.doc, g_d.selNode);
				if (cnt > 0) {
					int sel = UiDesign_TabsSelectedPage(&g_d.doc,
						g_d.selNode);
					UiDesign_TabsSetSelectedPage(&g_d.doc,
						g_d.selNode, (sel + 1) % cnt);
				}
			} else {
				int cnt = d_option_count(n->options);
				if (cnt > 0) n->selected = (n->selected + 1) % cnt;
			}
		} else if (key == DK_N_MODE) n->mode = (n->mode + 1) % 4;
		else if (key == DK_N_READONLY) n->readonly = !n->readonly;
		else if (key == DK_N_ECC) {
			n->ecc = (n->ecc + 1) % 4;
			d_status("二维码纠错: L 7% / M 15% / Q 25% / H 30%");
		}
	}
	g_d.doc.dirty = 1;
	d_undo_push();
	UiApp_Invalidate(g_d.app);
}

static void d_prop_apply_text(int key, const char *text)
{
	UiDesignNode *n = d_selnode();
	UiDesignHost *w = d_selwindow();
	UiDesignGroup *gr = d_selgroup();
	if (g_d.selKind == DSEL_WINDOW && w) {
		if (key == DK_W_ID) UiDesign_SetString(&w->id, text);
		else if (key == DK_W_NAME) UiDesign_SetString(&w->name, text);
		else if (key == DK_W_TITLE) UiDesign_SetString(&w->title, text);
		else if (key == DK_W_W) w->w = (float)d_atof(text);
		else if (key == DK_W_H) w->h = (float)d_atof(text);
		else if (key == DK_W_MINW) w->minW = (float)d_atof(text);
		else if (key == DK_W_MINH) w->minH = (float)d_atof(text);
		d_autosize_window(w);
	} else if (g_d.selKind == DSEL_VIEW && d_selview()) {
		UiDesignView *v = d_selview();
		if (key == DK_V_ID) UiDesign_SetString(&v->id, text);
		else if (key == DK_V_NAME) UiDesign_SetString(&v->name, text);
		else if (key == DK_V_W) v->w = (float)d_atof(text);
		else if (key == DK_V_H) v->h = (float)d_atof(text);
	} else if (g_d.selKind == DSEL_GROUP && gr) {
		if (key == DK_G_ID) UiDesign_SetString(&gr->id, text);
		else if (key == DK_G_NAME) UiDesign_SetString(&gr->name, text);
		else if (key == DK_G_OUTC) UiDesign_SetString(&gr->outputC, text);
		else if (key == DK_G_OUTH) UiDesign_SetString(&gr->outputH, text);
		else if (key == DK_G_SYM) UiDesign_SetString(&gr->symbolPrefix, text);
	} else if (g_d.selKind == DSEL_RADIOGROUP && d_selrg()) {
		UiDesignRadioGroup *rg = d_selrg();
		if (key == DK_RG_ID) UiDesign_SetString(&rg->id, text);
		else if (key == DK_RG_NAME) UiDesign_SetString(&rg->name, text);
		else if (key == DK_RG_CHANGE) UiDesign_SetString(&rg->onChange, text);
	} else if (g_d.selKind == DSEL_NODE && n) {
		if (key == DK_N_ID) d_node_set_id(g_d.selNode, text);
		else if (key == DK_N_NAME) UiDesign_SetString(&n->name, text);
		else if (key == DK_N_W) n->w = (float)d_atof(text);
		else if (key == DK_N_H) n->h = (float)d_atof(text);
		else if (key == DK_N_GROW) n->grow = (float)d_atof(text);
		else if (key == DK_N_GAP) n->gap = (float)d_atof(text);
		else if (key == DK_N_RADIUS) n->radius = (float)d_atof(text);
		else if (key == DK_N_PADL) n->padding[0] = (float)d_atof(text);
		else if (key == DK_N_PADT) n->padding[1] = (float)d_atof(text);
		else if (key == DK_N_PADR) n->padding[2] = (float)d_atof(text);
		else if (key == DK_N_PADB) n->padding[3] = (float)d_atof(text);
		else if (key == DK_N_X) n->x = (float)d_atof(text);
		else if (key == DK_N_Y) n->y = (float)d_atof(text);
		else if (key == DK_N_MARGINL) n->margin[0] = (float)d_atof(text);
		else if (key == DK_N_MARGINT) n->margin[1] = (float)d_atof(text);
		else if (key == DK_N_MARGINR) n->margin[2] = (float)d_atof(text);
		else if (key == DK_N_MARGINB) n->margin[3] = (float)d_atof(text);
		else if (key == DK_N_TEXT) UiDesign_NodeSetText(n, text);
		else if (key == DK_N_HINT) UiDesign_NodeSetHint(n, text);
		else if (key == DK_N_OPTIONS) UiDesign_NodeSetOptions(n, text);
		else if (key == DK_N_VALUE) {
			n->value = (float)d_atof(text);
			if (n->type == UI_DN_RINGPROGRESS)
				n->value = d_clampf(n->value, 0.0f, 1.0f);
		}
		else if (key == DK_N_CLICK) UiDesign_SetString(&n->onClick, text);
		else if (key == DK_N_CHANGE) UiDesign_SetString(&n->onChange, text);
		else if (key == DK_N_GRIDCOLS) n->gridCols = d_atoi(text) > 0 ? d_atoi(text) : 1;
		else if (key == DK_N_GRIDROWS) n->gridRows = d_atoi(text) > 0 ? d_atoi(text) : 0;
		else if (key == DK_N_GRIDROW) n->gridRow = d_atoi(text);
		else if (key == DK_N_GRIDCOL) n->gridCol = d_atoi(text);
		else if (key == DK_N_GRIDROWGAP) n->gridRowGap = (float)d_atof(text);
		else if (key == DK_N_GRIDCOLGAP) n->gridColGap = (float)d_atof(text);
		else if (key == DK_N_GRIDCOLTRACKS) {
			int tn = d_track_count(text);
			UiDesign_SetString(&n->gridColTracks, text);
			if (tn > n->gridCols) n->gridCols = tn;
			d_status("列宽: 120 固定 / 1fr 弹性 / auto 随子项, 空=均分");
		}
		else if (key == DK_N_COLSPAN) n->colSpan = d_atoi(text) > 0 ? d_atoi(text) : 1;
		else if (key == DK_N_ROWSPAN) n->rowSpan = d_atoi(text) > 0 ? d_atoi(text) : 1;
		else if (key == DK_N_COLOR) n->color = d_parse_color(text, n->color);
		else if (key == DK_N_BGCOLOR) n->bgColor = d_parse_color(text, n->bgColor);
		else if (key == DK_N_LOGOSRC) {
			UiDesign_SetString(&n->logoSrc, text);
			if (n->logoSrc[0] && n->ecc < UI_QR_ECC_H)
				d_status("Logo 会遮挡码区, 建议纠错等级 H");
		}
		else if (key == DK_N_LOGORATIO) {
			float r = (float)d_atof(text);
			if (r < 0.0f) r = 0.0f;
			if (r > UI_QR_LOGO_RATIO_MAX) r = UI_QR_LOGO_RATIO_MAX;
			n->logoRatio = r;
		}
		else if (key == DK_N_QRPAD) {
			float p = (float)d_atof(text);
			if (p < 0.0f) p = 0.0f;
			if (p > UI_QR_PAD_MAX) p = UI_QR_PAD_MAX;
			n->qrPad = p;
		}
		else if (key == DK_N_RINGMAX) {
			float m = (float)d_atof(text);
			n->ringMax = m > 0.0f ? m : 100.0f;
		}
		else if (key == DK_N_RINGWIDTH) {
			float w = (float)d_atof(text);
			if (w < 2.0f) w = 2.0f;
			if (w > 64.0f) w = 64.0f;
			n->ringWidth = w;
		}
	}
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	if (g_d.app) UiApp_Invalidate(g_d.app);
}

static void d_prop_start_edit(int row)
{
	DProp *p;
	if (row < 0 || row >= g_d.propN) return;
	p = &g_d.props[row];
	if (p->kind == DPK_READONLY || p->kind == DPK_ENUM || p->kind == DPK_BOOL) {
		if (p->kind != DPK_READONLY) d_prop_cycle(p->key);
		return;
	}
	if (p->kind == DPK_COLOR) return;
	d_after_edit();
	g_d.editActive = 1;
	g_d.editKey = p->key;
	g_d.editChanged = 0;
	g_d.editMultiline = p->kind == DPK_MULTILINE;
	if (g_d.editMultiline) {
		UiTextArea_SetTextUtf8(&g_d.propOptionsForm,
			p->valueRef ? p->valueRef : p->value);
		UiTextArea_SetCaret(&g_d.propOptionsForm, g_d.propOptionsForm.len);
		g_d.propOptionsForm.blinkOn = 1;
		Ui_SetFocusId(&g_d.app->ui, g_d.propOptionsForm.id);
		return;
	}
	g_d.propForm.focus = 0;
	g_d.propForm.blinkOn = 1;
	Edit_SetTextUtf8(UiInput_Edit(&g_d.propForm, 0),
		p->valueRef ? p->valueRef : p->value);
	if (g_d.app)
		UiIme_SetFocus(&g_d.app->ime, UiInput_Edit(&g_d.propForm, 0));
	Ui_SetFocusId(&g_d.app->ui, g_d.propForm.fields[0].id);
}

static void d_prop_apply_edit(void)
{
	char *text;
	if (!g_d.editActive) return;
	if (g_d.editMultiline)
		text = d_textarea_utf8_alloc(&g_d.propOptionsForm);
	else
		text = d_edit_utf8_alloc(UiInput_Edit(&g_d.propForm, 0));
	if (text) {
		d_prop_apply_text(g_d.editKey, text);
		HeapFree(GetProcessHeap(), 0, text);
	}
}

/* ── 操作 ───────────────────────────────────────────────────────────── */

static char *d_unique_node_id(int win, const char *base)
{
	int i;
	size_t cap = strlen(base) + 32;
	char *out = (char *)HeapAlloc(GetProcessHeap(), 0, cap);
	if (!out) return NULL;
	for (i = 1; i < 10000; i++) {
		snprintf(out, cap, "%s%d", base, i);
		if (UiDesign_FindNode(&g_d.doc, win, out) < 0) return out;
	}
	snprintf(out, cap, "%s", base);
	return out;
}

static int d_container_parent(void)
{
	UiDesignNode *n = d_selnode();
	if (n && UiDesign_IsContainer(n->type)) {
		if (n->type == UI_DN_TABS && n->childCount > 0) {
			int sel = n->selected;
			if (sel < 0) sel = 0;
			if (sel >= n->childCount) sel = n->childCount - 1;
			if (UiDesign_IsContainer(g_d.doc.nodes[n->children[sel]].type))
				return n->children[sel];
			return -1;   /* 叶子页不能作为插入容器 */
		}
		return g_d.selNode;
	}
	if (n && n->parent >= 0) return n->parent;
	return d_view_root();
}

static void d_grid_defaults(UiDesignNode *n)
{
	if (!n) return;
	if (n->gridCols < 2) n->gridCols = 2;
	n->gridRows = 0;              /* 0 = 行数随子节点自动增长 */
	if (n->gridColGap <= 0.0f) n->gridColGap = 8.0f;
	if (n->gridRowGap <= 0.0f) n->gridRowGap = 8.0f;
}

static int d_grid_cell_at(int parent, float x, float y, int *row, int *col)
{
	UiDesignNode *p = UiDesign_Node(&g_d.doc, parent);
	UiRect pr, inner;
	float *xs = NULL, *ws = NULL, cellH;
	int cols, rows, i, r, c;
	if (!p || p->type != UI_DN_GRID) return 0;
	pr = g_d.rects[parent];
	d_inset(pr, p->padding, &inner);
	cols = d_track_count(p->gridColTracks);
	if (p->gridCols > cols) cols = p->gridCols;
	if (cols < 1) cols = 1;
	xs = (float *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cols * sizeof(*xs));
	ws = (float *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cols * sizeof(*ws));
	if (!xs || !ws) { HeapFree(GetProcessHeap(), 0, xs); HeapFree(GetProcessHeap(), 0, ws); return 0; }
	cols = UiDesign_GridColumns(&g_d.doc, p, inner, xs, ws, cols);
	if (cols < 1) cols = 1;
	rows = p->gridRows > 0 ? p->gridRows
		: (d_layout_item_count(parent) + cols - 1) / cols;
	if (rows < 1) rows = 1;
	cellH = (inner.h - p->gridRowGap * (rows - 1)) / rows;
	if (cellH + p->gridRowGap <= 0.0001f) {
		HeapFree(GetProcessHeap(), 0, xs);
		HeapFree(GetProcessHeap(), 0, ws);
		return 0;
	}
	r = (int)((y - inner.y) / (cellH + p->gridRowGap));
	if (r < 0) r = 0;
	if (r >= rows) r = rows - 1;
	c = cols - 1;
	for (i = 0; i < cols; i++) {
		if (x < xs[i] + ws[i]) { c = i; break; }
	}
	if (row) *row = r;
	if (col) *col = c;
	HeapFree(GetProcessHeap(), 0, xs);
	HeapFree(GetProcessHeap(), 0, ws);
	return 1;
}

static void d_tabs_add_pages(int win, int tabsIdx);

static void d_init_added_node(int idx, int parent, int type, float x, float y)
{
	UiDesignNode *n = UiDesign_Node(&g_d.doc, idx);
	int layoutParent = d_layout_parent(idx);
	if (!n) return;
	if (UiDesign_IsContainer(type)) {
		n->wMode = UI_DN_SIZE_FILL;
		n->hMode = UI_DN_SIZE_FIXED;
		n->h = type == UI_DN_ROW ? 64.0f : 180.0f;
		n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 8.0f;
		n->gap = 8.0f;
		if (type == UI_DN_GRID) d_grid_defaults(n);
	} else {
		n->wMode = UI_DN_SIZE_FIXED;
		n->hMode = UI_DN_SIZE_FIXED;
		n->w = UiDesign_DefaultW(type);
		n->h = UiDesign_DefaultH(type);
		if (layoutParent >= 0) {
			int pt = g_d.doc.nodes[layoutParent].type;
			if (pt == UI_DN_GRID) {
				n->wMode = UI_DN_SIZE_FILL;
				n->hMode = UI_DN_SIZE_FILL;
			} else if (pt == UI_DN_COLUMN) {
				n->wMode = UI_DN_SIZE_FILL;
			} else if (pt == UI_DN_ROW) {
				n->hMode = UI_DN_SIZE_FILL;
			} else if (pt == UI_DN_PANEL && g_d.doc.nodes[layoutParent].parent >= 0 &&
				g_d.doc.nodes[g_d.doc.nodes[layoutParent].parent].type == UI_DN_TABS) {
				n->wMode = UI_DN_SIZE_FILL;
			}
		}
		if (parent >= 0) {
			UiDesignNode *p = UiDesign_Node(&g_d.doc, layoutParent >= 0 ? layoutParent : parent);
			if (p && (p->type == UI_DN_ABSOLUTE || p->type == UI_DN_PANEL)) {
				n->x = x;
				n->y = y;
			}
		}
		if (type == UI_DN_LABEL) UiDesign_NodeSetText(n, "Label");
		else if (type == UI_DN_BUTTON) UiDesign_NodeSetText(n, "Button");
		else if (type == UI_DN_INPUT) UiDesign_NodeSetHint(n, "请输入");
		else if (type == UI_DN_SELECT) {
			UiDesign_NodeSetOptions(n, "选项1,选项2,选项3");
			UiDesign_NodeSetHint(n, "请选择");
		} else if (type == UI_DN_IMAGE) UiDesign_NodeSetText(n, "Image");
		else if (type == UI_DN_TEXTAREA) UiDesign_NodeSetHint(n, "多行文本");
		else if (type == UI_DN_DATEPICKER) UiDesign_NodeSetHint(n, "选择日期");
		else if (type == UI_DN_RINGPROGRESS) {
			UiDesign_NodeSetText(n, "任务完成度");
			UiDesign_NodeSetHint(n, "%");
		}
	}
	if (type == UI_DN_TABS) {
		n->h = 220.0f;
		n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 10.0f;
		n->gap = 0.0f;
		n->selected = 0;
		d_tabs_add_pages(g_d.selView, idx);
	}
}

/* 在组末尾/父容器指定位置创建一个 Radio 成员. x/y 为设计坐标
 * (自由布局父容器下换算为相对父容器的坐标). pos<0 表示追加到组块之后. */
static int d_create_radio_node(int gi, int parent, float x, float y, int pos)
{
	UiDesignNode *p = UiDesign_Node(&g_d.doc, parent);
	UiDesignRadioGroup *rg = d_rg(gi);
	char *id;
	char text[48];
	int idx, count = d_rg_member_count(gi);
	if (!p || !rg) return -1;
	snprintf(text, sizeof(text), "选项%d", count + 1);
	id = d_unique_node_id(g_d.selView, "radio");
	if (!id) return -1;
	idx = UiDesign_AddNode(&g_d.doc, g_d.selView, UI_DN_RADIO, parent, id, text);
	HeapFree(GetProcessHeap(), 0, id);
	if (idx < 0) return -1;
	d_init_added_node(idx, parent, UI_DN_RADIO, 8.0f, 8.0f);
	UiDesign_NodeSetText(UiDesign_Node(&g_d.doc, idx), text);
	if (p->type == UI_DN_ABSOLUTE || p->type == UI_DN_PANEL) {
		UiRect pr = g_d.layoutReady ? g_d.rects[parent] : Ui_Rect(0, 0, 0, 0);
		g_d.doc.nodes[idx].x = d_round4(x - pr.x - p->padding[0]);
		g_d.doc.nodes[idx].y = d_round4(y - pr.y - p->padding[1]);
	}
	if (pos >= 0) UiDesign_NodeMove(&g_d.doc, idx, parent, pos);
	if (!UiDesign_RadioGroupAssign(&g_d.doc, idx, gi)) {
		UiDesign_NodeRemove(&g_d.doc, idx);
		return -1;
	}
	if (d_rg_selected_node(gi) < 0)
		UiDesign_RadioGroupSetSelectedNode(&g_d.doc, gi, idx);
	UiDesign_RadioGroupNormalize(&g_d.doc, gi);
	return idx;
}

/* 组在当前父容器中最后成员之后的位置 (组块尾部). */
static int d_rg_insert_pos(int gi)
{
	int parent = d_rg_member_parent(gi);
	int i, n = d_rg_member_count(gi), pos = -1;
	if (parent < 0 || parent >= g_d.doc.nodeCount) return -1;
	for (i = 0; i < n; i++) {
		int ni = d_rg_member_at(gi, i);
		int k;
		for (k = 0; k < g_d.doc.nodes[parent].childCount; k++)
			if (g_d.doc.nodes[parent].children[k] == ni) {
				if (k + 1 > pos) pos = k + 1;
				break;
			}
	}
	if (pos < 0) pos = g_d.doc.nodes[parent].childCount;
	return pos;
}

/* 追加一个 Radio 到已有组. */
static int d_append_radio(int gi, float x, float y)
{
	int parent = d_rg_member_parent(gi);
	int pos = d_rg_insert_pos(gi);
	if (parent < 0) return -1;
	return d_create_radio_node(gi, parent, x, y, pos);
}

/* 新建单选组: 元数据 + 3 个连续 Radio 直接子节点. 返回组索引. */
static int d_new_radio_group(int parent, float x, float y, int selectGroup)
{
	UiDesignNode *p = UiDesign_Node(&g_d.doc, parent);
	char name[64], *id;
	int gi, i, first = -1, pos;
	float baseX = 8.0f, baseY = 8.0f;
	if (!p || g_d.selView < 0) return -1;
	if (!UiDesign_CanParent(p->type, UI_DN_RADIO)) {
		d_status("当前容器不能放置单选组");
		return -1;
	}
	if (p->type == UI_DN_ABSOLUTE || p->type == UI_DN_PANEL) {
		if (!g_d.layoutReady) d_layout_current();
		baseX = x;
		baseY = y;
	}
	id = d_unique_rg_id(g_d.selView, "radiogroup");
	if (!id) return -1;
	snprintf(name, sizeof(name), "单选组 %d", d_rg_count() + 1);
	gi = UiDesign_RadioGroupAdd(&g_d.doc, g_d.selView, id, name);
	HeapFree(GetProcessHeap(), 0, id);
	if (gi < 0) return -1;
	pos = p->childCount;
	if (p->type != UI_DN_ABSOLUTE && p->type != UI_DN_PANEL) {
		baseX = 8.0f;
		baseY = 8.0f;
	}
	for (i = 0; i < 3; i++) {
		int idx = d_create_radio_node(gi, parent, baseX, baseY + (float)i * 28.0f,
			pos + i);
		if (idx < 0) {
			/* 回滚已创建的成员与元数据 */
			int k, n = d_rg_member_count(gi);
			for (k = n - 1; k >= 0; k--)
				UiDesign_NodeRemove(&g_d.doc, d_rg_member_at(gi, k));
			{
				int at = UiDesign_RadioGroupFind(&g_d.doc, d_rg(gi)->uid);
				if (at >= 0) UiDesign_RadioGroupRemove(&g_d.doc, at);
			}
			return -1;
		}
		if (first < 0) first = idx;
	}
	if (first >= 0)
		UiDesign_RadioGroupSetSelectedNode(&g_d.doc, gi, first);
	UiDesign_RadioGroupNormalize(&g_d.doc, gi);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	if (selectGroup) d_sel_radio_group(gi);
	else if (first >= 0) d_sel_single(first);
	d_undo_push();
	d_status("已添加单选组 · 3 个选项");
	return gi;
}

static void d_tabs_add_pages(int win, int tabsIdx)
{
	int i;
	for (i = 0; i < 2; i++) {
		char base[24], name[32];
		char *id;
		snprintf(base, sizeof(base), "page%d", i + 1);
		id = d_unique_node_id(win, base);
		if (!id) return;
		snprintf(name, sizeof(name), "页 %d", i + 1);
		UiDesign_TabsInsertPage(&g_d.doc, tabsIdx,
			UiDesign_TabsPageCount(&g_d.doc, tabsIdx), id, name);
		HeapFree(GetProcessHeap(), 0, id);
	}
	UiDesign_TabsSetSelectedPage(&g_d.doc, tabsIdx, 0);
}

/* 当前选中节点所属的 Tabs (节点自身是 Tabs 时返回自身). */
static int d_tabs_context_idx(void)
{
	int idx = g_d.selNode;
	while (idx >= 0 && idx < g_d.doc.nodeCount) {
		if (g_d.doc.nodes[idx].type == UI_DN_TABS) return idx;
		idx = g_d.doc.nodes[idx].parent;
	}
	return -1;
}

static void d_tabs_add_page(void)
{
	int tabs = d_tabs_context_idx();
	int count, pos, idx;
	char base[24], name[32];
	char *id;

	if (tabs < 0) {
		d_status("请先选中标签页或页内节点");
		return;
	}
	count = UiDesign_TabsPageCount(&g_d.doc, tabs);
	pos = UiDesign_TabsSelectedPage(&g_d.doc, tabs);
	pos = pos < 0 ? count : pos + 1;
	snprintf(base, sizeof(base), "page%d", count + 1);
	id = d_unique_node_id(g_d.selView, base);
	if (!id) return;
	snprintf(name, sizeof(name), "页 %d", count + 1);
	d_after_edit();
	idx = UiDesign_TabsInsertPage(&g_d.doc, tabs, pos, id, name);
	HeapFree(GetProcessHeap(), 0, id);
	if (idx < 0) {
		d_status("新增页失败");
		return;
	}
	d_sel_single(idx);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已新增页并切换到该页");
}

static void d_add_node(int type)
{
	int parent, idx;
	char *id;
	if (g_d.selView < 0) return;
	if (type == UI_DN_RADIO) {
		int gi = -1;
		if (g_d.selKind == DSEL_RADIOGROUP) gi = d_sel_radio_group_index();
		else if (g_d.selKind == DSEL_NODE) gi = d_node_group_index(g_d.selNode);
		if (gi < 0) {
			d_status("请先选中单选组或其选项");
			return;
		}
		d_after_edit();
		idx = d_append_radio(gi, 8.0f, 8.0f);
		if (idx < 0) { d_status("添加组件失败"); return; }
		d_sel_single(idx);
		g_d.doc.dirty = 1;
		g_d.layoutReady = 0;
		d_undo_push();
		d_status("已添加选项");
		return;
	}
	parent = d_container_parent();
	if (parent < 0) return;
	if (!UiDesign_CanParent(g_d.doc.nodes[parent].type, type)) return;
	d_after_edit();
	id = d_unique_node_id(g_d.selView, UiDesign_TypeName(type));
	if (!id) return;
	idx = UiDesign_AddNode(&g_d.doc, g_d.selView, type, parent, id,
		UiDesign_TypeName(type));
	HeapFree(GetProcessHeap(), 0, id);
	if (idx < 0) { d_status("添加组件失败"); return; }
	d_init_added_node(idx, parent, type, 8.0f, 8.0f);
	d_sel_single(idx);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已添加组件");
}

static void d_add_window(int kind)
{
	int wi, vi, gi = g_d.selGroup;
	int root;
	char id[32], name[64];
	char vid[64], vname[96];
	if (g_d.doc.groupCount <= 0) {
		UiDesign_AddGroup(&g_d.doc, "group1", "分组 1");
		gi = 0;
	}
	if (gi < 0) gi = 0;
	{
		int i;
		for (i = 1; i < 100; i++) {
			snprintf(id, sizeof(id), "win%d", i);
			if (UiDesign_FindHost(&g_d.doc, id) < 0) break;
		}
	}
	snprintf(name, sizeof(name), kind == UI_DH_DIALOG ? "Dialog %d" : "窗口 %d",
		g_d.doc.hostCount + 1);
	{
		int i;
		for (i = 1; i < 1000; i++) {
			snprintf(vid, sizeof(vid), "%s_view%d", id, i);
			if (UiDesign_FindView(&g_d.doc, vid) < 0) break;
		}
	}
	snprintf(vname, sizeof(vname), "%s 视图", name);
	vi = UiDesign_AddView(&g_d.doc, vid, vname);
	UiDesign_View(&g_d.doc, vi)->group = gi;
	wi = UiDesign_AddHost(&g_d.doc, id, name, kind, gi);
	root = UiDesign_AddNode(&g_d.doc, vi, UI_DN_COLUMN, -1, "root", "根容器");
	UiDesign_Node(&g_d.doc, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&g_d.doc, root)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&g_d.doc, root)->padding[0] = 16;
	UiDesign_Node(&g_d.doc, root)->padding[1] = 16;
	UiDesign_Node(&g_d.doc, root)->padding[2] = 16;
	UiDesign_Node(&g_d.doc, root)->padding[3] = 16;
	UiDesign_Node(&g_d.doc, root)->gap = 8;
	UiDesign_View(&g_d.doc, vi)->root = root;
	UiDesign_Host(&g_d.doc, wi)->view = vi;
	UiDesign_Host(&g_d.doc, wi)->w = 800;
	UiDesign_Host(&g_d.doc, wi)->h = 540;
	g_d.selWin = wi;
	g_d.selView = vi;
	g_d.selKind = DSEL_VIEW;
	g_d.selNode = root;
	d_sel_clear();
	if (root >= 0) d_sel_add(root);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status(kind == UI_DH_DIALOG ? "已添加虚拟 Dialog" : "已添加窗口");
}

static void d_add_group(void)
{
	int i;
	char id[32], name[64];
	for (i = 1; i < 100; i++) {
		snprintf(id, sizeof(id), "group%d", i);
		if (UiDesign_FindGroup(&g_d.doc, id) < 0) break;
	}
	snprintf(name, sizeof(name), "分组 %d", g_d.doc.groupCount + 1);
	g_d.selGroup = UiDesign_AddGroup(&g_d.doc, id, name);
	g_d.selKind = DSEL_GROUP;
	g_d.doc.dirty = 1;
	d_undo_push();
	d_status("已添加生成组");
}

static void d_del_node(void)
{
	int i, parent = -1, tabsAfter = -1, removedPage = 0;
	if (g_d.preview) return;
	d_after_edit();
	if (g_d.selSetN == 0 && g_d.selNode >= 0) d_sel_single(g_d.selNode);
	for (i = 0; i < g_d.selSetN; i++) {
		int idx = g_d.selSet[i];
		UiDesignNode *n = UiDesign_Node(&g_d.doc, idx);
		if (!n || n->parent < 0) continue;
		if (idx == g_d.selNode) parent = n->parent;
		if (g_d.doc.nodes[n->parent].type == UI_DN_TABS) {
			tabsAfter = n->parent;
			removedPage = 1;
		}
		UiDesign_NodeRemove(&g_d.doc, idx);
	}
	d_rg_prune();
	d_sel_clear();
	if (tabsAfter >= 0 && g_d.doc.nodes[tabsAfter].parent < 0)
		tabsAfter = -1;
	if (tabsAfter >= 0) {
		int page = UiDesign_TabsSelectedPage(&g_d.doc, tabsAfter);
		int node = page >= 0 ? UiDesign_TabsPageAt(&g_d.doc, tabsAfter, page)
			: tabsAfter;
		d_sel_single(node >= 0 ? node : tabsAfter);
	} else if (parent >= 0 && parent < g_d.doc.nodeCount) {
		d_sel_single(parent);
	}
	g_d.selKind = DSEL_NODE;
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status(removedPage ? "已删除页面" : "已删除组件");
}

static void d_del_window(void)
{
	int wi = g_d.selWin, i;
	if (g_d.preview || wi < 0 || wi >= g_d.doc.hostCount) return;
	d_preview_shutdown();
	HeapFree(GetProcessHeap(), 0, g_d.doc.hosts[wi].id);
	HeapFree(GetProcessHeap(), 0, g_d.doc.hosts[wi].name);
	HeapFree(GetProcessHeap(), 0, g_d.doc.hosts[wi].title);
	for (i = wi + 1; i < g_d.doc.hostCount; i++)
		g_d.doc.hosts[i - 1] = g_d.doc.hosts[i];
	g_d.doc.hostCount--;
	g_d.selWin = g_d.doc.hostCount > 0
		? (wi >= g_d.doc.hostCount ? g_d.doc.hostCount - 1 : wi) : -1;
	if (g_d.selWin >= 0) g_d.selView = g_d.doc.hosts[g_d.selWin].view;
	g_d.selKind = g_d.selView >= 0 ? DSEL_VIEW : DSEL_WINDOW;
	g_d.selNode = d_view_root();
	d_sel_clear();
	if (g_d.selNode >= 0) d_sel_add(g_d.selNode);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已删除宿主");
}

static void d_del_view(void)
{
	int vi = g_d.selView;
	if (g_d.preview || vi < 0 || vi >= g_d.doc.viewCount) return;
	d_preview_shutdown();
	UiDesign_ViewRemove(&g_d.doc, vi);
	if (g_d.selView >= g_d.doc.viewCount)
		g_d.selView = g_d.doc.viewCount - 1;
	g_d.selNode = -1;
	d_sel_clear();
	if (g_d.selView >= 0) {
		g_d.selKind = DSEL_VIEW;
		g_d.selNode = d_view_root();
		if (g_d.selNode >= 0) d_sel_add(g_d.selNode);
	} else {
		g_d.selKind = g_d.selWin >= 0 ? DSEL_WINDOW : DSEL_NODE;
	}
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已删除视图");
}

static void d_add_view(void)
{
	int vi, root, gi = g_d.selGroup;
	char id[32], name[64];
	int i;
	for (i = 1; i < 1000; i++) {
		snprintf(id, sizeof(id), "view%d", i);
		if (UiDesign_FindView(&g_d.doc, id) < 0) break;
	}
	snprintf(name, sizeof(name), "视图 %d", g_d.doc.viewCount + 1);
	vi = UiDesign_AddView(&g_d.doc, id, name);
	UiDesign_View(&g_d.doc, vi)->group = gi;
	root = UiDesign_AddNode(&g_d.doc, vi, UI_DN_COLUMN, -1, "root", "根容器");
	UiDesign_Node(&g_d.doc, root)->wMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&g_d.doc, root)->hMode = UI_DN_SIZE_FILL;
	UiDesign_Node(&g_d.doc, root)->padding[0] = 16;
	UiDesign_Node(&g_d.doc, root)->padding[1] = 16;
	UiDesign_Node(&g_d.doc, root)->padding[2] = 16;
	UiDesign_Node(&g_d.doc, root)->padding[3] = 16;
	UiDesign_Node(&g_d.doc, root)->gap = 8;
	UiDesign_View(&g_d.doc, vi)->root = root;
	g_d.selView = vi;
	g_d.selKind = DSEL_VIEW;
	g_d.selNode = root;
	d_sel_clear();
	d_sel_add(root);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已添加视图 · 可被窗口/Dialog/Panel 复用");
}

static void d_add_panel_host(void)
{
	int vi = g_d.selView, hi;
	char id[32], name[64];
	int i;
	if (vi < 0) {
		d_add_view();
		vi = g_d.selView;
	}
	for (i = 1; i < 1000; i++) {
		snprintf(id, sizeof(id), "panel%d", i);
		if (UiDesign_FindHost(&g_d.doc, id) < 0) break;
	}
	snprintf(name, sizeof(name), "Panel %d", g_d.doc.hostCount + 1);
	hi = UiDesign_AddHost(&g_d.doc, id, name, UI_DH_PANEL, g_d.selGroup);
	UiDesign_Host(&g_d.doc, hi)->view = vi;
	g_d.selWin = hi;
	g_d.selKind = DSEL_WINDOW;
	g_d.doc.dirty = 1;
	d_undo_push();
	d_status("已添加 Panel 宿主 (引用当前视图)");
}

static void d_del_group(void)
{
	int gi = g_d.selGroup, i;
	if (g_d.preview || gi < 0 || gi >= g_d.doc.groupCount) return;
	HeapFree(GetProcessHeap(), 0, g_d.doc.groups[gi].id);
	HeapFree(GetProcessHeap(), 0, g_d.doc.groups[gi].name);
	HeapFree(GetProcessHeap(), 0, g_d.doc.groups[gi].outputC);
	HeapFree(GetProcessHeap(), 0, g_d.doc.groups[gi].outputH);
	HeapFree(GetProcessHeap(), 0, g_d.doc.groups[gi].symbolPrefix);
	for (i = 0; i < g_d.doc.hostCount; i++) {
		if (g_d.doc.hosts[i].group == gi) g_d.doc.hosts[i].group = -1;
		else if (g_d.doc.hosts[i].group > gi) g_d.doc.hosts[i].group--;
	}
	for (i = 0; i < g_d.doc.viewCount; i++) {
		if (g_d.doc.views[i].group == gi) g_d.doc.views[i].group = -1;
		else if (g_d.doc.views[i].group > gi) g_d.doc.views[i].group--;
	}
	for (i = gi + 1; i < g_d.doc.groupCount; i++)
		g_d.doc.groups[i - 1] = g_d.doc.groups[i];
	g_d.doc.groupCount--;
	g_d.selGroup = g_d.doc.groupCount > 0
		? (gi >= g_d.doc.groupCount ? g_d.doc.groupCount - 1 : gi) : -1;
	g_d.doc.dirty = 1;
	d_undo_push();
	d_status("已删除生成组");
}

/* 清理已无成员的组元数据并修正选中索引. */
static void d_rg_prune(void)
{
	int i;
	for (i = g_d.doc.radioGroupCount - 1; i >= 0; i--) {
		if (UiDesign_RadioGroupMemberCount(&g_d.doc, i) > 0) {
			UiDesign_RadioGroupNormalize(&g_d.doc, i);
			continue;
		}
		UiDesign_RadioGroupRemove(&g_d.doc, i);
		if (g_d.selKind == DSEL_RADIOGROUP && g_d.selRadioGroup >= 0) {
			if (g_d.selRadioGroup == i) {
				g_d.selRadioGroup = -1;
				g_d.selKind = DSEL_NODE;
			} else if (g_d.selRadioGroup > i) g_d.selRadioGroup--;
		}
	}
}

static void d_del_selected(void)
{
	if (g_d.selKind == DSEL_WINDOW) d_del_window();
	else if (g_d.selKind == DSEL_VIEW) d_del_view();
	else if (g_d.selKind == DSEL_GROUP) d_del_group();
	else if (g_d.selKind == DSEL_RADIOGROUP) d_del_radio_group(g_d.selRadioGroup);
	else d_del_node();
}

static int d_has_selected_ancestor(int idx)
{
	int p;
	if (idx < 0 || idx >= g_d.doc.nodeCount) return 0;
	p = g_d.doc.nodes[idx].parent;
	while (p >= 0) {
		if (d_sel_has(p)) return 1;
		p = g_d.doc.nodes[p].parent;
	}
	return 0;
}

static void d_unique_all_ids(int win)
{
	int i, j;
	for (i = 0; i < g_d.doc.nodeCount; i++) {
		UiDesignNode *n = &g_d.doc.nodes[i];
		if (n->owner != win) continue;
		for (j = 0; j < i; j++) {
			if (g_d.doc.nodes[j].owner != win) continue;
			if (strcmp(g_d.doc.nodes[j].id, n->id) != 0) continue;
			{
				char *id = d_unique_node_id(win, n->id);
				if (!id) return;
				d_node_set_id(i, id);
				HeapFree(GetProcessHeap(), 0, id);
			}
			j = -1;
		}
	}
}

static void d_dup_radio_group(void)
{
	int gi = d_sel_radio_group_index();
	UiDesignRadioGroup *rg = d_rg(gi);
	int parent, start = 0, count, i, first = -1, ngi, after;
	int *members, origSel;
	char *id, *onChange;
	char name[96];
	int enabled;
	if (!rg) return;
	parent = d_rg_member_parent(gi);
	count = d_rg_member_count(gi);
	if (parent < 0 || count <= 0) return;
	d_after_edit();
	origSel = d_rg_selected_node(gi);
	enabled = rg->enabled;
	onChange = rg->onChange[0] ? d_strdup(rg->onChange) : NULL;
	snprintf(name, sizeof(name), "%s 副本", rg->name);
	members = (int *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)count * sizeof(*members));
	if (!members) {
		HeapFree(GetProcessHeap(), 0, onChange);
		return;
	}
	for (i = 0; i < count; i++) members[i] = d_rg_member_at(gi, i);
	if (d_rg_block_span(gi, NULL, &start, &count))
		after = start + count;
	else
		after = g_d.doc.nodes[parent].childCount;
	id = d_unique_rg_id(g_d.selView, "radiogroup");
	if (!id) {
		HeapFree(GetProcessHeap(), 0, members);
		HeapFree(GetProcessHeap(), 0, onChange);
		return;
	}
	ngi = UiDesign_RadioGroupAdd(&g_d.doc, g_d.selView, id, name);
	HeapFree(GetProcessHeap(), 0, id);
	if (ngi < 0) {
		HeapFree(GetProcessHeap(), 0, members);
		HeapFree(GetProcessHeap(), 0, onChange);
		return;
	}
	{
		UiDesignRadioGroup *ng = d_rg(ngi);
		if (onChange) UiDesign_SetString(&ng->onChange, onChange);
		ng->enabled = enabled;
	}
	HeapFree(GetProcessHeap(), 0, onChange);
	for (i = 0; i < count; i++) {
		int dup = UiDesign_NodeDuplicate(&g_d.doc, members[i]);
		if (dup < 0) continue;
		UiDesign_NodeMove(&g_d.doc, dup, parent, after + i);
		if (!UiDesign_RadioGroupAssign(&g_d.doc, dup, ngi)) continue;
		if (members[i] == origSel && first < 0) first = dup;
	}
	HeapFree(GetProcessHeap(), 0, members);
	d_unique_all_ids(g_d.selView);
	UiDesign_RadioGroupNormalize(&g_d.doc, ngi);
	if (first >= 0)
		UiDesign_RadioGroupSetSelectedNode(&g_d.doc, ngi, first);
	d_sel_radio_group(ngi);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已复制单选组");
}

static void d_dup_node(void)
{
	int i, idx, dup, count = 0, last = -1;
	int *sel;
	if (g_d.selKind == DSEL_RADIOGROUP) {
		d_dup_radio_group();
		return;
	}
	if (g_d.selKind != DSEL_NODE) return;
	d_after_edit();
	if (g_d.selSetN <= 1) {
		if (!d_selnode() || d_selnode()->parent < 0) return;
		dup = UiDesign_NodeDuplicate(&g_d.doc, g_d.selNode);
		if (dup < 0) return;
		if (g_d.doc.nodes[dup].type == UI_DN_RADIO)
			UiDesign_RadioGroupAssign(&g_d.doc, dup, -1);
		d_unique_all_ids(g_d.selView);
		g_d.selNode = dup;
		g_d.selKind = DSEL_NODE;
		d_sel_single(dup);
		g_d.doc.dirty = 1;
		g_d.layoutReady = 0;
		d_undo_push();
		d_status("已复制组件");
		return;
	}
	sel = (int *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)g_d.selSetN * sizeof(*sel));
	if (!sel) return;
	for (i = 0; i < g_d.selSetN; i++) sel[i] = g_d.selSet[i];
	for (i = 0; i < g_d.selSetN; i++) {
		UiDesignNode *sn;
		idx = sel[i];
		if (idx < 0 || idx >= g_d.doc.nodeCount) continue;
		sn = &g_d.doc.nodes[idx];
		if (sn->parent < 0 || d_has_selected_ancestor(idx)) continue;
		dup = UiDesign_NodeDuplicate(&g_d.doc, idx);
		if (dup < 0) continue;
		if (g_d.doc.nodes[dup].type == UI_DN_RADIO)
			UiDesign_RadioGroupAssign(&g_d.doc, dup, -1);
		sel[count++] = dup;
		last = dup;
	}
	if (count <= 0) {
		HeapFree(GetProcessHeap(), 0, sel);
		return;
	}
	d_unique_all_ids(g_d.selView);
	d_sel_clear();
	for (i = 0; i < count; i++) d_sel_add(sel[i]);
	HeapFree(GetProcessHeap(), 0, sel);
	g_d.selNode = last;
	g_d.selKind = DSEL_NODE;
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已复制多个组件");
}

static void d_move_node(int dir)
{
	if (g_d.selKind == DSEL_RADIOGROUP) {
		d_after_edit();
		if (d_rg_block_move(g_d.selRadioGroup, dir)) {
			g_d.doc.dirty = 1;
			g_d.layoutReady = 0;
			d_undo_push();
			d_status("单选组顺序已调整");
		}
		return;
	}
	if (g_d.selKind != DSEL_NODE || !d_selnode()) return;
	d_after_edit();
	if (dir ? UiDesign_NodeMoveDown(&g_d.doc, g_d.selNode)
		: UiDesign_NodeMoveUp(&g_d.doc, g_d.selNode)) {
		g_d.doc.dirty = 1;
		g_d.layoutReady = 0;
		d_undo_push();
	}
}

static void d_select_window(int wi)
{
	if (wi < 0 || wi >= g_d.doc.hostCount) return;
	g_d.selWin = wi;
	g_d.selView = g_d.doc.hosts[wi].view;
	g_d.selKind = DSEL_WINDOW;
	g_d.selNode = d_view_root();
	d_sel_clear();
	if (g_d.selNode >= 0) d_sel_add(g_d.selNode);
	g_d.layoutReady = 0;
	d_preview_shutdown();
}

static void d_select_view(int vi)
{
	int i, host = -1;
	if (vi < 0 || vi >= g_d.doc.viewCount) return;
	g_d.selView = vi;
	g_d.selKind = DSEL_VIEW;
	g_d.selNode = -1;
	d_sel_clear();
	/* 预览跟随一个引用该视图的宿主. */
	if (g_d.selWin < 0 || g_d.selWin >= g_d.doc.hostCount ||
	    g_d.doc.hosts[g_d.selWin].view != vi) {
		for (i = 0; i < g_d.doc.hostCount; i++)
			if (g_d.doc.hosts[i].view == vi) { host = i; break; }
		if (host >= 0) g_d.selWin = host;
	}
	g_d.layoutReady = 0;
	d_preview_shutdown();
}

static void d_select_node(int idx)
{
	if (idx < 0 || idx >= g_d.doc.nodeCount) return;
	d_sel_single(idx);
}

/* ── 静态预览绘制 ───────────────────────────────────────────────────── */

static int d_tabs_geom(int idx, UiTabBarGeometry *geo, UiTabStyle *stl,
	const char **labels)
{
	UiDesignNode *n;
	UiRect inner;
	int i, cnt;
	if (idx < 0 || idx >= g_d.doc.nodeCount) return 0;
	n = &g_d.doc.nodes[idx];
	if (n->type != UI_DN_TABS) return 0;
	cnt = n->childCount;
	if (cnt <= 0 || !labels) return 0;
	for (i = 0; i < cnt; i++)
		labels[i] = g_d.doc.nodes[n->children[i]].name;
	if (n->variant == 1) *stl = UiTabStyle_Pill();
	else if (n->variant == 2) *stl = UiTabStyle_Card();
	else *stl = UiTabStyle_Line();
	d_inset(g_d.rects[idx], n->padding, &inner);
	return UiTabBar_Measure(&g_d.app->ui, inner, labels, NULL, stl, cnt, geo);
}

static int d_tabs_item_at(int idx, float x, float y)
{
	UiTabBarGeometry geo;
	UiTabStyle stl;
	const char **labels;
	int i, cnt, found = -1;
	if (idx < 0 || idx >= g_d.doc.nodeCount) return -1;
	ZeroMemory(&geo, sizeof(geo));
	labels = (const char **)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)g_d.doc.nodes[idx].childCount * sizeof(*labels));
	if (!labels) return -1;
	cnt = d_tabs_geom(idx, &geo, &stl, labels);
	if (cnt > 0 && Ui_RectContains(geo.header, x, y)) {
		for (i = 0; i < cnt; i++)
			if (Ui_RectContains(geo.items[i], x, y)) { found = i; break; }
	}
	UiTabBarGeometry_Free(&geo);
	HeapFree(GetProcessHeap(), 0, labels);
	return found;
}

/* activePass=0 画非活动页签与 Line/Pill 活动态; Card 活动页签在页面内容
 * 之后用 activePass=1 覆盖, 与运行时 DrawActive 的两阶段顺序一致. */
static void d_tabs_draw_header(int idx, int activePass)
{
	UiDesignNode *n;
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiTabBarGeometry geo;
	UiTabStyle stl;
	const char **labels;
	UiColor cBorder = t->neutral.border;
	UiColor cCardBg = t->neutral.soft;
	UiColor cActiveBg = t->neutral.base;
	int i, cnt, variant, sel;
	float radius;

	if (idx < 0 || idx >= g_d.doc.nodeCount) return;
	n = &g_d.doc.nodes[idx];
	ZeroMemory(&geo, sizeof(geo));
	labels = (const char **)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)g_d.doc.nodes[idx].childCount * sizeof(*labels));
	if (!labels) return;
	cnt = d_tabs_geom(idx, &geo, &stl, labels);
	if (cnt <= 0) {
		UiTabBarGeometry_Free(&geo);
		HeapFree(GetProcessHeap(), 0, labels);
		return;
	}
	variant = geo.variant;
	sel = n->selected;
	if (sel < 0 || sel >= n->childCount) sel = 0;
	if (activePass && variant != UI_TAB_CARD) {
		UiTabBarGeometry_Free(&geo);
		HeapFree(GetProcessHeap(), 0, labels);
		return;
	}
	radius = stl.radius > 0.0f ? stl.radius : 8.0f;
	Gfx_PushClip(g, geo.header);
	if (!activePass && variant == UI_TAB_PILL)
		Gfx_FillRoundRect(g, geo.header, radius, t->neutral.soft);
	for (i = 0; i < cnt; i++) {
		UiRect item = geo.items[i];
		int isSel = i == sel;
		if (variant == UI_TAB_CARD) {
			if (isSel != (activePass != 0)) continue;
			if (isSel) {
				GfxCorners corners = Gfx_Corners(radius, radius, 0, 0);
				Gfx_FillRoundRectCorners(g,
					Ui_Rect(item.x, item.y, item.w, item.h + 1.0f),
					corners, t->card);
				Gfx_StrokeRoundRectOpen(g, item, corners, 1u | 2u | 8u,
					cBorder, 1.0f);
			} else {
				GfxCorners corners = Gfx_Corners(radius, radius, 0, 0);
				Gfx_FillRoundRectCorners(g, item, corners, cCardBg);
				Gfx_StrokeRoundRectOpen(g, item, corners, 1u | 2u | 8u,
					cBorder, 1.0f);
			}
			Gfx_Text(g, t->fLabel, labels[i], item,
				isSel ? t->primary.base : t->text2,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		} else if (variant == UI_TAB_PILL) {
			UiRect ir = Ui_RectInset(item, 2.0f);
			if (isSel) {
				Gfx_FillRoundRect(g, ir, radius, cActiveBg);
				Gfx_StrokeRoundRect(g, ir, radius, cBorder, 1.0f);
			}
			Gfx_Text(g, t->fLabel, labels[i], item,
				isSel ? t->primary.base : t->text2,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		} else {
			Gfx_Text(g, t->fLabel, labels[i], item,
				isSel ? t->primary.base : t->text2,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			if (isSel)
				Gfx_FillRect(g, Ui_Rect(item.x + 8.0f,
					item.y + item.h - 2.0f, item.w - 16.0f, 2.0f),
					t->primary.base);
		}
	}
	if (!activePass && variant == UI_TAB_LINE)
		Gfx_Line(g, geo.header.x, geo.header.y + geo.header.h - 1,
			geo.header.x + geo.header.w, geo.header.y + geo.header.h - 1,
			t->border, 1.0f);
	Gfx_PopClip(g);
	UiTabBarGeometry_Free(&geo);
	HeapFree(GetProcessHeap(), 0, labels);
}

/* 设计画布上的二维码预览: 每个节点复用一份编码/纹理, 避免逐帧重编码. */
#define D_QR_CACHE 8
static UiQrCode g_qrSlot[D_QR_CACHE];
static int g_qrSlotNode[D_QR_CACHE];
static int g_qrNext;

static UiQrCode *d_qr_slot(int idx)
{
	int i, slot = -1;
	for (i = 0; i < D_QR_CACHE; i++) {
		if (g_qrSlotNode[i] == idx) return &g_qrSlot[i];
		if (slot < 0 && g_qrSlotNode[i] < 0) slot = i;
	}
	if (slot < 0) {
		slot = g_qrNext++ % D_QR_CACHE;
		UiQrCode_Shutdown(&g_qrSlot[slot]);
	}
	UiQrCode_Init(&g_qrSlot[slot], UI_QR_ECC_M);
	g_qrSlotNode[slot] = idx;
	return &g_qrSlot[slot];
}

static void d_qr_slots_free(void)
{
	int i;
	for (i = 0; i < D_QR_CACHE; i++) {
		UiQrCode_Shutdown(&g_qrSlot[i]);
		g_qrSlotNode[i] = -1;
	}
	g_qrNext = 0;
}

static void d_ring_trim_zeros(char *b)
{
	int n = (int)strlen(b);
	while (n > 0 && b[n - 1] == '0') b[--n] = 0;
	if (n > 0 && b[n - 1] == '.') b[--n] = 0;
	if (n == 2 && b[0] == '-' && b[1] == '0') b[1] = 0;
}

static void d_draw_ring_node(UiDesignNode *n, UiRect rc)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiRect ringRc, titleRc, valueRc, unitRc;
	GfxFont *vf;
	GfxStrokeStyle st;
	char text[64];
	float titleH, gap, side, stroke, radius, ratio, max, opening;
	float inner, valueH, unitH, totalH, top, cx, cy;
	unsigned col;

	if (!n || rc.w <= 0.0f || rc.h <= 0.0f) return;
	titleH = n->text[0] ? Gfx_TextHeight(g, t->fSmall) : 0.0f;
	gap = n->text[0] ? 6.0f : 0.0f;
	side = rc.w < rc.h - titleH - gap ? rc.w : rc.h - titleH - gap;
	if (side < 0.0f) side = 0.0f;
	ringRc = Ui_Rect(rc.x + (rc.w - side) * 0.5f, rc.y, side, side);
	titleRc = Ui_Rect(rc.x, rc.y + side + gap, rc.w, titleH);
	ratio = d_clampf(n->value, 0.0f, 1.0f);
	max = n->ringMax > 0.0f ? n->ringMax : 100.0f;
	stroke = n->ringWidth;
	if (stroke < 2.0f) stroke = 2.0f;
	if (stroke > 64.0f) stroke = 64.0f;
	if (stroke > side * 0.5f) stroke = side * 0.5f;
	radius = (side - stroke) * 0.5f;
	col = n->color ? n->color : t->primary.base;
	cx = ringRc.x + side * 0.5f;
	cy = ringRc.y + side * 0.5f;
	if (side > 0.0f && stroke > 0.0f) {
		Gfx_StrokeEllipse(g, cx, cy, radius, t->progressTrack, stroke);
		if (ratio > 0.0f) {
			if (ratio >= 0.99999f)
				Gfx_StrokeEllipse(g, cx, cy, radius, col, stroke);
			else {
				st.cap = n->checked ? GFX_CAP_ROUND : GFX_CAP_BUTT;
				st.join = GFX_JOIN_ROUND;
				st.miterLimit = 4.0f;
				st.dashes = NULL;
				st.dashCount = 0;
				Gfx_StrokeArcStyle(g, cx, cy, radius, -90.0f,
					ratio * 360.0f, col, stroke, &st);
			}
		}
	}
	snprintf(text, sizeof(text), "%.2f", ratio * max);
	d_ring_trim_zeros(text);
	opening = side - stroke * 2.0f;
	inner = opening - 10.0f;
	if (inner < opening * 0.5f) inner = opening * 0.5f;
	if (inner < 1.0f) inner = 1.0f;
	vf = side >= 150.0f ? t->fTempBig : (side >= 100.0f ? t->fTemp : t->fBody);
	valueH = Gfx_TextHeight(g, vf);
	unitH = n->hint[0] ? Gfx_TextHeight(g, t->fSmall) : 0.0f;
	totalH = valueH + unitH + (unitH > 0.0f ? 2.0f : 0.0f);
	top = cy - totalH * 0.5f;
	valueRc = Ui_Rect(ringRc.x + (side - inner) * 0.5f, top, inner, valueH);
	Gfx_Text(g, vf, text, valueRc, t->text,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	if (unitH > 0.0f) {
		unitRc = Ui_Rect(valueRc.x, top + valueH + 2.0f, inner, unitH);
		Gfx_Text(g, t->fSmall, n->hint, unitRc, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	if (titleH > 0.0f)
		Gfx_Text(g, t->fSmall, n->text, titleRc, t->text2,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void d_draw_radio(Gfx *g, UiRect r, const UiDesignNode *n,
	const UiTheme *t)
{
	int idx = (int)(n - g_d.doc.nodes);
	int gi = d_node_group_index(idx);
	UiDesignRadioGroup *rg = d_rg(gi);
	int selected = rg && rg->selectedRadioId[0] &&
		strcmp(rg->selectedRadioId, n->id) == 0;
	int enabled = n->enabled && (!rg || rg->enabled);
	float cx = r.x + 9.0f, cy = r.y + r.h * 0.5f;
	UiColor fg = enabled ? t->text2 : t->btnDisabledText;
	UiColor bd = enabled ? (selected ? t->primary.base : t->border)
		: t->btnDisabledText;
	Gfx_StrokeEllipse(g, cx, cy, 8.0f, bd, 1.2f);
	if (selected)
		Gfx_FillEllipse(g, cx, cy, 4.0f, 4.0f,
			enabled ? t->primary.base : t->btnDisabledText);
	Gfx_Text(g, t->fBody, n->text,
		Ui_Rect(r.x + 26.0f, r.y, r.w - 26.0f, r.h), fg,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void d_draw_static_node(int idx)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiDesignNode *n = &g_d.doc.nodes[idx];
	UiRect r = g_d.rects[idx];
	int i;
	if (!n->visible || r.w <= 0 || r.h <= 0) return;
	if (n->type == UI_DN_TABS) {
		UiTabBarGeometry geo;
		UiTabStyle stl;
		const char **labels;
		UiRect inner, page;
		int cnt;
		ZeroMemory(&geo, sizeof(geo));
		labels = (const char **)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)n->childCount * sizeof(*labels));
		cnt = labels ? d_tabs_geom(idx, &geo, &stl, labels) : 0;
		if (cnt > 0) {
			page = Ui_Rect(geo.header.x, geo.header.y + geo.header.h,
				geo.header.w, geo.inner.h - geo.header.h);
			if (page.h < 0.0f) page.h = 0.0f;
			if (geo.variant == UI_TAB_CARD && page.w > 0.0f && page.h > 0.0f) {
				Gfx_FillRect(g, page, t->card);
				Gfx_StrokeRoundRectCornersDashed(g, page,
					Gfx_Corners(0, 0, 8, 8), Ui_ColorAlpha(t->text3, 75), 1.0f);
			}
			d_tabs_draw_header(idx, 0);
			{
				int sel = n->selected;
				if (sel < 0 || sel >= n->childCount) sel = 0;
				if (n->childCount > 0) d_draw_static_node(n->children[sel]);
			}
			d_tabs_draw_header(idx, 1);
		} else {
			float hh;
			d_inset(r, n->padding, &inner);
			hh = d_tabs_header_h(n->variant);
			if (hh > inner.h) hh = inner.h;
			Gfx_Text(g, t->fSmall, "标签页: 无子页 · 属性页新增",
				Ui_Rect(inner.x, inner.y + hh, inner.w, inner.h - hh),
				t->text4, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		}
		UiTabBarGeometry_Free(&geo);
		HeapFree(GetProcessHeap(), 0, labels);
		return;
	}
	if (UiDesign_IsContainer(n->type)) {
		if (n->type == UI_DN_PANEL)
			Gfx_FillRoundRect(g, r, n->radius > 0.0f ? n->radius : 0.0f,
				t->card);
		else if (n->type != UI_DN_COLUMN && n->type != UI_DN_ROW) {
			Gfx_StrokeRoundRectCornersDashed(g, r, Gfx_Corners(0, 0, 0, 0),
				Ui_ColorAlpha(t->text3, 75), 1.0f);
		}
		for (i = 0; i < n->childCount; i++) d_draw_static_node(n->children[i]);
		return;
	}
	switch (n->type) {
	case UI_DN_LABEL: {
		uint32_t fl = GFX_TEXT_ELLIPSIS;
		UiRect tr = r;
		if (n->textAlign == UI_DN_ALIGN_CENTER) fl |= GFX_TEXT_CENTER;
		else if (n->textAlign == UI_DN_ALIGN_END) fl |= GFX_TEXT_RIGHT;
		if (n->textVAlign == UI_DN_VALIGN_CENTER) fl |= GFX_TEXT_VCENTER;
		else if (n->textVAlign == UI_DN_VALIGN_BOTTOM) {
			float th = Gfx_TextHeight(g, t->fBody);
			if (th < tr.h) {
				tr.y += tr.h - th;
				tr.h = th;
			}
		}
		Gfx_Text(g, t->fBody, n->text, tr, n->enabled ? t->text : t->text4, fl);
		break;
	}
	case UI_DN_BUTTON: {
		UiColor bg = n->variant == 1 ? t->primary.base
			: (n->variant == 2 ? t->error.base : t->btnBg);
		UiColor fg = n->variant == 1 || n->variant == 2 ? t->primary.text : t->text;
		Gfx_FillRoundRect(g, r, 6.0f, bg);
		Gfx_StrokeRoundRect(g, Ui_RectInset(r, 0.5f), 5.5f,
			n->variant == 0 ? t->border : bg, 1.0f);
		Gfx_Text(g, t->fBtn, n->text, r, fg,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		break;
	}
	case UI_DN_INPUT:
	case UI_DN_SELECT: {
		const char *txt = n->text[0] ? n->text : n->hint;
		Gfx_FillRoundRect(g, r, 6.0f, t->inputBg);
		Gfx_StrokeRoundRect(g, r, 6.0f, t->inputBorder, 1.0f);
		Gfx_Text(g, t->fInput, txt, Ui_Rect(r.x + 8, r.y, r.w - 28, r.h),
			txt[0] ? t->text : t->text4, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (n->type == UI_DN_SELECT) {
			float cx = r.x + r.w - 14.0f, cy = r.y + r.h * 0.5f;
			Gfx_Line(g, cx - 4, cy - 2, cx, cy + 2, t->text3, 1.4f);
			Gfx_Line(g, cx, cy + 2, cx + 4, cy - 2, t->text3, 1.4f);
		}
		break;
	}
	case UI_DN_CHECKBOX: {
		UiRect box = Ui_Rect(r.x + 2.0f, r.y + (r.h - 16.0f) * 0.5f, 16, 16);
		Gfx_FillRoundRect(g, box, 3.0f, n->checked ? t->primary.base : t->inputBg);
		Gfx_StrokeRoundRect(g, box, 3.0f, n->checked ? t->primary.base : t->inputBorder, 1.0f);
		if (n->checked) {
			Gfx_Line(g, box.x + 4, box.y + 8, box.x + 7, box.y + 11, t->primary.text, 1.8f);
			Gfx_Line(g, box.x + 7, box.y + 11, box.x + 12, box.y + 5, t->primary.text, 1.8f);
		}
		Gfx_Text(g, t->fBody, n->text, Ui_Rect(box.x + 22, r.y, r.w - 22, r.h),
			n->enabled ? t->text : t->text4, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		break;
	}
	case UI_DN_RADIO:
		d_draw_radio(g, r, n, t);
		break;
	case UI_DN_SWITCH: {
		UiRect tr = Ui_Rect(r.x, r.y + (r.h - 20) * 0.5f, 38, 20);
		Gfx_FillRoundRect(g, tr, 10.0f, n->checked ? t->switchOn : t->switchOff);
		Gfx_FillEllipse(g, tr.x + (n->checked ? 28.0f : 10.0f), tr.y + 10,
			7, 7, t->neutral.base);
		Gfx_Text(g, t->fBody, n->text, Ui_Rect(tr.x + 46, r.y, r.w - 46, r.h),
			t->text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		break;
	}
	case UI_DN_SLIDER: {
		float y = r.y + r.h * 0.5f;
		float x = r.x + 8 + (r.w - 16) * d_clampf(n->value, 0.0f, 1.0f);
		Gfx_Line(g, r.x + 8, y, r.x + r.w - 8, y, t->border, 3.0f);
		Gfx_Line(g, r.x + 8, y, x, y, t->primary.base, 3.0f);
		Gfx_FillEllipse(g, x, y, 7, 7, t->primary.base);
		break;
	}
	case UI_DN_PROGRESS: {
		UiRect tr = Ui_Rect(r.x, r.y + (r.h - 8) * 0.5f, r.w, 8);
		Gfx_FillRoundRect(g, tr, 4.0f, t->progressTrack);
		Gfx_FillRoundRect(g, Ui_Rect(tr.x, tr.y, tr.w * d_clampf(n->value, 0, 1), tr.h),
			4.0f, t->primary.base);
		break;
	}
	case UI_DN_IMAGE: {
		float rad = n->radius > 0.0f ? n->radius : 0.0f;
		Gfx_FillRoundRect(g, r, rad, Ui_ColorMix(t->card, t->primary.soft, 0.35f));
		Gfx_StrokeRoundRect(g, r, rad, t->border, 1.0f);
		Gfx_Text(g, t->fSmall, n->text[0] ? n->text : "Image", r, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		break;
	}
	case UI_DN_TEXTAREA: {
		uint32_t fl = GFX_TEXT_WRAP | (n->align == UI_DN_ALIGN_CENTER ? GFX_TEXT_CENTER : 0);
		const char *txt = n->text[0] ? n->text : (n->hint[0] ? n->hint : "多行文本");
		Gfx_FillRoundRect(g, r, 6.0f, t->inputBg);
		Gfx_StrokeRoundRect(g, r, 6.0f, t->inputBorder, 1.0f);
		Gfx_PushClip(g, Ui_Rect(r.x + 8, r.y + 6, r.w - 16, r.h - 12));
		Gfx_Text(g, t->fBody, txt, Ui_Rect(r.x + 8, r.y + 6, r.w - 16, r.h - 12),
			n->text[0] ? t->text : t->text4, fl);
		Gfx_PopClip(g);
		break;
	}
	case UI_DN_DATEPICKER: {
		const char *txt = n->text[0] ? n->text : (n->hint[0] ? n->hint : "选择日期");
		float cx = r.x + r.w - 16.0f, cy = r.y + r.h * 0.5f;
		Gfx_FillRoundRect(g, r, 6.0f, t->inputBg);
		Gfx_StrokeRoundRect(g, r, 6.0f, t->inputBorder, 1.0f);
		Gfx_Text(g, t->fInput, txt, Ui_Rect(r.x + 8, r.y, r.w - 36, r.h),
			n->text[0] ? t->text : t->text4, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		Gfx_StrokeRoundRect(g, Ui_Rect(cx - 6, cy - 7, 14, 14), 2.0f,
			t->text3, 1.2f);
		Gfx_Line(g, cx - 9, cy - 3, cx + 5, cy - 3, t->text3, 1.2f);
		break;
	}
	case UI_DN_COLORSEL: {
		UiColor c = n->color;
		UiRect sw = Ui_Rect(r.x + 5, r.y + 5, 24, r.h - 10);
		Gfx_FillRoundRect(g, r, 6.0f, t->inputBg);
		Gfx_StrokeRoundRect(g, r, 6.0f, t->inputBorder, 1.0f);
		Gfx_FillRoundRect(g, sw, 4.0f, 0xFF000000u | (c & 0x00FFFFFFu));
		Gfx_StrokeRoundRect(g, sw, 4.0f, Ui_ColorAlpha(t->text3, 90), 1.0f);
		{
			char hx[16];
			snprintf(hx, sizeof(hx), "#%02X%02X%02X",
				(c >> 16) & 0xFFu, (c >> 8) & 0xFFu, c & 0xFFu);
			Gfx_Text(g, t->fInput, hx, Ui_Rect(sw.x + sw.w + 8, r.y,
				r.w - sw.w - 16, r.h), t->text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		}
		break;
	}
	case UI_DN_SEPARATOR:
		Gfx_Line(g, r.x, r.y + r.h * 0.5f, r.x + r.w, r.y + r.h * 0.5f,
			t->border, 1.0f);
		break;
	case UI_DN_RINGPROGRESS:
		d_draw_ring_node(n, r);
		break;
	case UI_DN_QRCODE: {
		UiQrCode *q = d_qr_slot(idx);
		UiContext *uic = &g_d.app->ui;
		float zs = uic->scale;
		UiQrCode_SetEcc(q, n->ecc);
		UiQrCode_SetColors(q, n->color, n->bgColor);
		UiQrCode_SetText(q, n->text);
		UiQrCode_SetLogoSrc(q, n->logoSrc);
		UiQrCode_SetLogoRatio(q, n->logoRatio);
		UiQrCode_SetPadding(q, n->qrPad);
		/* 画布有缩放变换: 纹理按实际屏幕像素生成, 放大时不糊 */
		uic->scale = zs * (g_d.zoom > 0.0f ? g_d.zoom : 1.0f);
		UiQrCode_Draw(q, uic, r);
		uic->scale = zs;
		break;
	}
	default:
		break;
	}
}

static void d_draw_handles(int idx)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiRect r = g_d.rects[idx];
	float s = 7.0f;
	float xs[3], ys[3];
	int i, j;
	xs[0] = r.x - s * 0.5f; xs[1] = r.x + r.w * 0.5f - s * 0.5f; xs[2] = r.x + r.w - s * 0.5f;
	ys[0] = r.y - s * 0.5f; ys[1] = r.y + r.h * 0.5f - s * 0.5f; ys[2] = r.y + r.h - s * 0.5f;
	Gfx_StrokeRoundRect(g, Ui_Rect(r.x - 1, r.y - 1, r.w + 2, r.h + 2),
		2.0f, t->primary.base, 1.5f);
	for (j = 0; j < 3; j++)
		for (i = 0; i < 3; i++) {
			if (i == 1 && j == 1) continue;
			Gfx_FillRect(g, Ui_Rect(xs[i], ys[j], s, s), t->card);
			Gfx_StrokeRoundRect(g, Ui_Rect(xs[i], ys[j], s, s), 0.0f,
				t->primary.base, 1.0f);
		}
}

static void d_layout_current(void)
{
	if (g_d.selView < 0 || g_d.selView >= g_d.doc.viewCount) return;
	if (!g_d.rects || g_d.rectCount < g_d.doc.nodeCount) {
		UiRect *np;
		if (g_d.rects)
			np = (UiRect *)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
				g_d.rects, (SIZE_T)g_d.doc.nodeCount * sizeof(UiRect));
		else
			np = (UiRect *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
				(SIZE_T)g_d.doc.nodeCount * sizeof(UiRect));
		if (!np) return;
		g_d.rects = np;
		g_d.rectCount = g_d.doc.nodeCount;
	}
	UiDesign_Layout(&g_d.doc, g_d.selView, g_d.winRc, g_d.rects, g_d.rectCount);
	g_d.layoutReady = 1;
}

/* ── 画布/面板 ──────────────────────────────────────────────────────── */

static UiRect d_canvas_area(void)
{
	return Ui_Rect(LEFT_W + GAP * 2.0f, TOP_H + PALETTE_H + GAP,
		g_d.app->w - LEFT_W - RIGHT_W - GAP * 4.0f,
		g_d.app->h - TOP_H - PALETTE_H - STATUS_H - GAP * 2.0f);
}

static void d_draw_grid_cells(const UiDesignNode *n, UiRect inner, GfxColor c)
{
	Gfx *g = g_d.app->gfx;
	float *xs = NULL, *ws = NULL;
	int cols = n->gridCols > d_track_count(n->gridColTracks)
		? n->gridCols : d_track_count(n->gridColTracks);
	int rows, i;
	if (cols > 0) {
		xs = (float *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cols * sizeof(*xs));
		ws = (float *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cols * sizeof(*ws));
		if (!xs || !ws) {
			HeapFree(GetProcessHeap(), 0, xs);
			HeapFree(GetProcessHeap(), 0, ws);
			return;
		}
		cols = UiDesign_GridColumns(&g_d.doc, n, inner, xs, ws, cols);
	}
	if (cols < 1) cols = 1;
	rows = n->gridRows > 0 ? n->gridRows
		: (d_layout_item_count((int)(n - g_d.doc.nodes)) + cols - 1) / cols;
	if (rows < 1) rows = 1;
	{
		float rh = (inner.h - n->gridRowGap * (rows - 1)) / rows;
		for (i = 1; i < cols; i++)
			Gfx_Line(g, xs[i] - n->gridColGap * 0.5f, inner.y,
				xs[i] - n->gridColGap * 0.5f, inner.y + inner.h, c, 1.0f);
		for (i = 1; i < rows; i++)
			Gfx_Line(g, inner.x, inner.y + i * (rh + n->gridRowGap),
				inner.x + inner.w, inner.y + i * (rh + n->gridRowGap), c, 1.0f);
	}
	HeapFree(GetProcessHeap(), 0, xs);
	HeapFree(GetProcessHeap(), 0, ws);
}

static void d_draw_layout_guides(void)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiDesignNode *n = d_selnode();
	UiRect nr, inner;
	char b[160];
	int layoutParent;
	if (!n || !g_d.layoutReady) return;
	nr = g_d.rects[g_d.selNode];
	layoutParent = d_layout_parent(g_d.selNode);
	if (layoutParent >= 0 &&
	    UiDesign_IsLayoutContainer(g_d.doc.nodes[layoutParent].type)) {
		UiDesignNode *p = &g_d.doc.nodes[layoutParent];
		UiRect pr = g_d.rects[layoutParent];
		UiRect pi;
		d_inset(pr, p->padding, &pi);
		Gfx_StrokeRoundRectCornersDashed(g, pi, Gfx_Corners(0, 0, 0, 0),
			Ui_ColorAlpha(t->primary.base, 170), 1.0f);
		{
			int i, count = d_layout_item_count(layoutParent);
			for (i = 0; i < count; i++) {
				UiRect cr = g_d.rects[d_layout_item_at(layoutParent, i)];
				if (p->type == UI_DN_COLUMN || p->type == UI_DN_ROW) {
					if (p->type == UI_DN_COLUMN)
						Gfx_FillRect(g, Ui_Rect(pi.x, cr.y + cr.h,
							pi.w, p->gap), Ui_ColorAlpha(t->primary.base, 14));
					else
						Gfx_FillRect(g, Ui_Rect(cr.x + cr.w, pi.y,
							p->gap, pi.h), Ui_ColorAlpha(t->primary.base, 14));
				}
			}
			if (p->type == UI_DN_GRID)
				d_draw_grid_cells(p, pi, Ui_ColorAlpha(t->primary.base, 35));
		}
	}
	if (UiDesign_IsLayoutContainer(n->type)) {
		d_inset(nr, n->padding, &inner);
		Gfx_StrokeRoundRectCornersDashed(g, inner, Gfx_Corners(0, 0, 0, 0),
			Ui_ColorAlpha(t->primary.base, 200), 1.0f);
		if (n->type == UI_DN_GRID) {
			int cols = n->gridCols > 0 ? n->gridCols : 1;
			int rows = n->gridRows > 0 ? n->gridRows
				: (d_layout_item_count(g_d.selNode) + cols - 1) / cols;
			if (rows < 1) rows = 1;
			snprintf(b, sizeof(b), "Grid %d×%d · gap %d/%d · W:%s H:%s",
				cols, rows, (int)n->gridColGap, (int)n->gridRowGap,
				d_size_mode(n->wMode), d_size_mode(n->hMode));
		} else {
			snprintf(b, sizeof(b), "%s · gap %d · W:%s H:%s",
				UiDesign_TypeName(n->type), (int)n->gap,
				d_size_mode(n->wMode), d_size_mode(n->hMode));
		}
		Gfx_Text(g, t->fMini, b, Ui_Rect(nr.x + 4, nr.y + 2, nr.w - 8, 16),
			t->primary.base, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (n->type == UI_DN_GRID)
			d_draw_grid_cells(n, inner, Ui_ColorAlpha(t->primary.base, 45));
	}
}

static int d_to_design(float sx, float sy, float *x, float *y)
{
	float dx, dy;
	if (g_d.zoom <= 0.01f) return 0;
	dx = (sx - g_d.viewTx) / g_d.zoom;
	dy = (sy - g_d.viewTy) / g_d.zoom;
	if (dx < 0 || dy < 0 || dx > g_d.winRc.w || dy > g_d.winRc.h) return 0;
	*x = dx;
	*y = dy;
	return 1;
}

static int d_in_canvas(float x, float y)
{
	UiRect c = d_canvas_area();
	return x >= c.x && y >= c.y + 34.0f &&
		x <= c.x + c.w && y <= c.y + c.h;
}

static void d_draw_canvas(void)
{
	UiTheme *t = &g_d.app->theme;
	UiContext *ui = &g_d.app->ui;
	Gfx *g = g_d.app->gfx;
	UiRect area = d_canvas_area();
	UiRect tabs = Ui_Rect(area.x, area.y, area.w, 30.0f);
	UiRect body = Ui_Rect(area.x, area.y + 34.0f, area.w, area.h - 34.0f);
	float x = 14.0f;
	int i;

	Ui_Panel(ui, area);
	for (i = 0; i < g_d.doc.viewCount; i++) {
		UiDesignView *v = &g_d.doc.views[i];
		float bw = 110.0f;
		UiRect rc = Ui_Rect(tabs.x + x, tabs.y + 2.0f, bw, 26.0f);
		UiWidgetState st;
		UiId id = Ui_IdIndex("dsn.viewtab", i, 0, 0);
		if (rc.x + bw > tabs.x + tabs.w) break;
		st = Ui_StateInstant(ui, id, rc, 1);
		if (i == g_d.selView) Gfx_FillRoundRect(g, rc, 5.0f, t->primary.soft);
		else if (st.hovered) Gfx_FillRoundRect(g, rc, 5.0f, t->rowOn);
		Gfx_Text(g, t->fSmall, v->name, rc,
			i == g_d.selView ? t->primary.base : t->text2,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (ui->clicked == id) {
			d_select_view(i);
			UiApp_Invalidate(g_d.app);
		}
		x += bw + 6.0f;
	}

	if (g_d.selView < 0 || g_d.selView >= g_d.doc.viewCount) {
		Gfx_Text(g, t->fBody, "新建或打开一个 .cui 工程", body, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		return;
	}
	if (g_d.preview && (g_d.selWin < 0 || g_d.selWin >= g_d.doc.hostCount)) {
		Gfx_Text(g, t->fBody, "预览需要一个宿主", body, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		return;
	}
	{
		UiDesignHost *w = (g_d.selWin >= 0 && g_d.selWin < g_d.doc.hostCount)
			? &g_d.doc.hosts[g_d.selWin] : NULL;
		UiDesignView *vw = g_d.selView >= 0 && g_d.selView < g_d.doc.viewCount
			? &g_d.doc.views[g_d.selView] : NULL;
		float dw = vw ? vw->w : 800.0f;
		float dh = vw ? vw->h : 540.0f;
		UiRect win;
		d_autosize_window(w);
		if (g_d.preview && g_d.instReady && w) {
			/* 预览不做缩放: 运行时控件命中仍使用屏幕坐标 */
			win.w = w->kind == UI_DH_PANEL ? dw : w->w;
			win.h = w->kind == UI_DH_PANEL ? dh : w->h;
			win.x = body.x + (body.w - win.w) * 0.5f;
			win.y = body.y + 10.0f;
			g_d.winScreen = win;
			Gfx_PushClip(g, body);
			if (w->kind == UI_DH_DIALOG) {
				UiRect card;
				if (UiDesign_DialogIsOpen(&g_d.inst)) {
					/* 真实 UiDialog 会在整窗遮罩上绘制, 画布不再放占位卡. */
				} else {
					Gfx_FillRect(g, body, Ui_ColorAlpha(0xFF000000, 90));
					card = Ui_Rect(body.x + (body.w - 360.0f) * 0.5f,
						body.y + (body.h - 150.0f) * 0.5f, 360.0f, 150.0f);
					Gfx_FillRoundRect(g, card, 12.0f, t->card);
					Gfx_StrokeRoundRect(g, card, 12.0f, t->border, 1.0f);
					Gfx_Text(g, t->fTitle, "虚拟 Dialog 预览",
						Ui_Rect(card.x + 18, card.y + 14, card.w - 36, 28),
						t->text, GFX_TEXT_VCENTER);
					Gfx_Text(g, t->fSmall,
						"使用真实 UiDialog 覆盖层显示，不创建独立窗口",
						Ui_Rect(card.x + 18, card.y + 48, card.w - 36, 20),
						t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
					if (Ui_Button(&g_d.app->ui,
					    Ui_Id("dsn.preview.reopen", 0, 0),
					    Ui_Rect(card.x + 18, card.y + 76, 120, 30),
					    "重新打开", UI_BTYPE_PRIMARY, 1)) {
						UiDesign_DialogOpen(&g_d.inst,
							Ui_Id("dsn.preview.owner", 0, 0));
						g_d.previewDialogTried = 1;
					}
				}
			} else {
				UiDesign_Draw(&g_d.inst, win);
			}
			Gfx_PopClip(g);
		} else {
			UiRect base = Ui_Rect(0, 0, dw, dh);
			g_d.winRc = base;
			g_d.viewTx = body.x + (body.w - dw * g_d.zoom) * 0.5f + g_d.panX;
			g_d.viewTy = body.y + 10.0f + g_d.panY;
			g_d.winScreen = Ui_Rect(g_d.viewTx, g_d.viewTy,
				dw * g_d.zoom, dh * g_d.zoom);
			d_layout_current();
			Gfx_PushClip(g, body);
			Gfx_PushScaleTranslate(g, g_d.zoom, g_d.viewTx, g_d.viewTy);
			Gfx_FillRoundRect(g, Ui_Rect(-5, -5, dw + 10, dh + 10),
				10.0f, Ui_ColorAlpha(0xFF000000, 24));
			Gfx_FillRoundRect(g, base, 0.0f, t->page);
			Gfx_StrokeRoundRect(g, base, 0.0f, t->border, 1.0f);
			if (d_view_root() >= 0) {
				d_draw_static_node(d_view_root());
				{
					int si;
					for (si = 0; si < g_d.selSetN; si++) {
						int ni = g_d.selSet[si];
						if (ni == g_d.selNode || ni < 0 || ni >= g_d.doc.nodeCount) continue;
						if (g_d.doc.nodes[ni].owner != g_d.selView) continue;
						Gfx_StrokeRoundRect(g, g_d.rects[ni], 1.0f,
							Ui_ColorAlpha(t->primary.base, 190), 1.5f);
					}
				}
				if (g_d.selKind == DSEL_RADIOGROUP) {
					UiDesignRadioGroup *rg = d_selrg();
					UiRect ur;
					if (rg && rg->owner == g_d.selView &&
					    d_rg_union_rect(g_d.selRadioGroup, &ur)) {
						UiRect gr = Ui_Rect(ur.x - 3, ur.y - 3, ur.w + 6, ur.h + 6);
						Gfx_StrokeRoundRectCornersDashed(g, gr, Gfx_Corners(4, 4, 4, 4),
							Ui_ColorAlpha(t->primary.base, 220), 1.5f);
					}
				}
				if (g_d.selKind == DSEL_NODE && g_d.selNode >= 0 &&
				    g_d.doc.nodes[g_d.selNode].owner == g_d.selView) {
					d_draw_layout_guides();
					d_draw_handles(g_d.selNode);
				}
				if (g_d.dragging && g_d.snapX)
					Gfx_Line(g, g_d.snapLineX, 0, g_d.snapLineX,
						w->h, Ui_ColorAlpha(t->primary.base, 150), 1.0f);
				if (g_d.dragging && g_d.snapY)
					Gfx_Line(g, 0, g_d.snapLineY, w->w,
						g_d.snapLineY, Ui_ColorAlpha(t->primary.base, 150), 1.0f);
				if (g_d.marquee) {
					UiRect mr = Ui_Rect(
						g_d.marqueeX0 < g_d.marqueeX1 ? g_d.marqueeX0 : g_d.marqueeX1,
						g_d.marqueeY0 < g_d.marqueeY1 ? g_d.marqueeY0 : g_d.marqueeY1,
						fabsf_local(g_d.marqueeX1 - g_d.marqueeX0),
						fabsf_local(g_d.marqueeY1 - g_d.marqueeY0));
					Gfx_FillRect(g, mr, Ui_ColorAlpha(t->primary.base, 24));
					Gfx_StrokeRoundRectCornersDashed(g, mr, Gfx_Corners(0, 0, 0, 0),
						t->primary.base, 1.0f);
				}
			}
			Gfx_PopTransform(g);
			Gfx_PopClip(g);
		}
	}
}

static UiIcon d_icon(UiApp *app, uint32_t cp)
{
	UiIcon ic;
	ZeroMemory(&ic, sizeof(ic));
	if (app && app->theme.fIcon) return UiIcon_Glyph(app->theme.fIcon, cp);
	return ic;
}

enum {
	DTB_LAYOUT = 0,
	DTB_COMPONENT,
	DTB_DELETE,
	DTB_DUPLICATE,
	DTB_MOVE_UP,
	DTB_MOVE_DOWN,
	DTB_PREVIEW,
	DTB_RADIOGROUP
};

typedef struct DToolItem {
	const char *label;
	int kind;
	int value;
	uint32_t cp;
} DToolItem;

#define D_TOOL_LAYOUT_N 6
#define D_TOOL_COMP_N   18
#define D_TOOL_ACTION_N 5
#define D_TOOL_LEFT_N   (D_TOOL_LAYOUT_N + D_TOOL_COMP_N)

/* 单选组不是节点类型, 用超出 UI_DN_* 范围的插入工具值表示. */
#define D_INSERT_RADIOGROUP 1000

static const DToolItem kToolbarItems[] = {
	{ "选择", DTB_LAYOUT, -1, 0xE8B0u },
	{ "自由布局", DTB_LAYOUT, UI_DN_ABSOLUTE, 0xE8B4u },
	{ "列布局", DTB_LAYOUT, UI_DN_COLUMN, 0xE784u },
	{ "行布局", DTB_LAYOUT, UI_DN_ROW, 0xE76Fu },
	{ "Grid 布局", DTB_LAYOUT, UI_DN_GRID, 0xF0E2u },
	{ "面板布局", DTB_LAYOUT, UI_DN_PANEL, 0xE8A5u },
	{ "标签", DTB_COMPONENT, UI_DN_LABEL, 0xE8D2u },
	{ "按钮", DTB_COMPONENT, UI_DN_BUTTON, 0xEECAu },
	{ "输入框", DTB_COMPONENT, UI_DN_INPUT, 0xE961u },
	{ "复选框", DTB_COMPONENT, UI_DN_CHECKBOX, 0xE73Au },
	{ "开关", DTB_COMPONENT, UI_DN_SWITCH, 0xE8ABu },
	{ "滑块", DTB_COMPONENT, UI_DN_SLIDER, 0xEC13u },
	{ "进度条", DTB_COMPONENT, UI_DN_PROGRESS, 0xF16Au },
	{ "下拉框", DTB_COMPONENT, UI_DN_SELECT, 0xE70Du },
	{ "单选组", DTB_RADIOGROUP, 0, 0x0000u },
	{ "单选项", DTB_COMPONENT, UI_DN_RADIO, 0x0000u },
	{ "图片", DTB_COMPONENT, UI_DN_IMAGE, 0xE8B9u },
	{ "分隔线", DTB_COMPONENT, UI_DN_SEPARATOR, 0xEF3Du },
	{ "Tabs", DTB_COMPONENT, UI_DN_TABS, 0x0000u },
	{ "多行输入", DTB_COMPONENT, UI_DN_TEXTAREA, 0xF465u },
	{ "日期", DTB_COMPONENT, UI_DN_DATEPICKER, 0xE787u },
	{ "颜色选择", DTB_COMPONENT, UI_DN_COLORSEL, 0xE790u },
	{ "二维码", DTB_COMPONENT, UI_DN_QRCODE, 0xED14u },
	{ "圆环进度", DTB_COMPONENT, UI_DN_RINGPROGRESS, 0xEA3Au },
	{ "删除", DTB_DELETE, 0, 0xE74Du },
	{ "复制", DTB_DUPLICATE, 0, 0xE8C8u },
	{ "上移", DTB_MOVE_UP, 0, 0xE74Au },
	{ "下移", DTB_MOVE_DOWN, 0, 0xE74Bu },
	{ "预览", DTB_PREVIEW, 0, 0xE768u }
};

#define D_TOOL_ITEM_N ((int)(sizeof(kToolbarItems) / sizeof(kToolbarItems[0])))

static int d_toolbar_item_enabled(const DToolItem *it)
{
	UiDesignNode *n;
	if (!it) return 0;
	if (it->kind == DTB_RADIOGROUP) {
		int parent = d_container_parent();
		if (parent < 0) return 0;
		return UiDesign_CanParent(g_d.doc.nodes[parent].type, UI_DN_RADIO);
	}
	if (it->kind == DTB_COMPONENT && it->value == UI_DN_RADIO) {
		if (g_d.selKind == DSEL_RADIOGROUP) return d_sel_radio_group_index() >= 0;
		n = d_selnode();
		return g_d.selKind == DSEL_NODE && n && n->type == UI_DN_RADIO &&
			d_node_group_index(g_d.selNode) >= 0;
	}
	switch (it->kind) {
	case DTB_DUPLICATE:
	case DTB_MOVE_UP:
	case DTB_MOVE_DOWN:
		return g_d.selKind == DSEL_NODE || g_d.selKind == DSEL_RADIOGROUP;
	default:
		return 1;
	}
}

static int d_toolbar_item_selected(const DToolItem *it)
{
	if (!it) return 0;
	if (it->kind == DTB_RADIOGROUP)
		return g_d.insertType == D_INSERT_RADIOGROUP;
	return (it->kind == DTB_LAYOUT || it->kind == DTB_COMPONENT) &&
		g_d.insertType == it->value;
}

static void d_toolbar_activate(const DToolItem *it)
{
	if (!it || !d_toolbar_item_enabled(it)) return;
	switch (it->kind) {
	case DTB_LAYOUT:
		g_d.insertType = it->value;
		d_status(it->value < 0 ? "选择工具" : "布局工具: 在画布中点击插入");
		break;
	case DTB_COMPONENT:
		g_d.insertType = it->value;
		d_status("组件工具: 在画布中点击插入 (Esc 退出)");
		break;
	case DTB_RADIOGROUP:
		g_d.insertType = D_INSERT_RADIOGROUP;
		d_status("单选组工具: 在画布中点击插入 (Esc 退出)");
		break;
	case DTB_DELETE:
		d_del_selected();
		break;
	case DTB_DUPLICATE:
		d_dup_node();
		break;
	case DTB_MOVE_UP:
		d_move_node(0);
		break;
	case DTB_MOVE_DOWN:
		d_move_node(1);
		break;
	case DTB_PREVIEW:
		if (!g_d.preview && (g_d.selWin < 0 ||
		    g_d.selWin >= g_d.doc.hostCount ||
		    g_d.doc.hosts[g_d.selWin].view < 0)) {
			d_error("预览需要一个挂载了视图的宿主");
		} else {
			if (g_d.preview) {
				g_d.preview = 0;
				d_preview_shutdown();
			} else {
				g_d.preview = 1;
				g_d.instReady = 0;
				d_preview_capture_tabs();
				d_preview_capture_radios();
			}
			d_status(g_d.preview ? "预览模式" : "设计模式");
		}
		break;
	default:
		break;
	}
}

static void d_toolbar_open_overflow(UiContext *ui, int start, UiRect anchor)
{
	int i, n = 0;
	for (i = start; i < D_TOOL_LEFT_N && n < (int)(sizeof(g_d.toolbarMenuItems) /
	    sizeof(g_d.toolbarMenuItems[0])); i++) {
		const DToolItem *it = &kToolbarItems[i];
		UiMenuItem *mi = &g_d.toolbarMenuItems[n];
		snprintf(g_d.toolbarMenuLabels[n], sizeof(g_d.toolbarMenuLabels[n]),
			"%s%s", d_toolbar_item_selected(it) ? "✓ " : "", it->label);
		mi->label = g_d.toolbarMenuLabels[n];
		mi->shortcut = NULL;
		mi->flags = d_toolbar_item_enabled(it) ? 0 : UI_MENU_DISABLED;
		g_d.toolbarMenuIdx[n] = i;
		n++;
	}
	g_d.toolbarMenuN = n;
	if (n <= 0) return;
	UiMenu_SetItems(&g_d.toolbarMenu, g_d.toolbarMenuItems, n);
	UiMenu_OpenAt(&g_d.toolbarMenu, &g_d.pm, ui,
		Ui_Id("dsn.tool.more", 0, 0), anchor, 1);
}

static UiNavItem d_nav_leaf(const char *id, int kind, const char *label)
{
	UiNavItem it;
	ZeroMemory(&it, sizeof(it));
	it.id = Ui_Id(id, kind, 0);
	it.label = label;
	g_d.navAct[g_d.navActN] = it.id;
	g_d.navActKind[g_d.navActN] = kind;
	g_d.navActN++;
	return it;
}

static UiNavItem d_nav_root(UiId id, const char *label, uint32_t cp,
	const UiNavItem *children, int count)
{
	UiNavItem it;
	ZeroMemory(&it, sizeof(it));
	it.id = id;
	it.label = label;
	it.icon = d_icon(g_d.app, cp);
	it.children = children;
	it.childCount = count;
	return it;
}

static void d_nav_init(void)
{
	UiNavStyle st;
	g_d.navActN = 0;
	g_d.navFile[0] = d_nav_leaf("dsn.nav.new", DNA_NEW, "新建");
	g_d.navFile[1] = d_nav_leaf("dsn.nav.open", DNA_OPEN, "打开");
	g_d.navFile[2] = d_nav_leaf("dsn.nav.save", DNA_SAVE, "保存");
	g_d.navFile[3] = d_nav_leaf("dsn.nav.saveas", DNA_SAVEAS, "另存为");
	g_d.navFile[4] = d_nav_leaf("dsn.nav.gen", DNA_GENERATE, "生成代码");
	g_d.navFile[5] = d_nav_leaf("dsn.nav.exit", DNA_EXIT, "退出");
	g_d.navFile[0].shortcut = "Ctrl+N";
	g_d.navFile[1].shortcut = "Ctrl+O";
	g_d.navFile[2].shortcut = "Ctrl+S";
	g_d.navFile[3].shortcut = "Ctrl+Shift+S";
	g_d.navFile[4].shortcut = "Ctrl+G";
	g_d.navFile[5].shortcut = "Alt+F4";
	g_d.navEdit[0] = d_nav_leaf("dsn.nav.undo", DNA_UNDO, "撤销");
	g_d.navEdit[1] = d_nav_leaf("dsn.nav.redo", DNA_REDO, "重做");
	g_d.navEdit[2] = d_nav_leaf("dsn.nav.dup", DNA_DUP, "复制组件");
	g_d.navEdit[3] = d_nav_leaf("dsn.nav.del", DNA_DEL, "删除组件");
	g_d.navEdit[4] = d_nav_leaf("dsn.nav.up", DNA_UP, "上移");
	g_d.navEdit[5] = d_nav_leaf("dsn.nav.down", DNA_DOWN, "下移");
	g_d.navProj[0] = d_nav_leaf("dsn.nav.addwin", DNA_ADDWIN, "新建窗口");
	g_d.navProj[1] = d_nav_leaf("dsn.nav.adddlg", DNA_ADDDLG, "新建虚拟 Dialog");
	g_d.navProj[2] = d_nav_leaf("dsn.nav.addpanel", DNA_ADDPANEL, "新建 Panel 宿主");
	g_d.navProj[3] = d_nav_leaf("dsn.nav.addgrp", DNA_ADDGRP, "新建生成组");
	g_d.navProj[4] = d_nav_leaf("dsn.nav.genall", DNA_GENALL, "生成全部分组");
	g_d.navProj[5] = d_nav_leaf("dsn.nav.genwin", DNA_GENWIN, "生成当前宿主");
	g_d.navView[0] = d_nav_leaf("dsn.nav.design", DNA_DESIGN, "设计模式");
	g_d.navView[1] = d_nav_leaf("dsn.nav.preview", DNA_PREVIEW, "预览模式");
	g_d.navRoot[0] = d_nav_root(Ui_Id("dsn.nav.root", 0, 0), "文件", 0xE8B7, g_d.navFile, 6);
	g_d.navRoot[1] = d_nav_root(Ui_Id("dsn.nav.root", 1, 0), "编辑", 0xE70F, g_d.navEdit, 6);
	g_d.navRoot[2] = d_nav_root(Ui_Id("dsn.nav.root", 2, 0), "工程", 0xE8A9, g_d.navProj, 6);
	g_d.navRoot[3] = d_nav_root(Ui_Id("dsn.nav.root", 3, 0), "视图", 0xE768, g_d.navView, 2);
	UiNavBar_Init(&g_d.nav, Ui_Id("dsn.nav", 0, 0));
	UiNavBar_SetItems(&g_d.nav, g_d.navRoot, 4);
	st = UiNavStyle_Default();
	st.barHeight = 34.0f;
	st.itemH = 30.0f;
	st.padX = 12.0f;
	st.radius = 7.0f;
	st.barBg = 0;
	st.barBorder = 0;
	UiNavBar_SetStyle(&g_d.nav, &st);
}

static void d_nav_do(int kind)
{
	switch (kind) {
	case DNA_NEW: d_new_doc(); break;
	case DNA_OPEN: d_open_file(); break;
	case DNA_SAVE: d_save_file(0); break;
	case DNA_SAVEAS: d_save_file(1); break;
	case DNA_EXIT: UiApp_Close(g_d.app); break;
	case DNA_GENERATE:
	case DNA_GENALL: d_generate(); break;
	case DNA_GENWIN: d_generate_window(); break;
	case DNA_UNDO: d_undo_step(0); break;
	case DNA_REDO: d_undo_step(1); break;
	case DNA_DUP: d_dup_node(); break;
	case DNA_DEL: d_del_selected(); break;
	case DNA_UP: d_move_node(0); break;
	case DNA_DOWN: d_move_node(1); break;
	case DNA_ADDWIN: d_add_window(UI_DH_WINDOW); break;
	case DNA_ADDDLG: d_add_window(UI_DH_DIALOG); break;
	case DNA_ADDPANEL: d_add_panel_host(); break;
	case DNA_ADDGRP: d_add_group(); break;
	case DNA_DESIGN:
		g_d.preview = 0;
		d_preview_shutdown();
		d_status("设计模式");
		break;
	case DNA_PREVIEW:
		if (g_d.selWin < 0 || g_d.selWin >= g_d.doc.hostCount ||
		    g_d.doc.hosts[g_d.selWin].view < 0) {
			d_error("预览需要一个挂载了视图的宿主");
			break;
		}
		g_d.preview = 1;
		g_d.instReady = 0;
		d_preview_capture_tabs();
		d_preview_capture_radios();
		d_status("预览模式");
		break;
	default: break;
	}
}

static void d_nav_take(void)
{
	UiId path[8];
	int n, i, kind = 0;
	if (!UiNavBar_TakeChanged(&g_d.nav, path, 8)) return;
	n = 0;
	while (n < 8 && path[n]) n++;
	if (n > 0) {
		for (i = 0; i < g_d.navActN; i++)
			if (g_d.navAct[i] == path[n - 1]) { kind = g_d.navActKind[i]; break; }
	}
	UiNavBar_SetSelected(&g_d.nav, NULL, 0);
	if (kind) d_nav_do(kind);
}

static void d_draw_topbar(void)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiContext *ui = &g_d.app->ui;
	UiRect bar = Ui_Rect(0, 0, g_d.app->w, TOP_H);
	UiRect navRc = Ui_Rect(10, 3, 560, 34);

	Gfx_FillRoundRectCorners(g, bar,
		Gfx_Corners(t->windowRadius, t->windowRadius, 0, 0), t->frame);
	Gfx_Line(g, 0, TOP_H - 1, g_d.app->w, TOP_H - 1, t->border, 1.0f);
	UiNavBar_Draw(&g_d.nav, &g_d.pm, ui, navRc, 1);
	d_nav_take();
	{
		char tb[64];
		snprintf(tb, sizeof(tb), "CUI Designer%s", g_d.doc.dirty ? " *" : "");
		Gfx_Text(g, t->fHead, tb,
			Ui_Rect(g_d.app->w - 400, 0, 344, TOP_H), t->text2,
			GFX_TEXT_VCENTER | GFX_TEXT_RIGHT | GFX_TEXT_ELLIPSIS);
	}
	{
		UiId cid = Ui_Id("dsn.close", 0, 0);
		UiRect crc = Ui_Rect(g_d.app->w - 44.0f, 0, 44.0f, TOP_H);
		UiWidgetState cs = Ui_State(ui, cid, crc, 1);
		float mx = crc.x + crc.w * 0.5f;
		float my = crc.y + crc.h * 0.5f;
		float d = 5.0f;
		UiColor xc = Ui_ColorMix(t->text3, 0xFFFFFFFF, cs.hover);
		if (cs.hover > 0.01f)
			Gfx_FillRoundRect(g, crc, 8.0f,
				Ui_ColorAlpha(0xFFE81123, (int)(cs.hover * 232.0f)));
		Gfx_Line(g, mx - d, my - d, mx + d, my + d, xc, 1.5f);
		Gfx_Line(g, mx - d, my + d, mx + d, my - d, xc, 1.5f);
		if (cs.hovered) Ui_SetCursor(ui, cid, UI_CURSOR_HAND);
		if (ui->clicked == cid) UiApp_Close(g_d.app);
	}
}

static void d_draw_tabs_icon(Gfx *g, UiRect rc, UiColor color)
{
	float x = rc.x + (rc.w - 16.0f) * 0.5f;
	float y = rc.y + (rc.h - 16.0f) * 0.5f;
	Gfx_Line(g, x + 1.0f, y + 4.0f, x + 1.0f, y + 15.0f, color, 1.25f);
	Gfx_Line(g, x + 1.0f, y + 15.0f, x + 15.0f, y + 15.0f, color, 1.25f);
	Gfx_Line(g, x + 15.0f, y + 4.0f, x + 15.0f, y + 15.0f, color, 1.25f);
	Gfx_Line(g, x + 1.0f, y + 4.0f, x + 6.0f, y + 4.0f, color, 1.25f);
	Gfx_Line(g, x + 6.0f, y + 4.0f, x + 6.0f, y + 7.0f, color, 1.25f);
	Gfx_Line(g, x + 6.0f, y + 4.0f, x + 11.0f, y + 4.0f, color, 1.25f);
	Gfx_Line(g, x + 11.0f, y + 4.0f, x + 11.0f, y + 7.0f, color, 1.25f);
	Gfx_Line(g, x + 11.0f, y + 4.0f, x + 15.0f, y + 4.0f, color, 1.25f);
	Gfx_Line(g, x + 1.0f, y + 7.0f, x + 15.0f, y + 7.0f, color, 1.25f);
}

static void d_draw_radio_icon(Gfx *g, UiRect rc, UiColor color)
{
	float x = rc.x + (rc.w - 16.0f) * 0.5f;
	float y = rc.y + (rc.h - 16.0f) * 0.5f;
	Gfx_StrokeEllipse(g, x + 4.0f, y + 4.5f, 2.8f, color, 1.2f);
	Gfx_FillEllipse(g, x + 4.0f, y + 4.5f, 1.5f, 1.5f, color);
	Gfx_StrokeEllipse(g, x + 4.0f, y + 11.5f, 2.8f, color, 1.2f);
	Gfx_Line(g, x + 9.0f, y + 4.5f, x + 15.0f, y + 4.5f, color, 1.2f);
	Gfx_Line(g, x + 9.0f, y + 11.5f, x + 15.0f, y + 11.5f, color, 1.2f);
}

static void d_draw_tool_item(UiContext *ui, const DToolItem *it, int index,
	UiRect rc, UiId *tipId, UiRect *tipRc, const char **tipText)
{
	UiTheme *t = &g_d.app->theme;
	UiButtonStyle normal = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	UiButtonStyle selected = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
	UiButtonStyle danger = UiButtonStyle_Make(UI_BTYPE_ERROR,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	const UiButtonStyle *st = d_toolbar_item_selected(it) ? &selected : &normal;
	UiIcon ic;
	UiId id = Ui_Id("dsn.tool", index, 0);
	int customIcon = it->value == UI_DN_TABS || it->kind == DTB_RADIOGROUP ||
		it->value == UI_DN_RADIO;
	int enabled = d_toolbar_item_enabled(it);
	int clicked;
	if (it->kind == DTB_DELETE) st = &danger;
	if (customIcon)
		ZeroMemory(&ic, sizeof(ic));
	else
		ic = d_icon(g_d.app, it->cp);
	clicked = Ui_IconButton(ui, id, rc, &ic, st, enabled);
	if (customIcon) {
		UiColor color = enabled ? (d_toolbar_item_selected(it) ? t->primary.base
			: t->text2) : t->btnDisabledText;
		if (d_toolbar_item_selected(it) && ui->hover == id) color = 0xFFFFFFFFu;
		if (it->value == UI_DN_TABS) d_draw_tabs_icon(ui->gfx, rc, color);
		else if (it->value == UI_DN_RADIO) {
			float cx = rc.x + rc.w * 0.38f, cy = rc.y + rc.h * 0.5f;
			Gfx_StrokeEllipse(ui->gfx, cx, cy, 5.0f, color, 1.3f);
			Gfx_FillEllipse(ui->gfx, cx, cy, 2.5f, 2.5f, color);
			Gfx_Line(ui->gfx, cx + 8.0f, cy, rc.x + rc.w - 6.0f, cy,
				color, 1.3f);
		} else d_draw_radio_icon(ui->gfx, rc, color);
	}
	if (clicked) d_toolbar_activate(it);
	if (ui->hover == id) {
		*tipId = id;
		*tipRc = rc;
		*tipText = it->label;
	}
}

static void d_draw_toolbar(void)
{
	UiTheme *t = &g_d.app->theme;
	UiContext *ui = &g_d.app->ui;
	Gfx *g = g_d.app->gfx;
	UiButtonStyle more = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiRect bar = Ui_Rect(0, TOP_H, g_d.app->w, PALETTE_H);
	UiId tipId = UI_ID_NONE;
	UiRect tipRc = Ui_Rect(0, 0, 0, 0);
	const char *tipText = NULL;
	float size = 30.0f, gap = 4.0f, groupExtra = 8.0f;
	float x = 12.0f, y = TOP_H + 6.0f;
	float actionW = D_TOOL_ACTION_N * size + (D_TOOL_ACTION_N - 1) * gap;
	float actionX = g_d.app->w - 12.0f - actionW;
	float leftAvail = actionX - 12.0f - x;
	float leftFullW = D_TOOL_LEFT_N * (size + gap) - gap + groupExtra;
	int fit, visible = D_TOOL_LEFT_N, overflow = 0, i;

	Gfx_FillRect(g, bar, Ui_ColorMix(t->page, t->frame, 0.45f));
	Gfx_Line(g, 0, TOP_H + PALETTE_H - 1, g_d.app->w,
		TOP_H + PALETTE_H - 1, t->border, 1.0f);
	if (leftAvail < size) leftAvail = size;

	if (leftFullW > leftAvail) {
		overflow = 1;
		fit = (int)((leftAvail - groupExtra + gap) / (size + gap));
		visible = fit - 1;
		if (visible < 0) visible = 0;
		if (visible >= D_TOOL_LEFT_N) visible = D_TOOL_LEFT_N - 1;
	}

	for (i = 0; i < visible; i++) {
		if (i == D_TOOL_LAYOUT_N) {
			x += groupExtra;
			Gfx_Line(g, x - (gap + groupExtra) * 0.5f, y + 7.0f,
				x - (gap + groupExtra) * 0.5f, y + size - 7.0f,
				t->border, 1.0f);
		}
		d_draw_tool_item(ui, &kToolbarItems[i], i, Ui_Rect(x, y, size, size),
			&tipId, &tipRc, &tipText);
		x += size + gap;
	}
	if (overflow) {
		UiId id = Ui_Id("dsn.tool.more", 0, 0);
		UiRect rc = Ui_Rect(x, y, size, size);
		if (Ui_ButtonEx(ui, id, rc, ">>", &more, 1))
			d_toolbar_open_overflow(ui, visible, rc);
		if (ui->hover == id) {
			tipId = id;
			tipRc = rc;
			tipText = "更多工具栏项";
		}
	}

	Gfx_Line(g, actionX - 6.0f, y + 7.0f, actionX - 6.0f,
		y + size - 7.0f, t->border, 1.0f);
	for (i = D_TOOL_LEFT_N; i < D_TOOL_ITEM_N; i++) {
		d_draw_tool_item(ui, &kToolbarItems[i], i,
			Ui_Rect(actionX, y, size, size), &tipId, &tipRc, &tipText);
		actionX += size + gap;
	}
	if (tipId)
		UiTooltip_Update(&g_d.toolbarTip, ui, tipId, tipRc, tipText,
			UiFrame_Now());
	else
		UiTooltip_Update(&g_d.toolbarTip, ui, UI_ID_NONE, tipRc, NULL,
			UiFrame_Now());
}

/* 该 Radio 是否为其父容器中同组第一项: 决定虚拟组行插入位置. */
static int d_rg_parent_first(int idx)
{
	UiDesignNode *n;
	int gi, p, i;
	if (idx < 0 || idx >= g_d.doc.nodeCount) return 0;
	n = &g_d.doc.nodes[idx];
	if (n->type != UI_DN_RADIO) return 0;
	gi = d_node_group_index(idx);
	if (gi < 0) return 0;
	p = n->parent;
	if (p < 0) return 1;
	for (i = 0; i < g_d.doc.nodes[p].childCount; i++) {
		int ci = g_d.doc.nodes[p].children[i];
		if (ci == idx) return 1;
		if (g_d.doc.nodes[ci].type == UI_DN_RADIO &&
		    d_node_group_index(ci) == gi) return 0;
	}
	return 1;
}

static void d_draw_tree_rec(int idx, int depth, float *y, UiRect panel)
{
	UiDesignNode *nd;
	UiRect rc;
	UiWidgetState st;
	UiId id;
	char b[160];
	if (idx < 0 || idx >= g_d.doc.nodeCount) return;
	if (*y + ROW_H > panel.y + panel.h - 8) return;
	if (d_rg_parent_first(idx)) {
		int gi = d_node_group_index(idx);
		UiDesignRadioGroup *rg = d_rg(gi);
		if (rg) {
			UiRect grc = Ui_Rect(panel.x + 8 + depth * 14.0f, *y,
				panel.w - 16 - depth * 14.0f, ROW_H);
			UiWidgetState gs = Ui_StateInstant(&g_d.app->ui,
				Ui_IdIndex("dsn.treegrp", gi, 0, 0), grc, 1);
			if (g_d.selKind == DSEL_RADIOGROUP && g_d.selRadioGroup == gi)
				Gfx_FillRoundRect(g_d.app->gfx, grc, 4.0f,
					g_d.app->theme.primary.soft);
			else if (gs.hovered)
				Gfx_FillRoundRect(g_d.app->gfx, grc, 4.0f, g_d.app->theme.rowOn);
			if (d_reserve((void **)&g_d.treeRows, &g_d.treeRowCap,
			    g_d.treeRowN + 1, sizeof(*g_d.treeRows))) {
				g_d.treeRows[g_d.treeRowN].rc = grc;
				g_d.treeRows[g_d.treeRowN].idx = -1;
				g_d.treeRows[g_d.treeRowN].kind = DROW_RADIOGROUP;
				g_d.treeRows[g_d.treeRowN].group = gi;
				g_d.treeRowN++;
			}
			snprintf(b, sizeof(b), "◉ %s", rg->name[0] ? rg->name : rg->id);
			Gfx_Text(g_d.app->gfx, g_d.app->theme.fSmall, b,
				Ui_Rect(grc.x + 6, grc.y, grc.w - 82, grc.h),
				g_d.app->theme.text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			Gfx_Text(g_d.app->gfx, g_d.app->theme.fMini, "RadioGroup",
				Ui_Rect(grc.x + grc.w - 74, grc.y, 70, grc.h),
				g_d.app->theme.text4, GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
			*y += ROW_H + 1;
			if (*y + ROW_H > panel.y + panel.h - 8) return;
		}
	}
	nd = &g_d.doc.nodes[idx];
	rc = Ui_Rect(panel.x + 8 + depth * 14.0f, *y,
		panel.w - 16 - depth * 14.0f, ROW_H);
	if (d_reserve((void **)&g_d.treeRows, &g_d.treeRowCap,
	    g_d.treeRowN + 1, sizeof(*g_d.treeRows))) {
		g_d.treeRows[g_d.treeRowN].rc = rc;
		g_d.treeRows[g_d.treeRowN].idx = idx;
		g_d.treeRows[g_d.treeRowN].kind = DROW_NODE;
		g_d.treeRows[g_d.treeRowN].group = -1;
		g_d.treeRowN++;
	}
	id = Ui_IdIndex("dsn.tree", idx, 0, 0);
	st = Ui_StateInstant(&g_d.app->ui, id, rc, 1);
	if (d_sel_has(idx))
		Gfx_FillRoundRect(g_d.app->gfx, rc, 4.0f, g_d.app->theme.primary.soft);
	else if (st.hovered)
		Gfx_FillRoundRect(g_d.app->gfx, rc, 4.0f, g_d.app->theme.rowOn);
	if (g_d.treeDragIdx >= 0 && g_d.treeDropIdx == idx) {
		if (g_d.treeDropMode == 0)
			Gfx_Line(g_d.app->gfx, rc.x, rc.y, rc.x + rc.w, rc.y,
				g_d.app->theme.primary.base, 2.0f);
		else if (g_d.treeDropMode == 2)
			Gfx_Line(g_d.app->gfx, rc.x, rc.y + rc.h, rc.x + rc.w,
				rc.y + rc.h, g_d.app->theme.primary.base, 2.0f);
		else
			Gfx_StrokeRoundRect(g_d.app->gfx, rc, 4.0f,
				g_d.app->theme.primary.base, 2.0f);
	}
	snprintf(b, sizeof(b), "%s", nd->name);
	Gfx_Text(g_d.app->gfx, g_d.app->theme.fSmall, b,
		Ui_Rect(rc.x + 6, rc.y, rc.w - 70, rc.h), g_d.app->theme.text2,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(g_d.app->gfx, g_d.app->theme.fMini, UiDesign_TypeName(nd->type),
		Ui_Rect(rc.x + rc.w - 62, rc.y, 58, rc.h), g_d.app->theme.text4,
		GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
	*y += ROW_H + 1;
	{
		int k;
		for (k = 0; k < nd->childCount; k++)
			d_draw_tree_rec(nd->children[k], depth + 1, y, panel);
	}
}

static void d_draw_left(void)
{
	UiTheme *t = &g_d.app->theme;
	UiContext *ui = &g_d.app->ui;
	Gfx *g = g_d.app->gfx;
	UiRect panel = Ui_Rect(10, TOP_H + PALETTE_H + GAP,
		LEFT_W, g_d.app->h - TOP_H - PALETTE_H - STATUS_H - GAP * 2);
	float y;
	int i;

	Ui_Panel(ui, panel);
	y = panel.y + 8;
	Gfx_Text(g, t->fLabel, "宿主 (窗口/Dialog/Panel)",
		Ui_Rect(panel.x + 12, y, panel.w - 50, 18),
		t->text2, GFX_TEXT_VCENTER);
	if (Ui_Button(ui, Ui_Id("dsn.add.host", 0, 0),
	    Ui_Rect(panel.x + panel.w - 34, y - 2, 24, 20), "+",
	    UI_BTYPE_DEFAULT, 1)) {
		d_add_window(UI_DH_WINDOW);
		UiApp_Invalidate(g_d.app);
	}
	y += 24;
	for (i = 0; i < g_d.doc.hostCount && i < 4; i++) {
		UiDesignHost *w = &g_d.doc.hosts[i];
		UiRect rc = Ui_Rect(panel.x + 8, y, panel.w - 16, ROW_H);
		UiWidgetState st = Ui_StateInstant(ui, Ui_IdIndex("dsn.winrow", i, 0, 0), rc, 1);
		char b[128];
		const char *badge = w->kind == UI_DH_DIALOG ? "◇" :
			(w->kind == UI_DH_PANEL ? "▤" : "▣");
		if (g_d.selKind == DSEL_WINDOW && g_d.selWin == i)
			Gfx_FillRoundRect(g, rc, 5.0f, t->primary.soft);
		else if (st.hovered) Gfx_FillRoundRect(g, rc, 5.0f, t->rowOn);
		snprintf(b, sizeof(b), "%s %s", badge, w->name);
		Gfx_Text(g, t->fBody, b, Ui_Rect(rc.x + 8, rc.y, rc.w - 14, rc.h),
			t->text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (ui->clicked == Ui_IdIndex("dsn.winrow", i, 0, 0)) {
			d_select_window(i);
			UiApp_Invalidate(g_d.app);
		}
		y += ROW_H + 2;
	}
	if (g_d.doc.hostCount > 4) {
		Gfx_Text(g, t->fMini, "…", Ui_Rect(panel.x + 20, y, 40, 14),
			t->text4, GFX_TEXT_VCENTER);
		y += 16;
	}

	y += 4;
	Gfx_Text(g, t->fLabel, "视图 (可复用内容)",
		Ui_Rect(panel.x + 12, y, panel.w - 50, 18),
		t->text2, GFX_TEXT_VCENTER);
	if (Ui_Button(ui, Ui_Id("dsn.add.view", 0, 0),
	    Ui_Rect(panel.x + panel.w - 34, y - 2, 24, 20), "+",
	    UI_BTYPE_DEFAULT, 1)) {
		d_add_view();
		UiApp_Invalidate(g_d.app);
	}
	y += 24;
	for (i = 0; i < g_d.doc.viewCount && i < 4; i++) {
		UiDesignView *v = &g_d.doc.views[i];
		UiRect rc = Ui_Rect(panel.x + 8, y, panel.w - 16, ROW_H);
		UiWidgetState st = Ui_StateInstant(ui, Ui_IdIndex("dsn.viewrow", i, 0, 0), rc, 1);
		if (g_d.selView == i)
			Gfx_FillRoundRect(g, rc, 5.0f, t->primary.soft);
		else if (st.hovered) Gfx_FillRoundRect(g, rc, 5.0f, t->rowOn);
		Gfx_Text(g, t->fSmall, v->name,
			Ui_Rect(rc.x + 20, rc.y, rc.w - 28, rc.h),
			t->text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (ui->clicked == Ui_IdIndex("dsn.viewrow", i, 0, 0)) {
			d_select_view(i);
			UiApp_Invalidate(g_d.app);
		}
		y += ROW_H + 2;
	}

	y += 4;
	Gfx_Text(g, t->fSmall, "生成组", Ui_Rect(panel.x + 12, y, panel.w - 24, 18),
		t->text3, GFX_TEXT_VCENTER);
	y += 20;
	for (i = 0; i < g_d.doc.groupCount && i < 4; i++) {
		UiDesignGroup *gr = &g_d.doc.groups[i];
		UiRect rc = Ui_Rect(panel.x + 8, y, panel.w - 16, ROW_H);
		UiWidgetState st = Ui_StateInstant(ui, Ui_IdIndex("dsn.grprow", i, 0, 0), rc, 1);
		if (g_d.selKind == DSEL_GROUP && g_d.selGroup == i)
			Gfx_FillRoundRect(g, rc, 5.0f, t->success.soft);
		else if (st.hovered) Gfx_FillRoundRect(g, rc, 5.0f, t->rowOn);
		Gfx_Text(g, t->fSmall, gr->name, Ui_Rect(rc.x + 20, rc.y, rc.w - 26, rc.h),
			t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (ui->clicked == Ui_IdIndex("dsn.grprow", i, 0, 0)) {
			g_d.selKind = DSEL_GROUP;
			g_d.selGroup = i;
			UiApp_Invalidate(g_d.app);
		}
		y += ROW_H + 2;
	}

	y += 10;
	Gfx_Line(g, panel.x + 10, y, panel.x + panel.w - 10, y, t->border, 1.0f);
	y += 8;
	Gfx_Text(g, t->fLabel, "当前视图组件树",
		Ui_Rect(panel.x + 12, y, panel.w - 24, 18), t->text2, GFX_TEXT_VCENTER);
	y += 24;
	if (g_d.selView >= 0 && g_d.selView < g_d.doc.viewCount) {
		int root = d_view_root();
		g_d.treeRowN = 0;
		if (root >= 0) d_draw_tree_rec(root, 0, &y, panel);
	}
}

static void d_collect_nodes(const UiDesignDoc *doc, int idx, int *out, int cap,
	int *count)
{
	const UiDesignNode *n;
	int i;
	if (idx < 0 || idx >= doc->nodeCount || *count >= cap) return;
	n = &doc->nodes[idx];
	out[(*count)++] = idx;
	for (i = 0; i < n->childCount; i++)
		d_collect_nodes(doc, n->children[i], out, cap, count);
}

static char *d_with_suffix(const char *s, const char *suffix)
{
	size_t a = strlen(s), b = strlen(suffix);
	char *out = (char *)HeapAlloc(GetProcessHeap(), 0, a + b + 1);
	if (!out) return NULL;
	memcpy(out, s, a);
	memcpy(out + a, suffix, b + 1);
	return out;
}

static int d_group_paths(int gi, char **outC, char **outH)
{
	const UiDesignGroup *g = &g_d.doc.groups[gi];
	*outC = g->outputC[0] ? d_strdup(g->outputC) : d_with_suffix(g->id, ".ui.c");
	*outH = g->outputH[0] ? d_strdup(g->outputH) : d_with_suffix(g->id, ".ui.h");
	return *outC && *outH;
}

static int d_validate(char *msg, int cap)
{
	int i, j, k, n, ok = 0;
	int *list = NULL;
	char *ci = NULL, *hi = NULL, *cj = NULL, *hj = NULL;
	char *sym = NULL, *wc = NULL, *wh = NULL, *sym2 = NULL;
	if (g_d.doc.nodeCount > 0) {
		list = (int *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)g_d.doc.nodeCount * sizeof(*list));
		if (!list) goto memfail;
	}
	for (i = 0; i < g_d.doc.groupCount; i++) {
		HeapFree(GetProcessHeap(), 0, ci); ci = NULL;
		HeapFree(GetProcessHeap(), 0, hi); hi = NULL;
		if (!d_group_paths(i, &ci, &hi)) goto memfail;
		for (j = i + 1; j < g_d.doc.groupCount; j++) {
			if (strcmp(g_d.doc.groups[i].id, g_d.doc.groups[j].id) == 0) {
				snprintf(msg, (size_t)cap, "生成组 ID 重复: %s", g_d.doc.groups[i].id);
				goto done;
			}
			if (strcmp(g_d.doc.groups[i].symbolPrefix, g_d.doc.groups[j].symbolPrefix) == 0) {
				snprintf(msg, (size_t)cap, "生成组符号前缀重复: %s", g_d.doc.groups[i].symbolPrefix);
				goto done;
			}
		}
		if (strcmp(ci, hi) == 0) {
			snprintf(msg, (size_t)cap, "输出 .c/.h 路径相同: %s", ci);
			goto done;
		}
		for (j = i + 1; j < g_d.doc.groupCount; j++) {
			HeapFree(GetProcessHeap(), 0, cj); cj = NULL;
			HeapFree(GetProcessHeap(), 0, hj); hj = NULL;
			if (!d_group_paths(j, &cj, &hj)) goto memfail;
			if (strcmp(ci, cj) == 0 || strcmp(ci, hj) == 0 ||
			    strcmp(hi, cj) == 0 || strcmp(hi, hj) == 0) {
				snprintf(msg, (size_t)cap, "生成组输出路径冲突: %s", ci);
				goto done;
			}
		}
	}
	for (i = 0; i < g_d.doc.hostCount; i++) {
		for (j = i + 1; j < g_d.doc.hostCount; j++)
			if (strcmp(g_d.doc.hosts[i].id, g_d.doc.hosts[j].id) == 0) {
				snprintf(msg, (size_t)cap, "窗口 ID 重复: %s", g_d.doc.hosts[i].id);
				goto done;
			}
		if (g_d.doc.hosts[i].view < 0 || g_d.doc.hosts[i].view >= g_d.doc.viewCount) {
			snprintf(msg, (size_t)cap, "宿主未挂载有效视图: %s", g_d.doc.hosts[i].id);
			goto done;
		}
		if (g_d.doc.views[g_d.doc.hosts[i].view].root < 0) {
			snprintf(msg, (size_t)cap, "视图根节点缺失: %s",
				g_d.doc.views[g_d.doc.hosts[i].view].id);
			goto done;
		}
		if (g_d.doc.hosts[i].group >= 0 &&
		    g_d.doc.views[g_d.doc.hosts[i].view].group != g_d.doc.hosts[i].group) {
			snprintf(msg, (size_t)cap, "宿主与视图必须属于同一生成组: %s", g_d.doc.hosts[i].id);
			goto done;
		}
		if (g_d.doc.hosts[i].group < 0) {
			sym = UiDesign_SymbolAlloc(g_d.doc.hosts[i].id);
			wc = sym ? d_with_suffix(sym, ".ui.c") : NULL;
			wh = sym ? d_with_suffix(sym, ".ui.h") : NULL;
			if (!wc || !wh) goto memfail;
			for (j = 0; j < g_d.doc.groupCount; j++) {
				HeapFree(GetProcessHeap(), 0, cj); cj = NULL;
				HeapFree(GetProcessHeap(), 0, hj); hj = NULL;
				if (!d_group_paths(j, &cj, &hj)) goto memfail;
				if (strcmp(wc, cj) == 0 || strcmp(wc, hj) == 0 ||
				    strcmp(wh, cj) == 0 || strcmp(wh, hj) == 0) {
					snprintf(msg, (size_t)cap, "未分组窗口输出与生成组冲突: %s", wc);
					goto done;
				}
			}
			for (j = 0; j < i; j++) {
				if (g_d.doc.hosts[j].group >= 0) continue;
				HeapFree(GetProcessHeap(), 0, sym2); sym2 = NULL;
				HeapFree(GetProcessHeap(), 0, cj); cj = NULL;
				HeapFree(GetProcessHeap(), 0, hj); hj = NULL;
				sym2 = UiDesign_SymbolAlloc(g_d.doc.hosts[j].id);
				cj = sym2 ? d_with_suffix(sym2, ".ui.c") : NULL;
				hj = sym2 ? d_with_suffix(sym2, ".ui.h") : NULL;
				if (!cj || !hj) goto memfail;
				if (strcmp(wc, cj) == 0 || strcmp(wc, hj) == 0 ||
				    strcmp(wh, cj) == 0 || strcmp(wh, hj) == 0) {
					snprintf(msg, (size_t)cap, "未分组窗口输出冲突: %s", wc);
					goto done;
				}
			}
			HeapFree(GetProcessHeap(), 0, sym); sym = NULL;
			HeapFree(GetProcessHeap(), 0, wc); wc = NULL;
			HeapFree(GetProcessHeap(), 0, wh); wh = NULL;
		}
		n = 0;
		d_collect_nodes(&g_d.doc, g_d.doc.views[g_d.doc.hosts[i].view].root,
			list, g_d.doc.nodeCount, &n);
		for (j = 0; j < n; j++)
			for (k = j + 1; k < n; k++)
				if (strcmp(g_d.doc.nodes[list[j]].id, g_d.doc.nodes[list[k]].id) == 0) {
					snprintf(msg, (size_t)cap, "组件 ID 重复: %s", g_d.doc.nodes[list[j]].id);
					goto done;
				}
	}
	for (i = 0; i < g_d.doc.viewCount; i++) {
		for (j = i + 1; j < g_d.doc.viewCount; j++)
			if (strcmp(g_d.doc.views[i].id, g_d.doc.views[j].id) == 0) {
				snprintf(msg, (size_t)cap, "视图 ID 重复: %s", g_d.doc.views[i].id);
				goto done;
			}
		if (g_d.doc.views[i].root < 0) {
			snprintf(msg, (size_t)cap, "视图根节点缺失: %s", g_d.doc.views[i].id);
			goto done;
		}
		n = 0;
		d_collect_nodes(&g_d.doc, g_d.doc.views[i].root, list, g_d.doc.nodeCount, &n);
		for (j = 0; j < n; j++)
			for (k = j + 1; k < n; k++)
				if (strcmp(g_d.doc.nodes[list[j]].id, g_d.doc.nodes[list[k]].id) == 0) {
					snprintf(msg, (size_t)cap, "组件 ID 重复: %s", g_d.doc.nodes[list[j]].id);
					goto done;
				}
	}
	msg[0] = 0;
	for (i = 0; i < g_d.doc.nodeCount; i++) {
		UiDesignNode *tn = &g_d.doc.nodes[i];
		int j2, isRoot = 0;
		for (j2 = 0; j2 < g_d.doc.viewCount; j2++)
			if (g_d.doc.views[j2].root == i) isRoot = 1;
		if (tn->parent < 0) {
			if (isRoot && tn->type == UI_DN_RADIO) {
				snprintf(msg, (size_t)cap, "单选项不能作为根节点: %s", tn->id);
				goto done;
			}
			continue;    /* 已删除的分离节点不参与结构校验 */
		}
		if (tn->parent >= g_d.doc.nodeCount ||
		    !UiDesign_CanParent(g_d.doc.nodes[tn->parent].type, tn->type)) {
			snprintf(msg, (size_t)cap, "节点父容器不合法: %s", tn->id);
			goto done;
		}
		if (tn->type == UI_DN_RADIO && tn->radioGroupUid &&
		    UiDesign_RadioGroupFind(&g_d.doc, tn->radioGroupUid) < 0 && !msg[0])
			snprintf(msg, (size_t)cap, "选项引用了不存在的单选组: %s", tn->id);
		if (tn->type != UI_DN_TABS || tn->parent < 0) continue;
		if (tn->selected < -1 || tn->selected >= tn->childCount) {
			UiDesign_TabsNormalize(&g_d.doc, i);
			g_d.doc.dirty = 1;
		}
		if (tn->childCount == 0 && !msg[0])
			snprintf(msg, (size_t)cap, "标签页无页面: %s", tn->id);
		for (j2 = 0; j2 < tn->childCount; j2++)
			if (!UiDesign_IsContainer(g_d.doc.nodes[tn->children[j2]].type) && !msg[0]) {
				snprintf(msg, (size_t)cap, "标签页含非容器页: %s", g_d.doc.nodes[tn->children[j2]].name);
				break;
			}
	}
	ok = 1;
	goto done;
memfail:
	snprintf(msg, (size_t)cap, "校验失败: 内存不足");
done:
	HeapFree(GetProcessHeap(), 0, list);
	HeapFree(GetProcessHeap(), 0, ci); HeapFree(GetProcessHeap(), 0, hi);
	HeapFree(GetProcessHeap(), 0, cj); HeapFree(GetProcessHeap(), 0, hj);
	HeapFree(GetProcessHeap(), 0, sym); HeapFree(GetProcessHeap(), 0, sym2);
	HeapFree(GetProcessHeap(), 0, wc); HeapFree(GetProcessHeap(), 0, wh);
	return ok;
}

static float d_draw_constraints(UiContext *ui, UiRect panel, float y)
{
	UiTheme *t = &g_d.app->theme;
	UiDesignNode *n = d_selnode();
	static const char *const wLabels[2] = { "固定", "填充" };
	static const char *const hLabels[2] = { "固定", "填充" };
	static const char *const aLabels[3] = { "起点", "居中", "末端" };
	static const char *const kLabels[4] = { "列", "行", "Grid", "自由" };
	int c;
	if (!n) return y;
	Gfx_Text(g_d.app->gfx, t->fLabel, "布局约束",
		Ui_Rect(panel.x + 4, y, 120, 18), t->text2, GFX_TEXT_VCENTER);
	y += 22;
	Gfx_Text(g_d.app->gfx, t->fSmall, "宽",
		Ui_Rect(panel.x + 4, y, 26, 26), t->text3, GFX_TEXT_VCENTER);
	UiButtonGroup_SetSelected(&g_d.constraintW, d_mode_index(n->wMode));
	c = UiButtonGroup_Draw(ui, &g_d.constraintW,
		Ui_Rect(panel.x + 32, y, panel.w - 36, 26), wLabels, NULL, NULL,
		UI_BTN_PRIMARY);
	if (c >= 0) {
		n->wMode = c ? UI_DN_SIZE_FILL : UI_DN_SIZE_FIXED;
		g_d.doc.dirty = 1;
		g_d.layoutReady = 0;
		d_undo_push();
		UiApp_Invalidate(g_d.app);
	}
	y += 30;
	Gfx_Text(g_d.app->gfx, t->fSmall, "高",
		Ui_Rect(panel.x + 4, y, 26, 26), t->text3, GFX_TEXT_VCENTER);
	UiButtonGroup_SetSelected(&g_d.constraintH, d_mode_index(n->hMode));
	c = UiButtonGroup_Draw(ui, &g_d.constraintH,
		Ui_Rect(panel.x + 32, y, panel.w - 36, 26), hLabels, NULL, NULL,
		UI_BTN_PRIMARY);
	if (c >= 0) {
		n->hMode = c ? UI_DN_SIZE_FILL : UI_DN_SIZE_FIXED;
		g_d.doc.dirty = 1;
		g_d.layoutReady = 0;
		d_undo_push();
		UiApp_Invalidate(g_d.app);
	}
	y += 30;
	Gfx_Text(g_d.app->gfx, t->fSmall, "对齐",
		Ui_Rect(panel.x + 4, y, 42, 26), t->text3, GFX_TEXT_VCENTER);
	UiButtonGroup_SetSelected(&g_d.constraintAlign, n->align);
	c = UiButtonGroup_Draw(ui, &g_d.constraintAlign,
		Ui_Rect(panel.x + 46, y, panel.w - 50, 26), aLabels, NULL,
		NULL, UI_BTN_PRIMARY);
	if (c >= 0) { n->align = c; g_d.doc.dirty = 1; g_d.layoutReady = 0; d_undo_push(); UiApp_Invalidate(g_d.app); }
	y += 30;
	if (UiDesign_IsContainer(n->type)) {
		int kind = n->type == UI_DN_ABSOLUTE ? 3
			: (n->type == UI_DN_ROW ? 1 : (n->type == UI_DN_GRID ? 2 : 0));
		Gfx_Text(g_d.app->gfx, t->fSmall, "容器",
			Ui_Rect(panel.x + 4, y, 42, 26), t->text3, GFX_TEXT_VCENTER);
		UiButtonGroup_SetSelected(&g_d.constraintKind, kind);
		c = UiButtonGroup_Draw(ui, &g_d.constraintKind,
			Ui_Rect(panel.x + 46, y, panel.w - 50, 26), kLabels, NULL, NULL,
			UI_BTN_PRIMARY);
		if (c >= 0) {
			int types[4] = { UI_DN_COLUMN, UI_DN_ROW, UI_DN_GRID, UI_DN_ABSOLUTE };
			if (n->type != types[c]) {
				n->type = types[c];
				if (n->type == UI_DN_GRID) d_grid_defaults(n);
				g_d.doc.dirty = 1;
				g_d.layoutReady = 0;
				d_undo_push();
				UiApp_Invalidate(g_d.app);
			}
		}
		y += 32;
	}
	return y;
}

static void d_do_align(int action)
{
	int *list, n, i, j;
	float minX = 0, maxX = 0, minY = 0, maxY = 0;
	if (!g_d.layoutReady) d_layout_current();
	g_d.dragListN = 0;
	d_drag_begin(g_d.selNode);
	list = (int *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)(g_d.dragListN > 0 ? g_d.dragListN : 1) * sizeof(*list));
	if (!list) return;
	for (n = 0; n < g_d.dragListN; n++) list[n] = g_d.dragList[n].idx;
	if (n < 2) {
		HeapFree(GetProcessHeap(), 0, list);
		d_status("对齐需要至少两个自由布局组件");
		return;
	}
	minX = g_d.rects[list[0]].x;
	maxX = minX + g_d.rects[list[0]].w;
	minY = g_d.rects[list[0]].y;
	maxY = minY + g_d.rects[list[0]].h;
	for (i = 1; i < n; i++) {
		UiRect r = g_d.rects[list[i]];
		if (r.x < minX) minX = r.x;
		if (r.x + r.w > maxX) maxX = r.x + r.w;
		if (r.y < minY) minY = r.y;
		if (r.y + r.h > maxY) maxY = r.y + r.h;
	}
	if (action >= 6) {
		/* 分布: 首尾不动, 中间按中心等距. */
		if (n < 3) {
			HeapFree(GetProcessHeap(), 0, list);
			d_status("分布需要至少三个组件");
			return;
		}
		for (i = 1; i < n; i++) {
			int k = list[i];
			for (j = i; j > 0; j--) {
				UiRect a = g_d.rects[list[j - 1]], b = g_d.rects[list[j]];
				float av = action == 6 ? a.x : a.y;
				float bv = action == 6 ? b.x : b.y;
				if (av <= bv) break;
				list[j - 1] = list[j];
				list[j] = k;
			}
		}
		{
			UiRect fr = g_d.rects[list[0]], lr = g_d.rects[list[n - 1]];
			float c0 = action == 6 ? fr.x + fr.w * 0.5f : fr.y + fr.h * 0.5f;
			float c1 = action == 6 ? lr.x + lr.w * 0.5f : lr.y + lr.h * 0.5f;
			float step = (c1 - c0) / (float)(n - 1);
			for (i = 1; i < n - 1; i++) {
				int idx = list[i];
				UiDesignNode *nd = &g_d.doc.nodes[idx];
				int parent = d_layout_parent(idx);
				UiRect pr = g_d.rects[parent];
				float inner = action == 6
					? pr.x + g_d.doc.nodes[parent].padding[0]
					: pr.y + g_d.doc.nodes[parent].padding[1];
				float c = c0 + step * (float)i;
				if (action == 6) nd->x = c - g_d.rects[idx].w * 0.5f - inner;
				else nd->y = c - g_d.rects[idx].h * 0.5f - inner;
			}
		}
	} else {
		for (i = 0; i < n; i++) {
			int idx = list[i];
			UiDesignNode *nd = &g_d.doc.nodes[idx];
			int parent = d_layout_parent(idx);
			UiRect pr = g_d.rects[parent];
			float innerX = pr.x + g_d.doc.nodes[parent].padding[0];
			float innerY = pr.y + g_d.doc.nodes[parent].padding[1];
			UiRect r = g_d.rects[idx];
			if (action == 0) nd->x = minX - innerX;
			else if (action == 1) nd->x = (minX + maxX - r.w) * 0.5f - innerX;
			else if (action == 2) nd->x = maxX - r.w - innerX;
			else if (action == 3) nd->y = minY - innerY;
			else if (action == 4) nd->y = (minY + maxY - r.h) * 0.5f - innerY;
			else if (action == 5) nd->y = maxY - r.h - innerY;
		}
	}
	HeapFree(GetProcessHeap(), 0, list);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status(action >= 6 ? "已分布组件" : "已对齐组件");
	UiApp_Invalidate(g_d.app);
}

static void d_draw_props(void)
{
	UiTheme *t = &g_d.app->theme;
	UiContext *ui = &g_d.app->ui;
	Gfx *g = g_d.app->gfx;
	UiRect panel = Ui_Rect(g_d.app->w - RIGHT_W - 10,
		TOP_H + PALETTE_H + GAP, RIGHT_W,
		g_d.app->h - TOP_H - PALETTE_H - STATUS_H - GAP * 2);
	UiRect props = Ui_Rect(panel.x + 8, panel.y + 40, panel.w - 16, panel.h - 48);
	float y;
	int i;

	/* 属性行不再对应某个 Radio 时, 收起遗留的下拉弹层. */
	if (g_d.propRgReady && UiPopupManager_IsOpen(&g_d.pm, g_d.propRgSelect.id)) {
		UiDesignNode *rn = d_selnode();
		if (g_d.propsTab != 0 || g_d.selKind != DSEL_NODE || !rn ||
		    rn->type != UI_DN_RADIO || g_d.selNode != g_d.propRgNode)
			UiPopupManager_Close(&g_d.pm, ui);
	}

	Ui_Panel(ui, panel);
	{
		UiRect tab1 = Ui_Rect(panel.x + 8, panel.y + 6, 90, 28);
		UiRect tab2 = Ui_Rect(panel.x + 104, panel.y + 6, 100, 28);
		UiWidgetState s1 = Ui_StateInstant(ui, Ui_Id("dsn.tabprop", 0, 0), tab1, 1);
		UiWidgetState s2 = Ui_StateInstant(ui, Ui_Id("dsn.tabcomp", 0, 0), tab2, 1);
		if (g_d.propsTab == 0) Gfx_FillRoundRect(g, tab1, 5, t->primary.soft);
		else if (s1.hovered) Gfx_FillRoundRect(g, tab1, 5, t->rowOn);
		if (g_d.propsTab == 1) Gfx_FillRoundRect(g, tab2, 5, t->primary.soft);
		else if (s2.hovered) Gfx_FillRoundRect(g, tab2, 5, t->rowOn);
		Gfx_Text(g, t->fSmall, "属性", tab1, t->text2, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		Gfx_Text(g, t->fSmall, "组件表", tab2, t->text2, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		if (ui->clicked == Ui_Id("dsn.tabprop", 0, 0)) { d_after_edit(); g_d.propsTab = 0; }
		if (ui->clicked == Ui_Id("dsn.tabcomp", 0, 0)) { d_after_edit(); g_d.propsTab = 1; }
	}
	if (g_d.propsTab == 1) {
		int root = d_view_root();
		int *list, count = 0;
		y = props.y - (float)g_d.propScroll;
		if (root < 0) {
			Gfx_Text(g, t->fSmall, "当前视图没有组件", props, t->text4,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
			return;
		}
		list = (int *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)g_d.doc.nodeCount * sizeof(*list));
		if (!list) return;
		d_collect_nodes(&g_d.doc, root, list, g_d.doc.nodeCount, &count);
		Gfx_PushClip(g, props);
		for (i = 0; i < count && y + 22 < props.y + props.h; i++) {
			int ni = list[i];
			UiDesignNode *nd = &g_d.doc.nodes[ni];
			UiRect rc;
			UiWidgetState st;
			char b[160];
			rc = Ui_Rect(props.x, y, props.w, 22);
			st = Ui_StateInstant(ui, Ui_IdIndex("dsn.comp", ni, 0, 0), rc, 1);
			if (d_sel_has(ni))
				Gfx_FillRoundRect(g, rc, 4, t->primary.soft);
			else if (st.hovered) Gfx_FillRoundRect(g, rc, 4, t->rowOn);
			snprintf(b, sizeof(b), "%-10.10s  %s", nd->id, UiDesign_TypeName(nd->type));
			Gfx_Text(g, t->fSmall, b, Ui_Rect(rc.x + 6, rc.y, rc.w - 8, rc.h),
				t->text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			if (ui->clicked == Ui_IdIndex("dsn.comp", ni, 0, 0)) {
				if (GetKeyState(VK_CONTROL) & 0x8000) d_sel_toggle(ni);
				else d_sel_single(ni);
				UiApp_Invalidate(g_d.app);
			}
			y += 22;
		}
		Gfx_PopClip(g);
		HeapFree(GetProcessHeap(), 0, list);
		g_d.propContentH = (int)(y - (props.y - (float)g_d.propScroll));
		if (g_d.propContentH <= (int)props.h) g_d.propScroll = 0;
		return;
	}

	if (g_d.propsTab == 0 && g_d.selSetN > 1) {
		static const char *const aLabels[6] = { "左", "水平", "右", "上", "垂直", "下" };
		static const char *const dLabels[2] = { "横向分布", "纵向分布" };
		char b[64];
		int c;
		g_d.propN = 0;
		snprintf(b, sizeof(b), "已选 %d 个组件", g_d.selSetN);
		Gfx_Text(g, t->fLabel, b,
			Ui_Rect(props.x, props.y, props.w, 22), t->text2,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		UiButtonGroup_SetSelected(&g_d.multiAlign, -1);
		c = UiButtonGroup_Draw(ui, &g_d.multiAlign,
			Ui_Rect(props.x, props.y + 26, props.w, 26), aLabels,
			NULL, NULL, UI_BTN_PRIMARY);
		if (c >= 0) d_do_align(c);
		UiButtonGroup_SetSelected(&g_d.multiDist, -1);
		c = UiButtonGroup_Draw(ui, &g_d.multiDist,
			Ui_Rect(props.x, props.y + 58, props.w, 26), dLabels,
			NULL, NULL, UI_BTN_PRIMARY);
		if (c >= 0) d_do_align(6 + c);
		Gfx_Text(g, t->fMini, "对齐/分布作用于自由布局子项",
			Ui_Rect(props.x, props.y + 90, props.w, 16), t->text4,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		g_d.propContentH = 110;
		g_d.propScroll = 0;
		return;
	}

	d_build_props();
	if (g_d.editActive && !g_d.editMultiline) UiInput_BeginFrame(&g_d.propForm);
	Gfx_PushClip(g, props);
	y = props.y - (float)g_d.propScroll;
	if (g_d.selKind == DSEL_NODE && d_selnode())
		y = d_draw_constraints(ui, props, y);
	{
		int lastSec = -1;
		for (i = 0; i < g_d.propN; i++) {
			DProp *p = &g_d.props[i];
			UiRect rr = Ui_Rect(props.x, y, props.w, PROP_ROW_H);
			UiRect vr = Ui_Rect(props.x + 128, y + 2, props.w - 132, PROP_ROW_H - 4);
			UiWidgetState st;
			UiId rid = Ui_IdIndex("dsn.proprow", p->key, 0, 0);
			if (p->sec != lastSec) {
				UiRect hr = Ui_Rect(props.x, y, props.w, PROP_SEC_H);
				UiId hid = Ui_IdIndex("dsn.propsec", p->sec, 0, 0);
				UiWidgetState hs = Ui_StateInstant(ui, hid, hr, 1);
				lastSec = p->sec;
				g_d.propRowRc[i] = Ui_Rect(0, 0, 0, 0);
				if (hs.hovered) Gfx_FillRoundRect(g, hr, 4.0f, t->rowOn);
				if (g_d.secCollapsed[p->sec])
					Gfx_FillTriangle(g, hr.x + 8, hr.y + 8, hr.x + 8, hr.y + 16,
						hr.x + 14, hr.y + 12, t->text3);
				else
					Gfx_FillTriangle(g, hr.x + 7, hr.y + 10, hr.x + 15, hr.y + 10,
						hr.x + 11, hr.y + 16, t->text3);
				Gfx_Text(g, t->fSmall, d_sec_title(p->sec),
					Ui_Rect(hr.x + 22, hr.y, hr.w - 26, hr.h), t->text2,
					GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
				if (ui->clicked == hid) {
					d_after_edit();
					g_d.secCollapsed[p->sec] = !g_d.secCollapsed[p->sec];
					UiApp_Invalidate(g_d.app);
				}
				y += PROP_SEC_H;
				rr = Ui_Rect(props.x, y, props.w, PROP_ROW_H);
				vr = Ui_Rect(props.x + 128, y + 2, props.w - 132, PROP_ROW_H - 4);
			}
			float rowH = g_d.editActive && g_d.editMultiline &&
				g_d.editKey == p->key ? 120.0f : PROP_ROW_H;
			rr.h = rowH;
			vr.h = rowH - 4.0f;
			g_d.propRowRc[i] = rr;
			if (y > props.y + props.h) break;
			if (g_d.secCollapsed[p->sec]) {
				g_d.propRowRc[i] = Ui_Rect(0, 0, 0, 0);
				continue;
			}
			st = Ui_StateInstant(ui, rid, rr, 1);
			if (st.hovered) Gfx_FillRoundRect(g, rr, 4.0f, t->rowOn);
			Gfx_Text(g, t->fSmall, p->label,
				Ui_Rect(rr.x + 4, rr.y, 120, rr.h), t->text3,
				GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			if (p->kind == DPK_COLOR) {
				UiDesignNode *cn = d_selnode();
				int isBg = p->key == DK_N_BGCOLOR;
				UiColorSelect *cs = isBg ? &g_d.propColorBg : &g_d.propColor;
				int *syncNode = isBg ? &g_d.propColorBgNode : &g_d.propColorNode;
				unsigned cur = cn ? (isBg ? cn->bgColor : cn->color) : 0;
				if (cn && g_d.propColorReady && g_d.propColorBgReady) {
					/* Draw 会把 changed 清零; 先留存拖动/输入产生的变化.
					 * committed 由应用读取后清 0, 覆盖松手/关闭提交. */
					int wasChanged = cs->changed;
					int drew = 0;
					if (*syncNode != g_d.selNode) {
						UiColorSelect_SetColor(cs, cur);
						*syncNode = g_d.selNode;
					}
					drew = UiColorSelect_Draw(cs, &g_d.pm, ui, vr, cn->enabled);
					if (drew || wasChanged || cs->committed) {
						unsigned c = UiColorSelect_GetColor(cs);
						unsigned old = isBg ? cn->bgColor : cn->color;
						if (c != old) {
							if (isBg) cn->bgColor = c;
							else cn->color = c;
							g_d.doc.dirty = 1;
							UiApp_Invalidate(g_d.app);
						}
					}
				}
				y += PROP_ROW_H;
				continue;
			}
			if (p->key == DK_N_RADIOGROUP) {
				UiDesignNode *rn = d_selnode();
				if (rn && rn->type == UI_DN_RADIO && g_d.propRgReady) {
					int ei = d_rg_combo_index(rn->owner,
						d_node_group_index(g_d.selNode));
					g_d.propRgNode = g_d.selNode;
					UiSelect_SetSelected(&g_d.propRgSelect, ei);
					Ui_Select(&g_d.propRgSelect, &g_d.pm, ui, vr,
						"未分组", 1);
				}
				y += PROP_ROW_H;
				continue;
			}
			if (!(g_d.editActive && g_d.editKey == p->key)) {
				Gfx_FillRoundRect(g, vr, 4.0f, t->inputBg);
				Gfx_StrokeRoundRect(g, vr, 4.0f, t->inputBorder, 1.0f);
				Gfx_Text(g, t->fSmall, p->value,
					Ui_Rect(vr.x + 6, vr.y, vr.w - 12, vr.h), t->text,
					GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			}
			if (ui->clicked == rid && !(g_d.editActive && g_d.editKey == p->key))
				d_prop_start_edit(i);
			y += rowH;
		}
	}
	if (g_d.editActive) {
		for (i = 0; i < g_d.propN; i++) {
			if (g_d.props[i].key == g_d.editKey) {
				UiRect vr = Ui_Rect(props.x + 128, g_d.propRowRc[i].y + 2,
					props.w - 132, g_d.editMultiline ? 116.0f : PROP_ROW_H - 4.0f);
				if (g_d.editMultiline)
					Ui_TextArea(&g_d.propOptionsForm, ui, vr, 1);
				else {
					UiInput_SetRect(&g_d.propForm, 0, vr);
					UiInput_Draw(&g_d.propForm, 0);
				}
				break;
			}
		}
		d_prop_apply_edit();
	}
	Gfx_PopClip(g);
	g_d.propContentH = (int)(y - (props.y - (float)g_d.propScroll));
	if (g_d.propContentH > (int)props.h) {
		int max = g_d.propContentH - (int)props.h;
		if (g_d.propScroll > max) g_d.propScroll = max;
	} else {
		g_d.propScroll = 0;
	}
}

static void d_draw_status(void)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiRect bar = Ui_Rect(0, g_d.app->h - STATUS_H, g_d.app->w, STATUS_H);
	char b[520];
	snprintf(b, sizeof(b), "%s%s  ·  Ctrl+滚轮缩放 %d%%  ·  空格+拖动平移  ·  Ctrl+G 生成",
		g_d.status[0] ? g_d.status : "就绪",
		g_d.preview ? "  [预览]" : "", (int)(g_d.zoom * 100.0f + 0.5f));
	Gfx_FillRoundRectCorners(g, bar,
		Gfx_Corners(0, 0, t->windowRadius, t->windowRadius), t->frame);
	Gfx_Line(g, 0, bar.y, g_d.app->w, bar.y, t->border, 1.0f);
	Gfx_Text(g, t->fSmall, b,
		Ui_Rect(12, bar.y, g_d.app->w - 24, bar.h), t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void d_draw_gen_preview(void)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiContext *ui = &g_d.app->ui;
	UiRect card;
	float w = 560.0f, h, y;
	int i, rows;
	char b[320];
	if (!g_d.genPreview) return;
	rows = g_d.genFileN < 10 ? g_d.genFileN : 10;
	h = 108.0f + (float)rows * 24.0f + 58.0f;
	card = Ui_Rect((g_d.app->w - w) * 0.5f, (g_d.app->h - h) * 0.5f, w, h);
	Gfx_FillRect(g, Ui_Rect(0, 0, g_d.app->w, g_d.app->h), 0x66000000);
	Gfx_FillRoundRect(g, card, 10.0f, t->card);
	Gfx_StrokeRoundRect(g, card, 10.0f, t->border, 1.0f);
	Gfx_Text(g, t->fBig, "生成预览",
		Ui_Rect(card.x + 20, card.y + 14, card.w - 40, 30), t->text,
		GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, g_d.status,
		Ui_Rect(card.x + 20, card.y + 46, card.w - 40, 20), t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	y = card.y + 76;
	for (i = 0; i < rows; i++) {
		UiDesignGenFile *f = &g_d.genFiles[i];
		const char *st;
		GfxColor sc;
		if (f->status == 0) { st = "新增"; sc = t->green; }
		else if (f->status == 1) { st = "更新"; sc = t->orange; }
		else { st = "未变"; sc = t->text4; }
		Gfx_Text(g, t->fSmall, f->name,
			Ui_Rect(card.x + 20, y, card.w - 200, 22), t->text,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (f->status == 0)
			snprintf(b, sizeof(b), "%s  %d 行", st, f->newLines);
		else if (f->status == 1)
			snprintf(b, sizeof(b), "%s  +%d -%d 行", st, f->added, f->removed);
		else
			snprintf(b, sizeof(b), "%s  %d 行", st, f->newLines);
		Gfx_Text(g, t->fSmall, b,
			Ui_Rect(card.x + card.w - 176, y, 156, 22), sc,
			GFX_TEXT_VCENTER | GFX_TEXT_RIGHT);
		y += 24.0f;
	}
	if (g_d.genFileN > rows) {
		snprintf(b, sizeof(b), "… 另有 %d 个文件", g_d.genFileN - rows);
		Gfx_Text(g, t->fSmall, b,
			Ui_Rect(card.x + 20, y, card.w - 40, 20), t->text4,
			GFX_TEXT_VCENTER);
	}
	{
		UiRect rc = Ui_Rect(card.x + card.w - 196, card.y + card.h - 46, 88, 32);
		UiRect rb = Ui_Rect(card.x + card.w - 100, card.y + card.h - 46, 84, 32);
		if (Ui_Button(ui, Ui_Id("dsn.gen.cancel", 0, 0), rc, "取消",
			UI_BTYPE_DEFAULT, 1)) {
			g_d.genPreview = 0;
			UiApp_Invalidate(g_d.app);
		}
		if (Ui_Button(ui, Ui_Id("dsn.gen.ok", 0, 0), rb, "生成",
			UI_BTYPE_PRIMARY, 1))
			d_generate_commit();
	}
}

static void d_draw_all(void)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, g_d.app->w, g_d.app->h),
		t->windowRadius, t->page, t->page2);
	d_draw_topbar();
	d_draw_toolbar();
	d_draw_left();
	d_draw_canvas();
	d_draw_props();
	d_draw_status();
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, g_d.app->w - 1.0f,
		g_d.app->h - 1.0f), t->windowRadius - 0.5f, t->border, 1.0f);
	d_draw_gen_preview();
}

/* ── 预览 ───────────────────────────────────────────────────────────── */

static void d_preview_update(void)
{
	if (!g_d.preview) {
		d_preview_shutdown();
		return;
	}
	if (g_d.selWin < 0 || g_d.selWin >= g_d.doc.hostCount) return;
	if (g_d.doc.hosts[g_d.selWin].view < 0 ||
	    g_d.doc.hosts[g_d.selWin].view >= g_d.doc.viewCount) return;
	if (!g_d.instReady || g_d.inst.host != g_d.selWin) {
		if (g_d.instReady) UiDesignInstance_Shutdown(&g_d.inst);
		if (UiDesignInstance_Init(&g_d.inst, &g_d.doc, g_d.selWin,
			&g_d.app->ui, &g_d.pm, &g_d.app->ime))
			g_d.instReady = 1;
		g_d.previewDialogTried = 0;
	}
}

/* ── 鼠标/键盘 ──────────────────────────────────────────────────────── */

static int d_handle_at(int idx, float x, float y)
{
	UiRect r;
	float cx[3], cy[3], s = 9.0f;
	int i, j, k = 0;
	if (idx < 0 || idx >= g_d.doc.nodeCount) return -1;
	r = g_d.rects[idx];
	cx[0] = r.x; cx[1] = r.x + r.w * 0.5f; cx[2] = r.x + r.w;
	cy[0] = r.y; cy[1] = r.y + r.h * 0.5f; cy[2] = r.y + r.h;
	for (j = 0; j < 3; j++)
		for (i = 0; i < 3; i++) {
			if (i == 1 && j == 1) continue;
			if (x >= cx[i] - s * 0.5f && x <= cx[i] + s * 0.5f &&
			    y >= cy[j] - s * 0.5f && y <= cy[j] + s * 0.5f)
				return k;
			k++;
		}
	return -1;
}

static float d_round4(float v)
{
	return (float)(int)(v / 4.0f + (v >= 0 ? 0.5f : -0.5f)) * 4.0f;
}

static int d_container_at(float x, float y)
{
	int hit;
	if (!g_d.layoutReady) d_layout_current();
	hit = UiDesign_FindNodeAt(&g_d.doc, g_d.selView, g_d.rects, x, y);
	if (hit < 0 && g_d.selView >= 0)
		hit = d_view_root();
	while (hit >= 0) {
		UiDesignNode *n = &g_d.doc.nodes[hit];
		if (UiDesign_IsContainer(n->type)) {
			if (n->type == UI_DN_TABS && n->childCount > 0) {
				int sel = n->selected;
				if (sel < 0) sel = 0;
				if (sel >= n->childCount) sel = n->childCount - 1;
				if (UiDesign_IsContainer(g_d.doc.nodes[n->children[sel]].type))
					return n->children[sel];
				return -1;   /* 叶子页内部不可插入 */
			}
			return hit;
		}
		hit = n->parent;
	}
	return d_view_root();
}

static void d_insert_at(float x, float y)
{
	int parent, idx = -1, type = g_d.insertType, gi = -1;
	UiDesignNode *p;
	char name[64];
	char *id;
	if (type < 0 || g_d.selView < 0) return;
	if (type == D_INSERT_RADIOGROUP) {
		parent = d_container_at(x, y);
		if (parent < 0) {
			d_status("当前页不是容器, 无法放入单选组");
			return;
		}
		p = UiDesign_Node(&g_d.doc, parent);
		if (!p || !UiDesign_CanParent(p->type, UI_DN_RADIO)) {
			d_status("当前容器不能放入单选组");
			return;
		}
		d_after_edit();
		if (d_new_radio_group(parent, x, y, 1) < 0)
			d_status("添加单选组失败");
		return;
	}
	if (type == UI_DN_RADIO) {
		if (g_d.selKind == DSEL_RADIOGROUP) gi = d_sel_radio_group_index();
		else if (g_d.selKind == DSEL_NODE) gi = d_node_group_index(g_d.selNode);
		if (gi < 0) {
			d_status("单选项只能插入所选单选组内");
			return;
		}
		parent = d_rg_member_parent(gi);
		if (parent < 0) {
			d_status("单选组没有可用容器");
			return;
		}
	} else {
		parent = d_container_at(x, y);
		if (parent < 0) {
			d_status("当前页不是容器, 无法放入组件");
			return;
		}
		p = UiDesign_Node(&g_d.doc, parent);
		if (!p || !UiDesign_CanParent(p->type, type)) {
			d_status("当前容器不能放入该组件");
			return;
		}
	}
	d_after_edit();
	if (type == UI_DN_RADIO) {
		idx = d_append_radio(gi, x, y);
	} else {
		UiDesign_SetText(name, sizeof(name),
			type == UI_DN_RINGPROGRESS ? "圆环" : UiDesign_TypeName(type));
		id = d_unique_node_id(g_d.selView, UiDesign_TypeName(type));
		if (!id) { d_status("内存不足"); return; }
		idx = UiDesign_AddNode(&g_d.doc, g_d.selView, type, parent, id, name);
		HeapFree(GetProcessHeap(), 0, id);
		if (idx >= 0) d_init_added_node(idx, parent, type, x, y);
	}
	if (idx < 0) { d_status("插入组件失败"); return; }
	if (type != UI_DN_RADIO) {
		int lp = d_layout_parent(idx);
		p = UiDesign_Node(&g_d.doc, lp);
		if (p && (p->type == UI_DN_ABSOLUTE || p->type == UI_DN_PANEL)) {
			UiRect pr = g_d.rects[lp];
			g_d.doc.nodes[idx].x = d_round4(x - pr.x - p->padding[0]);
			g_d.doc.nodes[idx].y = d_round4(y - pr.y - p->padding[1]);
		}
	}
	d_sel_single(idx);
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已插入组件 · Esc 退出插入工具");
}

static int d_tree_row_at(float x, float y)
{
	int i;
	for (i = 0; i < g_d.treeRowN; i++)
		if (Ui_RectContains(g_d.treeRows[i].rc, x, y)) return i;
	return -1;
}

static int d_tree_child_pos(int parent, int child)
{
	int i;
	if (parent < 0 || parent >= g_d.doc.nodeCount) return 0;
	for (i = 0; i < g_d.doc.nodes[parent].childCount; i++)
		if (g_d.doc.nodes[parent].children[i] == child) return i;
	return g_d.doc.nodes[parent].childCount;
}

static void d_tree_drag_finish(void)
{
	int src = g_d.treeDragIdx, target = g_d.treeDropIdx;
	int mode = g_d.treeDropMode;
	int parent = -1, pos = 0;
	g_d.treeDragIdx = -1;
	g_d.treeDropIdx = -1;
	if (src >= 0 && target >= 0 && src != target) {
		UiDesignNode *dn = &g_d.doc.nodes[target];
		int srcParent = g_d.doc.nodes[src].parent;
		int srcIsPage = srcParent >= 0 &&
			g_d.doc.nodes[srcParent].type == UI_DN_TABS;
		int srcIsRadio = g_d.doc.nodes[src].type == UI_DN_RADIO;
		if (mode == 1 && UiDesign_IsContainer(dn->type)) {
			parent = target;
			pos = dn->childCount;
		} else {
			parent = dn->parent;
			pos = d_tree_child_pos(parent, target) + (mode == 2 ? 1 : 0);
		}
		if (srcIsPage && parent != srcParent) {
			d_status("页面只能在所属标签页内重排, 不能拖出或移入其他容器");
			UiApp_Invalidate(g_d.app);
			return;
		}
		if (srcIsRadio && parent != srcParent) {
			d_status("单选项只能在所属单选组内重排");
			UiApp_Invalidate(g_d.app);
			return;
		}
		if (!srcIsPage && parent >= 0 &&
		    g_d.doc.nodes[parent].type == UI_DN_TABS) {
			d_status("普通节点不能直接拖入标签页; 请用属性页“新增页”");
			UiApp_Invalidate(g_d.app);
			return;
		}
		if (parent >= 0 && !UiDesign_CanParent(g_d.doc.nodes[parent].type,
		    g_d.doc.nodes[src].type)) {
			d_status("目标容器不能接收该组件");
			UiApp_Invalidate(g_d.app);
			return;
		}
		if (parent >= 0 && UiDesign_NodeMove(&g_d.doc, src, parent, pos)) {
			g_d.doc.dirty = 1;
			g_d.layoutReady = 0;
			d_undo_push();
			d_status(srcIsPage ? "页面顺序已调整" : "组件树层级已调整");
		}
	}
	UiApp_Invalidate(g_d.app);
}

static void d_mouse_down(float x, float y)
{
	UiDesignNode *n;
	int hitNode, handle;
	float dx, dy;
	if (g_d.genPreview) return;
	if (UiPopupManager_AnyOpen(&g_d.pm)) return;
	g_d.dragClickNode = -1;
	if (g_d.preview) {
		if (g_d.instReady) UiDesign_MouseDown(&g_d.inst, x, y);
		return;
	}
	if (g_d.editActive) {
		UiId hit = Ui_HitTest(&g_d.app->ui, x, y);
		if (g_d.editMultiline) {
			if (hit == g_d.propOptionsForm.id) return;
		} else if (hit == g_d.propForm.fields[0].id) {
			UiInput_MouseDown(&g_d.propForm, hit, x);
			return;
		}
		d_after_edit();
	}
	{
		int tr = d_tree_row_at(x, y);
		if (tr >= 0) {
			int idx = g_d.treeRows[tr].idx;
			if (g_d.treeRows[tr].kind == DROW_RADIOGROUP) {
				d_sel_radio_group(g_d.treeRows[tr].group);
				g_d.treeDragIdx = -1;
				g_d.treeDropIdx = -1;
				return;
			}
			if (GetKeyState(VK_CONTROL) & 0x8000) d_sel_toggle(idx);
			else d_sel_single(idx);
			g_d.treeDragIdx = idx == d_view_root() ? -1 : idx;
			g_d.treeDropIdx = -1;
			g_d.treeDropMode = 1;
			return;
		}
	}
	if ((GetKeyState(VK_SHIFT) & 0x8000) && !g_d.editActive) {
		int i;
		for (i = 0; i < g_d.propN; i++) {
			DProp *p = &g_d.props[i];
			if (p->kind != DPK_FLOAT && p->kind != DPK_INT) continue;
			if (!Ui_RectContains(g_d.propRowRc[i], x, y)) continue;
			g_d.propDrag = 1;
			g_d.propDragKey = p->key;
			g_d.propDragX = x;
			g_d.propDragIsInt = p->kind == DPK_INT;
			g_d.propDragVal = p->kind == DPK_INT
				? (float)d_atoi(p->value) : (float)d_atof(p->value);
			d_status("数值拖动: 左右移动, Shift 已按下 (松开提交)");
			return;
		}
	}
	if (d_in_canvas(x, y) && (GetKeyState(VK_SPACE) & 0x8000)) {
		g_d.panning = 1;
		g_d.panStartX = x;
		g_d.panStartY = y;
		g_d.panBaseX = g_d.panX;
		g_d.panBaseY = g_d.panY;
		return;
	}
	if (g_d.insertType >= 0 && !d_in_canvas(x, y)) return;
	if (!d_to_design(x, y, &dx, &dy)) return;
	x = dx;
	y = dy;
	if (g_d.insertType >= 0) {
		d_layout_current();
		d_insert_at(x, y);
		return;
	}
	if (!g_d.layoutReady) d_layout_current();
	handle = -1;
	if (g_d.selKind == DSEL_NODE && g_d.selNode >= 0)
		handle = d_handle_at(g_d.selNode, x, y);
	if (handle >= 0) {
		n = d_selnode();
		if (n && n->parent >= 0) {
			UiRect vr = g_d.rects[g_d.selNode];
			g_d.dragging = 2;
			g_d.dragHandle = handle;
			g_d.dragStartX = x;
			g_d.dragStartY = y;
			g_d.dragNodeX = n->x;
			g_d.dragNodeY = n->y;
			/* FILL 节点的 w/h 不是实际尺寸; 首次拖动手柄即转 FIXED,
			 * 必须以画布矩形为基准, 否则会先跳到模型里的默认宽高. */
			g_d.dragNodeW = vr.w;
			g_d.dragNodeH = vr.h;
			g_d.dragMoved = 0;
		}
		return;
	}
	hitNode = UiDesign_FindNodeAt(&g_d.doc, g_d.selView, g_d.rects, x, y);
	if (hitNode >= 0) {
		UiDesignNode *p;
		UiDesignNode *hn = &g_d.doc.nodes[hitNode];
		int additive = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (hn->type == UI_DN_TABS && hn->childCount > 0) {
			int ti = d_tabs_item_at(hitNode, x, y);
			if (ti >= 0) {
				if (hn->selected != ti) {
					hn->selected = ti;
					g_d.doc.dirty = 1;
					g_d.layoutReady = 0;
					d_undo_push();
				}
				d_sel_single(hn->children[ti]);
				UiApp_Invalidate(g_d.app);
				return;
			}
		}
		{
			int dragNode = hitNode, gi = -1, lp;
			g_d.dragClickNode = -1;
			if (hn->type == UI_DN_RADIO && hn->radioGroupUid)
				gi = d_node_group_index(hitNode);
			if (gi >= 0 && !additive) {
				d_sel_radio_group(gi);
				dragNode = hitNode;
			} else if (additive) d_sel_toggle(hitNode);
			else if (d_sel_has(hitNode)) {
				g_d.selNode = hitNode;
				g_d.selKind = DSEL_NODE;
			} else d_sel_single(hitNode);
			n = &g_d.doc.nodes[dragNode];
			lp = d_layout_parent(dragNode);
			p = lp >= 0 ? &g_d.doc.nodes[lp] : NULL;
			/* 单选组仅在自由布局容器中整体拖动; 结构化容器里拖动会拆散成员. */
			if (p && dragNode != d_view_root() &&
			    (gi >= 0 ? d_parent_free(lp) : d_parent_free(lp) ||
			     p->type == UI_DN_ROW || p->type == UI_DN_COLUMN || p->type == UI_DN_GRID) &&
			    (gi >= 0 || d_sel_has(dragNode))) {
				g_d.dragging = 1;
				g_d.dragStartX = x;
				g_d.dragStartY = y;
				g_d.dragNodeX = n->x;
				g_d.dragNodeY = n->y;
				g_d.dragMoved = 0;
				d_drag_begin(dragNode);
			}
		}
		UiApp_Invalidate(g_d.app);
		return;
	}
	/* 空白: 框选 */
	if (!(GetKeyState(VK_CONTROL) & 0x8000)) d_sel_clear();
	g_d.marquee = 1;
	g_d.marqueeX0 = g_d.marqueeX1 = x;
	g_d.marqueeY0 = g_d.marqueeY1 = y;
	UiApp_Invalidate(g_d.app);
}

static void d_snap_move(UiDesignNode *n, float *x, float *y)
{
	int idx = (int)(n - g_d.doc.nodes);
	int parent = d_layout_parent(idx);
	UiRect pr, inner, nr;
	float bestX = 4.0f, bestY = 4.0f;
	int i, k, count;
	if (parent < 0) return;
	pr = g_d.rects[parent];
	nr = g_d.rects[idx];
	d_inset(pr, g_d.doc.nodes[parent].padding, &inner);
	g_d.snapX = g_d.snapY = 0;
	count = d_layout_item_count(parent);
	for (i = 0; i < count; i++) {
		int ci = d_layout_item_at(parent, i);
		UiRect r;
		if (ci == idx || d_sel_has(ci) || d_has_selected_ancestor(ci)) continue;
		r = g_d.rects[ci];
		for (k = 0; k < 3; k++) {
			float tx = k == 0 ? r.x : (k == 1 ? r.x + r.w * 0.5f : r.x + r.w);
			float ty = k == 0 ? r.y : (k == 1 ? r.y + r.h * 0.5f : r.y + r.h);
			float ax[3] = { *x + inner.x, *x + inner.x + nr.w * 0.5f,
				*x + inner.x + nr.w };
			float ay[3] = { *y + inner.y, *y + inner.y + nr.h * 0.5f,
				*y + inner.y + nr.h };
			int a;
			for (a = 0; a < 3; a++) {
				float dx = ax[a] - tx;
				float dy = ay[a] - ty;
				if (fabsf_local(dx) < bestX) {
					bestX = fabsf_local(dx);
					*x -= dx;
					g_d.snapX = 1;
					g_d.snapLineX = tx;
				}
				if (fabsf_local(dy) < bestY) {
					bestY = fabsf_local(dy);
					*y -= dy;
					g_d.snapY = 1;
					g_d.snapLineY = ty;
				}
			}
		}
	}
	*x = d_round4(*x);
	*y = d_round4(*y);
}

static int d_node_overflow(const UiDesignNode *n)
{
	int idx = (int)(n - g_d.doc.nodes);
	int parent = d_layout_parent(idx);
	UiRect pr, inner, nr;
	if (!n || parent < 0) return 0;
	pr = g_d.rects[parent];
	d_inset(pr, g_d.doc.nodes[parent].padding, &inner);
	nr = g_d.rects[idx];
	return nr.x < inner.x - 1 || nr.y < inner.y - 1 ||
		nr.x + nr.w > inner.x + inner.w + 1 ||
		nr.y + nr.h > inner.y + inner.h + 1;
}

static void d_reorder_drag(UiDesignNode *n, float dx, float dy)
{
	int idx = (int)(n - g_d.doc.nodes);
	int lp = d_layout_parent(idx), parent = n->parent;
	int type, steps, target, current = 0, i;
	if (lp < 0 || parent < 0 || parent != g_d.dragReorderParent) return;
	type = g_d.doc.nodes[lp].type;
	if (type != UI_DN_ROW && type != UI_DN_COLUMN && type != UI_DN_GRID) return;
	if (type == UI_DN_ROW || (type == UI_DN_GRID && fabsf_local(dx) >= fabsf_local(dy)))
		steps = (int)(dx / 32.0f);
	else steps = (int)(dy / 32.0f);
	target = g_d.dragReorderPos + steps;
	if (target < 0) target = 0;
	if (target >= g_d.doc.nodes[parent].childCount)
		target = g_d.doc.nodes[parent].childCount - 1;
	for (i = 0; i < g_d.doc.nodes[parent].childCount; i++)
		if (g_d.doc.nodes[parent].children[i] == idx) { current = i; break; }
	while (current < target && UiDesign_NodeMoveDown(&g_d.doc, idx)) current++;
	while (current > target && UiDesign_NodeMoveUp(&g_d.doc, idx)) current--;
}

static void d_mouse_move(float x, float y)
{
	if (g_d.genPreview) return;
	if (g_d.preview) {
		if (g_d.instReady) UiDesign_MouseMove(&g_d.inst, x, y);
		return;
	}
	if (g_d.propDrag) {
		float dv = (x - g_d.propDragX) * (g_d.propDragIsInt ? 0.25f : 0.5f);
		char b[64];
		float v = g_d.propDragVal + dv;
		if (g_d.propDragIsInt) snprintf(b, sizeof(b), "%d", (int)(v + (v >= 0 ? 0.5f : -0.5f)));
		else d_fmt(b, sizeof(b), v);
		d_prop_apply_text(g_d.propDragKey, b);
		d_status(b);
		UiApp_Invalidate(g_d.app);
		return;
	}
	if (g_d.editActive) {
		if (!g_d.editMultiline) UiInput_MouseMove(&g_d.propForm, x);
		return;
	}
	if (g_d.panning) {
		g_d.panX = g_d.panBaseX + (x - g_d.panStartX);
		g_d.panY = g_d.panBaseY + (y - g_d.panStartY);
		UiApp_Invalidate(g_d.app);
		return;
	}
	if (g_d.treeDragIdx >= 0) {
		int tr = d_tree_row_at(x, y);
		if (tr < 0 || g_d.treeRows[tr].kind == DROW_RADIOGROUP) {
			g_d.treeDropIdx = -1;
		} else {
			UiRect rc = g_d.treeRows[tr].rc;
			float t = (y - rc.y) / (rc.h > 0 ? rc.h : 1.0f);
			g_d.treeDropIdx = g_d.treeRows[tr].idx;
			g_d.treeDropMode = t < 0.25f ? 0 : (t > 0.75f ? 2 : 1);
		}
		UiApp_Invalidate(g_d.app);
		return;
	}
	if (g_d.marquee) {
		g_d.marqueeX1 = (x - g_d.viewTx) / g_d.zoom;
		g_d.marqueeY1 = (y - g_d.viewTy) / g_d.zoom;
		UiApp_Invalidate(g_d.app);
		return;
	}
	if (g_d.dragging) {
			float dx = (x - g_d.viewTx) / g_d.zoom - g_d.dragStartX;
			float dy = (y - g_d.viewTy) / g_d.zoom - g_d.dragStartY;
			UiDesignNode *n = d_selnode();
			int primary = g_d.selNode;
			if (!n && g_d.selKind == DSEL_RADIOGROUP) {
				primary = g_d.dragListN > 0 ? g_d.dragList[0].idx : -1;
				n = UiDesign_Node(&g_d.doc, primary);
			}
			if (!n) return;
			if (fabsf_local(dx) + fabsf_local(dy) > 1.5f) g_d.dragMoved = 1;
			if (g_d.dragging == 1) {
				int lp = d_layout_parent(primary);
				if (lp >= 0 && d_parent_free(lp)) {
				float nx = g_d.dragNodeX + dx, ny = g_d.dragNodeY + dy;
				float sdx, sdy;
				int i;
				d_snap_move(n, &nx, &ny);
				sdx = nx - g_d.dragNodeX;
				sdy = ny - g_d.dragNodeY;
				for (i = 0; i < g_d.dragListN; i++) {
					DDragItem *item = &g_d.dragList[i];
					UiDesignNode *dn = UiDesign_Node(&g_d.doc, item->idx);
					if (!dn) continue;
					dn->x = item->x + sdx;
					dn->y = item->y + sdy;
				}
			} else d_reorder_drag(n, dx, dy);
		} else {
			/* 按抓取的手柄只改对应方向: 上下边只改高, 左右边只改宽,
			 * 左边/上边同时移动节点原点. 手柄编号见 d_handle_at. */
			int k = g_d.dragHandle;
			int left = (k == 0 || k == 3 || k == 5);
			int right = (k == 2 || k == 4 || k == 7);
			int top = (k == 0 || k == 1 || k == 2);
			int bottom = (k == 5 || k == 6 || k == 7);
			float nx = g_d.dragNodeX, ny = g_d.dragNodeY;
			float nw = g_d.dragNodeW, nh = g_d.dragNodeH;
			if (left) { nx += dx; nw -= dx; }
			else if (right) nw += dx;
			if (top) { ny += dy; nh -= dy; }
			else if (bottom) nh += dy;
			if (nw < 8.0f) {
				if (left) nx = g_d.dragNodeX + g_d.dragNodeW - 8.0f;
				nw = 8.0f;
			}
			if (nh < 8.0f) {
				if (top) ny = g_d.dragNodeY + g_d.dragNodeH - 8.0f;
				nh = 8.0f;
			}
			n->x = d_round4(nx);
			n->y = d_round4(ny);
			n->w = d_round4(nw);
			n->h = d_round4(nh);
			if (left || right) n->wMode = UI_DN_SIZE_FIXED;
			if (top || bottom) n->hMode = UI_DN_SIZE_FIXED;
			g_d.snapX = g_d.snapY = 0;
		}
		if (d_node_overflow(n))
			d_status("布局警告: 节点超出容器范围");
		g_d.doc.dirty = 1;
		g_d.layoutReady = 0;
		UiApp_Invalidate(g_d.app);
	}
}

static void d_mouse_up(void)
{
	if (g_d.genPreview) return;
	if (g_d.preview) {
		if (g_d.instReady) UiDesign_MouseUp(&g_d.inst);
		return;
	}
	if (g_d.treeDragIdx >= 0) {
		d_tree_drag_finish();
		return;
	}
	if (g_d.propDrag) {
		g_d.propDrag = 0;
		d_undo_push();
		d_status("数值已修改");
		return;
	}
	if (g_d.editActive) {
		if (!g_d.editMultiline) UiInput_MouseUp(&g_d.propForm);
		return;
	}
	if (g_d.panning) {
		g_d.panning = 0;
		return;
	}
	if (g_d.marquee) {
		UiRect r = Ui_Rect(
			g_d.marqueeX0 < g_d.marqueeX1 ? g_d.marqueeX0 : g_d.marqueeX1,
			g_d.marqueeY0 < g_d.marqueeY1 ? g_d.marqueeY0 : g_d.marqueeY1,
			fabsf_local(g_d.marqueeX1 - g_d.marqueeX0),
			fabsf_local(g_d.marqueeY1 - g_d.marqueeY0));
		int i;
		g_d.marquee = 0;
		if (r.w > 2.0f || r.h > 2.0f) {
			for (i = 0; i < g_d.doc.nodeCount; i++) {
				UiDesignNode *nd = &g_d.doc.nodes[i];
				UiRect nr;
				if (nd->owner != g_d.selView || !nd->visible) continue;
				if (i == d_view_root()) continue;
				nr = g_d.rects[i];
				if (nr.x < r.x + r.w && nr.x + nr.w > r.x &&
				    nr.y < r.y + r.h && nr.y + nr.h > r.y) {
					int k, has = 0;
					for (k = 0; k < g_d.selSetN; k++)
						if (g_d.selSet[k] == i) has = 1;
					if (!has) d_sel_add(i);
					g_d.selNode = i;
				}
			}
			g_d.selKind = g_d.selSetN > 0 ? DSEL_NODE : DSEL_WINDOW;
		}
		UiApp_Invalidate(g_d.app);
		return;
	}
	if (g_d.dragging) {
		int moved = g_d.dragMoved;
		int clickNode = g_d.dragClickNode;
		g_d.dragging = 0;
		g_d.dragMoved = 0;
		g_d.dragClickNode = -1;
		g_d.dragListN = 0;
		g_d.snapX = g_d.snapY = 0;
		if (moved) d_undo_push();
		else if (clickNode >= 0) d_sel_single(clickNode);
		d_status(moved ? "布局已修改" : "已选中");
	}
}

static float fabsf_local(float v) { return v < 0 ? -v : v; }

static int d_key(UINT vk)
{
	int ctrl;
	ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	if (g_d.genPreview) {
		if (vk == VK_ESCAPE) {
			g_d.genPreview = 0;
			UiApp_Invalidate(g_d.app);
		}
		return 1;
	}
	if (g_d.preview) {
		if (g_d.instReady && UiDesign_DialogIsOpen(&g_d.inst) &&
		    UiDialog_Key(&g_d.inst.dialog, &g_d.app->ui, vk))
			return 1;
		if (vk == VK_ESCAPE) {
			g_d.preview = 0;
			d_preview_shutdown();
			d_status("设计模式");
			return 1;
		}
		if (g_d.instReady && UiDesign_Key(&g_d.inst, vk)) return 1;
		return 0;
	}
	if (!g_d.editActive && g_d.propColorReady &&
	    UiColorSelect_Key(&g_d.propColor, &g_d.pm, &g_d.app->ui, vk))
		return 1;
	if (!g_d.editActive && g_d.propColorBgReady &&
	    UiColorSelect_Key(&g_d.propColorBg, &g_d.pm, &g_d.app->ui, vk))
		return 1;
	if (!g_d.editActive && g_d.propRgReady &&
	    UiPopupManager_IsOpen(&g_d.pm, g_d.propRgSelect.id) &&
	    UiSelect_Key(&g_d.propRgSelect, &g_d.pm, &g_d.app->ui, vk)) {
		d_rg_combo_commit();
		return 1;
	}
	if (g_d.editActive) {
		if (vk == VK_ESCAPE || vk == VK_TAB ||
		    (vk == VK_RETURN && !(GetKeyState(VK_SHIFT) & 0x8000))) {
			d_after_edit();
			return 1;
		}
		if (g_d.editMultiline)
			return UiTextArea_Key(&g_d.propOptionsForm, &g_d.app->ui, vk);
		return UiInput_Key(&g_d.propForm, g_d.app->hwnd, vk);
	}
	if (vk == VK_ESCAPE && g_d.insertType >= 0) {
		g_d.insertType = -1;
		d_status("选择工具");
		return 1;
	}
	if (ctrl && vk == 'S') {
		d_save_file((GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0);
		return 1;
	}
	if (ctrl && vk == 'O') { d_open_file(); return 1; }
	if (ctrl && vk == 'N') { d_new_doc(); return 1; }
	if (ctrl && vk == 'Z') { d_undo_step(0); return 1; }
	if (ctrl && vk == 'Y') { d_undo_step(1); return 1; }
	if (ctrl && vk == 'D') { d_dup_node(); return 1; }
	if (ctrl && vk == 'G') { d_generate(); return 1; }
	if (ctrl && (vk == '0' || vk == VK_NUMPAD0)) {
		g_d.zoom = 1.0f;
		g_d.panX = g_d.panY = 0;
		d_status("画布缩放已复位");
		return 1;
	}
	if (vk == VK_DELETE) { d_del_selected(); return 1; }
	if (g_d.selKind == DSEL_RADIOGROUP) {
		int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 8 : 1;
		int count = d_rg_member_count(g_d.selRadioGroup);
		int lp = d_layout_parent(d_rg_member_at(g_d.selRadioGroup, 0));
		float dx = 0, dy = 0;
		if (lp >= 0 && d_parent_free(lp) && count > 0) {
			if (vk == VK_LEFT) dx = -(float)step;
			else if (vk == VK_RIGHT) dx = (float)step;
			else if (vk == VK_UP) dy = -(float)step;
			else if (vk == VK_DOWN) dy = (float)step;
			else return 0;
			d_translate_layout_node(d_rg_member_at(g_d.selRadioGroup, 0), dx, dy);
			goto moved;
		}
	}
	if (g_d.selKind == DSEL_NODE && g_d.selNode >= 0) {
		UiDesignNode *n = d_selnode();
		int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 8 : 1;
		{
			int lp = n ? d_layout_parent(g_d.selNode) : -1;
			if (n && lp >= 0 && d_parent_free(lp)) {
				if (vk == VK_LEFT) { d_translate_layout_node(g_d.selNode, -(float)step, 0); goto moved; }
				if (vk == VK_RIGHT) { d_translate_layout_node(g_d.selNode, (float)step, 0); goto moved; }
				if (vk == VK_UP) { d_translate_layout_node(g_d.selNode, 0, -(float)step); goto moved; }
				if (vk == VK_DOWN) { d_translate_layout_node(g_d.selNode, 0, (float)step); goto moved; }
			}
		}
	}
	return 0;
moved:
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	UiApp_Invalidate(g_d.app);
	return 1;
}

static int d_chr(wchar_t ch)
{
	if (g_d.genPreview) return 0;
	if (g_d.preview) return g_d.instReady ? UiDesign_Char(&g_d.inst, ch) : 0;
	if (!g_d.editActive && g_d.propColorReady &&
	    UiColorSelect_Char(&g_d.propColor, &g_d.app->ui, ch))
		return 1;
	if (!g_d.editActive && g_d.propColorBgReady &&
	    UiColorSelect_Char(&g_d.propColorBg, &g_d.app->ui, ch))
		return 1;
	if (g_d.editActive) {
		if (g_d.editMultiline) return UiTextArea_Char(&g_d.propOptionsForm, &g_d.app->ui, ch);
		return UiInput_Char(&g_d.propForm, ch);
	}
	return 0;
}

static void d_ime(void)
{
	if (g_d.preview) { if (g_d.instReady) UiDesign_OnIme(&g_d.inst); }
	else if (g_d.editActive) {
		if (g_d.editMultiline) UiTextArea_OnIme(&g_d.propOptionsForm, &g_d.app->ui);
		else UiInput_OnIme(&g_d.propForm);
	}
	else {
		if (g_d.propColorReady) UiColorSelect_OnIme(&g_d.propColor, &g_d.app->ui);
		if (g_d.propColorBgReady) UiColorSelect_OnIme(&g_d.propColorBg, &g_d.app->ui);
	}
}

static void d_blink(void)
{
	if (g_d.preview) { if (g_d.instReady) UiDesign_Blink(&g_d.inst); }
	else {
		if (g_d.editActive) {
			if (g_d.editMultiline) UiTextArea_Blink(&g_d.propOptionsForm, &g_d.app->ui);
			else UiInput_Blink(&g_d.propForm);
		}
		else {
			if (g_d.propColorReady) UiColorSelect_Blink(&g_d.propColor, &g_d.app->ui);
			if (g_d.propColorBgReady) UiColorSelect_Blink(&g_d.propColorBg, &g_d.app->ui);
		}
	}
}

static int d_animating(UiApp *app, void *user)
{
	(void)app; (void)user;
	if (g_d.preview && g_d.instReady) return UiDesign_Busy(&g_d.inst);
	if (g_d.editActive && g_d.editMultiline && UiTextArea_Busy(&g_d.propOptionsForm)) return 1;
	if (UiTooltip_Pending(&g_d.toolbarTip)) return 1;
	if (UiPopupManager_IsOpen(&g_d.pm, g_d.toolbarMenu.id)) return 1;
	if (!g_d.preview && g_d.propColorReady &&
	    UiColorSelect_IsOpen(&g_d.pm, &g_d.propColor))
		return 1;
	if (!g_d.preview && g_d.propColorBgReady &&
	    UiColorSelect_IsOpen(&g_d.pm, &g_d.propColorBg))
		return 1;
	if (!g_d.preview && g_d.propRgReady &&
	    (UiPopupManager_IsOpen(&g_d.pm, g_d.propRgSelect.id) ||
	     UiSelect_Busy(&g_d.propRgSelect)))
		return 1;
	return 0;
}

/* ── App 回调 ───────────────────────────────────────────────────────── */

static void d_on_init(UiApp *app, void *user)
{
	(void)user;
	g_d.app = app;
	UiPopupManager_Init(&g_d.pm);
	d_qr_slots_free();
	UiInput_Init(&g_d.propForm, &app->ui, &app->ime);
	UiTextArea_Init(&g_d.propOptionsForm, Ui_Id("dsn.propoptions", 0, 0), 0);
	UiTextArea_AttachIme(&g_d.propOptionsForm, &app->ime);
	g_d.propForm.count = 0;
	g_d.propField = UiInput_Add(&g_d.propForm, Ui_Id("dsn.prop", 0, 0),
		0, 0, NULL, NULL);
	UiColorSelect_Init(&g_d.propColor, Ui_Id("dsn.propcolor", 0, 0),
		0xFF3B82F6u);
	UiColorSelect_AttachIme(&g_d.propColor, &app->ime);
	g_d.propColorReady = 1;
	g_d.propColorNode = -1;
	UiColorSelect_Init(&g_d.propColorBg, Ui_Id("dsn.propcolorbg", 0, 0),
		0xFFFFFFFFu);
	UiColorSelect_AttachIme(&g_d.propColorBg, &app->ime);
	g_d.propColorBgReady = 1;
	g_d.propColorBgNode = -1;
	UiSelect_Init(&g_d.propRgSelect, Ui_Id("dsn.proprg", 0, 0));
	UiSelect_SetFilterable(&g_d.propRgSelect, 0);
	g_d.propRgReady = 1;
	g_d.propRgNode = -1;
	g_d.propRgOwner = -1;
	UiMenu_Init(&g_d.toolbarMenu, Ui_Id("dsn.tool.menu", 0, 0));
	UiTooltip_Init(&g_d.toolbarTip, Ui_Id("dsn.tool.tip", 0, 0));
	UiTooltip_SetDelay(&g_d.toolbarTip, 400);
	g_d.toolbarMenuN = 0;
	UiButtonGroup_Init(&g_d.constraintW, Ui_Id("dsn.cw", 0, 0), UI_BG_SINGLE, 2);
	UiButtonGroup_Init(&g_d.constraintH, Ui_Id("dsn.ch", 0, 0), UI_BG_SINGLE, 2);
	UiButtonGroup_Init(&g_d.constraintAlign, Ui_Id("dsn.ca", 0, 0), UI_BG_SINGLE, 3);
	UiButtonGroup_Init(&g_d.constraintKind, Ui_Id("dsn.ck", 0, 0), UI_BG_SINGLE, 4);
	UiButtonGroup_Init(&g_d.multiAlign, Ui_Id("dsn.ma", 0, 0), UI_BG_SINGLE, 6);
	UiButtonGroup_Init(&g_d.multiDist, Ui_Id("dsn.md", 0, 0), UI_BG_SINGLE, 2);
	UiButtonGroup_SetSelected(&g_d.multiAlign, -1);
	UiButtonGroup_SetSelected(&g_d.multiDist, -1);
	g_d.insertType = -1;
	g_d.zoom = 1.0f;
	d_nav_init();
	g_d.selWin = g_d.selView = g_d.selGroup = -1;
	g_d.selNode = -1;
	g_d.selKind = DSEL_NODE;
	UiDesign_DefaultDoc(&g_d.doc);
	d_reset_selection();
	d_undo_reset();
	d_status("就绪 · 打开或新建 .cui 工程");
	if (g_d.startupFile && g_d.startupFile[0]) {
		if (UiDesign_LoadFile(&g_d.doc, g_d.startupFile)) {
			d_normalize_modes();
			d_reset_selection();
			d_undo_reset();
			d_set_dir(&g_d.lastDir, g_d.startupFile);
			d_status("已打开 .cui 工程");
		} else {
			d_error("打开失败: .cui 解析错误");
		}
	}
}

static void d_on_frame(UiApp *app, void *user)
{
	(void)user;
	UiPopupManager_BeginFrame(&g_d.pm, &app->ui);
	if (g_d.titleDirty != (g_d.doc.dirty != 0)) {
		g_d.titleDirty = g_d.doc.dirty != 0;
		SetWindowTextW(app->hwnd,
			g_d.titleDirty ? L"CUI Designer *" : L"CUI Designer");
	}
	d_preview_update();
	if (g_d.preview && g_d.instReady && !g_d.previewDialogTried &&
	    g_d.selWin >= 0 && g_d.selWin < g_d.doc.hostCount &&
	    g_d.doc.hosts[g_d.selWin].kind == UI_DH_DIALOG) {
		g_d.previewDialogTried = 1;
		UiDesign_DialogOpen(&g_d.inst, Ui_Id("dsn.preview.owner", 0, 0));
	}
	d_draw_all();
	if (g_d.preview && g_d.instReady) {
		UiDesign_DrawOverlay(&g_d.inst);
		if (UiDesign_DialogIsOpen(&g_d.inst)) UiDesign_DialogDraw(&g_d.inst);
	}
	if (!g_d.preview && !g_d.genPreview && g_d.propColorReady) {
		UiColorSelect_DrawPopup(&g_d.propColor, &g_d.pm, &app->ui);
		UiColorSelect_DrawPopup(&g_d.propColorBg, &g_d.pm, &app->ui);
	}
	if (!g_d.preview && !g_d.genPreview && g_d.propRgReady) {
		UiSelect_DrawPopup(&g_d.propRgSelect, &g_d.pm, &app->ui);
		d_rg_combo_commit();
	}
	UiNavBar_DrawPopups(&g_d.nav, &g_d.pm, &app->ui);
	UiMenu_Draw(&g_d.toolbarMenu, &g_d.pm, &app->ui);
	if (g_d.toolbarMenu.changed && g_d.toolbarMenu.item >= 0 &&
	    g_d.toolbarMenu.item < g_d.toolbarMenuN) {
		int idx = g_d.toolbarMenuIdx[g_d.toolbarMenu.item];
		if (idx >= 0 && idx < D_TOOL_ITEM_N)
			d_toolbar_activate(&kToolbarItems[idx]);
	}
	UiTooltip_Draw(&g_d.toolbarTip, &app->ui);
}

static void d_on_after(UiApp *app, void *user)
{
	(void)app; (void)user;
	if (g_d.preview && g_d.instReady) UiDesign_EndFrame(&g_d.inst);
	if (g_d.propColorReady) {
		if (g_d.propColor.committed) {
			g_d.propColor.committed = 0;
			d_undo_push();
		}
		UiColorSelect_EndFrame(&g_d.propColor, UiFrame_Now());
	}
	if (g_d.propColorBgReady) {
		if (g_d.propColorBg.committed) {
			g_d.propColorBg.committed = 0;
			d_undo_push();
		}
		UiColorSelect_EndFrame(&g_d.propColorBg, UiFrame_Now());
	}
}

static void d_on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
	if (!g_d.preview && !g_d.genPreview && g_d.propColorReady &&
	    UiColorSelect_Mouse(&g_d.propColor, &app->ui, msg, x, y)) {
		UiColorSelect_EndFrame(&g_d.propColor, UiFrame_Now());
		UiApp_Invalidate(app);
		return;
	}
	if (!g_d.preview && !g_d.genPreview && g_d.propColorBgReady &&
	    UiColorSelect_Mouse(&g_d.propColorBg, &app->ui, msg, x, y)) {
		UiColorSelect_EndFrame(&g_d.propColorBg, UiFrame_Now());
		UiApp_Invalidate(app);
		return;
	}
	if (msg == UI_APP_MOUSE_DOWN) d_mouse_down(x, y);
	else if (msg == UI_APP_MOUSE_MOVE) d_mouse_move(x, y);
	else if (msg == UI_APP_MOUSE_UP) d_mouse_up();
}

static int d_on_input(UiApp *app, UINT msg, WPARAM wp, LPARAM lp,
	LRESULT *result, void *user)
{
	(void)wp; (void)lp; (void)user;
	if (msg == WM_CLOSE && g_d.doc.dirty) {
		d_after_edit();
		if (!d_confirm_discard()) {
			*result = 0;
			return 1;
		}
	}
	return 0;
}

static int d_on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	float s = app && app->scale > 0.0f ? app->scale : 1.0f;
	int top = (int)(TOP_H * s);
	int navRight = (int)(620.0f * s);
	int closeLeft = (int)((app->w - 48.0f) * s);
	(void)user;
	if (y >= 0 && y < top && x >= navRight && x < closeLeft) {
		*hit = HTCAPTION;
		return 1;
	}
	return 0;
}

static int d_on_key(UiApp *app, UINT vk, void *user)
{
	(void)user;
	if (g_d.genPreview) return d_key(vk);
	if (g_d.preview) {
		if (UiPopupManager_Key(&g_d.pm, &app->ui, vk)) return 1;
		return d_key(vk);
	}
	/* 菜单面板打开时它优先 (模态); 其余情况先让输入框/画布消费按键,
	 * 否则编辑中的方向键、空格、回车会被导航栏截走. */
	if (UiPopupManager_IsOpen(&g_d.pm, g_d.nav.id) &&
	    UiNavBar_Key(&g_d.nav, &g_d.pm, &app->ui, vk))
		return 1;
	if (UiPopupManager_IsOpen(&g_d.pm, g_d.toolbarMenu.id) &&
	    UiMenu_Key(&g_d.toolbarMenu, &g_d.pm, &app->ui, vk))
		return 1;
	if (d_key(vk)) return 1;
	if (UiPopupManager_Key(&g_d.pm, &app->ui, vk)) return 1;
	if (UiNavBar_Key(&g_d.nav, &g_d.pm, &app->ui, vk)) return 1;
	return 0;
}

static int d_on_chr(UiApp *app, wchar_t ch, void *user)
{
	(void)app; (void)user;
	return d_chr(ch);
}

static void d_on_ime(UiApp *app, void *user)
{
	(void)app; (void)user;
	d_ime();
}

static void d_on_timer(UiApp *app, void *user)
{
	(void)app; (void)user;
	d_blink();
}

static void d_on_wheel(UiApp *app, float x, float y, float delta, void *user)
{
	float rx = app->w - RIGHT_W - 10.0f;
	float ry = TOP_H + PALETTE_H + GAP;
	(void)user;
	if (g_d.genPreview) return;
	if (!g_d.preview && g_d.propRgReady &&
	    UiPopupManager_IsOpen(&g_d.pm, g_d.propRgSelect.id) &&
	    Ui_RectContains(g_d.propRgSelect.listRc, x, y)) {
		Ui_OnMouseWheel(&app->ui, x, y, delta);
		return;
	}
	if (!g_d.preview && d_in_canvas(x, y) &&
	    (GetKeyState(VK_CONTROL) & 0x8000)) {
		UiRect body = d_canvas_area();
		float old = g_d.zoom, nz;
		float dx, dy;
		UiDesignView *vw = d_selview();
		if (!vw) return;
		body.y += 34.0f;
		body.h -= 34.0f;
		nz = old * (delta > 0 ? 1.12f : 0.89f);
		if (nz < 0.35f) nz = 0.35f;
		if (nz > 3.0f) nz = 3.0f;
		dx = (x - g_d.viewTx) / old;
		dy = (y - g_d.viewTy) / old;
		g_d.zoom = nz;
		g_d.panX = (x - dx * nz) - (body.x + (body.w - vw->w * nz) * 0.5f);
		g_d.panY = (y - dy * nz) - (body.y + 10.0f);
		UiApp_Invalidate(app);
		return;
	}
	if (g_d.editActive && g_d.editMultiline &&
	    Ui_RectContains(g_d.propOptionsForm.rc, x, y)) {
		Ui_OnMouseWheel(&app->ui, x, y, delta);
		return;
	}
	if (x >= rx && y >= ry) {
		int max = g_d.propContentH - (int)(app->h - TOP_H - PALETTE_H - STATUS_H - GAP * 2 - 48);
		if (max < 0) max = 0;
		g_d.propScroll -= (int)(delta * 54.0f);
		if (g_d.propScroll < 0) g_d.propScroll = 0;
		if (g_d.propScroll > max) g_d.propScroll = max;
		UiApp_Invalidate(app);
		return;
	}
	Ui_OnMouseWheel(&app->ui, x, y, delta);
}

static void d_on_tick(UiApp *app, double dtMs, void *user)
{
	(void)user;
	if (g_d.preview && g_d.instReady && g_d.inst.dialogReady)
		UiDialog_Tick(&g_d.inst.dialog, &app->ui, dtMs);
}

static void d_on_shutdown(UiApp *app, void *user)
{
	(void)app; (void)user;
	d_after_edit();
	if (g_d.propColorReady) {
		UiColorSelect_Shutdown(&g_d.propColor);
		g_d.propColorReady = 0;
	}
	if (g_d.propColorBgReady) {
		UiColorSelect_Shutdown(&g_d.propColorBg);
		g_d.propColorBgReady = 0;
	}
	if (g_d.propRgReady) {
		d_rg_combo_clear();
		UiSelect_Shutdown(&g_d.propRgSelect);
		g_d.propRgReady = 0;
	}
	if (g_d.instReady) {
		UiDesignInstance_Shutdown(&g_d.inst);
		g_d.instReady = 0;
	}
	UiInput_Shutdown(&g_d.propForm);
	UiTextArea_Shutdown(&g_d.propOptionsForm);
	d_preview_restore_tabs();
	d_preview_restore_radios();
	d_qr_slots_free();
	UiDesign_Free(&g_d.doc);
	d_undo_free();
	if (g_d.rects) HeapFree(GetProcessHeap(), 0, g_d.rects);
	g_d.rects = NULL;
	HeapFree(GetProcessHeap(), 0, g_d.lastDir);
	HeapFree(GetProcessHeap(), 0, g_d.startupFile);
	HeapFree(GetProcessHeap(), 0, g_d.genDir);
	HeapFree(GetProcessHeap(), 0, g_d.selSet);
	HeapFree(GetProcessHeap(), 0, g_d.dragList);
	HeapFree(GetProcessHeap(), 0, g_d.treeRows);
	if (g_d.genFiles) UiDesign_GenFilesFree(g_d.genFiles, g_d.genFileN);
	HeapFree(GetProcessHeap(), 0, g_d.genFiles);
}

static WCHAR *d_parse_startup_file(void)
{
	const WCHAR *p;
	int n = 0, cap;
	WCHAR *out;
	p = GetCommandLineW();
	if (!p) return NULL;
	if (*p == L'"') {
		p++;
		while (*p && *p != L'"') p++;
		if (*p) p++;
	} else {
		while (*p && *p != L' ' && *p != L'\t') p++;
	}
	while (*p == L' ' || *p == L'\t') p++;
	if (!*p) return NULL;
	cap = lstrlenW(p) + 1;
	out = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cap * sizeof(*out));
	if (!out) return NULL;
	if (*p == L'"') {
		p++;
		while (*p && *p != L'"') out[n++] = *p++;
	} else {
		while (*p && *p != L' ' && *p != L'\t') out[n++] = *p++;
	}
	out[n] = 0;
	return out;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&g_d, sizeof(g_d));
	g_d.startupFile = d_parse_startup_file();
	ZeroMemory(&cb, sizeof(cb));
	cb.init = d_on_init;
	cb.frame = d_on_frame;
	cb.input = d_on_input;
	cb.after_frame = d_on_after;
	cb.shutdown = d_on_shutdown;
	cb.animating = d_animating;
	cb.tick = d_on_tick;
	cb.mouse = d_on_mouse;
	cb.wheel = d_on_wheel;
	cb.key = d_on_key;
	cb.chr = d_on_chr;
	cb.ime = d_on_ime;
	cb.timer = d_on_timer;
	cb.nchittest = d_on_nchittest;
	return UiApp_Run(L"CUI Designer", DW_W, DH_H, UI_THEME_LIGHT, &cb, &g_d);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif



