/*
 * net_ws_server.c - WebSocket 服务端 (ws / wss), 并入 UiNet IO 线程
 *
 * 结构:
 *   NetSrv      监听器 + 证书 + 连接表 (IO 线程)
 *   NetSrvConn  AcceptEx 接受的连接, 状态 TLS -> HTTP -> WS
 *   NetSrvAccept 挂起的 AcceptEx 操作
 *
 * 所有网络状态只在 IO 线程; 事件经 net_emit_srv_* -> APC 回 GUI 线程。
 */
#include "net/net_internal.h"
#include "log.h"

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int sprintf(char *buf, const char *fmt, ...);

#define SRV_GUID      "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define SRV_HDR_MAX   128

typedef struct NetSrvAccept {
	NetOp op;              /* 必须首成员: ov */
	NetSrv *srv;
	SOCKET sock;
	char addrbuf[2 * (sizeof(SOCKADDR_STORAGE) + 16)];
} NetSrvAccept;

static void srv_free_(NetSrv *srv);
static void srv_ws_parse_(NetSrvConn *c);

/* ── 小工具 ──────────────────────────────────────────────────────────── */

static void srv_cfg_free_(UiWsServerConfig *c)
{
	if (!c) return;
	if (c->bindHost) free((void *)c->bindHost);
	if (c->path) free((void *)c->path);
	if (c->subprotocols) free((void *)c->subprotocols);
	if (c->extraHeaders) free((void *)c->extraHeaders);
	if (c->certPath) free((void *)c->certPath);
	if (c->certPassword) free((void *)c->certPassword);
	if (c->certSubject) free((void *)c->certSubject);
	if (c->certThumbprint) free((void *)c->certThumbprint);
	memset(c, 0, sizeof(*c));
}

static char *srv_str_(const char *s, const char *needle)
{
	return Net_MemFind(s, (int)strlen(s), needle, (int)strlen(needle));
}

static char *srv_char_(const char *s, int c)
{
	while (*s) {
		if (*s == (char)c) return (char *)s;
		s++;
	}
	return NULL;
}

static void srv_trim_tail_(char *s)
{
	int n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
			s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = '\0';
}

static int srv_has_token_(const char *v, const char *want)
{
	int wl = (int)strlen(want);
	if (!v) return 0;
	while (*v) {
		const char *a, *b;
		int n;
		while (*v == ' ' || *v == '\t' || *v == ',') v++;
		if (!*v) break;
		a = v;
		while (*v && *v != ',') v++;
		b = v;
		while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
		n = (int)(b - a);
		if (n == wl && Net_StrCaseCmpN(a, want, wl) == 0) return 1;
	}
	return 0;
}

static int srv_pick_protocol_(const char *client, const char *server,
	char *out, int cap)
{
	if (!client || !server || !out || cap <= 0) return 0;
	out[0] = '\0';
	while (*client) {
		const char *a, *b, *s;
		int n;
		while (*client == ' ' || *client == '\t' || *client == ',') client++;
		if (!*client) break;
		a = client;
		while (*client && *client != ',') client++;
		b = client;
		while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
		n = (int)(b - a);
		s = server;
		while (*s) {
			const char *sa, *sb;
			int sn;
			while (*s == ' ' || *s == '\t' || *s == ',') s++;
			if (!*s) break;
			sa = s;
			while (*s && *s != ',') s++;
			sb = s;
			while (sb > sa && (sb[-1] == ' ' || sb[-1] == '\t')) sb--;
			sn = (int)(sb - sa);
			if (n == sn && Net_StrCaseCmpN(a, sa, n) == 0) {
				if (n >= cap) n = cap - 1;
				memcpy(out, a, (size_t)n);
				out[n] = '\0';
				return 1;
			}
		}
	}
	return 0;
}

static const char *srv_status_text_(int status)
{
	switch (status) {
	case 400: return "Bad Request";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 408: return "Request Timeout";
	case 426: return "Upgrade Required";
	case 431: return "Request Header Fields Too Large";
	case 500: return "Internal Server Error";
	case 503: return "Service Unavailable";
	default: return "Error";
	}
}

static void srv_peer_(const SOCKADDR *sa, char *out, int cap)
{
	char host[64];
	DWORD hlen = sizeof(host);
	int port = 0;

	out[0] = '\0';
	if (!sa) { Net_StrCopy(out, cap, "?"); return; }
	if (sa->sa_family == AF_INET) {
		InetNtopA(AF_INET, &((const SOCKADDR_IN *)sa)->sin_addr, host, hlen);
		port = ntohs(((const SOCKADDR_IN *)sa)->sin_port);
		sprintf(out, "%s:%d", host, port);
		return;
	}
	if (sa->sa_family == AF_INET6) {
		InetNtopA(AF_INET6, &((const SOCKADDR_IN6 *)sa)->sin6_addr, host, hlen);
		port = ntohs(((const SOCKADDR_IN6 *)sa)->sin6_port);
		sprintf(out, "[%s]:%d", host, port);
		return;
	}
	Net_StrCopy(out, cap, "?");
}

/* ── 事件 ────────────────────────────────────────────────────────────── */

void net_emit_srv_listening(NetIo *io, uint64_t srvId, int port)
{
	NetEv *ev = net_ev_new(io->owner, NET_EV_SRV_LISTENING, srvId);
	if (!ev) return;
	ev->srvId = srvId;
	ev->code = port;
	net_post(io, ev);
}

void net_emit_srv_error(NetIo *io, uint64_t srvId, int err, const char *msg)
{
	NetEv *ev = net_ev_new(io->owner, NET_EV_SRV_ERROR, srvId);
	if (!ev) return;
	ev->srvId = srvId;
	ev->code = err;
	Net_StrCopy(ev->reason, (int)sizeof(ev->reason), msg ? msg : "");
	net_post(io, ev);
}

void net_emit_srv_open(NetIo *io, NetSrvConn *c, NetSrvOpen *o)
{
	NetEv *ev;
	if (!c || !o) { free(o); return; }
	ev = net_ev_new(io->owner, NET_EV_SRV_OPEN, c->srv->id);
	if (!ev) {
		if (o->hdrs) free(o->hdrs);
		if (o->hdrStore) free(o->hdrStore);
		free(o);
		return;
	}
	ev->srvId = c->srv->id;
	ev->connId = c->id;
	ev->p = o;
	net_post(io, ev);
}

void net_emit_srv_msg(NetIo *io, uint64_t srvId, uint64_t connId,
	int opcode, const void *data, int len)
{
	NetWsMsg *m;
	NetEv *ev;

	if (len < 0) return;
	ev = net_ev_new(io->owner, NET_EV_SRV_MSG, srvId);
	if (!ev) return;
	m = (NetWsMsg *)malloc(sizeof(NetWsMsg) + (size_t)(len > 0 ? len : 0));
	if (!m) { free(ev); return; }
	m->opcode = opcode;
	m->len = len;
	if (len > 0) memcpy(m->data, data, (size_t)len);
	ev->srvId = srvId;
	ev->connId = connId;
	ev->p = m;
	net_post(io, ev);
}

void net_emit_srv_close(NetIo *io, uint64_t srvId, uint64_t connId,
	int code, const char *reason)
{
	NetEv *ev = net_ev_new(io->owner, NET_EV_SRV_CLOSE, srvId);
	if (!ev) return;
	ev->srvId = srvId;
	ev->connId = connId;
	ev->code = code;
	Net_StrCopy(ev->reason, (int)sizeof(ev->reason), reason ? reason : "");
	net_post(io, ev);
}

void net_emit_srv_stopped(NetIo *io, uint64_t srvId, HANDLE done)
{
	NetEv *ev = net_ev_new(io->owner, NET_EV_SRV_STOPPED, srvId);
	if (!ev) {
		if (done) SetEvent(done);
		return;
	}
	ev->srvId = srvId;
	ev->done = done;
	if (!net_post(io, ev) && done) SetEvent(done);
}

/* ── 定时器 ──────────────────────────────────────────────────────────── */

static void srv_timer_cancel_(NetSrvConn *c, int kind)
{
	NetTimer *t;
	if (!c || kind < 1 || kind >= NET_T_SRV_MAX) return;
	t = c->timers[kind];
	if (!t) return;
	c->timers[kind] = NULL;
	if (c->srv && c->srv->io) UiHeap_Erase(&c->srv->io->timers, &t->hn);
	free(t);
}

static void srv_timer_cancel_all_(NetSrvConn *c)
{
	int i;
	if (!c) return;
	for (i = 1; i < NET_T_SRV_MAX; i++) srv_timer_cancel_(c, i);
}

static void srv_timer_arm_(NetSrvConn *c, int kind, int ms)
{
	NetIo *io;
	NetTimer *t;

	if (!c || !c->srv || !c->srv->io || c->dead) return;
	if (kind < 1 || kind >= NET_T_SRV_MAX) return;
	if (ms < 0) ms = 0;
	srv_timer_cancel_(c, kind);
	io = c->srv->io;
	t = (NetTimer *)calloc(1, sizeof(NetTimer));
	if (!t) return;
	t->hn.idx = -1;
	t->hn.deadline = Net_NowMs() + (uint64_t)ms;
	t->hn.seq = ++io->seq;
	t->hn.ptr = t;
	t->ownerKind = NET_TIMER_SRV;
	t->sconn = c;
	t->kind = kind;
	if (!UiHeap_Push(&io->timers, &t->hn)) { free(t); return; }
	c->timers[kind] = t;
}

/* ── 连接生命周期 ────────────────────────────────────────────────────── */

static void srv_conn_emit_close_(NetSrvConn *c, int code, const char *reason)
{
	if (!c || !c->opened || c->emittedClose) return;
	c->emittedClose = 1;
	net_emit_srv_close(c->srv->io, c->srv->id, c->id, code, reason ? reason : "");
}

static void srv_maybe_stopped_(NetSrv *srv);

static void srv_conn_free_(NetSrvConn *c)
{
	NetSrv *srv;
	NetSrvConn **pp;

	if (!c) return;
	srv = c->srv;
	srv_timer_cancel_all_(c);
	NetBuf_Free(&c->in);
	NetBuf_Free(&c->plain);
	NetBuf_Free(&c->out);
	NetBuf_Free(&c->tx);
	NetBuf_Free(&c->frag);
	NetBuf_Free(&c->hdrStore);
	if (c->hdrs) free(c->hdrs);
	if (c->tls) { net_tls_server_conn_free(c); c->tls = NULL; }
	if (c->s != INVALID_SOCKET) { closesocket(c->s); c->s = INVALID_SOCKET; }

	if (srv) {
		for (pp = &srv->conns; *pp; pp = &(*pp)->next) {
			if (*pp == c) { *pp = c->next; break; }
		}
		if (srv->connCount > 0) srv->connCount--;
	}
	free(c);
	if (srv) srv_maybe_stopped_(srv);
}

static void srv_conn_addref_(NetSrvConn *c)
{
	if (c) c->refs++;
}

static void srv_conn_release_(NetSrvConn *c)
{
	if (!c) return;
	c->refs--;
	if (c->refs <= 0) srv_conn_free_(c);
}

static void srv_conn_close_(NetSrvConn *c)
{
	if (!c || c->dead || c->closing) return;
	c->closing = 1;
	if (c->tls && c->tls->established) net_tls_server_close_notify(c);
	c->dead = 1;
	srv_timer_cancel_all_(c);
	if (c->s != INVALID_SOCKET) {
		shutdown(c->s, SD_BOTH);
		CancelIoEx((HANDLE)c->s, NULL);
		closesocket(c->s);
		c->s = INVALID_SOCKET;
	}
	srv_conn_release_(c);       /* 注册表引用 */
}

NetSrv *net_srv_by_id(NetIo *io, uint64_t id)
{
	NetSrv *s;
	if (!io) return NULL;
	for (s = io->servers; s; s = s->next)
		if (s->id == id) return s;
	return NULL;
}

static NetSrvConn *srv_conn_by_id_(NetSrv *srv, uint64_t id)
{
	NetSrvConn *c;
	if (!srv) return NULL;
	for (c = srv->conns; c; c = c->next)
		if (c->id == id) return c;
	return NULL;
}

/* ── 发送 ────────────────────────────────────────────────────────────── */

static void buf_swap_srv_(NetBuf *a, NetBuf *b)
{
	NetBuf t = *a;
	*a = *b;
	*b = t;
}

static void srv_maybe_close_after_send_(NetSrvConn *c)
{
	if (!c || c->dead || !c->closeAfterSend) return;
	if (!c->sending && c->tx.len <= 0 && c->out.len <= 0)
		srv_conn_close_(c);
}

static void srv_tx_flush_(NetSrvConn *c)
{
	NetOp *op;
	WSABUF ws;
	int r;

	if (!c || c->dead || c->sending || c->s == INVALID_SOCKET) return;
	if (c->tx.len <= 0) {
		if (c->out.len <= 0) return;
		buf_swap_srv_(&c->tx, &c->out);
		NetBuf_Clear(&c->out);
	}
	if (c->tx.len <= 0) return;

	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) { srv_conn_close_(c); return; }
	op->kind = NET_OP_SRV_SEND;
	op->srvConn = c;
	ws.buf = c->tx.p;
	ws.len = (ULONG)c->tx.len;
	c->sending = 1;
	srv_conn_addref_(c);
	r = WSASend(c->s, &ws, 1, NULL, 0, &op->ov, NULL);
	if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
		c->sending = 0;
		free(op);
		srv_conn_release_(c);
		srv_conn_close_(c);
	}
}

static int srv_conn_write_(NetSrvConn *c, const void *data, int len)
{
	int ok;

	if (!c || c->dead || !c->srv) return 0;
	if (len <= 0) return 1;
	if (c->out.len + len > c->srv->cfg.maxSendBuffer) return 0;
	if (c->tls && c->tls->established)
		ok = net_tls_encrypt_buf(c->tls, &c->out, data, len);
	else
		ok = NetBuf_Append(&c->out, data, len);
	if (!ok) return 0;
	if (c->out.len > c->srv->cfg.maxSendBuffer) return 0;
	srv_tx_flush_(c);
	return !c->dead;
}

/* 明文直写 (TLS 握手 token / close_notify), 不经加密层 */
int srv_conn_write_raw_(NetSrvConn *c, const void *data, int len)
{
	if (!c || !c->srv) return 0;
	if (len <= 0) return 1;
	if (c->out.len + len > c->srv->cfg.maxSendBuffer) return 0;
	if (!NetBuf_Append(&c->out, data, len)) return 0;
	if (c->out.len > c->srv->cfg.maxSendBuffer) return 0;
	srv_tx_flush_(c);
	return !c->dead;
}

/* ── HTTP Upgrade 握手 ───────────────────────────────────────────────── */

static NetBuf *srv_src_(NetSrvConn *c)
{
	return c->tls ? &c->plain : &c->in;
}

static const char *srv_hdr_(NetSrvConn *c, const char *name)
{
	int i;
	for (i = 0; i < c->hdrCount; i++)
		if (Net_StrCaseEq(c->hdrs[i].name, name)) return c->hdrs[i].value;
	return NULL;
}

static int srv_add_hdr_(NetSrvConn *c, char *name, char *value)
{
	if (c->hdrCount >= SRV_HDR_MAX) return 0;
	if (c->hdrCount == c->hdrCap) {
		int cap = c->hdrCap ? c->hdrCap * 2 : 16;
		UiHttpHeader *p = (UiHttpHeader *)realloc(c->hdrs,
			(size_t)cap * sizeof(UiHttpHeader));
		if (!p) return 0;
		c->hdrs = p;
		c->hdrCap = cap;
	}
	c->hdrs[c->hdrCount].name = name;
	c->hdrs[c->hdrCount].value = value;
	c->hdrCount++;
	return 1;
}

static void srv_http_error_(NetSrvConn *c, int status, const char *extra)
{
	NetBuf b;
	char body[96];
	int blen;

	if (!c || c->dead || c->state == NET_SRV_C_CLOSING) return;
	sprintf(body, "%d %s\n", status, srv_status_text_(status));
	blen = (int)strlen(body);
	NetBuf_Init(&b);
	NetBuf_AppendFmt(&b,
		"HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
		"Content-Length: %d\r\nConnection: close\r\n",
		status, srv_status_text_(status), blen);
	if (extra && extra[0]) NetBuf_AppendStr(&b, extra);
	NetBuf_AppendStr(&b, "\r\n");
	NetBuf_Append(&b, body, blen);
	if (!srv_conn_write_(c, b.p, b.len)) {
		NetBuf_Free(&b);
		srv_conn_close_(c);
		return;
	}
	NetBuf_Free(&b);
	c->state = NET_SRV_C_CLOSING;
	c->closeAfterSend = 1;
	srv_timer_cancel_(c, NET_T_SRV_HANDSHAKE);
	srv_timer_cancel_(c, NET_T_SRV_PING);
	srv_timer_cancel_(c, NET_T_SRV_IDLE);
	srv_timer_arm_(c, NET_T_SRV_CLOSE, c->srv->cfg.closeTimeoutMs);
	srv_maybe_close_after_send_(c);
}

static int srv_http_parse_(NetSrvConn *c)
{
	char *p = c->hdrStore.p;
	char *e, *sp1, *sp2, *q;
	const char *v;
	char *method, *path, *version;
	int plen;

	if (!p) { srv_http_error_(c, 400, NULL); return 0; }
	e = srv_str_(p, "\r\n");
	if (!e) { srv_http_error_(c, 400, NULL); return 0; }
	*e = '\0';
	sp1 = srv_char_(p, ' ');
	if (!sp1) { srv_http_error_(c, 400, NULL); return 0; }
	*sp1++ = '\0';
	sp2 = srv_char_(sp1, ' ');
	if (!sp2) { srv_http_error_(c, 400, NULL); return 0; }
	*sp2++ = '\0';
	method = p;
	path = sp1;
	version = sp2;
	if (strcmp(method, "GET") != 0) { srv_http_error_(c, 405, NULL); return 0; }
	if (strcmp(version, "HTTP/1.1") != 0) { srv_http_error_(c, 400, NULL); return 0; }

	plen = 0;
	while (path[plen] && path[plen] != '?' && plen < NET_URL_MAX - 1) {
		c->path[plen] = path[plen];
		plen++;
	}
	c->path[plen] = '\0';
	if (c->srv->cfg.path && strcmp(c->path, c->srv->cfg.path) != 0) {
		srv_http_error_(c, 404, NULL);
		return 0;
	}

	c->hdrCount = 0;
	q = e + 2;
	while (*q) {
		char *le = srv_str_(q, "\r\n");
		char *colon;
		if (!le) break;
		if (le == q) break;
		*le = '\0';
		colon = srv_char_(q, ':');
		if (colon) {
			char *val;
			*colon = '\0';
			val = colon + 1;
			while (*val == ' ' || *val == '\t') val++;
			srv_trim_tail_(val);
			if (!srv_add_hdr_(c, q, val)) {
				srv_http_error_(c, 431, NULL);
				return 0;
			}
		}
		q = le + 2;
	}

	v = srv_hdr_(c, "Upgrade");
	if (!v || !srv_has_token_(v, "websocket")) {
		srv_http_error_(c, 400, NULL);
		return 0;
	}
	v = srv_hdr_(c, "Connection");
	if (!v || !srv_has_token_(v, "upgrade")) {
		srv_http_error_(c, 400, NULL);
		return 0;
	}
	v = srv_hdr_(c, "Sec-WebSocket-Version");
	if (!v || !srv_has_token_(v, "13")) {
		srv_http_error_(c, 426, "Sec-WebSocket-Version: 13\r\n");
		return 0;
	}
	v = srv_hdr_(c, "Sec-WebSocket-Key");
	if (!v || !v[0] || (int)strlen(v) > 60) {
		srv_http_error_(c, 400, NULL);
		return 0;
	}

	c->protocol[0] = '\0';
	{
		const char *cp = srv_hdr_(c, "Sec-WebSocket-Protocol");
		if (cp && c->srv->cfg.subprotocols)
			srv_pick_protocol_(cp, c->srv->cfg.subprotocols,
				c->protocol, (int)sizeof(c->protocol));
	}

	Net_StrCopy(c->key, (int)sizeof(c->key), v);
	{
		char buf[128];
		BYTE hash[20];
		int n = Net_StrCopy(buf, (int)sizeof(buf), c->key);
		Net_StrCopy(buf + n, (int)sizeof(buf) - n, SRV_GUID);
		if (!Net_Sha1(buf, (int)strlen(buf), hash) ||
		    Net_Base64(hash, 20, c->accept, (int)sizeof(c->accept)) <= 0) {
			srv_http_error_(c, 500, NULL);
			return 0;
		}
	}

	{
		NetBuf b;
		NetSrvOpen *o;
		NetBuf_Init(&b);
		NetBuf_AppendFmt(&b,
			"HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\nConnection: Upgrade\r\n"
			"Sec-WebSocket-Accept: %s\r\n", c->accept);
		if (c->protocol[0])
			NetBuf_AppendFmt(&b, "Sec-WebSocket-Protocol: %s\r\n", c->protocol);
		if (c->srv->cfg.extraHeaders && c->srv->cfg.extraHeaders[0]) {
			int n = (int)strlen(c->srv->cfg.extraHeaders);
			NetBuf_AppendStr(&b, c->srv->cfg.extraHeaders);
			if (n < 2 || c->srv->cfg.extraHeaders[n - 1] != '\n')
				NetBuf_AppendStr(&b, "\r\n");
		}
		NetBuf_AppendStr(&b, "\r\n");
		if (!srv_conn_write_(c, b.p, b.len)) {
			NetBuf_Free(&b);
			srv_conn_close_(c);
			return 0;
		}
		NetBuf_Free(&b);

		c->upgraded = 1;
		c->opened = 1;
		c->state = NET_SRV_C_WS;
		c->lastRecv = Net_NowMs();
		srv_timer_cancel_(c, NET_T_SRV_HANDSHAKE);
		if (c->srv->cfg.pingIntervalMs > 0)
			srv_timer_arm_(c, NET_T_SRV_PING, c->srv->cfg.pingIntervalMs);
		if (c->srv->cfg.idleTimeoutMs > 0)
			srv_timer_arm_(c, NET_T_SRV_IDLE, c->srv->cfg.idleTimeoutMs);

		o = (NetSrvOpen *)calloc(1, sizeof(NetSrvOpen));
		if (o) {
			Net_StrCopy(o->path, (int)sizeof(o->path), c->path);
			Net_StrCopy(o->peer, (int)sizeof(o->peer), c->peer);
			Net_StrCopy(o->protocol, (int)sizeof(o->protocol), c->protocol);
			o->hdrStore = c->hdrStore.p;
			o->hdrs = c->hdrs;
			o->hdrCount = c->hdrCount;
			NetBuf_Init(&c->hdrStore);
			c->hdrs = NULL;
			c->hdrCount = 0;
			c->hdrCap = 0;
			net_emit_srv_open(c->srv->io, c, o);
		} else {
			net_emit_srv_error(c->srv->io, c->srv->id, UI_NET_E_OOM,
				"open event alloc failed");
		}
	}
	return 1;
}

static void srv_http_on_data_(NetSrvConn *c)
{
	NetBuf *src = srv_src_(c);
	char *he;
	int hlen;

	he = Net_MemFind(src->p, src->len, "\r\n\r\n", 4);
	if (!he) {
		if (src->len > NET_HDR_MAX) srv_http_error_(c, 431, NULL);
		return;
	}
	hlen = (int)(he - src->p) + 4;
	if (hlen > NET_HDR_MAX) { srv_http_error_(c, 431, NULL); return; }
	NetBuf_Clear(&c->hdrStore);
	if (!NetBuf_Append(&c->hdrStore, src->p, hlen)) {
		srv_conn_close_(c);
		return;
	}
	NetBuf_Consume(src, hlen);
	if (!srv_http_parse_(c)) return;
	if (c->state == NET_SRV_C_WS && !c->dead) srv_ws_parse_(c);
}

/* ── WebSocket 帧 ────────────────────────────────────────────────────── */

static int srv_ws_send_frame_(NetSrvConn *c, int opcode, const void *data,
	int len, int fin)
{
	NetBuf b;
	int ok;

	if (!c || c->dead) return 0;
	if (len < 0 || len > c->srv->cfg.maxMessageSize) return 0;
	NetBuf_Init(&b);
	if (!net_ws_build_frame(0, opcode, data, len, fin, &b)) {
		NetBuf_Free(&b);
		return 0;
	}
	ok = srv_conn_write_(c, b.p, b.len);
	NetBuf_Free(&b);
	return ok && !c->dead;
}

static int srv_ws_send_close_(NetSrvConn *c, int code, const char *reason)
{
	char p[125];
	int n = 2;
	int rl = reason ? (int)strlen(reason) : 0;

	if (rl > (int)sizeof(p) - 2) rl = (int)sizeof(p) - 2;
	p[0] = (char)((code >> 8) & 0xFF);
	p[1] = (char)(code & 0xFF);
	if (rl > 0) memcpy(p + 2, reason, (size_t)rl);
	n += rl;
	return srv_ws_send_frame_(c, 8, p, n, 1);
}

static int srv_close_code_ok_(int code)
{
	if (code < 1000 || code > 4999) return 0;
	if (code == 1004 || code == 1005 || code == 1006 || code == 1015) return 0;
	return 1;
}

static void srv_ws_fail_(NetSrvConn *c, int code, const char *reason)
{	if (!c || c->dead) return;
	if (c->upgraded && c->state != NET_SRV_C_CLOSING) {
		if (!c->closeSent) {
			if (!srv_ws_send_close_(c, code, reason)) {
				srv_conn_emit_close_(c, code, reason);
				srv_conn_close_(c);
				return;
			}
			c->closeSent = 1;
		}
		c->closeCode = code;
		Net_StrCopy(c->closeReason, (int)sizeof(c->closeReason),
			reason ? reason : "");
		srv_conn_emit_close_(c, code, reason);
		c->state = NET_SRV_C_CLOSING;
		c->closeAfterSend = 1;
		srv_timer_cancel_(c, NET_T_SRV_HANDSHAKE);
		srv_timer_cancel_(c, NET_T_SRV_PING);
		srv_timer_cancel_(c, NET_T_SRV_IDLE);
		srv_timer_arm_(c, NET_T_SRV_CLOSE, c->srv->cfg.closeTimeoutMs);
		srv_maybe_close_after_send_(c);
		return;
	}
	srv_conn_emit_close_(c, code, reason);
	srv_conn_close_(c);
}

static void srv_ws_handle_close_(NetSrvConn *c, const char *data, int len)
{
	int code = 1000;
	char reason[124];

	reason[0] = '\0';
	if (len == 1) { srv_ws_fail_(c, 1002, "bad close frame"); return; }
	if (len >= 2) {
		int rl;
		code = (((BYTE)data[0] << 8) | (BYTE)data[1]);
		if (!srv_close_code_ok_(code)) {
			srv_ws_fail_(c, 1002, "bad close code");
			return;
		}
		rl = len - 2;
		if (rl > (int)sizeof(reason) - 1) rl = (int)sizeof(reason) - 1;
		if (rl > 0) {
			memcpy(reason, data + 2, (size_t)rl);
			if (!net_ws_utf8_valid(reason, rl)) {
				srv_ws_fail_(c, 1007, "bad close reason");
				return;
			}
		}
		reason[rl] = '\0';
	}

	c->closeRecv = 1;
	c->closeCode = code;
	Net_StrCopy(c->closeReason, (int)sizeof(c->closeReason), reason);
	if (!c->closeSent) {
		if (!srv_ws_send_close_(c, code, reason)) {
			srv_conn_emit_close_(c, code, reason);
			srv_conn_close_(c);
			return;
		}
		c->closeSent = 1;
	}
	srv_conn_emit_close_(c, code, reason);
	c->state = NET_SRV_C_CLOSING;
	c->closeAfterSend = 1;
	srv_timer_cancel_(c, NET_T_SRV_PING);
	srv_timer_cancel_(c, NET_T_SRV_IDLE);
	srv_timer_arm_(c, NET_T_SRV_CLOSE, c->srv->cfg.closeTimeoutMs);
	srv_maybe_close_after_send_(c);
}

static void srv_ws_parse_(NetSrvConn *c)
{
	NetBuf *src;
	int guard = 0;

	if (!c || c->dead) return;
	src = srv_src_(c);
	while (!c->dead && guard++ < 512) {
		int fin, opcode, masked, hlen, r;
		uint64_t plen;
		BYTE mask[4];
		char *payload;
		int i;

		r = net_ws_parse_header(src->p, src->len, &fin, &opcode, &masked,
			mask, &plen, &hlen);
		if (r == 0) return;
		if (r < 0) { srv_ws_fail_(c, 1002, "bad frame header"); return; }
		if (!masked) { srv_ws_fail_(c, 1002, "client frame not masked"); return; }
		if (plen > (uint64_t)c->srv->cfg.maxMessageSize) {
			srv_ws_fail_(c, 1009, "message too big");
			return;
		}
		if (opcode >= 8) {
			if (!fin || plen > 125) {
				srv_ws_fail_(c, 1002, "bad control frame");
				return;
			}
			if (opcode != 8 && opcode != 9 && opcode != 10) {
				srv_ws_fail_(c, 1002, "bad opcode");
				return;
			}
		} else if (opcode != 0 && opcode != 1 && opcode != 2) {
			srv_ws_fail_(c, 1002, "bad opcode");
			return;
		} else if (plen > (uint64_t)(0x7FFFFFF0)) {
			srv_ws_fail_(c, 1009, "message too big");
			return;
		}
		if (plen > (uint64_t)(src->len - hlen)) return;

		payload = (char *)malloc((size_t)(plen ? (int)plen : 1));
		if (!payload) { srv_ws_fail_(c, 1011, "out of memory"); return; }
		if (plen > 0) {
			for (i = 0; i < (int)plen; i++)
				payload[i] = (char)(src->p[hlen + i] ^ mask[i & 3]);
		}
		NetBuf_Consume(src, hlen + (int)plen);
		c->lastRecv = Net_NowMs();

		if (opcode == 8) {
			srv_ws_handle_close_(c, payload, (int)plen);
			free(payload);
			return;
		}
		if (opcode == 9) {
			if (!srv_ws_send_frame_(c, 10, payload, (int)plen, 1)) {
				free(payload);
				srv_ws_fail_(c, 1011, "pong failed");
				return;
			}
			free(payload);
			continue;
		}
		if (opcode == 10) {
			c->pingOutstanding = 0;
			free(payload);
			continue;
		}

		if (opcode == 1 || opcode == 2) {
			if (c->fragOpcode || c->frag.len > 0) {
				free(payload);
				srv_ws_fail_(c, 1002, "new message during fragmentation");
				return;
			}
			if (fin) {
				if (opcode == 1 && !net_ws_utf8_valid(payload, (int)plen)) {
					free(payload);
					srv_ws_fail_(c, 1007, "invalid utf-8");
					return;
				}
				net_emit_srv_msg(c->srv->io, c->srv->id, c->id, opcode,
					payload, (int)plen);
			} else {
				c->fragOpcode = opcode;
				if (!NetBuf_Append(&c->frag, payload, (int)plen)) {
					free(payload);
					srv_ws_fail_(c, 1011, "out of memory");
					return;
				}
			}
			free(payload);
			continue;
		}

		/* opcode 0: continuation */
		if (!c->fragOpcode && c->frag.len == 0) {
			free(payload);
			srv_ws_fail_(c, 1002, "unexpected continuation");
			return;
		}
		if (c->frag.len + (int)plen > c->srv->cfg.maxMessageSize ||
		    !NetBuf_Append(&c->frag, payload, (int)plen)) {
			free(payload);
			srv_ws_fail_(c, 1009, "message too big");
			return;
		}
		free(payload);
		if (fin) {
			if (c->fragOpcode == 1 &&
			    !net_ws_utf8_valid(c->frag.p, c->frag.len)) {
				NetBuf_Clear(&c->frag);
				c->fragOpcode = 0;
				srv_ws_fail_(c, 1007, "invalid utf-8");
				return;
			}
			net_emit_srv_msg(c->srv->io, c->srv->id, c->id,
				c->fragOpcode, c->frag.p, c->frag.len);
			NetBuf_Clear(&c->frag);
			c->fragOpcode = 0;
		}
	}
}

static void srv_ws_on_data_(NetSrvConn *c)
{
	if (!c || c->dead) return;
	if (c->state == NET_SRV_C_HTTP) srv_http_on_data_(c);
	else if (c->state == NET_SRV_C_WS) srv_ws_parse_(c);
}

void net_srv_on_plain(NetSrvConn *c)
{
	srv_ws_on_data_(c);
}

void srv_conn_fail_(NetSrvConn *c, int err, const char *msg)
{
	(void)err;
	if (!c || c->dead) return;
	if (c->upgraded) {
		srv_ws_fail_(c, 1011, msg ? msg : "tls error");
		return;
	}
	srv_conn_emit_close_(c, 1006, msg ? msg : "tls error");
	srv_conn_close_(c);
}

static void srv_user_close_(NetSrvConn *c, int code, const char *reason)
{
	if (!c || c->dead || c->userClosed) return;
	c->userClosed = 1;
	if (!srv_close_code_ok_(code)) code = 1000;
	c->closeCode = code;
	Net_StrCopy(c->closeReason, (int)sizeof(c->closeReason),
		reason ? reason : "");
	if (!c->upgraded || c->state == NET_SRV_C_CLOSING) {
		srv_conn_emit_close_(c, code, c->closeReason);
		srv_conn_close_(c);
		return;
	}
	if (!c->closeSent) {
		if (!srv_ws_send_close_(c, code, c->closeReason)) {
			srv_conn_emit_close_(c, code, c->closeReason);
			srv_conn_close_(c);
			return;
		}
		c->closeSent = 1;
	}
	srv_conn_emit_close_(c, code, c->closeReason);
	c->state = NET_SRV_C_CLOSING;
	c->closeAfterSend = 1;
	srv_timer_cancel_(c, NET_T_SRV_PING);
	srv_timer_cancel_(c, NET_T_SRV_IDLE);
	srv_timer_arm_(c, NET_T_SRV_CLOSE, c->srv->cfg.closeTimeoutMs);
	srv_maybe_close_after_send_(c);
}

/* ── 接收 ────────────────────────────────────────────────────────────── */

static void srv_recv_(NetSrvConn *c)
{
	NetOp *op;
	WSABUF ws;
	DWORD flags = 0;
	int r;

	if (!c || c->dead || c->rxStopped || c->rxPending || c->s == INVALID_SOCKET)
		return;
	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) return;
	op->buf = (char *)malloc(NET_RECV_SIZE);
	if (!op->buf) { free(op); return; }
	op->bufCap = NET_RECV_SIZE;
	op->kind = NET_OP_SRV_RECV;
	op->srvConn = c;

	ws.buf = op->buf;
	ws.len = NET_RECV_SIZE;
	c->rxPending = 1;
	srv_conn_addref_(c);
	r = WSARecv(c->s, &ws, 1, NULL, &flags, &op->ov, NULL);
	if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
		c->rxPending = 0;
		free(op->buf);
		free(op);
		srv_conn_release_(c);
		srv_conn_close_(c);
	}
}

static void srv_on_recv_data_(NetSrvConn *c)
{
	if (!c || c->dead) return;
	if (c->tls && !c->tls->established) {
		net_tls_server_input(c);
		return;
	}
	if (c->tls) {
		net_tls_server_decrypt(c);
		return;
	}
	srv_ws_on_data_(c);
}

/* ── 定时器事件 ──────────────────────────────────────────────────────── */

void net_srv_conn_timer_fire(NetSrvConn *c, int kind)
{
	if (!c || c->dead) return;
	srv_conn_addref_(c);
	switch (kind) {
	case NET_T_SRV_HANDSHAKE:
		if (c->state == NET_SRV_C_HTTP)
			srv_http_error_(c, 408, NULL);
		else
			srv_conn_close_(c);
		break;
	case NET_T_SRV_PING:
		if (!c->upgraded || c->state == NET_SRV_C_CLOSING) break;
		if (c->pingOutstanding) {
			srv_conn_emit_close_(c, 1006, "ping timeout");
			srv_conn_close_(c);
			break;
		}
		if (srv_ws_send_frame_(c, 9, NULL, 0, 1)) {
			c->pingOutstanding = 1;
			srv_timer_arm_(c, NET_T_SRV_PING, c->srv->cfg.pingIntervalMs);
		} else {
			srv_conn_emit_close_(c, 1006, "ping failed");
			srv_conn_close_(c);
		}
		break;
	case NET_T_SRV_IDLE:
		srv_conn_emit_close_(c, 1001, "idle timeout");
		srv_conn_close_(c);
		break;
	case NET_T_SRV_CLOSE:
		if (c->opened && !c->emittedClose)
			srv_conn_emit_close_(c, c->closeCode ? c->closeCode : 1000,
				c->closeReason);
		srv_conn_close_(c);
		break;
	default:
		break;
	}
	srv_conn_release_(c);
}

/* ── 监听 / 停止 ─────────────────────────────────────────────────────── */

static int load_acceptex_(NetIo *io)
{
	GUID g1 = WSAID_ACCEPTEX;
	GUID g2 = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD bytes = 0;
	SOCKET s;
	int ok = 1;

	if (io->acceptEx && io->getAcceptExSockaddrs) return 1;
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return 0;
	if (!io->acceptEx) {
		ok = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &g1, sizeof(g1),
			&io->acceptEx, sizeof(io->acceptEx), &bytes, NULL, NULL) == 0;
	}
	if (ok && !io->getAcceptExSockaddrs) {
		ok = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &g2, sizeof(g2),
			&io->getAcceptExSockaddrs, sizeof(io->getAcceptExSockaddrs),
			&bytes, NULL, NULL) == 0;
	}
	closesocket(s);
	return ok && io->acceptEx && io->getAcceptExSockaddrs;
}

static int srv_arm_accept_(NetSrv *srv)
{
	NetIo *io = srv->io;
	NetSrvAccept *a;
	DWORD llen = (DWORD)(sizeof(SOCKADDR_STORAGE) + 16);
	int e;

	if (srv->stopping || srv->listenSock == INVALID_SOCKET) return 0;
	if (srv->acceptPending) return 1;
	a = (NetSrvAccept *)calloc(1, sizeof(NetSrvAccept));
	if (!a) return 0;
	a->srv = srv;
	a->sock = WSASocketW(srv->family, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
		WSA_FLAG_OVERLAPPED);
	if (a->sock == INVALID_SOCKET) { free(a); return 0; }
	a->op.kind = NET_OP_SRV_ACCEPT;
	if (!io->acceptEx(srv->listenSock, a->sock, a->addrbuf, 0, llen, llen,
			NULL, &a->op.ov)) {
		e = WSAGetLastError();
		if (e != WSA_IO_PENDING) {
			closesocket(a->sock);
			free(a);
			return 0;
		}
	}
	srv->acceptPending = 1;
	return 1;
}

static void srv_reject_socket_(NetSrv *srv, SOCKET s)
{
	if (srv && !srv->tls) {
		const char *r = "HTTP/1.1 503 Service Unavailable\r\n"
			"Content-Length: 0\r\nConnection: close\r\n\r\n";
		send(s, r, (int)strlen(r), 0);
	}
	closesocket(s);
}

static void srv_maybe_stopped_(NetSrv *srv)
{
	HANDLE done;
	if (!srv || !srv->stopping || srv->state == NET_SRV_ST_STOPPED) return;
	if (srv->inStop) return;
	if (srv->io && srv->io->stopping) return;
	if (srv->acceptPending > 0 || srv->connCount > 0) return;
	srv->state = NET_SRV_ST_STOPPED;
	done = srv->stopDone;
	srv->stopDone = NULL;
	if (done) net_emit_srv_stopped(srv->io, srv->id, done);
	srv_free_(srv);
}

static void srv_free_(NetSrv *srv)
{
	NetIo *io;
	NetSrv **pp;

	if (!srv) return;
	io = srv->io;
	if (srv->listenSock != INVALID_SOCKET) {
		CancelIoEx((HANDLE)srv->listenSock, NULL);
		closesocket(srv->listenSock);
		srv->listenSock = INVALID_SOCKET;
	}
	net_tls_server_free(srv);
	srv_cfg_free_(&srv->cfg);
	if (io) {
		for (pp = &io->servers; *pp; pp = &(*pp)->next) {
			if (*pp == srv) { *pp = srv->next; break; }
		}
	}
	free(srv);
}

static void srv_start_fail_(NetSrv *srv, int err, const char *msg)
{
	NetIo *io;
	if (!srv) return;
	io = srv->io;
	net_emit_srv_error(io, srv->id, err, msg);
	srv_free_(srv);
}

void net_srv_start(NetIo *io, NetSrvStart *st)
{
	NetSrv *srv;
	SOCKADDR_STORAGE addr;
	int alen = 0;
	int e;

	if (!io || !st) { if (st) { srv_cfg_free_(&st->cfg); free(st); } return; }
	srv = (NetSrv *)calloc(1, sizeof(NetSrv));
	if (!srv) {
		net_emit_srv_error(io, st->id, UI_NET_E_OOM, "server alloc failed");
		srv_cfg_free_(&st->cfg);
		free(st);
		return;
	}
	srv->io = io;
	srv->id = st->id;
	srv->cfg = st->cfg;
	memset(&st->cfg, 0, sizeof(st->cfg));
	free(st);
	srv->listenSock = INVALID_SOCKET;
	srv->state = NET_SRV_ST_STARTING;
	if (srv->cfg.maxConnections <= 0) srv->cfg.maxConnections = 1024;
	if (srv->cfg.maxMessageSize <= 0) srv->cfg.maxMessageSize = 16 * 1024 * 1024;
	if (srv->cfg.maxSendBuffer <= 0) srv->cfg.maxSendBuffer = 16 * 1024 * 1024;
	if (srv->cfg.handshakeTimeoutMs <= 0) srv->cfg.handshakeTimeoutMs = 10000;
	if (srv->cfg.closeTimeoutMs <= 0) srv->cfg.closeTimeoutMs = 2000;
	srv->tls = (srv->cfg.certPath || srv->cfg.certSubject ||
		srv->cfg.certThumbprint) ? 1 : 0;
	srv->next = io->servers;
	io->servers = srv;

	if (srv->tls) {
		e = net_tls_server_init(srv);
		if (e) {
			Log_Write("[srv] tls init failed %d", e);
			srv_start_fail_(srv, e, UiNet_StrError(e));
			return;
		}
	}
	if (!load_acceptex_(io)) {
		srv_start_fail_(srv, UI_NET_E_INTERNAL, "AcceptEx unavailable");
		return;
	}

	memset(&addr, 0, sizeof(addr));
	if (srv->cfg.bindHost && srv->cfg.bindHost[0]) {
		if (InetPtonA(AF_INET, srv->cfg.bindHost,
				&((SOCKADDR_IN *)&addr)->sin_addr) == 1) {
			((SOCKADDR_IN *)&addr)->sin_family = AF_INET;
			alen = (int)sizeof(SOCKADDR_IN);
			srv->family = AF_INET;
		} else if (InetPtonA(AF_INET6, srv->cfg.bindHost,
				&((SOCKADDR_IN6 *)&addr)->sin6_addr) == 1) {
			((SOCKADDR_IN6 *)&addr)->sin6_family = AF_INET6;
			alen = (int)sizeof(SOCKADDR_IN6);
			srv->family = AF_INET6;
		} else {
			srv_start_fail_(srv, UI_NET_E_PARAM, "bad bindHost");
			return;
		}
	} else {
		((SOCKADDR_IN *)&addr)->sin_family = AF_INET;
		((SOCKADDR_IN *)&addr)->sin_addr.s_addr = INADDR_ANY;
		alen = (int)sizeof(SOCKADDR_IN);
		srv->family = AF_INET;
	}
	if (srv->family == AF_INET) {
		((SOCKADDR_IN *)&addr)->sin_port = htons((u_short)srv->cfg.port);
	} else {
		((SOCKADDR_IN6 *)&addr)->sin6_port = htons((u_short)srv->cfg.port);
		{
			DWORD v6only = 0;
			(void)v6only;
		}
	}

	srv->listenSock = WSASocketW(srv->family, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
		WSA_FLAG_OVERLAPPED);
	if (srv->listenSock == INVALID_SOCKET) {
		srv_start_fail_(srv, UI_NET_E_LISTEN, "socket failed");
		return;
	}
	{
		BOOL ex = TRUE;
		setsockopt(srv->listenSock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			(const char *)&ex, sizeof(ex));
	}
	if (bind(srv->listenSock, (SOCKADDR *)&addr, alen) != 0 ||
	    listen(srv->listenSock, 128) != 0) {
		srv_start_fail_(srv, UI_NET_E_LISTEN, "bind/listen failed");
		return;
	}
	{
		SOCKADDR_STORAGE local;
		int llen = (int)sizeof(local);
		if (getsockname(srv->listenSock, (SOCKADDR *)&local, &llen) == 0) {
			if (local.ss_family == AF_INET)
				srv->port = ntohs(((SOCKADDR_IN *)&local)->sin_port);
			else
				srv->port = ntohs(((SOCKADDR_IN6 *)&local)->sin6_port);
		}
	}
	if (CreateIoCompletionPort((HANDLE)srv->listenSock, io->iocp, 0, 0) == NULL) {
		srv_start_fail_(srv, UI_NET_E_LISTEN, "iocp bind failed");
		return;
	}
	srv->state = NET_SRV_ST_LISTENING;
	if (!srv_arm_accept_(srv)) {
		srv_start_fail_(srv, UI_NET_E_LISTEN, "AcceptEx failed");
		return;
	}
	net_emit_srv_listening(io, srv->id, srv->port);
	if (srv->cfg.trace) Log_Write("[srv] %llu listen %d%s", srv->id, srv->port,
		srv->tls ? " wss" : " ws");
}

static void srv_stop_conn_(NetSrvConn *c)
{
	if (!c || c->dead) return;
	if (c->upgraded && !c->closeSent) {
		if (!srv_ws_send_close_(c, 1001, "server stopping"))
			c->closeAfterSend = 1;
		else
			c->closeSent = 1;
	}
	c->closeCode = 1001;
	Net_StrCopy(c->closeReason, (int)sizeof(c->closeReason), "server stopping");
	srv_conn_emit_close_(c, 1001, "server stopping");
	c->state = NET_SRV_C_CLOSING;
	c->closeAfterSend = 1;
	srv_timer_cancel_(c, NET_T_SRV_HANDSHAKE);
	srv_timer_cancel_(c, NET_T_SRV_PING);
	srv_timer_cancel_(c, NET_T_SRV_IDLE);
	srv_timer_arm_(c, NET_T_SRV_CLOSE, c->srv->cfg.closeTimeoutMs);
	srv_maybe_close_after_send_(c);
}

void net_srv_stop(NetIo *io, uint64_t id, HANDLE done)
{
	NetSrv *srv = net_srv_by_id(io, id);
	NetSrvConn *c;

	if (!srv) {
		if (done) SetEvent(done);
		return;
	}
	if (srv->stopping) {
		if (done) {
			if (srv->stopDone) SetEvent(srv->stopDone);
			srv->stopDone = done;
		}
		return;
	}
	srv->stopping = 1;
	srv->state = NET_SRV_ST_STOPPING;
	srv->stopDone = done;
	if (srv->listenSock != INVALID_SOCKET) {
		CancelIoEx((HANDLE)srv->listenSock, NULL);
		closesocket(srv->listenSock);
		srv->listenSock = INVALID_SOCKET;
	}
	srv->inStop = 1;
	c = srv->conns;
	while (c) {
		NetSrvConn *next = c->next;
		srv_conn_addref_(c);
		srv_stop_conn_(c);
		srv_conn_release_(c);
		c = next;
	}
	srv->inStop = 0;
	srv_maybe_stopped_(srv);
}

void net_srv_cancel_all(NetIo *io)
{
	NetSrv *srv;
	if (!io) return;
	for (srv = io->servers; srv; srv = srv->next) {
		NetSrvConn *c;
		srv->stopping = 1;
		srv->state = NET_SRV_ST_STOPPING;
		if (srv->listenSock != INVALID_SOCKET) {
			CancelIoEx((HANDLE)srv->listenSock, NULL);
			closesocket(srv->listenSock);
			srv->listenSock = INVALID_SOCKET;
		}
		for (c = srv->conns; c; c = c->next) {
			c->dead = 1;
			srv_timer_cancel_all_(c);
			if (c->s != INVALID_SOCKET) {
				shutdown(c->s, SD_BOTH);
				CancelIoEx((HANDLE)c->s, NULL);
				closesocket(c->s);
				c->s = INVALID_SOCKET;
			}
		}
	}
}

void net_srv_free_all(NetIo *io)
{
	NetSrv *srv;
	if (!io) return;
	srv = io->servers;
	io->servers = NULL;
	while (srv) {
		NetSrv *next = srv->next;
		NetSrvConn *c = srv->conns;
		srv->conns = NULL;
		while (c) {
			NetSrvConn *cn = c->next;
			srv_timer_cancel_all_(c);
			NetBuf_Free(&c->in);
			NetBuf_Free(&c->plain);
			NetBuf_Free(&c->out);
			NetBuf_Free(&c->tx);
			NetBuf_Free(&c->frag);
			NetBuf_Free(&c->hdrStore);
			if (c->hdrs) free(c->hdrs);
			if (c->tls) { net_tls_server_conn_free(c); c->tls = NULL; }
			if (c->s != INVALID_SOCKET) closesocket(c->s);
			free(c);
			c = cn;
		}
		net_tls_server_free(srv);
		if (srv->listenSock != INVALID_SOCKET) closesocket(srv->listenSock);
		srv_cfg_free_(&srv->cfg);
		free(srv);
		srv = next;
	}
}

void net_srv_cleanup_op(NetOp *op)
{
	if (!op) return;
	if (op->kind == NET_OP_SRV_ACCEPT) {
		NetSrvAccept *a = (NetSrvAccept *)op;
		if (a->sock != INVALID_SOCKET) closesocket(a->sock);
		a->sock = INVALID_SOCKET;
	} else if (op->buf) {
		free(op->buf);
		op->buf = NULL;
	}
}

/* ── Accept 完成 ─────────────────────────────────────────────────────── */

static void srv_on_accept_(NetSrv *srv, NetSrvAccept *a, DWORD err)
{
	NetSrvConn *c;
	SOCKADDR *local = NULL, *remote = NULL;
	int llen = 0, rlen = 0;
	SOCKET s = a->sock;

	srv->acceptPending = 0;
	if (err != 0 || s == INVALID_SOCKET) {
		if (s != INVALID_SOCKET) closesocket(s);
		free(a);
		if (!srv->stopping) {
			if (!srv_arm_accept_(srv))
				net_emit_srv_error(srv->io, srv->id, UI_NET_E_LISTEN,
					"re-arm accept failed");
		} else {
			srv_maybe_stopped_(srv);
		}
		return;
	}

	{
		DWORD llenArg = (DWORD)(sizeof(SOCKADDR_STORAGE) + 16);
		srv->io->getAcceptExSockaddrs(a->addrbuf, 0, llenArg, llenArg,
			&local, &llen, &remote, &rlen);
	}
	{
		SOCKET ls = srv->listenSock;
		setsockopt(s, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
			(const char *)&ls, sizeof(ls));
	}
	if (srv->stopping || srv->connCount >= srv->cfg.maxConnections) {
		srv_reject_socket_(srv, s);
		free(a);
		if (!srv->stopping) {
			if (!srv_arm_accept_(srv))
				net_emit_srv_error(srv->io, srv->id, UI_NET_E_LISTEN,
					"re-arm accept failed");
		} else {
			srv_maybe_stopped_(srv);
		}
		return;
	}
	{
		BOOL nodelay = TRUE;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay,
			sizeof(nodelay));
	}
	if (CreateIoCompletionPort((HANDLE)s, srv->io->iocp, 0, 0) == NULL) {
		closesocket(s);
		free(a);
		if (!srv_arm_accept_(srv))
			net_emit_srv_error(srv->io, srv->id, UI_NET_E_LISTEN,
				"re-arm accept failed");
		return;
	}
	c = (NetSrvConn *)calloc(1, sizeof(NetSrvConn));
	if (!c) {
		closesocket(s);
		free(a);
		if (!srv_arm_accept_(srv))
			net_emit_srv_error(srv->io, srv->id, UI_NET_E_LISTEN,
				"re-arm accept failed");
		return;
	}
	c->srv = srv;
	c->s = s;
	c->id = ++srv->connSeq;
	c->refs = 1;
	c->state = srv->tls ? NET_SRV_C_TLS : NET_SRV_C_HTTP;
	c->born = Net_NowMs();
	c->lastRecv = c->born;
	srv_peer_(remote, c->peer, (int)sizeof(c->peer));
	c->next = srv->conns;
	srv->conns = c;
	srv->connCount++;
	a->sock = INVALID_SOCKET;
	free(a);

	if (!srv_arm_accept_(srv))
		net_emit_srv_error(srv->io, srv->id, UI_NET_E_LISTEN,
			"re-arm accept failed");

	srv_conn_addref_(c);
	if (srv->tls) net_tls_server_start(c);
	srv_timer_arm_(c, NET_T_SRV_HANDSHAKE, srv->cfg.handshakeTimeoutMs);
	if (srv->cfg.idleTimeoutMs > 0)
		srv_timer_arm_(c, NET_T_SRV_IDLE, srv->cfg.idleTimeoutMs);
	srv_recv_(c);
	srv_conn_release_(c);
	(void)local; (void)llen; (void)rlen;
}

void net_srv_on_completion(NetIo *io, NetOp *op, DWORD bytes, DWORD err)
{
	(void)io;
	if (!op) return;
	if (op->kind == NET_OP_SRV_ACCEPT) {
		NetSrvAccept *a = (NetSrvAccept *)op;
		srv_on_accept_(a->srv, a, err);
		return;
	}
	if (op->kind == NET_OP_SRV_RECV) {
		NetSrvConn *c = op->srvConn;
		if (!c) { free(op->buf); free(op); return; }
		srv_conn_addref_(c);
		c->rxPending = 0;
		if (err == 0 && bytes > 0) {
			io->bytesIn += bytes;
			NetBuf_Append(&c->in, op->buf, (int)bytes);
			c->lastRecv = Net_NowMs();
			if (c->srv->cfg.idleTimeoutMs > 0)
				srv_timer_arm_(c, NET_T_SRV_IDLE, c->srv->cfg.idleTimeoutMs);
			srv_on_recv_data_(c);
			if (!c->dead) srv_recv_(c);
		} else {
			srv_conn_emit_close_(c, 1006, "connection lost");
			srv_conn_close_(c);
		}
		free(op->buf);
		free(op);
		srv_conn_release_(c);
		srv_conn_release_(c);
		return;
	}
	if (op->kind == NET_OP_SRV_SEND) {
		NetSrvConn *c = op->srvConn;
		if (!c) { free(op); return; }
		srv_conn_addref_(c);
		c->sending = 0;
		if (err == 0) {
			io->bytesOut += bytes;
			if (bytes >= (DWORD)c->tx.len) {
				c->tx.len = 0;
				if (c->tx.p) c->tx.p[0] = '\0';
			} else {
				NetBuf_Consume(&c->tx, (int)bytes);
			}
			srv_tx_flush_(c);
			srv_maybe_close_after_send_(c);
		} else {
			srv_conn_emit_close_(c, 1006, "send failed");
			srv_conn_close_(c);
		}
		free(op);
		srv_conn_release_(c);
		srv_conn_release_(c);
		return;
	}
	free(op);
}

/* ── 命令 ────────────────────────────────────────────────────────────── */

void net_srv_send_cmd(NetIo *io, uint64_t srvId, uint64_t connId,
	int opcode, const void *data, int len)
{
	NetSrv *srv = net_srv_by_id(io, srvId);
	NetSrvConn *c;
	int op = opcode == 2 ? 2 : 1;

	if (!srv || srv->stopping || len < 0) return;
	if (len > srv->cfg.maxMessageSize) return;
	c = srv_conn_by_id_(srv, connId);
	if (!c || c->dead || !c->upgraded || c->state == NET_SRV_C_CLOSING)
		return;
	srv_conn_addref_(c);
	if (!srv_ws_send_frame_(c, op, data, len, 1)) {
		srv_conn_emit_close_(c, 1011, "send failed");
		srv_conn_close_(c);
	}
	srv_conn_release_(c);
}

void net_srv_broadcast_cmd(NetIo *io, uint64_t srvId, int opcode,
	const void *data, int len)
{
	NetSrv *srv = net_srv_by_id(io, srvId);
	NetSrvConn *c;
	int op = opcode == 2 ? 2 : 1;

	if (!srv || srv->stopping || len < 0) return;
	if (len > srv->cfg.maxMessageSize) return;
	c = srv->conns;
	while (c) {
		NetSrvConn *next = c->next;
		if (!c->dead && c->upgraded && c->state != NET_SRV_C_CLOSING) {
			srv_conn_addref_(c);
			if (!srv_ws_send_frame_(c, op, data, len, 1))
				srv_conn_close_(c);
			srv_conn_release_(c);
		}
		c = next;
	}
}

void net_srv_close_cmd(NetIo *io, uint64_t srvId, uint64_t connId,
	int code, const char *reason)
{
	NetSrv *srv = net_srv_by_id(io, srvId);
	NetSrvConn *c;

	if (!srv || srv->stopping) return;
	if (connId == 0) {
		c = srv->conns;
		while (c) {
			NetSrvConn *next = c->next;
			if (!c->dead) {
				srv_conn_addref_(c);
				srv_user_close_(c, code, reason);
				srv_conn_release_(c);
			}
			c = next;
		}
		return;
	}
	c = srv_conn_by_id_(srv, connId);
	if (c) {
		srv_conn_addref_(c);
		srv_user_close_(c, code, reason);
		srv_conn_release_(c);
	}
}
