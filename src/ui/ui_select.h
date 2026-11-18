/*
 * ui_select.h - 单选/多选下拉 + 可搜索过滤 (触发器 + 延迟绘制的弹层列表)
 *
 * 与 UiPopupManager 配套:
 *   Ui_Select      每帧画触发器, 点击时打开弹层
 *   UiSelect_DrawPopup  帧末画搜索框/列表 (overlay 层)
 *   UiSelect_Key   键盘导航/编辑; 字符与 IME 由应用转发 Char/OnIme
 *
 * 列表滚动区复用 UiScrollView (滚轮/thumb 拖动/轨道翻页/预留槽).
 * 多选上限 32 项 (unsigned mask).
 */
#ifndef CUI_UI_SELECT_H
#define CUI_UI_SELECT_H

#include "edit.h"
#include "ui/ui_context.h"
#include "ui/ui_popup.h"
#include "ui/ui_scroll.h"
#include "ui/ui_ime.h"

typedef struct UiSelect {
	UiId id;
	const char *const *items;
	int count;
	int selected;          /* 单选: 选中项; 多选: 最近一次切换项; -1 = 无 */
	int focus;             /* 打开时高亮项 */
	int multiple;          /* 1 = 多选 */
	unsigned mask;         /* 多选位图 (最多 32 项) */
	int filterable;        /* 1 = 弹层顶部带搜索框 */
	unsigned disabledMask;
	float rowH;            /* 默认 32 */
	float maxListH;        /* 默认 280 */
	float listW;           /* 0 = 按内容/触发器自适应 */
	UiScrollView sv;       /* 列表滚动/拖动/滚动条复用 ScrollView */
	UiRect anchor;         /* 最近一次触发器矩形 */
	UiRect listRc;         /* 本帧列表矩形 (含搜索框) */
	int ensureFocus;       /* 下一帧把 focus 滚入可视区 */
	int changed;           /* 本帧选择发生变化 */
	int committed;         /* 单选提交 / 多选关闭时置 1 */
	float arrowAngle;      /* 0 = 向下, 1 = 向上; 180ms 补间 */
	float arrowFrom;
	double arrowT0;
	int arrowTarget;
	int arrowAnim;
	/* 合成器路径: chevron 是独立 visual (PartFx), 旋转由合成器补间 */
	void *arrowPart;
	unsigned arrowColor;
	int arrowPartTarget;
	int panelFx;           /* 打开首帧: 登记面板淡入矩形 */
	double lastTickNow;    /* 列表平滑滚动用真实帧间隔 */
	int visCount;          /* 当前过滤后的可见项数 */
	int searchActive;      /* 搜索框是否已接管焦点/IME */
	int blinkOn;
	int wasOpen;
	char placeholder[96];  /* 持久 placeholder (Ui_Select 参数优先) */
	UiEdit edit;           /* 搜索框 (UTF-16) */
	char filter[128];      /* 搜索词 UTF-8 (空 = 不过滤) */
	const wchar_t *searchHint; /* 搜索框提示, NULL = L"搜索…" */
	UiIme *ime;            /* 由应用 Attach; 打开时接管焦点 */
} UiSelect;

void UiSelect_Init(UiSelect *s, UiId id);
void UiSelect_Shutdown(UiSelect *s);
void UiSelect_SetItems(UiSelect *s, const char *const *items, int count);
void UiSelect_SetSelected(UiSelect *s, int index);
void UiSelect_SetDisabledMask(UiSelect *s, unsigned mask);
void UiSelect_SetMultiple(UiSelect *s, int on);
void UiSelect_SetMask(UiSelect *s, unsigned mask);
unsigned UiSelect_GetMask(const UiSelect *s);
void UiSelect_SetFilterable(UiSelect *s, int on);
/* 持久 placeholder; Ui_Select 的 placeholder 参数非空时优先用它. */
void UiSelect_SetPlaceholder(UiSelect *s, const char *text);
/* 搜索框提示 (UTF-16); NULL 恢复默认 L"搜索…". */
void UiSelect_SetSearchHint(UiSelect *s, const wchar_t *hint);

/* 可搜索 Select 必须 Attach Ime, 并转发 WM_CHAR / WM_IME 与 530ms 光标闪烁. */
void UiSelect_AttachIme(UiSelect *s, UiIme *ime);
int  UiSelect_SearchActive(const UiSelect *s);
int  UiSelect_Char(UiSelect *s, UiContext *ui, wchar_t ch);
void UiSelect_OnIme(UiSelect *s, UiContext *ui);
void UiSelect_Blink(UiSelect *s, UiContext *ui);

/* 画触发器; 返回 1 = 本帧选中项发生变化. */
int  Ui_Select(UiSelect *s, UiPopupManager *pm, UiContext *ui, UiRect rc,
	const char *placeholder, int enabled);

/* 帧末调用; 未打开时什么都不做. */
void UiSelect_DrawPopup(UiSelect *s, UiPopupManager *pm, UiContext *ui);

/* 列表滚轮平滑时间常数 (ms): 0 = 即时; 默认 100. 键盘 focus 滚入视口
 * 仍为即时. */
void UiSelect_SetScrollSmoothing(UiSelect *s, float tauMs);
/* 列表滚动/滚动条淡入动画进行中: app 可用于 animating 持续出帧. */
int  UiSelect_Busy(const UiSelect *s);

/* 键盘: 方向键导航; 单选 Enter 提交; 多选 Enter 切换 (无搜索时 Space 也切换);
 * 可搜索时 Backspace/Delete/左右/Home/End 编辑搜索词, Space 输入空格,
 * 字符由 Char 转发, IME 组合由 OnIme 转发. */
int  UiSelect_Key(UiSelect *s, UiPopupManager *pm, UiContext *ui, UINT vk);

#endif /* CUI_UI_SELECT_H */
