/*
 * ui_tooltip.h - 悬停延迟提示
 *
 * 应用持有 UiTooltip; 在目标控件绘制/注册之后调用 UiTooltip_Update,
 * 在整帧最后调用 UiTooltip_Draw. 提示本身不注册命中.
 */
#ifndef CUI_UI_TOOLTIP_H
#define CUI_UI_TOOLTIP_H

#include "ui/ui_context.h"

typedef struct UiTooltip {
	UiId id;
	UiId target;
	UiRect anchor;
	char text[192];
	double hoverStartMs;
	int delayMs;
	int open;
	int side;   /* UI_POPUP_* */
	int align;  /* UI_POPUP_ALIGN_* */
} UiTooltip;

void UiTooltip_Init(UiTooltip *tt, UiId id);
void UiTooltip_SetDelay(UiTooltip *tt, int delayMs);

/* 每帧对目标调用: target 未被 hover 时隐藏; 悬停超过 delay 才开. */
void UiTooltip_Update(UiTooltip *tt, UiContext *ui, UiId target,
	UiRect anchor, const char *text, double nowMs);

/* 1 = 正在等待 delay 到期, 应用应把 UiApp 帧循环保持运行到 open. */
int UiTooltip_Pending(const UiTooltip *tt);

/* 在基础 UI/Toast 之后绘制; 无动画时会立即显示/隐藏. */
void UiTooltip_Draw(UiTooltip *tt, UiContext *ui);

#endif /* CUI_UI_TOOLTIP_H */
