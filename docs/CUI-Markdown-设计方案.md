# CUI Markdown 渲染组件 UiMarkdown 设计方案

> 状态：设计中（2026-11-13）
> 关联：`src/ui/ui_md_parse.*`（新增，纯解析器）、`src/ui/ui_md.*`（新增，组件）、
> `src/gfx/gfx_rich.cpp` + `src/gfx/gfx.h`（新增富文本接口）、
> `src/ui/ui_scroll.*`（复用）、`src/ui/ui_image.h`（块级图片）、
> `tests/md_test.c`（新增，headless 金样测试）、`samples/markdown.c`（新增）、
> 白皮书 §8.21

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
- 0CRT、无第三方依赖、全部在 GUI 线程、兼容脏区重绘模型。

### 1.2 语法范围

| 类别 | v1 支持 |
| --- | --- |
| 块 | ATX 标题 `#`..`######`、Setext 标题 `===`/`---`、段落、空行分段 |
| 块 | 围栏代码 ``` ``` ``` / `~~~`（info string 取首个词作语言） |
| 块 | 缩进代码（4 空格）、水平线 `---` `***` `___` |
| 块 | 引用 `>`（可嵌套）、无序列表 `- * +`、有序列表 `1.`/`1)`（可嵌套） |
| 块 | GFM 任务列表 `- [ ]` / `- [x]`（v1 只读展示）、GFM 管道表格（列对齐） |
| 块 | 块级图片 `![alt](url "title")`（独占一行的图片段落） |
| 行内 | 强调 `*em*` `**strong**` `_em_` `__strong__`、GFM 删除线 `~~del~~` |
| 行内 | 行内代码 `` `code` ``、反斜杠转义、硬换行（行尾两空格或 `\`） |
| 行内 | 行内链接 `[t](url "title")`、引用式链接 `[t][ref]` `[ref]`、自动链接 `<url>` |
| 行内 | 实体 `&amp; &lt; &gt; &quot; &nbsp;` + 数字实体 `&#NNN;`/`&#xHH;` |
| 预处理 | 链接引用定义 `[label]: url "title"`（大小写不敏感、空白折叠） |

### 1.3 非目标（v1）

- 全文选区 / Ctrl+C 复制（v2；DWrite `HitTestTextRange` 已为它铺路）；
- 行内图片（需要 `IDWriteInlineObject` 基线对齐，v2）；
- 内置语法高亮（只留 tokenizer 回调，颜色由应用给）；
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
- **解析与测量都不在每帧发生**：只有内容/宽度/图片状态变化才做；
- **异步完成走失效**：图片加载完成由 `UiImage_SetContext` 自动失效自身矩形；
  链接 hover 动画用 `Ui_AnimDirtyRect`，严格遵守"`WM_PAINT` 内不 `InvalidateRect`"。

## 3. 解析层

### 3.1 数据结构

行内偏移统一用 **UTF-16 码元**（DWrite 的字符单位），源串偏移用 UTF-8 字节。
每个叶子块在解析时一次性从源串生成 UTF-16 展示文本（已解转义/实体），
`MdRun` 直接指向这份文本，绘制时零转换。

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
    int cols, alignCap;
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
6. **表格分隔行**：上一行含 `|` 且本行单元格全部匹配 `:?-+:?`；随后连续含 `|`
   的行作为数据行（`\|` 视为字面竖线，单元格只做行内解析）；
7. **缩进代码**：4 空格且不处于列表续行位置；
8. **空行**：结束当前段落；影响紧/松列表判定；
9. **其他**：段落行。**不支持 lazy continuation**（引用内省略 `>` 的续行会
   结束引用），在 §3.5 偏差表登记。

**列表**：无序标记宽度固定（`•` + 空隙），有序取该列表最大序号宽度并右对齐；
`tight` 由项间是否存在空行决定；续行缩进 = 标记列宽 + 空格。
任务列表在项首识别 `[ ]` / `[x]`（大小写不敏感），标记列改画复选框。

**引用定义预处理**：单遍收集 `[label]: url "title"`（可跨行），label 做
大小写折叠 + 内部空白归一；引用式链接在行内扫描时查表。

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

### 3.4 增量策略

| 场景 | 行为 |
| --- | --- |
| `SetText` 全量替换 | 新解析结果逐块与旧块比较 `hash + type + indent`；相同则**迁移旧 block**（复用 text16/runs/links 与已有测量高度），不同则重建 |
| `Append` 流式 | 定位"最后一个可能未闭合的块"（尾围栏 / 尾表格 / 尾列表 / 尾段落），从该块起始行重解析；之前已闭合块不可变 |
| 宽度变化 | 不重解析，只 `UiScrollLayout_SetWidth` 失效测量 |
| 图片状态变化 | 不重解析，单项失效回填高度 |
| 设备丢失 | 不重解析，布局缓存全部释放，按需重建 |

哈希取源字节 + 容器上下文（引用深度/列表类型/缩进），避免同文不同语境误复用。

### 3.5 与 CommonMark 的已知偏差

| 项 | v1 行为 |
| --- | --- |
| Lazy continuation | 不支持；引用内续行必须带 `>` |
| 行内 HTML / HTML 块 | 按转义纯文本显示 |
| 列表项内多块 | 支持（容器树），但块间距统一按 loose 处理 |
| 强调嵌套的极端情形 | 按分隔符栈主流实现；与规范在罕见重叠场景可能有差异 |
| 裸 URL 自动链接（GFM 扩展） | 不支持，需用 `<...>` 或 `[...](...)` |
| 标题属性 `{#id}` 等扩展 | 不支持 |

## 4. Gfx 富文本接口（新增基础设施）

### 4.1 API

```c
typedef struct GfxRichText GfxRichText;

#define GFX_RICH_WRAP 1u << 0   /* 软换行 (默认开) */

GfxRichText *Gfx_RichCreate(Gfx *g, const wchar_t *text16, int len16,
                            GfxFont *base, float wrapW, uint32_t flags);
void Gfx_RichDestroy(GfxRichText *rt);

/* 颜色: 0 = 用绘制时的默认色; font NULL = 继承 base */
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
```

### 4.2 实现要点（`gfx_rich.cpp`）

- 内部持 `IDWriteTextLayout` + 一个基格式（由 `GfxFont` 的 `IDWriteTextFormat`
  克隆）。范围样式直接调 `SetFontWeight` / `SetFontStyle` / `SetUnderline` /
  `SetStrikethrough` / `SetDrawingEffect`。
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

## 5. 布局层

### 5.1 渲染行模型

容器树扁平化为渲染行，**一个叶子块 = 一行 = 一个 `UiScrollItem`**：

```c
typedef struct MdRow {
    MdBlock *blk;
    int indent;          /* DIP 缩进 = 容器层级累加 */
    int markerKind;      /* NONE/BULLET/NUMBER/TASK */
    float markerW;       /* 标记列宽 (测量 & 绘制共用) */

    /* 测量结果 */
    float h;             /* 本行总高 (含块间距, 存在 UiScrollItem.height) */

    /* 富文本布局缓存 */
    GfxRichText *rt;
    int rtW4;            /* wrapW 量化值 */
    uint32_t rtGen;      /* Gfx_Generation */
    uint32_t rtStamp;    /* 最近使用帧, 驱逐用 */

    /* 图片行 */
    UiImage *img;
    int imgW, imgH, imgReady, imgHSet;

    /* 表格行 */
    GfxRichText **cells; int cellCount;   /* rows*cols, 含表头 */
} MdRow;
```

`UiScrollLayout` 的 `gap = 0`：块间距作为行高的一部分（标题/代码/表格上下
留白不同），保证滚动定位与可见区间计算简单。

### 5.2 测量

| 块类型 | 高度来源 |
| --- | --- |
| 段落 / 标题 / 列表项 | `Gfx_RichCreate(wrapW = contentW - indent - markerW)` → `Gfx_RichHeight()` |
| 代码块 | `行数 × 行高 + 2×codePadY`；超长行软换行（与段落一致），不另做横向滚动 |
| 引用容器 | 子块高度之和 + 竖条内边距；竖条与缩进是绘制装饰 |
| 表格 | 见 §5.3 |
| 图片 | 未就绪 = `style.imagePlaceholderH`；就绪 = `min(自然宽, contentW)` 等比，夹 `imageMaxH` |
| 水平线 | 固定 `style.ruleH + 2×style.ruleGap` |

测量结果写回 `UiScrollItem.height`，`rev = block->hash ^ 图片状态`。
宽度不变时**零测量**；宽度变化只失效 `UI_SCROLL_ITEM_WIDTH_DEPENDENT` 项。

### 5.3 表格列宽算法

1. 第一遍：每列求"不换行自然宽"（各单元格 `Gfx_RichWidth` 最大值）；
2. `sum(自然宽) + 单元格水平内边距 ≤ 可用宽` → 直接用自然宽（余量按对齐留白）；
3. 否则：每列先保底 `tableMinColW`（默认 48 DIP），剩余宽度按自然宽比例分配，
   夹在 `[minColW, 自然宽]`；
4. 第二遍：以分配列宽重新测量每格高度，行高 = 该行最大格高；表格高度 = 各行之和。

极端窄宽（可用宽 < 列数 × minColW）时退化为等分 + 允许字符级换行，不越界
（绘制始终裁剪到行矩形）。

### 5.4 缓存与驱逐

- 布局缓存（`GfxRichText`、表格单元格布局、`UiImage`）只保留**视口 ± 32 行**；
  远处的行释放对象，高度留在 `UiScrollLayout` 里，滚回视口时重建
  （一次 `CreateTextLayout`，成本可接受）；
- 每帧绘制后按 `rtStamp` 做过期清理，不额外扫描全文档；
- 图片对象随行销毁时 `UiImage_Destroy`（会取消未完成请求）；
- `Gfx_Generation` 变化：一次性释放所有布局缓存与图片位图（图片自身会重建），
  下一帧按可见行重建。

### 5.5 滚动集成

- `UiScrollView_Begin(view, ui, rc, contentW, contentH)`，滚动条样式复用
  `UiScrollbarStyle`（默认 reserveSpace），padding 由 `UiMarkdownStyle` 给；
- `UiScrollView_Cull` 已在可见区间内；绘制仍用 `Gfx_IsVisible` 跳过被其它
  窗口遮挡/脏区外的行；
- **跟随尾部**：`SetFollowTail(1)` 时，内容高度增长且用户原本就在底部附近
  （`maxOff - off < 4 DIP`）→ 自动滚到底；用户上滚则自动取消跟随，回到底部
  再恢复（流式输出的标准行为）；
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

### 6.2 链接交互

- 每行布局重建时，对每个 `link` 调 `Gfx_RichRangeRects` 缓存矩形（含换行多段）；
- 绘制后对每个矩形 `Ui_RegisterWidget(Ui_IdIndex("mdlink", rowIndex, linkIdx), rc)`
  + `Ui_SetCursor(HAND)`；同一链接的多个矩形用同一 id 多次注册（`Ui_State`
  的既有语义）；
- hover 用 `Ui_StateInstant`（离开立即回弹，不滞后），颜色在
  `style.link` ↔ `style.linkHover` 间 120ms `Ui_AnimSmooth` 插值，动画期间
  `Ui_AnimDirtyRect(链接矩形)`；
- 点击：先调 `onLink` 回调（若设），同时置 `clickedUrl`（本帧有效）；
  库**不内置导航**，由应用决定（建议白名单 scheme 后 `ShellExecuteW`）；
- 链接是否换行、是否与其它 run 相邻都不影响命中：矩形来自 DWrite 实测。

### 6.3 代码块复制

- 代码块 hover 时右上角出现复制按钮（24×24，图标用 `theme->fIcon` 的 MDL2
  字形；无图标字体时退化为"复制"小字）；
- 点击把该块**原始代码文本**（UTF-16）写入剪贴板：`OpenClipboard` /
  `EmptyClipboard` / `GlobalAlloc` / `SetClipboardData(CF_UNICODETEXT)`，
  与 `ui_textarea.c` 的复制逻辑同构（约 20 行，v1 在 `ui_md.c` 内实现）；
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

typedef void (*UiMarkdownLinkFn)(void *user, const char *url);
typedef void (*UiMarkdownCopyFn)(void *user, const wchar_t *text16, int len16);
/* 返回写入 out 的 span 数; start/len 为 UTF-16 码元 */
typedef int (*UiMarkdownTokenizeFn)(void *user, const char *lang,
    const wchar_t *text16, int len16, UiMdSpan *out, int cap);

typedef struct UiMarkdownStyle {
    /* 字体: NULL = 按主题字号/字重自动创建 (h1..h6 由 body 梯度派生) */
    GfxFont *h1, *h2, *h3, *h4, *h5, *h6;
    GfxFont *body, *bold, *italic, *code, *small;

    float blockGap;         /* 普通块间距 (默认 10) */
    float headingGapAbove, headingGapBelow;
    float listIndent, quoteIndent, quoteBarW;
    float codePadX, codePadY, codeRadius;
    float tableCellPadX, tableCellPadY, tableMinColW;
    float imageMaxH, imagePlaceholderH;
    float ruleH, ruleGap;

    UiColor text, text2, link, linkHover;
    UiColor codeText, codeBg, codeBorder;
    UiColor quoteBar, quoteBg, tableBorder, tableHeaderBg;
    UiColor ruleColor, markerColor;
    UiColor tokKeyword, tokString, tokComment, tokNumber;  /* tokenizer 用 */
} UiMarkdownStyle;

typedef struct UiMdSpan { int start, len; uint8_t style; } UiMdSpan;

UiMarkdown *UiMarkdown_Create(Gfx *gfx, UiNet *net /* 可 NULL */);
void UiMarkdown_Destroy(UiMarkdown *md);

void UiMarkdown_SetText(UiMarkdown *md, const char *utf8);     /* 全量, 内部 diff */
void UiMarkdown_Append(UiMarkdown *md, const char *utf8);      /* 流式 */
void UiMarkdown_SetStyle(UiMarkdown *md, const UiMarkdownStyle *st); /* 拷贝; NULL=默认 */
void UiMarkdown_SetBasePath(UiMarkdown *md, const wchar_t *dir);     /* 相对图片路径 */

void UiMarkdown_SetLinkFn(UiMarkdown *md, UiMarkdownLinkFn fn, void *user);
void UiMarkdown_SetCopyFn(UiMarkdown *md, UiMarkdownCopyFn fn, void *user);
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
- `SetStyle` 深拷贝数值字段；`GfxFont*` 是借用指针：应用提供的字体必须比
  `UiMarkdown` 活得久，为 NULL 的字体项由组件创建并在 `Destroy` 时释放；
- 所有 API 与回调都在 GUI 线程（`UiNet` 回调本身归位到 GUI）；
- `Busy` 只统计**可见行**的图片加载与动画；应用在 `animating` 回调里返回它；
- 流式场景：`Append` 后无需其它调用，尾块重解析 + 高度回填 + 跟随尾部自动完成；
- 图片源解析顺序：`http(s)://` → `UiNet`；绝对路径或 `basePath` 拼接后的
  相对路径 → 本地文件；其它 → 行内占位（显示 alt）。

## 8. 性能与内存

| 指标 | 目标 |
| --- | --- |
| 首次解析 | 10k 行 / ~500KB：GUI 线程 ≤ 50ms（/O2），之后全缓存 |
| 流式追加 | 每次 `Append` 只处理尾块，O(尾段长度)，与文档总长无关 |
| 滚动 | 只测量/绘制可见 ±1 行；4K@192 DPI 下稳定 60fps |
| 测量 | 每行每宽度一次；宽度不变时零测量；表格两遍测量退化为一遍（自然宽即够时） |
| 内存 | 源文本 1 份 + runs（≈ 每 16 字符 1 个 run）；布局缓存 ≤ 视口 + 64 行 |
| 脏区 | 链接 hover / 复制按钮动画 → `Ui_AnimDirtyRect`；图片完成 → `Ui_ContextInvalidate` |
| 空闲 | 无动画、无加载时不出帧（`Busy` 为 0） |

框架不变量照旧：`WM_PAINT` 内不调 `InvalidateRect`；逐帧动画只登记动画脏区；
设备丢失按 `Gfx_Generation` 重建。

## 9. 构建接入

- `build.cmd` OBJS 追加：`src\ui\ui_md_parse.c`、`src\ui\ui_md.c`；
  新增 C++ 编译行 `src\gfx\gfx_rich.cpp`（与 `gfx_d2d.cpp` 同 flags）并加入 OBJS；
- 新增样例 `samples/markdown.c` → `bin\cui_markdown.exe`；
- 新增 headless 测试 `tests/md_test.c` → `bin\cui_md_test.exe`（CONSOLE）：
  块/行内/嵌套/表格/增量 diff 断言，不依赖窗口；
- `build.cmd` 必须保持 CRLF（已知：LF 会静默挂起），只用编辑器修改。

## 10. 分步实施计划

| 阶段 | 内容 | 验收 |
| --- | --- | --- |
| 1 | Gfx 富文本接口（`gfx.h` + `gfx_rich.cpp`） | headless：混合样式段落高度/宽度、RangeRects 矩形非空、HitTest 命中位置 |
| 2 | 解析器（块扫描 + 行内扫描 + 引用定义） | `md_test` 金样断言 ≥ 120 条（含嵌套列表/引用/表格/强调边界/实体） |
| 3 | 行模型 + 测量 + 滚动（纯文本块可见） | 样例显示标题/段落/列表，resize 重排正确，滚动 60fps |
| 4 | 装饰：代码块/引用竖条/HR/表格/任务列表 | 截图覆盖全部块类型；窄宽表格不越界 |
| 5 | 链接命中/hover/点击 + 代码复制 | 原生点击链接回调收到 URL；hover 动画可见；复制后剪贴板内容正确 |
| 6 | 块级图片（UiImage 接入、高度回填、跟随尾部） | URL/文件图片加载、spinner、淡入；加载完成后布局无跳动 |
| 7 | 流式追加 + 大文档压测 + 4K@192 DPI + 文档 | Append 10k 行流式输出不卡顿；白皮书 §8.21 / README / 本文档同步 |

## 11. 验证判据

| 项 | 判据 |
| --- | --- |
| 块类型 | 6 级标题、Setext、段落、围栏/缩进代码、引用嵌套、有序/无序/任务列表、HR、表格、图片逐项截图 |
| 行内 | 粗体/斜体/粗斜体、删除线、行内代码、链接、自动链接、转义、数字/命名实体、硬换行 |
| 强调边界 | `**a *b* c**`、`a*b*c`（词内不强调）、未闭合 `*`、`~~` 嵌套 |
| 表格 | 对齐（左/中/右）、`\|` 转义、窄宽换行、超宽裁剪 |
| 链接 | hover 变色 + 手型光标 + 点击回调（真鼠标点击）；换行链接多矩形命中 |
| 复制 | 代码块按钮点击后剪贴板为原始代码（含缩进/换行） |
| 图片 | 块级 URL（本地测试服务器）spinner → 淡入；文件相对路径；失败占位 |
| 增量 | `SetText` 只重建变化块（内部计数断言）；`Append` 与整体重解析结果一致（金样对比） |
| 流式 | 跟随尾部：到底自动跟随、上滚取消、回到底部恢复 |
| 性能 | 10k 行首解析 ≤ 50ms；滚动无测量抖动；空闲零出帧 |
| DPI | 4K@192（scale 2）字号/行距/表格线清晰，无半像素模糊 |
| 设备 | 改变窗口尺寸触发 Gfx 重建后内容与图片正常 |
| 构建 | `cui_markdown.exe` / `cui_md_test.exe` 0CRT；全部样例回归 |

## 12. 风险与后续

- **强调算法**：CommonMark 分隔符栈在极端嵌套下与规范可能有细微差异，
  金样用例以规范示例集为主，偏差登记在 §3.5；
- **表格极窄宽**：退化为等分 + 字符级换行，绘制始终裁剪，不追求可读性最优；
- **超大表格**：单元格布局数量 = 行列积，驱逐粒度是整行（表格），大表入场时
  一次重建成本高；v2 可做按行缓存；
- **后续**：全文选区复制（`HitTestTextRange`）、行内图片（`IDWriteInlineObject`）、
  内置通用高亮 tokenizer、标题锚点/目录、脚注；
- **安全**：库不执行导航；应用对 `file:`/`javascript:` 等 scheme 做白名单，
  图片仅允许 `http(s)` 与本地路径。
