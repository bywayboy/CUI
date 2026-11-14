/*
 * net_tcp.c - IOCP 连接/收发/完成派发 (0CRT)
 */
#include "net/net_internal.h"
#include "log.h"

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

/* 完成包 kind 见 net_internal.h (NET_OP_*) */

static int load_connectex_(NetIo *io)
{
	GUID g = WSAID_CONNECTEX;
	DWORD bytes = 0;
	SOCKET s;
	int ok;

	if (io->connectEx) return 1;
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return 0;
	ok = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &g, sizeof(g),
		&io->connectEx, sizeof(io->connectEx), &bytes, NULL, NULL) == 0;
	closesocket(s);
	return ok && io->connectEx != NULL;
}

int net_tcp_connect(NetConn *conn, const SOCKADDR_STORAGE *a, int alen)
{
	NetIo *io;
	SOCKET s;
	NetOp *op;
	BOOL nodelay = TRUE;
	int fam;

	if (!conn || conn->dead) return 0;
	if (!a || alen <= 0) return 0;
	io = conn->io;
	if (!load_connectex_(io)) return 0;

	fam = ((const struct sockaddr *)a)->sa_family;
	if (fam != AF_INET && fam != AF_INET6) return 0;

	s = WSASocketW(fam, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (s == INVALID_SOCKET) {
		Log_Write("[tcp] WSASocket failed %d", WSAGetLastError());
		return 0;
	}
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

	if (fam == AF_INET) {
		struct sockaddr_in local;
		memset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = INADDR_ANY;
		if (bind(s, (struct sockaddr *)&local, sizeof(local)) != 0) {
			closesocket(s);
			return 0;
		}
	} else {
		struct sockaddr_in6 local;
		memset(&local, 0, sizeof(local));
		local.sin6_family = AF_INET6;
		if (bind(s, (struct sockaddr *)&local, sizeof(local)) != 0) {
			closesocket(s);
			return 0;
		}
	}

	if (CreateIoCompletionPort((HANDLE)s, io->iocp, 0, 0) == NULL) {
		Log_Write("[tcp] bind iocp failed %d", GetLastError());
		closesocket(s);
		return 0;
	}

	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) {
		closesocket(s);
		return 0;
	}
	op->conn = conn;
	op->kind = NET_OP_CONNECT;

	conn->s = s;
	net_conn_addref(conn);
	if (!io->connectEx(s, (const struct sockaddr *)a, alen, NULL, 0, NULL,
			&op->ov)) {
		int e = WSAGetLastError();
		if (e != WSA_IO_PENDING) {
			Log_Write("[tcp] ConnectEx immediate fail %d", e);
			free(op);
			conn->s = INVALID_SOCKET;
			closesocket(s);
			net_conn_release(conn);
			return 0;
		}
	}
	return 1;
}

void net_tcp_try(NetConn *conn)
{
	int i;

	if (!conn) return;
	/* 目标端口统一写回地址 (DNS/hosts/缓存给出的地址端口为 0) */
	for (i = 0; i < conn->addrCount; i++) {
		if (((struct sockaddr *)&conn->addrs[i])->sa_family == AF_INET)
			((struct sockaddr_in *)&conn->addrs[i])->sin_port =
				htons((u_short)conn->port);
		else if (((struct sockaddr *)&conn->addrs[i])->sa_family == AF_INET6)
			((struct sockaddr_in6 *)&conn->addrs[i])->sin6_port =
				htons((u_short)conn->port);
	}
	while (conn->addrIdx < conn->addrCount) {
		int fam = ((struct sockaddr *)&conn->addrs[conn->addrIdx])->sa_family;
		int alen = fam == AF_INET ? (int)sizeof(struct sockaddr_in)
			: (int)sizeof(struct sockaddr_in6);
		if (net_tcp_connect(conn, &conn->addrs[conn->addrIdx], alen))
			return;
		conn->addrIdx++;
	}
	Log_Write("[tcp] all %d addrs failed", conn->addrCount);
	net_conn_fail(conn, UI_NET_E_CONNECT);
}

void net_tcp_recv(NetConn *conn)
{
	NetOp *op;
	WSABUF ws;
	DWORD flags = 0;
	int r;

	if (!conn || conn->dead || conn->rxStopped || conn->rxPending)
		return;
	if (conn->s == INVALID_SOCKET) return;

	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) return;
	op->buf = (char *)malloc(NET_RECV_SIZE);
	if (!op->buf) { free(op); return; }
	op->bufCap = NET_RECV_SIZE;
	op->kind = NET_OP_RECV;
	op->conn = conn;

	ws.buf = op->buf;
	ws.len = NET_RECV_SIZE;
	conn->rxPending = 1;
	net_conn_addref(conn);
	r = WSARecv(conn->s, &ws, 1, NULL, &flags, &op->ov, NULL);
	if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
		conn->rxPending = 0;
		free(op->buf);
		free(op);
		net_conn_release(conn);
		net_conn_fail(conn, UI_NET_E_CLOSED);
	}
}

static void buf_swap_(NetBuf *a, NetBuf *b)
{
	NetBuf t = *a;
	*a = *b;
	*b = t;
}

void net_tx_flush(NetConn *conn)
{
	NetOp *op;
	WSABUF ws;
	int r;

	if (!conn || conn->dead || conn->sending || conn->s == INVALID_SOCKET)
		return;
	if (conn->tx.len <= 0) {
		if (conn->out.len <= 0) return;
		buf_swap_(&conn->tx, &conn->out);
		NetBuf_Clear(&conn->out);
	}
	if (conn->tx.len <= 0) return;

	op = (NetOp *)calloc(1, sizeof(NetOp));
	if (!op) {
		net_conn_fail(conn, UI_NET_E_OOM);
		return;
	}
	op->kind = NET_OP_SEND;
	op->conn = conn;

	ws.buf = conn->tx.p;
	ws.len = (ULONG)conn->tx.len;
	conn->sending = 1;
	net_conn_addref(conn);
	r = WSASend(conn->s, &ws, 1, NULL, 0, &op->ov, NULL);
	if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
		conn->sending = 0;
		free(op);
		net_conn_release(conn);
		net_conn_fail(conn, UI_NET_E_CLOSED);
	}
}

int net_conn_write(NetConn *conn, const void *data, int len)
{
	int ok;

	if (!conn || conn->dead) return 0;
	if (len <= 0) return 1;
	if (conn->tls && conn->tls->established)
		ok = net_tls_encrypt(conn, data, len);
	else
		ok = NetBuf_Append(&conn->out, data, len);
	if (ok) net_tx_flush(conn);
	return ok;
}

/* ── 完成派发 ───────────────────────────────────────────────────────── */

void net_io_on_completion(NetIo *io, OVERLAPPED *ov, ULONG_PTR key,
	DWORD bytes, DWORD st)
{
	NetOp *op = (NetOp *)ov;
	NetConn *conn;
	(void)key;

	if (!op) return;                 /* 唤醒包 */
	if (op->kind >= NET_OP_SRV_ACCEPT) {
		net_srv_on_completion(io, op, bytes, st);
		return;
	}
	conn = op->conn;
	if (!conn) { free(op->buf); free(op); return; }

	switch (op->kind) {
	case NET_OP_RECV:
		conn->rxPending = 0;
		if (st == 0 && bytes > 0 && conn->state != NET_S_IDLE && !conn->inIdle) {
			NetBuf_Append(&conn->in, op->buf, (int)bytes);
			io->bytesIn += bytes;
			net_on_recv_data(conn);
			if (!conn->dead && conn->state != NET_S_IDLE && !conn->inIdle)
				net_tcp_recv(conn);
		} else if (conn->state != NET_S_IDLE && !conn->inIdle) {
			net_conn_fail(conn, UI_NET_E_CLOSED);
		}
		free(op->buf);
		free(op);
		net_conn_release(conn);
		break;

	case NET_OP_SEND:
		conn->sending = 0;
		if (st == 0) {
			io->bytesOut += bytes;
			conn->tx.len = 0;
			if (conn->tx.p) conn->tx.p[0] = '\0';
			net_tx_flush(conn);
		} else if (conn->state != NET_S_IDLE) {
			net_conn_fail(conn, UI_NET_E_CLOSED);
		}
		free(op);
		net_conn_release(conn);
		break;

	case NET_OP_CONNECT:
		if (st == 0) {
			DWORD one = 1;
			setsockopt(conn->s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT,
				(const char *)&one, sizeof(one));
			if (conn->state == NET_S_DNS_TCP)
				net_dns_tcp_on_connected(conn);
			else
				net_on_connected(conn);
		} else {
			net_tcp_shutdown(conn);
			Log_Write("[tcp] connect failed st=0x%x idx=%d/%d", (unsigned)st,
				conn->addrIdx, conn->addrCount);
			if (conn->state == NET_S_DNS_TCP) {
				net_conn_fail(conn, UI_NET_E_DNS);
			} else {
			conn->addrIdx++;
			/* 换下一个地址; 全部失败才报错 */
			if (conn->addrIdx < conn->addrCount) {
				int i;
				int ok = 0;
				for (i = conn->addrIdx; i < conn->addrCount; i++) {
					int fam = ((struct sockaddr *)&conn->addrs[i])->sa_family;
					int alen = fam == AF_INET ? (int)sizeof(struct sockaddr_in)
						: (int)sizeof(struct sockaddr_in6);
					conn->addrIdx = i;
					if (net_tcp_connect(conn, &conn->addrs[i], alen)) { ok = 1; break; }
				}
				if (!ok) net_conn_fail(conn, UI_NET_E_CONNECT);
			} else {
				net_conn_fail(conn, UI_NET_E_CONNECT);
			}
			}
		}
		free(op);
		net_conn_release(conn);
		break;

	case NET_OP_DNS_RECV:
		if (op->gen != conn->dnsGen) {
			free(op->buf);
			free(op);
			net_conn_release(conn);
			break;
		}
		if (st == 0 && bytes > 0)
			net_dns_on_recv(conn, op->buf, (int)bytes, 0);
		else
			net_dns_on_recv(conn, NULL, 0, 1);
		free(op->buf);
		free(op);
		net_conn_release(conn);
		break;

	case NET_OP_DNS_TCP_RECV:
		if (op->gen != conn->dnsGen) {
			free(op->buf);
			free(op);
			net_conn_release(conn);
			break;
		}
		if (st == 0 && bytes > 0)
			net_dns_on_tcp_recv(conn, op->buf, (int)bytes, 0);
		else
			net_dns_on_tcp_recv(conn, NULL, 0, 1);
		free(op->buf);
		free(op);
		net_conn_release(conn);
		break;

	case NET_OP_DNS_TCP_SEND:
		if (op->gen != conn->dnsGen) {
			free(op);
			net_conn_release(conn);
			break;
		}
		if (st != 0) {
			net_conn_fail(conn, UI_NET_E_DNS);
		} else {
			/* 继续收: DNS TCP 接收由 net_dns_on_tcp_recv 负责重新武装 */
			net_dns_tcp_after_send(conn);
		}
		free(op);
		net_conn_release(conn);
		break;

	default:
		free(op->buf);
		free(op);
		net_conn_release(conn);
		break;
	}
}
