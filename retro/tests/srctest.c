/* srctest - sign in to a Jellyfin server and walk it, from a console.
 *   SRCTEST.EXE <server> <user> <password> */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/source.h"

int main(int argc, char **argv)
{
    ea_source s;
    ea_sitem *it;
    char err[200] = "", node[96] = "", url[900];
    int n, i, depth;
    if (argc < 4) { printf("usage: SRCTEST server user password\n"); return 2; }
    if (!jellyfin_sign_in(argv[1], "easyamp-retro-srctest", argv[2], argv[3], &s, err, sizeof err)) { printf("SIGN-IN FAILED: %s\n", err); return 1; }
    printf("signed in: '%s' at %s  user %s  alive=%d\n", s.name, s.base, s.user_id, source_alive(&s, "easyamp-retro-srctest"));
    for (depth = 0; depth < 5; depth++) {
        it = source_browse(&s, "easyamp-retro-srctest", node, &n, err, sizeof err);
        if (!it) { printf("BROWSE FAILED at '%s': %s\n", node, err); return 1; }
        printf("'%s': %d items\n", node, n);
        for (i = 0; i < n && i < 12; i++) printf("    %s%s  %s\n", it[i].name, it[i].is_track ? "" : "  >", it[i].is_track ? "" : it[i].node);
        if (!n) break;
        if (it[0].is_track) {
            source_auth_url(&s, it[n - 1].url, url, sizeof url);
            printf("last track (%d s): %.90s...\n", it[n - 1].dur_s, it[n - 1].url);
            { FILE *f = fopen("srctest-urls.txt", "w"); if (f) { source_auth_url(&s, it[0].url, url, sizeof url); fprintf(f, "%s\n", url); source_auth_url(&s, it[n - 1].url, url, sizeof url); fprintf(f, "%s\n", url); fclose(f); } }
            free(it); break;
        }
        strcpy(node, it[0].node);
        free(it);
    }
    return 0;
}
