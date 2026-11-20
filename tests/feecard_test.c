/*
 * feecard_test.c - UiFeeCard 校验/尺寸 headless 断言 (0CRT)
 */
#include <windows.h>
#include <string.h>
#include "ui/ui_feecard.h"

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

static void valid_form(UiFeeCard *c)
{
	ZeroMemory(c, sizeof(*c));
	c->form.minPrice = 100;
	c->form.ignoreTime = 3;
	c->form.timeUnit = 1;
	c->form.discount = 100;
}

static void test_validate(void)
{
	UiFeeCard c;
	const char *msg = NULL;

	valid_form(&c);
	CHECK(UiFeeCard_Validate(&c, &msg) == 1);
	CHECK(msg == NULL);

	valid_form(&c);
	c.form.minPrice = 0;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);
	CHECK(msg != NULL && msg[0] != 0);

	valid_form(&c);
	c.form.discount = 0;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);

	valid_form(&c);
	c.form.discount = 101;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);

	valid_form(&c);
	c.form.discount = -1;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);

	valid_form(&c);
	c.form.timeUnit = 0;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);

	valid_form(&c);
	c.form.ignoreTime = -1;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);

	valid_form(&c);
	c.form.prices[0].price = -1;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);

	valid_form(&c);
	c.form.prices[167].pack = 1;
	c.form.prices[167].packPrice = -1;
	CHECK(UiFeeCard_Validate(&c, &msg) == 0);
}

static void test_size_and_form(void)
{
	UiFeeCard c;
	valid_form(&c);
	CHECK(UiFeeCard_Form(&c) == &c.form);
	CHECK(UiFeeCard_IsDirty(&c) == 0);
	CHECK(UiFeeCard_MeasureHeight(900.0f) > 0.0f);
	CHECK(UiFeeCard_MeasureHeight(600.0f) >
		UiFeeCard_MeasureHeight(900.0f));
	CHECK(UiFeeCard_Form(NULL) == NULL);
	CHECK(UiFeeCard_IsDirty(NULL) == 0);
	CHECK(UiFeeCard_Validate(NULL, NULL) == 0);
}

void __stdcall startup(void)
{
	char line[160];

	test_validate();
	test_size_and_form();
	sprintf(line, "feecard_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
