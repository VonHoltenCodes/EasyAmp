/* app - the state the UI shows and edits. Platform-neutral: the Win32 shell
 * and the audio engine fill it in, the UI reads it and calls ea_actions. */
#ifndef EA_APP_H
#define EA_APP_H

#define EA_VERSION     "0.1.1"
#define EA_MAX_BANDS   32
#define EA_MIN_BANDS   10
#define EA_GRAPHIC_N   10
#define EA_VIZ_BANDS   20
#define EA_WAVE        64

#define EA_BAND_MIN  (-24.0f)
#define EA_BAND_MAX  ( 12.0f)
#define EA_PRE_MIN   (-12.0f)
#define EA_PRE_MAX   ( 12.0f)
#define EA_DEFAULT_Q 1.41f

enum { EA_PAGE_PLAYER, EA_PAGE_EQ, EA_PAGE_SOURCES, EA_PAGE_COUNT };
enum { EA_STOPPED, EA_PLAYING, EA_PAUSED };

enum {  /* ea_actions.command ids */
    EA_CMD_EJECT = 1, EA_CMD_PREV, EA_CMD_PLAYPAUSE, EA_CMD_STOP, EA_CMD_NEXT,
    EA_CMD_PL_ADD, EA_CMD_PL_REMOVE, EA_CMD_PL_CLEAR, EA_CMD_PL_LOAD, EA_CMD_PL_SAVE,
    EA_CMD_EQ_IMPORT, EA_CMD_EQ_EXPORT_APO, EA_CMD_EQ_EXPORT_GEQ,
    EA_CMD_WIN_MINIMIZE, EA_CMD_WIN_CLOSE, EA_CMD_OPEN_UPDATE,
    EA_CMD_SRC_LINK, EA_CMD_SRC_LINK_CANCEL, EA_CMD_SRC_JELLYFIN, EA_CMD_SRC_REMOVE,
    EA_CMD_SRC_BACK, EA_CMD_SRC_PLAY, EA_CMD_SRC_ADD
};

typedef struct { char title[160]; int dur_s; } ea_track;
typedef struct { char name[128]; int container; } ea_srcitem;      /* a row in the library browser */

enum { EA_SRC_NONE, EA_SRC_OK, EA_SRC_UNREACHABLE };

typedef struct ea_model {
    int   page;
    /* transport + now playing */
    int   state, pos_ms, dur_ms, kbps, khz, stereo;
    char  title[256];
    /* playlist */
    ea_track *tracks;
    int   ntracks, sel, cur;
    /* equalizer */
    int   eq_on, bass, loud;
    float preamp;
    int   nbands, selband;
    float gains[EA_MAX_BANDS], freqs[EA_MAX_BANDS], q[EA_MAX_BANDS];
    float in_gain, out_gain, balance, pitch;
    char  preset[32];
    /* visualizer feed (0..1 levels, -1..1 wave) */
    int   viz_vu;
    float levels[EA_VIZ_BANDS], wave[EA_WAVE], vu_l, vu_r;
    /* sources page: one linked Plex account and the level being browsed */
    int   src_state;                 /* EA_SRC_* */
    char  src_name[64], src_crumb[160], src_status[96];
    ea_srcitem *src_items;
    int   src_nitems, src_sel, src_busy;
    int   link_open;                 /* the "enter this code" dialog */
    char  link_code[8], link_status[96];
    /* footer */
    int   update_avail;
    char  latest[16];
} ea_model;

typedef struct ea_actions {
    void *ctx;
    void (*command)(void *ctx, int cmd);
    void (*seek)(void *ctx, float fraction);
    void (*play_index)(void *ctx, int index);
    void (*eq_changed)(void *ctx);          /* engine re-reads the model */
    void (*src_open)(void *ctx, int index); /* -1 = the account itself (library root) */
} ea_actions;

void  ea_model_init(ea_model *m);
void  ea_band_freqs(int n, float *out);
float ea_interp(float x, const float *xs, const float *ys, int n);
void  ea_fmt_freq(float f, char *out);
void  ea_set_nbands(ea_model *m, int n);              /* keeps the curve */
void  ea_graphic_get(const ea_model *m, float *ten);  /* the 10-band view */
void  ea_graphic_set(ea_model *m, const float *ten);
int   ea_preset_count(void);
const char *ea_preset_name(int i);
void  ea_preset_apply(ea_model *m, int i);

#endif
