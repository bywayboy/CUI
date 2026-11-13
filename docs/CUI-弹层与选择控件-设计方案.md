# CUI 弹层交互管理器 + Select/Menu 设计方案

> 状态：已实施（v0.9 批量）
> 关联：`src/ui/ui_popup.*`、`src/ui/ui_select.*`、`src/ui/ui_menu.*`、
> `samples/overlays.c`、白皮书 §8.16

## 1. 目标

在已有 `UiPopup_Place` 纯几何定位之上，补一层**应用持有的弹层交互管理器**，
解决 Select / Menu 这类"点击触发器打开、点外部或 Esc 关闭、关闭后焦点回到
触发器"的通用问题；再用它实现：

- `UiSelect`：单选下拉，支持长列表滚动、禁用项、键盘操作；
- `UiMenu`：下拉菜单 / 右键上下文菜单，支持禁用、分隔线、快捷键提示、危险项。

非目标（本批不做）：

- 多级子菜单、菜单栏（MenuBar）的悬停切换；
- Select 的搜索过滤、多选、可编辑输入；
- 原生独立弹层窗口（跨窗口/跨屏）；弹层仍绘制在主窗口客户区内；
- 虚拟 provider / 表格。

## 2. 为什么需要管理器

即时模式 UI 没有控件对象生命周期，弹层需要跨帧保持 4 件事：

1. 当前打开的是哪个弹层（只允许一个）；
2. 触发器是谁（关闭后 `navId` 回归）；
3. 点击"外部"的判定（弹层打开时底层控件不参与命中）；
4. Esc / 焦点可见性等键盘语义。

这些状态不能放全局单例（同进程多窗口会互相污染），因此由 App 持有
`UiPopupManager`，每个窗口一个；绘制仍走现有的 `UI_Z_OVERLAY` 层。

## 3. 架构与生命周期

### 3.1 状态

```c
typedef struct UiPopupManager {
    UiId   openId;       /* 当前弹层 id, 0 = 关闭 */
    UiId   ownerId;      /* 触发器 id, 关闭后焦点回归 */
    int    focusOnClose; /* 关闭后是否显示键盘焦点样式 */
    int    closed;       /* 单帧: 本帧发生了关闭 */
    int    outsidePress; /* 单帧: 打开期间在弹层外按下 */
} UiPopupManager;
```

`UiPopupManager` 不拥有弹层内容；内容结构（`UiSelect` / `UiMenu`）仍由 App 持有。

### 3.2 每帧顺序

```c
UiPopupManager_BeginFrame(&pm, &ui);   /* 每帧最前: 同步 popupOpen / 外部点击 */
... 普通控件 ...
UiSelect_DrawPopup(&sel, &pm, &ui);    /* 帧末: 按 z 顺序画弹层 */
UiMenu_Draw(&menu, &pm, &ui);
```

- `BeginFrame` 把 `ui->popupOpen` 设为 `openId != 0`；
- 打开/关闭时立即同步 `ui->popupOpen`，保证下一次鼠标消息的命中路由正确；
- 弹层内容在帧末绘制，避免被后画的普通控件覆盖；
- 弹层面板与菜单项在 `UI_Z_OVERLAY` 层注册命中，保证输入优先。

### 3.3 打开/关闭

| 动作 | 结果 |
| --- | --- |
| 触发器点击 / Enter / Space / Down | `Open(pm, id, owner, focusOnClose)` |
| 点击弹层内**面板空白** | 保持打开（面板已注册命中） |
| 点击弹层外任意位置 | `BeginFrame` 检测 `mouseDown && pressed==0` → 关闭 |
| Esc | `UiPopupManager_Key` 关闭，并显示触发器焦点样式 |
| 选中菜单项 / Select 项 | 控件提交后 `Close` |
| 窗口失焦 / 关闭 | App 调 `Close`（或随窗口销毁） |

关闭时：

```c
ui->popupOpen = 0;
ui->navId = pm->ownerId;
ui->navFocusVisible = pm->focusOnClose;
```

鼠标打开的弹层 `focusOnClose=0`（不出现键盘焦点环）；键盘打开 / Esc 关闭为 1。

### 3.4 输入路由

弹层打开时：

- 鼠标命中只看 `UI_LAYER_OVERLAY`（`Ui_HitTest` 已支持 `popupOpen` 过滤）；
- 底层控件仍正常绘制与注册，但不响应 hover/press/click；
- 滚轮目标新增 `layer` 字段，`popupOpen` 时只接受 overlay 层目标，
  防止弹层下面的 ScrollView 跟着滚；
- 右键不进入 `UiContext`，由 App 的 `mouse` 回调新增
  `UI_APP_MOUSE_RDOWN/RUP` 事件处理，避免误触底层控件。

**空区域按下修正**：原实现 `Ui_OnMouseDown` 在未命中控件时直接返回，
导致 `clickedOutside` 永远不置位。现在按下即锁存 `mouseDown=1`、
`pressed=0`，抬起时 `clickedOutside=1`，作为弹层外部点击的兜底信号。

**捕获拖动期间忽略 MouseLeave**：窗口按下后会 `SetCapture`，指针划出客户区
仍能收到移动消息；原 `Ui_OnMouseLeave` 无条件把 `mouseY` 清成 -10000，
会让 ScrollView 的 thumb 拖动、Slider 拖动瞬间跳回 0。现在 `mouseDown` 或
`drag.active` 期间只清 hover，不清坐标；松开后由 `Ui_BeginFrame` 的真实
光标检查恢复 `mouseOver`。

## 4. UiSelect

### 4.1 状态

```c
typedef struct UiSelect {
    UiId id;
    const char *const *items;
    int count;
    int selected;       /* 单选选中 / 多选最近切换项; -1 = 无 */
    int focus;          /* 打开时高亮项 */
    int multiple;       /* 多选 */
    unsigned mask;      /* 多选位图, 最多 32 项 */
    int filterable;     /* 顶部搜索框 */
    unsigned disabledMask;
    UiScrollView sv;    /* 列表滚动区: 复用 ScrollView (offset/拖动/滚动条) */
    float rowH;         /* 默认 32 */
    float maxListH;     /* 默认 280 */
    UiRect listRc;      /* 本帧列表矩形 (含搜索框) */
    int visCount;       /* 过滤后可见项数 */
    int searchActive, blinkOn;
    UiEdit edit;        /* 搜索框 (UTF-16) */
    char filter[128];   /* 搜索词 UTF-8 */
    UiIme *ime;
    int changed, committed;
} UiSelect;
```

### 4.2 绘制

- 触发器：圆角 6 DIP，`neutral.base` 底、`neutral.border` 描边；
  hover/打开/键盘焦点时描边转向 `primary.base`；
- 右侧箭头改为矢量 chevron（两段圆头线），展开/收起做 180ms ease-out
  旋转（0° 向下 / 180° 向上），颜色随角度从 `text3` 混到 `primary.base`；
  控件内用 `GetTickCount64` 计时并在动画期间自行 `InvalidateRect` 触发器，
  不依赖应用持续 `animating` 回调；`animReduced` 直接跳终态；
- 文本：单选显示选中项；多选显示前两个标签 + `+N`（`mask` 位图）；
  未选择显示 placeholder；`Ui_Select` 每帧参数非空优先，否则用
  `UiSelect_SetPlaceholder` 的持久文本，两者为空才留白（`text4`）；超出省略；
- 搜索：`filterable` 时面板顶部内嵌 `UiEdit`，提示文本由
  `UiSelect_SetSearchHint` 配置（默认 L"搜索…"）；打开时接管
  `UiContext.focusId` 与 `UiIme`，关闭时取消组合并把 IME 焦点置空；
  ASCII 大小写不敏感子串匹配，中文按 UTF-8 字符精确匹配；无结果显示“无匹配项”；
- 列表：帧末绘制，`toastBg` 底 + `toastBorder` 描边 + 8 DIP 圆角；
  定位复用 `UiPopup_Place`（below 优先，空间不足翻 above，align start）；
  宽度取 `max(触发器宽度, 最长项文本 + 内边距)`，封顶视口；
- 行：hover 用 `primary.soft` 圆角条；选中项文字 `primary.base`，
  右侧显示 check glyph（`E73E`）或 "✓"；禁用项 `btnDisabledText` 且不注册命中；
- 列表项状态用 `Ui_StateInstant`：hover 直接 0/1，不做平滑淡变，避免快速
  移动时的滞后感；也不为临时行创建动画条目；
- 列表滚动区**复用 `UiScrollView`**，不另写一套滚动物理：
  - 帧末在 overlay 层 `UiScrollView_Begin(view=panel, contentH=count*rowH)`
    → Select 按 `sv.offset` 只画可见行 → `UiScrollView_End` 画滚动条；
  - 因此滚轮、thumb 拖动、轨道点击翻页、滚动条 hover 高亮、内容缩高夹取、
    右侧预留槽全部由 ScrollView 提供；
  - 滚动条样式：宽 6 / 最小 thumb 24 / margin 2 / `reserveSpace=1` /
    `alwaysVisible=1`；track 用全透明色，只显示 thumb（el-scrollbar 观感）；
  - 滚轮步长 `wheelStep=rowH*3`、`wheelTauMs=100`（平滑尾随，与
    Markdown/TextArea 一致；`UiSelect_SetScrollSmoothing(0)` 恢复即时）；
    键盘 focus 滚入视口仍为即时；`UiSelect_Busy()` 供 animating 出帧；
    列表项绘制用 `Ui_PxSnap` 对齐滚动位移，避免亚像素抖动；
  - 弹层没有 UiApp 的 tick 回调，在 End 后补一次 `UiScrollView_Tick(16ms)`，
    让轨道翻页写入的 `target` 立即落地；thumb 拖动/滚轮本身不依赖 Tick；
  - 键盘 focus 移动后调用 `UiScrollView_ScrollTo(..., animate=0)` 把该项
    滚入 `sv.inner`，不再维护自己的 scrollPx。

### 4.3 键盘

| 键 | 关闭时 | 打开（无搜索） | 打开（搜索框聚焦） |
| --- | --- | --- | --- |
| Down / Up | 打开并聚焦下一/上一项 | 下一/上一项（跳过禁用） | 在过滤结果中移动 |
| Home / End | 打开并聚焦首/末项 | 首/末项 | 编辑搜索词光标 |
| PageUp / PageDown | — | 翻 5 行 | 在过滤结果中翻 5 行 |
| Enter | 打开 | 单选提交 / 多选切换 | 单选提交 / 多选切换 |
| Space | 打开 | 单选提交 / 多选切换 | 输入空格 |
| Backspace/Delete/左右 | — | — | 编辑搜索词 |
| Esc | — | 关闭（管理器处理） | 关闭（管理器处理） |

`focus` 变化时用 `UiScrollView_ScrollTo(..., 0)` 保证该项可见；鼠标点选直接提交。
多选点击/Enter 只切换、不关闭，关闭时 `committed=1`。

### 4.4 搜索 / IME 接线

```c
UiSelect_SetFilterable(&tags, 1);
UiSelect_AttachIme(&tags, &app->ime);

/* UiAppCallbacks */
cb.chr   = on_chr;    /* UiSelect_Char(&tags, &ui, ch) */
cb.ime   = on_ime;    /* UiSelect_OnIme(&tags, &ui) */
cb.timer = on_timer;  /* UiSelect_Blink(&tags, &ui), 530ms */
```

`sel_search_begin` 在弹层第一次绘制时执行：清空搜索词、`Edit_SetText`、
`Ui_SetFocusId(searchId)`、`UiIme_SetFocus(&edit)`；`sel_search_end` 在检测到
弹层关闭时执行：取消组合、IME 焦点置 NULL、清空搜索词。应用若同窗口还有
`UiInput` 字段，应在关闭后把 IME 焦点重新指回该字段。

过滤不改变原数组：`filter` 变化时重算 `visCount`，绘制/键盘通过
`match_at/vis_of` 在“可见序号 ↔ 原始下标”之间映射；命中项多时是
O(n·可见行) 的扫描，列表量级 ≤1000 可接受，超大列表仍应等虚拟 provider。

## 5. UiMenu

### 5.1 数据模型

```c
enum { UI_MENU_DISABLED = 1u << 0, UI_MENU_DIVIDER = 1u << 1,
       UI_MENU_DANGER = 1u << 2 };

typedef struct UiMenuItem {
    const char *label;
    const char *shortcut;   /* 右对齐提示，可为 NULL */
    unsigned flags;
} UiMenuItem;

typedef struct UiMenu {
    UiId id;
    const UiMenuItem *items;
    int count;
    int focus;
    int item;        /* 上次激活项 */
    int changed;
    UiRect anchor;
    float rowH;      /* 默认 30 */
} UiMenu;
```

### 5.2 定位与绘制

- `UiMenu_OpenAt(menu, pm, ui, anchor, focusOnClose)`：anchor 是触发按钮
  矩形或鼠标点（`w=h=0`）；菜单在 anchor 下方展开，空间不足翻上方；
  `UiPopup_Place` 负责左右夹取；
- 宽度 = max(标签 + 快捷键 + 左右内边距 + 勾选槽)，最小 160 DIP，封顶视口；
- 高度 = 项数 × rowH + 分隔线高度（9 DIP）+ 上下 4 DIP；
- 分隔线：1 px `neutral.border`，不注册命中、不参与键盘；
- hover：`primary.soft` 圆角条，使用 `Ui_StateInstant`（瞬时，无淡变滞后）；
  危险项文字 `error.base`；禁用项变灰；
- 右键上下文菜单：App 在 `UI_APP_MOUSE_RDOWN` 里记录点并打开菜单，
  菜单打开后该次右键不再触发底层控件。

### 5.3 键盘

Up/Down 跳过禁用与分隔；Home/End；Enter 激活；Esc 关闭。
激活后 `changed=1`、`item=index`，关闭并把焦点还给 owner。

## 6. UiApp 右键回调

`UiAppCallbacks.mouse` 的事件枚举新增：

```c
UI_APP_MOUSE_RDOWN = 4,
UI_APP_MOUSE_RUP   = 5
```

`WM_RBUTTONDOWN/UP` 只转给 App 回调，不进入 `Ui_OnMouseDown/Up`，
因此不会产生控件点击；App 用坐标打开 `UiMenu` 即可。

## 7. 边界与后续

- 弹层不能超出客户区，也不会跨到窗口外；需要跨屏时再做原生弹层窗口；
- 弹层不做碰撞避让，只避让触发器和视口；覆盖邻近控件是正常浮层语义；
- Select 列表项是固定行高；超大列表（>1000）应改用后续的虚拟 provider；
- 多选上限 32 项（unsigned mask）；搜索是 UTF-8 子串匹配，不做拼音/分词；
- 可搜索 Select 会接管窗口 IME 焦点；与 UiInput 同窗使用时，关闭后应用需
  把 `UiIme_SetFocus` 重新指回仍在编辑的表单字段；
- 菜单暂不支持子菜单、图标列、勾选态；数据结构已留 `flags` 扩展位；
- 弹层外观无阴影（Gfx 尚无低成本阴影 API），用边框 + 背景区分层次。
