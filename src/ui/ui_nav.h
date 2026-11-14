/*
 * ui_nav.h - 多级导航菜单 (导航栏 + 子菜单弹层)
 *
 * 数据是应用持有的 UiNavItem 树; UiNavBar 只保存稳定 UiId 路径、焦点、
 * 打开层级和溢出状态。水平模式尾部溢出收进 ">>"; 垂直模式支持图标 rail。
 *
 * 每帧:
 *   UiPopupManager_BeginFrame
 *   UiNavBar_Draw(...)
 *   ...其它控件...
 *   UiNavBar_DrawPopups(...)      (帧末, overlay)
 *
 * 多级子菜单属于一个 popup session (openId = nav->id), 不是多个并列弹层。
 */
#ifndef CUI_UI_NAV_H
#define CUI_UI_NAV_H

#include "ui/ui_context.h"
#include "ui/ui_icon.h"
#include "ui/ui_popup.h"

/* 弹层不滚动: 超出高度时末尾行变成 "更多 »" 再开一层尾部面板,
 * 因此面板层数可能多于逻辑层级. */
#define UI_NAV_MAX_DEPTH 8

enum {
	UI_NAV_HORIZONTAL = 0,
	UI_NAV_VERTICAL = 1
};

enum {
	UI_NAV_DISABLED   = 1u << 0,
	UI_NAV_DIVIDER    = 1u << 1,
	UI_NAV_DANGER     = 1u << 2,
	/* 有子项的父项也允许单独选中; 默认父项只展开子菜单. */
	UI_NAV_SELECTABLE = 1u << 3
};

typedef struct UiNavItem {
	UiId id;                    /* 稳定标识, 全树唯一 */
	const char *label;
	const char *shortcut;       /* 子菜单右侧提示, 可为 NULL */
	UiIcon icon;                /* icon.kind=0 表示无图标 */
	const struct UiNavItem *children;
	int childCount;
	unsigned flags;
} UiNavItem;

typedef struct UiNavStyle {
	float barHeight;      /* 0 -> 42 */
	float itemH;          /* 0 -> 38 */
	float padX;           /* 0 -> 14 */
	float gap;            /* 0 -> 2 */
	float radius;         /* 0 -> 8 */
	float iconSize;       /* 0 -> 18 */
	float iconGap;        /* 0 -> 8 */
	float railWidth;      /* 0 -> 56 */
	float panelMinW;      /* 0 -> 180 */
	float panelMaxW;      /* 0 -> 320 */
	float panelRowH;      /* 0 -> 32 */
	float panelMaxH;      /* 0 -> 360 */
	float panelPadX;      /* 0 -> 10 */
	float panelPadY;      /* 0 -> 5 */

	/* 0 = 主题默认 */
	UiColor barBg;
	UiColor barBorder;
	UiColor itemHover;
	UiColor itemSelected;
	UiColor text;
	UiColor textHover;
	UiColor textSelected;
	UiColor indicator;
	UiColor panelBg;
	UiColor panelBorder;
} UiNavStyle;

UiNavStyle UiNavStyle_Default(void);

typedef struct UiNavBar {
	UiId id;
	int layout;           /* UI_NAV_HORIZONTAL / UI_NAV_VERTICAL */
	int rail;             /* 垂直只显示图标 */
	const UiNavItem *items;
	int count;
	UiNavStyle style;

	/* 选中路径: requested 是应用设置的原始值, selected 是解析到树后的最长前缀. */
	UiId requested[UI_NAV_MAX_DEPTH];
	int requestedDepth;
	UiId selected[UI_NAV_MAX_DEPTH];
	int selectedDepth;

	/* 打开的面板: 面板 L 显示 openPath[L].children (溢出页例外).
	 * openTail[L]=1 表示该层是上一层隐藏尾部续页, openPath[L] 无意义. */
	UiId openPath[UI_NAV_MAX_DEPTH];
	int openTail[UI_NAV_MAX_DEPTH];
	int tailOffset[UI_NAV_MAX_DEPTH];
	int openDepth;
	int openOverflow;
	/* 本帧每层实际绘制的行范围与 "更多" 状态 (绘制/键盘共用). */
	int panelStart[UI_NAV_MAX_DEPTH];
	int panelVisible[UI_NAV_MAX_DEPTH];
	int panelMore[UI_NAV_MAX_DEPTH];

	/* 键盘焦点: 关闭时 keyRoot; 打开时 keyItem[level] 是各层面板焦点行. */
	UiId keyRoot;
	UiId keyItem[UI_NAV_MAX_DEPTH];
	int keyDepth;
	int keyFocusVisible;

	UiId hoverId;
	int hoverLevel;

	UiId activated[UI_NAV_MAX_DEPTH];
	int activatedDepth;
	int changed;

	UiRect rc;
	UiRect moreRc;
	UiId moreId;
	UiRect rootAnchor;         /* 当前打开顶级项的锚点 */
	UiRect panelRc[UI_NAV_MAX_DEPTH];
	int visibleStart;
	int visibleCount;
	int overflow;
} UiNavBar;

void UiNavBar_Init(UiNavBar *nav, UiId id);
void UiNavBar_SetItems(UiNavBar *nav, const UiNavItem *items, int count);
void UiNavBar_SetLayout(UiNavBar *nav, int layout);
void UiNavBar_SetRail(UiNavBar *nav, int rail);
void UiNavBar_SetStyle(UiNavBar *nav, const UiNavStyle *style);

/* 复制 depth 个稳定 id; 未在树中的后缀保留, 等数据恢复后重新高亮. */
void UiNavBar_SetSelected(UiNavBar *nav, const UiId *path, int depth);
int  UiNavBar_GetSelected(const UiNavBar *nav, UiId *path, int cap);

/* 画导航栏本体; 返回 1 = 本帧发生了选中变化 (同 changed). */
int  UiNavBar_Draw(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
	UiRect rc, int enabled);
/* 帧末画所有已展开面板. */
void UiNavBar_DrawPopups(UiNavBar *nav, UiPopupManager *pm, UiContext *ui);
/* 取走一次选中变化; 返回 1 = 有变化, path/cap 可为 NULL/0. */
int  UiNavBar_TakeChanged(UiNavBar *nav, UiId *path, int cap);

int  UiNavBar_Key(UiNavBar *nav, UiPopupManager *pm, UiContext *ui, UINT vk);

/* 清零组件状态; 不持有 items 内存. */
void UiNavBar_Shutdown(UiNavBar *nav);

#endif /* CUI_UI_NAV_H */
