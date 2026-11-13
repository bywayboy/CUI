/*
 * ui_net.c - UiNet 核心: GUI 侧表 + IO 线程 + IOCP + APC 归位
 *
 * 线程模型见 docs/CUI-网络层-设计方案.md 第 2 节。
 */
#include "net/net_internal.h"
#include "log.h"

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
int sprintf(char *buf, const char *fmt, ...);

/* ── 错误码 ─────────────────────────────────────────────────────────── */

const char *UiNet_StrError(int err)
{
	switch (err) {
	case UI_NET_OK: return "ok";
	case UI_NET_E_PARAM: return "bad parameter";
	case UI_NET_E_OOM: return "out of memory";
	case UI_NET_E_DNS: return "dns failed";
	case UI_NET_E_CONNECT: return "connect failed";
	case UI_NET_E_TLS: return "tls failed";
	case UI_NET_E_TIMEOUT: return "timeout";
	case UI_NET_E_CANCEL: return "cancelled";
	case UI_NET_E_CLOSED: return "closed";
	case UI_NET_E_HTTP: return "http protocol error";
	case UI_NET_E_REDIRECT: return "too many redirects";
	case UI_NET_E_WS: return "websocket error";
	case UI_NET_E_INTERNAL: return "internal error";
	default: return "unknown";
	}
}

/* ── 小工具 ─────────────────────────────────────────────────────────── */

static char *dupstr_(const char *s)
{
	char *p;
	int n;
	if (!s) return NULL;
	n = (int)strlen(s) + 1;
	p = (char *)malloc((size_t)n);
	if (p) memcpy(p, s, (size_t)n);
	return p;
}

static void trace_(UiNet *net, const char *msg, const char *detail)
{
	if (net && net->cfg.trace)
		Log_Write("[net] %s %s", msg, detail ? detail : "");
}

/* ── 事件 ───────────────────────────────────────────────────────────── */

NetEv *net_ev_new(UiNet *net, int type, uint64_t id)
{
	NetEv *ev = (NetEv *)calloc(1, sizeof(NetEv));
	if (!ev) return NULL;
	ev->net = net;
	ev->type = type;
	ev->id = id;
	return ev;
}

static void http_result_free_(NetHttpResult *r)
{
	if (!r) return;
	if (r->hdrs) free(r->hdrs);
	if (r->hdrStore) free(r->hdrStore);
	if (r->body) free(r->body);
	free(r);
}

void net_ev_free(NetEv *ev)
{
	if (!ev) return;
	if (ev->type == NET_EV_HTTP_DONE) http_result_free_((NetHttpResult *)ev->p);
	else if (ev->type == NET_EV_WS_MSG) free(ev->p);
	free(ev);
}

static VOID CALLBACK apc_dispatch_(ULONG_PTR param);

void net_post(NetIo *io, NetEv *ev)
{
	UiNet *net;
	if (!io || !ev) return;
	net = io->owner;
	if (!net) return;
	ev->net = net;
	if (!net->alive || !QueueUserAPC(apc_dispatch_, net->guiThread,
			(ULONG_PTR)ev)) {
		net_ev_free(ev);
		return;
	}
	InterlockedIncrement64(&net->evCount);
	io->evCount++;
}

void net_emit_http(NetIo *io, uint64_t id, NetHttpResult *r)
{
	NetEv *ev = net_ev_new(io->owner, NET_EV_HTTP_DONE, id);
	if (!ev) { http_result_free_(r); return; }
	ev->p = r;
	net_post(io, ev);
}

void net_emit_ws_open(NetIo *io, uint64_t id)
{
	NetEv *ev = net_ev_new(io->owner, NET_EV_WS_OPEN, id);
	net_post(io, ev);
}

void net_emit_ws_msg(NetIo *io, uint64_t id, int opcode, const void *data, int len)
{
	NetWsMsg *m;
	NetEv *ev = net_ev_new(io->owner, NET_EV_WS_MSG, id);
	if (!ev) return;
	m = (NetWsMsg *)malloc(sizeof(NetWsMsg) + (size_t)(len > 0 ? len : 0));
	if (!m) { free(ev); return; }
	m->opcode = opcode;
	m->len = len;
	if (len > 0) memcpy(m->data, data, (size_t)len);
	ev->p = m;
	net_post(io, ev);
}

void net_emit_ws_close(NetIo *io, uint64_t id, int code, const char *reason)
{
	NetEv *ev = net_ev_new(io->owner, NET_EV_WS_CLOSE, id);
	if (!ev) return;
	ev->code = code;
	Net_StrCopy(ev->reason, (int)sizeof(ev->reason), reason ? reason : "");
	net_post(io, ev);
}

/* ── 命令队列 ───────────────────────────────────────────────────────── */

void net_cmd_post(UiNet *net, NetCmd *cmd)
{
	if (!net || !cmd) return;
	InterlockedPushEntrySList(net->cmds, &cmd->entry);
	PostQueuedCompletionStatus(net->io->iocp, 0, NET_WAKE_KEY, NULL);
}

static void cmd_free_(NetCmd *c)
{
	if (!c) return;
	if (c->type == NET_CMD_HTTP) {
		NetHttpReq *r = (NetHttpReq *)c->p;
		if (r) {
			if (r->method) free(r->method);
			if (r->url) free(r->url);
			if (r->contentType) free(r->contentType);
			if (r->extraHeaders) free(r->extraHeaders);
			if (r->body) free(r->body);
			free(r);
		}
	} else if (c->type == NET_CMD_WS_OPEN) {
		NetWsOpenReq *r = (NetWsOpenReq *)c->p;
		if (r) {
			if (r->url) free(r->url);
			if (r->extraHeaders) free(r->extraHeaders);
			free(r);
		}
	} else if (c->type == NET_CMD_WS_SEND) {
		if (c->p) free(c->p);
	}
	free(c);
}

/* ── IO 定时器 ──────────────────────────────────────────────────────── */

void net_timer_cancel(NetConn *conn, int kind)
{
	NetTimer *t;
	if (!conn || kind < 1 || kind >= (int)(sizeof(conn->timers) / sizeof(conn->timers[0])))
		return;
	t = conn->timers[kind];
	if (!t) return;
	conn->timers[kind] = NULL;
	UiHeap_Erase(&conn->io->timers, &t->hn);
	free(t);
}

void net_timer_cancel_all(NetConn *conn)
{
	int i;
	if (!conn) return;
	for (i = 1; i < (int)(sizeof(conn->timers) / sizeof(conn->timers[0])); i++)
		net_timer_cancel(conn, i);
}

void net_timer_arm(NetConn *conn, int kind, int ms)
{
	NetIo *io;
	NetTimer *t;

	if (!conn || !conn->io || conn->dead) return;
	if (ms < 0) ms = 0;
	net_timer_cancel(conn, kind);
	io = conn->io;
	t = (NetTimer *)calloc(1, sizeof(NetTimer));
	if (!t) return;
	t->hn.idx = -1;
	t->hn.deadline = Net_NowMs() + (uint64_t)ms;
	t->hn.seq = ++io->seq;
	t->hn.ptr = t;
	t->conn = conn;
	t->kind = kind;
	if (!UiHeap_Push(&io->timers, &t->hn)) {
		free(t);
		return;
	}
	conn->timers[kind] = t;
}

/* ── 连接生命周期 ───────────────────────────────────────────────────── */

NetConn *net_conn_new(NetIo *io, int kind, uint64_t id)
{
	NetConn *c = (NetConn *)calloc(1, sizeof(NetConn));
	if (!c) return NULL;
	c->io = io;
	c->s = INVALID_SOCKET;
	c->id = id;
	c->kind = kind;
	c->state = NET_S_IDLE;
	c->refs = 1;                 /* 注册表引用 */
	c->born = Net_NowMs();
	NetBuf_Init(&c->in);
	NetBuf_Init(&c->plain);
	NetBuf_Init(&c->out);
	NetBuf_Init(&c->tx);
	NetBuf_Init(&c->dnsQuery);
	c->next = io->conns;
	io->conns = c;
	io->connCount++;
	return c;
}

void net_conn_addref(NetConn *conn)
{
	if (conn) conn->refs++;
}

void net_conn_detach_idle(NetConn *conn)
{
	NetIo *io;
	NetConn **pp;

	if (!conn || !conn->io || !conn->inIdle) return;
	io = conn->io;
	for (pp = &io->idle; *pp; pp = &(*pp)->idleNext) {
		if (*pp == conn) {
			*pp = conn->idleNext;
			conn->idleNext = NULL;
			conn->inIdle = 0;
			if (io->idleCount > 0) io->idleCount--;
			return;
		}
	}
	conn->inIdle = 0;
}

static void conn_free_(NetConn *conn)
{
	NetIo *io;
	NetConn **pp;

	if (!conn) return;
	io = conn->io;
	net_timer_cancel_all(conn);
	net_conn_detach_idle(conn);
	net_tls_free(conn);
	if (conn->http) net_http_free(conn);
	if (conn->ws) net_ws_free(conn);
	NetBuf_Free(&conn->in);
	NetBuf_Free(&conn->plain);
	NetBuf_Free(&conn->out);
	NetBuf_Free(&conn->tx);
	NetBuf_Free(&conn->dnsQuery);
	if (conn->s != INVALID_SOCKET) closesocket(conn->s);
	if (io) {
		for (pp = &io->conns; *pp; pp = &(*pp)->next) {
			if (*pp == conn) {
				*pp = conn->next;
				if (io->connCount > 0) io->connCount--;
				break;
			}
		}
	}
	free(conn);
}

void net_conn_release(NetConn *conn)
{
	if (!conn) return;
	if (conn->refs > 0) conn->refs--;
	if (conn->refs == 0) conn_free_(conn);
}

void net_tcp_shutdown(NetConn *conn)
{
	if (!conn || conn->s == INVALID_SOCKET) return;
	shutdown(conn->s, SD_BOTH);
	CancelIoEx((HANDLE)conn->s, NULL);
	closesocket(conn->s);
	conn->s = INVALID_SOCKET;
}

void net_conn_close(NetConn *conn)
{
	if (!conn || conn->dead) return;
	conn->dead = 1;
	conn->state = NET_S_DEAD;
	net_timer_cancel_all(conn);
	net_tcp_shutdown(conn);
	NetBuf_Clear(&conn->in);
	NetBuf_Clear(&conn->plain);
	NetBuf_Clear(&conn->out);
	NetBuf_Clear(&conn->tx);
	conn->sending = 0;
	net_conn_release(conn);      /* 注册表引用 */
}

void net_conn_fail(NetConn *conn, int err)
{
	if (!conn || conn->dead) return;
	if (conn->kind == NET_K_WS && conn->ws) {
		if (net_ws_on_fail(conn, err))
			return;              /* 已安排重连, 连接保留 */
	} else if (conn->kind == NET_K_HTTP && conn->http) {
		net_http_finish(conn, err);
	}
	if (conn->dead || conn->inIdle || conn->state == NET_S_IDLE)
		return;                      /* finish 已回收/池化 */
	net_conn_close(conn);
}

NetConn *net_conn_by_id(NetIo *io, uint64_t id, int kind)
{
	NetConn *c;
	for (c = io->conns; c; c = c->next)
		if (!c->dead && c->id == id && c->kind == kind)
			return c;
	return NULL;
}

/* ── 连接启动: DNS -> CONNECT -> TLS -> 传输 ────────────────────────── */

static int is_ip_literal(const char *host, SOCKADDR_STORAGE *out, int *len)
{
	IN_ADDR v4;
	IN6_ADDR v6;
	if (InetPtonA(AF_INET, host, &v4) == 1) {
		struct sockaddr_in *a = (struct sockaddr_in *)out;
		memset(a, 0, sizeof(*a));
		a->sin_family = AF_INET;
		a->sin_port = htons((u_short)0);
		a->sin_addr = v4;
		*len = sizeof(*a);
		return 1;
	}
	if (InetPtonA(AF_INET6, host, &v6) == 1) {
		struct sockaddr_in6 *a = (struct sockaddr_in6 *)out;
		memset(a, 0, sizeof(*a));
		a->sin6_family = AF_INET6;
		a->sin6_addr = v6;
		*len = sizeof(*a);
		return 1;
	}
	return 0;
}

void net_conn_start(NetConn *conn)
{
	SOCKADDR_STORAGE sa;
	int len = 0;

	if (!conn || conn->dead) return;
	conn->state = NET_S_IDLE;
	conn->addrCount = 0;
	conn->addrIdx = 0;

	if (is_ip_literal(conn->host, &sa, &len)) {
		if (len > 0) {
			conn->addrs[0] = sa;
			conn->addrCount = 1;
		}
		goto do_connect;
	}
	if (net_dns_cache_lookup(conn->io, conn->host, conn->addrs, NET_MAX_ADDRS) > 0) {
		conn->addrCount = net_dns_cache_lookup(conn->io, conn->host, conn->addrs,
			NET_MAX_ADDRS);
		goto do_connect;
	}

	conn->state = NET_S_DNS;
	net_timer_arm(conn, NET_T_CONNECT,
		conn->io->owner->cfg.dnsTimeoutMs * 2 + 1000);
	net_dns_resolve(conn);
	return;

do_connect:
	if (conn->addrCount <= 0) {
		net_conn_fail(conn, UI_NET_E_DNS);
		return;
	}
	{
		int i;
		for (i = 0; i < conn->addrCount; i++) {
			if (((struct sockaddr *)&conn->addrs[i])->sa_family == AF_INET)
				((struct sockaddr_in *)&conn->addrs[i])->sin_port = htons((u_short)conn->port);
			else if (((struct sockaddr *)&conn->addrs[i])->sa_family == AF_INET6)
				((struct sockaddr_in6 *)&conn->addrs[i])->sin6_port = htons((u_short)conn->port);
		}
	}
	conn->addrIdx = 0;
	conn->state = NET_S_CONNECT;
	net_timer_arm(conn, NET_T_CONNECT,
		conn->io->owner->cfg.connectTimeoutMs);
	net_tcp_try(conn);
}

void net_on_connected(NetConn *conn)
{
	if (!conn || conn->dead) return;
	net_timer_cancel(conn, NET_T_CONNECT);
	if (conn->https) {
		conn->state = NET_S_TLS;
		net_timer_arm(conn, NET_T_HANDSHAKE,
			conn->io->owner->cfg.connectTimeoutMs + 3000);
		if (!net_tls_start(conn)) {
			net_conn_fail(conn, UI_NET_E_TLS);
			return;
		}
		net_tcp_recv(conn);          /* 等 ServerHello */
		return;
	}
	conn->state = NET_S_TRANSPORT;
	net_tcp_recv(conn);
	if (conn->kind == NET_K_HTTP && net_http_need_tunnel(conn)) {
		net_http_begin_tunnel(conn);
		return;
	}
	if (conn->kind == NET_K_HTTP) net_http_on_event(conn, 1, 0);
	else if (conn->kind == NET_K_WS) net_ws_on_event(conn, 1, 0);
}

void net_on_tls_ready(NetConn *conn)
{
	if (!conn || conn->dead) return;
	net_timer_cancel(conn, NET_T_HANDSHAKE);
	conn->state = NET_S_TRANSPORT;
	net_tcp_recv(conn);
	if (conn->kind == NET_K_HTTP) net_http_on_event(conn, 1, 0);
	else if (conn->kind == NET_K_WS) net_ws_on_event(conn, 1, 0);
}

void net_on_recv_data(NetConn *conn)
{
	if (!conn || conn->dead) return;
	if (conn->tls && !conn->tls->established) {
		net_tls_handshake_input(conn);
		return;
	}
	if (conn->tls) {
		net_tls_on_cipher(conn);
		return;
	}
	/* 明文: in 就是传输字节 */
	if (conn->kind == NET_K_HTTP) net_http_on_plain(conn, conn->in.p, conn->in.len);
	else if (conn->kind == NET_K_WS) net_ws_on_plain(conn, conn->in.p, conn->in.len);
	else if (conn->kind == NET_K_DNS) net_dns_on_recv(conn, conn->in.p, conn->in.len, 0);
}

/* ── IO 线程 ────────────────────────────────────────────────────────── */

static void io_timer_fire(NetIo *io, NetTimer *t)
{
	NetConn *conn = t->conn;

	if (!conn || conn->dead) return;
	switch (t->kind) {
	case NET_T_CONNECT:
		/* DNS 阶段也复用该定时器: 还没连上就判超时 */
		net_conn_fail(conn, UI_NET_E_TIMEOUT);
		break;
	case NET_T_REQUEST:
		net_http_finish(conn, UI_NET_E_TIMEOUT);
		net_conn_close(conn);
		break;
	case NET_T_HANDSHAKE:
		net_conn_fail(conn, UI_NET_E_TIMEOUT);
		break;
	case NET_T_IDLE:
		net_conn_close(conn);
		break;
	case NET_T_PING:
		net_ws_on_ping_timeout(conn);
		break;
	case NET_T_DNS_RETRY:
		net_dns_retry(conn);
		break;
	case NET_T_CLOSE:
		net_ws_on_close_timeout(conn);
		break;
	case NET_T_DNS_TCP:
		net_conn_fail(conn, UI_NET_E_DNS);
		break;
	case NET_T_RECONNECT:
		net_ws_reconnect_now(conn);
		break;
	default:
		break;
	}
	(void)io;
}

static void io_run_timers(NetIo *io)
{
	for (;;) {
		UiHeapNode *hn = UiHeap_Top(&io->timers);
		NetTimer *t;
		uint64_t now;
		if (!hn) break;
		now = Net_NowMs();
		if (hn->deadline > now) break;
		t = (NetTimer *)hn;
		UiHeap_Pop(&io->timers);
		if (t->conn && t->kind >= 1 &&
		    t->kind < (int)(sizeof(t->conn->timers) / sizeof(t->conn->timers[0])) &&
		    t->conn->timers[t->kind] == t)
			t->conn->timers[t->kind] = NULL;
		io_timer_fire(io, t);
		free(t);
	}
}

static void io_handle_cmd(NetIo *io, NetCmd *c)
{
	NetConn *conn;

	switch (c->type) {
	case NET_CMD_STOP:
		io->stop = 1;
		break;

	case NET_CMD_HTTP: {
		NetHttpReq *r = (NetHttpReq *)c->p;
		c->p = NULL;
		conn = net_http_pool_get(io, r);   /* 可能返回 NULL */
		if (!conn) {
			conn = net_conn_new(io, NET_K_HTTP, c->id);
			if (!conn) {
				NetHttpResult *res = (NetHttpResult *)calloc(1, sizeof(NetHttpResult));
				if (res) { res->error = UI_NET_E_OOM; net_emit_http(io, c->id, res); }
				cmd_free_(c);
				return;
			}
		}
		conn->id = c->id;                  /* 复用连接也要换成新请求 id */
		net_http_start(conn, r);
		break;
	}
	case NET_CMD_HTTP_CANCEL:
		conn = net_conn_by_id(io, c->id, NET_K_HTTP);
		if (conn) {
			if (conn->http) conn->http->id = 0;   /* 不再投结果 */
			net_conn_close(conn);
		}
		break;

	case NET_CMD_WS_OPEN: {
		NetWsOpenReq *r = (NetWsOpenReq *)c->p;
		c->p = NULL;
		conn = net_conn_new(io, NET_K_WS, c->id);
		if (!conn) {
			net_emit_ws_close(io, c->id, 0, "out of memory");
			cmd_free_(c);
			return;
		}
		net_ws_start(conn, r);
		break;
	}
	case NET_CMD_WS_SEND:
		conn = net_conn_by_id(io, c->id, NET_K_WS);
		if (conn && conn->ws) net_ws_on_send_cmd(conn, (NetWsData *)c->p);
		c->p = NULL;
		break;

	case NET_CMD_WS_CLOSE: {
		NetWsCloseCmd *r = (NetWsCloseCmd *)c->p;
		conn = net_conn_by_id(io, c->id, NET_K_WS);
		c->p = NULL;
		if (conn && conn->ws) net_ws_on_close_cmd(conn, r ? r->code : 1000,
			r ? r->reason : "");
		if (r) free(r);
		break;
	}
	default:
		break;
	}
}

static void io_drain_cmds(NetIo *io)
{
	SLIST_ENTRY *head = InterlockedFlushSList(io->owner->cmds);
	SLIST_ENTRY *rev = NULL;

	while (head) {
		SLIST_ENTRY *n = head->Next;
		head->Next = rev;
		rev = head;
		head = n;
	}
	while (rev) {
		NetCmd *c = (NetCmd *)rev;
		rev = rev->Next;
		if (io->stop) {
			cmd_free_(c);
			continue;
		}
		io_handle_cmd(io, c);
		cmd_free_(c);
		if (io->stop) {
			while (rev) { NetCmd *d = (NetCmd *)rev; rev = rev->Next; cmd_free_(d); }
			break;
		}
	}
}

static void io_cleanup(NetIo *io)
{
	NetConn *c = io->conns;

	io->stopping = 1;
	while (c) {
		NetConn *next = c->next;
		if (c->s != INVALID_SOCKET) {
			shutdown(c->s, SD_BOTH);
			CancelIoEx((HANDLE)c->s, NULL);
			closesocket(c->s);
			c->s = INVALID_SOCKET;
		}
		net_timer_cancel_all(c);
		c->dead = 1;
		c = next;
	}

	/* 抽取已完成的中止包, 释放 NetOp */
	{
		OVERLAPPED_ENTRY ents[16];
		ULONG n = 0;
		int guard;
		for (guard = 0; guard < 64; guard++) {
			if (!GetQueuedCompletionStatusEx(io->iocp, ents, 16, &n, 20, FALSE))
				break;
			if (n == 0) break;
			{
				ULONG i;
				for (i = 0; i < n; i++) {
					OVERLAPPED *ov = ents[i].lpOverlapped;
					if (ov) {
						NetOp *op = (NetOp *)ov;
						if (op->buf) free(op->buf);
						free(op);
					}
				}
			}
		}
	}

	/* 剩下的连接直接释放; refs 未归零说明有丢失的完成包, 进程/实例销毁阶段接受 */
	c = io->conns;
	while (c) {
		NetConn *next = c->next;
		net_timer_cancel_all(c);
		NetBuf_Free(&c->in);
		NetBuf_Free(&c->plain);
		NetBuf_Free(&c->out);
		NetBuf_Free(&c->tx);
		NetBuf_Free(&c->dnsQuery);
		if (c->s != INVALID_SOCKET) closesocket(c->s);
		net_tls_free(c);
		if (c->http) net_http_free(c);
		if (c->ws) net_ws_free(c);
		free(c);
		c = next;
	}
	io->conns = NULL;
	io->idle = NULL;
	io->idleCount = 0;
	UiHeap_Free(&io->timers);
	net_dns_cache_free(io);
	if (io->sslCredOk) FreeCredentialsHandle(&io->sslCred);
	net_tls_shutdown(io);
}

static DWORD WINAPI io_thread_(LPVOID param)
{
	NetIo *io = (NetIo *)param;
	OVERLAPPED_ENTRY ents[16];

	trace_(io->owner, "io thread start", NULL);

	for (;;) {
		DWORD timeout = INFINITE;

		io_drain_cmds(io);
		if (io->stop) break;

		io_run_timers(io);

		if (!UiHeap_Empty(&io->timers)) {
			uint64_t now = Net_NowMs();
			uint64_t at = UiHeap_TopDeadline(&io->timers);
			uint64_t d = at > now ? at - now : 0;
			if (d > 60000) d = 60000;
			timeout = (DWORD)d;
		}

		{
			ULONG n = 0;
			BOOL ok = GetQueuedCompletionStatusEx(io->iocp, ents, 16, &n,
				timeout, FALSE);
			if (!ok) {
				DWORD e = GetLastError();
				if (e == WAIT_TIMEOUT) continue;
				continue;
			}
			{
				ULONG i;
				for (i = 0; i < n; i++) {
					OVERLAPPED *ov = ents[i].lpOverlapped;
					DWORD st = 0;
					if (!ov) continue;            /* 唤醒包 */
					st = (DWORD)ov->Internal;     /* NTSTATUS, 0=成功 */
					net_io_on_completion(io, ov, ents[i].lpCompletionKey,
						ents[i].dwNumberOfBytesTransferred, st);
				}
			}
		}
	}

	io_cleanup(io);
	trace_(io->owner, "io thread stop", NULL);
	return 0;
}

/* ── GUI 侧派发 ─────────────────────────────────────────────────────── */

static NetPend *pend_find_(UiNet *net, uint64_t id, int kind)
{
	NetPend *p;
	for (p = net->pend; p; p = p->next)
		if (p->id == id && p->kind == kind) return p;
	return NULL;
}

static NetPend *pend_take_(UiNet *net, uint64_t id, int kind)
{
	NetPend **pp;
	for (pp = &net->pend; *pp; pp = &(*pp)->next) {
		NetPend *p = *pp;
		if (p->id == id && p->kind == kind) {
			*pp = p->next;
			p->next = NULL;
			return p;
		}
	}
	return NULL;
}

static UiWsH *ws_find_(UiNet *net, uint64_t id)
{
	UiWsH *w;
	for (w = net->wsList; w; w = w->next)
		if (w->id == id) return w;
	return NULL;
}

static void ws_unlink_(UiNet *net, UiWsH *ws)
{
	UiWsH **pp;
	for (pp = &net->wsList; *pp; pp = &(*pp)->next) {
		if (*pp == ws) { *pp = ws->next; return; }
	}
}

static void gui_dispatch_(UiNet *net, NetEv *ev)
{
	switch (ev->type) {
	case NET_EV_HTTP_DONE: {
		NetPend *p = pend_take_(net, ev->id, NET_K_HTTP);
		NetHttpResult *r = (NetHttpResult *)ev->p;
		if (p) {
			UiHttpResponse resp;
			memset(&resp, 0, sizeof(resp));
			if (r) {
				resp.status = r->status;
				resp.error = r->error;
				resp.headers = r->hdrs;
				resp.headerCount = r->hdrCount;
				resp.body = r->body;
				resp.bodyLen = r->bodyLen;
			} else {
				resp.error = UI_NET_E_INTERNAL;
			}
			ev->p = NULL;
			if (p->cb) p->cb(p->user, &resp);
			free(p);
		}
		if (r) http_result_free_(r);
		break;
	}
	case NET_EV_WS_OPEN: {
		UiWsH *ws = ws_find_(net, ev->id);
		if (ws && !ws->closed && ws->cb.open)
			ws->cb.open(ws->user);
		break;
	}
	case NET_EV_WS_MSG: {
		UiWsH *ws = ws_find_(net, ev->id);
		NetWsMsg *m = (NetWsMsg *)ev->p;
		ev->p = NULL;
		if (ws && !ws->closed && ws->cb.message && m)
			ws->cb.message(ws->user, m->data, m->len, m->opcode);
		if (m) free(m);
		break;
	}
	case NET_EV_WS_CLOSE: {
		UiWsH *ws = ws_find_(net, ev->id);
		NetPend *p = pend_take_(net, ev->id, NET_K_WS);
		if (ws) {
			ws_unlink_(net, ws);
			ws->closed = 1;
			ws->state = ev->code;
			if (ws->cb.close) ws->cb.close(ws->user, ev->code, ev->reason);
			free(ws);
		}
		if (p) free(p);
		break;
	}
	case NET_EV_SENTINEL:
		if (ev->done) SetEvent(ev->done);
		break;
	default:
		break;
	}
}

static VOID CALLBACK apc_dispatch_(ULONG_PTR param)
{
	NetEv *ev = (NetEv *)param;
	if (!ev) return;
	if (ev->net) gui_dispatch_(ev->net, ev);
	net_ev_free(ev);
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

static void cfg_defaults_(UiNetConfig *c)
{
	memset(c, 0, sizeof(*c));
	c->connectTimeoutMs = 5000;
	c->requestTimeoutMs = 15000;
	c->dnsTimeoutMs = 2000;
	c->maxRedirects = 5;
	c->idleTimeoutMs = 60000;
	c->maxIdlePerHost = 4;
	c->maxIdleTotal = 8;
}

UiNet *UiNet_Create(const UiNetConfig *cfg)
{
	UiNet *net;
	NetIo *io;
	WSADATA wd;

	WSAStartup(MAKEWORD(2, 2), &wd);

	net = (UiNet *)calloc(1, sizeof(UiNet));
	if (!net) { WSACleanup(); return NULL; }
	cfg_defaults_(&net->cfg);
	if (cfg) {
		if (cfg->connectTimeoutMs > 0) net->cfg.connectTimeoutMs = cfg->connectTimeoutMs;
		if (cfg->requestTimeoutMs > 0) net->cfg.requestTimeoutMs = cfg->requestTimeoutMs;
		if (cfg->dnsTimeoutMs > 0) net->cfg.dnsTimeoutMs = cfg->dnsTimeoutMs;
		if (cfg->maxRedirects > 0) net->cfg.maxRedirects = cfg->maxRedirects;
		if (cfg->idleTimeoutMs > 0) net->cfg.idleTimeoutMs = cfg->idleTimeoutMs;
		if (cfg->maxIdlePerHost > 0) net->cfg.maxIdlePerHost = cfg->maxIdlePerHost;
		if (cfg->maxIdleTotal > 0) net->cfg.maxIdleTotal = cfg->maxIdleTotal;
		net->cfg.insecure = cfg->insecure;
		net->cfg.trace = cfg->trace;
		net->proxy = dupstr_(cfg->proxy);
		net->pin = dupstr_(cfg->pinSha256);
	}

	net->cmds = (SLIST_HEADER *)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY, sizeof(SLIST_HEADER));
	if (!net->cmds) goto fail;
	InitializeSListHead(net->cmds);
	net->nextId = 1;                 /* 0 保留为"无效 id" */

	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
			GetCurrentProcess(), &net->guiThread, 0, FALSE,
			DUPLICATE_SAME_ACCESS))
		goto fail;

	io = (NetIo *)calloc(1, sizeof(NetIo));
	if (!io) goto fail;
	io->owner = net;
	io->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
	if (!io->iocp) { free(io); goto fail; }
	UiHeap_Init(&io->timers);
	net->io = io;

	net_tls_init(io);

	net->ioThread = CreateThread(NULL, 0, io_thread_, io, 0, NULL);
	if (!net->ioThread) goto fail;

	net->alive = 1;
	trace_(net, "create", net->proxy ? net->proxy : "(direct)");
	return net;

fail:
	if (net->io) {
		if (net->io->iocp) CloseHandle(net->io->iocp);
		free(net->io);
	}
	if (net->guiThread) CloseHandle(net->guiThread);
	if (net->cmds) HeapFree(GetProcessHeap(), 0, net->cmds);
	if (net->proxy) free(net->proxy);
	if (net->pin) free(net->pin);
	free(net);
	WSACleanup();
	return NULL;
}

void UiNet_Destroy(UiNet *net)
{
	NetPend *p;
	UiWsH *w;

	if (!net) return;
	net->alive = 0;

	{
		NetCmd *c = (NetCmd *)calloc(1, sizeof(NetCmd));
		if (c) {
			c->type = NET_CMD_STOP;
			InterlockedPushEntrySList(net->cmds, &c->entry);
			PostQueuedCompletionStatus(net->io->iocp, 0, NET_WAKE_KEY, NULL);
		}
	}
	if (net->ioThread) {
		if (WaitForSingleObject(net->ioThread, 5000) != WAIT_OBJECT_0)
			Log_Write("[net] io thread did not exit");
		CloseHandle(net->ioThread);
		net->ioThread = NULL;
	}

	/* 哨兵 APC: 用 alertable 等待把此前所有事件 APC 执行完 */
	if (net->guiThread) {
		HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
		NetEv *e = net_ev_new(net, NET_EV_SENTINEL, 0);
		if (e) e->done = ev;
		if (e && QueueUserAPC(apc_dispatch_, net->guiThread, (ULONG_PTR)e))
			WaitForSingleObjectEx(ev, INFINITE, TRUE);
		else if (e)
			free(e);
		if (ev) CloseHandle(ev);
		CloseHandle(net->guiThread);
		net->guiThread = NULL;
	}

	p = net->pend;
	while (p) { NetPend *n = p->next; free(p); p = n; }
	net->pend = NULL;
	w = net->wsList;
	while (w) { UiWsH *n = w->next; free(w); w = n; }
	net->wsList = NULL;

	if (net->io) {
		if (net->io->iocp) CloseHandle(net->io->iocp);
		free(net->io);
		net->io = NULL;
	}
	if (net->cmds) HeapFree(GetProcessHeap(), 0, net->cmds);
	if (net->proxy) free(net->proxy);
	if (net->pin) free(net->pin);
	free(net);
	WSACleanup();
}

/* ── HTTP 公开 API ─────────────────────────────────────────────────── */

uint64_t UiHttp_Send(UiNet *net, const UiHttpRequest *req,
	UiHttpCallback cb, void *user)
{
	NetHttpReq *r;
	NetCmd *c;
	NetPend *p;
	uint64_t id;

	if (!net || !net->alive || !req || !req->url || !cb) return 0;
	if (!req->url[0]) return 0;

	r = (NetHttpReq *)calloc(1, sizeof(NetHttpReq));
	c = (NetCmd *)calloc(1, sizeof(NetCmd));
	p = (NetPend *)calloc(1, sizeof(NetPend));
	if (!r || !c || !p) {
		if (r) free(r);
		if (c) free(c);
		if (p) free(p);
		return 0;
	}
	r->url = dupstr_(req->url);
	r->method = dupstr_(req->method);
	r->contentType = dupstr_(req->contentType);
	r->extraHeaders = dupstr_(req->extraHeaders);
	if (req->body && req->bodyLen > 0) {
		r->body = (char *)malloc((size_t)req->bodyLen);
		if (!r->body) goto fail;
		memcpy(r->body, req->body, (size_t)req->bodyLen);
		r->bodyLen = req->bodyLen;
	}
	r->timeoutMs = req->timeoutMs;

	id = net->nextId++;
	if (net->nextId == 0) net->nextId = 1;
	p->id = id;
	p->kind = NET_K_HTTP;
	p->cb = cb;
	p->user = user;
	p->next = net->pend;
	net->pend = p;

	c->type = NET_CMD_HTTP;
	c->id = id;
	c->p = r;
	net_cmd_post(net, c);
	trace_(net, "http send", r->url);
	return id;

fail:
	if (r->url) free(r->url);
	if (r->method) free(r->method);
	if (r->contentType) free(r->contentType);
	if (r->extraHeaders) free(r->extraHeaders);
	free(r);
	free(c);
	free(p);
	return 0;
}

int UiHttp_Cancel(UiNet *net, uint64_t id)
{
	NetPend **pp;
	NetCmd *c;

	if (!net || !net->alive || !id) return 0;
	for (pp = &net->pend; *pp; pp = &(*pp)->next) {
		NetPend *p = *pp;
		if (p->id == id && p->kind == NET_K_HTTP) {
			*pp = p->next;
			free(p);
			c = (NetCmd *)calloc(1, sizeof(NetCmd));
			if (c) {
				c->type = NET_CMD_HTTP_CANCEL;
				c->id = id;
				net_cmd_post(net, c);
			}
			return 1;
		}
	}
	return 0;
}

int UiHttp_Header(const UiHttpResponse *r, const char *name, char *out, int cap)
{
	int i;
	int nl;
	if (!r || !name) return -1;
	nl = (int)strlen(name);
	for (i = 0; i < r->headerCount; i++) {
		const char *h = r->headers[i].name;
		if (h && Net_StrCaseCmpN(h, name, nl) == 0 && h[nl] == '\0') {
			const char *v = r->headers[i].value ? r->headers[i].value : "";
			int n = (int)strlen(v);
			if (out && cap > 0) Net_StrCopy(out, cap, v);
			return n;
		}
	}
	return -1;
}

/* ── WS 公开 API ───────────────────────────────────────────────────── */

void *UiWs_Open(UiNet *net, const char *url, const UiWsOptions *opt,
	const UiWsCallbacks *cb, void *user)
{
	NetWsOpenReq *r;
	NetCmd *c;
	NetPend *p;
	UiWsH *ws;
	uint64_t id;

	if (!net || !net->alive || !url || !cb) return NULL;

	ws = (UiWsH *)calloc(1, sizeof(UiWsH));
	r = (NetWsOpenReq *)calloc(1, sizeof(NetWsOpenReq));
	c = (NetCmd *)calloc(1, sizeof(NetCmd));
	p = (NetPend *)calloc(1, sizeof(NetPend));
	if (!ws || !r || !c || !p) {
		if (ws) free(ws);
		if (r) free(r);
		if (c) free(c);
		if (p) free(p);
		return NULL;
	}

	id = net->nextId++;
	if (net->nextId == 0) net->nextId = 1;
	ws->id = id;
	ws->net = net;
	ws->cb = *cb;
	ws->user = user;
	ws->closed = 0;
	ws->state = -1;
	ws->next = net->wsList;
	net->wsList = ws;

	p->id = id;
	p->kind = NET_K_WS;
	p->user = user;
	p->ws = ws;
	p->next = net->pend;
	net->pend = p;

	r->url = dupstr_(url);
	r->extraHeaders = dupstr_(opt ? opt->extraHeaders : NULL);
	if (opt) r->opt = *opt;
	else {
		r->opt.autoReconnect = 1;
		r->opt.reconnectMinMs = 500;
		r->opt.reconnectMaxMs = 10000;
		r->opt.pingIntervalMs = 30000;
		r->opt.handshakeTimeoutMs = 8000;
	}
	c->type = NET_CMD_WS_OPEN;
	c->id = id;
	c->p = r;
	net_cmd_post(net, c);
	return ws;
}

int UiWs_Send(void *handle, const void *data, int len, int opcode)
{
	UiWsH *ws = (UiWsH *)handle;
	NetCmd *c;
	NetWsData *d;

	if (!ws || ws->closed || !ws->net || !ws->net->alive) return 0;
	if (len < 0) return 0;
	if (len > 0 && !data) return 0;
	c = (NetCmd *)calloc(1, sizeof(NetCmd));
	d = (NetWsData *)malloc(sizeof(NetWsData) + (size_t)(len > 0 ? len : 0));
	if (!c || !d) { if (c) free(c); if (d) free(d); return 0; }
	d->opcode = opcode;
	d->len = len;
	if (len > 0) memcpy(d->data, data, (size_t)len);
	c->type = NET_CMD_WS_SEND;
	c->id = ws->id;
	c->p = d;
	net_cmd_post(ws->net, c);
	return 1;
}

void UiWs_Close(void *handle, int code, const char *reason)
{
	UiWsH *ws = (UiWsH *)handle;
	NetCmd *c;
	NetWsCloseCmd *r;

	if (!ws || ws->closed || !ws->net || !ws->net->alive) return;
	c = (NetCmd *)calloc(1, sizeof(NetCmd));
	r = (NetWsCloseCmd *)calloc(1, sizeof(NetWsCloseCmd));
	if (!c || !r) { if (c) free(c); if (r) free(r); return; }
	if (code <= 0) code = 1000;
	r->code = code;
	Net_StrCopy(r->reason, (int)sizeof(r->reason), reason ? reason : "");
	c->type = NET_CMD_WS_CLOSE;
	c->id = ws->id;
	c->p = r;
	net_cmd_post(ws->net, c);
}

int UiWs_State(void *handle)
{
	UiWsH *ws = (UiWsH *)handle;
	return ws ? ws->state : -1;
}
