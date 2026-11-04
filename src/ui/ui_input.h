/*
 * ui_input.h - 表单输入封装 (单行编辑框集合)
 *
 * 把 demo 里重复的焦点切换/光标闪烁/拖选/键盘/IME/字段失效收进一个组件:
 *   - 字段 ID / 矩形 / hint 由应用每帧设置
 *   - UiInput_Key 处理 Tab/Esc/方向/退格/复制粘贴; 回车返回 0 交给应用提交
 *   - 组合期间回车/ESC/Tab 交给输入法收尾
 */
#ifndef CUI_UI_INPUT_H
#define CUI_UI_INPUT_H

#include "ui/ui_context.h"
#include "ui/ui_ime.h"
#include "edit.h"

#define UI_INPUT_MAX 8

typedef struct {
	UiEdit edit;
	UiRect rc;
	const wchar_t *hint;
	UiId id;
} UiInputField;

typedef struct UiInput {
	UiContext *ui;
	UiIme *ime;
	UiInputField fields[UI_INPUT_MAX];
	int count;
	int focus;        /* -1 = 无 */
	int drag;
	int blinkOn;
} UiInput;

void UiInput_Init(UiInput *in, UiContext *ui, UiIme *ime);
int  UiInput_Add(UiInput *in, UiId id, int maxLen, int password,
	const char *allow, const wchar_t *hint);
UiInputField *UiInput_Field(UiInput *in, int index);
UiEdit *UiInput_Edit(UiInput *in, int index);

void UiInput_BeginFrame(UiInput *in);          /* 设置 Ui_SetFocusId */
void UiInput_SetRect(UiInput *in, int index, UiRect rc);
void UiInput_Draw(UiInput *in, int index);

/* hit 用 Ui_HitTest 的结果; 只有命中字段才聚焦, 命中按钮/toast 不改焦点。 */
void UiInput_MouseDown(UiInput *in, UiId hit, float x);
void UiInput_MouseMove(UiInput *in, float x);
void UiInput_MouseUp(UiInput *in);

int  UiInput_Key(UiInput *in, HWND hwnd, UINT vk);
int  UiInput_Char(UiInput *in, wchar_t ch);
void UiInput_OnIme(UiInput *in);               /* IME 组合变化: 失效聚焦字段 */
void UiInput_Blink(UiInput *in);               /* 530ms 定时器调用 */
int  UiInput_Focused(const UiInput *in);

#endif /* CUI_UI_INPUT_H */
