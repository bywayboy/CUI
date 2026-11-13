/*
 * net_tls.c - SChannel 客户端 TLS (0CRT)
 *
 * 握手 token 循环 / 就地加解密 / 证书链 + 主机名校验 / 可选 SHA-256 pinning。
 */
#include "net/net_internal.h"
#include "log.h"

#include <wincrypt.h>
#include <bcrypt.h>

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);

#define TLS_HOST_W 256

static void wide_host_(const NetConn *conn, WCHAR *out, int cap)
{
	MultiByteToWideChar(CP_UTF8, 0, conn->host, -1, out, cap);
	out[cap - 1] = 0;
}

int net_tls_init(NetIo *io)
{
	SCHANNEL_CRED sc;
	TimeStamp ts;
	SECURITY_STATUS st;

	if (io->sslCredOk) return 1;
	memset(&sc, 0, sizeof(sc));
	sc.dwVersion = SCHANNEL_CRED_VERSION;
	sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
#ifdef SP_PROT_TLS1_3_CLIENT
	sc.grbitEnabledProtocols |= SP_PROT_TLS1_3_CLIENT;
#endif
	sc.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;

	st = AcquireCredentialsHandleW(NULL, (SEC_WCHAR *)UNISP_NAME_W,
		SECPKG_CRED_OUTBOUND, NULL, &sc, NULL, NULL, &io->sslCred, &ts);
	io->sslCredOk = (st == SEC_E_OK);
	if (!io->sslCredOk)
		Log_Write("[tls] AcquireCredentialsHandle failed 0x%x", (unsigned)st);
	return io->sslCredOk;
}

void net_tls_shutdown(NetIo *io)
{
	(void)io;
}

void net_tls_free(NetConn *conn)
{
	NetTls *t;
	if (!conn || !conn->tls) return;
	t = conn->tls;
	if (t->ctxStarted)
		DeleteSecurityContext(&t->ctx);
	NetBuf_Free(&t->cipher);
	free(t);
	conn->tls = NULL;
}

/* ── 证书校验 ───────────────────────────────────────────────────────── */

static int sha256_(const BYTE *data, int len, BYTE out[32])
{
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE h = NULL;
	DWORD objLen = 0, got = 0;
	BYTE *obj = NULL;
	int ok = 0;

	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
		return 0;
	if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen,
			sizeof(objLen), &got, 0) < 0)
		goto done;
	obj = (BYTE *)malloc(objLen ? objLen : 1);
	if (!obj) goto done;
	if (BCryptCreateHash(alg, &h, obj, objLen, NULL, 0, 0) < 0) goto done;
	if (BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0) < 0) goto done;
	if (BCryptFinishHash(h, out, 32, 0) < 0) goto done;
	ok = 1;
done:
	if (h) BCryptDestroyHash(h);
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	if (obj) free(obj);
	return ok;
}

static int verify_cert_(NetConn *conn)
{
	NetTls *t = conn->tls;
	UiNet *net = conn->io->owner;
	PCCERT_CONTEXT cert = NULL;
	DWORD sz = sizeof(cert);
	CERT_CHAIN_PARA cp;
	PCCERT_CHAIN_CONTEXT chain = NULL;
	SSL_EXTRA_CERT_CHAIN_POLICY_PARA extra;
	CERT_CHAIN_POLICY_PARA pp;
	CERT_CHAIN_POLICY_STATUS ps;
	WCHAR whost[TLS_HOST_W];
	int ok = 0;

	if (QueryContextAttributesW(&t->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT,
			&cert) != SEC_E_OK || !cert) {
		Log_Write("[tls] no remote cert");
		return 0;
	}
	wide_host_(conn, whost, TLS_HOST_W);

	if (net->pin && net->pin[0]) {
		BYTE hash[32];
		char hex[65];
		if (!sha256_(cert->pbCertEncoded, (int)cert->cbCertEncoded, hash)) {
			CertFreeCertificateContext(cert);
			return 0;
		}
		Net_HexLower(hash, 32, hex);
		if (!Net_StrCaseEq(hex, net->pin)) {
			Log_Write("[tls] cert pin mismatch");
			CertFreeCertificateContext(cert);
			return 0;
		}
	}
	if (net->cfg.insecure) {
		CertFreeCertificateContext(cert);
		return 1;
	}

	memset(&cp, 0, sizeof(cp));
	cp.cbSize = sizeof(cp);
	if (!CertGetCertificateChain(NULL, cert, NULL, cert->hCertStore, &cp, 0,
			NULL, &chain)) {
		Log_Write("[tls] CertGetCertificateChain failed");
		CertFreeCertificateContext(cert);
		return 0;
	}

	memset(&extra, 0, sizeof(extra));
	extra.cbSize = sizeof(extra);
	extra.dwAuthType = AUTHTYPE_SERVER;
	extra.pwszServerName = whost;
	memset(&pp, 0, sizeof(pp));
	pp.cbSize = sizeof(pp);
	pp.pvExtraPolicyPara = &extra;
	memset(&ps, 0, sizeof(ps));
	ps.cbSize = sizeof(ps);

	if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &pp, &ps))
		ok = (ps.dwError == 0);
	if (!ok)
		Log_Write("[tls] cert verify failed 0x%x",
			(unsigned)(ps.dwError ? ps.dwError : GetLastError()));

	CertFreeCertificateChain(chain);
	CertFreeCertificateContext(cert);
	return ok;
}

/* ── 握手 ───────────────────────────────────────────────────────────── */

static SECURITY_STATUS tls_step_(NetConn *conn, const char *in, int inLen,
	int *consumed, int *done)
{
	NetTls *t = conn->tls;
	NetIo *io = conn->io;
	SecBuffer inbufs[2];
	SecBuffer outbuf;
	SecBufferDesc indesc, outdesc;
	SECURITY_STATUS st;
	TimeStamp ts;
	DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
		ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
		ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
		ISC_REQ_MANUAL_CRED_VALIDATION;
	DWORD outFlags = 0;
	WCHAR whost[TLS_HOST_W];

	wide_host_(conn, whost, TLS_HOST_W);

	*consumed = 0;
	*done = 0;

	memset(inbufs, 0, sizeof(inbufs));
	indesc.ulVersion = SECBUFFER_VERSION;
	indesc.cBuffers = 2;
	indesc.pBuffers = inbufs;
	if (inLen > 0) {
		inbufs[0].BufferType = SECBUFFER_TOKEN;
		inbufs[0].pvBuffer = (void *)in;
		inbufs[0].cbBuffer = (unsigned long)inLen;
		inbufs[1].BufferType = SECBUFFER_EMPTY;
	}
	outbuf.BufferType = SECBUFFER_TOKEN;
	outbuf.pvBuffer = NULL;
	outbuf.cbBuffer = 0;
	outdesc.ulVersion = SECBUFFER_VERSION;
	outdesc.cBuffers = 1;
	outdesc.pBuffers = &outbuf;

	st = InitializeSecurityContextW(&io->sslCred,
		t->ctxStarted ? &t->ctx : NULL,
		(SEC_WCHAR *)whost, flags, 0, 0,
		inLen > 0 ? &indesc : NULL,
		0,
		t->ctxStarted ? NULL : &t->ctx,
		&outdesc, &outFlags, &ts);
	if (t->ctx.dwLower || t->ctx.dwUpper) t->ctxStarted = 1;

	if (st == SEC_I_COMPLETE_NEEDED || st == SEC_I_COMPLETE_AND_CONTINUE) {
		if (CompleteAuthToken(&t->ctx, &outdesc) != SEC_E_OK) return SEC_E_INTERNAL_ERROR;
		if (st == SEC_I_COMPLETE_NEEDED) st = SEC_E_OK;
		else st = SEC_I_CONTINUE_NEEDED;
	}

	if (outbuf.pvBuffer && outbuf.cbBuffer > 0) {
		NetBuf_Append(&conn->out, outbuf.pvBuffer, (int)outbuf.cbBuffer);
		FreeContextBuffer(outbuf.pvBuffer);
		net_tx_flush(conn);
	} else if (outbuf.pvBuffer) {
		FreeContextBuffer(outbuf.pvBuffer);
	}

	if (inLen > 0) {
		int extra = 0;
		int i;
		for (i = 0; i < 2; i++) {
			if (inbufs[i].BufferType == SECBUFFER_EXTRA) {
				extra = (int)inbufs[i].cbBuffer;
				break;
			}
			if (inbufs[i].BufferType == SECBUFFER_MISSING && st == SEC_E_INCOMPLETE_MESSAGE)
				extra = inLen;             /* 全部保留 */
		}
		if (st == SEC_E_INCOMPLETE_MESSAGE) *consumed = 0;
		else *consumed = inLen - extra;
	}

	if (st == SEC_E_OK) *done = 1;
	return st;
}

static void tls_handshake_continue_(NetConn *conn)
{
	NetTls *t = conn->tls;
	NetIo *io = conn->io;
	int guard = 0;
	SecPkgContext_StreamSizes sizes;

	for (guard = 0; guard < 16; guard++) {
		int inLen = conn->in.len;
		int consumed = 0, done = 0;
		SECURITY_STATUS st = tls_step_(conn, conn->in.p ? conn->in.p : "",
			inLen, &consumed, &done);

		if (consumed > 0) NetBuf_Consume(&conn->in, consumed);
		if (conn->dead) return;

		if (st == SEC_E_INCOMPLETE_MESSAGE) return;     /* 等更多密文 */
		if (st == SEC_I_CONTINUE_NEEDED) {
			if (conn->in.len == 0) return;
			continue;
		}
		if (st != SEC_E_OK) {
			Log_Write("[tls] handshake failed 0x%x", (unsigned)st);
			net_conn_fail(conn, UI_NET_E_TLS);
			return;
		}

		/* 握手完成 */
		if (!verify_cert_(conn)) {
			net_conn_fail(conn, UI_NET_E_TLS);
			return;
		}
		memset(&sizes, 0, sizeof(sizes));
		if (QueryContextAttributesW(&t->ctx, SECPKG_ATTR_STREAM_SIZES,
				&sizes) != SEC_E_OK) {
			net_conn_fail(conn, UI_NET_E_TLS);
			return;
		}
		t->headerMax = sizes.cbHeader;
		t->trailerMax = sizes.cbTrailer;
		t->maxMessage = sizes.cbMaximumMessage;
		if (t->maxMessage == 0) t->maxMessage = 16384;
		t->established = 1;

		net_on_tls_ready(conn);
		if (conn->dead) return;
		if (conn->in.len > 0) {
			NetBuf_Append(&t->cipher, conn->in.p, conn->in.len);
			NetBuf_Clear(&conn->in);
			net_tls_on_cipher(conn);
		}
		(void)io;
		return;
	}
	net_conn_fail(conn, UI_NET_E_TLS);
}

int net_tls_start(NetConn *conn)
{
	NetTls *t;

	if (!conn || !conn->io || !conn->io->sslCredOk) return 0;
	t = (NetTls *)calloc(1, sizeof(NetTls));
	if (!t) return 0;
	NetBuf_Init(&t->cipher);
	conn->tls = t;
	tls_handshake_continue_(conn);       /* 无输入, 直接发 ClientHello */
	return !conn->dead;
}

void net_tls_handshake_input(NetConn *conn)
{
	if (!conn || !conn->tls) { net_conn_fail(conn, UI_NET_E_TLS); return; }
	tls_handshake_continue_(conn);
}

/* ── 解密 ───────────────────────────────────────────────────────────── */

void net_tls_on_cipher(NetConn *conn)
{
	NetTls *t;
	int guard = 0;

	if (!conn || !conn->tls || conn->dead) return;
	t = conn->tls;
	if (!t->established) { net_tls_handshake_input(conn); return; }

	if (conn->in.len > 0) {
		NetBuf_Append(&t->cipher, conn->in.p, conn->in.len);
		NetBuf_Clear(&conn->in);
	}

	while (t->cipher.len > 0 && guard++ < 64) {
		SecBuffer bufs[4];
		SecBufferDesc d;
		SECURITY_STATUS st;
		int i, dataOff = -1, dataLen = 0, extraOff = -1, extraLen = 0;

		memset(bufs, 0, sizeof(bufs));
		bufs[0].BufferType = SECBUFFER_DATA;
		bufs[0].pvBuffer = t->cipher.p;
		bufs[0].cbBuffer = (unsigned long)t->cipher.len;
		bufs[1].BufferType = SECBUFFER_EMPTY;
		bufs[2].BufferType = SECBUFFER_EMPTY;
		bufs[3].BufferType = SECBUFFER_EMPTY;
		d.ulVersion = SECBUFFER_VERSION;
		d.cBuffers = 4;
		d.pBuffers = bufs;

		st = DecryptMessage(&t->ctx, &d, 0, NULL);
		if (st == SEC_E_INCOMPLETE_MESSAGE) break;
		if (st == SEC_I_CONTEXT_EXPIRED) {        /* close_notify */
			net_conn_fail(conn, UI_NET_E_CLOSED);
			return;
		}
		if (st == SEC_I_RENEGOTIATE) {
			Log_Write("[tls] renegotiate not supported");
			net_conn_fail(conn, UI_NET_E_TLS);
			return;
		}
		if (st != SEC_E_OK) {
			Log_Write("[tls] decrypt failed 0x%x", (unsigned)st);
			net_conn_fail(conn, UI_NET_E_CLOSED);
			return;
		}

		for (i = 0; i < 4; i++) {
			if (bufs[i].BufferType == SECBUFFER_DATA && dataOff < 0) {
				dataOff = (int)((char *)bufs[i].pvBuffer - t->cipher.p);
				dataLen = (int)bufs[i].cbBuffer;
			}
			if (bufs[i].BufferType == SECBUFFER_EXTRA) {
				extraOff = (int)((char *)bufs[i].pvBuffer - t->cipher.p);
				extraLen = (int)bufs[i].cbBuffer;
			}
		}
		if (dataOff >= 0 && dataLen > 0)
			NetBuf_Append(&conn->plain, t->cipher.p + dataOff, dataLen);

		if (extraLen > 0 && extraOff >= 0) {
			memmove(t->cipher.p, t->cipher.p + extraOff, (size_t)extraLen);
			t->cipher.len = extraLen;
			t->cipher.p[extraLen] = '\0';
		} else {
			t->cipher.len = 0;
			if (t->cipher.p) t->cipher.p[0] = '\0';
		}
	}

	if (conn->dead || conn->plain.len <= 0) return;
	if (conn->kind == NET_K_HTTP)
		net_http_on_plain(conn, conn->plain.p, conn->plain.len);
	else if (conn->kind == NET_K_WS)
		net_ws_on_plain(conn, conn->plain.p, conn->plain.len);
}

/* ── 加密 ───────────────────────────────────────────────────────────── */

int net_tls_encrypt(NetConn *conn, const void *data, int len)
{
	NetTls *t;
	const char *p = (const char *)data;
	int remain = len;

	if (!conn || !conn->tls || !conn->tls->established) return 0;
	t = conn->tls;
	if (t->maxMessage == 0) t->maxMessage = 16384;

	while (remain > 0) {
		int chunk = remain > (int)t->maxMessage ? (int)t->maxMessage : remain;
		int total = (int)(t->headerMax + chunk + t->trailerMax);
		char *buf = (char *)malloc((size_t)total);
		SecBuffer bufs[3];
		SecBufferDesc d;
		SECURITY_STATUS st;

		if (!buf) return 0;
		memcpy(buf + t->headerMax, p, (size_t)chunk);
		memset(buf, 0, t->headerMax);
		memset(buf + t->headerMax + chunk, 0, t->trailerMax);

		memset(bufs, 0, sizeof(bufs));
		bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
		bufs[0].pvBuffer = buf;
		bufs[0].cbBuffer = t->headerMax;
		bufs[1].BufferType = SECBUFFER_DATA;
		bufs[1].pvBuffer = buf + t->headerMax;
		bufs[1].cbBuffer = (unsigned long)chunk;
		bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
		bufs[2].pvBuffer = buf + t->headerMax + chunk;
		bufs[2].cbBuffer = t->trailerMax;
		d.ulVersion = SECBUFFER_VERSION;
		d.cBuffers = 3;
		d.pBuffers = bufs;

		st = EncryptMessage(&t->ctx, 0, &d, 0);
		if (st != SEC_E_OK) {
			free(buf);
			return 0;
		}
		{
			int n = (int)(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
			int ok = NetBuf_Append(&conn->out, buf, n);
			free(buf);
			if (!ok) return 0;
		}
		p += chunk;
		remain -= chunk;
	}
	return 1;
}
