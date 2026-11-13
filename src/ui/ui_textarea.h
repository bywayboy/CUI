/*
 * ui_textarea.h - 多行纯文本编辑器 (动态容量 + 软换行 + 垂直滚动 + 选区 + IME)
 *
 * 文本/行索引/UTF-8 缓冲都按需增长, 不再有固定字符上限; maxLen=0 表示不限长.
 * 布局采用增量策略: 末尾追加只排版新字符; 普通字符编辑只重排当前段落;
 * 换行插入/删除或宽度变化才全文重排.
 *
 * IME 通过一个 UiEdit 代理接收上屏/组合串, 每次 Draw/Key/Char/OnIme 前排入正文.
 * 滚动复用 UiScrollView; 字段 ID 同时作为滚动视口 ID.
 */
#ifndef CUI_UI_TEXTAREA_H
#define CUI_UI_TEXTAREA_H

#include "edit.h"
#include "ui/ui_context.h"
#include "ui/ui_ime.h"
#include "ui/ui_scroll.h"

typedef struct UiTextArea {
	UiId id;
	wchar_t *text;         /* 动态增长 */
	int cap;               /* 分配的 wchar_t 数 (含结尾 0) */
	int len;
	int caret, anchor;
	int maxLen;            /* 0 = 不限长 */
	const wchar_t *placeholder;
	int readonly;
	int changed;           /* 本帧文本被修改 */

	UiEdit imeEdit;        /* IME 代理: 上屏文本 + 组合串 */
	UiIme *ime;

	UiScrollView sv;
	UiRect rc;             /* 最近一次字段矩形 */
	UiRect textRc;         /* 本帧文本视口 (sv.inner) */
	float lineH;
	float lineScale;       /* lineH 对齐设备像素时使用的 scale */
	float wrapW;
	float lastWrapW;
	float drawOffset;      /* 设备像素对齐的绘制位移: 只用于绘制, 不回写 */

	int *lineStart;        /* 动态行索引 */
	int *lineLen;
	float *lineW;
	int lineCap;
	int lineCount;

	/* 增量布局: 0=clean, 1=full, 2=append, 3=paragraph reflow */
	int layoutMode;
	int layoutLen;         /* 上次干净布局对应的文本长度 */
	int appendOldLen;      /* append 模式的基线长度 */
	int reflowPos;         /* reflow 模式的最早编辑位置 */
	int reflowDelta;       /* 当前 len - layoutLen */

	int *scratchStart;     /* reflow 临时行 */
	int *scratchLen;
	float *scratchW;
	int scratchCap;

	int ensureCaret;
	int dragging;
	int blinkOn;
	int wasFocused;
	double lastTickNow;   /* 平滑滚动用真实帧间隔 */
	float xPref;
	int hasXPref;

	char *u8;              /* UTF-8 转换缓冲, 动态 */
	int u8cap;
} UiTextArea;

void UiTextArea_Init(UiTextArea *ta, UiId id, int maxLen);
void UiTextArea_Shutdown(UiTextArea *ta);
void UiTextArea_SetText(UiTextArea *ta, const wchar_t *s);
void UiTextArea_SetTextUtf8(UiTextArea *ta, const char *s);
void UiTextArea_SetCaret(UiTextArea *ta, int pos);
void UiTextArea_TextUtf8(const UiTextArea *ta, char *out, int cap);
void UiTextArea_TextW(const UiTextArea *ta, wchar_t *out, int cap);
void UiTextArea_SetPlaceholder(UiTextArea *ta, const wchar_t *hint);
void UiTextArea_SetReadonly(UiTextArea *ta, int readonly);
void UiTextArea_AttachIme(UiTextArea *ta, UiIme *ime);

/* 滚轮平滑时间常数 (ms): 0 = 即时; 默认 100. 光标定位/拖选自动滚动始终
 * 即时, 不受此设置影响. */
void UiTextArea_SetScrollSmoothing(UiTextArea *ta, float tauMs);
/* 平滑滚动/滚动条淡入等动画进行中: app 可用于 animating 回调持续出帧. */
int  UiTextArea_Busy(const UiTextArea *ta);

/* 画字段; 返回 1 = 本帧文本被修改. */
int  Ui_TextArea(UiTextArea *ta, UiContext *ui, UiRect rc, int enabled);

int  UiTextArea_Key(UiTextArea *ta, UiContext *ui, UINT vk);
int  UiTextArea_Char(UiTextArea *ta, UiContext *ui, wchar_t ch);
void UiTextArea_OnIme(UiTextArea *ta, UiContext *ui);
void UiTextArea_Blink(UiTextArea *ta, UiContext *ui);

#endif /* CUI_UI_TEXTAREA_H */
