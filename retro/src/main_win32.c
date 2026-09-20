/* EasyAmp retro - the Win32 shell. One ANSI executable for Windows 98 SE
 * through XP: a borderless self-drawn window, the playlist, file dialogs and
 * the glue between the UI (ui.c) and the audio engine (engine_win32.c). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "net.h"
#include "plex.h"
#include "source.h"
#include "ui.h"

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif
#define OFN_SIZE_V400 76                 /* Windows 98 rejects the larger NT5 struct */
#define TIMER_ID 1
#define FRAME_MS 33

typedef struct { char path[640]; } item;      /* a file, or a token-free http:// URL on the Plex server */

/* how the finished picture reaches the screen, by desktop colour depth */
enum { PRESENT_DIRECT,      /* 24/32-bit: as drawn */
       PRESENT_HICOLOR,     /* 15/16-bit: our own ordered dither (GDI would truncate) */
       PRESENT_PAL256,      /* 256 colours: our palette, tuned to the skin, + dither */
       PRESENT_VGA16 };     /* 16 colours: dither into the fixed VGA palette */

static HWND       g_wnd;
static HDC        g_memdc;
static HBITMAP    g_dib, g_olddib;
static HPALETTE   g_pal;
static void      *g_dibbits;
static int        g_present, g_dib_stride, g_green_bits, g_force_depth;
static ea_model   g_m;
static ea_ui     *g_ui;
static ea_engine *g_eng;
static item      *g_items;
static int        g_cap;
static DWORD      g_last_tick;
static char       g_shot[MAX_PATH];
static DWORD      g_shot_at;

/* ---- playlist ------------------------------------------------------------------ */

static void id3_field(const unsigned char *p, int n, char *out, int cap)
{
    int i, o = 0;
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == 0)) n--;
    for (i = 0; i < n && o < cap - 1; i++) if (p[i] >= 32) out[o++] = (char)(p[i] < 127 ? p[i] : '?');
    out[o] = 0;
}

/* "Artist - Title" from an ID3v2 (TPE1/TIT2) or ID3v1 tag, else the file name */
static void read_title(const char *path, char *out, int cap)
{
    char artist[64] = "", title[96] = "";
    const char *base = strrchr(path, '\\'), *dot;
    FILE *f = fopen(path, "rb");
    if (f) {
        unsigned char h[10];
        if (fread(h, 1, 10, f) == 10 && !memcmp(h, "ID3", 3) && h[3] >= 3) {
            long size = ((long)h[6] & 127) << 21 | ((long)h[7] & 127) << 14 | ((long)h[8] & 127) << 7 | ((long)h[9] & 127), pos = 0;
            while (pos + 10 < size && pos < 65536 && !(artist[0] && title[0])) {
                unsigned char fh[10], buf[128];
                long fs;
                int take, k, o;
                char *dst;
                if (fread(fh, 1, 10, f) != 10 || fh[0] == 0) break;
                fs = h[3] == 4 ? (((long)fh[4] & 127) << 21 | ((long)fh[5] & 127) << 14 | ((long)fh[6] & 127) << 7 | ((long)fh[7] & 127))
                               : ((long)fh[4] << 24 | (long)fh[5] << 16 | (long)fh[6] << 8 | (long)fh[7]);
                if (fs <= 0 || fs > size) break;
                dst = !memcmp(fh, "TPE1", 4) ? artist : !memcmp(fh, "TIT2", 4) ? title : 0;
                take = fs > (long)sizeof buf ? (int)sizeof buf : (int)fs;
                if (dst && fread(buf, 1, (size_t)take, f) == (size_t)take) {
                    int capd = dst == artist ? (int)sizeof artist : (int)sizeof title, wide = buf[0] == 1 || buf[0] == 2;
                    for (k = 1, o = 0; k < take && o < capd - 1; k += wide ? 2 : 1) {
                        unsigned char c = buf[k], hi = wide && k + 1 < take ? buf[k + 1] : 0;
                        if (wide && buf[0] == 2) { unsigned char t = c; c = hi; hi = t; }
                        if (wide && ((c == 0xff && hi == 0xfe) || (c == 0xfe && hi == 0xff))) continue;   /* BOM */
                        if (c >= 32 && !hi) dst[o++] = (char)(c < 127 ? c : '?');
                        else if (hi) dst[o++] = '?';
                    }
                    dst[o] = 0;
                    fseek(f, fs - take, SEEK_CUR);
                } else fseek(f, dst ? fs - take : fs, SEEK_CUR);
                pos += 10 + fs;
            }
        }
        if (!title[0] && !fseek(f, -128, SEEK_END)) {
            unsigned char t[128];
            if (fread(t, 1, 128, f) == 128 && !memcmp(t, "TAG", 3)) { id3_field(t + 3, 30, title, sizeof title); id3_field(t + 33, 30, artist, sizeof artist); }
        }
        fclose(f);
    }
    if (title[0]) {
        if (artist[0]) _snprintf(out, (size_t)cap, "%s - %s", artist, title); else _snprintf(out, (size_t)cap, "%s", title);
        out[cap - 1] = 0;
        return;
    }
    base = base ? base + 1 : path;
    strncpy(out, base, (size_t)cap - 1); out[cap - 1] = 0;
    dot = strrchr(out, '.');
    if (dot) *(char *)dot = 0;
}

static int is_url(const char *path) { return !strncmp(path, "http://", 7) || !strncmp(path, "https://", 8); }

static int playable(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (is_url(path)) return 1;
    return dot && (!lstrcmpiA(dot, ".mp3") || !lstrcmpiA(dot, ".mp2") || !lstrcmpiA(dot, ".wav") ||
                   !lstrcmpiA(dot, ".flac") || !lstrcmpiA(dot, ".ogg") || !lstrcmpiA(dot, ".oga"));
}

static void pl_add(const char *given)
{
    char full[MAX_PATH], *part;
    const char *path = given;
    /* store files by absolute path: the saved playlist must still work when
     * EasyAmp is next started from somewhere else */
    if (!is_url(given) && GetFullPathNameA(given, MAX_PATH, full, &part) > 0) path = full;
    if (!playable(path)) return;
    if (g_m.ntracks == g_cap) {
        int ncap = g_cap ? g_cap * 2 : 64;
        item *ni = (item *)realloc(g_items, sizeof(item) * (size_t)ncap);
        ea_track *nt = (ea_track *)realloc(g_m.tracks, sizeof(ea_track) * (size_t)ncap);
        if (ni) g_items = ni;
        if (nt) g_m.tracks = nt;
        if (!ni || !nt) return;
        g_cap = ncap;
    }
    strncpy(g_items[g_m.ntracks].path, path, sizeof g_items[0].path - 1); g_items[g_m.ntracks].path[sizeof g_items[0].path - 1] = 0;
    memset(&g_m.tracks[g_m.ntracks], 0, sizeof(ea_track));
    if (!is_url(path)) read_title(path, g_m.tracks[g_m.ntracks].title, (int)sizeof g_m.tracks[0].title);
    else strcpy(g_m.tracks[g_m.ntracks].title, "Stream");
    g_m.ntracks++;
}

static void pl_add_named(const char *path, const char *title, int dur_s)
{
    int before = g_m.ntracks;
    pl_add(path);
    if (g_m.ntracks == before) return;
    strncpy(g_m.tracks[before].title, title, sizeof g_m.tracks[0].title - 1);
    g_m.tracks[before].dur_s = dur_s;
}

static void plex_play_url(const char *stored, char *out, int cap);

static void pl_clear(void) { g_m.ntracks = 0; g_m.sel = g_m.cur = -1; }

static void pl_remove(int i)
{
    if (i < 0 || i >= g_m.ntracks) return;
    memmove(&g_items[i], &g_items[i + 1], sizeof(item) * (size_t)(g_m.ntracks - i - 1));
    memmove(&g_m.tracks[i], &g_m.tracks[i + 1], sizeof(ea_track) * (size_t)(g_m.ntracks - i - 1));
    g_m.ntracks--;
    if (g_m.cur == i) g_m.cur = -1; else if (g_m.cur > i) g_m.cur--;
    if (g_m.sel >= g_m.ntracks) g_m.sel = g_m.ntracks - 1;
}

static void play_index(void *ctx, int i)
{
    (void)ctx;
    if (i < 0 || i >= g_m.ntracks) return;
    g_m.cur = g_m.sel = i;
    strncpy(g_m.title, g_m.tracks[i].title, sizeof g_m.title - 1);
    {
        char url[900];
        plex_play_url(g_items[i].path, url, (int)sizeof url);     /* adds the token for our server's URLs */
        eng_open(g_eng, url, g_m.tracks[i].dur_s * 1000);
    }
    ui_model_changed(g_ui, UI_CH_TITLE | UI_CH_PLAYLIST | UI_CH_TRANSPORT);
}

static void m3u_load(const char *path)
{
    char line[1024], dir[MAX_PATH], full[MAX_PATH], *slash;
    FILE *f = fopen(path, "r");
    if (!f) return;
    strncpy(dir, path, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
    slash = strrchr(dir, '\\');
    if (slash) slash[1] = 0; else dir[0] = 0;
    char title[200] = "";
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        char *comma;
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (!strncmp(line, "#EXTINF:", 8) && (comma = strchr(line, ',')) != 0) { strncpy(title, comma + 1, sizeof title - 1); title[sizeof title - 1] = 0; }
        if (!n || line[0] == '#') continue;
        if (is_url(line)) { pl_add_named(line, title[0] ? title : "Stream", 0); title[0] = 0; continue; }
        title[0] = 0;
        if (line[1] == ':' || line[0] == '\\') strncpy(full, line, MAX_PATH - 1);       /* absolute */
        else _snprintf(full, MAX_PATH, "%s%s", dir, line);
        full[MAX_PATH - 1] = 0;
        pl_add(full);
    }
    fclose(f);
}

static void m3u_save(const char *path)
{
    FILE *f = fopen(path, "w");
    int i;
    if (!f) return;
    fprintf(f, "#EXTM3U\n");
    for (i = 0; i < g_m.ntracks; i++) fprintf(f, "#EXTINF:-1,%s\n%s\n", g_m.tracks[i].title, g_items[i].path);
    fclose(f);
}

/* ---- sources (Plex, Jellyfin) ------------------------------------------------------------
 * Network calls block (a TLS handshake, a slow server), so each one runs as a
 * job on a worker thread; frame() picks the result up. One job at a time.
 * Accounts live in EASYAMP.INI beside the exe, tokens in plain text: Windows
 * 98 has no protected store to put them in. Passwords are never saved. */

enum { JOB_NONE, JOB_LINK, JOB_SIGNIN, JOB_RECONNECT, JOB_BROWSE, JOB_COLLECT };

typedef struct {
    int kind;
    volatile LONG done, cancel, code_ready;
    HANDLE thread;
    /* in */
    ea_source src;                                  /* a COPY: the worker never touches g_src */
    char node[96], field[3][128];
    int then_play;
    ea_sitem *targets;                              /* COLLECT: the rows to gather, in list order (owned by the job) */
    int ntargets;
    /* out */
    char code[8], status[96], err[200];
    ea_source result;
    int ok, alive[EA_MAX_SOURCES];
    ea_sitem *items;
    int nitems;
} job_t;

#define MAX_DEPTH 6
#define COLLECT_CAP 3000
static job_t     g_job;
static char      g_ini[MAX_PATH], g_client[64];
static ea_source g_src[EA_MAX_SOURCES];
static int       g_nsrc;
static ea_sitem *g_lib;                             /* the level on screen */
static int       g_nlib, g_depth;
static char      g_nodes[MAX_DEPTH][96], g_names[MAX_DEPTH][128];
static int       g_script[8], g_script_n, g_script_pos;   /* /open:1,0,2 - rows to open as levels load (testing) */
static int       g_then;                                  /* /then:add|addall|play - press it when the /open script ends (testing) */
static int       g_want_acct = -1;                        /* /acct:N - which saved account to open at startup (testing) */
static char      g_auto_jf[3][128];                       /* /jf:server,user,pass - sign in at startup (testing) */

static void on_src_open(void *ctx, int idx);
static void on_command(void *ctx, int cmd);

static void ini_path(void)
{
    char *slash;
    GetModuleFileNameA(0, g_ini, MAX_PATH - 12);
    slash = strrchr(g_ini, '\\');
    strcpy(slash ? slash + 1 : g_ini, "EASYAMP.INI");
}

static void accounts_to_model(void)
{
    int i;
    g_m.naccts = g_nsrc;
    for (i = 0; i < g_nsrc; i++) _snprintf(g_m.accts[i].name, sizeof g_m.accts[i].name, "%s  %s", source_type_name(g_src[i].type), g_src[i].name);
    if (g_m.acct_sel >= g_nsrc) g_m.acct_sel = g_nsrc - 1;
    if (g_m.acct_sel < 0 && g_nsrc) g_m.acct_sel = 0;
}

static void sources_save(void)
{
    char sec[16], num[8];
    int i;
    for (i = 0; i < EA_MAX_SOURCES; i++) {
        sprintf(sec, "source%d", i);
        WritePrivateProfileStringA(sec, 0, 0, g_ini);                       /* drop the section, then rewrite it */
        if (i >= g_nsrc) continue;
        sprintf(num, "%d", g_src[i].type);
        WritePrivateProfileStringA(sec, "type", num, g_ini);
        WritePrivateProfileStringA(sec, "name", g_src[i].name, g_ini);
        WritePrivateProfileStringA(sec, "base", g_src[i].base, g_ini);
        WritePrivateProfileStringA(sec, "token", g_src[i].token, g_ini);
        WritePrivateProfileStringA(sec, "acct", g_src[i].acct, g_ini);
        WritePrivateProfileStringA(sec, "user_id", g_src[i].user_id, g_ini);
    }
}

static void sources_load(void)
{
    char sec[16];
    int i;
    ini_path();
    GetPrivateProfileStringA("plex", "client", "", g_client, sizeof g_client, g_ini);
    if (!g_client[0]) {
        sprintf(g_client, "easyamp-retro-%08lx%04x", (unsigned long)GetTickCount(), (unsigned)(GetCurrentProcessId() & 0xffff));
        WritePrivateProfileStringA("plex", "client", g_client, g_ini);
    }
    for (i = 0; i < EA_MAX_SOURCES; i++) {
        ea_source *c = &g_src[g_nsrc];
        sprintf(sec, "source%d", i);
        memset(c, 0, sizeof *c);
        GetPrivateProfileStringA(sec, "base", "", c->base, sizeof c->base, g_ini);
        GetPrivateProfileStringA(sec, "token", "", c->token, sizeof c->token, g_ini);
        if (!c->base[0] || !c->token[0]) continue;
        c->type = (int)GetPrivateProfileIntA(sec, "type", 0, g_ini);
        GetPrivateProfileStringA(sec, "name", "server", c->name, sizeof c->name, g_ini);
        GetPrivateProfileStringA(sec, "acct", "", c->acct, sizeof c->acct, g_ini);
        GetPrivateProfileStringA(sec, "user_id", "", c->user_id, sizeof c->user_id, g_ini);
        g_nsrc++;
    }
    if (!g_nsrc) {                                                       /* a 0.2.0 / 0.2.1 link lived in [plex] */
        ea_source *c = &g_src[0];
        memset(c, 0, sizeof *c);
        GetPrivateProfileStringA("plex", "server_base", "", c->base, sizeof c->base, g_ini);
        GetPrivateProfileStringA("plex", "server_token", "", c->token, sizeof c->token, g_ini);
        GetPrivateProfileStringA("plex", "token", "", c->acct, sizeof c->acct, g_ini);
        GetPrivateProfileStringA("plex", "server_name", "server", c->name, sizeof c->name, g_ini);
        if (c->base[0] && c->token[0]) { c->type = SOURCE_PLEX; g_nsrc = 1; sources_save(); }
    }
    accounts_to_model();
}

/* the stored playlist URL carries no credentials; add them for the server that owns it */
static void plex_play_url(const char *stored, char *out, int cap)
{
    int i;
    for (i = 0; i < g_nsrc; i++)
        if (source_owns_url(&g_src[i], stored) && !strstr(stored, "X-Plex-Token=") && !strstr(stored, "api_key=")) { source_auth_url(&g_src[i], stored, out, cap); return; }
    strncpy(out, stored, (size_t)cap - 1); out[cap - 1] = 0;
}

static void append_items(job_t *j, const ea_sitem *more, int n)
{
    ea_sitem *grown = (ea_sitem *)realloc(j->items, sizeof(ea_sitem) * (size_t)(j->nitems + (n > 0 ? n : 1)));
    if (!grown) return;
    j->items = grown;
    if (n > 0) { memcpy(grown + j->nitems, more, sizeof(ea_sitem) * (size_t)n); j->nitems += n; }
}

/* every track under a folder, whatever the server calls its levels */
static void collect_node(job_t *j, const char *node, int depth)
{
    ea_sitem *it;
    int n = 0, i;
    if (depth > 4 || j->cancel || j->nitems >= COLLECT_CAP) return;
    it = source_browse(&j->src, g_client, node, &n, j->err, (int)sizeof j->err);
    if (!it) return;
    j->ok = 1;
    for (i = 0; i < n && j->nitems < COLLECT_CAP; i++) {
        if (it[i].is_track) append_items(j, &it[i], 1);
        else collect_node(j, it[i].node, depth + 1);
    }
    free(it);
}

static DWORD WINAPI job_thread(LPVOID arg)
{
    job_t *j = (job_t *)arg;
    long pin = 0;
    int i;
    switch (j->kind) {
    case JOB_LINK: {
        char token[96] = "";
        plex_server ps;
        strcpy(j->status, "CONTACTING PLEX.TV...");
        if (!plex_pin_start(g_client, &pin, j->code, j->err, sizeof j->err)) break;
        strcpy(j->status, "WAITING FOR APPROVAL...");
        InterlockedExchange(&j->code_ready, 1);
        for (i = 0; i < 450 && !j->cancel; i++) {               /* codes last about 15 minutes */
            int r = plex_pin_poll(g_client, pin, token, sizeof token, j->err, sizeof j->err), k;
            if (r < 0) break;
            if (r == 1) {
                strcpy(j->status, "LINKED - FINDING YOUR SERVER...");
                if (plex_discover(g_client, token, &ps, 1, j->err, sizeof j->err) > 0) {
                    memset(&j->result, 0, sizeof j->result);
                    j->result.type = SOURCE_PLEX;
                    strncpy(j->result.name, ps.name, sizeof j->result.name - 1);
                    strncpy(j->result.base, ps.base, sizeof j->result.base - 1);
                    strncpy(j->result.token, ps.token, sizeof j->result.token - 1);
                    strncpy(j->result.acct, token, sizeof j->result.acct - 1);
                    j->ok = 1;
                }
                break;
            }
            for (k = 0; k < 20 && !j->cancel; k++) Sleep(100);
        }
        if (!j->ok && !j->err[0] && !j->cancel) strcpy(j->err, "code expired - try again");
        break; }
    case JOB_SIGNIN:
        j->ok = jellyfin_sign_in(j->field[0], g_client, j->field[1], j->field[2], &j->result, j->err, sizeof j->err);
        memset(j->field[2], 0, sizeof j->field[2]);              /* the password is not kept anywhere */
        break;
    case JOB_RECONNECT:                                          /* startup: which saved servers still answer? */
        for (i = 0; i < g_nsrc && !j->cancel; i++) j->alive[i] = source_alive(&g_src[i], g_client);
        j->ok = 1;
        break;
    case JOB_BROWSE:
        j->items = source_browse(&j->src, g_client, j->node, &j->nitems, j->err, sizeof j->err);
        j->ok = j->items != 0;
        break;
    case JOB_COLLECT:
        for (i = 0; i < j->ntargets && !j->cancel && j->nitems < COLLECT_CAP; i++) {
            if (j->targets[i].is_track) { append_items(j, &j->targets[i], 1); j->ok = 1; }
            else collect_node(j, j->targets[i].node, 0);
        }
        break;
    }
    InterlockedExchange(&j->done, 1);
    return 0;
}

static int job_start(int kind)
{
    DWORD tid;
    job_t keep;
    if (g_job.kind != JOB_NONE) return 0;                        /* one at a time */
    keep = g_job;
    memset(&g_job, 0, sizeof g_job);
    strcpy(g_job.node, keep.node); g_job.then_play = keep.then_play; g_job.src = keep.src;
    g_job.targets = keep.targets; g_job.ntargets = keep.ntargets;
    memcpy(g_job.field, keep.field, sizeof g_job.field);
    g_job.kind = kind;
    g_job.thread = CreateThread(0, 0, job_thread, &g_job, 0, &tid);
    if (!g_job.thread) { g_job.kind = JOB_NONE; return 0; }
    return 1;
}

static void crumb_update(void)
{
    int i, o;
    if (g_m.acct_sel < 0 || g_m.acct_sel >= g_nsrc) { strcpy(g_m.src_crumb, "SELECT A SOURCE"); return; }
    o = _snprintf(g_m.src_crumb, sizeof g_m.src_crumb, "%s", g_src[g_m.acct_sel].name);
    for (i = 1; i <= g_depth && o > 0 && o < (int)sizeof g_m.src_crumb - 8; i++)
        o += _snprintf(g_m.src_crumb + o, sizeof g_m.src_crumb - (size_t)o, " > %s", g_names[i]);
    g_m.src_crumb[sizeof g_m.src_crumb - 1] = 0;
    CharUpperA(g_m.src_crumb);
}

static void browse(const char *node)
{
    if (g_m.acct_sel < 0 || g_m.acct_sel >= g_nsrc) return;
    strncpy(g_job.node, node, sizeof g_job.node - 1); g_job.node[sizeof g_job.node - 1] = 0;
    g_job.src = g_src[g_m.acct_sel];
    if (!job_start(JOB_BROWSE)) return;
    g_m.src_busy = 1; strcpy(g_m.src_status, "LOADING...");
    ui_model_changed(g_ui, UI_CH_SOURCES);
}

static void clear_level(void)
{
    free(g_lib); g_lib = 0; g_nlib = 0;
    free(g_m.src_items); g_m.src_items = 0; g_m.src_nitems = 0; g_m.src_sel = -1;
}

static void show_level(ea_sitem *items, int n)
{
    int i, tracks = 0;
    clear_level();
    g_lib = items; g_nlib = n;
    g_m.src_items = (ea_srcitem *)calloc((size_t)(n > 0 ? n : 1), sizeof(ea_srcitem));
    for (i = 0; i < n && g_m.src_items; i++) {
        strncpy(g_m.src_items[i].name, items[i].name, sizeof g_m.src_items[i].name - 1);
        g_m.src_items[i].container = !items[i].is_track;
        tracks += items[i].is_track;
    }
    g_m.src_nitems = g_m.src_items ? n : 0;
    g_m.src_sel = n > 0 ? 0 : -1;
    if (!n) strcpy(g_m.src_status, "NOTHING HERE");
    else if (tracks == n) sprintf(g_m.src_status, "%d TRACK%s", n, n == 1 ? "" : "S");
    else sprintf(g_m.src_status, "%d ITEM%s", n, n == 1 ? "" : "S");
    crumb_update();
    ui_list_reset(g_ui);
}

static void add_track(const ea_sitem *t) { pl_add_named(t->url, t->name, t->dur_s); }

static void on_src_open(void *ctx, int idx)
{
    (void)ctx;
    if (!g_nsrc || g_job.kind != JOB_NONE) return;
    if (idx < 0) { g_depth = 0; g_nodes[0][0] = 0; clear_level(); browse(""); return; }     /* an account row: its root */
    if (idx >= g_nlib) return;
    if (g_lib[idx].is_track) {
        add_track(&g_lib[idx]);
        ui_model_changed(g_ui, UI_CH_PLAYLIST);
        play_index(0, g_m.ntracks - 1);
        return;
    }
    if (g_depth + 1 >= MAX_DEPTH) return;
    g_depth++;
    strcpy(g_nodes[g_depth], g_lib[idx].node);
    strncpy(g_names[g_depth], g_lib[idx].name, sizeof g_names[0] - 1);
    browse(g_nodes[g_depth]);
}

/* PLAY / ADD / ADD ALL: the marked rows (or the one the cursor is on), in list
 * order - tracks as they are, folders expanded to every track beneath them */
static void collect(int then_play, int everything)
{
    ea_sitem *t;
    int i, n = 0, folders = 0, marked = 0;
    if (!g_nsrc || g_job.kind != JOB_NONE || g_nlib <= 0) return;
    for (i = 0; i < g_nlib && i < g_m.src_nitems; i++) marked += g_m.src_items[i].marked != 0;
    if (!everything && !marked && (g_m.src_sel < 0 || g_m.src_sel >= g_nlib)) return;
    t = (ea_sitem *)malloc(sizeof(ea_sitem) * (size_t)g_nlib);
    if (!t) return;
    for (i = 0; i < g_nlib; i++) {
        int take = everything || (marked ? (i < g_m.src_nitems && g_m.src_items[i].marked) : i == g_m.src_sel);
        if (take) { t[n++] = g_lib[i]; folders += !g_lib[i].is_track; }
    }
    if (folders && g_depth == 0) {                               /* a whole library is tens of thousands of tracks */
        free(t);
        strcpy(g_m.src_status, "OPEN THE LIBRARY FIRST"); ui_model_changed(g_ui, UI_CH_SOURCES);
        return;
    }
    if (!folders) {                                              /* only tracks: nothing to fetch */
        int first = g_m.ntracks;
        for (i = 0; i < n; i++) add_track(&t[i]);
        free(t);
        ui_model_changed(g_ui, UI_CH_PLAYLIST);
        if (then_play && g_m.ntracks > first) play_index(0, first);
        sprintf(g_m.src_status, "ADDED %d TRACK%s", n, n == 1 ? "" : "S"); ui_model_changed(g_ui, UI_CH_SOURCES);
        return;
    }
    g_job.targets = t; g_job.ntargets = n;
    g_job.src = g_src[g_m.acct_sel]; g_job.then_play = then_play;
    if (!job_start(JOB_COLLECT)) { free(t); g_job.targets = 0; g_job.ntargets = 0; return; }
    g_m.src_busy = 1; strcpy(g_m.src_status, "COLLECTING... BACK TO STOP");
    ui_model_changed(g_ui, UI_CH_SOURCES);
}

static void source_add(const ea_source *n)
{
    int i;
    for (i = 0; i < g_nsrc; i++) if (g_src[i].type == n->type && !strcmp(g_src[i].base, n->base)) break;   /* same server: replace */
    if (i == g_nsrc) { if (g_nsrc == EA_MAX_SOURCES) i = EA_MAX_SOURCES - 1; else g_nsrc++; }
    g_src[i] = *n;
    sources_save();
    accounts_to_model();
    g_m.acct_sel = i; g_m.accts[i].state = EA_SRC_OK;
}

static void source_remove_selected(void)
{
    int i = g_m.acct_sel;
    if (g_job.kind != JOB_NONE || i < 0 || i >= g_nsrc) return;
    memset(&g_src[i], 0, sizeof g_src[i]);
    memmove(&g_src[i], &g_src[i + 1], sizeof(ea_source) * (size_t)(g_nsrc - i - 1));
    memmove(&g_m.accts[i], &g_m.accts[i + 1], sizeof(ea_acct) * (size_t)(g_nsrc - i - 1));
    g_nsrc--;
    sources_save();
    accounts_to_model();
    clear_level(); g_depth = 0;
    strcpy(g_m.src_status, "REMOVED");
    crumb_update();
    ui_model_changed(g_ui, UI_CH_SOURCES);
    if (g_nsrc) on_src_open(0, -1);
}

/* called every frame: progress of, and results from, the running job */
static void job_poll(void)
{
    job_t *j = &g_job;
    int i;
    if (j->kind == JOB_NONE) return;
    if (j->kind == JOB_LINK && g_m.link_open) {
        if (j->code_ready && strcmp(g_m.link_code, j->code)) { strcpy(g_m.link_code, j->code); ui_model_changed(g_ui, UI_CH_SOURCES); }
        if (strcmp(g_m.link_status, j->status)) { strcpy(g_m.link_status, j->status); ui_model_changed(g_ui, UI_CH_SOURCES); }
    }
    if (j->kind == JOB_COLLECT && !j->done) {                     /* a live count: a whole artist list can take minutes */
        static int shown = -1;
        if (j->nitems != shown) { shown = j->nitems; sprintf(g_m.src_status, "%d TRACKS... BACK TO STOP", shown); ui_model_changed(g_ui, UI_CH_SOURCES); }
    }
    if (!j->done) return;
    WaitForSingleObject(j->thread, 2000); CloseHandle(j->thread);
    g_m.src_busy = 0;
    switch (j->kind) {
    case JOB_LINK:
    case JOB_SIGNIN:
        if (j->ok) {
            g_m.link_open = 0; g_m.link_code[0] = 0; g_m.form_open = 0;
            memset(g_m.form_field[2], 0, sizeof g_m.form_field[2]);
            source_add(&j->result);
            j->kind = JOB_NONE;
            on_src_open(0, -1);
            ui_model_changed(g_ui, UI_CH_SOURCES);
            return;
        }
        if (j->kind == JOB_SIGNIN) {                             /* stay in the form so the user can fix a typo */
            _snprintf(g_m.form_status, sizeof g_m.form_status, "%s", j->err); g_m.form_status[sizeof g_m.form_status - 1] = 0;
            CharUpperA(g_m.form_status);
        } else {
            g_m.link_open = 0; g_m.link_code[0] = 0;
            _snprintf(g_m.src_status, sizeof g_m.src_status, "%s", j->cancel ? "CANCELLED" : j->err);
        }
        break;
    case JOB_RECONNECT:
        for (i = 0; i < g_nsrc; i++) g_m.accts[i].state = j->alive[i] ? EA_SRC_OK : EA_SRC_UNREACHABLE;
        strcpy(g_m.src_status, g_nsrc ? "READY" : "");
        if (g_auto_jf[0][0]) break;
        for (i = 0; i < g_nsrc; i++) if (j->alive[i] && (g_want_acct < 0 || g_want_acct == i)) {   /* show a library straight away */
            g_m.acct_sel = i; j->kind = JOB_NONE;
            on_src_open(0, -1);
            ui_model_changed(g_ui, UI_CH_SOURCES);
            return;
        }
        break;
    case JOB_BROWSE:
        if (j->ok) {
            show_level(j->items, j->nitems); j->items = 0;
            if (g_m.acct_sel >= 0 && g_m.acct_sel < g_nsrc) g_m.accts[g_m.acct_sel].state = EA_SRC_OK;
            if (g_script_pos < g_script_n) {                     /* scripted descent, one row per loaded level */
                int row = g_script[g_script_pos++];
                j->kind = JOB_NONE; g_m.src_sel = row;
                ui_model_changed(g_ui, UI_CH_SOURCES);
                on_src_open(0, row);
                return;
            }
            if (g_then) { int c = g_then; g_then = 0; j->kind = JOB_NONE; g_m.src_busy = 0; on_command(0, c); ui_model_changed(g_ui, UI_CH_SOURCES); return; }
        } else {
            if (g_depth > 0) g_depth--;
            else if (g_m.acct_sel >= 0 && g_m.acct_sel < g_nsrc) g_m.accts[g_m.acct_sel].state = EA_SRC_UNREACHABLE;
            _snprintf(g_m.src_status, sizeof g_m.src_status, "%s", j->err);
        }
        break;
    case JOB_COLLECT: {
        int first = g_m.ntracks;
        for (i = 0; i < j->nitems; i++) add_track(&j->items[i]);
        if (j->nitems) {
            sprintf(g_m.src_status, "ADDED %d TRACK%s%s", j->nitems, j->nitems == 1 ? "" : "S", j->nitems >= COLLECT_CAP ? " (LIMIT)" : "");
            ui_model_changed(g_ui, UI_CH_PLAYLIST);
            if (j->then_play) play_index(0, first);
        } else _snprintf(g_m.src_status, sizeof g_m.src_status, "%s", j->err[0] ? j->err : "NO TRACKS HERE");
        free(j->items); j->items = 0;
        free(j->targets); j->targets = 0; j->ntargets = 0;
        break; }
    }
    g_m.src_status[sizeof g_m.src_status - 1] = 0;
    CharUpperA(g_m.src_status);
    j->kind = JOB_NONE;
    ui_model_changed(g_ui, UI_CH_SOURCES);
    if (g_auto_jf[0][0] && !g_m.form_open) {                     /* /jf: test switch: sign in once the startup check is done */
        memcpy(g_job.field, g_auto_jf, sizeof g_job.field); g_auto_jf[0][0] = 0;
        g_m.form_open = 1; strcpy(g_m.form_status, "SIGNING IN...");
        job_start(JOB_SIGNIN);
    }
}

static void sources_startup(void)
{
    sources_load();
    crumb_update();
    if (g_nsrc) strcpy(g_m.src_status, "CONNECTING...");
    job_start(JOB_RECONNECT);
}

/* ---- dialogs ----------------------------------------------------------------------- */

static int ask_file(int save, const char *filter, const char *defext, char *buf, int cap, int multi)
{
    OPENFILENAMEA o;
    memset(&o, 0, sizeof o);
    o.lStructSize = OFN_SIZE_V400;
    o.hwndOwner = g_wnd; o.lpstrFilter = filter; o.lpstrFile = buf; o.nMaxFile = (DWORD)cap; o.lpstrDefExt = defext;
    o.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST) | (multi ? OFN_ALLOWMULTISELECT : 0);
    buf[0] = 0;
    return save ? GetSaveFileNameA(&o) : GetOpenFileNameA(&o);
}

static void add_files_dialog(int replace_and_play)
{
    static char buf[32768];
    int first = replace_and_play ? 0 : g_m.ntracks;
    if (!ask_file(0, "Music (*.mp3;*.flac;*.ogg;*.wav)\0*.mp3;*.mp2;*.flac;*.ogg;*.oga;*.wav\0All files\0*.*\0", 0, buf, (int)sizeof buf, 1)) return;
    if (replace_and_play) { eng_stop(g_eng); pl_clear(); }
    if (buf[strlen(buf) + 1] == 0) pl_add(buf);                     /* a single file */
    else {                                                        /* dir\0name\0name\0\0 */
        const char *dir = buf, *p = buf + strlen(buf) + 1;
        char full[MAX_PATH];
        for (; *p; p += strlen(p) + 1) { _snprintf(full, MAX_PATH, "%s\\%s", dir, p); full[MAX_PATH - 1] = 0; pl_add(full); }
    }
    ui_model_changed(g_ui, UI_CH_PLAYLIST);
    if (replace_and_play && g_m.ntracks > first) play_index(0, first);
}

/* ---- settings that survive a restart -------------------------------------------------------
 * EASYAMP.INI beside the exe: [eq] the whole bank, [ui] page / panels / meters /
 * window position. The playlist is EASYAMP.M3U beside it (URLs stored without
 * credentials). Written on exit and on Windows shutdown. */

static int g_nostate;                               /* /nostate: neither read nor write (tests) */

static void ini_float(const char *sec, const char *key, float v) { char t[32]; sprintf(t, "%.3f", v); WritePrivateProfileStringA(sec, key, t, g_ini); }
static void ini_int(const char *sec, const char *key, int v) { char t[16]; sprintf(t, "%d", v); WritePrivateProfileStringA(sec, key, t, g_ini); }
static float ini_getf(const char *sec, const char *key, float def) { char t[32]; GetPrivateProfileStringA(sec, key, "", t, sizeof t, g_ini); return t[0] ? (float)atof(t) : def; }

static void state_path(char *out, const char *name)
{
    char *slash;
    strcpy(out, g_ini);
    slash = strrchr(out, '\\');
    strcpy(slash ? slash + 1 : out, name);
}

static void state_save(void)
{
    char key[8], val[96], path[MAX_PATH];
    RECT rc;
    int i;
    if (g_nostate) return;
    ini_int("eq", "on", g_m.eq_on); ini_int("eq", "bass", g_m.bass); ini_int("eq", "loud", g_m.loud);
    ini_float("eq", "preamp", g_m.preamp); ini_float("eq", "in", g_m.in_gain); ini_float("eq", "out", g_m.out_gain);
    ini_float("eq", "balance", g_m.balance); ini_float("eq", "pitch", g_m.pitch);
    ini_int("eq", "bands", g_m.nbands); ini_int("eq", "selected", g_m.selband);
    WritePrivateProfileStringA("eq", "preset", g_m.preset, g_ini);
    for (i = 0; i < EA_MAX_BANDS; i++) {
        sprintf(key, "b%d", i);
        if (i < g_m.nbands) sprintf(val, "%.2f %.2f %.3f %d", g_m.freqs[i], g_m.gains[i], g_m.q[i], g_m.types[i]);
        WritePrivateProfileStringA("eq", key, i < g_m.nbands ? val : 0, g_ini);
    }
    ini_int("ui", "page", g_m.page); ini_int("ui", "vu", g_m.viz_vu);
    ini_int("ui", "show_eq", g_m.show_eq); ini_int("ui", "show_pl", g_m.show_pl);
    if (g_wnd && !IsIconic(g_wnd) && GetWindowRect(g_wnd, &rc)) { ini_int("ui", "x", rc.left); ini_int("ui", "y", rc.top); }
    ini_int("ui", "selected", g_m.sel);
    state_path(path, "EASYAMP.M3U");
    if (g_m.ntracks) m3u_save(path); else DeleteFileA(path);
}

static void state_load(int *page, int *x, int *y)
{
    char key[8], val[96], path[MAX_PATH];
    int i, n;
    if (g_nostate) return;
    n = (int)GetPrivateProfileIntA("eq", "bands", 0, g_ini);
    if (n >= EA_MIN_BANDS && n <= EA_MAX_BANDS) {
        int good = 0;
        for (i = 0; i < n; i++) {
            float f, g, q; int t = 0;
            sprintf(key, "b%d", i);
            GetPrivateProfileStringA("eq", key, "", val, sizeof val, g_ini);
            {   /* not sscanf: mingw's scanf needs _strtoi64, which Windows 98's C library lacks */
                char *p = val, *e;
                f = (float)strtod(p, &e); if (e == p) break; p = e;
                g = (float)strtod(p, &e); if (e == p) break; p = e;
                q = (float)strtod(p, &e); if (e == p) break; p = e;
                t = (int)strtol(p, &e, 10);
            }
            if (f < 10 || f > 22000) break;
            g_m.freqs[i] = f; g_m.gains[i] = g < EA_BAND_MIN ? EA_BAND_MIN : (g > EA_BAND_MAX ? EA_BAND_MAX : g);
            g_m.q[i] = q < 0.1f ? 0.1f : (q > 12 ? 12 : q); g_m.types[i] = t < 0 || t > 2 ? 0 : t;
            good++;
        }
        if (good == n) g_m.nbands = n; else ea_model_init(&g_m);           /* a damaged bank: start flat rather than half-loaded */
    }
    g_m.eq_on = (int)GetPrivateProfileIntA("eq", "on", 1, g_ini) != 0;
    g_m.bass = (int)GetPrivateProfileIntA("eq", "bass", 0, g_ini) != 0;
    g_m.loud = (int)GetPrivateProfileIntA("eq", "loud", 0, g_ini) != 0;
    g_m.preamp = ini_getf("eq", "preamp", 0); g_m.in_gain = ini_getf("eq", "in", 0); g_m.out_gain = ini_getf("eq", "out", 0);
    g_m.balance = ini_getf("eq", "balance", 0); g_m.pitch = ini_getf("eq", "pitch", 1.0f);
    if (g_m.preamp < EA_PRE_MIN || g_m.preamp > EA_PRE_MAX) g_m.preamp = 0;
    if (g_m.pitch < 0.9f || g_m.pitch > 1.1f) g_m.pitch = 1.0f;
    if (g_m.balance < -1 || g_m.balance > 1) g_m.balance = 0;
    g_m.selband = (int)GetPrivateProfileIntA("eq", "selected", 0, g_ini);
    if (g_m.selband < 0 || g_m.selband >= g_m.nbands) g_m.selband = 0;
    GetPrivateProfileStringA("eq", "preset", "Flat", g_m.preset, sizeof g_m.preset, g_ini);
    g_m.viz_vu = (int)GetPrivateProfileIntA("ui", "vu", 0, g_ini) != 0;
    g_m.show_eq = (int)GetPrivateProfileIntA("ui", "show_eq", 1, g_ini) != 0;
    g_m.show_pl = (int)GetPrivateProfileIntA("ui", "show_pl", 1, g_ini) != 0;
    *page = (int)GetPrivateProfileIntA("ui", "page", 0, g_ini);
    *x = (int)GetPrivateProfileIntA("ui", "x", -32000, g_ini); *y = (int)GetPrivateProfileIntA("ui", "y", -32000, g_ini);
    state_path(path, "EASYAMP.M3U");
    m3u_load(path);
    g_m.sel = (int)GetPrivateProfileIntA("ui", "selected", -1, g_ini);
    if (g_m.sel >= g_m.ntracks) g_m.sel = g_m.ntracks - 1;
}

/* ---- EQ files ----------------------------------------------------------------------------- */

static void eq_import_file(void)
{
    static char text[65536];
    char path[MAX_PATH];
    FILE *f;
    size_t n;
    if (!ask_file(0, "Equalizer curves (*.txt)\0*.txt\0All files\0*.*\0", 0, path, MAX_PATH, 0)) return;
    f = fopen(path, "rb");
    if (!f) { MessageBoxA(g_wnd, "That file could not be opened.", "EasyAmp", MB_ICONWARNING); return; }
    n = fread(text, 1, sizeof text - 1, f); text[n] = 0;
    fclose(f);
    if (!ea_eq_import(&g_m, text)) { MessageBoxA(g_wnd, "No equalizer curve found in that file.\n\nEasyAmp reads Equalizer APO config files and AutoEQ GraphicEQ lines.", "EasyAmp", MB_ICONINFORMATION); return; }
    eng_set_dsp(g_eng, &g_m);
    ui_model_changed(g_ui, UI_CH_EQ);
}

static void eq_export_file(int graphic)
{
    static char text[8192];
    char path[MAX_PATH];
    FILE *f;
    if (!ask_file(1, "Text file (*.txt)\0*.txt\0", "txt", path, MAX_PATH, 0)) return;
    if (graphic) ea_eq_export_geq(&g_m, text, (int)sizeof text); else ea_eq_export_apo(&g_m, text, (int)sizeof text);
    f = fopen(path, "wb");
    if (!f) { MessageBoxA(g_wnd, "That file could not be written.", "EasyAmp", MB_ICONWARNING); return; }
    fputs(text, f);
    fclose(f);
}

/* ---- actions from the UI ------------------------------------------------------------ */

static void on_command(void *ctx, int cmd)
{
    char path[MAX_PATH];
    (void)ctx;
    switch (cmd) {
    case EA_CMD_EJECT:     add_files_dialog(1); break;
    case EA_CMD_PL_ADD:    add_files_dialog(0); break;
    case EA_CMD_PLAYPAUSE:
        if (g_m.state == EA_STOPPED) play_index(0, g_m.sel >= 0 ? g_m.sel : (g_m.cur >= 0 ? g_m.cur : 0));
        else eng_pause(g_eng, g_m.state == EA_PLAYING);
        break;
    case EA_CMD_STOP:      eng_stop(g_eng); break;
    case EA_CMD_PREV:      if (g_m.ntracks) play_index(0, g_m.cur > 0 ? g_m.cur - 1 : 0); break;
    case EA_CMD_NEXT:      if (g_m.cur + 1 < g_m.ntracks) play_index(0, g_m.cur + 1); break;
    case EA_CMD_PL_REMOVE: {
        int k, marked = 0;
        for (k = 0; k < g_m.ntracks; k++) marked += g_m.tracks[k].marked != 0;
        if (!marked) { if (g_m.sel == g_m.cur && g_m.cur >= 0) eng_stop(g_eng); pl_remove(g_m.sel); }
        else for (k = g_m.ntracks - 1; k >= 0; k--) if (g_m.tracks[k].marked) { if (k == g_m.cur) eng_stop(g_eng); pl_remove(k); }
        for (k = 0; k < g_m.ntracks; k++) g_m.tracks[k].marked = 0;
        ui_model_changed(g_ui, UI_CH_PLAYLIST);
        break; }
    case EA_CMD_PL_CLEAR:  eng_stop(g_eng); pl_clear(); g_m.title[0] = 0; ui_model_changed(g_ui, UI_CH_PLAYLIST | UI_CH_TITLE); break;
    case EA_CMD_PL_LOAD:
        if (ask_file(0, "Playlist (*.m3u)\0*.m3u;*.m3u8\0", 0, path, MAX_PATH, 0)) { eng_stop(g_eng); pl_clear(); m3u_load(path); ui_model_changed(g_ui, UI_CH_PLAYLIST); }
        break;
    case EA_CMD_PL_SAVE:
        if (g_m.ntracks && ask_file(1, "Playlist (*.m3u)\0*.m3u\0", "m3u", path, MAX_PATH, 0)) m3u_save(path);
        break;
    case EA_CMD_SRC_LINK:
        if (g_nsrc >= EA_MAX_SOURCES) { strcpy(g_m.src_status, "REMOVE AN ACCOUNT FIRST"); ui_model_changed(g_ui, UI_CH_SOURCES); break; }
        if (job_start(JOB_LINK)) { g_m.link_open = 1; g_m.link_code[0] = 0; strcpy(g_m.link_status, "CONTACTING PLEX.TV..."); ui_model_changed(g_ui, UI_CH_SOURCES); }
        break;
    case EA_CMD_SRC_LINK_CANCEL: InterlockedExchange(&g_job.cancel, 1); g_m.link_open = 0; ui_model_changed(g_ui, UI_CH_SOURCES); break;
    case EA_CMD_SRC_JELLYFIN:
        if (g_job.kind != JOB_NONE) break;
        g_m.form_open = 1; g_m.form_focus = g_m.form_field[0][0] ? (g_m.form_field[1][0] ? 2 : 1) : 0;
        strcpy(g_m.form_status, "");
        ui_model_changed(g_ui, UI_CH_SOURCES);
        break;
    case EA_CMD_SRC_FORM_SUBMIT:
        if (g_job.kind != JOB_NONE) break;
        if (!g_m.form_field[0][0] || !g_m.form_field[1][0]) { strcpy(g_m.form_status, "SERVER AND USERNAME ARE REQUIRED"); ui_model_changed(g_ui, UI_CH_SOURCES); break; }
        memcpy(g_job.field, g_m.form_field, sizeof g_job.field);
        strcpy(g_m.form_status, "SIGNING IN...");
        job_start(JOB_SIGNIN);
        ui_model_changed(g_ui, UI_CH_SOURCES);
        break;
    case EA_CMD_SRC_FORM_CANCEL:
        if (g_job.kind == JOB_SIGNIN) break;                      /* let the request finish; it is seconds at most */
        g_m.form_open = 0; memset(g_m.form_field[2], 0, sizeof g_m.form_field[2]);
        ui_model_changed(g_ui, UI_CH_SOURCES);
        break;
    case EA_CMD_SRC_REMOVE:   source_remove_selected(); break;
    case EA_CMD_SRC_BACK:
        if (g_job.kind == JOB_COLLECT) { InterlockedExchange(&g_job.cancel, 1); break; }     /* stop gathering; keep what was found */
        if (g_nsrc && g_depth > 0 && g_job.kind == JOB_NONE) { g_depth--; browse(g_nodes[g_depth]); }
        break;
    case EA_CMD_SRC_PLAY:    collect(1, 0); break;
    case EA_CMD_SRC_ADD:     collect(0, 0); break;
    case EA_CMD_SRC_ADD_ALL: collect(0, 1); break;
    case EA_CMD_EQ_IMPORT:     eq_import_file(); break;
    case EA_CMD_EQ_EXPORT_APO: eq_export_file(0); break;
    case EA_CMD_EQ_EXPORT_GEQ: eq_export_file(1); break;
    case EA_CMD_WIN_MINIMIZE: ShowWindow(g_wnd, SW_MINIMIZE); break;
    case EA_CMD_WIN_CLOSE:    PostMessageA(g_wnd, WM_CLOSE, 0, 0); break;
    case EA_CMD_OPEN_UPDATE:  ShellExecuteA(g_wnd, "open", "https://www.easyampstereo.com/download.html", 0, 0, SW_SHOWNORMAL); break;
    }
}

static void on_seek(void *ctx, float f) { (void)ctx; if (g_m.state != EA_STOPPED) eng_seek(g_eng, f); }
static void on_eq(void *ctx) { (void)ctx; eng_set_dsp(g_eng, &g_m); }

/* ---- painting ------------------------------------------------------------------------ */

static void presenter_free(void)
{
    if (g_memdc) { if (g_olddib) SelectObject(g_memdc, g_olddib); DeleteDC(g_memdc); }
    if (g_dib) DeleteObject(g_dib);
    if (g_pal) DeleteObject(g_pal);
    g_memdc = 0; g_dib = 0; g_olddib = 0; g_pal = 0; g_dibbits = 0;
}

/* (re)build the off-screen bitmap for the desktop's current colour depth */
static int presenter_init(void)
{
    struct { BITMAPINFOHEADER h; union { RGBQUAD rgb[256]; DWORD mask[3]; } c; } bi;
    HDC screen = GetDC(0);
    int bpp = GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES), i, bits;
    if (g_force_depth) bpp = g_force_depth;
    presenter_free();
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = EA_WIN_W; bi.h.biHeight = -EA_WIN_H; bi.h.biPlanes = 1; bi.h.biCompression = BI_RGB;
    if (bpp >= 24) { g_present = PRESENT_DIRECT; bits = 32; }
    else if (bpp >= 15) {
        g_present = PRESENT_HICOLOR; bits = 16;
        g_green_bits = bpp == 15 ? 5 : 6;
        if (g_green_bits == 6) { bi.h.biCompression = BI_BITFIELDS; bi.c.mask[0] = 0xf800; bi.c.mask[1] = 0x07e0; bi.c.mask[2] = 0x001f; }
    } else {
        const unsigned char *pal = bpp >= 8 ? EA_PAL256 : EA_PAL16;
        int n = bpp >= 8 ? EA_PAL256_N : 16;
        g_present = bpp >= 8 ? PRESENT_PAL256 : PRESENT_VGA16; bits = 8;
        bi.h.biClrUsed = (DWORD)n;
        for (i = 0; i < n; i++) { bi.c.rgb[i].rgbRed = pal[i * 3]; bi.c.rgb[i].rgbGreen = pal[i * 3 + 1]; bi.c.rgb[i].rgbBlue = pal[i * 3 + 2]; }
        if (g_present == PRESENT_PAL256) {
            /* our own logical palette: Windows keeps 20 system colours, the
             * other 236 slots become the skin's while we are in front */
            struct { WORD ver, n; PALETTEENTRY e[256]; } lp;
            lp.ver = 0x300; lp.n = (WORD)n;
            for (i = 0; i < n; i++) { lp.e[i].peRed = pal[i * 3]; lp.e[i].peGreen = pal[i * 3 + 1]; lp.e[i].peBlue = pal[i * 3 + 2]; lp.e[i].peFlags = 0; }
            g_pal = CreatePalette((LOGPALETTE *)&lp);
        }
    }
    bi.h.biBitCount = (WORD)bits;
    g_dib_stride = ((EA_WIN_W * bits + 31) / 32) * 4;
    g_memdc = CreateCompatibleDC(screen);
    g_dib = CreateDIBSection(screen, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &g_dibbits, 0, 0);
    ReleaseDC(0, screen);
    if (!g_memdc || !g_dib || !g_dibbits) return 0;
    g_olddib = (HBITMAP)SelectObject(g_memdc, g_dib);
    return 1;
}

/* convert one rect of the UI's picture into the off-screen bitmap */
static void convert(int x, int y, int w, int h)
{
    ea_surface *s = ui_surface(g_ui);
    int j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > EA_WIN_W) w = EA_WIN_W - x;
    if (y + h > EA_WIN_H) h = EA_WIN_H - y;
    if (w <= 0 || h <= 0) return;
    switch (g_present) {
    case PRESENT_DIRECT:
        for (j = 0; j < h; j++) memcpy((ea_px *)g_dibbits + (y + j) * EA_WIN_W + x, s->px + (y + j) * EA_WIN_W + x, (size_t)w * 4);
        break;
    case PRESENT_HICOLOR: gfx_dither16(s, x, y, w, h, (unsigned short *)g_dibbits, g_dib_stride, g_green_bits); break;
    case PRESENT_PAL256:  gfx_dither_indexed(s, x, y, w, h, (unsigned char *)g_dibbits, g_dib_stride, EA_LUT256, 20); break;
    case PRESENT_VGA16:   gfx_dither_indexed(s, x, y, w, h, (unsigned char *)g_dibbits, g_dib_stride, EA_LUT16, 96); break;
    }
}

static void use_palette(HDC dc)
{
    if (g_pal) { SelectPalette(dc, g_pal, FALSE); RealizePalette(dc); }
}

static void present(HDC dc)
{
    ea_rect d[48];
    int n = ui_render(g_ui, d, 48), i;
    use_palette(dc);
    for (i = 0; i < n; i++) {
        convert(d[i].x, d[i].y, d[i].w, d[i].h);
        BitBlt(dc, d[i].x, d[i].y, d[i].w, d[i].h, g_memdc, d[i].x, d[i].y, SRCCOPY);
    }
}

/* /shot: save the picture as presented - read back through GDI from the
 * off-screen bitmap, so an indexed or 16-bit mode is captured as the screen
 * would show it and the colour table itself is part of what gets checked */
static void save_shot(void)
{
    BITMAPFILEHEADER fh;
    BITMAPINFO bi;
    HDC screen = GetDC(0), dc = CreateCompatibleDC(screen);
    void *bits = 0;
    HBITMAP bm, old;
    FILE *f;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = EA_WIN_W; bi.bmiHeader.biHeight = EA_WIN_H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bm = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    ReleaseDC(0, screen);
    if (!dc || !bm) return;
    old = (HBITMAP)SelectObject(dc, bm);
    convert(0, 0, EA_WIN_W, EA_WIN_H);
    BitBlt(dc, 0, 0, EA_WIN_W, EA_WIN_H, g_memdc, 0, 0, SRCCOPY);
    GdiFlush();
    f = fopen(g_shot, "wb");
    if (f) {
        memset(&fh, 0, sizeof fh);
        fh.bfType = 0x4d42; fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader; fh.bfSize = fh.bfOffBits + EA_WIN_W * EA_WIN_H * 4;
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
        fwrite(bits, 4, EA_WIN_W * EA_WIN_H, f);
        fclose(f);
    }
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc);
}

static void frame(void)
{
    DWORD now = GetTickCount();
    int old_state = g_m.state, old_sec = g_m.pos_ms / 1000, old_kbps = g_m.kbps, ev, what = UI_CH_VIZ;
    HDC dc;
    POINT pt;
    RECT rc;
    ev = eng_poll(g_eng, &g_m);
    job_poll();
    if (g_m.state != old_state || g_m.kbps != old_kbps) what |= UI_CH_TRANSPORT | UI_CH_TIME;
    if (g_m.pos_ms / 1000 != old_sec || g_m.state == EA_PLAYING) what |= UI_CH_TIME;
    if (ev == ENG_EV_ENDED) { if (g_m.cur + 1 < g_m.ntracks) play_index(0, g_m.cur + 1); else { g_m.cur = -1; what |= UI_CH_PLAYLIST; } }
    if (ev == ENG_EV_ERROR) { strcpy(g_m.title, "LOAD ERROR"); what |= UI_CH_TITLE; }   /* never auto-advance on errors */
    ui_model_changed(g_ui, what);
    ui_tick(g_ui, (int)(now - g_last_tick));
    g_last_tick = now;
    GetCursorPos(&pt); GetWindowRect(g_wnd, &rc);
    if (!PtInRect(&rc, pt)) ui_mouse_leave(g_ui);
    dc = GetDC(g_wnd);
    present(dc);
    ReleaseDC(g_wnd, dc);
    if (g_shot[0] && now >= g_shot_at) { save_shot(); g_shot[0] = 0; PostMessageA(g_wnd, WM_CLOSE, 0, 0); }
}

static int map_key(WPARAM vk)
{
    switch (vk) {
    case VK_UP: return UI_KEY_UP;       case VK_DOWN: return UI_KEY_DOWN;
    case VK_PRIOR: return UI_KEY_PGUP;  case VK_NEXT: return UI_KEY_PGDN;
    case VK_HOME: return UI_KEY_HOME;   case VK_END: return UI_KEY_END;
    case VK_RETURN: return UI_KEY_ENTER; case VK_DELETE: return UI_KEY_DELETE;
    case VK_ESCAPE: return UI_KEY_ESC;  case VK_SPACE: return g_m.form_open ? 0 : UI_KEY_SPACE;
    }
    return 0;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        int pw = ps.rcPaint.right - ps.rcPaint.left, ph = ps.rcPaint.bottom - ps.rcPaint.top;
        present(dc);
        convert(ps.rcPaint.left, ps.rcPaint.top, pw, ph);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, pw, ph, g_memdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(h, &ps);
        return 0; }
    /* 256-colour desktops: take the palette when we come to the front, and
     * re-map when another program has just taken it */
    case WM_QUERYNEWPALETTE:
        if (g_pal) { HDC dc = GetDC(h); use_palette(dc); ReleaseDC(h, dc); InvalidateRect(h, 0, FALSE); return TRUE; }
        return FALSE;
    case WM_PALETTECHANGED:
        if (g_pal && (HWND)wp != h) { HDC dc = GetDC(h); use_palette(dc); ReleaseDC(h, dc); InvalidateRect(h, 0, FALSE); }
        return 0;
    case WM_DISPLAYCHANGE:          /* the user changed colour depth: pick the matching path */
        presenter_init();
        InvalidateRect(h, 0, FALSE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: frame(); return 0;
    case WM_NCHITTEST: {
        POINT p;
        p.x = x; p.y = y;
        ScreenToClient(h, &p);
        return ui_is_caption(g_ui, p.x, p.y) ? HTCAPTION : HTCLIENT; }
    case WM_MOUSEMOVE:   ui_mouse_move(g_ui, x, y); return 0;
    case WM_LBUTTONDOWN: SetCapture(h); ui_set_mods(g_ui, ((wp & MK_SHIFT) ? UI_MOD_SHIFT : 0) | ((wp & MK_CONTROL) ? UI_MOD_CTRL : 0)); ui_mouse_down(g_ui, x, y); return 0;
    case WM_LBUTTONUP:   ui_mouse_up(g_ui, x, y); ReleaseCapture(); return 0;
    case WM_LBUTTONDBLCLK: ui_mouse_dbl(g_ui, x, y); return 0;
    case WM_MOUSEWHEEL: {
        POINT p;
        p.x = x; p.y = y;
        ScreenToClient(h, &p);
        ui_wheel(g_ui, p.x, p.y, (short)HIWORD(wp) / 120);
        return 0; }
    case WM_KEYDOWN: {
        int ctrl = GetKeyState(VK_CONTROL) < 0, k = ctrl && wp == 'A' ? UI_KEY_SELECT_ALL : map_key(wp);
        ui_set_mods(g_ui, (GetKeyState(VK_SHIFT) < 0 ? UI_MOD_SHIFT : 0) | (ctrl ? UI_MOD_CTRL : 0));
        if (k) ui_key(g_ui, k);
        return 0; }
    case WM_CHAR: ui_char(g_ui, (int)wp); return 0;
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        UINT n = DragQueryFileA(d, 0xFFFFFFFF, 0, 0), i;
        char path[MAX_PATH];
        int first = g_m.ntracks;
        for (i = 0; i < n; i++) if (DragQueryFileA(d, i, path, MAX_PATH)) pl_add(path);
        DragFinish(d);
        ui_model_changed(g_ui, UI_CH_PLAYLIST);
        if (g_m.state == EA_STOPPED && g_m.ntracks > first) play_index(0, first);
        return 0; }
    case WM_ENDSESSION: if (wp) state_save(); return 0;          /* Windows is shutting down */
    case WM_DESTROY: state_save(); KillTimer(h, TIMER_ID); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASSA wc;
    ea_actions act;
    MSG msg;
    int i, sx, sy, page = 0, autoplay = 0, wx = -32000, wy = -32000, cmd_page = -1;
    (void)prev; (void)cmdline;

    ea_model_init(&g_m);
    act.ctx = 0; act.command = on_command; act.seek = on_seek; act.play_index = play_index; act.eq_changed = on_eq;
    act.src_open = on_src_open;
    g_ui = ui_create(&g_m, &act, 0);
    g_eng = eng_create();
    if (!g_ui || !g_eng) { MessageBoxA(0, "Out of memory.", "EasyAmp", MB_ICONERROR); return 1; }

    for (i = 1; i < __argc; i++) if (!strcmp(__argv[i], "/nostate")) g_nostate = 1;
    ini_path();
    state_load(&page, &wx, &wy);
    { int saved = g_m.ntracks; (void)saved; }
    for (i = 1; i < __argc; i++) {
        const char *a = __argv[i];
        if (!strncmp(a, "/shot:", 6)) { strncpy(g_shot, a + 6, MAX_PATH - 1); if (!g_shot_at) g_shot_at = 1500; }
        else if (!strncmp(a, "/shotms:", 8)) g_shot_at = (DWORD)atoi(a + 8);
        else if (!strncmp(a, "/page:", 6)) cmd_page = atoi(a + 6);
        else if (!strcmp(a, "/nostate")) g_nostate = 1;
        else if (!strncmp(a, "/depth:", 7)) g_force_depth = atoi(a + 7);      /* 32, 16, 15, 8, 4: try a colour path on any desktop */
        else if (!strncmp(a, "/preset:", 8)) { int p = atoi(a + 8); if (p >= 0 && p < ea_preset_count()) ea_preset_apply(&g_m, p); }
        else if (!strncmp(a, "/vu", 3)) g_m.viz_vu = 1;
        else if (!strncmp(a, "/jf:", 4)) { char t[400], *c1, *c2; strncpy(t, a + 4, sizeof t - 1); t[sizeof t - 1] = 0;
            if ((c1 = strchr(t, ',')) != 0 && (c2 = strchr(c1 + 1, ',')) != 0) { *c1 = 0; *c2 = 0; strcpy(g_auto_jf[0], t); strcpy(g_auto_jf[1], c1 + 1); strcpy(g_auto_jf[2], c2 + 1); } }
        else if (!strncmp(a, "/acct:", 6)) g_want_acct = atoi(a + 6);
        else if (!strncmp(a, "/then:", 6)) g_then = !strcmp(a + 6, "addall") ? EA_CMD_SRC_ADD_ALL : !strcmp(a + 6, "play") ? EA_CMD_SRC_PLAY : EA_CMD_SRC_ADD;
        else if (!strncmp(a, "/open:", 6)) { const char *q = a + 6; while (*q && g_script_n < 8) { g_script[g_script_n++] = atoi(q); q = strchr(q, ','); if (!q) break; q++; } }
        else { const char *dot = strrchr(a, '.'); if (!autoplay) pl_clear(); if (dot && !lstrcmpiA(dot, ".m3u")) m3u_load(a); else pl_add(a); autoplay = 1; }
    }
    eng_set_dsp(g_eng, &g_m);
    net_init();
    sources_startup();
    if (!presenter_init()) { MessageBoxA(0, "Could not create the display surface.", "EasyAmp", MB_ICONERROR); return 1; }

    memset(&wc, 0, sizeof wc);
    wc.style = CS_DBLCLKS | CS_OWNDC;
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "EasyAmpRetro";
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    if (!RegisterClassA(&wc)) return 1;
    sx = (GetSystemMetrics(SM_CXSCREEN) - EA_WIN_W) / 2; sy = (GetSystemMetrics(SM_CYSCREEN) - EA_WIN_H) / 2;
    /* the remembered position, unless the screen has since shrunk under it */
    if (wx > -EA_WIN_W + 80 && wy > -20 && wx < GetSystemMetrics(SM_CXSCREEN) - 80 && wy < GetSystemMetrics(SM_CYSCREEN) - 40) { sx = wx; sy = wy; }
    if (cmd_page >= 0) page = cmd_page;
    g_wnd = CreateWindowExA(WS_EX_ACCEPTFILES, "EasyAmpRetro", "EasyAmp", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                            sx, sy < 0 ? 0 : sy, EA_WIN_W, EA_WIN_H, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    ui_set_page(g_ui, page);
    if (autoplay && g_m.ntracks) play_index(0, 0);
    g_last_tick = GetTickCount();
    if (g_shot[0]) g_shot_at += g_last_tick;
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    SetTimer(g_wnd, TIMER_ID, FRAME_MS, 0);
    while (GetMessageA(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    InterlockedExchange(&g_job.cancel, 1);
    if (g_job.kind != JOB_NONE && g_job.thread) WaitForSingleObject(g_job.thread, 4000);
    eng_destroy(g_eng);
    ui_destroy(g_ui);
    presenter_free();
    net_shutdown();
    return 0;
}
