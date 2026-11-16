/*
 * markdown.c - Markdown 浏览器 + 编辑器
 *
 * 三栏结构: 左侧分级标题目录 / 中间渲染视图 / 右侧源码编辑器(可收起).
 * 导航能力:
 *   - 本地 md 相对/绝对路径, 支持 `a.md#锚点`
 *   - http/https md 文件 (UiNet), 按 Content-Type 的 charset 转 UTF-8
 *   - 页内 `#锚点` 跳转 (粘性定位, 懒测量收敛后不漂移)
 *   - 前进 / 后退 (历史项 = 文档 + 首行; 锚点跳转单独记一条)
 *   - 目录树点击跳转, 滚动时高亮当前章节
 *   - 编辑器改动实时同步预览, 本地文件可保存 (Ctrl+S)
 *
 * 构建: bin\cui_markdown.exe (仅此目标链接 gfx_rich/ui_md_parse/ui_md/ui_image/comdlg32)
 */
#include <windows.h>
#include <commdlg.h>
#include <stdarg.h>
#include "ui/ui_app.h"
#include "ui/ui_frame.h"
#include "ui/ui_md.h"
#include "ui/ui_scroll.h"
#include "ui/ui_textarea.h"
#include "ui/ui_tooltip.h"
#include "ui/ui_widgets.h"

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
size_t strlen(const char *s);
void *memcpy(void *d, const void *s, size_t n);

/* ── 动态缓冲 ─────────────────────────────────────────────────────── */

typedef struct DynBuf { char *p; int n, cap; } DynBuf;

static void db_free(DynBuf *b)
{
	if (b->p) HeapFree(GetProcessHeap(), 0, b->p);
	b->p = NULL;
	b->n = b->cap = 0;
}

static int db_reserve(DynBuf *b, int need)
{
	int nc;
	char *np;
	if (need <= b->cap) return 1;
	nc = b->cap > 0 ? b->cap : 256;
	while (nc < need) nc *= 2;
	np = b->p
		? (char *)HeapReAlloc(GetProcessHeap(), 0, b->p, (SIZE_T)nc)
		: (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)nc);
	if (!np) return 0;
	b->p = np;
	b->cap = nc;
	return 1;
}

static void db_addf(DynBuf *b, const char *fmt, ...)
{
	int space, n;
	va_list ap;
	for (;;) {
		space = b->cap - b->n;
		if (space < 2) {
			if (!db_reserve(b, b->cap > 0 ? b->cap * 2 : 256)) return;
			continue;
		}
		va_start(ap, fmt);
		n = vsnprintf(b->p + b->n, (size_t)space, fmt, ap);
		va_end(ap);
		if (n < 0) return;
		if (n < space - 1) { b->n += n; return; }
		if (!db_reserve(b, b->cap * 2)) return;
	}
}

static void db_puts(DynBuf *b, const char *s)
{
	int n = (int)strlen(s);
	if (!db_reserve(b, b->n + n + 1)) return;
	memcpy(b->p + b->n, s, (size_t)n);
	b->n += n;
	b->p[b->n] = 0;
}

/* ── 字符集 / 路径 ────────────────────────────────────────────────── */

static char *u8dup(const char *s)
{
	int n;
	char *p;
	if (!s) return NULL;
	n = (int)strlen(s);
	p = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n + 1);
	if (!p) return NULL;
	memcpy(p, s, (size_t)n + 1);
	return p;
}

static wchar_t *utf8_to_wide(const char *s)
{
	int need;
	wchar_t *w;
	if (!s) return NULL;
	need = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (need <= 0) return NULL;
	w = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
		(SIZE_T)need * sizeof(wchar_t));
	if (!w) return NULL;
	if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, need)) {
		HeapFree(GetProcessHeap(), 0, w);
		return NULL;
	}
	return w;
}

static char *wide_to_utf8(const wchar_t *w)
{
	int need;
	char *p;
	if (!w) return NULL;
	need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if (need <= 0) return NULL;
	p = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)need);
	if (!p) return NULL;
	if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, p, need, NULL, NULL)) {
		HeapFree(GetProcessHeap(), 0, p);
		return NULL;
	}
	return p;
}

static int starts_i(const char *s, const char *pre)
{
	int i = 0;
	if (!s) return 0;
	while (pre[i]) {
		char a = s[i], b = pre[i];
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return 0;
		i++;
	}
	return 1;
}

static int is_remote(const char *u)
{
	return u && (starts_i(u, "http://") || starts_i(u, "https://"));
}

static int is_local_abs(const char *u)
{
	if (!u || !u[0]) return 0;
	if (u[0] == '\\' && u[1] == '\\') return 1;
	if (u[1] == ':') return 1;
	return 0;
}

/* Content-Type 里的 charset; 非 UTF-8 按代码页转码. 返回新缓冲 (调用方释放). */
static char *http_body_utf8(const UiHttpResponse *r, int *outLen)
{
	const char *body = (const char *)r->body;
	int len = r->bodyLen;
	char ctype[256];
	char cs[32];
	int cp = 0;
	char *out;

	*outLen = 0;
	if (!body || len <= 0) return NULL;
	cs[0] = 0;
	if (UiHttp_Header(r, "content-type", ctype, (int)sizeof(ctype)) > 0) {
		const char *p = ctype;
		while (*p) {
			if (starts_i(p, "charset=")) {
				const char *q = p + 8;
				int k = 0;
				if (*q == '"') q++;
				while (*q && *q != '"' && *q != ';' && *q != ' ' &&
					k < (int)sizeof(cs) - 1) {
					char c = *q++;
					if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
					cs[k++] = c;
				}
				cs[k] = 0;
				break;
			}
			p++;
		}
	}
	if (cs[0] && !starts_i(cs, "utf-8") && !starts_i(cs, "utf8")) {
		if (starts_i(cs, "gb")) cp = 936;
		else if (starts_i(cs, "big5")) cp = 950;
		else if (starts_i(cs, "iso-8859-1") || starts_i(cs, "latin"))
			cp = 28591;
		else if (starts_i(cs, "utf-16")) cp = 1200;
	}
	if (cp == 0) {
		int off = 0;
		if (len >= 3 && (unsigned char)body[0] == 0xEF &&
			(unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF)
			off = 3;
		out = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(len - off) + 1);
		if (!out) return NULL;
		memcpy(out, body + off, (size_t)(len - off));
		out[len - off] = 0;
		*outLen = len - off;
		return out;
	}
	{
		int wc = MultiByteToWideChar(cp, 0, body, len, NULL, 0);
		wchar_t *w;
		int need;
		if (wc <= 0) return NULL;
		w = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)wc * sizeof(wchar_t));
		if (!w) return NULL;
		MultiByteToWideChar(cp, 0, body, len, w, wc);
		need = WideCharToMultiByte(CP_UTF8, 0, w, wc, NULL, 0, NULL, NULL);
		if (need <= 0) {
			HeapFree(GetProcessHeap(), 0, w);
			return NULL;
		}
		out = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)need + 1);
		if (out) {
			WideCharToMultiByte(CP_UTF8, 0, w, wc, out, need, NULL, NULL);
			out[need] = 0;
			*outLen = need;
		}
		HeapFree(GetProcessHeap(), 0, w);
		return out;
	}
}

static char *load_file_utf8(const wchar_t *path, int *outLen)
{
	HANDLE h;
	LARGE_INTEGER li;
	DWORD size, got = 0;
	BYTE *raw;
	char *out = NULL;
	int n = 0;

	*outLen = 0;
	h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 ||
		li.QuadPart > 32 * 1024 * 1024) {
		CloseHandle(h);
		return NULL;
	}
	size = (DWORD)li.QuadPart;
	raw = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size + 2);
	if (!raw) {
		CloseHandle(h);
		return NULL;
	}
	if (!ReadFile(h, raw, size, &got, NULL) || got != size) {
		HeapFree(GetProcessHeap(), 0, raw);
		CloseHandle(h);
		return NULL;
	}
	CloseHandle(h);
	raw[size] = 0;

	if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
		n = (int)(size - 3);
		out = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);
		if (out) {
			memcpy(out, raw + 3, (size_t)n);
			out[n] = 0;
		}
	} else if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
		const wchar_t *ws = (const wchar_t *)(raw + 2);
		int wc = (int)((size - 2) / 2);
		int need = WideCharToMultiByte(CP_UTF8, 0, ws, wc, NULL, 0, NULL, NULL);
		if (need > 0) {
			out = (char *)HeapAlloc(GetProcessHeap(), 0, need + 1);
			if (out) {
				int m = WideCharToMultiByte(CP_UTF8, 0, ws, wc, out, need,
					NULL, NULL);
				n = m > 0 ? m : 0;
				out[n] = 0;
			}
		}
	} else if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
		int wc = (int)((size - 2) / 2);
		wchar_t *sw = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
			(SIZE_T)wc * sizeof(wchar_t));
		if (sw) {
			int i, need;
			for (i = 0; i < wc; i++)
				sw[i] = (wchar_t)(((WORD)raw[2 + i * 2] << 8) |
					raw[3 + i * 2]);
			need = WideCharToMultiByte(CP_UTF8, 0, sw, wc, NULL, 0, NULL, NULL);
			if (need > 0) {
				out = (char *)HeapAlloc(GetProcessHeap(), 0, need + 1);
				if (out) {
					int m = WideCharToMultiByte(CP_UTF8, 0, sw, wc, out, need,
						NULL, NULL);
					n = m > 0 ? m : 0;
					out[n] = 0;
				}
			}
			HeapFree(GetProcessHeap(), 0, sw);
		}
	} else {
		out = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
		if (out) {
			memcpy(out, raw, size);
			out[size] = 0;
			n = (int)size;
		}
	}
	HeapFree(GetProcessHeap(), 0, raw);
	*outLen = n;
	return out;
}

static int save_file_utf8(const wchar_t *path, const char *text, int len)
{
	HANDLE h;
	DWORD wrote = 0;
	int ok;
	h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	ok = WriteFile(h, text, (DWORD)len, &wrote, NULL) && wrote == (DWORD)len;
	CloseHandle(h);
	return ok;
}

/* ── 应用状态 ─────────────────────────────────────────────────────── */

/* 工具栏布局常量与矩形: 绘制与 WM_NCHITTEST 共用, 定义在 App 之前
 * 是因为 App 要缓存一份 (nchittest 里不重复测量文字).
 * 按钮统一 32x30 的方形图标位: 分组之间 12, 组内 4, 分隔线落在 12 的中间. */
#define TB_TOP  10.0f
#define TB_H    30.0f
#define TB_GAP  8.0f
#define TB_IW   32.0f
#define TB_G4   4.0f
#define TB_G12  12.0f

typedef struct ToolbarRects {
	UiRect title, back, fwd, open, save, bar, toc, edit, theme, close;
	UiRect sep1, sep2;      /* 导航 | 文件 | 地址栏 的分组细线 */
} ToolbarRects;

/* 悬停提示槽位 (顺序即绘制顺序) */
enum {
	TIP_BACK, TIP_FWD, TIP_OPEN, TIP_SAVE,
	TIP_TOC, TIP_EDIT, TIP_THEME, TIP_CLOSE, TIP_TOP, TIP_N
};

typedef struct HistEntry {
	char *loc;      /* 文档地址, 可带 "#slug" */
	int row;        /* 离开时的首行 */
} HistEntry;

typedef struct TocNode {
	int level;
	int row;
	int collapsed;
	int parent;     /* 最近的前一个更高级标题; -1 = 根 */
	int hasChild;
	wchar_t text[160];
	int len;
} TocNode;

typedef struct App {
	UiMarkdown *md;
	UiNet *net;
	UiTextArea src;
	UiScrollView tocSv;
	ToolbarRects tb;        /* 上一帧的工具栏矩形 (nchittest 用) */
	float titleScale;       /* 上次量标题宽度时的 DPI 比例 */
	double tocNow;

	char *doc;              /* 当前文档文本 (唯一真源) */
	char docUrl[1024];      /* 当前文档地址; 空 = 内置示例 */
	char status[320];
	char loadUrl[1024];

	/* 历史 */
	HistEntry hist[64];
	int histN, histPos;
	int histBusy;

	/* 待执行的滚动 (文档刚换好, 等 ui 可用时再跳) */
	int pendingRow;
	char pendingAnchor[256];

	/* 远程加载 */
	uint64_t reqId;
	int loading;

	/* 目录 */
	TocNode *toc;
	int tocN, tocCap;
	int tocDirty;
	int tocOpen;

	/* 编辑器 */
	int editorOpen;
	int wantOpen;
	int wantSave;
	DynBuf editBuf;

	/* 工具栏/浮层悬停提示 */
	UiTooltip tips[TIP_N];
	int tipsReady;
	UiRect topRc;           /* "回到顶部" 按钮本帧矩形; 空 = 未显示 */

	char imgPath[512];
	char inlinePath[512];
} App;

/* ── 内置示例文档 ─────────────────────────────────────────────────── */

static const char *g_intro =
	"# CUI Markdown 浏览器\n"
	"\n"
	"左侧是**分级标题目录**，点击即可跳转；滚动时目录会跟随高亮当前章节。\n"
	"本页的 `#锚点` 链接都走粘性定位：跳转后目标标题被钉在视口顶部，"
	"直到懒测量的高度收敛，不会因为估算误差而偏移。\n"
	"\n"
	"[跳到「导航与历史」](#导航与历史) ｜ [跳到「排版容器」](#排版容器)\n"
	"\n"
	"## 特性\n"
	"\n"
	"- 行模型：一个叶子块 = 一个滚动项\n"
	"- 懒测量：首帧只做高度估算，滚动时才实测可见行\n"
	"- 增量：`Append` 保留已实测高度，尾部按需收敛\n"
	"- 相对地址：按当前文档地址解析链接与图片\n"
	"- 排版标签：`<div align|style>` / `<p>` / `<center>`\n"
	"\n"
	"> 引用块用于强调：性能与文档规模解耦，单帧成本只与视口相关。\n"
	"\n"
	"### 代码\n"
	"\n"
	"```c\n"
	"UiMarkdown *md = UiMarkdown_Create(gfx, id, net);\n"
	"UiMarkdown_SetDocumentUrl(md, \"D:\\\\docs\\\\a.md\");\n"
	"UiMarkdown_SetText(md, text);\n"
	"UiMarkdown_ScrollToRow(md, ui, row, 8.0f, 1);\n"
	"```\n"
	"\n"
	"### 任务列表\n"
	"\n"
	"- [x] 解析器 + 金样测试\n"
	"- [x] 行模型与懒测量\n"
	"- [x] 图片 / 表格真实渲染\n"
	"- [x] 目录树 / 前进后退 / 相对链接\n"
	"- [ ] 全文选区\n"
	"\n"
	"### 图片\n"
	"\n";

static const char *g_tail =
	"### 排版容器\n"
	"\n"
	"<center>这一行由 center 标签居中</center>\n"
	"\n"
	"<p align=\"center\">p align=center 单行居中，支持 **粗体** 与 `行内码`。</p>\n"
	"\n"
	"<div align=\"center\" style=\"padding:10px 16px; margin:8px 0; line-height:1.8\">\n"
	"#### div 内的小标题也居中\n"
	"\n"
	"正文继续按 Markdown 解析，内边距与 1.8 倍行距同时生效。\n"
	"\n"
	"- 列表项在 div 内照常解析\n"
	"- 代码块保持左对齐\n"
	"</div>\n"
	"\n"
	"### 提示容器\n"
	"\n"
	"::: tip 小提示\n"
	"支持 `::: note/tip/warning/danger` 容器，正文按普通块递归解析。\n"
	":::\n"
	"\n"
	"::: warning\n"
	"警告容器：左侧色条 + 标题 + 低饱和底色，颜色随主题切换。\n"
	":::\n"
	"\n"
	"> [!NOTE]\n"
	"> GitHub 风格警报与 `:::` 容器共用同一渲染器。\n"
	"\n"
	":::: danger 嵌套示例\n"
	"::: tip\n"
	"内部容器可以嵌套。\n"
	":::\n"
	"::::\n"
	"\n"
	"### 数学公式\n"
	"\n"
	"行内公式 $E = mc^2$ 与 $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ 跟随文字排版。\n"
	"\n"
	"超长行内公式按可用宽自动缩小："
	"$\\sum_{k=0}^{n} \\frac{k^{2}+2k+1}{k+1} + \\sum_{k=0}^{n} \\frac{k^{2}-2k+1}{k-1}"
	" + \\int_{0}^{1} \\frac{x^{3}+1}{x+1}\\,dx + \\int_{0}^{1} \\frac{x^{3}-1}{x-1}\\,dx$"
	" 后继续正文。\n"
	"\n"
	"$$\n"
	"\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}\n"
	"$$\n"
	"\n"
	"$$\n"
	"\\int_{0}^{1} x^2 \\, dx = \\frac{1}{3}\n"
	"$$\n"
	"\n"
	"\\[\n"
	"\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}\n"
	"\\]\n"
	"\n"
	"$$\n"
	"\\begin{aligned} f(x) &= x^2 \\\\ g(x) &= \\sqrt{x} \\end{aligned}\n"
	"$$\n"
	"\n"
	"$$\n"
	"f(x) = \\begin{cases} x, & x \\ge 0 \\\\ -x, & x < 0 \\end{cases}\n"
	"$$\n"
	"\n"
	"### 导航与历史\n"
	"\n"
	"支持三种目标：\n"
	"\n"
	"1. 页内锚点：[回到顶部](#cui-markdown-浏览器)\n"
	"2. 本地文件：`notes.md`、`../docs/a.md#章节`\n"
	"3. 远程文件：`https://host/a.md#章节`\n"
	"\n"
	"相对地址按**当前文档地址**解析：打开 `D:\\\\docs\\\\a.md` 后，"
	"`b.md` 指向 `D:\\\\docs\\\\b.md`；打开远程 md 后，"
	"`b.md` 指向同一目录的远程地址。\n"
	"\n"
	"::: tip 前进 / 后退\n"
	"历史项记录「文档 + 首行」，锚点跳转单独记一条 —— 后退回到上一处阅读位置，"
	"而不是文档顶部。\n"
	":::\n"
	"\n"
	"### 表格\n"
	"\n"
	"| 功能 | 状态 | 说明 |\n"
	"|:--|:-:|--:|\n"
	"| 解析 | 完成 | **GFM** 表格 |\n"
	"| 图片 | 完成 | 左 / 中 / 右 |\n"
	"| 目录 | 完成 | 树形缩进 + 折叠 |\n"
	"| 导航 | 完成 | 本地 / 远程 / 锚点 |\n"
	"\n"
	"---\n"
	"\n"
	"滚动、缩放窗口后，行高会按可见行逐个收敛。\n";

static int make_bmp(const char *path, int W, int H)
{
	int stride = (W * 3 + 3) & ~3;
	int size = 54 + stride * H;
	unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY, size);
	BITMAPFILEHEADER *fh;
	BITMAPINFOHEADER *ih;
	HANDLE h;
	DWORD wrote = 0;
	int x, y, ok;

	if (!buf) return 0;
	fh = (BITMAPFILEHEADER *)buf;
	ih = (BITMAPINFOHEADER *)(buf + sizeof(BITMAPFILEHEADER));
	fh->bfType = 0x4D42;
	fh->bfSize = (DWORD)size;
	fh->bfOffBits = 54;
	ih->biSize = 40;
	ih->biWidth = W;
	ih->biHeight = H;
	ih->biPlanes = 1;
	ih->biBitCount = 24;
	ih->biSizeImage = (DWORD)(stride * H);
	for (y = 0; y < H; y++) {
		unsigned char *row = buf + 54 + (H - 1 - y) * stride;
		for (x = 0; x < W; x++) {
			int r = 40 + x * 180 / W;
			int g = 60 + y * 150 / H;
			int b = 200 - x * 120 / W;
			if (((x / 32) + (y / 32)) & 1) { r += 18; g += 12; }
			row[x * 3 + 0] = (unsigned char)b;
			row[x * 3 + 1] = (unsigned char)g;
			row[x * 3 + 2] = (unsigned char)r;
		}
	}
	h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	ok = h != INVALID_HANDLE_VALUE &&
		WriteFile(h, buf, (DWORD)size, &wrote, NULL) && wrote == (DWORD)size;
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	HeapFree(GetProcessHeap(), 0, buf);
	return ok;
}

static void build_sample(App *a)
{
	DynBuf b;
	DWORD n;

	n = GetTempPathA((DWORD)sizeof(a->imgPath), a->imgPath);
	if (n == 0 || n >= sizeof(a->imgPath) - 32)
		lstrcpynA(a->imgPath, "C:\\Windows\\Temp\\", (int)sizeof(a->imgPath));
	lstrcatA(a->imgPath, "cui_md_demo.bmp");
	make_bmp(a->imgPath, 480, 300);
	lstrcpynA(a->inlinePath, a->imgPath, (int)sizeof(a->inlinePath));
	{
		char *base = a->inlinePath;
		char *p = base;
		char *last = base;
		while (*p) {
			if (*p == '/' || *p == '\\') last = p + 1;
			p++;
		}
		lstrcpynA(last, "cui_md_inline.bmp",
			(int)sizeof(a->inlinePath) - (int)(last - base));
	}
	make_bmp(a->inlinePath, 96, 48);
	{
		char *p = a->imgPath;
		while (*p) {
			if (*p == '\\') *p = '/';
			p++;
		}
		p = a->inlinePath;
		while (*p) {
			if (*p == '\\') *p = '/';
			p++;
		}
	}

	ZeroMemory(&b, sizeof(b));
	db_puts(&b, g_intro);
	db_addf(&b, "![左对齐](%s \"left\")\n\n", a->imgPath);
	db_addf(&b, "![居中](%s \"center\")\n\n", a->imgPath);
	db_addf(&b, "![右对齐](%s \"right\")\n\n", a->imgPath);
	db_addf(&b, "![限定尺寸 280x120](%s \"center\"){width=280 height=120}\n\n",
		a->imgPath);
	db_addf(&b, "行内图片跟随文字：这里是一张小图 "
		"![小图](%s){width=24 height=24} ，后面继续正文。\n\n", a->inlinePath);
	db_puts(&b,
		"#### 远程图片\n\n"
		"通过 UiNet / UiHttp 异步下载（需要网络；失败时保留占位高度）：\n\n"
		"![HTTPS 图片](https://httpbin.org/image/png \"center\"){width=200}\n\n");
	db_puts(&b,
		"### 链接\n\n"
		"链接交互：点击 [CUI Markdown 链接](https://example.com/markdown) "
		"会触发回调。\n\n");
	db_puts(&b, g_tail);
	if (a->doc) {
		HeapFree(GetProcessHeap(), 0, a->doc);
		a->doc = NULL;
	}
	a->doc = b.p;
}

/* ── 目录树 ───────────────────────────────────────────────────────── */

static void toc_rebuild(App *a)
{
	int i, n = UiMarkdown_HeadingCount(a->md);
	unsigned char oldCol[256];
	unsigned char oldLv[256];
	int oldN = a->tocN < 256 ? a->tocN : 256;

	/* 折叠状态按"同一下标且层级相同"沿用: 编辑时目录每帧重建,
	 * 不保留的话折叠的节点会自己展开. */
	for (i = 0; i < oldN; i++) {
		oldCol[i] = (unsigned char)a->toc[i].collapsed;
		oldLv[i] = (unsigned char)a->toc[i].level;
	}
	a->tocN = 0;
	if (n > a->tocCap) {
		int nc = a->tocCap ? a->tocCap : 32;
		TocNode *p;
		while (nc < n) nc *= 2;
		/* HeapReAlloc 不接受 NULL, 首次必须 HeapAlloc */
		p = a->toc
			? (TocNode *)HeapReAlloc(GetProcessHeap(), 0, a->toc,
				(SIZE_T)nc * sizeof(TocNode))
			: (TocNode *)HeapAlloc(GetProcessHeap(), 0,
				(SIZE_T)nc * sizeof(TocNode));
		if (!p) return;
		a->toc = p;
		a->tocCap = nc;
	}
	for (i = 0; i < n; i++) {
		const UiMdHeading *h = UiMarkdown_HeadingAt(a->md, i);
		TocNode *p;
		int k, len;
		if (!h) continue;
		p = &a->toc[a->tocN];
		ZeroMemory(p, sizeof(*p));
		p->level = h->level < 1 ? 1 : (h->level > 6 ? 6 : h->level);
		p->row = h->row;
		len = h->textLen;
		if (len > (int)(sizeof(p->text) / sizeof(wchar_t)) - 1)
			len = (int)(sizeof(p->text) / sizeof(wchar_t)) - 1;
		for (k = 0; k < len; k++) p->text[k] = h->text[k];
		p->text[len] = 0;
		p->len = len;
		p->parent = -1;
		for (k = a->tocN - 1; k >= 0; k--) {
			if (a->toc[k].level < p->level) {
				p->parent = k;
				break;
			}
		}
		if (p->parent >= 0) a->toc[p->parent].hasChild = 1;
		if (a->tocN < oldN && oldLv[a->tocN] == (unsigned char)p->level)
			p->collapsed = oldCol[a->tocN];
		a->tocN++;
	}
	a->tocDirty = 0;
}

static int toc_visible(const App *a, int i)
{
	int p = a->toc[i].parent;
	while (p >= 0) {
		if (a->toc[p].collapsed) return 0;
		p = a->toc[p].parent;
	}
	return 1;
}

static int toc_visible_count(const App *a)
{
	int i, n = 0;
	for (i = 0; i < a->tocN; i++)
		if (toc_visible(a, i)) n++;
	return n;
}

static int toc_row_index(const App *a, int row)
{
	int i, k = 0;
	for (i = 0; i < a->tocN; i++) {
		if (!toc_visible(a, i)) continue;
		if (k == row) return i;
		k++;
	}
	return -1;
}

/* ── 历史 ─────────────────────────────────────────────────────────── */

static void hist_clear(App *a)
{
	int i;
	for (i = 0; i < a->histN; i++) {
		if (a->hist[i].loc) HeapFree(GetProcessHeap(), 0, a->hist[i].loc);
		a->hist[i].loc = NULL;
	}
	a->histN = 0;
	a->histPos = -1;
}

static void hist_touch(App *a)
{
	if (a->histPos < 0 || a->histPos >= a->histN) return;
	a->hist[a->histPos].row = UiMarkdown_FirstVisibleRow(a->md);
}

static void hist_push(App *a, const char *loc, int row)
{
	int i;
	if (a->histBusy) return;
	if (a->histPos >= 0 && a->histPos < a->histN && a->hist[a->histPos].loc &&
		loc && lstrcmpA(a->hist[a->histPos].loc, loc) == 0) {
		a->hist[a->histPos].row = row;
		return;
	}
	for (i = a->histPos + 1; i < a->histN; i++)
		if (a->hist[i].loc) HeapFree(GetProcessHeap(), 0, a->hist[i].loc);
	a->histN = a->histPos + 1;
	if (a->histN >= 64) {
		HeapFree(GetProcessHeap(), 0, a->hist[0].loc);
		for (i = 1; i < a->histN; i++) a->hist[i - 1] = a->hist[i];
		a->histN--;
	}
	a->hist[a->histN].loc = u8dup(loc);
	if (!a->hist[a->histN].loc) return;
	a->hist[a->histN].row = row;
	a->histPos = a->histN;
	a->histN++;
}

static void hist_drop_last(App *a)
{
	if (a->histN <= 0) return;
	a->histN--;
	if (a->hist[a->histN].loc)
		HeapFree(GetProcessHeap(), 0, a->hist[a->histN].loc);
	a->hist[a->histN].loc = NULL;
	if (a->histPos >= a->histN) a->histPos = a->histN - 1;
}

/* ── 导航 ─────────────────────────────────────────────────────────── */

static void app_http_cb(void *user, const UiHttpResponse *resp);

static void app_set_doc(App *a, const char *text, const char *url, int row)
{
	if (a->doc) HeapFree(GetProcessHeap(), 0, a->doc);
	a->doc = u8dup(text ? text : "");
	if (!a->doc) return;
	lstrcpynA(a->docUrl, url ? url : "", (int)sizeof(a->docUrl));
	UiMarkdown_SetDocumentUrl(a->md, a->docUrl[0] ? a->docUrl : NULL);
	UiMarkdown_SetText(a->md, a->doc);
	UiTextArea_SetTextUtf8(&a->src, a->doc);
	UiTextArea_SetCaret(&a->src, 0);
	a->tocDirty = 1;
	/* 历史恢复行优先于调用方给的行 */
	if (a->pendingRow < 0) a->pendingRow = row;
}

static void app_load_remote(App *a, const char *url)
{
	UiHttpRequest req;
	if (!a->net) {
		lstrcpynA(a->status, "网络层未初始化", (int)sizeof(a->status));
		return;
	}
	ZeroMemory(&req, sizeof(req));
	req.url = url;
	if (a->reqId) UiHttp_Cancel(a->net, a->reqId);
	lstrcpynA(a->loadUrl, url, (int)sizeof(a->loadUrl));
	a->reqId = UiHttp_Send(a->net, &req, app_http_cb, a);
	a->loading = a->reqId ? 1 : 0;
	if (!a->reqId)
		lstrcpynA(a->status, "请求创建失败", (int)sizeof(a->status));
	else
		wsprintfA(a->status, "加载中… %s", url);
}

static int app_load_local(App *a, const char *path, int row)
{
	wchar_t *wp;
	char *text;
	int len = 0;

	wp = utf8_to_wide(path);
	if (!wp) {
		lstrcpynA(a->status, "路径无效", (int)sizeof(a->status));
		return 0;
	}
	text = load_file_utf8(wp, &len);
	HeapFree(GetProcessHeap(), 0, wp);
	if (!text) {
		wsprintfA(a->status, "打不开文件: %s", path);
		return 0;
	}
	app_set_doc(a, text, path, row);
	HeapFree(GetProcessHeap(), 0, text);
	wsprintfA(a->status, "已载入 %s (%d 字节)", path, len);
	return 1;
}

static void app_http_cb(void *user, const UiHttpResponse *resp)
{
	App *a = (App *)user;
	char *text;
	int len = 0;

	a->reqId = 0;
	a->loading = 0;
	if (!resp || resp->status < 200 || resp->status >= 300) {
		wsprintfA(a->status, "加载失败: %s (HTTP %d)",
			resp ? UiNet_StrError(resp->error) : "无响应",
			resp ? resp->status : 0);
		hist_drop_last(a);
		return;
	}
	text = http_body_utf8(resp, &len);
	if (!text) {
		lstrcpynA(a->status, "响应为空或解码失败", (int)sizeof(a->status));
		hist_drop_last(a);
		return;
	}
	app_set_doc(a, text, a->loadUrl, 0);
	HeapFree(GetProcessHeap(), 0, text);
	wsprintfA(a->status, "已载入 %s (%d 字节)", a->loadUrl, len);
}

/* 把一个链接变成"文档地址 + #锚点". 返回值: 0 = 同文档只滚动,
 * 1 = 需要装载其他文档. */
static int split_loc(const char *loc, char *doc, int docCap,
	char *anchor, int anchorCap)
{
	const char *h = loc;
	int n = 0;

	doc[0] = 0;
	anchor[0] = 0;
	if (!loc) return 0;
	if (loc[0] == '#') {
		lstrcpynA(anchor, loc + 1, anchorCap);
		return 0;
	}
	while (*h && *h != '#') h++;
	n = (int)(h - loc);
	if (n >= docCap) n = docCap - 1;
	memcpy(doc, loc, (size_t)n);
	doc[n] = 0;
	if (*h == '#') lstrcpynA(anchor, h + 1, anchorCap);
	return 1;
}

/* loc: 原始链接 (相对或绝对), 可能带锚点 */
static void app_navigate(UiApp *app, App *a, const char *loc, int pushHist)
{
	char doc[1024], anchor[256];
	char resolved[2048];
	const char *target;
	char full[1200];
	int sameDoc;

	if (!loc || !loc[0]) return;
	if (!split_loc(loc, doc, (int)sizeof(doc), anchor, (int)sizeof(anchor))) {
		/* 纯锚点: 本页跳转 */
		if (anchor[0] && UiMarkdown_ScrollToAnchor(a->md, &app->ui, anchor, 1)) {
			if (pushHist) {
				hist_touch(a);
				wsprintfA(full, "%s#%s", a->docUrl, anchor);
				hist_push(a, full, 0);
			}
			wsprintfA(a->status, "跳到 #%s", anchor);
		} else {
			wsprintfA(a->status, "找不到锚点 #%s", anchor);
		}
		return;
	}
	target = UiMarkdown_ResolveUrl(a->md, doc, resolved, (int)sizeof(resolved));
	sameDoc = a->docUrl[0] && lstrcmpA(target, a->docUrl) == 0;
	if (sameDoc) {
		if (anchor[0]) {
			if (UiMarkdown_ScrollToAnchor(a->md, &app->ui, anchor, 1)) {
				if (pushHist) {
					hist_touch(a);
					wsprintfA(full, "%s#%s", a->docUrl, anchor);
					hist_push(a, full, 0);
				}
				wsprintfA(a->status, "跳到 #%s", anchor);
			} else {
				wsprintfA(a->status, "找不到锚点 #%s", anchor);
			}
		} else {
			UiMarkdown_ScrollToTop(a->md);
		}
		return;
	}
	if (!is_remote(target) && !is_local_abs(target) && !a->docUrl[0]) {
		wsprintfA(a->status, "内置示例没有基准目录，打不开 %s", doc);
		return;
	}
	if (pushHist) {
		hist_touch(a);
		if (anchor[0]) wsprintfA(full, "%s#%s", target, anchor);
		else lstrcpynA(full, target, (int)sizeof(full));
		hist_push(a, full, 0);
	}
	a->pendingAnchor[0] = 0;
	a->pendingRow = -1;
	if (anchor[0])
		lstrcpynA(a->pendingAnchor, anchor, (int)sizeof(a->pendingAnchor));
	if (is_remote(target)) {
		app_load_remote(a, target);
	} else if (!app_load_local(a, target, 0)) {
		a->pendingAnchor[0] = 0;
		if (pushHist) hist_drop_last(a);
	}
}

static void hist_go(UiApp *app, App *a, int delta)
{
	int np = a->histPos + delta;
	if (np < 0 || np >= a->histN) return;
	hist_touch(a);
	a->histPos = np;
	a->histBusy = 1;
	a->pendingRow = a->hist[np].row >= 0 ? a->hist[np].row : 0;
	app_navigate(app, a, a->hist[np].loc, 0);
	a->histBusy = 0;
}

/* ── 编辑器 ───────────────────────────────────────────────────────── */

static void app_editor_pull(UiApp *app, App *a)
{
	int cap;
	char *p;
	float keepOff;

	if (!a->md || !a->src.text) return;
	cap = a->src.len * 4 + 16;
	if (!db_reserve(&a->editBuf, cap)) return;
	UiTextArea_TextUtf8(&a->src, a->editBuf.p, cap);
	if (a->doc && lstrcmpA(a->editBuf.p, a->doc) == 0) return;
	p = u8dup(a->editBuf.p);
	if (!p) return;
	if (a->doc) HeapFree(GetProcessHeap(), 0, a->doc);
	a->doc = p;
	/* SetText 语义是"新文档, 回到顶部"; 编辑时应保留阅读位置 */
	keepOff = UiMarkdown_ScrollOffset(a->md);
	UiMarkdown_SetText(a->md, a->doc);
	UiMarkdown_ScrollToOffset(a->md, &app->ui, keepOff, 0);
	a->tocDirty = 1;
	wsprintfA(a->status, "已同步 %d 字符", a->src.len);
}

static void app_save(UiApp *app, App *a)
{
	wchar_t *wp;
	int ok;
	if (!a->docUrl[0] || is_remote(a->docUrl)) {
		lstrcpynA(a->status, "内置示例/远程文档不能保存，请先「打开文件」",
			(int)sizeof(a->status));
		return;
	}
	(void)app;
	wp = utf8_to_wide(a->docUrl);
	if (!wp) return;
	ok = save_file_utf8(wp, a->doc ? a->doc : "",
		a->doc ? (int)strlen(a->doc) : 0);
	HeapFree(GetProcessHeap(), 0, wp);
	lstrcpynA(a->status, ok ? "已保存" : "保存失败", (int)sizeof(a->status));
}

/* ── 绘制辅助 ─────────────────────────────────────────────────────── */

/* 截断时必须落在 UTF-8 字符边界上, 否则尾部会出现半个汉字 */
static int u8_trim(const char *s, int len)
{
	int i = 0, last = 0;
	while (i < len) {
		unsigned char c = (unsigned char)s[i];
		int n = c < 0x80 ? 1
			: (c >> 5) == 0x06 ? 2
			: (c >> 4) == 0x0E ? 3
			: (c >> 3) == 0x1E ? 4 : 1;
		if (i + n > len) break;
		i += n;
		last = i;
	}
	return last;
}

/* 超宽时右截断 + 省略号 (TOC 用) */
static void draw_text_fit(Gfx *g, GfxFont *f, const char *s, UiRect rc,
	UiColor color)
{
	char buf[512];
	int len;

	if (!s) s = "";
	len = (int)strlen(s);
	if (len >= (int)sizeof(buf) - 4) len = (int)sizeof(buf) - 4;
	memcpy(buf, s, (size_t)len);
	buf[len] = 0;
	while (len > 0 && Gfx_TextWidth(g, f, buf) > rc.w - 10.0f) {
		len--;
		buf[len] = 0;
	}
	if (len < (int)strlen(s)) {
		len = u8_trim(buf, len);
		buf[len] = 0;
		memcpy(buf + len, "\xE2\x80\xA6", 4);   /* … */
	}
	Gfx_Text(g, f, buf, rc, color, GFX_TEXT_VCENTER);
}

/* 超宽时保留尾部 (地址栏用: 文件名比盘符重要) */
static void draw_text_tail(Gfx *g, GfxFont *f, const char *s, UiRect rc,
	UiColor color)
{
	char buf[512];
	int len, start = 0;

	if (!s) s = "";
	len = (int)strlen(s);
	if (len >= (int)sizeof(buf) - 8) {
		s += len - ((int)sizeof(buf) - 8);
		len = (int)sizeof(buf) - 8;
	}
	while (start < len && Gfx_TextWidth(g, f, s + start) > rc.w - 12.0f)
		start++;
	while (start < len && ((unsigned char)s[start] & 0xC0) == 0x80)
		start++;
	if (start > 0) {
		memcpy(buf, "\xE2\x80\xA6", 3);
		memcpy(buf + 3, s + start, (size_t)(len - start));
		buf[3 + len - start] = 0;
	} else {
		memcpy(buf, s, (size_t)len);
		buf[len] = 0;
	}
	Gfx_Text(g, f, buf, rc, color, GFX_TEXT_VCENTER);
}

static void draw_toc(UiApp *app, App *a, UiRect panel)
{
	UiTheme *t = &app->theme;
	UiRect inner, content;
	int visRows, firstRow, lastRow, k, active = -1;
	float contentH, dt, rowH;

	if (panel.w < 40.0f) return;
	Gfx_FillRoundRect(app->gfx, panel, 10.0f, t->card);
	Gfx_StrokeRoundRect(app->gfx, panel, 10.0f, t->border, 1.0f);
	Gfx_Text(app->gfx, t->fSmall, "目录",
		Ui_Rect(panel.x + 12.0f, panel.y + 5.0f, panel.w - 24.0f, 18.0f),
		t->text3, GFX_TEXT_VCENTER);

	if (a->tocDirty) toc_rebuild(a);
	if (a->md) active = UiMarkdown_ActiveHeading(a->md);
	/* 行距先落到设备像素网格: 否则 k*rowH 每行的相位都不同, 滚动时
	 * 相邻行在不同取整阈值切换, 看起来就是行距在闪 (与 TextArea 同法). */
	rowH = Ui_PxSnap(&app->ui, 24.0f);
	visRows = toc_visible_count(a);
	contentH = (float)visRows * rowH + 4.0f;

	inner = Ui_Rect(panel.x + 4.0f, panel.y + 26.0f,
		panel.w - 8.0f, panel.h - 30.0f);
	if (inner.h < 10.0f) return;

	dt = a->tocNow > 0.0 ? (float)(UiFrame_Now() - a->tocNow) : 16.0f;
	a->tocNow = UiFrame_Now();
	if (dt < 0.0f) dt = 0.0f;
	if (dt > 50.0f) dt = 50.0f;
	UiScrollView_Tick(&a->tocSv, &app->ui, dt);

	UiScrollView_Begin(&a->tocSv, &app->ui, inner, inner.w, contentH);
	content = UiScrollView_ContentRect(&a->tocSv);
	/* 整棵树的滚动位移只取整一次: 所有行共享同一整数设备偏移, 行内与
	 * 行间的亚像素相位恒定 (逐行取整会让不同行在不同阈值抖动). */
	content.y = Ui_PxSnap(&app->ui, content.y);

	firstRow = (int)((inner.y - content.y) / rowH);
	lastRow = firstRow + (int)(inner.h / rowH) + 1;
	if (firstRow < 0) firstRow = 0;

	for (k = firstRow; k <= lastRow; k++) {
		int idx = toc_row_index(a, k);
		UiRect rc;
		UiId id;
		UiWidgetState st;
		int indent;

		if (idx < 0) break;
		indent = (a->toc[idx].level - 1) * 12;
		rc = Ui_Rect(content.x + 2.0f + indent, content.y + k * rowH,
			inner.w - 10.0f - indent, rowH);
		if (rc.y + rc.h < inner.y || rc.y > inner.y + inner.h) continue;

		id = Ui_IdIndex("md.toc", 0, 0, idx);
		st = Ui_State(&app->ui, id, rc, 1);
		if (st.hovered) Ui_SetCursor(&app->ui, id, UI_CURSOR_HAND);
		if (idx == active)
			Gfx_FillRoundRect(app->gfx, rc, 5.0f,
				Ui_ColorAlpha(t->primary.base, 26));
		else if (st.hovered)
			Gfx_FillRoundRect(app->gfx, rc, 5.0f, Ui_ColorAlpha(t->text3, 16));

		if (a->toc[idx].hasChild) {
			UiRect ar = Ui_Rect(rc.x + 1.0f, rc.y + 4.0f, 15.0f, 16.0f);
			UiId aid = Ui_IdIndex("md.toc.arw", 0, 0, idx);
			UiWidgetState as = Ui_State(&app->ui, aid, ar, 1);
			if (as.hovered) Ui_SetCursor(&app->ui, aid, UI_CURSOR_HAND);
			Gfx_Text(app->gfx, t->fSmall,
				a->toc[idx].collapsed ? "\xE2\x96\xB8" : "\xE2\x96\xBE",
				ar, t->text3, GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
			if (app->ui.clicked == aid) {
				a->toc[idx].collapsed = !a->toc[idx].collapsed;
				app->ui.clicked = UI_ID_NONE;
			}
			rc.x += 15.0f;
			rc.w -= 15.0f;
		} else if (a->toc[idx].level > 1) {
			Gfx_FillRoundRect(app->gfx,
				Ui_Rect(rc.x + 4.0f, rc.y + rowH * 0.5f - 1.5f, 3.0f, 3.0f),
				1.5f, Ui_ColorAlpha(t->text3, 110));
			rc.x += 15.0f;
			rc.w -= 15.0f;
		}
		rc.x += 6.0f;
		rc.w -= 8.0f;
		{
			char u8[600];
			int ul = WideCharToMultiByte(CP_UTF8, 0, a->toc[idx].text,
				a->toc[idx].len, u8, (int)sizeof(u8) - 1, NULL, NULL);
			if (ul < 0) ul = 0;
			u8[ul] = 0;
			draw_text_fit(app->gfx,
				a->toc[idx].level <= 2 ? t->fBody : t->fSmall, u8, rc,
				idx == active ? t->primary.base : t->text2);
		}
		if (app->ui.clicked == id) {
			UiMarkdown_ScrollToRow(a->md, &app->ui, a->toc[idx].row,
				8.0f, 1);
			app->ui.clicked = UI_ID_NONE;
		}
	}
	UiScrollView_End(&a->tocSv, &app->ui);

	if (a->tocN == 0)
		Gfx_Text(app->gfx, t->fSmall, "（本文档没有标题）",
			Ui_Rect(inner.x + 10.0f, inner.y + 6.0f, inner.w - 20.0f, 20.0f),
			t->text4, GFX_TEXT_VCENTER);
}

/* ── 应用回调 ─────────────────────────────────────────────────────── */

static void open_markdown(UiApp *app, App *a)
{
	wchar_t path[1024];
	OPENFILENAMEW ofn;
	char *u8;

	path[0] = 0;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = app->hwnd;
	ofn.lpstrFilter = L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0"
		L"文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = 1024;
	ofn.lpstrTitle = L"打开 Markdown 文件";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&ofn)) return;

	u8 = wide_to_utf8(path);
	if (!u8) return;
	hist_touch(a);
	if (app_load_local(a, u8, 0)) {
		char full[1100];
		lstrcpynA(full, u8, (int)sizeof(full));
		hist_push(a, full, 0);
	}
	HeapFree(GetProcessHeap(), 0, u8);
}

/* 文件拖放: 把 .md 丢进窗口就打开, 与「打开」对话框走同一条路径
 * (本地加载 + 记一条历史). 多个文件时只处理第一个. */
static int on_dropfiles(UiApp *app, const wchar_t *path, void *user)
{
	App *a = (App *)user;
	char *u8;

	(void)app;
	if (!path || !path[0]) return 0;
	u8 = wide_to_utf8(path);
	if (!u8) return 0;
	hist_touch(a);
	if (app_load_local(a, u8, 0)) {
		char full[1100];
		lstrcpynA(full, u8, (int)sizeof(full));
		hist_push(a, full, 0);
	}
	HeapFree(GetProcessHeap(), 0, u8);
	return 1;
}

static void on_init(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiScrollConfig scfg;
	UiScrollbarStyle sbar;

	app->minW = 900.0f;
	build_sample(a);
	{
		UiNetConfig cfg;
		ZeroMemory(&cfg, sizeof(cfg));
		a->net = UiNet_Create(&cfg);
	}
	a->md = UiMarkdown_Create(app->gfx, Ui_Id("md.doc", 0, 0), a->net);
	if (a->md) {
		UiMarkdown_SetText(a->md, a->doc);
		UiMarkdown_SetDocumentUrl(a->md, NULL);
	}
	UiTextArea_Init(&a->src, Ui_Id("md.src", 0, 0), 0);
	UiTextArea_SetPlaceholder(&a->src, L"在此编辑 Markdown 源码，中间预览实时更新…");
	UiTextArea_AttachIme(&a->src, &app->ime);
	if (a->doc) UiTextArea_SetTextUtf8(&a->src, a->doc);
	UiTextArea_SetCaret(&a->src, 0);

	scfg = UiScrollConfig_Desktop();
	sbar = UiScrollbarStyle_Default();
	UiScrollView_Init(&a->tocSv, Ui_Id("md.tocsv", 0, 0), &scfg, &sbar);

	a->editorOpen = 0;
	a->tocOpen = 1;
	a->histPos = -1;
	a->pendingRow = -1;
	a->tocDirty = 1;   /* 首帧就建目录, 否则要等第一次换文档 */
	a->topRc = Ui_Rect(0, 0, 0, 0);
	{
		int i;
		for (i = 0; i < TIP_N; i++)
			UiTooltip_Init(&a->tips[i], Ui_Id("md.tip", i, 0));
		a->tipsReady = 1;
	}
	lstrcpynA(a->status, "内置示例 ｜ 可拖放 .md 到窗口, 或点「打开」", 
		(int)sizeof(a->status));
	/* 窗口/任务栏图标来自 exe 的资源 ID 1 (res\cui_markdown.rc),
	 * UiApp 注册窗口类时已载入, 这里不用再 WM_SETICON. */
}

/* ── 工具栏布局 ───────────────────────────────────────────────────── */

/* 唯一布局来源: 绘制与 WM_NCHITTEST (拖动/双击最大化) 共用同一组矩形,
 * 否则两边各自累加坐标迟早会漂移. 左侧是产品名, 右侧从关闭按钮往左排.
 * 左半: 标题 | 后退 前进 | 打开 保存 | 地址栏; 右半: 目录 编辑 主题 关闭. */
static void toolbar_rects(float w, float titleW, ToolbarRects *r)
{
	float cx, sx;

	r->title = Ui_Rect(16.0f, TB_TOP, titleW, TB_H);
	cx = r->title.x + titleW + (titleW > 0.0f ? 18.0f : 0.0f);
	r->back = Ui_Rect(cx, TB_TOP, TB_IW, TB_H); cx += TB_IW + TB_G4;
	r->fwd = Ui_Rect(cx, TB_TOP, TB_IW, TB_H); cx += TB_IW + TB_G12;
	sx = cx - TB_G12 * 0.5f - 0.5f;             /* 第一条分组线 */
	r->sep1 = Ui_Rect(sx, TB_TOP + 7.0f, 1.0f, TB_H - 14.0f);
	r->open = Ui_Rect(cx, TB_TOP, TB_IW, TB_H); cx += TB_IW + TB_G4;
	r->save = Ui_Rect(cx, TB_TOP, TB_IW, TB_H); cx += TB_IW + TB_G12;
	sx = cx - TB_G12 * 0.5f - 0.5f;             /* 第二条分组线 */
	r->sep2 = Ui_Rect(sx, TB_TOP + 7.0f, 1.0f, TB_H - 14.0f);
	r->close = Ui_Rect(w - 16.0f - 38.0f, TB_TOP, 38.0f, TB_H);
	r->theme = Ui_Rect(r->close.x - TB_GAP - TB_IW, TB_TOP, TB_IW, TB_H);
	r->edit = Ui_Rect(r->theme.x - TB_G4 - TB_IW, TB_TOP, TB_IW, TB_H);
	r->toc = Ui_Rect(r->edit.x - TB_G4 - TB_IW, TB_TOP, TB_IW, TB_H);
	r->bar = Ui_Rect(cx, TB_TOP, r->toc.x - TB_GAP - cx, TB_H);
}

/* 工具栏图标按钮: 有图标字体时纯图标 (TEXT 外观, 打开态用 primary 着色),
 * 没有图标字体时退回文字按钮 —— 否则整个工具栏会变成一排空白方块. */
static int tb_button(UiContext *ui, UiTheme *t, UiId id, UiRect rc,
	uint32_t cp, const char *fallback, int active, int enabled)
{
	UiButtonStyle st = UiButtonStyle_Make(
		active ? UI_BTYPE_PRIMARY : UI_BTYPE_DEFAULT,
		UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
	UiIcon ic;

	if (!t->fIcon) return Ui_ButtonEx(ui, id, rc, fallback, &st, enabled);
	ic = UiIcon_Glyph(t->fIcon, cp);
	return Ui_IconButton(ui, id, rc, &ic, &st, enabled);
}

static const char *g_title = "CUI Markdown Reader";

/* 工具栏这条空白带当标题栏: 拖动移动窗口, 双击走系统最大化/还原.
 * 落在按钮/地址栏上返回 0, 交回客户区给控件自己处理 (标题只是文字,
 * 所以留在可拖区里). */
static int on_nchittest(UiApp *app, int px, int py, LRESULT *hit, void *user)
{
	App *a = (App *)user;
	const ToolbarRects *r = &a->tb;
	float x, y;

	if (!app || !a || app->scale <= 0.0f) return 0;
	x = (float)px / app->scale;
	y = (float)py / app->scale;
	if (y < 0.0f || y > TB_TOP + TB_H + TB_GAP) return 0;
	if (Ui_RectContains(r->back, x, y) || Ui_RectContains(r->fwd, x, y) ||
		Ui_RectContains(r->open, x, y) || Ui_RectContains(r->save, x, y) ||
		Ui_RectContains(r->bar, x, y) || Ui_RectContains(r->toc, x, y) ||
		Ui_RectContains(r->edit, x, y) || Ui_RectContains(r->theme, x, y) ||
		Ui_RectContains(r->close, x, y))
		return 0;
	/* 指针进了非客户区, 控件 hover 不会再收到移动消息, 手动清掉 */
	Ui_OnMouseLeave(&app->ui);
	*hit = HTCAPTION;
	return 1;
}

static void on_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiTheme *t = &app->theme;
	float x = 16.0f, y = 14.0f, w = app->w - 32.0f;
	UiRect tocArea, viewArea, editArea;
	const char *clicked;
	char clickedCopy[1024];
	float titleW = a->tb.title.w;

	/* 标题宽度只在首帧/DPI 变化时量: Gfx_TextWidth 要建一次 layout,
	 * 每帧都量等于把这个字符串排版两遍. */
	if (titleW <= 0.0f || a->titleScale != app->scale) {
		titleW = Gfx_TextWidth(app->gfx, t->fHead, g_title);
		a->titleScale = app->scale;
	}

	/* 最大化时窗口铺满工作区, 圆角会把页底色漏到四角 */
	Gfx_FillRoundRectGradV(app->gfx, Ui_Rect(0, 0, app->w, app->h),
		IsZoomed(app->hwnd) ? 0.0f : 14.0f, t->page, t->page2);
	(void)x; (void)y; (void)w;

	/* ── 工具栏 ── */
	{
		UiId cid = Ui_Id("md.close", 0, 0);
		UiId idBack = Ui_Id("md.back", 0, 0);
		UiId idFwd = Ui_Id("md.fwd", 0, 0);
		UiId idOpen = Ui_Id("md.open", 0, 0);
		UiId idSave = Ui_Id("md.save", 0, 0);
		UiId idToc = Ui_Id("md.toc", 0, 0);
		UiId idEdit = Ui_Id("md.edit", 0, 0);
		UiId idTheme = Ui_Id("md.theme", 0, 0);
		ToolbarRects *tb = &a->tb;
		double now = UiFrame_Now();
		int dark = t->themeId == UI_THEME_DARK;

		toolbar_rects(app->w, titleW, tb);
		Gfx_Text(app->gfx, t->fHead, g_title, tb->title, t->text,
			GFX_TEXT_VCENTER);

		/* 分组细线: 导航 | 文件 | 地址栏. 只是视觉分隔, 仍属可拖区(不出现在
		 * nchittest 的排除列表里). */
		Gfx_Line(app->gfx, tb->sep1.x + 0.5f, tb->sep1.y,
			tb->sep1.x + 0.5f, tb->sep1.y + tb->sep1.h, t->border, 1.0f);
		Gfx_Line(app->gfx, tb->sep2.x + 0.5f, tb->sep2.y,
			tb->sep2.x + 0.5f, tb->sep2.y + tb->sep2.h, t->border, 1.0f);

		if (tb_button(&app->ui, t, idBack, tb->back, 0xE72Bu, "后退", 0,
			a->histPos > 0))
			hist_go(app, a, -1);
		if (tb_button(&app->ui, t, idFwd, tb->fwd, 0xE72Au, "前进", 0,
			a->histPos >= 0 && a->histPos + 1 < a->histN))
			hist_go(app, a, 1);
		if (tb_button(&app->ui, t, idOpen, tb->open, 0xE8E5u, "打开", 0, 1))
			a->wantOpen = 1;
		if (tb_button(&app->ui, t, idSave, tb->save, 0xE74Eu, "保存", 0,
			a->docUrl[0] && !is_remote(a->docUrl)))
			a->wantSave = 1;

		/* 地址栏 */
		if (tb->bar.w > 80.0f) {
			UiColor bg = t->inputBg ? t->inputBg : t->controlBox;
			Gfx_FillRoundRect(app->gfx, tb->bar, 8.0f, bg);
			Gfx_StrokeRoundRect(app->gfx, tb->bar, 8.0f,
				t->inputBorder ? t->inputBorder : t->border, 1.0f);
			draw_text_tail(app->gfx, t->fSmall,
				a->docUrl[0] ? a->docUrl : "内置示例  (markdown)",
				Ui_Rect(tb->bar.x + 10.0f, tb->bar.y, tb->bar.w - 20.0f,
					tb->bar.h),
				a->docUrl[0] ? t->text2 : t->text3);
		}

		if (tb_button(&app->ui, t, idToc, tb->toc, 0xE8FDu, "目录",
			a->tocOpen, 1))
			a->tocOpen = !a->tocOpen;
		if (tb_button(&app->ui, t, idEdit, tb->edit, 0xE70Fu, "编辑",
			a->editorOpen, 1)) {
			a->editorOpen = !a->editorOpen;
			if (a->editorOpen) {
				if (a->tocOpen) a->tocOpen = 0;
				Ui_SetFocusId(&app->ui, a->src.id);
			}
		}
		/* 主题按钮显示"要切到的目标": 深色时给太阳, 浅色时给月亮 */
		if (tb_button(&app->ui, t, idTheme, tb->theme,
			dark ? 0xE706u : 0xE708u, dark ? "浅色" : "深色", 0, 1))
			UiTheme_SetTheme(t, dark ? UI_THEME_LIGHT : UI_THEME_DARK);

		/* 关闭: 自绘 ✕, hover 渐入标准关闭红 (与其它按钮同一行) */
		{
			UiWidgetState cs = Ui_State(&app->ui, cid, tb->close, 1);
			float mx = tb->close.x + tb->close.w * 0.5f;
			float my = tb->close.y + tb->close.h * 0.5f;
			float d = 5.0f;
			UiColor xc = Ui_ColorMix(t->text3, 0xFFFFFFFF, cs.hover);

			if (cs.hover > 0.01f)
				Gfx_FillRoundRect(app->gfx, tb->close, 8.0f,
					Ui_ColorAlpha(0xFFE81123,
						(int)(cs.hover * 232.0f)));
			Gfx_Line(app->gfx, mx - d, my - d, mx + d, my + d, xc, 1.5f);
			Gfx_Line(app->gfx, mx - d, my + d, mx + d, my - d, xc, 1.5f);
			if (cs.hovered) Ui_SetCursor(&app->ui, cid, UI_CURSOR_HAND);
			if (app->ui.clicked == cid) UiApp_Close(app);
		}

		/* 悬停提示: 必须在控件注册之后调用 (读 ui->hover) */
		UiTooltip_Update(&a->tips[TIP_BACK], &app->ui, idBack, tb->back,
			"后退  Alt+←", now);
		UiTooltip_Update(&a->tips[TIP_FWD], &app->ui, idFwd, tb->fwd,
			"前进  Alt+→", now);
		UiTooltip_Update(&a->tips[TIP_OPEN], &app->ui, idOpen, tb->open,
			"打开本地 .md", now);
		UiTooltip_Update(&a->tips[TIP_SAVE], &app->ui, idSave, tb->save,
			"保存  Ctrl+S", now);
		UiTooltip_Update(&a->tips[TIP_TOC], &app->ui, idToc, tb->toc,
			a->tocOpen ? "收起目录" : "标题目录", now);
		UiTooltip_Update(&a->tips[TIP_EDIT], &app->ui, idEdit, tb->edit,
			a->editorOpen ? "收起源码" : "源码编辑", now);
		UiTooltip_Update(&a->tips[TIP_THEME], &app->ui, idTheme, tb->theme,
			dark ? "切换到浅色" : "切换到深色", now);
		UiTooltip_Update(&a->tips[TIP_CLOSE], &app->ui, cid, tb->close,
			"关闭", now);
	}

	/* ── 三栏 ── */
	{
		float gap = 12.0f;
		float contentY = TB_TOP + TB_H + 12.0f;
		float contentH = app->h - contentY - 30.0f;
		float tocW = a->tocOpen ? 224.0f : 0.0f;
		float editW = a->editorOpen ? 400.0f : 0.0f;
		float viewX = 16.0f + (tocW > 0.0f ? tocW + gap : 0.0f);
		float viewW = app->w - 16.0f - viewX - (editW > 0.0f ? editW + gap : 0.0f);

		if (viewW < 200.0f) {
			editW = 0.0f;
			viewW = app->w - 16.0f - viewX;
		}
		tocArea = Ui_Rect(16.0f, contentY, tocW, contentH);
		viewArea = Ui_Rect(viewX, contentY, viewW, contentH);
		editArea = Ui_Rect(app->w - 16.0f - editW, contentY, editW, contentH);

		/* 编辑器先画: 本帧输入立即反映到预览 */
		if (editArea.w > 1.0f) {
			UiRect panel = editArea;
			Gfx_FillRoundRect(app->gfx, panel, 10.0f, t->card);
			Gfx_StrokeRoundRect(app->gfx, panel, 10.0f, t->border, 1.0f);
			panel.x += 6.0f; panel.y += 6.0f;
			panel.w -= 12.0f; panel.h -= 12.0f;
			if (Ui_TextArea(&a->src, &app->ui, panel, 1))
				app_editor_pull(app, a);
		}

		if (a->tocOpen && tocArea.w > 1.0f) draw_toc(app, a, tocArea);

		if (a->md) {
			/* 文档刚换好: 应用待跳转位置 */
			if (a->pendingAnchor[0]) {
				if (UiMarkdown_ScrollToAnchor(a->md, &app->ui,
					a->pendingAnchor, 0))
					a->pendingRow = -1;
				a->pendingAnchor[0] = 0;
			}
			if (a->pendingRow >= 0) {
				UiMarkdown_ScrollToRow(a->md, &app->ui, a->pendingRow,
					8.0f, 0);
				a->pendingRow = -1;
			}
			Ui_Markdown(a->md, &app->ui, viewArea);

			/* 链接点击: 复制后立刻处理 (SetText 会释放旧文档) */
			clicked = UiMarkdown_ClickedUrl(a->md);
			clickedCopy[0] = 0;
			if (clicked && clicked[0])
				lstrcpynA(clickedCopy, clicked, (int)sizeof(clickedCopy));
			if (clickedCopy[0])
				app_navigate(app, a, clickedCopy, 1);

			/* ── 回到顶部: 浮在文档右下角, 滚过一段才淡入 ──
			 * 用两条动画通道: 透明度 + 8 DIP 上滑, 出现/消失都不突兀;
			 * Ui_AnimBusy 会替我们保持帧循环, 不用自己记时间. */
			{
				UiId tid = Ui_Id("md.top", 0, 0);
				float off = UiMarkdown_ScrollOffset(a->md);
				int want = off > 160.0f;
				float k, slide, sz, rad;
				UiRect rc;
				UiWidgetState st;

				Ui_AnimSmooth(&app->ui, tid, UI_AN_ALPHA,
					want ? 1.0f : 0.0f);
				Ui_AnimSmooth(&app->ui, tid, UI_AN_OFFSET,
					want ? 0.0f : 10.0f);
				k = Ui_AnimGet(&app->ui, tid, UI_AN_ALPHA, 0.0f);
				slide = Ui_AnimGet(&app->ui, tid, UI_AN_OFFSET, 10.0f);
				a->topRc = Ui_Rect(0, 0, 0, 0);
				if (k > 0.02f) {
					sz = 38.0f;
					rad = sz * 0.5f;
					rc = Ui_Rect(viewArea.x + viewArea.w - 14.0f - sz - 8.0f,
						viewArea.y + viewArea.h - 14.0f - sz + slide,
						sz, sz);
					a->topRc = rc;
					/* 比文档视图高一层: 命中按 z 取最大, 否则点不到 */
					Ui_PushLayer(&app->ui, UI_Z_BASE + 50);
					st = Ui_State(&app->ui, tid, rc, 1);
					{
						UiColor bg = Ui_ColorMix(t->card,
							t->primary.soft, st.hover);
						UiColor bd = Ui_ColorMix(t->border,
							t->primary.base, st.hover * 0.7f);
						UiColor fg = Ui_ColorMix(t->text2,
							t->primary.base, st.hover);
					int a255 = k > 0.85f ? 255
						: (int)(k * 255.0f);
						int sh1 = (int)(k * 26.0f);
						int sh2 = (int)(k * 44.0f);

						/* 两圈低透明度底当"影": Gfx_DrawGlowRect 的
						 * 柔光纹理会蒙住按钮内部, 不能用在这里 */
						Gfx_FillRoundRect(app->gfx,
							Ui_Rect(rc.x - 6, rc.y - 6,
								rc.w + 12, rc.h + 12),
							rad + 6,
							Ui_ColorAlpha(0xFF10162E, sh1));
						Gfx_FillRoundRect(app->gfx,
							Ui_Rect(rc.x - 2, rc.y - 2,
								rc.w + 4, rc.h + 4),
							rad + 2,
							Ui_ColorAlpha(0xFF10162E, sh2));
						Gfx_FillRoundRect(app->gfx, rc, rad,
							Ui_ColorAlpha(bg, a255));
						Gfx_StrokeRoundRect(app->gfx, rc, rad,
							Ui_ColorAlpha(bd, a255), 1.0f);
						if (t->fIcon) {
							UiIcon up = UiIcon_Glyph(t->fIcon, 0xE74Au);
							UiIcon_Draw(&app->ui, &up,
								Ui_Rect(rc.x + rad - 8.0f,
									rc.y + rad - 8.0f, 16.0f, 16.0f),
								Ui_ColorAlpha(fg, a255), 1);
						} else {
							Gfx_Text(app->gfx, t->fBtn, "↑",
								Ui_Rect(rc.x, rc.y, rc.w, rc.h),
								Ui_ColorAlpha(fg, a255),
								GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
						}
					}
					if (st.hovered)
						Ui_SetCursor(&app->ui, tid, UI_CURSOR_HAND);
					if (app->ui.clicked == tid)
						UiMarkdown_ScrollToOffset(a->md, &app->ui,
							0.0f, 1);
					Ui_PopLayer(&app->ui);
				}
			}
		}
	}

	/* ── 状态栏 ── */
	{
		char line[420];
		wsprintfA(line, "%s%s ｜ 历史 %d/%d%s",
			a->status,
			a->editorOpen ? " ｜ 源码编辑" : "",
			a->histPos + 1, a->histN,
			a->loading ? " ｜ 加载中" : "");
		draw_text_fit(app->gfx, t->fSmall, line,
			Ui_Rect(18.0f, app->h - 24.0f, app->w - 36.0f, 18.0f), t->hint);
	}

	/* ── 悬停提示: 最上层, 必须在所有控件注册之后 ── */
	{
		int i;

		/* 按钮消失时也要 update 一次, 否则提示会停在最后一帧不关 */
		UiTooltip_Update(&a->tips[TIP_TOP], &app->ui, Ui_Id("md.top", 0, 0),
			a->topRc, "回到顶部", UiFrame_Now());
		for (i = 0; i < TIP_N; i++)
			UiTooltip_Draw(&a->tips[i], &app->ui);
	}
}

static int on_animating(UiApp *app, void *user)
{
	App *a = (App *)user;
	int i;

	(void)app;
	for (i = 0; i < TIP_N; i++)
		if (UiTooltip_Pending(&a->tips[i])) return 1;   /* 等 delay 到期 */
	return (a->md ? UiMarkdown_Busy(a->md) : 0) || a->loading ||
		(a->editorOpen ? UiTextArea_Busy(&a->src) : 0);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	App *a = (App *)user;
	int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

	if (ctrl && vk == 'S') {
		a->wantSave = 1;
		return 1;
	}
	if (alt && vk == VK_LEFT) {
		hist_go(app, a, -1);
		return 1;
	}
	if (alt && vk == VK_RIGHT) {
		hist_go(app, a, 1);
		return 1;
	}
	if (vk == VK_F5) {
		char loc[1200];
		lstrcpynA(loc, a->docUrl[0] ? a->docUrl : "#", (int)sizeof(loc));
		if (a->docUrl[0]) app_navigate(app, a, loc, 0);
		return 1;
	}
	if (!a->editorOpen) return 0;
	return UiTextArea_Key(&a->src, &app->ui, vk);
}

static int on_chr(UiApp *app, wchar_t ch, void *user)
{
	App *a = (App *)user;
	if (!a->editorOpen) return 0;
	return UiTextArea_Char(&a->src, &app->ui, ch);
}

static void on_ime(UiApp *app, void *user)
{
	App *a = (App *)user;
	if (a->editorOpen) UiTextArea_OnIme(&a->src, &app->ui);
}

static void on_timer(UiApp *app, void *user)
{
	App *a = (App *)user;
	if (a->editorOpen) UiTextArea_Blink(&a->src, &app->ui);
}

/* 模态对话框必须在 Ui_EndFrame 之后打开: 对话框的消息循环会把 WM_PAINT
 * 重新派发回主窗口, 与 clicked 状态重入. */
static void on_after_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	if (a->wantSave) {
		a->wantSave = 0;
		app_save(app, a);
	}
	if (a->wantOpen) {
		a->wantOpen = 0;
		open_markdown(app, a);
	}
}

static void on_shutdown(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	if (a->reqId && a->net) {
		UiHttp_Cancel(a->net, a->reqId);
		a->reqId = 0;
	}
	hist_clear(a);
	if (a->md) {
		UiMarkdown_Destroy(a->md);
		a->md = NULL;
	}
	if (a->net) {
		UiNet_Destroy(a->net);
		a->net = NULL;
	}
	UiTextArea_Shutdown(&a->src);
	db_free(&a->editBuf);
	if (a->toc) {
		HeapFree(GetProcessHeap(), 0, a->toc);
		a->toc = NULL;
	}
	if (a->doc) {
		HeapFree(GetProcessHeap(), 0, a->doc);
		a->doc = NULL;
	}
}

void __stdcall startup(void)
{
	App app;
	static const UiAppCallbacks cb = {
		on_init, on_frame, on_after_frame, on_shutdown, on_animating,
		NULL, NULL, NULL, on_key, on_chr, on_ime, on_timer, on_nchittest,
		on_dropfiles
	};
	ZeroMemory(&app, sizeof(app));
	app.pendingRow = -1;
	ExitProcess((UINT)UiApp_Run(L"CUI Markdown Browser", 1180, 760,
		UI_THEME_LIGHT, &cb, &app));
}
