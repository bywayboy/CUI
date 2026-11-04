/*
 * ui_theme.h - 颜色/字体/尺寸令牌（组件不写死颜色）
 */
#ifndef SCV2_UI_THEME_H
#define SCV2_UI_THEME_H

#include "ui/ui_types.h"

#define UI_WIN_W 420.0f
#define UI_WIN_H 480.0f
#define UI_HEADER_H 52.0f
#define UI_PAD 10.0f
#define UI_PANEL_RADIUS 12.0f

typedef enum {
	UI_THEME_LIGHT = 0,
	UI_THEME_DARK = 1
} UiThemeId;

/* 语义色角色: 组件只读这里, 不直接依赖具体色值.
 * base/hover/press 是填充色; soft 是低饱和底板 (警告/成功/错误按钮);
 * text 是在 base/soft 上的可读文字色; border 是常规描边. */
typedef struct UiColorRole {
	UiColor base;
	UiColor hover;
	UiColor press;
	UiColor soft;
	UiColor text;
	UiColor border;
} UiColorRole;

typedef struct UiTheme {
	/* 字体（DIP 字号） */
	GfxFont *fTitle;    /* 16 bold */
	GfxFont *fHead;     /* 15 bold */
	GfxFont *fBody;     /* 13 normal */
	GfxFont *fSmall;    /* 11 normal */
	GfxFont *fTemp;     /* 26 bold */
	GfxFont *fBtn;      /* 12 bold */
	GfxFont *fMini;     /* 11 normal */
	GfxFont *fTempBig;  /* 46 bold */
	GfxFont *fInput;    /* 14 normal 编辑框 */
	GfxFont *fLabel;    /* 12 bold 字段标签 */
	GfxFont *fBig;      /* 22 bold 大标题 */

	/* 通用 */
	UiColor page;
	UiColor card;
	UiColor border;
	UiColor frame;
	UiColor text;
	UiColor text2;
	UiColor text3;
	UiColor text4;
	UiColor green;
	UiColor gold;
	UiColor red;
	UiColor orange;
	UiColor blue;
	UiColor blue2;
	UiColor btnBg;
	UiColor switchOn;
	UiColor switchOff;

	/* 设备面板（02.svg 取色） */
	UiColor panelTop;
	UiColor panelBottom;
	UiColor hdrLine;
	UiColor tab1;
	UiColor tab2;
	UiColor btn1;
	UiColor btn2;
	UiColor iconBlue;
	UiColor panelText;
	UiColor panelDisabled;
	UiColor tile;
	UiColor pill;
	UiColor power;

	/* 卡片/控件 */
	UiColor controlBox;
	UiColor controlBorder;
	UiColor rowOn;
	UiColor rowOnBorder;
	UiColor rowOff;
	UiColor rowOffBorder;
	UiColor pageLine;

	/* 安装器 */
	UiColor inputBg;
	UiColor inputBorder;
	UiColor inputFocus;
	UiColor hint;
	UiColor danger;
	UiColor dangerBg;
	UiColor okBg;

	/* ── 组件令牌 (Button / Panel / ProgressBar / Toast) ───────────── */
	UiThemeId themeId;
	UiColor page2;                 /* 窗口背景渐变的下端 */
	UiColor frameHi;               /* 内层高光描边 */
	UiColor panelBg, panelBorder;

	UiColor btnPrimary, btnPrimary2, btnPrimaryHover, btnPrimaryPress;
	UiColor btnPrimaryText, btnPrimaryBorder;
	UiColor btnHoverBg, btnPressBg;    /* 次级按钮; bg 复用 btnBg */
	UiColor btnText, btnBorder, btnHoverBorder;
	UiColor btnDisabledBg, btnDisabledText;

	UiColor progressTrack, progressFill, progressFill2;

	UiColor toastBg, toastBorder, toastText, toastOverlay;

	/* ── 语义色角色 (自定义主题 / 新组件使用) ──────────────────────── */
	UiColorRole primary;   /* 主色: 主按钮/焦点/进度/开关 */
	UiColorRole warning;   /* 警告 */
	UiColorRole success;   /* 成功 */
	UiColorRole error;     /* 错误/危险 */
	UiColorRole neutral;   /* default: 中性按钮/次要控件 */

	uint32_t themeRev;     /* 每次换肤 +1; overlay/layer 用它判断重建 */
	float panelRadius, buttonRadius, progressRadius, toastRadius;
} UiTheme;

/* 自定义主题的 5 个语义色. 字段传 0 = 保留 base 预设对应角色.
 * neutral 就是 "default" (C 里 default 是关键字). */
typedef struct UiThemeColors {
	UiColor primary;
	UiColor warning;
	UiColor success;
	UiColor error;
	UiColor neutral;
} UiThemeColors;

void UiTheme_Init(UiTheme *t, Gfx *gfx);
void UiTheme_Shutdown(UiTheme *t, Gfx *gfx);

/* 只切换颜色/圆角, 不重建字体; 共享同一个 UiTheme* 的控件下一帧全部换肤。
 * 应用切换后应整窗失效; overlay/Toast layer 通过 themeRev 自动重建/重画。 */
void UiTheme_SetTheme(UiTheme *t, UiThemeId id);

/* 在 base (亮/暗外壳) 上覆盖 5 个语义色, 并自动派生 hover/press/soft/文字.
 * colors 为 NULL 时等价于 UiTheme_SetTheme(t, base).
 * themeRev 会 +1, 进度 overlay 与 Toast layer 在下一帧自动更新. */
void UiTheme_SetColors(UiTheme *t, UiThemeId base, const UiThemeColors *colors);

/* 直接改了 UiTheme 的令牌后调用: themeRev+1, overlay/Toast layer 下一帧重建.
 * SetTheme/SetColors 已自动调用, 只有手改字段时才需要. */
void UiTheme_Touch(UiTheme *t);

#endif /* SCV2_UI_THEME_H */
