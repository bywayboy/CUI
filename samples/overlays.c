/*
 * overlays.c - Select / 下拉菜单 / 右键上下文菜单 样例
 *
 * 演示 UiPopupManager 的完整语义: 点击外部关闭、Esc 关闭、焦点回归、
 * 弹层打开时底层不参与命中; Select 的长列表滚动与禁用项。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_icon.h"
#include "ui/ui_dialog.h"
#include "ui/ui_controls.h"
#include "ui/ui_layout.h"
#include "ui/ui_menu.h"
#include "ui/ui_popup.h"
#include "ui/ui_select.h"
#include "ui/ui_textarea.h"
#include "ui/ui_widgets.h"

static const char *const kThemeItems[] = {
	"浅色", "深色"
};

static const char *const kCityItems[] = {
	"北京", "上海", "广州", "深圳", "杭州", "南京", "成都", "重庆",
	"武汉", "西安", "苏州", "天津", "长沙", "郑州", "青岛", "大连",
	"厦门", "福州", "济南", "合肥", "昆明", "宁波", "无锡", "佛山"
};

static const char *const kTagItems[] = {
	"UI", "Rendering", "Animation", "Input", "Layout",
	"ScrollView", "Theme", "Icons", "IME", "D3D11"
};

static const UiMenuItem kFileMenu[] = {
	{ "新建", "Ctrl+N", 0 },
	{ "打开...", "Ctrl+O", 0 },
	{ "保存", "Ctrl+S", 0 },
	{ "", "", UI_MENU_DIVIDER },
	{ "导出为 PNG", "", 0 },
	{ "导出为 SVG", "", UI_MENU_DISABLED },
	{ "", "", UI_MENU_DIVIDER },
	{ "删除项目", "Del", UI_MENU_DANGER }
};

static const UiMenuItem kContextMenu[] = {
	{ "复制", "Ctrl+C", 0 },
	{ "粘贴", "Ctrl+V", 0 },
	{ "全选", "Ctrl+A", 0 },
	{ "", "", UI_MENU_DIVIDER },
	{ "刷新", "F5", 0 },
	{ "属性", "", UI_MENU_DISABLED }
};

typedef struct OverlaysDemo {
	UiApp *app;
	UiPopupManager pm;
	UiSelect themeSel;
	UiSelect citySel;
	UiSelect tagSel;
	UiMenu fileMenu;
	UiMenu contextMenu;
	UiDialog dialog, confirm;
	UiSelect dialogSelect;
	UiId dialogBtnId, confirmBtnId;
	UiId dialogFocus[5];
	UiCheckbox notifications;
	UiTextArea notes;
	UiId menuBtnId;
	UiId themeBtnId;
	UiRect menuBtnRc;
	char status[192];
} OverlaysDemo;

static OverlaysDemo g_demo;

static void set_status(OverlaysDemo *d, const char *s)
{
	int i = 0;
	while (s && s[i] && i < (int)sizeof(d->status) - 1) {
		d->status[i] = s[i];
		i++;
	}
	d->status[i] = 0;
	UiApp_Invalidate(d->app);
}

static int selected_count(const UiSelect *s)
{
	int i, n = 0;
	for (i = 0; i < s->count && i < 32; i++)
		if (s->mask & (1u << i)) n++;
	return n;
}

static void consume_select(OverlaysDemo *d, UiSelect *s, const char *name)
{
	const char *value;
	if (!s->changed) return;
	if (s->multiple) {
		wsprintfA(d->status, "%s：已选 %d 项", name, selected_count(s));
	} else {
		value = (s->selected >= 0 && s->selected < s->count &&
			s->items[s->selected]) ? s->items[s->selected] : "(未选择)";
		wsprintfA(d->status, "%s：%s", name, value);
	}
	s->changed = 0;
	if (s == &d->themeSel) {
		UiTheme_SetTheme(&d->app->theme,
			s->selected == 1 ? UI_THEME_DARK : UI_THEME_LIGHT);
	}
	UiApp_Invalidate(d->app);
}

static UiDialog *active_dialog(OverlaysDemo *d)
{
	if (UiDialog_IsOpen(&d->confirm)) return &d->confirm;
	if (UiDialog_IsOpen(&d->dialog)) return &d->dialog;
	return NULL;
}

static void open_dialog(OverlaysDemo *d, int confirm)
{
	UiPopupManager_Close(&d->pm, &d->app->ui);
	UiDialog_Open(confirm ? &d->confirm : &d->dialog, &d->app->ui,
		confirm ? d->confirmBtnId : d->dialogBtnId);
}

static void consume_dialogs(OverlaysDemo *d)
{
	int result = UiDialog_TakeResult(&d->confirm);
	if (result) set_status(d, result == UI_DIALOG_ACCEPT
		? "已确认删除（仅演示，没有删除文件）。" : "已取消删除，焦点返回触发器。");
	result = UiDialog_TakeResult(&d->dialog);
	if (result) set_status(d, result == UI_DIALOG_ACCEPT
		? "Dialog 已完成，背景输入恢复。" : "Dialog 已取消，背景输入恢复。");
}

static void consume_menu(OverlaysDemo *d, UiMenu *m, const char *name)
{
	if (!m->changed) return;
	if (m->item >= 0 && m->item < m->count)
		wsprintfA(d->status, "%s：%s", name, m->items[m->item].label);
	m->changed = 0;
	UiApp_Invalidate(d->app);
}

static void on_init(UiApp *app, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	UiPopupManager_Init(&d->pm);

	UiSelect_Init(&d->themeSel, Ui_Id("ov.theme", 0, 0));
	UiSelect_SetItems(&d->themeSel, kThemeItems, 2);
	UiSelect_SetPlaceholder(&d->themeSel, "选择主题");

	UiSelect_Init(&d->citySel, Ui_Id("ov.city", 0, 0));
	UiSelect_SetItems(&d->citySel, kCityItems,
		(int)(sizeof(kCityItems) / sizeof(kCityItems[0])));
	UiSelect_SetDisabledMask(&d->citySel,
		(1u << 3) | (1u << 10) | (1u << 23));
	UiSelect_SetMultiple(&d->citySel, 1);
	UiSelect_SetMask(&d->citySel, (1u << 4) | (1u << 6));
	UiSelect_SetPlaceholder(&d->citySel, "选择城市（可多选）");

	UiSelect_Init(&d->tagSel, Ui_Id("ov.tag", 0, 0));
	UiSelect_SetItems(&d->tagSel, kTagItems,
		(int)(sizeof(kTagItems) / sizeof(kTagItems[0])));
	UiSelect_SetMultiple(&d->tagSel, 1);
	UiSelect_SetFilterable(&d->tagSel, 1);
	UiSelect_SetMask(&d->tagSel, (1u << 0) | (1u << 4));
	UiSelect_SetPlaceholder(&d->tagSel, "选择技术标签");
	UiSelect_SetSearchHint(&d->tagSel, L"搜索标签…");
	UiSelect_AttachIme(&d->tagSel, &app->ime);

	UiMenu_Init(&d->fileMenu, Ui_Id("ov.menu.file", 0, 0));
	UiMenu_SetItems(&d->fileMenu, kFileMenu,
		(int)(sizeof(kFileMenu) / sizeof(kFileMenu[0])));
	UiMenu_Init(&d->contextMenu, Ui_Id("ov.menu.ctx", 0, 0));
	UiMenu_SetItems(&d->contextMenu, kContextMenu,
		(int)(sizeof(kContextMenu) / sizeof(kContextMenu[0])));

	d->menuBtnId = Ui_Id("ov.btn.menu", 0, 0);
	d->themeBtnId = Ui_Id("ov.btn.theme", 0, 0);
	d->app = app;
	d->dialogBtnId = Ui_Id("ov.btn.dialog", 0, 0);
	d->confirmBtnId = Ui_Id("ov.btn.confirm", 0, 0);
	UiDialog_Init(&d->dialog, Ui_Id("ov.dialog", 0, 0));
	UiDialog_Init(&d->confirm, Ui_Id("ov.confirm", 0, 0));
	UiDialog_AttachIme(&d->dialog, &app->ime);
	UiDialog_AttachIme(&d->confirm, &app->ime);
	UiSelect_Init(&d->dialogSelect, Ui_Id("ov.dialog.select", 0, 0));
	UiSelect_SetItems(&d->dialogSelect, kTagItems, 10);
	UiSelect_SetSelected(&d->dialogSelect, 0);
	UiTextArea_Init(&d->notes, Ui_Id("ov.dialog.notes", 0, 0), 0);
	UiTextArea_AttachIme(&d->notes, &app->ime);
	UiTextArea_SetPlaceholder(&d->notes, L"备注（支持多行输入）");
	d->dialogFocus[0] = Ui_Id("ov.dialog.check", 0, 0);
	d->dialogFocus[1] = d->dialogSelect.id;
	d->dialogFocus[2] = d->notes.id;
	d->dialogFocus[3] = Ui_Id("ov.dialog.disabled", 0, 0);
	d->dialogFocus[4] = Ui_Id("ov.dialog.done", 0, 0);
	UiCheckbox_Init(&d->notifications, d->dialogFocus[0], 1);
	UiDialog_SetFocusOrder(&d->dialog, d->dialogFocus, 5);
	app->minW = 720.0f;
	app->minH = 560.0f;
	set_status(d, "点击 Select 打开下拉；空白处右键打开上下文菜单。");
}

static void on_frame(UiApp *app, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiButtonStyle primary = UiButtonStyle_Make(UI_BTYPE_PRIMARY,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiIcon sun = UiIcon_Glyph(t->fIcon, 0xE706u);
	UiRect card, row, r1, r2, info;
	UiStack col;
	UiFlexItem half[2] = { { 0, 1, 0, 0 }, { 0, 1, 0, 0 } };
	char line[220];

	UiPopupManager_BeginFrame(&d->pm, ui);

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_Text(g, t->fBig, "CUI Select & Menu",
		Ui_Rect(24, 14, app->w - 150, 32), t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "Select · Dropdown Menu · Context Menu",
		Ui_Rect(26, 46, app->w - 150, 18), t->text3, GFX_TEXT_VCENTER);
	if (Ui_IconButton(ui, d->themeBtnId, Ui_Rect(app->w - 66, 20, 36, 34),
		&sun, &plain, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiSelect_SetSelected(&d->themeSel,
			t->themeId == UI_THEME_DARK ? 1 : 0);
		UiApp_Invalidate(app);
	}

	card = Ui_Rect(22, 80, app->w - 44, app->h - 126);
	Ui_Panel(ui, card);
	Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 22, 18, 22, 18);

	Gfx_Text(g, t->fLabel, "Select（单选 / 多选 · 长列表 · 禁用项）",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	row = Ui_StackTake(&col, 38);
	{
		UiRect pair[2];
		if (!Ui_FlexRow(row, 16, half, 2, pair)) {
			pair[0] = Ui_Rect(row.x, row.y,
				(row.w - 16.0f) * 0.5f, row.h);
			pair[1] = Ui_Rect(pair[0].x + pair[0].w + 16.0f, row.y,
				pair[0].w, row.h);
		}
		r1 = pair[0];
		r2 = pair[1];
	}
	Ui_Select(&d->themeSel, &d->pm, ui, r1, NULL, 1);
	consume_select(d, &d->themeSel, "主题");
	Ui_Select(&d->citySel, &d->pm, ui, r2, NULL, 1);
	consume_select(d, &d->citySel, "城市");
	Ui_StackSkip(&col, 12);

	Gfx_Text(g, t->fLabel, "可搜索 / 多选（输入字母过滤，Enter 切换）",
		Ui_StackTake(&col, 18), t->text2, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	row = Ui_StackTake(&col, 38);
	Ui_Select(&d->tagSel, &d->pm, ui, row, NULL, 1);
	consume_select(d, &d->tagSel, "标签");
	Ui_StackSkip(&col, 18);

	Gfx_Text(g, t->fLabel, "菜单", Ui_StackTake(&col, 18), t->text2,
		GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 8);
	row = Ui_StackTake(&col, 34);
	d->menuBtnRc = Ui_Rect(row.x, row.y, 136.0f, row.h);
	if (Ui_ButtonEx(ui, d->menuBtnId, d->menuBtnRc, "打开菜单", &primary, 1)) {
		ui->navId = d->menuBtnId;
		UiMenu_OpenAt(&d->fileMenu, &d->pm, ui, d->menuBtnId,
			d->menuBtnRc, 0);
	}
	Gfx_Text(g, t->fSmall, "在卡片任意空白处按右键，打开上下文菜单。",
		Ui_Rect(row.x + 150.0f, row.y, row.w - 150.0f, row.h),
		t->text3, GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 20);

	Gfx_Text(g, t->fLabel, "当前状态", Ui_StackTake(&col, 18), t->text2,
		GFX_TEXT_VCENTER);
	Ui_StackSkip(&col, 6);
	info = Ui_StackTake(&col, 44);
	Gfx_FillRoundRect(g, info, 8.0f, t->panelBg);
	Gfx_StrokeRoundRect(g, info, 8.0f, t->panelBorder, 1.0f);
	wsprintfA(line, "主题：%s    城市：%d 项    标签：%d 项",
		d->themeSel.selected >= 0 ? kThemeItems[d->themeSel.selected] : "-",
		selected_count(&d->citySel), selected_count(&d->tagSel));
	Gfx_Text(g, t->fBody, line, Ui_RectInset(info, 12.0f), t->text,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	Ui_StackSkip(&col, 14);
	row = Ui_StackTake(&col, 34);
	if (Ui_Button(ui, d->dialogBtnId, Ui_Rect(row.x, row.y, 148, row.h),
		"打开 Dialog", UI_BTN_SECONDARY, 1)) open_dialog(d, 0);
	if (Ui_Button(ui, d->confirmBtnId, Ui_Rect(row.x + 164, row.y, 148, row.h),
		"删除确认…", UI_BTN_DANGER, 1)) open_dialog(d, 1);

	Gfx_Text(g, t->fSmall, d->status,
		Ui_Rect(24, app->h - 34, app->w - 48, 18), t->text3,
		GFX_TEXT_VCENTER);

	/* 帧末按 z 顺序绘制弹层 */
	UiSelect_DrawPopup(&d->themeSel, &d->pm, ui);
	consume_select(d, &d->themeSel, "主题");
	UiSelect_DrawPopup(&d->citySel, &d->pm, ui);
	consume_select(d, &d->citySel, "城市");
	UiSelect_DrawPopup(&d->tagSel, &d->pm, ui);
	consume_select(d, &d->tagSel, "标签");
	UiMenu_Draw(&d->fileMenu, &d->pm, ui);
	consume_menu(d, &d->fileMenu, "菜单");
	UiMenu_Draw(&d->contextMenu, &d->pm, ui);
	consume_menu(d, &d->contextMenu, "右键菜单");

	if (UiDialog_Begin(&d->dialog, ui, "通知设置", 480, 360, &row)) {
		Gfx_Text(g, t->fBody, "Tab 在对话框内循环，跳过禁用项；Esc 先关下拉，再关对话框。",
			Ui_Rect(row.x, row.y, row.w, 42), t->text2, GFX_TEXT_WRAP);
		Ui_Checkbox(&d->notifications, ui, Ui_Rect(row.x, row.y + 48, row.w, 28),
			"启用通知", 1);
		Ui_Select(&d->dialogSelect, &d->pm, ui,
			Ui_Rect(row.x, row.y + 90, row.w, 36), "通知类别", 1);
		Ui_TextArea(&d->notes, ui, Ui_Rect(row.x, row.y + 138, row.w, 72), 1);
		Ui_Button(ui, d->dialogFocus[3],
			Ui_Rect(row.x, row.y + row.h - 36, 120, 36), "禁用操作", UI_BTN_SECONDARY, 0);
		if (Ui_Button(ui, d->dialogFocus[4],
			Ui_Rect(row.x + row.w - 96, row.y + row.h - 36, 96, 36),
			"完成", UI_BTN_PRIMARY, 1)) UiDialog_Close(&d->dialog, ui, UI_DIALOG_ACCEPT);
		UiSelect_DrawPopup(&d->dialogSelect, &d->pm, ui);
		UiDialog_End(&d->dialog, ui);
	}
	Ui_ConfirmDialog(&d->confirm, ui, "删除这个项目？",
		"此操作将移除项目及其设置，无法撤销。请确认是否继续。这个样例仅演示交互，不会删除任何文件。",
		"确认删除", "取消", 1);
	consume_dialogs(d);
}

static int on_animating(UiApp *app, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	(void)app;
	return UiDialog_Busy(&d->dialog) || UiDialog_Busy(&d->confirm);
}

static void on_tick(UiApp *app, double dtMs, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	UiDialog_Tick(&d->dialog, &app->ui, dtMs);
	UiDialog_Tick(&d->confirm, &app->ui, dtMs);
	consume_dialogs(d);
}

static void on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	if (active_dialog(d) || msg != UI_APP_MOUSE_RDOWN) return;
	UiMenu_OpenAt(&d->contextMenu, &d->pm, &app->ui, UI_ID_NONE,
		Ui_Rect(x, y, 0, 0), 0);
	UiApp_Invalidate(app);
}

static int on_chr(UiApp *app, wchar_t ch, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	if (active_dialog(d)) {
		if (UiDialog_IsOpen(&d->dialog) && !d->dialog.closing && !app->ui.popupOpen)
			UiTextArea_Char(&d->notes, &app->ui, ch);
		return 1;
	}
	if (UiSelect_Char(&d->tagSel, &app->ui, ch)) return 1;
	return 0;
}

static void on_ime(UiApp *app, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	if (!active_dialog(d)) UiSelect_OnIme(&d->tagSel, &app->ui);
	else if (UiDialog_IsOpen(&d->dialog) && !d->dialog.closing && !app->ui.popupOpen)
		UiTextArea_OnIme(&d->notes, &app->ui);
}

static void on_timer(UiApp *app, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	UiSelect_Blink(&d->tagSel, &app->ui);
	if (UiDialog_IsOpen(&d->dialog)) UiTextArea_Blink(&d->notes, &app->ui);
}

static void on_shutdown(UiApp *app, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	(void)app;
	UiSelect_Shutdown(&d->themeSel);
	UiSelect_Shutdown(&d->citySel);
	UiSelect_Shutdown(&d->tagSel);
	UiSelect_Shutdown(&d->dialogSelect);
	UiTextArea_Shutdown(&d->notes);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	OverlaysDemo *d = (OverlaysDemo *)user;
	UiContext *ui = &app->ui;
	UiId navs[6];
	UiDialog *dialog = active_dialog(d);
	int i;

	if (dialog) {
		if (UiPopupManager_AnyOpen(&d->pm)) {
			if (UiPopupManager_Key(&d->pm, ui, vk) ||
				UiSelect_Key(&d->dialogSelect, &d->pm, ui, vk))
				UiApp_Invalidate(app);
			return 1;
		}
		if (UiDialog_Key(dialog, ui, vk)) return 1;
		if (dialog == &d->dialog) {
			if (UiTextArea_Key(&d->notes, ui, vk) ||
				UiCheckbox_Key(&d->notifications, ui, vk) ||
				UiSelect_Key(&d->dialogSelect, &d->pm, ui, vk)) {
				UiApp_Invalidate(app);
				return 1;
			}
			if (ui->navId == d->dialogFocus[4] &&
				(vk == VK_RETURN || vk == VK_SPACE))
				UiDialog_Close(dialog, ui, UI_DIALOG_ACCEPT);
		}
		return 1;
	}
	if (UiPopupManager_Key(&d->pm, ui, vk)) {
		set_status(d, "Esc 关闭弹层。");
		return 1;
	}
	if (UiMenu_Key(&d->fileMenu, &d->pm, ui, vk)) {
		consume_menu(d, &d->fileMenu, "菜单");
		return 1;
	}
	if (UiMenu_Key(&d->contextMenu, &d->pm, ui, vk)) {
		consume_menu(d, &d->contextMenu, "右键菜单");
		return 1;
	}
	if (UiSelect_Key(&d->themeSel, &d->pm, ui, vk)) {
		consume_select(d, &d->themeSel, "主题");
		return 1;
	}
	if (UiSelect_Key(&d->citySel, &d->pm, ui, vk)) {
		consume_select(d, &d->citySel, "城市");
		return 1;
	}
	if (UiSelect_Key(&d->tagSel, &d->pm, ui, vk)) {
		consume_select(d, &d->tagSel, "标签");
		return 1;
	}
	if (UiPopupManager_AnyOpen(&d->pm)) {
		if (vk == VK_TAB) {
			UiPopupManager_Close(&d->pm, ui);
			UiApp_Invalidate(app);
			return 1;
		}
		return 0;
	}
	navs[0] = d->themeSel.id;
	navs[1] = d->citySel.id;
	navs[2] = d->tagSel.id;
	navs[3] = d->menuBtnId;
	navs[4] = d->dialogBtnId;
	navs[5] = d->confirmBtnId;
	if (vk == VK_TAB) {
		int dir = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
		for (i = 0; i < 6; i++) if (ui->navId == navs[i]) break;
		if (i == 6) i = dir > 0 ? 5 : 0;
		Ui_SetNavId(ui, navs[(i + dir + 6) % 6]);
		UiApp_Invalidate(app);
		return 1;
	}
	if ((ui->navId == d->dialogBtnId || ui->navId == d->confirmBtnId) &&
		(vk == VK_RETURN || vk == VK_SPACE)) {
		open_dialog(d, ui->navId == d->confirmBtnId);
		return 1;
	}
	if (ui->navId == d->menuBtnId &&
		(vk == VK_RETURN || vk == VK_SPACE)) {
		UiMenu_OpenAt(&d->fileMenu, &d->pm, ui, d->menuBtnId,
			d->menuBtnRc, 1);
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
	cb.animating = on_animating;
	cb.tick = on_tick;
	cb.mouse = on_mouse;
	cb.key = on_key;
	cb.chr = on_chr;
	cb.ime = on_ime;
	cb.timer = on_timer;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI Select & Menu", 780, 620, UI_THEME_LIGHT, &cb, &g_demo);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
