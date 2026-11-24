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
	DSEL_NODE
} DSelKind;

enum {
	DPK_TEXT = 0,
	DPK_INT,
	DPK_FLOAT,
	DPK_ENUM,
	DPK_BOOL,
	DPK_READONLY,
	DPK_COLOR
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
	DK_N_ECC, DK_N_BGCOLOR, DK_N_LOGOSRC, DK_N_LOGORATIO, DK_N_QRPAD
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
} DProp;

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
	int selSet[128];
	int selSetN;

	int dragListN;
	int dragListIdx[128];
	float dragListX[128];
	float dragListY[128];

	UiRect treeRowRc[128];
	int treeRowIdx[128];
	int treeRowN;
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

	UiInput propForm;
	int propField;
	int editActive, editKey, editChanged;
	int propDrag, propDragKey;
	float propDragX, propDragVal;
	int propDragIsInt;
	UiColorSelect propColor;   /* 属性面板颜色行 */
	int propColorReady;
	int propColorNode;
	UiColorSelect propColorBg; /* 二维码背景色行 */
	int propColorBgReady;
	int propColorBgNode;
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
	UiButtonGroup layoutGroup, compGroup;
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

	/* 撤销 */
	char *undo[40];
	int undoN, undoPos;

	char status[320];
	char error[320];
	WCHAR lastDir[MAX_PATH];
	WCHAR startupFile[MAX_PATH];

	int genPreview;
	UiDesignGenFile genFiles[64];
	int genFileN;
	WCHAR genDir[MAX_PATH];
	int titleDirty;

	/* 属性/树行 */
	DProp props[48];
	int propN;
	UiRect propRowRc[48];
} Designer;

static Designer g_d;

static void d_prop_apply_text(int key, const char *text);
static float fabsf_local(float v);
static void d_preview_shutdown(void);
static void d_sel_clear(void);
static void d_sel_single(int idx);
static void d_sel_toggle(int idx);
static void d_collect_nodes(const UiDesignDoc *doc, int idx, int *out, int cap,
	int *count);
static int d_validate(char *msg, int cap);

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
	int c = 1;
	if (!s || !s[0]) return 0;
	while (*s) {
		if (*s == ',') c++;
		s++;
	}
	return c;
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

static int d_file_exists(const WCHAR *path)
{
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
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

static void d_after_edit(void)
{
	char b[256];
	if (!g_d.editActive) return;
	Edit_TextUtf8(UiInput_Edit(&g_d.propForm, 0), b, (int)sizeof(b));
	d_prop_apply_text(g_d.editKey, b);
	g_d.editActive = 0;
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
	g_d.selKind = g_d.selView >= 0 ? DSEL_VIEW : DSEL_WINDOW;
	d_sel_clear();
	if (d_view_root() >= 0) {
		g_d.selNode = d_view_root();
		g_d.selSet[0] = g_d.selNode;
		g_d.selSetN = 1;
	}
	g_d.treeDragIdx = -1;
	g_d.marquee = 0;
	g_d.layoutReady = 0;
	d_status(redo ? "重做" : "撤销");
}

/* ── 文件 ───────────────────────────────────────────────────────────── */

static int d_open_dialog(WCHAR *file, int cap)
{
	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_d.app ? g_d.app->hwnd : NULL;
	ofn.lpstrFilter = L"CUI 设计工程 (*.cui)\0*.cui\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = cap;
	ofn.lpstrDefExt = L"cui";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	if (g_d.lastDir[0]) ofn.lpstrInitialDir = g_d.lastDir;
	return GetOpenFileNameW(&ofn) ? 1 : 0;
}

static int d_save_dialog(WCHAR *file, int cap)
{
	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_d.app ? g_d.app->hwnd : NULL;
	ofn.lpstrFilter = L"CUI 设计工程 (*.cui)\0*.cui\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = cap;
	ofn.lpstrDefExt = L"cui";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (g_d.lastDir[0]) ofn.lpstrInitialDir = g_d.lastDir;
	return GetSaveFileNameW(&ofn) ? 1 : 0;
}

static void d_preview_shutdown(void)
{
	if (g_d.instReady) {
		UiDesignInstance_Shutdown(&g_d.inst);
		g_d.instReady = 0;
	}
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
	d_sel_clear();
	if (g_d.selNode >= 0) { g_d.selSet[0] = g_d.selNode; g_d.selSetN = 1; }
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
	WCHAR file[MAX_PATH] = L"";
	d_after_edit();
	if (!d_confirm_discard()) return;
	if (!d_open_dialog(file, MAX_PATH)) return;
	d_preview_shutdown();
	if (!UiDesign_LoadFile(&g_d.doc, file)) {
		d_error("打开失败: .cui 解析错误");
		return;
	}
	d_normalize_modes();
	d_dir_of(file, g_d.lastDir, MAX_PATH);
	d_reset_selection();
	d_undo_reset();
	d_status("打开工程");
}

static void d_save_file(int saveAs)
{
	WCHAR file[MAX_PATH];
	d_after_edit();
	if (!g_d.doc.path[0] || saveAs) {
		file[0] = 0;
		if (g_d.doc.path[0]) d_wide(g_d.doc.path, file, MAX_PATH);
		if (!d_save_dialog(file, MAX_PATH)) return;
	} else {
		d_wide(g_d.doc.path, file, MAX_PATH);
	}
	if (!UiDesign_SaveFile(&g_d.doc, file)) {
		d_error("保存失败");
		return;
	}
	d_dir_of(file, g_d.lastDir, MAX_PATH);
	d_undo_reset();
	d_status("已保存 .cui");
}

static void d_generate_window(void)
{
	WCHAR dir[MAX_PATH] = L".";
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
	if (g_d.doc.path[0]) {
		WCHAR file[MAX_PATH];
		d_wide(g_d.doc.path, file, MAX_PATH);
		d_dir_of(file, dir, MAX_PATH);
	}
	if (UiDesign_GenerateHost(&g_d.doc, g_d.selWin, dir, report,
	    sizeof(report)) < 0)
		d_error("单宿主生成失败 (检查输出目录)");
	else
		d_status(report);
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
	WCHAR dir[MAX_PATH] = L".";
	char report[256], err[256];
	d_after_edit();
	if (!d_validate(err, sizeof(err))) {
		d_error(err);
		return;
	}
	if (g_d.doc.path[0]) {
		WCHAR file[MAX_PATH];
		d_wide(g_d.doc.path, file, MAX_PATH);
		d_dir_of(file, dir, MAX_PATH);
	}
	g_d.genFileN = UiDesign_PreviewAll(&g_d.doc, dir, g_d.genFiles, 64,
		report, sizeof(report));
	if (g_d.genFileN < 0) {
		d_error("生成预览失败 (检查分组输出路径)");
		return;
	}
	lstrcpynW(g_d.genDir, dir, MAX_PATH);
	g_d.genPreview = 1;
	d_status(report);
	UiApp_Invalidate(g_d.app);
}

/* ── 属性模型 ───────────────────────────────────────────────────────── */

static UiDesignNode *d_selnode(void)
{
	return g_d.selNode >= 0 ? UiDesign_Node(&g_d.doc, g_d.selNode) : NULL;
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

static void d_drag_begin(int primary)
{
	int i;
	g_d.dragListN = 0;
	for (i = 0; i < g_d.selSetN; i++) {
		int idx = g_d.selSet[i];
		UiDesignNode *n = UiDesign_Node(&g_d.doc, idx);
		if (!n || n->parent < 0 || !d_parent_free(n->parent)) continue;
		if (g_d.dragListN >= (int)(sizeof(g_d.dragListIdx) / sizeof(g_d.dragListIdx[0])))
			break;
		g_d.dragListIdx[g_d.dragListN] = idx;
		g_d.dragListX[g_d.dragListN] = n->x;
		g_d.dragListY[g_d.dragListN] = n->y;
		g_d.dragListN++;
	}
	if (g_d.dragListN == 0) {
		UiDesignNode *n = UiDesign_Node(&g_d.doc, primary);
		if (n && n->parent >= 0 && d_parent_free(n->parent)) {
			g_d.dragListIdx[0] = primary;
			g_d.dragListX[0] = n->x;
			g_d.dragListY[0] = n->y;
			g_d.dragListN = 1;
		}
	}
}

static void d_sel_clear(void)
{
	g_d.selSetN = 0;
}

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
			}
		}
		idx = p;
	}
}

static void d_sel_single(int idx)
{
	d_sel_clear();
	if (idx >= 0) {
		g_d.selSet[0] = idx;
		g_d.selSetN = 1;
		g_d.selNode = idx;
		g_d.selKind = DSEL_NODE;
		d_tabs_reveal(idx);
	}
}

static void d_sel_toggle(int idx)
{
	int i;
	if (!d_sel_has(idx)) {
		if (g_d.selSetN < (int)(sizeof(g_d.selSet) / sizeof(g_d.selSet[0])))
			g_d.selSet[g_d.selSetN++] = idx;
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
	{
		UiDesignNode *n = d_selnode();
		int freeParent, gridParent;
		if (!n) return;
		freeParent = n->parent >= 0 &&
			(g_d.doc.nodes[n->parent].type == UI_DN_ABSOLUTE ||
			 g_d.doc.nodes[n->parent].type == UI_DN_PANEL);
		gridParent = n->parent >= 0 &&
			g_d.doc.nodes[n->parent].type == UI_DN_GRID;
		d_prop_add(DK_N_ID, DPK_TEXT, "ID", n->id);
		d_prop_add(DK_N_NAME, DPK_TEXT, "名称", n->name);
		d_prop_add(DK_N_TYPE, DPK_READONLY, "类型", UiDesign_TypeName(n->type));
		g_d.propSection = 2;
		if (n->parent >= 0 &&
		    g_d.doc.nodes[n->parent].type != UI_DN_TABS) {
			d_fmt(b, sizeof(b), n->w); d_prop_add(DK_N_W, DPK_FLOAT, "宽", b);
			d_fmt(b, sizeof(b), n->h); d_prop_add(DK_N_H, DPK_FLOAT, "高", b);
		}
		if (freeParent) {
			d_fmt(b, sizeof(b), n->x); d_prop_add(DK_N_X, DPK_FLOAT, "X", b);
			d_fmt(b, sizeof(b), n->y); d_prop_add(DK_N_Y, DPK_FLOAT, "Y", b);
		}
		g_d.propSection = 1;
		if (UiDesign_IsContainer(n->type)) {
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
			d_prop_add(DK_N_OPTIONS, DPK_TEXT, "Options", n->options);
			snprintf(b, sizeof(b), "%d", n->selected);
			d_prop_add(DK_N_SELECTED, DPK_ENUM, "Selected", b);
			d_prop_add(DK_N_HINT, DPK_TEXT, "Hint", n->hint);
			d_prop_add(DK_N_ENABLED, DPK_BOOL, "Enabled", n->enabled ? "1" : "0");
			break;
		case UI_DN_IMAGE:
			d_prop_add(DK_N_TEXT, DPK_TEXT, "Text", n->text);
			break;
		case UI_DN_TABS:
			snprintf(b, sizeof(b), "%d", n->selected);
			d_prop_add(DK_N_SELECTED, DPK_ENUM, "当前页", b);
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
	} else if (g_d.selKind == DSEL_NODE && n) {
		if (key == DK_N_WMODE) n->wMode = (n->wMode + 1) % 4;
		else if (key == DK_N_HMODE) n->hMode = (n->hMode + 1) % 4;
		else if (key == DK_N_ALIGN) n->align = (n->align + 1) % 3;
		else if (key == DK_N_TEXTALIGN) n->textAlign = (n->textAlign + 1) % 3;
		else if (key == DK_N_TEXTVALIGN) n->textVAlign = (n->textVAlign + 1) % 3;
		else if (key == DK_N_VARIANT) n->variant = (n->variant + 1) % 3;
		else if (key == DK_N_CHECKED) n->checked = !n->checked;
		else if (key == DK_N_ENABLED) n->enabled = !n->enabled;
		else if (key == DK_N_SELECTED) {
			int cnt = n->childCount;
			if (n->type == UI_DN_SELECT) cnt = d_option_count(n->options);
			if (cnt > 0) n->selected = (n->selected + 1) % cnt;
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
		if (key == DK_W_ID) UiDesign_SetText(w->id, UI_DN_ID_MAX, text);
		else if (key == DK_W_NAME) UiDesign_SetText(w->name, UI_DN_NAME_MAX, text);
		else if (key == DK_W_TITLE) UiDesign_SetText(w->title, 128, text);
		else if (key == DK_W_W) w->w = (float)d_atof(text);
		else if (key == DK_W_H) w->h = (float)d_atof(text);
		else if (key == DK_W_MINW) w->minW = (float)d_atof(text);
		else if (key == DK_W_MINH) w->minH = (float)d_atof(text);
		d_autosize_window(w);
	} else if (g_d.selKind == DSEL_VIEW && d_selview()) {
		UiDesignView *v = d_selview();
		if (key == DK_V_ID) UiDesign_SetText(v->id, UI_DN_ID_MAX, text);
		else if (key == DK_V_NAME) UiDesign_SetText(v->name, UI_DN_NAME_MAX, text);
		else if (key == DK_V_W) v->w = (float)d_atof(text);
		else if (key == DK_V_H) v->h = (float)d_atof(text);
	} else if (g_d.selKind == DSEL_GROUP && gr) {
		if (key == DK_G_ID) UiDesign_SetText(gr->id, UI_DN_ID_MAX, text);
		else if (key == DK_G_NAME) UiDesign_SetText(gr->name, UI_DN_NAME_MAX, text);
		else if (key == DK_G_OUTC) UiDesign_SetText(gr->outputC, UI_DN_PATH_MAX, text);
		else if (key == DK_G_OUTH) UiDesign_SetText(gr->outputH, UI_DN_PATH_MAX, text);
		else if (key == DK_G_SYM) UiDesign_SetText(gr->symbolPrefix, UI_DN_SYM_MAX, text);
	} else if (g_d.selKind == DSEL_NODE && n) {
		if (key == DK_N_ID) UiDesign_SetText(n->id, UI_DN_ID_MAX, text);
		else if (key == DK_N_NAME) UiDesign_SetText(n->name, UI_DN_NAME_MAX, text);
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
		else if (key == DK_N_TEXT) UiDesign_NodeSetText(n, text);
		else if (key == DK_N_HINT) UiDesign_NodeSetHint(n, text);
		else if (key == DK_N_OPTIONS) UiDesign_NodeSetOptions(n, text);
		else if (key == DK_N_VALUE) n->value = (float)d_atof(text);
		else if (key == DK_N_CLICK) UiDesign_SetText(n->onClick, UI_DN_ID_MAX, text);
		else if (key == DK_N_CHANGE) UiDesign_SetText(n->onChange, UI_DN_ID_MAX, text);
		else if (key == DK_N_GRIDCOLS) n->gridCols = d_atoi(text) > 0 ? d_atoi(text) : 1;
		else if (key == DK_N_GRIDROWS) n->gridRows = d_atoi(text) > 0 ? d_atoi(text) : 0;
		else if (key == DK_N_GRIDROW) n->gridRow = d_atoi(text);
		else if (key == DK_N_GRIDCOL) n->gridCol = d_atoi(text);
		else if (key == DK_N_GRIDROWGAP) n->gridRowGap = (float)d_atof(text);
		else if (key == DK_N_GRIDCOLGAP) n->gridColGap = (float)d_atof(text);
		else if (key == DK_N_GRIDCOLTRACKS) {
			int tn = d_track_count(text);
			UiDesign_SetText(n->gridColTracks, UI_DN_GRID_TRACKS_MAX, text);
			if (tn > n->gridCols) n->gridCols = tn;
			d_status("列宽: 120 固定 / 1fr 弹性 / auto 随子项, 空=均分");
		}
		else if (key == DK_N_COLSPAN) n->colSpan = d_atoi(text) > 0 ? d_atoi(text) : 1;
		else if (key == DK_N_ROWSPAN) n->rowSpan = d_atoi(text) > 0 ? d_atoi(text) : 1;
		else if (key == DK_N_COLOR) n->color = d_parse_color(text, n->color);
		else if (key == DK_N_BGCOLOR) n->bgColor = d_parse_color(text, n->bgColor);
		else if (key == DK_N_LOGOSRC) {
			UiDesign_SetText(n->logoSrc, UI_DN_PATH_MAX, text);
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
	g_d.propForm.focus = 0;
	g_d.propForm.blinkOn = 1;
	Edit_SetTextUtf8(UiInput_Edit(&g_d.propForm, 0), p->value);
	if (g_d.app)
		UiIme_SetFocus(&g_d.app->ime, UiInput_Edit(&g_d.propForm, 0));
	Ui_SetFocusId(&g_d.app->ui, g_d.propForm.fields[0].id);
}

static void d_prop_apply_edit(void)
{
	char b[256];
	if (!g_d.editActive) return;
	Edit_TextUtf8(UiInput_Edit(&g_d.propForm, 0), b, (int)sizeof(b));
	d_prop_apply_text(g_d.editKey, b);
}

/* ── 操作 ───────────────────────────────────────────────────────────── */

static int d_unique_node_id(int win, const char *base, char *out, int cap)
{
	int i;
	for (i = 1; i < 10000; i++) {
		snprintf(out, (size_t)cap, "%.48s%d", base, i);
		if (UiDesign_FindNode(&g_d.doc, win, out) < 0) return 1;
	}
	UiDesign_SetText(out, cap, base);
	return 1;
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

static void d_tabs_add_pages(int win, int tabsIdx)
{
	int i;
	for (i = 0; i < 2; i++) {
		char id[UI_DN_ID_MAX], name[UI_DN_NAME_MAX], base[24];
		UiDesignNode *pg;
		int ci;
		snprintf(base, sizeof(base), "page%d", i + 1);
		d_unique_node_id(win, base, id, sizeof(id));
		snprintf(name, sizeof(name), "页 %d", i + 1);
		ci = UiDesign_AddNode(&g_d.doc, win, UI_DN_COLUMN, tabsIdx, id, name);
		pg = UiDesign_Node(&g_d.doc, ci);
		if (!pg) continue;
		pg->wMode = UI_DN_SIZE_FILL;
		pg->hMode = UI_DN_SIZE_FILL;
		pg->padding[0] = pg->padding[1] = pg->padding[2] = pg->padding[3] = 12.0f;
		pg->gap = 8.0f;
	}
}

static void d_add_node(int type)
{
	int parent = d_container_parent();
	int view = g_d.selView;
	int idx;
	char id[UI_DN_ID_MAX];
	char name[UI_DN_NAME_MAX];
	UiDesignNode *n;
	if (view < 0 || parent < 0) return;
	d_after_edit();
	d_unique_node_id(view, UiDesign_TypeName(type), id, sizeof(id));
	UiDesign_SetText(name, sizeof(name), UiDesign_TypeName(type));
	idx = UiDesign_AddNode(&g_d.doc, view, type, parent, id, name);
	n = UiDesign_Node(&g_d.doc, idx);
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
		/* 线性布局里按交叉轴铺满, 与普通 UI 的预期一致. */
		if (parent >= 0) {
			int pt = g_d.doc.nodes[parent].type;
			if (pt == UI_DN_GRID) {
				n->wMode = UI_DN_SIZE_FILL;
				n->hMode = UI_DN_SIZE_FILL;
			} else if (pt == UI_DN_COLUMN) {
				n->wMode = UI_DN_SIZE_FILL;
			} else if (pt == UI_DN_ROW) {
				n->hMode = UI_DN_SIZE_FILL;
			} else if (pt == UI_DN_PANEL &&
			    g_d.doc.nodes[parent].parent >= 0 &&
			    g_d.doc.nodes[g_d.doc.nodes[parent].parent].type == UI_DN_TABS) {
				/* 旧文档的 Tabs 页仍是自由 Panel, 同样按页宽铺满. */
				n->wMode = UI_DN_SIZE_FILL;
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
	}
	if (type == UI_DN_TABS) {
		n->h = 220.0f;
		n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 10.0f;
		n->gap = 0.0f;
		n->selected = 0;
		d_tabs_add_pages(view, idx);
	}
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
	char id[UI_DN_ID_MAX], name[UI_DN_NAME_MAX];
	char vid[UI_DN_ID_MAX], vname[UI_DN_NAME_MAX];
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
	if (root >= 0) { g_d.selSet[0] = root; g_d.selSetN = 1; }
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status(kind == UI_DH_DIALOG ? "已添加虚拟 Dialog" : "已添加窗口");
}

static void d_add_group(void)
{
	int i;
	char id[UI_DN_ID_MAX], name[UI_DN_NAME_MAX];
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
	int i, parent = -1;
	if (g_d.preview) return;
	d_after_edit();
	if (g_d.selSetN == 0 && g_d.selNode >= 0) d_sel_single(g_d.selNode);
	for (i = 0; i < g_d.selSetN; i++) {
		int idx = g_d.selSet[i];
		UiDesignNode *n = UiDesign_Node(&g_d.doc, idx);
		if (!n || n->parent < 0) continue;
		if (idx == g_d.selNode) parent = n->parent;
		UiDesign_NodeRemove(&g_d.doc, idx);
	}
	d_sel_clear();
	if (parent >= 0 && parent < g_d.doc.nodeCount) d_sel_single(parent);
	g_d.selKind = DSEL_NODE;
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已删除组件");
}

static void d_del_window(void)
{
	int wi = g_d.selWin, i;
	if (g_d.preview || wi < 0 || wi >= g_d.doc.hostCount) return;
	d_preview_shutdown();
	for (i = wi + 1; i < g_d.doc.hostCount; i++)
		g_d.doc.hosts[i - 1] = g_d.doc.hosts[i];
	g_d.doc.hostCount--;
	g_d.selWin = g_d.doc.hostCount > 0
		? (wi >= g_d.doc.hostCount ? g_d.doc.hostCount - 1 : wi) : -1;
	if (g_d.selWin >= 0) g_d.selView = g_d.doc.hosts[g_d.selWin].view;
	g_d.selKind = g_d.selView >= 0 ? DSEL_VIEW : DSEL_WINDOW;
	g_d.selNode = d_view_root();
	d_sel_clear();
	if (g_d.selNode >= 0) { g_d.selSet[0] = g_d.selNode; g_d.selSetN = 1; }
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
		if (g_d.selNode >= 0) {
			g_d.selSet[0] = g_d.selNode;
			g_d.selSetN = 1;
		}
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
	char id[UI_DN_ID_MAX], name[UI_DN_NAME_MAX];
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
	g_d.selSet[0] = root;
	g_d.selSetN = 1;
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已添加视图 · 可被窗口/Dialog/Panel 复用");
}

static void d_add_panel_host(void)
{
	int vi = g_d.selView, hi;
	char id[UI_DN_ID_MAX], name[UI_DN_NAME_MAX];
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

static void d_del_selected(void)
{
	if (g_d.selKind == DSEL_WINDOW) d_del_window();
	else if (g_d.selKind == DSEL_VIEW) d_del_view();
	else if (g_d.selKind == DSEL_GROUP) d_del_group();
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
				char base[UI_DN_ID_MAX], id[UI_DN_ID_MAX];
				UiDesign_SetText(base, sizeof(base), n->id);
				d_unique_node_id(win, base, id, sizeof(id));
				UiDesign_SetText(n->id, UI_DN_ID_MAX, id);
			}
			j = -1;
		}
	}
}

static void d_dup_node(void)
{
	int i, idx, dup, count = 0, last = -1;
	int sel[128];
	if (g_d.selKind != DSEL_NODE) return;
	d_after_edit();
	if (g_d.selSetN <= 1) {
		if (!d_selnode() || d_selnode()->parent < 0) return;
		dup = UiDesign_NodeDuplicate(&g_d.doc, g_d.selNode);
		if (dup < 0) return;
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
	for (i = 0; i < g_d.selSetN && i < 128; i++) sel[i] = g_d.selSet[i];
	{
		int n = g_d.selSetN < 128 ? g_d.selSetN : 128;
		for (i = 0; i < n; i++) {
			UiDesignNode *sn;
			idx = sel[i];
			if (idx < 0 || idx >= g_d.doc.nodeCount) continue;
			sn = &g_d.doc.nodes[idx];
			if (sn->parent < 0 || d_has_selected_ancestor(idx)) continue;
			dup = UiDesign_NodeDuplicate(&g_d.doc, idx);
			if (dup < 0) continue;
			sel[count++] = dup;
			last = dup;
		}
	}
	if (count <= 0) return;
	d_unique_all_ids(g_d.selView);
	d_sel_clear();
	for (i = 0; i < count && g_d.selSetN < 128; i++)
		g_d.selSet[g_d.selSetN++] = sel[i];
	g_d.selNode = last;
	g_d.selKind = DSEL_NODE;
	g_d.doc.dirty = 1;
	g_d.layoutReady = 0;
	d_undo_push();
	d_status("已复制多个组件");
}

static void d_move_node(int dir)
{
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
	if (g_d.selNode >= 0) { g_d.selSet[0] = g_d.selNode; g_d.selSetN = 1; }
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

static int d_tabs_item_at(int idx, float x, float y)
{
	UiDesignNode *n = &g_d.doc.nodes[idx];
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiRect r = g_d.rects[idx], inner;
	float hh, cursor, gap, padX;
	int i, cnt = n->childCount;
	if (cnt <= 0) return -1;
	d_inset(r, n->padding, &inner);
	hh = d_tabs_header_h(n->variant);
	if (hh > inner.h) hh = inner.h;
	if (y < inner.y || y > inner.y + hh) return -1;
	gap = n->variant == 1 ? 2.0f : 4.0f;
	padX = n->variant == 1 ? 16.0f : 14.0f;
	cursor = inner.x + (n->variant == 1 ? 3.0f : 0.0f);
	if (n->variant == 1)
		inner.w -= 6.0f;
	for (i = 0; i < cnt; i++) {
		float w;
		if (n->variant == 1)
			w = (inner.w - gap * (cnt - 1)) / cnt;
		else {
			w = Gfx_TextWidth(g, t->fLabel,
				g_d.doc.nodes[n->children[i]].name) + padX * 2.0f;
			if (w < 44.0f) w = 44.0f;
		}
		if (x >= cursor && x < cursor + w) return i;
		cursor += w + gap;
	}
	return -1;
}

static void d_tabs_draw_header(int idx)
{
	UiDesignNode *n = &g_d.doc.nodes[idx];
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiRect r = g_d.rects[idx], inner;
	float hh, cursor, gap, padX;
	int i, cnt = n->childCount;
	if (cnt <= 0) return;
	d_inset(r, n->padding, &inner);
	hh = d_tabs_header_h(n->variant);
	if (hh > inner.h) hh = inner.h;
	if (hh <= 0.0f) return;
	gap = n->variant == 1 ? 2.0f : 4.0f;
	padX = n->variant == 1 ? 16.0f : 14.0f;
	if (n->variant == 1) {
		Gfx_FillRoundRect(g, Ui_Rect(inner.x, inner.y, inner.w, hh), 8.0f,
			t->neutral.soft);
		cursor = inner.x + 3.0f;
		inner.w -= 6.0f;
	} else {
		cursor = inner.x;
		Gfx_Line(g, inner.x, inner.y + hh - 1, inner.x + inner.w,
			inner.y + hh - 1, t->border, 1.0f);
	}
	for (i = 0; i < cnt; i++) {
		const char *label = g_d.doc.nodes[n->children[i]].name;
		float w;
		UiRect item;
		int sel = i == n->selected;
		if (n->variant == 1)
			w = (inner.w - gap * (cnt - 1)) / cnt;
		else {
			w = Gfx_TextWidth(g, t->fLabel, label) + padX * 2.0f;
			if (w < 44.0f) w = 44.0f;
		}
		item = Ui_Rect(cursor, inner.y, w, hh);
		if (sel && n->variant == 2) {
			Gfx_FillRoundRect(g, Ui_Rect(item.x, item.y, item.w, item.h), 6.0f,
				t->card);
			Gfx_StrokeRoundRect(g, Ui_Rect(item.x, item.y, item.w, item.h),
				6.0f, t->border, 1.0f);
		} else if (sel && n->variant == 1) {
			Gfx_FillRoundRect(g, Ui_Rect(item.x, item.y, item.w, item.h),
				6.0f, t->primary.soft);
		}
		Gfx_Text(g, t->fLabel, label, item,
			sel ? t->primary.base : t->text2,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (sel && n->variant == 0) {
			Gfx_FillRect(g, Ui_Rect(item.x + 8.0f, item.y + item.h - 2.0f,
				item.w - 16.0f, 2.0f), t->primary.base);
		}
		cursor += w + gap;
	}
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

static void d_draw_static_node(int idx)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiDesignNode *n = &g_d.doc.nodes[idx];
	UiRect r = g_d.rects[idx];
	int i;
	if (!n->visible || r.w <= 0 || r.h <= 0) return;
	if (n->type == UI_DN_TABS) {
		UiRect inner;
		float hh;
		d_inset(r, n->padding, &inner);
		hh = d_tabs_header_h(n->variant);
		if (hh > inner.h) hh = inner.h;
		if (n->variant == 2 && n->childCount > 0) {
			Gfx_StrokeRoundRectCornersDashed(g, Ui_Rect(inner.x,
				inner.y + hh, inner.w, inner.h - hh), Gfx_Corners(0, 0, 8, 8),
				Ui_ColorAlpha(t->text3, 75), 1.0f);
		}
		d_tabs_draw_header(idx);
		if (n->childCount > 0) {
			int sel = n->selected;
			if (sel < 0 || sel >= n->childCount) sel = 0;
			d_draw_static_node(n->children[sel]);
		} else {
			Gfx_Text(g, t->fSmall, "标签页: 无子页",
				Ui_Rect(inner.x, inner.y + hh, inner.w, inner.h - hh),
				t->text4, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		}
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
	float xs[UI_DN_GRID_MAX_COLS], ws[UI_DN_GRID_MAX_COLS];
	int cols = UiDesign_GridColumns(&g_d.doc, n, inner, xs, ws,
		UI_DN_GRID_MAX_COLS);
	int rows = n->gridRows > 0 ? n->gridRows
		: (n->childCount + cols - 1) / cols;
	float rh;
	int i;
	if (cols < 1) cols = 1;
	if (rows < 1) rows = 1;
	rh = (inner.h - n->gridRowGap * (rows - 1)) / rows;
	for (i = 1; i < cols; i++)
		Gfx_Line(g, xs[i] - n->gridColGap * 0.5f, inner.y,
			xs[i] - n->gridColGap * 0.5f, inner.y + inner.h, c, 1.0f);
	for (i = 1; i < rows; i++)
		Gfx_Line(g, inner.x, inner.y + i * (rh + n->gridRowGap),
			inner.x + inner.w, inner.y + i * (rh + n->gridRowGap), c, 1.0f);
}

static void d_draw_layout_guides(void)
{
	UiTheme *t = &g_d.app->theme;
	Gfx *g = g_d.app->gfx;
	UiDesignNode *n = d_selnode();
	UiRect nr, inner;
	char b[160];
	if (!n || !g_d.layoutReady) return;
	nr = g_d.rects[g_d.selNode];
	if (n->parent >= 0 && UiDesign_IsContainer(g_d.doc.nodes[n->parent].type)) {
		UiDesignNode *p = &g_d.doc.nodes[n->parent];
		UiRect pr = g_d.rects[n->parent];
		UiRect pi;
		d_inset(pr, p->padding, &pi);
		Gfx_StrokeRoundRectCornersDashed(g, pi, Gfx_Corners(0, 0, 0, 0),
			Ui_ColorAlpha(t->primary.base, 170), 1.0f);
		{
			int i;
			for (i = 0; i < p->childCount; i++) {
				UiRect cr = g_d.rects[p->children[i]];
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
	if (UiDesign_IsContainer(n->type)) {
		d_inset(nr, n->padding, &inner);
		Gfx_StrokeRoundRectCornersDashed(g, inner, Gfx_Corners(0, 0, 0, 0),
			Ui_ColorAlpha(t->primary.base, 200), 1.0f);
		if (n->type == UI_DN_GRID) {
			int cols = n->gridCols > 0 ? n->gridCols : 1;
			int rows = n->gridRows > 0 ? n->gridRows
				: (n->childCount + cols - 1) / cols;
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

static void d_draw_toolbar(void)
{
	UiTheme *t = &g_d.app->theme;
	UiContext *ui = &g_d.app->ui;
	Gfx *g = g_d.app->gfx;
	UiRect bar = Ui_Rect(0, TOP_H, g_d.app->w, PALETTE_H);
	static const char *const layoutLabels[6] = {
		"选择", "自由", "列", "行", "Grid", "面板"
	};
	static const char *const compLabels[15] = {
		"标签", "按钮", "输入", "复选", "开关", "滑块", "进度",
		"下拉", "图片", "分隔", "标签页", "多行", "日期", "颜色", "二维码"
	};
	static const int layoutTypes[6] = {
		-1, UI_DN_ABSOLUTE, UI_DN_COLUMN, UI_DN_ROW, UI_DN_GRID, UI_DN_PANEL
	};
	static const int compTypes[15] = {
		UI_DN_LABEL, UI_DN_BUTTON, UI_DN_INPUT, UI_DN_CHECKBOX, UI_DN_SWITCH,
		UI_DN_SLIDER, UI_DN_PROGRESS, UI_DN_SELECT, UI_DN_IMAGE, UI_DN_SEPARATOR,
		UI_DN_TABS, UI_DN_TEXTAREA, UI_DN_DATEPICKER, UI_DN_COLORSEL, UI_DN_QRCODE
	};
	float x = 12.0f;
	int clicked;

	Gfx_FillRect(g, bar, Ui_ColorMix(t->page, t->frame, 0.45f));
	Gfx_Line(g, 0, TOP_H + PALETTE_H - 1, g_d.app->w, TOP_H + PALETTE_H - 1, t->border, 1.0f);
	clicked = UiButtonGroup_Draw(ui, &g_d.layoutGroup,
		Ui_Rect(x, TOP_H + 6, 330, 30), layoutLabels, NULL, NULL, UI_BTN_PRIMARY);
	if (clicked >= 0) {
		UiButtonGroup_SetSelected(&g_d.compGroup, -1);
		g_d.insertType = layoutTypes[clicked];
		d_status(g_d.insertType < 0 ? "选择工具" : "布局工具: 在画布中点击插入");
	}
	x += 342;
	clicked = UiButtonGroup_Draw(ui, &g_d.compGroup,
		Ui_Rect(x, TOP_H + 6, 742, 30), compLabels, NULL, NULL, UI_BTN_PRIMARY);
	if (clicked >= 0) {
		UiButtonGroup_SetSelected(&g_d.layoutGroup, -1);
		g_d.insertType = compTypes[clicked];
		d_status("组件工具: 在画布中点击插入 (Esc 退出)");
	}
	{
		float bx = g_d.app->w - 360.0f;
		UiButtonStyle bs = UiButtonStyle_Make(UI_BTYPE_DEFAULT, UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
		UiButtonStyle bd = UiButtonStyle_Make(UI_BTYPE_ERROR, UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
		UiIcon icDel = d_icon(g_d.app, 0xE74D);
		UiIcon icDup = d_icon(g_d.app, 0xE8C8);
		UiIcon icUp = d_icon(g_d.app, 0xE74A);
		UiIcon icDown = d_icon(g_d.app, 0xE74B);
		UiIcon icView = d_icon(g_d.app, 0xE768);
		if (Ui_ButtonExIcon(ui, Ui_Id("dsn.act.del", 0, 0), Ui_Rect(bx, TOP_H + 6, 64, 30),
			&icDel, "删除", &bd, 1)) d_del_selected();
		if (Ui_ButtonExIcon(ui, Ui_Id("dsn.act.dup", 0, 0), Ui_Rect(bx + 68, TOP_H + 6, 64, 30),
			&icDup, "复制", &bs, g_d.selKind == DSEL_NODE)) d_dup_node();
		if (Ui_ButtonExIcon(ui, Ui_Id("dsn.act.up", 0, 0), Ui_Rect(bx + 136, TOP_H + 6, 56, 30),
			&icUp, "上移", &bs, g_d.selKind == DSEL_NODE)) d_move_node(0);
		if (Ui_ButtonExIcon(ui, Ui_Id("dsn.act.down", 0, 0), Ui_Rect(bx + 196, TOP_H + 6, 56, 30),
			&icDown, "下移", &bs, g_d.selKind == DSEL_NODE)) d_move_node(1);
		if (Ui_ButtonExIcon(ui, Ui_Id("dsn.act.preview", 0, 0), Ui_Rect(bx + 260, TOP_H + 6, 72, 30),
			&icView, "预览", &bs, 1)) {
			if (!g_d.preview && (g_d.selWin < 0 ||
			    g_d.selWin >= g_d.doc.hostCount ||
			    g_d.doc.hosts[g_d.selWin].view < 0)) {
				d_error("预览需要一个挂载了视图的宿主");
			} else {
				g_d.preview = !g_d.preview;
				g_d.instReady = 0;
				d_status(g_d.preview ? "预览模式" : "设计模式");
			}
		}
	}
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
	nd = &g_d.doc.nodes[idx];
	rc = Ui_Rect(panel.x + 8 + depth * 14.0f, *y,
		panel.w - 16 - depth * 14.0f, ROW_H);
	if (g_d.treeRowN < (int)(sizeof(g_d.treeRowRc) / sizeof(g_d.treeRowRc[0]))) {
		g_d.treeRowRc[g_d.treeRowN] = rc;
		g_d.treeRowIdx[g_d.treeRowN] = idx;
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

static void d_group_paths(int gi, char *outC, char *outH, int cap)
{
	const UiDesignGroup *g = &g_d.doc.groups[gi];
	if (g->outputC[0]) UiDesign_SetText(outC, cap, g->outputC);
	else snprintf(outC, (size_t)cap, "%s.ui.c", g->id);
	if (g->outputH[0]) UiDesign_SetText(outH, cap, g->outputH);
	else snprintf(outH, (size_t)cap, "%s.ui.h", g->id);
}

static int d_validate(char *msg, int cap)
{
	int i, j, k, n;
	int list[256];
	for (i = 0; i < g_d.doc.groupCount; i++) {
		char ci[UI_DN_PATH_MAX], hi[UI_DN_PATH_MAX];
		for (j = i + 1; j < g_d.doc.groupCount; j++) {
			if (strcmp(g_d.doc.groups[i].id, g_d.doc.groups[j].id) == 0) {
				snprintf(msg, (size_t)cap, "生成组 ID 重复: %s", g_d.doc.groups[i].id);
				return 0;
			}
			if (strcmp(g_d.doc.groups[i].symbolPrefix, g_d.doc.groups[j].symbolPrefix) == 0) {
				snprintf(msg, (size_t)cap, "生成组符号前缀重复: %s", g_d.doc.groups[i].symbolPrefix);
				return 0;
			}
		}
		d_group_paths(i, ci, hi, UI_DN_PATH_MAX);
		if (strcmp(ci, hi) == 0) {
			snprintf(msg, (size_t)cap, "输出 .c/.h 路径相同: %s", ci);
			return 0;
		}
		for (j = i + 1; j < g_d.doc.groupCount; j++) {
			char cj[UI_DN_PATH_MAX], hj[UI_DN_PATH_MAX];
			d_group_paths(j, cj, hj, UI_DN_PATH_MAX);
			if (strcmp(ci, cj) == 0 || strcmp(ci, hj) == 0 ||
			    strcmp(hi, cj) == 0 || strcmp(hi, hj) == 0) {
				snprintf(msg, (size_t)cap, "生成组输出路径冲突: %s", ci);
				return 0;
			}
		}
	}
	for (i = 0; i < g_d.doc.hostCount; i++) {
		for (j = i + 1; j < g_d.doc.hostCount; j++)
			if (strcmp(g_d.doc.hosts[i].id, g_d.doc.hosts[j].id) == 0) {
				snprintf(msg, (size_t)cap, "窗口 ID 重复: %s", g_d.doc.hosts[i].id);
				return 0;
			}
		if (g_d.doc.hosts[i].view < 0 ||
		    g_d.doc.hosts[i].view >= g_d.doc.viewCount) {
			snprintf(msg, (size_t)cap, "宿主未挂载有效视图: %s", g_d.doc.hosts[i].id);
			return 0;
		}
		if (g_d.doc.views[g_d.doc.hosts[i].view].root < 0) {
			snprintf(msg, (size_t)cap, "视图根节点缺失: %s",
				g_d.doc.views[g_d.doc.hosts[i].view].id);
			return 0;
		}
		if (g_d.doc.hosts[i].group >= 0 &&
		    g_d.doc.views[g_d.doc.hosts[i].view].group != g_d.doc.hosts[i].group) {
			snprintf(msg, (size_t)cap,
				"宿主与视图必须属于同一生成组: %s", g_d.doc.hosts[i].id);
			return 0;
		}
		if (g_d.doc.hosts[i].group < 0) {
			/* 未分组窗口单独输出, 检查与生成组/其他窗口的路径冲突. */
			char sym[UI_DN_SYM_MAX], wc[UI_DN_PATH_MAX], wh[UI_DN_PATH_MAX];
			UiDesign_Symbol(g_d.doc.hosts[i].id, sym, sizeof(sym));
			snprintf(wc, sizeof(wc), "%s.ui.c", sym);
			snprintf(wh, sizeof(wh), "%s.ui.h", sym);
			for (j = 0; j < g_d.doc.groupCount; j++) {
				char cj[UI_DN_PATH_MAX], hj[UI_DN_PATH_MAX];
				d_group_paths(j, cj, hj, UI_DN_PATH_MAX);
				if (strcmp(wc, cj) == 0 || strcmp(wc, hj) == 0 ||
				    strcmp(wh, cj) == 0 || strcmp(wh, hj) == 0) {
					snprintf(msg, (size_t)cap,
						"未分组窗口输出与生成组冲突: %s", wc);
					return 0;
				}
			}
			for (j = 0; j < i; j++) {
				char sym2[UI_DN_SYM_MAX], cj[UI_DN_PATH_MAX], hj[UI_DN_PATH_MAX];
				if (g_d.doc.hosts[j].group >= 0) continue;
				UiDesign_Symbol(g_d.doc.hosts[j].id, sym2, sizeof(sym2));
				snprintf(cj, sizeof(cj), "%s.ui.c", sym2);
				snprintf(hj, sizeof(hj), "%s.ui.h", sym2);
				if (strcmp(wc, cj) == 0 || strcmp(wc, hj) == 0 ||
				    strcmp(wh, cj) == 0 || strcmp(wh, hj) == 0) {
					snprintf(msg, (size_t)cap,
						"未分组窗口输出冲突: %s", wc);
					return 0;
				}
			}
		}
		n = 0;
		d_collect_nodes(&g_d.doc,
			g_d.doc.views[g_d.doc.hosts[i].view].root, list, 256, &n);
		for (j = 0; j < n; j++)
			for (k = j + 1; k < n; k++)
				if (strcmp(g_d.doc.nodes[list[j]].id, g_d.doc.nodes[list[k]].id) == 0) {
					snprintf(msg, (size_t)cap, "组件 ID 重复: %s", g_d.doc.nodes[list[j]].id);
					return 0;
				}
	}
	for (i = 0; i < g_d.doc.viewCount; i++) {
		for (j = i + 1; j < g_d.doc.viewCount; j++)
			if (strcmp(g_d.doc.views[i].id, g_d.doc.views[j].id) == 0) {
				snprintf(msg, (size_t)cap, "视图 ID 重复: %s", g_d.doc.views[i].id);
				return 0;
			}
		if (g_d.doc.views[i].root < 0) {
			snprintf(msg, (size_t)cap, "视图根节点缺失: %s", g_d.doc.views[i].id);
			return 0;
		}
		n = 0;
		d_collect_nodes(&g_d.doc, g_d.doc.views[i].root, list, 256, &n);
		for (j = 0; j < n; j++)
			for (k = j + 1; k < n; k++)
				if (strcmp(g_d.doc.nodes[list[j]].id, g_d.doc.nodes[list[k]].id) == 0) {
					snprintf(msg, (size_t)cap, "组件 ID 重复: %s",
						g_d.doc.nodes[list[j]].id);
					return 0;
				}
	}
	return 1;
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

static int d_collect_free_sel(int *out, int cap)
{
	int i, n = 0;
	for (i = 0; i < g_d.selSetN && n < cap; i++) {
		int idx = g_d.selSet[i];
		UiDesignNode *nd;
		if (idx < 0 || idx >= g_d.doc.nodeCount) continue;
		nd = &g_d.doc.nodes[idx];
		if (nd->parent < 0 || !d_parent_free(nd->parent)) continue;
		if (idx == d_view_root()) continue;
		out[n++] = idx;
	}
	return n;
}

static void d_do_align(int action)
{
	int list[128], n, i, j;
	float minX = 0, maxX = 0, minY = 0, maxY = 0;
	if (!g_d.layoutReady) d_layout_current();
	n = d_collect_free_sel(list, 128);
	if (n < 2) {
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
		if (n < 3) { d_status("分布需要至少三个组件"); return; }
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
				UiRect pr = g_d.rects[nd->parent];
				float inner = action == 6
					? pr.x + g_d.doc.nodes[nd->parent].padding[0]
					: pr.y + g_d.doc.nodes[nd->parent].padding[1];
				float c = c0 + step * (float)i;
				if (action == 6) nd->x = c - g_d.rects[idx].w * 0.5f - inner;
				else nd->y = c - g_d.rects[idx].h * 0.5f - inner;
			}
		}
	} else {
		for (i = 0; i < n; i++) {
			int idx = list[i];
			UiDesignNode *nd = &g_d.doc.nodes[idx];
			UiRect pr = g_d.rects[nd->parent];
			float innerX = pr.x + g_d.doc.nodes[nd->parent].padding[0];
			float innerY = pr.y + g_d.doc.nodes[nd->parent].padding[1];
			UiRect r = g_d.rects[idx];
			if (action == 0) nd->x = minX - innerX;
			else if (action == 1) nd->x = (minX + maxX - r.w) * 0.5f - innerX;
			else if (action == 2) nd->x = maxX - r.w - innerX;
			else if (action == 3) nd->y = minY - innerY;
			else if (action == 4) nd->y = (minY + maxY - r.h) * 0.5f - innerY;
			else if (action == 5) nd->y = maxY - r.h - innerY;
		}
	}
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
		int list[256], count = 0;
		y = props.y - (float)g_d.propScroll;
		if (root < 0) {
			Gfx_Text(g, t->fSmall, "当前视图没有组件", props, t->text4,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
			return;
		}
		d_collect_nodes(&g_d.doc, root, list, 256, &count);
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
	if (g_d.editActive) UiInput_BeginFrame(&g_d.propForm);
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
			if (!(g_d.editActive && g_d.editKey == p->key)) {
				Gfx_FillRoundRect(g, vr, 4.0f, t->inputBg);
				Gfx_StrokeRoundRect(g, vr, 4.0f, t->inputBorder, 1.0f);
				Gfx_Text(g, t->fSmall, p->value,
					Ui_Rect(vr.x + 6, vr.y, vr.w - 12, vr.h), t->text,
					GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			}
			if (ui->clicked == rid && !(g_d.editActive && g_d.editKey == p->key))
				d_prop_start_edit(i);
			y += PROP_ROW_H;
		}
	}
	if (g_d.editActive) {
		for (i = 0; i < g_d.propN; i++) {
			if (g_d.props[i].key == g_d.editKey) {
				UiRect vr = Ui_Rect(props.x + 128, g_d.propRowRc[i].y + 2,
					props.w - 132, PROP_ROW_H - 4);
				UiInput_SetRect(&g_d.propForm, 0, vr);
				UiInput_Draw(&g_d.propForm, 0);
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
		if (g_d.instReady) {
			UiDesignInstance_Shutdown(&g_d.inst);
			g_d.instReady = 0;
		}
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
			}
			return hit;
		}
		hit = n->parent;
	}
	return d_view_root();
}

static void d_insert_at(float x, float y)
{
	int parent, idx;
	UiDesignNode *p, *n;
	char id[UI_DN_ID_MAX], name[UI_DN_NAME_MAX];
	if (g_d.insertType < 0 || g_d.selView < 0) return;
	parent = d_container_at(x, y);
	if (parent < 0) return;
	p = &g_d.doc.nodes[parent];
	if (!UiDesign_IsContainer(p->type)) return;
	d_after_edit();
	d_unique_node_id(g_d.selView, UiDesign_TypeName(g_d.insertType), id, sizeof(id));
	UiDesign_SetText(name, sizeof(name), UiDesign_TypeName(g_d.insertType));
	idx = UiDesign_AddNode(&g_d.doc, g_d.selView, g_d.insertType, parent, id, name);
	n = UiDesign_Node(&g_d.doc, idx);
	if (!n) return;
	if (UiDesign_IsContainer(g_d.insertType)) {
		n->wMode = UI_DN_SIZE_FILL;
		n->hMode = UI_DN_SIZE_FIXED;
		n->h = g_d.insertType == UI_DN_ROW ? 64.0f : 180.0f;
		n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 8.0f;
		n->gap = 8.0f;
		if (g_d.insertType == UI_DN_GRID) d_grid_defaults(n);
	} else {
		n->wMode = UI_DN_SIZE_FIXED;
		n->hMode = UI_DN_SIZE_FIXED;
		n->w = UiDesign_DefaultW(g_d.insertType);
		n->h = UiDesign_DefaultH(g_d.insertType);
		/* Grid 子项铺满单元格; Column/Row 按交叉轴铺满容器. */
		if (p->type == UI_DN_GRID) {
			n->wMode = UI_DN_SIZE_FILL;
			n->hMode = UI_DN_SIZE_FILL;
		} else if (p->type == UI_DN_COLUMN) {
			n->wMode = UI_DN_SIZE_FILL;
		} else if (p->type == UI_DN_ROW) {
			n->hMode = UI_DN_SIZE_FILL;
		} else if (p->type == UI_DN_PANEL && p->parent >= 0 &&
		    g_d.doc.nodes[p->parent].type == UI_DN_TABS) {
			n->wMode = UI_DN_SIZE_FILL;
		}
		if (g_d.insertType == UI_DN_LABEL) UiDesign_NodeSetText(n, "Label");
		else if (g_d.insertType == UI_DN_BUTTON) UiDesign_NodeSetText(n, "Button");
		else if (g_d.insertType == UI_DN_INPUT) UiDesign_NodeSetHint(n, "请输入");
		else if (g_d.insertType == UI_DN_SELECT) {
			UiDesign_NodeSetOptions(n, "选项1,选项2,选项3");
			UiDesign_NodeSetHint(n, "请选择");
		} else if (g_d.insertType == UI_DN_IMAGE) UiDesign_NodeSetText(n, "Image");
		else if (g_d.insertType == UI_DN_TEXTAREA) UiDesign_NodeSetHint(n, "多行文本");
		else if (g_d.insertType == UI_DN_DATEPICKER) UiDesign_NodeSetHint(n, "选择日期");
	}
	if (g_d.insertType == UI_DN_TABS) {
		n->h = 220.0f;
		n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 10.0f;
		n->gap = 0.0f;
		n->selected = 0;
		d_tabs_add_pages(g_d.selView, idx);
		p = &g_d.doc.nodes[parent];
		n = &g_d.doc.nodes[idx];
	}
	if (p->type == UI_DN_ABSOLUTE || p->type == UI_DN_PANEL) {
		UiRect pr = g_d.rects[parent];
		n->x = (float)((int)((x - pr.x - p->padding[0]) / 4.0f + 0.5f)) * 4.0f;
		n->y = (float)((int)((y - pr.y - p->padding[1]) / 4.0f + 0.5f)) * 4.0f;
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
		if (Ui_RectContains(g_d.treeRowRc[i], x, y)) return i;
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
		if (mode == 1 && UiDesign_IsContainer(dn->type)) {
			parent = target;
			pos = dn->childCount;
		} else {
			parent = dn->parent;
			pos = d_tree_child_pos(parent, target) + (mode == 2 ? 1 : 0);
		}
		if (parent >= 0 && UiDesign_NodeMove(&g_d.doc, src, parent, pos)) {
			g_d.doc.dirty = 1;
			g_d.layoutReady = 0;
			d_undo_push();
			d_status("组件树层级已调整");
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
	if (g_d.preview) {
		if (g_d.instReady) UiDesign_MouseDown(&g_d.inst, x, y);
		return;
	}
	if (g_d.editActive) {
		UiId hit = Ui_HitTest(&g_d.app->ui, x, y);
		if (hit == g_d.propForm.fields[0].id) {
			UiInput_MouseDown(&g_d.propForm, hit, x);
			return;
		}
		d_after_edit();
	}
	{
		int tr = d_tree_row_at(x, y);
		if (tr >= 0) {
			int idx = g_d.treeRowIdx[tr];
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
			g_d.dragging = 2;
			g_d.dragHandle = handle;
			g_d.dragStartX = x;
			g_d.dragStartY = y;
			g_d.dragNodeX = n->x;
			g_d.dragNodeY = n->y;
			g_d.dragNodeW = n->w;
			g_d.dragNodeH = n->h;
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
		if (additive) d_sel_toggle(hitNode);
		else if (d_sel_has(hitNode)) {
			g_d.selNode = hitNode;
			g_d.selKind = DSEL_NODE;
		} else d_sel_single(hitNode);
		n = &g_d.doc.nodes[hitNode];
		p = n->parent >= 0 ? &g_d.doc.nodes[n->parent] : NULL;
		if (d_sel_has(hitNode) && p && d_parent_free(n->parent) &&
		    hitNode != d_view_root()) {
			g_d.dragging = 1;
			g_d.dragStartX = x;
			g_d.dragStartY = y;
			g_d.dragNodeX = n->x;
			g_d.dragNodeY = n->y;
			g_d.dragMoved = 0;
			d_drag_begin(hitNode);
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
	UiRect pr = g_d.rects[n->parent];
	UiRect inner;
	float bestX = 4.0f, bestY = 4.0f;
	float cx[3], cy[3];
	int i, k;
	d_inset(pr, g_d.doc.nodes[n->parent].padding, &inner);
	g_d.snapX = g_d.snapY = 0;
	cx[0] = inner.x; cx[1] = inner.x + inner.w * 0.5f; cx[2] = inner.x + inner.w;
	cy[0] = inner.y; cy[1] = inner.y + inner.h * 0.5f; cy[2] = inner.y + inner.h;
	for (i = 0; i < g_d.doc.nodes[n->parent].childCount; i++) {
		int ci = g_d.doc.nodes[n->parent].children[i];
		UiRect r;
		if (ci == g_d.selNode) continue;
		r = g_d.rects[ci];
		for (k = 0; k < 3; k++) {
			float tx = k == 0 ? r.x : (k == 1 ? r.x + r.w * 0.5f : r.x + r.w);
			float ty = k == 0 ? r.y : (k == 1 ? r.y + r.h * 0.5f : r.y + r.h);
			float ax[3] = { *x, *x + n->w * 0.5f, *x + n->w };
			float ay[3] = { *y, *y + n->h * 0.5f, *y + n->h };
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
	*x = (float)((int)(*x / 4.0f + 0.5f)) * 4.0f;
	*y = (float)((int)(*y / 4.0f + 0.5f)) * 4.0f;
}

static int d_node_overflow(const UiDesignNode *n)
{
	UiRect pr, inner, nr;
	if (!n || n->parent < 0) return 0;
	pr = g_d.rects[n->parent];
	d_inset(pr, g_d.doc.nodes[n->parent].padding, &inner);
	nr = Ui_Rect(n->x + pr.x, n->y + pr.y, n->w, n->h);
	return nr.x < inner.x - 1 || nr.y < inner.y - 1 ||
		nr.x + nr.w > inner.x + inner.w + 1 ||
		nr.y + nr.h > inner.y + inner.h + 1;
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
		UiInput_MouseMove(&g_d.propForm, x);
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
		if (tr < 0) {
			g_d.treeDropIdx = -1;
		} else {
			UiRect rc = g_d.treeRowRc[tr];
			float t = (y - rc.y) / (rc.h > 0 ? rc.h : 1.0f);
			g_d.treeDropIdx = g_d.treeRowIdx[tr];
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
		if (!n) return;
		if (fabsf_local(dx) + fabsf_local(dy) > 1.5f) g_d.dragMoved = 1;
		if (g_d.dragging == 1) {
			float nx = g_d.dragNodeX + dx, ny = g_d.dragNodeY + dy;
			float sdx, sdy;
			int i;
			d_snap_move(n, &nx, &ny);
			sdx = nx - g_d.dragNodeX;
			sdy = ny - g_d.dragNodeY;
			for (i = 0; i < g_d.dragListN; i++) {
				UiDesignNode *dn = UiDesign_Node(&g_d.doc, g_d.dragListIdx[i]);
				if (!dn) continue;
				dn->x = g_d.dragListX[i] + sdx;
				dn->y = g_d.dragListY[i] + sdy;
			}
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
		UiInput_MouseUp(&g_d.propForm);
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
		int i, n = g_d.selSetN;
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
					for (k = 0; k < n; k++) if (g_d.selSet[k] == i) has = 1;
					if (!has && n < 128) g_d.selSet[n++] = i;
					g_d.selNode = i;
				}
			}
			g_d.selSetN = n;
			g_d.selKind = n > 0 ? DSEL_NODE : DSEL_WINDOW;
		}
		UiApp_Invalidate(g_d.app);
		return;
	}
	if (g_d.dragging) {
		int moved = g_d.dragMoved;
		g_d.dragging = 0;
		g_d.dragMoved = 0;
		g_d.dragListN = 0;
		g_d.snapX = g_d.snapY = 0;
		if (moved) d_undo_push();
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
	if (g_d.editActive) {
		if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_TAB) {
			d_after_edit();
			return 1;
		}
		return UiInput_Key(&g_d.propForm, g_d.app->hwnd, vk);
	}
	if (vk == VK_ESCAPE && g_d.insertType >= 0) {
		g_d.insertType = -1;
		UiButtonGroup_SetSelected(&g_d.layoutGroup, 0);
		UiButtonGroup_SetSelected(&g_d.compGroup, -1);
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
	if (g_d.selKind == DSEL_NODE && g_d.selNode >= 0) {
		UiDesignNode *n = d_selnode();
		int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 8 : 1;
		if (n && n->parent >= 0 && g_d.doc.nodes[n->parent].type == UI_DN_ABSOLUTE) {
			if (vk == VK_LEFT) { n->x -= step; goto moved; }
			if (vk == VK_RIGHT) { n->x += step; goto moved; }
			if (vk == VK_UP) { n->y -= step; goto moved; }
			if (vk == VK_DOWN) { n->y += step; goto moved; }
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
	if (g_d.editActive) return UiInput_Char(&g_d.propForm, ch);
	return 0;
}

static void d_ime(void)
{
	if (g_d.preview) { if (g_d.instReady) UiDesign_OnIme(&g_d.inst); }
	else if (g_d.editActive) UiInput_OnIme(&g_d.propForm);
	else {
		if (g_d.propColorReady) UiColorSelect_OnIme(&g_d.propColor, &g_d.app->ui);
		if (g_d.propColorBgReady) UiColorSelect_OnIme(&g_d.propColorBg, &g_d.app->ui);
	}
}

static void d_blink(void)
{
	if (g_d.preview) { if (g_d.instReady) UiDesign_Blink(&g_d.inst); }
	else {
		if (g_d.editActive) UiInput_Blink(&g_d.propForm);
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
	if (!g_d.preview && g_d.propColorReady &&
	    UiColorSelect_IsOpen(&g_d.pm, &g_d.propColor))
		return 1;
	if (!g_d.preview && g_d.propColorBgReady &&
	    UiColorSelect_IsOpen(&g_d.pm, &g_d.propColorBg))
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
	g_d.propForm.count = 0;
	g_d.propField = UiInput_Add(&g_d.propForm, Ui_Id("dsn.prop", 0, 0),
		256, 0, NULL, NULL);
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
	UiButtonGroup_Init(&g_d.layoutGroup, Ui_Id("dsn.layoutgrp", 0, 0),
		UI_BG_SINGLE, 6);
	UiButtonGroup_SetSelected(&g_d.layoutGroup, 0);
	UiButtonGroup_Init(&g_d.compGroup, Ui_Id("dsn.compgrp", 0, 0),
		UI_BG_SINGLE, 15);
	UiButtonGroup_SetSelected(&g_d.compGroup, -1);
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
	if (g_d.startupFile[0]) {
		if (UiDesign_LoadFile(&g_d.doc, g_d.startupFile)) {
			d_normalize_modes();
			d_reset_selection();
			d_undo_reset();
			d_dir_of(g_d.startupFile, g_d.lastDir, MAX_PATH);
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
	UiNavBar_DrawPopups(&g_d.nav, &g_d.pm, &app->ui);
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
	if (g_d.instReady) {
		UiDesignInstance_Shutdown(&g_d.inst);
		g_d.instReady = 0;
	}
	UiInput_Shutdown(&g_d.propForm);
	d_qr_slots_free();
	UiDesign_Free(&g_d.doc);
	d_undo_free();
	if (g_d.rects) HeapFree(GetProcessHeap(), 0, g_d.rects);
	g_d.rects = NULL;
}

static void d_parse_startup_file(WCHAR *out, int cap)
{
	const WCHAR *p;
	int n = 0;
	if (!out || cap <= 0) return;
	out[0] = 0;
	p = GetCommandLineW();
	if (!p) return;
	if (*p == L'"') {
		p++;
		while (*p && *p != L'"') p++;
		if (*p) p++;
	} else {
		while (*p && *p != L' ' && *p != L'\t') p++;
	}
	while (*p == L' ' || *p == L'\t') p++;
	if (*p == L'"') {
		p++;
		while (*p && *p != L'"' && n < cap - 1) out[n++] = *p++;
	} else {
		while (*p && *p != L' ' && *p != L'\t' && n < cap - 1) out[n++] = *p++;
	}
	out[n] = 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&g_d, sizeof(g_d));
	d_parse_startup_file(g_d.startupFile, MAX_PATH);
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



