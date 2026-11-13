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
#include "gfx/gfx_rich.h"

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

typedef struct MdRow {
	MdBlock *blk;
	int indent;          /* DIP */
	int quoteDepth;
	int markerKind;
	int markerNo;
	int task;
	float markerW;
	float extraTop;      /* 容器首行: 标题区高度 */
	uint32_t hash;
	int measured;
	float measuredW;
	GfxRichText *rt;
	float rtH;           /* 布局高度缓存: 命中预筛, 避免逐移动 DWrite 查询 */
	GfxRect *codeRects;  /* 行内代码底色矩形 (布局局部坐标), 建 rt 时缓存 */
	int codeN, codeCap;

	/* IMAGE */
	UiImage *img;
	float imgW, imgH;
	int imgReady;
	int imgStateSeen;

	/* TABLE */
	GfxRichText **cellRt;   /* rows*cols; 布局缓存 */
	int cellN;
	float *colW;
	float *rowH;
	float *cellW;        /* 单元格文本宽缓存 (对齐用) */
	int tblCols, tblRows;
	float tblH;

	/* 行内图片 */
	UiImage **inlImgs;
	int *inlStates;
	int inlN;
} MdRow;

typedef struct MdOldRow {
	uint32_t hash;
	float height;
	int measured;
} MdOldRow;

/* 容器盒子: 由 flatten 生成, 渲染时按行区间绘制背景/色条/标题 */
typedef struct MdBox {
	int kind;
	int firstRow, lastRow;
	int indent;
	int depth;           /* 祖先容器数: 标题带下移 depth*boxTitleH */
	int titleOff, titleLen;
} MdBox;

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

	float contentW;
	int needEstimate;
	int pendingTop, followTail;
	int hasLinks;       /* 文档含可悬停链接: 指针移动需逐次重绘 */

	UiContext *ui;
	double lastNow;
	uint32_t frame;

	int *live;
	int liveCount, liveCap;

	int measureCalls;
	int imgBusy;
	const char *clickedUrl;
	int hoverRow, hoverLink;
	int hovRow, hovLink;
	int downRow, downLink;
	int pressActive;
	uint32_t themeRev;

	UiMarkdownLinkFn linkFn;
	void *linkUser;
	UiMarkdownCopyFn copyFn;
	void *copyUser;
	UiMarkdownTokenizeFn tokFn;
	void *tokUser;
	wchar_t *basePath;
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
	int i;

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
		md->boxTitleH = 20.0f;
		md->boxIndent = md->quoteIndent;
		md->boxRadius = 6.0f;
		md->boxBarW = md->quoteBarW;
	}
}

/* ── 行构建 ────────────────────────────────────────────────────────── */

static void md_row_release(UiMarkdown *md, MdRow *r)
{
	int i;
	if (md->hovRow >= 0 && md->hovRow < md->rowCount &&
		&md->rows[md->hovRow] == r) {
		md->hovRow = -1;
		md->hovLink = -1;
	}
	if (r->rt) {
		Gfx_RichDestroy(r->rt);
		r->rt = NULL;
	}
	md_free_(r->codeRects);
	r->codeRects = NULL;
	r->codeN = r->codeCap = 0;
	if (r->cellRt) {
		for (i = 0; i < r->cellN; i++)
			if (r->cellRt[i]) Gfx_RichDestroy(r->cellRt[i]);
		md_free_(r->cellRt);
		r->cellRt = NULL;
	}
	md_free_(r->colW);
	r->colW = NULL;
	md_free_(r->rowH);
	r->rowH = NULL;
	md_free_(r->cellW);
	r->cellW = NULL;
	if (r->img) {
		UiImage_Destroy(r->img);
		r->img = NULL;
	}
	if (r->inlImgs) {
		for (i = 0; i < r->inlN; i++)
			if (r->inlImgs[i]) UiImage_Destroy(r->inlImgs[i]);
		md_free_(r->inlImgs);
		r->inlImgs = NULL;
	}
	md_free_(r->inlStates);
	r->inlStates = NULL;
	r->inlN = 0;
	r->cellN = 0;
	r->tblCols = r->tblRows = 0;
	r->tblH = 0.0f;
	r->rtH = 0.0f;
	r->imgReady = 0;
	r->imgW = r->imgH = 0.0f;
}

static void md_live_reset(UiMarkdown *md)
{
	int i;
	for (i = 0; i < md->liveCount; i++) {
		int idx = md->live[i];
		if (idx >= 0 && idx < md->rowCount)
			md_row_release(md, &md->rows[idx]);
	}
	md->liveCount = 0;
}

static int md_live_push(UiMarkdown *md, int idx)
{
	int i;
	for (i = 0; i < md->liveCount; i++)
		if (md->live[i] == idx) return 1;
	if (md->liveCount >= md->liveCap) {
		int nc = md->liveCap ? md->liveCap * 2 : 32;
		int *np = (int *)md_realloc_(md->live,
			(SIZE_T)nc * sizeof(int));
		if (!np) return 0;
		md->live = np;
		md->liveCap = nc;
	}
	md->live[md->liveCount++] = idx;
	return 1;
}

static int md_box_push(UiMarkdown *md, int kind, int first, int last,
	int indent, int depth, int titleOff, int titleLen)
{
	MdBox *b;
	if (md->boxCount >= md->boxCap) {
		int nc = md->boxCap ? md->boxCap * 2 : 16;
		MdBox *np = (MdBox *)md_realloc_(md->boxes,
			(SIZE_T)nc * sizeof(MdBox));
		if (!np) return 0;
		md->boxes = np;
		md->boxCap = nc;
	}
	b = &md->boxes[md->boxCount++];
	b->kind = kind;
	b->firstRow = first;
	b->lastRow = last;
	b->indent = indent;
	b->depth = depth;
	b->titleOff = titleOff;
	b->titleLen = titleLen;
	return 1;
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
	return r;
}

static int md_flatten(UiMarkdown *md, MdBlock *b, int indent, int qd,
	int mk, int no, int task)
{
	int i, added = 0;
	if (!b) return 0;
	switch (b->type) {
	case MD_BLOCK_QUOTE:
		for (i = 0; i < b->childCount; i++) {
			int r = md_flatten(md, &b->children[i],
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
		float th = md->boxTitleH > 0.0f ? md->boxTitleH : 20.0f;
		int boxAt;
		if (bi <= 0) bi = 16;
		/* 先占位再展开: 外盒必须先于内盒绘制, 否则内层底色被外层覆盖 */
		boxAt = md->boxCount;
		md_box_push(md, b->boxKind, first, first, indent,
			bi > 0 ? indent / bi : 0, b->titleOff, b->titleLen);
		for (i = 0; i < b->childCount; i++) {
			int r = md_flatten(md, &b->children[i], indent + bi, qd,
				added ? MD_MK_NONE : mk,
				added ? 0 : no, added ? MD_TASK_NONE : task);
			if (r) added = 1;
		}
		if (md->rowCount > first) {
			md->rows[first].extraTop += th;
			if (boxAt >= 0 && boxAt < md->boxCount)
				md->boxes[boxAt].lastRow = md->rowCount - 1;
		} else if (boxAt >= 0 && boxAt < md->boxCount) {
			md->boxCount--;
		}
		return added;
	}
	case MD_BLOCK_LIST:
		for (i = 0; i < b->childCount; i++) {
			MdBlock *item = &b->children[i];
			int mk2 = item->task ? MD_MK_TASK
				: (b->ordered ? MD_MK_ORDERED : MD_MK_BULLET);
			int no2 = b->ordered ? b->startNo + i : 0;
			int r = md_flatten(md, item, indent + (int)md->listIndent, qd,
				mk2, no2, item->task);
			if (r) added = 1;
		}
		return added;
	case MD_BLOCK_ITEM:
		for (i = 0; i < b->childCount; i++) {
			int r = md_flatten(md, &b->children[i], indent, qd,
				added ? MD_MK_NONE : mk,
				added ? 0 : no, added ? MD_TASK_NONE : task);
			if (r) added = 1;
		}
		return added;
	default:
		if (b->linkCount > 0) md->hasLinks = 1;
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
	*top = r->extraTop;
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
	default:
		break;
	}
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
	float wrapW = md->contentW - (float)r->indent - r->markerW;
	float size = md_row_font_size(md, r);
	md_row_pads(md, r, &top, &bottom);
	switch (r->blk->type) {
	case MD_BLOCK_HR:
		return md->ruleH + top + bottom;
	case MD_BLOCK_IMAGE:
		return md->imagePlaceholderH + top + bottom;
	case MD_BLOCK_TABLE:
		h = (float)(r->blk->rows > 0 ? r->blk->rows : 1) * 22.0f + 12.0f;
		return h + top + bottom;
	case MD_BLOCK_CODE:
		h = md_est_text(r->blk->text16, r->blk->len16, wrapW, size,
			md->lineSpacing);
		return h + top + bottom;
	default:
		h = md_est_text(r->blk->text16, r->blk->len16, wrapW, size,
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

static int md_keyword(const wchar_t *w, int n)
{
	static const char *kws[] = {
		"int", "char", "float", "double", "void", "bool", "long", "short",
		"unsigned", "signed", "struct", "union", "enum", "typedef", "const",
		"static", "extern", "return", "if", "else", "for", "while", "do",
		"switch", "case", "break", "continue", "goto", "sizeof", "class",
		"public", "private", "protected", "virtual", "override", "namespace",
		"using", "template", "typename", "new", "delete", "this", "nullptr",
		"true", "false", "def", "import", "from", "as", "pass", "elif",
		"try", "except", "finally", "with", "lambda", "yield", "None", "True",
		"False", "async", "await", "func", "var", "let", "package", "type",
		"interface", "map", "chan", "go", "defer", "select", "fn", "impl",
		"mut", "pub", "use", "match", "loop", "in", "not", "and", "or"
	};
	int i, k;
	for (i = 0; i < (int)(sizeof(kws) / sizeof(kws[0])); i++) {
		const char *s = kws[i];
		for (k = 0; k < n && s[k]; k++)
			if (w[k] != (wchar_t)(unsigned char)s[k]) break;
		if (k == n && s[k] == 0) return 1;
	}
	return 0;
}

static int md_wordch(wchar_t c)
{
	return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
		(c >= L'0' && c <= L'9') || c == L'_';
}

static void md_highlight_builtin(UiMarkdown *md, GfxRichText *rt, const MdBlock *b)
{
	const wchar_t *t = b->text16;
	int n = b->len16, i = 0;
	while (i < n) {
		wchar_t c = t[i];
		if (c == L'/' && i + 1 < n && t[i + 1] == L'/') {
			int s = i;
			while (i < n && t[i] != L'\n') i++;
			Gfx_RichColor(rt, s, i - s, md->cTokComment);
			continue;
		}
		if (c == L'/' && i + 1 < n && t[i + 1] == L'*') {
			int s = i;
			i += 2;
			while (i + 1 < n && !(t[i] == L'*' && t[i + 1] == L'/')) i++;
			if (i + 1 < n) i += 2;
			Gfx_RichColor(rt, s, i - s, md->cTokComment);
			continue;
		}
		if (c == L'#') {
			int s = i;
			while (i < n && t[i] != L'\n') i++;
			Gfx_RichColor(rt, s, i - s, md->cTokComment);
			continue;
		}
		if (c == L'"' || c == L'\'' || c == L'`') {
			wchar_t q = c;
			int s = i;
			i++;
			while (i < n) {
				if (t[i] == L'\\' && i + 1 < n) { i += 2; continue; }
				if (t[i] == q) { i++; break; }
				if (t[i] == L'\n') break;
				i++;
			}
			Gfx_RichColor(rt, s, i - s, md->cTokString);
			continue;
		}
		if (c >= L'0' && c <= L'9') {
			int s = i;
			i++;
			while (i < n && (md_wordch(t[i]) || t[i] == L'.')) i++;
			Gfx_RichColor(rt, s, i - s, md->cTokNumber);
			continue;
		}
		if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_') {
			int s = i;
			while (i < n && md_wordch(t[i])) i++;
			if (md_keyword(t + s, i - s))
				Gfx_RichColor(rt, s, i - s, md->cTokKeyword);
			continue;
		}
		i++;
	}
}

static void md_highlight(UiMarkdown *md, GfxRichText *rt, const MdBlock *b)
{
	if (!rt || !b) return;
	if (md->tokFn) {
		int cap = 512, n, i;
		UiMdSpan *sp = (UiMdSpan *)md_alloc_(cap * (int)sizeof(UiMdSpan));
		if (!sp) return;
		n = md->tokFn(md->tokUser, MdDoc_Url(&md->doc, b->langOff),
			b->text16, b->len16, sp, cap);
		if (n > cap) n = cap;
		for (i = 0; i < n; i++) {
			UiColor c = 0;
			if (sp[i].style == MD_TOK_KEYWORD) c = md->cTokKeyword;
			else if (sp[i].style == MD_TOK_STRING) c = md->cTokString;
			else if (sp[i].style == MD_TOK_COMMENT) c = md->cTokComment;
			else if (sp[i].style == MD_TOK_NUMBER) c = md->cTokNumber;
			if (c) Gfx_RichColor(rt, sp[i].start, sp[i].len, c);
		}
		md_free_(sp);
		return;
	}
	md_highlight_builtin(md, rt, b);
}

static void md_inl_ensure(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	int i;
	if (!b || b->imgCount <= 0 || !md->gfx) return;
	if (r->inlImgs) return;
	r->inlImgs = (UiImage **)md_alloc_(b->imgCount * (int)sizeof(UiImage *));
	r->inlStates = (int *)md_alloc_(b->imgCount * (int)sizeof(int));
	if (!r->inlImgs || !r->inlStates) return;
	r->inlN = b->imgCount;
	for (i = 0; i < b->imgCount; i++) {
		UiImage *im = UiImage_Create(md->gfx, md->net);
		if (!im) continue;
		if (md->ui) UiImage_SetContext(im, md->ui);
		UiImage_SetFit(im, UI_IMG_FIT_CONTAIN);
		md_image_load(md, im, MdDoc_Url(&md->doc, b->imgs[i].urlOff));
		r->inlImgs[i] = im;
		r->inlStates[i] = UiImage_State(im);
	}
	md_live_push(md, index);
}

static int md_inl_changed(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	int i;
	for (i = 0; i < r->inlN; i++) {
		if (!r->inlImgs[i]) continue;
		if (UiImage_State(r->inlImgs[i]) != r->inlStates[i]) return 1;
	}
	return 0;
}

static void md_apply_inline_images(UiMarkdown *md, int index, GfxRichText *rt,
	float wrapW)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	float scale = (md->ui && md->ui->scale > 0.0f) ? md->ui->scale : 1.0f;
	int i;
	md_inl_ensure(md, index);
	for (i = 0; i < r->inlN; i++) {
		UiImage *im = r->inlImgs[i];
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
		if (w > wrapW) { w = wrapW; h = w * (float)ph / (float)pw; }
		if (h > md->imageMaxH) {
			h = md->imageMaxH;
			w = h * (float)pw / (float)ph;
		}
		Gfx_RichInlineImage(rt, b->imgs[i].start, b->imgs[i].len, bmp, w, h);
		r->inlStates[i] = UI_IMG_READY;
	}
}

static void md_link_chars(const MdBlock *b, int li, int *start, int *len)
{
	*start = 0;
	*len = 0;
	if (!b || li < 0 || li >= b->linkCount) return;
	{
		const MdLink *l = &b->links[li];
		if (l->start < 0 || l->end <= l->start || l->end > b->runCount) return;
		*start = b->runs[l->start].start;
		*len = b->runs[l->end - 1].start + b->runs[l->end - 1].len - *start;
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
		if (!r->rt || r->blk->linkCount <= 0) continue;
		md_row_pads(md, r, &top, &bottom);
		tx = content.x + (float)r->indent + r->markerW;
		ty = content.y + UiScrollLayout_ItemY(&md->layout, i) + top;
		lx = mx - tx;
		ly = my - ty;
		if (lx < 0.0f || ly < 0.0f) continue;
		if (r->rtH > 0.0f && ly >= r->rtH) continue;   /* 行内快速预筛 */
		if (!Gfx_RichHitTest(r->rt, lx, ly, &pos)) continue;
		for (li = 0; li < r->blk->linkCount; li++) {
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
		if (pr->rt && md->hovLink >= 0 && md->hovLink < pr->blk->linkCount) {
			md_link_chars(pr->blk, md->hovLink, &start, &len);
			if (len > 0) Gfx_RichColor(pr->rt, start, len, md->cLink);
		}
	}
	md->hovRow = -1;
	md->hovLink = -1;
	if (row >= 0 && row < md->rowCount) {
		MdRow *nr = &md->rows[row];
		if (nr->rt && link >= 0 && link < nr->blk->linkCount) {
			md_link_chars(nr->blk, link, &start, &len);
			if (len > 0) {
				Gfx_RichColor(nr->rt, start, len, md->cLinkHover);
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
	md_free_(r->codeRects);
	r->codeRects = NULL;
	r->codeN = r->codeCap = 0;
	if (!r->rt || !r->blk->runs) return;
	for (i = 0; i < r->blk->runCount; i++) {
		const MdRun *run = &r->blk->runs[i];
		GfxRect tmp[16];
		int n;
		if (!(run->style & MD_R_CODE) || run->len <= 0) continue;
		n = Gfx_RichRangeRects(r->rt, run->start, run->len, tmp, 16);
		for (k = 0; k < n; k++) {
			if (r->codeN >= r->codeCap) {
				int nc = r->codeCap ? r->codeCap * 2 : 8;
				GfxRect *np = (GfxRect *)md_realloc_(r->codeRects,
					(SIZE_T)nc * sizeof(GfxRect));
				if (!np) return;
				r->codeRects = np;
				r->codeCap = nc;
			}
			r->codeRects[r->codeN++] = tmp[k];
		}
	}
}

static GfxRichText *md_row_make_rt(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	float wrapW;
	GfxFont *font;
	float ls;
	if (r->rt) return r->rt;
	if (r->blk->type == MD_BLOCK_HR ||
		r->blk->type == MD_BLOCK_IMAGE ||
		r->blk->type == MD_BLOCK_TABLE)
		return NULL;
	wrapW = md->contentW - (float)r->indent - r->markerW;
	if (wrapW < 1.0f) wrapW = 1.0f;
	font = md_row_font(md, r);
	if (!font || !r->blk->text16) return NULL;
	/* 含行内图片时用自然行距: UNIFORM 固定行高会压扁/裁掉行内对象 */
	ls = r->blk->imgCount > 0 ? 0.0f : md->lineSpacing;
	r->rt = Gfx_RichCreate(md->gfx, r->blk->text16, r->blk->len16,
		font, wrapW, ls, GFX_RICH_WRAP);
	if (r->rt) {
		md_apply_runs(md, r->rt, r->blk->runs, r->blk->runCount,
			r->blk->type == MD_BLOCK_CODE);
		if (r->blk->type == MD_BLOCK_CODE)
			md_highlight(md, r->rt, r->blk);
		else {
			md_apply_inline_images(md, index, r->rt, wrapW);
			md_code_cache_build(md, index);
		}
		r->rtH = Gfx_RichHeight(r->rt);
		md_live_push(md, index);
	}
	return r->rt;
}

/* ── 图片 ──────────────────────────────────────────────────────────── */

static void md_u8_to_w(const char *s, wchar_t *out, int cap)
{
	int n;
	if (!out || cap <= 0) return;
	out[0] = 0;
	if (!s) return;
	n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
	if (n <= 0) out[0] = 0;
	else out[cap - 1] = 0;
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

static void md_image_load(UiMarkdown *md, UiImage *img, const char *url)
{
	wchar_t path[1024];
	int n = 0, i = 0, absolute = 0;
	if (!url || !url[0]) return;
	if (md_starts_i(url, "http://") || md_starts_i(url, "https://")) {
		UiImage_LoadUrl(img, url);
		return;
	}
	if (url[0] == '/' || url[0] == '\\' ||
		(url[0] && url[1] == ':'))
		absolute = 1;
	if (!absolute && md->basePath) {
		while (md->basePath[n] && n < 1000) { path[n] = md->basePath[n]; n++; }
		if (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\')
			path[n++] = L'\\';
	}
	while (url[i] && n < 1000) {
		char c = url[i++];
		if (c == '/') c = '\\';
		path[n++] = (wchar_t)(unsigned char)c;
	}
	path[n] = 0;
	UiImage_LoadFile(img, path);
}

static void md_image_ensure(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	if (r->img) return;
	if (!md->gfx) return;
	r->img = UiImage_Create(md->gfx, md->net);
	if (!r->img) return;
	if (md->ui) UiImage_SetContext(r->img, md->ui);
	UiImage_SetFit(r->img, UI_IMG_FIT_CONTAIN);
	md_image_load(md, r->img, MdDoc_Url(&md->doc, r->blk->urlOff));
	md_live_push(md, index);
}

static void md_image_measure(UiMarkdown *md, MdRow *r, float avail)
{
	int pw, ph;
	float scale, nw, nh, dw, dh;
	if (avail < 1.0f) avail = 1.0f;
	if (r->img && UiImage_State(r->img) == UI_IMG_READY) {
		pw = UiImage_PixelWidth(r->img);
		ph = UiImage_PixelHeight(r->img);
		scale = (md->ui && md->ui->scale > 0.0f) ? md->ui->scale : 1.0f;
		if (pw > 0 && ph > 0) {
			nw = (float)pw / scale;
			nh = (float)ph / scale;
			dw = nw > avail ? avail : nw;
			if (dw < 1.0f) dw = 1.0f;
			dh = dw * nh / nw;
			if (dh > md->imageMaxH) {
				dh = md->imageMaxH;
				dw = dh * nw / nh;
			}
			r->imgReady = 1;
			r->imgW = dw;
			r->imgH = dh;
			return;
		}
	}
	r->imgReady = 0;
	r->imgW = avail;
	r->imgH = md->imagePlaceholderH;
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

static int md_table_build(UiMarkdown *md, int index)
{
	MdRow *r = &md->rows[index];
	MdBlock *b = r->blk;
	int cols = b->cols, rows = b->rows, c, rr, sample;
	float avail, padX, padY, minW, total;
	MdCell *cells = b->cells;
	float *nat;

	if (r->cellRt) return 1;
	if (cols <= 0 || rows <= 0 || !cells) return 0;
	avail = md->contentW - (float)r->indent - r->markerW;
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

	r->colW = (float *)md_alloc_(cols * (int)sizeof(float));
	r->cellRt = (GfxRichText **)md_alloc_(rows * cols * (int)sizeof(GfxRichText *));
	r->rowH = (float *)md_alloc_(rows * (int)sizeof(float));
	r->cellW = (float *)md_alloc_(rows * cols * (int)sizeof(float));
	if (!r->colW || !r->cellRt || !r->rowH || !r->cellW) {
		md_free_(nat);
		md_row_release(md, r);
		return 0;
	}
	r->cellN = rows * cols;
	r->tblCols = cols;
	r->tblRows = rows;
	if (total <= avail) {
		for (c = 0; c < cols; c++) r->colW[c] = nat[c];
	} else if (minW * cols >= avail) {
		float eq = avail / cols;
		for (c = 0; c < cols; c++) r->colW[c] = eq;
	} else {
		float extra = avail - minW * cols;
		float sumExtra = 0.0f;
		for (c = 0; c < cols; c++) {
			float e = nat[c] > minW ? nat[c] - minW : 0.0f;
			sumExtra += e;
		}
		for (c = 0; c < cols; c++) {
			float e = nat[c] > minW ? nat[c] - minW : 0.0f;
			r->colW[c] = minW + (sumExtra > 0.0f
				? extra * e / sumExtra : extra / cols);
		}
	}
	for (rr = 0; rr < rows; rr++) r->rowH[rr] = 0.0f;
	for (rr = 0; rr < rows; rr++) {
		for (c = 0; c < cols; c++) {
			MdCell *cell = &cells[rr * cols + c];
			float wrapW = r->colW[c] - padX * 2.0f;
			float h = 0.0f;
			if (wrapW < 1.0f) wrapW = 1.0f;
			if (cell->text16 && cell->len16 > 0) {
				GfxRichText *rt = Gfx_RichCreate(md->gfx, cell->text16,
					cell->len16, md->fBody, wrapW, md->lineSpacing,
					GFX_RICH_WRAP);
				r->cellRt[rr * cols + c] = rt;
				if (rt) {
					md_apply_runs(md, rt, cell->runs, cell->runCount, 0);
					r->cellW[rr * cols + c] = Gfx_RichWidth(rt);
					h = Gfx_RichHeight(rt);
				}
			}
			h += padY * 2.0f;
			if (h > r->rowH[rr]) r->rowH[rr] = h;
		}
	}
	r->tblH = 0.0f;
	for (rr = 0; rr < rows; rr++) r->tblH += r->rowH[rr];
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
		float avail = md->contentW - (float)r->indent - r->markerW;
		md_image_ensure(md, index);
		md_image_measure(md, r, avail);
		r->imgStateSeen = r->img ? UiImage_State(r->img) : UI_IMG_IDLE;
		h = r->imgH + top + bottom;
		break;
	}
	case MD_BLOCK_TABLE:
		if (!md_table_build(md, index))
			h = md_row_estimate(md, r);
		else
			h = r->tblH + top + bottom;
		break;
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
			int st = r->img ? UiImage_State(r->img) : UI_IMG_IDLE;
			if (!r->img || st != r->imgStateSeen)
				md_measure_row(md, i);
		} else if (r->blk->type == MD_BLOCK_TABLE) {
			if (!r->cellRt) md_measure_row(md, i);
		} else if (r->blk->imgCount > 0 && md_inl_changed(md, i)) {
			if (r->rt) { Gfx_RichDestroy(r->rt); r->rt = NULL; }
			r->measured = 0;
			md_measure_row(md, i);
		} else if (r->blk->type != MD_BLOCK_HR && !r->rt) {
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
			if (idx >= 0 && idx < md->rowCount)
				md_row_release(md, &md->rows[idx]);
			md->live[i] = md->live[--md->liveCount];
			continue;
		}
		i++;
	}
}

static void md_invalidate_width(UiMarkdown *md)
{
	int i;
	for (i = 0; i < md->rowCount; i++) {
		md_row_release(md, &md->rows[i]);
		md->rows[i].measured = 0;
	}
	md->liveCount = 0;
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

static void md_rows_clear(UiMarkdown *md, int keep_rt)
{
	int i;
	(void)keep_rt;
	for (i = 0; i < md->rowCount; i++)
		md_row_release(md, &md->rows[i]);
	md->liveCount = 0;
	md->rowCount = 0;
	md->boxCount = 0;
}

/* 迁移旧行高度: prefix + suffix 按 hash 匹配 (不迁移 rt, 按需重建) */
static void md_rebuild_rows(UiMarkdown *md, MdOldRow *old, int oldN)
{
	int i, oi, ni, n;
	md_rows_clear(md, 0);
	md->hasLinks = 0;
	for (i = 0; i < md->doc.count; i++)
		md_flatten(md, &md->doc.blocks[i], 0, 0, MD_MK_NONE, 0, MD_TASK_NONE);
	if (!md_rebuild_items(md)) return;
	/* 先同步宽度: SetItemHeight 记录 measuredW, 否则下一帧 SetWidth 会把
	 * 迁移来的实测项再次失效 (行已标记 measured, 估算又会跳过它们)。 */
	UiScrollLayout_SetWidth(&md->layout, md->contentW);

	n = md->rowCount < oldN ? md->rowCount : oldN;
	for (i = 0; i < n; i++) {
		if (old[i].hash != md->rows[i].hash) break;
		md->rows[i].measured = old[i].measured;
		md->rows[i].measuredW = md->contentW;
		if (old[i].measured) {
			UiScrollLayout_SetItemHeight(&md->layout, i, md->rows[i].hash,
				old[i].height);
		}
	}
	oi = oldN - 1;
	ni = md->rowCount - 1;
	while (oi >= i && ni >= i) {
		if (old[oi].hash != md->rows[ni].hash) break;
		md->rows[ni].measured = old[oi].measured;
		md->rows[ni].measuredW = md->contentW;
		if (old[oi].measured) {
			UiScrollLayout_SetItemHeight(&md->layout, ni, md->rows[ni].hash,
				old[oi].height);
		}
		oi--;
		ni--;
	}
	md->needEstimate = 1;
}

static int md_parse_current(UiMarkdown *md, MdOldRow *old, int oldN)
{
	int rc;
	md_live_reset(md);
	rc = Md_Parse(&md->doc, md->text, md->textLen);
	if (rc != 0) {
		md_rows_clear(md, 0);
		return 0;
	}
	md_rebuild_rows(md, old, oldN);
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

	if (!r->cellRt && !md_table_build(md, index)) return;
	if (!r->cellRt || !r->colW || !r->rowH) return;
	for (c = 0; c < r->tblCols; c++) totalW += r->colW[c];

	Gfx_FillRect(md->gfx, Ui_Rect(x, y, totalW, r->rowH[0]), md->cTableHeader);
	for (rr = 0; rr < r->tblRows; rr++) {
		float xx = x;
		float rh = r->rowH[rr];
		for (c = 0; c < r->tblCols; c++) {
			GfxRichText *rt = r->cellRt[rr * r->tblCols + c];
			if (rt) {
				MdCell *cell = &r->blk->cells[rr * r->tblCols + c];
				float tw = r->cellW ? r->cellW[rr * r->tblCols + c] : 0.0f;
				float tx;
				int al = (cell && r->blk->align) ? r->blk->align[c]
					: MD_ALIGN_LEFT;
				if (al == MD_ALIGN_CENTER)
					tx = xx + (r->colW[c] - tw) * 0.5f;
				else if (al == MD_ALIGN_RIGHT)
					tx = xx + r->colW[c] - tw - padX;
				else
					tx = xx + padX;
				if (tx < xx + padX) tx = xx + padX;
				{
					int ri, k;
					for (ri = 0; cell && ri < cell->runCount; ri++) {
						const MdRun *run = &cell->runs[ri];
						GfxRect cr[16];
						int cn;
						if (!(run->style & MD_R_CODE) || run->len <= 0)
							continue;
						cn = Gfx_RichRangeRects(rt, run->start, run->len,
							cr, 16);
						for (k = 0; k < cn; k++)
							Gfx_FillRoundRect(md->gfx, Ui_Rect(
								tx + cr[k].x - 1.0f,
								yy + padY + cr[k].y,
								cr[k].w + 2.0f, cr[k].h),
								3.0f, md->cInlineCodeBg);
					}
				}
				Gfx_RichDraw(rt, md->gfx, tx, yy + padY, md->cText);
			}
			if (c > 0)
				Gfx_FillRect(md->gfx, Ui_Rect(xx, yy, 1.0f, rh),
					md->cTableBorder);
			xx += r->colW[c];
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
				Ui_Rect(x + md->boxBarW + 10.0f, y0 + 2.0f,
					w - md->boxBarW - 20.0f, md->boxTitleH - 4.0f),
				md->cBoxBar[kind], GFX_TEXT_VCENTER);
		}
	}
}

static void md_draw_row(UiMarkdown *md, UiContext *ui, int index,
	UiRect content)
{
	MdRow *r = &md->rows[index];
	float y = content.y + UiScrollLayout_ItemY(&md->layout, index);
	float h = md->layout.items[index].height;
	UiRect rr = Ui_Rect(content.x, y, content.w, h);
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
	wrapW = content.w - (float)r->indent - r->markerW;
	if (wrapW < 1.0f) wrapW = 1.0f;

	switch (r->blk->type) {
	case MD_BLOCK_HR:
		Gfx_FillRect(md->gfx, Ui_Rect(x, y + top + md->ruleH * 0.5f,
			wrapW, md->ruleH), md->cRule);
		break;
	case MD_BLOCK_IMAGE: {
		int align = r->blk->imgAlign != 255 ? (int)r->blk->imgAlign : md->imageAlign;
		float dw = r->imgW > 1.0f ? r->imgW : wrapW;
		float dh = r->imgH > 1.0f ? r->imgH : md->imagePlaceholderH;
		float ox = align == MD_ALIGN_CENTER ? (wrapW - dw) * 0.5f
			: (align == MD_ALIGN_RIGHT ? wrapW - dw : 0.0f);
		UiRect ir = Ui_Rect(x + ox, y + top, dw, dh);
		if (r->img) {
			UiImage_SetRect(r->img, ir);
			UiImage_Draw(r->img, ui->theme);
			r->imgStateSeen = UiImage_State(r->img);
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
	case MD_BLOCK_CODE:
		Gfx_FillRoundRect(md->gfx, Ui_Rect(x, y + top * 0.25f,
			wrapW, h - bottom - top * 0.0f), md->codeRadius,
			md->cCodeBg);
		if (r->rt) Gfx_RichDraw(r->rt, md->gfx, x + md->codePadX,
			y + top, md->cCodeText);
		break;
	default: {
		int k;
		if (r->rt) {
			for (k = 0; k < r->codeN; k++)
				Gfx_FillRoundRect(md->gfx,
					Ui_Rect(x + r->codeRects[k].x - 1.0f,
						y + top + r->codeRects[k].y,
						r->codeRects[k].w + 2.0f, r->codeRects[k].h),
					3.0f, md->cInlineCodeBg);
			Gfx_RichDraw(r->rt, md->gfx, x, y + top, md->cText);
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
	md->downRow = -1;
	md->downLink = -1;
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
	md->boxTitleH = 20.0f;
	md->boxIndent = 16.0f;
	md->boxRadius = 6.0f;
	md->boxBarW = 3.0f;
	md_build_fonts(md);
	return md;
}

void UiMarkdown_Destroy(UiMarkdown *md)
{
	if (!md) return;
	md_rows_clear(md, 0);
	md_free_(md->rows);
	md_free_(md->boxes);
	md_free_(md->items);
	md_free_(md->live);
	md_free_(md->text);
	MdDoc_Shutdown(&md->doc);
	md_free_fonts(md);
	md_free_(md->basePath);
	HeapFree(GetProcessHeap(), 0, md);
}

void UiMarkdown_SetText(UiMarkdown *md, const char *utf8)
{
	MdOldRow *old = NULL;
	int oldN = 0, i;
	if (!md) return;
	if (md->rowCount > 0) {
		old = (MdOldRow *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			(SIZE_T)md->rowCount * sizeof(MdOldRow));
		if (old) {
			oldN = md->rowCount;
			for (i = 0; i < oldN; i++) {
				old[i].hash = md->rows[i].hash;
				old[i].height = md->layout.items ? md->layout.items[i].height : 0.0f;
				old[i].measured = md->layout.items ? md->layout.items[i].measured : 0;
			}
		}
	}
	if (!md_text_set(md, utf8, utf8 ? (int)lstrlenA(utf8) : 0)) {
		md_free_(old);
		return;
	}
	md_parse_current(md, old, oldN);
	md_free_(old);
	md->pendingTop = 1;
}

void UiMarkdown_Append(UiMarkdown *md, const char *utf8)
{
	MdOldRow *old = NULL;
	int oldN = 0, i, add;
	if (!md || !utf8) return;
	if (md->rowCount > 0) {
		old = (MdOldRow *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			(SIZE_T)md->rowCount * sizeof(MdOldRow));
		if (old) {
			oldN = md->rowCount;
			for (i = 0; i < oldN; i++) {
				old[i].hash = md->rows[i].hash;
				old[i].height = md->layout.items ? md->layout.items[i].height : 0.0f;
				old[i].measured = md->layout.items ? md->layout.items[i].measured : 0;
			}
		}
	}
	add = (int)lstrlenA(utf8);
	if (!md_text_set(md, md->text, md->textLen)) { md_free_(old); return; }
	{
		int need = md->textLen + add + 1;
		if (need > md->textCap) {
			int nc = md->textCap ? md->textCap : 256;
			char *np;
			while (nc < need) nc *= 2;
			np = (char *)md_realloc_(md->text,
				(SIZE_T)nc);
			if (!np) { md_free_(old); return; }
			md->text = np;
			md->textCap = nc;
		}
		for (i = 0; i < add; i++) md->text[md->textLen + i] = utf8[i];
		md->textLen += add;
		md->text[md->textLen] = 0;
	}
	md_parse_current(md, old, oldN);
	md_free_(old);
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
	md_build_fonts(md);
	md_live_reset(md);
	for (i = 0; i < md->rowCount; i++) {
		md->rows[i].measured = 0;
		md->rows[i].rt = NULL;
	}
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

void UiMarkdown_ScrollToTop(UiMarkdown *md)
{
	if (md) md->pendingTop = 1;
}

void UiMarkdown_SetFollowTail(UiMarkdown *md, int on)
{
	if (md) md->followTail = on ? 1 : 0;
}

float UiMarkdown_ContentHeight(const UiMarkdown *md)
{
	return md ? md->layout.contentH : 0.0f;
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

	md_resolve_style(md, ui->theme);
	if (!md->fontsReady) md_build_fonts(md);
	{
		/* 链接/代码高亮颜色烙在行布局里, 换肤后必须重建可见行 */
		uint32_t rev = ui->theme ? ui->theme->themeRev : 0;
		if (rev != md->themeRev) {
			md->themeRev = rev;
			if (md->rowCount > 0) md_invalidate_width(md);
		}
	}

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

	UiScrollLayout_VisibleRange(&md->layout, md->sv.offset, viewH,
		&first, &last);
	{
		UiRect content = UiScrollView_ContentRect(&md->sv);
		int i, event = 0, hr = -1, hl = -1;
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
				MdLink *ll = &lr->blk->links[hl];
				md->clickedUrl = MdDoc_Url(&md->doc, ll->urlOff);
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
			if (r2->img && UiImage_Busy(r2->img)) md->imgBusy = 1;
			for (k = 0; k < r2->inlN; k++) {
				if (r2->inlImgs[k] && UiImage_Busy(r2->inlImgs[k]))
					md->imgBusy = 1;
			}
		}
		UiScrollView_End(&md->sv, ui);
		md_evict(md, first, last);
		return event;
	}
}
