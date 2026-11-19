/*
 * gfx_fx.h - 状态变化的合成器过渡（SnapshotFx）
 *
 * 语义: 调用方为 DIP 矩形登记一次“视觉状态变化”。gfx 立即抓旧快照,
 * 下一帧结束时抓新快照, 两张快照作为两个子 visual 交给 DComp,
 * 用 IDCompositionAnimation 交叉淡化; 动画期间 UI 线程不再重绘。
 *
 * 只在 DComp 路径可用; ULW 回退时 Gfx_FxEnabled 返回 0。
 */
#ifndef CUI_GFX_FX_H
#define CUI_GFX_FX_H

#include "gfx/gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 = 本窗口支持合成器过渡 */
int  Gfx_FxEnabled(Gfx *g);

/* 登记一次变化 (DIP)。durMs<=0 或不可用时返回 0。
 * 曲线固定为 cubic-out: 新内容可见度 1-(1-p)^3, 对齐 CPU 通道 EASE_OUT。 */
int  Gfx_FxQueue(Gfx *g, GfxRect rc, int durMs);

/* 撤销进行中的过渡 (resize/DPI/滚动/主题切换/隐藏窗口) */
void Gfx_FxCancelAll(Gfx *g);
/* 只撤销与矩形相交的过渡 (滚动区域内容移动时用) */
void Gfx_FxCancelRect(Gfx *g, GfxRect rc);

/* 还有过渡在跑或待提交? */
int  Gfx_FxBusy(Gfx *g);

/* 距最早结束还有多少毫秒; 无过渡返回 -1 */
int  Gfx_FxIdleMs(Gfx *g);

/* 回收已结束的过渡 (不绘制) */
void Gfx_FxTick(Gfx *g);

/* ── PartFx: 位移/旋转部件 ───────────────────────────────────────────
 * 把会移动的元素 (Switch 滑块、Tab 指示器) 渲染到独立 visual,
 * 位置用 IDCompositionAnimation 补间; 主表面绘制时跳过该部件。 */
typedef struct GfxPart GfxPart;
typedef void (*GfxPartDrawFn)(Gfx *g, void *user);

GfxPart *Gfx_PartCreate(Gfx *g, float dipW, float dipH,
	GfxPartDrawFn fn, void *user);
/* 部件挂在 layer 的 visual 下: 继承 layer 的 opacity/位置, 用来在被
 * 合成器淡入淡出的表面上叠加可独立旋转的元素 (Loading spinner). */
GfxPart *Gfx_PartCreateOn(GfxLayer *l, float dipW, float dipH,
	GfxPartDrawFn fn, void *user);
/* 内容需要重画 (主题/尺寸变化) */
void Gfx_PartUpdate(GfxPart *p);
/* 每帧声明静态位置 (DIP); 补间期间只更新 y, 结束后落到 baseX */
void Gfx_PartPlace(GfxPart *p, float xDip, float yDip);
/* 隐藏/显示 (被弹层/对话框遮挡时用) */
void Gfx_PartSetVisible(GfxPart *p, int visible);
/* 从当前呈现位置补间到新 x (DIP); 可被再次调用打断 */
void Gfx_PartAnimateX(GfxPart *p, float toXDip, int durMs);
/* 当前呈现 x (CPU 模型, 用于打断) */
float Gfx_PartX(const GfxPart *p);
/* 可动画宽度: 内容按最大宽度渲染, 用带圆角的 DComp clip 裁到可见宽度;
 * x / 宽度同时补间, 圆角在任意宽度下都正确 (Tab 指示器)。 */
void  Gfx_PartSetClipRadius(GfxPart *p, float radiusDip);
void  Gfx_PartPlaceRect(GfxPart *p, float xDip, float yDip, float wDip);
void  Gfx_PartAnimateRect(GfxPart *p, float xDip, float yDip, float wDip,
	int durMs);
float Gfx_PartW(const GfxPart *p);
/* 旋转 (chevron): 合成器 rotate transform, 角度单位度 */
void  Gfx_PartSetRotation(GfxPart *p, float deg);
void  Gfx_PartAnimateRotation(GfxPart *p, float toDeg, int durMs);
/* 连续匀速旋转 (Loading spinner): 线性合成器动画, 每 10 分钟重锚
 * 限制角度浮点误差; 同转速重复调用是空操作, 过期时从当前相位续接. */
void  Gfx_PartSpin(GfxPart *p, float degPerSec);
void  Gfx_PartSpinStop(GfxPart *p);
float Gfx_PartRotation(const GfxPart *p);
void Gfx_PartDestroy(GfxPart *p);

#ifdef __cplusplus
}
#endif

#endif /* CUI_GFX_FX_H */
