#include "gfx.h"
#include <math.h>
#include <string.h>

#define CLAMPI(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

static ea_px blend(ea_px d, ea_px c, int a)
{
    int r, g, b;
    if (a >= 255) return c;
    if (a <= 0) return d;
    r = EA_R(d) + ((EA_R(c) - EA_R(d)) * a) / 255;
    g = EA_G(d) + ((EA_G(c) - EA_G(d)) * a) / 255;
    b = EA_B(d) + ((EA_B(c) - EA_B(d)) * a) / 255;
    return EA_RGB(r, g, b);
}

ea_px gfx_mix(ea_px a, ea_px b, float t)
{
    if (t <= 0) return a;
    if (t >= 1) return b;
    return EA_RGB((int)(EA_R(a) + (EA_R(b) - EA_R(a)) * t + 0.5f),
                  (int)(EA_G(a) + (EA_G(b) - EA_G(a)) * t + 0.5f),
                  (int)(EA_B(a) + (EA_B(b) - EA_B(a)) * t + 0.5f));
}

int gfx_init(ea_surface *s, int w, int h, ea_px *pixels)
{
    s->w = w; s->h = h; s->px = pixels;
    gfx_unclip(s);
    return pixels != 0;
}

void gfx_clip(ea_surface *s, int x, int y, int w, int h)
{
    s->cx0 = CLAMPI(x, 0, s->w);     s->cy0 = CLAMPI(y, 0, s->h);
    s->cx1 = CLAMPI(x + w, 0, s->w); s->cy1 = CLAMPI(y + h, 0, s->h);
}

void gfx_unclip(ea_surface *s) { s->cx0 = s->cy0 = 0; s->cx1 = s->w; s->cy1 = s->h; }

/* clip a rect to the surface; returns 0 when nothing is left */
static int clip_rect(const ea_surface *s, int *x, int *y, int *w, int *h)
{
    int x1 = *x + *w, y1 = *y + *h;
    if (*x < s->cx0) *x = s->cx0;
    if (*y < s->cy0) *y = s->cy0;
    if (x1 > s->cx1) x1 = s->cx1;
    if (y1 > s->cy1) y1 = s->cy1;
    *w = x1 - *x; *h = y1 - *y;
    return *w > 0 && *h > 0;
}

void gfx_fill(ea_surface *s, int x, int y, int w, int h, ea_px c)
{
    int i, j;
    if (!clip_rect(s, &x, &y, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y + j) * s->w + x;
        for (i = 0; i < w; i++) p[i] = c;
    }
}

void gfx_fill_a(ea_surface *s, int x, int y, int w, int h, ea_px c, int alpha)
{
    int i, j;
    if (alpha >= 255) { gfx_fill(s, x, y, w, h, c); return; }
    if (!clip_rect(s, &x, &y, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y + j) * s->w + x;
        for (i = 0; i < w; i++) p[i] = blend(p[i], c, alpha);
    }
}

/* 1px bevel: top + left in one colour, bottom + right in the other */
void gfx_frame(ea_surface *s, int x, int y, int w, int h, ea_px tl, ea_px br)
{
    if (w < 2 || h < 2) return;
    gfx_fill(s, x, y, w, 1, tl);
    gfx_fill(s, x, y, 1, h, tl);
    gfx_fill(s, x, y + h - 1, w, 1, br);
    gfx_fill(s, x + w - 1, y, 1, h, br);
}

/* the 3px repeating sheen used on every metal surface: a light line, a
 * neutral line, a dark line (alphas 0..255: white, black, black) */
void gfx_scanlines(ea_surface *s, int x, int y, int w, int h, int light, int mid, int dark)
{
    int j, y0 = y;
    for (j = 0; j < h; j++) {
        int k = (y0 + j - y) % 3;
        if (k == 0) gfx_fill_a(s, x, y + j, w, 1, EA_RGB(255, 255, 255), light);
        else gfx_fill_a(s, x, y + j, w, 1, EA_RGB(0, 0, 0), k == 1 ? mid : dark);
    }
}

/* CSS "box-shadow: inset 0 0 Npx black": darken towards the edges */
void gfx_inner_shadow(ea_surface *s, int x, int y, int w, int h, int depth, int strength)
{
    int i, j, x0 = x, y0 = y, w0 = w, h0 = h;
    if (!clip_rect(s, &x, &y, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y + j) * s->w + x;
        int dy = y + j - y0, ey = dy < h0 - 1 - dy ? dy : h0 - 1 - dy;
        for (i = 0; i < w; i++) {
            int dx = x + i - x0, ex = dx < w0 - 1 - dx ? dx : w0 - 1 - dx;
            int e = ex < ey ? ex : ey;
            if (e < depth) {
                float t = 1.0f - (float)e / (float)depth;
                p[i] = blend(p[i], 0, (int)(t * t * strength));
            }
        }
    }
}

void gfx_blit(ea_surface *d, int dx, int dy, const ea_surface *src, int sx, int sy, int w, int h)
{
    int j, ox = dx, oy = dy;
    if (!clip_rect(d, &dx, &dy, &w, &h)) return;
    sx += dx - ox; sy += dy - oy;
    if (sx < 0 || sy < 0 || sx + w > src->w || sy + h > src->h) return;
    for (j = 0; j < h; j++)
        memcpy(d->px + (dy + j) * d->w + dx, src->px + (sy + j) * src->w + sx, (size_t)w * sizeof(ea_px));
}

static ea_px stops_at(const ea_stop *st, int n, float t)
{
    int i;
    if (t <= st[0].t) return st[0].col;
    for (i = 1; i < n; i++)
        if (t <= st[i].t)
            return gfx_mix(st[i - 1].col, st[i].col, (t - st[i - 1].t) / (st[i].t - st[i - 1].t));
    return st[n - 1].col;
}

void gfx_radial_rect(ea_surface *s, int x, int y, int w, int h, float cx, float cy,
                     float rx, float ry, const ea_stop *stops, int n, int alpha)
{
    int i, j;
    if (!clip_rect(s, &x, &y, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y + j) * s->w + x;
        float dy = ((float)(y + j) + 0.5f - cy) / ry;
        for (i = 0; i < w; i++) {
            float dx = ((float)(x + i) + 0.5f - cx) / rx;
            p[i] = blend(p[i], stops_at(stops, n, (float)sqrt(dx * dx + dy * dy)), alpha);
        }
    }
}

/* Cairo's two-circle radial gradient, painted into the disc (cx,cy,r).
 * The start circle (fx,fy,fr) sits off-centre, which is what gives the
 * knobs their brushed-metal highlight. */
void gfx_disc_gradient(ea_surface *s, float cx, float cy, float r, float fx, float fy,
                       float fr, const ea_stop *stops, int n)
{
    int x0 = (int)floor(cx - r - 1), y0 = (int)floor(cy - r - 1);
    int w = (int)ceil(2 * r + 3), h = w, i, j;
    float cdx = cx - fx, cdy = cy - fy, dr = r - fr;
    float a = cdx * cdx + cdy * cdy - dr * dr;
    if (!clip_rect(s, &x0, &y0, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y0 + j) * s->w + x0;
        for (i = 0; i < w; i++) {
            float px = (float)(x0 + i) + 0.5f, py = (float)(y0 + j) + 0.5f;
            float d = (float)sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
            float cov = r + 0.5f - d, pdx, pdy, b, c, t;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            pdx = px - fx; pdy = py - fy;
            b = pdx * cdx + pdy * cdy + fr * dr;
            c = pdx * pdx + pdy * pdy - fr * fr;
            if (fabs(a) < 1e-6f) t = b != 0 ? c / (2 * b) : 0;
            else {
                float disc = b * b - a * c;
                t = disc < 0 ? 0 : (b - (float)sqrt(disc)) / a;   /* a < 0 here */
                if (t < 0 || t != t) t = (b + (float)sqrt(disc < 0 ? 0 : disc)) / a;
            }
            p[i] = blend(p[i], stops_at(stops, n, t), (int)(cov * 255));
        }
    }
}

void gfx_line(ea_surface *s, float x0, float y0, float x1, float y1, float width, ea_px c, int alpha)
{
    float hw = width * 0.5f, dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    int bx = (int)floor((x0 < x1 ? x0 : x1) - hw - 1), by = (int)floor((y0 < y1 ? y0 : y1) - hw - 1);
    int bw = (int)ceil((x0 > x1 ? x0 : x1) + hw + 1) - bx, bh = (int)ceil((y0 > y1 ? y0 : y1) + hw + 1) - by;
    int i, j;
    if (!clip_rect(s, &bx, &by, &bw, &bh)) return;
    for (j = 0; j < bh; j++) {
        ea_px *p = s->px + (by + j) * s->w + bx;
        for (i = 0; i < bw; i++) {
            float px = (float)(bx + i) + 0.5f - x0, py = (float)(by + j) + 0.5f - y0;
            float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0, ex, ey, cov;
            if (t < 0) t = 0; else if (t > 1) t = 1;
            ex = px - t * dx; ey = py - t * dy;
            cov = hw + 0.5f - (float)sqrt(ex * ex + ey * ey);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            p[i] = blend(p[i], c, (int)(cov * alpha));
        }
    }
}

void gfx_disc(ea_surface *s, float cx, float cy, float r, ea_px c, int alpha)
{
    int x0 = (int)floor(cx - r - 1), y0 = (int)floor(cy - r - 1), w = (int)ceil(2 * r + 3), h = w, i, j;
    if (!clip_rect(s, &x0, &y0, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y0 + j) * s->w + x0;
        for (i = 0; i < w; i++) {
            float px = (float)(x0 + i) + 0.5f - cx, py = (float)(y0 + j) + 0.5f - cy;
            float cov = r + 0.5f - (float)sqrt(px * px + py * py);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            p[i] = blend(p[i], c, (int)(cov * alpha));
        }
    }
}

void gfx_ring(ea_surface *s, float cx, float cy, float r, float width, ea_px c, int alpha)
{
    float hw = width * 0.5f;
    int x0 = (int)floor(cx - r - hw - 1), y0 = (int)floor(cy - r - hw - 1), w = (int)ceil(2 * (r + hw) + 3), h = w, i, j;
    if (!clip_rect(s, &x0, &y0, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y0 + j) * s->w + x0;
        for (i = 0; i < w; i++) {
            float px = (float)(x0 + i) + 0.5f - cx, py = (float)(y0 + j) + 0.5f - cy;
            float cov = hw + 0.5f - (float)fabs(sqrt(px * px + py * py) - r);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            p[i] = blend(p[i], c, (int)(cov * alpha));
        }
    }
}

/* convex polygon, either winding: coverage = the smallest edge distance */
void gfx_poly(ea_surface *s, const float *xy, int n, ea_px c, int alpha)
{
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1], area = 0;
    int i, j, k, bx, by, bw, bh;
    for (k = 0; k < n; k++) {
        float x = xy[2 * k], y = xy[2 * k + 1], nx = xy[2 * ((k + 1) % n)], ny = xy[2 * ((k + 1) % n) + 1];
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
        area += x * ny - nx * y;
    }
    bx = (int)floor(minx - 1); by = (int)floor(miny - 1);
    bw = (int)ceil(maxx + 1) - bx; bh = (int)ceil(maxy + 1) - by;
    if (!clip_rect(s, &bx, &by, &bw, &bh)) return;
    for (j = 0; j < bh; j++) {
        ea_px *p = s->px + (by + j) * s->w + bx;
        for (i = 0; i < bw; i++) {
            float px = (float)(bx + i) + 0.5f, py = (float)(by + j) + 0.5f, cov = 1.0f;
            for (k = 0; k < n; k++) {
                float x = xy[2 * k], y = xy[2 * k + 1];
                float ex = xy[2 * ((k + 1) % n)] - x, ey = xy[2 * ((k + 1) % n) + 1] - y;
                float el = (float)sqrt(ex * ex + ey * ey), d;
                if (el <= 0) continue;
                d = ((px - x) * ey - (py - y) * ex) / el;
                if (area > 0) d = -d;
                d += 0.5f;
                if (d < cov) cov = d;
                if (cov <= 0) break;
            }
            if (cov <= 0) continue;
            p[i] = blend(p[i], c, (int)(cov * alpha));
        }
    }
}

void gfx_rectf(ea_surface *s, float x, float y, float w, float h, ea_px c, int alpha)
{
    float q[8];
    q[0] = x; q[1] = y; q[2] = x + w; q[3] = y; q[4] = x + w; q[5] = y + h; q[6] = x; q[7] = y + h;
    gfx_poly(s, q, 4, c, alpha);
}

static void glyph_blit(ea_surface *s, const unsigned char *cov, int gw, int gh, int x, int y, ea_px c)
{
    int i, j, ox = x, oy = y, w = gw, h = gh;
    if (!clip_rect(s, &x, &y, &w, &h)) return;
    for (j = 0; j < h; j++) {
        ea_px *p = s->px + (y + j) * s->w + x;
        const unsigned char *a = cov + (y + j - oy) * gw + (x - ox);
        for (i = 0; i < w; i++)
            if (a[i]) p[i] = blend(p[i], c, a[i]);
    }
}

static const ea_glyph *glyph_of(const ea_font *f, unsigned char ch)
{
    if (ch < 32 || ch > 126) ch = '?';
    return &f->glyphs[ch - 32];
}

int gfx_text_w(const ea_font *f, const char *str, int spacing)
{
    int w = 0;
    for (; *str; str++) w += glyph_of(f, (unsigned char)*str)->adv + spacing;
    return w;
}

int gfx_text(ea_surface *s, const ea_font *f, int x, int y, const char *str,
             ea_px col, int spacing, int flags, ea_px glowcol)
{
    const char *p;
    int pen = x;
    if ((flags & GFX_GLOW) && f->has_glow) {           /* halo first, under every glyph */
        for (p = str; *p; p++) {
            const ea_glyph *g = glyph_of(f, (unsigned char)*p);
            if (g->gw) glyph_blit(s, f->data + g->goff, g->gw, g->gh, pen + g->gxoff, y + g->gyoff, glowcol);
            pen += g->adv + spacing;
        }
        pen = x;
    }
    for (p = str; *p; p++) {
        const ea_glyph *g = glyph_of(f, (unsigned char)*p);
        if (g->w) glyph_blit(s, f->data + g->off, g->w, g->h, pen + g->xoff, y + g->yoff, col);
        pen += g->adv + spacing;
    }
    return pen - x;
}

/* soft halo around a rect (CSS "box-shadow: 0 0 Npx colour") - lit LEDs */
void gfx_glow_rect(ea_surface *s, int x, int y, int w, int h, int radius, ea_px c, int strength)
{
    int bx = x - radius, by = y - radius, bw = w + 2 * radius, bh = h + 2 * radius, i, j;
    if (!clip_rect(s, &bx, &by, &bw, &bh)) return;
    for (j = 0; j < bh; j++) {
        ea_px *p = s->px + (by + j) * s->w + bx;
        int py = by + j, dy = py < y ? y - py : (py >= y + h ? py - (y + h - 1) : 0);
        for (i = 0; i < bw; i++) {
            int px = bx + i, dx = px < x ? x - px : (px >= x + w ? px - (x + w - 1) : 0);
            float d = (float)sqrt((double)(dx * dx + dy * dy)), t;
            if (d <= 0 || d >= radius) continue;
            t = 1.0f - d / (float)radius;
            p[i] = blend(p[i], c, (int)(t * t * strength));
        }
    }
}

static const unsigned char BAYER8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,   48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,   60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,   51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,   63, 31, 55, 23, 61, 29, 53, 21 };

void gfx_dither16(const ea_surface *src, int x, int y, int w, int h,
                  unsigned short *dst, int dst_stride_bytes, int green_bits)
{
    int i, j, gmax = (1 << green_bits) - 1, rshift = 5 + green_bits;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > src->w) w = src->w - x;
    if (y + h > src->h) h = src->h - y;
    for (j = 0; j < h; j++) {
        const ea_px *p = src->px + (y + j) * src->w + x;
        unsigned short *o = (unsigned short *)((unsigned char *)dst + (y + j) * dst_stride_bytes) + x;
        const unsigned char *row = BAYER8 + ((y + j) & 7) * 8;
        for (i = 0; i < w; i++) {
            /* threshold in 0..254: a value exactly on a 16-bit level stays
             * flat, anything between two levels is mixed from both */
            int thr = (row[(x + i) & 7] * 255 + 127) / 64;
            int r = (EA_R(p[i]) * 31 + thr) / 255, g = (EA_G(p[i]) * gmax + thr) / 255, b = (EA_B(p[i]) * 31 + thr) / 255;
            o[i] = (unsigned short)((r << rshift) | (g << 5) | b);
        }
    }
}

void gfx_dither_indexed(const ea_surface *src, int x, int y, int w, int h, unsigned char *dst,
                        int dst_stride_bytes, const unsigned char *lut, int spread)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > src->w) w = src->w - x;
    if (y + h > src->h) h = src->h - y;
    for (j = 0; j < h; j++) {
        const ea_px *p = src->px + (y + j) * src->w + x;
        unsigned char *o = dst + (y + j) * dst_stride_bytes + x;
        const unsigned char *row = BAYER8 + ((y + j) & 7) * 8;
        for (i = 0; i < w; i++) {
            int d = ((row[(x + i) & 7] * 2 - 63) * spread) / 128;     /* -spread/2 .. +spread/2 */
            int r = EA_R(p[i]) + d, g = EA_G(p[i]) + d, b = EA_B(p[i]) + d;
            r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
            o[i] = lut[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
        }
    }
}

void gfx_downscale(const ea_surface *src, ea_surface *dst, float scale, int dx, int dy, int dw, int dh)
{
    /* 16.16 fixed point: one destination pixel covers `step` source pixels */
    unsigned step = (unsigned)(65536.0f / scale);
    int X, Y;
    if (dx < 0) { dw += dx; dx = 0; }
    if (dy < 0) { dh += dy; dy = 0; }
    if (dx + dw > dst->w) dw = dst->w - dx;
    if (dy + dh > dst->h) dh = dst->h - dy;
    for (Y = dy; Y < dy + dh; Y++) {
        unsigned sy0 = (unsigned)Y * step, sy1 = sy0 + step;
        for (X = dx; X < dx + dw; X++) {
            unsigned sx0 = (unsigned)X * step, sx1 = sx0 + step, r = 0, g = 0, b = 0, wsum = 0, y;
            for (y = sy0 >> 16; y <= (sy1 - 1) >> 16 && (int)y < src->h; y++) {
                unsigned ya = y << 16, yb = ya + 65536, wy = ((yb < sy1 ? yb : sy1) - (ya > sy0 ? ya : sy0)) >> 8, x;
                const ea_px *row = src->px + y * (unsigned)src->w;
                for (x = sx0 >> 16; x <= (sx1 - 1) >> 16 && (int)x < src->w; x++) {
                    unsigned xa = x << 16, xb = xa + 65536, wx = ((xb < sx1 ? xb : sx1) - (xa > sx0 ? xa : sx0)) >> 8;
                    unsigned w = (wx * wy) >> 8;                      /* 0..256 */
                    ea_px p = row[x];
                    r += (unsigned)EA_R(p) * w; g += (unsigned)EA_G(p) * w; b += (unsigned)EA_B(p) * w; wsum += w;
                }
            }
            if (wsum) dst->px[Y * dst->w + X] = EA_RGB(r / wsum, g / wsum, b / wsum);
        }
    }
}
