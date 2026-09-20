#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "bearssl.h"
#include "trust_anchors.h"

typedef struct {
    int https, port;
    char host[128], path[1400];
} url_t;

typedef struct {                     /* one connection, plain or TLS */
    SOCKET s;
    int tls;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context io;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
} conn_t;

static int g_started;

int net_init(void)
{
    WSADATA w;
    if (g_started) return 1;
    if (WSAStartup(MAKEWORD(1, 1), &w) != 0) return 0;
    g_started = 1;
    return 1;
}

void net_shutdown(void) { if (g_started) { WSACleanup(); g_started = 0; } }

void net_free(ea_http *r) { if (r && r->body) { free(r->body); r->body = 0; } }

static int parse_url(const char *url, url_t *u)
{
    const char *p = url, *slash, *colon;
    size_t hl;
    memset(u, 0, sizeof *u);
    if (!strncmp(p, "https://", 8)) { u->https = 1; u->port = 443; p += 8; }
    else if (!strncmp(p, "http://", 7)) { u->port = 80; p += 7; }
    else return 0;
    slash = strchr(p, '/');
    hl = slash ? (size_t)(slash - p) : strlen(p);
    if (hl == 0 || hl >= sizeof u->host) return 0;
    memcpy(u->host, p, hl); u->host[hl] = 0;
    colon = strchr(u->host, ':');
    if (colon) { u->port = atoi(colon + 1); *(char *)colon = 0; }
    strncpy(u->path, slash ? slash : "/", sizeof u->path - 1);
    return 1;
}

/* ---- entropy ------------------------------------------------------------------
 * BearSSL's own Windows seeder passes CRYPT_SILENT, which Windows 98 rejects,
 * so the engine is seeded here: the OS generator when it will answer, mixed
 * with timing and machine state, all folded through SHA-256. */
typedef BOOL (WINAPI *acq_fn)(ULONG_PTR *, LPCSTR, LPCSTR, DWORD, DWORD);
typedef BOOL (WINAPI *gen_fn)(ULONG_PTR, DWORD, BYTE *);
typedef BOOL (WINAPI *rel_fn)(ULONG_PTR, DWORD);

static void gather_entropy(unsigned char out[32])
{
    br_sha256_context h;
    unsigned char os[32];
    HMODULE adv = LoadLibraryA("advapi32.dll");
    LARGE_INTEGER pc;
    MEMORYSTATUS ms;
    FILETIME ft;
    POINT pt;
    DWORD v;
    int i;
    br_sha256_init(&h);
    if (adv) {
        acq_fn acq = (acq_fn)GetProcAddress(adv, "CryptAcquireContextA");
        gen_fn gen = (gen_fn)GetProcAddress(adv, "CryptGenRandom");
        rel_fn rel = (rel_fn)GetProcAddress(adv, "CryptReleaseContext");
        ULONG_PTR prov = 0;
        if (acq && gen && rel && (acq(&prov, 0, 0, 1 /* PROV_RSA_FULL */, 0xF0000000u /* VERIFYCONTEXT */) ||
                                  acq(&prov, 0, 0, 1, 0) || acq(&prov, 0, 0, 1, 8 /* NEWKEYSET */))) {
            if (gen(prov, sizeof os, os)) br_sha256_update(&h, os, sizeof os);
            rel(prov, 0);
        }
        FreeLibrary(adv);
    }
    for (i = 0; i < 64; i++) {                 /* timer jitter: cheap, and present on every box */
        QueryPerformanceCounter(&pc); br_sha256_update(&h, &pc, sizeof pc);
        v = GetTickCount(); br_sha256_update(&h, &v, sizeof v);
        Sleep(0);
    }
    GetSystemTimeAsFileTime(&ft); br_sha256_update(&h, &ft, sizeof ft);
    ms.dwLength = sizeof ms; GlobalMemoryStatus(&ms); br_sha256_update(&h, &ms, sizeof ms);
    GetCursorPos(&pt); br_sha256_update(&h, &pt, sizeof pt);
    v = GetCurrentProcessId(); br_sha256_update(&h, &v, sizeof v);
    v = GetCurrentThreadId(); br_sha256_update(&h, &v, sizeof v);
    br_sha256_update(&h, &h, sizeof(void *));
    br_sha256_out(&h, out);
}

/* ---- sockets ---------------------------------------------------------------------- */

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    int n = recv(*(SOCKET *)ctx, (char *)buf, (int)len, 0);
    return n <= 0 ? -1 : n;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    int n = send(*(SOCKET *)ctx, (const char *)buf, (int)len, 0);
    return n <= 0 ? -1 : n;
}

static SOCKET tcp_connect(const char *host, int port, int timeout_ms, char *err, int errcap)
{
    struct hostent *he;
    struct sockaddr_in sa;
    SOCKET s;
    u_long nb = 1;
    fd_set wf, ef;
    struct timeval tv;
    int to = timeout_ms;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((u_short)port);
    sa.sin_addr.s_addr = inet_addr(host);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
        he = gethostbyname(host);
        if (!he || !he->h_addr_list[0]) { _snprintf(err, (size_t)errcap, "cannot resolve %s", host); return INVALID_SOCKET; }
        memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { _snprintf(err, (size_t)errcap, "no socket"); return s; }
    ioctlsocket(s, FIONBIO, &nb);                          /* non-blocking only for a bounded connect */
    connect(s, (struct sockaddr *)&sa, sizeof sa);
    FD_ZERO(&wf); FD_SET(s, &wf); FD_ZERO(&ef); FD_SET(s, &ef);
    tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (select(0, 0, &wf, &ef, &tv) <= 0 || !FD_ISSET(s, &wf)) {
        closesocket(s);
        _snprintf(err, (size_t)errcap, "cannot connect to %s:%d", host, port);
        return INVALID_SOCKET;
    }
    nb = 0; ioctlsocket(s, FIONBIO, &nb);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&to, sizeof to);
    return s;
}

static conn_t *conn_open(const url_t *u, int timeout_ms, char *err, int errcap)
{
    conn_t *c = (conn_t *)calloc(1, sizeof *c);
    unsigned char seed[32];
    if (!c) { _snprintf(err, (size_t)errcap, "out of memory"); return 0; }
    c->s = tcp_connect(u->host, u->port, timeout_ms, err, errcap);
    if (c->s == INVALID_SOCKET) { free(c); return 0; }
    if (!u->https) return c;
    c->tls = 1;
    br_ssl_client_init_full(&c->sc, &c->xc, TAs, TAs_NUM);
    gather_entropy(seed);
    br_ssl_engine_inject_entropy(&c->sc.eng, seed, sizeof seed);
    br_ssl_engine_set_buffer(&c->sc.eng, c->iobuf, sizeof c->iobuf, 1);
    br_ssl_client_reset(&c->sc, u->host, 0);
    br_sslio_init(&c->io, &c->sc.eng, sock_read, &c->s, sock_write, &c->s);
    return c;
}

static int conn_write(conn_t *c, const char *buf, int n)
{
    if (c->tls) return br_sslio_write_all(&c->io, buf, (size_t)n) == 0 ? n : -1;
    while (n > 0) { int k = send(c->s, buf, n, 0); if (k <= 0) return -1; buf += k; n -= k; }
    return 1;
}

static int conn_flush(conn_t *c) { return c->tls ? br_sslio_flush(&c->io) : 0; }

static int conn_read(conn_t *c, char *buf, int n)
{
    if (c->tls) return br_sslio_read(&c->io, buf, (size_t)n);
    return recv(c->s, buf, n, 0);
}

static void conn_close(conn_t *c)
{
    if (!c) return;
    if (c->s != INVALID_SOCKET) closesocket(c->s);
    free(c);
}

static const char *tls_error(conn_t *c, char *buf, int cap)
{
    int e = br_ssl_engine_last_error(&c->sc.eng);
    const char *why = e == BR_ERR_X509_EXPIRED ? "certificate expired or not yet valid - check this PC's date" :
                      e == BR_ERR_X509_NOT_TRUSTED ? "certificate not trusted" :
                      e == BR_ERR_X509_BAD_SERVER_NAME ? "certificate name mismatch" :
                      e == BR_ERR_NO_RANDOM ? "no entropy" : e == BR_ERR_IO ? "connection lost" : 0;
    if (why) _snprintf(buf, (size_t)cap, "TLS: %s", why); else _snprintf(buf, (size_t)cap, "TLS error %d", e);
    return buf;
}

/* ---- HTTP -------------------------------------------------------------------------- */

static int send_request_v(conn_t *c, const url_t *u, const char *method, const char *headers, const char *body, const char *extra, const char *ver)
{
    char req[3072];
    int blen = body ? (int)strlen(body) : 0, n;
    n = _snprintf(req, sizeof req,
                  "%s %s HTTP/%s\r\nHost: %s\r\nUser-Agent: EasyAmp-retro\r\nAccept: application/json\r\nConnection: close\r\n%s%s",
                  method, u->path, ver, u->host, headers ? headers : "", extra ? extra : "");
    if (n < 0 || n >= (int)sizeof req - 64) return -1;
    if (body || !strcmp(method, "POST")) n += sprintf(req + n, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n", blen);
    n += sprintf(req + n, "\r\n");
    if (conn_write(c, req, n) < 0) return -1;
    if (blen && conn_write(c, body, blen) < 0) return -1;
    return conn_flush(c);
}

static int send_request(conn_t *c, const url_t *u, const char *method, const char *headers, const char *body, const char *extra)
{
    return send_request_v(c, u, method, headers, body, extra, "1.1");
}

/* undo "Transfer-Encoding: chunked" in place; returns the new length */
static int dechunk(char *b, int len)
{
    int in = 0, out = 0;
    while (in < len) {
        long sz = strtol(b + in, 0, 16);
        char *eol = strstr(b + in, "\r\n");
        if (!eol || sz <= 0) break;
        in = (int)(eol - b) + 2;
        if (in + sz > len) sz = len - in;
        memmove(b + out, b + in, (size_t)sz);
        out += (int)sz; in += (int)sz + 2;
    }
    b[out] = 0;
    return out;
}

int net_request(const char *method, const char *url, const char *headers, const char *body, ea_http *out, int timeout_ms)
{
    url_t u;
    conn_t *c;
    char *buf = 0, *hdr_end;
    int cap = 0, len = 0, n;
    memset(out, 0, sizeof *out);
    if (!net_init()) { strcpy(out->err, "winsock unavailable"); return 0; }
    if (!parse_url(url, &u)) { strcpy(out->err, "bad url"); return 0; }
    c = conn_open(&u, timeout_ms, out->err, (int)sizeof out->err);
    if (!c) return 0;
    if (send_request(c, &u, method, headers, body, 0) < 0) {
        if (c->tls) tls_error(c, out->err, (int)sizeof out->err); else strcpy(out->err, "send failed");
        conn_close(c); return 0;
    }
    for (;;) {
        if (len + 4096 + 1 > cap) { char *nb; cap = cap ? cap * 2 : 16384; nb = (char *)realloc(buf, (size_t)cap); if (!nb) break; buf = nb; }
        n = conn_read(c, buf + len, 4096);
        if (n <= 0) break;
        len += n;
        if (len > 8 * 1024 * 1024) break;                  /* nothing we ask for is this big */
    }
    if (!buf || len < 12) {
        if (c->tls && br_ssl_engine_last_error(&c->sc.eng)) tls_error(c, out->err, (int)sizeof out->err);
        else strcpy(out->err, "no response");
        conn_close(c); free(buf); return 0;
    }
    conn_close(c);
    buf[len] = 0;
    out->status = atoi(buf + 9);
    hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end) { strcpy(out->err, "malformed response"); free(buf); out->status = 0; return 0; }
    {
        int chunked = 0, blen;
        char *p;
        *hdr_end = 0;
        for (p = buf; *p; p++) *p = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
        chunked = strstr(buf, "transfer-encoding: chunked") != 0;
        blen = len - (int)(hdr_end + 4 - buf);
        memmove(buf, hdr_end + 4, (size_t)blen);
        buf[blen] = 0;
        out->len = chunked ? dechunk(buf, blen) : blen;
        out->body = buf;
    }
    return out->status;
}

/* ---- playback stream (plain HTTP on the LAN) -------------------------------------- */

struct ea_stream { conn_t *c; char pend[8192]; int pend_len, pend_pos; };

ea_stream *net_stream_open(const char *url, long offset, long *total_len, char *err, int errcap)
{
    url_t u;
    ea_stream *s;
    char range[64], head[8192], *end, *p;
    int len = 0, n, status;
    if (total_len) *total_len = 0;
    if (!net_init() || !parse_url(url, &u) || u.https) { _snprintf(err, (size_t)errcap, "stream needs a plain http url"); return 0; }
    s = (ea_stream *)calloc(1, sizeof *s);
    if (!s) return 0;
    s->c = conn_open(&u, 8000, err, errcap);
    if (!s->c) { free(s); return 0; }
    range[0] = 0;
    if (offset > 0) sprintf(range, "Range: bytes=%ld-\r\n", offset);
    /* HTTP/1.0: the body must then arrive un-chunked, so the decoder can read it raw */
    if (send_request_v(s->c, &u, "GET", 0, 0, range, "1.0") < 0) { _snprintf(err, (size_t)errcap, "send failed"); net_stream_close(s); return 0; }
    for (;;) {                                              /* read just past the header block */
        n = conn_read(s->c, head + len, (int)sizeof head - 1 - len);
        if (n <= 0) { _snprintf(err, (size_t)errcap, "no response"); net_stream_close(s); return 0; }
        len += n; head[len] = 0;
        if ((end = strstr(head, "\r\n\r\n")) != 0) break;
        if (len >= (int)sizeof head - 1) { _snprintf(err, (size_t)errcap, "header too large"); net_stream_close(s); return 0; }
    }
    status = atoi(head + 9);
    if (status != 200 && status != 206) { _snprintf(err, (size_t)errcap, "server said %d", status); net_stream_close(s); return 0; }
    s->pend_len = len - (int)(end + 4 - head);
    memcpy(s->pend, end + 4, (size_t)s->pend_len);
    *end = 0;
    for (p = head; *p; p++) *p = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
    if (total_len) {
        if ((p = strstr(head, "content-range:")) != 0 && (p = strchr(p, '/')) != 0) *total_len = atol(p + 1);
        else if ((p = strstr(head, "content-length:")) != 0) *total_len = atol(p + 15) + offset;
    }
    return s;
}

int net_stream_read(ea_stream *s, void *buf, int n)
{
    if (s->pend_pos < s->pend_len) {
        int k = s->pend_len - s->pend_pos < n ? s->pend_len - s->pend_pos : n;
        memcpy(buf, s->pend + s->pend_pos, (size_t)k);
        s->pend_pos += k;
        return k;
    }
    n = conn_read(s->c, (char *)buf, n);
    return n < 0 ? -1 : n;
}

void net_stream_close(ea_stream *s) { if (s) { conn_close(s->c); free(s); } }
