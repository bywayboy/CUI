/*
 * net_base.c - 动态缓冲 / 字符串 / 编码 (0CRT)
 */
#include "net/net_base.h"

#include <bcrypt.h>
#include <wincrypt.h>
#include <limits.h>
#include <stdarg.h>

/* crtstub 提供 */
int sprintf(char *buf, const char *fmt, ...);
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
void *malloc(size_t sz);
void *realloc(void *p, size_t sz);
void free(void *p);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
size_t strlen(const char *s);

/* ── NetBuf ─────────────────────────────────────────────────────────── */

void NetBuf_Init(NetBuf *b)
{
	b->p = NULL;
	b->len = 0;
	b->cap = 0;
}

void NetBuf_Free(NetBuf *b)
{
	if (b->p) free(b->p);
	b->p = NULL;
	b->len = 0;
	b->cap = 0;
}

void NetBuf_Clear(NetBuf *b)
{
	b->len = 0;
	if (b->p) b->p[0] = '\0';
}

int NetBuf_Reserve(NetBuf *b, int extra)
{
	int need, cap;
	char *np;

	if (extra < 0) return 0;
	need = b->len + extra;
	if (need < 0) return 0;                    /* int 溢出 */
	if (need + 1 <= b->cap) return 1;          /* +1 留给 '\0' */

	cap = b->cap > 0 ? b->cap : 256;
	while (cap <= need) {
		if (cap > INT_MAX / 2) { cap = need + 1; break; }
		cap *= 2;
	}
	np = (char *)realloc(b->p, (size_t)cap);
	if (!np) return 0;
	b->p = np;
	b->cap = cap;
	b->p[b->len] = '\0';
	return 1;
}

int NetBuf_Append(NetBuf *b, const void *data, int n)
{
	if (n <= 0) {
		if (!b->p) {
			if (!NetBuf_Reserve(b, 1)) return 0;
			b->p[0] = '\0';
		}
		return 1;
	}
	if (!data) return 0;
	if (!NetBuf_Reserve(b, n)) return 0;
	memcpy(b->p + b->len, data, (size_t)n);
	b->len += n;
	b->p[b->len] = '\0';
	return 1;
}

int NetBuf_AppendStr(NetBuf *b, const char *s)
{
	if (!s) return 1;
	return NetBuf_Append(b, s, (int)strlen(s));
}

int NetBuf_AppendChar(NetBuf *b, char c)
{
	return NetBuf_Append(b, &c, 1);
}

int NetBuf_AppendFmt(NetBuf *b, const char *fmt, ...)
{
	char tmp[1024];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0) return 0;
	if ((size_t)n >= sizeof(tmp)) n = (int)sizeof(tmp) - 1;
	return NetBuf_Append(b, tmp, n);
}

void NetBuf_Consume(NetBuf *b, int n)
{
	if (n <= 0) return;
	if (n >= b->len) {
		b->len = 0;
		if (b->p) b->p[0] = '\0';
		return;
	}
	memmove(b->p, b->p + n, (size_t)(b->len - n));
	b->len -= n;
	b->p[b->len] = '\0';
}

/* ── 字符串 ─────────────────────────────────────────────────────────── */

static int lower_(int c)
{
	if (c >= 'A' && c <= 'Z') return c + 32;
	return c;
}

int Net_StrCaseCmpN(const char *a, const char *b, int n)
{
	int i;
	if (!a || !b) return a ? 1 : (b ? -1 : 0);
	for (i = 0; i < n; i++) {
		int ca = lower_((unsigned char)a[i]);
		int cb = lower_((unsigned char)b[i]);
		if (ca != cb) return ca - cb;
		if (!ca) return 0;
	}
	return 0;
}

int Net_StrCaseEq(const char *a, const char *b)
{
	int i;
	if (!a || !b) return a == b;
	for (i = 0;; i++) {
		int ca = lower_((unsigned char)a[i]);
		int cb = lower_((unsigned char)b[i]);
		if (ca != cb) return 0;
		if (!ca) return 1;
	}
}

char *Net_StrCaseStr(const char *hay, const char *needle)
{
	int hl, nl, i;
	if (!hay || !needle) return NULL;
	hl = (int)strlen(hay);
	nl = (int)strlen(needle);
	if (nl > hl) return NULL;
	for (i = 0; i + nl <= hl; i++)
		if (Net_StrCaseCmpN(hay + i, needle, nl) == 0)
			return (char *)(hay + i);
	return NULL;
}

int Net_StrCopy(char *dst, int cap, const char *src)
{
	int n;
	if (!dst || cap <= 0) return 0;
	if (!src) { dst[0] = '\0'; return 0; }
	n = (int)strlen(src);
	if (n >= cap) n = cap - 1;
	memcpy(dst, src, (size_t)n);
	dst[n] = '\0';
	return n;
}

char *Net_MemFind(const char *buf, int len, const char *needle, int nlen)
{
	int i;
	if (!buf || !needle || nlen <= 0 || len < nlen) return NULL;
	for (i = 0; i + nlen <= len; i++)
		if (buf[i] == needle[0] && memcmp(buf + i, needle, (size_t)nlen) == 0)
			return (char *)(buf + i);
	return NULL;
}

int Net_HexVal(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

void Net_HexLower(const BYTE *in, int n, char *out)
{
	static const char hex[] = "0123456789abcdef";
	int i;
	for (i = 0; i < n; i++) {
		out[i * 2] = hex[(in[i] >> 4) & 0xF];
		out[i * 2 + 1] = hex[in[i] & 0xF];
	}
	out[n * 2] = '\0';
}

/* ── 编码 / 随机 ────────────────────────────────────────────────────── */

int Net_Sha1(const void *data, int len, BYTE out[20])
{
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	DWORD objLen = 0, got = 0;
	BYTE *obj = NULL;
	int ok = 0;
	static const BYTE empty = 0;

	if (!data) { data = &empty; len = 0; }
	if (len < 0) return 0;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0) < 0)
		return 0;
	if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen,
			sizeof(objLen), &got, 0) < 0)
		goto done;
	obj = (BYTE *)malloc(objLen ? objLen : 1);
	if (!obj) goto done;
	if (BCryptCreateHash(alg, &hash, obj, objLen, NULL, 0, 0) < 0)
		goto done;
	if (len > 0 && BCryptHashData(hash, (PUCHAR)data, (ULONG)len, 0) < 0)
		goto done;
	if (BCryptFinishHash(hash, out, 20, 0) < 0)
		goto done;
	ok = 1;

done:
	if (hash) BCryptDestroyHash(hash);
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	if (obj) free(obj);
	return ok;
}

int Net_Base64(const BYTE *in, int len, char *out, int cap)
{
	DWORD n = 0;
	if (!out || cap <= 1) return 0;
	out[0] = '\0';
	if (len == 0) return 0;
	if (!CryptBinaryToStringA(in, (DWORD)len,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &n))
		return 0;
	if ((int)n > cap) return 0;
	if (!CryptBinaryToStringA(in, (DWORD)len,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &n))
		return 0;
	out[cap - 1] = '\0';
	return (int)strlen(out);
}

int Net_Random(void *buf, int len)
{
	if (!buf || len <= 0) return 0;
	return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
		BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

uint64_t Net_NowMs(void)
{
	return GetTickCount64();
}
