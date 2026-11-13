/*
 * net_dns.c - 自研 DNS 解析器 (0CRT)
 *
 * UDP 组包 + IOCP 收发, 不阻塞 IO 线程; 截断(TC)走 TCP 重查; 结果带 TTL 缓存。
 * 服务器来自 GetAdaptersAddresses, 先查 hosts 文件。
 */
#include "net/net_internal.h"
#include "log.h"

#include <iphlpapi.h>

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

#define DNS_MAX_MSG   4096
#define DNS_UDP_WAIT  800
#define DNS_TCP_WAIT  3000
#define DNS_CACHE_MAX 64

typedef struct NetDnsCache {
	struct NetDnsCache *next;
	char name[NET_HOST_MAX + 1];
	SOCKADDR_STORAGE addrs[NET_MAX_ADDRS];
	int count;
	uint64_t expires;
} NetDnsCache;

/* ── 工具 ───────────────────────────────────────────────────────────── */

static void lower_copy_(char *dst, int cap, const char *src)
{
	int i = 0;
	if (!dst || cap <= 0) return;
	if (!src) { dst[0] = '\0'; return; }
	for (; src[i] && i < cap - 1; i++) {
		char c = src[i];
		if (c >= 'A' && c <= 'Z') c += 32;
		dst[i] = c;
	}
	dst[i] = '\0';
}

static uint16_t rd16_(const BYTE *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32_(const BYTE *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		((uint32_t)p[2] << 8) | p[3];
}

static int add_addr_(SOCKADDR_STORAGE *out, int cap, int *count,
	const SOCKADDR_STORAGE *a)
{
	int i;
	if (*count >= cap) return 0;
	for (i = 0; i < *count; i++) {
		if (out[i].ss_family != a->ss_family) continue;
		if (a->ss_family == AF_INET &&
			((struct sockaddr_in *)&out[i])->sin_addr.s_addr ==
			((const struct sockaddr_in *)a)->sin_addr.s_addr)
			return 0;
		if (a->ss_family == AF_INET6 &&
			memcmp(&((struct sockaddr_in6 *)&out[i])->sin6_addr,
				&((const struct sockaddr_in6 *)a)->sin6_addr, 16) == 0)
			return 0;
	}
	out[*count] = *a;
	(*count)++;
	return 1;
}

/* ── hosts 文件 ─────────────────────────────────────────────────────── */

static int hosts_lookup_(const char *name, SOCKADDR_STORAGE *out, int cap)
{
	char path[MAX_PATH];
	char *buf;
	HANDLE h;
	DWORD got = 0, total = 0, pos = 0;
	int count = 0;

	if (Net_StrCaseEq(name, "localhost")) {
		struct sockaddr_in a4;
		struct sockaddr_in6 a6;
		memset(&a4, 0, sizeof(a4));
		a4.sin_family = AF_INET;
		a4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		memcpy(out, &a4, sizeof(a4));
		count = 1;
		memset(&a6, 0, sizeof(a6));
		a6.sin6_family = AF_INET6;
		a6.sin6_addr = in6addr_loopback;
		memcpy(&out[1], &a6, sizeof(a6));
		return 2;
	}

	GetWindowsDirectoryA(path, MAX_PATH);
	Net_StrCopy(path + (int)strlen(path), MAX_PATH - (int)strlen(path) - 1,
		"\\System32\\drivers\\etc\\hosts");
	h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	buf = (char *)malloc(65536);
	if (!buf) { CloseHandle(h); return 0; }
	ReadFile(h, buf, 65535, &got, NULL);
	CloseHandle(h);
	buf[got] = '\0';
	total = got;

	while (pos < total && count < cap) {
		char *line = buf + pos;
		char *end = strchr(line, '\n');
		char *p, *ip;
		int lineLen;

		if (end) { *end = '\0'; pos += (DWORD)(end - line) + 1; }
		else { pos = total; }
		lineLen = (int)strlen(line);
		while (lineLen > 0 && (line[lineLen - 1] == '\r' ||
				line[lineLen - 1] == ' ' || line[lineLen - 1] == '\t'))
			line[--lineLen] = '\0';

		p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (!*p || *p == '#') continue;
		{
			char *hash = strchr(p, '#');
			if (hash) *hash = '\0';
		}

		ip = p;
		while (*p && *p != ' ' && *p != '\t') p++;
		if (*p) { *p++ = '\0'; }
		while (*p == ' ' || *p == '\t') p++;

		/* 剩下的每个 token 都是主机名 */
		while (*p && count < cap) {
			char *hname = p;
			int matched = 0;
			while (*p && *p != ' ' && *p != '\t') p++;
			if (*p) *p++ = '\0';
			if (hname[0] && Net_StrCaseEq(hname, name)) {
				SOCKADDR_STORAGE sa;
				IN_ADDR v4;
				IN6_ADDR v6;
				memset(&sa, 0, sizeof(sa));
				if (InetPtonA(AF_INET, ip, &v4) == 1) {
					struct sockaddr_in *a = (struct sockaddr_in *)&sa;
					a->sin_family = AF_INET;
					a->sin_addr = v4;
					add_addr_(out, cap, &count, &sa);
					matched = 1;
				} else if (InetPtonA(AF_INET6, ip, &v6) == 1) {
					struct sockaddr_in6 *a = (struct sockaddr_in6 *)&sa;
					a->sin6_family = AF_INET6;
					a->sin6_addr = v6;
					add_addr_(out, cap, &count, &sa);
					matched = 1;
				}
				(void)matched;
				break;
			}
			while (*p == ' ' || *p == '\t') p++;
		}
	}
	free(buf);
	return count;
}

/* ── 服务器列表 ─────────────────────────────────────────────────────── */

static void servers_load_(NetIo *io)
{
	ULONG size = 16384;
	IP_ADAPTER_ADDRESSES *buf;
	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
		GAA_FLAG_SKIP_FRIENDLY_NAME;
	DWORD r;
	int count = 0;

	if (io->dnsLoaded && Net_NowMs() - io->dnsServersAt < 60000) return;

	buf = (IP_ADAPTER_ADDRESSES *)malloc(size);
	if (!buf) return;
	r = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, buf, &size);
	if (r == ERROR_BUFFER_OVERFLOW) {
		IP_ADAPTER_ADDRESSES *nb = (IP_ADAPTER_ADDRESSES *)realloc(buf, size);
		if (!nb) { free(buf); return; }
		buf = nb;
		r = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, buf, &size);
	}
	if (r == NO_ERROR) {
		IP_ADAPTER_ADDRESSES *a;
		for (a = buf; a && count < 8; a = a->Next) {
			IP_ADAPTER_DNS_SERVER_ADDRESS *d;
			if (a->OperStatus != IfOperStatusUp) continue;
			for (d = a->FirstDnsServerAddress; d && count < 8; d = d->Next) {
				struct sockaddr *sa = d->Address.lpSockaddr;
				int dup = 0, i;
				if (!sa) continue;
				if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
					continue;
				for (i = 0; i < count; i++) {
					if (io->dnsServers[i].ss_family != sa->sa_family) continue;
					if (sa->sa_family == AF_INET) {
						if (((struct sockaddr_in *)&io->dnsServers[i])->sin_addr.s_addr ==
							((struct sockaddr_in *)sa)->sin_addr.s_addr) { dup = 1; break; }
					} else {
						if (memcmp(&((struct sockaddr_in6 *)&io->dnsServers[i])->sin6_addr,
							&((struct sockaddr_in6 *)sa)->sin6_addr, 16) == 0) { dup = 1; break; }
					}
				}
				if (dup) continue;
				memset(&io->dnsServers[count], 0, sizeof(SOCKADDR_STORAGE));
				if (sa->sa_family == AF_INET) {
					memcpy(&io->dnsServers[count], sa, sizeof(struct sockaddr_in));
					((struct sockaddr_in *)&io->dnsServers[count])->sin_port = htons(53);
				} else {
					memcpy(&io->dnsServers[count], sa, sizeof(struct sockaddr_in6));
					((struct sockaddr_in6 *)&io->dnsServers[count])->sin6_port = htons(53);
				}
				count++;
			}
		}
	}
	free(buf);
	io->dnsServerCount = count;
	io->dnsLoaded = 1;
	io->dnsServersAt = Net_NowMs();
	if (count == 0)
		Log_Write("[dns] no dns servers found");
}

/* ── 缓存 ───────────────────────────────────────────────────────────── */

static int cache_get_(NetIo *io, const char *host, SOCKADDR_STORAGE *out, int cap);

int net_dns_cache_lookup(NetIo *io, const char *host, SOCKADDR_STORAGE *out, int cap)
{
	int n;

	if (!host || !out || cap <= 0) return 0;
	n = hosts_lookup_(host, out, cap);
	if (n > 0) return n;
	return io ? cache_get_(io, host, out, cap) : 0;
}

static int cache_get_(NetIo *io, const char *host, SOCKADDR_STORAGE *out, int cap)
{
	NetDnsCache *c;
	char low[NET_HOST_MAX + 1];
	uint64_t now = Net_NowMs();

	lower_copy_(low, sizeof(low), host);
	for (c = io->dnsCache; c; c = c->next) {
		if (c->expires <= now) continue;
		if (!Net_StrCaseEq(c->name, low)) continue;
		if (c->count > cap) {
			memcpy(out, c->addrs, (size_t)cap * sizeof(SOCKADDR_STORAGE));
			return cap;
		}
		memcpy(out, c->addrs, (size_t)c->count * sizeof(SOCKADDR_STORAGE));
		return c->count;
	}
	return 0;
}

static void cache_put_(NetIo *io, const char *host, const SOCKADDR_STORAGE *addrs,
	int count, int ttl)
{
	NetDnsCache *c, *tail = NULL, *prev = NULL;
	char low[NET_HOST_MAX + 1];
	int n = 0;

	lower_copy_(low, sizeof(low), host);
	if (ttl < 10) ttl = 10;
	if (ttl > 300) ttl = 300;

	for (c = io->dnsCache; c; prev = c, c = c->next) {
		n++;
		tail = c;
		if (Net_StrCaseEq(c->name, low)) {
			c->count = count > NET_MAX_ADDRS ? NET_MAX_ADDRS : count;
			memcpy(c->addrs, addrs, (size_t)c->count * sizeof(SOCKADDR_STORAGE));
			c->expires = Net_NowMs() + (uint64_t)ttl * 1000;
			return;
		}
	}
	if (n >= DNS_CACHE_MAX && prev) {          /* 淘汰最旧 */
		NetDnsCache **pp;
		for (pp = &io->dnsCache; (*pp) && (*pp)->next; pp = &(*pp)->next) { }
		if (*pp) { free(*pp); *pp = NULL; }
		(void)tail;
	}
	c = (NetDnsCache *)calloc(1, sizeof(NetDnsCache));
	if (!c) return;
	Net_StrCopy(c->name, sizeof(c->name), low);
	c->count = count > NET_MAX_ADDRS ? NET_MAX_ADDRS : count;
	memcpy(c->addrs, addrs, (size_t)c->count * sizeof(SOCKADDR_STORAGE));
	c->expires = Net_NowMs() + (uint64_t)ttl * 1000;
	c->next = io->dnsCache;
	io->dnsCache = c;
}

void net_dns_cache_free(NetIo *io)
{
	NetDnsCache *c;
	if (!io) return;
	c = io->dnsCache;
	while (c) { NetDnsCache *n = c->next; free(c); c = n; }
	io->dnsCache = NULL;
	io->dnsLoaded = 0;
	io->dnsServerCount = 0;
}

/* ── 报文 ───────────────────────────────────────────────────────────── */

static int build_query_(const char *host, int qtype, uint16_t id, NetBuf *out)
{
	BYTE hdr[12];
	const char *p = host;

	memset(hdr, 0, sizeof(hdr));
	hdr[0] = (BYTE)(id >> 8);
	hdr[1] = (BYTE)id;
	hdr[2] = 0x01;                 /* RD */
	hdr[5] = 0x01;                 /* QDCOUNT=1 */

	NetBuf_Clear(out);
	if (!NetBuf_Append(out, hdr, 12)) return 0;
	while (*p) {
		const char *dot = strchr(p, '.');
		int len = dot ? (int)(dot - p) : (int)strlen(p);
		if (len <= 0 || len > 63) return 0;
		if (!NetBuf_AppendChar(out, (char)len)) return 0;
		if (!NetBuf_Append(out, p, len)) return 0;
		if (!dot) break;
		p = dot + 1;
	}
	if (!NetBuf_AppendChar(out, 0)) return 0;
	{
		BYTE t[4];
		t[0] = (BYTE)(qtype >> 8);
		t[1] = (BYTE)qtype;
		t[2] = 0;
		t[3] = 1;                  /* IN */
		if (!NetBuf_Append(out, t, 4)) return 0;
	}
	return 1;
}

static const BYTE *skip_name_(const BYTE *p, const BYTE *end)
{
	int guard = 0;
	while (p < end && guard++ < 128) {
		BYTE c = *p;
		if (c == 0) return p + 1;
		if ((c & 0xC0) == 0xC0) {
			if (p + 1 >= end) return NULL;
			return p + 2;
		}
		if (c & 0xC0) return NULL;
		p += 1 + c;
	}
	return NULL;
}

/* 返回地址条数; tc/rcode 输出 */
static int parse_response_(const BYTE *p, int n, uint16_t id, int *tc,
	int *rcode, SOCKADDR_STORAGE *addrs, int cap, int *ttlOut)
{
	const BYTE *end = p + n;
	int qd, an, i, count = 0;
	uint32_t ttlMin = 0;
	int haveTtl = 0;

	if (n < 12) return -1;
	if (rd16_(p) != id) return -1;
	{
		uint16_t flags = rd16_(p + 2);
		if (!(flags & 0x8000)) return -1;        /* 不是响应 */
		*tc = (flags & 0x0200) ? 1 : 0;
		*rcode = flags & 0x000F;
	}
	qd = rd16_(p + 4);
	an = rd16_(p + 6);
	p += 12;

	for (i = 0; i < qd; i++) {
		p = skip_name_(p, end);
		if (!p || p + 4 > end) return -1;
		p += 4;
	}

	for (i = 0; i < an; i++) {
		uint16_t type, cls;
		uint32_t ttl, rdlen;
		const BYTE *rdata;

		p = skip_name_(p, end);
		if (!p || p + 10 > end) return -1;
		type = rd16_(p);
		cls = rd16_(p + 2);
		ttl = rd32_(p + 4);
		rdlen = rd16_(p + 8);
		p += 10;
		if (p + rdlen > end) return -1;
		rdata = p;

		if (cls == 1 && type == 1 && rdlen == 4) {
			SOCKADDR_STORAGE sa;
			struct sockaddr_in *a = (struct sockaddr_in *)&sa;
			memset(&sa, 0, sizeof(sa));
			a->sin_family = AF_INET;
			memcpy(&a->sin_addr, rdata, 4);
			add_addr_(addrs, cap, &count, &sa);
			if (!haveTtl || ttl < ttlMin) { ttlMin = ttl; haveTtl = 1; }
		} else if (cls == 1 && type == 28 && rdlen == 16) {
			SOCKADDR_STORAGE sa;
			struct sockaddr_in6 *a = (struct sockaddr_in6 *)&sa;
			memset(&sa, 0, sizeof(sa));
			a->sin6_family = AF_INET6;
			memcpy(&a->sin6_addr, rdata, 16);
			add_addr_(addrs, cap, &count, &sa);
			if (!haveTtl || ttl < ttlMin) { ttlMin = ttl; haveTtl = 1; }
		}
		p += rdlen;
	}
	*ttlOut = haveTtl ? (int)ttlMin : 60;
	return count;
}

/* ── 状态机 ─────────────────────────────────────────────────────────── */

static void dns_close_(NetConn *conn)
{
	conn->dnsGen++;              /* 作废此前所有 DNS 完成包 */
	net_tcp_shutdown(conn);
}

static void dns_fail_(NetConn *conn)
{
	dns_close_(conn);
	net_timer_cancel(conn, NET_T_DNS_RETRY);
	net_timer_cancel(conn, NET_T_DNS_TCP);
	net_conn_fail(conn, UI_NET_E_DNS);
}

static void dns_finish_(NetConn *conn, SOCKADDR_STORAGE *addrs, int count, int ttl)
{
	NetIo *io = conn->io;

	dns_close_(conn);
	net_timer_cancel(conn, NET_T_DNS_RETRY);
	net_timer_cancel(conn, NET_T_DNS_TCP);
	if (count <= 0) { net_conn_fail(conn, UI_NET_E_DNS); return; }
	if (count > NET_MAX_ADDRS) count = NET_MAX_ADDRS;
	memcpy(conn->addrs, addrs, (size_t)count * sizeof(SOCKADDR_STORAGE));
	conn->addrCount = count;
	conn->addrIdx = 0;
	cache_put_(io, conn->dnsName, addrs, count, ttl);
	{
		char a[64];
		if (addrs[0].ss_family == AF_INET) {
			BYTE *b = (BYTE *)&((struct sockaddr_in *)&addrs[0])->sin_addr;
			Log_Write("[dns] %s -> %d addrs, first %d.%d.%d.%d", conn->dnsName,
				count, b[0], b[1], b[2], b[3]);
		} else {
			Log_Write("[dns] %s -> %d addrs, first v6", conn->dnsName, count);
		}
		(void)a;
	}

	conn->state = NET_S_CONNECT;
	net_timer_arm(conn, NET_T_CONNECT, io->owner->cfg.connectTimeoutMs);
	net_tcp_try(conn);
}

static int dns_send_udp_(NetConn *conn)
{
	NetIo *io = conn->io;
	SOCKADDR_STORAGE *srv;
	SOCKET s;
	int alen;
	NetOp *op;
	WSABUF ws;
	DWORD flags = 0;
	int r;

	if (conn->dnsServerIdx < 0 || conn->dnsServerIdx >= io->dnsServerCount)
		return 0;
	srv = &io->dnsServers[conn->dnsServerIdx];
	alen = srv->ss_family == AF_INET ? (int)sizeof(struct sockaddr_in)
		: (int)sizeof(struct sockaddr_in6);

	s = WSASocketW(srv->ss_family, SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
		WSA_FLAG_OVERLAPPED);
	if (s == INVALID_SOCKET) return 0;
	if (CreateIoCompletionPort((HANDLE)s, io->iocp, 0, 0) == NULL) {
		closesocket(s);
		return 0;
	}
	if (connect(s, (const struct sockaddr *)srv, alen) != 0) {
		closesocket(s);
		return 0;
	}
	conn->s = s;
	if (send(s, conn->dnsQuery.p, conn->dnsQuery.len, 0) != conn->dnsQuery.len) {
		net_tcp_shutdown(conn);
		return 0;
	}

	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) { net_tcp_shutdown(conn); return 0; }
	op->buf = (char *)malloc(DNS_MAX_MSG);
	if (!op->buf) { free(op); net_tcp_shutdown(conn); return 0; }
	op->bufCap = DNS_MAX_MSG;
	op->kind = NET_OP_DNS_RECV;
	op->conn = conn;
	op->gen = conn->dnsGen;
	ws.buf = op->buf;
	ws.len = DNS_MAX_MSG;
	net_conn_addref(conn);
	r = WSARecv(conn->s, &ws, 1, NULL, &flags, &op->ov, NULL);
	if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
		free(op->buf);
		free(op);
		net_conn_release(conn);
		net_tcp_shutdown(conn);
		return 0;
	}
	net_timer_arm(conn, NET_T_DNS_RETRY, DNS_UDP_WAIT);
	return 1;
}

static void dns_next_(NetConn *conn)
{
	NetIo *io = conn->io;
	uint64_t now = Net_NowMs();
	uint64_t budget = (uint64_t)io->owner->cfg.dnsTimeoutMs;
	BYTE rnd[2];

	if (now - conn->dnsStart > budget) { dns_fail_(conn); return; }

	conn->dnsAttempts++;
	if (conn->dnsAttempts >= 2) {
		conn->dnsAttempts = 0;
		conn->dnsServerIdx++;
	}
	if (conn->dnsServerIdx >= io->dnsServerCount) {
		/* 一台都没答: A 查不到再试 AAAA */
		if (conn->dnsQType == 1) {
			conn->dnsQType = 28;
			conn->dnsServerIdx = 0;
			conn->dnsAttempts = 0;
			conn->dnsStart = Net_NowMs();
			Net_Random(rnd, 2);
			conn->dnsQid = (uint16_t)((rnd[0] << 8) | rnd[1]);
			if (!build_query_(conn->dnsName, conn->dnsQType, conn->dnsQid,
					&conn->dnsQuery)) {
				dns_fail_(conn);
				return;
			}
		} else {
			dns_fail_(conn);
			return;
		}
	}
	dns_close_(conn);
	if (!dns_send_udp_(conn)) { dns_fail_(conn); return; }
	conn->state = NET_S_DNS;
}

void net_dns_retry(NetConn *conn)
{
	if (!conn || conn->dead || conn->state != NET_S_DNS) return;
	dns_close_(conn);
	dns_next_(conn);
}

void net_dns_resolve(NetConn *conn)
{
	NetIo *io;
	BYTE rnd[2];

	if (!conn || conn->dead) return;
	io = conn->io;
	servers_load_(io);
	if (io->dnsServerCount <= 0) { dns_fail_(conn); return; }

	lower_copy_(conn->dnsName, sizeof(conn->dnsName), conn->host);
	conn->dnsQType = 1;                    /* A */
	conn->dnsServerIdx = 0;
	conn->dnsAttempts = 0;
	conn->dnsStart = Net_NowMs();
	Net_Random(rnd, 2);
	conn->dnsQid = (uint16_t)((rnd[0] << 8) | rnd[1]);

	if (!build_query_(conn->dnsName, conn->dnsQType, conn->dnsQid,
			&conn->dnsQuery)) {
		dns_fail_(conn);
		return;
	}
	if (!dns_send_udp_(conn)) { dns_fail_(conn); return; }
	conn->state = NET_S_DNS;
}

/* ── TCP 兜底 ───────────────────────────────────────────────────────── */

static void dns_tcp_recv_(NetConn *conn);

void net_dns_tcp_on_connected(NetConn *conn)
{
	NetOp *op;
	char *buf;
	int n;

	if (!conn || conn->dead || conn->state != NET_S_DNS_TCP) return;
	net_timer_cancel(conn, NET_T_DNS_TCP);
	net_timer_arm(conn, NET_T_DNS_TCP, DNS_TCP_WAIT);

	n = conn->dnsQuery.len;
	buf = (char *)malloc((size_t)n + 2);
	if (!buf) { dns_fail_(conn); return; }
	buf[0] = (char)((n >> 8) & 0xFF);
	buf[1] = (char)(n & 0xFF);
	memcpy(buf + 2, conn->dnsQuery.p, (size_t)n);

	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) { free(buf); dns_fail_(conn); return; }
	op->kind = NET_OP_DNS_TCP_SEND;
	op->conn = conn;
	op->gen = conn->dnsGen;
	op->buf = buf;
	op->bufCap = n + 2;
	net_conn_addref(conn);
	{
		WSABUF ws;
		ws.buf = buf;
		ws.len = (ULONG)(n + 2);
		if (WSASend(conn->s, &ws, 1, NULL, 0, &op->ov, NULL) == SOCKET_ERROR &&
			WSAGetLastError() != WSA_IO_PENDING) {
			free(buf);
			free(op);
			net_conn_release(conn);
			dns_fail_(conn);
		}
	}
}

static void dns_tcp_recv_(NetConn *conn)
{
	NetOp *op;
	WSABUF ws;
	DWORD flags = 0;
	int r;

	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) { dns_fail_(conn); return; }
	op->buf = (char *)malloc(DNS_MAX_MSG);
	if (!op->buf) { free(op); dns_fail_(conn); return; }
	op->bufCap = DNS_MAX_MSG;
	op->kind = NET_OP_DNS_TCP_RECV;
	op->conn = conn;
	op->gen = conn->dnsGen;
	ws.buf = op->buf;
	ws.len = DNS_MAX_MSG;
	net_conn_addref(conn);
	r = WSARecv(conn->s, &ws, 1, NULL, &flags, &op->ov, NULL);
	if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
		free(op->buf);
		free(op);
		net_conn_release(conn);
		dns_fail_(conn);
	}
}

void net_dns_tcp_after_send(NetConn *conn)
{
	if (!conn || conn->dead || conn->state != NET_S_DNS_TCP) return;
	NetBuf_Clear(&conn->in);
	dns_tcp_recv_(conn);
}

static void dns_tcp_begin_(NetConn *conn, const SOCKADDR_STORAGE *srv)
{
	int alen;

	net_tcp_shutdown(conn);
	NetBuf_Clear(&conn->in);
	conn->state = NET_S_DNS_TCP;
	net_timer_cancel(conn, NET_T_DNS_RETRY);
	net_timer_arm(conn, NET_T_DNS_TCP, DNS_TCP_WAIT);
	alen = srv->ss_family == AF_INET ? (int)sizeof(struct sockaddr_in)
		: (int)sizeof(struct sockaddr_in6);
	if (!net_tcp_connect(conn, srv, alen)) dns_fail_(conn);
}

/* ── 完成入口 ───────────────────────────────────────────────────────── */

void net_dns_on_recv(NetConn *conn, const char *buf, int n, int err)
{
	SOCKADDR_STORAGE addrs[NET_MAX_ADDRS];
	int tc = 0, rcode = 0, ttl = 0, count;

	if (!conn || conn->dead || conn->state != NET_S_DNS) return;
	if (err || !buf || n <= 0) { dns_next_(conn); return; }

	memset(addrs, 0, sizeof(addrs));
	count = parse_response_((const BYTE *)buf, n, conn->dnsQid, &tc, &rcode,
		addrs, NET_MAX_ADDRS, &ttl);
	if (count < 0) { dns_next_(conn); return; }
	if (tc) {
		if (conn->dnsServerIdx >= 0 && conn->dnsServerIdx < conn->io->dnsServerCount)
			dns_tcp_begin_(conn, &conn->io->dnsServers[conn->dnsServerIdx]);
		else
			dns_fail_(conn);
		return;
	}
	if (rcode == 3) { dns_fail_(conn); return; }   /* NXDOMAIN */
	if (count > 0) { dns_finish_(conn, addrs, count, ttl); return; }
	dns_next_(conn);                               /* NODATA: 换服务器/AAAA */
}

void net_dns_on_tcp_recv(NetConn *conn, const char *buf, int n, int err)
{
	int need;

	if (!conn || conn->dead || conn->state != NET_S_DNS_TCP) return;
	if (err || !buf || n <= 0) { dns_fail_(conn); return; }
	if (!NetBuf_Append(&conn->in, buf, n)) { dns_fail_(conn); return; }
	if (conn->in.len < 2) { dns_tcp_recv_(conn); return; }
	need = (((BYTE)conn->in.p[0] << 8) | (BYTE)conn->in.p[1]);
	if (need <= 0 || need > DNS_MAX_MSG) { dns_fail_(conn); return; }
	if (conn->in.len < need + 2) { dns_tcp_recv_(conn); return; }

	{
		SOCKADDR_STORAGE addrs[NET_MAX_ADDRS];
		int tc = 0, rcode = 0, ttl = 0, count;
		memset(addrs, 0, sizeof(addrs));
		count = parse_response_((const BYTE *)conn->in.p + 2, need,
			conn->dnsQid, &tc, &rcode, addrs, NET_MAX_ADDRS, &ttl);
		if (count > 0 && rcode == 0) dns_finish_(conn, addrs, count, ttl);
		else dns_fail_(conn);
	}
}
