/*
 * qrcode_test.c - 二维码编码器结构自检 (无窗口)
 *
 * 用独立实现的读取路径反向验证编码结果:
 *   功能图案 -> 交织数据提取 (按格式信息解掩码) -> 反交织 ->
 *   每块 RS 校验子归零 -> 位流解析还原原始字节.
 * 覆盖: 各纠错等级的容量边界, 版本自动选择, UTF-8 内容, 组件生命周期.
 * 构建: bin\cui_qrcode_test.exe; 退出码 0 = 全通过.
 */
#include <windows.h>
#include <stdint.h>
#include "ui/ui_qrcode.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);
void *memset(void *d, int c, size_t n);

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

/* ── 规格表 (独立副本) ──────────────────────────────────────────────── */

static const unsigned char kEccN[4][41] = {
	{ 0, 7,10,15,20,26,18,20,24,30,18,20,24,26,30,22,24,28,30,28,
	  28,28,28,30,30,26,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30 },
	{ 0,10,16,26,18,24,16,18,22,22,26,30,22,22,24,24,28,28,26,26,
	  26,26,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28 },
	{ 0,13,22,18,26,18,24,18,22,20,24,28,26,24,20,30,24,28,28,26,
	  30,28,30,30,30,30,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30 },
	{ 0,17,28,22,16,22,28,26,26,24,28,24,28,22,24,24,30,28,28,26,
	  28,30,24,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30 }
};

static const unsigned char kBlkN[4][41] = {
	{ 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7,
	  8, 8, 9, 9,10,12,12,12,13,14,15,16,17,18,19,19,20,21,22,24,25 },
	{ 0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9,10,10,11,13,14,
	 16,17,17,18,20,21,23,25,26,28,29,31,33,35,37,38,40,43,45,47,49 },
	{ 0, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8,10,12,16,12,17,16,18,21,
	 20,23,23,25,27,29,34,34,35,38,40,43,45,48,51,53,56,59,62,65,68 },
	{ 0, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8,11,11,16,16,18,16,19,21,25,
	 25,25,34,30,32,35,37,40,42,45,48,51,54,57,60,63,66,70,74,77,81 }
};

static int raw_modules(int ver)
{
	int r = (16 * ver + 128) * ver + 64;
	if (ver >= 2) {
		int na = ver / 7 + 2;
		r -= (25 * na - 10) * na - 55;
		if (ver >= 7) r -= 36;
	}
	return r;
}

static int data_cw(int ver, int ecc)
{
	return raw_modules(ver) / 8 - kEccN[ecc][ver] * kBlkN[ecc][ver];
}

static int cap_bytes(int ver, int ecc)
{
	int cb = ver <= 9 ? 8 : 16;
	int c = (data_cw(ver, ecc) * 8 - 4 - cb) / 8;
	return c > 0 ? c : 0;
}

/* ── GF(256) 独立实现 (俄罗斯农民乘法) ────────────────────────────── */

static unsigned char t_exp[512], t_log[256];

static void t_gf_init(void)
{
	int i, x = 1;
	for (i = 0; i < 255; i++) {
		t_exp[i] = (unsigned char)x;
		t_log[x] = (unsigned char)i;
		x <<= 1;
		if (x & 0x100) x ^= 0x11D;
	}
	for (i = 255; i < 512; i++) t_exp[i] = t_exp[i - 255];
	t_log[0] = 0;
}

static unsigned char t_mul(unsigned char a, unsigned char b)
{
	return (a && b) ? t_exp[(int)t_log[a] + (int)t_log[b]] : 0;
}

/* 码字多项式按 α^0..α^(ecLen-1) 求值必须全 0. */
static int rs_ok(const unsigned char *b, int n, int ecLen)
{
	int j, i;
	for (j = 0; j < ecLen; j++) {
		unsigned char acc = 0;
		for (i = 0; i < n; i++) acc = (unsigned char)(t_mul(acc, t_exp[j]) ^ b[i]);
		if (acc) return 0;
	}
	return 1;
}

/* ── 矩阵读取 ───────────────────────────────────────────────────────── */

static int mbit(const unsigned char *m, int size, int x, int y)
{
	return UiQrCode_Bit(m, size, x, y);
}

static int t_mask_on(int mask, int x, int y)
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

static int align_positions(int ver, int *pos)
{
	int num, step, i, p;
	if (ver == 1) return 0;
	num = ver / 7 + 2;
	step = ver == 32 ? 26 : (ver * 4 + num * 2 + 1) / (num * 2 - 2) * 2;
	pos[0] = 6;
	pos[num - 1] = ver * 4 + 10;
	for (i = num - 2, p = pos[num - 1] - step; i >= 1; i--, p -= step)
		pos[i] = p;
	return num;
}

static void set_f(unsigned char *f, int size, int x, int y)
{
	if (x >= 0 && y >= 0 && x < size && y < size) f[y * size + x] = 1;
}

static void build_func(int ver, int size, unsigned char *f)
{
	int pos[8], num, i, j, dx, dy;
	memset(f, 0, (size_t)size * (size_t)size);
	for (i = 0; i < 3; i++) {
		int cx = i == 1 ? size - 4 : 3;
		int cy = i == 2 ? size - 4 : 3;
		for (dy = -4; dy <= 4; dy++)
			for (dx = -4; dx <= 4; dx++)
				set_f(f, size, cx + dx, cy + dy);
	}
	for (i = 0; i < size; i++) {
		set_f(f, size, 6, i);
		set_f(f, size, i, 6);
	}
	num = align_positions(ver, pos);
	for (i = 0; i < num; i++)
		for (j = 0; j < num; j++) {
			if ((i == 0 && j == 0) || (i == 0 && j == num - 1) ||
			    (i == num - 1 && j == 0))
				continue;
			for (dy = -2; dy <= 2; dy++)
				for (dx = -2; dx <= 2; dx++)
					set_f(f, size, pos[i] + dx, pos[j] + dy);
		}
	/* 格式信息 + 固定暗模块 */
	for (i = 0; i <= 5; i++) set_f(f, size, 8, i);
	set_f(f, size, 8, 7);
	set_f(f, size, 8, 8);
	set_f(f, size, 7, 8);
	for (i = 9; i < 15; i++) set_f(f, size, 14 - i, 8);
	for (i = 0; i < 8; i++) set_f(f, size, size - 1 - i, 8);
	for (i = 8; i < 15; i++) set_f(f, size, 8, size - 15 + i);
	set_f(f, size, 8, size - 8);
	/* 版本信息 */
	if (ver >= 7)
		for (i = 0; i < 18; i++) {
			int a = size - 11 + i % 3, b = i / 3;
			set_f(f, size, a, b);
			set_f(f, size, b, a);
		}
}

static int fmt_read(const unsigned char *m, int size)
{
	int v = 0, i;
	for (i = 0; i <= 5; i++) v |= mbit(m, size, 8, i) << i;
	v |= mbit(m, size, 8, 7) << 6;
	v |= mbit(m, size, 8, 8) << 7;
	v |= mbit(m, size, 7, 8) << 8;
	for (i = 9; i < 15; i++) v |= mbit(m, size, 14 - i, 8) << i;
	return v;
}

static int fmt_read2(const unsigned char *m, int size)
{
	int v = 0, i;
	for (i = 0; i < 8; i++) v |= mbit(m, size, size - 1 - i, 8) << i;
	for (i = 8; i < 15; i++) v |= mbit(m, size, 8, size - 15 + i) << i;
	return v;
}

/* 反向验证一次编码; 返回 1 = 结构/纠错/内容全部正确. */
static int verify(const char *text, int ecc, const unsigned char *mat, int size)
{
	static unsigned char f[UI_QR_MODULES_MAX * UI_QR_MODULES_MAX];
	static unsigned char cw[UI_QR_MATRIX_BYTES];
	static unsigned char dbuf[UI_QR_MATRIX_BYTES];
	static unsigned char blk[81][200];
	int ver = (size - 17) / 4;
	int len = (int)strlen(text);
	int nb, ecLen, raw, shortFull, nshort, datShort;
	int dlen[81], i, j, x, y, right, vert, idx, bits, fl, f2, mask;
	int dataBytes, pos, ok = 1;

	if (size < 21 || size > UI_QR_MODULES_MAX || (size - 17) % 4) return 0;

	/* 三个定位图案 (含分隔区) 与固定暗模块 */
	for (i = 0; i < 3; i++) {
		int cx = i == 1 ? size - 4 : 3;
		int cy = i == 2 ? size - 4 : 3;
		for (y = -4; y <= 4 && ok; y++)
			for (x = -4; x <= 4 && ok; x++) {
				int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
				int d = ax > ay ? ax : ay;
				int want, px = cx + x, py = cy + y;
				if (px < 0 || py < 0 || px >= size || py >= size) continue;
				want = (d == 4 || d == 2) ? 0 : 1;
				if (mbit(mat, size, px, py) != want) ok = 0;
			}
	}
	if (mbit(mat, size, 8, size - 8) != 1) ok = 0;
	for (i = 8; i < size - 8 && ok; i++) {
		if (mbit(mat, size, 6, i) != ((i & 1) == 0)) ok = 0;
		if (mbit(mat, size, i, 6) != ((i & 1) == 0)) ok = 0;
	}

	/* 格式信息: 两份一致, BCH 余数 0 */
	fl = fmt_read(mat, size);
	f2 = fmt_read2(mat, size);
	if (fl != f2) ok = 0;
	{
		int rem = fl ^ 0x5412;
		for (i = 14; i >= 10; i--)
			if ((rem >> i) & 1) rem ^= 0x537 << (i - 10);
		if (rem & 0x3FF) ok = 0;
		rem = (fl ^ 0x5412) >> 10;
		if (((rem >> 3) & 3) != (ecc == UI_QR_ECC_L ? 1 :
			ecc == UI_QR_ECC_M ? 0 : ecc == UI_QR_ECC_Q ? 3 : 2)) ok = 0;
		mask = rem & 7;
	}

	/* 版本信息 */
	if (ver >= 7) {
		int vb = 0, va = 0;
		for (i = 0; i < 18; i++) {
			int a = size - 11 + i % 3, b = i / 3;
			vb |= mbit(mat, size, a, b) << i;
			va |= mbit(mat, size, b, a) << i;
		}
		if (va != vb || (vb >> 12) != ver) ok = 0;
		{
			int rem = vb;
			for (i = 17; i >= 12; i--)
				if ((rem >> i) & 1) rem ^= 0x1F25 << (i - 12);
			if (rem & 0xFFF) ok = 0;
		}
	}

	/* 数据提取 (按格式信息给出的掩码) */
	build_func(ver, size, f);
	memset(cw, 0, (size_t)raw_modules(ver) / 8 + 1);
	raw = raw_modules(ver) / 8;
	bits = raw * 8;
	idx = 0;
	for (right = size - 1; right >= 1; right -= 2) {
		if (right == 6) right = 5;
		for (vert = 0; vert < size; vert++) {
			for (j = 0; j < 2; j++) {
				x = right - j;
				y = ((right + 1) & 2) == 0 ? size - 1 - vert : vert;
				if (f[y * size + x]) continue;
				if (idx < bits) {
					int bit = mbit(mat, size, x, y) ^ t_mask_on(mask, x, y);
					if (bit) cw[idx >> 3] |= (unsigned char)(0x80 >> (idx & 7));
				}
				idx++;
			}
		}
	}
	if (idx < bits || idx - bits > 7) ok = 0;

	/* 反交织 + 每块 RS 校验 */
	nb = kBlkN[ecc][ver];
	ecLen = kEccN[ecc][ver];
	shortFull = raw / nb;
	nshort = nb - raw % nb;
	datShort = shortFull - ecLen;
	for (i = 0; i < nb; i++) dlen[i] = datShort + (i >= nshort ? 1 : 0);
	pos = 0;
	for (i = 0; i <= datShort; i++)
		for (j = 0; j < nb; j++)
			if (i < dlen[j]) blk[j][i] = cw[pos++];
	for (i = 0; i < ecLen; i++)
		for (j = 0; j < nb; j++) blk[j][dlen[j] + i] = cw[pos++];
	if (pos != raw) ok = 0;
	for (j = 0; j < nb; j++)
		if (!rs_ok(blk[j], dlen[j] + ecLen, ecLen)) ok = 0;

	/* 位流: 模式 4 / 计数 8|16 / UTF-8 字节 */
	dataBytes = data_cw(ver, ecc);
	pos = 0;
	for (j = 0; j < nb; j++)
		for (i = 0; i < dlen[j]; i++) dbuf[pos++] = blk[j][i];
	if (pos != dataBytes) ok = 0;
	{
		int bp = 0, cb = ver <= 9 ? 8 : 16, got = 0, gotlen = 0, k;
		for (i = 0; i < 4; i++, bp++)
			got = (got << 1) | ((dbuf[bp >> 3] >> (7 - (bp & 7))) & 1);
		if (got != 4) ok = 0;
		for (i = 0; i < cb; i++, bp++)
			gotlen = (gotlen << 1) | ((dbuf[bp >> 3] >> (7 - (bp & 7))) & 1);
		if (gotlen != len) ok = 0;
		for (k = 0; k < len && (bp >> 3) < dataBytes; k++) {
			int c = 0;
			for (i = 0; i < 8; i++, bp++)
				c = (c << 1) | ((dbuf[bp >> 3] >> (7 - (bp & 7))) & 1);
			if (c != (unsigned char)text[k]) ok = 0;
		}
	}
	return ok;
}

/* ── 测试 ───────────────────────────────────────────────────────────── */

static unsigned char g_mat[UI_QR_MATRIX_BYTES];
static char g_text[UI_QR_TEXT_MAX + 2];

static void fill_text(int n)
{
	int i;
	for (i = 0; i < n; i++) g_text[i] = (char)('A' + i % 26);
	g_text[n] = 0;
}

void __stdcall startup(void)
{
	int ecc, ver, len, size, r, rev0;
	UiQrCode q;

	t_gf_init();

	/* 参数/边界 */
	CHECK(UiQrCode_Encode(NULL, UI_QR_ECC_M, g_mat, sizeof(g_mat), &size) == UI_QR_E_PARAM);
	CHECK(UiQrCode_Encode("", UI_QR_ECC_M, g_mat, sizeof(g_mat), &size) == UI_QR_E_EMPTY);
	CHECK(UiQrCode_Encode("x", 9, g_mat, sizeof(g_mat), &size) == UI_QR_E_PARAM);
	CHECK(UiQrCode_Encode("hello", UI_QR_ECC_M, g_mat, 4, &size) == UI_QR_E_PARAM);
	fill_text(UI_QR_TEXT_MAX + 1);
	CHECK(UiQrCode_Encode(g_text, UI_QR_ECC_L, g_mat, sizeof(g_mat), &size) == UI_QR_E_LONG);
	fill_text(UI_QR_TEXT_MAX);
	CHECK(UiQrCode_Encode(g_text, UI_QR_ECC_L, g_mat, sizeof(g_mat), &size) == UI_QR_OK);
	CHECK(size == 177);
	CHECK(verify(g_text, UI_QR_ECC_L, g_mat, size));
	CHECK(UiQrCode_Encode(g_text, UI_QR_ECC_M, g_mat, sizeof(g_mat), &size) == UI_QR_E_LONG);

	/* 标准容量: v1 边界值 */
	CHECK(cap_bytes(1, UI_QR_ECC_L) == 17);
	CHECK(cap_bytes(1, UI_QR_ECC_M) == 14);
	CHECK(cap_bytes(1, UI_QR_ECC_Q) == 11);
	CHECK(cap_bytes(1, UI_QR_ECC_H) == 7);
	CHECK(cap_bytes(40, UI_QR_ECC_L) == 2953);
	CHECK(cap_bytes(40, UI_QR_ECC_M) == 2331);
	CHECK(cap_bytes(40, UI_QR_ECC_Q) == 1663);
	CHECK(cap_bytes(40, UI_QR_ECC_H) == 1273);

	/* 全部版本/纠错等级: 恰好一个版本的容量 -> 选中该版本且可解 */
	for (ecc = 0; ecc < 4; ecc++)
		for (ver = 1; ver <= 40; ver++) {
			len = cap_bytes(ver, ecc);
			fill_text(len);
			r = UiQrCode_Encode(g_text, ecc, g_mat, sizeof(g_mat), &size);
			if (r != UI_QR_OK || size != ver * 4 + 17) {
				CHECK(!"capacity boundary encode");
				continue;
			}
			if (!verify(g_text, ecc, g_mat, size)) {
				CHECK(!"decode mismatch");
				continue;
			}
			g_pass++;
			/* 多一字节必须落到更高版本 (v40 无更高版本) */
			if (len < UI_QR_TEXT_MAX) {
				fill_text(len + 1);
				r = UiQrCode_Encode(g_text, ecc, g_mat, sizeof(g_mat), &size);
				if (ver < 40) {
					CHECK(r == UI_QR_OK && size > ver * 4 + 17);
				} else {
					CHECK(r == UI_QR_E_LONG);
				}
			}
		}

	/* UTF-8 多字节内容 */
	{
		const char *u8 = "二维码 QR\xE6\xB5\x8B\xE8\xAF\x95";
		CHECK(UiQrCode_Encode(u8, UI_QR_ECC_Q, g_mat, sizeof(g_mat), &size) == UI_QR_OK);
		CHECK(verify(u8, UI_QR_ECC_Q, g_mat, size));
	}

	/* 组件生命周期 */
	memset(&q, 0, sizeof(q));
	UiQrCode_Init(&q, UI_QR_ECC_M);
	CHECK(UiQrCode_Error(&q) == UI_QR_E_EMPTY);
	CHECK(UiQrCode_Version(&q) == 0);
	CHECK(UiQrCode_SetText(&q, NULL) == 0);
	CHECK(UiQrCode_Error(&q) == UI_QR_E_EMPTY);
	CHECK(UiQrCode_SetText(&q, "hello") == 1);
	CHECK(UiQrCode_Error(&q) == UI_QR_OK);
	CHECK(UiQrCode_Version(&q) == 1);
	rev0 = q.rev;
	CHECK(UiQrCode_SetText(&q, "hello") == 1);
	CHECK(q.rev == rev0);                 /* 内容未变不重编码 */
	CHECK(UiQrCode_SetText(&q, "hello2") == 1);
	CHECK(q.rev == rev0 + 1);
	UiQrCode_SetEcc(&q, UI_QR_ECC_H);
	CHECK(q.rev == rev0 + 2);
	fill_text(10);
	CHECK(UiQrCode_SetText(&q, g_text) == 1);
	UiQrCode_SetEcc(&q, UI_QR_ECC_H);
	CHECK(UiQrCode_Error(&q) == UI_QR_OK);
	CHECK(UiQrCode_Version(&q) == 2);     /* H: v1=7, v2=14 */
	rev0 = q.rev;
	UiQrCode_SetEcc(&q, UI_QR_ECC_H);
	CHECK(q.rev == rev0);                 /* 同等级不变 */
	UiQrCode_SetColors(&q, 0x80FF0000u, 0x00FFFFFFu);
	CHECK(q.fg == 0x80FF0000u && q.bg == 0x00FFFFFFu);
	CHECK(q.rev == rev0 + 1);
	UiQrCode_SetColors(&q, 0x80FF0000u, 0x00FFFFFFu);
	CHECK(q.rev == rev0 + 1);             /* 同色不变 */
	UiQrCode_SetColors(&q, 0x40FF0000u, 0x00FFFFFFu);
	CHECK(q.fg == 0x40FF0000u);           /* 仅 alpha 变化也刷新纹理 */
	CHECK(q.rev == rev0 + 2);

	/* Logo: 默认比例 / 路径复制 / 比例钳制 / 关闭 */
	CHECK(q.logoSrc[0] == 0);
	CHECK(q.logoRatio == UI_QR_LOGO_RATIO_DEFAULT);
	rev0 = q.rev;
	UiQrCode_SetLogoSrc(&q, "C:\\logo.png");
	{
		const char *lp = "C:\\logo.png";
		int i = 0;
		while (lp[i] && q.logoSrc[i] == lp[i]) i++;
		CHECK(lp[i] == 0 && q.logoSrc[i] == 0);
	}
	CHECK(q.logoSrcDirty == 1);
	UiQrCode_SetLogoRatio(&q, 9.0f);
	CHECK(q.logoRatio == UI_QR_LOGO_RATIO_MAX);
	UiQrCode_SetLogoRatio(&q, -1.0f);
	CHECK(q.logoRatio == 0.0f);
	UiQrCode_SetLogoRatio(&q, 0.22f);
	CHECK(q.logoRatio == 0.22f);
	CHECK(q.rev == rev0);                 /* Logo 不触发码区重编码 */
	UiQrCode_SetLogoSrc(&q, NULL);
	CHECK(q.logoSrc[0] == 0);

	/* 留白: 默认 1 模块 / 钳制 / 不改码区 */
	CHECK(q.padding == UI_QR_PAD_DEFAULT);
	rev0 = q.rev;
	UiQrCode_SetPadding(&q, 3.0f);
	CHECK(q.padding == 3.0f);
	UiQrCode_SetPadding(&q, -1.0f);
	CHECK(q.padding == 0.0f);
	UiQrCode_SetPadding(&q, 99.0f);
	CHECK(q.padding == UI_QR_PAD_MAX);
	CHECK(q.rev == rev0);

	fill_text(UI_QR_TEXT_MAX);
	CHECK(UiQrCode_SetText(&q, g_text) == 0);   /* H 等级装不下 2953 字节 */
	CHECK(UiQrCode_Error(&q) == UI_QR_E_LONG);
	UiQrCode_Shutdown(&q);
	CHECK(q.text == NULL && q.matrix == NULL && q.size == 0 && q.bmp == NULL);
	CHECK(q.logoSrc[0] == 0 && q.logoBmp == NULL);

	{
		char line[128];
		sprintf(line, "qrcode_test: %d passed, %d failed\n", g_pass, g_fail);
		out_(line);
	}
	ExitProcess(g_fail ? 1u : 0u);
}