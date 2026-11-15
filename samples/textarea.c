/*
 * textarea.c - 多行文本样例: 软换行 / 滚动 / 选区 / IME / readonly / disabled
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_icon.h"
#include "ui/ui_layout.h"
#include "ui/ui_textarea.h"
#include "ui/ui_widgets.h"

typedef struct TextAreaDemo {
	UiApp *app;
	UiTextArea edit;
	UiTextArea ro;
	UiTextArea disabled;
	UiId themeBtn;
	char status[192];
} TextAreaDemo;

static TextAreaDemo g_demo;

static const char kSample[] =
	"第一行：多行文本支持软换行、垂直滚动和中文输入法。\n"
	"第二行：这是一段很长的英文文本，用来验证自动换行是否正确工作。"
	"The quick brown fox jumps over the lazy dog. "
	"The quick brown fox jumps over the lazy dog.\n"
	"\n"
	"第四行：Ctrl+A/C/X/V、Shift+方向键、Home/End、Ctrl+Home/End 均可使用。\n"
	"第五行：滚轮带平滑尾随（约 100ms），光标定位与拖选自动滚动为即时。\n"
	"第六行：滚动只绘制可见行，成本与文档规模无关。\n"
	"第七行：继续输入可验证动态扩容，不受固定缓冲区限制。\n"
	"第八行：IME 组合串显示在光标位置，上屏后进入撤销无关的普通编辑流。\n"
	"第九行：选区跨行时按行绘制高亮，末行空白参与选择。\n"
	"第十行：软换行宽度随字段宽度变化，重新排版只发生在宽度改变时。\n"
	"第十一行：Home/End 按视觉行移动，Ctrl+Home/End 到全文首尾。\n"
	"第十二行：此段用于制造超出视口的内容，方便观察滚动与滚动条。\n"
	"第十三行：滚动条 thumb 支持拖拽，拖动期间内容即时跟随。\n"
	"第十四行：松开鼠标后焦点仍停留在文本框，可直接继续输入。";

static void set_status(TextAreaDemo *d, const char *s)
{
	int i = 0;
	while (s && s[i] && i < (int)sizeof(d->status) - 1) {
		d->status[i] = s[i];
		i++;
	}
	d->status[i] = 0;
	UiApp_Invalidate(d->app);
}

static void on_init(UiApp *app, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	d->app = app;
	app->minW = 720.0f;
	app->minH = 560.0f;

	UiTextArea_Init(&d->edit, Ui_Id("ta.edit", 0, 0), 0);   /* 0 = 不限长 */
	UiTextArea_SetPlaceholder(&d->edit, L"输入备注…（支持多行、IME、选择）");
	UiTextArea_AttachIme(&d->edit, &app->ime);
	UiTextArea_SetTextUtf8(&d->edit, kSample);
	UiTextArea_SetCaret(&d->edit, 0);

	UiTextArea_Init(&d->ro, Ui_Id("ta.ro", 0, 0), 0);
	UiTextArea_SetReadonly(&d->ro, 1);
	UiTextArea_SetTextUtf8(&d->ro,
		"只读文本：可以选择、复制，但不能修改。\n"
		"Ctrl+C 可用；Ctrl+X/V 被忽略。");

	UiTextArea_Init(&d->disabled, Ui_Id("ta.disabled", 0, 0), 0);
	UiTextArea_SetTextUtf8(&d->disabled,
		"禁用状态：不响应鼠标/键盘，也不显示光标。");

	d->themeBtn = Ui_Id("ta.theme", 0, 0);
	Ui_SetFocusId(&app->ui, d->edit.id);
	set_status(d, "点击文本开始编辑；Tab 切换可聚焦字段。");
}

static void on_frame(UiApp *app, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiIcon sun = UiIcon_Glyph(t->fIcon, 0xE706u);
	UiRect card, row, editRc, roRc, disRc;
	UiRect out[2];
	UiStack col;
	UiFixedSize fx[2] = { { 0, 0 }, { 0, 0 } };
	UiMeasureItem pair[2] = {
		Ui_FixedItem(&fx[0], 1.0f), Ui_FixedItem(&fx[1], 1.0f) };
	int changed;

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_Text(g, t->fBig, "CUI TextArea",
		Ui_Rect(24, 14, app->w - 150, 32), t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "Soft Wrap · Scroll · Selection · IME · Readonly",
		Ui_Rect(26, 46, app->w - 150, 18), t->text3, GFX_TEXT_VCENTER);
	if (Ui_IconButton(ui, d->themeBtn, Ui_Rect(app->w - 66, 20, 36, 34),
		&sun, &plain, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(app);
	}

	card = Ui_Rect(22, 80, app->w - 44, app->h - 126);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 22, 18, 22, 18);

	Gfx_Text(g, t->fLabel, "多行文本（软换行 / 滚动 / 选择 / IME）",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	editRc = Ui_StackTake(&col, 250);
	changed = Ui_TextArea(&d->edit, ui, editRc, 1);

	Ui_StackSkip(&col, 14);
	Gfx_Text(g, t->fLabel, "只读 / 禁用",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	row = Ui_StackTake(&col, 92);
	if (!Ui_FlexLayout(row, UI_ROW, 16, pair, 2, out)) {
		out[0] = Ui_Rect(row.x, row.y, (row.w - 16.0f) * 0.5f, row.h);
		out[1] = Ui_Rect(out[0].x + out[0].w + 16.0f, row.y,
			out[0].w, row.h);
	}
	roRc = out[0];
	disRc = out[1];
	Ui_TextArea(&d->ro, ui, roRc, 1);
	Ui_TextArea(&d->disabled, ui, disRc, 0);

	Ui_StackSkip(&col, 12);
	if (changed) {
		wsprintfA(d->status, "已修改：%d 字符 / %d 行",
			d->edit.len, d->edit.lineCount);
	}
	Gfx_Text(g, t->fSmall, d->status,
		Ui_Rect(24, app->h - 34, app->w - 48, 18), t->text3,
		GFX_TEXT_VCENTER);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	UiContext *ui = &app->ui;
	if (UiTextArea_Key(&d->edit, ui, vk)) return 1;
	if (UiTextArea_Key(&d->ro, ui, vk)) return 1;
	if (vk == VK_TAB) {
		UiId next = ui->focusId == d->edit.id ? d->ro.id : d->edit.id;
		Ui_SetFocusId(ui, next);
		UiApp_Invalidate(app);
		return 1;
	}
	if (vk == VK_ESCAPE) {
		Ui_SetFocusId(ui, UI_ID_NONE);
		UiApp_Invalidate(app);
		return 1;
	}
	return 0;
}

static int on_chr(UiApp *app, wchar_t ch, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	return UiTextArea_Char(&d->edit, &app->ui, ch);
}

static void on_ime(UiApp *app, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	UiTextArea_OnIme(&d->edit, &app->ui);
}

static void on_timer(UiApp *app, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	UiTextArea_Blink(&d->edit, &app->ui);
	UiTextArea_Blink(&d->ro, &app->ui);
}

static void on_shutdown(UiApp *app, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	(void)app;
	UiTextArea_Shutdown(&d->edit);
	UiTextArea_Shutdown(&d->ro);
	UiTextArea_Shutdown(&d->disabled);
}

static int on_animating(UiApp *app, void *user)
{
	TextAreaDemo *d = (TextAreaDemo *)user;
	(void)app;
	return UiTextArea_Busy(&d->edit) || UiTextArea_Busy(&d->ro);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.animating = on_animating;
	cb.key = on_key;
	cb.chr = on_chr;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI TextArea", 820, 640, UI_THEME_LIGHT, &cb, &g_demo);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
