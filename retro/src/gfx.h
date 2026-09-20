/* gfx - EasyAmp retro's software renderer.
 *
 * Every pixel of the UI is drawn here into a plain 32-bit buffer, then handed
 * to the OS as one finished picture. Nothing depends on GDI's text, pens or
 * theming, so Windows 98 SE, XP and Wine produce the same image, and the same
 * code renders screenshots on the build machine with no Windows in sight.
 *
 * Coordinates follow Cairo's convention (pixel centres at n + 0.5) so the
 * drawing code ported from the GTK app keeps its numbers.
 */
#ifndef EA_GFX_H
#define EA_GFX_H

typedef unsigned int ea_px;                 /* 0x00RRGGBB == a 32bpp DIB pixel */
#define EA_RGB(r, g, b) ((ea_px)((((ea_px)(r) & 255) << 16) | (((ea_px)(g) & 255) << 8) | ((ea_px)(b) & 255)))
#define EA_RGBF(r, g, b) EA_RGB((int)((r) * 255.0f + 0.5f), (int)((g) * 255.0f + 0.5f), (int)((b) * 255.0f + 0.5f))
/* channels come back as int: they get subtracted, and unsigned underflow
 * turns every darker-than-background blend into noise */
#define EA_R(c) ((int)(((c) >> 16) & 255))
#define EA_G(c) ((int)(((c) >> 8) & 255))
#define EA_B(c) ((int)((c) & 255))

typedef struct {
    int w, h;
    ea_px *px;                              /* top-down, w pixels per row */
    int cx0, cy0, cx1, cy1;                 /* clip rectangle, half-open */
} ea_surface;

typedef struct { int x, y, w, h; } ea_rect;

typedef struct {                            /* one glyph in a generated atlas */
    short w, h, xoff, yoff, adv; int off;   /* coverage bitmap */
    short gw, gh, gxoff, gyoff; int goff;   /* pre-blurred glow bitmap */
} ea_glyph;

typedef struct {
    int size, ascent, descent, has_glow;
    const ea_glyph *glyphs;                 /* ASCII 32..126 */
    const unsigned char *data;
} ea_font;

typedef struct { float t; ea_px col; } ea_stop;

int   gfx_init(ea_surface *s, int w, int h, ea_px *pixels);
void  gfx_clip(ea_surface *s, int x, int y, int w, int h);
void  gfx_unclip(ea_surface *s);

void  gfx_fill(ea_surface *s, int x, int y, int w, int h, ea_px c);
void  gfx_fill_a(ea_surface *s, int x, int y, int w, int h, ea_px c, int alpha);
void  gfx_frame(ea_surface *s, int x, int y, int w, int h, ea_px tl, ea_px br);
void  gfx_scanlines(ea_surface *s, int x, int y, int w, int h, int light, int mid, int dark);
void  gfx_inner_shadow(ea_surface *s, int x, int y, int w, int h, int depth, int strength);
void  gfx_glow_rect(ea_surface *s, int x, int y, int w, int h, int radius, ea_px c, int strength);
void  gfx_blit(ea_surface *d, int dx, int dy, const ea_surface *src, int sx, int sy, int w, int h);

/* gradients: elliptical radial around (cx,cy) with radii (rx,ry); and Cairo's
 * two-circle radial (focal highlight), both clipped to a rect or a disc */
void  gfx_radial_rect(ea_surface *s, int x, int y, int w, int h, float cx, float cy,
                      float rx, float ry, const ea_stop *stops, int n, int alpha);
void  gfx_disc_gradient(ea_surface *s, float cx, float cy, float r, float fx, float fy,
                        float fr, const ea_stop *stops, int n);

/* anti-aliased primitives, float coordinates */
void  gfx_line(ea_surface *s, float x0, float y0, float x1, float y1, float width, ea_px c, int alpha);
void  gfx_disc(ea_surface *s, float cx, float cy, float r, ea_px c, int alpha);
void  gfx_ring(ea_surface *s, float cx, float cy, float r, float width, ea_px c, int alpha);
void  gfx_poly(ea_surface *s, const float *xy, int n, ea_px c, int alpha);   /* convex */
void  gfx_rectf(ea_surface *s, float x, float y, float w, float h, ea_px c, int alpha);

/* text: y is the baseline. Returns the pen advance in pixels. */
#define GFX_GLOW 1
int   gfx_text(ea_surface *s, const ea_font *f, int x, int y, const char *str,
               ea_px col, int spacing, int flags, ea_px glowcol);
int   gfx_text_w(const ea_font *f, const char *str, int spacing);

ea_px gfx_mix(ea_px a, ea_px b, float t);

/* Convert a rect of `src` to 16-bit pixels with ordered dithering, for High
 * Color desktops. Left to itself GDI truncates 8-bit channels to 5 or 6 bits,
 * which erases the skin's subtle work outright: the scanline sheen is a 2-6%
 * step and the LCD / smoke gradients band. The 8x8 threshold matrix is keyed
 * to absolute screen position, so partial redraws tile without a seam and
 * nothing shimmers between frames. green_bits is 6 (5-6-5) or 5 (5-5-5). */
void  gfx_dither16(const ea_surface *src, int x, int y, int w, int h,
                   unsigned short *dst, int dst_stride_bytes, int green_bits);


/* Last resort for a screen smaller than the layout can shrink to (640x480):
 * area-average a rect of `src` down into `dst`, which is `scale` (< 1) times
 * the size. dx..dh is the destination rect. Every source pixel contributes in
 * proportion to its overlap, so thin lines and small text dim instead of
 * dropping out. */
void  gfx_downscale(const ea_surface *src, ea_surface *dst, float scale, int dx, int dy, int dw, int dh);

/* Palettized desktops (256 or 16 colours): dither into a fixed palette.
 * `lut` maps 15-bit RGB to the nearest palette index (tools/mkpalette.py);
 * `spread` is the dither amplitude in 8-bit steps - roughly the distance
 * between neighbouring palette colours (small for the tuned 236, large for
 * the VGA 16). Position-keyed like gfx_dither16, so redraws never shimmer. */
void  gfx_dither_indexed(const ea_surface *src, int x, int y, int w, int h, unsigned char *dst,
                         int dst_stride_bytes, const unsigned char *lut, int spread);

extern const int EA_PAL256_N;
extern const unsigned char EA_PAL256[], EA_LUT256[], EA_PAL16[], EA_LUT16[];

#endif
