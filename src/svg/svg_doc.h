/*
 * svg_doc.h - SVG 文档模型 (纯 C, 不依赖 UiContext / Direct2D 设备)
 *
 * 坐标: 世界单位 (DIP 语义, 原点在画板左上, y 向下).
 * 元素几何存未旋转坐标, 旋转绕元素局部包围盒中心 (见 Svg_ElementCenter).
 * 路径段型: LINE / CUBIC / ARC; 弧为 SVG A 端点参数化, 导出不降级.
 *
 * 历史为快照式: 完整操作结束时 Svg_History_Commit, 上限 100 步.
 * 错误处理: 新增/复制失败返回 NULL/-1; 内存全部走 HeapAlloc.
 */
#ifndef CUI_SVG_DOC_H
#define CUI_SVG_DOC_H

#include <windows.h>
#include "gfx/gfx.h"

#define SVG_MAX_ELEMENTS     10000
#define SVG_MAX_PATH_NODES   100000
#define SVG_HISTORY_STEPS    100

typedef enum {
	SVG_EL_RECT, SVG_EL_ELLIPSE, SVG_EL_LINE, SVG_EL_POLYGON, SVG_EL_PATH,
	SVG_EL_GROUP
} SvgElType;

/* 每个元素局部 TRS; 世界 = 父世界 × 自身. 枢轴 (cx,cy) 在元素局部坐标. */
typedef struct SvgTransform {
	int has;                /* 0 = 单位 */
	float tx, ty;
	float rot;              /* 度, 屏幕顺时针 */
	float cx, cy;           /* 旋转/缩放中心 */
	float sx, sy;           /* 默认 1 */
} SvgTransform;

typedef enum { SVG_NODE_CORNER, SVG_NODE_SMOOTH } SvgNodeType;

/* 节点: 锚点 + 三次贝塞尔进出控制点 (相对锚点的偏移, 元素局部坐标) */
typedef struct {
	float x, y;
	float inX, inY, outX, outY;
	uint8_t type, hasIn, hasOut;
} SvgNode;

/* 段: nodes[i] -> nodes[i+1]; 闭合时 seg[count-1] 是回到 nodes[0] 的段 */
typedef enum { SVG_SEG_LINE, SVG_SEG_CUBIC, SVG_SEG_ARC } SvgSegType;

/* SVG A 段参数: 端点由目标节点给出 */
typedef struct {
	float rx, ry, rotDeg;
	uint8_t large, sweep;
} SvgArc;

typedef struct {
	SvgNode *nodes;  int count, cap;
	SvgSegType *seg; /* 长度 = closed ? count : count-1 (无段时 0) */
	SvgArc *arc;     /* 与 seg 同长; 仅 seg==SVG_SEG_ARC 有效 */
	uint8_t closed;
} SvgPath;

typedef enum { SVG_PAINT_NONE, SVG_PAINT_SOLID, SVG_PAINT_LINEAR } SvgPaintKind;

typedef struct { float offset; GfxColor color; } SvgGradStop;

typedef struct {
	SvgPaintKind kind;
	GfxColor color;                 /* SOLID */
	float angleDeg;                 /* LINEAR: 屏幕坐标系角度, 0=左→右 */
	SvgGradStop *stops;             /* LINEAR, N 色标 (调用方保证有序) */
	int stopCount;
} SvgPaint;

enum { SVG_CAP_BUTT = 0, SVG_CAP_ROUND, SVG_CAP_SQUARE };
enum { SVG_JOIN_MITER = 0, SVG_JOIN_ROUND, SVG_JOIN_BEVEL };

/* 样式覆盖位: 组只设置部分字段, 叶子按父链补全 (SVG 继承). */
enum {
	SVG_STYLE_FILL   = 1u << 0,
	SVG_STYLE_STROKE = 1u << 1,
	SVG_STYLE_WIDTH  = 1u << 2,
	SVG_STYLE_CAPJOIN = 1u << 3,
	SVG_STYLE_DASH   = 1u << 4,
	SVG_STYLE_OPACITY = 1u << 5,
	SVG_STYLE_ALL    = 0x3Fu
};

typedef struct {
	SvgPaint fill, stroke;
	float strokeWidth;              /* 世界单位, 默认 2 */
	float dash[8]; int dashCount;   /* 空 = 实线 */
	int cap, join;
	float opacity;                  /* 0..1 */
	unsigned setMask;               /* SVG_STYLE_*; 叶子默认全开 */
} SvgStyle;

typedef struct {
	uint32_t uid;                   /* 文档内稳定 id (历史快照保留) */
	int parent;                     /* 父元素下标; -1 = 根 */
	int *children;                  /* 仅 GROUP: 有序子下标 */
	int childCount, childCap;
	SvgElType type;
	char name[48];                  /* id / data-name, 可空 */
	/* 参数几何 (世界坐标, 未旋转) */
	union {
		struct { float x, y, w, h; } rect;
		struct { float cx, cy, rx, ry; } ellipse;
		struct { float x1, y1, x2, y2; } line;
	} p;
	SvgPath path;                   /* PATH / POLYGON; 其余类型按参数展开 */
	SvgStyle style;
	SvgTransform xf;                /* 新模型; 组合根/组/嵌套变换 */
	float rot;                      /* 一期兼容: 叶子自转 (xf 未启用时使用) */
	uint8_t visible, locked;
	int version;                    /* 几何/样式变更 +1 (UI 缓存键) */
} SvgElement;

typedef struct SvgDoc {
	float width, height;            /* artboard = SVG viewBox */
	GfxColor background;            /* 0 = 透明 */
	SvgElement *els;  int count, cap;
	uint32_t nextUid;
	void *history;                  /* 快照环 (内部) */
} SvgDoc;

/* ── 文件生命周期 ─────────────────────────────────────────────────── */
SvgDoc *SvgDoc_Create(float w, float h);
void    SvgDoc_Destroy(SvgDoc *d);

/* ── 元素 ─────────────────────────────────────────────────────────── */
SvgElement *SvgDoc_Add(SvgDoc *d, SvgElType type);   /* 置顶, 默认样式 */
void SvgDoc_Remove(SvgDoc *d, int index);
int  SvgDoc_Duplicate(SvgDoc *d, int index);         /* 返回新索引, -1 失败 */
void SvgDoc_MoveZ(SvgDoc *d, int index, int dir);    /* 同父内 dir>0 上移 */

/* ── 树 (M7) ─────────────────────────────────────────────────────── */
SvgElement *SvgDoc_AddChild(SvgDoc *d, int parent, SvgElType type);
int  SvgDoc_RootCount(const SvgDoc *d);
int  SvgDoc_RootAt(const SvgDoc *d, int nth);
int  SvgDoc_ChildCount(const SvgDoc *d, int parent);   /* -1 = 根 */
int  SvgDoc_ChildAt(const SvgDoc *d, int parent, int nth);
int  SvgDoc_ParentOf(const SvgDoc *d, int idx);
int  SvgDoc_DepthOf(const SvgDoc *d, int idx);
uint32_t SvgDoc_UidAt(const SvgDoc *d, int idx);
int  SvgDoc_IndexOfUid(const SvgDoc *d, uint32_t uid);
typedef int (*SvgVisitFn)(SvgDoc *d, int idx, void *user); /* 1 = 停止 */
void SvgDoc_WalkDfs(SvgDoc *d, int visibleOnly, SvgVisitFn fn, void *user);
int  SvgDoc_CommonParent(const SvgDoc *d, const int *idx, int n);

int  SvgDoc_Group(SvgDoc *d, const int *indices, int n); /* 同父; -1 失败 */
int  SvgDoc_Ungroup(SvgDoc *d, int group);               /* 返回子数 */
int  SvgDoc_Reparent(SvgDoc *d, int idx, int newParent, int pos);
void SvgDoc_SetVisible(SvgDoc *d, int idx, int on);
void SvgDoc_SetLocked(SvgDoc *d, int idx, int on);
int  SvgDoc_IsVisible(const SvgDoc *d, int idx);         /* 含祖先 */
int  SvgDoc_IsLocked(const SvgDoc *d, int idx);

/* 2×3 仿射 (a b c d e f): x' = a*x + c*y + e; y' = b*x + d*y + f */
void SvgDoc_SelfMatrix(const SvgDoc *d, int idx, float m[6]);
void SvgDoc_ParentMatrix(const SvgDoc *d, int idx, float m[6]);
void SvgDoc_WorldMatrix(const SvgDoc *d, int idx, float m[6]);
void Svg_DocWorldToLocal(const SvgDoc *d, int idx, float wx, float wy,
	float *lx, float *ly);
void Svg_DocLocalToWorld(const SvgDoc *d, int idx, float lx, float ly,
	float *wx, float *wy);
int  SvgDoc_ResolveStyle(const SvgDoc *d, int idx, SvgStyle *out);
GfxRect SvgDoc_ElementBounds(const SvgDoc *d, int idx);
void SvgDoc_Translate(SvgDoc *d, int idx, float dx, float dy);
void SvgDoc_RotateBy(SvgDoc *d, int idx, float dDeg, float cx, float cy);
void SvgDoc_ScaleBy(SvgDoc *d, int idx, float kx, float ky, float cx, float cy);
/* 几何/样式改动后调用: version++ (绘制与画刷缓存失效) */
void Svg_ElementTouch(SvgElement *e);

void Svg_StyleDefault(SvgStyle *s);
int  Svg_StyleCopy(SvgStyle *dst, const SvgStyle *src);
void Svg_StyleFree(SvgStyle *s);
int  Svg_StyleSetFillStops(SvgPaint *p, const SvgGradStop *stops, int count);

/* ── 路径 ─────────────────────────────────────────────────────────── */
void Svg_PathInit(SvgPath *p);
void Svg_PathFree(SvgPath *p);
int  Svg_PathCopy(SvgPath *dst, const SvgPath *src);
int  Svg_PathAddNode(SvgPath *p, float x, float y, SvgNodeType type);
/* 在 segIndex 段参数 t (0..1) 处插入节点; 返回新节点索引, -1 失败.
 * 直线切分 / 贝塞尔 De Casteljau / 圆弧按同一圆心半径切成两段 A. */
int  Svg_PathInsertNode(SvgPath *p, int segIndex, float t);
void Svg_PathRemoveNode(SvgPath *p, int nodeIndex);
/* 设置段型; 切到 CUBIC 时保留现有 hasIn/hasOut, 切到 ARC 时给圆弧初值 */
void Svg_PathSetSeg(SvgPath *p, int segIndex, SvgSegType type);

/* 节点切线参数 (即"角度 + 力度"): angleDeg 为 out 手柄方向 (度, 元素
 * 局部坐标, 0 = +x, y 向下故顺时针为正), strength 为手柄长度 (世界单位);
 * 仅 in 手柄时取反向. */
void Svg_NodeGetTangent(const SvgNode *n, float *angleDeg, float *strength,
	int *symmetric);
/* 写 out 手柄; symmetric=1 同步镜像 in 并置 SMOOTH, 0 保留 in 并置 CORNER. */
void Svg_NodeSetTangent(SvgNode *n, float angleDeg, float strength,
	int symmetric);

/* ── 圆弧解算与采样 (返回 1 成功; 0 = 退化, 调用方改画直线) ───────── */
int Svg_ArcFromTangent(float x0, float y0, float tx, float ty,
	float x1, float y1, SvgArc *out);
int Svg_ArcFromPoints(float x0, float y0, float xm, float ym,
	float x1, float y1, SvgArc *out);
/* 端点参数化 -> 中心/参数角 (弧度). theta1 为起点参数角, dtheta 带符号 */
int Svg_ArcGeometry(float x0, float y0, float x1, float y1, const SvgArc *a,
	float *cx, float *cy, float *theta1, float *dtheta);
/* 端点参数化 -> 椭圆包围盒 (世界坐标, 未旋转) */
GfxRect Svg_ArcBounds(float x0, float y0, float x1, float y1, const SvgArc *a);

/* ── 几何查询 ─────────────────────────────────────────────────────── */
GfxRect Svg_ElementLocalBounds(const SvgElement *e); /* 未旋转紧包围盒 */
GfxRect Svg_ElementBounds(const SvgElement *e);      /* 含旋转的 AABB */
void    Svg_ElementCenter(const SvgElement *e, float *cx, float *cy);
int     Svg_ElementHit(const SvgElement *e, float wx, float wy, float tol);
int     Svg_DocHit(const SvgDoc *d, float wx, float wy, float tol); /* -1 无 */

/* 世界 <-> 元素局部 (逆旋转) */
void Svg_WorldToLocal(const SvgElement *e, float wx, float wy,
	float *lx, float *ly);
void Svg_LocalToWorld(const SvgElement *e, float lx, float ly,
	float *wx, float *wy);

/* 展平折线 (旋转已应用): 多轮廓; 用于命中/包围盒/预览 */
typedef struct SvgPoly {
	float *pts;         /* x,y 成对 */
	int count, cap;
	int *starts;        /* 每个轮廓的起点 (点索引) */
	int contourCount, contourCap;
	uint8_t *closed;    /* 每个轮廓是否闭合 */
	int closedCap;
} SvgPoly;

void Svg_PolyInit(SvgPoly *po);
void Svg_PolyFree(SvgPoly *po);
/* tol: 世界单位展平容差 (曲线/圆弧自适应) */
int  Svg_ElementPoly(const SvgElement *e, float tol, SvgPoly *out);

/* ── 历史 (快照) ──────────────────────────────────────────────────── */
void Svg_History_Commit(SvgDoc *d);   /* 当前状态入栈, 清空 redo */
int  Svg_History_Undo(SvgDoc *d);
int  Svg_History_Redo(SvgDoc *d);
int  Svg_History_CanUndo(const SvgDoc *d);
int  Svg_History_CanRedo(const SvgDoc *d);

/* ── 序列化 ───────────────────────────────────────────────────────── */
/* 导出 SVG 1.1 UTF-8 文本; 返回 HeapAlloc 字符串, Svg_FreeString 释放 */
char *Svg_Export(const SvgDoc *d);
void  Svg_FreeString(char *s);
/* 解析绝对命令 d (M/L/C/A/Z); 供往返测试与二期导入 */
int   Svg_ImportPathD(const char *d, SvgPath *out);

/* ── 视口 (纯算术, 与 UI 共享) ────────────────────────────────────── */
typedef struct { float scale, tx, ty; } SvgView;

static __inline void Svg_ViewScreen(const SvgView *v, float wx, float wy,
	float *sx, float *sy)
{
	*sx = wx * v->scale + v->tx;
	*sy = wy * v->scale + v->ty;
}

static __inline void Svg_ViewWorld(const SvgView *v, float sx, float sy,
	float *wx, float *wy)
{
	*wx = (sx - v->tx) / v->scale;
	*wy = (sy - v->ty) / v->scale;
}

#endif /* CUI_SVG_DOC_H */
