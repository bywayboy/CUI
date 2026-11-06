# CUI ScrollView 设计方案

> 目标：在即时模式 CUI 上实现一个类似 Element Plus `el-scrollbar` 的滚动容器。
> 容器只负责视口、裁剪、滚动、物理和滚动条；内容尺寸由布局/measure 结果提供。
> 设计原则：核心薄、缓存显式、双轴不自动、虚拟化可选。
>
> 实现状态：v1 已落地（`src/ui/ui_scroll.*` + `samples/scrollview.c`），
> 本文档同时作为该模块的长期设计说明。

---

## 1. 目标与非目标

### 1.1 目标

1. **通用容器**：固定视口，内容可超出；纵向为主，横向可后续扩展。
2. **自动内容尺寸**：由 `UiScrollLayout` 的 measure 缓存/前缀和得到 `contentH`，
   不要求固定行高，支持卡片、文本块、任意高度的控件组合。
3. **真正的离屏剔除**：应用只绘制可见块；完全在视口外的块不调用绘制、
   不注册命中，避免 `UI_MAX_WIDGETS`（256）被长列表撑爆。
4. **物理**：可配置滚轮平滑、惯性甩动、越界阻尼与回弹；帧率无关，
   静止后不再请求帧。
5. **el-scrollbar 风格**：overlay 滚动条、半透明 thumb、悬停/滚动淡入、
   自动隐藏、可拖拽、轨道翻页。
6. **命中裁剪**：视口外的部分即使控件矩形溢出，也不能接收鼠标事件。

### 1.2 非目标（v1 不做）

- 水平滚动、双轴自由画布。
- 嵌套滚动链（内层到边界后传给外层）。
- 估算总高的 provider 虚拟列表（只做“测量全部 + 绘制可见”）。
- 滚动锚定（scroll anchoring）的自动实现；v1 先做夹取，锚定用应用侧
  `UiScrollLayout_ItemY` 配合显式 `offset` 调整。
- 触摸/触控板手势；拖拽滚动 v1 只支持滚动条 thumb。
- 圆角视口裁剪；v1 使用矩形 clip。

---

## 2. 分层

```
应用 View
  ├─ UiScrollLayout     高度缓存 / 前缀 y / contentH / 可见区间
  └─ UiScrollView       视口 / offset / 物理 / 裁剪 / 滚动条
        ├─ Gfx_PushClip 绘制裁剪
        ├─ Ui_PushClipRect 命中裁剪
        └─ UiContext wheel target
```

`UiScrollView` 不感知子控件类型，不遍历内容。应用拿 `ContentRect` 和
`VisibleRange` 自己绘制可见块；这是即时模式里实现 O(可见) 的唯一干净方式。

---

## 3. 内容尺寸与缓存

### 3.1 UiScrollLayout

```c
typedef struct UiScrollItem {
    UiId     id;          /* 稳定 ID, 便于诊断 */
    uint32_t rev;         /* 该块内容版本 */
    float    height;      /* 最近一次测量高度 */
    float    y;           /* 含 padTop 的局部 y (缓存) */
    float    measuredW;   /* 测量时的可用宽度 */
    unsigned flags;       /* UI_SCROLL_ITEM_WIDTH_DEPENDENT */
    unsigned char measured;
} UiScrollItem;

typedef struct UiScrollLayout {
    UiScrollItem *items;  /* 调用方数组, 库不分配 */
    int count, cap;
    float width, contentH;
    float gap, padTop, padBottom;
    float fixedH;         /* >0: 固定行高模式, 忽略 items */
    uint32_t revision;    /* 每次 Commit 变化 */
    int dirty;            /* y/前缀需要重算 */
} UiScrollLayout;
```

规则：

- `UiScrollLayout_ItemNeedsMeasure(layout, i, rev)`：
  - 未测量、`rev` 不同，或该项标记为宽度相关且宽度变化 → 需要重测。
  - 固定行高模式永远返回 0。
- `UiScrollLayout_SetItemHeight()` 只写单项高度并置 `dirty`。
- `UiScrollLayout_Commit()` 重算前缀 y / contentH，`revision++`，
  返回 contentH 是否变化。O(N) 浮点运算，只在失效时发生。
- 滚动帧不调用 measure/Commit；只有宽度、数据 rev、字体/DPI 变化才重算。
- 文本块应标记 `UI_SCROLL_ITEM_WIDTH_DEPENDENT`；固定高度行不标记，
  resize 时只重算位置，不重测。
- 嵌套容器：子块高度变化 → 更新子项 rev/height → 父级或布局层 Commit；
  在即时模式里这由应用显式完成，库不建控件树。

### 3.2 自动 contentH

```c
if (width != layout.width) UiScrollLayout_SetWidth(&layout, width);
for (i = 0; i < count; i++)
    if (UiScrollLayout_ItemNeedsMeasure(&layout, i, block[i].rev))
        UiScrollLayout_SetItemHeight(&layout, i, block[i].rev,
            block_measure(&block[i], width));
UiScrollLayout_Commit(&layout);
```

然后：

```c
UiScrollView_Begin(&sv, &ui, viewport, innerW, layout.contentH);
```

`Begin` 会用最新 contentH 夹取 offset，因此不需要“先画再知道高度”。

---

## 4. UiScrollView

### 4.1 状态

```c
typedef struct UiScrollView {
    UiId id;
    UiScrollConfig cfg;
    UiScrollbarStyle bar;
    UiRect viewport;    /* 外层: wheel/hover/滚动条所在矩形 */
    UiRect inner;       /* padding box: 内容裁剪/命中/可见区域 */
    UiScrollPadding pad;
    float contentW, contentH;
    float offset;       /* 实际位置, 可短暂越界 */
    float target;       /* 目标位置 (滚轮/轨道翻页) */
    float velocity;     /* DIP/s, 正方向向下 */
    float barOpacity;
    float idleMs;
    int   mouseInside;
    int   draggingThumb;
    int   busy;
} UiScrollView;
```

状态由应用持有，生命周期与窗口/ViewModel 一致；库不做全局注册。

### 4.1.1 四向 padding 与滚动条槽

```c
UiScrollbarStyle bs = UiScrollbarStyle_Default();
bs.reserveSpace = 1;              /* 右侧给 thumb 留槽, 不压内容 */
UiScrollView_SetPadding(&sv, (UiScrollPadding){ 6, 4, 8, 6 });

float cw = UiScrollView_ContentWidth(&sv, viewport);
/* 用 cw 做 measure, 再 Begin(..., cw, contentH) */
```

- `viewport` 是外层矩形：滚轮、hover、滚动条定位都以它为准。
- `inner = viewport` 去掉四向 padding，再在 `reserveSpace=1` 时扣除
  `bar.width + bar.margin`。内容裁剪、输入命中、`Cull`、可见高度都用 `inner`。
- 上下 padding 是固定视口边距，不随内容滚动；左右 padding 缩窄内容宽度。
- `reserveSpace=0`：el-scrollbar 的 overlay 行为，内容可延伸到 thumb 下方；
  需要完全不遮挡时置 1，或把 `pad.right` 设得大于 thumb 宽度。
- `UiScrollView_ContentWidth()` 必须在 Begin 前调用，让测量宽度与内部一致。
- 滚动条轨道仍贴 viewport 右缘；`pad.right` 与 reserve 提供的内容缩进形成
  内容区和 thumb 之间的空隙。

### 4.2 帧流程

```c
UiScrollView_Tick(&sv, &ui, dtMs);              /* 动画帧; 更新物理/淡出 */
UiScrollView_Begin(&sv, &ui, viewport, cw, ch); /* clip + wheel + 夹取 */
    /* 应用绘制可见内容 (screen 坐标 = contentRect + local) */
UiScrollView_End(&sv, &ui);                     /* 弹 clip + 画滚动条 */
```

- `Begin`：注册滚动视口、注册 wheel target、消费本帧滚轮、压绘制 clip
  与命中 clip、夹取 offset。若 `maxOffset` 因内容/视口变矮而变小，
  立即把 offset 夹到新底部并清速度，thumb 同帧回到合法位置。
- `End`：弹 clip，在视口上方绘制滚动条并处理 thumb/track。
- 惯性积分只在速度归零时把 target 对齐实际位置；滚轮/拖动随时可中断，
  不会被惯性 target 覆盖。越界量小于 0.75 DIP 时直接吸附到边界，
  避免“弹簧速度小于一帧惯性衰减，永远落不到边界”的死区。
- `UiScrollView_ContentRect()` 返回内容面的屏幕坐标（y 已减 offset）。
- `UiScrollView_Cull(sv, rc)`：矩形与视口不相交时返回 1，应用直接跳过绘制。
- `Busy` 在惯性、回弹、淡出、隐藏倒计时期间为 1；应用在 `animating`
  回调里 OR 上它。

### 4.3 滚轮 / 惯性 / 回弹

- 滚轮：`target = clamp(target - delta * wheelStep, 0, maxOffset)`。
  `wheelTauMs=0` 时立即到目标；默认约 120ms 指数接近，桌面端不做长惯性。
- 惯性：`velocity` 以 `flingDecel` 线性衰减；`|v| < minVelocity` 停止。
  通过 `UiScrollView_Fling()` 注入（触摸/自定义手势/按钮演示）。
- 越界：`bounce=0` 直接夹取；`bounce=1` 允许最多 `maxOverscroll`，
  用阻尼弹簧回 0。越界量按 `overscrollResist` 压缩。
- 停止条件：位移接近 target、速度归零、回到边界内；随后 `busy=0`，
  不再请求帧。
- `dt` 夹到 ≤50ms，避免休眠唤醒后跳变。

### 4.4 滚动条

- overlay，不占内容宽度；默认宽 6 DIP、thumb 最小 24 DIP、右/上内缩 2 DIP。
- 轨道默认透明；悬停/拖动时显示淡轨道。
- thumb 高度 = `max(minThumb, trackH * viewH / contentH)`，
  位置按 `offset / maxOffset` 线性映射。
- 自动隐藏：滚动或鼠标在视口内时显示；停止后 `barHideDelayMs` 开始淡出，
  `barFadeMs` 完成。鼠标进入/离开由 `Begin` 记录的视口命中决定。
- 交互：thumb 拖拽按比例映射 offset；点击轨道翻一页；隐藏时不注册命中。
- 颜色从 `UiTheme` 解析，`UiScrollbarStyle` 只覆盖非 0 字段；换肤自动生效。
- 滚动条绘制在 `UI_Z_OVERLAY` 层，保证命中优先于内容。

### 4.5 命中裁剪与 wheel 路由

- `Ui_PushClipRect()/Ui_PopClipRect()` 维护输入裁剪栈；
  `Ui_RegisterWidget` 把当前 clip 与控件矩形一起保存，
  `Ui_HitTest` 同时检查两者。解决“部分露出视口的控件在视口外仍可点击”。
- `Ui_RegisterWheelTarget()` 登记视口矩形；`Ui_OnMouseWheel` 选择最后登记的
  命中目标（内层/后画优先），把累计 delta 与目标 id 放入上下文。
  `UiScrollView_Begin` 若匹配则消费并清除。嵌套链 v1 不向上传递。
- `UiApp` 增加 `WM_MOUSEWHEEL` 接线和可选 `wheel` 回调；不处理滚轮的应用
  由 ScrollView 自动消费。

---

## 5. 性能模型

| 场景 | 每帧成本 |
|---|---|
| 静止 | 0 帧；无可见动画 |
| 滚动 | O(可见块) 绘制 + 文本布局缓存命中；不 measure |
| 宽度/resize | O(N) measure/Commit 一次，之后回到 O(可见) |
| 单个块变高 | 该块 measure + O(N) 前缀重算 + 可视区/滚动条更新 |
| 超长列表 10k | 固定模式 contentH 为公式；只画约 viewH/rowH + 1 行 |

- 绘制裁剪 + 应用侧 cull 同时存在：Gfx 负责正确性，应用负责省掉调用。
- 文本布局缓存 128 条，可见行通常远小于该上限。
- 不在滚动内容里创建 D2D layer/effect；滚动条用普通绘制 + alpha。
- 滚动时整视口失效是必要的；静止/仅滚动条淡出时只失效滚动条窄条。
- `UiScrollView_Busy` + `UiApp.animating` 控制帧循环；隐藏倒计时期间仍会
  跑帧，但只重绘滚动条区域。

---

## 6. 已知取舍

- **测全部 vs 估算**：v1 对所有块 measure 一次并缓存，contentH/滚动条精确，
  但改宽时 O(N)。超大异构列表需要 provider + 估算 + 锚定，属于后续阶段。
- **双轴自动不可行**：文本换行高度依赖宽度；v1 固定宽度、自动高度。
- **滚动锚定**：v1 只夹取；如果上方块高度变化，应用可用
  `UiScrollLayout_ItemY()` 计算 delta 后自行调整 `offset`。
- **嵌套滚动**：v1 最后登记的目标独占滚轮；滚动链后续再加。
- **圆角视口**：v1 矩形裁剪；圆角卡片内滚动需要圆角 clip mask。

---

## 7. 实施阶段

1. `UiContext`：输入 clip 栈、wheel target/事件；`UiApp` wheel 消息。
2. `UiScrollLayout`：固定行高 + 变高缓存/前缀/可见区间。
3. `UiScrollView`：clip、offset、滚轮平滑、惯性、回弹、滚动条。
4. `samples/scrollview.c`：10k 固定行 + 变高卡片 + 展开/收起 + 主题。
5. 回归：0CRT、DPI、主题、裁剪命中、自动隐藏、CPU。

后续：水平/双轴、滚动锚定、嵌套链、provider 估算、圆角裁剪、拖拽滚动。
