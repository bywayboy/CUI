# CUI 分页组件设计方案

> 目标：即时模式下的行内分页器。组件只负责页码窗口、翻页交互与键盘，
> 不持有数据、不滚动、不弹层。
>
> 实现状态：已实现（`src/ui/ui_pagination.*`）；headless 断言
> `tests/ui_pagination_test.c`（320 项）；样例 `samples/pagination.c`。

## 1. 目标与边界

- 输入是 `total` + `pageSize`，组件内部推导页数；数据偏移由组件提供
  `Offset()`，应用据此取数据。
- 组件不做每页条数下拉、跳页输入、总数文案；这些由应用用 `UiSelect` /
  `UiText` 组合（样例中示范）。
- 页码内部 0 基，界面显示 +1。
- 宽度不足时按“数字 → 紧凑 → 只有翻页”退化，任何形态都不溢出给定矩形。
- 不做：换行、横向滚动、滚轮翻页、RTL、省略号点击跳转。

## 2. 状态与 API

### 2.1 状态

```c
typedef struct UiPaginationStyle {
    float itemW;            /* 0 -> 30；实际取 max(itemW, 最宽页码宽 + 2*padX) */
    float itemH;            /* 0 -> 32 */
    float gap;              /* 0 -> 2 */
    float padX;             /* 0 -> 5；格内水平留白（每侧） */
    float radius;           /* 0 -> theme.buttonRadius (7) */
    int   maxSlots;         /* 0 -> 按宽度算；否则 clamp 到 [5,9] */
    int   firstLast;        /* 1 = 显示首页/末页 « »，默认 0 */
    int   showSingle;       /* 1 = pageCount==1 也绘制，默认 0 */
    const char *prevText;   /* NULL -> 有图标字体时用 chevron，否则 "‹" */
    const char *nextText;   /* NULL -> 有图标字体时用 chevron，否则 "›" */
} UiPaginationStyle;

typedef struct UiPagination {
    UiId id;
    int  page;              /* 0 基当前页 */
    int  total;             /* 条目总数 */
    int  pageSize;          /* >= 1 */
    int  focusVisible;      /* 键盘焦点环 */
    int  changed;           /* 自上次 TakeChanged 以来 page 被改写 */
    UiRect rc;              /* 最近一次绘制区域（改为局部失效） */
} UiPagination;

void  UiPagination_Init(UiPagination *p, UiId id, int total, int pageSize);
int   UiPagination_PageCount(const UiPagination *p);
int   UiPagination_Offset(const UiPagination *p);      /* page*pageSize */
```

- `page` 始终满足 `0 <= page < PageCount()`；`PageCount()==0` 时 `page` 恒为 0。
- `Init` 对非法值收敛：`pageSize < 1` 按 1，`total < 0` 按 0，`page = 0`。
- 状态由应用持有，可以放在 ViewModel 里；组件没有全局注册表。
- `changed` 是粘滞位：点击/键盘/setter 置 1，直到 `UiPagination_TakeChanged`
  读取并清除。应用可在一帧末尾统一消费，不必逐帧比对 `page`。
- 同一帧里若用多根条演示同一份数据（如样例的宽/中/窄三态），必须给每个
  实例不同的 `p->id`——控件表与 `ui->clicked` 都按 id 匹配，同 id 会导致一次
  点击被多次消费。页码由应用自行同步。

### 2.2 setter

```c
void UiPagination_SetPage(UiPagination *p, UiContext *ui, int page);
void UiPagination_SetTotal(UiPagination *p, UiContext *ui, int total);
void UiPagination_SetPageSize(UiPagination *p, UiContext *ui, int pageSize);
```

- 三个 setter 都做 clamp；只有 `page` 真的变化时置 `changed=1`。
- `SetTotal`：页数变少时 `page` 收敛到末页；页数变多不主动移动。
- `SetPageSize`：**保持原首条数据落在新页内**——先算
  `offset = page * oldSize`，再 `page = offset / newSize`，最后 clamp。
  例：`pageSize 20, page 5`（偏移 100）切到 50 -> `page 2`；切到 30 -> `page 3`
  （新页从 90 开始，原首条 100 仍在页内）。
- setter 收到 `ui` 且 `rc.w > 0` 时对整条 `Ui_InvalidateRect`，应用改完数据
  不必再手动失效。

### 2.3 页数与偏移

- 页数：`total / pageSize + (total % pageSize != 0)`，避免
  `total + pageSize - 1` 在接近 `INT_MAX` 时溢出。
- 偏移：`page * pageSize`；调用方自行截断末页长度
  （`min(pageSize, total - offset)`）。
- `total == 0`：页数 0，`Draw` 不绘制，`showSingle` 也不生效。

## 3. 页码槽位

### 3.1 算法

导出的纯函数，便于单测：

```c
/* out 至少 maxSlots 个；-1 表示省略号，其余为 0 基页码。
 * 返回槽位数；pageCount==0 时返回 0。 */
int UiPagination_Slots(int page, int pageCount, int maxSlots, int *out);
```

```text
n = pageCount
if n <= 0: return 0
if maxSlots < 5: maxSlots = 5   /* 上限由调用方/宽度算，最多 9 */
if n <= maxSlots: 输出 0..n-1
edge = maxSlots - 2             /* 贴边时连续的页数 */
win  = maxSlots - 4             /* 中间窗口页数 */

if page <= edge - 1:            /* 贴左：首块 + … + 末页 */
    0 .. edge-1, -1, n-1
else if page >= n - edge:       /* 贴右：首页 + … + 末块 */
    0, -1, n-edge .. n-1
else:                           /* 中间：首页 + … + 窗口 + … + 末页 */
    lo = page - win/2
    0, -1, lo .. lo+win-1, -1, n-1
```

- 任何一种分支都恰好输出 `maxSlots` 个槽位；首、末页始终存在。
- 只在真正有隐藏页时输出省略号，不会出现相邻省略号。
- `maxSlots` 取奇数或偶数都成立（8 也合法：edge=6，win=4）。
- 示例（n=20，maxSlots=9）：
  - `page 2`：`0 1 2 3 4 5 6 … 19`
  - `page 7`：`0 … 5 6 7 8 9 … 19`
  - `page 18`：`0 … 13 14 15 16 17 18 19`

### 3.2 宽度与三种形态

绘制前按 `rc.w` 选择形态，阈值都用当前页码文本实宽计算：

| 形态 | 条件 | 内容 |
| --- | --- | --- |
| 数字 | `w >= 数字最小宽` | `«` `‹` + 槽位（5~9 个）+ `›` `»` |
| 紧凑 | `w >= ‹ + 文本 + ›` | `‹ 6 / 20 ›` |
| 极窄 | `w >= 2 格` | 只有 `‹ ›` |
| 不绘制 | 小于 2 格 | — |

- 数字最小宽 = `(base + 5)*cellW + (base + 4)*gap`，`base = 2`（无首末页）
  或 `4`（`firstLast=1`）；即最少 7 格或 9 格。
- 数字形态的槽位数：先扣掉翻页与 gap，再
  `slots = floor((avail + gap) / (cellW + gap))`，clamp 到 `[5,9]`；
  `style.maxSlots` 非 0 时直接用它并 clamp。
- 所有页码格统一宽度 `cellW = max(itemW, 最宽可见页码文本宽 + 2*padX)`，
  五位数也不会被截断；省略号格固定 16 DIP。
- 紧凑形态的文本是“当前页/总页数”（都 +1），不显示条目总数；
  两侧留白由 `PAG_COMPACT_PAD = 12` 控制。
- `UiPagination_Measure(p, ui, style, compact)` 返回对应形态需要的最小宽度，
  供应用居中/右对齐；数字形态返回最小格数宽度，调用方可以再给更宽。

## 4. 视觉

| 元素 | 规范 |
| --- | --- |
| 格子 | `cellW × 32`，圆角 `theme.buttonRadius`，gap 2，格内左右各 `padX=5` |
| 当前页 | `primary.base` 实底 + `primary.text` 文字 |
| 普通页 | 无底，`theme.text`；hover `neutral.hover` 实色（按动画 alpha），press `neutral.press` |
| 禁用翻页 | `theme.btnDisabledText` 图标/文字，不注册命中，无 hover |
| 省略号 | `theme.text3`，不可点击，16 DIP 宽 |
| 翻页图标 | `fIconS`（12，Segoe MDL2 E76B/E76C）；首/末页画双 chevron；无图标字体时回退 `‹ › « »` 文本 |
| 焦点环 | 当前页格 1px `primary` alpha 150；compact 用文本区外框 |
| 紧凑文本 | `fBody` + `text2`；两侧各 6 DIP 留白 |
| 字号 | 页码 `theme.fBody`（13）；箭头 `fIconS`（12，与正文同视觉大小） |

- hover/press 用 `Ui_State` 平滑，与 `Ui_Button` 一致；不做位移动画。
- hover/press 与行内文字按钮共用 `neutral.hover` / `neutral.press`；亮色主题这两个
  token 已从近白（#F1F3FA）加深到 #E4E9F8 / #D5DDF0，落在白色卡片上可一眼分辨。
  早期方案用的 `primary.soft` 与页面底色色差只有 ~10，实际几乎看不出悬停。
- 主题换肤沿用 `UiTheme.notifyHwnd` 整窗重绘，组件不缓存颜色。
- `firstLast=1` 时在两侧增加 `«` / `»`，语义为首/末页，禁用规则同翻页。
- 自定义 `prevText` / `nextText` 优先于图标箭头；首/末页始终随图标字体切换。

## 5. 交互

### 5.1 鼠标

- 每个页码格独立注册（`Ui_State`），悬停动画互不影响。
- 点击当前页：无动作，`Draw` 返回 -1。
- 点击其他页/翻页：更新 `page`、`changed=1`、整条失效，`Draw` 返回新页码。
- 省略号不注册控件；点击落在面板空白，不产生任何状态变化。
- 边界翻页（首页的 `‹`、末页的 `›`）禁用；紧凑/极窄形态同样。
- 鼠标按下时 `focusVisible=0`（与 TabBar 一致）。

### 5.2 键盘

`UiPagination_Key` 只在 `ui->navId == p->id` 时生效：

| 按键 | 行为 |
| --- | --- |
| `←` / `→` | 上一页 / 下一页（clamp） |
| `Home` / `End` | 首页 / 末页 |
| `PgUp` / `PgDn` | -5 / +5 页（clamp） |
| `Space` / `Enter` | 不处理（没有焦点槽位，返回 0） |

- 处理时置 `focusVisible=1`，并整条失效；`page` 未变化时返回 1 但不置
  `changed`（避免应用误重载）。
- 不注册滚轮目标，避免和页面 `UiScrollView` 抢事件。

### 5.3 失效

- hover/press：`Ui_OnMouseMove` / `Ui_OnMouseDown` 通过控件表局部失效。
- 换页：整条失效（槽位窗口会平移，不能只重绘单格）。
- `Set*`：按 `rc` 整条失效。
- 首帧 `rc` 为空时 setter 不失效；下一帧 `Draw` 会记录新矩形。

## 6. 绘制顺序

1. 计算页数；`pageCount==0` 或（`pageCount==1` 且 `!showSingle`）直接返回；
2. 解析样式默认值，量当前页文本宽，选形态并计算 `cellW`；
3. `UiPagination_Slots` 生成槽位；
4. 依次绘制前翻页、槽位、后翻页（先底色后文字）；
5. `focusVisible` 时最后画焦点环；
6. 返回本帧激活的页码。

`Ui_PushClipRect(p->rc)` 包住整条，保证任何计算误差都不会画出边界。

## 7. 边界情况

| 场景 | 行为 |
| --- | --- |
| `total == 0` | 不绘制，`PageCount()==0` |
| `pageCount == 1` | 默认不绘制；`showSingle=1` 时画单个当前页 + 两个禁用翻页 |
| `pageSize` 切换 | 首条数据留在新页内（floor 除法），再 clamp |
| `total` 变小 | `page` 收敛到末页 |
| 页码到 5 位数 | 统一加宽格子，不截断、不溢出 |
| 宽度只够 2 格 | 只画 `‹ ›`；再窄不画 |
| `INT_MAX` 总量 | 页数用除法+取余计算，不溢出 |
| 连续点击翻页 | 每帧最多前进一页，无加速度/连击 |

## 8. 测试清单

`tests/ui_pagination_test.c`（headless，沿用 `ui_nav_test` 的
`SCV2_ULW=1` + 离屏 `Gfx` 模式；当前 320 项断言）：

1. 页数：0 / 1 / 整除 / 有余数 / `total < pageSize` / 接近 `INT_MAX`；
2. `Init` 与 setter 的非法值、clamp、`changed` 只在变化时置位；
3. `SetPageSize` 保持首条：20 -> 50 / 30，以及 `total` 缩小后的收敛；
4. 槽位：页数 ≤ maxSlots、贴左/中间/贴右、maxSlots 5..9、
   首末页始终存在、无相邻省略号、页码单调且在范围内；
5. 形态：足够宽为数字、阈值附近退化为紧凑、再退化为极窄、
   单页隐藏与 `showSingle`、`total=0` 不绘制；
6. 交互：`Draw` + `Ui_OnMouseMove/Down/Up` 点击页码/翻页/当前页/边界禁用，
   `changed` 与返回值一致（点击后补画一帧处理事件，再画一帧刷新命中表）；
7. 键盘：方向键/Home/End/PgUp/PgDn 的 clamp、`focusVisible`、`navId` 门控、
   未变化时 `changed=0`；
8. 几何：所有格子都在 `rc` 内，紧凑文本不被裁切；
9. 200% DPI：`Measure` 与 DIP 坐标不变、点击仍命中；
10. `HeapValidate` 通过，无泄漏。

## 9. 落地清单

| 文件 | 内容 |
| --- | --- |
| `src/ui/ui_pagination.h/.c` | 约 600 行，含槽位纯函数与图标/文本双箭头 |
| `src/ui/ui_theme.*` | 新增 `fIconS`（12 DIP MDL2），行内图标与正文同视觉大小 |
| `tests/ui_pagination_test.c` | 320 项断言（含 200% DPI） |
| `samples/pagination.c` | 三根同步条（数字/紧凑/极简）+ 假数据列表 + 范围文案 + 每页条数 `UiSelect` + 主题切换 |
| `build.mk` / `build.cmd` | `ui_pagination.obj` 入核心对象，新增样例与测试目标、0CRT 校验 |
| `README.md` / 白皮书 | 目录、运行列表、能力矩阵与 8.22 用法段落 |

样例演示“组件不内置每页条数”的组合方式：下方一页 20 条假数据、
“共 N 条 / 第 a–b 条”文案由应用绘制，切换 `pageSize` 时先用组件当前
`Offset()` 算出新页码再 `SetPage`（或直接交给 `SetPageSize` 的保持首条规则）。

## 10. 已定决策

| 决策 | 结论 |
| --- | --- |
| 功能范围 | 页码条 + 翻页；每页条数/跳页/总数文案由应用组合 |
| 数据模型 | `total + pageSize`，组件内部算页数 |
| 页码基准 | 内部 0 基，显示 +1 |
| 窄宽度 | 自动紧凑退化，任何宽度不溢出 |
| pageSize 切换 | 保持原首条数据落在新页内（floor 除法 + clamp） |
| 单页 | 默认隐藏，`showSingle` 可强制显示 |
| 省略号 | 只表示隐藏页，不可点击 |
| 键盘翻页 | 方向键 ±1、Home/End、PgUp/PgDn ±5；不占用滚轮 |
