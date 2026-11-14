/*
 * gfx_rich.h - 富文本排版/绘制（DirectWrite 范围样式）
 *
 * 独立可选模块: 只有使用它的应用才需要编译/链接 gfx_rich.cpp。
 * gfx 核心不依赖本文件；依赖方向为 gfx_rich -> gfx。
 *
 * 语义:
 *  - 一个 GfxRichText = 一个 IDWriteTextLayout，负责换行/断词/CJK/bidi;
 *  - 所有字符范围 (start/len) 都是 UTF-16 码元，相对创建时传入的文本；
 *  - 颜色范围绑定 gfx 画刷缓存里的实色画刷；设备代际变化后应销毁重建；
 *  - 所有 API 只在 GUI 线程调用。
 *
 * 设计见 docs/CUI-Markdown-设计方案.md §4
 */
#ifndef CUI_GFX_RICH_H
#define CUI_GFX_RICH_H

#include "gfx/gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GfxRichText GfxRichText;

#define GFX_RICH_WRAP (1u << 0)   /* 置位 = 软换行 (Markdown 恒开) */

/* lineSpacing: 行高倍数; 0 = 使用字体自然行高。wrapW <= 0 时为 1。
 * 返回 NULL = 参数非法/内存不足; 失败不崩溃。 */
GfxRichText *Gfx_RichCreate(Gfx *g, const wchar_t *text16, int len16,
	GfxFont *base, float wrapW, float lineSpacing, uint32_t flags);
void Gfx_RichDestroy(GfxRichText *rt);

/* 范围样式。start/len 超界会被裁剪; len<=0 为 no-op。 */
void Gfx_RichColor(GfxRichText *rt, int start, int len, GfxColor color);
void Gfx_RichBold(GfxRichText *rt, int start, int len, int on);
void Gfx_RichItalic(GfxRichText *rt, int start, int len, int on);
void Gfx_RichUnderline(GfxRichText *rt, int start, int len, int on);
void Gfx_RichStrike(GfxRichText *rt, int start, int len, int on);

/* 把 [start, len) 的字体家族切换为 font 的家族 (字号/字重/斜体不变)。
 * 用于行内代码等需要等宽字体的范围。返回 1 成功。 */
int Gfx_RichFontFamily(GfxRichText *rt, int start, int len, GfxFont *font);

/* 测量: 高度含换行; 宽度为最宽行 (含行尾空白)。 */
float Gfx_RichWidth(const GfxRichText *rt);
float Gfx_RichHeight(const GfxRichText *rt);

/* 绘制: (x,y) 是布局左上角 (DIP)。color 是无范围颜色时的默认色。
 * 必须在一帧内 (Gfx_Begin 之后); 脏区外自动跳过。 */
void Gfx_RichDraw(GfxRichText *rt, Gfx *g, float x, float y, GfxColor color);

/* 布局局部坐标 -> UTF-16 位置。返回 1 = 点在布局矩形内, 0 = 未命中。
 * 命中后由调用方按 run/链接区间再过滤。 */
int Gfx_RichHitTest(const GfxRichText *rt, float x, float y, int *pos16);

/* 字符范围 -> 矩形数组 (换行时可能多段, 相对布局原点)。
 * 返回实际写入个数 (<= cap)。 */
int Gfx_RichRangeRects(const GfxRichText *rt, int start, int len,
	GfxRect *out, int cap);

/* 用行内对象替换 [start, len) 的文本: 参与断行/行高, 绘制时画 bmp
 * 到该范围矩形 (w/h 为 DIP)。bmp 必须比布局活得久; 同一范围重复设置
 * 以最后一次为准。返回 1 成功。 */
int Gfx_RichInlineImage(GfxRichText *rt, int start, int len,
	GfxBitmap *bmp, float w, float h);

/* 自定义行内对象 (矢量公式等): 占位 w×(h) 参与断行/行高, baseline 为
 * 顶部到基线距离; 绘制时回调收到范围矩形左端与基线 y。回调必须在一帧
 * 内 (Gfx_RichDraw 内) 同步完成绘制, user 由调用方保证生命周期。 */
typedef void (*GfxRichCustomDrawFn)(void *user, Gfx *g, float x,
	float baseline, float w, float h, GfxColor color);

int Gfx_RichInlineCustom(GfxRichText *rt, int start, int len, float w, float h,
	float baseline, GfxRichCustomDrawFn draw, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CUI_GFX_RICH_H */
