/*
 * gfx.h - 后端无关的 2D 绘制接口（组件只依赖本文件，不接触 D2D/DWrite）
 *
 * 坐标: DIP(float)。后端在 Begin 时按 DPI 设置缩放，组件无需关心物理像素。
 * 颜色: 0xAARRGGBB（直通 alpha，类似 CSS rgba）。
 * 文本: UTF-8 输入，后端负责转 UTF-16 与 DirectWrite 排版。
 */
#ifndef SCV2_GFX_H
#define SCV2_GFX_H

#include <windows.h>
#include <stdint.h>

typedef struct Gfx Gfx;
typedef struct GfxFont GfxFont;
typedef struct GfxBitmap GfxBitmap;
typedef struct GfxOverlay GfxOverlay;
typedef struct GfxLayer GfxLayer;

/* 通用 DComp 子 visual 内容绘制回调。回调里用 Gfx_* 画 (DIP 坐标),
 * 不要调用 Gfx_Begin/Gfx_End; 透明度由 Gfx_LayerSetOpacity 控制。 */
typedef void (*GfxLayerDrawFn)(Gfx *g, void *user);

typedef struct { float x, y, w, h; } GfxRect;
typedef uint32_t GfxColor;

enum {
	GFX_TEXT_CENTER   = 1u << 0,
	GFX_TEXT_RIGHT    = 1u << 1,
	GFX_TEXT_VCENTER  = 1u << 2,
	GFX_TEXT_ELLIPSIS = 1u << 3
};

#ifdef __cplusplus
extern "C" {
#endif

/* 生命周期 */
int  Gfx_Init(Gfx **out, HWND hwnd);
void Gfx_Shutdown(Gfx *g);
int  Gfx_Resize(Gfx *g, int pixelW, int pixelH, float scale);
int  Gfx_Generation(Gfx *g);   /* 每次重建绘制表面 +1，供位图缓存失效 */
int  Gfx_GradientCreates(Gfx *g);  /* 取走累计的渐变画刷新建次数 (诊断用) */

/* 帧 */
void Gfx_Begin(Gfx *g);
int  Gfx_End(Gfx *g);          /* 提交一帧；DComp 失败会自动回退 ULW，成功返回 1 */
void Gfx_Clear(Gfx *g, GfxColor c);

/* 全帧透明度 (淡入淡出): 压一个 D2D 图层, 影响其后所有绘制; 必须成对。
 * 返回 1 = 真的压了图层 (Pop 一次); 0 = 未压 (alpha≈1 时优化掉)。 */
int  Gfx_PushAlpha(Gfx *g, float alpha);
void Gfx_PopAlpha(Gfx *g);

/* 脏区: 物理像素; 在 Gfx_Begin 前调用。rc=NULL 或空 => 整帧重绘。
 * 未变化的像素保留上一帧内容；resize/设备恢复会自动强制整帧。 */
void Gfx_SetDirtyRect(Gfx *g, const RECT *rcPx);

/* 当前帧脏区是否与 r 相交。用于在脏区外跳过昂贵的文本排版/UTF-8 转换；
 * 未 Begin 或整帧重绘时返回 1。 */
int Gfx_IsVisible(Gfx *g, GfxRect r);

/* 形状 */
void Gfx_FillRect(Gfx *g, GfxRect r, GfxColor c);
void Gfx_FillRoundRect(Gfx *g, GfxRect r, float radius, GfxColor c);
void Gfx_FillRoundRectGradV(Gfx *g, GfxRect r, float radius, GfxColor top, GfxColor bottom);
void Gfx_FillRoundRectGradH(Gfx *g, GfxRect r, float radius, GfxColor left, GfxColor right);
void Gfx_FillRoundRectMultiGrad(Gfx *g, GfxRect r, float radius,
	const GfxColor *colors, const float *positions, int count, int horizontal);
void Gfx_FillEllipse(Gfx *g, float cx, float cy, float rx, float ry, GfxColor c);
void Gfx_FillEllipseGradV(Gfx *g, float cx, float cy, float r, GfxColor top, GfxColor bottom);
void Gfx_StrokeRoundRect(Gfx *g, GfxRect r, float radius, GfxColor c, float width);
void Gfx_StrokeEllipse(Gfx *g, float cx, float cy, float r, GfxColor c, float width);
void Gfx_Line(Gfx *g, float x1, float y1, float x2, float y2, GfxColor c, float width);
/* 圆弧: start/sweep 为角度，屏幕坐标系顺时针；圆头线帽 */
void Gfx_StrokeArc(Gfx *g, float cx, float cy, float r, float startDeg, float sweepDeg,
	GfxColor c, float width);
void Gfx_FillTriangle(Gfx *g, float x1, float y1, float x2, float y2, float x3, float y3,
	GfxColor c);

/* 裁剪（必须成对） */
void Gfx_PushClip(Gfx *g, GfxRect r);
void Gfx_PopClip(Gfx *g);

/* 文本 */
GfxFont *Gfx_CreateFont(Gfx *g, float sizeDip, int weight);
void Gfx_DestroyFont(Gfx *g, GfxFont *f);
float Gfx_TextWidth(Gfx *g, GfxFont *f, const char *utf8);
float Gfx_TextHeight(Gfx *g, GfxFont *f);
void Gfx_Text(Gfx *g, GfxFont *f, const char *utf8, GfxRect rc, GfxColor color, uint32_t flags);

/* 位图（BGRA，预乘 alpha；用于色板等静态图像） */
GfxBitmap *Gfx_CreateBitmap(Gfx *g, int w, int h, const uint8_t *bgra);
void Gfx_DestroyBitmap(Gfx *g, GfxBitmap *b);
void Gfx_DrawBitmap(Gfx *g, GfxBitmap *b, GfxRect dst);

/* 合成器动画 overlay: 一条静态渐变条画在独立的小 swapchain 上, 由 DComp
 * 子 visual 显示; 只有 reveal (0..1) 变化时才 Commit, 由 DWM 做补间。
 * 用于"静态窗口 + 一个持续动的进度条"这类场景: 主窗口不需要每帧 Present。
 * 仅 DComp 路径可用; ULW 回退返回 NULL, 应用应退回普通绘制。
 *
 * rect 是窗口客户区 DIP 坐标; radius/left/right 是条的圆角与左右渐变色。
 * 创建/改尺寸必须在 Gfx_End 之后调用 (不要在一帧中间)。 */
GfxOverlay *Gfx_CreateOverlay(Gfx *g, GfxRect rect, float radius,
	GfxColor left, GfxColor right);

/* 设置显示比例: p=0 全隐藏, p=1 全显示。
 * animateMs > 0 时由合成器在指定时长内线性补间; 0 立即到位。
 * 返回 1 成功, 0 失败/overlay 已失效。 */
int  Gfx_OverlaySetReveal(GfxOverlay *o, float p, int animateMs);

/* 更新位置/尺寸 (DIP)。返回 1=未变, 2=已重建, 0=失败。
 * 重建后应用需重新调用 Gfx_OverlaySetReveal 恢复当前比例。 */
int  Gfx_OverlaySetRect(GfxOverlay *o, GfxRect rect);

/* 整体透明度 0..1 (用于让 overlay 暂时让位给主窗内的模态遮罩, 动画继续跑). */
int  Gfx_OverlaySetOpacity(GfxOverlay *o, float opacity);

/* overlay 是否仍然可用 (DComp 设备存活)。运行期回退 ULW 后会变成 0,
 * 应用应销毁它并退回主窗绘制。 */
int  Gfx_OverlayAlive(GfxOverlay *o);

void Gfx_OverlayDestroy(GfxOverlay *o);

/* 通用合成器 layer: 独立 swapchain 的 DComp 子 visual, 默认加在所有
 * 已有 overlay/layer 之上; 内容用回调画一次, opacity 可交给 DWM 补间
 * (新系统用 IDCompositionVisual3, 老系统自动退回 effect group)。
 * 仅 DComp 路径可用; ULW 返回 NULL。创建/绘制/改尺寸必须在 Gfx_End 之后。
 * 主窗内的输入拦截由应用调用 Ui_SetModal 注册不可见全窗 blocker。 */
GfxLayer *Gfx_CreateLayer(Gfx *g, int pixelW, int pixelH);
int  Gfx_LayerDraw(GfxLayer *l, GfxLayerDrawFn fn, void *user);
int  Gfx_LayerSetOpacity(GfxLayer *l, float opacity, int animateMs);
int  Gfx_LayerResize(GfxLayer *l, int pixelW, int pixelH);
int  Gfx_LayerAlive(GfxLayer *l);
void Gfx_LayerDestroy(GfxLayer *l);

#ifdef __cplusplus
}
#endif

#endif /* SCV2_GFX_H */
