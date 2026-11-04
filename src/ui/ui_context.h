/*
 * ui_context.h - 即时模式 UI 上下文: 输入状态、控件注册/命中、拖动、单帧事件
 *
 * 生命周期:
 *   Ui_BeginFrame -> 组件绘制并 Ui_State/Ui_RegisterWidget -> Ui_EndFrame
 * 输入:
 *   WM_MOUSEMOVE/DOWN/UP -> Ui_On* 更新 hover/pressed/clicked/released
 *   下一次绘制时组件读取这些一次性事件并执行动作。
 */
#ifndef SCV2_UI_CONTEXT_H
#define SCV2_UI_CONTEXT_H

#include "ui/ui_types.h"
#include "ui/ui_theme.h"
#include "ui/ui_anim.h"

#define UI_MAX_WIDGETS 256
#define UI_LAYER_BASE    0
#define UI_LAYER_OVERLAY 1

/* z-index: 只影响命中优先级; 绘制顺序仍是调用顺序 (即时模式, 按 z 从低到高画). */
#define UI_Z_BASE    0
#define UI_Z_OVERLAY 1000
#define UI_Z_TOAST   2000

typedef struct {
	UiId id;
	UiRect rect;
	int layer;
	int z;
} UiWidget;

struct UiContext {
	HWND hwnd;
	Gfx *gfx;
	UiTheme *theme;
	float scale;
	float width, height;      /* 视口尺寸（DIP） */
	float mouseX, mouseY;     /* 最近鼠标位置（DIP） */
	int mouseDown;

	UiWidget widgets[UI_MAX_WIDGETS];
	int widgetCount;
	int layer;
	int z;                    /* 当前注册层 z-index */
	int zDepth;
	int zStack[8];
	int layerStack[8];

	UiId hover;
	UiId pressed;
	UiId active;
	UiId focusId;             /* 由 App 每帧设置: 键盘焦点所在控件 (编辑框) */
	UiId clicked;             /* 单帧: 同一控件内按下并抬起 */
	UiId down;                /* 单帧: 本次按下的控件 (编辑框立即定位光标用) */
	UiId released;            /* 单帧: 按下过的控件被抬起（滑块提交用） */
	int clickedOutside;       /* 单帧: 抬起时未命中任何控件 */
	int popupOpen;
	UiId modal;               /* 非 0: 不可见全窗输入 blocker (合成器 Toast 等) */

	GfxRect panelRect;        /* 设备面板内容区（弹窗定位/夹取） */
	GfxRect closeRect;        /* 顶部关闭按钮命中区 */

	struct {
		UiId id;
		float v1, v2;
		int active;
	} drag;

	/* 动画: 固定池, 按控件 ID 存通道值 (见 ui_anim.h) */
	UiAnimEntry anim[UI_AN_MAX_ENTRIES];
	uint32_t    animFrame;
	double      animTimeMs;
	int         animReduced;      /* 系统关闭了"显示动画" */
	UiRect      animDirty;        /* 本帧动画改动的并集 (DIP) */
	int         animDirtyHas;
	int         animDirtyFull;    /* 无法定位到矩形 (窗口淡入等) */
};

void Ui_ContextInit(UiContext *ctx, HWND hwnd, Gfx *gfx, UiTheme *theme);
void Ui_SetViewport(UiContext *ctx, float w, float h, float scale);

void Ui_BeginFrame(UiContext *ctx);
void Ui_EndFrame(UiContext *ctx);

void Ui_RegisterWidget(UiContext *ctx, UiId id, UiRect rc);
UiId Ui_HitTest(const UiContext *ctx, float x, float y);

/* 切换注册层: 命中优先取最高 z; 同 z 时后注册(后画)的优先。
 * 必须成对; 绘制顺序仍需调用方按 z 从低到高排。 */
void Ui_PushLayer(UiContext *ctx, int z);
void Ui_PopLayer(UiContext *ctx);
UiWidgetState Ui_State(UiContext *ctx, UiId id, UiRect rc, int enabled);
void Ui_SetFocusId(UiContext *ctx, UiId id);

/* 不可见的全窗输入 blocker: 设置后所有命中都返回 id, 但不会触发控件重绘.
 * 用于合成器 Toast (遮罩在独立的 DComp layer 上, 主窗不需要重画).
 * 传 UI_ID_NONE 解除. */
void Ui_SetModal(UiContext *ctx, UiId id);

void Ui_OnMouseMove(UiContext *ctx, float x, float y);
void Ui_OnMouseDown(UiContext *ctx, float x, float y);
void Ui_OnMouseUp(UiContext *ctx, float x, float y);
void Ui_OnMouseLeave(UiContext *ctx);
void Ui_OnCaptureChanged(UiContext *ctx);

#endif /* SCV2_UI_CONTEXT_H */
