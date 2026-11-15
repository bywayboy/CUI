/*
 * ui_theme_edit.h - 主题令牌表 / 调色板存取 / 工程文件 / 代码生成
 *
 * 仅供主题编辑器与 headless 测试使用, 不进入 CORE_OBJS:
 * 运行时 UI 仍只依赖 UiTheme 的字段本身.
 */
#ifndef SCV2_UI_THEME_EDIT_H
#define SCV2_UI_THEME_EDIT_H

#include "ui/ui_theme.h"

#define UI_THEME_TOKEN_COUNT 68
#define UI_THEME_RADIUS_COUNT 5
#define UI_THEME_FIELD_COUNT (UI_THEME_TOKEN_COUNT + UI_THEME_RADIUS_COUNT)

/* 基础视图: 12 个关键颜色 + 3 个常用圆角, 其余令牌收进高级视图. */
#define UI_THEME_BASIC_COUNT 15

enum {
	UI_THEME_TOKEN_COLOR = 0,
	UI_THEME_TOKEN_RADIUS = 1
};

typedef struct UiThemeTokenDesc {
	const char *key;      /* UiTheme 字段名, 也是工程文件 key */
	const char *group;    /* 列表分组标题 */
	const char *hint;     /* 使用位置提示 */
	unsigned offset;      /* offsetof(UiTheme, key) */
	int kind;             /* UI_THEME_TOKEN_* */
} UiThemeTokenDesc;

/* 一份完整的调色板: 68 色 + 5 圆角 (与 theme_apply_preset 一一对应) */
typedef struct UiThemeSet {
	UiColor color[UI_THEME_TOKEN_COUNT];
	float radius[UI_THEME_RADIUS_COUNT];
} UiThemeSet;

extern const UiThemeTokenDesc UiTheme_TokenTable[UI_THEME_TOKEN_COUNT];
extern const UiThemeTokenDesc UiTheme_RadiusTable[UI_THEME_RADIUS_COUNT];

/* 基础视图. UiTheme_BasicField(n) 返回组合表索引 (0..72), -1 = 无效. */
int UiTheme_BasicField(int nth);
int UiTheme_IsBasicField(int field);
/* 基础令牌的中文短名; 非基础令牌返回 NULL. */
const char *UiTheme_FieldLabel(int field);
/* 基础令牌在基础视图中的分组 ("外观"/"文字"/"交互"/"几何"); 其余返回 NULL. */
const char *UiTheme_FieldGroup(int field);

void UiThemeSet_Read(UiThemeSet *set, const UiTheme *t);
/* 写入 UiTheme 并刷新派生角色; themeId 一并设置. */
void UiThemeSet_Apply(UiTheme *t, UiThemeId id, const UiThemeSet *set);
int  UiThemeSet_Equals(const UiThemeSet *a, const UiThemeSet *b);

/* index: 0..67 颜色, 68..72 圆角. 1 = 与 base 不同. */
int UiThemeSet_FieldModified(const UiThemeSet *set, const UiThemeSet *base,
	int index);

/* 工程文件 (UTF-8, 行格式). 返回写入字节数 (不含结尾 0) 或 -1. */
int UiThemeSet_Save(const UiThemeSet *light, const UiThemeSet *dark,
	char *buf, int cap);
/* 成功返回 1; 缺失字段保留调用方预置值. */
int UiThemeSet_Load(UiThemeSet *light, UiThemeSet *dark, const char *text);

/* 生成可直接替换 ui_theme.c 中 theme_apply_preset 的函数体.
 * 返回写入字节数或 -1. */
int UiThemeSet_Codegen(const UiThemeSet *light, const UiThemeSet *dark,
	char *buf, int cap, const char *stamp);

/* "F7F9FF" / "FFF7F9FF" / "0x..." -> ARGB. 1 = 成功. */
int UiTheme_ParseHex(const char *s, UiColor *out);
/* alpha=FF 输出 6 位, 否则 8 位 (AARRGGBB). 返回长度. */
int UiTheme_FormatHex(UiColor c, char *buf, int cap);
/* WCAG 风格对比度 (亮度按 299/587/114 近似, alpha 先合成到 bg). */
float UiTheme_Contrast(UiColor fg, UiColor bg);

/* ── 随机配色生成 (抽卡) ─────────────────────────────────────────── */

/* 锁定组: 抽卡时从当前工程原样保留该组令牌, 只重抽其余部分. */
enum {
	UI_THEME_LOCK_PRIMARY  = 1u << 0,  /* 主色 + 强调 + 主按钮文字 */
	UI_THEME_LOCK_NEUTRAL  = 1u << 1,  /* 底/边/文字/行/面板中性族 */
	UI_THEME_LOCK_SEMANTIC = 1u << 2   /* 成功/警告/危险 */
};

typedef struct UiThemeGenOpts {
	unsigned seed;                  /* 0 = 用默认种子 */
	unsigned locks;                 /* UI_THEME_LOCK_* 位掩码 */
	const UiThemeSet *templateLight;/* 骨架, 必填 */
	const UiThemeSet *templateDark;
	const UiThemeSet *keepLight;    /* 锁定来源 = 当前工程; NULL 时用模板 */
	const UiThemeSet *keepDark;
} UiThemeGenOpts;

/* 基于模板做受控变换 (只改色相/饱和度, 保留明度与 alpha), Light/Dark 同一
 * 种子联动; 圆角不参与随机. 结果会做对比度校验并在内部重试.
 * 返回实际使用的种子 (>0); 0 = 失败, light/dark 不变. */
unsigned UiThemeGen_Palette(UiThemeSet *light, UiThemeSet *dark,
	const UiThemeGenOpts *opts);

#endif /* SCV2_UI_THEME_EDIT_H */
