/*
 * mvvm.c - ButtonGroup + MVVM-lite 示例
 *
 * View 只做两件事: 读 ViewModel 画控件, 把控件点击写回 ViewModel。
 * 命令在 after_frame 执行, 不在绘制中改状态/关窗口。
 *
 * UiButtonGroup 自己管理 selected/mask/focus; 它是 ViewModel 的字段,
 * 不是全局注册表。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_button_group.h"
#include "ui/ui_layout.h"

typedef struct FormVM FormVM;
typedef void (*FormCommand)(FormVM *vm);

struct FormVM {
	UiApp *app;
	UiButtonGroup themeGroup;   /* SINGLE */
	UiButtonGroup flagGroup;    /* MULTI */
	UiButtonGroup actionGroup;  /* NORMAL */
	FormCommand submit;
	FormCommand reset;
	char status[192];
	int pendingTheme;
	int pendingFlags;
	int pendingAction;
};

static FormVM g_vm;

static const char *const kThemeLabels[2] = { "浅色", "深色" };
static const char *const kFlagLabels[3] = { "选项 A", "选项 B", "选项 C" };
static const char *const kActionLabels[3] = { "提交", "重置", "退出" };

static void vm_set_status(FormVM *vm)
{
	wsprintfA(vm->status, "主题=%s  flags=0x%X  nav=%s",
		vm->themeGroup.selected == 1 ? "深色" : "浅色",
		vm->flagGroup.mask,
		vm->app->ui.navId == vm->themeGroup.id ? "主题组" :
		(vm->app->ui.navId == vm->flagGroup.id ? "选项组" :
		 (vm->app->ui.navId == vm->actionGroup.id ? "操作组" : "无")));
}

static void vm_apply_theme(FormVM *vm)
{
	UiThemeId id = vm->themeGroup.selected == 1 ? UI_THEME_DARK : UI_THEME_LIGHT;
	UiTheme_SetTheme(&vm->app->theme, id);
	vm->pendingTheme = 0;
	vm_set_status(vm);
}

static void vm_do_submit(FormVM *vm)
{
	wsprintfA(vm->status, "已提交: flags=0x%X, 主题=%s",
		vm->flagGroup.mask, vm->themeGroup.selected == 1 ? "深色" : "浅色");
	vm->pendingAction = -1;
}

static void vm_do_reset(FormVM *vm)
{
	UiButtonGroup_SetSelected(&vm->themeGroup, 0);
	UiButtonGroup_SetMask(&vm->flagGroup, 0);
	vm_apply_theme(vm);
	vm->status[0] = 0;
	vm_set_status(vm);
	vm->pendingAction = -1;
}

static void vm_apply_action(FormVM *vm, int index)
{
	if (index == 0) vm->submit(vm);
	else if (index == 1) vm->reset(vm);
	else if (index == 2) UiApp_Close(vm->app);
	vm->pendingAction = -1;
}

static void on_init(UiApp *app, void *user)
{
	FormVM *vm = (FormVM *)user;
	vm->app = app;
	vm->submit = vm_do_submit;
	vm->reset = vm_do_reset;
	vm->pendingAction = -1;
	app->minW = 520.0f;
	app->minH = 590.0f;

	UiButtonGroup_Init(&vm->themeGroup, Ui_Id("vm.theme", 0, 0), UI_BG_SINGLE, 2);
	UiButtonGroup_SetSelected(&vm->themeGroup, app->theme.themeId == UI_THEME_DARK ? 1 : 0);
	UiButtonGroup_Init(&vm->flagGroup, Ui_Id("vm.flags", 0, 0), UI_BG_MULTI, 3);
	UiButtonGroup_Init(&vm->actionGroup, Ui_Id("vm.actions", 0, 0), UI_BG_NORMAL, 3);
	Ui_SetNavId(&app->ui, vm->themeGroup.id);
	vm_set_status(vm);
}

static void on_frame(UiApp *app, void *user)
{
	FormVM *vm = (FormVM *)user;
	UiTheme *t = &app->theme;
	UiContext *ui = &app->ui;
	Gfx *gfx = app->gfx;
	UiRect card;
	UiStack col;
	UiRect line1, labelTheme, themeRc, labelFlags, flagsRc, labelActions, actionsRc;
	UiRect labelStyles, stylesRc, statusRc;
	UiRect styleButtons[5] = { { 0, 0, 0, 0 } };

	Gfx_FillRoundRectGradV(gfx, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(gfx, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_FillRoundRectGradV(gfx, Ui_Rect(22, 22, 4, 26), 2.0f,
		t->blue2, t->blue);
	Gfx_Text(gfx, t->fBig, "CUI MVVM-lite", Ui_Rect(34, 18, app->w - 80.0f, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fSmall, "ButtonGroup = ViewModel 字段 + 显式命令",
		Ui_Rect(32, 48, app->w - 60.0f, 20), t->text3, GFX_TEXT_VCENTER);

	card = Ui_Rect(20, 82, app->w - 40.0f, app->h - 102.0f);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 22, 20, 22, 20);

	line1 = Ui_StackTake(&col, 20.0f);
	Ui_StackSkip(&col, 18.0f);
	labelTheme = Ui_StackTake(&col, 18.0f);
	Ui_StackSkip(&col, 6.0f);
	themeRc = Ui_StackTake(&col, 36.0f);
	Ui_StackSkip(&col, 20.0f);
	labelFlags = Ui_StackTake(&col, 18.0f);
	Ui_StackSkip(&col, 6.0f);
	flagsRc = Ui_StackTake(&col, 36.0f);
	Ui_StackSkip(&col, 20.0f);
	labelActions = Ui_StackTake(&col, 18.0f);
	Ui_StackSkip(&col, 6.0f);
	actionsRc = Ui_StackTake(&col, 38.0f);
	Ui_StackSkip(&col, 20.0f);
	labelStyles = Ui_StackTake(&col, 18.0f);
	Ui_StackSkip(&col, 6.0f);
	stylesRc = Ui_StackTake(&col, 34.0f);
	Ui_StackSkip(&col, 18.0f);
	statusRc = Ui_StackTakeFill(&col);

	Gfx_Text(gfx, t->fBody,
		"控件只返回本帧事件；selected/mask/focus 都在 FormVM 里。",
		line1, t->text2, GFX_TEXT_VCENTER);
	Gfx_Text(gfx, t->fLabel, "主题（单选 ButtonGroup）",
		labelTheme, t->text2, GFX_TEXT_VCENTER);
	UiButtonGroup_Draw(ui, &vm->themeGroup, themeRc, kThemeLabels, NULL, NULL,
		UI_BTN_SECONDARY);

	Gfx_Text(gfx, t->fLabel, "选项（多选 ButtonGroup）",
		labelFlags, t->text2, GFX_TEXT_VCENTER);
	UiButtonGroup_Draw(ui, &vm->flagGroup, flagsRc, kFlagLabels, NULL, NULL,
		UI_BTN_SECONDARY);

	Gfx_Text(gfx, t->fLabel, "操作（普通 ButtonGroup，视觉融合）",
		labelActions, t->text2, GFX_TEXT_VCENTER);
	UiButtonGroup_Draw(ui, &vm->actionGroup, actionsRc, kActionLabels, NULL, NULL,
		UI_BTN_SECONDARY);

	Gfx_Text(gfx, t->fLabel, "按钮样式（solid / plain / text / dashed / round）",
		labelStyles, t->text2, GFX_TEXT_VCENTER);
	{
		UiFlexItem items[5] = {
			{ 0, 1, 0, 0 }, { 0, 1, 0, 0 }, { 0, 1, 0, 0 },
			{ 0, 1, 0, 0 }, { 0, 1, 0, 0 }
		};
		UiButtonStyle s0 = UiButtonStyle_Make(UI_BTYPE_PRIMARY, UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
		UiButtonStyle s1 = UiButtonStyle_Make(UI_BTYPE_SUCCESS, UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
		UiButtonStyle s2 = UiButtonStyle_Make(UI_BTYPE_WARNING, UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
		UiButtonStyle s3 = UiButtonStyle_Make(UI_BTYPE_ERROR, UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, UI_BFLAG_DASHED);
		UiButtonStyle s4 = UiButtonStyle_Make(UI_BTYPE_DEFAULT, UI_BAPPEAR_SOLID, UI_BSHAPE_ROUND, 0);
		if (!Ui_FlexRow(stylesRc, 8.0f, items, 5, styleButtons)) {
			for (int i = 0; i < 5; i++) styleButtons[i] = Ui_Rect(0, 0, 0, 0);
		}
		Ui_ButtonEx(ui, Ui_Id("style.solid", 0, 0), styleButtons[0], "Solid", &s0, 1);
		Ui_ButtonEx(ui, Ui_Id("style.plain", 0, 0), styleButtons[1], "Plain", &s1, 1);
		Ui_ButtonEx(ui, Ui_Id("style.text", 0, 0), styleButtons[2], "Text", &s2, 1);
		Ui_ButtonEx(ui, Ui_Id("style.dash", 0, 0), styleButtons[3], "Dashed", &s3, 1);
		Ui_ButtonEx(ui, Ui_Id("style.round", 0, 0), styleButtons[4], "Round", &s4, 1);
	}

	Gfx_Text(gfx, t->fBody, vm->status, statusRc, t->text2, GFX_TEXT_VCENTER);
}

static void on_after_frame(UiApp *app, void *user)
{
	FormVM *vm = (FormVM *)user;
	(void)app;

	/* 鼠标事件: Draw 已把选中/点击写进 manager */
	if (vm->themeGroup.changed || vm->pendingTheme)
		vm_apply_theme(vm);
	if (vm->flagGroup.changed || vm->pendingFlags) {
		vm->pendingFlags = 0;
		vm_set_status(vm);
	}
	if (vm->actionGroup.clicked >= 0)
		vm_apply_action(vm, vm->actionGroup.clicked);
	if (vm->pendingAction >= 0)
		vm_apply_action(vm, vm->pendingAction);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	FormVM *vm = (FormVM *)user;
	UiId ids[3];
	int i;

	if (vk == VK_TAB) {
		ids[0] = vm->themeGroup.id;
		ids[1] = vm->flagGroup.id;
		ids[2] = vm->actionGroup.id;
		for (i = 0; i < 3; i++) {
			if (app->ui.navId == ids[i]) {
				Ui_SetNavId(&app->ui, ids[(i + 1) % 3]);
				break;
			}
		}
		if (i == 3) Ui_SetNavId(&app->ui, ids[0]);
		UiApp_Invalidate(app);
		return 1;
	}

	/* Key 会立即更新 manager; 命令在这里转成 pending, 下一帧再执行 */
	if (UiButtonGroup_Key(&app->ui, &vm->themeGroup, vk)) {
		if (vm->themeGroup.clicked >= 0) vm->pendingTheme = 1;
		UiApp_Invalidate(app);
		return 1;
	}
	if (UiButtonGroup_Key(&app->ui, &vm->flagGroup, vk)) {
		if (vm->flagGroup.clicked >= 0) vm->pendingFlags = 1;
		UiApp_Invalidate(app);
		return 1;
	}
	if (UiButtonGroup_Key(&app->ui, &vm->actionGroup, vk)) {
		if (vm->actionGroup.clicked >= 0)
			vm->pendingAction = vm->actionGroup.clicked;
		UiApp_Invalidate(app);
		return 1;
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.after_frame = on_after_frame;
	cb.key = on_key;
	return UiApp_Run(L"CUI MVVM-lite", 560, 620,
		UI_THEME_LIGHT, &cb, &g_vm);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
