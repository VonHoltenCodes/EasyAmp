/* plextest - drive the whole Plex flow from a console, against real servers.
 *   PLEXTEST.EXE link              -> prints a code, waits for approval, saves PLEXTEST.INI
 *   PLEXTEST.EXE walk              -> discover, then sections/artists/albums/tracks (first of each)
 * The token is kept in PLEXTEST.INI beside the exe so `walk` can be re-run. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/plex.h"
#include "../src/net.h"

static char INI[MAX_PATH];

int main(int argc, char **argv)
{
    char err[200] = "", client[64], token[80] = "", code[8];
    long pin;
    int i, n;
    GetCurrentDirectoryA(MAX_PATH - 16, INI); strcat(INI, "\\PLEXTEST.INI");
    GetPrivateProfileStringA("plex", "client", "", client, sizeof client, INI);
    if (!client[0]) { sprintf(client, "easyamp-retro-%08lx%04x", (unsigned long)GetTickCount(), (unsigned)(GetCurrentProcessId() & 0xffff)); WritePrivateProfileStringA("plex", "client", client, INI); }
    if (argc > 1 && !strcmp(argv[1], "link")) {
        if (!plex_pin_start(client, &pin, code, err, sizeof err)) { printf("ERROR %s\n", err); return 1; }
        printf("CODE %s\n", code); fflush(stdout);
        for (i = 0; i < 450; i++) {                          /* up to 15 minutes */
            int r = plex_pin_poll(client, pin, token, sizeof token, err, sizeof err);
            if (r == 1) { WritePrivateProfileStringA("plex", "token", token, INI); printf("LINKED\n"); return 0; }
            if (r < 0) { printf("ERROR %s\n", err); return 1; }
            Sleep(2000);
        }
        printf("ERROR timed out\n");
        return 1;
    }
    GetPrivateProfileStringA("plex", "token", "", token, sizeof token, INI);
    if (!token[0]) { printf("not linked: run `PLEXTEST.EXE link` first\n"); return 1; }
    {
        plex_server sv[4];
        plex_item *it;
        char node[64] = "", url[512];
        static const char *kinds[] = { "section", "artist", "album", "track" };
        int ns = plex_discover(client, token, sv, 4, err, sizeof err), depth;
        DWORD t0;
        if (!ns) { printf("DISCOVER FAILED: %s\n", err); return 1; }
        for (i = 0; i < ns; i++) printf("server %d: %s  %s\n", i, sv[i].name, sv[i].base);
        for (depth = 0; depth < 4; depth++) {
            t0 = GetTickCount();
            it = plex_browse(&sv[0], client, node, &n, err, sizeof err);
            if (!it) { printf("BROWSE FAILED at '%s': %s\n", node, err); return 1; }
            printf("%-8s '%s': %d items in %lu ms\n", kinds[depth], node, n, (unsigned long)(GetTickCount() - t0));
            for (i = 0; i < n && i < 5; i++) printf("    %s%s%s\n", it[i].name, it[i].codec[0] ? "   [" : "", it[i].codec[0] ? it[i].codec : "");
            if (!n) { free(it); break; }
            if (it[0].kind == PLEX_TRACK) { plex_track_url(&sv[0], &it[0], url, sizeof url); printf("first track url: %s (+token)  %d s\n", url, it[0].dur_s); free(it); break; }
            sprintf(node, "%s/%s", kinds[it[0].kind], it[0].key);
            free(it);
        }
    }
    return 0;
}
