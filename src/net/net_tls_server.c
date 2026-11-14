/*
 * net_tls_server.c - SChannel 服务端 TLS (wss), 0CRT
 *
 * 证书来源: PFX 文件 或 Windows 证书存储 (CurrentUser / LocalMachine \ MY)。
 * 握手用 AcceptSecurityContext; 加解密复用 net_tls_encrypt_buf / decrypt_buf。
 */
#include "net/net_internal.h"
#include "log.h"

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void free(void *p);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

static int wide_(const char *s, WCHAR *out, int cap)
{
	int n;
	if (!s || cap <= 0) return 0;
	n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
	if (n <= 0) return 0;
	out[cap - 1] = 0;
	return n - 1;
}

static int cert_has_key_(PCCERT_CONTEXT cert)
{
	DWORD len = 0;
	return CertGetCertificateContextProperty(cert,
		CERT_KEY_PROV_INFO_PROP_ID, NULL, &len) ? 1 : 0;
}

static PCCERT_CONTEXT cert_from_pfx_(const char *path, const char *password,
	HCERTSTORE *outStore)
{
	HANDLE f;
	LARGE_INTEGER size;
	BYTE *data;
	DWORD got = 0;
	CRYPT_DATA_BLOB blob;
	WCHAR wpass[128];
	HCERTSTORE store;
	PCCERT_CONTEXT cert = NULL;

	f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) {
		Log_Write("[tls-srv] open pfx failed %lu", GetLastError());
		return NULL;
	}
	if (!GetFileSizeEx(f, &size) || size.QuadPart <= 0 ||
	    size.QuadPart > (32 * 1024 * 1024)) {
		CloseHandle(f);
		return NULL;
	}
	data = (BYTE *)malloc((size_t)size.QuadPart);
	if (!data) { CloseHandle(f); return NULL; }
	if (!ReadFile(f, data, (DWORD)size.QuadPart, &got, NULL) ||
	    got != (DWORD)size.QuadPart) {
		free(data);
		CloseHandle(f);
		return NULL;
	}
	CloseHandle(f);

	blob.pbData = data;
	blob.cbData = got;
	if (!wide_(password ? password : "", wpass, 128)) wpass[0] = 0;
	store = PFXImportCertStore(&blob, wpass, CRYPT_EXPORTABLE);
	free(data);
	if (!store) {
		Log_Write("[tls-srv] PFXImportCertStore failed %lu", GetLastError());
		return NULL;
	}
	{
		PCCERT_CONTEXT it = NULL;
		for (;;) {
			it = CertFindCertificateInStore(store,
				X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
				CERT_FIND_HAS_PRIVATE_KEY, NULL, it);
			if (!it) break;
			if (cert_has_key_(it)) {
				cert = CertDuplicateCertificateContext(it);
				break;
			}
		}
	}
	if (!cert) Log_Write("[tls-srv] pfx has no keyed cert");
	*outStore = store;
	return cert;
}

static int hex_bytes_(const char *s, BYTE *out, int cap)
{
	int n = 0;
	int v = -1;

	while (*s) {
		int c = (BYTE)*s++;
		int h;
		if (c == ' ' || c == ':' || c == '-' || c == '\t') continue;
		if (c >= '0' && c <= '9') h = c - '0';
		else if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') h = c - 'A' + 10;
		else return 0;
		if (v < 0) v = h;
		else {
			if (n >= cap) return 0;
			out[n++] = (BYTE)((v << 4) | h);
			v = -1;
		}
	}
	return v < 0 ? n : 0;
}

static PCCERT_CONTEXT cert_from_store_(NetSrv *srv, HCERTSTORE *outStore)
{
	DWORD flag = srv->cfg.certStoreLocalMachine
		? CERT_SYSTEM_STORE_LOCAL_MACHINE : CERT_SYSTEM_STORE_CURRENT_USER;
	HCERTSTORE store;
	PCCERT_CONTEXT cert = NULL;

	store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
		flag | CERT_STORE_READONLY_FLAG, (const void *)L"MY");
	if (!store) {
		Log_Write("[tls-srv] CertOpenStore failed %lu", GetLastError());
		return NULL;
	}
	*outStore = store;

	if (srv->cfg.certThumbprint && srv->cfg.certThumbprint[0]) {
		BYTE hash[64];
		int n = hex_bytes_(srv->cfg.certThumbprint, hash, (int)sizeof(hash));
		CRYPT_HASH_BLOB blob;
		if (n <= 0) { Log_Write("[tls-srv] bad thumbprint"); return NULL; }
		blob.cbData = (DWORD)n;
		blob.pbData = hash;
		cert = CertFindCertificateInStore(store,
			X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
			CERT_FIND_HASH, &blob, NULL);
		if (cert && !cert_has_key_(cert)) cert = NULL;
		if (!cert) Log_Write("[tls-srv] thumbprint not found");
		return cert;
	}
	{
		WCHAR wsub[256];
		PCCERT_CONTEXT it = NULL;
		if (!wide_(srv->cfg.certSubject ? srv->cfg.certSubject : "",
				wsub, 256)) {
			wsub[0] = 0;
		}
		for (;;) {
			it = CertFindCertificateInStore(store,
				X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
				CERT_FIND_SUBJECT_STR_W, (const void *)wsub, it);
			if (!it) break;
			if (cert_has_key_(it)) { cert = it; break; }
		}
		if (!cert) Log_Write("[tls-srv] subject not found");
	}
	return cert;
}

int net_tls_server_init(NetSrv *srv)
{
	SCHANNEL_CRED sc;
	TimeStamp ts;
	SECURITY_STATUS st;

	if (!srv) return UI_NET_E_PARAM;
	if (srv->credOk) return 0;

	if (srv->cfg.certPath && srv->cfg.certPath[0]) {
		srv->cert = cert_from_pfx_(srv->cfg.certPath,
			srv->cfg.certPassword, &srv->certStore);
	} else {
		srv->cert = cert_from_store_(srv, &srv->certStore);
	}
	if (!srv->cert) return UI_NET_E_CERT;

	memset(&sc, 0, sizeof(sc));
	sc.dwVersion = SCHANNEL_CRED_VERSION;
	sc.cCreds = 1;
	sc.paCred = &srv->cert;
	sc.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
#ifdef SP_PROT_TLS1_3_SERVER
	sc.grbitEnabledProtocols |= SP_PROT_TLS1_3_SERVER;
#endif

	st = AcquireCredentialsHandleW(NULL, (SEC_WCHAR *)UNISP_NAME_W,
		SECPKG_CRED_INBOUND, NULL, &sc, NULL, NULL, &srv->cred, &ts);
	if (st != SEC_E_OK) {
		Log_Write("[tls-srv] AcquireCredentialsHandle 0x%x", (unsigned)st);
		return UI_NET_E_CERT;
	}
	srv->credOk = 1;
	return 0;
}

void net_tls_server_free(NetSrv *srv)
{
	if (!srv) return;
	if (srv->credOk) {
		FreeCredentialsHandle(&srv->cred);
		srv->credOk = 0;
	}
	if (srv->cert) {
		CertFreeCertificateContext(srv->cert);
		srv->cert = NULL;
	}
	if (srv->certStore) {
		CertCloseStore(srv->certStore, 0);
		srv->certStore = NULL;
	}
}

/* ── 单连接 TLS 状态 ────────────────────────────────────────────────── */

static NetTls *sconn_tls_new_(NetSrvConn *c)
{
	NetTls *t;

	if (!c || !c->srv || !c->srv->credOk) return NULL;
	t = (NetTls *)calloc(1, sizeof(NetTls));
	if (!t) return NULL;
	t->cred = c->srv->cred;
	NetBuf_Init(&t->cipher);
	c->tls = t;
	return t;
}

void net_tls_server_conn_free(NetSrvConn *c)
{
	NetTls *t;
	if (!c || !c->tls) return;
	t = c->tls;
	if (t->ctxStarted) DeleteSecurityContext(&t->ctx);
	NetBuf_Free(&t->cipher);
	free(t);
	c->tls = NULL;
}

void net_tls_server_start(NetSrvConn *c)
{
	NetTls *t = sconn_tls_new_(c);
	if (!t) {
		srv_conn_fail_(c, UI_NET_E_TLS, "tls init failed");
		return;
	}
	/* 等客户端 ClientHello, 无需立即产生输出 */
}

/* 握手一步; 返回 0 继续/等待, 否则 UiNetErr */
static int tls_server_step_(NetSrvConn *c)
{
	NetTls *t = c->tls;
	SecBuffer inbufs[2];
	SecBuffer outbuf;
	SecBufferDesc indesc, outdesc;
	SECURITY_STATUS st;
	TimeStamp ts;
	DWORD outFlags = 0;
	int inLen = c->in.len;
	int extra = 0;
	int consumed = 0;

	memset(inbufs, 0, sizeof(inbufs));
	indesc.ulVersion = SECBUFFER_VERSION;
	indesc.cBuffers = 2;
	indesc.pBuffers = inbufs;
	if (inLen > 0) {
		inbufs[0].BufferType = SECBUFFER_TOKEN;
		inbufs[0].pvBuffer = c->in.p;
		inbufs[0].cbBuffer = (unsigned long)inLen;
		inbufs[1].BufferType = SECBUFFER_EMPTY;
	}
	outbuf.BufferType = SECBUFFER_TOKEN;
	outbuf.pvBuffer = NULL;
	outbuf.cbBuffer = 0;
	outdesc.ulVersion = SECBUFFER_VERSION;
	outdesc.cBuffers = 1;
	outdesc.pBuffers = &outbuf;

	st = AcceptSecurityContext(&c->srv->cred,
		t->ctxStarted ? &t->ctx : NULL,
		inLen > 0 ? &indesc : NULL,
		ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
			ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR |
			ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
		0, t->ctxStarted ? NULL : &t->ctx, &outdesc, &outFlags, &ts);
	if (t->ctx.dwLower || t->ctx.dwUpper) t->ctxStarted = 1;

	if (st == SEC_I_COMPLETE_NEEDED || st == SEC_I_COMPLETE_AND_CONTINUE) {
		if (CompleteAuthToken(&t->ctx, &outdesc) != SEC_E_OK)
			return UI_NET_E_TLS;
		st = st == SEC_I_COMPLETE_NEEDED ? SEC_E_OK : SEC_I_CONTINUE_NEEDED;
	}

	if (outbuf.pvBuffer && outbuf.cbBuffer > 0) {
		if (!srv_conn_write_raw_(c, outbuf.pvBuffer, (int)outbuf.cbBuffer)) {
			FreeContextBuffer(outbuf.pvBuffer);
			return UI_NET_E_OOM;
		}
		FreeContextBuffer(outbuf.pvBuffer);
	} else if (outbuf.pvBuffer) {
		FreeContextBuffer(outbuf.pvBuffer);
	}

	if (inLen > 0) {
		extra = 0;
		{
			int i;
			for (i = 0; i < 2; i++) {
				if (inbufs[i].BufferType == SECBUFFER_EXTRA) {
					extra = (int)inbufs[i].cbBuffer;
					break;
				}
			}
		}
		if (st == SEC_E_INCOMPLETE_MESSAGE || st == SEC_I_INCOMPLETE_CREDENTIALS)
			consumed = 0;
		else
			consumed = inLen - extra;
		if (consumed > 0) NetBuf_Consume(&c->in, consumed);
	}

	if (st == SEC_E_INCOMPLETE_MESSAGE || st == SEC_I_INCOMPLETE_CREDENTIALS)
		return 0;
	if (st == SEC_I_CONTINUE_NEEDED) return 0;
	if (st != SEC_E_OK) {
		Log_Write("[tls-srv] handshake failed 0x%x", (unsigned)st);
		return UI_NET_E_TLS;
	}

	/* 握手完成 */
	{
		SecPkgContext_StreamSizes sizes;
		memset(&sizes, 0, sizeof(sizes));
		if (QueryContextAttributesW(&t->ctx, SECPKG_ATTR_STREAM_SIZES,
				&sizes) != SEC_E_OK)
			return UI_NET_E_TLS;
		t->headerMax = sizes.cbHeader;
		t->trailerMax = sizes.cbTrailer;
		t->maxMessage = sizes.cbMaximumMessage;
		if (t->maxMessage == 0) t->maxMessage = 16384;
		t->established = 1;
		if (c->state == NET_SRV_C_TLS) c->state = NET_SRV_C_HTTP;
	}
	if (c->in.len > 0) {
		NetBuf_Append(&t->cipher, c->in.p, c->in.len);
		NetBuf_Clear(&c->in);
	}
	return 0;
}

void net_tls_server_input(NetSrvConn *c)
{
	int err;
	int guard;

	if (!c || c->dead || !c->tls) return;
	for (guard = 0; guard < 16 && !c->dead; guard++) {
		err = tls_server_step_(c);
		if (err) {
			srv_conn_fail_(c, err, UiNet_StrError(err));
			return;
		}
		if (c->tls->established) break;
		if (c->in.len == 0) break;
	}
	if (c->dead || !c->tls || !c->tls->established) return;
	net_tls_server_decrypt(c);
}

void net_tls_server_decrypt(NetSrvConn *c)
{
	NetTls *t;
	int err;

	if (!c || c->dead || !c->tls) return;
	t = c->tls;
	if (!t->established) { net_tls_server_input(c); return; }

	if (c->in.len > 0) {
		NetBuf_Append(&t->cipher, c->in.p, c->in.len);
		NetBuf_Clear(&c->in);
	}
	err = net_tls_decrypt_buf(t, &t->cipher, &c->plain);
	if (err) {
		srv_conn_fail_(c, err, UiNet_StrError(err));
		return;
	}
	if (c->dead || c->plain.len <= 0) return;
	net_srv_on_plain(c);
}

int net_tls_server_close_notify(NetSrvConn *c)
{
	NetTls *t;
	SecBuffer outbuf;
	SecBufferDesc outdesc;
	SECURITY_STATUS st;
	DWORD outFlags = 0;
	TimeStamp ts;

	if (!c || !c->tls || c->dead) return 0;
	t = c->tls;
	if (!t->established || t->shuttingDown) return 0;
	t->shuttingDown = 1;

	{
		SecBuffer shutdownToken;
		shutdownToken.BufferType = SECBUFFER_TOKEN;
		shutdownToken.cbBuffer = 0;
		shutdownToken.pvBuffer = NULL;
		{
			SecBufferDesc sd;
			sd.ulVersion = SECBUFFER_VERSION;
			sd.cBuffers = 1;
			sd.pBuffers = &shutdownToken;
			if (ApplyControlToken(&t->ctx, &sd) != SEC_E_OK) return 0;
		}
	}
	outbuf.BufferType = SECBUFFER_TOKEN;
	outbuf.pvBuffer = NULL;
	outbuf.cbBuffer = 0;
	outdesc.ulVersion = SECBUFFER_VERSION;
	outdesc.cBuffers = 1;
	outdesc.pBuffers = &outbuf;

	st = AcceptSecurityContext(&c->srv->cred, &t->ctx, NULL, 0, 0,
		NULL, &outdesc, &outFlags, &ts);
	if (outbuf.pvBuffer && outbuf.cbBuffer > 0)
		srv_conn_write_raw_(c, outbuf.pvBuffer, (int)outbuf.cbBuffer);
	if (outbuf.pvBuffer) FreeContextBuffer(outbuf.pvBuffer);
	return st == SEC_E_OK || st == SEC_I_CONTINUE_NEEDED;
}
