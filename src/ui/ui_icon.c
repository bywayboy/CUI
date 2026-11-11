#include "ui/ui_icon.h"

static int icon_utf8(uint32_t cp, char out[5])
{
	if (cp <= 0x7Fu) {
		out[0] = (char)cp;
		out[1] = 0;
		return 1;
	}
	if (cp <= 0x7FFu) {
		out[0] = (char)(0xC0u | (cp >> 6));
		out[1] = (char)(0x80u | (cp & 0x3Fu));
		out[2] = 0;
		return 2;
	}
	if (cp <= 0xFFFFu) {
		out[0] = (char)(0xE0u | (cp >> 12));
		out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		out[2] = (char)(0x80u | (cp & 0x3Fu));
		out[3] = 0;
		return 3;
	}
	if (cp <= 0x10FFFFu) {
		out[0] = (char)(0xF0u | (cp >> 18));
		out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
		out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		out[3] = (char)(0x80u | (cp & 0x3Fu));
		out[4] = 0;
		return 4;
	}
	out[0] = 0;
	return 0;
}

void UiIcon_Draw(UiContext *ui, const UiIcon *icon, UiRect rc,
	UiColor tint, int enabled)
{
	char utf8[5];
	UiTheme *t;
	if (!ui || !ui->gfx || !icon || icon->kind != UI_ICON_GLYPH || !icon->font)
		return;
	if (rc.w <= 0.0f || rc.h <= 0.0f) return;
	if (!icon_utf8(icon->codepoint, utf8)) return;
	t = ui->theme;
	if (!enabled && t) tint = t->btnDisabledText;
	else if (!tint && t) tint = t->text;
	Gfx_Text(ui->gfx, icon->font, utf8, rc, tint,
		GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
}
