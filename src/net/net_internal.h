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
#include "net/ui_ws_server.h"
#include <mswsock.h>
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>

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
	NET_CMD_STOP,
	NET_CMD_SRV_START,
	NET_CMD_SRV_STOP,
	NET_CMD_SRV_SEND,
	NET_CMD_SRV_CLOSE,
	NET_CMD_SRV_BROADCAST
};

enum {
	NET_EV_HTTP_DONE = 1,
	NET_EV_WS_OPEN,
	NET_EV_WS_MSG,
	NET_EV_WS_CLOSE,
	NET_EV_SENTINEL,
	NET_EV_SRV_LISTENING,
	NET_EV_SRV_ERROR,
	NET_EV_SRV_OPEN,
	NET_EV_SRV_MSG,
	NET_EV_SRV_CLOSE,
	NET_EV_SRV_STOPPED
};

/* 服务端定时器按 ownerKind 区分, 编号可与客户端重叠 */
enum {
	NET_T_SRV_HANDSHAKE = 1,
	NET_T_SRV_PING,
	NET_T_SRV_IDLE,
	NET_T_SRV_CLOSE,
	NET_T_SRV_MAX
};

enum {
	NET_SRV_ST_STARTING = 1,
	NET_SRV_ST_LISTENING,
	NET_SRV_ST_STOPPING,
	NET_SRV_ST_STOPPED
};

enum {
	NET_SRV_C_TLS = 1,
	NET_SRV_C_HTTP,
	NET_SRV_C_WS,
	NET_SRV_C_CLOSING,
	NET_SRV_C_DEAD
};

typedef struct NetConn NetConn;
typedef struct NetIo NetIo;
typedef struct NetTls NetTls;
typedef struct NetHttpConn NetHttpConn;
typedef struct NetWsConn NetWsConn;
typedef struct NetSrv NetSrv;
typedef struct NetSrvConn NetSrvConn;

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

typedef struct NetSrvStart {
	uint64_t id;
	UiWsServerConfig cfg;   /* 字符串均为本结构拥有的副本 */
} NetSrvStart;

typedef struct NetSrvSend {
	uint64_t srvId;
	uint64_t connId;        /* 0 = 广播 */
	int opcode;
	int len;
	char data[1];
} NetSrvSend;

typedef struct NetSrvCloseCmd {
	uint64_t srvId;
	uint64_t connId;        /* 0 = 全部 */
	int code;
	char reason[124];
} NetSrvCloseCmd;

typedef struct NetSrvStop {
	uint64_t id;
	HANDLE done;
} NetSrvStop;

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

typedef struct NetSrvOpen {
	char path[NET_URL_MAX];
	char peer[64];
	char protocol[64];
	char *hdrStore;
	UiHttpHeader *hdrs;
	int hdrCount;
} NetSrvOpen;

typedef struct NetEv {
	UiNet *net;
	int type;
	uint64_t id;
	uint64_t srvId;
	uint64_t connId;
	void *p;             /* NetHttpResult / NetWsMsg / NetSrvOpen / NULL */
	int code;            /* WS/SRV_CLOSE / SRV_LISTENING 端口 */
	char reason[160];
	HANDLE done;         /* SENTINEL / SRV_STOPPED */
} NetEv;

/* ── 定时器 ───────────────────────────────────────────────────────── */
enum {
	NET_TIMER_CONN = 1,
	NET_TIMER_SRV
};

typedef struct NetTimer {
	UiHeapNode hn;
	int ownerKind;       /* NET_TIMER_* */
	NetConn *conn;
	NetSrvConn *sconn;
	int kind;
} NetTimer;

enum {
	NET_OP_RECV = 1,
	NET_OP_SEND,
	NET_OP_CONNECT,
	NET_OP_DNS_RECV,
	NET_OP_DNS_TCP_RECV,
	NET_OP_DNS_TCP_SEND,
	NET_OP_SRV_ACCEPT,
	NET_OP_SRV_RECV,
	NET_OP_SRV_SEND
};

/* ── 完成操作 ─────────────────────────────────────────────────────── */
typedef struct NetOp {
	OVERLAPPED ov;       /* 必须首成员 */
	NetConn *conn;
	NetSrvConn *srvConn; /* 服务端 recv/send */
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
	ULONGLONG connectedAt;      /* 本次连接建立时刻 (退火判定) */

	NetBuf frag;
	int fragOpcode;
};

/* ── WebSocket 服务端 ─────────────────────────────────────────────── */
struct NetSrv {
	NetIo *io;
	uint64_t id;
	UiWsServerConfig cfg;       /* 字符串所有权归本结构 */
	SOCKET listenSock;
	int state;
	int port;
	int family;
	int tls;                    /* 配置了证书 => wss */
	CredHandle cred;
	int credOk;
	HCERTSTORE certStore;
	PCCERT_CONTEXT cert;
	int acceptPending;
	int stopping;
	int inStop;
	int connCount;
	uint64_t connSeq;
	HANDLE stopDone;
	NetSrvConn *conns;
	NetSrv *next;
};

struct NetSrvConn {
	NetSrv *srv;
	SOCKET s;
	uint64_t id;
	int state;
	int dead;
	int closing;
	int refs;
	int rxPending;
	int sending;
	int rxStopped;
	ULONGLONG born;
	ULONGLONG lastRecv;
	NetBuf in;                  /* socket 原始字节 */
	NetBuf plain;               /* TLS 解密后的明文 */
	NetBuf out;                 /* 待发送 (已加密) */
	NetBuf tx;                  /* 正在发送 */
	NetBuf frag;                /* 分片消息累积 */
	NetTls *tls;

	int upgraded;
	int opened;
	int emittedClose;
	int userClosed;
	int closeSent;
	int closeRecv;
	int closeCode;
	char closeReason[124];
	int closeAfterSend;
	int pingOutstanding;
	int fragOpcode;

	char path[NET_URL_MAX];
	char peer[64];
	char protocol[64];
	char key[64];
	char accept[64];

	NetBuf hdrStore;            /* 握手头原文 */
	UiHttpHeader *hdrs;
	int hdrCount, hdrCap;
	int headerBytes;

	NetTimer *timers[NET_T_SRV_MAX];
	NetSrvConn *next;
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
	LPFN_ACCEPTEX acceptEx;
	LPFN_GETACCEPTEXSOCKADDRS getAcceptExSockaddrs;
	CredHandle sslCred;
	int sslCredOk;
	NetSrv *servers;
	uint64_t srvSeq;
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

typedef struct UiWsConnH {
	uint64_t id;
	struct UiWsSrvH *srv;
	int closed;
	int closing;
	char path[NET_URL_MAX];
	char peer[64];
	char protocol[64];
	struct UiWsConnH *next;
} UiWsConnH;

typedef struct UiWsSrvH {
	uint64_t id;
	UiNet *net;
	UiWsServerCallbacks cb;
	void *user;
	int closed;
	int state;
	int error;
	int port;
	HANDLE ready;
	UiWsConnH *conns;
	struct UiWsSrvH *next;
} UiWsSrvH;

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
	UiWsSrvH *srvList;
	/* stats (GUI 读) */
	volatile LONG64 evCount;
};

/* ── 核心 (ui_net.c) ──────────────────────────────────────────────── */
NetEv *net_ev_new(UiNet *net, int type, uint64_t id);
void   net_ev_free(NetEv *ev);
int    net_post(NetIo *io, NetEv *ev);
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

/* 客户端/服务端共享的流式加解密 */
int  net_tls_encrypt_buf(NetTls *t, NetBuf *out, const void *data, int len);
int  net_tls_decrypt_buf(NetTls *t, NetBuf *cipher, NetBuf *plain); /* 0=OK/待更多, 否则 UiNetErr */

/* ── TLS 服务端 (net_tls_server.c) ────────────────────────────────── */
int  net_tls_server_init(NetSrv *srv);
void net_tls_server_free(NetSrv *srv);
void net_tls_server_start(NetSrvConn *c);
void net_tls_server_input(NetSrvConn *c);
void net_tls_server_decrypt(NetSrvConn *c);
int  net_tls_server_close_notify(NetSrvConn *c);
void net_tls_server_conn_free(NetSrvConn *c);

/* ── HTTP (net_http.c) ────────────────────────────────────────────── */
void net_http_req_free(NetHttpReq *r);
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
void net_ws_open_req_free(NetWsOpenReq *req);
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

/* 客户端/服务端共享的帧工具 */
int net_ws_build_frame(int masked, int opcode, const void *data, int len,
	int fin, NetBuf *out);
int net_ws_parse_header(const char *buf, int len, int *fin, int *opcode,
	int *masked, BYTE mask[4], uint64_t *plen, int *hlen);
int net_ws_utf8_valid(const void *data, int len);

/* ── WS 服务端 (net_ws_server.c) ──────────────────────────────────── */
void net_srv_start(NetIo *io, NetSrvStart *st);
void net_srv_stop(NetIo *io, uint64_t id, HANDLE done);
void net_srv_send_cmd(NetIo *io, uint64_t srvId, uint64_t connId,
	int opcode, const void *data, int len);
void net_srv_broadcast_cmd(NetIo *io, uint64_t srvId, int opcode,
	const void *data, int len);
void net_srv_close_cmd(NetIo *io, uint64_t srvId, uint64_t connId,
	int code, const char *reason);
void net_srv_on_completion(NetIo *io, NetOp *op, DWORD bytes, DWORD err);
void net_srv_conn_timer_fire(NetSrvConn *c, int kind);
void net_srv_cancel_all(NetIo *io);
void net_srv_free_all(NetIo *io);
void net_srv_cleanup_op(NetOp *op);
NetSrv *net_srv_by_id(NetIo *io, uint64_t id);
void net_srv_on_plain(NetSrvConn *c);
void srv_conn_fail_(NetSrvConn *c, int err, const char *msg);
int  srv_conn_write_raw_(NetSrvConn *c, const void *data, int len);

/* ── 事件构造 ─────────────────────────────────────────────────────── */
void net_emit_http(NetIo *io, uint64_t id, NetHttpResult *r);
void net_emit_ws_open(NetIo *io, uint64_t id);
void net_emit_ws_msg(NetIo *io, uint64_t id, int opcode, const void *data, int len);
void net_emit_ws_close(NetIo *io, uint64_t id, int code, const char *reason);
void net_emit_srv_listening(NetIo *io, uint64_t srvId, int port);
void net_emit_srv_error(NetIo *io, uint64_t srvId, int err, const char *msg);
void net_emit_srv_open(NetIo *io, NetSrvConn *c, NetSrvOpen *o);
void net_emit_srv_msg(NetIo *io, uint64_t srvId, uint64_t connId,
	int opcode, const void *data, int len);
void net_emit_srv_close(NetIo *io, uint64_t srvId, uint64_t connId,
	int code, const char *reason);
void net_emit_srv_stopped(NetIo *io, uint64_t srvId, HANDLE done);

/* ── 工具 ─────────────────────────────────────────────────────────── */
int  net_parse_url(const char *url, char *host, int hostCap, int *port,
	int *https, char *path, int pathCap);
NetConn *net_conn_by_id(NetIo *io, uint64_t id, int kind);

#ifdef __cplusplus
}
#endif

#endif /* CUI_NET_INTERNAL_H */
