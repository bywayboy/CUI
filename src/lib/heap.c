/*
 * heap.c - 侵入式最小堆 (0CRT)
 */
#include "lib/heap.h"

#include <limits.h>

void *malloc(size_t sz);
void *realloc(void *p, size_t sz);
void free(void *p);

static int heap_cmp_(const UiHeapNode *a, const UiHeapNode *b)
{
	if (a->deadline != b->deadline)
		return a->deadline < b->deadline ? -1 : 1;
	if (a->seq != b->seq)
		return a->seq < b->seq ? -1 : 1;
	return 0;
}

void UiHeap_Init(UiHeap *h)
{
	h->a = 0;
	h->n = 0;
	h->cap = 0;
}

void UiHeap_Free(UiHeap *h)
{
	if (h->a) free(h->a);
	h->a = 0;
	h->n = 0;
	h->cap = 0;
}

static int heap_reserve_(UiHeap *h, int need)
{
	int cap;
	UiHeapNode **na;

	if (need <= h->cap) return 1;
	cap = h->cap > 0 ? h->cap : 16;
	while (cap < need) {
		if (cap > INT_MAX / 2) { cap = need; break; }
		cap *= 2;
	}
	na = (UiHeapNode **)realloc(h->a, (size_t)cap * sizeof(*na));
	if (!na) return 0;
	h->a = na;
	h->cap = cap;
	return 1;
}

static void set_(UiHeap *h, int i, UiHeapNode *e)
{
	h->a[i] = e;
	e->idx = i;
}

static void up_(UiHeap *h, int i)
{
	UiHeapNode *e = h->a[i];
	while (i > 0) {
		int p = (i - 1) / 2;
		if (heap_cmp_(h->a[p], e) <= 0) break;
		set_(h, i, h->a[p]);
		i = p;
	}
	set_(h, i, e);
}

static void down_(UiHeap *h, int i)
{
	for (;;) {
		int l = i * 2 + 1, r = l + 1, m = i;
		UiHeapNode *t;
		if (l < h->n && heap_cmp_(h->a[l], h->a[m]) < 0) m = l;
		if (r < h->n && heap_cmp_(h->a[r], h->a[m]) < 0) m = r;
		if (m == i) break;
		t = h->a[i];
		set_(h, i, h->a[m]);
		set_(h, m, t);
		i = m;
	}
}

int UiHeap_Push(UiHeap *h, UiHeapNode *e)
{
	if (!e) return 0;
	if (e->idx >= 0 && e->idx < h->n && h->a[e->idx] == e)
		return 1;
	if (!heap_reserve_(h, h->n + 1)) return 0;
	e->idx = h->n;
	h->a[h->n++] = e;
	up_(h, e->idx);
	return 1;
}

void UiHeap_Erase(UiHeap *h, UiHeapNode *e)
{
	int i, last;
	UiHeapNode *moved;

	if (!e || e->idx < 0 || e->idx >= h->n || h->a[e->idx] != e) return;
	i = e->idx;
	last = --h->n;
	e->idx = -1;
	if (i == last) return;
	moved = h->a[last];
	set_(h, i, moved);
	if (i > 0 && heap_cmp_(h->a[i], h->a[(i - 1) / 2]) < 0)
		up_(h, i);
	else
		down_(h, i);
}

void UiHeap_Adjust(UiHeap *h, UiHeapNode *e)
{
	int i;

	if (!e) return;
	if (e->idx < 0 || e->idx >= h->n || h->a[e->idx] != e) {
		UiHeap_Push(h, e);
		return;
	}
	i = e->idx;
	if (i > 0 && heap_cmp_(h->a[i], h->a[(i - 1) / 2]) < 0)
		up_(h, i);
	else
		down_(h, i);
}

UiHeapNode *UiHeap_Pop(UiHeap *h)
{
	UiHeapNode *top;

	if (h->n <= 0) return 0;
	top = h->a[0];
	UiHeap_Erase(h, top);
	return top;
}

UiHeapNode *UiHeap_Top(UiHeap *h)
{
	return h->n > 0 ? h->a[0] : 0;
}
