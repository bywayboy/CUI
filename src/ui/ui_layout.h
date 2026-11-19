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
	UI_ALIGN_END = 2,
	UI_ALIGN_BASELINE = 3   /* 仅行布局: 首行文字基线对齐; baseline=0 退回起点 */
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

/* 测量协议: 回调在 maxW 约束下报告尺寸.
 * maxW < 0 表示不限宽 (自然尺寸); maxW >= 0 时 prefH 是换行后的高度.
 * 回调可只写关心的字段, 其余由 Ui_MeasureNormalize 归零/修正:
 * 0 <= minW <= prefW, 0 <= minH <= prefH.
 * baseline 是首行文字基线到顶边的距离 (0 = 未知), 归一化到 [0, prefH]. */
typedef struct UiMeasure {
	float maxW;     /* 输入 */
	float minW;     /* 不可再压缩的宽度 (最长不可断内容) */
	float prefW;    /* 期望宽度 */
	float minH;
	float prefH;
	float baseline;
} UiMeasure;

typedef void (*UiMeasureFn)(void *user, UiMeasure *m);

/* measure 容器项: 主轴尺寸取测量值, 交叉轴默认取测量值.
 * minSize/maxSize 是主轴硬限制; minCross/maxCross 是交叉轴硬限制 (0 = 不限). */
typedef struct UiMeasureItem {
	void *user;
	UiMeasureFn measure;
	float grow;       /* 主轴剩余空间权重; 0 = 不扩张 */
	float minSize;    /* 主轴硬下限; 0 = 无 */
	float maxSize;    /* 主轴硬上限; 0 = 无 */
	float minCross;   /* 交叉轴硬下限; 0 = 无 */
	float maxCross;   /* 交叉轴硬上限; 0 = 无 */
	float crossSize;  /* 交叉轴固定尺寸; 0 = 用测量值 */
	int stretch;      /* 1 = 交叉轴铺满容器 */
	int align;        /* 未拉伸时的交叉轴对齐: UI_ALIGN_* */
} UiMeasureItem;

void Ui_MeasureNormalize(UiMeasure *m);

/* 固定尺寸/弹簧项的测量适配器: w/h 为自然尺寸, 0 表示无基础尺寸.
 * 弹簧项用 w=0, grow>0; 固定项用 w=固定宽, grow=0. */
typedef struct UiFixedSize {
	float w;
	float h;
} UiFixedSize;

void Ui_MeasureFixed(void *user, UiMeasure *m);

/* 构造固定项 (grow=0) 或弹簧项 (w/h=0, grow>0); 交叉轴默认铺满. */
static __inline UiMeasureItem Ui_FixedItem(UiFixedSize *s, float grow)
{
	UiMeasureItem it;
	it.user = s;
	it.measure = Ui_MeasureFixed;
	it.grow = grow;
	it.minSize = 0.0f;
	it.maxSize = 0.0f;
	it.minCross = 0.0f;
	it.maxCross = 0.0f;
	it.crossSize = 0.0f;
	it.stretch = 1;
	it.align = UI_ALIGN_START;
	return it;
}

/* 容器自然尺寸 (相当于 maxW<0 测量): 行输出主轴 = 各 min/pref 之和 + gap,
 * 交叉轴 = 最大高度; 列相反. 用于父容器决定子容器需要多大. */
void Ui_FlexMeasure(int dir, float gap,
                    const UiMeasureItem *items, int count, UiMeasure *out);

/* 在 inner 内求解并摆放: 主轴按 [硬下限, 期望, 硬上限] 分配
 * (空间不足优先压到硬下限并允许溢出, 富余按 grow 分配),
 * 再以最终主轴尺寸重测交叉轴. 返回 1 = 成功. */
int Ui_FlexLayout(UiRect inner, int dir, float gap,
                  const UiMeasureItem *items, int count, UiRect *out);

/* ── Grid ───────────────────────────────────────────────────────────
 * 显式轨道 + 隐式 auto 轨道; 固定 / auto / fr 三种轨道。
 * 自动放置按行优先; dense=1 时每项从 (0,0) 重新寻找最早空位。
 * 显式行列与已占区域冲突时就近回填, 摆放结果互不重叠。
 * 跨格项的尺寸贡献均分到跨过的 auto/fr 轨道, 固定轨道不吸收内容。 */
enum {
	UI_GRID_FIXED = 0,   /* value = DIP */
	UI_GRID_AUTO = 1,    /* 内容自适应 */
	UI_GRID_FR = 2       /* value = 剩余空间权重 (<=0 视为 1) */
};

typedef struct UiGridTrack {
	int kind;        /* UI_GRID_* */
	float value;     /* FIXED: DIP; FR: 权重; AUTO 忽略 */
	float minSize;   /* minmax 下限; 0 = 内容/0 */
	float maxSize;   /* minmax 上限; 0 = 内容/不限 */
} UiGridTrack;

typedef struct UiGridItem {
	void *user;
	UiMeasureFn measure;
	int row;         /* -1 = 自动 */
	int col;         /* -1 = 自动 */
	int rowSpan;     /* <=0 视为 1 */
	int colSpan;     /* <=0 视为 1 */
	int stretch;     /* 1 = 填满单元格; 0 = 自然尺寸 */
	int align;       /* 未拉伸时单元格内对齐 (UI_ALIGN_*) */
} UiGridItem;

typedef struct UiGrid {
	int rows;        /* 显式行数; 0 = 全部隐式 */
	int cols;        /* 显式列数; 0 = 由显式项推断 */
	float rowGap;
	float colGap;
	int dense;
} UiGrid;

/* 自然尺寸: maxW<0 取内容宽; maxW>=0 时按该宽度解列并报告换行后高度。 */
void Ui_GridMeasure(const UiGrid *g,
                    const UiGridTrack *colTracks,
                    const UiGridTrack *rowTracks,
                    const UiGridItem *items, int count, UiMeasure *out);

/* 在 inner 内放置; out[i] 必须与 items[i] 对应。返回 1 = 成功。 */
int Ui_GridLayout(UiRect inner, const UiGrid *g,
                  const UiGridTrack *colTracks,
                  const UiGridTrack *rowTracks,
                  const UiGridItem *items, int count, UiRect *out);

#endif /* CUI_UI_LAYOUT_H */
