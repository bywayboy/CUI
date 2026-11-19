# CUI SVG 编辑器组件设计方案

> 状态：设计定案（2026-11-19），待实现
> 关联：`src/gfx/gfx.*`（新增路径/描边样式/视口变换/渐变画刷）、
> `src/svg/svg_doc.*`（新增，文档模型与序列化）、
> `src/ui/ui_svg_edit.*`（新增，交互组件）、`samples/svgedit.c`（新增）、
> `tests/svg_test.c`（新增）

## 1. 目标

一个**应用持有**的 SVG 编辑器组件：文档模型独立于窗口，交互层以即时模式
挂到任意 `UiApp`；自带独立示例 `cui_svgedit.exe`。

一期能力：

| 能力 | 范围 |
| --- | --- |
| 视区 | 等比缩放 5%–3200%（滚轮以光标为中心、工具栏/快捷键），平移，适应窗口 |
| 绘制 | 直线、矩形、椭圆、任意多边形（3–64 边）、折线/贝塞尔钢笔、圆弧（切向弧/三点弧） |
| 编辑 | 整体选择/移动/缩放/旋转；路径锚点与贝塞尔手柄编辑、增删节点 |
| 样式 | 填充/描边：无、纯色、线性渐变；宽度、虚线、线帽、连接、不透明度 |
| 历史 | Undo/Redo，覆盖除视区外的全部编辑 |
| 输出 | 导出/复制 SVG 1.1 文本 |

非目标（一期）：

- 打开/解析已有 SVG 文件（二期）；
- SVG `text`/`image`/`use`/滤镜/裁剪路径；
- 图案填充、径向渐变、渐变编辑超过两色标（模型按 N 色标设计，UI 先给两色标）；
- 多文档标签、图层分组（`<g>`）、动画（SMIL）；
- 吸附参考线、对齐分布、布尔运算。

关键决策：

1. **元素几何存局部坐标 + 每元素旋转**（中心 + 角度）；移动/缩放直接改几何，
   旋转进 `transform="rotate(a cx cy)"`。命中、导出、数值编辑都可逆且可单测。
2. **Undo 用快照**而非命令+逆操作：覆盖全面、无遗漏风险，单步 KB 级。
3. 视口变换在 gfx 层通过变换栈实现，描边/文本随缩放保持矢量清晰，
   线宽按世界单位缩放（SVG 语义）。
4. 画布不跑持续 CPU 动画；悬停/按压走现成合成器 FX，不为画布新造动画通道。

## 2. 分层架构

```
samples/svgedit.c          外壳: 工具栏/属性面板/状态栏/文件对话框
        │
src/ui/ui_svg_edit.c       交互层: 视口、工具状态机、命中、手柄、吸附
        │  只用 CUI 公共组件: UiButton/UiPopup/UiScrollView/UiTooltip/UiMenu
src/svg/svg_doc.c          文档模型: 元素/路径/样式/渐变/历史/序列化 (纯 C)
        │
src/gfx/gfx.h              新增: 路径构建与填充/描边、描边样式、视口变换、渐变画刷
```

- `svg_doc` 不依赖 `UiContext`/`Gfx`，可用纯单测覆盖（bbox、命中、序列化、
  历史一致性、矩阵互逆）；
- `ui_svg_edit` 只持有 `SvgDoc` 指针与工具/视口状态，便于嵌入其他应用；
- 示例程序负责全部面板与文件 IO，组件本身不弹系统对话框。

## 3. gfx 新增能力

### 3.1 路径

```c
typedef struct GfxPath GfxPath;

GfxPath *Gfx_PathCreate(Gfx *g);
void  Gfx_PathReset(GfxPath *p);
void  Gfx_PathDestroy(Gfx *g, GfxPath *p);
void  Gfx_PathMoveTo(GfxPath *p, float x, float y);
void  Gfx_PathLineTo(GfxPath *p, float x, float y);
void  Gfx_PathCubicTo(GfxPath *p, float x1, float y1, float x2, float y2,
                      float x, float y);
void  Gfx_PathQuadTo(GfxPath *p, float x1, float y1, float x, float y);
/* 椭圆弧: 语义同 SVG A (端点参数化); 后端映射 D2D1_ARC_SEGMENT */
void  Gfx_PathArcTo(GfxPath *p, float rx, float ry, float rotDeg,
                    int largeArc, int sweep, float x, float y);
void  Gfx_PathClose(GfxPath *p);

GfxRect Gfx_PathBounds(Gfx *g, GfxPath *p);   /* 曲线精确包围盒 */

void Gfx_FillPath(Gfx *g, GfxPath *p, GfxColor c);
void Gfx_FillPathBrush(Gfx *g, GfxPath *p, GfxBrush *brush);
void Gfx_StrokePath(Gfx *g, GfxPath *p, GfxBrush *brush, float width,
                    const GfxStrokeStyle *st);
```

后端：`ID2D1PathGeometry`（sink 直接来自上述命令，弧段走
`ID2D1GeometrySink::AddArc`）+ `ID2D1StrokeStyle`。
`GfxPath` 创建后归调用方所有，编辑器按"元素 id + version"缓存，
只在几何变化时重建；`Gfx_FillPath` 的纯色路径复用内部 brush。

### 3.2 描边样式

```c
enum { GFX_CAP_BUTT = 0, GFX_CAP_ROUND, GFX_CAP_SQUARE };
enum { GFX_JOIN_MITER = 0, GFX_JOIN_ROUND, GFX_JOIN_BEVEL };

typedef struct GfxStrokeStyle {
    int cap, join;
    float miterLimit;           /* 默认 4 */
    const float *dashes;        /* DIP, 世界单位; NULL = 实线 */
    int dashCount;
} GfxStrokeStyle;
```

虚线预设（示例/属性面板）：实线、点线 `[1,3]`、虚线 `[6,4]`、点划线 `[8,3,1,3]`。
gfx 内部按 (cap/join/miter/dash 序列/DPI) 键缓存 `ID2D1StrokeStyle`。

### 3.3 视口变换

现有只有 `Gfx_PushRotate`。扩展为通用变换栈（最多 8 层）：

```c
/* 等比缩放 + 平移: 世界坐标 → DIP。与 Gfx_PushRotate 共用 Pop。 */
void Gfx_PushScaleTranslate(Gfx *g, float scale, float tx, float ty);
void Gfx_PopTransform(Gfx *g);          /* 已有, 语义不变 */
```

用法固定为：`Gfx_PushClip(viewportDIP)` → `Gfx_PushScaleTranslate(视口)` →
逐元素 `Gfx_PushRotate(元素中心, 角度)` → 绘制 → `Gfx_PopTransform` → … →
`Gfx_PopTransform` → `Gfx_PopClip`。

裁剪在屏幕空间先压栈，再进世界变换；手柄/节点等装饰在屏幕空间绘制，
不随缩放变形。

### 3.4 线性渐变画刷

```c
typedef struct { float offset; GfxColor color; } GfxGradStop;

GfxBrush *Gfx_LinearGradientBrush(Gfx *g, GfxRect boxLocal,
    float angleDeg, const GfxGradStop *stops, int count);
void  Gfx_BrushDestroy(Gfx *g, GfxBrush *b);
```

- 按元素局部包围盒 `boxLocal` + 角度 + 色标哈希缓存；几何/样式变更才重建；
- 语义对齐 SVG `gradientUnits="objectBoundingBox"`：画刷在元素局部空间创建，
  随元素旋转/视口缩放一起变换，导出时按包围盒算出 `x1/y1/x2/y2`；
- 纯色也可封装为 `GfxBrush`（`Gfx_SolidBrush`），绘制 API 统一收 brush 参数。

## 4. 文档模型

```c
/* src/svg/svg_doc.h */
typedef enum { SVG_EL_RECT, SVG_EL_ELLIPSE, SVG_EL_LINE,
               SVG_EL_POLYGON, SVG_EL_PATH } SvgElType;

typedef enum { SVG_NODE_CORNER, SVG_NODE_SMOOTH } SvgNodeType;

/* 节点: 锚点 + 三次贝塞尔进出控制点 (相对锚点的偏移, 局部坐标) */
typedef struct {
    float x, y;
    float inX, inY, outX, outY;
    uint8_t type, hasIn, hasOut;
} SvgNode;

/* 段类型: nodes[i] -> nodes[i+1]; 闭合时 seg[count-1] 是回到 nodes[0] 的段 */
typedef enum { SVG_SEG_LINE, SVG_SEG_CUBIC, SVG_SEG_ARC } SvgSegType;

/* SVG A 段的端点参数化: 端点由目标节点给出, 其余参数在这里 */
typedef struct {
    float rx, ry, rotDeg;
    uint8_t large, sweep;
} SvgArc;

typedef struct {
    SvgNode    *nodes;  int count, cap;
    SvgSegType *seg;    /* 开放 count-1 项, 闭合 count 项 */
    SvgArc     *arc;    /* 与 seg 同长, 仅 seg==SVG_SEG_ARC 有效 */
    uint8_t closed;
} SvgPath;

typedef enum { SVG_PAINT_NONE, SVG_PAINT_SOLID, SVG_PAINT_LINEAR } SvgPaintKind;

typedef struct {
    SvgPaintKind kind;
    GfxColor color;                 /* SOLID */
    float angleDeg;                 /* LINEAR: 世界角度 */
    struct { float offset; GfxColor color; } *stops;  /* LINEAR, N 色标 */
    int stopCount;
} SvgPaint;

typedef struct {
    SvgPaint fill, stroke;
    float strokeWidth;              /* 世界单位, 默认 2 */
    float dash[8]; int dashCount;   /* 空 = 实线 */
    int cap, join;
    float opacity;                  /* 0..1 */
} SvgStyle;

typedef struct {
    SvgElType type;
    char name[48];
    /* 参数几何 (世界坐标, 未旋转前) */
    union {
        struct { float x, y, w, h; } rect;
        struct { float cx, cy, rx, ry; } ellipse;
        struct { float x1, y1, x2, y2; } line;
    } p;
    SvgPath path;                   /* PATH / POLYGON; 其余类型同步生成用于命中/导出 */
    SvgStyle style;
    float rot;                      /* 度, 正 = 屏幕顺时针 */
    uint8_t visible, locked;
    int version;                    /* 几何/样式变更 +1, 几何/画刷缓存键 */
} SvgElement;

typedef struct {
    float width, height;            /* artboard = SVG viewBox */
    GfxColor background;            /* 导出时可写 rect, 0 = 透明 */
    SvgElement *els;  int count, cap;
} SvgDoc;
```

- 矩形/椭圆/直线**参数化存储**，属性面板可直接改数值；命中与导出时按需
  展开为 path（或专用 D2D 图元，性能更好）。
- 多边形与钢笔路径都是 `SvgPath`：段类型逐段声明——直线 `SVG_SEG_LINE`、
  三次贝塞尔 `SVG_SEG_CUBIC`（用两端节点的 in/out 控制点）、椭圆弧
  `SVG_SEG_ARC`（SVG `A` 端点参数化）。同一路径允许 L/C/A 混排，
  闭合路径 `closed=1`。
- 圆弧保持原生 `A` 语义：三点弧由"起点/弧上一点/终点"求圆，写成
  `rx=ry=r, rot=0` + `large/sweep`；切向弧由"起点切向 + 终点"求圆。
  属性面板可直接改 `rx/ry/rot/large/sweep`（把圆改成椭圆弧）。
- 旋转绕元素局部包围盒中心；多选旋转时每个元素角度同时增加，并把各自中心
  绕选择框中心旋转，保持相对布局。

### 4.1 文档 API（摘要）

```c
SvgDoc  *SvgDoc_Create(float w, float h);
void     SvgDoc_Destroy(SvgDoc *d);
SvgElement *SvgDoc_Add(SvgDoc *d, SvgElType type);      /* 置顶, 默认样式 */
void     SvgDoc_Remove(SvgDoc *d, int index);
void     SvgDoc_MoveZ(SvgDoc *d, int index, int dir);   /* 上/下移一层 */
GfxRect  Svg_ElementBounds(const SvgElement *e);        /* 含旋转后的包围盒 */
int      Svg_ElementHit(const SvgElement *e, float x, float y, float tol);
int      Svg_DocHit(const SvgDoc *d, float x, float y, float tol); /* 最上层 */
```

命中与包围盒：`Svg_PathFlatten()` 把 L/C/A 自适应展平成折线（世界容差
0.1，圆弧/贝塞尔同一套采样），填充按折线内部判定，描边按折线距离
`<= strokeWidth/2 + tol`，包围盒取展平点 min/max。`tol` 由屏幕像素
换算（`tol = 4 / viewScale`）。全部在逆旋转后的局部坐标中计算；
文档层不依赖 D2D，可在单测里覆盖。

### 4.2 历史（快照式）

```c
typedef struct SvgHistory SvgHistory;   /* 内嵌于 SvgDoc, 100 步环 */

void Svg_History_Commit(SvgDoc *d);     /* 当前状态入栈, 清空 redo */
int  Svg_History_Undo(SvgDoc *d);
int  Svg_History_Redo(SvgDoc *d);
int  Svg_History_CanUndo/CanRedo(const SvgDoc *d);
```

- 提交时机：一个完整操作结束时（画完一个形状、手柄松手、节点拖动松手、
  样式调整松手、删除、层级移动）；
- 拖拽期间只改活文档，mouse-down 不提交；mouse-up 有变化才提交一次，
  拖动中的视觉预览不进历史；
- 视区缩放/平移、选择状态不进历史；
- 深拷贝元素数组（节点数组一并复制）；上限 100 步，超出丢最旧。
- 保护：元素数 > 10000 或单路径节点 > 100000 时拒绝新增并提示，
  避免快照放大。

## 5. 交互规范

### 5.1 视口

| 操作 | 行为 |
| --- | --- |
| 滚轮 | 以光标为中心缩放，指数步进（每 120° 一格 ×1.2），clamp 5%–3200% |
| Ctrl+0 / 工具栏 | 适应窗口（artboard 完整可见，留 32 DIP 边距） |
| Ctrl+1 | 100%，以视口中心为锚点 |
| + / - | 以视口中心缩放 |
| 中键拖拽 / 空格+左键拖拽 | 平移 |
| 状态栏 | 显示缩放百分比 |

视口 `{scale, tx, ty}`：`screen = world * scale + t`；命中用
`world = (screen - t) / scale`，再乘元素逆旋转。

### 5.2 工具与快捷键

| 键 | 工具 | 交互 |
| --- | --- | --- |
| V | 选择 | 点选/Shift 多选/空白处框选；拖动移动；箭头微调（Shift ×10） |
| N | 节点 | 进入选中路径的锚点编辑；点空白取消 |
| L | 直线 | 拖拽画线段；Shift 锁 15° 增量 |
| R | 矩形 | 拖拽；Shift 正方；Alt 从中心；默认圆角 0 |
| O | 椭圆 | 拖拽；Shift 正圆；Alt 从中心 |
| G | 多边形 | 拖拽定半径，滚轮调边数（3–64，状态栏显示），松开确认 |
| P | 钢笔（初始 L 段） | 单击加点（直线段）；Enter/双击结束；点首点闭合；Esc 取消；会话内按 L/A/B 切段型 |
| B | 钢笔（初始 B 段） | 按下落锚点，按住拖动拉出对称手柄：方向 = 切线角度、长度 = 力度；Alt 拖拽断开对称（CORNER）；拖动中实时预览手柄与曲线；点首点闭合；Enter/双击结束；会话内按 L/A/B 切段型 |
| Delete | — | 删除选中元素/节点 |
| Esc | — | 取消当前绘制或选择 |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | — | 撤销 / 重做 |
| Ctrl+D | — | 复制选中元素（偏移 16 DIP，进历史） |
| Ctrl+S / Ctrl+Shift+S | — | 导出 SVG（示例程序接系统对话框） |

输入框聚焦时工具快捷键不响应（`Ui_Input` 已占用的键交还控件）。

### 5.2.1 钢笔会话（CAD 式段命令）

钢笔（P）与贝塞尔（B）共用同一个"路径会话"。起点落定后状态栏常驻提示：

```
指定下一点  [L 直线] [A 圆弧] [B 贝塞尔] [C 闭合] [U 撤销节点]
```

| 键 | 行为 |
| --- | --- |
| L | 下一段为直线（CAD PLINE 的 Line） |
| A | 下一段为切向弧：只点终点，弧在起点与上一段相切 |
| Shift+A | 三点弧：依次点"弧上一点"+终点 |
| B | 下一段为贝塞尔：单击落角点，按住拖出对称手柄，Alt 断柄 |
| C | 以直线闭合路径（至少 2 个节点） |
| U / Ctrl+Z | 撤掉上一个节点，不进文档历史 |
| Enter / 双击 / 右键 | 结束开放路径（整条路径此时提交一次历史） |
| Esc | 取消整条路径 |

- 切换只影响**下一段**，已画段不变；同一条 PATH 内 L/C/A 混排；
- 会话进行中这些键是段命令，没有会话时才是工具快捷键；
- 首段没有"上一段切向"，按 A 自动退化为三点弧；
- 退化处理：切向弧终点落在切线上 → 改画直线；三点共线 → 改画直线；
- 整条路径完成才提交历史，撤销粒度是"一条路径"，不是单个节点。

### 5.3 选择、变换与旋转

- 选中显示旋转后的包围盒、8 个缩放手柄 + 顶部 1 个旋转手柄；
  手柄尺寸恒定（屏幕空间 8 DIP），不随缩放缩小；
- 缩放：在元素局部坐标系中解释拖拽向量，Shift 等比；从对面边/角为锚点；
  多选时统一缩放到选择框内；
- 旋转：拖旋转手柄，Shift 15° 步进；旋转绕元素中心（多选绕选择框中心）；
- 属性面板同步显示 x/y/w/h（或 cx/cy/rx/ry）、角度、旋转中心。

### 5.4 节点编辑

- 节点画法与状态：角点方块、平滑点圆点，控制点小圆（仅 `SVG_SEG_CUBIC`）；
- 拖动锚点移动节点，拖动控制点调整曲线（平滑点对侧手柄联动，Alt 单独调）；
- 每个节点的切线以"角度 + 力度"表示：角度 = out 手柄方向（度，元素局部
  坐标，0 = +x、y 向下顺时针为正），
  力度 = 手柄长度（世界单位）；`Svg_NodeGetTangent/SetTangent` 读写，
  属性面板选中节点时直接显示并可编辑，画布拖动同步更新；
- 圆弧段在弧中点画"凸度手柄"：拖动后按"两端点 + 拖动点"重解圆/椭圆参数，
  `sweep` 方向保持不变；属性面板可精调 `rx/ry/rot/large/sweep`；
- 双击线段插入节点：直线切分、贝塞尔用 De Casteljau、圆弧在参数 t=0.5
  处切成两段 `A`（同一组圆心/半径，重算 large/sweep），形状不变；
- Delete 删除选中节点；少于 2 个节点的路径随节点删除一起删除；
- 节点编辑同样只在松手时提交历史。

### 5.5 绘制中的预览

- 世界空间画已确认的段；当前拖拽段（橡皮筋）、锚点、手柄画在屏幕空间，
  1 DIP 细线，不受视口缩放影响；
- 绘制与拖动时只失效"旧包围盒 ∪ 新包围盒"（含描边余量），避免整窗重绘；
- 网格吸附：默认 8 世界单位，工具栏开关；按住 Alt 临时关闭；
- 十字光标 + 状态栏显示世界坐标。

### 5.6 属性面板

- 几何：x/y/w/h（矩形）、cx/cy/rx/ry（椭圆）、端点（直线）、角度；
- 填充/描边：无 / 纯色（预设色板 + 色值输入 + 最近使用）/ 线性渐变
  （角度 + 两色标颜色；模型支持 N 色标）；
- 宽度 0–64（0 = 不描边）、虚线四预设、线帽、连接、整体不透明度 0–100%；
- 有选中：实时改选中元素；无选中：改"新建默认样式"；
- 节点工具选中节点时，标题行显示该节点"角度 / 力度"，回车写回手柄
  并合并为一步历史；角点只改 out，平滑点镜像 in；
- 样式拖动结束才提交历史（滑杆拖动期间只预览）。

## 6. 序列化与导出

### 6.1 SVG 输出

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="800" height="600"
     viewBox="0 0 800 600">
  <defs>
    <linearGradient id="g0" gradientUnits="objectBoundingBox"
        x1="0" y1="0.5" x2="1" y2="0.5">
      <stop offset="0" stop-color="#4C8BF5"/>
      <stop offset="1" stop-color="#8226F5"/>
    </linearGradient>
  </defs>
  <rect x="40" y="40" width="200" height="120" fill="url(#g0)"
        stroke="#202020" stroke-width="2"/>
  <path d="M 10 10 L 100 20 C 120 40 140 40 160 20" fill="none"
        stroke="#202020" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round"
        stroke-dasharray="6 4" transform="rotate(15 85 15)"/>
</svg>
```

- 数值保留 2 位小数并去尾零；渐变定义去重（同参数共享 id）；
- 纯色用 `#RRGGBB`（alpha<255 时用 `stroke-opacity`/`fill-opacity`）；
- 矩形/椭圆/直线直出对应元素，多边形/路径出 `d`；
- 圆弧段出原生 `A rx ry rot large-arc-flag sweep-flag x y`，不降级成 `C`；
  `large/sweep` 在内部用 0/1 存储，导出时展开成 SVG 标志位；
- 保存：示例程序 `GetSaveFileNameW` + `CreateFileW` 写 UTF-8；
  复制：`CF_UNICODETEXT` 写 SVG 文本。

## 7. 示例程序 `samples/svgedit.c`

```
┌─────────────────────────────────────────────────────────────────┐
│ CUI SVG Editor                    | 导出 SVG | 复制 | 深/浅色   │
├─────────────────────────────────────────────────────────────────┤
│ 选择 节点 直线 矩形 椭圆 多边形 钢笔 贝塞尔 | 撤销 重做 | 网格   │
│                                  - 100% + 适应 100%             │
├──────────────────────────────────────────┬──────────────────────┤
│ 灰色工作区 + 白色画板 (可选网格/吸附)     │ 属性: X Y W H        │
│                                          │       旋转 线宽      │
│                                          │       透明 渐变角    │
│                                          │ 样式: 填充/描边/虚线 │
│                                          │       端点/连接      │
│                                          │ 层级: 上移 下移 复制 │
│                                          │       删除           │
├──────────────────────────────────────────┴──────────────────────┤
│ 状态栏: 提示/导出消息 | 缩放 107% · 世界坐标 · 元素 n · 选中 k   │
└─────────────────────────────────────────────────────────────────┘
```

- 默认 artboard 800×600；矩形/椭圆/直线/多边形与钢笔只能在画板内起笔，
  元素绘制裁剪到画板；
- 深/浅主题跟随 `UiTheme`；画板始终白色（SVG 语义），工作区用 `theme->page2`；
- 工具栏/按钮沿用 `Ui_ButtonEx`，悬停/按压走既有合成器 FX；
- 属性面板：几何字段（X/Y/W/H/旋转）应用首个选中元素，线宽/透明度/
  渐变角/填充/描边/端点/连接应用全部选中；回车提交并合并为一步历史；
- 窗口尺寸 1280×860 DIP，最小 960×640；顶栏空白区可拖动窗口。

## 8. 性能

- 路径几何缓存：`元素 id + version` 键，只在编辑/样式变化时重建；
- 渐变画刷缓存：包围盒量化（0.5 DIP）+ 角度 + 色标哈希；
- 脏区重绘：拖动/绘制失效旧新包围盒并集，缩放/平移时才整窗重绘；
- 视口变换用 D2D 变换栈，不逐点变换 CPU 侧几何；
- 典型目标：2000 元素、单路径 1000 节点，拖动时 60fps（DComp 路径）。

## 9. 验证判据

单元测试 `cui_svg_test.exe`：

| 项 | 判据 |
| --- | --- |
| 包围盒 | 直线/旋转矩形/曲线路径 bbox 与解析解一致（±0.01） |
| 命中 | 填充内部、描边容差、旋转后命中、最上层优先 |
| 节点插入 | De Casteljau 切分前后采样点偏差 < 0.01；圆弧切分前后采样偏差 < 0.01 |
| 节点切线 | 角度/力度往返误差 < 0.02° / 1e-4；对称写入手柄严格反向；力度 0 清空手柄 |
| 圆弧 | 切向弧/三点弧中心与半径对照解析解误差 < 1e-3；退化（共线/终点在切线上）改直线 |
| 圆弧编辑 | 拖动凸度手柄后弧过拖动点；改 rx≠ry 后导出与浏览器 `A` 渲染一致 |
| 历史 | 连续 20 次操作 undo 到初始态、redo 回终态，序列化逐字节一致 |
| 视口 | 世界↔屏幕互逆 1000 组随机点误差 < 1e-4 |
| 序列化 | 导出文本解析回元素属性与内存一致（导出→自解析往返） |

手工验收：

- 200% DPI 下描边宽度、手柄尺寸、命中容差符合预期；
- 5% 与 3200% 缩放正常绘制/命中，无几何抖动；
- 深/浅主题、网格开关、吸附开关；
- 钢笔/贝塞尔闭合路径、Alt 断柄、双击结束；
- 旋转后再缩放/节点编辑，形状不漂移；
- 导出文件可在浏览器（WebView2 示例页）正确显示。

## 10. 里程碑

实现状态：M1–M6 已完成（2026-11-19）。全量 `build.cmd` 通过（0CRT 校验），
`cui_svg_test.exe` 153 项全过；样例 [samples/svgedit.c](../samples/svgedit.c)
可运行，复制/导出 SVG 已与浏览器语义对照。

| 阶段 | 内容 |
| --- | --- |
| M1 | gfx: 路径（含 `AddArc`）、描边样式、变换栈、渐变画刷 + gfx 单测 |
| M2 | `svg_doc` 模型（L/C/A 段）、历史、序列化 + `cui_svg_test` |
| M3 | 组件骨架: 视口、选择工具、矩形/椭圆/直线、命中、整体移动缩放 |
| M4 | 钢笔会话（L/A/Shift+A/B/C/U）、节点编辑（含弧凸柄/切分）、旋转 |
| M5 | 属性面板、渐变编辑、导出/复制、示例外壳打磨 |
| M6 | 手工验收 + 文档/白皮书章节 |

## 11. 风险与取舍

| 风险 | 处理 |
| --- | --- |
| D2D 变换栈与裁剪嵌套出错导致画面错位 | M1 里先用单测/示例把 push/pop 配对覆盖满；断言栈深 |
| 快照内存随节点数放大 | 元素/节点上限保护 + 100 步环；后续可换差分 |
| 圆弧参数化与退化 | 切向/三点求解写解析单测；共线、终点在切线上、半径过小（按 SVG 规范放大半径）逐项覆盖 |
| 曲线命中/包围盒精度 | 统一展平（容差 0.1 世界单位），展平误差进单测；圆弧与贝塞尔同一套采样 |
| 渐变语义与浏览器不一致 | 用 `objectBoundingBox`，导出后用浏览器对照验收 |
| 多选旋转的锚点约定歧义 | 固定"绕选择框中心"，Shift 15°；属性面板显示角度 |
