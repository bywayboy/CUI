/*
 * md_highlight_test.c - Markdown 内置语法着色 headless 断言 (0CRT)
 *
 * 覆盖语言识别/关键字/类型/函数/预处理/字符串/注释/属性与缓冲扩容。
 * 构建: bin\cui_md_highlight_test.exe; 退出码 0 = 全通过.
 */
#include <windows.h>
#include <wchar.h>
#include "ui/ui_md_highlight.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);

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

#define MAX_SPANS 512

static int span_at(const UiMdSpan *sp, int n, int pos)
{
	int i;
	for (i = 0; i < n; i++)
		if (pos >= sp[i].start && pos < sp[i].start + sp[i].len)
			return sp[i].style;
	return 0;
}

static int wfind(const wchar_t *text, const wchar_t *needle)
{
	int i, k;
	for (i = 0; text[i]; i++) {
		for (k = 0; needle[k] && text[i + k] == needle[k]; k++) {}
		if (!needle[k]) return i;
	}
	return -1;
}

static int wlen(const wchar_t *s)
{
	int n = 0;
	while (s && s[n]) n++;
	return n;
}

static int tok(const char *lang, const wchar_t *text, UiMdSpan *sp, int cap)
{
	int n = wlen(text);
	return UiMdHighlight_Tokenize(lang, text, n, sp, cap);
}

static void test_c(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *src =
		L"#include <stdio.h>\n"
		L"int main(void) {\n"
		L"    const char *s = \"hi\";\n"
		L"    return 0; // done\n"
		L"}\n";
	int n = tok("c", src, sp, MAX_SPANS);
	int p;

	CHECK(n > 0);
	CHECK(span_at(sp, n, 0) == MD_TOK_PREPROC);
	p = wfind(src, L"int main");
	CHECK(p >= 0 && span_at(sp, n, p) == MD_TOK_TYPE);
	CHECK(p >= 0 && span_at(sp, n, p + 4) == MD_TOK_FUNCTION);
	CHECK(span_at(sp, n, wfind(src, L"const")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"\"hi\"")) == MD_TOK_STRING);
	CHECK(span_at(sp, n, wfind(src, L"return")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"0;")) == MD_TOK_NUMBER);
	CHECK(span_at(sp, n, wfind(src, L"// done")) == MD_TOK_COMMENT);
	CHECK(span_at(sp, n, wfind(src, L"main")) != MD_TOK_COMMENT);

	/* 扩容协议: cap 不足时返回需要的总数, 且至少写满 cap */
	{
		UiMdSpan one[1];
		int need = UiMdHighlight_Tokenize("c", src, wlen(src), one, 1);
		CHECK(need > 1);
		CHECK(one[0].len > 0 && one[0].style != 0);
	}
}

static void test_json(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *src = L"{\"name\": \"cui\", \"n\": 42, \"ok\": true}";
	int n = tok("json", src, sp, MAX_SPANS);

	CHECK(n > 0);
	CHECK(span_at(sp, n, wfind(src, L"\"name\"")) == MD_TOK_PROPERTY);
	CHECK(span_at(sp, n, wfind(src, L"\"cui\"")) == MD_TOK_STRING);
	CHECK(span_at(sp, n, wfind(src, L"42")) == MD_TOK_NUMBER);
	CHECK(span_at(sp, n, wfind(src, L"true")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"{")) == 0);
}

static void test_python(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *src =
		L"@app.route(\"/\")\n"
		L"def hello(name):\n"
		L"    \"\"\"doc string\"\"\"\n"
		L"    return \"hi\" # note\n"
		L"    ok = True\n";
	int n = tok("py", src, sp, MAX_SPANS);

	CHECK(n > 0);
	CHECK(span_at(sp, n, 0) == MD_TOK_PREPROC);
	CHECK(span_at(sp, n, wfind(src, L"def")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"hello")) == MD_TOK_FUNCTION);
	CHECK(span_at(sp, n, wfind(src, L"\"\"\"doc")) == MD_TOK_STRING);
	CHECK(span_at(sp, n, wfind(src, L"return")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"# note")) == MD_TOK_COMMENT);
	CHECK(span_at(sp, n, wfind(src, L"True")) == MD_TOK_KEYWORD);
}

static void test_js(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *src =
		L"function add(a, b) {\n"
		L"  const p = { name: \"cui\" };\n"
		L"  return a + b; // sum\n"
		L"}\n";
	int n = tok("js", src, sp, MAX_SPANS);

	CHECK(n > 0);
	CHECK(span_at(sp, n, wfind(src, L"function")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"add")) == MD_TOK_FUNCTION);
	CHECK(span_at(sp, n, wfind(src, L"const")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"\"cui\"")) == MD_TOK_STRING);
	CHECK(span_at(sp, n, wfind(src, L"// sum")) == MD_TOK_COMMENT);
}

static void test_go_rust_sql(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *go =
		L"func main() {\n"
		L"    fmt.Println(\"hi\")\n"
		L"}\n";
	const wchar_t *rust =
		L"fn main() {\n"
		L"    println!(\"hi\");\n"
		L"}\n";
	const wchar_t *sql =
		L"SELECT id FROM users WHERE id = 1; -- note\n";
	int n;

	n = tok("go", go, sp, MAX_SPANS);
	CHECK(n > 0);
	CHECK(span_at(sp, n, wfind(go, L"func")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(go, L"main")) == MD_TOK_FUNCTION);
	CHECK(span_at(sp, n, wfind(go, L"Println")) == MD_TOK_FUNCTION);
	CHECK(span_at(sp, n, wfind(go, L"\"hi\"")) == MD_TOK_STRING);

	n = tok("rust", rust, sp, MAX_SPANS);
	CHECK(n > 0);
	CHECK(span_at(sp, n, wfind(rust, L"fn")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(rust, L"main")) == MD_TOK_FUNCTION);
	CHECK(span_at(sp, n, wfind(rust, L"println!")) == MD_TOK_PREPROC);
	CHECK(span_at(sp, n, wfind(rust, L"\"hi\"")) == MD_TOK_STRING);

	n = tok("sql", sql, sp, MAX_SPANS);
	CHECK(n > 0);
	CHECK(span_at(sp, n, wfind(sql, L"SELECT")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(sql, L"FROM")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(sql, L"users")) == 0);
	CHECK(span_at(sp, n, wfind(sql, L"1;")) == MD_TOK_NUMBER);
	CHECK(span_at(sp, n, wfind(sql, L"-- note")) == MD_TOK_COMMENT);
}

static void test_java_cs(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *java =
		L"public class Main {\n"
		L"    static void main(String[] args) { // entry\n"
		L"        System.out.println(1);\n"
		L"    }\n"
		L"}\n";
	const wchar_t *cs =
		L"using System;\n"
		L"class P { static void Main(string[] a) {} }\n";
	int n;

	n = tok("java", java, sp, MAX_SPANS);
	CHECK(n > 0);
	CHECK(span_at(sp, n, wfind(java, L"public")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(java, L"class")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(java, L"String")) == MD_TOK_TYPE);
	CHECK(span_at(sp, n, wfind(java, L"main")) == MD_TOK_FUNCTION);
	CHECK(span_at(sp, n, wfind(java, L"// entry")) == MD_TOK_COMMENT);

	n = tok("cs", cs, sp, MAX_SPANS);
	CHECK(n > 0);
	CHECK(span_at(sp, n, wfind(cs, L"using")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(cs, L"string")) == MD_TOK_TYPE);
	CHECK(span_at(sp, n, wfind(cs, L"Main")) == MD_TOK_FUNCTION);
}

static void test_shell(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *src =
		L"#!/bin/bash\n"
		L"if [ -n $HOME ]; then\n"
		L"    echo \"hi\" # note\n"
		L"fi\n";
	int n = tok("sh", src, sp, MAX_SPANS);

	CHECK(n > 0);
	CHECK(span_at(sp, n, 0) == MD_TOK_PREPROC);
	CHECK(span_at(sp, n, wfind(src, L"if")) == MD_TOK_KEYWORD);
	CHECK(span_at(sp, n, wfind(src, L"$HOME")) == MD_TOK_PROPERTY);
	CHECK(span_at(sp, n, wfind(src, L"\"hi\"")) == MD_TOK_STRING);
	CHECK(span_at(sp, n, wfind(src, L"# note")) == MD_TOK_COMMENT);
}

static void test_generic_and_bounds(void)
{
	UiMdSpan sp[MAX_SPANS];
	const wchar_t *src = L"// hi\nint x = 1;\n";
	int n = tok("", src, sp, MAX_SPANS);

	CHECK(n > 0);
	CHECK(span_at(sp, n, 0) == MD_TOK_COMMENT);
	CHECK(span_at(sp, n, wfind(src, L"int")) == MD_TOK_TYPE);
	CHECK(span_at(sp, n, wfind(src, L"1;")) == MD_TOK_NUMBER);

	CHECK(UiMdHighlight_Tokenize("c", NULL, 10, sp, MAX_SPANS) == 0);
	CHECK(UiMdHighlight_Tokenize("c", L"x", 0, sp, MAX_SPANS) == 0);
	CHECK(UiMdHighlight_Tokenize("c", src, wlen(src), NULL, 0) == n);
}

void __stdcall startup(void)
{
	char line[160];

	test_c();
	test_json();
	test_python();
	test_js();
	test_go_rust_sql();
	test_java_cs();
	test_shell();
	test_generic_and_bounds();

	sprintf(line, "md_highlight_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	ExitProcess(g_fail ? 1u : 0u);
}
