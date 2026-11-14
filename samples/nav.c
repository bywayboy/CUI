/*
 * nav.c - 多级导航菜单样例
 *
 * 演示: 水平/垂直切换、水平尾部 >> 溢出、垂直图标 rail、多级子菜单、
 * 选中路径高亮与面包屑。弹层只使用一个 UiPopupManager 会话。
 */
#include <windows.h>
#include "ui/ui_app.h"
#include "ui/ui_icon.h"
#include "ui/ui_nav.h"
#include "ui/ui_popup.h"
#include "ui/ui_widgets.h"

enum {
	ID_HOME = 1,
	ID_PRODUCT = 10, ID_DESKTOP = 11, ID_STUDIO = 111, ID_RUNTIME = 112,
	ID_COMPONENTS = 113, ID_MOBILE = 12, ID_IOS = 121, ID_ANDROID = 122,
	ID_CLOUD = 13, ID_COMPUTE = 131, ID_STORAGE = 132,
	ID_SOLUTIONS = 20, ID_FINANCE = 21, ID_MANUFACTURE = 22, ID_EDU = 23,
	ID_RESOURCES = 30, ID_DOCS = 31, ID_EXAMPLES = 32, ID_CHANGELOG = 33,
	ID_CUSTOMERS = 40,
	ID_PRICING = 50,
	ID_DEVELOPERS = 60, ID_API = 61, ID_SDK = 62,
	ID_SUPPORT = 70, ID_FAQ = 71, ID_CONTACT = 72,
	ID_ABOUT = 80,
	ID_CAREERS = 90,
	ID_PARTNERS = 100,
	ID_SECURITY = 110,
	ID_STATUS = 120,
	ID_SALES = 130
};

typedef struct NavDemo {
	UiApp *app;
	UiNavBar nav;
	UiPopupManager pm;
	char status[192];
} NavDemo;

static NavDemo g_demo;

static UiNavItem g_desktop[3], g_mobile[2], g_cloud[2], g_product[3];
static UiNavItem g_developerKids[2], g_supportKids[2];
static UiNavItem g_roots[14];

static void set_item(UiNavItem *it, UiId id, const char *label,
	const char *shortcut, const UiNavItem *kids, int count, unsigned flags,
	GfxFont *font, uint32_t glyph)
{
	ZeroMemory(it, sizeof(*it));
	it->id = id;
	it->label = label;
	it->shortcut = shortcut;
	if (glyph) it->icon = UiIcon_Glyph(font, glyph);
	it->children = kids;
	it->childCount = count;
	it->flags = flags;
}

static void build_tree(GfxFont *font)
{
	set_item(&g_desktop[0], ID_STUDIO, "CUI Studio", "Ctrl+1", NULL, 0, 0, font, 0);
	set_item(&g_desktop[1], ID_RUNTIME, "CUI Runtime", "Ctrl+2", NULL, 0, 0, font, 0);
	set_item(&g_desktop[2], ID_COMPONENTS, "组件库", "Ctrl+3", NULL, 0, 0, font, 0);
	set_item(&g_mobile[0], ID_IOS, "iOS", NULL, NULL, 0, 0, font, 0);
	set_item(&g_mobile[1], ID_ANDROID, "Android", NULL, NULL, 0, 0, font, 0);
	set_item(&g_cloud[0], ID_COMPUTE, "云主机", NULL, NULL, 0, 0, font, 0);
	set_item(&g_cloud[1], ID_STORAGE, "对象存储", NULL, NULL, 0, 0, font, 0);

	set_item(&g_product[0], ID_DESKTOP, "桌面应用", NULL, g_desktop, 3, 0,
		font, 0xE7F4u);
	set_item(&g_product[1], ID_MOBILE, "移动端", NULL, g_mobile, 2, 0,
		font, 0xE8EAu);
	set_item(&g_product[2], ID_CLOUD, "云服务", NULL, g_cloud, 2, 0,
		font, 0xE753u);
	set_item(&g_developerKids[0], ID_API, "API 参考", NULL, NULL, 0, 0,
		font, 0);
	set_item(&g_developerKids[1], ID_SDK, "SDK 下载", NULL, NULL, 0, 0,
		font, 0);
	set_item(&g_supportKids[0], ID_FAQ, "常见问题", NULL, NULL, 0, 0,
		font, 0);
	set_item(&g_supportKids[1], ID_CONTACT, "联系我们", NULL, NULL, 0, 0,
		font, 0);

	set_item(&g_roots[0], ID_HOME, "首页", NULL, NULL, 0, 0, font, 0xE80Fu);
	set_item(&g_roots[1], ID_PRODUCT, "产品", NULL, g_product, 3, 0,
		font, 0xE71Du);
	set_item(&g_roots[2], ID_SOLUTIONS, "解决方案", NULL, NULL, 0, 0,
		font, 0xE8F1u);
	set_item(&g_roots[3], ID_RESOURCES, "资源", NULL, NULL, 0, 0,
		font, 0xE8A5u);
	set_item(&g_roots[4], ID_CUSTOMERS, "客户案例", NULL, NULL, 0, 0,
		font, 0xE716u);
	set_item(&g_roots[5], ID_PRICING, "定价", NULL, NULL, 0, 0,
		font, 0xE8ECu);
	set_item(&g_roots[6], ID_DEVELOPERS, "开发者", NULL, g_developerKids, 2, 0,
		font, 0xE943u);
	set_item(&g_roots[7], ID_SUPPORT, "支持", NULL, g_supportKids, 2, 0,
		font, 0xE897u);
	set_item(&g_roots[8], ID_ABOUT, "关于", NULL, NULL, 0, 0, font, 0xE946u);
	set_item(&g_roots[9], ID_CAREERS, "加入我们", NULL, NULL, 0, 0,
		font, 0xE77Bu);
	set_item(&g_roots[10], ID_PARTNERS, "合作伙伴", NULL, NULL, 0, 0,
		font, 0xE774u);
	set_item(&g_roots[11], ID_SECURITY, "安全与合规", NULL, NULL, 0, 0,
		font, 0xE72Eu);
	set_item(&g_roots[12], ID_STATUS, "服务状态", NULL, NULL, 0, 0,
		font, 0xE9D9u);
	set_item(&g_roots[13], ID_SALES, "联系销售", NULL, NULL, 0, 0,
		font, 0xE719u);
}

static void set_status(NavDemo *d, const char *text)
{
	int i = 0;
	while (text && text[i] && i < (int)sizeof(d->status) - 1) {
		d->status[i] = text[i];
		i++;
	}
	d->status[i] = 0;
}

static const UiNavItem *find_item(const UiNavItem *items, int count, UiId id)
{
	int i;
	for (i = 0; items && i < count; i++) {
		if (items[i].id == id) return &items[i];
	}
	return NULL;
}

static const UiNavItem *find_path_item(const UiId *path, int depth, int index,
	const UiNavItem **parentItems, int *parentCount)
{
	const UiNavItem *items = g_roots;
	int count = (int)(sizeof(g_roots) / sizeof(g_roots[0]));
	int i;
	const UiNavItem *it = NULL;
	for (i = 0; i <= index && i < depth; i++) {
		it = find_item(items, count, path[i]);
		if (!it) return NULL;
		if (i == index) {
			if (parentItems) *parentItems = items;
			if (parentCount) *parentCount = count;
			return it;
		}
		items = it->children;
		count = it->childCount;
	}
	return NULL;
}

static void build_crumb(const UiId *path, int depth, char *out, int cap)
{
	int i, n = 0;
	if (cap <= 0) return;
	out[0] = 0;
	for (i = 0; i < depth; i++) {
		const UiNavItem *it = find_path_item(path, depth, i, NULL, NULL);
		const char *s;
		if (!it) break;
		if (n && n < cap - 1) out[n++] = ' ';
		if (n && n < cap - 1) out[n++] = '/';
		if (n && n < cap - 1) out[n++] = ' ';
		s = it->label;
		while (s && *s && n < cap - 1) out[n++] = *s++;
	}
	out[n] = 0;
}

static void on_init(UiApp *app, void *user)
{
	NavDemo *d = (NavDemo *)user;
	UiId path[3] = { ID_PRODUCT, ID_DESKTOP, ID_COMPONENTS };
	d->app = app;
	app->minW = 860.0f;
	app->minH = 600.0f;
	build_tree(app->theme.fIcon);
	UiPopupManager_Init(&d->pm);
	UiNavBar_Init(&d->nav, Ui_Id("demo.nav", 0, 0));
	UiNavBar_SetItems(&d->nav, g_roots,
		(int)(sizeof(g_roots) / sizeof(g_roots[0])));
	UiNavBar_SetSelected(&d->nav, path, 3);
	d->nav.style.panelMinW = 210.0f;
	d->nav.style.panelMaxW = 340.0f;
	d->nav.style.panelMaxH = 400.0f;
	set_status(d, "点击父项展开子菜单；按 Esc 逐级关闭；水平溢出收进 >>。");
}

static void on_frame(UiApp *app, void *user)
{
	NavDemo *d = (NavDemo *)user;
	UiTheme *t = &app->theme;
	Gfx *g = app->gfx;
	UiContext *ui = &app->ui;
	UiButtonStyle plain = UiButtonStyle_Make(UI_BTYPE_DEFAULT,
		UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
	UiIcon sun = UiIcon_Glyph(t->fIcon, 0xE706u);
	UiId got[UI_NAV_MAX_DEPTH];
	char crumb[256];
	UiRect navRc, card, toolbar;
	int depth, vertical;

	UiPopupManager_BeginFrame(&d->pm, ui);

	Gfx_FillRoundRectGradV(g, Ui_Rect(0, 0, app->w, app->h), t->windowRadius,
		t->page, t->page2);
	Gfx_StrokeRoundRect(g, Ui_Rect(0.5f, 0.5f, app->w - 1.0f, app->h - 1.0f),
		t->windowRadius - 0.5f, t->frame, 1.0f);
	Gfx_Text(g, t->fBig, "CUI Navigation", Ui_Rect(24, 14, app->w - 160, 32),
		t->text, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fSmall, "水平 / 垂直 / 图标栏 · 多级菜单 · 路径高亮",
		Ui_Rect(26, 46, app->w - 160, 18), t->text3, GFX_TEXT_VCENTER);
	if (Ui_IconButton(ui, Ui_Id("nav.theme", 0, 0),
		Ui_Rect(app->w - 66, 20, 36, 34), &sun, &plain, 1)) {
		UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
			? UI_THEME_LIGHT : UI_THEME_DARK);
		UiApp_Invalidate(app);
	}

	vertical = d->nav.layout == UI_NAV_VERTICAL;
	toolbar = Ui_Rect(24, 82, app->w - 48, 34);
	if (Ui_ButtonEx(ui, Ui_Id("nav.layout", 0, 0),
		Ui_Rect(toolbar.x, toolbar.y, 96, toolbar.h),
		vertical ? "切换为水平" : "切换为垂直", &plain, 1)) {
		UiNavBar_SetLayout(&d->nav,
			vertical ? UI_NAV_HORIZONTAL : UI_NAV_VERTICAL);
		UiApp_Invalidate(app);
	}
	if (vertical) {
		if (Ui_ButtonEx(ui, Ui_Id("nav.rail", 0, 0),
			Ui_Rect(toolbar.x + 106, toolbar.y, 96, toolbar.h),
			d->nav.rail ? "展开图标栏" : "收起为图标栏", &plain, 1)) {
			UiNavBar_SetRail(&d->nav, !d->nav.rail);
			UiApp_Invalidate(app);
		}
	}
	if (Ui_ButtonEx(ui, Ui_Id("nav.reset", 0, 0),
		Ui_Rect(toolbar.x + 212, toolbar.y, 132, toolbar.h),
		"恢复默认选中", &plain, 1)) {
		UiId path[3] = { ID_PRODUCT, ID_DESKTOP, ID_COMPONENTS };
		UiNavBar_SetSelected(&d->nav, path, 3);
		UiApp_Invalidate(app);
	}

	if (vertical) {
		navRc = Ui_Rect(24, 126, d->nav.rail ? 66.0f : 236.0f,
			app->h - 218.0f);
		card = Ui_Rect(navRc.x + navRc.w + 16, navRc.y,
			app->w - navRc.x - navRc.w - 40, navRc.h);
	} else {
		navRc = Ui_Rect(24, 126, app->w - 48, 44);
		card = Ui_Rect(24, 186, app->w - 48, 148);
	}
	UiNavBar_Draw(&d->nav, &d->pm, ui, navRc, 1);

	depth = UiNavBar_GetSelected(&d->nav, got, UI_NAV_MAX_DEPTH);
	build_crumb(got, depth, crumb, (int)sizeof(crumb));

	Ui_Panel(ui, card);
	Gfx_Text(g, t->fLabel, "当前位置", Ui_Rect(card.x + 20, card.y + 16,
		card.w - 40, 20), t->text3, GFX_TEXT_VCENTER);
	Gfx_Text(g, t->fBig, crumb[0] ? crumb : "(无选中)",
		Ui_Rect(card.x + 20, card.y + 42, card.w - 40, 30), t->text,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(g, t->fBody,
		"选中路径会持续高亮祖先与叶子；父项默认只展开子菜单。",
		Ui_Rect(card.x + 20, card.y + 80, card.w - 40, 22), t->text2,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
	Gfx_Text(g, t->fBody,
		"水平模式尾部溢出后由 >> 收起；垂直模式可切换为纯图标栏，"
		"高度不足时底部同样提供 >> 入口。",
		Ui_Rect(card.x + 20, card.y + 104, card.w - 40, 40), t->text3,
		GFX_TEXT_WRAP);

	Gfx_Text(g, t->fSmall, d->status,
		Ui_Rect(24, app->h - 34, app->w - 48, 18), t->text3,
		GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);

	UiNavBar_DrawPopups(&d->nav, &d->pm, ui);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	NavDemo *d = (NavDemo *)user;
	UiId got[UI_NAV_MAX_DEPTH];
	char crumb[256];
	if (UiNavBar_Key(&d->nav, &d->pm, &app->ui, vk)) {
		int depth = UiNavBar_GetSelected(&d->nav, got, UI_NAV_MAX_DEPTH);
		build_crumb(got, depth, crumb, (int)sizeof(crumb));
		if (depth) {
			char line[320];
			wsprintfA(line, "已选择: %s", crumb);
			set_status(d, line);
		}
		UiApp_Invalidate(app);
		return 1;
	}
	if (UiPopupManager_Key(&d->pm, &app->ui, vk)) {
		UiApp_Invalidate(app);
		return 1;
	}
	return 0;
}

static void on_shutdown(UiApp *app, void *user)
{
	NavDemo *d = (NavDemo *)user;
	(void)app;
	UiNavBar_Shutdown(&d->nav);
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
	return UiApp_Run(L"CUI Navigation", 1080, 720, UI_THEME_LIGHT, &cb, &g_demo);
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#endif
