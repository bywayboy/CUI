/*
 * net_test.c - 网络层纯逻辑 headless 断言 (0CRT 控制台)
 *
 * 覆盖: 最小堆 / 动态缓冲 / 字符串与编码 / Base64+SHA1 (RFC 6455 向量)。
 * 构建: build.cmd 产出 bin\cui_net_test.exe; 退出码 0=全通过。
 */
#include "net/net_base.h"
#include "net/ui_net.h"
#include "log.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);
void *memset(void *d, int c, size_t n);

static int g_pass, g_fail;

static void out_(const char *s)
{
	DWORD n = 0;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &n, NULL);
}

#define CHECK(c) do { \
	if (c) g_pass++; \
	else { \
		char b_[320]; \
		g_fail++; \
		sprintf(b_, "FAIL line %d: %s\n", __LINE__, #c); \
		out_(b_); \
	} \
} while (0)

/* ── 最小堆 ─────────────────────────────────────────────────────────── */

static void test_heap(void)
{
	UiHeap h;
	UiHeapNode nodes[200];
	uint64_t last = 0;
	unsigned seed = 12345;
	int i, count;

	UiHeap_Init(&h);
	for (i = 0; i < 200; i++) {
		nodes[i].idx = -1;
		seed = seed * 1103515245u + 12345u;
		nodes[i].deadline = 1000 + (seed >> 8) % 5000;
		nodes[i].seq = (uint64_t)i;
		nodes[i].ptr = NULL;
		CHECK(UiHeap_Push(&h, &nodes[i]) == 1);
	}
	CHECK(h.n == 200);
	count = 0;
	while (!UiHeap_Empty(&h)) {
		UiHeapNode *e = UiHeap_Pop(&h);
		CHECK(e != NULL);
		if (!e) break;
		CHECK(e->deadline >= last);
		CHECK(e->idx == -1);
		last = e->deadline;
		count++;
	}
	CHECK(count == 200);

	/* erase 中间元素 + adjust 变最近 */
	UiHeap_Init(&h);
	for (i = 0; i < 30; i++) {
		nodes[i].idx = -1;
		nodes[i].deadline = (uint64_t)(1000 + i * 10);
		nodes[i].seq = (uint64_t)i;
		CHECK(UiHeap_Push(&h, &nodes[i]) == 1);
	}
	UiHeap_Erase(&h, &nodes[10]);
	CHECK(nodes[10].idx == -1);
	CHECK(h.n == 29);
	nodes[25].deadline = 1;                 /* 变成最早 */
	UiHeap_Adjust(&h, &nodes[25]);
	CHECK(UiHeap_Top(&h) == &nodes[25]);
	CHECK(UiHeap_Pop(&h) == &nodes[25]);

	/* 重复 push 同一节点不应重复计数 */
	CHECK(UiHeap_Push(&h, &nodes[0]) == 1);
	{
		int before = h.n;
		CHECK(UiHeap_Push(&h, &nodes[0]) == 1);
		CHECK(h.n == before);
	}
	UiHeap_Free(&h);
	CHECK(h.a == NULL && h.n == 0);
}

/* ── 缓冲 ───────────────────────────────────────────────────────────── */

static void test_buf(void)
{
	NetBuf b;
	char data[600];
	int i;

	for (i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i & 0xFF);

	NetBuf_Init(&b);
	CHECK(b.p == NULL && b.len == 0);
	CHECK(NetBuf_Append(&b, data, 500) == 1);
	CHECK(b.len == 500);
	CHECK(b.p[500] == '\0');
	CHECK(NetBuf_Reserve(&b, 1000) == 1);
	CHECK(b.cap >= 1501);
	NetBuf_Consume(&b, 200);
	CHECK(b.len == 300);
	CHECK(b.p[0] == data[200]);
	CHECK(b.p[299] == data[499]);
	CHECK(NetBuf_AppendStr(&b, "tail") == 1);
	CHECK(b.len == 304);
	CHECK(NetBuf_AppendFmt(&b, " %d/%s", 42, "x") == 1);
	CHECK(NetBuf_AppendChar(&b, '!') == 1);
	CHECK(Net_MemFind(b.p, b.len, "tail 42/x!", 10) == b.p + 300);
	NetBuf_Consume(&b, 100000);
	CHECK(b.len == 0);
	NetBuf_Free(&b);
	CHECK(b.p == NULL);
}

/* ── 字符串与编码 ───────────────────────────────────────────────────── */

static void test_str(void)
{
	BYTE hash[20];
	BYTE raw[16];
	char b64[64];
	char hex[64];

	CHECK(Net_StrCaseEq("Sec-WebSocket-Accept", "sec-websocket-accept"));
	CHECK(!Net_StrCaseEq("abc", "abcd"));
	CHECK(Net_StrCaseCmpN("Upgrade", "upgrade", 7) == 0);
	CHECK(Net_StrCaseCmpN("UpgradeX", "upgrade", 7) == 0);
	CHECK(Net_StrCaseStr("Content-Length: 12\r\n", "content-length:") != NULL);
	CHECK(Net_StrCaseStr("abc", "abd") == NULL);

	CHECK(Net_Sha1("abc", 3, hash) == 1);
	Net_HexLower(hash, 20, hex);
	CHECK(Net_StrCaseEq(hex, "a9993e364706816aba3e25717850c26c9cd0d89d"));

	CHECK(Net_Base64((const BYTE *)"abc", 3, b64, sizeof(b64)) == 4);
	CHECK(Net_StrCaseEq(b64, "YWJj"));
	CHECK(Net_Base64((const BYTE *)"abcd", 4, b64, sizeof(b64)) == 8);
	CHECK(Net_StrCaseEq(b64, "YWJjZA=="));

	/* RFC 6455 1.3 握手向量: key + GUID 的 SHA1/Base64 */
	{
		const char *s = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		CHECK(Net_Sha1(s, (int)strlen(s), hash) == 1);
		CHECK(Net_Base64(hash, 20, b64, sizeof(b64)) > 0);
		CHECK(Net_StrCaseEq(b64, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
	}

	CHECK(Net_Random(raw, sizeof(raw)) == 1);
	CHECK(Net_NowMs() != 0);
	CHECK(Net_HexVal('f') == 15 && Net_HexVal('A') == 10 && Net_HexVal('z') == -1);
}

static void test_net_lifecycle(void)
{
	UiNetConfig cfg;
	UiNet *net;

	memset(&cfg, 0, sizeof(cfg));
	net = UiNet_Create(&cfg);
	CHECK(net != NULL);
	if (net) {
		Sleep(30);
		UiNet_Destroy(net);
	}
	CHECK(UiNet_StrError(UI_NET_E_DNS) != NULL);
	CHECK(UiNet_StrError(UI_NET_E_TLS) != NULL);
}

/* ── 实网测试 (CUI_NET_LIVE=1) ─────────────────────────────────────── */

typedef struct Live {
	volatile int done;
	volatile int open;
	volatile int msgs;
	volatile int closed;
	int status, err, bodyLen, closeCode;
	int connReq;
	char body[256];
	char last[256];
} Live;

char *strstr(const char *hay, const char *needle);

static void live_http_cb(void *u, const UiHttpResponse *r)
{
	Live *L = (Live *)u;
	L->status = r ? r->status : 0;
	L->err = r ? r->error : -1;
	L->bodyLen = r ? r->bodyLen : 0;
	L->body[0] = '\0';
	L->connReq = 0;
	if (r && r->body && r->bodyLen > 0) {
		int n = r->bodyLen < 255 ? r->bodyLen : 255;
		memcpy(L->body, r->body, (size_t)n);
		L->body[n] = '\0';
	}
	if (r) {
		char tmp[16];
		if (UiHttp_Header(r, "x-conn-req", tmp, sizeof(tmp)) > 0)
			L->connReq = atoi(tmp);
	}
	L->done = 1;
}

static void live_ws_open_cb(void *u) { ((Live *)u)->open = 1; }
static void live_ws_msg_cb(void *u, const void *d, int n, int op)
{
	Live *L = (Live *)u;
	int k = n < 255 ? n : 255;
	(void)op;
	memcpy(L->last, d, (size_t)k);
	L->last[k] = '\0';
	L->msgs++;
}
static void live_ws_close_cb(void *u, int code, const char *reason)
{
	Live *L = (Live *)u;
	(void)reason;
	L->closeCode = code;
	L->closed = 1;
}

static int live_wait(volatile int *flag, int ms)
{
	uint64_t t0 = GetTickCount64();
	while (!*flag && (int)(GetTickCount64() - t0) < ms)
		MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_ALERTABLE);
	return *flag;
}

static void live_pump(int ms)
{
	uint64_t t0 = GetTickCount64();
	while ((int)(GetTickCount64() - t0) < ms)
		MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_ALERTABLE);
}

static void live_keepalive(UiNet *net)
{
	Live L1, L2;
	UiHttpRequest r;
	uint64_t id;

	memset(&L1, 0, sizeof(L1));
	memset(&L2, 0, sizeof(L2));
	memset(&r, 0, sizeof(r));
	r.url = "http://127.0.0.1:18080/ka";
	r.timeoutMs = 5000;
	id = UiHttp_Send(net, &r, live_http_cb, &L1);
	CHECK(id != 0);
	if (id) live_wait(&L1.done, 8000);
	CHECK(L1.connReq == 1);
	id = UiHttp_Send(net, &r, live_http_cb, &L2);
	CHECK(id != 0);
	if (id) live_wait(&L2.done, 8000);
	{
		char line[160];
		sprintf(line, "live keepalive req1=%d req2=%d\n", L1.connReq, L2.connReq);
		out_(line);
	}
	CHECK(L2.connReq == 2);   /* 同一连接复用 */
}

static void live_timeout_cancel(UiNet *net)
{
	Live L;
	UiHttpRequest r;
	uint64_t id;

	memset(&L, 0, sizeof(L));
	memset(&r, 0, sizeof(r));
	r.url = "http://127.0.0.1:18080/slow";
	r.timeoutMs = 400;
	id = UiHttp_Send(net, &r, live_http_cb, &L);
	CHECK(id != 0);
	live_wait(&L.done, 6000);
	out_("live timeout\n");
	CHECK(L.done && L.err == UI_NET_E_TIMEOUT);

	memset(&L, 0, sizeof(L));
	r.timeoutMs = 5000;
	id = UiHttp_Send(net, &r, live_http_cb, &L);
	CHECK(id != 0);
	CHECK(UiHttp_Cancel(net, id) == 1);
	live_pump(800);
	out_("live cancel\n");
	CHECK(L.done == 0);        /* 取消后不再回调 */
}

static void live_http(UiNet *net, const char *url, const char *expect, int want)
{
	Live L;
	UiHttpRequest r;
	uint64_t id;
	char line[200];

	memset(&L, 0, sizeof(L));
	memset(&r, 0, sizeof(r));
	r.url = url;
	r.timeoutMs = 10000;
	id = UiHttp_Send(net, &r, live_http_cb, &L);
	CHECK(id != 0);
	if (id) live_wait(&L.done, 15000);
	sprintf(line, "live http %s -> status=%d err=%d body=%d\n", url, L.status,
		L.err, L.bodyLen);
	out_(line);
	CHECK(L.done);
	CHECK(L.err == 0);
	CHECK(L.status == want);
	if (expect) CHECK(strstr(L.body, expect) != NULL);
}

static void live_http_fail(UiNet *net, const char *url, int wantErr)
{
	Live L;
	UiHttpRequest r;

	memset(&L, 0, sizeof(L));
	memset(&r, 0, sizeof(r));
	r.url = url;
	r.timeoutMs = 8000;
	CHECK(UiHttp_Send(net, &r, live_http_cb, &L) != 0);
	live_wait(&L.done, 10000);
	{
		char line[200];
		sprintf(line, "live fail %s -> err=%d (want %d)\n", url, L.err, wantErr);
		out_(line);
	}
	CHECK(L.done && L.err == wantErr);
}

static void live_post(UiNet *net, const char *url)
{
	Live L;
	UiHttpRequest r;

	memset(&L, 0, sizeof(L));
	memset(&r, 0, sizeof(r));
	r.method = "POST";
	r.url = url;
	r.contentType = "application/json";
	r.body = "{\"msg\":\"hi\"}";
	r.bodyLen = 12;
	r.timeoutMs = 10000;
	CHECK(UiHttp_Send(net, &r, live_http_cb, &L) != 0);
	live_wait(&L.done, 15000);
	out_("live http POST\n");
	CHECK(L.done && L.err == 0 && L.status == 200);
	CHECK(strstr(L.body, "msg") != NULL || strstr(L.body, "got") != NULL);
}

static void live_ws(UiNet *net, const char *url, const char *expect)
{
	Live L;
	UiWsOptions o;
	UiWsCallbacks cb;
	void *ws;

	memset(&L, 0, sizeof(L));
	memset(&o, 0, sizeof(o));
	memset(&cb, 0, sizeof(cb));
	o.autoReconnect = 0;
	o.pingIntervalMs = 0;
	o.handshakeTimeoutMs = 8000;
	cb.open = live_ws_open_cb;
	cb.message = live_ws_msg_cb;
	cb.close = live_ws_close_cb;

	ws = UiWs_Open(net, url, &o, &cb, &L);
	out_("live ws open\n");
	CHECK(ws != NULL);
	if (!ws) return;
	CHECK(live_wait(&L.open, 10000));
	if (L.open) {
		CHECK(UiWs_Send(ws, "ping", 4, 1) == 1);
		CHECK(live_wait(&L.msgs, 5000));
		{
			char line[220];
			sprintf(line, "live ws msg=%d last=%s\n", L.msgs, L.last);
			out_(line);
		}
		CHECK(strstr(L.last, expect) != NULL);
	}
	UiWs_Close(ws, 1000, "bye");
	CHECK(live_wait(&L.closed, 5000));
}

static void test_net_live(void)
{
	UiNetConfig cfg;
	UiNet *net;

	memset(&cfg, 0, sizeof(cfg));
	cfg.connectTimeoutMs = 8000;
	cfg.requestTimeoutMs = 15000;
	cfg.dnsTimeoutMs = 3000;
	net = UiNet_Create(&cfg);
	CHECK(net != NULL);
	if (!net) return;
	out_("--- live tests ---\n");

	live_http(net, "http://127.0.0.1:18080/hello", "hello", 200);
	live_http(net, "http://127.0.0.1:18080/chunked", "chunk-c", 200);
	live_http(net, "http://127.0.0.1:18080/redirect", "hello", 200);
	live_post(net, "http://127.0.0.1:18080/echo");
	live_http(net, "http://localhost:18080/hello", "hello", 200);   /* hosts */
	live_keepalive(net);
	live_timeout_cancel(net);
	live_http(net, "https://www.example.com/", NULL, 200);        /* DNS+TLS */
	live_http_fail(net, "https://localhost:18082/", UI_NET_E_TLS); /* 自签证书被拒 */
	live_ws(net, "ws://127.0.0.1:18081/ws", "echo:ping");

	UiNet_Destroy(net);

	/* WSS: 本机自签证书, 仅验证 client 的 insecure 开关 */
	{
		UiNetConfig c2;
		UiNet *n2;
		memset(&c2, 0, sizeof(c2));
		c2.insecure = 1;
		c2.connectTimeoutMs = 5000;
		n2 = UiNet_Create(&c2);
		CHECK(n2 != NULL);
		if (n2) {
			live_ws(n2, "wss://localhost:18082/ws", "wss-echo:ping");
			UiNet_Destroy(n2);
		}
	}

	out_("--- live tests done ---\n");
}

void __stdcall startup(void)
{
	char line[160];

	Log_Init();
	test_heap();
	test_buf();
	test_str();
	test_net_lifecycle();
	if (GetEnvironmentVariableA("CUI_NET_LIVE", NULL, 0) > 0)
		test_net_live();

	sprintf(line, "net_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
