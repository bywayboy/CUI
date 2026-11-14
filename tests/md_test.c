/*
 * md_test.c - Markdown 解析器 headless 金样断言 (0CRT 控制台)
 *
 * 覆盖: 标题/段落/代码/引用/列表/表格/行内/链接引用/图片/哈希/健壮性。
 * 构建: bin\cui_md_test.exe; 退出码 0=全通过。
 */
#include <windows.h>
#include <wchar.h>
#include "ui/ui_md_parse.h"
#include "log.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);

static int g_pass, g_fail;

static void out_(const char *s)
{
	DWORD n = 0;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &n, NULL);
}

#define CHECK(c) do { \
	if (c) g_pass++; \
	else { \
		char b_[320]; \
		g_fail++; \
		sprintf(b_, "FAIL line %d: %s\n", __LINE__, #c); \
		out_(b_); \
	} \
} while (0)

static int wstr_eq(const wchar_t *a, const wchar_t *b)
{
	if (!a || !b) return a == b;
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

static int parse_(MdDoc *d, const char *src)
{
	int rc;
	MdDoc_Init(d);
	rc = Md_Parse(d, src, (int)strlen(src));
	return rc == 0;
}

static uint16_t style_at(const MdBlock *b, int pos)
{
	int i;
	if (!b) return 0;
	for (i = 0; i < b->runCount; i++) {
		const MdRun *r = &b->runs[i];
		if (pos >= r->start && pos < r->start + r->len) return r->style;
	}
	return 0;
}

static int block_has_style(const MdBlock *b, uint16_t mask)
{
	int i;
	for (i = 0; i < b->runCount; i++)
		if (b->runs[i].style & mask) return 1;
	return 0;
}

static int block_all_style(const MdBlock *b, uint16_t mask)
{
	int i;
	if (b->len16 <= 0 || b->runCount <= 0) return 0;
	for (i = 0; i < b->runCount; i++)
		if ((b->runs[i].style & mask) != mask) return 0;
	return 1;
}

static const MdBlock *child(const MdBlock *b, int i)
{
	if (!b || i < 0 || i >= b->childCount) return NULL;
	return &b->children[i];
}

static void test_headings(void)
{
	MdDoc d;
	CHECK(parse_(&d, "# H1"));
	if (d.count == 1) {
		CHECK(d.blocks[0].type == MD_BLOCK_HEADING);
		CHECK(d.blocks[0].level == 1);
		CHECK(wstr_eq(d.blocks[0].text16, L"H1"));
		CHECK(d.blocks[0].runCount == 1);
		CHECK(d.blocks[0].runs[0].style == 0);
	}
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "###### H6"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HEADING);
	if (d.count == 1) {
		CHECK(d.blocks[0].level == 6);
		CHECK(wstr_eq(d.blocks[0].text16, L"H6"));
	}
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "####### x"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(d.blocks[0].text16, L"####### x"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "#nospace"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "## Close ##"));
	CHECK(d.count == 1 && wstr_eq(d.blocks[0].text16, L"Close"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "Title\n==="));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HEADING);
	CHECK(d.blocks[0].level == 1 && wstr_eq(d.blocks[0].text16, L"Title"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "Sub\n---"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HEADING);
	CHECK(d.blocks[0].level == 2 && wstr_eq(d.blocks[0].text16, L"Sub"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "---"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HR);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "* * *"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HR);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "___"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HR);
	MdDoc_Shutdown(&d);
}

static void test_paragraphs(void)
{
	MdDoc d;
	CHECK(parse_(&d, "a\nb"));
	CHECK(d.count == 1 && wstr_eq(d.blocks[0].text16, L"a b"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a  \nb"));
	CHECK(d.count == 1 && wstr_eq(d.blocks[0].text16, L"a\nb"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a\\\nb"));
	CHECK(d.count == 1 && wstr_eq(d.blocks[0].text16, L"a\nb"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "   lead   "));
	CHECK(d.count == 1 && wstr_eq(d.blocks[0].text16, L"lead"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a\n\nb"));
	CHECK(d.count == 2 && wstr_eq(d.blocks[0].text16, L"a") &&
		wstr_eq(d.blocks[1].text16, L"b"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a\n# h"));
	CHECK(d.count == 2);
	CHECK(d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(d.blocks[1].type == MD_BLOCK_HEADING);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a\n> q"));
	CHECK(d.count == 2 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(d.blocks[1].type == MD_BLOCK_QUOTE);
	MdDoc_Shutdown(&d);
}

static void test_code(void)
{
	MdDoc d;
	CHECK(parse_(&d, "```c\nint x;\n```"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CODE);
	CHECK((d.blocks[0].flags & MD_BF_FENCE_CLOSED) != 0);
	CHECK(d.blocks[0].fence == 1);
	CHECK(wstr_eq(d.blocks[0].text16, L"int x;"));
	CHECK(d.blocks[0].langLen == 1 && MdDoc_Url(&d, d.blocks[0].langOff)[0] == 'c');
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "~~~\nx\n~~~"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CODE);
	CHECK(wstr_eq(d.blocks[0].text16, L"x"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "```\na\n"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CODE);
	CHECK((d.blocks[0].flags & MD_BF_FENCE_CLOSED) == 0);
	CHECK(wstr_eq(d.blocks[0].text16, L"a"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "    code"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CODE);
	CHECK(d.blocks[0].fence == 0);
	CHECK(wstr_eq(d.blocks[0].text16, L"code"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "```\n\tif\n```"));
	CHECK(wstr_eq(d.blocks[0].text16, L"    if"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "```\n```"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CODE);
	CHECK(d.blocks[0].len16 == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a\n\n```\nb\nc\n```"));
	CHECK(d.count == 2 && d.blocks[1].type == MD_BLOCK_CODE);
	CHECK(wstr_eq(d.blocks[1].text16, L"b\nc"));
	MdDoc_Shutdown(&d);
}

static void test_quote(void)
{
	MdDoc d;
	CHECK(parse_(&d, "> a\n> b"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_QUOTE);
	CHECK(d.blocks[0].childCount == 1);
	CHECK(wstr_eq(child(&d.blocks[0], 0)->text16, L"a b"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "> > a"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_QUOTE);
	CHECK(d.blocks[0].childCount == 1);
	CHECK(child(&d.blocks[0], 0)->type == MD_BLOCK_QUOTE);
	CHECK(wstr_eq(child(child(&d.blocks[0], 0), 0)->text16, L"a"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "> # h"));
	CHECK(d.count == 1 && d.blocks[0].childCount == 1);
	CHECK(child(&d.blocks[0], 0)->type == MD_BLOCK_HEADING);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "> ```\n> a\n> ```"));
	CHECK(d.count == 1 && d.blocks[0].childCount == 1);
	CHECK(child(&d.blocks[0], 0)->type == MD_BLOCK_CODE);
	CHECK(wstr_eq(child(&d.blocks[0], 0)->text16, L"a"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "> a\n\nb"));
	CHECK(d.count == 2 && d.blocks[0].type == MD_BLOCK_QUOTE);
	CHECK(d.blocks[1].type == MD_BLOCK_PARAGRAPH);
	MdDoc_Shutdown(&d);
}

static void test_containers(void)
{
	MdDoc d;
	CHECK(parse_(&d, "::: tip 小提示\n正文\n:::"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CONTAINER);
	CHECK(d.blocks[0].boxKind == MD_BOX_TIP);
	CHECK(d.blocks[0].childCount == 1);
	CHECK(child(&d.blocks[0], 0)->type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(child(&d.blocks[0], 0)->text16, L"正文"));
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].titleOff), "小提示") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "::: warning\n**x**\n:::"));
	CHECK(d.blocks[0].type == MD_BLOCK_CONTAINER);
	CHECK(d.blocks[0].boxKind == MD_BOX_WARNING);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].titleOff), "警告") == 0);
	MdDoc_Shutdown(&d);

	/* 未识别首词: 整段 info 作为标题, kind=note */
	CHECK(parse_(&d, "::: 自定义 标题\nx\n:::"));
	CHECK(d.blocks[0].boxKind == MD_BOX_NOTE);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].titleOff), "自定义 标题") == 0);
	MdDoc_Shutdown(&d);

	/* 未闭合: 回退为段落字面量 */
	CHECK(parse_(&d, "::: tip\nx"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(d.blocks[0].text16, L"::: tip x"));
	MdDoc_Shutdown(&d);

	/* 嵌套: 外层 4 冒号, 内层 3 冒号 */
	CHECK(parse_(&d, ":::: note\n::: tip\nx\n:::\n::::"));
	CHECK(d.blocks[0].type == MD_BLOCK_CONTAINER);
	CHECK(child(&d.blocks[0], 0)->type == MD_BLOCK_CONTAINER);
	CHECK(child(&d.blocks[0], 0)->boxKind == MD_BOX_TIP);
	CHECK(wstr_eq(child(child(&d.blocks[0], 0), 0)->text16, L"x"));
	MdDoc_Shutdown(&d);

	/* GitHub 警报 */
	CHECK(parse_(&d, "> [!NOTE]\n> 内容"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CONTAINER);
	CHECK(d.blocks[0].boxKind == MD_BOX_NOTE);
	CHECK(d.blocks[0].childCount == 1);
	CHECK(wstr_eq(child(&d.blocks[0], 0)->text16, L"内容"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "> [!warning]\n> 注意安全"));
	CHECK(d.blocks[0].type == MD_BLOCK_CONTAINER);
	CHECK(d.blocks[0].boxKind == MD_BOX_WARNING);
	MdDoc_Shutdown(&d);

	/* 未识别 marker: 保持引用原样 */
	CHECK(parse_(&d, "> [!FOO]\n> x"));
	CHECK(d.blocks[0].type == MD_BLOCK_QUOTE);
	CHECK(wstr_eq(child(&d.blocks[0], 0)->text16, L"[!FOO] x"));
	MdDoc_Shutdown(&d);

	/* 标记后同行为正文 */
	CHECK(parse_(&d, "> [!TIP] 后面正文"));
	CHECK(d.blocks[0].type == MD_BLOCK_CONTAINER);
	CHECK(wstr_eq(child(&d.blocks[0], 0)->text16, L"后面正文"));
	MdDoc_Shutdown(&d);
}

static void test_lists(void)
{
	MdDoc d;
	CHECK(parse_(&d, "- a\n- b"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_LIST);
	CHECK(d.blocks[0].childCount == 2);
	CHECK(d.blocks[0].tight == 1);
	CHECK(d.blocks[0].ordered == 0);
	CHECK(wstr_eq(child(child(&d.blocks[0], 0), 0)->text16, L"a"));
	CHECK(wstr_eq(child(child(&d.blocks[0], 1), 0)->text16, L"b"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "1. a\n2. b"));
	CHECK(d.count == 1 && d.blocks[0].ordered == 1);
	CHECK(d.blocks[0].startNo == 1);
	CHECK(d.blocks[0].childCount == 2);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "3. a"));
	CHECK(d.count == 1 && d.blocks[0].startNo == 3);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "- a\n  - b"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_LIST);
	CHECK(MdDoc_CountType(&d, MD_BLOCK_LIST) == 2);
	CHECK(MdDoc_CountType(&d, MD_BLOCK_ITEM) == 2);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "- a\n\n- b"));
	CHECK(d.count == 1 && d.blocks[0].tight == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "- [ ] t\n- [x] d"));
	CHECK(d.count == 1 && d.blocks[0].childCount == 2);
	CHECK(child(&d.blocks[0], 0)->task == MD_TASK_TODO);
	CHECK(child(&d.blocks[0], 1)->task == MD_TASK_DONE);
	CHECK(wstr_eq(child(child(&d.blocks[0], 0), 0)->text16, L"t"));
	CHECK(wstr_eq(child(child(&d.blocks[0], 1), 0)->text16, L"d"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a\n- b"));
	CHECK(d.count == 2 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(d.blocks[1].type == MD_BLOCK_LIST);
	MdDoc_Shutdown(&d);
}

static void test_tables(void)
{
	MdDoc d;
	CHECK(parse_(&d, "| A | B |\n|:--|--:|\n| 1 | 2 |"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_TABLE);
	CHECK(d.blocks[0].cols == 2 && d.blocks[0].rows == 2);
	CHECK(d.blocks[0].align[0] == MD_ALIGN_LEFT);
	CHECK(d.blocks[0].align[1] == MD_ALIGN_RIGHT);
	CHECK(wstr_eq(d.blocks[0].cells[0].text16, L"A"));
	CHECK(wstr_eq(d.blocks[0].cells[1].text16, L"B"));
	CHECK(wstr_eq(d.blocks[0].cells[2].text16, L"1"));
	CHECK(wstr_eq(d.blocks[0].cells[3].text16, L"2"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "A|B\n:-:|---\n1|2"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_TABLE);
	CHECK(d.blocks[0].cols == 2 && d.blocks[0].align[0] == MD_ALIGN_CENTER);
	CHECK(d.blocks[0].align[1] == MD_ALIGN_LEFT);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "| a\\|b | c |\n|---|---|\n| 1 | 2 |"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_TABLE);
	CHECK(wstr_eq(d.blocks[0].cells[0].text16, L"a|b"));
	CHECK(wstr_eq(d.blocks[0].cells[1].text16, L"c"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "| A | B |\n|---|---|\n| 1 |"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_TABLE);
	CHECK(d.blocks[0].rows == 2 && d.blocks[0].cols == 2);
	CHECK(d.blocks[0].cells[3].len16 == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "| A | B |\n|---|\n| 1 | 2 |"));
	CHECK(d.count == 1 && d.blocks[0].type != MD_BLOCK_TABLE);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "| **b** |\n|---|\n| x |"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_TABLE);
	CHECK(wstr_eq(d.blocks[0].cells[0].text16, L"b"));
	CHECK(d.blocks[0].cells[0].runCount == 1 &&
		(d.blocks[0].cells[0].runs[0].style & MD_R_BOLD) != 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "| A |\n|---|"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_TABLE);
	CHECK(d.blocks[0].rows == 1 && d.blocks[0].cols == 1);
	MdDoc_Shutdown(&d);
}

static void test_inline(void)
{
	MdDoc d;
	CHECK(parse_(&d, "**a**"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a"));
	CHECK(block_all_style(&d.blocks[0], MD_R_BOLD));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "*a*"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a"));
	CHECK(block_all_style(&d.blocks[0], MD_R_ITALIC));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "__a__"));
	CHECK(block_all_style(&d.blocks[0], MD_R_BOLD));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "***a***"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a"));
	CHECK(block_all_style(&d.blocks[0], MD_R_BOLD | MD_R_ITALIC));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "**a *b* c**"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a b c"));
	CHECK(block_all_style(&d.blocks[0], MD_R_BOLD));
	CHECK((style_at(&d.blocks[0], 2) & (MD_R_BOLD | MD_R_ITALIC)) ==
		(MD_R_BOLD | MD_R_ITALIC));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a*b*c"));
	CHECK(wstr_eq(d.blocks[0].text16, L"abc"));
	CHECK(block_has_style(&d.blocks[0], MD_R_ITALIC));
	CHECK(!(style_at(&d.blocks[0], 0) & MD_R_ITALIC));
	CHECK(style_at(&d.blocks[0], 1) & MD_R_ITALIC);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a_b_c"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a_b_c"));
	CHECK(!block_has_style(&d.blocks[0], MD_R_BOLD | MD_R_ITALIC));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "*a"));
	CHECK(wstr_eq(d.blocks[0].text16, L"*a"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "`a*b*`"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a*b*"));
	CHECK(block_all_style(&d.blocks[0], MD_R_CODE));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "\\*a\\*"));
	CHECK(wstr_eq(d.blocks[0].text16, L"*a*"));
	CHECK(!block_has_style(&d.blocks[0], MD_R_ITALIC));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a &amp; b"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a & b"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "&#65;&#x42;"));
	CHECK(wstr_eq(d.blocks[0].text16, L"AB"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "~~x~~"));
	CHECK(wstr_eq(d.blocks[0].text16, L"x"));
	CHECK(block_all_style(&d.blocks[0], MD_R_STRIKE));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "~x~"));
	CHECK(wstr_eq(d.blocks[0].text16, L"~x~"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "**a**b**c**"));
	CHECK(wstr_eq(d.blocks[0].text16, L"abc"));
	CHECK(style_at(&d.blocks[0], 0) & MD_R_BOLD);
	CHECK(!(style_at(&d.blocks[0], 1) & MD_R_BOLD));
	CHECK(style_at(&d.blocks[0], 2) & MD_R_BOLD);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "*a **b** c*"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a b c"));
	CHECK(block_all_style(&d.blocks[0], MD_R_ITALIC));
	CHECK(style_at(&d.blocks[0], 2) & MD_R_BOLD);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "` a `"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a"));
	MdDoc_Shutdown(&d);
}

static void test_links_refs(void)
{
	MdDoc d;
	CHECK(parse_(&d, "[t](u)"));
	CHECK(wstr_eq(d.blocks[0].text16, L"t"));
	CHECK(block_has_style(&d.blocks[0], MD_R_LINK));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "u") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[t](u \"T\")"));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "u") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[**b**](u)"));
	CHECK(wstr_eq(d.blocks[0].text16, L"b"));
	CHECK(style_at(&d.blocks[0], 0) == (MD_R_BOLD | MD_R_LINK));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[t][r]\n\n[r]: http://x"));
	CHECK(d.count == 1 && d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "http://x") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[t][]\n\n[t]: /u"));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "/u") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[t]\n\n[t]: /u"));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "/u") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[T]\n\n[t]: /u"));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "/u") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[a  b]\n\n[a b]: /u"));
	CHECK(d.blocks[0].linkCount == 1);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[t]\n\n[t]: /a\n[t]: /b"));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "/a") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "<https://a.b/c>"));
	CHECK(wstr_eq(d.blocks[0].text16, L"https://a.b/c"));
	CHECK(block_all_style(&d.blocks[0], MD_R_LINK));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff),
		"https://a.b/c") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "<a@b.com>"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a@b.com"));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff),
		"mailto:a@b.com") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[](u)"));
	CHECK(d.blocks[0].linkCount == 1);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "[x](<u v>)"));
	CHECK(d.blocks[0].linkCount == 1);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].links[0].urlOff), "u v") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a [b](c) d"));
	CHECK(wstr_eq(d.blocks[0].text16, L"a b d"));
	CHECK(d.blocks[0].linkCount == 1);
	if (d.blocks[0].linkCount == 1) {
		const MdLink *l = &d.blocks[0].links[0];
		CHECK(l->start >= 0 && l->end > l->start);
		if (l->start >= 0 && l->end > l->start) {
			const MdRun *r0 = &d.blocks[0].runs[l->start];
			CHECK(r0->start == 2 && r0->len == 1);
		}
	}
	MdDoc_Shutdown(&d);
}

static void test_images(void)
{
	MdDoc d;
	CHECK(parse_(&d, "![alt](u)"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(wstr_eq(d.blocks[0].text16, L"alt"));
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].urlOff), "u") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![a][r]\n\n[r]: u2"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(wstr_eq(d.blocks[0].text16, L"a"));
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].urlOff), "u2") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![alt](u) text"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(d.blocks[0].text16, L"alt text"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "before ![alt](u) after"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(d.blocks[0].text16, L"before alt after"));
	CHECK(d.blocks[0].imgCount == 1);
	CHECK(d.blocks[0].imgs && d.blocks[0].imgs[0].start == 7 &&
		d.blocks[0].imgs[0].len == 3);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].imgs[0].urlOff), "u") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![a](u \"left\")"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgAlign == MD_ALIGN_LEFT);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![a](u \"center\")"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgAlign == MD_ALIGN_CENTER);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![a](u \"right\")"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgAlign == MD_ALIGN_RIGHT);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![a](u \"title\")"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgAlign == 255);
	MdDoc_Shutdown(&d);

	/* 尺寸属性 (Pandoc/Markdown-Extra): 块级/行内, width/height 可省略 */
	CHECK(parse_(&d, "![a](u){width=120 height=80}"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgW == 120 && d.blocks[0].imgH == 80);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![a](u \"left\"){height=40}"));
	CHECK(d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgW == 0 && d.blocks[0].imgH == 40);
	CHECK(d.blocks[0].imgAlign == MD_ALIGN_LEFT);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![a](u){ Width = 33 }"));
	CHECK(d.blocks[0].imgW == 33 && d.blocks[0].imgH == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "![尺寸 280x120](u \"center\"){width=280 height=120}"));
	CHECK(d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgW == 280 && d.blocks[0].imgH == 120);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "x ![a](u){w=20 h=10} y"));
	CHECK(d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(d.blocks[0].text16, L"x a y"));
	CHECK(d.blocks[0].imgCount == 1);
	CHECK(d.blocks[0].imgs[0].w == 20 && d.blocks[0].imgs[0].h == 10);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "x ![a](u \"center\"){width=280 height=120} y"));
	CHECK(d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(d.blocks[0].text16, L"x a y"));
	CHECK(d.blocks[0].imgCount == 1);
	CHECK(d.blocks[0].imgs[0].w == 280 && d.blocks[0].imgs[0].h == 120);
	MdDoc_Shutdown(&d);

	/* 未知属性被忽略, 不影响图片本身 */
	CHECK(parse_(&d, "![a](u){foo=1}"));
	CHECK(d.blocks[0].type == MD_BLOCK_IMAGE);
	CHECK(d.blocks[0].imgW == 0 && d.blocks[0].imgH == 0);
	MdDoc_Shutdown(&d);
}

static void test_hash(void)
{
	MdDoc a, b;
	CHECK(parse_(&a, "# h\n\npara\n"));
	CHECK(parse_(&b, "# h\n\npara\n"));
	CHECK(a.count == b.count);
	CHECK(a.blocks[0].hash == b.blocks[0].hash);
	CHECK(a.blocks[1].hash == b.blocks[1].hash);
	MdDoc_Shutdown(&b);
	MdDoc_Shutdown(&a);

	CHECK(parse_(&a, "# h\n\na"));
	CHECK(parse_(&b, "# h\n\nb"));
	CHECK(a.blocks[0].hash == b.blocks[0].hash);
	CHECK(a.blocks[1].hash != b.blocks[1].hash);
	MdDoc_Shutdown(&b);
	MdDoc_Shutdown(&a);

	CHECK(parse_(&a, "# h"));
	CHECK(a.blocks[0].hash != 0);
	MdDoc_Shutdown(&a);
}

static void test_math(void)
{
	MdDoc d;

	CHECK(parse_(&d, "a $x^2 + y$ b"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	{
		const MdBlock *b = &d.blocks[0];
		CHECK(b->mathCount == 1);
		CHECK(wstr_eq(b->text16, L"a x^2 + y b"));
		if (b->mathCount == 1) {
			CHECK(b->maths[0].start == 2 && b->maths[0].len == 7);
			CHECK(b->maths[0].srcLen == 7);
			CHECK(strcmp(MdDoc_Url(&d, b->maths[0].srcOff), "x^2 + y") == 0);
		}
	}
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a \\(b+c\\) d"));
	CHECK(d.count == 1);
	{
		const MdBlock *b = &d.blocks[0];
		CHECK(b->mathCount == 1 && wstr_eq(b->text16, L"a b+c d"));
		if (b->mathCount == 1)
			CHECK(strcmp(MdDoc_Url(&d, b->maths[0].srcOff), "b+c") == 0);
	}
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "# $e=mc^2$"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HEADING);
	CHECK(d.blocks[0].mathCount == 1);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "costs $5 and $6"));
	{
		const MdBlock *b = &d.blocks[0];
		CHECK(b->mathCount == 0);
		CHECK(wstr_eq(b->text16, L"costs $5 and $6"));
	}
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "esc \\$5 and \\$6"));
	CHECK(d.blocks[0].mathCount == 0);
	CHECK(wstr_eq(d.blocks[0].text16, L"esc $5 and $6"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "code `$x$` end"));
	CHECK(d.blocks[0].mathCount == 0);
	CHECK(block_has_style(&d.blocks[0], MD_R_CODE));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "$$a^2+b^2$$"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_MATH);
	CHECK(d.blocks[0].mathLen == 7);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].mathOff), "a^2+b^2") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "$$\n\\frac{a}{b}\n$$"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_MATH);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].mathOff), "\\frac{a}{b}") == 0);
	CHECK(d.srcLen > 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "\\[\nx=1\n\\]"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_MATH);
	CHECK(strcmp(MdDoc_Url(&d, d.blocks[0].mathOff), "x=1") == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "before\n$$\nx=1\n$$\nafter"));
	CHECK(d.count == 3);
	CHECK(d.blocks[1].type == MD_BLOCK_MATH);
	CHECK(d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(d.blocks[2].type == MD_BLOCK_PARAGRAPH);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "$$unclosed"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	CHECK(wstr_eq(d.blocks[0].text16, L"$$unclosed"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "```\n$x$\n$$y$$\n```"));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_CODE);
	CHECK(d.blocks[0].mathCount == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "x $a$ y"));
	{
		uint32_t h1 = d.blocks[0].hash;
		MdDoc_Shutdown(&d);
		CHECK(parse_(&d, "x $b$ y"));
		CHECK(d.blocks[0].hash != h1);
	}
	MdDoc_Shutdown(&d);
}

static void test_robust(void)
{
	MdDoc d;
	char big[512];
	int i;

	CHECK(parse_(&d, ""));
	CHECK(d.count == 0);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "   \n\t\n"));
	CHECK(d.count == 0);
	MdDoc_Shutdown(&d);

	ZeroMemory(big, sizeof(big));
	for (i = 0; i < 100; i++) big[i] = '>';
	big[100] = ' ';
	big[101] = 'a';
	CHECK(parse_(&d, big));
	CHECK(d.count >= 1);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "\xFF" "bad"));
	CHECK(d.count == 1);
	CHECK(d.blocks[0].type == MD_BLOCK_PARAGRAPH);
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "# a\r\n\r\nb"));
	CHECK(d.count == 2);
	CHECK(d.blocks[0].type == MD_BLOCK_HEADING);
	CHECK(wstr_eq(d.blocks[0].text16, L"a"));
	CHECK(wstr_eq(d.blocks[1].text16, L"b"));
	MdDoc_Shutdown(&d);

	CHECK(parse_(&d, "a\n="));
	CHECK(d.count == 1 && d.blocks[0].type == MD_BLOCK_HEADING);
	CHECK(d.blocks[0].level == 1);
	MdDoc_Shutdown(&d);

	{
		char *wide = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4096);
		int n = 0;
		if (wide) {
			wide[n++] = '|';
			for (i = 0; i < 80; i++) {
				wide[n++] = ' ';
				wide[n++] = 'c';
				wide[n++] = ' ';
				wide[n++] = '|';
			}
			wide[n++] = '\n';
			wide[n++] = '|';
			for (i = 0; i < 80; i++) { wide[n++] = '-'; wide[n++] = '|'; }
			wide[n] = 0;
			CHECK(parse_(&d, wide));
			if (d.count == 1 && d.blocks[0].type == MD_BLOCK_TABLE)
				CHECK(d.blocks[0].cols <= 64);
			else
				CHECK(1);
			MdDoc_Shutdown(&d);
			HeapFree(GetProcessHeap(), 0, wide);
		}
	}
}

void __stdcall startup(void)
{
	char line[160];

	Log_Init();
	test_headings();
	test_paragraphs();
	test_code();
	test_quote();
	test_containers();
	test_lists();
	test_tables();
	test_inline();
	test_links_refs();
	test_images();
	test_math();
	test_hash();
	test_robust();

	sprintf(line, "md_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
