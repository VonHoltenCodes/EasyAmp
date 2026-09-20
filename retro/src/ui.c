#include "ui.h"
#include "fonts_gen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- palette (straight from the GTK app's style.css) -------------------- */
#define C_CHASSIS   EA_RGB(0x16, 0x16, 0x1c)
#define C_BAR       EA_RGB(0x36, 0x40, 0x6e)
#define C_BAR_HI    EA_RGB(0x7d, 0x8e, 0xc0)
#define C_BAR_LO    EA_RGB(0x09, 0x0b, 0x14)
#define C_TITLE     EA_RGB(0xc2, 0xcc, 0xf0)
#define C_PANELLBL  EA_RGB(0xb2, 0xbc, 0xe8)
#define C_GOLD      EA_RGBF(0.85f, 0.70f, 0.24f)
#define C_METAL     EA_RGB(0x24, 0x24, 0x2c)
#define C_METAL_HI  EA_RGB(0x50, 0x50, 0x5e)
#define C_METAL_LO  EA_RGB(0x06, 0x06, 0x0a)
#define C_BTN       EA_RGB(0xc4, 0xc4, 0xcc)
#define C_BTN_HOVER EA_RGB(0xd0, 0xd0, 0xd8)
#define C_BTN_DOWN  EA_RGB(0xae, 0xae, 0xb6)
#define C_BTN_INK   EA_RGB(0x1e, 0x1e, 0x24)
#define C_BTN_DARK  EA_RGB(0x2c, 0x2c, 0x32)
#define C_BTN_RIDGE EA_RGB(0xed, 0xed, 0xf2)
#define C_BTN_SHADE EA_RGB(0x8a, 0x8a, 0x92)
#define C_LCD       EA_RGB(0x1e, 0xff, 0x1e)
#define C_LCD_ON    EA_RGB(0x2b, 0xff, 0x2b)
#define C_IND_OFF   EA_RGB(0x16, 0x6e, 0x16)
#define C_WELL_EDGE EA_RGB(0x2a, 0x2a, 0x32)
#define C_DISP_EDGE EA_RGB(0x34, 0x34, 0x3e)
#define C_PLAYLIST  EA_RGB(0x0a, 0x0a, 0x10)
#define C_SELECT    EA_RGB(0x1b, 0x3a, 0xa0)
#define C_FOOT      EA_RGB(0x14, 0x14, 0x1a)
#define C_FOOT_DIM  EA_RGB(0x3f, 0x7a, 0x3f)
#define C_EQLABEL   EA_RGBF(0.42f, 0.58f, 0.85f)
#define C_CTLLABEL  EA_RGB(0x6f, 0xa0, 0xd8)
#define C_WHITE     EA_RGB(255, 255, 255)
#define C_BLACK     EA_RGB(0, 0, 0)

/* ---- layout -------------------------------------------------------------- */
static const ea_rect R_TITLE   = {   3,   3, 724,  26 };
static const ea_rect R_FOOTER  = {   3, 554, 724,  21 };
/* player page */
static const ea_rect R_DISPLAY = {   3,  32, 436,  96 };
static const ea_rect R_VIZ     = {   3, 130, 436, 138 };
static const ea_rect R_XPORT   = {   3, 270, 436,  38 };
static const ea_rect R_EQBAR   = {   3, 311, 436,  24 };
static const ea_rect R_EQCTL   = {   3, 335, 436,  36 };
static const ea_rect R_EQSMALL = {   3, 373, 436, 178 };
static const ea_rect R_PLBAR   = { 442,  32, 285,  24 };
static const ea_rect R_PLLIST  = { 442,  56, 285, 455 };
static const ea_rect R_PLBTNS  = { 442, 513, 285,  38 };
/* equalizer page */
static const ea_rect R_EQTOP   = {   3,  32, 724,  42 };
static const ea_rect R_LOGSPEC = {   3,  76, 512,  70 };
static const ea_rect R_WAVE    = { 518,  76, 209,  70 };
static const ea_rect R_BANK    = {   3, 148, 724, 206 };
static const ea_rect R_LOWER   = {   3, 356, 724, 195 };
static const ea_rect R_LEDMTR  = {  10, 363, 710,  44 };

#define PL_ROW_H   18
#define SMALL_CURVE_H 30
#define SMALL_LABEL_H 14
#define BANK_CURVE_H  34
#define BANK_LABEL_H  16

/* ---- widgets --------------------------------------------------------------- */
enum { K_XPORT, K_LEDBTN, K_TOGGLE, K_BUTTON, K_MENUBTN, K_STACKBTN, K_WINBTN, K_TAB,
       K_FOOTSTAT, K_STATUS, K_INFO, K_SCOPE, K_SEEK, K_VIZ, K_EQSMALL, K_PLAYLIST,
       K_EQTIME, K_LOGSPEC, K_WAVE, K_BANK, K_LEDMETER, K_KNOB };

enum { IC_EJECT, IC_PREV, IC_PLAY, IC_PAUSE, IC_STOP, IC_NEXT, IC_MIN, IC_CLOSE };

enum { ID_NONE, ID_EQ_SHOW, ID_PL_SHOW, ID_VU, ID_EQ_ON, ID_BASS, ID_LOUD, ID_PRESETS,
       ID_PRESETS2, ID_IMPORT, ID_EXPORT, ID_RESET, ID_TAB0, ID_TAB1, ID_TAB2,
       ID_K_BANDS, ID_K_PREAMP, ID_K_IN, ID_K_OUT, ID_K_BAL, ID_K_PITCH, ID_K_FREQ, ID_K_Q };

#define PG(p) (1 << (p))
#define PG_ALL 7

typedef struct {
    int id, kind, pages;
    ea_rect r;
    const char *label;
    int arg;                       /* command id or icon */
    int hover, down, dirty;
    float vmin, vmax, vstep, vdef; /* knobs */
} widget;

#define MAX_WIDGETS 80
#define MAX_MENU 12

struct ea_ui {
    ea_model *m;
    ea_actions act;
    ea_surface fb, bg;
    int own_pixels, bg_page, full_dirty;
    widget w[MAX_WIDGETS];
    int nw, capture, hover;
    int press_x, press_y;
    float press_v;
    /* playlist view */
    int pl_scroll, pl_thumb_drag, pl_thumb_off;
    /* marquee */
    int mq_pos, mq_acc;
    /* viz peak holds */
    float peaks[EA_VIZ_BANDS], pk_l, pk_r;
    /* overlay menu */
    struct { int open, owner, n, hover; ea_rect r; const char *items[MAX_MENU]; } menu;
    int show_eq, show_pl;
};

/* ======================================================================== */
/*  small drawing helpers                                                    */
/* ======================================================================== */

static int text_base(const ea_font *f, int y, int h)
{
    return y + (h - (f->ascent + f->descent)) / 2 + f->ascent;
}

static void text_center(ea_surface *s, const ea_font *f, const ea_rect *r, const char *t,
                        ea_px c, int spacing, int dy)
{
    int w = gfx_text_w(f, t, spacing) - spacing;
    gfx_text(s, f, r->x + (r->w - w) / 2, text_base(f, r->y, r->h) + dy, t, c, spacing, 0, 0);
}

static void metal_panel(ea_surface *s, const ea_rect *r)
{
    gfx_fill(s, r->x, r->y, r->w, r->h, C_METAL);
    gfx_scanlines(s, r->x, r->y, r->w, r->h, 8, 6, 31);
    gfx_frame(s, r->x, r->y, r->w, r->h, C_METAL_HI, C_METAL_LO);
}

static void well(ea_surface *s, const ea_rect *r, ea_px fill, ea_px edge, int shadow, int strength)
{
    gfx_fill(s, r->x, r->y, r->w, r->h, fill);
    if (shadow) gfx_inner_shadow(s, r->x + 1, r->y + 1, r->w - 2, r->h - 2, shadow, strength);
    gfx_frame(s, r->x, r->y, r->w, r->h, C_BLACK, edge);
}

/* the blue-black "smoke" behind both EQ slider banks */
static void smoke(ea_surface *s, const ea_rect *r)
{
    static const ea_stop st[3] = { { 0.0f, EA_RGB(28, 46, 102) }, { 0.45f, EA_RGB(13, 23, 51) }, { 1.0f, EA_RGB(0, 0, 0) } };
    float rad = (float)(r->w > r->h ? r->w : r->h) * 1.15f;
    gfx_fill(s, r->x, r->y, r->w, r->h, C_BLACK);
    gfx_radial_rect(s, r->x, r->y, r->w, r->h, (float)(r->x + r->w), (float)(r->y + r->h), rad, rad, st, 3, 255);
    gfx_frame(s, r->x, r->y, r->w, r->h, C_BLACK, C_WELL_EDGE);
}

static void lcd_well(ea_surface *s, const ea_rect *r)
{
    static const ea_stop st[3] = { { 0.0f, EA_RGB(0x17, 0x28, 0x5c) }, { 0.42f, EA_RGB(0x0a, 0x14, 0x30) }, { 0.82f, EA_RGB(0, 0, 0) } };
    gfx_fill(s, r->x, r->y, r->w, r->h, C_BLACK);
    gfx_radial_rect(s, r->x, r->y, r->w, r->h, (float)(r->x + r->w), (float)(r->y + r->h),
                    r->w * 1.5f, r->h * 1.5f, st, 3, 255);
    gfx_inner_shadow(s, r->x + 1, r->y + 1, r->w - 2, r->h - 2, 14, 200);
    gfx_frame(s, r->x, r->y, r->w, r->h, C_BLACK, C_DISP_EDGE);
}

static void gold_bars(ea_surface *s, int x0, int x1, int cy)
{
    if (x1 - x0 < 8) return;
    gfx_line(s, x0 + 2.0f, cy - 3 + 0.5f, x1 - 2.0f, cy - 3 + 0.5f, 1.5f, C_GOLD, 255);
    gfx_line(s, x0 + 2.0f, cy + 3 + 0.5f, x1 - 2.0f, cy + 3 + 0.5f, 1.5f, C_GOLD, 255);
}

/* blue textured bar: gold bars | LABEL | gold bars (stops short of `right_pad`) */
static void title_bar(ea_surface *s, const ea_rect *r, const char *label, const ea_font *f,
                      ea_px col, int spacing, int right_pad)
{
    int tw = gfx_text_w(f, label, spacing) - spacing;
    int tx = r->x + (r->w - tw) / 2, base = text_base(f, r->y, r->h), cy = r->y + r->h / 2;
    gfx_fill(s, r->x, r->y, r->w, r->h, C_BAR);
    gfx_scanlines(s, r->x, r->y, r->w, r->h, 15, 12, 41);
    gfx_fill(s, r->x, r->y, r->w, 1, C_BAR_HI);
    gfx_fill(s, r->x, r->y + r->h - 1, r->w, 1, C_BAR_LO);
    gold_bars(s, r->x + 6, tx - 8, cy);
    gold_bars(s, tx + tw + 8, r->x + r->w - 6 - right_pad, cy);
    gfx_text(s, f, tx, base - 1, label, gfx_mix(C_BAR, C_WHITE, 0.15f), spacing, 0, 0);  /* top light */
    gfx_text(s, f, tx, base + 1, label, gfx_mix(C_BAR, C_BLACK, 0.90f), spacing, 0, 0);  /* drop shadow */
    gfx_text(s, f, tx, base, label, col, spacing, 0, 0);
}

static void button_face(ea_surface *s, const ea_rect *r, int hover, int down)
{
    gfx_fill(s, r->x, r->y, r->w, r->h, down ? C_BTN_DOWN : (hover ? C_BTN_HOVER : C_BTN));
    if (down) {
        gfx_frame(s, r->x, r->y, r->w, r->h, C_BTN_DARK, C_WHITE);
        gfx_fill(s, r->x + 1, r->y + 1, r->w - 2, 1, C_BTN_SHADE);
        gfx_fill(s, r->x + 1, r->y + 1, 1, r->h - 2, C_BTN_SHADE);
    } else {
        gfx_frame(s, r->x, r->y, r->w, r->h, C_WHITE, C_BTN_DARK);
        gfx_frame(s, r->x + 1, r->y + 1, r->w - 2, r->h - 2, C_BTN_RIDGE, C_BTN_SHADE);
    }
}

static void button_text(ea_surface *s, const ea_font *f, int x, int base, const char *t, ea_px ink, int spacing)
{
    gfx_text(s, f, x, base + 1, t, EA_RGB(0xe2, 0xe2, 0xe8), spacing, 0, 0);   /* 50% white under-light */
    gfx_text(s, f, x, base, t, ink, spacing, 0, 0);
}

static void led(ea_surface *s, int x, int y, int on)
{
    if (on) {
        gfx_glow_rect(s, x + 1, y + 1, 8, 8, 6, C_LCD_ON, 210);
        gfx_fill(s, x, y, 10, 10, C_LCD_ON);
        gfx_frame(s, x, y, 10, 10, EA_RGB(0x04, 0x0a, 0x04), EA_RGB(0x1c, 0x3c, 0x1c));
    } else {
        gfx_fill(s, x, y, 10, 10, EA_RGB(0x0b, 0x2a, 0x0b));
        gfx_frame(s, x, y, 10, 10, EA_RGB(0x04, 0x0a, 0x04), EA_RGB(0x1c, 0x3c, 0x1c));
    }
}

static void down_arrow(ea_surface *s, float cx, float cy, ea_px c)
{
    float t[6];
    t[0] = cx - 4.5f; t[1] = cy - 2.5f; t[2] = cx + 4.5f; t[3] = cy - 2.5f; t[4] = cx; t[5] = cy + 3.0f;
    gfx_poly(s, t, 3, c, 255);
}

static void icon(ea_surface *s, int kind, const ea_rect *r, ea_px ink)
{
    /* a square glyph centred in r, same proportions as the GTK TransportIcon */
    float w = (float)(r->h - 2), h = w, ox = r->x + (r->w - w) / 2.0f, oy = r->y + (r->h - h) / 2.0f;
    float top = oy + h * 0.28f, bot = oy + h * 0.72f, mid = oy + h * 0.5f, bar = w * 0.085f, t[6];
#define TRI(ax, ay, bx, by, cx, cy) do { t[0]=ax; t[1]=ay; t[2]=bx; t[3]=by; t[4]=cx; t[5]=cy; gfx_poly(s, t, 3, ink, 255); } while (0)
    switch (kind) {
    case IC_PLAY:  TRI(ox + w * 0.36f, top, ox + w * 0.36f, bot, ox + w * 0.70f, mid); break;
    case IC_PAUSE: gfx_rectf(s, ox + w * 0.36f, top, bar * 1.4f, bot - top, ink, 255);
                   gfx_rectf(s, ox + w * 0.55f, top, bar * 1.4f, bot - top, ink, 255); break;
    case IC_STOP:  gfx_rectf(s, ox + w * 0.5f - (bot - top) / 2, top, bot - top, bot - top, ink, 255); break;
    case IC_PREV:  gfx_rectf(s, ox + w * 0.28f, top, bar, bot - top, ink, 255);
                   TRI(ox + w * 0.66f, top, ox + w * 0.66f, bot, ox + w * 0.42f, mid); break;
    case IC_NEXT:  TRI(ox + w * 0.34f, top, ox + w * 0.34f, bot, ox + w * 0.58f, mid);
                   gfx_rectf(s, ox + w * 0.64f - bar, top, bar, bot - top, ink, 255); break;
    case IC_EJECT: TRI(ox + w * 0.5f, top, ox + w * 0.30f, mid, ox + w * 0.70f, mid);
                   gfx_rectf(s, ox + w * 0.30f, bot - h * 0.08f, w * 0.40f, h * 0.08f, ink, 255); break;
    case IC_MIN:   gfx_fill(s, r->x + 4, r->y + r->h - 6, r->w - 8, 2, ink); break;
    case IC_CLOSE: gfx_line(s, r->x + 5.0f, r->y + 4.5f, r->x + r->w - 5.0f, r->y + r->h - 4.5f, 1.6f, ink, 255);
                   gfx_line(s, r->x + r->w - 5.0f, r->y + 4.5f, r->x + 5.0f, r->y + r->h - 4.5f, 1.6f, ink, 255); break;
    }
#undef TRI
}

static ea_px lerp3(const float *a, const float *b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return EA_RGBF(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t);
}

/* gain colour: yellow at 0, warming to red when raised, cooling to green when cut */
static ea_px value_color(float v, float vmin, float vmax)
{
    static const float yellow[3] = { 0.92f, 0.82f, 0.14f }, orange[3] = { 0.93f, 0.52f, 0.10f },
                       red[3] = { 0.90f, 0.13f, 0.10f }, green[3] = { 0.16f, 0.82f, 0.18f };
    float f;
    if (v >= 0) {
        f = vmax != 0 ? v / vmax : 0;
        if (f > 1) f = 1;
        return f < 0.5f ? lerp3(yellow, orange, f / 0.5f) : lerp3(orange, red, (f - 0.5f) / 0.5f);
    }
    f = vmin != 0 ? v / vmin : 0;
    return lerp3(yellow, green, f > 1 ? 1 : f);
}

/* light-grey beveled slider thumb with a dark "=" grip */
static void thumb(ea_surface *s, float cx, float cy, float tw, float th)
{
    int x = (int)floor(cx - tw / 2 + 0.5f), y = (int)floor(cy - th / 2 + 0.5f), w = (int)(tw + 0.5f), h = (int)(th + 0.5f);
    int mx = x + w / 2, my = y + h / 2;
    gfx_fill(s, x, y, w, h, EA_RGBF(0.80f, 0.80f, 0.84f));
    gfx_frame(s, x, y, w, h, EA_RGBF(0.94f, 0.94f, 0.98f), EA_RGBF(0.30f, 0.30f, 0.34f));
    gfx_fill(s, mx - 4, my - 2, 8, 1, EA_RGBF(0.22f, 0.22f, 0.26f));
    gfx_fill(s, mx - 4, my + 1, 8, 1, EA_RGBF(0.22f, 0.22f, 0.26f));
}

/* ======================================================================== */
/*  static chrome: painted once per page into ui->bg                          */
/* ======================================================================== */

static void chrome_common(ea_ui *ui)
{
    ea_surface *s = &ui->bg;
    char ver[48];
    (void)ver;
    gfx_fill(s, 0, 0, EA_WIN_W, EA_WIN_H, C_CHASSIS);
    gfx_scanlines(s, 0, 0, EA_WIN_W, EA_WIN_H, 6, 5, 26);
    gfx_frame(s, 0, 0, EA_WIN_W, EA_WIN_H, C_METAL_HI, C_METAL_LO);
    title_bar(s, &R_TITLE, "EASYAMP", &EA_FONT_TITLE, C_TITLE, 3, 46);
    gfx_fill(s, R_FOOTER.x, R_FOOTER.y, R_FOOTER.w, R_FOOTER.h, C_FOOT);
    gfx_fill(s, R_FOOTER.x, R_FOOTER.y, R_FOOTER.w, 1, C_BTN_DARK);
}

static void separator(ea_surface *s, int x, int y, int h)
{
    gfx_fill(s, x, y, 2, h, EA_RGB(0x05, 0x05, 0x08));
    gfx_fill_a(s, x + 2, y, 1, h, C_WHITE, 26);
}

static void chrome_player(ea_ui *ui)
{
    ea_surface *s = &ui->bg;
    lcd_well(s, &R_DISPLAY);
    well(s, &R_VIZ, C_BLACK, C_WELL_EDGE, 8, 255);
    metal_panel(s, &R_XPORT);
    separator(s, R_XPORT.x + 188, R_XPORT.y + 5, R_XPORT.h - 10);
    title_bar(s, &R_EQBAR, "EASYAMP EQUALIZER", &EA_FONT_PANEL, C_PANELLBL, 2, 0);
    metal_panel(s, &R_EQCTL);
    smoke(s, &R_EQSMALL);
    title_bar(s, &R_PLBAR, "EASYAMP PLAYLIST", &EA_FONT_PANEL, C_PANELLBL, 2, 0);
    well(s, &R_PLLIST, C_PLAYLIST, C_WELL_EDGE, 12, 230);
    metal_panel(s, &R_PLBTNS);
}

static void chrome_eq(ea_ui *ui)
{
    ea_surface *s = &ui->bg;
    static const char *klabels[8] = { "BANDS", "PREAMP", "IN", "OUT", "BALANCE", "PITCH", "SEL FREQ", "SEL Q" };
    int i;
    lcd_well(s, &R_EQTOP);
    well(s, &R_LOGSPEC, C_BLACK, C_WELL_EDGE, 8, 255);
    well(s, &R_WAVE, C_BLACK, C_WELL_EDGE, 8, 255);
    smoke(s, &R_BANK);
    metal_panel(s, &R_LOWER);
    well(s, &R_LEDMTR, C_BLACK, C_WELL_EDGE, 0, 0);
    for (i = 0; i < 8; i++) {
        ea_rect c;
        c.x = 109 + i * 64; c.y = 412; c.w = 64; c.h = 12;
        text_center(s, &EA_FONT_CTL, &c, klabels[i], C_CTLLABEL, 1, 0);
    }
    { ea_rect a = { 219, 494, 88, 12 }, b = { 317, 494, 150, 12 };
      text_center(s, &EA_FONT_CTL, &a, "EQ PRESETS", C_CTLLABEL, 1, 0);
      text_center(s, &EA_FONT_CTL, &b, "APO / GEQ", C_CTLLABEL, 1, 0); }
}

static void chrome_sources(ea_ui *ui)
{
    ea_surface *s = &ui->bg;
    ea_rect bar = { 3, 32, 724, 24 }, body = { 3, 56, 724, 495 }, msg = { 3, 270, 724, 20 };
    title_bar(s, &bar, "EASYAMP SOURCES", &EA_FONT_PANEL, C_PANELLBL, 2, 0);
    well(s, &body, C_PLAYLIST, C_WELL_EDGE, 12, 230);
    text_center(s, &EA_FONT_LCD, &msg, "PLEX + JELLYFIN - COMING TO THIS BUILD", C_IND_OFF, 1, 0);
}

static void build_bg(ea_ui *ui)
{
    chrome_common(ui);
    switch (ui->m->page) {
    case EA_PAGE_PLAYER:  chrome_player(ui); break;
    case EA_PAGE_EQ:      chrome_eq(ui); break;
    default:              chrome_sources(ui); break;
    }
    ui->bg_page = ui->m->page;
}

/* ======================================================================== */
/*  widget drawing                                                            */
/* ======================================================================== */

static void draw_xport(ea_ui *ui, widget *w)
{
    int ic = w->arg == EA_CMD_EJECT ? IC_EJECT : w->arg == EA_CMD_PREV ? IC_PREV :
             w->arg == EA_CMD_STOP ? IC_STOP : w->arg == EA_CMD_NEXT ? IC_NEXT :
             (ui->m->state == EA_PLAYING ? IC_PAUSE : IC_PLAY);
    ea_rect r = w->r;
    button_face(&ui->fb, &w->r, w->hover, w->down);
    if (w->down) { r.x++; r.y++; }
    icon(&ui->fb, ic, &r, EA_RGBF(0.12f, 0.12f, 0.16f));
}

static int toggle_state(ea_ui *ui, int id)
{
    switch (id) {
    case ID_EQ_SHOW: return ui->show_eq;
    case ID_PL_SHOW: return ui->show_pl;
    case ID_VU:      return ui->m->viz_vu;
    case ID_EQ_ON:   return ui->m->eq_on;
    case ID_BASS:    return ui->m->bass;
    case ID_LOUD:    return ui->m->loud;
    }
    return 0;
}

static void draw_ledbtn(ea_ui *ui, widget *w)
{
    /* K_LEDBTN stays raised and just lights its LED; K_TOGGLE also sinks */
    int on = toggle_state(ui, w->id), sunk = w->down || (w->kind == K_TOGGLE && on);
    int tw = gfx_text_w(&EA_FONT_BTN, w->label, 1) - 1, x = w->r.x + (w->r.w - (10 + 5 + tw)) / 2 + (sunk ? 1 : 0);
    int base = text_base(&EA_FONT_BTN, w->r.y, w->r.h) + (sunk ? 1 : 0);
    button_face(&ui->fb, &w->r, w->hover, sunk);
    led(&ui->fb, x, w->r.y + (w->r.h - 10) / 2 + (sunk ? 1 : 0), on);
    button_text(&ui->fb, &EA_FONT_BTN, x + 15, base, w->label, on ? EA_RGB(0x0a, 0x6a, 0x0a) : C_BTN_INK, 1);
}

static void draw_button(ea_ui *ui, widget *w)
{
    int sunk = w->down || (w->kind == K_MENUBTN && ui->menu.open && ui->menu.owner == w->id);
    int arrow = w->kind == K_MENUBTN ? 14 : 0;
    int tw = gfx_text_w(&EA_FONT_BTN, w->label, 1) - 1;
    int x = w->r.x + (w->r.w - tw - arrow) / 2 + (sunk ? 1 : 0);
    int base = text_base(&EA_FONT_BTN, w->r.y, w->r.h) + (sunk ? 1 : 0);
    button_face(&ui->fb, &w->r, w->hover, sunk);
    button_text(&ui->fb, &EA_FONT_BTN, x, base, w->label, C_BTN_INK, 1);
    if (arrow) down_arrow(&ui->fb, (float)(x + tw + 9), w->r.y + w->r.h / 2.0f + (sunk ? 1 : 0), C_BTN_INK);
}

static void draw_stackbtn(ea_ui *ui, widget *w)
{
    /* "+" over "FILE": w->label is "+FILE" / "-FILE" */
    char sym[2];
    ea_rect top = w->r, bot = w->r;
    int o = w->down ? 1 : 0;
    sym[0] = w->label[0]; sym[1] = 0;
    button_face(&ui->fb, &w->r, w->hover, w->down);
    top.y += 2 + o; top.h = 14; top.x += o;
    bot.y += 16 + o; bot.h = 10; bot.x += o;
    text_center(&ui->fb, &EA_FONT_SYM, &top, sym, EA_RGB(0x26, 0x26, 0x2c), 0, 0);
    text_center(&ui->fb, &EA_FONT_SMALL, &bot, w->label + 1, EA_RGB(0x26, 0x26, 0x2c), 1, 0);
}

static void draw_winbtn(ea_ui *ui, widget *w)
{
    ea_rect r = w->r;
    button_face(&ui->fb, &w->r, w->hover, w->down);
    if (w->down) { r.x++; r.y++; }
    icon(&ui->fb, w->arg == EA_CMD_WIN_CLOSE ? IC_CLOSE : IC_MIN, &r, C_BTN_INK);
}

static void draw_tab(ea_ui *ui, widget *w)
{
    int on = ui->m->page == w->arg;
    ea_surface *s = &ui->fb;
    if (on) {
        gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, EA_RGB(0x1b, 0x2a, 0x1b));
        gfx_fill(s, w->r.x, w->r.y, w->r.w, 2, C_LCD_ON);
    } else if (w->hover)
        gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, EA_RGB(0x1c, 0x1c, 0x24));
    {
        int tw = gfx_text_w(&EA_FONT_CTL, w->label, 1) - 1;
        gfx_text(s, &EA_FONT_CTL, w->r.x + (w->r.w - tw) / 2, text_base(&EA_FONT_CTL, w->r.y, w->r.h) + 1,
                 w->label, on ? C_LCD_ON : C_FOOT_DIM, 1, on ? GFX_GLOW : 0, C_LCD_ON);
    }
}

static void draw_footstat(ea_ui *ui, widget *w)
{
    char t[64];
    ea_surface *s = &ui->fb;
    int up = ui->m->update_avail, tw;
    if (up) sprintf(t, "UPDATE  v%s  AVAILABLE", ui->m->latest);
    else sprintf(t, "EASYAMP   v%s", EA_VERSION);
    if (up) gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, w->hover ? EA_RGB(0x34, 0x2c, 0x14) : EA_RGB(0x2a, 0x24, 0x10));
    tw = gfx_text_w(&EA_FONT_CTL, t, 1) - 1;
    gfx_text(s, &EA_FONT_CTL, w->r.x + w->r.w - tw - 9, text_base(&EA_FONT_CTL, w->r.y, w->r.h) + 1, t,
             up ? EA_RGB(0xf0, 0xc8, 0x4a) : C_FOOT_DIM, 1, up ? GFX_GLOW : 0, EA_RGB(0xf0, 0xc8, 0x4a));
}

static void fmt_time(int ms, char *out)
{
    int sec = ms / 1000;
    if (sec < 0) sec = 0;
    sprintf(out, "%02d:%02d", (sec / 60) % 100, sec % 60);
}

/* status LEDs + play triangle + the big 7-segment clock */
static void draw_status(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    int playing = ui->m->state == EA_PLAYING, stopped = ui->m->state == EA_STOPPED;
    int x = w->r.x + 4, y = w->r.y + 6;
    float t[6];
    char clock[8];
    gfx_fill(s, x + 1, y + 2, 6, 6, playing ? EA_RGBF(0.16f, 1.0f, 0.20f) : EA_RGBF(0.05f, 0.20f, 0.07f));
    gfx_fill(s, x + 1, y + 10, 6, 6, stopped ? EA_RGBF(1.0f, 0.16f, 0.12f) : EA_RGBF(0.26f, 0.05f, 0.04f));
    t[0] = x + 12.0f; t[1] = y + 4.0f; t[2] = x + 12.0f; t[3] = y + 14.0f; t[4] = x + 22.0f; t[5] = y + 9.0f;
    gfx_poly(s, t, 3, playing ? EA_RGBF(0.16f, 1.0f, 0.20f) : EA_RGBF(0.10f, 0.34f, 0.12f), 255);
    fmt_time(ui->m->pos_ms, clock);
    gfx_text(s, &EA_FONT_BIG, w->r.x + 52, w->r.y + 40, clock, C_LCD, 0, GFX_GLOW, C_LCD);
}

/* the visible slice of the title: as many characters as fit in `pixels`,
 * rotating through "title   ***   " once the whole title no longer fits */
static void marquee_text(ea_ui *ui, const ea_font *f, int pixels, char *out, int cap)
{
    const char *t = ui->m->title[0] ? ui->m->title : "--";
    char loop[300];
    int ln, i, w = 0;
    if (gfx_text_w(f, t, 1) <= pixels) { sprintf(out, "%.*s", cap - 1, t); return; }
    sprintf(loop, "%.255s   ***   ", t);
    ln = (int)strlen(loop);
    for (i = 0; i < cap - 1; i++) {
        char c[2];
        c[0] = loop[(ui->mq_pos + i) % ln]; c[1] = 0;
        w += gfx_text_w(f, c, 1);
        if (w > pixels) break;
        out[i] = c[0];
    }
    out[i] = 0;
}

static void draw_info(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    ea_model *m = ui->m;
    char t[64];
    int x = w->r.x, base = w->r.y + 12, tw;
    int paused = m->state == EA_PAUSED;
    x += gfx_text(s, &EA_FONT_IND, x, base, "PAUSE", paused ? C_LCD_ON : C_IND_OFF, 1, paused ? GFX_GLOW : 0, C_LCD_ON) + 8;
    sprintf(t, "%dK", m->kbps > 0 ? m->kbps : 0);
    x += gfx_text(s, &EA_FONT_IND, x, base, m->kbps > 0 ? t : "---K", m->kbps > 0 ? C_LCD_ON : C_IND_OFF, 1, 0, 0) + 8;
    sprintf(t, "%dK", m->khz);
    gfx_text(s, &EA_FONT_IND, x, base, m->khz > 0 ? t : "--K", m->khz > 0 ? C_LCD_ON : C_IND_OFF, 1, 0, 0);
    tw = gfx_text_w(&EA_FONT_IND, "STEREO", 1) - 1;
    gfx_text(s, &EA_FONT_IND, w->r.x + w->r.w - tw, base, "STEREO", m->stereo ? C_LCD_ON : C_IND_OFF, 1,
             m->stereo ? GFX_GLOW : 0, C_LCD_ON);
    marquee_text(ui, &EA_FONT_LCD, w->r.w, t, (int)sizeof t);
    gfx_clip(s, w->r.x - 6, w->r.y, w->r.w + 8, w->r.h);
    gfx_text(s, &EA_FONT_LCD, w->r.x, w->r.y + 32, t, C_LCD, 1, GFX_GLOW, C_LCD);
    gfx_unclip(s);
}

/* white mirrored bar scope with a faint grid and blue axis dots */
static void draw_scope(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    int x0 = w->r.x, y0 = w->r.y, W = w->r.w, H = w->r.h, c, b, k, p;
    float mid = H / 2.0f, amp = H * 0.44f, bw = (float)W / 22.0f;
    ea_px grid = EA_RGBF(0.40f, 0.52f, 0.85f), light = EA_RGBF(0.46f, 0.66f, 1.0f), dark = EA_RGBF(0.12f, 0.22f, 0.52f);
    for (c = 1; c < 8; c++) gfx_fill_a(s, x0 + (W * c) / 8, y0, 1, H, grid, 33);
    for (c = 1; c < 4; c++) gfx_fill_a(s, x0, y0 + (H * c) / 4, W, 1, grid, 33);
    for (b = 0; b < 22; b++) {
        int i = (b * EA_WAVE) / 22;
        float mag = (float)fabs(ui->m->wave[i]) * amp;
        if (mag < 0.5f) mag = 0.5f;
        gfx_rectf(s, x0 + b * bw + 1, y0 + mid - mag, bw - 1.5f, mag * 2, EA_RGBF(0.96f, 0.97f, 1.0f), 255);
    }
    for (k = 0, p = 1; p < W - 1; p += 6, k++) gfx_fill(s, x0 + p, y0 + H - 2, 2, 2, k % 2 ? dark : light);
    for (k = 0, p = 1; p < H - 1; p += 6, k++) gfx_fill(s, x0, y0 + p, 2, 2, k % 2 ? dark : light);
}

static float seek_fraction(ea_ui *ui)
{
    if (ui->m->dur_ms <= 0) return 0;
    return (float)ui->m->pos_ms / (float)ui->m->dur_ms;
}

static void draw_seek(ea_ui *ui, widget *w, float frac)
{
    ea_surface *s = &ui->fb;
    int x = w->r.x, W = w->r.w, H = w->r.h, gy = w->r.y + H / 2 - 2, tw = 14, th = H - 2, tx, ty = w->r.y + 1, mx, k;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx_fill(s, x + 1, gy, W - 2, 5, EA_RGBF(0.02f, 0.10f, 0.02f));
    gfx_frame(s, x, gy - 1, W, 7, C_BLACK, EA_RGB(0x0a, 0x3a, 0x0a));
    if (frac > 0) {
        gfx_glow_rect(s, x + 1, gy, (int)((W - 2) * frac), 5, 4, C_LCD, 90);
        gfx_fill(s, x + 1, gy, (int)((W - 2) * frac), 5, EA_RGBF(0.12f, 0.95f, 0.14f));
    }
    tx = x + (int)((W - tw) * frac + 0.5f);
    gfx_fill(s, tx, ty, tw, th, EA_RGBF(0.78f, 0.78f, 0.82f));
    gfx_frame(s, tx, ty, tw, th, EA_RGBF(0.96f, 0.96f, 0.99f), EA_RGBF(0.25f, 0.25f, 0.30f));
    mx = tx + tw / 2;
    for (k = -3; k <= 3; k += 3) gfx_fill(s, mx + k, ty + (th * 3) / 10, 1, (th * 4) / 10 + 1, EA_RGBF(0.20f, 0.20f, 0.24f));
}

/* ---- main visualizer: segmented spectrum or two analog VU gauges --------- */

static void draw_spectrum(ea_ui *ui, const ea_rect *r)
{
    ea_surface *s = &ui->fb;
    const int n = EA_VIZ_BANDS;
    float gap = 2.0f, bw = ((float)r->w - gap * (n + 1)) / n, seg_h = 4.0f, seg_gap = 1.5f;
    int total = (int)((float)r->h / (seg_h + seg_gap)), i, k;
    if (total < 1) total = 1;
    for (i = 0; i < n; i++) {
        float x = r->x + gap + i * (bw + gap);
        int lit = (int)(ui->m->levels[i] * total), ps = (int)(ui->peaks[i] * total);
        int xi = (int)(x + 0.5f), wi = (int)(bw + 0.5f);
        for (k = 0; k < lit; k++) {
            float frac = (float)k / (float)(total > 1 ? total - 1 : 1);
            int y = (int)(r->y + r->h - (k + 1) * (seg_h + seg_gap) + 0.5f);
            ea_px c = frac > 0.80f ? EA_RGBF(0.90f, 0.13f, 0.10f) : frac > 0.58f ? EA_RGBF(0.93f, 0.45f, 0.10f) :
                      frac > 0.36f ? EA_RGBF(0.92f, 0.82f, 0.14f) : EA_RGBF(0.14f, 0.88f, 0.20f);
            gfx_fill(s, xi, y, wi, 4, c);
        }
        if (ps > 0) gfx_fill(s, xi, (int)(r->y + r->h - ps * (seg_h + seg_gap) + 0.5f), wi, 2, EA_RGBF(0.80f, 1.0f, 0.85f));
    }
}

static const struct { int db; float frac; int major; } VU_SCALE[7] = {
    { -20, 0.0f, 1 }, { -10, 0.22f, 1 }, { -7, 0.36f, 0 }, { -5, 0.48f, 1 }, { -3, 0.62f, 0 }, { 0, 0.80f, 1 }, { 3, 1.0f, 1 } };
#define VU_RED 0.80f

static void vu_pt(float cx, float base_y, float frac, float rad, float *ox, float *oy)
{
    float a = (135.0f - frac * 90.0f) * 3.14159265f / 180.0f;
    *ox = cx + rad * (float)cos(a);
    *oy = base_y - rad * (float)sin(a);
}

static void vu_gauge(ea_surface *s, float x, float y, float w, float h, float value, const char *label)
{
    static const ea_stop glow[3] = { { 0.0f, EA_RGB(26, 117, 56) }, { 0.55f, EA_RGB(13, 56, 28) }, { 1.0f, EA_RGB(8, 33, 18) } };
    float cx = x + w / 2, base_y = y + h * 0.90f, r = w * 0.43f < h * 0.82f ? w * 0.43f : h * 0.82f;
    float ax, ay, bx, by, tipx, tipy;
    int i, vw;
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    gfx_clip(s, (int)x, (int)y, (int)w, (int)h);
    gfx_radial_rect(s, (int)x, (int)y, (int)w, (int)h, cx, base_y, r * 1.28f, r * 1.28f, glow, 3, 255);
    for (i = 0; i < 7; i++) {
        int red = VU_SCALE[i].frac >= VU_RED;
        ea_px c = red ? EA_RGBF(0.97f, 0.27f, 0.18f) : EA_RGBF(0.34f, 1.0f, 0.62f);
        vu_pt(cx, base_y, VU_SCALE[i].frac, r - (VU_SCALE[i].major ? 6 : 4), &ax, &ay);
        vu_pt(cx, base_y, VU_SCALE[i].frac, r, &bx, &by);
        gfx_line(s, ax, ay, bx, by, VU_SCALE[i].major ? 1.4f : 1.0f, c, 255);
        if (VU_SCALE[i].major) {
            char t[8];
            sprintf(t, VU_SCALE[i].db > 0 ? "+%d" : "%d", VU_SCALE[i].db);
            vu_pt(cx, base_y, VU_SCALE[i].frac, r - 14, &ax, &ay);
            gfx_text(s, &EA_FONT_SMALL, (int)(ax - gfx_text_w(&EA_FONT_SMALL, t, 0) / 2.0f), (int)(ay + 3), t, c, 0, 0, 0);
        }
    }
    for (i = 0; i < 40; i++) {
        vu_pt(cx, base_y, VU_RED * i / 40.0f, r, &ax, &ay);
        vu_pt(cx, base_y, VU_RED * (i + 1) / 40.0f, r, &bx, &by);
        gfx_line(s, ax, ay, bx, by, 2.0f, EA_RGBF(0.18f, 0.95f, 0.50f), 255);
    }
    for (i = 0; i < 12; i++) {
        vu_pt(cx, base_y, VU_RED + (1 - VU_RED) * i / 12.0f, r, &ax, &ay);
        vu_pt(cx, base_y, VU_RED + (1 - VU_RED) * (i + 1) / 12.0f, r, &bx, &by);
        gfx_line(s, ax, ay, bx, by, 2.0f, EA_RGBF(0.95f, 0.22f, 0.16f), 255);
    }
    vu_pt(cx, base_y, value, r - 3, &tipx, &tipy);
    gfx_line(s, cx, base_y, tipx, tipy, 5.0f, EA_RGBF(0.20f, 1.0f, 0.55f), 56);
    gfx_line(s, cx, base_y, tipx, tipy, 1.8f, EA_RGBF(0.68f, 1.0f, 0.82f), 255);
    gfx_disc(s, cx, base_y, 4.0f, EA_RGBF(0.16f, 0.92f, 0.46f), 255);
    gfx_disc(s, cx, base_y, 1.6f, EA_RGBF(0.82f, 1.0f, 0.90f), 255);
    gfx_text(s, &EA_FONT_BTN, (int)x + 6, (int)(y + h) - 5, label, EA_RGBF(0.34f, 0.98f, 0.58f), 0, 0, 0);
    vw = gfx_text_w(&EA_FONT_SMALL, "VU", 0);
    gfx_text(s, &EA_FONT_SMALL, (int)(x + w) - vw - 6, (int)(y + h) - 5, "VU", EA_RGBF(0.34f, 0.98f, 0.58f), 0, 0, 0);
    gfx_unclip(s);
}

static void draw_viz(ea_ui *ui, widget *w)
{
    ea_rect in = w->r;
    in.x += 2; in.y += 2; in.w -= 4; in.h -= 4;
    if (ui->m->viz_vu) {
        float gap = 8.0f, cell = ((float)in.w - gap * 3) / 2;
        vu_gauge(&ui->fb, in.x + gap, in.y + gap, cell, in.h - 2 * gap, ui->m->vu_l, "L");
        vu_gauge(&ui->fb, in.x + gap * 2 + cell, in.y + gap, cell, in.h - 2 * gap, ui->m->vu_r, "R");
    } else {
        gfx_clip(&ui->fb, in.x, in.y, in.w, in.h);
        draw_spectrum(ui, &in);
        gfx_unclip(&ui->fb);
    }
}

/* ---- player-page 10-band EQ ---------------------------------------------------- */

static const char *SMALL_LABELS[EA_GRAPHIC_N] = { "60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K" };

static void small_geom(const ea_rect *r, float *pre_w, float *lbl_w, float *band_w, float *top, float *bottom)
{
    *pre_w = r->w * 0.09f;
    *lbl_w = r->w * 0.115f;
    *band_w = (r->w - *pre_w - *lbl_w) / EA_GRAPHIC_N;
    *top = (float)r->y + SMALL_CURVE_H;
    *bottom = (float)(r->y + r->h) - SMALL_LABEL_H;
}

static void curve(ea_surface *s, const float *xs, const float *ys, const ea_px *cs, int n, int dots)
{
    int i;
    for (i = 0; i + 1 < n; i++) gfx_line(s, xs[i], ys[i], xs[i + 1], ys[i + 1], 2.0f, cs[i], 255);
    if (dots) for (i = 0; i < n; i++) gfx_disc(s, xs[i], ys[i], 1.6f, cs[i], 255);
}

static void column(ea_surface *s, float cx, float top, float span, float colw, float v, float vmin, float vmax,
                   float tw, float th)
{
    int x = (int)floor(cx - colw / 2 + 0.5f), y = (int)top, w = (int)(colw + 0.5f), h = (int)span;
    gfx_fill(s, x, y, w, h, value_color(v, vmin, vmax));
    gfx_frame(s, x, y, w, h, C_BLACK, C_BLACK);
    thumb(s, cx, top + (vmax - v) / (vmax - vmin) * span, tw, th);
}

static void draw_eqsmall(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    float pre_w, lbl_w, band_w, top, bottom, span, ten[EA_GRAPHIC_N], xs[EA_GRAPHIC_N], ys[EA_GRAPHIC_N], tw;
    ea_px cs[EA_GRAPHIC_N];
    ea_rect lab;
    int i, lx;
    small_geom(&w->r, &pre_w, &lbl_w, &band_w, &top, &bottom);
    span = bottom - top;
    ea_graphic_get(ui->m, ten);
    lx = w->r.x + (int)pre_w + 2;
    gfx_text(s, &EA_FONT_SMALL, lx, (int)top + 7, "+12db", C_EQLABEL, 0, 0, 0);
    gfx_text(s, &EA_FONT_SMALL, lx, (int)(top + span / 3) + 3, "+0db", C_EQLABEL, 0, 0, 0);
    gfx_text(s, &EA_FONT_SMALL, lx, (int)bottom, "-24db", C_EQLABEL, 0, 0, 0);
    for (i = 0; i < EA_GRAPHIC_N; i++) {
        xs[i] = w->r.x + pre_w + lbl_w + (i + 0.5f) * band_w;
        ys[i] = w->r.y + 3 + (EA_BAND_MAX - ten[i]) / (EA_BAND_MAX - EA_BAND_MIN) * (SMALL_CURVE_H - 6);
        cs[i] = value_color(ten[i], EA_BAND_MIN, EA_BAND_MAX);
    }
    curve(s, xs, ys, cs, EA_GRAPHIC_N, 1);
    tw = band_w - 5 < 18 ? band_w - 5 : 18;
    column(s, w->r.x + pre_w / 2, top, span, 7, ui->m->preamp, EA_PRE_MIN, EA_PRE_MAX, tw, tw * 0.55f);
    lab.y = w->r.y + w->r.h - SMALL_LABEL_H; lab.h = SMALL_LABEL_H;
    lab.x = w->r.x; lab.w = (int)pre_w;
    text_center(s, &EA_FONT_CTL, &lab, "PRE", C_EQLABEL, 0, 0);
    for (i = 0; i < EA_GRAPHIC_N; i++) {
        column(s, xs[i], top, span, 7, ten[i], EA_BAND_MIN, EA_BAND_MAX, tw, tw * 0.55f);
        lab.x = (int)(xs[i] - band_w / 2); lab.w = (int)band_w;
        text_center(s, &EA_FONT_CTL, &lab, SMALL_LABELS[i], C_EQLABEL, 0, 0);
    }
}

/* ---- playlist ---------------------------------------------------------------------- */

static int pl_rows(void) { return (R_PLLIST.h - 4) / PL_ROW_H; }

static void pl_thumb(ea_ui *ui, ea_rect *out)
{
    int rows = pl_rows(), n = ui->m->ntracks, track_h = R_PLLIST.h - 4, th, range;
    out->x = R_PLLIST.x + R_PLLIST.w - 9; out->w = 7; out->y = R_PLLIST.y + 2; out->h = 0;
    if (n <= rows) return;
    th = track_h * rows / n;
    if (th < 18) th = 18;
    range = n - rows;
    out->h = th;
    out->y += (track_h - th) * ui->pl_scroll / range;
}

static void draw_playlist(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    ea_model *m = ui->m;
    int rows = pl_rows(), i, scroll = m->ntracks > rows;
    ea_rect th;
    gfx_clip(s, w->r.x + 1, w->r.y + 1, w->r.w - 2 - (scroll ? 9 : 0), w->r.h - 2);
    for (i = 0; i < rows && ui->pl_scroll + i < m->ntracks; i++) {
        int idx = ui->pl_scroll + i, y = w->r.y + 2 + i * PL_ROW_H, sel = idx == m->sel;
        char line[200];
        ea_px c = (sel || idx == m->cur) ? C_WHITE : C_LCD;
        if (sel) gfx_fill(s, w->r.x + 1, y, w->r.w - 2, PL_ROW_H, C_SELECT);
        sprintf(line, "%d. %.180s", idx + 1, m->tracks[idx].title);
        gfx_text(s, &EA_FONT_TRACK, w->r.x + 6, y + 13, line, c, 0, GFX_GLOW, sel ? EA_RGB(150, 180, 255) : C_LCD);
    }
    gfx_unclip(s);
    if (scroll) {
        gfx_fill(s, w->r.x + w->r.w - 10, w->r.y + 1, 9, w->r.h - 2, EA_RGB(0x06, 0x06, 0x0a));
        pl_thumb(ui, &th);
        gfx_fill(s, th.x, th.y, th.w, th.h, EA_RGB(0x44, 0x44, 0x4e));
        gfx_frame(s, th.x, th.y, th.w, th.h, EA_RGB(0x76, 0x76, 0x8a), EA_RGB(0x05, 0x05, 0x08));
    }
}

/* ---- equalizer page -------------------------------------------------------------------- */

static void draw_eqtime(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    char clock[8], t[128];
    int base = text_base(&EA_FONT_LCD, w->r.y, w->r.h), x = w->r.x;
    fmt_time(ui->m->pos_ms, clock);
    x += gfx_text(s, &EA_FONT_IND, x, base - 1, clock, EA_RGB(0x23, 0xb4, 0x23), 1, 0, 0) + 8;
    marquee_text(ui, &EA_FONT_LCD, w->r.x + w->r.w - x, t, (int)sizeof t);
    gfx_clip(s, x - 6, w->r.y, w->r.x + w->r.w - x + 6, w->r.h);
    gfx_text(s, &EA_FONT_LCD, x, base, t, C_LCD, 1, GFX_GLOW, C_LCD);
    gfx_unclip(s);
}

static void draw_logspec(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    static const struct { float f; const char *lab; } ticks[3] = { { 100, "100" }, { 1000, "1K" }, { 10000, "10K" } };
    float span = (float)log10(24000.0 / 40.0), bw;
    ea_rect in = w->r;
    int i;
    in.x += 2; in.y += 2; in.w -= 4; in.h -= 4;
    gfx_clip(s, in.x, in.y, in.w, in.h);
    bw = (float)in.w / EA_VIZ_BANDS;
    for (i = 0; i < EA_VIZ_BANDS; i++) {
        float lv = ui->m->levels[i], bh = lv * (in.h - 10), warm = lv * 1.3f > 1 ? 1 : lv * 1.3f;
        if (bh < 1) bh = 1;
        gfx_rectf(s, in.x + i * bw + 0.5f, in.y + (in.h - 10) - bh, bw - 1 > 1 ? bw - 1 : 1, bh,
                  EA_RGBF(0.12f + 0.8f * warm, 0.9f - 0.5f * warm, 0.12f), 255);
    }
    for (i = 0; i < 3; i++) {
        int x = in.x + (int)((float)log10(ticks[i].f / 40.0) / span * in.w);
        gfx_fill_a(s, x, in.y, 1, in.h - 9, EA_RGBF(0.3f, 0.45f, 0.7f), 89);
        gfx_text(s, &EA_FONT_SMALL, x + 2, in.y + in.h - 1, ticks[i].lab, EA_RGBF(0.4f, 0.55f, 0.8f), 0, 0, 0);
    }
    gfx_unclip(s);
}

static void draw_wave(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    ea_rect in = w->r;
    int i;
    float mid;
    in.x += 2; in.y += 2; in.w -= 4; in.h -= 4;
    mid = in.y + in.h / 2.0f;
    gfx_clip(s, in.x, in.y, in.w, in.h);
    for (i = 0; i + 1 < EA_WAVE; i++)
        gfx_line(s, in.x + (float)i / (EA_WAVE - 1) * in.w, mid - ui->m->wave[i] * in.h * 0.45f,
                 in.x + (float)(i + 1) / (EA_WAVE - 1) * in.w, mid - ui->m->wave[i + 1] * in.h * 0.45f,
                 1.4f, EA_RGBF(0.10f, 0.95f, 0.14f), 255);
    gfx_unclip(s);
}

static void draw_bank(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    ea_model *m = ui->m;
    int n = m->nbands, i, step;
    float band_w = (float)w->r.w / n, top = (float)w->r.y + BANK_CURVE_H, bottom = (float)(w->r.y + w->r.h) - BANK_LABEL_H;
    float span = bottom - top, colw, tw, xs[EA_MAX_BANDS], ys[EA_MAX_BANDS];
    ea_px cs[EA_MAX_BANDS];
    const ea_font *lf = n <= 16 ? &EA_FONT_CTL : &EA_FONT_SMALL;
    for (i = 0; i < n; i++) {
        xs[i] = w->r.x + (i + 0.5f) * band_w;
        ys[i] = w->r.y + 3 + (EA_BAND_MAX - m->gains[i]) / (EA_BAND_MAX - EA_BAND_MIN) * (BANK_CURVE_H - 6);
        cs[i] = value_color(m->gains[i], EA_BAND_MIN, EA_BAND_MAX);
    }
    if (m->selband >= 0 && m->selband < n)
        gfx_fill_a(s, (int)(xs[m->selband] - band_w / 2) + 1, w->r.y + 1, (int)band_w - 2, w->r.h - 2, EA_RGB(41, 56, 115), 105);
    curve(s, xs, ys, cs, n, 0);
    colw = band_w * 0.5f < 9 ? band_w * 0.5f : 9;
    if (colw < 5) colw = 5;
    tw = band_w - 4 < 20 ? band_w - 4 : 20;
    for (i = 0; i < n; i++)
        column(s, xs[i], top, span, colw, m->gains[i], EA_BAND_MIN, EA_BAND_MAX, tw, tw * 0.5f > 7 ? tw * 0.5f : 7);
    step = band_w > 22 ? 1 : (band_w > 13 ? 2 : 3);
    for (i = 0; i < n; i += step) {
        char t[12];
        ea_rect lab;
        ea_fmt_freq(m->freqs[i], t);
        lab.x = (int)(xs[i] - 30); lab.w = 60; lab.y = w->r.y + w->r.h - BANK_LABEL_H; lab.h = BANK_LABEL_H;
        text_center(s, lf, &lab, t, i == m->selband ? EA_RGBF(0.55f, 0.78f, 1.0f) : C_EQLABEL, 0, -1);
    }
}

static ea_px seg_color(float frac)
{
    static const float g[3] = { 0.12f, 0.85f, 0.22f }, y[3] = { 0.92f, 0.82f, 0.12f }, r[3] = { 0.94f, 0.16f, 0.10f };
    return frac < 0.6f ? lerp3(g, y, frac / 0.6f) : lerp3(y, r, (frac - 0.6f) / 0.4f);
}

static void led_bar(ea_surface *s, int x0, int y, int w, int h, float level, float peak)
{
    int n = w / 6, i;
    if (n < 2) n = 2;
    for (i = 0; i < n; i++) {
        float frac = (float)i / (n - 1);
        ea_px c = seg_color(frac);
        if (frac > level) c = gfx_mix(C_BLACK, c, 0.16f);
        gfx_fill(s, x0 + i * 6, y, 4, h, c);
    }
    if (peak > 0.01f) {
        int pi = (int)(peak * (n - 1));
        if (pi > n - 1) pi = n - 1;
        gfx_fill(s, x0 + pi * 6, y - 1, 4, h + 2, seg_color((float)pi / (n - 1)));
    }
}

static void draw_ledmeter(ea_ui *ui, widget *w)
{
    ea_surface *s = &ui->fb;
    int bh = (w->r.h - 13) / 2, k, bar_w = w->r.w - 14 - 34 - 8;
    for (k = 0; k < 2; k++) {
        float lv = k ? ui->m->vu_r : ui->m->vu_l, pk = k ? ui->pk_r : ui->pk_l;
        int y = w->r.y + 4 + k * (bh + 5);
        char t[8];
        gfx_text(s, &EA_FONT_CTL, w->r.x + 4, y + bh - 2, k ? "R" : "L", EA_RGBF(0.45f, 0.6f, 0.85f), 0, 0, 0);
        led_bar(s, w->r.x + 16, y, bar_w, bh, lv, pk);
        sprintf(t, "%+.0f", lv * 50.0f - 50.0f);
        gfx_text(s, &EA_FONT_CTL, w->r.x + 16 + bar_w + 8, y + bh - 2, t, EA_RGBF(0.5f, 0.85f, 0.55f), 0, 0, 0);
    }
}

static float knob_value(ea_ui *ui, int id)
{
    ea_model *m = ui->m;
    int sb = m->selband >= 0 && m->selband < m->nbands ? m->selband : 0;
    switch (id) {
    case ID_K_BANDS:  return (float)m->nbands;
    case ID_K_PREAMP: return m->preamp;
    case ID_K_IN:     return m->in_gain;
    case ID_K_OUT:    return m->out_gain;
    case ID_K_BAL:    return m->balance;
    case ID_K_PITCH:  return m->pitch;
    case ID_K_FREQ:   return (float)log10(m->freqs[sb]);
    case ID_K_Q:      return m->q[sb];
    }
    return 0;
}

static void knob_format(ea_ui *ui, int id, float v, char *out)
{
    (void)ui;
    switch (id) {
    case ID_K_BANDS: sprintf(out, "%d", (int)(v + 0.5f)); break;
    case ID_K_BAL:
        if (fabs(v) < 0.025f) strcpy(out, "C");
        else sprintf(out, "%c%d", v < 0 ? 'L' : 'R', (int)(fabs(v) * 100 + 0.5f));
        break;
    case ID_K_PITCH: sprintf(out, "%.2fx", v); break;
    case ID_K_FREQ:  ea_fmt_freq((float)pow(10.0, v), out); break;
    case ID_K_Q:     sprintf(out, "Q%.1f", v); break;
    default:         sprintf(out, "%+.1f", v); break;
    }
}

static void draw_knob(ea_ui *ui, widget *w)
{
    static const ea_stop body[3] = { { 0.0f, EA_RGB(184, 184, 199) }, { 0.55f, EA_RGB(107, 107, 120) }, { 1.0f, EA_RGB(41, 41, 48) } };
    ea_surface *s = &ui->fb;
    float W = (float)w->r.w, H = (float)w->r.h, kr = W * 0.42f < (H - 16) * 0.5f ? W * 0.42f : (H - 16) * 0.5f;
    float cx = w->r.x + W / 2, cy = w->r.y + kr + 3, v = knob_value(ui, w->id);
    float t = w->vmax > w->vmin ? (v - w->vmin) / (w->vmax - w->vmin) : 0, ang, dx, dy, dotr;
    char txt[16];
    ea_rect lab;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    gfx_disc(s, cx, cy, kr + 2, EA_RGBF(0.05f, 0.05f, 0.07f), 255);
    gfx_disc_gradient(s, cx, cy, kr, cx - kr * 0.35f, cy - kr * 0.35f, kr * 0.05f, body, 3);
    gfx_ring(s, cx, cy, kr, 1.0f, w->hover || w->down ? EA_RGBF(0.78f, 0.78f, 0.86f) : EA_RGBF(0.55f, 0.55f, 0.60f), 255);
    ang = (225.0f - 270.0f * t) * 3.14159265f / 180.0f;
    dx = (float)cos(ang); dy = -(float)sin(ang);
    dotr = kr * 0.16f > 2 ? kr * 0.16f : 2;
    gfx_disc(s, cx + kr * 0.6f * dx, cy + kr * 0.6f * dy, dotr + 0.5f, EA_RGBF(0.3f, 0.0f, 0.0f), 255);
    gfx_disc(s, cx + kr * 0.6f * dx, cy + kr * 0.6f * dy, dotr, EA_RGBF(0.95f, 0.16f, 0.12f), 255);
    knob_format(ui, w->id, v, txt);
    lab = w->r; lab.y = w->r.y + w->r.h - 12; lab.h = 12;
    text_center(s, &EA_FONT_CTL, &lab, txt, EA_RGBF(0.16f, 1.0f, 0.16f), 0, 0);
}

/* ---- overlay menu (PRESETS / EXPORT drop-downs) ------------------------------------ */

#define MENU_ROW_H 18

static void draw_menu(ea_ui *ui)
{
    ea_surface *s = &ui->fb;
    ea_rect *r = &ui->menu.r;
    int i;
    gfx_fill_a(s, r->x + 3, r->y + 3, r->w, r->h, C_BLACK, 110);              /* drop shadow */
    gfx_fill(s, r->x, r->y, r->w, r->h, EA_RGB(0x23, 0x2b, 0x4a));
    gfx_scanlines(s, r->x, r->y, r->w, r->h, 8, 6, 26);
    gfx_frame(s, r->x, r->y, r->w, r->h, C_BAR_HI, C_BAR_LO);
    for (i = 0; i < ui->menu.n; i++) {
        int y = r->y + 3 + i * MENU_ROW_H, cur = !strcmp(ui->menu.items[i], ui->m->preset) && ui->menu.owner != ID_EXPORT;
        if (i == ui->menu.hover) gfx_fill(s, r->x + 2, y, r->w - 4, MENU_ROW_H, C_SELECT);
        if (cur) gfx_fill(s, r->x + 7, y + 7, 4, 4, C_LCD_ON);
        gfx_text(s, &EA_FONT_TRACK, r->x + 17, y + 13, ui->menu.items[i],
                 i == ui->menu.hover ? C_WHITE : EA_RGB(0xb8, 0xc0, 0xd4), 0, 0, 0);
    }
}

/* ======================================================================== */
/*  widget table                                                              */
/* ======================================================================== */

static widget *add(ea_ui *ui, int kind, int pages, int x, int y, int w, int h, const char *label, int id, int arg)
{
    widget *n = &ui->w[ui->nw++];
    memset(n, 0, sizeof *n);
    n->kind = kind; n->pages = pages; n->label = label; n->id = id; n->arg = arg;
    n->r.x = x; n->r.y = y; n->r.w = w; n->r.h = h;
    n->dirty = 1;
    return n;
}

static void add_knob(ea_ui *ui, int i, int id, float lo, float hi, float step, float def)
{
    widget *k = add(ui, K_KNOB, PG(EA_PAGE_EQ), 114 + i * 64, 426, 54, 58, 0, id, 0);
    k->vmin = lo; k->vmax = hi; k->vstep = step; k->vdef = def;
}

static void build_widgets(ea_ui *ui)
{
    static const int xcmd[5] = { EA_CMD_EJECT, EA_CMD_PREV, EA_CMD_PLAYPAUSE, EA_CMD_STOP, EA_CMD_NEXT };
    int i, P = PG(EA_PAGE_PLAYER), E = PG(EA_PAGE_EQ);
    /* window */
    add(ui, K_WINBTN, PG_ALL, 683, 7, 19, 18, 0, 0, EA_CMD_WIN_MINIMIZE);
    add(ui, K_WINBTN, PG_ALL, 704, 7, 19, 18, 0, 0, EA_CMD_WIN_CLOSE);
    add(ui, K_TAB, PG_ALL, 6, 555, 66, 20, "PLAYER", ID_TAB0, EA_PAGE_PLAYER);
    add(ui, K_TAB, PG_ALL, 72, 555, 88, 20, "EQUALIZER", ID_TAB1, EA_PAGE_EQ);
    add(ui, K_TAB, PG_ALL, 160, 555, 74, 20, "SOURCES", ID_TAB2, EA_PAGE_SOURCES);
    add(ui, K_FOOTSTAT, PG_ALL, 500, 555, 227, 20, 0, 0, EA_CMD_OPEN_UPDATE);
    /* player: display */
    add(ui, K_STATUS, P, 9, 36, 176, 50, 0, 0, 0);
    add(ui, K_INFO, P, 194, 38, 238, 40, 0, 0, 0);
    add(ui, K_SCOPE, P, 12, 84, 172, 40, 0, 0, 0);
    add(ui, K_SEEK, P, 194, 100, 238, 15, 0, 0, 0);
    add(ui, K_VIZ, P, R_VIZ.x, R_VIZ.y, R_VIZ.w, R_VIZ.h, 0, 0, 0);
    for (i = 0; i < 5; i++) add(ui, K_XPORT, P, 9 + i * 36, 275, 34, 28, 0, 0, xcmd[i]);
    add(ui, K_LEDBTN, P, 203, 275, 52, 28, "EQ", ID_EQ_SHOW, 0);
    add(ui, K_LEDBTN, P, 259, 275, 52, 28, "PL", ID_PL_SHOW, 0);
    add(ui, K_LEDBTN, P, 315, 275, 52, 28, "VU", ID_VU, 0);
    add(ui, K_TOGGLE, P, 9, 340, 56, 26, "ON", ID_EQ_ON, 0);
    add(ui, K_TOGGLE, P, 69, 340, 66, 26, "BASS", ID_BASS, 0);
    add(ui, K_TOGGLE, P, 139, 340, 66, 26, "LOUD", ID_LOUD, 0);
    add(ui, K_MENUBTN, P, 343, 340, 90, 26, "PRESETS", ID_PRESETS, 0);
    add(ui, K_EQSMALL, P, R_EQSMALL.x, R_EQSMALL.y, R_EQSMALL.w, R_EQSMALL.h, 0, 0, 0);
    add(ui, K_PLAYLIST, P, R_PLLIST.x, R_PLLIST.y, R_PLLIST.w, R_PLLIST.h, 0, 0, 0);
    add(ui, K_STACKBTN, P, 448, 517, 44, 30, "+FILE", 0, EA_CMD_PL_ADD);
    add(ui, K_STACKBTN, P, 495, 517, 44, 30, "-FILE", 0, EA_CMD_PL_REMOVE);
    add(ui, K_BUTTON, P, 542, 517, 44, 30, "CLR", 0, EA_CMD_PL_CLEAR);
    add(ui, K_BUTTON, P, 589, 517, 52, 30, "LOAD", 0, EA_CMD_PL_LOAD);
    add(ui, K_BUTTON, P, 644, 517, 52, 30, "SAVE", 0, EA_CMD_PL_SAVE);
    /* equalizer */
    for (i = 0; i < 5; i++) add(ui, K_XPORT, E, 11 + i * 36, 39, 34, 28, 0, 0, xcmd[i]);
    add(ui, K_EQTIME, E, 198, 36, 520, 34, 0, 0, 0);
    add(ui, K_LOGSPEC, E, R_LOGSPEC.x, R_LOGSPEC.y, R_LOGSPEC.w, R_LOGSPEC.h, 0, 0, 0);
    add(ui, K_WAVE, E, R_WAVE.x, R_WAVE.y, R_WAVE.w, R_WAVE.h, 0, 0, 0);
    add(ui, K_BANK, E, R_BANK.x, R_BANK.y, R_BANK.w, R_BANK.h, 0, 0, 0);
    add(ui, K_LEDMETER, E, R_LEDMTR.x + 1, R_LEDMTR.y + 1, R_LEDMTR.w - 2, R_LEDMTR.h - 2, 0, 0, 0);
    add_knob(ui, 0, ID_K_BANDS, 10, 32, 1, 10);
    add_knob(ui, 1, ID_K_PREAMP, -12, 12, 0.5f, 0);
    add_knob(ui, 2, ID_K_IN, -12, 12, 0.5f, 0);
    add_knob(ui, 3, ID_K_OUT, -12, 12, 0.5f, 0);
    add_knob(ui, 4, ID_K_BAL, -1, 1, 0.05f, 0);
    add_knob(ui, 5, ID_K_PITCH, 0.90f, 1.10f, 0.01f, 1.0f);
    add_knob(ui, 6, ID_K_FREQ, 1.30103f, 4.30103f, 0.02f, 3.0f);
    add_knob(ui, 7, ID_K_Q, 0.3f, 12.0f, 0.1f, EA_DEFAULT_Q);
    add(ui, K_MENUBTN, E, 219, 510, 88, 28, "PRESETS", ID_PRESETS2, 0);
    add(ui, K_BUTTON, E, 317, 510, 66, 28, "IMPORT", ID_IMPORT, EA_CMD_EQ_IMPORT);
    add(ui, K_MENUBTN, E, 387, 510, 80, 28, "EXPORT", ID_EXPORT, 0);
    add(ui, K_BUTTON, E, 477, 510, 58, 28, "RESET", ID_RESET, 0);
}

static int on_page(ea_ui *ui, const widget *w) { return (w->pages & PG(ui->m->page)) != 0; }

static void mark_kind(ea_ui *ui, int kind)
{
    int i;
    for (i = 0; i < ui->nw; i++) if (ui->w[i].kind == kind) ui->w[i].dirty = 1;
}

static void draw_widget(ea_ui *ui, widget *w)
{
    switch (w->kind) {
    case K_XPORT:    draw_xport(ui, w); break;
    case K_LEDBTN: case K_TOGGLE: draw_ledbtn(ui, w); break;
    case K_BUTTON: case K_MENUBTN: draw_button(ui, w); break;
    case K_STACKBTN: draw_stackbtn(ui, w); break;
    case K_WINBTN:   draw_winbtn(ui, w); break;
    case K_TAB:      draw_tab(ui, w); break;
    case K_FOOTSTAT: draw_footstat(ui, w); break;
    case K_STATUS:   draw_status(ui, w); break;
    case K_INFO:     draw_info(ui, w); break;
    case K_SCOPE:    draw_scope(ui, w); break;
    case K_SEEK:     draw_seek(ui, w, ui->capture >= 0 && &ui->w[ui->capture] == w ? ui->press_v : seek_fraction(ui)); break;
    case K_VIZ:      draw_viz(ui, w); break;
    case K_EQSMALL:  draw_eqsmall(ui, w); break;
    case K_PLAYLIST: draw_playlist(ui, w); break;
    case K_EQTIME:   draw_eqtime(ui, w); break;
    case K_LOGSPEC:  draw_logspec(ui, w); break;
    case K_WAVE:     draw_wave(ui, w); break;
    case K_BANK:     draw_bank(ui, w); break;
    case K_LEDMETER: draw_ledmeter(ui, w); break;
    case K_KNOB:     draw_knob(ui, w); break;
    }
}

/* ======================================================================== */
/*  lifecycle + rendering                                                     */
/* ======================================================================== */

ea_ui *ui_create(ea_model *m, const ea_actions *a, ea_px *pixels)
{
    ea_ui *ui = (ea_ui *)calloc(1, sizeof *ui);
    ea_px *bgpx;
    if (!ui) return 0;
    ui->m = m;
    if (a) ui->act = *a;
    if (!pixels) { pixels = (ea_px *)malloc(sizeof(ea_px) * EA_WIN_W * EA_WIN_H); ui->own_pixels = 1; }
    bgpx = (ea_px *)malloc(sizeof(ea_px) * EA_WIN_W * EA_WIN_H);
    if (!pixels || !bgpx) { free(ui); return 0; }
    gfx_init(&ui->fb, EA_WIN_W, EA_WIN_H, pixels);
    gfx_init(&ui->bg, EA_WIN_W, EA_WIN_H, bgpx);
    ui->capture = ui->hover = -1;
    ui->menu.hover = -1;
    ui->bg_page = -1;
    ui->show_eq = ui->show_pl = 1;
    build_widgets(ui);
    return ui;
}

void ui_destroy(ea_ui *ui)
{
    if (!ui) return;
    if (ui->own_pixels) free(ui->fb.px);
    free(ui->bg.px);
    free(ui);
}

ea_surface *ui_surface(ea_ui *ui) { return &ui->fb; }

static void close_menu(ea_ui *ui)
{
    int i;
    if (!ui->menu.open) return;
    ui->menu.open = 0;
    ui->full_dirty = 1;            /* simplest correct way to lift an overlay */
    for (i = 0; i < ui->nw; i++) ui->w[i].dirty = 1;
}

void ui_set_page(ea_ui *ui, int page)
{
    if (page < 0 || page >= EA_PAGE_COUNT) return;
    close_menu(ui);
    ui->m->page = page;
    ui->capture = -1;
}

void ui_model_changed(ea_ui *ui, int what)
{
    if (what & UI_CH_TIME)      { mark_kind(ui, K_STATUS); mark_kind(ui, K_SEEK); mark_kind(ui, K_EQTIME); }
    if (what & UI_CH_TRANSPORT) { mark_kind(ui, K_STATUS); mark_kind(ui, K_INFO); mark_kind(ui, K_XPORT); }
    if (what & UI_CH_TITLE)     { ui->mq_pos = 0; mark_kind(ui, K_INFO); mark_kind(ui, K_EQTIME); }
    if (what & UI_CH_PLAYLIST)  mark_kind(ui, K_PLAYLIST);
    if (what & UI_CH_EQ)        { mark_kind(ui, K_EQSMALL); mark_kind(ui, K_BANK); mark_kind(ui, K_KNOB);
                                  mark_kind(ui, K_TOGGLE); }
    if (what & UI_CH_VIZ) {
        int i;
        for (i = 0; i < EA_VIZ_BANDS; i++)
            ui->peaks[i] = ui->m->levels[i] > ui->peaks[i] - 0.015f ? ui->m->levels[i] : ui->peaks[i] - 0.015f;
        ui->pk_l = ui->m->vu_l > ui->pk_l - 0.012f ? ui->m->vu_l : ui->pk_l - 0.012f;
        ui->pk_r = ui->m->vu_r > ui->pk_r - 0.012f ? ui->m->vu_r : ui->pk_r - 0.012f;
        mark_kind(ui, K_VIZ); mark_kind(ui, K_SCOPE); mark_kind(ui, K_LOGSPEC); mark_kind(ui, K_WAVE);
        mark_kind(ui, K_LEDMETER);
    }
    if (what & UI_CH_FOOTER)    mark_kind(ui, K_FOOTSTAT);
}

void ui_tick(ea_ui *ui, int elapsed_ms)
{
    ui->mq_acc += elapsed_ms;
    if (ui->mq_acc >= 220) {
        ui->mq_acc = 0;
        if (gfx_text_w(&EA_FONT_LCD, ui->m->title, 1) > 238) { ui->mq_pos++; mark_kind(ui, K_INFO); mark_kind(ui, K_EQTIME); }
    }
}

static int intersects(const ea_rect *a, const ea_rect *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

int ui_render(ea_ui *ui, ea_rect *dirty, int max)
{
    int i, n = 0, menu_hit = 0;
    if (ui->bg_page != ui->m->page) { build_bg(ui); ui->full_dirty = 1; }
    if (ui->full_dirty) {
        gfx_blit(&ui->fb, 0, 0, &ui->bg, 0, 0, EA_WIN_W, EA_WIN_H);
        for (i = 0; i < ui->nw; i++) ui->w[i].dirty = 1;
    }
    for (i = 0; i < ui->nw; i++) {
        widget *w = &ui->w[i];
        ea_rect r;
        if (!w->dirty) continue;
        w->dirty = 0;
        if (!on_page(ui, w)) continue;
        /* a glow can spill a few pixels outside the widget: restore a margin */
        r.x = w->r.x - 6; r.y = w->r.y - 6; r.w = w->r.w + 12; r.h = w->r.h + 12;
        if (w->kind == K_VIZ || w->kind == K_PLAYLIST || w->kind == K_EQSMALL || w->kind == K_BANK ||
            w->kind == K_LOGSPEC || w->kind == K_WAVE || w->kind == K_LEDMETER || w->kind == K_SCOPE) r = w->r;
        if (!ui->full_dirty) {
            gfx_clip(&ui->fb, r.x, r.y, r.w, r.h);
            gfx_blit(&ui->fb, r.x < 0 ? 0 : r.x, r.y < 0 ? 0 : r.y, &ui->bg, r.x < 0 ? 0 : r.x, r.y < 0 ? 0 : r.y,
                     r.w, r.h);
            draw_widget(ui, w);
            gfx_unclip(&ui->fb);
            /* neighbours whose glow margin we just wiped must repaint too */
            { int j; for (j = 0; j < ui->nw; j++) if (j != i && on_page(ui, &ui->w[j]) && !ui->w[j].dirty &&
                  j > i && intersects(&r, &ui->w[j].r)) ui->w[j].dirty = 1; }
            if (ui->menu.open && intersects(&r, &ui->menu.r)) menu_hit = 1;
            if (n < max) dirty[n++] = r;
        } else
            draw_widget(ui, w);
    }
    if (ui->menu.open && (ui->full_dirty || menu_hit || ui->menu.hover != -2)) {
        draw_menu(ui);
        if (!ui->full_dirty && n < max) { dirty[n] = ui->menu.r; dirty[n].w += 3; dirty[n].h += 3; n++; }
    }
    if (ui->full_dirty) {
        ui->full_dirty = 0;
        if (max > 0) { dirty[0].x = dirty[0].y = 0; dirty[0].w = EA_WIN_W; dirty[0].h = EA_WIN_H; }
        return 1;
    }
    return n;
}

/* ======================================================================== */
/*  input                                                                     */
/* ======================================================================== */

static int inside(const ea_rect *r, int x, int y) { return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h; }

static int hit(ea_ui *ui, int x, int y)
{
    int i;
    for (i = ui->nw - 1; i >= 0; i--)
        if (on_page(ui, &ui->w[i]) && inside(&ui->w[i].r, x, y)) return i;
    return -1;
}

static int interactive(int kind)
{
    return !(kind == K_STATUS || kind == K_INFO || kind == K_SCOPE || kind == K_EQTIME || kind == K_LOGSPEC ||
             kind == K_WAVE || kind == K_LEDMETER);
}

static void command(ea_ui *ui, int cmd) { if (ui->act.command) ui->act.command(ui->act.ctx, cmd); }
static void eq_changed(ea_ui *ui) { if (ui->act.eq_changed) ui->act.eq_changed(ui->act.ctx); ui_model_changed(ui, UI_CH_EQ); }

static void open_menu(ea_ui *ui, widget *owner)
{
    int i, wmax = owner->r.w;
    ui->menu.n = 0;
    if (owner->id == ID_EXPORT) {
        ui->menu.items[ui->menu.n++] = "Equalizer APO (.txt)";
        ui->menu.items[ui->menu.n++] = "GraphicEQ (.txt)";
    } else
        for (i = 0; i < ea_preset_count() && ui->menu.n < MAX_MENU; i++) ui->menu.items[ui->menu.n++] = ea_preset_name(i);
    for (i = 0; i < ui->menu.n; i++) {
        int tw = gfx_text_w(&EA_FONT_TRACK, ui->menu.items[i], 0) + 30;
        if (tw > wmax) wmax = tw;
    }
    ui->menu.r.w = wmax; ui->menu.r.h = ui->menu.n * MENU_ROW_H + 6;
    ui->menu.r.x = owner->r.x + owner->r.w - wmax;
    ui->menu.r.y = owner->r.y - ui->menu.r.h - 1;                       /* opens upward */
    if (ui->menu.r.y < 30) ui->menu.r.y = owner->r.y + owner->r.h + 1;
    ui->menu.open = 1; ui->menu.owner = owner->id; ui->menu.hover = -1;
    owner->dirty = 1;
}

static void menu_pick(ea_ui *ui, int i)
{
    int owner = ui->menu.owner;
    close_menu(ui);
    if (i < 0) return;
    if (owner == ID_EXPORT) command(ui, i == 0 ? EA_CMD_EQ_EXPORT_APO : EA_CMD_EQ_EXPORT_GEQ);
    else { ea_preset_apply(ui->m, i); eq_changed(ui); }
}

static float snap(float v, float lo, float hi, float step)
{
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (step > 0) v = (float)floor(v / step + 0.5f) * step;
    return v;
}

static void knob_set(ea_ui *ui, widget *w, float v)
{
    ea_model *m = ui->m;
    int sb = m->selband >= 0 && m->selband < m->nbands ? m->selband : 0;
    v = snap(v, w->vmin, w->vmax, w->vstep);
    switch (w->id) {
    case ID_K_BANDS:  ea_set_nbands(m, (int)(v + 0.5f)); break;
    case ID_K_PREAMP: m->preamp = v; break;
    case ID_K_IN:     m->in_gain = v; break;
    case ID_K_OUT:    m->out_gain = v; break;
    case ID_K_BAL:    m->balance = v; break;
    case ID_K_PITCH:  m->pitch = v; break;
    case ID_K_FREQ:   m->freqs[sb] = (float)pow(10.0, v); break;
    case ID_K_Q:      m->q[sb] = v; break;
    }
    eq_changed(ui);
}

/* a press or drag inside one of the slider banks */
static void bank_drag(ea_ui *ui, widget *w, int x, int y, int pressing)
{
    ea_model *m = ui->m;
    float frac;
    if (w->kind == K_EQSMALL) {
        float pre_w, lbl_w, band_w, top, bottom, ten[EA_GRAPHIC_N];
        int idx;
        small_geom(&w->r, &pre_w, &lbl_w, &band_w, &top, &bottom);
        frac = ((float)y - top) / (bottom - top);
        if (pressing) ui->press_x = x < w->r.x + pre_w ? -1 : (int)(((float)x - w->r.x - pre_w - lbl_w) / band_w);
        idx = ui->press_x;
        if (idx == -1) m->preamp = snap(EA_PRE_MAX - frac * (EA_PRE_MAX - EA_PRE_MIN), EA_PRE_MIN, EA_PRE_MAX, 1);
        else if (idx >= 0 && idx < EA_GRAPHIC_N && x >= w->r.x + pre_w + lbl_w - band_w) {
            ea_graphic_get(m, ten);
            ten[idx] = snap(EA_BAND_MAX - frac * (EA_BAND_MAX - EA_BAND_MIN), EA_BAND_MIN, EA_BAND_MAX, 1);
            ea_graphic_set(m, ten);
            strcpy(m->preset, "Custom");
        }
    } else {
        float band_w = (float)w->r.w / m->nbands, top = (float)w->r.y + BANK_CURVE_H;
        float bottom = (float)(w->r.y + w->r.h) - BANK_LABEL_H;
        if (pressing) {
            int idx = (int)(((float)x - w->r.x) / band_w);
            ui->press_x = idx < 0 ? 0 : (idx >= m->nbands ? m->nbands - 1 : idx);
            m->selband = ui->press_x;
        }
        frac = ((float)y - top) / (bottom - top);
        m->gains[ui->press_x] = snap(EA_BAND_MAX - frac * (EA_BAND_MAX - EA_BAND_MIN), EA_BAND_MIN, EA_BAND_MAX, 0.5f);
        strcpy(m->preset, "Custom");
    }
    eq_changed(ui);
}

static void pl_clamp(ea_ui *ui)
{
    int maxs = ui->m->ntracks - pl_rows();
    if (ui->pl_scroll > maxs) ui->pl_scroll = maxs;
    if (ui->pl_scroll < 0) ui->pl_scroll = 0;
}

static void pl_reveal(ea_ui *ui)
{
    int s = ui->m->sel;
    if (s < 0) return;
    if (s < ui->pl_scroll) ui->pl_scroll = s;
    if (s >= ui->pl_scroll + pl_rows()) ui->pl_scroll = s - pl_rows() + 1;
    pl_clamp(ui);
}

static void set_hover(ea_ui *ui, int idx)
{
    if (idx >= 0 && !interactive(ui->w[idx].kind)) idx = -1;
    if (idx == ui->hover) return;
    if (ui->hover >= 0) { ui->w[ui->hover].hover = 0; ui->w[ui->hover].dirty = 1; }
    ui->hover = idx;
    if (idx >= 0) { ui->w[idx].hover = 1; ui->w[idx].dirty = 1; }
}

void ui_mouse_move(ea_ui *ui, int x, int y)
{
    if (ui->menu.open) {
        int h = inside(&ui->menu.r, x, y) ? (y - ui->menu.r.y - 3) / MENU_ROW_H : -1;
        if (h >= ui->menu.n) h = -1;
        if (h != ui->menu.hover) { ui->menu.hover = h; ui->full_dirty = 1; }
        return;
    }
    if (ui->capture >= 0) {
        widget *w = &ui->w[ui->capture];
        switch (w->kind) {
        case K_KNOB:    knob_set(ui, w, ui->press_v + (float)(x - ui->press_x) / 150.0f * (w->vmax - w->vmin)); break;
        case K_EQSMALL: case K_BANK: bank_drag(ui, w, x, y, 0); break;
        case K_SEEK:    ui->press_v = (float)(x - w->r.x - 7) / (float)(w->r.w - 14);
                        ui->press_v = ui->press_v < 0 ? 0 : (ui->press_v > 1 ? 1 : ui->press_v); w->dirty = 1; break;
        case K_PLAYLIST:
            if (ui->pl_thumb_drag) {
                ea_rect th; int range = ui->m->ntracks - pl_rows(), travel;
                pl_thumb(ui, &th); travel = R_PLLIST.h - 4 - th.h;
                if (travel > 0) ui->pl_scroll = (y - ui->pl_thumb_off - (R_PLLIST.y + 2)) * range / travel;
                pl_clamp(ui); w->dirty = 1;
            }
            break;
        default: { int in = inside(&w->r, x, y); if (in != w->down) { w->down = in; w->dirty = 1; } } break;
        }
        return;
    }
    set_hover(ui, hit(ui, x, y));
}

void ui_mouse_down(ea_ui *ui, int x, int y)
{
    int i;
    widget *w;
    if (ui->menu.open) {
        int h = inside(&ui->menu.r, x, y) ? (y - ui->menu.r.y - 3) / MENU_ROW_H : -1;
        menu_pick(ui, h >= 0 && h < ui->menu.n ? h : -1);
        return;
    }
    i = hit(ui, x, y);
    if (i < 0 || !interactive(ui->w[i].kind)) return;
    w = &ui->w[i];
    ui->capture = i;
    w->down = 1; w->dirty = 1;
    switch (w->kind) {
    case K_KNOB:    ui->press_x = x; ui->press_v = knob_value(ui, w->id); break;
    case K_EQSMALL: case K_BANK: bank_drag(ui, w, x, y, 1); break;
    case K_SEEK:    ui->press_v = (float)(x - w->r.x - 7) / (float)(w->r.w - 14);
                    ui->press_v = ui->press_v < 0 ? 0 : (ui->press_v > 1 ? 1 : ui->press_v); break;
    case K_PLAYLIST: {
        ea_rect th;
        pl_thumb(ui, &th);
        ui->pl_thumb_drag = 0;
        if (th.h && x >= th.x - 2) {
            if (y >= th.y && y < th.y + th.h) { ui->pl_thumb_drag = 1; ui->pl_thumb_off = y - th.y; }
            else { ui->pl_scroll += y < th.y ? -pl_rows() : pl_rows(); pl_clamp(ui); }
        } else {
            int idx = ui->pl_scroll + (y - w->r.y - 2) / PL_ROW_H;
            ui->m->sel = idx >= 0 && idx < ui->m->ntracks ? idx : -1;
        }
        break; }
    }
}

void ui_mouse_up(ea_ui *ui, int x, int y)
{
    widget *w;
    int in;
    if (ui->capture < 0) return;
    w = &ui->w[ui->capture];
    in = inside(&w->r, x, y);
    ui->capture = -1;
    w->down = 0; w->dirty = 1;
    ui->pl_thumb_drag = 0;
    if (w->kind == K_SEEK) { if (ui->act.seek) ui->act.seek(ui->act.ctx, ui->press_v); }
    else if (in) switch (w->kind) {
    case K_XPORT: case K_STACKBTN: case K_WINBTN: command(ui, w->arg); break;
    case K_BUTTON:
        if (w->id == ID_RESET) { int k; for (k = 0; k < ui->m->nbands; k++) ui->m->gains[k] = 0;
                                 ui->m->preamp = 0; strcpy(ui->m->preset, "Flat"); eq_changed(ui); }
        else command(ui, w->arg);
        break;
    case K_MENUBTN: open_menu(ui, w); ui->full_dirty = 1; break;
    case K_TAB:     ui_set_page(ui, w->arg); break;
    case K_FOOTSTAT: if (ui->m->update_avail) command(ui, w->arg); break;
    case K_LEDBTN: case K_TOGGLE:
        switch (w->id) {
        case ID_EQ_SHOW: ui->show_eq = !ui->show_eq; break;
        case ID_PL_SHOW: ui->show_pl = !ui->show_pl; break;
        case ID_VU:      ui->m->viz_vu = !ui->m->viz_vu; mark_kind(ui, K_VIZ); break;
        case ID_EQ_ON:   ui->m->eq_on = !ui->m->eq_on; eq_changed(ui); break;
        case ID_BASS:    ui->m->bass = !ui->m->bass; eq_changed(ui); break;
        case ID_LOUD:    ui->m->loud = !ui->m->loud; eq_changed(ui); break;
        }
        break;
    }
    ui_mouse_move(ui, x, y);
}

void ui_mouse_dbl(ea_ui *ui, int x, int y)
{
    int i = hit(ui, x, y);
    widget *w;
    if (i < 0) return;
    w = &ui->w[i];
    if (w->kind == K_KNOB) knob_set(ui, w, w->vdef);
    else if (w->kind == K_PLAYLIST) {
        int idx = ui->pl_scroll + (y - w->r.y - 2) / PL_ROW_H;
        if (idx >= 0 && idx < ui->m->ntracks && x < w->r.x + w->r.w - 10 && ui->act.play_index) ui->act.play_index(ui->act.ctx, idx);
    } else if (w->kind == K_BANK && ui->m->selband >= 0) { ui->m->gains[ui->m->selband] = 0; eq_changed(ui); }
    else ui_mouse_down(ui, x, y);
}

void ui_mouse_leave(ea_ui *ui) { if (ui->capture < 0) set_hover(ui, -1); }

void ui_wheel(ea_ui *ui, int x, int y, int notches)
{
    int i = hit(ui, x, y);
    widget *w;
    if (i < 0) return;
    w = &ui->w[i];
    if (w->kind == K_KNOB) knob_set(ui, w, knob_value(ui, w->id) + notches * w->vstep);
    else if (w->kind == K_PLAYLIST) { ui->pl_scroll -= notches * 3; pl_clamp(ui); w->dirty = 1; }
}

void ui_key(ea_ui *ui, int key)
{
    ea_model *m = ui->m;
    if (key == UI_KEY_ESC) { close_menu(ui); return; }
    if (key == UI_KEY_SPACE) { command(ui, EA_CMD_PLAYPAUSE); return; }
    if (m->page != EA_PAGE_PLAYER || m->ntracks == 0) return;
    switch (key) {
    case UI_KEY_UP:     m->sel = m->sel > 0 ? m->sel - 1 : 0; break;
    case UI_KEY_DOWN:   m->sel = m->sel < m->ntracks - 1 ? m->sel + 1 : m->ntracks - 1; break;
    case UI_KEY_PGUP:   m->sel = m->sel - pl_rows() > 0 ? m->sel - pl_rows() : 0; break;
    case UI_KEY_PGDN:   m->sel = m->sel + pl_rows() < m->ntracks ? m->sel + pl_rows() : m->ntracks - 1; break;
    case UI_KEY_HOME:   m->sel = 0; break;
    case UI_KEY_END:    m->sel = m->ntracks - 1; break;
    case UI_KEY_ENTER:  if (m->sel >= 0 && ui->act.play_index) ui->act.play_index(ui->act.ctx, m->sel); return;
    case UI_KEY_DELETE: command(ui, EA_CMD_PL_REMOVE); return;
    default: return;
    }
    pl_reveal(ui);
    mark_kind(ui, K_PLAYLIST);
}

int ui_is_caption(ea_ui *ui, int x, int y)
{
    (void)ui;
    return inside(&R_TITLE, x, y) && x < 680;
}

int ui_wants_capture(ea_ui *ui) { return ui->capture >= 0; }
