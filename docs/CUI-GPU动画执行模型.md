# CUI GPU 动画执行模型（合成器交叉淡化）

> 目标：状态类动画（hover / press / 选中 / 展开）的**插值全部在 GPU 合成器**完成；
> UI 线程只在状态变化时渲染一帧，动画期间**零重绘**。打不断、掉不了帧，
> 4K 最大化窗口下同样 60Hz 顺滑。

---

## 1. 根因：问题不在曲线，在执行位置

现行动画是“CPU 逐帧算值 + 整场景重绘”：

```
输入 → 记录目标值 → [每帧] UI 线程: 推进动画值 → 重画整个场景 → Present
```

三个结构性缺陷：

1. **每帧成本 = 整场景**。`app_render` 每帧 `Gfx_Clear` + 全页绘制；4K 最大化时
   D2D 几何/文字/特效的成本按像素与图元数增长，帧预算被吃满就会掉帧。
2. **掉帧会“跳值”**。动画值按绝对时间求值，UI 线程卡一下，下一次绘制直接落到
   当前时刻的值 → 体感“先延迟，然后突变”。
3. **打断不可控**。同级状态在 CPU 里互相覆盖，谁后写谁赢；多控件同步只是
   “碰巧同帧推进”。

结论：调时长、调 ease 只是把症状挪位置。必须换执行者。

---

## 2. 目标与非目标

### 目标
- 状态动画的插值由 DComp 合成器执行，**与 UI 线程解耦**；
- 动画运行期间 UI 线程**不重绘、不逐帧计算**（可休眠）；
- 可打断：新状态从**当前屏幕呈现值**继续，不跳变；
- 同一次事件里的多个控件动画**同一提交、同一时间轴**；
- 4K@192 最大化、120Hz 下无掉帧；
- 纯 C（`src/ui`）+ 现有 C++ gfx 核心，无第三方库。

### 非目标
- 不把整个 UI 改成保留型可视树（成本不可接受，见 §9）；
- 不改布局/命中/绘制语义（仍是 immediate mode）；
- 滚动惯性、文本光标闪烁、进度条这类“连续量”不在本方案范围（保留 CPU 或单独处理）。

---

## 3. 方案总览

两个原语：

| 原语 | 用途 | GPU 做什么 |
|---|---|---|
| **SnapshotFx（交叉淡化）** | 颜色/透明度/选中态变化 | 两张快照的不透明度补间 |
| **PartFx（变换部件）** | 位移/旋转（Switch 滑块、Tab 指示器、chevron） | visual 的 Offset/Transform 补间 |

本期先做 SnapshotFx；PartFx 在 M4 落地。两者都用 `IDCompositionAnimation`
提交后由合成器求值，UI 线程不参与。

### 3.1 SnapshotFx 核心思想

状态变化时，矩形内“变化前”和“变化后”的像素各拍一张快照：

```
父 visual (位于 rect, 物理像素)
 ├─ 子 visual A：content = 旧快照表面（不透明）
 └─ 子 visual B：content = 新快照表面，effect-group opacity 0 → 1（合成器动画）
```

- 旧快照取自**主 swapchain 后缓冲**（上一帧的屏幕内容），包含背景，故完全不透明；
- 主表面在动画期间**立即显示新状态**，被覆盖层完全遮住；
- 合成器把 B 的不透明度从 0 补到 1，视觉上就是该矩形从旧状态淡到新状态；
- 矩形外的像素新旧帧完全一致，无需处理；
- 动画结束移除覆盖层 → 露出主表面（已经是新状态，逐像素一致，无跳变）。

颜色渐变与真实颜色插值等价：对同一背景 G，两个状态的混合像素
`(1-p)·F(S0,G) + p·F(S1,G)` 正是颜色值线性插值的渲染结果，与 CPU 算
`mix(color0, color1, p)` 在视觉上一致。文字、描边、阴影一并正确。

### 3.2 时间轴与曲线

- 时长：默认 **250ms**（与 macOS Core Animation 隐式动画默认一致），
  hover 200ms / press 120ms 可分别覆盖；
- 曲线：**smoothstep** `s(p)=3p²−2p³`（三次 ease-in-out），
  它在时间域是**三次多项式**，可以**精确**写进
  `IDCompositionAnimation::AddCubic`：

```
p = t / d
value(t) = 3p² − 2p³ = 0 + 0·t + (3/d²)·t² + (−2/d³)·t³
anim->AddCubic(0, 0, 0, 3/d², −2/d³);
anim->End(d, 1.0);
```

同一多项式在 CPU 侧只用于**打断时刻求当前值**（每次事件算一次，非逐帧）。

### 3.3 打断：从当前呈现值继续

某矩形上已有动画，又来一次状态变化时：

1. CPU 用同一多项式求出该覆盖层当前的不透明度 `p`；
2. GPU 一次 D2D 合成：把 `旧图·(1−p) + 新图·p` 烘到一张新表面（“当前呈现快照”）；
3. 销毁旧覆盖层，新建覆盖层：`A' = 烘焙结果`、`B' = 最新快照`，从 0→1 起跑。

结果：第二次变化从**屏幕上正显示的值**继续，不回到旧状态、也不跳到新状态。

### 3.4 同事件多控件

一次点击改两个 Radio（旧选中、新选中）→ 两个矩形各自一条 SnapshotFx，
在同一提交里创建、同一时刻起跑。它们互不依赖，天然锁步。

---

## 4. 帧序（零动画重绘的关键）

```
输入事件（mouse move/down/up、click 回调、键盘）
  └─ 状态更新（模型值 = 最终值）
  └─ Gfx_FxQueue(rect, dur, curve)
       └─ 立即从后缓冲抓“旧快照”（此时后缓冲 = 上一帧屏幕）
       └─ InvalidateRect(rect)
WM_PAINT
  ├─ Gfx_Begin：照常绘制新状态（整场景一次）
  ├─ Gfx_End：EndDraw
  │    ├─ 从后缓冲抓“新快照” → 建覆盖层 → 提交合成器动画
  │    └─ Present
  └─ 返回。之后 UI 线程不再为这条动画做任何事。
动画期间
  └─ 主循环按“最早动画结束时间”休眠；到点清理覆盖层（不绘制）
```

关键点：
- 旧快照在**输入时**抓，避免中间插入绘制导致“旧=新”；
- 覆盖层提交在 Present **之前**，首个合成帧覆盖层不透明度为 0、内容=旧帧，
  与屏幕一致，不会闪一帧新状态；
- 动画期间主循环可休眠，CPU 占用回到静止水平。

---

## 5. API 设计

### 5.1 gfx 核心（C 接口，`gfx_fx.h`）

```c
/* 为 DIP 矩形登记一次“状态变化”：立即抓旧快照。
 * durMs<=0 或非 DComp 路径返回 0（调用方走降级路径）。 */
int  Gfx_FxQueue(Gfx *g, GfxRect rc, int durMs, int curve);

/* 撤销所有进行中的覆盖层（resize/DPI/滚动/主题切换/隐藏窗口时调用） */
void Gfx_FxCancelAll(Gfx *g);
void Gfx_FxCancelRect(Gfx *g, GfxRect rc);

/* 还有动画在跑？主循环据此决定“休眠到结束”还是普通等待 */
int  Gfx_FxBusy(Gfx *g);
/* 距最早动画结束还有多少毫秒；无动画返回 -1 */
int  Gfx_FxIdleMs(Gfx *g);
/* 回收已完成覆盖层（不绘制）；主循环到点调用 */
void Gfx_FxTick(Gfx *g);
```

内部实现：
- `IDCompositionSurface`（W×H 物理像素，PREMULTIPLIED BGRA）存两张快照；
- 父 `IDCompositionVisual` + 两个子 visual；B 挂 `IDCompositionEffectGroup`，
  `SetOpacity(IDCompositionAnimation*)` 由合成器求值；
- 覆盖层挂在专用的 `fxRoot` visual 下，位于主内容之上、Toast/弹层之下；
- 表面按“像素尺寸分箱”池化，避免每次 hover 都建新表面；
- `Gfx_End` 中在 `EndDraw` 之后抓新快照、提交动画，再 Present。

### 5.2 UI 层（`ui_fx.c`，纯 C）

```c
/* 状态变化时登记：按 UiId 查上一帧控件矩形，转 Gfx_FxQueue。
 * 同一事件内多次登记会合并为一个矩形集合。 */
void Ui_FxChangeId(UiContext *ctx, UiId id, int durMs);
void Ui_FxChangeRect(UiContext *ctx, UiRect rc, int durMs);
void Ui_FxCommit(UiContext *ctx);      /* 帧末/绘制前统一提交给 gfx */
void Ui_FxCancelAll(UiContext *ctx);
```

集成点：
- `Ui_OnMouseMove`：hover 旧/新两个 id → 两次 `Ui_FxChangeId`；
- `Ui_OnMouseDown` / `Ui_OnMouseUp`：press 变化 + hover 恢复；
- Radio/Checkbox/CheckboxGroup/Switch：状态在**输入事件**内落（click 回调），
  同时 `Ui_FxChangeId` 旧/新两项；
- Tab 指示器、Switch 滑块：M4 用 PartFx。

绘制侧不改：`Ui_State` 直接返回最终权重（hover=1/0），因为过渡由覆盖层负责。

### 5.3 降级

| 环境 | 行为 |
|---|---|
| DComp 可用 | SnapshotFx（合成器） |
| ULW 回退 / 无合成器 | 保留现有 CPU 通道动画（旧内核），或直接落最终值 |
| `Ui_AnimSetReduced` | 不排队，直接落最终值 |

---

## 6. 失效与边界

- **resize / DPI / 主题切换**：`Gfx_FxCancelAll`，主表面重画，动画即时落位；
- **滚动 / 布局移动**：动画矩形会“错位”，调用 `Gfx_FxCancelRect` 或全局取消；
- **弹层/Toast 打开**：覆盖层在其下方，不受影响；若覆盖层压在弹层触发器上，
  下次事件会重新排队；
- **WebView 子 visual**：位于 fxRoot 之上，区域不参与淡化；
- **窗口隐藏/最小化**：取消全部；
- **区域过大**（如整页切换）：设面积上限（默认 ≤ 1024×1024 物理像素）；
  超限直接落最终值（过渡动画不明显，且省显存）；
- **矩形合并**：同一事件的重叠矩形求并集；与进行中覆盖层重叠时走 §3.3 烘焙。

---

## 7. 性能与预算

- 单次 hover 常见矩形 ~200×30 DIP @2x = 400×60 px ≈ 94KB/张，两张 + 池化；
- 每次状态变化：一次整场景绘制（本来就要画）+ 两张 GPU 拷贝 + 一次 Commit；
- 动画期间：UI 线程 0 次绘制、0 次动画计算，合成器按刷新率合成；
- 上限：同时覆盖层数 ≤ 8，超出按最旧回收；表面池总字节设上限，LRU 复用。

---

## 8. 实施步骤

### M0 gfx 合成器原语（1 步）
- 新增 `gfx_fx.h/.cpp`（或并入 gfx_d2d.cpp）：快照表面、覆盖层、smoothstep
  `AddCubic`、提交/清理/取消/Busy/IdleMs。
- 验收：DComp 窗口内对任意矩形排队，屏幕上出现 250ms 交叉淡化；
  `Gfx_FxBusy` 在结束后归 0；无泄漏（Commit 后引用计数正确）。

### M1 UI 触发层（纯 C）
- `ui_fx.c`：矩形登记/合并/提交；`UiContext` 增加 `fx` 字段与 DComp 探测。
- 输入事件接入 hover/press；控件点击落到输入事件（Radio/Switch/Checkbox）。
- 验收：hover 关闭键、Radio 选中在 4K 最大化下可见 250ms 渐变；
  动画期间 UI 线程重绘次数 = 0（用 Present 计数/CPU 时间验证）。

### M2 打断与并行
- 重叠烘焙、同事件多矩形、同一时间轴。
- 验收：第二个 Radio 快速切换，全部行从当前呈现值继续；无跳变。

### M3 替换旧状态动画
- DComp 路径关闭 hover/press/focus/glow/select 的 CPU 通道推进；
  `Ui_State` 返回最终权重；保留 CPU 内核仅作 ULW 降级。
- 验收：静止 CPU 0；动画期间 CPU ≈ 0；所有控件外观过渡一致。

### M4 PartFx（位移/旋转）——已完成
- Switch 滑块 Offset 补间、Tab 指示器 offset + clip 宽度补间、chevron 旋转。
- 验收：位移无重影，打断从当前位置继续；换页部件自动隐藏。

### M5 回归与文档
- 全量样例 + 像素采样脚本；4K 最大化/120Hz 验证；白皮书同步。

---

## 9. 为什么不选“逐控件保留可视树”

- 每个可动画控件都要独立 visual + 内容表面 + 状态位图，控件数量 × 状态数
  → 显存与提交次数爆炸；
- 与 immediate mode 的绘制模型冲突：需要把控件绘制拆成可独立渲染的部件、
  维护 z 序/滚动裁剪，等于重写渲染层；
- SnapshotFx 用“矩形级”快照获得同样的合成器插值，不需要改任何控件绘制代码。

## 10. 备选：Windows.UI.Composition

`CompositionColorBrush + ColorKeyFrameAnimation` 可直接在合成器插值**颜色**，
语义最接近 CALayer.backgroundColor；但纯 C 走 WinRT ABI 代码量大、对象生命周期
复杂，且会把渲染模型推向保留型。SnapshotFx 已能满足“颜色 + 透明度 + 状态过渡”，
先按 SnapshotFx 落地；若后续需要大量纯色形状动画，再评估 WUC。

---

## 11. 验收指标（硬）

1. 4K@192 最大化 gallery：hover/选中过渡可见、连续、无台阶；
2. 动画运行期间 UI 线程绘制次数 = 0，CPU 占用回到静止水平；
3. 快速连点 Radio/Switch：每一次点击都从当前呈现值继续，不跳变；
4. 静止 CPU 0；内存稳定（覆盖层/表面池无增长）；
5. ULW 回退路径不崩、行为可预期。

---

## 12. 实施状态（与本仓库对应）

### 已完成

- **M0 gfx 原语**：`gfx_fx.h` + `gfx_d2d.cpp` 内的 SnapshotFx。
  - 旧快照来源是 `uiTarget` 的底层 D3D 纹理（上一帧完整画面），不再维护
    额外整帧副本；新快照取自 swapchain 后缓冲；
  - DComp 表面通过 `BeginDraw(IID_IDXGISurface)` 取 DXGI 表面后做 D3D 拷贝
    （v1 设备未与 D2D 设备关联，传 `ID2D1DeviceContext` 会 `E_INVALIDARG`）；
  - 新快照子 visual 挂 `IDCompositionEffectGroup`，`AddCubic` 精确写入
    smoothstep；合成器求值；
  - 重叠过渡在当前呈现值处烘焙（GPU 合成）后替换，实现“从当前值继续”；
  - 结束回收、`Gfx_FxBusy/IdleMs/Tick/CancelAll`。
- **M1 UI 触发层**：`src/ui/ui_fx.c`（纯 C）。
  - 输入事件登记矩形，`app_render` 绘制前 `Ui_FxFlush` 一次性提交；
  - DComp 路径下 `Ui_State` / 控件选中值直接落最终值，CPU 通道仅作降级；
  - Radio/Checkbox/CheckboxGroup/Switch 改为**输入事件内落状态**（click 回调）。
- **M2 打断**：重叠烘焙 + 同事件多矩形；快速连点验证通过。

### 实测（4K@192 最大化 gallery）

- 关闭键 hover：文字色 124→255 平滑渐变（约 230ms，逐帧中间值）；
- Radio 单击：选中行淡入、原选中行淡出同时进行（约 250ms）；
- 快速下→中→上连点：三行同时有中间值，后一次从当前呈现值继续；
- 过渡期间进程 CPU 增量 = 0ms / 500ms，UI 线程零重绘；
- 回归：md_ui 295 / gfx_rich 34 / layout 4239 / pagination 320 / nav 140 /
  grid 74 / theme_edit 7574，全部通过。

### 已知待办

- **页面切换/主题切换**：样例在切换前显式 `Ui_FxCancelAll`；库层不做隐式取消，
  避免状态栏这类整窗重绘把正在起跑的过渡取消掉（实测踩过）。

### Dialog 保持 CPU 进度通道（已回退）

Dialog 的遮罩与卡片同属一张主表面，卡片内容由应用在 `Begin/End` 之间内联绘制，
无法在不改 API 的前提下放进独立 visual。整窗快照方案（两张 4K 纹理 + 两个表面
约 132MB）在受限显卡上失败后会连带影响后续过渡，实测出现“对话框、按钮都
没动画”。因此 Dialog 保留 CPU 进度通道（打开 180ms、关闭 140ms，遮罩淡变 +
卡片轻微上移，每帧整窗重绘，仅模态期间）。

同时恢复 `app_kick_anim_clock`：空闲 -> 动画时把上一帧时间设为一个正常帧距，
避免首帧 dt 被夹到 100ms 直接把进度推到中后段（对话框“没有动画”的另一半原因）。

### M5 回归（2026-11-19）

- md 525 / md_math 96 / gfx_math 20 / gfx_rich 34 / md_ui 295 /
  layout 4239 / grid 74 / pagination 320 / nav 140 / theme_edit 7574，
  全部 0 失败。
- 白皮书第 9 章已同步为“合成器执行 + CPU 回退”模型，并补第 45 条变更记录。

### 快速交互修正（实测问题）

- **hover 来回移动闪烁**：打断旧过渡时，旧覆盖层原先在 `Gfx_FxQueue` 里立刻销毁，
  而新覆盖层要到本帧 `FxEndFrame` 才建好，中间 1–2 帧露出主表面的旧最终态。
  现在旧层标记 `replaced`，等新覆盖层提交后再拆（新层旧子=烘焙后的当前呈现，
  替换无跳变）。
- **hover/press 时长**：200/120ms → 120/90ms；快速扫过多个按钮时不再拖尾互相
  叠影。选中态仍 250ms。
- **弹层遮挡 part**：下拉/菜单/日期面板/对话框画在主表面，而 chevron/滑块等
  part visual 在其上方。新增 `Ui_OccludeRect`（本帧登记、下一帧生效），
  `Ui_PartPlace*` 命中遮挡时隐藏部件。修复“下拉列表后面透出下方 Select 的
  向下三角”。
- **滚动中的过渡**：`UiScrollView_Tick` 检测偏移变化时 `Ui_FxCancelRect`
  只撤销该视口内的过渡；滚轮也只撤销目标区域。避免移动内容与静态快照错位
  （下拉列表/长列表滚动时的错误显示）。

### 弹层 / 日期面板淡入（SnapshotFx）

打开/换月这类变化发生在绘制期间，等下一次 paint 再抓“旧快照”已经晚了。
做法：变化帧先**不画**新内容（`uiTarget` 保留上一帧像素），同时把矩形登记为
过渡并立刻安排重绘；下一帧正常绘制，覆盖层从旧画面淡到新内容。

- `UiSelect_DrawPopup` / `UiMenu_Draw`：打开首帧只登记面板 blocker + 过渡矩形，
  下一帧面板淡入（160ms）；
- `UiDatePicker_DrawPopup`：面板打开首帧同样跳过；换月时该月块跳过一帧，
  下一帧由覆盖层交叉淡化（140ms）；
- 首帧仍登记面板 blocker，避免这一帧点击穿透；面板内的控件晚一帧可点。

### PartFx 已落地

- `Gfx_PartCreate/Place/PlaceRect/AnimateX/AnimateRect/SetRotation/
  AnimateRotation/Update/Destroy`：内容渲染到独立纹理 + DComp 表面 + visual；
  每帧声明位置刷新 seen，帧末未声明的部件自动隐藏（换页不残留）。
- **Switch 滑块**：轨道留在主表面（颜色走 SnapshotFx），滑块由 part visual
  绘制并滑动；主表面与快照都不包含滑块，避免重影；快速连点从当前呈现 x 反向。
- **Tab 指示器**：内容按最大宽度渲染，用带圆角的 DComp `IDCompositionRectangleClip`
  裁到可见宽度；x 与 clip 宽度同时用 smoothstep 补间，圆角在任意宽度下都正确。
- **Select chevron**：chevron 内容独立渲染，`IDCompositionRotateTransform`
  的 Angle 用 `AddCubic` 补间 0°↔180°；开关状态切换即从当前角度起跑。
- 实测：Switch 单拇指滑动；Tab 指示器滑动+宽度渐变、圆角保持；
  chevron 40ms 处于中间角度、180ms 到位，关闭反向；换页后部件立即隐藏。

### 首帧闪最终状态（已修复）

现象：点击后先看到一帧最终状态，再“倒回”旧状态开始过渡。

原因：`swapchain Present` 的 flip 与 `IDCompositionDevice::Commit` 是两条独立
路径，DWM 可能先合成了新主帧、后应用覆盖层，即使 Commit 在 Present 之前发出。

修复（不依赖两者先后）：

1. 主表面在过渡区域**始终保留旧像素**：Present 绘制完 `uiTarget` 后，把每个
   进行中过渡的旧快照再画回对应矩形（`FxMaskOld`）。覆盖层的旧子 visual 与它
   逐像素一致，所以新主帧什么时候上屏都不会露出来；
2. 动画在绘制前起跑，覆盖层由合成器求值；
3. 动画结束后只标记 `done` 并请求一次重绘：该次绘制不再遮挡过渡区（主表面
   展示新状态），Present 之后再等一个合成帧拆除覆盖层（`FxRemoveDone`）。

实测：4K 最大化下不再出现最终帧闪帧；点击到首个可见变化的延迟约 2–4 帧
（一次整场景绘制 + 一次 vsync + 合成延迟，immediate mode 的固定成本）。

## 13. Windows 配置与异常环境

- 我们的动画是挂在 DComp visual 上的 `IDCompositionAnimation`，由 DWM
  合成器按时间求值，**不受“Windows 动画效果”系统开关控制**（该开关影响的是
  DWM 自身的窗口最小化/最大化等系统过渡）。系统开关关闭时，控件过渡照常。
- 机器性能不足时合成器可能降低帧率，但动画是按时间多项式求值的，总时长不变，
  表现为帧数少、不会“丢失动画”或停在半路。
- 若窗口回退到 ULW（无 DComp），走 `ui_anim` 的 CPU 通道作为降级路径。
- 无障碍“减少动态效果”目前不自动生效；如需遵循可在启动时读
  `SPI_GETCLIENTAREAANIMATION`，关闭时调用 `Ui_AnimSetReduced(1)`。


