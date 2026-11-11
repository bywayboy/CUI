/*
 * ui_widgets.h - 主题化基础组件 (Button / Panel / ProgressBar)
 *
 * 全部走 UiContext->theme; 换肤用 UiTheme_SetTheme / UiTheme_SetColors 后整窗重绘。
 * 标签用 UTF-8; 进度条不注册交互, 只负责画轨道/回退填充。
 */
#ifndef CUI_UI_WIDGETS_H
#define CUI_UI_WIDGETS_H

#include "ui/ui_context.h"
#include "ui/ui_icon.h"

enum {
	UI_BTN_SECONDARY = 0,   /* 旧 variant: 兼容包装 */
	UI_BTN_PRIMARY = 1,
	UI_BTN_DANGER = 2,      /* = error */
	UI_BTN_WARNING = 3,
	UI_BTN_SUCCESS = 4,
	UI_BTN_DEFAULT = 0,
	UI_BTN_ERROR = 2
};

/* 新按钮样式: 类型 × 外观 × 形状, 类似 Element Plus. */
typedef enum {
	UI_BTYPE_DEFAULT = 0,
	UI_BTYPE_PRIMARY,
	UI_BTYPE_SUCCESS,
	UI_BTYPE_WARNING,
	UI_BTYPE_ERROR
} UiButtonType;

typedef enum {
	UI_BAPPEAR_SOLID = 0,   /* 实心 */
	UI_BAPPEAR_PLAIN,       /* 浅底 + 彩色文字/描边 */
	UI_BAPPEAR_TEXT         /* 无底无边框 */
} UiButtonAppearance;

typedef enum {
	UI_BSHAPE_RECT = 0,
	UI_BSHAPE_ROUND,        /* 胶囊 */
	UI_BSHAPE_CIRCLE        /* 圆形 */
} UiButtonShape;

enum { UI_BFLAG_DASHED = 1u << 0 };

typedef struct UiButtonStyle {
	int type;         /* UiButtonType */
	int appearance;   /* UiButtonAppearance */
	int shape;        /* UiButtonShape */
	unsigned flags;   /* UI_BFLAG_* */
} UiButtonStyle;

static __inline UiButtonStyle UiButtonStyle_Make(int type, int appearance,
	int shape, unsigned flags)
{
	UiButtonStyle s;
	s.type = type;
	s.appearance = appearance;
	s.shape = shape;
	s.flags = flags;
	return s;
}

int  Ui_ButtonEx(UiContext *ui, UiId id, UiRect rc, const char *label,
	const UiButtonStyle *style, int enabled);

/* 图标 + 文本按钮; icon=NULL 等价 Ui_ButtonEx. label 可为 NULL/空 (纯图标). */
int  Ui_ButtonExIcon(UiContext *ui, UiId id, UiRect rc, const UiIcon *icon,
	const char *label, const UiButtonStyle *style, int enabled);

/* 纯图标按钮 (label=NULL); tooltip 由应用通过 UiTooltip 绑定同一 id. */
int  Ui_IconButton(UiContext *ui, UiId id, UiRect rc, const UiIcon *icon,
	const UiButtonStyle *style, int enabled);

/* 返回 1 = 本帧被点击 (ui->clicked == id)。 */
int  Ui_Button(UiContext *ui, UiId id, UiRect rc, const char *label,
	int variant, int enabled);

/* 卡片/面板底板。 */
void Ui_Panel(UiContext *ui, UiRect rc);

/* 进度条: value 0..1; drawFill=0 时只画轨道 (填充由 GfxOverlay 负责)。 */
void Ui_ProgressBar(UiContext *ui, UiRect rc, float value, int drawFill);

#endif /* CUI_UI_WIDGETS_H */
