/*
 * net_base.h - 网络层内部基础件 (0CRT)
 *
 * 动态字节缓冲 / 字符串与编码工具 / 侵入式最小堆。
 * 只在 src/net 内部使用, 应用请包含 ui_net.h。
 */
#ifndef CUI_NET_BASE_H
#define CUI_NET_BASE_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#include "net/ui_net.h"
#include "lib/heap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 动态字节缓冲 ─────────────────────────────────────────────────────
 * p 由 malloc 分配, 始终以 '\0' 结尾 (方便按 C 串打印), len 不含该结尾。 */
typedef struct NetBuf {
	char *p;
	int   len;
	int   cap;
} NetBuf;

void NetBuf_Init(NetBuf *b);
void NetBuf_Free(NetBuf *b);
void NetBuf_Clear(NetBuf *b);
int  NetBuf_Reserve(NetBuf *b, int extra);          /* 保证再写 extra 字节 */
int  NetBuf_Append(NetBuf *b, const void *data, int n);
int  NetBuf_AppendStr(NetBuf *b, const char *s);
int  NetBuf_AppendChar(NetBuf *b, char c);
int  NetBuf_AppendFmt(NetBuf *b, const char *fmt, ...); /* 单行上限 1024 */
void NetBuf_Consume(NetBuf *b, int n);              /* 从头部丢弃 n 字节 */

/* ── 字符串 / 编码 ─────────────────────────────────────────────────── */
int   Net_StrCaseCmpN(const char *a, const char *b, int n);
int   Net_StrCaseEq(const char *a, const char *b);
char *Net_StrCaseStr(const char *hay, const char *needle);
int   Net_StrCopy(char *dst, int cap, const char *src);   /* 返回长度 */
char *Net_MemFind(const char *buf, int len, const char *needle, int nlen);
int   Net_HexVal(int c);
void  Net_HexLower(const BYTE *in, int n, char *out);     /* out 需 2n+1 */

int  Net_Sha1(const void *data, int len, BYTE out[20]);
int  Net_Base64(const BYTE *in, int len, char *out, int cap); /* 返回长度, 0=失败 */
int  Net_Random(void *buf, int len);

uint64_t Net_NowMs(void);

#ifdef __cplusplus
}
#endif

#endif /* CUI_NET_BASE_H */
