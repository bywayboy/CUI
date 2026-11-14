/*
 * net_test.c - 网络层纯逻辑 headless 断言 (0CRT 控制台)
 *
 * 覆盖: 最小堆 / 动态缓冲 / 字符串与编码 / Base64+SHA1 (RFC 6455 向量)。
 * 构建: build.cmd 产出 bin\cui_net_test.exe; 退出码 0=全通过。
 */
#include "net/net_base.h"
#include "net/ui_net.h"
#include "net/ui_ws_server.h"
#include "log.h"

int sprintf(char *buf, const char *fmt, ...);
size_t strlen(const char *s);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int strcmp(const char *a, const char *b);

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

/* ── 本机 WebSocket 服务端测试 ─────────────────────────────────────── */

static void tcopy_(char *d, int cap, const char *s)
{
	int n = s ? (int)strlen(s) : 0;
	if (n > cap - 1) n = cap - 1;
	if (n > 0) memcpy(d, s, (size_t)n);
	d[n] = '\0';
}

typedef struct SrvT {
	volatile int listening;
	volatile int port;
	volatile int errors;
	volatile int opens;
	volatile int msgs;
	volatile int closes;
	int lastOp;
	int lastLen;
	char last[1024];
	void *conn;
	int closeCode;
	char path[64];
	char peer[48];
	char proto[48];
	char err[96];
} SrvT;

typedef struct CliT {
	volatile int open;
	volatile int closed;
	volatile int msgs;
	int lastOp;
	int lastLen;
	int closeCode;
	unsigned char bin[1024];
} CliT;

static void srv_on_listening_(void *u, int port)
{
	SrvT *s = (SrvT *)u;
	s->port = port;
	s->listening = 1;
}

static void srv_on_open_(void *u, UiWsConn *conn, const UiWsServerRequest *req)
{
	SrvT *s = (SrvT *)u;
	s->opens++;
	s->conn = conn;
	if (req) {
		tcopy_(s->path, (int)sizeof(s->path), req->path);
		tcopy_(s->peer, (int)sizeof(s->peer), req->peer);
		tcopy_(s->proto, (int)sizeof(s->proto), req->protocol);
	}
}

static void srv_on_msg_(void *u, UiWsConn *conn, const void *d, int n, int op)
{
	SrvT *s = (SrvT *)u;
	int k = n < (int)sizeof(s->last) ? n : (int)sizeof(s->last);
	s->msgs++;
	s->lastOp = op;
	s->lastLen = n;
	if (k > 0) memcpy(s->last, d, (size_t)k);
	UiWsServer_Send(conn, d, n, op);
}

static void srv_on_close_(void *u, UiWsConn *conn, int code, const char *reason)
{
	SrvT *s = (SrvT *)u;
	(void)conn;
	(void)reason;
	s->closes++;
	s->closeCode = code;
	s->conn = NULL;
}

static void srv_on_error_(void *u, int err, const char *msg)
{
	SrvT *s = (SrvT *)u;
	s->errors++;
	s->port = err;
	tcopy_(s->err, (int)sizeof(s->err), msg);
}

static void cli_on_open_(void *u) { ((CliT *)u)->open = 1; }

static void cli_on_msg_(void *u, const void *d, int n, int op)
{
	CliT *c = (CliT *)u;
	int k = n < (int)sizeof(c->bin) ? n : (int)sizeof(c->bin);
	c->msgs++;
	c->lastOp = op;
	c->lastLen = n;
	if (k > 0) memcpy(c->bin, d, (size_t)k);
}

static void cli_on_close_(void *u, int code, const char *reason)
{
	CliT *c = (CliT *)u;
	(void)reason;
	c->closeCode = code;
	c->closed = 1;
}

static void srv_cfg_(UiWsServerConfig *c, const char *path)
{
	memset(c, 0, sizeof(*c));
	c->bindHost = "127.0.0.1";
	c->port = 0;
	c->path = path;
	c->subprotocols = "chat,superchat";
	c->maxConnections = 16;
	c->maxMessageSize = 1024 * 1024;
	c->handshakeTimeoutMs = 5000;
}

static void srv_cb_(UiWsServerCallbacks *cb, SrvT *s)
{
	memset(cb, 0, sizeof(*cb));
	cb->listening = srv_on_listening_;
	cb->open = srv_on_open_;
	cb->message = srv_on_msg_;
	cb->close = srv_on_close_;
	cb->error = srv_on_error_;
	(void)s;
}

static void test_ws_server_api(void)
{
	UiNetConfig ncfg;
	UiNet *net;
	UiWsServerConfig sc;
	UiWsServerCallbacks cb;
	UiWsOptions o;
	UiWsCallbacks wcb;
	SrvT S;
	CliT C1, C2;
	UiWsServer *srv;
	void *w1, *w2;
	char url[128];
	unsigned char bin[8];
	int i;

	memset(&ncfg, 0, sizeof(ncfg));
	net = UiNet_Create(&ncfg);
	CHECK(net != NULL);
	if (!net) return;

	memset(&S, 0, sizeof(S));
	memset(&C1, 0, sizeof(C1));
	memset(&C2, 0, sizeof(C2));
	srv_cfg_(&sc, "/ws");
	srv_cb_(&cb, &S);
	srv = UiWsServer_Create(net, &sc, &cb, &S);
	CHECK(srv != NULL);
	if (!srv) { UiNet_Destroy(net); return; }
	CHECK(S.listening == 1);
	CHECK(UiWsServer_Port(srv) > 0);
	CHECK(UiWsServer_State(srv) == UI_WS_SRV_LISTENING);

	memset(&o, 0, sizeof(o));
	o.autoReconnect = 0;
	o.pingIntervalMs = 0;
	o.handshakeTimeoutMs = 5000;
	o.extraHeaders = "Sec-WebSocket-Protocol: chat\r\n";
	memset(&wcb, 0, sizeof(wcb));
	wcb.open = cli_on_open_;
	wcb.message = cli_on_msg_;
	wcb.close = cli_on_close_;

	sprintf(url, "ws://127.0.0.1:%d/ws", UiWsServer_Port(srv));
	w1 = UiWs_Open(net, url, &o, &wcb, &C1);
	CHECK(w1 != NULL);
	CHECK(live_wait(&C1.open, 5000));
	CHECK(live_wait(&S.opens, 5000));
	CHECK(strcmp(S.path, "/ws") == 0);
	CHECK(strcmp(S.proto, "chat") == 0);
	CHECK(S.peer[0] != '\0');
	CHECK(UiWsServer_ConnCount(srv) == 1);
	CHECK(strcmp(UiWsConn_Path(S.conn), "/ws") == 0);
	CHECK(strcmp(UiWsConn_Protocol(S.conn), "chat") == 0);
	CHECK(strcmp(UiWsConn_Peer(S.conn), S.peer) == 0);

	CHECK(UiWs_Send(w1, "hello", 5, 1) == 1);
	CHECK(live_wait(&C1.msgs, 5000));
	CHECK(C1.lastOp == 1 && C1.lastLen == 5 && memcmp(C1.bin, "hello", 5) == 0);
	CHECK(S.msgs >= 1 && S.lastOp == 1 && S.lastLen == 5);

	for (i = 0; i < 8; i++) bin[i] = (unsigned char)(i == 4 ? 0 : i * 31);
	bin[4] = 0;
	CHECK(UiWs_Send(w1, bin, 8, 2) == 1);
	{
		uint64_t t0 = GetTickCount64();
		while (C1.msgs < 2 && GetTickCount64() - t0 < 5000)
			MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_ALERTABLE);
	}
	CHECK(C1.msgs >= 2);
	CHECK(C1.lastOp == 2 && C1.lastLen == 8 && memcmp(C1.bin, bin, 8) == 0);

	CHECK(UiWsServer_Broadcast(srv, "bc", 2, 1) == 1);
	{
		uint64_t t0 = GetTickCount64();
		while (C1.msgs < 3 && GetTickCount64() - t0 < 5000)
			MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_ALERTABLE);
	}
	CHECK(C1.msgs >= 3 && C1.lastLen == 2 && memcmp(C1.bin, "bc", 2) == 0);

	/* 第二个连接: 服务端主动关闭 */
	w2 = UiWs_Open(net, url, &o, &wcb, &C2);
	CHECK(w2 != NULL);
	CHECK(live_wait(&C2.open, 5000));
	{
		uint64_t t0 = GetTickCount64();
		while (S.opens < 2 && GetTickCount64() - t0 < 5000)
			MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_ALERTABLE);
	}
	CHECK(S.opens >= 2);
	CHECK(UiWsServer_ConnCount(srv) == 2);
	UiWsServer_Close(S.conn, 1001, "srv bye");
	CHECK(live_wait(&C2.closed, 5000));
	CHECK(C2.closeCode == 1001);
	{
		uint64_t t0 = GetTickCount64();
		while (UiWsServer_ConnCount(srv) > 1 && GetTickCount64() - t0 < 5000)
			MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_ALERTABLE);
	}
	CHECK(UiWsServer_ConnCount(srv) == 1);

	UiWs_Close(w1, 1000, "bye");
	CHECK(live_wait(&C1.closed, 5000));
	CHECK(C1.closeCode == 1000);
	{
		uint64_t t0 = GetTickCount64();
		while (UiWsServer_ConnCount(srv) > 0 && GetTickCount64() - t0 < 5000)
			MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_ALERTABLE);
	}
	CHECK(UiWsServer_ConnCount(srv) == 0);

	UiWsServer_Destroy(srv);
	UiNet_Destroy(net);
}

/* ── 原始 socket 辅助 ──────────────────────────────────────────────── */

static SOCKET raw_connect_(int port)
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in a;
	DWORD tv = 4000;

	if (s == INVALID_SOCKET) return INVALID_SOCKET;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((u_short)port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
		closesocket(s);
		return INVALID_SOCKET;
	}
	return s;
}

static int raw_rx_(SOCKET s, void *buf, int n)
{
	char *p = (char *)buf;
	int got = 0;
	while (got < n) {
		int r = recv(s, p + got, n - got, 0);
		if (r <= 0) return 0;
		got += r;
	}
	return 1;
}

static SOCKET raw_upgrade_(int port, const char *path)
{
	SOCKET s = raw_connect_(port);
	char req[512];
	char buf[512];
	int n = 0, r, len;

	if (s == INVALID_SOCKET) return s;
	len = sprintf(req,
		"GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
		"Upgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",
		path, port);
	if (send(s, req, len, 0) != len) { closesocket(s); return INVALID_SOCKET; }
	while (n < (int)sizeof(buf) - 1) {
		r = recv(s, buf + n, 1, 0);
		if (r <= 0) break;
		n += r;
		if (n >= 4 && memcmp(buf + n - 4, "\r\n\r\n", 4) == 0) break;
	}
	buf[n] = '\0';
	if (!Net_MemFind(buf, n, "HTTP/1.1 101", 12)) {
		closesocket(s);
		return INVALID_SOCKET;
	}
	return s;
}

static int raw_http_probe_(int port, const char *path, const char *ver,
	const char *expect)
{
	SOCKET s = raw_connect_(port);
	char req[512];
	char buf[1024];
	int n = 0, r, len, found = 0;

	if (s == INVALID_SOCKET) return 0;
	len = sprintf(req,
		"GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
		"Upgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Version: %s\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",
		path, ver);
	if (send(s, req, len, 0) != len) { closesocket(s); return 0; }
	for (;;) {
		if (n >= (int)sizeof(buf) - 1) break;
		r = recv(s, buf + n, 1, 0);
		if (r <= 0) break;
		n += r;
		if (n >= 4 && memcmp(buf + n - 4, "\r\n\r\n", 4) == 0) break;
	}
	buf[n] = '\0';
	if (Net_MemFind(buf, n, expect, (int)strlen(expect))) found = 1;
	closesocket(s);
	return found;
}

static int raw_frame_send_(SOCKET s, int opcode, int fin, const void *d, int len)
{
	BYTE h[14];
	BYTE mask[4] = { 0x11, 0x22, 0x33, 0x44 };
	char body[512];
	int hl = 2, i;

	if (len < 0 || len > (int)sizeof(body)) return 0;
	h[0] = (BYTE)((fin ? 0x80 : 0) | (opcode & 0x0F));
	if (len < 126) {
		h[1] = (BYTE)(0x80 | len);
	} else {
		h[1] = 0x80 | 126;
		h[2] = (BYTE)((len >> 8) & 0xFF);
		h[3] = (BYTE)(len & 0xFF);
		hl = 4;
	}
	memcpy(h + hl, mask, 4);
	hl += 4;
	if (send(s, (const char *)h, hl, 0) != hl) return 0;
	{
		const BYTE *p = (const BYTE *)d;
		for (i = 0; i < len; i++) body[i] = (char)(p[i] ^ mask[i & 3]);
	}
	return len == 0 || send(s, body, len, 0) == len;
}

static int raw_frame_recv_(SOCKET s, char *payload, int cap, int *op, int *fin)
{
	BYTE h[2], m[4];
	uint64_t len;
	int i;

	if (!raw_rx_(s, h, 2)) return 0;
	*fin = (h[0] & 0x80) ? 1 : 0;
	*op = h[0] & 0x0F;
	len = h[1] & 0x7F;
	if (len == 126) {
		BYTE e[2];
		if (!raw_rx_(s, e, 2)) return 0;
		len = ((uint64_t)e[0] << 8) | e[1];
	} else if (len == 127) {
		BYTE e[8];
		if (!raw_rx_(s, e, 8)) return 0;
		len = 0;
		for (i = 0; i < 8; i++) len = (len << 8) | e[i];
	}
	if (h[1] & 0x80) {
		if (!raw_rx_(s, m, 4)) return 0;
	} else {
		m[0] = m[1] = m[2] = m[3] = 0;
	}
	if (len > (uint64_t)cap) return 0;
	if (len > 0 && !raw_rx_(s, payload, (int)len)) return 0;
	if (h[1] & 0x80) {
		for (i = 0; i < (int)len; i++) payload[i] ^= (char)m[i & 3];
	}
	return 1;
}

static void test_ws_server_http_reject(void)
{
	UiNetConfig ncfg;
	UiNet *net;
	UiWsServerConfig sc;
	UiWsServerCallbacks cb;
	SrvT S;
	UiWsServer *srv;

	memset(&ncfg, 0, sizeof(ncfg));
	net = UiNet_Create(&ncfg);
	CHECK(net != NULL);
	if (!net) return;
	memset(&S, 0, sizeof(S));
	srv_cfg_(&sc, "/only");
	srv_cb_(&cb, &S);
	srv = UiWsServer_Create(net, &sc, &cb, &S);
	CHECK(srv != NULL);
	if (!srv) { UiNet_Destroy(net); return; }
	CHECK(raw_http_probe_(UiWsServer_Port(srv), "/other", "13", "404") == 1);
	CHECK(raw_http_probe_(UiWsServer_Port(srv), "/only", "8", "426") == 1);
	CHECK(raw_http_probe_(UiWsServer_Port(srv), "/only", "13", "101") == 1);
	UiWsServer_Destroy(srv);
	UiNet_Destroy(net);
}

static void test_ws_server_raw(void)
{
	UiNetConfig ncfg;
	UiNet *net;
	UiWsServerConfig sc;
	UiWsServerCallbacks cb;
	SrvT S;
	UiWsServer *srv;
	SOCKET s;
	char pay[256];
	int op = 0, fin = 0;
	BYTE closePay[2];

	memset(&ncfg, 0, sizeof(ncfg));
	net = UiNet_Create(&ncfg);
	CHECK(net != NULL);
	if (!net) return;
	memset(&S, 0, sizeof(S));
	srv_cfg_(&sc, "/raw");
	srv_cb_(&cb, &S);
	srv = UiWsServer_Create(net, &sc, &cb, &S);
	CHECK(srv != NULL);
	if (!srv) { UiNet_Destroy(net); return; }

	/* raw 握手时未带子协议, 所以 protocol 应为空 */
	s = raw_upgrade_(UiWsServer_Port(srv), "/raw");
	CHECK(s != INVALID_SOCKET);
	if (s != INVALID_SOCKET) {
		CHECK(live_wait(&S.opens, 5000));
		CHECK(S.proto[0] == '\0');
		CHECK(raw_frame_send_(s, 1, 0, "he", 2) == 1);
		CHECK(raw_frame_send_(s, 0, 1, "llo", 3) == 1);
		CHECK(raw_frame_recv_(s, pay, (int)sizeof(pay), &op, &fin) == 1);
		CHECK(op == 1 && fin == 1);
		CHECK(memcmp(pay, "hello", 5) == 0);
		CHECK(raw_frame_send_(s, 9, 1, "px", 2) == 1);
		CHECK(raw_frame_recv_(s, pay, (int)sizeof(pay), &op, &fin) == 1);
		CHECK(op == 10 && memcmp(pay, "px", 2) == 0);
		closePay[0] = 0x03; closePay[1] = 0xE8;   /* 1000 */
		CHECK(raw_frame_send_(s, 8, 1, closePay, 2) == 1);
		CHECK(raw_frame_recv_(s, pay, (int)sizeof(pay), &op, &fin) == 1);
		CHECK(op == 8);
		closesocket(s);
		CHECK(live_wait(&S.closes, 5000));
		CHECK(S.closeCode == 1000);
	}
	UiWsServer_Destroy(srv);
	UiNet_Destroy(net);
}

static void test_ws_server_wss(void)
{
	UiNetConfig ncfg;
	UiNet *net;
	UiWsServerConfig sc;
	UiWsServerCallbacks cb;
	UiWsOptions o;
	UiWsCallbacks wcb;
	SrvT S;
	CliT C;
	UiWsServer *srv;
	void *w;
	char url[128];

	memset(&ncfg, 0, sizeof(ncfg));
	ncfg.insecure = 1;
	ncfg.connectTimeoutMs = 5000;
	net = UiNet_Create(&ncfg);
	CHECK(net != NULL);
	if (!net) return;
	memset(&S, 0, sizeof(S));
	memset(&C, 0, sizeof(C));
	srv_cfg_(&sc, "/ws");
	if (GetFileAttributesA("build\\test_wss.pfx") != INVALID_FILE_ATTRIBUTES) {
		sc.certPath = "build\\test_wss.pfx";
		sc.certPassword = "cui-test";
	} else {
		sc.certSubject = "CN=localhost-cui-test";
	}
	srv_cb_(&cb, &S);
	srv = UiWsServer_Create(net, &sc, &cb, &S);
	if (!srv) {
		out_("wss skipped (test cert not found)\n");
		UiNet_Destroy(net);
		return;
	}
	CHECK(S.listening == 1);

	memset(&o, 0, sizeof(o));
	o.autoReconnect = 0;
	o.pingIntervalMs = 0;
	o.handshakeTimeoutMs = 8000;
	memset(&wcb, 0, sizeof(wcb));
	wcb.open = cli_on_open_;
	wcb.message = cli_on_msg_;
	wcb.close = cli_on_close_;
	sprintf(url, "wss://127.0.0.1:%d/ws", UiWsServer_Port(srv));
	w = UiWs_Open(net, url, &o, &wcb, &C);
	CHECK(w != NULL);
	CHECK(live_wait(&C.open, 8000));
	CHECK(live_wait(&S.opens, 8000));
	if (C.open) {
		CHECK(UiWs_Send(w, "tls", 3, 1) == 1);
		CHECK(live_wait(&C.msgs, 5000));
		CHECK(C.lastLen == 3 && memcmp(C.bin, "tls", 3) == 0);
		CHECK(strcmp(UiWsConn_Protocol(S.conn), "") == 0);
	}
	UiWs_Close(w, 1000, "bye");
	CHECK(live_wait(&C.closed, 5000));
	CHECK(live_wait(&S.closes, 5000));
	UiWsServer_Destroy(srv);
	UiNet_Destroy(net);
}

void __stdcall startup(void)
{
	char line[160];

	Log_Init();
	test_heap();
	test_buf();
	test_str();
	test_net_lifecycle();
	test_ws_server_api();
	test_ws_server_http_reject();
	test_ws_server_raw();
	test_ws_server_wss();
	if (GetEnvironmentVariableA("CUI_NET_LIVE", NULL, 0) > 0)
		test_net_live();

	sprintf(line, "net_test: %d passed, %d failed\n", g_pass, g_fail);
	out_(line);
	Log_Close();
	ExitProcess(g_fail ? 1u : 0u);
}
