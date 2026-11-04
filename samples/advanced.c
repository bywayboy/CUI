/*
 * demo.c — CUI 独立演示程序
 *
 * 展示:
 *   - Gfx (Direct2D/DComp) 窗口渲染 + 脏区
 *   - UiContext 即时模式交互 (悬停/按下/动画)
 *   - Ui_DrawSurface 统一控件表面 (按钮/关闭键)
 *   - UiEdit 自绘编辑框 (普通 + 密码 + 中文输入法)
 *   - UiFrame 跟随 vsync 的动画节拍
 *
 * 0CRT 构建: 入口是 startup() -> WinMain (见 src/lib/crtstub.c 与 /ENTRY:startup)。
 */
#include <windows.h>
#include <imm.h>
#include <stdint.h>
#include <string.h>

#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_paint.h"
#include "ui/ui_frame.h"
#include "ui/ui_ime.h"
#include "ui/ui_widgets.h"
#include "ui/ui_toast.h"
#include "edit.h"
#include "log.h"

#define DEMO_W 720.0f
#define DEMO_H 560.0f

typedef struct Demo {
	HWND hwnd;
	Gfx *gfx;
	UiTheme theme;
	UiContext ui;
	UiFrameClock clock;
	float scale;
	float w, h;
	int minimized;
	int tracking;

	UiEdit edName;
	UiEdit edPass;
	UiIme ime;
	int focus;          /* 0 = 名称, 1 = 密码, -1 = 无 */
	int dragField;
	int blinkOn;

	int autoProgress;   /* 进度条自动播放 */
	float progress;     /* 0..1 逻辑进度 */
	float barP;         /* 回退路径的平滑显示值 */
	GfxOverlay *overlay;/* 合成器动画进度条; NULL = 回退主窗绘制 */
	UiRect barRc;       /* 进度条轨道 (DIP), 每帧重算 */
	UiRect overlayRc;
	int overlayPxW, overlayPxH;
	int clicks;
	wchar_t status[256];
	int statusKind;     /* 0=提示 1=成功 */
	unsigned statusSeq, statusAnimSeq;
	UiToast toast;

	UiRect fieldRc[2];
	UiRect closeRc;
	int pending;        /* 1=提交 2=重置 3=关闭 4=动画 5=主题 (帧后处理) */
} Demo;

static Demo g;

/* ── 脏区 ───────────────────────────────────────────────────────────
 * 即时模式不会自动知道“哪里变了”。连续动画走 Ui_AnimTakeDirty (通道矩形),
 * 离散/应用驱动变化 (输入、焦点、光标、IME、按钮动作) 在这里显式失效。
 * 所有输入坐标是 DIP, 转成物理像素交给 InvalidateRect。 */
static void invalidate_dip(UiRect r, float pad)
{
	RECT rc;
	float s = g.scale > 0.0f ? g.scale : 1.0f;
	if (!g.hwnd || r.w <= 0.0f || r.h <= 0.0f) return;
	rc.left = (LONG)((r.x - pad) * s);
	rc.top = (LONG)((r.y - pad) * s);
	rc.right = (LONG)((r.x + r.w + pad) * s + 1.0f);
	rc.bottom = (LONG)((r.y + r.h + pad) * s + 1.0f);
	InvalidateRect(g.hwnd, &rc, FALSE);
}

static void invalidate_field(int f)
{
	if (f >= 0 && f < 2) invalidate_dip(g.fieldRc[f], 12.0f);
}

/* 取走本帧动画改动的并集: 只有进度条在动时, 就只重绘进度条。
 * Ui_AnimTakeDirty 已经包含 10 DIP 扩边 (光晕/描边/位移)。 */
static void invalidate_anim_dirty(void)
{
	UiRect d;
	int kind;
	if (!g.hwnd) return;
	kind = Ui_AnimTakeDirty(&g.ui, &d);
	if (kind == 2) InvalidateRect(g.hwnd, NULL, FALSE);
	else if (kind == 1) invalidate_dip(d, 0.0f);
}

/* ── 合成器动画进度条 ───────────────────────────────────────────────
 * 静态轨道画在主窗口; 填充画在独立的小 swapchain 上, 由 DComp 子 visual
 * 的矩形 clip 显示。只有创建/换尺寸/暂停/每 6 秒回卷时 Commit, 平时由
 * DWM 按 6 秒线性补间, 应用不需要每帧 Present 主窗口。 */
static void bar_overlay_apply(int animate)
{
	if (!g.overlay) return;
	if (animate && g.autoProgress) {
		if (!Gfx_OverlaySetReveal(g.overlay, g.progress, 0) ||
			!Gfx_OverlaySetReveal(g.overlay, 1.0f,
				(int)((1.0f - g.progress) * 6000.0f))) {
			Gfx_OverlayDestroy(g.overlay);
			g.overlay = NULL;
		}
	} else {
		if (!Gfx_OverlaySetReveal(g.overlay, g.progress, 0)) {
			Gfx_OverlayDestroy(g.overlay);
			g.overlay = NULL;
		}
	}
}

static void bar_overlay_sync(void)
{
	int pw, ph;
	if (!g.gfx || g.barRc.w <= 0.0f) return;
	/* DComp visual 永远在主窗之上; Toast 的遮罩在主窗里, 必须让 overlay 让位。
	 * 只把透明度压到 0, 合成器动画继续跑; Toast 结束后恢复即可, 不会跳变。 */
	if (UiToast_IsVisible(&g.toast)) {
		if (g.overlay && !Gfx_OverlayAlive(g.overlay)) {
			Gfx_OverlayDestroy(g.overlay);
			g.overlay = NULL;
		}
		if (g.overlay && !Gfx_OverlaySetOpacity(g.overlay, 0.0f)) {
			Gfx_OverlayDestroy(g.overlay);
			g.overlay = NULL;
		}
		return;
	}
	if (g.overlay && Gfx_OverlayAlive(g.overlay))
		Gfx_OverlaySetOpacity(g.overlay, 1.0f);
	if (g.overlay && !Gfx_OverlayAlive(g.overlay)) {
		Gfx_OverlayDestroy(g.overlay);
		g.overlay = NULL;
		g.overlayPxW = g.overlayPxH = 0;
		return;
	}
	pw = (int)(g.barRc.w * g.scale + 0.5f);
	ph = (int)(g.barRc.h * g.scale + 0.5f);
	if (!g.overlay) {
		/* 等主窗进场淡入结束再创建 overlay, 避免进度条比整窗先亮起来 */
		float introA = Ui_AnimGet(&g.ui, Ui_Id("demo.intro", 0, 0), UI_AN_ALPHA, 1.0f);
		if (introA < 0.999f) return;
		g.overlay = Gfx_CreateOverlay(g.gfx, g.barRc,
			g.theme.progressRadius, g.theme.progressFill, g.theme.progressFill2);
		if (!g.overlay) return;
		g.overlayRc = g.barRc;
		g.overlayPxW = pw;
		g.overlayPxH = ph;
		bar_overlay_apply(1);
		return;
	}
	if (pw == g.overlayPxW && ph == g.overlayPxH &&
		g.barRc.x == g.overlayRc.x && g.barRc.y == g.overlayRc.y &&
		g.barRc.w == g.overlayRc.w && g.barRc.h == g.overlayRc.h)
		return;
	/* 先把当前比例同步给旧 overlay, 重建后 clip 不会先闪一下满条 */
	Gfx_OverlaySetReveal(g.overlay, g.progress, 0);
	if (Gfx_OverlaySetRect(g.overlay, g.barRc) == 0) {
		Gfx_OverlayDestroy(g.overlay);
		g.overlay = NULL;
		g.overlayPxW = g.overlayPxH = 0;
		return;
	}
	g.overlayRc = g.barRc;
	g.overlayPxW = pw;
	g.overlayPxH = ph;
	bar_overlay_apply(1);
}

static void bar_overlay_shutdown(void)
{
	if (g.overlay) {
		Gfx_OverlayDestroy(g.overlay);
		g.overlay = NULL;
	}
}

/* ── 小工具 ───────────────────────────────────────────────────────── */

static void draw_wtext(Gfx *gfx, GfxFont *f, const wchar_t *s, UiRect rc, UiColor c, uint32_t flags)
{
	char buf[1024];
	int n;
	if (!s || !*s) return;
	if (!Gfx_IsVisible(gfx, rc)) return;   /* 脏区外不转换/不排版 */
	n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, (int)sizeof(buf) - 1, NULL, NULL);
	if (n <= 0) return;
	buf[n] = 0;
	Gfx_Text(gfx, f, buf, rc, c, flags);
}

/* 标题栏关闭键: 圆形表面 + 两条对角线 */
static void draw_close(UiRect rc)
{
	UiSurfaceStyle ss;
	UiWidgetState st;
	UiColor c;

	ZeroMemory(&ss, sizeof(ss));
	ss.bg = g.theme.card;
	ss.hoverBg = g.theme.btnHoverBg;
	ss.pressBg = g.theme.btnPressBg;
	ss.border = 0x00000000;
	ss.hoverBorder = 0;
	ss.focusBorder = 0;
	ss.radius = rc.w * 0.5f;
	ss.borderW = 0.0f;
	ss.glowW = 0.0f;
	ss.glowMask = 0;
	ss.glow = 0;
	st = Ui_DrawSurface(&g.ui, Ui_Id("demo.close", 0, 0), rc, &ss, 1);
	c = st.pressed ? g.theme.blue2 : g.theme.text2;
	{
		float cx = rc.x + rc.w * 0.5f, cy = rc.y + rc.h * 0.5f, d = 5.0f;
		Gfx_Line(g.gfx, cx - d, cy - d, cx + d, cy + d, c, 1.6f);
		Gfx_Line(g.gfx, cx - d, cy + d, cx + d, cy - d, c, 1.6f);
	}
}

/* ── 生命周期 ─────────────────────────────────────────────────────── */

static void demo_init(HWND hwnd)
{
	ZeroMemory(&g, sizeof(g));
	g.hwnd = hwnd;
	g.focus = -1;
	g.dragField = -1;
	g.blinkOn = 1;
	g.autoProgress = 1;
	g.scale = 1.0f;

	if (!Gfx_Init(&g.gfx, hwnd) || !g.gfx) {
		MessageBoxW(hwnd, L"初始化 Direct2D 失败", L"CUI Demo", MB_ICONERROR);
		return;
	}
	UiTheme_Init(&g.theme, g.gfx);
	Ui_ContextInit(&g.ui, hwnd, g.gfx, &g.theme);
	UiIme_Init(&g.ime, hwnd);
	UiToast_Init(&g.toast);

	/* 进场淡入: 首帧前置 0, 保证 overlay 等主窗淡入完成后再创建 */
	Ui_AnimSet(&g.ui, Ui_Id("demo.intro", 0, 0), UI_AN_ALPHA, 0.0f);
	Ui_AnimTo(&g.ui, Ui_Id("demo.intro", 0, 0), UI_AN_ALPHA, 1.0f, 220, UI_AN_EASE_OUT);

	Edit_Init(&g.edName, 63, 0, NULL);
	Edit_Init(&g.edPass, 63, 1, NULL);          /* 密码: 掩码显示 */
	Edit_SetImeEnabled(&g.edPass, 0);            /* 密码框禁用输入法 */
	Edit_SetTextUtf8(&g.edName, "CUI Demo");

	lstrcpynW(g.status, L"就绪: 点输入框, 用中文输入法试试", 256);
	g.statusSeq = 1;
}

static void demo_shutdown(void)
{
	bar_overlay_shutdown();
	if (g.gfx) {
		UiTheme_Shutdown(&g.theme, g.gfx);
		Gfx_Shutdown(g.gfx);
		g.gfx = NULL;
	}
}

void demo_set_scale(float scale)
{
	g.scale = scale > 0.0f ? scale : 1.0f;
}

static int demo_animating(void)
{
	if (g.minimized) return 0;
	if (g.autoProgress) return 1;
	if (UiToast_IsVisible(&g.toast)) return 1;
	if (Ui_AnimBusy(&g.ui)) return 1;
	return 0;
}

static void demo_advance(double dtMs)
{
	Ui_AnimAdvance(&g.ui, (float)dtMs);
	if (g.autoProgress) {
		g.progress += (float)(dtMs / 6000.0);      /* 6 秒一圈 */
		if (g.progress > 1.0f) {
			g.progress -= 1.0f;
			/* 新一圈: 先归零再交给合成器补间 */
			if (g.overlay) {
				if (!Gfx_OverlaySetReveal(g.overlay, 0.0f, 0) ||
					!Gfx_OverlaySetReveal(g.overlay, 1.0f, 6000)) {
					Gfx_OverlayDestroy(g.overlay);
					g.overlay = NULL;
				}
			}
		}
	}
	{
		/* 回退路径的平滑值 (overlay 存在时不用于显示) */
		float a = (float)(dtMs / (140.0 + dtMs));
		g.barP += (g.progress - g.barP) * a;
		if (g.barP < 0.0f) g.barP = 0.0f;
		if (g.barP > 1.0f) g.barP = 1.0f;
	}
	if (g.statusSeq != g.statusAnimSeq) {
		g.statusAnimSeq = g.statusSeq;
		Ui_AnimSet(&g.ui, Ui_Id("demo.status", 0, 0), UI_AN_ALPHA, 0.0f);
		Ui_AnimSet(&g.ui, Ui_Id("demo.status", 0, 0), UI_AN_OFFSET, 6.0f);
		Ui_AnimTo(&g.ui, Ui_Id("demo.status", 0, 0), UI_AN_ALPHA, 1.0f, 180, UI_AN_EASE_OUT);
		Ui_AnimTo(&g.ui, Ui_Id("demo.status", 0, 0), UI_AN_OFFSET, 0.0f, 180, UI_AN_EASE_OUT);
	}
}

/* ── 绘制 ─────────────────────────────────────────────────────────── */

static void demo_render(HWND hwnd, const RECT *dirtyPx)
{
	RECT rc;
	UiRect card, r;
	float x, y, w;
	int bSubmit, bReset, bToggle, bTheme, bQuit;
	float intro;

	if (!g.gfx) return;
	GetClientRect(hwnd, &rc);
	Gfx_Resize(g.gfx, (int)rc.right, (int)rc.bottom, g.scale);
	g.w = (float)rc.right / g.scale;
	g.h = (float)rc.bottom / g.scale;
	Ui_SetViewport(&g.ui, g.w, g.h, g.scale);
	Ui_SetFocusId(&g.ui, g.focus == 0 ? Ui_Id("demo.edit", 0, 0)
		: g.focus == 1 ? Ui_Id("demo.edit", 1, 0) : UI_ID_NONE);

	/* Toast 已结束时, 在画主窗之前恢复 overlay, 避免空一帧 */
	if (!UiToast_IsVisible(&g.toast) && g.overlay && Gfx_OverlayAlive(g.overlay))
		Gfx_OverlaySetOpacity(g.overlay, 1.0f);

	/* 进场淡入: 首次渲染前 0.999 (DComp 预热帧), 之后走动画通道 */
	intro = Ui_AnimGet(&g.ui, Ui_Id("demo.intro", 0, 0), UI_AN_ALPHA, 1.0f);

	/* Toast 遮罩是整窗的; 任何局部重绘都会把它擦掉, 可见时强制整窗 */
	Gfx_SetDirtyRect(g.gfx, UiToast_IsVisible(&g.toast) ? NULL : dirtyPx);
	Gfx_Begin(g.gfx);
	int alphaPushed = Gfx_PushAlpha(g.gfx, intro);
	Gfx_Clear(g.gfx, 0x00000000);

	/* 窗口底板 */
	Gfx_FillRoundRectGradV(g.gfx, Ui_Rect(0, 0, g.w, g.h), 14.0f,
		g.theme.page, g.theme.page2);
	Gfx_StrokeRoundRect(g.gfx, Ui_Rect(0.5f, 0.5f, g.w - 1.0f, g.h - 1.0f), 13.5f,
		g.theme.frame, 1.0f);
	Gfx_StrokeRoundRect(g.gfx, Ui_Rect(1.5f, 1.5f, g.w - 3.0f, g.h - 3.0f), 12.5f,
		g.theme.frameHi, 1.0f);

	/* 标题 */
	Gfx_FillRoundRectGradV(g.gfx, Ui_Rect(24, 26, 4, 26), 2.0f,
		g.theme.blue2, g.theme.blue);
	draw_wtext(g.gfx, g.theme.fBig, L"CUI Demo", Ui_Rect(36, 22, g.w - 140, 32),
		g.theme.text, GFX_TEXT_VCENTER);
	draw_wtext(g.gfx, g.theme.fSmall,
		L"即时模式 UI · Direct2D/DComp · 自绘编辑框与输入法 · 0CRT",
		Ui_Rect(34, 56, g.w - 120, 20), g.theme.text3, GFX_TEXT_VCENTER);

	Ui_BeginFrame(&g.ui);
	g.closeRc = Ui_Rect(g.w - 46, 20, 28, 28);
	draw_close(g.closeRc);

	/* 卡片 */
	card = Ui_Rect(24, 96, g.w - 48, g.h - 132);
	Ui_Panel(&g.ui, card);

	x = card.x + 28;
	y = card.y + 26;
	w = card.w - 56;

	draw_wtext(g.gfx, g.theme.fLabel, L"名称", Ui_Rect(x, y, 120, 18),
		g.theme.text2, GFX_TEXT_VCENTER);
	draw_wtext(g.gfx, g.theme.fSmall, L"支持中文输入法 (组合串就地绘制)",
		Ui_Rect(x + 70, y, 380, 18), g.theme.hint, GFX_TEXT_VCENTER);
	g.fieldRc[0] = Ui_Rect(x, y + 24, w, 40);
	g.edName.focused = (g.focus == 0);
	g.edName.caretOn = g.blinkOn;
	Edit_Draw(&g.ui, Ui_Id("demo.edit", 0, 0), g.fieldRc[0], &g.edName, L"输入名称");

	y += 84;
	draw_wtext(g.gfx, g.theme.fLabel, L"密码", Ui_Rect(x, y, 120, 18),
		g.theme.text2, GFX_TEXT_VCENTER);
	draw_wtext(g.gfx, g.theme.fSmall, L"已禁用输入法 (ImmAssociateContext(NULL))",
		Ui_Rect(x + 70, y, 420, 18), g.theme.hint, GFX_TEXT_VCENTER);
	g.fieldRc[1] = Ui_Rect(x, y + 24, w, 40);
	g.edPass.focused = (g.focus == 1);
	g.edPass.caretOn = g.blinkOn;
	Edit_Draw(&g.ui, Ui_Id("demo.edit", 1, 0), g.fieldRc[1], &g.edPass, L"输入密码");

	/* 按钮行 */
	y += 92;
	bSubmit = Ui_Button(&g.ui, Ui_Id("demo.btn.submit", 0, 0),
		Ui_Rect(x, y, 110, 38), "提交", UI_BTN_PRIMARY, 1);
	bReset = Ui_Button(&g.ui, Ui_Id("demo.btn.reset", 0, 0),
		Ui_Rect(x + 122, y, 90, 38), "重置", UI_BTN_SECONDARY, 1);
	bToggle = Ui_Button(&g.ui, Ui_Id("demo.btn.anim", 0, 0),
		Ui_Rect(x + 224, y, 110, 38),
		g.autoProgress ? "暂停动画" : "播放动画", UI_BTN_SECONDARY, 1);
	bTheme = Ui_Button(&g.ui, Ui_Id("demo.btn.theme", 0, 0),
		Ui_Rect(x + 346, y, 90, 38),
		g.theme.themeId == UI_THEME_LIGHT ? "深色" : "浅色", UI_BTN_SECONDARY, 1);
	bQuit = Ui_Button(&g.ui, Ui_Id("demo.btn.quit", 0, 0),
		Ui_Rect(x + 448, y, 90, 38), "退出", UI_BTN_DANGER, 1);
	(void)bSubmit; (void)bReset; (void)bToggle; (void)bTheme; (void)bQuit;

	/* 进度条: 平滑值 + 轨道 */
	y += 66;
	draw_wtext(g.gfx, g.theme.fLabel, L"进度 (动画节拍)", Ui_Rect(x, y, 200, 18),
		g.theme.text2, GFX_TEXT_VCENTER);
	{
		r = Ui_Rect(x, y + 24, w, 10);
		g.barRc = r;
		/* overlay 存在时填充由合成器画; 否则由组件在主窗画 */
		Ui_ProgressBar(&g.ui, r, g.barP,
			!g.overlay || !Gfx_OverlayAlive(g.overlay));
	}

	/* 状态行 (提交/重置时淡入) */
	y += 58;
	{
		float sa = Ui_AnimGet(&g.ui, Ui_Id("demo.status", 0, 0), UI_AN_ALPHA, 1.0f);
		float sy = Ui_AnimGet(&g.ui, Ui_Id("demo.status", 0, 0), UI_AN_OFFSET, 0.0f);
		UiColor c = g.statusKind == 1 ? g.theme.green : g.theme.text2;
		UiRect sr = Ui_Rect(x, y + sy, w, 22);
		int baseA = (int)((c >> 24) & 0xFF);
		Ui_AnimSetRect(&g.ui, Ui_Id("demo.status", 0, 0), Ui_Rect(x, y, w, 22));
		draw_wtext(g.gfx, g.theme.fBody, g.status, sr,
			Ui_ColorAlpha(c, (int)(baseA * sa)), GFX_TEXT_VCENTER);
	}

	/* 点击计数 (纯 ASCII, 直接给 Gfx_Text) */
	{
		UiRect cntRc = Ui_Rect(x, card.y + card.h - 30, 260, 18);
		if (Gfx_IsVisible(g.gfx, cntRc)) {
			char cnt[64];
			wsprintfA(cnt, "button clicks: %d", g.clicks);
			Gfx_Text(g.gfx, g.theme.fSmall, cnt, cntRc,
				g.theme.hint, GFX_TEXT_VCENTER);
		}
		draw_wtext(g.gfx, g.theme.fSmall, L"Tab 切换字段 · 回车提交 · Esc 取消焦点",
			Ui_Rect(x + 260, card.y + card.h - 30, w - 260, 18),
			g.theme.hint, GFX_TEXT_RIGHT | GFX_TEXT_VCENTER);
	}

	/* Toast 最后画, 内部用 z-index 把遮罩/卡片放到基础控件之上 */
	UiToast_Draw(&g.ui, &g.toast, UiFrame_Now(), g.w, g.h);

	/* 单帧事件必须在 EndFrame 之前快照: Ui_EndFrame 会清空 clicked/down/released,
	 * 之后再读永远是 0 (关闭键/按钮因此全部无响应)。动作仍推迟到 Gfx_End 之后执行,
	 * 避免绘制中途弹模态/重绘导致渲染重入 (安装器的 SHBrowseForFolder 踩过)。 */
	UiId clicked = g.ui.clicked;
	Ui_EndFrame(&g.ui);

	if (clicked == Ui_Id("demo.btn.submit", 0, 0)) g.pending = 1;
	if (clicked == Ui_Id("demo.btn.reset", 0, 0)) g.pending = 2;
	if (clicked == Ui_Id("demo.btn.anim", 0, 0)) g.pending = 4;
	if (clicked == Ui_Id("demo.btn.theme", 0, 0)) g.pending = 5;
	if (clicked == Ui_Id("demo.btn.quit", 0, 0)) g.pending = 3;
	if (clicked == Ui_Id("demo.close", 0, 0)) g.pending = 3;

	if (UiToast_IsVisible(&g.toast) &&
		(clicked == UiToast_BackdropId() || clicked == UiToast_CardId())) {
		UiToast_Hide(&g.toast, UiFrame_Now());
		InvalidateRect(g.hwnd, NULL, FALSE);
	}

	if (alphaPushed) Gfx_PopAlpha(g.gfx);
	Gfx_End(g.gfx);
	bar_overlay_sync();          /* DComp 已初始化; 创建/更新进度条 overlay */

	if (g.pending) {
		int act = g.pending;
		g.pending = 0;
		if (act == 1) {
			char nm[200];
			char msg[320];
			g.clicks++;
			lstrcpynW(g.status, L"已提交: ", 256);
			lstrcatW(g.status, g.edName.text[0] ? g.edName.text : L"(空)");
			lstrcatW(g.status, L" (密码不显示)");
			g.statusKind = 1;
			g.statusSeq++;
			Edit_TextUtf8(&g.edName, nm, (int)sizeof(nm));
			wsprintfA(msg, "已提交: %s", nm[0] ? nm : "(空)");
			if (g.overlay && !Gfx_OverlaySetOpacity(g.overlay, 0.0f)) {
				Gfx_OverlayDestroy(g.overlay);
				g.overlay = NULL;
			}
			UiToast_Show(&g.toast, msg, UiFrame_Now(), 2600, UI_TOAST_SUCCESS);
		} else if (act == 2) {
			Edit_SetText(&g.edName, L"");
			Edit_SetText(&g.edPass, L"");
			lstrcpynW(g.status, L"已重置", 256);
			g.statusKind = 0;
			g.statusSeq++;
			g.blinkOn = 1;
			if (g.overlay && !Gfx_OverlaySetOpacity(g.overlay, 0.0f)) {
				Gfx_OverlayDestroy(g.overlay);
				g.overlay = NULL;
			}
			UiToast_Show(&g.toast, "已重置", UiFrame_Now(), 2200, UI_TOAST_INFO);
		} else if (act == 4) {
			g.autoProgress = !g.autoProgress;
			bar_overlay_apply(g.autoProgress);
		} else if (act == 5) {
			UiThemeId next = g.theme.themeId == UI_THEME_LIGHT
				? UI_THEME_DARK : UI_THEME_LIGHT;
			UiTheme_SetTheme(&g.theme, next);
			bar_overlay_shutdown();
			UiToast_Show(&g.toast,
				next == UI_THEME_DARK ? "已切换到深色主题" : "已切换到浅色主题",
				UiFrame_Now(), 2000, UI_TOAST_INFO);
		} else if (act == 3) {
			PostMessageW(g.hwnd, WM_CLOSE, 0, 0);
		}
		if (act != 3) {
			/* 离散动作会改状态文字/按钮文案/输入框内容, 没有动画脏区可依赖:
			 * 整窗刷一次。点击是低频事件, 不影响连续动画的 CPU。
			 * demo_advance(0) 让“暂停动画”时状态文字补间也能启动。 */
			demo_advance(0.0);
			InvalidateRect(g.hwnd, NULL, FALSE);
		}
	}
}

/* ── 输入 ─────────────────────────────────────────────────────────── */

static void set_focus(int f)
{
	UiEdit *e = f == 0 ? &g.edName : f == 1 ? &g.edPass : NULL;
	int old = g.focus;
	if (f == g.focus) return;
	g.focus = f;
	g.blinkOn = 1;
	UiIme_SetFocus(&g.ime, e);
	/* 焦点边框/光晕/光标/组合下划线都只影响这两个字段 */
	invalidate_field(old);
	invalidate_field(f);
}

static void demo_mouse_move(float x, float y)
{
	if (g.dragField >= 0) {
		Edit_DragTo(g.dragField == 0 ? &g.edName : &g.edPass, g.gfx,
			g.theme.fInput, g.fieldRc[g.dragField], x);
		invalidate_field(g.dragField);   /* 拖选只改选区, 没有动画通道 */
	}
	Ui_OnMouseMove(&g.ui, x, y);
}

static void demo_mouse_down(float x, float y)
{
	UiId hit;
	Ui_OnMouseDown(&g.ui, x, y);
	/* 用命中结果而不是字段矩形: toast 遮罩/z-index 会优先拦截 */
	hit = Ui_HitTest(&g.ui, x, y);
	if (hit == Ui_Id("demo.edit", 0, 0) || hit == Ui_Id("demo.edit", 1, 0)) {
		int f = hit == Ui_Id("demo.edit", 0, 0) ? 0 : 1;
		UiEdit *e = f == 0 ? &g.edName : &g.edPass;
		set_focus(f);
		Edit_PlaceCaret(e, g.gfx, g.theme.fInput, g.fieldRc[f], x);
		invalidate_field(f);
		g.dragField = f;
		g.blinkOn = 1;
	} else if (hit == UI_ID_NONE) {
		set_focus(-1);
	}
}

static void demo_mouse_up(float x, float y)
{
	if (g.dragField >= 0) {
		Edit_StopDrag(g.dragField == 0 ? &g.edName : &g.edPass);
		g.dragField = -1;
	}
	Ui_OnMouseUp(&g.ui, x, y);
}

static void demo_key(UINT vk)
{
	int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	UiEdit *e = g.focus == 0 ? &g.edName : g.focus == 1 ? &g.edPass : NULL;

	if (UiToast_IsVisible(&g.toast)) {
		if (vk == VK_ESCAPE) {
			UiToast_Hide(&g.toast, UiFrame_Now());
			InvalidateRect(g.hwnd, NULL, FALSE);
		}
		return;
	}

	/* 组合期间的回车/ESC/Tab 先交给输入法收尾, 不能当成"提交/跳字段" */
	if (e && Edit_HasComposition(e) && (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB))
		return;

	if (vk == VK_TAB) {
		if (g.focus < 0) set_focus(shift ? 1 : 0);
		else set_focus((g.focus + (shift ? 1 : 1)) % 2);   /* 只有两个字段: 两边都是另一个 */
		return;
	}
	if (vk == VK_RETURN) {
		if (g.focus >= 0) g.pending = 1;
		return;
	}
	if (vk == VK_ESCAPE) {
		set_focus(-1);
		return;
	}
	if (!e) return;
	if (Edit_Key(e, g.hwnd, vk)) {
		g.blinkOn = 1;
		invalidate_field(g.focus);
	}
}

static void demo_char(wchar_t ch)
{
	UiEdit *e;
	if (UiToast_IsVisible(&g.toast)) return;
	e = g.focus == 0 ? &g.edName : g.focus == 1 ? &g.edPass : NULL;
	if (e && Edit_Char(e, ch)) {
		g.blinkOn = 1;
		invalidate_field(g.focus);
	}
}

/* ── 窗口 ─────────────────────────────────────────────────────────── */

static void apply_dpi(HWND hwnd)
{
	UINT dpi = 96;
	HMODULE u32 = GetModuleHandleW(L"user32.dll");
	if (u32) {
		typedef UINT(WINAPI *FnGetDpiForWindow)(HWND);
		FnGetDpiForWindow f = (FnGetDpiForWindow)GetProcAddress(u32, "GetDpiForWindow");
		if (f) { UINT d = f(hwnd); if (d > 0) dpi = d; }
	}
	demo_set_scale((float)dpi / 96.0f);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE:
		demo_init(hwnd);
		apply_dpi(hwnd);
		SetTimer(hwnd, 2, 530, NULL);      /* 编辑框光标闪烁 */
		UiFrame_Init(&g.clock, hwnd);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		EndPaint(hwnd, &ps);
		demo_render(hwnd, &ps.rcPaint);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_NCHITTEST: {
		LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		RECT rc;
		ScreenToClient(hwnd, &pt);
		GetClientRect(hwnd, &rc);
		/* 顶部标题带可拖动; 关闭键不在带内 */
		if (pt.y >= 0 && pt.y < (LONG)(92.0f * g.scale) &&
			pt.x >= 0 && pt.x < (LONG)(rc.right - 56.0f * g.scale))
			return HTCAPTION;
		return hit;
	}
	case WM_MOUSEMOVE: {
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		if (!g.tracking) {
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(tme);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hwnd;
			tme.dwHoverTime = 0;
			if (TrackMouseEvent(&tme)) g.tracking = 1;
		}
		demo_mouse_move(pt.x / g.scale, pt.y / g.scale);
		return 0;
	}
	case WM_MOUSELEAVE:
		g.tracking = 0;
		Ui_OnMouseLeave(&g.ui);
		return 0;
	case WM_LBUTTONDOWN: {
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		SetCapture(hwnd);
		SetFocus(hwnd);
		demo_mouse_down(pt.x / g.scale, pt.y / g.scale);
		return 0;
	}
	case WM_LBUTTONUP: {
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		demo_mouse_up(pt.x / g.scale, pt.y / g.scale);
		ReleaseCapture();
		return 0;
	}
	case WM_CAPTURECHANGED:
		Ui_OnCaptureChanged(&g.ui);
		return 0;
	case WM_KEYDOWN:
		demo_key((UINT)wp);
		return 0;
	case WM_CHAR:
		demo_char((wchar_t)wp);
		return 0;
	case WM_SETFOCUS:
		UiIme_Apply(&g.ime);
		return 0;
	case WM_KILLFOCUS:
		UiIme_Cancel(&g.ime);
		invalidate_field(g.focus);      /* 去掉未上屏的组合串下划线 */
		return 0;
	case WM_IME_SETCONTEXT:
		UiIme_Pos(&g.ime, g.scale);
		return DefWindowProcW(hwnd, msg, wp,
			(LPARAM)((DWORD)lp & ~(DWORD)ISC_SHOWUICOMPOSITIONWINDOW));
	case WM_IME_STARTCOMPOSITION:
		UiIme_Start(&g.ime);
		UiIme_Pos(&g.ime, g.scale);
		invalidate_field(g.focus);
		return 0;
	case WM_IME_COMPOSITION:
		UiIme_Comp(&g.ime, lp);
		UiIme_Pos(&g.ime, g.scale);
		invalidate_field(g.focus);
		return 0;
	case WM_IME_ENDCOMPOSITION:
		UiIme_End(&g.ime);
		invalidate_field(g.focus);
		return 0;
	case WM_IME_CHAR:
		UiIme_Char(&g.ime, (wchar_t)wp);
		invalidate_field(g.focus);
		return 0;
	case WM_SIZE:
		g.minimized = (wp == SIZE_MINIMIZED) ? 1 : 0;
		if (g.overlay) {
			if (g.minimized) bar_overlay_apply(0);
			else bar_overlay_apply(1);
		}
		if (!g.minimized) {
			UiFrame_ResetTick(&g.clock);
			InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	case WM_TIMER:
		if (wp == 2) {
			g.blinkOn = !g.blinkOn;
			if (g.focus >= 0 && !g.minimized && !UiToast_IsVisible(&g.toast))
				invalidate_field(g.focus);   /* 只闪字段, 不刷整窗 */
		}
		return 0;
	case WM_DPICHANGED: {
		const RECT *sugg = (const RECT *)lp;
		apply_dpi(hwnd);
		SetWindowPos(hwnd, NULL, sugg->left, sugg->top,
			sugg->right - sugg->left, sugg->bottom - sugg->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, 2);
		UiFrame_Shutdown(&g.clock);
		demo_shutdown();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static void place_window(HWND hwnd)
{
	RECT wa;
	int w = (int)(DEMO_W * g.scale), h = (int)(DEMO_H * g.scale);
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
	SetWindowPos(hwnd, NULL,
		wa.left + ((wa.right - wa.left) - w) / 2,
		wa.top + ((wa.bottom - wa.top) - h) / 2,
		w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void enable_dpi_awareness(void)
{
	HMODULE u32 = GetModuleHandleW(L"user32.dll");
	if (u32) {
		typedef BOOL(WINAPI *FnSetCtx)(HANDLE);
		FnSetCtx f = (FnSetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
		if (f) {
			/* PER_MONITOR_AWARE_V2 = (DPI_AWARENESS_CONTEXT)-4 */
			if (f((HANDLE)(INT_PTR)-4)) return;
		}
	}
	SetProcessDPIAware();
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	WNDCLASSW wc;
	MSG msg;
	int quit = 0;

	(void)prev; (void)cmd; (void)show;
	enable_dpi_awareness();
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	Log_Init();

	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.lpszClassName = L"CuiDemoWnd";
	if (!RegisterClassW(&wc)) return 1;

	g.hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
		L"CuiDemoWnd", L"CUI Demo", WS_POPUP,
		0, 0, 0, 0, NULL, NULL, inst, NULL);
	if (!g.hwnd) return 1;
	place_window(g.hwnd);
	ShowWindow(g.hwnd, SW_SHOW);
	UpdateWindow(g.hwnd);

	UiFrame_ResetTick(&g.clock);
	while (!quit) {
		int anim = demo_animating();
		if (anim) {
			double t0 = UiFrame_Now();
			double dt = UiFrame_Tick(&g.clock);
			demo_advance(dt);
			if (UiToast_Tick(&g.toast, UiFrame_Now()))
				InvalidateRect(g.hwnd, NULL, FALSE);  /* 遮罩是整窗的 */
			if (UiToast_IsVisible(&g.toast)) {
				/* 模态期间不局部刷新基础 UI, 只让 Toast 的整窗帧走 */
				UiRect dummy;
				Ui_AnimTakeDirty(&g.ui, &dummy);
				if (g.hwnd) UpdateWindow(g.hwnd);
			} else {
				if ((!g.overlay || !Gfx_OverlayAlive(g.overlay)) && g.barRc.w > 0.0f)
					invalidate_dip(g.barRc, 2.0f);   /* 回退: 主窗刷进度条 */
				if (g.hwnd) {
					/* 进度条由 overlay 的 DWM 动画负责; 主窗只刷状态/悬停等 */
					invalidate_anim_dirty();
					UpdateWindow(g.hwnd);
				}
			}
			UiFrame_Delay(&g.clock, g.clock.periodMs - (UiFrame_Now() - t0));
		} else {
			MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		}
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) { quit = 1; break; }
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	Log_Close();
	CoUninitialize();
	return 0;
}

#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
	__security_init_cookie();
	ExitProcess(WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}
#else
int main(void)
{
	return WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT);
}
#endif
