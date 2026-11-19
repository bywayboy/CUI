# CUI ColorSelect 设计方案

> 状态：M1–M3 已完成（2026-11-19），gallery 已集成 `PG_COLORSEL` 演示页
> 关联：`src/ui/ui_color_select.*`、`samples/gallery.c`（演示页）
> 决策：下拉 Select 形态；面板 = SV 方形 + 色相条 + 透明度条；
> 组件自持 HSVA；结构体 Init/Draw + UiPopupManager 集成。

## 1. 目标

一个下拉式颜色选择控件：

- 触发器显示当前色块 + `#RRGGBBAA` 文本 + chevron，点击弹出取色面板；
- 面板由 **SV 方形（饱和度×明度）+ 色相条 + 透明度条** 组成，全部可拖拽；
- 组件**自持 h/s/v/a 四个状态**：灰色（S=0）或全黑（V=0）时不丢色相，
  拖动不会出现色相跳变；
- 输出 `UiColor`（`0xAARRGGBB`），同时提供 HSVA 双向读写接口；
- 与 `UiSelect` 相同的弹层语义：PopupManager 打开/关闭、点外面关闭、Esc 关闭。

非目标：hex 文本输入、预设色板、新旧色对比、屏幕吸管、渐变/多色标编辑、
CMYK/HSL 输出、键盘方向键微调（后续可按需补）。

## 2. 状态模型

```c
typedef struct UiColorSelect {
	UiId id;
	float h;    /* 色相 0..360 */
	float s;    /* 饱和度 0..1 */
	float v;    /* 明度 0..1 */
	float a;    /* 透明度 0..1 (alpha) */

	int changed;      /* 本帧颜色变化 (拖动中每帧) */
	int committed;    /* 拖动结束/面板关闭时置 1, 应用据此落库 */

	UiRect anchor;    /* 最近一次触发器矩形 */
	UiRect panelRc;   /* 面板矩形 */
	UiRect svRc;      /* SV 方块 */
	UiRect hueRc;     /* 色相条 */
	UiRect alphaRc;   /* 透明度条 */

	int drag;         /* 0 无; 1 SV; 2 色相; 3 透明度 */
	int panelFx;      /* 打开首帧登记淡入 */
	int wasOpen;
	int enabled;      /* 最近一次 Draw 的可用态 */
	UiColor initial;  /* Init 传入, 供后续"重置"扩展留位 */
} UiColorSelect;
```

规则：

- `h` 始终有效（0..360）。S/V 归零时不覆盖 h；`SetColor()` 遇到灰/黑
  只更新 s/v/a，保留原 h（避免“拖动明度到底再拉回变红”的经典问题）。
- 内部一律 float 0..1（h 为 0..360），对外 `UiColor` 的 alpha 取
  `(int)(a*255+0.5)`。
- 拖动中只改被拖动的分量：SV 改 s/v，色相条改 h，透明条改 a。

## 3. API

```c
/* 纯函数 (可单测): HSV<->RGB, h 0..360, s/v/a 0..1 */
UiColor UiColor_HsvToRgb(float h, float s, float v, float a);
void    UiColor_RgbToHsv(UiColor c, float *h, float *s, float *v, float *a);

void UiColorSelect_Init(UiColorSelect *cs, UiId id, UiColor initial);
void UiColorSelect_Shutdown(UiColorSelect *cs);

/* 双向读写: SetColor 保留灰/黑时的 h */
void     UiColorSelect_SetColor(UiColorSelect *cs, UiColor c);
void     UiColorSelect_SetHSVA(UiColorSelect *cs, float h, float s, float v, float a);
UiColor  UiColorSelect_GetColor(const UiColorSelect *cs);
void     UiColorSelect_GetHSVA(const UiColorSelect *cs, float *h, float *s, float *v, float *a);

/* 帧内: 画触发器; 点击开/关弹层; 返回 1 = 本帧颜色变化 */
int  UiColorSelect_Draw(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UiRect rc, int enabled);

/* 帧末 (overlay): 面板 + SV/色相/透明度条 + 拖拽 */
void UiColorSelect_DrawPopup(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui);

/* 键盘: Esc 关面板; 其余转 PopupManager. 返回 1 = 已消费 */
int  UiColorSelect_Key(UiColorSelect *cs, UiPopupManager *pm,
	UiContext *ui, UINT vk);

int  UiColorSelect_IsOpen(const UiPopupManager *pm, const UiColorSelect *cs);
```

用法（与 Select 一致）：

```c
UiColorSelect_Init(&color, Ui_Id("ui.color", 0, 0), 0xFF3B82F6u);
/* 帧内 */    UiColorSelect_Draw(&color, &pm, &ui, rc, 1);
/* 帧末 */    UiColorSelect_DrawPopup(&color, &pm, &ui);
/* 键盘 */    UiColorSelect_Key(&color, &pm, &ui, vk);
if (color.committed) { save(color 的 GetColor()); color.committed = 0; }
```

## 4. 视觉规格

触发器（高度沿用 34 DIP，宽度自适应/由调用方给）：

- 左侧 22×22 色块：棋盘格底 + 当前色，圆角 6，1px 描边 `neutral.border`；
- 文本 `#RRGGBBAA`（fBody，text），透明度为 255 时仍显示 8 位；
- 右侧 MDL2 chevron `0xE70D`，展开时旋转 180°（沿用 `Gfx_PartAnimateRotation`
  或 CPU 角度补间）；
- hover/press 走 `Ui_State`，与普通 Select 触发器一致。

面板（DIP）：

| 部件 | 尺寸 | 绘制 |
|:--|:--|:--|
| 面板 | 宽 240，内边距 12，圆角 10 | `toastBg`/`toastBorder` + 面板淡入 |
| SV 方块 | 216 × 136，圆角 8 | 横：白→纯色相 `GradH`；竖叠：透明→黑 `GradV` |
| 色相条 | 216 × 14，圆角 7 | 7 档线性渐变 (红/黄/绿/青/蓝/品/红) |
| 透明度条 | 216 × 14，圆角 7 | 棋盘格 + 透明→不透明 `GradH` |
| 间距 | 12 / 10 / 8 / 12 | |
| 面板总高 | 206 | 12+136+10+14+8+14+12 |

指示标：

- SV：r=6 圆点，白描边 2 + 外侧 1px 黑（在浅/深底都可见）；
- 色相/透明度：6×18 竖向胶囊，白描边 + 1px 黑描边；
- 色相条拖满一圈时 h 取 0.999×360，避免与 0° 标记重叠。

棋盘格：8×8 DIP，固定 `0xFFE6E9EF` / `0xFFFFFFFF` 两色（与主题明暗无关，
保证"透明"的通用语义；深色主题下仍可辨认）。

## 5. 交互

- 触发器点击：`UiPopupManager_Open(pm, ui, id, id, focusOnClose)`；
  已打开则关闭。面板首帧登记 `panelFx` 淡入（与 Select 相同写法）。
- 打开后指针在面板内按下：
  - SV：`s = clamp((x-svRc.x)/w)`, `v = clamp(1-(y-svRc.y)/h)`；
  - 色相：`h = clamp(x/w)*360`；
  - 透明度：`a = clamp(x/w)`。
- 按下即生效并进入 `drag=kind`；拖动允许移出条外（坐标夹紧），
  直到 `ui->mouseDown == 0` 结束并置 `committed=1`。
- 每帧仅在值变化（>1e-4）时 `changed=1` 并 `Ui_InvalidateRectPad` 面板；
  拖动不整窗重绘。
- Esc / 点击外部：PopupManager 关闭；关闭当帧 `committed=1`（若曾变化）。
- 多实例：弹层 id = 组件 id；子命中 id 由 `id + 内部 tag` 派生，
  与 Select 的 `sel_child` 同法，实例间互不干扰。
- disabled：触发器灰化、不可展开；面板若已打开则强制关闭。

## 6. 绘制与性能

- 面板在帧末 overlay 绘制（Select 模式），无额外 DComp layer：
  拖拽期本来每帧都在变，合成器层的"只画一次"收益不成立。
- SV 两个渐变、色相条 7 档渐变用现有 `Gfx_FillRoundRectGradH/V` 与
  `Gfx_LinearGradientBrush`；透明度条棋盘格在帧内画（约 27×2 个小方块，
  仅面板区域受影响）。
- 稳定不交互时无持续重绘；面板淡入由 SnapshotFx 覆盖层完成。
- HSV 转换是纯整数/浮点运算，不引入数学库依赖（0CRT）。

## 7. 边界与错误

- Init 传 `a=0` 合法（透明色）；`GetColor` 原样保留 alpha。
- `SetHSVA` 对 h 取模到 [0,360)，s/v/a 夹到 [0,1]。
- `RgbToHsv`：max==min 时 s=0 且 h 返回 0（调用方 SetColor 会保留旧 h）。
- 面板尺寸小于最小宽（240）时按可用宽度收缩，SV 高度按 0.63 比例缩放，
  色相/透明条高度不变。
- 触发器宽度过小时只画色块 + chevron，省略 hex 文本（ELLIPSIS 兜底）。

## 8. 里程碑

- **M1 核心与面板**：`ui_color_select.c/h`，HSVA 状态、触发器、弹层
  （PopupManager）、SV/色相/透明度绘制与拖拽、changed/committed。
- **M2 交互收尾**：chevron 旋转、面板淡入、disabled/多实例/Esc/点外关闭、
  DPI 缩放、宽度退化、纯函数单测（HSV 往返、灰/黑保留色相）。
- **M3 集成与收尾**：gallery `PG_COLORSEL` 演示页（触发器 + 实时色块 +
  hex/HSVA 只读回显 + 两三个实例）、四段文档（文档/API/事件/最佳实践）、
  全量回归、提交。

进度：M1–M3 已完成（2026-11-19）。已验证：面板开合与淡入、SV/色相/透明度
三处拖拽、松手一次 committed、Esc/点外关闭、双实例互不影响、触发器 hex
回显与窄宽度省略、合成器 chevron 旋转；`ui_color_test` 161 项（HSV 已知值/
往返/灰黑保留色相/夹取）全通过。

## 9. 验收

1. `./build.cmd` 通过；非使用者二进制体积不变（`/Gy`+`/OPT:REF`）。
2. 拖动 SV/色相/透明度即时改变；松手只产生一次 `committed`。
3. S 拉到 0 再拉回、V 拉到 0 再拉回：色相不丢、无红色跳变。
4. 面板拖动/关闭只失效面板矩形，稳定期主窗帧计数不增长。
5. Esc/点外部关闭；关闭即 `committed`（有变化时）。
6. 同时开两个实例互不影响；disabled 不能展开。
7. 100%/125%/150%/200% 缩放下指示标与渐变对齐；窄触发器省略 hex。
8. 1000 次 HSV→RGB→HSV 往返误差 < 1/255（单测断言）。

## 10. 风险

- **渐变组合**：SV 用两个渐变叠加模拟（白→色相 + 透明→黑），与真 HSV
  数学等价（v 线性、s 线性），无 D2D 颜色空间问题。
- **透明度条的棋盘格与圆角**：圆角裁剪不可用，棋盘格按矩形绘制、外层用
  圆角描边覆盖；四角可能有 1–2px 露底，视觉可接受，后续可改用离屏绘制。
- **弹层与 Toast/Select 互斥**：同一 PopupManager 会话，同一时间只开一个，
  与现有 Select/Menu 行为一致。
