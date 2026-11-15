/*
 * ui_theme_edit_test.c - 主题令牌工具 headless 断言
 *
 * 覆盖: 令牌表完整性、读写往返、工程文件保存/加载、导出代码结构、
 * HEX 解析/格式化、对比度、字段修改判定。
 * 构建: bin\cui_theme_edit_test.exe; 退出码 0=全通过。
 */
#include <windows.h>
#include <stdarg.h>
#include "ui/ui_theme.h"
#include "ui/ui_theme_edit.h"

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

static int eq(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

static int contains(const char *hay, const char *needle)
{
	int i, j;
	for (i = 0; hay[i]; i++) {
		for (j = 0; needle[j] && hay[i + j] == needle[j]; j++) ;
		if (!needle[j]) return 1;
	}
	return 0;
}

static int count_of(const char *hay, const char *needle)
{
	int i, j, n = 0;
	for (i = 0; hay[i]; i++) {
		for (j = 0; needle[j] && hay[i + j] == needle[j]; j++) ;
		if (!needle[j]) { n++; i += j - 1; }
	}
	return n;
}

static int idx_of(const char *key)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		if (eq(UiTheme_TokenTable[i].key, key)) return i;
	return -1;
}

static int field_index_of(const char *key)
{
	int i = idx_of(key);
	if (i >= 0) return i;
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		if (eq(UiTheme_RadiusTable[i].key, key))
			return UI_THEME_TOKEN_COUNT + i;
	return -1;
}

/* 与导出一致的 8 位大写十六进制 (不被 FormatHex 省略 alpha) */
static void color8(UiColor c, char *out)
{
	static const char hx[] = "0123456789ABCDEF";
	int i;
	for (i = 0; i < 8; i++)
		out[i] = hx[(c >> ((7 - i) * 4)) & 0xF];
	out[8] = 0;
}

static void fill_sets(UiThemeSet *l, UiThemeSet *d)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
		l->color[i] = 0xFF000000u | (UiColor)(i * 0x010203u);
		d->color[i] = 0x80000000u | (UiColor)(0xFFFFFFu - i * 0x010101u);
	}
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++) {
		l->radius[i] = (float)i + 0.5f;
		d->radius[i] = (float)(i + 2) * 1.5f;
	}
}

static void test_table(void)
{
	int i, j;
	CHECK(UI_THEME_TOKEN_COUNT == 68);
	CHECK(UI_THEME_RADIUS_COUNT == 5);
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
		CHECK(UiTheme_TokenTable[i].key && UiTheme_TokenTable[i].key[0]);
		CHECK(UiTheme_TokenTable[i].hint && UiTheme_TokenTable[i].group);
		CHECK(UiTheme_TokenTable[i].kind == UI_THEME_TOKEN_COLOR);
		for (j = 0; j < i; j++)
			CHECK(!eq(UiTheme_TokenTable[i].key, UiTheme_TokenTable[j].key));
		for (j = 0; j < UI_THEME_RADIUS_COUNT; j++)
			CHECK(!eq(UiTheme_TokenTable[i].key, UiTheme_RadiusTable[j].key));
	}
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		CHECK(UiTheme_RadiusTable[i].kind == UI_THEME_TOKEN_RADIUS);
	CHECK(UiTheme_TokenTable[0].offset ==
		(unsigned)((char *)&((UiTheme *)0)->page - (char *)0));
}

static void test_read_write(void)
{
	UiThemeSet l, d, back;
	UiTheme t;
	int i;

	fill_sets(&l, &d);
	ZeroMemory(&t, sizeof(t));
	UiThemeSet_Apply(&t, UI_THEME_LIGHT, &l);
	CHECK(t.themeId == UI_THEME_LIGHT);
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
		UiColor c = *(UiColor *)((char *)&t + UiTheme_TokenTable[i].offset);
		CHECK(c == l.color[i]);
	}
	CHECK(t.windowRadius == l.radius[0]);
	CHECK(t.toastRadius == l.radius[4]);
	/* 派生角色: primary.base 跟随 blue */
	CHECK(t.primary.base == t.blue);
	CHECK(t.neutral.hover == t.btnHoverBg);

	UiThemeSet_Read(&back, &t);
	CHECK(UiThemeSet_Equals(&back, &l));
	UiThemeSet_Apply(&t, UI_THEME_DARK, &d);
	CHECK(t.themeId == UI_THEME_DARK);
	CHECK(idx_of("blue") > 0);
	CHECK(t.primary.base == d.color[idx_of("blue")]);
}

static void test_project_io(void)
{
	UiThemeSet l, d, l2, d2;
	char buf[16384];
	int n;

	fill_sets(&l, &d);
	n = UiThemeSet_Save(&l, &d, buf, (int)sizeof(buf));
	CHECK(n > 0);
	CHECK(contains(buf, "cui-theme 1"));
	CHECK(contains(buf, "[light]"));
	CHECK(contains(buf, "[dark]"));
	CHECK(contains(buf, "windowRadius="));
	CHECK(contains(buf, "toastOverlay="));

	ZeroMemory(&l2, sizeof(l2));
	ZeroMemory(&d2, sizeof(d2));
	CHECK(UiThemeSet_Load(&l2, &d2, buf) == 1);
	CHECK(UiThemeSet_Equals(&l, &l2));
	CHECK(UiThemeSet_Equals(&d, &d2));

	/* 容错: 未知键/缺行不影响已解析值 */
	l2 = l;
	d2 = d;
	CHECK(UiThemeSet_Load(&l2, &d2,
		"cui-theme 1\n[light]\nunknownKey=123456\npage=112233\n") == 1);
	CHECK(l2.color[1] == l.color[1]);      /* page2 未在文件里 = 保留 */
	CHECK(l2.color[0] == 0xFF112233u);
	CHECK(d2.color[0] == d.color[0]);

	CHECK(UiThemeSet_Load(&l2, &d2, "not a project\n") == 0);
	CHECK(UiThemeSet_Load(&l2, &d2, "") == 0);

	/* 缓冲区过小返回 -1 且以 NUL 结尾 */
	{
		char tiny[40];
		CHECK(UiThemeSet_Save(&l, &d, tiny, (int)sizeof(tiny)) == -1);
		CHECK(tiny[sizeof(tiny) - 1] == 0);
	}
}

static void test_codegen(void)
{
	UiThemeSet l, d;
	char buf[16384];
	int n, i;
	char needle[64];

	fill_sets(&l, &d);
	n = UiThemeSet_Codegen(&l, &d, buf, (int)sizeof(buf), "unit-test");
	CHECK(n > 0);
	CHECK(contains(buf, "/* generated by cui_theme_editor unit-test */"));
	CHECK(contains(buf, "static void theme_apply_preset(UiTheme *t, UiThemeId id)"));
	CHECK(contains(buf, "t->themeId = id;"));
	CHECK(contains(buf, "if (id == UI_THEME_DARK) {"));
	CHECK(contains(buf, "t->windowRadius = 3.0f;"));
	CHECK(contains(buf, "t->toastRadius = 9.0f;"));
	/* 两模式圆角各自保留, 不能只生成 Dark 的一份 */
	CHECK(contains(buf, "t->windowRadius = 0.5f;"));
	CHECK(contains(buf, "t->toastRadius = 4.5f;"));
	CHECK(count_of(buf, "= 0x") == UI_THEME_TOKEN_COUNT * 2);
	CHECK(count_of(buf, "t->") >= UI_THEME_TOKEN_COUNT * 2);
	CHECK(count_of(buf, "return;") == 1);

	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
		char hex[16];
		lstrcpyA(needle, "t->");
		lstrcatA(needle, UiTheme_TokenTable[i].key);
		lstrcatA(needle, " = 0x");
		color8(d.color[i], hex);
		lstrcatA(needle, hex);
		lstrcatA(needle, ";");
		CHECK(contains(buf, needle));
	}
	{
		char tiny[64];
		CHECK(UiThemeSet_Codegen(&l, &d, tiny, (int)sizeof(tiny), NULL)
			== -1);
		CHECK(tiny[sizeof(tiny) - 1] == 0);
	}
}

static void test_hex(void)
{
	UiColor c;
	char buf[16];

	CHECK(UiTheme_ParseHex("4C6FFF", &c) && c == 0xFF4C6FFFu);
	CHECK(UiTheme_ParseHex("0x4c6fff", &c) && c == 0xFF4C6FFFu);
	CHECK(UiTheme_ParseHex("804C6FFF", &c) && c == 0x804C6FFFu);
	CHECK(UiTheme_ParseHex("  112233  ", &c) && c == 0xFF112233u);
	CHECK(!UiTheme_ParseHex("11223", &c));
	CHECK(!UiTheme_ParseHex("1122334", &c));
	CHECK(!UiTheme_ParseHex("GGHHII", &c));
	CHECK(!UiTheme_ParseHex("", &c));
	CHECK(!UiTheme_ParseHex(NULL, &c));

	CHECK(UiTheme_FormatHex(0xFF4C6FFFu, buf, (int)sizeof(buf)) == 6);
	CHECK(eq(buf, "4C6FFF"));
	CHECK(UiTheme_FormatHex(0x804C6FFFu, buf, (int)sizeof(buf)) == 8);
	CHECK(eq(buf, "804C6FFF"));
	CHECK(UiTheme_FormatHex(0xFF000000u, buf, (int)sizeof(buf)) == 6);
	CHECK(eq(buf, "000000"));
	{
		char tiny[4];
		CHECK(UiTheme_FormatHex(0xFF4C6FFFu, tiny, (int)sizeof(tiny)) == 0);
		CHECK(tiny[0] == 0);
	}
}

static void test_contrast(void)
{
	float v;
	v = UiTheme_Contrast(0xFF000000u, 0xFFFFFFFFu);
	CHECK(v > 20.9f && v < 21.1f);
	v = UiTheme_Contrast(0xFFFFFFFFu, 0xFFFFFFFFu);
	CHECK(v > 0.99f && v < 1.01f);
	v = UiTheme_Contrast(0xFF808080u, 0xFFFFFFFFu);
	CHECK(v > 1.85f && v < 1.95f);
	/* 半透明前景先合成到背景, 结果应比纯黑弱 */
	v = UiTheme_Contrast(0x80000000u, 0xFFFFFFFFu);
	CHECK(v > 1.0f && v < 21.0f);
}

static void test_modified(void)
{
	UiThemeSet l, d;
	int i;
	fill_sets(&l, &d);
	for (i = 0; i < UI_THEME_FIELD_COUNT; i++)
		CHECK(!UiThemeSet_FieldModified(&l, &l, i));
	{
		UiThemeSet base = l;
		base.color[3] = 0;
		CHECK(UiThemeSet_FieldModified(&l, &base, 3) == 1);
		base = l;
		base.radius[2] += 1.0f;
		CHECK(UiThemeSet_FieldModified(&l, &base, UI_THEME_TOKEN_COUNT + 2)
			== 1);
		CHECK(!UiThemeSet_FieldModified(&l, &base, UI_THEME_TOKEN_COUNT + 1));
	}
	CHECK(!UiThemeSet_FieldModified(&l, NULL, 0));
	CHECK(!UiThemeSet_FieldModified(&l, &d, -1));
	CHECK(!UiThemeSet_FieldModified(&l, &d, UI_THEME_FIELD_COUNT));
}

static void test_basic_view(void)
{
	int i, j;
	int expect[UI_THEME_FIELD_COUNT] = { 0 };
	const char *keys[UI_THEME_BASIC_COUNT] = {
		"page", "card", "border", "text", "text2", "text3", "blue",
		"btnBg", "rowOn", "inputBg", "btnPrimary", "btnPrimaryText",
		"windowRadius", "panelRadius", "buttonRadius"
	};
	int found = 0;

	CHECK(UI_THEME_BASIC_COUNT == 15);
	for (i = 0; i < UI_THEME_BASIC_COUNT; i++) {
		int fi = UiTheme_BasicField(i);
		CHECK(fi >= 0 && fi < UI_THEME_FIELD_COUNT);
		if (fi < 0 || fi >= UI_THEME_FIELD_COUNT) continue;
		CHECK(UiTheme_IsBasicField(fi));
		CHECK(UiTheme_FieldLabel(fi) && UiTheme_FieldLabel(fi)[0]);
		CHECK(UiTheme_FieldGroup(fi) && UiTheme_FieldGroup(fi)[0]);
		CHECK(field_index_of(keys[i]) == fi);
		CHECK(!expect[fi]);
		expect[fi] = 1;
		for (j = 0; j < i; j++)
			CHECK(UiTheme_BasicField(j) != fi);
	}
	CHECK(UiTheme_BasicField(-1) == -1);
	CHECK(UiTheme_BasicField(UI_THEME_BASIC_COUNT) == -1);
	CHECK(!UiTheme_IsBasicField(-1));
	CHECK(!UiTheme_IsBasicField(UI_THEME_FIELD_COUNT));
	CHECK(!UiTheme_FieldLabel(-1));
	CHECK(!UiTheme_FieldGroup(UI_THEME_FIELD_COUNT));
	CHECK(!UiTheme_FieldLabel(idx_of("toastBg")));
	CHECK(!UiTheme_IsBasicField(idx_of("toastBg")));
	CHECK(!UiTheme_FieldGroup(idx_of("page2")));

	/* 基础视图必须覆盖颜色和圆角两类, 且分组收敛为 4 组 */
	for (i = 0; i < UI_THEME_FIELD_COUNT; i++) {
		if (!expect[i]) continue;
		found++;
		CHECK(UiTheme_FieldGroup(i) != NULL);
	}
	CHECK(found == UI_THEME_BASIC_COUNT);
	{
		static const char *groups[4] = { "外观", "文字", "交互", "几何" };
		int g;
		for (g = 0; g < 4; g++) {
			int seen = 0;
			for (i = 0; i < UI_THEME_FIELD_COUNT; i++)
				if (expect[i] && eq(UiTheme_FieldGroup(i), groups[g]))
					seen = 1;
			CHECK(seen);
		}
	}
}

static void test_preset_consistency(void)
{
	/* 内置预设经编辑器读出并原样生成, 必须复现同样的 68 个色值 */
	UiTheme t;
	UiThemeSet l, d;
	char buf[16384];
	int i, n;

	ZeroMemory(&t, sizeof(t));
	UiTheme_SetTheme(&t, UI_THEME_LIGHT);   /* 全局注册表为空: 仅本实例 */
	UiThemeSet_Read(&l, &t);
	UiTheme_SetTheme(&t, UI_THEME_DARK);
	UiThemeSet_Read(&d, &t);

	n = UiThemeSet_Codegen(&l, &d, buf, (int)sizeof(buf), "rt");
	CHECK(n > 0);
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
		char needle[96], hex[16];
		lstrcpyA(needle, "t->");
		lstrcatA(needle, UiTheme_TokenTable[i].key);
		lstrcatA(needle, " = 0x");
		color8(l.color[i], hex);
		lstrcatA(needle, hex);
		lstrcatA(needle, ";");
		CHECK(contains(buf, needle));

		lstrcpyA(needle, "t->");
		lstrcatA(needle, UiTheme_TokenTable[i].key);
		lstrcatA(needle, " = 0x");
		color8(d.color[i], hex);
		lstrcatA(needle, hex);
		lstrcatA(needle, ";");
		CHECK(contains(buf, needle));
	}
	/* 内置 Light 的亮色悬停令牌保持已发布值 */
	CHECK(l.color[idx_of("btnHoverBg")] == 0xFFE4E9F8u);
	CHECK(l.color[idx_of("btnPressBg")] == 0xFFD5DDF0u);
	CHECK(d.color[idx_of("page")] == 0xFF121722u);
}

static void load_base_sets(UiThemeSet *l, UiThemeSet *d)
{
	UiTheme t;
	ZeroMemory(&t, sizeof(t));
	UiTheme_SetTheme(&t, UI_THEME_LIGHT);
	UiThemeSet_Read(l, &t);
	UiTheme_SetTheme(&t, UI_THEME_DARK);
	UiThemeSet_Read(d, &t);
}

static int hue_deg(UiColor c)
{
	float r = (float)((c >> 16) & 0xFF) / 255.0f;
	float g = (float)((c >> 8) & 0xFF) / 255.0f;
	float b = (float)(c & 0xFF) / 255.0f;
	float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
	float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
	float d = mx - mn, h = 0.0f;
	if (d > 0.0f) {
		if (mx == r) h = (g - b) / d;
		else if (mx == g) h = (b - r) / d + 2.0f;
		else h = (r - g) / d + 4.0f;
		h *= 60.0f;
		if (h < 0.0f) h += 360.0f;
	}
	return (int)(h + 0.5f) % 360;
}

static int hue_dist(int a, int b)
{
	int d = a - b;
	if (d < 0) d = -d;
	return d > 180 ? 360 - d : d;
}

static int sets_equal(const UiThemeSet *a, const UiThemeSet *b)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		if (a->color[i] != b->color[i]) return 0;
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		if (a->radius[i] != b->radius[i]) return 0;
	return 1;
}

static int colors_equal(const UiThemeSet *a, const UiThemeSet *b,
	const char *const *keys, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		int k = idx_of(keys[i]);
		if (k < 0 || a->color[k] != b->color[k]) return 0;
	}
	return 1;
}

static int pair_readable(UiColor fg, UiColor bg, float floor)
{
	return UiTheme_Contrast(fg, bg) + 0.001f >= floor;
}

static unsigned gen_once(UiThemeSet *l, UiThemeSet *d, unsigned seed,
	unsigned locks, const UiThemeSet *tplL, const UiThemeSet *tplD,
	const UiThemeSet *keepL, const UiThemeSet *keepD)
{
	UiThemeGenOpts o;
	o.seed = seed;
	o.locks = locks;
	o.templateLight = tplL;
	o.templateDark = tplD;
	o.keepLight = keepL;
	o.keepDark = keepD;
	return UiThemeGen_Palette(l, d, &o);
}

static void test_generator(void)
{
	UiThemeSet tplL, tplD, keepL, keepD, l, d, l2, d2;
	static const char *pri_keys[] = {
		"blue", "blue2", "switchOn", "tab2", "btn2", "iconBlue", "power",
		"rowOn", "rowOnBorder", "inputFocus", "btnPrimary", "btnPrimary2",
		"btnPrimaryHover", "btnPrimaryPress", "btnPrimaryBorder",
		"progressFill", "progressFill2"
	};
	static const char *acc_keys[] = { "gold", "tab1", "btn1" };
	static const char *neu_keys[] = {
		"page", "page2", "card", "border", "frame", "frameHi",
		"text", "text2", "text3", "text4", "btnBg", "switchOff",
		"panelTop", "panelBottom", "hdrLine", "panelText", "panelDisabled",
		"tile", "pill", "controlBox", "controlBorder", "rowOff",
		"rowOffBorder", "pageLine", "inputBg", "inputBorder", "hint",
		"panelBg", "panelBorder", "btnHoverBg", "btnPressBg", "btnText",
		"btnBorder", "btnHoverBorder", "btnDisabledBg", "btnDisabledText",
		"progressTrack", "toastBg", "toastBorder", "toastText", "toastOverlay"
	};
	static const char *sem_keys[] = { "green", "okBg", "orange", "red",
		"danger", "dangerBg" };
	unsigned seed;
	int i;

	load_base_sets(&tplL, &tplD);

	/* 确定性 + 种子有效性 */
	seed = gen_once(&l, &d, 0x12345678u, 0, &tplL, &tplD, &tplL, &tplD);
	CHECK(seed != 0);
	CHECK(gen_once(&l2, &d2, 0x12345678u, 0, &tplL, &tplD, &tplL, &tplD) != 0);
	CHECK(sets_equal(&l, &l2) && sets_equal(&d, &d2));
	CHECK(!sets_equal(&l, &tplL));
	CHECK(!sets_equal(&d, &tplD));
	CHECK(gen_once(&l2, &d2, 0x87654321u, 0, &tplL, &tplD, &tplL, &tplD) != 0);
	CHECK(!sets_equal(&l, &l2) || !sets_equal(&d, &d2));
	/* seed=0 仍可用, 且实际种子可复现 */
	{
		unsigned s0 = gen_once(&l, &d, 0, 0, &tplL, &tplD, &tplL, &tplD);
		CHECK(s0 != 0);
		CHECK(gen_once(&l2, &d2, s0, 0, &tplL, &tplD, &tplL, &tplD) != 0);
		CHECK(sets_equal(&l, &l2) && sets_equal(&d, &d2));
	}

	/* 非法参数不写输出 */
	l.color[idx_of("page")] = 0x11223344u;
	d.color[idx_of("page")] = 0x55667788u;
	{
		UiThemeGenOpts bad;
		bad.seed = 1; bad.locks = 0;
		bad.templateLight = NULL;
		bad.templateDark = &tplD;
		bad.keepLight = NULL; bad.keepDark = NULL;
		CHECK(UiThemeGen_Palette(&l, &d, &bad) == 0);
		CHECK(l.color[idx_of("page")] == 0x11223344u);
		CHECK(d.color[idx_of("page")] == 0x55667788u);
		CHECK(UiThemeGen_Palette(NULL, &d, &bad) == 0);
		CHECK(UiThemeGen_Palette(&l, &d, NULL) == 0);
	}

	/* alpha 保留 + 圆角沿用当前工程 */
	keepL = tplL;
	keepD = tplD;
	keepL.radius[0] = 3.25f;
	keepL.radius[1] = 4.5f;
	keepL.radius[2] = 6.75f;
	keepL.radius[3] = 2.5f;
	keepL.radius[4] = 9.0f;
	keepD.radius[0] = 7.25f;
	keepD.radius[1] = 8.5f;
	keepD.radius[2] = 9.75f;
	keepD.radius[3] = 10.5f;
	keepD.radius[4] = 11.0f;
	CHECK(gen_once(&l, &d, 0x11111111u, 0, &tplL, &tplD, &keepL, &keepD));
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
		CHECK((l.color[i] & 0xFF000000u) == (tplL.color[i] & 0xFF000000u));
		CHECK((d.color[i] & 0xFF000000u) == (tplD.color[i] & 0xFF000000u));
	}
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++) {
		CHECK(l.radius[i] == keepL.radius[i]);
		CHECK(d.radius[i] == keepD.radius[i]);
	}

	/* 锁定主色: 主色族/强调/主按钮文字原样, 其余仍可变化 */
	keepL = tplL;
	keepD = tplD;
	keepL.color[idx_of("blue")] = 0xFF123456u;
	keepL.color[idx_of("tab1")] = 0xFF654321u;
	keepL.color[idx_of("btnPrimaryText")] = 0xFF0E1218u;
	keepL.color[idx_of("page")] = 0xFF010203u;
	keepD = keepL;
	CHECK(gen_once(&l, &d, 0x22222222u, UI_THEME_LOCK_PRIMARY,
		&tplL, &tplD, &keepL, &keepD));
	CHECK(colors_equal(&l, &keepL, pri_keys,
		(int)(sizeof(pri_keys) / sizeof(pri_keys[0]))));
	CHECK(colors_equal(&l, &keepL, acc_keys,
		(int)(sizeof(acc_keys) / sizeof(acc_keys[0]))));
	CHECK(l.color[idx_of("btnPrimaryText")] ==
		keepL.color[idx_of("btnPrimaryText")]);
	CHECK(colors_equal(&d, &keepD, pri_keys,
		(int)(sizeof(pri_keys) / sizeof(pri_keys[0]))));
	CHECK(l.color[idx_of("page")] != keepL.color[idx_of("page")]);
	CHECK(l.color[idx_of("text")] != keepL.color[idx_of("text")] ||
		l.color[idx_of("card")] != keepL.color[idx_of("card")]);

	/* 锁定中性色: 中性族原样, 主色重新生成 */
	keepL = tplL;
	keepD = tplD;
	keepL.color[idx_of("page")] = 0xFF010203u;
	keepL.color[idx_of("text")] = 0xFF040506u;
	keepL.color[idx_of("blue")] = 0xFF123456u;
	keepD = keepL;
	CHECK(gen_once(&l, &d, 0x33333333u, UI_THEME_LOCK_NEUTRAL,
		&tplL, &tplD, &keepL, &keepD));
	CHECK(colors_equal(&l, &keepL, neu_keys,
		(int)(sizeof(neu_keys) / sizeof(neu_keys[0]))));
	CHECK(colors_equal(&d, &keepD, neu_keys,
		(int)(sizeof(neu_keys) / sizeof(neu_keys[0]))));
	CHECK(l.color[idx_of("blue")] != keepL.color[idx_of("blue")]);

	/* 锁定语义色: 绿/橙/红原样 */
	keepL = tplL;
	keepD = tplD;
	keepL.color[idx_of("green")] = 0xFF112233u;
	keepL.color[idx_of("orange")] = 0xFF223344u;
	keepL.color[idx_of("red")] = 0xFF334455u;
	keepL.color[idx_of("blue")] = 0xFF445566u;
	keepD = keepL;
	CHECK(gen_once(&l, &d, 0x44444444u, UI_THEME_LOCK_SEMANTIC,
		&tplL, &tplD, &keepL, &keepD));
	CHECK(colors_equal(&l, &keepL, sem_keys,
		(int)(sizeof(sem_keys) / sizeof(sem_keys[0]))));
	CHECK(colors_equal(&d, &keepD, sem_keys,
		(int)(sizeof(sem_keys) / sizeof(sem_keys[0]))));
	CHECK(l.color[idx_of("blue")] != keepL.color[idx_of("blue")]);

	/* 三组全锁: 等于当前工程 (含圆角), 不因对比度拒稿 */
	keepL = tplL;
	keepD = tplD;
	keepL.color[idx_of("page")] = 0xFF010101u;
	keepD.color[idx_of("page")] = 0xFF020202u;
	keepL.radius[0] = 1.0f;
	keepD.radius[0] = 2.0f;
	CHECK(gen_once(&l, &d, 0x55555555u,
		UI_THEME_LOCK_PRIMARY | UI_THEME_LOCK_NEUTRAL |
		UI_THEME_LOCK_SEMANTIC, &tplL, &tplD, &keepL, &keepD));
	CHECK(sets_equal(&l, &keepL));
	CHECK(sets_equal(&d, &keepD));

	/* keep = NULL 时锁定组回落到模板 */
	CHECK(gen_once(&l, &d, 0x66666666u, UI_THEME_LOCK_NEUTRAL,
		&tplL, &tplD, NULL, NULL));
	CHECK(colors_equal(&l, &tplL, neu_keys,
		(int)(sizeof(neu_keys) / sizeof(neu_keys[0]))));

	/* Light/Dark 同种子联动: 主色/强调色相接近 */
	for (i = 0; i < 24; i++) {
		CHECK(gen_once(&l, &d, 0x70000000u + (unsigned)i * 7919u, 0,
			&tplL, &tplD, &tplL, &tplD));
		CHECK(hue_dist(hue_deg(l.color[idx_of("blue")]),
			hue_deg(d.color[idx_of("blue")])) <= 8);
		CHECK(hue_dist(hue_deg(l.color[idx_of("tab1")]),
			hue_deg(d.color[idx_of("tab1")])) <= 8);
	}

	/* 语义色分离 + 对比度底线 (200 个种子) */
	for (i = 0; i < 200; i++) {
		int danger, warn, succ, primary;
		CHECK(gen_once(&l, &d, 0x80000000u + (unsigned)i * 2654435761u, 0,
			&tplL, &tplD, &tplL, &tplD));
		primary = hue_deg(l.color[idx_of("blue")]);
		danger = hue_deg(l.color[idx_of("red")]);
		warn = hue_deg(l.color[idx_of("orange")]);
		succ = hue_deg(l.color[idx_of("green")]);
		CHECK(hue_dist(primary, danger) >= 25);
		CHECK(hue_dist(primary, warn) >= 20);
		CHECK(hue_dist(primary, succ) >= 20);
		CHECK(pair_readable(l.color[idx_of("text")], l.color[idx_of("page")], 4.3f));
		CHECK(pair_readable(l.color[idx_of("text")], l.color[idx_of("card")], 4.3f));
		CHECK(pair_readable(l.color[idx_of("text2")], l.color[idx_of("card")], 2.15f));
		CHECK(pair_readable(l.color[idx_of("text3")], l.color[idx_of("card")], 1.45f));
		CHECK(pair_readable(l.color[idx_of("btnText")], l.color[idx_of("btnBg")], 2.15f));
		CHECK(pair_readable(l.color[idx_of("hint")], l.color[idx_of("inputBg")], 1.15f));
		CHECK(pair_readable(l.color[idx_of("toastText")], l.color[idx_of("toastBg")], 3.25f));
		CHECK(pair_readable(l.color[idx_of("panelText")], l.color[idx_of("panelBg")], 1.8f));
		CHECK(pair_readable(l.color[idx_of("btnPrimaryText")], l.color[idx_of("btnPrimary")], 1.25f));
		CHECK(pair_readable(l.color[idx_of("btnPrimaryText")], l.color[idx_of("btnPrimary2")], 1.25f));
		CHECK(pair_readable(l.color[idx_of("btnPrimaryText")], l.color[idx_of("btnPrimaryHover")], 1.25f));
		CHECK(pair_readable(d.color[idx_of("text")], d.color[idx_of("page")], 4.3f));
		CHECK(pair_readable(d.color[idx_of("text")], d.color[idx_of("card")], 4.3f));
		CHECK(pair_readable(d.color[idx_of("btnText")], d.color[idx_of("btnBg")], 2.15f));
		CHECK(pair_readable(d.color[idx_of("btnPrimaryText")], d.color[idx_of("btnPrimaryHover")], 1.25f));
	}

	/* 批量种子应覆盖足够多的主色 */
	{
		UiColor seen[48];
		int n = 0, j, dup;
		for (i = 0; i < 48; i++) {
			UiColor c;
			CHECK(gen_once(&l, &d, 0x90000000u + (unsigned)i * 40503u, 0,
				&tplL, &tplD, &tplL, &tplD));
			c = l.color[idx_of("blue")];
			dup = 0;
			for (j = 0; j < n; j++) if (seen[j] == c) { dup = 1; break; }
			if (!dup) seen[n++] = c;
		}
		CHECK(n >= 24);
	}
}

void __stdcall startup(void)
{
	char line[160];

	test_table();
	test_basic_view();
	test_read_write();
	test_project_io();
	test_codegen();
	test_hex();
	test_contrast();
	test_modified();
	test_preset_consistency();
	test_generator();

	sprintf(line, "ui_theme_edit_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
