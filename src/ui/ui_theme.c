#include "ui/ui_theme.h"

/* ── 语义色派生 ───────────────────────────────────────────────────── */

static int theme_luma(UiColor c)
{
	int r = (int)((c >> 16) & 0xFF);
	int g = (int)((c >> 8) & 0xFF);
	int b = (int)(c & 0xFF);
	return (r * 299 + g * 587 + b * 114) / 1000;
}

static UiColor theme_on(UiColor c)
{
	return theme_luma(c) > 145 ? 0xFF10151F : 0xFFFFFFFF;
}

static int theme_dark(const UiTheme *t)
{
	return t->themeId == UI_THEME_DARK;
}

/* 低饱和底板: 向卡片色靠, 深色外壳保留更多原色. */
static UiColor theme_soft(const UiTheme *t, UiColor base)
{
	return Ui_ColorMix(t->card, base, theme_dark(t) ? 0.22f : 0.10f);
}

/* 填充型角色 (primary): 高亮悬停、压暗按下、对比文字. */
static void role_set_fill(UiColorRole *r, const UiTheme *t, UiColor base)
{
	UiColor white = 0xFFFFFFFFu;
	r->base = base;
	r->soft = theme_soft(t, base);
	r->hover = Ui_ColorMix(base, white, theme_dark(t) ? 0.18f : 0.10f);
	r->press = Ui_ColorMix(base, 0xFF000000u, 0.16f);
	r->text = theme_on(base);
	r->border = 0;
}

/* 强调型角色 (warning/success/error): 浅底板 + 同色文字. */
static void role_set_accent(UiColorRole *r, const UiTheme *t, UiColor base)
{
	r->base = base;
	r->soft = theme_soft(t, base);
	r->hover = Ui_ColorMix(r->soft, base, 0.12f);
	r->press = Ui_ColorMix(r->soft, 0xFF000000u, theme_dark(t) ? 0.10f : 0.06f);
	r->text = base;
	r->border = base;
}

/* 中性角色 (default): 底色就是 base, 文字自动取对比色. */
static void role_set_neutral(UiColorRole *r, UiColor base)
{
	int light = theme_luma(base) > 145;
	UiColor white = 0xFFFFFFFFu;
	UiColor black = 0xFF000000u;
	r->base = base;
	r->text = theme_on(base);
	r->hover = light ? Ui_ColorMix(base, black, 0.06f) : Ui_ColorMix(base, white, 0.08f);
	r->press = light ? Ui_ColorMix(base, black, 0.12f) : Ui_ColorMix(base, black, 0.06f);
	r->soft = light ? Ui_ColorMix(base, black, 0.04f) : Ui_ColorMix(base, white, 0.05f);
	r->border = light ? Ui_ColorMix(base, black, 0.12f) : Ui_ColorMix(base, white, 0.16f);
}

/* 内置预设 -> 角色. 保留原配色, 不改内置外观. */
static void theme_bind_roles(UiTheme *t)
{
	t->primary.base = t->blue;
	t->primary.hover = t->btnPrimaryHover;
	t->primary.press = t->btnPrimaryPress;
	t->primary.soft = theme_soft(t, t->blue);
	t->primary.text = t->btnPrimaryText;
	t->primary.border = 0;

	role_set_accent(&t->warning, t, t->orange);
	role_set_accent(&t->success, t, t->green);
	t->success.soft = t->okBg;
	t->error.base = t->danger;
	t->error.soft = t->dangerBg;
	t->error.hover = t->dangerBg;
	t->error.press = Ui_ColorShift(t->dangerBg, -8);
	t->error.text = t->danger;
	t->error.border = t->danger;

	t->neutral.base = t->btnBg;
	t->neutral.hover = t->btnHoverBg;
	t->neutral.press = t->btnPressBg;
	t->neutral.soft = t->btnDisabledBg;
	t->neutral.text = t->btnText;
	t->neutral.border = t->btnBorder;
}

/* 自定义角色 -> 旧 token, 保证老组件一起换肤. */
static void theme_write_primary(UiTheme *t)
{
	t->blue = t->primary.base;
	t->blue2 = t->primary.hover;
	t->switchOn = t->primary.base;
	t->inputFocus = t->primary.base;
	t->btnPrimary = Ui_ColorMix(t->primary.base, 0xFFFFFFFFu, 0.08f);
	t->btnPrimary2 = t->primary.base;
	t->btnPrimaryHover = t->primary.hover;
	t->btnPrimaryPress = t->primary.press;
	t->btnPrimaryText = t->primary.text;
	t->btnPrimaryBorder = t->primary.border;
	t->progressFill = Ui_ColorMix(t->primary.base, 0xFFFFFFFFu, 0.10f);
	t->progressFill2 = t->primary.base;
}

static void theme_write_warning(UiTheme *t)
{
	t->orange = t->warning.base;
	t->gold = Ui_ColorMix(t->warning.base, 0xFFFFFFFFu, 0.12f);
}

static void theme_write_success(UiTheme *t)
{
	t->green = t->success.base;
	t->okBg = t->success.soft;
}

static void theme_write_error(UiTheme *t)
{
	t->red = t->error.base;
	t->danger = t->error.base;
	t->dangerBg = t->error.soft;
}

static void theme_write_neutral(UiTheme *t)
{
	int light = theme_luma(t->neutral.base) > 145;
	t->btnBg = t->neutral.base;
	t->btnHoverBg = t->neutral.hover;
	t->btnPressBg = t->neutral.press;
	t->btnText = t->neutral.text;
	t->btnBorder = t->neutral.border;
	t->btnHoverBorder = light
		? Ui_ColorMix(t->neutral.base, 0xFF000000u, 0.18f)
		: Ui_ColorMix(t->neutral.base, 0xFFFFFFFFu, 0.22f);
	t->btnDisabledBg = t->neutral.soft;
	t->btnDisabledText = Ui_ColorMix(t->neutral.text, t->neutral.base, 0.55f);
}

static void theme_apply_preset(UiTheme *t, UiThemeId id)
{
	t->themeId = id;
	t->windowRadius = 14.0f;
	t->panelRadius = 14.0f;
	t->buttonRadius = 7.0f;
	t->progressRadius = 5.0f;
	t->toastRadius = 12.0f;

	if (id == UI_THEME_DARK) {
		t->page = 0xFF121722;
		t->page2 = 0xFF0B1018;
		t->card = 0xFF1B2230;
		t->border = 0xFF2C3545;
		t->frame = 0xFF2C3545;
		t->frameHi = 0x18FFFFFF;
		t->text = 0xFFE8ECF5;
		t->text2 = 0xFFB8C1D6;
		t->text3 = 0xFF8A94A8;
		t->text4 = 0xFF5F6A80;
		t->green = 0xFF4ADE80;
		t->gold = 0xFFF2C66D;
		t->red = 0xFFFF7A88;
		t->orange = 0xFFF0A35E;
		t->blue = 0xFF6F8CFF;
		t->blue2 = 0xFF7C97FF;
		t->btnBg = 0xFF232B3A;
		t->switchOn = 0xFF7C97FF;
		t->switchOff = 0xFF3A4456;

		t->panelTop = 0xFF1B2230;
		t->panelBottom = 0xFF111823;
		t->hdrLine = 0xFF2C3545;
		t->tab1 = 0xFF2DD4BF;
		t->tab2 = 0xFF6F8CFF;
		t->btn1 = 0xFF5DD1FF;
		t->btn2 = 0xFF4C8DFF;
		t->iconBlue = 0xFF8AA4FF;
		t->panelText = 0xFFB8C1D6;
		t->panelDisabled = 0xFF6B768A;
		t->tile = 0xFF232C3B;
		t->pill = 0xFF3A4456;
		t->power = 0xFF8AA4FF;

		t->controlBox = 0xFF1F2735;
		t->controlBorder = 0xFF2C3545;
		t->rowOn = 0xFF24304A;
		t->rowOnBorder = 0xFF35507A;
		t->rowOff = 0xFF1B2230;
		t->rowOffBorder = 0xFF2C3545;
		t->pageLine = 0xFF2C3545;

		t->inputBg = 0xFF161C27;
		t->inputBorder = 0xFF2E3848;
		t->inputFocus = 0xFF7C97FF;
		t->hint = 0xFF6F7A90;
		t->danger = 0xFFFF7A88;
		t->dangerBg = 0xFF3A1F26;
		t->okBg = 0xFF14301F;

		t->panelBg = t->card;
		t->panelBorder = t->border;

		t->btnPrimary = 0xFF5E7BFF;
		t->btnPrimary2 = 0xFF3F5BD8;
		t->btnPrimaryHover = 0xFF718CFF;
		t->btnPrimaryPress = 0xFF3550C8;
		t->btnPrimaryText = 0xFFFFFFFF;
		t->btnPrimaryBorder = 0;
		t->btnHoverBg = 0xFF2B3547;
		t->btnPressBg = 0xFF1B2230;
		t->btnText = 0xFFE0E6F2;
		t->btnBorder = 0xFF364153;
		t->btnHoverBorder = 0xFF455169;
		t->btnDisabledBg = 0xFF252C39;
		t->btnDisabledText = 0xFF6B768A;

		t->progressTrack = 0xFF2A3342;
		t->progressFill = 0xFF718CFF;
		t->progressFill2 = 0xFF4F6BFF;

		t->toastBg = 0xFF232B3A;
		t->toastBorder = 0xFF364153;
		t->toastText = 0xFFE8ECF5;
		t->toastOverlay = 0xFF000000;
		return;
	}

	/* 冷灰蓝 + 靛蓝主色: 比 Element 默认的 #409EFF 更沉一点, 配浅紫背景不刺眼 */
	t->page = 0xFFF7F9FF;
	t->page2 = 0xFFECF1FB;
	t->card = 0xFFFFFFFF;
	t->border = 0xFFE6EBF5;
	t->frame = 0xFFE3E8F3;
	t->frameHi = 0x70FFFFFF;
	t->text = 0xFF1F2637;
	t->text2 = 0xFF4B5567;
	t->text3 = 0xFF7C8698;
	t->text4 = 0xFFB7BFCF;
	t->green = 0xFF2FA56B;
	t->gold = 0xFFE8B45A;
	t->red = 0xFFEE5D6C;
	t->orange = 0xFFE39A45;
	t->blue = 0xFF4C6FFF;
	t->blue2 = 0xFF5B7CFA;
	t->btnBg = 0xFFFFFFFF;
	t->switchOn = 0xFF5B7CFA;
	t->switchOff = 0xFFD9DFEC;

	t->panelTop = 0xFFFFFFFF;
	t->panelBottom = 0xFFEDF2FF;
	t->hdrLine = 0xFFD1DCFF;
	t->tab1 = 0xFF00C7E5;
	t->tab2 = 0xFF1068FF;
	t->btn1 = 0xFF5DD1FF;
	t->btn2 = 0xFF4C8DFF;
	t->iconBlue = 0xFF3562F6;
	t->panelText = 0xFF666666;
	t->panelDisabled = 0xFFB0B6C0;
	t->tile = 0xFFEFF3FF;
	t->pill = 0xFFD3DBF3;
	t->power = 0xFF83A0FF;

	t->controlBox = 0xFFFAFAFA;
	t->controlBorder = 0xFFEBEEF5;
	t->rowOn = 0xFFECF5FF;
	t->rowOnBorder = 0xFFD9ECFF;
	t->rowOff = 0xFFFAFAFA;
	t->rowOffBorder = 0xFFEBEEF5;
	t->pageLine = 0xFFF0F2F5;

	t->inputBg = 0xFFF8FAFF;
	t->inputBorder = 0xFFDEE5F2;
	t->inputFocus = 0xFF5B7CFA;
	t->hint = 0xFFA0A9BB;
	t->danger = 0xFFEE5D6C;
	t->dangerBg = 0xFFFFF1F3;
	t->okBg = 0xFFEFFAF4;

	t->panelBg = t->card;
	t->panelBorder = t->border;

	t->btnPrimary = 0xFF6E8CFF;
	t->btnPrimary2 = 0xFF4C6FFF;
	t->btnPrimaryHover = 0xFF7E9BFF;
	t->btnPrimaryPress = 0xFF3F5BE0;
	t->btnPrimaryText = 0xFFFFFFFF;
	t->btnPrimaryBorder = 0;
	t->btnHoverBg = 0xFFF1F3FA;
	t->btnPressBg = 0xFFE4E8F5;
	t->btnText = t->text2;
	t->btnBorder = 0xFFDEE5F2;
	t->btnHoverBorder = 0xFFD0D8E8;
	t->btnDisabledBg = 0xFFF2F4F9;
	t->btnDisabledText = t->panelDisabled;

	t->progressTrack = 0xFFEDF1F9;
	t->progressFill = 0xFF7C97FF;
	t->progressFill2 = 0xFF4C6FFF;

	t->toastBg = 0xFFFFFFFF;
	t->toastBorder = 0xFFDCE3F2;
	t->toastText = t->text;
	t->toastOverlay = 0xFF0B1020;
}

void UiTheme_Init(UiTheme *t, Gfx *gfx)
{
	if (!t || !gfx) return;
	t->fTitle = Gfx_CreateFont(gfx, 16.0f, 700);
	t->fHead = Gfx_CreateFont(gfx, 15.0f, 700);
	t->fBody = Gfx_CreateFont(gfx, 13.0f, 400);
	t->fSmall = Gfx_CreateFont(gfx, 11.0f, 400);
	t->fTemp = Gfx_CreateFont(gfx, 26.0f, 700);
	t->fBtn = Gfx_CreateFont(gfx, 12.0f, 700);
	t->fMini = Gfx_CreateFont(gfx, 11.0f, 400);
	t->fTempBig = Gfx_CreateFont(gfx, 46.0f, 700);
	t->fInput = Gfx_CreateFont(gfx, 14.0f, 400);
	t->fLabel = Gfx_CreateFont(gfx, 12.0f, 700);
	t->fBig = Gfx_CreateFont(gfx, 22.0f, 700);

	UiTheme_SetTheme(t, UI_THEME_LIGHT);
}

void UiTheme_Touch(UiTheme *t)
{
	if (t) t->themeRev++;
}

void UiTheme_SetTheme(UiTheme *t, UiThemeId id)
{
	if (!t) return;
	theme_apply_preset(t, id);
	theme_bind_roles(t);
	UiTheme_Touch(t);
}

void UiTheme_SetColors(UiTheme *t, UiThemeId base, const UiThemeColors *colors)
{
	if (!t) return;
	theme_apply_preset(t, base);
	theme_bind_roles(t);
	if (colors) {
		if (colors->primary) {
			role_set_fill(&t->primary, t, colors->primary);
			theme_write_primary(t);
		}
		if (colors->warning) {
			role_set_accent(&t->warning, t, colors->warning);
			theme_write_warning(t);
		}
		if (colors->success) {
			role_set_accent(&t->success, t, colors->success);
			theme_write_success(t);
		}
		if (colors->error) {
			role_set_accent(&t->error, t, colors->error);
			theme_write_error(t);
		}
		if (colors->neutral) {
			role_set_neutral(&t->neutral, colors->neutral);
			theme_write_neutral(t);
		}
	}
	UiTheme_Touch(t);
}

void UiTheme_Shutdown(UiTheme *t, Gfx *gfx)
{
	if (!t) return;
	Gfx_DestroyFont(gfx, t->fTitle);
	Gfx_DestroyFont(gfx, t->fHead);
	Gfx_DestroyFont(gfx, t->fBody);
	Gfx_DestroyFont(gfx, t->fSmall);
	Gfx_DestroyFont(gfx, t->fTemp);
	Gfx_DestroyFont(gfx, t->fBtn);
	Gfx_DestroyFont(gfx, t->fMini);
	Gfx_DestroyFont(gfx, t->fTempBig);
	Gfx_DestroyFont(gfx, t->fInput);
	Gfx_DestroyFont(gfx, t->fLabel);
	Gfx_DestroyFont(gfx, t->fBig);
	t->fTitle = t->fHead = t->fBody = t->fSmall = NULL;
	t->fTemp = t->fBtn = t->fMini = t->fTempBig = NULL;
	t->fInput = t->fLabel = t->fBig = NULL;
}
