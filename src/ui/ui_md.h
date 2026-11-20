/*
 * ui_md.h - Markdown 渲染组件 (应用持有)
 *
 * 解析用 ui_md_parse (纯 C), 排版用 DirectWrite 富文本 (gfx_rich),
 * 滚动用 UiScrollView/UiScrollLayout。支持段落/标题/列表/引用/容器/代码/
 * 表格/图片（含显式尺寸）/行内公式与块级公式（gfx_math 可选模块）。
 *
 * 设计见 docs/CUI-Markdown-设计方案.md
 */
#ifndef CUI_UI_MD_H
#define CUI_UI_MD_H

#include "gfx/gfx.h"
#include "net/ui_net.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_md_highlight.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiMarkdown UiMarkdown;

typedef void (*UiMarkdownLinkFn)(void *user, const char *url);
typedef void (*UiMarkdownCopyFn)(void *user, const wchar_t *text16, int len16);
/* 返回需要的 span 总数 (可大于 cap, 组件扩容后重调); start/len 为 UTF-16 */
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
	UiColor tokType, tokFunction, tokPreproc, tokProperty;
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

/* 当前文档地址 (UTF-8): 本地路径 `D:\docs\a.md` 或 `https://host/a.md`.
 * 只用作相对链接/相对图片的基准 (取文档所在目录), 组件不读该地址.
 * 优先级高于 SetBasePath; NULL 清空. */
void UiMarkdown_SetDocumentUrl(UiMarkdown *md, const char *url);
const char *UiMarkdown_DocumentUrl(const UiMarkdown *md);

/* 相对 → 绝对: 基于文档地址解析. 返回 out (写入成功) 或 url 本身
 * (绝对地址/无基准时原样返回). 以 '#' 开头的锚点原样返回.
 * 图片解析内部走同一条路径, 宿主可用它把链接变成可加载地址. */
const char *UiMarkdown_ResolveUrl(const UiMarkdown *md, const char *url,
	char *out, int cap);

/* 兼容旧接口: 目录 (wchar). 新代码用 SetDocumentUrl. */
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

/* 滚动位置/最大偏移 (DIP) 与设置: 供宿主做编辑/预览双视图联动. */
float UiMarkdown_ScrollOffset(const UiMarkdown *md);
float UiMarkdown_MaxOffset(const UiMarkdown *md);
void UiMarkdown_ScrollToOffset(UiMarkdown *md, UiContext *ui,
	float offset, int animate);

/* 滚到指定行, 行顶距视口顶 padTop (DIP).
 * 粘性定位: 跳转后目标行会被钉在视口顶部若干帧, 直到懒测量收敛或用户
 * 主动滚动 (滚轮/拖 thumb). 估算高度累加误差导致的落点漂移靠它修正,
 * 目录跳转与前进/后退恢复位置都应走这里, 而不是直接算 ItemY. */
void UiMarkdown_ScrollToRow(UiMarkdown *md, UiContext *ui, int row,
	float padTop, int animate);

/* 上一帧视口首行; 文档为空/尚未绘制返回 -1. 注意它是"与视口相交的
 * 第一行", 跳转后往往仍是被跳转标题的上一行, 不要拿它反推章节;
 * 目录高亮用 UiMarkdown_ActiveHeading. */
int UiMarkdown_FirstVisibleRow(const UiMarkdown *md);

/* 当前章节: 标题在 UiMarkdown_HeadingAt 里的下标, -1 = 不在任何章节.
 * 判定规则: 显式跳转 (ScrollToRow 落在标题上 / ScrollToAnchor / 点
 * `#锚点`) 后锁定该标题, 直到用户自己滚轮或拖 thumb; 否则取标题行顶
 * 不晚于视口顶 (含少量余量) 的最后一项, 滚到底时取视口内最后一个标题. */
int UiMarkdown_ActiveHeading(const UiMarkdown *md);

/* 章节跳转: name 是锚点名 (不含 '#'), 按 GitHub 风格 slug 匹配
 * (`#Getting Started` 与 `#getting-started` 等价). 返回 1 = 找到并滚动.
 * 文档里 [文字](#slug) 的点击会自动走这条路径, 也会照常触发 linkFn. */
int UiMarkdown_ScrollToAnchor(UiMarkdown *md, UiContext *ui,
	const char *name, int animate);

/* 标题目录: 由当前文档的标题按文档顺序生成, 供左侧目录树使用.
 * 返回的结构在下次 SetText/Append/SetStyle 之前有效 (文本为借用指针).
 * 注意: 只有能生成非空 slug 的标题才会出现在这里 (与锚点表一致). */
typedef struct UiMdHeading {
	int level;              /* 1..6 */
	int row;                /* 行下标, 可传给 ScrollToRow */
	const char *slug;       /* UTF-8, 不含 '#' */
	const wchar_t *text;    /* 借用: 去格式后的标题文本 */
	int textLen;
} UiMdHeading;

int UiMarkdown_HeadingCount(const UiMarkdown *md);
const UiMdHeading *UiMarkdown_HeadingAt(const UiMarkdown *md, int index);

/* 测试访问器 (headless 验收懒测量/行数), 不产生任何输出. */
int UiMarkdown_DebugRows(const UiMarkdown *md);
int UiMarkdown_DebugMeasured(const UiMarkdown *md);
int UiMarkdown_DebugMeasureCalls(const UiMarkdown *md);
int UiMarkdown_DebugMathCache(const UiMarkdown *md);    /* 缓存的公式条目数 */
int UiMarkdown_DebugMathMisses(const UiMarkdown *md);   /* 累计新建布局次数 */
int UiMarkdown_DebugRowSize(void);                      /* sizeof(MdRow) */
int UiMarkdown_DebugRowsReused(const UiMarkdown *md);   /* 上次重建复用的行数 */
int UiMarkdown_DebugImagesReused(const UiMarkdown *md); /* 其中复用的图片行数 */
int UiMarkdown_DebugImagesRetained(const UiMarkdown *md); /* 宽度失效时保留的图片数 */
int UiMarkdown_DebugTableCols(const UiMarkdown *md, int index, float *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_MD_H */
