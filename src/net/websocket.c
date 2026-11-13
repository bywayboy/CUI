/*
 * websocket.c - RFC 6455 WebSocket 客户端（0CRT 兼容）
 * 裸 socket + ws2_32/crypt32；无 SSL
 * 目标: ws://127.0.0.1:19860/smartCtrl
 * 与 WJTrayApp 同方案（WJClientService ws_bridge 桥接至服务器）
 */
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>

#include "websocket.h"
#include "lib/cJSON.h"
#include "log.h"

/* CRT stubs（0CRT 模式，见 lib/crtstub.c） */
int sprintf(char *buf, const char *fmt, ...);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
int strncmp(const char *a, const char *b, size_t n);
void *malloc(size_t sz);
void free(void *p);
void *realloc(void *p, size_t sz);

#define WS_HOST   "127.0.0.1"
#define WS_PORT   19860
#define WS_PATH   "/smartCtrl"
#define WS_GUID   "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define MAX_FRAME   (256 * 1024)   /* 单帧上限 */
#define MAX_FRAG    (256 * 1024)   /* 分片累积上限 */
#define RECONNECT_MIN   500
#define RECONNECT_MAX   2000
#define RECONNECT_STEP  100
#define PING_INTERVAL   30000
#define DEAD_INTERVAL   60000
#define CONNECT_TIMEOUT 5000

static HWND g_hwnd = NULL;
static HANDLE g_thread = NULL;
static volatile LONG g_run = 0;
static SOCKET g_sock = INVALID_SOCKET;
static CRITICAL_SECTION g_lock;
static int g_started = 0;
static HCRYPTPROV g_prov = 0;

/* 唤醒 APC: 运行在 ws 线程上下文 —— 在此置停止标志天然有序, 无需额外同步。
   同时打破该线程当前的 alertable 等待(SleepEx / WSAWaitForMultipleEvents) */
static void CALLBACK ws_wake_apc(ULONG_PTR param)
{
	(void)param;
	InterlockedExchange(&g_run, 0);
}

/* ── 基础工具 ── */

static int recv_exact(SOCKET s, char *buf, int n)
{
	while (n > 0) {
		int r = recv(s, buf, n, 0);
		if (r <= 0) return 0;
		buf += r;
		n -= r;
	}
	return 1;
}

static int send_all(SOCKET s, const char *buf, int n)
{
	while (n > 0) {
		int r = send(s, buf, n, 0);
		if (r == SOCKET_ERROR) return 0;
		buf += r;
		n -= r;
	}
	return 1;
}

static int b64encode(const BYTE *in, DWORD inLen, char *out, DWORD outCap)
{
	DWORD len = 0;
	if (!CryptBinaryToStringA(in, inLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &len)) return 0;
	if (len > outCap) return 0;
	if (!CryptBinaryToStringA(in, inLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &len)) return 0;
	return (int)lstrlenA(out);
}

static int sha1_buf(const char *data, int len, BYTE out[20])
{
	HCRYPTPROV prov = 0;
	HCRYPTHASH h = 0;
	if (!CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return 0;
	if (!CryptCreateHash(prov, CALG_SHA1, 0, 0, &h)) { CryptReleaseContext(prov, 0); return 0; }
	CryptHashData(h, (const BYTE *)data, len, 0);
	DWORD hl = 20;
	CryptGetHashParam(h, HP_HASHVAL, out, &hl, 0);
	CryptDestroyHash(h);
	CryptReleaseContext(prov, 0);
	return 1;
}

static int strncasecmp_c(const char *a, const char *b, int n)
{
	for (int i = 0; i < n; i++) {
		char ca = a[i], cb = b[i];
		if (ca >= 'A' && ca <= 'Z') ca += 32;
		if (cb >= 'A' && cb <= 'Z') cb += 32;
		if (ca != cb) return ca - cb;
		if (!ca) return 0;
	}
	return 0;
}

/* ── 连接 ── */

static int connect_with_timeout(SOCKET *out, int timeout_ms)
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET) return 0;

	u_long nb = 1;
	ioctlsocket(s, FIONBIO, &nb);

	WSAEVENT ev = WSACreateEvent();
	if (ev == WSA_INVALID_EVENT) { closesocket(s); return 0; }
	if (WSAEventSelect(s, ev, FD_CONNECT) == SOCKET_ERROR) {
		WSACloseEvent(ev);
		closesocket(s);
		return 0;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(WS_PORT);
	addr.sin_addr.s_addr = inet_addr(WS_HOST);

	int r = connect(s, (struct sockaddr *)&addr, sizeof(addr));
	int err = 0;
	if (r == SOCKET_ERROR) {
		int le = WSAGetLastError();
		if (le != WSAEWOULDBLOCK) { WSACloseEvent(ev); closesocket(s); return 0; }
		/* alertable 等待 FD_CONNECT: 退出时 QueueUserAPC 立即中断(返回 WAIT_IO_COMPLETION),
		   不必等满 timeout_ms, 也无需短轮询。
		   注: Windows 回环连未监听端口时 RST 不会唤醒 select(实测等满超时),
		   所以这里必须用可中断的事件等待。 */
		DWORD wr = WSAWaitForMultipleEvents(1, &ev, FALSE, (DWORD)timeout_ms, TRUE);
		if (wr != WSA_WAIT_EVENT_0) { WSACloseEvent(ev); closesocket(s); return 0; }
		WSANETWORKEVENTS ne;
		memset(&ne, 0, sizeof(ne));
		WSAEnumNetworkEvents(s, ev, &ne);
		err = ne.iErrorCode[FD_CONNECT_BIT];
	}
	WSAEventSelect(s, ev, 0);   /* 解除事件关联 */
	WSACloseEvent(ev);
	if (err != 0) { closesocket(s); return 0; }
	u_long nb0 = 0;
	ioctlsocket(s, FIONBIO, &nb0);
	*out = s;
	return 1;
}

/* 查找 \r\n\r\n */
static const char *find_hdr_end(const char *buf, int len)
{
	for (int i = 0; i + 3 < len; i++)
		if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n')
			return buf + i;
	return NULL;
}

/* 在响应头里校验 Sec-WebSocket-Accept（大小写不敏感） */
static int check_accept(const char *resp, const char *expect)
{
	const char *p = resp;
	while (*p) {
		const char *line = p;
		while (*p && *p != '\r' && *p != '\n') p++;
		int len = (int)(p - line);
		if (len > 21 && strncasecmp_c(line, "sec-websocket-accept:", 21) == 0) {
			const char *v = line + 21;
			while (*v == ' ') v++;
			int vlen = len - (int)(v - line);
			int elen = (int)lstrlenA(expect);
			return (vlen == elen && strncasecmp_c(v, expect, elen) == 0);
		}
		while (*p == '\r' || *p == '\n') p++;
	}
	return 0;
}

static int ws_handshake(SOCKET s)
{
	BYTE rnd[16];
	char key[32];
	if (!CryptGenRandom(g_prov, 16, rnd)) return 0;
	int klen = b64encode(rnd, 16, key, sizeof(key));
	if (klen <= 0) return 0;
	key[klen] = '\0';

	char req[512];
	int rn = sprintf(req,
		"GET " WS_PATH " HTTP/1.1\r\n"
		"Host: " WS_HOST ":%d\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n",
		WS_PORT, key);
	if (!send_all(s, req, rn)) return 0;

	/* 读响应头，5s 超时 */
	DWORD to = CONNECT_TIMEOUT;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&to, sizeof(to));

	char resp[4096];
	int rlen = 0;
	while (rlen < (int)sizeof(resp) - 1) {
		int n = recv(s, resp + rlen, (int)sizeof(resp) - 1 - rlen, 0);
		if (n <= 0) return 0;
		rlen += n;
		resp[rlen] = '\0';
		if (find_hdr_end(resp, rlen)) break;
	}
	if (!find_hdr_end(resp, rlen)) return 0;
	if (strncmp(resp, "HTTP/1.1 101", 12) != 0) return 0;

	/* 校验 Sec-WebSocket-Accept = base64(SHA1(key + GUID)) */
	char tohash[128];
	memcpy(tohash, key, klen);
	memcpy(tohash + klen, WS_GUID, 36);
	BYTE hash[20];
	if (!sha1_buf(tohash, klen + 36, hash)) return 0;
	char expect[64];
	int elen = b64encode(hash, 20, expect, sizeof(expect));
	if (elen <= 0) return 0;
	expect[elen] = '\0';
	return check_accept(resp, expect);
}

/* ── 帧收发 ── */

static int send_frame(SOCKET s, int opcode, const char *payload, int len)
{
	BYTE hdr[14];
	int hlen = 2;
	hdr[0] = (BYTE)(0x80 | opcode);

	BYTE mask[4];
	CryptGenRandom(g_prov, 4, mask);

	hdr[1] = 0x80; /* MASK */
	if (len < 126) {
		hdr[1] |= (BYTE)len;
	} else if (len < 65536) {
		hdr[1] |= 126;
		hdr[2] = (BYTE)((len >> 8) & 0xFF);
		hdr[3] = (BYTE)(len & 0xFF);
		hlen = 4;
	} else {
		hdr[1] |= 127;
		uint64_t l64 = (uint64_t)len;
		for (int i = 0; i < 8; i++) hdr[2 + i] = (BYTE)((l64 >> (56 - i * 8)) & 0xFF);
		hlen = 10;
	}
	memcpy(hdr + hlen, mask, 4);
	hlen += 4;

	if (!send_all(s, (const char *)hdr, hlen)) return 0;
	if (len > 0) {
		BYTE *tmp = (BYTE *)malloc((size_t)len);
		if (!tmp) return 0;
		for (int i = 0; i < len; i++) tmp[i] = (BYTE)payload[i] ^ mask[i & 3];
		int ok = send_all(s, (const char *)tmp, len);
		free(tmp);
		if (!ok) return 0;
	}
	return 1;
}

static int send_pong(SOCKET s, const char *payload, int len)
{
	return send_frame(s, 0xA, payload, len);
}

/* 读取一个完整消息。返回 1=有文本消息；0=继续（控制帧/分片中）；-1=断开 */
static int ws_read_frame(SOCKET s, char **out_payload, int *out_len)
{
	static char *g_frag = NULL;   /* 分片累积缓冲 */
	static int g_fragLen = 0;
	static int g_fragCap = 0;

	char hdr[2];
	if (!recv_exact(s, hdr, 2)) return -1;
	int fin = hdr[0] & 0x80;
	int opcode = hdr[0] & 0x0F;
	int masked = hdr[1] & 0x80;
	uint64_t len = hdr[1] & 0x7F;

	if (len == 126) {
		BYTE b[2];
		if (!recv_exact(s, (char *)b, 2)) return -1;
		len = ((uint64_t)b[0] << 8) | b[1];
	} else if (len == 127) {
		BYTE b[8];
		if (!recv_exact(s, (char *)b, 8)) return -1;
		len = 0;
		for (int i = 0; i < 8; i++) len = (len << 8) | b[i];
	}
	if (len > MAX_FRAME) return -1;

	BYTE mask[4];
	if (masked) {
		if (!recv_exact(s, (char *)mask, 4)) return -1;
	}

	char *buf = (char *)malloc((size_t)(len ? len : 1));
	if (!buf) return -1;
	if (!recv_exact(s, buf, (int)len)) { free(buf); return -1; }
	if (masked) {
		for (uint64_t i = 0; i < len; i++) buf[i] ^= (char)mask[i & 3];
	}

	switch (opcode) {
	case 0x8: /* close */
		free(buf);
		return -1;
	case 0x9: /* ping → 回 pong */
		if (!send_pong(s, buf, (int)len)) { free(buf); return -1; }
		free(buf);
		return 0;
	case 0xA: /* pong */
		free(buf);
		return 0;
	case 0x1: /* text */
	case 0x2: /* binary（本地服务仅文本，按文本处理） */
		if (!fin) {
			if (g_fragLen + (int)len > MAX_FRAG) { free(buf); return -1; }
			if (g_fragLen + (int)len > g_fragCap) {
				int ncap = g_fragCap ? g_fragCap * 2 : 1024;
				while (ncap < g_fragLen + (int)len) ncap *= 2;
				char *nf = (char *)realloc(g_frag, (size_t)ncap);
				if (!nf) { free(buf); return -1; }
				g_frag = nf;
				g_fragCap = ncap;
			}
			memcpy(g_frag + g_fragLen, buf, (size_t)len);
			g_fragLen += (int)len;
			free(buf);
			return 0;
		}
		*out_payload = buf;
		*out_len = (int)len;
		return 1;
	case 0x0: /* continuation */
		if (g_fragLen + (int)len > MAX_FRAG) { free(buf); return -1; }
		if (g_fragLen + (int)len > g_fragCap) {
			int ncap = g_fragCap ? g_fragCap * 2 : 1024;
			while (ncap < g_fragLen + (int)len) ncap *= 2;
			char *nf = (char *)realloc(g_frag, (size_t)ncap);
			if (!nf) { free(buf); return -1; }
			g_frag = nf;
			g_fragCap = ncap;
		}
		memcpy(g_frag + g_fragLen, buf, (size_t)len);
		g_fragLen += (int)len;
		free(buf);
		if (fin) {
			*out_payload = g_frag;
			*out_len = g_fragLen;
			g_fragLen = 0;
			return 1;
		}
		return 0;
	default:
		free(buf);
		return -1;
	}
}

/* ── 线程 ── */

static DWORD WINAPI ws_thread(LPVOID param)
{
	(void)param;
	int delay = RECONNECT_MIN;

	while (g_run) {
		SOCKET s = INVALID_SOCKET;
		if (!connect_with_timeout(&s, CONNECT_TIMEOUT)) {
			Log_Write("[ws] connect fail, retry in %dms", delay);
			if (g_run) PostMessageW(g_hwnd, WM_APP_WS_CLOSE, 0, 0);
			/* 可中断退避: alertable 睡眠, 被唤醒时返回 WAIT_IO_COMPLETION, 直接回循环顶部检查 g_run */
			if (g_run && SleepEx((DWORD)delay, TRUE) != WAIT_IO_COMPLETION) {
				if (delay < RECONNECT_MAX) delay += RECONNECT_STEP;
			}
			continue;
		}

		EnterCriticalSection(&g_lock);
		g_sock = s;
		LeaveCriticalSection(&g_lock);

		if (!ws_handshake(s)) {
			Log_Write("[ws] handshake fail");
			EnterCriticalSection(&g_lock);
			g_sock = INVALID_SOCKET;
			LeaveCriticalSection(&g_lock);
			closesocket(s);
			if (g_run) PostMessageW(g_hwnd, WM_APP_WS_CLOSE, 0, 0);
			if (g_run && SleepEx((DWORD)delay, TRUE) != WAIT_IO_COMPLETION) {
				if (delay < RECONNECT_MAX) delay += RECONNECT_STEP;
			}
			continue;
		}

		delay = RECONNECT_MIN; /* 连接成功，重置退避 */
		Log_Write("[ws] connected ws://%s:%d%s", WS_HOST, WS_PORT, WS_PATH);
		if (g_run) PostMessageW(g_hwnd, WM_APP_WS_OPEN, 0, 0);

		ULONGLONG last_active = GetTickCount64();
		ULONGLONG ping_time = 0;
		int ping_out = 0;

		/* 读循环: WSAEventSelect + alertable 等待, 替代不可中断的 select(5s)。
		   退出时 QueueUserAPC 投递到本线程, WSAWaitForMultipleEvents 立即返回
		   WAIT_IO_COMPLETION → 回循环顶部检查 g_run, 不必等满 5s。
		   注意: WSAEventSelect 会把 socket 切为非阻塞, 故在 recv 前用
		   WSAEventSelect(s, rdEv, 0) 取消事件关联、恢复阻塞模式, 否则
		   ws_read_frame 的阻塞 recv 会拿到 WSAEWOULDBLOCK 而被误判为断开。 */
		WSAEVENT rdEv = WSACreateEvent();
		if (rdEv == WSA_INVALID_EVENT) {
			EnterCriticalSection(&g_lock);
			g_sock = INVALID_SOCKET;
			LeaveCriticalSection(&g_lock);
			closesocket(s);
			if (g_run) PostMessageW(g_hwnd, WM_APP_WS_CLOSE, 0, 0);
			if (g_run && SleepEx((DWORD)delay, TRUE) != WAIT_IO_COMPLETION) {
				if (delay < RECONNECT_MAX) delay += RECONNECT_STEP;
			}
			continue;
		}

		for (;;) {
			if (!g_run) break;

			if (WSAEventSelect(s, rdEv, FD_READ | FD_CLOSE) == SOCKET_ERROR)
				break;
			DWORD wr = WSAWaitForMultipleEvents(1, &rdEv, FALSE, 5000, TRUE);
			if (wr == WAIT_IO_COMPLETION) {       /* 收到退出 APC → 回顶部查 g_run */
				WSAEventSelect(s, rdEv, 0);
				continue;
			}
			if (wr != WSA_WAIT_EVENT_0) {          /* 超时 / 等待失败 */
				WSAEventSelect(s, rdEv, 0);
				if (wr == WAIT_FAILED) break;      /* 等待失败 → 重连 */
				/* 超时: 心跳 / 死亡判定 */
				ULONGLONG now = GetTickCount64();
				if (!ping_out && now - last_active > PING_INTERVAL) {
					EnterCriticalSection(&g_lock);
					int ok = (g_sock == s) ? send_frame(g_sock, 0x9, NULL, 0) : 0;
					LeaveCriticalSection(&g_lock);
					if (!ok) break;
					ping_out = 1;
					ping_time = now;
				}
				if (now - last_active > DEAD_INTERVAL ||
					(ping_out && now - ping_time > PING_INTERVAL)) {
					break; /* 判定死亡，重连 */
				}
				continue;
			}

			/* 有可读/关闭事件: 取事件位并复位, 再取消事件关联恢复阻塞 recv */
			WSANETWORKEVENTS ne;
			memset(&ne, 0, sizeof(ne));
			WSAEnumNetworkEvents(s, rdEv, &ne);
			WSAEventSelect(s, rdEv, 0);
			if (ne.lNetworkEvents & FD_CLOSE) {
				if (ne.iErrorCode[FD_CLOSE_BIT] != 0) break;  /* 硬关闭 → 重连 */
				/* 优雅关闭: 落空余数据后 recv 返回 0 → ws_read_frame 返回 -1 → 断开 */
			}

			char *payload = NULL;
			int plen = 0;
			int got = ws_read_frame(s, &payload, &plen);
			if (got < 0) break;
			last_active = GetTickCount64();
			ping_out = 0;
			if (got == 1) {
				/* 日志: 截断到 800 字节(crtstub vsprintf 不支持 %.*s) */
				char logbuf[832];
				int logn = plen < 800 ? plen : 800;
				memcpy(logbuf, payload, (size_t)logn);
				logbuf[logn] = '\0';
				Log_Write("[ws] recv %d bytes: %s", plen, logbuf);
				cJSON *json = cJSON_ParseWithLength(payload, (size_t)plen);
				if (json) {
					if (g_run)
						PostMessageW(g_hwnd, WM_APP_WS_MSG, 0, (LPARAM)json);
					else
						cJSON_Delete(json);
				} else {
					break; /* JSON 解析失败 → 断开重连 */
				}
			}
		}

		WSAEventSelect(s, rdEv, 0);
		WSACloseEvent(rdEv);

		EnterCriticalSection(&g_lock);
		g_sock = INVALID_SOCKET;
		LeaveCriticalSection(&g_lock);
		closesocket(s);

		if (g_run) PostMessageW(g_hwnd, WM_APP_WS_CLOSE, 0, 0);
		if (g_run && SleepEx((DWORD)delay, TRUE) != WAIT_IO_COMPLETION) {
			if (delay < RECONNECT_MAX) delay += RECONNECT_STEP;
		}
	}
	return 0;
}

/* ── 对外接口 ── */

int WS_Start(HWND hwnd)
{
	if (g_thread) return 1;
	g_hwnd = hwnd;
	InitializeCriticalSection(&g_lock);
	g_started = 1;
	if (!g_prov)
		CryptAcquireContextA(&g_prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
	g_run = 1;
	g_thread = CreateThread(NULL, 0, ws_thread, NULL, 0, NULL);
	return g_thread ? 1 : 0;
}

void WS_Stop(void)
{
	if (!g_thread) return;
	Log_Write("[ws] stop: begin");
	/* 置停止标志 + 中断线程当前的 alertable 等待; APC 投递失败说明线程已退出, 直接置标志即可 */
	if (!QueueUserAPC(ws_wake_apc, g_thread, 0))
		InterlockedExchange(&g_run, 0);
	EnterCriticalSection(&g_lock);
	if (g_sock != INVALID_SOCKET) shutdown(g_sock, SD_BOTH); /* 唤醒 select/recv */
	LeaveCriticalSection(&g_lock);
	DWORD wr = WaitForSingleObject(g_thread, 1500);
	if (wr != WAIT_OBJECT_0) {
		/* 线程未及时退出: 放弃等待。故意不 CloseHandle/DeleteCriticalSection,
		   避免仍在运行的线程访问已释放的锁(进程即将退出, 由系统回收) */
		Log_Write("[ws] stop: thread not exited (wait=%u), give up", (unsigned int)wr);
		return;
	}
	CloseHandle(g_thread);
	g_thread = NULL;
	if (g_prov) { CryptReleaseContext(g_prov, 0); g_prov = 0; }
	g_started = 0;
	DeleteCriticalSection(&g_lock);
	Log_Write("[ws] stop: done");
}

int WS_Send(const char *data, int len)
{
	int ok = 0;
	if (!g_started) return 0;
	EnterCriticalSection(&g_lock);
	if (g_sock != INVALID_SOCKET)
		ok = send_frame(g_sock, 0x1, data, len);
	LeaveCriticalSection(&g_lock);
	if (ok) {
		/* 日志: 截断到 800 字节 */
		char logbuf[832];
		int logn = len < 800 ? len : 800;
		memcpy(logbuf, data, (size_t)logn);
		logbuf[logn] = '\0';
		Log_Write("[ws] send %d bytes: %s", len, logbuf);
	}
	return ok;
}
