/*
 * ui_anim.h - 轻量动画内核: 按控件 ID 存"通道"标量, 两种运动模型
 *
 *   1) 指数平滑 (SMOOTH): 交互态 (hover/press/focus/glow), 天然可中途反向
 *      v += (target - v) * dt / (tau + dt)      —— 不用 exp(), 帧率无关
 *   2) 显式补间 (TWEEN): 转场 (淡入/上滑), from -> to 在固定时长内按缓动走完
 *
 * 约定:
 *   - 只在 UI 线程访问; 固定池, 零分配;
 *   - 时间由 Ui_AnimAdvance(dt) 推进 (QPC 得来的毫秒);
 *   - 控件不持有动画状态, 只按 UiId + 通道读取;
 *   - "显示动画"关闭时 (reduced=1) 所有通道立即到位.
 */
#ifndef SCV2_UI_ANIM_H
#define SCV2_UI_ANIM_H

#include "ui/ui_types.h"

typedef struct UiContext UiContext;

/* 语义通道: 内置控件共用; 8 以后留给控件自定义 (进度条/自绘等) */
enum {
	UI_AN_HOVER = 0,
	UI_AN_PRESS,
	UI_AN_FOCUS,
	UI_AN_GLOW,
	UI_AN_ALPHA,      /* 0..1 透明度 (转场/淡入) */
	UI_AN_OFFSET,     /* DIP 位移 (状态文字上滑) */
	UI_AN_PROGRESS,   /* 0..1 进度 */
	UI_AN_USER = 8,
	UI_AN_CH = 16
};

#define UI_AN_MAX_ENTRIES 64

enum { UI_AN_IDLE = 0, UI_AN_SMOOTH, UI_AN_TWEEN };
enum { UI_AN_EASE_LINEAR = 0, UI_AN_EASE_OUT, UI_AN_EASE_INOUT };

typedef struct {
	float    cur, target;   /* SMOOTH */
	float    from, to;      /* TWEEN */
	uint32_t t0, dur;       /* TWEEN: 起点 ms / 时长 ms */
	uint8_t  mode, ease;
	uint8_t  inited;        /* 有控件写过这个通道? 没写过时 Get 返回默认值 */
} UiAnimChan;

typedef struct {
	UiId       id;
	UiAnimChan ch[UI_AN_CH];
	UiRect     rect;        /* 最近一次绘制位置 (DIP), 动画帧脏区用 */
	uint32_t   seen;        /* 最近一次被访问的帧号 (GC 用) */
	uint8_t    used;
	uint8_t    hasRect;
} UiAnimEntry;

void  Ui_AnimSetReduced(UiContext *ctx, int reduced);

/* 直接设值 (无动画); 用于初始化或重置 */
void  Ui_AnimSet(UiContext *ctx, UiId id, int ch, float v);

/* 平滑到 target (tau 由通道默认值决定) */
void  Ui_AnimSmooth(UiContext *ctx, UiId id, int ch, float target);

/* 从当前值补间到 target; ease 见 UI_AN_EASE_* */
void  Ui_AnimTo(UiContext *ctx, UiId id, int ch, float target, int ms, int ease);

/* 读当前值; 条目不存在时返回 def (并建立初始值) */
float Ui_AnimGet(UiContext *ctx, UiId id, int ch, float def);

/* 每帧推进一次 (dtMs 会被夹到 0..100) */
void  Ui_AnimAdvance(UiContext *ctx, float dtMs);

/* 记录控件绘制矩形 (每帧绘制时调用, 供动画帧局部失效) */
void  Ui_AnimSetRect(UiContext *ctx, UiId id, UiRect rc);

/* 取走"本帧动画改动的区域": 0=无变化, 1=out 有矩形, 2=需整窗 (如窗口淡入) */
int   Ui_AnimTakeDirty(UiContext *ctx, UiRect *out);
/* 组件逐帧自绘 (如 loading 动画) 时登记脏区; 由动画循环统一失效, 避免在
 * WM_PAINT 内 InvalidateRect 造成消息循环无法排空。 */
void  Ui_AnimDirtyRect(UiContext *ctx, UiRect r);

/* 还有通道在动? (驱动帧时钟) */
int   Ui_AnimBusy(UiContext *ctx);

#endif /* SCV2_UI_ANIM_H */
