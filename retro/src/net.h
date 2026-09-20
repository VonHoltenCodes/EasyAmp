/* net - a small blocking HTTP/HTTPS client for Windows 98 SE and XP.
 *
 * Neither OS can speak TLS 1.2, which plex.tv requires, so HTTPS goes through
 * BearSSL (pure C, no OS crypto). Plain HTTP is used for everything on the
 * LAN. Calls block: run them on a worker thread, never the UI thread.
 */
#ifndef EA_NET_H
#define EA_NET_H

typedef struct {
    int   status;            /* HTTP status, or 0 when the request never completed */
    char *body;              /* malloc'd, NUL-terminated; caller frees with net_free */
    int   len;
    char  err[128];          /* human-readable reason when status == 0 */
} ea_http;

int  net_init(void);                                   /* WSAStartup; 1 on success */
void net_shutdown(void);
/* headers: "Name: value\r\n" lines, or NULL. body: NULL for GET. Returns status. */
int  net_request(const char *method, const char *url, const char *headers,
                 const char *body, ea_http *out, int timeout_ms);
void net_free(ea_http *r);
/* one line for diagnostics: how names resolved, whether the OS RNG answered */
void net_diag(char *out, int cap);

/* a byte stream over HTTP or HTTPS, for playback; follows redirects.
 * Range-capable servers can seek. */
typedef struct ea_stream ea_stream;
ea_stream *net_stream_open(const char *url, long offset, long *total_len, char *err, int errcap);
int        net_stream_read(ea_stream *s, void *buf, int n);   /* 0 = end, <0 = error */
void       net_stream_close(ea_stream *s);

#endif
