/*
 * mem_avx2.c - AVX2 大块内存操作 (crtstub.c 在 CPU 支持时调用)
 *
 * 单独一个 TU: _mm256_* 必须 /arch:AVX2, 不能把整个 crtstub 升到 AVX2
 * (老 CPU 会非法指令). 本文件只定义 mem*_avx2, 不定义 memcpy/memset 名字.
 */
#include <windows.h>
#include <immintrin.h>
#include <intrin.h>
#include <stddef.h>

extern int memcmp(const void *a, const void *b, size_t n);

int memcmp_avx2(const void *a, const void *b, size_t n)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;

	while (n >= 32) {
		__m256i va = _mm256_loadu_si256((const __m256i *)(const void *)pa);
		__m256i vb = _mm256_loadu_si256((const __m256i *)(const void *)pb);
		unsigned m = (unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(va, vb));
		if (m != 0xFFFFFFFFu) {
			unsigned long i;
			_BitScanForward(&i, ~m);
			_mm256_zeroupper();
			return (int)pa[i] - (int)pb[i];
		}
		pa += 32; pb += 32; n -= 32;
	}
	_mm256_zeroupper();
	return memcmp(pa, pb, n);
}

void memcpy_avx2(void *d, const void *s, size_t n)
{
	unsigned char *p = (unsigned char *)d;
	const unsigned char *q = (const unsigned char *)s;

	while (n >= 128) {
		__m256i a = _mm256_loadu_si256((const __m256i *)(const void *)q);
		__m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(q + 32));
		__m256i c = _mm256_loadu_si256((const __m256i *)(const void *)(q + 64));
		__m256i e = _mm256_loadu_si256((const __m256i *)(const void *)(q + 96));
		_mm256_storeu_si256((__m256i *)(void *)p, a);
		_mm256_storeu_si256((__m256i *)(void *)(p + 32), b);
		_mm256_storeu_si256((__m256i *)(void *)(p + 64), c);
		_mm256_storeu_si256((__m256i *)(void *)(p + 96), e);
		p += 128; q += 128; n -= 128;
	}
	while (n >= 32) {
		_mm256_storeu_si256((__m256i *)(void *)p,
			_mm256_loadu_si256((const __m256i *)(const void *)q));
		p += 32; q += 32; n -= 32;
	}
	if (n >= 16) {
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
	_mm256_zeroupper();
}

void memset_avx2(void *d, int c, size_t n)
{
	unsigned char *p = (unsigned char *)d;
	__m256i v = _mm256_set1_epi8((char)c);
	__m128i v128 = _mm256_castsi256_si128(v);
	unsigned int c4 = (unsigned int)(c & 0xFF) * 0x01010101u;
	unsigned short c2 = (unsigned short)((c & 0xFF) * 0x0101);

	while (n >= 128) {
		_mm256_storeu_si256((__m256i *)(void *)p, v);
		_mm256_storeu_si256((__m256i *)(void *)(p + 32), v);
		_mm256_storeu_si256((__m256i *)(void *)(p + 64), v);
		_mm256_storeu_si256((__m256i *)(void *)(p + 96), v);
		p += 128; n -= 128;
	}
	while (n >= 32) {
		_mm256_storeu_si256((__m256i *)(void *)p, v);
		p += 32; n -= 32;
	}
	if (n >= 16) { _mm_storeu_si128((__m128i *)(void *)p, v128); p += 16; n -= 16; }
	if (n & 8) { _mm_storel_epi64((__m128i *)(void *)p, v128); p += 8; }
	if (n & 4) { *(unsigned int *)(void *)p = c4; p += 4; }
	if (n & 2) { *(unsigned short *)(void *)p = c2; p += 2; }
	if (n & 1) *p = (unsigned char)c;
	_mm256_zeroupper();
}

void memmove_avx2(void *d, const void *s, size_t n)
{
	unsigned char *p = (unsigned char *)d;
	const unsigned char *q = (const unsigned char *)s;

	if (n == 0 || p == q) { _mm256_zeroupper(); return; }
	if (p < q || (size_t)(p - q) >= n) { memcpy_avx2(d, s, n); return; }

	p += n; q += n;
	while (n >= 128) {
		__m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(q - 32));
		__m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(q - 64));
		__m256i c = _mm256_loadu_si256((const __m256i *)(const void *)(q - 96));
		__m256i e = _mm256_loadu_si256((const __m256i *)(const void *)(q - 128));
		_mm256_storeu_si256((__m256i *)(void *)(p - 32), a);
		_mm256_storeu_si256((__m256i *)(void *)(p - 64), b);
		_mm256_storeu_si256((__m256i *)(void *)(p - 96), c);
		_mm256_storeu_si256((__m256i *)(void *)(p - 128), e);
		p -= 128; q -= 128; n -= 128;
	}
	while (n >= 32) {
		_mm256_storeu_si256((__m256i *)(void *)(p - 32),
			_mm256_loadu_si256((const __m256i *)(const void *)(q - 32)));
		p -= 32; q -= 32; n -= 32;
	}
	while (n >= 16) {
		_mm_storeu_si128((__m128i *)(void *)(p - 16),
			_mm_loadu_si128((const __m128i *)(const void *)(q - 16)));
		p -= 16; q -= 16; n -= 16;
	}
	while (n--) { p--; q--; *p = *q; }
	_mm256_zeroupper();
}
