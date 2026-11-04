/*
 * ui_types.h - UI 基础类型与 ID/矩形工具
 */
#ifndef SCV2_UI_TYPES_H
#define SCV2_UI_TYPES_H

#include <stdint.h>
#include "gfx/gfx.h"

typedef struct UiContext UiContext;
typedef struct UiTheme UiTheme;

typedef GfxRect UiRect;
typedef GfxColor UiColor;
typedef uint64_t UiId;

#define UI_ID_NONE 0

/* 一次组件调用的交互结果 */
typedef struct {
	int clicked;    /* 鼠标在同一控件内按下并抬起 */
	int changed;    /* 拖动中值变化（仅滑块/色板） */
	int committed;  /* 拖动结束，应提交值 */
	float value;    /* 主值（滑块 0..1；色板 hue 0..1） */
	float value2;   /* 次值（色板 saturation 0..1） */
} UiResult;

typedef struct {
	int hovered;
	int pressed;
	int active;    /* 正在拖动 */
	int focused;   /* 键盘焦点在此控件 */
	/* 平滑后的交互强度 (0..1): 控件用它们做颜色/光晕插值 */
	float hover;
	float press;
	float focus;
	float glow;
} UiWidgetState;

static __inline UiRect Ui_Rect(float x, float y, float w, float h)
{
	UiRect r; r.x = x; r.y = y; r.w = w; r.h = h; return r;
}

static __inline int Ui_RectContains(UiRect r, float x, float y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static __inline UiRect Ui_RectInset(UiRect r, float v)
{
	return Ui_Rect(r.x + v, r.y + v, r.w - v * 2.0f, r.h - v * 2.0f);
}

static __inline float Ui_Clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static __inline int Ui_Clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static __inline UiColor Ui_ColorShift(UiColor c, int delta)
{
	int a = (int)((c >> 24) & 0xFF);
	int r = Ui_Clampi((int)((c >> 16) & 0xFF) + delta, 0, 255);
	int g = Ui_Clampi((int)((c >> 8) & 0xFF) + delta, 0, 255);
	int b = Ui_Clampi((int)(c & 0xFF) + delta, 0, 255);
	return ((UiColor)a << 24) | ((UiColor)r << 16) | ((UiColor)g << 8) | (UiColor)b;
}

/* 颜色插值: t=0 -> a, t=1 -> b (含 alpha) */
static __inline UiColor Ui_ColorMix(UiColor a, UiColor b, float t)
{
	int ca, cr, cg, cb;
	if (t <= 0.0f) return a;
	if (t >= 1.0f) return b;
	ca = (int)((int)((a >> 24) & 0xFF) + ((int)((b >> 24) & 0xFF) - (int)((a >> 24) & 0xFF)) * t);
	cr = (int)((int)((a >> 16) & 0xFF) + ((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t);
	cg = (int)((int)((a >> 8) & 0xFF) + ((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t);
	cb = (int)((int)(a & 0xFF) + ((int)(b & 0xFF) - (int)(a & 0xFF)) * t);
	return ((UiColor)Ui_Clampi(ca, 0, 255) << 24) | ((UiColor)Ui_Clampi(cr, 0, 255) << 16)
		| ((UiColor)Ui_Clampi(cg, 0, 255) << 8) | (UiColor)Ui_Clampi(cb, 0, 255);
}

static __inline UiColor Ui_ColorAlpha(UiColor c, int a)
{
	return (c & 0x00FFFFFF) | ((UiColor)Ui_Clampi(a, 0, 255) << 24);
}

static __inline UiColor Ui_ColorRGB(int r, int g, int b)
{
	return 0xFF000000u | ((UiColor)Ui_Clampi(r, 0, 255) << 16)
		| ((UiColor)Ui_Clampi(g, 0, 255) << 8) | (UiColor)Ui_Clampi(b, 0, 255);
}

/* 稳定 ID: 字符串作用域 + 两个整数（通常 dev/ent/param）。同帧内必须唯一。 */
static __inline UiId Ui_Id(const char *scope, int a, int b)
{
	uint64_t h = 1469598103934665603ULL;
	while (*scope) {
		h ^= (unsigned char)*scope++;
		h *= 1099511628211ULL;
	}
	h ^= (uint64_t)(uint32_t)a * 0x9E3779B97F4A7C15ULL;
	h *= 1099511628211ULL;
	h ^= (uint64_t)(uint32_t)b * 0xC2B2AE3D27D4EB4FULL;
	h *= 1099511628211ULL;
	return h ? h : 1;
}

static __inline UiId Ui_IdIndex(const char *scope, int a, int b, int idx)
{
	uint64_t h = Ui_Id(scope, a, b);
	h ^= (uint64_t)(uint32_t)idx * 0xD6E8FEB86659FD93ULL;
	h *= 1099511628211ULL;
	return h ? h : 1;
}

#endif /* SCV2_UI_TYPES_H */
