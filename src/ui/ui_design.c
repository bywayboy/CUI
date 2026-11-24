/*
 * ui_design.c - CUI 设计器运行时: 模型 / JSON / 布局 / 运行时 / 代码生成
 */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include "ui/ui_design.h"
#include "ui/ui_frame.h"
#include "ui/ui_layout.h"
#include "ui/ui_widgets.h"

void *malloc(size_t n);
void free(void *p);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t cap, const char *fmt, ...);
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);


double atof_local(const char *s);

/* ── 基础 ───────────────────────────────────────────────────────────── */

static void *dn_alloc(size_t n)
{
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n);
}

static void *dn_realloc(void *p, size_t n)
{
	if (p) return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, n);
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n);
}

static void dn_free(void *p)
{
	if (p) HeapFree(GetProcessHeap(), 0, p);
}

void UiDesign_SetText(char *dst, int cap, const char *src)
{
	int i = 0;
	if (!dst || cap <= 0) return;
	if (src) {
		while (src[i] && i < cap - 1) {
			dst[i] = src[i];
			i++;
		}
	}
	dst[i] = 0;
}

void UiDesign_NodeSetText(UiDesignNode *n, const char *text)
{
	if (n) UiDesign_SetText(n->text, UI_DN_TEXT_MAX, text);
}

void UiDesign_NodeSetHint(UiDesignNode *n, const char *hint)
{
	if (n) UiDesign_SetText(n->hint, (int)sizeof(n->hint), hint);
}

void UiDesign_NodeSetOptions(UiDesignNode *n, const char *options)
{
	if (n) UiDesign_SetText(n->options, UI_DN_TEXT_MAX, options);
}

void UiDesign_Init(UiDesignDoc *doc)
{
	if (!doc) return;
	memset(doc, 0, sizeof(*doc));
}

void UiDesign_Free(UiDesignDoc *doc)
{
	int i;
	if (!doc) return;
	for (i = 0; i < doc->nodeCount; i++)
		dn_free(doc->nodes[i].children);
	dn_free(doc->groups);
	dn_free(doc->views);
	dn_free(doc->hosts);
	dn_free(doc->nodes);
	memset(doc, 0, sizeof(*doc));
}

void UiDesign_NormalizeSizeModes(UiDesignDoc *doc)
{
	int i;
	if (!doc) return;
	for (i = 0; i < doc->nodeCount; i++) {
		UiDesignNode *n = &doc->nodes[i];
		if (n->wMode == UI_DN_SIZE_AUTO) {
			if (n->w <= 0.0f) n->w = UiDesign_DefaultW(n->type);
			n->wMode = UI_DN_SIZE_FIXED;
		} else if (n->wMode == UI_DN_SIZE_GROW) {
			n->wMode = UI_DN_SIZE_FILL;
		}
		if (n->hMode == UI_DN_SIZE_AUTO) {
			if (n->h <= 0.0f) n->h = UiDesign_DefaultH(n->type);
			n->hMode = UI_DN_SIZE_FIXED;
		} else if (n->hMode == UI_DN_SIZE_GROW) {
			n->hMode = UI_DN_SIZE_FILL;
		}
	}
}

/* ── 组件类型 ───────────────────────────────────────────────────────── */

static const struct {
	const char *name;
	float w, h;
	int container;
} kTypes[UI_DN_COUNT] = {
	{ "absolute", 320, 240, 1 },
	{ "column", 320, 240, 1 },
	{ "row", 320, 80, 1 },
	{ "grid", 320, 240, 1 },
	{ "panel", 240, 160, 1 },
	{ "label", 120, 20, 0 },
	{ "button", 96, 32, 0 },
	{ "input", 180, 34, 0 },
	{ "checkbox", 120, 24, 0 },
	{ "switch", 80, 28, 0 },
	{ "slider", 180, 28, 0 },
	{ "progress", 160, 10, 0 },
	{ "select", 180, 34, 0 },
	{ "image", 160, 100, 0 },
	{ "separator", 2, 2, 0 },
	{ "tabs", 320, 220, 1 },
	{ "textarea", 240, 120, 0 },
	{ "datepicker", 200, 34, 0 },
	{ "colorsel", 180, 34, 0 },
	{ "qrcode", 120, 120, 0 },
	{ "ringprogress", 160, 160, 0 }
};

const char *UiDesign_TypeName(int type)
{
	if (type < 0 || type >= UI_DN_COUNT) return "?";
	return kTypes[type].name;
}

float UiDesign_DefaultW(int type)
{
	if (type < 0 || type >= UI_DN_COUNT) return 100.0f;
	return kTypes[type].w;
}

float UiDesign_DefaultH(int type)
{
	if (type < 0 || type >= UI_DN_COUNT) return 32.0f;
	return kTypes[type].h;
}

int UiDesign_IsContainer(int type)
{
	return type >= 0 && type < UI_DN_COUNT && kTypes[type].container;
}

int UiDesign_TypeFromName(const char *name)
{
	int i;
	if (!name) return -1;
	for (i = 0; i < UI_DN_COUNT; i++)
		if (strcmp(name, kTypes[i].name) == 0) return i;
	return -1;
}

/* ── 模型增删 ───────────────────────────────────────────────────────── */

static int dn_reserve(void **arr, int *cap, int need, size_t elem)
{
	int nc;
	void *np;
	if (need <= *cap) return 1;
	nc = *cap > 0 ? *cap : 8;
	while (nc < need) nc *= 2;
	np = dn_realloc(*arr, (size_t)nc * elem);
	if (!np) return 0;
	*arr = np;
	*cap = nc;
	return 1;
}

int UiDesign_AddGroup(UiDesignDoc *doc, const char *id, const char *name)
{
	UiDesignGroup *g;
	int idx;
	if (!doc || !dn_reserve((void **)&doc->groups, &doc->groupCap,
		doc->groupCount + 1, sizeof(UiDesignGroup))) return -1;
	idx = doc->groupCount++;
	g = &doc->groups[idx];
	memset(g, 0, sizeof(*g));
	UiDesign_SetText(g->id, UI_DN_ID_MAX, id ? id : "group");
	UiDesign_SetText(g->name, UI_DN_NAME_MAX, name ? name : id ? id : "分组");
	UiDesign_Symbol(g->id, g->symbolPrefix, UI_DN_SYM_MAX);
	return idx;
}

int UiDesign_AddView(UiDesignDoc *doc, const char *id, const char *name)
{
	UiDesignView *v;
	int idx;
	if (!doc || !dn_reserve((void **)&doc->views, &doc->viewCap,
		doc->viewCount + 1, sizeof(UiDesignView))) return -1;
	idx = doc->viewCount++;
	v = &doc->views[idx];
	memset(v, 0, sizeof(*v));
	UiDesign_SetText(v->id, UI_DN_ID_MAX, id ? id : "view");
	UiDesign_SetText(v->name, UI_DN_NAME_MAX, name ? name : id ? id : "视图");
	v->group = -1;
	v->w = 800.0f;
	v->h = 540.0f;
	v->root = -1;
	return idx;
}

int UiDesign_AddHost(UiDesignDoc *doc, const char *id, const char *name,
	int kind, int group)
{
	UiDesignHost *h;
	int idx;
	if (!doc || !dn_reserve((void **)&doc->hosts, &doc->hostCap,
		doc->hostCount + 1, sizeof(UiDesignHost))) return -1;
	idx = doc->hostCount++;
	h = &doc->hosts[idx];
	memset(h, 0, sizeof(*h));
	UiDesign_SetText(h->id, UI_DN_ID_MAX, id ? id : "host");
	UiDesign_SetText(h->name, UI_DN_NAME_MAX, name ? name : id ? id : "宿主");
	UiDesign_SetText(h->title, 128, name ? name : id ? id : "窗口");
	h->kind = kind == UI_DH_DIALOG ? UI_DH_DIALOG
		: (kind == UI_DH_PANEL ? UI_DH_PANEL : UI_DH_WINDOW);
	h->group = group;
	h->w = 800.0f;
	h->h = 540.0f;
	h->minW = 320.0f;
	h->minH = 240.0f;
	h->theme = UI_THEME_LIGHT;
	h->view = -1;
	return idx;
}

UiDesignNode *UiDesign_Node(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->nodeCount) return NULL;
	return &doc->nodes[index];
}

UiDesignView *UiDesign_View(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->viewCount) return NULL;
	return &doc->views[index];
}

UiDesignHost *UiDesign_Host(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->hostCount) return NULL;
	return &doc->hosts[index];
}

UiDesignGroup *UiDesign_Group(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->groupCount) return NULL;
	return &doc->groups[index];
}

int UiDesign_ViewRemove(UiDesignDoc *doc, int view)
{
	int *map;
	int i, j, keep = 0;
	if (!doc || view < 0 || view >= doc->viewCount) return 0;
	map = (int *)dn_alloc((size_t)doc->nodeCount * sizeof(int));
	if (!map) return 0;
	for (i = 0; i < doc->nodeCount; i++)
		map[i] = doc->nodes[i].owner == view ? -1 : keep++;
	/* 前移保留节点: newIndex 永远 <= oldIndex, 原地搬移安全. */
	j = 0;
	for (i = 0; i < doc->nodeCount; i++) {
		UiDesignNode *n = &doc->nodes[i];
		int k, w;
		if (map[i] < 0) {
			dn_free(n->children);
			n->children = NULL;
			continue;
		}
		if (j != i) doc->nodes[j] = *n;
		n = &doc->nodes[j];
		if (n->parent >= 0) {
			if (n->parent >= doc->nodeCount || map[n->parent] < 0)
				n->parent = -1;
			else n->parent = map[n->parent];
		}
		w = 0;
		for (k = 0; k < n->childCount; k++) {
			int c = n->children[k];
			if (c >= 0 && c < doc->nodeCount && map[c] >= 0)
				n->children[w++] = map[c];
		}
		n->childCount = w;
		if (n->owner > view) n->owner--;
		j++;
	}
	for (i = 0; i < doc->viewCount; i++) {
		if (doc->views[i].root >= 0 && doc->views[i].root < doc->nodeCount)
			doc->views[i].root = map[doc->views[i].root];
	}
	doc->nodeCount = j;
	for (i = view + 1; i < doc->viewCount; i++)
		doc->views[i - 1] = doc->views[i];
	doc->viewCount--;
	for (i = 0; i < doc->hostCount; i++) {
		if (doc->hosts[i].view == view) doc->hosts[i].view = -1;
		else if (doc->hosts[i].view > view) doc->hosts[i].view--;
	}
	dn_free(map);
	doc->dirty = 1;
	return 1;
}

void UiDesign_NodeDetach(UiDesignDoc *doc, int index)
{
	UiDesignNode *n;
	int i, j;
	if (!doc || index < 0 || index >= doc->nodeCount) return;
	n = &doc->nodes[index];
	if (!n || n->parent < 0) return;
	{
		UiDesignNode *p = &doc->nodes[n->parent];
		for (i = 0; i < p->childCount; i++) {
			if (p->children[i] == index) {
				for (j = i + 1; j < p->childCount; j++)
					p->children[j - 1] = p->children[j];
				p->childCount--;
				break;
			}
		}
	}
	n->parent = -1;
}

void UiDesign_NodeAddChild(UiDesignDoc *doc, int parent, int child)
{
	UiDesignNode *p;
	if (!doc || parent < 0 || child < 0 || parent >= doc->nodeCount ||
	    child >= doc->nodeCount) return;
	UiDesign_NodeDetach(doc, child);
	p = &doc->nodes[parent];
	if (!dn_reserve((void **)&p->children, &p->childCap,
		p->childCount + 1, sizeof(int))) return;
	p->children[p->childCount++] = child;
	doc->nodes[child].parent = parent;
}

int UiDesign_AddNode(UiDesignDoc *doc, int view, int type, int parent,
	const char *id, const char *name)
{
	UiDesignNode *n;
	int idx;
	if (!doc || !dn_reserve((void **)&doc->nodes, &doc->nodeCap,
		doc->nodeCount + 1, sizeof(UiDesignNode))) return -1;
	if (type < 0 || type >= UI_DN_COUNT) type = UI_DN_LABEL;
	idx = doc->nodeCount++;
	n = &doc->nodes[idx];
	memset(n, 0, sizeof(*n));
	n->type = type;
	n->owner = view;
	n->parent = -1;
	n->visible = 1;
	n->enabled = 1;
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FIXED;
	n->w = UiDesign_DefaultW(type);
	n->h = UiDesign_DefaultH(type);
	n->align = UI_DN_ALIGN_START;
	n->textAlign = UI_DN_ALIGN_START;
	n->textVAlign = UI_DN_VALIGN_CENTER;
	n->gap = 8.0f;
	n->radius = type == UI_DN_IMAGE ? 6.0f : 8.0f;
	n->gridRows = 1;
	n->gridCols = 1;
	n->gridRow = -1;
	n->gridCol = -1;
	n->rowSpan = 1;
	n->colSpan = 1;
	n->value = 0.5f;
	n->selected = type == UI_DN_TABS ? -1 : 0;
	n->color = type == UI_DN_QRCODE ? 0xFF000000u :
		(type == UI_DN_RINGPROGRESS ? 0xFF4F6FF3u : 0xFF3B82F6u);
	n->bgColor = 0xFFFFFFFFu;
	n->ringMax = 100.0f;
	n->ringWidth = 14.0f;
	if (type == UI_DN_RINGPROGRESS) n->checked = 1;
	n->ecc = UI_QR_ECC_M;
	n->logoRatio = UI_QR_LOGO_RATIO_DEFAULT;
	n->qrPad = UI_QR_PAD_DEFAULT;
	UiDesign_SetText(n->id, UI_DN_ID_MAX, id ? id : "node");
	UiDesign_SetText(n->name, UI_DN_NAME_MAX, name ? name : id ? id : "组件");
	if (parent >= 0) UiDesign_NodeAddChild(doc, parent, idx);
	return idx;
}

static void dn_remove_subtree(UiDesignDoc *doc, int index)
{
	int i;
	UiDesignNode *n = UiDesign_Node(doc, index);
	if (!n) return;
	for (i = n->childCount - 1; i >= 0; i--)
		dn_remove_subtree(doc, n->children[i]);
	UiDesign_NodeDetach(doc, index);
}

static UiDesignNode *dn_tabs_node(UiDesignDoc *doc, int tabs)
{
	UiDesignNode *n;
	if (!doc || tabs < 0 || tabs >= doc->nodeCount) return NULL;
	n = &doc->nodes[tabs];
	return n->type == UI_DN_TABS ? n : NULL;
}

static const UiDesignNode *dn_tabs_node_c(const UiDesignDoc *doc, int tabs)
{
	const UiDesignNode *n;
	if (!doc || tabs < 0 || tabs >= doc->nodeCount) return NULL;
	n = &doc->nodes[tabs];
	return n->type == UI_DN_TABS ? n : NULL;
}

static int dn_tabs_index_of_node(const UiDesignDoc *doc, int tabs, int node)
{
	const UiDesignNode *n = dn_tabs_node_c(doc, tabs);
	int i;
	if (!n || node < 0) return -1;
	for (i = 0; i < n->childCount; i++)
		if (n->children[i] == node) return i;
	return -1;
}

static int dn_tabs_selected_node(const UiDesignDoc *doc, int tabs)
{
	const UiDesignNode *n = dn_tabs_node_c(doc, tabs);
	int sel;
	if (!n || n->childCount <= 0) return -1;
	sel = n->selected;
	if (sel < 0 || sel >= n->childCount) sel = 0;
	return n->children[sel];
}

static void dn_tabs_select_node(UiDesignDoc *doc, int tabs, int node)
{
	UiDesignNode *n = dn_tabs_node(doc, tabs);
	int idx;
	if (!n) return;
	idx = dn_tabs_index_of_node(doc, tabs, node);
	if (idx >= 0) n->selected = idx;
	else UiDesign_TabsNormalize(doc, tabs);
}

int UiDesign_TabsPageCount(const UiDesignDoc *doc, int tabs)
{
	const UiDesignNode *n = dn_tabs_node_c(doc, tabs);
	return n ? n->childCount : -1;
}

int UiDesign_TabsPageAt(const UiDesignDoc *doc, int tabs, int page)
{
	const UiDesignNode *n = dn_tabs_node_c(doc, tabs);
	if (!n || page < 0 || page >= n->childCount) return -1;
	return n->children[page];
}

int UiDesign_TabsSelectedPage(const UiDesignDoc *doc, int tabs)
{
	const UiDesignNode *n = dn_tabs_node_c(doc, tabs);
	if (!n || n->childCount <= 0) return -1;
	if (n->selected < 0 || n->selected >= n->childCount) return 0;
	return n->selected;
}

int UiDesign_TabsSetSelectedPage(UiDesignDoc *doc, int tabs, int page)
{
	UiDesignNode *n = dn_tabs_node(doc, tabs);
	if (!n) return -1;
	if (n->childCount <= 0) {
		n->selected = -1;
		return -1;
	}
	if (page < 0) page = 0;
	if (page >= n->childCount) page = n->childCount - 1;
	n->selected = page;
	return page;
}

void UiDesign_TabsNormalize(UiDesignDoc *doc, int tabs)
{
	UiDesignNode *n = dn_tabs_node(doc, tabs);
	if (!n) return;
	if (n->childCount <= 0) {
		n->selected = -1;
		return;
	}
	if (n->selected < 0) n->selected = 0;
	else if (n->selected >= n->childCount)
		n->selected = n->childCount - 1;
}

int UiDesign_TabsInsertPage(UiDesignDoc *doc, int tabs, int pos,
	const char *id, const char *name)
{
	UiDesignNode *n = dn_tabs_node(doc, tabs);
	UiDesignNode *pg;
	int idx, count;
	if (!n || n->childCount >= UI_DN_TABS_MAX_PAGES) return -1;
	count = n->childCount;
	if (pos < 0 || pos > count) pos = count;
	idx = UiDesign_AddNode(doc, n->owner, UI_DN_COLUMN, tabs,
		id ? id : "page", name ? name : "页");
	if (idx < 0) return -1;
	pg = &doc->nodes[idx];
	pg->wMode = UI_DN_SIZE_FILL;
	pg->hMode = UI_DN_SIZE_FILL;
	pg->padding[0] = pg->padding[1] = 12.0f;
	pg->padding[2] = pg->padding[3] = 12.0f;
	pg->gap = 8.0f;
	if (pos < count)
		UiDesign_NodeMove(doc, idx, tabs, pos);
	n = &doc->nodes[tabs];
	n->selected = dn_tabs_index_of_node(doc, tabs, idx);
	if (n->selected < 0) UiDesign_TabsNormalize(doc, tabs);
	return idx;
}

int UiDesign_TabsRemovePage(UiDesignDoc *doc, int tabs, int page)
{
	UiDesignNode *n = dn_tabs_node(doc, tabs);
	int selNode, pageNode;
	if (!n || page < 0 || page >= n->childCount) return 0;
	selNode = dn_tabs_selected_node(doc, tabs);
	pageNode = n->children[page];
	dn_remove_subtree(doc, pageNode);
	n = &doc->nodes[tabs];
	if (n->childCount <= 0) {
		n->selected = -1;
		return 1;
	}
	if (selNode == pageNode) {
		n->selected = page < n->childCount ? page : n->childCount - 1;
		return 1;
	}
	dn_tabs_select_node(doc, tabs, selNode);
	return 1;
}

int UiDesign_TabsMovePage(UiDesignDoc *doc, int tabs, int from, int to)
{
	UiDesignNode *n = dn_tabs_node(doc, tabs);
	int node;
	if (!n || from < 0 || from >= n->childCount) return 0;
	if (to < 0) to = 0;
	if (to >= n->childCount) to = n->childCount - 1;
	if (from == to) return 1;
	node = n->children[from];
	return UiDesign_NodeMove(doc, node, tabs, to);
}

int UiDesign_NodeRemove(UiDesignDoc *doc, int index)
{
	int p;
	if (!doc || index < 0 || index >= doc->nodeCount) return 0;
	p = doc->nodes[index].parent;
	if (p < 0) return 0;    /* 根节点不删 */
	if (p < doc->nodeCount && doc->nodes[p].type == UI_DN_TABS) {
		int page = dn_tabs_index_of_node(doc, p, index);
		if (page >= 0) return UiDesign_TabsRemovePage(doc, p, page);
	}
	dn_remove_subtree(doc, index);
	return 1;
}

int UiDesign_NodeMove(UiDesignDoc *doc, int index, int parent, int pos)
{
	UiDesignNode *p;
	int i, j;
	int oldParent, oldSelNode = -1, newSelNode = -1;
	if (!doc || index < 0 || index >= doc->nodeCount ||
	    parent < 0 || parent >= doc->nodeCount) return 0;
	if (index == parent) return 0;
	/* 禁止移到自己的子树 */
	for (i = parent; i >= 0; i = doc->nodes[i].parent)
		if (i == index) return 0;
	oldParent = doc->nodes[index].parent;
	if (oldParent >= 0 && doc->nodes[oldParent].type == UI_DN_TABS)
		oldSelNode = dn_tabs_selected_node(doc, oldParent);
	if (doc->nodes[parent].type == UI_DN_TABS)
		newSelNode = dn_tabs_selected_node(doc, parent);
	UiDesign_NodeDetach(doc, index);
	p = &doc->nodes[parent];
	if (!dn_reserve((void **)&p->children, &p->childCap,
		p->childCount + 1, sizeof(int))) return 0;
	if (pos < 0 || pos > p->childCount) pos = p->childCount;
	for (j = p->childCount; j > pos; j--) p->children[j] = p->children[j - 1];
	p->children[pos] = index;
	p->childCount++;
	doc->nodes[index].parent = parent;
	if (oldParent >= 0 && oldParent != parent &&
	    doc->nodes[oldParent].type == UI_DN_TABS)
		dn_tabs_select_node(doc, oldParent, oldSelNode);
	if (doc->nodes[parent].type == UI_DN_TABS)
		dn_tabs_select_node(doc, parent,
			oldParent == parent ? oldSelNode : newSelNode);
	return 1;
}

int UiDesign_NodeMoveUp(UiDesignDoc *doc, int index)
{
	UiDesignNode *n;
	int i, p, selNode = -1;
	if (!doc || index < 0 || index >= doc->nodeCount) return 0;
	n = &doc->nodes[index];
	p = n->parent;
	if (p < 0) return 0;
	for (i = 0; i < doc->nodes[p].childCount; i++)
		if (doc->nodes[p].children[i] == index) break;
	if (i <= 0 || i >= doc->nodes[p].childCount) return 0;
	if (doc->nodes[p].type == UI_DN_TABS)
		selNode = dn_tabs_selected_node(doc, p);
	{
		int t = doc->nodes[p].children[i - 1];
		doc->nodes[p].children[i - 1] = index;
		doc->nodes[p].children[i] = t;
	}
	if (doc->nodes[p].type == UI_DN_TABS)
		dn_tabs_select_node(doc, p, selNode);
	return 1;
}

int UiDesign_NodeMoveDown(UiDesignDoc *doc, int index)
{
	UiDesignNode *n;
	int i, p, selNode = -1;
	if (!doc || index < 0 || index >= doc->nodeCount) return 0;
	n = &doc->nodes[index];
	p = n->parent;
	if (p < 0) return 0;
	for (i = 0; i < doc->nodes[p].childCount; i++)
		if (doc->nodes[p].children[i] == index) break;
	if (i < 0 || i + 1 >= doc->nodes[p].childCount) return 0;
	if (doc->nodes[p].type == UI_DN_TABS)
		selNode = dn_tabs_selected_node(doc, p);
	{
		int t = doc->nodes[p].children[i + 1];
		doc->nodes[p].children[i + 1] = index;
		doc->nodes[p].children[i] = t;
	}
	if (doc->nodes[p].type == UI_DN_TABS)
		dn_tabs_select_node(doc, p, selNode);
	return 1;
}

static int dn_clone_node(UiDesignDoc *doc, int src, int parent)
{
	UiDesignNode *s = UiDesign_Node(doc, src), *n;
	int idx, i;
	char id[UI_DN_ID_MAX];
	if (!s) return -1;
	sprintf(id, "%.48s_copy", s->id);
	idx = UiDesign_AddNode(doc, s->owner, s->type, parent, id, s->name);
	n = UiDesign_Node(doc, idx);
	if (!n) return -1;
	*n = *s;
	n->parent = parent;
	n->children = NULL;
	n->childCount = n->childCap = 0;
	for (i = 0; i < s->childCount; i++)
		dn_clone_node(doc, s->children[i], idx);
	return idx;
}

int UiDesign_NodeDuplicate(UiDesignDoc *doc, int index)
{
	UiDesignNode *s;
	int p, pos, i, dup;
	if (!doc || index < 0 || index >= doc->nodeCount) return -1;
	s = &doc->nodes[index];
	p = s->parent;
	if (p < 0) return -1;
	for (i = 0; i < doc->nodes[p].childCount; i++)
		if (doc->nodes[p].children[i] == index) break;
	pos = i + 1;
	dup = dn_clone_node(doc, index, -1);
	if (dup < 0) return -1;
	UiDesign_NodeMove(doc, dup, p, pos);
	return dup;
}

int UiDesign_FindNode(const UiDesignDoc *doc, int view, const char *id)
{
	int i;
	if (!doc || !id) return -1;
	for (i = 0; i < doc->nodeCount; i++)
		if (doc->nodes[i].owner == view && strcmp(doc->nodes[i].id, id) == 0)
			return i;
	return -1;
}

int UiDesign_FindView(const UiDesignDoc *doc, const char *id)
{
	int i;
	if (!doc || !id) return -1;
	for (i = 0; i < doc->viewCount; i++)
		if (strcmp(doc->views[i].id, id) == 0) return i;
	return -1;
}

int UiDesign_FindHost(const UiDesignDoc *doc, const char *id)
{
	int i;
	if (!doc || !id) return -1;
	for (i = 0; i < doc->hostCount; i++)
		if (strcmp(doc->hosts[i].id, id) == 0) return i;
	return -1;
}

int UiDesign_FindGroup(const UiDesignDoc *doc, const char *id)
{
	int i;
	if (!doc || !id) return -1;
	for (i = 0; i < doc->groupCount; i++)
		if (strcmp(doc->groups[i].id, id) == 0) return i;
	return -1;
}

int UiDesign_DefaultDoc(UiDesignDoc *doc)
{
	int g, v, hd, root, label, col;
	UiDesignNode *n;
	UiDesignView *view;
	UiDesignHost *host;
	UiDesignGroup *grp;
	if (!doc) return 0;
	UiDesign_Init(doc);
	UiDesign_SetText(doc->name, UI_DN_NAME_MAX, "未命名工程");
	g = UiDesign_AddGroup(doc, "main", "主分组");
	v = UiDesign_AddView(doc, "main", "主视图");
	hd = UiDesign_AddHost(doc, "main", "主窗口", UI_DH_WINDOW, g);
	if (g < 0 || v < 0 || hd < 0) return 0;
	grp = UiDesign_Group(doc, g);
	UiDesign_SetText(grp->outputC, UI_DN_PATH_MAX, "main.ui.c");
	UiDesign_SetText(grp->outputH, UI_DN_PATH_MAX, "main.ui.h");
	UiDesign_SetText(grp->symbolPrefix, UI_DN_SYM_MAX, "Main");
	view = UiDesign_View(doc, v);
	view->group = g;
	view->root = UiDesign_AddNode(doc, v, UI_DN_COLUMN, -1, "root", "根容器");
	root = view->root;
	UiDesign_Host(doc, hd)->view = v;
	n = UiDesign_Node(doc, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 16.0f;
	n->gap = 8.0f;
	label = UiDesign_AddNode(doc, v, UI_DN_LABEL, root, "title", "标题");
	n = UiDesign_Node(doc, label);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FIXED;
	n->h = 28.0f;
	UiDesign_SetText(n->text, UI_DN_TEXT_MAX, "新建窗口");
	col = UiDesign_AddNode(doc, v, UI_DN_BUTTON, root, "ok", "确定按钮");
	n = UiDesign_Node(doc, col);
	n->wMode = UI_DN_SIZE_FIXED;
	n->hMode = UI_DN_SIZE_FIXED;
	n->w = 96.0f;
	n->h = 32.0f;
	n->variant = 1;
	n->enabled = 1;
	UiDesign_SetText(n->text, UI_DN_TEXT_MAX, "确定");
	return 1;
}

/* ── JSON 写 ────────────────────────────────────────────────────────── */

typedef struct UiDStr {
	char *p;
	int n, cap;
} UiDStr;

static void ds_init(UiDStr *s)
{
	memset(s, 0, sizeof(*s));
}

static void ds_free(UiDStr *s)
{
	dn_free(s->p);
	memset(s, 0, sizeof(*s));
}

static int ds_reserve(UiDStr *s, int need)
{
	int nc;
	char *np;
	if (need <= s->cap) return 1;
	nc = s->cap > 0 ? s->cap : 512;
	while (nc < need) nc *= 2;
	np = (char *)dn_realloc(s->p, (size_t)nc);
	if (!np) return 0;
	s->p = np;
	s->cap = nc;
	return 1;
}

static void ds_add(UiDStr *s, const char *text)
{
	int n;
	if (!s || !text) return;
	n = (int)strlen(text);
	if (!ds_reserve(s, s->n + n + 1)) return;
	memcpy(s->p + s->n, text, (size_t)n);
	s->n += n;
	s->p[s->n] = 0;
}

static void ds_addf(UiDStr *s, const char *fmt, ...)
{
	char b[256];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if (n >= (int)sizeof(b)) n = (int)sizeof(b) - 1;
	if (!ds_reserve(s, s->n + n + 1)) return;
	memcpy(s->p + s->n, b, (size_t)n);
	s->n += n;
	s->p[s->n] = 0;
}

static void ds_json_str(UiDStr *s, const char *text)
{
	int i;
	ds_add(s, "\"");
	if (text) {
		for (i = 0; text[i]; i++) {
			unsigned char c = (unsigned char)text[i];
			if (c == '"' || c == '\\') {
				char e[3] = { '\\', (char)c, 0 };
				ds_add(s, e);
			} else if (c == '\n') ds_add(s, "\\n");
			else if (c == '\r') ds_add(s, "\\r");
			else if (c == '\t') ds_add(s, "\\t");
			else if (c < 0x20) ds_addf(s, "\\u%04x", c);
			else {
				char e[2] = { (char)c, 0 };
				ds_add(s, e);
			}
		}
	}
	ds_add(s, "\"");
}

static void ds_num(UiDStr *s, float v)
{
	char b[40];
	sprintf(b, "%.3f", v);
	ds_add(s, b);
}

static void ds_color(UiDStr *s, unsigned c)
{
	ds_addf(s, "\"#%08X\"", c);
}

static void ds_node(UiDStr *s, const UiDesignDoc *doc, int index);

static void ds_node(UiDStr *s, const UiDesignDoc *doc, int index)
{
	const UiDesignNode *n = index >= 0 && index < doc->nodeCount
		? &doc->nodes[index] : NULL;
	int i;
	if (!n) {
		ds_add(s, "null");
		return;
	}
	ds_add(s, "{\"id\":");
	ds_json_str(s, n->id);
	ds_add(s, ",\"name\":");
	ds_json_str(s, n->name);
	ds_add(s, ",\"type\":");
	ds_json_str(s, UiDesign_TypeName(n->type));
	ds_addf(s, ",\"visible\":%d,\"locked\":%d", n->visible, n->locked);
	ds_add(s, ",\"layout\":{");
	ds_addf(s, "\"wMode\":%d,\"hMode\":%d,\"w\":", n->wMode, n->hMode);
	ds_num(s, n->w);
	ds_add(s, ",\"h\":"); ds_num(s, n->h);
	ds_add(s, ",\"minW\":"); ds_num(s, n->minW);
	ds_add(s, ",\"minH\":"); ds_num(s, n->minH);
	ds_add(s, ",\"maxW\":"); ds_num(s, n->maxW);
	ds_add(s, ",\"maxH\":"); ds_num(s, n->maxH);
	ds_add(s, ",\"grow\":"); ds_num(s, n->grow);
	ds_addf(s, ",\"align\":%d", n->align);
	ds_addf(s, ",\"textAlign\":%d,\"textVAlign\":%d", n->textAlign, n->textVAlign);
	ds_add(s, ",\"margin\":[");
	for (i = 0; i < 4; i++) { if (i) ds_add(s, ","); ds_num(s, n->margin[i]); }
	ds_add(s, "],\"padding\":[");
	for (i = 0; i < 4; i++) { if (i) ds_add(s, ","); ds_num(s, n->padding[i]); }
	ds_add(s, "],\"gap\":"); ds_num(s, n->gap);
	if (n->type == UI_DN_PANEL || n->type == UI_DN_IMAGE) {
		ds_add(s, ",\"radius\":"); ds_num(s, n->radius);
	}
	ds_add(s, ",\"x\":"); ds_num(s, n->x);
	ds_add(s, ",\"y\":"); ds_num(s, n->y);
	ds_addf(s, ",\"anchors\":%u", n->anchors);
	ds_addf(s, ",\"gridRows\":%d,\"gridCols\":%d", n->gridRows, n->gridCols);
	ds_add(s, ",\"gridRowGap\":"); ds_num(s, n->gridRowGap);
	ds_add(s, ",\"gridColGap\":"); ds_num(s, n->gridColGap);
	ds_addf(s, ",\"gridRow\":%d,\"gridCol\":%d,\"rowSpan\":%d,\"colSpan\":%d",
		n->gridRow, n->gridCol, n->rowSpan, n->colSpan);
	ds_add(s, ",\"gridColTracks\":");
	ds_json_str(s, n->gridColTracks);
	ds_add(s, "},\"props\":{\"text\":");
	ds_json_str(s, n->text);
	ds_add(s, ",\"hint\":");
	ds_json_str(s, n->hint);
	ds_add(s, ",\"options\":");
	ds_json_str(s, n->options);
	ds_add(s, ",\"value\":"); ds_num(s, n->value);
	ds_addf(s, ",\"checked\":%d,\"enabled\":%d,\"selected\":%d,\"variant\":%d",
		n->checked, n->enabled, n->selected, n->variant);
	ds_addf(s, ",\"mode\":%d,\"readonly\":%d,\"color\":", n->mode, n->readonly);
	ds_color(s, n->color);
	if (n->type == UI_DN_QRCODE) {
		ds_addf(s, ",\"ecc\":%d,\"bgColor\":", n->ecc);
		ds_color(s, n->bgColor);
		ds_add(s, ",\"src\":");
		ds_json_str(s, n->logoSrc);
		ds_add(s, ",\"logoRatio\":");
		ds_num(s, n->logoRatio);
		ds_add(s, ",\"qrPad\":");
		ds_num(s, n->qrPad);
	}
	if (n->type == UI_DN_RINGPROGRESS) {
		ds_add(s, ",\"ringMax\":");
		ds_num(s, n->ringMax);
		ds_add(s, ",\"ringWidth\":");
		ds_num(s, n->ringWidth);
	}
	ds_add(s, "},\"onClick\":");
	ds_json_str(s, n->onClick);
	ds_add(s, ",\"onChange\":");
	ds_json_str(s, n->onChange);
	ds_add(s, ",\"children\":[");
	for (i = 0; i < n->childCount; i++) {
		if (i) ds_add(s, ",");
		ds_node(s, doc, n->children[i]);
	}
	ds_add(s, "]}");
}

char *UiDesign_ToJson(const UiDesignDoc *doc)
{
	UiDStr s;
	int i;
	if (!doc) return NULL;
	ds_init(&s);
	ds_add(&s, "{\n  \"format\":\"cui-design\",\n  \"version\":2,\n  \"name\":");
	ds_json_str(&s, doc->name);
	ds_add(&s, ",\n  \"groups\":[");
	for (i = 0; i < doc->groupCount; i++) {
		const UiDesignGroup *g = &doc->groups[i];
		if (i) ds_add(&s, ",");
		ds_add(&s, "\n    {\"id\":");
		ds_json_str(&s, g->id);
		ds_add(&s, ",\"name\":");
		ds_json_str(&s, g->name);
		ds_add(&s, ",\"outputC\":");
		ds_json_str(&s, g->outputC);
		ds_add(&s, ",\"outputH\":");
		ds_json_str(&s, g->outputH);
		ds_add(&s, ",\"symbolPrefix\":");
		ds_json_str(&s, g->symbolPrefix);
		ds_add(&s, "}");
	}
	ds_add(&s, "\n  ],\n  \"views\":[");
	for (i = 0; i < doc->viewCount; i++) {
		const UiDesignView *v = &doc->views[i];
		if (i) ds_add(&s, ",");
		ds_add(&s, "\n    {\"id\":");
		ds_json_str(&s, v->id);
		ds_add(&s, ",\"name\":");
		ds_json_str(&s, v->name);
		ds_add(&s, ",\"width\":"); ds_num(&s, v->w);
		ds_add(&s, ",\"height\":"); ds_num(&s, v->h);
		ds_add(&s, ",\"group\":");
		if (v->group >= 0 && v->group < doc->groupCount)
			ds_json_str(&s, doc->groups[v->group].id);
		else ds_add(&s, "null");
		ds_add(&s, ",\"root\":");
		if (v->root >= 0) ds_node(&s, doc, v->root);
		else ds_add(&s, "null");
		ds_add(&s, "}");
	}
	ds_add(&s, "\n  ],\n  \"hosts\":[");
	for (i = 0; i < doc->hostCount; i++) {
		const UiDesignHost *w = &doc->hosts[i];
		if (i) ds_add(&s, ",");
		ds_add(&s, "\n    {\"id\":");
		ds_json_str(&s, w->id);
		ds_add(&s, ",\"name\":");
		ds_json_str(&s, w->name);
		ds_add(&s, ",\"title\":");
		ds_json_str(&s, w->title);
		ds_addf(&s, ",\"kind\":%d,\"group\":", w->kind);
		if (w->group >= 0 && w->group < doc->groupCount)
			ds_json_str(&s, doc->groups[w->group].id);
		else ds_add(&s, "null");
		ds_add(&s, ",\"width\":"); ds_num(&s, w->w);
		ds_add(&s, ",\"height\":"); ds_num(&s, w->h);
		ds_add(&s, ",\"minWidth\":"); ds_num(&s, w->minW);
		ds_add(&s, ",\"minHeight\":"); ds_num(&s, w->minH);
		ds_addf(&s, ",\"theme\":%d,\"view\":", w->theme);
		if (w->view >= 0 && w->view < doc->viewCount)
			ds_json_str(&s, doc->views[w->view].id);
		else ds_add(&s, "null");
		ds_add(&s, "}");
	}
	ds_add(&s, "\n  ]\n}\n");
	return s.p;
}

/* ── JSON 解析 ──────────────────────────────────────────────────────── */

typedef struct JP { const char *p, *end; } JP;

static void jp_ws(JP *j)
{
	while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' ||
		*j->p == '\r' || *j->p == '\n')) j->p++;
}

static int jp_ch(JP *j, char c)
{
	jp_ws(j);
	if (j->p < j->end && *j->p == c) { j->p++; return 1; }
	return 0;
}

static int jp_string(JP *j, char *out, int cap)
{
	int n = 0;
	jp_ws(j);
	if (j->p >= j->end || *j->p != '"') return 0;
	j->p++;
	while (j->p < j->end) {
		char c = *j->p++;
		if (c == '"') { if (out && cap > 0) out[n < cap ? n : cap - 1] = 0; return 1; }
		if (c == '\\') {
			if (j->p >= j->end) return 0;
			c = *j->p++;
			if (c == 'n') c = '\n';
			else if (c == 'r') c = '\r';
			else if (c == 't') c = '\t';
			else if (c == 'b') c = '\b';
			else if (c == 'f') c = '\f';
			else if (c == 'u') {
				unsigned v = 0;
				int k;
				for (k = 0; k < 4 && j->p < j->end; k++) {
					char h = *j->p++;
					v <<= 4;
					if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
					else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
					else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
				}
				if (v < 0x80) c = (char)v;
				else {
					if (out && n < cap - 1) out[n++] = '?';
					c = 0;
				}
			}
		}
		if (c && out && n < cap - 1) out[n++] = c;
	}
	return 0;
}

static int jp_number(JP *j, float *v)
{
	char b[64];
	int n = 0;
	jp_ws(j);
	while (j->p < j->end && n < (int)sizeof(b) - 1) {
		char c = *j->p;
		if ((c >= '0' && c <= '9') || c == '-' || c == '+' ||
		    c == '.' || c == 'e' || c == 'E') {
			b[n++] = c;
			j->p++;
		} else break;
	}
	b[n] = 0;
	if (!n) return 0;
	*v = (float)atof_local(b);
	return 1;
}

static int jp_int(JP *j, int *v)
{
	float f;
	if (!jp_number(j, &f)) return 0;
	*v = (int)(f >= 0 ? f + 0.5f : f - 0.5f);
	return 1;
}

static int jp_bool(JP *j, int *v)
{
	jp_ws(j);
	if (j->end - j->p >= 4 && memcmp(j->p, "true", 4) == 0) {
		j->p += 4; *v = 1; return 1;
	}
	if (j->end - j->p >= 5 && memcmp(j->p, "false", 5) == 0) {
		j->p += 5; *v = 0; return 1;
	}
	if (j->end - j->p >= 4 && memcmp(j->p, "null", 4) == 0) {
		j->p += 4; *v = 0; return 1;
	}
	if (j->p < j->end && (*j->p == '-' || (*j->p >= '0' && *j->p <= '9'))) {
		int n;
		if (!jp_int(j, &n)) return 0;
		*v = n != 0;
		return 1;
	}
	return 0;
}

static int jp_hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* "#RRGGBB" 补 FF alpha; "#RRGGBBAA" 原样. 失败不改 *out. */
static int jp_color(JP *j, unsigned *out)
{
	char b[16];
	int i, n = 0;
	unsigned v = 0;
	jp_ws(j);
	if (j->p >= j->end || *j->p != '"') return 0;
	j->p++;
	while (j->p < j->end && *j->p != '"' && n < (int)sizeof(b) - 1)
		b[n++] = *j->p++;
	b[n] = 0;
	if (j->p >= j->end || *j->p != '"') return 0;
	j->p++;
	if (b[0] != '#' || (n != 7 && n != 9)) return 0;
	for (i = 1; i < n; i++) {
		int h = jp_hexval(b[i]);
		if (h < 0) return 0;
		v = (v << 4) | (unsigned)h;
	}
	if (n == 7) v |= 0xFF000000u;
	*out = v;
	return 1;
}

static int jp_skip(JP *j)
{
	char b[8];
	float f;
	int x;
	jp_ws(j);
	if (j->p >= j->end) return 0;
	if (*j->p == '"') return jp_string(j, NULL, 0);
	if (*j->p == '{') {
		j->p++;
		jp_ws(j);
		if (jp_ch(j, '}')) return 1;
		for (;;) {
			if (!jp_string(j, b, sizeof(b)) || !jp_ch(j, ':')) return 0;
			if (!jp_skip(j)) return 0;
			if (jp_ch(j, '}')) return 1;
			if (!jp_ch(j, ',')) return 0;
		}
	}
	if (*j->p == '[') {
		j->p++;
		jp_ws(j);
		if (jp_ch(j, ']')) return 1;
		for (;;) {
			if (!jp_skip(j)) return 0;
			if (jp_ch(j, ']')) return 1;
			if (!jp_ch(j, ',')) return 0;
		}
	}
	if (jp_bool(j, &x)) return 1;
	return jp_number(j, &f);
}

static int jp_key(JP *j, char *out, int cap)
{
	if (!jp_string(j, out, cap)) return 0;
	return jp_ch(j, ':');
}

static int jp_array_float(JP *j, float *out, int count)
{
	int i;
	if (!jp_ch(j, '[')) return 0;
	for (i = 0; i < count; i++) {
		if (i && !jp_ch(j, ',')) return 0;
		if (!jp_number(j, &out[i])) return 0;
	}
	return jp_ch(j, ']');
}

static int jp_node(JP *j, UiDesignDoc *doc, int win, int parent, int *outIndex);

static int jp_node(JP *j, UiDesignDoc *doc, int win, int parent, int *outIndex)
{
	char key[64], val[UI_DN_TEXT_MAX];
	int idx, type = UI_DN_LABEL;
	int gotType = 0, gotRadius = 0, gotColor = 0, gotBg = 0, gotEcc = 0;
	int gotLogo = 0, gotLogoRatio = 0, gotQrPad = 0;
	int gotChecked = 0, gotRingMax = 0, gotRingWidth = 0;
	if (!jp_ch(j, '{')) return 0;
	idx = UiDesign_AddNode(doc, win, type, parent, "node", "组件");
	if (idx < 0) return 0;
	*outIndex = idx;
	for (;;) {
		jp_ws(j);
		if (jp_ch(j, '}')) break;
		if (!jp_key(j, key, sizeof(key))) return 0;
		if (strcmp(key, "id") == 0) {
			jp_string(j, val, sizeof(val));
			UiDesign_SetText(doc->nodes[idx].id, UI_DN_ID_MAX, val);
		} else if (strcmp(key, "name") == 0) {
			jp_string(j, val, sizeof(val));
			UiDesign_SetText(doc->nodes[idx].name, UI_DN_NAME_MAX, val);
		} else if (strcmp(key, "type") == 0) {
			jp_string(j, val, sizeof(val));
			type = UiDesign_TypeFromName(val);
			if (type >= 0) doc->nodes[idx].type = type;
			gotType = 1;
		} else if (strcmp(key, "visible") == 0) jp_bool(j, &doc->nodes[idx].visible);
		else if (strcmp(key, "locked") == 0) jp_bool(j, &doc->nodes[idx].locked);
		else if (strcmp(key, "layout") == 0) {
			if (!jp_ch(j, '{')) return 0;
			for (;;) {
				char lk[48];
				jp_ws(j);
				if (jp_ch(j, '}')) break;
				if (!jp_key(j, lk, sizeof(lk))) return 0;
				if (strcmp(lk, "wMode") == 0) jp_int(j, &doc->nodes[idx].wMode);
				else if (strcmp(lk, "hMode") == 0) jp_int(j, &doc->nodes[idx].hMode);
				else if (strcmp(lk, "w") == 0) jp_number(j, &doc->nodes[idx].w);
				else if (strcmp(lk, "h") == 0) jp_number(j, &doc->nodes[idx].h);
				else if (strcmp(lk, "minW") == 0) jp_number(j, &doc->nodes[idx].minW);
				else if (strcmp(lk, "minH") == 0) jp_number(j, &doc->nodes[idx].minH);
				else if (strcmp(lk, "maxW") == 0) jp_number(j, &doc->nodes[idx].maxW);
				else if (strcmp(lk, "maxH") == 0) jp_number(j, &doc->nodes[idx].maxH);
				else if (strcmp(lk, "grow") == 0) jp_number(j, &doc->nodes[idx].grow);
				else if (strcmp(lk, "align") == 0) jp_int(j, &doc->nodes[idx].align);
				else if (strcmp(lk, "valign") == 0) jp_int(j, &doc->nodes[idx].textVAlign);
				else if (strcmp(lk, "textAlign") == 0) jp_int(j, &doc->nodes[idx].textAlign);
				else if (strcmp(lk, "textVAlign") == 0) jp_int(j, &doc->nodes[idx].textVAlign);
				else if (strcmp(lk, "margin") == 0) jp_array_float(j, doc->nodes[idx].margin, 4);
				else if (strcmp(lk, "padding") == 0) jp_array_float(j, doc->nodes[idx].padding, 4);
				else if (strcmp(lk, "gap") == 0) jp_number(j, &doc->nodes[idx].gap);
				else if (strcmp(lk, "radius") == 0) {
					jp_number(j, &doc->nodes[idx].radius);
					gotRadius = 1;
				}
				else if (strcmp(lk, "x") == 0) jp_number(j, &doc->nodes[idx].x);
				else if (strcmp(lk, "y") == 0) jp_number(j, &doc->nodes[idx].y);
				else if (strcmp(lk, "anchors") == 0) jp_int(j, (int *)&doc->nodes[idx].anchors);
				else if (strcmp(lk, "gridRows") == 0) jp_int(j, &doc->nodes[idx].gridRows);
				else if (strcmp(lk, "gridCols") == 0) jp_int(j, &doc->nodes[idx].gridCols);
				else if (strcmp(lk, "gridRowGap") == 0) jp_number(j, &doc->nodes[idx].gridRowGap);
				else if (strcmp(lk, "gridColGap") == 0) jp_number(j, &doc->nodes[idx].gridColGap);
				else if (strcmp(lk, "gridRow") == 0) jp_int(j, &doc->nodes[idx].gridRow);
				else if (strcmp(lk, "gridCol") == 0) jp_int(j, &doc->nodes[idx].gridCol);
				else if (strcmp(lk, "rowSpan") == 0) jp_int(j, &doc->nodes[idx].rowSpan);
				else if (strcmp(lk, "colSpan") == 0) jp_int(j, &doc->nodes[idx].colSpan);
				else if (strcmp(lk, "gridColTracks") == 0)
					jp_string(j, doc->nodes[idx].gridColTracks,
						UI_DN_GRID_TRACKS_MAX);
				else if (!jp_skip(j)) return 0;
				jp_ws(j);
				if (jp_ch(j, ',')) continue;
			}
		} else if (strcmp(key, "props") == 0) {
			if (!jp_ch(j, '{')) return 0;
			for (;;) {
				char pk[48];
				jp_ws(j);
				if (jp_ch(j, '}')) break;
				if (!jp_key(j, pk, sizeof(pk))) return 0;
				if (strcmp(pk, "text") == 0) jp_string(j, doc->nodes[idx].text, UI_DN_TEXT_MAX);
				else if (strcmp(pk, "hint") == 0) jp_string(j, doc->nodes[idx].hint, (int)sizeof(doc->nodes[idx].hint));
				else if (strcmp(pk, "options") == 0) jp_string(j, doc->nodes[idx].options, UI_DN_TEXT_MAX);
				else if (strcmp(pk, "value") == 0) jp_number(j, &doc->nodes[idx].value);
				else if (strcmp(pk, "checked") == 0) {
					jp_bool(j, &doc->nodes[idx].checked);
					gotChecked = 1;
				}
				else if (strcmp(pk, "enabled") == 0) jp_bool(j, &doc->nodes[idx].enabled);
				else if (strcmp(pk, "selected") == 0) jp_int(j, &doc->nodes[idx].selected);
				else if (strcmp(pk, "variant") == 0) jp_int(j, &doc->nodes[idx].variant);
				else if (strcmp(pk, "mode") == 0) jp_int(j, &doc->nodes[idx].mode);
				else if (strcmp(pk, "readonly") == 0) jp_bool(j, &doc->nodes[idx].readonly);
				else if (strcmp(pk, "color") == 0) {
					jp_color(j, &doc->nodes[idx].color);
					gotColor = 1;
				} else if (strcmp(pk, "bgColor") == 0) {
					jp_color(j, &doc->nodes[idx].bgColor);
					gotBg = 1;
				} else if (strcmp(pk, "ecc") == 0) {
					jp_int(j, &doc->nodes[idx].ecc);
					gotEcc = 1;
				} else if (strcmp(pk, "src") == 0) {
					jp_string(j, doc->nodes[idx].logoSrc, UI_DN_PATH_MAX);
					gotLogo = 1;
				} else if (strcmp(pk, "logoRatio") == 0) {
					jp_number(j, &doc->nodes[idx].logoRatio);
					gotLogoRatio = 1;
				} else if (strcmp(pk, "qrPad") == 0) {
					jp_number(j, &doc->nodes[idx].qrPad);
					gotQrPad = 1;
				} else if (strcmp(pk, "ringMax") == 0) {
					jp_number(j, &doc->nodes[idx].ringMax);
					gotRingMax = 1;
				} else if (strcmp(pk, "ringWidth") == 0) {
					jp_number(j, &doc->nodes[idx].ringWidth);
					gotRingWidth = 1;
				}
				else if (!jp_skip(j)) return 0;
				jp_ws(j);
				if (jp_ch(j, ',')) continue;
			}
		} else if (strcmp(key, "onClick") == 0) jp_string(j, doc->nodes[idx].onClick, UI_DN_ID_MAX);
		else if (strcmp(key, "onChange") == 0) jp_string(j, doc->nodes[idx].onChange, UI_DN_ID_MAX);
		else if (strcmp(key, "children") == 0) {
			if (!jp_ch(j, '[')) return 0;
			jp_ws(j);
			if (jp_ch(j, ']')) continue;
			for (;;) {
				int child;
				if (!jp_node(j, doc, win, idx, &child)) return 0;
				jp_ws(j);
				if (jp_ch(j, ']')) break;
				if (!jp_ch(j, ',')) return 0;
			}
		} else if (!jp_skip(j)) return 0;
		jp_ws(j);
		if (jp_ch(j, ',')) continue;
	}
	(void)gotType;
	if (!gotRadius)
		doc->nodes[idx].radius = doc->nodes[idx].type == UI_DN_IMAGE ? 6.0f : 8.0f;
	if (doc->nodes[idx].type == UI_DN_QRCODE) {
		if (!gotEcc) doc->nodes[idx].ecc = UI_QR_ECC_M;
		if (doc->nodes[idx].ecc < 0 || doc->nodes[idx].ecc > 3)
			doc->nodes[idx].ecc = UI_QR_ECC_M;
		if (!gotColor) doc->nodes[idx].color = 0xFF000000u;
		if (!gotBg) doc->nodes[idx].bgColor = 0xFFFFFFFFu;
		if (!gotLogo) doc->nodes[idx].logoSrc[0] = 0;
		if (!gotLogoRatio)
			doc->nodes[idx].logoRatio = UI_QR_LOGO_RATIO_DEFAULT;
		if (doc->nodes[idx].logoRatio < 0.0f)
			doc->nodes[idx].logoRatio = 0.0f;
		if (doc->nodes[idx].logoRatio > UI_QR_LOGO_RATIO_MAX)
			doc->nodes[idx].logoRatio = UI_QR_LOGO_RATIO_MAX;
		if (!gotQrPad) doc->nodes[idx].qrPad = UI_QR_PAD_DEFAULT;
		if (doc->nodes[idx].qrPad < 0.0f) doc->nodes[idx].qrPad = 0.0f;
		if (doc->nodes[idx].qrPad > UI_QR_PAD_MAX)
			doc->nodes[idx].qrPad = UI_QR_PAD_MAX;
	}
	if (doc->nodes[idx].type == UI_DN_RINGPROGRESS) {
		if (!gotColor) doc->nodes[idx].color = 0xFF4F6FF3u;
		if (!gotChecked) doc->nodes[idx].checked = 1;
		if (!gotRingMax || doc->nodes[idx].ringMax <= 0.0f)
			doc->nodes[idx].ringMax = 100.0f;
		if (!gotRingWidth) doc->nodes[idx].ringWidth = 14.0f;
		if (doc->nodes[idx].ringWidth < 2.0f) doc->nodes[idx].ringWidth = 2.0f;
		if (doc->nodes[idx].ringWidth > 64.0f) doc->nodes[idx].ringWidth = 64.0f;
	}
	return 1;
}

int UiDesign_FromJson(UiDesignDoc *doc, const char *text)
{
	JP j;
	char key[64], val[UI_DN_PATH_MAX];
	int version = 1;
	if (!doc || !text) return 0;
	UiDesign_Free(doc);
	UiDesign_Init(doc);
	j.p = text;
	j.end = text + strlen(text);
	if (!jp_ch(&j, '{')) return 0;
	for (;;) {
		jp_ws(&j);
		if (jp_ch(&j, '}')) break;
		if (!jp_key(&j, key, sizeof(key))) return 0;
		if (strcmp(key, "name") == 0) {
			jp_string(&j, doc->name, UI_DN_NAME_MAX);
		} else if (strcmp(key, "version") == 0) {
			jp_int(&j, &version);
		} else if (strcmp(key, "groups") == 0) {
			if (!jp_ch(&j, '[')) return 0;
			jp_ws(&j);
			if (jp_ch(&j, ']')) continue;
			for (;;) {
				int gi;
				jp_ws(&j);
				if (!jp_ch(&j, '{')) return 0;
				gi = UiDesign_AddGroup(doc, "group", "分组");
				for (;;) {
					char gk[48];
					jp_ws(&j);
					if (jp_ch(&j, '}')) break;
					if (!jp_key(&j, gk, sizeof(gk))) return 0;
					if (strcmp(gk, "id") == 0) jp_string(&j, doc->groups[gi].id, UI_DN_ID_MAX);
					else if (strcmp(gk, "name") == 0) jp_string(&j, doc->groups[gi].name, UI_DN_NAME_MAX);
					else if (strcmp(gk, "outputC") == 0) jp_string(&j, doc->groups[gi].outputC, UI_DN_PATH_MAX);
					else if (strcmp(gk, "outputH") == 0) jp_string(&j, doc->groups[gi].outputH, UI_DN_PATH_MAX);
					else if (strcmp(gk, "symbolPrefix") == 0) jp_string(&j, doc->groups[gi].symbolPrefix, UI_DN_SYM_MAX);
					else if (!jp_skip(&j)) return 0;
					jp_ws(&j);
					if (jp_ch(&j, ',')) continue;
				}
				if (!doc->groups[gi].symbolPrefix[0])
					UiDesign_Symbol(doc->groups[gi].id,
						doc->groups[gi].symbolPrefix, UI_DN_SYM_MAX);
				jp_ws(&j);
				if (jp_ch(&j, ']')) break;
				if (!jp_ch(&j, ',')) return 0;
			}
		} else if (strcmp(key, "views") == 0) {
			if (!jp_ch(&j, '[')) return 0;
			jp_ws(&j);
			if (jp_ch(&j, ']')) continue;
			for (;;) {
				int vi, gi = -1;
				char group[UI_DN_ID_MAX] = "";
				UiDesignView *v;
				jp_ws(&j);
				if (!jp_ch(&j, '{')) return 0;
				vi = UiDesign_AddView(doc, "view", "视图");
				v = UiDesign_View(doc, vi);
				for (;;) {
					char vk[48];
					jp_ws(&j);
					if (jp_ch(&j, '}')) break;
					if (!jp_key(&j, vk, sizeof(vk))) return 0;
					if (strcmp(vk, "id") == 0) jp_string(&j, v->id, UI_DN_ID_MAX);
					else if (strcmp(vk, "name") == 0) jp_string(&j, v->name, UI_DN_NAME_MAX);
					else if (strcmp(vk, "width") == 0) jp_number(&j, &v->w);
					else if (strcmp(vk, "height") == 0) jp_number(&j, &v->h);
					else if (strcmp(vk, "group") == 0) {
						jp_ws(&j);
						if (j.p < j.end && *j.p == '"') {
							if (jp_string(&j, group, sizeof(group)))
								gi = UiDesign_FindGroup(doc, group);
						} else if (!jp_skip(&j)) return 0;
					} else if (strcmp(vk, "root") == 0) {
						int root = -1;
						jp_ws(&j);
						if (j.p < j.end && *j.p == 'n') {
							jp_skip(&j);
							root = -1;
						} else if (jp_node(&j, doc, vi, -1, &root)) {
							v->root = root;
						}
					} else if (!jp_skip(&j)) return 0;
					jp_ws(&j);
					if (jp_ch(&j, ',')) continue;
				}
				v->group = gi;
				jp_ws(&j);
				if (jp_ch(&j, ']')) break;
				if (!jp_ch(&j, ',')) return 0;
			}
		} else if (strcmp(key, "hosts") == 0) {
			if (!jp_ch(&j, '[')) return 0;
			jp_ws(&j);
			if (jp_ch(&j, ']')) continue;
			for (;;) {
				int hi, gi = -1, vi = -1;
				char group[UI_DN_ID_MAX] = "";
				char viewId[UI_DN_ID_MAX] = "";
				UiDesignHost *w;
				jp_ws(&j);
				if (!jp_ch(&j, '{')) return 0;
				hi = UiDesign_AddHost(doc, "host", "宿主", UI_DH_WINDOW, -1);
				w = UiDesign_Host(doc, hi);
				for (;;) {
					char hk[48];
					jp_ws(&j);
					if (jp_ch(&j, '}')) break;
					if (!jp_key(&j, hk, sizeof(hk))) return 0;
					if (strcmp(hk, "id") == 0) jp_string(&j, w->id, UI_DN_ID_MAX);
					else if (strcmp(hk, "name") == 0) jp_string(&j, w->name, UI_DN_NAME_MAX);
					else if (strcmp(hk, "title") == 0) jp_string(&j, w->title, 128);
					else if (strcmp(hk, "kind") == 0) jp_int(&j, &w->kind);
					else if (strcmp(hk, "group") == 0) {
						jp_ws(&j);
						if (j.p < j.end && *j.p == '"') {
							if (jp_string(&j, group, sizeof(group)))
								gi = UiDesign_FindGroup(doc, group);
						} else if (!jp_skip(&j)) return 0;
					} else if (strcmp(hk, "view") == 0) {
						jp_ws(&j);
						if (j.p < j.end && *j.p == '"') {
							jp_string(&j, viewId, sizeof(viewId));
							vi = UiDesign_FindView(doc, viewId);
						} else if (!jp_skip(&j)) return 0;
					} else if (strcmp(hk, "width") == 0) jp_number(&j, &w->w);
					else if (strcmp(hk, "height") == 0) jp_number(&j, &w->h);
					else if (strcmp(hk, "minWidth") == 0) jp_number(&j, &w->minW);
					else if (strcmp(hk, "minHeight") == 0) jp_number(&j, &w->minH);
					else if (strcmp(hk, "theme") == 0) jp_int(&j, &w->theme);
					else if (!jp_skip(&j)) return 0;
					jp_ws(&j);
					if (jp_ch(&j, ',')) continue;
				}
				w->group = gi;
				w->view = vi;
				if (w->kind < UI_DH_WINDOW || w->kind > UI_DH_PANEL)
					w->kind = UI_DH_WINDOW;
				jp_ws(&j);
				if (jp_ch(&j, ']')) break;
				if (!jp_ch(&j, ',')) return 0;
			}
		} else if (strcmp(key, "windows") == 0) {
			/* v1 迁移: 每个窗口生成一个同名视图 + 宿主. */
			if (!jp_ch(&j, '[')) return 0;
			jp_ws(&j);
			if (jp_ch(&j, ']')) continue;
			for (;;) {
				int vi, wi, gi = -1;
				char group[UI_DN_ID_MAX] = "";
				UiDesignView *v;
				UiDesignHost *w;
				jp_ws(&j);
				if (!jp_ch(&j, '{')) return 0;
				vi = UiDesign_AddView(doc, "view", "视图");
				wi = UiDesign_AddHost(doc, "window", "窗口", UI_DH_WINDOW, -1);
				v = UiDesign_View(doc, vi);
				w = UiDesign_Host(doc, wi);
				w->view = vi;
				for (;;) {
					char wk[48];
					jp_ws(&j);
					if (jp_ch(&j, '}')) break;
					if (!jp_key(&j, wk, sizeof(wk))) return 0;
					if (strcmp(wk, "id") == 0) {
						jp_string(&j, v->id, UI_DN_ID_MAX);
						UiDesign_SetText(w->id, UI_DN_ID_MAX, v->id);
					} else if (strcmp(wk, "name") == 0) {
						jp_string(&j, v->name, UI_DN_NAME_MAX);
						UiDesign_SetText(w->name, UI_DN_NAME_MAX, v->name);
					} else if (strcmp(wk, "title") == 0) jp_string(&j, w->title, 128);
					else if (strcmp(wk, "kind") == 0) jp_int(&j, &w->kind);
					else if (strcmp(wk, "group") == 0) {
						jp_ws(&j);
						if (j.p < j.end && *j.p == '"') {
							if (jp_string(&j, group, sizeof(group)))
								gi = UiDesign_FindGroup(doc, group);
						} else if (!jp_skip(&j)) return 0;
					} else if (strcmp(wk, "width") == 0) {
						jp_number(&j, &w->w);
						v->w = w->w;
					} else if (strcmp(wk, "height") == 0) {
						jp_number(&j, &w->h);
						v->h = w->h;
					} else if (strcmp(wk, "minWidth") == 0) jp_number(&j, &w->minW);
					else if (strcmp(wk, "minHeight") == 0) jp_number(&j, &w->minH);
					else if (strcmp(wk, "theme") == 0) jp_int(&j, &w->theme);
					else if (strcmp(wk, "root") == 0) {
						int root = -1;
						jp_ws(&j);
						if (j.p < j.end && *j.p == 'n') {
							jp_skip(&j);
							root = -1;
						} else if (jp_node(&j, doc, vi, -1, &root)) {
							v->root = root;
						}
					} else if (!jp_skip(&j)) return 0;
					jp_ws(&j);
					if (jp_ch(&j, ',')) continue;
				}
				v->group = gi;
				w->group = gi;
				w->kind = w->kind == 1 ? UI_DH_DIALOG : UI_DH_WINDOW;
				if (v->id[0] == 0)
					UiDesign_SetText(v->id, UI_DN_ID_MAX, w->id);
				jp_ws(&j);
				if (jp_ch(&j, ']')) break;
				if (!jp_ch(&j, ',')) return 0;
			}
		} else if (!jp_skip(&j)) return 0;
		jp_ws(&j);
		if (jp_ch(&j, ',')) continue;
	}
	(void)version;
	for (int ni = 0; ni < doc->nodeCount; ni++)
		if (doc->nodes[ni].type == UI_DN_TABS)
			UiDesign_TabsNormalize(doc, ni);
	doc->dirty = 0;
	return 1;
}

/* atof 的 0CRT 替代 */
double atof_local(const char *s)
{
	int sign = 1;
	double v = 0, f = 0, div = 1;
	int e = 0, esign = 1;
	if (!s) return 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { sign = -1; s++; }
	else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') { f = f * 10 + (*s - '0'); div *= 10; s++; }
	}
	v += f / div;
	if (*s == 'e' || *s == 'E') {
		s++;
		if (*s == '-') { esign = -1; s++; }
		else if (*s == '+') s++;
		while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
		while (e-- > 0) { if (esign > 0) v *= 10; else v /= 10; }
	}
	return sign * v;
}

/* ── 符号名 ─────────────────────────────────────────────────────────── */

const char *UiDesign_Symbol(const char *id, char *out, int cap)
{
	int i = 0, n = 0;
	if (!out || cap <= 0) return "";
	out[0] = 0;
	if (!id || !id[0]) id = "item";
	while (id[i] && n < cap - 1) {
		char c = id[i++];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_')
			out[n++] = c;
		else if (n > 0 && out[n - 1] != '_')
			out[n++] = '_';
	}
	if (n == 0) {
		UiDesign_SetText(out, cap, "Item");
		return out;
	}
	out[n] = 0;
	if (out[0] >= '0' && out[0] <= '9') {
		char tmp[UI_DN_SYM_MAX];
		UiDesign_SetText(tmp, UI_DN_SYM_MAX, out);
		out[0] = 0;
		UiDesign_SetText(out, cap, "Ui_");
		if (cap > 3) UiDesign_SetText(out + 3, cap - 3, tmp);
	}
	return out;
}

/* ── 文件 ───────────────────────────────────────────────────────────── */

static char *dn_read_utf8(const wchar_t *path, int *outLen)
{
	HANDLE h;
	DWORD size, got = 0;
	char *buf;
	int n;
	h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	size = GetFileSize(h, NULL);
	if (size == INVALID_FILE_SIZE || size > 32u * 1024u * 1024u) {
		CloseHandle(h);
		return NULL;
	}
	buf = (char *)dn_alloc((size_t)size + 1);
	if (!buf) { CloseHandle(h); return NULL; }
	if (size && !ReadFile(h, buf, size, &got, NULL)) {
		dn_free(buf);
		CloseHandle(h);
		return NULL;
	}
	CloseHandle(h);
	buf[got] = 0;
	n = (int)got;
	if (n >= 3 && (unsigned char)buf[0] == 0xEF &&
	    (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
		memmove(buf, buf + 3, (size_t)(n - 3));
		n -= 3;
		buf[n] = 0;
	}
	if (outLen) *outLen = n;
	return buf;
}

static int dn_write_utf8(const wchar_t *path, const char *text, int len)
{
	HANDLE h;
	DWORD wrote = 0;
	if (!path || !text || len < 0) return 0;
	h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	if (len && !WriteFile(h, text, (DWORD)len, &wrote, NULL)) {
		CloseHandle(h);
		return 0;
	}
	CloseHandle(h);
	return (int)wrote == len;
}

static void dn_path_utf8(const wchar_t *path, char *out, int cap)
{
	if (!path || !out || cap <= 0) return;
	WideCharToMultiByte(CP_UTF8, 0, path, -1, out, cap, NULL, NULL);
	out[cap - 1] = 0;
}

int UiDesign_LoadFile(UiDesignDoc *doc, const wchar_t *path)
{
	char *text;
	int len = 0, ok;
	if (!doc || !path) return 0;
	text = dn_read_utf8(path, &len);
	if (!text) return 0;
	ok = UiDesign_FromJson(doc, text);
	dn_free(text);
	if (ok) {
		dn_path_utf8(path, doc->path, UI_DN_PATH_MAX);
		doc->dirty = 0;
	}
	return ok;
}

int UiDesign_SaveFile(UiDesignDoc *doc, const wchar_t *path)
{
	char *text;
	int len, ok;
	if (!doc || !path) return 0;
	text = UiDesign_ToJson(doc);
	if (!text) return 0;
	len = (int)strlen(text);
	ok = dn_write_utf8(path, text, len);
	dn_free(text);
	if (ok) {
		dn_path_utf8(path, doc->path, UI_DN_PATH_MAX);
		doc->dirty = 0;
	}
	return ok;
}

/* ── 布局 ───────────────────────────────────────────────────────────── */

static float dn_clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static float dn_natural_w(const UiDesignNode *n)
{
	float w = n->w > 0.0f ? n->w : UiDesign_DefaultW(n->type);
	return w;
}

static float dn_natural_h(const UiDesignNode *n)
{
	float h = n->h > 0.0f ? n->h : UiDesign_DefaultH(n->type);
	return h;
}

/* 与 UiTabStyle_Line/Pill/Card 的默认高度一致. */
static float dn_tabs_header_h(int variant)
{
	if (variant == UI_TAB_PILL) return 34.0f;
	if (variant == UI_TAB_CARD) return 36.0f;
	return 38.0f;
}

static void dn_inset(UiRect r, const float *pad, UiRect *out)
{
	out->x = r.x + pad[0];
	out->y = r.y + pad[1];
	out->w = r.w - pad[0] - pad[2];
	out->h = r.h - pad[1] - pad[3];
	if (out->w < 0.0f) out->w = 0.0f;
	if (out->h < 0.0f) out->h = 0.0f;
}

static void dn_layout_node(const UiDesignDoc *doc, int index, UiRect rect,
	UiRect *rects);

static void dn_layout_absolute(const UiDesignDoc *doc, const UiDesignNode *p,
	UiRect inner, UiRect *rects)
{
	int i;
	for (i = 0; i < p->childCount; i++) {
		int ci = p->children[i];
		UiDesignNode *n = &doc->nodes[ci];
		UiRect r;
		float w, h;
		if (!n->visible) { rects[ci] = Ui_Rect(0, 0, 0, 0); continue; }
		w = n->wMode == UI_DN_SIZE_FILL ? inner.w : dn_natural_w(n);
		h = n->hMode == UI_DN_SIZE_FILL ? inner.h : dn_natural_h(n);
		if (n->wMode == UI_DN_SIZE_FILL && (n->anchors & (UI_DN_ANCHOR_LEFT | UI_DN_ANCHOR_RIGHT))) {
			w = inner.w - n->margin[0] - n->margin[2];
			r.x = inner.x + n->margin[0];
		} else if (n->anchors & UI_DN_ANCHOR_RIGHT) {
			r.x = inner.x + inner.w - n->margin[2] - w;
		} else {
			r.x = inner.x + n->x + n->margin[0];
		}
		if (n->hMode == UI_DN_SIZE_FILL && (n->anchors & (UI_DN_ANCHOR_TOP | UI_DN_ANCHOR_BOTTOM))) {
			h = inner.h - n->margin[1] - n->margin[3];
			r.y = inner.y + n->margin[1];
		} else if (n->anchors & UI_DN_ANCHOR_BOTTOM) {
			r.y = inner.y + inner.h - n->margin[3] - h;
		} else {
			r.y = inner.y + n->y + n->margin[1];
		}
		if (w < 0.0f) w = 0.0f;
		if (h < 0.0f) h = 0.0f;
		r.w = w;
		r.h = h;
		dn_layout_node(doc, ci, r, rects);
	}
}

static void dn_layout_linear(const UiDesignDoc *doc, const UiDesignNode *p,
	UiRect inner, int row, UiRect *rects)
{
	int i, grows = 0;
	float avail = row ? inner.w : inner.h;
	float fixed = 0.0f;
	float gapTotal = p->gap * (p->childCount > 1 ? p->childCount - 1 : 0);
	float cursor = row ? inner.x : inner.y;
	int count = p->childCount;
	if (gapTotal > avail) gapTotal = avail;
	avail -= gapTotal;
	for (i = 0; i < count; i++) {
		UiDesignNode *n = &doc->nodes[p->children[i]];
		int mode = row ? n->wMode : n->hMode;
		if (!n->visible) continue;
		if (mode == UI_DN_SIZE_GROW || mode == UI_DN_SIZE_FILL) grows++;
		else if (mode == UI_DN_SIZE_FIXED) fixed += row ? n->w : n->h;
		else fixed += row ? dn_natural_w(n) : dn_natural_h(n);
	}
	if (fixed > avail) fixed = avail;
	for (i = 0; i < count; i++) {
		int ci = p->children[i];
		UiDesignNode *n = &doc->nodes[ci];
		int mode, cmode;
		float mainSize, crossSize, mainPos, crossPos = 0.0f;
		UiRect r;
		if (!n->visible) { rects[ci] = Ui_Rect(0, 0, 0, 0); continue; }
		mode = row ? n->wMode : n->hMode;
		cmode = row ? n->hMode : n->wMode;
		if (mode == UI_DN_SIZE_GROW || mode == UI_DN_SIZE_FILL)
			mainSize = grows > 0 ? (avail - fixed) / grows : 0.0f;
		else if (mode == UI_DN_SIZE_FIXED) mainSize = row ? n->w : n->h;
		else mainSize = row ? dn_natural_w(n) : dn_natural_h(n);
		if (mainSize < 0.0f) mainSize = 0.0f;
		if (cmode == UI_DN_SIZE_FILL || cmode == UI_DN_SIZE_GROW)
			crossSize = row ? inner.h : inner.w;
		else crossSize = row ? dn_natural_h(n) : dn_natural_w(n);
		if (row) {
			mainPos = cursor;
			if (crossSize < inner.h) {
				if (n->align == UI_DN_ALIGN_CENTER) crossPos = inner.y + (inner.h - crossSize) * 0.5f;
				else if (n->align == UI_DN_ALIGN_END) crossPos = inner.y + inner.h - crossSize;
				else crossPos = inner.y;
			} else crossPos = inner.y;
			r = Ui_Rect(mainPos, crossPos, mainSize, crossSize);
		} else {
			mainPos = cursor;
			if (crossSize < inner.w) {
				if (n->align == UI_DN_ALIGN_CENTER) crossPos = inner.x + (inner.w - crossSize) * 0.5f;
				else if (n->align == UI_DN_ALIGN_END) crossPos = inner.x + inner.w - crossSize;
				else crossPos = inner.x;
			} else crossPos = inner.x;
			r = Ui_Rect(crossPos, mainPos, crossSize, mainSize);
		}
		cursor += mainSize + p->gap;
		dn_layout_node(doc, ci, r, rects);
	}
}

enum { DN_TRACK_FIXED = 0, DN_TRACK_FR, DN_TRACK_AUTO };

/* "120,1fr,auto" -> kinds/vals; 无效项按 1fr. 返回轨道数. */
static int dn_parse_col_tracks(const char *spec, int *kinds, float *vals, int cap)
{
	int n = 0;
	const char *p = spec;
	while (p && *p && n < cap) {
		char tok[32];
		int len = 0, digits = 0, isFr = 0, neg = 0;
		float v = 0.0f;
		const char *q;
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;
		while (*p && *p != ',' && len < (int)sizeof(tok) - 1)
			tok[len++] = *p++;
		if (*p == ',') p++;
		tok[len] = 0;
		while (len > 0 && (tok[len - 1] == ' ' || tok[len - 1] == '\t'))
			tok[--len] = 0;
		if (!len) continue;
		if ((tok[0] == 'a' || tok[0] == 'A') &&
		    (tok[1] == 'u' || tok[1] == 'U') &&
		    (tok[2] == 't' || tok[2] == 'T') &&
		    (tok[3] == 'o' || tok[3] == 'O') && !tok[4]) {
			kinds[n] = DN_TRACK_AUTO;
			vals[n] = 0.0f;
			n++;
			continue;
		}
		q = tok;
		if (*q == '-') { neg = 1; q++; }
		while (*q >= '0' && *q <= '9') {
			v = v * 10.0f + (float)(*q - '0');
			q++;
			digits = 1;
		}
		if (*q == '.') {
			float f = 0.0f, div = 1.0f;
			q++;
			while (*q >= '0' && *q <= '9') {
				f = f * 10.0f + (float)(*q - '0');
				div *= 10.0f;
				q++;
				digits = 1;
			}
			v += f / div;
		}
		if (neg) v = -v;
		if (*q == 'f' || *q == 'F') {
			isFr = 1;
			q++;
			if (*q == 'r' || *q == 'R') q++;
		} else if (*q == 'p' || *q == 'P') {
			q++;
			if (*q == 'x' || *q == 'X') q++;
		}
		if (!digits) {
			kinds[n] = DN_TRACK_FR;
			vals[n] = 1.0f;
		} else if (isFr || v <= 0.0f) {
			kinds[n] = DN_TRACK_FR;
			vals[n] = v > 0.0f ? v : 1.0f;
		} else {
			kinds[n] = DN_TRACK_FIXED;
			vals[n] = v;
		}
		n++;
	}
	return n;
}

static void dn_grid_auto_widths(const UiDesignDoc *doc, const UiDesignNode *p,
	int cols, float *widths)
{
	int i;
	for (i = 0; i < p->childCount; i++) {
		UiDesignNode *n = &doc->nodes[p->children[i]];
		int c, cs;
		if (!n->visible) continue;
		c = n->gridCol >= 0 ? n->gridCol : i % cols;
		cs = n->colSpan > 0 ? n->colSpan : 1;
		if (c < 0) c = 0;
		if (c >= cols) c = cols - 1;
		if (cs > 1) continue;   /* 跨列项不参与 AUTO 列宽 */
		{
			float w = dn_natural_w(n);
			if (w > widths[c]) widths[c] = w;
		}
	}
}

int UiDesign_GridColumns(const UiDesignDoc *doc, const UiDesignNode *n,
	UiRect inner, float *xs, float *ws, int cap)
{
	int kinds[UI_DN_GRID_MAX_COLS];
	float vals[UI_DN_GRID_MAX_COLS];
	float widths[UI_DN_GRID_MAX_COLS];
	float fixedSum = 0.0f, frSum = 0.0f, autoSum = 0.0f;
	float avail, remain, cursor = inner.x;
	int ntok, cols, i;
	if (!doc || !n || !xs || !ws || cap <= 0) return 0;
	ntok = dn_parse_col_tracks(n->gridColTracks, kinds, vals,
		UI_DN_GRID_MAX_COLS);
	cols = n->gridCols > 0 ? n->gridCols : (ntok > 0 ? ntok : 1);
	if (cols < 1) cols = 1;
	if (cols > UI_DN_GRID_MAX_COLS) cols = UI_DN_GRID_MAX_COLS;
	if (cols > cap) cols = cap;
	for (i = 0; i < cols; i++) {
		if (i >= ntok) {
			kinds[i] = DN_TRACK_FR;
			vals[i] = 1.0f;
		}
		widths[i] = 0.0f;
	}
	dn_grid_auto_widths(doc, n, cols, widths);
	avail = inner.w - n->gridColGap * (float)(cols - 1);
	if (avail < 0.0f) avail = 0.0f;
	for (i = 0; i < cols; i++) {
		if (kinds[i] == DN_TRACK_FIXED) fixedSum += vals[i];
		else if (kinds[i] == DN_TRACK_AUTO) autoSum += widths[i];
		else frSum += vals[i];
	}
	remain = avail - fixedSum - autoSum;
	if (remain < 0.0f) remain = 0.0f;
	for (i = 0; i < cols; i++) {
		float w;
		if (kinds[i] == DN_TRACK_FIXED) w = vals[i];
		else if (kinds[i] == DN_TRACK_AUTO) w = widths[i];
		else w = frSum > 0.0f ? remain * vals[i] / frSum : 0.0f;
		if (w < 0.0f) w = 0.0f;
		xs[i] = cursor;
		ws[i] = w;
		cursor += w + n->gridColGap;
	}
	return cols;
}

static void dn_layout_grid(const UiDesignDoc *doc, const UiDesignNode *p,
	UiRect inner, UiRect *rects)
{
	float xs[UI_DN_GRID_MAX_COLS], ws[UI_DN_GRID_MAX_COLS];
	int cols;
	int rows;
	float rowGap = p->gridRowGap;
	float cellH;
	int i;
	cols = UiDesign_GridColumns(doc, p, inner, xs, ws, UI_DN_GRID_MAX_COLS);
	if (cols < 1) cols = 1;
	/* gridRows <= 0: 按子节点数量自动确定行数. */
	rows = p->gridRows > 0 ? p->gridRows
		: (p->childCount + cols - 1) / cols;
	if (rows < 1) rows = 1;
	cellH = (inner.h - rowGap * (rows - 1)) / rows;
	if (cellH < 0.0f) cellH = 0.0f;
	for (i = 0; i < p->childCount; i++) {
		int ci = p->children[i];
		UiDesignNode *n = &doc->nodes[ci];
		int r = n->gridRow >= 0 ? n->gridRow : i / cols;
		int c = n->gridCol >= 0 ? n->gridCol : i % cols;
		int rs = n->rowSpan > 0 ? n->rowSpan : 1;
		int cs = n->colSpan > 0 ? n->colSpan : 1;
		float w, h;
		UiRect cell;
		if (!n->visible) { rects[ci] = Ui_Rect(0, 0, 0, 0); continue; }
		if (r >= rows) r = rows - 1;
		if (c >= cols) c = cols - 1;
		if (rs > rows - r) rs = rows - r;
		if (cs > cols - c) cs = cols - c;
		cell.x = xs[c];
		cell.y = inner.y + r * (cellH + rowGap);
		cell.w = xs[c + cs - 1] + ws[c + cs - 1] - xs[c];
		cell.h = cellH * rs + rowGap * (rs - 1);
		w = n->wMode == UI_DN_SIZE_FIXED ? n->w
			: (n->wMode == UI_DN_SIZE_AUTO ? dn_natural_w(n) : cell.w);
		h = n->hMode == UI_DN_SIZE_FIXED ? n->h
			: (n->hMode == UI_DN_SIZE_AUTO ? dn_natural_h(n) : cell.h);
		{
			UiRect rct = Ui_Rect(cell.x, cell.y, w, h);
			float ox = 0.0f, oy = 0.0f;
			if (w < cell.w) {
				if (n->align == UI_DN_ALIGN_CENTER) ox = (cell.w - w) * 0.5f;
				else if (n->align == UI_DN_ALIGN_END) ox = cell.w - w;
			}
			if (h < cell.h) oy = (cell.h - h) * 0.5f;
			rct.x += ox;
			rct.y += oy;
			dn_layout_node(doc, ci, rct, rects);
		}
	}
}

static void dn_layout_node(const UiDesignDoc *doc, int index, UiRect rect,
	UiRect *rects)
{
	UiDesignNode *n;
	UiRect inner;
	if (index < 0 || index >= doc->nodeCount) return;
	n = &doc->nodes[index];
	rects[index] = rect;
	if (!n->visible || n->childCount <= 0) return;
	dn_inset(rect, n->padding, &inner);
	if (n->type == UI_DN_TABS) {
		float hh = dn_tabs_header_h(n->variant);
		UiRect page;
		int i;
		if (hh > inner.h) hh = inner.h;
		page = Ui_Rect(inner.x, inner.y + hh, inner.w, inner.h - hh);
		if (page.h < 0.0f) page.h = 0.0f;
		/* 页与页重叠: 只显示 selected, 但每页都按同一页区布局. */
		for (i = 0; i < n->childCount; i++) {
			rects[n->children[i]] = page;
			dn_layout_node(doc, n->children[i], page, rects);
		}
		return;
	}
	switch (n->type) {
	case UI_DN_COLUMN: dn_layout_linear(doc, n, inner, 0, rects); break;
	case UI_DN_ROW: dn_layout_linear(doc, n, inner, 1, rects); break;
	case UI_DN_GRID: dn_layout_grid(doc, n, inner, rects); break;
	default: dn_layout_absolute(doc, n, inner, rects); break;
	}
}

int UiDesign_Layout(const UiDesignDoc *doc, int view, UiRect client,
	UiRect *rects, int rectCap)
{
	UiDesignView *v;
	int i;
	if (!doc || !rects || view < 0 || view >= doc->viewCount) return 0;
	if (rectCap < doc->nodeCount) return 0;
	for (i = 0; i < doc->nodeCount; i++) rects[i] = Ui_Rect(0, 0, 0, 0);
	v = &doc->views[view];
	if (v->root >= 0) dn_layout_node(doc, v->root, client, rects);
	return 1;
}

static int dn_hit_node(const UiDesignDoc *doc, int index, const UiRect *rects,
	float x, float y)
{
	UiDesignNode *n;
	int i;
	if (index < 0 || index >= doc->nodeCount) return -1;
	n = &doc->nodes[index];
	if (!n->visible) return -1;
	{
		int first = 0, last = n->childCount;
		if (n->type == UI_DN_TABS && n->childCount > 0) {
			int sel = n->selected;
			if (sel < 0) sel = 0;
			if (sel >= n->childCount) sel = n->childCount - 1;
			first = sel;
			last = sel + 1;
		}
		for (i = last - 1; i >= first; i--) {
			int hit = dn_hit_node(doc, n->children[i], rects, x, y);
			if (hit >= 0) return hit;
		}
	}
	if (Ui_RectContains(rects[index], x, y)) return index;
	return -1;
}

int UiDesign_FindNodeAt(const UiDesignDoc *doc, int view,
	const UiRect *rects, float x, float y)
{
	if (!doc || !rects || view < 0 || view >= doc->viewCount) return -1;
	return dn_hit_node(doc, doc->views[view].root, rects, x, y);
}

/* ── 运行时 ─────────────────────────────────────────────────────────── */

static UiId dn_widget_id(const UiDesignInstance *inst, int idx, int kind)
{
	return Ui_IdIndex("cui.design", inst->view, idx, kind);
}

static UiDesignNodeState *dn_state(UiDesignInstance *inst, int idx)
{
	if (!inst || idx < 0 || idx >= inst->stateCount) return NULL;
	return &inst->states[idx];
}

static void dn_select_parse(UiDesignInstance *inst, int idx, UiDesignNode *n,
	UiDesignNodeState *st)
{
	char buf[UI_DN_TEXT_MAX];
	int count = 0, i = 0, j = 0;
	if (strcmp(n->options, st->selCache) == 0) return;
	UiDesign_SetText(st->selCache, sizeof(st->selCache), n->options);
	while (n->options[i] && count < 8) {
		int start = i;
		while (n->options[i] && n->options[i] != ',') i++;
		j = 0;
		while (start < i && (n->options[start] == ' ' || n->options[start] == '\t')) start++;
		while (start < i && j < (int)sizeof(st->selItems[count]) - 1)
			st->selItems[count][j++] = n->options[start++];
		while (j > 0 && (st->selItems[count][j - 1] == ' ' ||
			st->selItems[count][j - 1] == '\t')) j--;
		st->selItems[count][j] = 0;
		if (j > 0) {
			st->selPtrs[count] = st->selItems[count];
			count++;
		}
		if (n->options[i] == ',') i++;
	}
	st->selCount = count;
	UiSelect_SetItems(&st->select, st->selPtrs, count);
	if (n->selected < 0 || n->selected >= count)
		UiSelect_SetSelected(&st->select, 0);
	else
		UiSelect_SetSelected(&st->select, n->selected);
	(void)inst; (void)idx; (void)buf;
}

static void dn_state_init(UiDesignInstance *inst, int idx)
{
	UiDesignNode *n;
	UiDesignNodeState *st;
	if (!inst || idx < 0 || idx >= inst->stateCount) return;
	st = &inst->states[idx];
	if (st->inited) return;
	n = &inst->doc->nodes[idx];
	st->inited = 1;
	UiDesign_SetText(st->text, UI_DN_TEXT_MAX, n->text);
	switch (n->type) {
	case UI_DN_INPUT:
		UiInput_Init(&st->input, inst->ui, inst->ime);
		UiInput_Add(&st->input, dn_widget_id(inst, idx, 1), 255, 0, NULL, NULL);
		Edit_SetTextUtf8(UiInput_Edit(&st->input, 0), st->text);
		st->input.blinkOn = 1;
		break;
	case UI_DN_CHECKBOX:
		UiCheckbox_Init(&st->checkbox, dn_widget_id(inst, idx, 2), n->checked);
		break;
	case UI_DN_SWITCH:
		UiSwitch_Init(&st->sw, dn_widget_id(inst, idx, 3), n->checked);
		break;
	case UI_DN_SLIDER:
		UiSlider_Init(&st->slider, dn_widget_id(inst, idx, 4),
			dn_clampf(n->value, 0.0f, 1.0f));
		break;
	case UI_DN_SELECT:
		UiSelect_Init(&st->select, dn_widget_id(inst, idx, 5));
		UiSelect_AttachIme(&st->select, inst->ime);
		st->selCache[0] = 0;
		dn_select_parse(inst, idx, n, st);
		break;
	case UI_DN_TEXTAREA:
		UiTextArea_Init(&st->ta, dn_widget_id(inst, idx, 7),
			(UI_DN_TEXT_MAX - 1) / 4);
		UiTextArea_AttachIme(&st->ta, inst->ime);
		UiTextArea_SetTextUtf8(&st->ta, st->text);
		MultiByteToWideChar(CP_UTF8, 0, n->hint[0] ? n->hint : "多行文本",
			-1, st->taHint, 128);
		UiTextArea_SetPlaceholder(&st->ta, st->taHint);
		UiTextArea_SetReadonly(&st->ta, n->readonly);
		break;
	case UI_DN_DATEPICKER: {
		int mode = n->mode >= 0 && n->mode <= UI_DP_RANGE_DATETIME
			? n->mode : UI_DP_DATE;
		UiDatePicker_Init(&st->dp, dn_widget_id(inst, idx, 8), mode);
		UiDatePicker_AttachIme(&st->dp, inst->ime);
		if (n->hint[0]) UiDatePicker_SetPlaceholder(&st->dp, n->hint);
		if (mode >= UI_DP_RANGE_DATE) {
			UiDateTime a, b;
			if (n->text[0] && n->options[0] &&
			    UiDate_Parse(n->text, st->dp.format, &a) &&
			    UiDate_Parse(n->options, st->dp.format, &b))
				UiDatePicker_SetRange(&st->dp, &a, &b);
		} else if (n->text[0]) {
			UiDateTime v;
			if (UiDate_Parse(n->text, st->dp.format, &v))
				UiDatePicker_SetValue(&st->dp, &v);
		}
		break;
	}
	case UI_DN_COLORSEL:
		UiColorSelect_Init(&st->cs, dn_widget_id(inst, idx, 9), n->color);
		UiColorSelect_AttachIme(&st->cs, inst->ime);
		break;
	case UI_DN_QRCODE:
		UiQrCode_Init(&st->qr, n->ecc);
		break;
	case UI_DN_RINGPROGRESS:
		UiRingProgress_Init(&st->ring, inst->ui);
		/* 设计器/生成界面是静态表单: 固定值不启动补间, 也无需宿主补调 EndFrame. */
		UiRingProgress_SetDurationMs(&st->ring, 0);
		break;
	case UI_DN_TABS:
		st->barCount = n->childCount > 32 ? 32 : n->childCount;
		UiTabBar_Init(&st->bar, dn_widget_id(inst, idx, 6), st->barCount);
		if (n->selected >= 0 && n->selected < st->barCount)
			UiTabBar_SetSelected(&st->bar, n->selected);
		break;
	default:
		break;
	}
}

int UiDesignInstance_Init(UiDesignInstance *inst, UiDesignDoc *doc,
	int host, UiContext *ui, UiPopupManager *pm, UiIme *ime)
{
	if (!inst) return 0;
	memset(inst, 0, sizeof(*inst));
	if (!doc || !ui || host < 0 || host >= doc->hostCount) return 0;
	inst->doc = doc;
	inst->host = host;
	inst->view = doc->hosts[host].view;
	inst->ui = ui;
	inst->pm = pm;
	inst->ime = ime;
	inst->stateCount = doc->nodeCount;
	inst->rects = (UiRect *)dn_alloc((size_t)doc->nodeCount * sizeof(UiRect));
	inst->states = (UiDesignNodeState *)dn_alloc(
		(size_t)doc->nodeCount * sizeof(UiDesignNodeState));
	if (!inst->rects || !inst->states) {
		UiDesignInstance_Shutdown(inst);
		return 0;
	}
	if (doc->hosts[host].kind == UI_DH_DIALOG) {
		UiDialog_Init(&inst->dialog, Ui_Id("cui.design.dialog", host, 0));
		UiDialog_AttachIme(&inst->dialog, ime);
		inst->dialogReady = 1;
	}
	return 1;
}

void UiDesignInstance_Shutdown(UiDesignInstance *inst)
{
	int i;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (!st->inited) continue;
		UiInput_Shutdown(&st->input);
		UiSelect_Shutdown(&st->select);
		if (inst->doc->nodes[i].type == UI_DN_TEXTAREA)
			UiTextArea_Shutdown(&st->ta);
		else if (inst->doc->nodes[i].type == UI_DN_DATEPICKER)
			UiDatePicker_Shutdown(&st->dp);
		else if (inst->doc->nodes[i].type == UI_DN_COLORSEL)
			UiColorSelect_Shutdown(&st->cs);
		else if (inst->doc->nodes[i].type == UI_DN_QRCODE)
			UiQrCode_Shutdown(&st->qr);
		else if (inst->doc->nodes[i].type == UI_DN_RINGPROGRESS)
			UiRingProgress_Shutdown(&st->ring);
	}
	dn_free(inst->rects);
	dn_free(inst->states);
	memset(inst, 0, sizeof(*inst));
}

void UiDesignInstance_SetEventFn(UiDesignInstance *inst,
	void (*fn)(void *, UiDesignInstance *, const char *, const char *, int),
	void *user)
{
	if (!inst) return;
	inst->eventFn = fn;
	inst->eventUser = user;
}

static void dn_emit_event(UiDesignInstance *inst, int idx, int kind)
{
	const UiDesignNode *n;
	const char *action;
	if (!inst || idx < 0 || idx >= inst->doc->nodeCount) return;
	n = &inst->doc->nodes[idx];
	action = kind == 1 ? n->onClick : n->onChange;
	inst->lastEventNode = idx;
	inst->lastEventKind = kind;
	if (inst->eventFn)
		inst->eventFn(inst->eventUser, inst, n->id, action, kind);
}

const char *UiDesign_GetText(UiDesignInstance *inst, const char *nodeId)
{
	int idx;
	UiDesignNodeState *st;
	if (!inst || !nodeId) return "";
	idx = UiDesign_FindNode(inst->doc, inst->view, nodeId);
	if (idx < 0) return "";
	if (inst->states[idx].inited &&
	    (inst->doc->nodes[idx].type == UI_DN_INPUT ||
	     inst->doc->nodes[idx].type == UI_DN_TEXTAREA))
		return inst->states[idx].text;
	st = &inst->states[idx];
	if (!st->inited) return inst->doc->nodes[idx].text;
	return inst->doc->nodes[idx].text;
}

void UiDesign_SetTextValue(UiDesignInstance *inst, const char *nodeId,
	const char *text)
{
	int idx;
	UiDesignNodeState *st;
	if (!inst || !nodeId) return;
	idx = UiDesign_FindNode(inst->doc, inst->view, nodeId);
	if (idx < 0) return;
	UiDesign_NodeSetText(&inst->doc->nodes[idx], text);
	if (!inst->states[idx].inited)
		dn_state_init(inst, idx);
	st = &inst->states[idx];
	UiDesign_SetText(st->text, UI_DN_TEXT_MAX, text);
	if (inst->doc->nodes[idx].type == UI_DN_INPUT)
		Edit_SetTextUtf8(UiInput_Edit(&st->input, 0), text);
	else if (inst->doc->nodes[idx].type == UI_DN_TEXTAREA)
		UiTextArea_SetTextUtf8(&st->ta, text);
	else if (inst->doc->nodes[idx].type == UI_DN_DATEPICKER) {
		UiDateTime v;
		if (text[0] && UiDate_Parse(text, st->dp.format, &v))
			UiDatePicker_SetValue(&st->dp, &v);
	}
}

static void dn_draw_node(UiDesignInstance *inst, int idx)
{
	UiContext *ui = inst->ui;
	UiTheme *t = ui->theme;
	Gfx *g = ui->gfx;
	UiDesignNode *n = &inst->doc->nodes[idx];
	UiDesignNodeState *st;
	UiRect r = inst->rects[idx];
	int i;
	if (!n->visible || r.w <= 0.0f || r.h <= 0.0f) return;
	dn_state_init(inst, idx);
	st = dn_state(inst, idx);
	if (n->type == UI_DN_TABS) {
		const char *labels[32];
		UiTabStyle stl;
		UiRect inner, page;
		int cnt, i, sel, ci;
		float hh;
		if (!st) return;
		cnt = n->childCount > 32 ? 32 : n->childCount;
		if (cnt != st->barCount) {
			UiTabBar_SetCount(&st->bar, cnt);
			st->barCount = cnt;
		}
		for (i = 0; i < cnt; i++)
			labels[i] = inst->doc->nodes[n->children[i]].name;
		sel = n->selected;
		if (sel < 0) sel = 0;
		if (sel >= cnt) sel = cnt > 0 ? cnt - 1 : -1;
		if (n->variant == 1) stl = UiTabStyle_Pill();
		else if (n->variant == 2) stl = UiTabStyle_Card();
		else stl = UiTabStyle_Line();
		dn_inset(r, n->padding, &inner);
		UiTabBar_SetSelected(&st->bar, sel);
		ci = UiTabBar_Draw(&st->bar, ui, inner, labels, &stl, n->enabled);
		if (ci >= 0) {
			n->selected = ci;
			dn_emit_event(inst, idx, 2);
		}
		hh = dn_tabs_header_h(n->variant);
		if (hh > inner.h) hh = inner.h;
		page = Ui_Rect(inner.x, inner.y + hh, inner.w, inner.h - hh);
		if (page.h < 0.0f) page.h = 0.0f;
		if (n->variant == 2 && page.w > 0.0f && page.h > 0.0f)
			Gfx_FillRect(g, page, stl.contentBg ? stl.contentBg : t->card);
		sel = n->selected;
		if (sel < 0) sel = 0;
		if (sel >= cnt) sel = cnt > 0 ? cnt - 1 : -1;
		if (sel >= 0)
			dn_draw_node(inst, n->children[sel]);
		else
			Gfx_Text(g, t->fSmall, "标签页: 无子页", page, t->text4,
				GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
		/* Card 的活动页签延迟到页面内容之后, 才能盖住页面上沿接缝. */
		UiTabBar_DrawActive(&st->bar, ui, labels, &stl);
		return;
	}
	if (UiDesign_IsContainer(n->type)) {
		if (n->type == UI_DN_PANEL)
			Gfx_FillRoundRect(g, r, n->radius > 0.0f ? n->radius : 0.0f,
				t->card);
		for (i = 0; i < n->childCount; i++)
			dn_draw_node(inst, n->children[i]);
		return;
	}
	switch (n->type) {
	case UI_DN_LABEL: {
		uint32_t flags = GFX_TEXT_ELLIPSIS;
		UiRect tr = r;
		if (n->textAlign == UI_DN_ALIGN_CENTER) flags |= GFX_TEXT_CENTER;
		else if (n->textAlign == UI_DN_ALIGN_END) flags |= GFX_TEXT_RIGHT;
		if (n->textVAlign == UI_DN_VALIGN_CENTER) flags |= GFX_TEXT_VCENTER;
		else if (n->textVAlign == UI_DN_VALIGN_BOTTOM) {
			float th = Gfx_TextHeight(g, t->fBody);
			if (th < tr.h) {
				tr.y += tr.h - th;
				tr.h = th;
			}
		}
		Gfx_Text(g, t->fBody, n->text, tr,
			n->enabled ? t->text : t->btnDisabledText, flags);
		break;
	}
	case UI_DN_BUTTON: {
		int variant = n->variant == 1 ? UI_BTN_PRIMARY
			: (n->variant == 2 ? UI_BTN_DANGER : UI_BTN_SECONDARY);
		if (Ui_Button(ui, dn_widget_id(inst, idx, 10), r,
			n->text, variant, n->enabled))
			dn_emit_event(inst, idx, 1);
		break;
	}
	case UI_DN_INPUT: {
		char buf[UI_DN_TEXT_MAX];
		if (!st) break;
		UiInput_SetRect(&st->input, 0, r);
		UiInput_Draw(&st->input, 0);
		Edit_TextUtf8(UiInput_Edit(&st->input, 0), buf, UI_DN_TEXT_MAX);
		if (strcmp(buf, st->text) != 0) {
			UiDesign_SetText(st->text, UI_DN_TEXT_MAX, buf);
			UiDesign_SetText(n->text, UI_DN_TEXT_MAX, buf);
			dn_emit_event(inst, idx, 2);
		}
		break;
	}
	case UI_DN_CHECKBOX: {
		if (!st) break;
		if (Ui_Checkbox(&st->checkbox, ui, r, n->text, n->enabled))
			dn_emit_event(inst, idx, 2);
		break;
	}
	case UI_DN_SWITCH: {
		if (!st) break;
		if (Ui_Switch(&st->sw, ui, r, n->text, n->enabled))
			dn_emit_event(inst, idx, 2);
		break;
	}
	case UI_DN_SLIDER: {
		if (!st) break;
		if (Ui_Slider(&st->slider, ui, r, NULL, n->enabled))
			dn_emit_event(inst, idx, 2);
		break;
	}
	case UI_DN_PROGRESS:
		Ui_ProgressBar(ui, r, dn_clampf(n->value, 0.0f, 1.0f), 1);
		break;
	case UI_DN_SELECT: {
		if (!st) break;
		dn_select_parse(inst, idx, n, st);
		if (Ui_Select(&st->select, inst->pm, ui, r,
			n->hint[0] ? n->hint : "请选择", n->enabled)) {
			n->selected = st->select.selected;
			dn_emit_event(inst, idx, 2);
		}
		break;
	}
	case UI_DN_IMAGE: {
		float rad = n->radius > 0.0f ? n->radius : 0.0f;
		Gfx_FillRoundRect(g, r, rad, Ui_ColorMix(t->card, t->primary.soft, 0.35f));
		Gfx_StrokeRoundRect(g, r, rad, t->border, 1.0f);
		Gfx_Text(g, t->fSmall, n->text[0] ? n->text : "Image", r,
			t->text3, GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		break;
	}
	case UI_DN_TEXTAREA:
		if (!st) break;
		UiTextArea_SetReadonly(&st->ta, n->readonly);
		if (Ui_TextArea(&st->ta, ui, r, n->enabled)) {
			UiTextArea_TextUtf8(&st->ta, st->text, UI_DN_TEXT_MAX);
			UiDesign_SetText(n->text, UI_DN_TEXT_MAX, st->text);
			dn_emit_event(inst, idx, 2);
		}
		break;
	case UI_DN_DATEPICKER:
		if (!st) break;
		Ui_DatePicker(&st->dp, inst->pm, ui, r,
			n->hint[0] ? n->hint : NULL, n->enabled);
		if (st->dp.changed || st->dp.committed) {
			if (st->dp.hasValue)
				UiDate_Format(&st->dp.value, st->dp.format, n->text,
					UI_DN_TEXT_MAX);
			else n->text[0] = 0;
			if (st->dp.mode >= UI_DP_RANGE_DATE) {
				if (st->dp.hasEnd)
					UiDate_Format(&st->dp.end, st->dp.format, n->options,
						UI_DN_TEXT_MAX);
				else n->options[0] = 0;
			}
			st->dp.changed = 0;
			st->dp.committed = 0;
			dn_emit_event(inst, idx, 2);
		}
		break;
	case UI_DN_COLORSEL:
		if (!st) break;
		if (UiColorSelect_Draw(&st->cs, inst->pm, ui, r, n->enabled)) {
			n->color = UiColorSelect_GetColor(&st->cs);
			dn_emit_event(inst, idx, 2);
		}
		if (st->cs.committed) {
			st->cs.committed = 0;
			n->color = UiColorSelect_GetColor(&st->cs);
		}
		break;
	case UI_DN_SEPARATOR:
		Gfx_Line(g, r.x, r.y + r.h * 0.5f, r.x + r.w, r.y + r.h * 0.5f,
			t->border, 1.0f);
		break;
	case UI_DN_QRCODE:
		if (!st) break;
		UiQrCode_SetEcc(&st->qr, n->ecc);
		UiQrCode_SetColors(&st->qr, n->color, n->bgColor);
		UiQrCode_SetText(&st->qr, n->text);
		UiQrCode_SetLogoSrc(&st->qr, n->logoSrc);
		UiQrCode_SetLogoRatio(&st->qr, n->logoRatio);
		UiQrCode_SetPadding(&st->qr, n->qrPad);
		UiQrCode_Draw(&st->qr, ui, r);
		break;
	case UI_DN_RINGPROGRESS: {
		float max;
		if (!st) break;
		max = n->ringMax > 0.0f ? n->ringMax : 100.0f;
		UiRingProgress_SetStyle(&st->ring, n->ringWidth, n->color, n->checked);
		UiRingProgress_SetRange(&st->ring, 0.0, max);
		UiRingProgress_SetValue(&st->ring,
			(double)dn_clampf(n->value, 0.0f, 1.0f) * max);
		UiRingProgress_SetTitle(&st->ring, n->text);
		UiRingProgress_SetUnit(&st->ring, n->hint);
		UiRingProgress_Draw(&st->ring, r);
		break;
	}
	default:
		break;
	}
}

void UiDesign_Draw(UiDesignInstance *inst, UiRect rc)
{
	int i;
	if (!inst || !inst->doc || !inst->ui) return;
	inst->lastEventNode = -1;
	inst->lastEventKind = 0;
	if (inst->view < 0 || inst->view >= inst->doc->viewCount) return;
	UiDesign_Layout(inst->doc, inst->view, rc, inst->rects, inst->stateCount);
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && st->input.focus >= 0) {
			Ui_SetFocusId(inst->ui, dn_widget_id(inst, i, 1));
			break;
		}
	}
	if (inst->doc->views[inst->view].root >= 0)
		dn_draw_node(inst, inst->doc->views[inst->view].root);
}

void UiDesign_DrawOverlay(UiDesignInstance *inst)
{
	int i;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (!st->inited) continue;
		if (inst->doc->nodes[i].type == UI_DN_SELECT)
			UiSelect_DrawPopup(&st->select, inst->pm, inst->ui);
		else if (inst->doc->nodes[i].type == UI_DN_DATEPICKER)
			UiDatePicker_DrawPopup(&st->dp, inst->pm, inst->ui);
		else if (inst->doc->nodes[i].type == UI_DN_COLORSEL)
			UiColorSelect_DrawPopup(&st->cs, inst->pm, inst->ui);
	}
}

void UiDesign_EndFrame(UiDesignInstance *inst)
{
	int i;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (!st->inited) continue;
		if (inst->doc->nodes[i].type == UI_DN_COLORSEL)
			UiColorSelect_EndFrame(&st->cs, UiFrame_Now());
		else if (inst->doc->nodes[i].type == UI_DN_RINGPROGRESS)
			UiRingProgress_EndFrame(&st->ring);
	}
}

int UiDesign_DialogIsOpen(const UiDesignInstance *inst)
{
	return inst && inst->dialogReady && UiDialog_IsOpen(&inst->dialog);
}

int UiDesign_DialogOpen(UiDesignInstance *inst, UiId owner)
{
	if (!inst || !inst->dialogReady) return 0;
	return UiDialog_Open(&inst->dialog, inst->ui, owner);
}

void UiDesign_DialogDraw(UiDesignInstance *inst)
{
	UiDesignHost *w;
	UiRect body;
	if (!inst || !inst->dialogReady || !UiDialog_IsOpen(&inst->dialog)) return;
	w = &inst->doc->hosts[inst->host];
	if (UiDialog_Begin(&inst->dialog, inst->ui, w->title, w->w, w->h, &body)) {
		UiDesign_Draw(inst, body);
		UiDialog_End(&inst->dialog, inst->ui);
	}
	UiDesign_DrawOverlay(inst);
}

static int dn_hit_input(UiDesignInstance *inst, UiId hit)
{
	int i;
	if (!inst) return -1;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (!st->inited || inst->doc->nodes[i].type != UI_DN_INPUT) continue;
		if (dn_widget_id(inst, i, 1) == hit) return i;
	}
	return -1;
}

static void dn_color_end(UiDesignInstance *inst)
{
	int i;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_COLORSEL)
			UiColorSelect_EndFrame(&st->cs, UiFrame_Now());
	}
}

void UiDesign_MouseDown(UiDesignInstance *inst, float x, float y)
{
	UiId hit;
	int target, i, used = 0;
	if (!inst || !inst->ui) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_COLORSEL)
			used |= UiColorSelect_Mouse(&st->cs, inst->ui,
				UI_COLOR_MOUSE_DOWN, x, y);
	}
	if (used) {
		dn_color_end(inst);
		return;
	}
	hit = Ui_HitTest(inst->ui, x, y);
	target = dn_hit_input(inst, hit);
	if (target >= 0) {
		for (i = 0; i < inst->stateCount; i++)
			if (i != target) inst->states[i].input.focus = -1;
	} else {
		/* 点到输入框以外: 释放 UiInput 焦点, 让 TextArea/DatePicker
		 * 等自管焦点的控件能拿到 focusId. */
		for (i = 0; i < inst->stateCount; i++)
			inst->states[i].input.focus = -1;
	}
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_INPUT)
			UiInput_MouseDown(&st->input, hit, x);
	}
}

void UiDesign_MouseMove(UiDesignInstance *inst, float x, float y)
{
	int i, used = 0;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_COLORSEL)
			used |= UiColorSelect_Mouse(&st->cs, inst->ui,
				UI_COLOR_MOUSE_MOVE, x, y);
	}
	if (used) {
		dn_color_end(inst);
		return;
	}
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_INPUT)
			UiInput_MouseMove(&st->input, x);
	}
}

void UiDesign_MouseUp(UiDesignInstance *inst)
{
	int i, used = 0;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_COLORSEL)
			used |= UiColorSelect_Mouse(&st->cs, inst->ui,
				UI_COLOR_MOUSE_UP, 0.0f, 0.0f);
	}
	if (used) {
		dn_color_end(inst);
		return;
	}
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_INPUT)
			UiInput_MouseUp(&st->input);
	}
}

static int dn_focus_index(UiDesignInstance *inst)
{
	int i;
	for (i = 0; i < inst->stateCount; i++)
		if (inst->states[i].inited && inst->states[i].input.focus >= 0) return i;
	return -1;
}

int UiDesign_Key(UiDesignInstance *inst, UINT vk)
{
	int i;
	if (!inst) return 0;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		UiDesignNode *n;
		if (!st->inited) continue;
		n = &inst->doc->nodes[i];
		if (n->type == UI_DN_DATEPICKER) {
			if (UiDatePicker_Key(&st->dp, inst->pm, inst->ui, vk)) return 1;
		} else if (n->type == UI_DN_COLORSEL) {
			if (UiColorSelect_Key(&st->cs, inst->pm, inst->ui, vk)) return 1;
		} else if (n->type == UI_DN_TEXTAREA) {
			if (UiTextArea_Key(&st->ta, inst->ui, vk)) return 1;
		} else if (n->type == UI_DN_TABS) {
			int before = st->bar.selected;
			if (UiTabBar_Key(&st->bar, inst->ui, vk)) {
				if (st->bar.selected != before) {
					n->selected = st->bar.selected;
					dn_emit_event(inst, i, 2);
				}
				return 1;
			}
		}
	}
	i = dn_focus_index(inst);
	if (i < 0) return 0;
	return UiInput_Key(&inst->states[i].input, inst->ui->hwnd, vk);
}

int UiDesign_Char(UiDesignInstance *inst, wchar_t ch)
{
	int i = dn_focus_index(inst);
	if (!inst) return 0;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		UiDesignNode *n;
		if (!st->inited) continue;
		n = &inst->doc->nodes[i];
		if (n->type == UI_DN_DATEPICKER) {
			if (UiDatePicker_Char(&st->dp, inst->ui, ch)) return 1;
		} else if (n->type == UI_DN_COLORSEL) {
			if (UiColorSelect_Char(&st->cs, inst->ui, ch)) return 1;
		} else if (n->type == UI_DN_TEXTAREA) {
			if (UiTextArea_Char(&st->ta, inst->ui, ch)) return 1;
		}
	}
	i = dn_focus_index(inst);
	if (i < 0) return 0;
	return UiInput_Char(&inst->states[i].input, ch);
}

void UiDesign_OnIme(UiDesignInstance *inst)
{
	int i, focus;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (!st->inited) continue;
		if (inst->doc->nodes[i].type == UI_DN_DATEPICKER)
			UiDatePicker_OnIme(&st->dp, inst->ui);
		else if (inst->doc->nodes[i].type == UI_DN_COLORSEL)
			UiColorSelect_OnIme(&st->cs, inst->ui);
		else if (inst->doc->nodes[i].type == UI_DN_TEXTAREA)
			UiTextArea_OnIme(&st->ta, inst->ui);
	}
	focus = dn_focus_index(inst);
	if (focus >= 0) UiInput_OnIme(&inst->states[focus].input);
}

void UiDesign_Blink(UiDesignInstance *inst)
{
	int i;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (!st->inited) continue;
		if (inst->doc->nodes[i].type == UI_DN_INPUT)
			UiInput_Blink(&st->input);
		else if (inst->doc->nodes[i].type == UI_DN_DATEPICKER)
			UiDatePicker_Blink(&st->dp, inst->ui);
		else if (inst->doc->nodes[i].type == UI_DN_COLORSEL)
			UiColorSelect_Blink(&st->cs, inst->ui);
		else if (inst->doc->nodes[i].type == UI_DN_TEXTAREA)
			UiTextArea_Blink(&st->ta, inst->ui);
	}
}

int UiDesign_Busy(const UiDesignInstance *inst)
{
	int i;
	if (!inst) return 0;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (!st->inited) continue;
		if (inst->doc->nodes[i].type == UI_DN_SELECT) {
			if (UiSelect_Busy(&st->select)) return 1;
		} else if (inst->doc->nodes[i].type == UI_DN_TEXTAREA) {
			if (UiTextArea_Busy(&st->ta)) return 1;
		} else if (inst->doc->nodes[i].type == UI_DN_DATEPICKER) {
			if (inst->pm && UiPopupManager_IsOpen(inst->pm, st->dp.id)) return 1;
		} else if (inst->doc->nodes[i].type == UI_DN_COLORSEL) {
			if (inst->pm && UiColorSelect_IsOpen(inst->pm, &st->cs)) return 1;
		}
	}
	if (inst->dialogReady && UiDialog_Busy(&inst->dialog)) return 1;
	return 0;
}

/* ── 代码生成 ───────────────────────────────────────────────────────── */

static void cg_quote(UiDStr *s, const char *text)
{
	int i;
	ds_add(s, "\"");
	if (text) {
		for (i = 0; text[i]; i++) {
			unsigned char c = (unsigned char)text[i];
			if (c == '"' || c == '\\') {
				char e[3] = { '\\', (char)c, 0 };
				ds_add(s, e);
			} else if (c == '\n') ds_add(s, "\\n");
			else if (c == '\r') ds_add(s, "\\r");
			else if (c == '\t') ds_add(s, "\\t");
			else {
				char e[2] = { (char)c, 0 };
				ds_add(s, e);
			}
		}
	}
	ds_add(s, "\"");
}

static void cg_upper(const char *id, char *out, int cap)
{
	int i = 0, n = 0;
	if (!out || cap <= 0) return;
	out[0] = 0;
	while (id && id[i] && n < cap - 1) {
		char c = id[i++];
		if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
			out[n++] = c;
		else if (n > 0 && out[n - 1] != '_')
			out[n++] = '_';
	}
	out[n] = 0;
	if (n == 0) UiDesign_SetText(out, cap, "WIN");
}

static void cg_num(UiDStr *s, float v)
{
	ds_addf(s, "%.3ff", v);
}

static int cg_uses_enabled(int t)
{
	switch (t) {
	case UI_DN_LABEL: case UI_DN_BUTTON: case UI_DN_INPUT:
	case UI_DN_CHECKBOX: case UI_DN_SWITCH: case UI_DN_SLIDER:
	case UI_DN_SELECT: case UI_DN_TEXTAREA: case UI_DN_DATEPICKER:
	case UI_DN_COLORSEL: case UI_DN_TABS:
		return 1;
	default:
		return 0;
	}
}

static int cg_uses_text(int t)
{
	switch (t) {
	case UI_DN_LABEL: case UI_DN_BUTTON: case UI_DN_INPUT:
	case UI_DN_CHECKBOX: case UI_DN_SWITCH: case UI_DN_IMAGE:
	case UI_DN_TEXTAREA: case UI_DN_DATEPICKER: case UI_DN_QRCODE:
	case UI_DN_RINGPROGRESS:
		return 1;
	default:
		return 0;
	}
}

static int cg_uses_hint(int t)
{
	return t == UI_DN_INPUT || t == UI_DN_SELECT ||
		t == UI_DN_TEXTAREA || t == UI_DN_DATEPICKER ||
		t == UI_DN_RINGPROGRESS;
}

static int cg_uses_change(int t)
{
	switch (t) {
	case UI_DN_INPUT: case UI_DN_CHECKBOX: case UI_DN_SWITCH:
	case UI_DN_SLIDER: case UI_DN_SELECT: case UI_DN_TEXTAREA:
	case UI_DN_DATEPICKER: case UI_DN_COLORSEL: case UI_DN_TABS:
		return 1;
	default:
		return 0;
	}
}

static int cg_parent_type(const UiDesignDoc *doc, const UiDesignNode *n,
	int wantFree, int wantGrid)
{
	int pt;
	if (!doc || !n || n->parent < 0 || n->parent >= doc->nodeCount) return 0;
	pt = doc->nodes[n->parent].type;
	if (wantFree) return pt == UI_DN_ABSOLUTE || pt == UI_DN_PANEL;
	return pt == UI_DN_GRID;
}

static void cg_node(UiDStr *s, const UiDesignDoc *doc, int old, int viewNew,
	int parentOld, int *rootOut)
{
	const UiDesignNode *n = &doc->nodes[old];
	float defW = UiDesign_DefaultW(n->type);
	float defH = UiDesign_DefaultH(n->type);
	int freeParent = cg_parent_type(doc, n, 1, 0);
	int gridParent = cg_parent_type(doc, n, 0, 1);
	/* 根节点由宿主窗口定尺寸; Tabs 子页由页区定尺寸, 生成这些值无效. */
	int sized = n->parent >= 0 &&
		doc->nodes[n->parent].type != UI_DN_TABS;
	int i;
	ds_addf(s, "    int n%d = UiDesign_AddNode(&gDoc, %d, ",
		old, viewNew);
	ds_addf(s, "%d, ", n->type);
	if (parentOld >= 0) ds_addf(s, "n%d, ", parentOld);
	else ds_add(s, "-1, ");
	cg_quote(s, n->id);
	ds_add(s, ", ");
	cg_quote(s, n->name);
	ds_add(s, ");\n");
	ds_addf(s, "    UiDesignNode *p%d = UiDesign_Node(&gDoc, n%d);\n", old, old);
	/* AddNode 已写入类型与默认布局; 只输出与默认值不同的项, 且跳过
	 * 该类型用不到的字段, 避免生成代码臃肿. */
	if (!n->visible)
		ds_addf(s, "    p%d->visible=0;\n", old);
	if (!n->enabled && cg_uses_enabled(n->type))
		ds_addf(s, "    p%d->enabled=0;\n", old);
	if (n->wMode != UI_DN_SIZE_FILL && sized)
		ds_addf(s, "    p%d->wMode=%d;\n", old, n->wMode);
	if (n->hMode != UI_DN_SIZE_FIXED && sized)
		ds_addf(s, "    p%d->hMode=%d;\n", old, n->hMode);
	if (n->w != defW && sized) {
		ds_addf(s, "    p%d->w=", old); cg_num(s, n->w); ds_add(s, ";\n");
	}
	if (n->h != defH && sized) {
		ds_addf(s, "    p%d->h=", old); cg_num(s, n->h); ds_add(s, ";\n");
	}
	if (n->minW != 0.0f && sized) {
		ds_addf(s, "    p%d->minW=", old); cg_num(s, n->minW); ds_add(s, ";\n");
	}
	if (n->minH != 0.0f && sized) {
		ds_addf(s, "    p%d->minH=", old); cg_num(s, n->minH); ds_add(s, ";\n");
	}
	if (n->maxW != 0.0f && sized) {
		ds_addf(s, "    p%d->maxW=", old); cg_num(s, n->maxW); ds_add(s, ";\n");
	}
	if (n->maxH != 0.0f && sized) {
		ds_addf(s, "    p%d->maxH=", old); cg_num(s, n->maxH); ds_add(s, ";\n");
	}
	if (n->align != UI_DN_ALIGN_START)
		ds_addf(s, "    p%d->align=%d;\n", old, n->align);
	if (n->type == UI_DN_LABEL) {
		if (n->textAlign != UI_DN_ALIGN_START)
			ds_addf(s, "    p%d->textAlign=%d;\n", old, n->textAlign);
		if (n->textVAlign != UI_DN_VALIGN_CENTER)
			ds_addf(s, "    p%d->textVAlign=%d;\n", old, n->textVAlign);
	}
	if ((n->type == UI_DN_COLUMN || n->type == UI_DN_ROW) &&
	    n->gap != 8.0f) {
		ds_addf(s, "    p%d->gap=", old); cg_num(s, n->gap); ds_add(s, ";\n");
	}
	if ((n->type == UI_DN_PANEL || n->type == UI_DN_IMAGE) &&
	    n->radius != (n->type == UI_DN_IMAGE ? 6.0f : 8.0f)) {
		ds_addf(s, "    p%d->radius=", old); cg_num(s, n->radius); ds_add(s, ";\n");
	}
	if (UiDesign_IsContainer(n->type)) {
		for (i = 0; i < 4; i++)
			if (n->padding[i] != 0.0f) {
				ds_addf(s, "    p%d->padding[%d]=", old, i);
				cg_num(s, n->padding[i]);
				ds_add(s, ";\n");
			}
	}
	if (freeParent) {
		if (n->x != 0.0f) {
			ds_addf(s, "    p%d->x=", old); cg_num(s, n->x); ds_add(s, ";\n");
		}
		if (n->y != 0.0f) {
			ds_addf(s, "    p%d->y=", old); cg_num(s, n->y); ds_add(s, ";\n");
		}
		for (i = 0; i < 4; i++)
			if (n->margin[i] != 0.0f) {
				ds_addf(s, "    p%d->margin[%d]=", old, i);
				cg_num(s, n->margin[i]);
				ds_add(s, ";\n");
			}
		if (n->anchors)
			ds_addf(s, "    p%d->anchors=%u;\n", old, n->anchors);
	}
	if (n->type == UI_DN_GRID) {
		if (n->gridCols != 1)
			ds_addf(s, "    p%d->gridCols=%d;\n", old, n->gridCols);
		if (n->gridRows != 1)
			ds_addf(s, "    p%d->gridRows=%d;\n", old, n->gridRows);
		if (n->gridRowGap != 0.0f) {
			ds_addf(s, "    p%d->gridRowGap=", old);
			cg_num(s, n->gridRowGap);
			ds_add(s, ";\n");
		}
		if (n->gridColGap != 0.0f) {
			ds_addf(s, "    p%d->gridColGap=", old);
			cg_num(s, n->gridColGap);
			ds_add(s, ";\n");
		}
		if (n->gridColTracks[0]) {
			ds_addf(s, "    UiDesign_SetText(p%d->gridColTracks, UI_DN_GRID_TRACKS_MAX, ",
				old);
			cg_quote(s, n->gridColTracks);
			ds_add(s, ");\n");
		}
	}
	if (gridParent) {
		if (n->gridRow != -1)
			ds_addf(s, "    p%d->gridRow=%d;\n", old, n->gridRow);
		if (n->gridCol != -1)
			ds_addf(s, "    p%d->gridCol=%d;\n", old, n->gridCol);
		if (n->rowSpan != 1)
			ds_addf(s, "    p%d->rowSpan=%d;\n", old, n->rowSpan);
		if (n->colSpan != 1)
			ds_addf(s, "    p%d->colSpan=%d;\n", old, n->colSpan);
	}
	if (cg_uses_text(n->type) && n->text[0]) {
		ds_addf(s, "    UiDesign_NodeSetText(p%d, ", old);
		cg_quote(s, n->text);
		ds_add(s, ");\n");
	}
	if (cg_uses_hint(n->type) && n->hint[0]) {
		ds_addf(s, "    UiDesign_NodeSetHint(p%d, ", old);
		cg_quote(s, n->hint);
		ds_add(s, ");\n");
	}
	if ((n->type == UI_DN_SELECT ||
	    (n->type == UI_DN_DATEPICKER && n->mode >= UI_DP_RANGE_DATE)) &&
	    n->options[0]) {
		ds_addf(s, "    UiDesign_NodeSetOptions(p%d, ", old);
		cg_quote(s, n->options);
		ds_add(s, ");\n");
	}
	if ((n->type == UI_DN_SLIDER || n->type == UI_DN_PROGRESS ||
	    n->type == UI_DN_RINGPROGRESS) && n->value != 0.5f) {
		ds_addf(s, "    p%d->value=", old);
		cg_num(s, n->value);
		ds_add(s, ";\n");
	}
	if ((n->type == UI_DN_CHECKBOX || n->type == UI_DN_SWITCH) && n->checked)
		ds_addf(s, "    p%d->checked=1;\n", old);
	if (n->type == UI_DN_RINGPROGRESS && !n->checked)
		ds_addf(s, "    p%d->checked=0;\n", old);
	if ((n->type == UI_DN_SELECT || n->type == UI_DN_TABS) &&
	    n->selected > 0)
		ds_addf(s, "    p%d->selected=%d;\n", old, n->selected);
	if ((n->type == UI_DN_BUTTON || n->type == UI_DN_TABS) && n->variant)
		ds_addf(s, "    p%d->variant=%d;\n", old, n->variant);
	if (n->type == UI_DN_DATEPICKER && n->mode)
		ds_addf(s, "    p%d->mode=%d;\n", old, n->mode);
	if (n->type == UI_DN_TEXTAREA && n->readonly)
		ds_addf(s, "    p%d->readonly=1;\n", old);
	if (n->type == UI_DN_COLORSEL && n->color != 0xFF3B82F6u)
		ds_addf(s, "    p%d->color=0x%08Xu;\n", old, n->color);
	if (n->type == UI_DN_RINGPROGRESS) {
		if (n->color != 0xFF4F6FF3u)
			ds_addf(s, "    p%d->color=0x%08Xu;\n", old, n->color);
		if (n->ringMax != 100.0f) {
			ds_addf(s, "    p%d->ringMax=", old);
			cg_num(s, n->ringMax);
			ds_add(s, ";\n");
		}
		if (n->ringWidth != 14.0f) {
			ds_addf(s, "    p%d->ringWidth=", old);
			cg_num(s, n->ringWidth);
			ds_add(s, ";\n");
		}
	}
	if (n->type == UI_DN_QRCODE) {
		if (n->ecc != UI_QR_ECC_M)
			ds_addf(s, "    p%d->ecc=%d;\n", old, n->ecc);
		if (n->color != 0xFF000000u)
			ds_addf(s, "    p%d->color=0x%08Xu;\n", old, n->color);
		if (n->bgColor != 0xFFFFFFFFu)
			ds_addf(s, "    p%d->bgColor=0x%08Xu;\n", old, n->bgColor);
		if (n->logoRatio != UI_QR_LOGO_RATIO_DEFAULT) {
			ds_addf(s, "    p%d->logoRatio=", old);
			cg_num(s, n->logoRatio);
			ds_add(s, ";\n");
		}
		if (n->qrPad != UI_QR_PAD_DEFAULT) {
			ds_addf(s, "    p%d->qrPad=", old);
			cg_num(s, n->qrPad);
			ds_add(s, ";\n");
		}
		if (n->logoSrc[0]) {
			ds_addf(s, "    UiDesign_SetText(p%d->logoSrc, UI_DN_PATH_MAX, ",
				old);
			cg_quote(s, n->logoSrc);
			ds_add(s, ");\n");
		}
	}
	if (n->type == UI_DN_BUTTON && n->onClick[0]) {
		ds_addf(s, "    UiDesign_SetText(p%d->onClick, UI_DN_ID_MAX, ", old);
		cg_quote(s, n->onClick);
		ds_add(s, ");\n");
	}
	if (cg_uses_change(n->type) && n->onChange[0]) {
		ds_addf(s, "    UiDesign_SetText(p%d->onChange, UI_DN_ID_MAX, ", old);
		cg_quote(s, n->onChange);
		ds_add(s, ");\n");
	}
	if (rootOut) *rootOut = old;
	for (i = 0; i < n->childCount; i++)
		cg_node(s, doc, n->children[i], viewNew, old, NULL);
}

static int cg_join_path(const wchar_t *dir, const char *name, wchar_t *out, int cap)
{
	int n;
	if (!out || cap <= 0) return 0;
	if (!name || !name[0]) return 0;
	if (name[0] == '/' || name[0] == '\\' ||
	    (name[0] && name[1] == ':')) {
		return MultiByteToWideChar(CP_UTF8, 0, name, -1, out, cap) > 0;
	}
	n = 0;
	if (dir && dir[0]) {
		while (dir[n] && n < cap - 1) { out[n] = dir[n]; n++; }
		if (n > 0 && out[n - 1] != L'\\' && out[n - 1] != L'/') out[n++] = L'\\';
	}
	out[n] = 0;
	return MultiByteToWideChar(CP_UTF8, 0, name, -1, out + n, cap - n) > 0;
}

static const char *cg_base_name(const char *path, char *out, int cap)
{
	int i, n = 0, start = 0;
	if (!path) return "";
	for (i = 0; path[i]; i++)
		if (path[i] == '\\' || path[i] == '/') start = i + 1;
	while (path[start + n] && n < cap - 1) { out[n] = path[start + n]; n++; }
	out[n] = 0;
	return out;
}

static int cg_build_group(const UiDesignDoc *doc, int group, int onlyHost,
	UiDStr *c, UiDStr *h, char *outC, int outCCap, char *outH, int outHCap,
	char *prefix, int prefixCap, int *hostCount)
{
	int i, count = 0, vcount = 0;
	int *vmap = NULL;
	const UiDesignGroup *g = NULL;
	const char *gid, *gname;
	char baseH[UI_DN_PATH_MAX];
	if (!doc) return -1;
	if (group >= 0) {
		if (group >= doc->groupCount) return -1;
		g = &doc->groups[group];
		gid = g->id;
		gname = g->name;
		if (g->outputC[0])
			UiDesign_SetText(outC, outCCap, g->outputC);
		else
			snprintf(outC, (size_t)outCCap, "%s.ui.c", g->id);
		if (g->outputH[0])
			UiDesign_SetText(outH, outHCap, g->outputH);
		else
			snprintf(outH, (size_t)outHCap, "%s.ui.h", g->id);
		UiDesign_Symbol(g->symbolPrefix[0] ? g->symbolPrefix : g->id, prefix,
			prefixCap);
	} else {
		const UiDesignHost *w;
		char sym[UI_DN_SYM_MAX];
		if (onlyHost < 0 || onlyHost >= doc->hostCount) return -1;
		w = &doc->hosts[onlyHost];
		gid = w->id;
		gname = w->name;
		UiDesign_Symbol(w->id, sym, sizeof(sym));
		UiDesign_Symbol(w->id, prefix, prefixCap);
		snprintf(outC, (size_t)outCCap, "%s.ui.c", sym);
		snprintf(outH, (size_t)outHCap, "%s.ui.h", sym);
	}
	cg_base_name(outH, baseH, sizeof(baseH));
	if (doc->viewCount > 0) {
		vmap = (int *)dn_alloc((size_t)doc->viewCount * sizeof(int));
		if (!vmap) return -1;
		for (i = 0; i < doc->viewCount; i++) vmap[i] = -1;
	}

	ds_addf(c, "/* Generated by CUI Designer. Source: %s */\n", doc->name);
	ds_add(c, "#include \"ui/ui_design.h\"\n");
	ds_add(c, "#include \""); ds_add(c, baseH); ds_add(c, "\"\n\n");
	ds_add(c, "static UiDesignDoc gDoc;\nstatic int gBuilt;\n\n");
	ds_add(c, "static void build_doc(void)\n{\n");
	ds_add(c, "    UiDesign_Init(&gDoc);\n");
	ds_add(c, "    UiDesign_AddGroup(&gDoc, "); cg_quote(c, gid);
	ds_add(c, ", "); cg_quote(c, gname); ds_add(c, ");\n");
	/* 视图: 同一视图只构建一次, 可被多个宿主复用. */
	for (i = 0; i < doc->viewCount; i++) {
		const UiDesignView *v = &doc->views[i];
		int vn, root = -1;
		if (onlyHost >= 0) {
			int hv = doc->hosts[onlyHost].view;
			if (i != hv) continue;
		} else if (v->group != group) continue;
		vn = vcount++;
		vmap[i] = vn;
		ds_addf(c, "    { int v%d = UiDesign_AddView(&gDoc, ", vn);
		cg_quote(c, v->id); ds_add(c, ", "); cg_quote(c, v->name);
		ds_add(c, ");\n");
		ds_addf(c, "      UiDesignView *pv%d = UiDesign_View(&gDoc, v%d);\n",
			vn, vn);
		if (v->w != 800.0f) {
			ds_addf(c, "      pv%d->w=", vn); cg_num(c, v->w); ds_add(c, ";\n");
		}
		if (v->h != 540.0f) {
			ds_addf(c, "      pv%d->h=", vn); cg_num(c, v->h); ds_add(c, ";\n");
		}
		if (v->root >= 0) {
			int oldRoot = v->root;
			cg_node(c, doc, oldRoot, vn, -1, &root);
			ds_addf(c, "      pv%d->root=n%d;\n", vn, oldRoot);
		}
		ds_add(c, "    }\n");
	}
	/* 宿主: 只记录外壳与挂载的视图. */
	for (i = 0; i < doc->hostCount; i++) {
		const UiDesignHost *w = &doc->hosts[i];
		int n0, vn = -1;
		if (onlyHost >= 0) {
			if (i != onlyHost) continue;
		} else if (w->group != group) continue;
		n0 = count++;
		if (w->view >= 0 && w->view < doc->viewCount)
			vn = vmap[w->view];
		ds_addf(c, "    { int w%d = UiDesign_AddHost(&gDoc, ", n0);
		cg_quote(c, w->id); ds_add(c, ", "); cg_quote(c, w->name);
		ds_addf(c, ", %d, 0);\n", w->kind);
		ds_addf(c, "      UiDesignHost *pw%d = UiDesign_Host(&gDoc, w%d);\n",
			n0, n0);
		if (w->title[0] && strcmp(w->title, w->name) != 0) {
			ds_addf(c, "      UiDesign_SetText(pw%d->title, 128, ", n0);
			cg_quote(c, w->title); ds_add(c, ");\n");
		}
		if (w->kind != UI_DH_PANEL) {
			if (w->w != 800.0f) {
				ds_addf(c, "      pw%d->w=", n0); cg_num(c, w->w); ds_add(c, ";\n");
			}
			if (w->h != 540.0f) {
				ds_addf(c, "      pw%d->h=", n0); cg_num(c, w->h); ds_add(c, ";\n");
			}
			if (w->minW != 320.0f) {
				ds_addf(c, "      pw%d->minW=", n0); cg_num(c, w->minW); ds_add(c, ";\n");
			}
			if (w->minH != 240.0f) {
				ds_addf(c, "      pw%d->minH=", n0); cg_num(c, w->minH); ds_add(c, ";\n");
			}
		}
		if (w->theme != UI_THEME_LIGHT)
			ds_addf(c, "      pw%d->theme=%d;\n", n0, w->theme);
		if (vn >= 0)
			ds_addf(c, "      pw%d->view=v%d;\n", n0, vn);
		ds_add(c, "    }\n");
	}
	ds_add(c, "    gBuilt=1;\n}\n\n");
	ds_addf(c, "UiDesignDoc *%s_Doc(void)\n{\n", prefix);
	ds_add(c, "    if (!gBuilt) build_doc();\n    return &gDoc;\n}\n\n");

	ds_add(h, "/* Generated by CUI Designer. Do not edit by hand. */\n");
	{
		char guard[UI_DN_SYM_MAX + 16];
		cg_upper(prefix, guard, sizeof(guard));
		ds_addf(h, "#ifndef %s_UI_H\n#define %s_UI_H\n\n", guard, guard);
	}
	ds_add(h, "#include \"ui/ui_design.h\"\n\n");
	ds_add(h, "UiDesignDoc *"); ds_add(h, prefix); ds_add(h, "_Doc(void);\n\n");
	{
		int wi = 0;
		for (i = 0; i < doc->hostCount; i++) {
			if (onlyHost >= 0) {
				if (i != onlyHost) continue;
			} else if (doc->hosts[i].group != group) continue;
			{
				char wu[UI_DN_SYM_MAX];
				cg_upper(doc->hosts[i].id, wu, sizeof(wu));
				ds_addf(h, "#define %s_HOST_%s %d\n", prefix, wu, wi++);
			}
		}
	}
	ds_add(h, "\n");
	count = 0;
	for (i = 0; i < doc->hostCount; i++) {
		const UiDesignHost *w = &doc->hosts[i];
		char ws[UI_DN_SYM_MAX];
		if (onlyHost >= 0) {
			if (i != onlyHost) continue;
		} else if (w->group != group) continue;
		UiDesign_Symbol(w->id, ws, sizeof(ws));
		ds_addf(h, "void %s_%s_Init(UiDesignInstance *inst, UiContext *ui, UiPopupManager *pm, UiIme *ime);\n", prefix, ws);
		if (w->kind == UI_DH_DIALOG) {
			ds_addf(h, "int  %s_%s_DialogOpen(UiDesignInstance *inst, UiId owner);\n", prefix, ws);
			ds_addf(h, "void %s_%s_DialogDraw(UiDesignInstance *inst);\n", prefix, ws);
		} else {
			ds_addf(h, "void %s_%s_Draw(UiDesignInstance *inst, UiRect rc);\n", prefix, ws);
		}
		ds_add(h, "\n");
		ds_addf(c, "void %s_%s_Init(UiDesignInstance *inst, UiContext *ui, UiPopupManager *pm, UiIme *ime)\n{\n", prefix, ws);
		ds_addf(c, "    UiDesignInstance_Init(inst, %s_Doc(), %d, ui, pm, ime);\n}\n\n", prefix, count);
		if (w->kind == UI_DH_DIALOG) {
			ds_addf(c, "int %s_%s_DialogOpen(UiDesignInstance *inst, UiId owner)\n{\n    return UiDesign_DialogOpen(inst, owner);\n}\n\n", prefix, ws);
			ds_addf(c, "void %s_%s_DialogDraw(UiDesignInstance *inst)\n{\n    UiDesign_DialogDraw(inst);\n}\n\n", prefix, ws);
		} else {
			ds_addf(c, "void %s_%s_Draw(UiDesignInstance *inst, UiRect rc)\n{\n    UiDesign_Draw(inst, rc);\n    UiDesign_DrawOverlay(inst);\n}\n\n", prefix, ws);
		}
		count++;
	}
	{
		char guard[UI_DN_SYM_MAX + 16];
		cg_upper(prefix, guard, sizeof(guard));
		ds_addf(h, "#endif /* %s_UI_H */\n", guard);
	}
	if (vmap) dn_free(vmap);
	if (hostCount) *hostCount = count;
	return 0;
}

int UiDesign_GenerateGroup(const UiDesignDoc *doc, int group,
	const wchar_t *dir, char *report, int cap)
{
	UiDStr c, h;
	int wrote = 0, count = 0;
	char prefix[UI_DN_SYM_MAX], outC[UI_DN_PATH_MAX], outH[UI_DN_PATH_MAX];
	wchar_t wc[UI_DN_PATH_MAX * 2], wh[UI_DN_PATH_MAX * 2];
	if (!doc || group < 0 || group >= doc->groupCount) return -1;
	ds_init(&c);
	ds_init(&h);
	if (cg_build_group(doc, group, -1, &c, &h, outC, sizeof(outC), outH,
	    sizeof(outH), prefix, sizeof(prefix), &count) < 0) {
		ds_free(&c); ds_free(&h);
		return -1;
	}
	if (!cg_join_path(dir, outC, wc, (int)(sizeof(wc) / sizeof(wc[0]))) ||
	    !cg_join_path(dir, outH, wh, (int)(sizeof(wh) / sizeof(wh[0])))) {
		ds_free(&c); ds_free(&h);
		return -1;
	}
	if (dn_write_utf8(wc, c.p ? c.p : "", c.n)) wrote++;
	if (dn_write_utf8(wh, h.p ? h.p : "", h.n)) wrote++;
	ds_free(&c); ds_free(&h);
	if (report) snprintf(report, cap, "group %s: %d file(s), %d host(s)",
		doc->groups[group].name, wrote, count);
	return wrote == 2 ? 2 : -1;
}

int UiDesign_GenerateHost(const UiDesignDoc *doc, int host,
	const wchar_t *dir, char *report, int cap)
{
	UiDStr c, h;
	int wrote = 0, count = 0;
	char prefix[UI_DN_SYM_MAX], outC[UI_DN_PATH_MAX], outH[UI_DN_PATH_MAX];
	wchar_t wc[UI_DN_PATH_MAX * 2], wh[UI_DN_PATH_MAX * 2];
	if (!doc || host < 0 || host >= doc->hostCount) return -1;
	ds_init(&c);
	ds_init(&h);
	if (cg_build_group(doc, -1, host, &c, &h, outC, sizeof(outC), outH,
	    sizeof(outH), prefix, sizeof(prefix), &count) < 0) {
		ds_free(&c); ds_free(&h);
		return -1;
	}
	if (!cg_join_path(dir, outC, wc, (int)(sizeof(wc) / sizeof(wc[0]))) ||
	    !cg_join_path(dir, outH, wh, (int)(sizeof(wh) / sizeof(wh[0])))) {
		ds_free(&c); ds_free(&h);
		return -1;
	}
	if (dn_write_utf8(wc, c.p ? c.p : "", c.n)) wrote++;
	if (dn_write_utf8(wh, h.p ? h.p : "", h.n)) wrote++;
	ds_free(&c); ds_free(&h);
	if (report) snprintf(report, cap, "host %s: %d file(s)",
		doc->hosts[host].name, wrote);
	return wrote == 2 ? 2 : -1;
}

int UiDesign_GenerateAll(const UiDesignDoc *doc, const wchar_t *dir,
	char *report, int cap)
{
	int i, total = 0;
	if (!doc) return -1;
	for (i = 0; i < doc->groupCount; i++) {
		int r = UiDesign_GenerateGroup(doc, i, dir, report, cap);
		if (r < 0) return -1;
		total += r;
	}
	for (i = 0; i < doc->hostCount; i++) {
		int r;
		if (doc->hosts[i].group >= 0) continue;
		r = UiDesign_GenerateHost(doc, i, dir, report, cap);
		if (r < 0) return -1;
		total += r;
	}
	if (report) snprintf(report, cap, "generated %d files", total);
	return total;
}

static int cg_line_count(const char *s, int n)
{
	int i, lines = 0;
	if (!s || n <= 0) return 0;
	for (i = 0; i < n; i++)
		if (s[i] == '\n') lines++;
	if (s[n - 1] != '\n') lines++;
	return lines;
}

static void cg_next_line(const char *s, int n, int *pos)
{
	while (*pos < n && s[*pos] != '\n') (*pos)++;
	if (*pos < n) (*pos)++;
}

/* 公共前缀/后缀行计数, 中间部分视为增删. */
static void cg_line_diff(const char *a, int an, const char *b, int bn,
	int *added, int *removed)
{
	int al = cg_line_count(a, an), bl = cg_line_count(b, bn);
	int ai = 0, bi = 0, pre = 0, suf = 0;
	int ae = an, be = bn;
	while (ai < an && bi < bn) {
		int as = ai, bs = bi;
		cg_next_line(a, an, &ai);
		cg_next_line(b, bn, &bi);
		if ((ai - as) != (bi - bs) ||
		    memcmp(a + as, b + bs, (size_t)(ai - as)) != 0) {
			ai = as;
			bi = bs;
			break;
		}
		pre++;
	}
	while (suf < al - pre && suf < bl - pre) {
		int as, bs, ae2, be2;
		ae2 = ae;
		if (ae2 > 0 && a[ae2 - 1] == '\n') ae2--;
		as = ae2;
		while (as > 0 && a[as - 1] != '\n') as--;
		be2 = be;
		if (be2 > 0 && b[be2 - 1] == '\n') be2--;
		bs = be2;
		while (bs > 0 && b[bs - 1] != '\n') bs--;
		if ((ae2 - as) != (be2 - bs) ||
		    memcmp(a + as, b + bs, (size_t)(ae2 - as)) != 0)
			break;
		ae = as;
		be = bs;
		suf++;
	}
	*added = bl - pre - suf;
	*removed = al - pre - suf;
	if (*added < 0) *added = 0;
	if (*removed < 0) *removed = 0;
}

static int cg_preview_one(const UiDesignDoc *doc, int group, int onlyHost,
	const wchar_t *dir, UiDesignGenFile *files, int maxFiles,
	int *stored, int *fresh, int *changed, int *same)
{
	UiDStr c, h;
	char prefix[UI_DN_SYM_MAX], outC[UI_DN_PATH_MAX], outH[UI_DN_PATH_MAX];
	wchar_t wc[UI_DN_PATH_MAX * 2], wh[UI_DN_PATH_MAX * 2];
	const char *texts[2], *names[2];
	wchar_t *paths[2];
	int k;
	ds_init(&c);
	ds_init(&h);
	if (cg_build_group(doc, group, onlyHost, &c, &h, outC, sizeof(outC), outH,
	    sizeof(outH), prefix, sizeof(prefix), NULL) < 0) {
		ds_free(&c); ds_free(&h);
		return -1;
	}
	if (!cg_join_path(dir, outC, wc, (int)(sizeof(wc) / sizeof(wc[0]))) ||
	    !cg_join_path(dir, outH, wh, (int)(sizeof(wh) / sizeof(wh[0])))) {
		ds_free(&c); ds_free(&h);
		return -1;
	}
	texts[0] = c.p ? c.p : "";
	texts[1] = h.p ? h.p : "";
	names[0] = outC;
	names[1] = outH;
	paths[0] = wc;
	paths[1] = wh;
	for (k = 0; k < 2; k++) {
		int oldLen = 0, newLen = k == 0 ? c.n : h.n;
		int newLines = cg_line_count(texts[k], newLen);
		int oldLines, status, added = 0, removed = 0;
		char *old = dn_read_utf8(paths[k], &oldLen);
		oldLines = old ? cg_line_count(old, oldLen) : 0;
		if (!old) {
			status = 0;
			(*fresh)++;
			added = newLines;
		} else if (oldLen == newLen &&
		    memcmp(old, texts[k], (size_t)newLen) == 0) {
			status = 2;
			(*same)++;
		} else {
			status = 1;
			(*changed)++;
			cg_line_diff(old, oldLen, texts[k], newLen, &added, &removed);
		}
		if (files && *stored < maxFiles) {
			UiDesignGenFile *f = &files[*stored];
			UiDesign_SetText(f->name, (int)sizeof(f->name), names[k]);
			f->status = status;
			f->oldLines = oldLines;
			f->newLines = newLines;
			f->added = added;
			f->removed = removed;
			(*stored)++;
		}
		if (old) dn_free(old);
	}
	ds_free(&c); ds_free(&h);
	return 2;
}

int UiDesign_PreviewAll(const UiDesignDoc *doc, const wchar_t *dir,
	UiDesignGenFile *files, int maxFiles, char *report, int cap)
{
	int i, total = 0, fresh = 0, changed = 0, same = 0, stored = 0;
	if (!doc) return -1;
	if (files && maxFiles > 0)
		memset(files, 0, sizeof(files[0]) * (size_t)maxFiles);
	for (i = 0; i < doc->groupCount; i++) {
		int r = cg_preview_one(doc, i, -1, dir, files, maxFiles,
			&stored, &fresh, &changed, &same);
		if (r < 0) return -1;
		total += r;
	}
	for (i = 0; i < doc->hostCount; i++) {
		int r;
		if (doc->hosts[i].group >= 0) continue;
		r = cg_preview_one(doc, -1, i, dir, files, maxFiles,
			&stored, &fresh, &changed, &same);
		if (r < 0) return -1;
		total += r;
	}
	if (report) snprintf(report, cap, "%d 个文件: 新增 %d, 更新 %d, 未变 %d",
		total, fresh, changed, same);
	return total;
}




