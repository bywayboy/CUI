/*
 * net_ws.c - WebSocket 客户端 (0CRT)
 *
 * 握手复用 HTTP 语义; 帧编解码为 per-connection 状态; 控制帧优先; 心跳 + 退避重连。
 */
#include "net/net_internal.h"
#include "log.h"

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);

#define WS_GUID       "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_MAX_FRAME  (16 * 1024 * 1024)
#define WS_CLOSE_WAIT 2000

enum {
	WS_OP_CONT = 0,
	WS_OP_TEXT = 1,
	WS_OP_BIN = 2,
	WS_OP_CLOSE = 8,
	WS_OP_PING = 9,
	WS_OP_PONG = 10
};

static void ws_emit_close_(NetConn *conn, int code, const char *reason)
{
	NetWsConn *w = conn->ws;
	if (!w || w->emitted) return;
	w->emitted = 1;
	w->closeCode = code;
	Net_StrCopy(w->closeReason, sizeof(w->closeReason), reason ? reason : "");
	net_emit_ws_close(conn->io, w->id, code, w->closeReason);
}

static void ws_reset_(NetWsConn *w)
{
	NetBuf_Clear(&w->frag);
	w->fragOpcode = 0;
	w->handshakeSent = 0;
	w->upgraded = 0;
	w->closeSent = 0;
	w->closeRecv = 0;
	w->pingAt = 0;
	w->openedOnce = 0;
	w->emitted = 0;
	w->accept[0] = '\0';
}

/* ── 帧编码 ─────────────────────────────────────────────────────────── */

/* 追加一个完整帧到 out; masked=1 用于客户端, 0 用于服务端. */
int net_ws_build_frame(int masked, int opcode, const void *data, int len,
	int fin, NetBuf *out)
{
	BYTE mask[4];
	BYTE h[14];
	int hl = 2;
	int i;

	if (!out || len < 0 || len > WS_MAX_FRAME) return 0;
	h[0] = (BYTE)((fin ? 0x80 : 0) | (opcode & 0x0F));
	h[1] = masked ? 0x80 : 0x00;
	if (len < 126) {
		h[1] |= (BYTE)len;
	} else if (len < 65536) {
		h[1] |= 126;
		h[2] = (BYTE)((len >> 8) & 0xFF);
		h[3] = (BYTE)(len & 0xFF);
		hl = 4;
	} else {
		uint64_t l = (uint64_t)len;
		int k;
		h[1] |= 127;
		for (k = 0; k < 8; k++) h[2 + k] = (BYTE)((l >> (56 - k * 8)) & 0xFF);
		hl = 10;
	}
	if (masked) {
		Net_Random(mask, 4);
		memcpy(h + hl, mask, 4);
		hl += 4;
	}
	if (!NetBuf_Append(out, h, hl)) return 0;
	if (len > 0) {
		if (masked) {
			const BYTE *p = (const BYTE *)data;
			int off = out->len;
			if (!NetBuf_Reserve(out, len)) return 0;
			for (i = 0; i < len; i++)
				out->p[off + i] = (char)(p[i] ^ mask[i & 3]);
			out->len += len;
			out->p[out->len] = '\0';
		} else if (!NetBuf_Append(out, data, len)) {
			return 0;
		}
	}
	return 1;
}

/* 解析帧头, 不消费. 返回 1=完整, 0=需更多, -1=协议错误 (RSV). */
int net_ws_parse_header(const char *buf, int len, int *fin, int *opcode,
	int *masked, BYTE mask[4], uint64_t *plen, int *hlen)
{
	BYTE b0, b1;
	uint64_t l;
	int hl = 2;
	int m;

	if (!buf || len < 2) return 0;
	b0 = (BYTE)buf[0];
	b1 = (BYTE)buf[1];
	if (b0 & 0x70) return -1;              /* RSV1..3 必须为 0 */
	*fin = (b0 & 0x80) ? 1 : 0;
	*opcode = b0 & 0x0F;
	m = (b1 & 0x80) ? 1 : 0;
	l = b1 & 0x7F;
	if (l == 126) {
		if (len < 4) return 0;
		l = ((uint64_t)(BYTE)buf[2] << 8) | (BYTE)buf[3];
		hl = 4;
	} else if (l == 127) {
		int k;
		if (len < 10) return 0;
		l = 0;
		for (k = 0; k < 8; k++) l = (l << 8) | (BYTE)buf[2 + k];
		hl = 10;
	}
	if (m) {
		if (len < hl + 4) return 0;
		if (mask) memcpy(mask, buf + hl, 4);
		hl += 4;
	}
	*masked = m;
	*plen = l;
	*hlen = hl;
	return 1;
}

int net_ws_utf8_valid(const void *data, int len)
{
	const BYTE *s = (const BYTE *)data;
	int i = 0;

	while (i < len) {
		uint32_t cp;
		int n, k;
		BYTE c = s[i++];
		if (c < 0x80) continue;
		if ((c & 0xE0) == 0xC0) {
			n = 1;
			cp = c & 0x1F;
			if (cp < 2) return 0;
		} else if ((c & 0xF0) == 0xE0) {
			n = 2;
			cp = c & 0x0F;
		} else if ((c & 0xF8) == 0xF0) {
			n = 3;
			cp = c & 0x07;
			if (cp > 4) return 0;
		} else {
			return 0;
		}
		if (i + n > len) return 0;
		for (k = 0; k < n; k++) {
			BYTE cc = s[i++];
			if ((cc & 0xC0) != 0x80) return 0;
			cp = (cp << 6) | (cc & 0x3F);
		}
		if ((n == 1 && cp < 0x80) ||
		    (n == 2 && cp < 0x800) ||
		    (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) ||
		    (cp >= 0xD800 && cp <= 0xDFFF))
			return 0;
	}
	return 1;
}

static int ws_send_frame_(NetConn *conn, int opcode, const void *data, int len,
	int fin)
{
	NetBuf b;
	int ok;

	if (!conn || conn->dead) return 0;
	if (len < 0 || len > WS_MAX_FRAME) return 0;
	NetBuf_Init(&b);
	if (!net_ws_build_frame(1, opcode, data, len, fin, &b)) {
		NetBuf_Free(&b);
		return 0;
	}
	ok = net_conn_write(conn, b.p, b.len);
	NetBuf_Free(&b);
	return ok;
}

/* ── 握手 ───────────────────────────────────────────────────────────── */

static void ws_build_(NetConn *conn)
{
	NetWsConn *w = conn->ws;
	NetBuf *b = &w->request;
	BYTE rnd[16];
	char key[64];
	int klen;
	BYTE hash[20];
	char buf[128];

	NetBuf_Clear(b);
	Net_Random(rnd, 16);
	klen = Net_Base64(rnd, 16, key, sizeof(key));
	if (klen <= 0) return;
	Net_StrCopy(w->key, sizeof(w->key), key);

	Net_StrCopy(buf, sizeof(buf), key);
	Net_StrCopy(buf + (int)strlen(buf), (int)sizeof(buf) - (int)strlen(buf),
		WS_GUID);
	Net_Sha1(buf, (int)strlen(buf), hash);
	if (Net_Base64(hash, 20, w->accept, sizeof(w->accept)) <= 0)
		w->accept[0] = '\0';

	NetBuf_AppendStr(b, "GET ");
	NetBuf_AppendStr(b, w->path);
	NetBuf_AppendStr(b, " HTTP/1.1\r\nHost: ");
	NetBuf_AppendStr(b, w->host);
	if (!((w->https && w->port == 443) || (!w->https && w->port == 80)))
		NetBuf_AppendFmt(b, ":%d", w->port);
	NetBuf_AppendStr(b, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n");
	NetBuf_AppendStr(b, "Sec-WebSocket-Key: ");
	NetBuf_AppendStr(b, key);
	NetBuf_AppendStr(b, "\r\nSec-WebSocket-Version: 13\r\n");
	if (w->extraHeaders && w->extraHeaders[0]) {
		NetBuf_AppendStr(b, w->extraHeaders);
		{
			int n = (int)strlen(w->extraHeaders);
			if (n < 2 || w->extraHeaders[n - 1] != '\n')
				NetBuf_AppendStr(b, "\r\n");
		}
	}
	NetBuf_AppendStr(b, "\r\n");
}

/* 在 header 块 (不含结尾空行) 里查 name, 返回 value 起始/长度 */
static int ws_hdr_(const char *hdr, int len, const char *name,
	const char **val, int *vlen)
{
	int nl = (int)strlen(name);
	int pos = 0;

	while (pos < len) {
		int e = pos;
		int n;
		while (e + 1 < len && !(hdr[e] == '\r' && hdr[e + 1] == '\n')) e++;
		n = e - pos;
		if (n > nl && hdr[pos + nl] == ':' &&
			Net_StrCaseCmpN(hdr + pos, name, nl) == 0) {
			int v = pos + nl + 1;
			while (v < e && (hdr[v] == ' ' || hdr[v] == '\t')) v++;
			*val = hdr + v;
			*vlen = e - v;
			return 1;
		}
		pos = e + 2;
	}
	return 0;
}

static int ws_val_eq_(const char *v, int n, const char *want)
{
	int wl = (int)strlen(want);
	return n == wl && Net_StrCaseCmpN(v, want, wl) == 0;
}

static int ws_val_has_(const char *v, int n, const char *want)
{
	char tmp[256];
	int k = n < (int)sizeof(tmp) - 1 ? n : (int)sizeof(tmp) - 1;
	memcpy(tmp, v, (size_t)k);
	tmp[k] = '\0';
	return Net_StrCaseStr(tmp, want) != NULL;
}

static int ws_handshake_(NetConn *conn, const char *hdr, int len)
{
	NetWsConn *w = conn->ws;
	const char *v;
	int vl;

	if (len < 12 || Net_StrCaseCmpN(hdr, "HTTP/1.1 101", 12) != 0)
		return 0;
	if (!ws_hdr_(hdr, len, "Upgrade", &v, &vl) || !ws_val_eq_(v, vl, "websocket")) {
		Log_Write("[ws] handshake: bad Upgrade");
		return 0;
	}
	if (!ws_hdr_(hdr, len, "Connection", &v, &vl) || !ws_val_has_(v, vl, "upgrade")) {
		Log_Write("[ws] handshake: bad Connection");
		return 0;
	}
	if (!ws_hdr_(hdr, len, "Sec-WebSocket-Accept", &v, &vl)) {
		Log_Write("[ws] handshake: no Accept");
		return 0;
	}
	if (!ws_val_eq_(v, vl, w->accept)) {
		char got[80];
		int n = vl < 79 ? vl : 79;
		memcpy(got, v, (size_t)n);
		got[n] = '\0';
		Log_Write("[ws] handshake: accept mismatch got=%s want=%s", got, w->accept);
		return 0;
	}
	return 1;
}

/* ── 重连 / 心跳 / 关闭 ─────────────────────────────────────────────── */

static void ws_schedule_reconnect_(NetConn *conn)
{
	NetWsConn *w = conn->ws;
	UiWsOptions *o = &w->opt;
	int maxDelay = o->reconnectMaxMs > 0 ? o->reconnectMaxMs : 10000;
	int minDelay = o->reconnectMinMs > 0 ? o->reconnectMinMs : 500;
	int mult = o->reconnectBackoff >= 1 ? o->reconnectBackoff : 2;
	int pct = o->reconnectJitterPct;
	uint64_t stable = o->reconnectStableMs > 0 ? (uint64_t)o->reconnectStableMs : 30000;
	uint64_t now = Net_NowMs();
	int base;
	BYTE r;
	int spread;

	if (mult > 10) mult = 10;
	if (pct < 0) pct = 20;
	if (pct > 50) pct = 50;

	/* 退火: 上次连接稳定超过阈值, 下次从最小间隔重新开始 */
	if (w->connectedAt && now - w->connectedAt >= stable)
		w->reconnectDelay = 0;

	base = w->reconnectDelay > 0 ? w->reconnectDelay * mult : minDelay;
	if (base > maxDelay) base = maxDelay;
	w->reconnectDelay = base;

	spread = base * pct / 100;
	if (spread > 0) {
		unsigned range = (unsigned)(spread * 2 + 1);
		Net_Random(&r, 1);
		base = base - spread + (int)(r % range);
	}
	if (base < 1) base = 1;
	Log_Write("[ws] reconnect in %dms", base);

	net_timer_cancel(conn, NET_T_PING);
	net_timer_cancel(conn, NET_T_HANDSHAKE);
	net_timer_cancel(conn, NET_T_CLOSE);
	net_tcp_shutdown(conn);
	net_tls_free(conn);
	NetBuf_Clear(&conn->in);
	NetBuf_Clear(&conn->plain);
	NetBuf_Clear(&conn->out);
	NetBuf_Clear(&conn->tx);
	conn->sending = 0;
	conn->rxPending = 0;
	ws_reset_(w);
	conn->state = NET_S_IDLE;
	net_timer_arm(conn, NET_T_RECONNECT, base);
}

int net_ws_on_fail(NetConn *conn, int err)
{
	NetWsConn *w = conn ? conn->ws : NULL;
	if (!w) return 0;
	if (w->userClosed || !w->opt.autoReconnect) {
		ws_emit_close_(conn, 0, UiNet_StrError(err));
		return 0;
	}
	ws_schedule_reconnect_(conn);
	return 1;
}

void net_ws_reconnect_now(NetConn *conn)
{
	NetWsConn *w = conn ? conn->ws : NULL;
	if (!w) return;
	if (w->userClosed) {
		ws_emit_close_(conn, w->closeCode ? w->closeCode : 1000, "");
		net_conn_close(conn);
		return;
	}
	conn->state = NET_S_IDLE;
	net_conn_start(conn);
}

void net_ws_on_ping_timeout(NetConn *conn)
{
	NetWsConn *w = conn ? conn->ws : NULL;
	if (!w || conn->dead) return;
	if (w->opt.pingIntervalMs <= 0) return;
	if (w->pingAt > 0) {
		net_conn_fail(conn, UI_NET_E_TIMEOUT);
		return;
	}
	if (ws_send_frame_(conn, WS_OP_PING, NULL, 0, 1)) {
		w->pingAt = (int)(Net_NowMs() & 0x7FFFFFFF);
		net_timer_arm(conn, NET_T_PING, w->opt.pingIntervalMs);
	} else {
		net_conn_fail(conn, UI_NET_E_CLOSED);
	}
}

void net_ws_on_close_timeout(NetConn *conn)
{
	NetWsConn *w = conn ? conn->ws : NULL;
	if (!w) { if (conn) net_conn_close(conn); return; }
	ws_emit_close_(conn, w->closeCode ? w->closeCode : 1000, w->closeReason);
	net_conn_close(conn);
}

static void ws_handle_close_(NetConn *conn, const char *data, int len)
{
	NetWsConn *w = conn->ws;
	int code = 1000;

	if (!w) return;
	if (len >= 2) code = (((BYTE)data[0] << 8) | (BYTE)data[1]);
	w->closeRecv = 1;
	w->closeCode = code;
	if (!w->closeSent) {
		ws_send_frame_(conn, WS_OP_CLOSE, data, len >= 2 ? 2 : 0, 1);
		w->closeSent = 1;
	}
	net_timer_cancel(conn, NET_T_CLOSE);
	if (w->userClosed || !w->opt.autoReconnect) {
		ws_emit_close_(conn, code, "");
		net_conn_close(conn);
	} else {
		ws_schedule_reconnect_(conn);
	}
}

void net_ws_user_close(NetConn *conn, int code, const char *reason)
{
	NetWsConn *w = conn ? conn->ws : NULL;
	if (!w) { if (conn) net_conn_close(conn); return; }
	if (w->userClosed) return;
	w->userClosed = 1;
	w->closeCode = code;
	Net_StrCopy(w->closeReason, sizeof(w->closeReason), reason ? reason : "");
	net_timer_cancel(conn, NET_T_PING);
	net_timer_cancel(conn, NET_T_RECONNECT);

	if (conn->state != NET_S_TRANSPORT || !w->upgraded || conn->s == INVALID_SOCKET) {
		ws_emit_close_(conn, code, w->closeReason);
		net_conn_close(conn);
		return;
	}
	{
		char payload[126];
		int n = 2 + (int)strlen(w->closeReason);
		if (n > (int)sizeof(payload)) n = (int)sizeof(payload);
		payload[0] = (char)((code >> 8) & 0xFF);
		payload[1] = (char)(code & 0xFF);
		if (n > 2)
			memcpy(payload + 2, w->closeReason, (size_t)(n - 2));
		ws_send_frame_(conn, WS_OP_CLOSE, payload, n, 1);
		w->closeSent = 1;
	}
	net_timer_arm(conn, NET_T_CLOSE, WS_CLOSE_WAIT);
}

/* ── 帧解析 ─────────────────────────────────────────────────────────── */

static void ws_dispatch_payload_(NetConn *conn, int fin, int opcode,
	const char *payload, int plen)
{
	NetWsConn *w = conn->ws;
	if (!w) return;

	if (opcode == WS_OP_TEXT || opcode == WS_OP_BIN) {
		if (w->frag.len > 0) {          /* 上一条分片未结束 */
			net_conn_fail(conn, UI_NET_E_WS);
			return;
		}
		if (fin) {
			net_emit_ws_msg(conn->io, w->id, opcode, payload, plen);
		} else {
			w->fragOpcode = opcode;
			NetBuf_Append(&w->frag, payload, plen);
		}
		return;
	}
	if (opcode == WS_OP_CONT) {
		if (w->fragOpcode == 0 && w->frag.len == 0) {
			net_conn_fail(conn, UI_NET_E_WS);
			return;
		}
		if (w->frag.len + plen > WS_MAX_FRAME) {
			net_conn_fail(conn, UI_NET_E_WS);
			return;
		}
		NetBuf_Append(&w->frag, payload, plen);
		if (fin) {
			net_emit_ws_msg(conn->io, w->id, w->fragOpcode, w->frag.p, w->frag.len);
			NetBuf_Clear(&w->frag);
			w->fragOpcode = 0;
		}
	}
}

static void ws_parse_frames_(NetConn *conn)
{
	NetWsConn *w = conn->ws;
	NetBuf *src = conn->tls ? &conn->plain : &conn->in;
	int guard = 0;

	while (!conn->dead && guard++ < 128) {
		int fin, opcode, masked, hl, r;
		uint64_t len;
		BYTE mask[4];
		char *tmp;
		int i;

		r = net_ws_parse_header(src->p, src->len, &fin, &opcode, &masked,
			mask, &len, &hl);
		if (r == 0) return;
		if (r < 0) { net_conn_fail(conn, UI_NET_E_WS); return; }
		if (len > WS_MAX_FRAME) { net_conn_fail(conn, UI_NET_E_WS); return; }
		if (len > (uint64_t)(src->len - hl)) return;

		tmp = (char *)malloc((size_t)(len ? len : 1));
		if (!tmp) { net_conn_fail(conn, UI_NET_E_OOM); return; }
		if (len > 0) {
			if (masked) {
				for (i = 0; i < (int)len; i++)
					tmp[i] = (char)(src->p[hl + i] ^ mask[i & 3]);
			} else {
				memcpy(tmp, src->p + hl, (size_t)len);
			}
		}
		NetBuf_Consume(src, hl + (int)len);

		if (opcode == WS_OP_CLOSE) {
			ws_handle_close_(conn, tmp, (int)len);
			free(tmp);
			return;
		}
		if (opcode == WS_OP_PING) {
			ws_send_frame_(conn, WS_OP_PONG, tmp, (int)len, 1);
			w->pingAt = 0;
			free(tmp);
			continue;
		}
		if (opcode == WS_OP_PONG) {
			w->pingAt = 0;
			free(tmp);
			continue;
		}
		if (opcode != WS_OP_TEXT && opcode != WS_OP_BIN && opcode != WS_OP_CONT) {
			free(tmp);
			net_conn_fail(conn, UI_NET_E_WS);
			return;
		}
		ws_dispatch_payload_(conn, fin, opcode, tmp, (int)len);
		free(tmp);
	}
}

/* ── 入口 ───────────────────────────────────────────────────────────── */

void net_ws_on_plain(NetConn *conn, const char *data, int len)
{
	NetWsConn *w = conn ? conn->ws : NULL;
	NetBuf *src;

	(void)data; (void)len;
	if (!w || conn->dead) return;
	src = conn->tls ? &conn->plain : &conn->in;

	if (!w->upgraded) {
		char *he = Net_MemFind(src->p, src->len, "\r\n\r\n", 4);
		if (!he) {
			if (src->len > NET_HDR_MAX) net_conn_fail(conn, UI_NET_E_WS);
			return;
		}
		{
			/* hlen 含最后一行 header 的 CRLF; 之后还有空行的 CRLF */
			int hlen = (int)(he - src->p) + 2;
			if (!ws_handshake_(conn, src->p, hlen)) {
				net_conn_fail(conn, UI_NET_E_WS);
				return;
			}
			NetBuf_Consume(src, hlen + 2);
		}
		w->upgraded = 1;
		w->connectedAt = Net_NowMs();
		net_timer_cancel(conn, NET_T_HANDSHAKE);
		if (!w->openedOnce) {
			w->openedOnce = 1;
			net_emit_ws_open(conn->io, w->id);
		}
		if (w->opt.pingIntervalMs > 0) {
			w->pingAt = 0;
			net_timer_arm(conn, NET_T_PING, w->opt.pingIntervalMs);
		}
	}

	ws_parse_frames_(conn);
}

void net_ws_on_event(NetConn *conn, int connected, int err)
{
	NetWsConn *w = conn ? conn->ws : NULL;

	if (!w) return;
	if (!connected || err) {
		net_conn_fail(conn, err ? err : UI_NET_E_CONNECT);
		return;
	}
	if (w->userClosed) {
		ws_emit_close_(conn, w->closeCode ? w->closeCode : 1000, "");
		net_conn_close(conn);
		return;
	}
	ws_build_(conn);
	if (!net_conn_write(conn, w->request.p, w->request.len)) {
		net_conn_fail(conn, UI_NET_E_CONNECT);
		return;
	}
	w->handshakeSent = 1;
	net_timer_arm(conn, NET_T_HANDSHAKE,
		w->opt.handshakeTimeoutMs > 0 ? w->opt.handshakeTimeoutMs : 8000);
}

void net_ws_start(NetConn *conn, NetWsOpenReq *req)
{
	NetWsConn *w;

	if (!conn || !req) { if (req) free(req); return; }
	w = (NetWsConn *)calloc(1, sizeof(NetWsConn));
	if (!w) {
		ws_emit_close_(conn, 0, "out of memory");
		net_conn_close(conn);
		free(req);
		return;
	}
	NetBuf_Init(&w->request);
	NetBuf_Init(&w->frag);
	conn->ws = w;
	w->id = conn->id;
	Net_StrCopy(w->url, sizeof(w->url), req->url);
	w->opt = req->opt;
	if (w->opt.handshakeTimeoutMs <= 0) w->opt.handshakeTimeoutMs = 8000;
	w->extraHeaders = req->extraHeaders;
	req->extraHeaders = NULL;
	free(req->url);
	free(req);

	if (!net_parse_url(w->url, w->host, sizeof(w->host), &w->port, &w->https,
			w->path, sizeof(w->path))) {
		ws_emit_close_(conn, 0, "bad url");
		net_conn_close(conn);
		return;
	}
	Net_StrCopy(conn->host, sizeof(conn->host), w->host);
	conn->port = w->port;
	conn->https = w->https;
	net_conn_start(conn);
}

void net_ws_on_send_cmd(NetConn *conn, NetWsData *d)
{
	if (!conn || !d) return;
	if (!conn->ws || !conn->ws->upgraded || conn->dead ||
		conn->ws->userClosed) {
		free(d);
		return;
	}
	ws_send_frame_(conn, d->opcode == 2 ? WS_OP_BIN : WS_OP_TEXT, d->data,
		d->len, 1);
	free(d);
}

void net_ws_on_close_cmd(NetConn *conn, int code, const char *reason)
{
	if (!conn) return;
	net_ws_user_close(conn, code, reason);
}

void net_ws_free(NetConn *conn)
{
	NetWsConn *w;
	if (!conn || !conn->ws) return;
	w = conn->ws;
	NetBuf_Free(&w->request);
	NetBuf_Free(&w->frag);
	if (w->extraHeaders) free(w->extraHeaders);
	free(w);
	conn->ws = NULL;
}
