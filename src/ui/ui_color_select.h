/*
 * ui_color_select.h - 下拉式颜色选择器 (自持 HSVA)
 *
 * 触发器: 色块 + #RRGGBBAA + chevron; 点击弹出取色面板.
 * 面板:   SV 方形 (饱和度×明度) + 色相条 + 透明度条, 全部可拖拽.
 *
 * 组件内部保存 h(0..360)/s/v/a(0..1), 灰色或全黑时不丢色相;
 * 对外输出 UiColor (0xAARRGGBB). 弹层走 UiPopupManager, 与 UiSelect 一致:
 *
 *   UiColorSelect_Init(&cs, Ui_Id("color", 0, 0), 0xFF3B82F6u);
 *   帧内:     changed   = UiColorSelect_Draw(&cs, &pm, &ui, rc, 1);
 *   帧末:     UiColorSelect_DrawPopup(&cs, &pm, &ui);
 *   键盘:     UiColorSelect_Key(&cs, &pm, &ui, vk);
 *   落库:     if (cs.committed) { save(UiColorSelect_GetColor(&cs)); }
 *
 * changed 每帧清除 (拖动中每帧置 1); committed 在松手/关闭时置 1,
 * 应用在 DrawPopup 之后读取.
 */
#ifndef CUI_UI_COLOR_SELECT_H
#define CUI_UI_COLOR_SELECT_H

#include "edit.h"
#include "ui/ui_context.h"
#include "ui/ui_ime.h"
#include "ui/ui_popup.h"

typedef struct UiColorSelect {
	UiId id;
	float h;             /* 色相 0..360 (灰/黑时仍保留) */
	float s, v, a;       /* 饱和度 / 明度 / 透明度 0..1 */

	int changed;         /* 本帧颜色变化 */
	int committed;       /* 松手/关闭: 应用据此落库 */
	int dirty;           /* 打开期间发生过变化 */

	UiRect anchor;       /* 最近一次触发器矩形 */
	UiRect panelRc;
	UiRect svRc, hueRc, alphaRc;
	UiRect hexRc, aEditRc;

	int drag;            /* 0 无; 1 SV; 2 色相; 3 透明度 */
	int panelFx;         /* 打开首帧登记淡入 */
	int wasOpen;

	/* HEX / 透明度 编辑框 */
	int alphaEnabled;    /* 0 = 固定不透明 (隐藏透明度条与输入框) */
	int focusField;      /* 0 无; 1 HEX; 2 透明度 */
	int blinkOn;
	int editing;         /* 正在编辑: 不回写文本 (焦点停在输入框) */
	UiEdit editHex, editA;
	UiIme *ime;          /* 由应用 Attach; 可为 NULL (纯 ASCII 输入) */

	/* 交互条合成器层: SV/色相/透明度画在独立 GfxLayer 上, 拖动期
	 * 只重绘这一小块; 主窗在拖动期间零重绘. ULW/创建失败回退主窗绘制. */
	UiContext *ui;        /* DrawPopup 时记录, EndFrame/Mouse 使用 */
	void *layer;          /* GfxLayer* */
	int layerW, layerH;
	float layerX, layerY;
	int layerDirty, layerValid, fallback;
	int layerClosing;     /* 关闭淡出中 */
	double closeAt;
	UiRect barsRc;        /* layer 覆盖的绝对矩形 */
	UiRect lSv, lHue, lAlpha;  /* layer 内局部布局 */
	int lAlphaOn;

	/* chevron 旋转: 合成器路径用 part, CPU 路径用本地补间 */
	void *arrowPart;
	unsigned arrowColor;
	int arrowPartTarget;
	float arrowAngle, arrowFrom;
	double arrowT0;
	int arrowTarget, arrowAnim;
} UiColorSelect;

/* 纯函数: h 0..360, s/v/a 0..1. 可单测, 不依赖 UI 上下文. */
UiColor UiColor_HsvToRgb(float h, float s, float v, float a);
void    UiColor_RgbToHsv(UiColor c, float *h, float *s, float *v, float *a);

void UiColorSelect_Init(UiColorSelect *cs, UiId id, UiColor initial);
void UiColorSelect_Shutdown(UiColorSelect *cs);

/* 双向读写; SetColor 在灰/黑时保留原色相. */
void    UiColorSelect_SetColor(UiColorSelect *cs, UiColor c);
void    UiColorSelect_SetHSVA(UiColorSelect *cs, float h, float s, float v,
	float a);
UiColor UiColorSelect_GetColor(const UiColorSelect *cs);
void    UiColorSelect_GetHSVA(const UiColorSelect *cs, float *h, float *s,
	float *v, float *a);

/* 帧内画触发器并处理开/关; 返回 1 = 本帧颜色变化. */
int  UiColorSelect_Draw(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UiRect rc, int enabled);
/* 帧末 (overlay): 面板绘制与拖拽. */
void UiColorSelect_DrawPopup(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui);
/* Gfx_End 之后调用: 同步/重绘交互条 layer (拖动期由 Mouse 后紧跟调用).
 * 关闭时销毁 layer. */
enum {
	UI_COLOR_MOUSE_MOVE = 0,
	UI_COLOR_MOUSE_DOWN = 1,
	UI_COLOR_MOUSE_UP = 2
};
void UiColorSelect_EndFrame(UiColorSelect *cs, double nowMs);
/* 原始鼠标事件 (on_mouse 转发): 拖动条的即时更新, 不依赖主窗出帧.
 * 返回 1 = 已消费. 回退模式返回 0 (走帧内命中). */
int  UiColorSelect_Mouse(UiColorSelect *cs, UiContext *ui, int msg,
	float x, float y);

/* 键盘: Esc 关闭面板; 返回 1 = 已消费. 编辑框聚焦时先走 Edit_Key. */
int  UiColorSelect_Key(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UINT vk);
int  UiColorSelect_IsOpen(const UiPopupManager *pm, const UiColorSelect *cs);

/* 0 = 不提供透明度编辑 (输出 alpha 固定 255, 面板隐藏透明条/输入框).
 * 默认 1. */
void UiColorSelect_SetAlphaEnabled(UiColorSelect *cs, int on);
int  UiColorSelect_AlphaEnabled(const UiColorSelect *cs);

/* HEX / 透明度输入: Attach Ime 后可输入 IME 字符 (纯 ASCII 不需要).
 * Char/OnIme/Blink 由应用转发, 与 UiInput 的接法一致. */
void UiColorSelect_AttachIme(UiColorSelect *cs, UiIme *ime);
int  UiColorSelect_Char(UiColorSelect *cs, UiContext *ui, wchar_t ch);
void UiColorSelect_OnIme(UiColorSelect *cs, UiContext *ui);
void UiColorSelect_Blink(UiColorSelect *cs, UiContext *ui);

#endif /* CUI_UI_COLOR_SELECT_H */
