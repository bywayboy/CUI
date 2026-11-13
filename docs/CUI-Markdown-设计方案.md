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
| 块类型 | 6 级标题、Setext、段落、围栏/缩进代码、引用嵌套、有序/无序/任务列表、HR、表格、图片逐项截图 |
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
  本帧有效。悬停时手型光标并加粗下划线。

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
