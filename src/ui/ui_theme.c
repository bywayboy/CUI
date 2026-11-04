#include "ui/ui_theme.h"

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

void UiTheme_SetTheme(UiTheme *t, UiThemeId id)
{
	if (!t) return;
	t->themeId = id;
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
