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

	float panelRadius, buttonRadius, progressRadius, toastRadius;
} UiTheme;

void UiTheme_Init(UiTheme *t, Gfx *gfx);
void UiTheme_Shutdown(UiTheme *t, Gfx *gfx);

/* 只切换颜色/圆角, 不重建字体; 共享同一个 UiTheme* 的控件下一帧全部换肤。
 * 应用切换后应整窗失效; 有 GfxOverlay 时需销毁并在下一帧重建。 */
void UiTheme_SetTheme(UiTheme *t, UiThemeId id);

#endif /* SCV2_UI_THEME_H */
