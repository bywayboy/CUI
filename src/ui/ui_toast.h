/*
 * ui_toast.h - 带渐变 overlay 的 Toast
 *
 * 两种模式:
 *   1. 合成器模式 (推荐): UiToast_Attach 之后, 渐变遮罩和卡片画在独立的
 *      GfxLayer 上, 主窗只保留一个不可见的全窗输入 blocker. 内容只画一次,
 *      淡入淡出交给 DWM 补间; Toast 显示期间主窗不需要任何重绘, 进度条
 *      overlay 也能继续在下面更新.
 *   2. 主窗回退: 未 Attach (老代码) 或 layer 创建失败时, 仍在主窗里画
 *      渐变/卡片, 并按帧请求重绘.
 *
 * 时间用单调毫秒 (UiFrame_Now()); 应用每帧 UiToast_Tick 决定是否需要重绘.
 */
#ifndef CUI_UI_TOAST_H
#define CUI_UI_TOAST_H

#include "ui/ui_context.h"

#define UI_TOAST_FADE_MS 200

typedef enum {
	UI_TOAST_INFO = 0,
	UI_TOAST_SUCCESS = 1,
	UI_TOAST_ERROR = 2
} UiToastKind;

typedef struct UiToast {
	char text[256];
	double startMs;
	double holdUntilMs;
	double hideStartMs;
	int kind;
	int visible;
	int blockInput;

	/* 合成器 layer 状态 (UiToast_Attach 后使用) */
	UiContext *ui;
	GfxLayer *layer;
	int layerW, layerH;
	int layerDirty;
	int layerIn, layerOut;
	int fallback;            /* layer 不可用, 退回主窗绘制 */
	uint32_t themeRev;       /* UiTheme.themeRev: 任意换肤都会重画 layer */
} UiToast;

void UiToast_Init(UiToast *t);

/* 绑定 UiContext: 之后优先走 DComp layer. 未调用则保持主窗回退行为. */
void UiToast_Attach(UiToast *t, UiContext *ui);

void UiToast_Show(UiToast *t, const char *text, double nowMs,
	int durationMs, int kind);
void UiToast_Hide(UiToast *t, double nowMs);
int  UiToast_IsVisible(const UiToast *t);

/* 1 = 本帧需要重绘主窗 (仅主窗回退模式; layer 模式返回 0). */
int  UiToast_Tick(UiToast *t, double nowMs);

/* 在 Ui_BeginFrame/EndFrame 之间、基础 UI 之后调用; 视图尺寸为 DIP.
 * layer 模式只设置全窗 input blocker, 不绘制任何像素. */
void UiToast_Draw(UiContext *ui, UiToast *t, double nowMs,
	float viewW, float viewH);

/* 在 Gfx_End 之后调用: 同步 layer 尺寸/主题/淡入淡出 (layer 模式). */
void UiToast_EndFrame(UiToast *t, double nowMs, float viewW, float viewH);

/* 1 = Toast 使用合成器 layer (主窗无需重绘); 0 = 主窗回退. */
int  UiToast_UsingLayer(const UiToast *t);

void UiToast_Shutdown(UiToast *t);

UiId UiToast_BackdropId(void);
UiId UiToast_CardId(void);

#endif /* CUI_UI_TOAST_H */
