/*
 * ui_loading.h - 父容器级加载遮罩
 *
 * 半透明遮罩 + 合成器旋转的圆环弧 270° + 单行文字 + 可选圆形 X 取消。
 * 默认拦截父容器内全部输入; passThrough=1 时只画不拦。
 * 取消只产生请求 (UiLoading_TakeCancel), 显示/隐藏始终由程序控制。
 *
 * 合成器路径: 遮罩/文字/按钮画在父容器大小的 GfxLayer 上 (只画一次),
 * spinner 是 layer 的子 GfxPart, 由 DComp 匀速旋转; 稳定期主窗零重绘。
 * ULW / layer 创建失败时退回主窗绘制 (spinner 角度按帧推进)。
 *
 * 用法:
 *   任务开始: UiLoading_Show()
 *   每拍:     if (UiLoading_Tick(ld, UiFrame_Now())) UiApp_Invalidate(app);
 *   按键:     if (UiLoading_Key(ld, vk)) return 1;
 *   帧内, 页面内容之后: UiLoading_Draw(ld, parentRc, UiFrame_Now());
 *   Gfx_End 之后:       UiLoading_EndFrame(ld, UiFrame_Now());
 *   TakeCancel() 后: 停任务并 UiLoading_Hide()。
 */
#ifndef CUI_UI_LOADING_H
#define CUI_UI_LOADING_H

#include "ui/ui_context.h"
#include "gfx/gfx_fx.h"

#define UI_LOADING_FADE_MS 160

typedef enum {
	UI_LOADING_HIDDEN = 0,
	UI_LOADING_PENDING,   /* Show 后等待 delayMs */
	UI_LOADING_SHOWN,
	UI_LOADING_HIDING
} UiLoadingPhase;

typedef struct UiLoading {
	UiContext *ui;
	int slot;                 /* 实例序号: 区分命中 id */
	char text[96];            /* UTF-8, 单行 */
	int cancelable;           /* 1 = 圆形 X + Esc 可取消 */
	int passThrough;          /* 1 = 不拦截输入 */
	int delayMs, minShowMs;

	UiLoadingPhase phase;
	double showAt, shownAt, hideAt;
	int hidePending;          /* Hide 已请求, 等 minShowMs 到期 */
	int pendingRedraw;        /* 状态刚变化, 下一拍需要主窗重绘 */
	int shownDrawn;           /* 本轮 SHOWN 是否已做过首次命中清理 */
	int cancelReq;            /* 已发起取消请求, 等待 TakeCancel */

	UiRect rc;                /* 最近一次父容器矩形 (DIP) */
	float radius;             /* 遮罩圆角 */

	float btnHover, btnPress;      /* 本帧取消按钮交互强度 */
	float drawnHover, drawnPress;  /* layer 内容最后一次画进去的强度 */
	int contentRev, drawnRev;      /* 文本/按钮/圆角变更序号 */

	UiId idBlock, idCancel;   /* Init 时算好, 避免逐帧哈希字符串 */

	GfxLayer *layer;
	GfxPart *spin;
	int layerW, layerH;
	float layerX, layerY;
	int layerDirty, layerIn, layerOut, fallback;
	uint32_t themeRev;
} UiLoading;

void UiLoading_Init(UiLoading *ld, UiContext *ui);
void UiLoading_Shutdown(UiLoading *ld);

void UiLoading_SetText(UiLoading *ld, const char *utf8);
void UiLoading_SetCancelable(UiLoading *ld, int on);
void UiLoading_SetPassThrough(UiLoading *ld, int on);
void UiLoading_SetTiming(UiLoading *ld, int delayMs, int minShowMs);
void UiLoading_SetRadius(UiLoading *ld, float radiusDip);

void UiLoading_Show(UiLoading *ld);
void UiLoading_Hide(UiLoading *ld);
int  UiLoading_Visible(const UiLoading *ld);   /* PENDING/SHOWN/HIDING */
int  UiLoading_Shown(const UiLoading *ld);     /* SHOWN/HIDING: 可据此门控按键 */
int  UiLoading_UsingLayer(const UiLoading *ld);

/* 一次性取走取消请求; 组件不会自行隐藏. */
int  UiLoading_TakeCancel(UiLoading *ld);
/* Esc 且 cancelable 时登记请求并返回 1 (已消费). */
int  UiLoading_Key(UiLoading *ld, int vk);

/* 每拍调用; 1 = 本拍需要主窗重绘. */
int  UiLoading_Tick(UiLoading *ld, double nowMs);
/* 帧内、页面内容之后调用: 登记拦截/取消按钮命中区, 回退模式画全部内容. */
void UiLoading_Draw(UiLoading *ld, UiRect parentRc, double nowMs);
/* Gfx_End 之后调用: layer/part 同步、淡入淡出与销毁. */
void UiLoading_EndFrame(UiLoading *ld, double nowMs);

#endif /* CUI_UI_LOADING_H */
