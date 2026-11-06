/*
 * ui_button_group.h - 融合按钮组管理器
 *
 * 一个 UiButtonGroup 同时管理:
 *   - 视觉: 相邻按钮共用外框/圆角, 内部 1px 分隔线
 *   - 状态: NORMAL 无选择 / SINGLE 单选 / MULTI 位掩码
 *   - 键盘: focus index + 方向键/Home/End/Space
 *
 * 状态由应用持有 (通常是 ViewModel 的字段), 不是全局注册表。
 * 绘制返回本帧 clicked index, 同时更新 selected/mask/changed/clicked。
 */
#ifndef CUI_UI_BUTTON_GROUP_H
#define CUI_UI_BUTTON_GROUP_H

#include "ui/ui_widgets.h"

enum {
	UI_BG_NORMAL = 0,   /* 普通动作按钮分组, 不选中 */
	UI_BG_SINGLE = 1,   /* 单选 */
	UI_BG_MULTI  = 2    /* 多选, mask 位图 */
};

typedef struct UiButtonGroup {
	UiId id;
	int mode;
	int count;
	int selected;       /* SINGLE: 0..count-1, -1 = 无 */
	unsigned mask;      /* MULTI: 每位一个选项 */
	int focus;          /* 键盘焦点 index, -1 = 无 */
	int focusVisible;   /* 1 = 显示焦点环 (键盘导航); 鼠标点击置 0 */
	int changed;        /* 本帧 selected/mask 变化 */
	int clicked;        /* 本帧点击/键盘激活 index, -1 = 无 */
	UiRect rc;          /* 最近一次绘制矩形 (键盘失效用) */
} UiButtonGroup;

void UiButtonGroup_Init(UiButtonGroup *g, UiId id, int mode, int count);
void UiButtonGroup_SetCount(UiButtonGroup *g, int count);
void UiButtonGroup_SetSelected(UiButtonGroup *g, int index);
void UiButtonGroup_SetMask(UiButtonGroup *g, unsigned mask);

/* 键盘焦点环可见性: Tab 进入组时由应用置 1; 鼠标点击会自动置 0,
 * 方向键/Space/Enter 会重新置 1 (focus-visible 语义). */
void UiButtonGroup_SetFocusVisible(UiButtonGroup *g, int visible);

/* labels 长度 >= count; widths=NULL 等分, enabled=NULL 全可用.
 * variant 选择选中段的强调角色 (PRIMARY/SECONDARY -> primary,
 * WARNING/SUCCESS/DANGER -> 对应角色). 返回 clicked index 或 -1. */
int UiButtonGroup_Draw(UiContext *ui, UiButtonGroup *g, UiRect rc,
	const char *const *labels, const float *widths,
	const unsigned char *enabled, int variant);

/* 组获得键盘焦点时处理方向键/Home/End/Space/Enter; 返回 1 = 已处理. */
int UiButtonGroup_Key(UiContext *ui, UiButtonGroup *g, UINT vk);

#endif /* CUI_UI_BUTTON_GROUP_H */
