/*
 * md_math_test.c - 数学公式解析/盒子布局 headless 金样断言 (0CRT)
 *
 * 度量用固定字形宽高, 只验证结构/几何; 绘制用回调记录序列.
 * 构建: bin\cui_md_math_test.exe; 退出码 0 = 全通过.
 */
#include <windows.h>
#include <wchar.h>
#include "ui/md_math.h"
#include "log.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);

static int g_pass, g_fail;

static void out_(const char *s)
{
	DWORD n = 0;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &n, NULL);
}

#define CHECK(c) do { \
	if (c) g_pass++; \
	else { \
		char b_[320]; \
		g_fail++; \
		sprintf(b_, "FAIL line %d: %s\n", __LINE__, #c); \
		out_(b_); \
	} \
} while (0)

/* 固定度量: 拉丁 0.55em, CJK/符号 1em, 高 0.7em, 深 0.2em */
static float fake_measure(void *user, const wchar_t *s, int len, float size,
	uint32_t flags, float *h, float *d)
{
	float w = 0.0f;
	int i;
	(void)user;
	(void)flags;
	for (i = 0; i < len; i++)
		w += (s[i] >= 0x2E80 ? 1.0f : 0.55f) * size;
	*h = size * 0.7f;
	*d = size * 0.2f;
	return w;
}

/* 括号墨迹更高: 验证定界符组按墨迹中心对齐, 而不是基线硬贴 */
static float fake_measure_delim(void *user, const wchar_t *s, int len,
	float size, uint32_t flags, float *h, float *d)
{
	(void)user;
	if (len == 1 && (s[0] == L'(' || s[0] == L')')) {
		*h = size * 0.9f;
		*d = size * 0.3f;
		return size * 0.4f;
	}
	return fake_measure(NULL, s, len, size, flags, h, d);
}

typedef struct Cap {
	int texts, rules;
	int n;
	wchar_t ch[256];
	float x[256];
	float base[256];
	float size[256];
	uint32_t flags[256];
	float ruleW;
} Cap;

static void cap_text(void *user, const wchar_t *s, int len, float x,
	float baseline, float size, uint32_t flags, float height)
{
	Cap *c = (Cap *)user;
	int i;
	(void)height;
	c->texts++;
	for (i = 0; i < len && c->n < 256; i++) {
		c->ch[c->n] = s[i];
		c->x[c->n] = x;
		c->base[c->n] = baseline;
		c->size[c->n] = size;
		c->flags[c->n] = flags;
		c->n++;
	}
}

static void cap_rule(void *user, float x, float baseline, float w,
	float above, float below)
{
	Cap *c = (Cap *)user;
	(void)x;
	(void)baseline;
	(void)above;
	(void)below;
	c->rules++;
	c->ruleW = w;
}

static int cap_has(Cap *c, wchar_t ch)
{
	int i;
	for (i = 0; i < c->n; i++)
		if (c->ch[i] == ch) return 1;
	return 0;
}

static float cap_base_of(Cap *c, wchar_t ch)
{
	int i;
	for (i = 0; i < c->n; i++)
		if (c->ch[i] == ch) return c->base[i];
	return 0.0f;
}

static float cap_x_of(Cap *c, wchar_t ch)
{
	int i;
	for (i = 0; i < c->n; i++)
		if (c->ch[i] == ch) return c->x[i];
	return 0.0f;
}

static float cap_size_of(Cap *c, wchar_t ch)
{
	int i;
	for (i = 0; i < c->n; i++)
		if (c->ch[i] == ch) return c->size[i];
	return 0.0f;
}

static MdMath *layout_(const char *s, float size)
{
	return MdMath_Layout(s, (int)strlen(s), size, 0, fake_measure, NULL);
}

static MdMath *layout_d(const char *s, float size)
{
	return MdMath_Layout(s, (int)strlen(s), size, 1, fake_measure, NULL);
}

static void draw_(const MdMath *m, Cap *c)
{
	MdMathDraw d;
	ZeroMemory(c, sizeof(*c));
	ZeroMemory(&d, sizeof(d));
	d.user = c;
	d.text = cap_text;
	d.rule = cap_rule;
	MdMath_Draw(m, 0.0f, 0.0f, &d);
}

void __stdcall startup(void)
{
	Log_Init();
	{
		MdMath *m;
		Cap c;

		/* 上下标: 上标基线抬高, 字号缩小 */
		m = layout_("x^2", 10.0f);
		CHECK(m != NULL);
		if (m) {
			CHECK(MdMath_Width(m) > 0.0f);
			draw_(m, &c);
			CHECK(c.texts >= 2);
			CHECK(cap_base_of(&c, L'2') < cap_base_of(&c, L'x'));
			CHECK(cap_size_of(&c, L'2') < cap_size_of(&c, L'x') * 0.8f);
			MdMath_Free(m);
		}

		/* 同时有上下标: 基线分开且保证 subSuperscriptGapMin */
		m = layout_("x_i^2", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, L'i') && cap_has(&c, L'2'));
			CHECK(cap_base_of(&c, L'2') < 0.0f);
			CHECK(cap_base_of(&c, L'i') > 0.0f);
			CHECK(cap_base_of(&c, L'i') - cap_base_of(&c, L'2') > 7.5f);
			MdMath_Free(m);
		}

		m = layout_("\\sum_{i=1}^{n}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_base_of(&c, L'i') > 0.0f);
			CHECK(cap_base_of(&c, L'n') < 0.0f);
			/* 行内: 运算符不放大, 上下标在右侧 */
			CHECK(cap_size_of(&c, 0x2211) < 10.01f);
			CHECK(cap_x_of(&c, L'n') > cap_x_of(&c, 0x2211));
			MdMath_Free(m);
		}

		/* display: \sum 放大, 上下限居中排在正上/正下方 */
		m = layout_d("\\sum_{i=1}^{n}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			float ow;
			draw_(m, &c);
			ow = cap_size_of(&c, 0x2211);
			CHECK(ow > 10.0f * 1.2f);
			CHECK(cap_base_of(&c, L'i') > 0.0f);
			CHECK(cap_base_of(&c, L'n') < -10.0f);   /* 运算符上方 */
			CHECK(cap_x_of(&c, L'n') >= 0.0f);
			CHECK(cap_x_of(&c, L'n') < 12.0f);   /* 居中于运算符列 */
			CHECK(cap_x_of(&c, L'i') < 12.0f);
			MdMath_Free(m);
		}

		/* display: \int 放大但上下标仍在右侧 */
		m = layout_d("\\int_{0}^{1}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			float ow;
			draw_(m, &c);
			ow = cap_size_of(&c, 0x222B);
			CHECK(ow > 10.0f * 1.2f);
			/* 右侧脚本: x 超过运算符本身宽度 (固定度量 0.55em ≈ 7.98) */
			CHECK(cap_x_of(&c, L'1') > 7.0f);
			CHECK(cap_x_of(&c, L'0') > 7.0f);
			MdMath_Free(m);
		}

		/* 分式: 一条规则线, 分子在上/分母在下 */
		m = layout_("\\frac{a}{b}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			CHECK(MdMath_Height(m) > 0.0f);
			CHECK(MdMath_Depth(m) > 0.0f);
			draw_(m, &c);
			CHECK(c.rules == 1);
			CHECK(c.ruleW > 0.0f);
			CHECK(cap_base_of(&c, L'a') < 0.0f);
			CHECK(cap_base_of(&c, L'b') > 0.0f);
			MdMath_Free(m);
		}

		/* 根号: 根号字形 + 上横线 */
		m = layout_("\\sqrt{x}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, 0x221A));
			CHECK(cap_has(&c, L'x'));
			CHECK(c.rules >= 1);
			/* 被开方项保持基线 (不再被抬高) */
			CHECK(cap_base_of(&c, L'x') > -0.01f);
			CHECK(cap_base_of(&c, L'x') < 0.01f);
			MdMath_Free(m);
		}

		/* \left(\frac..\right): 定界符随内容放大 */
		m = layout_("\\left(\\frac{a}{b}\\right)", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, L'('));
			CHECK(cap_has(&c, L')'));
			CHECK(cap_size_of(&c, L'(') > 10.0f * 1.05f);
			MdMath_Free(m);
		}

		/* pmatrix: 2x2, 带圆括号, 两行基线不同 */
		m = layout_("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			CHECK(MdMath_Width(m) > 0.0f);
			CHECK(MdMath_Height(m) > 0.0f);
			draw_(m, &c);
			CHECK(cap_has(&c, L'a') && cap_has(&c, L'b'));
			CHECK(cap_has(&c, L'c') && cap_has(&c, L'd'));
			CHECK(cap_has(&c, L'(') && cap_has(&c, L')'));
			CHECK(cap_base_of(&c, L'a') < cap_base_of(&c, L'c'));
			MdMath_Free(m);
		}

		/* aligned: 两列对齐, 无外部定界符 */
		m = layout_("\\begin{aligned}a&=b\\\\c&=d\\end{aligned}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, L'a') && cap_has(&c, L'd'));
			CHECK(!cap_has(&c, L'('));
			MdMath_Free(m);
		}

		/* cases: 左花括号 */
		m = layout_("\\begin{cases}a&b\\\\c&d\\end{cases}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, L'{'));
			/* 大括号按两行内容自适应放大 (基准 10 DIP) */
			CHECK(cap_size_of(&c, L'{') > 18.0f);
			/* 两行以数学轴居中: 首行在基线上方, 次行在下方且近似对称 */
			CHECK(cap_base_of(&c, L'a') < 0.0f);
			CHECK(cap_base_of(&c, L'c') > 0.0f);
			CHECK(cap_base_of(&c, L'a') + cap_base_of(&c, L'c') > -1.2f);
			CHECK(cap_base_of(&c, L'a') + cap_base_of(&c, L'c') < 1.2f);
			MdMath_Free(m);
		}

		/* 希腊/运算符/关系符, 小写希腊斜体 */
		m = layout_("\\alpha+\\beta\\le\\infty", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, 0x03B1));
			CHECK(cap_has(&c, 0x03B2));
			CHECK(cap_has(&c, 0x2264));
			CHECK(cap_has(&c, 0x221E));
			CHECK((c.flags[0] & MD_MATHF_ITALIC) != 0);
			MdMath_Free(m);
		}

		/* 间距: a\,b 宽于 ab */
		{
			MdMath *m1 = layout_("ab", 10.0f);
			MdMath *m2 = layout_("a\\,b", 10.0f);
			CHECK(m1 && m2);
			if (m1 && m2) CHECK(MdMath_Width(m2) > MdMath_Width(m1));
			MdMath_Free(m1);
			MdMath_Free(m2);
		}

		/* 原子间距: 关系 > 二元; \left..\right 内含定界符内边距 */
		{
			MdMath *m1 = layout_("xy", 10.0f);
			MdMath *m2 = layout_("x=y", 10.0f);
			MdMath *m3 = layout_("x+y", 10.0f);
			MdMath *m4 = layout_("\\left(x\\right)", 10.0f);
			CHECK(m1 && m2 && m3 && m4);
			if (m1 && m2 && m3 && m4) {
				CHECK(MdMath_Width(m2) > MdMath_Width(m1) + 5.0f);
				CHECK(MdMath_Width(m3) > MdMath_Width(m1) + 3.0f);
				CHECK(MdMath_Width(m4) > MdMath_Width(m1) + 5.0f);
			}
			MdMath_Free(m1);
			MdMath_Free(m2);
			MdMath_Free(m3);
			MdMath_Free(m4);
		}

		/* 上下标与主体留缝 */
		{
			MdMath *m = layout_("x^2", 10.0f);
			CHECK(m != NULL);
			if (m) {
				draw_(m, &c);
				CHECK(cap_x_of(&c, L'2') > 5.5f + 0.8f);
				MdMath_Free(m);
			}
		}

		/* 普通括号组: 内容与更高括号的墨迹中心对齐 (括号基线下移) */
		{
			MdMath *m = MdMath_Layout("(x)", 3, 10.0f, 0,
				fake_measure_delim, NULL);
			CHECK(m != NULL);
			if (m) {
				draw_(m, &c);
				CHECK(cap_has(&c, L'(') && cap_has(&c, L'x'));
				CHECK(cap_base_of(&c, L'(') > cap_base_of(&c, L'x') + 0.3f);
				CHECK(MdMath_Width(m) > 6.5f);
				MdMath_Free(m);
			}
		}
		CHECK(layout_("(a", 10.0f) == NULL);

		/* \text: 正体+保留空格 */
		m = layout_("\\text{hello world}", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, L'h') && cap_has(&c, L' '));
			CHECK((c.flags[0] & MD_MATHF_TEXT) != 0);
			MdMath_Free(m);
		}

		/* 左大括号定界符 (转义字符) */
		m = layout_("\\left\\{x\\right.", 10.0f);
		CHECK(m != NULL);
		if (m) {
			draw_(m, &c);
			CHECK(cap_has(&c, L'{'));
			MdMath_Free(m);
		}

		/* 错误: 缺参数/未知命令/未闭合/意外 \right */
		CHECK(layout_("\\frac{a}", 10.0f) == NULL);
		CHECK(layout_("\\sqrt", 10.0f) == NULL);
		CHECK(layout_("\\foo", 10.0f) == NULL);
		CHECK(layout_("\\left(a", 10.0f) == NULL);
		CHECK(layout_("\\right)", 10.0f) == NULL);
		CHECK(layout_("\\begin{matrix}a", 10.0f) == NULL);
		CHECK(layout_("a&b", 10.0f) == NULL);
		CHECK(layout_("a\\\\b", 10.0f) == NULL);
		CHECK(layout_("", 10.0f) == NULL);

		/* 字号放大: 宽度同比增大 */
		{
			MdMath *a = layout_("x^2+y", 10.0f);
			MdMath *b = layout_("x^2+y", 20.0f);
			CHECK(a && b);
			if (a && b) CHECK(MdMath_Width(b) > MdMath_Width(a) * 1.9f);
			MdMath_Free(a);
			MdMath_Free(b);
		}

		/* 嵌套过深 -> 失败而非崩溃 */
		{
			char deep[200];
			int i;
			for (i = 0; i < 40; i++) deep[i] = '{';
			deep[40] = 'x';
			for (i = 0; i < 40; i++) deep[41 + i] = '}';
			deep[81] = 0;
			CHECK(layout_(deep, 10.0f) == NULL);
		}
	}
	{
		char line[120];
		sprintf(line, "md_math_test: %d passed, %d failed\n", g_pass, g_fail);
		out_(line);
	}
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
