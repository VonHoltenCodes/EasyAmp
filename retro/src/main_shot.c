/* easyamp-shot: render the UI to BMP files with no window system at all.
 * This is how the look is reviewed on the build machine, and it doubles as a
 * smoke test that every page and overlay state draws without crashing. */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_bmp(const char *path, const ea_surface *s)
{
    FILE *f = fopen(path, "wb");
    int row = (s->w * 3 + 3) & ~3, size = 54 + row * s->h, x, y;
    unsigned char hdr[54], *line;
    if (!f) return 0;
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (unsigned char)size; hdr[3] = (unsigned char)(size >> 8); hdr[4] = (unsigned char)(size >> 16); hdr[5] = (unsigned char)(size >> 24);
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = (unsigned char)s->w; hdr[19] = (unsigned char)(s->w >> 8);
    hdr[22] = (unsigned char)s->h; hdr[23] = (unsigned char)(s->h >> 8);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    line = (unsigned char *)calloc(1, (size_t)row);
    for (y = s->h - 1; y >= 0; y--) {
        for (x = 0; x < s->w; x++) {
            ea_px p = s->px[y * s->w + x];
            line[x * 3] = (unsigned char)EA_B(p); line[x * 3 + 1] = (unsigned char)EA_G(p); line[x * 3 + 2] = (unsigned char)EA_R(p);
        }
        fwrite(line, 1, (size_t)row, f);
    }
    free(line);
    fclose(f);
    return 1;
}

/* the same picture as a High Color (5-6-5) desktop shows it */
static void shot16(ea_ui *ui, const char *dir, const char *name, int dither)
{
    ea_surface *s = ui_surface(ui), out;
    static unsigned short buf16[EA_WIN_W * EA_WIN_H];
    static ea_px px[EA_WIN_W * EA_WIN_H];
    char path[512];
    int i;
    if (dither) gfx_dither16(s, 0, 0, s->w, s->h, buf16, s->w * 2, 6);
    for (i = 0; i < s->w * s->h; i++) {
        int r, g, b;
        if (dither) { r = buf16[i] >> 11; g = (buf16[i] >> 5) & 63; b = buf16[i] & 31; }
        else { r = EA_R(s->px[i]) >> 3; g = EA_G(s->px[i]) >> 2; b = EA_B(s->px[i]) >> 3; }     /* GDI: truncate */
        px[i] = EA_RGB(r * 255 / 31, g * 255 / 63, b * 255 / 31);
    }
    gfx_init(&out, s->w, s->h, px);
    sprintf(path, "%s/%s.bmp", dir, name);
    write_bmp(path, &out);
    printf("%s\n", path);
}

/* the same picture on a palettized desktop */
static void shot_indexed(ea_ui *ui, const char *dir, const char *name, const unsigned char *pal,
                         const unsigned char *lut, int spread)
{
    ea_surface *s = ui_surface(ui), out;
    static unsigned char idx[EA_WIN_W * EA_WIN_H];
    static ea_px px[EA_WIN_W * EA_WIN_H];
    char path[512];
    int i;
    gfx_dither_indexed(s, 0, 0, s->w, s->h, idx, s->w, lut, spread);
    for (i = 0; i < s->w * s->h; i++) px[i] = EA_RGB(pal[idx[i] * 3], pal[idx[i] * 3 + 1], pal[idx[i] * 3 + 2]);
    gfx_init(&out, s->w, s->h, px);
    sprintf(path, "%s/%s.bmp", dir, name);
    write_bmp(path, &out);
    printf("%s\n", path);
}

static void demo(ea_model *m, ea_track *tracks)
{
    static const float spec[EA_VIZ_BANDS] = { 0.48f, 0.62f, 0.66f, 0.82f, 0.62f, 0.42f, 0.38f, 0.37f, 0.34f, 0.33f,
                                              0.34f, 0.25f, 0.29f, 0.28f, 0.19f, 0.13f, 0.06f, 0.03f, 0.0f, 0.0f };
    int i;
    ea_model_init(m);
    strcpy(tracks[0].title, "Aerosmith - Cryin'");
    strcpy(tracks[1].title, "Aerosmith - Crazy");
    strcpy(tracks[2].title, "Aerosmith - Gotta Love It");
    m->tracks = tracks; m->ntracks = 3; m->sel = 0; m->cur = 0;
    m->state = EA_PLAYING; m->pos_ms = 11000; m->dur_ms = 309000; m->kbps = 128; m->khz = 44; m->stereo = 1;
    strcpy(m->title, "Aerosmith - Cryin'");
    ea_preset_apply(m, 1);
    for (i = 0; i < EA_VIZ_BANDS; i++) m->levels[i] = spec[i];
    for (i = 0; i < EA_WAVE; i++) m->wave[i] = 0.55f * (float)(sin(i * 0.45) * cos(i * 0.13) + 0.3 * sin(i * 1.9));
    m->vu_l = 0.84f; m->vu_r = 0.86f;
}

static void shot(ea_ui *ui, const char *dir, const char *name)
{
    ea_rect d[64];
    char path[512];
    ui_render(ui, d, 64);
    sprintf(path, "%s/%s.bmp", dir, name);
    if (!write_bmp(path, ui_surface(ui))) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    printf("%s\n", path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    static ea_track tracks[64];
    ea_model m;
    ea_ui *ui;
    int i;
    demo(&m, tracks);
    ui = ui_create(&m, 0, 0);
    if (!ui) return 1;
    ui_model_changed(ui, UI_CH_ALL);
    shot(ui, dir, "player");
    shot16(ui, dir, "player-16bit-gdi", 0);
    shot16(ui, dir, "player-16bit-dithered", 1);
    shot_indexed(ui, dir, "player-256", EA_PAL256, EA_LUT256, 20);
    shot_indexed(ui, dir, "player-vga16", EA_PAL16, EA_LUT16, 96);
    m.viz_vu = 1; ui_model_changed(ui, UI_CH_VIZ);
    ui_mouse_move(ui, 60, 288);                                  /* hover a transport button */
    shot(ui, dir, "player-vu-hover");
    m.viz_vu = 0;
    ui_mouse_move(ui, 380, 352); ui_mouse_down(ui, 380, 352); ui_mouse_up(ui, 380, 352);
    ui_mouse_move(ui, 380, 300);
    shot(ui, dir, "player-presets-menu");
    ui_key(ui, UI_KEY_ESC);
    for (i = 3; i < 40; i++) sprintf(tracks[i].title, "Various Artists - Track number %d with a long name", i + 1);
    m.ntracks = 40; m.state = EA_PAUSED;
    strcpy(m.title, "Aerosmith - Get A Grip (Remastered 2012 Deluxe Edition)");
    ui_model_changed(ui, UI_CH_ALL);
    ui_mouse_move(ui, 110, 290); ui_mouse_down(ui, 110, 290);    /* hold stop down */
    shot(ui, dir, "player-long-paused-pressed");
    ui_mouse_up(ui, 110, 290);
    m.show_eq = 0; ui_model_changed(ui, UI_CH_ALL); shot(ui, dir, "player-no-eq");
    m.show_pl = 0; m.viz_vu = 1; ui_model_changed(ui, UI_CH_ALL); shot(ui, dir, "player-no-eq-no-pl-vu");
    m.show_eq = 1; m.viz_vu = 0; ui_model_changed(ui, UI_CH_ALL); shot(ui, dir, "player-no-pl");
    m.show_pl = 1; ui_model_changed(ui, UI_CH_ALL);
    ui_set_page(ui, EA_PAGE_EQ);
    shot(ui, dir, "equalizer");
    shot_indexed(ui, dir, "equalizer-256", EA_PAL256, EA_LUT256, 20);
    shot_indexed(ui, dir, "equalizer-vga16", EA_PAL16, EA_LUT16, 96);
    ea_set_nbands(&m, 24); m.selband = 7; m.balance = -0.3f; m.pitch = 1.04f;
    ui_model_changed(ui, UI_CH_EQ);
    shot(ui, dir, "equalizer-24");
    ui_set_page(ui, EA_PAGE_SOURCES);
    shot(ui, dir, "sources");
    m.link_open = 1; strcpy(m.link_code, "B8Z2"); strcpy(m.link_status, "WAITING FOR APPROVAL...");
    ui_model_changed(ui, UI_CH_SOURCES);
    ui_mouse_move(ui, 360, 350);
    shot(ui, dir, "sources-link");
    strcpy(m.link_code, "S5O0"); ui_model_changed(ui, UI_CH_SOURCES); shot(ui, dir, "sources-link-2");
    strcpy(m.link_code, "G6I1"); ui_model_changed(ui, UI_CH_SOURCES); shot(ui, dir, "sources-link-3");
    {
        static ea_srcitem lib[60];
        static const char *names[] = { "AC/DC", "Aerosmith", "Alice in Chains", "The Beatles", "Beyonce", "Black Sabbath", "Blondie",
            "Bob Dylan", "Creedence Clearwater Revival", "David Bowie", "Deep Purple", "Dire Straits", "The Doors", "Eagles" };
        m.link_open = 0; m.form_open = 1; m.form_focus = 2;
        strcpy(m.form_field[0], "192.168.68.72:8096"); strcpy(m.form_field[1], "trent"); strcpy(m.form_field[2], "secret99");
        strcpy(m.form_status, "SIGNING IN...");
        ui_model_changed(ui, UI_CH_SOURCES); ui_tick(ui, 500); ui_mouse_move(ui, 300, 390);
        shot(ui, dir, "sources-jellyfin-form");
        m.form_open = 0;
        m.naccts = 2; m.acct_sel = 0;
        strcpy(m.accts[0].name, "Plex  starbase1"); m.accts[0].state = EA_SRC_OK;
        strcpy(m.accts[1].name, "Jellyfin  attic-nas"); m.accts[1].state = EA_SRC_UNREACHABLE;
        strcpy(m.src_crumb, "STARBASE1 > MUSIC"); strcpy(m.src_status, "60 ARTISTS");
        for (i = 0; i < 60; i++) { sprintf(lib[i].name, "%s%s", names[i % 14], i >= 14 ? " (more)" : ""); lib[i].container = 1; }
        m.src_items = lib; m.src_nitems = 60; m.src_sel = 1;
        lib[1].marked = lib[3].marked = lib[4].marked = lib[5].marked = lib[9].marked = 1; strcpy(m.src_status, "5 SELECTED");
        ui_model_changed(ui, UI_CH_SOURCES);
        shot(ui, dir, "sources-browse");
        shot_indexed(ui, dir, "sources-browse-256", EA_PAL256, EA_LUT256, 20);
    }
    ui_destroy(ui);
    return 0;
}
