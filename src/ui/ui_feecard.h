/*
 * ui_feecard.h - 费率卡片 (7x24 小时单价编辑器)
 *
 * 纯 UI 组件: 价格按 "分" 存储, 输入框按 "元" 显示; 保存/删除/校验结果
 * 通过回调交给宿主. 弹层/对话框在帧末调用 UiFeeCard_DrawOverlay.
 *
 * 输入转发 (宿主统一接线):
 *   mouse: UiFeeCard_MouseDown/Move/Up
 *   key:   UiFeeCard_Key
 *   char:  UiFeeCard_Char
 *   IME:   UiFeeCard_OnIme
 *   530ms: UiFeeCard_Blink
 *   tick:  UiFeeCard_Tick(ui, dtMs)
 */
#ifndef CUI_UI_FEECARD_H
#define CUI_UI_FEECARD_H

#include "ui/ui_context.h"
#include "ui/ui_dialog.h"
#include "ui/ui_ime.h"
#include "ui/ui_input.h"
#include "ui/ui_popup.h"
#include "ui/ui_select.h"
#include "ui/ui_controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_FEE_CARD_DAYS  7
#define UI_FEE_CARD_HOURS 24
#define UI_FEE_CARD_LEN   (UI_FEE_CARD_DAYS * UI_FEE_CARD_HOURS)

typedef struct UiFeePrice {
	int price;       /* 分/小时 */
	int pack;        /* 1 = 包时封顶 */
	int packPrice;   /* 封顶价 (分) */
} UiFeePrice;

typedef struct UiFeeModel {
	int startPrice;  /* 分 */
	int minPrice;    /* 分 */
	int ignoreTime;  /* 分钟 */
	int timeUnit;    /* 分钟 */
	int discount;    /* 0..100 */
	UiFeePrice prices[UI_FEE_CARD_LEN];
} UiFeeModel;

typedef struct UiFeeCardInfo {
	const char *title;
	int hasFee;       /* 0 = 未配置 */
	int active;       /* hasFee 时有效 */
	int refCount;
	const char *updatedAt; /* 宿主格式化好的时间文本, 可为 NULL */
	int canCreate;
	int canUpdate;
	int canDelete;
	int saving;
} UiFeeCardInfo;

typedef struct UiFeeCardCallbacks {
	void (*save)(void *user, struct UiFeeCard *card, const UiFeeModel *model);
	void (*delete_)(void *user, struct UiFeeCard *card);
	void (*invalid)(void *user, struct UiFeeCard *card, const char *message);
} UiFeeCardCallbacks;

enum {
	UI_FEE_CARD_EV_SAVE = 1u << 0,
	UI_FEE_CARD_EV_DELETE = 1u << 1,
	UI_FEE_CARD_EV_INVALID = 1u << 2,
	UI_FEE_CARD_EV_CHANGED = 1u << 3
};

typedef struct UiFeeCard {
	UiId id;
	UiId fieldId[4];
	UiId unitId, gridId, saveId, delId;
	UiId gridFieldId[2], packId, gridOkId, gridCancelId;
	UiPopupManager *pm;
	UiIme *ime;

	char title[128];
	char updatedAt[96];
	int hasFee, active, refCount;
	int canCreate, canUpdate, canDelete, saving;

	UiFeeModel form;
	UiFeeModel src;
	int hasSrc;
	int dirty;
	int inited;

	UiInput fields;          /* 起步价/最小收费/忽略时间/折扣 */
	UiSelect unit;
	char unitLabels[8][16];
	const char *unitItems[8];
	int unitValues[8];
	int unitCount;
	int lastFocus;
	int changedThisFrame;

	UiDialog gridDlg;
	UiInput gridFields;      /* 小时单价/封顶价 */
	UiSwitch pack;
	UiDialog confirmDlg;
	char confirmText[256];

	unsigned char selected[UI_FEE_CARD_LEN];
	int selCount;
	int selMin, selMax;

	int dragActive, dragStart, dragCur;
	int dragMoved;
	uint64_t lastClickMs;
	int lastClickCell;
	UiRect gridRect;
	float gridDayW, gridHeadH, gridRowH, gridCellW;

	char error[128];
	UiFeeCardCallbacks cb;
	void *cbUser;
} UiFeeCard;

void UiFeeCard_Init(UiFeeCard *card, UiId id,
	UiPopupManager *pm, UiIme *ime);
void UiFeeCard_Shutdown(UiFeeCard *card);

void UiFeeCard_SetInfo(UiFeeCard *card, const UiFeeCardInfo *info);
void UiFeeCard_SetCallbacks(UiFeeCard *card,
	const UiFeeCardCallbacks *cb, void *user);

/* NULL = 未配置 (恢复默认值). 表单脏时不覆盖用户编辑; 用 Reset 强制回显. */
void UiFeeCard_SetFee(UiFeeCard *card, const UiFeeModel *fee);
void UiFeeCard_Reset(UiFeeCard *card);

const UiFeeModel *UiFeeCard_Form(const UiFeeCard *card);
int  UiFeeCard_IsDirty(const UiFeeCard *card);
/* 1 = 通过; 失败时 *message 指向静态文案. */
int  UiFeeCard_Validate(const UiFeeCard *card, const char **message);

/* 建议高度; 宽度不足 720 时标量区改两行. */
float UiFeeCard_MeasureHeight(float width);

/* 绘制卡片; 返回 UI_FEE_CARD_EV_* 位. */
int  Ui_FeeCard(UiFeeCard *card, UiContext *ui, UiRect rc);

/* 帧末: 计费单位下拉 + 网格编辑对话框 + 删除确认. */
void UiFeeCard_DrawOverlay(UiFeeCard *card, UiContext *ui);

void UiFeeCard_Tick(UiFeeCard *card, UiContext *ui, double dtMs);

void UiFeeCard_MouseDown(UiFeeCard *card, UiContext *ui, float x, float y);
void UiFeeCard_MouseMove(UiFeeCard *card, float x, float y);
void UiFeeCard_MouseUp(UiFeeCard *card);
int  UiFeeCard_Key(UiFeeCard *card, UiContext *ui, UINT vk);
int  UiFeeCard_Char(UiFeeCard *card, wchar_t ch);
void UiFeeCard_OnIme(UiFeeCard *card);
void UiFeeCard_Blink(UiFeeCard *card);
int  UiFeeCard_Busy(const UiFeeCard *card);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_FEECARD_H */
