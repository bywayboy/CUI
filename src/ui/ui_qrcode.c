/*
 * ui_qrcode.c - 二维码编码 (Byte 模式) 与组件绘制
 *
 * 编码流程按 ISO/IEC 18004: 选版本 -> 位流 -> RS 纠错/交织 ->
 * 功能图案/数据放置 -> 8 掩码惩罚评分 -> 输出位打包矩阵.
 */
#include "ui/ui_qrcode.h"

#include <initguid.h>
#include <wincodec.h>

void *malloc(size_t sz);
void free(void *p);
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
size_t strlen(const char *s);
int memcmp(const void *a, const void *b, size_t n);

/* ── 规格表 ─────────────────────────────────────────────────────────── */

/* 每块纠错码字数; 下标 [ecc][version], version 0 占位. */
static const unsigned char kEccPerBlock[4][41] = {
	{ 0, 7,10,15,20,26,18,20,24,30,18,20,24,26,30,22,24,28,30,28,
	  28,28,28,30,30,26,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30 },
	{ 0,10,16,26,18,24,16,18,22,22,26,30,22,22,24,24,28,28,26,26,
	  26,26,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28 },
	{ 0,13,22,18,26,18,24,18,22,20,24,28,26,24,20,30,24,28,28,26,
	  30,28,30,30,30,30,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30 },
	{ 0,17,28,22,16,22,28,26,26,24,28,24,28,22,24,24,30,28,28,26,
	  28,30,24,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30 }
};

/* 纠错块数; 下标 [ecc][version]. */
static const unsigned char kBlocks[4][41] = {
	{ 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7,
	  8, 8, 9, 9,10,12,12,12,13,14,15,16,17,18,19,19,20,21,22,24,25 },
	{ 0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9,10,10,11,13,14,
	 16,17,17,18,20,21,23,25,26,28,29,31,33,35,37,38,40,43,45,47,49 },
	{ 0, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8,10,12,16,12,17,16,18,21,
	 20,23,23,25,27,29,34,34,35,38,40,43,45,48,51,53,56,59,62,65,68 },
	{ 0, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8,11,11,16,16,18,16,19,21,25,
	 25,25,34,30,32,35,37,40,42,45,48,51,54,57,60,63,66,70,74,77,81 }
};

/* 含纠错在内的总码字数所需的原始模块数 (bits). */
static int qr_raw_data_modules(int ver)
{
	int result = (16 * ver + 128) * ver + 64;
	if (ver >= 2) {
		int numAlign = ver / 7 + 2;
		result -= (25 * numAlign - 10) * numAlign - 55;
		if (ver >= 7) result -= 36;
	}
	return result;
}

static int qr_data_codewords(int ver, int ecc)
{
	int raw = qr_raw_data_modules(ver) / 8;
	return raw - kEccPerBlock[ecc][ver] * kBlocks[ecc][ver];
}

static int qr_capacity_bytes(int ver, int ecc)
{
	int dataBits = qr_data_codewords(ver, ecc) * 8;
	int countBits = ver <= 9 ? 8 : 16;
	int cap = (dataBits - 4 - countBits) / 8;
	return cap > 0 ? cap : 0;
}

/* ── GF(256) / RS ───────────────────────────────────────────────────── */

static unsigned char kGfExp[512];
static unsigned char kGfLog[256];
static int kGfReady;

static void qr_gf_init(void)
{
	int i, x = 1;
	if (kGfReady) return;
	for (i = 0; i < 255; i++) {
		kGfExp[i] = (unsigned char)x;
		kGfLog[x] = (unsigned char)i;
		x <<= 1;
		if (x & 0x100) x ^= 0x11D;
	}
	for (i = 255; i < 512; i++) kGfExp[i] = kGfExp[i - 255];
	kGfLog[0] = 0;
	kGfReady = 1;
}

static unsigned char gf_mul(unsigned char a, unsigned char b)
{
	if (!a || !b) return 0;
	return kGfExp[(int)kGfLog[a] + (int)kGfLog[b]];
}

static void qr_rs_divisor(int degree, unsigned char *out)
{
	int i, j;
	unsigned char root = 1;
	memset(out, 0, (size_t)degree);
	out[degree - 1] = 1;
	for (i = 0; i < degree; i++) {
		for (j = 0; j < degree; j++) {
			out[j] = gf_mul(out[j], root);
			if (j + 1 < degree) out[j] ^= out[j + 1];
		}
		root = gf_mul(root, 0x02);
	}
}

static void qr_rs_rem(const unsigned char *data, int len,
	const unsigned char *div, int deg, unsigned char *out)
{
	int i, j;
	unsigned char factor;
	memset(out, 0, (size_t)deg);
	for (i = 0; i < len; i++) {
		factor = (unsigned char)(data[i] ^ out[0]);
		for (j = 0; j < deg - 1; j++) out[j] = out[j + 1];
		out[deg - 1] = 0;
		for (j = 0; j < deg; j++) out[j] ^= gf_mul(div[j], factor);
	}
}

/* 分块计算纠错并交织; out 长度 = raw codewords. */
static void qr_interleave(const unsigned char *data, int ver, int ecc,
	unsigned char *out)
{
	unsigned char eccBuf[81 * 30];
	unsigned char div[30];
	int off[81], dlen[81];
	int raw = qr_raw_data_modules(ver) / 8;
	int nb = kBlocks[ecc][ver];
	int ecLen = kEccPerBlock[ecc][ver];
	int shortFull = raw / nb;
	int nshort = nb - raw % nb;
	int datShort = shortFull - ecLen;
	int i, j, k = 0, pos = 0;

	qr_rs_divisor(ecLen, div);
	for (i = 0; i < nb; i++) {
		dlen[i] = datShort + (i >= nshort ? 1 : 0);
		off[i] = pos;
		pos += dlen[i];
		qr_rs_rem(data + off[i], dlen[i], div, ecLen, eccBuf + i * ecLen);
	}
	for (i = 0; i <= datShort; i++)
		for (j = 0; j < nb; j++)
			if (i < dlen[j]) out[k++] = data[off[j] + i];
	for (i = 0; i < ecLen; i++)
		for (j = 0; j < nb; j++)
			out[k++] = eccBuf[j * ecLen + i];
}

/* ── 矩阵 ───────────────────────────────────────────────────────────── */

static void qr_set_func(unsigned char *mod, unsigned char *func, int size,
	int x, int y, int dark)
{
	if (x < 0 || y < 0 || x >= size || y >= size) return;
	mod[y * size + x] = (unsigned char)(dark ? 1 : 0);
	func[y * size + x] = 1;
}

/* 中心 (cx,cy), 含 1 模块分隔区 (9x9). */
static void qr_draw_finder(unsigned char *mod, unsigned char *func, int size,
	int cx, int cy)
{
	int dx, dy;
	for (dy = -4; dy <= 4; dy++)
		for (dx = -4; dx <= 4; dx++) {
			int ax = dx < 0 ? -dx : dx;
			int ay = dy < 0 ? -dy : dy;
			int dist = ax > ay ? ax : ay;
			qr_set_func(mod, func, size, cx + dx, cy + dy,
				dist != 2 && dist != 4);
		}
}

static void qr_draw_align(unsigned char *mod, unsigned char *func, int size,
	int cx, int cy)
{
	int dx, dy;
	for (dy = -2; dy <= 2; dy++)
		for (dx = -2; dx <= 2; dx++) {
			int ax = dx < 0 ? -dx : dx;
			int ay = dy < 0 ? -dy : dy;
			int dist = ax > ay ? ax : ay;
			qr_set_func(mod, func, size, cx + dx, cy + dy, dist != 1);
		}
}

static int qr_align_positions(int ver, int *pos)
{
	int num, step, i, p;
	if (ver == 1) return 0;
	num = ver / 7 + 2;
	step = ver == 32 ? 26
		: (ver * 4 + num * 2 + 1) / (num * 2 - 2) * 2;
	pos[0] = 6;
	pos[num - 1] = ver * 4 + 10;   /* size - 7 */
	for (i = num - 2, p = pos[num - 1] - step; i >= 1; i--, p -= step)
		pos[i] = p;
	return num;
}

static int qr_format_word(int ecc, int mask)
{
	static const int kFormatEcc[4] = { 1, 0, 3, 2 };   /* L/M/Q/H */
	int data = (kFormatEcc[ecc] << 3) | mask;
	int rem = data, i;
	for (i = 0; i < 10; i++)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	return ((data << 10) | rem) ^ 0x5412;
}

static void qr_draw_format(unsigned char *mod, unsigned char *func, int size,
	int ecc, int mask)
{
	int bits = qr_format_word(ecc, mask);
	int i;
	for (i = 0; i <= 5; i++)
		qr_set_func(mod, func, size, 8, i, (bits >> i) & 1);
	qr_set_func(mod, func, size, 8, 7, (bits >> 6) & 1);
	qr_set_func(mod, func, size, 8, 8, (bits >> 7) & 1);
	qr_set_func(mod, func, size, 7, 8, (bits >> 8) & 1);
	for (i = 9; i < 15; i++)
		qr_set_func(mod, func, size, 14 - i, 8, (bits >> i) & 1);
	for (i = 0; i < 8; i++)
		qr_set_func(mod, func, size, size - 1 - i, 8, (bits >> i) & 1);
	for (i = 8; i < 15; i++)
		qr_set_func(mod, func, size, 8, size - 15 + i, (bits >> i) & 1);
	qr_set_func(mod, func, size, 8, size - 8, 1);   /* 固定暗模块 */
}

static void qr_draw_version(unsigned char *mod, unsigned char *func, int size,
	int ver)
{
	int bits, rem, i, a, b;
	if (ver < 7) return;
	rem = ver;
	for (i = 0; i < 12; i++)
		rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	bits = (ver << 12) | rem;
	for (i = 0; i < 18; i++) {
		int bit = (bits >> i) & 1;
		a = size - 11 + i % 3;
		b = i / 3;
		qr_set_func(mod, func, size, a, b, bit);
		qr_set_func(mod, func, size, b, a, bit);
	}
}

static void qr_draw_patterns(unsigned char *mod, unsigned char *func, int size,
	int ver, int ecc)
{
	int pos[8], num, i, j;
	memset(mod, 0, (size_t)size * (size_t)size);
	memset(func, 0, (size_t)size * (size_t)size);
	for (i = 0; i < size; i++) {
		qr_set_func(mod, func, size, 6, i, (i & 1) == 0);
		qr_set_func(mod, func, size, i, 6, (i & 1) == 0);
	}
	qr_draw_finder(mod, func, size, 3, 3);
	qr_draw_finder(mod, func, size, size - 4, 3);
	qr_draw_finder(mod, func, size, 3, size - 4);
	num = qr_align_positions(ver, pos);
	for (i = 0; i < num; i++)
		for (j = 0; j < num; j++) {
			if ((i == 0 && j == 0) || (i == 0 && j == num - 1) ||
			    (i == num - 1 && j == 0))
				continue;
			qr_draw_align(mod, func, size, pos[i], pos[j]);
		}
	qr_draw_format(mod, func, size, ecc, 0);   /* 先占位, 掩码评分时重写 */
	qr_draw_version(mod, func, size, ver);
}

static int qr_mask_on(int mask, int x, int y)
{
	switch (mask) {
	case 0: return ((x + y) & 1) == 0;
	case 1: return (y & 1) == 0;
	case 2: return x % 3 == 0;
	case 3: return (x + y) % 3 == 0;
	case 4: return ((y / 2 + x / 3) & 1) == 0;
	case 5: return (x * y) % 2 + (x * y) % 3 == 0;
	case 6: return (((x * y) % 2 + (x * y) % 3) & 1) == 0;
	case 7: return (((x + y) % 2 + (x * y) % 3) & 1) == 0;
	default: return 0;
	}
}

/* 规则 1/2/3/4 惩罚分. */
static long qr_penalty(const unsigned char *m, int size)
{
	long p = 0;
	int x, y, run, dark = 0;
	long total = (long)size * size;

	for (y = 0; y < size; y++) {
		run = 1;
		for (x = 1; x < size; x++) {
			if (m[y * size + x] == m[y * size + x - 1]) {
				run++;
				if (run == 5) p += 3;
				else if (run > 5) p += 1;
			} else run = 1;
		}
	}
	for (x = 0; x < size; x++) {
		run = 1;
		for (y = 1; y < size; y++) {
			if (m[y * size + x] == m[(y - 1) * size + x]) {
				run++;
				if (run == 5) p += 3;
				else if (run > 5) p += 1;
			} else run = 1;
		}
	}
	for (y = 0; y < size - 1; y++)
		for (x = 0; x < size - 1; x++) {
			int c = m[y * size + x];
			if (c == m[y * size + x + 1] &&
			    c == m[(y + 1) * size + x] &&
			    c == m[(y + 1) * size + x + 1])
				p += 3;
		}
	for (y = 0; y < size; y++)
		for (x = 0; x + 10 < size; x++) {
			int h = 0, v = 0, i;
			for (i = 0; i < 11; i++) {
				h = (h << 1) | m[y * size + x + i];
				v = (v << 1) | m[(x + i) * size + y];
			}
			if (h == 0x05D || h == 0x5D0) p += 40;
			if (v == 0x05D || v == 0x5D0) p += 40;
		}
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			dark += m[y * size + x] ? 1 : 0;
	{
		long k = (long)(((dark * 20L - total * 10L) < 0
			? total * 10L - dark * 20L : dark * 20L - total * 10L)
			+ total - 1) / total - 1;
		if (k > 0) p += k * 10;
	}
	return p;
}

static void qr_place_data(unsigned char *mod, const unsigned char *func,
	int size, const unsigned char *cw, int cwBytes)
{
	int right, vert, j, i = 0;
	int bits = cwBytes * 8;
	for (right = size - 1; right >= 1; right -= 2) {
		if (right == 6) right = 5;
		for (vert = 0; vert < size; vert++) {
			for (j = 0; j < 2; j++) {
				int x = right - j;
				int upward = ((right + 1) & 2) == 0;
				int y = upward ? size - 1 - vert : vert;
				if (!func[y * size + x] && i < bits) {
					mod[y * size + x] = (unsigned char)
						((cw[i >> 3] >> (7 - (i & 7))) & 1);
					i++;
				}
			}
		}
	}
}

static void qr_put_bits(unsigned char *buf, int *pos, int val, int nbits)
{
	int i;
	for (i = nbits - 1; i >= 0; i--) {
		int p = (*pos)++;
		if ((val >> i) & 1) buf[p >> 3] |= (unsigned char)(0x80 >> (p & 7));
	}
}

int UiQrCode_Encode(const char *utf8, int ecc, unsigned char *matrix,
	int matrixCap, int *outSize)
{
	unsigned char *data = NULL, *cw = NULL;
	unsigned char *mod = NULL, *func = NULL, *trial = NULL;
	int len, ver, size, rawBytes, dataCw, need, capBits, bp, i, x, y;
	int bestMask = -1, countBits, term;
	long best = 0;
	int ret = UI_QR_OK;

	if (!utf8 || !matrix || !outSize || ecc < 0 || ecc > 3)
		return UI_QR_E_PARAM;
	*outSize = 0;
	len = (int)strlen(utf8);
	if (len == 0) return UI_QR_E_EMPTY;
	if (len > UI_QR_TEXT_MAX) return UI_QR_E_LONG;
	for (ver = 1; ver <= UI_QR_MAX_VERSION; ver++)
		if (qr_capacity_bytes(ver, ecc) >= len) break;
	if (ver > UI_QR_MAX_VERSION) return UI_QR_E_LONG;

	size = ver * 4 + 17;
	rawBytes = qr_raw_data_modules(ver) / 8;
	dataCw = qr_data_codewords(ver, ecc);
	need = (size * size + 7) / 8;
	if (need > matrixCap) return UI_QR_E_PARAM;

	qr_gf_init();
	data = (unsigned char *)malloc((size_t)dataCw);
	cw = (unsigned char *)malloc((size_t)rawBytes);
	mod = (unsigned char *)malloc((size_t)size * (size_t)size);
	func = (unsigned char *)malloc((size_t)size * (size_t)size);
	trial = (unsigned char *)malloc((size_t)size * (size_t)size);
	if (!data || !cw || !mod || !func || !trial) {
		ret = UI_QR_E_OOM;
		goto done;
	}
	memset(data, 0, (size_t)dataCw);

	capBits = dataCw * 8;
	countBits = ver <= 9 ? 8 : 16;
	bp = 0;
	qr_put_bits(data, &bp, 0x4, 4);          /* Byte 模式 */
	qr_put_bits(data, &bp, len, countBits);
	for (i = 0; i < len; i++)
		qr_put_bits(data, &bp, (unsigned char)utf8[i], 8);
	term = capBits - bp;
	if (term > 4) term = 4;
	if (term > 0) qr_put_bits(data, &bp, 0, term);
	while (bp & 7) qr_put_bits(data, &bp, 0, 1);
	for (i = 0; bp < capBits; i++)
		qr_put_bits(data, &bp, (i & 1) ? 0x11 : 0xEC, 8);

	qr_interleave(data, ver, ecc, cw);
	qr_draw_patterns(mod, func, size, ver, ecc);
	qr_place_data(mod, func, size, cw, rawBytes);

	for (i = 0; i < 8; i++) {
		long p;
		memcpy(trial, mod, (size_t)size * (size_t)size);
		for (y = 0; y < size; y++)
			for (x = 0; x < size; x++)
				if (!func[y * size + x] && qr_mask_on(i, x, y))
					trial[y * size + x] ^= 1;
		qr_draw_format(trial, func, size, ecc, i);
		p = qr_penalty(trial, size);
		if (bestMask < 0 || p < best) {
			best = p;
			bestMask = i;
		}
	}

	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			if (!func[y * size + x] && qr_mask_on(bestMask, x, y))
				mod[y * size + x] ^= 1;
	qr_draw_format(mod, func, size, ecc, bestMask);

	memset(matrix, 0, (size_t)need);
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			if (mod[y * size + x]) {
				int p = y * size + x;
				matrix[p >> 3] |= (unsigned char)(0x80 >> (p & 7));
			}
	*outSize = size;

done:
	if (data) free(data);
	if (cw) free(cw);
	if (mod) free(mod);
	if (func) free(func);
	if (trial) free(trial);
	return ret;
}

/* ── 组件 ───────────────────────────────────────────────────────────── */

static uint32_t qr_premult(uint32_t c)
{
	unsigned a = (c >> 24) & 0xFFu;
	unsigned r = ((c >> 16) & 0xFFu) * a / 255u;
	unsigned g = ((c >> 8) & 0xFFu) * a / 255u;
	unsigned b = (c & 0xFFu) * a / 255u;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

void UiQrCode_Init(UiQrCode *q, int ecc)
{
	if (!q) return;
	memset(q, 0, sizeof(*q));
	q->ecc = (ecc >= 0 && ecc <= 3) ? ecc : UI_QR_ECC_M;
	q->fg = 0xFF000000u;
	q->bg = 0xFFFFFFFFu;
	q->logoRatio = UI_QR_LOGO_RATIO_DEFAULT;
	q->padding = UI_QR_PAD_DEFAULT;
	q->error = UI_QR_E_EMPTY;
}

static void qr_reencode(UiQrCode *q)
{
	unsigned char *m;
	int size = 0;
	int r;

	q->dirty = 0;
	q->rev++;
	if (q->matrix) {
		free(q->matrix);
		q->matrix = NULL;
	}
	q->size = 0;
	q->version = 0;
	if (!q->textLen) {
		q->error = UI_QR_E_EMPTY;
		return;
	}
	m = (unsigned char *)malloc(UI_QR_MATRIX_BYTES);
	if (!m) {
		q->error = UI_QR_E_OOM;
		return;
	}
	r = UiQrCode_Encode(q->text, q->ecc, m, UI_QR_MATRIX_BYTES, &size);
	if (r != UI_QR_OK) {
		free(m);
		q->error = r;
		return;
	}
	q->matrix = m;
	q->size = size;
	q->version = (size - 17) / 4;
	q->error = UI_QR_OK;
}

int UiQrCode_SetText(UiQrCode *q, const char *utf8)
{
	int len;
	char *copy;

	if (!q) return 0;
	if (!utf8) utf8 = "";
	len = (int)strlen(utf8);
	if (q->text && q->textLen == len &&
	    memcmp(q->text, utf8, (size_t)len) == 0)
		return q->error == UI_QR_OK;

	copy = (char *)malloc((size_t)len + 1);
	if (!copy) {
		q->error = UI_QR_E_OOM;
		return 0;
	}
	memcpy(copy, utf8, (size_t)len + 1);
	if (q->text) free(q->text);
	q->text = copy;
	q->textLen = len;
	qr_reencode(q);
	return q->error == UI_QR_OK;
}

void UiQrCode_SetEcc(UiQrCode *q, int ecc)
{
	if (!q || ecc < 0 || ecc > 3 || ecc == q->ecc) return;
	q->ecc = ecc;
	qr_reencode(q);
}

void UiQrCode_SetColors(UiQrCode *q, uint32_t fg, uint32_t bg)
{
	if (!q) return;
	if (fg == q->fg && bg == q->bg) return;
	q->fg = fg;
	q->bg = bg;
	q->rev++;
}

int UiQrCode_Error(const UiQrCode *q)
{
	return q ? q->error : UI_QR_E_PARAM;
}

static void qr_logo_free(UiQrCode *q)
{
	if (q->logoBmp) {
		if (q->logoGfx) Gfx_DestroyBitmap(q->logoGfx, q->logoBmp);
		q->logoBmp = NULL;
	}
	q->logoGfx = NULL;
	q->logoW = q->logoH = 0;
	q->logoGen = 0;
	q->logoState = 0;
}

void UiQrCode_SetLogoSrc(UiQrCode *q, const char *utf8Path)
{
	size_t len;
	if (!q) return;
	if (!utf8Path) utf8Path = "";
	len = strlen(utf8Path);
	if (len >= UI_QR_LOGO_MAX) len = UI_QR_LOGO_MAX - 1;
	if (q->logoSrc[0] && strlen(q->logoSrc) == len &&
	    memcmp(q->logoSrc, utf8Path, len) == 0)
		return;
	memcpy(q->logoSrc, utf8Path, len);
	q->logoSrc[len] = 0;
	qr_logo_free(q);
	q->logoSrcDirty = 1;
}

void UiQrCode_SetLogoRatio(UiQrCode *q, float ratio)
{
	if (!q) return;
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > UI_QR_LOGO_RATIO_MAX) ratio = UI_QR_LOGO_RATIO_MAX;
	q->logoRatio = ratio;
}

void UiQrCode_SetPadding(UiQrCode *q, float modules)
{
	if (!q) return;
	if (modules < 0.0f) modules = 0.0f;
	if (modules > UI_QR_PAD_MAX) modules = UI_QR_PAD_MAX;
	q->padding = modules;
}

int UiQrCode_Version(const UiQrCode *q)
{
	return q ? q->version : 0;
}

void UiQrCode_Shutdown(UiQrCode *q)
{
	if (!q) return;
	qr_logo_free(q);
	if (q->bmp) {
		if (q->gfx) Gfx_DestroyBitmap(q->gfx, q->bmp);
		q->bmp = NULL;
	}
	if (q->text) free(q->text);
	if (q->matrix) free(q->matrix);
	memset(q, 0, sizeof(*q));
}

/* ── Logo 解码 (最小 WIC 路径, 避免引入整个 UiImage) ────────────────── */

#define QR_REL_(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

static unsigned char *qr_read_file(const WCHAR *path, int *outLen)
{
	HANDLE h;
	LARGE_INTEGER sz;
	unsigned char *buf;
	DWORD got = 0;

	*outLen = 0;
	h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 ||
	    sz.QuadPart > (32 << 20)) {
		CloseHandle(h);
		return NULL;
	}
	buf = (unsigned char *)malloc((size_t)sz.QuadPart);
	if (!buf) { CloseHandle(h); return NULL; }
	if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) ||
	    got != (DWORD)sz.QuadPart) {
		free(buf);
		CloseHandle(h);
		return NULL;
	}
	CloseHandle(h);
	*outLen = (int)got;
	return buf;
}

static GfxBitmap *qr_logo_decode(Gfx *g, const char *utf8, int *ow, int *oh)
{
	WCHAR path[MAX_PATH];
	unsigned char *bytes;
	int len = 0;
	IWICImagingFactory *fac = NULL;
	IWICStream *stream = NULL;
	IWICBitmapDecoder *dec = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICBitmapScaler *scaler = NULL;
	IWICFormatConverter *conv = NULL;
	IWICBitmapSource *src;
	UINT w = 0, h = 0, tw, th;
	BYTE *pix = NULL;
	GfxBitmap *bmp = NULL;
	HRESULT hr;

	*ow = *oh = 0;
	if (!g || !utf8 || !utf8[0] || !Gfx_Ready(g)) return NULL;
	if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, path, MAX_PATH) <= 0)
		return NULL;
	bytes = qr_read_file(path, &len);
	if (!bytes) return NULL;

	hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
		CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&fac);
	if (FAILED(hr) || !fac) goto done;
	hr = fac->lpVtbl->CreateStream(fac, &stream);
	if (FAILED(hr) || !stream) goto done;
	hr = stream->lpVtbl->InitializeFromMemory(stream, bytes, (DWORD)len);
	if (FAILED(hr)) goto done;
	hr = fac->lpVtbl->CreateDecoderFromStream(fac, (IStream *)stream, NULL,
		WICDecodeMetadataCacheOnDemand, &dec);
	if (FAILED(hr) || !dec) goto done;
	hr = dec->lpVtbl->GetFrame(dec, 0, &frame);
	if (FAILED(hr) || !frame) goto done;
	hr = frame->lpVtbl->GetSize(frame, &w, &h);
	if (FAILED(hr) || w == 0 || h == 0) goto done;

	tw = w; th = h;
	if (w > UI_QR_LOGO_BITMAP_MAX || h > UI_QR_LOGO_BITMAP_MAX) {
		UINT m = w > h ? w : h;
		tw = (UINT)((uint64_t)w * UI_QR_LOGO_BITMAP_MAX / m);
		th = (UINT)((uint64_t)h * UI_QR_LOGO_BITMAP_MAX / m);
		if (tw == 0) tw = 1;
		if (th == 0) th = 1;
		hr = fac->lpVtbl->CreateBitmapScaler(fac, &scaler);
		if (FAILED(hr) || !scaler) goto done;
		hr = scaler->lpVtbl->Initialize(scaler, (IWICBitmapSource *)frame,
			tw, th, WICBitmapInterpolationModeFant);
		if (FAILED(hr)) goto done;
		src = (IWICBitmapSource *)scaler;
	} else {
		src = (IWICBitmapSource *)frame;
	}

	hr = fac->lpVtbl->CreateFormatConverter(fac, &conv);
	if (FAILED(hr) || !conv) goto done;
	hr = conv->lpVtbl->Initialize(conv, src, &GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
	if (FAILED(hr)) goto done;
	pix = (BYTE *)malloc((size_t)tw * th * 4);
	if (!pix) goto done;
	hr = conv->lpVtbl->CopyPixels(conv, NULL, tw * 4, tw * th * 4, pix);
	if (FAILED(hr)) goto done;
	bmp = Gfx_CreateBitmap(g, (int)tw, (int)th, pix);
	if (bmp) { *ow = (int)tw; *oh = (int)th; }

done:
	if (pix) free(pix);
	QR_REL_(conv);
	QR_REL_(scaler);
	QR_REL_(frame);
	QR_REL_(dec);
	QR_REL_(stream);
	QR_REL_(fac);
	free(bytes);
	return bmp;
}

static void qr_logo_ensure(UiQrCode *q, UiContext *ui)
{
	Gfx *g = ui->gfx;

	if (!q->logoSrc[0]) return;
	if (!g || !Gfx_Ready(g)) return;
	if (q->logoBmp && q->logoGen != Gfx_DeviceGeneration(g)) {
		Gfx_DestroyBitmap(q->logoGfx ? q->logoGfx : g, q->logoBmp);
		q->logoBmp = NULL;
		q->logoGfx = NULL;
		q->logoState = 0;
		q->logoSrcDirty = 1;
	}
	if (!q->logoSrcDirty && q->logoState != 0) return;
	q->logoSrcDirty = 0;
	q->logoBmp = qr_logo_decode(g, q->logoSrc, &q->logoW, &q->logoH);
	if (q->logoBmp) {
		q->logoGfx = g;
		q->logoGen = Gfx_DeviceGeneration(g);
		q->logoState = 1;
	} else {
		q->logoState = -1;
	}
}

/* side: 含静区的整体边长 (DIP); n: 含静区模块数. */
static void qr_draw_logo(UiQrCode *q, UiContext *ui, float x, float y,
	float side, int n)
{
	float scale, ls, fs;
	int dw, dh;
	UiRect dst;

	(void)n;
	if (!q->logoSrc[0] || q->logoRatio <= 0.0f) return;
	qr_logo_ensure(q, ui);
	if (q->logoState != 1 || !q->logoBmp || q->logoW <= 0 || q->logoH <= 0)
		return;

	scale = ui->scale > 0.0f ? ui->scale : 1.0f;
	ls = side * q->logoRatio;
	fs = ls * scale / (float)(q->logoW > q->logoH ? q->logoW : q->logoH);
	dw = (int)(q->logoW * fs + 0.5f);
	dh = (int)(q->logoH * fs + 0.5f);
	if (dw < 1 || dh < 1) return;
	dst.w = (float)dw / scale;
	dst.h = (float)dh / scale;
	dst.x = (float)(int)((x + (side - dst.w) * 0.5f) * scale + 0.5f) / scale;
	dst.y = (float)(int)((y + (side - dst.h) * 0.5f) * scale + 0.5f) / scale;
	Gfx_DrawBitmap(ui->gfx, q->logoBmp, dst);
}

static void qr_placeholder(UiContext *ui, UiRect rc, const char *msg)
{
	UiTheme *t = ui->theme;
	Gfx_FillRoundRect(ui->gfx, rc, 6.0f, t ? t->frame : 0xFFF2F2F2u);
	Gfx_StrokeRoundRect(ui->gfx, rc, 6.0f, t ? t->border : 0xFFCCCCCCu, 1.0f);
	if (t && rc.w >= 48.0f && rc.h >= 24.0f)
		Gfx_Text(ui->gfx, t->fSmall, msg, rc, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

/* 第 i 条模块边界在 px 像素纹理中的位置: 四舍五入, 保证纹理正好铺满. */
static int qr_span(int i, int px, int n)
{
	return (int)(((long long)i * px + n / 2) / n);
}

static int qr_build_bitmap(UiQrCode *q, UiContext *ui, int px, int n)
{
	uint32_t *buf;
	uint32_t fg;
	GfxBitmap *bmp;
	int x, y;

	if (!ui->gfx || px <= 0) return 0;
	buf = (uint32_t *)malloc((size_t)px * (size_t)px * sizeof(uint32_t));
	if (!buf) return 0;
	/* 模块之外的像素透明: 底色由 Draw 的 FillRect 只铺一次,
	 * 否则半透明底色会在码区被叠两遍. */
	memset(buf, 0, (size_t)px * (size_t)px * sizeof(uint32_t));
	fg = qr_premult(q->fg);
	for (y = 0; y < q->size; y++) {
		int y0 = qr_span(y, px, n);
		int y1 = qr_span(y + 1, px, n);
		x = 0;
		while (x < q->size) {
			int x0, x1, xx, yy;
			if (!UiQrCode_Bit(q->matrix, q->size, x, y)) { x++; continue; }
			x0 = x;
			while (x < q->size && UiQrCode_Bit(q->matrix, q->size, x, y)) x++;
			x1 = x;
			x0 = qr_span(x0, px, n);
			x1 = qr_span(x1, px, n);
			for (yy = y0; yy < y1; yy++) {
				uint32_t *row = buf + (size_t)yy * px;
				for (xx = x0; xx < x1; xx++) row[xx] = fg;
			}
		}
	}
	bmp = Gfx_CreateBitmap(ui->gfx, px, px, (const uint8_t *)buf);
	free(buf);
	if (!bmp) return 0;
	if (q->bmp) Gfx_DestroyBitmap(q->gfx ? q->gfx : ui->gfx, q->bmp);
	q->gfx = ui->gfx;
	q->bmp = bmp;
	return 1;
}

/* 纹理不可用时的回退: 设备像素对齐的逐行游程矩形. */
static void qr_draw_direct(UiQrCode *q, UiContext *ui, UiRect rc, int ppm,
	float scale)
{
	Gfx *g = ui->gfx;
	int n = q->size;
	int baseX, baseY, x, y, clip = 0;
	float total;

	if (ppm < 1) ppm = 1;
	total = (float)(n * ppm) / scale;
	baseX = (int)((rc.x + (rc.w - total) * 0.5f) * scale + 0.5f);
	baseY = (int)((rc.y + (rc.h - total) * 0.5f) * scale + 0.5f);
	/* rc 已由 Draw 铺过一次底色, 半透明 bg 不能重复叠. */
	if (total > rc.w + 0.01f || total > rc.h + 0.01f) {
		Gfx_PushClip(g, rc);
		clip = 1;
	}
	for (y = 0; y < q->size; y++) {
		x = 0;
		while (x < q->size) {
			int x0;
			if (!UiQrCode_Bit(q->matrix, q->size, x, y)) { x++; continue; }
			x0 = x;
			while (x < q->size && UiQrCode_Bit(q->matrix, q->size, x, y)) x++;
			Gfx_FillRect(g, Ui_Rect(
				(float)(baseX + x0 * ppm) / scale,
				(float)(baseY + y * ppm) / scale,
				(float)((x - x0) * ppm) / scale, (float)ppm / scale),
				q->fg);
		}
	}
	if (clip) Gfx_PopClip(g);
	qr_draw_logo(q, ui, (float)baseX / scale, (float)baseY / scale, total, n);
}

void UiQrCode_Draw(UiQrCode *q, UiContext *ui, UiRect rc)
{
	float scale, side, x, y;
	int n, px, avail, box, gen;

	if (!q || !ui) return;
	if (q->dirty) qr_reencode(q);
	if (q->error != UI_QR_OK || !q->matrix || q->size <= 0) {
		qr_placeholder(ui, rc,
			q->error == UI_QR_E_LONG ? "内容超长" : "二维码");
		return;
	}
	if (!ui->gfx || rc.w <= 0.0f || rc.h <= 0.0f) return;

	scale = ui->scale > 0.0f ? ui->scale : 1.0f;
	n = q->size;
	avail = (int)((rc.w < rc.h ? rc.w : rc.h) * scale);
	/* 背景色铺满组件; padding 只决定码区到四边的距离 (模块). */
	Gfx_FillRect(ui->gfx, rc, q->bg);
	/* box + 2*pad*module = avail, module = box/n. */
	box = (int)((double)avail * n / ((double)n + 2.0 * q->padding));
	if (box < n) {
		qr_draw_direct(q, ui, rc, 1, scale);
		return;
	}
	px = box > UI_QR_BITMAP_MAX ? UI_QR_BITMAP_MAX : box;

	gen = Gfx_DeviceGeneration(ui->gfx);
	if (!q->bmp || q->bmpPx != px || q->bmpN != n ||
	    q->bmpRev != q->rev || q->bmpGen != gen) {
		if (!qr_build_bitmap(q, ui, px, n)) {
			qr_draw_direct(q, ui, rc, box / n, scale);
			return;
		}
		q->bmpPx = px;
		q->bmpN = n;
		q->bmpRev = q->rev;
		q->bmpGen = gen;
	}

	side = (float)box / scale;
	x = rc.x + (rc.w - side) * 0.5f;
	y = rc.y + (rc.h - side) * 0.5f;
	x = (float)(int)(x * scale + 0.5f) / scale;
	y = (float)(int)(y * scale + 0.5f) / scale;
	Gfx_DrawBitmap(ui->gfx, q->bmp, Ui_Rect(x, y, side, side));
	qr_draw_logo(q, ui, x, y, side, n);
}