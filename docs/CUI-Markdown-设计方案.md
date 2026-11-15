# CUI Markdown 渲染组件 UiMarkdown 设计方案

> 状态：阶段 1–3 已实现（2026-11-13），并已补齐真实图片（块级 + 行内）、
> 真实表格、链接点击与内置语法高亮；剩余：全文选区/复制、代码复制按钮、
> 流式解析优化与文档收尾
> 关联：`src/ui/ui_md_parse.*`（新增，纯解析器）、`src/ui/ui_md.*`（新增，组件）、
> `src/gfx/gfx_rich.h` + `src/gfx/gfx_rich.cpp` + `src/gfx/gfx_internal.h`
> （新增，可选富文本模块；`gfx.h` 核心不变）、
> `src/ui/ui_scroll.*`（复用）、`src/ui/ui_image.h`（块级图片）、
> `tests/md_test.c` + `tests/gfx_rich_test.c`（新增，headless 金样/隐藏窗口断言）、
> `samples/markdown.c`（新增）、白皮书 §8.21

## 1. 目标与非目标

### 1.1 目标

一个**应用持有**的只读 Markdown 渲染组件：

- 解析 **CommonMark 核心 + GFM 常用扩展**（表格 / 删除线 / 任务列表）；
- 富文本排版交给 DirectWrite：一个段落一个 `IDWriteTextLayout`，范围打样式，
  换行 / 断词 / CJK / bidi 全部原生处理；
- 复用 `UiScrollView` + `UiScrollLayout`：可见区间裁剪、高度缓存、宽度失效；
- **流式追加**（LLM 输出场景）：`Append` 只重解析未闭合尾块；
- 块级图片内嵌 `UiImage`：网络 / 文件 / 内存源、spinner、淡入、设备丢失重建全继承；
- 链接 hover / 点击回调、代码块复制按钮；
- **性能与文档规模解耦**：解析/估算 O(n)，首帧不建 DWrite 布局，滚动只测量可见行，
  超长代码块/大表格切分为有界单元；
- 内置 **Light / Dark 两套样式**，支持自定义，也可跟随当前主题；
- 行距可调（`lineSpacing`）；
- 0CRT、无第三方依赖、全部在 GUI 线程、兼容脏区重绘模型。

### 1.2 语法范围

| 类别 | v1 支持 |
| --- | --- |
| 块 | ATX 标题 `#`..`######`、Setext 标题 `===`/`---`、段落、空行分段 |
| 块 | 围栏代码（三反引号或三波浪线；info string 取首个词作语言） |
| 块 | 缩进代码（4 空格）、水平线 `---` `***` `___` |
| 块 | 引用 `>`（可嵌套）、无序列表 `- * +`、有序列表 `1.`/`1)`（可嵌套） |
| 块 | GFM 任务列表 `- [ ]` / `- [x]`（v1 只读展示）、GFM 管道表格（列对齐） |
| 块 | 块级图片 `![alt](url "title")`（独占一行；title 为 `left`/`center`/`right` 时作为对齐提示） |
| 行内 | 强调 `*em*` `**strong**` `_em_` `__strong__`、GFM 删除线 `~~del~~` |
| 行内 | 行内代码 `` `code` ``、反斜杠转义、硬换行（行尾两空格或 `\`） |
| 行内 | 行内链接 `[t](url "title")`、引用式链接 `[t][ref]` `[ref]`、自动链接 `<url>` |
| 行内 | 实体 `&amp; &lt; &gt; &quot; &nbsp;` + 数字实体 `&#NNN;`/`&#xHH;` |
| 预处理 | 链接引用定义 `[label]: url "title"`（大小写不敏感、空白折叠） |

### 1.3 非目标（v1）

- 全文选区 / Ctrl+C 复制（v2；DWrite `HitTestTextRange` 已为它铺路）；
- 链接的键盘激活（v1 只有鼠标，无链接焦点模型）；
- 可编辑（富文本编辑器是另一个量级的工程）；
- HTML 块 / 行内 HTML 渲染（一律按转义纯文本显示，不做 HTML 引擎）；
- 脚注、定义列表、数学公式、mermaid 等扩展；
- 标题锚点 / 目录生成。

## 2. 总体架构

```
UTF-8 源文本
   │ SetText / Append
   ▼
┌───────────────────────────────────────────────┐
│ 解析层 ui_md_parse（纯 C，不依赖 gfx/user32）   │
│  MdDoc: MdBlock 树 + MdRun[] + MdLink[]        │
│  块哈希 diff / 尾块重解析 / 引用定义预处理       │
└───────────────────────────────────────────────┘
   │ 每行高度可独立测量
   ▼
┌───────────────────────────────────────────────┐
│ 布局层 ui_md                                   │
│  MdRow[]（叶子块扁平化 + 缩进/标记上下文）       │
│  ↔ UiScrollItem[] → UiScrollLayout / UiScrollView│
│  宽度失效 / 图片高度回填 / 布局缓存 ±N 行         │
└───────────────────────────────────────────────┘
   │ 可见区间
   ▼
┌───────────────────────────────────────────────┐
│ 绘制层                                        │
│  Gfx_RichText 逐行绘制 + 装饰（竖条/HR/标记/表格）│
│  UiImage 块级图片 / 链接命中 / 复制按钮          │
└───────────────────────────────────────────────┘
```

关键约束：

- **解析器零依赖**：只用 `HeapAlloc`/`HeapRealloc` 风格的动态缓冲（现有代码同款），
  不引 gfx/window，可 headless 跑金样测试；
- **状态全部归 `UiMarkdown` 实例**，应用持有、随 ViewModel 同生命周期；
- **成本与文档规模解耦**：解析是单遍 O(n)，首帧只做 O(块数) 高度估算、
  不创建 DWrite 布局；滚动时只实测可见行；超长代码块/大表格/超大段落切分为
  有界渲染行。文件再复杂，单帧成本只与视口相关（复杂度表见 §8）；
- **交互只注册一个控件 ID**：整篇内容注册一个 widget，链接与复制按钮在组件
  内部命中，不随链接数量消耗 `UI_MAX_WIDGETS`（§6.2）；
- **gfx 核心与富文本解耦**：`gfx_d2d.cpp` 不引用、不包含、不回调任何
  `Gfx_Rich*`；富文本是独立可选模块，经 `gfx_internal.h` 的最小内部接口
  **单向**依赖 gfx 核心。不用 Markdown 的应用不链接、不携带该模块（§9）；
- **异步完成走失效**：图片加载完成由 `UiImage_SetContext` 自动失效自身矩形；
  链接 hover 动画用 `Ui_AnimDirtyRect`，严格遵守"`WM_PAINT` 内不 `InvalidateRect`"。

## 3. 解析层

### 3.1 数据结构

行内偏移统一用 **UTF-16 码元**（DWrite 的字符单位），源串偏移用 UTF-8 字节。
每个叶子块在解析时一次性从源串生成 UTF-16 展示文本（已解转义/实体），
`MdRun` 直接指向这份文本，绘制时零转换。

源文本先做换行归一（CRLF/CR → LF）；非法 UTF-8 字节按 U+FFFD 替换，
不中断解析。空文档/纯空白得到 0 个块，正常显示空白。

```c
typedef struct MdRun {
    int start, len;      /* UTF-16 码元, 在 block->text16 内 */
    uint16_t style;      /* MD_R_BOLD/ITALIC/STRIKE/CODE/LINK/H1..H6 缩放 */
    int link;            /* -1 或 links[] 下标 */
} MdRun;

typedef struct MdLink {
    int runStart, runEnd;   /* 覆盖的 run 区间 [runStart, runEnd) */
    int urlOff, urlLen;     /* MdDoc.src 内偏移 */
} MdLink;

typedef struct MdBlock {
    uint8_t type;        /* PARAGRAPH/HEADING/CODE/HR/IMAGE/TABLE/QUOTE/LIST */
    uint8_t flags;       /* TIGHT / FENCE_CLOSED / ... */
    uint32_t hash;       /* 内容 + 容器上下文 */
    int srcOff, srcLen;  /* 源行区间 (诊断/重建) */

    wchar_t *text16;     /* 叶子块的展示文本, 动态增长 */
    int len16;
    MdRun  *runs; int runCount, runCap;
    MdLink *links; int linkCount, linkCap;

    struct MdBlock *children; int childCount, childCap;  /* QUOTE/LIST */

    /* CODE */
    int fence;           /* 0=缩进代码, 1=围栏 */
    int langOff, langLen;

    /* LIST */
    int ordered, startNo, tight, task;  /* task: 0无 1未勾 2已勾 */

    /* TABLE */
    int cols, rows, alignCap;
    uint8_t *align;      /* 每列 LEFT/CENTER/RIGHT */
    /* 单元格: rows×cols 的 {text16, runs, links}, 行优先连续分配 */

    /* IMAGE: URL 区间直接复用上面的 srcOff/srcLen */
} MdBlock;

typedef struct MdDoc {
    char *src; int srcLen, srcCap;      /* 自有 UTF-8 副本 */
    MdBlock *blocks; int count, cap;    /* 顶层块 (树序) */
    /* 解析中间态: 容器栈、行缓冲、尾块起始行 (流式) */
} MdDoc;
```

表格单元格复用叶子结构的最小集（text16 + runs + links），由 `MdBlock` 内联数组持有。
所有数组按需 ×1.5~2 增长，无固定上限。

### 3.2 块扫描

逐行状态机，每行按固定优先级判定：

1. **围栏切换**：在围栏代码块内时，只有匹配的闭合围栏（同字符、长度 ≥ 开头）
   生效，其余行原样进代码；
2. **容器前缀剥离**：`>`（可嵌套）→ 列表标记（缩进 + `-*+` / `\d+[.)]` + 空格）
   → 更新容器栈，得到当前容器与缩进；容器内再按 3 之后规则判定内容块；
3. **ATX 标题**：`#{1,6}` + 空格；
4. **Setext**：上一行是段落行且本行全 `=`/`-`，把段落升级为标题；
5. **水平线**：`-`/`*`/`_` ≥ 3 且无其他字符；
6. **表格分隔行**：上一行含 `|` 且本行每个单元格去首尾空白后匹配 `:?-+:?`；
   随后连续含 `|` 的行作为数据行（`\|` 视为字面竖线，单元格只做行内解析）；
7. **缩进代码**：4 空格且不处于列表续行位置；
8. **空行**：结束当前段落；影响紧/松列表判定；
9. **其他**：段落行。**不支持 lazy continuation**（引用内省略 `>` 的续行会
   结束引用），在 §3.5 偏差表登记。

**列表**：无序标记宽度固定（`•` + 空隙），有序取该列表最大序号宽度并右对齐；
`tight` 由项间是否存在空行决定；续行缩进 = 标记列宽 + 空格。
任务列表在项首识别 `[ ]` / `[x]`（大小写不敏感），标记列改画复选框。

**引用定义预处理**：单遍收集 `[label]: url "title"`（可跨行），label 做
大小写折叠 + 内部空白归一，存入 FNV 开放寻址哈希表（容量 = 2×定义数，
按需扩容）；重复定义以第一个为准；引用式链接解析 O(1)，避免"每个链接
线性扫定义表"的 O(n²)。

**资源上限**：容器嵌套深度上限 32（超出部分按普通文本），表格列数上限 64
（超出的 `|` 段并入最后一列），代码块内制表符展开为 4 空格（复制仍取原文）。
上限只用于约束恶意/异常输入的内存与递归深度，正常文档不会触达。

**容器树**：引用与列表是容器块，持有 children。扁平化时不复制内容，只记录
`indent` 与装饰上下文（引用竖条、列表标记、任务态）。

### 3.3 行内扫描

单遍扫描 + 分隔符栈：

1. 反斜杠转义：`\` + 标点 → 字面字符、不参与强调判定；
2. 行内代码：n 个反引号配对同数量反引号，内容不做任何其他解析；
3. 强调：`*`/`_` 按 CommonMark 左右 flanking 规则入栈，闭合时成对出栈，
   内层优先；`~~` 同法（GFM）。未闭合的定界符按字面文本输出；
4. 链接 / 图片：`[text](url)`、`[text][ref]`、`[ref]`、`![alt](url)`；
   括号内 URL 支持尖括号包裹与一层配对括号；
5. 自动链接：`<scheme:...>`、`<mailto:...>`；
6. 实体：常见命名实体表（约 20 个）+ 十进制 / 十六进制数字实体；
7. 硬换行：行尾两个及以上空格或 `\` 在行末 → 行内换行 run。

输出 `runs[]` 按 start 严格递增、互不重叠；`links[]` 指向覆盖的 run 区间。
强调/链接允许嵌套，但 LINK 样式只落在最内层 run 上（避免重叠）。

强调匹配使用分隔符栈 + `openers_bottom` 记忆（按定界符字符、长度模 3、
flanking 状态记录下界），病态输入（成千上万个 `*`/`_`）仍为摊还 O(n)，
不做朴素回溯。

### 3.4 增量策略

| 场景 | 行为 |
| --- | --- |
| `SetText` 全量替换 | 新解析结果逐块与旧块比较 `hash + type + indent`；相同则**迁移旧 block**（复用 text16/runs/links 与已有测量高度），不同则重建 |
| `Append` 流式 | 定位"最后一个可能未闭合的块"（尾围栏 / 尾表格 / 尾列表 / 尾段落），从该块起始行重解析；之前已闭合块不可变 |
| 宽度变化 | 不重解析，只 `UiScrollLayout_SetWidth` 失效测量 |
| 图片状态变化 | 不重解析，单项失效回填高度 |
| 设备丢失 | 不重解析，布局缓存全部释放，按需重建 |

哈希取源字节 + 容器上下文（引用深度/列表类型/缩进），避免同文不同语境误复用。

`SetText` 保留当前滚动偏移（内容缩短时由 ScrollView 夹取）；`Append` 不改动
位置，`followTail` 开启时才滚到底。块结构重建后行索引会变化，但偏移按像素
保留，用户视图基本不动。

### 3.5 与 CommonMark 的已知偏差

| 项 | v1 行为 |
| --- | --- |
| Lazy continuation | 不支持；引用内续行必须带 `>` |
| 行内 HTML / HTML 块 | 按转义纯文本显示 |
| 列表项内多块 | 支持（容器树），但块间距统一按 loose 处理 |
| 强调嵌套的极端情形 | 按分隔符栈主流实现；与规范在罕见重叠场景可能有差异 |
| 裸 URL 自动链接（GFM 扩展） | 不支持，需用 `<...>` 或 `[...](...)` |
| 标题属性 `{#id}` 等扩展 | 不支持 |

## 4. Gfx 富文本接口（独立可选模块）

### 4.1 API（`gfx_rich.h`）

```c
typedef struct GfxRichText GfxRichText;

#define GFX_RICH_WRAP 1u << 0   /* 置位 = 软换行 (Markdown 恒开) */

GfxRichText *Gfx_RichCreate(Gfx *g, const wchar_t *text16, int len16,
                            GfxFont *base, float wrapW, float lineSpacing,
                            uint32_t flags);
void Gfx_RichDestroy(GfxRichText *rt);

/* 颜色: 0 = 用 Gfx_RichDraw 传入的默认色 */
void Gfx_RichColor(GfxRichText *rt, int start, int len, GfxColor color);
void Gfx_RichBold(GfxRichText *rt, int start, int len, int on);
void Gfx_RichItalic(GfxRichText *rt, int start, int len, int on);
void Gfx_RichUnderline(GfxRichText *rt, int start, int len, int on);
void Gfx_RichStrike(GfxRichText *rt, int start, int len, int on);
/* 仅切换字体家族 (字号/字重不变): 行内代码等宽字体 */
int  Gfx_RichFontFamily(GfxRichText *rt, int start, int len, GfxFont *font);

float Gfx_RichWidth(const GfxRichText *rt);
float Gfx_RichHeight(const GfxRichText *rt);

void Gfx_RichDraw(GfxRichText *rt, Gfx *g, float x, float y, GfxColor color);

/* 点 → UTF-16 位置; 返回 1 = 命中文本, 0 = 未命中 */
int Gfx_RichHitTest(const GfxRichText *rt, float x, float y, int *pos16);
/* 范围 → 矩形数组 (换行时可能多个); 返回写入个数, 不足 cap 时截断 */
int Gfx_RichRangeRects(const GfxRichText *rt, int start, int len,
                       GfxRect *out, int cap);
/* 行内对象: 用位图替换 [start,len), 参与断行/行高; 绘制时贴图 */
int Gfx_RichInlineImage(GfxRichText *rt, int start, int len,
                        GfxBitmap *bmp, float w, float h);
```

### 4.2 实现要点（`gfx_rich.cpp` + `gfx_internal.h`）

- **依赖方向（硬约束）**：`gfx_d2d.cpp` 不引用任何 `Gfx_Rich*` 符号；富文本
  通过 `gfx_internal.h` 暴露的最小内部接口（DW 工厂、D2D RT、画刷缓存、
  可见性判断）**单向**依赖 gfx 核心。`gfx.h` 保持不动，`gfx_rich.h` 只被
  `ui_md.c` / `gfx_rich.cpp` 包含，普通应用看不到这层 API。
- `gfx_rich.h` 带 `extern "C"` 守卫（实现为 C++17）；`gfx_internal.h` 只被
  两个 C++ 文件包含，不做 C 链接。
- 内部持 `IDWriteTextLayout` + 一个基格式（由 `GfxFont` 的 `IDWriteTextFormat`
  克隆）。范围样式直接调 `SetFontWeight` / `SetFontStyle` / `SetUnderline` /
  `SetStrikethrough` / `SetDrawingEffect`。
- **行距**：`lineSpacing > 0` 时在克隆格式上
  `SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 字号 × lineSpacing,
  字号 × lineSpacing × 0.8)`，得到与字体自然度量无关的确定行高，测量/绘制/
  命中三者一致；`lineSpacing = 0` 使用字体自然行高。
- 颜色范围用轻量 `IUnknown` 包装（持 `GfxColor`），`Gfx_RichDraw` 时经
  `GetBrush`（现有画刷缓存）解析成 `ID2D1SolidColorBrush`，避免为每种颜色
  新建持久画刷；布局随设备代际（`Gfx_Generation`）由调用方重建。
- `Gfx_RichHeight` 取 `GetMetrics().height`（含换行），`Gfx_RichWidth` 取
  `widthIncludingTrailingWhitespace`（与 `Gfx_TextWidth` 一致）。
- `Gfx_RichRangeRects` 用 `HitTestTextRange`，返回的 rect 是相对布局原点，
  调用方加行偏移。
- 布局**不进** `Gfx_Text` 的 128 条文本缓存（Markdown 自己按块管理），避免
  把编辑控件的热条目挤掉；`Gfx_Text` 路径完全不变。
- 设备丢失时布局本身不持有设备资源，但绘制效果包装持有颜色引用；统一由
  UiMarkdown 在 `Gfx_Generation` 变化时销毁重建，最省心。
- 创建失败（OOM/非法参数）：该行按估算高度留白，下一帧重试；不崩溃、
  不破坏滚动布局。

## 5. 布局层

### 5.1 渲染行模型

容器树扁平化为渲染行，**一个行 = 一个 `UiScrollItem`**。超长内容切分为
多个行（代码块 ≤100 行或 ≤4KB 一片、表格 ≤50 数据行一组、段落 >2000 源行
在行边界切分），保证**单行成本有界**，不随文件规模恶化。

```c
typedef struct MdTable {          /* 表格共享数据, 多行分片共用 */
    int cols, rows;
    uint8_t *align;               /* 每列 LEFT/CENTER/RIGHT */
    float *colW;                  /* 全表统一的列宽 */
    /* 单元格: 行优先 {text16, runs, links}, 解析期生成 */
} MdTable;

typedef struct MdRow {
    MdBlock *blk;        /* 源块 */
    int part, partCount; /* 分片序号 / 总数 (代码、表格、超长段落) */
    int srcLine0;        /* 本片起始源行 (代码分片/表格组) */
    int indent;          /* DIP 缩进 = 容器层级累加 */
    int markerKind;      /* NONE/BULLET/NUMBER/TASK */
    float markerW;       /* 标记列宽 (测量 & 绘制共用) */

    /* 高度: UiScrollItem.height 始终保存"当前可用值"(估算或实测) */
    uint32_t estHash;    /* 估算依据的块 hash */
    int measured;        /* 1 = 已用 DWrite 实测 */
    float measuredW;     /* 实测时的 wrapW */

    /* 富文本布局缓存 (仅实测/绘制时存在) */
    GfxRichText *rt;
    uint32_t rtGen;      /* Gfx_Generation */
    uint32_t rtStamp;    /* 最近使用帧, 驱逐用 */

    /* 图片行 */
    UiImage *img;
    int imgReady;

    /* 表格分片 */
    MdTable *tbl;
    int row0, rowN;      /* 本片覆盖的数据行区间 */
} MdRow;
```

`UiScrollLayout.gap = 0`：块间距作为行高的一部分（标题/代码/表格上下留白不同），
保证滚动定位与可见区间计算简单。

### 5.2 测量

| 块类型 | 高度来源 |
| --- | --- |
| 段落 / 标题 / 列表项 | `Gfx_RichCreate(wrapW = contentW - indent - markerW, lineSpacing)` → `Gfx_RichHeight()` |
| 代码块 | 行数 × 行高 + 2×codePadY；超长行软换行；按 100 行/4KB 分片，片间不重复内边距 |
| 引用容器 | 子块高度之和 + 竖条内边距；竖条与缩进是绘制装饰 |
| 表格 | 列宽全表统一（§5.4）；每组高度 = 组内行高之和；表头单独成一组 |
| 图片 | 未就绪 = `style.imagePlaceholderH`；就绪 = `min(像素宽/DPI, contentW)` 等比，夹 `imageMaxH`；对齐取 title 提示或 `style.imageAlign` |
| 水平线 | 固定 `style.ruleH + 2×style.ruleGap` |

行距来自 `style.lineSpacing`（§4），标题与正文用同一语义，字号差异由字体给。

### 5.3 懒测量与高度估算（规模解耦的关键）

首次 `SetText` **不创建任何 DWrite 布局**：

1. 每行按 `源字符数 × 平均字宽 / wrapW` 估算行数（CJK 与拉丁字符分别计数），
   乘 `lineSpacing` 与类型内边距得到估算高度；`wrapW` 统一取
   `UiScrollView_ContentWidth(sv, rc)`，与滚动条槽位保持一致的可用宽度；
2. 估算值通过 `UiScrollLayout_SetItemHeight` 写入——该 API 要求 `measured=1`
   才计入高度，估算值即"当前可用值"；是否真的实测由 `MdRow.measured` 单独跟踪。
   只在估算值变化（块/宽度变化）时写入，避免每帧触发 `Commit`；
3. 滚动到某行前后，对**可见 ±1 行**实测：`Gfx_RichCreate` + `GetMetrics`；
4. 实测值与当前值差 > 0.5 DIP → `SetItemHeight` 触发 `Commit`，随后做
   **锚点修正**：测量前记录视口首行 `(index, offset - itemY[index])`，
   Commit 后把 offset 调回 `新 itemY[index] + 原偏差`（`ScrollTo(offset, 0)`），
   视口内容不跳动；
5. 宽度变化时所有行回退为估算态（`UI_SCROLL_ITEM_WIDTH_DEPENDENT` + 清零
   `measured`），逐帧按可见行收敛；滚动条总高随之收敛。

每帧顺序固定为：`SetWidth →（首次或宽度变化：重估 + 先 Commit 一次估算布局）
→ 取可见区间 → 实测可见行 → Commit/锚点修正 → ScrollView_Begin → 绘制`。
必须先 Commit 再取可见区间：未 Commit 的 `y` 全是 0，二分查找会把整个文档
当成可见。宽度变化时的全表重估是 O(行数) 的纯浮点操作（只在 resize 时发生），
滚动路径不触发。

`Commit` 是 O(行数) 的纯浮点扫描，只在高度变化时发生（10 万行约 0.05ms，
可接受）；不做每帧强制 Commit。

### 5.4 表格列宽算法

1. 全表统一列宽：从数据行中**均匀采样**求每列"不换行自然宽"（`Gfx_RichWidth`）：
   采样行 ≤64 且总单元格 ≤512（列多时按比例减少采样行数），避免为超大表做
   全量测量；
2. `sum(自然宽) + 单元格水平内边距 ≤ 可用宽` → 直接用自然宽（余量按对齐留白）；
3. 否则每列保底 `tableMinColW`（默认 48 DIP），剩余宽度按自然宽比例分配，
   夹在 `[minColW, 自然宽]`；
4. 采样之外的超长单元格在列宽内换行；表格按 50 数据行一组分片，每组可见时
   实测组内行高（列宽全表一致，跨组对齐）；表头行单独一组，边框按组边界绘制；
5. 列数上限 64（§3.2）；极窄宽退化为等分 + 字符级换行，绘制始终裁剪到行矩形。

### 5.5 缓存与驱逐

- 布局缓存（`GfxRichText`、表格单元格布局、`UiImage`）只保留**视口 ± 32 行**；
  组件维护一个"活跃行"小列表（≤ 视口 + 64），驱逐/清理只遍历该列表，不扫描
  全文档；远处的行释放对象，高度与 `measured` 标志保留，滚回视口时重建
  （一次 `CreateTextLayout`，成本可接受）；
- 每帧绘制后按 `rtStamp` 做过期清理，不额外扫描全文档；
- 图片对象随行销毁时 `UiImage_Destroy`（会取消未完成请求）；创建时
  `UiImage_SetContext(img, ui)`，加载完成自动失效所在行；
- `Gfx_Generation` 变化：释放全部布局与派生字体，下一帧按可见行重建；
  估算高度与 `measured` 一并清零（DPI 可能同时变化）。

### 5.6 滚动集成

- `UiScrollView_Begin(view, ui, rc, contentW, contentH)`，滚动条样式复用
  `UiScrollbarStyle`（默认 reserveSpace），padding 由 `UiMarkdownStyle` 给；
- `UiScrollLayout_VisibleRange` 是二分查找 O(log N)，大文档滚动不产生线性扫描；
- `UiScrollView_Cull` 已在可见区间内；绘制仍用 `Gfx_IsVisible` 跳过脏区外的行；
- **跟随尾部**：`SetFollowTail(1)` 时，内容高度增长且用户原本就在底部附近
  （`maxOff - off < 4 DIP`）→ 自动滚到底；用户上滚则自动取消跟随，回到底部
  再恢复（流式输出的标准行为）。估算态下按当前 contentH 工作，实测修正后
  由锚点补偿，不产生回跳；
- 键盘 PageUp/PageDown / Home/End 由应用转发到 `UiScrollView_ScrollTo`
  （与现有样例一致，组件不抢焦点）。

## 6. 绘制与交互

### 6.1 绘制顺序

```
PushClip(视口 inner)
  行背景: 代码块底 / 表头底 / 隔行底 / 引用底
  装饰:   引用竖条、列表标记、任务复选框、HR
  文本:   Gfx_RichDraw per run (链接色经 Gfx_RichColor 预先打样式)
  链接:   下划线 & hover 动画 (RangeRects + 动画色)
  表格:   边框线 (1px, 主题色 @ 半透明)
  图片:   UiImage_Draw(居中于行矩形)
  代码复制按钮 (hover 才显, 120ms 淡入)
PopClip
滚动条 (UiScrollView_End)
```

所有绘制严格裁剪到行/视口矩形，长表格/超宽图片不越界（框架既有约束）。

### 6.2 链接交互（单控件 ID，组件内命中）

`UI_MAX_WIDGETS = 256`，注册溢出会**静默丢弃**（ui_context.c:267）。逐链接
注册在链接密集文档里必然击穿上限，因此**整篇 Markdown 只注册一个 widget**：

- 每帧 `Ui_State(ui, mdId, viewport, 1)` 注册视口并取 `hovered/pressed`；
- 链接矩形在布局重建时用 `Gfx_RichRangeRects` 缓存在行里（换行时多段）；
  当 `st.hovered` 时把鼠标位置换算到内容坐标（减去 ScrollView 偏移），在
  **可见行**的链接矩形中命中，得到 `hoverLink`——命中完全在组件内部完成，
  不注册额外控件；
- hover 动画按组件内生成的链接 ID（md id + row + link 经 FNV 组合）调
  `Ui_AnimSmooth`（动画通道不要求控件注册），动画期间
  `Ui_AnimDirtyRect(链接矩形)`；链接色随之在 `link` ↔ `linkHover` 间插值，
  动画帧才对该范围重设 `Gfx_RichColor`；
- 按下时（`ctx->down == mdId`）记录 `downLink`；抬起时
  `ctx->clicked == mdId` 且鼠标仍在同一链接 → 触发 `onLink` + `clickedUrl`；
- 悬停链接时 `Ui_SetCursor(ui, mdId, UI_CURSOR_HAND)`（每帧重新声明，
  离开链接自动回箭头）；
- 库**不内置导航**，由应用决定（建议白名单 scheme 后 `ShellExecuteW`）。

控件注册数与链接数量无关：整个窗口的 256 上限只消耗"Markdown 实例数 × 1"，
ScrollView 自身的注册不受影响。链接是否换行、是否与其它 run 相邻都不影响
命中——矩形来自 DWrite 实测。

### 6.3 代码块复制

- 代码块 hover 时右上角出现复制按钮（24×24，图标用 `theme->fIcon` 的 MDL2
  字形；无图标字体时退化为"复制"小字）；
- 与链接共用同一个 widget 命中：按钮矩形优先于链接矩形；
- 点击把该块**完整原始代码文本**（UTF-16，跨分片合并）写入剪贴板：
  `OpenClipboard` / `EmptyClipboard` / `GlobalAlloc` /
  `SetClipboardData(CF_UNICODETEXT)`，与 `ui_textarea.c` 的复制逻辑同构
  （约 20 行，v1 在 `ui_md.c` 内实现）；
- 成功后按钮短暂变勾（300ms）作为反馈。

### 6.4 装饰细节

- **引用**：左侧 3 DIP 竖条（`style.quoteBar`）+ 整块浅底（`style.quoteBg`，
  alpha 低）；竖条随引用深度缩进；
- **列表标记**：无序 `•`（`style.markerColor`），有序右对齐 `N.`；任务列表
  画 14×14 圆角方框，勾选态填充 `theme->primary.base` + 白色勾；
- **HR**：`1px` 主题边框色，宽度 = 内容宽；
- **表格**：外框 + 表头下边线 + 列分隔线，全部 1px `style.tableBorder`；
  单元格内边距 `tableCellPadX/Y`；对齐按 `align[]` 用 DWrite 段落对齐。

## 7. 公共 API

```c
typedef struct UiMarkdown UiMarkdown;   /* 不透明, 应用持有 */

typedef struct UiMdSpan { int start, len; uint8_t style; } UiMdSpan;
/* style 用 MD_TOK_* 位 (KEYWORD/STRING/COMMENT/NUMBER), 颜色取 style.tok* */

typedef void (*UiMarkdownLinkFn)(void *user, const char *url);
typedef void (*UiMarkdownCopyFn)(void *user, const wchar_t *text16, int len16);
/* 返回写入 out 的 span 数; start/len 为 UTF-16 码元 */
typedef int (*UiMarkdownTokenizeFn)(void *user, const char *lang,
    const wchar_t *text16, int len16, UiMdSpan *out, int cap);

typedef struct UiMarkdownStyle {
    /* 字体: NULL = 按主题字号/字重自动创建 (h1..h6 由 body 梯度派生) */
    GfxFont *h1, *h2, *h3, *h4, *h5, *h6;
    GfxFont *body, *bold, *italic, *code, *tiny; /* tiny: Windows 头里 small 是宏 */

    float blockGap;         /* 普通块间距 (默认 10) */
    float lineSpacing;      /* 行高倍数, 0 = 1.25; 单一语义, 只管行距 */
    float headingGapAbove, headingGapBelow;
    float listIndent, quoteIndent, quoteBarW;
    float codePadX, codePadY, codeRadius;
    float tableCellPadX, tableCellPadY, tableMinColW;
    float imageMaxH, imagePlaceholderH;
    int   imageAlign;       /* 默认图片对齐: 0=左 1=中 2=右 */
    float ruleH, ruleGap;

    UiColor text, text2, link, linkHover;
    UiColor codeText, codeBg, codeBorder;
    UiColor quoteBar, quoteBg, tableBorder, tableHeaderBg;
    UiColor ruleColor, markerColor;
    UiColor tokKeyword, tokString, tokComment, tokNumber;  /* tokenizer 用 */
} UiMarkdownStyle;

/* 内置两套样式: 返回完整具体值 (字体项为 NULL, 由组件按主题派生) */
UiMarkdownStyle UiMarkdownStyle_Light(void);
UiMarkdownStyle UiMarkdownStyle_Dark(void);
/* 从当前主题派生一套具体值 (浅/深随 themeId) */
UiMarkdownStyle UiMarkdownStyle_FromTheme(const UiTheme *t);

UiMarkdown *UiMarkdown_Create(Gfx *gfx, UiId id, UiNet *net /* 可 NULL */);
void UiMarkdown_Destroy(UiMarkdown *md);

void UiMarkdown_SetText(UiMarkdown *md, const char *utf8);     /* 全量, 内部 diff; NULL = 清空 */
void UiMarkdown_Append(UiMarkdown *md, const char *utf8);      /* 流式 */
void UiMarkdown_SetStyle(UiMarkdown *md, const UiMarkdownStyle *st); /* 拷贝; NULL=跟随主题 */
void UiMarkdown_SetBasePath(UiMarkdown *md, const wchar_t *dir);     /* 相对图片路径 */

void UiMarkdown_SetLinkFn(UiMarkdown *md, UiMarkdownLinkFn fn, void *user);
void UiMarkdown_SetCopyFn(UiMarkdown *md, UiMarkdownCopyFn fn, void *user); /* 替代默认剪贴板写入 */
void UiMarkdown_SetTokenizeFn(UiMarkdown *md, UiMarkdownTokenizeFn fn, void *user);

/* 绘制 + 输入; 返回 1 = 本帧有事件 (链接点击/复制) */
int  Ui_Markdown(UiMarkdown *md, UiContext *ui, UiRect rc);

const char *UiMarkdown_ClickedUrl(const UiMarkdown *md);  /* 本帧有效, 否则 NULL */
int  UiMarkdown_Busy(const UiMarkdown *md);   /* 有图片加载/动画 → 继续出帧 */
void UiMarkdown_ScrollToTop(UiMarkdown *md);
void UiMarkdown_SetFollowTail(UiMarkdown *md, int on);
float UiMarkdown_ContentHeight(const UiMarkdown *md);
```

语义约定：

- `Create` 时 `Gfx` 必须已初始化；`Destroy` 必须在 `Gfx_Shutdown` 之前；
- `id` 是整篇内容的根控件 ID（`Ui_State` 注册与链接/动画 ID 的根），同一帧内
  多个实例必须不同；
- `SetStyle` 深拷贝数值字段，并清空实测高度（按新字体/行距/间距重估重测）；
- `GfxFont*` 是借用指针：应用提供的字体必须比 `UiMarkdown` 活得久，为 NULL
  的字体项由组件按主题字号派生并在 `Destroy` 时释放；`Gfx_Generation`
  变化时派生字体自动重建；
- **默认跟随主题**（`SetStyle(md, NULL)`）：颜色每帧从 `ui->theme` 解析，
  全局浅/深切换自动生效；`Light/Dark()` 返回具体值快照，设置后不再跟随
  全局换肤（可用 `FromTheme` 重新取值），应用只改某一个字段即可定制；
- `lineSpacing` 只控制行距（0 = 1.25）；段间距由 `blockGap` /
  `headingGapAbove/Below` 控制，语义不重叠；
- 所有 API 与回调都在 GUI 线程（`UiNet` 回调本身归位到 GUI）；
- `Busy` 只统计**可见行**的图片加载与动画；推荐 `animating` 回调返回
  `Ui_AnimBusy(ui) || UiMarkdown_Busy(md)`（hover 过渡由动画内核驱动）；
- 流式场景：`Append` 后无需其它调用，尾块重解析 + 高度回填 + 跟随尾部自动完成；
- `UiMarkdown_ScrollToTop` 同时取消跟随尾部（直到再次 `SetFollowTail(1)` 或
  用户主动滚回底部）；
- 图片源解析顺序：`http(s)://` → `UiNet`；绝对路径或 `basePath` 拼接后的
  相对路径 → 本地文件；其它 → 行内占位（显示 alt）。

## 8. 性能与内存

**复杂度保证（数据规模增大时单帧成本不增长）**：

| 维度 | 保证 |
| --- | --- |
| 解析 | 单遍扫描 O(n)；强调分隔符栈带 `openers_bottom` 记忆，病态定界符仍摊还 O(n)；引用定义走哈希表 O(1) |
| 首帧 | 只做 O(块数) 估算，**不创建 DWrite 布局**；10k 行 / ~500KB 目标 ≤ 20ms（/O2） |
| 流式追加 | `Append` 只重解析尾块，O(尾段长度)，与文档总长无关 |
| 滚动每帧 | O(可见行) 实测/绘制 + O(log N) 可见区间 + O(N) 纯浮点 Commit（仅高度变化时） |
| 单块成本 | 代码 ≤100 行/4KB 分片、表格 ≤50 行/组、段落 >2000 行切分：单行成本有界 |
| 控件注册 | 每实例 1 个 widget，与链接数量无关（256 上限不再构成约束） |
| 内存 | 源文本 1 份 + runs（≈每 16 字符 1 个）；估算态每行 O(1)；布局缓存 ≤ 视口 + 64 行 |
| 脏区 | 链接/复制按钮动画 → `Ui_AnimDirtyRect`；图片完成 → `Ui_ContextInvalidate` |
| 空闲 | 无动画、无加载时不出帧（`Busy` 为 0） |

文件复杂度（块数、链接数、表格规模、代码块长度）只影响解析时间与内存占用，
不影响滚动/绘制帧成本；首屏滚动条长度在实测收敛前是估算值。

框架不变量照旧：`WM_PAINT` 内不调 `InvalidateRect`；逐帧动画只登记动画脏区；
设备丢失按 `Gfx_Generation` 重建。

## 9. 构建接入（可选模块隔离）

核心 `CORE_OBJS` **不加入** `gfx_rich.cpp` / `ui_md*.c`：

| 目标 | 链接的额外对象 |
| --- | --- |
| `cui_markdown.exe`（样例） | `gfx_rich.obj` + `ui_md_parse.obj` + `ui_md.obj` |
| `cui_md_test.exe`（headless） | 同上（纯解析断言可只链 `ui_md_parse.obj`） |
| 其余全部 exe | 不链接，OBJS 保持现状 |

- `gfx_rich.cpp` 用与 `gfx_d2d.cpp` 相同的 CXXFLAGS 单独编译成 obj；
  `gfx_d2d.obj` 不引用它，没有它也能链接其余应用；
- 三个 obj 只出现在上述两个目标的链接行，其他 exe 的符号表与体积不受影响；
  可选模块变多后可打包成 `build\cui_md.lib`（静态库按需拉取成员），当前直接
  列 obj，避免多一步 lib 工具链；
- `build.cmd` 目前是单次全量构建：可选模块的 obj 仍会编译一次，但**不进入
  其他 exe 的链接与产物**；若要连编译都按目标裁剪，需要给 build.cmd 增加
  目标参数（后续，不影响当前隔离目标）；
- `ui_md.h` 是应用唯一需要包含的头；`gfx_rich.h` 只被组件内部包含；
- 新增样例 `samples/markdown.c` → `bin\cui_markdown.exe`；
- 新增 headless 测试 `tests/md_test.c` → `bin\cui_md_test.exe`（CONSOLE）：
  块/行内/嵌套/表格/增量 diff 断言，不依赖窗口；
- 富文本复用现有 dwrite/d2d 链接，**无需新增 .lib**；
- `build.cmd` 必须保持 CRLF（已知：LF 会静默挂起），只用编辑器修改。

## 10. 分步实施计划

| 阶段 | 内容 | 验收 |
| --- | --- | --- |
| 1 | Gfx 富文本接口（`gfx_rich.h` + `gfx_internal.h` + `gfx_rich.cpp`） | 隐藏窗口断言：混合样式段落高度/宽度、RangeRects 矩形非空、HitTest 命中位置 |
| 2 | 解析器（块扫描 + 行内扫描 + 引用定义） | `md_test` 金样断言 ≥ 120 条（含嵌套列表/引用/表格/强调边界/实体） |
| 3 | 行模型 + 懒测量/估算 + 滚动（纯文本块可见） | 样例显示标题/段落/列表，resize 重排正确；10k 行首帧 ≤20ms，滚动时才实测（计数器断言） |
| 4 | 装饰：代码块/引用竖条/HR/表格/任务列表 | 截图覆盖全部块类型；窄宽表格不越界 |
| 5 | 链接命中/hover/点击 + 代码复制 | 原生点击链接回调收到 URL；hover 动画可见；复制后剪贴板内容正确 |
| 6 | 块级图片（UiImage 接入、高度回填、跟随尾部） | URL/文件图片加载、spinner、淡入；加载完成后布局无跳动 |
| 7 | 流式追加 + 大文档压测 + 4K@192 DPI + 文档 | Append 10k 行流式输出不卡顿；白皮书 §8.21 / README / 本文档同步 |

## 11. 验证判据

| 项 | 判据 |
| --- | --- |
| 块类型 | 6 级标题、Setext、段落、围栏/缩进代码、引用嵌套、有序/无序/任务列表、HR、表格、图片、`:::`/GitHub 警报容器逐项截图 |
| 行内 | 粗体/斜体/粗斜体、删除线、行内代码、链接、自动链接、转义、数字/命名实体、硬换行 |
| 强调边界 | `**a *b* c**`、`a*b*c`（星号允许词内）、`a_b_c`（下划线不允许词内）、未闭合 `*`、`~~` 嵌套 |
| 表格 | 对齐（左/中/右）、`\|` 转义、窄宽换行、超宽裁剪、≥50 行分组后跨组列宽一致 |
| 链接 | hover 变色 + 手型光标 + 点击回调（真鼠标点击）；换行链接多矩形命中 |
| 复制 | 代码块按钮点击后剪贴板为原始代码（含缩进/换行，跨分片完整） |
| 图片 | 块级 URL（本地测试服务器）spinner → 淡入；文件相对路径；失败占位 |
| 增量 | `SetText` 只重建变化块（内部计数断言）；`Append` 与整体重解析结果一致（金样对比） |
| 流式 | 跟随尾部：到底自动跟随、上滚取消、回到底部恢复 |
| 行距 | `lineSpacing` 0.9 / 1.25 / 1.6 三档：行高按比例变化、多行不重叠、命中矩形一致 |
| 样式 | Light / Dark 预设截图；跟随主题时浅深切换自动变色；显式样式只改单字段生效 |
| 规模 | 10k 行含 2000 链接：注册 widget 计数 ≤ 8（断言），链接全部可点；首帧不建布局（计数断言） |
| 大块 | 单代码块 5000 行：分片渲染、滚动流畅、复制得到完整原文；10000 行段落切分正常 |
| 嵌套/上限 | 100 层引用嵌套不崩溃（32 层截断）；65 列表格按 64 列截断，不越界 |
| 健壮性 | 空文档 / 纯空白 / 非法 UTF-8 / CRLF / 未闭合围栏均不崩溃，渲染有界 |
| 估算收敛 | 懒测量下滚动条长度逐步收敛，视口内容不跳动（锚点修正） |
| 性能 | 10k 行首解析+估算 ≤ 20ms；滚动无线性扫描；空闲零出帧 |
| DPI | 4K@192（scale 2）字号/行距/表格线清晰，无半像素模糊 |
| 设备 | 改变窗口尺寸触发 Gfx 重建后内容与图片正常 |
| 构建 | `cui_markdown.exe` / `cui_md_test.exe` 0CRT；全部样例回归 |
| 解耦 | 其余 exe 的符号表/链接映射中无 `Gfx_Rich*` / `UiMarkdown*` / `MdParse*`；`gfx_d2d.obj` 可脱离 `gfx_rich.obj` 正常链接 |

## 12. 风险与后续

- **强调算法**：CommonMark 分隔符栈在极端嵌套下与规范可能有细微差异，
  金样用例以规范示例集为主，偏差登记在 §3.5；
- **表格列宽采样**：自然宽只采样 ≤64 行，更靠后的超长单元格会在列宽内换行，
  个别大表可能不如全量测量紧凑（换取有界的首测成本）；
- **表格极窄宽**：退化为等分 + 字符级换行，绘制始终裁剪，不追求可读性最优；
- **估算高度**：首屏滚动条长度是估算值，随实测收敛；锚点修正保证内容不跳，
  但滚动条 thumb 长度会在最初几帧微调；
- **Commit O(N)**：高度修正触发的 `Commit` 是纯浮点全表扫描，百万行级才会
  显现；v2 可改为前缀和增量更新；
- **后续**：全文选区复制（`HitTestTextRange`）、行内图片（`IDWriteInlineObject`）、
  内置通用高亮 tokenizer、标题锚点/目录、脚注；
- **安全**：库不执行导航；应用对 `file:`/`javascript:` 等 scheme 做白名单，
  图片仅允许 `http(s)` 与本地路径。

## 13. 实施记录

### 阶段 1（2026-11-13，已完成）

新增文件：

- `src/gfx/gfx_internal.h`：可选模块与 gfx 核心之间的最小内部接口
  （`Gfx_IntReady/DWrite/RT/Brush/Visible/FontFormat/FontSize/FontWeight`），
  实现在 `gfx_d2d.cpp` 末尾；接口不暴露 `Gfx` 布局。
- `src/gfx/gfx_rich.h` / `gfx_rich.cpp`：`IDWriteTextLayout` 封装
  （创建/销毁、范围粗斜/下划线/删除线/颜色、行距、宽高测量、绘制、
  `HitTest`、`RangeRects`）；范围参数越界自动裁剪。
- `tests/gfx_rich_test.c`：隐藏窗口 + ULW 路径断言 21 项
  （窄宽换行、行距倍数、范围样式、RangeRects 非空、HitTest 命中/越界、
  一帧实际绘制）。
- `build.cmd`：`gfx_rich.obj` **不进入 OBJS**，仅
  `cui_gfx_rich_test.exe` 的链接行追加；其余 15 个 exe 的链接与产物不变。

验证结果：

| 项 | 结果 |
| --- | --- |
| 构建 | 16 个 exe 全部通过，全部 0CRT |
| 富文本断言 | `cui_gfx_rich_test.exe`：21 passed / 0 failed |
| 回归 | `cui_net_test.exe`：877 passed / 0 failed |
| 解耦 | `dumpbin /symbols gfx_d2d.obj` 无任何 `Gfx_Rich*` 引用；`gfx_rich.obj` 定义齐全 |
| 链接隔离 | 非 Markdown 目标链接行不含 `gfx_rich.obj`，产物不受影响 |

### 阶段 2（2026-11-13，已完成）

新增：

- `src/ui/ui_md_parse.h` / `ui_md_parse.c`：块扫描（ATX/Setext 标题、段落、
  围栏/缩进代码、引用、有序/无序/任务列表、HR、GFM 表格、引用定义、块级图片）、
  行内扫描（实体、转义、代码 span、强调/删除线、行内/引用/自动链接、图片、
  硬换行）、内容哈希；纯解析层，不依赖 gfx/window。
- `tests/md_test.c`：288 项金样断言（16 类，含 CRLF、非法 UTF-8、100 层引用、
  80 列表格截断、引用前向解析、重复定义首个生效、哈希稳定性）。
- `build.cmd`：`ui_md_parse.obj` **不进入 OBJS**，仅 `cui_md_test.exe` 链接。

实现中修正：

1. `HeapReAlloc(NULL, ...)` 在目标环境不可靠 → 空数组统一走 `HeapAlloc`
   （`md_grow` / `wbuf_reserve` / `inl_reserve`）。
2. 强调匹配的 `openers_bottom` 下界排除了"当前定界符自身作为 opener"的可能
   （`a*b*c`、`*a **b** c*` 失配）→ 搜索边界改为包含当前项。
3. 末尾换行不再生成额外空行，代码块/段落高度稳定。

验证结果：

| 项 | 结果 |
| --- | --- |
| 构建 | 17 个 exe 全部 0CRT |
| 解析金样 | `cui_md_test.exe`：288 passed / 0 failed |
| 回归 | `cui_gfx_rich_test.exe` 21/0；`cui_net_test.exe` 877/0 |

### 阶段 3（2026-11-13，已完成）

新增：

- `src/ui/ui_md.h` / `ui_md.c`：UiMarkdown 组件——行模型（叶子块 ↔
  UiScrollItem）、懒测量与锚点修正、ScrollView 集成、段落/标题/列表/引用/
  代码绘制；表格/图片先画占位；浅/深/跟随主题样式。
- `samples/markdown.c` → `bin\cui_markdown.exe`：标题、段落、引用、代码、
  任务列表、占位表格/图片；滚轮、追加 50 行、回到顶部。
- `tests/md_ui_test.c` → `bin\cui_md_ui_test.exe`：隐藏窗口，10k 行文档
  （2642 渲染行），27 项断言（首帧成本、懒测量计数、滚轮增量、宽度重排）。
- `build.cmd`：`ui_md.obj` / 样例 / 测试均不进入 `CORE_OBJS`，只链接到
  markdown 目标。

实现中修正：

1. 批量替换把 `md_realloc_` 自身改成了自递归（尾调用优化成死循环），
   `Append` 时 100% CPU 卡死；恢复为 `HeapReAlloc`。
2. 行高迁移时 `layout.width` 仍为 0，下一帧 `SetWidth` 把迁移项的
   `measured` 失效，而估算又跳过 `row->measured` 的行 → y 塌缩、回到顶部
   仍显示中段。重建后先 `SetWidth` 再迁移。
3. 样式字段 `small` 与 Windows 头历史宏冲突，改名 `tiny`。

验证结果：

| 项 | 结果 |
| --- | --- |
| 构建 | 19 个 exe 全部 0CRT |
| 10k 行 | rows=2642；parse 3.6ms；首帧 10.3ms；次帧 3.3ms |
| 懒测量 | 首帧 measured=18 / calls=18（<80）；滚轮与宽度变化后仍只保留可见行实测 |
| 原生 UI | 4K@192：标题/段落/列表/引用/代码/任务列表/占位表格图片；滚轮、追加、回顶均正常 |
| 回归 | `cui_md_test` 288/0；`cui_gfx_rich_test` 21/0；`cui_net_test` 877/0 |

### 补充：真实图片与真实表格（2026-11-13，已完成）

图片：

- 块级图片接入 `UiImage`：`http(s)://` 走 `UiNet`，其余按绝对路径或
  `basePath` 拼接后走 `UiImage_LoadFile`；spinner/淡入/设备丢失重建由
  `UiImage` 继承。
- 显示尺寸：像素宽高先按 DPI 折算成 DIP，再等比夹到内容宽与 `imageMaxH`；
  解码完成前按 `imagePlaceholderH` 占位，完成后回填行高。
- **对齐**：标准 Markdown 没有图片对齐语法，v1 用 title 关键字
  `"left"`/`"center"`/`"right"` 作为逐图提示；未指定时取
  `UiMarkdownStyle.imageAlign`（默认居中）。
- 新增 `UiImage_PixelWidth/Height` 供上层做等比排版。

表格：

- 列宽：均匀采样 ≤64 行求自然宽，总量不超过可用宽直接使用；否则按
  `tableMinColW` 保底后按自然宽比例分配。
- 单元格：`Gfx_RichText` 在列宽内换行，行高取同列/同行最大值；表头底色、
  row/column/外框 1px 边框、左/中/右对齐按 `align[]` 绘制。
- 表格整体仍是**一个滚动项**（列宽需全表一致）；≤50 行分组的大表切分
  仍留在后续阶段，超大表内存/入场成本尚未做上界。

用例与根因修正：

- **固定缓冲区**：测试曾用 `char line[128]` + `wsprintfA` 构造长中文段落，
  写爆栈导致间歇崩溃；样例也在用固定文档缓冲。全部改为动态 `DynBuf`
  （`vsnprintf` 写满即扩容重试），内容不设上限。
- **WIC 需要 COM**：隐藏窗口测试补 `CoInitializeEx`，否则 WIC 工厂创建失败，
  图片退化为占位（内容高度 130 而非 42）。
- 解析器图片对齐字段与表格 `align` 数组成员重名，改名 `imgAlign`。

验证：

| 项 | 结果 |
| --- | --- |
| 解析金样 | 300 passed（新增 left/center/right/默认对齐断言） |
| UI 断言 | 40 passed（10k 行、表格高度、图片文件解码后高度 42、宽度重排） |
| 原生 4K@192 | 三张 BMP 分别左/中/右对齐；表格表头、边框、列对齐正确 |
| 回归 | `cui_gfx_rich_test` 21/0、`cui_net_test` 877/0；19 exe 0CRT |

### 补充：样例功能（2026-11-13）

- 浅/深主题切换：按钮调用 `UiTheme_SetTheme`，Markdown 默认跟随主题，
  正文/代码底/引用/表格/边框颜色即时更新。
- 打开本地 Markdown：`GetOpenFileNameW` + 动态读取（上限 32MB），支持
  UTF-8（含 BOM）、UTF-16LE/BE BOM；载入后 `SetText` 并回到顶部。
  `comdlg32.lib` 只链接到 `cui_markdown.exe`，核心/其它样例不受影响。
- 图片仍由样例生成 BMP 后按文件路径加载，用于验证对齐与懒加载。

验证：

| 项 | 结果 |
| --- | --- |
| 主题 | 深色截图中正文/代码底/引用/边框/表格同步变色，状态栏显示主题 |
| 打开文件 | 通过文件对话框载入 `cui_md_open_test.md`，标题/列表/表格正确渲染 |

### 补充：行内图片 / 链接点击 / 语法高亮（2026-11-13，已完成）

行内图片：

- `Gfx_RichInlineImage` 用自实现 `IDWriteInlineObject` 替换 alt 文本范围，
  参与断行与行高；布局绘制完成后按 `HitTestTextRange` 实测矩形贴位图。
- Markdown 渲染层为每个行内图片维护一个 `UiImage`；解码完成后重建该行
  `GfxRichText` 并回填行高，加载中显示 alt 文本。
- **行距陷阱**：`DWRITE_LINE_SPACING_METHOD_UNIFORM` 固定行高会压扁行内对象，
  含行内图片的段落改用自然行距。

链接：

- 链接 run 在布局创建时应用链接色 + 下划线；命中用 `Gfx_RichHitTest`
  （点 → UTF-16 位置）+ 链接字符区间，不再做矩形换算。
- 单 widget（整篇一个控件 ID）：`pressed == mdId` 时记录按下点；
  `clicked == mdId` 且抬起位置仍在同一链接才触发；回调 + `clickedUrl`
  本帧有效。悬停时手型光标，**仅切换链接文字颜色**（沿用布局自带下划线）。

语法高亮：

- 内置通用词法器：`//`、`/* */`、`#` 注释；`" ' \`` 字符串；数字；常见
  C/C++/JS/TS/Java/C#/Go/Rust/Python 关键字 → 主题 token 颜色。
- `UiMarkdown_SetTokenizeFn` 优先于内置词法器；span 使用
  `MD_TOK_KEYWORD/STRING/COMMENT/NUMBER`。

实现中修正：

1. `gfx_rich.cpp` 行内图片记录数组首次分配又踩 `HeapReAlloc(NULL)`，
   空指针改 `HeapAlloc`。
2. 链接点击最初依赖单帧 `ctx->down`，按下与抬起之间没有绘制帧时丢失；
   改为基于 `pressed` 状态的 pressActive 跟踪。
3. 行内图片高度被 UNIFORM 行距压住（见上）。

验证：

| 项 | 结果 |
| --- | --- |
| 行内图片 | UI 断言：段落行高 27.5 → 42；4K 截图行内小图与文字基线一致 |
| 链接 | UI 级命中已通；原生点击状态栏显示 `https://example.com/markdown`，hover 手型 |
| 高亮 | 注释灰、关键字红、字符串/数字蓝，深色主题同步 |
| 回归 | `md_test` 305/0、`md_ui_test` 44/0、`gfx_rich_test` 26/0、`net_test` 877/0 |

### 补充：悬停/滚动/换肤回归修正（2026-11-13，已完成）

问题与修正：

1. **悬停下划线叠加**：之前悬停时在链接下方再填一条 2px `cLinkHover`，
   与布局自带下划线叠加后明显变粗。改为只对链接字符区间做
   `Gfx_RichColor`（`cLink` ↔ `cLinkHover`），离开/悬停目标切换时恢复
   基础色；布局重建/逐出时清空记录，避免对已销毁布局写颜色。
2. **关窗后进程残留**：样例 `startup` 只调用 `UiApp_Run` 后返回，未走
   `ExitProcess`；改为 `ExitProcess((UINT)UiApp_Run(...))`，与其它样例一致。
3. **滚轮/拖 thumb 被吞掉**：行高实测导致 `contentH` 变化时，锚点校正会用
   `ScrollTo(anchorTarget, 0)` 把 `target` 硬拉回当前 `offset`，正在进行的
   滚轮平滑动画被逐帧掐死。改为仅在滚动空闲（无滚轮待消费、未拖 thumb、
   `target == offset` 且速度为 0）时做锚点校正。
4. **换肤后行内颜色不更新**：链接色与代码 token 色在创建 `GfxRichText`
   时烙进布局。记录 `UiTheme.themeRev`，变化时释放并重建全部行布局，
   链接/代码高亮随主题更新。
5. **打开文件对话框无限重入**：模态 `GetOpenFileNameW` 在 `on_frame`
   内调用时，其模态消息循环会把 `WM_PAINT` 重新派发回主窗口；此时外层
   帧尚未 `Ui_EndFrame`，`clicked` 仍是"打开文件"按钮，于是再次进入
   按钮分支、再次弹框，形成无限创建。改为按钮只置 `wantOpen` 标志，
   在 `after_frame`（`Ui_EndFrame` 之后、`Gfx_End` 之后）执行模态对话框。

验证：

| 项 | 结果 |
| --- | --- |
| 悬停 | 4K@192 截图：悬停仅颜色变化（像素均值 109,138,255 → 120,148,251），下划线像素数不变（3015），离开完全恢复 |
| 点击 | 状态栏显示 `链接: https://example.com/markdown` |
| 换肤 | 深色下代码 token/链接颜色正确重建，不再出现深底暗字 |
| 打开文件 | 连续两次点击各只出现 1 个 `#32770` 对话框；Esc 关闭后归零，无层叠/重入 |
| 退出 | 关闭窗口后 `HasExited=True`、`ExitCode=0`，tasklist 无残留 |
| 回归 | `md_test` 305/0、`md_ui_test` 44/0、`gfx_rich_test` 26/0、`net_test` 877/0；19 exe 0CRT；build.cmd CRLF 201/201 |

### 补充：行内代码呈现（2026-11-13，已完成）

解析层原本已支持 code span（n 个反引号配对、内容不做其他解析、换行转空格、
首尾各一个空格的裁剪、未配对反引号按字面输出）；本轮补齐**视觉呈现**：

- `gfx_rich` 新增 `Gfx_RichFontFamily(rt, start, len, font)`：从 `GfxFont`
  的 `IDWriteTextFormat` 取字体家族，仅对范围切换家族，字号/字重/斜体保持
  不变；越界按现有 `rich_clamp_range` 语义裁剪，参数非法返回 0。
- 行内代码 run 在应用 `cCodeText` 颜色之外，再切换为代码字体家族
  （`style.code`，默认 Consolas），字号跟随所在行——标题内不会缩成 13px。
- 段落/标题/列表沿用原有 `RangeRects` 圆角底色；**表格单元格**现在同样
  在文字前绘制行内代码底色，与正文一致。
- 样例新增演示：`` `code` ``、多反引号 ``a`b``、`**不解析**`、`[非链接](x)`
  与表格单元格内代码。

验证：

| 项 | 结果 |
| --- | --- |
| gfx_rich 断言 | 34 passed / 0 failed（新增家族切换、宽度变化、越界裁剪、空参 6 项） |
| 原生 4K@192 | 正文/列表/表格中的行内代码均为等宽字体 + 底色；多反引号与"不解析"示例正确 |
| 深色主题 | 行内代码底色与文字随主题重建 |
| 回归 | `md_test` 305/0、`md_ui_test` 44/0、`net_test` 877/0；19 exe 0CRT |

## 14. 容器指令与行内代码样式（设计，2026-11-13）

### 14.1 语法

- VuePress/Docusaurus 风格容器：
  `::: <kind> [标题]` … `:::`；kind 大小写不敏感，支持
  `note/info`、`tip/hint`、`warning/attention`、`danger/caution/error`；
  未识别的首词按 `note` 处理，整段 info 作为标题；标题缺省用本地化默认值。
  围栏为 ≥3 个反引号后的冒号（`:::`）；闭合行须 ≥ 开启行冒号数且其后只有空白。
  未闭合的 `:::` 不吞内容，回退为普通段落字面量。
- GitHub 警报：`> [!NOTE]` / `[!TIP]` / `[!IMPORTANT]` / `[!WARNING]` /
  `[!CAUTION]`（大小写不敏感）。首行标记从引用内容中剥离，其余行按普通块
  递归解析；未识别标记（如 `[!FOO]`）保持原样。

### 14.2 模型

- 新增块类型 `MD_BLOCK_CONTAINER`，字段 `boxKind`（`MD_BOX_*`）与
  `titleOff/titleLen`（`MdDoc.urls` 池，UTF-8）。子块递归存放，可嵌套；
  GitHub 警报复用同一块类型。
- 哈希混入 `boxKind` 与标题文本；渲染行沿用现有叶子块 flatten 机制。
- UiMarkdown 在 flatten 时按容器记录盒子 `{kind, firstRow, lastRow, indent,
  title}`；首行额外预留标题高度，盒子背景/色条按行区间整体绘制并按视口裁剪。

### 14.3 样式

- 新增 `UiMarkdownStyle.inlineCodeBg / inlineCodeText`：行内代码与代码块
  语义分离；浅色默认琥珀底 `#FFF4E5` + 深琥珀字，深色默认 `#3A2A16`
  + 亮琥珀字，可由应用覆盖。
- 容器色条取主题语义色（note=primary、tip=success、warning=warning、
  danger=error），底色按浅/深主题固定低饱和色；标题用 `fSmall` 加粗语义色。
- 半径 6 DIP，左侧色条 3 DIP，正文缩进 `quoteIndent`；
- **卡片内边距统一**：标题带 32 DIP（顶距 8 + 标题 16 + 到正文 8），
  正文末行额外 8 DIP 底内边距；代码块同样上下各 `codePadY(8)`，卡面从
  行顶画到行底减去 `blockGap`。外间距仍由 `blockGap`/标题间距承担，
  代码块与容器卡的内边距因此一致（此前代码块 8/0、容器标题 2/正文 0）。

### 实施记录：容器与配色（2026-11-13，已完成）

- 解析器新增 `MD_BLOCK_CONTAINER`、`MD_BOX_NOTE/TIP/WARNING/DANGER`；
  `:::` 容器支持 `<kind> [标题]`、默认标题、未知类型回退、未闭合回退段落、
  嵌套（闭合行冒号数 ≥ 开启行）；GitHub `> [!NOTE]` 等警报在引用首行识别，
  剥离标记后按普通块递归解析；哈希混入 kind 与标题。
- UiMarkdown flatten 生成容器盒子 `{kind, firstRow, lastRow, indent, depth,
  title}`；首行 `extraTop` 累加标题带高度，盒子按 `depth*boxTitleH` 下移，
  外盒先于内盒绘制（先占位后展开），绘制顺序保证嵌套正确；盒子按
  `inner` 显示坐标裁剪，末行收在块间距之前留出卡片间隙。
- 行内代码：`Gfx_RichFontFamily` 等宽家族 + `inlineCodeBg/inlineCodeText`
  琥珀配色；**代码块 run 仍使用 `codeText`**（语法高亮 token 覆盖其上），
  避免整块正文被行内代码配色污染。
- 修正：盒子裁剪曾把 `content.y`（含滚动偏移）当成视口上界，导致所有盒子
  被裁掉；改为 `inner.y/inner.h` 显示坐标。

验证：

| 项 | 结果 |
| --- | --- |
| 解析金样 | `md_test` 341/0（新增 10 组容器断言：多冒号、默认标题、未知类型、未闭合、嵌套、GitHub 警报/大小写/未识别/同行正文） |
| UI 断言 | `md_ui_test` 49/0（容器 3 行、高度 >90、无越界写） |
| 原生 4K@192 | 浅/深主题下 note/tip/warning/danger 卡片、标题、色条、嵌套内盒正确；相邻卡片有间距 |
| 配色回归 | 代码块默认色恢复 `codeText`（浅色深灰 / 深色浅灰），仅行内代码为琥珀；表格内行内代码同样为琥珀 + 等宽 |
| 回归 | `gfx_rich_test` 34/0、`net_test` 877/0；19 exe 0CRT；build.cmd CRLF 201/201 |

### 修正：链接悬停光标不跟手（2026-11-13）

- 根因：Ui 只在 hover 目标 **id 变化**时失效重绘。整篇 Markdown 是一个
  控件，指针在文档内部移动不会产生新帧，链接命中/光标停留在上一帧；
  只在点击（强制帧）或移出整篇文档时才更新。
- 新增 `Ui_SetCursorPoll(ui, id, on)`：标记控件内部存在精细命中区，指针
  在控件内每次移动都失效重绘（`Ui_OnMouseMove` 检查 `cursorPoll`）。
  `UiMarkdown` 在文档含链接时开启（flatten 时记录 `hasLinks`）。
- 效果：首次悬停即出现手型；移出链接下一帧恢复箭头，不再需要点击；
  点击/拖动逻辑不变（仍为单 widget）。

验证：`GetCursorInfo` 采样 —— 首次悬停 `HAND`、移到正文空白 `ARROW`、
再次悬停 `HAND`、再移出 `ARROW`；`md_test` 341/0、`md_ui_test` 49/0、
`gfx_rich_test` 34/0、`net_test` 877/0。

### 修正：容器标题对比度（2026-11-13）

浅色主题下“注意”标题几乎看不见：容器标题取 `primary.text`，而该 token
是**主按钮上的白字**（白字配 `#EAF2FD` 浅蓝底）。改为按浅/深主题固定
高对比语义色（不进 `UiMarkdownStyle`，避免再被按钮 token 误导）：

| 类型 | 浅色 标题/色条 | 浅色底 | 深色 标题/色条 | 深色底 |
| --- | --- | --- | --- | --- |
| note | `#1557B0` | `#EAF2FD` | `#8FB8FF` | `#17304A` |
| tip | `#1E7B47` | `#E9F7EF` | `#63D68F` | `#16331F` |
| warning | `#B45309` | `#FFF4E5` | `#F0B45A` | `#3A2A16` |
| danger | `#C0392B` | `#FDECEC` | `#FF8A94` | `#3A1D1D` |

标题同时改用 12 bold 小字号（新增内部 `fSmallBold`），提升可读性。
验证：浅/深 4K 截图标题与底/色条对比清晰；`md_test` 341/0、
`md_ui_test` 49/0。

### 修正（二）：链接光标 —— 事件驱动（2026-11-13，最终方案）

前两版（逐帧重绘、WM_SETCURSOR 实时查询）都不对，根因与正确做法：

1. 真实移动先发 `WM_SETCURSOR`（OS 行为，无法避免），若处理函数实时做
   DWrite 命中会带来逐移动成本；若回放上一帧缓存，单控件内的命中区又会
   滞后/被同 id 后注册的 ScrollView 视口项覆盖。
2. 正确模型：**控件逻辑在鼠标移动事件内判断**。`Ui_OnMouseMove` 命中
   控件后调用该控件的 `UiHoverFn`（新增）；Markdown 在其中用当前滚动
   偏移 + 可见行 + `rtH` 行内预筛做点命中，只在链接进入/离开时返回 1
   请求重绘，并用 `Ui_SetCursor` 更新控件光标。
3. `WM_MOUSEMOVE` 处理在 `Ui_OnMouseMove` 之后立即 `app_apply_cursor`：
   只有 `GetCursor() != cur` 才 `SetCursor`，光标变化与重绘无关。
4. `WM_SETCURSOR` 只回放缓存的控件光标，不再做任何命中查询；同样
   “不同才 SetCursor”。滚动（无鼠标移动）由滚动动画帧中的
   `Ui_Markdown` 重新判定并更新缓存光标，帧末应用。

清理：移除上一版无效的 `Ui_SetCursorPoll` / `Ui_SetCursorFn` 及
`cursorPoll/cursorFn` 字段；`UiHoverFn` 取代之，返回 1 才重绘，
不是每次移动都重绘。已删除临时调试 API。

验证：

| 项 | 结果 |
| --- | --- |
| 事件路径 | 投递 `WM_MOUSEMOVE`：链接上 HAND → 空白 ARROW → 移回 HAND，无点击/无重绘依赖 |
| 静止滚动 | 指针固定，滚轮让链接移走：HAND → ARROW |
| headless | `md_ui_test` 63/0：移动事件找链接 HAND、右侧空白 ARROW、滚动后原坐标失效且新坐标仍 HAND |
| 回归 | `md_test` 341/0、`gfx_rich_test` 34/0、`net_test` 877/0；19 exe 0CRT；build.cmd CRLF 201/201 |

### 性能：鼠标移动 / 拖滚动条（2026-11-13）

实测方法：`SendMessage(WM_MOUSEMOVE)` 定量投递（另有 `WM_NULL` 基准扣除
跨进程消息开销），`GetProcessTimes` 读应用 CPU；4K@200%（1520×1440 窗口）。

发现并修复：

1. `Ui_OnMouseMove` 在按下期间**每次移动都失效 pressed 控件**。在文档内
   按住拖动时 200 次移动产生 232 帧整窗重绘。改为显式 opt-in
   `Ui_SetPressPoll`（滑块、TextArea 拖选），其余控件按下期间移动不再重绘。
2. 行内代码底色与表格单元格文本宽每帧重复调 `Gfx_RichRangeRects` /
   `Gfx_RichWidth`（DWrite 查询）。改为建布局时缓存：`MdRow.codeRects`、
   `MdRow.cellW`，绘制只做平移。
3. 链接 hover 变化过去失效整个文档 widget。`UiHoverFn` 增加 dirty 输出，
   Markdown 返回旧/新链接**行**的并集，经 `Ui_InvalidateRect` 局部失效。

结果（同机同文档）：

| 场景 | 修改前 | 修改后 |
| --- | --- | --- |
| 正文移动（2000 次，含消息开销） | 109 ms | 47 ms（WM_NULL 基准 31 ms） |
| 拖 thumb（200 步） | 329 ms | 62 ms（0.31 ms/步） |
| 链接/空白交替（2000 次） | 3109 ms | 3359 ms（每次切换 1 帧，仍由宿主整窗背景+Present 主导；Markdown 只重绘链接行） |

说明：链接切换的剩余成本是宿主每帧绘制整窗渐变与合成，不是文档布局；
若要继续下降需要滚动快路径（层缓存/带状平移），属后续独立优化。
回归：`md_test` 341/0、`md_ui_test` 63/0、`gfx_rich_test` 34/0、
`net_test` 877/0；滑块 65%→84% 拖动、TextArea 拖选原生验证通过。

### 修正：平滑滚动时的亚像素行距抖动（2026-11-13）

滚动偏移带小数时，每行文本的起点落在非整数设备像素上，逐帧取整会在
相邻像素间来回切换，表现为行距/文字轻微跳动。处理：

- 新增 `Ui_PxSnap(ctx, dip)`：把 DIP 对齐到设备像素；
- **只对滚动位移整体取整一次**：`content.y` 对齐后，所有行/盒子都用同一
  整数设备偏移平移，行内与行间的亚像素相位恒定；
- 逐行/逐盒子分别 `Ui_PxSnap` **是错的**：不同行的舍入阈值不同，块间距
  会逐帧在 ±1px 间切换（round 本身没错，错在各自取整）；
- 命中基准仍用精确 `content.y + ItemY`（与绘制相差 <1px，不影响交互）；
- TextArea（`drawOffset`）、Select（列表 `off`）同样是“单一位移对齐”。

验证：4K@200% 滚动动画两帧取样，相邻行中心距稳定为 45 设备像素
（22.5 DIP×2）；`md_ui_test` 66/0（新增 `Ui_PxSnap` 断言）。

**对齐原则（误差不累积）**：

- 滚动状态、动画 target/velocity、布局、命中计算全部使用精确浮点；
- `Ui_PxSnap` 只作用于提交给绘制的**滚动位移**（单个 origin），且
  每帧从精确值重新取整，属于纯函数，**不回写** offset/target；
- TextArea 的行距 `lineH` 是有意量化一次（设备像素网格），之后行位置
  为 `i*lineH` 的精确倍数、命中也用同一 `lineH` 反算，不存在逐行累积；
- 审计后已把此前的两处“snap 值参与计算”改回精确值：TextArea 的
  命中映射/可见范围、Select 的可见范围；Markdown 命中也回到精确基准。

### 样例：预览 + 源码编辑器双联动（2026-11-13）

- 窗口 1100×720：左侧 Markdown 预览，右侧可展开源码编辑器（默认收起，
  「编辑源码 / 收起源码」按钮切换；展开时预览让出 380 DIP）。
- 文档真源仍是 `char *doc`：`UiTextArea` 修改后经 `UiTextArea_TextUtf8`
  取回 UTF-8，更新 `doc` 并 `UiMarkdown_SetText`，预览同帧更新（小文档
  全量重解析；输入路径见下）。
- 滚动联动：按 `offset / maxOffset` 比例双向映射。谁在本帧移动就驱动另
  一侧（0.6 DIP 阈值区分驱动者）；文本更新后按编辑器比例回填预览。展开
  时以预览位置为准映射编辑器视口并定位光标行，避免 SetText 的 caret=end
  把两侧一起拉到底部。
- 键盘：`UiTextArea_Key/Char/OnIme/Blink` 全部转发；IME 挂到 `app->ime`。
- 新增 `UiMarkdown_ScrollOffset / UiMarkdown_MaxOffset /
  UiMarkdown_ScrollToOffset`（设置会清除 pendingTop），供宿主做联动。
- 「追加 50 行」「打开文件」同时更新文档真源、编辑器和预览。

验证：默认收起；展开后两侧均从预览当前位置开始；编辑器滚动 → 预览跟随
（ta=1012 → md=872），预览滚动 → 编辑器跟随（ta=1541 → md=1352）；
在编辑器末尾输入 `## live-sync` + 两回车 + 正文，预览立即出现对应标题/
正文；收起后编辑内容保留且预览占满宽度。

### 补充：图片显式尺寸 `{width= height=}`（2026-11-13）

- 语法采用 Pandoc/Markdown-Extra 属性：`![alt](url "title"){width=280 height=120}`，
  块级与行内图片都支持；键大小写不敏感，`w/h` 为简写，未知属性忽略。
- 数值按 **DIP**；只给 `width` 或 `height` 时按原始宽高比补另一个；
  两个都给时按精确盒子显示（块级用 `UI_IMG_FIT_STRETCH` 拉伸，行内对象
  直接绘到该矩形）；显式尺寸仍夹到内容宽，避免横向溢出。
- 未指定尺寸时维持原策略：内容宽 + `imageMaxH` 等比缩放；显式高度不再被
  `imageMaxH` 覆盖。
- 解析产物：`MdBlock.imgW/imgH`（块级）、`MdImgRef.w/h`（行内），并混入
  内容哈希，尺寸变化会触发行重建。

验证：`md_test` 366/0（块级/行内、大小写/空格、省略一项、未知属性）；
`md_ui_test` 76/0（`height=20`→高约 30、`40x15`→约 25、行内 200x100 撑高）；
4K@200% 样例中 `280x120` 块图为 556×239 设备像素（≈280×120 DIP）。

## 15. 数学公式（设计 + 阶段 1 实现，2026-11-13）

### 15.1 语法

- 行内：`$...$`、`\(...\)`；块级：`$$...$$`、`\[...\]`（独占行时优先块级）。
- 转义 `\$` 为字面美元；代码 span/代码块优先于公式；公式内不解析 Markdown。
- v1 命令：`\frac \sqrt ^ _ \left \right \text`、小写/大写希腊字母、常用运算符
  （`\sum \int \prod \lim \infty \cdot \times \pm \le \ge \ne \to ...`）、
  间距（`\, \; \: \! \quad \qquad`）；环境 `matrix/pmatrix/bmatrix/vmatrix/
  aligned/cases`，`&` 分列、`\` 换行。
- 未知命令、缺参数、未闭合、意外 `\right/\end` → 布局失败，由渲染层回退
  原始 LaTeX 文本（不吞内容）。

### 15.2 模块与盒子模型

- `src/ui/md_math.h/.c`：纯 0CRT，无 gfx/window 依赖；输入 UTF-8 公式源 +
  基准字号，输出盒子树。
- 盒子类型 `MB_H/MB_V/MB_TEXT/MB_RULE/MB_SPACE`；每个盒子记录
  `w/h/d`（宽/基线以上/以下）与相对父盒子的 `(x,y)`（y 为基线偏移）。
- 度量与绘制均注入：
  `MdMathMeasureFn`（字宽/高/深，实际用 DirectWrite，测试用固定值）；
  `MdMathDraw`（text/rule 回调，实际用 D2D，测试记录序列）。
- 排版规则：上下标层级 display/text→script(0.7)→scriptscript(0.5)；
  分式轴 0.25em、线厚 0.06em；根号字形按内容高度选档 + 上横线；
  `\left/\right` 定界符按内容高度选字号档位并垂直居中；
  矩阵列宽取列最大、行距 0.35em，`aligned` 奇偶列分别右/左对齐，
  `cases` 左花括号 + 左对齐列；`pmatrix/bmatrix/vmatrix` 自动包裹定界符。
- 复杂度上限：节点 8192、递归 32 层、矩阵 64×64。

### 15.3 阶段 1 完成内容

- 新增 `src/ui/md_math.h/.c`、`tests/md_math_test.c`；`build.cmd` 中
  `md_math.obj` 不进入 `CORE_OBJS`，只链接新测试目标
  `bin\cui_md_math_test.exe`。
- 测试覆盖：上下标（基线/字号）、分式（规则线/分子分母位置）、根号、
  定界符放大、pmatrix/aligned/cases、希腊/运算符、间距、`\text`、
  `\{` 转义、错误回退、复杂度上限。
- 验证：`md_math_test` 60/0；其余回归 `md_test` 366/0、`md_ui_test` 76/0、
  `gfx_rich_test` 34/0、`net_test` 877/0；build.cmd CRLF 208/208。

### 15.4 后续阶段

1. `gfx_math` 矢量绘制（Cambria Math + D2D 规则线），隐藏窗口像素断言；
2. `gfx_rich` 行内自定义对象钩子 + Markdown 行内/块级接入（含过期 fallback）；
3. 样例公式、深色主题、4K 截图；显示公式超宽等比缩到内容宽；
4. 性能：按（源哈希, 字号）缓存盒子树，仅绘制可见公式。

### 数学公式阶段 2：gfx_math 矢量绘制（2026-11-13）

- 新增 `src/gfx/gfx_math.h/.cpp`（可选模块）：
  - `Gfx_MathCreate(g, latex, len, sizeDip, textFont)` → 布局失败返回 NULL；
  - `Gfx_MathWidth/Height/Depth`；`Gfx_MathDraw(g, x, baseline, color)`。
- 实现：
  - 文本 run 用 `IDWriteTextFormat`（数学族 **Cambria Math**，`\text{}` 用
    应用传入的正文字体族）+ `IDWriteTextLayout`，按 `(文本, 字号, 样式)` 缓存，
    滚动重绘不重建；基线以上取 `DWRITE_LINE_METRICS.baseline`。
  - 规则线直接 `FillRectangle`；整式按 `Gfx_IntVisible` 脏区裁剪；
    仅在一帧内 (`Gfx_Begin` 后) 绘制。
  - 画刷走 `Gfx_IntBrush`，颜色由调用方传入（换肤无需重建布局）。
  - 与 gfx 核心零耦合：gfx 核心不引用 `Gfx_Math*`，`gfx_math.obj` +
    `md_math.obj` 只链接到 `bin\cui_gfx_math_test.exe`（0CRT）。
- 修正：缓存数组首次分配不能用 `HeapReAlloc(NULL)`（改为 `HeapAlloc`），
  否则所有公式布局失败。
- 测试：`tests/gfx_math_test.c` 隐藏窗口 ULW —— 基本公式尺寸、字号放大、
  pmatrix/aligned 创建与单帧绘制、错误公式返回 NULL。
- 验证：`gfx_math_test` 18/0；`md_math_test` 60/0；其余全量回归通过；
  build.cmd CRLF 215/215。

### 数学公式阶段 3：gfx_rich 行内钩子 + Markdown 接入（2026-11-14）

- 解析层（`ui_md_parse.c`）：
  - 行内 `$...$`：开定界符后不能是空白，闭定界符前不能是空白、后不能紧跟
    数字（货币规则）；不跨行；`\$` 仍走转义。行内 `\(...\)` 优先于反斜杠
    转义识别，未闭合时回退为字面 `(`。
  - 块级 `$$...$$` / `\[...\]`：行首（≤3 空格）独占块，支持单行与多行；
    未闭合回退为段落。块内原始源码（含换行/反斜杠）原样保留。
  - 源码区间写入 `MdMathRef {start,len,srcOff,srcLen}`；`text16` 保留原始
    LaTeX（渲染失败时直接显示）。代码块与代码跨度内的 `$` 不解析。
  - 哈希覆盖行内公式源码与块级公式源码，增量 diff 能感知公式变化。
- `gfx_rich`：
  - 新增 `Gfx_RichInlineCustom(rt, start, len, w, h, baseline, draw, user)`；
    `RichInline` 增加显式 baseline（图片仍用 `0.85h`）。
  - `Gfx_RichDraw` 在文字/图片之后用 `HitTestTextRange` 取范围矩形，回调
    收到左上 x 与基线 y；自定义对象与文字共用一次布局断行。
- `UiMarkdown`：
  - 行内公式：按行字体/字号创建 `GfxMath`，宽度/高度/深度作为行内对象；
    有行内对象时关闭 UNIFORM 行距，避免裁掉上下标。语法失败则按行内代码
    样式（等宽 + 警示色 + 底色块）回退显示源码。
  - 块级公式：独立行，基准 16 DIP、水平居中；超内容宽时按比例缩小重建
    （最多 3 次，下限 8 DIP）；上下各 6 DIP 内边距；语法失败回退为等宽
    源码富文本（可换行）。
  - 公式对象随行布局缓存/驱逐释放；块级公式对象被驱逐后（`measured`
    仍为 1）回到视口时按 `bm/bmFallback` 为空重新测量重建，避免空白行。
  - 主题切换只改绘制颜色，不重建公式。
- **修正**：`MdMath_Draw` 曾把盒子模型的正向上 y 直接加到屏幕基线，导致
  分式、矩阵行、上下标整体镜像（上标变下标、行序颠倒）。改为
  `base - c->y`，绘制回调约定为屏幕坐标（y 向下为正），`md_math_test`
  基线断言同步调整。
- 构建：`cui_markdown.exe` 与 `cui_md_ui_test.exe` 追加链接
  `gfx_math.obj + md_math.obj`；其余目标不受影响。
- 样例：行内公式、`\int`、`pmatrix`、`aligned`、`cases` 五组演示；
  4K@200% 浅色/深色截图确认基线、行序、居中与换肤。
- 验证：`md_test` 414/0、`md_ui_test` 87/0（含驱逐后重建）、
  `md_math_test` 60/0、`gfx_math_test` 18/0、`gfx_rich_test` 34/0、
  `net_test` 877/0；build.cmd CRLF 215/215，21 exe 0CRT。
- 后续：按（源哈希, 字号）缓存盒子树；display 风格大号运算符；`cases`
  大括号按内容高度缩放；超长行内公式的断行策略。

### 数学公式阶段 4：布局缓存与复用（2026-11-14）

- 目标：滚动驱逐后回看、编辑重解析、同一公式多行出现时，不重复解析 LaTeX、
  不重复创建 DirectWrite 布局；主题切换只换颜色。
- 缓存归属：`UiMarkdown` 私有（应用持有状态），不是进程全局；随组件销毁。
- 键与校验：
  - `FNV-1a(LaTeX 源)` + 源长度 + 字号 + `textFont` 指针；
  - 命中前逐字节比较源副本，避免 32 位哈希碰撞误用。
- 条目与生命周期：
  - `{src 副本, GfxMath*, refs, lastUse}`；行持有引用，行释放 `refs--`；
  - `refs=0` 的条目保留供回看；上限 128 条，满额时驱逐“无引用且最久未用”
    的条目；若全部在用，本次不入缓存（`put` 时直接销毁）；
  - 行内公式与块级公式共用同一缓存；块级等比缩宽的中间字号也按键缓存。
  - `SetStyle` 重建字体前：先逐行释放（同时归还公式引用）→ 清缓存 →
    重建字体；`UiMarkdown_Destroy` 顺序相同。原先 `SetStyle` 只把 `rt`
    置 NULL 不释放行，会泄漏并留下悬空公式引用，本次一并修正。
- 绘制可见性：行级 `UiScrollView_Cull` + `Gfx_MathDraw` 内 `Gfx_IntVisible`；
  公式不把颜色烘进缓存，换肤零重建。
- 测试（`md_ui_test` 新增访问器 `DebugMathCache/DebugMathMisses`）：
  - 120 行相同块级公式：首帧只新建 1 次布局；
  - 滚到底再回顶（行被驱逐后重建）：misses 不再增长、缓存条目数不变；
  - 编辑后重解析（公式未变）：misses 与条目数均不变；
  - 3 行混合文档（行内 14 DIP + 块级 16 DIP + 语法错误）：缓存 2 条，
    语法错误不占缓存。
- 验证：`md_ui_test` 96/0、`md_test` 414/0、`md_math_test` 60/0、
  `gfx_math_test` 18/0、`gfx_rich_test` 34/0、`net_test` 877/0；
  build.cmd CRLF 215/215，21 exe 0CRT。
- 后续（阶段 5 已实现）：display 风格大号运算符；`cases` 大括号按内容
  高度缩放；超长行内公式缩宽适配。

### 数学公式阶段 5：display 运算符与尺寸适配（2026-11-14）

- display 风格：
  - `MdMath_Layout` / `Gfx_MathCreate` 增加 `display` 参数：块级公式传 1，
    行内传 0；公式缓存键加入 `display`。
  - `\sum` `\prod` `\int` `\oint` 在 display 且 level ≤ text 时放大约 1.45 倍。
  - `\sum` / `\prod` 的上下限在 display 下居中排在运算符正上方/正下方：
    列宽取运算符与上下限宽度最大值，运算符在列内居中；`\int` 按 TeX 默认
    仍用右侧脚本；行内公式全部保持右侧脚本。
  - 放大运算符的右侧脚本位置随字形高度外移，避免与字形重叠。
- 定界符：
  - `mm_delim_box` 由离散档位改为“基准字号度量 + 按高度比例连续放大”
    （1.08 余量，缩放夹在 1.15x–5x）；矩阵圆括号不再过冲，`cases`
    大括号覆盖全部行高。
- 超长行内公式：
  - 对象宽度超过行可用宽时按比例缩小字号重建（最多 3 次，下限 8 DIP），
    重建走同一公式缓存；缩到下限仍超宽时由 DirectWrite 在公式前断行。
- 测试：
  - `md_math_test`：display `\sum` 运算符 >1.2 倍、上下限在运算符列内且
    基线上/下；display `\int` 放大但脚本仍在右侧；`cases` 大括号 >1.8 倍
    基准字号。
  - `gfx_math_test`：新增 display 求和公式创建/高度/绘制。
  - `md_ui_test`：200 DIP 窄宽下超长行内公式触发字号缩放（misses ≥ 2），
    行高与测量状态正确。
- 验证：`md_math_test` 74/0、`gfx_math_test` 20/0、`md_ui_test` 102/0、
  `md_test` 414/0、`gfx_rich_test` 34/0、`net_test` 877/0；
  build.cmd CRLF 215/215，21 exe 0CRT。
- 4K@200% 截图：display `\sum` 上下限、`cases` 大括号、超长行内公式
  在 1100/820 DIP 窗口下的适配。

### 阶段 5 修正：数学轴居中与原子间距（2026-11-14）

- 垂直居中（`\vcenter` 语义）：
  - `mm_wrap_delim` 在定界符+内容布局完成后，把整组子盒平移
    `dy = axis − (h − d)/2`，`axis = 0.25em`，再重新定界；
  - `cases` / `pmatrix` / `\left…\right` 等多行构造整体以数学轴居中，
    左侧 `f(x)=` 与块中心视觉对齐；单行文本 `h−d` 本身等于 2×axis，
    平移量为 0，不改变原有对齐。
- 原子间距（TeX 简化表）：
  - `MBox.cls` 增加 ORD/OP/BIN/REL/OPEN/CLOSE/PUNCT/INNER 类别；
    普通字符按字符分类，`\sum`/`\int` 记 OP，`\le`/`\ge`/`\in` 等记 REL，
    `\cdot`/`\times` 等记 BIN；带定界符构造整体记 INNER。
  - `mm_expr` 相邻原子插入间距：关系 0.28em、二元 0.22em、
    运算符-普通 0.17em、标点后 0.17em、括号内侧 0.06em。
  - 一元符号规则：表达式开头或 OPEN/BIN/REL/PUNCT/OP 之后的 `+/-`
    按 ORD 处理，不产生前导间距（`-x` 不出现空隙）。
  - 缩放定界符与内容之间留 0.1em 内边距；根号与内容留 0.05em 缝隙。
- 测试：
  - `cases` 两行基线以数学轴对称（首行在上、次行在下，基线和≈0）；
  - 宽度 `x=y` > `xy` + 5，`x+y` > `xy` + 3，
    `\left(x\right)` > `xy` + 5。
- 验证：`md_math_test` 82/0、`md_ui_test` 102/0、`md_test` 414/0、
  `gfx_math_test` 20/0、`gfx_rich_test` 34/0、`net_test` 877/0；
  4K@200% 截图确认 `f(x)={…}` 垂直居中、关系/二元/括号间距。

### 阶段 5 修正 2：墨迹度量 + 普通括号组 + 上下标间距（2026-11-14）

- 墨迹度量（`gfx_math.cpp`）：
  - `GMEntry` 分离 `lb`（布局基线，用于 `DrawTextLayout` 定位）与
    `h/d`（真实墨迹上下高深）；
  - 用 `IDWriteTextLayout::GetOverhangMetrics` 求墨迹相对**布局约束盒**
    （`CreateTextLayout` 的 100000×100000，不是行高 `tm.height`）的溢出：
    `h = lb − (−overhang.top)`、`d = GM_LAYOUT_BOX + overhang.bottom − lb`、
    墨迹右端 `= GM_LAYOUT_BOX + overhang.right` 并入盒宽；
  - 修正前误用 `tm.height` 计算底部溢出，导致所有字形深度恒为 0：
    括号没有下深、被轴线居中逻辑整体压低，斜体 `f` 的右侧墨迹也未计入
    advance，才会出现“括号在另一条线、f 贴住括号”；
  - 定界符高度、上下标位置、行高全部基于墨迹，而不是字体全局行高。
- 普通括号组：
  - `(...)` / `[...]` 在 `mm_primary` 中解析成定界符组：`ST_PAREN`
    停止条件 + `mm_delim_group` → `mm_wrap_delim`；
  - 组内内容以数学轴居中、定界符按内容墨迹自适应高度、两侧各 0.15em
    内边距；未配对时按语法错误回退源码。
- 间距/行距补充规则：
  - `ORD→INNER` 0.12em（`f(` 不再粘连），`INNER→ORD` 与 `INNER↔INNER`
    0.06em；
  - 上下标水平偏移 0.10em、上标基线 0.48em（`x²` 不再贴住 x）；
  - 根号上横线再加 0.04em 间隙；
  - 环境列间距：matrix 0.8em、cases 0.6em、aligned 0.28em；
    行基线步长 `max(1.2em, rh + rd + 0.15em)`，行距不再随墨迹变小而拥挤。
- 根号（`mm_radical_box`）：
  - 不再固定把被开方项抬高 0.12em；
  - 根号字号按被开方项墨迹高度 `needH + 0.10em` 与深度选择（可放大/缩小，
    0.6x–5x），横线放在根号顶钩处（`y = rad.h − t/2`），与根号相连；
  - 被开方项保持基线（`cb.y = 0`），根号宽度的 0.8 处开始排内容。
- 度量来源说明：MathJax/KaTeX 使用 OpenType MATH 表常量 + advance 宽度；
  DirectWrite 可通过 `IDWriteFontFace::TryGetFontTable` 取原始 MATH 表，
  本实现先读出 Cambria Math 常量并在下一节固化为规则表；字形墨迹
  （`GetOverhangMetrics`，相对约束盒）只用于边界/宽度，不参与固定间距规则。
- 测试：高括号墨迹中心对齐（括号基线下移）、`x^2` 上标水平间距、
  根号被开方项保持基线、未配对括号报错；`md_math_test` 91/0。
- 验证：`md_math_test` 91/0、`md_ui_test` 102/0、`md_test` 414/0、
  `gfx_math_test` 20/0、`gfx_rich_test` 34/0、`net_test` 877/0；
  4K@200% 截图确认 `f( x )` 间距、x 在括号内墨迹居中、`x²` 上标间隙、
  matrix/cases 行距与定界符内边距。

### 阶段 5 修正 3：Cambria Math MATH 常量规则表（2026-11-14）

- 用临时探针通过 `TryGetFontTable('MATH')` 读出 Cambria Math
  （upem=2048，MATH 表 25722 字节）的真实常量，然后固化为
  `md_math.c` 的 `MM_*` 规则表（探针已删除）：

  | 常量 | 字体值 | em |
  |:--|--:|--:|
  | scriptPercentScaleDown | 73% | 0.73 |
  | scriptScriptPercentScaleDown | 60% | 0.60 |
  | axisHeight | 585 | 0.2856 |
  | superscriptShiftUp | 750 | 0.3662 |
  | subscriptShiftDown | 418 | 0.2041 |
  | subSuperscriptGapMin | 300 | 0.1464 |
  | upper/lowerLimitGapMin | 133 | 0.0649 |
  | fractionRuleThickness | 133 | 0.0649 |
  | fractionNumber/DenomGapMin | 133 | 0.0649 |
  | fractionNumeratorShiftUp | 1200 | 0.5859 |
  | fractionNumeratorDisplayStyleShiftUp | 1550 | 0.7568 |
  | fractionDenominatorShiftDown | 1030 | 0.5029 |
  | fractionDenominatorDisplayStyleShiftDown | 1370 | 0.6689 |
  | radicalRuleThickness | 133 | 0.0649 |
  | radicalVerticalGap | 166 | 0.0810 |
  | displayOperatorMinHeight | 2500 | 1.2207 |

- 应用：
  - 字号层级改为 1.0 / 1.0 / 0.73 / 0.60；
  - 分式：轴线、规则线厚度、gap 来自常量；分子/分母基线优先用
    text/display 的 ShiftUp/ShiftDown，若内容更高再按内容深度撑开；
  - 上下标：基线位移直接用 superscriptShiftUp / subscriptShiftDown；
    同时有上下标时强制 `subSuperscriptGapMin` 最小间隙；
  - display 大运算符：按 `displayOperatorMinHeight` 选择字号（墨迹高度
    不足时按比例放大），不再写死 1.45x；
  - 根号：规则线厚度/垂直间隙来自常量；根号只用基础或更大字形，
    不缩到比基础字形更小，横线贴根号顶钩，被开方项保持基线。
- 仍依赖字形墨迹的部分：推进宽度/右侧溢出、定界符高度选择、
  被开方项边界。DirectWrite 未提供按 MATH 变体选择并绘制 glyph ID 的
  高层接口，这部分用墨迹边界近似；所有轴线、位移、间距、规则线厚度
  均来自 MATH 常量。
- 测试：同时上下标最小间隙、x² 上标间距、根号基线、括号组居中；
  `md_math_test` 96/0。
- 验证：`md_math_test` 96/0、`md_ui_test` 102/0、`md_test` 414/0、
  `gfx_math_test` 20/0、`gfx_rich_test` 34/0、`net_test` 877/0；
  4K@200% 截图确认分式更紧凑、上下标比例/间隙、display 求和字号、
  根号顶钩与横线连接。

### 内存布局优化：命名 union + tag 纪律（2026-11-14）

- 实测尺寸（x64）：
  | 结构 | 重构前 | 重构后 | 变化 |
  |:--|--:|--:|--:|
  | `MdBlock` | 216 B | 152 B | −64 B（−29.6%） |
  | `MdRow`（行缓存） | 232 B | 128 B | −104 B（−44.8%） |
  | `MdCell` | 64 B | 64 B | 未改 |
  以 1 万叶块/行计，元数据约省 1.6 MB；正文 `text16`、`MdRun`、
  URL 池与可见行 DWrite 布局仍是主要内存占用。
- `MdBlock`：
  - 类型独占标量 `u.code / u.image / u.math / u.box / u.table`；
  - payload `p.text`（段落/标题/代码/图片 alt）、`p.kids`（引用/列表/
    条目/容器）、`p.table`（表格对齐 + 单元格）；
  - 公共字段（type/flags/level/src/hash/raw）保持独立。
- `MdRow`：
  - `p.text`（段落/标题/代码）、`p.image`、`p.table`、`p.math`；
  - 公共字段（缩进/标记/间距/哈希/测量状态）保持独立。
- 命名 union 而非匿名 union：所有访问显式写成 `b->u.image.urlOff`、
  `b->p.text.imgs`、`r->p.table.cellRt`，用错标签在代码评审/grep 中可见，
  也强制编译器报出所有旧访问点。
- tag 纪律：
  - `md_free_block`/`md_row_release` 改为按 type `switch`，每个分支只释放
    本类型成员；
  - 递归/哈希/行内 pass/count、测量/绘制/busy 循环都先用
    `MD_BT_KIDS` / `MD_BT_TEXT` / `md_row_is_text` / 显式 type 判断；
  - `MdRow` 增加创建时快照 `rowType`：旧行释放时 `blk` 可能已随文档
    释放（SetText 先释放旧 MdDoc 再清行），按 `blk->type` 会 use-after-free；
    本次由 `md_ui_test` 的“大文档 → 表格”切换实测暴露并修复。
- 回归：`md_test` 416/0（含 `sizeof(MdBlock)<=152`）、`md_ui_test` 103/0
  （含 `sizeof(MdRow)<=128` 与 SetText 复用）、`md_math_test` 96/0、
  `gfx_math_test` 20/0、`gfx_rich_test` 34/0、`net_test` 935/0；
  4K@200% 样例截图确认公式/图片/表格/容器渲染不变。
- 后续可继续：`MdCell` 的 parse 中间态 `raw` 与 runs/links 互斥，可再省
  ~16 B/单元格；正文/payload 数组才是下一步大头。

### 样例：UiNet + HTTPS 远程图片（2026-11-14）

- `samples/markdown.c` 在 `on_init` 用默认 `UiNetConfig` 创建 `UiNet`，
  并传给 `UiMarkdown_Create(gfx, id, net)`；`on_shutdown` 先销毁 Markdown
  （内部 `UiImage` 会 `UiHttp_Cancel`），再 `UiNet_Destroy`。
- 文档加入 HTTPS 示例：
  `![HTTPS 图片](https://httpbin.org/image/png "center"){width=200}`；
  加载期间显示占位 + spinner，完成后按宽度等比显示；失败显示占位。
- 链路：`md_image_load` 识别 `http://`/`https://` → `UiImage_LoadUrl` →
  `UiHttp_Send`（https 走 SChannel TLS）→ 回调 WIC 解码；Markdown 无共享
  HTTP 图片缓存，行被驱逐后回看会重新下载（后续可加 URL→解码位图 LRU）。
- 验证：4K@200% 下 HTTPS 图片约 1–2 秒异步加载完成并居中显示；
  关闭窗口后进程正常退出（先 md 后 net 的销毁顺序）。

### 增量重建：行级 payload 复用（2026-11-14）

- 问题：`SetText`/`Append` 全量重解析后 `md_rows_clear` 释放所有行，
  RichText 布局、UiImage（含已下载的 HTTPS 图）、表格缓存全部销毁重建。
- 方案（解析仍全量，行资源增量复用）：
  1. 解析前 `md_rows_detach`：把旧 `MdRow` 数组与 `items` 数组整体摘出
     （行数组置空），保留每行 payload 与旧高度；
  2. 全量 `Md_Parse` + flatten 生成新行（未测量）；
  3. `md_rows_merge` 按 `(content hash, rowType)` 匹配：
     - 公共前缀/后缀 O(n)；
     - 中段用开放寻址桶（重复内容按顺序取桶链）处理替换/移动；
     - 匹配且 `indent/markerW` 一致：`p = old.p` 直接搬 payload
       （rt/codeRects/UiImage/inlMath/cellRt/bm），旧行 payload 清零；
     - 旧行已测且 `measuredW == contentW`：沿用高度并 `SetItemHeight`，
       同时加入 live 列表；否则保留资源、按需重新测量；
     - 上下文不一致/未匹配的行只释放 payload；
  4. `Append` 天然命中公共前缀：只新增尾部行，旧行不重建。
- 安全点：
  - 旧行的 `blk` 在解析后已悬空，释放只按 `rowType` 快照（不读 blk）；
  - `md_row_release` 拆为 `md_row_payload_release`（纯 payload）+ 悬停清理，
    便于摘除/移交；
  - 匹配高度额外校验 `indent/markerW/measuredW`，避免缩进变化复用了
    旧换行宽度；
  - OOM/解析失败路径重新 `UiScrollLayout_Init(NULL)`，避免布局指向已释放
    的旧 items。
- 测试（md_ui_test）：
  - 4000 行文档 Append 一段：`DebugRowsReused >= 旧行数`，测量调用增量 ≤2；
  - 三行文档改中间一段：复用 2 行，测量增量 ≤2；
  - 图片文档在别处新增段落：复用图片行且 `DebugImagesReused == 1`，
    图片不重新加载；
  - 4K@200% 原生：HTTPS 图片加载完成后点“追加 50 行”，图片保持显示、
    无 spinner、滚动位置稳定。
- 验证：md_test 416/0、md_ui_test 119/0、md_math_test 96/0、
  gfx_math_test 20/0、gfx_rich_test 34/0、net_test 935/0。
- 仍待做：URL→解码位图 LRU（解决滚动/驱逐后的重复下载与同 URL 共享）。

### 图片复用：UiImage 自带缓存 + 宽度失效保留对象（2026-11-14）

- 问题：窗口 resize、源码编辑器展开/收起都会改变预览宽度，触发
  `md_invalidate_width` 释放所有行，图片行随之销毁并重新下载。
- 设计（不引入 Markdown 级 URL 缓存）：
  - 缓存就是 UiImage 自己：已加载的 `src` 编码字节 + 解码位图保留在
    UiImage 实例内，随 UiImage 一起销毁；
  - `UiImage_LoadUrl` 增加同源短路：`source` 相同且状态为 LOADING/READY
    时直接返回，不 reset、不重新请求/解码；ERROR 仍允许重试；
  - 本地文件也统一经 `UiImage_LoadUrl`（内部再 LoadFile），成功后把
    UTF-8 URL 写回 `source`，保证同源比较成立；
  - `md_invalidate_width` 改为 `md_row_relayout_release`：只销毁/重建与
    宽度相关的布局（RichText、codeRects、表格列/行宽、块级公式拟合），
    **保留图片对象**；行内公式因宽度拟合会重建，图片/公式缓存各自负责；
  - 保留的图片行加入 live 列表，首帧 `md_evict` 回收视口外图片，避免
    resize 后整篇图片常驻。
- 表面 vs 设备：`Gfx` 拆出 `Gfx_DeviceGeneration`（仅 D3D/D2D 设备级资源
  失效，ULW 的 DC RT 重建也计入；DComp `ResizeBuffers` 不改变它），
  `Gfx_Generation` 继续表示“绘制表面重建”。UiImage 只用 device generation
  判断位图是否失效，窗口 resize 不再重新解码。
- 解码尺寸：UiImage 按**原图像素尺寸**解码，绘制时由 D2D 缩放到目标矩形，
  因此 UIImage 自身显示尺寸变化不会导致位图过小，也无需重新解码。若未来
  改为按渲染尺寸生成位图，则矩形尺寸变化时必须从保留的 `src` 重新解码/
  生成，而不是放大旧位图。
- 效果：窗口 resize、容器/编辑器宽度变化时，视口内图片直接复用已解码
  位图，无 spinner/重新请求，也不做 CPU 重新解码（DComp 路径）。
- 边界：滚动驱逐仍按原策略销毁 UiImage，滚回会重新加载；ULW 的 DC RT
  重建会 bump device generation，从而从保留 `src` 重解码（不重新联网）。
- 测试：图片文档宽度 320→300→320 后 `DebugImagesRetained > 0` 且内容
  高度正常；`Gfx_DeviceGeneration > 0`；4K@200% 原生展开/收起编辑器与
  窗口缩放，HTTPS 图片立即显示、无重新加载。
- 验证：md_ui_test 125/0，其余套件全通过。
