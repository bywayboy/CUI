/*
 * md_math.h - Markdown 数学公式: LaTeX 子集解析 + 盒子布局 (纯层, 0CRT)
 *
 * 只负责 源码 -> 盒子树 (横向/纵向/文字/规则线) 与几何; 不依赖 gfx/window,
 * 可 headless 测试. 度量与绘制都由调用方注入:
 *   - MdMathMeasureFn 提供字形宽/高/深 (实际用 DirectWrite, 测试用固定值)
 *   - MdMathDraw 提供最终绘制 (实际用 D2D, 测试记录回调序列)
 *
 * 语法 (v1): $...$ / \(...\) 行内, $$...$$ / \[...\] 块级;
 * \frac \sqrt ^ _ \left \right \text, 希腊字母/运算符/间距,
 * matrix/pmatrix/bmatrix/vmatrix/aligned/cases 环境.
 */
#ifndef CUI_MD_MATH_H
#define CUI_MD_MATH_H

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 排版层级: 决定字号比例 (display 1.0 / text 1.0 / script 0.7 / 0.5) */
enum {
	MD_MATH_DISPLAY = 0,
	MD_MATH_TEXT,
	MD_MATH_SCRIPT,
	MD_MATH_SCRIPTSCRIPT
};

/* 文字盒标志 */
enum {
	MD_MATHF_ITALIC = 1u << 0,   /* 数学斜体 (变量/小写希腊) */
	MD_MATHF_TEXT   = 1u << 1    /* \text{}: 正体、保留空格 */
};

typedef struct MdMath MdMath;

/* 测量一段文字: 返回 advance 宽度 (可含右侧墨迹溢出), 输出基线以上/以下
 * 的字形墨迹高度/深度. 失败返回负值. */
typedef float (*MdMathMeasureFn)(void *user, const wchar_t *s, int len,
	float size, uint32_t flags, float *height, float *depth);

/* 绘制回调: 坐标均为相对公式原点 (x, baseline), y 向下为正 (屏幕约定).
 * 文字传基线 y 与高度; 规则线传基线上下厚度. */
typedef struct MdMathDraw {
	void *user;
	void (*text)(void *user, const wchar_t *s, int len, float x,
		float baseline, float size, uint32_t flags, float height);
	void (*rule)(void *user, float x, float baseline, float w,
		float above, float below);
} MdMathDraw;

/* 解析+布局. size 为基准字号 (DIP). display != 0 时按行间公式排版:
 * \sum/\prod 等大运算符放大, 上下限排在正上方/正下方; 行内公式传 0.
 * 失败 (语法错误/复杂度超限/度量失败) 返回 NULL, 调用方应回退显示源码. */
MdMath *MdMath_Layout(const char *utf8, int len, float size, int display,
	MdMathMeasureFn measure, void *measureUser);
void MdMath_Free(MdMath *m);

float MdMath_Width(const MdMath *m);
float MdMath_Height(const MdMath *m);   /* 基线以上 */
float MdMath_Depth(const MdMath *m);    /* 基线以下 */

/* 以 (x, baseline) 为原点遍历盒子树 */
void MdMath_Draw(const MdMath *m, float x, float baseline,
	const MdMathDraw *draw);

#ifdef __cplusplus
}
#endif

#endif /* CUI_MD_MATH_H */
