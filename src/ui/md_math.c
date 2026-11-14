/*
 * md_math.c - Markdown 数学公式: LaTeX 子集 -> 盒子树 (0CRT, 无 gfx 依赖)
 *
 * 盒子统一以"左端 + 基线"为原点: 子盒子的 (x,y) 相对父盒子原点,
 * y 为基线偏移 (向上为正). 每个盒子记录 w/h/d (宽/基线以上/以下).
 * 度量由调用方注入, 因此 headless 测试可用固定字形宽度验证几何.
 */
#include "ui/md_math.h"
#include <windows.h>

#define MM_MAX_DEPTH   32
#define MM_MAX_NODES   8192
#define MM_MAX_ROWS    64
#define MM_MAX_COLS    64

/* Cambria Math (upem=2048) OpenType MATH 常量, 2026-11-14 从字体表读出.
 * 排版规则只引用这些常量, 不再用随手估计值. */
#define MM_SCRIPT_SCALE      0.7300f   /* scriptPercentScaleDown 73% */
#define MM_SCRIPTSCRIPT_SCALE 0.6000f  /* scriptScriptPercentScaleDown 60% */
#define MM_AXIS_EM           0.2856f   /* axisHeight 585/2048 */
#define MM_SUP_SHIFT_EM      0.3662f   /* superscriptShiftUp 750/2048 */
#define MM_SUB_SHIFT_EM      0.2041f   /* subscriptShiftDown 418/2048 */
#define MM_SUBSUP_GAP_EM     0.1464f   /* subSuperscriptGapMin 300/2048 */
#define MM_LIMIT_GAP_EM      0.0649f   /* upper/lowerLimitGapMin 133/2048 */
#define MM_FRAC_RULE_EM      0.0649f   /* fractionRuleThickness 133/2048 */
#define MM_FRAC_GAP_EM       0.0649f   /* fractionNumerator/DenomGapMin */
#define MM_FRAC_NUM_UP_EM    0.5859f   /* fractionNumeratorShiftUp 1200/2048 */
#define MM_FRAC_NUM_UP_DISP  0.7568f   /* displayStyle 1550/2048 */
#define MM_FRAC_DEN_DN_EM    0.5029f   /* fractionDenominatorShiftDown 1030/2048 */
#define MM_FRAC_DEN_DN_DISP  0.6689f   /* displayStyle 1370/2048 */
#define MM_RADICAL_RULE_EM   0.0649f   /* radicalRuleThickness 133/2048 */
#define MM_RADICAL_GAP_EM    0.0810f   /* radicalVerticalGap 166/2048 */
#define MM_DISPLAY_OP_EM     1.2207f   /* displayOperatorMinHeight 2500/2048 */

enum {
	MB_H = 0,     /* 横向盒: 子项依次右排, 基线对齐 */
	MB_V,         /* 纵向盒: 子项位置由布局显式给出 */
	MB_TEXT,      /* 文本 run */
	MB_RULE,      /* 规则线 (分式横线/根号横线) */
	MB_SPACE      /* 间距 */
};

/* 原子类别 (TeX 间距表) */
enum {
	MC_ORD = 0,
	MC_OP,
	MC_BIN,
	MC_REL,
	MC_OPEN,
	MC_CLOSE,
	MC_PUNCT,
	MC_INNER
};

typedef struct MBox {
	uint8_t type, level;
	uint8_t cls;           /* MC_* 原子类别 */
	uint8_t bigop;         /* \sum/\prod/\int 等大运算符 */
	uint8_t limits;        /* display 时上下限排在正上/正下 */
	uint16_t flags;
	float w, h, d;
	float x, y;            /* 相对父盒子: 左端 / 基线偏移 */
	float size;            /* TEXT: 字号 */
	int *kids;
	int kidN, kidCap;
	int off, tlen;         /* TEXT: 在 MdMath.t 中的区间 */
} MBox;

struct MdMath {
	MBox *box;
	int n, cap;
	int root;
	wchar_t *t;
	int tn, tcap;
	float w, h, d;
};

/* 解析停止条件 (位掩码) */
enum {
	ST_BRACE   = 1u << 0,
	ST_BRACKET = 1u << 1,
	ST_RIGHT   = 1u << 2,
	ST_AMP     = 1u << 3,
	ST_ROW     = 1u << 4,
	ST_END     = 1u << 5,
	ST_PAREN   = 1u << 6
};

typedef struct MP {
	MdMath *m;
	const char *p;
	int len, i;
	int err;
	int depth;
	int nodes;
	int display;           /* 1 = 行间公式 (大运算符/上下限) */
	float base;            /* 基准字号 */
	MdMathMeasureFn fn;
	void *fnUser;
} MP;

static void *mm_alloc(int n)
{
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)(n > 0 ? n : 1));
}

static void *mm_realloc(void *p, int n)
{
	if (p) return HeapReAlloc(GetProcessHeap(), 0, p, (SIZE_T)(n > 0 ? n : 1));
	return HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(n > 0 ? n : 1));
}

static float mm_max2(float a, float b) { return a > b ? a : b; }

static float mm_level_size(const MP *mp, int level)
{
	static const float k[4] = { 1.0f, 1.0f, MM_SCRIPT_SCALE,
		MM_SCRIPTSCRIPT_SCALE };
	int l = level;
	if (l < 0) l = 0;
	if (l > 3) l = 3;
	return mp->base * k[l];
}

static int mm_char_class(uint32_t cp, int letter);

/* 上/下标层级: display/text -> script, script -> scriptscript */
static int mm_script_level(int level)
{
	if (level <= MD_MATH_TEXT) return MD_MATH_SCRIPT;
	return MD_MATH_SCRIPTSCRIPT;
}

static MBox *mm_box(MP *mp, int idx)
{
	if (!mp->m || idx < 0 || idx >= mp->m->n) return NULL;
	return &mp->m->box[idx];
}

static int mm_new(MP *mp, int type, int level)
{
	MdMath *m = mp->m;
	MBox *b;
	if (mp->err) return -1;
	if (mp->nodes++ >= MM_MAX_NODES) { mp->err = 1; return -1; }
	if (m->n >= m->cap) {
		int nc = m->cap ? m->cap * 2 : 128;
		MBox *nb = (MBox *)mm_realloc(m->box, nc * (int)sizeof(MBox));
		if (!nb) { mp->err = 1; return -1; }
		m->box = nb;
		m->cap = nc;
	}
	b = &m->box[m->n];
	ZeroMemory(b, sizeof(*b));
	b->type = (uint8_t)type;
	b->level = (uint8_t)level;
	b->size = mm_level_size(mp, level);
	return m->n++;
}

static int mm_add_kid(MP *mp, int parent, int kid)
{
	MBox *b = mm_box(mp, parent);
	if (!b) { mp->err = 1; return 0; }
	if (b->kidN >= b->kidCap) {
		int nc = b->kidCap ? b->kidCap * 2 : 4;
		int *nk = (int *)mm_realloc(b->kids, nc * (int)sizeof(int));
		if (!nk) { mp->err = 1; return 0; }
		b->kids = nk;
		b->kidCap = nc;
	}
	b->kids[b->kidN++] = kid;
	return 1;
}

/* 把 UTF-16 追加到公式文本池, 返回起始下标 */
static int mm_tput(MP *mp, const wchar_t *s, int n)
{
	MdMath *m = mp->m;
	int off;
	if (n <= 0) return 0;
	if (m->tn + n + 1 > m->tcap) {
		int nc = m->tcap ? m->tcap : 128;
		wchar_t *nt;
		while (nc < m->tn + n + 1) nc *= 2;
		nt = (wchar_t *)mm_realloc(m->t, nc * (int)sizeof(wchar_t));
		if (!nt) { mp->err = 1; return 0; }
		m->t = nt;
		m->tcap = nc;
	}
	off = m->tn;
	{
		int i;
		for (i = 0; i < n; i++) m->t[m->tn + i] = s[i];
	}
	m->tn += n;
	m->t[m->tn] = 0;
	return off;
}

static int mm_measure_box(MP *mp, int idx, const wchar_t *s, int n,
	float size, uint32_t flags)
{
	MBox *b = mm_box(mp, idx);
	float h = 0.0f, d = 0.0f, w;
	if (!b || !mp->fn) { mp->err = 1; return 0; }
	w = mp->fn(mp->fnUser, s, n, size, flags, &h, &d);
	if (w < 0.0f || h < 0.0f || d < 0.0f) { mp->err = 1; return 0; }
	b->w = w;
	b->h = h;
	b->d = d;
	return 1;
}

static int mm_text_add(MP *mp, const wchar_t *s, int n, uint32_t flags,
	int level, float size)
{
	int idx = mm_new(mp, MB_TEXT, level);
	MBox *b = mm_box(mp, idx);
	if (!b) return -1;
	b->flags = (uint16_t)flags;
	if (size <= 0.0f) size = mm_level_size(mp, level);
	b->size = size;
	b->off = mm_tput(mp, s, n);
	b->tlen = n;
	if (!mm_measure_box(mp, idx, s, n, size, flags)) return -1;
	return idx;
}

static int mm_text_cp(MP *mp, uint32_t cp, uint32_t flags, int level)
{
	wchar_t s[2];
	int n = 0;
	if (cp < 0x10000u) {
		s[n++] = (wchar_t)cp;
	} else {
		cp -= 0x10000u;
		s[n++] = (wchar_t)(0xD800u + (cp >> 10));
		s[n++] = (wchar_t)(0xDC00u + (cp & 0x3FFu));
	}
	{
		int idx = mm_text_add(mp, s, n, flags, level, 0.0f);
		if (idx >= 0) {
			MBox *b = mm_box(mp, idx);
			if (b) b->cls = (uint8_t)mm_char_class(cp, 0);
		}
		return idx;
	}
}

static int mm_space(MP *mp, float em, int level)
{
	int idx = mm_new(mp, MB_SPACE, level);
	MBox *b = mm_box(mp, idx);
	if (!b) return -1;
	b->w = em * mm_level_size(mp, level);
	b->h = b->d = 0.0f;
	return idx;
}

/* UTF-8 解码: 返回码点, *adv 写入消耗字节数; 非法字节按 U+FFFD */
static uint32_t mm_utf8(const char *p, int len, int *adv)
{
	unsigned char c = (unsigned char)p[0];
	uint32_t cp;
	int need, i;
	*adv = 1;
	if (c < 0x80u) return c;
	if ((c & 0xE0u) == 0xC0u) { cp = c & 0x1Fu; need = 1; }
	else if ((c & 0xF0u) == 0xE0u) { cp = c & 0x0Fu; need = 2; }
	else if ((c & 0xF8u) == 0xF0u) { cp = c & 0x07u; need = 3; }
	else return 0xFFFDu;
	if (1 + need > len) return 0xFFFDu;
	for (i = 1; i <= need; i++) {
		unsigned char e = (unsigned char)p[i];
		if ((e & 0xC0u) != 0x80u) return 0xFFFDu;
		cp = (cp << 6) | (uint32_t)(e & 0x3Fu);
	}
	*adv = 1 + need;
	return cp;
}

static int mm_is_letter_cp(uint32_t cp)
{
	if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return 1;
	if (cp >= 0x3B1u && cp <= 0x3C9u) return 1;   /* 小写希腊 */
	return 0;
}

static int mm_is_special(unsigned char c)
{
	return c == '{' || c == '}' || c == '^' || c == '_' || c == '\\' ||
		c == '&' || c == '$' || c == '[' || c == ']';
}

static int mm_name_is(const char *name, const char *w);

/* 原子类别: 决定相邻元素间距 (关系 > 二元 > 运算符/标点 > 括号内侧) */
static int mm_char_class(uint32_t cp, int letter)
{
	if (letter) return MC_ORD;
	switch (cp) {
	case '(': case '[': case '{': return MC_OPEN;
	case ')': case ']': case '}': return MC_CLOSE;
	case ',': case ';': return MC_PUNCT;
	case '=': case '<': case '>': return MC_REL;
	case '+': case '-': case '*': case '/': return MC_BIN;
	default: break;
	}
	if (cp == 0x2264u || cp == 0x2265u || cp == 0x2260u || cp == 0x2248u ||
		cp == 0x2261u || cp == 0x2192u || cp == 0x2190u || cp == 0x21D2u ||
		cp == 0x2208u || cp == 0x2209u || cp == 0x2282u || cp == 0x2283u)
		return MC_REL;
	if (cp == 0x00D7u || cp == 0x00F7u || cp == 0x00B1u || cp == 0x2213u ||
		cp == 0x22C5u || cp == 0x222Au || cp == 0x2229u)
		return MC_BIN;
	return MC_ORD;
}

static int mm_sym_class(const char *name)
{
	if (mm_name_is(name, "sum") || mm_name_is(name, "prod") ||
		mm_name_is(name, "int") || mm_name_is(name, "oint"))
		return MC_OP;
	if (mm_name_is(name, "le") || mm_name_is(name, "leq") ||
		mm_name_is(name, "ge") || mm_name_is(name, "geq") ||
		mm_name_is(name, "ne") || mm_name_is(name, "neq") ||
		mm_name_is(name, "approx") || mm_name_is(name, "equiv") ||
		mm_name_is(name, "to") || mm_name_is(name, "rightarrow") ||
		mm_name_is(name, "leftarrow") || mm_name_is(name, "Rightarrow") ||
		mm_name_is(name, "in") || mm_name_is(name, "notin") ||
		mm_name_is(name, "subset") || mm_name_is(name, "supset"))
		return MC_REL;
	if (mm_name_is(name, "cdot") || mm_name_is(name, "times") ||
		mm_name_is(name, "div") || mm_name_is(name, "pm") ||
		mm_name_is(name, "mp") || mm_name_is(name, "cup") ||
		mm_name_is(name, "cap"))
		return MC_BIN;
	return MC_ORD;
}

/* TeX 简化间距表 (单位 em): 关系 0.28, 二元 0.22,
 * 运算符-普通 0.17, 标点后 0.17, 括号内侧 0.06 */
static float mm_class_space(int a, int b)
{
	if (a == MC_REL || b == MC_REL) return 0.28f;
	if ((a == MC_OP && (b == MC_ORD || b == MC_INNER)) ||
		(b == MC_OP && (a == MC_ORD || a == MC_INNER)))
		return 0.17f;
	if (a == MC_BIN || b == MC_BIN) return 0.22f;
	if (a == MC_PUNCT) return 0.17f;
	/* 函数名/变量与括号组之间需要可见间隔 (f( x )) */
	if (a == MC_ORD && b == MC_INNER) return 0.12f;
	if (a == MC_INNER && b == MC_ORD) return 0.06f;
	if (a == MC_INNER && b == MC_INNER) return 0.06f;
	if (a == MC_OPEN || b == MC_CLOSE) return 0.06f;
	return 0.0f;
}

static void mm_skip_ws(MP *mp)
{
	while (mp->i < mp->len) {
		char c = mp->p[mp->i];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
		mp->i++;
	}
}

static int mm_at_stop(MP *mp, uint32_t stops)
{
	int i = mp->i;
	if (i >= mp->len) return 1;
	if (mp->p[i] == '}' && (stops & ST_BRACE)) return 1;
	if (mp->p[i] == ']' && (stops & ST_BRACKET)) return 1;
	if ((mp->p[i] == ')' || mp->p[i] == ']') && (stops & ST_PAREN)) return 1;
	if (mp->p[i] == '&' && (stops & ST_AMP)) return 1;
	if (mp->p[i] == '\\' && i + 1 < mp->len) {
		if (mp->p[i + 1] == '\\' && (stops & ST_ROW)) return 1;
		if (mp->p[i + 1] == 'r' && i + 5 <= mp->len &&
			mp->p[i + 2] == 'i' && mp->p[i + 3] == 'g' &&
			mp->p[i + 4] == 'h' && mp->p[i + 5] == 't' &&
			(stops & ST_RIGHT)) return 1;
		if (mp->p[i + 1] == 'e' && i + 4 <= mp->len &&
			mp->p[i + 2] == 'n' && mp->p[i + 3] == 'd' &&
			(stops & ST_END)) return 1;
	}
	return 0;
}

static void mm_bbox_join(MBox *b, int type)
{
	int k;
	b->type = (uint8_t)type;
	b->w = b->h = b->d = 0.0f;
	if (type == MB_H) {
		for (k = 0; k < b->kidN; k++);
	}
}

/* 横向盒: 子盒子 x 依次累加, y=0 */
static void mm_layout_h(MP *mp, int idx)
{
	MBox *b = mm_box(mp, idx);
	float x = 0.0f;
	int k;
	if (!b) return;
	b->w = b->h = b->d = 0.0f;
	for (k = 0; k < b->kidN; k++) {
		MBox *c = mm_box(mp, b->kids[k]);
		if (!c) continue;
		c->x = x;
		c->y = 0.0f;
		x += c->w;
		b->h = mm_max2(b->h, c->h);
		b->d = mm_max2(b->d, c->d);
	}
	b->w = x;
}

/* 纵向盒: 子盒子位置已给出, 统一定界 */
static void mm_layout_v(MP *mp, int idx)
{
	MBox *b = mm_box(mp, idx);
	int k;
	if (!b) return;
	b->w = b->h = b->d = 0.0f;
	for (k = 0; k < b->kidN; k++) {
		MBox *c = mm_box(mp, b->kids[k]);
		float top, bot;
		if (!c) continue;
		top = c->y + c->h;
		bot = c->d - c->y;
		if (c->x + c->w > b->w) b->w = c->x + c->w;
		if (top > b->h) b->h = top;
		if (bot > b->d) b->d = bot;
	}
}

static int mm_expr(MP *mp, int level, uint32_t stops);

static int mm_is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int mm_cmd_name(MP *mp, char *name, int cap, int *single)
{
	int n = 0;
	*single = 0;
	mp->i++;   /* 跳过 '\' */
	if (mp->i >= mp->len) { mp->err = 1; return 0; }
	if (!mm_is_alpha(mp->p[mp->i])) {
		char c = mp->p[mp->i++];
		*single = 1;
		if (n < cap - 1) name[n++] = c;
		name[n] = 0;
		return 1;
	}
	while (mp->i < mp->len && mm_is_alpha(mp->p[mp->i])) {
		if (n >= cap - 1) { mp->err = 1; return 0; }
		name[n++] = mp->p[mp->i++];
	}
	name[n] = 0;
	return 1;
}

static int mm_name_is(const char *name, const char *w)
{
	int i = 0;
	while (name[i] && w[i]) {
		if (name[i] != w[i]) return 0;
		i++;
	}
	return name[i] == 0 && w[i] == 0;
}

static int mm_arg(MP *mp, int level);

/* 纯字符 run: 字母(斜体) / 其它(正体) 分段 */
static int mm_plain_run(MP *mp, int level)
{
	const char *p = mp->p;
	int start = mp->i;
	int a;
	uint32_t cp = mm_utf8(p + start, mp->len - start, &a);
	int letter = mm_is_letter_cp(cp);
	int cls = mm_char_class(cp, letter);
	wchar_t buf[64];
	int n = 0;
	int idx;
	while (mp->i < mp->len) {
		int adv;
		uint32_t c = mm_utf8(p + mp->i, mp->len - mp->i, &adv);
		int cletter = mm_is_letter_cp(c);
		int ccls = mm_char_class(c, cletter);
		if (c < 0x80u && mm_is_special((unsigned char)c)) break;
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
		if (cletter != letter) break;
		if (ccls != cls) break;   /* 运算符/括号/标点各自成盒, 便于加间距 */
		if (n + 2 >= (int)(sizeof(buf) / sizeof(buf[0]))) break;
		if (c < 0x10000u) {
			buf[n++] = (wchar_t)c;
		} else {
			uint32_t v = c - 0x10000u;
			buf[n++] = (wchar_t)(0xD800u + (v >> 10));
			buf[n++] = (wchar_t)(0xDC00u + (v & 0x3FFu));
		}
		mp->i += adv;
	}
	if (n <= 0) { mp->err = 1; return -1; }
	idx = mm_text_add(mp, buf, n, letter ? MD_MATHF_ITALIC : 0u, level,
		0.0f);
	if (idx >= 0) {
		MBox *b = mm_box(mp, idx);
		if (b) b->cls = (uint8_t)cls;
	}
	return idx;
}

static int mm_arg(MP *mp, int level);

/* 读取 {环境名} */
static int mm_read_env_name(MP *mp, char *name, int cap)
{
	int n = 0;
	mm_skip_ws(mp);
	if (mp->i >= mp->len || mp->p[mp->i] != '{') { mp->err = 1; return 0; }
	mp->i++;
	while (mp->i < mp->len && mp->p[mp->i] != '}') {
		if (n >= cap - 1) { mp->err = 1; return 0; }
		name[n++] = mp->p[mp->i++];
	}
	if (mp->i >= mp->len) { mp->err = 1; return 0; }
	name[n] = 0;
	mp->i++;
	return 1;
}

/* 定界符: 读取一个字符或 \name, 输出 UTF-16 串 */
static int mm_delim(MP *mp, wchar_t *out, int *outN)
{
	mm_skip_ws(mp);
	*outN = 0;
	if (mp->i >= mp->len) { mp->err = 1; return 0; }
	if (mp->p[mp->i] == '\\') {
		char nm[24];
		int single;
		if (!mm_cmd_name(mp, nm, (int)sizeof(nm), &single)) return 0;
		if (single) {
			out[0] = (wchar_t)(unsigned char)nm[0];
			*outN = 1;
			return 1;
		}
		if (mm_name_is(nm, "langle")) { out[0] = 0x27E8; *outN = 1; }
		else if (mm_name_is(nm, "rangle")) { out[0] = 0x27E9; *outN = 1; }
		else if (mm_name_is(nm, "lfloor")) { out[0] = 0x230A; *outN = 1; }
		else if (mm_name_is(nm, "rfloor")) { out[0] = 0x230B; *outN = 1; }
		else if (mm_name_is(nm, "lceil")) { out[0] = 0x2308; *outN = 1; }
		else if (nm[0] == '|' && nm[1] == 0) { out[0] = 0x2016; *outN = 1; }
		else { mp->err = 1; return 0; }
		return 1;
	}
	{
		int adv;
		uint32_t cp = mm_utf8(mp->p + mp->i, mp->len - mp->i, &adv);
		mp->i += adv;
		if (cp == '.') { *outN = 0; return 1; }   /* 空定界符 */
		if (cp < 0x10000u) out[0] = (wchar_t)cp;
		else out[0] = 0xFFFD;
		*outN = 1;
	}
	return 1;
}

/* 定界符按内容高度连续缩放: 先按基准字号度量, 不足时按高度比例放大
 * (字形行高随字号线性变化), 避免离散档位要么压不住、要么留白过多. */
static int mm_delim_box(MP *mp, const wchar_t *s, int n, int level,
	float needH, float needD)
{
	float baseSize = mm_level_size(mp, level), size = baseSize;
	float h = 0.0f, d = 0.0f, w = 0.0f;
	float need = needH + needD;
	int idx;
	if (n <= 0) {
		idx = mm_new(mp, MB_SPACE, level);
		return idx;
	}
	w = mp->fn(mp->fnUser, s, n, size, 0u, &h, &d);
	if (w < 0.0f || h < 0.0f || d < 0.0f) { mp->err = 1; return -1; }
	if (need > 0.0f && h + d > 0.05f && h + d < need * 1.02f) {
		float k = need / (h + d) * 1.08f;
		if (k < 1.15f) k = 1.15f;
		if (k > 5.0f) k = 5.0f;
		size = baseSize * k;
		w = mp->fn(mp->fnUser, s, n, size, 0u, &h, &d);
		if (w < 0.0f || h < 0.0f || d < 0.0f) { mp->err = 1; return -1; }
	}
	idx = mm_new(mp, MB_TEXT, level);
	if (idx < 0) return -1;
	{
		MBox *b = mm_box(mp, idx);
		b->flags = 0;
		b->size = size;
		b->off = mm_tput(mp, s, n);
		b->tlen = n;
		b->w = w;
		b->h = h;
		b->d = d;
	}
	return idx;
}

/* 用定界符包裹内容 (pmatrix / cases 等) */
static int mm_wrap_delim(MP *mp, int content, const wchar_t *l, int ln,
	const wchar_t *r, int rn, int level)
{
	MBox *cb = mm_box(mp, content);
	int lb, rb, idx;
	float yl, yr, gap;
	if (!cb) return -1;
	lb = mm_delim_box(mp, l, ln, level, cb->h, cb->d);
	rb = mm_delim_box(mp, r, rn, level, cb->h, cb->d);
	idx = mm_new(mp, MB_H, level);
	if (lb < 0 || rb < 0 || idx < 0) return -1;
	gap = mm_level_size(mp, level) * 0.15f;  /* 定界符与内容内边距 */
	{
		MBox *bL = mm_box(mp, lb);
		MBox *bR = mm_box(mp, rb);
		yl = (cb->h - cb->d - (bL->h - bL->d)) * 0.5f;
		yr = (cb->h - cb->d - (bR->h - bR->d)) * 0.5f;
		bL->x = 0.0f; bL->y = yl;
		cb->x = bL->w + (ln > 0 ? gap : 0.0f); cb->y = 0.0f;
		bR->x = cb->x + cb->w + (rn > 0 ? gap : 0.0f); bR->y = yr;
		mm_add_kid(mp, idx, lb);
		mm_add_kid(mp, idx, content);
		mm_add_kid(mp, idx, rb);
	}
	mm_layout_v(mp, idx);
	/* 整体以数学轴为中心 (TeX \vcenter): 多行 cases/matrix 不再整体挂在
	 * 基线上, 左侧 f(x)= 与块中心视觉对齐. */
	{
		MBox *b = mm_box(mp, idx);
		if (b) {
			float axis = mm_level_size(mp, level) * MM_AXIS_EM;
			float dy = axis - (b->h - b->d) * 0.5f;
			if (dy > 0.01f || dy < -0.01f) {
				int k;
				for (k = 0; k < b->kidN; k++) {
					MBox *c = mm_box(mp, b->kids[k]);
					if (c) c->y += dy;
				}
				mm_layout_v(mp, idx);
				b = mm_box(mp, idx);
			}
			if (b) b->cls = MC_INNER;
		}
	}
	return idx;
}

/* 普通 (...) / [...] 同样按定界符组排版: 自适应高度、数学轴居中、
 * 定界符与内容之间保留内边距; 未配对时按语法错误回退源码。 */
static int mm_delim_group(MP *mp, int level)
{
	char open = mp->p[mp->i];
	char close = (open == '(') ? ')' : ']';
	wchar_t lo = (wchar_t)open, lc = (wchar_t)close;
	int content, idx;
	mp->i++;
	content = mm_expr(mp, level, ST_PAREN);
	if (content < 0) return -1;
	mm_skip_ws(mp);
	if (mp->i >= mp->len || mp->p[mp->i] != close) {
		mp->err = 1;
		return -1;
	}
	mp->i++;
	idx = mm_wrap_delim(mp, content, &lo, 1, &lc, 1, level);
	return idx;
}

static int mm_frac(MP *mp, int level)
{
	int num = mm_arg(mp, level + 1);
	int den = mm_arg(mp, level + 1);
	int idx, rule;
	float size = mm_level_size(mp, level);
	float axis = size * MM_AXIS_EM;
	float t = size * MM_FRAC_RULE_EM;
	float gap = size * MM_FRAC_GAP_EM;
	float side = size * 0.1f;
	float W;
	if (num < 0 || den < 0) return -1;
	if (t < 0.5f) t = 0.5f;
	{
		MBox *nb = mm_box(mp, num);
		MBox *db = mm_box(mp, den);
		W = mm_max2(nb->w, db->w) + side * 2.0f;
	}
	idx = mm_new(mp, MB_V, level);
	rule = mm_new(mp, MB_RULE, level);
	if (idx < 0 || rule < 0) return -1;
	{
		MBox *nb = mm_box(mp, num);
		MBox *db = mm_box(mp, den);
		MBox *rb = mm_box(mp, rule);
		int disp = mp->display && level <= MD_MATH_TEXT;
		float ny = size * (disp ? MM_FRAC_NUM_UP_DISP : MM_FRAC_NUM_UP_EM);
		float dy = size * (disp ? MM_FRAC_DEN_DN_DISP : MM_FRAC_DEN_DN_EM);
		float needUp = axis + t * 0.5f + gap + nb->d;
		float needDown = axis + t * 0.5f + gap + db->h;
		if (ny < needUp) ny = needUp;
		if (dy < needDown) dy = needDown;
		nb->x = (W - nb->w) * 0.5f; nb->y = ny;
		db->x = (W - db->w) * 0.5f; db->y = -dy;
		rb->x = 0.0f; rb->y = axis;
		rb->w = W; rb->h = t * 0.5f; rb->d = t * 0.5f;
		mm_add_kid(mp, idx, num);
		mm_add_kid(mp, idx, den);
		mm_add_kid(mp, idx, rule);
	}
	mm_layout_v(mp, idx);
	return idx;
}

/* 根号字形: 按被开方项的墨迹高度/深度选择合适字号 (可放大或缩小),
 * 使根号顶钩正好落在被开方项上方的横线高度. 返回 TEXT 盒. */
static int mm_radical_box(MP *mp, int level, float needH, float needD,
	float clear)
{
	float base = mm_level_size(mp, level);
	float h = 0.0f, d = 0.0f, w, target, k;
	int idx;
	w = mp->fn(mp->fnUser, L"\u221A", 1, base, 0u, &h, &d);
	if (w < 0.0f || h <= 0.01f) { mp->err = 1; return -1; }
	target = needH + clear;
	k = target / h;
	if (needD > 0.01f && d > 0.01f) {
		float kd = needD / d;
		if (kd > k) k = kd;
	}
	/* 根号只用基础字形或更大变体, 不缩到比基础字形还小 */
	if (k < 1.0f) k = 1.0f;
	if (k > 5.0f) k = 5.0f;
	if (k > 1.06f) {
		base *= k;
		w = mp->fn(mp->fnUser, L"\u221A", 1, base, 0u, &h, &d);
		if (w < 0.0f || h < 0.0f) { mp->err = 1; return -1; }
	}
	idx = mm_new(mp, MB_TEXT, level);
	if (idx < 0) return -1;
	{
		MBox *b = mm_box(mp, idx);
		b->flags = 0;
		b->size = base;
		b->off = mm_tput(mp, L"\u221A", 1);
		b->tlen = 1;
		b->w = w;
		b->h = h;
		b->d = d;
	}
	return idx;
}

static int mm_sqrt(MP *mp, int level)
{
	int index = -1, content, idx, rad, rule;
	float size = mm_level_size(mp, level);
	float t = size * MM_RADICAL_RULE_EM;
	float rx;
	MBox *cb;
	if (t < 0.5f) t = 0.5f;
	mm_skip_ws(mp);
	if (mp->i < mp->len && mp->p[mp->i] == '[') {
		mp->i++;
		index = mm_expr(mp, mm_script_level(level), ST_BRACKET);
		if (index < 0) return -1;
		mm_skip_ws(mp);
		if (mp->i >= mp->len || mp->p[mp->i] != ']') { mp->err = 1; return -1; }
		mp->i++;
	}
	content = mm_arg(mp, level);
	if (content < 0) return -1;
	cb = mm_box(mp, content);
	if (!cb) { mp->err = 1; return -1; }
	idx = mm_new(mp, MB_V, level);
	rad = mm_radical_box(mp, level, cb->h, cb->d, size * MM_RADICAL_GAP_EM);
	rule = mm_new(mp, MB_RULE, level);
	if (idx < 0 || rad < 0 || rule < 0) return -1;
	{
		MBox *rb = mm_box(mp, rad);
		MBox *ub = mm_box(mp, rule);
		rx = rb->w * 0.80f;
		cb->x = rx;
		cb->y = 0.0f;                    /* 被开方项保持基线 */
		ub->x = rx;
		ub->y = rb->h - t * 0.5f;        /* 横线贴住根号顶钩 */
		ub->w = cb->w + size * 0.02f;
		ub->h = t * 0.5f;
		ub->d = t * 0.5f;
		mm_add_kid(mp, idx, rad);
		mm_add_kid(mp, idx, content);
		mm_add_kid(mp, idx, rule);
		if (index >= 0) {
			MBox *ib = mm_box(mp, index);
			ib->x = 0.0f;
			ib->y = rb->h + size * 0.1f;
			mm_add_kid(mp, idx, index);
		}
	}
	mm_layout_v(mp, idx);
	return idx;
}

static int mm_text_cmd(MP *mp, int level)
{
	int start, end, n, need;
	wchar_t *tmp;
	int depth = 0;
	mm_skip_ws(mp);
	if (mp->i >= mp->len || mp->p[mp->i] != '{') { mp->err = 1; return -1; }
	mp->i++;
	start = mp->i;
	while (mp->i < mp->len) {
		char c = mp->p[mp->i];
		if (c == '\\' && mp->i + 1 < mp->len) { mp->i += 2; continue; }
		if (c == '{') depth++;
		else if (c == '}') {
			if (depth == 0) break;
			depth--;
		}
		mp->i++;
	}
	if (mp->i >= mp->len) { mp->err = 1; return -1; }
	end = mp->i;
	mp->i++;
	n = end - start;
	need = MultiByteToWideChar(CP_UTF8, 0, mp->p + start, n, NULL, 0);
	if (need < 0) { mp->err = 1; return -1; }
	if (need == 0) return mm_text_add(mp, L"", 0, MD_MATHF_TEXT, level, 0.0f);
	tmp = (wchar_t *)mm_alloc(need * (int)sizeof(wchar_t));
	if (!tmp) { mp->err = 1; return -1; }
	MultiByteToWideChar(CP_UTF8, 0, mp->p + start, n, tmp, need);
	{
		int idx = mm_text_add(mp, tmp, need, MD_MATHF_TEXT, level, 0.0f);
		HeapFree(GetProcessHeap(), 0, tmp);
		return idx;
	}
}

typedef struct MSym { const char *name; const wchar_t *s; uint32_t flags; } MSym;

static const MSym kSyms[] = {
	{ "alpha", L"\u03B1", MD_MATHF_ITALIC }, { "beta", L"\u03B2", MD_MATHF_ITALIC },
	{ "gamma", L"\u03B3", MD_MATHF_ITALIC }, { "delta", L"\u03B4", MD_MATHF_ITALIC },
	{ "epsilon", L"\u03B5", MD_MATHF_ITALIC }, { "zeta", L"\u03B6", MD_MATHF_ITALIC },
	{ "eta", L"\u03B7", MD_MATHF_ITALIC }, { "theta", L"\u03B8", MD_MATHF_ITALIC },
	{ "iota", L"\u03B9", MD_MATHF_ITALIC }, { "kappa", L"\u03BA", MD_MATHF_ITALIC },
	{ "lambda", L"\u03BB", MD_MATHF_ITALIC }, { "mu", L"\u03BC", MD_MATHF_ITALIC },
	{ "nu", L"\u03BD", MD_MATHF_ITALIC }, { "xi", L"\u03BE", MD_MATHF_ITALIC },
	{ "pi", L"\u03C0", MD_MATHF_ITALIC }, { "rho", L"\u03C1", MD_MATHF_ITALIC },
	{ "sigma", L"\u03C3", MD_MATHF_ITALIC }, { "tau", L"\u03C4", MD_MATHF_ITALIC },
	{ "phi", L"\u03C6", MD_MATHF_ITALIC }, { "chi", L"\u03C7", MD_MATHF_ITALIC },
	{ "psi", L"\u03C8", MD_MATHF_ITALIC }, { "omega", L"\u03C9", MD_MATHF_ITALIC },
	{ "Gamma", L"\u0393", 0 }, { "Delta", L"\u0394", 0 },
	{ "Theta", L"\u0398", 0 }, { "Lambda", L"\u039B", 0 },
	{ "Pi", L"\u03A0", 0 }, { "Sigma", L"\u03A3", 0 },
	{ "Phi", L"\u03A6", 0 }, { "Psi", L"\u03A8", 0 },
	{ "Omega", L"\u03A9", 0 },
	{ "sum", L"\u2211", 0 }, { "prod", L"\u220F", 0 },
	{ "int", L"\u222B", 0 }, { "oint", L"\u222E", 0 },
	{ "infty", L"\u221E", 0 }, { "cdot", L"\u22C5", 0 },
	{ "times", L"\u00D7", 0 }, { "div", L"\u00F7", 0 },
	{ "pm", L"\u00B1", 0 }, { "mp", L"\u2213", 0 },
	{ "le", L"\u2264", 0 }, { "leq", L"\u2264", 0 },
	{ "ge", L"\u2265", 0 }, { "geq", L"\u2265", 0 },
	{ "ne", L"\u2260", 0 }, { "neq", L"\u2260", 0 },
	{ "approx", L"\u2248", 0 }, { "equiv", L"\u2261", 0 },
	{ "to", L"\u2192", 0 }, { "rightarrow", L"\u2192", 0 },
	{ "leftarrow", L"\u2190", 0 }, { "Rightarrow", L"\u21D2", 0 },
	{ "forall", L"\u2200", 0 }, { "exists", L"\u2203", 0 },
	{ "in", L"\u2208", 0 }, { "notin", L"\u2209", 0 },
	{ "subset", L"\u2282", 0 }, { "supset", L"\u2283", 0 },
	{ "cup", L"\u222A", 0 }, { "cap", L"\u2229", 0 },
	{ "emptyset", L"\u2205", 0 }, { "partial", L"\u2202", 0 },
	{ "nabla", L"\u2207", 0 }, { "ldots", L"\u2026", 0 },
	{ "cdots", L"\u22EF", 0 }, { "prime", L"\u2032", 0 },
	{ "quad", L"\u2003", 0 }, { "qquad", L"\u2003\u2003", 0 }
};

static void mm_cells_free(int *cells, int *rowStart, int *rowLen)
{
	if (cells) HeapFree(GetProcessHeap(), 0, cells);
	if (rowStart) HeapFree(GetProcessHeap(), 0, rowStart);
	if (rowLen) HeapFree(GetProcessHeap(), 0, rowLen);
}

/* \begin{env} ... \end{env} */
static int mm_env(MP *mp, int level)
{
	char env[20];
	int *cells = NULL, *rowStart = NULL, *rowLen = NULL;
	int cellN = 0, cellCap = 0, rowN = 0, rowCap = 0;
	int rows = 0, cols = 0, r, c;
	int idx = -1;
	if (!mm_read_env_name(mp, env, (int)sizeof(env))) return -1;
	for (;;) {
		int cell = mm_expr(mp, level, ST_AMP | ST_ROW | ST_END);
		if (cell < 0) break;
		if (cellN >= cellCap) {
			int nc = cellCap ? cellCap * 2 : 32;
			int *np = (int *)mm_realloc(cells, nc * (int)sizeof(int));
			if (!np) { mp->err = 1; break; }
			cells = np;
			cellCap = nc;
		}
		cells[cellN++] = cell;
		mm_skip_ws(mp);
		if (mp->i < mp->len && mp->p[mp->i] == '&') {
			mp->i++;
			continue;
		}
		/* 行结束 */
		if (rowN >= rowCap) {
			int nc = rowCap ? rowCap * 2 : 8;
			int *ns = (int *)mm_realloc(rowStart, nc * (int)sizeof(int));
			int *nl = (int *)mm_realloc(rowLen, nc * (int)sizeof(int));
			if (!ns || !nl) { mp->err = 1; break; }
			rowStart = ns;
			rowLen = nl;
			rowCap = nc;
		}
		rowStart[rowN] = cellN - 1;   /* 本行最后一格前面推导 */
		rowLen[rowN] = 0;
		{
			int k = rowN > 0 ? rowStart[rowN - 1] + rowLen[rowN - 1] : 0;
			rowLen[rowN] = cellN - k;
			rowStart[rowN] = k;
		}
		if (rowLen[rowN] > cols) cols = rowLen[rowN];
		rowN++;
		rows = rowN;
		if (rows > MM_MAX_ROWS || cols > MM_MAX_COLS) { mp->err = 1; break; }
		if (mp->i < mp->len && mp->p[mp->i] == '\\' &&
			mp->i + 1 < mp->len && mp->p[mp->i + 1] == '\\') {
			mp->i += 2;
			continue;
		}
		if (mp->i < mp->len && mp->p[mp->i] == '\\' && mp->i + 4 <= mp->len &&
			mp->p[mp->i + 1] == 'e' && mp->p[mp->i + 2] == 'n' &&
			mp->p[mp->i + 3] == 'd') {
			char nm[20];
			mp->i += 4;   /* 跳过 \end, 后面是 {env} */
			if (!mm_read_env_name(mp, nm, (int)sizeof(nm))) break;
			if (!mm_name_is(nm, env)) { mp->err = 1; break; }
			break;
		}
		mp->err = 1;
		break;
	}
	if (mp->err || rowN <= 0) {
		mm_cells_free(cells, rowStart, rowLen);
		return -1;
	}
	{
		int aligned = mm_name_is(env, "aligned");
		int cases = mm_name_is(env, "cases");
		int matrix = mm_name_is(env, "matrix");
		int pmat = mm_name_is(env, "pmatrix");
		int bmat = mm_name_is(env, "bmatrix");
		int vmat = mm_name_is(env, "vmatrix");
		float size = mm_level_size(mp, level);
		float colGap = aligned ? size * 0.28f
			: (cases ? size * 0.6f : size * 0.8f);
		float *cw = NULL;
		if (!(aligned || cases || matrix || pmat || bmat || vmat)) {
			mm_cells_free(cells, rowStart, rowLen);
			return -1;
		}
		cw = (float *)mm_alloc(cols * (int)sizeof(float));
		if (!cw) {
			mm_cells_free(cells, rowStart, rowLen);
			mp->err = 1;
			return -1;
		}
		for (c = 0; c < cols; c++) cw[c] = 0.0f;
		for (r = 0; r < rows; r++) {
			for (c = 0; c < rowLen[r]; c++) {
				MBox *cb = mm_box(mp, cells[rowStart[r] + c]);
				if (cb && cb->w > cw[c]) cw[c] = cb->w;
			}
		}
		idx = mm_new(mp, MB_V, level);
		{
			float down = 0.0f;
			for (r = 0; r < rows && idx >= 0; r++) {
				float rh = 0.0f, rd = 0.0f, x = 0.0f;
				for (c = 0; c < rowLen[r]; c++) {
					MBox *cb = mm_box(mp, cells[rowStart[r] + c]);
					if (!cb) continue;
					if (cb->h > rh) rh = cb->h;
					if (cb->d > rd) rd = cb->d;
				}
				for (c = 0; c < rowLen[r]; c++) {
					MBox *cb = mm_box(mp, cells[rowStart[r] + c]);
					float cx = x;
					if (!cb) continue;
					if (aligned) {
						if ((c & 1) == 0) cx = x + cw[c] - cb->w;
					} else if (!cases) {
						cx = x + (cw[c] - cb->w) * 0.5f;
					}
					cb->x = cx;
					cb->y = -down;
					mm_add_kid(mp, idx, cells[rowStart[r] + c]);
					x += cw[c] + colGap;
				}
				/* 行距按字号给基线步长 (≈1.2em), 行内容更高时再撑开 */
				{
					float step = rh + rd + size * 0.15f;
					if (step < size * 1.2f) step = size * 1.2f;
					down += step;
				}
			}
		}
		mm_layout_v(mp, idx);
		if (pmat) idx = mm_wrap_delim(mp, idx, L"(", 1, L")", 1, level);
		else if (bmat) idx = mm_wrap_delim(mp, idx, L"[", 1, L"]", 1, level);
		else if (vmat) idx = mm_wrap_delim(mp, idx, L"|", 1, L"|", 1, level);
		else if (cases) idx = mm_wrap_delim(mp, idx, L"{", 1, L"", 0, level);
		HeapFree(GetProcessHeap(), 0, cw);
	}
	mm_cells_free(cells, rowStart, rowLen);
	return idx;
}

static int mm_left_right(MP *mp, int level)
{
	wchar_t ld[4], rd[4];
	int ln = 0, rn = 0;
	int content, idx;
	if (!mm_delim(mp, ld, &ln)) return -1;
	content = mm_expr(mp, level, ST_RIGHT);
	if (content < 0) return -1;
	mm_skip_ws(mp);
	if (mp->i + 5 > mp->len || mp->p[mp->i] != '\\' ||
		mp->p[mp->i + 1] != 'r' || mp->p[mp->i + 2] != 'i' ||
		mp->p[mp->i + 3] != 'g' || mp->p[mp->i + 4] != 'h' ||
		mp->p[mp->i + 5] != 't') {
		mp->err = 1;
		return -1;
	}
	mp->i += 6;
	if (!mm_delim(mp, rd, &rn)) return -1;
	idx = mm_wrap_delim(mp, content, ld, ln, rd, rn, level);
	return idx;
}

/* 解析一个命令, 返回盒子 (可能已消耗参数) */
static int mm_command(MP *mp, int level)
{
	char nm[32];
	int single;
	if (!mm_cmd_name(mp, nm, (int)sizeof(nm), &single)) return -1;
	if (single) {
		char c = nm[0];
		if (c == '\\') { mp->err = 1; return -1; }   /* 行内不应出现换行 */
		if (c == ',') return mm_space(mp, 0.17f, level);
		if (c == ';') return mm_space(mp, 0.28f, level);
		if (c == ':') return mm_space(mp, 0.22f, level);
		if (c == '!') return mm_space(mp, -0.17f, level);
		if (c == '{' || c == '}' || c == '|' || c == '%' || c == '$' ||
			c == '&' || c == '_' || c == '#') {
			return mm_text_cp(mp, (uint32_t)(unsigned char)c, 0u, level);
		}
		mp->err = 1;
		return -1;
	}
	if (mm_name_is(nm, "frac") || mm_name_is(nm, "dfrac")) return mm_frac(mp, level);
	if (mm_name_is(nm, "sqrt")) return mm_sqrt(mp, level);
	if (mm_name_is(nm, "text")) return mm_text_cmd(mp, level);
	if (mm_name_is(nm, "left")) return mm_left_right(mp, level);
	if (mm_name_is(nm, "right") || mm_name_is(nm, "end")) {
		mp->err = 1;
		return -1;
	}
	if (mm_name_is(nm, "begin")) return mm_env(mp, level);
	if (mm_name_is(nm, "quad")) return mm_space(mp, 1.0f, level);
	if (mm_name_is(nm, "qquad")) return mm_space(mp, 2.0f, level);
	{
		int i;
		for (i = 0; i < (int)(sizeof(kSyms) / sizeof(kSyms[0])); i++) {
			if (mm_name_is(nm, kSyms[i].name)) {
				int n = 0, idx;
				float sz = 0.0f;
				int big = mm_name_is(nm, "sum") || mm_name_is(nm, "prod") ||
					mm_name_is(nm, "int") || mm_name_is(nm, "oint");
				int stacked = mm_name_is(nm, "sum") || mm_name_is(nm, "prod");
				while (kSyms[i].s[n]) n++;
				/* display 风格: 大运算符墨迹高度不小于数学字体
				 * displayOperatorMinHeight (Cambria Math 1.2207em) */
				if (big && mp->display && level <= MD_MATH_TEXT) {
					float base = mm_level_size(mp, level);
					float gh = 0.0f, gd = 0.0f, need = base * MM_DISPLAY_OP_EM;
					float gw = mp->fn(mp->fnUser, kSyms[i].s, n, base, 0u,
						&gh, &gd);
					if (gw >= 0.0f && gh > 0.01f && gh < need)
						sz = base * need / gh;
				}
				idx = mm_text_add(mp, kSyms[i].s, n, kSyms[i].flags,
					level, sz);
				if (idx >= 0) {
					MBox *b = mm_box(mp, idx);
					if (b) {
						b->cls = (uint8_t)mm_sym_class(nm);
						if (big) {
							b->bigop = 1;
							b->limits = (uint8_t)(stacked ? 1 : 0);
						}
					}
				}
				return idx;
			}
		}
	}
	mp->err = 1;
	return -1;
}

/* 无脚本的原子 */
static int mm_primary(MP *mp, int level)
{
	char c;
	mm_skip_ws(mp);
	if (mp->i >= mp->len) { mp->err = 1; return -1; }
	c = mp->p[mp->i];
	if (c == '{') {
		int b;
		mp->i++;
		b = mm_expr(mp, level, ST_BRACE);
		mm_skip_ws(mp);
		if (mp->i >= mp->len || mp->p[mp->i] != '}') { mp->err = 1; return -1; }
		mp->i++;
		return b;
	}
	if (c == '(' || c == '[') return mm_delim_group(mp, level);
	if (c == '\\') return mm_command(mp, level);
	if (c == '&' || c == '}' || c == ']' || c == '^' || c == '_') {
		mp->err = 1;
		return -1;
	}
	return mm_plain_run(mp, level);
}

/* 原子 + 上下标 */
static int mm_atom(MP *mp, int level)
{
	int base = mm_primary(mp, level);
	int sup = -1, sub = -1;
	if (base < 0) return -1;
	for (;;) {
		char c;
		mm_skip_ws(mp);
		if (mp->i >= mp->len) break;
		c = mp->p[mp->i];
		if (c != '^' && c != '_') break;
		mp->i++;
		if (c == '^') {
			if (sup >= 0) { mp->err = 1; return -1; }
			sup = mm_arg(mp, mm_script_level(level));
			if (sup < 0) return -1;
		} else {
			if (sub >= 0) { mp->err = 1; return -1; }
			sub = mm_arg(mp, mm_script_level(level));
			if (sub < 0) return -1;
		}
	}
	if (sup < 0 && sub < 0) return base;
	{
		MBox *bb = mm_box(mp, base);
		/* display 风格的 \sum/\prod: 上下限居中排在正上/正下方 */
		if (bb && bb->limits && mp->display && level <= MD_MATH_TEXT) {
			int idx = mm_new(mp, MB_V, level);
			float size = mm_level_size(mp, level);
			float gap = size * MM_LIMIT_GAP_EM, width;
			MBox *sbox = sup >= 0 ? mm_box(mp, sup) : NULL;
			MBox *ubox = sub >= 0 ? mm_box(mp, sub) : NULL;
			if (idx < 0) return -1;
			width = bb->w;
			if (sbox && sbox->w > width) width = sbox->w;
			if (ubox && ubox->w > width) width = ubox->w;
			bb->x = (width - bb->w) * 0.5f;
			bb->y = 0.0f;
			mm_add_kid(mp, idx, base);
			if (sbox) {
				sbox->x = (width - sbox->w) * 0.5f;
				sbox->y = bb->h + gap + sbox->d;
				mm_add_kid(mp, idx, sup);
			}
			if (ubox) {
				ubox->x = (width - ubox->w) * 0.5f;
				ubox->y = -(bb->d + gap + ubox->h);
				mm_add_kid(mp, idx, sub);
			}
			mm_layout_v(mp, idx);
			{
				MBox *vb = mm_box(mp, idx);
				if (vb) vb->w = mm_max2(vb->w, width);
			}
			return idx;
		}
	}
	{
		int idx = mm_new(mp, MB_V, level);
		MBox *bb = mm_box(mp, base);
		float size = mm_level_size(mp, level);
		float sw = 0.0f;
		if (idx < 0) return -1;
		bb->x = 0.0f;
		bb->y = 0.0f;
		mm_add_kid(mp, idx, base);
		if (sup >= 0) {
			MBox *sb = mm_box(mp, sup);
			float sy = size * MM_SUP_SHIFT_EM;
			if (bb->bigop) {
				float f = bb->h - size * 0.55f;
				if (f > sy) sy = f;
			}
			sb->x = bb->w + size * 0.10f;   /* 上下标与主体留缝 */
			sb->y = sy;
			if (sb->w > sw) sw = sb->w;
			mm_add_kid(mp, idx, sup);
		}
		if (sub >= 0) {
			MBox *sb = mm_box(mp, sub);
			float sy = -size * MM_SUB_SHIFT_EM;
			if (bb->bigop) {
				float f = -(bb->d - size * 0.5f);
				if (f < sy) sy = f;
			}
			sb->x = bb->w + size * 0.10f;
			sb->y = sy;
			if (sb->w > sw) sw = sb->w;
			mm_add_kid(mp, idx, sub);
		}
		/* 同时有上下标时保证最小间隙 (subSuperscriptGapMin) */
		if (sup >= 0 && sub >= 0) {
			MBox *sb = mm_box(mp, sup);
			MBox *ub = mm_box(mp, sub);
			float gap = size * MM_SUBSUP_GAP_EM;
			float supBottom = sb->y - sb->d;
			float subTop = ub->y + ub->h;
			if (supBottom - subTop < gap) {
				float d = gap - (supBottom - subTop);
				sb->y += d * 0.5f;
				ub->y -= d * 0.5f;
			}
		}
		mm_layout_v(mp, idx);
		{
			MBox *vb = mm_box(mp, idx);
			if (vb) vb->w = mm_max2(vb->w, bb->w + size * 0.10f + sw);
		}
		return idx;
	}
}

static int mm_arg(MP *mp, int level)
{
	mm_skip_ws(mp);
	if (mp->i >= mp->len) { mp->err = 1; return -1; }
	if (mp->p[mp->i] == '{') {
		int b;
		mp->i++;
		b = mm_expr(mp, level, ST_BRACE);
		mm_skip_ws(mp);
		if (mp->i >= mp->len || mp->p[mp->i] != '}') { mp->err = 1; return -1; }
		mp->i++;
		return b;
	}
	return mm_primary(mp, level);
}

static int mm_expr(MP *mp, int level, uint32_t stops)
{
	int idx;
	if (mp->depth++ > MM_MAX_DEPTH) {
		mp->depth--;
		mp->err = 1;
		return -1;
	}
	idx = mm_new(mp, MB_H, level);
	if (idx >= 0) {
		int prev = -1;
		while (!mp->err) {
			int atom, cur;
			MBox *ab;
			mm_skip_ws(mp);
			if (mm_at_stop(mp, stops)) break;
			if (mp->i >= mp->len) break;
			atom = mm_atom(mp, level);
			if (atom < 0) break;
			ab = mm_box(mp, atom);
			cur = ab ? ab->cls : MC_ORD;
			/* 表达式/括号/关系符后的 +- 为一元符号: 不当作二元加间距 */
			if (cur == MC_BIN && (prev < 0 || prev == MC_OPEN ||
				prev == MC_BIN || prev == MC_REL || prev == MC_PUNCT ||
				prev == MC_OP))
				cur = MC_ORD;
			if (prev >= 0) {
				float em = mm_class_space(prev, cur);
				if (em > 0.0f) {
					int sp = mm_space(mp, em, level);
					if (sp >= 0) mm_add_kid(mp, idx, sp);
				}
			}
			mm_add_kid(mp, idx, atom);
			prev = cur;
		}
		mm_layout_h(mp, idx);
	}
	mp->depth--;
	return mp->err ? -1 : idx;
}

MdMath *MdMath_Layout(const char *utf8, int len, float size, int display,
	MdMathMeasureFn measure, void *measureUser)
{
	MdMath *m;
	MP mp;
	int root;
	if (!utf8 || len <= 0 || size <= 0.0f || !measure) return NULL;
	m = (MdMath *)mm_alloc((int)sizeof(MdMath));
	if (!m) return NULL;
	ZeroMemory(&mp, sizeof(mp));
	mp.m = m;
	mp.p = utf8;
	mp.len = len;
	mp.base = size;
	mp.display = display ? 1 : 0;
	mp.fn = measure;
	mp.fnUser = measureUser;
	root = mm_expr(&mp, MD_MATH_DISPLAY, 0);
	mm_skip_ws(&mp);
	if (mp.err || root < 0 || mp.i != len) {
		MdMath_Free(m);
		return NULL;
	}
	m->root = root;
	{
		MBox *b = mm_box(&mp, root);
		if (!b) { MdMath_Free(m); return NULL; }
		m->w = b->w;
		m->h = b->h;
		m->d = b->d;
	}
	return m;
}

void MdMath_Free(MdMath *m)
{
	int i;
	if (!m) return;
	for (i = 0; i < m->n; i++) {
		if (m->box[i].kids) HeapFree(GetProcessHeap(), 0, m->box[i].kids);
	}
	if (m->box) HeapFree(GetProcessHeap(), 0, m->box);
	if (m->t) HeapFree(GetProcessHeap(), 0, m->t);
	HeapFree(GetProcessHeap(), 0, m);
}

float MdMath_Width(const MdMath *m) { return m ? m->w : 0.0f; }
float MdMath_Height(const MdMath *m) { return m ? m->h : 0.0f; }
float MdMath_Depth(const MdMath *m) { return m ? m->d : 0.0f; }

static void mm_draw_box(const MdMath *m, int idx, float x, float base,
	const MdMathDraw *d)
{
	const MBox *b;
	int k;
	if (!m || !d || idx < 0 || idx >= m->n) return;
	b = &m->box[idx];
	if (b->type == MB_TEXT) {
		if (d->text && b->tlen > 0)
			d->text(d->user, m->t + b->off, b->tlen, x, base, b->size,
				b->flags, b->h);
	} else if (b->type == MB_RULE) {
		if (d->rule)
			d->rule(d->user, x, base, b->w, b->h, b->d);
	}
	/* 盒子模型的 y 向上为正, 绘制基线 y 向下为正 */
	for (k = 0; k < b->kidN; k++) {
		const MBox *c = &m->box[b->kids[k]];
		mm_draw_box(m, b->kids[k], x + c->x, base - c->y, d);
	}
}

void MdMath_Draw(const MdMath *m, float x, float baseline,
	const MdMathDraw *draw)
{
	if (!m || !draw) return;
	if (m->n > 0) mm_draw_box(m, m->root, x, baseline, draw);
}
