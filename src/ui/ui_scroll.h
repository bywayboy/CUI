/*
 * ui_scroll.h - 通用滚动容器 + 高度缓存布局
 *
 * 分层:
 *   UiScrollLayout: 测量缓存 / 前缀 y / contentH / 可见区间 (不分配, 调用方给数组)
 *   UiScrollView  : 视口 / 裁剪 / 滚轮 / 惯性 / 回弹 / el-scrollbar 风格滚动条
 *
 * 内容尺寸由应用测好后交给 Begin; 应用只绘制 VisibleRange 内的块.
 * 状态由应用持有, 与 ViewModel 同生命周期.
 */
#ifndef CUI_UI_SCROLL_H
#define CUI_UI_SCROLL_H

#include "ui/ui_context.h"

/* ── 滚动配置 ───────────────────────────────────────────────────────── */

typedef struct UiScrollConfig {
	float wheelStep;         /* DIP / 滚轮格; 默认 48 */
	float wheelTauMs;        /* 滚轮平滑时间常数; 0 = 立即; 默认 120 */
	float flingDecel;        /* 惯性减速度 DIP/s^2; 默认 2600 */
	float minVelocity;       /* 停止阈值 DIP/s; 默认 8 */
	int   bounce;            /* 1 = 越界回弹; 默认桌面 0 / 触摸 1 */
	float overscrollResist;  /* 拖拽越界阻尼 0..1; 默认 0.55 */
	float maxOverscroll;     /* 最大越界 DIP; 默认 40 */
	int   autoHideBar;       /* 1 = 自动隐藏滚动条 */
	float barHideDelayMs;    /* 停止滚动后延迟; 默认 800 */
	float barFadeMs;         /* 淡入淡出; 默认 150 */
} UiScrollConfig;

UiScrollConfig UiScrollConfig_Desktop(void);
UiScrollConfig UiScrollConfig_Touch(void);

/* ── 滚动条样式 (0 = 主题默认) ─────────────────────────────────────── */

typedef struct UiScrollbarStyle {
	float width;        /* 0 -> 6 DIP */
	float minThumb;     /* 0 -> 24 DIP */
	float margin;       /* 0 -> 2 DIP */
	float radius;       /* 0 -> width/2 */
	UiColor track;      /* 0 -> theme.text3 @ 18 */
	UiColor thumb;      /* 0 -> theme.text3 @ 90 */
	UiColor thumbHover; /* 0 -> theme.text3 @ 140 */
	int alwaysVisible;  /* 1 = 覆盖 autoHideBar */
	/* 1 = 在内容右侧预留滚动条槽 (内容宽度扣除 width+margin);
	 * 0 = overlay (el-scrollbar 风格), 内容可能延伸到 thumb 下方. */
	int reserveSpace;
} UiScrollbarStyle;

/* 视口内边距 (DIP). 上下边距不随内容滚动; 右内边距可与 reserveSpace
 * 一起保证内容完全不和 thumb 重叠. */
typedef struct UiScrollPadding {
	float left, top, right, bottom;
} UiScrollPadding;

UiScrollbarStyle UiScrollbarStyle_Default(void);

/* ── 滚动容器 ───────────────────────────────────────────────────────── */

typedef struct UiScrollView {
	UiId id;
	UiScrollConfig cfg;
	UiScrollbarStyle bar;

	UiRect viewport;
	UiRect inner;        /* padding box: 内容裁剪/可见区域 */
	UiScrollPadding pad;
	float contentW, contentH;

	float offset;       /* 实际位置; 回弹期间可短暂越界 */
	float target;       /* 目标位置 */
	float velocity;     /* DIP/s, + 向下 */
	float lastMaxOff;   /* 上一帧最大偏移, 用于内容缩小时夹取 */

	float barOpacity;
	float barTarget;
	float idleMs;
	int   mouseInside;
	int   barHold;
	int   draggingThumb;
	float dragGrab;     /* 按下时 mouseY - thumb.y */
	int   active;       /* Begin 已压 clip, End 需要弹 */
	int   clipPushed;   /* 输入 clip 是否压栈成功 */
	int   busy;
} UiScrollView;

void UiScrollView_Init(UiScrollView *sv, UiId id,
	const UiScrollConfig *cfg, const UiScrollbarStyle *style);

/* 四向 padding; 每帧/属性变化时设置, 不触发额外分配. */
void UiScrollView_SetPadding(UiScrollView *sv, UiScrollPadding pad);

/* 在 Begin 前计算可用内容宽度: viewport - padding - 可选滚动条槽.
 * 变高布局用它作为 measure 宽度, 保证与 Begin 内部一致. */
float UiScrollView_ContentWidth(const UiScrollView *sv, UiRect viewport);

/* 帧流程: Tick -> Begin -> 应用绘制可见内容 -> End.
 * Tick 传 dtMs (UiApp tick 回调); 静止时不请求帧. */
void UiScrollView_Tick(UiScrollView *sv, UiContext *ui, float dtMs);
void UiScrollView_Begin(UiScrollView *sv, UiContext *ui,
	UiRect viewport, float contentW, float contentH);
void UiScrollView_End(UiScrollView *sv, UiContext *ui);

/* 内容面的屏幕矩形 (y 已减 offset); 应用在其中按局部坐标绘制. */
UiRect UiScrollView_ContentRect(const UiScrollView *sv);
float  UiScrollView_MaxOffset(const UiScrollView *sv);

/* 与视口不相交返回 1; 应用应在绘制/文本排版前跳过. */
int UiScrollView_Cull(const UiScrollView *sv, UiRect screenRc);

void UiScrollView_ScrollTo(UiScrollView *sv, UiContext *ui,
	float offset, int animate);
void UiScrollView_EnsureVisible(UiScrollView *sv, UiContext *ui,
	float localY, float localH);
void UiScrollView_Fling(UiScrollView *sv, UiContext *ui, float velocity);
int  UiScrollView_Wheel(UiScrollView *sv, UiContext *ui, float delta);
int  UiScrollView_Busy(const UiScrollView *sv);

/* ── 高度缓存布局 ───────────────────────────────────────────────────── */

#define UI_SCROLL_ITEM_WIDTH_DEPENDENT 1u

typedef struct UiScrollItem {
	UiId     id;          /* 稳定 ID, 仅诊断/应用使用 */
	uint32_t rev;         /* 内容版本 */
	unsigned flags;       /* UI_SCROLL_ITEM_* */
	float    height;      /* 最近测量高度 */
	float    y;           /* 含 padTop 的局部 y (Commit 生成) */
	float    measuredW;   /* 测量时的可用宽度 */
	int      measured;
} UiScrollItem;

typedef struct UiScrollLayout {
	UiScrollItem *items;  /* 调用方数组, 库不分配; 固定模式可为 NULL */
	int   count, cap;
	float width;
	float contentH;
	float gap, padTop, padBottom;
	float fixedH;         /* >0 = 固定行高模式, 忽略 items */
	uint32_t revision;    /* 每次 Commit +1 (布局变化时) */
	int   dirty;
} UiScrollLayout;

void  UiScrollLayout_Init(UiScrollLayout *l, UiScrollItem *items, int cap);
void  UiScrollLayout_SetCount(UiScrollLayout *l, int count);
void  UiScrollLayout_SetPadding(UiScrollLayout *l, float top, float bottom);
void  UiScrollLayout_SetGap(UiScrollLayout *l, float gap);
/* 固定行高模式; count/rowH 不变时为 no-op. */
void  UiScrollLayout_SetFixed(UiScrollLayout *l, int count, float rowH);
float UiScrollLayout_Width(const UiScrollLayout *l);

/* 宽度变化时只失效宽相关项; 返回 1 = 发生失效. */
int   UiScrollLayout_SetWidth(UiScrollLayout *l, float width);
int   UiScrollLayout_ItemNeedsMeasure(const UiScrollLayout *l, int index,
	uint32_t rev);
void  UiScrollLayout_SetItemFlags(UiScrollLayout *l, int index, unsigned flags);
void  UiScrollLayout_SetItemHeight(UiScrollLayout *l, int index,
	uint32_t rev, float height);
void  UiScrollLayout_InvalidateItem(UiScrollLayout *l, int index);
void  UiScrollLayout_InvalidateAll(UiScrollLayout *l);

/* 重算 y/contentH; 返回 1 = contentH 变化. 只在 dirty 时做 O(N). */
int   UiScrollLayout_Commit(UiScrollLayout *l);

float UiScrollLayout_ContentHeight(const UiScrollLayout *l);
float UiScrollLayout_ItemY(const UiScrollLayout *l, int index);
/* 可见区间 [first,last]; 可能为空 (first > last). */
void  UiScrollLayout_VisibleRange(const UiScrollLayout *l,
	float offset, float viewH, int *first, int *last);
UiRect UiScrollLayout_ItemRect(const UiScrollLayout *l, int index,
	UiRect contentRc);

#endif /* CUI_UI_SCROLL_H */
