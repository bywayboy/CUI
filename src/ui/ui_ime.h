/*
 * ui_ime.h - 自绘编辑框的输入法 (IMM32) 适配
 *
 * 为什么需要它: UiEdit 是自绘控件, 没有原生 EDIT 的输入上下文; 中文/日文输入法的
 * 组合串、候选窗、以及"密码框禁用输入法"都要应用自己接线。这里把安装器里验证过的
 * 那套逻辑抽成可复用模块 (来源: installer/src/app.c 的 App_Ime*)。
 *
 * 接线 (窗口过程):
 *   WM_SETFOCUS            -> UiIme_Apply()
 *   WM_KILLFOCUS           -> UiIme_Cancel()
 *   WM_IME_SETCONTEXT      -> 先 UiIme_Pos(), 再 DefWindowProc(lParam 去掉
 *                             ISC_SHOWUICOMPOSITIONWINDOW) —— 组合串由 Edit_Draw 自绘
 *   WM_IME_STARTCOMPOSITION-> UiIme_Start(); UiIme_Pos()
 *   WM_IME_COMPOSITION     -> UiIme_Comp(lp); UiIme_Pos()
 *   WM_IME_ENDCOMPOSITION  -> UiIme_End()
 *   WM_IME_CHAR            -> UiIme_Char(wp)
 * 聚焦字段变化时: UiIme_SetFocus(&ime, 新字段或 NULL)
 */
#ifndef CUI_UI_IME_H
#define CUI_UI_IME_H

#include <windows.h>
#include <imm.h>
#include "edit.h"

typedef struct UiIme {
	HWND    hwnd;
	UiEdit *edit;     /* 当前聚焦字段; NULL = 无 */
	HIMC    ctx;      /* 禁用输入法时摘下来的输入上下文 */
	int     off;      /* 1 = 当前已摘掉 (ImmAssociateContext(NULL)) */
	int     seen;     /* 当前字段收到过组合消息: WM_IME_CHAR 不再直插 (防双字) */
} UiIme;

void UiIme_Init(UiIme *m, HWND hwnd);

/* 切换聚焦字段: 取消旧字段未上屏的组合, 按新字段的开关落实输入法, 重置探测标志。 */
void UiIme_SetFocus(UiIme *m, UiEdit *e);

/* 按当前字段启用/禁用输入法 (禁用 = ImmAssociateContext(hwnd, NULL))。 */
void UiIme_Apply(UiIme *m);

/* 取消未上屏的组合 (失焦/切字段前调用)。 */
void UiIme_Cancel(UiIme *m);

void UiIme_Start(UiIme *m);
void UiIme_Comp(UiIme *m, LPARAM lp);
void UiIme_End(UiIme *m);
void UiIme_Char(UiIme *m, wchar_t ch);

/* 组合窗/候选窗跟随插入点; scale 为 DIP->物理像素比例。 */
void UiIme_Pos(UiIme *m, float scale);

#endif /* CUI_UI_IME_H */
