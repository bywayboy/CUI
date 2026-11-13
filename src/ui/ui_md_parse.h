/*
 * ui_md_parse.h - Markdown 解析器 (CommonMark 核心 + GFM 常用扩展)
 *
 * 纯解析层: 不依赖 gfx/window, 可 headless 测试。设计见
 * docs/CUI-Markdown-设计方案.md §3。
 *
 * 产物:
 *   MdDoc -> MdBlock 树 (QUOTE/LIST 是容器; 其余是叶子)
 *   叶子块的 text16 是展示文本 (UTF-16, 实体/转义已解开, 强调符号已移除)
 *   MdRun 引用 text16 的字符区间并携带样式位
 *   MdLink 指向块内 run 区间 + 文档 URL 池
 *
 * 偏移约定:
 *   MdRun.start/len            UTF-16 码元, 相对 block->text16
 *   MdLink.urlOff/urlLen       MdDoc.urls 内的字节偏移 (0 结尾)
 *   MdBlock.langOff/langLen    MdDoc.urls 内的语言串偏移 (0 结尾)
 *   MdBlock.urlOff/urlLen      MdDoc.urls 内的图片 URL 偏移
 *   MdBlock.srcOff/srcLen      原始源串区间 (诊断/哈希)
 */
#ifndef CUI_UI_MD_PARSE_H
#define CUI_UI_MD_PARSE_H

#include <stdint.h>
#include <wchar.h>

/* 块类型 */
enum {
	MD_BLOCK_PARAGRAPH = 0,
	MD_BLOCK_HEADING,
	MD_BLOCK_CODE,
	MD_BLOCK_HR,
	MD_BLOCK_IMAGE,
	MD_BLOCK_TABLE,
	MD_BLOCK_QUOTE,
	MD_BLOCK_LIST,
	MD_BLOCK_ITEM
};

/* run 样式位 */
enum {
	MD_R_BOLD     = 1u << 0,
	MD_R_ITALIC   = 1u << 1,
	MD_R_STRIKE   = 1u << 2,
	MD_R_CODE     = 1u << 3,
	MD_R_LINK     = 1u << 4,
	MD_R_AUTOLINK = 1u << 5
};

/* 任务列表状态 */
enum {
	MD_TASK_NONE = 0,
	MD_TASK_TODO,
	MD_TASK_DONE
};

/* 代码块 flag */
enum {
	MD_BF_FENCE_CLOSED = 1u << 0,
	MD_BF_LOOSE        = 1u << 1   /* 列表: 松散 */
};

/* 表格对齐 (与渲染层约定一致) */
enum {
	MD_ALIGN_LEFT = 0,
	MD_ALIGN_CENTER,
	MD_ALIGN_RIGHT
};

typedef struct MdRun {
	int start, len;      /* UTF-16 码元 */
	uint16_t style;      /* MD_R_* */
	int link;            /* -1 或 block->links[] 下标 */
} MdRun;

typedef struct MdLink {
	int start, end;      /* 覆盖的 run 区间 [start, end) */
	int urlOff, urlLen;  /* MdDoc.urls 内偏移 */
} MdLink;

typedef struct MdCell {
	wchar_t *text16;
	int len16;
	MdRun *runs;
	int runCount, runCap;
	MdLink *links;
	int linkCount, linkCap;
	char *raw;           /* 解析中间态, 完成后为 NULL */
	int rawLen;
} MdCell;

typedef struct MdBlock {
	uint8_t type;
	uint8_t flags;       /* MD_BF_* */
	uint8_t level;       /* HEADING: 1..6 */
	uint8_t tight;       /* LIST: 1 = 紧凑 */
	uint8_t task;        /* ITEM: MD_TASK_* */
	int ordered;         /* LIST: 1 = 有序 */
	int startNo;         /* LIST: 起始序号 */
	int fence;           /* CODE: 1 = 围栏, 0 = 缩进 */
	int srcOff, srcLen;  /* 源串区间 */
	uint32_t hash;       /* 内容哈希 (用于增量 diff) */

	int langOff, langLen;/* CODE: 信息串语言 (urls 池) */
	int urlOff, urlLen;  /* IMAGE: URL (urls 池) */
	uint8_t imgAlign;    /* IMAGE: MD_ALIGN_*; 255 = 用样式默认 */

	wchar_t *text16;     /* 叶子展示文本 */
	int len16;
	MdRun *runs;
	int runCount, runCap;
	MdLink *links;
	int linkCount, linkCap;

	struct MdBlock *children;  /* QUOTE/LIST/ITEM */
	int childCount, childCap;

	int cols, rows;      /* TABLE */
	uint8_t *align;
	MdCell *cells;       /* rows*cols, 行优先 */

	char *raw;           /* 解析中间态, 完成后为 NULL */
	int rawLen;
} MdBlock;

typedef struct MdRef {
	int normOff, normLen;     /* urls 池: 归一化后的 label */
	int urlOff, urlLen;       /* urls 池: 目标 URL */
	uint32_t hash;            /* 归一化 label 哈希 */
} MdRef;

typedef struct MdDoc {
	char *src;           /* 自有副本, 换行已归一为 \n */
	int srcLen, srcCap;
	char *urls;          /* URL 池, 每个条目 0 结尾 */
	int urlsLen, urlsCap;
	MdBlock *blocks;     /* 顶层块 (树序) */
	int count, cap;
	MdRef *refs;         /* 引用定义 */
	int refCount, refsCap;
	int *refSlots;       /* 开放寻址: 值 = refs 下标 + 1 */
	int refCap;
} MdDoc;

void MdDoc_Init(MdDoc *d);
void MdDoc_Shutdown(MdDoc *d);

/* 全量解析 (会 clear 旧内容)。utf8=NULL/len<=0 得到空文档。
 * 返回 0 成功, -1 参数错误, -2 内存不足。 */
int Md_Parse(MdDoc *d, const char *utf8, int len);

/* 池访问 (返回以 0 结尾的指针; off 越界返回空串) */
const char *MdDoc_Url(const MdDoc *d, int off);
const char *MdDoc_Src(const MdDoc *d, int off);

/* 诊断: 统计指定类型块 (递归) */
int MdDoc_CountType(const MdDoc *d, int type);

#endif /* CUI_UI_MD_PARSE_H */
