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

/* 光标形状: 控件绘制时用 Ui_SetCursor 声明, 由 app 在 WM_SETCURSOR /
 * 帧末应用. 不在列表里 = 箭头. */
enum {
	UI_CURSOR_ARROW = 0,
	UI_CURSOR_IBEAM,
	UI_CURSOR_HAND,
	UI_CURSOR_SIZE_NS,
	UI_CURSOR_SIZE_WE
};

/* 控件内悬停判定: 指针在此控件上移动时调用 (WM_MOUSEMOVE 内同步执行).
 * 可更新内部 hover 状态并用 Ui_SetCursor 声明光标. 返回 1 = 需要重绘;
 * 同时通过 dirty 给出局部区域 (w/h<=0 表示整个控件), 避免整控件失效. */
typedef int (*UiHoverFn)(void *user, UiContext *ctx, float x, float y,
	UiRect *dirty);

typedef struct {
	UiId id;
	UiRect rect;
	UiRect clip;      /* 注册时的输入裁剪; w<=0 = 无裁剪 */
	int layer;
	int z;
	uint8_t cursor;   /* UI_CURSOR_*: 命中此控件时的鼠标形状 */
	uint8_t pressPoll; /* 1 = 按下期间指针移动需逐次重绘 (滑块/拖选等) */
	UiHoverFn hoverFn; /* 非 NULL: 指针在控件上移动时调用 */
	void *hoverUser;
	UiId scope;
} UiWidget;

typedef struct {
	UiId id;
	UiRect rect;
	UiRect clip;
	int layer;        /* 弹层打开时只接受 overlay 滚轮目标 */
	UiId scope;
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
	UiId inputOwner;          /* 持续的模态输入 owner; 0 = 无 */
	UiId inputScope;          /* 当前注册作用域; 帧开始/结束清零 */

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
/* 组件主动失效一个 DIP 矩形 (异步加载完成/逐帧动画时调用) */
void Ui_ContextInvalidate(UiContext *ctx, UiRect rc);

void Ui_BeginFrame(UiContext *ctx);
void Ui_EndFrame(UiContext *ctx);

int Ui_InputAllowed(const UiContext *ctx);
void Ui_RegisterWidget(UiContext *ctx, UiId id, UiRect rc);
UiId Ui_HitTest(const UiContext *ctx, float x, float y);

/* 给本帧已注册的控件声明光标形状 (同一 ID 的多次注册一起生效, 例如
 * TextArea 的 surface + ScrollView viewport). 松开/不声明时回到箭头. */
void Ui_SetCursor(UiContext *ctx, UiId id, int kind);
/* 按下期间需要跟随指针的控件 (滑块/拖选): 每次移动失效自身. */
void Ui_SetPressPoll(UiContext *ctx, UiId id, int on);
/* 局部失效 (DIP 客户区坐标): 悬停/内容小范围变化时避免整控件重绘. */
void Ui_InvalidateRect(UiContext *ctx, UiRect rc);
/* 注册控件内悬停判定 (见 UiHoverFn). 控件内部有链接等精细命中区时,
 * 在鼠标移动事件中同步判断光标/悬停, 只在变化时改光标并请求重绘. */
void Ui_SetHoverFn(UiContext *ctx, UiId id, UiHoverFn fn, void *user);

/* 命中处应显示的 UI_CURSOR_*; 无控件/模态遮罩时返回箭头. */
int  Ui_CursorAt(const UiContext *ctx, float x, float y);

/* UI_CURSOR_* -> 系统光标句柄 (不缓存, 交给 user32 复用). */
HCURSOR Ui_CursorHandle(int kind);

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
