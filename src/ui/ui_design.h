/*
 * ui_design.h - CUI 可视化设计器运行时
 *
 * .cui 是唯一设计工程文件 (UTF-8 JSON). 模型分两层:
 *   View = 可复用根容器树; Host = Window/Dialog/Panel 外壳.
 * 生成器按"生成组"输出 C/H, 宿主引用同组视图.
 */
#ifndef CUI_UI_DESIGN_H
#define CUI_UI_DESIGN_H

#include "ui/ui_color_select.h"
#include "ui/ui_context.h"
#include "ui/ui_controls.h"
#include "ui/ui_datepicker.h"
#include "ui/ui_dialog.h"
#include "ui/ui_ime.h"
#include "ui/ui_input.h"
#include "ui/ui_popup.h"
#include "ui/ui_qrcode.h"
#include "ui/ui_ring_progress.h"
#include "ui/ui_select.h"
#include "ui/ui_tab.h"
#include "ui/ui_textarea.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_DN_ID_MAX    64
#define UI_DN_NAME_MAX  64
#define UI_DN_TEXT_MAX  1024
#define UI_DN_PATH_MAX  260
#define UI_DN_SYM_MAX   64
#define UI_DN_GRID_MAX_COLS    16
#define UI_DN_GRID_TRACKS_MAX  128
#define UI_DN_TABS_MAX_PAGES   32
#define UI_DN_OPTIONS_MAX      8
#define UI_DN_OPTION_TEXT_MAX  48

typedef enum {
	UI_DN_ABSOLUTE = 0,
	UI_DN_COLUMN,
	UI_DN_ROW,
	UI_DN_GRID,
	UI_DN_PANEL,
	UI_DN_LABEL,
	UI_DN_BUTTON,
	UI_DN_INPUT,
	UI_DN_CHECKBOX,
	UI_DN_SWITCH,
	UI_DN_SLIDER,
	UI_DN_PROGRESS,
	UI_DN_SELECT,
	UI_DN_IMAGE,
	UI_DN_SEPARATOR,
	UI_DN_TABS,        /* 容器: 子节点即页, 页签文字取子节点名称 */
	UI_DN_TEXTAREA,
	UI_DN_DATEPICKER,
	UI_DN_COLORSEL,
	UI_DN_QRCODE,
	UI_DN_RINGPROGRESS,
	UI_DN_RADIOGROUP,
	UI_DN_COUNT
} UiDesignType;

enum {
	UI_DN_SIZE_FIXED = 0,
	UI_DN_SIZE_AUTO,
	UI_DN_SIZE_FILL,
	UI_DN_SIZE_GROW
};

enum {
	UI_DN_ALIGN_START = 0,
	UI_DN_ALIGN_CENTER,
	UI_DN_ALIGN_END
};

enum {
	UI_DN_VALIGN_TOP = 0,
	UI_DN_VALIGN_CENTER,   /* 默认: 与旧文档一致 */
	UI_DN_VALIGN_BOTTOM
};

enum {
	UI_DN_ANCHOR_LEFT = 1u << 0,
	UI_DN_ANCHOR_TOP = 1u << 1,
	UI_DN_ANCHOR_RIGHT = 1u << 2,
	UI_DN_ANCHOR_BOTTOM = 1u << 3
};

typedef struct UiDesignNode {
	int type;
	int owner;          /* window index */
	int parent;         /* -1 = 根 */
	int *children;
	int childCount, childCap;
	int visible, locked;

	char id[UI_DN_ID_MAX];
	char name[UI_DN_NAME_MAX];

	/* 通用尺寸/布局 */
	int wMode, hMode;
	float w, h, minW, minH, maxW, maxH;
	float grow;
	int align;          /* 布局交叉轴对齐: UI_DN_ALIGN_* */
	int textAlign;      /* 单行文本水平对齐: UI_DN_ALIGN_* */
	int textVAlign;     /* 单行文本垂直对齐: UI_DN_VALIGN_* */
	float margin[4];    /* 左/上/右/下 */
	float padding[4];
	float gap;
	float radius;       /* Panel 圆角, DIP */

	/* Absolute */
	float x, y;
	unsigned anchors;

	/* Grid */
	int gridRows, gridCols;
	float gridRowGap, gridColGap;
	int gridRow, gridCol, rowSpan, colSpan;
	/* 列宽轨道: "120,1fr,auto" (px / fr 权重 / auto=列内子项设计宽);
	 * 空 = 均分. 列数由 gridCols 决定 (0 = 取轨道数). */
	char gridColTracks[UI_DN_GRID_TRACKS_MAX];

	/* 数据 */
	char text[UI_DN_TEXT_MAX];
	char hint[128];
	char options[UI_DN_TEXT_MAX];
	float value;
	int checked, enabled, selected, variant;
	int mode;           /* DatePicker: UiDatePickerMode */
	int readonly;       /* TextArea */
	unsigned color;     /* ColorSelect / QR 前景色: 0xAARRGGBB */
	unsigned bgColor;   /* QR 背景色: 0xAARRGGBB */
	int ecc;            /* QR 纠错等级: UiQrEcc */
	char logoSrc[UI_DN_PATH_MAX]; /* QR 中央 Logo 图片路径 (UTF-8) */
	float logoRatio;    /* QR Logo 边长 / 码区边长 */
	float qrPad;        /* QR 绘制区域到组件四边留白 (模块) */
	float ringMax;      /* RingProgress 数值范围上限 */
	float ringWidth;    /* RingProgress 环宽 (DIP) */
	char onClick[UI_DN_ID_MAX];
	char onChange[UI_DN_ID_MAX];
} UiDesignNode;

typedef enum {
	UI_DH_WINDOW = 0,   /* 独立窗口 */
	UI_DH_DIALOG,       /* 虚拟 Dialog 覆盖层 */
	UI_DH_PANEL         /* 供应用嵌入的内容块 */
} UiDesignHostKind;

/* 视图: 可复用的根容器树, 不含宿主外壳. */
typedef struct UiDesignView {
	char id[UI_DN_ID_MAX];
	char name[UI_DN_NAME_MAX];
	float w, h;         /* 设计尺寸 (宿主可覆盖) */
	int group;          /* group index, -1 = 不生成 */
	int root;           /* node index, -1 = 无 */
} UiDesignView;

/* 宿主: 窗口/Dialog/Panel 外壳, 挂载一个视图. */
typedef struct UiDesignHost {
	char id[UI_DN_ID_MAX];
	char name[UI_DN_NAME_MAX];
	char title[128];
	int kind;           /* UiDesignHostKind */
	int group;          /* group index, -1 = 不生成 */
	float w, h, minW, minH;
	int theme;          /* UiThemeId */
	int view;           /* view index, -1 = 无内容 */
} UiDesignHost;

typedef struct UiDesignGroup {
	char id[UI_DN_ID_MAX];
	char name[UI_DN_NAME_MAX];
	char outputC[UI_DN_PATH_MAX];
	char outputH[UI_DN_PATH_MAX];
	char symbolPrefix[UI_DN_SYM_MAX];
} UiDesignGroup;

typedef struct UiDesignDoc {
	UiDesignGroup *groups;
	int groupCount, groupCap;
	UiDesignView *views;
	int viewCount, viewCap;
	UiDesignHost *hosts;
	int hostCount, hostCap;
	UiDesignNode *nodes;
	int nodeCount, nodeCap;
	char name[UI_DN_NAME_MAX];
	char path[UI_DN_PATH_MAX];   /* 最近打开/保存的 .cui, UTF-8 */
	int dirty;
} UiDesignDoc;

/* ── 模型 ───────────────────────────────────────────────────────────── */

void UiDesign_Init(UiDesignDoc *doc);
void UiDesign_Free(UiDesignDoc *doc);
/* 把旧的 AUTO/GROW 尺寸模式归一化为 FIXED/FILL:
 * AUTO 宽高为 0 时取类型默认值, GROW 视作 FILL. */
void UiDesign_NormalizeSizeModes(UiDesignDoc *doc);

int UiDesign_AddGroup(UiDesignDoc *doc, const char *id, const char *name);
int UiDesign_AddView(UiDesignDoc *doc, const char *id, const char *name);
int UiDesign_AddHost(UiDesignDoc *doc, const char *id, const char *name,
	int kind, int group);
/* 删除视图及其全部节点; 引用它的宿主 view 置 -1. */
int UiDesign_ViewRemove(UiDesignDoc *doc, int view);
int UiDesign_AddNode(UiDesignDoc *doc, int view, int type, int parent,
	const char *id, const char *name);
UiDesignNode *UiDesign_Node(UiDesignDoc *doc, int index);
UiDesignView *UiDesign_View(UiDesignDoc *doc, int index);
UiDesignHost *UiDesign_Host(UiDesignDoc *doc, int index);
UiDesignGroup *UiDesign_Group(UiDesignDoc *doc, int index);

void UiDesign_NodeAddChild(UiDesignDoc *doc, int parent, int child);
void UiDesign_NodeDetach(UiDesignDoc *doc, int index);
int  UiDesign_NodeRemove(UiDesignDoc *doc, int index);
int  UiDesign_NodeMove(UiDesignDoc *doc, int index, int newParent, int pos);
int  UiDesign_NodeDuplicate(UiDesignDoc *doc, int index);
int  UiDesign_NodeMoveUp(UiDesignDoc *doc, int index);
int  UiDesign_NodeMoveDown(UiDesignDoc *doc, int index);

/* Tabs 页面: 直接子节点即页面. selected 始终是有效页索引或 -1. */
int  UiDesign_TabsPageCount(const UiDesignDoc *doc, int tabs);
int  UiDesign_TabsPageAt(const UiDesignDoc *doc, int tabs, int page);
int  UiDesign_TabsSelectedPage(const UiDesignDoc *doc, int tabs);
int  UiDesign_TabsSetSelectedPage(UiDesignDoc *doc, int tabs, int page);
int  UiDesign_TabsInsertPage(UiDesignDoc *doc, int tabs, int pos,
	const char *id, const char *name);
int  UiDesign_TabsRemovePage(UiDesignDoc *doc, int tabs, int page);
int  UiDesign_TabsMovePage(UiDesignDoc *doc, int tabs, int from, int to);
void UiDesign_TabsNormalize(UiDesignDoc *doc, int tabs);

int  UiDesign_FindNode(const UiDesignDoc *doc, int view, const char *id);
int  UiDesign_FindView(const UiDesignDoc *doc, const char *id);
int  UiDesign_FindHost(const UiDesignDoc *doc, const char *id);
int  UiDesign_FindGroup(const UiDesignDoc *doc, const char *id);

void UiDesign_SetText(char *dst, int cap, const char *src);
void UiDesign_NodeSetText(UiDesignNode *n, const char *text);
void UiDesign_NodeSetHint(UiDesignNode *n, const char *hint);
void UiDesign_NodeSetOptions(UiDesignNode *n, const char *options);

/* 新建默认工程 / 默认节点尺寸 */
int  UiDesign_DefaultDoc(UiDesignDoc *doc);
const char *UiDesign_TypeName(int type);
float UiDesign_DefaultW(int type);
float UiDesign_DefaultH(int type);
int  UiDesign_IsContainer(int type);
int  UiDesign_TypeFromName(const char *name);

/* ── JSON / 文件 ────────────────────────────────────────────────────── */

char *UiDesign_ToJson(const UiDesignDoc *doc); /* HeapAlloc; caller frees */
int  UiDesign_FromJson(UiDesignDoc *doc, const char *text);
int  UiDesign_LoadFile(UiDesignDoc *doc, const wchar_t *path);
int  UiDesign_SaveFile(UiDesignDoc *doc, const wchar_t *path);

/* ── 布局 ───────────────────────────────────────────────────────────── */

/* rects 长度 >= doc->nodeCount; 返回 1. */
int UiDesign_Layout(const UiDesignDoc *doc, int view, UiRect client,
	UiRect *rects, int rectCap);
int UiDesign_FindNodeAt(const UiDesignDoc *doc, int view,
	const UiRect *rects, float x, float y);

/* 解析/求解 Grid 列轨道并输出每列绝对位置.
 * xs/ws 长度 >= UI_DN_GRID_MAX_COLS; 返回有效列数 (>=1).
 * 规格空或无效时各列 1fr 均分; AUTO 取该列子项的设计宽度. */
int UiDesign_GridColumns(const UiDesignDoc *doc, const UiDesignNode *n,
	UiRect inner, float *xs, float *ws, int cap);

/* ── 运行时 ─────────────────────────────────────────────────────────── */

typedef struct UiDesignNodeState {
	int inited;
	UiInput input;
	UiCheckbox checkbox;
	UiSwitch sw;
	UiSlider slider;
	UiSelect select;
	UiRadioGroup radio;
	UiTextArea ta;
	UiDatePicker dp;
	UiColorSelect cs;
	UiQrCode qr;
	UiRingProgress ring;
	UiTabBar bar;
	int barCount;
	wchar_t taHint[128];   /* TextArea placeholder 由控件保存指针, 需自持 */
	char text[UI_DN_TEXT_MAX];
	char optItems[UI_DN_OPTIONS_MAX][UI_DN_OPTION_TEXT_MAX];
	const char *optPtrs[UI_DN_OPTIONS_MAX];
	int optCount;
	char optCache[UI_DN_TEXT_MAX];
} UiDesignNodeState;

typedef struct UiDesignInstance {
	UiDesignDoc *doc;
	int host;           /* host index */
	int view;           /* 挂载视图, -1 = 空 */
	UiContext *ui;
	UiPopupManager *pm;
	UiIme *ime;
	UiRect *rects;
	UiDesignNodeState *states;
	int stateCount;
	UiDialog dialog;
	int dialogReady;
	int lastEventNode;
	int lastEventKind;   /* 1=click 2=change */
	void (*eventFn)(void *user, struct UiDesignInstance *inst,
		const char *nodeId, const char *action, int kind);
	void *eventUser;
} UiDesignInstance;

int  UiDesignInstance_Init(UiDesignInstance *inst, UiDesignDoc *doc,
	int host, UiContext *ui, UiPopupManager *pm, UiIme *ime);
void UiDesignInstance_Shutdown(UiDesignInstance *inst);
void UiDesignInstance_SetEventFn(UiDesignInstance *inst,
	void (*fn)(void *, UiDesignInstance *, const char *, const char *, int),
	void *user);
const char *UiDesign_GetText(UiDesignInstance *inst, const char *nodeId);
void UiDesign_SetTextValue(UiDesignInstance *inst, const char *nodeId,
	const char *text);
void UiDesign_Draw(UiDesignInstance *inst, UiRect rc);
void UiDesign_DrawOverlay(UiDesignInstance *inst);
/* Gfx_End 之后调用: 同步 ColorSelect 的合成器交互层. 使用颜色控件的
 * 宿主必须每帧调用 (设计器/生成代码的宿主跳过后颜色条不刷新). */
void UiDesign_EndFrame(UiDesignInstance *inst);
int  UiDesign_DialogIsOpen(const UiDesignInstance *inst);
int  UiDesign_DialogOpen(UiDesignInstance *inst, UiId owner);
void UiDesign_DialogDraw(UiDesignInstance *inst);
void UiDesign_MouseDown(UiDesignInstance *inst, float x, float y);
void UiDesign_MouseMove(UiDesignInstance *inst, float x, float y);
void UiDesign_MouseUp(UiDesignInstance *inst);
int  UiDesign_Key(UiDesignInstance *inst, UINT vk);
int  UiDesign_Char(UiDesignInstance *inst, wchar_t ch);
void UiDesign_OnIme(UiDesignInstance *inst);
void UiDesign_Blink(UiDesignInstance *inst);
int  UiDesign_Busy(const UiDesignInstance *inst);

/* ── 代码生成 ───────────────────────────────────────────────────────── */

/* 生成一个组; dir 是 .cui 所在目录. 返回写入文件数, -1 = 失败.
 * report 返回错误/摘要文本. */
int UiDesign_GenerateGroup(const UiDesignDoc *doc, int group,
	const wchar_t *dir, char *report, int cap);
/* 单个宿主导出: 输出 <宿主ID>.ui.c/.h, 与生成组互不影响. */
int UiDesign_GenerateHost(const UiDesignDoc *doc, int host,
	const wchar_t *dir, char *report, int cap);
int UiDesign_GenerateAll(const UiDesignDoc *doc, const wchar_t *dir,
	char *report, int cap);

/* 生成预览: 不写文件, 报告每个输出文件的状态.
 * status: 0 = 新增, 1 = 内容有变化, 2 = 无变化. 返回文件总数, -1 = 失败. */
typedef struct UiDesignGenFile {
	char name[UI_DN_PATH_MAX];
	int status;
	int oldLines;
	int newLines;
	int added;
	int removed;
} UiDesignGenFile;

int UiDesign_PreviewAll(const UiDesignDoc *doc, const wchar_t *dir,
	UiDesignGenFile *files, int maxFiles, char *report, int cap);

/* 由组/宿主 id 生成合法 C 标识/枚举名. */
const char *UiDesign_Symbol(const char *id, char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_DESIGN_H */
