/*
 * gallery.c - CUI 组件画廊 (综合演示)
 *
 * 左侧导航栏列出全部组件; 每个组件六个标签页:
 *   演示 / 属性(可改参数实时预览 + 实时生成创建代码) /
 *   说明文档 / 帮助 API / 事件(接线文档 + 实时事件日志) / 最佳实践
 *
 * 文档用内置 UiMarkdown 组件渲染 (samples/gallery_docs.c)。
 * 窗口/消息/DPI/IME/帧循环全部来自 UiApp 壳; 侧边导航是
 * "ScrollView + Ui_State" 的自绘控件示范。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/ui_widgets.h"
#include "ui/ui_controls.h"
#include "ui/ui_button_group.h"
#include "ui/ui_icon.h"
#include "ui/ui_input.h"
#include "ui/ui_textarea.h"
#include "ui/ui_select.h"
#include "ui/ui_menu.h"
#include "ui/ui_dialog.h"
#include "ui/ui_datepicker.h"
#include "ui/ui_datetime.h"
#include "ui/ui_tab.h"
#include "ui/ui_nav.h"
#include "ui/ui_scroll.h"
#include "ui/ui_pagination.h"
#include "ui/ui_tooltip.h"
#include "ui/ui_toast.h"
#include "ui/ui_progress.h"
#include "ui/ui_popup.h"
#include "ui/ui_image.h"
#include "ui/ui_md.h"
#include "net/ui_net.h"
#include "net/ui_ws_server.h"
#include "edit.h"
#include "gallery_docs.h"
#include "gallery_icons.h"

/* crtstub(0CRT) 提供 snprintf(支持 %f); wsprintfA 不支持浮点格式 */
int snprintf(char *buf, size_t cap, const char *fmt, ...);

/* ── 布局常量 ──────────────────────────────────────────────────────── */

#define TOP_H    46.0f
#define SIDE_X   12.0f
#define SIDE_W   210.0f
#define SIDE_GAP 14.0f
#define SIDE_ROW 30.0f
#define SIDE_SEC 30.0f
#define STATUS_H 26.0f

#define FIXED_ROWS 1000
#define FIXED_ROW_H 34.0f
#define VAR_CARD_N  12

/* ── 页面表 ────────────────────────────────────────────────────────── */

typedef struct Gallery Gallery;

typedef struct GalleryPage {
	const char *label;
	const char *title;
	const char *subtitle;
	void (*init)(Gallery *g);
	void (*demo)(Gallery *g, UiRect rc);
	void (*props)(Gallery *g, UiRect rc);
	int  (*key)(Gallery *g, UINT vk);
	int  (*chr)(Gallery *g, wchar_t ch);
	void (*ime)(Gallery *g);
	void (*blink)(Gallery *g);
	void (*mouse)(Gallery *g, int msg, float x, float y);
	void (*tick)(Gallery *g, double dtMs);
	void (*popups)(Gallery *g);
	int  (*animating)(Gallery *g);
} GalleryPage;

/* 页面表 (定义在文件末尾; draw_sidebar/draw_content 提前引用) */
static const GalleryPage kPages[PG_COUNT];

/* 命令行直跳页 (cui_gallery.exe wssrv); WinMain 解析后赋值 */
static int g_startPage = PG_WELCOME;
static int g_wsAuto = 0;          /* wssrv 页自动启动监听 (联调用) */

/* ── 全局状态 ──────────────────────────────────────────────────────── */

typedef struct ScrollCard {
	const char *const *lines;
	int nbase;
	int expanded;
	uint32_t rev;
} ScrollCard;

typedef struct LayoutCard {
	const char *label;
	const char *sub;
	float minW;
	float prefH;
} LayoutCard;

typedef struct Gallery {
	UiApp *app;
	UiPopupManager pm;
	UiToast toast;
	UiTooltip tip;
	UiMarkdown *md;          /* 文档标签页共享 */
	UiNet *net;
	UiTabBar subTabs;
	UiTabStyle pill;
	UiScrollView sideSv;
	int page;
	int subTab[PG_COUNT];
	int docKey;
	int pendingToastHide;
	double lastNow;
	char status[256];

	GfxFont *fCode;          /* Consolas 代码字体 (创建失败回退 fSmall) */
	UiRect codeRc;           /* 属性页右下角: 实时生成的创建代码面板 */
	char codeText[896];      /* 属性页生成的创建代码 (props 每帧刷新) */
	struct {
		char lines[10][96];  /* 环形事件日志: 最新在最上 */
		int count;
		int total;
	} ev;                   /* 全局事件日志: set_status 自动入列 */

	/* Button */
	struct {
		UiInput form;
		int fld;
		UiButtonGroup type, appear, shape;
		UiCheckbox dashed, disabled, icon;
	} button;

	/* ButtonGroup */
	struct {
		UiButtonGroup demoN, demoS, demoM;
		UiButtonGroup pv, mode, count, variant;
		UiCheckbox disMid;
		int pvMode, pvCount;
	} bgroup;

	/* Icon */
	struct {
		UiScrollView sv;        /* glyph 全集虚拟滚动 */
		UiSlider size;
		UiButtonGroup tint, zone;  /* tint: 属性页着色; zone: 演示页 PUA 分区 */
		UiCheckbox disabled;
		UiInput form;           /* 演示页搜索框 */
		int search;
		unsigned short vis[GALLERY_ICON_MAX]; /* 过滤结果: kMdl2Icons 下标 */
		int visN;
		int sel;                /* 选中 glyph (kMdl2Icons 下标), -1 = 无 */
		char lastQuery[48];
		int lastZone;
	} icon;

	/* Input */
	struct {
		UiInput form;
		int name, pass, pv;
		UiSlider maxLen;
		UiCheckbox pwd, allow;
	} input;

	/* TextArea */
	struct {
		UiTextArea ta;
		UiCheckbox ro;
		char *buf;
		int len, cap;
	} ta;

	/* Checkbox */
	struct {
		UiCheckbox a, b, c, pv;
		UiCheckboxGroup grp;
		UiInput form;
		int fld;
		UiCheckbox dis, ind;
	} checks;

	/* Radio */
	struct {
		UiRadioGroup rg, pv;
		UiCheckbox dis1, dis2;
	} radio;

	/* Switch */
	struct {
		UiSwitch s1, s2, pv;
		UiCheckbox dis;
	} sw;

	/* Slider */
	struct {
		UiSlider sl;
	} slider;

	/* Select */
	struct {
		UiSelect single, multi, filter, pv;
		UiButtonGroup cnt;
		UiCheckbox multiCb, filterCb, dis0;
		int pvCfg;
	} sel;

	/* DatePicker */
	struct {
		UiDatePicker dp, dt, rng;
		UiButtonGroup mode, weekStart;
		UiCheckbox seconds;
		int weekApplied;
	} dp;

	/* Tabs */
	struct {
		UiTabBar bar;
		UiTabPager pager;
		UiTabStyle line, pill, card;
		UiButtonGroup style, axis;
		UiCheckbox fade, reverse;
		UiSlider dur;
	} tabs;

	/* Nav */
	struct {
		UiNavBar nav;
		UiButtonGroup layout;
		UiCheckbox rail;
		UiNavItem roots[7], prod[3], res[2];
	} nav;

	/* Pagination */
	struct {
		UiPagination pg;
		UiButtonGroup size, total, slots;
		UiCheckbox firstLast;
	} pag;

	/* ScrollView */
	struct {
		UiScrollView fixedSv, varSv;
		UiScrollLayout fixedL, varL;
		UiScrollItem varItems[VAR_CARD_N];
		ScrollCard cards[VAR_CARD_N];
		UiButtonGroup preset;
		UiSlider wheelStep;
		UiCheckbox reserve;
		int first, last, drawn;
	} scroll;

	/* Layout (Stack 游标流 + 对齐) */
	struct {
		UiSlider headH;
		UiButtonGroup align;
	} layout;

	/* Flex (水平/垂直弹性) */
	struct {
		UiSlider gap, w2, w3, cross;
		UiButtonGroup dir;         /* 0=水平 ROW, 1=垂直 COLUMN */
	} flex;

	/* Grid */
	struct {
		UiSlider gap;
		UiCheckbox wide, tall, dense;
	} grid;

	/* Dialog */
	struct {
		UiDialog dlg, confirm;
		UiCheckbox notif;
		UiSelect cat;
		UiSlider w;
		UiCheckbox danger;
		UiId okId;
		UiId focus[3];
	} dlg;

	/* Menu */
	struct {
		UiMenu menu, ctx;
		UiCheckbox danger, disable;
		int applied;
	} menu;

	/* Toast 页 */
	struct {
		UiInput form;
		int fld;
		UiButtonGroup kind;
		UiSlider dur;
	} toastP;

	/* Tooltip 页 */
	struct {
		UiSlider delay;
	} tipP;

	/* Progress */
	struct {
		UiProgress prog;
		UiSlider sl;
		UiCheckbox paused;
	} prog;

	/* Image */
	struct {
		UiImage *img;
		UiButtonGroup fit;
		int anchor;
		UiCheckbox bg;
	} img;

	/* Markdown 页 */
	struct {
		UiMarkdown *md;
		UiSlider lineSpacing;
		int curDoc;
		float lastLS;
		uint32_t lastRev;
	} mdp;

	/* Theme */
	struct {
		UiButtonGroup preset;
		int applied;
	} theme;

	/* Net 页 (与顶层 UiNet *net 引擎句柄重名, 故叫 netp) */
	struct {
		UiInput form;
		int url;
		uint64_t req;
		int loading, code, len;
		char ctype[96];
		char preview[256];
		UiSlider timeout;
		int everyOn;
		uint64_t everyId;
		int ticks;
		int afterOn;
		uint64_t afterId;
	} netp;

	/* WebSocket 客户端页 */
	struct {
		UiInput form;
		int url, msg;
		void *ws;              /* UiWs 句柄; NULL=未连接 */
		int state;             /* 0=断开 1=已连接 2=重连中 */
		char log[2048];
		int logLen;
		int msgCount;
		UiCheckbox autoRe;
	} wsc;

	/* WebSocket 服务端页 */
	struct {
		UiInput form;
		int port, path, msg;
		UiWsServer *srv;
		int state;             /* UiWsServerState */
		int portShown;
		int conns, totalConns, msgCount;
		char log[2048];
		int logLen;
	} wss;

	/* App 框架页: 壳回调监视器 */
	struct {
		uint64_t frame, tick, anim, mouse, key, chr, ime, timer, after;
		int initDone;
		double lastFrameMs;
	} appf;
} Gallery;

static Gallery g_gl;

/* ── 小工具 ────────────────────────────────────────────────────────── */

/* 事件日志: 本应用把对组件事件的响应统一写入状态栏, 这里同步入列,
 * 「事件」标签页可看到实时的触发序列。 */
static void evlog(Gallery *g, const char *s)
{
	int i, n;
	if (!s || !s[0]) return;
	n = lstrlenA(s);
	if (n > 92) n = 92;
	if (g->ev.count == 10) {           /* 环形: 丢最旧 */
		for (i = 1; i < 10; i++)
			lstrcpynA(g->ev.lines[i - 1], g->ev.lines[i], 96);
		g->ev.count = 9;
	}
	lstrcpynA(g->ev.lines[g->ev.count], s, n + 1);
	g->ev.count++;
	g->ev.total++;
}

static void set_status(Gallery *g, const char *s)
{
	int i = 0;
	if (!s) return;
	while (s[i] && i < (int)sizeof(g->status) - 1) {
		g->status[i] = s[i];
		i++;
	}
	g->status[i] = 0;
	evlog(g, s);
	UiApp_Invalidate(g->app);
}

static float frame_dt(Gallery *g)
{
	double now = UiFrame_Now();
	float dt = g->lastNow > 0.0 ? (float)(now - g->lastNow) : 16.0f;
	g->lastNow = now;
	if (dt < 0.0f) dt = 0.0f;
	if (dt > 50.0f) dt = 50.0f;
	return dt;
}

static void lbl(Gfx *g, UiTheme *t, UiRect rc, const char *s)
{
	Gfx_Text(g, t->fLabel, s, rc, t->text2,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void small_txt(Gfx *g, UiTheme *t, UiRect rc, const char *s)
{
	Gfx_Text(g, t->fSmall, s, rc, t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void note_txt(Gfx *g, UiTheme *t, UiRect rc, const char *s)
{
	Gfx_Text(g, t->fBody, s, rc, t->text3, GFX_TEXT_WRAP);
}

static UiRect pane_in(UiRect p, float pad)
{
	return Ui_Rect(p.x + pad, p.y + pad, p.w - pad * 2.0f, p.h - pad * 2.0f);
}

/* 属性页: 左编辑列 + 右预览列; 右列再纵向分为 预览(上) + 实时代码(下)。
 * 窗口太矮时保持全高预览, 代码面板隐藏 (codeRc 置空)。 */
static void props_split(Gallery *g, UiContext *ui, UiRect rc, UiRect *ed, UiRect *pv)
{
	float w = 336.0f;
	float cdH;
	if (rc.w < 720.0f) w = rc.w * 0.46f;
	*ed = Ui_Rect(rc.x, rc.y, w, rc.h);
	*pv = Ui_Rect(rc.x + w + 14.0f, rc.y, rc.w - w - 14.0f, rc.h);
	g->codeRc = Ui_Rect(0, 0, 0, 0);
	if (rc.h >= 420.0f) {
		cdH = rc.h * 0.42f;
		if (cdH > 260.0f) cdH = 260.0f;
		if (cdH < 150.0f) cdH = 150.0f;
		pv->h = rc.h - cdH - 10.0f;
		g->codeRc = Ui_Rect(pv->x, pv->y + pv->h + 10.0f, pv->w, cdH);
	}
	Ui_Panel(ui, *ed);
	Ui_Panel(ui, *pv);
}

/* 属性页右下角: 实时生成的组件创建代码 (注释行浅色, Consolas) */
static void code_view(Gallery *g, UiRect rc, const char *code)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	GfxFont *f = g->fCode ? g->fCode : t->fSmall;
	int dark = t->themeId == UI_THEME_DARK;
	UiColor bg = dark ? 0xFF2B2F3A : 0xFF24262E;
	UiColor lineC = dark ? 0xFF454A58 : 0xFF3A3D46;
	UiColor fg = 0xFFD6DAE1;
	UiColor cm = 0xFF8B93A5;
	const char *p = code, *ls = code;
	float y;

	if (rc.w <= 0.0f || rc.h <= 0.0f || !code) return;
	Gfx_FillRoundRect(gfx, rc, 10.0f, bg);
	Gfx_StrokeRoundRect(gfx, rc, 10.0f, lineC, 1.0f);
	Gfx_FillRoundRect(gfx, Ui_Rect(rc.x + 14.0f, rc.y + 14.0f, 6.0f, 6.0f),
		3.0f, t->success.base);
	Gfx_Text(gfx, t->fMini, "创建代码 · 随左侧属性实时变化",
		Ui_Rect(rc.x + 26.0f, rc.y + 6.0f, rc.w - 40.0f, 18.0f),
		0xFF9DA2AE, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	y = rc.y + 30.0f;
	while (*p) {
		if (*p == '\n') {
			char tmp[168];
			const char *q = ls;
			int len, isComment;
			UiRect row;
			while (q < p && (*q == ' ' || *q == '\t')) q++;
			isComment = (q + 1 < p && q[0] == '/' &&
				(q[1] == '/' || q[1] == '*')) || (q < p && q[0] == '*');
			len = (int)(p - ls);
			if (len > 164) len = 164;
			lstrcpynA(tmp, ls, len + 1);
			row = Ui_Rect(rc.x + 14.0f, y, rc.w - 28.0f, 15.0f);
			if (row.y + row.h <= rc.y + rc.h - 4.0f)
				Gfx_Text(gfx, f, tmp, row, isComment ? cm : fg,
					GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			y += 15.0f;
			ls = p + 1;
		}
		p++;
	}
	if (p > ls && ls[0]) {                    /* 尾行 (无换行结尾) */
		char tmp[168];
		int len = (int)(p - ls);
		UiRect row = Ui_Rect(rc.x + 14.0f, y, rc.w - 28.0f, 15.0f);
		if (len > 164) len = 164;
		lstrcpynA(tmp, ls, len + 1);
		if (row.y + row.h <= rc.y + rc.h - 4.0f)
			Gfx_Text(gfx, f, tmp, row, fg,
				GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
}

/* 「事件」页底部: 实时事件日志 (组件事件 -> set_status -> 本日志) */
static void draw_event_log(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect list;
	char buf[64];
	int i, n;

	if (rc.w <= 0.0f || rc.h <= 0.0f) return;
	Gfx_FillRoundRect(gfx, rc, 10.0f, t->card);
	Gfx_StrokeRoundRect(gfx, rc, 10.0f, t->border, 1.0f);
	wsprintfA(buf, "实时事件日志 · 共 %d 条", g->ev.total);
	small_txt(gfx, t, Ui_Rect(rc.x + 14.0f, rc.y + 4.0f, rc.w - 120.0f, 26.0f), buf);
	if (Ui_Button(ui, Ui_Id("gl.ev.clear", 0, 0),
			Ui_Rect(rc.x + rc.w - 86.0f, rc.y + 8.0f, 72.0f, 24.0f),
			"清空", UI_BTN_SECONDARY, g->ev.count > 0)) {
		g->ev.count = 0;
		g->ev.total = 0;
		UiApp_Invalidate(app);
	}
	list = Ui_Rect(rc.x + 12.0f, rc.y + 34.0f, rc.w - 24.0f, rc.h - 42.0f);
	n = g->ev.count;
	if (!n) {
		note_txt(gfx, t, list,
			"还没有事件 —— 去「演示」页操作组件, 每次事件响应\n"
			"都会写入状态栏并出现在这里。清空后重新累积。");
		return;
	}
	for (i = 0; i < n; i++) {
		int idx = n - 1 - i;                   /* 最新在最上 */
		UiRect row = Ui_Rect(list.x, list.y + (float)i * 16.0f,
			list.w, 16.0f);
		if (row.y + row.h > list.y + list.h) break;
		Gfx_FillRoundRect(gfx, Ui_Rect(row.x, row.y + 6.0f, 4.0f, 4.0f),
			2.0f, i == 0 ? t->primary.base : t->text3);
		Gfx_Text(gfx, t->fMini, g->ev.lines[idx],
			Ui_Rect(row.x + 12.0f, row.y, row.w - 12.0f, row.h),
			i == 0 ? t->text2 : t->text3,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
}

/* 表单(单行编辑)鼠标转发 */
static void form_mouse(UiInput *form, UiApp *app, int msg, float x, float y)
{
	if (msg == UI_APP_MOUSE_DOWN)
		UiInput_MouseDown(form, Ui_HitTest(&app->ui, x, y), x);
	else if (msg == UI_APP_MOUSE_MOVE)
		UiInput_MouseMove(form, x);
	else if (msg == UI_APP_MOUSE_UP)
		UiInput_MouseUp(form);
}

/* ── 侧边导航数据 ──────────────────────────────────────────────────── */

typedef struct SideRow {
	const char *title;
	int page;
} SideRow;

static const SideRow kSide[] = {
	{ "开始", -1 },
	{ NULL, PG_WELCOME },
	{ "基础组件", -1 },
	{ NULL, PG_BUTTON },
	{ NULL, PG_BGROUP },
	{ NULL, PG_ICON },
	{ "表单输入", -1 },
	{ NULL, PG_INPUT },
	{ NULL, PG_TEXTAREA },
	{ NULL, PG_CHECKBOX },
	{ NULL, PG_RADIO },
	{ NULL, PG_SWITCH },
	{ NULL, PG_SLIDER },
	{ "选择器", -1 },
	{ NULL, PG_SELECT },
	{ NULL, PG_DATEPICKER },
	{ NULL, PG_PAGINATION },
	{ "导航与容器", -1 },
	{ NULL, PG_TABS },
	{ NULL, PG_NAV },
	{ NULL, PG_SCROLL },
	{ NULL, PG_LAYOUT },
	{ NULL, PG_FLEX },
	{ NULL, PG_GRID },
	{ "弹层与反馈", -1 },
	{ NULL, PG_DIALOG },
	{ NULL, PG_MENU },
	{ NULL, PG_TOAST },
	{ NULL, PG_TOOLTIP },
	{ "数据展示", -1 },
	{ NULL, PG_PROGRESS },
	{ NULL, PG_IMAGE },
	{ NULL, PG_MARKDOWN },
	{ "系统能力", -1 },
	{ NULL, PG_THEME },
	{ NULL, PG_NET },
	{ NULL, PG_WS },
	{ NULL, PG_WSSRV },
	{ NULL, PG_APP },
};
#define SIDE_ROWS (int)(sizeof(kSide) / sizeof(kSide[0]))

/* ── Markdown 文档渲染 ─────────────────────────────────────────────── */

static const char *const kNoDoc = "*本页暂无此文档。*";

static void draw_md_doc(Gallery *g, UiRect rc, int slot)
{
	const GalleryDocText *dt = GalleryDoc_Get(g->page);
	const char *text = NULL;
	int key = g->page * 5 + slot;

	if (!dt) {
		note_txt(g->app->gfx, &g->app->theme, rc, "文档缺失。");
		return;
	}
	if (slot == 0) text = dt->intro;
	else if (slot == 1) text = dt->doc;
	else if (slot == 2) text = dt->api;
	else if (slot == 4) text = dt->events;
	else text = dt->best;
	if (!text) text = kNoDoc;

	if (g->docKey != key) {
		UiMarkdown_SetText(g->md, text);
		UiMarkdown_ScrollToTop(g->md);
		g->docKey = key;
	}
	Ui_Markdown(g->md, &g->app->ui, rc);
}

/* ── 顶栏 ──────────────────────────────────────────────────────────── */

static void draw_topbar(Gallery *g)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiButtonStyle style = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	UiId tid = Ui_Id("gl.theme", 0, 0);
	UiId cid = Ui_Id("gl.close", 0, 0);
	UiRect trc = Ui_Rect(app->w - 116.0f, 8.0f, 36.0f, 30.0f);
	UiRect crc = Ui_Rect(app->w - 66.0f, 8.0f, 44.0f, 30.0f);
	int dark = t->themeId == UI_THEME_DARK;

	Gfx_Text(gfx, t->fBig, "CUI 组件画廊",
		Ui_Rect(24.0f, 8.0f, 320.0f, 30.0f), t->text, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fSmall, "即时模式 · Direct2D · 0CRT · 全组件演示",
		Ui_Rect(26.0f, 0.0f, 400.0f, 0.0f), t->hint, 0);

	if (t->fIcon) {
		UiIcon ic = UiIcon_Glyph(t->fIcon, dark ? 0xE706u : 0xE708u);
		if (Ui_IconButton(ui, tid, trc, &ic, &style, 1))
			UiTheme_SetTheme(t, dark ? UI_THEME_LIGHT : UI_THEME_DARK);
	} else {
		if (Ui_ButtonEx(ui, tid, trc, dark ? "浅色" : "深色", &style, 1))
			UiTheme_SetTheme(t, dark ? UI_THEME_LIGHT : UI_THEME_DARK);
	}

	/* 自绘关闭键: hover 渐入关闭红 */
	{
		UiWidgetState cs = Ui_State(ui, cid, crc, 1);
		float mx = crc.x + crc.w * 0.5f;
		float my = crc.y + crc.h * 0.5f;
		float d = 5.0f;
		UiColor xc = Ui_ColorMix(t->text3, 0xFFFFFFFF, cs.hover);
		if (cs.hover > 0.01f)
			Gfx_FillRoundRect(gfx, crc, 8.0f,
				Ui_ColorAlpha(0xFFE81123, (int)(cs.hover * 232.0f)));
		Gfx_Line(gfx, mx - d, my - d, mx + d, my + d, xc, 1.5f);
		Gfx_Line(gfx, mx - d, my + d, mx + d, my - d, xc, 1.5f);
		if (cs.hovered) Ui_SetCursor(ui, cid, UI_CURSOR_HAND);
		if (ui->clicked == cid) UiApp_Close(app);
	}
}

/* ── 侧边导航 ──────────────────────────────────────────────────────── */

static void switch_page(Gallery *g, int pg)
{
	if (pg == g->page) return;
	g->page = pg;
	Ui_SetFocusId(&g->app->ui, UI_ID_NONE);
	UiApp_Invalidate(g->app);
}

static void draw_sidebar(Gallery *g)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect side = Ui_Rect(SIDE_X, TOP_H + 10.0f, SIDE_W,
		app->h - TOP_H - 10.0f - STATUS_H);
	UiRect content;
	float contentH = 8.0f;
	float dt;
	int i;

	for (i = 0; i < SIDE_ROWS; i++)
		contentH += kSide[i].title ? SIDE_SEC : SIDE_ROW;

	Gfx_FillRoundRect(gfx, side, 10.0f, t->card);
	Gfx_StrokeRoundRect(gfx, side, 10.0f, t->border, 1.0f);

	dt = frame_dt(g);
	UiScrollView_Tick(&g->sideSv, ui, dt);
	UiScrollView_Begin(&g->sideSv, ui, side, side.w, contentH);
	content = UiScrollView_ContentRect(&g->sideSv);
	{
		float y = content.y + 6.0f;
		for (i = 0; i < SIDE_ROWS; i++) {
			if (kSide[i].title) {
				UiRect rc = Ui_Rect(content.x + 8.0f, y,
					content.w - 16.0f, SIDE_SEC);
				if (!UiScrollView_Cull(&g->sideSv, rc))
					Gfx_Text(gfx, t->fMini, kSide[i].title,
						Ui_Rect(rc.x + 6.0f, rc.y, rc.w - 12.0f, rc.h),
						t->hint, GFX_TEXT_VCENTER);
				y += SIDE_SEC;
			} else {
				int pg = kSide[i].page;
				UiRect rc = Ui_Rect(content.x + 8.0f, y,
					content.w - 16.0f, SIDE_ROW);
				if (!UiScrollView_Cull(&g->sideSv, rc)) {
					UiId id = Ui_IdIndex("gl.side", 0, 0, pg);
					UiWidgetState st = Ui_State(ui, id, rc, 1);
					int sel = pg == g->page;
					if (st.hovered && !sel)
						Gfx_FillRoundRect(gfx, rc, 7.0f,
							Ui_ColorAlpha(t->text3, 24));
					if (sel)
						Gfx_FillRoundRect(gfx, rc, 7.0f, t->primary.soft);
					if (st.hovered) Ui_SetCursor(ui, id, UI_CURSOR_HAND);
					Gfx_Text(gfx, sel ? t->fBtn : t->fBody, kPages[pg].label,
						Ui_Rect(rc.x + 12.0f, rc.y, rc.w - 24.0f, rc.h),
						sel ? t->primary.base : t->text2,
						GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
					if (ui->clicked == id) switch_page(g, pg);
				}
				y += SIDE_ROW;
			}
		}
	}
	UiScrollView_End(&g->sideSv, ui);
}

/* ── 内容区 ────────────────────────────────────────────────────────── */

static const char *const kSubTabLabels[6] = {
	"演示", "属性", "文档", "API", "事件", "最佳实践"
};

static void draw_content(Gallery *g)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	const GalleryPage *p = &kPages[g->page];
	float cx = SIDE_X + SIDE_W + SIDE_GAP;
	float cw = app->w - cx - 12.0f;
	UiRect titleRc = Ui_Rect(cx, TOP_H + 6.0f, cw * 0.52f, 30.0f);
	UiRect subRc = Ui_Rect(cx, TOP_H + 40.0f, cw, 34.0f);
	UiRect bodyOuter = Ui_Rect(cx, TOP_H + 82.0f, cw,
		app->h - TOP_H - 82.0f - STATUS_H);
	UiRect body = Ui_RectInset(bodyOuter, 12.0f);

	Gfx_Text(gfx, t->fBig, p->title, titleRc, t->text,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(gfx, t->fSmall, p->subtitle,
		Ui_Rect(cx + cw * 0.54f, TOP_H + 6.0f, cw * 0.46f, 30.0f), t->hint,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	Ui_Panel(ui, bodyOuter);

	if (g->page == PG_WELCOME) {
		draw_md_doc(g, body, 0);
		return;
	}

	/* 子标签页 */
	{
		int prev = g->subTab[g->page];
		UiTabBar_SetSelected(&g->subTabs, prev);
		UiTabBar_Draw(&g->subTabs, ui, subRc, kSubTabLabels, &g->pill, 1);
		if (g->subTabs.selected != prev) {
			g->subTab[g->page] = g->subTabs.selected;
			Ui_SetFocusId(ui, UI_ID_NONE);
			UiApp_Invalidate(app);
		}
	}

	if (g->subTab[g->page] == 0) {
		if (p->demo) p->demo(g, body);
	} else if (g->subTab[g->page] == 1) {
		if (p->props) {
			p->props(g, body);
			/* props 内已刷新 codeText, props_split 已切好右下面板 */
			code_view(g, g->codeRc, g->codeText);
		} else draw_md_doc(g, body, 1);
	} else if (g->subTab[g->page] == 2) {
		draw_md_doc(g, body, 1);
	} else if (g->subTab[g->page] == 3) {
		draw_md_doc(g, body, 2);
	} else if (g->subTab[g->page] == 4) {
		/* 事件: 上半事件接线文档, 下半实时事件日志 */
		UiRect doc = body, log;
		if (body.h >= 560.0f) {
			log = Ui_Rect(body.x, body.y + body.h - 200.0f, body.w, 200.0f);
			doc.h = body.h - 200.0f - 10.0f;
		} else {
			log = Ui_Rect(0, 0, 0, 0);
		}
		draw_md_doc(g, doc, 4);
		draw_event_log(g, log);
	} else {
		draw_md_doc(g, body, 3);
	}
}

/* ── 状态栏 ────────────────────────────────────────────────────────── */

static void draw_status(Gallery *g)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx_Text(app->gfx, t->fSmall, g->status,
		Ui_Rect(24.0f, app->h - STATUS_H + 4.0f, app->w - 48.0f, 18.0f),
		t->hint, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

/* ════════════════════════════════════════════════════════════════════
 * 页面实现
 * ════════════════════════════════════════════════════════════════════ */

/* ── Button ────────────────────────────────────────────────────────── */

static const char *const kBtnTypeLabels[5] = {
	"默认", "主要", "成功", "警告", "危险"
};
static const char *const kBtnAppearLabels[3] = { "实心", "浅底", "文字" };
static const char *const kBtnShapeLabels[3] = { "直角", "胶囊", "圆形" };
static const int kBtnTypes[5] = {
	UI_BTYPE_DEFAULT, UI_BTYPE_PRIMARY, UI_BTYPE_SUCCESS,
	UI_BTYPE_WARNING, UI_BTYPE_ERROR
};

static void button_init(Gallery *g)
{
	UiInput_Init(&g->button.form, &g->app->ui, &g->app->ime);
	g->button.fld = UiInput_Add(&g->button.form,
		Ui_Id("gl.btn.label", 0, 0), 31, 0, NULL, L"按钮文字");
	Edit_SetTextUtf8(UiInput_Edit(&g->button.form, g->button.fld), "主要操作");
	UiButtonGroup_Init(&g->button.type, Ui_Id("gl.btn.type", 0, 0),
		UI_BG_SINGLE, 5);
	UiButtonGroup_SetSelected(&g->button.type, 1);
	UiButtonGroup_Init(&g->button.appear, Ui_Id("gl.btn.appear", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->button.appear, 0);
	UiButtonGroup_Init(&g->button.shape, Ui_Id("gl.btn.shape", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->button.shape, 0);
	UiCheckbox_Init(&g->button.dashed, Ui_Id("gl.btn.dashed", 0, 0), 0);
	UiCheckbox_Init(&g->button.disabled, Ui_Id("gl.btn.disabled", 0, 0), 0);
	UiCheckbox_Init(&g->button.icon, Ui_Id("gl.btn.icon", 0, 0), 0);
}

static void button_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect r;
	int i;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "实心 (SOLID)");
	Ui_StackSkip(&col, 6.0f);
	r = Ui_StackTake(&col, 38.0f);
	for (i = 0; i < 5; i++) {
		UiButtonStyle st = UiButtonStyle_Make(kBtnTypes[i],
			UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
		if (Ui_ButtonEx(ui, Ui_IdIndex("gl.btn.solid", i, 0, 0),
				Ui_Rect(r.x + i * 122.0f, r.y, 112.0f, 36.0f),
				kBtnTypeLabels[i], &st, 1))
			set_status(g, "点击了实心按钮");
	}
	Ui_StackSkip(&col, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "浅底 (PLAIN)");
	Ui_StackSkip(&col, 6.0f);
	r = Ui_StackTake(&col, 38.0f);
	for (i = 0; i < 5; i++) {
		UiButtonStyle st = UiButtonStyle_Make(kBtnTypes[i],
			UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
		if (Ui_ButtonEx(ui, Ui_IdIndex("gl.btn.plain", i, 0, 0),
				Ui_Rect(r.x + i * 122.0f, r.y, 112.0f, 36.0f),
				kBtnTypeLabels[i], &st, 1))
			set_status(g, "点击了浅底按钮");
	}
	Ui_StackSkip(&col, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "文字 / 胶囊 / 虚线 / 圆形 / 图标 / 禁用");
	Ui_StackSkip(&col, 6.0f);
	r = Ui_StackTake(&col, 38.0f);
	{
		UiButtonStyle text = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
			UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
		UiButtonStyle round = UiButtonStyle_Make(UI_BTYPE_SUCCESS,
			UI_BAPPEAR_SOLID, UI_BSHAPE_ROUND, 0);
		UiButtonStyle dashed = UiButtonStyle_Make(UI_BTYPE_ERROR,
			UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, UI_BFLAG_DASHED);
		UiButtonStyle circle = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
			UI_BAPPEAR_SOLID, UI_BSHAPE_CIRCLE, 0);
		UiButtonStyle icon = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
			UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
		if (Ui_ButtonEx(ui, Ui_Id("gl.btn.text", 0, 0),
				Ui_Rect(r.x, r.y, 88.0f, 36.0f), "文字按钮", &text, 1))
			set_status(g, "点击了文字按钮");
		if (Ui_ButtonEx(ui, Ui_Id("gl.btn.round", 0, 0),
				Ui_Rect(r.x + 100.0f, r.y, 96.0f, 36.0f), "胶囊", &round, 1))
			set_status(g, "点击了胶囊按钮");
		if (Ui_ButtonEx(ui, Ui_Id("gl.btn.dashed", 0, 0),
				Ui_Rect(r.x + 208.0f, r.y, 96.0f, 36.0f), "虚线", &dashed, 1))
			set_status(g, "点击了虚线按钮");
		if (Ui_ButtonEx(ui, Ui_Id("gl.btn.circle", 0, 0),
				Ui_Rect(r.x + 316.0f, r.y, 36.0f, 36.0f), "+", &circle, 1))
			set_status(g, "点击了圆形按钮");
		if (t->fIcon) {
			UiIcon ic = UiIcon_Glyph(t->fIcon, 0xE74Eu);
			if (Ui_ButtonExIcon(ui, Ui_Id("gl.btn.icn", 0, 0),
					Ui_Rect(r.x + 364.0f, r.y, 96.0f, 36.0f),
					&ic, "保存", &icon, 1))
				set_status(g, "点击了图标按钮");
		}
		Ui_ButtonEx(ui, Ui_Id("gl.btn.off", 0, 0),
			Ui_Rect(r.x + 472.0f, r.y, 96.0f, 36.0f), "禁用", &icon, 0);
	}
	Ui_StackSkip(&col, 16.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"悬停 / 按下 / 禁用都是真实交互: 按钮点击会写入底部状态栏。\n"
		"颜色全部来自主题令牌 —— 右上角切换亮暗主题可立即验证。\n"
		"「属性」页可以逐项修改一个按钮的类型、外观、形状并实时预览。");
}

static void button_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码: 随左侧属性刷新 (draw_content 里渲染) */
	{
		static const char *const kBtnTypeNames[5] = {
			"UI_BTYPE_DEFAULT", "UI_BTYPE_PRIMARY", "UI_BTYPE_SUCCESS",
			"UI_BTYPE_WARNING", "UI_BTYPE_ERROR"
		};
		static const char *const kBtnAppearNames[3] = {
			"UI_BAPPEAR_SOLID", "UI_BAPPEAR_PLAIN", "UI_BAPPEAR_TEXT"
		};
		static const char *const kBtnShapeNames[3] = {
			"UI_BSHAPE_RECT", "UI_BSHAPE_ROUND", "UI_BSHAPE_CIRCLE"
		};
		char lbl[64];
		lbl[0] = 0;
		Edit_TextUtf8(UiInput_Edit(&g->button.form, g->button.fld),
			lbl, (int)sizeof(lbl));
		if (!lbl[0]) lstrcpynA(lbl, "按钮", (int)sizeof(lbl));
		snprintf(g->codeText, sizeof(g->codeText),
			"/* 1) 组样式 (当前属性) */\n"
			"UiButtonStyle st = UiButtonStyle_Make(\n"
			"    %s, %s, %s, %s);\n"
			"/* 2) 画 + 接点击 (当帧返回 1) */\n"
			"if (Ui_ButtonEx%s(ui, Ui_Id(\"btn.ok\", 0, 0), rc,\n"
			"        \"%s\", &st, %s)) {\n"
			"    on_submit();   /* 响应帧内或置 pending */\n"
			"}\n",
			kBtnTypeNames[g->button.type.selected],
			kBtnAppearNames[g->button.appear.selected],
			kBtnShapeNames[g->button.shape.selected],
			g->button.dashed.checked ? "UI_BFLAG_DASHED" : "0",
			g->button.icon.checked ? "Icon" : "",
			lbl,
			g->button.disabled.checked ? "0" : "1");
	}
	char label[64];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "按钮文字 (UTF-8)");
	Ui_StackSkip(&col, 4.0f);
	UiInput_BeginFrame(&g->button.form);
	UiInput_SetRect(&g->button.form, g->button.fld, Ui_StackTake(&col, 36.0f));
	UiInput_Draw(&g->button.form, g->button.fld);
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "类型");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->button.type, Ui_StackTake(&col, 34.0f),
		kBtnTypeLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "外观");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->button.appear, Ui_StackTake(&col, 34.0f),
		kBtnAppearLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "形状");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->button.shape, Ui_StackTake(&col, 34.0f),
		kBtnShapeLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	if (Ui_Checkbox(&g->button.dashed, ui, Ui_StackTake(&col, 26.0f),
			"虚线描边", 1))
		;
	Ui_StackSkip(&col, 6.0f);
	if (Ui_Checkbox(&g->button.icon, ui, Ui_StackTake(&col, 26.0f),
			"带图标", 1))
		;
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->button.disabled, ui, Ui_StackTake(&col, 26.0f),
		"禁用状态", 1);

	label[0] = 0;
	if (UiInput_Edit(&g->button.form, g->button.fld))
		Edit_TextUtf8(UiInput_Edit(&g->button.form, g->button.fld),
			label, (int)sizeof(label));
	if (!label[0]) lstrcpynA(label, "按钮", (int)sizeof(label));

	/* 右侧预览 */
	{
		UiRect area = pane_in(pv, 20.0f);
		UiButtonStyle st = UiButtonStyle_Make(
			kBtnTypes[g->button.type.selected],
			g->button.appear.selected, g->button.shape.selected,
			g->button.dashed.checked ? UI_BFLAG_DASHED : 0);
		UiRect b = Ui_Rect(area.x + area.w * 0.5f - 110.0f,
			area.y + area.h * 0.5f - 24.0f, 220.0f, 48.0f);
		int enabled = !g->button.disabled.checked;
		int hit = 0;
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"实时预览 —— 点击会写入状态栏");
		if (g->button.icon.checked && t->fIcon) {
			UiIcon ic = UiIcon_Glyph(t->fIcon, 0xE8E5u);
			hit = Ui_ButtonExIcon(ui, Ui_Id("gl.btn.pv", 0, 0), b,
				&ic, label, &st, enabled);
		} else {
			hit = Ui_ButtonEx(ui, Ui_Id("gl.btn.pv", 0, 0), b,
				label, &st, enabled);
		}
		if (hit) set_status(g, "预览按钮被点击");
	}
}

static int button_key(Gallery *g, UINT vk)
{
	return UiInput_Key(&g->button.form, g->app->hwnd, vk);
}

static int button_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->button.form, ch);
}

static void button_ime(Gallery *g)
{
	UiInput_OnIme(&g->button.form);
}

static void button_blink(Gallery *g)
{
	UiInput_Blink(&g->button.form);
}

static void button_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->button.form, g->app, msg, x, y);
}

/* ── ButtonGroup ───────────────────────────────────────────────────── */

static const char *const kBgnLabels[3] = { "提交", "重置", "导出" };
static const char *const kBgsLabels[2] = { "浅色", "深色" };
static const char *const kBgmLabels[3] = { "选项 A", "选项 B", "选项 C" };
static const char *const kBgModeLabels[3] = { "普通", "单选", "多选" };
static const char *const kBgCountLabels[3] = { "2 段", "3 段", "4 段" };
static const char *const kBgVarLabels[4] = { "主色", "成功", "警告", "危险" };
static const int kBgVariants[4] = {
	UI_BTN_PRIMARY, UI_BTN_SUCCESS, UI_BTN_WARNING, UI_BTN_DANGER
};
static const char *const kBgPvLabels[4] = { "一段", "二段", "三段", "四段" };

static void bgroup_init(Gallery *g)
{
	UiButtonGroup_Init(&g->bgroup.demoN, Ui_Id("gl.bg.dn", 0, 0),
		UI_BG_NORMAL, 3);
	UiButtonGroup_Init(&g->bgroup.demoS, Ui_Id("gl.bg.ds", 0, 0),
		UI_BG_SINGLE, 2);
	UiButtonGroup_SetSelected(&g->bgroup.demoS, 0);
	UiButtonGroup_Init(&g->bgroup.demoM, Ui_Id("gl.bg.dm", 0, 0),
		UI_BG_MULTI, 3);
	UiButtonGroup_SetMask(&g->bgroup.demoM, 0x1u);
	UiButtonGroup_Init(&g->bgroup.mode, Ui_Id("gl.bg.mode", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->bgroup.mode, 1);
	UiButtonGroup_Init(&g->bgroup.count, Ui_Id("gl.bg.count", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->bgroup.count, 1);
	UiButtonGroup_Init(&g->bgroup.variant, Ui_Id("gl.bg.var", 0, 0),
		UI_BG_SINGLE, 4);
	UiButtonGroup_SetSelected(&g->bgroup.variant, 0);
	UiButtonGroup_Init(&g->bgroup.pv, Ui_Id("gl.bg.pv", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->bgroup.pv, 0);
	g->bgroup.pvMode = UI_BG_SINGLE;
	g->bgroup.pvCount = 3;
	UiCheckbox_Init(&g->bgroup.disMid, Ui_Id("gl.bg.dis", 0, 0), 0);
}

static void bgroup_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	char buf[96];
	int prev;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "普通分组 (NORMAL) —— 点击写状态栏");
	Ui_StackSkip(&col, 6.0f);
	if (UiButtonGroup_Draw(ui, &g->bgroup.demoN, Ui_StackTake(&col, 36.0f),
			kBgnLabels, NULL, NULL, 1) >= 0 &&
		g->bgroup.demoN.clicked >= 0)
		set_status(g, kBgnLabels[g->bgroup.demoN.clicked]);
	Ui_StackSkip(&col, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "单选分组 (SINGLE) —— 切换全局主题");
	Ui_StackSkip(&col, 6.0f);
	prev = g->bgroup.demoS.selected;
	UiButtonGroup_Draw(ui, &g->bgroup.demoS, Ui_StackTake(&col, 36.0f),
		kBgsLabels, NULL, NULL, 1);
	if (g->bgroup.demoS.changed && g->bgroup.demoS.selected != prev) {
		UiTheme_SetTheme(t, g->bgroup.demoS.selected == 1
			? UI_THEME_DARK : UI_THEME_LIGHT);
		set_status(g, g->bgroup.demoS.selected == 1
			? "主题: 深色" : "主题: 浅色");
	}
	Ui_StackSkip(&col, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "多选分组 (MULTI) —— mask 位图");
	Ui_StackSkip(&col, 6.0f);
	UiButtonGroup_Draw(ui, &g->bgroup.demoM, Ui_StackTake(&col, 36.0f),
		kBgmLabels, NULL, NULL, 1);
	if (g->bgroup.demoM.changed) {
		wsprintfA(buf, "flags mask = 0x%X", g->bgroup.demoM.mask);
		set_status(g, buf);
	}
	Ui_StackSkip(&col, 16.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"三组共享同一套视觉规则: 相邻按钮融合外框与圆角, 内部 1px 分隔线,\n"
		"键盘焦点环只在 Tab 导航时出现。\n"
		"「属性」页可以切换预览组的模式 / 段数 / 强调色并实时查看。");
}

static void bgroup_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiButtonGroup bg;\n"
			"UiButtonGroup_Init(&bg, Ui_Id(\"bg.x\", 0, 0),\n"
			"    %s, %d);          /* 模式, 段数 */\n"
			"UiButtonGroup_Draw(ui, &bg, rc, kLabels,\n"
			"    NULL, %s, 1);     /* en=禁用位图, NULL=全启用 */\n"
			"if (bg.changed) {\n"
			"    /* 单选读 bg.selected; 多选读 bg.mask */\n"
			"    result = %s;\n"
			"    bg.changed = 0;\n"
			"}\n",
			g->bgroup.mode.selected == 1 ? "UI_BG_SINGLE"
				: (g->bgroup.mode.selected == 2 ? "UI_BG_MULTI" : "UI_BG_NORMAL"),
			2 + g->bgroup.count.selected,
			g->bgroup.disMid.checked ? "en" : "NULL",
			g->bgroup.mode.selected == 2 ? "(int)bg.mask"
				: (g->bgroup.mode.selected == 1 ? "bg.selected" : "bg.clicked"));
	}
	int mode, cnt;
	unsigned char en[4];
	int i;

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "模式");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->bgroup.mode, Ui_StackTake(&col, 34.0f),
		kBgModeLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "段数");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->bgroup.count, Ui_StackTake(&col, 34.0f),
		kBgCountLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "选中段强调色");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->bgroup.variant, Ui_StackTake(&col, 34.0f),
		kBgVarLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	Ui_Checkbox(&g->bgroup.disMid, ui, Ui_StackTake(&col, 26.0f),
		"禁用第二段", 1);

	mode = g->bgroup.mode.selected;
	cnt = 2 + g->bgroup.count.selected;
	if (mode != g->bgroup.pvMode || cnt != g->bgroup.pvCount) {
		UiButtonGroup_Init(&g->bgroup.pv, Ui_Id("gl.bg.pv", 0, 0), mode, cnt);
		UiButtonGroup_SetSelected(&g->bgroup.pv, 0);
		g->bgroup.pvMode = mode;
		g->bgroup.pvCount = cnt;
	}

	for (i = 0; i < 4; i++) en[i] = 1;
	if (g->bgroup.disMid.checked && cnt > 1) en[1] = 0;
	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"实时预览");
		UiButtonGroup_Draw(ui, &g->bgroup.pv,
			Ui_Rect(area.x, area.y + 34.0f, area.w, 40.0f),
			kBgPvLabels, NULL, en, kBgVariants[g->bgroup.variant.selected]);
		if (g->bgroup.pv.changed) {
			char buf[96];
			if (mode == UI_BG_MULTI)
				wsprintfA(buf, "预览组 mask = 0x%X", g->bgroup.pv.mask);
			else if (mode == UI_BG_SINGLE)
				wsprintfA(buf, "预览组选中: %s", kBgPvLabels[g->bgroup.pv.selected]);
			else if (g->bgroup.pv.clicked >= 0)
				wsprintfA(buf, "预览组点击: %s", kBgPvLabels[g->bgroup.pv.clicked]);
			else
				buf[0] = 0;
			if (buf[0]) set_status(g, buf);
		}
	}
}

static int bgroup_key(Gallery *g, UINT vk)
{
	UiContext *ui = &g->app->ui;
	if (ui->navId == g->bgroup.demoN.id)
		return UiButtonGroup_Key(ui, &g->bgroup.demoN, vk);
	if (ui->navId == g->bgroup.demoS.id)
		return UiButtonGroup_Key(ui, &g->bgroup.demoS, vk);
	if (ui->navId == g->bgroup.demoM.id)
		return UiButtonGroup_Key(ui, &g->bgroup.demoM, vk);
	return 0;
}

/* ── Icon ──────────────────────────────────────────────────────────── */

/* glyph 数据表移至 samples/gallery_icons.c: 1386 条官方档案全量收录
 * (来源/口径见 gallery_icons.h 头注释), 这里只留页面自身的常量。 */

/* 属性页着色 (tint) 组 */
static const char *const kIconTintLabels[5] = {
	"主色", "成功", "警告", "正文", "禁用"
};

/* 演示页 PUA 分区过滤: 0 = 全部; 1..6 对应官方档案的六个分区表 */
static const char *const kIconZoneLabels[7] = {
	"全部", "E7-E9", "EA-EC", "ED-EF", "F0-F2", "F3-F5", "F6-F8"
};
static const uint32_t kIconZoneLo[7] = {
	0x0000u, 0xE700u, 0xEA00u, 0xED00u, 0xF000u, 0xF300u, 0xF600u
};
static const uint32_t kIconZoneHi[7] = {
	0xFFFFu, 0xE9FFu, 0xECFFu, 0xEFFFu, 0xF2FFu, 0xF5FFu, 0xF8FFu
};
#define ICON_ZONE_N 7

static int icon_anim(Gallery *g)
{
	return UiScrollView_Busy(&g->icon.sv) || UiTooltip_Pending(&g->tip);
}

/* ANSI 文本写剪贴板 (码点串是纯 ASCII, CF_TEXT 即可) */
static int icon_copy_text(UiApp *app, const char *s)
{
	int len = lstrlenA(s), ok = 0, i;
	HGLOBAL h;
	char *p;

	if (!OpenClipboard(app->hwnd)) return 0;
	if (EmptyClipboard()) {
		h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len + 1u);
		if (h) {
			p = (char *)GlobalLock(h);
			if (p) {
				for (i = 0; i < len; i++) p[i] = s[i];
				p[len] = 0;
				GlobalUnlock(h);
				ok = SetClipboardData(CF_TEXT, h) != NULL;
			}
			if (!ok) GlobalFree(h);
		}
	}
	CloseClipboard();
	return ok;
}

/* 大小写不敏感 ASCII 子串匹配; q 必须先转成小写 */
static int icon_icontains(const char *hay, const char *q)
{
	int i;
	if (!q[0]) return 1;
	for (i = 0; hay[i]; i++) {
		int j = i, k = 0;
		while (hay[j] && q[k]) {
			char a = hay[j];
			if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
			if (a != q[k]) break;
			j++; k++;
		}
		if (!q[k]) return 1;
	}
	return 0;
}

/* 按分区 + 搜索串过滤官方档案, 结果存 vis[] (kMdl2Icons 下标)。
 * 名称与码点都参与匹配 (码点按 4 位十六进制子串)。 */
static void icon_refilter(Gallery *g, const char *q)
{
	char low[48];
	int i, n = 0, z = g->icon.zone.selected;

	if (z < 0 || z >= ICON_ZONE_N) z = 0;
	for (i = 0; q[i] && i < (int)sizeof(low) - 1; i++) {
		char c = q[i];
		if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		low[i] = c;
	}
	low[i] = 0;
	for (i = 0; i < kMdl2IconCount; i++) {
		uint32_t cp = kMdl2Icons[i].cp;
		if (cp < kIconZoneLo[z] || cp > kIconZoneHi[z]) continue;
		if (low[0]) {
			char hex[8];
			wsprintfA(hex, "%04X", cp);
			if (!icon_icontains(kMdl2Icons[i].name, low) &&
				!icon_icontains(hex, low)) continue;
		}
		if (n >= (int)(sizeof(g->icon.vis) / sizeof(g->icon.vis[0]))) break;
		g->icon.vis[n++] = (unsigned short)i;
	}
	g->icon.visN = n;
	g->icon.lastZone = z;
	lstrcpynA(g->icon.lastQuery, q, (int)sizeof(g->icon.lastQuery));
}

static void icon_init(Gallery *g)
{
	UiScrollConfig icfg = UiScrollConfig_Desktop();
	UiScrollbarStyle ibar = UiScrollbarStyle_Default();
	icfg.wheelStep = 72.0f;
	UiScrollView_Init(&g->icon.sv, Ui_Id("gl.icon.sv", 0, 0), &icfg, &ibar);
	UiSlider_Init(&g->icon.size, Ui_Id("gl.icon.size", 0, 0), 0.36f);
	UiButtonGroup_Init(&g->icon.tint, Ui_Id("gl.icon.tint", 0, 0),
		UI_BG_SINGLE, 5);
	UiButtonGroup_SetSelected(&g->icon.tint, 0);
	UiButtonGroup_Init(&g->icon.zone, Ui_Id("gl.icon.zone", 0, 0),
		UI_BG_SINGLE, ICON_ZONE_N);
	UiButtonGroup_SetSelected(&g->icon.zone, 0);
	UiCheckbox_Init(&g->icon.disabled, Ui_Id("gl.icon.dis", 0, 0), 0);
	UiInput_Init(&g->icon.form, &g->app->ui, &g->app->ime);
	g->icon.search = UiInput_Add(&g->icon.form, Ui_Id("gl.icon.q", 0, 0),
		40, 0, NULL, L"搜索名称 / 码点 (camera / e713)");
	g->icon.sel = -1;
	g->icon.lastZone = -1;            /* 首帧必重过滤 (与初始 0 不同) */
	icon_refilter(g, "");
}

static UiColor icon_tint(Gallery *g)
{
	UiTheme *t = &g->app->theme;
	switch (g->icon.tint.selected) {
	case 0: return t->primary.base;
	case 1: return t->success.base;
	case 2: return t->warning.base;
	case 3: return t->text;
	default: return t->text4;
	}
}

static void icon_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col, bar;
	UiRect grid;
	char q[64], buf[160];
	int i, cols, rows, tipMatched = 0;
	float tileW, tileH = 72.0f, rowGap = 8.0f, contentH;

	/* 搜索文本 / 分区变化时重过滤, 并把滚动复位到顶部 */
	Edit_TextUtf8(UiInput_Edit(&g->icon.form, g->icon.search), q,
		(int)sizeof(q));
	if (lstrcmpA(q, g->icon.lastQuery) ||
		g->icon.zone.selected != g->icon.lastZone) {
		icon_refilter(g, q);
		UiScrollView_ScrollTo(&g->icon.sv, ui, 0.0f, 0);
		UiApp_Invalidate(app);
	}

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f);

	wsprintfA(buf, "Segoe MDL2 Assets 官方档案 (共 %d glyphs · 当前 %d)",
		kMdl2IconCount, g->icon.visN);
	lbl(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 4.0f);

	/* 搜索框 + PUA 分区过滤 (同一行) */
	Ui_StackBegin(&bar, Ui_StackTake(&col, 38.0f), UI_ROW, 10.0f, 0.0f);
	UiInput_BeginFrame(&g->icon.form);
	UiInput_SetRect(&g->icon.form, g->icon.search, Ui_StackTake(&bar, 250.0f));
	UiInput_Draw(&g->icon.form, g->icon.search);
	UiButtonGroup_Draw(ui, &g->icon.zone, Ui_StackTakeFill(&bar),
		kIconZoneLabels, NULL, NULL, 1);
	if (g->icon.zone.changed) UiApp_Invalidate(app);
	Ui_StackSkip(&col, 8.0f);

	/* 点击选中详情卡 (未选中时显示引导, 即时反馈而非只写状态栏) */
	if (g->icon.sel >= 0 && g->icon.sel < kMdl2IconCount) {
		const GalleryIcon *ic = &kMdl2Icons[g->icon.sel];
		UiRect card = Ui_StackTake(&col, 92.0f);
		float bigPx = 40.0f;
		Gfx_FillRoundRect(gfx, card, 10.0f,
			Ui_ColorAlpha(t->primary.base, 16));
		Gfx_StrokeRoundRect(gfx, card, 10.0f,
			Ui_ColorAlpha(t->primary.base, 60), 1.0f);
		if (t->fIcon) {
			UiIcon icx = UiIcon_Glyph(t->fIcon, ic->cp);
			UiIcon_Draw(ui, &icx,
				Ui_Rect(card.x + 18.0f,
					card.y + (card.h - bigPx) * 0.5f, bigPx, bigPx),
				t->primary.base, 1);
		}
		wsprintfA(buf, "%s  ·  U+%04X  ·  已复制到剪贴板, 粘贴即用",
			ic->name, ic->cp);
		Gfx_Text(gfx, t->fSmall, buf,
			Ui_Rect(card.x + 72.0f, card.y + 12.0f, card.w - 88.0f, 20.0f),
			t->text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		wsprintfA(buf, "UiIcon ic = UiIcon_Glyph(t->fIcon, 0x%04Xu);", ic->cp);
		Gfx_Text(gfx, g->fCode ? g->fCode : t->fSmall, buf,
			Ui_Rect(card.x + 72.0f, card.y + 34.0f, card.w - 88.0f, 18.0f),
			t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		wsprintfA(buf, "// %s · UiIcon_Draw(ui, &ic, rc, tint, 1);",
			ic->name);
		Gfx_Text(gfx, g->fCode ? g->fCode : t->fSmall, buf,
			Ui_Rect(card.x + 72.0f, card.y + 54.0f, card.w - 88.0f, 18.0f),
			t->hint, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	} else {
		UiRect card = Ui_StackTake(&col, 62.0f);
		note_txt(gfx, t, card,
			"1386 个 glyph 全量收录自 Windows 官方 MDL2 档案, 按 PUA 分区浏览,\n"
			"可按英文名与码点搜索; 点击任意图标: 选中高亮 + 复制码点到剪贴板 + 此处显示用法代码。");
	}
	Ui_StackSkip(&col, 8.0f);

	/* 全量网格: 放进 ScrollView (虚拟滚动, 只画可见 tile) */
	grid = Ui_StackTakeFill(&col);
	cols = (int)(grid.w / 84.0f);
	if (cols < 8) cols = 8;
	if (cols > 20) cols = 20;
	tileW = grid.w / (float)cols;
	rows = (g->icon.visN + cols - 1) / cols;
	contentH = g->icon.visN ? (float)rows * (tileH + rowGap) + 8.0f : grid.h;

	UiScrollView_Tick(&g->icon.sv, ui, frame_dt(g));
	UiScrollView_Begin(&g->icon.sv, ui, grid, grid.w, contentH);
	{
		UiRect content = UiScrollView_ContentRect(&g->icon.sv);
		for (i = 0; i < g->icon.visN; i++) {
			unsigned short idx = g->icon.vis[i];
			const GalleryIcon *ic = &kMdl2Icons[idx];
			UiRect tile = Ui_Rect(
				content.x + (float)(i % cols) * tileW + 4.0f,
				content.y + (float)(i / cols) * (tileH + rowGap) + 4.0f,
				tileW - 8.0f, tileH);
			UiId id = Ui_IdIndex("gl.icon.tile", idx, 0, 0);
			UiWidgetState st = Ui_State(ui, id, tile, 1);
			int sel = idx == g->icon.sel;
			if (UiScrollView_Cull(&g->icon.sv, tile)) continue;
			Gfx_FillRoundRect(gfx, tile, 8.0f, sel
				? Ui_ColorAlpha(t->primary.base, 36)
				: st.hover > 0.01f
				? Ui_ColorAlpha(t->primary.base,
					(int)(st.hover * 30.0f))
				: Ui_ColorAlpha(t->text3, 14));
			if (sel)
				Gfx_StrokeRoundRect(gfx, tile, 8.0f, t->primary.base, 1.5f);
			if (t->fIcon) {
				UiIcon icx = UiIcon_Glyph(t->fIcon, ic->cp);
				UiIcon_Draw(ui, &icx,
					Ui_Rect(tile.x + tile.w * 0.5f - 13.0f,
						tile.y + 8.0f, 26.0f, 26.0f),
					sel ? t->primary.base : t->text2, 1);
			}
			Gfx_Text(gfx, t->fMini, ic->name,
				Ui_Rect(tile.x + 4.0f, tile.y + 38.0f, tile.w - 8.0f, 14.0f),
				sel ? t->text : t->text2,
				GFX_TEXT_CENTER | GFX_TEXT_ELLIPSIS);
			wsprintfA(buf, "%04X", ic ? ic->cp : 0);
			Gfx_Text(gfx, t->fMini, buf,
				Ui_Rect(tile.x + 4.0f, tile.y + 52.0f, tile.w - 8.0f, 13.0f),
				t->hint, GFX_TEXT_CENTER | GFX_TEXT_ELLIPSIS);
			if (st.hovered) {
				Ui_SetCursor(ui, id, UI_CURSOR_HAND);
				wsprintfA(buf, "%s (U+%04X)", ic->name, ic->cp);
				UiTooltip_Update(&g->tip, ui, id, tile, buf,
					UiFrame_Now());
				tipMatched = 1;
			}
			if (ui->clicked == id) {
				g->icon.sel = idx;
				wsprintfA(buf, "U+%04X", ic->cp);
				icon_copy_text(app, buf);
				wsprintfA(buf, "%s (U+%04X) 码点已复制到剪贴板",
					ic->name, ic->cp);
				set_status(g, buf);
				UiApp_Invalidate(app);
			}
		}
	}
	UiScrollView_End(&g->icon.sv, ui);
	if (!tipMatched)
		UiTooltip_Update(&g->tip, ui, UI_ID_NONE, Ui_Rect(0, 0, 0, 0),
			"", UiFrame_Now());
	if (!g->icon.visN)
		note_txt(gfx, t, Ui_Rect(grid.x, grid.y + grid.h * 0.4f, grid.w, 40.0f),
			"无匹配项: 试试英文名 (camera) 或 4 位码点 (e713)");
}

static void icon_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;
	char buf[64], cpTxt[8], hdr[96];
	float px;
	UiColor tint;
	int enabled;
	uint32_t pvCp = 0xE713u;
	const char *pvName = "Setting";

	/* 演示页点击选中 glyph 后, 代码/预览同步切换到该图标 */
	if (g->icon.sel >= 0 && g->icon.sel < kMdl2IconCount) {
		pvCp = kMdl2Icons[g->icon.sel].cp;
		pvName = kMdl2Icons[g->icon.sel].name;
	}
	wsprintfA(cpTxt, "%04X", pvCp);

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"/* %s (U+%s), 字号 %d DIP, tint 从主题令牌取色 */\n"
			"UiIcon ic = UiIcon_Glyph(t->fIcon, 0x%su);\n"
			"UiIcon_Draw(ui, &ic, rc, t->primary.base, %s);\n"
			"\n"
			"/* 可点击图标: 返回 1 = 点击 */\n"
			"UiButtonStyle st = UiButtonStyle_Make(\n"
			"    UI_BTYPE_DEFAULT, UI_BAPPEAR_SOLID,\n"
			"    UI_BSHAPE_RECT, 0);\n"
			"if (Ui_IconButton(ui, id, rc, &ic, &st, %s))\n"
			"    on_settings();\n",
			pvName, cpTxt,
			(int)(12.0f + g->icon.size.value * 48.0f + 0.5f), cpTxt,
			g->icon.disabled.checked ? "0" : "1",
			g->icon.disabled.checked ? "0" : "1");
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "尺寸");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->icon.size, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	if (g->icon.size.changed || g->icon.size.committed)
		UiApp_Invalidate(app);
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "着色 (tint)");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->icon.tint, Ui_StackTake(&col, 34.0f),
		kIconTintLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	Ui_Checkbox(&g->icon.disabled, ui, Ui_StackTake(&col, 26.0f),
		"禁用态 (灰显)", 1);
	Ui_StackSkip(&col, 10.0f);

	px = 12.0f + g->icon.size.value * 48.0f;
	wsprintfA(hdr, "实时预览 (%s · U+%s)", pvName, cpTxt);

	wsprintfA(buf, "预览字号 %d DIP", (int)(px + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);

	tint = icon_tint(g);
	enabled = !g->icon.disabled.checked;
	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f), hdr);
		if (t->fIcon) {
			UiIcon ic = UiIcon_Glyph(t->fIcon, pvCp);
			UiIcon_Draw(ui, &ic,
				Ui_Rect(area.x + area.w * 0.5f - px * 0.5f,
					area.y + area.h * 0.5f - px * 0.5f, px, px),
				tint, enabled);
		} else {
			note_txt(gfx, t, area, "未找到 Segoe MDL2 Assets 字体。");
		}
	}
}

/* 搜索框事件转发 (UiInput 生命周期与 button/input 页一致) */
static int icon_key(Gallery *g, UINT vk)
{
	return UiInput_Key(&g->icon.form, g->app->hwnd, vk);
}

static int icon_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->icon.form, ch);
}

static void icon_ime(Gallery *g)
{
	UiInput_OnIme(&g->icon.form);
}

static void icon_blink(Gallery *g)
{
	UiInput_Blink(&g->icon.form);
}

static void icon_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->icon.form, g->app, msg, x, y);
}

/* ── Input ─────────────────────────────────────────────────────────── */

static void input_init(Gallery *g)
{
	UiInput *form = &g->input.form;
	UiInput_Init(form, &g->app->ui, &g->app->ime);
	g->input.name = UiInput_Add(form, Ui_Id("gl.in.name", 0, 0),
		63, 0, NULL, L"输入名称");
	g->input.pass = UiInput_Add(form, Ui_Id("gl.in.pass", 0, 0),
		63, 1, NULL, L"输入密码");
	g->input.pv = UiInput_Add(form, Ui_Id("gl.in.pv", 0, 0),
		32, 0, NULL, L"预览字段");
	Edit_SetTextUtf8(UiInput_Edit(form, g->input.name), "CUI 画廊");
	Edit_SetImeEnabled(UiInput_Edit(form, g->input.pass), 0);
	UiSlider_Init(&g->input.maxLen, Ui_Id("gl.in.maxlen", 0, 0), 0.5f);
	UiCheckbox_Init(&g->input.pwd, Ui_Id("gl.in.pwd", 0, 0), 0);
	UiCheckbox_Init(&g->input.allow, Ui_Id("gl.in.allow", 0, 0), 0);
}

static void input_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect r;
	char buf[160];

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "名称 (支持中文输入法)");
	Ui_StackSkip(&col, 4.0f);
	UiInput_BeginFrame(&g->input.form);
	UiInput_SetRect(&g->input.form, g->input.name, Ui_StackTake(&col, 40.0f));
	UiInput_Draw(&g->input.form, g->input.name);
	Ui_StackSkip(&col, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "密码 (输入法已禁用)");
	Ui_StackSkip(&col, 4.0f);
	UiInput_SetRect(&g->input.form, g->input.pass, Ui_StackTake(&col, 40.0f));
	UiInput_Draw(&g->input.form, g->input.pass);
	Ui_StackSkip(&col, 16.0f);

	r = Ui_StackTake(&col, 38.0f);
	if (Ui_Button(ui, Ui_Id("gl.in.submit", 0, 0),
			Ui_Rect(r.x, r.y, 110.0f, 36.0f), "提交",
			UI_BTN_PRIMARY, 1)) {
		char nm[128];
		Edit_TextUtf8(UiInput_Edit(&g->input.form, g->input.name),
			nm, (int)sizeof(nm));
		wsprintfA(buf, "已提交: %s", nm[0] ? nm : "(空)");
		set_status(g, buf);
		UiToast_Show(&g->toast, buf, UiFrame_Now(), 2000, UI_TOAST_SUCCESS);
	}
	Ui_StackSkip(&col, 14.0f);

	{
		int n = UiInput_Edit(&g->input.form, g->input.name)->len;
		wsprintfA(buf, "名称长度: %d 字符 (Tab 切换字段, Esc 清焦点, 回车提交)",
			n);
		note_txt(gfx, t, Ui_StackTakeFill(&col), buf);
	}
}

static void input_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiInput_Init(&form, &app->ui, &app->ime);\n"
			"int f = UiInput_Add(&form, Ui_Id(\"in.name\", 0, 0),\n"
			"    %d,        /* maxLen, 0 = 不限 */\n"
			"    %d,        /* password: 1 禁输入法 */\n"
			"    NULL, L\"姓名\");\n"
			"/* 每帧: */\n"
			"UiInput_BeginFrame(&form);\n"
			"UiInput_SetRect(&form, f, rc);\n"
			"UiInput_Draw(&form, f);\n"
			"/* 事件: 壳回调转发 + 内容 diff */\n"
			"on_key:   UiInput_Key(&form, hwnd, vk);\n"
			"on_chr:   UiInput_Char(&form, ch);\n"
			"on_ime:   UiInput_OnIme(&form);\n"
			"on_timer: UiInput_Blink(&form);\n",
			(int)(g->input.maxLen.value * 32.0f + 0.5f),
			g->input.pwd.checked ? 1 : 0);
	}
	UiEdit *e = UiInput_Edit(&g->input.form, g->input.pv);
	char buf[96];
	int maxlen;

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "最大长度 (0 = 不限)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->input.maxLen, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	maxlen = (int)(g->input.maxLen.value * 32.0f + 0.5f);
	e->maxLen = maxlen;
	Ui_StackSkip(&col, 8.0f);
	if (maxlen)
		wsprintfA(buf, "maxLen = %d", maxlen);
	else
		wsprintfA(buf, "maxLen = 0 (不限长)");
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 12.0f);

	if (Ui_Checkbox(&g->input.pwd, ui, Ui_StackTake(&col, 26.0f),
			"密码掩码", 1))
		e->password = g->input.pwd.checked;
	Ui_StackSkip(&col, 6.0f);
	if (Ui_Checkbox(&g->input.allow, ui, Ui_StackTake(&col, 26.0f),
			"只允许数字", 1))
		e->allow = g->input.allow.checked ? "0123456789" : NULL;
	Ui_StackSkip(&col, 10.0f);

	small_txt(gfx, t, Ui_StackTake(&col, 18.0f),
		"改完设置后输入几个字符试试。");

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"实时预览");
		UiInput_BeginFrame(&g->input.form);
		UiInput_SetRect(&g->input.form, g->input.pv,
			Ui_Rect(area.x, area.y + 34.0f, area.w, 40.0f));
		UiInput_Draw(&g->input.form, g->input.pv);
	}
}

static int input_key(Gallery *g, UINT vk)
{
	if (UiInput_Key(&g->input.form, g->app->hwnd, vk)) return 1;
	return 0;
}

static int input_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->input.form, ch);
}

static void input_ime(Gallery *g)
{
	UiInput_OnIme(&g->input.form);
}

static void input_blink(Gallery *g)
{
	UiInput_Blink(&g->input.form);
}

static void input_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->input.form, g->app, msg, x, y);
}

/* ── TextArea ──────────────────────────────────────────────────────── */

static void ta_buf_reserve(Gallery *g, int need)
{
	int nc;
	char *np;
	if (need <= g->ta.cap) return;
	nc = g->ta.cap > 0 ? g->ta.cap : 256;
	while (nc < need) nc *= 2;
	np = g->ta.buf
		? (char *)HeapReAlloc(GetProcessHeap(), 0, g->ta.buf, (SIZE_T)nc)
		: (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)nc);
	if (!np) return;
	g->ta.buf = np;
	g->ta.cap = nc;
}

static void ta_buf_puts(Gallery *g, const char *s)
{
	int n = lstrlenA(s);
	ta_buf_reserve(g, g->ta.len + n + 1);
	if (!g->ta.buf) return;
	CopyMemory(g->ta.buf + g->ta.len, s, (SIZE_T)n + 1);
	g->ta.len += n;
}

static void ta_init(Gallery *g)
{
	UiTextArea_Init(&g->ta.ta, Ui_Id("gl.ta", 0, 0), 0);
	UiTextArea_SetPlaceholder(&g->ta.ta,
		L"输入多行文本: 软换行 / 选区 / 剪贴板 / 中文输入法都可用…");
	UiTextArea_AttachIme(&g->ta.ta, &g->app->ime);
	g->ta.len = 0;
	ta_buf_puts(g, "CUI TextArea 动态容量演示。\n"
		"末尾追加只排版新字符; 普通编辑只重排当前段落;\n"
		"插入换行或宽度变化才全文重排。\n");
	UiTextArea_SetTextUtf8(&g->ta.ta, g->ta.buf);
	UiTextArea_SetCaret(&g->ta.ta, g->ta.len);
	UiCheckbox_Init(&g->ta.ro, Ui_Id("gl.ta.ro", 0, 0), 0);
}

static void ta_shutdown(Gallery *g)
{
	UiTextArea_Shutdown(&g->ta.ta);
	if (g->ta.buf) {
		HeapFree(GetProcessHeap(), 0, g->ta.buf);
		g->ta.buf = NULL;
		g->ta.len = g->ta.cap = 0;
	}
}

static void ta_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect taRc, row;
	char buf[128];

	Ui_StackBegin(&col, rc, UI_COLUMN, 0.0f, 0.0f);
	row = Ui_StackTake(&col, 34.0f);
	Ui_StackSkip(&col, 10.0f);
	taRc = Ui_StackTakeFill(&col);

	if (Ui_TextArea(&g->ta.ta, ui, taRc, 1) && g->ta.ta.len != g->ta.len) {
		/* 组件内部编辑: 回读到缓冲, 保持一致 */
		ta_buf_reserve(g, g->ta.ta.len * 3 + 16);
		if (g->ta.buf) {
			UiTextArea_TextUtf8(&g->ta.ta, g->ta.buf, g->ta.cap);
			g->ta.len = lstrlenA(g->ta.buf);
		}
	}

	if (Ui_Button(ui, Ui_Id("gl.ta.append", 0, 0),
			Ui_Rect(row.x, row.y, 128.0f, 32.0f), "追加 200 字符",
			UI_BTN_PRIMARY, g->ta.len < 48000)) {
		int i;
		ta_buf_puts(g, "追加的内容块: 动态缓冲按需倍增，超过旧上限也不截断。\n");
		for (i = 0; i < 8; i++)
			ta_buf_puts(g, "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklm\n");
		UiTextArea_SetTextUtf8(&g->ta.ta, g->ta.buf);
		UiTextArea_SetCaret(&g->ta.ta, g->ta.len);
		set_status(g, "已追加 (增量布局只排新字符)");
	}
	if (Ui_Button(ui, Ui_Id("gl.ta.clear", 0, 0),
			Ui_Rect(row.x + 140.0f, row.y, 96.0f, 32.0f), "清空",
			UI_BTN_SECONDARY, 1)) {
		g->ta.len = 0;
		if (g->ta.buf) g->ta.buf[0] = 0;
		UiTextArea_SetTextUtf8(&g->ta.ta, "");
		set_status(g, "已清空");
	}
	{
		wsprintfA(buf, "字符 %d · 行 %d · 容量 %d 字节",
			g->ta.ta.len, g->ta.ta.lineCount, g->ta.cap);
		small_txt(gfx, t,
			Ui_Rect(row.x + 252.0f, row.y, row.w - 252.0f, row.h), buf);
	}
}

static void ta_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiTextArea ta;   /* 状态常驻应用 */\n"
			"UiTextArea_SetTextUtf8(&ta, initText);\n"
			"UiTextArea_SetReadonly(&ta, %d);\n"
			"\n"
			"if (Ui_TextArea(&ta, ui, rc, 1)) {\n"
			"    /* 返回 1: 内部发生了编辑 */\n"
			"    UiTextArea_TextUtf8(&ta, buf, cap);\n"
			"    dirty = 1;\n"
			"}\n"
			"UiTextArea_Key(&ta, ui, vk);\n"
			"UiTextArea_OnIme(&ta, ui);\n"
			"UiTextArea_Blink(&ta, ui);\n",
			g->ta.ro.checked ? 1 : 0);
	}
	char buf[96];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	if (Ui_Checkbox(&g->ta.ro, ui, Ui_StackTake(&col, 26.0f),
			"只读 (readonly)", 1))
		UiTextArea_SetReadonly(&g->ta.ta, g->ta.ro.checked);
	Ui_StackSkip(&col, 10.0f);
	wsprintfA(buf, "字符 %d · 行 %d", g->ta.ta.len, g->ta.ta.lineCount);
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 10.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"右侧预览与演示页是同一个实例 —— 切标签页不会丢内容。\n"
		"只读态仍可选择、复制, 但不接受输入。");

	Ui_TextArea(&g->ta.ta, ui, pane_in(pv, 16.0f), 1);
}

static int ta_key(Gallery *g, UINT vk)
{
	return UiTextArea_Key(&g->ta.ta, &g->app->ui, vk);
}

static int ta_chr(Gallery *g, wchar_t ch)
{
	return UiTextArea_Char(&g->ta.ta, &g->app->ui, ch);
}

static void ta_ime(Gallery *g)
{
	UiTextArea_OnIme(&g->ta.ta, &g->app->ui);
}

static void ta_blink(Gallery *g)
{
	UiTextArea_Blink(&g->ta.ta, &g->app->ui);
}

static int ta_anim(Gallery *g)
{
	return UiTextArea_Busy(&g->ta.ta);
}

/* ── Checkbox ──────────────────────────────────────────────────────── */

static const char *const kFeatureLabels[3] = {
	"开机启动", "自动检查更新", "发送使用统计"
};

static void checks_init(Gallery *g)
{
	UiCheckbox_Init(&g->checks.a, Ui_Id("gl.ck.a", 0, 0), 1);
	UiCheckbox_Init(&g->checks.b, Ui_Id("gl.ck.b", 0, 0), 1);
	g->checks.b.indeterminate = 1;
	UiCheckbox_Init(&g->checks.c, Ui_Id("gl.ck.c", 0, 0), 1);
	UiCheckbox_Init(&g->checks.pv, Ui_Id("gl.ck.pv", 0, 0), 1);
	UiCheckboxGroup_Init(&g->checks.grp, Ui_Id("gl.ck.grp", 0, 0), 3);
	UiCheckboxGroup_SetMask(&g->checks.grp, 0x1u);
	UiCheckbox_Init(&g->checks.dis, Ui_Id("gl.ck.dis", 0, 0), 0);
	UiCheckbox_Init(&g->checks.ind, Ui_Id("gl.ck.ind", 0, 0), 0);
	UiInput_Init(&g->checks.form, &g->app->ui, &g->app->ime);
	g->checks.fld = UiInput_Add(&g->checks.form,
		Ui_Id("gl.ck.label", 0, 0), 31, 0, NULL, L"标签文字");
	Edit_SetTextUtf8(UiInput_Edit(&g->checks.form, g->checks.fld), "启用通知");
}

static void checks_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	char buf[96];

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "单项 (普通 / 部分选中 / 禁用)");
	Ui_StackSkip(&col, 6.0f);
	if (Ui_Checkbox(&g->checks.a, ui,
			Ui_StackTake(&col, 28.0f), "启用通知", 1))
		set_status(g, g->checks.a.checked ? "通知已启用" : "通知已关闭");
	Ui_StackSkip(&col, 4.0f);
	if (Ui_Checkbox(&g->checks.b, ui,
			Ui_StackTake(&col, 28.0f), "部分应用", 1))
		set_status(g, g->checks.b.indeterminate ? "部分选中"
			: (g->checks.b.checked ? "全部应用" : "未选择"));
	Ui_StackSkip(&col, 4.0f);
	Ui_Checkbox(&g->checks.c, ui, Ui_StackTake(&col, 28.0f), "禁用项", 0);
	Ui_StackSkip(&col, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "复选组 (mask 位图)");
	Ui_StackSkip(&col, 6.0f);
	if (UiCheckboxGroup_Draw(&g->checks.grp, ui, Ui_StackTake(&col, 92.0f),
			kFeatureLabels, 28.0f, 1)) {
		wsprintfA(buf, "功能 mask = 0x%X", g->checks.grp.mask);
		set_status(g, buf);
	}
	Ui_StackSkip(&col, 12.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"部分选中(indeterminate)点一次变成全选。\n"
		"「属性」页可以改预览项的标签 / 禁用 / 部分选中状态。");
}

static void checks_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		char lbl2[64];
		lbl2[0] = 0;
		Edit_TextUtf8(UiInput_Edit(&g->checks.form, g->checks.fld),
			lbl2, (int)sizeof(lbl2));
		if (!lbl2[0]) lstrcpynA(lbl2, "选项", (int)sizeof(lbl2));
		snprintf(g->codeText, sizeof(g->codeText),
			"UiCheckbox cb;\n"
			"UiCheckbox_Init(&cb, Ui_Id(\"cb.x\", 0, 0), 0);\n"
			"cb.indeterminate = %d;        /* 部分选中 */\n"
			"if (Ui_Checkbox(&cb, ui, rc, \"%s\", %s)) {\n"
			"    /* 翻转当帧返回 1; 三态逻辑在应用层 */\n"
			"    opt = cb.checked;\n"
			"}\n",
			g->checks.ind.checked ? 1 : 0,
			lbl2,
			g->checks.dis.checked ? "0" : "1");
	}
	char label[64];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "标签文字");
	Ui_StackSkip(&col, 4.0f);
	UiInput_BeginFrame(&g->checks.form);
	UiInput_SetRect(&g->checks.form, g->checks.fld, Ui_StackTake(&col, 36.0f));
	UiInput_Draw(&g->checks.form, g->checks.fld);
	Ui_StackSkip(&col, 12.0f);

	Ui_Checkbox(&g->checks.ind, ui, Ui_StackTake(&col, 26.0f),
		"部分选中 (indeterminate)", 1);
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->checks.dis, ui, Ui_StackTake(&col, 26.0f),
		"禁用", 1);
	Ui_StackSkip(&col, 8.0f);
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f),
		"下面的预览项可以点选切换。");

	label[0] = 0;
	if (UiInput_Edit(&g->checks.form, g->checks.fld))
		Edit_TextUtf8(UiInput_Edit(&g->checks.form, g->checks.fld),
			label, (int)sizeof(label));
	if (!label[0]) lstrcpynA(label, "选项", (int)sizeof(label));

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f), "实时预览");
		g->checks.pv.indeterminate = g->checks.ind.checked;
		Ui_Checkbox(&g->checks.pv, ui,
			Ui_Rect(area.x, area.y + 34.0f, area.w, 28.0f), label,
			!g->checks.dis.checked);
	}
}

static int checks_key(Gallery *g, UINT vk)
{
	if (UiInput_Key(&g->checks.form, g->app->hwnd, vk)) return 1;
	if (g->app->ui.navId == g->checks.grp.id)
		return UiCheckboxGroup_Key(&g->checks.grp, &g->app->ui, vk);
	return 0;
}

static int checks_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->checks.form, ch);
}

static void checks_ime(Gallery *g)
{
	UiInput_OnIme(&g->checks.form);
}

static void checks_blink(Gallery *g)
{
	UiInput_Blink(&g->checks.form);
}

static void checks_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->checks.form, g->app, msg, x, y);
}

/* ── Radio ─────────────────────────────────────────────────────────── */

static const char *const kModeLabels[3] = {
	"标准模式", "节能模式", "高性能模式"
};

static void radio_init(Gallery *g)
{
	UiRadioGroup_Init(&g->radio.rg, Ui_Id("gl.rg", 0, 0), 3);
	UiRadioGroup_SetSelected(&g->radio.rg, 0);
	UiRadioGroup_Init(&g->radio.pv, Ui_Id("gl.rg.pv", 0, 0), 3);
	UiRadioGroup_SetSelected(&g->radio.pv, 1);
	UiCheckbox_Init(&g->radio.dis1, Ui_Id("gl.rg.d1", 0, 0), 0);
	UiCheckbox_Init(&g->radio.dis2, Ui_Id("gl.rg.d2", 0, 0), 0);
}

static void radio_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "电源模式 (互斥单选)");
	Ui_StackSkip(&col, 6.0f);
	if (UiRadioGroup_Draw(&g->radio.rg, ui, Ui_StackTake(&col, 92.0f),
			kModeLabels, 28.0f, 1)) {
		set_status(g, kModeLabels[g->radio.rg.selected]);
	}
	Ui_StackSkip(&col, 12.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"选项 ≤5 个且需要一眼看全时用 RadioGroup;\n"
		"更多选项或空间紧张时用 Select。\n"
		"「属性」页可以禁用任意项并实时预览。");
}

static void radio_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiRadioGroup rg;\n"
			"UiRadioGroup_Init(&rg, Ui_Id(\"rg.x\", 0, 0), 0);\n"
			"UiRadioGroup_SetDisabledMask(&rg, 0x%X);\n"
			"\n"
			"if (UiRadioGroup_Draw(&rg, ui, rc, kLabels,\n"
			"        28.0f, 1)) {\n"
			"    /* changed: selected 已更新 */\n"
			"    mode = rg.selected;\n"
			"}\n",
			(unsigned)((g->radio.dis1.checked ? 1u << 1 : 0u) |
				(g->radio.dis2.checked ? 1u << 2 : 0u)));
	}
	unsigned dis;

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	if (Ui_Checkbox(&g->radio.dis1, ui, Ui_StackTake(&col, 26.0f),
			"禁用第 2 项 (节能模式)", 1))
		;
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->radio.dis2, ui, Ui_StackTake(&col, 26.0f),
		"禁用第 3 项 (高性能模式)", 1);
	Ui_StackSkip(&col, 10.0f);

	dis = (unsigned)((g->radio.dis1.checked ? 1u << 1 : 0u) |
		(g->radio.dis2.checked ? 1u << 2 : 0u));
	UiRadioGroup_SetDisabledMask(&g->radio.pv, dis);

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"实时预览 (可点击切换)");
		UiRadioGroup_Draw(&g->radio.pv, ui,
			Ui_Rect(area.x, area.y + 30.0f, area.w, 92.0f),
			kModeLabels, 28.0f, 1);
		if (g->radio.pv.changed)
			set_status(g, kModeLabels[g->radio.pv.selected]);
	}
}

static int radio_key(Gallery *g, UINT vk)
{
	if (g->app->ui.navId == g->radio.rg.id)
		return UiRadioGroup_Key(&g->radio.rg, &g->app->ui, vk);
	return 0;
}

/* ── Switch ────────────────────────────────────────────────────────── */

static void sw_init(Gallery *g)
{
	UiSwitch_Init(&g->sw.s1, Ui_Id("gl.sw.1", 0, 0), 1);
	UiSwitch_Init(&g->sw.s2, Ui_Id("gl.sw.2", 0, 0), 0);
	UiSwitch_Init(&g->sw.pv, Ui_Id("gl.sw.pv", 0, 0), 1);
	UiCheckbox_Init(&g->sw.dis, Ui_Id("gl.sw.dis", 0, 0), 0);
}

static void sw_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	if (Ui_Switch(&g->sw.s1, ui, Ui_StackTake(&col, 30.0f), "Wi-Fi", 1))
		set_status(g, g->sw.s1.on ? "Wi-Fi 已开启" : "Wi-Fi 已关闭");
	Ui_StackSkip(&col, 10.0f);
	if (Ui_Switch(&g->sw.s2, ui, Ui_StackTake(&col, 30.0f), "自动检查更新", 1))
		set_status(g, g->sw.s2.on ? "自动更新已开启" : "自动更新已关闭");
	Ui_StackSkip(&col, 16.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"开关改变立即生效, 不需要\"保存\"按钮。\n"
		"「属性」页可以预览禁用态。");
}

static void sw_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiSwitch s;\n"
			"UiSwitch_Init(&s, Ui_Id(\"sw.x\", 0, 0), %d);\n"
			"if (Ui_Switch(&s, ui, rc, \"自动更新\", %s))\n"
			"    opt = s.on;      /* 翻转当帧返回 1 */\n",
			g->sw.pv.on ? 1 : 0,
			g->sw.dis.checked ? "0" : "1");
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	Ui_Checkbox(&g->sw.dis, ui, Ui_StackTake(&col, 26.0f), "禁用", 1);
	Ui_StackSkip(&col, 10.0f);
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f),
		"预览开关可以直接点按切换。");

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f), "实时预览");
		Ui_Switch(&g->sw.pv, ui,
			Ui_Rect(area.x, area.y + 34.0f, area.w, 30.0f),
			"预览开关", !g->sw.dis.checked);
		if (g->sw.pv.changed)
			set_status(g, g->sw.pv.on ? "预览开关: 开" : "预览开关: 关");
	}
}

static int sw_key(Gallery *g, UINT vk)
{
	if (g->app->ui.navId == g->sw.s1.id)
		return UiSwitch_Key(&g->sw.s1, &g->app->ui, vk);
	if (g->app->ui.navId == g->sw.s2.id)
		return UiSwitch_Key(&g->sw.s2, &g->app->ui, vk);
	return 0;
}

/* ── Slider ────────────────────────────────────────────────────────── */

static void slider_init(Gallery *g)
{
	UiSlider_Init(&g->slider.sl, Ui_Id("gl.sl", 0, 0), 0.65f);
}

static void slider_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	char buf[64];

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "音量 (拖动 / 点击轨道)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->slider.sl, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 10.0f);
	wsprintfA(buf, "%d%%", (int)(g->slider.sl.value * 100.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "绑定到静态进度条");
	Ui_StackSkip(&col, 4.0f);
	Ui_ProgressBar(ui, Ui_StackTake(&col, 10.0f), g->slider.sl.value, 1);
	Ui_StackSkip(&col, 16.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"拖动中 changed 每帧置位, 松手 committed 置位一次 ——\n"
		"预览用 changed, 持久化用 committed。\n"
		"键盘: Tab 聚焦后 ←/→ 微调, Home/End 到边界。");
}

static void slider_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiSlider sl;\n"
			"UiSlider_Init(&sl, Ui_Id(\"sl.x\", 0, 0), %.2ff);\n"
			"if (Ui_Slider(&sl, ui, rc, NULL, 1)) {\n"
			"    /* 拖动/键盘/点击轨道都置 changed */\n"
			"    v = (int)(sl.value * 100.0f + 0.5f);\n"
			"    if (sl.committed)   /* 松手才提交 */\n"
			"        apply(v);\n"
			"}\n",
			g->slider.sl.value);
	}
	char buf[96];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "当前值");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->slider.sl, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 10.0f);
	wsprintfA(buf, "value = %d%% (0..1 内部表示)",
		(int)(g->slider.sl.value * 100.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 12.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"右侧预览是同一个滑块实例的放大视图。\n"
		"抓住拇指边缘按下不会跳位 (抓取偏移记忆), 拖出控件仍 1:1 跟手。");

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f), "实时预览");
		Ui_Slider(&g->slider.sl, ui,
			Ui_Rect(area.x, area.y + 40.0f, area.w, 34.0f), NULL, 1);
		Ui_ProgressBar(ui, Ui_Rect(area.x, area.y + 92.0f, area.w, 10.0f),
			g->slider.sl.value, 1);
	}
}

static int slider_key(Gallery *g, UINT vk)
{
	if (g->app->ui.navId == g->slider.sl.id) {
		if (UiSlider_Key(&g->slider.sl, &g->app->ui, vk)) {
			UiApp_Invalidate(g->app);
			return 1;
		}
	}
	return 0;
}

/* ── Select ────────────────────────────────────────────────────────── */

static const char *const kSelThemeItems[3] = {
	"浅色主题", "深色主题", "跟随系统"
};
static const char *const kCityItems[8] = {
	"北京", "上海", "广州", "深圳", "杭州", "南京", "成都", "西安"
};
static const char *const kTagItems[10] = {
	"UI", "Rendering", "Animation", "Input", "Layout",
	"ScrollView", "Theme", "Icons", "IME", "D3D11"
};
static const int kSelCounts[3] = { 3, 6, 12 };
static const char *const kSelCntLabels[3] = { "3 项", "6 项", "12 项" };

static char g_selItemBuf[12][24];
static const char *g_selItems[12];

static void sel_init(Gallery *g)
{
	UiSelect *s;
	int i;
	for (i = 0; i < 12; i++) {
		wsprintfA(g_selItemBuf[i], "选项 %02d", i + 1);
		g_selItems[i] = g_selItemBuf[i];
	}

	s = &g->sel.single;
	UiSelect_Init(s, Ui_Id("gl.sel.single", 0, 0));
	UiSelect_SetItems(s, kSelThemeItems, 3);
	UiSelect_SetPlaceholder(s, "选择主题");

	s = &g->sel.multi;
	UiSelect_Init(s, Ui_Id("gl.sel.multi", 0, 0));
	UiSelect_SetItems(s, kCityItems, 8);
	UiSelect_SetMultiple(s, 1);
	UiSelect_SetMask(s, (1u << 4) | (1u << 6));
	UiSelect_SetDisabledMask(s, 1u << 3);
	UiSelect_SetPlaceholder(s, "选择城市（可多选）");

	s = &g->sel.filter;
	UiSelect_Init(s, Ui_Id("gl.sel.filter", 0, 0));
	UiSelect_SetItems(s, kTagItems, 10);
	UiSelect_SetMultiple(s, 1);
	UiSelect_SetFilterable(s, 1);
	UiSelect_SetMask(s, (1u << 0) | (1u << 4));
	UiSelect_SetPlaceholder(s, "选择技术标签");
	UiSelect_SetSearchHint(s, L"搜索标签…");
	UiSelect_AttachIme(s, &g->app->ime);

	s = &g->sel.pv;
	UiSelect_Init(s, Ui_Id("gl.sel.pv", 0, 0));
	UiSelect_SetItems(s, g_selItems, 6);
	UiSelect_SetPlaceholder(s, "预览下拉");
	UiSelect_AttachIme(s, &g->app->ime);
	g->sel.pvCfg = 6;

	UiButtonGroup_Init(&g->sel.cnt, Ui_Id("gl.sel.cnt", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->sel.cnt, 1);
	UiCheckbox_Init(&g->sel.multiCb, Ui_Id("gl.sel.multi.c", 0, 0), 0);
	UiCheckbox_Init(&g->sel.filterCb, Ui_Id("gl.sel.filter.c", 0, 0), 0);
	UiCheckbox_Init(&g->sel.dis0, Ui_Id("gl.sel.dis0", 0, 0), 0);
}

static void sel_consume(Gallery *g, UiSelect *s, const char *name)
{
	char buf[192];
	if (!s->changed) return;
	s->changed = 0;
	if (s->multiple) {
		int i, n = 0;
		for (i = 0; i < s->count && i < 32; i++)
			if (s->mask & (1u << i)) n++;
		wsprintfA(buf, "%s: 已选 %d 项", name, n);
	} else {
		const char *v = (s->selected >= 0 && s->selected < s->count &&
			s->items[s->selected]) ? s->items[s->selected] : "(未选择)";
		wsprintfA(buf, "%s: %s", name, v);
	}
	set_status(g, buf);
	if (s == &g->sel.single && s->selected >= 0) {
		if (s->selected == 1)
			UiTheme_SetTheme(&g->app->theme, UI_THEME_DARK);
		else if (s->selected == 0)
			UiTheme_SetTheme(&g->app->theme, UI_THEME_LIGHT);
	}
	UiApp_Invalidate(g->app);
}

static void sel_apply_pv(Gallery *g)
{
	int cfg = kSelCounts[g->sel.cnt.selected] |
		(g->sel.multiCb.checked ? 0x100 : 0) |
		(g->sel.filterCb.checked ? 0x200 : 0) |
		(g->sel.dis0.checked ? 0x400 : 0);
	if (cfg == g->sel.pvCfg) return;
	g->sel.pvCfg = cfg;
	UiSelect_SetItems(&g->sel.pv, g_selItems, kSelCounts[g->sel.cnt.selected]);
	UiSelect_SetMultiple(&g->sel.pv, g->sel.multiCb.checked);
	UiSelect_SetFilterable(&g->sel.pv, g->sel.filterCb.checked);
	UiSelect_SetDisabledMask(&g->sel.pv, g->sel.dis0.checked ? 1u : 0u);
}

static int sel_count_of(const UiSelect *s)
{
	int i, n = 0;
	for (i = 0; i < s->count && i < 32; i++)
		if (s->mask & (1u << i)) n++;
	return n;
}

static void sel_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	char buf[192];

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"单选 (选中项会切换全局主题)");
	Ui_StackSkip(&col, 6.0f);
	Ui_Select(&g->sel.single, &g->pm, ui, Ui_StackTake(&col, 38.0f),
		NULL, 1);
	sel_consume(g, &g->sel.single, "主题");
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "多选 (第 4 项禁用)");
	Ui_StackSkip(&col, 6.0f);
	Ui_Select(&g->sel.multi, &g->pm, ui, Ui_StackTake(&col, 38.0f),
		NULL, 1);
	sel_consume(g, &g->sel.multi, "城市");
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"可搜索多选 (输入字母过滤, Enter 切换, Backspace 回退)");
	Ui_StackSkip(&col, 6.0f);
	Ui_Select(&g->sel.filter, &g->pm, ui, Ui_StackTake(&col, 38.0f),
		NULL, 1);
	sel_consume(g, &g->sel.filter, "标签");
	Ui_StackSkip(&col, 14.0f);

	wsprintfA(buf, "城市: %d 项    标签: %d 项    (Esc / 点击外部关闭, 焦点回归触发器)",
		sel_count_of(&g->sel.multi), sel_count_of(&g->sel.filter));
	note_txt(gfx, t, Ui_StackTakeFill(&col), buf);
}

static void sel_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiSelect s;\n"
			"UiSelect_Init(&s, Ui_Id(\"sel.x\", 0, 0));\n"
			"UiSelect_SetItems(&s, items, %d);\n"
			"UiSelect_SetMultiple(&s, %d);\n"
			"UiSelect_SetFilterable(&s, %d);\n"
			"UiSelect_SetDisabledMask(&s, 0x%X);\n"
			"\n"
			"/* 每帧触发器: 返回 1 = 选中/收起变化 */\n"
			"if (Ui_Select(&s, &pm, ui, rc, NULL, 1))\n"
			"    city = s.selected;   /* 多选读 s.mask */\n"
			"UiSelect_DrawPopup(&s, &pm, ui);   /* 帧末 */\n"
			"/* 弹层打开时: Key/Char/OnIme/Blink 转发 */\n",
			kSelCounts[g->sel.cnt.selected],
			g->sel.multiCb.checked ? 1 : 0,
			g->sel.filterCb.checked ? 1 : 0,
			g->sel.dis0.checked ? 1u : 0u);
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "选项数量");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->sel.cnt, Ui_StackTake(&col, 34.0f),
		kSelCntLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	Ui_Checkbox(&g->sel.multiCb, ui, Ui_StackTake(&col, 26.0f),
		"多选模式 (mask)", 1);
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->sel.filterCb, ui, Ui_StackTake(&col, 26.0f),
		"顶部搜索框 (可过滤)", 1);
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->sel.dis0, ui, Ui_StackTake(&col, 26.0f),
		"禁用第 1 项", 1);
	Ui_StackSkip(&col, 10.0f);

	sel_apply_pv(g);

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"实时预览 (点开试试)");
		Ui_Select(&g->sel.pv, &g->pm, ui,
			Ui_Rect(area.x, area.y + 32.0f, area.w, 38.0f), NULL, 1);
		sel_consume(g, &g->sel.pv, "预览");
	}
}

static int sel_keys(Gallery *g, UINT vk)
{
	UiApp *app = g->app;
	if (UiSelect_Key(&g->sel.single, &g->pm, &app->ui, vk)) {
		sel_consume(g, &g->sel.single, "主题");
		return 1;
	}
	if (UiSelect_Key(&g->sel.multi, &g->pm, &app->ui, vk)) {
		sel_consume(g, &g->sel.multi, "城市");
		return 1;
	}
	if (UiSelect_Key(&g->sel.filter, &g->pm, &app->ui, vk)) {
		sel_consume(g, &g->sel.filter, "标签");
		return 1;
	}
	if (UiSelect_Key(&g->sel.pv, &g->pm, &app->ui, vk)) {
		sel_consume(g, &g->sel.pv, "预览");
		return 1;
	}
	return 0;
}

static int sel_key(Gallery *g, UINT vk)
{
	return sel_keys(g, vk);
}

static int sel_chr(Gallery *g, wchar_t ch)
{
	UiApp *app = g->app;
	if (UiSelect_Char(&g->sel.filter, &app->ui, ch)) return 1;
	if (UiSelect_Char(&g->sel.pv, &app->ui, ch)) return 1;
	return 0;
}

static void sel_ime(Gallery *g)
{
	UiApp *app = g->app;
	UiSelect_OnIme(&g->sel.filter, &app->ui);
	UiSelect_OnIme(&g->sel.pv, &app->ui);
}

static void sel_blink(Gallery *g)
{
	UiApp *app = g->app;
	UiSelect_Blink(&g->sel.filter, &app->ui);
	UiSelect_Blink(&g->sel.pv, &app->ui);
}

static void sel_popups(Gallery *g)
{
	UiApp *app = g->app;
	UiSelect_DrawPopup(&g->sel.single, &g->pm, &app->ui);
	sel_consume(g, &g->sel.single, "主题");
	UiSelect_DrawPopup(&g->sel.multi, &g->pm, &app->ui);
	sel_consume(g, &g->sel.multi, "城市");
	UiSelect_DrawPopup(&g->sel.filter, &g->pm, &app->ui);
	sel_consume(g, &g->sel.filter, "标签");
	UiSelect_DrawPopup(&g->sel.pv, &g->pm, &app->ui);
	sel_consume(g, &g->sel.pv, "预览");
}

static int sel_anim(Gallery *g)
{
	return UiSelect_Busy(&g->sel.single) || UiSelect_Busy(&g->sel.multi) ||
		UiSelect_Busy(&g->sel.filter) || UiSelect_Busy(&g->sel.pv);
}

static void sel_shutdown(Gallery *g)
{
	UiSelect_Shutdown(&g->sel.single);
	UiSelect_Shutdown(&g->sel.multi);
	UiSelect_Shutdown(&g->sel.filter);
	UiSelect_Shutdown(&g->sel.pv);
}

/* ── DatePicker ────────────────────────────────────────────────────── */

static const char *const kDpModeLabels[4] = {
	"日期", "日期+时间", "日期范围", "范围+时间"
};
static const char *const kDpWeekLabels[2] = { "周一开始", "周日开始" };

static void dp_init(Gallery *g)
{
	UiApp *app = g->app;
	UiDateTime now, min, max;

	UiDatePicker_Init(&g->dp.dp, Ui_Id("gl.dp.dp", 0, 0), UI_DP_DATE);
	UiDatePicker_SetPlaceholder(&g->dp.dp, "选择日期");
	UiDatePicker_AttachIme(&g->dp.dp, &app->ime);
	UiDate_Now(&now);
	min = now;
	UiDate_AddDays(&min, -30);
	max = now;
	UiDate_AddDays(&max, 30);
	UiDatePicker_SetBounds(&g->dp.dp, &min, &max);

	UiDatePicker_Init(&g->dp.dt, Ui_Id("gl.dp.dt", 0, 0), UI_DP_DATETIME);
	UiDatePicker_SetPlaceholder(&g->dp.dt, "选择日期时间");
	UiDatePicker_SetShowSeconds(&g->dp.dt, 1);
	UiDatePicker_AttachIme(&g->dp.dt, &app->ime);

	UiDatePicker_Init(&g->dp.rng, Ui_Id("gl.dp.rng", 0, 0), UI_DP_RANGE_DATE);
	UiDatePicker_SetPlaceholder(&g->dp.rng, "选择范围");
	UiDatePicker_AttachIme(&g->dp.rng, &app->ime);

	UiButtonGroup_Init(&g->dp.mode, Ui_Id("gl.dp.mode", 0, 0),
		UI_BG_SINGLE, 4);
	UiButtonGroup_SetSelected(&g->dp.mode, 0);
	UiButtonGroup_Init(&g->dp.weekStart, Ui_Id("gl.dp.week", 0, 0),
		UI_BG_SINGLE, 2);
	UiButtonGroup_SetSelected(&g->dp.weekStart, 1);
	UiCheckbox_Init(&g->dp.seconds, Ui_Id("gl.dp.sec", 0, 0), 0);
	g->dp.weekApplied = 1;
}

static void dp_consume(Gallery *g, UiDatePicker *d, const char *name)
{
	char a[48], b[48], buf[160];
	if (!d->changed && !d->committed) return;
	d->changed = 0;
	d->committed = 0;
	if (d->mode >= UI_DP_RANGE_DATE) {
		if (d->hasValue)
			UiDate_Format(&d->value, "YYYY-MM-DD", a, (int)sizeof(a));
		else
			lstrcpynA(a, "?", (int)sizeof(a));
		if (d->hasEnd)
			UiDate_Format(&d->end, "YYYY-MM-DD", b, (int)sizeof(b));
		else
			lstrcpynA(b, "?", (int)sizeof(b));
		wsprintfA(buf, "%s: %s → %s", name, a, b);
	} else {
		if (d->hasValue)
			UiDate_Format(&d->value, "YYYY-MM-DD HH:mm", a, (int)sizeof(a));
		else
			lstrcpynA(a, "(未选)", (int)sizeof(a));
		wsprintfA(buf, "%s: %s", name, a);
	}
	set_status(g, buf);
}

static void dp_apply_props(Gallery *g)
{
	int mode = g->dp.mode.selected + 0;   /* 0..3 与 UI_DP_* 一致 */
	UiDatePicker_SetMode(&g->dp.dp, mode);
	UiDatePicker_SetShowSeconds(&g->dp.dp, g->dp.seconds.checked);
	if (g->dp.weekStart.selected != g->dp.weekApplied) {
		g->dp.weekApplied = g->dp.weekStart.selected;
		UiDatePicker_SetWeekStart(&g->dp.dp, g->dp.weekApplied);
	}
}

static void dp_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"纯日期 (限定 ±30 天, 越界禁用; 触发器可直接键入)");
	Ui_StackSkip(&col, 6.0f);
	Ui_DatePicker(&g->dp.dp, &g->pm, ui, Ui_StackTake(&col, 38.0f), NULL, 1);
	dp_consume(g, &g->dp.dp, "日期");
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "日期 + 时间 (含秒, 滚动列)");
	Ui_StackSkip(&col, 6.0f);
	Ui_DatePicker(&g->dp.dt, &g->pm, ui, Ui_StackTake(&col, 38.0f), NULL, 1);
	dp_consume(g, &g->dp.dt, "日期时间");
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"日期范围 (双联动月历, 两阶段点选 + 悬停预览)");
	Ui_StackSkip(&col, 6.0f);
	Ui_DatePicker(&g->dp.rng, &g->pm, ui, Ui_StackTake(&col, 38.0f), NULL, 1);
	dp_consume(g, &g->dp.rng, "范围");

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"键入非法时红框回退, 合法即提交; Esc / 点击外部关闭弹层。\n"
		"「属性」页可以切换预览选择器的四种模式。");
}

static void dp_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		static const char *const kDpModeNames[4] = {
			"UI_DP_DATE", "UI_DP_DATETIME",
			"UI_DP_RANGE_DATE", "UI_DP_RANGE_DATETIME"
		};
		snprintf(g->codeText, sizeof(g->codeText),
			"UiDatePicker d;\n"
			"UiDatePicker_Init(&d, Ui_Id(\"dp.x\", 0, 0), %s);\n"
			"UiDatePicker_SetShowSeconds(&d, %d);\n"
			"UiDatePicker_SetWeekStart(&d, %d);\n"
			"\n"
			"/* 每帧触发器: 返回 1 = 值变化 */\n"
			"if (Ui_DatePicker(&d, &pm, ui, rc, NULL, 1))\n"
			"    read(&d.value, &d.end);\n"
			"UiDatePicker_DrawPopup(&d, &pm, ui);   /* 帧末 */\n"
			"/* 弹层: Key/Char/OnIme/Blink 转发 */\n",
			kDpModeNames[g->dp.mode.selected],
			g->dp.seconds.checked ? 1 : 0,
			g->dp.weekStart.selected);
	}
	char buf[96];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "模式");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->dp.mode, Ui_StackTake(&col, 56.0f),
		kDpModeLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	Ui_Checkbox(&g->dp.seconds, ui, Ui_StackTake(&col, 26.0f),
		"时间列包含秒", 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "周起始");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->dp.weekStart, Ui_StackTake(&col, 34.0f),
		kDpWeekLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	dp_apply_props(g);

	{
		UiRect area = pane_in(pv, 20.0f);
		int mode = g->dp.mode.selected;
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"实时预览 (同一个选择器实例)");
		Ui_DatePicker(&g->dp.dp, &g->pm, ui,
			Ui_Rect(area.x, area.y + 32.0f, area.w, 38.0f), NULL, 1);
		dp_consume(g, &g->dp.dp, "预览");
		if (mode >= UI_DP_RANGE_DATE)
			lstrcpynA(buf, "范围模式: 两阶段点选 + 双联动月历",
				(int)sizeof(buf));
		else if (mode == UI_DP_DATETIME)
			lstrcpynA(buf, "时间模式: 时/分(秒)滚动列", (int)sizeof(buf));
		else
			lstrcpynA(buf, "日期模式: 月历点选或键入", (int)sizeof(buf));
		small_txt(gfx, t,
			Ui_Rect(area.x, area.y + 84.0f, area.w, 18.0f), buf);
	}
}

static int dp_keys(Gallery *g, UINT vk)
{
	UiApp *app = g->app;
	if (UiDatePicker_Key(&g->dp.dp, &g->pm, &app->ui, vk)) {
		dp_consume(g, &g->dp.dp, "日期");
		return 1;
	}
	if (UiDatePicker_Key(&g->dp.dt, &g->pm, &app->ui, vk)) {
		dp_consume(g, &g->dp.dt, "日期时间");
		return 1;
	}
	if (UiDatePicker_Key(&g->dp.rng, &g->pm, &app->ui, vk)) {
		dp_consume(g, &g->dp.rng, "范围");
		return 1;
	}
	return 0;
}

static int dp_key(Gallery *g, UINT vk)
{
	return dp_keys(g, vk);
}

static int dp_chr(Gallery *g, wchar_t ch)
{
	UiApp *app = g->app;
	if (UiDatePicker_Char(&g->dp.dp, &app->ui, ch)) return 1;
	if (UiDatePicker_Char(&g->dp.dt, &app->ui, ch)) return 1;
	if (UiDatePicker_Char(&g->dp.rng, &app->ui, ch)) return 1;
	return 0;
}

static void dp_ime(Gallery *g)
{
	UiApp *app = g->app;
	UiDatePicker_OnIme(&g->dp.dp, &app->ui);
	UiDatePicker_OnIme(&g->dp.dt, &app->ui);
	UiDatePicker_OnIme(&g->dp.rng, &app->ui);
}

static void dp_blink(Gallery *g)
{
	UiApp *app = g->app;
	UiDatePicker_Blink(&g->dp.dp, &app->ui);
	UiDatePicker_Blink(&g->dp.dt, &app->ui);
	UiDatePicker_Blink(&g->dp.rng, &app->ui);
}

static void dp_popups(Gallery *g)
{
	UiApp *app = g->app;
	UiDatePicker_DrawPopup(&g->dp.dp, &g->pm, &app->ui);
	dp_consume(g, &g->dp.dp, "日期");
	UiDatePicker_DrawPopup(&g->dp.dt, &g->pm, &app->ui);
	dp_consume(g, &g->dp.dt, "日期时间");
	UiDatePicker_DrawPopup(&g->dp.rng, &g->pm, &app->ui);
	dp_consume(g, &g->dp.rng, "范围");
}

static void dp_shutdown(Gallery *g)
{
	UiDatePicker_Shutdown(&g->dp.dp);
	UiDatePicker_Shutdown(&g->dp.dt);
	UiDatePicker_Shutdown(&g->dp.rng);
}

static int dp_anim(Gallery *g)
{
	return UiSelect_Busy(&g->sel.single);   /* 占位: 日历无滚动动画外露 */
}

/* ── Tabs ──────────────────────────────────────────────────────────── */

static const char *const kTabLabels[3] = { "概览", "数据", "关于" };
static const char *const kTabStyleLabels[3] = { "LINE", "PILL", "CARD" };
static const char *const kTabAxisLabels[3] = { "横向", "纵向", "淡入" };
static const int kTabAxis[3] = {
	UI_TAB_AXIS_HORIZONTAL, UI_TAB_AXIS_VERTICAL, UI_TAB_AXIS_NONE
};

static void tabs_init(Gallery *g)
{
	UiTabPagerConfig cfg;
	UiTabBar_Init(&g->tabs.bar, Ui_Id("gl.tabs.bar", 0, 0), 3);
	ZeroMemory(&cfg, sizeof(cfg));
	cfg.axis = UI_TAB_AXIS_HORIZONTAL;
	cfg.durationMs = 260.0f;
	UiTabPager_Init(&g->tabs.pager, Ui_Id("gl.tabs.pager", 0, 0), &cfg);
	g->tabs.line = UiTabStyle_Line();
	g->tabs.pill = UiTabStyle_Pill();
	g->tabs.card = UiTabStyle_Card();
	UiButtonGroup_Init(&g->tabs.style, Ui_Id("gl.tabs.style", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->tabs.style, 0);
	UiButtonGroup_Init(&g->tabs.axis, Ui_Id("gl.tabs.axis", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->tabs.axis, 0);
	UiCheckbox_Init(&g->tabs.fade, Ui_Id("gl.tabs.fade", 0, 0), 1);
	UiCheckbox_Init(&g->tabs.reverse, Ui_Id("gl.tabs.rev", 0, 0), 0);
	UiSlider_Init(&g->tabs.dur, Ui_Id("gl.tabs.dur", 0, 0), 0.3f);
}

static void tabs_page_fn(UiContext *ui, void *user, int index, UiRect rc,
	int interactive)
{
	Gallery *g = (Gallery *)user;
	UiTheme *t = ui->theme;
	Gfx *gfx = ui->gfx;
	(void)interactive;   /* 转场期间 UiContext inputBlock 已阻止命中 */

	if (index == 0) {
		Gfx_Text(gfx, t->fBig, "概览", Ui_Rect(rc.x, rc.y, rc.w, 34.0f),
			t->text, GFX_TEXT_VCENTER);
		note_txt(gfx, t, Ui_Rect(rc.x, rc.y + 44.0f, rc.w, 64.0f),
			"TabBar 只负责选择与指示条; Pager 负责当前页/目标页/进度。\n"
			"转场期间页面控件不注册命中, header 仍然可以点击打断。");
		if (Ui_Button(ui, Ui_Id("gl.tabs.p0", 0, 0),
				Ui_Rect(rc.x, rc.y + rc.h - 46.0f, 132.0f, 36.0f),
				"跳到「数据」", UI_BTN_PRIMARY, 1)) {
			UiTabBar_SetSelected(&g->tabs.bar, 1);
			UiTabPager_GoTo(&g->tabs.pager, ui, 1, 1);
			set_status(g, "已跳到「数据」页");
		}
	} else if (index == 1) {
		Gfx_Text(gfx, t->fBig, "数据", Ui_Rect(rc.x, rc.y, rc.w, 34.0f),
			t->text, GFX_TEXT_VCENTER);
		{
			int i;
			for (i = 0; i < 4; i++) {
				UiRect row = Ui_Rect(rc.x, rc.y + 48.0f + i * 36.0f,
					rc.w, 32.0f);
				char line[64];
				Gfx_FillRoundRect(gfx, row, 6.0f,
					Ui_ColorAlpha(t->text3, i & 1 ? 18 : 8));
				wsprintfA(line, "数据行 %d —— 即时模式每帧绘制", i + 1);
				Gfx_Text(gfx, t->fBody, line,
					Ui_Rect(row.x + 12.0f, row.y, row.w - 24.0f, row.h),
					t->text2, GFX_TEXT_VCENTER);
			}
		}
	} else {
		Gfx_Text(gfx, t->fBig, "关于", Ui_Rect(rc.x, rc.y, rc.w, 34.0f),
			t->text, GFX_TEXT_VCENTER);
		note_txt(gfx, t, Ui_Rect(rc.x, rc.y + 44.0f, rc.w, 64.0f),
			"卡片样式 (CARD) 需要 DrawActive 两阶段绘制:\n"
			"活动 tab 与内容面板连通, 不会出现双线接缝。");
	}
}

static void tabs_draw(Gallery *g, UiRect rc, const char *noteText)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiTabStyle *style = g->tabs.style.selected == 0 ? &g->tabs.line
		: (g->tabs.style.selected == 1 ? &g->tabs.pill : &g->tabs.card);
	UiStack col;
	UiRect headerRc, contentRc, activeRc;
	int prev;

	g->tabs.pager.cfg.axis = kTabAxis[g->tabs.axis.selected];
	g->tabs.pager.cfg.fade = g->tabs.fade.checked;
	g->tabs.pager.cfg.reverse = g->tabs.reverse.checked;
	g->tabs.pager.cfg.durationMs = 120.0f + g->tabs.dur.value * 480.0f;

	Ui_StackBegin(&col, rc, UI_COLUMN, 0.0f, 0.0f);
	headerRc = Ui_StackTake(&col, style->height > 0.0f ? style->height : 38.0f);
	Ui_StackSkip(&col, g->tabs.style.selected == 2 ? 0.0f : 10.0f);
	contentRc = Ui_StackTakeFill(&col);

	prev = g->tabs.bar.selected;
	if (UiTabBar_Draw(&g->tabs.bar, ui, headerRc, kTabLabels, style, 1) >= 0 &&
		g->tabs.bar.selected != prev) {
		UiTabPager_GoTo(&g->tabs.pager, ui, g->tabs.bar.selected, 1);
		set_status(g, kTabLabels[g->tabs.bar.selected]);
	}
	UiTabPager_Draw(&g->tabs.pager, ui, contentRc, tabs_page_fn, g);

	if (g->tabs.style.selected == 2) {
		UiColor line = Ui_ColorAlpha(t->neutral.border, 220);
		activeRc = UiTabBar_ActiveRect(&g->tabs.bar);
		if (activeRc.w > 0.0f) {
			if (activeRc.x > contentRc.x)
				Gfx_FillRoundRect(gfx,
					Ui_Rect(contentRc.x, contentRc.y - 0.5f,
						activeRc.x - contentRc.x, 1.0f), 0.0f, line);
			if (activeRc.x + activeRc.w < contentRc.x + contentRc.w)
				Gfx_FillRoundRect(gfx,
					Ui_Rect(activeRc.x + activeRc.w, contentRc.y - 0.5f,
						contentRc.x + contentRc.w - activeRc.x - activeRc.w,
						1.0f), 0.0f, line);
		} else {
			Gfx_FillRoundRect(gfx,
				Ui_Rect(contentRc.x, contentRc.y - 0.5f, contentRc.w, 1.0f),
				0.0f, line);
		}
		UiTabBar_DrawActive(&g->tabs.bar, ui, kTabLabels, style);
	}

	if (noteText)
		small_txt(gfx, t,
			Ui_Rect(rc.x, rc.y + rc.h + 4.0f, rc.w, 18.0f), noteText);
}

static void tabs_demo(Gallery *g, UiRect rc)
{
	UiStack col;
	UiRect tabsRc;
	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 34.0f, 8.0f);
	tabsRc = Ui_StackTakeFill(&col);
	tabs_draw(g, tabsRc, NULL);
}

static void tabs_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		static const char *const kTabStyleNames[3] = {
			"UiTabStyle_Line", "UiTabStyle_Pill", "UiTabStyle_Card"
		};
		static const char *const kTabAxisNames[3] = {
			"UI_TAB_AXIS_HORIZONTAL", "UI_TAB_AXIS_VERTICAL",
			"UI_TAB_AXIS_NONE"
		};
		snprintf(g->codeText, sizeof(g->codeText),
			"UiTabBar bar;  UiTabPager pager;\n"
			"UiTabBar_Init(&bar, Ui_Id(\"tb.x\", 0, 0), 3);\n"
			"UiTabStyle st = %s();\n"
			"pager.cfg.axis = %s;\n"
			"pager.cfg.fade = %d;\n"
			"pager.cfg.reverse = %d;\n"
			"pager.cfg.durationMs = %.0ff;\n"
			"\n"
			"/* 返回被点击的标签下标 (无点击 -1) */\n"
			"if (UiTabBar_Draw(&bar, ui, rc, kLabels, &st, 1) >= 0)\n"
			"    UiTabPager_GoTo(&pager, ui, bar.selected, 1);\n"
			"UiTabPager_Draw(&pager, ui, contentRc, page_fn, user);\n",
			kTabStyleNames[g->tabs.style.selected],
			kTabAxisNames[g->tabs.axis.selected],
			g->tabs.fade.checked ? 1 : 0,
			g->tabs.reverse.checked ? 1 : 0,
			120.0f + g->tabs.dur.value * 480.0f);
	}
	char buf[96];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "样式");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->tabs.style, Ui_StackTake(&col, 34.0f),
		kTabStyleLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "转场方向");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->tabs.axis, Ui_StackTake(&col, 34.0f),
		kTabAxisLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	Ui_Checkbox(&g->tabs.fade, ui, Ui_StackTake(&col, 26.0f), "淡入淡出", 1);
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->tabs.reverse, ui, Ui_StackTake(&col, 26.0f),
		"反向翻页", 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "时长");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->tabs.dur, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	wsprintfA(buf, "durationMs = %d",
		(int)(120.0f + g->tabs.dur.value * 480.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);

	{
		UiRect area = pane_in(pv, 12.0f);
		tabs_draw(g, area, NULL);
	}
}

static int tabs_key(Gallery *g, UINT vk)
{
	int prev;
	if (vk == VK_TAB && g->app->ui.navId != g->tabs.bar.id) return 0;
	if (vk == VK_TAB) {
		UiTabBar_SetFocusVisible(&g->tabs.bar, 1);
		UiApp_Invalidate(g->app);
		return 1;
	}
	prev = g->tabs.bar.selected;
	if (UiTabBar_Key(&g->tabs.bar, &g->app->ui, vk)) {
		if (g->tabs.bar.changed && g->tabs.bar.selected != prev)
			UiTabPager_GoTo(&g->tabs.pager, &g->app->ui,
				g->tabs.bar.selected, 1);
		UiApp_Invalidate(g->app);
		return 1;
	}
	return 0;
}

static int tabs_anim(Gallery *g)
{
	return UiTabPager_Busy(&g->tabs.pager);
}

/* ── Nav ───────────────────────────────────────────────────────────── */

static void nav_set(Gallery *g, UiNavItem *it, UiId id, const char *label,
	const char *shortcut, const UiNavItem *kids, int count, unsigned flags,
	uint32_t glyph)
{
	ZeroMemory(it, sizeof(*it));
	it->id = id;
	it->label = label;
	it->shortcut = shortcut;
	if (glyph && g->app->theme.fIcon)
		it->icon = UiIcon_Glyph(g->app->theme.fIcon, glyph);
	it->children = kids;
	it->childCount = count;
	it->flags = flags;
}

static const UiNavItem *nav_find(const UiNavItem *items, int count, UiId id)
{
	int i, k;
	if (!items) return NULL;
	for (i = 0; i < count; i++) {
		if (items[i].id == id) return &items[i];
		if (items[i].children) {
			const UiNavItem *hit = nav_find(items[i].children,
				items[i].childCount, id);
			if (hit) return hit;
		}
		(void)k;
	}
	return NULL;
}

static void nav_build_crumb(Gallery *g, const UiId *path, int depth,
	char *out, int cap)
{
	int i, n = 0;
	out[0] = 0;
	if (cap <= 0) return;
	for (i = 0; i < depth; i++) {
		const UiNavItem *it = nav_find(g->nav.roots, 7, path[i]);
		const char *s;
		if (!it) break;
		if (n && n < cap - 1) out[n++] = ' ';
		if (n && n < cap - 1) out[n++] = '/';
		if (n && n < cap - 1) out[n++] = ' ';
		s = it->label;
		while (s && *s && n < cap - 1) out[n++] = *s++;
	}
	out[n] = 0;
}

static void nav_init(Gallery *g)
{
	UiId path[2];
	nav_set(g, &g->nav.prod[0], Ui_Id("gl.nav.p1", 0, 0), "桌面应用", NULL,
		NULL, 0, 0, 0xE7F4u);
	nav_set(g, &g->nav.prod[1], Ui_Id("gl.nav.p2", 0, 0), "移动端", NULL,
		NULL, 0, 0, 0xE8EAu);
	nav_set(g, &g->nav.prod[2], Ui_Id("gl.nav.p3", 0, 0), "云服务", NULL,
		NULL, 0, 0, 0xE753u);
	nav_set(g, &g->nav.res[0], Ui_Id("gl.nav.r1", 0, 0), "使用文档", NULL,
		NULL, 0, 0, 0);
	nav_set(g, &g->nav.res[1], Ui_Id("gl.nav.r2", 0, 0), "示例代码", NULL,
		NULL, 0, 0, 0);
	nav_set(g, &g->nav.roots[0], Ui_Id("gl.nav.0", 0, 0), "首页", NULL,
		NULL, 0, 0, 0xE80Fu);
	nav_set(g, &g->nav.roots[1], Ui_Id("gl.nav.1", 0, 0), "产品", NULL,
		g->nav.prod, 3, 0, 0xE71Du);
	nav_set(g, &g->nav.roots[2], Ui_Id("gl.nav.2", 0, 0), "解决方案", NULL,
		NULL, 0, 0, 0xE8F1u);
	nav_set(g, &g->nav.roots[3], Ui_Id("gl.nav.3", 0, 0), "资源", NULL,
		g->nav.res, 2, 0, 0xE8A5u);
	nav_set(g, &g->nav.roots[4], Ui_Id("gl.nav.4", 0, 0), "支持", NULL,
		NULL, 0, 0, 0xE897u);
	nav_set(g, &g->nav.roots[5], Ui_Id("gl.nav.5", 0, 0), "关于", NULL,
		NULL, 0, 0, 0xE946u);
	nav_set(g, &g->nav.roots[6], Ui_Id("gl.nav.6", 0, 0), "定价", NULL,
		NULL, 0, 0, 0xE8ECu);

	UiNavBar_Init(&g->nav.nav, Ui_Id("gl.nav", 0, 0));
	UiNavBar_SetItems(&g->nav.nav, g->nav.roots, 7);
	UiNavBar_SetLayout(&g->nav.nav, UI_NAV_HORIZONTAL);
	path[0] = g->nav.roots[1].id;
	path[1] = g->nav.prod[0].id;
	UiNavBar_SetSelected(&g->nav.nav, path, 2);

	UiButtonGroup_Init(&g->nav.layout, Ui_Id("gl.nav.layout", 0, 0),
		UI_BG_SINGLE, 2);
	UiButtonGroup_SetSelected(&g->nav.layout, 0);
	UiCheckbox_Init(&g->nav.rail, Ui_Id("gl.nav.rail", 0, 0), 0);
}

static void nav_draw_bar(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiNavBar_Draw(&g->nav.nav, &g->pm, &app->ui, rc, 1);
	if (g->nav.nav.changed) {
		UiId got[UI_NAV_MAX_DEPTH];
		char crumb[192];
		int depth = UiNavBar_GetSelected(&g->nav.nav, got,
			UI_NAV_MAX_DEPTH);
		nav_build_crumb(g, got, depth, crumb, (int)sizeof(crumb));
		set_status(g, crumb[0] ? crumb : "(无选中)");
		UiApp_Invalidate(app);
	}
}

static void nav_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	UiId got[UI_NAV_MAX_DEPTH];
	char crumb[192];
	int depth;
	int vertical = g->nav.nav.layout == UI_NAV_VERTICAL;
	UiRect navRc, card;

	if (vertical) {
		float w = g->nav.nav.rail ? 66.0f : 216.0f;
		navRc = Ui_Rect(rc.x, rc.y, w, rc.h);
		card = Ui_Rect(rc.x + w + 14.0f, rc.y, rc.w - w - 14.0f, rc.h);
	} else {
		navRc = Ui_Rect(rc.x, rc.y, rc.w, 44.0f);
		card = Ui_Rect(rc.x, rc.y + 56.0f, rc.w, rc.h - 56.0f);
	}

	nav_draw_bar(g, navRc);

	Ui_Panel(&app->ui, card);
	depth = UiNavBar_GetSelected(&g->nav.nav, got, UI_NAV_MAX_DEPTH);
	nav_build_crumb(g, got, depth, crumb, (int)sizeof(crumb));
	lbl(gfx, t, Ui_Rect(card.x + 18.0f, card.y + 14.0f, card.w - 36.0f, 20.0f),
		"当前位置");
	Gfx_Text(gfx, t->fBig, crumb[0] ? crumb : "(无选中)",
		Ui_Rect(card.x + 18.0f, card.y + 38.0f, card.w - 36.0f, 30.0f),
		t->text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	note_txt(gfx, t,
		Ui_Rect(card.x + 18.0f, card.y + 80.0f, card.w - 36.0f, 84.0f),
		"「产品」「资源」两项带子菜单 —— 点击展开;\n"
		"其余项是叶子 (点击直接选中, 不弹菜单)。\n"
		"子菜单打开后再点同一父项 = 收起; 悬停父项也会展开。\n"
		"水平栏尾部放不下时收进 >>; 垂直模式可切换为图标 rail。");
}

/* ── Nav 导航菜单段常量 ── (布局按钮组标签: 0=水平 1=垂直) */
static const char *const kNavLayoutLabels[2] = { "水平", "垂直" };

static void nav_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiNavBar nav;\n"
			"UiNavBar_Init(&nav, Ui_Id(\"nav.x\", 0, 0));\n"
			"UiNavBar_SetItems(&nav, roots, 7);\n"
			"UiNavBar_SetLayout(&nav, %s);\n"
			"\n"
			"UiNavBar_Draw(&nav, &pm, ui, rc, 1);\n"
			"if (nav.changed) {\n"
			"    /* 路径变化: 取回 Id 数组做路由 */\n"
			"    UiNavBar_GetSelected(&nav, got, 8);\n"
			"    nav.changed = 0;\n"
			"}\n"
			"UiNavBar_DrawPopups(&nav, &pm, ui);   /* 帧末 */\n"
			"UiNavBar_Key(&nav, &pm, ui, vk);\n",
			g->nav.layout.selected == 1 ? "UI_NAV_VERTICAL" : "UI_NAV_HORIZONTAL");
	}
	int vertical;

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "布局");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->nav.layout, Ui_StackTake(&col, 34.0f),
		kNavLayoutLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	vertical = g->nav.layout.selected == 1;
	UiNavBar_SetLayout(&g->nav.nav, vertical ? UI_NAV_VERTICAL
		: UI_NAV_HORIZONTAL);

	/* rail 仅垂直布局可用: 水平时禁用 (点击不翻转, 文案提示先切布局) */
	if (Ui_Checkbox(&g->nav.rail, ui, Ui_StackTake(&col, 26.0f),
			vertical ? "垂直图标 rail (收窄为图标栏)"
				: "垂直图标 rail (先切「垂直」布局启用)",
			vertical)) {
		char buf[64];
		wsprintfA(buf, "rail: %s", g->nav.rail.checked ? "开" : "关");
		set_status(g, buf);
	}
	UiNavBar_SetRail(&g->nav.nav, vertical ? g->nav.rail.checked : 0);
	Ui_StackSkip(&col, 10.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"预览就是同一个导航栏实例 —— 直接在上面点选。\n"
		"布局切「垂直」后, 勾选 rail 收窄为图标栏。");

	{
		UiRect area = pane_in(pv, 12.0f);
		nav_draw_bar(g, Ui_Rect(area.x, area.y, vertical ?
			(g->nav.rail.checked ? 66.0f : 200.0f) : area.w,
			area.h));
	}
}

static int nav_key(Gallery *g, UINT vk)
{
	if (UiNavBar_Key(&g->nav.nav, &g->pm, &g->app->ui, vk)) {
		UiId got[UI_NAV_MAX_DEPTH];
		char crumb[192];
		int depth = UiNavBar_GetSelected(&g->nav.nav, got,
			UI_NAV_MAX_DEPTH);
		nav_build_crumb(g, got, depth, crumb, (int)sizeof(crumb));
		if (depth) {
			char line[224];
			wsprintfA(line, "已选择: %s", crumb);
			set_status(g, line);
		}
		UiApp_Invalidate(g->app);
		return 1;
	}
	return 0;
}

static void nav_popups(Gallery *g)
{
	UiNavBar_DrawPopups(&g->nav.nav, &g->pm, &g->app->ui);
}

static void nav_shutdown(Gallery *g)
{
	UiNavBar_Shutdown(&g->nav.nav);
}

/* ── Pagination ────────────────────────────────────────────────────── */

static const char *const kPagSizeLabels[3] = { "10 条/页", "20 条/页", "50 条/页" };
static const int kPagSizes[3] = { 10, 20, 50 };
static const char *const kPagTotalLabels[3] = { "100 条", "500 条", "2000 条" };
static const int kPagTotals[3] = { 100, 500, 2000 };
static const char *const kPagSlotLabels[4] = { "自动", "5 格", "7 格", "9 格" };
static const int kPagSlots[4] = { 0, 5, 7, 9 };

static void pag_init(Gallery *g)
{
	UiPagination_Init(&g->pag.pg, Ui_Id("gl.pag", 0, 0), 1000, 20);
	UiButtonGroup_Init(&g->pag.size, Ui_Id("gl.pag.size", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->pag.size, 1);
	UiButtonGroup_Init(&g->pag.total, Ui_Id("gl.pag.total", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->pag.total, 0);
	UiButtonGroup_Init(&g->pag.slots, Ui_Id("gl.pag.slots", 0, 0),
		UI_BG_SINGLE, 4);
	UiButtonGroup_SetSelected(&g->pag.slots, 0);
	UiCheckbox_Init(&g->pag.firstLast, Ui_Id("gl.pag.fl", 0, 0), 1);
}

static void pag_apply(Gallery *g)
{
	int size = kPagSizes[g->pag.size.selected];
	int total = kPagTotals[g->pag.total.selected];
	if (g->pag.pg.pageSize != size)
		UiPagination_SetPageSize(&g->pag.pg, &g->app->ui, size);
	if (g->pag.pg.total != total)
		UiPagination_SetTotal(&g->pag.pg, &g->app->ui, total);
}

static void pag_style(UiPaginationStyle *st, Gallery *g)
{
	ZeroMemory(st, sizeof(*st));
	st->firstLast = g->pag.firstLast.checked;
	st->maxSlots = kPagSlots[g->pag.slots.selected];
}

static void pag_draw_list(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	int off = UiPagination_Offset(&g->pag.pg);
	int rows = (int)(rc.h / 34.0f);
	int i, shown = 0;

	Ui_Panel(&app->ui, rc);
	for (i = 0; i < rows && off + i < g->pag.pg.total; i++) {
		char title[64], sub[48];
		UiRect row = Ui_Rect(rc.x + 10.0f, rc.y + 8.0f + i * 34.0f,
			rc.w - 20.0f, 30.0f);
		if (i & 1)
			Gfx_FillRoundRect(gfx, row, 6.0f, t->neutral.soft);
		wsprintfA(title, "数据条目 #%d", off + i + 1);
		wsprintfA(sub, "SC-%05d", off + i + 1);
		Gfx_Text(gfx, t->fBody, title,
			Ui_Rect(row.x + 12.0f, row.y, row.w - 160.0f, row.h),
			t->text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		Gfx_Text(gfx, t->fSmall, sub,
			Ui_Rect(row.x, row.y, row.w - 12.0f, row.h), t->text3,
			GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
		shown++;
	}
	if (shown == 0)
		Gfx_Text(gfx, t->fBody, "当前页没有数据", rc, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
}

static void pag_consume(Gallery *g)
{
	char buf[160];
	int end;
	if (!UiPagination_TakeChanged(&g->pag.pg)) return;
	end = UiPagination_Offset(&g->pag.pg) + g->pag.pg.pageSize;
	if (end > g->pag.pg.total) end = g->pag.pg.total;
	wsprintfA(buf, "第 %d/%d 页 · 范围 %d–%d / %d",
		g->pag.pg.page + 1, UiPagination_PageCount(&g->pag.pg),
		UiPagination_Offset(&g->pag.pg) + 1, end, g->pag.pg.total);
	set_status(g, buf);
}

static void pag_draw_bar(Gallery *g, UiRect rc, char *rangeBuf, int cap)
{
	UiPaginationStyle st;
	pag_style(&st, g);
	UiPagination_Draw(&g->pag.pg, &g->app->ui, rc, &st);
	pag_consume(g);
	if (rangeBuf && cap > 0)
		wsprintfA(rangeBuf, "第 %d–%d 条 · 共 %d 条 · 第 %d/%d 页",
			g->pag.pg.total ? UiPagination_Offset(&g->pag.pg) + 1 : 0,
			UiPagination_Offset(&g->pag.pg) + g->pag.pg.pageSize < g->pag.pg.total
				? UiPagination_Offset(&g->pag.pg) + g->pag.pg.pageSize
				: g->pag.pg.total,
			g->pag.pg.total, g->pag.pg.page + 1,
			UiPagination_PageCount(&g->pag.pg));
}

static void pag_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect listRc, barRc, rangeRc;
	char range[128];

	pag_apply(g);
	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 8.0f, 4.0f, 8.0f);
	rangeRc = Ui_StackTake(&col, 20.0f);
	Ui_StackSkip(&col, 6.0f);
	/* 固定行在前、TakeFill 最后: 先为底部分页条预留空间 */
	Ui_StackSkip(&col, 36.0f + 10.0f);
	listRc = Ui_StackTakeFill(&col);
	barRc = Ui_Rect(rc.x, rc.y + rc.h - 40.0f, rc.w, 36.0f);

	pag_draw_bar(g, barRc, range, (int)sizeof(range));
	Gfx_Text(gfx, t->fBody, range, rangeRc, t->text2,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	pag_draw_list(g, listRc);
}

static void pag_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiPagination pg;\n"
			"UiPagination_Init(&pg, Ui_Id(\"pg.x\", 0, 0),\n"
			"    %d, %d);          /* total, pageSize */\n"
			"pg.cfg.maxSlots = %d;   /* 0 = 按宽度自适应 */\n"
			"\n"
			"/* 返回 1 = 本帧翻页 (含键盘) */\n"
			"if (Ui_Pagination(&pg, ui, rc, 1))\n"
			"    fetch(UiPagination_Offset(&pg));\n",
			kPagTotals[g->pag.total.selected],
			kPagSizes[g->pag.size.selected],
			kPagSlots[g->pag.slots.selected]);
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "每页条数");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->pag.size, Ui_StackTake(&col, 34.0f),
		kPagSizeLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "总条数");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->pag.total, Ui_StackTake(&col, 34.0f),
		kPagTotalLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "页码窗口");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->pag.slots, Ui_StackTake(&col, 34.0f),
		kPagSlotLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	Ui_Checkbox(&g->pag.firstLast, ui, Ui_StackTake(&col, 26.0f),
		"显示首页/末页 « »", 1);

	{
		UiRect area = pane_in(pv, 20.0f);
		UiRect barRc;
		pag_apply(g);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"实时预览 (键盘 ←/→/Home/End/PgUp/PgDn)");
		barRc = Ui_Rect(area.x, area.y + 34.0f, area.w, 36.0f);
		pag_draw_bar(g, barRc, NULL, 0);
	}
}

static int pag_key(Gallery *g, UINT vk)
{
	if (UiPagination_Key(&g->pag.pg, &g->app->ui, vk)) {
		pag_consume(g);
		UiApp_Invalidate(g->app);
		return 1;
	}
	return 0;
}

/* ── ScrollView ────────────────────────────────────────────────────── */

static const char *const kCardLinesA[3] = {
	"固定 1000 行只绘制可见的十几行。",
	"滚轮平滑 + 甩动惯性。",
	"拖动右侧 thumb 精确定位。"
};
static const char *const kCardLinesB[4] = {
	"变高卡片: 高度由内容测量并缓存。",
	"展开 / 收起只重测这一项。",
	"前缀 y 与 contentH 随之更新。",
	"触摸配置支持越界回弹。"
};
static const char *const kCardExtra[2] = {
	"展开后追加的任意高度内容。",
	"ScrollView 不关心子控件类型。"
};
static const char *const kScrollPresetLabels[2] = { "桌面", "触摸" };

static float card_measure(const ScrollCard *c, float width)
{
	int n = c->nbase + (c->expanded ? 2 : 0);
	(void)width;
	return 48.0f + n * 18.0f + (c->expanded ? 8.0f : 0.0f);
}

static void scroll_init(Gallery *g)
{
	UiScrollConfig desktop = UiScrollConfig_Desktop();
	UiScrollConfig touch = UiScrollConfig_Touch();
	UiScrollbarStyle bar = UiScrollbarStyle_Default();
	UiScrollPadding pad;
	int i;

	bar.reserveSpace = 1;
	pad.left = 4.0f; pad.top = 4.0f; pad.right = 6.0f; pad.bottom = 6.0f;

	UiScrollView_Init(&g->scroll.fixedSv, Ui_Id("gl.sv.fixed", 0, 0),
		&desktop, &bar);
	UiScrollView_SetPadding(&g->scroll.fixedSv, pad);
	UiScrollLayout_Init(&g->scroll.fixedL, NULL, 0);
	UiScrollLayout_SetPadding(&g->scroll.fixedL, 6.0f, 8.0f);
	UiScrollLayout_SetFixed(&g->scroll.fixedL, FIXED_ROWS, FIXED_ROW_H);
	UiScrollLayout_Commit(&g->scroll.fixedL);

	UiScrollView_Init(&g->scroll.varSv, Ui_Id("gl.sv.var", 0, 0),
		&touch, &bar);
	UiScrollView_SetPadding(&g->scroll.varSv, pad);
	UiScrollLayout_Init(&g->scroll.varL, g->scroll.varItems, VAR_CARD_N);
	UiScrollLayout_SetPadding(&g->scroll.varL, 6.0f, 8.0f);
	UiScrollLayout_SetGap(&g->scroll.varL, 10.0f);
	UiScrollLayout_SetCount(&g->scroll.varL, VAR_CARD_N);
	for (i = 0; i < VAR_CARD_N; i++) {
		g->scroll.cards[i].lines = (i & 1) ? kCardLinesB : kCardLinesA;
		g->scroll.cards[i].nbase = (i & 1) ? 4 : 3;
		g->scroll.cards[i].expanded = (i % 5 == 2);
		g->scroll.cards[i].rev = (uint32_t)(i + 1);
		UiScrollLayout_SetItemFlags(&g->scroll.varL, i,
			UI_SCROLL_ITEM_WIDTH_DEPENDENT);
	}
	UiScrollLayout_Commit(&g->scroll.varL);

	UiButtonGroup_Init(&g->scroll.preset, Ui_Id("gl.sv.preset", 0, 0),
		UI_BG_SINGLE, 2);
	UiButtonGroup_SetSelected(&g->scroll.preset, 0);
	UiSlider_Init(&g->scroll.wheelStep, Ui_Id("gl.sv.step", 0, 0), 0.5f);
	UiCheckbox_Init(&g->scroll.reserve, Ui_Id("gl.sv.reserve", 0, 0), 1);
}

static void scroll_tick(Gallery *g, double dtMs)
{
	UiScrollView_Tick(&g->scroll.fixedSv, &g->app->ui, (float)dtMs);
	UiScrollView_Tick(&g->scroll.varSv, &g->app->ui, (float)dtMs);
}

static void scroll_apply(Gallery *g)
{
	UiScrollConfig cfg = g->scroll.preset.selected == 1
		? UiScrollConfig_Touch() : UiScrollConfig_Desktop();
	cfg.wheelStep = 24.0f + g->scroll.wheelStep.value * 72.0f;
	cfg.autoHideBar = 1;
	g->scroll.fixedSv.cfg = cfg;
	g->scroll.varSv.cfg = cfg;
	g->scroll.varSv.bar.reserveSpace = g->scroll.reserve.checked;
}

static void scroll_draw_fixed(Gallery *g, UiRect viewport)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiScrollLayout *l = &g->scroll.fixedL;
	UiRect content, row;
	int i, first = 0, last = -1, drawn = 0;
	float innerW = UiScrollView_ContentWidth(&g->scroll.fixedSv, viewport);

	UiScrollView_Begin(&g->scroll.fixedSv, ui, viewport, innerW, l->contentH);
	content = UiScrollView_ContentRect(&g->scroll.fixedSv);
	UiScrollLayout_VisibleRange(l, g->scroll.fixedSv.offset, viewport.h,
		&first, &last);
	for (i = first; i <= last; i++) {
		UiId id;
		UiWidgetState st;
		char line[64];
		if (i < 0 || i >= l->count) continue;
		row = UiScrollLayout_ItemRect(l, i, content);
		if (UiScrollView_Cull(&g->scroll.fixedSv, row)) continue;
		id = Ui_IdIndex("gl.sv.row", 0, 0, i);
		st = Ui_State(ui, id, row, 1);
		if (i & 1)
			Gfx_FillRoundRect(gfx, Ui_RectInset(row, 1.0f), 6.0f,
				Ui_ColorAlpha(t->text3, 10));
		if (st.hovered)
			Gfx_FillRoundRect(gfx, Ui_RectInset(row, 1.0f), 6.0f,
				Ui_ColorAlpha(t->primary.base, 20));
		Gfx_FillRoundRect(gfx,
			Ui_Rect(row.x + 10.0f, row.y + row.h * 0.5f - 2.0f, 4.0f, 4.0f),
			2.0f, i == 0 ? t->primary.base : t->text4);
		wsprintfA(line, "行 %05d", i);
		Gfx_Text(gfx, t->fBody, line,
			Ui_Rect(row.x + 24.0f, row.y, row.w - 130.0f, row.h),
			t->text, GFX_TEXT_VCENTER);
		small_txt(gfx, t,
			Ui_Rect(row.x + row.w - 120.0f, row.y, 108.0f, row.h),
			"固定 34 DIP");
		drawn++;
	}
	UiScrollView_End(&g->scroll.fixedSv, ui);
	g->scroll.first = first;
	g->scroll.last = last;
	g->scroll.drawn = drawn;
}

static void scroll_draw_var(Gallery *g, UiRect viewport)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiScrollLayout *l = &g->scroll.varL;
	UiRect content, rc;
	int i, first = 0, last = -1;
	float innerW = UiScrollView_ContentWidth(&g->scroll.varSv, viewport);

	UiScrollLayout_SetWidth(l, innerW);
	for (i = 0; i < l->count; i++) {
		if (UiScrollLayout_ItemNeedsMeasure(l, i, g->scroll.cards[i].rev))
			UiScrollLayout_SetItemHeight(l, i, g->scroll.cards[i].rev,
				card_measure(&g->scroll.cards[i], innerW));
	}
	UiScrollLayout_Commit(l);

	UiScrollView_Begin(&g->scroll.varSv, ui, viewport, innerW, l->contentH);
	content = UiScrollView_ContentRect(&g->scroll.varSv);
	UiScrollLayout_VisibleRange(l, g->scroll.varSv.offset, viewport.h,
		&first, &last);
	for (i = first; i <= last; i++) {
		UiId id;
		UiWidgetState st;
		UiColor bg, border;
		int n, k;
		if (i < 0 || i >= l->count) continue;
		rc = UiScrollLayout_ItemRect(l, i, content);
		if (UiScrollView_Cull(&g->scroll.varSv, rc)) continue;
		id = Ui_IdIndex("gl.sv.card", 0, 0, i);
		st = Ui_State(ui, id, rc, 1);
		bg = g->scroll.cards[i].expanded
			? Ui_ColorMix(t->card, t->primary.soft, 0.35f) : t->card;
		border = st.hovered ? t->primary.base : t->neutral.border;
		Gfx_FillRoundRect(gfx, rc, 8.0f, bg);
		Gfx_StrokeRoundRect(gfx, rc, 8.0f, border, 1.0f);
		n = g->scroll.cards[i].nbase + (g->scroll.cards[i].expanded ? 2 : 0);
		for (k = 0; k < n && k < 6; k++) {
			const char *line = k < g->scroll.cards[i].nbase
				? g->scroll.cards[i].lines[k] : kCardExtra[k - g->scroll.cards[i].nbase];
			Gfx_Text(gfx, t->fBody, line,
				Ui_Rect(rc.x + 14.0f, rc.y + 26.0f + k * 18.0f,
					rc.w - 28.0f, 18.0f),
				k == 0 ? t->text : t->text2, GFX_TEXT_VCENTER);
		}
		if (ui->clicked == id) {
			g->scroll.cards[i].expanded = !g->scroll.cards[i].expanded;
			g->scroll.cards[i].rev++;
		}
	}
	UiScrollView_End(&g->scroll.varSv, ui);
}

static void scroll_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	UiStack row;
	UiRect left, right, stat;
	char buf[192];

	scroll_apply(g);
	Ui_StackBegin(&row, rc, UI_ROW, 14.0f, 0.0f);
	{
		UiFixedSize fx[3] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
		UiMeasureItem items[3] = {
			Ui_FixedItem(&fx[0], 1.0f),
			Ui_FixedItem(&fx[1], 1.0f),
			Ui_FixedItem(&fx[2], 0.0f) };
		UiRect panes[3];
		if (Ui_FlexLayout(rc, UI_ROW, 14.0f, items, 3, panes)) {
			left = panes[0];
			right = panes[1];
			stat = panes[2];
		} else {
			left = rc;
			right = Ui_Rect(0, 0, 0, 0);
			stat = Ui_Rect(0, 0, 0, 0);
		}
	}
	scroll_draw_fixed(g, left);
	scroll_draw_var(g, right);
	wsprintfA(buf, "可见 %d..%d (%d 行)\n\n变高卡片\n点击展开/收起",
		g->scroll.first, g->scroll.last, g->scroll.drawn);
	note_txt(gfx, t, stat, buf);
	(void)t;
}

static void scroll_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"/* 滚动位置是连续状态, 不是离散事件 */\n"
			"UiScrollConfig cfg = UiScrollConfig_%s();\n"
			"UiScrollView_Init(&sv, id, &cfg, &bar);\n"
			"\n"
			"/* 每帧: 声明内容总尺寸 */\n"
			"UiScrollView_Begin(&sv, ui, rc, viewW, contentH);\n"
			"content = UiScrollView_ContentRect(&sv);\n"
			"if (!UiScrollView_Cull(&sv, rowRc))   /* 可见性裁剪 */\n"
			"    draw_row(gfx, rowRc, i);\n"
			"UiScrollView_End(&sv, ui);\n"
			"\n"
			"on_tick:  UiScrollView_Tick(&sv, ui, dtMs);\n"
			"程序滚动: UiScrollView_ScrollTo / Fling\n",
			g->scroll.preset.selected == 1 ? "Touch" : "Desktop");
	}
	char buf[96];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "滚动配置 (左侧列表)");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->scroll.preset, Ui_StackTake(&col, 34.0f),
		kScrollPresetLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "滚轮步长 (DIP/格)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->scroll.wheelStep, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	wsprintfA(buf, "wheelStep = %d", (int)(24.0f + g->scroll.wheelStep.value * 72.0f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 10.0f);

	Ui_Checkbox(&g->scroll.reserve, ui, Ui_StackTake(&col, 26.0f),
		"右侧预留滚动条槽 (变高卡片)", 1);

	{
		UiRect area = pane_in(pv, 12.0f);
		scroll_apply(g);
		scroll_draw_fixed(g, Ui_Rect(area.x, area.y, area.w * 0.55f, area.h));
		scroll_draw_var(g, Ui_Rect(area.x + area.w * 0.55f + 10.0f,
			area.y, area.w * 0.45f - 10.0f, area.h));
	}
}

static int scroll_anim(Gallery *g)
{
	return UiScrollView_Busy(&g->scroll.fixedSv) ||
		UiScrollView_Busy(&g->scroll.varSv);
}

/* ── Layout ────────────────────────────────────────────────────────── */

static LayoutCard kLcHead = { "表头 (跨 3 列)", "fixed / auto / fr 轨道", 80.0f, 44.0f };
static LayoutCard kLcA = { "卡片 A", "auto 内容", 80.0f, 64.0f };
static LayoutCard kLcB = { "高卡片 B", "rowSpan 2", 80.0f, 64.0f };
static LayoutCard kLcC = { "卡片 C", NULL, 80.0f, 64.0f };
static LayoutCard kLcD = { "卡片 D", NULL, 80.0f, 64.0f };
static LayoutCard kLcE = { "自动回填 E", "row=-1 (dense 回填)", 80.0f, 64.0f };

static void layout_measure(void *user, UiMeasure *m)
{
	LayoutCard *c = (LayoutCard *)user;
	m->minW = c->minW;
	m->prefW = c->minW + 60.0f;
	m->minH = m->prefH = c->prefH;
}

static UiGridItem layout_item(LayoutCard *c, int row, int col,
	int rowSpan, int colSpan)
{
	UiGridItem it;
	it.user = c;
	it.measure = layout_measure;
	it.row = row;
	it.col = col;
	it.rowSpan = rowSpan;
	it.colSpan = colSpan;
	it.stretch = 1;
	it.align = 0;
	return it;
}

static void layout_draw_cell(Gallery *g, UiRect rc, LayoutCard *c, UiColor accent)
{
	UiTheme *t = &g->app->theme;
	Gfx *gfx = g->app->gfx;
	Gfx_FillRoundRect(gfx, rc, 8.0f, Ui_ColorAlpha(accent, 36));
	Gfx_StrokeRoundRect(gfx, rc, 8.0f, Ui_ColorAlpha(accent, 140), 1.0f);
	Gfx_Text(gfx, t->fBody, c->label,
		Ui_Rect(rc.x + 10.0f, rc.y + 8.0f, rc.w - 20.0f, 18.0f),
		t->text, GFX_TEXT_VCENTER);
	if (c->sub)
		Gfx_Text(gfx, t->fMini, c->sub,
			Ui_Rect(rc.x + 10.0f, rc.y + rc.h - 20.0f, rc.w - 20.0f, 16.0f),
			t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

/* Stack 演示块: 圆角色块 + 居中标注 (标注可用 NULL) */
static void layout_block(Gfx *gfx, UiTheme *t, UiRect rc, UiColor accent,
	const char *tag, UiColor fg)
{
	if (rc.w <= 1.0f || rc.h <= 1.0f) return;
	Gfx_FillRoundRect(gfx, rc, 6.0f, Ui_ColorAlpha(accent, 40));
	Gfx_StrokeRoundRect(gfx, rc, 6.0f, Ui_ColorAlpha(accent, 150), 1.0f);
	if (tag)
		Gfx_Text(gfx, t->fMini, tag,
			Ui_Rect(rc.x + 4.0f, rc.y, rc.w - 8.0f, rc.h), fg,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void layout_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect stackArea, alignArea, moreArea;
	char buf[64];
	int i;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"UiStack 游标流: Take / Skip / TakeFill / TakeRight (右侧实时读数)");
	Ui_StackSkip(&col, 4.0f);
	stackArea = Ui_StackTake(&col, 190.0f);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"Ui_StackTakeAligned: 交叉轴对齐 (属性页切换)");
	Ui_StackSkip(&col, 4.0f);
	alignArea = Ui_StackTake(&col, 84.0f);
	Ui_StackSkip(&col, 10.0f);

	moreArea = Ui_StackTakeFill(&col);
	note_txt(gfx, t, moreArea,
		"Flex 弹性布局与 Grid 网格布局已拆为独立页面 (左侧导航「导航与容器」分组),\n"
		"本页专注 UiStack 游标流与对齐 —— 全画廊每个页面的骨架都由它搭起。");

	/* ── Stack 游标流解剖: 左侧卡片, 右侧读数 ── */
	{
		UiStack s, rrow;
		UiRect card, inner, head, f1, fill, brow;
		UiRect rest;
		float cardW = stackArea.w * 0.55f;
		float hh = 26.0f + g->layout.headH.value * 26.0f;
		if (cardW > 380.0f) cardW = 380.0f;
		if (cardW < 240.0f) cardW = 240.0f;
		card = Ui_Rect(stackArea.x, stackArea.y, cardW, stackArea.h);
		rest = Ui_Rect(stackArea.x + cardW + 18.0f, stackArea.y,
			stackArea.w - cardW - 18.0f, stackArea.h);

		Gfx_FillRoundRect(gfx, card, 10.0f, Ui_ColorAlpha(t->text3, 10));
		Gfx_StrokeRoundRect(gfx, card, 10.0f,
			Ui_ColorAlpha(t->text3, 90), 1.0f);
		Gfx_Text(gfx, t->fMini,
			"Ui_StackBeginPad(&s, card, UI_COLUMN, 0, 14,14,14,14)",
			Ui_Rect(card.x + 10.0f, card.y + 4.0f, card.w - 20.0f, 14.0f),
			t->hint, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

		inner = pane_in(card, 14.0f);
		Ui_StackBegin(&s, inner, UI_COLUMN, 0.0f, 0.0f);
		head = Ui_StackTake(&s, hh);
		Ui_StackSkip(&s, 10.0f);
		f1 = Ui_StackTake(&s, 28.0f);
		Ui_StackSkip(&s, 10.0f);
		fill = Ui_StackTakeFill(&s);
		Ui_StackSkip(&s, 10.0f);
		brow = Ui_StackTake(&s, 28.0f);

		{
			UiColor acc[4];
			acc[0] = t->primary.base;
			acc[1] = t->blue;
			acc[2] = t->success.base;
			acc[3] = t->warning.base;
			wsprintfA(buf, "Take(%.0f)", hh);
			layout_block(gfx, t, head, acc[0], buf, t->text);
			layout_block(gfx, t, f1, acc[1], "Take(28)", t->text);
			layout_block(gfx, t, fill, acc[2], "TakeFill()", t->text);
		}

		/* 行内: Take + TakeFill 弹簧 + TakeRight 右贴 */
		Ui_StackBegin(&rrow, brow, UI_ROW, 8.0f, 0.0f);
		{
			UiRect b1, sp, b2;
			b1 = Ui_StackTake(&rrow, 80.0f);
			sp = Ui_StackTakeFill(&rrow);
			b2 = Ui_StackTakeRight(&rrow, 92.0f);
			layout_block(gfx, t, b1, t->warning.base, "Take(80)", t->text);
			Gfx_Text(gfx, t->fMini, "TakeFill 弹簧",
				Ui_Rect(sp.x, sp.y, sp.w, sp.h), t->hint,
				GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			layout_block(gfx, t, b2, t->red, "TakeRight(92)", t->text);
		}

		/* 右侧: 游标实时读数 + 调用一览 */
		if (rest.w > 120.0f) {
			static const char *const kStackCalls[4] = {
				"Take(n)   取固定尺寸",
				"Skip(n)   推进空隙",
				"TakeFill() 吃掉全部剩余",
				"TakeRight(n) 行内右贴"
			};
			Gfx_Text(gfx, t->fSmall, "游标状态 (每帧刷新)",
				Ui_Rect(rest.x, rest.y, rest.w, 16.0f), t->text2,
				GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			wsprintfA(buf, "cursor=%.0f  remaining=%.0f  count=%d",
				s.cursor, Ui_StackRemaining(&s), s.count);
			Gfx_Text(gfx, g->fCode ? g->fCode : t->fMini, buf,
				Ui_Rect(rest.x, rest.y + 18.0f, rest.w, 15.0f),
				t->primary.base, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			for (i = 0; i < 4; i++)
				Gfx_Text(gfx, t->fMini, kStackCalls[i],
					Ui_Rect(rest.x, rest.y + 42.0f + (float)i * 16.0f,
						rest.w, 14.0f), t->text3,
					GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			Gfx_Text(gfx, t->fMini,
				"固定项在前、TakeFill 只能收尾 —— 顺序即布局",
				Ui_Rect(rest.x, rest.y + 112.0f, rest.w, 14.0f), t->hint,
				GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS | GFX_TEXT_WRAP);
		}
	}

	/* ── TakeAligned 对齐演示 ── */
	{
		UiStack row;
		UiRect area = pane_in(alignArea, 12.0f);
		int al = g->layout.align.selected;
		static const char *const kAlNames[3] = {
			"align = UI_ALIGN_START", "align = UI_ALIGN_CENTER",
			"align = UI_ALIGN_END"
		};
		UiColor accs[3];
		accs[0] = t->primary.base;
		accs[1] = t->success.base;
		accs[2] = t->warning.base;
		Gfx_FillRoundRect(gfx, alignArea, 8.0f, Ui_ColorAlpha(t->text3, 8));
		Gfx_StrokeRoundRect(gfx, alignArea, 8.0f,
			Ui_ColorAlpha(t->text3, 80), 1.0f);
		Gfx_Text(gfx, t->fMini, kAlNames[al],
			Ui_Rect(alignArea.x + 10.0f, alignArea.y + 2.0f,
				alignArea.w - 20.0f, 14.0f), t->hint,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		Ui_StackBegin(&row, area, UI_ROW, 12.0f, 0.0f);
		for (i = 0; i < 3; i++) {
			static const char *const kAlBlk[3] = { "块 A", "块 B", "块 C" };
			UiRect b = Ui_StackTakeAligned(&row, 76.0f, 32.0f, al);
			layout_block(gfx, t, b, accs[i], kAlBlk[i], t->text);
		}
	}
}

static const char *const kLayAlignLabels[3] = { "起点", "居中", "终点" };
static const char *const kLayAlignNames[3] = {
	"UI_ALIGN_START", "UI_ALIGN_CENTER", "UI_ALIGN_END"
};

static void layout_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;
	float hh = 26.0f + g->layout.headH.value * 26.0f;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"/* UiStack 游标流: 固定项在前, TakeFill 收尾 */\n"
			"Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f,\n"
			"    14, 14, 14, 14);\n"
			"UiRect head = Ui_StackTake(&col, %.0ff);\n"
			"Ui_StackSkip(&col, 10.0f);\n"
			"UiRect body = Ui_StackTakeFill(&col);\n"
			"\n"
			"/* 行内右贴: Take + 弹簧 + TakeRight */\n"
			"Ui_StackBegin(&row, brow, UI_ROW, 8.0f, 0.0f);\n"
			"UiRect ok = Ui_StackTake(&row, 80.0f);\n"
			"Ui_StackTakeFill(&row);\n"
			"UiRect cancel = Ui_StackTakeRight(&row, 92.0f);\n"
			"\n"
			"/* 对齐: 交叉轴 */\n"
			"UiRect a = Ui_StackTakeAligned(&row,\n"
			"    76.0f, 32.0f, %s);\n",
			hh,
			kLayAlignNames[g->layout.align.selected < 0 ||
				g->layout.align.selected > 2 ? 1 : g->layout.align.selected]);
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "Stack 标题块高度 (Take)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->layout.headH, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	if (g->layout.headH.changed || g->layout.headH.committed)
		UiApp_Invalidate(app);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "对齐 (TakeAligned)");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->layout.align, Ui_StackTake(&col, 34.0f),
		kLayAlignLabels, NULL, NULL, 1);
	if (g->layout.align.changed) UiApp_Invalidate(app);
	Ui_StackSkip(&col, 10.0f);

	{
		UiRect area = pane_in(pv, 12.0f);
		layout_demo(g, area);
	}
}

static void layout_init(Gallery *g)
{
	UiSlider_Init(&g->layout.headH, Ui_Id("gl.lay.hh", 0, 0), 0.15f);
	UiButtonGroup_Init(&g->layout.align, Ui_Id("gl.lay.align", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->layout.align, 1);
}

/* ── Flex 弹性布局 (独立页: 水平 / 垂直双模式) ───────────────────── */

static const char *const kFlexDirLabels[2] = { "水平 (UI_ROW)", "垂直 (UI_COLUMN)" };

static void flex_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	UiContext *ui = &app->ui;
	UiStack col;
	UiRect rowArea, colArea, liteArea;
	float gap = 4.0f + g->flex.gap.value * 20.0f;
	float gr2 = 1.0f + g->flex.w2.value * 3.0f;
	float gr3 = 1.0f + g->flex.w3.value * 3.0f;
	char buf[64];
	int i;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"水平模式 (UI_ROW): measure 协议 Ui_FlexLayout");
	Ui_StackSkip(&col, 4.0f);
	rowArea = Ui_StackTake(&col, 76.0f);
	Ui_StackSkip(&col, 8.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"垂直模式 (UI_COLUMN): 同参数, 方向切到列");
	Ui_StackSkip(&col, 4.0f);
	colArea = Ui_StackTakeFill(&col);
	Ui_StackSkip(&col, 8.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"轻量版 Ui_FlexRow / Ui_FlexCol (basis+grow, 无测量回调)");
	Ui_StackSkip(&col, 4.0f);
	liteArea = Ui_StackTake(&col, 56.0f);

	/* 演示用的模式开关由属性页控制; 两区块同时显示, 方向按钮组即时对比 */
	{
		int dir = g->flex.dir.selected;      /* 0=水平 1=垂直 */
		UiColor accs[3];
		accs[0] = t->primary.base;
		accs[1] = t->success.base;
		accs[2] = t->warning.base;

		/* 水平: measure 协议 */
		{
			UiFixedSize fx[3];
			UiMeasureItem items[3];
			UiRect outs[3];
			float g2 = dir == 0 ? gr2 : gr2;
			fx[0].w = 110.0f; fx[0].h = 0.0f;
			fx[1].w = 0.0f;   fx[1].h = 0.0f;
			fx[2].w = 0.0f;   fx[2].h = 0.0f;
			items[0] = Ui_FixedItem(&fx[0], 0.0f);
			items[1] = Ui_FixedItem(&fx[1], g2);
			items[2] = Ui_FixedItem(&fx[2], gr3);
			if (Ui_FlexLayout(rowArea, UI_ROW, gap, items, 3, outs)) {
				static const char *const kTags[3] = {
					"固定 110", "弹簧 A", "弹簧 B"
				};
				for (i = 0; i < 3; i++) {
					LayoutCard c;
					c.label = kTags[i];
					c.sub = NULL;
					c.minW = 60.0f;
					c.prefH = 40.0f;
					layout_draw_cell(g, outs[i], &c, accs[i]);
					if (i > 0) {
						int gv = (int)((i == 1 ? g2 : gr3) * 10.0f + 0.5f);
						wsprintfA(buf, "grow=%d.%d", gv / 10, gv % 10);
						Gfx_Text(gfx, t->fMini, buf,
							Ui_Rect(outs[i].x + 10.0f, outs[i].y + 28.0f,
								outs[i].w - 20.0f, 16.0f),
							t->text3, GFX_TEXT_VCENTER);
					}
				}
			}
		}

		/* 垂直: 同参数 UI_COLUMN */
		{
			UiFixedSize fx[3];
			UiMeasureItem items[3];
			UiRect outs[3];
			UiRect vArea;
			float g2 = gr2;
			/* 垂直区块里再分左右: 左列演示, 右侧留说明 */
			vArea = Ui_Rect(colArea.x, colArea.y,
				colArea.w * 0.44f, colArea.h);
			fx[0].w = 0.0f; fx[0].h = 34.0f;   /* 垂直时 basis 用 h */
			fx[1].w = 0.0f; fx[1].h = 0.0f;
			fx[2].w = 0.0f; fx[2].h = 0.0f;
			items[0] = Ui_FixedItem(&fx[0], 0.0f);
			items[1] = Ui_FixedItem(&fx[1], g2);
			items[2] = Ui_FixedItem(&fx[2], gr3);
			if (Ui_FlexLayout(vArea, UI_COLUMN, gap, items, 3, outs)) {
				static const char *const kTags[3] = {
					"固定 34", "弹簧 A", "弹簧 B"
				};
				for (i = 0; i < 3; i++) {
					LayoutCard c;
					c.label = kTags[i];
					c.sub = NULL;
					c.minW = 60.0f;
					c.prefH = 30.0f;
					layout_draw_cell(g, outs[i], &c, accs[i]);
					if (i > 0) {
						int gv = (int)((i == 1 ? g2 : gr3) * 10.0f + 0.5f);
						wsprintfA(buf, "grow=%d.%d", gv / 10, gv % 10);
						Gfx_Text(gfx, t->fMini, buf,
							Ui_Rect(outs[i].x + 10.0f, outs[i].y + 26.0f,
								outs[i].w - 20.0f, 14.0f),
							t->text3, GFX_TEXT_VCENTER);
					}
				}
			}
			Gfx_Text(gfx, t->fMini,
				"垂直方向: 主轴是高, basis/grow 作用于高度;\n"
				"弹簧项认领剩余高度, 固定项不受影响。",
				Ui_Rect(colArea.x + colArea.w * 0.46f + 12.0f, colArea.y,
					colArea.w * 0.54f - 12.0f, colArea.h), t->hint,
				GFX_TEXT_WRAP);
		}

		/* 轻量版: Ui_FlexRow (水平, UiFlexItem basis+grow) */
		{
			UiFlexItem it[3];
			UiRect outs[3];
			it[0].basis = 96.0f; it[0].grow = 0.0f;
			it[0].minSize = 0.0f; it[0].maxSize = 0.0f;
			it[1].basis = 0.0f;  it[1].grow = 2.0f;
			it[1].minSize = 0.0f; it[1].maxSize = 0.0f;
			it[2].basis = 0.0f;  it[2].grow = 1.0f;
			it[2].minSize = 0.0f; it[2].maxSize = 0.0f;
			if (Ui_FlexRow(liteArea, 8.0f, it, 3, outs)) {
				static const char *const kTags[3] = {
					"basis 96", "grow 2", "grow 1"
				};
				UiColor acc2[3];
				acc2[0] = t->blue;
				acc2[1] = t->blue2;
				acc2[2] = t->text3;
				for (i = 0; i < 3; i++) {
					LayoutCard c;
					c.label = kTags[i];
					c.sub = NULL;
					c.minW = 60.0f;
					c.prefH = 40.0f;
					layout_draw_cell(g, outs[i], &c, acc2[i]);
				}
				Gfx_Text(gfx, t->fMini, "Ui_FlexCol 同参数即垂直 (样例见属性页代码)",
					Ui_Rect(liteArea.x + liteArea.w - 240.0f,
						liteArea.y - 20.0f, 240.0f, 16.0f),
					t->hint, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			}
		}
	}
	(void)ui;
}

static void flex_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;
	float gr2 = 1.0f + g->flex.w2.value * 3.0f;
	float gr3 = 1.0f + g->flex.w3.value * 3.0f;
	char buf[64];

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"/* Flex 水平: measure 协议 */\n"
			"UiFixedSize fx[3] = { {110,0}, {0,0}, {0,0} };\n"
			"UiMeasureItem it[3] = {\n"
			"    Ui_FixedItem(&fx[0], 0.0f),   /* 固定 */\n"
			"    Ui_FixedItem(&fx[1], %.1ff),   /* 弹簧 */\n"
			"    Ui_FixedItem(&fx[2], %.1ff) };\n"
			"Ui_FlexLayout(rowRc, UI_ROW, %.0ff, it, 3, out);\n"
			"\n"
			"/* 垂直: 同参数换 UI_COLUMN */\n"
			"Ui_FlexLayout(sideRc, UI_COLUMN, %.0ff, it, 3, out);\n"
			"\n"
			"/* 轻量版: UiFlexItem basis+grow */\n"
			"UiFlexItem lite[3] = {\n"
			"    { 96, 0, 0, 0 }, { 0, 2, 0, 0 }, { 0, 1, 0, 0 } };\n"
			"Ui_FlexRow(barRc, 8.0f, lite, 3, out);   /* 或 Ui_FlexCol */\n",
			gr2, gr3,
			4.0f + g->flex.gap.value * 20.0f,
			4.0f + g->flex.gap.value * 20.0f);
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "方向 (主轴)");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->flex.dir, Ui_StackTake(&col, 34.0f),
		kFlexDirLabels, NULL, NULL, 1);
	if (g->flex.dir.changed) UiApp_Invalidate(app);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "间距 (gap)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->flex.gap, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "弹簧 A 权重 (grow)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->flex.w2, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	{
		int gv = (int)(gr2 * 10.0f + 0.5f);
		wsprintfA(buf, "弹簧 A grow = %d.%d", gv / 10, gv % 10);
	}
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "弹簧 B 权重 (grow)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->flex.w3, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	{
		int gv = (int)(gr3 * 10.0f + 0.5f);
		wsprintfA(buf, "弹簧 B grow = %d.%d", gv / 10, gv % 10);
	}
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 10.0f);

	{
		UiRect area = pane_in(pv, 12.0f);
		flex_demo(g, area);
	}
}

static void flex_init(Gallery *g)
{
	UiSlider_Init(&g->flex.gap, Ui_Id("gl.fx.gap", 0, 0), 0.35f);
	UiSlider_Init(&g->flex.w2, Ui_Id("gl.fx.w2", 0, 0), 0.0f);
	UiSlider_Init(&g->flex.w3, Ui_Id("gl.fx.w3", 0, 0), 0.0f);
	UiButtonGroup_Init(&g->flex.dir, Ui_Id("gl.fx.dir", 0, 0),
		UI_BG_SINGLE, 2);
	UiButtonGroup_SetSelected(&g->flex.dir, 0);
}

/* ── Grid 网格布局 (独立页) ──────────────────────────────────────── */

static void grid_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect gridRc, trackRc;
	float gap = 4.0f + g->grid.gap.value * 24.0f;
	int i;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"Ui_GridLayout: fixed / auto / fr 轨道 · span · dense");
	Ui_StackSkip(&col, 4.0f);
	gridRc = Ui_StackTakeFill(&col);
	Ui_StackSkip(&col, 8.0f);

	trackRc = Ui_StackTake(&col, 44.0f);
	Gfx_Text(gfx, t->fMini,
		"轨道: [FIXED 110] [AUTO minmax(90,190)] [FR 1·min 80]   "
		"自动放置 row=-1 col=-1 · dense 时 E 抢占最早空位",
		trackRc, t->hint, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	{
		UiGrid grid;
		UiGridTrack cols[3];
		UiGridItem gridItems[6];
		UiRect cell[6];
		int wide = g->grid.wide.checked;
		int tall = g->grid.tall.checked;
		grid.rows = 0;
		grid.cols = 3;
		grid.rowGap = gap;
		grid.colGap = gap;
		grid.dense = g->grid.dense.checked;
		cols[0].kind = UI_GRID_FIXED; cols[0].value = 110.0f;
		cols[0].minSize = 0.0f; cols[0].maxSize = 0.0f;
		cols[1].kind = UI_GRID_AUTO;  cols[1].value = 0.0f;
		cols[1].minSize = 90.0f; cols[1].maxSize = 190.0f;
		cols[2].kind = UI_GRID_FR;    cols[2].value = 1.0f;
		cols[2].minSize = 80.0f; cols[2].maxSize = 0.0f;

		gridItems[0] = layout_item(&kLcHead, 0, 0, 1, 3);
		gridItems[1] = layout_item(&kLcA, 1, 0, 1, wide ? 2 : 1);
		gridItems[2] = layout_item(&kLcB, 1, wide ? 2 : 1, tall ? 2 : 1, 1);
		gridItems[3] = layout_item(&kLcC, 2, 0, 1, 1);
		gridItems[4] = layout_item(&kLcD, 2, 1, 1, 1);
		gridItems[5] = layout_item(&kLcE, -1, -1, 1, 1);
		if (Ui_GridLayout(gridRc, &grid, cols, NULL, gridItems, 6, cell)) {
			UiColor accents[6];
			accents[0] = t->blue;
			accents[1] = t->primary.base;
			accents[2] = t->warning.base;
			accents[3] = t->success.base;
			accents[4] = t->red;
			accents[5] = t->blue2;
			for (i = 0; i < 6; i++)
				layout_draw_cell(g, cell[i],
					(LayoutCard *)gridItems[i].user, accents[i]);
		}
	}
}

static void grid_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"/* Grid: 显式轨道 + minmax + span + dense */\n"
			"UiGrid g = { .rows=0, .cols=3,\n"
			"    .rowGap=%.0f, .colGap=%.0f, .dense=%d };\n"
			"UiGridTrack cols[3] = {\n"
			"    { UI_GRID_FIXED, 110, 0, 0 },\n"
			"    { UI_GRID_AUTO, 0, 90, 190 },   /* minmax */\n"
			"    { UI_GRID_FR, 1, 80, 0 } };\n"
			"UiGridItem items[6];   /* row/col=-1 自动放置 */\n"
			"Ui_GridLayout(inner, &g, cols, NULL, items, 6, out);\n",
			4.0f + g->grid.gap.value * 24.0f,
			4.0f + g->grid.gap.value * 24.0f,
			g->grid.dense.checked);
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "轨道间距 (gap)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->grid.gap, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	Ui_Checkbox(&g->grid.wide, ui, Ui_StackTake(&col, 26.0f),
		"卡片 A 跨两列 (colSpan=2)", 1);
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->grid.tall, ui, Ui_StackTake(&col, 26.0f),
		"高卡片 B 跨两行 (rowSpan=2)", 1);
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->grid.dense, ui, Ui_StackTake(&col, 26.0f),
		"dense 紧凑回填 (E 抢占空位)", 1);

	{
		UiRect area = pane_in(pv, 12.0f);
		grid_demo(g, area);
	}
}

static void grid_init(Gallery *g)
{
	UiSlider_Init(&g->grid.gap, Ui_Id("gl.gr.gap", 0, 0), 0.35f);
	UiCheckbox_Init(&g->grid.wide, Ui_Id("gl.gr.wide", 0, 0), 1);
	UiCheckbox_Init(&g->grid.tall, Ui_Id("gl.gr.tall", 0, 0), 1);
	UiCheckbox_Init(&g->grid.dense, Ui_Id("gl.gr.dense", 0, 0), 0);
}

/* ── Dialog ────────────────────────────────────────────────────────── */

static void dlg_init(Gallery *g)
{
	UiApp *app = g->app;
	UiDialog_Init(&g->dlg.dlg, Ui_Id("gl.dlg", 0, 0));
	UiDialog_AttachIme(&g->dlg.dlg, &app->ime);
	UiDialog_Init(&g->dlg.confirm, Ui_Id("gl.dlg.confirm", 0, 0));
	g->dlg.okId = Ui_Id("gl.dlg.ok", 0, 0);
	g->dlg.focus[0] = Ui_Id("gl.dlg.notif", 0, 0);
	g->dlg.focus[1] = Ui_Id("gl.dlg.cat", 0, 0);
	g->dlg.focus[2] = g->dlg.okId;
	UiDialog_SetFocusOrder(&g->dlg.dlg, g->dlg.focus, 3);
	UiCheckbox_Init(&g->dlg.notif, g->dlg.focus[0], 1);
	UiSelect_Init(&g->dlg.cat, g->dlg.focus[1]);
	UiSelect_SetItems(&g->dlg.cat, kTagItems, 10);
	UiSelect_SetSelected(&g->dlg.cat, 0);
	UiSlider_Init(&g->dlg.w, Ui_Id("gl.dlg.w", 0, 0), 0.45f);
	UiCheckbox_Init(&g->dlg.danger, Ui_Id("gl.dlg.danger", 0, 0), 1);
}

static int dlg_any_open(Gallery *g)
{
	return UiDialog_IsOpen(&g->dlg.dlg) || UiDialog_IsOpen(&g->dlg.confirm);
}

static void dlg_consume(Gallery *g)
{
	int r = UiDialog_TakeResult(&g->dlg.confirm);
	if (r)
		set_status(g, r == UI_DIALOG_ACCEPT
			? "已确认删除 (演示, 没有删除任何东西)" : "已取消删除");
	r = UiDialog_TakeResult(&g->dlg.dlg);
	if (r)
		set_status(g, r == UI_DIALOG_ACCEPT
			? "对话框完成, 背景输入恢复" : "对话框已取消");
}

static void dlg_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect row;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "标准对话框 (Tab 循环 / Esc 取消 / 动画)");
	Ui_StackSkip(&col, 6.0f);
	row = Ui_StackTake(&col, 38.0f);
	if (Ui_Button(ui, Ui_Id("gl.dlg.open", 0, 0),
			Ui_Rect(row.x, row.y, 150.0f, 36.0f), "打开对话框",
			UI_BTN_PRIMARY, 1))
		UiDialog_Open(&g->dlg.dlg, ui, Ui_Id("gl.dlg.open", 0, 0));
	if (Ui_Button(ui, Ui_Id("gl.dlg.confirm", 0, 0),
			Ui_Rect(row.x + 164.0f, row.y, 150.0f, 36.0f), "删除确认…",
			UI_BTN_DANGER, 1))
		UiDialog_Open(&g->dlg.confirm, ui, Ui_Id("gl.dlg.confirm", 0, 0));
	Ui_StackSkip(&col, 14.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"对话框打开时背景输入被完全隔离, 关闭后焦点回到触发器。\n"
		"内部包含 Checkbox 与 Select, 验证嵌套弹层场景。\n"
		"「属性」页可以调整宽度与危险样式。");
}

static void dlg_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiDialog d;\n"
			"UiDialog_Init(&d, Ui_Id(\"dl.x\", 0, 0));\n"
			"\n"
			"/* 触发: */\n"
			"if (Ui_Button(ui, delBtn, rc, \"删除\", UI_BTN_DANGER, 1))\n"
			"    UiDialog_Open(&d, ui, delBtn);\n"
			"\n"
			"/* 每帧: 宽度 %d DIP */\n"
			"if (UiDialog_Begin(&d, ui, \"删除项目?\",\n"
			"        %.0ff, 190.0f, &body)) {\n"
			"    draw_content(ui, body);\n"
			"    UiDialog_End(&d, ui);\n"
			"}\n"
			"/* after_frame 消费 (一次性取出): */\n"
			"int r = UiDialog_TakeResult(&d);\n"
			"if (r == UI_DIALOG_ACCEPT) delete_item();\n",
			(int)(380.0f + g->dlg.w.value * 260.0f + 0.5f),
			380.0f + g->dlg.w.value * 260.0f);
	}
	char buf[64];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "对话框宽度");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->dlg.w, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	wsprintfA(buf, "width = %d DIP",
		(int)(380.0f + g->dlg.w.value * 260.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 12.0f);

	Ui_Checkbox(&g->dlg.danger, ui, Ui_StackTake(&col, 26.0f),
		"确认框使用危险样式 (红)", 1);

	{
		UiRect area = pane_in(pv, 20.0f);
		UiRect row = Ui_Rect(area.x, area.y + 30.0f, area.w, 38.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"从这里打开同样有效");
		if (Ui_Button(ui, Ui_Id("gl.dlg.open2", 0, 0),
				Ui_Rect(row.x, row.y, 140.0f, 36.0f), "打开对话框",
				UI_BTN_PRIMARY, 1))
			UiDialog_Open(&g->dlg.dlg, ui, Ui_Id("gl.dlg.open2", 0, 0));
		if (Ui_Button(ui, Ui_Id("gl.dlg.confirm2", 0, 0),
				Ui_Rect(row.x + 154.0f, row.y, 140.0f, 36.0f), "删除确认…",
				UI_BTN_DANGER, 1))
			UiDialog_Open(&g->dlg.confirm, ui, Ui_Id("gl.dlg.confirm2", 0, 0));
	}
}

static int dlg_key(Gallery *g, UINT vk)
{
	UiApp *app = g->app;
	UiContext *ui = &app->ui;
	if (UiDialog_IsOpen(&g->dlg.dlg)) {
		if (UiDialog_Key(&g->dlg.dlg, ui, vk)) return 1;
		if (UiCheckbox_Key(&g->dlg.notif, ui, vk)) return 1;
		if (UiSelect_Key(&g->dlg.cat, &g->pm, ui, vk)) return 1;
		if (ui->navId == g->dlg.okId &&
			(vk == VK_RETURN || vk == VK_SPACE)) {
			UiDialog_Close(&g->dlg.dlg, ui, UI_DIALOG_ACCEPT);
			UiApp_Invalidate(app);
			return 1;
		}
		return 1;   /* 打开期间吞掉全部按键 */
	}
	if (UiDialog_IsOpen(&g->dlg.confirm)) {
		if (UiDialog_Key(&g->dlg.confirm, ui, vk)) return 1;
		return 1;
	}
	return 0;
}

static int dlg_chr(Gallery *g, wchar_t ch)
{
	UiApp *app = g->app;
	if (UiDialog_IsOpen(&g->dlg.dlg) && !g->dlg.dlg.closing &&
		!app->ui.popupOpen)
		return UiSelect_Char(&g->dlg.cat, &app->ui, ch);
	return 0;
}

static void dlg_ime(Gallery *g)
{
	UiApp *app = g->app;
	if (UiDialog_IsOpen(&g->dlg.dlg) && !g->dlg.dlg.closing &&
		!app->ui.popupOpen)
		UiSelect_OnIme(&g->dlg.cat, &app->ui);
}

static void dlg_blink(Gallery *g)
{
	UiApp *app = g->app;
	if (UiDialog_IsOpen(&g->dlg.dlg))
		UiSelect_Blink(&g->dlg.cat, &app->ui);
}

static void dlg_popups(Gallery *g)
{
	UiApp *app = g->app;
	UiContext *ui = &app->ui;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	float w = 380.0f + g->dlg.w.value * 260.0f;
	UiRect body;

	if (UiDialog_Begin(&g->dlg.dlg, ui, "通知设置", w, 330.0f, &body)) {
		Gfx_Text(gfx, t->fBody,
			"Tab 在对话框内循环并跳过禁用项; Esc 先关下拉, 再关对话框。",
			Ui_Rect(body.x, body.y, body.w, 40.0f), t->text2, GFX_TEXT_WRAP);
		Ui_Checkbox(&g->dlg.notif, ui,
			Ui_Rect(body.x, body.y + 50.0f, body.w, 28.0f), "启用通知", 1);
		Ui_Select(&g->dlg.cat, &g->pm, ui,
			Ui_Rect(body.x, body.y + 92.0f, body.w, 36.0f), "通知类别", 1);
		if (Ui_Button(ui, g->dlg.okId,
				Ui_Rect(body.x + body.w - 96.0f,
					body.y + body.h - 40.0f, 96.0f, 36.0f),
				"完成", UI_BTN_PRIMARY, 1))
			UiDialog_Close(&g->dlg.dlg, ui, UI_DIALOG_ACCEPT);
		UiSelect_DrawPopup(&g->dlg.cat, &g->pm, ui);
		UiDialog_End(&g->dlg.dlg, ui);
	}
	Ui_ConfirmDialog(&g->dlg.confirm, ui, "删除这个项目？",
		"此操作将移除项目及其设置, 无法撤销。这是一个演示, 不会删除任何文件。",
		"确认删除", "取消", g->dlg.danger.checked);
	dlg_consume(g);
}

static void dlg_tick(Gallery *g, double dtMs)
{
	UiDialog_Tick(&g->dlg.dlg, &g->app->ui, dtMs);
	UiDialog_Tick(&g->dlg.confirm, &g->app->ui, dtMs);
}

static int dlg_anim(Gallery *g)
{
	return UiDialog_Busy(&g->dlg.dlg) || UiDialog_Busy(&g->dlg.confirm) ||
		UiSelect_Busy(&g->dlg.cat);
}

static void dlg_shutdown(Gallery *g)
{
	UiSelect_Shutdown(&g->dlg.cat);
}

/* ── Menu ──────────────────────────────────────────────────────────── */

static const UiMenuItem kMenuFull[] = {
	{ "新建", "Ctrl+N", 0 },
	{ "打开...", "Ctrl+O", 0 },
	{ "保存", "Ctrl+S", 0 },
	{ "", "", UI_MENU_DIVIDER },
	{ "导出为 PNG", "", 0 },
	{ "导出为 SVG", "", UI_MENU_DISABLED },
	{ "", "", UI_MENU_DIVIDER },
	{ "删除项目", "Del", UI_MENU_DANGER }
};
static const UiMenuItem kMenuPlain[] = {
	{ "新建", "Ctrl+N", 0 },
	{ "打开...", "Ctrl+O", 0 },
	{ "保存", "Ctrl+S", 0 },
	{ "", "", UI_MENU_DIVIDER },
	{ "导出为 PNG", "", 0 },
	{ "导出为 SVG", "", 0 },
	{ "", "", UI_MENU_DIVIDER },
	{ "删除项目", "Del", 0 }
};
#define MENU_N 8

static const UiMenuItem kCtxItems[] = {
	{ "复制", "Ctrl+C", 0 },
	{ "粘贴", "Ctrl+V", 0 },
	{ "全选", "Ctrl+A", 0 },
	{ "", "", UI_MENU_DIVIDER },
	{ "刷新", "F5", 0 }
};
#define CTX_N 5

static void menu_init(Gallery *g)
{
	UiMenu_Init(&g->menu.menu, Ui_Id("gl.menu", 0, 0));
	UiMenu_SetItems(&g->menu.menu, kMenuFull, MENU_N);
	UiMenu_Init(&g->menu.ctx, Ui_Id("gl.menu.ctx", 0, 0));
	UiMenu_SetItems(&g->menu.ctx, kCtxItems, CTX_N);
	UiCheckbox_Init(&g->menu.danger, Ui_Id("gl.menu.danger", 0, 0), 1);
	UiCheckbox_Init(&g->menu.disable, Ui_Id("gl.menu.dis", 0, 0), 1);
	g->menu.applied = 0x3;
}

static void menu_apply(Gallery *g)
{
	int want = (g->menu.danger.checked ? 0x1 : 0) |
		(g->menu.disable.checked ? 0x2 : 0);
	if (want == g->menu.applied) return;
	g->menu.applied = want;
	UiMenu_SetItems(&g->menu.menu,
		want == 0x3 ? kMenuFull : kMenuPlain, MENU_N);
}

static void menu_consume(Gallery *g, UiMenu *m, const char *name)
{
	char buf[128];
	if (!m->changed) return;
	m->changed = 0;
	if (m->item >= 0 && m->item < m->count)
		wsprintfA(buf, "%s: %s", name, m->items[m->item].label);
	else
		wsprintfA(buf, "%s: (无)", name);
	set_status(g, buf);
	UiApp_Invalidate(g->app);
}

static void menu_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;

	menu_apply(g);
	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "下拉菜单");
	Ui_StackSkip(&col, 6.0f);
	{
		UiRect row = Ui_StackTake(&col, 38.0f);
		UiRect anchor = Ui_Rect(row.x, row.y, 140.0f, 36.0f);
		if (Ui_Button(ui, Ui_Id("gl.menu.open", 0, 0), anchor, "打开菜单",
				UI_BTN_PRIMARY, 1)) {
			ui->navId = Ui_Id("gl.menu.open", 0, 0);
			UiMenu_OpenAt(&g->menu.menu, &g->pm, ui,
				Ui_Id("gl.menu.open", 0, 0), anchor, 1);
		}
		small_txt(gfx, t,
			Ui_Rect(row.x + 156.0f, row.y, row.w - 156.0f, row.h),
			"在下面的空白区右键打开上下文菜单");
	}
	Ui_StackSkip(&col, 12.0f);
	note_txt(gfx, t, Ui_StackTake(&col, 40.0f),
		"菜单项支持分隔线 / 禁用 / 危险样式与快捷键提示;\n"
		"Esc 或点击外部关闭, 焦点回归触发器。");
	Ui_StackSkip(&col, 8.0f);
	{
		UiRect zone = Ui_StackTakeFill(&col);
		Gfx_FillRoundRect(gfx, zone, 8.0f, Ui_ColorAlpha(t->text3, 12));
		Gfx_StrokeRoundRect(gfx, zone, 8.0f,
			Ui_ColorAlpha(t->text3, 40), 1.0f);
		Gfx_Text(gfx, t->fSmall, "右键区域 (在此按下右键)",
			Ui_Rect(zone.x + 14.0f, zone.y + 10.0f, zone.w - 28.0f, 20.0f),
			t->hint, GFX_TEXT_VCENTER);
	}
}

static void menu_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"static const UiMenuItem items[] = {\n"
			"    { \"新建\", \"Ctrl+N\", 0 },\n"
			"    { \"\", \"\", UI_MENU_DIVIDER },\n"
			"    { \"删除\", \"Del\", %s },\n"
			"    { \"导出SVG\", \"\", %s },\n"
			"};\n"
			"UiMenu m;\n"
			"UiMenu_Init(&m, Ui_Id(\"mn.x\", 0, 0));\n"
			"UiMenu_SetItems(&m, items, 4);\n"
			"\n"
			"/* 触发: 点击 / 右键 (anchor 在光标处) */\n"
			"UiMenu_OpenAt(&m, &pm, ui, owner, anchor, 1);\n"
			"/* 帧末: 画 + 消费选中 */\n"
			"UiMenu_Draw(&m, &pm, ui);\n"
			"if (m.changed) run(items[m.item].label);\n",
			g->menu.danger.checked ? "UI_MENU_DANGER" : "0",
			g->menu.disable.checked ? "UI_MENU_DISABLED" : "0");
	}

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	Ui_Checkbox(&g->menu.danger, ui, Ui_StackTake(&col, 26.0f),
		"「删除项目」使用危险样式", 1);
	Ui_StackSkip(&col, 6.0f);
	Ui_Checkbox(&g->menu.disable, ui, Ui_StackTake(&col, 26.0f),
		"「导出为 SVG」禁用", 1);
	Ui_StackSkip(&col, 10.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"切换后从右侧再打开菜单验证效果。");

	{
		UiRect area = pane_in(pv, 20.0f);
		UiRect anchor = Ui_Rect(area.x, area.y + 32.0f, 140.0f, 36.0f);
		menu_apply(g);
		if (Ui_Button(ui, Ui_Id("gl.menu.open2", 0, 0), anchor, "打开菜单",
				UI_BTN_PRIMARY, 1))
			UiMenu_OpenAt(&g->menu.menu, &g->pm, ui,
				Ui_Id("gl.menu.open2", 0, 0), anchor, 1);
	}
}

static int menu_key(Gallery *g, UINT vk)
{
	if (UiMenu_Key(&g->menu.menu, &g->pm, &g->app->ui, vk)) {
		menu_consume(g, &g->menu.menu, "菜单");
		return 1;
	}
	if (UiMenu_Key(&g->menu.ctx, &g->pm, &g->app->ui, vk)) {
		menu_consume(g, &g->menu.ctx, "右键菜单");
		return 1;
	}
	return 0;
}

static void menu_mouse(Gallery *g, int msg, float x, float y)
{
	if (msg == UI_APP_MOUSE_RDOWN && !UiPopupManager_AnyOpen(&g->pm)) {
		UiMenu_OpenAt(&g->menu.ctx, &g->pm, &g->app->ui, UI_ID_NONE,
			Ui_Rect(x, y, 0.0f, 0.0f), 0);
		UiApp_Invalidate(g->app);
	}
}

static void menu_popups(Gallery *g)
{
	UiMenu_Draw(&g->menu.menu, &g->pm, &g->app->ui);
	menu_consume(g, &g->menu.menu, "菜单");
	UiMenu_Draw(&g->menu.ctx, &g->pm, &g->app->ui);
	menu_consume(g, &g->menu.ctx, "右键菜单");
}

/* ── Toast 页 ──────────────────────────────────────────────────────── */

static const char *const kToastKindLabels[3] = { "信息", "成功", "错误" };
static const int kToastKinds[3] = {
	UI_TOAST_INFO, UI_TOAST_SUCCESS, UI_TOAST_ERROR
};

static void toast_init(Gallery *g)
{
	UiInput_Init(&g->toastP.form, &g->app->ui, &g->app->ime);
	g->toastP.fld = UiInput_Add(&g->toastP.form,
		Ui_Id("gl.toast.text", 0, 0), 127, 0, NULL, L"Toast 文案");
	Edit_SetTextUtf8(UiInput_Edit(&g->toastP.form, g->toastP.fld),
		"这是一条轻提示");
	UiButtonGroup_Init(&g->toastP.kind, Ui_Id("gl.toast.kind", 0, 0),
		UI_BG_SINGLE, 3);
	UiButtonGroup_SetSelected(&g->toastP.kind, 0);
	UiSlider_Init(&g->toastP.dur, Ui_Id("gl.toast.dur", 0, 0), 0.3f);
}

static void toast_show(Gallery *g, int kind, const char *text)
{
	int dur = 1000 + (int)(g->toastP.dur.value * 4000.0f + 0.5f);
	UiToast_Show(&g->toast, text, UiFrame_Now(), dur, kind);
	set_status(g, text);
}

static void toast_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect row;
	char text[192];

	text[0] = 0;
	if (UiInput_Edit(&g->toastP.form, g->toastP.fld))
		Edit_TextUtf8(UiInput_Edit(&g->toastP.form, g->toastP.fld),
			text, (int)sizeof(text));
	if (!text[0]) lstrcpynA(text, "轻提示", (int)sizeof(text));

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "三种 kind (图标自动)");
	Ui_StackSkip(&col, 6.0f);
	row = Ui_StackTake(&col, 38.0f);
	if (Ui_Button(ui, Ui_Id("gl.toast.info", 0, 0),
			Ui_Rect(row.x, row.y, 110.0f, 36.0f), "信息",
			UI_BTN_SECONDARY, 1))
		toast_show(g, UI_TOAST_INFO, text);
	if (Ui_Button(ui, Ui_Id("gl.toast.ok", 0, 0),
			Ui_Rect(row.x + 122.0f, row.y, 110.0f, 36.0f), "成功",
			UI_BTN_PRIMARY, 1))
		toast_show(g, UI_TOAST_SUCCESS, text);
	if (Ui_Button(ui, Ui_Id("gl.toast.err", 0, 0),
			Ui_Rect(row.x + 244.0f, row.y, 110.0f, 36.0f), "错误",
			UI_BTN_DANGER, 1))
		toast_show(g, UI_TOAST_ERROR, text);
	Ui_StackSkip(&col, 14.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"合成器模式: 渐变遮罩与卡片画在独立 GfxLayer 上, 只画一次,\n"
		"淡入淡出由 DWM 补间 —— 显示期间主窗口零重绘。\n"
		"点击遮罩 / 卡片或按 Esc 关闭。\n"
		"「属性」页可以修改文案 / kind / 时长。");
}

static void toast_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		static const char *const kToastKindNames[3] = {
			"UI_TOAST_INFO", "UI_TOAST_SUCCESS", "UI_TOAST_ERROR"
		};
		char text2[192];
		text2[0] = 0;
		Edit_TextUtf8(UiInput_Edit(&g->toastP.form, g->toastP.fld),
			text2, (int)sizeof(text2));
		if (!text2[0]) lstrcpynA(text2, "轻提示", (int)sizeof(text2));
		snprintf(g->codeText, sizeof(g->codeText),
			"UiToast_Init(&toast);\n"
			"UiToast_Attach(&toast, &app->ui);\n"
			"\n"
			"/* 任意业务处触发: */\n"
			"UiToast_Show(&toast, \"%s\",\n"
			"    UiFrame_Now(), %d, %s);\n"
			"\n"
			"/* 点击关闭: 帧内置 pending, after_frame 执行 */\n"
			"if (ui->clicked == UiToast_BackdropId() ||\n"
			"    ui->clicked == UiToast_CardId()) hide = 1;\n",
			text2,
			1000 + (int)(g->toastP.dur.value * 4000.0f + 0.5f),
			kToastKindNames[g->toastP.kind.selected]);
	}
	char text[192];
	char buf[64];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "文案");
	Ui_StackSkip(&col, 4.0f);
	UiInput_BeginFrame(&g->toastP.form);
	UiInput_SetRect(&g->toastP.form, g->toastP.fld, Ui_StackTake(&col, 36.0f));
	UiInput_Draw(&g->toastP.form, g->toastP.fld);
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "kind");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->toastP.kind, Ui_StackTake(&col, 34.0f),
		kToastKindLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 10.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "时长");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->toastP.dur, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	wsprintfA(buf, "durationMs = %d",
		1000 + (int)(g->toastP.dur.value * 4000.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 10.0f);

	text[0] = 0;
	if (UiInput_Edit(&g->toastP.form, g->toastP.fld))
		Edit_TextUtf8(UiInput_Edit(&g->toastP.form, g->toastP.fld),
			text, (int)sizeof(text));
	if (!text[0]) lstrcpynA(text, "轻提示", (int)sizeof(text));

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"用当前配置弹一个");
		if (Ui_Button(ui, Ui_Id("gl.toast.pv", 0, 0),
				Ui_Rect(area.x, area.y + 32.0f, 150.0f, 38.0f),
				"显示 Toast", UI_BTN_PRIMARY, 1))
			toast_show(g, kToastKinds[g->toastP.kind.selected], text);
	}
}

static int toast_key(Gallery *g, UINT vk)
{
	return UiInput_Key(&g->toastP.form, g->app->hwnd, vk);
}

static int toast_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->toastP.form, ch);
}

static void toast_ime(Gallery *g)
{
	UiInput_OnIme(&g->toastP.form);
}

static void toast_blink(Gallery *g)
{
	UiInput_Blink(&g->toastP.form);
}

static void toast_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->toastP.form, g->app, msg, x, y);
}

/* ── Tooltip ───────────────────────────────────────────────────────── */

static const uint32_t kTipIcons[3] = { 0xE74Eu, 0xE72Cu, 0xE74Du };
static const char *const kTipTexts[3] = {
	"保存  Ctrl+S", "刷新  F5", "删除  Del"
};
static const char *const kTipNames[3] = { "保存", "刷新", "删除" };

static UiRect g_tipRects[3];

static void tip_init(Gallery *g)
{
	/* g->tip 已在 on_init 统一 Init/SetDelay; 这里只建本页滑块 */
	UiSlider_Init(&g->tipP.delay, Ui_Id("gl.tip.delay", 0, 0), 0.375f);
}

static int tip_update_targets(Gallery *g)
{
	UiApp *app = g->app;
	UiContext *ui = &app->ui;
	int i, matched = 0;
	for (i = 0; i < 3; i++) {
		UiId id = Ui_IdIndex("gl.tip.btn", i, 0, 0);
		UiRect rc = Ui_Rect(ui->mouseX, 0.0f, 0.0f, 0.0f);
		(void)rc;
		if (ui->hover == id) {
			UiRect r = g_tipRects[i];
			UiTooltip_Update(&g->tip, ui, id, r, kTipTexts[i],
				UiFrame_Now());
			matched = 1;
			break;
		}
	}
	if (!matched)
		UiTooltip_Update(&g->tip, ui, UI_ID_NONE, Ui_Rect(0, 0, 0, 0),
			"", UiFrame_Now());
	return matched;
}

static void tip_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect row;
	int i;
	UiButtonStyle style = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "悬停图标按钮 (延迟 450ms)");
	Ui_StackSkip(&col, 6.0f);
	row = Ui_StackTake(&col, 40.0f);
	for (i = 0; i < 3; i++) {
		UiRect ir = Ui_Rect(row.x + i * 52.0f, row.y, 40.0f, 38.0f);
		UiId id = Ui_IdIndex("gl.tip.btn", i, 0, 0);
		g_tipRects[i] = ir;
		if (t->fIcon) {
			UiIcon ic = UiIcon_Glyph(t->fIcon, kTipIcons[i]);
			if (Ui_IconButton(ui, id, ir, &ic, &style, 1))
				set_status(g, kTipNames[i]);
		} else {
			if (Ui_ButtonEx(ui, id, ir, kTipNames[i], &style, 1))
				set_status(g, kTipNames[i]);
		}
	}
	Ui_StackSkip(&col, 14.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"Tooltip 不注册命中, 不会挡住下面的控件;\n"
		"悬停超过延迟才出现, 目标消失自动关闭。\n"
		"「属性」页可以调整延迟。");
	tip_update_targets(g);
}

static void tip_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiTooltip tip;\n"
			"UiTooltip_Init(&tip, Ui_Id(\"tip.x\", 0, 0));\n"
			"UiTooltip_SetDelay(&tip, %d);   /* 悬停延迟 ms */\n"
			"\n"
			"/* 每帧: 喂当前悬停目标 */\n"
			"UiTooltip_Update(&tip, ui, btnId, btnRc,\n"
			"    \"保存 Ctrl+S\", UiFrame_Now());\n"
			"if (!matched)   /* 目标消失: 自动关闭 */\n"
			"    UiTooltip_Update(&tip, ui, UI_ID_NONE,\n"
			"        Ui_Rect(0, 0, 0, 0), \"\", UiFrame_Now());\n"
			"UiTooltip_Draw(&tip, ui);   /* 帧末, 最上层 */\n"
			"/* animating: UiTooltip_Pending(&tip) */\n",
			(int)(g->tipP.delay.value * 1200.0f + 0.5f));
	}
	char buf[64];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "延迟");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->tipP.delay, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	wsprintfA(buf, "delayMs = %d",
		(int)(g->tipP.delay.value * 1200.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	UiTooltip_SetDelay(&g->tip, (int)(g->tipP.delay.value * 1200.0f + 0.5f));

	{
		UiRect area = pane_in(pv, 20.0f);
		UiButtonStyle style = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
			UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
		UiRect ir = Ui_Rect(area.x + area.w * 0.5f - 20.0f,
			area.y + area.h * 0.5f - 20.0f, 40.0f, 38.0f);
		UiId id = Ui_IdIndex("gl.tip.btn", 0, 0, 0);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"悬停这个按钮试试");
		g_tipRects[0] = ir;
		if (t->fIcon) {
			UiIcon ic = UiIcon_Glyph(t->fIcon, kTipIcons[0]);
			Ui_IconButton(ui, id, ir, &ic, &style, 1);
		} else {
			Ui_ButtonEx(ui, id, ir, "保存", &style, 1);
		}
		if (ui->hover == id)
			UiTooltip_Update(&g->tip, ui, id, ir, kTipTexts[0],
				UiFrame_Now());
		else
			UiTooltip_Update(&g->tip, ui, UI_ID_NONE,
				Ui_Rect(0, 0, 0, 0), "", UiFrame_Now());
	}
}

static int tip_anim(Gallery *g)
{
	return UiTooltip_Pending(&g->tip);
}

/* ── Progress ──────────────────────────────────────────────────────── */

static void prog_init(Gallery *g)
{
	UiProgress_Init(&g->prog.prog, &g->app->ui, 6000);
	UiSlider_Init(&g->prog.sl, Ui_Id("gl.prog.sl", 0, 0), 0.42f);
	UiCheckbox_Init(&g->prog.paused, Ui_Id("gl.prog.paused", 0, 0), 0);
}

static void prog_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect row;
	char buf[64];

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 20.0f, 16.0f, 20.0f, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"自循环 (6 秒一圈, 填充在合成器 overlay 上)");
	Ui_StackSkip(&col, 4.0f);
	UiProgress_Draw(&g->prog.prog, Ui_StackTake(&col, 10.0f));
	Ui_StackSkip(&col, 10.0f);
	row = Ui_StackTake(&col, 34.0f);
	if (Ui_Button(ui, Ui_Id("gl.prog.pause", 0, 0),
			Ui_Rect(row.x, row.y, 110.0f, 32.0f),
			UiProgress_Paused(&g->prog.prog) ? "播放" : "暂停",
			UI_BTN_SECONDARY, 1))
		UiProgress_SetPaused(&g->prog.prog,
			!UiProgress_Paused(&g->prog.prog));
	wsprintfA(buf, "value = %d%%",
		(int)(UiProgress_Value(&g->prog.prog) * 100.0f + 0.5f));
	small_txt(gfx, t, Ui_Rect(row.x + 124.0f, row.y, 120.0f, row.h), buf);
	Ui_StackSkip(&col, 16.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "静态条 (绑定下面的滑块)");
	Ui_StackSkip(&col, 4.0f);
	Ui_ProgressBar(ui, Ui_StackTake(&col, 10.0f), g->prog.sl.value, 1);
	Ui_StackSkip(&col, 12.0f);
	Ui_Slider(&g->prog.sl, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 14.0f);

	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"合成器模式下前台播放时应用进程 CPU 约 0% (overlay 由 DWM 补间);\n"
		"ULW 回退自动改为在主窗内绘制填充, 行为不变。");
}

static void prog_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiProgress p;\n"
			"UiProgress_Init(&p, Ui_Id(\"pg.x\", 0, 0));\n"
			"UiProgress_SetPaused(&p, %d);\n"
			"\n"
			"on_frame:  UiProgress_Draw(&p, rc);\n"
			"on_tick:   UiProgress_Tick(&p, dtMs);\n"
			"on_after:  UiProgress_EndFrame(&p);\n"
			"\n"
			"/* 静态条: 值是普通参数 (%d%%) */\n"
			"Ui_ProgressBar(ui, rc, %.2ff, 1);\n",
			UiProgress_Paused(&g->prog.prog) ? 1 : 0,
			(int)(g->prog.sl.value * 100.0f + 0.5f),
			g->prog.sl.value);
	}
	char buf[64];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	Ui_Checkbox(&g->prog.paused, ui, Ui_StackTake(&col, 26.0f),
		"暂停自循环", 1);
	UiProgress_SetPaused(&g->prog.prog, g->prog.paused.checked);
	Ui_StackSkip(&col, 10.0f);
	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "静态条取值");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->prog.sl, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	wsprintfA(buf, "value = %d%%",
		(int)(g->prog.sl.value * 100.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f), "实时预览");
		Ui_ProgressBar(ui, Ui_Rect(area.x, area.y + 34.0f, area.w, 10.0f),
			g->prog.sl.value, 1);
	}
}

static int prog_anim(Gallery *g)
{
	return !UiProgress_Paused(&g->prog.prog);
}

/* ── Image ─────────────────────────────────────────────────────────── */

static const char *const kImgFitLabels[4] = {
	"留白", "剪裁", "拉伸", "原始"
};
static const int kImgFits[4] = {
	UI_IMG_FIT_CONTAIN, UI_IMG_FIT_COVER, UI_IMG_FIT_STRETCH, UI_IMG_FIT_NONE
};

static int make_bmp_mem(unsigned char **out, int W, int H)
{
	int stride = (W * 3 + 3) & ~3;
	int size = 54 + stride * H;
	unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY, (SIZE_T)size);
	BITMAPFILEHEADER *fh;
	BITMAPINFOHEADER *ih;
	int x, y;
	if (!buf) return 0;
	fh = (BITMAPFILEHEADER *)buf;
	ih = (BITMAPINFOHEADER *)(buf + sizeof(BITMAPFILEHEADER));
	fh->bfType = 0x4D42;
	fh->bfSize = (DWORD)size;
	fh->bfOffBits = 54;
	ih->biSize = 40;
	ih->biWidth = W;
	ih->biHeight = H;
	ih->biPlanes = 1;
	ih->biBitCount = 24;
	ih->biSizeImage = (DWORD)(stride * H);
	for (y = 0; y < H; y++) {
		unsigned char *row = buf + 54 + (H - 1 - y) * stride;
		for (x = 0; x < W; x++) {
			int r = 40 + x * 180 / W;
			int gg = 60 + y * 150 / H;
			int b = 200 - x * 120 / W;
			if (((x / 32) + (y / 32)) & 1) { r += 18; gg += 12; }
			row[x * 3 + 0] = (unsigned char)b;
			row[x * 3 + 1] = (unsigned char)gg;
			row[x * 3 + 2] = (unsigned char)r;
		}
	}
	*out = buf;
	return size;
}

static void img_init(Gallery *g)
{
	unsigned char *bmp = NULL;
	int size = make_bmp_mem(&bmp, 480, 300);
	g->img.img = UiImage_Create(g->app->gfx, g->net);
	if (g->img.img) {
		UiImage_SetContext(g->img.img, &g->app->ui);
		if (bmp && size > 0)
			UiImage_SetBytes(g->img.img, bmp, size);
	}
	if (bmp) HeapFree(GetProcessHeap(), 0, bmp);
	UiButtonGroup_Init(&g->img.fit, Ui_Id("gl.img.fit", 0, 0),
		UI_BG_SINGLE, 4);
	UiButtonGroup_SetSelected(&g->img.fit, 0);
	UiCheckbox_Init(&g->img.bg, Ui_Id("gl.img.bg", 0, 0), 0);
	g->img.anchor = UI_IMG_ANCHOR_MC;
}

static void img_shutdown(Gallery *g)
{
	if (g->img.img) {
		UiImage_Destroy(g->img.img);
		g->img.img = NULL;
	}
}

static const char *img_state_name(int st)
{
	switch (st) {
	case UI_IMG_LOADING: return "加载中";
	case UI_IMG_READY: return "就绪";
	case UI_IMG_ERROR: return "错误";
	default: return "空闲";
	}
}

static void img_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect box, meta;
	char buf[128];
	int st;

	Ui_StackBegin(&col, rc, UI_COLUMN, 0.0f, 0.0f);
	meta = Ui_StackTake(&col, 20.0f);
	Ui_StackSkip(&col, 10.0f);
	box = Ui_StackTakeFill(&col);

	st = UiImage_State(g->img.img);
	UiImage_SetRect(g->img.img, box);
	UiImage_SetFit(g->img.img, kImgFits[g->img.fit.selected]);
	UiImage_SetAnchor(g->img.img, g->img.anchor);
	UiImage_SetBackground(g->img.img,
		g->img.bg.checked ? 0xFFF3F5F9 : 0x00000000);
	UiImage_Draw(g->img.img, t);

	wsprintfA(buf, "状态: %s · 像素 %d x %d · fit=%d · anchor=%d",
		img_state_name(st), UiImage_PixelWidth(g->img.img),
		UiImage_PixelHeight(g->img.img),
		g->img.fit.selected, g->img.anchor);
	small_txt(gfx, t, meta, buf);
}

static void img_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		static const char *const kImgFitNames[4] = {
			"UI_IMG_FIT_CONTAIN", "UI_IMG_FIT_COVER",
			"UI_IMG_FIT_STRETCH", "UI_IMG_FIT_NONE"
		};
		static const char *const kImgAnchorNames[9] = {
			"UI_IMG_ANCHOR_TL", "UI_IMG_ANCHOR_TC", "UI_IMG_ANCHOR_TR",
			"UI_IMG_ANCHOR_ML", "UI_IMG_ANCHOR_MC", "UI_IMG_ANCHOR_MR",
			"UI_IMG_ANCHOR_BL", "UI_IMG_ANCHOR_BC", "UI_IMG_ANCHOR_BR"
		};
		snprintf(g->codeText, sizeof(g->codeText),
			"UiImage *img = UiImage_Create(gfx, net);\n"
			"UiImage_SetUrl(img, \"https://.../a.png\");\n"
			"UiImage_SetRect(img, rc);\n"
			"UiImage_SetFit(img, %s);\n"
			"UiImage_SetAnchor(img, %s);\n"
			"UiImage_SetBackground(img, 0x%08X);   /* 0 = 透明 */\n"
			"UiImage_Draw(img, t);\n"
			"\n"
			"/* 状态机轮询 (无回调): */\n"
			"switch (UiImage_State(img)) {\n"
			"case UI_IMG_READY:   break;\n"
			"case UI_IMG_ERROR:   draw_placeholder(rc); break;\n"
			"}\n",
			kImgFitNames[g->img.fit.selected],
			kImgAnchorNames[g->img.anchor],
			g->img.bg.checked ? 0xFFF3F5F9u : 0x00000000u);
	}
	int i, j;

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "适配模式 (fit)");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->img.fit, Ui_StackTake(&col, 34.0f),
		kImgFitLabels, NULL, NULL, 1);
	Ui_StackSkip(&col, 12.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "取景锚点 (3x3)");
	Ui_StackSkip(&col, 4.0f);
	{
		UiRect grid = Ui_StackTake(&col, 84.0f);
		for (j = 0; j < 3; j++) {
			for (i = 0; i < 3; i++) {
				int a = j * 3 + i;
				UiId id = Ui_IdIndex("gl.img.anc", a, 0, 0);
				UiRect ar = Ui_Rect(grid.x + i * 34.0f,
					grid.y + j * 28.0f, 30.0f, 24.0f);
				UiWidgetState st = Ui_State(ui, id, ar, 1);
				int sel = g->img.anchor == a;
				Gfx_FillRoundRect(gfx, ar, 5.0f, sel
					? t->primary.base
					: (st.hovered ? Ui_ColorAlpha(t->text3, 40)
						: Ui_ColorAlpha(t->text3, 18)));
				Gfx_FillRoundRect(gfx, Ui_Rect(
					ar.x + ar.w * 0.5f - 2.5f,
					ar.y + ar.h * 0.5f - 2.5f, 5.0f, 5.0f),
					2.5f, sel ? 0xFFFFFFFF : t->text3);
				if (ui->clicked == id) {
					g->img.anchor = a;
					UiApp_Invalidate(app);
				}
			}
		}
	}
	Ui_StackSkip(&col, 10.0f);
	Ui_Checkbox(&g->img.bg, ui, Ui_StackTake(&col, 26.0f),
		"铺留白底色", 1);

	{
		UiRect area = pane_in(pv, 16.0f);
		img_demo(g, area);
	}
}

static int img_anim(Gallery *g)
{
	return g->img.img ? UiImage_Busy(g->img.img) : 0;
}

/* ── Markdown 页 ───────────────────────────────────────────────────── */

static const char *const kMdDocA =
	"# 文档 A: 容器与表格\n"
	"\n"
	"::: tip 提示容器\n"
	"这是 `::: tip` 容器, 左侧色条 + 标题 + 低饱和底色。\n"
	":::\n"
	"\n"
	"::: warning 警告容器\n"
	"警告内容随主题切换颜色。\n"
	":::\n"
	"\n"
	"### 任务列表\n"
	"\n"
	"- [x] GFM 表格\n"
	"- [x] 任务列表\n"
	"- [x] `:::` 容器\n"
	"- [ ] 演示锚点跳转\n"
	"\n"
	"### 表格\n"
	"\n"
	"| 功能 | 状态 | 说明 |\n"
	"|:--|:-:|--:|\n"
	"| 表格 | 完成 | 对齐随分隔符 |\n"
	"| 容器 | 完成 | note/tip/warning/danger |\n"
	"| 图片 | 完成 | 对齐 + 显式尺寸 |\n"
	"\n"
	"### 排版标签\n"
	"\n"
	"<center>这一行由 center 标签居中</center>\n"
	"\n"
	"<div align=\"center\" style=\"padding:8px 12px; line-height:1.8\">\n"
	"div 内的 **Markdown** 继续递归解析, 样式同时生效。\n"
	"</div>\n";

static const char *const kMdDocB =
	"# 文档 B: 数学公式\n"
	"\n"
	"行内公式 $E = mc^2$ 与 $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ 跟随文字排版。\n"
	"\n"
	"$$\n"
	"\\int_{0}^{1} x^2 \\, dx = \\frac{1}{3}\n"
	"$$\n"
	"\n"
	"\\[\n"
	"\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}\n"
	"\\]\n"
	"\n"
	"$$\n"
	"f(x) = \\begin{cases} x, & x \\ge 0 \\\\ -x, & x < 0 \\end{cases}\n"
	"$$\n"
	"\n"
	"超长行内公式按可用宽自动缩小后继续正文, 不会撑破布局。\n";

static void mdp_init(Gallery *g)
{
	g->mdp.md = UiMarkdown_Create(g->app->gfx, Ui_Id("gl.md2", 0, 0),
		g->net);
	if (g->mdp.md)
		UiMarkdown_SetText(g->mdp.md, kMdDocA);
	g->mdp.curDoc = 0;
	g->mdp.lastLS = -1.0f;
	UiSlider_Init(&g->mdp.lineSpacing, Ui_Id("gl.mdp.ls", 0, 0), 0.25f);
}

static void mdp_shutdown(Gallery *g)
{
	if (g->mdp.md) {
		UiMarkdown_Destroy(g->mdp.md);
		g->mdp.md = NULL;
	}
}

static void mdp_apply_style(Gallery *g)
{
	float ls = 1.0f + g->mdp.lineSpacing.value * 1.0f;
	uint32_t rev = g->app->theme.themeRev;
	if (ls == g->mdp.lastLS && rev == g->mdp.lastRev) return;
	g->mdp.lastLS = ls;
	g->mdp.lastRev = rev;
	if (g->mdp.md) {
		UiMarkdownStyle st = UiMarkdownStyle_FromTheme(&g->app->theme);
		st.lineSpacing = ls;
		UiMarkdown_SetStyle(g->mdp.md, &st);
	}
}

static void mdp_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect row, view;
	const char *clicked;

	mdp_apply_style(g);
	Ui_StackBegin(&col, rc, UI_COLUMN, 0.0f, 0.0f);
	row = Ui_StackTake(&col, 36.0f);
	Ui_StackSkip(&col, 8.0f);
	view = Ui_StackTakeFill(&col);

	if (Ui_Button(ui, Ui_Id("gl.mdp.a", 0, 0),
			Ui_Rect(row.x, row.y, 96.0f, 32.0f), "文档 A",
			g->mdp.curDoc == 0 ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1)) {
		if (g->mdp.md) {
			UiMarkdown_SetText(g->mdp.md, kMdDocA);
			UiMarkdown_ScrollToTop(g->mdp.md);
		}
		g->mdp.curDoc = 0;
	}
	if (Ui_Button(ui, Ui_Id("gl.mdp.b", 0, 0),
			Ui_Rect(row.x + 108.0f, row.y, 96.0f, 32.0f), "文档 B",
			g->mdp.curDoc == 1 ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1)) {
		if (g->mdp.md) {
			UiMarkdown_SetText(g->mdp.md, kMdDocB);
			UiMarkdown_ScrollToTop(g->mdp.md);
		}
		g->mdp.curDoc = 1;
	}
	if (Ui_Button(ui, Ui_Id("gl.mdp.top", 0, 0),
			Ui_Rect(row.x + 216.0f, row.y, 110.0f, 32.0f), "回到顶部",
			UI_BTN_SECONDARY, 1)) {
		UiMarkdown_ScrollToTop(g->mdp.md);
	}
	small_txt(gfx, t,
		Ui_Rect(row.x + 340.0f, row.y, row.w - 340.0f, row.h),
		"文档内 [链接](#slug) 与目录跳转都走粘性定位");

	if (g->mdp.md) {
		Ui_Markdown(g->mdp.md, ui, view);
		clicked = UiMarkdown_ClickedUrl(g->mdp.md);
		if (clicked && clicked[0])
			set_status(g, clicked);
	}
}

static void mdp_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiMarkdown *md = UiMarkdown_Create(gfx, id, net);\n"
			"UiMarkdown_SetText(md, docUtf8);\n"
			"\n"
			"/* 样式: NULL = 跟随主题 */\n"
			"UiMarkdownStyle st;\n"
			"st.lineSpacing = %.2ff;\n"
			"UiMarkdown_SetStyle(md, &st);\n"
			"\n"
			"/* 链接事件: */\n"
			"UiMarkdown_SetLinkFn(md, on_link, user);\n"
			"/* 每帧: */\n"
			"Ui_Markdown(md, ui, rc);\n"
			"/* animating: UiMarkdown_Busy(md) */\n",
			1.0f + g->mdp.lineSpacing.value);
	}
	char buf[96];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "行距倍数 (lineSpacing)");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->mdp.lineSpacing, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	{
		int lv = (int)((1.0f + g->mdp.lineSpacing.value) * 100.0f + 0.5f);
		wsprintfA(buf, "lineSpacing = %d.%02d", lv / 100, lv % 100);
	}
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 8.0f);
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f),
		"其余样式默认跟随主题 (SetStyle(NULL) 恢复)。");
	Ui_StackSkip(&col, 10.0f);

	{
		UiRect area = pane_in(pv, 8.0f);
		mdp_apply_style(g);
		if (g->mdp.md)
			Ui_Markdown(g->mdp.md, ui, area);
	}
}

static int mdp_anim(Gallery *g)
{
	return g->mdp.md ? UiMarkdown_Busy(g->mdp.md) : 0;
}

/* ── Theme ─────────────────────────────────────────────────────────── */

static const char *const kThemePresetLabels[5] = {
	"浅色", "深色", "橙色品牌", "蓝色品牌", "墨绿(深)"
};
static const UiThemeColors kBrandOrange = {
	0xFFE4572E, 0xFFF2A93B, 0xFF2FA56B, 0xFFD64545, 0xFFF3F5F9
};
static const UiThemeColors kBrandBlue = {
	0xFF2F6BE4, 0xFFE4A12F, 0xFF2FA56B, 0xFFD64545, 0xFFF3F5F9
};
static const UiThemeColors kBrandTeal = {
	0xFF2FA5A0, 0xFFE4A12F, 0xFF57C08A, 0xFFE4576B, 0xFF2A3138
};

static void theme_apply_preset(Gallery *g, int idx)
{
	UiTheme *t = &g->app->theme;
	switch (idx) {
	case 1: UiTheme_SetTheme(t, UI_THEME_DARK); break;
	case 2: UiTheme_SetColors(t, UI_THEME_LIGHT, &kBrandOrange); break;
	case 3: UiTheme_SetColors(t, UI_THEME_LIGHT, &kBrandBlue); break;
	case 4: UiTheme_SetColors(t, UI_THEME_DARK, &kBrandTeal); break;
	default: UiTheme_SetTheme(t, UI_THEME_LIGHT); break;
	}
	g->theme.applied = idx;
	UiApp_Invalidate(g->app);
}

static void theme_init(Gallery *g)
{
	UiButtonGroup_Init(&g->theme.preset, Ui_Id("gl.theme.preset", 0, 0),
		UI_BG_SINGLE, 5);
	UiButtonGroup_SetSelected(&g->theme.preset, 0);
	g->theme.applied = 0;
}

static void theme_draw_swatches(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	typedef struct ThemeSwatch { const char *name; UiColor c; } ThemeSwatch;
	ThemeSwatch sw[28];
	int n = 0, i, cols, cellW = 108.0f, cellH = 64.0f;
	float x0 = rc.x, y = rc.y;

	sw[n++] = (ThemeSwatch){ "page", t->page };
	sw[n++] = (ThemeSwatch){ "page2", t->page2 };
	sw[n++] = (ThemeSwatch){ "card", t->card };
	sw[n++] = (ThemeSwatch){ "border", t->border };
	sw[n++] = (ThemeSwatch){ "text", t->text };
	sw[n++] = (ThemeSwatch){ "text2", t->text2 };
	sw[n++] = (ThemeSwatch){ "text3", t->text3 };
	sw[n++] = (ThemeSwatch){ "hint", t->hint };
	sw[n++] = (ThemeSwatch){ "primary", t->primary.base };
	sw[n++] = (ThemeSwatch){ "primary.hover", t->primary.hover };
	sw[n++] = (ThemeSwatch){ "primary.soft", t->primary.soft };
	sw[n++] = (ThemeSwatch){ "primary.text", t->primary.text };
	sw[n++] = (ThemeSwatch){ "success", t->success.base };
	sw[n++] = (ThemeSwatch){ "success.soft", t->success.soft };
	sw[n++] = (ThemeSwatch){ "warning", t->warning.base };
	sw[n++] = (ThemeSwatch){ "warning.soft", t->warning.soft };
	sw[n++] = (ThemeSwatch){ "error", t->error.base };
	sw[n++] = (ThemeSwatch){ "error.soft", t->error.soft };
	sw[n++] = (ThemeSwatch){ "neutral", t->neutral.base };
	sw[n++] = (ThemeSwatch){ "neutral.soft", t->neutral.soft };
	sw[n++] = (ThemeSwatch){ "blue", t->blue };
	sw[n++] = (ThemeSwatch){ "blue2", t->blue2 };
	sw[n++] = (ThemeSwatch){ "green", t->green };
	sw[n++] = (ThemeSwatch){ "gold", t->gold };
	sw[n++] = (ThemeSwatch){ "red", t->red };
	sw[n++] = (ThemeSwatch){ "orange", t->orange };
	sw[n++] = (ThemeSwatch){ "switchOn", t->switchOn };
	sw[n++] = (ThemeSwatch){ "panelBg", t->panelBg };

	cols = (int)(rc.w / (cellW + 10.0f));
	if (cols < 1) cols = 1;
	for (i = 0; i < n; i++) {
		int c = i % cols;
		int r = i / cols;
		UiRect cell = Ui_Rect(x0 + c * (cellW + 10.0f),
			y + r * (cellH + 8.0f), cellW, cellH);
		if (cell.y + cell.h > rc.y + rc.h) break;
		Gfx_FillRoundRect(gfx, Ui_Rect(cell.x, cell.y, cell.w, 40.0f),
			7.0f, sw[i].c);
		Gfx_StrokeRoundRect(gfx, Ui_Rect(cell.x, cell.y, cell.w, 40.0f),
			7.0f, t->border, 1.0f);
		Gfx_Text(gfx, t->fMini, sw[i].name,
			Ui_Rect(cell.x, cell.y + 44.0f, cell.w, 16.0f),
			t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
}

static void theme_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect swRc;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 8.0f, 4.0f, 8.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "预设 (一次调用, 所有窗口同步换肤)");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->theme.preset, Ui_StackTake(&col, 34.0f),
		kThemePresetLabels, NULL, NULL, 1);
	if (g->theme.preset.changed)
		theme_apply_preset(g, g->theme.preset.selected);
	Ui_StackSkip(&col, 10.0f);

	swRc = Ui_StackTakeFill(&col);
	theme_draw_swatches(g, swRc);
}

static void theme_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"/* 当前预设: %s */\n"
			"UiTheme_SetTheme(&app->theme, %s);\n"
			"UiApp_Invalidate(app);\n"
			"\n"
			"/* 品牌色: 五个语义色一组 */\n"
			"static const UiThemeColors brand = {\n"
			"    0xFF2F6BE4, 0xFFE4A12F, 0xFF2FA56B,\n"
			"    0xFFD64545, 0xFFF3F5F9 };\n"
			"UiTheme_SetColors(t, UI_THEME_LIGHT, &brand);\n",
			kThemePresetLabels[g->theme.applied],
			g->theme.applied == 1 || g->theme.applied == 4
				? "UI_THEME_DARK" : "UI_THEME_LIGHT");
	}
	char buf[64];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "预设");
	Ui_StackSkip(&col, 4.0f);
	UiButtonGroup_Draw(ui, &g->theme.preset, Ui_StackTake(&col, 56.0f),
		kThemePresetLabels, NULL, NULL, 1);
	if (g->theme.preset.changed)
		theme_apply_preset(g, g->theme.preset.selected);
	Ui_StackSkip(&col, 10.0f);
	wsprintfA(buf, "themeId = %s",
		t->themeId == UI_THEME_DARK ? "UI_THEME_DARK" : "UI_THEME_LIGHT");
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 10.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"SetColors 只需要 5 个语义 base 色, hover/press/soft/text/border\n"
		"自动按同一套规则派生; themeRev +1 让 overlay/layer 自动重建。\n"
		"右侧色板实时反映当前令牌。");

	{
		UiRect area = pane_in(pv, 8.0f);
		theme_draw_swatches(g, area);
	}
}

/* ── Net ───────────────────────────────────────────────────────────── */

static void net_http_cb(void *user, const UiHttpResponse *resp)
{
	Gallery *g = (Gallery *)user;
	const char *body;
	int i, n;

	g->netp.req = 0;
	g->netp.loading = 0;
	g->netp.code = 0;
	g->netp.len = 0;
	g->netp.ctype[0] = 0;
	g->netp.preview[0] = 0;
	if (!resp || resp->status < 200 || resp->status >= 300) {
		set_status(g, resp ? UiNet_StrError(resp->error)
			: "请求失败 (无响应)");
		UiApp_Invalidate(g->app);
		return;
	}
	g->netp.code = resp->status;
	g->netp.len = resp->bodyLen;
	UiHttp_Header(resp, "content-type", g->netp.ctype,
		(int)sizeof(g->netp.ctype));
	body = (const char *)resp->body;
	n = resp->bodyLen < (int)sizeof(g->netp.preview) - 1
		? resp->bodyLen : (int)sizeof(g->netp.preview) - 1;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)body[i];
		g->netp.preview[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
	}
	g->netp.preview[n] = 0;
	wsprintfA(g->status, "HTTP %d · %d 字节 · %.60s", resp->status,
		resp->bodyLen, g->netp.ctype);
	UiApp_Invalidate(g->app);
}

static void net_send(Gallery *g)
{
	UiHttpRequest req;
	char url[512];

	url[0] = 0;
	if (UiInput_Edit(&g->netp.form, g->netp.url))
		Edit_TextUtf8(UiInput_Edit(&g->netp.form, g->netp.url),
			url, (int)sizeof(url));
	if (!url[0]) {
		set_status(g, "请输入 URL");
		return;
	}
	if (g->netp.req) {
		UiHttp_Cancel(g->net, g->netp.req);
		g->netp.req = 0;
	}
	ZeroMemory(&req, sizeof(req));
	req.url = url;
	req.timeoutMs = 1000 + (int)(g->netp.timeout.value * 29000.0f + 0.5f);
	g->netp.loading = 1;
	g->netp.code = 0;
	g->netp.len = 0;
	g->netp.preview[0] = 0;
	g->netp.req = UiHttp_Send(g->net, &req, net_http_cb, g);
	if (!g->netp.req) {
		g->netp.loading = 0;
		set_status(g, "请求创建失败");
	} else {
		set_status(g, "请求中…");
	}
}

static void net_every_cb(void *user, uint64_t id)
{
	Gallery *g = (Gallery *)user;
	(void)id;
	g->netp.ticks++;
	UiApp_Invalidate(g->app);
}

static void net_after_cb(void *user, uint64_t id)
{
	Gallery *g = (Gallery *)user;
	(void)id;
	g->netp.afterOn = 0;
	g->netp.afterId = 0;
	UiToast_Show(&g->toast, "定时器触发: 3 秒到了", UiFrame_Now(), 2200,
		UI_TOAST_INFO);
	set_status(g, "定时器触发 (UiTimer_After)");
	UiApp_Invalidate(g->app);
}

static void net_init(Gallery *g)
{
	UiInput_Init(&g->netp.form, &g->app->ui, &g->app->ime);
	g->netp.url = UiInput_Add(&g->netp.form, Ui_Id("gl.net.url", 0, 0),
		255, 0, NULL, L"https://…");
	Edit_SetTextUtf8(UiInput_Edit(&g->netp.form, g->netp.url),
		"https://www.example.com/");
	UiSlider_Init(&g->netp.timeout, Ui_Id("gl.net.tmo", 0, 0), 0.4f);
	g->netp.everyOn = 0;
	g->netp.afterOn = 0;
}

static void net_shutdown(Gallery *g)
{
	if (g->netp.everyId && g->app->timer)
		UiTimer_Cancel(g->app->timer, g->netp.everyId);
	if (g->netp.afterId && g->app->timer)
		UiTimer_Cancel(g->app->timer, g->netp.afterId);
	if (g->netp.req && g->net)
		UiHttp_Cancel(g->net, g->netp.req);
	if (g->net) {
		UiNet_Destroy(g->net);
		g->net = NULL;
	}
}

static void net_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect row, res, timers;
	char buf[192];

	Ui_StackBegin(&col, rc, UI_COLUMN, 0.0f, 0.0f);
	row = Ui_StackTake(&col, 38.0f);
	Ui_StackSkip(&col, 8.0f);
	res = Ui_StackTake(&col, 118.0f);
	Ui_StackSkip(&col, 12.0f);
	timers = Ui_StackTakeFill(&col);

	UiInput_BeginFrame(&g->netp.form);
	UiInput_SetRect(&g->netp.form, g->netp.url,
		Ui_Rect(row.x, row.y, row.w - 250.0f, 36.0f));
	UiInput_Draw(&g->netp.form, g->netp.url);
	if (Ui_Button(ui, Ui_Id("gl.net.go", 0, 0),
			Ui_Rect(row.x + row.w - 236.0f, row.y, 90.0f, 36.0f),
			g->netp.loading ? "请求中…" : "发送 GET",
			UI_BTN_PRIMARY, !g->netp.loading))
		net_send(g);
	if (Ui_Button(ui, Ui_Id("gl.net.p1", 0, 0),
			Ui_Rect(row.x + row.w - 136.0f, row.y, 60.0f, 36.0f),
			"预设1", UI_BTN_SECONDARY, 1))
		Edit_SetTextUtf8(UiInput_Edit(&g->netp.form, g->netp.url),
			"https://www.example.com/");
	if (Ui_Button(ui, Ui_Id("gl.net.p2", 0, 0),
			Ui_Rect(row.x + row.w - 68.0f, row.y, 60.0f, 36.0f),
			"预设2", UI_BTN_SECONDARY, 1))
		Edit_SetTextUtf8(UiInput_Edit(&g->netp.form, g->netp.url),
			"https://httpbin.org/get");

	Ui_Panel(ui, res);
	if (g->netp.code) {
		wsprintfA(buf, "HTTP %d · %d 字节", g->netp.code, g->netp.len);
		Gfx_Text(gfx, t->fLabel, buf,
			Ui_Rect(res.x + 16.0f, res.y + 12.0f, res.w - 32.0f, 20.0f),
			t->success.base, GFX_TEXT_VCENTER);
	} else if (g->netp.loading) {
		Gfx_Text(gfx, t->fLabel, "请求中…",
			Ui_Rect(res.x + 16.0f, res.y + 12.0f, res.w - 32.0f, 20.0f),
			t->warning.base, GFX_TEXT_VCENTER);
	} else {
		Gfx_Text(gfx, t->fLabel, "响应 (点「发送 GET」)",
			Ui_Rect(res.x + 16.0f, res.y + 12.0f, res.w - 32.0f, 20.0f),
			t->text3, GFX_TEXT_VCENTER);
	}
	Gfx_Text(gfx, t->fSmall,
		g->netp.ctype[0] ? g->netp.ctype : "Content-Type: -",
		Ui_Rect(res.x + 16.0f, res.y + 36.0f, res.w - 32.0f, 18.0f),
		t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(gfx, t->fSmall,
		g->netp.preview[0] ? g->netp.preview : "(正文预览: 前 255 字节, 非 ASCII 显示为 .)",
		Ui_Rect(res.x + 16.0f, res.y + 58.0f, res.w - 32.0f, 52.0f),
		t->text2, GFX_TEXT_WRAP);

	/* 定时器演示 */
	Ui_Panel(ui, timers);
	lbl(gfx, t, Ui_Rect(timers.x + 16.0f, timers.y + 12.0f, 260.0f, 20.0f),
		"UiTimer (GUI 线程定时器)");
	{
		UiRect r1 = Ui_Rect(timers.x + 16.0f, timers.y + 42.0f,
			(timers.w - 42.0f) * 0.5f, 88.0f);
		UiRect r2 = Ui_Rect(timers.x + 26.0f + (timers.w - 42.0f) * 0.5f,
			timers.y + 42.0f, (timers.w - 42.0f) * 0.5f, 88.0f);
		Gfx_FillRoundRect(gfx, r1, 8.0f, Ui_ColorAlpha(t->text3, 14));
		Gfx_FillRoundRect(gfx, r2, 8.0f, Ui_ColorAlpha(t->text3, 14));
		wsprintfA(buf, "每 1s +1 · 已计 %d", g->netp.ticks);
		Gfx_Text(gfx, t->fBody, buf,
			Ui_Rect(r1.x + 12.0f, r1.y + 10.0f, r1.w - 24.0f, 22.0f),
			t->text2, GFX_TEXT_VCENTER);
		if (Ui_Button(ui, Ui_Id("gl.net.every", 0, 0),
				Ui_Rect(r1.x + 12.0f, r1.y + r1.h - 36.0f, 120.0f, 28.0f),
				g->netp.everyOn ? "停止" : "启动",
				g->netp.everyOn ? UI_BTN_SECONDARY : UI_BTN_PRIMARY, 1)) {
			if (g->netp.everyOn) {
				if (g->netp.everyId && app->timer)
					UiTimer_Cancel(app->timer, g->netp.everyId);
				g->netp.everyId = 0;
				g->netp.everyOn = 0;
			} else if (app->timer) {
				g->netp.everyId = UiTimer_Every(app->timer, 1000,
					net_every_cb, g);
				g->netp.everyOn = g->netp.everyId != 0;
			}
			set_status(g, g->netp.everyOn ? "定时器已启动"
				: "定时器已停止");
		}
		Gfx_Text(gfx, t->fBody, g->netp.afterOn ? "3 秒后触发…" : "一次性任务",
			Ui_Rect(r2.x + 12.0f, r2.y + 10.0f, r2.w - 24.0f, 22.0f),
			t->text2, GFX_TEXT_VCENTER);
		if (Ui_Button(ui, Ui_Id("gl.net.after", 0, 0),
				Ui_Rect(r2.x + 12.0f, r2.y + r2.h - 36.0f, 120.0f, 28.0f),
				g->netp.afterOn ? "等待中…" : "3 秒后提醒",
				UI_BTN_PRIMARY, !g->netp.afterOn)) {
			if (app->timer) {
				g->netp.afterId = UiTimer_After(app->timer, 3000,
					net_after_cb, g);
				g->netp.afterOn = g->netp.afterId != 0;
				set_status(g, "已安排 3 秒后回调");
			}
		}
	}
}

static void net_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	/* 实时代码 */
	{
		snprintf(g->codeText, sizeof(g->codeText),
			"UiNet *net = UiNet_Create(NULL);\n"
			"\n"
			"UiHttpRequest req;\n"
			"req.url = \"https://httpbin.org/get\";\n"
			"req.timeoutMs = %d;     /* 滑块当前值 */\n"
			"uint64_t id = UiHttp_Send(net, &req, on_resp, app);\n"
			"\n"
			"/* 回调在 GUI 线程 (APC 归位 + UiTimer 分发): */\n"
			"static void on_resp(void *u, const UiHttpResponse *r) {\n"
			"    if (!r || r->status == 0) return;  /* 失败/取消 */\n"
			"    /* r->body 借用指针: 留存先拷贝 */\n"
			"}\n"
			"\n"
			"/* 定时器事件: */\n"
			"UiTimer_Every(app->timer, 1000, on_tick, user);\n"
			"UiTimer_After(app->timer, 3000, on_once, user);\n",
			1000 + (int)(g->netp.timeout.value * 29000.0f + 0.5f));
	}
	char buf[96];

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "请求超时");
	Ui_StackSkip(&col, 4.0f);
	Ui_Slider(&g->netp.timeout, ui, Ui_StackTake(&col, 30.0f), NULL, 1);
	Ui_StackSkip(&col, 8.0f);
	wsprintfA(buf, "timeoutMs = %d",
		1000 + (int)(g->netp.timeout.value * 29000.0f + 0.5f));
	small_txt(gfx, t, Ui_StackTake(&col, 18.0f), buf);
	Ui_StackSkip(&col, 12.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"超时按请求设置 (UiHttpRequest.timeoutMs);\n"
		"连接/DNS/重定向/代理等在 UiNetConfig 里, 0 字段用默认。\n"
		"回调在 GUI 线程执行, 可以直接更新 UI。\n"
		"响应体是借用指针, 回调返回即失效 —— 需要留存先拷贝。");

	{
		UiRect area = pane_in(pv, 16.0f);
		Ui_Panel(ui, area);
		Gfx_Text(gfx, t->fLabel, "最近响应",
			Ui_Rect(area.x + 14.0f, area.y + 10.0f, area.w - 28.0f, 20.0f),
			t->text2, GFX_TEXT_VCENTER);
		Gfx_Text(gfx, t->fSmall,
			g->netp.code ? "已有结果, 见「演示」页" : "暂无",
			Ui_Rect(area.x + 14.0f, area.y + 36.0f, area.w - 28.0f, 20.0f),
			t->text3, GFX_TEXT_VCENTER);
	}
}

static int net_key(Gallery *g, UINT vk)
{
	if (UiInput_Key(&g->netp.form, g->app->hwnd, vk)) return 1;
	if (vk == VK_RETURN) {
		net_send(g);
		UiApp_Invalidate(g->app);
		return 1;
	}
	return 0;
}

static int net_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->netp.form, ch);
}

static void net_ime(Gallery *g)
{
	UiInput_OnIme(&g->netp.form);
}

static void net_blink(Gallery *g)
{
	UiInput_Blink(&g->netp.form);
}

static void net_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->netp.form, g->app, msg, x, y);
}

/* ════════════════════════════════════════════════════════════════════
 * WebSocket 客户端 / 服务端 / App 框架页
 * ════════════════════════════════════════════════════════════════════ */

static void wsc_log(Gallery *g, const char *s)
{
	int n = lstrlenA(s);
	if (n <= 0) return;
	if (g->wsc.logLen + n + 2 > (int)sizeof(g->wsc.log)) {
		int cut = g->wsc.logLen / 2;
		while (cut < g->wsc.logLen && g->wsc.log[cut] != '\n') cut++;
		if (cut < g->wsc.logLen) cut++;
		memmove(g->wsc.log, g->wsc.log + cut, (size_t)(g->wsc.logLen - cut));
		g->wsc.logLen -= cut;
	}
	if (g->wsc.logLen + n + 1 > (int)sizeof(g->wsc.log))
		n = (int)sizeof(g->wsc.log) - g->wsc.logLen - 1;
	memcpy(g->wsc.log + g->wsc.logLen, s, (size_t)n);
	g->wsc.logLen += n;
	g->wsc.log[g->wsc.logLen++] = '\n';
	g->wsc.log[g->wsc.logLen] = '\0';
	if (g->app) UiApp_Invalidate(g->app);
}

static void wsc_on_open(void *user)
{
	Gallery *g = (Gallery *)user;
	g->wsc.state = 1;
	wsc_log(g, "[ws] 已连接");
	set_status(g, "WebSocket 已连接");
}

static void wsc_on_message(void *user, const void *data, int len, int opcode)
{
	Gallery *g = (Gallery *)user;
	char buf[320];
	int i, n = len < 200 ? len : 200;
	(void)opcode;
	g->wsc.msgCount++;
	buf[0] = 0;
	for (i = 0; i < n; i++) {
		char c = ((const char *)data)[i];
		buf[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
	}
	buf[n] = 0;
	wsc_log(g, buf);
}

static void wsc_on_close(void *user, int code, const char *reason)
{
	Gallery *g = (Gallery *)user;
	char buf[96];
	g->wsc.ws = NULL;
	g->wsc.state = 0;
	wsprintfA(buf, "[ws] 已关闭 (code=%d%s%s)", code,
		reason && reason[0] ? ", " : "", reason && reason[0] ? reason : "");
	wsc_log(g, buf);
	if (g->app) UiApp_Invalidate(g->app);
}

static const UiWsCallbacks kWscCb = { wsc_on_open, wsc_on_message, wsc_on_close };

static void wsc_connect(Gallery *g);   /* wsc_init 里 autostart 先用 */

static void wsc_init(Gallery *g)
{
	UiInput_Init(&g->wsc.form, &g->app->ui, &g->app->ime);
	g->wsc.url = UiInput_Add(&g->wsc.form,
		Ui_Id("gl.wsc.url", 0, 0), 255, 0, NULL, L"ws:// 地址");
	Edit_SetTextUtf8(UiInput_Edit(&g->wsc.form, g->wsc.url),
		"ws://127.0.0.1:18082/ws");
	g->wsc.msg = UiInput_Add(&g->wsc.form,
		Ui_Id("gl.wsc.msg", 0, 0), 127, 0, NULL, L"发送内容");
	Edit_SetTextUtf8(UiInput_Edit(&g->wsc.form, g->wsc.msg), "你好 CUI");
	UiCheckbox_Init(&g->wsc.autoRe, Ui_Id("gl.wsc.re", 0, 0), 1);
	g->wsc.logLen = 0;
	g->wsc.log[0] = 0;
	if (g_wsAuto && g->page == PG_WS) {
		/* autostart: 自动连接 (联调验证; 需先起 wssrv autostart 实例) */
		set_status(g, "自动连接 (autostart)");
		wsc_connect(g);
	}
}

static void wsc_connect(Gallery *g)
{
	char url[256];
	UiWsOptions opt;
	if (g->wsc.ws) return;
	url[0] = 0;
	Edit_TextUtf8(UiInput_Edit(&g->wsc.form, g->wsc.url), url,
		(int)sizeof(url));
	if (!url[0]) { set_status(g, "请先填写 ws:// 地址"); return; }
	ZeroMemory(&opt, sizeof(opt));
	opt.autoReconnect = g->wsc.autoRe.checked;
	opt.pingIntervalMs = 15000;
	g->wsc.ws = UiWs_Open(g->net, url, &opt, &kWscCb, g);
	g->wsc.state = 2;
	set_status(g, "正在连接 WebSocket…");
	wsc_log(g, "[ws] 开始连接 (自动重连开/关随属性页)");
}

static void wsc_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect logRc;
	char buf[128];

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f);
	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"连接 (默认连本机 18082 端口, 先去「WS 服务端」页启动)");
	Ui_StackSkip(&col, 6.0f);
	{
		UiRect row = Ui_StackTake(&col, 36.0f);
		UiInput_BeginFrame(&g->wsc.form);
		UiInput_SetRect(&g->wsc.form, g->wsc.url,
			Ui_Rect(row.x, row.y, row.w - 220.0f, row.h));
		UiInput_Draw(&g->wsc.form, g->wsc.url);
		if (Ui_Button(ui, Ui_Id("gl.wsc.conn", 0, 0),
				Ui_Rect(row.x + row.w - 212.0f, row.y, 100.0f, 32.0f),
				g->wsc.ws ? "断开" : "连接",
				g->wsc.ws ? UI_BTN_SECONDARY : UI_BTN_PRIMARY, 1)) {
			if (g->wsc.ws) {
				UiWs_Close(g->wsc.ws, 1000, "用户断开");
				g->wsc.state = 2;
				wsc_log(g, "[ws] 用户断开");
			} else {
				wsc_connect(g);
			}
		}
		wsprintfA(buf, "%s · 收到 %d 条",
			g->wsc.state == 1 ? "已连接" : (g->wsc.state == 2 ? "连接中" : "未连接"),
			g->wsc.msgCount);
		small_txt(gfx, t, Ui_Rect(row.x + row.w - 106.0f, row.y, 106.0f, row.h),
			buf);
	}
	Ui_StackSkip(&col, 10.0f);
	{
		UiRect row = Ui_StackTake(&col, 36.0f);
		UiInput_SetRect(&g->wsc.form, g->wsc.msg,
			Ui_Rect(row.x, row.y, row.w - 160.0f, row.h));
		UiInput_Draw(&g->wsc.form, g->wsc.msg);
		if (Ui_Button(ui, Ui_Id("gl.wsc.send", 0, 0),
				Ui_Rect(row.x + row.w - 150.0f, row.y, 140.0f, 32.0f),
				"发送 (text)", UI_BTN_PRIMARY, g->wsc.state == 1)) {
			char msg[128];
			msg[0] = 0;
			Edit_TextUtf8(UiInput_Edit(&g->wsc.form, g->wsc.msg), msg,
				(int)sizeof(msg));
			if (msg[0] && UiWs_Send(g->wsc.ws, msg, lstrlenA(msg), 1)) {
				wsc_log(g, msg);
				set_status(g, "已发送 (服务端原样回显)");
			} else if (msg[0]) {
				set_status(g, "发送失败: 未连接");
			}
		}
	}
	Ui_StackSkip(&col, 10.0f);
	logRc = Ui_StackTakeFill(&col);
	Gfx_FillRoundRect(gfx, logRc, 10.0f, t->card);
	Gfx_StrokeRoundRect(gfx, logRc, 10.0f, t->border, 1.0f);
	small_txt(gfx, t, Ui_Rect(logRc.x + 12.0f, logRc.y + 4.0f,
		logRc.w - 24.0f, 20.0f), "消息日志 (回显 / 事件)");
	{
		UiRect area = Ui_RectInset(logRc, 10.0f);
		area.y += 28.0f;
		area.h -= 30.0f;
		const char *p = g->wsc.log;
		float y = area.y;
		while (p && *p && y + 16.0f <= area.y + area.h) {
			const char *nl = p;
			while (*nl && *nl != '\n') nl++;
			Gfx_Text(gfx, t->fMini, p, Ui_Rect(area.x, y, area.w, 15.0f),
				t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			y += 16.0f;
			p = (*nl == '\n') ? nl + 1 : nl;
		}
	}
}

static void wsc_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	snprintf(g->codeText, sizeof(g->codeText),
		"/* 回调 (GUI 线程触发): */\n"
		"static void on_open(void *u)                 { /* 连上 */ }\n"
		"static void on_msg(void *u, const void *d,\n"
		"                 int len, int opcode)     { /* 1=text 2=binary */ }\n"
		"static void on_close(void *u, int c,\n"
		"                 const char *reason)      { /* 只来一次 */ }\n"
		"static const UiWsCallbacks cb = {\n"
		"    on_open, on_msg, on_close };\n"
		"\n"
		"UiWsOptions opt;   /* autoReconnect=1 退避重连 */\n"
		"void *ws = UiWs_Open(net, \"ws://127.0.0.1:18082/ws\",\n"
		"    &opt, &cb, user);\n"
		"UiWs_Send(ws, \"hello\", 5, 1);   /* 1=text */\n"
		"UiWs_Close(ws, 1000, \"再见\");\n"
		"/* 状态: UiWs_State(ws); */\n");

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "ws:// 地址");
	Ui_StackSkip(&col, 4.0f);
	UiInput_BeginFrame(&g->wsc.form);
	UiInput_SetRect(&g->wsc.form, g->wsc.url, Ui_StackTake(&col, 34.0f));
	UiInput_Draw(&g->wsc.form, g->wsc.url);
	Ui_StackSkip(&col, 10.0f);

	Ui_Checkbox(&g->wsc.autoRe, ui, Ui_StackTake(&col, 26.0f),
		"自动重连 (退避 + 抖动)", 1);
	Ui_StackSkip(&col, 10.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"右下角是客户端接线代码;\n"
		"「演示」页可连接 / 断开 / 收发消息。\n"
		"与服务端页在本机即可联调。");
}

static int wsc_key(Gallery *g, UINT vk)
{
	return UiInput_Key(&g->wsc.form, g->app->hwnd, vk);
}

static int wsc_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->wsc.form, ch);
}

static void wsc_ime(Gallery *g)
{
	UiInput_OnIme(&g->wsc.form);
}

static void wsc_blink(Gallery *g)
{
	UiInput_Blink(&g->wsc.form);
}

static void wsc_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->wsc.form, g->app, msg, x, y);
}

static void wsc_shutdown(Gallery *g)
{
	if (g->wsc.ws) {
		UiWs_Close(g->wsc.ws, 1000, "窗口关闭");
		g->wsc.ws = NULL;
	}
}

/* ── WebSocket 服务端 ───────────────────────────────────────────────── */

static void wss_log(Gallery *g, const char *s)
{
	int n = lstrlenA(s);
	if (n <= 0) return;
	if (g->wss.logLen + n + 2 > (int)sizeof(g->wss.log)) {
		int cut = g->wss.logLen / 2;
		while (cut < g->wss.logLen && g->wss.log[cut] != '\n') cut++;
		if (cut < g->wss.logLen) cut++;
		memmove(g->wss.log, g->wss.log + cut, (size_t)(g->wss.logLen - cut));
		g->wss.logLen -= cut;
	}
	if (g->wss.logLen + n + 1 > (int)sizeof(g->wss.log))
		n = (int)sizeof(g->wss.log) - g->wss.logLen - 1;
	memcpy(g->wss.log + g->wss.logLen, s, (size_t)n);
	g->wss.logLen += n;
	g->wss.log[g->wss.logLen++] = '\n';
	g->wss.log[g->wss.logLen] = '\0';
	if (g->app) UiApp_Invalidate(g->app);
}

static void wss_srv_listening(void *user, int port)
{
	Gallery *g = (Gallery *)user;
	char b[96];
	g->wss.portShown = port;
	wsprintfA(b, "[srv] 监听 0.0.0.0:%d (路径 /ws)", port);
	wss_log(g, b);
	set_status(g, b);
}

static void wss_srv_open(void *user, UiWsConn *conn, const UiWsServerRequest *req)
{
	Gallery *g = (Gallery *)user;
	char b[128];
	g->wss.totalConns++;
	wsprintfA(b, "[conn] %s 打开 (path=%s)",
		req && req->peer ? req->peer : "?", req && req->path ? req->path : "?");
	wss_log(g, b);
	(void)conn;
}

static void wss_srv_message(void *user, UiWsConn *conn, const void *data, int len, int opcode)
{
	Gallery *g = (Gallery *)user;
	char b[256];
	const char *peer = UiWsConn_Peer(conn);
	int i, n = len < 160 ? len : 160;
	(void)opcode;
	g->wss.msgCount++;
	b[0] = 0;
	for (i = 0; i < n; i++) {
		char c = ((const char *)data)[i];
		b[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
	}
	b[n] = 0;
	wss_log(g, b);
	/* 原样回显: 演示服务端主动 Send */
	UiWsServer_Send(conn, data, len, opcode);
	(void)peer;
}

static void wss_srv_close(void *user, UiWsConn *conn, int code, const char *reason)
{
	Gallery *g = (Gallery *)user;
	char b[128];
	wsprintfA(b, "[conn] %s 关闭 (%d)", UiWsConn_Peer(conn), code);
	wss_log(g, b);
	(void)reason;
}

static void wss_srv_error(void *user, int err, const char *message)
{
	Gallery *g = (Gallery *)user;
	char b[160];
	wsprintfA(b, "[srv] 错误 %d: %s", err, message ? message : "?");
	wss_log(g, b);
}

static const UiWsServerCallbacks kWssCb = {
	wss_srv_listening, wss_srv_open, wss_srv_message, wss_srv_close,
	wss_srv_error
};

static void wss_start(Gallery *g)
{
	UiWsServerConfig cfg;
	char port[16], path[64];
	if (g->wss.srv) return;
	port[0] = 0;
	Edit_TextUtf8(UiInput_Edit(&g->wss.form, g->wss.port), port,
		(int)sizeof(port));
	path[0] = 0;
	Edit_TextUtf8(UiInput_Edit(&g->wss.form, g->wss.path), path,
		(int)sizeof(path));
	ZeroMemory(&cfg, sizeof(cfg));
	cfg.port = port[0] ? atoi(port) : 18082;
	if (cfg.port <= 0 || cfg.port > 65535) cfg.port = 18082;
	cfg.path = path[0] ? path : "/ws";
	cfg.maxConnections = 16;
	cfg.maxMessageSize = 1 << 20;
	g->wss.srv = UiWsServer_Create(g->net, &cfg, &kWssCb, g);
	g->wss.state = g->wss.srv ? UI_WS_SRV_STARTING : UI_WS_SRV_ERROR;
	if (!g->wss.srv) wss_log(g, "[srv] Create 失败 (参数非法?)");
}

static void wss_stop(Gallery *g)
{
	if (!g->wss.srv) return;
	wss_log(g, "[srv] 停止监听并关闭全部连接");
	UiWsServer_Destroy(g->wss.srv);
	g->wss.srv = NULL;
	g->wss.state = UI_WS_SRV_STOPPED;
	g->wss.portShown = 0;
}

static void wss_init(Gallery *g)
{
	UiInput_Init(&g->wss.form, &g->app->ui, &g->app->ime);
	g->wss.port = UiInput_Add(&g->wss.form,
		Ui_Id("gl.wss.port", 0, 0), 15, 0, NULL, L"端口");
	Edit_SetTextUtf8(UiInput_Edit(&g->wss.form, g->wss.port), "18082");
	g->wss.path = UiInput_Add(&g->wss.form,
		Ui_Id("gl.wss.path", 0, 0), 63, 0, NULL, L"路径");
	Edit_SetTextUtf8(UiInput_Edit(&g->wss.form, g->wss.path), "/ws");
	g->wss.msg = UiInput_Add(&g->wss.form,
		Ui_Id("gl.wss.msg", 0, 0), 127, 0, NULL, L"广播内容");
	Edit_SetTextUtf8(UiInput_Edit(&g->wss.form, g->wss.msg), "服务端广播");
	g->wss.logLen = 0;
	g->wss.log[0] = 0;
	g->wss.state = UI_WS_SRV_STOPPED;
	if (g_wsAuto && g->page == PG_WSSRV) {
		set_status(g, "自动启动监听 (autostart)");
		wss_start(g);
	}
}

static void wss_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect logRc;
	char buf[128];

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f);
	{
		UiRect row = Ui_StackTake(&col, 36.0f);
		if (Ui_Button(ui, Ui_Id("gl.wss.toggle", 0, 0),
				Ui_Rect(row.x, row.y, 120.0f, 32.0f),
				g->wss.srv ? "停止服务" : "启动服务",
				g->wss.srv ? UI_BTN_DANGER : UI_BTN_PRIMARY, 1)) {
			if (g->wss.srv) wss_stop(g);
			else wss_start(g);
		}
		wsprintfA(buf, "%s · 端口 %d · 连接 %d",
			g->wss.state == UI_WS_SRV_LISTENING ? "监听中" :
			(g->wss.state == UI_WS_SRV_STARTING ? "启动中" : "已停止"),
			g->wss.portShown ? g->wss.portShown : 18082,
			g->wss.srv ? UiWsServer_ConnCount(g->wss.srv) : 0);
		small_txt(gfx, t, Ui_Rect(row.x + 132.0f, row.y, row.w - 132.0f, row.h),
			buf);
	}
	Ui_StackSkip(&col, 8.0f);
	{
		UiRect row = Ui_StackTake(&col, 36.0f);
		UiInput_BeginFrame(&g->wss.form);
		UiInput_SetRect(&g->wss.form, g->wss.msg,
			Ui_Rect(row.x, row.y, row.w - 150.0f, row.h));
		UiInput_Draw(&g->wss.form, g->wss.msg);
		if (Ui_Button(ui, Ui_Id("gl.wss.bc", 0, 0),
				Ui_Rect(row.x + row.w - 140.0f, row.y, 130.0f, 32.0f),
				"广播 (broadcast)", UI_BTN_SECONDARY,
				g->wss.srv && UiWsServer_ConnCount(g->wss.srv) > 0)) {
			char msg[128];
			int n;
			msg[0] = 0;
			Edit_TextUtf8(UiInput_Edit(&g->wss.form, g->wss.msg), msg,
				(int)sizeof(msg));
			if (msg[0] && (n = UiWsServer_Broadcast(g->wss.srv, msg,
					lstrlenA(msg), 1)) > 0) {
				char b[160];
				wsprintfA(b, "[srv] 已向 %d 个客户端广播", n);
				wss_log(g, b);
			}
		}
	}
	Ui_StackSkip(&col, 10.0f);
	logRc = Ui_StackTakeFill(&col);
	Gfx_FillRoundRect(gfx, logRc, 10.0f, t->card);
	Gfx_StrokeRoundRect(gfx, logRc, 10.0f, t->border, 1.0f);
	wsprintfA(buf, "服务端日志 · 连接 %d 次 · 消息 %d 条",
		g->wss.totalConns, g->wss.msgCount);
	small_txt(gfx, t, Ui_Rect(logRc.x + 12.0f, logRc.y + 4.0f,
		logRc.w - 24.0f, 20.0f), buf);
	{
		UiRect area = Ui_RectInset(logRc, 10.0f);
		area.y += 28.0f;
		area.h -= 30.0f;
		const char *p = g->wss.log;
		float y = area.y;
		while (p && *p && y + 16.0f <= area.y + area.h) {
			const char *nl = p;
			while (*nl && *nl != '\n') nl++;
			Gfx_Text(gfx, t->fMini, p, Ui_Rect(area.x, y, area.w, 15.0f),
				t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
			y += 16.0f;
			p = (*nl == '\n') ? nl + 1 : nl;
		}
	}
}

static void wss_props(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect ed, pv;
	UiStack col;

	snprintf(g->codeText, sizeof(g->codeText),
		"/* 回调全在 GUI 线程; Destroy 等清理完返回 */\n"
		"static const UiWsServerCallbacks cb = {\n"
		"    on_listening, on_open, on_message,\n"
		"    on_close, on_error };\n"
		"\n"
		"UiWsServerConfig cfg;   /* port=0 临时端口 */\n"
		"cfg.port = 18082;  cfg.path = \"/ws\";\n"
		"cfg.maxConnections = 16;\n"
		"srv = UiWsServer_Create(net, &cfg, &cb, user);\n"
		"\n"
		"UiWsServer_Broadcast(srv, msg, len, 1);\n"
		"UiWsServer_Send(conn, data, len, opcode);\n"
		"UiWsServer_CloseAll(srv, 1000, \"再见\");\n"
		"UiWsServer_Destroy(srv);\n");

	props_split(g, ui, rc, &ed, &pv);
	Ui_StackBeginPad(&col, pane_in(ed, 16.0f), UI_COLUMN, 0.0f,
		16.0f, 14.0f, 16.0f, 14.0f);

	lbl(gfx, t, Ui_StackTake(&col, 18.0f), "端口 / 路径 (编辑后重新启动生效)");
	Ui_StackSkip(&col, 4.0f);
	UiInput_BeginFrame(&g->wss.form);
	UiInput_SetRect(&g->wss.form, g->wss.port, Ui_StackTake(&col, 34.0f));
	UiInput_Draw(&g->wss.form, g->wss.port);
	Ui_StackSkip(&col, 6.0f);
	UiInput_SetRect(&g->wss.form, g->wss.path, Ui_StackTake(&col, 34.0f));
	UiInput_Draw(&g->wss.form, g->wss.path);
	Ui_StackSkip(&col, 10.0f);
	note_txt(gfx, t, Ui_StackTakeFill(&col),
		"wss (TLS) 需要 PFX 证书或证书存储 Subject;\n"
		"样例见 samples/ws_server.c (tests/gen_test_cert.ps1 生成测试证书)。");

	{
		UiRect area = pane_in(pv, 20.0f);
		small_txt(gfx, t, Ui_Rect(area.x, area.y, area.w, 20.0f),
			"预览 = 左侧字段 + 启动按钮 (同「演示」页)");
	}
}

static int wss_key(Gallery *g, UINT vk)
{
	return UiInput_Key(&g->wss.form, g->app->hwnd, vk);
}

static int wss_chr(Gallery *g, wchar_t ch)
{
	return UiInput_Char(&g->wss.form, ch);
}

static void wss_ime(Gallery *g)
{
	UiInput_OnIme(&g->wss.form);
}

static void wss_blink(Gallery *g)
{
	UiInput_Blink(&g->wss.form);
}

static void wss_mouse(Gallery *g, int msg, float x, float y)
{
	form_mouse(&g->wss.form, g->app, msg, x, y);
}

static void wss_shutdown(Gallery *g)
{
	wss_stop(g);
}

/* ── App 框架 (回调监视器) ──────────────────────────────────────────── */

static void appf_init(Gallery *g)
{
	g->appf.frame = 0;
	g->appf.tick = 0;
	g->appf.anim = 0;
	g->appf.mouse = 0;
	g->appf.key = 0;
	g->appf.chr = 0;
	g->appf.ime = 0;
	g->appf.timer = 0;
	g->appf.after = 0;
	g->appf.initDone = 1;
	g->appf.lastFrameMs = 0.0;
}

static void appf_row2(Gallery *g, Gfx *gfx, UiTheme *t, float x, float y,
	float w, const char *name, const char *when, uint64_t count)
{
	char buf[48];
	(void)g;
	wsprintfA(buf, "%I64u", count);
	Gfx_Text(gfx, t->fBody, name, Ui_Rect(x, y, w * 0.26f, 18.0f),
		t->text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(gfx, t->fMini, when, Ui_Rect(x + w * 0.28f, y, w * 0.46f, 18.0f),
		t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(gfx, t->fBody, buf, Ui_Rect(x + w * 0.76f, y, w * 0.24f, 18.0f),
		count ? t->primary.base : t->text4,
		GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
}

static void appf_demo(Gallery *g, UiRect rc)
{
	UiApp *app = g->app;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;
	UiStack col;
	UiRect tbl, codeRc2;
	int i;

	Ui_StackBeginPad(&col, rc, UI_COLUMN, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f);
	lbl(gfx, t, Ui_StackTake(&col, 18.0f),
		"壳回调监视器 (本页正在被 UiApp 壳驱动)");
	Ui_StackSkip(&col, 6.0f);
	tbl = Ui_StackTake(&col, 210.0f);
	Ui_StackSkip(&col, 10.0f);
	codeRc2 = Ui_StackTakeFill(&col);

	Gfx_FillRoundRect(gfx, tbl, 10.0f, t->card);
	Gfx_StrokeRoundRect(gfx, tbl, 10.0f, t->border, 1.0f);
	Gfx_Text(gfx, t->fLabel, "回调",
		Ui_Rect(tbl.x + 14.0f, tbl.y + 6.0f, tbl.w * 0.30f, 18.0f),
		t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fLabel, "触发时机",
		Ui_Rect(tbl.x + 14.0f + tbl.w * 0.30f, tbl.y + 6.0f, tbl.w * 0.46f, 18.0f),
		t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fLabel, "次数",
		Ui_Rect(tbl.x + 10.0f + tbl.w * 0.78f, tbl.y + 6.0f, tbl.w * 0.22f, 18.0f),
		t->text2, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);

	{
		static const char *const names[9] = {
			"init", "frame", "after_frame", "tick", "animating", "mouse",
			"key", "chr / ime", "timer"
		};
		static const char *const whens[9] = {
			"启动一次 (Gfx/Theme/IME 就绪)",
			"每帧绘制 (Begin/End 已处理)",
			"Gfx_End 后 (模态动作/overlay)",
			"动画帧 (壳已 AnimAdvance)",
			"询问是否有持续动画",
			"鼠标按下/移动/抬起/离开",
			"键盘 (含 Esc/方向键)",
			"字符 / 输入法合成",
			"光标闪烁定时器"
		};
		uint64_t counts[9];
		counts[0] = g->appf.initDone;
		counts[1] = g->appf.frame;
		counts[2] = g->appf.after;
		counts[3] = g->appf.tick;
		counts[4] = g->appf.anim;
		counts[5] = g->appf.mouse;
		counts[6] = g->appf.key;
		counts[7] = g->appf.chr + g->appf.ime;
		counts[8] = g->appf.timer;
		for (i = 0; i < 9; i++)
			appf_row2(g, gfx, t, tbl.x + 14.0f, tbl.y + 28.0f + i * 20.0f,
				tbl.w - 24.0f, names[i], whens[i], counts[i]);
	}

	code_view(g, codeRc2,
		"int WINAPI WinMain(HINSTANCE i, HINSTANCE p, LPSTR c, int s)\n"
		"{\n"
		"    UiAppCallbacks cb;\n"
		"    ZeroMemory(&cb, sizeof(cb));\n"
		"    cb.init = on_init;       /* 状态初始化 */\n"
		"    cb.frame = on_frame;     /* 每帧绘制 */\n"
		"    cb.after_frame = on_after_frame;   /* 模态动作 */\n"
		"    cb.tick = on_tick;       /* 动画节拍 */\n"
		"    cb.animating = on_animating;\n"
		"    cb.mouse = on_mouse;     /* + wheel/key/chr/ime/timer */\n"
		"    return UiApp_Run(L\"标题\", 1160, 780,\n"
		"        UI_THEME_LIGHT, &cb, &g_app);\n"
		"}\n"
		"\n"
		"/* 壳接管: HWND/Gfx/Theme/IME/帧时钟/消息循环/DPI */\n"
		"/* 业务动作放 after_frame (绘制中途不弹窗) */\n"
		);
}

/* ── Welcome ────────────────────────────────────────────────── */
/* ── Welcome ───────────────────────────────────────────────────────── */

static void welcome_demo(Gallery *g, UiRect rc)
{
	draw_md_doc(g, rc, 0);
}

/* ════════════════════════════════════════════════════════════════════
 * 页面表
 * ════════════════════════════════════════════════════════════════════ */

static const GalleryPage kPages[PG_COUNT] = {
	/* PG_WELCOME */
	{ "欢迎与导览", "CUI 组件画廊", "左侧选择组件 · 每页六个标签",
		NULL, welcome_demo, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL },
	/* PG_BUTTON */
	{ "Button 按钮", "Button 按钮", "类型 × 外观 × 形状 · 主题化 · 平滑动画",
		button_init, button_demo, button_props, button_key, button_chr,
		button_ime, button_blink, button_mouse, NULL, NULL, NULL },
	/* PG_BGROUP */
	{ "ButtonGroup", "ButtonGroup 按钮组", "融合分段 · 普通/单选/多选 · 键盘",
		bgroup_init, bgroup_demo, bgroup_props, bgroup_key, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL },
	/* PG_ICON */
	{ "Icon 图标", "Icon 图标", "Segoe MDL2 官方档案 1386 glyphs · 搜索 / 分区 / 复制码点",
		icon_init, icon_demo, icon_props, icon_key, icon_chr, icon_ime,
		icon_blink, icon_mouse, NULL, NULL, icon_anim },
	/* PG_INPUT */
	{ "Input 输入框", "Input 单行输入", "选区 / 剪贴板 / 中文输入法 / 密码",
		input_init, input_demo, input_props, input_key, input_chr,
		input_ime, input_blink, input_mouse, NULL, NULL, NULL },
	/* PG_TEXTAREA */
	{ "TextArea", "TextArea 多行文本", "动态容量 · 软换行 · 增量布局",
		ta_init, ta_demo, ta_props, ta_key, ta_chr, ta_ime, ta_blink,
		NULL, NULL, NULL, ta_anim },
	/* PG_CHECKBOX */
	{ "Checkbox", "Checkbox 复选框", "单项 / 组 mask / indeterminate",
		checks_init, checks_demo, checks_props, checks_key, checks_chr,
		checks_ime, checks_blink, checks_mouse, NULL, NULL, NULL },
	/* PG_RADIO */
	{ "RadioGroup", "RadioGroup 单选组", "互斥选项 · 禁用位图 · 键盘",
		radio_init, radio_demo, radio_props, radio_key, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL },
	/* PG_SWITCH */
	{ "Switch", "Switch 开关", "两态即时切换 · 焦点环",
		sw_init, sw_demo, sw_props, sw_key, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL },
	/* PG_SLIDER */
	{ "Slider", "Slider 滑块", "0..1 · 跟手拖动 · changed/committed",
		slider_init, slider_demo, slider_props, slider_key, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL },
	/* PG_SELECT */
	{ "Select 下拉", "Select 下拉选择", "单选 / 多选 / 可搜索 / 禁用项",
		sel_init, sel_demo, sel_props, sel_key, sel_chr, sel_ime,
		sel_blink, NULL, NULL, sel_popups, sel_anim },
	/* PG_DATEPICKER */
	{ "DatePicker", "DatePicker 日期选择", "四模式 · 键入校验 · 范围两阶段",
		dp_init, dp_demo, dp_props, dp_key, dp_chr, dp_ime, dp_blink,
		NULL, NULL, dp_popups, dp_anim },
	/* PG_TABS */
	{ "Tabs 标签页", "Tabs 标签页", "LINE / PILL / CARD · 四向转场 · 输入锁",
		tabs_init, tabs_demo, tabs_props, tabs_key, NULL, NULL, NULL,
		NULL, NULL, NULL, tabs_anim },
	/* PG_NAV */
	{ "Nav 导航", "Nav 导航菜单", "多级子菜单 · 溢出续页 · 路径高亮",
		nav_init, nav_demo, nav_props, nav_key, NULL, NULL, NULL, NULL,
		NULL, nav_popups, NULL },
	/* PG_PAGINATION */
	{ "Pagination", "Pagination 分页", "页码窗口 · 宽度退化 · 保持首条",
		pag_init, pag_demo, pag_props, pag_key, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL },
	/* PG_SCROLL */
	{ "ScrollView", "ScrollView 滚动", "虚拟列表 · 变高缓存 · 惯性/回弹",
		scroll_init, scroll_demo, scroll_props, NULL, NULL, NULL, NULL, NULL,
		scroll_tick, NULL, scroll_anim },
	/* PG_LAYOUT */
	{ "Layout 布局", "Layout 布局", "Stack 游标流 · 对齐 · 矩形数学",
		layout_init, layout_demo, layout_props, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL },
	/* PG_FLEX */
	{ "Flex 弹性布局", "Flex 弹性布局", "水平 / 垂直 · flex-lite 与 measure 协议",
		flex_init, flex_demo, flex_props, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL },
	/* PG_GRID */
	{ "Grid 网格布局", "Grid 网格布局", "fixed / auto / fr · span · dense",
		grid_init, grid_demo, grid_props, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL },
	/* PG_DIALOG */
	{ "Dialog 对话框", "Dialog 对话框", "窗内模态 · 焦点循环 · 危险确认",
		dlg_init, dlg_demo, dlg_props, dlg_key, dlg_chr, dlg_ime,
		dlg_blink, NULL, dlg_tick, dlg_popups, dlg_anim },
	/* PG_MENU */
	{ "Menu 菜单", "Menu 菜单", "下拉 / 右键 · 分隔 / 禁用 / 危险",
		menu_init, menu_demo, menu_props, menu_key, NULL, NULL, NULL,
		menu_mouse, NULL, menu_popups, NULL },
	/* PG_TOAST */
	{ "Toast 轻提示", "Toast 轻提示", "合成器 layer · 零重绘 · 自动图标",
		toast_init, toast_demo, toast_props, toast_key, toast_chr,
		toast_ime, toast_blink, toast_mouse, NULL, NULL, NULL },
	/* PG_TOOLTIP */
	{ "Tooltip 提示", "Tooltip 工具提示", "悬停延迟气泡 · 不挡命中",
		tip_init, tip_demo, tip_props, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, tip_anim },
	/* PG_PROGRESS */
	{ "Progress 进度", "Progress 进度", "静态条 + 自循环 overlay",
		prog_init, prog_demo, prog_props, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, prog_anim },
	/* PG_IMAGE */
	{ "Image 图片", "Image 图片", "URL / 文件 / 内存 · fit / 锚点",
		img_init, img_demo, img_props, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, img_anim },
	/* PG_MARKDOWN */
	{ "Markdown", "Markdown 渲染", "GFM / 容器 / 公式 · 懒测量",
		mdp_init, mdp_demo, mdp_props, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, mdp_anim },
	/* PG_THEME */
	{ "Theme 主题", "Theme 主题", "全局令牌 · 语义色 · 一键换肤",
		theme_init, theme_demo, theme_props, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL },
	/* PG_NET */
	{ "网络与定时器", "网络与定时器", "HTTP(S) / UiTimer · GUI 线程回调",
		net_init, net_demo, net_props, net_key, net_chr, net_ime,
		net_blink, net_mouse, NULL, NULL, NULL },
	/* PG_WS */
	{ "WS 客户端", "WebSocket 客户端", "UiWs · 自动重连 · 心跳 · 回显联调",
		wsc_init, wsc_demo, wsc_props, wsc_key, wsc_chr, wsc_ime,
		wsc_blink, wsc_mouse, NULL, NULL, NULL },
	/* PG_WSSRV */
	{ "WS 服务端", "WebSocket 服务端", "UiWsServer · 监听 / 广播 / 回显",
		wss_init, wss_demo, wss_props, wss_key, wss_chr, wss_ime,
		wss_blink, wss_mouse, NULL, NULL, NULL },
	/* PG_APP */
	{ "App 框架", "UiApp 应用框架", "回调监视器 · 壳接管一切",
		appf_init, appf_demo, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL },
};

/* ════════════════════════════════════════════════════════════════════
 * 全局回调
 * ════════════════════════════════════════════════════════════════════ */

static void on_init(UiApp *app, void *user)
{
	Gallery *g = (Gallery *)user;
	UiScrollConfig sideCfg = UiScrollConfig_Desktop();
	UiScrollbarStyle sideBar = UiScrollbarStyle_Default();
	int i;

	g->app = app;
	app->minW = 980.0f;
	app->minH = 620.0f;

	UiPopupManager_Init(&g->pm);
	UiToast_Init(&g->toast);
	UiToast_Attach(&g->toast, &app->ui);
	UiTooltip_Init(&g->tip, Ui_Id("gl.tip", 0, 0));
	UiTooltip_SetDelay(&g->tip, 450);

	{
		UiNetConfig cfg;
		ZeroMemory(&cfg, sizeof(cfg));
		g->net = UiNet_Create(&cfg);
	}

	g->md = UiMarkdown_Create(app->gfx, Ui_Id("gl.doc", 0, 0), g->net);
	if (g->md)
		UiMarkdown_SetText(g->md, "#\n");
	g->fCode = Gfx_CreateFontFamily(app->gfx, L"Consolas", 12.0f, 400);
	g->ev.count = 0;
	g->ev.total = 0;

	sideCfg.wheelStep = 44.0f;
	UiScrollView_Init(&g->sideSv, Ui_Id("gl.side.sv", 0, 0), &sideCfg,
		&sideBar);

	UiTabBar_Init(&g->subTabs, Ui_Id("gl.subtab", 0, 0), 6);
	g->pill = UiTabStyle_Pill();

	for (i = 0; i < PG_COUNT; i++)
		g->subTab[i] = 0;
	g->page = g_startPage;
	g->docKey = -1;
	lstrcpynA(g->status, "就绪: 左侧选择组件, 每页有 演示/属性/文档/API/最佳实践",
		(int)sizeof(g->status));

	for (i = 0; i < PG_COUNT; i++) {
		if (kPages[i].init && i != PG_WELCOME) kPages[i].init(g);
	}
}

static void on_frame(UiApp *app, void *user)
{
	Gallery *g = (Gallery *)user;
	UiTheme *t = &app->theme;
	Gfx *gfx = app->gfx;

	g->appf.frame++;
	/* 进度条 overlay 只在 Progress 页的「演示」子标签显示;
	 * 其它页面隐藏 (合成器 overlay 是独立层, 不随页面绘制消失)。 */
	UiProgress_SetHidden(&g->prog.prog,
		!(g->page == PG_PROGRESS && g->subTab[PG_PROGRESS] == 0));

	UiPopupManager_BeginFrame(&g->pm, &app->ui);

	Gfx_FillRoundRectGradV(gfx, Ui_Rect(0, 0, app->w, app->h),
		t->windowRadius, t->page, t->page2);
	Gfx_StrokeRoundRect(gfx, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);

	draw_topbar(g);
	draw_sidebar(g);
	draw_content(g);
	draw_status(g);

	/* Toast (含遮罩) */
	UiToast_Draw(&app->ui, &g->toast, UiFrame_Now(), app->w, app->h);
	if (UiToast_IsVisible(&g->toast) &&
		(app->ui.clicked == UiToast_BackdropId() ||
			app->ui.clicked == UiToast_CardId()))
		g->pendingToastHide = 1;

	/* 帧末弹层 */
	if (kPages[g->page].popups) kPages[g->page].popups(g);

	/* Tooltip 最后 (最上层) */
	UiTooltip_Draw(&g->tip, &app->ui);
}

static void on_after_frame(UiApp *app, void *user)
{
	Gallery *g = (Gallery *)user;

	g->appf.after++;
	if (g->pendingToastHide) {
		g->pendingToastHide = 0;
		UiToast_Hide(&g->toast, UiFrame_Now());
		UiApp_Invalidate(app);
	}
	UiToast_EndFrame(&g->toast, UiFrame_Now(), app->w, app->h);
	UiProgress_EndFrame(&g->prog.prog);
}

static int on_animating(UiApp *app, void *user)
{
	Gallery *g = (Gallery *)user;
	int busy;
	const GalleryPage *p;

	g->appf.anim++;
	busy = UiToast_IsVisible(&g->toast) || UiTooltip_Pending(&g->tip) ||
		UiMarkdown_Busy(g->md) || UiScrollView_Busy(&g->sideSv);
	p = &kPages[g->page];
	if (p->animating && p->animating(g)) busy = 1;
	return busy;
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	Gallery *g = (Gallery *)user;
	int redraw;

	g->appf.tick++;
	UiTabPager_Tick(&g->tabs.pager, &app->ui, (float)dtMs);
	/* 进度条只在自身页面跑: 离开页面时动画冻结 (overlay 已隐藏) */
	if (!UiProgress_Paused(&g->prog.prog) ||
		g->page == PG_PROGRESS)
		UiProgress_Tick(&g->prog.prog, dtMs);
	UiDialog_Tick(&g->dlg.dlg, &app->ui, dtMs);
	UiDialog_Tick(&g->dlg.confirm, &app->ui, dtMs);
	redraw = UiToast_Tick(&g->toast, UiFrame_Now());
	if (!UiToast_UsingLayer(&g->toast) &&
		(UiToast_IsVisible(&g->toast) || redraw))
		UiApp_Invalidate(app);
	if (kPages[g->page].tick) kPages[g->page].tick(g, dtMs);
}

static void on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
	Gallery *g = (Gallery *)user;
	const GalleryPage *p = &kPages[g->page];
	g->appf.mouse++;
	if (p->mouse) p->mouse(g, msg, x, y);
}

static void on_wheel(UiApp *app, float x, float y, float delta, void *user)
{
	(void)app; (void)user;
	Ui_OnMouseWheel(&app->ui, x, y, delta);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	Gallery *g = (Gallery *)user;
	UiContext *ui = &app->ui;
	const GalleryPage *p;

	g->appf.key++;
	if (UiToast_IsVisible(&g->toast) && vk == VK_ESCAPE) {
		UiToast_Hide(&g->toast, UiFrame_Now());
		UiApp_Invalidate(app);
		return 1;
	}
	if (UiPopupManager_Key(&g->pm, ui, vk)) {
		UiApp_Invalidate(app);
		return 1;
	}
	p = &kPages[g->page];
	if (p->key && p->key(g, vk)) {
		UiApp_Invalidate(app);
		return 1;
	}
	if (UiPopupManager_AnyOpen(&g->pm)) {
		if (vk == VK_TAB) {
			UiPopupManager_Close(&g->pm, ui);
			UiApp_Invalidate(app);
		}
		return 1;
	}
	return 0;
}

static int on_chr(UiApp *app, wchar_t ch, void *user)
{
	Gallery *g = (Gallery *)user;
	const GalleryPage *p = &kPages[g->page];
	g->appf.chr++;
	if (UiToast_IsVisible(&g->toast)) return 1;
	if (p->chr && p->chr(g, ch)) return 1;
	return 0;
}

static void on_ime(UiApp *app, void *user)
{
	Gallery *g = (Gallery *)user;
	const GalleryPage *p = &kPages[g->page];
	g->appf.ime++;
	if (p->ime) p->ime(g);
}

static void on_timer(UiApp *app, void *user)
{
	Gallery *g = (Gallery *)user;
	const GalleryPage *p = &kPages[g->page];
	g->appf.timer++;
	if (UiToast_IsVisible(&g->toast)) return;
	if (p->blink) p->blink(g);
}

static void on_shutdown(UiApp *app, void *user)
{
	Gallery *g = (Gallery *)user;
	int i;
	(void)app;

	for (i = PG_COUNT - 1; i >= 0; i--) {
		if (i == PG_TEXTAREA) ta_shutdown(g);
		else if (i == PG_SELECT) sel_shutdown(g);
		else if (i == PG_DATEPICKER) dp_shutdown(g);
		else if (i == PG_NAV) nav_shutdown(g);
		else if (i == PG_DIALOG) dlg_shutdown(g);
		else if (i == PG_IMAGE) img_shutdown(g);
		else if (i == PG_MARKDOWN) mdp_shutdown(g);
	}
	net_shutdown(g);
	wsc_shutdown(g);
	wss_shutdown(g);
	if (g->md) {
		UiMarkdown_Destroy(g->md);
		g->md = NULL;
	}
	UiToast_Shutdown(&g->toast);
}

static int on_nchittest(UiApp *app, int x, int y, LRESULT *hit, void *user)
{
	(void)user;
	if (y >= 0 && y < (int)(TOP_H * app->scale) &&
		x >= 0 && x < (int)(app->w * app->scale - 130.0f * app->scale)) {
		*hit = HTCAPTION;
		return 1;
	}
	return 0;
}

static int page_from_cmd(const char *name)
{
	static const struct { const char *key; int pg; } kMap[] = {
		{ "welcome", PG_WELCOME }, { "button", PG_BUTTON },
		{ "bgroup", PG_BGROUP }, { "icon", PG_ICON },
		{ "input", PG_INPUT }, { "textarea", PG_TEXTAREA },
		{ "checkbox", PG_CHECKBOX }, { "radio", PG_RADIO },
		{ "switch", PG_SWITCH }, { "slider", PG_SLIDER },
		{ "select", PG_SELECT }, { "datepicker", PG_DATEPICKER },
		{ "tabs", PG_TABS }, { "nav", PG_NAV },
		{ "pagination", PG_PAGINATION }, { "scroll", PG_SCROLL },
		{ "layout", PG_LAYOUT }, { "flex", PG_FLEX },
		{ "grid", PG_GRID }, { "dialog", PG_DIALOG },
		{ "menu", PG_MENU }, { "toast", PG_TOAST },
		{ "tooltip", PG_TOOLTIP }, { "progress", PG_PROGRESS },
		{ "image", PG_IMAGE }, { "markdown", PG_MARKDOWN },
		{ "theme", PG_THEME }, { "net", PG_NET },
		{ "ws", PG_WS }, { "wssrv", PG_WSSRV },
		{ "app", PG_APP }
	};
	int i;
	for (i = 0; i < (int)(sizeof(kMap) / sizeof(kMap[0])); i++)
		if (lstrcmpiA(kMap[i].key, name) == 0) return kMap[i].pg;
	return -1;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)show;
	if (cmd && cmd[0]) {
		/* startup 传的是完整命令行: 先跳过 exe 路径 token */
		const char *p = cmd;
		if (*p == '"') {
			p++;
			while (*p && *p != '"') p++;
			if (*p == '"') p++;
		} else {
			while (*p && *p != ' ' && *p != '\t') p++;
		}
		while (*p == ' ' || *p == '\t') p++;
		if (*p && *p != '"') {
			char tok[32];
			int i = 0;
			while (p[i] && p[i] != ' ' && p[i] != '\t' && i < 30) {
				tok[i] = p[i];
				i++;
			}
			tok[i] = 0;
			g_startPage = page_from_cmd(tok);
			/* 第二个 token: autostart (wssrv 页自动监听, 联调用) */
			while (p[i] == ' ' || p[i] == '\t') i++;
			{
				char tok2[32];
				int j = 0;
				while (p[i + j] && p[i + j] != ' ' && p[i + j] != '\t' &&
					j < 30) {
					tok2[j] = p[i + j];
					j++;
				}
				tok2[j] = 0;
				if (tok2[0] && lstrcmpiA(tok2, "autostart") == 0)
					g_wsAuto = 1;
			}
		}
	}
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.after_frame = on_after_frame;
	cb.shutdown = on_shutdown;
	cb.animating = on_animating;
	cb.tick = on_tick;
	cb.mouse = on_mouse;
	cb.wheel = on_wheel;
	cb.key = on_key;
	cb.chr = on_chr;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.nchittest = on_nchittest;
	return UiApp_Run(L"CUI 组件画廊", 1160, 780, UI_THEME_LIGHT, &cb, &g_gl);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
