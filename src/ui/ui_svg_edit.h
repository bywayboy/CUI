/*
 * ui_svg_edit.h - SVG 画布编辑组件 (视口/工具/选择/钢笔/节点/属性)
 *
 * 只负责画布内的绘制与交互: 缩放平移、创建、选择、移动/缩放/旋转、
 * 钢笔 (直线/圆弧/贝塞尔)、节点与圆弧手柄编辑、历史提交。
 * 工具箱/属性面板/导出由应用绘制, 通过本文件的选择与样式 API 读写。
 *
 * 用法:
 *   ed = Ui_SvgEdit_Create(doc, Ui_Id("svg.canvas", 0, 0));
 *   每帧: UiSvgEdit_Frame(ed, &app->ui, canvasRect);
 *   key/鼠标右键: Ui_SvgEdit_Key / Ui_SvgEdit_RightDown
 *   doc 结构变化 (增删/层级) 走本组件 API; 应用直接改几何/样式后须
 *   Svg_ElementTouch 让缓存失效。
 */
#ifndef CUI_UI_SVG_EDIT_H
#define CUI_UI_SVG_EDIT_H

#include "ui/ui_context.h"
#include "svg/svg_doc.h"

enum {
	UI_SVG_TOOL_SELECT = 0,
	UI_SVG_TOOL_NODE,
	UI_SVG_TOOL_LINE,
	UI_SVG_TOOL_RECT,
	UI_SVG_TOOL_ELLIPSE,
	UI_SVG_TOOL_POLYGON,
	UI_SVG_TOOL_PEN,
	UI_SVG_TOOL_BEZIER,
	UI_SVG_TOOL_TEXT
};

typedef struct UiSvgEdit UiSvgEdit;

UiSvgEdit *Ui_SvgEdit_Create(SvgDoc *doc, UiId id);
void Ui_SvgEdit_Destroy(UiSvgEdit *ed);

/* 每帧调用: 注册画布命中/滚轮, 处理拖动, 绘制画板/元素/选择层。 */
void Ui_SvgEdit_Frame(UiSvgEdit *ed, UiContext *ui, UiRect viewport);

/* 键盘工具键: V N L R O G P B L A B C U Delete Esc Ctrl+Z/Y/Shift+Z
 * Ctrl+0/1/+/- Ctrl+A。返回 1 = 已消费。文本输入焦点在别处时不要调用。 */
int  Ui_SvgEdit_Key(UiSvgEdit *ed, UiContext *ui, UINT vk);
/* 右键: 钢笔会话中结束路径 (客户区 DIP 坐标)。 */
void Ui_SvgEdit_RightDown(UiSvgEdit *ed, float x, float y);

void  Ui_SvgEdit_SetTool(UiSvgEdit *ed, int tool);
int   Ui_SvgEdit_Tool(const UiSvgEdit *ed);
void  Ui_SvgEdit_ZoomAt(UiSvgEdit *ed, float factor, float sx, float sy);
void  Ui_SvgEdit_ZoomFit(UiSvgEdit *ed, UiRect viewport);
void  Ui_SvgEdit_ZoomReset(UiSvgEdit *ed, UiRect viewport);
float Ui_SvgEdit_Zoom(const UiSvgEdit *ed);       /* 1 = 100% */
int   Ui_SvgEdit_PenActive(const UiSvgEdit *ed);

void Ui_SvgEdit_SetGrid(UiSvgEdit *ed, int on, float step);
int  Ui_SvgEdit_GridOn(const UiSvgEdit *ed);
int  Ui_SvgEdit_MouseWorld(const UiSvgEdit *ed, float *wx, float *wy);

/* ── 选择 ─────────────────────────────────────────────────────────── */
int  Ui_SvgEdit_SelCount(const UiSvgEdit *ed);
int  Ui_SvgEdit_SelIndex(const UiSvgEdit *ed, int nth);
void Ui_SvgEdit_SelectAll(UiSvgEdit *ed);
void Ui_SvgEdit_ClearSel(UiSvgEdit *ed);
int  Ui_SvgEdit_SelectionBounds(const UiSvgEdit *ed, GfxRect *out);
void Ui_SvgEdit_DeleteSel(UiSvgEdit *ed, UiContext *ui);
void Ui_SvgEdit_DuplicateSel(UiSvgEdit *ed, UiContext *ui);
void Ui_SvgEdit_Reorder(UiSvgEdit *ed, UiContext *ui, int dir);
/* 组 / 解组 (Ctrl+G / Ctrl+Shift+G); 返回 1 = 成功. */
int  Ui_SvgEdit_Group(UiSvgEdit *ed, UiContext *ui);
int  Ui_SvgEdit_Ungroup(UiSvgEdit *ed, UiContext *ui);
/* 图层树只读快照 (DFS 先序, depth 从 0 起) */
typedef struct UiSvgLayerRow {
	int index, parent, depth;
	uint32_t uid;
	int isGroup, visible, locked, childCount;
	char name[48];
} UiSvgLayerRow;
int  Ui_SvgEdit_Layers(const UiSvgEdit *ed, UiSvgLayerRow *out, int cap);
/* 图层面板操作 */
void Ui_SvgEdit_SelectIndex(UiSvgEdit *ed, int idx);
void Ui_SvgEdit_SetVisible(UiSvgEdit *ed, int idx, int on);
void Ui_SvgEdit_SetLocked(UiSvgEdit *ed, int idx, int on);
void Ui_SvgEdit_Rename(UiSvgEdit *ed, int idx, const char *utf8);
int  Ui_SvgEdit_MoveOut(UiSvgEdit *ed, UiContext *ui);   /* 移出所在组 */

/* ── 文本 (M9) ───────────────────────────────────────────────────── */
int  Ui_SvgEdit_TextTarget(const UiSvgEdit *ed);   /* 选中文本元素下标, -1 */
void Ui_SvgEdit_SetTextContent(UiSvgEdit *ed, UiContext *ui,
	const char *utf8);
void Ui_SvgEdit_SetTextFont(UiSvgEdit *ed, UiContext *ui,
	const char *family, float size, int weight);
void Ui_SvgEdit_SetTextAlign(UiSvgEdit *ed, UiContext *ui,
	int align, int anchor);

/* ── 节点切线 (角度/力度) ─────────────────────────────────────────── */
/* 节点工具下当前选中节点; 返回节点索引, -1 = 无. */
int  Ui_SvgEdit_NodeIndex(const UiSvgEdit *ed, int *elIndex);
/* 选中节点的切线: 角度 (度, 元素局部坐标) + 力度 (世界单位). */
int  Ui_SvgEdit_NodeParams(const UiSvgEdit *ed, float *angleDeg,
	float *strength, int *symmetric);
/* 写选中节点切线; symmetric=1 镜像对侧手柄. 调用方负责 Svg_History_Commit. */
int  Ui_SvgEdit_SetNodeParams(UiSvgEdit *ed, float angleDeg, float strength,
	int symmetric);

/* ── 样式 ─────────────────────────────────────────────────────────── */
/* 取首个选中元素的样式 (无选中取新建默认样式); 调用方用 Svg_StyleFree。 */
int  Ui_SvgEdit_GetStyle(const UiSvgEdit *ed, SvgStyle *out);
/* 应用到全部选中元素, 并成为之后新建元素的默认样式。 */
void Ui_SvgEdit_SetStyle(UiSvgEdit *ed, UiContext *ui, const SvgStyle *style);

/* ── 历史 ─────────────────────────────────────────────────────────── */
void Ui_SvgEdit_Commit(UiSvgEdit *ed);            /* 属性面板改完后提交 */
void Ui_SvgEdit_Undo(UiSvgEdit *ed, UiContext *ui);
void Ui_SvgEdit_Redo(UiSvgEdit *ed, UiContext *ui);
int  Ui_SvgEdit_CanUndo(const UiSvgEdit *ed);
int  Ui_SvgEdit_CanRedo(const UiSvgEdit *ed);

/* 状态栏提示 (UTF-8, 随工具/会话变化) */
const char *Ui_SvgEdit_Hint(const UiSvgEdit *ed);

#endif /* CUI_UI_SVG_EDIT_H */
