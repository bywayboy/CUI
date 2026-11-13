# CUI 网络层设计方案

> 状态：已实现（P0–P4，2026-11-13）
> 关联：`src/net/*`（新增）、`src/lib/heap.*`（新增）、`src/ui/ui_timer.*`（新增）、
> `src/ui/ui_app.*`（接入）、`samples/net.c`（新增）、`tests/net_test.c`（新增）、
> 白皮书第 10 章
> 参考：`WJClientService` 的 APC 归位与定时器模型（`src/lib/service_base.cc`）、
> `src/net/websocket.c`（旧例，仅作帧编解码参考，不参与构建）

## 1. 目标与非目标

为 CUI 补一套**应用持有、零锁、单 IO 线程**的网络与定时器：

| 能力 | 说明 |
| --- | --- |
| HTTP/1.1 客户端 | GET/POST/自定义方法、header、body、超时、取消、keep-alive 池、重定向、代理 |
| HTTPS | SChannel 原生 TLS，证书链 + 主机名校验，可选指纹 pinning |
| WebSocket 客户端 | ws/wss、文本/二进制、分片、ping/pong 心跳、指数退避重连 |
| DNS | **自研解析器**：UDP 组包 + IOCP，A/AAAA、CNAME、重试、TC 走 TCP、TTL 缓存 |
| 定时器 | GUI 线程 `UiTimer`（最小堆 + waitable timer）；IO 线程定时器复用同一份最小堆 |

非目标（v1 不做）：

- 服务端监听（AcceptEx / HTTP server）；
- HTTP/2（需 nghttp2，违背最小依赖）、gzip 解压（不自带 Accept-Encoding）；
- 代理自动发现 WPAD / PAC（只支持显式代理，后续可加）；
- 并行 Happy Eyeballs（IPv6 先探测）、TLS 1.3 0-RTT、客户端证书；
- 绑定引擎 / 全局单例 / 回调线程池：一切状态都由 App 持有的 `UiNet` 收敛。

已确认的两项产品决定：

1. **不用 WinHTTP**，自建 IOCP + SChannel，保证真·单 IO 线程与完全控制权；
2. **DNS 不允许阻塞 IO 线程**，自组 DNS 报文走 UDP（TC 置位再走 TCP），不借系统线程池。

## 2. 线程模型与不变量

```
GUI 线程 ──命令──▶ SLIST 无锁队列 + PostQueuedCompletionStatus ──▶ IO 线程(唯一) + IOCP(concurrency=1)
GUI 线程 ◀──事件── QueueUserAPC(堆节点) ◀─────────────────────────── IO 线程
```

**三条不变量**（代码评审与文档都以它为准）：

1. **网络状态只在 IO 线程修改**：连接、TLS 上下文、解析器、IO 定时器、DNS 缓存全部
   属于 `NetIo`，GUI 线程只持有句柄表与回调表。
2. **应用状态只在 GUI 线程修改**：HTTP/WS 回调、取消、销毁都在 GUI 线程执行，
   App 不需要任何锁。
3. **APC 只在帧边界执行**：GUI 线程进入 alertable 等待时才会执行 APC。CUI 的等待点
   只有 `ui_app.c` 主循环与 `ui_frame.c` 的帧间隔，二者都在
   `Ui_BeginFrame/EndFrame` 之外，因此回调里可安全 `UiApp_Invalidate*`。

### 2.1 原语选择

| 方向 | 原语 | 原因 |
| --- | --- | --- |
| GUI → IO | `InterlockedPushEntrySList` / `InterlockedFlushSList` + `PostQueuedCompletionStatus` | IOCP 等待（`GetQueuedCompletionStatusEx`）**不是 alertable**，APC 唤不醒它；SLIST 是原生无锁栈，flush 后反转得 FIFO，单消费者无 ABA |
| IO → GUI | `QueueUserAPC` | 目标线程 alertable 即执行，APC 队列 FIFO，天然有序 |
| 定时 | 最小堆 + 等待超时 / `SetWaitableTimer` | IO 侧不需要额外内核对象；GUI 侧一个 waitable timer 服务全部任务 |

每条命令都 post 一个 wake packet（`PostQueuedCompletionStatus` 约 1µs，客户端命令
频率低，不做唤醒合并——合并会引入丢唤醒风险且收益可忽略）。

### 2.2 GUI 线程接入（两个必须的改动）

```c
/* ui_app.c:484 空闲等待 */
MsgWaitForMultipleObjectsEx(n, handles, INFINITE, QS_ALLINPUT,
                            MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
/* ui_frame.c:116 帧间隔等待 */
MsgWaitForMultipleObjectsEx(n, handles, ms + 4, QS_ALLINPUT,
                            MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
```

APC 提前中断帧等待只会让该帧略早渲染，不影响正确性。**自定义消息循环的应用必须
自己加 `MWMO_ALERTABLE`**，否则收不到网络回调（文档与头文件注明）。

### 2.3 关闭时序（防 UAF）

APC 是排队执行的：IO 线程投出的事件可能仍在 GUI 队列里，此时释放 `UiNet` 会 UAF。
利用 APC FIFO，用哨兵收口：

```
GUI: UiNet_Destroy
  1. post STOP 命令
  2. IO: 拒绝新命令 → 关闭所有连接 → 退出线程（此后不再投 APC）
  3. GUI: WaitForSingleObject(ioThread)         // 非 alertable，无死锁风险
  4. GUI: 投哨兵 APC，然后 WaitForSingleObjectEx(ev, INFINITE, TRUE)
         // alertable 等待会按序执行之前所有事件 APC，再执行哨兵
  5. GUI: 释放 NetIo / 句柄
```

约束：**回调里不得调用 `UiNet_Destroy`**（自等死锁）；需要销毁请 post 到下一帧。
回调里可以调 `UiHttp_Cancel` / `UiWs_Close`（同线程，表操作安全）。

## 3. 组件划分

| 文件 | 职责 |
| --- | --- |
| `src/net/net_base.h/.c` | 错误码、动态字节缓冲、字符串/大小写/十六进制、SHA-1/Base64、随机数 |
| `src/lib/heap.c/.h` | 通用侵入式最小堆（net IO 定时器与 UiTimer 共用） |
| `src/net/ui_net.h` | **唯一公开头**：`UiNet` / `UiHttp` / `UiWs` API、错误码、配置 |
| `src/net/net_internal.h` | 内部结构：NetIo / NetConn / 命令 / 事件 / 各模块入口 |
| `src/net/ui_net.c` | `UiNet_Create/Destroy`、IO 线程、IOCP 循环、命令分发、APC 派发、IO 定时器、连接注册表 |
| `src/net/net_dns.c` | 自研 DNS：服务器发现、hosts、A/AAAA 组包解析、重试、TC→TCP、缓存 |
| `src/net/net_tcp.c` | socket 生命周期、ConnectEx、重叠收发、send 队列、引用计数释放 |
| `src/net/net_tls.c` | SChannel 凭据、握手状态机、加解密、证书校验、pinning、关闭 |
| `src/net/net_http.c` | URL 解析、请求序列化、增量响应解析、chunked、keep-alive 池、重定向、代理 |
| `src/net/net_ws.c` | upgrade、帧编解码、分片、控制帧、心跳、重连、关闭握手 |
| `src/ui/ui_timer.h/.c` | GUI 线程定时器（最小堆 + waitable timer） |
| `samples/net.c` | 演示 UI：HTTP 请求、WS 连接、定时器 |
| `tests/net_test.c` | headless 断言：纯逻辑全量（堆/队列/URL/Base64/SHA1/DNS 包/HTTP/WS/定时器） |

依赖新增：`ws2_32.lib`、`secur32.lib`、`crypt32.lib`、`bcrypt.lib`、`iphlpapi.lib`。
全部动态容量（`malloc/realloc`），无固定上限；0CRT 约束不变（无 CRT 函数）。

## 4. 公共 API（thin）

```c
/* ── 错误码 ── */
typedef enum UiNetErr {
    UI_NET_OK = 0, UI_NET_E_PARAM, UI_NET_E_OOM, UI_NET_E_DNS,
    UI_NET_E_CONNECT, UI_NET_E_TLS, UI_NET_E_TIMEOUT, UI_NET_E_CANCEL,
    UI_NET_E_CLOSED, UI_NET_E_HTTP, UI_NET_E_REDIRECT, UI_NET_E_WS,
    UI_NET_E_INTERNAL
} UiNetErr;
const char *UiNet_StrError(int err);

/* ── 配置 ── */
typedef struct UiNetConfig {
    int connectTimeoutMs;    /* 默认 5000 */
    int requestTimeoutMs;    /* 默认 15000, 0=不限 */
    int dnsTimeoutMs;        /* 单服务器单次 800, 总预算 = 此值 */
    int maxRedirects;        /* 默认 5 */
    int idleTimeoutMs;       /* keep-alive 空闲回收, 默认 60000 */
    int maxIdlePerHost;      /* 默认 4 */
    int maxIdleTotal;        /* 默认 8 */
    const char *proxy;       /* "http://[user:pass@]host:port", NULL=直连 */
    int insecure;            /* 1=跳过证书校验(仅调试) */
    const char *pinSha256;   /* 可选: 叶子证书 SHA-256 小写 hex */
    int trace;               /* 1=写 Log */
} UiNetConfig;

UiNet *UiNet_Create(const UiNetConfig *cfg);   /* GUI 线程调用, cfg=NULL 用默认 */
void   UiNet_Destroy(UiNet *net);              /* 返回后保证不再有回调 */

/* ── HTTP ── */
typedef struct UiHttpHeader { const char *name; const char *value; } UiHttpHeader;
typedef struct UiHttpResponse {
    int status;                    /* 0 = 传输失败 */
    int error;                     /* UiNetErr */
    const UiHttpHeader *headers;   /* 借用, 回调返回即失效 */
    int headerCount;
    const void *body;              /* 借用, 可含 \0 的二进制 */
    int bodyLen;
} UiHttpResponse;
typedef void (*UiHttpCallback)(void *user, const UiHttpResponse *resp);

typedef struct UiHttpRequest {
    const char *method;       /* NULL = GET */
    const char *url;          /* 必须是完整 http:// 或 https:// URL */
    const char *contentType;  /* 可 NULL */
    const void *body;         /* 可 NULL */
    int bodyLen;
    const char *extraHeaders; /* "K: V\r\n..." 可 NULL */
    int timeoutMs;            /* 0 = 用 net 配置 */
} UiHttpRequest;

/* 内部拷贝请求, 返回后入参可释放。返回请求 id (0=失败)。 */
uint64_t UiHttp_Send(UiNet *net, const UiHttpRequest *req,
                     UiHttpCallback cb, void *user);
/* 成功取消返回 1, 该请求不再回调; 已回调/不存在返回 0。 */
int  UiHttp_Cancel(UiNet *net, uint64_t id);
/* 大小写不敏感取 header; 返回长度, -1=不存在。 */
int  UiHttp_Header(const UiHttpResponse *r, const char *name, char *out, int cap);

/* ── WebSocket ── */
typedef struct UiWsOptions {
    int autoReconnect;       /* 默认 1 */
    int reconnectMinMs;      /* 默认 500 */
    int reconnectMaxMs;      /* 默认 10000 */
    int reconnectBackoff;    /* 退火倍率 1..10, 默认 2; 1=固定间隔 */
    int reconnectJitterPct;  /* 抖动百分比 0..50, 默认 20 */
    int reconnectStableMs;   /* 稳定超过该值后重置退避, 默认 30000 */
    int pingIntervalMs;      /* 默认 30000, 0=关 */
    int handshakeTimeoutMs;  /* 默认 8000 */
    const char *extraHeaders;
} UiWsOptions;
typedef struct UiWsCallbacks {
    void (*open)(void *user);
    void (*message)(void *user, const void *data, int len, int opcode); /* 1=text 2=binary */
    void (*close)(void *user, int code, const char *reason); /* code=0+err 见 reason */
} UiWsCallbacks;

/* 返回句柄(失败也会返回, 错误经 close 回调); net=NULL 返回 NULL。 */
void *UiWs_Open(UiNet *net, const char *url, const UiWsOptions *opt,
                const UiWsCallbacks *cb, void *user);
int   UiWs_Send(void *ws, const void *data, int len, int opcode); /* 1=入队 */
void  UiWs_Close(void *ws, int code, const char *reason);         /* 幂等 */
/* 返回 close 回调里给出的 code; -1 = 未关闭 */
int   UiWs_State(void *ws);

/* ── GUI 定时器（UiApp 内嵌一份） ── */
typedef void (*UiTimerFn)(void *user, uint64_t id);
UiTimer *UiTimer_Create(void);
void     UiTimer_Destroy(UiTimer *tm);
uint64_t UiTimer_After(UiTimer *tm, int ms, UiTimerFn fn, void *user);
uint64_t UiTimer_Every(UiTimer *tm, int ms, UiTimerFn fn, void *user);
int      UiTimer_Cancel(UiTimer *tm, uint64_t id);
/* 自定义消息循环: 把 WaitHandle 加进 MsgWait 数组; 返回后调 Dispatch 跑到期回调 */
HANDLE   UiTimer_WaitHandle(UiTimer *tm);
int      UiTimer_Dispatch(UiTimer *tm);   /* 返回下个到期剩余 ms, -1=无任务 */
```

### 4.1 所有权与生命周期

| 对象 | 谁释放 | 规则 |
| --- | --- | --- |
| `UiNet` | App（`UiNet_Destroy`） | Create/Destroy 必须同线程（GUI）；Destroy 返回后无回调 |
| HTTP 请求 | UiNet | 入参在 `Send` 返回后即可释放；回调只来一次（成功/失败/取消除外，见下） |
| HTTP 响应 | UiNet | header/body 是**借用切片**，回调返回即失效；需要留存请自行拷贝 |
| `UiWs` | UiNet | `close` 回调之后句柄失效，App 必须丢弃 |
| `UiTimer` | App（`UiTimer_Destroy`） | 回调在 GUI 线程；Destroy 后不再回调 |

**取消语义**：`UiHttp_Cancel` 从 GUI 表摘除条目并 post 取消命令；成功返回 1 后
**不会再回调**。若响应 APC 已在队列里，派发时查不到条目 → 静默释放。

**WS 重连语义**：连接断开 → `close(code=1006 或错误)` → 若 `autoReconnect`，
退避后自动重连 → 成功再 `open`。App 在 `close` 里可以决定 `UiWs_Close` 停止重连。

## 5. 无锁命令队列与 APC 事件

```c
/* GUI → IO */
typedef struct NetCmd {
    SLIST_ENTRY entry;
    int type;             /* NET_CMD_HTTP_SEND / WS_OPEN / WS_SEND / WS_CLOSE / HTTP_CANCEL / STOP */
    uint64_t id;
    void *ptr;            /* 已深拷贝的请求/负载 */
} NetCmd;

/* IO → GUI */
typedef struct NetEvent {
    struct NetEvent *next;  /* 仅调试用; APC 队列本身 FIFO */
    UiNet *net;
    int type;               /* HTTP_DONE / WS_OPEN / WS_MSG / WS_CLOSE / SHUTDOWN_SENTINEL */
    uint64_t id;
    void *data;
} NetEvent;
```

- 命令节点由 GUI 线程 `malloc`，IO 线程处理完 `free`；SLIST flush 得到 LIFO，
  反转一次变 FIFO。
- 事件节点由 IO 线程 `malloc`，APC trampoline 在 GUI 线程派发后 `free`。
- 派发失败（net 已 dead）只释放节点，不回调。
- 每个连接有 `pending` 计数；`dead` 后计数归零才释放连接，避免 `CancelIoEx`
  之后仍收到完成包导致 UAF。

## 6. IOCP 引擎

- `CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1)`；连接 socket 用
  `WSA_FLAG_OVERLAPPED` 创建后 `CreateIoCompletionPort(s, iocp, (ULONG_PTR)conn, 0)`。
- 等待：`GetQueuedCompletionStatusEx(..., 16, &n, timeoutMs, FALSE)`，批量取；
  `timeoutMs` = IO 定时器堆顶剩余时间（无任务则 `INFINITE`）。
- 完成包键：`KEY_WAKE`（命令唤醒，`lpOverlapped == NULL`）、`KEY_DNS`、
  `KEY_CONN`（携带 `IoOp*`）。
- 所有 overlapped 操作使用 `IoOp`，`OVERLAPPED` 必须是首成员（`container_of`）。
- 取消：`CancelIoEx(s, &op->ov)` 后不立即释放，等该操作完成包返回（`ERROR_OPERATION_ABORTED`）
  再减计数；连接的 `dead` 置位且 `pending == 0` 才 close/free。
- 唤醒不合并：每条命令一个 `PostQueuedCompletionStatus(iocp, 0, KEY_WAKE, NULL)`。

### 6.1 连接状态机

```
IDLE → DNS → CONNECTING → [TLS_HANDSHAKE] → TRANSPORT(HTTP 或 WS) → CLOSING → DEAD
```

每个状态挂一个 IO 定时器（connect/request/handshake/idle/ping），状态切换时
`UiHeap_Erase/Adjust`，到期回调只改状态 + 投事件，不做阻塞动作。

## 7. 自研 DNS 解析器（net_dns.c）

### 7.1 服务器发现

- `GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST|SKIP_MULTICAST|SKIP_FRIENDLY_NAME)`
  遍历 `FirstDnsServerAddress`，收集 IPv4/IPv6 DNS 服务器（去重，保持系统顺序）。
- 读 `%SystemRoot%\System32\drivers\etc\hosts`（`CreateFileW`+`ReadFile`，0CRT），
  支持 `#` 注释、IPv4/IPv6、大小写不敏感精确名匹配；`localhost` 内置兜底。
- 服务器列表为空 → 解析失败（不硬编码公共 DNS，避免隐私/合规问题）。

### 7.2 查询

- 报文：header(id 随机、RD=1、QD=1) + QNAME 标签 + QTYPE(A=1 / AAAA=28) + QCLASS(IN)。
  不做 EDNS0（A/AAAA 不触发截断；TC 时有 TCP 兜底）。
- 传输：`WSASendTo` / `WSARecvFrom` 重叠 IO，socket 挂在同一个 IOCP（KEY_DNS），
  **不阻塞 IO 线程**。
- 重试：每台服务器 2 次 × 800ms，轮换下一台，总预算 `dnsTimeoutMs`。
- 响应校验：id 匹配、QR=1、rcode=0；跳过 question；遍历 answer，支持压缩指针
  （跳转上限 32 次）；CNAME(5) 建链，A(1)/AAAA(28) 收地址；其他类型跳过。
- `TC=1`：对该服务器走 TCP 重查（2 字节长度前缀 + 报文），复用 TCP 引擎的
  连接/收发原语，仍在本 IO 线程。
- 缓存：`{name, addrs[], ttl, expires}`，最多 64 条，TTL 夹取 [10, 300] 秒；
  未命中的负缓存 10 秒。缓存读写只在 IO 线程，无锁。

### 7.3 地址使用顺序

A 记录优先，AAAA 其次（v1 不做 Happy Eyeballs）。连接失败按列表顺序换下一个
地址，全部失败才报 `UI_NET_E_CONNECT`。

## 8. SChannel TLS（net_tls.c）

### 8.1 凭据与握手

- 每个 `UiNet` 一份 `CredHandle`：`AcquireCredentialsHandleW(UNISP_NAME_W,
  SECPKG_CRED_OUTBOUND, SCHANNEL_CRED)`，启用 TLS 1.2/1.3（1.3 宏按 SDK 条件编译），
  `SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS`。
- 每连接一个 `CtxtHandle`。握手循环：
  `InitializeSecurityContextW(ISC_REQ_STREAM|SEQUENCE_DETECT|REPLAY_DETECT|
  CONFIDENTIALITY|EXTENDED_ERROR|ALLOCATE_MEMORY|MANUAL_CRED_VALIDATION,
  pszTargetName = host)` → 输出 token → `WSASend`；收到对端数据 → 继续 ISC，
  直到 `SEC_E_OK` / `SEC_I_CONTINUE_NEEDED`。
- `pszTargetName` 同时启用 SNI。`SEC_I_INCOMPLETE_CREDENTIALS` / `SEC_I_RENEGOTIATE`
  按状态机继续。
- **握手结束时 ISC 输出缓冲里的 `SECBUFFER_EXTRA` 是首个应用数据**，必须转入
  解密路径，否则丢首包。

### 8.2 证书校验

```
QueryContextAttributes(SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert)
→ CertGetCertificateChain(hCertStore=NULL, cert, ...)
→ CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
     SSL_EXTRA_CERT_CHAIN_POLICY_PARA{ .pwszServerName = host })
```
一次完成链、有效期、用途、主机名匹配（SAN/CN）。失败 → `UI_NET_E_TLS`，关闭连接。
`insecure=1` 跳过；`pinSha256` 非空时再比对叶子证书 SHA-256（十六进制小写）。

### 8.3 加解密与关闭

- 发送：`QueryContextAttributes(SECPKG_ATTR_STREAM_SIZES)` 得
  `cbHeader/cbMaximumMessage/cbTrailer`；按 `[header|明文|trailer]` 连续缓冲就地
  `EncryptMessage`，大 body 按 `cbMaximumMessage` 切 record。WS 掩码也在这块缓冲
  里就地写，消除旧实现两次分配 + 全量拷贝。
- 接收：`DecryptMessage` 的 `STREAM + DATA` 就地解密；`SEC_E_INCOMPLETE_MESSAGE`
  继续收；`SECBUFFER_EXTRA` 前移保留；`SEC_I_CONTEXT_EXPIRED` 视为 close_notify。
  `SEC_I_RENEGOTIATE` 在 v1 **不支持**：直接断开报 `UI_NET_E_TLS`（实现取舍，
  见第 15 节）。
- 关闭：`ApplyControlToken(SCHANNEL_SHUTDOWN)` → `EncryptMessage` 生成
  close_notify → `WSASend` → `closesocket`。

## 9. HTTP/1.1 客户端（net_http.c）

- URL 解析：scheme/host/port/path/query/fragment，支持 IPv6 字面量 `[::1]`，
  仅接受 `http`/`https`。
- 请求序列化：`METHOD path HTTP/1.1\r\nHost: host[:port]\r\n` + Content-Length
  + `extraHeaders` + body；HTTPS 默认 443、HTTP 80，非默认端口写入 Host。
- 响应增量解析（对已解密字节流）：状态行 → headers（上限 64KB）→ body
  （Content-Length / chunked / 读到 EOF 三种）；1xx 跳过；HEAD/204/304 无 body。
- chunked：解析 chunk-size（含扩展）、chunk-data、trailer；非法字节流报 `UI_NET_E_HTTP`。
- keep-alive：按 `(scheme, host, port)` 池化空闲连接，`idleTimeoutMs` 后由 IO
  定时器回收；`maxIdlePerHost/maxIdleTotal` 上限；服务器 `Connection: close` 或
  HTTP/1.0 不复用。
- 重定向：301/302/303/307/308 + Location；相对地址解析；303 改 GET；
  跨 host 去除 `Authorization`/`Cookie`；超过 `maxRedirects` 报错。
- 代理：显式 `http://[user:pass@]host:port`。HTTP 走绝对 URI；HTTPS 先
  `CONNECT host:port`（可选 `Proxy-Authorization: Basic`），200 后再进 TLS。
- 超时：connect / 整体请求 / idle 三段；`UiHttp_Cancel` 走 `CancelIoEx`。
- 传输层**不解析 JSON**、不依赖 cJSON；body 原样交给 App。

## 10. WebSocket 客户端（net_ws.c）

- 握手复用 HTTP 引擎：`GET path HTTP/1.1` + `Upgrade: websocket` +
  `Connection: Upgrade` + `Sec-WebSocket-Key` + `Version: 13`；校验 `101`、
  `Upgrade`、`Connection`、`Sec-WebSocket-Accept = base64(SHA1(key+GUID))`。
  握手响应头之后剩余的字节直接喂给帧解析器（不丢包）。
- 帧编解码：从 `src/net/websocket.c` 移植，改为**每连接状态**（分片缓冲、
  控制帧、掩码），去掉 `static` 全局；客户端发送全部掩码，掩码就地写入发送缓冲；
  控制帧（pong/close）插到发送队列头部；支持 16 位/64 位长度；单帧上限可配
  （默认 16MB，分片累积同限）。
- 心跳：`pingIntervalMs` 到期发 ping；收到 pong 刷新活跃时间；ping 超时未
  pong 判死 → 断开重连（避免旧实现 60s 死等）。
- 重连：断开后指数退避（`reconnectMinMs → reconnectMaxMs`，倍率
  `reconnectBackoff`、抖动 `reconnectJitterPct`）；**退火**：上次连接稳定超过
  `reconnectStableMs` 才把退避重置回下限，抖动型故障会继续翻倍；退避期间
  `UiWs_Close` 可立即停止。
- 关闭：发 close 帧，等对端 close 或 `ClosingTimeout`（2s）后 close socket；
  `close` 回调只来一次。
- `wss` 与 https 共用同一 TLS 路径；v1 不支持 WS 代理（代理只作用于 HTTP 请求）。

## 11. GUI 定时器（src/ui/ui_timer）

- 最小堆按 `(deadline, id)` 排序；节点含 interval / 周期标志 / 回调 / user。
- 一个 `CreateWaitableTimerW(FALSE)`；`SetWaitableTimer` 到堆顶（不挂 APC，
  避免依赖 alertable），handle 暴露给消息循环。
- `UiTimer_Dispatch` 弹所有到期任务、执行（周期任务重排）、再 program 下一个，
  返回下个剩余 ms。
- `UiApp` 内嵌 `UiTimer *timer`：启动时 Create，退出时 Destroy；空闲等待数组
  加入 `UiTimer_WaitHandle`；每轮循环 `UiTimer_Dispatch`。动画期间帧间隔
  ≤ 8ms，定时器精度自然满足（文档注明"动画期按帧量化"）。
- 旧 `SetTimer(hwnd, 2, 530ms)` 的编辑光标闪烁迁移到 `UiTimer_Every`。

## 12. 0CRT / 依赖 / 构建

- 只用 `malloc/realloc/free/memcpy/memset/strlen/strcmp/sprintf/snprintf` 等项目
  已有 stub；不引入 CRT、STL、异常。
- 新链接库：`ws2_32.lib secur32.lib crypt32.lib bcrypt.lib iphlpapi.lib`。
- 构建：`build.cmd` 把 `src/net/*.c src/ui/ui_timer.c` 加入 `OBJS`（所有样例共享），
  新增 `samples/net.c` → `bin\cui_net.exe`；新增 `tests/net_test.c` →
  `bin\cui_net_test.exe`（`/SUBSYSTEM:CONSOLE`，写 stdout 断言计数，非 0 退出码即失败）。
- 0CRT 校验：对两个新 exe 执行与现有样例相同的 imports 检查（不得出现 msvcrt）。

## 13. 分阶段落地与判据

| 阶段 | 内容 | 判据 |
| --- | --- | --- |
| P0 | 文档、最小堆/缓冲/队列、`UiTimer`、UiNet 骨架（IOCP/命令/APC/关闭哨兵） | headless：堆有序性/随机 adjust/erase、SLIST FIFO、定时器到期与取消；GUI 空转无泄漏 |
| P1 | DNS、TCP、HTTP/1.1、WS（明文） | 本机 Node 服务器：GET/POST/chunked/keep-alive/重定向/超时/取消、WS 回显/分片/心跳/断线重连；headless：URL/DNS 包/HTTP 解析/WS 帧 |
| P2 | SChannel、HTTPS、WSS、证书校验与 pinning | 真实站点 https GET；wss 回显；错误证书/错误主机名被拒；`insecure` 仅调试生效 |
| P3 | 连接池/代理/压力与性能 | 并发请求吞吐、GUI 线程零阻塞（回调期间无 socket 调用）、命令→APC 归位延迟（P50/P99） |
| P4 | 样例、白皮书、README、设计文档修订 | 文档与实现一致；样例可交互验证 |

验证工具：本机 Node 起 HTTP/WS 测试服务器（仅测试用，不进项目依赖）；headless
断言覆盖所有纯逻辑；真实网络验证需外网时使用用户提供的 URL 或公开站点。

## 14. 与旧 `src/net/websocket.c` 的关系

旧文件是 WJTrayApp 的移植样例：硬编码 `127.0.0.1:19860`、全局单例 + 临界区、
GUI 线程持锁阻塞 send、`static` 分片缓冲、无 TLS、传输层耦合 cJSON，且未接入
构建。新实现**只复用它的协议知识**（握手头、帧格式、分片、控制帧），代码按本
文档的 per-connection 状态机重写。旧文件（含 `websocket.h`）已删除，历史版本
可在 git 中查阅。

## 15. 风险与取舍

| 风险 | 缓解 |
| --- | --- |
| SChannel 半包/extra 处理错误 → 偶发丢包或卡死 | 状态机单测 + 大 body/分片压力；解密缓冲前移逻辑独立函数并断言 |
| APC 在非 alertable 循环里不执行 | 头文件/文档写明；`UiApp_Run` 已内置；样例演示自定义循环写法 |
| DNS 解析器兼容性（压缩指针/截断/异常包） | 用真实报文做 headless 用例；跳转上限、长度边界全部校验 |
| 连接释放与 CancelIoEx 竞争 | pending 计数 + dead 标志；引用归零才释放；压力测试开/关连接 |
| 单 IO 线程被大 body 解析占满 | 解析只做线性扫描与 memcpy；单请求上限可配；后续可拆"解析线程"（暂不做） |
| 证书校验策略过严/过松 | 默认系统策略 + 主机名；`insecure` 显式开关；pinning 可选 |

## 16. 实现修订与验证记录（2026-11-13）

实现过程中确认/修正的关键点：

1. **请求 id 从 1 开始**：0 保留为“无效 id”，否则首个请求的结果会被派发逻辑丢弃。
2. **`maxRedirects = 0` 视为默认值**（5），不是“禁止重定向”，避免零初始化配置
   意外禁用重定向。
3. **目标端口统一在 `net_tcp_try` 写回**：DNS/hosts/缓存返回的地址端口为 0，
   早期只在 `net_conn_start` 的直连分支写端口，走 DNS 时 ConnectEx 报
   `WSAEADDRNOTAVAIL (10049)`。
4. **TLS 分支必须武装首个 recv**：明文分支在 `net_on_connected` 里 arm recv，
   TLS 分支漏了，结果 ClientHello 发出后没人读 ServerHello，表现为握手超时。
5. **keep-alive 复用要换 id + 重新 arm recv**：池化连接沿用旧请求 id 会导致
   新结果被丢弃；池化时 recv 已停，复用后必须重新 `net_tcp_recv`。
6. **WS 握手头块长度**：`\r\n\r\n` 搜索命中的是第一行尾 CRLF，传给校验函数的
   块必须包含该 CRLF（+2），否则最后一行 header 少一个字符，`Sec-WebSocket-Accept`
   尾部的 `=` 丢失而被判失败。
7. **DNS 代次 `dnsGen`**：关闭 UDP/TCP 查询 socket 时自增，完成包代次不匹配即丢弃，
   避免“旧查询的 aborted 完成包”把新查询推进到下一台服务器。
8. **HTTP 请求日志按 `cfg.trace` 开关**；DNS/连接/握手失败日志始终保留（低频）。
9. **测试服务器**：`tests/net_server_http.ps1`（18080，含 /ka keep-alive 与
   `X-Conn-Req`）、`tests/net_server_ws.ps1`（18081 回显）、
   `tests/net_server_wss.ps1`（18082 自签证书）；`cui_net_test.exe` 在
   `CUI_NET_LIVE=1` 时跑实网断言，否则只跑纯逻辑。

验证结果：

| 项 | 结果 |
| --- | --- |
| headless 纯逻辑 | 877 断言 0 失败（堆/缓冲/字符串/SHA1/Base64/生命周期） |
| 实网 | 52 断言 0 失败：HTTP 200/chunked/redirect/POST、keep-alive 复用（同连接第 2 请求）、400ms 超时、取消不回调、hosts、DNS 自解析 + HTTPS（example.com 577B）、自签证书被拒（`UI_NET_E_TLS`）、ws 回显、wss 回显 |
| 原生 UI | 200% DPI 真实鼠标：GET/POST/WS 连接/发送/断开；真实键入 `https://www.example.com/` 后 GET 返回 200/577B；定时器 1s 递增 |
| 构建 | 14 个 exe 0CRT（`cui_net.exe` / `cui_net_test.exe` 新增） |
