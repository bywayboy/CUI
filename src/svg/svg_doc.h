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
	SVG_EL_RECT, SVG_EL_ELLIPSE, SVG_EL_LINE, SVG_EL_POLYGON, SVG_EL_PATH
} SvgElType;

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

typedef struct {
	SvgPaint fill, stroke;
	float strokeWidth;              /* 世界单位, 默认 2 */
	float dash[8]; int dashCount;   /* 空 = 实线 */
	int cap, join;
	float opacity;                  /* 0..1 */
} SvgStyle;

typedef struct {
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
	float rot;                      /* 度, 正 = 屏幕顺时针 */
	uint8_t visible, locked;
	int version;                    /* 几何/样式变更 +1 (UI 缓存键) */
} SvgElement;

typedef struct SvgDoc {
	float width, height;            /* artboard = SVG viewBox */
	GfxColor background;            /* 0 = 透明 */
	SvgElement *els;  int count, cap;
	void *history;                  /* 快照环 (内部) */
} SvgDoc;

/* ── 文件生命周期 ─────────────────────────────────────────────────── */
SvgDoc *SvgDoc_Create(float w, float h);
void    SvgDoc_Destroy(SvgDoc *d);

/* ── 元素 ─────────────────────────────────────────────────────────── */
SvgElement *SvgDoc_Add(SvgDoc *d, SvgElType type);   /* 置顶, 默认样式 */
void SvgDoc_Remove(SvgDoc *d, int index);
int  SvgDoc_Duplicate(SvgDoc *d, int index);         /* 返回新索引, -1 失败 */
void SvgDoc_MoveZ(SvgDoc *d, int index, int dir);    /* dir>0 上移, <0 下移 */
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
