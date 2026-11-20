/*
 * ui_ws_server.h - CUI WebSocket 服务端公开 API (ws / wss)
 *
 * 线程规则与 ui_net.h 相同: 所有函数只能在 GUI 线程调用, 所有回调都在
 * GUI 线程执行, GUI 消息循环必须 alertable (UiApp_Run 已满足).
 *
 * 生命周期:
 *   Create 后立即异步 bind/listen; 成功经 listening 回调给出实际端口,
 *   失败经 error 回调. Destroy 会关闭监听与全部连接, 等待 close/stopped
 *   事件处理完再返回; 不得在服务端回调里调用 Destroy.
 */
#ifndef CUI_UI_WS_SERVER_H
#define CUI_UI_WS_SERVER_H

#include <stdint.h>
#include "net/ui_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum UiWsServerState {
	UI_WS_SRV_STARTING = 0,
	UI_WS_SRV_LISTENING,
	UI_WS_SRV_STOPPING,
	UI_WS_SRV_STOPPED,
	UI_WS_SRV_ERROR
} UiWsServerState;

typedef struct UiWsServerConfig {
	const char *bindHost;      /* NULL = 0.0.0.0; 支持 IPv4/IPv6 字面量 */
	int port;                  /* 0 = 临时端口; listening 回调给出实际值 */
	const char *path;          /* NULL = 接受任意路径; 否则精确匹配 (不含 query) */
	const char *subprotocols;  /* "chat,superchat"; 可 NULL */
	const char *extraHeaders;  /* 101 响应附加 header, "K: V\r\n..."; 可 NULL */

	/* wss: 以下任一证书来源非 NULL 即启用 TLS. PFX 优先. */
	const char *certPath;      /* PFX/P12 文件路径 */
	const char *certPassword;  /* PFX 密码, 可 NULL */
	const char *certSubject;   /* 证书存储 Subject, 如 "CN=localhost" */
	const char *certThumbprint;/* SHA-1 thumbprint (hex, 可含空格/冒号) */
	int certStoreLocalMachine; /* 1=LocalMachine\MY, 0=CurrentUser\MY */

	int maxConnections;        /* 默认 1024 */
	int maxMessageSize;        /* 单条消息上限, 默认 16 MiB */
	int maxSendBuffer;         /* 单连接待发送上限, 默认 16 MiB */
	int handshakeTimeoutMs;    /* 默认 10000 */
	int pingIntervalMs;        /* 默认 0=关闭; >0 主动 ping */
	int idleTimeoutMs;         /* 默认 0=关闭; >0 无入站数据则断开 */
	int closeTimeoutMs;        /* 关闭握手等待, 默认 2000 */
	int trace;                 /* 1=写 Log */
} UiWsServerConfig;

typedef struct UiWsServerRequest {
	const char *path;          /* 不含 query */
	const char *peer;          /* "ip:port" */
	const char *protocol;      /* 协商出的子协议, 可能为 NULL */
	const UiHttpHeader *headers;
	int headerCount;
} UiWsServerRequest;

typedef struct UiWsSrvH UiWsServer;
typedef struct UiWsConnH UiWsConn;

typedef struct UiWsServerCallbacks {
	void (*listening)(void *user, int port);
	void (*open)(void *user, UiWsConn *conn, const UiWsServerRequest *req);
	/* opcode: 1=text(UTF-8) 2=binary; data 仅在回调期间有效 */
	void (*message)(void *user, UiWsConn *conn, const void *data, int len, int opcode);
	/* 只对已 open 的连接回调一次; 回调返回后 conn 失效 */
	void (*close)(void *user, UiWsConn *conn, int code, const char *reason);
	void (*error)(void *user, int err, const char *message);
} UiWsServerCallbacks;

/* 返回句柄; net=NULL 或参数非法返回 NULL. 监听结果为异步. */
UiWsServer *UiWsServer_Create(UiNet *net, const UiWsServerConfig *cfg,
	const UiWsServerCallbacks *cb, void *user);

/* 停止监听、关闭全部连接并等待清理; 句柄随后失效. */
void UiWsServer_Destroy(UiWsServer *srv);

int UiWsServer_Port(const UiWsServer *srv);      /* 0 = 尚未监听 */
int UiWsServer_State(const UiWsServer *srv);     /* UiWsServerState */
int UiWsServer_ConnCount(const UiWsServer *srv);

/* 1=已入队 (data 可立即释放); 0=失败/未 open/正在关闭.
 * opcode 仅接受 1=text 2=binary; text 必须是合法 UTF-8, 否则返回 0. */
int UiWsServer_Send(UiWsConn *conn, const void *data, int len, int opcode);

/* 向当前所有已 open 连接发送; 返回入队前的连接数.
 * text 同样按 UTF-8 校验, 非法时返回 0. */
int UiWsServer_Broadcast(UiWsServer *srv, const void *data, int len, int opcode);

/* 幂等; 发起关闭握手, close 回调后 conn 失效. */
void UiWsServer_Close(UiWsConn *conn, int code, const char *reason);

/* 对全部连接发起关闭. */
void UiWsServer_CloseAll(UiWsServer *srv, int code, const char *reason);

const char *UiWsConn_Path(const UiWsConn *conn);
const char *UiWsConn_Peer(const UiWsConn *conn);
const char *UiWsConn_Protocol(const UiWsConn *conn);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_WS_SERVER_H */
