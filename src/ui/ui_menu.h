/*
 * ui_menu.h - 下拉菜单 / 右键上下文菜单 (延迟绘制的 overlay 弹层)
 *
 *   UiMenu_OpenAt  由按钮点击或右键回调触发
 *   UiMenu_Draw    帧末绘制
 *   UiMenu_Key     打开期间处理方向键/Enter/Esc
 *
 * 支持禁用项、分隔线、危险项、右对齐快捷键提示; 不支持子菜单.
 */
#ifndef CUI_UI_MENU_H
#define CUI_UI_MENU_H

#include "ui/ui_context.h"
#include "ui/ui_popup.h"

enum {
	UI_MENU_DISABLED = 1u << 0,
	UI_MENU_DIVIDER  = 1u << 1,
	UI_MENU_DANGER   = 1u << 2
};

typedef struct UiMenuItem {
	const char *label;
	const char *shortcut;   /* 可为 NULL */
	unsigned flags;         /* UI_MENU_* */
} UiMenuItem;

typedef struct UiMenu {
	UiId id;
	const UiMenuItem *items;
	int count;
	int focus;
	int item;               /* 上次激活项, -1 = 无 */
	int changed;
	int keyFocus;           /* 键盘移动过高亮项 */
	UiRect anchor;
	float rowH;             /* 默认 30 */
	float minW;             /* 默认 160 */
	UiRect panelRc;         /* 最近一帧面板矩形 */
} UiMenu;

void UiMenu_Init(UiMenu *m, UiId id);
void UiMenu_SetItems(UiMenu *m, const UiMenuItem *items, int count);

/* owner 用于关闭后焦点回归 (右键菜单可传 0). */
void UiMenu_OpenAt(UiMenu *m, UiPopupManager *pm, UiContext *ui, UiId owner,
	UiRect anchor, int focusOnClose);

void UiMenu_Draw(UiMenu *m, UiPopupManager *pm, UiContext *ui);

/* 返回 1 = 已消费. */
int  UiMenu_Key(UiMenu *m, UiPopupManager *pm, UiContext *ui, UINT vk);

#endif /* CUI_UI_MENU_H */
