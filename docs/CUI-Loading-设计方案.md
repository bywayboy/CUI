# CUI Loading 设计方案

> 状态：M1–M4 已完成（2026-11-19），gallery 已集成 `PG_LOADING` 演示页
> 关联：`src/ui/ui_loading.*`、`src/gfx/gfx.h`（3 个新 API）、`samples/gallery.c`
> 前置结论：动画 = 圆环弧 270°；默认全拦 + `passThrough`；结构体 Init/Draw；
> 取消 = 圆形 X + Esc。

## 1. 目标

覆盖任意父容器（面板、卡片、列表区）的加载遮罩：

- 半透明 overlay，裁剪到父容器矩形，跟随父容器位置/尺寸；
- GPU 旋转的圆环弧 270° spinner（合成器旋转，UI 线程零逐帧重绘）；
- 一行可配置文字（默认 `载入中...`），超宽省略号；
- `cancelable` 打开时显示圆形 X 按钮，Esc 也发起取消；
- 默认拦截父容器内全部鼠标/滚轮输入；`passThrough` 可关闭拦截；
- 取消只产生“请求”，显示/隐藏始终由程序控制。

非目标：确定性进度模式、多行文字、遮罩内队列提示、父容器内多个 Loading、
取消确认框、模糊背景（后续可接 D2D 效果）。

## 2. 状态机与时间

```
HIDDEN ──Show──> PENDING ──delayMs 到──> SHOWN ──Hide()+minShow 到──> HIDING ──fade 完──> HIDDEN
                    │                     ↑  │
                    └──Hide()（从未显示）──┘  └──── Show()（反悔）──────┘
```

- `PENDING`：`Show()` 后等 `delayMs`（默认 150ms）。期间不画、不拦输入；
  短任务在 delay 内 `Hide()` 则从未出现过，杜绝闪烁。
- `SHOWN`：`started=1`，从此刻起计时 `minShowMs`（默认 400ms）。
  `Hide()` 只登记 `hidePending`，到 `startedAt + minShowMs` 才进入 `HIDING`，
  避免“闪一下就没”。
- `HIDING`：淡出 `UI_LOADING_FADE_MS`（160ms），结束后在 `EndFrame` 销毁
  layer/part。淡出期间继续拦截输入，防止点击穿透。
- `HIDING` 中 `Show()`：撤销 `hidePending`，直接回 `SHOWN`，layer 淡回 1。
- 状态推进在 `UiLoading_Tick`（每拍）与 `UiLoading_EndFrame`（`Gfx_End` 后）完成，
  两者都以 `UiFrame_Now()` 的单调毫秒为准。

## 3. API

```c
#define UI_LOADING_FADE_MS 160

typedef enum {
	UI_LOADING_HIDDEN = 0,
	UI_LOADING_PENDING,
	UI_LOADING_SHOWN,
	UI_LOADING_HIDING
} UiLoadingPhase;

typedef struct UiLoading {
	UiContext *ui;
	int slot;                 /* 实例序号: 生成 blocker/cancel 的 UiId */
	char text[96];            /* UTF-8, 单行 */
	int cancelable;           /* 1 = 显示圆形 X, Esc 可取消 */
	int passThrough;          /* 1 = 不拦截输入 */
	int delayMs, minShowMs;

	UiLoadingPhase phase;
	double showAt, shownAt, hideAt;
	int hidePending;
	int cancelReq;            /* 已发起取消请求, 由 TakeCancel 消费 */

	UiRect rc;                /* 最近一次父容器矩形 (DIP) */
	float radius;             /* 遮罩圆角, 0 = 直角 */

	float fallbackDeg;        /* 主窗回退时的 spinner 角度 */
	GfxLayer *layer;          /* 合成器遮罩层 */
	GfxPart *spin;            /* layer 子 visual, DComp 旋转 */
	int layerW, layerH, layerX, layerY;
	int layerDirty, layerIn, layerOut, fallback;
	uint32_t themeRev;
} UiLoading;

void UiLoading_Init(UiLoading *ld, UiContext *ui);
void UiLoading_Shutdown(UiLoading *ld);

void UiLoading_SetText(UiLoading *ld, const char *utf8);
void UiLoading_SetCancelable(UiLoading *ld, int on);
void UiLoading_SetPassThrough(UiLoading *ld, int on);
void UiLoading_SetTiming(UiLoading *ld, int delayMs, int minShowMs);
void UiLoading_SetRadius(UiLoading *ld, float radiusDip);

void UiLoading_Show(UiLoading *ld);
void UiLoading_Hide(UiLoading *ld);
int  UiLoading_Visible(const UiLoading *ld);   /* PENDING/SHOWN/HIDING */
int  UiLoading_Shown(const UiLoading *ld);     /* SHOWN/HIDING, 可据此门控按键 */
int  UiLoading_UsingLayer(const UiLoading *ld);/* 0 = 主窗回退路径 */

/* 取消请求是一次性事件: 返回 1 并清除. 组件绝不自行隐藏. */
int  UiLoading_TakeCancel(UiLoading *ld);
/* 按键入口: Esc 且 cancelable 时登记请求并返回 1 (已消费). */
int  UiLoading_Key(UiLoading *ld, int vk);

/* 每拍 (WM_TIMER/WM_PAINT 之前) 调用; 1 = 本拍需要主窗重绘. */
int  UiLoading_Tick(UiLoading *ld, double nowMs);
/* 帧内、页面内容之后调用: 登记 blocker/取消按钮命中区并返回本帧父容器矩形. */
void UiLoading_Draw(UiLoading *ld, UiRect parentRc, double nowMs);
/* Gfx_End 之后调用: layer/part 的创建、定位、重画与淡入淡出. */
void UiLoading_EndFrame(UiLoading *ld, double nowMs);
```

用法（gallery 集成顺序见 §10）：`Init` → 任务开始时 `Show`；
`Tick` 返回 1 时 `UiApp_Invalidate`；每帧内容之后 `Draw`；
`on_after_frame` 里 `EndFrame`；拿到 `TakeCancel` 后停任务再 `Hide`。

`UiLoading_Shown` 为 1 时，应用应把键盘事件先交给 `UiLoading_Key`，
并在其返回 0 时不把按键转发给页面控件（`passThrough=1` 除外）。
IME：组件不改变焦点；若聚焦编辑框在父容器内，应用在 `Show` 前按现有
“外部点击”路径失焦（`UiInput_MouseDown(in, UI_ID_NONE, 0)`）。

## 4. 绘制架构

### 4.1 合成器路径（默认）

完全复用 Toast 的已验证模型（`ui_toast.c` 的 `toast_sync_layer`）：

- 一个 `GfxLayer`，像素尺寸 = 父容器 DIP 矩形 × scale，位置 = 父容器原点。
  layer 内容只画一次：半透明遮罩 + 文字 + 取消按钮（按需重画）；
- spinner 是 **layer 的子 GfxPart**：自身 bitmap 只画一次 270° 弧，
  由 `IDCompositionRotateTransform` 连续旋转，与 layer 的 opacity 一起淡入淡出；
- 主窗在 SHOWN 稳定期**不做任何重绘**；layer 的淡入淡出由 DWM 合成器补间；
- 主题切换 / 文字变更 / 取消按钮 hover 过渡 → 仅重画 layer，不动主窗；
- 父容器移动/改尺寸 → `Gfx_LayerResize` + `Gfx_LayerSetPos` + 重排内容。

层级：

```
Toast layer（最高，独立 visual）
Loading layer + spinner 子 visual
主窗表面（页面、弹层、对话框）
```

Loading 高于主窗所有内容：**弹层/对话框不能盖在 Loading 之上**。
需要在其上弹窗时，应用先 `Hide`（文档明示，不做自动处理）。

### 4.2 主窗回退路径（ULW / layer 创建失败 / `SCV2_ULW=1`）

- 全部内容画在主窗，父容器矩形参与脏区裁剪；
- spinner 角度由 `UiFrame_Now()` 计算（`fallbackDeg`），随 `Tick` 每 16ms
  失效父容器区域，直至隐藏——这是唯一允许的 CPU 逐帧路径；
- 淡入淡出用 `Gfx_PushAlpha`（同 Toast 回退）；
- `UiLoading_UsingLayer` 供应用判断，与 `UiToast_UsingLayer` 用法一致。

## 5. gfx 增量

```c
/* 静态定位 layer (DIP); 内容绘制后、Gfx_End 之后可调用 */
void Gfx_LayerSetPos(GfxLayer *l, float xDip, float yDip);

/* 在 layer 的 visual 下创建一个部件 (子 visual):
 * 继承 layer 的 opacity/位置; 内容画一次, 位置/旋转交给合成器. */
GfxPart *Gfx_PartCreateOn(GfxLayer *l, float dipW, float dipH,
	GfxPartDrawFn fn, void *user);

/* 连续匀速旋转 (度/秒). 幂等: 同转速重复调用是空操作;
 * 内部用线性 IDCompositionAnimation, 从当前相位续接, 每 10 分钟重锚一次
 * 以限制角度浮点误差; Gfx_PartRotation 返回 CPU 模型相位. */
void Gfx_PartSpin(GfxPart *p, float degPerSec);
void Gfx_PartSpinStop(GfxPart *p);
```

实现要点：

- `Gfx_LayerSetPos`：`visual->SetOffsetX/Y(px)`，`Commit`；仅静态定位，
  不做补间（Loading 出现在稳定布局上）。
- `Gfx_PartCreateOn`：现有 `Gfx_PartCreate` 的视觉父节点改为
  `l->visual`（`AddVisual(v, FALSE)`），`GfxPart` 记录 `parent`；
  `Gfx_PartDestroy` 从 parent 移除。旋转中心沿用内容中心。
- `Gfx_PartSpin`：`CreateAnimation` + `AddLinear(0, phaseDeg, speed)`，
  `End(T, phaseDeg + speed*T)`，`T = 600s`（10 分钟，误差 < 0.25°）；
  UiLoading 每帧调用一次，过期即重锚；`SetAngle(anim)` 一次提交。
  ULW 无 DComp，返回失败由调用方走回退。

## 6. 视觉规格（DIP）

| 元素 | 规格 |
| --- | --- |
| 遮罩 | `Ui_ColorAlpha(theme->page, 170)`，圆角 = 父容器 `radius` |
| spinner | 直径 44，线宽 4，270° 弧；底色环 `primary.base` α36 |
| 弧尾 | 24 段子弧，头 255 → 尾 40 线性衰减，圆头线帽 |
| 转速 | 360°/1.1s（≈327°/s），线性匀速 |
| 文字 | `fBody`，`theme->text`，单行居中，`GFX_TEXT_CENTER|VCENTER|ELLIPSIS` |
| 间距 | spinner ↓14 文字；可取消时文字 ↓16 按钮 |
| 取消按钮 | 圆形 30，1px 描边；X 图标 14（`fIcon` 无则两根线画） |
| 按钮状态 | `Ui_State` 标准通道（hover 90/ press 55）；hover=primary 软底+primary 边，press 更深 |

整块内容（spinner+文字+按钮）在父容器矩形内水平/垂直居中；
文字可用宽 = 父容器宽 − 48；父容器过小时按可用宽截断（ellipsis），
按钮仍完整显示。

## 7. 输入拦截

默认（`passThrough=0`）在 `UiLoading_Draw` 里：

1. `Ui_PushLayer(ui, UI_Z_OVERLAY)`；
2. 登记不可见 block 控件 `Ui_Id("loading.blocker", slot, 0)`，矩形 = 父容器；
   后画的取消按钮同层登记，因此可命中；
3. `Ui_RegisterWheelTarget` 登记同 id/矩形的吞轮目标（最后登记者优先，
   滚动容器不会收到滚轮）；
4. `Ui_PopLayer`。

进入 `SHOWN` 的第一帧：若指针在父容器内且未按住，清理
`ctx->hover/pressed` 并对旧 hover 控件失效一次（不触碰 `drag/mouseDown`）。
`passThrough=1` 时跳过 2–4，只画不拦。

取消按钮点击 = 登记 `cancelReq`；Esc 由 `UiLoading_Key` 登记。
两者都**不**改变 phase；`cancelReq` 保持锁存到 `TakeCancel` 消费或
下一次 `Show` 清除（`Hide` 不清，防止误吞）。

## 8. layer/part 生命周期（EndFrame）

```
EndFrame:
  1. phase==HIDDEN → 若 layer 存在: Destroy layer/part; return
  2. phase==PENDING → return
  3. 计算 px 尺寸; 无 layer → Gfx_CreateLayer(父容器 px)
     失败 → fallback=1 (下一拍走主窗绘制)
     layer 尺寸/位置/主题/文字/按钮状态变化 → Resize/SetPos + 重画内容
  4. 无 spin → Gfx_PartCreateOn(layer, spinnerRect, draw_spinner, ld)
     Gfx_PartPlace + Gfx_PartSpin(327°/s)
  5. SHOWN: opacity→1 (animateMs=FADE, 首次与淡出返场)
     HIDING: opacity→0; 过期后 HIDDEN → 下一拍销毁
  6. Gfx_LayerAlive 失败 → fallback
```

`UiLoading_EndFrame` 必须在 `Gfx_End` 之后调用（`Gfx_CreateLayer`/
`Gfx_LayerDraw` 的既有约束）。销毁 layer 后 `fallback` 置 0 复位状态。

## 9. 主题 / DPI / 异常

- `themeRev` 变化：重画 layer 内容 + `Gfx_PartUpdate(spin)`；
- DPI/scale 变化：layer 尺寸按新 scale 重建，spinner DIP 尺寸不变；
- 设备恢复 `Gfx_LayerAlive==0`：销毁 layer/part，`fallback=1` 继续工作；
- `UiLoading_Shutdown`：销毁 layer/part，无残留输入状态（blocker 是
  每帧登记，不写入全局 modal）。

## 10. 集成顺序（gallery 演示页）

```c
/* on_tick */
if (UiLoading_Tick(&g->ld, UiFrame_Now())) UiApp_Invalidate(app);

/* on_key 最前 */
if (UiLoading_Key(&g->ld, vk)) return 1;

/* on_render: 页面内容之后, Toast 之前 */
UiLoading_Draw(&g->ld, panelRc, UiFrame_Now());

/* on_after_frame (Gfx_End 之后) */
UiLoading_EndFrame(&g->ld, UiFrame_Now());
```

演示页 `PG_LOADING`：

- 面板 + 三个按钮：`开始载入 (2s)`、`开始载入 (可取消)`、`模拟失败`；
- “可取消”模式：`cancelable=1`，`TakeCancel` 后 600ms 结束任务并 `Hide`，
  状态行显示“已取消”；
- 勾选框：`passThrough`（面板内可点一个示范按钮证明穿透）、`delay/minShow`
  两种组合（快任务不闪 / 慢任务至少 400ms）；
- 页面内计数面板显示主窗帧计数，用于验证合成器模式稳定期不重绘。

## 11. 里程碑

- **M1 gfx 增量**：`Gfx_LayerSetPos` / `Gfx_PartCreateOn` / `Gfx_PartSpin(Stop)`；
  实现 + 目视烟测（旋转、淡入淡出、layer 定位）。
- **M2 UiLoading 核心**：状态机 + 主窗回退绘制（spinner CPU 角度、文字、
  取消按钮）+ 输入拦截/滚轮吞掉 + `Key/TakeCancel` + `SCV2_ULW=1` 验收。
- **M3 合成器路径**：layer + 子 part 旋转 + DWM 淡入淡出 + 主题/尺寸/设备
  恢复 + fallback 切换。
- **M4 集成与收尾**：gallery Loading 演示页（三种场景 + 两个选项）、
  文档校对、全量回归、提交。

进度：M1–M4 已完成（2026-11-19）。已验证：稳定期主窗帧计数不增长、
spinner 合成器旋转、遮罩内点击拦截、passThrough 反证、X/Esc 取消请求、
窗口改尺寸后 layer/命中区跟随、`SCV2_ULW=1` 回退逐帧绘制；
gallery `PG_LOADING` 演示（三场景 / 两勾选 / 属性代码面板）与六页文档已就绪。

## 12. 验收

1. `./build.cmd` 通过；`cui_gallery` 无新警告。
2. 合成器模式：SHOWN 稳定后状态栏帧计数**不再增长**，前后两张截图 spinner
   角度不同（顺时针）、遮罩与文字不动。
3. 快任务（<delay）不出现闪烁；慢任务可见时长 ≥ minShow。
4. 默认拦截：面板内点击/滚轮全部无效，面板外控件正常；`passThrough` 反证。
5. 取消：X hover/press 有过渡；点击或 Esc 只触发一次请求，应用 `Hide` 前
   Loading 不消失；`cancelable=0` 时 Esc 不产生请求。
6. 父容器移动/改尺寸、主题切换、`SCV2_ULW=1` 回退均正确。
7. 连续显示 30 分钟无角度抖动（spin 重锚）与资源泄漏（layer 销毁后
   `Gfx_LayerAlive` 无残留 swapchain 日志）。

## 13. 风险与约束

- **层级**：Loading 高于主窗弹层/对话框（合成器层属性），需要时应用先隐藏；
  Toast 正确盖在 Loading 之上。
- **一个父容器一个实例**：`slot` 只用于区分多个实例的命中 id，不处理嵌套
  Loading 的空间关系。
- **spinner 不被祖先滚动裁剪**：part 位于 layer 内，不受主窗 clip 影响；
  父容器若在滚动视口内，Loading 应随容器整体滚动（传当前 DIP 矩形即可）。
- **旋转精度**：线性动画 10 分钟重锚，误差 < 0.25°，肉眼不可见。
- **回退路径逐帧重绘**：仅 ULW 场景，脏区限制在父容器矩形内。
