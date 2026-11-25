/*
 * ui_md_parse.h - Markdown 解析器 (CommonMark 核心 + GFM 常用扩展)
 *
 * 纯解析层: 不依赖 gfx/window, 可 headless 测试。设计见
 * docs/CUI-Markdown-设计方案.md §3。
 *
 * 产物:
 *   MdDoc -> MdBlock 树 (QUOTE/LIST/CONTAINER 是容器; 其余是叶子)
 *   叶子块的 text16 是展示文本 (UTF-16, 实体/转义已解开, 强调符号已移除)
 *   MdRun 引用 text16 的字符区间并携带样式位
 *   MdLink 指向块内 run 区间 + 文档 URL 池
 *
 * 偏移约定:
 *   MdRun.start/len            UTF-16 码元, 相对 block->text16
 *   MdLink.urlOff/urlLen       MdDoc.urls 内的字节偏移 (0 结尾)
 *   MdBlock.langOff/langLen    MdDoc.urls 内的语言串偏移 (0 结尾)
 *   MdBlock.fileOff/fileLen    围栏代码的文件名 (标题栏用), 同上
 *   MdBlock.urlOff/urlLen      MdDoc.urls 内的图片 URL 偏移
 *   MdMathRef.srcOff/srcLen    MdDoc.urls 内的 LaTeX 源码 (0 结尾)
 *   MdBlock.mathOff/mathLen    MdDoc.urls 内的块级公式源码 (0 结尾)
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
	MD_BLOCK_ITEM,
	MD_BLOCK_CONTAINER,
	MD_BLOCK_MATH,
	MD_BLOCK_DIV          /* 块级 HTML 容器 (div/p/center 等价), 内部继续按
	                       * Markdown 解析; 标签名只影响"是否隐含居中" */
};

/* div 的 align 取 MD_ALIGN_*, 继承用该哨兵 */
#define MD_DIV_ALIGN_INHERIT 255

/* div 数值统一 0.25 DIP 量化 (int16 可表 ±8191.75 DIP), 以保持 MdBlock
 * 的 union 不增大; 行距倍率 ×100 存 uint16. */
#define MD_Q4(v)   ((int16_t)((v) * 4.0f))
#define MD_UNQ4(q) ((float)(q) * 0.25f)

/* 容器类型 (::: note/tip/warning/danger 与 GitHub > [!NOTE] 共用) */
enum {
	MD_BOX_NOTE = 0,
	MD_BOX_TIP,
	MD_BOX_WARNING,
	MD_BOX_DANGER,
	MD_BOX_COUNT
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

/* 行内图片: 占据 text16 的 [start,len) (alt 文本), URL 在 urls 池 */
typedef struct MdImgRef {
	int start, len;
	int urlOff, urlLen;
	int w, h;            /* 显式尺寸 (DIP); 0 = 未指定 */
} MdImgRef;

/* 行内公式: 占据 text16 的 [start,len) (原始源码, 渲染失败时作为回退),
 * LaTeX 源码在 urls 池. */
typedef struct MdMathRef {
	int start, len;
	int srcOff, srcLen;
} MdMathRef;

/* 块类型谓词: union 成员必须按 type 标签访问 (违反会读到别的成员) */
#define MD_BT_TEXT(t) ((t) == MD_BLOCK_PARAGRAPH || (t) == MD_BLOCK_HEADING || \
	(t) == MD_BLOCK_CODE || (t) == MD_BLOCK_IMAGE)
#define MD_BT_KIDS(t) ((t) == MD_BLOCK_QUOTE || (t) == MD_BLOCK_LIST || \
	(t) == MD_BLOCK_ITEM || (t) == MD_BLOCK_CONTAINER || (t) == MD_BLOCK_DIV)

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

	/* 类型独占标量: 必须按 type 访问 u.code / u.image / ... */
	union {
		struct {
			int langOff, langLen;
			int fileOff, fileLen;    /* 标题栏文件名; 0 = 未指定 */
		} code;
		struct {
			int urlOff, urlLen;
			int imgW, imgH;          /* 显式尺寸 (DIP); 0 = 未指定 */
			uint8_t imgAlign;        /* MD_ALIGN_*; 255 = 默认 */
		} image;
		struct { int mathOff, mathLen; } math;
		struct {
			int boxKind;             /* MD_BOX_* */
			int titleOff, titleLen;
		} box;
		struct { int cols, rows; } table;
		struct {
			int16_t padL, padT, padR, padB;  /* 0.25 DIP 量化 (MD_Q4) */
			int16_t marginT, marginB;
			int16_t lineSpacing;             /* >0 = 倍率×100; <0 = -绝对DIP×4; 0 = 继承 */
			uint8_t align;                   /* MD_ALIGN_*; 继承 = MD_DIV_ALIGN_INHERIT */
		} div;
	} u;

	/* 类型独占 payload: p.text / p.kids / p.table */
	union {
		struct {
			wchar_t *text16;         /* 叶子展示文本 */
			int len16;
			MdRun *runs;
			int runCount, runCap;
			MdLink *links;
			int linkCount, linkCap;
			MdImgRef *imgs;          /* 行内图片 */
			int imgCount, imgCap;
			MdMathRef *maths;        /* 行内公式 */
			int mathCount, mathCap;
		} text;
		struct {
			struct MdBlock *children; /* QUOTE/LIST/ITEM/CONTAINER */
			int childCount, childCap;
		} kids;
		struct {
			uint8_t *align;          /* TABLE: 列对齐 */
			MdCell *cells;           /* rows*cols, 行优先 */
		} table;
	} p;

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
	/* 行内扫描缓冲池 (内部类型, 对调用方不透明): 段落/标题的行内解析
	 * 逐块串行进行, 复用同一组 t/st/lk/rm 数组可省掉每块 4 次堆往返。
	 * 跨 Md_Parse 保留, 只把占用标记清零。 */
	void *inlPool;
	int inlPoolN, inlPoolCap;
	/* 块文本 arena: text16/runs 一次解析内只增不减, 用大块串联分配,
	 * 省掉每块 2 次堆往返 (一次解析动辄上千块)。随 md_clear 整链释放。 */
	void *chunkHead;
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
