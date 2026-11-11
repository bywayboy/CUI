/*
 * ui_controls.h - Checkbox / RadioGroup / Switch / Slider
 *
 * 全部是应用持有的状态管理器: 状态在结构体里, 每帧返回 changed/committed.
 * 键盘通过 Ui_SetNavId 接入; 鼠标点击会把 navId 指到自己.
 */
#ifndef CUI_UI_CONTROLS_H
#define CUI_UI_CONTROLS_H

#include "ui/ui_context.h"

typedef struct UiCheckbox {
	UiId id;
	int checked;
	int indeterminate;
	int changed;
} UiCheckbox;

void UiCheckbox_Init(UiCheckbox *c, UiId id, int checked);
int  Ui_Checkbox(UiCheckbox *c, UiContext *ui, UiRect rc,
	const char *label, int enabled);
int  UiCheckbox_Key(UiCheckbox *c, UiContext *ui, UINT vk);

typedef struct UiCheckboxGroup {
	UiId id;
	int count;
	unsigned mask;
	unsigned disabledMask;
	int focus;
	int changed;
} UiCheckboxGroup;

void UiCheckboxGroup_Init(UiCheckboxGroup *g, UiId id, int count);
void UiCheckboxGroup_SetMask(UiCheckboxGroup *g, unsigned mask);
void UiCheckboxGroup_SetDisabledMask(UiCheckboxGroup *g, unsigned mask);
int  UiCheckboxGroup_Draw(UiCheckboxGroup *g, UiContext *ui, UiRect rc,
	const char *const *labels, float rowH, int enabled);
int  UiCheckboxGroup_Key(UiCheckboxGroup *g, UiContext *ui, UINT vk);

typedef struct UiRadioGroup {
	UiId id;
	int count;
	int selected;
	unsigned disabledMask;
	int focus;
	int changed;
} UiRadioGroup;

void UiRadioGroup_Init(UiRadioGroup *g, UiId id, int count);
void UiRadioGroup_SetSelected(UiRadioGroup *g, int index);
void UiRadioGroup_SetDisabledMask(UiRadioGroup *g, unsigned mask);
int  UiRadioGroup_Draw(UiRadioGroup *g, UiContext *ui, UiRect rc,
	const char *const *labels, float rowH, int enabled);
int  UiRadioGroup_Key(UiRadioGroup *g, UiContext *ui, UINT vk);

typedef struct UiSwitch {
	UiId id;
	int on;
	int changed;
} UiSwitch;

void UiSwitch_Init(UiSwitch *s, UiId id, int on);
int  Ui_Switch(UiSwitch *s, UiContext *ui, UiRect rc,
	const char *label, int enabled);
int  UiSwitch_Key(UiSwitch *s, UiContext *ui, UINT vk);

typedef struct UiSlider {
	UiId id;
	float value;       /* 0..1 */
	int changed;
	int committed;
} UiSlider;

void  UiSlider_Init(UiSlider *s, UiId id, float value);
void  UiSlider_SetValue(UiSlider *s, float value);
int   Ui_Slider(UiSlider *s, UiContext *ui, UiRect rc,
	const char *label, int enabled);
int   UiSlider_Key(UiSlider *s, UiContext *ui, UINT vk);

#endif /* CUI_UI_CONTROLS_H */
