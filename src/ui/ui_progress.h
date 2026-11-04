/*
 * ui_progress.h - 自循环进度条组件
 *
 * 封装: 6 秒循环值、轨道绘制、GfxOverlay 合成器补间、ULW/回退绘制、
 *       暂停/恢复、主题切换重建、Toast 出现时让位。
 *
 * 用法:
 *   UiProgress_Init(&p, &app->ui, 6000);
 *   帧内: UiProgress_Draw(&p, rc);
 *   Gfx_End 之后: UiProgress_EndFrame(&p);
 *   动画节拍: UiProgress_Tick(&p, dtMs);
 */
#ifndef CUI_UI_PROGRESS_H
#define CUI_UI_PROGRESS_H

#include "ui/ui_context.h"

typedef struct UiProgress {
	UiContext *ui;
	GfxOverlay *overlay;
	UiRect rc;
	UiThemeId themeId;
	float value;        /* 0..1 */
	float cycleMs;
	int paused;
	int hidden;         /* Toast/模态遮罩出现时隐藏 overlay, 主窗回退绘制 */
} UiProgress;

void  UiProgress_Init(UiProgress *p, UiContext *ui, int cycleMs);
void  UiProgress_Tick(UiProgress *p, double dtMs);
void  UiProgress_SetPaused(UiProgress *p, int paused);
void  UiProgress_SetHidden(UiProgress *p, int hidden);

/* 帧内调用: 画轨道; overlay 不可用/隐藏时同时画填充. */
void  UiProgress_Draw(UiProgress *p, UiRect rc);

/* Gfx_End 之后调用: 创建/更新 overlay, 处理主题与尺寸变化. */
void  UiProgress_EndFrame(UiProgress *p);

int   UiProgress_Paused(const UiProgress *p);
int   UiProgress_OverlayAlive(const UiProgress *p);
int   UiProgress_NeedsMainRedraw(const UiProgress *p);
float UiProgress_Value(const UiProgress *p);
void  UiProgress_Shutdown(UiProgress *p);

#endif /* CUI_UI_PROGRESS_H */
