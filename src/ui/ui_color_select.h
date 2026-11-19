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

#include "ui/ui_context.h"
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

	int drag;            /* 0 无; 1 SV; 2 色相; 3 透明度 */
	int panelFx;         /* 打开首帧登记淡入 */
	int wasOpen;

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
/* 键盘: Esc 关闭面板; 返回 1 = 已消费. */
int  UiColorSelect_Key(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UINT vk);
int  UiColorSelect_IsOpen(const UiPopupManager *pm, const UiColorSelect *cs);

#endif /* CUI_UI_COLOR_SELECT_H */
