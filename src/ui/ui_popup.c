#include "ui/ui_popup.h"

static float popup_clamp(float v, float lo, float hi)
{
	if (lo > hi) return lo;
	return v < lo ? lo : (v > hi ? hi : v);
}

static float popup_align_x(UiRect anchor, float w, int align)
{
	if (align == UI_POPUP_ALIGN_CENTER) return anchor.x + (anchor.w - w) * 0.5f;
	if (align == UI_POPUP_ALIGN_END) return anchor.x + anchor.w - w;
	return anchor.x;
}

static float popup_align_y(UiRect anchor, float h, int align)
{
	if (align == UI_POPUP_ALIGN_CENTER) return anchor.y + (anchor.h - h) * 0.5f;
	if (align == UI_POPUP_ALIGN_END) return anchor.y + anchor.h - h;
	return anchor.y;
}

UiRect UiPopup_Place(const UiContext *ui, UiRect anchor, float w, float h,
	int side, int align, float gap)
{
	float vw = ui && ui->width > 0.0f ? ui->width : 100000.0f;
	float vh = ui && ui->height > 0.0f ? ui->height : 100000.0f;
	float margin = 8.0f;
	float maxW = vw - margin * 2.0f;
	float maxH = vh - margin * 2.0f;
	float below, above, left, right, x, y;

	if (maxW < 0.0f) maxW = 0.0f;
	if (maxH < 0.0f) maxH = 0.0f;
	if (w < 0.0f) w = 0.0f;
	if (h < 0.0f) h = 0.0f;
	if (w > maxW) w = maxW;
	if (h > maxH) h = maxH;
	if (gap < 0.0f) gap = 0.0f;

	if (side == UI_POPUP_RIGHT || side == UI_POPUP_LEFT) {
		right = vw - margin - (anchor.x + anchor.w);
		left = anchor.x - margin;
		if (side == UI_POPUP_RIGHT) {
			if (right >= w + gap) x = anchor.x + anchor.w + gap;
			else if (left >= w + gap) x = anchor.x - gap - w;
			else x = right >= left ? anchor.x + anchor.w + gap
				: anchor.x - gap - w;
		} else {
			if (left >= w + gap) x = anchor.x - gap - w;
			else if (right >= w + gap) x = anchor.x + anchor.w + gap;
			else x = left >= right ? anchor.x - gap - w
				: anchor.x + anchor.w + gap;
		}
		y = popup_align_y(anchor, h, align);
	} else {
		below = vh - margin - (anchor.y + anchor.h);
		above = anchor.y - margin;
		if (side == UI_POPUP_BELOW) {
			if (below >= h + gap) y = anchor.y + anchor.h + gap;
			else if (above >= h + gap) y = anchor.y - gap - h;
			else y = below >= above ? anchor.y + anchor.h + gap
				: anchor.y - gap - h;
		} else {
			if (above >= h + gap) y = anchor.y - gap - h;
			else if (below >= h + gap) y = anchor.y + anchor.h + gap;
			else y = above >= below ? anchor.y - gap - h
				: anchor.y + anchor.h + gap;
		}
		x = popup_align_x(anchor, w, align);
	}

	/* 最终只允许在视口内; 如果两侧都放不下, 这里会夹取, 可能覆盖 anchor
	 * (窗口/锚点本身太满时无解). 只要任意一侧空间足够, 上面的分支不会
	 * 产生覆盖. */
	x = popup_clamp(x, margin, vw - margin - w);
	y = popup_clamp(y, margin, vh - margin - h);
	return Ui_Rect(x, y, w, h);
}

/* ── 弹层交互管理器 ───────────────────────────────────────────────── */

void UiPopupManager_Init(UiPopupManager *pm)
{
	if (!pm) return;
	ZeroMemory(pm, sizeof(*pm));
}

int UiPopupManager_IsOpen(const UiPopupManager *pm, UiId id)
{
	return pm && id && pm->openId == id;
}

int UiPopupManager_AnyOpen(const UiPopupManager *pm)
{
	return pm && pm->openId != UI_ID_NONE;
}

void UiPopupManager_Open(UiPopupManager *pm, UiContext *ui, UiId id,
	UiId owner, int focusOnClose)
{
	if (!pm || !ui || !id) return;
	/* 同一会话切换触发项 (如导航栏切换顶级项) 也要更新 owner/scope. */
	pm->openId = id;
	pm->ownerId = owner;
	pm->scope = ui->inputOwner;
	pm->focusOnClose = focusOnClose != 0;
	pm->closed = 0;
	pm->outsidePress = 0;
	ui->popupOpen = 1;
	if (owner) ui->navId = owner;
	/* 弹层在触发器脏区之外, 只失效按钮矩形会画不出来. */
	if (ui->hwnd) InvalidateRect(ui->hwnd, NULL, FALSE);
}

void UiPopupManager_Close(UiPopupManager *pm, UiContext *ui)
{
	if (!pm || !pm->openId) return;
	pm->openId = UI_ID_NONE;
	pm->closed = 1;
	if (ui) {
		ui->popupOpen = 0;
		if (pm->ownerId) {
			ui->navId = pm->ownerId;
			ui->navFocusVisible = pm->focusOnClose;
		}
		/* 关闭后必须擦掉整个弹层. */
		if (ui->hwnd) InvalidateRect(ui->hwnd, NULL, FALSE);
	}
	pm->ownerId = UI_ID_NONE;
	pm->scope = UI_ID_NONE;
	pm->scopeDepth = 0;
	pm->focusOnClose = 0;
}

void UiPopupManager_BeginDraw(UiPopupManager *pm, UiContext *ui)
{
	if (!pm || !ui) return;
	if (pm->scopeDepth < (int)(sizeof(pm->scopeSaved) /
		sizeof(pm->scopeSaved[0])))
		pm->scopeSaved[pm->scopeDepth++] = ui->inputScope;
	ui->inputScope = pm->scope;
}

void UiPopupManager_EndDraw(UiPopupManager *pm, UiContext *ui)
{
	if (!pm || !ui || pm->scopeDepth <= 0) return;
	ui->inputScope = pm->scopeSaved[--pm->scopeDepth];
}

void UiPopupManager_BeginFrame(UiPopupManager *pm, UiContext *ui)
{
	if (!pm || !ui) return;
	pm->closed = 0;
	pm->outsidePress = 0;
	if (!pm->openId) {
		ui->popupOpen = 0;
		return;
	}
	ui->popupOpen = 1;
	/* 打开时底层不参与命中; 因此鼠标按下后 pressed==0 即"点在弹层外".
	 * 弹层面板本身会注册一个 overlay 命中, 所以点面板空白不会误关. */
	if (ui->mouseDown && ui->pressed == UI_ID_NONE) {
		pm->outsidePress = 1;
		UiPopupManager_Close(pm, ui);
	}
}

int UiPopupManager_Key(UiPopupManager *pm, UiContext *ui, UINT vk)
{
	if (!pm || !pm->openId) return 0;
	if (vk != VK_ESCAPE) return 0;
	/* 键盘关闭: 焦点回到触发器并可见. */
	pm->focusOnClose = 1;
	UiPopupManager_Close(pm, ui);
	return 1;
}
