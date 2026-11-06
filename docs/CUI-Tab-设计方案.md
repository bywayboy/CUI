# CUI TabBar / TabPager 设计方案

> 目标：即时模式下的页签选择 + 页面转场。TabBar 只管 header 状态，
> TabPager 只管当前页/目标页/进度；都不持有页面控件树。
>
> 实现状态：v1 已落地（`src/ui/ui_tab.*` + `samples/tabs.c`），
> 包含 LINE/PILL/CARD、横/纵/淡入转场、reverse、输入锁。

## 1. 分层

```text
UiTabBar     selected / focus / disabledMask / changed / 指示条
UiTabPager   from / to / progress / axis / fade / reverse
页面回调      (ui, user, index, pageRc, interactive)
```

- 应用持有两个 manager，通常放在 VM 里。
- TabBar 不画内容，Pager 不画 header；两者可以独立使用。
- 页面内容仍是即时模式：每帧用回调绘制，不保存控件对象。

## 2. TabBar

### 2.1 样式

- `UI_TAB_LINE`：下划线指示条，header 底部 1px 分隔线；悬停显示
  `primary.soft` 浅色底并提亮文字。
- `UI_TAB_PILL`：`neutral.soft` 胶囊容器，选中项白底 + 边框，等宽；
  非活动项悬停为 `primary.soft` 底 + 低 alpha primary 描边。
- `UI_TAB_CARD`：活动 tab 与内容面板连通；顶部两角圆角、底边留空。
  非活动项悬停为 `primary.soft` 底 + primary 混合描边。
  需要两阶段绘制：`UiTabBar_Draw` → 内容面板/页面 →
  `UiTabBar_DrawActive`。

### 2.2 状态

```c
UiTabBar {
    UiId id;
    int count, selected, focus;
    int focusVisible, changed;
    unsigned disabledMask;
    UiRect rc;
    int indicatorInit;
}
```

- 鼠标点击：更新 selected/focus，`changed=1`，点击态返回 index。
- 键盘：方向键/Home/End **只移动 focus**；Space/Enter 切换 selected。
  这与 ButtonGroup 的语义一致，也方便先预览再激活。
- 禁用项：鼠标不注册命中，键盘导航跳过，文字使用 disabled 颜色。
- 指示条：LINE 用 `UI_AN_USER` 两个通道平滑 x/width；宽度 = 文字宽 + 12 DIP，
  夹在 tab 项宽度内。选中或布局变化时自动追目标。

### 2.3 布局

- `stretch=1`：header 内等宽；PILL 默认。
- `stretch=0`：按文字宽 + `padX*2` 自适应；LINE 默认。
- v1 不换行；数量超出 header 时裁剪。后续复用横向 `UiScrollView`。

## 3. TabPager

```c
UiTabPager {
    UiId id;
    UiTabPagerConfig cfg;
    int from, to;
    float progress;   /* 0..1 */
    int animating;
    UiRect viewport;
}
```

- `GoTo(index, animate)`：动画中再次切换时先 snap 到当前目标，
  再从目标开始新转场，避免三页同时参与。
- `Tick(dtMs)`：线性 progress；绘制时做 ease-out cubic。
- 轴：横向 / 纵向 / 无（仅淡入）。
- `reverse`：只翻转滑动方向符号。
- 淡入淡出：`Gfx_PushAlpha` 包住单页绘制；仅转场期间有 layer。
- 页面矩形 `pageRc` 已包含滑动偏移；应用直接在其中布局，不用 Gfx
  全局 transform。
- 转场期间 `Ui_PushInputBlock`：页面控件不注册命中/滚轮目标，但照常绘制；
  header 不受影响。结束后自动恢复。
- 固定内容视口；页面高度不同不要自动改外层高度，避免外层重排。

## 4. 交互边界

- 点击 header 立即切换 selected，并启动 Pager 转场；指示条与页面同方向。
- 转场中页面控件不可点；header 仍可点，快速连点会 snap 后重新转场。
- 方向键不直接切页；Space/Enter 才切。PageUp/PageDown 由应用决定是否绑定。
- 滚轮不用于切页，避免与页面内 ScrollView 冲突；触摸手势后续再做轴锁定。
- 页面内按钮 ID 必须包含页索引，转场时旧页/新页同时绘制也不会撞 ID。

## 5. 已知限制

- 无 CARD 样式、无关闭按钮/badge。
- header 数量多时只裁剪，没有横向滚动。
- 无自动高度、无手势滑动、无页面 keep-alive/懒加载。
- 转场期间页面输入整体锁定（约 260ms），不是“新页先可点”。

## 6. 阶段

1. LINE/PILL/CARD + Pager 横/纵/淡入 + reverse + 输入锁（已完成）。
2. badge/icon、header 横向滚动。
3. 触摸手势、页面懒加载、页面高度补间（如确实需要）。

## 7. CARD 样式实现记录

> 已按 P1~P4 落地。下面是实现细节与验收记录，供后续维护/扩展参考。

### 7.1 目标外观

```text
╭──概览──╮ ┌─网络─┐ ┌─高级─┐
│        ╰─┴──────╯ ...      │
├────────────────────────────┤  ← 内容面板顶边只在非活动 tab 下存在
│  page content              │
╰────────────────────────────╯
```

- 活动 tab：上两角圆角 `{R,R,0,0}`，填充 = 内容面板底色，左右/上描边延续到
  内容面板；底边不画，和面板连成一体。
- 非活动 tab：透明或 `neutral.soft`，底边落在 header 基线上；悬停浅底。
- Header 高度 36，`padX=14`，tab 间距 2~4；默认按文字宽，不强制等宽。
- 无下划线指示条；焦点环内缩 2 DIP，不能和 card 边框叠成双线。
- 内容面板：左右/下边框照常，顶边在活动 tab 区间断开。

### 7.2 必须先补的几何能力

`GfxCorners` + `FillRoundRectCorners` 已有；缺一个**缺边描边**：

```c
/* edgeMask: bit0=top, bit1=right, bit2=bottom, bit3=left; 0=全部 */
void Gfx_StrokeRoundRectOpen(Gfx *g, GfxRect r, GfxCorners c,
    unsigned edgeMask, GfxColor color, float width);
```

- CARD 活动 tab 只需要 top+left+right，底边留空，避免和内容面板顶边叠成 2px。
- 实现可复用现有 path geometry 缓存，增加 edgeMask 到 cache key；不能用
  “画完整边框再盖 1px” 的 hack，否则层级/颜色变化时会露线。
- 备选：用 `Line` + `StrokeArc` 手工拼三条边；语义更差，不推荐。

### 7.3 绘制顺序与 API

CARD 的关键是“活动 tab 盖住内容面板顶边”，需要两阶段：

```c
/* 1. header: 背景、非活动 tab、焦点环、注册命中 */
int UiTabBar_Draw(...);                 /* variant=CARD */

/* 2. 内容面板 + 页面 (由应用/Pager 绘制) */
UiTabPager_Draw(...);

/* 3. 活动 tab 覆盖在内容顶边上; 只画不注册, 不改变状态 */
void UiTabBar_DrawActive(UiTabBar *tb, UiContext *ui,
    const UiTabStyle *style);
```

- `UiTabBar_Draw` 保存最后一个活动 tab 的矩形，`DrawActive` 用它；
  或提供 `UiRect UiTabBar_ActiveRect(const UiTabBar*)`。
- `DrawActive` 必须在内容面板之后调用；它只画活动 tab 的填充 + 三边描边，
  用面板底色盖住顶边。
- 活动 tab 的命中/悬停仍由第一阶段注册的 item 负责，避免重复注册。
- 不把 Pager 和 TabBar 互相耦合；两者仍可独立使用。

### 7.4 样式字段

在 `UiTabStyle` 上增加 CARD 相关字段（0 = 主题默认）：

```c
UiColor cardBg;          /* 非活动 tab 底: 0 -> transparent/soft */
UiColor cardBorder;      /* tab 描边: 0 -> neutral.border */
UiColor contentBg;       /* 内容面板底: 0 -> theme.card */
UiColor contentBorder;   /* 内容面板边: 0 -> neutral.border */
float   contentRadius;   /* 内容面板下两角, 0 -> style.radius */
int     mergeContent;    /* 1 = 活动 tab 与面板连通 */
```

避免再增加 `underline/card/tabbed` 多个 bool；仍用 `variant=UI_TAB_CARD`
一个语义，`mergeContent` 只控制是否做接缝。

### 7.5 分阶段实施

**P1 — 静态 CARD header（无接缝）**

- 新增 `Gfx_StrokeRoundRectOpen`（edgeMask 参与几何/描边 cache key）。
- `UiTabBar` 支持 `UI_TAB_CARD`：活动 tab 上圆角填充、非活动 tab 悬停、
  禁用/焦点环、header 基线。
- 内容仍由应用画独立 card；活动 tab 暂不盖面板顶边。
- 验收：100%/200% DPI 下 1px 边框不双线、不缺口；浅/深主题正确。

**P2 — 活动 tab 与面板连通**

- 保存活动 tab 矩形；新增 `UiTabBar_DrawActive`。
- 应用先画 header，再画内容面板/页面，再画 active overlay。
- 面板顶边在活动 tab 区间断开（可用 active rect 分两段画，或由 overlay
  盖住；推荐前者，避免背景非纯色时盖不干净）。
- 转场期间 overlay 仍画在内容之上；Pager 的输入锁不受影响。
- 验收：活动 tab 底部无 2px 双线；滑动转场时 header 不被内容裁切；
  reverse/纵向/淡入均正常。

**P3 — 交互与视觉打磨**

- 非活动 tab hover 浅底、按下加深；活动 tab 不响应 press 变色。
- 指示条不再出现；焦点环与 card 边框分离。
- 活动 tab 切换时不做位置动画（CARD 通常瞬切）；如需动画，只做
  header 内部的 fade，不移动边界。
- 溢出 header 先裁剪；后续复用横向 `UiScrollView`。
- disabled tab 不参与点击/键盘/转场。

**P4 — 回归与文档**

- 截图：LINE/PILL/CARD × 浅/深 × 100/200% DPI × 4 页。
- 场景：点击切换、键盘 Space、reverse、纵向、淡入、转场中输入锁、
  内容面板 resize、活动 tab 靠边。
- 0CRT 构建、静止 CPU、`build.cmd` CRLF 保持。
- 白皮书 8.13 增加 CARD 用法；README 增加 `cui_tabs` 的 CARD 截图/验证。

### 7.6 已定决策

1. CARD 默认**文字宽**（`stretch=0`）。
2. tab 间距 **2 DIP**，不依赖 border 折叠。
3. 内容面板由应用绘制；TabBar 提供 `UiTabBar_ActiveRect` 和
   `UiTabBar_DrawActive`，不引入 `UiTabView` 复合管理器。
4. badge/关闭按钮留到后续；当前不进入 seam/命中区域设计。

