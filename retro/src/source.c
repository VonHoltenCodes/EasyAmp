#include "source.h"
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

const char *source_type_name(int type) { return type == SOURCE_JELLYFIN ? "Jellyfin" : "Plex"; }

int source_owns_url(const ea_source *s, const char *url)
{
    size_t bl = strlen(s->base);
    return bl > 0 && !strncmp(url, s->base, bl) && (url[bl] == '/' || url[bl] == 0);
}

void source_auth_url(const ea_source *s, const char *url, char *out, int cap)
{
    const char *param = s->type == SOURCE_JELLYFIN ? "api_key" : "X-Plex-Token";
    snprintf(out, (size_t)cap, "%s%s%s=%s", url, strchr(url, '?') ? "&" : "?", param, s->token);
    out[cap - 1] = 0;
}

/* ---- Plex, adapted ------------------------------------------------------------------ */

static void as_plex(const ea_source *s, plex_server *p)
{
    memset(p, 0, sizeof *p);
    strncpy(p->name, s->name, sizeof p->name - 1);
    strncpy(p->base, s->base, sizeof p->base - 1);
    strncpy(p->token, s->token, sizeof p->token - 1);
}

static ea_sitem *plex_level(const ea_source *s, const char *client_id, const char *node, int *n, char *err, int errcap)
{
    static const char *prefix[] = { "section", "artist", "album" };
    plex_server p;
    plex_item *it;
    ea_sitem *out;
    int i, cnt = 0;
    as_plex(s, &p);
    it = plex_browse(&p, client_id, node, &cnt, err, errcap);
    if (!it) return 0;
    out = (ea_sitem *)calloc((size_t)(cnt > 0 ? cnt : 1), sizeof *out);
    for (i = 0; out && i < cnt; i++) {
        strncpy(out[i].name, it[i].name, sizeof out[i].name - 1);
        if (it[i].kind == PLEX_TRACK) {
            out[i].is_track = 1; out[i].dur_s = it[i].dur_s;
            plex_track_url(&p, client_id, &it[i], out[i].url, (int)sizeof out[i].url);
        } else snprintf(out[i].node, sizeof out[i].node, "%s/%s", prefix[it[i].kind], it[i].key);
    }
    free(it);
    *n = out ? cnt : 0;
    return out;
}

/* ---- Jellyfin ------------------------------------------------------------------------- */

static void jf_headers(char *out, int cap, const char *client_id, const char *token, int json_body)
{
    int n = snprintf(out, (size_t)cap, "Authorization: MediaBrowser Client=\"EasyAmp\", Device=\"EasyAmp retro\", DeviceId=\"%s\", Version=\"" EA_VERSION "\"", client_id);
    if (token && token[0] && n > 0 && n < cap) n += snprintf(out + n, (size_t)(cap - n), ", Token=\"%s\"", token);
    if (n > 0 && n < cap) snprintf(out + n, (size_t)(cap - n), "\r\n%s", json_body ? "Content-Type: application/json\r\n" : "");
    out[cap - 1] = 0;
}

static void json_escape(const char *in, char *out, int cap)
{
    int o = 0;
    for (; *in && o < cap - 7; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 32) o += sprintf(out + o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = 0;
}

/* "192.168.1.5:8096" -> "http://192.168.1.5:8096" (no trailing slash) */
static void normalize_base(const char *in, char *out, int cap)
{
    size_t n;
    while (*in == ' ') in++;
    if (strncmp(in, "http://", 7) && strncmp(in, "https://", 8)) snprintf(out, (size_t)cap, "http://%s", in);
    else snprintf(out, (size_t)cap, "%s", in);
    out[cap - 1] = 0;
    n = strlen(out);
    while (n && (out[n - 1] == '/' || out[n - 1] == ' ')) out[--n] = 0;
}

int jellyfin_sign_in(const char *server, const char *client_id, const char *user, const char *pass,
                     ea_source *out, char *err, int errcap)
{
    char url[256], h[512], body[400], eu[128], ep[160];
    ea_http r;
    ea_json j;
    int st, ok = 0;
    memset(out, 0, sizeof *out);
    out->type = SOURCE_JELLYFIN;
    normalize_base(server, out->base, (int)sizeof out->base);
    if (strlen(out->base) < 10) { snprintf(err, (size_t)errcap, "enter the server address, like 192.168.1.5:8096"); return 0; }
    json_escape(user, eu, sizeof eu); json_escape(pass, ep, sizeof ep);
    snprintf(body, sizeof body, "{\"Username\":\"%s\",\"Pw\":\"%s\"}", eu, ep);
    snprintf(url, sizeof url, "%s/Users/AuthenticateByName", out->base);
    jf_headers(h, sizeof h, client_id, 0, 1);
    st = net_request("POST", url, h, body, &r, 15000);
    if (st == 401) { snprintf(err, (size_t)errcap, "wrong username or password"); net_free(&r); return 0; }
    if (st != 200) {
        if (st) snprintf(err, (size_t)errcap, "server said HTTP %d - is this a Jellyfin address?", st);
        else snprintf(err, (size_t)errcap, "%s", r.err);
        net_free(&r); return 0;
    }
    if (json_parse(&j, r.body, r.len)) {
        int root = json_root(&j), t = json_get(&j, root, "AccessToken"), u = json_get(&j, json_get(&j, root, "User"), "Id");
        if (json_is_string(&j, t) && json_is_string(&j, u)) {
            json_str(&j, t, out->token, sizeof out->token);
            json_str(&j, u, out->user_id, sizeof out->user_id);
            ok = 1;
        }
        json_free(&j);
    }
    net_free(&r);
    if (!ok) { snprintf(err, (size_t)errcap, "sign-in gave no token"); return 0; }
    /* a friendly name for the account list */
    snprintf(url, sizeof url, "%s/System/Info/Public", out->base);
    strncpy(out->name, user, sizeof out->name - 1);
    if (net_request("GET", url, 0, 0, &r, 6000) == 200 && json_parse(&j, r.body, r.len)) {
        char sn[48];
        json_str(&j, json_get(&j, json_root(&j), "ServerName"), sn, sizeof sn);
        if (sn[0]) snprintf(out->name, sizeof out->name, "%s", sn);
        json_free(&j);
    }
    net_free(&r);
    return 1;
}

static ea_sitem *jf_level(const ea_source *s, const char *client_id, const char *node, int *n, char *err, int errcap)
{
    char url[512], h[512];
    const char *key = strchr(node, '/');
    ea_http r;
    ea_json j;
    ea_sitem *out = 0;
    int root, arr, d, cnt, i = 0, want_tracks = 0, playlists_row = 0;
    const char *child = 0;
    key = key ? key + 1 : "";
    *n = 0;
    if (!node[0]) { snprintf(url, sizeof url, "%s/Users/%s/Views", s->base, s->user_id); child = "lib"; playlists_row = 1; }
    else if (!strncmp(node, "lib/", 4)) { snprintf(url, sizeof url, "%s/Artists/AlbumArtists?ParentId=%s&UserId=%s&SortBy=SortName", s->base, key, s->user_id); child = "artist"; }
    else if (!strncmp(node, "artist/", 7)) { snprintf(url, sizeof url, "%s/Users/%s/Items?AlbumArtistIds=%s&Recursive=true&IncludeItemTypes=MusicAlbum&SortBy=PremiereDate,SortName", s->base, s->user_id, key); child = "album"; }
    else if (!strncmp(node, "album/", 6)) { snprintf(url, sizeof url, "%s/Users/%s/Items?ParentId=%s&SortBy=ParentIndexNumber,IndexNumber", s->base, s->user_id, key); want_tracks = 1; }
    else if (!strcmp(node, "playlists")) { snprintf(url, sizeof url, "%s/Users/%s/Items?Recursive=true&IncludeItemTypes=Playlist", s->base, s->user_id); child = "playlist"; }
    else if (!strncmp(node, "playlist/", 9)) { snprintf(url, sizeof url, "%s/Playlists/%s/Items?UserId=%s", s->base, key, s->user_id); want_tracks = 1; }
    else { snprintf(err, (size_t)errcap, "bad node"); return 0; }
    jf_headers(h, sizeof h, client_id, s->token, 0);
    if (net_request("GET", url, h, 0, &r, 30000) != 200) {
        if (r.status == 401) snprintf(err, (size_t)errcap, "sign-in expired - remove and add the account again");
        else if (r.status) snprintf(err, (size_t)errcap, "%s: HTTP %d", s->name, r.status);
        else snprintf(err, (size_t)errcap, "%s: %s", s->name, r.err);
        net_free(&r); return 0;
    }
    if (!json_parse(&j, r.body, r.len)) { snprintf(err, (size_t)errcap, "server sent an unexpected reply"); net_free(&r); return 0; }
    root = json_root(&j);
    arr = json_get(&j, root, "Items");
    cnt = json_size(&j, arr);
    out = (ea_sitem *)calloc((size_t)(cnt + 2), sizeof *out);
    for (d = json_first(&j, arr); out && d >= 0 && i < cnt; d = json_next(&j, d)) {
        char id[48], type[24];
        json_str(&j, json_get(&j, d, "Id"), id, sizeof id);
        if (!node[0]) {                                           /* root: music libraries only */
            json_str(&j, json_get(&j, d, "CollectionType"), type, sizeof type);
            if (strcmp(type, "music")) continue;
        }
        if (want_tracks) {
            char title[96], artist[64] = "";
            int artists;
            json_str(&j, json_get(&j, d, "Type"), type, sizeof type);
            if (strcmp(type, "Audio")) continue;
            json_str(&j, json_get(&j, d, "Name"), title, sizeof title);
            artists = json_get(&j, d, "Artists");
            if (json_size(&j, artists) > 0) json_str(&j, json_first(&j, artists), artist, sizeof artist);
            else if (json_get(&j, d, "AlbumArtist") >= 0) json_str(&j, json_get(&j, d, "AlbumArtist"), artist, sizeof artist);
            snprintf(out[i].name, sizeof out[i].name, "%s%s%s", artist, artist[0] ? " - " : "", title);
            out[i].name[sizeof out[i].name - 1] = 0;
            out[i].is_track = 1;
            out[i].dur_s = (int)(json_num(&j, json_get(&j, d, "RunTimeTicks")) / 10000000.0);
            /* "universal" lets the server decide: an MP3 is sent as-is, anything
             * else is transcoded - and MP3 is the only thing we say we can take */
            snprintf(out[i].url, sizeof out[i].url,
                     "%s/Audio/%s/universal?UserId=%s&DeviceId=%s&MaxStreamingBitrate=320000&Container=mp3"
                     "&TranscodingContainer=mp3&TranscodingProtocol=http&AudioCodec=mp3", s->base, id, s->user_id, client_id);
        } else {
            json_str(&j, json_get(&j, d, "Name"), out[i].name, sizeof out[i].name);
            snprintf(out[i].node, sizeof out[i].node, "%s/%s", child, id);
        }
        i++;
    }
    if (out && playlists_row) { strcpy(out[i].name, "Playlists"); strcpy(out[i].node, "playlists"); i++; }
    json_free(&j);
    net_free(&r);
    *n = out ? i : 0;
    return out;
}

/* ---- dispatch ----------------------------------------------------------------------------- */

ea_sitem *source_browse(const ea_source *s, const char *client_id, const char *node, int *n, char *err, int errcap)
{
    *n = 0;
    return s->type == SOURCE_JELLYFIN ? jf_level(s, client_id, node, n, err, errcap) : plex_level(s, client_id, node, n, err, errcap);
}

int source_alive(const ea_source *s, const char *client_id)
{
    if (s->type == SOURCE_JELLYFIN) {
        char url[256], h[512];
        ea_http r;
        int st;
        snprintf(url, sizeof url, "%s/Users/%s", s->base, s->user_id);
        jf_headers(h, sizeof h, client_id, s->token, 0);
        st = net_request("GET", url, h, 0, &r, 5000);
        net_free(&r);
        return st == 200;
    } else {
        plex_server p;
        as_plex(s, &p);
        return plex_alive(&p);
    }
}
