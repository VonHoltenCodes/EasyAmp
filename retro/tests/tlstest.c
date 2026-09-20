/* tlstest - the make-or-break check for Plex on Windows 98: can this machine
 * complete a TLS 1.2 handshake with plex.tv and get a link code back?
 * Writes TLSTEST.TXT beside itself. Given an upload URL it also posts the
 * report there as a multipart form, so nobody has to read a console on the
 * old machine:   TLSTEST.EXE http://192.168.1.10:8089/ */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/net.h"

int main(int argc, char **argv)
{
    static char report[4096], body[5000];
    char diag[160], host[64] = "unknown", head[128];
    DWORD hl = sizeof host, t0, t1;
    OSVERSIONINFOA os;
    SYSTEMTIME now;
    ea_http r, up;
    FILE *f;
    int st, n = 0, pass;

    GetComputerNameA(host, &hl);
    os.dwOSVersionInfoSize = sizeof os; GetVersionExA(&os);
    GetLocalTime(&now);
    t0 = GetTickCount();
    st = net_request("POST", "https://plex.tv/api/v2/pins?strong=false",
                     "X-Plex-Product: EasyAmp\r\nX-Plex-Client-Identifier: easyamp-retro-tlstest\r\n", "", &r, 30000);
    t1 = GetTickCount();
    net_diag(diag, sizeof diag);
    pass = st == 201 && r.body && strstr(r.body, "\"code\"");

    n += sprintf(report + n, "machine : %s  (Windows %lu.%lu build %lu %s)\r\n", host, os.dwMajorVersion, os.dwMinorVersion,
                 os.dwBuildNumber & 0xffff, os.szCSDVersion);
    n += sprintf(report + n, "clock   : %04d-%02d-%02d %02d:%02d local\r\n", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    n += sprintf(report + n, "status  : %d\r\n", st);
    n += sprintf(report + n, "time    : %lu ms (resolve + connect + TLS 1.2 handshake + request)\r\n", (unsigned long)(t1 - t0));
    n += sprintf(report + n, "net     : %s\r\n", diag);
    if (st) n += sprintf(report + n, "body    : %.24s... (%d bytes)\r\n", r.body, r.len);      /* not the code itself */
    else n += sprintf(report + n, "error   : %s\r\n", r.err);
    n += sprintf(report + n, "verdict : %s\r\n", pass ? "PASS - TLS 1.2 to plex.tv works on this machine" : "FAIL");
    net_free(&r);

    printf("%s", report);
    f = fopen("TLSTEST.TXT", "w");
    if (f) { fputs(report, f); fclose(f); }

    sprintf(body, "--EAB\r\nContent-Disposition: form-data; name=\"f\"; filename=\"TLSTEST-%s.TXT\"\r\nContent-Type: text/plain\r\n\r\n%s\r\n--EAB--\r\n", host, report);
    strcpy(head, "Content-Type: multipart/form-data; boundary=EAB\r\n");
    if (argc > 1) {
        printf("report %s\n", net_request("POST", argv[1], head, body, &up, 6000) == 200 ? "uploaded" : "NOT uploaded - see TLSTEST.TXT");
        net_free(&up);
    }
    Sleep(2500);                                   /* leave the console readable for a moment */
    return pass ? 0 : 1;
}
