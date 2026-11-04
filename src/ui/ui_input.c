#include <windows.h>
#include <string.h>
#include "ui/ui_input.h"

static void input_invalidate(UiInput *in, int f)
{
	RECT r;
	float s;
	UiRect rc;
	if (!in || !in->ui || !in->ui->hwnd) return;
	if (f < 0 || f >= in->count) return;
	rc = in->fields[f].rc;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return;
	s = in->ui->scale > 0.0f ? in->ui->scale : 1.0f;
	r.left = (LONG)((rc.x - 12.0f) * s);
	r.top = (LONG)((rc.y - 12.0f) * s);
	r.right = (LONG)((rc.x + rc.w + 12.0f) * s + 1.0f);
	r.bottom = (LONG)((rc.y + rc.h + 12.0f) * s + 1.0f);
	InvalidateRect(in->ui->hwnd, &r, FALSE);
}

static void input_focus(UiInput *in, int f)
{
	int old;
	if (!in) return;
	if (f < -1 || f >= in->count) f = -1;
	old = in->focus;
	if (f == old) return;
	in->focus = f;
	in->blinkOn = 1;
	UiIme_SetFocus(in->ime, f >= 0 ? &in->fields[f].edit : NULL);
	input_invalidate(in, old);
	input_invalidate(in, f);
}

static int input_hit(UiInput *in, UiId hit)
{
	int i;
	if (!in) return -1;
	for (i = 0; i < in->count; i++)
		if (in->fields[i].id == hit) return i;
	return -1;
}

void UiInput_Init(UiInput *in, UiContext *ui, UiIme *ime)
{
	if (!in) return;
	ZeroMemory(in, sizeof(*in));
	in->ui = ui;
	in->ime = ime;
	in->focus = -1;
	in->drag = -1;
	in->blinkOn = 1;
}

int UiInput_Add(UiInput *in, UiId id, int maxLen, int password,
	const char *allow, const wchar_t *hint)
{
	int i;
	if (!in || in->count >= UI_INPUT_MAX) return -1;
	i = in->count++;
	Edit_Init(&in->fields[i].edit, maxLen, password, allow);
	in->fields[i].hint = hint;
	in->fields[i].id = id ? id : Ui_Id("input.field", i, 0);
	in->fields[i].rc = Ui_Rect(0, 0, 0, 0);
	return i;
}

UiInputField *UiInput_Field(UiInput *in, int index)
{
	if (!in || index < 0 || index >= in->count) return NULL;
	return &in->fields[index];
}

UiEdit *UiInput_Edit(UiInput *in, int index)
{
	UiInputField *f = UiInput_Field(in, index);
	return f ? &f->edit : NULL;
}

void UiInput_BeginFrame(UiInput *in)
{
	if (!in) return;
	Ui_SetFocusId(in->ui, in->focus >= 0 ? in->fields[in->focus].id : UI_ID_NONE);
}

void UiInput_SetRect(UiInput *in, int index, UiRect rc)
{
	if (!in || index < 0 || index >= in->count) return;
	in->fields[index].rc = rc;
}

void UiInput_Draw(UiInput *in, int index)
{
	UiInputField *f;
	if (!in || index < 0 || index >= in->count) return;
	f = &in->fields[index];
	f->edit.focused = (in->focus == index);
	f->edit.caretOn = in->blinkOn;
	Edit_Draw(in->ui, f->id, f->rc, &f->edit, f->hint);
}

void UiInput_MouseDown(UiInput *in, UiId hit, float x)
{
	int f;
	UiInputField *fd;
	if (!in) return;
	f = input_hit(in, hit);
	if (f >= 0) {
		fd = &in->fields[f];
		input_focus(in, f);
		Edit_PlaceCaret(&fd->edit, in->ui->gfx, in->ui->theme->fInput, fd->rc, x);
		input_invalidate(in, f);
		in->drag = f;
		in->blinkOn = 1;
	} else if (hit == UI_ID_NONE) {
		input_focus(in, -1);
	}
}

void UiInput_MouseMove(UiInput *in, float x)
{
	UiInputField *fd;
	if (!in || in->drag < 0) return;
	fd = &in->fields[in->drag];
	Edit_DragTo(&fd->edit, in->ui->gfx, in->ui->theme->fInput, fd->rc, x);
	input_invalidate(in, in->drag);
}

void UiInput_MouseUp(UiInput *in)
{
	if (!in || in->drag < 0) return;
	Edit_StopDrag(&in->fields[in->drag].edit);
	in->drag = -1;
}

int UiInput_Key(UiInput *in, HWND hwnd, UINT vk)
{
	UiEdit *e;
	int shift;
	if (!in) return 0;
	e = in->focus >= 0 ? &in->fields[in->focus].edit : NULL;
	shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

	/* 组合期间的回车/ESC/Tab 先让输入法收尾 */
	if (e && Edit_HasComposition(e)
		&& (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB))
		return 1;

	if (vk == VK_TAB) {
		if (in->count <= 0) return 1;
		if (in->focus < 0) input_focus(in, shift ? in->count - 1 : 0);
		else input_focus(in, (in->focus + (shift ? in->count - 1 : 1)) % in->count);
		return 1;
	}
	if (vk == VK_ESCAPE) {
		input_focus(in, -1);
		return 1;
	}
	if (vk == VK_RETURN) return 0;    /* 交给应用提交 */
	if (e && Edit_Key(e, hwnd, vk)) {
		in->blinkOn = 1;
		input_invalidate(in, in->focus);
		return 1;
	}
	return 0;
}

int UiInput_Char(UiInput *in, wchar_t ch)
{
	UiEdit *e;
	if (!in) return 0;
	e = in->focus >= 0 ? &in->fields[in->focus].edit : NULL;
	if (e && Edit_Char(e, ch)) {
		in->blinkOn = 1;
		input_invalidate(in, in->focus);
		return 1;
	}
	return 0;
}

void UiInput_OnIme(UiInput *in)
{
	if (in) input_invalidate(in, in->focus);
}

void UiInput_Blink(UiInput *in)
{
	if (!in) return;
	in->blinkOn = !in->blinkOn;
	input_invalidate(in, in->focus);
}

int UiInput_Focused(const UiInput *in)
{
	return in ? in->focus : -1;
}
