/*
 * ui_design.c - CUI 设计器运行时: 模型 / JSON / 布局 / 运行时 / 代码生成
 */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include "ui/ui_design.h"
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
	dn_free(doc->windows);
	dn_free(doc->nodes);
	memset(doc, 0, sizeof(*doc));
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
	{ "separator", 2, 2, 0 }
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

int UiDesign_AddWindow(UiDesignDoc *doc, const char *id, const char *name,
	int kind, int group)
{
	UiDesignWindow *w;
	int idx;
	if (!doc || !dn_reserve((void **)&doc->windows, &doc->windowCap,
		doc->windowCount + 1, sizeof(UiDesignWindow))) return -1;
	idx = doc->windowCount++;
	w = &doc->windows[idx];
	memset(w, 0, sizeof(*w));
	UiDesign_SetText(w->id, UI_DN_ID_MAX, id ? id : "window");
	UiDesign_SetText(w->name, UI_DN_NAME_MAX, name ? name : id ? id : "窗口");
	UiDesign_SetText(w->title, 128, name ? name : id ? id : "窗口");
	w->kind = kind == UI_DW_DIALOG ? UI_DW_DIALOG : UI_DW_NATIVE;
	w->group = group;
	w->w = 800.0f;
	w->h = 540.0f;
	w->minW = 320.0f;
	w->minH = 240.0f;
	w->theme = UI_THEME_LIGHT;
	w->root = -1;
	return idx;
}

UiDesignNode *UiDesign_Node(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->nodeCount) return NULL;
	return &doc->nodes[index];
}

UiDesignWindow *UiDesign_Window(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->windowCount) return NULL;
	return &doc->windows[index];
}

UiDesignGroup *UiDesign_Group(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->groupCount) return NULL;
	return &doc->groups[index];
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

int UiDesign_AddNode(UiDesignDoc *doc, int win, int type, int parent,
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
	n->owner = win;
	n->parent = -1;
	n->visible = 1;
	n->enabled = 1;
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FIXED;
	n->w = UiDesign_DefaultW(type);
	n->h = UiDesign_DefaultH(type);
	n->align = UI_DN_ALIGN_START;
	n->gap = 8.0f;
	n->gridRows = 1;
	n->gridCols = 1;
	n->gridRow = -1;
	n->gridCol = -1;
	n->rowSpan = 1;
	n->colSpan = 1;
	n->value = 0.5f;
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

int UiDesign_NodeRemove(UiDesignDoc *doc, int index)
{
	if (!doc || index < 0 || index >= doc->nodeCount) return 0;
	if (doc->nodes[index].parent < 0) return 0;    /* 根节点不删 */
	dn_remove_subtree(doc, index);
	return 1;
}

int UiDesign_NodeMove(UiDesignDoc *doc, int index, int parent, int pos)
{
	UiDesignNode *p;
	int i, j;
	if (!doc || index < 0 || index >= doc->nodeCount ||
	    parent < 0 || parent >= doc->nodeCount) return 0;
	if (index == parent) return 0;
	/* 禁止移到自己的子树 */
	for (i = parent; i >= 0; i = doc->nodes[i].parent)
		if (i == index) return 0;
	UiDesign_NodeDetach(doc, index);
	p = &doc->nodes[parent];
	if (!dn_reserve((void **)&p->children, &p->childCap,
		p->childCount + 1, sizeof(int))) return 0;
	if (pos < 0 || pos > p->childCount) pos = p->childCount;
	for (j = p->childCount; j > pos; j--) p->children[j] = p->children[j - 1];
	p->children[pos] = index;
	p->childCount++;
	doc->nodes[index].parent = parent;
	return 1;
}

int UiDesign_NodeMoveUp(UiDesignDoc *doc, int index)
{
	UiDesignNode *n;
	int i, p;
	if (!doc || index < 0 || index >= doc->nodeCount) return 0;
	n = &doc->nodes[index];
	p = n->parent;
	if (p < 0) return 0;
	for (i = 0; i < doc->nodes[p].childCount; i++)
		if (doc->nodes[p].children[i] == index) break;
	if (i <= 0 || i >= doc->nodes[p].childCount) return 0;
	{
		int t = doc->nodes[p].children[i - 1];
		doc->nodes[p].children[i - 1] = index;
		doc->nodes[p].children[i] = t;
	}
	return 1;
}

int UiDesign_NodeMoveDown(UiDesignDoc *doc, int index)
{
	UiDesignNode *n;
	int i, p;
	if (!doc || index < 0 || index >= doc->nodeCount) return 0;
	n = &doc->nodes[index];
	p = n->parent;
	if (p < 0) return 0;
	for (i = 0; i < doc->nodes[p].childCount; i++)
		if (doc->nodes[p].children[i] == index) break;
	if (i < 0 || i + 1 >= doc->nodes[p].childCount) return 0;
	{
		int t = doc->nodes[p].children[i + 1];
		doc->nodes[p].children[i + 1] = index;
		doc->nodes[p].children[i] = t;
	}
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

int UiDesign_FindNode(const UiDesignDoc *doc, int win, const char *id)
{
	int i;
	if (!doc || !id) return -1;
	for (i = 0; i < doc->nodeCount; i++)
		if (doc->nodes[i].owner == win && strcmp(doc->nodes[i].id, id) == 0)
			return i;
	return -1;
}

int UiDesign_FindWindow(const UiDesignDoc *doc, const char *id)
{
	int i;
	if (!doc || !id) return -1;
	for (i = 0; i < doc->windowCount; i++)
		if (strcmp(doc->windows[i].id, id) == 0) return i;
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
	int g, w, root, label, col;
	UiDesignNode *n;
	UiDesignWindow *win;
	UiDesignGroup *grp;
	if (!doc) return 0;
	UiDesign_Init(doc);
	UiDesign_SetText(doc->name, UI_DN_NAME_MAX, "未命名工程");
	g = UiDesign_AddGroup(doc, "main", "主分组");
	w = UiDesign_AddWindow(doc, "main", "主窗口", UI_DW_NATIVE, g);
	if (g < 0 || w < 0) return 0;
	grp = UiDesign_Group(doc, g);
	UiDesign_SetText(grp->outputC, UI_DN_PATH_MAX, "main.ui.c");
	UiDesign_SetText(grp->outputH, UI_DN_PATH_MAX, "main.ui.h");
	UiDesign_SetText(grp->symbolPrefix, UI_DN_SYM_MAX, "Main");
	win = UiDesign_Window(doc, w);
	win->root = UiDesign_AddNode(doc, w, UI_DN_COLUMN, -1, "root", "根容器");
	root = win->root;
	n = UiDesign_Node(doc, root);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FILL;
	n->padding[0] = n->padding[1] = n->padding[2] = n->padding[3] = 16.0f;
	n->gap = 8.0f;
	label = UiDesign_AddNode(doc, w, UI_DN_LABEL, root, "title", "标题");
	n = UiDesign_Node(doc, label);
	n->wMode = UI_DN_SIZE_FILL;
	n->hMode = UI_DN_SIZE_FIXED;
	n->h = 28.0f;
	UiDesign_SetText(n->text, UI_DN_TEXT_MAX, "新建窗口");
	col = UiDesign_AddNode(doc, w, UI_DN_BUTTON, root, "ok", "确定按钮");
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
	ds_add(s, ",\"margin\":[");
	for (i = 0; i < 4; i++) { if (i) ds_add(s, ","); ds_num(s, n->margin[i]); }
	ds_add(s, "],\"padding\":[");
	for (i = 0; i < 4; i++) { if (i) ds_add(s, ","); ds_num(s, n->padding[i]); }
	ds_add(s, "],\"gap\":"); ds_num(s, n->gap);
	ds_add(s, ",\"x\":"); ds_num(s, n->x);
	ds_add(s, ",\"y\":"); ds_num(s, n->y);
	ds_addf(s, ",\"anchors\":%u", n->anchors);
	ds_addf(s, ",\"gridRows\":%d,\"gridCols\":%d", n->gridRows, n->gridCols);
	ds_add(s, ",\"gridRowGap\":"); ds_num(s, n->gridRowGap);
	ds_add(s, ",\"gridColGap\":"); ds_num(s, n->gridColGap);
	ds_addf(s, ",\"gridRow\":%d,\"gridCol\":%d,\"rowSpan\":%d,\"colSpan\":%d",
		n->gridRow, n->gridCol, n->rowSpan, n->colSpan);
	ds_add(s, "},\"props\":{\"text\":");
	ds_json_str(s, n->text);
	ds_add(s, ",\"hint\":");
	ds_json_str(s, n->hint);
	ds_add(s, ",\"options\":");
	ds_json_str(s, n->options);
	ds_add(s, ",\"value\":"); ds_num(s, n->value);
	ds_addf(s, ",\"checked\":%d,\"enabled\":%d,\"selected\":%d,\"variant\":%d",
		n->checked, n->enabled, n->selected, n->variant);
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
	ds_add(&s, "{\n  \"format\":\"cui-design\",\n  \"version\":1,\n  \"name\":");
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
	ds_add(&s, "\n  ],\n  \"windows\":[");
	for (i = 0; i < doc->windowCount; i++) {
		const UiDesignWindow *w = &doc->windows[i];
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
		ds_addf(&s, ",\"theme\":%d,\"root\":", w->theme);
		ds_node(&s, doc, w->root);
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
	int gotType = 0;
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
				else if (strcmp(lk, "margin") == 0) jp_array_float(j, doc->nodes[idx].margin, 4);
				else if (strcmp(lk, "padding") == 0) jp_array_float(j, doc->nodes[idx].padding, 4);
				else if (strcmp(lk, "gap") == 0) jp_number(j, &doc->nodes[idx].gap);
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
				else if (strcmp(pk, "checked") == 0) jp_bool(j, &doc->nodes[idx].checked);
				else if (strcmp(pk, "enabled") == 0) jp_bool(j, &doc->nodes[idx].enabled);
				else if (strcmp(pk, "selected") == 0) jp_int(j, &doc->nodes[idx].selected);
				else if (strcmp(pk, "variant") == 0) jp_int(j, &doc->nodes[idx].variant);
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
	return 1;
}

int UiDesign_FromJson(UiDesignDoc *doc, const char *text)
{
	JP j;
	char key[64], val[UI_DN_PATH_MAX];
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
		} else if (strcmp(key, "windows") == 0) {
			if (!jp_ch(&j, '[')) return 0;
			jp_ws(&j);
			if (jp_ch(&j, ']')) continue;
			for (;;) {
				int wi, gi = -1;
				char group[UI_DN_ID_MAX] = "";
				jp_ws(&j);
				if (!jp_ch(&j, '{')) return 0;
				wi = UiDesign_AddWindow(doc, "window", "窗口", UI_DW_NATIVE, -1);
				for (;;) {
					char wk[48];
					jp_ws(&j);
					if (jp_ch(&j, '}')) break;
					if (!jp_key(&j, wk, sizeof(wk))) return 0;
					if (strcmp(wk, "id") == 0) jp_string(&j, doc->windows[wi].id, UI_DN_ID_MAX);
					else if (strcmp(wk, "name") == 0) jp_string(&j, doc->windows[wi].name, UI_DN_NAME_MAX);
					else if (strcmp(wk, "title") == 0) jp_string(&j, doc->windows[wi].title, 128);
					else if (strcmp(wk, "kind") == 0) jp_int(&j, &doc->windows[wi].kind);
					else if (strcmp(wk, "group") == 0) {
						jp_ws(&j);
						if (j.p < j.end && *j.p == '"') {
							if (jp_string(&j, group, sizeof(group)))
								gi = UiDesign_FindGroup(doc, group);
						} else if (!jp_skip(&j)) return 0;
					} else if (strcmp(wk, "width") == 0) jp_number(&j, &doc->windows[wi].w);
					else if (strcmp(wk, "height") == 0) jp_number(&j, &doc->windows[wi].h);
					else if (strcmp(wk, "minWidth") == 0) jp_number(&j, &doc->windows[wi].minW);
					else if (strcmp(wk, "minHeight") == 0) jp_number(&j, &doc->windows[wi].minH);
					else if (strcmp(wk, "theme") == 0) jp_int(&j, &doc->windows[wi].theme);
					else if (strcmp(wk, "root") == 0) {
						int root = -1;
						jp_ws(&j);
						if (j.p < j.end && *j.p == 'n') {
							jp_skip(&j);
							root = -1;
						} else if (jp_node(&j, doc, wi, -1, &root)) {
							doc->windows[wi].root = root;
						}
					} else if (!jp_skip(&j)) return 0;
					jp_ws(&j);
					if (jp_ch(&j, ',')) continue;
				}
				doc->windows[wi].group = gi;
				doc->windows[wi].kind = doc->windows[wi].kind == UI_DW_DIALOG
					? UI_DW_DIALOG : UI_DW_NATIVE;
				jp_ws(&j);
				if (jp_ch(&j, ']')) break;
				if (!jp_ch(&j, ',')) return 0;
			}
		} else if (!jp_skip(&j)) return 0;
		jp_ws(&j);
		if (jp_ch(&j, ',')) continue;
	}
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

static void dn_layout_grid(const UiDesignDoc *doc, const UiDesignNode *p,
	UiRect inner, UiRect *rects)
{
	int rows = p->gridRows > 0 ? p->gridRows : 1;
	int cols = p->gridCols > 0 ? p->gridCols : 1;
	float rowGap = p->gridRowGap;
	float colGap = p->gridColGap;
	float cellW = (inner.w - colGap * (cols - 1)) / cols;
	float cellH = (inner.h - rowGap * (rows - 1)) / rows;
	int i;
	if (cellW < 0.0f) cellW = 0.0f;
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
		cell.x = inner.x + c * (cellW + colGap);
		cell.y = inner.y + r * (cellH + rowGap);
		cell.w = cellW * cs + colGap * (cs - 1);
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
	switch (n->type) {
	case UI_DN_COLUMN: dn_layout_linear(doc, n, inner, 0, rects); break;
	case UI_DN_ROW: dn_layout_linear(doc, n, inner, 1, rects); break;
	case UI_DN_GRID: dn_layout_grid(doc, n, inner, rects); break;
	default: dn_layout_absolute(doc, n, inner, rects); break;
	}
}

int UiDesign_Layout(const UiDesignDoc *doc, int win, UiRect client,
	UiRect *rects, int rectCap)
{
	UiDesignWindow *w;
	int i;
	if (!doc || !rects || win < 0 || win >= doc->windowCount) return 0;
	if (rectCap < doc->nodeCount) return 0;
	for (i = 0; i < doc->nodeCount; i++) rects[i] = Ui_Rect(0, 0, 0, 0);
	w = &doc->windows[win];
	if (w->root >= 0) dn_layout_node(doc, w->root, client, rects);
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
	for (i = n->childCount - 1; i >= 0; i--) {
		int hit = dn_hit_node(doc, n->children[i], rects, x, y);
		if (hit >= 0) return hit;
	}
	if (Ui_RectContains(rects[index], x, y)) return index;
	return -1;
}

int UiDesign_FindNodeAt(const UiDesignDoc *doc, int win,
	const UiRect *rects, float x, float y)
{
	if (!doc || !rects || win < 0 || win >= doc->windowCount) return -1;
	return dn_hit_node(doc, doc->windows[win].root, rects, x, y);
}

/* ── 运行时 ─────────────────────────────────────────────────────────── */

static UiId dn_widget_id(const UiDesignInstance *inst, int idx, int kind)
{
	return Ui_IdIndex("cui.design", inst->win, idx, kind);
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
	default:
		break;
	}
}

int UiDesignInstance_Init(UiDesignInstance *inst, UiDesignDoc *doc,
	int win, UiContext *ui, UiPopupManager *pm, UiIme *ime)
{
	if (!inst) return 0;
	memset(inst, 0, sizeof(*inst));
	if (!doc || !ui || win < 0 || win >= doc->windowCount) return 0;
	inst->doc = doc;
	inst->win = win;
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
	if (doc->windows[win].kind == UI_DW_DIALOG) {
		UiDialog_Init(&inst->dialog, Ui_Id("cui.design.dialog", win, 0));
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
	idx = UiDesign_FindNode(inst->doc, inst->win, nodeId);
	if (idx < 0) return "";
	if (inst->states[idx].inited && inst->doc->nodes[idx].type == UI_DN_INPUT)
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
	idx = UiDesign_FindNode(inst->doc, inst->win, nodeId);
	if (idx < 0) return;
	UiDesign_NodeSetText(&inst->doc->nodes[idx], text);
	if (!inst->states[idx].inited)
		dn_state_init(inst, idx);
	st = &inst->states[idx];
	UiDesign_SetText(st->text, UI_DN_TEXT_MAX, text);
	if (inst->doc->nodes[idx].type == UI_DN_INPUT)
		Edit_SetTextUtf8(UiInput_Edit(&st->input, 0), text);
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
	if (UiDesign_IsContainer(n->type)) {
		if (n->type == UI_DN_PANEL)
			Gfx_FillRoundRect(g, r, 8.0f, t->card);
		for (i = 0; i < n->childCount; i++)
			dn_draw_node(inst, n->children[i]);
		return;
	}
	switch (n->type) {
	case UI_DN_LABEL: {
		uint32_t flags = GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS;
		if (n->align == UI_DN_ALIGN_CENTER) flags |= GFX_TEXT_CENTER;
		else if (n->align == UI_DN_ALIGN_END) flags |= GFX_TEXT_RIGHT;
		Gfx_Text(g, t->fBody, n->text, r,
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
		if (!st) break;
		UiInput_SetRect(&st->input, 0, r);
		UiInput_Draw(&st->input, 0);
		Edit_TextUtf8(UiInput_Edit(&st->input, 0), st->text,
			UI_DN_TEXT_MAX);
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
	case UI_DN_IMAGE:
		Gfx_FillRoundRect(g, r, 6.0f, Ui_ColorMix(t->card, t->primary.soft, 0.35f));
		Gfx_StrokeRoundRect(g, r, 6.0f, t->border, 1.0f);
		Gfx_Text(g, t->fSmall, n->text[0] ? n->text : "Image", r,
			t->text3, GFX_TEXT_CENTER | GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
		break;
	case UI_DN_SEPARATOR:
		Gfx_Line(g, r.x, r.y + r.h * 0.5f, r.x + r.w, r.y + r.h * 0.5f,
			t->border, 1.0f);
		break;
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
	UiDesign_Layout(inst->doc, inst->win, rc, inst->rects, inst->stateCount);
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && st->input.focus >= 0) {
			Ui_SetFocusId(inst->ui, dn_widget_id(inst, i, 1));
			break;
		}
	}
	if (inst->doc->windows[inst->win].root >= 0)
		dn_draw_node(inst, inst->doc->windows[inst->win].root);
}

void UiDesign_DrawOverlay(UiDesignInstance *inst)
{
	int i;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_SELECT)
			UiSelect_DrawPopup(&st->select, inst->pm, inst->ui);
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
	UiDesignWindow *w;
	UiRect body;
	if (!inst || !inst->dialogReady || !UiDialog_IsOpen(&inst->dialog)) return;
	w = &inst->doc->windows[inst->win];
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

void UiDesign_MouseDown(UiDesignInstance *inst, float x, float y)
{
	UiId hit;
	int target, i;
	if (!inst || !inst->ui) return;
	hit = Ui_HitTest(inst->ui, x, y);
	target = dn_hit_input(inst, hit);
	if (target >= 0) {
		for (i = 0; i < inst->stateCount; i++)
			if (i != target) inst->states[i].input.focus = -1;
	}
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_INPUT)
			UiInput_MouseDown(&st->input, hit, x);
	}
}

void UiDesign_MouseMove(UiDesignInstance *inst, float x, float y)
{
	int i;
	(void)y;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++) {
		UiDesignNodeState *st = &inst->states[i];
		if (st->inited && inst->doc->nodes[i].type == UI_DN_INPUT)
			UiInput_MouseMove(&st->input, x);
	}
}

void UiDesign_MouseUp(UiDesignInstance *inst)
{
	int i;
	if (!inst) return;
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
	int i = dn_focus_index(inst);
	if (i < 0) return 0;
	return UiInput_Key(&inst->states[i].input, inst->ui->hwnd, vk);
}

int UiDesign_Char(UiDesignInstance *inst, wchar_t ch)
{
	int i = dn_focus_index(inst);
	if (i < 0) return 0;
	return UiInput_Char(&inst->states[i].input, ch);
}

void UiDesign_OnIme(UiDesignInstance *inst)
{
	int i = dn_focus_index(inst);
	if (i >= 0) UiInput_OnIme(&inst->states[i].input);
}

void UiDesign_Blink(UiDesignInstance *inst)
{
	int i;
	if (!inst) return;
	for (i = 0; i < inst->stateCount; i++)
		if (inst->states[i].inited &&
		    inst->doc->nodes[i].type == UI_DN_INPUT)
			UiInput_Blink(&inst->states[i].input);
}

int UiDesign_Busy(const UiDesignInstance *inst)
{
	int i;
	if (!inst) return 0;
	for (i = 0; i < inst->stateCount; i++)
		if (inst->states[i].inited &&
		    inst->doc->nodes[i].type == UI_DN_SELECT &&
		    UiSelect_Busy(&inst->states[i].select)) return 1;
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

static void cg_node(UiDStr *s, const UiDesignDoc *doc, int old, int winNew,
	int parentOld, int *rootOut)
{
	const UiDesignNode *n = &doc->nodes[old];
	int i;
	ds_addf(s, "    int n%d = UiDesign_AddNode(&gDoc, %d, ",
		old, winNew);
	ds_addf(s, "%d, ", n->type);
	if (parentOld >= 0) ds_addf(s, "n%d, ", parentOld);
	else ds_add(s, "-1, ");
	cg_quote(s, n->id);
	ds_add(s, ", ");
	cg_quote(s, n->name);
	ds_add(s, ");\n");
	ds_addf(s, "    UiDesignNode *p%d = UiDesign_Node(&gDoc, n%d);\n", old, old);
	ds_addf(s, "    p%d->type=%d; p%d->visible=%d; p%d->locked=%d; p%d->enabled=%d;\n",
		old, n->type, old, n->visible, old, n->locked, old, n->enabled);
	ds_addf(s, "    p%d->wMode=%d; p%d->hMode=%d; p%d->w=", old, n->wMode, old, n->hMode, old);
	cg_num(s, n->w);
	ds_addf(s, "; p%d->h=", old); cg_num(s, n->h);
	ds_addf(s, "; p%d->minW=", old); cg_num(s, n->minW);
	ds_addf(s, "; p%d->minH=", old); cg_num(s, n->minH);
	ds_addf(s, "; p%d->maxW=", old); cg_num(s, n->maxW);
	ds_addf(s, "; p%d->maxH=", old); cg_num(s, n->maxH);
	ds_addf(s, "; p%d->grow=", old); cg_num(s, n->grow);
	ds_addf(s, "; p%d->align=%d; p%d->gap=", old, n->align, old);
	cg_num(s, n->gap);
	ds_add(s, ";\n");
	for (i = 0; i < 4; i++) {
		ds_addf(s, "    p%d->margin[%d]=", old, i); cg_num(s, n->margin[i]); ds_add(s, ";");
	}
	ds_add(s, "\n");
	for (i = 0; i < 4; i++) {
		ds_addf(s, "    p%d->padding[%d]=", old, i); cg_num(s, n->padding[i]); ds_add(s, ";");
	}
	ds_add(s, "\n");
	ds_addf(s, "    p%d->x=", old); cg_num(s, n->x);
	ds_addf(s, "; p%d->y=", old); cg_num(s, n->y);
	ds_addf(s, "; p%d->anchors=%u;\n", old, n->anchors);
	ds_addf(s, "    p%d->gridRows=%d; p%d->gridCols=%d; p%d->gridRowGap=",
		old, n->gridRows, old, n->gridCols);
	cg_num(s, n->gridRowGap);
	ds_addf(s, "; p%d->gridColGap=", old); cg_num(s, n->gridColGap);
	ds_addf(s, "; p%d->gridRow=%d; p%d->gridCol=%d; p%d->rowSpan=%d; p%d->colSpan=%d;\n",
		old, n->gridRow, old, n->gridCol, old, n->rowSpan, old, n->colSpan);
	ds_addf(s, "    p%d->value=", old); cg_num(s, n->value);
	ds_addf(s, "; p%d->checked=%d; p%d->selected=%d; p%d->variant=%d;\n",
		old, n->checked, old, n->selected, old, n->variant);
	ds_addf(s, "    UiDesign_NodeSetText(p%d, ", old); cg_quote(s, n->text); ds_add(s, ");\n");
	ds_addf(s, "    UiDesign_NodeSetHint(p%d, ", old); cg_quote(s, n->hint); ds_add(s, ");\n");
	ds_addf(s, "    UiDesign_NodeSetOptions(p%d, ", old); cg_quote(s, n->options); ds_add(s, ");\n");
	ds_addf(s, "    UiDesign_SetText(p%d->onClick, UI_DN_ID_MAX, ", old);
	cg_quote(s, n->onClick); ds_add(s, ");\n");
	ds_addf(s, "    UiDesign_SetText(p%d->onChange, UI_DN_ID_MAX, ", old);
	cg_quote(s, n->onChange); ds_add(s, ");\n");
	if (rootOut) *rootOut = old;
	for (i = 0; i < n->childCount; i++)
		cg_node(s, doc, n->children[i], winNew, old, NULL);
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

static int cg_build_group(const UiDesignDoc *doc, int group, UiDStr *c, UiDStr *h,
	char *outC, int outCCap, char *outH, int outHCap,
	char *prefix, int prefixCap, int *winCount)
{
	int i, count = 0;
	const UiDesignGroup *g;
	char baseH[UI_DN_PATH_MAX];
	if (!doc || group < 0 || group >= doc->groupCount) return -1;
	g = &doc->groups[group];
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
	cg_base_name(outH, baseH, sizeof(baseH));

	ds_addf(c, "/* Generated by CUI Designer. Source: %s */\n", doc->name);
	ds_add(c, "#include \"ui/ui_design.h\"\n");
	ds_add(c, "#include \""); ds_add(c, baseH); ds_add(c, "\"\n\n");
	ds_add(c, "static UiDesignDoc gDoc;\nstatic int gBuilt;\n\n");
	ds_add(c, "static void build_doc(void)\n{\n");
	ds_add(c, "    UiDesign_Init(&gDoc);\n");
	ds_add(c, "    UiDesign_AddGroup(&gDoc, "); cg_quote(c, g->id);
	ds_add(c, ", "); cg_quote(c, g->name); ds_add(c, ");\n");
	for (i = 0; i < doc->windowCount; i++) {
		const UiDesignWindow *w = &doc->windows[i];
		int n0, root = -1;
		if (w->group != group) continue;
		n0 = count++;
		ds_addf(c, "    { int w%d = UiDesign_AddWindow(&gDoc, ", n0);
		cg_quote(c, w->id); ds_add(c, ", "); cg_quote(c, w->name);
		ds_addf(c, ", %d, 0);\n", w->kind);
		ds_addf(c, "      UiDesignWindow *pw%d = UiDesign_Window(&gDoc, w%d);\n", n0, n0);
		ds_addf(c, "      pw%d->w=", n0); cg_num(c, w->w);
		ds_addf(c, "; pw%d->h=", n0); cg_num(c, w->h);
		ds_addf(c, "; pw%d->minW=", n0); cg_num(c, w->minW);
		ds_addf(c, "; pw%d->minH=", n0); cg_num(c, w->minH);
		ds_addf(c, "; pw%d->theme=%d; pw%d->kind=%d;\n", n0, w->theme, n0, w->kind);
		ds_addf(c, "      UiDesign_SetText(pw%d->title, 128, ", n0);
		cg_quote(c, w->title); ds_add(c, ");\n");
		if (w->root >= 0) {
			int oldRoot = w->root;
			cg_node(c, doc, oldRoot, n0, -1, &root);
			ds_addf(c, "      pw%d->root=n%d;\n", n0, oldRoot);
		}
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
		for (i = 0; i < doc->windowCount; i++)
			if (doc->windows[i].group == group) {
				char wu[UI_DN_SYM_MAX];
				cg_upper(doc->windows[i].id, wu, sizeof(wu));
				ds_addf(h, "#define %s_WIN_%s %d\n", prefix, wu, wi++);
			}
	}
	ds_add(h, "\n");
	count = 0;
	for (i = 0; i < doc->windowCount; i++) {
		const UiDesignWindow *w = &doc->windows[i];
		char ws[UI_DN_SYM_MAX];
		if (w->group != group) continue;
		UiDesign_Symbol(w->id, ws, sizeof(ws));
		ds_addf(h, "void %s_%s_Init(UiDesignInstance *inst, UiContext *ui, UiPopupManager *pm, UiIme *ime);\n", prefix, ws);
		if (w->kind == UI_DW_DIALOG) {
			ds_addf(h, "int  %s_%s_DialogOpen(UiDesignInstance *inst, UiId owner);\n", prefix, ws);
			ds_addf(h, "void %s_%s_DialogDraw(UiDesignInstance *inst);\n", prefix, ws);
		} else {
			ds_addf(h, "void %s_%s_Draw(UiDesignInstance *inst, UiRect rc);\n", prefix, ws);
		}
		ds_add(h, "\n");
		ds_addf(c, "void %s_%s_Init(UiDesignInstance *inst, UiContext *ui, UiPopupManager *pm, UiIme *ime)\n{\n", prefix, ws);
		ds_addf(c, "    UiDesignInstance_Init(inst, %s_Doc(), %d, ui, pm, ime);\n}\n\n", prefix, count);
		if (w->kind == UI_DW_DIALOG) {
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
	if (winCount) *winCount = count;
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
	if (cg_build_group(doc, group, &c, &h, outC, sizeof(outC), outH,
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
	if (report) snprintf(report, cap, "group %s: %d file(s), %d window(s)",
		doc->groups[group].name, wrote, count);
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

int UiDesign_PreviewAll(const UiDesignDoc *doc, const wchar_t *dir,
	UiDesignGenFile *files, int maxFiles, char *report, int cap)
{
	int gi, total = 0, fresh = 0, changed = 0, same = 0, stored = 0;
	if (!doc) return -1;
	if (files && maxFiles > 0) memset(files, 0, sizeof(files[0]) * (size_t)maxFiles);
	for (gi = 0; gi < doc->groupCount; gi++) {
		UiDStr c, h;
		char prefix[UI_DN_SYM_MAX], outC[UI_DN_PATH_MAX], outH[UI_DN_PATH_MAX];
		wchar_t wc[UI_DN_PATH_MAX * 2], wh[UI_DN_PATH_MAX * 2];
		const char *texts[2];
		const char *names[2];
		wchar_t *paths[2];
		int k;
		ds_init(&c);
		ds_init(&h);
		if (cg_build_group(doc, gi, &c, &h, outC, sizeof(outC), outH,
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
			char *old = dn_read_utf8(paths[k], &oldLen);
			UiDesignGenFile *f = NULL;
			if (files && stored < maxFiles) f = &files[stored];
			if (f) {
				UiDesign_SetText(f->name, (int)sizeof(f->name), names[k]);
				f->newLines = cg_line_count(texts[k], newLen);
				f->oldLines = old ? cg_line_count(old, oldLen) : 0;
				if (!old) { f->status = 0; fresh++; }
				else if (oldLen == newLen && memcmp(old, texts[k], (size_t)newLen) == 0) {
					f->status = 2; same++;
				} else { f->status = 1; changed++; }
				stored++;
			} else {
				if (!old) fresh++;
				else if (oldLen == newLen && memcmp(old, texts[k], (size_t)newLen) == 0) same++;
				else changed++;
			}
			if (old) dn_free(old);
			total++;
		}
		ds_free(&c); ds_free(&h);
	}
	if (report) snprintf(report, cap, "%d 个文件: 新增 %d, 更新 %d, 未变 %d",
		total, fresh, changed, same);
	return total;
}




