#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include "ui/ui_theme_edit.h"

#define C_(key, group, hint) { #key, group, hint, \
	(unsigned)offsetof(UiTheme, key), UI_THEME_TOKEN_COLOR }
#define R_(key, hint) { #key, "几何", hint, \
	(unsigned)offsetof(UiTheme, key), UI_THEME_TOKEN_RADIUS }

const UiThemeTokenDesc UiTheme_TokenTable[UI_THEME_TOKEN_COUNT] = {
	C_(page, "基础", "窗口背景渐变上端"),
	C_(page2, "基础", "窗口背景渐变下端"),
	C_(card, "基础", "卡片/输入底色"),
	C_(border, "基础", "分隔线与描边"),
	C_(frame, "基础", "窗口外框"),
	C_(frameHi, "基础", "内层高光描边"),
	C_(text, "基础", "主文本"),
	C_(text2, "基础", "次文本"),
	C_(text3, "基础", "辅助文本"),
	C_(text4, "基础", "最弱文本"),
	C_(green, "强调", "成功 (旧令牌)"),
	C_(gold, "强调", "金色强调"),
	C_(red, "强调", "错误 (旧令牌)"),
	C_(orange, "强调", "警告 (旧令牌)"),
	C_(blue, "强调", "主色 (旧令牌)"),
	C_(blue2, "强调", "主色亮阶"),
	C_(btnBg, "控件", "次级按钮底色"),
	C_(switchOn, "控件", "开关打开"),
	C_(switchOff, "控件", "开关关闭"),
	C_(panelTop, "设备面板", "面板渐变上端"),
	C_(panelBottom, "设备面板", "面板渐变下端"),
	C_(hdrLine, "设备面板", "面板分隔线"),
	C_(tab1, "设备面板", "Tab 渐变起点"),
	C_(tab2, "设备面板", "Tab 渐变终点"),
	C_(btn1, "设备面板", "面板按钮亮端"),
	C_(btn2, "设备面板", "面板按钮暗端"),
	C_(iconBlue, "设备面板", "面板图标"),
	C_(panelText, "设备面板", "面板文字"),
	C_(panelDisabled, "设备面板", "面板禁用文字"),
	C_(tile, "设备面板", "磁贴底"),
	C_(pill, "设备面板", "胶囊底"),
	C_(power, "设备面板", "电源图标"),
	C_(controlBox, "卡片/控件", "控件盒底"),
	C_(controlBorder, "卡片/控件", "控件盒描边"),
	C_(rowOn, "卡片/控件", "选中行底"),
	C_(rowOnBorder, "卡片/控件", "选中行描边"),
	C_(rowOff, "卡片/控件", "普通行底"),
	C_(rowOffBorder, "卡片/控件", "普通行描边"),
	C_(pageLine, "卡片/控件", "表格行分隔"),
	C_(inputBg, "安装器", "输入框底"),
	C_(inputBorder, "安装器", "输入框描边"),
	C_(inputFocus, "安装器", "输入框焦点描边"),
	C_(hint, "安装器", "占位文字"),
	C_(danger, "安装器", "危险色"),
	C_(dangerBg, "安装器", "危险浅底"),
	C_(okBg, "安装器", "成功浅底"),
	C_(panelBg, "面板/主按钮", "通用面板底"),
	C_(panelBorder, "面板/主按钮", "通用面板描边"),
	C_(btnPrimary, "面板/主按钮", "主按钮渐变上端"),
	C_(btnPrimary2, "面板/主按钮", "主按钮渐变下端"),
	C_(btnPrimaryHover, "面板/主按钮", "主按钮悬停"),
	C_(btnPrimaryPress, "面板/主按钮", "主按钮按下"),
	C_(btnPrimaryText, "面板/主按钮", "主按钮文字"),
	C_(btnPrimaryBorder, "面板/主按钮", "主按钮描边"),
	C_(btnHoverBg, "次级按钮", "次级悬停底"),
	C_(btnPressBg, "次级按钮", "次级按下底"),
	C_(btnText, "次级按钮", "次级文字"),
	C_(btnBorder, "次级按钮", "次级描边"),
	C_(btnHoverBorder, "次级按钮", "次级悬停描边"),
	C_(btnDisabledBg, "次级按钮", "禁用底"),
	C_(btnDisabledText, "次级按钮", "禁用文字"),
	C_(progressTrack, "进度", "进度轨道"),
	C_(progressFill, "进度", "进度渐变起点"),
	C_(progressFill2, "进度", "进度渐变终点"),
	C_(toastBg, "Toast", "Toast 底"),
	C_(toastBorder, "Toast", "Toast 描边"),
	C_(toastText, "Toast", "Toast 文字"),
	C_(toastOverlay, "Toast", "Toast 遮罩")
};

const UiThemeTokenDesc UiTheme_RadiusTable[UI_THEME_RADIUS_COUNT] = {
	R_(windowRadius, "窗口外轮廓圆角"),
	R_(panelRadius, "面板/卡片圆角"),
	R_(buttonRadius, "按钮圆角"),
	R_(progressRadius, "进度条圆角"),
	R_(toastRadius, "Toast 圆角")
};

/* 基础视图: 先编辑这 15 项就能得到一套完整观感, 其余令牌仍在高级视图. */
typedef struct UiThemeBasicDesc {
	const char *key;
	const char *label;
	const char *group;
} UiThemeBasicDesc;

static const UiThemeBasicDesc basic_table[UI_THEME_BASIC_COUNT] = {
	{ "page",            "窗口背景",   "外观" },
	{ "card",            "卡片底色",   "外观" },
	{ "border",          "边框/分隔",  "外观" },
	{ "text",            "正文",       "文字" },
	{ "text2",           "次级文字",   "文字" },
	{ "text3",           "辅助文字",   "文字" },
	{ "blue",            "主色",       "交互" },
	{ "btnBg",           "次级按钮",   "交互" },
	{ "rowOn",           "选中行底色", "交互" },
	{ "inputBg",         "输入框底色", "交互" },
	{ "btnPrimary",      "主按钮",     "交互" },
	{ "btnPrimaryText",  "主按钮文字", "交互" },
	{ "windowRadius",    "窗口圆角",   "几何" },
	{ "panelRadius",     "面板圆角",   "几何" },
	{ "buttonRadius",    "按钮圆角",   "几何" }
};

static int field_index_by_key(const char *key)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		if (lstrcmpA(key, UiTheme_TokenTable[i].key) == 0) return i;
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		if (lstrcmpA(key, UiTheme_RadiusTable[i].key) == 0)
			return UI_THEME_TOKEN_COUNT + i;
	return -1;
}

static int basic_fields[UI_THEME_BASIC_COUNT];
static int basic_ready;

static void basic_prepare(void)
{
	int i;
	if (basic_ready) return;
	for (i = 0; i < UI_THEME_BASIC_COUNT; i++)
		basic_fields[i] = field_index_by_key(basic_table[i].key);
	basic_ready = 1;
}

int UiTheme_BasicField(int nth)
{
	if (nth < 0 || nth >= UI_THEME_BASIC_COUNT) return -1;
	basic_prepare();
	return basic_fields[nth];
}

int UiTheme_IsBasicField(int field)
{
	basic_prepare();
	return UiTheme_FieldLabel(field) != NULL;
}

const char *UiTheme_FieldLabel(int field)
{
	int i;
	if (field < 0 || field >= UI_THEME_FIELD_COUNT) return NULL;
	basic_prepare();
	for (i = 0; i < UI_THEME_BASIC_COUNT; i++)
		if (basic_fields[i] == field) return basic_table[i].label;
	return NULL;
}

const char *UiTheme_FieldGroup(int field)
{
	int i;
	if (field < 0 || field >= UI_THEME_FIELD_COUNT) return NULL;
	basic_prepare();
	for (i = 0; i < UI_THEME_BASIC_COUNT; i++)
		if (basic_fields[i] == field) return basic_table[i].group;
	return NULL;
}

/* ── 读写 ─────────────────────────────────────────────────────────── */

void UiThemeSet_Read(UiThemeSet *set, const UiTheme *t)
{
	int i;
	if (!set || !t) return;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		set->color[i] = *(const UiColor *)((const char *)t +
			UiTheme_TokenTable[i].offset);
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		set->radius[i] = *(const float *)((const char *)t +
			UiTheme_RadiusTable[i].offset);
}

void UiThemeSet_Apply(UiTheme *t, UiThemeId id, const UiThemeSet *set)
{
	int i;
	if (!t || !set) return;
	t->themeId = id;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		*(UiColor *)((char *)t + UiTheme_TokenTable[i].offset) = set->color[i];
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		*(float *)((char *)t + UiTheme_RadiusTable[i].offset) = set->radius[i];
	UiTheme_DeriveRoles(t);
}

int UiThemeSet_Equals(const UiThemeSet *a, const UiThemeSet *b)
{
	int i;
	if (!a || !b) return 0;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		if (a->color[i] != b->color[i]) return 0;
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		if (a->radius[i] != b->radius[i]) return 0;
	return 1;
}

int UiThemeSet_FieldModified(const UiThemeSet *set, const UiThemeSet *base,
	int index)
{
	if (!set || !base || index < 0 || index >= UI_THEME_FIELD_COUNT) return 0;
	if (index < UI_THEME_TOKEN_COUNT)
		return set->color[index] != base->color[index];
	return set->radius[index - UI_THEME_TOKEN_COUNT] !=
		base->radius[index - UI_THEME_TOKEN_COUNT];
}

/* ── 文本工具 ─────────────────────────────────────────────────────── */

typedef struct Sb {
	char *buf;
	int cap;
	int len;
	int trunc;
} Sb;

static void sb_put(Sb *s, const char *text)
{
	while (*text) {
		if (s->len + 1 >= s->cap) { s->trunc = 1; return; }
		s->buf[s->len++] = *text++;
	}
}

static void sb_fmt(Sb *s, const char *fmt, ...);

static void sb_fmt(Sb *s, const char *fmt, ...)
{
	va_list ap;
	char tmp[256];
	int n;
	va_start(ap, fmt);
	n = wvsprintfA(tmp, fmt, ap);
	va_end(ap);
	if (n > 0) {
		tmp[n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1] = 0;
		sb_put(s, tmp);
	}
}

static int sb_done(Sb *s)
{
	if (s->len >= s->cap) { s->buf[s->cap - 1] = 0; return -1; }
	s->buf[s->len] = 0;
	return s->trunc ? -1 : s->len;
}

static float parse_radius(const char *s, int *ok)
{
	int sign = 1, ip = 0, frac = 0, div = 1, any = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { sign = -1; s++; }
	while (*s >= '0' && *s <= '9') { ip = ip * 10 + (*s - '0'); s++; any = 1; }
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9' && div < 1000) {
			frac = frac * 10 + (*s - '0'); div *= 10; s++; any = 1;
		}
	}
	while (*s == ' ' || *s == '\t' || *s == '\r') s++;
	*ok = any && *s == 0;
	return (float)sign * ((float)ip + (float)frac / (float)div);
}

static void format_radius(char *buf, int cap, float v)
{
	int i = (int)v;
	if (v == (float)i)
		wsprintfA(buf, "%d.0f", i);
	else {
		int t = (int)(v * 10.0f + 0.5f);
		wsprintfA(buf, "%d.%df", t / 10, t % 10);
	}
	(void)cap;
}

int UiTheme_FormatHex(UiColor c, char *buf, int cap)
{
	int n;
	if (cap < 7) {
		if (cap > 0) buf[0] = 0;
		return 0;
	}
	if (((c >> 24) & 0xFF) == 0xFF)
		n = wsprintfA(buf, "%02X%02X%02X",
			(int)((c >> 16) & 0xFF), (int)((c >> 8) & 0xFF), (int)(c & 0xFF));
	else
		n = wsprintfA(buf, "%02X%02X%02X%02X",
			(int)((c >> 24) & 0xFF), (int)((c >> 16) & 0xFF),
			(int)((c >> 8) & 0xFF), (int)(c & 0xFF));
	return n;
}

static int hex_val(int ch)
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	return -1;
}

int UiTheme_ParseHex(const char *s, UiColor *out)
{
	unsigned v = 0;
	int n = 0, d;
	if (!s || !out) return 0;
	while (*s == ' ' || *s == '\t') s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	while ((d = hex_val((unsigned char)*s)) >= 0) {
		v = (v << 4) | (unsigned)d;
		n++;
		if (n > 8) return 0;
		s++;
	}
	while (*s == ' ' || *s == '\t' || *s == '\r') s++;
	if (*s != 0 || (n != 6 && n != 8)) return 0;
	if (n == 6) v |= 0xFF000000u;
	*out = (UiColor)v;
	return 1;
}

/* ── 工程文件 ─────────────────────────────────────────────────────── */

static void save_section(Sb *s, const char *name, const UiThemeSet *set)
{
	char hex[16], rad[24];
	int i;
	sb_fmt(s, "[%s]\n", name);
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
		UiTheme_FormatHex(set->color[i], hex, (int)sizeof(hex));
		sb_fmt(s, "%s=%s\n", UiTheme_TokenTable[i].key, hex);
	}
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++) {
		format_radius(rad, (int)sizeof(rad), set->radius[i]);
		/* 工程文件里去掉 C 浮点后缀 */
		{
			int n = 0;
			while (rad[n] && rad[n] != 'f') n++;
			rad[n] = 0;
		}
		sb_fmt(s, "%s=%s\n", UiTheme_RadiusTable[i].key, rad);
	}
}

int UiThemeSet_Save(const UiThemeSet *light, const UiThemeSet *dark,
	char *buf, int cap)
{
	Sb s;
	if (!light || !dark || !buf || cap <= 0) return -1;
	s.buf = buf; s.cap = cap; s.len = 0; s.trunc = 0;
	sb_put(&s, "cui-theme 1\n");
	save_section(&s, "light", light);
	save_section(&s, "dark", dark);
	return sb_done(&s);
}

static int key_is(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == 0 && *b == 0;
}

int UiThemeSet_Load(UiThemeSet *light, UiThemeSet *dark, const char *text)
{
	const char *p = text;
	int section = -1, header = 0;
	char line[192];

	if (!light || !dark || !text) return 0;
	while (*p) {
		int n = 0;
		while (*p && *p != '\n' && n < (int)sizeof(line) - 1) {
			if (*p != '\r') line[n++] = *p;
			p++;
		}
		line[n] = 0;
		while (*p && *p != '\n') p++;
		if (*p == '\n') p++;
		if (n == 0) continue;
		if (!header) {
			if (line[0] == 'c' && key_is(line, "cui-theme 1")) header = 1;
			else return 0;
			continue;
		}
		if (line[0] == '[') {
			if (key_is(line, "[light]")) section = 0;
			else if (key_is(line, "[dark]")) section = 1;
			else section = -1;
			continue;
		}
		if (section >= 0) {
			char key[96], val[96];
			int k = 0, v = 0;
			char *eq;
			UiThemeSet *set = section == 0 ? light : dark;
			eq = line;
			while (*eq && *eq != '=') eq++;
			if (!*eq) continue;
			k = (int)(eq - line);
			if (k >= (int)sizeof(key)) k = (int)sizeof(key) - 1;
			for (v = 0; v < k; v++) key[v] = line[v];
			key[k] = 0;
			eq++;
			while (*eq == ' ' || *eq == '\t') eq++;
			lstrcpynA(val, eq, (int)sizeof(val));
			{
				int i, done = 0;
				for (i = 0; i < UI_THEME_TOKEN_COUNT; i++) {
					if (key_is(key, UiTheme_TokenTable[i].key)) {
						UiColor c;
						if (UiTheme_ParseHex(val, &c)) set->color[i] = c;
						done = 1;
						break;
					}
				}
				if (!done) {
					for (i = 0; i < UI_THEME_RADIUS_COUNT; i++) {
						if (key_is(key, UiTheme_RadiusTable[i].key)) {
							int ok = 0;
							float r = parse_radius(val, &ok);
							if (ok) set->radius[i] = r;
							break;
						}
					}
				}
			}
		}
	}
	return header;
}

/* ── 代码生成 ─────────────────────────────────────────────────────── */

static void codegen_colors(Sb *s, const UiThemeSet *set, const char *indent)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		sb_fmt(s, "%st->%s = 0x%08X;\n", indent,
			UiTheme_TokenTable[i].key, (unsigned)set->color[i]);
}

static void codegen_radii(Sb *s, const UiThemeSet *set, const char *indent)
{
	int i;
	char rad[24];
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++) {
		format_radius(rad, (int)sizeof(rad), set->radius[i]);
		sb_fmt(s, "%st->%s = %s;\n", indent,
			UiTheme_RadiusTable[i].key, rad);
	}
}

int UiThemeSet_Codegen(const UiThemeSet *light, const UiThemeSet *dark,
	char *buf, int cap, const char *stamp)
{
	Sb s;
	if (!light || !dark || !buf || cap <= 0) return -1;
	s.buf = buf; s.cap = cap; s.len = 0; s.trunc = 0;
	sb_fmt(&s, "/* generated by cui_theme_editor %s */\n",
		(stamp && stamp[0]) ? stamp : "manual");
	sb_put(&s, "static void theme_apply_preset(UiTheme *t, UiThemeId id)\n{\n");
	sb_put(&s, "\tt->themeId = id;\n");
	sb_put(&s, "\n\tif (id == UI_THEME_DARK) {\n");
	codegen_colors(&s, dark, "\t\t");
	codegen_radii(&s, dark, "\t\t");
	sb_put(&s, "\t\treturn;\n\t}\n\n");
	codegen_colors(&s, light, "\t");
	codegen_radii(&s, light, "\t");
	sb_put(&s, "}\n");
	return sb_done(&s);
}

/* ── 对比度 ───────────────────────────────────────────────────────── */

static UiColor over_opaque(UiColor fg, UiColor bg)
{
	int a = (int)((fg >> 24) & 0xFF);
	int r = (int)((fg >> 16) & 0xFF);
	int g = (int)((fg >> 8) & 0xFF);
	int b = (int)(fg & 0xFF);
	int br = (int)((bg >> 16) & 0xFF);
	int bgc = (int)((bg >> 8) & 0xFF);
	int bb = (int)(bg & 0xFF);
	r = (r * a + br * (255 - a) + 127) / 255;
	g = (g * a + bgc * (255 - a) + 127) / 255;
	b = (b * a + bb * (255 - a) + 127) / 255;
	return 0xFF000000u | ((UiColor)r << 16) | ((UiColor)g << 8) | (UiColor)b;
}

static float luma01(UiColor c)
{
	int r = (int)((c >> 16) & 0xFF);
	int g = (int)((c >> 8) & 0xFF);
	int b = (int)(c & 0xFF);
	return (float)(r * 299 + g * 587 + b * 114) / 255000.0f;
}

float UiTheme_Contrast(UiColor fg, UiColor bg)
{
	UiColor base = over_opaque(bg, 0xFFFFFFFFu);
	UiColor front = over_opaque(fg, base);
	float a = luma01(front);
	float b = luma01(base);
	if (a < b) { float t = a; a = b; b = t; }
	return (a + 0.05f) / (b + 0.05f);
}

/* ── 随机配色生成 ─────────────────────────────────────────────────── */

/* 与 UiTheme_TokenTable 顺序一一对应 */
enum {
	TG_PAGE = 0, TG_PAGE2, TG_CARD, TG_BORDER, TG_FRAME, TG_FRAMEHI,
	TG_TEXT, TG_TEXT2, TG_TEXT3, TG_TEXT4,
	TG_GREEN, TG_GOLD, TG_RED, TG_ORANGE, TG_BLUE, TG_BLUE2,
	TG_BTNBG, TG_SWITCHON, TG_SWITCHOFF,
	TG_PANELTOP, TG_PANELBOTTOM, TG_HDRLINE, TG_TAB1, TG_TAB2,
	TG_BTN1, TG_BTN2, TG_ICONBLUE, TG_PANELTEXT, TG_PANELDISABLED,
	TG_TILE, TG_PILL, TG_POWER,
	TG_CONTROLBOX, TG_CONTROLBORDER, TG_ROWON, TG_ROWONBORDER,
	TG_ROWOFF, TG_ROWOFFBORDER, TG_PAGELINE,
	TG_INPUTBG, TG_INPUTBORDER, TG_INPUTFOCUS, TG_HINT,
	TG_DANGER, TG_DANGERBG, TG_OKBG,
	TG_PANELBG, TG_PANELBORDER,
	TG_BTNPRIMARY, TG_BTNPRIMARY2, TG_BTNPRIMARYHOVER,
	TG_BTNPRIMARYPRESS, TG_BTNPRIMARYTEXT, TG_BTNPRIMARYBORDER,
	TG_BTNHOVERBG, TG_BTNPRESSBG, TG_BTNTEXT, TG_BTNBORDER,
	TG_BTNHOVERBORDER, TG_BTNDISABLEDBG, TG_BTNDISABLEDTEXT,
	TG_PROGRESSTRACK, TG_PROGRESSFILL, TG_PROGRESSFILL2,
	TG_TOASTBG, TG_TOASTBORDER, TG_TOASTTEXT, TG_TOASTOVERLAY,
	TG_COUNT
};

#define TG_NEU 0
#define TG_PRI 1
#define TG_ACC 2
#define TG_SUC 3
#define TG_WRN 4
#define TG_DAN 5
#define TG_PTX 6

static const unsigned char tg_family[UI_THEME_TOKEN_COUNT] = {
	TG_NEU, TG_NEU, TG_NEU, TG_NEU, TG_NEU, TG_NEU,
	TG_NEU, TG_NEU, TG_NEU, TG_NEU,
	TG_SUC, TG_ACC, TG_DAN, TG_WRN, TG_PRI, TG_PRI,
	TG_NEU, TG_PRI, TG_NEU,
	TG_NEU, TG_NEU, TG_NEU, TG_ACC, TG_PRI, TG_ACC, TG_PRI,
	TG_PRI, TG_NEU, TG_NEU, TG_NEU, TG_NEU, TG_PRI,
	TG_NEU, TG_NEU, TG_PRI, TG_PRI, TG_NEU, TG_NEU, TG_NEU,
	TG_NEU, TG_NEU, TG_PRI, TG_NEU,
	TG_DAN, TG_DAN, TG_SUC,
	TG_NEU, TG_NEU,
	TG_PRI, TG_PRI, TG_PRI, TG_PRI, TG_PTX, TG_PRI,
	TG_NEU, TG_NEU, TG_NEU, TG_NEU, TG_NEU, TG_NEU, TG_NEU,
	TG_NEU, TG_PRI, TG_PRI,
	TG_NEU, TG_NEU, TG_NEU, TG_NEU
};

typedef struct TgRng {
	unsigned s;
} TgRng;

static unsigned tg_u32(TgRng *r)
{
	unsigned x = r->s;
	if (!x) x = 0x6D2B79F5u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	r->s = x;
	return x;
}

static float tg_f01(TgRng *r)
{
	return (float)(tg_u32(r) >> 8) / 16777216.0f;
}

static float tg_range(TgRng *r, float lo, float hi)
{
	return lo + (hi - lo) * tg_f01(r);
}

static void tg_rgb_hsv(UiColor c, float *h, float *s, float *v)
{
	float r = (float)((c >> 16) & 0xFF) / 255.0f;
	float g = (float)((c >> 8) & 0xFF) / 255.0f;
	float b = (float)(c & 0xFF) / 255.0f;
	float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
	float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
	float d = mx - mn;
	*h = 0.0f;
	if (d > 0.0f) {
		if (mx == r) *h = (g - b) / d;
		else if (mx == g) *h = (b - r) / d + 2.0f;
		else *h = (r - g) / d + 4.0f;
		*h *= 60.0f;
		if (*h < 0.0f) *h += 360.0f;
	}
	*s = mx > 0.0f ? d / mx : 0.0f;
	*v = mx;
}

static UiColor tg_hsv_rgb(float h, float s, float v)
{
	int i, r, g, b;
	float f, p, q, t;
	while (h < 0.0f) h += 360.0f;
	while (h >= 360.0f) h -= 360.0f;
	if (s < 0.0f) s = 0.0f;
	if (s > 1.0f) s = 1.0f;
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	h /= 60.0f;
	i = (int)h;
	f = h - (float)i;
	p = v * (1.0f - s);
	q = v * (1.0f - s * f);
	t = v * (1.0f - s * (1.0f - f));
	switch (i) {
	case 0: r = (int)(v * 255.0f + 0.5f); g = (int)(t * 255.0f + 0.5f); b = (int)(p * 255.0f + 0.5f); break;
	case 1: r = (int)(q * 255.0f + 0.5f); g = (int)(v * 255.0f + 0.5f); b = (int)(p * 255.0f + 0.5f); break;
	case 2: r = (int)(p * 255.0f + 0.5f); g = (int)(v * 255.0f + 0.5f); b = (int)(t * 255.0f + 0.5f); break;
	case 3: r = (int)(p * 255.0f + 0.5f); g = (int)(q * 255.0f + 0.5f); b = (int)(v * 255.0f + 0.5f); break;
	case 4: r = (int)(t * 255.0f + 0.5f); g = (int)(p * 255.0f + 0.5f); b = (int)(v * 255.0f + 0.5f); break;
	default: r = (int)(v * 255.0f + 0.5f); g = (int)(p * 255.0f + 0.5f); b = (int)(q * 255.0f + 0.5f); break;
	}
	return 0xFF000000u | ((UiColor)r << 16) | ((UiColor)g << 8) | (UiColor)b;
}

static float tg_hue_dist(float a, float b)
{
	float d = a - b;
	while (d < -180.0f) d += 360.0f;
	while (d > 180.0f) d -= 360.0f;
	return d < 0.0f ? -d : d;
}

/* 色相避开语义色; 用固定步长逐次跨过禁区, 保持确定性 */
static float tg_hue_clear(float h, const float *bad, const float *minD, int n,
	float step)
{
	int k, i;
	while (h < 0.0f) h += 360.0f;
	while (h >= 360.0f) h -= 360.0f;
	for (k = 0; k < 8; k++) {
		int ok = 1;
		for (i = 0; i < n; i++) {
			if (tg_hue_dist(h, bad[i]) < minD[i]) { ok = 0; break; }
		}
		if (ok) return h;
		h += step;
		while (h >= 360.0f) h -= 360.0f;
	}
	return h;
}

static float tg_hue_of(const UiThemeSet *set, int idx, float fallback)
{
	float h, s, v;
	tg_rgb_hsv(set->color[idx], &h, &s, &v);
	return s < 0.02f ? fallback : h;
}

typedef struct TgStyle {
	float h0, h1;
	float delta;
	float neutralScale, tint;
	float chromaScale, semScale, valueScale;
} TgStyle;

static UiColor tg_luma_fix(UiColor src, UiColor out)
{
	float want = luma01(src);
	float got = luma01(out);
	int r, g, b;
	float scale;
	if (want < 0.002f || want > 0.998f || got < 0.002f) return out;
	scale = want / got;
	if (scale > 0.96f && scale < 1.04f) return out;
	r = (int)((float)((out >> 16) & 0xFF) * scale + 0.5f);
	g = (int)((float)((out >> 8) & 0xFF) * scale + 0.5f);
	b = (int)((float)(out & 0xFF) * scale + 0.5f);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (out & 0xFF000000u) | ((UiColor)r << 16) | ((UiColor)g << 8) |
		(UiColor)b;
}

/* 主色/强调不超过模板亮度: 只向下压, 避免加亮造成截断偏色与白字对比不足 */
static UiColor tg_luma_cap(UiColor src, UiColor out, float hi)
{
	float base = luma01(src);
	float got = luma01(out);
	float want;
	int r, g, b;
	float scale;
	if (base < 0.01f || got < 0.002f) return out;
	want = base * hi;
	if (got <= want) return out;
	scale = want / got;
	r = (int)((float)((out >> 16) & 0xFF) * scale + 0.5f);
	g = (int)((float)((out >> 8) & 0xFF) * scale + 0.5f);
	b = (int)((float)(out & 0xFF) * scale + 0.5f);
	return (out & 0xFF000000u) | ((UiColor)r << 16) | ((UiColor)g << 8) |
		(UiColor)b;
}

static UiColor tg_transform(UiColor src, int fam, int idx, const TgStyle *st)
{
	unsigned a = src & 0xFF000000u;
	float h, s, v, h2, s2, v2;
	UiColor out;
	tg_rgb_hsv(src, &h, &s, &v);
	h2 = h;
	s2 = s;
	v2 = v;
	switch (fam) {
	case TG_NEU:
		h2 = st->h0;
		s2 = s * st->neutralScale + st->tint;
		break;
	case TG_PRI:
		h2 = h + st->delta;
		s2 = s * st->chromaScale + 0.012f;
		v2 = v * st->valueScale;
		break;
	case TG_ACC:
		h2 = idx == TG_GOLD ? st->h1 + 22.0f : st->h1;
		s2 = s * st->chromaScale + 0.01f;
		break;
	default:
		/* 语义色只做微调, 保持绿/橙/红的识别度 */
		h2 = h + ((float)((idx * 37) % 11) - 5.0f) * 0.7f;
		s2 = s * st->semScale;
		break;
	}
	out = (tg_hsv_rgb(h2, s2, v2) & 0x00FFFFFFu) | a;
	/* 中性族承载绝大部分文字, 重新着色后把亮度拉回原值, 保住对比度 */
	if (fam == TG_NEU || fam == TG_SUC || fam == TG_WRN || fam == TG_DAN)
		out = tg_luma_fix(src, out);
	else if (fam == TG_PRI)
		out = tg_luma_cap(src, out, 1.0f);
	else if (fam == TG_ACC)
		out = tg_luma_cap(src, out, 1.05f);
	return out;
}

static unsigned tg_lock_bit(int fam)
{
	switch (fam) {
	case TG_NEU: return UI_THEME_LOCK_NEUTRAL;
	case TG_PRI:
	case TG_ACC:
	case TG_PTX: return UI_THEME_LOCK_PRIMARY;
	default: return UI_THEME_LOCK_SEMANTIC;
	}
}

static void tg_copy_family(UiThemeSet *out, const UiThemeSet *src, int fam)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		if (tg_family[i] == fam) out->color[i] = src->color[i];
}

static UiColor tg_pick_primary_text(const UiThemeSet *set,
	const UiThemeSet *tpl)
{
	static const int fills[4] = {
		TG_BTNPRIMARY, TG_BTNPRIMARY2, TG_BTNPRIMARYHOVER, TG_BTNPRIMARYPRESS
	};
	float cw = 100.0f, cd = 100.0f;
	int i, white = 1;
	for (i = 0; i < 4; i++) {
		float a = UiTheme_Contrast(0xFFFFFFFFu, set->color[fills[i]]);
		float b = UiTheme_Contrast(0xFF0E1218u, set->color[fills[i]]);
		if (a < cw) cw = a;
		if (b < cd) cd = b;
	}
	/* 优先沿用模板极性; 亮底(黄/绿)白字不够时才翻成深色字 */
	if (luma01(tpl->color[TG_BTNPRIMARYTEXT]) > 0.5f) {
		if (cw >= 1.55f) white = 1;
		else if (cd >= 1.55f) white = 0;
		else white = cw >= cd;
	} else {
		if (cd >= 1.55f) white = 0;
		else if (cw >= 1.55f) white = 1;
		else white = cd >= cw;
	}
	return white ? 0xFFFFFFFFu : 0xFF0E1218u;
}

typedef struct TgPair {
	signed char fg, bg;
	float floor;
} TgPair;

static const TgPair tg_pairs[] = {
	{ TG_TEXT, TG_PAGE, 4.40f },
	{ TG_TEXT, TG_CARD, 4.40f },
	{ TG_TEXT2, TG_CARD, 2.30f },
	{ TG_TEXT3, TG_CARD, 1.55f },
	{ TG_BTNTEXT, TG_BTNBG, 2.30f },
	{ TG_HINT, TG_INPUTBG, 1.25f },
	{ TG_TOASTTEXT, TG_TOASTBG, 3.40f },
	{ TG_PANELTEXT, TG_PANELBG, 1.90f },
	{ TG_BTNPRIMARYTEXT, TG_BTNPRIMARY, 1.38f },
	{ TG_BTNPRIMARYTEXT, TG_BTNPRIMARY2, 1.38f },
	{ TG_BTNPRIMARYTEXT, TG_BTNPRIMARYHOVER, 1.38f },
	{ TG_BTNPRIMARYTEXT, TG_BTNPRIMARYPRESS, 1.30f }
};

static int tg_pair_ok(const UiThemeSet *cand, const UiThemeSet *tpl,
	const TgPair *p, unsigned locks)
{
	unsigned needed = tg_lock_bit(tg_family[(int)p->fg]) |
		tg_lock_bit(tg_family[(int)p->bg]);
	float tc, cc, need;
	if ((locks & needed) == needed) return 1;
	tc = UiTheme_Contrast(tpl->color[(int)p->fg], tpl->color[(int)p->bg]);
	cc = UiTheme_Contrast(cand->color[(int)p->fg], cand->color[(int)p->bg]);
	need = tc * 0.90f;
	if (need < p->floor) need = p->floor;
	return cc + 0.015f >= need;
}

static int tg_validate(const UiThemeSet *cand, const UiThemeSet *tpl,
	unsigned locks)
{
	int i;
	for (i = 0; i < (int)(sizeof(tg_pairs) / sizeof(tg_pairs[0])); i++)
		if (!tg_pair_ok(cand, tpl, &tg_pairs[i], locks)) return 0;
	return 1;
}

static void tg_build(UiThemeSet *out, const UiThemeSet *tpl, const TgStyle *st)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		out->color[i] = tg_transform(tpl->color[i], tg_family[i], i, st);
}

unsigned UiThemeGen_Palette(UiThemeSet *light, UiThemeSet *dark,
	const UiThemeGenOpts *opts)
{
	UiThemeSet candL, candD;
	unsigned base, locks, attempt;
	const UiThemeSet *tplL, *tplD, *keepL, *keepD;
	float refDanger, refWarn, refSucc, baseHue;
	int i;

	if (!light || !dark || !opts || !opts->templateLight ||
		!opts->templateDark)
		return 0;
	tplL = opts->templateLight;
	tplD = opts->templateDark;
	keepL = opts->keepLight ? opts->keepLight : tplL;
	keepD = opts->keepDark ? opts->keepDark : tplD;
	locks = opts->locks & (UI_THEME_LOCK_PRIMARY | UI_THEME_LOCK_NEUTRAL |
		UI_THEME_LOCK_SEMANTIC);
	base = opts->seed ? opts->seed : 0x1A2B3C4Du;

	{
		const UiThemeSet *ref = (locks & UI_THEME_LOCK_SEMANTIC) ? keepL : tplL;
		refDanger = tg_hue_of(ref, TG_RED, 352.0f);
		refWarn = tg_hue_of(ref, TG_ORANGE, 36.0f);
		refSucc = tg_hue_of(ref, TG_GREEN, 148.0f);
	}
	baseHue = tg_hue_of(tplL, TG_BLUE, 228.0f);

	for (attempt = 0; attempt < 6; attempt++) {
		TgStyle st;
		TgRng rng;
		unsigned s = base + attempt * 0x9E3779B9u;
		float bad[4], minD[4];
		int scheme;
		if (!s) s = 0xA341316Cu;
		rng.s = s;
		/* 小种子(1,2,3...)的首个 xorshift 值都接近 0, 先预热 */
		tg_u32(&rng); tg_u32(&rng); tg_u32(&rng);
		tg_u32(&rng); tg_u32(&rng); tg_u32(&rng);

		if (locks & UI_THEME_LOCK_PRIMARY) {
			st.h0 = tg_hue_of(keepL, TG_BLUE, baseHue);
		} else {
			float h = tg_range(&rng, 0.0f, 360.0f);
			float step = tg_range(&rng, 43.0f, 91.0f);
			bad[0] = refDanger; minD[0] = 38.0f;
			bad[1] = refWarn; minD[1] = 30.0f;
			bad[2] = refSucc; minD[2] = 28.0f;
			st.h0 = tg_hue_clear(h, bad, minD, 3, step);
		}
		st.delta = st.h0 - baseHue;
		while (st.delta > 180.0f) st.delta -= 360.0f;
		while (st.delta < -180.0f) st.delta += 360.0f;

		scheme = (int)(tg_f01(&rng) * 4.0f);
		if (scheme > 3) scheme = 3;
		switch (scheme) {
		case 0: st.h1 = st.h0 + tg_range(&rng, 26.0f, 60.0f); break;
		case 1: st.h1 = st.h0 + tg_range(&rng, 170.0f, 190.0f); break;
		case 2: st.h1 = st.h0 + tg_range(&rng, 115.0f, 130.0f); break;
		default: st.h1 = st.h0 + tg_range(&rng, 145.0f, 165.0f); break;
		}
		bad[0] = refDanger; minD[0] = 22.0f;
		bad[1] = refWarn; minD[1] = 20.0f;
		bad[2] = refSucc; minD[2] = 22.0f;
		bad[3] = st.h0; minD[3] = 22.0f;
		st.h1 = tg_hue_clear(st.h1, bad, minD, 4,
			tg_range(&rng, 31.0f, 73.0f));

		st.neutralScale = tg_range(&rng, 0.55f, 1.05f);
		st.tint = tg_range(&rng, 0.006f, 0.030f);
		st.chromaScale = tg_range(&rng, 0.82f, 1.14f);
		st.semScale = tg_range(&rng, 0.88f, 1.08f);
		st.valueScale = tg_range(&rng, 0.97f, 1.03f);

		tg_build(&candL, tplL, &st);
		tg_build(&candD, tplD, &st);
		candL.color[TG_BTNPRIMARYTEXT] = tg_pick_primary_text(&candL, tplL);
		candD.color[TG_BTNPRIMARYTEXT] = tg_pick_primary_text(&candD, tplD);

		if (locks & UI_THEME_LOCK_PRIMARY) {
			tg_copy_family(&candL, keepL, TG_PRI);
			tg_copy_family(&candD, keepD, TG_PRI);
			tg_copy_family(&candL, keepL, TG_ACC);
			tg_copy_family(&candD, keepD, TG_ACC);
			tg_copy_family(&candL, keepL, TG_PTX);
			tg_copy_family(&candD, keepD, TG_PTX);
		}
		if (locks & UI_THEME_LOCK_NEUTRAL) {
			tg_copy_family(&candL, keepL, TG_NEU);
			tg_copy_family(&candD, keepD, TG_NEU);
		}
		if (locks & UI_THEME_LOCK_SEMANTIC) {
			tg_copy_family(&candL, keepL, TG_SUC);
			tg_copy_family(&candD, keepD, TG_SUC);
			tg_copy_family(&candL, keepL, TG_WRN);
			tg_copy_family(&candD, keepD, TG_WRN);
			tg_copy_family(&candL, keepL, TG_DAN);
			tg_copy_family(&candD, keepD, TG_DAN);
		}

		for (i = 0; i < UI_THEME_RADIUS_COUNT; i++) {
			candL.radius[i] = keepL->radius[i];
			candD.radius[i] = keepD->radius[i];
		}

		if (locks == (UI_THEME_LOCK_PRIMARY | UI_THEME_LOCK_NEUTRAL |
			UI_THEME_LOCK_SEMANTIC) ||
			(tg_validate(&candL, tplL, locks) &&
			 tg_validate(&candD, tplD, locks))) {
			*light = candL;
			*dark = candD;
			return s;
		}
	}
	return 0;
}
