/* source - one interface over the music servers the client can browse.
 * A source is an account on a server; browsing it yields folders (which have
 * a node to browse next) and tracks (which have a playable URL, stored
 * WITHOUT credentials - source_auth_url adds them at play time, so saved
 * playlists never carry a token). Blocking: call from a worker thread. */
#ifndef EA_SOURCE_H
#define EA_SOURCE_H

enum { SOURCE_PLEX, SOURCE_JELLYFIN };

typedef struct {
    int  type;
    char name[64];           /* shown in the account list */
    char base[128];          /* http://host:port */
    char token[96];          /* server access token */
    char acct[96];           /* Plex: account token, to re-discover a server that moved */
    char user_id[48];        /* Jellyfin */
} ea_source;

typedef struct {
    int  is_track;
    char node[96];           /* folders: what to browse next */
    char name[128];
    char url[700];           /* tracks: credential-free stream URL */
    int  dur_s;
} ea_sitem;

/* node "" is the root. Returns a malloc'd array (free it), NULL on failure. */
ea_sitem *source_browse(const ea_source *s, const char *client_id, const char *node, int *n, char *err, int errcap);
int  source_alive(const ea_source *s, const char *client_id);
int  source_owns_url(const ea_source *s, const char *url);
void source_auth_url(const ea_source *s, const char *url, char *out, int cap);
const char *source_type_name(int type);

/* Jellyfin sign-in: fills *out (name, base, token, user_id). 1 ok / 0 failed. */
int  jellyfin_sign_in(const char *server, const char *client_id, const char *user, const char *pass,
                      ea_source *out, char *err, int errcap);

#endif
