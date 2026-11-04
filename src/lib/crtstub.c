/*
 * crtstub.c - 最小 CRT 替代存根 (0CRT 模式)
 *
 * 参考 RemoteCtrl/agent helpers.c 的 0CRT 实现，保留与 UI 库相关的部分：
 *   - /GS / SEH 运行时、__security_init_cookie
 *   - SSE2/AVX2/ERMSB 内存与字符串原语
 *   - 完整 sprintf/vsnprintf（flags/宽度/精度/length/%f/%p/%n）
 *   - calloc/realloc/strtol/strtoul/qsort/bsearch/localeconv 等
 *
 * 编译要求:
 *   /DNO_CRT_NO_DEFAULTLIB /GS- /Oi- /Gs32768
 *   mem_avx2.c 需要 /arch:AVX2；crtstub.c 本身是 x64 SSE2 基线。
 */
#include <windows.h>
#include <stddef.h>
#include <stdarg.h>
#include <locale.h>
#include <intrin.h>
#include <emmintrin.h>
#include <immintrin.h>

#ifdef NO_CRT_NO_DEFAULTLIB

/* ── /GS 运行时 ─────────────────────────────────────────────────────
 * 本文件必须 /GS-：若 __security_check_cookie 自身被 /GS 保护，其尾声
 * 又调用自己 → 无限递归。cookie 单独放 .gs_cookie，避免与相邻变量越界。 */
#pragma data_seg(".gs_cookie")
__declspec(allocate(".gs_cookie")) unsigned __int64 __security_cookie;
#pragma data_seg()

void __security_init_cookie(void)
{
	__security_cookie = 0x0022980F0000A017ULL
		^ ((unsigned __int64)GetCurrentProcessId() << 32);
}

/* 叶子函数: 成功路径只比较/返回, 不碰返回值寄存器 */
void __security_check_cookie(unsigned __int64 cookie)
{
	if (cookie != __security_cookie) ExitProcess(0xE0000001u);
}

int __GSHandlerCheck(unsigned __int64 *pAddress, int exceptionCode)
{
	(void)pAddress; (void)exceptionCode;
	return 0;
}

/* C 的 __try/__except (不开 C++ 异常) 只需路由到 __except 块 */
int __C_specific_handler(struct _EXCEPTION_RECORD *pExceptionRecord,
	void *pEstablisherFrame, struct _CONTEXT *pContext,
	struct _DISPATCHER_CONTEXT *pDispatcherContext)
{
	(void)pExceptionRecord; (void)pEstablisherFrame;
	(void)pContext; (void)pDispatcherContext;
	return 1;
}

int __GSHandlerCheck_SEH(void *pExceptionRecord, void *pEstablisherFrame,
	void *pContext, void *pDispatcherContext)
{
	(void)pExceptionRecord; (void)pEstablisherFrame;
	(void)pContext; (void)pDispatcherContext;
	return 0;
}

void __report_rangecheckfailure(void)
{
	ExitProcess(0xE0000002u);
}

/* ── 内存原语：SSE2 / AVX2 / ERMSB 分档 ─────────────────────────────
 * n < 512B: SSE2 16B×4；512B..4KB: AVX2；>=4KB 且 CPU 支持 ERMSB/FSRM: rep movsb。
 * AVX2 必须查 CPUID + OSXSAVE/XCR0；SSE2 是 x64 ABI 基线。 */
extern void memcpy_avx2(void *d, const void *s, size_t n);
extern void memset_avx2(void *d, int c, size_t n);
extern void memmove_avx2(void *d, const void *s, size_t n);
extern int  memcmp_avx2(const void *a, const void *b, size_t n);

static int rep_movsb_ok(void)
{
	static volatile LONG s_ok = -1;
	LONG v = s_ok;
	if (v < 0) {
		int r[4];
		int ok = 0;
		__cpuid(r, 0);
		if (r[0] >= 7) {
			__cpuidex(r, 7, 0);
			ok = (int)(((unsigned)r[1] >> 9) & 1)   /* ERMSB */
				| (int)(((unsigned)r[3] >> 4) & 1);  /* FSRM  */
		}
		InterlockedCompareExchange(&s_ok, (LONG)ok, -1);
		v = s_ok;
	}
	return (int)v;
}

static int avx2_ok(void)
{
	static volatile LONG s_ok = -1;
	LONG v = s_ok;
	if (v < 0) {
		int r[4];
		int ok = 0;
		__cpuid(r, 0);
		if (r[0] >= 7) {
			int osxsave;
			__cpuidex(r, 1, 0);
			osxsave = (int)(((unsigned)r[2] >> 27) & 1);   /* OSXSAVE */
			__cpuidex(r, 7, 0);
			if (osxsave && (((unsigned)r[1] >> 5) & 1)) {  /* AVX2 */
				unsigned __int64 xcr0 = _xgetbv(0);
				ok = ((xcr0 & 0x6) == 0x6);                /* XMM+YMM 已启用 */
			}
		}
		InterlockedCompareExchange(&s_ok, (LONG)ok, -1);
		v = s_ok;
	}
	return (int)v;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *p = (unsigned char *)d;
	const unsigned char *q = (const unsigned char *)s;

	if (n >= 4096 && rep_movsb_ok()) {
		__movsb(p, q, n);
		return d;
	}
	if (n >= 512 && avx2_ok()) {
		memcpy_avx2(d, s, n);
		return d;
	}
	while (n >= 64) {
		__m128i a = _mm_loadu_si128((const __m128i *)(const void *)q);
		__m128i b = _mm_loadu_si128((const __m128i *)(const void *)(q + 16));
		__m128i c = _mm_loadu_si128((const __m128i *)(const void *)(q + 32));
		__m128i e = _mm_loadu_si128((const __m128i *)(const void *)(q + 48));
		_mm_storeu_si128((__m128i *)(void *)p, a);
		_mm_storeu_si128((__m128i *)(void *)(p + 16), b);
		_mm_storeu_si128((__m128i *)(void *)(p + 32), c);
		_mm_storeu_si128((__m128i *)(void *)(p + 48), e);
		p += 64; q += 64; n -= 64;
	}
	while (n >= 16) {
		_mm_storeu_si128((__m128i *)(void *)p,
			_mm_loadu_si128((const __m128i *)(const void *)q));
		p += 16; q += 16; n -= 16;
	}
	if (n & 8) {
		_mm_storel_epi64((__m128i *)(void *)p,
			_mm_loadl_epi64((const __m128i *)(const void *)q));
		p += 8; q += 8;
	}
	if (n & 4) { *(unsigned int *)(void *)p = *(const unsigned int *)(const void *)q; p += 4; q += 4; }
	if (n & 2) { *(unsigned short *)(void *)p = *(const unsigned short *)(const void *)q; p += 2; q += 2; }
	if (n & 1) *p = *q;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = (unsigned char *)d;
	__m128i v;
	unsigned int c4 = (unsigned int)(c & 0xFF) * 0x01010101u;
	unsigned short c2 = (unsigned short)((c & 0xFF) * 0x0101);

	if (n >= 512 && avx2_ok()) {
		memset_avx2(d, c, n);
		return d;
	}
	v = _mm_set1_epi8((char)c);
	while (n >= 64) {
		_mm_storeu_si128((__m128i *)(void *)p, v);
		_mm_storeu_si128((__m128i *)(void *)(p + 16), v);
		_mm_storeu_si128((__m128i *)(void *)(p + 32), v);
		_mm_storeu_si128((__m128i *)(void *)(p + 48), v);
		p += 64; n -= 64;
	}
	while (n >= 16) {
		_mm_storeu_si128((__m128i *)(void *)p, v);
		p += 16; n -= 16;
	}
	if (n & 8) { _mm_storel_epi64((__m128i *)(void *)p, v); p += 8; }
	if (n & 4) { *(unsigned int *)(void *)p = c4; p += 4; }
	if (n & 2) { *(unsigned short *)(void *)p = c2; p += 2; }
	if (n & 1) *p = (unsigned char)c;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *p = (unsigned char *)d;
	const unsigned char *q = (const unsigned char *)s;

	if (n == 0 || p == q) return d;
	if (p < q || (size_t)(p - q) >= n) return memcpy(d, s, n);
	if (n >= 512 && avx2_ok()) {
		memmove_avx2(d, s, n);
		return d;
	}
	p += n; q += n;
	while (n >= 64) {
		__m128i a = _mm_loadu_si128((const __m128i *)(const void *)(q - 16));
		__m128i b = _mm_loadu_si128((const __m128i *)(const void *)(q - 32));
		__m128i c = _mm_loadu_si128((const __m128i *)(const void *)(q - 48));
		__m128i e = _mm_loadu_si128((const __m128i *)(const void *)(q - 64));
		_mm_storeu_si128((__m128i *)(void *)(p - 16), a);
		_mm_storeu_si128((__m128i *)(void *)(p - 32), b);
		_mm_storeu_si128((__m128i *)(void *)(p - 48), c);
		_mm_storeu_si128((__m128i *)(void *)(p - 64), e);
		p -= 64; q -= 64; n -= 64;
	}
	while (n >= 16) {
		_mm_storeu_si128((__m128i *)(void *)(p - 16),
			_mm_loadu_si128((const __m128i *)(const void *)(q - 16)));
		p -= 16; q -= 16; n -= 16;
	}
	while (n--) { p--; q--; *p = *q; }
	return d;
}

static int first_diff_byte64(unsigned __int64 x)
{
	int i = 0;
	while (!(x & 0xFFu)) { x >>= 8; i++; }
	return i;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;

	if (n >= 64 && avx2_ok()) return memcmp_avx2(a, b, n);
	while (n >= 16) {
		__m128i va = _mm_loadu_si128((const __m128i *)(const void *)pa);
		__m128i vb = _mm_loadu_si128((const __m128i *)(const void *)pb);
		unsigned eq = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb));
		if (eq != 0xFFFFu) {
			unsigned long i;
			_BitScanForward(&i, (~eq) & 0xFFFFu);
			return (int)pa[i] - (int)pb[i];
		}
		pa += 16; pb += 16; n -= 16;
	}
	if (n >= 8) {
		unsigned __int64 xa = *(const unsigned __int64 *)(const void *)pa;
		unsigned __int64 xb = *(const unsigned __int64 *)(const void *)pb;
		if (xa != xb) {
			int i = first_diff_byte64(xa ^ xb);
			return (int)pa[i] - (int)pb[i];
		}
		pa += 8; pb += 8; n -= 8;
	}
	if (n >= 4) {
		unsigned int xa = *(const unsigned int *)(const void *)pa;
		unsigned int xb = *(const unsigned int *)(const void *)pb;
		if (xa != xb) {
			int i = first_diff_byte64((unsigned __int64)(xa ^ xb));
			return (int)pa[i] - (int)pb[i];
		}
		pa += 4; pb += 4; n -= 4;
	}
	if (n >= 2) {
		unsigned short xa = *(const unsigned short *)(const void *)pa;
		unsigned short xb = *(const unsigned short *)(const void *)pb;
		if (xa != xb) {
			int d = (int)pa[0] - (int)pb[0];
			return d ? d : (int)pa[1] - (int)pb[1];
		}
		pa += 2; pb += 2; n -= 2;
	}
	if (n) return (int)pa[0] - (int)pb[0];
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	const __m128i zero = _mm_setzero_si128();

	while (((size_t)(const void *)p & 15u) != 0) {
		if (!*p) return (size_t)(p - s);
		p++;
	}
	for (;;) {
		__m128i v = _mm_load_si128((const __m128i *)(const void *)p);
		unsigned m = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(v, zero));
		if (m) {
			unsigned long i;
			_BitScanForward(&i, m);
			return (size_t)(p - s) + (size_t)i;
		}
		p += 16;
	}
}

#define HAS_ZERO8(x) (((x) - 0x0101010101010101ULL) & ~(x) & 0x8080808080808080ULL)

static int str_cmp_sse(const char *a, const char *b, size_t n)
{
	const __m128i zero = _mm_setzero_si128();

	while (n >= 16) {
		size_t oa = (size_t)(const void *)a & 0xFFFu;
		size_t ob = (size_t)(const void *)b & 0xFFFu;
		if (oa > 0xFF0u || ob > 0xFF0u) break;
		{
			__m128i va = _mm_loadu_si128((const __m128i *)(const void *)a);
			__m128i vb = _mm_loadu_si128((const __m128i *)(const void *)b);
			unsigned eq = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb));
			unsigned z = (unsigned)_mm_movemask_epi8(
				_mm_or_si128(_mm_cmpeq_epi8(va, zero), _mm_cmpeq_epi8(vb, zero)));
			unsigned stop = ((~eq) & 0xFFFFu) | z;
			if (stop) {
				unsigned long i;
				_BitScanForward(&i, stop);
				return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
			}
		}
		a += 16; b += 16; n -= 16;
	}
	while (n >= 8) {
		size_t oa = (size_t)(const void *)a & 0xFFFu;
		size_t ob = (size_t)(const void *)b & 0xFFFu;
		if (oa > 0xFF8u || ob > 0xFF8u) break;
		{
			unsigned __int64 wa = *(const unsigned __int64 *)(const void *)a;
			unsigned __int64 wb = *(const unsigned __int64 *)(const void *)b;
			if (wa != wb) {
				int i;
				for (i = 0; i < 8; i++) {
					unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
					if (ca != cb || ca == 0) return (int)ca - (int)cb;
				}
			}
			if (HAS_ZERO8(wa)) return 0;
		}
		a += 8; b += 8; n -= 8;
	}
	while (n--) {
		unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
		if (ca != cb || ca == 0) return (int)ca - (int)cb;
		a++; b++;
	}
	return 0;
}

int strcmp(const char *a, const char *b) { return str_cmp_sse(a, b, (size_t)-1); }
int strncmp(const char *a, const char *b, size_t n) { return str_cmp_sse(a, b, n); }

char *strcpy(char *d, const char *s)
{
	char *p = d;
	while ((*p++ = *s++)) ;
	return d;
}

char *strncpy(char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++) d[i] = s[i];
	for (; i < n; i++) d[i] = 0;
	return d;
}

char *strcat(char *d, const char *s)
{
	char *p = d;
	while (*p) p++;
	while (*s) *p++ = *s++;
	*p = 0;
	return d;
}

char *strncat(char *d, const char *s, size_t n)
{
	char *p = d;
	while (*p) p++;
	while (n && *s) { *p++ = *s++; n--; }
	*p = 0;
	return d;
}

char *strrchr(const char *s, int c)
{
	const char *last = 0;
	while (*s) {
		if (*s == (char)c) last = s;
		s++;
	}
	if ((char)c == 0) return (char *)s;
	return (char *)last;
}

char *strchr(const char *s, int c)
{
	while (*s) {
		if (*s == (char)c) return (char *)s;
		s++;
	}
	return (c == 0) ? (char *)s : NULL;
}

char *strstr(const char *hay, const char *needle)
{
	size_t nl = strlen(needle);
	if (nl == 0) return (char *)hay;
	for (; *hay; hay++) {
		if (!memcmp(hay, needle, nl)) return (char *)hay;
	}
	return NULL;
}

long strtol(const char *s, char **end, int base)
{
	long v = 0;
	int neg = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { neg = 1; s++; }
	else if (*s == '+') s++;
	if (base == 0) {
		if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
		else if (s[0] == '0') base = 8;
		else base = 10;
	}
	while (*s) {
		int d;
		if (*s >= '0' && *s <= '9') d = *s - '0';
		else if (base == 16 && *s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
		else if (base == 16 && *s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
		else break;
		if (d >= base) break;
		v = v * base + d;
		s++;
	}
	if (end) *end = (char *)s;
	return neg ? -v : v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
	unsigned long v = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '+') s++;
	if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	while (*s) {
		int d;
		if (*s >= '0' && *s <= '9') d = *s - '0';
		else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
		else break;
		if (d >= base) break;
		v = v * (unsigned long)base + (unsigned long)d;
		s++;
	}
	if (end) *end = (char *)s;
	return v;
}

int atoi(const char *s)
{
	int v = 0, neg = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { neg = 1; s++; }
	else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
	return neg ? -v : v;
}

void exit(int code) { ExitProcess((unsigned int)code); }
void abort(void) { ExitProcess(3); }

void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *))
{
	unsigned char *b = (unsigned char *)base;
	size_t i, j;
	unsigned char tmp[256];
	if (size > 256) return;
	for (i = 1; i < nmemb; i++) {
		memcpy(tmp, b + i * size, size);
		for (j = i; j > 0 && cmp(tmp, b + (j - 1) * size) < 0; j--)
			memcpy(b + j * size, b + (j - 1) * size, size);
		memcpy(b + j * size, tmp, size);
	}
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
	int (*cmp)(const void *, const void *))
{
	size_t lo = 0, hi = nmemb;
	const unsigned char *b = (const unsigned char *)base;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		int c = cmp(key, b + mid * size);
		if (c == 0) return (void *)(b + mid * size);
		if (c < 0) hi = mid;
		else lo = mid + 1;
	}
	return 0;
}

/* ── 浮点/locale ─────────────────────────────────────────────────── */
double strtod(const char *s, char **end)
{
	double v = 0.0;
	int neg = 0, i;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { neg = 1; s++; }
	else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; }
	if (*s == '.') {
		double frac = 1.0;
		s++;
		while (*s >= '0' && *s <= '9') {
			frac /= 10.0;
			v += (*s - '0') * frac;
			s++;
		}
	}
	if (*s == 'e' || *s == 'E') {
		int eneg = 0, ev = 0;
		s++;
		if (*s == '-') { eneg = 1; s++; }
		else if (*s == '+') s++;
		while (*s >= '0' && *s <= '9') { ev = ev * 10 + (*s - '0'); s++; }
		{
			double e = 1.0;
			for (i = 0; i < ev; i++) e *= eneg ? 0.1 : 10.0;
			v *= e;
		}
	}
	if (end) *end = (char *)s;
	return neg ? -v : v;
}

/* cJSON 等只用 "%lg"；其余格式返回 -1 */
int sscanf(const char *buf, const char *fmt, ...)
{
	if (fmt && fmt[0] == '%' && fmt[1] == 'l' && fmt[2] == 'g' && fmt[3] == '\0') {
		va_list ap;
		double *val;
		char *end = NULL;
		va_start(ap, fmt);
		val = va_arg(ap, double *);
		va_end(ap);
		if (!val) return -1;
		*val = strtod(buf, &end);
		return (end > buf) ? 1 : 0;
	}
	return -1;
}

static struct lconv gLconv;
struct lconv *localeconv(void)
{
	static char dot[] = ".";
	if (!gLconv.decimal_point) gLconv.decimal_point = dot;
	return &gLconv;
}

int tolower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

double fabs(double d) { return d < 0 ? -d : d; }

/* 牛顿迭代；D2D/UI 里偶有 sqrt 需求 */
double sqrt(double x)
{
	double g;
	int i;
	if (x <= 0) return 0;
	if (x == 1.0) return 1.0;
	g = x;
	for (i = 0; i < 64; i++) {
		double ng = (g + x / g) * 0.5;
		if (ng == g) break;
		g = ng;
	}
	return g;
}

int _dclass(double x)
{
	if (x != x) return 2;
	if ((x * 0) != 0) return (x > 0) ? 0x0200 : 0x0004;
	if (x == 0.0) return 0x0040;
	return (x > 0) ? 0x0100 : 0x0008;
}

int _fltused = 0;

/* ── 独立堆 + 16 字节块头 ──────────────────────────────────────────
 * 头 16B: [0]=size（realloc 用）。返回 p+16，保持 16B 对齐。 */
#define CRT_HDR 16
static HANDLE g_heap;

static HANDLE cui_heap(void)
{
	HANDLE h = g_heap, winner;
	if (h) return h;
	h = HeapCreate(0, 0x100000, 0);
	winner = (HANDLE)InterlockedCompareExchangePointer(&g_heap, h, NULL);
	if (winner) { HeapDestroy(h); return winner; }
	return h;
}

void *malloc(size_t sz)
{
	void *p;
	if (sz == 0) sz = 1;
	p = HeapAlloc(cui_heap(), 0, sz + CRT_HDR);
	if (!p) return NULL;
	((size_t *)p)[0] = sz;
	return (char *)p + CRT_HDR;
}

void free(void *p)
{
	if (p) HeapFree(cui_heap(), 0, (char *)p - CRT_HDR);
}

void *calloc(size_t nmemb, size_t size)
{
	size_t total;
	void *p;
	if (size && nmemb > (size_t)-1 / size) return NULL;
	total = nmemb * size;
	p = malloc(total);
	if (p) memset(p, 0, total);
	return p;
}

void *realloc(void *p, size_t sz)
{
	size_t old;
	void *q;
	if (!p) return malloc(sz);
	if (sz == 0) { free(p); return NULL; }
	old = ((size_t *)p)[-2];
	if (old >= sz) return p;
	q = malloc(sz);
	if (q) {
		memcpy(q, p, old);
		free(p);
	}
	return q;
}

/* ── printf/vsnprintf ───────────────────────────────────────────────
 * 支持 flags(- + space # 0)、宽度/精度(含 *)、l/ll/z/I64、%s/%d/%u/%x/%X/
 * %c/%p/%n/%f/%e/%g/%%；%e/%g 按定点输出（日志/UI 足够，不会错位）。
 * snprintf/vsnprintf 有界；sprintf/vsprintf 由调用方保证缓冲。 */
static int FmtDigits(char *out, unsigned long long v, int base, int width)
{
	char tmp[24];
	char padch;
	int n = 0, i = 0, w, padn;
	if (width < 0) { padch = ' '; w = -width; }
	else { padch = '0'; w = width; }
	if (w < 0 || w > 16) w = 16;
	if (v == 0) tmp[n++] = '0';
	while (v > 0) {
		int d = (int)(v % base);
		tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
		v /= base;
	}
	padn = w > n ? w - n : 0;
	while (padn-- > 0) out[i++] = padch;
	while (n > 0) out[i++] = tmp[--n];
	return i;
}

static int FmtBody(char *buf, size_t cap, const char *fmt, va_list ap)
{
	char *p = buf;
	char *end = (cap == (size_t)-1) ? NULL : buf + cap;

	while (*fmt) {
		char c;
		int width = 0, zero = 0, minus = 0, longCount = 0, sizeCount = 0, prec = -1;
		if (end && p + 1 >= end) break;
		c = *fmt++;
		if (c != '%') { *p++ = c; continue; }
		for (;;) {
			if (*fmt == '-') { minus = 1; fmt++; continue; }
			if (*fmt == '+' || *fmt == ' ' || *fmt == '#') { fmt++; continue; }
			if (*fmt == '0') { zero = 1; fmt++; continue; }
			break;
		}
		if (*fmt == '*') {
			fmt++;
			width = va_arg(ap, int);
			if (width < 0) width = -width;
			if (width > 32) width = 32;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			if (*fmt == '0' && width == 0) zero = 1;
			width = width * 10 + (*fmt++ - '0');
			if (width > 32) width = 32;
		}
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				fmt++;
				prec = va_arg(ap, int);
				if (prec < 0) prec = -1;
				if (prec > 12) prec = 12;
			}
			while (*fmt >= '0' && *fmt <= '9') {
				prec = prec * 10 + (*fmt++ - '0');
				if (prec > 12) prec = 12;
			}
		}
		while (*fmt == 'l') { longCount++; fmt++; }
		while (*fmt == 'h') { fmt++; }
		if (*fmt == 'z' || *fmt == 'I') {
			sizeCount = 1;
			if (*fmt == 'I' && fmt[1] == '6' && fmt[2] == '4') fmt += 3;
			else fmt++;
		}
		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			int i = 0, n2;
			if (!s) s = "(null)";
			else if ((size_t)s < 0x10000) s = "(badptr)";
			while (s[i] && (prec < 0 || i < prec)) i++;
			if (!minus) for (n2 = width - i; n2 > 0; n2--) {
				if (end && p + 1 >= end) break;
				*p++ = ' ';
			}
			for (n2 = 0; n2 < i; n2++) {
				if (end && p + 1 >= end) break;
				*p++ = s[n2];
			}
			if (minus) for (n2 = width - i; n2 > 0; n2--) {
				if (end && p + 1 >= end) break;
				*p++ = ' ';
			}
			break;
		}
		case 'd': case 'u': case 'x': case 'X': {
			char num[48];
			unsigned long long uv;
			int base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
			int i = 0, nn;
			if (sizeCount) {
				if (*fmt == 'd') {
					long long sv = va_arg(ap, long long);
					if (sv < 0) { if (!end || p + 1 < end) *p++ = '-'; uv = (unsigned long long)(-sv); }
					else uv = (unsigned long long)sv;
				} else {
					uv = (unsigned long long)va_arg(ap, size_t);
				}
			} else if (longCount >= 2) {
				if (*fmt == 'd') {
					long long sv = va_arg(ap, long long);
					if (sv < 0) { if (!end || p + 1 < end) *p++ = '-'; uv = (unsigned long long)(-sv); }
					else uv = (unsigned long long)sv;
				} else {
					uv = va_arg(ap, unsigned long long);
				}
			} else if (longCount == 1) {
				if (*fmt == 'd') {
					long sv = va_arg(ap, long);
					if (sv < 0) { if (!end || p + 1 < end) *p++ = '-'; uv = (unsigned long long)(-sv); }
					else uv = (unsigned long long)sv;
				} else {
					uv = (unsigned long long)va_arg(ap, unsigned long);
				}
			} else if (*fmt == 'd') {
				int sv = va_arg(ap, int);
				if (sv < 0) { if (!end || p + 1 < end) *p++ = '-'; uv = (unsigned long long)(-sv); }
				else uv = (unsigned long long)sv;
			} else {
				uv = (unsigned long long)va_arg(ap, unsigned int);
			}
			if (minus) {
				int pad;
				nn = FmtDigits(num, uv, base, 0);
				while (i < nn) {
					if (end && p + 1 >= end) break;
					*p++ = num[i++];
				}
				for (pad = width - nn; pad > 0; pad--) {
					if (end && p + 1 >= end) break;
					*p++ = ' ';
				}
			} else {
				nn = FmtDigits(num, uv, base, zero ? width : -width);
				while (i < nn) {
					if (end && p + 1 >= end) break;
					*p++ = num[i++];
				}
			}
			break;
		}
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
			double dv = va_arg(ap, double);
			char num[48];
			unsigned long long ipart;
			double frac, sc = 1.0;
			int k, nd = (prec < 0) ? 6 : prec;
			if (nd > 12) nd = 12;
			if (dv < 0) { if (!end || p + 1 < end) *p++ = '-'; dv = -dv; }
			for (k = 0; k < nd; k++) sc *= 10.0;
			dv += 0.5 / sc;
			ipart = (unsigned long long)dv;
			frac = dv - (double)ipart;
			k = 0;
			{
				int n2 = FmtDigits(num, ipart, 10, -1);
				while (k < n2) {
					if (end && p + 1 >= end) break;
					*p++ = num[k++];
				}
			}
			if (nd > 0) {
				if (!end || p + 1 < end) *p++ = '.';
				for (k = 0; k < nd; k++) {
					int dg;
					frac *= 10.0;
					dg = (int)frac;
					if (dg < 0) dg = 0;
					if (dg > 9) dg = 9;
					frac -= dg;
					if (end && p + 1 >= end) break;
					*p++ = (char)('0' + dg);
				}
			}
			break;
		}
		case 'p': {
			char num[48];
			int k = 0, n2;
			void *pv = va_arg(ap, void *);
			if (!end || p + 1 < end) *p++ = '0';
			if (!end || p + 1 < end) *p++ = 'x';
			n2 = FmtDigits(num, (unsigned long long)(size_t)pv, 16, -1);
			while (k < n2) {
				if (end && p + 1 >= end) break;
				*p++ = num[k++];
			}
			break;
		}
		case 'n': {
			int *np = va_arg(ap, int *);
			if (np) *np = (int)(p - buf);
			break;
		}
		case 'c':
			if (!end || p < end - 1) *p++ = (char)va_arg(ap, int);
			else va_arg(ap, int);
			break;
		case '%':
			if (!end || p + 1 < end) *p++ = '%';
			break;
		default:
			if (!end || p + 2 < end) { *p++ = '%'; *p++ = c; }
			break;
		}
		if (*fmt) fmt++;
	}
	if (end && p > end - 1) p = end - 1;
	*p = 0;
	return (int)(p - buf);
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
	return FmtBody(buf, (size_t)-1, fmt, ap);
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
	if (cap == 0) return 0;
	return FmtBody(buf, cap, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = FmtBody(buf, (size_t)-1, fmt, ap);
	va_end(ap);
	return n;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
	va_list ap;
	int n;
	if (cap == 0) return 0;
	va_start(ap, fmt);
	n = FmtBody(buf, cap, fmt, ap);
	va_end(ap);
	return n;
}

/* MSVC /RTC1 的运行时检查符号（0CRT 下提供空实现） */
void __cdecl _RTC_CheckEsp(void) { }
void __cdecl _RTC_InitBase(void) { }
void __cdecl _RTC_Shutdown(void) { }
void __cdecl _RTC_CheckStackVars(void *_Esp, void *_FrameDescriptor)
{
	(void)_Esp; (void)_FrameDescriptor;
}

#endif /* NO_CRT_NO_DEFAULTLIB */
