/*
 * pagination.c - 行内分页器样例
 *
 * 演示: 数字页码条 (含 « ») / 紧凑退化 / 极简退化共享同一状态；
 * 每页条数下拉 (保持首条位置)；total+pageSize 派生页数；键盘翻页。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_icon.h"
#include "ui/ui_pagination.h"
#include "ui/ui_popup.h"
#include "ui/ui_select.h"
#include "ui/ui_widgets.h"

#define DEMO_TOTAL 1000

typedef struct PagDemo {
	UiApp *app;
	/* 三个宽度演示同一份数据: 每根条必须用不同 id (同帧内 id 唯一),
	 * 页码在各自 Draw 后手动同步. */
	UiPagination pag[3];
	UiPopupManager pm;
	UiSelect sizeSel;
	char status[192];
} PagDemo;

static PagDemo g_demo;

static const char *const kSizes[] = { "20 条/页", "50 条/页", "100 条/页" };
static const int kSizeVals[] = { 20, 50, 100 };

static void set_status(PagDemo *d, const char *text)
{
	int i = 0;
	while (text && text[i] && i < (int)sizeof(d->status) - 1) {
		d->status[i] = text[i];
		i++;
	}
	d->status[i] = 0;
}

static void sync_page(PagDemo *d, int page)
{
	int i;
	for (i = 0; i < 3; i++) {
		d->pag[i].page = page;
		d->pag[i].changed = 0;
	}
}

static void on_init(UiApp *app, void *user)
{
	PagDemo *d = (PagDemo *)user;
	d->app = app;
	app->minW = 900.0f;
	app->minH = 620.0f;
	UiPopupManager_Init(&d->pm);
	UiPagination_Init(&d->pag[0], Ui_Id("demo.pag.full", 0, 0),
		DEMO_TOTAL, 20);
	UiPagination_Init(&d->pag[1], Ui_Id("demo.pag.mid", 0, 0),
		DEMO_TOTAL, 20);
	UiPagination_Init(&d->pag[2], Ui_Id("demo.pag.min", 0, 0),
		DEMO_TOTAL, 20);
	UiSelect_Init(&d->sizeSel, Ui_Id("demo.pag.size", 0, 0));
	UiSelect_SetItems(&d->sizeSel, kSizes, 3);
	UiSelect_SetSelected(&d->sizeSel, 0);
	set_status(d, "点击页码/‹ ›翻页；点击后键盘 ← → Home End PgUp PgDn 可用。");
}

static void draw_rows(PagDemo *d, UiContext *ui, UiRect rc)
{
	UiTheme *t = &d->app->theme;
	Gfx *g = d->app->gfx;
	int off = UiPagination_Offset(&d->pag[0]);
	int rows = (int)(rc.h / 34.0f);
	int i, shown = 0;

	Ui_Panel(ui, rc);
	for (i = 0; i < rows && off + i < d->pag[0].total; i++) {
		char title[64], sub[48];
		UiRect row = Ui_Rect(rc.x + 10, rc.y + 8 + (float)i * 34.0f,
			rc.w - 20, 30);
		if (i & 1)
			Gfx_FillRoundRect(g, row, 6.0f, t->neutral.soft);
		wsprintfA(title, "数据条目 #%d", off + i + 1);
		wsprintfA(sub, "SC-%05d", off + i + 1);
		Gfx_Text(g, t->fBody, title,
			Ui_Rect(row.x + 12, row.y, row.w - 160, row.h), t->text,
			GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		Gfx_Text(g, t->fSmall, sub,
			Ui_Rect(row.x, row.y, row.w - 12, row.h), t->text3,
			GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
		shown++;
	}
	if (shown == 0) {
		Gfx_Text(g, t->fBody, "当前页没有数据", rc, t->text3,
			GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
	}
}

static void on_frame(UiApp *app, void *user)
{
	PagDemo *d = (PagDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiIcon sun = UiIcon_Glyph(t->fIcon, 0xE706u);
	UiPaginationStyle full, compact;
	UiRect listRc, barY, sizeRc, rangeRc;
	int end, n, i, changed = -1;
	char line[160];

	UiPopupManager_BeginFrame(&d->pm, ui);

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_Text(g, t->fBig, "CUI Pagination", Ui_Rect(24, 14, app->w - 160, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "total + pageSize · 自动紧凑退化 · 保持首条位置",
		Ui_Rect(26, 46, app->w - 160, 18), t->text3, GFX_TEXT_VCENTER);
	if (Ui_IconButton(ui, Ui_Id("pag.theme", 0, 0),
		Ui_Rect(app->w - 66, 20, 36, 34), &sun, &plain, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(app);
	}

	n = UiPagination_PageCount(&d->pag[0]);
	rangeRc = Ui_Rect(24, 78, app->w - 260, 24);
	wsprintfA(line, "第 %d–%d 条 · 共 %d 条 · 第 %d/%d 页",
		d->pag[0].total ? UiPagination_Offset(&d->pag[0]) + 1 : 0,
		UiPagination_Offset(&d->pag[0]) + d->pag[0].pageSize < d->pag[0].total
			? UiPagination_Offset(&d->pag[0]) + d->pag[0].pageSize
			: d->pag[0].total,
		d->pag[0].total, d->pag[0].page + 1, n);
	Gfx_Text(g, t->fBody, line, rangeRc, t->text2, GFX_TEXT_VCENTER);

	sizeRc = Ui_Rect(app->w - 220, 74, 140, 32);
	Gfx_Text(g, t->fSmall, "每页条数", Ui_Rect(sizeRc.x - 70, sizeRc.y,
		66, sizeRc.h), t->text3, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
	if (Ui_Select(&d->sizeSel, &d->pm, ui, sizeRc, NULL, 1)) {
		int idx = d->sizeSel.selected;
		if (idx >= 0 && idx < 3) {
			UiPagination_SetPageSize(&d->pag[0], ui, kSizeVals[idx]);
			UiPagination_SetPageSize(&d->pag[1], ui, kSizeVals[idx]);
			UiPagination_SetPageSize(&d->pag[2], ui, kSizeVals[idx]);
			sync_page(d, d->pag[0].page);
			wsprintfA(line, "每页 %d 条：原页首条仍在当前页（第 %d 页）",
				kSizeVals[idx], d->pag[0].page + 1);
			set_status(d, line);
			UiApp_Invalidate(app);
		}
	}

	listRc = Ui_Rect(24, 112, app->w - 48, 330);
	draw_rows(d, ui, listRc);

	ZeroMemory(&full, sizeof(full));
	full.firstLast = 1;
	full.maxSlots = 9;
	compact = full;
	compact.firstLast = 0;

	barY = Ui_Rect(24, 458, app->w - 48, 20);
	Gfx_Text(g, t->fSmall, "数字模式（« ‹ 页码 › »，maxSlots=9）",
		Ui_Rect(barY.x, barY.y, 460, 18), t->text3, GFX_TEXT_VCENTER);
	UiPagination_Draw(&d->pag[0], ui, Ui_Rect(24, 480, 560, 36), &full);

	Gfx_Text(g, t->fSmall, "紧凑：‹ 6 / 20 ›",
		Ui_Rect(608, 458, 150, 18), t->text3, GFX_TEXT_VCENTER);
	UiPagination_Draw(&d->pag[1], ui, Ui_Rect(608, 480, 150, 36), &compact);

	Gfx_Text(g, t->fSmall, "极简：‹ ›",
		Ui_Rect(776, 458, 76, 18), t->text3, GFX_TEXT_VCENTER);
	UiPagination_Draw(&d->pag[2], ui, Ui_Rect(776, 480, 76, 36), &compact);

	Gfx_Text(g, t->fBody,
		"状态由 total/pageSize 派生；宽度不足时自动退化为「‹ 6 / 20 ›」"
		"→「‹ ›」→ 不绘制，不会溢出容器。",
		Ui_Rect(24, 530, app->w - 48, 40), t->text3, GFX_TEXT_WRAP);

	Gfx_Text(g, t->fSmall, d->status,
		Ui_Rect(24, app->h - 34, app->w - 48, 18), t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	UiSelect_DrawPopup(&d->sizeSel, &d->pm, ui);

	for (i = 0; i < 3; i++) {
		if (UiPagination_TakeChanged(&d->pag[i])) changed = d->pag[i].page;
	}
	if (changed >= 0) {
		sync_page(d, changed);
		end = UiPagination_Offset(&d->pag[0]) + d->pag[0].pageSize;
		if (end > d->pag[0].total) end = d->pag[0].total;
		wsprintfA(line, "已翻到第 %d/%d 页，当前范围 %d–%d / %d",
			d->pag[0].page + 1, UiPagination_PageCount(&d->pag[0]),
			UiPagination_Offset(&d->pag[0]) + 1, end, d->pag[0].total);
		set_status(d, line);
		UiApp_Invalidate(app);
	}
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	PagDemo *d = (PagDemo *)user;
	char line[160];
	int end, i;

	if (UiPopupManager_Key(&d->pm, &app->ui, vk) ||
		UiSelect_Key(&d->sizeSel, &d->pm, &app->ui, vk)) {
		UiApp_Invalidate(app);
		return 1;
	}
	for (i = 0; i < 3; i++) {
		if (!UiPagination_Key(&d->pag[i], &app->ui, vk)) continue;
		sync_page(d, d->pag[i].page);
		end = UiPagination_Offset(&d->pag[0]) + d->pag[0].pageSize;
		if (end > d->pag[0].total) end = d->pag[0].total;
		wsprintfA(line, "键盘翻页: 第 %d/%d 页，范围 %d–%d / %d",
			d->pag[0].page + 1, UiPagination_PageCount(&d->pag[0]),
			UiPagination_Offset(&d->pag[0]) + 1, end, d->pag[0].total);
		set_status(d, line);
		UiApp_Invalidate(app);
		return 1;
	}
	return 0;
}

static void on_shutdown(UiApp *app, void *user)
{
	PagDemo *d = (PagDemo *)user;
	(void)app;
	UiSelect_Shutdown(&d->sizeSel);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	UiAppCallbacks cb;
	(void)inst; (void)prev; (void)cmd; (void)show;
	ZeroMemory(&cb, sizeof(cb));
	cb.init = on_init;
	cb.frame = on_frame;
	cb.key = on_key;
	cb.shutdown = on_shutdown;
	return UiApp_Run(L"CUI Pagination", 1080, 720, UI_THEME_LIGHT, &cb, &g_demo);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
