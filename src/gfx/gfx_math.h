/*
 * gfx_math.h - 数学公式矢量绘制 (可选模块)
 *
 * 依赖: md_math (盒子布局) + gfx 内部接口 (D2D/DWrite). gfx 核心不引用
 * 本模块; 只有使用公式的目标才链接 gfx_math.obj + md_math.obj.
 *
 * 每个 TEXT 盒子缓存一个 IDWriteTextLayout (按文本/字号/样式), 滚动重绘
 * 不重建布局; 规则线与字形都以 DIP 坐标直接绘制, 任意 DPI 下清晰.
 */
#ifndef CUI_GFX_MATH_H
#define CUI_GFX_MATH_H

#include "gfx/gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GfxMath GfxMath;

/* utf8/len: 公式 LaTeX 子集源码; sizeDip: 基准字号;
 * textFont: \text{} 使用的字体 (可为 NULL, 回退 Cambria Math);
 * display: 1 = 行间公式 (大运算符放大 + 上下限正排), 0 = 行内.
 * 语法错误返回 NULL, 调用方回退显示原始源码. */
GfxMath *Gfx_MathCreate(Gfx *g, const char *utf8, int len, float sizeDip,
	GfxFont *textFont, int display);
void Gfx_MathDestroy(GfxMath *m);

float Gfx_MathWidth(const GfxMath *m);
float Gfx_MathHeight(const GfxMath *m);   /* 基线以上 */
float Gfx_MathDepth(const GfxMath *m);    /* 基线以下 */

/* 在 (x, baseline) 绘制; 必须在一帧内 (Gfx_Begin 之后). */
void Gfx_MathDraw(GfxMath *m, Gfx *g, float x, float baseline, GfxColor color);

#ifdef __cplusplus
}
#endif

#endif /* CUI_GFX_MATH_H */
