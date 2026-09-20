#include "plex.h"
#include "json.h"
#include "net.h"
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define snprintf _snprintf
#endif

static void headers(char *out, int cap, const char *client_id, const char *token)
{
    int n = snprintf(out, (size_t)cap,
                     "X-Plex-Product: EasyAmp\r\nX-Plex-Version: " EA_VERSION "\r\nX-Plex-Client-Identifier: %s\r\n"
                     "X-Plex-Device-Name: EasyAmp retro\r\nX-Plex-Platform: Windows\r\n", client_id);
    if (token && token[0] && n > 0 && n < cap) snprintf(out + n, (size_t)(cap - n), "X-Plex-Token: %s\r\n", token);
    out[cap - 1] = 0;
}

static void fail(char *err, int cap, const char *what, const ea_http *r)
{
    if (r->status) snprintf(err, (size_t)cap, "%s: HTTP %d", what, r->status);
    else snprintf(err, (size_t)cap, "%s: %s", what, r->err);
    err[cap - 1] = 0;
}

int plex_pin_start(const char *client_id, long *pin_id, char code[8], char *err, int errcap)
{
    char h[512];
    ea_http r;
    ea_json j;
    int ok = 0;
    headers(h, sizeof h, client_id, 0);
    if (net_request("POST", "https://plex.tv/api/v2/pins?strong=false", h, "", &r, 20000) != 201) { fail(err, errcap, "plex.tv", &r); net_free(&r); return 0; }
    if (json_parse(&j, r.body, r.len)) {
        int root = json_root(&j), c = json_get(&j, root, "code");
        *pin_id = json_int(&j, json_get(&j, root, "id"));
        if (json_is_string(&j, c) && *pin_id) { json_str(&j, c, code, 8); ok = 1; }
        json_free(&j);
    }
    if (!ok) snprintf(err, (size_t)errcap, "plex.tv sent an unexpected reply");
    net_free(&r);
    return ok;
}

int plex_pin_poll(const char *client_id, long pin_id, char *token, int tokcap, char *err, int errcap)
{
    char h[512], url[128];
    ea_http r;
    ea_json j;
    int res = 0, st;
    headers(h, sizeof h, client_id, 0);
    sprintf(url, "https://plex.tv/api/v2/pins/%ld", pin_id);
    st = net_request("GET", url, h, 0, &r, 20000);
    if (st == 404) { snprintf(err, (size_t)errcap, "code expired - try again"); net_free(&r); return -1; }
    if (st != 200) { fail(err, errcap, "plex.tv", &r); net_free(&r); return st ? -1 : 0; }   /* a dropped poll is not fatal */
    if (json_parse(&j, r.body, r.len)) {
        int t = json_get(&j, json_root(&j), "authToken");
        if (json_is_string(&j, t)) { json_str(&j, t, token, tokcap); res = token[0] ? 1 : 0; }
        json_free(&j);
    }
    net_free(&r);
    return res;
}

int plex_alive(const plex_server *s)
{
    char url[160], h[128];
    ea_http r;
    int st;
    snprintf(url, sizeof url, "%s/identity", s->base);
    snprintf(h, sizeof h, "X-Plex-Token: %s\r\n", s->token);
    st = net_request("GET", url, h, 0, &r, 4000);
    net_free(&r);
    return st != 0;                  /* any HTTP answer, even 401, proves it is reachable */
}

int plex_discover(const char *client_id, const char *account_token, plex_server *out, int max, char *err, int errcap)
{
    char h[640];
    ea_http r;
    ea_json j;
    int found = 0, saw_server = 0, res;
    headers(h, sizeof h, client_id, account_token);
    if (net_request("GET", "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=0", h, 0, &r, 25000) != 200) {
        fail(err, errcap, "plex.tv", &r); net_free(&r); return 0;
    }
    if (!json_parse(&j, r.body, r.len)) { snprintf(err, (size_t)errcap, "plex.tv sent an unexpected reply"); net_free(&r); return 0; }
    for (res = json_first(&j, json_root(&j)); res >= 0 && found < max; res = json_next(&j, res)) {
        char provides[64], addr[64], tmp[16];
        int conns, c, pass;
        plex_server s;
        json_str(&j, json_get(&j, res, "provides"), provides, sizeof provides);
        if (!strstr(provides, "server")) continue;
        saw_server = 1;
        memset(&s, 0, sizeof s);
        json_str(&j, json_get(&j, res, "name"), s.name, sizeof s.name);
        json_str(&j, json_get(&j, res, "clientIdentifier"), s.machine, sizeof s.machine);
        if (json_is_string(&j, json_get(&j, res, "accessToken"))) json_str(&j, json_get(&j, res, "accessToken"), s.token, sizeof s.token);
        else strncpy(s.token, account_token, sizeof s.token - 1);
        conns = json_get(&j, res, "connections");
        /* LAN addresses first, over plain HTTP: no TLS cost per request, and
         * the playback stream can use the very same base URL */
        for (pass = 0; pass < 2 && !s.base[0]; pass++)
            for (c = json_first(&j, conns); c >= 0; c = json_next(&j, c)) {
                int local = json_true(&j, json_get(&j, c, "local")), relay = json_true(&j, json_get(&j, c, "relay"));
                if (relay || local != (pass == 0)) continue;
                json_str(&j, json_get(&j, c, "address"), addr, sizeof addr);
                json_str(&j, json_get(&j, c, "port"), tmp, sizeof tmp);
                if (!addr[0] || strchr(addr, ':')) continue;              /* no IPv6 on Windows 98 */
                snprintf(s.base, sizeof s.base, "http://%s:%s", addr, tmp[0] ? tmp : "32400");
                if (plex_alive(&s)) break;
                s.base[0] = 0;
            }
        if (s.base[0]) out[found++] = s;
    }
    json_free(&j);
    net_free(&r);
    if (!found) snprintf(err, (size_t)errcap, saw_server ? "server found but not reachable over plain HTTP (Settings > Network > Secure connections: Preferred)"
                                                         : "no Plex Media Server on this account");
    return found;
}

static void track_from(const ea_json *j, int d, plex_item *it)
{
    int media = json_first(j, json_get(j, d, "Media")), part = json_first(j, json_get(j, media, "Part"));
    char title[96];
    it->kind = PLEX_TRACK;
    json_str(j, json_get(j, d, "ratingKey"), it->key, sizeof it->key);
    json_str(j, json_get(j, d, "title"), title, sizeof title);
    json_str(j, json_get(j, d, "grandparentTitle"), it->artist, sizeof it->artist);
    if (json_get(j, d, "originalTitle") >= 0) json_str(j, json_get(j, d, "originalTitle"), it->artist, sizeof it->artist);
    snprintf(it->name, sizeof it->name, "%s%s%s", it->artist, it->artist[0] ? " - " : "", title);
    it->name[sizeof it->name - 1] = 0;
    json_str(j, json_get(j, part, "key"), it->part, sizeof it->part);
    json_str(j, json_get(j, media, "audioCodec"), it->codec, sizeof it->codec);
    it->dur_s = (int)(json_int(j, json_get(j, d, "duration")) / 1000);
}

plex_item *plex_browse(const plex_server *s, const char *client_id, const char *node, int *n, char *err, int errcap)
{
    char url[256], h[640], list[16] = "Metadata";
    ea_http r;
    ea_json j;
    plex_item *items = 0;
    int kind, mc, arr, d, count, i = 0;
    *n = 0;
    if (!node[0]) { snprintf(url, sizeof url, "%s/library/sections", s->base); kind = PLEX_SECTION; strcpy(list, "Directory"); }
    else if (!strncmp(node, "section/", 8)) { snprintf(url, sizeof url, "%s/library/sections/%s/all?type=8", s->base, node + 8); kind = PLEX_ARTIST; }
    else if (!strncmp(node, "artist/", 7)) { snprintf(url, sizeof url, "%s/library/metadata/%s/children", s->base, node + 7); kind = PLEX_ALBUM; }
    else if (!strncmp(node, "album/", 6)) { snprintf(url, sizeof url, "%s/library/metadata/%s/children", s->base, node + 6); kind = PLEX_TRACK; }
    else { snprintf(err, (size_t)errcap, "bad node"); return 0; }
    headers(h, sizeof h, client_id, s->token);
    if (net_request("GET", url, h, 0, &r, 30000) != 200) { fail(err, errcap, s->name, &r); net_free(&r); return 0; }
    if (!json_parse(&j, r.body, r.len)) { snprintf(err, (size_t)errcap, "server sent an unexpected reply"); net_free(&r); return 0; }
    mc = json_get(&j, json_root(&j), "MediaContainer");
    arr = json_get(&j, mc, list);
    count = json_size(&j, arr);
    items = (plex_item *)calloc((size_t)(count > 0 ? count : 1), sizeof *items);
    for (d = json_first(&j, arr); items && d >= 0 && i < count; d = json_next(&j, d)) {
        plex_item *it = &items[i];
        if (kind == PLEX_SECTION) {
            char type[16];
            json_str(&j, json_get(&j, d, "type"), type, sizeof type);
            if (strcmp(type, "artist")) continue;                       /* music libraries only */
            json_str(&j, json_get(&j, d, "key"), it->key, sizeof it->key);
            json_str(&j, json_get(&j, d, "title"), it->name, sizeof it->name);
            it->kind = PLEX_SECTION;
        } else if (kind == PLEX_TRACK) track_from(&j, d, it);
        else {
            it->kind = kind;
            json_str(&j, json_get(&j, d, "ratingKey"), it->key, sizeof it->key);
            json_str(&j, json_get(&j, d, "title"), it->name, sizeof it->name);
        }
        i++;
    }
    json_free(&j);
    net_free(&r);
    *n = i;
    if (!i && items) err[0] = 0;
    return items;
}

static int is_mp3(const char *codec)
{
    return !codec[0] || ((codec[0] | 32) == 'm' && (codec[1] | 32) == 'p' && codec[2] == '3' && !codec[3]);
}

void plex_track_url(const plex_server *s, const char *client_id, const plex_item *t, char *out, int cap)
{
    if (is_mp3(t->codec)) snprintf(out, (size_t)cap, "%s%s", s->base, t->part);
    else snprintf(out, (size_t)cap,
                  "%s/music/:/transcode/universal/start.mp3?path=%%2Flibrary%%2Fmetadata%%2F%s&mediaIndex=0&partIndex=0"
                  "&protocol=http&directPlay=0&directStream=0&audioCodec=mp3&maxAudioBitrate=192&X-Plex-Platform=Chrome"
                  "&X-Plex-Client-Identifier=%s&X-Plex-Session-Identifier=%s-%s"
                  "&X-Plex-Client-Profile-Extra=add-transcode-target%%28type%%3DmusicProfile%%26context%%3Dstreaming%%26protocol%%3Dhttp%%26container%%3Dmp3%%26audioCodec%%3Dmp3%%29",
                  s->base, t->key, client_id, client_id, t->key);
    out[cap - 1] = 0;
}

void plex_auth_url(const plex_server *s, const char *url, char *out, int cap)
{
    snprintf(out, (size_t)cap, "%s%sX-Plex-Token=%s", url, strchr(url, '?') ? "&" : "?", s->token);
    out[cap - 1] = 0;
}
