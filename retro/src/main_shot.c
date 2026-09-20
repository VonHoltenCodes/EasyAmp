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
    ui_set_page(ui, EA_PAGE_EQ);
    shot(ui, dir, "equalizer");
    ea_set_nbands(&m, 24); m.selband = 7; m.balance = -0.3f; m.pitch = 1.04f;
    ui_model_changed(ui, UI_CH_EQ);
    shot(ui, dir, "equalizer-24");
    ui_set_page(ui, EA_PAGE_SOURCES);
    shot(ui, dir, "sources");
    ui_destroy(ui);
    return 0;
}
