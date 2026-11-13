/*
 * ui_net.h - CUI 网络层公开 API
 *
 * 线程规则:
 *   - UiNet_Create / Destroy / UiHttp_* / UiWs_* 只能在 GUI 线程调用;
 *   - 所有回调都在 GUI 线程执行 (IO 线程通过 QueueUserAPC 归位);
 *   - GUI 线程必须进入 alertable 等待才能收到回调: UiApp_Run 已内置,
 *     自定义消息循环请在 MsgWaitForMultipleObjectsEx 上带 MWMO_ALERTABLE;
 *   - 回调里不得调用 UiNet_Destroy (自等死锁), 可调用 UiHttp_Cancel / UiWs_Close.
 *
 * 详细设计见 docs/CUI-网络层-设计方案.md
 */
#ifndef CUI_UI_NET_H
#define CUI_UI_NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum UiNetErr {
	UI_NET_OK = 0,
	UI_NET_E_PARAM,
	UI_NET_E_OOM,
	UI_NET_E_DNS,
	UI_NET_E_CONNECT,
	UI_NET_E_TLS,
	UI_NET_E_TIMEOUT,
	UI_NET_E_CANCEL,
	UI_NET_E_CLOSED,
	UI_NET_E_HTTP,
	UI_NET_E_REDIRECT,
	UI_NET_E_WS,
	UI_NET_E_INTERNAL
} UiNetErr;

const char *UiNet_StrError(int err);

typedef struct UiNetConfig {
	int connectTimeoutMs;   /* 默认 5000 */
	int requestTimeoutMs;   /* 默认 15000, 0=不限 */
	int dnsTimeoutMs;       /* 单台服务器总预算, 默认 2000 */
	int maxRedirects;       /* 默认 5 */
	int idleTimeoutMs;      /* keep-alive 空闲回收, 默认 60000 */
	int maxIdlePerHost;     /* 默认 4 */
	int maxIdleTotal;       /* 默认 8 */
	const char *proxy;      /* "http://[user:pass@]host:port", NULL=直连 */
	int insecure;           /* 1=跳过证书校验 (仅调试) */
	const char *pinSha256;  /* 叶子证书 SHA-256 小写 hex, NULL=不校验 */
	int trace;              /* 1=写 Log */
} UiNetConfig;

typedef struct UiNet UiNet;   /* 不透明, App 持有指针 */

UiNet *UiNet_Create(const UiNetConfig *cfg);  /* cfg=NULL 用默认; GUI 线程 */
void   UiNet_Destroy(UiNet *net);             /* 返回后保证不再有回调 */

/* ── HTTP/1.1 ─────────────────────────────────────────────────────────── */

typedef struct UiHttpHeader {
	const char *name;
	const char *value;
} UiHttpHeader;

typedef struct UiHttpResponse {
	int status;                    /* 0 = 传输失败, 看 error */
	int error;                     /* UiNetErr */
	const UiHttpHeader *headers;   /* 借用: 回调返回即失效 */
	int headerCount;
	const void *body;              /* 借用: 可含 \0 的二进制 */
	int bodyLen;
} UiHttpResponse;

typedef void (*UiHttpCallback)(void *user, const UiHttpResponse *resp);

typedef struct UiHttpRequest {
	const char *method;       /* NULL = GET */
	const char *url;          /* 完整 http:// 或 https:// URL */
	const char *contentType;  /* 可 NULL; 有 body 时默认 application/octet-stream */
	const void *body;         /* 可 NULL */
	int bodyLen;
	const char *extraHeaders; /* "K: V\r\n..." 可 NULL */
	int timeoutMs;            /* 0 = 用 net 配置 */
} UiHttpRequest;

/* 内部深拷贝请求; 返回后入参可释放。返回请求 id, 0=失败。回调最多一次。 */
uint64_t UiHttp_Send(UiNet *net, const UiHttpRequest *req,
	UiHttpCallback cb, void *user);

/* 成功取消返回 1, 该请求不再回调; 已回调/未知 id 返回 0。 */
int UiHttp_Cancel(UiNet *net, uint64_t id);

/* 大小写不敏感查 header; 返回 value 长度, -1=不存在。out 可 NULL。 */
int UiHttp_Header(const UiHttpResponse *r, const char *name, char *out, int cap);

/* ── WebSocket ────────────────────────────────────────────────────────── */

typedef struct UiWsOptions {
	int autoReconnect;       /* 默认 1 */
	int reconnectMinMs;      /* 默认 500 */
	int reconnectMaxMs;      /* 默认 10000 */
	int reconnectBackoff;    /* 退火倍率 1..10, 默认 2; 1=固定间隔 */
	int reconnectJitterPct;  /* 抖动百分比 0..50, 默认 20 */
	int reconnectStableMs;   /* 连接稳定超过该值后重置退避, 默认 30000 */
	int pingIntervalMs;      /* 默认 30000, 0=关闭心跳 */
	int handshakeTimeoutMs;  /* 默认 8000 */
	const char *extraHeaders;/* 握手附加 header, 可 NULL */
} UiWsOptions;

typedef struct UiWsCallbacks {
	void (*open)(void *user);
	void (*message)(void *user, const void *data, int len, int opcode); /* 1=text 2=binary */
	void (*close)(void *user, int code, const char *reason);            /* 只来一次 */
} UiWsCallbacks;

/* 返回句柄 (连接失败也会返回, 错误经 close 回调); net=NULL 返回 NULL。 */
void *UiWs_Open(UiNet *net, const char *url, const UiWsOptions *opt,
	const UiWsCallbacks *cb, void *user);

/* 1=已入队 (调用返回后 data 可释放); 0=失败/未连接。 */
int UiWs_Send(void *ws, const void *data, int len, int opcode);

/* 幂等; 关闭完成后 close 回调, 之后句柄失效。 */
void UiWs_Close(void *ws, int code, const char *reason);

/* 103 正常关闭 / 1000 等; -1 = 未关闭。 */
int UiWs_State(void *ws);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_NET_H */
