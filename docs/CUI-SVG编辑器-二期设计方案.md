# CUI SVG 编辑器二期设计方案（增强）

> 状态：设计已确认（2026-11-19），待实施
> 关联：一期文档 `docs/CUI-SVG编辑器-设计方案.md`（M1–M6 已完成）
> 涉及：`src/svg/svg_doc.*`、`src/ui/ui_svg_edit.*`、`src/gfx/gfx.*`、
> `samples/svgedit.c`、`tests/svg_test.c`

## 1. 背景与定位

一期已交付：视口缩放/平移/滚动条、矩形/椭圆/直线/多边形/钢笔画布、
路径节点与圆弧/贝塞尔手柄、整体移动缩放旋转、线性渐变、快照 Undo、
SVG 文本导出。当前短板集中在“**创作表达力**”而非基础框架：

| 缺口 | 影响 |
|:--|:--|
| 无文本 | 只能画图形，任何带标注的图都做不了 |
| 无 `<g>` 分组/图层 | 元素一多就无法组织；导入/导出层级受限 |
| 渐变只有线性两色标 | 插画/UI 图形表达力不足 |
| 无对齐/吸附/数值检查器 | 精修费手，重复劳动多 |
| 外壳面板拥挤 | 图层、样式、几何信息没有落点 |
| 不能打开 SVG | 明确留到三期（本期范围外） |

二期目标：把组件从“绘图组件”提升为**可日常创作图形的编辑器**；
导入已有 SVG 仍是三期，本期只做文档模型兼容（树/文本/径向渐变都能导出）。

二期锁定决策：

1. **绘制能力优先**（文本/分组/径向渐变先于导入）；
2. `<g>` 采用**真正嵌套树**（嵌套变换与样式继承）；
3. 文本 = **基础 text + 转路径外框兜底**（跨端一致性）；
4. 外壳改为**三栏**：左工具栏 / 中画布 / 右检查器（样式·几何·图层）。

## 2. 架构改动

### 2.1 文档模型：嵌套树

一期元素是扁平数组；二期升级为树，仍保持**快照历史**可复制：

```c
typedef struct SvgTransform {
	int has;              /* 0 = 单位变换 */
	float tx, ty;         /* translate */
	float rot;            /* rotate (度, 绕元素局部原点) */
	float sx, sy;         /* scale, 默认 1 */
} SvgTransform;

typedef struct SvgElement {
	SvgElType type;       /* ... 新增 SVG_EL_GROUP / SVG_EL_TEXT */
	int parent;           /* 父元素下标, -1 = 根 */
	int *children;        /* 子元素下标数组 (仅 GROUP) */
	int childCount, childCap;
	SvgTransform xf;      /* 局部变换, 与一期 rotation 兼容迁移 */
	SvgStyle style;       /* 继承: 未设置的字段由父链补齐 */
	/* 几何: rect/ellipse/line/polygon/path ... */
	/* 文本: char *text(UTF-8); char family[64]; float size,weight;
	   int align, anchor; float lineHeight; */
	unsigned short version;
} SvgElement;

typedef struct SvgDoc {
	float width, height;
	SvgElement *els; int count, cap;   /* 森林: parent==-1 为根 */
	GfxColor background;
	void *history;
} SvgDoc;
```

- 渲染/命中：按**文档顺序深度优先**，后序在上；命中取最上层。
- 变换：`world = 父 world × 自身 transform × 几何局部`；一期
  `rotate(a,cx,cy)` 迁移成 `rot` + 元素局部原点，导出保持 SVG `A` 段不变。
- 样式继承：`SvgStyle` 增加 `hasFill/hasStroke/hasWidth/...` 位，
  未设置时沿父链取（等价 SVG 继承）；组本身不绘制。
- 历史：快照复制需递归复制 children 数组；提供 `Svg_DocClone`；
  元素/节点数上限与 100 步环保留。

### 2.2 文本

- `SVG_EL_TEXT`：单段文本 + 显式换行（`\n` 逐行绘制，导出为多个
  `<tspan x dy>`）；字体族/字号/字重/对齐(left/center/right)/锚点
  (start/middle/end)/行高，颜色走 fill。
- 测量：`Gfx_TextWidth/TextHeight`（DWrite 同源）；命中用文本外框矩形。
- 字体选择：面板提供系统字体族列表（DWrite 枚举，懒加载缓存），
  默认 `Segoe UI`；不在文档里嵌字体。
- **转路径兜底**：`Svg_TextToPath(doc, index)` 用 DWrite glyph outline
  取轮廓生成 `SVG_EL_PATH`（新增 `Gfx_TextToPath`），替换原文本元素，
  可撤销；用于导出跨端一致性。

### 2.3 渐变与描边

- `SVG_PAINT_RADIAL`：`radialGradient(cx,cy,r,fx,fy)` + N 色标；
  渲染用 D2D radial brush，导出原生 `<radialGradient>`。
- 色标编辑：默认面板两枚，支持增删/拖动；**画布手柄**：
  线性画起止与色标位置，径向画圆心/半径/焦点。
- 描边补齐：虚线数组编辑（预设 + 自定义）、dash offset、线帽/连接、
  填充规则 evenodd、描边不透明度。

### 2.4 交互与效率

- 多选包围框：移动/缩放/旋转（组与多选统一走 world 变换）。
- 吸附：网格、元素包围盒边/中点、画布中心、角度 45°；参考线可拖出
  （视口内辅助线，不入历史）。
- 对齐/分布：左/中/右/上/中/下，等距分布（≥3 个元素）。
- 数值检查器：X/Y/W/H、旋转、节点坐标、字号/行高，可键入。
- 剪贴板：组件内复制/粘贴（跨文档暂不支持）；方向键 1px / Shift 10px；
  框选（marquee）。
- 上下文菜单：组合/解组、层级、对齐、复制/删除、转路径。

### 2.5 外壳（三栏）

```
┌──────────────────────────────────────────────────────────────┐
│ TopBar: 文件(打开/保存/导出)* · Undo/Redo · 缩放 · 画布尺寸     │
├──────┬───────────────────────────────────┬───────────────────┤
│ Tool │                                   │ Inspector tabs:   │
│ rail │           Canvas (viewport)       │ 样式 | 几何 | 图层 │
│ V N T│                                   │ (可折叠/可调宽)    │
│ L R O│                                   │ 图层树: 显隐/锁定  │
│ G P B│                                   │        重命名/拖排 │
├──────┴───────────────────────────────────┴───────────────────┤
│ Status: 坐标 / 缩放 / 工具提示 / 选中信息                       │
└──────────────────────────────────────────────────────────────┘
```
*打开/保存仍为三期；TopBar 本期保留导出。

## 3. 新增/变更 API（草案）

```c
/* svg_doc */
SvgElement *SvgDoc_AddChild(SvgDoc *d, int parent, SvgElType type);
int   SvgDoc_Group(SvgDoc *d, const int *indices, int n);   /* 返回组下标 */
int   SvgDoc_Ungroup(SvgDoc *d, int groupIndex);
int   SvgDoc_SetText(SvgDoc *d, int index, const char *utf8);
int   SvgDoc_TextToPath(SvgDoc *d, int index);              /* 替换为 PATH */
int   SvgDoc_Reparent(SvgDoc *d, int index, int newParent, int pos);
int   SvgDoc_SetVisible / SetLocked(d, index, on);          /* 编辑辅助位, 不入 SVG */
SvgElement *SvgDoc_HitTest(...);   /* 树版, 返回最上层 */
void  SvgDoc_WorldMatrix(const SvgDoc*, int index, float m[6]);

/* ui_svg_edit */
void  Ui_SvgEdit_SetTool(... SVG_EL 增加 TEXT 工具 T);
int   Ui_SvgEdit_Group / Ungroup(SvgEdit*, UiContext*);
int   Ui_SvgEdit_TextToPath(SvgEdit*, UiContext*);
void  Ui_SvgEdit_SetTextProps(...);
void  Ui_SvgEdit_SetSnap(...);      /* grid/element/guide/angle 开关与容差 */
void  Ui_SvgEdit_Align / Distribute(...);
int   Ui_SvgEdit_SelectionRect(...); /* 多选 world 包围框 */
int   Ui_SvgEdit_LayersSnapshot(...); /* 图层树只读快照供外壳绘制 */

/* gfx */
int   Gfx_TextToPath(GfxFont *f, const char *utf8, GfxPath *out);
void  Gfx_FillRadialGradient(...);   /* 或 Gfx_RadialBrush + FillPathBrush */
```

## 4. 里程碑（M7–M12）

| 阶段 | 内容 | 验收 |
|:--|:--|:--|
| **M7 文档树** | `<g>` 嵌套、transform 组合、样式继承、扁平→树迁移、组/解组 API、图层树只读快照；历史/导出/命中/包围盒全部适配 | `svg_test` 增加树序、变换组合、导出往返；一期样例功能不回归 |
| **M8 图层与外壳三栏** | 三栏布局；图层树（显隐/锁定/重命名/拖拽排序）；组合/解组/层级命令；检查器页签骨架 | 真机：10+ 元素分组、排序、显隐/锁定生效；撤销正确 |
| **M9 文本 v1** | `SVG_EL_TEXT`、文本工具、字体族/字号/字重/对齐/锚点/行高、多行、命中/边框、导出 `<text>`；检查器文本页 | 新建/编辑/删除文本；导出在浏览器与编辑器内一致（字体同机） |
| **M10 文本转路径** | `Gfx_TextToPath`（DWrite 轮廓→GfxPath）；元素替换、可撤销；跨端导出验收 | 转路径后形状与 text 视觉一致；取消可恢复文本 |
| **M11 渐变/描边完善** | 径向渐变 + N 色标 + 画布手柄；虚线/线帽/连接/填充规则；样式检查器完整 | 双色标/三色标嵌套渐变导出正确；画布手柄拖动命中 |
| **M12 效率与收尾** | 吸附（网格/元素/参考线/45°）、对齐分布、数值检查器、复制粘贴、方向键、框选、右键菜单；PNG 导出（可选）；文档/快捷键总表 | 吸附/对齐精度 ≤0.5 DIP；10k 节点文档交互无明显卡顿 |

说明：导入（打开 SVG）仍为**三期**；M7 的树模型/样式继承为导入打基础。

进度：M7（文档树）、M8（三栏外壳 + 图层面板）已于 2026-11-19 实现。
M8 现状：左工具栏 / 中画布 / 右检查器（样式·几何·图层页签）；图层树
显示层级缩进、显隐/锁定开关、选中、名称编辑、上移/下移、移出组、
组合/解组/复制/删除。拖拽排序暂用按钮（行拖拽留待后续）；嵌套组内
节点编辑仍限制为根级（M9 起逐步放开）。

## 5. 性能与内存

- 渲染：视口裁剪 + 元素 bounds 缓存（一期已有）扩展到树；
  组变换改变时仅失效子树 bounds。
- 命中：按 z 逆序（文档顺序逆向）早退；组命中先测包围框。
- 历史：树快照；元素/节点上限（如 5000 元素）与 100 步环；
  M12 评估是否切换为“结构快照 + 几何差分”。
- 文本：测量结果缓存（text/font/width/version 为键）；字体枚举懒加载。

## 6. 风险

- **树迁移面大**：命中/包围盒/导出/历史/编辑器操作都依赖扁平索引；
  迁移必须同一步完成并靠黄金测试兜底，禁止半树半扁。
- **快照体积**：组/文本/导入后文档变大；上线前压测 5000 元素 × 100 步。
- **文本度量差异**：DWrite 与浏览器 shaping 不同；靠“转路径”兜底，
  文档明确说明不支持复杂排版（双向文本/连字）。
- **DWrite 轮廓**：`GetGlyphRunOutline` 需处理 bezier 拼接与闭合；
  先单测（字母/汉字/曲线）再接入。
- **径向渐变**：D2D radial 与 SVG focal 语义映射需验证；先导出往返比对。

## 7. 讨论中已锁定的结论

- 二期优先级：**绘制能力**（文本/分组/渐变）> 导入；
- 分组：**真正嵌套树**（非扁平 groupId）；
- 文本：**基础 text + 转路径兜底**；
- 外壳：**三栏重排**（左工具栏/中画布/右检查器）。
