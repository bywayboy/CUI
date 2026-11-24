/*
 * ui_tab.h - TabBar (选择/键盘) + TabPager (页面转场)
 *
 * 两个都是应用持有的状态管理器, 不做保留式页面树:
 *   UiTabBar  : 只画 header, 维护 selected/focus/disabledMask/changed
 *   UiTabPager: 只负责 current/from/progress, 用回调绘制页面
 *
 * 支持 LINE / PILL 两种 header; 页面转场支持横向/纵向滑动、可选淡入淡出、
 * 反向翻页; 转场期间页面控件不注册命中 (输入锁).
 */
#ifndef CUI_UI_TAB_H
#define CUI_UI_TAB_H

#include "ui/ui_context.h"
#include "ui/ui_icon.h"

typedef enum {
	UI_TAB_LINE = 0,
	UI_TAB_PILL,
	UI_TAB_CARD
} UiTabVariant;

typedef enum {
	UI_TAB_AXIS_NONE = 0,
	UI_TAB_AXIS_HORIZONTAL,
	UI_TAB_AXIS_VERTICAL
} UiTabAxis;

typedef struct UiTabStyle {
	int   variant;
	float height;       /* 0 -> LINE 38 / PILL 34 */
	float padX;         /* 0 -> 14 */
	float gap;          /* 0 -> LINE 4 / PILL 2 */
	float indicatorW;   /* LINE 指示条高度, 0 -> 2 */
	float radius;       /* PILL/CARD 圆角, 0 -> 8 */
	int   stretch;      /* 1 = header 内等宽, 0 = 文字宽度 */
	int   showDivider;  /* LINE 底部 1px 分隔线 */
	UiColor text;
	UiColor textHover;
	UiColor textActive;
	UiColor textDisabled;
	UiColor indicator;
	UiColor hoverBg;
	UiColor activeBg;
	UiColor activeBorder;
	UiColor cardBg;       /* CARD 非活动 tab 底; 0 -> neutral.soft */
	UiColor contentBg;    /* CARD 活动 tab/内容面板底; 0 -> theme.card */
	int     mergeContent; /* CARD: 1 = 活动 tab 与内容面板连通 */
} UiTabStyle;

UiTabStyle UiTabStyle_Line(void);
UiTabStyle UiTabStyle_Pill(void);
UiTabStyle UiTabStyle_Card(void);

#define UI_TAB_GEOM_MAX 32

/* header 与可见 tab 项的几何. 与 DrawEx 共用同一套宽度/间距规则,
 * 设计器静态画布用它避免维护第二套公式. */
typedef struct UiTabBarGeometry {
	UiRect header;
	UiRect inner;
	int variant;
	int count;
	UiRect items[UI_TAB_GEOM_MAX];
	float textW[UI_TAB_GEOM_MAX];
} UiTabBarGeometry;

/* 只计算几何, 不绘制、不注册命中. 返回可见项数. */
int UiTabBar_Measure(UiContext *ui, UiRect rc, const char *const *labels,
	const UiIcon *icons, const UiTabStyle *style, int count,
	UiTabBarGeometry *geo);

typedef struct UiTabBar {
	UiId id;
	int count;
	int selected;
	int focus;
	int focusVisible;
	int changed;
	unsigned disabledMask;
	UiRect rc;
	int indicatorInit;
	/* LINE 指示条: 合成器路径下是独立 visual (PartFx), 宽度用 clip 补间 */
	void *indPart;
	unsigned indColor;
	float indContentW;
	float indH;
	int indSel;
	/* CARD 两阶段绘制: Draw 保存活动项几何, DrawActive 在内容之后覆盖. */
	UiRect activeRc;
} UiTabBar;

void UiTabBar_Init(UiTabBar *tb, UiId id, int count);
void UiTabBar_SetCount(UiTabBar *tb, int count);
void UiTabBar_SetSelected(UiTabBar *tb, int index);
void UiTabBar_SetDisabledMask(UiTabBar *tb, unsigned mask);
void UiTabBar_SetFocusVisible(UiTabBar *tb, int visible);

/* 返回本帧点击/激活的 tab index; -1 = 无. changed 表示 selected 真的变了.
 * labels 长度 >= count; style=NULL 用 LINE. */
int UiTabBar_Draw(UiTabBar *tb, UiContext *ui, UiRect rc,
	const char *const *labels, const UiTabStyle *style, int enabled);
/* 可选图标 (icons 长度 >= count; icons=NULL 等价于普通 Draw). */
int UiTabBar_DrawEx(UiTabBar *tb, UiContext *ui, UiRect rc,
	const char *const *labels, const UiIcon *icons,
	const UiTabStyle *style, int enabled);

/* CARD: 在内容面板/页面之后调用, 画活动 tab 填充/三边/文字/焦点环.
 * LINE/PILL 为空操作. 只画不注册, 不改变选中/命中状态. */
void UiTabBar_DrawActive(UiTabBar *tb, UiContext *ui,
	const char *const *labels, const UiTabStyle *style);
void UiTabBar_DrawActiveEx(UiTabBar *tb, UiContext *ui,
	const char *const *labels, const UiIcon *icons,
	const UiTabStyle *style);

/* 最近一次 Draw 的活动 tab 矩形; 应用可用它在内容面板顶边让出接缝. */
UiRect UiTabBar_ActiveRect(const UiTabBar *tb);

/* 组获得键盘焦点时处理方向键/Home/End/Space/Enter; 方向键只移动 focus,
 * Space/Enter 才切换 selected. 返回 1 = 已处理. */
int UiTabBar_Key(UiTabBar *tb, UiContext *ui, UINT vk);

typedef struct UiTabPagerConfig {
	int   axis;         /* UI_TAB_AXIS_* */
	int   fade;         /* 1 = 与滑动同时做淡入淡出 */
	int   reverse;      /* 1 = 翻转滑动方向 */
	float durationMs;   /* 0 -> 260 */
} UiTabPagerConfig;

typedef struct UiTabPager {
	UiId id;
	UiTabPagerConfig cfg;
	int from, to;
	float progress;     /* 0..1 */
	int animating;
	UiRect viewport;
} UiTabPager;

/* page 回调: rc 已包含滑动偏移; interactive=0 时不要注册控件 (转场). */
typedef void (*UiTabPageFn)(UiContext *ui, void *user, int index,
	UiRect rc, int interactive);

void UiTabPager_Init(UiTabPager *p, UiId id, const UiTabPagerConfig *cfg);
void UiTabPager_GoTo(UiTabPager *p, UiContext *ui, int index, int animate);
int  UiTabPager_Active(const UiTabPager *p);
int  UiTabPager_Busy(const UiTabPager *p);
void UiTabPager_Tick(UiTabPager *p, UiContext *ui, float dtMs);

/* 画当前页或转场的两页; 内部负责 clip 和输入锁. */
void UiTabPager_Draw(UiTabPager *p, UiContext *ui, UiRect viewport,
	UiTabPageFn fn, void *user);

#endif /* CUI_UI_TAB_H */
