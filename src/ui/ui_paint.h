/*
 * ui_paint.h - 交互表面的统一外观: 填充 / 描边 / 光晕
 *
 * 所有可交互控件 (按钮、编辑框、以后的开关/标签/表格单元) 都调这一个函数,
 * 颜色按 Ui_State 平滑出来的 hover/press/focus 插值, 光晕只在命中时出现。
 * 控件自身不再关心"悬停该变多亮""聚焦该不该发光"。
 */
#ifndef SCV2_UI_PAINT_H
#define SCV2_UI_PAINT_H

#include "ui/ui_context.h"

enum { UI_GLOW_HOVER = 1u << 0, UI_GLOW_FOCUS = 1u << 1 };

typedef struct {
	UiColor bg, bg2;                       /* bg2 非 0 = 上下渐变 (bg -> bg2) */
	UiColor hoverBg, pressBg;              /* pressBg 可 0 = 不区分 */
	UiColor border, hoverBorder, focusBorder;
	UiColor glow;                          /* 含 alpha; 0 = 不发光 */
	float   radius;                        /* useCorners=0 时使用 */
	GfxCorners corners;                    /* 四角独立圆角 */
	int     useCorners;                    /* 1 = 用 corners 代替 radius */
	int     dashed;                        /* 1 = 虚线描边 */
	float   borderW;
	float   glowW;                         /* 光晕外扩宽度 (DIP) */
	unsigned glowMask;                     /* UI_GLOW_* 位掩码 */
	/* 渐变面专用: hover/press 是"整体蒙上一层纯色"(两端同色)时置 1。
	 * 绘制改为 固定渐变 + 半透明纯色叠加, 动画期间渐变画刷可复用不重建。 */
	int     overlayTint;
} UiSurfaceStyle;

/* 画一个交互表面并返回本帧交互结果 (调用方随后画文字/图标). */
UiWidgetState Ui_DrawSurface(UiContext *ui, UiId id, UiRect rc,
	const UiSurfaceStyle *style, int enabled);

#endif /* SCV2_UI_PAINT_H */
