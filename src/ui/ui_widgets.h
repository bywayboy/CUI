/*
 * ui_widgets.h - 主题化基础组件 (Button / Panel / ProgressBar)
 *
 * 全部走 UiContext->theme; 换肤用 UiTheme_SetTheme / UiTheme_SetColors 后整窗重绘。
 * 标签用 UTF-8; 进度条不注册交互, 只负责画轨道/回退填充。
 */
#ifndef CUI_UI_WIDGETS_H
#define CUI_UI_WIDGETS_H

#include "ui/ui_context.h"

enum {
	UI_BTN_SECONDARY = 0,   /* = default: 中性按钮 */
	UI_BTN_PRIMARY = 1,
	UI_BTN_DANGER = 2,      /* = error */
	UI_BTN_WARNING = 3,
	UI_BTN_SUCCESS = 4,
	UI_BTN_DEFAULT = 0,
	UI_BTN_ERROR = 2
};

/* 返回 1 = 本帧被点击 (ui->clicked == id)。 */
int  Ui_Button(UiContext *ui, UiId id, UiRect rc, const char *label,
	int variant, int enabled);

/* 卡片/面板底板。 */
void Ui_Panel(UiContext *ui, UiRect rc);

/* 进度条: value 0..1; drawFill=0 时只画轨道 (填充由 GfxOverlay 负责)。 */
void Ui_ProgressBar(UiContext *ui, UiRect rc, float value, int drawFill);

#endif /* CUI_UI_WIDGETS_H */
