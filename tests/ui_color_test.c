/*
 * ui_color_test.c - ColorSelect 纯函数断言 (headless)
 *
 * 覆盖: HSV<->RGB 已知值、往返误差、SetColor 灰/黑保留色相、
 * SetHSVA 夹取、GetColor alpha 透传. 退出码 0 = 全通过.
 */
#include <windows.h>
#include "ui/ui_color_select.h"

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

static int near_(float a, float b, float eps)
{
	float d = a - b;
	return d <= eps && d >= -eps;
}

static int rgb_near_(UiColor a, UiColor b)
{
	int i;
	for (i = 0; i < 4; i++) {
		int va = (int)((a >> (i * 8)) & 0xFF);
		int vb = (int)((b >> (i * 8)) & 0xFF);
		int d = va - vb;
		if (d < -1 || d > 1) return 0;
	}
	return 1;
}

static void test_known(void)
{
	CHECK(UiColor_HsvToRgb(0.0f, 1.0f, 1.0f, 1.0f) == 0xFFFF0000u);
	CHECK(UiColor_HsvToRgb(120.0f, 1.0f, 1.0f, 1.0f) == 0xFF00FF00u);
	CHECK(UiColor_HsvToRgb(240.0f, 1.0f, 1.0f, 1.0f) == 0xFF0000FFu);
	CHECK(UiColor_HsvToRgb(0.0f, 0.0f, 1.0f, 1.0f) == 0xFFFFFFFFu);
	CHECK(UiColor_HsvToRgb(0.0f, 1.0f, 0.0f, 1.0f) == 0xFF000000u);
	CHECK(UiColor_HsvToRgb(45.0f, 0.0f, 1.0f, 0.5f) == 0x80FFFFFFu);
	{
		float h = -1.0f, s = -1.0f, v = -1.0f, a = -1.0f;
		UiColor_RgbToHsv(0xFF808080u, &h, &s, &v, &a);
		CHECK(near_(h, 0.0f, 0.01f) && near_(s, 0.0f, 0.001f));
		CHECK(near_(v, 0.50196f, 0.002f) && near_(a, 1.0f, 0.001f));
	}
}

static void test_roundtrip(void)
{
	float hs[4] = { 0.0f, 37.0f, 120.0f, 250.0f };
	float ss[3] = { 0.25f, 0.6f, 1.0f };
	float vs[3] = { 0.35f, 0.7f, 1.0f };
	int i, j, k;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 3; j++) {
			for (k = 0; k < 3; k++) {
				float h = hs[i], s = ss[j], v = vs[k];
				float h2, s2, v2, a2;
				UiColor c = UiColor_HsvToRgb(h, s, v, 1.0f);
				UiColor_RgbToHsv(c, &h2, &s2, &v2, &a2);
				CHECK(near_(s2, s, 0.01f) && near_(v2, v, 0.01f));
				CHECK(near_(a2, 1.0f, 0.004f));
				if (s > 0.01f && v > 0.01f) {
					float dh = h2 - h;
					if (dh > 180.0f) dh -= 360.0f;
					if (dh < -180.0f) dh += 360.0f;
					/* s=1 的边界色允许 1/255 量化对应的小角度误差 */
					CHECK(dh <= 1.5f && dh >= -1.5f);
				}
				CHECK(rgb_near_(c, UiColor_HsvToRgb(h2, s2, v2, a2)));
			}
		}
	}
}

static void test_hue_preserve(void)
{
	UiColorSelect cs;
	float h, s, v, a;

	UiColorSelect_Init(&cs, 42, 0xFF1E90FFu);   /* dodger blue, h≈210 */
	h = cs.h;
	CHECK(h > 200.0f && h < 220.0f);

	UiColorSelect_SetColor(&cs, 0xFF808080u);   /* 灰: 无色调 */
	UiColorSelect_GetHSVA(&cs, &h, &s, &v, &a);
	CHECK(near_(s, 0.0f, 0.005f) && near_(v, 0.50196f, 0.002f));
	CHECK(h > 200.0f && h < 220.0f);            /* 色相未被清零 */

	UiColorSelect_SetColor(&cs, 0xFF000000u);   /* 全黑: 同样保留 */
	UiColorSelect_GetHSVA(&cs, &h, &s, &v, &a);
	CHECK(near_(v, 0.0f, 0.001f) && h > 200.0f && h < 220.0f);
}

static void test_clamp_and_alpha(void)
{
	UiColorSelect cs;
	float h, s, v, a;
	UiColor c;

	UiColorSelect_Init(&cs, 7, 0xFF123456u);
	UiColorSelect_SetHSVA(&cs, 740.0f, 2.0f, -1.0f, -0.5f);
	UiColorSelect_GetHSVA(&cs, &h, &s, &v, &a);
	CHECK(near_(h, 20.0f, 0.01f));
	CHECK(near_(s, 1.0f, 0.001f) && near_(v, 0.0f, 0.001f));
	CHECK(near_(a, 0.0f, 0.001f));

	UiColorSelect_SetHSVA(&cs, 30.0f, 1.0f, 1.0f, 0.5f);
	c = UiColorSelect_GetColor(&cs);
	CHECK(((c >> 24) & 0xFF) == 128 || ((c >> 24) & 0xFF) == 127);
	CHECK(UiColorSelect_GetColor(&cs) == UiColor_HsvToRgb(30.0f, 1.0f,
		1.0f, 0.5f));
}

void __stdcall startup(void)
{
	char line[128];
	test_known();
	test_roundtrip();
	test_hue_preserve();
	test_clamp_and_alpha();
	sprintf(line, "ui_color_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
