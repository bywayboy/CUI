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
#define UI_MAX_WHEEL_TARGETS 16
#define UI_MAX_CLIP_DEPTH 8
#define UI_LAYER_BASE    0
#define UI_LAYER_OVERLAY 1

/* z-index: 只影响命中优先级; 绘制顺序仍是调用顺序 (即时模式, 按 z 从低到高画). */
#define UI_Z_BASE    0
#define UI_Z_OVERLAY 1000
#define UI_Z_TOAST   2000

typedef struct {
	UiId id;
	UiRect rect;
	UiRect clip;      /* 注册时的输入裁剪; w<=0 = 无裁剪 */
	int layer;
	int z;
} UiWidget;

typedef struct {
	UiId id;
	UiRect rect;
	UiRect clip;
	int layer;        /* 弹层打开时只接受 overlay 滚轮目标 */
} UiWheelTarget;

struct UiContext {
	HWND hwnd;
	Gfx *gfx;
	UiTheme *theme;
	float scale;
	float width, height;      /* 视口尺寸（DIP） */
	float mouseX, mouseY;     /* 最近鼠标位置（DIP） */
	int mouseDown;
	int mouseOver;            /* 光标在窗口客户区内 (WM_MOUSELEAVE 清 0) */
	int inputBlock;           /* >0: 本帧不注册控件/滚轮目标 (翻页转场) */

	UiWidget widgets[UI_MAX_WIDGETS];
	int widgetCount;
	int layer;
	int z;                    /* 当前注册层 z-index */
	int zDepth;
	int zStack[8];
	int layerStack[8];

	/* 输入裁剪: 注册控件时取当前裁剪与控件矩形的交集; 命中必须同时落在
	 * clip 内. ScrollView 用它阻止部分露出视口的控件在视口外被点击. */
	UiRect hitClip;           /* 当前裁剪 (w<=0 = 无) */
	int hitClipDepth;
	UiRect hitClipStack[UI_MAX_CLIP_DEPTH];

	/* 滚轮路由: 本帧登记的滚动区域, 最后登记(内层/后画)优先. */
	UiWheelTarget wheelTargets[UI_MAX_WHEEL_TARGETS];
	int wheelTargetCount;
	UiId wheelTarget;         /* 待消费的滚轮目标 */
	float wheelDelta;         /* 累计滚轮格数 (+ = 向上) */

	UiId hover;
	UiId pressed;
	UiId active;
	UiId focusId;             /* 由 App 每帧设置: 键盘焦点所在控件 (编辑框) */
	UiId navId;               /* 键盘导航焦点: ButtonGroup 等非文本控件 */
	int navFocusVisible;      /* 键盘焦点是否显示焦点样式; 鼠标按下清 0 */
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

/* 瞬时状态: 正常登记命中, 但不写动画通道, hover/press/focus 直接 0/1.
 * 用于菜单/下拉列表项等临时行: 指针离开应立即还原, 不能有淡出滞后. */
UiWidgetState Ui_StateInstant(UiContext *ctx, UiId id, UiRect rc, int enabled);

void Ui_SetFocusId(UiContext *ctx, UiId id);

/* 非文本控件的键盘导航焦点 (ButtonGroup). 与编辑框 focusId 分开, 避免 IME 冲突. */
void Ui_SetNavId(UiContext *ctx, UiId id);

/* 不可见的全窗输入 blocker: 设置后所有命中都返回 id, 但不会触发控件重绘.
 * 用于合成器 Toast (遮罩在独立的 DComp layer 上, 主窗不需要重画).
 * 传 UI_ID_NONE 解除. */
void Ui_SetModal(UiContext *ctx, UiId id);

/* 输入裁剪: 与 Gfx_PushClip 配套使用, 常与 Gfx_PushClip/PopClip 成对.
 * 只影响命中/注册矩形, 不影响绘制. 可嵌套 (最多 UI_MAX_CLIP_DEPTH).
 * 返回 1 = 已压栈 (必须对应 Pop); 0 = 栈满, 未改变状态. */
int  Ui_PushClipRect(UiContext *ctx, UiRect rc);
void Ui_PopClipRect(UiContext *ctx);

/* 转场输入锁: 压栈期间 Ui_State 不产生 hover/press, 新控件不注册命中,
 * 滚轮目标也不登记. 只影响输入, 不影响绘制. */
void Ui_PushInputBlock(UiContext *ctx);
void Ui_PopInputBlock(UiContext *ctx);

/* 滚轮区域: 每帧由可滚动容器登记; Ui_OnMouseWheel 选择最后登记且命中的
 * 目标, 把累计 delta 放入 wheelTarget/wheelDelta, 由容器在 Begin 中消费. */
void Ui_RegisterWheelTarget(UiContext *ctx, UiId id, UiRect rc);
void Ui_OnMouseWheel(UiContext *ctx, float x, float y, float delta);

void Ui_OnMouseMove(UiContext *ctx, float x, float y);
void Ui_OnMouseDown(UiContext *ctx, float x, float y);
void Ui_OnMouseUp(UiContext *ctx, float x, float y);
void Ui_OnMouseLeave(UiContext *ctx);
void Ui_OnCaptureChanged(UiContext *ctx);

#endif /* SCV2_UI_CONTEXT_H */
