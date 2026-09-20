/* tlstest - the make-or-break check for Plex on Windows 98: can this machine
 * complete a TLS 1.2 handshake with plex.tv and get a link code back?
 * Console program; writes TLSTEST.TXT next to itself as well as stdout. */
#include <windows.h>
#include <stdio.h>
#include "../src/net.h"

int main(void)
{
    ea_http r;
    FILE *log = fopen("TLSTEST.TXT", "w");
    DWORD t0 = GetTickCount(), t1;
    int st = net_request("POST", "https://plex.tv/api/v2/pins?strong=false",
                         "X-Plex-Product: EasyAmp\r\nX-Plex-Client-Identifier: easyamp-retro-tlstest\r\n", "", &r, 20000);
    t1 = GetTickCount();
#define SAY(...) do { printf(__VA_ARGS__); if (log) fprintf(log, __VA_ARGS__); } while (0)
    SAY("status  : %d\n", st);
    SAY("time    : %lu ms (connect + TLS handshake + request)\n", (unsigned long)(t1 - t0));
    if (st) SAY("body    : %.400s\n", r.body); else SAY("error   : %s\n", r.err);
    SAY("verdict : %s\n", st == 201 && r.body && strstr(r.body, "\"code\"") ? "PASS - TLS 1.2 to plex.tv works on this machine" : "FAIL");
    if (log) fclose(log);
    net_free(&r);
    return st == 201 ? 0 : 1;
}
