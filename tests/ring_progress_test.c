/*
 * ring_progress_test.c - UiRingProgress 无窗口自检
 * 覆盖范围/钳制、CPU 补间、文案覆盖、格式回调、样式与生命周期。
 */
#include <windows.h>
#include <stdint.h>
#include "ui/ui_ring_progress.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
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

static const char *fmt_mbps(char *buf, size_t size, double value, void *user)
{
	(void)user;
	sprintf(buf, "%.1f", value);
	return buf;
}

static double nonfinite_nan(void)
{
	volatile double z = 0.0;
	return z / z;
}

static double nonfinite_inf(void)
{
	volatile double one = 1.0;
	volatile double z = 0.0;
	return one / z;
}

static int utf8_valid(const char *s)
{
	while (s && *s) {
		unsigned char c = (unsigned char)*s++;
		int n;
		if (c < 0x80u) continue;
		if ((c & 0xE0u) == 0xC0u) n = 1;
		else if ((c & 0xF0u) == 0xE0u) n = 2;
		else if ((c & 0xF8u) == 0xF0u) n = 3;
		else return 0;
		while (n-- > 0) {
			if (((unsigned char)*s & 0xC0u) != 0x80u) return 0;
			s++;
		}
	}
	return 1;
}

void __stdcall startup(void)
{
	static UiContext ui;
	static UiRingProgress p;
	float v;

	memset(&ui, 0, sizeof(ui));
	UiRingProgress_Init(&p, &ui);
	CHECK(p.minValue == 0.0 && p.maxValue == 100.0);
	CHECK(p.value == 0.0 && p.ringWidth == 14.0f);
	CHECK(p.durationMs == Ui_AnimFxMs(UI_AN_PROGRESS));
	CHECK(UiRingProgress_Value(&p) == 0.0);

	UiRingProgress_SetValue(&p, 140.0);
	CHECK(p.value == 100.0);
	UiRingProgress_SetValue(&p, -20.0);
	CHECK(p.value == 0.0);
	UiRingProgress_SetRange(&p, 10.0, 10.0);
	CHECK(p.minValue == 0.0 && p.maxValue == 100.0);

	UiRingProgress_SetDurationMs(&p, 0);
	Ui_AnimSet(&ui, p.id, UI_AN_USER, 0.0f);
	UiRingProgress_SetDurationMs(&p, 300);
	UiRingProgress_SetValue(&p, 50.0);
	CHECK(UiRingProgress_Animating(&p));
	Ui_AnimAdvance(&ui, 100.0f);
	Ui_AnimAdvance(&ui, 50.0f);
	v = Ui_AnimGet(&ui, p.id, UI_AN_USER, 0.0f);
	CHECK(v > 0.437f && v < 0.438f);
	Ui_AnimAdvance(&ui, 100.0f);
	Ui_AnimAdvance(&ui, 100.0f);
	CHECK(Ui_AnimGet(&ui, p.id, UI_AN_USER, 0.0f) == 0.5f);
	CHECK(!UiRingProgress_Animating(&p));

	UiRingProgress_SetRange(&p, 0.0, 200.0);
	CHECK(p.value == 50.0);
	Ui_AnimAdvance(&ui, 100.0f);
	Ui_AnimAdvance(&ui, 100.0f);
	Ui_AnimAdvance(&ui, 100.0f);
	CHECK(Ui_AnimGet(&ui, p.id, UI_AN_USER, 0.0f) == 0.25f);

	UiRingProgress_SetDurationMs(&p, 0);
	UiRingProgress_SetValue(&p, 75.0);
	CHECK(Ui_AnimGet(&ui, p.id, UI_AN_USER, 0.0f) == 0.375f);
	CHECK(!UiRingProgress_Animating(&p));
	UiRingProgress_SetDurationMs(&p, -10);
	CHECK(p.durationMs == 0);
	UiRingProgress_SetDurationMs(&p, 999999);
	CHECK(p.durationMs == 60000);

	UiRingProgress_SetDurationMs(&p, 400);
	UiRingProgress_SetValue(&p, 20.0);
	CHECK(UiRingProgress_Animating(&p));
	UiRingProgress_SetDurationMs(&p, 0);
	CHECK(!UiRingProgress_Animating(&p));
	CHECK(Ui_AnimGet(&ui, p.id, UI_AN_USER, -1.0f) == 0.1f);

	UiRingProgress_SetRange(&p, nonfinite_nan(), 100.0);
	CHECK(p.minValue == 0.0 && p.maxValue == 200.0);
	UiRingProgress_SetRange(&p, 0.0, nonfinite_inf());
	CHECK(p.minValue == 0.0 && p.maxValue == 200.0);
	UiRingProgress_SetRange(&p, 100.0, 0.0);
	CHECK(p.maxValue == 200.0);
	UiRingProgress_SetValue(&p, nonfinite_nan());
	CHECK(p.value == 20.0);
	UiRingProgress_SetValue(&p, nonfinite_inf());
	CHECK(p.value == 20.0);

	UiRingProgress_SetUnit(&p, "Mbps");
	CHECK(strcmp(p.unit, "Mbps") == 0);
	UiRingProgress_SetTitle(&p, "实时吞吐");
	CHECK(strcmp(p.title, "实时吞吐") == 0);
	UiRingProgress_SetValueText(&p, "在线");
	CHECK(strcmp(p.valueText, "在线") == 0);
	UiRingProgress_SetValueText(&p, NULL);
	CHECK(p.valueText[0] == 0);
	UiRingProgress_SetFormatter(&p, fmt_mbps, NULL);
	CHECK(p.formatter == fmt_mbps);

	UiRingProgress_SetUnit(&p,
		"一二三四五六七八九十一二三四五六七八九十AB");
	CHECK(utf8_valid(p.unit));
	{
		size_t n = 0;
		while (p.unit[n]) n++;
		CHECK(n <= UI_RING_UNIT_MAX - 1);
	}
	UiRingProgress_SetTitle(&p,
		"任务完成度任务完成度任务完成度任务完成度任务完成度任务完成度"
		"任务完成度任务完成度任务完成度任务完成度任务完成度任务完成度");
	CHECK(utf8_valid(p.title));

	UiRingProgress_SetStyle(&p, 1.0f, 0xFF112233u, 0);
	CHECK(p.ringWidth == 2.0f && p.color == 0xFF112233u && !p.roundCaps);
	p.overlayDirty = 0;
	UiRingProgress_SetStyle(&p, 2.0f, 0xFF112233u, 0);
	CHECK(p.overlayDirty == 0);
	UiRingProgress_SetStyle(&p, 999.0f, UI_RING_THEME_COLOR, 1);
	CHECK(p.ringWidth == 64.0f && p.roundCaps);

	UiRingProgress_SetDurationMs(&p, 300);
	UiRingProgress_SetValue(&p, 90.0);
	CHECK(UiRingProgress_Animating(&p));
	UiRingProgress_SetHidden(&p, 1);
	CHECK(p.hidden && !UiRingProgress_Animating(&p));
	CHECK(Ui_AnimGet(&ui, p.id, UI_AN_USER, -1.0f) == 0.45f);
	UiRingProgress_SetHidden(&p, 0);
	CHECK(!p.hidden);
	CHECK(!UiRingProgress_OverlayAlive(&p));

	UiRingProgress_Shutdown(&p);
	CHECK(p.ui == NULL && p.ring == NULL);

	out_(g_fail ? "ring_progress: FAILED\n" : "ring_progress: all passed\n");
	{
		char summary[80];
		sprintf(summary, "passed=%d failed=%d\n", g_pass, g_fail);
		out_(summary);
	}
	ExitProcess(g_fail ? 1u : 0u);
}
