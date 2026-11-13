/*
 * ui_timer.c - GUI 线程定时器 (0CRT)
 */
#include "ui/ui_timer.h"
#include "lib/heap.h"

void *malloc(size_t sz);
void free(void *p);

typedef struct UiTimerNode {
	UiHeapNode hn;          /* 必须是首成员: 由 hn 反推节点 */
	UiTimer *owner;
	uint64_t id;
	uint64_t interval;
	int repeat;
	int canceled;
	UiTimerFn fn;
	void *user;
} UiTimerNode;

struct UiTimer {
	UiHeap heap;
	HANDLE ht;
	uint64_t nextId;
	uint64_t seq;
	UiTimerNode *cur;       /* 正在执行的回调节点 (支持回调内 Cancel) */
	int dead;
};

static void program_(UiTimer *tm)
{
	LARGE_INTEGER due;
	uint64_t now, at;
	LONGLONG rel;

	if (!tm->ht || tm->dead) return;
	if (UiHeap_Empty(&tm->heap)) {
		CancelWaitableTimer(tm->ht);
		return;
	}
	now = GetTickCount64();
	at = UiHeap_TopDeadline(&tm->heap);
	rel = (at > now) ? (LONGLONG)(at - now) : 1;
	due.QuadPart = -rel * 10000LL;          /* 相对时间: 负数 100ns */
	SetWaitableTimer(tm->ht, &due, 0, NULL, NULL, FALSE);
}

UiTimer *UiTimer_Create(void)
{
	UiTimer *tm = (UiTimer *)malloc(sizeof(UiTimer));
	if (!tm) return NULL;
	UiHeap_Init(&tm->heap);
	tm->ht = CreateWaitableTimerW(NULL, FALSE, NULL);
	tm->nextId = 1;
	tm->seq = 0;
	tm->cur = NULL;
	tm->dead = 0;
	if (!tm->ht) {
		free(tm);
		return NULL;
	}
	return tm;
}

void UiTimer_Destroy(UiTimer *tm)
{
	int i;
	if (!tm) return;
	tm->dead = 1;
	for (i = 0; i < tm->heap.n; i++)
		free((UiTimerNode *)tm->heap.a[i]);
	UiHeap_Free(&tm->heap);
	if (tm->ht) CloseHandle(tm->ht);
	free(tm);
}

static uint64_t add_(UiTimer *tm, uint64_t ms, int repeat, UiTimerFn fn, void *user)
{
	UiTimerNode *n;

	if (!tm || tm->dead || !fn) return 0;
	if (ms == 0) ms = 1;
	n = (UiTimerNode *)malloc(sizeof(UiTimerNode));
	if (!n) return 0;
	n->hn.idx = -1;
	n->hn.deadline = GetTickCount64() + ms;
	n->hn.seq = ++tm->seq;
	n->hn.ptr = n;
	n->owner = tm;
	n->id = tm->nextId++;
	n->interval = ms;
	n->repeat = repeat;
	n->canceled = 0;
	n->fn = fn;
	n->user = user;
	if (!UiHeap_Push(&tm->heap, &n->hn)) {
		free(n);
		return 0;
	}
	program_(tm);
	return n->id;
}

uint64_t UiTimer_After(UiTimer *tm, int ms, UiTimerFn fn, void *user)
{
	if (ms <= 0) return 0;
	return add_(tm, (uint64_t)ms, 0, fn, user);
}

uint64_t UiTimer_Every(UiTimer *tm, int ms, UiTimerFn fn, void *user)
{
	if (ms <= 0) return 0;
	return add_(tm, (uint64_t)ms, 1, fn, user);
}

int UiTimer_Cancel(UiTimer *tm, uint64_t id)
{
	int i;

	if (!tm || tm->dead || id == 0) return 0;
	if (tm->cur && tm->cur->id == id) {
		tm->cur->canceled = 1;             /* 回调返回后释放, 不再重排 */
		return 1;
	}
	for (i = 0; i < tm->heap.n; i++) {
		UiTimerNode *n = (UiTimerNode *)tm->heap.a[i];
		if (n->id == id) {
			UiHeap_Erase(&tm->heap, &n->hn);
			free(n);
			program_(tm);
			return 1;
		}
	}
	return 0;
}

HANDLE UiTimer_WaitHandle(UiTimer *tm)
{
	if (!tm || tm->dead || UiHeap_Empty(&tm->heap)) return NULL;
	return tm->ht;
}

int UiTimer_Dispatch(UiTimer *tm)
{
	uint64_t now;

	if (!tm || tm->dead) return -1;
	now = GetTickCount64();
	for (;;) {
		UiHeapNode *hn = UiHeap_Top(&tm->heap);
		UiTimerNode *n;

		if (!hn || hn->deadline > now) break;
		n = (UiTimerNode *)hn;
		UiHeap_Pop(&tm->heap);
		tm->cur = n;
		n->canceled = 0;
		n->fn(n->user, n->id);
		tm->cur = NULL;

		if (n->repeat && !n->canceled) {
			n->hn.deadline = GetTickCount64() + n->interval;
			n->hn.seq = ++tm->seq;
			n->hn.idx = -1;
			if (!UiHeap_Push(&tm->heap, &n->hn))
				free(n);
		} else {
			free(n);
		}
		now = GetTickCount64();
	}
	program_(tm);

	if (UiHeap_Empty(&tm->heap)) return -1;
	now = GetTickCount64();
	{
		uint64_t at = UiHeap_TopDeadline(&tm->heap);
		return at > now ? (int)(at - now) : 0;
	}
}
