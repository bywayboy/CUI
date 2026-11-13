/*
 * net_internal.h - 网络层内部结构 (0CRT)
 *
 * 线程归属:
 *   UiNet / NetPend / UiWsH  -> GUI 线程独占
 *   NetIo / NetConn / NetTls / NetHttpConn / NetWsConn / 定时器 / DNS 缓存
 *                            -> IO 线程独占 (命令队列是唯一入口)
 */
#ifndef CUI_NET_INTERNAL_H
#define CUI_NET_INTERNAL_H

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif

#include "net/net_base.h"
#include <mswsock.h>
#include <security.h>
#include <schannel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WAKE_KEY      1
#define NET_RECV_SIZE     16384
#define NET_MAX_ADDRS     8
#define NET_HOST_MAX      255
#define NET_URL_MAX       2048
#define NET_HDR_MAX       (64 * 1024)

enum {
	NET_K_HTTP = 1,
	NET_K_WS,
	NET_K_DNS
};

enum {
	NET_S_IDLE = 0,     /* 刚创建 / 重连前 */
	NET_S_DNS,          /* UDP 查询中 */
	NET_S_DNS_TCP,      /* 截断后的 TCP 查询 */
	NET_S_CONNECT,      /* ConnectEx 进行中 */
	NET_S_TLS,          /* TLS 握手 */
	NET_S_PROXY,        /* 代理 CONNECT 隧道 */
	NET_S_TRANSPORT,    /* 已连通: HTTP 或 WS */
	NET_S_CLOSING,
	NET_S_DEAD
};

enum {
	NET_T_CONNECT = 1,
	NET_T_REQUEST,
	NET_T_HANDSHAKE,
	NET_T_IDLE,
	NET_T_PING,
	NET_T_DNS_RETRY,
	NET_T_CLOSE,
	NET_T_DNS_TCP,
	NET_T_RECONNECT
};

enum {
	NET_CMD_HTTP = 1,
	NET_CMD_HTTP_CANCEL,
	NET_CMD_WS_OPEN,
	NET_CMD_WS_SEND,
	NET_CMD_WS_CLOSE,
	NET_CMD_STOP
};

enum {
	NET_EV_HTTP_DONE = 1,
	NET_EV_WS_OPEN,
	NET_EV_WS_MSG,
	NET_EV_WS_CLOSE,
	NET_EV_SENTINEL
};

typedef struct NetConn NetConn;
typedef struct NetIo NetIo;
typedef struct NetTls NetTls;
typedef struct NetHttpConn NetHttpConn;
typedef struct NetWsConn NetWsConn;

/* ── 命令 (GUI -> IO, 堆节点) ─────────────────────────────────────── */
typedef struct NetHttpReq {
	char *method;        /* NULL=GET */
	char *url;
	char *contentType;
	char *extraHeaders;
	char *body;
	int bodyLen;
	int timeoutMs;
	int redirects;
} NetHttpReq;

typedef struct NetWsOpenReq {
	char *url;
	char *extraHeaders;
	UiWsOptions opt;
} NetWsOpenReq;

typedef struct NetWsData {
	int opcode;
	int len;
	char data[1];
} NetWsData;

typedef struct NetWsCloseCmd {
	int code;
	char reason[124];
} NetWsCloseCmd;

typedef struct NetCmd {
	SLIST_ENTRY entry;
	int type;
	uint64_t id;
	void *p;
} NetCmd;

/* ── 事件 (IO -> GUI, 堆节点) ─────────────────────────────────────── */
typedef struct NetHttpResult {
	int status;
	int error;
	UiHttpHeader *hdrs;
	int hdrCount;
	char *hdrStore;
	char *body;
	int bodyLen;
} NetHttpResult;

typedef struct NetWsMsg {
	int opcode;
	int len;
	char data[1];
} NetWsMsg;

typedef struct NetEv {
	UiNet *net;
	int type;
	uint64_t id;
	void *p;             /* NetHttpResult / NetWsMsg / NULL */
	int code;            /* WS_CLOSE */
	char reason[160];
	HANDLE done;         /* SENTINEL */
} NetEv;

/* ── 定时器 ───────────────────────────────────────────────────────── */
typedef struct NetTimer {
	UiHeapNode hn;
	NetConn *conn;
	int kind;
} NetTimer;

enum {
	NET_OP_RECV = 1,
	NET_OP_SEND,
	NET_OP_CONNECT,
	NET_OP_DNS_RECV,
	NET_OP_DNS_TCP_RECV,
	NET_OP_DNS_TCP_SEND
};

/* ── 完成操作 ─────────────────────────────────────────────────────── */
typedef struct NetOp {
	OVERLAPPED ov;       /* 必须首成员 */
	NetConn *conn;
	int kind;            /* NET_OP_* */
	int gen;             /* DNS 查询代次, 丢弃已作废的完成包 */
	char *buf;
	int bufCap;
	SOCKADDR_STORAGE addr;
	int addrLen;
} NetOp;

/* ── TLS ──────────────────────────────────────────────────────────── */
struct NetTls {
	CredHandle cred;
	CtxtHandle ctx;
	int ctxStarted;
	int established;
	DWORD headerMax, trailerMax, maxMessage;
	NetBuf cipher;       /* 待解密 (socket 原始字节) */
	int shuttingDown;
};

/* ── HTTP 连接状态 ────────────────────────────────────────────────── */
struct NetHttpConn {
	uint64_t id;
	char host[NET_HOST_MAX + 1];
	int port;
	int https;
	char path[NET_URL_MAX];     /* path + query */
	char *method;
	char *extraHeaders;
	char *body;
	int bodyLen;
	char *contentType;
	int timeoutMs;

	NetBuf request;             /* 序列化后的请求 */
	int sent;

	int redirects;
	int isHead;
	int viaProxy;
	int emitted;

	int pstate;                 /* 0=状态行 1=头 2=body 3=chunk-size 4=chunk-data 5=trailer 6=done */
	int status;
	int contentLen;             /* -1 未知 */
	int chunked;
	int noBody;
	int closeDelim;
	int keepAlive;
	int headerBytes;
	int chunkRemain;

	NetBuf hdrStore;            /* 头原文 (就地切成 NUL 结尾) */
	UiHttpHeader *hdrs;
	int hdrCount, hdrCap;
	NetBuf body2;               /* 响应体 */
	int err;
	int done;
};

/* ── WS 连接状态 ──────────────────────────────────────────────────── */
struct NetWsConn {
	uint64_t id;
	char url[NET_URL_MAX];
	char host[NET_HOST_MAX + 1];
	int port;
	int https;
	char path[NET_URL_MAX];
	UiWsOptions opt;
	char *extraHeaders;

	NetBuf request;             /* 握手请求 */
	int handshakeSent;
	int upgraded;
	int userClosed;
	int emitted;
	char key[32];
	char accept[64];
	int closeSent;
	int closeRecv;
	int closeCode;
	char closeReason[124];
	int reconnectDelay;
	int openedOnce;
	int pingAt;

	NetBuf frag;
	int fragOpcode;
};

/* ── 连接 ─────────────────────────────────────────────────────────── */
struct NetConn {
	NetIo *io;
	SOCKET s;
	uint64_t id;
	int kind;
	int state;
	int dead;
	int refs;
	ULONGLONG born;
	NetBuf in;           /* socket 收到的原始字节 */
	NetBuf plain;        /* TLS 解密后的明文 (明文连接直接用 in) */
	NetBuf out;          /* 待发送的原始字节 (已加密) */
	NetBuf tx;           /* 正在发送的字节 */
	int sending;
	int rxPending;
	int rxStopped;

	SOCKADDR_STORAGE addrs[NET_MAX_ADDRS];
	int addrCount, addrIdx, family;
	char host[NET_HOST_MAX + 1];
	int port;
	int https;

	NetTls *tls;
	NetHttpConn *http;
	NetWsConn *ws;

	/* DNS */
	char dnsName[NET_HOST_MAX + 1];
	int dnsQType;
	uint16_t dnsQid;
	int dnsServerIdx;
	int dnsAttempts;
	ULONGLONG dnsStart;
	int dnsGen;
	NetBuf dnsQuery;

	NetTimer *timers[NET_T_RECONNECT + 1];
	NetConn *next;       /* IO 注册表 */
	NetConn *idleNext;   /* HTTP keep-alive 池 */
	int inIdle;
};

/* ── IO 线程状态 ──────────────────────────────────────────────────── */
struct NetIo {
	UiNet *owner;
	HANDLE iocp;
	UiHeap timers;
	uint64_t seq;
	NetConn *conns;
	NetConn *idle;
	int idleCount;
	int stop;
	int stopping;
	LPFN_CONNECTEX connectEx;
	CredHandle sslCred;
	int sslCredOk;
	/* DNS */
	struct NetDnsCache *dnsCache;
	SOCKADDR_STORAGE dnsServers[8];
	int dnsServerCount;
	int dnsLoaded;
	uint64_t dnsServersAt;
	/* stats */
	uint64_t cmdCount, evCount, bytesIn, bytesOut, connCount;
};

/* ── GUI 侧 ───────────────────────────────────────────────────────── */
typedef struct NetPend {
	uint64_t id;
	int kind;            /* NET_K_HTTP / NET_K_WS */
	UiHttpCallback cb;
	void *user;
	struct UiWsH *ws;
	struct NetPend *next;
} NetPend;

typedef struct UiWsH {
	uint64_t id;
	UiNet *net;
	UiWsCallbacks cb;
	void *user;
	int closed;
	int state;
	struct UiWsH *next;
} UiWsH;

struct UiNet {
	UiNetConfig cfg;
	char *proxy;
	char *pin;
	NetIo *io;
	HANDLE guiThread;
	HANDLE ioThread;
	PSLIST_HEADER cmds;   /* 16 字节对齐, 单独分配 */
	volatile LONG alive;
	uint64_t nextId;
	NetPend *pend;
	UiWsH *wsList;
	/* stats (GUI 读) */
	volatile LONG64 evCount;
};

/* ── 核心 (ui_net.c) ──────────────────────────────────────────────── */
NetEv *net_ev_new(UiNet *net, int type, uint64_t id);
void   net_ev_free(NetEv *ev);
void   net_post(NetIo *io, NetEv *ev);
void   net_cmd_post(UiNet *net, NetCmd *cmd);

NetConn *net_conn_new(NetIo *io, int kind, uint64_t id);
void   net_conn_addref(NetConn *conn);
void   net_conn_release(NetConn *conn);       /* refs-- , dead 且 0 时释放 */
void   net_conn_close(NetConn *conn);         /* 置 dead + 取消 IO + 释放 */
void   net_conn_start(NetConn *conn);         /* DNS -> CONNECT -> TLS -> 传输 */
void   net_conn_fail(NetConn *conn, int err);
void   net_conn_detach_idle(NetConn *conn);
void   net_on_connected(NetConn *conn);
void   net_on_tls_ready(NetConn *conn);
void   net_on_recv_data(NetConn *conn);

void net_timer_arm(NetConn *conn, int kind, int ms);
void net_timer_cancel(NetConn *conn, int kind);
void net_timer_cancel_all(NetConn *conn);

/* ── TCP (net_tcp.c) ──────────────────────────────────────────────── */
int  net_tcp_connect(NetConn *conn, const SOCKADDR_STORAGE *a, int alen);
void net_tcp_try(NetConn *conn);              /* 从 addrIdx 起逐个尝试 */
void net_tcp_recv(NetConn *conn);
int  net_conn_write(NetConn *conn, const void *data, int len); /* 明文入口, 内部按 TLS 加密 */
void net_tx_flush(NetConn *conn);
void net_tcp_shutdown(NetConn *conn);
void net_io_on_completion(NetIo *io, OVERLAPPED *ov, ULONG_PTR key,
	DWORD bytes, DWORD err);

/* ── DNS (net_dns.c) ──────────────────────────────────────────────── */
void net_dns_resolve(NetConn *conn);                    /* 从 conn->host 开始 */
int  net_dns_cache_lookup(NetIo *io, const char *host, SOCKADDR_STORAGE *out, int cap);
void net_dns_cache_free(NetIo *io);
void net_dns_on_recv(NetConn *conn, const char *buf, int n, int err);
void net_dns_on_tcp_recv(NetConn *conn, const char *buf, int n, int err);
void net_dns_retry(NetConn *conn);
void net_dns_tcp_on_connected(NetConn *conn);
void net_dns_tcp_after_send(NetConn *conn);

/* ── TLS (net_tls.c) ──────────────────────────────────────────────── */
int  net_tls_init(NetIo *io);
void net_tls_shutdown(NetIo *io);
int  net_tls_start(NetConn *conn);
void net_tls_handshake_input(NetConn *conn);    /* 握手阶段: conn->in -> out */
void net_tls_on_cipher(NetConn *conn);          /* conn->in 新增密文 -> plain */
int  net_tls_encrypt(NetConn *conn, const void *data, int len); /* 追加到 out */
void net_tls_free(NetConn *conn);

/* ── HTTP (net_http.c) ────────────────────────────────────────────── */
NetConn *net_http_pool_get(NetIo *io, NetHttpReq *req);
int  net_http_need_tunnel(NetConn *conn);
void net_http_begin_tunnel(NetConn *conn);
void net_http_start(NetConn *conn, NetHttpReq *req);  /* 接管 req 所有权 */
void net_http_on_plain(NetConn *conn, const char *data, int len);
void net_http_on_event(NetConn *conn, int connected, int err);
void net_http_finish(NetConn *conn, int err);         /* 投结果并按需回收连接 */
void net_http_free(NetConn *conn);
void net_http_pool_put(NetConn *conn);

/* ── WS (net_ws.c) ────────────────────────────────────────────────── */
void net_ws_start(NetConn *conn, NetWsOpenReq *req);
void net_ws_on_plain(NetConn *conn, const char *data, int len);
void net_ws_on_event(NetConn *conn, int connected, int err);
void net_ws_on_send_cmd(NetConn *conn, NetWsData *d);
void net_ws_on_close_cmd(NetConn *conn, int code, const char *reason);
void net_ws_free(NetConn *conn);
void net_ws_user_close(NetConn *conn, int code, const char *reason);
int  net_ws_on_fail(NetConn *conn, int err);      /* 1=已安排重连, 连接保留 */
void net_ws_on_ping_timeout(NetConn *conn);
void net_ws_on_close_timeout(NetConn *conn);
void net_ws_reconnect_now(NetConn *conn);

/* ── 事件构造 ─────────────────────────────────────────────────────── */
void net_emit_http(NetIo *io, uint64_t id, NetHttpResult *r);
void net_emit_ws_open(NetIo *io, uint64_t id);
void net_emit_ws_msg(NetIo *io, uint64_t id, int opcode, const void *data, int len);
void net_emit_ws_close(NetIo *io, uint64_t id, int code, const char *reason);

/* ── 工具 ─────────────────────────────────────────────────────────── */
int  net_parse_url(const char *url, char *host, int hostCap, int *port,
	int *https, char *path, int pathCap);
NetConn *net_conn_by_id(NetIo *io, uint64_t id, int kind);

#ifdef __cplusplus
}
#endif

#endif /* CUI_NET_INTERNAL_H */
