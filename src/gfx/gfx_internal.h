/*
 * gfx_internal.h - gfx 核心与可选模块之间的最小内部接口（C++ only）
 *
 * 只被 gfx_d2d.cpp 与可选模块（当前: gfx_rich.cpp）包含，不是公共 API。
 * 依赖方向固定为 可选模块 -> gfx 核心；gfx_d2d.cpp 不引用任何 Gfx_Rich*。
 *
 * 访问器只暴露绘制所需的设备对象，不暴露 Gfx 的布局/字段，避免可选模块
 * 与核心内部结构耦合。
 */
#ifndef CUI_GFX_INTERNAL_H
#define CUI_GFX_INTERNAL_H

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include "gfx/gfx.h"
#include "gfx/gfx_visual.h"

/* 绘制表面是否就绪 (首帧前/设备丢失时为 0) */
int Gfx_IntReady(Gfx *g);
HWND Gfx_IntHwnd(Gfx *g);
float Gfx_IntScale(Gfx *g);

/* DirectWrite 工厂 / 当前 D2D 渲染目标 (借用, 不要 Release) */
IDWriteFactory *Gfx_IntDWrite(Gfx *g);
ID2D1RenderTarget *Gfx_IntRT(Gfx *g);

/* 取/建核心画刷缓存里的实色画刷 (借用, 不要 Release) */
ID2D1SolidColorBrush *Gfx_IntBrush(Gfx *g, GfxColor c);

/* 脏区可见性判断 (与 Gfx_IsVisible 一致, 避免跨模块重复实现) */
int Gfx_IntVisible(Gfx *g, GfxRect r);

/* 画位图 (和 Gfx_DrawBitmap 一致, 供富文本行内图片使用) */
void Gfx_IntDrawBitmap(Gfx *g, GfxBitmap *b, GfxRect dst);

/* GfxFont 的最小只读属性 (借用; 字体必须比调用方活得久) */
IDWriteTextFormat *Gfx_IntFontFormat(GfxFont *f);
float Gfx_IntFontSize(GfxFont *f);
int Gfx_IntFontWeight(GfxFont *f);   /* 原始字重; >=600 视为粗体 */

/* 外部合成 Visual 宿主见 gfx_visual.h (C 兼容). */

#endif /* CUI_GFX_INTERNAL_H */
