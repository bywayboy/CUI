/*
 * net_http.c - HTTP/1.1 客户端 (0CRT)
 *
 * URL 解析 / 请求序列化 / 增量响应解析 / keep-alive 池 / 重定向 / 代理隧道。
 * 传输层不解析 JSON, body 原样交给应用。
 */
#include "net/net_internal.h"
#include "log.h"

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
int sprintf(char *buf, const char *fmt, ...);

#define HTTP_P_STATUS   0
#define HTTP_P_HEADERS  1
#define HTTP_P_BODY     2
#define HTTP_P_CHUNK_SZ 3
#define HTTP_P_CHUNK    4
#define HTTP_P_TRAILER  5

/* ── URL ────────────────────────────────────────────────────────────── */

int net_parse_url(const char *url, char *host, int hostCap, int *port,
	int *https, char *path, int pathCap)
{
	const char *p = url;
	const char *authEnd;
	const char *hstart;
	int isTls;

	if (!url) return 0;
	if (Net_StrCaseCmpN(p, "https://", 8) == 0) { isTls = 1; p += 8; }
	else if (Net_StrCaseCmpN(p, "http://", 7) == 0) { isTls = 0; p += 7; }
	else if (Net_StrCaseCmpN(p, "wss://", 6) == 0) { isTls = 1; p += 6; }
	else if (Net_StrCaseCmpN(p, "ws://", 5) == 0) { isTls = 0; p += 5; }
	else return 0;

	hstart = p;
	while (*p && *p != '/' && *p != '?' && *p != '#') p++;
	authEnd = p;

	/* userinfo@ 去掉 */
	{
		const char *q, *at = NULL;
		for (q = hstart; q < authEnd; q++) if (*q == '@') at = q;
		if (at) hstart = at + 1;
	}

	*port = isTls ? 443 : 80;
	if (*hstart == '[') {
		const char *e;
		int n;
		hstart++;
		e = hstart;
		while (e < authEnd && *e != ']') e++;
		if (e >= authEnd) return 0;
		n = (int)(e - hstart);
		if (n <= 0 || n >= hostCap) return 0;
		memcpy(host, hstart, (size_t)n);
		host[n] = '\0';
		if (e + 1 < authEnd && e[1] == ':') {
			int v = 0;
			const char *s = e + 2;
			while (s < authEnd && *s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
			if (v > 0) *port = v;
		}
	} else {
		const char *e = hstart;
		int n;
		while (e < authEnd && *e != ':') e++;
		n = (int)(e - hstart);
		if (n <= 0 || n >= hostCap) return 0;
		memcpy(host, hstart, (size_t)n);
		host[n] = '\0';
		if (e < authEnd && *e == ':') {
			int v = 0;
			const char *s = e + 1;
			while (s < authEnd && *s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
			if (v > 0) *port = v;
		}
	}

	if (*p == '/') {
		int n = 0;
		while (p[n] && p[n] != '#') n++;
		if (n >= pathCap) n = pathCap - 1;
		memcpy(path, p, (size_t)n);
		path[n] = '\0';
	} else {
		path[0] = '/';
		if (*p && *p != '#') {
			int n = 0;
			const char *s = p;
			while (s[n] && s[n] != '#') n++;
			if (n + 1 >= pathCap) n = pathCap - 2;
			memcpy(path + 1, s, (size_t)n);
			path[1 + n] = '\0';
		} else {
			path[1] = '\0';
		}
	}
	*https = isTls;
	return 1;
}

/* ── 代理 ───────────────────────────────────────────────────────────── */

/* 返回 1 并输出 host/port/auth("user:pass" 或空) */
static int proxy_parse_(UiNet *net, char *host, int cap, int *port,
	char *auth, int acap)
{
	const char *p;
	const char *slash, *at;
	int v = 0;

	if (!net || !net->proxy || !net->proxy[0]) return 0;
	p = net->proxy;
	if (Net_StrCaseCmpN(p, "http://", 7) == 0) p += 7;
	slash = strchr(p, '/');
	at = strchr(p, '@');
	if (at && (!slash || at < slash)) {
		int n = (int)(at - p);
		if (n >= acap) n = acap - 1;
		memcpy(auth, p, (size_t)n);
		auth[n] = '\0';
		p = at + 1;
	} else {
		auth[0] = '\0';
	}
	{
		const char *e = p;
		int n;
		while (*e && *e != ':' && *e != '/') e++;
		n = (int)(e - p);
		if (n <= 0 || n >= cap) return 0;
		memcpy(host, p, (size_t)n);
		host[n] = '\0';
		if (*e == ':') {
			const char *s = e + 1;
			while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
		}
	}
	*port = v > 0 ? v : 8080;
	return 1;
}

int net_http_need_tunnel(NetConn *conn)
{
	return conn && conn->kind == NET_K_HTTP && conn->http &&
		conn->http->viaProxy && conn->http->https;
}

static void proxy_authorization_(UiNet *net, NetBuf *out)
{
	char ph[256], puser[128] = "";
	int pport = 0;
	char raw[256];
	int n;

	if (!proxy_parse_(net, ph, sizeof(ph), &pport, puser, sizeof(puser)))
		return;
	if (!puser[0]) return;
	n = sprintf(raw, "%s", puser);
	{
		char enc[384];
		int len = Net_Base64((const BYTE *)raw, n, enc, sizeof(enc));
		if (len > 0) {
			NetBuf_AppendStr(out, "Proxy-Authorization: Basic ");
			NetBuf_AppendStr(out, enc);
			NetBuf_AppendStr(out, "\r\n");
		}
	}
}

/* ── 结果与回收 ─────────────────────────────────────────────────────── */

static void http_reset_(NetHttpConn *h)
{
	if (!h) return;
	if (h->method) free(h->method);
	if (h->extraHeaders) free(h->extraHeaders);
	if (h->body) free(h->body);
	if (h->contentType) free(h->contentType);
	if (h->hdrs) free(h->hdrs);
	NetBuf_Free(&h->request);
	NetBuf_Free(&h->hdrStore);
	NetBuf_Free(&h->body2);
	memset(h, 0, sizeof(*h));
	NetBuf_Init(&h->request);
	NetBuf_Init(&h->hdrStore);
	NetBuf_Init(&h->body2);
	h->contentLen = -1;
}

static void http_pool_clear_(NetHttpConn *h)
{
	if (!h) return;
	if (h->method) { free(h->method); h->method = NULL; }
	if (h->extraHeaders) { free(h->extraHeaders); h->extraHeaders = NULL; }
	if (h->body) { free(h->body); h->body = NULL; }
	if (h->contentType) { free(h->contentType); h->contentType = NULL; }
	if (h->hdrs) { free(h->hdrs); h->hdrs = NULL; }
	NetBuf_Clear(&h->request);
	NetBuf_Clear(&h->hdrStore);
	NetBuf_Clear(&h->body2);
	h->hdrCount = 0;
	h->hdrCap = 0;
	h->bodyLen = 0;
	h->emitted = 0;
	h->status = 0;
	h->pstate = HTTP_P_STATUS;
	h->contentLen = -1;
	h->chunked = 0;
	h->closeDelim = 0;
	h->keepAlive = 1;
	h->id = 0;
}

void net_http_free(NetConn *conn)
{
	if (!conn) return;
	if (conn->http) {
		http_reset_(conn->http);
		free(conn->http);
		conn->http = NULL;
	}
}

void net_http_req_free(NetHttpReq *r)
{
	if (!r) return;
	if (r->method) free(r->method);
	if (r->url) free(r->url);
	if (r->contentType) free(r->contentType);
	if (r->extraHeaders) free(r->extraHeaders);
	if (r->body) free(r->body);
	free(r);
}

static void http_pool_put_(NetConn *conn)
{
	NetIo *io = conn->io;
	UiNet *net = io->owner;
	int same = 0;
	NetConn *c;
	NetHttpConn *h = conn->http;

	if (!h) { net_conn_close(conn); return; }
	if (io->idleCount >= net->cfg.maxIdleTotal) { net_conn_close(conn); return; }
	for (c = io->idle; c; c = c->idleNext) {
		if (c->http && c->http->https == h->https &&
			c->http->port == h->port &&
			Net_StrCaseEq(c->http->host, h->host))
			same++;
	}
	if (same >= net->cfg.maxIdlePerHost) { net_conn_close(conn); return; }

	NetBuf_Clear(&conn->in);
	NetBuf_Clear(&conn->plain);
	NetBuf_Clear(&conn->out);
	NetBuf_Clear(&conn->tx);
	conn->sending = 0;
	conn->rxPending = 0;
	conn->state = NET_S_IDLE;
	conn->idleNext = io->idle;
	io->idle = conn;
	conn->inIdle = 1;
	io->idleCount++;
	net_timer_arm(conn, NET_T_IDLE, net->cfg.idleTimeoutMs);
}

NetConn *net_http_pool_get(NetIo *io, NetHttpReq *req)
{
	char host[NET_HOST_MAX + 1];
	char path[NET_URL_MAX];
	int port = 0, https = 0;
	NetConn *c, **pp;

	if (!io || !req || !req->url) return NULL;
	if (!net_parse_url(req->url, host, sizeof(host), &port, &https, path, sizeof(path)))
		return NULL;
	for (pp = &io->idle; *pp; pp = &(*pp)->idleNext) {
		c = *pp;
		if (!c->http || c->dead) continue;
		if (c->http->https != https) continue;
		if (c->http->port != port) continue;
		if (!Net_StrCaseEq(c->http->host, host)) continue;
		*pp = c->idleNext;
		c->idleNext = NULL;
		c->inIdle = 0;
		if (io->idleCount > 0) io->idleCount--;
		net_timer_cancel(c, NET_T_IDLE);
		if (io->owner->cfg.trace)
			Log_Write("[http] reuse conn host=%s:%d", c->http->host, c->http->port);
		return c;
	}
	return NULL;
}

/* ── 请求构造 ───────────────────────────────────────────────────────── */

static void http_build_(NetConn *conn)
{
	NetHttpConn *h = conn->http;
	NetBuf *b = &h->request;
	char tmp[64];

	NetBuf_Clear(b);
	h->isHead = (h->method && Net_StrCaseEq(h->method, "HEAD"));
	if (h->viaProxy && !h->https) {
		NetBuf_AppendFmt(b, "%s http://%s", h->method ? h->method : "GET", h->host);
		if (!((h->https && h->port == 443) || (!h->https && h->port == 80)))
			NetBuf_AppendFmt(b, ":%d", h->port);
		NetBuf_AppendStr(b, h->path);
		NetBuf_AppendStr(b, " HTTP/1.1\r\n");
	} else {
		NetBuf_AppendFmt(b, "%s %s HTTP/1.1\r\n", h->method ? h->method : "GET",
			h->path);
	}

	NetBuf_AppendStr(b, "Host: ");
	NetBuf_AppendStr(b, h->host);
	if (!((h->https && h->port == 443) || (!h->https && h->port == 80)))
		NetBuf_AppendFmt(b, ":%d", h->port);
	NetBuf_AppendStr(b, "\r\n");

	NetBuf_AppendStr(b, "User-Agent: CUI-Net/1.0\r\n");
	NetBuf_AppendStr(b, "Accept: */*\r\n");
	NetBuf_AppendStr(b, "Accept-Encoding: identity\r\n");
	NetBuf_AppendStr(b, "Connection: keep-alive\r\n");

	if (h->bodyLen > 0) {
		NetBuf_AppendStr(b, "Content-Type: ");
		NetBuf_AppendStr(b, h->contentType ? h->contentType : "application/octet-stream");
		NetBuf_AppendStr(b, "\r\n");
		sprintf(tmp, "Content-Length: %d\r\n", h->bodyLen);
		NetBuf_AppendStr(b, tmp);
	}
	if (h->extraHeaders && h->extraHeaders[0]) {
		NetBuf_AppendStr(b, h->extraHeaders);
		{
			int n = (int)strlen(h->extraHeaders);
			if (n < 2 || h->extraHeaders[n - 1] != '\n')
				NetBuf_AppendStr(b, "\r\n");
		}
	}
	if (h->viaProxy && !h->https)
		proxy_authorization_(conn->io->owner, b);
	NetBuf_AppendStr(b, "\r\n");
	if (h->bodyLen > 0)
		NetBuf_Append(b, h->body, h->bodyLen);
}

/* ── 响应头解析 ─────────────────────────────────────────────────────── */

static char *crlf_(char *p, char *end)
{
	while (p + 1 < end) {
		if (p[0] == '\r' && p[1] == '\n') return p;
		p++;
	}
	return NULL;
}

static int hdr_add_(NetHttpConn *h, char *name, char *value)
{
	if (h->hdrCount >= h->hdrCap) {
		int cap = h->hdrCap ? h->hdrCap * 2 : 16;
		UiHttpHeader *n = (UiHttpHeader *)realloc(h->hdrs,
			(size_t)cap * sizeof(UiHttpHeader));
		if (!n) return 0;
		h->hdrs = n;
		h->hdrCap = cap;
	}
	h->hdrs[h->hdrCount].name = name;
	h->hdrs[h->hdrCount].value = value;
	h->hdrCount++;
	return 1;
}

static int http_parse_headers_(NetHttpConn *h)
{
	char *p = h->hdrStore.p;
	char *end = p + h->hdrStore.len;
	char *e;
	char *line;
	int http11 = 0;

	if (!p || h->hdrStore.len < 12) return 0;
	e = crlf_(p, end);
	if (!e) return 0;
	if (e - p < 12) return 0;
	if (Net_StrCaseCmpN(p, "HTTP/", 5) != 0) return 0;
	http11 = (p[5] == '1' && p[6] == '.' && p[7] == '1');
	{
		char *sp = p;
		while (sp < e && *sp != ' ') sp++;
		if (sp + 4 > e) return 0;
		sp++;
		if (sp[0] < '0' || sp[0] > '9' || sp[1] < '0' || sp[1] > '9' ||
			sp[2] < '0' || sp[2] > '9')
			return 0;
		h->status = (sp[0] - '0') * 100 + (sp[1] - '0') * 10 + (sp[2] - '0');
	}
	h->keepAlive = http11;
	*e = '\0';

	line = e + 2;
	while (line < end) {
		char *colon;
		e = crlf_(line, end);
		if (!e) { e = end; }
		if (e == line) break;
		*e = '\0';
		colon = line;
		while (colon < e && *colon != ':') colon++;
		if (colon < e) {
			char *v;
			*colon = '\0';
			v = colon + 1;
			while (v < e && (*v == ' ' || *v == '\t')) v++;
			hdr_add_(h, line, v);

			if (Net_StrCaseEq(line, "content-length")) {
				int n = 0;
				const char *s = v;
				while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
				h->contentLen = n;
			} else if (Net_StrCaseEq(line, "transfer-encoding")) {
				if (Net_StrCaseStr(v, "chunked")) h->chunked = 1;
			} else if (Net_StrCaseEq(line, "connection")) {
				if (Net_StrCaseStr(v, "close")) h->keepAlive = 0;
				else if (Net_StrCaseStr(v, "keep-alive")) h->keepAlive = 1;
			}
		}
		line = e + 2;
	}
	return 1;
}

static const char *http_hdr_(NetHttpConn *h, const char *name)
{
	int i;
	for (i = 0; i < h->hdrCount; i++)
		if (Net_StrCaseEq(h->hdrs[i].name, name)) return h->hdrs[i].value;
	return NULL;
}

/* ── 完成 / 重定向 ──────────────────────────────────────────────────── */

static void http_finish_(NetConn *conn, int err);

static int http_absolute_(NetHttpConn *h, const char *location, char *out, int cap)
{
	char base[NET_URL_MAX];
	int n;

	if (Net_StrCaseCmpN(location, "http://", 7) == 0 ||
		Net_StrCaseCmpN(location, "https://", 8) == 0 ||
		Net_StrCaseCmpN(location, "ws://", 5) == 0 ||
		Net_StrCaseCmpN(location, "wss://", 6) == 0) {
		Net_StrCopy(out, cap, location);
		return 1;
	}
	if (location[0] == '/') {
		n = sprintf(base, "%s://%s", h->https ? "https" : "http", h->host);
		if (!((h->https && h->port == 443) || (!h->https && h->port == 80)))
			n = sprintf(base + n, ":%d", h->port);
		(void)n;
		Net_StrCopy(out, cap, base);
		Net_StrCopy(out + (int)strlen(out), cap - (int)strlen(out), location);
		return 1;
	}
	/* 相对路径: 去掉最后一段 */
	{
		char dir[NET_URL_MAX];
		int i, last = -1;
		Net_StrCopy(dir, sizeof(dir), h->path);
		for (i = 0; dir[i]; i++) if (dir[i] == '/') last = i;
		if (last >= 0) dir[last + 1] = '\0';
		else Net_StrCopy(dir, sizeof(dir), "/");
		n = sprintf(base, "%s://%s", h->https ? "https" : "http", h->host);
		if (!((h->https && h->port == 443) || (!h->https && h->port == 80)))
			n = sprintf(base + n, ":%d", h->port);
		(void)n;
		Net_StrCopy(out, cap, base);
		Net_StrCopy(out + (int)strlen(out), cap - (int)strlen(out), dir);
		Net_StrCopy(out + (int)strlen(out), cap - (int)strlen(out), location);
	}
	return 1;
}

static void http_redirect_(NetConn *conn, const char *location)
{
	NetHttpConn *h = conn->http;
	UiNet *net = conn->io->owner;
	NetHttpReq *r;
	NetConn *nc;
	char url[NET_URL_MAX];
	int status = h->status;

	if (!location || !http_absolute_(h, location, url, sizeof(url))) {
		http_finish_(conn, UI_NET_E_REDIRECT);
		return;
	}
	if (h->redirects >= net->cfg.maxRedirects) {
		http_finish_(conn, UI_NET_E_REDIRECT);
		return;
	}

	r = (NetHttpReq *)calloc(1, sizeof(NetHttpReq));
	if (!r) { http_finish_(conn, UI_NET_E_OOM); return; }
	r->url = (char *)malloc(strlen(url) + 1);
	if (!r->url) { free(r); http_finish_(conn, UI_NET_E_OOM); return; }
	Net_StrCopy(r->url, (int)strlen(url) + 1, url);
	if (status == 303) {
		r->method = NULL;              /* GET */
	} else if (h->method) {
		r->method = (char *)malloc(strlen(h->method) + 1);
		if (r->method) Net_StrCopy(r->method, (int)strlen(h->method) + 1, h->method);
	}
	if (status != 303 && h->bodyLen > 0 && h->body) {
		r->body = (char *)malloc((size_t)h->bodyLen);
		if (r->body) { memcpy(r->body, h->body, (size_t)h->bodyLen); r->bodyLen = h->bodyLen; }
	}
	if (h->contentType) {
		r->contentType = (char *)malloc(strlen(h->contentType) + 1);
		if (r->contentType) Net_StrCopy(r->contentType, (int)strlen(h->contentType) + 1, h->contentType);
	}
	if (h->extraHeaders) {
		r->extraHeaders = (char *)malloc(strlen(h->extraHeaders) + 1);
		if (r->extraHeaders) Net_StrCopy(r->extraHeaders, (int)strlen(h->extraHeaders) + 1, h->extraHeaders);
	}
	r->timeoutMs = h->timeoutMs;
	r->redirects = h->redirects + 1;

	nc = net_conn_new(conn->io, NET_K_HTTP, conn->id);
	if (!nc) {
		net_http_req_free(r);
		http_finish_(conn, UI_NET_E_OOM);
		return;
	}
	net_conn_close(conn);
	net_http_start(nc, r);
}

static void http_finish_(NetConn *conn, int err)
{
	NetHttpConn *h = conn->http;
	NetHttpResult *res;

	if (!h || h->emitted) return;

	/* 读到 EOF 且是 close-delimited: 视为正常结束 */
	if (err == UI_NET_E_CLOSED && h->status > 0 && h->closeDelim)
		err = 0;

	net_timer_cancel(conn, NET_T_REQUEST);
	net_timer_cancel(conn, NET_T_HANDSHAKE);

	h->emitted = 1;
	if (conn->io->owner->cfg.trace)
		Log_Write("[http] finish id=%d status=%d err=%d ka=%d delim=%d body=%d",
			(int)h->id, h->status, err, h->keepAlive, h->closeDelim, h->body2.len);
	if (h->id != 0) {
		res = (NetHttpResult *)calloc(1, sizeof(NetHttpResult));
		if (res) {
			res->status = err ? 0 : h->status;
			res->error = err;
			if (!err) {
				res->hdrs = h->hdrs;
				res->hdrCount = h->hdrCount;
				res->hdrStore = h->hdrStore.p;
				res->body = h->body2.p;
				res->bodyLen = h->body2.len;
				h->hdrs = NULL;
				h->hdrCount = 0;
				h->hdrCap = 0;
				NetBuf_Init(&h->hdrStore);
				NetBuf_Init(&h->body2);
			}
			net_emit_http(conn->io, h->id, res);
		}
	}

	if (!err && h->keepAlive && !conn->dead && !h->closeDelim && h->id != 0) {
		http_pool_clear_(h);
		http_pool_put_(conn);
		return;
	}
	net_conn_close(conn);
}

void net_http_finish(NetConn *conn, int err)
{
	if (conn && conn->http) http_finish_(conn, err);
}

/* ── 解析 ───────────────────────────────────────────────────────────── */

static NetBuf *http_src_(NetConn *conn)
{
	return conn->tls ? &conn->plain : &conn->in;
}

static int chunk_size_(const char *p, int n)
{
	int v = 0, i = 0;
	while (i < n) {
		int c = (unsigned char)p[i];
		int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else break;
		v = v * 16 + d;
		if (v > (64 << 20)) return -1;
		i++;
	}
	return v;
}

void net_http_on_plain(NetConn *conn, const char *data, int len)
{
	NetHttpConn *h;
	NetBuf *src;
	int guard = 0;

	(void)data; (void)len;
	if (!conn || conn->dead) return;
	h = conn->http;
	if (!h) return;
	src = http_src_(conn);

	/* 代理 CONNECT 隧道响应: 只读状态行, 200 后转 TLS 握手 */
	if (conn->state == NET_S_PROXY) {
		char *he;
		if (src->len > NET_HDR_MAX) { net_conn_fail(conn, UI_NET_E_CONNECT); return; }
		he = Net_MemFind(src->p, src->len, "\r\n\r\n", 4);
		if (!he) return;                /* 等完整响应头 */
		{
			int n = (int)(he - src->p);
			int ok = (n >= 12 && Net_StrCaseCmpN(src->p, "HTTP/", 5) == 0 &&
				src->p[9] == '2');
			NetBuf_Consume(src, n + 4);
			if (!ok) { net_conn_fail(conn, UI_NET_E_CONNECT); return; }
		}
		/* 隧道已建立: 之后的字节就是 TLS 密文 */
		Net_StrCopy(conn->host, NET_HOST_MAX + 1, h->host);
		conn->https = 1;
		conn->state = NET_S_TLS;
		net_timer_arm(conn, NET_T_HANDSHAKE,
			conn->io->owner->cfg.connectTimeoutMs + 3000);
		if (!net_tls_start(conn)) { net_conn_fail(conn, UI_NET_E_TLS); return; }
		if (!conn->dead && conn->in.len > 0)
			net_tls_handshake_input(conn);
		return;
	}

	while (!conn->dead && !h->emitted && guard++ < 64) {
		if (h->pstate == HTTP_P_STATUS || h->pstate == HTTP_P_HEADERS) {
			char *he = Net_MemFind(src->p, src->len, "\r\n\r\n", 4);
			if (he) {
				int take = (int)(he - src->p) + 4;
				NetBuf_Append(&h->hdrStore, src->p, take);
				NetBuf_Consume(src, take);
			} else {
				if (src->len <= 0) break;
				if (h->hdrStore.len + src->len > NET_HDR_MAX) {
					net_conn_fail(conn, UI_NET_E_HTTP);
					return;
				}
				NetBuf_Append(&h->hdrStore, src->p, src->len);
				NetBuf_Consume(src, src->len);
				break;
			}
			if (!http_parse_headers_(h)) { net_conn_fail(conn, UI_NET_E_HTTP); return; }

			/* 1xx 跳过 (101 只在 WS 中合法) */
			if (h->status >= 100 && h->status < 200) {
				if (h->status == 101) { net_conn_fail(conn, UI_NET_E_HTTP); return; }
				NetBuf_Clear(&h->hdrStore);
				h->hdrCount = 0;
				h->status = 0;
				h->keepAlive = 1;
				h->chunked = 0;
				h->contentLen = -1;
				continue;
			}
			/* 重定向 */
			if (h->status == 301 || h->status == 302 || h->status == 303 ||
				h->status == 307 || h->status == 308) {
				const char *loc = http_hdr_(h, "location");
				if (loc) { http_redirect_(conn, loc); return; }
			}

			h->noBody = h->isHead || h->status == 204 || h->status == 304;
			h->closeDelim = 0;
			if (h->noBody) {
				h->pstate = HTTP_P_STATUS;    /* 直接完成 */
				http_finish_(conn, 0);
				return;
			}
			if (h->chunked) {
				h->pstate = HTTP_P_CHUNK_SZ;
			} else if (h->contentLen >= 0) {
				h->pstate = HTTP_P_BODY;
				if (h->contentLen == 0) { http_finish_(conn, 0); return; }
			} else {
				h->pstate = HTTP_P_BODY;
				h->closeDelim = 1;
			}
			continue;
		}

		if (h->pstate == HTTP_P_BODY) {
			if (h->closeDelim) {
				if (src->len > 0) {
					NetBuf_Append(&h->body2, src->p, src->len);
					NetBuf_Consume(src, src->len);
				}
				break;                        /* 等 EOF */
			}
			{
				int remain = h->contentLen - h->body2.len;
				int take = src->len < remain ? src->len : remain;
				if (take > 0) {
					NetBuf_Append(&h->body2, src->p, take);
					NetBuf_Consume(src, take);
				}
				if (h->body2.len >= h->contentLen) { http_finish_(conn, 0); return; }
				if (take <= 0) break;
			}
			continue;
		}

		if (h->pstate == HTTP_P_CHUNK_SZ) {
			char *e = Net_MemFind(src->p, src->len, "\r\n", 2);
			int sz;
			if (!e) break;
			sz = chunk_size_(src->p, (int)(e - src->p));
			NetBuf_Consume(src, (int)(e - src->p) + 2);
			if (sz < 0) { net_conn_fail(conn, UI_NET_E_HTTP); return; }
			if (sz == 0) { h->pstate = HTTP_P_TRAILER; continue; }
			h->chunkRemain = sz;
			h->pstate = HTTP_P_CHUNK;
			continue;
		}

		if (h->pstate == HTTP_P_CHUNK) {
			if (src->len < h->chunkRemain + 2) break;
			NetBuf_Append(&h->body2, src->p, h->chunkRemain);
			NetBuf_Consume(src, h->chunkRemain + 2);
			h->pstate = HTTP_P_CHUNK_SZ;
			continue;
		}

		if (h->pstate == HTTP_P_TRAILER) {
			char *e = Net_MemFind(src->p, src->len, "\r\n", 2);
			if (!e) break;
			NetBuf_Consume(src, (int)(e - src->p) + 2);
			http_finish_(conn, 0);
			return;
		}
		break;
	}
}

/* ── 启动 ───────────────────────────────────────────────────────────── */

void net_http_start(NetConn *conn, NetHttpReq *req)
{
	NetHttpConn *h;
	UiNet *net;
	char ph[256];
	int pport = 0;
	char pauth[192];

	if (!conn || !req) { net_http_req_free(req); return; }
	net = conn->io->owner;
	if (conn->http) {
		/* 复用的 keep-alive 连接 */
		http_reset_(conn->http);
		h = conn->http;
	} else {
		h = (NetHttpConn *)calloc(1, sizeof(NetHttpConn));
		if (!h) {
			NetHttpResult *r = (NetHttpResult *)calloc(1, sizeof(NetHttpResult));
			if (r) { r->error = UI_NET_E_OOM; net_emit_http(conn->io, conn->id, r); }
			net_conn_close(conn);
			net_http_req_free(req);
			return;
		}
		NetBuf_Init(&h->request);
		NetBuf_Init(&h->hdrStore);
		NetBuf_Init(&h->body2);
		conn->http = h;
	}

	h->id = conn->id;
	h->method = req->method;
	h->extraHeaders = req->extraHeaders;
	h->body = req->body;
	h->bodyLen = req->bodyLen;
	h->contentType = req->contentType;
	h->timeoutMs = req->timeoutMs > 0 ? req->timeoutMs : net->cfg.requestTimeoutMs;
	h->redirects = req->redirects;
	h->contentLen = -1;
	h->keepAlive = 1;
	h->pstate = HTTP_P_STATUS;

	if (!net_parse_url(req->url, h->host, sizeof(h->host), &h->port, &h->https,
			h->path, sizeof(h->path))) {
		free(req->url);
		free(req);
		http_finish_(conn, UI_NET_E_PARAM);
		return;
	}
	free(req->url);
	free(req);

	h->viaProxy = proxy_parse_(net, ph, sizeof(ph), &pport, pauth, sizeof(pauth)) ? 1 : 0;
	(void)pauth;
	NetBuf_Clear(&conn->in);
	NetBuf_Clear(&conn->plain);

	if (h->viaProxy) {
		Net_StrCopy(conn->host, sizeof(conn->host), ph);
		conn->port = pport;
		conn->https = 0;                 /* 先与代理建立明文连接 */
	} else {
		Net_StrCopy(conn->host, sizeof(conn->host), h->host);
		conn->port = h->port;
		conn->https = h->https;
	}

	if (conn->s != INVALID_SOCKET && conn->state == NET_S_IDLE) {
		/* 池化连接: 重新武装接收后直接发请求 */
		conn->state = NET_S_TRANSPORT;
		net_tcp_recv(conn);
		net_http_on_event(conn, 1, 0);
	} else {
		net_conn_start(conn);
	}
}

void net_http_on_event(NetConn *conn, int connected, int err)
{
	NetHttpConn *h;

	if (!conn) return;
	h = conn->http;
	if (!connected || err) {
		net_conn_fail(conn, err ? err : UI_NET_E_CONNECT);
		return;
	}
	if (!h || conn->dead) return;
	if (conn->state == NET_S_PROXY) return;

	http_build_(conn);
	if (conn->io->owner->cfg.trace)
		Log_Write("[http] send id=%d %s %s:%d", (int)h->id,
			h->method ? h->method : "GET", h->host, h->port);
	if (!net_conn_write(conn, h->request.p, h->request.len)) {
		net_conn_fail(conn, UI_NET_E_CONNECT);
		return;
	}
	net_timer_arm(conn, NET_T_REQUEST, h->timeoutMs);
}

/* ── 代理隧道 ───────────────────────────────────────────────────────── */

void net_http_begin_tunnel(NetConn *conn)
{
	NetHttpConn *h = conn ? conn->http : NULL;
	NetBuf *b;
	char tmp[64];

	if (!h || conn->dead) return;
	b = &h->request;
	NetBuf_Clear(b);
	NetBuf_AppendStr(b, "CONNECT ");
	NetBuf_AppendStr(b, h->host);
	sprintf(tmp, ":%d", h->port);
	NetBuf_AppendStr(b, tmp);
	NetBuf_AppendStr(b, " HTTP/1.1\r\nHost: ");
	NetBuf_AppendStr(b, h->host);
	NetBuf_AppendStr(b, tmp);
	NetBuf_AppendStr(b, "\r\n");
	proxy_authorization_(conn->io->owner, b);
	NetBuf_AppendStr(b, "\r\n");

	conn->state = NET_S_PROXY;
	net_timer_cancel(conn, NET_T_CONNECT);
	net_timer_arm(conn, NET_T_HANDSHAKE,
		conn->io->owner->cfg.connectTimeoutMs + 3000);
	if (!net_conn_write(conn, b->p, b->len))
		net_conn_fail(conn, UI_NET_E_CONNECT);
}
