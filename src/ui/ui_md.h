/*
 * ui_md.h - Markdown 渲染组件 (应用持有)
 *
 * 解析用 ui_md_parse (纯 C), 排版用 DirectWrite 富文本 (gfx_rich),
 * 滚动用 UiScrollView/UiScrollLayout。当前阶段: 行模型 + 懒测量 + 滚动 +
 * 段落/标题/列表/引用/代码纯文本绘制; 装饰/链接/图片/表格见后续阶段。
 *
 * 设计见 docs/CUI-Markdown-设计方案.md
 */
#ifndef CUI_UI_MD_H
#define CUI_UI_MD_H

#include "gfx/gfx.h"
#include "net/ui_net.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiMarkdown UiMarkdown;
typedef struct UiMdSpan { int start, len; uint8_t style; } UiMdSpan;

/* UiMdSpan.style (tokenizer 回调) */
enum {
	MD_TOK_KEYWORD = 1,
	MD_TOK_STRING = 2,
	MD_TOK_COMMENT = 3,
	MD_TOK_NUMBER = 4
};

typedef void (*UiMarkdownLinkFn)(void *user, const char *url);
typedef void (*UiMarkdownCopyFn)(void *user, const wchar_t *text16, int len16);
/* 返回写入 out 的 span 数; start/len 为 UTF-16 码元 */
typedef int (*UiMarkdownTokenizeFn)(void *user, const char *lang,
	const wchar_t *text16, int len16, UiMdSpan *out, int cap);

typedef struct UiMarkdownStyle {
	/* 字体: NULL = 按默认字号/字重派生 (h1..h6 由内置梯度) */
	GfxFont *h1, *h2, *h3, *h4, *h5, *h6;
	GfxFont *body, *bold, *italic, *code, *tiny;

	float blockGap;         /* 普通块间距 */
	float lineSpacing;      /* 行高倍数; 0 = 1.25 */
	float headingGapAbove, headingGapBelow;
	float listIndent, quoteIndent, quoteBarW;
	float codePadX, codePadY, codeRadius;
	float tableCellPadX, tableCellPadY, tableMinColW;
	float imageMaxH, imagePlaceholderH;
	int   imageAlign;       /* 默认图片对齐: 0=左 1=中 2=右 */
	float ruleH, ruleGap;

	UiColor text, text2, link, linkHover;
	UiColor codeText, codeBg, codeBorder;
	UiColor inlineCodeText, inlineCodeBg;   /* 行内代码 (与代码块分离) */
	UiColor quoteBar, quoteBg, tableBorder, tableHeaderBg;
	UiColor ruleColor, markerColor;
	UiColor tokKeyword, tokString, tokComment, tokNumber;
} UiMarkdownStyle;

/* 内置两套样式: 返回完整具体值 (字体项为 NULL, 由组件派生) */
UiMarkdownStyle UiMarkdownStyle_Light(void);
UiMarkdownStyle UiMarkdownStyle_Dark(void);
/* 从当前主题派生 (浅/深随 themeId); 组件默认模式即每帧调用它 */
UiMarkdownStyle UiMarkdownStyle_FromTheme(const UiTheme *t);

UiMarkdown *UiMarkdown_Create(Gfx *gfx, UiId id, UiNet *net /* 可 NULL */);
void UiMarkdown_Destroy(UiMarkdown *md);

void UiMarkdown_SetText(UiMarkdown *md, const char *utf8);  /* 全量; NULL 清空 */
void UiMarkdown_Append(UiMarkdown *md, const char *utf8);   /* 流式追加 */
void UiMarkdown_SetStyle(UiMarkdown *md, const UiMarkdownStyle *st); /* NULL=跟随主题 */
void UiMarkdown_SetBasePath(UiMarkdown *md, const wchar_t *dir);

void UiMarkdown_SetLinkFn(UiMarkdown *md, UiMarkdownLinkFn fn, void *user);
void UiMarkdown_SetCopyFn(UiMarkdown *md, UiMarkdownCopyFn fn, void *user); /* 替代默认剪贴板写入 */
void UiMarkdown_SetTokenizeFn(UiMarkdown *md, UiMarkdownTokenizeFn fn, void *user);

/* 绘制 + 输入; 必须在 Ui_BeginFrame/EndFrame 之间调用。
 * 返回 1 = 本帧有事件 (链接点击/复制)。 */
int  Ui_Markdown(UiMarkdown *md, UiContext *ui, UiRect rc);

const char *UiMarkdown_ClickedUrl(const UiMarkdown *md);  /* 本帧有效, 否则 NULL */
int  UiMarkdown_Busy(const UiMarkdown *md);
void UiMarkdown_ScrollToTop(UiMarkdown *md);
void UiMarkdown_SetFollowTail(UiMarkdown *md, int on);
float UiMarkdown_ContentHeight(const UiMarkdown *md);

/* 诊断 (阶段 3 懒测量验收): 行数 / 已实测行数 / 本帧实测次数 */
int UiMarkdown_DebugRows(const UiMarkdown *md);
int UiMarkdown_DebugMeasured(const UiMarkdown *md);
int UiMarkdown_DebugMeasureCalls(const UiMarkdown *md);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_MD_H */
