/*
 * ui_popup.h - 弹层定位基础
 *
 * 只做纯几何: 根据 anchor、期望方向/对齐、视口尺寸做翻转和夹取。
 * Tooltip/Select/Menu 共用; 不给全局弹层注册表。
 */
#ifndef CUI_UI_POPUP_H
#define CUI_UI_POPUP_H

#include "ui/ui_context.h"

enum {
	UI_POPUP_BELOW = 0,
	UI_POPUP_ABOVE,
	UI_POPUP_RIGHT,
	UI_POPUP_LEFT
};

enum {
	UI_POPUP_ALIGN_START = 0,
	UI_POPUP_ALIGN_CENTER,
	UI_POPUP_ALIGN_END
};

/* 返回 DIP 矩形; 自动在视口内翻转/夹取, 四周留 8 DIP. */
UiRect UiPopup_Place(const UiContext *ui, UiRect anchor, float w, float h,
	int side, int align, float gap);

/*
 * 弹层交互管理器: 同一时刻只允许一个弹层 (Select/Menu/...).
 * 由应用持有, 每窗口一个; 内容结构与绘制仍由具体控件负责。
 *
 * 每帧顺序:
 *   1. UiPopupManager_BeginFrame  (最前; 同步 popupOpen / 处理外部点击)
 *   2. 普通控件绘制
 *   3. UiSelect_DrawPopup / UiMenu_Draw (帧末, overlay 层)
 */
typedef struct UiPopupManager {
	UiId openId;          /* 当前弹层 id, 0 = 关闭 */
	UiId ownerId;         /* 触发器 id, 关闭后焦点回归 */
	UiId scope;           /* 打开时捕获的 inputOwner; 绘制时恢复注册作用域 */
	UiId scopeSaved[4];
	int  scopeDepth;
	int  focusOnClose;    /* 关闭后是否显示键盘焦点样式 */
	int  closed;          /* 单帧: 本帧发生关闭 */
	int  outsidePress;    /* 单帧: 打开期间在弹层外按下 */
} UiPopupManager;

void UiPopupManager_Init(UiPopupManager *pm);

/* 每帧最前调用; 打开期间若在弹层外按下则关闭. */
void UiPopupManager_BeginFrame(UiPopupManager *pm, UiContext *ui);

int  UiPopupManager_IsOpen(const UiPopupManager *pm, UiId id);
int  UiPopupManager_AnyOpen(const UiPopupManager *pm);

/* 打开: owner 用于关闭后焦点回归; focusOnClose 见设计文档. */
void UiPopupManager_Open(UiPopupManager *pm, UiContext *ui, UiId id,
	UiId owner, int focusOnClose);
void UiPopupManager_Close(UiPopupManager *pm, UiContext *ui);

/* 绘制弹层面板前/后调用: 临时把 inputScope 恢复为打开时捕获的 owner,
 * 使帧末绘制、对话框内弹层都能被正确命中. */
void UiPopupManager_BeginDraw(UiPopupManager *pm, UiContext *ui);
void UiPopupManager_EndDraw(UiPopupManager *pm, UiContext *ui);

/* Esc 关闭 (并显示触发器焦点). 返回 1 = 已处理. */
int  UiPopupManager_Key(UiPopupManager *pm, UiContext *ui, UINT vk);

#endif /* CUI_UI_POPUP_H */
