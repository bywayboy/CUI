/*
 * ui_layout.h - 轻量布局: UiStack (游标) + UiFlex (basis/grow)
 *
 * 只计算矩形, 不绘制、不注册控件、不持有控件状态。
 * 坐标全部是 DIP; padding/gap 属于容器, 没有 per-item margin。
 */
#ifndef CUI_UI_LAYOUT_H
#define CUI_UI_LAYOUT_H

#include "ui/ui_types.h"

enum {
	UI_ROW = 0,
	UI_COLUMN = 1
};

enum {
	UI_ALIGN_START = 0,
	UI_ALIGN_CENTER = 1,
	UI_ALIGN_END = 2
};

/* 游标式: 固定尺寸 + 单个 Fill + 主轴末端对齐.
 * 适合纵向表单/简单按钮行; 权重分配用 UiFlex. */
typedef struct UiStack {
	UiRect outer;
	UiRect inner;
	int dir;
	float gap;
	float cursor;      /* 主轴已用长度 (相对 inner) */
	int count;         /* 已取项数, 用于自动 gap */
	int fillUsed;
} UiStack;

void   Ui_StackBegin(UiStack *s, UiRect outer, int dir, float gap, float padding);
void   Ui_StackBeginPad(UiStack *s, UiRect outer, int dir, float gap,
                        float left, float top, float right, float bottom);
UiRect Ui_StackInner(const UiStack *s);
float  Ui_StackRemaining(const UiStack *s);

UiRect Ui_StackTake(UiStack *s, float size);
UiRect Ui_StackTakeFill(UiStack *s);
UiRect Ui_StackTakeRight(UiStack *s, float size);
/* mainSize 沿主轴; crossSize<=0 表示铺满交叉轴. */
UiRect Ui_StackTakeAligned(UiStack *s, float mainSize, float crossSize, int align);
void   Ui_StackSkip(UiStack *s, float amount);

/* 声明式 flex-lite: basis 固定基础尺寸, grow 是剩余空间的权重.
 * 不做 shrink; 超出就是溢出. min/max 会夹取并重分剩余 (最多 4 轮). */
typedef struct UiFlexItem {
	float basis;       /* DIP; 0 = 没有基础尺寸 */
	float grow;        /* 剩余空间权重; 0 = 固定 */
	float minSize;     /* 0 = 不限制 */
	float maxSize;     /* 0 = 不限制 */
} UiFlexItem;

int Ui_FlexRow(UiRect inner, float gap,
               const UiFlexItem *items, int count, UiRect *out);
int Ui_FlexCol(UiRect inner, float gap,
               const UiFlexItem *items, int count, UiRect *out);

#endif /* CUI_UI_LAYOUT_H */
