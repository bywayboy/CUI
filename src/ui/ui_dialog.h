#ifndef CUI_UI_DIALOG_H
#define CUI_UI_DIALOG_H

#include "ui/ui_context.h"
#include "ui/ui_ime.h"

enum {
	UI_DIALOG_NONE = 0,
	UI_DIALOG_ACCEPT,
	UI_DIALOG_CANCEL
};

typedef struct UiDialog {
	UiId id;
	UiId savedNav, savedFocus;
	int savedFocusVisible;
	UiIme *ime;
	UiEdit *savedEdit;
	const UiId *focusOrder;
	int focusCount;
	UiId actions[2];
	int confirmMode;
	int closeOnBackdrop;
	int visible, closing;
	int pendingResult, result;
	/* 入场/离场由合成器 layer 承担: 1=入场 2=稳定 3=离场 */
	int stage;
	double stageMs;
	GfxLayer *dimLayer, *cardLayer;
	int layerPxW, layerPxH;
	int dimDirty;
	int kicked;              /* 入场动画已交给合成器 */
	int fxHeld;              /* 持有 ctx->fxOff */
	UiRect rect, body;
	UiId savedScope;
	int begun, capture, inputBlocked, ownerPushed;
} UiDialog;

void UiDialog_Init(UiDialog *d, UiId id);
void UiDialog_AttachIme(UiDialog *d, UiIme *ime);
/* 借用数组至关闭；只包含内容控件，关闭键自动加入。 */
void UiDialog_SetFocusOrder(UiDialog *d, const UiId *ids, int count);
int  UiDialog_Open(UiDialog *d, UiContext *ui, UiId owner);
void UiDialog_Close(UiDialog *d, UiContext *ui, int result);
int  UiDialog_IsOpen(const UiDialog *d);
int  UiDialog_Busy(const UiDialog *d);
void UiDialog_Tick(UiDialog *d, UiContext *ui, double dtMs);
int  UiDialog_TakeResult(UiDialog *d);

/* Begin 返回 1 时必须 End；body 仅供布局，不裁剪描边、光晕或弹层。 */
int  UiDialog_Begin(UiDialog *d, UiContext *ui, const char *title,
	float width, float height, UiRect *body);
void UiDialog_End(UiDialog *d, UiContext *ui);
/* 返回 0 的按键只应继续交给 Dialog 子控件，不得派发给背景。 */
int  UiDialog_Key(UiDialog *d, UiContext *ui, UINT vk);
void Ui_ConfirmDialog(UiDialog *d, UiContext *ui, const char *title,
	const char *message, const char *acceptText, const char *cancelText, int danger);

#endif
