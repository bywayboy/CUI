/*
 * ui_md_parse.c - Markdown 解析器 (CommonMark 核心 + GFM 常用扩展)
 *
 * 两遍结构:
 *   1) 块扫描: 行状态机 -> MdBlock 树; 叶子块只保存 raw 文本; 引用定义收集;
 *   2) 行内扫描: 对 raw 做实体/转义/强调/代码/链接/图片/表格单元格解析。
 *
 * 纯解析层: 只依赖 kernel32 (内存/CPU), 无 gfx/window, 可 headless 测试。
 * 设计见 docs/CUI-Markdown-设计方案.md §3。
 */
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#include "ui/ui_md_parse.h"

#define MD_MAX_DEPTH 32
#define MD_MAX_COLS  64
#define MD_FNV_OFFSET 2166136261u
#define MD_FNV_PRIME  16777619u

/* ── 内存与动态数组 ───────────────────────────────────────────────── */

static void *md_alloc(int n)
{
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)(n > 0 ? n : 1));
}

static void md_free(void *p)
{
	if (p) HeapFree(GetProcessHeap(), 0, p);
}

static void *md_realloc_arr(void *p, size_t bytes)
{
	if (p) return HeapReAlloc(GetProcessHeap(), 0, p, bytes);
	return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static int md_grow(void **arr, int *cap, int need, int elem)
{
	int nc;
	void *np;
	if (need <= *cap) return 1;
	nc = *cap > 0 ? *cap : 8;
	while (nc < need) nc *= 2;
	/* HeapReAlloc(NULL, ...) 在部分环境下行为不可靠, 空数组显式 HeapAlloc */
	np = md_realloc_arr(*arr, (SIZE_T)nc * (SIZE_T)elem);
	if (!np) return 0;
	*arr = np;
	*cap = nc;
	return 1;
}

/* ── UTF-8 ────────────────────────────────────────────────────────── */

static const char *md_utf8_next(const char *p, const char *end, uint32_t *cp)
{
	const unsigned char *u = (const unsigned char *)p;
	uint32_t c;
	int n, i;
	uint32_t minv;

	if (p >= end) { *cp = 0; return p; }
	c = u[0];
	if (c < 0x80) { *cp = c; return p + 1; }
	if ((c & 0xE0) == 0xC0) { n = 1; c &= 0x1F; minv = 0x80; }
	else if ((c & 0xF0) == 0xE0) { n = 2; c &= 0x0F; minv = 0x800; }
	else if ((c & 0xF8) == 0xF0) { n = 3; c &= 0x07; minv = 0x10000; }
	else { *cp = 0xFFFD; return p + 1; }
	if (p + 1 + n > end || u[0] == 0) { *cp = 0xFFFD; return p + 1; }
	for (i = 1; i <= n; i++) {
		if ((u[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return p + 1; }
		c = (c << 6) | (u[i] & 0x3F);
	}
	if (c < minv || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
		c = 0xFFFD;
	*cp = c;
	return p + 1 + n;
}

static uint32_t md_cp_at(const char *p, int len, int at)
{
	uint32_t cp = 0;
	if (at >= len) return ' ';
	md_utf8_next(p + at, p + len, &cp);
	return cp;
}

static uint32_t md_cp_before(const char *p, int at)
{
	int i;
	uint32_t cp = 0;
	if (at <= 0) return ' ';
	i = at - 1;
	while (i > 0 && ((unsigned char)p[i] & 0xC0) == 0x80) i--;
	md_utf8_next(p + i, p + at, &cp);
	return cp;
}

static int md_is_ws(uint32_t cp)
{
	return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
		cp == '\f' || cp == '\v' || cp == 0x3000;
}

static int md_is_punct(uint32_t cp)
{
	if (cp < 0x80) {
		return (cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) ||
			(cp >= 0x5B && cp <= 0x60) || (cp >= 0x7B && cp <= 0x7E);
	}
	return (cp >= 0xA1 && cp <= 0xBF) || (cp >= 0x2010 && cp <= 0x2027) ||
		(cp >= 0x2030 && cp <= 0x205E) || (cp >= 0x3000 && cp <= 0x303F) ||
		(cp >= 0xFF01 && cp <= 0xFF65);
}

static int md_is_ascii_punct(char c)
{
	return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
		(c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

static int md_is_space_char(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
		c == '\f' || c == '\v';
}

/* ── 行 ───────────────────────────────────────────────────────────── */

typedef struct MdLine {
	const char *p;
	int len;
	int srcOff, srcLen;
} MdLine;

typedef struct MdLines {
	MdLine *v;
	int n, cap;
} MdLines;

static int lines_add(MdLines *ls, const char *p, int len, int srcOff, int srcLen)
{
	if (!md_grow((void **)&ls->v, &ls->cap, ls->n + 1, (int)sizeof(MdLine)))
		return 0;
	ls->v[ls->n].p = p;
	ls->v[ls->n].len = len;
	ls->v[ls->n].srcOff = srcOff;
	ls->v[ls->n].srcLen = srcLen;
	ls->n++;
	return 1;
}

static int md_blank(const MdLine *ln)
{
	int i;
	for (i = 0; i < ln->len; i++)
		if (!md_is_space_char(ln->p[i])) return 0;
	return 1;
}

static int md_indent(const char *p, int len)
{
	int col = 0, i = 0;
	while (i < len) {
		if (p[i] == ' ') { col++; i++; }
		else if (p[i] == '\t') { col = (col / 4 + 1) * 4; i++; }
		else break;
	}
	return col;
}

static void md_strip_cols(const char *p, int len, int cols,
	const char **out, int *outLen)
{
	int col = 0, i = 0;
	while (i < len && col < cols) {
		if (p[i] == ' ') { col++; i++; }
		else if (p[i] == '\t') { col = (col / 4 + 1) * 4; i++; }
		else break;
	}
	*out = p + i;
	*outLen = len - i;
}

/* ── UTF-16 / 文本缓冲 ────────────────────────────────────────────── */

typedef struct MdWbuf {
	wchar_t *p;
	int n, cap;
} MdWbuf;

static int wbuf_reserve(MdWbuf *b, int need)
{
	int nc;
	wchar_t *np;
	if (need <= b->cap) return 1;
	nc = b->cap > 0 ? b->cap : 32;
	while (nc < need) nc *= 2;
	np = (wchar_t *)md_realloc_arr(b->p,
		(SIZE_T)nc * sizeof(wchar_t));
	if (!np) return 0;
	b->p = np;
	b->cap = nc;
	return 1;
}

static int wbuf_cp(MdWbuf *b, uint32_t cp)
{
	if (cp > 0xFFFF) {
		uint32_t v = cp - 0x10000;
		if (!wbuf_reserve(b, b->n + 2)) return 0;
		b->p[b->n++] = (wchar_t)(0xD800 + (v >> 10));
		b->p[b->n++] = (wchar_t)(0xDC00 + (v & 0x3FF));
		return 1;
	}
	if (!wbuf_reserve(b, b->n + 1)) return 0;
	b->p[b->n++] = (wchar_t)cp;
	return 1;
}

static int wbuf_utf8(MdWbuf *b, const char *p, int len)
{
	const char *end = p + len;
	while (p < end) {
		uint32_t cp;
		p = md_utf8_next(p, end, &cp);
		if (cp == 0) cp = 0xFFFD;
		if (!wbuf_cp(b, cp)) return 0;
	}
	return 1;
}

/* 字节缓冲 (段落 raw 文本) */
typedef struct MdBuf {
	char *p;
	int n, cap;
} MdBuf;

static int mbuf_add(MdBuf *b, const char *p, int len)
{
	int i;
	if (len < 0) len = 0;
	if (!md_grow((void **)&b->p, &b->cap, b->n + len + 1, 1)) return 0;
	for (i = 0; i < len; i++) b->p[b->n + i] = p[i];
	b->n += len;
	b->p[b->n] = 0;
	return 1;
}

/* ── URL / 语言 / 归一化 label 池 ─────────────────────────────────── */

static int md_url_put(MdDoc *d, const char *p, int len, int unescape,
	int *outLen)
{
	int off, n = 0, i;
	if (!d) return -1;
	if (!md_grow((void **)&d->urls, &d->urlsCap, d->urlsLen + len + 1, 1))
		return -1;
	off = d->urlsLen;
	for (i = 0; i < len; i++) {
		if (unescape && p[i] == '\\' && i + 1 < len &&
			md_is_ascii_punct(p[i + 1])) {
			d->urls[off + n++] = p[i + 1];
			i++;
		} else {
			d->urls[off + n++] = p[i];
		}
	}
	d->urls[off + n] = 0;
	d->urlsLen += n + 1;
	if (outLen) *outLen = n;
	return off;
}

static int md_label_norm(const char *p, int len, char *out, int cap)
{
	int i = 0, n = 0, pendingSpace = 0;
	if (cap <= 0) return 0;
	while (i < len) {
		char c = p[i++];
		if (md_is_space_char(c)) {
			if (n > 0) pendingSpace = 1;
			continue;
		}
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		if (pendingSpace && n + 1 < cap) out[n++] = ' ';
		pendingSpace = 0;
		if (n + 1 >= cap) break;
		out[n++] = c;
	}
	out[n] = 0;
	return n;
}

static uint32_t md_hash_bytes(const char *p, int n)
{
	uint32_t h = MD_FNV_OFFSET;
	int i;
	for (i = 0; i < n; i++) {
		h ^= (uint32_t)(unsigned char)p[i];
		h *= MD_FNV_PRIME;
	}
	return h;
}

static int md_memeq(const char *a, const char *b, int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (a[i] != b[i]) return 0;
	return 1;
}

static void md_ref_rehash(MdDoc *d)
{
	int i, cap = 16;
	while (cap < (d->refCount + 1) * 2) cap *= 2;
	if (d->refCap == cap) return;
	md_free(d->refSlots);
	d->refSlots = (int *)md_alloc(cap * (int)sizeof(int));
	d->refCap = d->refSlots ? cap : 0;
	if (!d->refSlots) return;
	for (i = 0; i < d->refCount; i++) {
		MdRef *r = &d->refs[i];
		int slot = (int)(r->hash & (uint32_t)(cap - 1));
		while (d->refSlots[slot]) slot = (slot + 1) & (cap - 1);
		d->refSlots[slot] = i + 1;
	}
}

static int md_ref_add(MdDoc *d, const char *label, int labelLen,
	const char *url, int urlLen)
{
	char norm[512];
	int nl, normOff, urlOff;
	uint32_t h;
	int slot;
	MdRef *r;
	MdRef *nr;

	nl = md_label_norm(label, labelLen, norm, (int)sizeof(norm));
	if (nl <= 0) return 0;
	h = md_hash_bytes(norm, nl);
	md_ref_rehash(d);
	if (!d->refSlots) return 0;
	slot = (int)(h & (uint32_t)(d->refCap - 1));
	while (d->refSlots[slot]) {
		MdRef *e = &d->refs[d->refSlots[slot] - 1];
		if (e->hash == h && e->normLen == nl &&
			md_memeq(d->urls + e->normOff, norm, nl)) {
			/* 重复定义: 第一个生效 */
			return 1;
		}
		slot = (slot + 1) & (d->refCap - 1);
	}
	normOff = md_url_put(d, norm, nl, 0, NULL);
	urlOff = md_url_put(d, url, urlLen, 1, NULL);
	if (normOff < 0 || urlOff < 0) return 0;
	if (!md_grow((void **)&d->refs, &d->refsCap, d->refCount + 1,
		(int)sizeof(MdRef)))
		return 0;
	nr = d->refs;
	r = &nr[d->refCount++];
	r->normOff = normOff;
	r->normLen = nl;
	r->urlOff = urlOff;
	r->urlLen = urlLen;
	r->hash = h;
	d->refSlots[slot] = d->refCount;
	return 1;
}

static int md_ref_lookup(const MdDoc *d, const char *label, int labelLen,
	int *urlOff, int *urlLen)
{
	char norm[512];
	int nl, slot;
	uint32_t h;
	nl = md_label_norm(label, labelLen, norm, (int)sizeof(norm));
	if (nl <= 0 || d->refCap <= 0 || !d->refSlots) return 0;
	h = md_hash_bytes(norm, nl);
	slot = (int)(h & (uint32_t)(d->refCap - 1));
	while (d->refSlots[slot]) {
		MdRef *e = &d->refs[d->refSlots[slot] - 1];
		if (e->hash == h && e->normLen == nl &&
			md_memeq(d->urls + e->normOff, norm, nl)) {
			*urlOff = e->urlOff;
			*urlLen = e->urlLen;
			return 1;
		}
		slot = (slot + 1) & (d->refCap - 1);
	}
	return 0;
}

/* ── 行内构建器 ───────────────────────────────────────────────────── */

typedef struct MdDelim {
	int pos;        /* 未压缩字符位置 */
	int count;      /* 剩余数量 */
	int orig;
	uint32_t ch;
	uint8_t canOpen, canClose;
} MdDelim;

typedef struct MdInl {
	MdDoc *d;
	wchar_t *t;
	uint16_t *st;
	int *lk;
	uint8_t *rm;
	int n, cap;
	MdDelim *dl;
	int dn, dcap;
	MdLink *links;
	int ln, lcap;
	MdImgRef *imgs;
	int imgCount, imgCap;
	MdMathRef *maths;
	int mathCount, mathCap;
} MdInl;

static void inl_init(MdInl *b, MdDoc *d)
{
	ZeroMemory(b, sizeof(*b));
	b->d = d;
}

static void inl_free(MdInl *b)
{
	md_free(b->t);
	md_free(b->st);
	md_free(b->lk);
	md_free(b->rm);
	md_free(b->dl);
	md_free(b->links);
	md_free(b->imgs);
	md_free(b->maths);
	ZeroMemory(b, sizeof(*b));
}

static int inl_reserve(MdInl *b, int need)
{
	int nc;
	wchar_t *t;
	uint16_t *st;
	int *lk;
	uint8_t *rm;
	if (need <= b->cap) return 1;
	nc = b->cap > 0 ? b->cap : 32;
	while (nc < need) nc *= 2;
	t = (wchar_t *)md_realloc_arr(b->t, (SIZE_T)nc * sizeof(wchar_t));
	if (!t) return 0;
	b->t = t;
	st = (uint16_t *)md_realloc_arr(b->st, (SIZE_T)nc * sizeof(uint16_t));
	if (!st) return 0;
	b->st = st;
	lk = (int *)md_realloc_arr(b->lk, (SIZE_T)nc * sizeof(int));
	if (!lk) return 0;
	b->lk = lk;
	rm = (uint8_t *)md_realloc_arr(b->rm, (SIZE_T)nc);
	if (!rm) return 0;
	b->rm = rm;
	b->cap = nc;
	return 1;
}

static int inl_put_w(MdInl *b, wchar_t w, uint16_t style, int link)
{
	if (!inl_reserve(b, b->n + 1)) return 0;
	b->t[b->n] = w;
	b->st[b->n] = style;
	b->lk[b->n] = link;
	b->rm[b->n] = 0;
	b->n++;
	return 1;
}

static int inl_put_cp(MdInl *b, uint32_t cp, uint16_t style, int link)
{
	if (cp > 0xFFFF) {
		uint32_t v = cp - 0x10000;
		if (!inl_put_w(b, (wchar_t)(0xD800 + (v >> 10)), style, link))
			return 0;
		return inl_put_w(b, (wchar_t)(0xDC00 + (v & 0x3FF)), style, link);
	}
	return inl_put_w(b, (wchar_t)cp, style, link);
}

static int inl_put_utf8(MdInl *b, const char *p, int len, uint16_t style,
	int link)
{
	const char *end = p + len;
	while (p < end) {
		uint32_t cp;
		p = md_utf8_next(p, end, &cp);
		if (cp == 0) cp = 0xFFFD;
		if (!inl_put_cp(b, cp, style, link)) return 0;
	}
	return 1;
}

static int inl_delim_add(MdInl *b, int pos, int count, uint32_t ch,
	int canOpen, int canClose)
{
	MdDelim *d;
	if (!md_grow((void **)&b->dl, &b->dcap, b->dn + 1,
		(int)sizeof(MdDelim)))
		return 0;
	d = &b->dl[b->dn++];
	d->pos = pos;
	d->count = count;
	d->orig = count;
	d->ch = ch;
	d->canOpen = (uint8_t)(canOpen ? 1 : 0);
	d->canClose = (uint8_t)(canClose ? 1 : 0);
	return 1;
}

static int inl_link_add(MdInl *b, int urlOff, int urlLen)
{
	MdLink *l;
	if (!md_grow((void **)&b->links, &b->lcap, b->ln + 1,
		(int)sizeof(MdLink)))
		return -1;
	l = &b->links[b->ln];
	l->start = -1;
	l->end = -1;
	l->urlOff = urlOff;
	l->urlLen = urlLen;
	return b->ln++;
}

/* 实体 */
typedef struct MdEntity { const char *name; uint32_t cp; } MdEntity;
static const MdEntity md_entities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' },
	{ "apos", '\'' }, { "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE },
	{ "trade", 0x2122 }, { "hellip", 0x2026 }, { "mdash", 0x2014 },
	{ "ndash", 0x2013 }, { "laquo", 0xAB }, { "raquo", 0xBB },
	{ "times", 0xD7 }, { "divide", 0xF7 }, { "deg", 0xB0 },
	{ "plusmn", 0xB1 }, { "frac12", 0xBD }, { "frac14", 0xBC },
	{ "frac34", 0xBE }
};

static int md_entity(const char *p, int len, int *used, uint32_t *cp)
{
	int i, semi = -1, n;
	if (len < 3 || p[0] != '&') return 0;
	for (i = 1; i < len && i <= 34; i++) {
		if (p[i] == ';') { semi = i; break; }
		if (p[i] == ' ' || p[i] == '\n') return 0;
	}
	if (semi < 0) return 0;
	if (p[1] == '#') {
		uint32_t v = 0;
		int base = 10, k = 2;
		if (k < semi && (p[k] == 'x' || p[k] == 'X')) { base = 16; k++; }
		if (k >= semi) return 0;
		for (; k < semi; k++) {
			int d;
			char c = p[k];
			if (c >= '0' && c <= '9') d = c - '0';
			else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
			else return 0;
			v = v * (uint32_t)base + (uint32_t)d;
			if (v > 0x10FFFF) return 0;
		}
		if (v == 0 || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
		*cp = v;
		*used = semi + 1;
		return 1;
	}
	n = semi - 1;
	for (i = 0; i < (int)(sizeof(md_entities) / sizeof(md_entities[0])); i++) {
		const char *nm = md_entities[i].name;
		int k = 0;
		while (nm[k] && k < n && nm[k] == p[1 + k]) k++;
		if (nm[k] == 0 && k == n) {
			*cp = md_entities[i].cp;
			*used = semi + 1;
			return 1;
		}
	}
	return 0;
}

/* 强调 flanking */
static void md_flank(const char *p, int len, int start, int runLen,
	int underscore, int *canOpen, int *canClose)
{
	uint32_t prev = md_cp_before(p, start);
	uint32_t next = md_cp_at(p, len, start + runLen);
	int wsPrev = md_is_ws(prev);
	int wsNext = md_is_ws(next);
	int pPrev = md_is_punct(prev);
	int pNext = md_is_punct(next);
	int left = !wsNext && (!pNext || wsPrev || pPrev);
	int right = !wsPrev && (!pPrev || wsNext || pNext);
	if (underscore) {
		*canOpen = left && (!right || pPrev);
		*canClose = right && (!left || pNext);
	} else {
		*canOpen = left;
		*canClose = right;
	}
}

/* 找到 [ ... ] 的配对位置 (p[start]=='['), 返回 ']' 下标或 -1 */
static int md_scan_bracket(const char *p, int len, int start)
{
	int i = start + 1, depth = 1;
	while (i < len) {
		char c = p[i];
		if (c == '\\') { i += 2; continue; }
		if (c == '`') {
			int n = 0, j, found = -1;
			while (i + n < len && p[i + n] == '`') n++;
			j = i + n;
			while (j < len) {
				if (p[j] == '`') {
					int m = 0;
					while (j + m < len && p[j + m] == '`') m++;
					if (m == n) { found = j; break; }
					j += m;
				} else j++;
			}
			if (found >= 0) { i = found + n; continue; }
			i += n;
			continue;
		}
		if (c == '[') depth++;
		else if (c == ']') {
			depth--;
			if (depth == 0) return i;
		}
		i++;
	}
	return -1;
}

/* 解析 (dest "title")；p[open]=='('；返回 1 并给出 dest 区间与下一个位置。
 * tOff/tLen 可为 NULL; 无 title 时返回 -1/0。 */
static int md_parse_dest(const char *p, int len, int open,
	int *dOff, int *dLen, int *tOff, int *tLen, int *next)
{
	int i = open + 1, s, depth;
	if (tOff) *tOff = -1;
	if (tLen) *tLen = 0;
	while (i < len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
	if (i >= len) return 0;
	if (p[i] == '<') {
		s = ++i;
		while (i < len && p[i] != '>' && p[i] != '\n') i++;
		if (i >= len || p[i] != '>') return 0;
		*dOff = s;
		*dLen = i - s;
		i++;
	} else {
		s = i;
		depth = 0;
		while (i < len) {
			char c = p[i];
			if (c == '\\' && i + 1 < len) { i += 2; continue; }
			if (c == '(') depth++;
			else if (c == ')') {
				if (depth == 0) break;
				depth--;
			} else if (c == ' ' || c == '\t' || c == '\n') break;
			i++;
		}
		if (depth != 0) return 0;
		*dOff = s;
		*dLen = i - s;
	}
	if (*dLen <= 0) return 0;
	while (i < len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
	if (i < len && (p[i] == '"' || p[i] == '\'' || p[i] == '(')) {
		char openq = p[i];
		char closeq = openq == '(' ? ')' : openq;
		int ts;
		i++;
		ts = i;
		while (i < len && p[i] != closeq) {
			if (p[i] == '\\' && i + 1 < len) i += 2;
			else i++;
		}
		if (i >= len) return 0;
		if (tOff) *tOff = ts;
		if (tLen) *tLen = i - ts;
		i++;
		while (i < len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n')) i++;
	}
	if (i >= len || p[i] != ')') return 0;
	*next = i + 1;
	return 1;
}

/* 强调匹配 */
static int md_rule3(const MdDelim *o, const MdDelim *c)
{
	if (!o->canClose && !c->canOpen) return 0;
	if ((o->orig + c->orig) % 3 != 0) return 0;
	return !(o->orig % 3 == 0 && c->orig % 3 == 0);
}

static void inl_emph(MdInl *b)
{
	int bottomStar = -1, bottomUnder = -1, bottomTilde = -1;
	int j;
	for (j = 0; j < b->dn; j++) {
		MdDelim *c = &b->dl[j];
		int idx = c->ch == '*' ? 0 : (c->ch == '_' ? 1 : 2);
		int *bottom = idx == 0 ? &bottomStar
			: (idx == 1 ? &bottomUnder : &bottomTilde);
		while (c->count > 0 && c->canClose) {
			int k = -1, x;
			MdDelim *o = NULL;
			int use, i, innerStart, innerEnd;
			for (x = j - 1; x >= 0 && x >= *bottom; x--) {
				MdDelim *t = &b->dl[x];
				if (t->ch != c->ch || !t->canOpen || t->count <= 0) continue;
				if (md_rule3(t, c)) continue;
				if (c->ch == '~' && (t->count < 2 || c->count < 2)) continue;
				k = x;
				o = t;
				break;
			}
			if (k < 0) { *bottom = j; break; }
			if (c->ch == '~') {
				use = 2;
			} else {
				use = (o->count >= 2 && c->count >= 2) ? 2 : 1;
			}
			{
				uint16_t mask = c->ch == '~' ? MD_R_STRIKE
					: (use == 2 ? MD_R_BOLD : MD_R_ITALIC);
				/* 标记消耗掉的定界符字符 */
				for (i = o->pos + o->count - use; i < o->pos + o->count; i++)
					if (i >= 0 && i < b->n) b->rm[i] = 1;
				{
					int cpos = c->pos + (c->orig - c->count);
					for (i = cpos; i < cpos + use; i++)
						if (i >= 0 && i < b->n) b->rm[i] = 1;
				}
				innerStart = o->pos + o->count;
				innerEnd = c->pos + (c->orig - c->count);
				if (innerStart < 0) innerStart = 0;
				if (innerEnd > b->n) innerEnd = b->n;
				for (i = innerStart; i < innerEnd; i++)
					if (!b->rm[i]) b->st[i] |= mask;
			}
			o->count -= use;
			c->count -= use;
			if (o->count <= 0 && c->count <= 0) break;
			if (c->ch == '~') break;
		}
	}
}

static void inl_merge(MdInl *dst, MdInl *src, int linkId)
{
	int i;
	inl_emph(src);
	for (i = 0; i < src->n; i++) {
		if (src->rm[i]) continue;
		inl_put_w(dst, src->t[i], src->st[i], linkId);
	}
	inl_free(src);
}

static void inl_parse(MdInl *b, const char *p, int len, int allowLinks);

static int inl_link(MdInl *b, const char *p, int len, int *consumed)
{
	int close = md_scan_bracket(p, len, 0);
	int after, urlOff = -1, urlLen = 0, end, linkId;
	MdInl sub;
	int labelOff, labelLen;

	if (close < 0) return 0;
	after = close + 1;
	labelOff = 1;
	labelLen = close - 1;
	if (after < len && p[after] == '(') {
		int dOff, dLen, next;
		if (!md_parse_dest(p, len, after, &dOff, &dLen, NULL, NULL, &next)) return 0;
		urlOff = md_url_put(b->d, p + dOff, dLen, 1, &urlLen);
		if (urlOff < 0) return 0;
		end = next;
	} else if (after < len && p[after] == '[') {
		int c2 = md_scan_bracket(p, len, after);
		int l2Off = after + 1, l2Len;
		if (c2 < 0) return 0;
		l2Len = c2 - after - 1;
		if (l2Len == 0) { l2Off = 1; l2Len = close - 1; }
		if (!md_ref_lookup(b->d, p + l2Off, l2Len, &urlOff, &urlLen))
			return 0;
		end = c2 + 1;
	} else {
		if (!md_ref_lookup(b->d, p + 1, close - 1, &urlOff, &urlLen))
			return 0;
		end = after;
	}
	linkId = inl_link_add(b, urlOff, urlLen);
	if (linkId < 0) return 0;
	inl_init(&sub, b->d);
	inl_parse(&sub, p + labelOff, labelLen, 0);
	inl_merge(b, &sub, linkId);
	*consumed = end;
	return 1;
}

/* 图片尺寸属性: ![alt](url){width=320 height=200} (Pandoc/Markdown-Extra).
 * 键大小写不敏感, 支持 w/h 简写; 数值按 DIP. 返回消耗到的下标. */
static int md_attr_eq(const char *p, int n, const char *w)
{
	int i;
	for (i = 0; i < n; i++) {
		char a = p[i], b = w[i];
		if (!b) return 0;
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (a != b) return 0;
	}
	return w[n] == 0;
}

static int md_parse_img_attrs(const char *p, int len, int at, int *w, int *h)
{
	int i = at, end = -1;
	if (i >= len || p[i] != '{') return at;
	for (; i < len; i++) {
		if (p[i] == '\n') return at;
		if (p[i] == '}') { end = i; break; }
	}
	if (end < 0) return at;
	i = at + 1;
	while (i < end) {
		int ks, ke, val = 0, any = 0;
		while (i < end && (p[i] == ' ' || p[i] == '\t' || p[i] == ',')) i++;
		ks = i;
		while (i < end && p[i] != '=' && p[i] != ' ' && p[i] != '\t' &&
			p[i] != ',') i++;
		ke = i;
		while (i < end && (p[i] == ' ' || p[i] == '\t')) i++;
		if (i < end && p[i] == '=') {
			i++;
			while (i < end && (p[i] == ' ' || p[i] == '\t')) i++;
			while (i < end && p[i] >= '0' && p[i] <= '9') {
				val = val * 10 + (p[i] - '0');
				if (val > 100000) val = 100000;
				any = 1;
				i++;
			}
			if (any) {
				if (md_attr_eq(p + ks, ke - ks, "width") ||
					md_attr_eq(p + ks, ke - ks, "w"))
					*w = val;
				else if (md_attr_eq(p + ks, ke - ks, "height") ||
					md_attr_eq(p + ks, ke - ks, "h"))
					*h = val;
			}
			while (i < end && p[i] != ',' && p[i] != ' ' && p[i] != '\t')
				i++;
		} else if (ke == ks) {
			i++;
		}
	}
	return end + 1;
}

static int inl_img_add(MdInl *b, int start, int len, int urlOff, int urlLen,
	int w, int h)
{
	MdImgRef *r;
	if (!md_grow((void **)&b->imgs, &b->imgCap, b->imgCount + 1,
		(int)sizeof(MdImgRef)))
		return 0;
	r = &b->imgs[b->imgCount++];
	r->start = start;
	r->len = len;
	r->urlOff = urlOff;
	r->urlLen = urlLen;
	r->w = w;
	r->h = h;
	return 1;
}

static int inl_math_add(MdInl *b, int start, int len, int srcOff, int srcLen)
{
	MdMathRef *r;
	if (!md_grow((void **)&b->maths, &b->mathCap, b->mathCount + 1,
		(int)sizeof(MdMathRef)))
		return 0;
	r = &b->maths[b->mathCount++];
	r->start = start;
	r->len = len;
	r->srcOff = srcOff;
	r->srcLen = srcLen;
	return 1;
}

/* $...$: 源码区间写入 text16 (布局失败时作为回退) 与 urls 池 (LaTeX)。
 * 开定界符后不能是空白; 闭定界符前不能是空白, 后不能紧跟数字 (货币规则)。 */
static int inl_math_dollar(MdInl *b, const char *p, int len, int *consumed)
{
	int i, end = -1, srcOff, srcLen, start;
	if (len < 3 || p[0] != '$' || p[1] == '$') return 0;
	if (md_is_space_char(p[1])) return 0;
	for (i = 1; i < len; i++) {
		if (p[i] == '\n') return 0;
		if (p[i] == '\\' && i + 1 < len) { i++; continue; }
		if (p[i] != '$') continue;
		if (md_is_space_char(p[i - 1])) continue;
		if (i + 1 < len && p[i + 1] >= '0' && p[i + 1] <= '9') continue;
		end = i;
		break;
	}
	if (end < 0) return 0;
	srcLen = end - 1;
	if (srcLen <= 0) return 0;
	srcOff = md_url_put(b->d, p + 1, srcLen, 0, NULL);
	if (srcOff < 0) return 0;
	start = b->n;
	if (!inl_put_utf8(b, p + 1, srcLen, 0, -1)) return 0;
	if (!inl_math_add(b, start, b->n - start, srcOff, srcLen)) return 0;
	*consumed = end + 1;
	return 1;
}

/* \(...\): 与 $...$ 相同的产物; 不跨行。 */
static int inl_math_paren(MdInl *b, const char *p, int len, int *consumed)
{
	int i, end = -1, srcOff, srcLen, start;
	if (len < 5 || p[0] != '\\' || p[1] != '(') return 0;
	for (i = 2; i + 1 < len; i++) {
		if (p[i] == '\n') return 0;
		if (p[i] == '\\' && p[i + 1] == ')') { end = i; break; }
		if (p[i] == '\\') i++;
	}
	if (end < 0 || end <= 2) return 0;
	srcLen = end - 2;
	srcOff = md_url_put(b->d, p + 2, srcLen, 0, NULL);
	if (srcOff < 0) return 0;
	start = b->n;
	if (!inl_put_utf8(b, p + 2, srcLen, 0, -1)) return 0;
	if (!inl_math_add(b, start, b->n - start, srcOff, srcLen)) return 0;
	*consumed = end + 2;
	return 1;
}

static int inl_image(MdInl *b, const char *p, int len, int *consumed)
{
	int close, after, urlOff = -1, urlLen = 0, end;
	int aw = 0, ah = 0;
	MdInl sub;

	if (len < 3 || p[0] != '!' || p[1] != '[') return 0;
	close = md_scan_bracket(p + 1, len - 1, 0);
	if (close < 0) return 0;
	close += 1;    /* 变回 p 的下标 */
	after = close + 1;
	if (after < len && p[after] == '(') {
		int dOff, dLen, next;
		if (!md_parse_dest(p, len, after, &dOff, &dLen, NULL, NULL, &next)) return 0;
		urlOff = md_url_put(b->d, p + dOff, dLen, 1, &urlLen);
		if (urlOff < 0) return 0;
		end = next;
	} else if (after < len && p[after] == '[') {
		int c2 = md_scan_bracket(p, len, after);
		int l2Off = after + 1, l2Len;
		if (c2 < 0) return 0;
		l2Len = c2 - after - 1;
		if (l2Len == 0) { l2Off = 2; l2Len = close - 2; }
		if (!md_ref_lookup(b->d, p + l2Off, l2Len, &urlOff, &urlLen))
			return 0;
		end = c2 + 1;
	} else {
		if (!md_ref_lookup(b->d, p + 2, close - 2, &urlOff, &urlLen))
			return 0;
		end = after;
	}
	end = md_parse_img_attrs(p, len, end, &aw, &ah);
	inl_init(&sub, b->d);
	inl_parse(&sub, p + 2, close - 2, 0);
	{
		int start = b->n;
		inl_merge(b, &sub, -1);
		inl_img_add(b, start, b->n - start, urlOff, urlLen, aw, ah);
	}
	*consumed = end;
	return 1;
}

static int inl_autolink(MdInl *b, const char *p, int len, int *consumed)
{
	int end = -1, i, isUrl = 0, isMail = 0, linkId;
	const char *s;
	int slen, urlOff, urlLen;

	if (len < 3 || p[0] != '<') return 0;
	for (i = 1; i < len; i++) {
		if (p[i] == '>') { end = i; break; }
		if (p[i] == '<' || p[i] == '\n') return 0;
	}
	if (end < 0) return 0;
	s = p + 1;
	slen = end - 1;
	if (slen <= 0) return 0;
	{
		int colon = -1;
		for (i = 0; i < slen; i++) {
			if (s[i] == ':') { colon = i; break; }
			if (s[i] == ' ') break;
		}
		if (colon > 0) {
			int ok = 1;
			for (i = 0; i < colon; i++) {
				char c = s[i];
				int alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
					(c >= '0' && c <= '9');
				if (i == 0) {
					if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) ok = 0;
				} else if (!alnum && c != '+' && c != '-' && c != '.') {
					ok = 0;
				}
			}
			if (ok) isUrl = 1;
		}
	}
	if (!isUrl) {
		int at = -1, dot = 0, ok = 1;
		for (i = 0; i < slen; i++) {
			if (s[i] == ' ') { ok = 0; break; }
			if (s[i] == '@') at = i;
			else if (s[i] == '.' && at >= 0) dot = 1;
		}
		if (ok && at > 0 && at < slen - 1 && dot) isMail = 1;
	}
	if (!isUrl && !isMail) return 0;

	if (isMail) {
		char *tmp = (char *)md_alloc(slen + 8);
		if (!tmp) return 0;
		tmp[0] = 'm'; tmp[1] = 'a'; tmp[2] = 'i'; tmp[3] = 'l';
		tmp[4] = 't'; tmp[5] = 'o'; tmp[6] = ':';
		for (i = 0; i < slen; i++) tmp[7 + i] = s[i];
		urlOff = md_url_put(b->d, tmp, slen + 7, 0, &urlLen);
		md_free(tmp);
	} else {
		urlOff = md_url_put(b->d, s, slen, 0, &urlLen);
	}
	if (urlOff < 0) return 0;
	linkId = inl_link_add(b, urlOff, urlLen);
	if (linkId < 0) return 0;
	if (!inl_put_utf8(b, s, slen, MD_R_AUTOLINK, linkId)) return 0;
	*consumed = end + 1;
	return 1;
}

static int inl_code_span(MdInl *b, const char *p, int len, int *ip)
{
	int i = *ip, n = 0, j, found = -1, s, e, k;
	while (i + n < len && p[i + n] == '`') n++;
	j = i + n;
	while (j < len) {
		if (p[j] == '`') {
			int m = 0;
			while (j + m < len && p[j + m] == '`') m++;
			if (m == n) { found = j; break; }
			j += m;
		} else j++;
	}
	if (found < 0) {
		for (k = 0; k < n; k++) inl_put_w(b, L'`', 0, -1);
		*ip = i + n;
		return 1;
	}
	s = i + n;
	e = found;
	if (e - s >= 2 && p[s] == ' ' && p[e - 1] == ' ') {
		int allSpace = 1;
		for (k = s; k < e; k++)
			if (p[k] != ' ') { allSpace = 0; break; }
		if (!allSpace) { s++; e--; }
	}
	for (k = s; k < e; ) {
		if (p[k] == '\n') { inl_put_w(b, L' ', MD_R_CODE, -1); k++; }
		else {
			uint32_t cp = 0;
			const char *nx = md_utf8_next(p + k, p + e, &cp);
			if (cp == 0) cp = 0xFFFD;
			inl_put_cp(b, cp, MD_R_CODE, -1);
			k = (int)(nx - p);
		}
	}
	*ip = found + n;
	return 1;
}

static void inl_parse(MdInl *b, const char *p, int len, int allowLinks)
{
	int i = 0;
	while (i < len) {
		char c = p[i];
		if (c == '$') {
			int used = 0;
			if (inl_math_dollar(b, p + i, len - i, &used)) {
				i += used;
				continue;
			}
		}
		if (c == '\\' && i + 1 < len && p[i + 1] == '(') {
			int used = 0;
			if (inl_math_paren(b, p + i, len - i, &used)) {
				i += used;
				continue;
			}
		}
		if (c == '\\' && i + 1 < len) {
			char n = p[i + 1];
			if (n == '\n') { inl_put_w(b, L'\n', 0, -1); i += 2; continue; }
			if (md_is_ascii_punct(n)) {
				inl_put_cp(b, (uint32_t)(unsigned char)n, 0, -1);
				i += 2;
				continue;
			}
		}
		if (c == '`') {
			inl_code_span(b, p, len, &i);
			continue;
		}
		if (c == '!' && i + 1 < len && p[i + 1] == '[') {
			int used = 0;
			if (inl_image(b, p + i, len - i, &used)) { i += used; continue; }
		}
		if (c == '[' && allowLinks) {
			int used = 0;
			if (inl_link(b, p + i, len - i, &used)) { i += used; continue; }
		}
		if (c == '<') {
			int used = 0;
			if (inl_autolink(b, p + i, len - i, &used)) { i += used; continue; }
		}
		if (c == '&') {
			uint32_t cp;
			int used = 0;
			if (md_entity(p + i, len - i, &used, &cp)) {
				inl_put_cp(b, cp, 0, -1);
				i += used;
				continue;
			}
		}
		if (c == '*' || c == '_' || c == '~') {
			int n = 1, k, pos, canOpen = 0, canClose = 0;
			while (i + n < len && p[i + n] == c) n++;
			md_flank(p, len, i, n, c == '_', &canOpen, &canClose);
			pos = b->n;
			for (k = 0; k < n; k++) inl_put_cp(b, (uint32_t)(unsigned char)c, 0, -1);
			if (!(c == '~' && n < 2))
				inl_delim_add(b, pos, n, (uint32_t)(unsigned char)c,
					canOpen, canClose);
			i += n;
			continue;
		}
		if (c == '\n') {
			int sp = 0;
			while (b->n > 0 && b->t[b->n - 1] == ' ') { b->n--; sp++; }
			if (sp >= 2) inl_put_w(b, L'\n', 0, -1);
			else {
				if (b->n > 0 && b->t[b->n - 1] != ' ')
					inl_put_w(b, L' ', 0, -1);
			}
			i++;
			continue;
		}
		{
			uint32_t cp;
			const char *nx = md_utf8_next(p + i, p + len, &cp);
			if (cp == 0) cp = 0xFFFD;
			inl_put_cp(b, cp, 0, -1);
			i = (int)(nx - p);
		}
	}
	while (b->n > 0 && (b->t[b->n - 1] == ' ' || b->t[b->n - 1] == '\t'))
		b->n--;
}

static void inl_to_leaf(MdInl *b, wchar_t **outT, int *outLen,
	MdRun **outR, int *outRN, MdLink **outL, int *outLN)
{
	wchar_t *t;
	int n = 0, rn = 0, rcap = 0, i, k;
	MdRun *runs = NULL;

	inl_emph(b);
	t = (wchar_t *)md_alloc((b->n + 1) * (int)sizeof(wchar_t));
	if (!t) {
		*outT = NULL; *outLen = 0; *outR = NULL; *outRN = 0;
		*outL = NULL; *outLN = 0;
		return;
	}
	for (i = 0; i < b->n; i++) {
		uint16_t st;
		int lk;
		if (b->rm[i]) continue;
		st = b->st[i];
		lk = b->lk[i];
		if (lk >= 0) st |= MD_R_LINK;
		if (rn > 0 && runs[rn - 1].style == st && runs[rn - 1].link == lk &&
			runs[rn - 1].start + runs[rn - 1].len == n) {
			runs[rn - 1].len++;
		} else {
			if (!md_grow((void **)&runs, &rcap, rn + 1, (int)sizeof(MdRun))) {
				break;
			}
			runs[rn].start = n;
			runs[rn].len = 1;
			runs[rn].style = st;
			runs[rn].link = lk;
			rn++;
		}
		t[n++] = b->t[i];
	}
	t[n] = 0;
	for (k = 0; k < b->ln; k++) {
		b->links[k].start = -1;
		b->links[k].end = -1;
		for (i = 0; i < rn; i++) {
			if (runs[i].link == k) {
				if (b->links[k].start < 0) b->links[k].start = i;
				b->links[k].end = i + 1;
			}
		}
		if (b->links[k].start < 0) b->links[k].start = b->links[k].end = 0;
	}
	*outT = t;
	*outLen = n;
	*outR = runs;
	*outRN = rn;
	*outL = b->links;
	*outLN = b->ln;
	b->links = NULL;
	b->ln = 0;
	b->lcap = 0;
}

/* 行内图片记录随叶子块转移 (段落/标题) */
static void md_take_imgs(MdBlock *blk, MdInl *ib)
{
	blk->p.text.imgs = ib->imgs;
	blk->p.text.imgCount = ib->imgCount;
	blk->p.text.imgCap = ib->imgCap;
	ib->imgs = NULL;
	ib->imgCount = 0;
	ib->imgCap = 0;
}

/* 行内公式记录随叶子块转移 */
static void md_take_maths(MdBlock *blk, MdInl *ib)
{
	blk->p.text.maths = ib->maths;
	blk->p.text.mathCount = ib->mathCount;
	blk->p.text.mathCap = ib->mathCap;
	ib->maths = NULL;
	ib->mathCount = 0;
	ib->mathCap = 0;
}

/* ── 块扫描 ───────────────────────────────────────────────────────── */

static MdBlock *md_add_block(MdDoc *d, MdBlock *parent)
{
	MdBlock *arr, *b;
	int *cnt, *cap;
	if (parent) {
		arr = parent->p.kids.children;
		cnt = &parent->p.kids.childCount;
		cap = &parent->p.kids.childCap;
	} else {
		arr = d->blocks;
		cnt = &d->count;
		cap = &d->cap;
	}
	if (!md_grow((void **)&arr, cap, *cnt + 1, (int)sizeof(MdBlock)))
		return NULL;
	if (parent) parent->p.kids.children = arr;
	else d->blocks = arr;
	b = &arr[(*cnt)++];
	ZeroMemory(b, sizeof(*b));
	b->task = MD_TASK_NONE;
	b->tight = 1;
	return b;
}

static void md_set_span(MdBlock *b, const MdLines *ls, int start, int end)
{
	if (end <= start) {
		b->srcOff = start < ls->n ? ls->v[start].srcOff : 0;
		b->srcLen = 0;
		return;
	}
	b->srcOff = ls->v[start].srcOff;
	b->srcLen = ls->v[end - 1].srcOff + ls->v[end - 1].srcLen - b->srcOff;
}

static int md_atx(const MdLine *ln, int *level, int *cOff, int *cLen)
{
	int i = 0, n = 0, j, e, k;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	while (i + n < ln->len && ln->p[i + n] == '#') n++;
	if (n < 1 || n > 6) return 0;
	j = i + n;
	if (j < ln->len && ln->p[j] != ' ' && ln->p[j] != '\t') return 0;
	while (j < ln->len && (ln->p[j] == ' ' || ln->p[j] == '\t')) j++;
	e = ln->len;
	while (e > j && (ln->p[e - 1] == ' ' || ln->p[e - 1] == '\t')) e--;
	if (e > j && ln->p[e - 1] == '#') {
		k = e;
		while (k > j && ln->p[k - 1] == '#') k--;
		if (k == j || ln->p[k - 1] == ' ' || ln->p[k - 1] == '\t') {
			e = k;
			while (e > j && (ln->p[e - 1] == ' ' || ln->p[e - 1] == '\t')) e--;
		}
	}
	*level = n;
	*cOff = j;
	*cLen = e - j;
	return 1;
}

static int md_setext(const MdLine *ln, int *level)
{
	int i = 0, n = 0;
	char c;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	if (i >= ln->len) return 0;
	c = ln->p[i];
	if (c != '=' && c != '-') return 0;
	for (; i < ln->len; i++) {
		if (ln->p[i] == c) n++;
		else if (ln->p[i] != ' ' && ln->p[i] != '\t') return 0;
	}
	if (n < 1) return 0;
	*level = c == '=' ? 1 : 2;
	return 1;
}

static int md_hr(const MdLine *ln)
{
	int i = 0, n = 0;
	char c;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	if (i >= ln->len) return 0;
	c = ln->p[i];
	if (c != '*' && c != '-' && c != '_') return 0;
	for (; i < ln->len; i++) {
		if (ln->p[i] == c) n++;
		else if (ln->p[i] != ' ' && ln->p[i] != '\t') return 0;
	}
	return n >= 3;
}

static int md_fence_open(const MdLine *ln, char *fch, int *flen,
	int *indent, int *infoOff, int *infoLen)
{
	int i = 0, n = 0, s, k;
	char c;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	if (i >= ln->len) return 0;
	c = ln->p[i];
	if (c != '`' && c != '~') return 0;
	while (i + n < ln->len && ln->p[i + n] == c) n++;
	if (n < 3) return 0;
	if (c == '`') {
		for (k = i + n; k < ln->len; k++)
			if (ln->p[k] == '`') return 0;
	}
	*fch = c;
	*flen = n;
	*indent = i;
	s = i + n;
	while (s < ln->len && (ln->p[s] == ' ' || ln->p[s] == '\t')) s++;
	*infoOff = s;
	*infoLen = ln->len - s;
	return 1;
}

static int md_fence_close(const MdLine *ln, char fch, int flen)
{
	int i = 0, n = 0, k;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	while (i + n < ln->len && ln->p[i + n] == fch) n++;
	if (n < flen) return 0;
	k = i + n;
	while (k < ln->len && (ln->p[k] == ' ' || ln->p[k] == '\t')) k++;
	return k == ln->len;
}

static int md_quote(const MdLine *ln, int *contentOff)
{
	int i = 0;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	if (i >= ln->len || ln->p[i] != '>') return 0;
	i++;
	if (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) i++;
	*contentOff = i;
	return 1;
}

/* ── 容器指令 (::: 与 GitHub > [!NOTE]) ─────────────────────────── */

static int md_word_eq(const char *p, int n, const char *w)
{
	int i;
	for (i = 0; i < n; i++) {
		char a = p[i], b = w[i];
		if (!b) return 0;
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (a != b) return 0;
	}
	return w[n] == 0;
}

static int md_box_kind_of(const char *p, int n)
{
	if (md_word_eq(p, n, "note") || md_word_eq(p, n, "info"))
		return MD_BOX_NOTE;
	if (md_word_eq(p, n, "tip") || md_word_eq(p, n, "hint"))
		return MD_BOX_TIP;
	if (md_word_eq(p, n, "warning") || md_word_eq(p, n, "attention"))
		return MD_BOX_WARNING;
	if (md_word_eq(p, n, "danger") || md_word_eq(p, n, "caution") ||
		md_word_eq(p, n, "error"))
		return MD_BOX_DANGER;
	return -1;
}

static const char *md_box_default_title(int kind)
{
	switch (kind) {
	case MD_BOX_TIP: return "提示";
	case MD_BOX_WARNING: return "警告";
	case MD_BOX_DANGER: return "危险";
	default: return "注意";
	}
}

static int md_box_title_set(MdDoc *d, MdBlock *b, const char *p, int len,
	int kind)
{
	int n = 0, off;
	if (len > 0) {
		off = md_url_put(d, p, len, 0, &n);
	} else {
		const char *t = md_box_default_title(kind);
		int tl = 0;
		while (t[tl]) tl++;
		off = md_url_put(d, t, tl, 0, &n);
	}
	if (off < 0) return 0;
	b->u.box.titleOff = off;
	b->u.box.titleLen = n;
	return 1;
}

/* 开启行: 最多 3 空格 + >=3 个冒号 + info */
static int md_colon_fence(const MdLine *ln, int *colons, int *infoOff,
	int *infoLen)
{
	int i = 0, n = 0;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	while (i + n < ln->len && ln->p[i + n] == ':') n++;
	if (n < 3) return 0;
	*colons = n;
	*infoOff = i + n;
	*infoLen = ln->len - i - n;
	return 1;
}

/* 闭合行: 冒号数 >= 开启行且其后只有空白 */
static int md_colon_close(const MdLine *ln, int openColons)
{
	int i = 0, n = 0;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	while (i + n < ln->len && ln->p[i + n] == ':') n++;
	if (n < openColons) return 0;
	while (i + n < ln->len &&
		(ln->p[i + n] == ' ' || ln->p[i + n] == '\t'))
		n++;
	return i + n == ln->len;
}

/* GitHub 警报首行 "[!TYPE]" (允许前导空白) */
static int md_alert_marker(const MdLine *ln, int *kind, int *consumed)
{
	int i = 0, j, k;
	while (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) i++;
	if (i + 3 > ln->len || ln->p[i] != '[' || ln->p[i + 1] != '!') return 0;
	j = i + 2;
	while (j < ln->len && ln->p[j] != ']') j++;
	if (j >= ln->len) return 0;
	k = md_box_kind_of(ln->p + i + 2, j - (i + 2));
	if (k < 0) return 0;
	*consumed = j + 1;
	*kind = k;
	return 1;
}

static int md_list_marker(const MdLine *ln, int *ordered, int *startNo,
	int *markerLen, int *contentOff, int *contentCol)
{
	int i = 0, ind = 0, mlen, num = 0, digits = 0;
	int afterMarker, rawSpaces = 0, effSpaces;

	while (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) {
		if (ln->p[i] == ' ') ind++;
		else ind = (ind / 4 + 1) * 4;
		i++;
	}
	if (i >= ln->len) return 0;
	if (ln->p[i] == '-' || ln->p[i] == '+' || ln->p[i] == '*') {
		*ordered = 0;
		*startNo = 1;
		mlen = 1;
		i++;
	} else if (ln->p[i] >= '0' && ln->p[i] <= '9') {
		int s = i;
		*ordered = 1;
		while (i < ln->len && ln->p[i] >= '0' && ln->p[i] <= '9' && digits < 9) {
			num = num * 10 + (ln->p[i] - '0');
			i++;
			digits++;
		}
		if (digits <= 0 || i >= ln->len ||
			(ln->p[i] != '.' && ln->p[i] != ')'))
			return 0;
		i++;
		*startNo = num;
		mlen = i - s;
	} else {
		return 0;
	}
	afterMarker = i;
	if (i < ln->len && ln->p[i] != ' ' && ln->p[i] != '\t') return 0;
	while (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) {
		if (ln->p[i] == ' ') rawSpaces++;
		else rawSpaces += 4;
		i++;
	}
	if (rawSpaces == 0) {
		effSpaces = 1;
		i = afterMarker;
	} else if (rawSpaces <= 4) {
		effSpaces = rawSpaces;
	} else {
		effSpaces = 1;
		i = afterMarker + 1;
	}
	if (contentOff) *contentOff = i;
	*markerLen = mlen;
	*contentCol = ind + mlen + effSpaces;
	return 1;
}

static int md_row_has_pipe(const char *p, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if (p[i] == '\\' && i + 1 < len) { i++; continue; }
		if (p[i] == '|') return 1;
	}
	return 0;
}

typedef struct MdSpan { const char *p; int len; } MdSpan;

static void md_trim_span(MdSpan *s)
{
	int a = 0, b = s->len;
	while (a < b && (s->p[a] == ' ' || s->p[a] == '\t')) a++;
	while (b > a && (s->p[b - 1] == ' ' || s->p[b - 1] == '\t')) b--;
	s->p += a;
	s->len = b - a;
}

static int md_split_row(const char *p, int len, MdSpan *out, int cap)
{
	int i = 0, start = 0, n = 0;
	for (i = 0; i <= len; i++) {
		int sep = 0;
		if (i == len) sep = 1;
		else if (p[i] == '\\' && i + 1 < len) { i++; continue; }
		else if (p[i] == '|') sep = 1;
		if (sep) {
			if (n >= cap) return -1;
			out[n].p = p + start;
			out[n].len = i - start;
			md_trim_span(&out[n]);
			n++;
			start = i + 1;
		}
	}
	if (n > 1) {
		int lead = 0;
		while (lead < len && (p[lead] == ' ' || p[lead] == '\t')) lead++;
		if (lead < len && p[lead] == '|' && out[0].len == 0) {
			for (i = 1; i < n; i++) out[i - 1] = out[i];
			n--;
		}
	}
	if (n > 1) {
		int tail = len - 1;
		while (tail >= 0 && (p[tail] == ' ' || p[tail] == '\t')) tail--;
		if (tail >= 0 && p[tail] == '|' && out[n - 1].len == 0) n--;
	}
	return n;
}

static int md_align_cell(const MdSpan *s, uint8_t *align)
{
	int l = 0, r = s->len, leftColon = 0, rightColon = 0, k;
	if (r <= 0) return 0;
	if (s->p[l] == ':') { leftColon = 1; l++; }
	if (r > l && s->p[r - 1] == ':') { rightColon = 1; r--; }
	if (r <= l) return 0;
	for (k = l; k < r; k++)
		if (s->p[k] != '-') return 0;
	if (leftColon && rightColon) *align = MD_ALIGN_CENTER;
	else if (rightColon) *align = MD_ALIGN_RIGHT;
	else *align = MD_ALIGN_LEFT;
	return 1;
}

static int md_ref_def(MdDoc *d, const MdLine *ln)
{
	int i = 0, close = -1, s, e, labelOff, labelLen;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	if (i >= ln->len || ln->p[i] != '[') return 0;
	labelOff = i + 1;
	for (s = labelOff; s < ln->len; s++) {
		if (ln->p[s] == '\\' && s + 1 < ln->len) { s++; continue; }
		if (ln->p[s] == ']') { close = s; break; }
	}
	if (close < 0) return 0;
	labelLen = close - labelOff;
	if (labelLen <= 0 || labelLen > 999) return 0;
	i = close + 1;
	if (i >= ln->len || ln->p[i] != ':') return 0;
	i++;
	while (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) i++;
	if (i >= ln->len) return 0;
	if (ln->p[i] == '<') {
		s = ++i;
		while (i < ln->len && ln->p[i] != '>') i++;
		if (i >= ln->len) return 0;
		e = i;
		i++;
	} else {
		s = i;
		while (i < ln->len && ln->p[i] != ' ' && ln->p[i] != '\t') i++;
		e = i;
	}
	if (e <= s) return 0;
	while (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) i++;
	if (i < ln->len) {
		char c = ln->p[i];
		char closec;
		if (c != '"' && c != '\'' && c != '(') return 0;
		closec = c == '(' ? ')' : c;
		i++;
		while (i < ln->len && ln->p[i] != closec) {
			if (ln->p[i] == '\\' && i + 1 < ln->len) i += 2;
			else i++;
		}
		if (i >= ln->len) return 0;
		i++;
		while (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) i++;
		if (i < ln->len) return 0;
	}
	return md_ref_add(d, ln->p + labelOff, labelLen, ln->p + s, e - s);
}

/* 代码块文本 (Tab 展开; 行间 \n) */
static int md_code_line(MdInl *b, const char *p, int len)
{
	int i, col = 0;
	for (i = 0; i < len; i++) {
		char c = p[i];
		if (c == '\t') {
			int sp = 4 - (col % 4);
			while (sp-- > 0) { inl_put_w(b, L' ', MD_R_CODE, -1); col++; }
		} else {
			uint32_t cp;
			const char *nx = md_utf8_next(p + i, p + len, &cp);
			if (cp == 0) cp = 0xFFFD;
			inl_put_cp(b, cp, MD_R_CODE, -1);
			i = (int)(nx - p) - 1;
			col++;
		}
	}
	return 1;
}

static int md_code_build(MdInl *b, MdLines *sub, int firstLine, int indent)
{
	int i;
	for (i = firstLine; i < sub->n; i++) {
		const char *p;
		int len;
		if (i > firstLine) inl_put_w(b, L'\n', MD_R_CODE, -1);
		md_strip_cols(sub->v[i].p, sub->v[i].len, indent, &p, &len);
		md_code_line(b, p, len);
	}
	return 1;
}

static void md_parse_blocks(MdDoc *d, MdLines *ls, int *ip, MdBlock *parent,
	int depth);

static void md_parse_list(MdDoc *d, MdLines *ls, int *ip, MdBlock *parent,
	int depth)
{
	MdLine *first = &ls->v[*ip];
	int ordered = 0, startNo = 0, mlen = 0, cOff = 0, cCol = 0;
	int baseInd, i = *ip, blankBetween = 0;
	MdBlock *list;

	baseInd = md_indent(first->p, first->len);
	md_list_marker(first, &ordered, &startNo, &mlen, &cOff, &cCol);
	list = md_add_block(d, parent);
	if (!list) return;
	list->type = MD_BLOCK_LIST;
	list->ordered = ordered;
	list->startNo = ordered ? startNo : 1;
	list->tight = 1;

	while (i < ls->n) {
		MdLine *ln = &ls->v[i];
		int o = 0, sno = 0, ml = 0, co = 0, cc = 0;
		MdBlock *item;
		MdLines sub;
		int itemBlank = 0;
		if (md_blank(ln)) { blankBetween = 1; i++; continue; }
		if (md_indent(ln->p, ln->len) != baseInd) break;
		if (!md_list_marker(ln, &o, &sno, &ml, &co, &cc)) break;
		if (o != ordered) break;
		item = md_add_block(d, list);
		if (!item) break;
		item->type = MD_BLOCK_ITEM;
		item->task = MD_TASK_NONE;
		if (blankBetween) list->tight = 0;
		blankBetween = 0;

		ZeroMemory(&sub, sizeof(sub));
		{
			const char *p = ln->p + co;
			int len = ln->len - co;
			if (len >= 4 && p[0] == '[' && (p[1] == ' ' || p[1] == 'x' || p[1] == 'X') &&
				p[2] == ']' && (p[3] == ' ' || p[3] == '\t')) {
				item->task = (p[1] == ' ' ? MD_TASK_TODO : MD_TASK_DONE);
				p += 4;
				len -= 4;
				while (len > 0 && (p[0] == ' ' || p[0] == '\t')) { p++; len--; }
			}
			lines_add(&sub, p, len, ln->srcOff + co, ln->srcLen - co);
		}
		i++;
		while (i < ls->n) {
			MdLine *n2 = &ls->v[i];
			const char *p;
			int len;
			if (md_blank(n2)) {
				lines_add(&sub, n2->p, 0, n2->srcOff, n2->srcLen);
				itemBlank = 1;
				i++;
				continue;
			}
			if (md_indent(n2->p, n2->len) > baseInd) {
				md_strip_cols(n2->p, n2->len, cCol, &p, &len);
				lines_add(&sub, p, len, n2->srcOff, n2->srcLen);
				i++;
			} else {
				break;
			}
		}
		if (itemBlank) list->tight = 0;
		{
			int subi = 0;
			md_parse_blocks(d, &sub, &subi, item, depth + 1);
		}
		md_free(sub.v);
	}
	md_set_span(list, ls, *ip, i);
	*ip = i;
}

static int md_table_try(MdDoc *d, MdLines *ls, int *ip, MdBlock *parent)
{
	MdLine *h, *dl;
	MdSpan *hc = NULL, *dc = NULL;
	int hn, dn, cols, rows = 1, j, c, k;
	uint8_t *align = NULL;
	MdCell *cells = NULL;
	MdBlock *b;
	int capSpans;

	if (*ip + 1 >= ls->n) return 0;
	h = &ls->v[*ip];
	dl = &ls->v[*ip + 1];
	if (!md_row_has_pipe(h->p, h->len) || !md_row_has_pipe(dl->p, dl->len))
		return 0;
	capSpans = h->len + 2;
	hc = (MdSpan *)md_alloc(capSpans * (int)sizeof(MdSpan));
	dc = (MdSpan *)md_alloc((dl->len + 2) * (int)sizeof(MdSpan));
	if (!hc || !dc) { md_free(hc); md_free(dc); return 0; }
	hn = md_split_row(h->p, h->len, hc, capSpans);
	dn = md_split_row(dl->p, dl->len, dc, dl->len + 2);
	if (hn <= 0 || hn != dn) { md_free(hc); md_free(dc); return 0; }
	cols = hn > MD_MAX_COLS ? MD_MAX_COLS : hn;
	align = (uint8_t *)md_alloc(cols);
	if (!align) { md_free(hc); md_free(dc); return 0; }
	for (c = 0; c < cols; c++) {
		MdSpan s = dc[c];
		if (!md_align_cell(&s, &align[c])) {
			md_free(align); md_free(hc); md_free(dc);
			return 0;
		}
	}
	b = md_add_block(d, parent);
	if (!b) { md_free(align); md_free(hc); md_free(dc); return 0; }
	b->type = MD_BLOCK_TABLE;
	b->u.table.cols = cols;
	b->p.table.align = align;
	cells = (MdCell *)md_alloc(cols * (int)sizeof(MdCell));
	if (!cells) { b->u.table.cols = 0; md_free(hc); md_free(dc); return 0; }

	/* 表头行 */
	for (c = 0; c < cols; c++) {
		MdCell *cell = &cells[c];
		if (c == cols - 1 && hn > cols) {
			int m;
			MdWbuf wb;
			ZeroMemory(&wb, sizeof(wb));
			wbuf_utf8(&wb, hc[c].p, hc[c].len);
			for (m = cols; m < hn; m++) {
				wbuf_utf8(&wb, " | ", 3);
				wbuf_utf8(&wb, hc[m].p, hc[m].len);
			}
			cell->rawLen = wb.n * 4 + 1;
			cell->raw = (char *)md_alloc(cell->rawLen);
			if (cell->raw) {
				int wn = 0;
				for (m = 0; m < wb.n; m++) {
					uint32_t cp = (uint32_t)wb.p[m];
					char tmp[4];
					int tn = 0;
					if (cp < 0x80) tmp[tn++] = (char)cp;
					else if (cp < 0x800) {
						tmp[tn++] = (char)(0xC0 | (cp >> 6));
						tmp[tn++] = (char)(0x80 | (cp & 0x3F));
					} else {
						tmp[tn++] = (char)(0xE0 | (cp >> 12));
						tmp[tn++] = (char)(0x80 | ((cp >> 6) & 0x3F));
						tmp[tn++] = (char)(0x80 | (cp & 0x3F));
					}
					for (k = 0; k < tn && wn < cell->rawLen - 1; k++)
						cell->raw[wn++] = tmp[k];
				}
				cell->raw[wn] = 0;
			}
			md_free(wb.p);
		} else {
			cell->raw = (char *)md_alloc(hc[c].len + 1);
			if (cell->raw) {
				for (k = 0; k < hc[c].len; k++)
					cell->raw[k] = hc[c].p[k];
				cell->raw[hc[c].len] = 0;
			}
			cell->rawLen = hc[c].len;
		}
	}
	j = *ip + 2;
	while (j < ls->n) {
		MdLine *r = &ls->v[j];
		MdSpan *sc;
		int sn;
		if (md_blank(r) || !md_row_has_pipe(r->p, r->len)) break;
		sc = (MdSpan *)md_alloc((r->len + 2) * (int)sizeof(MdSpan));
		if (!sc) break;
		sn = md_split_row(r->p, r->len, sc, r->len + 2);
		if (sn <= 0) { md_free(sc); break; }
		{
			int need = (rows + 1) * cols;
			int old = rows * cols;
			MdCell *tmp = (MdCell *)HeapReAlloc(GetProcessHeap(), 0, cells,
				(SIZE_T)need * sizeof(MdCell));
			if (!tmp) { md_free(sc); break; }
			cells = tmp;
			ZeroMemory(&cells[old], (SIZE_T)cols * sizeof(MdCell));
		}
		for (c = 0; c < cols; c++) {
			MdCell *cell = &cells[rows * cols + c];
			if (c == cols - 1 && hn > cols) {
				int m;
				MdWbuf wb;
				ZeroMemory(&wb, sizeof(wb));
				if (c < sn) wbuf_utf8(&wb, sc[c].p, sc[c].len);
				for (m = cols; m < sn; m++) {
					wbuf_utf8(&wb, " | ", 3);
					wbuf_utf8(&wb, sc[m].p, sc[m].len);
				}
				cell->rawLen = wb.n * 4 + 1;
				cell->raw = (char *)md_alloc(cell->rawLen);
				if (cell->raw) {
					int wn = 0;
					for (m = 0; m < wb.n; m++) {
						uint32_t cp = (uint32_t)wb.p[m];
						char tmp[4];
						int tn = 0;
						if (cp < 0x80) tmp[tn++] = (char)cp;
						else if (cp < 0x800) {
							tmp[tn++] = (char)(0xC0 | (cp >> 6));
							tmp[tn++] = (char)(0x80 | (cp & 0x3F));
						} else {
							tmp[tn++] = (char)(0xE0 | (cp >> 12));
							tmp[tn++] = (char)(0x80 | ((cp >> 6) & 0x3F));
							tmp[tn++] = (char)(0x80 | (cp & 0x3F));
						}
						for (k = 0; k < tn && wn < cell->rawLen - 1; k++)
							cell->raw[wn++] = tmp[k];
					}
					cell->raw[wn] = 0;
				}
				md_free(wb.p);
			} else if (c < sn) {
				cell->raw = (char *)md_alloc(sc[c].len + 1);
				if (cell->raw) {
					for (k = 0; k < sc[c].len; k++)
						cell->raw[k] = sc[c].p[k];
					cell->raw[sc[c].len] = 0;
				}
				cell->rawLen = sc[c].len;
			} else {
				cell->raw = (char *)md_alloc(1);
				cell->rawLen = 0;
			}
		}
		md_free(sc);
		rows++;
		j++;
	}
	b->u.table.rows = rows;
	b->p.table.cells = cells;
	md_set_span(b, ls, *ip, j);
	*ip = j;
	md_free(hc);
	md_free(dc);
	return 1;
}

/* ── 块级公式 ─────────────────────────────────────────────────────── */

/* 行首 (<=3 空格) $$ 或 \[ : 返回内容起始下标, 否则 -1。
 * kind: 0 = $$, 1 = \[ */
static int md_math_fence(const MdLine *ln, int *kind)
{
	int i = 0;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	if (i + 1 < ln->len && ln->p[i] == '$' && ln->p[i + 1] == '$') {
		*kind = 0;
		return i + 2;
	}
	if (i + 1 < ln->len && ln->p[i] == '\\' && ln->p[i + 1] == '[') {
		*kind = 1;
		return i + 2;
	}
	return -1;
}

/* 整行只有闭合定界符 (允许前后空白) 返回 1 */
static int md_math_close(const MdLine *ln, int kind)
{
	int i = 0;
	while (i < 3 && i < ln->len && ln->p[i] == ' ') i++;
	if (i + 2 > ln->len) return 0;
	if (kind == 0) {
		if (ln->p[i] != '$' || ln->p[i + 1] != '$') return 0;
	} else {
		if (ln->p[i] != '\\' || ln->p[i + 1] != ']') return 0;
	}
	i += 2;
	while (i < ln->len && (ln->p[i] == ' ' || ln->p[i] == '\t')) i++;
	return i == ln->len;
}

/* $$...$$ / \[...\] 独占块。支持单行与多行; 未闭合返回 0 (回退段落)。 */
static int md_math_block_try(MdDoc *d, MdLines *ls, int *ip, MdBlock *parent)
{
	MdLine *ln = &ls->v[*ip];
	MdBuf mb;
	MdBlock *b;
	int kind, oi, j, closeAt = -1, closed = 0, s, e, srcOff;

	oi = md_math_fence(ln, &kind);
	if (oi < 0) return 0;
	ZeroMemory(&mb, sizeof(mb));
	{
		const char *op = ln->p + oi;
		int rem = ln->len - oi, m = 0;
		while (m + 1 < rem) {
			if (op[m] == '\\') { m += 2; continue; }
			if ((kind == 0 && op[m] == '$' && op[m + 1] == '$') ||
				(kind == 1 && op[m] == '\\' && op[m + 1] == ']')) {
				closeAt = m;
				break;
			}
			m++;
		}
		if (closeAt >= 0) {
			int t = oi + closeAt + 2;
			while (t < ln->len && (ln->p[t] == ' ' || ln->p[t] == '\t')) t++;
			if (t != ln->len) closeAt = -1;
		}
	}
	if (closeAt >= 0) {
		if (closeAt > 0 && !mbuf_add(&mb, ln->p + oi, closeAt)) {
			md_free(mb.p);
			return 0;
		}
		j = *ip + 1;
		closed = 1;
	} else {
		if (ln->len - oi > 0) mbuf_add(&mb, ln->p + oi, ln->len - oi);
		j = *ip + 1;
		while (j < ls->n) {
			if (md_math_close(&ls->v[j], kind)) {
				closed = 1;
				j++;
				break;
			}
			if (mb.n > 0) mbuf_add(&mb, "\n", 1);
			mbuf_add(&mb, ls->v[j].p, ls->v[j].len);
			j++;
		}
	}
	if (!closed) {
		md_free(mb.p);
		return 0;
	}
	s = 0;
	e = mb.n;
	while (s < e && md_is_space_char(mb.p[s])) s++;
	while (e > s && md_is_space_char(mb.p[e - 1])) e--;
	if (e <= s) {
		md_free(mb.p);
		return 0;
	}
	srcOff = md_url_put(d, mb.p + s, e - s, 0, NULL);
	md_free(mb.p);
	if (srcOff < 0) return 0;
	b = md_add_block(d, parent);
	if (!b) return 0;
	b->type = MD_BLOCK_MATH;
	b->u.math.mathOff = srcOff;
	b->u.math.mathLen = e - s;
	md_set_span(b, ls, *ip, j);
	*ip = j;
	return 1;
}

static int md_interrupts(MdDoc *d, MdLines *ls, int i)
{
	MdLine *ln = &ls->v[i];
	int o = 0, sn = 0, ml = 0, co = 0, cc = 0;
	int a = 0, b = 0, c = 0, e = 0;
	char fch = 0;
	if (md_hr(ln)) return 1;
	if (md_atx(ln, &a, &b, &c)) return 1;
	if (md_fence_open(ln, &fch, &a, &b, &c, &e)) return 1;
	if (md_quote(ln, &a)) return 1;
	if (md_list_marker(ln, &o, &sn, &ml, &co, &cc)) return 1;
	{
		int mk = 0;
		if (md_math_fence(ln, &mk) >= 0) return 1;
	}
	(void)d;
	return 0;
}

static void md_parse_blocks(MdDoc *d, MdLines *ls, int *ip, MdBlock *parent,
	int depth)
{
	int i = *ip;
	while (i < ls->n) {
		MdLine *ln = &ls->v[i];
		char fch;
		int flen, ind, infoOff, infoLen;
		if (md_blank(ln)) { i++; continue; }

		if (md_fence_open(ln, &fch, &flen, &ind, &infoOff, &infoLen)) {
			MdBlock *b = md_add_block(d, parent);
			int j, closed = 0;
			MdInl ib;
			if (!b) break;
			b->type = MD_BLOCK_CODE;
			b->fence = 1;
			{
				int wordLen = 0;
				while (infoOff + wordLen < ln->len &&
					ln->p[infoOff + wordLen] != ' ' &&
					ln->p[infoOff + wordLen] != '\t')
					wordLen++;
				b->u.code.langOff = md_url_put(d, ln->p + infoOff, wordLen, 0, NULL);
				b->u.code.langLen = wordLen;
			}
			inl_init(&ib, d);
			j = i + 1;
			while (j < ls->n) {
				if (md_fence_close(&ls->v[j], fch, flen)) {
					closed = 1;
					j++;
					break;
				}
				j++;
			}
			{
				int k;
				for (k = i + 1; k < (closed ? j - 1 : j); k++) {
					const char *p;
					int len;
					if (k > i + 1) inl_put_w(&ib, L'\n', MD_R_CODE, -1);
					md_strip_cols(ls->v[k].p, ls->v[k].len, ind, &p, &len);
					md_code_line(&ib, p, len);
				}
			}
			if (closed) b->flags |= MD_BF_FENCE_CLOSED;
			inl_to_leaf(&ib, &b->p.text.text16, &b->p.text.len16,
				&b->p.text.runs, &b->p.text.runCount, &b->p.text.links,
				&b->p.text.linkCount);
			inl_free(&ib);
			md_set_span(b, ls, i, j);
			i = j;
			continue;
		}

		if (md_math_block_try(d, ls, &i, parent)) continue;

		if (depth < MD_MAX_DEPTH && md_quote(ln, &infoOff)) {
			MdBlock *b = md_add_block(d, parent);
			MdLines sub;
			int j = i, subi = 0, k;
			if (!b) break;
			b->type = MD_BLOCK_QUOTE;
			ZeroMemory(&sub, sizeof(sub));
			while (j < ls->n) {
				int co;
				MdLine *q = &ls->v[j];
				if (!md_quote(q, &co)) break;
				lines_add(&sub, q->p + co, q->len - co,
					q->srcOff + co, q->srcLen - co);
				j++;
			}
			/* GitHub 警报: 首行 "[!NOTE]" 等标记 -> 容器块 */
			{
				int kind = -1, used = 0;
				if (sub.n > 0 &&
					md_alert_marker(&sub.v[0], &kind, &used)) {
					MdLine *l0 = &sub.v[0];
					int onlySpace = 1, rest;
					for (k = used; k < l0->len; k++) {
						if (l0->p[k] != ' ' && l0->p[k] != '\t') {
							onlySpace = 0;
							break;
						}
					}
					b->type = MD_BLOCK_CONTAINER;
					b->u.box.boxKind = (uint8_t)kind;
					md_box_title_set(d, b, NULL, 0, kind);
					if (onlySpace) {
						if (sub.n > 1) {
							for (k = 0; k + 1 < sub.n; k++)
								sub.v[k] = sub.v[k + 1];
							sub.n--;
						} else {
							sub.n = 0;
						}
					} else {
						while (used < l0->len &&
							(l0->p[used] == ' ' || l0->p[used] == '\t'))
							used++;
						rest = l0->len - used;
						l0->p += used;
						l0->len = rest;
						l0->srcOff += used;
						l0->srcLen -= used;
					}
				}
			}
			md_parse_blocks(d, &sub, &subi, b, depth + 1);
			md_free(sub.v);
			md_set_span(b, ls, i, j);
			i = j;
			continue;
		}

		/* ::: kind [标题] ... ::: 容器 (未闭合则回退为段落字面量) */
		if (depth < MD_MAX_DEPTH) {
			int cn = 0, ioff = 0, ilen = 0;
			if (md_colon_fence(ln, &cn, &ioff, &ilen)) {
				int j = i + 1, closed = -1, k;
				while (j < ls->n) {
					if (md_colon_close(&ls->v[j], cn)) {
						closed = j;
						break;
					}
					j++;
				}
				if (closed >= 0) {
					MdBlock *b = md_add_block(d, parent);
					MdLines sub;
					int subi = 0, kind, s = ioff, e = ioff + ilen, we;
					if (!b) break;
					b->type = MD_BLOCK_CONTAINER;
					while (s < e && (ln->p[s] == ' ' || ln->p[s] == '\t')) s++;
					we = s;
					while (we < e && ln->p[we] != ' ' &&
						ln->p[we] != '\t') we++;
					kind = md_box_kind_of(ln->p + s, we - s);
					if (kind < 0) {
						kind = MD_BOX_NOTE;
						/* 未识别首词: 整段 info 作为标题 */
						md_box_title_set(d, b, ln->p + s, e - s, kind);
					} else {
						while (we < e && (ln->p[we] == ' ' ||
							ln->p[we] == '\t')) we++;
						md_box_title_set(d, b, ln->p + we, e - we, kind);
					}
					b->u.box.boxKind = (uint8_t)kind;
					ZeroMemory(&sub, sizeof(sub));
					for (k = i + 1; k < closed; k++)
						lines_add(&sub, ls->v[k].p, ls->v[k].len,
							ls->v[k].srcOff, ls->v[k].srcLen);
					md_parse_blocks(d, &sub, &subi, b, depth + 1);
					md_free(sub.v);
					md_set_span(b, ls, i, closed + 1);
					i = closed + 1;
					continue;
				}
			}
		}

		{
			int level, cOff, cLen;
			if (md_atx(ln, &level, &cOff, &cLen)) {
				MdBlock *b = md_add_block(d, parent);
				MdInl ib;
				if (!b) break;
				b->type = MD_BLOCK_HEADING;
				b->level = (uint8_t)level;
				b->raw = (char *)md_alloc(cLen + 1);
				if (b->raw) {
					int k;
					for (k = 0; k < cLen; k++) b->raw[k] = ln->p[cOff + k];
					b->raw[cLen] = 0;
					b->rawLen = cLen;
				}
				inl_init(&ib, d);
				inl_parse(&ib, ln->p + cOff, cLen, 1);
				inl_to_leaf(&ib, &b->p.text.text16, &b->p.text.len16, &b->p.text.runs,
					&b->p.text.runCount, &b->p.text.links, &b->p.text.linkCount);
				md_take_imgs(b, &ib);
				md_take_maths(b, &ib);
				inl_free(&ib);
				md_free(b->raw);
				b->raw = NULL;
				b->rawLen = 0;
				md_set_span(b, ls, i, i + 1);
				i++;
				continue;
			}
		}

		if (md_hr(ln)) {
			MdBlock *b = md_add_block(d, parent);
			if (!b) break;
			b->type = MD_BLOCK_HR;
			md_set_span(b, ls, i, i + 1);
			i++;
			continue;
		}

		if (depth < MD_MAX_DEPTH) {
			int o = 0, sno = 0, ml = 0, co = 0, cc = 0;
			if (md_list_marker(ln, &o, &sno, &ml, &co, &cc)) {
				md_parse_list(d, ls, &i, parent, depth);
				continue;
			}
		}

		if (md_indent(ln->p, ln->len) >= 4) {
			MdBlock *b = md_add_block(d, parent);
			MdLines sub;
			MdInl ib;
			int j = i, first;
			if (!b) break;
			b->type = MD_BLOCK_CODE;
			b->fence = 0;
			ZeroMemory(&sub, sizeof(sub));
			while (j < ls->n && (md_blank(&ls->v[j]) ||
				md_indent(ls->v[j].p, ls->v[j].len) >= 4)) {
				const char *p;
				int len;
				md_strip_cols(ls->v[j].p, ls->v[j].len, 4, &p, &len);
				lines_add(&sub, p, len, ls->v[j].srcOff, ls->v[j].srcLen);
				j++;
			}
			first = 0;
			while (sub.n > 0 && md_blank(&sub.v[sub.n - 1])) sub.n--;
			inl_init(&ib, d);
			if (sub.n > 0) md_code_build(&ib, &sub, first, 0);
			inl_to_leaf(&ib, &b->p.text.text16, &b->p.text.len16,
				&b->p.text.runs, &b->p.text.runCount, &b->p.text.links,
				&b->p.text.linkCount);
			inl_free(&ib);
			md_free(sub.v);
			md_set_span(b, ls, i, j);
			i = j;
			continue;
		}

		if (md_ref_def(d, ln)) { i++; continue; }

		if (md_table_try(d, ls, &i, parent)) continue;

		/* 段落 */
		{
			MdBlock *b;
			MdBuf mb;
			int j = i, first = 1, headingLevel = 0;
			int startLine = i;
			ZeroMemory(&mb, sizeof(mb));
			while (j < ls->n) {
				MdLine *pl = &ls->v[j];
				const char *p;
				int len, lvl;
				if (md_blank(pl)) break;
				if (!first) {
					if (md_setext(pl, &lvl)) { headingLevel = lvl; j++; break; }
					if (md_interrupts(d, ls, j)) break;
				}
				p = pl->p;
				len = pl->len;
				{
					int lead = 0;
					while (lead < len && lead < 3 && p[lead] == ' ') lead++;
					p += lead;
					len -= lead;
				}
				if (!first) mbuf_add(&mb, "\n", 1);
				mbuf_add(&mb, p, len);
				first = 0;
				j++;
			}
			b = md_add_block(d, parent);
			if (!b) { md_free(mb.p); break; }
			if (headingLevel) {
				b->type = MD_BLOCK_HEADING;
				b->level = (uint8_t)headingLevel;
			} else {
				b->type = MD_BLOCK_PARAGRAPH;
			}
			b->rawLen = mb.n;
			b->raw = mb.p;
			md_set_span(b, ls, startLine, j);
			i = j;
			if (headingLevel) {
				/* Setext 标题: 立即行内解析 */
				MdInl ib;
				inl_init(&ib, d);
				inl_parse(&ib, b->raw, b->rawLen, 1);
				inl_to_leaf(&ib, &b->p.text.text16, &b->p.text.len16, &b->p.text.runs,
					&b->p.text.runCount, &b->p.text.links, &b->p.text.linkCount);
				md_take_imgs(b, &ib);
				md_take_maths(b, &ib);
				inl_free(&ib);
				md_free(b->raw);
				b->raw = NULL;
				b->rawLen = 0;
			}
		}
	}
	*ip = i;
}

/* ── 行内 pass: 段落/表格单元格/图片块 ───────────────────────────── */

static int md_title_align(const char *p, int n)
{
	int i; int isLeft = (n == 4), isCenter = (n == 6), isRight = (n == 5);
	if (!isLeft && !isCenter && !isRight) return 255;
	for (i = 0; i < n; i++) {
		char c = p[i];
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		if (isLeft && c != "left"[i]) return 255;
		if (isCenter && c != "center"[i]) return 255;
		if (isRight && c != "right"[i]) return 255;
	}
	return isLeft ? MD_ALIGN_LEFT : (isCenter ? MD_ALIGN_CENTER : MD_ALIGN_RIGHT);
}

static int md_image_block(MdDoc *d, MdBlock *b)
{
	const char *p = b->raw;
	int len = b->rawLen;
	int i = 0, close, after, urlOff = -1, urlLen = 0, end;
	int altOff, altLen, tOff = -1, tLen = 0;
	MdInl ib;

	if (!p || len <= 0) return 0;
	for (i = 0; i < len; i++) if (p[i] == '\n') return 0;
	i = 0;
	while (i < len && (p[i] == ' ' || p[i] == '\t')) i++;
	if (i + 1 >= len || p[i] != '!' || p[i + 1] != '[') return 0;
	close = md_scan_bracket(p + i + 1, len - i - 1, 0);
	if (close < 0) return 0;
	close += i + 1;
	altOff = i + 2;
	altLen = close - altOff;
	after = close + 1;
	if (after < len && p[after] == '(') {
		int dOff, dLen, next;
		if (!md_parse_dest(p, len, after, &dOff, &dLen, &tOff, &tLen,
			&next)) return 0;
		urlOff = md_url_put(d, p + dOff, dLen, 1, &urlLen);
		if (urlOff < 0) return 0;
		end = next;
	} else if (after < len && p[after] == '[') {
		int c2 = md_scan_bracket(p, len, after);
		int l2Off = after + 1, l2Len;
		if (c2 < 0) return 0;
		l2Len = c2 - after - 1;
		if (l2Len == 0) { l2Off = altOff; l2Len = altLen; }
		if (!md_ref_lookup(d, p + l2Off, l2Len, &urlOff, &urlLen)) return 0;
		end = c2 + 1;
	} else {
		if (!md_ref_lookup(d, p + altOff, altLen, &urlOff, &urlLen)) return 0;
		end = after;
	}
	end = md_parse_img_attrs(p, len, end, &b->u.image.imgW, &b->u.image.imgH);
	while (end < len && (p[end] == ' ' || p[end] == '\t')) end++;
	if (end != len) return 0;

	b->type = MD_BLOCK_IMAGE;
	b->u.image.urlOff = urlOff;
	b->u.image.urlLen = urlLen;
	b->u.image.imgAlign = (uint8_t)(tOff >= 0 ? md_title_align(p + tOff, tLen) : 255);
	inl_init(&ib, d);
	inl_parse(&ib, p + altOff, altLen, 0);
	inl_to_leaf(&ib, &b->p.text.text16, &b->p.text.len16,
		&b->p.text.runs, &b->p.text.runCount, &b->p.text.links,
		&b->p.text.linkCount);
	inl_free(&ib);
	return 1;
}

static void md_inline_cell(MdDoc *d, MdCell *c)
{
	MdInl ib;
	if (!c->raw) return;
	inl_init(&ib, d);
	inl_parse(&ib, c->raw, c->rawLen, 1);
	inl_to_leaf(&ib, &c->text16, &c->len16, &c->runs, &c->runCount,
		&c->links, &c->linkCount);
	inl_free(&ib);
	md_free(c->raw);
	c->raw = NULL;
	c->rawLen = 0;
}

static void md_inline_pass(MdDoc *d, MdBlock *b)
{
	int i;
	if (MD_BT_KIDS(b->type)) {
		for (i = 0; i < b->p.kids.childCount; i++)
			md_inline_pass(d, &b->p.kids.children[i]);
	}

	switch (b->type) {
	case MD_BLOCK_PARAGRAPH:
		if (md_image_block(d, b)) {
			md_free(b->raw);
			b->raw = NULL;
			b->rawLen = 0;
		} else if (b->raw) {
			MdInl ib;
			inl_init(&ib, d);
			inl_parse(&ib, b->raw, b->rawLen, 1);
			inl_to_leaf(&ib, &b->p.text.text16, &b->p.text.len16, &b->p.text.runs,
				&b->p.text.runCount, &b->p.text.links, &b->p.text.linkCount);
			md_take_imgs(b, &ib);
			md_take_maths(b, &ib);
			inl_free(&ib);
			md_free(b->raw);
			b->raw = NULL;
			b->rawLen = 0;
		}
		break;
	case MD_BLOCK_TABLE:
		if (b->p.table.cells) {
			int n = b->u.table.rows * b->u.table.cols;
			for (i = 0; i < n; i++)
				md_inline_cell(d, &b->p.table.cells[i]);
		}
		break;
	default:
		break;
	}
}

/* ── 哈希 / 释放 ──────────────────────────────────────────────────── */

static uint32_t md_hash_u32(uint32_t h, uint32_t v)
{
	int i;
	for (i = 0; i < 4; i++) {
		h ^= (uint32_t)((v >> (i * 8)) & 0xFF);
		h *= MD_FNV_PRIME;
	}
	return h;
}

static uint32_t md_hash_block(MdDoc *d, MdBlock *b)
{
	uint32_t h = MD_FNV_OFFSET;
	int i;
	h = md_hash_u32(h, b->type);
	h = md_hash_u32(h, b->flags);
	h = md_hash_u32(h, b->level);
	h = md_hash_u32(h, b->tight);
	h = md_hash_u32(h, b->task);
	h = md_hash_u32(h, (uint32_t)b->ordered);
	h = md_hash_u32(h, (uint32_t)b->startNo);
	h = md_hash_u32(h, (uint32_t)b->fence);
	if (b->srcOff >= 0 && b->srcLen >= 0 &&
		b->srcOff + b->srcLen <= d->srcLen) {
		h = md_hash_u32(h, (uint32_t)b->srcLen);
		for (i = 0; i < b->srcLen; i++) {
			h ^= (uint32_t)(unsigned char)d->src[b->srcOff + i];
			h *= MD_FNV_PRIME;
		}
	}
	if (b->type == MD_BLOCK_IMAGE) {
		if (b->u.image.urlOff >= 0 && b->u.image.urlOff < d->urlsLen) {
			const char *u = d->urls + b->u.image.urlOff;
			h = md_hash_u32(h, (uint32_t)b->u.image.imgW);
			h = md_hash_u32(h, (uint32_t)b->u.image.imgH);
			while (*u) {
				h ^= (uint32_t)(unsigned char)*u++;
				h *= MD_FNV_PRIME;
			}
		}
	}
	if (b->type == MD_BLOCK_PARAGRAPH || b->type == MD_BLOCK_HEADING) {
		for (i = 0; i < b->p.text.imgCount; i++) {
			const char *u = d->urls + b->p.text.imgs[i].urlOff;
			h = md_hash_u32(h, (uint32_t)b->p.text.imgs[i].w);
			h = md_hash_u32(h, (uint32_t)b->p.text.imgs[i].h);
			while (*u) {
				h ^= (uint32_t)(unsigned char)*u++;
				h *= MD_FNV_PRIME;
			}
		}
	}
	if (b->type == MD_BLOCK_MATH) {
		const char *u = d->urls +
			(b->u.math.mathOff >= 0 ? b->u.math.mathOff : 0);
		h = md_hash_u32(h, (uint32_t)b->u.math.mathLen);
		while (*u) {
			h ^= (uint32_t)(unsigned char)*u++;
			h *= MD_FNV_PRIME;
		}
	}
	if (b->type == MD_BLOCK_PARAGRAPH || b->type == MD_BLOCK_HEADING) {
		for (i = 0; i < b->p.text.mathCount; i++) {
			const char *u = d->urls + b->p.text.maths[i].srcOff;
			while (*u) {
				h ^= (uint32_t)(unsigned char)*u++;
				h *= MD_FNV_PRIME;
			}
		}
	}
	if (b->type == MD_BLOCK_TABLE) {
		h = md_hash_u32(h, (uint32_t)b->u.table.cols);
		h = md_hash_u32(h, (uint32_t)b->u.table.rows);
		for (i = 0; i < b->u.table.cols; i++)
			h = md_hash_u32(h, b->p.table.align[i]);
	}
	if (b->type == MD_BLOCK_CONTAINER) {
		const char *u;
		h = md_hash_u32(h, b->u.box.boxKind);
		h = md_hash_u32(h, (uint32_t)b->u.box.titleLen);
		if (b->u.box.titleOff >= 0 && b->u.box.titleOff < d->urlsLen) {
			u = d->urls + b->u.box.titleOff;
			while (*u) {
				h ^= (uint32_t)(unsigned char)*u++;
				h *= MD_FNV_PRIME;
			}
		}
	}
	if (MD_BT_KIDS(b->type)) {
		for (i = 0; i < b->p.kids.childCount; i++) {
			uint32_t ch = md_hash_block(d, &b->p.kids.children[i]);
			h = md_hash_u32(h, ch);
		}
	}
	b->hash = h;
	return h;
}

/* union 成员按 type 标签释放: 每个分支只碰该类型有效的成员 */
static void md_free_block(MdBlock *b)
{
	int i;
	switch (b->type) {
	case MD_BLOCK_QUOTE:
	case MD_BLOCK_LIST:
	case MD_BLOCK_ITEM:
	case MD_BLOCK_CONTAINER:
		if (b->p.kids.children) {
			for (i = 0; i < b->p.kids.childCount; i++)
				md_free_block(&b->p.kids.children[i]);
			md_free(b->p.kids.children);
		}
		break;
	case MD_BLOCK_TABLE:
		if (b->p.table.cells) {
			int n = b->u.table.rows * b->u.table.cols;
			for (i = 0; i < n; i++) {
				MdCell *c = &b->p.table.cells[i];
				md_free(c->text16);
				md_free(c->runs);
				md_free(c->links);
				md_free(c->raw);
			}
			md_free(b->p.table.cells);
		}
		md_free(b->p.table.align);
		break;
	case MD_BLOCK_PARAGRAPH:
	case MD_BLOCK_HEADING:
		md_free(b->p.text.text16);
		md_free(b->p.text.runs);
		md_free(b->p.text.links);
		md_free(b->p.text.imgs);
		md_free(b->p.text.maths);
		break;
	case MD_BLOCK_CODE:
	case MD_BLOCK_IMAGE:
		md_free(b->p.text.text16);
		md_free(b->p.text.runs);
		md_free(b->p.text.links);
		break;
	case MD_BLOCK_MATH:
	case MD_BLOCK_HR:
	default:
		break;
	}
	md_free(b->raw);
	ZeroMemory(b, sizeof(*b));
}

static void md_clear(MdDoc *d)
{
	int i;
	for (i = 0; i < d->count; i++) md_free_block(&d->blocks[i]);
	md_free(d->blocks);
	md_free(d->src);
	md_free(d->urls);
	md_free(d->refs);
	md_free(d->refSlots);
	ZeroMemory(d, sizeof(*d));
}

/* ── 公共 API ─────────────────────────────────────────────────────── */

void MdDoc_Init(MdDoc *d)
{
	if (d) ZeroMemory(d, sizeof(*d));
}

void MdDoc_Shutdown(MdDoc *d)
{
	if (d) md_clear(d);
}

int Md_Parse(MdDoc *d, const char *utf8, int len)
{
	char *src;
	int n = 0, i, start;
	MdLines lines;
	int li = 0;

	if (!d) return -1;
	md_clear(d);
	if (!utf8 || len <= 0) return 0;

	src = (char *)md_alloc(len + 1);
	if (!src) return -2;
	for (i = 0; i < len; i++) {
		char c = utf8[i];
		if (c == '\r') {
			if (i + 1 < len && utf8[i + 1] == '\n') i++;
			src[n++] = '\n';
		} else if (c == 0) {
			src[n++] = ' ';
		} else {
			src[n++] = c;
		}
	}
	src[n] = 0;
	d->src = src;
	d->srcLen = n;

	ZeroMemory(&lines, sizeof(lines));
	start = 0;
	for (i = 0; i <= n; i++) {
		if (i == n || src[i] == '\n') {
			/* 末尾换行不产生额外空行 (避免代码块/段落多一个空行) */
			if (i == n && start == n) break;
			lines_add(&lines, src + start, i - start, start, i - start);
			start = i + 1;
		}
	}
	md_parse_blocks(d, &lines, &li, NULL, 0);
	md_free(lines.v);

	for (i = 0; i < d->count; i++) md_inline_pass(d, &d->blocks[i]);
	for (i = 0; i < d->count; i++) md_hash_block(d, &d->blocks[i]);
	return 0;
}

const char *MdDoc_Url(const MdDoc *d, int off)
{
	if (!d || !d->urls || off < 0 || off >= d->urlsLen) return "";
	return d->urls + off;
}

const char *MdDoc_Src(const MdDoc *d, int off)
{
	if (!d || !d->src || off < 0 || off >= d->srcLen) return "";
	return d->src + off;
}

static int md_count_type_block(const MdBlock *b, int type)
{
	int n = 0, i;
	if (b->type == type) n++;
	if (MD_BT_KIDS(b->type)) {
		for (i = 0; i < b->p.kids.childCount; i++)
			n += md_count_type_block(&b->p.kids.children[i], type);
	}
	return n;
}

int MdDoc_CountType(const MdDoc *d, int type)
{
	int n = 0, i;
	if (!d) return 0;
	for (i = 0; i < d->count; i++)
		n += md_count_type_block(&d->blocks[i], type);
	return n;
}
