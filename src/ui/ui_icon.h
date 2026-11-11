/*
 * ui_icon.h - 图标后端 (当前: 字体 glyph)
 *
 * GLYPH: 使用 Segoe MDL2 Assets 等图标字体的 PUA 码点, 用文字色直接 tint,
 *        支持主题/禁用/DPI; 绘制就是一次 Gfx_Text.
 *
 * VECTOR/SVG 后端后续在同一 UiIcon 上扩展; 目前 kind 只接受 GLYPH.
 */
#ifndef CUI_UI_ICON_H
#define CUI_UI_ICON_H

#include "ui/ui_context.h"

typedef enum {
	UI_ICON_GLYPH = 0
	/* UI_ICON_VECTOR, UI_ICON_RUNTIME_SVG: 预留 */
} UiIconKind;

typedef struct UiIcon {
	int kind;            /* UiIconKind */
	GfxFont *font;       /* GLYPH: 字体 */
	uint32_t codepoint;  /* GLYPH: Unicode 码点 (MDL2 通常在 E000..F8FF) */
	const void *data;    /* 预留: 矢量路径/SVG 文档 */
	float viewW, viewH;  /* 预留: viewBox */
} UiIcon;

static __inline UiIcon UiIcon_Glyph(GfxFont *font, uint32_t codepoint)
{
	UiIcon i;
	i.kind = UI_ICON_GLYPH;
	i.font = font;
	i.codepoint = codepoint;
	i.data = 0;
	i.viewW = i.viewH = 0.0f;
	return i;
}

/* tint=0 用 theme.text; enabled=0 用 btnDisabledText. */
void UiIcon_Draw(UiContext *ui, const UiIcon *icon, UiRect rc,
	UiColor tint, int enabled);

#endif /* CUI_UI_ICON_H */
