/*
 * theme_editor.c - CUI 主题编辑器
 *
 * 编辑 Light / Dark 两份全量令牌 (68 色 + 5 圆角), 实时预览; 启动恢复上次工程,
 * 导出可整段替换 ui_theme.c 中 theme_apply_preset 的 C 代码 (文件/剪贴板).
 */
#include <windows.h>
#include <commdlg.h>
#include "ui/ui_app.h"
#include "ui/ui_controls.h"
#include "ui/ui_icon.h"
#include "ui/ui_input.h"
#include "ui/ui_pagination.h"
#include "ui/ui_scroll.h"
#include "ui/ui_theme_edit.h"
#include "ui/ui_widgets.h"
#include "edit.h"

#define TE_LEFT_W   336.0f
#define TE_CENTER_W 340.0f
#define TE_GAP      14.0f
#define TE_PREVIEW_HITS 64
#define TE_DRAW_HITS 32
#define TE_DRAW_CARDS 3

enum {
	TE_DRAG_NONE = 0,
	TE_DRAG_SV,
	TE_DRAG_HUE,
	TE_DRAG_ALPHA
};

enum {
	TE_DRAW_NONE = 0,
	TE_DRAW_CLOSE,
	TE_DRAW_REROLL,
	TE_DRAW_UNDO,
	TE_DRAW_LOCK,
	TE_DRAW_USE
};

typedef struct TePreviewHit {
	UiRect rc;
	int field;
} TePreviewHit;

typedef struct TeDrawHit {
	UiRect rc;
	int action;
	int arg;
} TeDrawHit;

typedef struct ThemeEditor {
	UiApp *app;
	UiThemeSet light, dark;
	UiThemeSet baseLight, baseDark;
	int mode;                    /* 0 = Light, 1 = Dark */
	int sel;                     /* 组合表索引: 0..67 色, 68..72 圆角 */
	int basicMode;               /* 默认只显示 15 个基础令牌 */
	int revealSel;               /* 下一帧把选中项滚进列表视口 */

	char search[64];
	int onlyModified;
	UiInput form;
	int fSearch, fHex;

	UiScrollView listSv, prevSv;
	int rowMap[UI_THEME_FIELD_COUNT];
	float rowY[UI_THEME_FIELD_COUNT];
	int  rowCount;
	float rowTotal;

	TePreviewHit hits[TE_PREVIEW_HITS];
	int hitCount;
	int hoverField;
	int pressedField;

	int drawOpen;
	unsigned drawLocks;
	unsigned drawNonce;
	unsigned drawSeed;
	UiThemeSet drawLight[TE_DRAW_CARDS], drawDark[TE_DRAW_CARDS];
	unsigned drawCardSeed[TE_DRAW_CARDS];
	int drawValid[TE_DRAW_CARDS];
	UiThemeSet undoLight, undoDark;
	int undoValid;
	TeDrawHit drawHits[TE_DRAW_HITS];
	int drawHitCount;
	int drawHover, drawHoverArg;
	int drawPress, drawPressArg;
	UiRect drawPanel;

	float hue, sat, val;
	int hsvDirty;
	int radiusDirty;
	int dragKind;

	UiSwitch sw;
	UiCheckbox ckMod, ckPrev;
	UiSlider slRadius, slPrev;
	UiPagination pg;

	char status[256];
	char hexText[12];
	WCHAR palettePath[MAX_PATH];
	char paletteText[16384];
	char gen[16384];
} ThemeEditor;

static ThemeEditor g_te;

/* ── 小工具 ───────────────────────────────────────────────────────── */

static int te_strlen(const char *s)
{
	int n = 0;
	if (s) while (s[n]) n++;
	return n;
}

static int te_lower(int ch)
{
	return (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
}

static int te_contains(const char *hay, const char *needle)
{
	int i, j;
	if (!needle[0]) return 1;
	for (i = 0; hay[i]; i++) {
		for (j = 0; needle[j]; j++) {
			if (!hay[i + j] || te_lower(hay[i + j]) != te_lower(needle[j]))
				break;
		}
		if (!needle[j]) return 1;
	}
	return 0;
}

static float te_clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static int te_absint(int v)
{
	return v < 0 ? -v : v;
}

static void set_status(ThemeEditor *te, const char *text)
{
	lstrcpynA(te->status, text, (int)sizeof(te->status));
}

/* wsprintf 不支持浮点, 自己按固定小数位格式化 */
static void fmt_fixed(char *out, int cap, float v, int dec)
{
	int mul = 1, i, n = 0, whole, frac, neg = v < 0.0f;
	char tmp[16];
	int t = 0;
	if (cap <= 1) { if (cap > 0) out[0] = 0; return; }
	if (neg) v = -v;
	for (i = 0; i < dec; i++) mul *= 10;
	whole = (int)v;
	frac = (int)((v - (float)whole) * (float)mul + 0.5f);
	if (frac >= mul) { frac -= mul; whole++; }
	if (neg && (whole || frac)) out[n++] = '-';
	if (whole == 0) tmp[t++] = '0';
	while (whole > 0 && t < (int)sizeof(tmp)) {
		tmp[t++] = (char)('0' + whole % 10);
		whole /= 10;
	}
	while (t > 0 && n < cap - 1) out[n++] = tmp[--t];
	if (dec > 0 && n < cap - 1) {
		out[n++] = '.';
		for (i = dec - 1; i >= 0 && n < cap - 1; i--) {
			int div = 1, j;
			for (j = 0; j < i; j++) div *= 10;
			out[n++] = (char)('0' + (frac / div) % 10);
		}
	}
	out[n] = 0;
}

static UiThemeSet *active_set(ThemeEditor *te)
{
	return te->mode ? &te->dark : &te->light;
}

static UiThemeSet *base_set(ThemeEditor *te)
{
	return te->mode ? &te->baseDark : &te->baseLight;
}

static UiThemeSet *other_set(ThemeEditor *te)
{
	return te->mode ? &te->light : &te->dark;
}

static int field_is_radius(int i)
{
	return i >= UI_THEME_TOKEN_COUNT;
}

static const UiThemeTokenDesc *field_desc(int i)
{
	if (i < 0 || i >= UI_THEME_FIELD_COUNT) i = 0;
	if (i < UI_THEME_TOKEN_COUNT) return &UiTheme_TokenTable[i];
	return &UiTheme_RadiusTable[i - UI_THEME_TOKEN_COUNT];
}

static int field_by_key(const char *key)
{
	int i;
	for (i = 0; i < UI_THEME_TOKEN_COUNT; i++)
		if (lstrcmpA(key, UiTheme_TokenTable[i].key) == 0) return i;
	for (i = 0; i < UI_THEME_RADIUS_COUNT; i++)
		if (lstrcmpA(key, UiTheme_RadiusTable[i].key) == 0)
			return UI_THEME_TOKEN_COUNT + i;
	return -1;
}

static const char *list_group(ThemeEditor *te, int fi)
{
	const char *g = te->basicMode ? UiTheme_FieldGroup(fi) : NULL;
	return g ? g : field_desc(fi)->group;
}

static UiRect inset_rect(UiRect r, float d)
{
	r.x += d;
	r.y += d;
	r.w -= d * 2.0f;
	r.h -= d * 2.0f;
	if (r.w < 0.0f) r.w = 0.0f;
	if (r.h < 0.0f) r.h = 0.0f;
	return r;
}

/* 预览命中的优先级由面积决定: 内层文字/内容盖住外层底色与描边. */
static void pv_hit(ThemeEditor *te, UiRect rc, int field)
{
	if (field < 0 || field >= UI_THEME_FIELD_COUNT) return;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (te->hitCount >= TE_PREVIEW_HITS) return;
	te->hits[te->hitCount].rc = rc;
	te->hits[te->hitCount].field = field;
	te->hitCount++;
}

static int pv_hit_at(ThemeEditor *te, float x, float y, UiRect *out)
{
	int i, best = -1;
	float bestArea = 0.0f;
	for (i = 0; i < te->hitCount; i++) {
		UiRect rc = te->hits[i].rc;
		float area;
		if (x < rc.x || y < rc.y || x >= rc.x + rc.w || y >= rc.y + rc.h)
			continue;
		area = rc.w * rc.h;
		if (best < 0 || area < bestArea) {
			best = i;
			bestArea = area;
		}
	}
	if (best >= 0) {
		if (out) *out = te->hits[best].rc;
		return te->hits[best].field;
	}
	return -1;
}

static UiColor *field_color(ThemeEditor *te, int i)
{
	return &active_set(te)->color[i];
}

static float *field_radius(ThemeEditor *te, int i)
{
	return &active_set(te)->radius[i - UI_THEME_TOKEN_COUNT];
}

/* 工程文件写盘 (静默; 失败在状态栏提示) */
static int write_file_utf8(const WCHAR *path, const char *text, int len)
{
	HANDLE h;
	DWORD wr = 0;
	int ok = 0;
	h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	if (WriteFile(h, text, (DWORD)len, &wr, NULL) && wr == (DWORD)len) ok = 1;
	CloseHandle(h);
	return ok;
}

static int read_file_utf8(const WCHAR *path, char *buf, int cap)
{
	HANDLE h;
	DWORD rd = 0, size;
	if (cap <= 1) return 0;
	h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	size = GetFileSize(h, NULL);
	if (size > (DWORD)cap - 1) size = (DWORD)cap - 1;
	if (!ReadFile(h, buf, size, &rd, NULL)) rd = 0;
	CloseHandle(h);
	buf[rd] = 0;
	return rd > 0;
}

static void te_palette_path(ThemeEditor *te)
{
	WCHAR dir[MAX_PATH];
	DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
	int i = (int)n - 1;
	while (i > 0 && dir[i] != L'\\' && dir[i] != L'/') i--;
	dir[i + 1] = 0;
	lstrcpynW(te->palettePath, dir, MAX_PATH);
	lstrcatW(te->palettePath, L"theme_editor.palette");
}

static void te_save(ThemeEditor *te)
{
	int n = UiThemeSet_Save(&te->light, &te->dark, te->paletteText,
		(int)sizeof(te->paletteText));
	if (n <= 0 || !write_file_utf8(te->palettePath, te->paletteText, n))
		set_status(te, "自动保存失败: 无法写入工程文件");
}

/* ── HSV ──────────────────────────────────────────────────────────── */

static UiColor hsv_rgb(float h, float s, float v)
{
	int i, r, g, b;
	float f, p, q, t;
	if (h < 0.0f) h = 0.0f;
	while (h >= 6.0f) h -= 6.0f;
	s = te_clamp01(s);
	v = te_clamp01(v);
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

static void rgb_hsv(UiColor c, float *h, float *s, float *v)
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
		if (*h < 0.0f) *h += 6.0f;
	}
	*s = mx > 0.0f ? d / mx : 0.0f;
	*v = mx;
}

static void sync_hsv(ThemeEditor *te)
{
	if (field_is_radius(te->sel)) return;
	rgb_hsv(*field_color(te, te->sel), &te->hue, &te->sat, &te->val);
	te->hsvDirty = 0;
}

static void sync_hex(ThemeEditor *te)
{
	UiColor c;
	if (field_is_radius(te->sel)) {
		te->hexText[0] = 0;
		return;
	}
	c = *field_color(te, te->sel);
	UiTheme_FormatHex(c, te->hexText, (int)sizeof(te->hexText));
	if (UiInput_Focused(&te->form) != te->fHex)
		Edit_SetTextUtf8(UiInput_Edit(&te->form, te->fHex), te->hexText);
}

/* 主题应用到预览: 角色派生 + 失效 */
static void te_refresh(ThemeEditor *te)
{
	UiThemeSet_Apply(&te->app->theme, te->mode ? UI_THEME_DARK : UI_THEME_LIGHT,
		active_set(te));
	te->app->themeId = te->mode ? UI_THEME_DARK : UI_THEME_LIGHT;
	UiTheme_Touch(&te->app->theme);
	UiApp_Invalidate(te->app);
}

static void te_commit_color(ThemeEditor *te)
{
	sync_hex(te);
	te_refresh(te);
	te_save(te);
}

/* ── 抽卡 ─────────────────────────────────────────────────────────── */

static unsigned te_draw_next_seed(ThemeEditor *te)
{
	unsigned s;
	te->drawNonce = te->drawNonce * 1664525u + 1013904223u;
	s = (unsigned)GetTickCount() ^ (te->drawNonce * 2654435761u);
	return s ? s : 0x7F4A7C15u;
}

static void te_draw_generate(ThemeEditor *te)
{
	UiThemeGenOpts o;
	int i;
	o.locks = te->drawLocks;
	o.templateLight = &te->baseLight;
	o.templateDark = &te->baseDark;
	o.keepLight = &te->light;
	o.keepDark = &te->dark;
	for (i = 0; i < TE_DRAW_CARDS; i++) {
		unsigned s = te->drawSeed + (unsigned)i * 0x9E3779B9u;
		int t;
		if (!s) s = 0x2545F491u;
		te->drawLight[i] = te->light;
		te->drawDark[i] = te->dark;
		te->drawCardSeed[i] = s;
		te->drawValid[i] = 0;
		for (t = 0; t < 3; t++) {
			unsigned used;
			o.seed = s + (unsigned)t * 0x85EBCA6Bu;
			used = UiThemeGen_Palette(&te->drawLight[i], &te->drawDark[i],
				&o);
			if (used) {
				te->drawCardSeed[i] = used;
				te->drawValid[i] = 1;
				break;
			}
		}
	}
}

static void te_draw_open(ThemeEditor *te)
{
	if (te->drawOpen) return;
	te->drawOpen = 1;
	te->drawHover = TE_DRAW_NONE;
	te->drawHoverArg = 0;
	te->drawPress = TE_DRAW_NONE;
	te->drawPressArg = 0;
	te->drawSeed = te_draw_next_seed(te);
	te_draw_generate(te);
	Ui_SetModal(&te->app->ui, Ui_Id("te.draw.modal", 0, 0));
	UiApp_Invalidate(te->app);
}

static void te_draw_close(ThemeEditor *te)
{
	if (!te->drawOpen) return;
	te->drawOpen = 0;
	te->drawHover = TE_DRAW_NONE;
	te->drawHoverArg = 0;
	te->drawPress = TE_DRAW_NONE;
	te->drawPressArg = 0;
	te->drawHitCount = 0;
	Ui_SetModal(&te->app->ui, UI_ID_NONE);
	UiApp_Invalidate(te->app);
}

static void te_draw_apply(ThemeEditor *te, int i)
{
	char msg[128];
	if (i < 0 || i >= TE_DRAW_CARDS || !te->drawValid[i]) return;
	te->undoLight = te->light;
	te->undoDark = te->dark;
	te->undoValid = 1;
	te->light = te->drawLight[i];
	te->dark = te->drawDark[i];
	te->hsvDirty = 1;
	te->radiusDirty = 1;
	sync_hsv(te);
	sync_hex(te);
	te_refresh(te);
	te_save(te);
	wsprintfA(msg, "已应用抽卡方案 %c · 可再抽卡或撤销", 'A' + i);
	set_status(te, msg);
	te_draw_close(te);
}

static void te_draw_undo(ThemeEditor *te)
{
	if (!te->undoValid) return;
	te->light = te->undoLight;
	te->dark = te->undoDark;
	te->undoValid = 0;
	te->hsvDirty = 1;
	te->radiusDirty = 1;
	sync_hsv(te);
	sync_hex(te);
	te_refresh(te);
	te_save(te);
	set_status(te, "已撤销上次抽卡");
	te_draw_close(te);
}

static void te_draw_reroll(ThemeEditor *te)
{
	te->drawSeed = te_draw_next_seed(te);
	te_draw_generate(te);
	te->drawHover = TE_DRAW_NONE;
	te->drawHoverArg = 0;
	te->drawPress = TE_DRAW_NONE;
	te->drawPressArg = 0;
	UiApp_Invalidate(te->app);
}

static void te_draw_activate(ThemeEditor *te, int action, int arg)
{
	switch (action) {
	case TE_DRAW_CLOSE:
		te_draw_close(te);
		break;
	case TE_DRAW_REROLL:
		te_draw_reroll(te);
		break;
	case TE_DRAW_UNDO:
		te_draw_undo(te);
		break;
	case TE_DRAW_LOCK:
		if (arg >= 0 && arg < 3) {
			unsigned bit = arg == 0 ? UI_THEME_LOCK_PRIMARY :
				(arg == 1 ? UI_THEME_LOCK_NEUTRAL :
				 UI_THEME_LOCK_SEMANTIC);
			te->drawLocks ^= bit;
			te_draw_generate(te);
			UiApp_Invalidate(te->app);
		}
		break;
	case TE_DRAW_USE:
		te_draw_apply(te, arg);
		break;
	}
}

static void select_field(ThemeEditor *te, int i)
{
	if (i < 0 || i >= UI_THEME_FIELD_COUNT || i == te->sel) return;
	if (field_is_radius(i) && UiInput_Focused(&te->form) == te->fHex)
		UiInput_Key(&te->form, te->app ? te->app->hwnd : NULL, VK_ESCAPE);
	te->sel = i;
	te->hsvDirty = 1;
	te->radiusDirty = 1;
	sync_hsv(te);
	sync_hex(te);
}

static void select_preview_field(ThemeEditor *te, int field)
{
	const UiThemeTokenDesc *d = field_desc(field);
	const char *name = UiTheme_FieldLabel(field);
	char msg[192];

	if (!name) name = d->hint;
	if (te->basicMode && !UiTheme_IsBasicField(field)) {
		te->basicMode = 0;
		wsprintfA(msg, "已切到高级视图 · %s · %s", name, d->key);
	} else {
		wsprintfA(msg, "已选择 %s · %s", name, d->key);
	}
	select_field(te, field);
	te->revealSel = 1;
	set_status(te, msg);
	UiApp_Invalidate(te->app);
}

/* ── 列表布局 ─────────────────────────────────────────────────────── */

static void build_rows(ThemeEditor *te)
{
	int i, n = 0;
	float y = 4.0f;
	te->rowCount = 0;
	te->rowTotal = 0.0f;
	for (i = 0; i < UI_THEME_FIELD_COUNT; i++) {
		const UiThemeTokenDesc *d = field_desc(i);
		int keep;
		if (te->basicMode && !te->onlyModified && !te->search[0] &&
			!UiTheme_IsBasicField(i))
			continue;
		if (te->onlyModified &&
			!UiThemeSet_FieldModified(active_set(te), base_set(te), i))
			continue;
		if (te->search[0]) {
			char combo[160];
			if (te_contains(d->key, te->search) ||
				te_contains(d->group, te->search) ||
				te_contains(d->hint, te->search)) keep = 1;
			else keep = 0;
			if (!keep) {
				/* 同时允许 "group key" 组合搜索 */
				wsprintfA(combo, "%s %s", d->group, d->key);
				keep = te_contains(combo, te->search);
			}
			if (!keep) continue;
		}
		if (n == 0 || lstrcmpA(list_group(te, te->rowMap[n - 1]),
				list_group(te, i)) != 0)
			y += 24.0f;
		te->rowMap[n] = i;
		te->rowY[n] = y;
		/* 基础视图 15 项压到 28, 一屏放下且不出现滚动条 */
		y += te->basicMode && !te->search[0] && !te->onlyModified ? 28.0f : 30.0f;
		n++;
	}
	te->rowCount = n;
	te->rowTotal = y + 6.0f;
}

/* ── 绘制小工具 ───────────────────────────────────────────────────── */

static void draw_checker(Gfx *g, UiRect rc, float cell)
{
	int x, y, ix = 0, iy = 0;
	UiColor a = 0xFFD8DCE4u, b = 0xFFF2F4F8u;
	Gfx_PushClip(g, rc);
	for (y = 0; y < (int)rc.h; y += (int)cell, iy++) {
		ix = 0;
		for (x = 0; x < (int)rc.w; x += (int)cell, ix++) {
			Gfx_FillRect(g, Ui_Rect(rc.x + (float)x, rc.y + (float)y, cell, cell),
				((ix + iy) & 1) ? b : a);
		}
	}
	Gfx_PopClip(g);
}

static void draw_swatch(Gfx *g, UiTheme *t, UiRect rc, UiColor c, float radius)
{
	draw_checker(g, rc, 6.0f);
	Gfx_FillRoundRect(g, rc, radius, c);
	/* 白/近白色块在浅色卡片上仅靠 border 几乎不可见, 混一点 text3 提边 */
	Gfx_StrokeRoundRect(g, rc, radius,
		Ui_ColorMix(t->border, t->text3, 0.35f), 1.0f);
}

static void draw_radius_glyph(Gfx *g, UiTheme *t, UiRect rc, float radius)
{
	float r = radius;
	if (r > rc.w * 0.5f) r = rc.w * 0.5f;
	Gfx_FillRoundRect(g, rc, r, t->primary.soft);
	Gfx_StrokeRoundRect(g, rc, r, t->primary.base, 1.5f);
}

static void section_label(Gfx *g, UiTheme *t, const char *text, UiRect rc)
{
	Gfx_Text(g, t->fSmall, text, rc, t->text3, GFX_TEXT_VCENTER);
}

/* ── 左侧列表 ─────────────────────────────────────────────────────── */

static void draw_list(ThemeEditor *te, UiContext *ui, UiRect panel)
{
	UiTheme *t = &te->app->theme;
	Gfx *g = te->app->gfx;
	UiRect inner, vp, content;
	UiButtonStyle segOn = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle segOff = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	float w;
	int i;

	Ui_Panel(ui, panel);
	inner = Ui_Rect(panel.x + 14, panel.y + 12, panel.w - 28, panel.h - 12);

	Gfx_Text(g, t->fHead, "令牌", Ui_Rect(inner.x, inner.y, 110, 24),
		t->text, GFX_TEXT_VCENTER);
	{
		UiRect advRc = Ui_Rect(inner.x + inner.w - 58, inner.y, 58, 24);
		UiRect basicRc = Ui_Rect(advRc.x - 4 - 58, inner.y, 58, 24);
		if (Ui_ButtonEx(ui, Ui_Id("te.view.basic", 0, 0), basicRc, "基础",
				te->basicMode ? &segOn : &segOff, 1) && !te->basicMode) {
			te->basicMode = 1;
			if (!UiTheme_IsBasicField(te->sel) && !te->search[0] &&
				!te->onlyModified)
				select_field(te, UiTheme_BasicField(0));
			te->revealSel = 1;
			set_status(te, "基础视图 · 15 项 · 其余令牌在高级视图");
			UiApp_Invalidate(te->app);
		}
		if (Ui_ButtonEx(ui, Ui_Id("te.view.all", 0, 0), advRc, "高级",
				te->basicMode ? &segOff : &segOn, 1) && te->basicMode) {
			te->basicMode = 0;
			te->revealSel = 1;
			set_status(te, "高级视图 · 全量 68 色 + 5 圆角");
			UiApp_Invalidate(te->app);
		}
	}

	UiInput_SetRect(&te->form, te->fSearch,
		Ui_Rect(inner.x, inner.y + 30, inner.w, 32));
	UiInput_Draw(&te->form, te->fSearch);

	{
		char mod[64], meta[64];
		int n = 0;
		for (i = 0; i < UI_THEME_FIELD_COUNT; i++)
			if (UiThemeSet_FieldModified(active_set(te), base_set(te), i)) n++;
		wsprintfA(mod, "仅显示已改 (%d)", n);
		if (Ui_Checkbox(&te->ckMod, ui,
				Ui_Rect(inner.x, inner.y + 70, 168, 24), mod, 1)) {
			te->onlyModified = te->ckMod.checked;
			UiApp_Invalidate(te->app);
		}
		wsprintfA(meta, "显示 %d / %d", te->rowCount, UI_THEME_FIELD_COUNT);
		Gfx_Text(g, t->fSmall, meta,
			Ui_Rect(inner.x, inner.y + 70, inner.w, 24), t->text4,
			GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
	}

	Gfx_Line(g, inner.x, inner.y + 102, inner.x + inner.w, inner.y + 102,
		t->border, 1.0f);

	vp = Ui_Rect(panel.x + 8, inner.y + 108, panel.w - 16,
		panel.h - 108 - 40 - 12);
	w = UiScrollView_ContentWidth(&te->listSv, vp);
	if (te->rowTotal < vp.h) te->rowTotal = vp.h;
	UiScrollView_Begin(&te->listSv, ui, vp, w, te->rowTotal);
	content = UiScrollView_ContentRect(&te->listSv);

	if (te->revealSel) {
		for (i = 0; i < te->rowCount; i++) {
			if (te->rowMap[i] == te->sel) {
				/* 点选预览/切视图是导航动作, 直接落位, 不陪滚动动画等待 */
				float top = te->rowY[i] - 6.0f;
				float bottom = te->rowY[i] + 34.0f;
				float target = te->listSv.offset;
				float innerH = te->listSv.inner.h;
				if (top < target) target = top;
				else if (bottom > target + innerH) target = bottom - innerH;
				UiScrollView_ScrollTo(&te->listSv, ui, target, 0);
				break;
			}
		}
		te->revealSel = 0;
	}

	for (i = 0; i < te->rowCount; i++) {
		int fi = te->rowMap[i];
		const UiThemeTokenDesc *d = field_desc(fi);
		const char *grp = list_group(te, fi);
		UiRect row = Ui_Rect(content.x, content.y + te->rowY[i], content.w, 28);
		int newGroup = i == 0 ||
			lstrcmpA(list_group(te, te->rowMap[i - 1]), grp) != 0;
		UiWidgetState st;
		UiColor fg;
		char label[96];

		if (newGroup) {
			UiRect hdr = Ui_Rect(content.x + 4, row.y - 24, content.w - 8, 22);
			if (!UiScrollView_Cull(&te->listSv, hdr)) {
				Gfx_FillRect(g, Ui_Rect(hdr.x, hdr.y + 6, 2, 10),
					t->primary.base);
				Gfx_Text(g, t->fSmall, grp,
					Ui_Rect(hdr.x + 10, hdr.y, hdr.w - 10, hdr.h), t->text3,
					GFX_TEXT_VCENTER);
			}
		}
		if (UiScrollView_Cull(&te->listSv, row)) continue;

		st = Ui_StateInstant(ui, Ui_Id("te.row", 7, fi), row, 1);
		if (ui->clicked == Ui_Id("te.row", 7, fi)) {
			select_field(te, fi);
			UiApp_Invalidate(te->app);
		}
		if (fi == te->sel)
			Gfx_FillRoundRect(g, row, 6.0f, t->rowOn);
		else if (st.hover > 0.01f)
			Gfx_FillRoundRect(g, row, 6.0f,
				Ui_ColorAlpha(t->neutral.soft, (int)(255.0f * st.hover)));
		if (fi == te->sel)
			Gfx_StrokeRoundRect(g, row, 6.0f, t->rowOnBorder, 1.0f);

		if (field_is_radius(fi)) {
			draw_radius_glyph(g, t, Ui_Rect(row.x + 8, row.y + 6, 16, 16),
				active_set(te)->radius[fi - UI_THEME_TOKEN_COUNT]);
		} else {
			draw_swatch(g, t, Ui_Rect(row.x + 8, row.y + 5, 18, 18),
				active_set(te)->color[fi], 4.0f);
		}

		{
			const char *shortName = UiTheme_FieldLabel(fi);
			if (shortName)
				wsprintfA(label, "%s · %s", shortName, d->key);
			else
				lstrcpynA(label, d->key, (int)sizeof(label));
		}
		fg = (fi == te->sel) ? t->text : t->text2;
		Gfx_Text(g, t->fBody, label,
			Ui_Rect(row.x + 34, row.y, row.w - 34 - 76, row.h), fg,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

		/* 另一模式的对照色块 */
		if (!field_is_radius(fi)) {
			UiRect oc = Ui_Rect(row.x + row.w - 52, row.y + 7, 14, 14);
			draw_swatch(g, t, oc, other_set(te)->color[fi], 3.0f);
		} else {
			draw_radius_glyph(g, t, Ui_Rect(row.x + row.w - 52, row.y + 7,
				14, 14), other_set(te)->radius[fi - UI_THEME_TOKEN_COUNT]);
		}

		if (UiThemeSet_FieldModified(active_set(te), base_set(te), fi))
			Gfx_FillEllipse(g, row.x + row.w - 20, row.y + 15, 3.0f, 3.0f,
				t->primary.base);

		(void)label;
	}

	if (te->rowCount == 0) {
		Gfx_Text(g, t->fBody, "没有匹配的令牌", content,
			t->text3, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}
	UiScrollView_End(&te->listSv, ui);

	Gfx_Text(g, t->fSmall, te->status,
		Ui_Rect(panel.x + 14, panel.y + panel.h - 30, panel.w - 28, 20),
		t->text3, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	(void)inner;
}

/* ── 颜色拾取器 ───────────────────────────────────────────────────── */

static void picker_draw(ThemeEditor *te, UiContext *ui, UiRect sv, UiRect hueRc,
	UiRect alphaRc)
{
	Gfx *g = te->app->gfx;
	UiTheme *t = &te->app->theme;
	UiColor cur = *field_color(te, te->sel);
	int alpha = (int)((cur >> 24) & 0xFF);
	UiColor base = hsv_rgb(te->hue, 1.0f, 1.0f);
	UiColor curRgb = hsv_rgb(te->hue, te->sat, te->val);
	UiColor black0 = 0x00000000u, black1 = 0x9F000000u;
	UiColor white0 = 0x00FFFFFFu, white1 = 0xFFFFFFFFu;
	UiColor cols[7] = { 0xFFFF0000u, 0xFFFFFF00u, 0xFF00FF00u, 0xFF00FFFFu,
		0xFF0000FFu, 0xFFFF00FFu, 0xFFFF0000u };
	float pos[7] = { 0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f,
		5.0f / 6.0f, 1.0f };
	UiId svId = Ui_Id("te.pick.sv", 0, 0);
	UiId hueId = Ui_Id("te.pick.hue", 0, 0);
	UiId alphaId = Ui_Id("te.pick.alpha", 0, 0);
	int changed = 0;

	/* SV 方块 */
	Gfx_FillRoundRect(g, sv, 6.0f, base);
	Gfx_FillRoundRectGradH(g, sv, 6.0f, white1, white0);
	Gfx_FillRoundRectGradV(g, sv, 6.0f, black0, black1);
	Gfx_StrokeRoundRect(g, sv, 6.0f, t->border, 1.0f);
	{
		float hx = sv.x + te->sat * sv.w;
		float hy = sv.y + (1.0f - te->val) * sv.h;
		Gfx_FillEllipse(g, hx, hy, 6.0f, 6.0f, 0xFFFFFFFFu);
		Gfx_StrokeEllipse(g, hx, hy, 6.0f, 0xFF10151Fu, 1.5f);
	}

	/* 色相条 */
	Gfx_FillRoundRectMultiGrad(g, hueRc, 6.0f, cols, pos, 7, 1);
	Gfx_StrokeRoundRect(g, hueRc, 6.0f, t->border, 1.0f);
	{
		float hx = hueRc.x + (te->hue / 6.0f) * hueRc.w;
		Gfx_FillRoundRect(g, Ui_Rect(hx - 2.0f, hueRc.y - 2.0f, 4.0f,
			hueRc.h + 4.0f), 2.0f, 0xFFFFFFFFu);
		Gfx_StrokeRoundRect(g, Ui_Rect(hx - 2.5f, hueRc.y - 2.5f, 5.0f,
			hueRc.h + 5.0f), 2.5f, 0xFF10151Fu, 1.0f);
	}

	/* 透明度条 */
	draw_checker(g, alphaRc, 5.0f);
	Gfx_FillRoundRectGradH(g, alphaRc, 6.0f,
		Ui_ColorAlpha(curRgb, 0), Ui_ColorAlpha(curRgb, 255));
	Gfx_StrokeRoundRect(g, alphaRc, 6.0f, t->border, 1.0f);
	{
		float ax = alphaRc.x + ((float)alpha / 255.0f) * alphaRc.w;
		Gfx_FillRoundRect(g, Ui_Rect(ax - 2.0f, alphaRc.y - 2.0f, 4.0f,
			alphaRc.h + 4.0f), 2.0f, 0xFFFFFFFFu);
		Gfx_StrokeRoundRect(g, Ui_Rect(ax - 2.5f, alphaRc.y - 2.5f, 5.0f,
			alphaRc.h + 5.0f), 2.5f, 0xFF10151Fu, 1.0f);
	}

	/* 交互 */
	Ui_StateInstant(ui, svId, sv, 1);
	Ui_StateInstant(ui, hueId, hueRc, 1);
	Ui_StateInstant(ui, alphaId, alphaRc, 1);
	Ui_SetPressPoll(ui, svId, 1);
	Ui_SetPressPoll(ui, hueId, 1);
	Ui_SetPressPoll(ui, alphaId, 1);
	Ui_SetCursor(ui, svId, UI_CURSOR_HAND);
	Ui_SetCursor(ui, hueId, UI_CURSOR_HAND);
	Ui_SetCursor(ui, alphaId, UI_CURSOR_HAND);
	if (ui->down == svId) te->dragKind = TE_DRAG_SV;
	else if (ui->down == hueId) te->dragKind = TE_DRAG_HUE;
	else if (ui->down == alphaId) te->dragKind = TE_DRAG_ALPHA;
	if (!ui->mouseDown) te->dragKind = TE_DRAG_NONE;

	if (te->dragKind == TE_DRAG_SV) {
		float s = te_clamp01((ui->mouseX - sv.x) / (sv.w > 0 ? sv.w : 1.0f));
		float v = 1.0f - te_clamp01((ui->mouseY - sv.y) /
			(sv.h > 0 ? sv.h : 1.0f));
		if (s != te->sat || v != te->val) {
			te->sat = s;
			te->val = v;
			changed = 1;
		}
	} else if (te->dragKind == TE_DRAG_HUE) {
		float h = te_clamp01((ui->mouseX - hueRc.x) /
			(hueRc.w > 0 ? hueRc.w : 1.0f)) * 6.0f;
		if (h != te->hue) { te->hue = h; changed = 1; }
	} else if (te->dragKind == TE_DRAG_ALPHA) {
		int a = (int)(te_clamp01((ui->mouseX - alphaRc.x) /
			(alphaRc.w > 0 ? alphaRc.w : 1.0f)) * 255.0f + 0.5f);
		if (a != alpha) {
			UiColor rgb = hsv_rgb(te->hue, te->sat, te->val);
			*field_color(te, te->sel) = Ui_ColorAlpha(rgb, a);
			changed = 1;
		}
	}
	if (changed && te->dragKind != TE_DRAG_ALPHA) {
		UiColor rgb = hsv_rgb(te->hue, te->sat, te->val);
		*field_color(te, te->sel) = Ui_ColorAlpha(rgb, alpha);
	}
	if (changed) {
		sync_hex(te);
		te_refresh(te);
		te_save(te);
	}
}

/* ── 中间编辑面板 ─────────────────────────────────────────────────── */

static void copy_field_to_other(ThemeEditor *te)
{
	if (te->sel < UI_THEME_TOKEN_COUNT)
		other_set(te)->color[te->sel] = *field_color(te, te->sel);
	else
		other_set(te)->radius[te->sel - UI_THEME_TOKEN_COUNT] =
			*field_radius(te, te->sel);
	te_save(te);
	set_status(te, te->mode ? "已复制到 Light" : "已复制到 Dark");
	UiApp_Invalidate(te->app);
}

static void reset_field(ThemeEditor *te)
{
	if (te->sel < UI_THEME_TOKEN_COUNT)
		*field_color(te, te->sel) = base_set(te)->color[te->sel];
	else
		*field_radius(te, te->sel) =
			base_set(te)->radius[te->sel - UI_THEME_TOKEN_COUNT];
	te->hsvDirty = 1;
	te->radiusDirty = 1;
	sync_hsv(te);
	sync_hex(te);
	te_refresh(te);
	te_save(te);
	set_status(te, "已恢复本项内置值");
}

static void draw_editor(ThemeEditor *te, UiContext *ui, UiRect panel)
{
	UiTheme *t = &te->app->theme;
	Gfx *g = te->app->gfx;
	UiRect inner;
	const UiThemeTokenDesc *d = field_desc(te->sel);
	UiButtonStyle primary = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle textBtn = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	float y, bw;
	char line[128];
	char grp[96];

	Ui_Panel(ui, panel);
	inner = Ui_Rect(panel.x + 16, panel.y + 14, panel.w - 32, panel.h - 28);

	{
		const char *shortName = UiTheme_FieldLabel(te->sel);
		if (shortName)
			wsprintfA(grp, "%s · %s", d->group, shortName);
		else
			lstrcpynA(grp, d->group, (int)sizeof(grp));
	}
	Gfx_Text(g, t->fSmall, grp,
		Ui_Rect(inner.x, inner.y, inner.w, 18), t->text3, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fHead, d->key,
		Ui_Rect(inner.x, inner.y + 18, inner.w, 24), t->text,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(g, t->fSmall, d->hint,
		Ui_Rect(inner.x, inner.y + 44, inner.w, 18), t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	y = inner.y + 70;

	if (!field_is_radius(te->sel)) {
		UiColor cur;
		int alpha;
		UiRect sw = Ui_Rect(inner.x, y, 52, 52);
		UiRect sv, hueRc, ar;
		char ratio[128], f1[16], f2[16];
		char tmp[16];
		UiColor parsed;

		/* HEX 输入优先: 本帧后续绘制/拾取都用新颜色 */
		if (UiInput_Focused(&te->form) == te->fHex) {
			Edit_TextUtf8(UiInput_Edit(&te->form, te->fHex), tmp,
				(int)sizeof(tmp));
			if (UiTheme_ParseHex(tmp, &parsed)) {
				cur = *field_color(te, te->sel);
				if (parsed != cur) {
					*field_color(te, te->sel) = parsed;
					te->hsvDirty = 1;
					sync_hex(te);
				}
				tmp[0] = 0;   /* 合法: 不提示 */
			}
		} else {
			tmp[0] = 0;
		}
		if (te->hsvDirty) sync_hsv(te);

		sv = Ui_Rect(inner.x, y + 102, inner.w, 150);
		hueRc = Ui_Rect(inner.x, y + 264, inner.w, 14);
		ar = Ui_Rect(inner.x, y + 290, inner.w, 14);
		picker_draw(te, ui, sv, hueRc, ar);
		if (te->hsvDirty) sync_hsv(te);

		cur = *field_color(te, te->sel);
		alpha = (int)((cur >> 24) & 0xFF);
		draw_swatch(g, t, sw, cur, 8.0f);
		Gfx_Text(g, t->fInput, te->hexText,
			Ui_Rect(inner.x + 64, y, inner.w - 64, 20), t->text,
			GFX_TEXT_VCENTER);
		wsprintfA(line, "A %d · %d%%", alpha, (alpha * 100 + 127) / 255);
		Gfx_Text(g, t->fSmall, line,
			Ui_Rect(inner.x + 64, y + 22, inner.w - 64, 18), t->text3,
			GFX_TEXT_VCENTER);
		{
			int modified = UiThemeSet_FieldModified(active_set(te),
				base_set(te), te->sel);
			Gfx_Text(g, t->fSmall, modified ? "已修改" : "内置值",
				Ui_Rect(inner.x + 64, y + 38, inner.w - 64, 16),
				modified ? t->primary.base : t->text4, GFX_TEXT_VCENTER);
		}

		/* HEX 输入框 (在色块下方, 拾取器上方) */
		section_label(g, t, "HEX", Ui_Rect(inner.x, y + 62, 36, 30));
		UiInput_SetRect(&te->form, te->fHex,
			Ui_Rect(inner.x + 40, y + 62, 132, 30));
		UiInput_Draw(&te->form, te->fHex);
		if (tmp[0]) {
			Gfx_Text(g, t->fSmall, "无效颜色",
				Ui_Rect(inner.x + 182, y + 62, 120, 30), t->error.base,
				GFX_TEXT_VCENTER);
		}

		fmt_fixed(f1, (int)sizeof(f1), UiTheme_Contrast(cur, t->card), 2);
		fmt_fixed(f2, (int)sizeof(f2), UiTheme_Contrast(cur, t->page), 2);
		wsprintfA(ratio, "对比度  卡片 %s   窗口 %s", f1, f2);
		Gfx_Text(g, t->fSmall, ratio,
			Ui_Rect(inner.x, y + 318, inner.w, 18), t->text3,
			GFX_TEXT_VCENTER);
		y += 346;
	} else {
		float r = *field_radius(te, te->sel);
		UiRect box = Ui_Rect(inner.x, y, 120, 120);
		float rr = r;
		char num[24];
		if (rr > 60.0f) rr = 60.0f;
		Gfx_FillRoundRect(g, box, rr, t->panelBg);
		Gfx_StrokeRoundRect(g, box, rr, t->primary.base, 2.0f);
		fmt_fixed(num, (int)sizeof(num), r, 1);
		lstrcatA(num, " DIP");
		Gfx_Text(g, t->fTemp, num,
			Ui_Rect(inner.x + 136, y + 20, inner.w - 136, 40), t->text,
			GFX_TEXT_VCENTER);
		section_label(g, t, "0 – 24", Ui_Rect(inner.x + 136, y + 60,
			inner.w - 136, 20));
		if (te->radiusDirty) {
			UiSlider_SetValue(&te->slRadius, r / 24.0f);
			te->radiusDirty = 0;
		}
		if (Ui_Slider(&te->slRadius, ui, Ui_Rect(inner.x, y + 136, inner.w, 34),
				"圆角", 1)) {
			float nv = Ui_Clampf(te->slRadius.value, 0.0f, 1.0f) * 24.0f;
			int q = (int)(nv * 2.0f + 0.5f);
			*field_radius(te, te->sel) = (float)q * 0.5f;
			te_refresh(te);
			te_save(te);
		}
		y += 186;
		{
			static const float chips[7] = { 0, 4, 6, 8, 10, 12, 16 };
			int i;
			bw = (inner.w - 6.0f * 6.0f) / 7.0f;
			for (i = 0; i < 7; i++) {
				char lbl[16];
				UiRect cr = Ui_Rect(inner.x + (float)i * (bw + 6.0f), y,
					bw, 28);
				wsprintfA(lbl, "%d", (int)chips[i]);
				if (Ui_ButtonEx(ui, Ui_Id("te.chip", i, 0), cr, lbl,
						&plain, 1)) {
					*field_radius(te, te->sel) = chips[i];
					te_refresh(te);
					te_save(te);
					te->radiusDirty = 1;
				}
			}
			y += 40;
		}
	}

	/* 底部动作 */
	{
		float btnY = panel.y + panel.h - 16 - 34;
		int modified = UiThemeSet_FieldModified(active_set(te), base_set(te),
			te->sel);
		if (modified) {
			if (Ui_ButtonEx(ui, Ui_Id("te.resetOne", 0, 0),
					Ui_Rect(inner.x, btnY, 108, 34), "恢复此项",
					&plain, 1))
				reset_field(te);
			if (Ui_ButtonEx(ui, Ui_Id("te.copyOther", 0, 0),
					Ui_Rect(inner.x + 118, btnY, inner.w - 118, 34),
					te->mode ? "复制到 Light" : "复制到 Dark", &plain, 1))
				copy_field_to_other(te);
		} else {
			int hasOther = te->sel < UI_THEME_TOKEN_COUNT
				? other_set(te)->color[te->sel] != *field_color(te, te->sel)
				: other_set(te)->radius[te->sel - UI_THEME_TOKEN_COUNT] !=
					*field_radius(te, te->sel);
			if (hasOther) {
				if (Ui_ButtonEx(ui, Ui_Id("te.copyOther", 0, 0),
						Ui_Rect(inner.x, btnY, inner.w, 34),
						te->mode ? "复制到 Light" : "复制到 Dark",
						&plain, 1))
					copy_field_to_other(te);
			} else {
				if (Ui_ButtonEx(ui, Ui_Id("te.copyOther", 0, 0),
						Ui_Rect(inner.x, btnY, inner.w, 34),
						"两模式一致", &textBtn, 0))
					;
			}
		}
	}

	(void)primary;
}

/* ── 右侧预览 ─────────────────────────────────────────────────────── */

static void draw_roles(ThemeEditor *te, Gfx *g, UiTheme *t, UiRect rc)
{
	static const char *keys[5] = { "blue", "orange", "green", "danger", "btnBg" };
	UiColorRole *list[5];
	int i;
	float cw;
	list[0] = &t->primary; list[1] = &t->warning; list[2] = &t->success;
	list[3] = &t->error; list[4] = &t->neutral;
	cw = (rc.w - 4.0f * 8.0f) / 5.0f;
	for (i = 0; i < 5; i++) {
		UiRect chip = Ui_Rect(rc.x + (float)i * (cw + 8.0f), rc.y, cw, 26);
		Gfx_FillRoundRect(g, chip, 6.0f, list[i]->base);
		Gfx_Text(g, t->fMini, i == 0 ? "主色" : i == 1 ? "警告" :
			i == 2 ? "成功" : i == 3 ? "错误" : "中性",
			chip, list[i]->text, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		Gfx_StrokeRoundRect(g, chip, 6.0f, t->border, 1.0f);
		pv_hit(te, chip, field_by_key(keys[i]));
	}
}

static void preview_hover_draw(ThemeEditor *te, Gfx *g, UiTheme *t, UiRect vp,
	UiRect rc, int field)
{
	(void)vp;
	Gfx_StrokeRoundRect(g, rc, 5.0f, t->primary.base, 2.0f);
	te->hoverField = field;
}

/* 悬停令牌名放在预览面板头部: 滚动视口会裁剪内容, 且浮动气泡易遮挡邻近行 */
static void preview_hover_pill(Gfx *g, UiTheme *t, UiRect panel, int field)
{
	const UiThemeTokenDesc *d = field_desc(field);
	const char *name = UiTheme_FieldLabel(field);
	char tip[192];
	float tw, pw, px, py, maxW, titleW;
	UiRect pill;

	if (!name) name = d->hint;
	wsprintfA(tip, "%s · %s", name, d->key);
	titleW = Gfx_TextWidth(g, t->fHead, "组件预览");
	px = panel.x + 16.0f + titleW + 12.0f;
	tw = Gfx_TextWidth(g, t->fSmall, tip);
	pw = tw + 18.0f;
	maxW = panel.x + panel.w - 62.0f - px;
	if (pw > maxW) pw = maxW;
	if (pw < 24.0f) pw = 24.0f;
	py = panel.y + 13.0f;
	pill = Ui_Rect(px, py, pw, 22.0f);
	Gfx_FillRoundRect(g, pill, 6.0f, t->text);
	Gfx_StrokeRoundRect(g, pill, 6.0f, t->primary.base, 1.0f);
	Gfx_Text(g, t->fSmall, tip, pill, t->page,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void draw_preview(ThemeEditor *te, UiContext *ui, UiRect panel)
{
	UiTheme *t = &te->app->theme;
	Gfx *g = te->app->gfx;
	UiRect vp;
	float w, y;
	UiButtonStyle primary = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle textBtn = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	UiButtonStyle danger = UiButtonStyle_Make(UI_BTYPE_ERROR,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	int fText = field_by_key("text");
	int fText2 = field_by_key("text2");
	int fText3 = field_by_key("text3");
	int fText4 = field_by_key("text4");
	int fBtnPrimary = field_by_key("btnPrimary");
	int fBtnPrimaryBorder = field_by_key("btnPrimaryBorder");
	int fBtnBg = field_by_key("btnBg");
	int fBtnBorder = field_by_key("btnBorder");
	int fDanger = field_by_key("danger");
	int fSwitchOn = field_by_key("switchOn");
	int fSwitchOff = field_by_key("switchOff");
	int fBlue = field_by_key("blue");
	int fInputBg = field_by_key("inputBg");
	int fInputBorder = field_by_key("inputBorder");
	int fHint = field_by_key("hint");
	int fProgressFill = field_by_key("progressFill");
	int fProgressTrack = field_by_key("progressTrack");
	int fToastBg = field_by_key("toastBg");
	int fToastBorder = field_by_key("toastBorder");
	int fToastText = field_by_key("toastText");
	int fRowOn = field_by_key("rowOn");
	int fRowOff = field_by_key("rowOff");
	int fRowOnBorder = field_by_key("rowOnBorder");
	int fRowOffBorder = field_by_key("rowOffBorder");
	int fPageLine = field_by_key("pageLine");
	UiRect hoverRc;

	te->hitCount = 0;
	Ui_Panel(ui, panel);
	Gfx_Text(g, t->fHead, "组件预览",
		Ui_Rect(panel.x + 16, panel.y + 12, 200, 24), t->text,
		GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, te->mode ? "Dark" : "Light",
		Ui_Rect(panel.x, panel.y + 12, panel.w - 16, 24),
		t->primary.base, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
	if (te->hoverField >= 0)
		preview_hover_pill(g, t, panel, te->hoverField);

	vp = Ui_Rect(panel.x + 8, panel.y + 42, panel.w - 16, panel.h - 54);
	w = UiScrollView_ContentWidth(&te->prevSv, vp);
	UiScrollView_Begin(&te->prevSv, ui, vp, w,
		te->prevSv.contentH > 0.0f ? te->prevSv.contentH : 700.0f);
	{
		UiRect content = UiScrollView_ContentRect(&te->prevSv);
		float x = content.x + 8;
		y = content.y + 4;
		w = content.w - 16;

		section_label(g, t, "文本层级", Ui_Rect(x, y, w, 20));
		y += 24;
		{
			UiRect r1 = Ui_Rect(x, y, w, 22);
			UiRect r2 = Ui_Rect(x, y + 22, w, 22);
			UiRect r3 = Ui_Rect(x, y + 44, w, 20);
			UiRect r4 = Ui_Rect(x, y + 64, w, 20);
			Gfx_Text(g, t->fBody, "主文本 text · 设备状态正常",
				r1, t->text, GFX_TEXT_VCENTER);
			Gfx_Text(g, t->fBody, "次文本 text2 · 最近更新 2 分钟前",
				r2, t->text2, GFX_TEXT_VCENTER);
			Gfx_Text(g, t->fSmall, "辅助 text3 · 固件版本 1.8.2",
				r3, t->text3, GFX_TEXT_VCENTER);
			Gfx_Text(g, t->fSmall, "最弱 text4 · 序列号 SC-000042",
				r4, t->text4, GFX_TEXT_VCENTER);
			pv_hit(te, r1, fText);
			pv_hit(te, r2, fText2);
			pv_hit(te, r3, fText3);
			pv_hit(te, r4, fText4);
		}
		y += 94;

		section_label(g, t, "按钮", Ui_Rect(x, y, w, 18));
		y += 20;
		{
			float bw = (w - 3.0f * 8.0f) / 4.0f;
			UiRect b1 = Ui_Rect(x, y, bw, 34);
			UiRect b2 = Ui_Rect(x + bw + 8, y, bw, 34);
			UiRect b3 = Ui_Rect(x + (bw + 8) * 2, y, bw, 34);
			UiRect b4 = Ui_Rect(x + (bw + 8) * 3, y, bw, 34);
			Ui_ButtonEx(ui, Ui_Id("te.pv.primary", 0, 0), b1,
				"主按钮", &primary, 1);
			Ui_ButtonEx(ui, Ui_Id("te.pv.plain", 0, 0), b2,
				"次按钮", &plain, 1);
			Ui_ButtonEx(ui, Ui_Id("te.pv.text", 0, 0), b3,
				"文字", &textBtn, 1);
			Ui_ButtonEx(ui, Ui_Id("te.pv.danger", 0, 0), b4,
				"删除", &danger, 1);
			pv_hit(te, b1, fBtnPrimaryBorder);
			pv_hit(te, inset_rect(b1, 3.0f), fBtnPrimary);
			pv_hit(te, b2, fBtnBorder);
			pv_hit(te, inset_rect(b2, 3.0f), fBtnBg);
			pv_hit(te, b3, fText2);
			pv_hit(te, b4, fDanger);
		}
		y += 44;

		section_label(g, t, "交互控件", Ui_Rect(x, y, w, 18));
		y += 22;
		{
			UiRect swRc = Ui_Rect(x, y, w * 0.5f, 28);
			UiRect ckRc = Ui_Rect(x + w * 0.5f, y, w * 0.5f, 28);
			if (Ui_Switch(&te->sw, ui, swRc, "自动同步", 1))
				UiApp_Invalidate(te->app);
			if (Ui_Checkbox(&te->ckPrev, ui, ckRc, "后台刷新", 1))
				UiApp_Invalidate(te->app);
			pv_hit(te, swRc, te->sw.on ? fSwitchOn : fSwitchOff);
			pv_hit(te, ckRc, fBlue);
		}
		y += 30;
		{
			UiRect slRc = Ui_Rect(x, y, w, 28);
			Ui_Slider(&te->slPrev, ui, slRc, "亮度", 1);
			pv_hit(te, slRc, fBlue);
		}
		y += 36;

		section_label(g, t, "输入框", Ui_Rect(x, y, w, 18));
		y += 20;
		{
			UiRect in = Ui_Rect(x, y, w, 36);
			UiRect hintRc = Ui_Rect(in.x + 12, in.y + 8,
				Gfx_TextWidth(g, t->fInput, "设备名称"), 20);
			Gfx_FillRoundRect(g, in, t->buttonRadius, t->inputBg);
			Gfx_StrokeRoundRect(g, in, t->buttonRadius, t->inputBorder,
				1.0f);
			Gfx_Text(g, t->fInput, "设备名称", Ui_Rect(in.x + 12, in.y,
				in.w - 24, in.h), t->hint, GFX_TEXT_VCENTER);
			pv_hit(te, in, fInputBorder);
			pv_hit(te, inset_rect(in, 3.0f), fInputBg);
			pv_hit(te, hintRc, fHint);
		}
		y += 46;

		section_label(g, t, "进度", Ui_Rect(x, y, w, 18));
		y += 20;
		{
			UiRect pr = Ui_Rect(x, y, w, 8);
			Ui_ProgressBar(ui, pr, 0.62f, 1);
			pv_hit(te, Ui_Rect(x, y - 4, w * 0.62f, 16), fProgressFill);
			pv_hit(te, Ui_Rect(x + w * 0.62f, y - 4, w * 0.38f, 16),
				fProgressTrack);
		}
		y += 18;

		section_label(g, t, "Toast", Ui_Rect(x, y, w, 18));
		y += 20;
		{
			UiRect tt = Ui_Rect(x, y, w, 52);
			UiRect txtRc = Ui_Rect(tt.x + 14, tt.y + 14,
				Gfx_TextWidth(g, t->fBody, "设置已保存"), 24);
			Gfx_FillRoundRect(g, tt, t->toastRadius, t->toastBg);
			Gfx_StrokeRoundRect(g, tt, t->toastRadius, t->toastBorder, 1.0f);
			Gfx_Text(g, t->fBody, "设置已保存", Ui_Rect(tt.x + 14, tt.y,
				tt.w - 28, tt.h), t->toastText, GFX_TEXT_VCENTER);
			pv_hit(te, tt, fToastBorder);
			pv_hit(te, inset_rect(tt, 3.0f), fToastBg);
			pv_hit(te, txtRc, fToastText);
		}
		y += 62;

		section_label(g, t, "列表行", Ui_Rect(x, y, w, 18));
		y += 20;
		{
			int i;
			char nm[48];
			for (i = 0; i < 4; i++) {
				UiRect row = Ui_Rect(x, y, w, 28);
				int on = i == 1;
				wsprintfA(nm, "设备 %d · 在线", i + 1);
				Gfx_FillRoundRect(g, row, 5.0f,
					on ? t->rowOn : t->rowOff);
				Gfx_StrokeRoundRect(g, row, 5.0f,
					on ? t->rowOnBorder : t->rowOffBorder, 1.0f);
				Gfx_Text(g, t->fSmall, nm, Ui_Rect(row.x + 10, row.y,
					row.w - 20, row.h), on ? t->text : t->text2,
					GFX_TEXT_VCENTER);
				Gfx_Line(g, x, row.y + row.h + 2, x + w, row.y + row.h + 2,
					t->pageLine, 1.0f);
				pv_hit(te, row, on ? fRowOnBorder : fRowOffBorder);
				pv_hit(te, inset_rect(row, 3.0f), on ? fRowOn : fRowOff);
				pv_hit(te, Ui_Rect(x, row.y + row.h + 1, w, 3),
					fPageLine);
				y += 30;
			}
		}
		y += 10;

		section_label(g, t, "分页", Ui_Rect(x, y, w, 18));
		y += 22;
		UiPagination_Draw(&te->pg, ui, Ui_Rect(x, y, w, 34), NULL);
		y += 46;

		section_label(g, t, "语义角色 (由原始令牌派生)", Ui_Rect(x, y, w, 18));
		y += 22;
		draw_roles(te, g, t, Ui_Rect(x, y, w, 26));
		y += 38;

		te->prevSv.contentH = (y - content.y) + 8.0f;

		te->hoverField = -1;
		if (ui->mouseOver) {
			int hf = pv_hit_at(te, ui->mouseX, ui->mouseY, &hoverRc);
			if (hf >= 0)
				preview_hover_draw(te, g, t, vp, hoverRc, hf);
		}
	}
	UiScrollView_End(&te->prevSv, ui);
}

/* ── 抽卡面板 ─────────────────────────────────────────────────────── */

static int pick_hover(ThemeEditor *te, int action, int arg)
{
	return te->drawHover == action && te->drawHoverArg == arg;
}

static void pick_hit_add(ThemeEditor *te, UiRect rc, int action, int arg)
{
	if (action == TE_DRAW_NONE || rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (te->drawHitCount >= TE_DRAW_HITS) return;
	te->drawHits[te->drawHitCount].rc = rc;
	te->drawHits[te->drawHitCount].action = action;
	te->drawHits[te->drawHitCount].arg = arg;
	te->drawHitCount++;
}

static int pick_hit_at(ThemeEditor *te, float x, float y, int *arg)
{
	int i, best = -1;
	float bestArea = 0.0f;
	for (i = 0; i < te->drawHitCount; i++) {
		UiRect rc = te->drawHits[i].rc;
		float area;
		if (x < rc.x || y < rc.y || x >= rc.x + rc.w || y >= rc.y + rc.h)
			continue;
		area = rc.w * rc.h;
		if (best < 0 || area < bestArea) {
			best = i;
			bestArea = area;
		}
	}
	if (best >= 0) {
		if (arg) *arg = te->drawHits[best].arg;
		return te->drawHits[best].action;
	}
	if (arg) *arg = 0;
	return TE_DRAW_NONE;
}

static int pick_in_rect(UiRect rc, float x, float y)
{
	return x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h;
}

static UiColor pick_set_color(const UiThemeSet *s, const char *key)
{
	int i = field_by_key(key);
	if (i < 0 || i >= UI_THEME_TOKEN_COUNT) return 0xFF000000u;
	return s->color[i];
}

static void pick_button(Gfx *g, UiTheme *t, UiRect rc, const char *label,
	int primary, int hover, int pressed, int enabled)
{
	UiColor bg, fg, bd = 0;
	if (!enabled) {
		bg = t->btnDisabledBg;
		fg = t->btnDisabledText;
	} else if (primary) {
		bg = pressed ? t->primary.press :
			(hover ? t->primary.hover : t->primary.base);
		fg = t->primary.text;
		bd = t->btnPrimaryBorder;
	} else {
		bg = pressed ? t->btnPressBg :
			(hover ? t->btnHoverBg : t->btnBg);
		fg = t->btnText;
		bd = hover ? t->btnHoverBorder : t->btnBorder;
	}
	Gfx_FillRoundRect(g, rc, t->buttonRadius, bg);
	if (bd && (bd >> 24))
		Gfx_StrokeRoundRect(g, rc, t->buttonRadius, bd, 1.0f);
	Gfx_Text(g, t->fBtn, label, rc, fg,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void pick_chip(Gfx *g, UiTheme *t, UiRect rc, const char *label,
	int active, int hover)
{
	UiColor bg = active ? t->primary.soft :
		(hover ? t->btnHoverBg : t->btnBg);
	UiColor fg = active ? t->primary.base : t->text2;
	UiColor bd = active ? t->primary.base :
		(hover ? t->btnHoverBorder : t->btnBorder);
	float cy = rc.y + rc.h * 0.5f;
	Gfx_FillRoundRect(g, rc, 8.0f, bg);
	Gfx_StrokeRoundRect(g, rc, 8.0f, bd, active ? 1.5f : 1.0f);
	Gfx_FillEllipse(g, rc.x + 12.0f, cy, 3.0f, 3.0f,
		active ? t->primary.base : t->text4);
	Gfx_Text(g, t->fSmall, label, Ui_Rect(rc.x + 21, rc.y, rc.w - 25, rc.h),
		fg, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
}

static void pick_mini(Gfx *g, UiTheme *t, UiRect rc, const UiThemeSet *s,
	int dark)
{
	UiColor page = pick_set_color(s, "page");
	UiColor card = pick_set_color(s, "card");
	UiColor border = pick_set_color(s, "border");
	UiColor text = pick_set_color(s, "text");
	UiColor text2 = pick_set_color(s, "text2");
	UiColor text3 = pick_set_color(s, "text3");
	UiColor text4 = pick_set_color(s, "text4");
	UiColor pri = pick_set_color(s, "blue");
	UiColor ptext = pick_set_color(s, "btnPrimaryText");
	UiColor inputBg = pick_set_color(s, "inputBg");
	UiColor inputBd = pick_set_color(s, "inputBorder");
	UiColor hint = pick_set_color(s, "hint");
	UiColor track = pick_set_color(s, "progressTrack");
	UiColor fill = pick_set_color(s, "progressFill");
	UiColor acc = pick_set_color(s, "tab1");
	UiColor green = pick_set_color(s, "green");
	UiColor orange = pick_set_color(s, "orange");
	UiColor red = pick_set_color(s, "red");
	float pad = 10.0f;
	UiRect inner = Ui_Rect(rc.x + pad, rc.y + 21.0f, rc.w - pad * 2.0f,
		rc.h - 31.0f);
	UiRect cbox = Ui_Rect(inner.x, inner.y, inner.w * 0.56f, inner.h);
	float rx = cbox.x + cbox.w + 10.0f;
	float rw = inner.x + inner.w - rx;

	Gfx_FillRoundRect(g, rc, 8.0f, page);
	Gfx_StrokeRoundRect(g, rc, 8.0f, border, 1.0f);
	Gfx_Text(g, t->fMini, dark ? "Dark" : "Light",
		Ui_Rect(rc.x + pad, rc.y + 2.0f, rc.w - pad * 2.0f, 17.0f),
		text3, GFX_TEXT_VCENTER);

	Gfx_FillRoundRect(g, cbox, 6.0f, card);
	Gfx_StrokeRoundRect(g, cbox, 6.0f, border, 1.0f);
	if (cbox.w > 90.0f) {
		UiRect btn = Ui_Rect(cbox.x + 9.0f, cbox.y + cbox.h - 34.0f,
			62.0f, 24.0f);
		Gfx_Text(g, t->fSmall, "正文 Aa",
			Ui_Rect(cbox.x + 9.0f, cbox.y + 8.0f, cbox.w - 18.0f, 18.0f),
			text, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		Gfx_Text(g, t->fMini, "次级说明",
			Ui_Rect(cbox.x + 9.0f, cbox.y + 26.0f, cbox.w - 18.0f, 16.0f),
			text2, GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		Gfx_FillRoundRect(g, btn, 5.0f, pri);
		Gfx_Text(g, t->fMini, "按钮", btn, ptext,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}
	if (rw > 50.0f) {
		UiRect inp = Ui_Rect(rx, inner.y, rw, 26.0f);
		UiRect trk = Ui_Rect(rx, inner.y + 40.0f, rw, 8.0f);
		Gfx_FillRoundRect(g, inp, 5.0f, inputBg);
		Gfx_StrokeRoundRect(g, inp, 5.0f, inputBd, 1.0f);
		Gfx_Text(g, t->fMini, "输入",
			Ui_Rect(inp.x + 8.0f, inp.y, inp.w - 12.0f, inp.h), hint,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		Gfx_FillRoundRect(g, trk, 4.0f, track);
		Gfx_FillRoundRect(g, Ui_Rect(trk.x, trk.y, trk.w * 0.62f, trk.h),
			4.0f, fill);
		Gfx_FillEllipse(g, rx + 7.0f, inner.y + 68.0f, 6.0f, 6.0f, green);
		Gfx_FillEllipse(g, rx + 25.0f, inner.y + 68.0f, 6.0f, 6.0f, orange);
		Gfx_FillEllipse(g, rx + 43.0f, inner.y + 68.0f, 6.0f, 6.0f, red);
		Gfx_FillRoundRect(g, Ui_Rect(rx, inner.y + inner.h - 18.0f,
			rw * 0.52f, 10.0f), 5.0f, acc);
		Gfx_FillRoundRect(g, Ui_Rect(rx + rw * 0.58f,
			inner.y + inner.h - 18.0f, rw * 0.42f, 10.0f), 5.0f, text4);
	}
}

static void draw_pick_overlay(ThemeEditor *te)
{
	UiApp *app = te->app;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	float pad = 22.0f, gap = 14.0f;
	float pw = app->w - 96.0f, ph = app->h - 80.0f;
	float top, footerY, contentW, cardW, cardH;
	UiRect panel;
	int i;

	if (pw > 1000.0f) pw = 1000.0f;
	if (ph > 640.0f) ph = 640.0f;
	if (pw < 620.0f) pw = 620.0f;
	if (ph < 440.0f) ph = 440.0f;
	panel = Ui_Rect((app->w - pw) * 0.5f, (app->h - ph) * 0.5f, pw, ph);
	te->drawPanel = panel;
	te->drawHitCount = 0;

	Gfx_FillRect(g, Ui_Rect(0, 0, app->w, app->h),
		Ui_ColorAlpha(t->toastOverlay, 0xB4));
	Gfx_FillRoundRect(g, panel, 16.0f, t->card);
	Gfx_StrokeRoundRect(g, panel, 16.0f, t->border, 1.0f);

	Gfx_Text(g, t->fHead, "抽卡",
		Ui_Rect(panel.x + pad, panel.y + 13.0f, 120.0f, 22.0f), t->text,
		GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall,
		"模板受控变换 · 同种子联动 Light/Dark · 锁定组不参与重抽",
		Ui_Rect(panel.x + pad, panel.y + 37.0f, 440.0f, 18.0f), t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	{
		const char *labels[3] = { "主色", "中性色", "语义色" };
		unsigned bits[3] = { UI_THEME_LOCK_PRIMARY, UI_THEME_LOCK_NEUTRAL,
			UI_THEME_LOCK_SEMANTIC };
		float cw = 86.0f, cgap = 8.0f;
		float chipsW = cw * 3.0f + cgap * 2.0f;
		float cx = panel.x + panel.w - pad - chipsW;
		float cy = panel.y + 18.0f;
		Gfx_Text(g, t->fMini, "锁定组",
			Ui_Rect(cx - 58.0f, cy, 50.0f, 30.0f), t->text3,
			GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
		for (i = 0; i < 3; i++) {
			UiRect rc = Ui_Rect(cx + (cw + cgap) * i, cy, cw, 30.0f);
			int active = (te->drawLocks & bits[i]) != 0;
			pick_chip(g, t, rc, labels[i], active,
				pick_hover(te, TE_DRAW_LOCK, i));
			pick_hit_add(te, rc, TE_DRAW_LOCK, i);
		}
	}

	top = panel.y + 72.0f;
	footerY = panel.y + panel.h - pad - 36.0f;
	contentW = panel.w - pad * 2.0f;
	cardW = (contentW - gap * 2.0f) / 3.0f;
	cardH = footerY - 12.0f - top;
	for (i = 0; i < TE_DRAW_CARDS; i++) {
		UiRect c = Ui_Rect(panel.x + pad + (cardW + gap) * i, top, cardW,
			cardH);
		UiRect use;
		char title[24], seed[32];
		float miniY, miniH, useY;
		int useHover = pick_hover(te, TE_DRAW_USE, i);
		int usePress = te->drawPress == TE_DRAW_USE &&
			te->drawPressArg == i;

		Gfx_FillRoundRect(g, c, 12.0f, t->page);
		Gfx_StrokeRoundRect(g, c, 12.0f,
			useHover ? t->primary.base : t->border, useHover ? 1.5f : 1.0f);
		wsprintfA(title, "方案 %c", 'A' + i);
		Gfx_Text(g, t->fHead, title,
			Ui_Rect(c.x + 12.0f, c.y + 8.0f, 100.0f, 20.0f), t->text,
			GFX_TEXT_VCENTER);
		wsprintfA(seed, "#%08X", te->drawCardSeed[i]);
		Gfx_Text(g, t->fMini, seed,
			Ui_Rect(c.x + c.w - 116.0f, c.y + 9.0f, 104.0f, 18.0f),
			t->text3, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
		if (!te->drawValid[i]) {
			Gfx_Text(g, t->fSmall, "未生成可读方案, 请重抽",
				Ui_Rect(c.x + 12.0f, c.y + 56.0f, c.w - 24.0f, 20.0f),
				t->danger, GFX_TEXT_VCENTER);
			continue;
		}
		useY = c.y + c.h - 12.0f - 36.0f;
		miniY = c.y + 38.0f;
		miniH = (useY - 10.0f - miniY - 8.0f) * 0.5f;
		pick_mini(g, t, Ui_Rect(c.x + 12.0f, miniY, c.w - 24.0f, miniH),
			&te->drawLight[i], 0);
		pick_mini(g, t, Ui_Rect(c.x + 12.0f, miniY + miniH + 8.0f,
			c.w - 24.0f, miniH), &te->drawDark[i], 1);
		use = Ui_Rect(c.x + 12.0f, useY, c.w - 24.0f, 36.0f);
		pick_button(g, t, use, "使用这套", 1, useHover, usePress, 1);
		pick_hit_add(te, use, TE_DRAW_USE, i);
	}

	{
		UiRect closeRc = Ui_Rect(panel.x + panel.w - pad - 76.0f, footerY,
			76.0f, 36.0f);
		UiRect rerollRc = Ui_Rect(closeRc.x - 8.0f - 84.0f, footerY, 84.0f,
			36.0f);
		UiRect undoRc = Ui_Rect(rerollRc.x - 8.0f - 112.0f, footerY,
			112.0f, 36.0f);
		char foot[128];
		int nLocks = (int)((te->drawLocks & UI_THEME_LOCK_PRIMARY) != 0) +
			(int)((te->drawLocks & UI_THEME_LOCK_NEUTRAL) != 0) +
			(int)((te->drawLocks & UI_THEME_LOCK_SEMANTIC) != 0);
		wsprintfA(foot, "批次 #%08X · %d 组锁定 · 生成器自动校验对比度",
			te->drawSeed, nLocks);
		Gfx_Text(g, t->fMini, foot,
			Ui_Rect(panel.x + pad, footerY,
				undoRc.x - panel.x - pad - 12.0f, 36.0f), t->text3,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

		pick_button(g, t, undoRc, "撤销上次抽卡", 0,
			te->undoValid && pick_hover(te, TE_DRAW_UNDO, 0),
			te->drawPress == TE_DRAW_UNDO, te->undoValid);
		if (te->undoValid) pick_hit_add(te, undoRc, TE_DRAW_UNDO, 0);
		pick_button(g, t, rerollRc, "重抽", 0,
			pick_hover(te, TE_DRAW_REROLL, 0),
			te->drawPress == TE_DRAW_REROLL, 1);
		pick_hit_add(te, rerollRc, TE_DRAW_REROLL, 0);
		pick_button(g, t, closeRc, "关闭", 0,
			pick_hover(te, TE_DRAW_CLOSE, 0),
			te->drawPress == TE_DRAW_CLOSE, 1);
		pick_hit_add(te, closeRc, TE_DRAW_CLOSE, 0);
	}
}

/* ── 导出 ─────────────────────────────────────────────────────────── */

static int copy_clipboard(HWND hwnd, const char *utf8)
{
	int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	HGLOBAL h;
	WCHAR *dst;
	if (wlen <= 0) return 0;
	h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wlen * sizeof(WCHAR));
	if (!h) return 0;
	dst = (WCHAR *)GlobalLock(h);
	if (!dst) { GlobalFree(h); return 0; }
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, wlen);
	GlobalUnlock(h);
	if (!OpenClipboard(hwnd)) { GlobalFree(h); return 0; }
	EmptyClipboard();
	if (!SetClipboardData(CF_UNICODETEXT, h)) {
		GlobalFree(h);
		CloseClipboard();
		return 0;
	}
	CloseClipboard();
	return 1;
}

static int te_build_codegen(ThemeEditor *te)
{
	SYSTEMTIME st;
	char stamp[32];
	GetLocalTime(&st);
	wsprintfA(stamp, "%04d-%02d-%02d %02d:%02d", (int)st.wYear,
		(int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute);
	return UiThemeSet_Codegen(&te->light, &te->dark, te->gen,
		(int)sizeof(te->gen), stamp);
}

static void te_export(ThemeEditor *te)
{
	OPENFILENAMEW ofn;
	WCHAR file[MAX_PATH];
	WCHAR dir[MAX_PATH];
	WCHAR title[64];
	int n;

	n = te_build_codegen(te);
	if (n <= 0) { set_status(te, "代码生成失败"); return; }

	ZeroMemory(&ofn, sizeof(ofn));
	GetModuleFileNameW(NULL, dir, MAX_PATH);
	{
		int i = (int)lstrlenW(dir) - 1;
		while (i > 0 && dir[i] != L'\\' && dir[i] != L'/') i--;
		dir[i + 1] = 0;
	}
	lstrcpynW(file, dir, MAX_PATH);
	lstrcatW(file, L"theme_apply_preset.c");
	lstrcpynW(title, L"导出 theme_apply_preset()", 64);
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = te->app->hwnd;
	ofn.lpstrFilter = L"C 源文件 (*.c)\0*.c\0所有文件 (*.*)\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"c";
	ofn.lpstrTitle = title;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&ofn)) {
		set_status(te, "已取消导出");
		return;
	}
	if (write_file_utf8(file, te->gen, n)) {
		char msg[320], path8[MAX_PATH * 3];
		WideCharToMultiByte(CP_UTF8, 0, file, -1, path8, (int)sizeof(path8),
			NULL, NULL);
		wsprintfA(msg, "已导出 %d 字节: %s", n, path8);
		set_status(te, msg);
	} else {
		set_status(te, "导出失败: 无法写入文件");
	}
}

/* ── 帧 ──────────────────────────────────────────────────────────── */

static void draw_header(ThemeEditor *te, UiContext *ui)
{
	UiTheme *t = &te->app->theme;
	Gfx *g = te->app->gfx;
	UiApp *app = te->app;
	UiButtonStyle primary = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	float right = app->w - 20.0f;
	UiRect exportRc = Ui_Rect(right - 124, 14, 124, 36);
	UiRect copyRc = Ui_Rect(exportRc.x - 10 - 88, 14, 88, 36);
	UiRect resetRc = Ui_Rect(copyRc.x - 10 - 100, 14, 100, 36);
	UiRect darkRc = Ui_Rect(resetRc.x - 10 - 62, 14, 62, 36);
	UiRect lightRc = Ui_Rect(darkRc.x - 6 - 62, 14, 62, 36);
	UiRect drawRc = Ui_Rect(lightRc.x - 10 - 78, 14, 78, 36);

	Gfx_Text(g, t->fBig, "CUI Theme Studio",
		Ui_Rect(24, 12, 420, 30), t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "点选预览元素定位令牌 · 基础/高级视图 · 抽卡配色 · 导出预设",
		Ui_Rect(25, 42, 620, 18), t->text3, GFX_TEXT_VCENTER);

	if (Ui_ButtonEx(ui, Ui_Id("te.draw", 0, 0), drawRc, "抽卡", &plain, 1))
		te_draw_open(te);

	if (Ui_ButtonEx(ui, Ui_Id("te.mode.light", 0, 0), lightRc, "Light",
			te->mode == 0 ? &primary : &plain, 1) && te->mode != 0) {
		te->mode = 0;
		te->hsvDirty = 1;
		te->radiusDirty = 1;
		sync_hsv(te);
		sync_hex(te);
		te_refresh(te);
		set_status(te, "切换到 Light");
	}
	if (Ui_ButtonEx(ui, Ui_Id("te.mode.dark", 0, 0), darkRc, "Dark",
			te->mode == 1 ? &primary : &plain, 1) && te->mode != 1) {
		te->mode = 1;
		te->hsvDirty = 1;
		te->radiusDirty = 1;
		sync_hsv(te);
		sync_hex(te);
		te_refresh(te);
		set_status(te, "切换到 Dark");
	}
	if (Ui_ButtonEx(ui, Ui_Id("te.reset.all", 0, 0), resetRc, "重置本模式",
			&plain, 1)) {
		*active_set(te) = *base_set(te);
		te->hsvDirty = 1;
		te->radiusDirty = 1;
		sync_hsv(te);
		sync_hex(te);
		te_refresh(te);
		te_save(te);
		set_status(te, "已恢复当前模式内置配色");
	}
	if (Ui_ButtonEx(ui, Ui_Id("te.copy.clip", 0, 0), copyRc, "复制代码",
			&plain, 1)) {
		int n = te_build_codegen(te);
		if (n > 0 && copy_clipboard(app->hwnd, te->gen))
			set_status(te, "代码已复制到剪贴板");
		else
			set_status(te, "复制到剪贴板失败");
	}
	if (Ui_ButtonEx(ui, Ui_Id("te.export", 0, 0), exportRc, "导出代码",
			&primary, 1))
		te_export(te);
}

static void on_frame(UiApp *app, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	float top = 70.0f;
	float h = app->h - top - 34.0f;
	UiRect listRc, centerRc, prevRc;

	UiInput_BeginFrame(&te->form);
	build_rows(te);

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);

	draw_header(te, ui);

	listRc = Ui_Rect(20, top, TE_LEFT_W, h);
	centerRc = Ui_Rect(listRc.x + TE_LEFT_W + TE_GAP, top, TE_CENTER_W, h);
	prevRc = Ui_Rect(centerRc.x + TE_CENTER_W + TE_GAP, top,
		app->w - (centerRc.x + TE_CENTER_W + TE_GAP) - 20, h);

	draw_list(te, ui, listRc);
	draw_editor(te, ui, centerRc);
	draw_preview(te, ui, prevRc);

	{
		char hint[160];
		wsprintfA(hint, "↑↓ 选择令牌 · 搜索支持 key/分组/中文描述 · "
			"滚轮滚动列表");
		Gfx_Text(g, t->fSmall, hint,
			Ui_Rect(24, app->h - 26, app->w - 48, 16), t->text4,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	}
	if (te->drawOpen)
		draw_pick_overlay(te);
}

/* ── 输入 ─────────────────────────────────────────────────────────── */

static void on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	if (te->drawOpen) {
		if (msg == UI_APP_MOUSE_MOVE) {
			int arg = 0;
			int action = pick_hit_at(te, x, y, &arg);
			if (action != te->drawHover || arg != te->drawHoverArg) {
				te->drawHover = action;
				te->drawHoverArg = arg;
				UiApp_Invalidate(app);
			}
		} else if (msg == UI_APP_MOUSE_LEAVE) {
			if (te->drawHover != TE_DRAW_NONE) {
				te->drawHover = TE_DRAW_NONE;
				te->drawHoverArg = 0;
				UiApp_Invalidate(app);
			}
		} else if (msg == UI_APP_MOUSE_DOWN) {
			int arg = 0;
			int action = pick_hit_at(te, x, y, &arg);
			te->drawPress = action;
			te->drawPressArg = arg;
			if (action != TE_DRAW_NONE) UiApp_Invalidate(app);
		} else if (msg == UI_APP_MOUSE_UP) {
			int arg = 0;
			int action = pick_hit_at(te, x, y, &arg);
			int hitPress = action != TE_DRAW_NONE &&
				action == te->drawPress && arg == te->drawPressArg;
			te->drawPress = TE_DRAW_NONE;
			te->drawPressArg = 0;
			if (hitPress)
				te_draw_activate(te, action, arg);
			else if (action == TE_DRAW_NONE &&
				!pick_in_rect(te->drawPanel, x, y))
				te_draw_close(te);
		}
		return;
	}
	if (msg == UI_APP_MOUSE_MOVE) {
		int hf = pv_hit_at(te, x, y, NULL);
		if (hf != te->hoverField) {
			te->hoverField = hf;
			UiApp_Invalidate(app);
		}
	} else if (msg == UI_APP_MOUSE_LEAVE) {
		if (te->hoverField >= 0) {
			te->hoverField = -1;
			UiApp_Invalidate(app);
		}
	} else if (msg == UI_APP_MOUSE_DOWN) {
		te->pressedField = pv_hit_at(te, x, y, NULL);
		if (te->pressedField >= 0)
			UiApp_Invalidate(app);
	}
	if (msg == UI_APP_MOUSE_DOWN)
		UiInput_MouseDown(&te->form, Ui_HitTest(&app->ui, x, y), x);
	else if (msg == UI_APP_MOUSE_MOVE)
		UiInput_MouseMove(&te->form, x);
	else if (msg == UI_APP_MOUSE_UP) {
		int f = pv_hit_at(te, x, y, NULL);
		if (f >= 0 && f == te->pressedField)
			select_preview_field(te, f);
		te->pressedField = -1;
		UiInput_MouseUp(&te->form);
	}
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	int focused = UiInput_Focused(&te->form);

	if (te->drawOpen) {
		if (vk == VK_ESCAPE) {
			te_draw_close(te);
			return 1;
		}
		if (vk == 'R') {
			te_draw_reroll(te);
			return 1;
		}
		if (vk == 'U') {
			te_draw_undo(te);
			return 1;
		}
		if (vk >= '1' && vk <= '3') {
			te_draw_apply(te, (int)vk - '1');
			return 1;
		}
		return 1;
	}
	if (UiInput_Key(&te->form, app->hwnd, vk)) {
		if (focused == te->fSearch) {
			Edit_TextUtf8(UiInput_Edit(&te->form, te->fSearch),
				te->search, (int)sizeof(te->search));
			UiApp_Invalidate(app);
		}
		if (focused == te->fHex && te->sel < UI_THEME_TOKEN_COUNT) {
			char tmp[16];
			UiColor parsed;
			Edit_TextUtf8(UiInput_Edit(&te->form, te->fHex), tmp,
				(int)sizeof(tmp));
			if (UiTheme_ParseHex(tmp, &parsed)) {
				*field_color(te, te->sel) = parsed;
				te->hsvDirty = 1;
				te_refresh(te);
				te_save(te);
			}
		}
		return 1;
	}
	if (focused >= 0) {
		if (vk == VK_RETURN) {
			if (focused == te->fSearch) {
				Edit_TextUtf8(UiInput_Edit(&te->form, te->fSearch),
					te->search, (int)sizeof(te->search));
				UiApp_Invalidate(app);
			} else if (focused == te->fHex) {
				char tmp[16];
				UiColor parsed;
				Edit_TextUtf8(UiInput_Edit(&te->form, te->fHex), tmp,
					(int)sizeof(tmp));
				if (UiTheme_ParseHex(tmp, &parsed)) {
					*field_color(te, te->sel) = parsed;
					te->hsvDirty = 1;
					te_refresh(te);
					te_save(te);
					set_status(te, "HEX 已应用");
				} else {
					set_status(te, "HEX 无效: 需要 RRGGBB 或 AARRGGBB");
				}
			}
			return 1;
		}
		return 1;
	}

	if (vk == VK_UP || vk == VK_DOWN) {
		int pos = -1, i;
		for (i = 0; i < te->rowCount; i++)
			if (te->rowMap[i] == te->sel) pos = i;
		if (pos < 0) pos = 0;
		pos += (vk == VK_DOWN) ? 1 : -1;
		if (pos < 0) pos = 0;
		if (pos >= te->rowCount) pos = te->rowCount - 1;
		if (te->rowCount > 0) {
			select_field(te, te->rowMap[pos]);
			UiScrollView_EnsureVisible(&te->listSv, &app->ui,
				te->rowY[pos], 30.0f);
			UiApp_Invalidate(app);
		}
		return 1;
	}
	return 0;
}

static int on_char(UiApp *app, wchar_t ch, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	int focused = UiInput_Focused(&te->form);
	int r;
	if (te->drawOpen) return 1;
	r = UiInput_Char(&te->form, ch);
	if (r && focused == te->fSearch) {
		Edit_TextUtf8(UiInput_Edit(&te->form, te->fSearch),
			te->search, (int)sizeof(te->search));
		UiApp_Invalidate(app);
	}
	return r;
}

static void on_ime(UiApp *app, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	UiInput_OnIme(&te->form);
	(void)app;
}

static void on_timer(UiApp *app, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	UiInput_Blink(&te->form);
}

static int on_animating(UiApp *app, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	(void)app;
	return UiScrollView_Busy(&te->listSv) || UiScrollView_Busy(&te->prevSv);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	UiScrollView_Tick(&te->listSv, &app->ui, (float)dtMs);
	UiScrollView_Tick(&te->prevSv, &app->ui, (float)dtMs);
}

static void on_shutdown(UiApp *app, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	te_save(te);
	UiInput_Shutdown(&te->form);
	(void)app;
}

static void on_init(UiApp *app, void *user)
{
	ThemeEditor *te = (ThemeEditor *)user;
	UiScrollConfig cfg = UiScrollConfig_Desktop();
	UiScrollbarStyle style = UiScrollbarStyle_Default();
	UiScrollPadding pad;

	te->app = app;
	app->minW = 1120.0f;
	app->minH = 700.0f;

	/* 内置快照: 先用壳初始化好的 Light, 切 Dark 再读. */
	UiThemeSet_Read(&te->baseLight, &app->theme);
	UiTheme_SetTheme(&app->theme, UI_THEME_DARK);
	UiThemeSet_Read(&te->baseDark, &app->theme);
	UiTheme_SetTheme(&app->theme, UI_THEME_LIGHT);

	te->light = te->baseLight;
	te->dark = te->baseDark;
	te->mode = 0;
	te->sel = 0;
	te->basicMode = 1;
	te->hoverField = -1;
	te->pressedField = -1;
	te->hsvDirty = 1;

	te_palette_path(te);
	{
		static char buf[16384];
		if (read_file_utf8(te->palettePath, buf, (int)sizeof(buf)) &&
			!UiThemeSet_Load(&te->light, &te->dark, buf)) {
			te->light = te->baseLight;
			te->dark = te->baseDark;
		}
	}

	UiInput_Init(&te->form, &app->ui, &app->ime);
	te->fSearch = UiInput_Add(&te->form, Ui_Id("te.search", 0, 0), 63, 0,
		NULL, L"搜索令牌 / 分组 / 描述");
	te->fHex = UiInput_Add(&te->form, Ui_Id("te.hex", 0, 0), 9, 0,
		"0123456789abcdefABCDEFxX", L"RRGGBB");

	UiScrollView_Init(&te->listSv, Ui_Id("te.list", 0, 0), &cfg, &style);
	style.reserveSpace = 1;
	UiScrollView_Init(&te->prevSv, Ui_Id("te.prev", 0, 0), &cfg, &style);
	pad = (UiScrollPadding){ 10, 4, 10, 8 };
	UiScrollView_SetPadding(&te->listSv, pad);
	pad = (UiScrollPadding){ 4, 4, 4, 8 };
	UiScrollView_SetPadding(&te->prevSv, pad);
	te->prevSv.contentH = 700.0f;

	UiSwitch_Init(&te->sw, Ui_Id("te.pv.sw", 0, 0), 1);
	UiCheckbox_Init(&te->ckMod, Ui_Id("te.ckMod", 0, 0), 0);
	UiCheckbox_Init(&te->ckPrev, Ui_Id("te.ckPrev", 0, 0), 1);
	UiSlider_Init(&te->slRadius, Ui_Id("te.slRadius", 0, 0), 0.5f);
	UiSlider_Init(&te->slPrev, Ui_Id("te.pv.sl", 0, 0), 0.62f);
	UiPagination_Init(&te->pg, Ui_Id("te.pv.pg", 0, 0), 120, 10);

	te_refresh(te);
	te->hsvDirty = 1;
	sync_hsv(te);
	sync_hex(te);
	set_status(te, "基础视图就绪 · 点选右侧组件也能定位令牌 · 修改自动保存");
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.animating = on_animating;
	cb.tick = on_tick;
	cb.mouse = on_mouse;
	cb.key = on_key;
	cb.chr = on_char;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI Theme Studio", 1280, 840, UI_THEME_LIGHT, &cb, &g_te);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
