/*
 * svgedit.c - SVG 编辑器组件样例
 *
 * 布局: 顶栏 (导出/复制/主题) + 工具栏 (工具/撤销/网格/缩放) +
 *       画布 (UiSvgEdit) + 右侧属性面板 + 底部状态栏。
 * 画布交互全部由组件处理; 这里只做工具箱、属性面板、导出/剪贴板。
 */
#include <windows.h>
#include <commdlg.h>
#include "ui/ui_app.h"
#include "ui/ui_color_select.h"
#include "ui/ui_frame.h"
#include "ui/ui_input.h"
#include "ui/ui_paint.h"
#include "ui/ui_popup.h"
#include "ui/ui_svg_edit.h"
#include "ui/ui_widgets.h"
#include "edit.h"

#define ED_CANVAS_MIN_W 520.0f

typedef struct EdDemo {
	UiApp *app;
	SvgDoc *doc;
	UiSvgEdit *ed;
	UiInput form;

	int fX, fY, fW, fH, fRot, fStroke, fOpacity, fAngle;
	int fNodeAng, fNodeLen;
	int fName, fText, fFamily, fSize;
	int inspTab;          /* 0 样式 / 1 几何 / 2 文本 / 3 图层 */
	int nameSyncIdx, textSyncIdx;
	char geoLabel[4][10];
	long syncKey;
	int syncType;

	int wantExport;
	int wantCopy;
	int ctxOpen;
	float ctxX, ctxY;
	UiRect ctxRc;

	UiPopupManager pm;
	UiColorSelect csFill, csStroke;
	long csKey;
	int lastSelIdx, lastSelCount;
	char msg[192];
	DWORD msgTick;

	UiRect canvasRc;
	UiRect statusRc;
} EdDemo;

static EdDemo g_demo;

/* ── 文本/数字工具 (0CRT: 无 sprintf %f) ─────────────────────────── */

static void num_i(char *b, int v)
{
	char tmp[16];
	int n = 0, neg = 0;
	if (v < 0) { neg = 1; v = -v; }
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v > 0 && n < 15);
	{
		int i = 0;
		if (neg) b[i++] = '-';
		while (n > 0) b[i++] = tmp[--n];
		b[i] = 0;
	}
}

static void num_f1(char *b, float v)
{
	long r;
	int i = 0, len = 0;
	if (v < 0.0f) { b[i++] = '-'; v = -v; }
	r = (long)(v * 10.0f + 0.5f);
	num_i(b + i, (int)(r / 10));
	while (b[len]) len++;
	b[len++] = '.';
	b[len++] = (char)('0' + (int)(r % 10));
	b[len] = 0;
}

static int parse_f(const char *s, float *out)
{
	float sign = 1.0f, val = 0.0f, frac = 0.1f;
	int any = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { sign = -1.0f; s++; }
	else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') { val = val * 10.0f + (float)(*s - '0'); s++; any = 1; }
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') {
			val += (float)(*s - '0') * frac;
			frac *= 0.1f;
			s++;
			any = 1;
		}
	}
	if (!any) return 0;
	*out = sign * val;
	return 1;
}

static void set_msg(EdDemo *d, const char *s)
{
	int i = 0;
	while (s && s[i] && i < (int)sizeof(d->msg) - 1) { d->msg[i] = s[i]; i++; }
	d->msg[i] = 0;
	d->msgTick = GetTickCount();
}

/* ── 样式辅助 ─────────────────────────────────────────────────────── */

static int dash_index(const SvgStyle *st)
{
	if (st->dashCount == 0) return 0;
	if (st->dashCount == 2 && st->dash[0] == 1.0f && st->dash[1] == 3.0f)
		return 1;
	if (st->dashCount == 2 && st->dash[0] == 6.0f && st->dash[1] == 4.0f)
		return 2;
	if (st->dashCount == 4 && st->dash[0] == 8.0f && st->dash[1] == 3.0f &&
		st->dash[2] == 1.0f && st->dash[3] == 3.0f)
		return 3;
	return 0;
}

static void dash_apply(SvgStyle *st, int idx)
{
	static const float p0[1] = { 0 };
	static const float p1[2] = { 1, 3 };
	static const float p2[2] = { 6, 4 };
	static const float p3[4] = { 8, 3, 1, 3 };
	int i;
	st->dashCount = 0;
	if (idx == 1) { st->dashCount = 2; for (i = 0; i < 2; i++) st->dash[i] = p1[i]; }
	else if (idx == 2) { st->dashCount = 2; for (i = 0; i < 2; i++) st->dash[i] = p2[i]; }
	else if (idx == 3) { st->dashCount = 4; for (i = 0; i < 4; i++) st->dash[i] = p3[i]; }
	(void)p0;
}

/* ── 属性面板字段同步/应用 ───────────────────────────────────────── */

static void geo_labels(EdDemo *d, int type)
{
	if (type == SVG_EL_ELLIPSE) {
		lstrcpynA(d->geoLabel[0], "CX", 10);
		lstrcpynA(d->geoLabel[1], "CY", 10);
		lstrcpynA(d->geoLabel[2], "RX", 10);
		lstrcpynA(d->geoLabel[3], "RY", 10);
	} else if (type == SVG_EL_LINE) {
		lstrcpynA(d->geoLabel[0], "X1", 10);
		lstrcpynA(d->geoLabel[1], "Y1", 10);
		lstrcpynA(d->geoLabel[2], "X2", 10);
		lstrcpynA(d->geoLabel[3], "Y2", 10);
	} else {
		lstrcpynA(d->geoLabel[0], "X", 10);
		lstrcpynA(d->geoLabel[1], "Y", 10);
		lstrcpynA(d->geoLabel[2], "W", 10);
		lstrcpynA(d->geoLabel[3], "H", 10);
	}
}

static void sync_fields(EdDemo *d)
{
	int idx = Ui_SvgEdit_SelIndex(d->ed, 0);
	SvgElement *e = NULL;
	SvgStyle st;
	long key;
	char b[32];
	int type = -1;
	int eidx, nidx = Ui_SvgEdit_NodeIndex(d->ed, &eidx);
	if (idx >= 0 && idx < d->doc->count) {
		e = &d->doc->els[idx];
		type = (int)e->type;
	}
	key = (long)(idx + 1) * 1000003L + (long)type * 7919L +
		(e ? e->version : 0) + (long)Ui_SvgEdit_SelCount(d->ed) * 131L +
		(long)(nidx + 1) * 17L;
	if (key == d->syncKey && type == d->syncType) return;
	if (UiInput_Focused(&d->form) >= 0) return;
	d->syncKey = key;
	d->syncType = type;
	geo_labels(d, type);

	if (!e) {
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fX), "");
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fY), "");
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fW), "");
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fH), "");
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fRot), "0.0");
	} else if (e->type == SVG_EL_RECT) {
		num_f1(b, e->p.rect.x); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fX), b);
		num_f1(b, e->p.rect.y); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fY), b);
		num_f1(b, e->p.rect.w); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fW), b);
		num_f1(b, e->p.rect.h); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fH), b);
		num_f1(b, e->rot); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fRot), b);
	} else if (e->type == SVG_EL_ELLIPSE) {
		num_f1(b, e->p.ellipse.cx); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fX), b);
		num_f1(b, e->p.ellipse.cy); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fY), b);
		num_f1(b, e->p.ellipse.rx); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fW), b);
		num_f1(b, e->p.ellipse.ry); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fH), b);
		num_f1(b, e->rot); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fRot), b);
	} else if (e->type == SVG_EL_LINE) {
		num_f1(b, e->p.line.x1); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fX), b);
		num_f1(b, e->p.line.y1); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fY), b);
		num_f1(b, e->p.line.x2); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fW), b);
		num_f1(b, e->p.line.y2); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fH), b);
		num_f1(b, e->rot); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fRot), b);
	} else {
		GfxRect lb = Svg_ElementLocalBounds(e);
		num_f1(b, lb.x); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fX), b);
		num_f1(b, lb.y); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fY), b);
		num_f1(b, lb.w); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fW), b);
		num_f1(b, lb.h); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fH), b);
		num_f1(b, e->rot); Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fRot), b);
	}

	Ui_SvgEdit_GetStyle(d->ed, &st);
	num_f1(b, st.strokeWidth);
	Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fStroke), b);
	num_f1(b, st.opacity * 100.0f);
	Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fOpacity), b);
	num_f1(b, st.fill.angleDeg);
	Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fAngle), b);
	Svg_StyleFree(&st);

	if (nidx >= 0) {
		float ang = 0.0f, len = 0.0f;
		int sym = 0;
		if (Ui_SvgEdit_NodeParams(d->ed, &ang, &len, &sym)) {
			num_f1(b, ang);
			Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fNodeAng), b);
			num_f1(b, len);
			Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fNodeLen), b);
		}
	} else {
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fNodeAng), "");
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fNodeLen), "");
	}
}

static int field_float(EdDemo *d, int field, float *out)
{
	char b[64];
	Edit_TextUtf8(UiInput_Edit(&d->form, field), b, (int)sizeof(b));
	return parse_f(b, out);
}

static void path_scale_about(SvgElement *e, float ax, float ay,
	float sx, float sy)
{
	int i, n = e->path.count;
	for (i = 0; i < n; i++) {
		SvgNode *nd = &e->path.nodes[i];
		nd->x = ax + (nd->x - ax) * sx;
		nd->y = ay + (nd->y - ay) * sy;
		nd->inX *= sx; nd->inY *= sy;
		nd->outX *= sx; nd->outY *= sy;
	}
	n = e->path.closed ? e->path.count : e->path.count - 1;
	for (i = 0; i < n && i < e->path.count; i++) {
		if (e->path.seg[i] == SVG_SEG_ARC) {
			e->path.arc[i].rx = e->path.arc[i].rx * (sx < 0 ? -sx : sx);
			e->path.arc[i].ry = e->path.arc[i].ry * (sy < 0 ? -sy : sy);
		}
	}
}

static void path_translate(SvgElement *e, float dx, float dy)
{
	int i;
	for (i = 0; i < e->path.count; i++) {
		e->path.nodes[i].x += dx;
		e->path.nodes[i].y += dy;
	}
}

static void apply_fields(EdDemo *d, UiApp *app)
{
	int idx = Ui_SvgEdit_SelIndex(d->ed, 0);
	float vx, vy, vw, vh, vrot, vsw, vop, vang;
	int okx, oky, okw, okh, okrot, oksw, okop, okang;
	int i;
	if (idx < 0 || idx >= d->doc->count) return;
	okx = field_float(d, d->fX, &vx);
	oky = field_float(d, d->fY, &vy);
	okw = field_float(d, d->fW, &vw);
	okh = field_float(d, d->fH, &vh);
	okrot = field_float(d, d->fRot, &vrot);
	oksw = field_float(d, d->fStroke, &vsw);
	okop = field_float(d, d->fOpacity, &vop);
	okang = field_float(d, d->fAngle, &vang);

	{
		SvgElement *e = &d->doc->els[idx];
		if (e->type == SVG_EL_RECT) {
			if (okx) e->p.rect.x = vx;
			if (oky) e->p.rect.y = vy;
			if (okw) e->p.rect.w = vw < 1.0f ? 1.0f : vw;
			if (okh) e->p.rect.h = vh < 1.0f ? 1.0f : vh;
		} else if (e->type == SVG_EL_ELLIPSE) {
			if (okx) e->p.ellipse.cx = vx;
			if (oky) e->p.ellipse.cy = vy;
			if (okw) e->p.ellipse.rx = vw < 0.5f ? 0.5f : vw;
			if (okh) e->p.ellipse.ry = vh < 0.5f ? 0.5f : vh;
		} else if (e->type == SVG_EL_LINE) {
			if (okx) e->p.line.x1 = vx;
			if (oky) e->p.line.y1 = vy;
			if (okw) e->p.line.x2 = vw;
			if (okh) e->p.line.y2 = vh;
		} else {
			GfxRect lb = Svg_ElementLocalBounds(e);
			if (okx) path_translate(e, vx - lb.x, 0.0f);
			if (oky) path_translate(e, 0.0f, vy - lb.y);
			if (okw && lb.w > 0.5f && vw > 0.5f)
				path_scale_about(e, lb.x, lb.y, vw / lb.w, 1.0f);
			if (okh && lb.h > 0.5f && vh > 0.5f)
				path_scale_about(e, lb.x, lb.y, 1.0f, vh / lb.h);
		}
		if (okrot) e->rot = vrot;
		Svg_ElementTouch(e);
	}
	for (i = 0; i < Ui_SvgEdit_SelCount(d->ed); i++) {
		int si = Ui_SvgEdit_SelIndex(d->ed, i);
		SvgElement *se;
		if (si < 0 || si >= d->doc->count) continue;
		se = &d->doc->els[si];
		if (oksw) se->style.strokeWidth = vsw < 0.0f ? 0.0f :
			(vsw > 64.0f ? 64.0f : vsw);
		if (okop) {
			float op = vop / 100.0f;
			se->style.opacity = op < 0.0f ? 0.0f : (op > 1.0f ? 1.0f : op);
		}
		if (okang) se->style.fill.angleDeg = vang;
		Svg_ElementTouch(se);
	}
	{
		int eidx;
		if (Ui_SvgEdit_NodeIndex(d->ed, &eidx) >= 0) {
			float nang, nlen;
			int sym = 0;
			if (field_float(d, d->fNodeAng, &nang) &&
				field_float(d, d->fNodeLen, &nlen)) {
				Ui_SvgEdit_NodeParams(d->ed, NULL, NULL, &sym);
				Ui_SvgEdit_SetNodeParams(d->ed, nang, nlen, sym);
			}
		}
	}
	Ui_SvgEdit_Commit(d->ed);
	d->syncKey = 0;
	UiApp_Invalidate(app);
}

/* ── 导出/剪贴板 ─────────────────────────────────────────────────── */

static int save_svg(EdDemo *d)
{
	wchar_t path[MAX_PATH];
	OPENFILENAMEW ofn;
	char *svg;
	HANDLE h;
	int ok = 0;
	path[0] = 0;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = d->app->hwnd;
	ofn.lpstrFilter = L"SVG 文件 (*.svg)\0*.svg\0所有文件\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"svg";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	if (!GetSaveFileNameW(&ofn)) return 0;
	svg = Svg_Export(d->doc);
	if (!svg) return 0;
	h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD wr = 0;
		int len = 0;
		while (svg[len]) len++;
		ok = WriteFile(h, svg, (DWORD)len, &wr, NULL) && wr == (DWORD)len;
		CloseHandle(h);
	}
	Svg_FreeString(svg);
	if (ok) set_msg(d, "已导出 SVG 文件");
	else set_msg(d, "导出失败：无法写入文件");
	return 1;
}

static void copy_svg(EdDemo *d)
{
	char *svg = Svg_Export(d->doc);
	int len, wn;
	HGLOBAL mem;
	if (!svg) return;
	len = 0;
	while (svg[len]) len++;
	wn = MultiByteToWideChar(CP_UTF8, 0, svg, len, NULL, 0);
	mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(wn + 1) * sizeof(wchar_t));
	if (mem) {
		wchar_t *wp = (wchar_t *)GlobalLock(mem);
		if (wp) {
			MultiByteToWideChar(CP_UTF8, 0, svg, len, wp, wn);
			wp[wn] = 0;
			GlobalUnlock(mem);
			if (OpenClipboard(d->app->hwnd)) {
				EmptyClipboard();
				SetClipboardData(CF_UNICODETEXT, mem);
				CloseClipboard();
				mem = NULL;
				set_msg(d, "SVG 已复制到剪贴板");
			}
		}
		if (mem) GlobalFree(mem);
	}
	Svg_FreeString(svg);
}

/* ── 初始文档 ─────────────────────────────────────────────────────── */

static void add_gradient_rect(SvgDoc *doc)
{
	SvgElement *e = SvgDoc_Add(doc, SVG_EL_RECT);
	SvgGradStop stops[2];
	if (!e) return;
	e->p.rect.x = 60.0f; e->p.rect.y = 60.0f;
	e->p.rect.w = 240.0f; e->p.rect.h = 150.0f;
	e->style.fill.kind = SVG_PAINT_LINEAR;
	e->style.fill.angleDeg = 0.0f;
	stops[0].offset = 0.0f; stops[0].color = 0xFF7FC5FFu;
	stops[1].offset = 1.0f; stops[1].color = 0xFF1D5FE0u;
	Svg_StyleSetFillStops(&e->style.fill, stops, 2);
	e->style.stroke.color = 0xFF1B4FA8u;
	e->style.strokeWidth = 2.0f;
	Svg_ElementTouch(e);
}

static void add_ellipse(SvgDoc *doc)
{
	SvgElement *e = SvgDoc_Add(doc, SVG_EL_ELLIPSE);
	if (!e) return;
	e->p.ellipse.cx = 560.0f; e->p.ellipse.cy = 210.0f;
	e->p.ellipse.rx = 130.0f; e->p.ellipse.ry = 90.0f;
	e->style.fill.color = 0xFFFFD8A8u;
	e->style.stroke.color = 0xFFD98324u;
	e->style.strokeWidth = 3.0f;
	e->rot = -12.0f;
	Svg_ElementTouch(e);
}

static void add_curve(SvgDoc *doc)
{
	SvgElement *e = SvgDoc_Add(doc, SVG_EL_PATH);
	SvgNode *n;
	if (!e) return;
	Svg_PathFree(&e->path);
	Svg_PathInit(&e->path);
	Svg_PathAddNode(&e->path, 80.0f, 420.0f, SVG_NODE_SMOOTH);
	Svg_PathAddNode(&e->path, 300.0f, 300.0f, SVG_NODE_SMOOTH);
	Svg_PathAddNode(&e->path, 500.0f, 380.0f, SVG_NODE_SMOOTH);
	n = &e->path.nodes[0];
	n->outX = 80.0f; n->outY = -40.0f; n->hasOut = 1;
	n = &e->path.nodes[1];
	n->inX = -90.0f; n->inY = 30.0f; n->hasIn = 1;
	n->outX = 110.0f; n->outY = 60.0f; n->hasOut = 1;
	n = &e->path.nodes[2];
	n->inX = -80.0f; n->inY = -40.0f; n->hasIn = 1;
	e->path.seg[0] = SVG_SEG_CUBIC;
	e->path.seg[1] = SVG_SEG_ARC;
	e->path.arc[1].rx = 130.0f;
	e->path.arc[1].ry = 130.0f;
	e->path.arc[1].large = 0;
	e->path.arc[1].sweep = 1;
	e->style.fill.kind = SVG_PAINT_NONE;
	e->style.stroke.color = 0xFF26A69Au;
	e->style.strokeWidth = 5.0f;
	e->style.cap = SVG_CAP_ROUND;
	Svg_ElementTouch(e);
}

/* ── 生命周期 ─────────────────────────────────────────────────────── */

static void on_init(UiApp *app, void *user)
{
	EdDemo *d = (EdDemo *)user;
	d->app = app;
	app->minW = 960.0f;
	app->minH = 640.0f;
	d->doc = SvgDoc_Create(800.0f, 600.0f);
	d->doc->background = 0xFFFFFFFFu;
	add_gradient_rect(d->doc);
	add_ellipse(d->doc);
	add_curve(d->doc);
	Svg_History_Commit(d->doc);
	d->ed = Ui_SvgEdit_Create(d->doc, Ui_Id("svgedit.canvas", 0, 0));

	UiInput_Init(&d->form, &app->ui, &app->ime);
	UiPopupManager_Init(&d->pm);
	UiColorSelect_Init(&d->csFill, Ui_Id("svgedit.cs.fill", 0, 0), 0xFF3B82F6u);
	UiColorSelect_Init(&d->csStroke, Ui_Id("svgedit.cs.stroke", 0, 0), 0xFFE0A33Eu);
	UiColorSelect_AttachIme(&d->csFill, &app->ime);
	UiColorSelect_AttachIme(&d->csStroke, &app->ime);
	d->csKey = 0;
	d->lastSelIdx = -2;
	d->lastSelCount = -1;
	d->fX = UiInput_Add(&d->form, Ui_Id("svgedit.x", 0, 0), 16, 0,
		"0123456789.-+", L"X");
	d->fY = UiInput_Add(&d->form, Ui_Id("svgedit.y", 0, 0), 16, 0,
		"0123456789.-+", L"Y");
	d->fW = UiInput_Add(&d->form, Ui_Id("svgedit.w", 0, 0), 16, 0,
		"0123456789.-+", L"W");
	d->fH = UiInput_Add(&d->form, Ui_Id("svgedit.h", 0, 0), 16, 0,
		"0123456789.-+", L"H");
	d->fRot = UiInput_Add(&d->form, Ui_Id("svgedit.rot", 0, 0), 16, 0,
		"0123456789.-+", L"0");
	d->fStroke = UiInput_Add(&d->form, Ui_Id("svgedit.sw", 0, 0), 16, 0,
		"0123456789.-+", L"2");
	d->fOpacity = UiInput_Add(&d->form, Ui_Id("svgedit.op", 0, 0), 16, 0,
		"0123456789.-+", L"100");
	d->fAngle = UiInput_Add(&d->form, Ui_Id("svgedit.ang", 0, 0), 16, 0,
		"0123456789.-+", L"0");
	d->fNodeAng = UiInput_Add(&d->form, Ui_Id("svgedit.nang", 0, 0), 16, 0,
		"0123456789.-+", L"角度");
	d->fNodeLen = UiInput_Add(&d->form, Ui_Id("svgedit.nlen", 0, 0), 16, 0,
		"0123456789.-+", L"力度");
	d->fName = UiInput_Add(&d->form, Ui_Id("svgedit.name", 0, 0), 47, 0,
		NULL, L"图层名称");
	d->fText = UiInput_Add(&d->form, Ui_Id("svgedit.text", 0, 0), 255, 0,
		NULL, L"文本内容");
	d->fFamily = UiInput_Add(&d->form, Ui_Id("svgedit.family", 0, 0), 47, 0,
		NULL, L"字体");
	d->fSize = UiInput_Add(&d->form, Ui_Id("svgedit.fsize", 0, 0), 8, 0,
		"0123456789.", L"字号");
	d->inspTab = 0;
	d->nameSyncIdx = -2;
	d->textSyncIdx = -2;
	geo_labels(d, -1);
	set_msg(d, "");
	d->msgTick = 0;
}

static void on_shutdown(UiApp *app, void *user)
{
	EdDemo *d = (EdDemo *)user;
	(void)app;
	UiInput_Shutdown(&d->form);
	UiColorSelect_Shutdown(&d->csFill);
	UiColorSelect_Shutdown(&d->csStroke);
	Ui_SvgEdit_Destroy(d->ed);
	SvgDoc_Destroy(d->doc);
	d->ed = NULL;
	d->doc = NULL;
}

/* ── 绘制辅助 ─────────────────────────────────────────────────────── */

static void label(UiApp *app, const char *text, UiRect rc)
{
	Gfx_Text(app->gfx, app->theme.fSmall, text, rc,
		app->theme.text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static UiButtonStyle btn_plain(void)
{
	return UiButtonStyle_Make(UI_BTYPE_DEFAULT, UI_BAPPEAR_PLAIN,
		UI_BSHAPE_RECT, 0);
}

static UiButtonStyle btn_active(int on)
{
	if (on)
		return UiButtonStyle_Make(UI_BTYPE_PRIMARY, UI_BAPPEAR_SOLID,
			UI_BSHAPE_RECT, 0);
	return btn_plain();
}

static int tool_button(EdDemo *d, UiContext *ui, UiRect rc,
	const char *text, int tool)
{
	UiButtonStyle st = btn_active(Ui_SvgEdit_Tool(d->ed) == tool);
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.tool", tool, 0), rc, text, &st, 1)) {
		Ui_SvgEdit_SetTool(d->ed, tool);
		return 1;
	}
	return 0;
}

static long cs_sel_key_(EdDemo *d)
{
	int idx = Ui_SvgEdit_SelIndex(d->ed, 0);
	long k = (long)(idx + 1) * 1000003L + (long)Ui_SvgEdit_SelCount(d->ed) * 131L;
	if (idx >= 0 && idx < d->doc->count) k += d->doc->els[idx].version;
	return k;
}

/* 触发器颜色跟随选中元素; 面板打开/拖动中不覆盖. */
static void cs_sync(EdDemo *d)
{
	SvgStyle st;
	long key = cs_sel_key_(d);
	if (key == d->csKey) return;
	d->csKey = key;
	if (UiColorSelect_IsOpen(&d->pm, &d->csFill) ||
		UiColorSelect_IsOpen(&d->pm, &d->csStroke)) return;
	Ui_SvgEdit_GetStyle(d->ed, &st);
	if (st.fill.kind == SVG_PAINT_NONE)
		UiColorSelect_SetColor(&d->csFill, 0xFF9AA3AFu);
	else if ((st.fill.kind == SVG_PAINT_LINEAR ||
			st.fill.kind == SVG_PAINT_RADIAL) && st.fill.stops &&
		st.fill.stopCount > 0)
		UiColorSelect_SetColor(&d->csFill, st.fill.stops[0].color);
	else
		UiColorSelect_SetColor(&d->csFill, st.fill.color);
	UiColorSelect_SetColor(&d->csStroke, st.stroke.kind == SVG_PAINT_NONE
		? 0xFF9AA3AFu : st.stroke.color);
	Svg_StyleFree(&st);
}

static void cs_apply(EdDemo *d, int stroke, UiColor c)
{
	SvgStyle st;
	if (Ui_SvgEdit_SelCount(d->ed) <= 0) return;
	Ui_SvgEdit_GetStyle(d->ed, &st);
	if (stroke) {
		st.stroke.kind = SVG_PAINT_SOLID;
		st.stroke.color = c;
	} else {
		if (st.fill.kind == SVG_PAINT_NONE) st.fill.kind = SVG_PAINT_SOLID;
		st.fill.color = c;
		if ((st.fill.kind == SVG_PAINT_LINEAR ||
				st.fill.kind == SVG_PAINT_RADIAL) && st.fill.stops &&
			st.fill.stopCount > 0)
			st.fill.stops[0].color = c;
	}
	Ui_SvgEdit_SetStyleLive(d->ed, &d->app->ui, &st);
	Svg_StyleFree(&st);
}

static void fill_row_ui(EdDemo *d, UiContext *ui, float x, float y)
{
	SvgStyle st;
	UiButtonStyle plain = btn_plain();
	const char *kindText;
	Ui_SvgEdit_GetStyle(d->ed, &st);
	cs_sync(d);
	if (st.fill.kind == SVG_PAINT_NONE) kindText = "填充: 无";
	else if (st.fill.kind == SVG_PAINT_LINEAR) kindText = "填充: 线";
	else if (st.fill.kind == SVG_PAINT_RADIAL) kindText = "填充: 径";
	else kindText = "填充: 纯色";

	if (Ui_ButtonEx(ui, Ui_Id("svgedit.fillkind", 0, 0),
		Ui_Rect(x, y, 84, 30), kindText, &plain, 1)) {
		switch (st.fill.kind) {
		case SVG_PAINT_NONE: st.fill.kind = SVG_PAINT_SOLID; break;
		case SVG_PAINT_SOLID: st.fill.kind = SVG_PAINT_LINEAR; break;
		case SVG_PAINT_LINEAR: st.fill.kind = SVG_PAINT_RADIAL; break;
		default: st.fill.kind = SVG_PAINT_NONE; break;
		}
		if ((st.fill.kind == SVG_PAINT_LINEAR ||
			 st.fill.kind == SVG_PAINT_RADIAL) && st.fill.stopCount < 2) {
			SvgGradStop stops[2];
			stops[0].offset = 0.0f;
			stops[0].color = st.fill.color ? st.fill.color : 0xFF2D7FF9u;
			stops[1].offset = 1.0f;
			stops[1].color = 0xFFFFFFFFu;
			Svg_StyleSetFillStops(&st.fill, stops, 2);
		}
		if (st.fill.kind == SVG_PAINT_RADIAL) {
			st.fill.cx = 0.5f; st.fill.cy = 0.5f; st.fill.r = 0.5f;
		}
		Ui_SvgEdit_SetStyle(d->ed, ui, &st);
		d->csKey = 0;
	}
	if (UiColorSelect_Draw(&d->csFill, &d->pm, ui,
		Ui_Rect(x + 92, y, 96, 30),
		Ui_SvgEdit_SelCount(d->ed) > 0 && st.fill.kind != SVG_PAINT_NONE) &&
		st.fill.kind != SVG_PAINT_NONE)
		cs_apply(d, 0, UiColorSelect_GetColor(&d->csFill));

	if (st.fill.kind == SVG_PAINT_LINEAR || st.fill.kind == SVG_PAINT_RADIAL) {
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.stopadd", 0, 0),
			Ui_Rect(x + 128, y + 120, 66, 28), "色标+", &plain, 1)) {
			SvgGradStop gs[17];
			int n = st.fill.stopCount, i;
			if (n > 16) n = 16;
			for (i = 0; i < n; i++) gs[i] = st.fill.stops[i];
			if (n >= 2) {
				gs[n].offset = st.fill.stops[n - 1].offset;
				gs[n].color = st.fill.stops[n - 1].color;
				gs[n - 1].offset = (st.fill.stops[n - 2].offset +
					st.fill.stops[n - 1].offset) * 0.5f;
				gs[n - 1].color = st.fill.stops[n - 2].color;
				Svg_StyleSetFillStops(&st.fill, gs, n + 1);
			}
			Ui_SvgEdit_SetStyle(d->ed, ui, &st);
		}
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.stopdel", 0, 0),
			Ui_Rect(x + 200, y + 120, 66, 28), "色标-", &plain, 1)) {
			if (st.fill.stopCount > 2)
				Svg_StyleSetFillStops(&st.fill, st.fill.stops,
					st.fill.stopCount - 1);
			Ui_SvgEdit_SetStyle(d->ed, ui, &st);
		}
	}
	Svg_StyleFree(&st);
}

static void stroke_row_ui(EdDemo *d, UiContext *ui, float x, float y)
{
	SvgStyle st;
	UiButtonStyle plain = btn_plain();
	char dashLabel[48];
	int di;
	Ui_SvgEdit_GetStyle(d->ed, &st);
	di = dash_index(&st);
	wsprintfA(dashLabel, "虚线 %d", di);

	if (Ui_ButtonEx(ui, Ui_Id("svgedit.strokekind", 0, 0),
		Ui_Rect(x, y, 80, 30),
		st.stroke.kind == SVG_PAINT_NONE ? "描边: 无" : "描边: 实色",
		&plain, 1)) {
		st.stroke.kind = st.stroke.kind == SVG_PAINT_NONE
			? SVG_PAINT_SOLID : SVG_PAINT_NONE;
		Ui_SvgEdit_SetStyle(d->ed, ui, &st);
	}
	if (UiColorSelect_Draw(&d->csStroke, &d->pm, ui,
		Ui_Rect(x + 88, y, 84, 30),
		Ui_SvgEdit_SelCount(d->ed) > 0 && st.stroke.kind != SVG_PAINT_NONE) &&
		st.stroke.kind != SVG_PAINT_NONE)
		cs_apply(d, 1, UiColorSelect_GetColor(&d->csStroke));
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.dash", 0, 0),
		Ui_Rect(x + 180, y, 92, 30), dashLabel, &plain, 1)) {
		dash_apply(&st, (di + 1) % 4);
		Ui_SvgEdit_SetStyle(d->ed, ui, &st);
	}
	Svg_StyleFree(&st);
}

/* ── M8: 三栏检查器 (样式/几何/图层) ─────────────────────────────── */

static int insp_tab_btn(EdDemo *d, UiContext *ui, UiRect rc, const char *label,
	int tab)
{
	UiButtonStyle st = btn_active(d->inspTab == tab);
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.insp.tab", tab, 0), rc, label, &st, 1)) {
		d->inspTab = tab;
		d->nameSyncIdx = -2;
		return 1;
	}
	return 0;
}

static void inspector_style_tab(EdDemo *d, UiContext *ui, UiRect panel)
{
	UiApp *app = d->app;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	SvgStyle st;
	UiButtonStyle plain = btn_plain();
	float x = panel.x + 14, y = panel.y + 72;
	char buf[64];

	fill_row_ui(d, ui, x, y);
	stroke_row_ui(d, ui, x, y + 40);
	Ui_SvgEdit_GetStyle(d->ed, &st);
	wsprintfA(buf, "端点:%s", st.cap == SVG_CAP_ROUND ? "圆"
		: (st.cap == SVG_CAP_SQUARE ? "方" : "平"));
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.cap", 0, 0),
		Ui_Rect(x, y + 84, 120, 30), buf, &plain, 1)) {
		st.cap = (st.cap + 1) % 3;
		Ui_SvgEdit_SetStyle(d->ed, ui, &st);
	}
	wsprintfA(buf, "连接:%s", st.join == SVG_JOIN_ROUND ? "圆"
		: (st.join == SVG_JOIN_BEVEL ? "斜" : "尖"));
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.join", 0, 0),
		Ui_Rect(x + 128, y + 84, 120, 30), buf, &plain, 1)) {
		st.join = (st.join + 1) % 3;
		Ui_SvgEdit_SetStyle(d->ed, ui, &st);
	}
	wsprintfA(buf, "规则:%s", st.fillRule ? "奇偶" : "非零");
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.fillrule", 0, 0),
		Ui_Rect(x, y + 120, 120, 28), buf, &plain, 1)) {
		st.fillRule = st.fillRule ? 0 : 1;
		Ui_SvgEdit_SetStyle(d->ed, ui, &st);
	}
	if (st.fill.kind == SVG_PAINT_LINEAR || st.fill.kind == SVG_PAINT_RADIAL) {
		/* 色标增删由 fill_row_ui 提供 (右侧第 1 行下) */
	} else {
		wsprintfA(buf, "渐变 %.0f°", st.fill.angleDeg);
		Gfx_Text(g, t->fSmall, buf, Ui_Rect(x + 128, y + 120, 140, 28),
			t->text3, GFX_TEXT_VCENTER);
	}
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.clipcopy", 0, 0),
		Ui_Rect(x, y + 152, 76, 26), "复制", &plain, 1))
		Ui_SvgEdit_CopySel(d->ed);
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.clippaste", 0, 0),
		Ui_Rect(x + 82, y + 152, 76, 26), "粘贴", &plain, 1))
		Ui_SvgEdit_Paste(d->ed, ui);
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.clipdup", 0, 0),
		Ui_Rect(x + 164, y + 152, 76, 26), "副本", &plain, 1))
		Ui_SvgEdit_DuplicateSel(d->ed, ui);
	Svg_StyleFree(&st);
	Gfx_Text(g, t->fSmall,
		"钢笔: L 直线  A 圆弧  Shift+A 三点弧  B 贝塞尔\n"
		"C 闭合  U 撤销节点  Enter/双击/右键 完成\n"
		"节点: 双击线段插入，拖橙点调弧，Shift 15° 吸附\n"
		"组: Ctrl+G 组合 / Ctrl+Shift+G 解组\n"
		"效率: Ctrl+C/V/D 复制粘贴副本 · 方向键微移",
		Ui_Rect(x, y + 190, panel.w - 28, 130), t->text3,
		GFX_TEXT_WRAP);
}

static void inspector_geom_tab(EdDemo *d, UiContext *ui, UiRect panel)
{
	UiApp *app = d->app;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	float x = panel.x + 14, y = panel.y + 72;
	float fx1 = x + 52, fx2 = x + 200;
	char buf[64];

	{
		int eidx, nidx = Ui_SvgEdit_NodeIndex(d->ed, &eidx);
		if (nidx >= 0) {
			float ang = 0.0f, len = 0.0f;
			int sym = 0;
			Ui_SvgEdit_NodeParams(d->ed, &ang, &len, &sym);
			wsprintfA(buf, "节点%d%s", nidx, sym ? "" : " 角");
			label(app, buf, Ui_Rect(x, y, 66, 28));
			label(app, "角度", Ui_Rect(x + 68, y, 34, 28));
			label(app, "力度", Ui_Rect(x + 166, y, 34, 28));
			UiInput_SetRect(&d->form, d->fNodeAng,
				Ui_Rect(x + 104, y, 58, 28));
			UiInput_SetRect(&d->form, d->fNodeLen,
				Ui_Rect(x + 202, y, 68, 28));
			UiInput_Draw(&d->form, d->fNodeAng);
			UiInput_Draw(&d->form, d->fNodeLen);
		} else {
			Gfx_Text(g, t->fSmall, Ui_SvgEdit_SelCount(d->ed) > 1
				? "多选：样式应用到全部" : "选中元素后可编辑",
				Ui_Rect(x, y, panel.w - 28, 18), t->text3,
				GFX_TEXT_VCENTER);
		}
	}
	label(app, d->geoLabel[0], Ui_Rect(x, y + 30, 44, 28));
	label(app, d->geoLabel[1], Ui_Rect(x + 150, y + 30, 44, 28));
	label(app, d->geoLabel[2], Ui_Rect(x, y + 66, 44, 28));
	label(app, d->geoLabel[3], Ui_Rect(x + 150, y + 66, 44, 28));
	label(app, "旋转", Ui_Rect(x, y + 102, 44, 28));
	label(app, "线宽", Ui_Rect(x + 150, y + 102, 44, 28));
	label(app, "透明", Ui_Rect(x, y + 138, 44, 28));
	label(app, "渐变角", Ui_Rect(x + 150, y + 138, 44, 28));
	UiInput_SetRect(&d->form, d->fX, Ui_Rect(fx1, y + 30, 92, 28));
	UiInput_SetRect(&d->form, d->fY, Ui_Rect(fx2, y + 30, 86, 28));
	UiInput_SetRect(&d->form, d->fW, Ui_Rect(fx1, y + 66, 92, 28));
	UiInput_SetRect(&d->form, d->fH, Ui_Rect(fx2, y + 66, 86, 28));
	UiInput_SetRect(&d->form, d->fRot, Ui_Rect(fx1, y + 102, 92, 28));
	UiInput_SetRect(&d->form, d->fStroke, Ui_Rect(fx2, y + 102, 86, 28));
	UiInput_SetRect(&d->form, d->fOpacity, Ui_Rect(fx1, y + 138, 92, 28));
	UiInput_SetRect(&d->form, d->fAngle, Ui_Rect(fx2, y + 138, 86, 28));
	UiInput_Draw(&d->form, d->fX);
	UiInput_Draw(&d->form, d->fY);
	UiInput_Draw(&d->form, d->fW);
	UiInput_Draw(&d->form, d->fH);
	UiInput_Draw(&d->form, d->fRot);
	UiInput_Draw(&d->form, d->fStroke);
	UiInput_Draw(&d->form, d->fOpacity);
	UiInput_Draw(&d->form, d->fAngle);

	{
		static const char *al[6] = { "左", "横中", "右", "上", "纵中", "下" };
		int i;
		UiButtonStyle bs = btn_plain();
		label(app, "对齐", Ui_Rect(x, y + 178, 44, 26));
		for (i = 0; i < 6; i++) {
			if (Ui_ButtonEx(ui, Ui_Id("svgedit.align", i, 0),
				Ui_Rect(x + 50 + (float)i * 37, y + 176, 34, 28),
				al[i], &bs, 1))
				Ui_SvgEdit_Align(d->ed, ui, i);
		}
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.distx", 0, 0),
			Ui_Rect(x, y + 212, 128, 28), "水平等距", &bs, 1))
			Ui_SvgEdit_Align(d->ed, ui, 6);
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.disty", 0, 0),
			Ui_Rect(x + 136, y + 212, 128, 28), "垂直等距", &bs, 1))
			Ui_SvgEdit_Align(d->ed, ui, 7);
	}
}

static void inspector_layers_tab(EdDemo *d, UiContext *ui, UiRect panel)
{
	UiApp *app = d->app;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiButtonStyle plain = btn_plain();
	UiSvgLayerRow rows[160];
	int n, shown, k, sel = Ui_SvgEdit_SelIndex(d->ed, 0);
	float x = panel.x + 14, y = panel.y + 72;
	float listY = y + 36;
	float listH = panel.h - 72 - 36 - 122;
	char buf[80];

	n = Ui_SvgEdit_Layers(d->ed, rows, 160);
	shown = (int)(listH / 24.0f);
	if (shown > n) shown = n;
	sel = Ui_SvgEdit_SelIndex(d->ed, 0);

	/* 重命名框 */
	if (Ui_SvgEdit_SelCount(d->ed) == 1 && sel >= 0 &&
		sel < d->doc->count) {
		if (d->nameSyncIdx != sel && UiInput_Focused(&d->form) < 0) {
			Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fName),
				d->doc->els[sel].name);
			d->nameSyncIdx = sel;
		}
		label(app, "名称", Ui_Rect(x, y, 44, 28));
		UiInput_SetRect(&d->form, d->fName, Ui_Rect(x + 50, y, panel.w - 78, 28));
		UiInput_Draw(&d->form, d->fName);
		{
			char nm[48];
			Edit_TextUtf8(UiInput_Edit(&d->form, d->fName), nm,
				(int)sizeof(nm));
			if (lstrcmpA(nm, d->doc->els[sel].name) != 0)
				Ui_SvgEdit_Rename(d->ed, sel, nm);
		}
	} else {
		label(app, "选中单个元素可重命名",
			Ui_Rect(x, y, panel.w - 28, 28));
	}

	Gfx_FillRoundRect(g, Ui_Rect(x, listY, panel.w - 28, listH), 8.0f,
		t->card);
	Gfx_StrokeRoundRect(g, Ui_Rect(x, listY, panel.w - 28, listH), 8.0f,
		t->border, 1.0f);
	for (k = 0; k < shown; k++) {
		UiSvgLayerRow *r = &rows[k];
		UiRect rr = Ui_Rect(x + 4, listY + 4 + (float)k * 24.0f,
			panel.w - 36, 22);
		UiWidgetState st = Ui_State(ui,
			Ui_IdIndex("svgedit.layer.row", k, 0, r->uid), rr, 1);
		UiId eyeId = Ui_IdIndex("svgedit.layer.eye", k, 0, r->uid);
		UiId lockId = Ui_IdIndex("svgedit.layer.lock", k, 0, r->uid);
		int isSel = r->index == sel;
		if (isSel)
			Gfx_FillRoundRect(g, rr, 6.0f, t->primary.soft);
		else if (st.hovered)
			Gfx_FillRoundRect(g, rr, 6.0f,
				Ui_ColorAlpha(t->text3, 24));
		if (Ui_ButtonEx(ui, eyeId,
			Ui_Rect(rr.x + 2, rr.y + 2, 22, 18),
			r->visible ? "显" : "隐", &plain, 1))
			Ui_SvgEdit_SetVisible(d->ed, r->index, !r->visible);
		if (Ui_ButtonEx(ui, lockId,
			Ui_Rect(rr.x + 26, rr.y + 2, 22, 18),
			r->locked ? "锁" : "开", &plain, 1))
			Ui_SvgEdit_SetLocked(d->ed, r->index, !r->locked);
		wsprintfA(buf, "%s%s", r->isGroup ? "▸ " : "",
			r->name[0] ? r->name : (r->isGroup ? "组" : "元素"));
		Gfx_Text(g, r->isGroup ? t->fBtn : t->fSmall, buf,
			Ui_Rect(rr.x + 56 + (float)r->depth * 12.0f, rr.y,
				rr.w - 60 - (float)r->depth * 12.0f, rr.h),
			r->visible ? t->text2 : t->text4,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		if (st.hovered) Ui_SetCursor(ui,
			Ui_IdIndex("svgedit.layer.row", k, 0, r->uid), UI_CURSOR_HAND);
		if (ui->clicked == Ui_IdIndex("svgedit.layer.row", k, 0, r->uid))
			Ui_SvgEdit_SelectIndex(d->ed, r->index);
	}
	if (n > shown)
		label(app, "列表过长，缩放窗口或折叠组查看",
			Ui_Rect(x, listY + listH + 2, panel.w - 28, 16));

	/* 操作按钮 (三行) */
	{
		float by = panel.y + panel.h - 108;
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.up", 0, 0),
			Ui_Rect(x, by, 62, 28), "上移", &plain, 1))
			Ui_SvgEdit_Reorder(d->ed, ui, 1);
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.down", 0, 0),
			Ui_Rect(x + 70, by, 62, 28), "下移", &plain, 1))
			Ui_SvgEdit_Reorder(d->ed, ui, -1);
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.out", 0, 0),
			Ui_Rect(x + 140, by, 92, 28), "移出组", &plain,
			Ui_SvgEdit_SelCount(d->ed) == 1)) {
			Ui_SvgEdit_MoveOut(d->ed, ui);
			d->nameSyncIdx = -2;
		}
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.group", 0, 0),
			Ui_Rect(x, by + 34, 78, 28), "组合", &plain, 1)) {
			Ui_SvgEdit_Group(d->ed, ui);
			d->nameSyncIdx = -2;
		}
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.ungroup", 0, 0),
			Ui_Rect(x + 86, by + 34, 78, 28), "解组", &plain, 1)) {
			Ui_SvgEdit_Ungroup(d->ed, ui);
			d->nameSyncIdx = -2;
		}
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.dup", 0, 0),
			Ui_Rect(x + 172, by + 34, 62, 28), "复制", &plain, 1))
			Ui_SvgEdit_DuplicateSel(d->ed, ui);
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.del", 0, 0),
			Ui_Rect(x, by + 68, 62, 28), "删除", &plain, 1))
			Ui_SvgEdit_DeleteSel(d->ed, ui);
	}
	(void)plain;
}

static void inspector_text_tab(EdDemo *d, UiContext *ui, UiRect panel)
{
	UiApp *app = d->app;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiButtonStyle plain = btn_plain();
	int idx = Ui_SvgEdit_TextTarget(d->ed);
	SvgElement *e = NULL;
	float x = panel.x + 14, y = panel.y + 72;
	char buf[300];
	char nm[64];

	if (idx < 0 || idx >= d->doc->count) {
		label(app, "选中文本元素后可编辑 (工具: T/文本)",
			Ui_Rect(x, y, panel.w - 28, 24));
		return;
	}
	e = &d->doc->els[idx];
	if (d->textSyncIdx != idx && UiInput_Focused(&d->form) < 0) {
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fText),
			e->text ? e->text : "");
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fFamily), e->family);
		num_f1(buf, e->fontSize);
		Edit_SetTextUtf8(UiInput_Edit(&d->form, d->fSize), buf);
		d->textSyncIdx = idx;
	}

	label(app, "内容", Ui_Rect(x, y, 44, 24));
	UiInput_SetRect(&d->form, d->fText, Ui_Rect(x + 50, y, panel.w - 78, 28));
	UiInput_Draw(&d->form, d->fText);
	Edit_TextUtf8(UiInput_Edit(&d->form, d->fText), buf, (int)sizeof(buf));
	if (lstrcmpA(buf, e->text ? e->text : "") != 0)
		Ui_SvgEdit_SetTextContent(d->ed, ui, buf);

	label(app, "字体", Ui_Rect(x, y + 40, 44, 24));
	UiInput_SetRect(&d->form, d->fFamily, Ui_Rect(x + 50, y + 38,
		panel.w - 78, 28));
	UiInput_Draw(&d->form, d->fFamily);
	Edit_TextUtf8(UiInput_Edit(&d->form, d->fFamily), nm, (int)sizeof(nm));
	label(app, "字号", Ui_Rect(x, y + 76, 44, 24));
	UiInput_SetRect(&d->form, d->fSize, Ui_Rect(x + 50, y + 74, 70, 28));
	UiInput_Draw(&d->form, d->fSize);
	Edit_TextUtf8(UiInput_Edit(&d->form, d->fSize), buf, 16);
	{
		float size = e->fontSize;
		float ns;
		if (parse_f(buf, &ns) && ns > 0.0f) size = ns;
		if (lstrcmpA(nm, e->family) != 0 || size != e->fontSize)
			Ui_SvgEdit_SetTextFont(d->ed, ui, nm, size, e->fontWeight);
	}
	wsprintfA(buf, "字重: %d", e->fontWeight);
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.text.weight", 0, 0),
		Ui_Rect(x + 130, y + 74, 96, 28), buf, &plain, 1))
		Ui_SvgEdit_SetTextFont(d->ed, ui, e->family, e->fontSize,
			e->fontWeight >= 600 ? 400 : 700);

	{
		static const char *al[3] = { "左", "中", "右" };
		static const char *an[3] = { "起", "中", "止" };
		int i;
		label(app, "对齐", Ui_Rect(x, y + 112, 44, 24));
		for (i = 0; i < 3; i++) {
			UiButtonStyle bs = btn_active(e->align == i);
			if (Ui_ButtonEx(ui, Ui_Id("svgedit.text.align", i, 0),
				Ui_Rect(x + 50 + (float)i * 40, y + 110, 34, 26),
				al[i], &bs, 1))
				Ui_SvgEdit_SetTextAlign(d->ed, ui, i, e->anchor);
		}
		label(app, "锚点", Ui_Rect(x, y + 148, 44, 24));
		for (i = 0; i < 3; i++) {
			UiButtonStyle bs = btn_active(e->anchor == i);
			if (Ui_ButtonEx(ui, Ui_Id("svgedit.text.anchor", i, 0),
				Ui_Rect(x + 50 + (float)i * 40, y + 146, 34, 26),
				an[i], &bs, 1))
				Ui_SvgEdit_SetTextAlign(d->ed, ui, e->align, i);
		}
	}
	{
		UiButtonStyle bs = btn_plain();
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.text.topath", 0, 0),
			Ui_Rect(x, y + 186, 96, 28), "转路径", &bs, 1))
			if (Ui_SvgEdit_TextToPath(d->ed, ui))
				return; /* 元素已变为路径, 本帧不再编辑文本字段 */
	}
	Gfx_Text(g, t->fSmall,
		"新建: 选文本工具后在画布点击\n"
		"多行: 模型/导出支持 \\n, 输入框暂为单行",
		Ui_Rect(x, y + 226, panel.w - 28, 48), t->text3, GFX_TEXT_WRAP);
}

static void inspector2(EdDemo *d, UiContext *ui, UiRect panel)
{
	UiApp *app = d->app;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	float tw;
	int sidx = Ui_SvgEdit_SelIndex(d->ed, 0);
	int scnt = Ui_SvgEdit_SelCount(d->ed);
	cs_sync(d);
	if (sidx != d->lastSelIdx || scnt != d->lastSelCount) {
		d->lastSelIdx = sidx;
		d->lastSelCount = scnt;
		if (scnt == 1 && sidx >= 0 && sidx < d->doc->count &&
			d->doc->els[sidx].type == SVG_EL_TEXT)
			d->inspTab = 2;
		else if (d->inspTab == 2)
			d->inspTab = 0;
		d->nameSyncIdx = -2;
		d->textSyncIdx = -2;
	}
	Ui_Panel(ui, panel);
	Gfx_Text(g, t->fHead, "检查器", Ui_Rect(panel.x + 14, panel.y + 8,
		panel.w - 28, 24), t->text, GFX_TEXT_VCENTER);
	tw = (panel.w - 28 - 18) / 4.0f;
	insp_tab_btn(d, ui, Ui_Rect(panel.x + 14, panel.y + 34, tw, 28), "样式", 0);
	insp_tab_btn(d, ui, Ui_Rect(panel.x + 14 + tw + 6, panel.y + 34, tw, 28),
		"几何", 1);
	insp_tab_btn(d, ui, Ui_Rect(panel.x + 14 + (tw + 6) * 2, panel.y + 34,
		tw, 28), "文本", 2);
	insp_tab_btn(d, ui, Ui_Rect(panel.x + 14 + (tw + 6) * 3, panel.y + 34,
		tw, 28), "图层", 3);
	if (d->inspTab == 0) inspector_style_tab(d, ui, panel);
	else if (d->inspTab == 1) inspector_geom_tab(d, ui, panel);
	else if (d->inspTab == 2) inspector_text_tab(d, ui, panel);
	else inspector_layers_tab(d, ui, panel);
}

/* ── 帧 ───────────────────────────────────────────────────────────── */

static void ctx_menu(EdDemo *d, UiApp *app, UiContext *ui)
{
	UiButtonStyle plain = btn_plain();
	float w = 140.0f, h = 8 * 30.0f + 8.0f;
	float x = d->ctxX, y = d->ctxY;
	int i;
	if (!d->ctxOpen) return;
	if (x + w > app->w - 8.0f) x = app->w - w - 8.0f;
	if (y + h > app->h - 8.0f) y = app->h - h - 8.0f;
	if (x < 8.0f) x = 8.0f;
	if (y < 8.0f) y = 8.0f;
	d->ctxRc = Ui_Rect(x, y, w, h);
	Ui_Panel(ui, d->ctxRc);
	{
		static const char *items[8] = {
			"复制", "粘贴", "副本", "删除", "置顶", "置底", "组合", "解组"
		};
		for (i = 0; i < 8; i++) {
			if (!Ui_ButtonEx(ui, Ui_Id("svgedit.ctx", i, 0),
				Ui_Rect(x + 4, y + 4 + (float)i * 30, w - 8, 28),
				items[i], &plain, 1))
				continue;
			switch (i) {
			case 0: Ui_SvgEdit_CopySel(d->ed); break;
			case 1: Ui_SvgEdit_Paste(d->ed, ui); break;
			case 2: Ui_SvgEdit_DuplicateSel(d->ed, ui); break;
			case 3: Ui_SvgEdit_DeleteSel(d->ed, ui); break;
			case 4: Ui_SvgEdit_Reorder(d->ed, ui, 1); break;
			case 5: Ui_SvgEdit_Reorder(d->ed, ui, -1); break;
			case 6: Ui_SvgEdit_Group(d->ed, ui); break;
			case 7: Ui_SvgEdit_Ungroup(d->ed, ui); break;
			default: break;
			}
			d->ctxOpen = 0;
			UiApp_Invalidate(app);
			return;
		}
	}
}

static void on_frame(UiApp *app, void *user)
{
	EdDemo *d = (EdDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiButtonStyle plain = btn_plain();
	float top = 56.0f, bar = 44.0f, statusH = 30.0f;
	float insW = 300.0f;
	char buf[160];
	int zi;
	float zoom;

	UiPopupManager_BeginFrame(&d->pm, ui);
	sync_fields(d);

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h),
		t->windowRadius, t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1, app->h - 1),
		t->windowRadius - 0.5f, t->frame, 1.0f);

	/* 顶栏 */
	Gfx_Text(g, t->fBig, "CUI SVG Editor", Ui_Rect(24, 8, app->w - 420, 30),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "缩放 · 钢笔/圆弧 · 节点编辑 · 渐变 · Undo/Redo · 导出",
		Ui_Rect(26, 34, app->w - 420, 16), t->text3, GFX_TEXT_VCENTER);
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.export", 0, 0),
		Ui_Rect(app->w - 300, 12, 96, 32), "导出 SVG", &plain, 1))
		d->wantExport = 1;
	if (Ui_ButtonEx(ui, Ui_Id("svgedit.copy", 0, 0),
		Ui_Rect(app->w - 196, 12, 82, 32), "复制", &plain, 1))
		d->wantCopy = 1;
	{
		UiButtonStyle th = plain;
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.theme", 0, 0),
			Ui_Rect(app->w - 104, 12, 80, 32),
			t->themeId == UI_THEME_DARK ? "浅色" : "深色", &th, 1)) {
			UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
				? UI_THEME_LIGHT : UI_THEME_DARK);
			UiApp_Invalidate(app);
		}
	}

	/* 顶栏缩放 (工具已移到左侧栏) */
	{
		float bx = app->w - insW - 16.0f - 260.0f, by = 12.0f;
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.zoomout", 0, 0),
			Ui_Rect(bx, by, 34, 32), "-", &plain, 1))
			Ui_SvgEdit_ZoomAt(d->ed, 0.8f, app->w * 0.5f,
				app->h * 0.5f);
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.zoomin", 0, 0),
			Ui_Rect(bx + 38, by, 34, 32), "+", &plain, 1))
			Ui_SvgEdit_ZoomAt(d->ed, 1.25f, app->w * 0.5f,
				app->h * 0.5f);
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.fit", 0, 0),
			Ui_Rect(bx + 76, by, 52, 32), "适应", &plain, 1))
			Ui_SvgEdit_ZoomFit(d->ed, d->canvasRc);
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.one", 0, 0),
			Ui_Rect(bx + 132, by, 52, 32), "100%", &plain, 1))
			Ui_SvgEdit_ZoomReset(d->ed, d->canvasRc);
	}

	/* 画布区域先算, 左栏高度跟随 */
	d->canvasRc = Ui_Rect(84.0f, top + 8.0f,
		app->w - 84.0f - insW - 16.0f - 12.0f,
		app->h - (top + 8.0f) - statusH - 12.0f);
	if (d->canvasRc.w < ED_CANVAS_MIN_W) d->canvasRc.w = ED_CANVAS_MIN_W;

	/* 左侧工具栏 (三栏第一栏) */
	{
		UiRect rail = Ui_Rect(12.0f, top + 8.0f, 60.0f, d->canvasRc.h);
		float bx = rail.x + 8, by = rail.y + 8;
		Ui_Panel(ui, rail);
		tool_button(d, ui, Ui_Rect(bx, by, 44, 30), "选择", UI_SVG_TOOL_SELECT);
		tool_button(d, ui, Ui_Rect(bx, by + 34, 44, 30), "节点", UI_SVG_TOOL_NODE);
		tool_button(d, ui, Ui_Rect(bx, by + 68, 44, 30), "直线", UI_SVG_TOOL_LINE);
		tool_button(d, ui, Ui_Rect(bx, by + 102, 44, 30), "矩形", UI_SVG_TOOL_RECT);
		tool_button(d, ui, Ui_Rect(bx, by + 136, 44, 30), "椭圆", UI_SVG_TOOL_ELLIPSE);
		tool_button(d, ui, Ui_Rect(bx, by + 170, 44, 30), "多边", UI_SVG_TOOL_POLYGON);
		tool_button(d, ui, Ui_Rect(bx, by + 204, 44, 30), "钢笔", UI_SVG_TOOL_PEN);
		tool_button(d, ui, Ui_Rect(bx, by + 238, 44, 30), "贝塞", UI_SVG_TOOL_BEZIER);
		tool_button(d, ui, Ui_Rect(bx, by + 272, 44, 30), "文本", UI_SVG_TOOL_TEXT);
		by += 312;
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.undo", 0, 0),
			Ui_Rect(bx, by, 44, 30), "撤销", &plain,
			Ui_SvgEdit_CanUndo(d->ed))) {
			Ui_SvgEdit_Undo(d->ed, ui);
			UiApp_Invalidate(app);
		}
		if (Ui_ButtonEx(ui, Ui_Id("svgedit.redo", 0, 0),
			Ui_Rect(bx, by + 34, 44, 30), "重做", &plain,
			Ui_SvgEdit_CanRedo(d->ed))) {
			Ui_SvgEdit_Redo(d->ed, ui);
			UiApp_Invalidate(app);
		}
		{
			UiButtonStyle gs = btn_active(Ui_SvgEdit_GridOn(d->ed));
			if (Ui_ButtonEx(ui, Ui_Id("svgedit.grid", 0, 0),
				Ui_Rect(bx, by + 68, 44, 30), "网格", &gs, 1))
				Ui_SvgEdit_SetGrid(d->ed, !Ui_SvgEdit_GridOn(d->ed), 8.0f);
		}
	}

	Ui_SvgEdit_SetInputBlock(d->ed,
		d->ctxOpen || UiPopupManager_AnyOpen(&d->pm));
	Ui_SvgEdit_Frame(d->ed, ui, d->canvasRc);

	inspector2(d, ui, Ui_Rect(app->w - insW - 16.0f, top + 8.0f,
		insW, d->canvasRc.h));

	/* 状态栏 */
	d->statusRc = Ui_Rect(16.0f, app->h - statusH, app->w - 32.0f, statusH - 4.0f);
	Ui_Panel(ui, d->statusRc);
	{
		const char *hint = Ui_SvgEdit_Hint(d->ed);
		DWORD now = GetTickCount();
		if (d->msg[0] && now - d->msgTick < 3500)
			hint = d->msg;
		Gfx_Text(g, t->fSmall, hint,
			Ui_Rect(d->statusRc.x + 10, d->statusRc.y,
				d->statusRc.w - 380, d->statusRc.h),
			t->text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	zoom = Ui_SvgEdit_Zoom(d->ed) * 100.0f;
	zi = (int)(zoom + 0.5f);
	{
		float mwx, mwy;
		int mx = 0, my = 0;
		if (Ui_SvgEdit_MouseWorld(d->ed, &mwx, &mwy)) {
			mx = (int)(mwx >= 0 ? mwx + 0.5f : mwx - 0.5f);
			my = (int)(mwy >= 0 ? mwy + 0.5f : mwy - 0.5f);
		}
		{
			char zi_s[16], mx_s[16], my_s[16], cnt_s[16], sel_s[16];
			num_i(zi_s, zi);
			num_i(mx_s, mx);
			num_i(my_s, my);
			num_i(cnt_s, d->doc->count);
			num_i(sel_s, Ui_SvgEdit_SelCount(d->ed));
			wsprintfA(buf, "缩放 %s%%  ·  %s, %s  ·  元素 %s  ·  选中 %s",
				zi_s, mx_s, my_s, cnt_s, sel_s);
		}
	}
	Gfx_Text(g, t->fSmall, buf,
		Ui_Rect(d->statusRc.x + d->statusRc.w - 360, d->statusRc.y,
			350, d->statusRc.h), t->text3,
		GFX_TEXT_RIGHT | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	ctx_menu(d, app, ui);
	UiColorSelect_DrawPopup(&d->csFill, &d->pm, ui);
	UiColorSelect_DrawPopup(&d->csStroke, &d->pm, ui);
}

static void on_after_frame(UiApp *app, void *user)
{
	EdDemo *d = (EdDemo *)user;
	if (d->csFill.committed || d->csStroke.committed) {
		d->csFill.committed = 0;
		d->csStroke.committed = 0;
		Ui_SvgEdit_Commit(d->ed);
	}
	UiColorSelect_EndFrame(&d->csFill, UiFrame_Now());
	UiColorSelect_EndFrame(&d->csStroke, UiFrame_Now());
	if (d->wantExport) {
		d->wantExport = 0;
		save_svg(d);
		UiApp_Invalidate(app);
	}
	if (d->wantCopy) {
		d->wantCopy = 0;
		copy_svg(d);
		UiApp_Invalidate(app);
	}
}

/* ── 输入 ─────────────────────────────────────────────────────────── */

static void on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
	EdDemo *d = (EdDemo *)user;
	{
		int used;
		used = UiColorSelect_Mouse(&d->csFill, &app->ui, msg, x, y);
		used |= UiColorSelect_Mouse(&d->csStroke, &app->ui, msg, x, y);
		if (used) {
			double now = UiFrame_Now();
			UiColorSelect_EndFrame(&d->csFill, now);
			UiColorSelect_EndFrame(&d->csStroke, now);
		}
	}
	if (msg == UI_APP_MOUSE_DOWN) {
		if (d->ctxOpen) {
			UiRect r = d->ctxRc;
			if (x < r.x || y < r.y || x > r.x + r.w || y > r.y + r.h)
				d->ctxOpen = 0;
		}
		UiInput_MouseDown(&d->form, Ui_HitTest(&app->ui, x, y), x);
	} else if (msg == UI_APP_MOUSE_MOVE) {
		UiInput_MouseMove(&d->form, x);
		UiApp_InvalidateRect(app, d->statusRc, 2.0f);
	} else if (msg == UI_APP_MOUSE_UP) {
		UiInput_MouseUp(&d->form);
	} else if (msg == UI_APP_MOUSE_RDOWN) {
		Ui_SvgEdit_RightDown(d->ed, x, y);
		if (Ui_SvgEdit_SelCount(d->ed) > 0) {
			d->ctxOpen = 1;
			d->ctxX = x;
			d->ctxY = y;
			UiApp_Invalidate(app);
		}
	}
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	EdDemo *d = (EdDemo *)user;
	int focused = UiInput_Focused(&d->form) >= 0;
	if (d->ctxOpen && vk == VK_ESCAPE) {
		d->ctxOpen = 0;
		UiApp_Invalidate(app);
		return 1;
	}
	{
		int csOpen = UiColorSelect_IsOpen(&d->pm, &d->csFill) ||
			UiColorSelect_IsOpen(&d->pm, &d->csStroke);
		if (UiColorSelect_Key(&d->csFill, &d->pm, &app->ui, vk) ||
			UiColorSelect_Key(&d->csStroke, &d->pm, &app->ui, vk)) {
			UiApp_Invalidate(app);
			return 1;
		}
		if (csOpen) return 1;   /* 面板打开时屏蔽编辑器快捷键 */
	}
	if (UiInput_Key(&d->form, app->hwnd, vk)) {
		UiApp_Invalidate(app);
		return 1;
	}
	if (focused && vk == VK_RETURN) {
		apply_fields(d, app);
		return 1;
	}
	if (!focused && Ui_SvgEdit_Key(d->ed, &app->ui, vk)) {
		UiApp_Invalidate(app);
		return 1;
	}
	if ((GetKeyState(VK_CONTROL) & 0x8000) && vk == 'S') {
		d->wantExport = 1;
		UiApp_Invalidate(app);
		return 1;
	}
	if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)
		&& vk == 'C') {
		d->wantCopy = 1;
		UiApp_Invalidate(app);
		return 1;
	}
	if (focused) return 1;
	return 0;
}

static int on_char(UiApp *app, wchar_t ch, void *user)
{
	EdDemo *d = (EdDemo *)user;
	(void)app;
	if (UiColorSelect_Char(&d->csFill, &app->ui, ch)) return 1;
	if (UiColorSelect_Char(&d->csStroke, &app->ui, ch)) return 1;
	return UiInput_Char(&d->form, ch);
}

static void on_ime(UiApp *app, void *user)
{
	EdDemo *d = (EdDemo *)user;
	(void)app;
	UiColorSelect_OnIme(&d->csFill, &app->ui);
	UiColorSelect_OnIme(&d->csStroke, &app->ui);
	UiInput_OnIme(&d->form);
}

static void on_timer(UiApp *app, void *user)
{
	EdDemo *d = (EdDemo *)user;
	(void)app;
	UiColorSelect_Blink(&d->csFill, &d->app->ui);
	UiColorSelect_Blink(&d->csStroke, &d->app->ui);
	UiInput_Blink(&d->form);
	if (d->msg[0] && GetTickCount() - d->msgTick >= 3500)
		d->msg[0] = 0;
}

static int on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	(void)user;
	if (y < (int)(56.0f * app->scale)) {
		float dx = (float)x / app->scale;
		/* 右上角按钮区必须留在客户区, 否则点击会被当成拖窗. */
		if (dx < app->w - 304.0f || dx > app->w - 12.0f) {
			*hit = HTCAPTION;
			return 1;
		}
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.after_frame = on_after_frame;
	cb.mouse = on_mouse;
	cb.key = on_key;
	cb.chr = on_char;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.nchittest = on_nchittest;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI SVG Editor", 1280, 860, UI_THEME_LIGHT, &cb,
		&g_demo);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(),
		SW_SHOWDEFAULT));
}
#endif
