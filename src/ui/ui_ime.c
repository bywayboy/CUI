/*
 * ui_ime.c - 输入法适配实现 (从 installer/src/app.c 的 App_Ime* 抽出)
 *
 * 关键行为 (都是真机踩过的):
 *   - 禁用输入法必须摘输入上下文 (ImmAssociateContext(hwnd,NULL)), 只改转换模式
 *     各家输入法未必听; 密码框的标准做法就是摘掉;
 *   - 组合串只画进编辑框 (Edit_SetComposition), 不插入正文; 上屏 (GCS_RESULTSTR)
 *     才 Edit_InsertString;
 *   - WM_IME_CHAR 在"已经走过组合消息"的输入法上会重复上屏, 所以 seen=1 后忽略;
 *   - 候选窗要跟插入点 (CANDIDATEFORM), 否则默认落在窗口左上角。
 */
#include <windows.h>
#include <imm.h>
#include <string.h>
#include "ui/ui_ime.h"

void UiIme_Init(UiIme *m, HWND hwnd)
{
	if (!m) return;
	memset(m, 0, sizeof(*m));
	m->hwnd = hwnd;
}

void UiIme_Apply(UiIme *m)
{
	int want = 1;
	if (!m) return;
	if (m->edit) want = Edit_ImeEnabled(m->edit);
	if (!want) {
		if (!m->off) {
			HIMC old = ImmAssociateContext(m->hwnd, NULL);
			if (old) m->ctx = old;
			m->off = 1;
		}
	} else if (m->off) {
		if (m->ctx) ImmAssociateContext(m->hwnd, m->ctx);
		m->ctx = NULL;
		m->off = 0;
	}
	m->seen = 0;   /* 新字段重新探测: 该输入法是否走组合消息 */
}

void UiIme_Cancel(UiIme *m)
{
	HIMC h;
	if (!m || !m->edit) return;
	Edit_ClearComposition(m->edit);
	h = ImmGetContext(m->hwnd);
	if (h) {
		ImmNotifyIME(h, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
		ImmReleaseContext(m->hwnd, h);
	}
}

void UiIme_SetFocus(UiIme *m, UiEdit *e)
{
	if (!m) return;
	UiIme_Cancel(m);
	m->edit = e;
	UiIme_Apply(m);
}

void UiIme_Start(UiIme *m)
{
	if (!m) return;
	m->seen = 1;   /* 该输入法走组合路径: WM_IME_CHAR 不再直插 */
	if (!m->edit) return;
	Edit_ClearComposition(m->edit);
}

void UiIme_Comp(UiIme *m, LPARAM lp)
{
	UiEdit *e;
	HIMC h;
	LONG n, k;
	wchar_t buf[EDIT_MAX];

	if (!m) return;
	m->seen = 1;
	if (!m->edit) return;
	e = m->edit;
	h = ImmGetContext(m->hwnd);
	if (!h) return;

	if (lp & GCS_RESULTSTR) {
		n = ImmGetCompositionStringW(h, GCS_RESULTSTR, NULL, 0);
		if (n > 0) {
			if (n > (LONG)sizeof(buf) - (LONG)sizeof(wchar_t))
				n = (LONG)sizeof(buf) - (LONG)sizeof(wchar_t);
			k = ImmGetCompositionStringW(h, GCS_RESULTSTR, buf, (DWORD)n);
			if (k > 0) Edit_InsertString(e, buf, (int)(k / (LONG)sizeof(wchar_t)));
		}
		Edit_ClearComposition(e);
	}
	if (lp & GCS_COMPSTR) {
		n = ImmGetCompositionStringW(h, GCS_COMPSTR, NULL, 0);
		if (n > 0) {
			LONG cap = (LONG)(EDIT_COMP_MAX - 1) * (LONG)sizeof(wchar_t);
			if (n > cap) n = cap;
			k = ImmGetCompositionStringW(h, GCS_COMPSTR, buf, (DWORD)n);
			Edit_SetComposition(e, buf, k > 0 ? (int)(k / (LONG)sizeof(wchar_t)) : 0);
		} else {
			Edit_ClearComposition(e);
		}
	}
	ImmReleaseContext(m->hwnd, h);
}

void UiIme_End(UiIme *m)
{
	if (!m || !m->edit) return;
	Edit_ClearComposition(m->edit);
}

void UiIme_Char(UiIme *m, wchar_t ch)
{
	/* 走组合消息的输入法在上屏后仍会补发 WM_IME_CHAR; 已由 GCS_RESULTSTR 插入过,
	 * 再插一次就是"双字"。只有从未出现过组合消息的输入法才当普通字符。 */
	if (!m || m->seen) return;
	if (m->edit) Edit_Char(m->edit, ch);
}

void UiIme_Pos(UiIme *m, float scale)
{
	POINT pt;
	HIMC h;
	COMPOSITIONFORM cf;
	CANDIDATEFORM cd;

	if (!m || !m->edit || m->off) return;
	Edit_GetImePoint(m->edit, scale, &pt);
	h = ImmGetContext(m->hwnd);
	if (!h) return;
	memset(&cf, 0, sizeof(cf));
	cf.dwStyle = CFS_POINT;
	cf.ptCurrentPos = pt;
	ImmSetCompositionWindow(h, &cf);
	memset(&cd, 0, sizeof(cd));
	cd.dwIndex = 0;
	cd.dwStyle = CFS_CANDIDATEPOS;
	cd.ptCurrentPos = pt;
	ImmSetCandidateWindow(h, &cd);
	ImmReleaseContext(m->hwnd, h);
}
