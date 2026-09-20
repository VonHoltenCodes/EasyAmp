/* plex - the Plex client. Same flow as the GTK app's sources/plex.py:
 *   link: POST plex.tv/api/v2/pins -> show the 4-character code -> the user
 *         enters it at plex.tv/link ON ANOTHER DEVICE (a phone) -> poll
 *   discover: plex.tv/api/v2/resources -> pick a reachable server
 *   browse: sections -> artists -> albums -> tracks, all on the server itself
 * Only the plex.tv calls need TLS. The server is spoken to over plain HTTP on
 * the LAN, so browsing and streaming cost the old CPU nothing extra.
 * Blocking calls: run them on a worker thread. */
#ifndef EA_PLEX_H
#define EA_PLEX_H

enum { PLEX_SECTION, PLEX_ARTIST, PLEX_ALBUM, PLEX_TRACK };

typedef struct {
    char name[64], base[96], token[64], machine[48];
} plex_server;

typedef struct {
    int  kind;
    char key[24];            /* ratingKey (or section key) */
    char name[128];          /* display name, folded to ASCII */
    char artist[64];
    char part[200];          /* tracks: the media part path on the server */
    char codec[12];          /* tracks: audio codec */
    int  dur_s;
} plex_item;

/* 1 ok / 0 failed (err filled). */
int plex_pin_start(const char *client_id, long *pin_id, char code[8], char *err, int errcap);
/* 1 linked (token filled), 0 still waiting, -1 failed or expired */
int plex_pin_poll(const char *client_id, long pin_id, char *token, int tokcap, char *err, int errcap);
/* number of servers written (0 with err set when none reachable) */
int plex_discover(const char *client_id, const char *account_token, plex_server *out, int max, char *err, int errcap);
int plex_alive(const plex_server *s);
/* node: "" (sections), "section/<key>", "artist/<key>", "album/<key>". Returns a
 * malloc'd array (free it) and its length through *n; NULL on failure. */
plex_item *plex_browse(const plex_server *s, const char *client_id, const char *node, int *n, char *err, int errcap);
/* token-free URL for the playlist, and the playable one with the token added */
void plex_track_url(const plex_server *s, const plex_item *t, char *out, int cap);
void plex_auth_url(const plex_server *s, const char *url, char *out, int cap);

#endif
