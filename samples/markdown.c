/*
 * markdown.c - UiMarkdown 样例
 *
 * 展示标题/段落/列表/引用/代码/任务列表、真实表格、真实图片（左/中/右对齐）。
 * 按钮：浅/深主题切换、打开本地 Markdown 文件（动态读取，UTF-8/UTF-16 BOM）、
 * 追加 50 行、回到顶部。
 *
 * 构建: bin\cui_markdown.exe (仅此目标链接 gfx_rich/ui_md_parse/ui_md/ui_image/comdlg32)
 */
#include <windows.h>
#include <commdlg.h>
#include <stdarg.h>
#include "ui/ui_app.h"
#include "ui/ui_frame.h"
#include "ui/ui_md.h"
#include "ui/ui_textarea.h"
#include "ui/ui_widgets.h"

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
size_t strlen(const char *s);
void *memcpy(void *d, const void *s, size_t n);

/* 动态文本缓冲: 内容不设上限; 空指针时 HeapAlloc, 避免 HeapReAlloc(NULL) */
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

typedef struct App {
	UiMarkdown *md;
	UiTextArea src;        /* Markdown 源码编辑器 */
	int appended;
	int fromFile;
	int wantOpen;
	int editorOpen;        /* 默认收起 */
	float editorW;         /* 当前展开宽度 (动画) */
	float lastTaOff, lastMdOff;
	int wantSyncFromEditor;
	int pendingOpenSync;   /* 展开时以预览位置为准映射源码视口 */
	DynBuf editBuf;
	char status[288];
	char lastLink[192];
	char *doc;
	char imgPath[512];
	char inlinePath[512];
} App;

static const char *g_intro =
	"# CUI Markdown 渲染\n"
	"\n"
	"一个**应用持有**的只读 Markdown 组件：解析用自研 0CRT 解析器，"
	"排版交给 DirectWrite，滚动复用 UiScrollView。\n"
	"\n"
	"## 特性\n"
	"\n"
	"- 行模型：一个叶子块 = 一个滚动项\n"
	"- 懒测量：首帧只做高度估算，滚动时才实测可见行\n"
	"- 增量：`Append` 保留已实测高度，尾部按需收敛\n"
	"- 行内代码：`code`、``a`b``、`**不解析**` 与 `[非链接](x)`\n"
	"- 图片尺寸：`![alt](url){width=280 height=120}`，只给一个则等比\n"
	"- 主题：默认跟随全局浅/深主题\n"
	"\n"
	"> 引用块用于强调：性能与文档规模解耦，单帧成本只与视口相关。\n"
	"\n"
	"### 代码\n"
	"\n"
	"```c\n"
	"UiMarkdown *md = UiMarkdown_Create(gfx, id, net);\n"
	"UiMarkdown_SetText(md, text);\n"
	"Ui_Markdown(md, ui, rc);\n"
	"```\n"
	"\n"
	"### 任务列表\n"
	"\n"
	"- [x] 解析器 + 金样测试\n"
	"- [x] 行模型与懒测量\n"
	"- [x] 图片/表格真实渲染\n"
	"- [ ] 链接 / 代码复制\n"
	"\n"
	"### 图片对齐（title 提示）\n"
	"\n";

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
	db_addf(&b, "### 行内图片与链接\n\n");
	db_addf(&b, "行内图片跟随文字：这里是一张小图 ![小图](%s){width=24 height=24} ，后面继续正文。\n\n",
		a->inlinePath);
	db_puts(&b,
		"链接交互：点击 [CUI Markdown 链接](https://example.com/markdown) 会触发回调。\n\n"
		"### 容器\n\n"
		"::: tip 小提示\n"
		"支持 `::: note/tip/warning/danger` 容器，正文按普通块递归解析。\n"
		":::\n\n"
		"::: warning\n"
		"警告容器：左侧色条 + 标题 + 低饱和底色，颜色随主题切换。\n"
		":::\n\n"
		"> [!NOTE]\n"
		"> GitHub 风格警报与 `:::` 容器共用同一渲染器。\n\n"
		":::: danger 嵌套示例\n"
		"::: tip\n"
		"内部容器可以嵌套。\n"
		":::\n"
		"::::\n\n"
		"### 语法高亮\n\n"
		"```c\n"
		"// 注释\n"
		"int main(void) {\n"
		"    const char *s = \"hello\";\n"
		"    return 42;\n"
		"}\n"
		"```\n\n");
	db_puts(&b,
		"### 数学公式\n"
		"\n"
		"行内公式 $E = mc^2$ 与 $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ 跟随文字排版。\n"
		"\n"
		"超长行内公式按可用宽自动缩小："
		"$\\sum_{k=0}^{n} \\frac{k^{2}+2k+1}{k+1} + \\sum_{k=0}^{n} \\frac{k^{2}-2k+1}{k-1} + \\int_{0}^{1} \\frac{x^{3}+1}{x+1}\\,dx + \\int_{0}^{1} \\frac{x^{3}-1}{x-1}\\,dx$"
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
		"### 真实表格\n"
		"\n"
		"| 功能 | 状态 | 说明 |\n"
		"|:--|:-:|--:|\n"
		"| 解析 | 完成 | **GFM** 表格 |\n"
		"| 图片 | 完成 | 左 / 中 / 右 |\n"
		"| 表格 | 完成 | 列宽 + 边框 |\n"
		"| 代码 | 完成 | `inline` |\n"
		"\n"
		"---\n"
		"\n"
		"滚动、缩放窗口后，行高会按可见行逐个收敛；已实测高度在 `Append` 后保留。\n");
	if (a->doc) {
		HeapFree(GetProcessHeap(), 0, a->doc);
		a->doc = NULL;
	}
	a->doc = b.p;
}

static void append_lines(App *a)
{
	DynBuf b, nd;
	int i;
	ZeroMemory(&b, sizeof(b));
	for (i = 0; i < 50; i++)
		db_addf(&b, "追加段落 %d：这是流式追加的正文，用于观察滚动位置与懒测量收敛。\n\n",
			a->appended + i + 1);
	a->appended += 50;
	if (b.n > 0 && a->md) {
		/* 文档是唯一真源: 追加后同步给编辑器与预览 */
		ZeroMemory(&nd, sizeof(nd));
		db_puts(&nd, a->doc ? a->doc : "");
		db_puts(&nd, b.p);
		if (nd.p) {
			if (a->doc) HeapFree(GetProcessHeap(), 0, a->doc);
			a->doc = nd.p;
		}
		UiMarkdown_SetFollowTail(a->md, 0);
		UiMarkdown_Append(a->md, b.p);
		UiTextArea_SetTextUtf8(&a->src, a->doc);
		UiTextArea_SetCaret(&a->src, a->src.len);
	}
	db_free(&b);
}

static char *load_file_utf8(const wchar_t *path, int *outLen)
{
	HANDLE h;
	LARGE_INTEGER li;
	DWORD size, got = 0;
	BYTE *raw;
	char *out = NULL;
	int n = 0;

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

static void open_markdown(UiApp *app, App *a)
{
	wchar_t path[1024];
	OPENFILENAMEW ofn;
	char *text;
	int len = 0;

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

	text = load_file_utf8(path, &len);
	if (!text) {
		lstrcpynA(a->status, "文件读取失败 (为空或超过 32MB)",
			(int)sizeof(a->status));
		return;
	}
	UiMarkdown_SetText(a->md, text);
	UiMarkdown_ScrollToTop(a->md);
	UiTextArea_SetTextUtf8(&a->src, text);
	UiTextArea_SetCaret(&a->src, 0);
	a->fromFile = 1;
	wsprintfA(a->status, "已载入 %d 字节的 Markdown 文件", len);
	HeapFree(GetProcessHeap(), 0, text);
}

static void on_link(void *user, const char *url)
{
	App *a = (App *)user;
	lstrcpynA(a->lastLink, url ? url : "(null)", (int)sizeof(a->lastLink));
}

static float absf_(float v) { return v < 0.0f ? -v : v; }

/* 编辑器文本 -> 文档真源 -> 预览, 保持"输入即更新" */
static void app_editor_pull(App *a)
{
	int cap, n;
	char *p;
	if (!a->md || !a->src.text) return;
	cap = a->src.len * 4 + 16;
	if (!db_reserve(&a->editBuf, cap)) return;
	UiTextArea_TextUtf8(&a->src, a->editBuf.p, cap);
	if (a->doc && lstrcmpA(a->editBuf.p, a->doc) == 0) return;
	n = (int)strlen(a->editBuf.p) + 1;
	p = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n);
	if (!p) return;
	memcpy(p, a->editBuf.p, (size_t)n);
	if (a->doc) HeapFree(GetProcessHeap(), 0, a->doc);
	a->doc = p;
	UiMarkdown_SetText(a->md, a->doc);
	a->wantSyncFromEditor = 1;
	wsprintfA(a->status, "已同步 %d 字符", a->src.len);
}

/* 编辑/预览滚动联动: 谁在本帧移动, 就把另一侧按比例映射过去 */
static void app_sync_scroll(UiApp *app, App *a)
{
	float ta, md, taMax, mdMax;
	int taMoved, mdMoved;
	if (!a->md) return;
	ta = a->src.sv.offset;
	md = UiMarkdown_ScrollOffset(a->md);
	if (!a->editorOpen || a->editorW < 1.0f) {
		a->lastTaOff = ta;
		a->lastMdOff = md;
		return;
	}
	taMax = UiScrollView_MaxOffset(&a->src.sv);
	mdMax = UiMarkdown_MaxOffset(a->md);
	if (taMax <= 0.5f || mdMax <= 0.5f) goto done;
	if (a->pendingOpenSync) {
		float ratio = md > 0.0f ? md / mdMax : 0.0f;
		int line;
		if (ratio < 0.0f) ratio = 0.0f;
		if (ratio > 1.0f) ratio = 1.0f;
		UiScrollView_ScrollTo(&a->src.sv, &app->ui, ratio * taMax, 0);
		line = (int)(ratio * (float)(a->src.lineCount - 1) + 0.5f);
		if (line < 0) line = 0;
		if (line >= a->src.lineCount) line = a->src.lineCount - 1;
		if (a->src.lineStart) UiTextArea_SetCaret(&a->src,
			a->src.lineStart[line]);
		a->pendingOpenSync = 0;
		goto done;
	}
	taMoved = absf_(ta - a->lastTaOff) > 0.6f;
	mdMoved = absf_(md - a->lastMdOff) > 0.6f;
	if (a->wantSyncFromEditor) {
		UiMarkdown_ScrollToOffset(a->md, &app->ui,
			(ta / taMax) * mdMax, 0);
		a->wantSyncFromEditor = 0;
	} else if (taMoved && !mdMoved) {
		UiMarkdown_ScrollToOffset(a->md, &app->ui,
			(ta / taMax) * mdMax, 0);
	} else if (mdMoved && !taMoved) {
		UiScrollView_ScrollTo(&a->src.sv, &app->ui,
			(md / mdMax) * taMax, 0);
	}
done:
	a->lastTaOff = a->src.sv.offset;
	a->lastMdOff = UiMarkdown_ScrollOffset(a->md);
}

static void on_init(UiApp *app, void *user)
{
	App *a = (App *)user;
	app->minW = 820.0f;
	build_sample(a);
	a->md = UiMarkdown_Create(app->gfx, Ui_Id("md.doc", 0, 0), NULL);
	if (a->md) {
		UiMarkdown_SetLinkFn(a->md, on_link, a);
		UiMarkdown_SetText(a->md, a->doc);
		UiMarkdown_SetFollowTail(a->md, 0);
	}
	UiTextArea_Init(&a->src, Ui_Id("md.src", 0, 0), 0);
	UiTextArea_SetPlaceholder(&a->src, L"在此编辑 Markdown 源码，右侧预览实时更新…");
	UiTextArea_AttachIme(&a->src, &app->ime);
	if (a->doc) UiTextArea_SetTextUtf8(&a->src, a->doc);
	UiTextArea_SetCaret(&a->src, 0);
	a->editorOpen = 0;   /* 默认收起 */
	a->editorW = 0.0f;
	lstrcpynA(a->status, "点击「编辑源码」展开；输入实时同步预览",
		(int)sizeof(a->status));
}

static void on_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	UiTheme *t = &app->theme;
	float x = 24.0f, y = 20.0f, w = app->w - 48.0f;
	float btnW = 118.0f, btnH = 32.0f, gap = 16.0f;
	UiRect area, srcArea;
	UiId idAppend = Ui_Id("md.append", 0, 0);

	Gfx_FillRoundRectGradV(app->gfx, Ui_Rect(0, 0, app->w, app->h),
		14.0f, t->page, t->page2);

	Gfx_Text(app->gfx, t->fHead, "Markdown",
		Ui_Rect(x, y, 200, 30), t->text, GFX_TEXT_VCENTER);
	y += 42.0f;

	area = Ui_Rect(x, y, w, app->h - y - 20.0f);
	srcArea = Ui_Rect(0, 0, 0, 0);
	a->editorW = a->editorOpen ? 380.0f : 0.0f;
	if (a->editorW > w - 260.0f) a->editorW = w - 260.0f;
	if (a->editorW > 1.0f) {
		area.w = w - a->editorW - gap;
		if (area.w < 200.0f) area.w = 200.0f;
		srcArea = Ui_Rect(area.x + area.w + gap, y, a->editorW, area.h);
		/* 编辑器先画: 本帧输入立即反映到右侧预览 */
		if (srcArea.w > 1.0f &&
			Ui_TextArea(&a->src, &app->ui, srcArea, 1))
			app_editor_pull(a);
		Gfx_FillRect(app->gfx, Ui_Rect(area.x + area.w + gap * 0.5f,
			area.y, 1.0f, area.h), t->border);
	}
	if (a->md) {
		Ui_Markdown(a->md, &app->ui, area);
		wsprintfA(a->status, "%s ｜ %s",
			t->themeId == UI_THEME_DARK ? "深色" : "浅色",
			a->editorOpen ? "源码编辑 ｜ 实时预览 ｜ 滚动联动"
				: "点击「编辑源码」展开");
		if (a->lastLink[0]) {
			lstrcatA(a->status, " ｜ 链接: ");
			lstrcatA(a->status, a->lastLink);
		}
	} else {
		lstrcpynA(a->status, "UiMarkdown 创建失败", (int)sizeof(a->status));
	}

	{
		float xAppend = app->w - 24.0f - btnW;
		float xTop = xAppend - 8.0f - 96.0f;
		float xOpen = xTop - 8.0f - 96.0f;
		float xTheme = xOpen - 8.0f - 88.0f;
		float xEdit = xTheme - 8.0f - 96.0f;
		if (a->md && Ui_Button(&app->ui, Ui_Id("md.edit", 0, 0),
			Ui_Rect(xEdit, 18.0f, 96.0f, btnH),
			a->editorOpen ? "收起源码" : "编辑源码",
			UI_BTN_DEFAULT, 1)) {
			a->editorOpen = !a->editorOpen;
			if (a->editorOpen) {
				a->pendingOpenSync = 1;
				Ui_SetFocusId(&app->ui, a->src.id);
			}
		}
		if (a->md && Ui_Button(&app->ui, Ui_Id("md.theme", 0, 0),
			Ui_Rect(xTheme, 18.0f, 88.0f, btnH),
			t->themeId == UI_THEME_DARK ? "浅色主题" : "深色主题",
			UI_BTN_DEFAULT, 1))
			UiTheme_SetTheme(t, t->themeId == UI_THEME_DARK
				? UI_THEME_LIGHT : UI_THEME_DARK);
		if (a->md && Ui_Button(&app->ui, Ui_Id("md.open", 0, 0),
			Ui_Rect(xOpen, 18.0f, 96.0f, btnH),
			"打开文件", UI_BTN_DEFAULT, 1))
			a->wantOpen = 1;   /* 帧结束后再弹模态对话框, 避免重入 */
		if (a->md && Ui_Button(&app->ui, Ui_Id("md.top", 0, 0),
			Ui_Rect(xTop, 18.0f, 96.0f, btnH),
			"回到顶部", UI_BTN_DEFAULT, 1))
			UiMarkdown_ScrollToTop(a->md);
		if (a->md && Ui_Button(&app->ui, idAppend,
			Ui_Rect(xAppend, 18.0f, btnW, btnH),
			"追加 50 行", UI_BTN_PRIMARY, 1))
			append_lines(a);
	}

	Gfx_Text(app->gfx, t->fSmall, a->status,
		Ui_Rect(24.0f, app->h - 18.0f, app->w - 48.0f, 16.0f),
		t->hint, GFX_TEXT_VCENTER);

	app_sync_scroll(app, a);
}

static int on_animating(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	return (a->md ? UiMarkdown_Busy(a->md) : 0) ||
		(a->editorOpen ? UiTextArea_Busy(&a->src) : 0);
}

static int on_key(UiApp *app, UINT vk, void *user)
{
	App *a = (App *)user;
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

/* 模态文件对话框必须在 Ui_EndFrame 之后打开: 对话框的消息循环会把
 * WM_PAINT 重新派发回主窗口, 若在 on_frame 内弹框, 同一帧的 clicked
 * 状态尚未清除, 会反复重入本按钮并无限创建对话框. */
static void on_after_frame(UiApp *app, void *user)
{
	App *a = (App *)user;
	if (a->wantOpen) {
		a->wantOpen = 0;
		open_markdown(app, a);
	}
}

static void on_shutdown(UiApp *app, void *user)
{
	App *a = (App *)user;
	(void)app;
	if (a->md) {
		UiMarkdown_Destroy(a->md);
		a->md = NULL;
	}
	UiTextArea_Shutdown(&a->src);
	db_free(&a->editBuf);
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
		NULL, NULL, NULL, on_key, on_chr, on_ime, on_timer, NULL
	};
	ZeroMemory(&app, sizeof(app));
	ExitProcess((UINT)UiApp_Run(L"CUI Markdown", 1100, 720,
		UI_THEME_LIGHT, &cb, &app));
}
