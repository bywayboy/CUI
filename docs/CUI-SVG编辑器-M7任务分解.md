# CUI SVG 编辑器 M7 任务分解（文档树）

> 状态：M7.1–M7.5 已完成（2026-11-19）：文档树/uid/TRS/样式继承/DFS 导出/
> 历史快照/命中/`Gfx_PushMatrix`，`cui_svg_test` 180 项全通过。
> 编辑器侧：DFS 绘制、doc 级包围盒/命中、组/解组 API 与按钮、组/嵌套元素
> 的移动/缩放/旋转走 xf。已知限制（M8+）：嵌套组内节点编辑暂禁用；框选只
> 选根级；图层面板与拖拽排序未做（M8）。
> 关联：`CUI-SVG编辑器-二期设计方案.md` §2.1 / §4
> 目标：把扁平元素数组升级为 `<g>` 嵌套树（变换组合 + 样式继承），
> 命中/包围盒/导出/历史/编辑器/示例全部适配；**不做**文本/径向渐变/导入。

## 0. 关键设计前置

1. **元素稳定 uid**：`SvgElement.uid`（doc 内唯一，历史快照保留）。
   编辑器选择集改为存 uid，树结构操作/撤销后不再依赖下标重映射；
   对外 API 仍返回索引（每帧解析）。
2. **局部几何 + TRS 变换**：
   ```c
   typedef struct SvgTransform {
       int has;                 /* 0 = 单位 */
       float tx, ty;            /* translate */
       float rot;               /* 度, 顺时针 */
       float cx, cy;            /* 旋转/缩放中心 (元素局部坐标) */
       float sx, sy;            /* scale, 默认 1 */
   } SvgTransform;
   ```
   世界矩阵 = 父世界 × T(tx,ty)·T(cx,cy)·R(rot)·S(sx,sy)·T(-cx,-cy)。
   一期的 `SvgElement.rot` 迁移为 `xf.rot`，`cx,cy = Svg_ElementCenter`，
   几何坐标不变 → 旧文档视觉不变。
3. **根序 = 数组序**：`parent == -1` 的元素按 `els[]` 下标顺序为根序；
   子序 = `children[]` 顺序；绘制/导出 = DFS 先序，后序在上。
4. **样式继承**：`SvgStyle` 增加 `unsigned setMask`（`SVG_STYLE_FILL /
   STROKE / WIDTH / CAPJOIN / DASH / OPACITY`）。组只携带“覆盖位”，
   叶子的有效样式由 `SvgDoc_ResolveStyle()` 沿父链补齐。
5. **限制**：深度 ≤ 32，元素 ≤ `SVG_MAX_ELEMENTS`，节点 ≤
   `SVG_MAX_PATH_NODES`；组不得成为自身祖先。
6. **gfx 增量**：`void Gfx_PushMatrix(Gfx *g, const float m[6]);`
   （与 `Gfx_PopTransform` 共用栈，深度 8 → **16**）。

## 1. `src/svg/svg_doc.h` 结构变更

| 结构 | 变更 |
|:--|:--|
| `SvgElType` | 追加 `SVG_EL_GROUP`（M9 再追加 TEXT） |
| `SvgTransform` | 新增（上面的定义） |
| `SvgStyle` | 追加 `unsigned setMask`；`opacity` 参与掩码 |
| `SvgElement` | 追加 `uint32_t uid; int parent; int *children; int childCount, childCap; SvgTransform xf;`；保留 `rot` 字段一版作迁移别名（仅 `Svg_ElementCenter/旋转手柄` 内部使用，M7 完成后删除） |
| `SvgDoc` | 追加 `uint32_t nextUid; int depthMax;` |

新增常量/枚举：`SVG_MAX_DEPTH 32`、`SVG_STYLE_*` 位。

## 2. `src/svg/svg_doc.c` 函数级清单

### 2.1 现有函数需要修改

| 函数 | 修改 |
|:--|:--|
| `element_init` | 初始化 parent=-1/children/xf/uid；GROUP 默认空组、无几何 |
| `element_free` | 释放 children 数组；GROUP 不再持有 path |
| `element_copy` | 深拷贝 children 索引数组与 xf/uid/style 掩码 |
| `doc_reserve` | 不变（元素池仍平铺） |
| `snap_take` / `doc_restore` / `doc_clear_elements` | 快照包含 parent/children/uid/nextUid；restore 后校验 dfs 无环 |
| `SvgDoc_Add` | 追加到根，`parent=-1`，分配 uid |
| `SvgDoc_Remove` | 从父 children 摘除；组递归删子树；**压缩数组后 remap 所有 parent/children 索引**（新增静态 `doc_compact_and_remap_(d, removed)`） |
| `SvgDoc_Duplicate` | 深拷贝子树（新 uid），插入源元素之后的同父位置 |
| `SvgDoc_MoveZ` | 改为**同父内**兄弟交换（原先全局数组交换） |
| `Svg_ElementTouch` | version++；新增 `doc_touch_subtree_`：组变换/样式变更时子树 bounds 失效 |
| `Svg_DocHit` | DFS 逆序遍历；`SvgDoc_IsLocked` 跳过；组先测世界 AABB；返回最上层 uid 对应索引 |
| `Svg_Export` | 拆分 `export_element_(d,b,idx,indent,gd)`，DFS 输出：组 `<g transform="…" style="…">` 子元素 `</g>`；叶沿用一期属性 + `transform` |
| `export_style` | 只输出 `setMask` 中的字段（组与叶子共用） |
| `Svg_ElementPoly` / `element_flatten_raw` | 仅叶子；组返回 0（命中走高层的子遍历） |
| `Svg_ElementLocalBounds` | 保留：叶子几何局部 bounds（GROUP 返回空） |
| `Svg_WorldToLocal` / `Svg_LocalToWorld` | 一期签名保留给叶子内部（基于 xf），新增文档版（见下） |

### 2.2 新增公共函数

```c
/* 树遍历 */
int  SvgDoc_RootCount(const SvgDoc *d);
int  SvgDoc_RootAt(const SvgDoc *d, int nth);              /* 根下标 */
int  SvgDoc_ChildCount(const SvgDoc *d, int parent);       /* -1 = 根 */
int  SvgDoc_ChildAt(const SvgDoc *d, int parent, int nth);
int  SvgDoc_ParentOf(const SvgDoc *d, int idx);
int  SvgDoc_DepthOf(const SvgDoc *d, int idx);
int  SvgDoc_IndexOfUid(const SvgDoc *d, uint32_t uid);
uint32_t SvgDoc_UidAt(const SvgDoc *d, int idx);
typedef int (*SvgVisitFn)(SvgDoc *d, int idx, void *user); /* 1 = 停止 */
void SvgDoc_WalkDfs(SvgDoc *d, int visibleOnly, SvgVisitFn fn, void *user);

/* 结构编辑 */
SvgElement *SvgDoc_AddChild(SvgDoc *d, int parent, SvgElType type);
int  SvgDoc_Group(SvgDoc *d, const int *indices, int n);   /* 同父; 返回组下标, -1 失败 */
int  SvgDoc_Ungroup(SvgDoc *d, int group);                 /* 返回解组后的子数 */
int  SvgDoc_Reparent(SvgDoc *d, int idx, int newParent, int pos); /* 保持世界外观 */
void SvgDoc_SetVisible(SvgDoc *d, int idx, int on);        /* 有效可见性 */
void SvgDoc_SetLocked(SvgDoc *d, int idx, int on);
int  SvgDoc_IsVisible(const SvgDoc *d, int idx);           /* 含祖先链 */
int  SvgDoc_IsLocked(const SvgDoc *d, int idx);
int  SvgDoc_CommonParent(const SvgDoc *d, const int *idx, int n);

/* 变换 / 样式 */
void SvgDoc_ParentMatrix(const SvgDoc *d, int idx, float m[6]);
void SvgDoc_WorldMatrix(const SvgDoc *d, int idx, float m[6]);
void Svg_DocWorldToLocal(const SvgDoc *d, int idx, float wx, float wy,
                         float *lx, float *ly);
void Svg_DocLocalToWorld(const SvgDoc *d, int idx, float lx, float ly,
                         float *wx, float *wy);
int  SvgDoc_ResolveStyle(const SvgDoc *d, int idx, SvgStyle *out);
GfxRect SvgDoc_ElementBounds(const SvgDoc *d, int idx);    /* 世界 AABB, 组=子树并集 */

/* 组变换的编辑辅助（保持局部几何不动） */
void SvgDoc_Translate(SvgDoc *d, int idx, float dx, float dy);   /* 改 xf.tx/ty */
void SvgDoc_RotateBy(SvgDoc *d, int idx, float dDeg, float cx, float cy);
void SvgDoc_ScaleBy(SvgDoc *d, int idx, float kx, float ky, float cx, float cy);
```

> `SvgDoc_Translate/RotateBy/ScaleBy` 采用“重写 xf 并前置补偿”的方式让
> 世界矩阵产生期望增量，避免逐元素改几何；组与多选都走这三个入口。

### 2.3 静态辅助（新增）

- `doc_parent_of_ / doc_child_append_ / doc_child_remove_ / doc_child_insert_`
- `doc_compact_and_remap_(d, removed)`：数组压缩 + parent/children 全量 remap
- `doc_cycle_guard_(d, idx, newParent)`
- `mat_mul_ / mat_invert_ / mat_from_trs_`（2×3 仿射）
- `style_inherit_(child, parent)`（用于 ResolveStyle）
- `export_element_ / export_style_attrs_ / indent_`

## 3. `src/ui/ui_svg_edit.c` 函数级清单

| 函数 | 修改点 |
|:--|:--|
| `sel_bounds` / `sel_bounds_key` | 用 `SvgDoc_ElementBounds`；key 纳入 uid + 子树版本 + 变换 |
| `path_edit_index` | DFS 找首个 PATH/POLYGON；组本身不可做节点编辑 |
| `orig_capture` | 捕获 xf（组也一样），不再只存几何快照 |
| `draw_element` → `draw_subtree_` | DFS：组压 `Gfx_PushMatrix` 后递归；叶子沿用几何绘制（局部坐标） |
| `draw_pen_overlay` / `draw_node_overlay` / `hit_node` | 节点命中走 `Svg_DocWorldToLocal`；组不参与 |
| `create_update` / `create_finish` | 新元素用 `SvgDoc_AddChild` 挂到**当前激活父级**（默认根）；鼠标世界坐标先转父局部 |
| `pen_apply_first_handles` / `pen_add_point` / `pen_finish` | 同上，路径几何存父局部 |
| `press_left` / `update_drag` | 移动/缩放/旋转统一调用 `SvgDoc_Translate/RotateBy/ScaleBy`；多选按公共父计算 |
| `down/up` 选择逻辑 & `rubber_finish` | 选择集改存 **uid**：`sel_add_uid/sel_has_uid/sel_sync_indices_` |
| `drag_end_commit` | 结构变化后 `sel_sanitize_`（uid 解析不到则剔除） |
| `press_node_tool` | 组不进入节点分支 |
| `Ui_SvgEdit_Frame` | 遍历改 DFS；裁剪用世界 AABB；组选中绘制包围框而非手柄堆 |
| `Ui_SvgEdit_Key` | 新增 `Ctrl+G` 组 / `Ctrl+Shift+G` 解组；Delete/Duplicate/Reorder 走父级感知 API |
| `Ui_SvgEdit_DeleteSel / DuplicateSel / Reorder` | 同父操作；删除含组则整组删 |
| `Ui_SvgEdit_GetStyle / SetStyle` | Get 返回**解析后样式**并附 `setMask`；Set 对组写覆盖位、对叶子写完整样式 |
| `Ui_SvgEdit_ZoomFit` | 边界取 `SvgDoc_ElementBounds` 的并集 |
| `sel_node` 及选择公开 API | 内部 uid → 每帧索引解析 |

新增 API（`ui_svg_edit.h`）：

```c
int  Ui_SvgEdit_Group(UiSvgEdit *ed, UiContext *ui);     /* Ctrl+G */
int  Ui_SvgEdit_Ungroup(UiSvgEdit *ed, UiContext *ui);
int  Ui_SvgEdit_ActiveParent(const UiSvgEdit *ed);       /* -1 = 根 */
void Ui_SvgEdit_SetActiveParent(UiSvgEdit *ed, int idx); /* 双击组进入? M8 用 */
/* 图层树只读快照（M8 图层面板消费） */
typedef struct UiSvgLayerRow {
    int index, depth, parent;     /* 文档下标 / 缩进 / 父下标 */
    uint32_t uid;
    int isGroup, visible, locked;
    char name[48];
} UiSvgLayerRow;
int  Ui_SvgEdit_Layers(const UiSvgEdit *ed, UiSvgLayerRow *out, int cap);
```

## 4. `src/gfx` 增量

- `void Gfx_PushMatrix(Gfx *g, const float m[6]);`：与现有变换栈共用
  `Gfx_PopTransform`；深度常量 8 → 16；D2D 用 `SetTransform(comp * m)`。
- 单测：`tests/gfx_path_test.c` 增加 push/pop 嵌套矩阵后绘制位置断言。

## 5. 示例与测试

`src` 示例 `samples/svgedit.c`：
- 3 处扁平访问改根/DFS；工具栏加“组/解组”按钮；
- 状态栏显示当前父级/图层数；M8 再做完整图层面板。

`tests/svg_test.c` 新增（黄金值）：
1. 建树：根 3 + 组 2 子，DFS 顺序 = 根序 + 组内序；
2. 世界矩阵：祖父 translate + 父 rotate + 子 scale，点变换与手算一致；
3. 组/解组：世界外观不变（抽 5 个点比对）；嵌套组解组；
4. `Reparent` 保持世界外观（同上）；
5. 样式继承：组 fill + 子无 fill → 解析为组色；子覆盖生效；
6. 导出往返：`<g transform>` 嵌套 + 样式属性；再解析（M7 用字符串断言，
   M8+ 用三期解析器）；结构序列化后 uid/父链一致；
7. 历史：树操作 undo/redo 后 DFS/uid/父子一致；组内编辑 undo；
8. 命中：组内上层元素优先；锁定/隐藏不命中；
9. 压缩 remap：删除中间元素后 parent/children 全部正确；
10. 深度/循环保护：Group 自身/祖先 → 失败。

## 6. 实施顺序（M7 内部）

1. `svg_doc.h` 结构与 gfx `Gfx_PushMatrix`；
2. `element_*` + 树遍历 + uid + 快照（先让 `svg_test` 编译通过）；
3. 结构编辑（AddChild/Remove/Duplicate/MoveZ/Group/Ungroup/Reparent）
   + remap + 测试 1/3/4/9；
4. 世界矩阵/样式继承/bounds/hit + 测试 2/5/8；
5. 导出 DFS + 测试 6；
6. 编辑器改造（选择 uid、拖动三入口、绘制 DFS、Key/工具栏）；
7. 示例适配 + 回归一期手工项（绘制/节点/钢笔/旋转/渐变异形）；
8. 性能抽查：500 元素深度 5 的 DFS 绘帧与命中耗时；快照 100 步内存。

## 7. 验收

- `./build.cmd` 通过，`cui_svg_test` 新增用例全绿，一期测试不回归；
- 真机：一期全部操作在新模型下行为不变；组/解组、嵌套移动/旋转/
  缩放视觉正确；隐藏/锁定生效；导出在浏览器与编辑器内一致；
- 性能：500 元素含 10 个嵌套组，拖动帧耗时 ≤ 一期同规模 +20%；
- 历史：树编辑 100 步内 Undo/Redo 无索引错乱（uid 兜底）。

## 8. 回滚策略

M7 是结构性提交：先在分支/补丁内完成，`svg_test` 黄金用例通过后再合入；
gfx 矩阵栈与文档树一起提交，避免出现“树模型 + 旧扁平绘制”的中间态。
