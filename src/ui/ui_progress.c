#include "ui/ui_progress.h"
#include "ui/ui_widgets.h"

static void progress_drop_overlay(UiProgress *p)
{
	if (p && p->overlay) {
		Gfx_OverlayDestroy(p->overlay);
		p->overlay = NULL;
	}
}

static void progress_apply(UiProgress *p)
{
	int remain;
	if (!p || !p->overlay) return;
	if (p->hidden) return;
	Gfx_OverlaySetReveal(p->overlay, p->value, 0);
	remain = (int)((1.0f - p->value) * p->cycleMs);
	if (!p->paused && remain > 0)
		Gfx_OverlaySetReveal(p->overlay, 1.0f, remain);
}

void UiProgress_Init(UiProgress *p, UiContext *ui, int cycleMs)
{
	if (!p) return;
	ZeroMemory(p, sizeof(*p));
	p->ui = ui;
	p->cycleMs = cycleMs > 0 ? (float)cycleMs : 6000.0f;
	p->themeId = (ui && ui->theme) ? ui->theme->themeId : UI_THEME_LIGHT;
}

void UiProgress_Tick(UiProgress *p, double dtMs)
{
	if (!p || p->cycleMs <= 0.0f || p->paused) return;
	p->value += (float)(dtMs / (double)p->cycleMs);
	if (p->value >= 1.0f) {
		p->value -= 1.0f;
		if (p->overlay) {
			/* 隐藏时也继续跑, 恢复显示时不会停在旧位置 */
			Gfx_OverlaySetReveal(p->overlay, 0.0f, 0);
			Gfx_OverlaySetReveal(p->overlay, 1.0f, (int)p->cycleMs);
		}
	}
}

void UiProgress_SetPaused(UiProgress *p, int paused)
{
	if (!p) return;
	paused = paused ? 1 : 0;
	if (p->paused == paused) return;
	p->paused = paused;
	if (p->overlay && !p->hidden) {
		if (paused) Gfx_OverlaySetReveal(p->overlay, p->value, 0);
		else progress_apply(p);
	}
}

void UiProgress_SetHidden(UiProgress *p, int hidden)
{
	if (!p) return;
	hidden = hidden ? 1 : 0;
	if (p->hidden == hidden) return;
	p->hidden = hidden;
	if (!p->overlay) return;
	if (hidden) {
		if (!Gfx_OverlaySetOpacity(p->overlay, 0.0f))
			progress_drop_overlay(p);   /* 老系统没有 Visual3: 回退主窗 */
	} else {
		Gfx_OverlaySetOpacity(p->overlay, 1.0f);
		progress_apply(p);   /* 恢复显示时对齐当前比例 */
	}
}

void UiProgress_Draw(UiProgress *p, UiRect rc)
{
	int fill;
	if (!p || !p->ui) return;
	p->rc = rc;
	fill = !p->overlay || !Gfx_OverlayAlive(p->overlay) || p->hidden;
	Ui_ProgressBar(p->ui, rc, p->value, fill);
}

void UiProgress_EndFrame(UiProgress *p)
{
	UiContext *ui;
	UiTheme *t;
	int rc;
	if (!p || !p->ui) return;
	ui = p->ui;
	if (!ui->gfx || !ui->theme) return;
	t = ui->theme;

	if (p->overlay && !Gfx_OverlayAlive(p->overlay))
		progress_drop_overlay(p);
	if (p->overlay && p->themeId != t->themeId)
		progress_drop_overlay(p);
	p->themeId = t->themeId;

	if (p->hidden) {
		if (p->overlay && !Gfx_OverlaySetOpacity(p->overlay, 0.0f))
			progress_drop_overlay(p);
		return;
	}
	if (p->rc.w <= 0.0f || p->rc.h <= 0.0f) return;

	if (!p->overlay) {
		p->overlay = Gfx_CreateOverlay(ui->gfx, p->rc, t->progressRadius,
			t->progressFill, t->progressFill2);
		if (!p->overlay) return;         /* ULW: Draw 已走回退填充 */
		Gfx_OverlaySetOpacity(p->overlay, 1.0f);
		progress_apply(p);
		return;
	}

	Gfx_OverlaySetOpacity(p->overlay, 1.0f);
	rc = Gfx_OverlaySetRect(p->overlay, p->rc);
	if (rc == 0) {
		progress_drop_overlay(p);
		return;
	}
	if (rc == 2) progress_apply(p);      /* 重建后恢复当前比例 */
}

int UiProgress_Paused(const UiProgress *p)
{
	return p ? p->paused : 1;
}

int UiProgress_OverlayAlive(const UiProgress *p)
{
	return p && p->overlay && Gfx_OverlayAlive(p->overlay);
}

int UiProgress_NeedsMainRedraw(const UiProgress *p)
{
	if (!p) return 0;
	return !p->overlay || !Gfx_OverlayAlive(p->overlay) || p->hidden;
}

float UiProgress_Value(const UiProgress *p)
{
	return p ? p->value : 0.0f;
}

void UiProgress_Shutdown(UiProgress *p)
{
	if (!p) return;
	progress_drop_overlay(p);
}
