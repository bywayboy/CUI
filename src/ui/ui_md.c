/*
 * ui_md.c - Markdown 组件: 行模型 + 懒测量 + 滚动 (阶段 3)
 *
 * 每个叶子块 = 一行 = 一个 UiScrollItem:
 *   - 首次布局只做 O(行数) 高度估算, 不创建 DWrite 布局;
 *   - 每帧只实测可见 ±1 行, 实测高度变化时 Commit + 锚点修正;
 *   - 富文本布局只在可见行存在, 视口外 ±32 行缓存, 超出释放。
 *
 * 绘制范围 (阶段 3): 段落/标题/列表/引用/代码纯文本;
 * 表格/图片先画占位; 链接/复制/装饰/图片内嵌在后续阶段。
 */
#include <windows.h>
#include <stdint.h>

#include "ui/ui_md.h"
#include "ui/ui_md_parse.h"
#include "ui/ui_image.h"
#include "ui/ui_scroll.h"
#include "ui/ui_frame.h"
#include "ui/ui_layout.h"
#include "gfx/gfx_rich.h"
#include "gfx/gfx_math.h"

#define MD_OVERSCAN      32
#define MD_MARKER_W      22.0f
#define MD_OWN_FONT_MAX  12

static void md_free_(void *p)
{
	if (p) HeapFree(GetProcessHeap(), 0, p);
}

static void *md_realloc_(void *p, size_t bytes)
{
	if (p) return HeapReAlloc(GetProcessHeap(), 0, p, bytes);
	return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static void *md_alloc_(int n)
{
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)(n > 0 ? n : 1));
}

enum {
	MD_MK_NONE = 0,
	MD_MK_BULLET,
	MD_MK_ORDERED,
	MD_MK_TASK
};

/* 行 payload 按块类型互斥: 必须与 blk->type 标签一致.
 * 公共字段: 缩进/标记/间距/哈希/测量状态. */
typedef struct MdRow {
	MdBlock *blk;
	int indent;          /* DIP */
	int quoteDepth;
	int markerKind;
	int markerNo;
	int task;
	float markerW;
	float extraTop;      /* 容器首行: 标题区高度 */
	float extraBottom;   /* 容器末行: 卡内底部内边距 */
	uint32_t hash;
	uint32_t boxSig;     /* 所属盒子的布局签名 (align/行距/padR);
	                      * 同一段文本在 <div line-height> 里与裸段落的高度
	                      * 不同, 行复用必须把它算进 key, 否则会搬错高度 */
	int measured;
	float measuredW;
	uint32_t liveStamp;  /* == md->liveEpoch 表示已在 live[] 中 */
	uint8_t rowType;     /* 创建时快照: 旧行释放时 blk 可能已随文档释放 */
	int boxIdx;          /* 所属盒子 (div/容器) 下标; -1 = 无. div 的
	                      * align/lineSpacing/padR 都从这里查. */

	/* 按 rowType 访问 p.text / p.image / p.table / p.math */
	union {
		struct {
			GfxRichText *rt;
			float rtH;           /* 命中预筛, 避免逐移动 DWrite 查询 */
			GfxRect *codeRects;  /* 行内代码底色矩形 (布局局部) */
			int codeN, codeCap;
			UiImage **inlImgs;   /* 行内图片 */
			int *inlStates;
			int inlN;
			GfxMath **inlMath;   /* 行内公式 (blk->p.text.maths[i]) */
			int mathN;
		} text;                  /* PARAGRAPH / HEADING / CODE */
		struct {
			UiImage *img;
			float imgW, imgH;
			int imgReady;
			int imgStateSeen;
		} image;
		struct {
			GfxRichText **cellRt;   /* rows*cols; 布局缓存 */
			int cellN;
			float *colW;
			float *rowH;
			float *cellW;        /* 单元格文本宽缓存 (对齐用) */
			GfxRect **cellCode;  /* 每格内联代码底色矩形 (布局局部) */
			int *cellCodeN;
			int tblCols, tblRows;
			float tblH;
		} table;
		struct {
			GfxMath *bm;
			GfxRichText *bmFallback;   /* 语法失败时的源码文本 */
			float bmW, bmH, bmD;
		} math;
	} p;
} MdRow;

/* TEXT payload 行 (IMAGE 虽也是 text leaf, 但用 image payload) */
static int md_row_is_text(int type)
{
	return type == MD_BLOCK_PARAGRAPH || type == MD_BLOCK_HEADING ||
		type == MD_BLOCK_CODE;
}


/* 容器盒子: 由 flatten 生成, 渲染时按行区间绘制背景/色条/标题 */
/* div 是纯布局盒子: 只用 first/last/align/lineSpacing/padR, 不画色条/标题/底色.
 * 值刻意避开 MD_BOX_* (0..MD_BOX_COUNT-1)。 */
#define MD_BOX_DIV 255

typedef struct MdBox {
	int kind;            /* MD_BOX_* 或 MD_BOX_DIV (div 不画色条/标题) */
	int firstRow, lastRow;
	int indent;
	int depth;           /* 祖先容器数: 标题带下移 depth*boxTitleH */
	int titleOff, titleLen;

	/* div 专用属性 (容器为默认值, 不影响原行为) */
	uint8_t align;       /* MD_ALIGN_*; 255 = 继承 */
	float lineSpacing;   /* >0 = 倍率; <0 = 绝对 DIP (取负); 0 = 继承 */
	float padR;          /* 右内边距 (DIP): wrapW 要扣掉 */
	uint32_t sig;        /* 上面三者的摘要, 抄进行里做复用 key */
} MdBox;

/* 公式缓存条目: 同一 (源码, 字号, 字体) 只保留一棵盒子树与 DWrite 布局,
 * 多行/重复公式共享; refs=0 的条目留在缓存里供滚动回看, 满额时 LRU 驱逐. */
typedef struct MdMathCacheEnt {
	uint32_t hash;
	float size;
	void *font;
	int display;
	char *src;
	int len;
	GfxMath *gm;
	int refs;
	uint32_t lastUse;
} MdMathCacheEnt;

/* 章节锚点: 标题 -> 规范化 slug -> 行下标. 供 [x](#slug) 与目录跳转使用. */
typedef struct MdAnchor {
	int slugOff;     /* 在 md->anchorChars 内的偏移 (整段连续存放) */
	int row;
	UiMdHeading h;   /* 对外的标题目录项; h.slug 在字符池定稿后统一回填 */
} MdAnchor;

struct UiMarkdown {
	UiId id;
	Gfx *gfx;
	UiNet *net;

	char *text;
	int textLen, textCap;

	MdDoc doc;

	UiScrollView sv;
	UiScrollLayout layout;
	UiScrollItem *items;
	int itemCap;
	MdRow *rows;
	int rowCount, rowCap;
	MdBox *boxes;
	int boxCount, boxCap;
	MdAnchor *anchors;
	int anchorCount, anchorCap;
	char *anchorChars;       /* slug 字符池: 一条标题一个 0 结尾片段, 追加存放 */
	int anchorCharsLen, anchorCharsCap;
	int *anchorSlots;        /* 开放寻址: 值 = anchors 下标 + 1, 0 = 空 */
	int anchorSlotCap;

	UiMarkdownStyle st;
	int followTheme;
	GfxFont *fH[6];
	GfxFont *fBody, *fCode, *fSmall, *fSmallBold;
	GfxFont *ownFonts[MD_OWN_FONT_MAX];
	int ownCount;
	int fontsReady;

	/* 解析后的样式数值/颜色 */
	float blockGap, lineSpacing, headGapAbove, headGapBelow;
	float listIndent, quoteIndent, quoteBarW;
	float codePadX, codePadY, codeRadius;
	float tableCellPadX, tableCellPadY, tableMinColW;
	float ruleH, ruleGap, imageMaxH, imagePlaceholderH;
	int imageAlign;
	float boxTitleH, boxIndent, boxRadius, boxBarW;
	UiColor cText, cText2, cLink, cLinkHover;
	UiColor cCodeText, cCodeBg, cCodeBorder;
	UiColor cInlineCodeText, cInlineCodeBg;
	UiColor cBoxBar[MD_BOX_COUNT], cBoxBg[MD_BOX_COUNT];
	UiColor cQuoteBar, cQuoteBg, cRule, cMarker;
	UiColor cTableBorder, cTableHeader;
	UiColor cTokKeyword, cTokString, cTokComment, cTokNumber;
	UiColor cTokType, cTokFunction, cTokPreproc, cTokProperty;

	float contentW;
	int needEstimate;
	int pendingTop, followTail;
	int hasLinks;       /* 文档含可悬停链接: 指针移动需逐次重绘 */

	/* 粘性定位: >=0 时每帧把该行顶到视口 (懒测量收敛前不松手) */
	int stickRow;
	float stickPad;
	double stickUntil;
	int stickSettle;
	int stickAnim;      /* 1 = 平滑跟到目标, 0 = 直接落位 */
	int firstVisible;   /* 上一帧视口首行, -1 = 未知/空文档 */
	/* 当前章节: 显式跳转 (>0) 优先, 用户自己滚动后回到视口推导 */
	int activeAnchor;   /* 锁定到某个标题 (跳转目标), -1 = 跟随视口 */
	int activeHeading;  /* 上一帧激活的标题下标, -1 = 无 (目录高亮) */

	MdMathCacheEnt *mmath;
	int mmathN, mmathCap;
	uint32_t mmathTick;
	int mmathMisses;

	UiContext *ui;
	double lastNow;
	uint32_t frame;

	int *live;
	int liveCount, liveCap;
	uint32_t liveEpoch;   /* 去重代次: 重置 live[] 时 +1, 免 O(n) 清标记 */

	int measureCalls;
	int reuseRows;      /* 上次 SetText/Append 复用的行数 */
	int reuseImages;    /* 其中复用的图片行数 */
	int imgRetained;    /* 宽度失效时保留下来的图片对象数 */
	int imgBusy;
	const char *clickedUrl;
	int hoverRow, hoverLink;
	int hovRow, hovLink;
	int downRow, downLink;
	int pressActive;
	uint32_t themeRev;
	int styleDirty;     /* md->st 变了, 下一帧重算派生样式 */

	UiMarkdownLinkFn linkFn;
	void *linkUser;
	UiMarkdownCopyFn copyFn;
	void *copyUser;
	UiMarkdownTokenizeFn tokFn;
	void *tokUser;
	wchar_t *basePath;
	char *docUrl;       /* 当前文档地址 (UTF-8): 相对链接/图片的基准 */
};

/* ── 样式 ──────────────────────────────────────────────────────────── */

static UiMarkdownStyle md_style_base(void)
{
	UiMarkdownStyle s;
	ZeroMemory(&s, sizeof(s));
	s.blockGap = 10.0f;
	s.lineSpacing = 1.25f;
	s.headingGapAbove = 18.0f;
	s.headingGapBelow = 8.0f;
	s.listIndent = 20.0f;
	s.quoteIndent = 16.0f;
	s.quoteBarW = 3.0f;
	s.codePadX = 12.0f;
	s.codePadY = 8.0f;
	s.codeRadius = 6.0f;
	s.tableCellPadX = 8.0f;
	s.tableCellPadY = 5.0f;
	s.tableMinColW = 48.0f;
	s.imageMaxH = 320.0f;
	s.imagePlaceholderH = 120.0f;
	s.imageAlign = 1;       /* 默认居中 */
	s.ruleH = 1.0f;
	s.ruleGap = 12.0f;
	return s;
}

UiMarkdownStyle UiMarkdownStyle_Light(void)
{
	UiMarkdownStyle s = md_style_base();
	s.text = 0xFF1F2329;
	s.text2 = 0xFF5A6068;
	s.link = 0xFF1E6FD9;
	s.linkHover = 0xFF0B57B8;
	s.codeText = 0xFF24292F;
	s.codeBg = 0xFFF2F4F7;
	s.codeBorder = 0xFFE1E5EA;
	s.inlineCodeText = 0xFFB45309;
	s.inlineCodeBg = 0xFFFFF4E5;
	s.quoteBar = 0xFFC9D1D9;
	s.quoteBg = 0x0A1E6FD9;
	s.tableBorder = 0xFFDDE1E6;
	s.tableHeaderBg = 0xFFF6F7F9;
	s.ruleColor = 0xFFE1E5EA;
	s.markerColor = 0xFF5A6068;
	s.tokKeyword = 0xFFCF222E;
	s.tokString = 0xFF0A3069;
	s.tokComment = 0xFF6E7781;
	s.tokNumber = 0xFF0550AE;
	s.tokType = 0xFF953800;
	s.tokFunction = 0xFF8250DF;
	s.tokPreproc = 0xFF9A6700;
	s.tokProperty = 0xFF116329;
	return s;
}

UiMarkdownStyle UiMarkdownStyle_Dark(void)
{
	UiMarkdownStyle s = md_style_base();
	s.text = 0xFFE6E8EB;
	s.text2 = 0xFFA6ADB5;
	s.link = 0xFF6CB0FF;
	s.linkHover = 0xFF9CCBFF;
	s.codeText = 0xFFE6E8EB;
	s.codeBg = 0xFF20242A;
	s.codeBorder = 0xFF343A42;
	s.inlineCodeText = 0xFFF0B45A;
	s.inlineCodeBg = 0xFF3A2A16;
	s.quoteBar = 0xFF4A515B;
	s.quoteBg = 0x146CB0FF;
	s.tableBorder = 0xFF343A42;
	s.tableHeaderBg = 0xFF252A31;
	s.ruleColor = 0xFF343A42;
	s.markerColor = 0xFFA6ADB5;
	s.tokKeyword = 0xFFFF7B72;
	s.tokString = 0xFFA5D6FF;
	s.tokComment = 0xFF8B949E;
	s.tokNumber = 0xFF79C0FF;
	s.tokType = 0xFFFFA657;
	s.tokFunction = 0xFFD2A8FF;
	s.tokPreproc = 0xFFE3B341;
	s.tokProperty = 0xFF7EE787;
	return s;
}

UiMarkdownStyle UiMarkdownStyle_FromTheme(const UiTheme *t)
{
	UiMarkdownStyle s = md_style_base();
	if (!t) return UiMarkdownStyle_Light();
	s.text = t->text;
	s.text2 = t->text2;
	s.link = t->blue;
	s.linkHover = t->blue2;
	s.codeText = t->text;
	s.codeBg = t->frame;
	s.codeBorder = t->border;
	s.inlineCodeText = t->themeId == UI_THEME_DARK
		? 0xFFF0B45A : 0xFFB45309;
	s.inlineCodeBg = t->themeId == UI_THEME_DARK
		? 0xFF3A2A16 : 0xFFFFF4E5;
	s.quoteBar = t->border;
	s.quoteBg = t->rowOff;
	s.tableBorder = t->border;
	s.tableHeaderBg = t->card;
	s.ruleColor = t->border;
	s.markerColor = t->text2;
	s.tokKeyword = t->red;
	s.tokString = t->blue;
	s.tokComment = t->text3;
	s.tokNumber = t->blue2;
	s.tokType = t->orange;
	s.tokFunction = t->green;
	s.tokPreproc = t->gold;
	s.tokProperty = t->text2;
	return s;
}

/* ── 字体与样式解析 ───────────────────────────────────────────────── */

static GfxFont *md_own_font(UiMarkdown *md, float size, int weight)
{
	GfxFont *f;
	if (!md->gfx) return NULL;
	f = Gfx_CreateFont(md->gfx, size, weight);
	if (f && md->ownCount < MD_OWN_FONT_MAX)
		md->ownFonts[md->ownCount++] = f;
	return f;
}

static GfxFont *md_own_mono(UiMarkdown *md, float size)
{
	GfxFont *f = NULL;
	if (!md->gfx) return NULL;
	f = Gfx_CreateFontFamily(md->gfx, L"Consolas", size, 400);
	if (!f) f = Gfx_CreateFont(md->gfx, size, 400);
	if (f && md->ownCount < MD_OWN_FONT_MAX)
		md->ownFonts[md->ownCount++] = f;
	return f;
}

static void md_free_fonts(UiMarkdown *md)
{
	int i;
	for (i = 0; i < md->ownCount; i++) {
		if (md->ownFonts[i]) Gfx_DestroyFont(md->gfx, md->ownFonts[i]);
		md->ownFonts[i] = NULL;
	}
	md->ownCount = 0;
	md->fontsReady = 0;
	ZeroMemory(md->fH, sizeof(md->fH));
	md->fBody = md->fCode = md->fSmall = NULL;
	md->fSmallBold = NULL;
}

static void md_build_fonts(UiMarkdown *md)
{
	float hsz[6] = { 26.0f, 22.0f, 19.0f, 16.0f, 15.0f, 14.0f };

	md_free_fonts(md);
	if (!md->gfx) return;
	md->fH[0] = md->st.h1 ? md->st.h1 : md_own_font(md, hsz[0], 700);
	md->fH[1] = md->st.h2 ? md->st.h2 : md_own_font(md, hsz[1], 700);
	md->fH[2] = md->st.h3 ? md->st.h3 : md_own_font(md, hsz[2], 700);
	md->fH[3] = md->st.h4 ? md->st.h4 : md_own_font(md, hsz[3], 700);
	md->fH[4] = md->st.h5 ? md->st.h5 : md_own_font(md, hsz[4], 700);
	md->fH[5] = md->st.h6 ? md->st.h6 : md_own_font(md, hsz[5], 700);
	md->fBody = md->st.body ? md->st.body : md_own_font(md, 14.0f, 400);
	md->fCode = md->st.code ? md->st.code : md_own_mono(md, 13.0f);
	md->fSmall = md->st.tiny ? md->st.tiny : md_own_font(md, 12.0f, 400);
	md->fSmallBold = md_own_font(md, 12.0f, 700);
	if (!md->fSmallBold) md->fSmallBold = md->fSmall;
	md->fontsReady = md->fBody && md->fH[0] && md->fCode && md->fSmall;
}

#define MD_F(orDefault, def) ((orDefault) > 0.0f ? (orDefault) : (def))

static void md_resolve_style(UiMarkdown *md, const UiTheme *t)
{
	if (md->followTheme) md->st = UiMarkdownStyle_FromTheme(t);
	md->blockGap = MD_F(md->st.blockGap, 10.0f);
	md->lineSpacing = MD_F(md->st.lineSpacing, 1.25f);
	md->headGapAbove = MD_F(md->st.headingGapAbove, 18.0f);
	md->headGapBelow = MD_F(md->st.headingGapBelow, 8.0f);
	md->listIndent = MD_F(md->st.listIndent, 20.0f);
	md->quoteIndent = MD_F(md->st.quoteIndent, 16.0f);
	md->quoteBarW = MD_F(md->st.quoteBarW, 3.0f);
	md->codePadX = MD_F(md->st.codePadX, 12.0f);
	md->codePadY = MD_F(md->st.codePadY, 8.0f);
	md->codeRadius = MD_F(md->st.codeRadius, 6.0f);
	md->tableCellPadX = MD_F(md->st.tableCellPadX, 8.0f);
	md->tableCellPadY = MD_F(md->st.tableCellPadY, 5.0f);
	md->tableMinColW = MD_F(md->st.tableMinColW, 48.0f);
	md->ruleH = MD_F(md->st.ruleH, 1.0f);
	md->ruleGap = MD_F(md->st.ruleGap, 12.0f);
	md->imageMaxH = MD_F(md->st.imageMaxH, 320.0f);
	md->imagePlaceholderH = MD_F(md->st.imagePlaceholderH, 120.0f);
	md->imageAlign = (md->st.imageAlign >= 0 && md->st.imageAlign <= 2)
		? md->st.imageAlign : 1;

	md->cText = md->st.text ? md->st.text : (t ? t->text : 0xFF1F2329);
	md->cText2 = md->st.text2 ? md->st.text2 : (t ? t->text2 : 0xFF5A6068);
	md->cLink = md->st.link ? md->st.link : (t ? t->blue : 0xFF1E6FD9);
	md->cLinkHover = md->st.linkHover ? md->st.linkHover : md->cLink;
	md->cCodeText = md->st.codeText ? md->st.codeText : md->cText;
	md->cCodeBg = md->st.codeBg ? md->st.codeBg : (t ? t->frame : 0xFFF2F4F7);
	md->cCodeBorder = md->st.codeBorder ? md->st.codeBorder : (t ? t->border : 0xFFE1E5EA);
	md->cInlineCodeText = md->st.inlineCodeText ? md->st.inlineCodeText
		: (t ? t->warning.text : 0xFFB45309);
	md->cInlineCodeBg = md->st.inlineCodeBg ? md->st.inlineCodeBg
		: ((t && t->themeId == UI_THEME_DARK) ? 0xFF3A2A16 : 0xFFFFF4E5);
	md->cQuoteBar = md->st.quoteBar ? md->st.quoteBar : (t ? t->border : 0xFFC9D1D9);
	md->cQuoteBg = md->st.quoteBg;
	md->cRule = md->st.ruleColor ? md->st.ruleColor : (t ? t->border : 0xFFE1E5EA);
	md->cMarker = md->st.markerColor ? md->st.markerColor : md->cText2;
	md->cTableBorder = md->st.tableBorder ? md->st.tableBorder
		: (t ? t->border : 0xFFDDE1E6);
	md->cTableHeader = md->st.tableHeaderBg ? md->st.tableHeaderBg
		: (t ? t->card : 0xFFF6F7F9);
	md->cTokKeyword = md->st.tokKeyword ? md->st.tokKeyword
		: (t ? t->red : 0xFFCF222E);
	md->cTokString = md->st.tokString ? md->st.tokString
		: (t ? t->blue : 0xFF0A3069);
	md->cTokComment = md->st.tokComment ? md->st.tokComment
		: (t ? t->text3 : 0xFF6E7781);
	md->cTokNumber = md->st.tokNumber ? md->st.tokNumber
		: (t ? t->blue2 : 0xFF0550AE);
	md->cTokType = md->st.tokType ? md->st.tokType
		: (t ? t->orange : 0xFF953800);
	md->cTokFunction = md->st.tokFunction ? md->st.tokFunction
		: (t ? t->green : 0xFF8250DF);
	md->cTokPreproc = md->st.tokPreproc ? md->st.tokPreproc
		: (t ? t->gold : 0xFF9A6700);
	md->cTokProperty = md->st.tokProperty ? md->st.tokProperty
		: (t ? t->text2 : 0xFF116329);

	/* 容器: 标题/色条用与浅色底有足够对比的固定语义色.
	 * 不能取 role.text: primary.text 是主按钮上的白字, 浅色标题会看不见. */
	{
		int dark = t && t->themeId == UI_THEME_DARK;
		md->cBoxBar[MD_BOX_NOTE] = dark ? 0xFF8FB8FF : 0xFF1557B0;
		md->cBoxBar[MD_BOX_TIP] = dark ? 0xFF63D68F : 0xFF1E7B47;
		md->cBoxBar[MD_BOX_WARNING] = dark ? 0xFFF0B45A : 0xFFB45309;
		md->cBoxBar[MD_BOX_DANGER] = dark ? 0xFFFF8A94 : 0xFFC0392B;
		md->cBoxBg[MD_BOX_NOTE] = dark ? 0xFF17304A : 0xFFEAF2FD;
		md->cBoxBg[MD_BOX_TIP] = dark ? 0xFF16331F : 0xFFE9F7EF;
		md->cBoxBg[MD_BOX_WARNING] = dark ? 0xFF3A2A16 : 0xFFFFF4E5;
		md->cBoxBg[MD_BOX_DANGER] = dark ? 0xFF3A1D1D : 0xFFFDECEC;
		md->boxTitleH = 32.0f;   /* 8 顶距 + 16 标题 + 8 到正文 */
		md->boxIndent = md->quoteIndent;
		md->boxRadius = 6.0f;
		md->boxBarW = md->quoteBarW;
	}
}

/* ── 行构建 ────────────────────────────────────────────────────────── */

/* ── 公式缓存 ──────────────────────────────────────────────────────── */

#define MD_MATH_CACHE_MAX 128

static uint32_t md_math_hash(const char *s, int n)
{
	uint32_t h = 2166136261u;
	int i;
	for (i = 0; i < n; i++) {
		h ^= (uint32_t)(unsigned char)s[i];
		h *= 16777619u;
	}
	return h;
}

static int md_math_src_eq(const MdMathCacheEnt *e, const char *s, int n)
{
	int i;
	if (!e->src || e->len != n) return 0;
	for (i = 0; i < n; i++)
		if (e->src[i] != s[i]) return 0;
	return e->src[n] == 0;
}

/* 取一个公式对象 (refs++), 未命中则创建并入缓存. 返回 NULL = 语法错误.
 * 创建后 cache_get 增加一次引用, 调用方必须用 md_math_cache_put 归还. */
static GfxMath *md_math_cache_get(UiMarkdown *md, const char *src, int len,
	float size, GfxFont *font, int display)
{
	uint32_t h;
	GfxMath *gm;
	int i, victim = -1;
	if (!md || !src || len <= 0 || !md->gfx) return NULL;
	h = md_math_hash(src, len);
	for (i = 0; i < md->mmathN; i++) {
		MdMathCacheEnt *e = &md->mmath[i];
		if (e->hash == h && e->size == size && e->font == font &&
			e->display == (display ? 1 : 0) &&
			md_math_src_eq(e, src, len)) {
			e->refs++;
			e->lastUse = ++md->mmathTick;
			return e->gm;
		}
	}
	gm = Gfx_MathCreate(md->gfx, src, len, size, font, display);
	if (!gm) return NULL;
	md->mmathMisses++;

	if (md->mmathN >= MD_MATH_CACHE_MAX) {
		uint32_t best = 0;
		for (i = 0; i < md->mmathN; i++) {
			if (md->mmath[i].refs != 0) continue;
			if (victim < 0 || md->mmath[i].lastUse < best) {
				victim = i;
				best = md->mmath[i].lastUse;
			}
		}
		if (victim < 0) return gm;   /* 全部在用: 不缓存, put 时直接销毁 */
		Gfx_MathDestroy(md->mmath[victim].gm);
		md_free_(md->mmath[victim].src);
		md->mmath[victim] = md->mmath[--md->mmathN];
	}
	if (md->mmathN >= md->mmathCap) {
		int nc = md->mmathCap ? md->mmathCap * 2 : 16;
		MdMathCacheEnt *np = (MdMathCacheEnt *)md_realloc_(md->mmath,
			(SIZE_T)nc * sizeof(MdMathCacheEnt));
		if (!np) return gm;
		md->mmath = np;
		md->mmathCap = nc;
	}
	{
		MdMathCacheEnt *e = &md->mmath[md->mmathN];
		e->src = (char *)md_alloc_(len + 1);
		if (!e->src) return gm;
		for (i = 0; i < len; i++) e->src[i] = src[i];
		e->src[len] = 0;
		e->hash = h;
		e->len = len;
		e->size = size;
		e->font = font;
		e->display = display ? 1 : 0;
		e->gm = gm;
		e->refs = 1;
		e->lastUse = ++md->mmathTick;
		md->mmathN++;
	}
	return gm;
}

static void md_math_cache_put(UiMarkdown *md, GfxMath *gm)
{
	int i;
	if (!gm) return;
	if (md) {
		for (i = 0; i < md->mmathN; i++) {
			if (md->mmath[i].gm == gm) {
				if (md->mmath[i].refs > 0) md->mmath[i].refs--;
				md->mmath[i].lastUse = ++md->mmathTick;
				return;
			}
		}
	}
	Gfx_MathDestroy(gm);
}

/* 只能在所有行已释放后调用 (否则行上的指针悬空) */
static void md_math_cache_clear(UiMarkdown *md)
{
	int i;
	if (!md) return;
	for (i = 0; i < md->mmathN; i++) {
		Gfx_MathDestroy(md->mmath[i].gm);
		md_free_(md->mmath[i].src);
	}
	md_free_(md->mmath);
	md->mmath = NULL;
	md->mmathN = 0;
	md->mmathCap = 0;
}

/* 按 blk->type 释放对应 payload; union 成员不可跨类型访问.
 * keepImages: 宽度变化时保留行内图片对象 (尺寸在 measure 阶段重算). */
static void md_row_payload_release(UiMarkdown *md, MdRow *r, int keepImages)
{
	int i, type;
	if (!r) return;
	type = (int)r->rowType;
	switch (type) {
	case MD_BLOCK_PARAGRAPH:
	case MD_BLOCK_HEADING:
	case MD_BLOCK_CODE:
		if (r->p.text.rt) {
			Gfx_RichDestroy(r->p.text.rt);
			r->p.text.rt = NULL;
		}
		r->p.text.rtH = 0.0f;
		md_free_(r->p.text.codeRects);
		r->p.text.codeRects = NULL;
		r->p.text.codeN = r->p.text.codeCap = 0;
		if (!keepImages) {
			if (r->p.text.inlImgs) {
				for (i = 0; i < r->p.text.inlN; i++)
					if (r->p.text.inlImgs[i])
						UiImage_Destroy(r->p.text.inlImgs[i]);
				md_free_(r->p.text.inlImgs);
				r->p.text.inlImgs = NULL;
			}
			md_free_(r->p.text.inlStates);
			r->p.text.inlStates = NULL;
			r->p.text.inlN = 0;
		}
		if (r->p.text.inlMath) {
			for (i = 0; i < r->p.text.mathN; i++)
				if (r->p.text.inlMath[i]) md_math_cache_put(md, r->p.text.inlMath[i]);
			md_free_(r->p.text.inlMath);
			r->p.text.inlMath = NULL;
		}
		r->p.text.mathN = 0;
		break;
	case MD_BLOCK_IMAGE:
		if (keepImages) {
			/* 保留 img/src/解码位图, 尺寸在 measure 时重算 */
			r->p.image.imgReady = 0;
			break;
		}
		if (r->p.image.img) {
			UiImage_Destroy(r->p.image.img);
			r->p.image.img = NULL;
		}
		r->p.image.imgW = r->p.image.imgH = 0.0f;
		r->p.image.imgReady = 0;
		r->p.image.imgStateSeen = 0;
		break;
	case MD_BLOCK_TABLE:
		if (r->p.table.cellRt) {
			for (i = 0; i < r->p.table.cellN; i++)
				if (r->p.table.cellRt[i]) Gfx_RichDestroy(r->p.table.cellRt[i]);
			md_free_(r->p.table.cellRt);
			r->p.table.cellRt = NULL;
		}
		md_free_(r->p.table.colW);
		r->p.table.colW = NULL;
		md_free_(r->p.table.rowH);
		r->p.table.rowH = NULL;
		md_free_(r->p.table.cellW);
		r->p.table.cellW = NULL;
		if (r->p.table.cellCode) {
			for (i = 0; i < r->p.table.cellN; i++)
				md_free_(r->p.table.cellCode[i]);
			md_free_(r->p.table.cellCode);
			r->p.table.cellCode = NULL;
		}
		md_free_(r->p.table.cellCodeN);
		r->p.table.cellCodeN = NULL;
		r->p.table.cellN = 0;
		r->p.table.tblCols = r->p.table.tblRows = 0;
		r->p.table.tblH = 0.0f;
		break;
	case MD_BLOCK_MATH:
		if (r->p.math.bm) {
			md_math_cache_put(md, r->p.math.bm);
			r->p.math.bm = NULL;
		}
		if (r->p.math.bmFallback) {
			Gfx_RichDestroy(r->p.math.bmFallback);
			r->p.math.bmFallback = NULL;
		}
		r->p.math.bmW = r->p.math.bmH = r->p.math.bmD = 0.0f;
		break;
	default:
		break;
	}
}

static void md_row_release(UiMarkdown *md, MdRow *r)
{
	if (!r) return;
	if (md->hovRow >= 0 && md->hovRow < md->rowCount &&
		&md->rows[md->hovRow] == r) {
		md->hovRow = -1;
		md->hovLink = -1;
	}
	md_row_payload_release(md, r, 0);
}

static void md_live_reset(UiMarkdown *md)
{
	md->liveCount = 0;
	if (++md->liveEpoch == 0) md->liveEpoch = 1;
}

static int md_live_push(UiMarkdown *md, int idx)
{
	if (idx < 0 || idx >= md->rowCount) return 0;
	if (!md->liveEpoch) md->liveEpoch = 1;
	if (md->rows[idx].liveStamp == md->liveEpoch) return 1;
	if (md->liveCount >= md->liveCap) {
		int nc = md->liveCap ? md->liveCap * 2 : 32;
		int *np = (int *)md_realloc_(md->live,
			(SIZE_T)nc * sizeof(int));
		if (!np) return 0;
		md->live = np;
		md->liveCap = nc;
	}
	md->live[md->liveCount++] = idx;
	md->rows[idx].liveStamp = md->liveEpoch;
	return 1;
}

/* 盒子的布局签名: 只收会影响行高/断行的属性。行复用 key 必须带上它 ——
 * 同一段文本在 <div style="line-height:2"> 里和在裸段落里高度不同,
 * 只按 (hash, rowType) 匹配会把旧高度搬过去。 */
static uint32_t md_box_sig(const MdBox *b)
{
	uint32_t s = 2166136261u;
	s = (s ^ (uint32_t)b->align) * 16777619u;
	s = (s ^ (uint32_t)(int32_t)(b->lineSpacing * 100.0f)) * 16777619u;
	s = (s ^ (uint32_t)(int32_t)(b->padR * 4.0f)) * 16777619u;
	return s;
}

static int md_box_push(UiMarkdown *md, int kind, int first, int last,
	int indent, int depth, int titleOff, int titleLen)
{
	MdBox *b;
	if (md->boxCount >= md->boxCap) {
		int nc = md->boxCap ? md->boxCap * 2 : 16;
		MdBox *np = (MdBox *)md_realloc_(md->boxes,
			(SIZE_T)nc * sizeof(MdBox));
		if (!np) return -1;
		md->boxes = np;
		md->boxCap = nc;
	}
	b = &md->boxes[md->boxCount];
	b->kind = kind;
	b->firstRow = first;
	b->lastRow = last;
	b->indent = indent;
	b->depth = depth;
	b->titleOff = titleOff;
	b->titleLen = titleLen;
	b->align = (uint8_t)MD_DIV_ALIGN_INHERIT;   /* 未指定 = 继承全局左对齐 */
	b->lineSpacing = 0.0f;
	b->padR = 0.0f;
	return md->boxCount++;      /* 返回新盒子下标; -1 = 失败 */
}

/* 行可用宽度: 测量与绘制共用一处, 避免两边算法漂移 (div 的 padR 要扣).
 * 注意这里统一用 md->contentW, 不要用绘制时的 content.w —— 两者一旦不等,
 * 就会出现"测出来 3 行、画出来 4 行". */
static float md_row_wrap_w(const UiMarkdown *md, const MdRow *r)
{
	float w = md->contentW - (float)r->indent - r->markerW;
	if (r->boxIdx >= 0 && r->boxIdx < md->boxCount)
		w -= md->boxes[r->boxIdx].padR;
	return w < 1.0f ? 1.0f : w;
}

static MdRow *md_row_push(UiMarkdown *md, MdBlock *blk, int indent, int qd,
	int mk, int no, int task)
{
	MdRow *r;
	if (md->rowCount >= md->rowCap) {
		int nc = md->rowCap ? md->rowCap * 2 : 128;
		MdRow *np = (MdRow *)md_realloc_(md->rows,
			(SIZE_T)nc * sizeof(MdRow));
		if (!np) return NULL;
		md->rows = np;
		md->rowCap = nc;
	}
	r = &md->rows[md->rowCount++];
	ZeroMemory(r, sizeof(*r));
	r->blk = blk;
	r->indent = indent;
	r->quoteDepth = qd;
	r->markerKind = mk;
	r->markerNo = no;
	r->task = task;
	r->markerW = mk != MD_MK_NONE ? MD_MARKER_W : 0.0f;
	r->hash = blk->hash;
	r->rowType = blk->type;
	r->boxIdx = -1;
	return r;
}

static int md_flatten(UiMarkdown *md, MdBlock *b, int indent, int qd,
	int mk, int no, int task)
{
	int i, added = 0;
	if (!b) return 0;
	switch (b->type) {
	case MD_BLOCK_QUOTE:
		for (i = 0; i < b->p.kids.childCount; i++) {
			int r = md_flatten(md, &b->p.kids.children[i],
				indent + (int)md->quoteIndent, qd + 1,
				added ? MD_MK_NONE : mk,
				added ? 0 : no, added ? MD_TASK_NONE : task);
			if (r) added = 1;
		}
		return added;
	case MD_BLOCK_CONTAINER: {
		int first = md->rowCount;
		int bi = md->boxIndent > 0.0f ? (int)md->boxIndent
			: (int)md->quoteIndent;
		float th = md->boxTitleH > 0.0f ? md->boxTitleH : 32.0f;
		int boxAt;
		if (bi <= 0) bi = 16;
		/* 先占位再展开: 外盒必须先于内盒绘制, 否则内层底色被外层覆盖 */
		boxAt = md->boxCount;
		md_box_push(md, b->u.box.boxKind, first, first, indent,
			bi > 0 ? indent / bi : 0, b->u.box.titleOff, b->u.box.titleLen);
		for (i = 0; i < b->p.kids.childCount; i++) {
			int r = md_flatten(md, &b->p.kids.children[i], indent + bi, qd,
				added ? MD_MK_NONE : mk,
				added ? 0 : no, added ? MD_TASK_NONE : task);
			if (r) added = 1;
		}
		if (md->rowCount > first) {
			md->rows[first].extraTop += th;
			/* 卡内底部内边距: 与代码块 codePadY 对齐 */
			md->rows[md->rowCount - 1].extraBottom += 8.0f;
			if (boxAt >= 0 && boxAt < md->boxCount)
				md->boxes[boxAt].lastRow = md->rowCount - 1;
		} else if (boxAt >= 0 && boxAt < md->boxCount) {
			md->boxCount--;
		}
		return added;
	}
	case MD_BLOCK_DIV: {
		int first = md->rowCount, k;
		MdBlock *dv = b;
		float padL = MD_UNQ4(dv->u.div.padL);
		float padT = MD_UNQ4(dv->u.div.padT);
		float padR = MD_UNQ4(dv->u.div.padR);
		float padB = MD_UNQ4(dv->u.div.padB);
		float mT = MD_UNQ4(dv->u.div.marginT);
		float mB = MD_UNQ4(dv->u.div.marginB);
		int boxAt = md_box_push(md, MD_BOX_DIV, first, first,
			indent, 0, 0, 0);
		for (i = 0; i < dv->p.kids.childCount; i++) {
			int r = md_flatten(md, &dv->p.kids.children[i],
				indent + (int)(padL + 0.5f), qd,
				added ? MD_MK_NONE : mk,
				added ? 0 : no, added ? MD_TASK_NONE : task);
			if (r) added = 1;
		}
		if (md->rowCount > first && boxAt >= 0 && boxAt < md->boxCount) {
			MdBox *bx = &md->boxes[boxAt];
			int q = dv->u.div.lineSpacing;
			bx->align = dv->u.div.align;
			if (q > 0) bx->lineSpacing = (float)q * 0.01f;      /* 倍率 */
			else if (q < 0) bx->lineSpacing = (float)q * 0.25f; /* 负数 = -绝对 DIP */
			bx->padR = padR;
			bx->lastRow = md->rowCount - 1;
			md->rows[first].extraTop += padT + mT;
			md->rows[md->rowCount - 1].extraBottom += padB + mB;
			/* 内层后写, 自然覆盖外层: 行归属最内层 div */
			bx->sig = md_box_sig(bx);
			for (k = first; k < md->rowCount; k++) {
				md->rows[k].boxIdx = (int16_t)boxAt;
				md->rows[k].boxSig = bx->sig;
			}
		} else if (boxAt >= 0 && boxAt < md->boxCount) {
			md->boxCount--;      /* 空 div: 不占盒子 */
		}
		return added;
	}
	case MD_BLOCK_LIST:
		for (i = 0; i < b->p.kids.childCount; i++) {
			MdBlock *item = &b->p.kids.children[i];
			int mk2 = item->task ? MD_MK_TASK
				: (b->ordered ? MD_MK_ORDERED : MD_MK_BULLET);
			int no2 = b->ordered ? b->startNo + i : 0;
			int r = md_flatten(md, item, indent + (int)md->listIndent, qd,
				mk2, no2, item->task);
			if (r) added = 1;
		}
		return added;
	case MD_BLOCK_ITEM:
		for (i = 0; i < b->p.kids.childCount; i++) {
			int r = md_flatten(md, &b->p.kids.children[i], indent, qd,
				added ? MD_MK_NONE : mk,
				added ? 0 : no, added ? MD_TASK_NONE : task);
			if (r) added = 1;
		}
		return added;
	default:
		if (MD_BT_TEXT(b->type) && b->p.text.linkCount > 0) md->hasLinks = 1;
		return md_row_push(md, b, indent, qd, mk, no, task) ? 1 : 0;
	}
}

/* ── 尺寸估算 ──────────────────────────────────────────────────────── */

static float md_row_font_size(const UiMarkdown *md, const MdRow *r)
{
	if (r->blk->type == MD_BLOCK_HEADING) {
		static const float hs[6] = { 26.0f, 22.0f, 19.0f, 16.0f, 15.0f, 14.0f };
		int lv = r->blk->level;
		if (lv < 1) lv = 1;
		if (lv > 6) lv = 6;
		return hs[lv - 1];
	}
	if (r->blk->type == MD_BLOCK_CODE) return 13.0f;
	return 14.0f;
}

static GfxFont *md_row_font(const UiMarkdown *md, const MdRow *r)
{
	if (r->blk->type == MD_BLOCK_HEADING) {
		int lv = r->blk->level;
		if (lv < 1) lv = 1;
		if (lv > 6) lv = 6;
		return md->fH[lv - 1];
	}
	if (r->blk->type == MD_BLOCK_CODE) return md->fCode;
	return md->fBody;
}

static void md_row_pads(const UiMarkdown *md, const MdRow *r,
	float *top, float *bottom)
{
	/* 注意: 各类型的间距是"基础值", 容器/div 的 padding/margin 记在
	 * extraTop/extraBottom 里, 必须叠加而不是覆盖 —— 否则 div 内首行是
	 * 标题/代码块时, 卡片顶部内边距会整个丢掉, 内容顶到卡片上缘. */
	*top = 0.0f;
	*bottom = md->blockGap;
	switch (r->blk->type) {
	case MD_BLOCK_HEADING:
		*top = md->headGapAbove;
		*bottom = md->headGapBelow;
		break;
	case MD_BLOCK_CODE:
		*top = md->codePadY;
		*bottom = md->codePadY + md->blockGap;
		break;
	case MD_BLOCK_HR:
		*top = md->ruleGap;
		*bottom = md->ruleGap;
		break;
	case MD_BLOCK_MATH:
		*top = 6.0f;
		*bottom = 6.0f + md->blockGap;
		break;
	default:
		break;
	}
	*top += r->extraTop;
	*bottom += r->extraBottom;
}

static float md_est_text(const wchar_t *t, int n, float wrapW, float size,
	float lineSpacing)
{
	int i, lines = 1;
	float w = 0.0f;
	float lineH;
	if (wrapW < 1.0f) wrapW = 1.0f;
	lineH = size * (lineSpacing > 0.0f ? lineSpacing : 1.25f);
	if (!t || n <= 0) return lineH;
	for (i = 0; i < n; i++) {
		wchar_t c = t[i];
		if (c == L'\n') {
			lines++;
			w = 0.0f;
			continue;
		}
		if (c >= 0x2E80) w += size;
		else w += size * 0.55f;
		if (w > wrapW * 1.5f) {
			lines++;
			w = 0.0f;
		}
	}
	return (float)lines * lineH;
}

static float md_row_estimate(const UiMarkdown *md, const MdRow *r)
{
	float top, bottom, h;
	float wrapW = md_row_wrap_w(md, r);
	float size = md_row_font_size(md, r);
	md_row_pads(md, r, &top, &bottom);
	switch (r->blk->type) {
	case MD_BLOCK_HR:
		return md->ruleH + top + bottom;
	case MD_BLOCK_IMAGE:
		return md->imagePlaceholderH + top + bottom;
	case MD_BLOCK_TABLE:
		h = (float)(r->blk->u.table.rows > 0 ? r->blk->u.table.rows : 1) * 22.0f + 12.0f;
		return h + top + bottom;
	case MD_BLOCK_CODE:
		h = md_est_text(r->blk->p.text.text16, r->blk->p.text.len16, wrapW, size,
			md->lineSpacing);
		return h + top + bottom;
	case MD_BLOCK_MATH:
		return 52.0f + top + bottom;
	default:
		h = md_est_text(r->blk->p.text.text16, r->blk->p.text.len16, wrapW, size,
			md->lineSpacing);
		return h + top + bottom;
	}
}

/* ── 布局/缓存 ─────────────────────────────────────────────────────── */

static int md_rebuild_items(UiMarkdown *md)
{
	int i;
	if (md->rowCount > md->itemCap) {
		int nc = md->itemCap ? md->itemCap : 128;
		UiScrollItem *np;
		while (nc < md->rowCount) nc *= 2;
		np = (UiScrollItem *)md_realloc_(md->items,
			(SIZE_T)nc * sizeof(UiScrollItem));
		if (!np) return 0;
		md->items = np;
		md->itemCap = nc;
	}
	UiScrollLayout_Init(&md->layout, md->items, md->itemCap);
	UiScrollLayout_SetPadding(&md->layout, 0.0f, 0.0f);
	UiScrollLayout_SetGap(&md->layout, 0.0f);
	UiScrollLayout_SetCount(&md->layout, md->rowCount);
	for (i = 0; i < md->rowCount; i++)
		UiScrollLayout_SetItemFlags(&md->layout, i,
			UI_SCROLL_ITEM_WIDTH_DEPENDENT);
	return 1;
}

static void md_estimate_all(UiMarkdown *md)
{
	int i;
	if (md->contentW <= 0.0f) return;
	for (i = 0; i < md->rowCount; i++) {
		MdRow *r = &md->rows[i];
		if (r->measured) continue;
		UiScrollLayout_SetItemHeight(&md->layout, i, r->hash,
			md_row_estimate(md, r));
	}
	md->needEstimate = 0;
}

/* ── 行内样式 / 图片 / 高亮 / 链接 ─────────────────────────────────── */

static void md_image_load(UiMarkdown *md, UiImage *img, const char *url);
static const char *md_image_key(UiMarkdown *md, const char *url, char *buf,
	int cap);

/* codeBlock: 代码块整体 run 使用 codeText; 行内代码用琥珀色 + 等宽 */
static void md_apply_runs(UiMarkdown *md, GfxRichText *rt, const MdRun *runs,
	int runCount, int codeBlock)
{
	int i;
	if (!rt || !runs) return;
	for (i = 0; i < runCount; i++) {
		const MdRun *r = &runs[i];
		if (r->len <= 0) continue;
		if (r->style & MD_R_BOLD) Gfx_RichBold(rt, r->start, r->len, 1);
		if (r->style & MD_R_ITALIC) Gfx_RichItalic(rt, r->start, r->len, 1);
		if (r->style & MD_R_STRIKE) Gfx_RichStrike(rt, r->start, r->len, 1);
		if (r->style & MD_R_LINK) {
			Gfx_RichColor(rt, r->start, r->len, md->cLink);
			Gfx_RichUnderline(rt, r->start, r->len, 1);
		} else if (r->style & MD_R_CODE) {
			if (codeBlock) {
				Gfx_RichColor(rt, r->start, r->len, md->cCodeText);
			} else {
				Gfx_RichColor(rt, r->start, r->len, md->cInlineCodeText);
				/* 行内代码: 换等宽家族, 字号跟随所在行 */
				if (md->fCode)
					Gfx_RichFontFamily(rt, r->start, r->len, md->fCode);
			}
		}
	}
}

static UiColor md_tok_color(UiMarkdown *md, uint8_t style)
{
	switch (style) {
	case MD_TOK_KEYWORD: return md->cTokKeyword;
	case MD_TOK_STRING: return md->cTokString;
	case MD_TOK_COMMENT: return md->cTokComment;
	case MD_TOK_NUMBER: return md->cTokNumber;
	case MD_TOK_TYPE: return md->cTokType;
	case MD_TOK_FUNCTION: return md->cTokFunction;
	case MD_TOK_PREPROC: return md->cTokPreproc;
	case MD_TOK_PROPERTY: return md->cTokProperty;
	default: return 0;
	}
}

static void md_highlight(UiMarkdown *md, GfxRichText *rt, const MdBlock *b)
{
	const char *lang;
	int len16 = b->p.text.len16;
	int cap = 512, n = 0, i;
	UiMdSpan *sp;

	if (!rt || !b || len16 <= 0) return;
	lang = MdDoc_Url(&md->doc, b->u.code.langOff);
	if (cap > len16 + 1) cap = len16 + 1;
	for (;;) {
		sp = (UiMdSpan *)md_alloc_(cap * (int)sizeof(UiMdSpan));
		if (!sp) return;
		if (md->tokFn)
			n = md->tokFn(md->tokUser, lang, b->p.text.text16, len16,
				sp, cap);
		else
			n = UiMdHighlight_Tokenize(lang, b->p.text.text16, len16,
				sp, cap);
		if (n <= cap) break;
		md_free_(sp);
		sp = NULL;
		if (n > len16 + 1) n = len16 + 1;
		if (n <= cap) break;
		cap = n;
	}
	if (sp) {
		for (i = 0; i < n; i++) {
			UiColor c = md_tok_color(md, sp[i].style);
			if (c) Gfx_RichColor(rt, sp[i].start, sp[i].len, c);
		}
		md_free_(sp);
	}
}

static void md_inl_ensure(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	int i;
	if (!b || b->p.text.imgCount <= 0 || !md->gfx) return;
	if (r->p.text.inlImgs) {
		md_live_push(md, index);   /* 宽度重建: 已保留的图片继续受 evict 管理 */
		return;
	}
	r->p.text.inlImgs = (UiImage **)md_alloc_(b->p.text.imgCount * (int)sizeof(UiImage *));
	r->p.text.inlStates = (int *)md_alloc_(b->p.text.imgCount * (int)sizeof(int));
	if (!r->p.text.inlImgs || !r->p.text.inlStates) return;
	r->p.text.inlN = b->p.text.imgCount;
	for (i = 0; i < b->p.text.imgCount; i++) {
		UiImage *im = UiImage_Create(md->gfx, md->net);
		if (!im) continue;
		if (md->ui) UiImage_SetContext(im, md->ui);
		UiImage_SetFit(im, UI_IMG_FIT_CONTAIN);
		md_image_load(md, im,
			MdDoc_Url(&md->doc, b->p.text.imgs[i].urlOff));
		r->p.text.inlImgs[i] = im;
		r->p.text.inlStates[i] = UiImage_State(im);
	}
	md_live_push(md, index);
}

static int md_inl_changed(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	int i;
	for (i = 0; i < r->p.text.inlN; i++) {
		if (!r->p.text.inlImgs[i]) continue;
		if (UiImage_State(r->p.text.inlImgs[i]) != r->p.text.inlStates[i]) return 1;
	}
	return 0;
}

/* 图片显示尺寸: 显式 width/height (DIP) 优先; 只给一个则等比.
 * 未指定时沿用默认策略 (夹内容宽 + imageMaxH). 显式尺寸仍夹到可用宽,
 * 避免横向溢出; 显式高度不再被 imageMaxH 覆盖. */
static void md_img_size(float nw, float nh, int ew, int eh, float maxW,
	float maxH, float *ow, float *oh)
{
	float w, h;
	if (ew > 0 || eh > 0) {
		if (ew > 0 && eh > 0) { w = (float)ew; h = (float)eh; }
		else if (ew > 0) { w = (float)ew; h = w * nh / nw; }
		else { h = (float)eh; w = h * nw / nh; }
		if (w > maxW && w > 0.0f) {
			float k = maxW / w;
			w *= k;
			h *= k;
		}
	} else {
		w = nw > maxW ? maxW : nw;
		h = w * nh / nw;
		if (h > maxH) {
			h = maxH;
			w = h * nw / nh;
		}
	}
	if (w < 1.0f) w = 1.0f;
	if (h < 1.0f) h = 1.0f;
	*ow = w;
	*oh = h;
}

static void md_apply_inline_images(UiMarkdown *md, int index, GfxRichText *rt,
	float wrapW)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	float scale = (md->ui && md->ui->scale > 0.0f) ? md->ui->scale : 1.0f;
	int i;
	md_inl_ensure(md, index);
	for (i = 0; i < r->p.text.inlN; i++) {
		UiImage *im = r->p.text.inlImgs[i];
		GfxBitmap *bmp;
		int pw, ph;
		float w, h;
		if (!im || UiImage_State(im) != UI_IMG_READY) continue;
		bmp = UiImage_Bitmap(im);
		pw = UiImage_PixelWidth(im);
		ph = UiImage_PixelHeight(im);
		if (!bmp || pw <= 0 || ph <= 0) continue;
		w = (float)pw / scale;
		h = (float)ph / scale;
		md_img_size(w, h, b->p.text.imgs[i].w, b->p.text.imgs[i].h, wrapW,
			md->imageMaxH, &w, &h);
		Gfx_RichInlineImage(rt, b->p.text.imgs[i].start, b->p.text.imgs[i].len, bmp, w, h);
		r->p.text.inlStates[i] = UI_IMG_READY;
	}
}

static void md_math_draw_cb(void *user, Gfx *g, float x, float baseline,
	float w, float h, GfxColor color)
{
	(void)w;
	(void)h;
	Gfx_MathDraw((GfxMath *)user, g, x, baseline, color);
}

/* 行内公式对象按行字体/字号创建一次; 超可用宽时按比例缩小字号重建
 * (走同一缓存); 创建失败保持 NULL, 渲染层回退为等宽源码文本。 */
static void md_inl_math_ensure(UiMarkdown *md, int index, float maxW)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	GfxFont *font;
	float size;
	int i;
	if (!b || b->p.text.mathCount <= 0 || !md->gfx) return;
	if (r->p.text.inlMath) return;
	r->p.text.inlMath = (GfxMath **)md_alloc_(
		b->p.text.mathCount * (int)sizeof(GfxMath *));
	if (!r->p.text.inlMath) return;
	r->p.text.mathN = b->p.text.mathCount;
	font = md_row_font(md, r);
	size = md_row_font_size(md, r);
	for (i = 0; i < b->p.text.mathCount; i++) {
		const char *src = MdDoc_Url(&md->doc, b->p.text.maths[i].srcOff);
		int len = b->p.text.maths[i].srcLen;
		float sz = size;
		int tries = 0;
		for (;;) {
			float w;
			GfxMath *gm = md_math_cache_get(md, src, len, sz, font, 0);
			if (!gm) break;
			w = Gfx_MathWidth(gm);
			if (maxW < 1.0f || w <= maxW || w <= 1.0f) {
				r->p.text.inlMath[i] = gm;
				break;
			}
			{
				float ns = sz * maxW / w * 0.98f;
				if (ns < 8.0f) ns = 8.0f;
				md_math_cache_put(md, gm);
				if (++tries >= 3 || ns >= sz - 0.01f) {
					r->p.text.inlMath[i] = md_math_cache_get(md, src, len, ns,
						font, 0);
					break;
				}
				sz = ns;
			}
		}
	}
}

static void md_apply_inline_math(UiMarkdown *md, int index, GfxRichText *rt,
	float maxW)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	int i;
	if (!rt || !b || b->p.text.mathCount <= 0) return;
	md_inl_math_ensure(md, index, maxW);
	for (i = 0; i < b->p.text.mathCount; i++) {
		const MdMathRef *m = &b->p.text.maths[i];
		GfxMath *gm = r->p.text.inlMath ? r->p.text.inlMath[i] : NULL;
		if (!gm) {
			Gfx_RichColor(rt, m->start, m->len, md->cInlineCodeText);
			if (md->fCode)
				Gfx_RichFontFamily(rt, m->start, m->len, md->fCode);
			continue;
		}
		Gfx_RichInlineCustom(rt, m->start, m->len,
			Gfx_MathWidth(gm), Gfx_MathHeight(gm) + Gfx_MathDepth(gm),
			Gfx_MathHeight(gm), md_math_draw_cb, gm);
	}
}

static void md_link_chars(const MdBlock *b, int li, int *start, int *len)
{
	*start = 0;
	*len = 0;
	if (!b || li < 0 || li >= b->p.text.linkCount) return;
	{
		const MdLink *l = &b->p.text.links[li];
		if (l->start < 0 || l->end <= l->start || l->end > b->p.text.runCount) return;
		*start = b->p.text.runs[l->start].start;
		*len = b->p.text.runs[l->end - 1].start + b->p.text.runs[l->end - 1].len - *start;
	}
}

static int md_hit_links_at(UiMarkdown *md, UiRect content, int first, int last,
	float mx, float my, int *rowOut, int *linkOut)
{
	int i, li;
	*rowOut = -1;
	*linkOut = -1;
	for (i = first; i <= last; i++) {
		MdRow *r;
		float tx, ty, top, bottom;
		float lx, ly;
		int pos;
		if (i < 0 || i >= md->rowCount) continue;
		r = &md->rows[i];
		if (!md_row_is_text(r->blk->type)) continue;
		if (!r->p.text.rt || r->blk->p.text.linkCount <= 0) continue;
		md_row_pads(md, r, &top, &bottom);
		tx = content.x + (float)r->indent + r->markerW;
		ty = content.y + UiScrollLayout_ItemY(&md->layout, i) + top;
		lx = mx - tx;
		ly = my - ty;
		if (lx < 0.0f || ly < 0.0f) continue;
		if (r->p.text.rtH > 0.0f && ly >= r->p.text.rtH) continue;   /* 行内快速预筛 */
		if (!Gfx_RichHitTest(r->p.text.rt, lx, ly, &pos)) continue;
		for (li = 0; li < r->blk->p.text.linkCount; li++) {
			int start, len;
			md_link_chars(r->blk, li, &start, &len);
			if (len > 0 && pos >= start && pos < start + len) {
				*rowOut = i;
				*linkOut = li;
				return 1;
			}
		}
	}
	return 0;
}

static int md_hit_links(UiMarkdown *md, UiContext *ui, UiRect content,
	int first, int last, int *rowOut, int *linkOut)
{
	if (!ui || !ui->mouseOver || !Ui_InputAllowed(ui)) return 0;
	return md_hit_links_at(md, content, first, last, ui->mouseX, ui->mouseY,
		rowOut, linkOut);
}

static UiRect md_row_display_rect(const UiMarkdown *md, int row)
{
	UiRect rc = { 0, 0, 0, 0 };
	UiRect content;
	if (!md || row < 0 || row >= md->rowCount || !md->layout.items)
		return rc;
	content = UiScrollView_ContentRect(&md->sv);
	rc.x = md->sv.inner.x;
	rc.y = content.y + UiScrollLayout_ItemY(&md->layout, row);
	rc.w = md->sv.inner.w;
	rc.h = md->layout.items[row].height;
	return rc;
}

static UiRect md_rect_union(UiRect a, UiRect b)
{
	UiRect r;
	float ax1, ay1, bx1, by1;
	if (a.w <= 0.0f || a.h <= 0.0f) return b;
	if (b.w <= 0.0f || b.h <= 0.0f) return a;
	ax1 = a.x + a.w;
	ay1 = a.y + a.h;
	bx1 = b.x + b.w;
	by1 = b.y + b.h;
	r.x = a.x < b.x ? a.x : b.x;
	r.y = a.y < b.y ? a.y : b.y;
	r.w = (ax1 > bx1 ? ax1 : bx1) - r.x;
	r.h = (ay1 > by1 ? ay1 : by1) - r.y;
	return r;
}

/* 控件内悬停判定 (鼠标移动事件内同步执行): 只在链接进出时返回 1,
 * 并通过 dirty 给出旧/新链接行区域, 避免整文档重绘. */
static int md_hover_fn(void *user, UiContext *ui, float x, float y,
	UiRect *dirty)
{
	UiMarkdown *md = (UiMarkdown *)user;
	UiRect content;
	int first, last, r = -1, l = -1, changed, oldRow;
	if (!md || !ui || !md->hasLinks || md->rowCount <= 0) return 0;
	oldRow = md->hoverRow;
	if (md->sv.inner.w > 0.0f && md->sv.inner.h > 0.0f &&
		Ui_RectContains(md->sv.inner, x, y)) {
		content = UiScrollView_ContentRect(&md->sv);
		UiScrollLayout_VisibleRange(&md->layout, md->sv.offset,
			md->sv.inner.h, &first, &last);
		if (first < 0) first = 0;
		if (last >= md->rowCount) last = md->rowCount - 1;
		md_hit_links_at(md, content, first, last, x, y, &r, &l);
	}
	changed = (r != md->hoverRow || l != md->hoverLink);
	md->hoverRow = r;
	md->hoverLink = l;
	Ui_SetCursor(ui, md->id, r >= 0 ? UI_CURSOR_HAND : UI_CURSOR_ARROW);
	if (changed && dirty)
		*dirty = md_rect_union(md_row_display_rect(md, oldRow),
			md_row_display_rect(md, r));
	return changed;
}

/* 悬停只改颜色: 沿用布局自带下划线, 不再叠加加粗下划线 */
static void md_hover_set(UiMarkdown *md, int row, int link)
{
	int start, len;
	if (md->hovRow == row && md->hovLink == link) return;
	if (md->hovRow >= 0 && md->hovRow < md->rowCount) {
		MdRow *pr = &md->rows[md->hovRow];
		if (pr->p.text.rt && md->hovLink >= 0 && md->hovLink < pr->blk->p.text.linkCount) {
			md_link_chars(pr->blk, md->hovLink, &start, &len);
			if (len > 0) Gfx_RichColor(pr->p.text.rt, start, len, md->cLink);
		}
	}
	md->hovRow = -1;
	md->hovLink = -1;
	if (row >= 0 && row < md->rowCount) {
		MdRow *nr = &md->rows[row];
		if (nr->p.text.rt && link >= 0 && link < nr->blk->p.text.linkCount) {
			md_link_chars(nr->blk, link, &start, &len);
			if (len > 0) {
				Gfx_RichColor(nr->p.text.rt, start, len, md->cLinkHover);
				md->hovRow = row;
				md->hovLink = link;
			}
		}
	}
}

/* 行内代码底色矩形随布局构建一次; 每帧直接平移绘制, 不再逐帧
 * 调 Gfx_RichRangeRects (DWrite 命中查询). */
static void md_code_cache_build(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	int i, k;
	md_free_(r->p.text.codeRects);
	r->p.text.codeRects = NULL;
	r->p.text.codeN = r->p.text.codeCap = 0;
	if (!r->p.text.rt || !r->blk->p.text.runs) return;
	for (i = 0; i < r->blk->p.text.runCount; i++) {
		const MdRun *run = &r->blk->p.text.runs[i];
		GfxRect tmp[16];
		int n;
		if (!(run->style & MD_R_CODE) || run->len <= 0) continue;
		n = Gfx_RichRangeRects(r->p.text.rt, run->start, run->len, tmp, 16);
		for (k = 0; k < n; k++) {
			if (r->p.text.codeN >= r->p.text.codeCap) {
				int nc = r->p.text.codeCap ? r->p.text.codeCap * 2 : 8;
				GfxRect *np = (GfxRect *)md_realloc_(r->p.text.codeRects,
					(SIZE_T)nc * sizeof(GfxRect));
				if (!np) return;
				r->p.text.codeRects = np;
				r->p.text.codeCap = nc;
			}
			r->p.text.codeRects[r->p.text.codeN++] = tmp[k];
		}
	}
	/* 语法失败的公式按源码回退: 与行内代码一致给底色 */
	if (r->blk->p.text.mathCount > 0 && r->p.text.inlMath) {
		for (i = 0; i < r->blk->p.text.mathCount; i++) {
			const MdMathRef *m = &r->blk->p.text.maths[i];
			GfxRect tmp[16];
			int n, k;
			if (r->p.text.inlMath[i]) continue;
			n = Gfx_RichRangeRects(r->p.text.rt, m->start, m->len, tmp, 16);
			for (k = 0; k < n; k++) {
				if (r->p.text.codeN >= r->p.text.codeCap) {
					int nc = r->p.text.codeCap ? r->p.text.codeCap * 2 : 8;
					GfxRect *np = (GfxRect *)md_realloc_(r->p.text.codeRects,
						(SIZE_T)nc * sizeof(GfxRect));
					if (!np) return;
					r->p.text.codeRects = np;
					r->p.text.codeCap = nc;
				}
				r->p.text.codeRects[r->p.text.codeN++] = tmp[k];
			}
		}
	}
}

static GfxRichText *md_row_make_rt(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	float wrapW;
	GfxFont *font;
	float ls;
	if (r->p.text.rt) return r->p.text.rt;
	if (r->blk->type == MD_BLOCK_HR ||
		r->blk->type == MD_BLOCK_IMAGE ||
		r->blk->type == MD_BLOCK_TABLE ||
		r->blk->type == MD_BLOCK_MATH)
		return NULL;
	wrapW = md_row_wrap_w(md, r);
	font = md_row_font(md, r);
	if (!font || !r->blk->p.text.text16) return NULL;
	/* 含行内对象时用自然行距: UNIFORM 固定行高会压扁/裁掉行内对象 */
	ls = (r->blk->p.text.imgCount > 0 || r->blk->p.text.mathCount > 0)
		? 0.0f : md->lineSpacing;
	/* div 的 line-height: 倍率直接用, 绝对 DIP 按该行字号换算 (gfx_rich
	 * 的行距参数是倍率: lh = size * ls). 含行内对象时仍守自然行距. */
	if (ls > 0.0f && r->boxIdx >= 0 && r->boxIdx < md->boxCount) {
		float bl = md->boxes[r->boxIdx].lineSpacing;
		if (bl > 0.0f) {
			ls = bl;
		} else if (bl < 0.0f) {
			float fsz = md_row_font_size(md, r);
			if (fsz > 0.0f) ls = (-bl) / fsz;
		}
	}
	r->p.text.rt = Gfx_RichCreate(md->gfx, r->blk->p.text.text16, r->blk->p.text.len16,
		font, wrapW, ls, GFX_RICH_WRAP);
	if (r->p.text.rt) {
		if (r->boxIdx >= 0 && r->boxIdx < md->boxCount &&
			md->boxes[r->boxIdx].align != MD_DIV_ALIGN_INHERIT)
			Gfx_RichSetAlign(r->p.text.rt, (int)md->boxes[r->boxIdx].align);
		md_apply_runs(md, r->p.text.rt, r->blk->p.text.runs, r->blk->p.text.runCount,
			r->blk->type == MD_BLOCK_CODE);
		if (r->blk->type == MD_BLOCK_CODE)
			md_highlight(md, r->p.text.rt, r->blk);
		else {
			md_apply_inline_images(md, index, r->p.text.rt, wrapW);
			md_apply_inline_math(md, index, r->p.text.rt, wrapW);
			md_code_cache_build(md, index);
		}
		r->p.text.rtH = Gfx_RichHeight(r->p.text.rt);
		md_live_push(md, index);
	}
	return r->p.text.rt;
}

/* ── 图片 ──────────────────────────────────────────────────────────── */

/* 块级公式: 基准 16 DIP, 超出可用宽时按比例重建 (最多 3 次);
 * 语法错误回退为代码字体的原始源码。 */
static void md_math_block_build(UiMarkdown *md, MdRow *r, float avail)
{
	const char *src;
	float size = 16.0f;
	int tries = 0;
	if (r->p.math.bm || r->p.math.bmFallback) return;
	if (!md->gfx || !r->blk) return;
	if (avail < 1.0f) avail = 1.0f;
	src = MdDoc_Url(&md->doc, r->blk->u.math.mathOff);
	for (;;) {
		float w;
		r->p.math.bm = md_math_cache_get(md, src, r->blk->u.math.mathLen, size,
			md->fBody, 1);
		if (!r->p.math.bm) break;
		w = Gfx_MathWidth(r->p.math.bm);
		if (w <= avail || w <= 1.0f) break;
		size *= avail / w * 0.98f;
		if (size < 8.0f) size = 8.0f;
		md_math_cache_put(md, r->p.math.bm);
		r->p.math.bm = NULL;
		if (++tries >= 3 || size <= 8.0f) {
			r->p.math.bm = md_math_cache_get(md, src, r->blk->u.math.mathLen, size,
				md->fBody, 1);
			break;
		}
	}
	if (r->p.math.bm) {
		r->p.math.bmW = Gfx_MathWidth(r->p.math.bm);
		r->p.math.bmH = Gfx_MathHeight(r->p.math.bm);
		r->p.math.bmD = Gfx_MathDepth(r->p.math.bm);
		return;
	}
	{
		int cap = r->blk->u.math.mathLen + 1, got;
		wchar_t *tmp = (wchar_t *)md_alloc_(
			cap * (int)sizeof(wchar_t));
		if (!tmp) return;
		got = MultiByteToWideChar(CP_UTF8, 0, src, r->blk->u.math.mathLen,
			tmp, cap);
		if (got > 0) {
			r->p.math.bmFallback = Gfx_RichCreate(md->gfx, tmp, got,
				md->fCode ? md->fCode : md->fBody, avail,
				md->lineSpacing, GFX_RICH_WRAP);
			if (r->p.math.bmFallback)
				Gfx_RichColor(r->p.math.bmFallback, 0, got, md->cCodeText);
		}
		md_free_(tmp);
	}
}

static int md_starts_i(const char *s, const char *pre)
{
	int i = 0;
	while (pre[i]) {
		char a = s[i], b = pre[i];
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return 0;
		i++;
	}
	return 1;
}

/* ── 地址解析: 相对链接/图片 → 绝对 ───────────────────────────────── */

/* 带 scheme 的地址 (http:/https:/file:/mailto:/data:/C:\ 都算).
 * 注意: 开头的 '/' 是"站点根相对", 不是绝对地址 —— 对 URL 基准要拼接. */
static int md_has_scheme(const char *u)
{
	int i = 0;
	if (!u || !u[0]) return 0;
	if (!((u[0] >= 'a' && u[0] <= 'z') || (u[0] >= 'A' && u[0] <= 'Z')))
		return 0;
	while (u[i] && u[i] != ':' && u[i] != '/' && u[i] != '\\' && i < 12) i++;
	return u[i] == ':';
}

/* 折叠 "." 与 "a/../" 段; sep 是路径分隔符 (URL 用 '/', 本地用 '\\').
 * 调用前保证 scheme://host 已被排除在 s 之外. */
static void md_path_clean(char *s, char sep)
{
	int r = 0, w = 0;
	int abs0 = (s[0] == sep);

	while (s[r] == sep) s[w++] = s[r++];
	while (s[r]) {
		int seg = r, len, isDot, isDotDot, k;
		while (s[r] && s[r] != sep) r++;
		len = r - seg;
		isDot = (len == 1 && s[seg] == '.');
		isDotDot = (len == 2 && s[seg] == '.' && s[seg + 1] == '.');
		if (isDot) {
			/* 当前目录: 直接跳过 */
		} else if (isDotDot) {
			if (w > 0 && s[w - 1] == sep) w--;
			while (w > 0 && s[w - 1] != sep) w--;
			if (w > 0 && s[w - 1] == sep && s[w] == 0) w--;
		} else if (len > 0) {
			if (w > 0 && s[w - 1] != sep) s[w++] = (char)sep;
			for (k = 0; k < len; k++) s[w++] = s[seg + k];
		}
		if (s[r] == sep) r++;
	}
	/* ".." 把绝对路径吃空 (如 D:\..\a) 时保留根分隔符 */
	if (abs0 && w > 0 && s[0] != sep) {
		int k;
		for (k = w; k >= 0; k--) s[k + 1] = s[k];
		s[0] = (char)sep;
		w++;
	}
	s[w] = 0;
}

/* 旧的 basePath (目录, wchar) 拼接: 保持既有行为. */
static const char *md_basepath_join(UiMarkdown *md, const char *url, char *buf,
	int cap)
{
	int n, ul, i;
	if (!md->basePath || cap < 8) return url;
	n = WideCharToMultiByte(CP_UTF8, 0, md->basePath, -1, buf, cap, NULL, NULL);
	if (n <= 0) return url;
	n--;
	if (n > 0 && buf[n - 1] != '/' && buf[n - 1] != '\\') {
		if (n + 1 >= cap) return url;
		buf[n++] = '\\';
	}
	ul = lstrlenA(url);
	if (n + ul >= cap) return url;
	for (i = 0; i < ul; i++) buf[n + i] = url[i];
	buf[n + ul] = 0;
	return buf;
}

const char *UiMarkdown_ResolveUrl(const UiMarkdown *md, const char *url,
	char *out, int cap)
{
	const char *base;
	int isHttp, n, i, ul, cut;

	if (!url) return "";
	if (!url[0] || url[0] == '#') return url;
	if (md_has_scheme(url)) return url;
	if (!out || cap < 8) return url;
	base = md ? md->docUrl : NULL;
	if (!base || !base[0])
		return md ? md_basepath_join((UiMarkdown *)md, url, out, cap) : url;

	isHttp = md_starts_i(base, "http://") || md_starts_i(base, "https://");
	if (!isHttp) {
		/* 本地文档: 开头带分隔符是绝对路径 (含 UNC) */
		if (url[0] == '/' || url[0] == '\\') return url;
	}
	if (isHttp) {
		int org = 0;
		while (base[org] && base[org] != ':') org++;
		if (base[org] == ':') {
			org++;
			while (base[org] == '/') org++;
			while (base[org] && base[org] != '/' && base[org] != '?' &&
				base[org] != '#') org++;
		}
		if (url[0] == '/') {
			/* 站点根相对: scheme://host + url */
			if (org >= cap) return url;
			for (i = 0; i < org; i++) out[i] = base[i];
			n = org;
		} else {
			/* 文档所在目录: 取 host 之后最后一个 '/' */
			cut = -1;
			for (i = org; base[i]; i++)
				if (base[i] == '/') cut = i;
			if (cut < org) cut = org - 1;   /* 只有 host: 补一个 '/' */
			if (cut + 1 >= cap) return url;
			for (i = 0; i <= cut; i++) out[i] = base[i];
			n = cut + 1;
		}
		ul = lstrlenA(url);
		if (n + ul >= cap) return url;
		for (i = 0; i < ul; i++) out[n + i] = url[i];
		out[n + ul] = 0;
		md_path_clean(out + org, '/');
		return out;
	}

	/* 本地文档: 取目录 (去掉 file:// 前缀), 统一反斜杠 */
	{
		int off = 0;
		if (md_starts_i(base, "file://")) {
			off = 7;
			if (base[off] == '/') off++;
		}
		cut = -1;
		for (i = off; base[i]; i++)
			if (base[i] == '/' || base[i] == '\\') cut = i;
		if (cut < off) return md_basepath_join((UiMarkdown *)md, url, out, cap);
		if (cut + 2 >= cap) return url;
		for (i = off; i <= cut; i++) out[i - off] = base[i];
		n = cut + 1 - off;
		out[n++] = '\\';
		ul = lstrlenA(url);
		if (n + ul >= cap) return url;
		for (i = 0; i < ul; i++)
			out[n + i] = url[i] == '/' ? '\\' : url[i];
		out[n + ul] = 0;
		/* ".." 段: 盘符前缀不参与折叠 ("C:" 不能被当成一段) */
		if (out[0] && out[1] == ':') {
			char tail[900];
			int k = 0;
			for (i = 2; out[i] && k < (int)sizeof(tail) - 1; i++)
				tail[k++] = out[i];
			tail[k] = 0;
			md_path_clean(tail, '\\');
			out[2] = 0;
			for (i = 0; tail[i] && 3 + i < cap; i++) out[2 + i] = tail[i];
			out[2 + i] = 0;
		} else {
			md_path_clean(out, '\\');
		}
		return out;
	}
}

/* 图片缓存键/加载路径: 远程 URL 原样; 相对路径按文档地址解析. */
static const char *md_image_key(UiMarkdown *md, const char *url, char *buf,
	int cap)
{
	if (!url) return "";
	if (!url[0]) return url;
	return UiMarkdown_ResolveUrl(md, url, buf, cap);
}

static void md_image_load(UiMarkdown *md, UiImage *img, const char *url)
{
	char buf[2048];
	const char *key;
	if (!url || !url[0]) return;
	key = md_image_key(md, url, buf, (int)sizeof(buf));
	UiImage_LoadUrl(img, key);
}

static void md_image_ensure(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	if (r->p.image.img) {
		md_live_push(md, index);
		return;
	}
	if (!md->gfx) return;
	r->p.image.img = UiImage_Create(md->gfx, md->net);
	if (!r->p.image.img) return;
	if (md->ui) UiImage_SetContext(r->p.image.img, md->ui);
	UiImage_SetFit(r->p.image.img, UI_IMG_FIT_CONTAIN);
	md_image_load(md, r->p.image.img,
		MdDoc_Url(&md->doc, r->blk->u.image.urlOff));
	md_live_push(md, index);
}

static void md_image_measure(UiMarkdown *md, MdRow *r, float avail)
{
	int pw, ph;
	float scale, nw, nh, dw, dh;
	if (avail < 1.0f) avail = 1.0f;
	/* 宽高都给定时按精确盒子拉伸 (与 HTML/img 语义一致); 只给一个时
	 * 仍等比, 用 CONTAIN 留白. */
	if (r->p.image.img)
		UiImage_SetFit(r->p.image.img,
			(r->blk->u.image.imgW > 0 && r->blk->u.image.imgH > 0)
				? UI_IMG_FIT_STRETCH : UI_IMG_FIT_CONTAIN);
	if (r->p.image.img && UiImage_State(r->p.image.img) == UI_IMG_READY) {
		pw = UiImage_PixelWidth(r->p.image.img);
		ph = UiImage_PixelHeight(r->p.image.img);
		scale = (md->ui && md->ui->scale > 0.0f) ? md->ui->scale : 1.0f;
		if (pw > 0 && ph > 0) {
			nw = (float)pw / scale;
			nh = (float)ph / scale;
			md_img_size(nw, nh, r->blk->u.image.imgW, r->blk->u.image.imgH, avail,
				md->imageMaxH, &dw, &dh);
			r->p.image.imgReady = 1;
			r->p.image.imgW = dw;
			r->p.image.imgH = dh;
			return;
		}
	}
	r->p.image.imgReady = 0;
	r->p.image.imgW = avail;
	r->p.image.imgH = md->imagePlaceholderH;
}

/* ── 表格 ──────────────────────────────────────────────────────────── */

static float md_cell_natural_w(UiMarkdown *md, MdCell *c)
{
	GfxRichText *rt;
	float w = 0.0f;
	if (!c->text16 || c->len16 <= 0) return 0.0f;
	rt = Gfx_RichCreate(md->gfx, c->text16, c->len16, md->fBody,
		10000.0f, md->lineSpacing, 0);
	if (rt) {
		w = Gfx_RichWidth(rt);
		Gfx_RichDestroy(rt);
	}
	return w;
}

typedef struct MdColFit {
	float minW;
	float prefW;
} MdColFit;

static void md_col_fit_measure(void *user, UiMeasure *m)
{
	const MdColFit *c = (const MdColFit *)user;
	m->minW = c->minW;
	m->prefW = c->prefW;
}

/* 总宽超出可用宽但每列硬下限仍放得下时, 用 measure 协议按 nat-min 容量
 * 等比压缩. 失败 (OOM) 返回 0, 由调用方走手工兜底. */
static int md_table_fit_cols(float *colW, const float *nat, int cols,
	float minW, float avail)
{
	MdColFit *fit = (MdColFit *)md_alloc_(cols * (int)sizeof(MdColFit));
	UiMeasureItem *items = (UiMeasureItem *)md_alloc_(
		cols * (int)sizeof(UiMeasureItem));
	UiRect *rects = (UiRect *)md_alloc_(cols * (int)sizeof(UiRect));
	int i, ok = 0;
	if (fit && items && rects) {
		for (i = 0; i < cols; i++) {
			fit[i].minW = minW;
			fit[i].prefW = nat[i];
			items[i].user = &fit[i];
			items[i].measure = md_col_fit_measure;
			items[i].grow = 0.0f;
			items[i].minSize = 0.0f;
			items[i].maxSize = 0.0f;
			items[i].minCross = 0.0f;
			items[i].maxCross = 0.0f;
			items[i].crossSize = 0.0f;
			items[i].stretch = 1;
			items[i].align = UI_ALIGN_START;
		}
		if (Ui_FlexLayout(Ui_Rect(0.0f, 0.0f, avail, 1.0f), UI_ROW, 0.0f,
				items, cols, rects)) {
			for (i = 0; i < cols; i++) colW[i] = rects[i].w;
			ok = 1;
		}
	}
	md_free_(fit);
	md_free_(items);
	md_free_(rects);
	return ok;
}

static int md_table_build(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	int cols = b->u.table.cols, rows = b->u.table.rows, c, rr, sample;
	float avail, padX, padY, minW, total;
	MdCell *cells = b->p.table.cells;
	float *nat;

	if (r->p.table.cellRt) return 1;
	if (cols <= 0 || rows <= 0 || !cells) return 0;
	avail = md_row_wrap_w(md, r);
	if (avail < 7.0f * cols) avail = 7.0f * cols;
	if (avail < 1.0f) avail = 1.0f;
	padX = md->tableCellPadX;
	padY = md->tableCellPadY;
	minW = md->tableMinColW;
	nat = (float *)md_alloc_(cols * (int)sizeof(float));
	if (!nat) return 0;
	for (c = 0; c < cols; c++) nat[c] = 0.0f;

	sample = rows > 64 ? 64 : rows;
	for (rr = 0; rr < sample; rr++) {
		int ri = rows <= 64 ? rr
			: (int)((long long)rr * (rows - 1) / (sample > 1 ? sample - 1 : 1));
		for (c = 0; c < cols; c++) {
			float w = md_cell_natural_w(md, &cells[ri * cols + c]) + padX * 2.0f;
			if (w > nat[c]) nat[c] = w;
		}
	}
	total = 0.0f;
	for (c = 0; c < cols; c++) total += nat[c];

	r->p.table.colW = (float *)md_alloc_(cols * (int)sizeof(float));
	r->p.table.cellRt = (GfxRichText **)md_alloc_(rows * cols * (int)sizeof(GfxRichText *));
	r->p.table.rowH = (float *)md_alloc_(rows * (int)sizeof(float));
	r->p.table.cellW = (float *)md_alloc_(rows * cols * (int)sizeof(float));
	r->p.table.cellCode = (GfxRect **)md_alloc_(
		rows * cols * (int)sizeof(GfxRect *));
	r->p.table.cellCodeN = (int *)md_alloc_(
		rows * cols * (int)sizeof(int));
	if (!r->p.table.colW || !r->p.table.cellRt || !r->p.table.rowH ||
		!r->p.table.cellW || !r->p.table.cellCode || !r->p.table.cellCodeN) {
		md_free_(nat);
		md_row_release(md, r);
		return 0;
	}
	r->p.table.cellN = rows * cols;
	r->p.table.tblCols = cols;
	r->p.table.tblRows = rows;
	if (total <= avail) {
		for (c = 0; c < cols; c++) r->p.table.colW[c] = nat[c];
	} else if (minW * cols >= avail) {
		float eq = avail / cols;
		for (c = 0; c < cols; c++) r->p.table.colW[c] = eq;
	} else if (!md_table_fit_cols(r->p.table.colW, nat, cols, minW, avail)) {
		float extra = avail - minW * cols;
		float sumExtra = 0.0f;
		for (c = 0; c < cols; c++) {
			float e = nat[c] > minW ? nat[c] - minW : 0.0f;
			sumExtra += e;
		}
		for (c = 0; c < cols; c++) {
			float e = nat[c] > minW ? nat[c] - minW : 0.0f;
			r->p.table.colW[c] = minW + (sumExtra > 0.0f
				? extra * e / sumExtra : extra / cols);
		}
	}
	for (rr = 0; rr < rows; rr++) r->p.table.rowH[rr] = 0.0f;
	for (rr = 0; rr < rows; rr++) {
		for (c = 0; c < cols; c++) {
			MdCell *cell = &cells[rr * cols + c];
			float wrapW = r->p.table.colW[c] - padX * 2.0f;
			float h = 0.0f;
			if (wrapW < 1.0f) wrapW = 1.0f;
			if (cell->text16 && cell->len16 > 0) {
				GfxRichText *rt = Gfx_RichCreate(md->gfx, cell->text16,
					cell->len16, md->fBody, wrapW, md->lineSpacing,
					GFX_RICH_WRAP);
				r->p.table.cellRt[rr * cols + c] = rt;
				if (rt) {
					md_apply_runs(md, rt, cell->runs, cell->runCount, 0);
					r->p.table.cellW[rr * cols + c] = Gfx_RichWidth(rt);
					h = Gfx_RichHeight(rt);
					/* 预取内联代码底色矩形: 绘制时不再逐帧 DWrite 命中 */
					if (cell->runCount > 0) {
						int ci = rr * cols + c;
						int ri, k, n = 0, cap = 0;
						GfxRect *rs = NULL;
						for (ri = 0; ri < cell->runCount; ri++)
							if (cell->runs[ri].style & MD_R_CODE)
								cap += 16;
						if (cap > 0)
							rs = (GfxRect *)md_alloc_(
								cap * (int)sizeof(GfxRect));
						for (ri = 0; rs && ri < cell->runCount; ri++) {
							const MdRun *run = &cell->runs[ri];
							GfxRect tmp[16];
							int cn;
							if (!(run->style & MD_R_CODE) || run->len <= 0)
								continue;
							if (n + 16 > cap) {
								int nc2 = cap * 2;
								GfxRect *np = (GfxRect *)md_realloc_(rs,
									(SIZE_T)nc2 * sizeof(GfxRect));
								if (!np) break;
								rs = np;
								cap = nc2;
							}
							cn = Gfx_RichRangeRects(rt, run->start,
								run->len, tmp, 16);
							for (k = 0; k < cn; k++) rs[n++] = tmp[k];
						}
						r->p.table.cellCode[ci] = rs;
						r->p.table.cellCodeN[ci] = n;
					}
				}
			}
			h += padY * 2.0f;
			if (h > r->p.table.rowH[rr]) r->p.table.rowH[rr] = h;
		}
	}
	r->p.table.tblH = 0.0f;
	for (rr = 0; rr < rows; rr++) r->p.table.tblH += r->p.table.rowH[rr];
	md_free_(nat);
	md_live_push(md, index);
	return 1;
}

static void md_measure_row(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	float top, bottom, h;
	md_row_pads(md, r, &top, &bottom);
	switch (r->blk->type) {
	case MD_BLOCK_HR:
		h = md->ruleH + top + bottom;
		break;
	case MD_BLOCK_IMAGE: {
		float avail = md_row_wrap_w(md, r);
		md_image_ensure(md, index);
		md_image_measure(md, r, avail);
		r->p.image.imgStateSeen = r->p.image.img ? UiImage_State(r->p.image.img) : UI_IMG_IDLE;
		h = r->p.image.imgH + top + bottom;
		break;
	}
	case MD_BLOCK_TABLE:
		if (!md_table_build(md, index))
			h = md_row_estimate(md, r);
		else
			h = r->p.table.tblH + top + bottom;
		break;
	case MD_BLOCK_MATH: {
		float avail = md_row_wrap_w(md, r);
		md_math_block_build(md, r, avail);
		if (r->p.math.bm) h = r->p.math.bmH + r->p.math.bmD + top + bottom;
		else if (r->p.math.bmFallback)
			h = Gfx_RichHeight(r->p.math.bmFallback) + top + bottom;
		else
			h = md_row_estimate(md, r);
		break;
	}
	default: {
		GfxRichText *rt = md_row_make_rt(md, index);
		h = rt ? Gfx_RichHeight(rt) + top + bottom
			: md_row_estimate(md, r);
		break;
	}
	}
	UiScrollLayout_SetItemHeight(&md->layout, index, r->hash, h);
	r->measured = 1;
	r->measuredW = md->contentW;
	md->measureCalls++;
}

static void md_measure_visible(UiMarkdown *md, int first, int last)
{
	int i;
	if (first < 0) first = 0;
	if (last >= md->rowCount) last = md->rowCount - 1;
	if (first > 0) first--;
	if (last + 1 < md->rowCount) last++;
	for (i = first; i <= last; i++) {
		MdRow *r = &md->rows[i];
		if (!r->measured) {
			md_measure_row(md, i);
		} else if (r->blk->type == MD_BLOCK_IMAGE) {
			int st = r->p.image.img ? UiImage_State(r->p.image.img) : UI_IMG_IDLE;
			if (!r->p.image.img || st != r->p.image.imgStateSeen)
				md_measure_row(md, i);
		} else if (r->blk->type == MD_BLOCK_TABLE) {
			if (!r->p.table.cellRt) md_measure_row(md, i);
		} else if (r->blk->type == MD_BLOCK_MATH) {
			/* 块级公式对象被驱逐后 (measured 仍为 1) 回到视口需重建 */
			if (!r->p.math.bm && !r->p.math.bmFallback) md_measure_row(md, i);
		} else if (md_row_is_text(r->blk->type) && r->blk->p.text.imgCount > 0 &&
			md_inl_changed(md, i)) {
			if (r->p.text.rt) { Gfx_RichDestroy(r->p.text.rt); r->p.text.rt = NULL; }
			r->measured = 0;
			md_measure_row(md, i);
		} else if (md_row_is_text(r->blk->type) && !r->p.text.rt) {
			md_row_make_rt(md, i);
		}
	}
}

static void md_evict(UiMarkdown *md, int first, int last)
{
	int i;
	for (i = 0; i < md->liveCount; ) {
		int idx = md->live[i];
		if (idx < first - MD_OVERSCAN || idx > last + MD_OVERSCAN) {
			if (idx >= 0 && idx < md->rowCount) {
				md_row_release(md, &md->rows[idx]);
				md->rows[idx].liveStamp = 0;
			}
			md->live[i] = md->live[--md->liveCount];
			continue;
		}
		i++;
	}
}

static void md_invalidate_width(UiMarkdown *md)
{
	int i;
	md->hovRow = -1;
	md->hovLink = -1;
	md->downRow = -1;
	md->downLink = -1;
	md->pressActive = 0;
	md_live_reset(md);
	for (i = 0; i < md->rowCount; i++) {
		MdRow *r = &md->rows[i];
		md_row_payload_release(md, r, 1);
		r->measured = 0;
		/* 保留的图片进入 live, 首帧 evict 回收视口外的 */
		if (r->rowType == MD_BLOCK_IMAGE && r->p.image.img) {
			md->imgRetained++;
			md_live_push(md, i);
		} else if (md_row_is_text(r->rowType) &&
			r->p.text.inlImgs && r->p.text.inlN > 0) {
			md->imgRetained += r->p.text.inlN;
			md_live_push(md, i);
		}
	}
	md->needEstimate = 1;
}

/* ── 文本源与行重建 ────────────────────────────────────────────────── */

static int md_text_set(UiMarkdown *md, const char *s, int n)
{
	if (n < 0) n = 0;
	if (n + 1 > md->textCap) {
		int nc = md->textCap ? md->textCap : 256;
		char *np;
		while (nc < n + 1) nc *= 2;
		np = (char *)md_realloc_(md->text,
			(SIZE_T)nc);
		if (!np) return 0;
		md->text = np;
		md->textCap = nc;
	}
	if (n > 0 && s) {
		int i;
		for (i = 0; i < n; i++) md->text[i] = s[i];
	}
	md->textLen = n;
	md->text[n] = 0;
	return 1;
}

static void md_rows_clear(UiMarkdown *md)
{
	int i;
	for (i = 0; i < md->rowCount; i++)
		md_row_release(md, &md->rows[i]);
	md_live_reset(md);
	md->rowCount = 0;
	md->boxCount = 0;
}

/* 摘出当前行数组: 旧 payload 交给增量匹配, 避免解析后整批销毁 */
static void md_rows_detach(UiMarkdown *md, MdRow **outRows, int *outN,
	UiScrollItem **outItems)
{
	*outRows = md->rows;
	*outN = md->rowCount;
	*outItems = md->items;
	md->rows = NULL;
	md->rowCount = 0;
	md->rowCap = 0;
	md->items = NULL;
	md->itemCap = 0;
	md_live_reset(md);
	md->boxCount = 0;
	md->hovRow = -1;
	md->hovLink = -1;
	md->downRow = -1;
	md->downLink = -1;
	md->pressActive = 0;
}

static int md_row_key_eq(const MdRow *a, const MdRow *b)
{
	return a->hash == b->hash && a->rowType == b->rowType &&
		a->boxSig == b->boxSig;
}

/* 增量复用: 公共前缀/后缀 + 中段 (hash,type) 桶匹配.
 * 匹配行直接搬移 payload (rt/图片/表格/公式都保留), 只对新增/变化行懒测量. */
static void md_rows_merge(UiMarkdown *md, MdRow *oldRows, int oldN,
	UiScrollItem *oldItems)
{
	int n = md->rowCount, i, p = 0, os, ns;
	int *map = NULL;       /* new index -> old index */
	int *moved = NULL;     /* old: 2 = payload 已搬走 */
	int *next = NULL, *head = NULL, cap = 0;

	md->reuseRows = 0;
	md->reuseImages = 0;
	if (oldN > 0) {
		map = (int *)md_alloc_(n * (int)sizeof(int));
		moved = (int *)md_alloc_(oldN * (int)sizeof(int));
		if (!map || !moved) {
			md_free_(map);
			md_free_(moved);
			map = NULL;
			moved = NULL;
		}
	}
	if (map) {
		for (i = 0; i < n; i++) map[i] = -1;
		for (i = 0; i < oldN; i++) moved[i] = 0;
		while (p < n && p < oldN && md_row_key_eq(&oldRows[p], &md->rows[p])) {
			map[p] = p;
			p++;
		}
		os = oldN - 1;
		ns = n - 1;
		while (os >= p && ns >= p && md_row_key_eq(&oldRows[os], &md->rows[ns])) {
			map[ns] = os;
			os--;
			ns--;
		}
		if (os >= p && ns >= p) {
			int mc = os - p + 1;
			cap = 8;
			while (cap < mc * 2) cap *= 2;
			head = (int *)md_alloc_(cap * (int)sizeof(int));
			next = (int *)md_alloc_(oldN * (int)sizeof(int));
			if (head && next) {
				int k;
				for (i = 0; i < cap; i++) head[i] = -1;
				for (i = 0; i < oldN; i++) next[i] = -1;
				for (k = p; k <= os; k++) {
					unsigned slot = (oldRows[k].hash ^
						((unsigned)oldRows[k].rowType * 2654435761u)) &
						(unsigned)(cap - 1);
					next[k] = head[slot];
					head[slot] = k;
				}
				for (i = p; i <= ns; i++) {
					unsigned slot = (md->rows[i].hash ^
						((unsigned)md->rows[i].rowType * 2654435761u)) &
						(unsigned)(cap - 1);
					int prev = -1, cur = head[slot];
					while (cur >= 0 &&
						!(moved[cur] == 0 &&
							md_row_key_eq(&oldRows[cur], &md->rows[i]))) {
						prev = cur;
						cur = next[cur];
					}
					if (cur >= 0) {
						map[i] = cur;
						if (prev < 0) head[slot] = next[cur];
						else next[prev] = next[cur];
					}
				}
			}
			md_free_(head);
			md_free_(next);
		}
	}
	/* 搬 payload; 上下文 (缩进/标记宽) 一致才复用, 否则按新行重建 */
	for (i = 0; i < n; i++) {
		int k = map ? map[i] : -1;
		if (k < 0) continue;
		if (oldRows[k].indent != md->rows[i].indent ||
			oldRows[k].markerW != md->rows[i].markerW)
			continue;
		{
			int was = oldRows[k].measured;
			float h = 0.0f, mw = 0.0f;
			if (was && oldItems) {
				h = oldItems[k].height;
				mw = oldItems[k].measuredW;
			}
			md->rows[i].p = oldRows[k].p;
			ZeroMemory(&oldRows[k].p, sizeof(oldRows[k].p));
			moved[k] = 2;
			md->rows[i].rowType = oldRows[k].rowType;
			if (was && mw == md->contentW) {
				md->rows[i].measured = 1;
				md->rows[i].measuredW = md->contentW;
				UiScrollLayout_SetItemHeight(&md->layout, i,
					md->rows[i].hash, h);
				md_live_push(md, i);
			}
			md->reuseRows++;
			if (md->rows[i].rowType == MD_BLOCK_IMAGE &&
				md->rows[i].p.image.img)
				md->reuseImages++;
		}
	}
	/* 未搬走的旧 payload 全部释放 */
	for (i = 0; i < oldN; i++) {
		if (!moved || moved[i] != 2)
			md_row_payload_release(md, &oldRows[i], 0);
	}
	md_free_(map);
	md_free_(moved);
}

/* ── 章节锚点 ─────────────────────────────────────────────────────── */

/* GitHub 风格 slug: ASCII 小写、空白转 '-'、丢弃 ASCII 标点、保留其它
 * UTF-8 字节 (中文/emoji 原样). 不做 percent 解码. */
static void md_slug_of(const char *u8, char *out, int cap)
{
	int i = 0, o = 0;

	if (!u8 || !out || cap <= 1) return;
	while (u8[i] && o < cap - 1) {
		unsigned char c = (unsigned char)u8[i];
		if (c == ' ' || c == '\t') {
			out[o++] = '-';
			i++;
		} else if (c >= 'A' && c <= 'Z') {
			out[o++] = (char)(c - 'A' + 'a');
			i++;
		} else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_') {
			out[o++] = (char)c;
			i++;
		} else if (c >= 0x80) {
			out[o++] = (char)c;
			i++;
		} else {
			i++;                 /* 其它 ASCII 标点丢弃 */
		}
	}
	out[o] = 0;
}

static void md_anchors_clear(UiMarkdown *md)
{
	/* slug 统一放在 anchorChars 里, 这里只丢引用, 容量留给下次复用 */
	md->anchorCount = 0;
	md->anchorCharsLen = 0;
	if (md->anchorSlots && md->anchorSlotCap > 0)
		ZeroMemory(md->anchorSlots,
			(size_t)md->anchorSlotCap * sizeof(int));
}

static uint32_t md_slug_hash(const char *s)
{
	uint32_t h = 2166136261u;
	while (s && *s) {
		h ^= (uint32_t)(unsigned char)*s++;
		h *= 16777619u;
	}
	return h;
}

/* 哈希桶: 2000 个标题时线性查找是 O(M²) (实测 361ms), 必须开放寻址. */
static int md_anchor_rehash(UiMarkdown *md, int cap)
{
	int *slots;
	int i;
	uint32_t mask;

	if (cap < 32) cap = 32;
	slots = (int *)md_alloc_(cap * (int)sizeof(int));
	if (!slots) return 0;
	ZeroMemory(slots, (size_t)cap * sizeof(int));
	mask = (uint32_t)(cap - 1);
	for (i = 0; i < md->anchorCount; i++) {
		uint32_t j = md_slug_hash(md->anchorChars + md->anchors[i].slugOff) & mask;
		while (slots[j]) j = (j + 1) & mask;
		slots[j] = i + 1;
	}
	md_free_(md->anchorSlots);
	md->anchorSlots = slots;
	md->anchorSlotCap = cap;
	return 1;
}

static int md_anchor_find(const UiMarkdown *md, const char *slug)
{
	uint32_t mask, j;

	if (!slug || !slug[0]) return -1;
	if (!md->anchorSlots || md->anchorSlotCap <= 0) return -1;
	mask = (uint32_t)(md->anchorSlotCap - 1);
	j = md_slug_hash(slug) & mask;
	while (md->anchorSlots[j]) {
		int idx = md->anchorSlots[j] - 1;
		if (md->anchorChars &&
			lstrcmpA(md->anchorChars + md->anchors[idx].slugOff, slug) == 0)
			return idx;
		j = (j + 1) & mask;
	}
	return -1;
}

static int md_anchor_push(UiMarkdown *md, const char *slug, int row,
	int level, const wchar_t *text, int textLen)
{
	MdAnchor *p;
	int ncap, len, i;

	if (md->anchorCount >= md->anchorCap) {
		ncap = md->anchorCap ? md->anchorCap * 2 : 16;
		p = (MdAnchor *)md_realloc_(md->anchors,
			(size_t)ncap * sizeof(MdAnchor));
		if (!p) return 0;
		md->anchors = p;
		md->anchorCap = ncap;
	}
	if (!md->anchorSlots || (md->anchorCount + 1) * 2 > md->anchorSlotCap) {
		int need = md->anchorCount + 1;
		int cap = 32;
		while (cap < need * 2) cap *= 2;
		if (!md_anchor_rehash(md, cap)) return 0;
	}
	len = (int)lstrlenA(slug) + 1;
	if (md->anchorCharsLen + len > md->anchorCharsCap) {
		int nc = md->anchorCharsCap ? md->anchorCharsCap : 256;
		char *np;
		while (nc < md->anchorCharsLen + len) nc *= 2;
		np = (char *)md_realloc_(md->anchorChars, (size_t)nc);
		if (!np) return 0;
		md->anchorChars = np;
		md->anchorCharsCap = nc;
	}
	for (i = 0; i < len; i++) md->anchorChars[md->anchorCharsLen + i] = slug[i];
	md->anchors[md->anchorCount].slugOff = md->anchorCharsLen;
	md->anchors[md->anchorCount].row = row;
	md->anchors[md->anchorCount].h.level = level;
	md->anchors[md->anchorCount].h.row = row;
	md->anchors[md->anchorCount].h.text = text;
	md->anchors[md->anchorCount].h.textLen = textLen;
	md->anchors[md->anchorCount].h.slug = NULL;   /* 字符池定稿后回填 */
	md->anchorCharsLen += len;
	{
		uint32_t mask = (uint32_t)(md->anchorSlotCap - 1);
		uint32_t j = md_slug_hash(slug) & mask;
		while (md->anchorSlots[j]) j = (j + 1) & mask;
		md->anchorSlots[j] = md->anchorCount + 1;
	}
	md->anchorCount++;
	return 1;
}

/* 每次行重建后重算: 标题按文档顺序生成 slug, 重名追加 -1/-2. */
/* 直接从 UTF-16 生成 slug: 规则与 md_slug_of 等价,
 * 但省掉每条标题一次"转 UTF-8 临时串 + 堆分配 + 释放"。
 * 非 ASCII 码点仍按 UTF-8 写出, 和锚点 URL 的编码保持一致。 */
static void md_slug_from_w16(const wchar_t *w, int n, char *out, int cap)
{
	int i = 0, o = 0;

	if (!w || !out || cap <= 1) return;
	while (i < n && o < cap - 1) {
		uint32_t cp = (uint32_t)w[i];
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
			uint32_t lo = (uint32_t)w[i + 1];
			if (lo >= 0xDC00 && lo <= 0xDFFF) {
				cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
				i++;
			}
		}
		i++;
		if (cp == ' ' || cp == '\t') {
			out[o++] = '-';
		} else if (cp >= 'A' && cp <= 'Z') {
			out[o++] = (char)(cp - 'A' + 'a');
		} else if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') ||
			cp == '-' || cp == '_') {
			out[o++] = (char)cp;
		} else if (cp >= 0x80) {
			if (cp < 0x800) {
				if (o + 2 > cap - 1) break;
				out[o++] = (char)(0xC0 | (cp >> 6));
				out[o++] = (char)(0x80 | (cp & 0x3F));
			} else if (cp < 0x10000) {
				if (o + 3 > cap - 1) break;
				out[o++] = (char)(0xE0 | (cp >> 12));
				out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
				out[o++] = (char)(0x80 | (cp & 0x3F));
			} else {
				if (o + 4 > cap - 1) break;
				out[o++] = (char)(0xF0 | (cp >> 18));
				out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
				out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
				out[o++] = (char)(0x80 | (cp & 0x3F));
			}
		}
	}
	out[o] = 0;
}

static void md_anchors_build(UiMarkdown *md)
{
	char slug[256];
	int i, nHead = 0;

	md_anchors_clear(md);
	/* 先数出标题数并一次性建好哈希表: 否则每翻倍一次就要把所有已有
	 * 锚点重新插入一遍, 长文档标题多时这笔重插比建表本身还贵。 */
	for (i = 0; i < md->rowCount; i++)
		if (md->rows[i].rowType == MD_BLOCK_HEADING) nHead++;
	if (nHead > 0) {
		int cap = 32;
		while (cap < nHead * 2) cap *= 2;
		md_anchor_rehash(md, cap);
	}
	for (i = 0; i < md->rowCount; i++) {
		MdRow *r = &md->rows[i];
		char uniq[280];
		int dup = 0;

		if (r->rowType != MD_BLOCK_HEADING || !r->blk) continue;
		if (!r->blk->p.text.text16 || r->blk->p.text.len16 <= 0) continue;
		md_slug_from_w16(r->blk->p.text.text16, r->blk->p.text.len16,
			slug, (int)sizeof(slug));
		if (!slug[0]) continue;
		lstrcpynA(uniq, slug, (int)sizeof(uniq));
		while (md_anchor_find(md, uniq) >= 0) {
			dup++;
			wsprintfA(uniq, "%s-%d", slug, dup);
		}
		md_anchor_push(md, uniq, i, (int)r->blk->level,
			r->blk->p.text.text16, r->blk->p.text.len16);
	}
	/* 字符池可能在中途 realloc, slug 指针统一到最后回填 */
	for (i = 0; i < md->anchorCount; i++)
		md->anchors[i].h.slug = md->anchorChars + md->anchors[i].slugOff;
}

/* 先精确匹配, 再把输入规范化后比较 (`#Getting Started` 也能命中). */
static int md_anchor_lookup(const UiMarkdown *md, const char *name)
{
	char norm[256];
	int idx;

	if (!name || !name[0]) return -1;
	idx = md_anchor_find(md, name);
	if (idx >= 0) return idx;
	md_slug_of(name, norm, (int)sizeof(norm));
	if (!norm[0]) return -1;
	return md_anchor_find(md, norm);
}

/* 首可见行只是"与视口相交的第一行", 它常常是目标标题**上面那一行**
 * (跳转后目标行顶距视口顶只有一个 padTop), 直接拿它反推章节会永远
 * 落后一项, 滚到底时更是全都不满足. 所以章节判定改用标题行顶与视口
 * 顶的距离, 并允许显式跳转锁定. */
static int md_anchor_of_row(const UiMarkdown *md, int row)
{
	int i;
	for (i = 0; i < md->anchorCount; i++)
		if (md->anchors[i].row == row) return i;
	return -1;
}

#define MD_ACTIVE_TOL 12.0f   /* 标题视作"已到达视口顶"的余量 (含 padTop) */

/* 标题按文档顺序, ItemY 非递减 → 二分找最后一个满足条件的, 成本
 * O(log M) 而不是每帧扫全表 (大文档下扫描会随标题数线性增长). */
static int md_anchor_last_before(const UiMarkdown *md, float limit,
	int exclusive)
{
	int lo = 0, hi = md->anchorCount - 1, act = -1;

	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		float y = UiScrollLayout_ItemY(&md->layout, md->anchors[mid].row);
		int hit = exclusive ? (y < limit) : (y <= limit);
		if (hit) { act = mid; lo = mid + 1; }
		else hi = mid - 1;
	}
	return act;
}

static void md_update_active(UiMarkdown *md, float viewH)
{
	float off, maxOff;

	if (md->anchorCount <= 0) {
		md->activeHeading = -1;
		return;
	}
	if (md->activeAnchor >= 0 && md->activeAnchor < md->anchorCount) {
		md->activeHeading = md->activeAnchor;
		return;                 /* 点击/锚点跳转优先, 直到用户自己滚动 */
	}
	off = md->sv.offset;
	md->activeHeading = md_anchor_last_before(md, off + MD_ACTIVE_TOL, 0);
	maxOff = UiScrollView_MaxOffset(&md->sv);
	if (maxOff > 0.5f && off >= maxOff - 0.5f) {
		/* 到底: 末尾几节再也到不了视口顶, 取视口内最后一个标题 */
		int last = md_anchor_last_before(md, off + viewH, 1);
		if (last >= 0) md->activeHeading = last;
	}
}

/* 跳转到行 + 粘性定位. 粘性很关键: ItemY 是"已实测高度 + 未测行估算"
 * 的前缀和, 跳到远处时估算误差会累加, 落点可能偏几百像素; 钉住目标行
 * 直到可见行实测收敛, 位置才是准的. */
static void md_scroll_to_row(UiMarkdown *md, UiContext *ui, int row,
	float padTop, int animate)
{
	float y;

	if (!md || !ui || md->rowCount <= 0) return;
	if (row < 0) row = 0;
	if (row >= md->rowCount) row = md->rowCount - 1;
	md->followTail = 0;      /* 跳转后不要被"跟随尾部"拉回底部 */
	md->pendingTop = 0;
	y = UiScrollLayout_ItemY(&md->layout, row) - padTop;
	if (y < 0.0f) y = 0.0f;
	md->stickRow = row;
	md->stickPad = padTop;
	md->stickSettle = 0;
	md->stickAnim = animate ? 1 : 0;
	md->stickUntil = UiFrame_Now() + 900.0;
	/* 跳到标题上就把它记为当前章节: 底部几节滚不到视口顶, 只能靠这里 */
	md->activeAnchor = md_anchor_of_row(md, row);
	UiScrollView_ScrollTo(&md->sv, ui, y, animate);
}

static int md_scroll_to_anchor(UiMarkdown *md, UiContext *ui,
	const char *name, int animate)
{
	int idx = md_anchor_lookup(md, name);

	if (idx < 0 || !ui) return 0;
	md_scroll_to_row(md, ui, md->anchors[idx].row, 8.0f, animate);
	return 1;
}

static void md_rebuild_rows_incremental(UiMarkdown *md, MdRow *oldRows,
	int oldN, UiScrollItem *oldItems)
{
	int i;
	md->hasLinks = 0;
	for (i = 0; i < md->doc.count; i++)
		md_flatten(md, &md->doc.blocks[i], 0, 0, MD_MK_NONE, 0, MD_TASK_NONE);
	md_anchors_build(md);
	if (!md_rebuild_items(md)) {
		md_rows_clear(md);
		md_anchors_clear(md);
		for (i = 0; i < oldN; i++) md_row_payload_release(md, &oldRows[i], 0);
		UiScrollLayout_Init(&md->layout, NULL, 0);
		return;
	}
	/* 先同步宽度: SetItemHeight 记录 measuredW, 否则下一帧 SetWidth 会把
	 * 复用的实测项再次失效 (行已标记 measured, 估算又会跳过它们)。 */
	UiScrollLayout_SetWidth(&md->layout, md->contentW);
	md_rows_merge(md, oldRows, oldN, oldItems);
	md->needEstimate = 1;
}

static int md_parse_current(UiMarkdown *md)
{
	MdRow *oldRows = NULL;
	UiScrollItem *oldItems = NULL;
	int oldN = 0, rc, i;
	md_rows_detach(md, &oldRows, &oldN, &oldItems);
	rc = Md_Parse(&md->doc, md->text, md->textLen);
	if (rc != 0) {
		md_rows_clear(md);
		for (i = 0; i < oldN; i++) md_row_payload_release(md, &oldRows[i], 0);
		md_free_(oldRows);
		md_free_(oldItems);
		UiScrollLayout_Init(&md->layout, NULL, 0);
		return 0;
	}
	md_rebuild_rows_incremental(md, oldRows, oldN, oldItems);
	md_free_(oldRows);
	md_free_(oldItems);
	return 1;
}

/* ── 绘制 ──────────────────────────────────────────────────────────── */

static void md_draw_marker(UiMarkdown *md, UiContext *ui, const MdRow *r,
	float x, float y)
{
	UiRect rc = Ui_Rect(x, y, r->markerW, 20.0f);
	if (r->markerKind == MD_MK_BULLET) {
		Gfx_Text(md->gfx, md->fBody, "\xE2\x80\xA2", rc, md->cMarker,
			GFX_TEXT_VCENTER);
	} else if (r->markerKind == MD_MK_ORDERED) {
		char num[16];
		float w;
		wsprintfA(num, "%d.", r->markerNo);
		w = Gfx_TextWidth(md->gfx, md->fBody, num);
		rc.x = x + r->markerW - 4.0f - w;
		Gfx_Text(md->gfx, md->fBody, num, rc, md->cMarker, GFX_TEXT_VCENTER);
	} else if (r->markerKind == MD_MK_TASK) {
		UiRect box = Ui_Rect(x, y + 3.0f, 14.0f, 14.0f);
		UiColor primary = ui->theme ? ui->theme->primary.base : 0xFF1E6FD9;
		if (r->task == MD_TASK_DONE) {
			Gfx_FillRoundRect(md->gfx, box, 3.0f, primary);
			Gfx_Text(md->gfx, md->fSmall, "\xE2\x9C\x93",
				Ui_Rect(box.x, box.y, box.w, box.h), 0xFFFFFFFF,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		} else {
			Gfx_StrokeRoundRect(md->gfx, box, 3.0f, md->cMarker, 1.0f);
		}
	}
}

static void md_draw_table(UiMarkdown *md, int index, float x, float y)
{
	MdRow *r = &md->rows[index];
	int rr, c;
	float yy = y, totalW = 0.0f, padX = md->tableCellPadX;
	float padY = md->tableCellPadY;

	if (!r->p.table.cellRt && !md_table_build(md, index)) return;
	if (!r->p.table.cellRt || !r->p.table.colW || !r->p.table.rowH) return;
	for (c = 0; c < r->p.table.tblCols; c++) totalW += r->p.table.colW[c];

	Gfx_FillRect(md->gfx, Ui_Rect(x, y, totalW, r->p.table.rowH[0]), md->cTableHeader);
	for (rr = 0; rr < r->p.table.tblRows; rr++) {
		float xx = x;
		float rh = r->p.table.rowH[rr];
		for (c = 0; c < r->p.table.tblCols; c++) {
			GfxRichText *rt = r->p.table.cellRt[rr * r->p.table.tblCols + c];
			if (rt) {
				MdCell *cell = &r->blk->p.table.cells[rr * r->p.table.tblCols + c];
				float tw = r->p.table.cellW ? r->p.table.cellW[rr * r->p.table.tblCols + c] : 0.0f;
				float tx;
				int al = (cell && r->blk->p.table.align) ? r->blk->p.table.align[c]
					: MD_ALIGN_LEFT;
				if (al == MD_ALIGN_CENTER)
					tx = xx + (r->p.table.colW[c] - tw) * 0.5f;
				else if (al == MD_ALIGN_RIGHT)
					tx = xx + r->p.table.colW[c] - tw - padX;
				else
					tx = xx + padX;
				if (tx < xx + padX) tx = xx + padX;
				{
					int k;
					int ci = rr * r->p.table.tblCols + c;
					GfxRect *cr = r->p.table.cellCode
						? r->p.table.cellCode[ci] : NULL;
					int cn = r->p.table.cellCodeN
						? r->p.table.cellCodeN[ci] : 0;
					for (k = 0; k < cn; k++)
						Gfx_FillRoundRect(md->gfx, Ui_Rect(
							tx + cr[k].x - 1.0f,
							yy + padY + cr[k].y,
							cr[k].w + 2.0f, cr[k].h),
							3.0f, md->cInlineCodeBg);
				}
				Gfx_RichDraw(rt, md->gfx, tx, yy + padY, md->cText);
			}
			if (c > 0)
				Gfx_FillRect(md->gfx, Ui_Rect(xx, yy, 1.0f, rh),
					md->cTableBorder);
			xx += r->p.table.colW[c];
		}
		Gfx_FillRect(md->gfx, Ui_Rect(x, yy, totalW, 1.0f), md->cTableBorder);
		yy += rh;
	}
	Gfx_FillRect(md->gfx, Ui_Rect(x, yy, totalW, 1.0f), md->cTableBorder);
	Gfx_FillRect(md->gfx, Ui_Rect(x, y, 1.0f, yy - y), md->cTableBorder);
	Gfx_FillRect(md->gfx, Ui_Rect(x + totalW - 1.0f, y, 1.0f, yy - y),
		md->cTableBorder);
}

/* 容器盒子: 整段背景/左侧色条/标题 (按视口裁剪, 绘制在行内容之前) */
static void md_draw_boxes(UiMarkdown *md, UiRect content)
{
	int i;
	for (i = 0; i < md->boxCount; i++) {
		MdBox *b = &md->boxes[i];
		float y0, y1, x, w, bh;
		int kind = b->kind;
		if (kind == MD_BOX_DIV) continue;   /* div: 纯布局, 不画色条/标题/底 */
		if (b->firstRow < 0 || b->lastRow >= md->rowCount ||
			b->lastRow < b->firstRow)
			continue;
		y0 = content.y + UiScrollLayout_ItemY(&md->layout, b->firstRow);
		y0 += (float)b->depth * md->boxTitleH;
		y1 = content.y + UiScrollLayout_ItemY(&md->layout, b->lastRow) +
			md->layout.items[b->lastRow].height;
		/* 行高含块尾间距: 盒子收在间距之前, 相邻卡片之间留白 */
		if (y1 - y0 > md->blockGap) y1 -= md->blockGap;
		/* y0/y1 是显示坐标; 视口上下界同样用 inner 的显示坐标 */
		if (y1 <= md->sv.inner.y ||
			y0 >= md->sv.inner.y + md->sv.inner.h) continue;
		x = content.x + (float)b->indent;
		w = content.w - (float)b->indent;
		if (w < 16.0f) continue;
		if (kind < 0 || kind >= MD_BOX_COUNT) kind = MD_BOX_NOTE;
		Gfx_FillRoundRect(md->gfx, Ui_Rect(x, y0, w, y1 - y0),
			md->boxRadius, md->cBoxBg[kind]);
		bh = y1 - y0 - 4.0f;
		if (bh > 0.0f)
			Gfx_FillRect(md->gfx, Ui_Rect(x, y0 + 2.0f, md->boxBarW, bh),
				md->cBoxBar[kind]);
		if (b->titleLen > 0 && b->titleOff >= 0 &&
			b->titleOff < md->doc.urlsLen) {
			Gfx_Text(md->gfx,
				md->fSmallBold ? md->fSmallBold : md->fSmall,
				MdDoc_Url(&md->doc, b->titleOff),
				Ui_Rect(x + md->boxBarW + 10.0f, y0 + 8.0f,
					w - md->boxBarW - 20.0f, 16.0f),
				md->cBoxBar[kind], GFX_TEXT_VCENTER);
		}
	}
}

static void md_draw_row(UiMarkdown *md, UiContext *ui, int index,
	UiRect content)
{
	MdRow *r = &md->rows[index];
	float y = content.y + UiScrollLayout_ItemY(&md->layout, index);
	float h = md->layout.items[index].height;	UiRect rr = Ui_Rect(content.x, y, content.w, h);
	float top, bottom, x, wrapW;
	int d;

	if (h <= 0.0f) return;
	if (UiScrollView_Cull(&md->sv, rr)) return;
	md_row_pads(md, r, &top, &bottom);

	for (d = 0; d < r->quoteDepth; d++) {
		UiRect bar = Ui_Rect(content.x + (float)d * md->quoteIndent,
			y, md->quoteBarW, h);
		Gfx_FillRect(md->gfx, bar, md->cQuoteBar);
	}

	x = content.x + (float)r->indent;
	if (r->markerKind != MD_MK_NONE) md_draw_marker(md, ui, r, x, y + top);

	x += r->markerW;
	wrapW = md_row_wrap_w(md, r);
	if (wrapW < 1.0f) wrapW = 1.0f;

	switch (r->blk->type) {
	case MD_BLOCK_HR:
		Gfx_FillRect(md->gfx, Ui_Rect(x, y + top + md->ruleH * 0.5f,
			wrapW, md->ruleH), md->cRule);
		break;
	case MD_BLOCK_IMAGE: {
		int align = r->blk->u.image.imgAlign != 255 ? (int)r->blk->u.image.imgAlign : md->imageAlign;
		float dw = r->p.image.imgW > 1.0f ? r->p.image.imgW : wrapW;
		float dh = r->p.image.imgH > 1.0f ? r->p.image.imgH : md->imagePlaceholderH;
		float ox = align == MD_ALIGN_CENTER ? (wrapW - dw) * 0.5f
			: (align == MD_ALIGN_RIGHT ? wrapW - dw : 0.0f);
		UiRect ir = Ui_Rect(x + ox, y + top, dw, dh);
		if (r->p.image.img) {
			UiImage_SetRect(r->p.image.img, ir);
			UiImage_Draw(r->p.image.img, ui->theme);
			r->p.image.imgStateSeen = UiImage_State(r->p.image.img);
		} else {
			Gfx_FillRoundRect(md->gfx, ir, 6.0f, md->cCodeBg);
			Gfx_Text(md->gfx, md->fSmall, "\xE5\x9B\xBE\xE7\x89\x87",
				ir, md->cText2, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		}
		break;
	}
	case MD_BLOCK_TABLE:
		md_draw_table(md, index, x, y + top);
		break;
	case MD_BLOCK_MATH: {
		if (r->p.math.bm) {
			float bx = x + (wrapW - r->p.math.bmW) * 0.5f;
			if (bx < x) bx = x;
			Gfx_MathDraw(r->p.math.bm, md->gfx, bx, y + top + r->p.math.bmH, md->cText);
		} else if (r->p.math.bmFallback) {
			Gfx_RichDraw(r->p.math.bmFallback, md->gfx, x, y + top,
				md->cCodeText);
		}
		break;
	}
	case MD_BLOCK_CODE:
		/* 卡面从行顶画到行底-blockGap: 上/下外间距与容器卡一致,
		 * 卡内上下内边距均为 codePadY */
		Gfx_FillRoundRect(md->gfx, Ui_Rect(x, y, wrapW,
			h - md->blockGap), md->codeRadius, md->cCodeBg);
		if (r->p.text.rt) Gfx_RichDraw(r->p.text.rt, md->gfx, x + md->codePadX,
			y + top, md->cCodeText);
		break;
	default: {
		int k;
		if (r->p.text.rt) {
			for (k = 0; k < r->p.text.codeN; k++)
				Gfx_FillRoundRect(md->gfx,
					Ui_Rect(x + r->p.text.codeRects[k].x - 1.0f,
						y + top + r->p.text.codeRects[k].y,
						r->p.text.codeRects[k].w + 2.0f, r->p.text.codeRects[k].h),
					3.0f, md->cInlineCodeBg);
			Gfx_RichDraw(r->p.text.rt, md->gfx, x, y + top, md->cText);
		}
		break;
	}
	}
}

/* ── 公共 API ──────────────────────────────────────────────────────── */

UiMarkdown *UiMarkdown_Create(Gfx *gfx, UiId id, UiNet *net)
{
	UiMarkdown *md = (UiMarkdown *)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY, sizeof(UiMarkdown));
	if (!md) return NULL;
	md->gfx = gfx;
	md->id = id ? id : 1;
	md->net = net;
	md->st = UiMarkdownStyle_Light();
	md->followTheme = 1;
	md->styleDirty = 1;
	md->downRow = -1;
	md->downLink = -1;
	md->stickRow = -1;
	md->firstVisible = -1;
	md->activeAnchor = -1;
	md->activeHeading = -1;
	md->lineSpacing = 1.25f;
	md->blockGap = 10.0f;
	MdDoc_Init(&md->doc);
	{
		UiScrollConfig cfg = UiScrollConfig_Desktop();
		UiScrollbarStyle bar = UiScrollbarStyle_Default();
		UiScrollView_Init(&md->sv, id ? id : 1, &cfg, &bar);
	}
	{
		UiScrollPadding pad;
		pad.left = 18.0f;
		pad.top = 14.0f;
		pad.right = 18.0f;
		pad.bottom = 14.0f;
		UiScrollView_SetPadding(&md->sv, pad);
	}
	UiScrollLayout_Init(&md->layout, NULL, 0);
	md->boxTitleH = 32.0f;
	md->boxIndent = 16.0f;
	md->boxRadius = 6.0f;
	md->boxBarW = 3.0f;
	md_build_fonts(md);
	return md;
}

void UiMarkdown_Destroy(UiMarkdown *md)
{
	if (!md) return;
	md_rows_clear(md);
	md_anchors_clear(md);
	md_math_cache_clear(md);
	md_free_(md->anchors);
	md_free_(md->anchorChars);
	md_free_(md->anchorSlots);
	md_free_(md->rows);
	md_free_(md->boxes);
	md_free_(md->items);
	md_free_(md->live);
	md_free_(md->text);
	MdDoc_Shutdown(&md->doc);
	md_free_fonts(md);
	md_free_(md->basePath);
	md_free_(md->docUrl);
	HeapFree(GetProcessHeap(), 0, md);
}

void UiMarkdown_SetText(UiMarkdown *md, const char *utf8)
{
	if (!md) return;
	if (!md_text_set(md, utf8, utf8 ? (int)lstrlenA(utf8) : 0)) return;
	md_parse_current(md);
	md->pendingTop = 1;
	md->stickRow = -1;      /* 行下标在新文档里没有意义 */
	md->firstVisible = -1;
	md->activeAnchor = -1;
	md->activeHeading = -1;
}

void UiMarkdown_Append(UiMarkdown *md, const char *utf8)
{
	int i, add;
	if (!md || !utf8) return;
	add = (int)lstrlenA(utf8);
	{
		int need = md->textLen + add + 1;
		if (need > md->textCap) {
			int nc = md->textCap ? md->textCap : 256;
			char *np;
			while (nc < need) nc *= 2;
			np = (char *)md_realloc_(md->text, (SIZE_T)nc);
			if (!np) return;
			md->text = np;
			md->textCap = nc;
		}
		for (i = 0; i < add; i++) md->text[md->textLen + i] = utf8[i];
		md->textLen += add;
		md->text[md->textLen] = 0;
	}
	md_parse_current(md);
}

void UiMarkdown_SetStyle(UiMarkdown *md, const UiMarkdownStyle *st)
{
	int i;
	if (!md) return;
	if (st) {
		md->st = *st;
		md->followTheme = 0;
	} else {
		md->st = UiMarkdownStyle_Light();
		md->followTheme = 1;
	}
	md->styleDirty = 1;
	/* 字体对象会被重建: 先释放行 (含公式引用), 再清公式缓存, 最后换字体 */
	for (i = 0; i < md->rowCount; i++) {
		md_row_release(md, &md->rows[i]);
		md->rows[i].measured = 0;
	}
	md_live_reset(md);
	md_math_cache_clear(md);
	md_build_fonts(md);
	md->needEstimate = 1;
}

void UiMarkdown_SetBasePath(UiMarkdown *md, const wchar_t *dir)
{
	int n;
	if (!md) return;
	md_free_(md->basePath);
	md->basePath = NULL;
	if (!dir) return;
	n = lstrlenW(dir);
	md->basePath = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)(n + 1) * sizeof(wchar_t));
	if (!md->basePath) return;
	lstrcpynW(md->basePath, dir, n + 1);
}

void UiMarkdown_SetDocumentUrl(UiMarkdown *md, const char *url)
{
	int n;
	if (!md) return;
	md_free_(md->docUrl);
	md->docUrl = NULL;
	if (!url || !url[0]) return;
	n = lstrlenA(url);
	md->docUrl = (char *)md_alloc_(n + 1);
	if (!md->docUrl) return;
	lstrcpyA(md->docUrl, url);
}

const char *UiMarkdown_DocumentUrl(const UiMarkdown *md)
{
	return md ? md->docUrl : NULL;
}

void UiMarkdown_SetLinkFn(UiMarkdown *md, UiMarkdownLinkFn fn, void *user)
{
	if (md) { md->linkFn = fn; md->linkUser = user; }
}

void UiMarkdown_SetCopyFn(UiMarkdown *md, UiMarkdownCopyFn fn, void *user)
{
	if (md) { md->copyFn = fn; md->copyUser = user; }
}

void UiMarkdown_SetTokenizeFn(UiMarkdown *md, UiMarkdownTokenizeFn fn, void *user)
{
	if (md) { md->tokFn = fn; md->tokUser = user; }
}

const char *UiMarkdown_ClickedUrl(const UiMarkdown *md)
{
	return md ? md->clickedUrl : NULL;
}

int UiMarkdown_Busy(const UiMarkdown *md)
{
	if (!md) return 0;
	return UiScrollView_Busy(&md->sv) || md->imgBusy;
}

int UiMarkdown_ScrollToAnchor(UiMarkdown *md, UiContext *ui,
	const char *name, int animate)
{
	if (!md) return 0;
	return md_scroll_to_anchor(md, ui, name, animate);
}

void UiMarkdown_ScrollToTop(UiMarkdown *md)
{
	if (md) {
		md->pendingTop = 1;
		md->stickRow = -1;
		md->activeAnchor = -1;
	}
}

void UiMarkdown_SetFollowTail(UiMarkdown *md, int on)
{
	if (md) md->followTail = on ? 1 : 0;
}

float UiMarkdown_ContentHeight(const UiMarkdown *md)
{
	return md ? md->layout.contentH : 0.0f;
}

float UiMarkdown_ScrollOffset(const UiMarkdown *md)
{
	return md ? md->sv.offset : 0.0f;
}

float UiMarkdown_MaxOffset(const UiMarkdown *md)
{
	return md ? UiScrollView_MaxOffset(&md->sv) : 0.0f;
}

void UiMarkdown_ScrollToOffset(UiMarkdown *md, UiContext *ui,
	float offset, int animate)
{
	if (!md || !ui) return;
	md->pendingTop = 0;
	md->stickRow = -1;
	md->activeAnchor = -1;
	UiScrollView_ScrollTo(&md->sv, ui, offset, animate);
}

void UiMarkdown_ScrollToRow(UiMarkdown *md, UiContext *ui, int row,
	float padTop, int animate)
{
	md_scroll_to_row(md, ui, row, padTop, animate);
}

int UiMarkdown_FirstVisibleRow(const UiMarkdown *md)
{
	return md ? md->firstVisible : -1;
}

int UiMarkdown_ActiveHeading(const UiMarkdown *md)
{
	return md ? md->activeHeading : -1;
}

int UiMarkdown_HeadingCount(const UiMarkdown *md)
{
	return md ? md->anchorCount : 0;
}

const UiMdHeading *UiMarkdown_HeadingAt(const UiMarkdown *md, int index)
{
	if (!md || index < 0 || index >= md->anchorCount) return NULL;
	return &md->anchors[index].h;
}

int UiMarkdown_DebugRows(const UiMarkdown *md)
{
	return md ? md->rowCount : 0;
}

int UiMarkdown_DebugMeasured(const UiMarkdown *md)
{
	int i, n = 0;
	if (!md) return 0;
	for (i = 0; i < md->rowCount; i++)
		if (md->rows[i].measured) n++;
	return n;
}

int UiMarkdown_DebugMeasureCalls(const UiMarkdown *md)
{
	return md ? md->measureCalls : 0;
}

int UiMarkdown_DebugMathCache(const UiMarkdown *md)
{
	return md ? md->mmathN : 0;
}

int UiMarkdown_DebugMathMisses(const UiMarkdown *md)
{
	return md ? md->mmathMisses : 0;
}

int UiMarkdown_DebugRowSize(void)
{
	return (int)sizeof(MdRow);
}

int UiMarkdown_DebugRowsReused(const UiMarkdown *md)
{
	return md ? md->reuseRows : 0;
}

int UiMarkdown_DebugImagesReused(const UiMarkdown *md)
{
	return md ? md->reuseImages : 0;
}

int UiMarkdown_DebugImagesRetained(const UiMarkdown *md)
{
	return md ? md->imgRetained : 0;
}

int UiMarkdown_DebugTableCols(const UiMarkdown *md, int index, float *out, int cap)
{
	const MdRow *r;
	int i, n;
	if (!md || !out || cap <= 0 || index < 0 || index >= md->rowCount)
		return -1;
	r = &md->rows[index];
	if (r->rowType != MD_BLOCK_TABLE || !r->p.table.colW) return -1;
	n = r->p.table.tblCols;
	if (n <= 0) return -1;
	for (i = 0; i < n && i < cap; i++) out[i] = r->p.table.colW[i];
	return n;
}

int Ui_Markdown(UiMarkdown *md, UiContext *ui, UiRect rc)
{
	float contentW, viewH, oldContentH, anchorDelta = 0.0f;
	int first, last, anchor = -1;
	int anchorValid = 0;
	float anchorTarget = 0.0f;
	int nearBottom = 0;
	double now;
	float dt;

	if (!md || !ui || !ui->gfx || rc.w <= 0.0f || rc.h <= 0.0f) return 0;
	md->ui = ui;
	md->frame++;
	md->imgBusy = 0;
	md->clickedUrl = NULL;
	md->hoverRow = -1;
	md->hoverLink = -1;

	{
		/* 样式是 (theme, st, followTheme) 的纯函数: 只在输入变化时重算.
		 * 链接/代码高亮颜色烙在行布局里, 换肤后必须重建可见行. */
		uint32_t rev = ui->theme ? ui->theme->themeRev : 0;
		if (rev != md->themeRev || md->styleDirty) {
			int themeChanged = rev != md->themeRev;
			md->themeRev = rev;
			md->styleDirty = 0;
			md_resolve_style(md, ui->theme);
			if (themeChanged && md->rowCount > 0) md_invalidate_width(md);
		}
	}
	if (!md->fontsReady) md_build_fonts(md);

	contentW = UiScrollView_ContentWidth(&md->sv, rc);
	viewH = rc.h - md->sv.pad.top - md->sv.pad.bottom;
	if (viewH < 1.0f) viewH = 1.0f;

	now = UiFrame_Now();
	dt = md->lastNow > 0.0 ? (float)(now - md->lastNow) : 16.0f;
	md->lastNow = now;
	if (dt < 0.0f) dt = 0.0f;
	if (dt > 50.0f) dt = 50.0f;
	UiScrollView_Tick(&md->sv, ui, dt);

	(void)Ui_State(ui, md->id, rc, 1);

	UiScrollLayout_SetWidth(&md->layout, contentW);
	if (contentW != md->contentW) {
		md->contentW = contentW;
		if (md->rowCount > 0) md_invalidate_width(md);
	}
	if (md->needEstimate && md->rowCount > 0) {
		md_estimate_all(md);
		UiScrollLayout_Commit(&md->layout);
	}

	oldContentH = md->layout.contentH;
	nearBottom = (oldContentH - viewH - md->sv.target) < 4.0f;

	UiScrollLayout_VisibleRange(&md->layout, md->sv.offset, viewH,
		&first, &last);
	if (first <= last && first < md->rowCount) {
		anchor = first;
		anchorDelta = md->sv.offset - UiScrollLayout_ItemY(&md->layout, first);
	}
	md_measure_visible(md, first, last);
	UiScrollLayout_Commit(&md->layout);

	anchorValid = anchor >= 0 && md->layout.contentH != oldContentH;
	anchorTarget = anchorValid
		? UiScrollLayout_ItemY(&md->layout, anchor) + anchorDelta : 0.0f;

	/* 锚点校正只在滚动空闲时做: 滚轮/拖 thumb 当帧, 以及平滑动画尚未
	 * 到位 (target != offset) 时, ScrollTo 会把 target 拉回当前 offset,
	 * 把这次滚动整个吞掉. */
	{
		float d = md->sv.target - md->sv.offset;
		int scrolling = (ui->wheelTarget == md->sv.id &&
			ui->wheelDelta != 0.0f) || md->sv.draggingThumb ||
			d > 0.5f || d < -0.5f || md->sv.velocity != 0.0f;
		if (scrolling) anchorValid = 0;
	}
	/* 粘性定位期间由目标行决定位置, 首行校正会让位 */
	if (md->stickRow >= 0) anchorValid = 0;

	UiScrollView_Begin(&md->sv, ui, rc, contentW,
		UiScrollLayout_ContentHeight(&md->layout));
	/* 悬停钩子放在 ScrollView 注册之后: 它用同一个 id 再注册视口,
	 * 后注册项才是命中项. */
	if (md->hasLinks)
		Ui_SetHoverFn(ui, md->id, md_hover_fn, md);

	if (anchorValid)
		UiScrollView_ScrollTo(&md->sv, ui, anchorTarget, 0);
	if (md->pendingTop) {
		UiScrollView_ScrollTo(&md->sv, ui, 0.0f, 0);
		md->pendingTop = 0;
		md->followTail = 0;
	} else if (md->followTail && nearBottom) {
		float maxOff = UiScrollView_MaxOffset(&md->sv);
		UiScrollView_ScrollTo(&md->sv, ui, maxOff, 0);
	}

	/* 粘性定位: 目标行顶部钉住视口, 直到懒测量收敛 (连续 3 帧无需
	 * 修正) / 超时 / 用户主动滚动. 直接改 target 而不是 ScrollTo,
	 * 避免每帧把滚动条透明度与空闲计时重置. */
	if (md->stickRow >= 0) {
		int release = 0;
		if (ui->wheelTarget == md->sv.id && ui->wheelDelta != 0.0f)
			release = 1;                       /* 用户滚轮 */
		if (md->sv.draggingThumb) release = 1;  /* 用户拖 thumb */
		if (md->stickRow >= md->rowCount) release = 1;
		if (!release) {
			float y = UiScrollLayout_ItemY(&md->layout, md->stickRow)
				- md->stickPad;
			float maxOff = UiScrollView_MaxOffset(&md->sv);
			if (y < 0.0f) y = 0.0f;
			if (y > maxOff) y = maxOff;
			if (y > md->sv.target + 0.5f || y < md->sv.target - 0.5f) {
				md->sv.target = y;
				/* 非平滑: 直接落位. 直接改 offset 是安全的 ——
				 * 可见区间与绘制都在本块之后重新计算. */
				if (!md->stickAnim) md->sv.offset = y;
				md->stickSettle = 0;
			} else if (md->sv.target == md->sv.offset) {
				if (++md->stickSettle >= 3) release = 1;
			}
			if (now > md->stickUntil) release = 1;
		}
		if (release) md->stickRow = -1;
	}

	UiScrollLayout_VisibleRange(&md->layout, md->sv.offset, viewH,
		&first, &last);
	md->firstVisible = (first <= last && first < md->rowCount) ? first : -1;
	/* 用户自己滚动就不再坚持跳转目标, 回到按视口推导 */
	if ((ui->wheelTarget == md->sv.id && ui->wheelDelta != 0.0f) ||
		md->sv.draggingThumb)
		md->activeAnchor = -1;
	md_update_active(md, viewH);
	{
		UiRect content = UiScrollView_ContentRect(&md->sv);
		int i, event = 0, hr = -1, hl = -1;
		/* 只对滚动位移整体取整一次: 所有行共享同一整数设备偏移,
		 * 行内/行间的亚像素相位恒定. 逐行取整会让不同行在不同
		 * 舍入阈值切换, 块间距逐帧 ±1px 抖动. */
		content.y = Ui_PxSnap(ui, content.y);
		if (first < 0) first = 0;
		if (last >= md->rowCount) last = md->rowCount - 1;
		if (!md->sv.draggingThumb &&
			Ui_RectContains(rc, ui->mouseX, ui->mouseY) &&
			md_hit_links(md, ui, content, first, last, &hr, &hl)) {
			md->hoverRow = hr;
			md->hoverLink = hl;
			Ui_SetCursor(ui, md->id, UI_CURSOR_HAND);
		} else {
			Ui_SetCursor(ui, md->id, UI_CURSOR_ARROW);
		}
		md_hover_set(md, md->hoverRow, md->hoverLink);
		if (ui->mouseDown && ui->pressed == md->id) {
			if (!md->pressActive) {
				md->pressActive = 1;
				md->downRow = hr;
				md->downLink = hl;
			}
		} else {
			if (ui->clicked == md->id && md->pressActive && hr >= 0 &&
				hr == md->downRow && hl == md->downLink) {
				MdRow *lr = &md->rows[hr];
				MdLink *ll = &lr->blk->p.text.links[hl];
				md->clickedUrl = MdDoc_Url(&md->doc, ll->urlOff);
				/* #锚点: 内部平滑滚到对应标题; 仍然回调, 应用可自行拦截 */
				if (md->clickedUrl && md->clickedUrl[0] == '#' &&
					md->clickedUrl[1])
					md_scroll_to_anchor(md, ui, md->clickedUrl + 1, 1);
				if (md->linkFn) md->linkFn(md->linkUser, md->clickedUrl);
				event = 1;
			}
			if (!ui->mouseDown) md->pressActive = 0;
		}
		md_draw_boxes(md, content);
		for (i = first; i <= last; i++) {
			MdRow *r2 = &md->rows[i];
			int k;
			md_draw_row(md, ui, i, content);
			if (r2->blk->type == MD_BLOCK_IMAGE && r2->p.image.img &&
				UiImage_Busy(r2->p.image.img))
				md->imgBusy = 1;
			if (md_row_is_text(r2->blk->type)) {
				for (k = 0; k < r2->p.text.inlN; k++) {
					if (r2->p.text.inlImgs[k] && UiImage_Busy(r2->p.text.inlImgs[k]))
						md->imgBusy = 1;
				}
			}
		}
		UiScrollView_End(&md->sv, ui);
		md_evict(md, first, last);
		return event;
	}
}
