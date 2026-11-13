/*
 * heap.h - 侵入式最小堆 (0CRT, 通用)
 *
 * 按 (deadline, seq) 排序; 节点由调用方分配/复用, idx==-1 表示不在堆中。
 * net 的 IO 定时器与 ui 的 UiTimer 共用。
 */
#ifndef CUI_LIB_HEAP_H
#define CUI_LIB_HEAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiHeapNode {
	int      idx;
	uint64_t deadline;
	uint64_t seq;
	void    *ptr;
} UiHeapNode;

typedef struct UiHeap {
	UiHeapNode **a;
	int n, cap;
} UiHeap;

void UiHeap_Init(UiHeap *h);
void UiHeap_Free(UiHeap *h);
int  UiHeap_Push(UiHeap *h, UiHeapNode *e);      /* 1=ok, 已在堆中返回 1 */
void UiHeap_Erase(UiHeap *h, UiHeapNode *e);
void UiHeap_Adjust(UiHeap *h, UiHeapNode *e);    /* deadline/seq 改变后 */
UiHeapNode *UiHeap_Pop(UiHeap *h);
UiHeapNode *UiHeap_Top(UiHeap *h);

static __inline int UiHeap_Empty(const UiHeap *h) { return h->n == 0; }
static __inline uint64_t UiHeap_TopDeadline(const UiHeap *h)
{
	return h->n ? h->a[0]->deadline : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* CUI_LIB_HEAP_H */
