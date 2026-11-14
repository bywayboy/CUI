# CUI NavBar 导航菜单设计方案

> 目标：即时模式下的多级导航菜单，支持水平/垂直布局、水平溢出收起、
> 垂直图标 rail 模式、可设置并保持高亮的选中路径，以及绝不越出视口、
> 绝不使用滚动条的多级子菜单。
>
> 状态：v1 已完成。落地文件：`src/ui/ui_nav.*`、`samples/nav.c`、
> `tests/ui_nav_test.c`。

## 1. 组件边界

```text
应用
 ├─ UiNavItem 树（静态/应用持有，树结构不复制）
 ├─ UiNavBar（选中路径、焦点、打开层级、溢出/分页）
 └─ UiPopupManager（全局只允许一个弹出会话）
```

- `UiNavBar` 不拥有菜单数据，只保存 `UiNavItem*` 和稳定 `UiId`。
- 导航栏与子菜单共用一个 `UiPopupManager`；多级子菜单属于**一个弹出会话**，
  不是多个并列弹层。
- 选中路径由稳定 `UiId` 组成，不按索引保存；菜单项增删/重排不会把选中态
  错配到别的项。
- 动画只做颜色渐变（hover/选中/祖先高亮），不做面板位移/缩放/淡入；
  子菜单开合即时完成，避免几何动画和视口夹取互相打架。

## 2. 数据模型

```c
enum {
    UI_NAV_DISABLED   = 1u << 0,
    UI_NAV_DIVIDER    = 1u << 1,
    UI_NAV_DANGER     = 1u << 2,
    UI_NAV_SELECTABLE = 1u << 3,   /* 有子项的父项也允许被选中 */
};

typedef struct UiNavItem {
    UiId id;                 /* 稳定路径标识，必须全树唯一 */
    const char *label;
    const char *shortcut;    /* 子菜单右侧提示，可为 NULL */
    UiIcon icon;             /* icon.kind=0 表示无图标 */
    const struct UiNavItem *children;
    int childCount;
    unsigned flags;
} UiNavItem;
```

`id` 必须显式给出；组件不根据 label 或索引推导路径。建议应用用
`Ui_Id("nav.file", 0, 0)` 这类固定作用域，避免菜单刷新后 ID 变化。

`UiNavBar` 核心状态：

```c
typedef struct UiNavBar {
    UiId id;
    int layout;              /* UI_NAV_HORIZONTAL / UI_NAV_VERTICAL */
    int rail;                /* 1 = 垂直图标模式 */
    const UiNavItem *items;
    int count;

    UiId selected[UI_NAV_MAX_DEPTH];
    int selectedDepth;
    UiId requested[UI_NAV_MAX_DEPTH];
    int requestedDepth;      /* 数据暂缺时保留，恢复后自动重新高亮 */

    UiId openPath[UI_NAV_MAX_DEPTH];
    int openTail[UI_NAV_MAX_DEPTH];    /* 1 = 上一层列表的隐藏尾部续页 */
    int tailOffset[UI_NAV_MAX_DEPTH];
    int openDepth;
    int openOverflow;

    /* 本帧每层实际绘制的行范围; panelMore=1 时末尾还有 "更多" 行. */
    int panelStart[UI_NAV_MAX_DEPTH];
    int panelVisible[UI_NAV_MAX_DEPTH];
    int panelMore[UI_NAV_MAX_DEPTH];

    UiId keyRoot;
    UiId keyItem[UI_NAV_MAX_DEPTH];
    int keyDepth;

    UiId activated[UI_NAV_MAX_DEPTH];
    int activatedDepth;
    int changed;

    UiRect rc, moreRc;
    UiRect panelRc[UI_NAV_MAX_DEPTH];
    int visibleStart;
    int visibleCount;
    int overflow;
    ...
} UiNavBar;
```

## 3. 对外 API

```c
UiNavStyle UiNavStyle_Default(void);

void UiNavBar_Init(UiNavBar *nav, UiId id);
void UiNavBar_SetItems(UiNavBar *nav, const UiNavItem *items, int count);
void UiNavBar_SetLayout(UiNavBar *nav, int layout);   /* 水平/垂直 */
void UiNavBar_SetRail(UiNavBar *nav, int rail);
void UiNavBar_SetStyle(UiNavBar *nav, const UiNavStyle *style);

/* path 只复制前 depth 个 UiId；数据暂时不存在也保留。 */
void UiNavBar_SetSelected(UiNavBar *nav, const UiId *path, int depth);
int  UiNavBar_GetSelected(const UiNavBar *nav, UiId *path, int cap);

/* 每帧在普通控件区绘制 bar；返回 1 = 本帧有选中变化。 */
int  UiNavBar_Draw(UiNavBar *nav, UiPopupManager *pm, UiContext *ui,
    UiRect rc, int enabled);

/* 帧末统一绘制所有子菜单面板（overlay 层）。 */
void UiNavBar_DrawPopups(UiNavBar *nav, UiPopupManager *pm, UiContext *ui);

int  UiNavBar_Key(UiNavBar *nav, UiPopupManager *pm, UiContext *ui, UINT vk);
int  UiNavBar_TakeChanged(UiNavBar *nav, UiId *path, int cap);
void UiNavBar_Shutdown(UiNavBar *nav);
```

`UiNavBar_Draw` 负责：

- 测量/布局顶级项；
- 水平溢出计算；
- 选中/祖先高亮；
- 鼠标点击、悬停展开；
- 键盘焦点绘制；
- 垂直模式高度不足时在底部提供 `>>` 入口。

`UiNavBar_DrawPopups` 负责：

- 按 `openPath`/`openTail` 逐级计算面板；
- 每级 `Ui_Popup_Place` + 分页（"更多"续页）+ z 层；
- 注册面板/行/"更多"行命中；
- 绘制图标、标签、快捷键、箭头；
- 点击叶子提交选中，点击父项继续展开，点击"更多"打开同会话续页。

## 4. 选中路径语义

### 4.1 设置与保持

- `UiNavBar_SetSelected` 保存完整 `UiId` 路径；每帧从树根解析。
- 路径中的 id 暂时不存在时**不清空**，只按最长有效前缀高亮；菜单数据恢复后
  自动恢复完整高亮。
- `UiNavBar_SetItems` 不重置选中路径。
- 叶子被选中后，`selected` 自动更新并置 `changed`；应用可以读
  `UiNavBar_TakeChanged` 做路由，也可以用 `SetSelected` 覆盖。

### 4.2 高亮层级

| 路径位置 | 视觉 |
| --- | --- |
| 叶子 | primary soft 底、primary 文字、2px 指示线 |
| 祖先 | primary 文字；水平用低 alpha 指示线，垂直用左侧 3px 竖线 |
| 悬停 | primary 低 alpha 底色；子菜单行即时，顶级项平滑过渡 |
| 展开子菜单的父项 | 左侧 3px 细线（primary 55% alpha），不用底色 |
| 键盘焦点 | 焦点环/描边，不覆盖选中态 |

- 水平顶部项：`selected[0]` 是该项时视为祖先/叶子高亮。
- 子菜单行：路径前缀命中即祖先行；`selectedDepth` 最后一项为叶子。
- 展开父项不用底色：指针快速移到兄弟行时，旧子菜单可能还开着，
  两行都铺底色会同时像 hover；细线只标记“这一支已展开”，与 hover 可区分。
- 选中项在溢出区时，`>>` 按钮显示 active 点/下划线；打开溢出菜单后选中行
  仍保持高亮。
- rail 模式只显示图标，选中/祖先图标用 primary 色；有子项的项在行右缘显示
  `›`（距行右缘 10 DIP，图标整体左移让位，与展开态一致），完整标签通过
  tooltip 或展开 rail 显示。

### 4.3 父项是否选中

默认父项点击只展开子菜单，不改变选中路径；只有叶子或带
`UI_NAV_SELECTABLE` 的父项会提交选中。这样“文件/编辑”这类菜单标题不会
把当前页面错误地改成父级。

### 4.4 无滚动：分页与"更多"

导航组件内部**不使用任何滚动条/滚动视图**：

- 水平模式：超出宽度的尾部项收进 `>>`；
- 垂直模式：超出高度的根部项收进底部 `>>` 行；
- 子菜单面板：超高时首页末尾出现"更多 >>"行，点击/回车打开同会话的
  下一层面板，从隐藏尾部第一项继续显示；重复该过程可翻完长列表；
- 续页面板用 `openTail[level]=1` 标记，`tailOffset[level]` 记录该页在
  原列表中的绝对起点；
- `UI_NAV_MAX_DEPTH = 8`，容纳逻辑层级 + 续页层；
- Esc 从续页返回时，焦点回到父面板的"更多"行，而不是丢失；
- 续页面板中的行仍是正常菜单行，可继续展开自己的子菜单。

## 5. 水平布局与溢出

### 5.1 顶级项测量

```text
itemW = padX*2 + iconW + gap + textW
```

- `showIcons=0` 或 icon.kind=0 时不计图标宽；
- 文本过长时先按内容宽计算，布局放不下再由溢出规则隐藏整个项，
  不在顶级项内部做半截省略；
- 计算全部项宽后，决定是否保留 `>>`。

### 5.2 溢出算法

1. 若 `sum(itemW) <= rc.w`：全部显示，`overflow=0`；
2. 否则预留 `moreW`（`>>` 按钮宽度）；
3. 从第一项开始累加，直到再放下一项会超过 `rc.w - moreW`；
4. `visibleCount` 之前的项直接显示，剩余项进入 `>>` 菜单；
5. 若连一个项都放不下，则只显示 `>>`，其菜单包含全部项；
6. `>>` 菜单本身是一个正常面板，可继续打开隐藏项的子菜单。

`moreW` 只在确实溢出时占用；窗口变宽后自动取消 `>>`，不需要重建状态。

### 5.3 垂直布局

- 展开模式：完整标签 + 图标，可选子菜单右侧 flyout；
- rail 模式：只显示图标，宽度默认 56 DIP；悬停/点击打开 flyout；有子项的项在
  行右缘显示 `›`，收起状态下也能分辨哪些项可展开；
- 垂直项超出高度时，只显示能放下的前缀项，并在底部保留一行 `>>`；
  `>>` 打开隐藏根部项的面板（与水平溢出菜单同一模型），不出现滚动条；
- 选中祖先在 rail 中保持左侧竖线/图标高亮；被 `>>` 收起的选中项通过
  `>>` 行的高亮提示其位置。

## 6. 子菜单会话模型

### 6.1 一个会话，多级面板

`UiPopupManager.openId` 保存导航栏 id（会话 id），不是某一级面板 id。
`openPath[]` 和 `openDepth` 由 `UiNavBar` 自己保存：

```text
openDepth = 1
  openPath[0] = 顶级项 A        -> 面板 0 显示 A.children
  openDepth = 2
  openPath[0] = A
  openPath[1] = 子项 B          -> 面板 1 显示 B.children
```

overflow 菜单是特殊根：

```text
openOverflow = 1
  openDepth = 1                 -> 面板 0 显示隐藏的顶级项
  openDepth = 2
  openPath[1] = 隐藏项 H        -> 面板 1 显示 H.children
```

面板超高的续页：

```text
openPath[0] = A
  panel 0: A.children[0..8] + "更多"
  openTail[1] = 1, tailOffset[1] = 9
  panel 1: A.children[9..17] + "更多"
  openTail[2] = 1, tailOffset[2] = 18 ...
```

- 全局仍然只允许一个 popup session；打开别的 Select/菜单会替换当前导航会话；
- 点击会话外关闭整个会话；
- Esc 先关闭最深层，最深层关完后再关闭整个会话；
- 左右方向键在层级间回退/进入；
- 切换顶级项时复用同一 session id，但更新 owner/scope 并清空旧层级。

### 6.2 视觉与命中层

- 每一级面板使用 `UI_Z_OVERLAY + level` 注册/绘制；
- 先画父面板，再画子面板；子面板覆盖处命中优先；
- 面板背景注册一个命中（吞掉空白点击），行注册自己的命中；
- 子菜单行使用 `Ui_StateInstant`，避免快速移动时 hover 滞后；
- 顶级导航项使用 `Ui_State` 做颜色平滑；
- 会话已打开时，悬停顶级项或父行**立即**切换/展开下一级，无防抖延迟；
  关闭态不因鼠标路过而弹菜单，仍由点击打开。切换时只更新
  `openPath`/`openDepth`，不需要额外的等待帧。

### 6.3 弹层作用域

弹层在打开时捕获 `ui->inputOwner`：

```c
pm->scope = ui->inputOwner;   /* 打开时 */
```

绘制弹层前临时把 `ui->inputScope` 设为 `pm->scope`，结束恢复。
这样：

- 在虚拟对话框内打开的导航子菜单，即使帧末在 `UiDialog_End` 之后绘制，
  仍会以对话框 id 注册，能正确命中；
- 不在对话框内时 scope 为 0，不改变原有行为；
- 不再依赖“必须在 UiDialog_End 之前画弹层”的调用顺序。

如果将来支持“对话框里再开对话框”，`UiContext` 增加 `inputOwnerStack[]`；
popup session 只捕获栈顶 owner。弹层仍只有一个会话，不引入多会话栈。

## 7. 子菜单位置：硬性不越界

每级面板在每帧重新布局；不使用缓存位置，避免 resize/DPI/溢出重排后错位。

### 7.1 尺寸

```text
panelW = clamp(max(labelW + iconW + shortcutW + arrowW + padX*2),
               panelMinW, min(panelMaxW, viewW - 2m))
contentH = padY*2 + Σ rowH + Σ dividerH
maxH = min(panelMaxH, viewH - 2m)
panelH = min(contentH, maxH)
```

- `contentH > maxH` 时不滚动，而是只铺第一页行，并在末尾保留一行
  "更多 >>"；点击后在同会话的下层面板继续剩余行；
- 最小面板高度保证至少 `2*rowH + padY*2`（一行内容 + 一行"更多"），
  视口更小时优先保证不越界；
- 超长标签省略；
- 最小宽度不足时允许压到 `viewW - 2m`，不出现负宽。

### 7.2 位置

- 顶级菜单：从导航栏底边起算（不压住导航栏），优先锚点下方，空间不够翻
  上方；两侧都不够时取空间较大的一侧，高度按视口夹取，超出部分走"更多"续页；
- 子菜单：以**父面板外缘**为锚点，优先父面板右侧；右侧不够翻到父面板左侧，
  任意方向都不压住父面板；两边都不够时夹取到视口内，允许在极端窄窗口下
  覆盖其它面板，但绝不越界；
- 垂直：面板顶对齐父行，再按底边夹取；
- "更多"续页面板以"更多"行（或父行）为锚点，同样走 `UiPopup_Place`；
- 最后统一断言：

```text
x >= 8 && y >= 8 &&
x + w <= viewW - 8 &&
y + h <= viewH - 8 &&
w > 0 && h > 0
```

调试构建断言；发布构建静默夹取。

### 7.3 重排与失效

- 窗口缩放：下一帧重算每一级；父锚点不可见时关闭该分支（或退到最近的
  可见祖先），不画半截面板；
- 水平溢出变化：如果打开根项被收进 `>>`，会话转为 overflow 根，或直接关闭；
  v1 选择直接关闭该会话，避免锚点漂移；
- 主题/字体/DPI 变化：重新测量文本和行高。

## 8. 键盘

| 键 | 关闭时 | 打开时 |
| --- | --- | --- |
| Left/Right | 移动顶级焦点 | 子菜单内回退/进入子层级 |
| Up/Down | 移动顶级焦点 | 当前面板上一项/下一项 |
| Home/End | 首个/末个顶级项 | 面板首项/末项；末项为"更多"行 |
| Enter/Space | 叶子选中；父项展开 | 叶子选中；父项/更多行展开 |
| Esc | — | 关闭最深层；续页返回到父面板"更多"行；根层关闭会话 |
| Tab | 由应用处理 | 由应用处理；可先关闭会话 |

方向键只移动焦点；Enter 才提交选中。禁用项跳过，分隔线跳过。

## 9. 与现有组件的关系

- 不扩展 `UiTabBar`：TabBar 是页签选择，不是树形导航；
- 不把 `UiMenu` 直接改成树形：菜单仍保持单级简单模型；
- 复用：
  - `UiPopup_Place`：翻转/夹取；
  - `UiPopupManager`：单会话、外部点击、owner 回归；
  - `Ui_StateInstant`：子菜单行；
  - `Ui_State`：顶级项颜色渐变；
  - `UiIcon` / `UiTheme`：图标和颜色令牌；
  - `Ui_Anim`：只用于颜色（hover/选中强度），不用于几何。
- 不依赖 `UiScrollView`：导航栏和子菜单面板完全不滚动，只用 `>>`/
  "更多"续页。

## 10. 基础设施改造

### 10.1 动态控件表

当前 `UiContext.widgets[UI_MAX_WIDGETS]` 会在 256 个控件后静默丢弃。
改为：

```c
UiWidget *widgets;
int widgetCap;
```

- 首次注册按 `max(512, cap*2)` 增长；
- 新区域清零；
- 分配失败只丢弃当前注册并置 `widgetOom`，不崩溃、不越界；
- 新增 `Ui_ContextShutdown` 释放 widgets 和动画池；
- `UiApp` 在 `WM_DESTROY` 的 `shutdown` 回调之后、`Gfx_Shutdown` 之前调用。

动画池 `UI_AN_MAX_ENTRIES` 同样改为可增长，避免长导航的 hover 通道被
静默丢弃。`zStack/layerStack/hitClipStack` 保持固定深度并记录上限；
`wheelTargets` 改为可增长，避免多层滚动容器互相挤掉。

### 10.2 输入 owner 栈

为支持嵌套虚拟对话框：

```c
UiId inputOwnerStack[4];
int inputOwnerDepth;
UiId inputOwner;          /* 派生：栈顶 */
```

- `UiDialog_Open` 压栈，`UiDialog_Close` 完成时按 id 出栈；
- `widget_at` / `Ui_InputAllowed` / 滚轮过滤继续使用 `inputOwner`；
- 弹层会话捕获栈顶 owner；对话框关闭前不允许关闭它自己的弹层（沿用
  `UiDialog_Close` 的 `popupOpen` 检查）。

## 11. 测试与验收

### 11.1 headless

- 动态控件表：注册 2000 个控件、200 个滚轮目标、320 个动画通道，全部保留；
- 选中路径：设置完整路径、祖先高亮、路径暂时缺失后恢复、items 重排不漂移；
- 溢出：逐像素改变宽度，验证 visibleCount/overflow 单调、`>>` 菜单内容正确；
- 垂直溢出：底部出现 `>>`，点击后面板从第一个隐藏根部项开始；
- 长面板：`panelMore=1`、`VK_END` 聚焦"更多"、回车打开续页、
  续页 `panelStart` 连续、`inside_view` 通过、Esc 回到"更多"行；
- 定位：锚点遍历四角、窄视口 320×240、4K@200%，断言每级面板在视口内；
- 层级：4 级打开/Esc 逐级关闭；会话外点击关闭全部；
- 对话框内导航：子菜单在帧末绘制仍能命中；
- 动态存储与输入 owner 栈：扩容、乱序出栈、shutdown 释放。

### 11.2 图形/交互

- 水平 1080×720：顶级项、`>>`、子菜单、选中祖先；
- 垂直展开 + rail 切换；高度不足时底部 `>>`，无滚动条；
- 浅色/深色主题，100%/200% DPI；
- 键盘完整路径；Esc 逐级；
- 窗口缩放时面板重排，无越界、无残影。

### 11.3 回归

- `build.cmd` 全量构建，0 CRT；
- `cui_net_test`、`md_test`、`md_ui_test`、`gfx_rich_test`、`gfx_math_test`
  保持全绿；
- 现有 Select/Menu/Dialog 行为不变（特别是弹层外部点击和焦点回归）。

## 12. 实施顺序

1. 动态控件/动画池 + `Ui_ContextShutdown`；
2. input owner 栈 + popup scope 捕获；
3. `ui_nav.h/.c`：顶级布局、选中路径、水平溢出；
4. 子菜单会话、每级定位、分页/"更多"续页；
5. 垂直/rail、底部 `>>`、键盘、样例；
6. 测试、文档与回归。

## 13. 已定决策

1. 选中路径按 `UiId`，不按索引；路径暂缺不丢。
2. 父项默认只展开；`UI_NAV_SELECTABLE` 才提交父项选中。
3. 多级子菜单 = 一个 popup session，N 个面板；全局仍只有一个会话。
4. 水平溢出只收起尾部，不做横向滚动。
5. 弹层永不显示滚动条：超高时首页 + "更多 >>" 续页；任何情况下不得越出视口。
6. 垂直模式超高时底部显示 `>>`，与水平溢出同一模型。
7. 动画只做颜色渐变；面板开合即时。
8. 垂直展开/rail 的子菜单使用右侧 flyout，不使用 accordion。
9. `UI_NAV_MAX_DEPTH = 8`，逻辑层级与续页共用该上限。
