/*
 * ui_md_highlight.h - Markdown 围栏代码的内置语法着色 (纯 C)
 *
 * 输入 UTF-16 代码块文本, 输出按 UTF-16 偏移的 UiMdSpan. 不分配内存,
 * 不依赖 gfx/user32; 调用方按返回的 span 总数决定输出缓冲是否够大.
 * 支持 C/C++/Java/C#、JS/TS、JSON、Python、Shell、Go、Rust、SQL;
 * 未知语言走通用规则.
 */
#ifndef CUI_UI_MD_HIGHLIGHT_H
#define CUI_UI_MD_HIGHLIGHT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiMdSpan { int start, len; uint8_t style; } UiMdSpan;

/* token 类别; 1..4 是 v1 兼容类别, 5..8 由内置 tokenizer 使用 */
enum {
	MD_TOK_KEYWORD = 1,
	MD_TOK_STRING = 2,
	MD_TOK_COMMENT = 3,
	MD_TOK_NUMBER = 4,
	MD_TOK_TYPE = 5,
	MD_TOK_FUNCTION = 6,
	MD_TOK_PREPROC = 7,
	MD_TOK_PROPERTY = 8
};

/* 返回需要的 span 总数 (可能大于 cap; 此时 out 只写了前 cap 条).
 * lang 为围栏 info string, NULL/空/未知语言走通用规则. */
int UiMdHighlight_Tokenize(const char *lang, const wchar_t *text, int len,
	UiMdSpan *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_MD_HIGHLIGHT_H */
