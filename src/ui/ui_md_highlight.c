/*
 * ui_md_highlight.c - 内置围栏代码 tokenizer (纯 C, 0 分配)
 *
 * 语言按围栏 info string 选择; 未知语言走通用规则. 输出 UTF-16 偏移,
 * 同一位置只产生一个 span, 调用方按顺序叠加颜色.
 */
#include "ui/ui_md_highlight.h"

typedef enum HlLang {
	HL_GENERIC = 0,
	HL_C,
	HL_JS,
	HL_JSON,
	HL_PY,
	HL_SHELL,
	HL_GO,
	HL_RUST,
	HL_SQL
} HlLang;

typedef struct HlCtx {
	const wchar_t *t;
	int n;
	int i;
	UiMdSpan *out;
	int cap;
	int count;
	HlLang lang;
} HlCtx;

/* ── 字符与名称 ─────────────────────────────────────────────────────── */

static int hl_is_digit(wchar_t c)
{
	return c >= L'0' && c <= L'9';
}

static int hl_is_alpha(wchar_t c)
{
	return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_';
}

static int hl_is_ident(wchar_t c)
{
	return hl_is_alpha(c) || hl_is_digit(c);
}

static wchar_t hl_lower(wchar_t c)
{
	return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c;
}

static int hl_name_is(const char *s, const char *want)
{
	while (*s && *want) {
		if (hl_lower((wchar_t)(unsigned char)*s) !=
		    hl_lower((wchar_t)(unsigned char)*want))
			return 0;
		s++;
		want++;
	}
	return *s == 0 && *want == 0;
}

static HlLang hl_lang_of(const char *name)
{
	if (!name || !name[0]) return HL_GENERIC;
	if (hl_name_is(name, "c") || hl_name_is(name, "h") ||
	    hl_name_is(name, "cpp") || hl_name_is(name, "c++") ||
	    hl_name_is(name, "cc") || hl_name_is(name, "cxx") ||
	    hl_name_is(name, "hpp") || hl_name_is(name, "hh") ||
	    hl_name_is(name, "hxx") || hl_name_is(name, "objc") ||
	    hl_name_is(name, "objective-c") || hl_name_is(name, "m") ||
	    hl_name_is(name, "mm") || hl_name_is(name, "java") ||
	    hl_name_is(name, "cs") || hl_name_is(name, "csharp") ||
	    hl_name_is(name, "c#"))
		return HL_C;
	if (hl_name_is(name, "js") || hl_name_is(name, "javascript") ||
	    hl_name_is(name, "jsx") || hl_name_is(name, "mjs") ||
	    hl_name_is(name, "cjs") || hl_name_is(name, "ts") ||
	    hl_name_is(name, "typescript") || hl_name_is(name, "tsx"))
		return HL_JS;
	if (hl_name_is(name, "json")) return HL_JSON;
	if (hl_name_is(name, "py") || hl_name_is(name, "python") ||
	    hl_name_is(name, "python3") || hl_name_is(name, "py3"))
		return HL_PY;
	if (hl_name_is(name, "sh") || hl_name_is(name, "bash") ||
	    hl_name_is(name, "zsh") || hl_name_is(name, "shell") ||
	    hl_name_is(name, "console"))
		return HL_SHELL;
	if (hl_name_is(name, "go") || hl_name_is(name, "golang"))
		return HL_GO;
	if (hl_name_is(name, "rs") || hl_name_is(name, "rust"))
		return HL_RUST;
	if (hl_name_is(name, "sql") || hl_name_is(name, "mysql") ||
	    hl_name_is(name, "postgres") || hl_name_is(name, "postgresql") ||
	    hl_name_is(name, "sqlite"))
		return HL_SQL;
	return HL_GENERIC;
}

/* ── 词表 ───────────────────────────────────────────────────────────── */

static const char *const kCKeyword[] = {
	"auto", "break", "case", "const", "continue", "default", "do", "else",
	"enum", "extern", "for", "goto", "if", "inline", "register",
	"restrict", "return", "sizeof", "static", "struct", "switch",
	"typedef", "union", "volatile", "while", "_Alignas", "_Alignof",
	"_Atomic", "_Generic", "_Noreturn", "_Static_assert", "_Thread_local",
	"alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor",
	"catch", "class", "compl", "concept", "consteval", "constexpr",
	"constinit", "const_cast", "co_await", "co_return", "co_yield",
	"decltype", "delete", "dynamic_cast", "explicit", "export", "false",
	"friend", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
	"nullptr", "operator", "or", "or_eq", "override", "private",
	"protected", "public", "reinterpret_cast", "requires", "static_assert",
	"static_cast", "template", "this", "thread_local", "throw", "true",
	"try", "typeid", "typename", "using", "virtual", "xor", "xor_eq",
	/* Java / C# 常驻关键字 (info string 映射到同一套 C 风格扫描) */
	"abstract", "assert", "base", "boolean", "byte", "checked", "delegate",
	"event", "explicit", "extends", "final", "finally", "fixed", "foreach",
	"get", "implicit", "implements", "import", "instanceof", "interface",
	"internal", "is", "lock", "native", "out", "package", "params",
	"partial", "readonly", "ref", "sbyte", "sealed", "set", "stackalloc",
	"strictfp", "synchronized", "throws", "transient", "unchecked",
	"unsafe", "value", "var", "where", "yield"
};

static const char *const kCType[] = {
	"void", "char", "short", "int", "long", "float", "double", "signed",
	"unsigned", "_Bool", "bool", "size_t", "ssize_t", "ptrdiff_t",
	"wchar_t", "char8_t", "char16_t", "char32_t", "intptr_t", "uintptr_t",
	"int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
	"uint32_t", "uint64_t",
	"DWORD", "WORD", "BYTE", "BOOL", "UINT", "ULONG", "LONG", "HANDLE",
	"HWND", "HINSTANCE", "HRESULT", "LPSTR", "LPCSTR", "LPWSTR",
	"LPCWSTR", "LPARAM", "WPARAM", "LRESULT", "SIZE_T", "UINT64",
	"INT64", "UINT32", "INT32", "POINT", "RECT", "GUID", "IID",
	/* Java / C# 常见类型 */
	"byte", "sbyte", "decimal", "string", "object", "dynamic", "boolean",
	"String", "Object", "Boolean", "Integer", "Long", "Double", "Float",
	"Byte", "Short", "Character", "List", "Map", "Set", "ArrayList",
	"HashMap", "HashSet", "Task", "Exception"
};

static const char *const kJsKeyword[] = {
	"as", "async", "await", "break", "case", "catch", "class", "const",
	"continue", "debugger", "default", "delete", "do", "else", "export",
	"extends", "false", "finally", "for", "from", "function", "get", "if",
	"import", "in", "instanceof", "let", "new", "null", "of", "return",
	"set", "static", "super", "switch", "this", "throw", "true", "try",
	"typeof", "undefined", "var", "void", "while", "with", "yield"
};

static const char *const kJsType[] = {
	"Array", "ArrayBuffer", "BigInt", "Boolean", "Date", "Error",
	"EvalError", "Function", "JSON", "Map", "Math", "Number", "Object",
	"Promise", "Proxy", "RangeError", "ReferenceError", "RegExp", "Set",
	"String", "Symbol", "SyntaxError", "TypeError", "Uint8Array",
	"Uint16Array", "Uint32Array", "Int8Array", "Int16Array", "Int32Array",
	"Float32Array", "Float64Array", "WeakMap", "WeakSet"
};

static const char *const kPyKeyword[] = {
	"False", "None", "True", "and", "as", "assert", "async", "await",
	"break", "class", "continue", "def", "del", "elif", "else", "except",
	"finally", "for", "from", "global", "if", "import", "in", "is",
	"lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
	"while", "with", "yield", "match", "case"
};

static const char *const kPyType[] = {
	"bool", "bytearray", "bytes", "complex", "dict", "float", "frozenset",
	"int", "list", "memoryview", "object", "range", "set", "slice", "str",
	"tuple", "type", "BaseException", "Exception", "ValueError",
	"TypeError", "KeyError", "IndexError", "RuntimeError", "OSError"
};

static const char *const kShKeyword[] = {
	"if", "then", "else", "elif", "fi", "for", "while", "until", "do",
	"done", "case", "esac", "function", "in", "select", "time", "coproc"
};

static const char *const kGoKeyword[] = {
	"break", "case", "chan", "const", "continue", "default", "defer",
	"else", "fallthrough", "for", "func", "go", "goto", "if", "import",
	"interface", "map", "package", "range", "return", "select", "struct",
	"switch", "type", "var"
};

static const char *const kGoType[] = {
	"bool", "byte", "complex64", "complex128", "error", "float32",
	"float64", "int", "int8", "int16", "int32", "int64", "rune", "string",
	"uint", "uint8", "uint16", "uint32", "uint64", "uintptr", "any"
};

static const char *const kRustKeyword[] = {
	"as", "async", "await", "break", "const", "continue", "crate", "dyn",
	"else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
	"let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
	"self", "Self", "static", "struct", "super", "trait", "true", "type",
	"unsafe", "use", "where", "while", "yield"
};

static const char *const kRustType[] = {
	"bool", "char", "str", "i8", "i16", "i32", "i64", "i128", "isize",
	"u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64", "String",
	"Vec", "Option", "Result", "Box", "Rc", "Arc", "RefCell", "Cell",
	"HashMap", "HashSet", "BTreeMap", "BTreeSet"
};

static const char *const kSqlKeyword[] = {
	"add", "all", "alter", "and", "any", "as", "asc", "between", "by",
	"case", "check", "column", "constraint", "create", "cross", "database",
	"default", "delete", "desc", "distinct", "drop", "else", "end",
	"exists", "foreign", "from", "full", "group", "having", "in", "index",
	"inner", "insert", "into", "is", "join", "key", "left", "like",
	"limit", "not", "null", "offset", "on", "or", "order", "outer",
	"primary", "references", "right", "select", "set", "table", "then",
	"union", "unique", "update", "values", "view", "when", "where"
};

static const char *const kSqlType[] = {
	"bigint", "blob", "boolean", "char", "date", "decimal", "double",
	"float", "int", "integer", "numeric", "real", "smallint", "text",
	"time", "timestamp", "varchar"
};

static const char *const kGenKeyword[] = {
	"if", "else", "for", "while", "do", "switch", "case", "break",
	"continue", "return", "class", "struct", "enum", "interface",
	"function", "func", "def", "fn", "let", "var", "const", "static",
	"public", "private", "protected", "new", "delete", "try", "catch",
	"finally", "throw", "import", "from", "package", "using", "namespace",
	"async", "await", "yield", "in", "of", "not", "and", "or", "true",
	"false", "null", "None", "True", "False", "this", "self", "super"
};

static const char *const kGenType[] = {
	"void", "char", "short", "int", "long", "float", "double", "bool",
	"signed", "unsigned", "size_t", "string", "number", "boolean",
	"object", "String", "Array", "Object", "List", "Dict", "Map", "Set"
};

#define HL_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int hl_word_eq(const wchar_t *w, int n, const char *s, int ci)
{
	int k = 0;
	for (; k < n && s[k]; k++) {
		wchar_t a = w[k];
		wchar_t b = (wchar_t)(unsigned char)s[k];
		if (ci) { a = hl_lower(a); b = hl_lower(b); }
		if (a != b) return 0;
	}
	return k == n && s[k] == 0;
}

static int hl_in_list(const wchar_t *w, int n, const char *const *list,
	int count, int ci)
{
	int i;
	for (i = 0; i < count; i++)
		if (hl_word_eq(w, n, list[i], ci)) return 1;
	return 0;
}

/* ── 输出 ───────────────────────────────────────────────────────────── */

static void hl_put(HlCtx *c, int start, int len, int style)
{
	if (start < 0 || len <= 0) return;
	if (c->count < c->cap && c->out) {
		c->out[c->count].start = start;
		c->out[c->count].len = len;
		c->out[c->count].style = (uint8_t)style;
	}
	c->count++;
}

static int hl_line_start(const wchar_t *t, int i)
{
	int k = i - 1;
	while (k >= 0 && (t[k] == L' ' || t[k] == L'\t')) k--;
	return k < 0 || t[k] == L'\n' || t[k] == L'\r';
}

static wchar_t hl_next_code(const wchar_t *t, int n, int i)
{
	while (i < n && (t[i] == L' ' || t[i] == L'\t')) i++;
	return i < n ? t[i] : 0;
}

static int hl_prev_dot(const wchar_t *t, int i)
{
	int k = i - 1;
	while (k >= 0 && (t[k] == L' ' || t[k] == L'\t')) k--;
	return k >= 0 && t[k] == L'.';
}

/* ── 扫描器 ─────────────────────────────────────────────────────────── */

static int hl_scan_line(const wchar_t *t, int n, int i)
{
	while (i < n && t[i] != L'\n') i++;
	return i;
}

static int hl_scan_block_comment(const wchar_t *t, int n, int i, int nested)
{
	int depth = 1;
	i += 2;
	while (i < n) {
		if (nested && i + 1 < n && t[i] == L'/' && t[i + 1] == L'*') {
			depth++;
			i += 2;
			continue;
		}
		if (i + 1 < n && t[i] == L'*' && t[i + 1] == L'/') {
			depth--;
			i += 2;
			if (depth == 0) break;
			continue;
		}
		i++;
	}
	return i;
}

static int hl_scan_string(const wchar_t *t, int n, int i, wchar_t q,
	int allow_nl, int no_escape, int doubled_escape)
{
	i++;
	while (i < n) {
		wchar_t ch = t[i];
		if (!no_escape && ch == L'\\' && i + 1 < n) {
			i += 2;
			continue;
		}
		if (doubled_escape && ch == q && i + 1 < n && t[i + 1] == q) {
			i += 2;
			continue;
		}
		if (ch == q) { i++; break; }
		if (!allow_nl && (ch == L'\n' || ch == L'\r')) break;
		i++;
	}
	return i;
}

static int hl_scan_triple(const wchar_t *t, int n, int i, wchar_t q)
{
	i += 3;
	while (i + 2 < n) {
		if (t[i] == q && t[i + 1] == q && t[i + 2] == q) {
			i += 3;
			return i;
		}
		i++;
	}
	return n;
}

static int hl_scan_number(const wchar_t *t, int n, int i)
{
	if (t[i] == L'0' && i + 1 < n &&
	    (t[i + 1] == L'x' || t[i + 1] == L'X')) {
		i += 2;
		while (i < n && (hl_is_digit(t[i]) ||
			(t[i] >= L'a' && t[i] <= L'f') ||
			(t[i] >= L'A' && t[i] <= L'F') || t[i] == L'_'))
			i++;
	} else if (t[i] == L'0' && i + 1 < n &&
		(t[i + 1] == L'b' || t[i + 1] == L'B' ||
		 t[i + 1] == L'o' || t[i + 1] == L'O')) {
		i += 2;
		while (i < n && (hl_is_ident(t[i]) || t[i] == L'_')) i++;
	} else {
		while (i < n && (hl_is_digit(t[i]) || t[i] == L'_')) i++;
		if (i < n && t[i] == L'.' && i + 1 < n && hl_is_digit(t[i + 1])) {
			i++;
			while (i < n && (hl_is_digit(t[i]) || t[i] == L'_')) i++;
		}
		if (i < n && (t[i] == L'e' || t[i] == L'E')) {
			int k = i + 1;
			if (k < n && (t[k] == L'+' || t[k] == L'-')) k++;
			if (k < n && hl_is_digit(t[k])) {
				i = k;
				while (i < n && (hl_is_digit(t[i]) || t[i] == L'_')) i++;
			}
		}
	}
	while (i < n && (hl_is_alpha(t[i]) || hl_is_digit(t[i]))) i++;
	return i;
}

static int hl_keyword_style(HlCtx *c, int start, int end)
{
	const wchar_t *w = c->t + start;
	int n = end - start;
	int ci = c->lang == HL_SQL;
	int is_type = 0, is_kw = 0;

	switch (c->lang) {
	case HL_C:
		is_type = hl_in_list(w, n, kCType, HL_COUNT(kCType), 0);
		is_kw = hl_in_list(w, n, kCKeyword, HL_COUNT(kCKeyword), 0);
		break;
	case HL_JS:
		is_type = hl_in_list(w, n, kJsType, HL_COUNT(kJsType), 0);
		is_kw = hl_in_list(w, n, kJsKeyword, HL_COUNT(kJsKeyword), 0);
		break;
	case HL_PY:
		is_type = hl_in_list(w, n, kPyType, HL_COUNT(kPyType), 0);
		is_kw = hl_in_list(w, n, kPyKeyword, HL_COUNT(kPyKeyword), 0);
		break;
	case HL_SHELL:
		is_kw = hl_in_list(w, n, kShKeyword, HL_COUNT(kShKeyword), 0);
		break;
	case HL_GO:
		is_type = hl_in_list(w, n, kGoType, HL_COUNT(kGoType), 0);
		is_kw = hl_in_list(w, n, kGoKeyword, HL_COUNT(kGoKeyword), 0);
		break;
	case HL_RUST:
		is_type = hl_in_list(w, n, kRustType, HL_COUNT(kRustType), 0);
		is_kw = hl_in_list(w, n, kRustKeyword, HL_COUNT(kRustKeyword), 0);
		break;
	case HL_SQL:
		is_type = hl_in_list(w, n, kSqlType, HL_COUNT(kSqlType), ci);
		is_kw = hl_in_list(w, n, kSqlKeyword, HL_COUNT(kSqlKeyword), ci);
		break;
	default:
		is_type = hl_in_list(w, n, kGenType, HL_COUNT(kGenType), 0);
		is_kw = hl_in_list(w, n, kGenKeyword, HL_COUNT(kGenKeyword), 0);
		break;
	}
	if (is_type) return MD_TOK_TYPE;
	if (is_kw) return MD_TOK_KEYWORD;
	if (hl_next_code(c->t, c->n, end) == L'(') return MD_TOK_FUNCTION;
	if (c->lang == HL_JS && hl_prev_dot(c->t, start)) return MD_TOK_PROPERTY;
	return 0;
}

static void hl_scan_code(HlCtx *c)
{
	const wchar_t *t = c->t;
	int n = c->n;

	while (c->i < n) {
		int i = c->i;
		wchar_t ch = t[i];
		wchar_t nx = i + 1 < n ? t[i + 1] : 0;

		if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
			c->i++;
			continue;
		}

		if (c->lang == HL_JSON) {
			if (ch == L'"') {
				int s = i;
				int e = hl_scan_string(t, n, i, L'"', 0, 0, 0);
				int k = e;
				int style = MD_TOK_STRING;
				while (k < n && (t[k] == L' ' || t[k] == L'\t')) k++;
				if (k < n && t[k] == L':') style = MD_TOK_PROPERTY;
				hl_put(c, s, e - s, style);
				c->i = e;
				continue;
			}
			if (hl_is_digit(ch) || (ch == L'-' && i + 1 < n &&
			    hl_is_digit(t[i + 1]))) {
				int s = i;
				if (ch == L'-') i++;
				i = hl_scan_number(t, n, i);
				hl_put(c, s, i - s, MD_TOK_NUMBER);
				c->i = i;
				continue;
			}
			if (hl_is_alpha(ch)) {
				int s = i;
				while (i < n && hl_is_ident(t[i])) i++;
				if (hl_word_eq(t + s, i - s, "true", 0) ||
				    hl_word_eq(t + s, i - s, "false", 0) ||
				    hl_word_eq(t + s, i - s, "null", 0))
					hl_put(c, s, i - s, MD_TOK_KEYWORD);
				c->i = i;
				continue;
			}
			c->i++;
			continue;
		}

		/* 行注释 / 块注释 */
		if ((c->lang == HL_C || c->lang == HL_JS || c->lang == HL_GO ||
		     c->lang == HL_RUST || c->lang == HL_GENERIC) &&
		    ch == L'/' && nx == L'/') {
			int s = i;
			i = hl_scan_line(t, n, i);
			hl_put(c, s, i - s, MD_TOK_COMMENT);
			c->i = i;
			continue;
		}
		if ((c->lang == HL_C || c->lang == HL_JS || c->lang == HL_GO ||
		     c->lang == HL_RUST || c->lang == HL_SQL ||
		     c->lang == HL_GENERIC) &&
		    ch == L'/' && nx == L'*') {
			int s = i;
			i = hl_scan_block_comment(t, n, i, c->lang == HL_RUST);
			hl_put(c, s, i - s, MD_TOK_COMMENT);
			c->i = i;
			continue;
		}
		/* shell shebang */
		if (c->lang == HL_SHELL && ch == L'#' && nx == L'!' &&
		    hl_line_start(t, i)) {
			int s = i;
			i = hl_scan_line(t, n, i);
			hl_put(c, s, i - s, MD_TOK_PREPROC);
			c->i = i;
			continue;
		}
		if ((c->lang == HL_PY || c->lang == HL_SHELL ||
		     c->lang == HL_GENERIC) && ch == L'#') {
			int s = i;
			i = hl_scan_line(t, n, i);
			hl_put(c, s, i - s, MD_TOK_COMMENT);
			c->i = i;
			continue;
		}
		if (c->lang == HL_SQL && ch == L'-' && nx == L'-') {
			int s = i;
			i = hl_scan_line(t, n, i);
			hl_put(c, s, i - s, MD_TOK_COMMENT);
			c->i = i;
			continue;
		}

		/* C 预处理指令: 行首 # 到行尾 */
		if (c->lang == HL_C && ch == L'#' && hl_line_start(t, i)) {
			int s = i;
			i = hl_scan_line(t, n, i);
			hl_put(c, s, i - s, MD_TOK_PREPROC);
			c->i = i;
			continue;
		}
		/* Python 装饰器 @app.route */
		if (c->lang == HL_PY && ch == L'@' && hl_line_start(t, i)) {
			int s = i;
			i++;
			while (i < n && (hl_is_ident(t[i]) || t[i] == L'.')) i++;
			hl_put(c, s, i - s, MD_TOK_PREPROC);
			c->i = i;
			continue;
		}
		/* shell 变量 $NAME / ${NAME} */
		if (c->lang == HL_SHELL && ch == L'$') {
			int s = i;
			i++;
			if (i < n && t[i] == L'{') {
				int depth = 1;
				i++;
				while (i < n && depth > 0) {
					if (t[i] == L'{') depth++;
					else if (t[i] == L'}') depth--;
					i++;
				}
			} else {
				while (i < n && hl_is_ident(t[i])) i++;
			}
			if (i > s + 1) hl_put(c, s, i - s, MD_TOK_PROPERTY);
			c->i = i;
			continue;
		}

		/* 字符串 */
		if (c->lang == HL_PY && (ch == L'\'' || ch == L'"') &&
		    i + 2 < n && t[i + 1] == ch && t[i + 2] == ch) {
			int s = i;
			i = hl_scan_triple(t, n, i, ch);
			hl_put(c, s, i - s, MD_TOK_STRING);
			c->i = i;
			continue;
		}
		if (ch == L'"' || ch == L'\'' ||
		    (ch == L'`' && (c->lang == HL_JS || c->lang == HL_GO ||
		     c->lang == HL_GENERIC))) {
			int s = i;
			int allow_nl = ch == L'`' && c->lang == HL_JS;
			int no_escape = c->lang == HL_SHELL && ch == L'\'';
			int doubled = c->lang == HL_SQL && ch == L'\'';
			if (c->lang == HL_RUST && ch == L'\'' &&
			    !(i + 2 < n && t[i + 2] == L'\'')) {
				c->i++;
				continue;   /* 生命周期 'a, 不按字符字面量处理 */
			}
			i = hl_scan_string(t, n, i, ch, allow_nl, no_escape, doubled);
			hl_put(c, s, i - s, MD_TOK_STRING);
			c->i = i;
			continue;
		}

		/* 数字 */
		if (hl_is_digit(ch)) {
			int s = i;
			i = hl_scan_number(t, n, i);
			hl_put(c, s, i - s, MD_TOK_NUMBER);
			c->i = i;
			continue;
		}

		/* 标识符 */
		if (hl_is_alpha(ch)) {
			int s = i;
			int style;
			while (i < n && hl_is_ident(t[i])) i++;
			if (c->lang == HL_RUST && i < n && t[i] == L'!') {
				i++;
				hl_put(c, s, i - s, MD_TOK_PREPROC);
				c->i = i;
				continue;
			}
			style = hl_keyword_style(c, s, i);
			if (style) hl_put(c, s, i - s, style);
			c->i = i;
			continue;
		}

		c->i++;
	}
}

int UiMdHighlight_Tokenize(const char *lang, const wchar_t *text, int len,
	UiMdSpan *out, int cap)
{
	HlCtx c;
	if (!text || len <= 0) return 0;
	c.t = text;
	c.n = len;
	c.i = 0;
	c.out = out;
	c.cap = cap > 0 ? cap : 0;
	c.count = 0;
	c.lang = hl_lang_of(lang);
	hl_scan_code(&c);
	return c.count;
}
