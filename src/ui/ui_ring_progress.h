/*
 * ui_ring_progress.h - 圆形进度条
 *
 * 圆环从 12 点顺时针绘制; rc 包含可选标题。DComp 路径用完整圆环纹理的
 * 固定/旋转半平面相交补间, 主表面负责轨道/数值/标题; 稳定后回到主表面
 * 清晰圆弧。ULW 或设备回退复用 UiAnim 的 cubic-out 通道。
 */
#ifndef CUI_UI_RING_PROGRESS_H
#define CUI_UI_RING_PROGRESS_H

#include "ui/ui_context.h"
#include <stddef.h>

#define UI_RING_UNIT_MAX 32
#define UI_RING_TITLE_MAX 96
#define UI_RING_TEXT_MAX 96
#define UI_RING_THEME_COLOR 0u

typedef const char *(*UiRingProgressFormatFn)(char *buf, size_t size,
	double value, void *user);

typedef struct UiRingProgress {
	UiContext *ui;
	GfxRing *ring;
	UiId id;
	UiRect rc, ringRect, overlayRect;
	uint32_t themeRev;
	int gfxGen;
	double minValue, maxValue, value;
	float ringWidth;
	uint32_t color;
	char unit[UI_RING_UNIT_MAX];
	char title[UI_RING_TITLE_MAX];
	char valueText[UI_RING_TEXT_MAX];
	UiRingProgressFormatFn formatter;
	void *formatterUser;
	int durationMs;
	int roundCaps;
	int hidden;
	int cpuAnimating;
	int overlayDirty;
	int overlayUnavailable;
	int ringDirty;
	int drew;
} UiRingProgress;

void UiRingProgress_Init(UiRingProgress *p, UiContext *ui);
void UiRingProgress_Shutdown(UiRingProgress *p);

void UiRingProgress_SetValue(UiRingProgress *p, double value);
void UiRingProgress_SetRange(UiRingProgress *p, double minValue, double maxValue);
void UiRingProgress_SetDurationMs(UiRingProgress *p, int durationMs);
void UiRingProgress_SetUnit(UiRingProgress *p, const char *unit);
void UiRingProgress_SetTitle(UiRingProgress *p, const char *title);
void UiRingProgress_SetValueText(UiRingProgress *p, const char *text);
void UiRingProgress_SetFormatter(UiRingProgress *p, UiRingProgressFormatFn fn,
	void *user);
/* color==UI_RING_THEME_COLOR 使用主题 primary.base; width 为 DIP。 */
void UiRingProgress_SetStyle(UiRingProgress *p, float width, uint32_t color,
	int roundCaps);
void UiRingProgress_SetHidden(UiRingProgress *p, int hidden);

void UiRingProgress_Draw(UiRingProgress *p, UiRect rc);
void UiRingProgress_EndFrame(UiRingProgress *p);

int UiRingProgress_Animating(const UiRingProgress *p);
int UiRingProgress_OverlayAlive(const UiRingProgress *p);
double UiRingProgress_Value(const UiRingProgress *p);

#endif /* CUI_UI_RING_PROGRESS_H */
