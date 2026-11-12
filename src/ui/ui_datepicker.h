/*
 * ui_datepicker.h - 日期选择 (纯日期 / 日期+时间 / 日期范围 / 日期+时间范围)
 *
 * 与 UiPopupManager 配套, 形态镜像 UiSelect:
 *   Ui_DatePicker        每帧画触发器 (内嵌可编辑键入段 + 日历 glyph)
 *   UiDatePicker_DrawPopup  帧末画月历/时间列/页脚 (overlay 层)
 *   UiDatePicker_Key     键盘导航/编辑; 字符与 IME 由应用转发 Char/OnIme
 *
 * 复用: UiPopup_Place 定位, UiScrollView 时间列, UiEdit 键入, UiAnim 补间,
 *       UiIcon MDL2 日历/箭头。日期数学见 ui_datetime.h (0CRT)。
 * 范围: 双联动月历, 两阶段选择 + 悬停预览 + 自动排序。
 * 时间: 时/分 (秒可选) 滚动列; 范围+时间只编辑当前活动端点。
 */
#ifndef CUI_UI_DATEPICKER_H
#define CUI_UI_DATEPICKER_H

#include "edit.h"
#include "ui/ui_context.h"
#include "ui/ui_datetime.h"
#include "ui/ui_popup.h"
#include "ui/ui_scroll.h"
#include "ui/ui_ime.h"

typedef enum {
	UI_DP_DATE = 0,
	UI_DP_DATETIME,
	UI_DP_RANGE_DATE,
	UI_DP_RANGE_DATETIME
} UiDatePickerMode;

typedef struct UiDatePicker {
	UiId id;
	int mode;
	UiDateTime value;      /* 单值 / 范围起点 */
	UiDateTime end;        /* 范围终点 */
	int hasValue;
	int hasEnd;

	int viewY, viewM;      /* 左面板显示月; 右面板 = 下一月 */
	int phase;             /* 范围: 0=选起点 1=选终点 */
	int activeEnd;         /* 范围+时间: 当前编辑端点 0=起 1=止 */
	int hoverDay;          /* 范围预览悬停 day-number, -1=无 */
	int focusDay;          /* 键盘焦点 day-number, -1=无 */

	UiDateTime min, max;
	int hasMin, hasMax;
	int (*disabled)(int dayNumber, void *user);
	void *user;

	int weekStart;         /* 0=日 1=一 .. 6=六; 默认 1 */
	int showSeconds;
	char format[40];

	UiRect anchor;
	UiRect panelRc;
	UiScrollView timeSv[2][3]; /* [端点][时/分/秒]; 范围每端点一套 */
	int timeFocus;         /* 键盘焦点列: -1=日历, 否则 端点*ncol+列 */
	int timeCenter;        /* 位掩码: 下一帧把对应端点时间列选中行居中 */
	int formatSet;         /* 1 = 应用自定义 format, 不再随 mode/秒自动改 */

	UiEdit edit[2];        /* 触发器键入段 [0]=起/单值 [1]=止 */
	UiIme *ime;
	int editActive;        /* 当前聚焦段 -1/0/1 */
	int blinkOn;
	int invalid;           /* 键入解析失败: 错误描边, 提交时回退 */

	int wasOpen;
	int changed;
	int committed;
	char placeholder[96];
} UiDatePicker;

void UiDatePicker_Init(UiDatePicker *dp, UiId id, int mode);
void UiDatePicker_Shutdown(UiDatePicker *dp);
void UiDatePicker_SetMode(UiDatePicker *dp, int mode);
void UiDatePicker_SetValue(UiDatePicker *dp, const UiDateTime *v);
void UiDatePicker_SetRange(UiDatePicker *dp, const UiDateTime *a, const UiDateTime *b);
/* min/max 传 NULL = 该侧不限。 */
void UiDatePicker_SetBounds(UiDatePicker *dp, const UiDateTime *min, const UiDateTime *max);
void UiDatePicker_SetDisabledFn(UiDatePicker *dp, int (*fn)(int dayNumber, void *user), void *user);
void UiDatePicker_SetFormat(UiDatePicker *dp, const char *pattern);
void UiDatePicker_SetWeekStart(UiDatePicker *dp, int ws);
void UiDatePicker_SetShowSeconds(UiDatePicker *dp, int on);
void UiDatePicker_SetPlaceholder(UiDatePicker *dp, const char *text);
void UiDatePicker_AttachIme(UiDatePicker *dp, UiIme *ime);

/* 画触发器; 返回 1 = 本帧值发生变化。 */
int  Ui_DatePicker(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
	UiRect rc, const char *placeholder, int enabled);
/* 帧末调用; 未打开时什么都不做。 */
void UiDatePicker_DrawPopup(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui);
int  UiDatePicker_Key(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui, UINT vk);
int  UiDatePicker_Char(UiDatePicker *dp, UiContext *ui, wchar_t ch);
void UiDatePicker_OnIme(UiDatePicker *dp, UiContext *ui);
void UiDatePicker_Blink(UiDatePicker *dp, UiContext *ui);

#endif /* CUI_UI_DATEPICKER_H */