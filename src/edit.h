/*
 * edit.h - 单行自绘编辑框（0CRT，不使用原生 EDIT 子窗口）
 *
 * 交互逻辑参照 DuiLib 的 CEditUI/CRichEditUI：
 *   光标/选择/鼠标拖选、Home/End/方向键、Backspace/Delete、
 *   Ctrl+A/C/X/V、字符过滤、最大长度、密码模式、水平滚动、IME 组合窗口定位。
 * 文本用 UTF-16 保存；绘制时转 UTF-8 交给 gfx（DirectWrite）。
 */
#ifndef INST_EDIT_H
#define INST_EDIT_H

#include <windows.h>
#include "ui/ui_context.h"

#define EDIT_MAX 512
#define EDIT_COMP_MAX 64   /* IME 组合串上限 (拼音/五笔的一整段远小于此) */

typedef struct UiEdit {
	wchar_t text[EDIT_MAX];
	int len;
	int caret;
	int anchor;          /* 选择锚点; anchor != caret 即有选区 */
	int maxLen;
	int password;
	const char *allow;   /* 非 NULL: 只接受这些 ASCII 字符 */
	int noIme;           /* 1 = 该字段禁用输入法 (密码); 由 app 在聚焦时落实 */
	int focused;
	int dragging;
	int caretOn;         /* 由 app 的光标闪烁定时器驱动 */
	float scrollX;       /* DIP */
	/* IME 组合串 (未上屏): 只用于就地绘制 + 下划线, 不参与 text/caret */
	wchar_t comp[EDIT_COMP_MAX];
	int compLen;
	/* 最近一次绘制结果（IME 定位用） */
	int hasRect;
	UiRect lastRc;
	float caretDipX;
	float textTopDip;
	float textHDip;
} UiEdit;

void Edit_Init(UiEdit *e, int maxLen, int password, const char *allow);
void Edit_SetText(UiEdit *e, const wchar_t *s);
void Edit_SetTextUtf8(UiEdit *e, const char *s);
void Edit_TextUtf8(const UiEdit *e, char *out, int cap);
void Edit_TextW(const UiEdit *e, wchar_t *out, int cap);
int  Edit_Empty(const UiEdit *e);

void Edit_Draw(UiContext *ui, UiId id, UiRect rc, UiEdit *e, const wchar_t *hint);
void Edit_PlaceCaret(UiEdit *e, Gfx *g, GfxFont *f, UiRect rc, float mouseX);
void Edit_DragTo(UiEdit *e, Gfx *g, GfxFont *f, UiRect rc, float mouseX);
void Edit_StopDrag(UiEdit *e);

int  Edit_Key(UiEdit *e, HWND hwnd, UINT vk);   /* 1 = 已处理 */
int  Edit_Char(UiEdit *e, wchar_t ch);          /* 1 = 已处理 */
int  Edit_InsertString(UiEdit *e, const wchar_t *s, int n);  /* 返回实际插入的字符数 */

/* ── IME (IMM32) ────────────────────────────────────────────────────
 * 禁用输入法: 只是给字段打标记; 真正"摘掉/装回"窗口的输入上下文由 app 在
 * 聚焦切换时做 (ImmAssociateContext(NULL)), 所以同一时刻只有聚焦字段生效。
 * 组合串由本组件就地绘制 (下划线); app 收到 WM_IME_COMPOSITION 后转给下面两个函数。 */
void Edit_SetImeEnabled(UiEdit *e, int on);
int  Edit_ImeEnabled(const UiEdit *e);
void Edit_SetComposition(UiEdit *e, const wchar_t *s, int n);  /* NULL/n<=0 = 清空 */
void Edit_ClearComposition(UiEdit *e);
int  Edit_HasComposition(const UiEdit *e);

void Edit_GetImePoint(const UiEdit *e, float scale, POINT *pt);

#endif /* INST_EDIT_H */
