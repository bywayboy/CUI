/*
 * ui_pagination.h - 行内分页器（页码窗口 + 翻页）
 *
 * 应用持有 UiPagination；组件只负责页数推导、页码窗口、绘制与键盘，
 * 不持有数据、不滚动、不弹层。页码内部 0 基，界面显示 +1。
 *
 * 用法:
 *   UiPagination_Init(&pg, Ui_Id("vm.pg", 0, 0), 200, 20);
 *   帧内: int page = UiPagination_Draw(&pg, ui, rc, NULL);
 *   改数据量/每页条数: UiPagination_SetTotal / SetPageSize
 *   键盘: ui->navId == pg.id 时调用 UiPagination_Key
 *
 * changed 自上次 UiPagination_TakeChanged 以来 page 是否被改写
 * （点击、键盘、setter 都会置位），由应用决定何时消费。
 */
#ifndef CUI_UI_PAGINATION_H
#define CUI_UI_PAGINATION_H

#include "ui/ui_context.h"

typedef struct UiPaginationStyle {
	float itemW;            /* 0 -> 30；实际格宽还会保证页码不被截断 */
	float itemH;            /* 0 -> 32 */
	float gap;              /* 0 -> 2 */
	float padX;             /* 0 -> 5；格内水平留白（每侧） */
	float radius;           /* 0 -> theme.buttonRadius */
	int   maxSlots;         /* 0 -> 按宽度自适应；否则 clamp 到 [5,9] */
	int   firstLast;        /* 1 = 额外显示首页/末页 « »，默认 0 */
	int   showSingle;       /* 1 = 只有一页也绘制，默认 0 */
	const char *prevText;   /* NULL -> 有图标字体时用 chevron，否则 "‹" */
	const char *nextText;   /* NULL -> 有图标字体时用 chevron，否则 "›" */
} UiPaginationStyle;

typedef struct UiPagination {
	UiId id;
	int  page;              /* 0 基当前页 */
	int  total;             /* 条目总数 */
	int  pageSize;          /* >= 1 */
	int  focusVisible;      /* 键盘焦点环 */
	int  changed;           /* 自上次 TakeChanged 以来 page 被改写 */
	UiRect rc;              /* 最近一次绘制区域（setter/键盘局部失效用） */
} UiPagination;

void UiPagination_Init(UiPagination *p, UiId id, int total, int pageSize);
int  UiPagination_PageCount(const UiPagination *p);
int  UiPagination_Offset(const UiPagination *p);   /* page*pageSize */

void UiPagination_SetPage(UiPagination *p, UiContext *ui, int page);
void UiPagination_SetTotal(UiPagination *p, UiContext *ui, int total);
/* 切换每页条数: 保持原首条数据落在新页内, 再 clamp. */
void UiPagination_SetPageSize(UiPagination *p, UiContext *ui, int pageSize);

/* 画分页条; 返回本帧鼠标/键盘激活的页码 (0 基), -1 = 无.
 * 宽度不足时依次退化为 数字 -> "‹ 6 / 20 ›" -> "‹ ›" -> 不绘制. */
int  UiPagination_Draw(UiPagination *p, UiContext *ui, UiRect rc,
	const UiPaginationStyle *style);

/* ui->navId == p->id 时处理: ←/→ ±1, Home/End, PgUp/PgDn ±5.
 * 返回 1 = 已处理 (即使已到边界). */
int  UiPagination_Key(UiPagination *p, UiContext *ui, UINT vk);

/* 需要的最小宽度; compact=1 取紧凑形态. 只有一页且不显示时返回 0. */
float UiPagination_Measure(const UiPagination *p, UiContext *ui,
	const UiPaginationStyle *style, int compact);

/* 页码窗口: out 输出 0 基页码, -1 = 省略号; 返回槽位数.
 * pageCount<=0 返回 0; maxSlots 会 clamp 到 [5,9]. */
int UiPagination_Slots(int page, int pageCount, int maxSlots, int *out);

/* 读取并清除 changed. */
int UiPagination_TakeChanged(UiPagination *p);

#endif /* CUI_UI_PAGINATION_H */
