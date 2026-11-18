/*
 * gfx_visual.h - 外部 DComp 子视觉宿主接口 (C, 可选模块用)
 *
 * 只暴露把外部内容 (如 WebView2 CompositionController) 挂到 CUI
 * 根视觉所需的最小接口, 不引入 D2D/DWrite 头文件.
 */
#ifndef CUI_GFX_VISUAL_H
#define CUI_GFX_VISUAL_H

#include <windows.h>
#include "gfx/gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

HWND Gfx_IntHwnd(Gfx *g);
float Gfx_IntScale(Gfx *g);
int  Gfx_IntDcomp(Gfx *g);
/* 渲染后端决策: 0 = 尚未决定 (首帧前), 1 = DComp, 2 = ULW. */
int  Gfx_IntRenderMode(Gfx *g);

/* rc 为 DIP; top!=0 时挂在根视觉最上层. 返回 IDCompositionVisual*. */
void *Gfx_IntCreateChildVisual(Gfx *g, GfxRect rc, int top);
void Gfx_IntUpdateChildVisual(Gfx *g, void *visual, GfxRect rc, int visible);
void Gfx_IntDestroyChildVisual(Gfx *g, void *visual);
void Gfx_IntCommit(Gfx *g);

#ifdef __cplusplus
}
#endif

#endif /* CUI_GFX_VISUAL_H */
