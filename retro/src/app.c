#include "app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float GRAPHIC_FREQS[EA_GRAPHIC_N] =
    { 29, 59, 119, 237, 474, 947, 1889, 3770, 7523, 15011 };

/* the same roster as the GTK app's eqpresets.BUILTIN */
static const struct { const char *name; float g[EA_GRAPHIC_N]; } PRESETS[] = {
    { "Flat",       { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "EasyAmp",    { 0, 4, 8, 3, 3, 3, 1, 4, 4, 8 } },
    { "Rock",       { 5, 4, 2, 0, -1, 0, 2, 3, 4, 4 } },
    { "Pop",        { -1, 1, 3, 4, 4, 2, 0, -1, -1, -2 } },
    { "Jazz",       { 3, 2, 1, 2, -1, -1, 0, 1, 2, 3 } },
    { "Classical",  { 4, 3, 2, 1, -1, -1, 0, 2, 3, 4 } },
    { "Bass Boost", { 7, 6, 5, 3, 1, 0, 0, 0, 0, 0 } },
    { "Treble",     { 0, 0, 0, 0, 0, 1, 3, 5, 6, 7 } },
    { "Vocal",      { -2, -1, 0, 2, 4, 4, 3, 1, 0, -1 } },
};

int ea_preset_count(void) { return (int)(sizeof PRESETS / sizeof PRESETS[0]); }
const char *ea_preset_name(int i) { return PRESETS[i].name; }

void ea_band_freqs(int n, float *out)
{
    int i;
    if (n == EA_GRAPHIC_N) { memcpy(out, GRAPHIC_FREQS, sizeof GRAPHIC_FREQS); return; }
    for (i = 0; i < n; i++)
        out[i] = 30.0f * (float)pow(16000.0 / 30.0, (double)i / (double)(n - 1));
}

float ea_interp(float x, const float *xs, const float *ys, int n)
{
    int i;
    if (n <= 0) return 0;
    if (x <= xs[0]) return ys[0];
    if (x >= xs[n - 1]) return ys[n - 1];
    for (i = 1; i < n; i++)
        if (x <= xs[i]) {
            float d = xs[i] - xs[i - 1];
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (d != 0 ? (x - xs[i - 1]) / d : 0);
        }
    return ys[n - 1];
}

void ea_fmt_freq(float f, char *out)
{
    if (f >= 1000) {
        float v = f / 1000.0f;
        if (v >= 10 || v == (float)(int)v) sprintf(out, "%.0fK", v);
        else sprintf(out, "%.1fK", v);
    } else sprintf(out, "%.0f", f);
}

/* change the band count without wiping the curve: resample it, in log-f */
void ea_set_nbands(ea_model *m, int n)
{
    float lx[EA_MAX_BANDS], ng[EA_MAX_BANDS], nf[EA_MAX_BANDS];
    int i;
    if (n < EA_MIN_BANDS) n = EA_MIN_BANDS;
    if (n > EA_MAX_BANDS) n = EA_MAX_BANDS;
    if (n == m->nbands) return;
    for (i = 0; i < m->nbands; i++) lx[i] = (float)log(m->freqs[i]);
    ea_band_freqs(n, nf);
    for (i = 0; i < n; i++) ng[i] = ea_interp((float)log(nf[i]), lx, m->gains, m->nbands);
    for (i = 0; i < n; i++) { m->gains[i] = ng[i]; m->freqs[i] = nf[i]; m->q[i] = EA_DEFAULT_Q; m->types[i] = EA_PEAK; }
    m->nbands = n;
    if (m->selband >= n) m->selband = n - 1;
}

void ea_graphic_get(const ea_model *m, float *ten)
{
    float lx[EA_MAX_BANDS];
    int i;
    for (i = 0; i < m->nbands; i++) lx[i] = (float)log(m->freqs[i]);
    for (i = 0; i < EA_GRAPHIC_N; i++)
        ten[i] = ea_interp((float)log(GRAPHIC_FREQS[i]), lx, m->gains, m->nbands);
}

void ea_graphic_set(ea_model *m, const float *ten)
{
    float lx[EA_GRAPHIC_N];
    int i;
    for (i = 0; i < EA_GRAPHIC_N; i++) lx[i] = (float)log(GRAPHIC_FREQS[i]);
    for (i = 0; i < m->nbands; i++)
        m->gains[i] = ea_interp((float)log(m->freqs[i]), lx, ten, EA_GRAPHIC_N);
}

void ea_preset_apply(ea_model *m, int i)
{
    ea_graphic_set(m, PRESETS[i].g);
    m->preamp = 0;
    strncpy(m->preset, PRESETS[i].name, sizeof m->preset - 1);
}

void ea_model_init(ea_model *m)
{
    int i;
    memset(m, 0, sizeof *m);
    m->cur = m->sel = -1;
    m->src_sel = -1;
    strcpy(m->src_crumb, "SELECT A SOURCE");
    m->eq_on = 1;
    m->show_eq = m->show_pl = 1;
    m->nbands = EA_GRAPHIC_N;
    ea_band_freqs(m->nbands, m->freqs);
    for (i = 0; i < EA_MAX_BANDS; i++) m->q[i] = EA_DEFAULT_Q;
    m->pitch = 1.0f;
    strcpy(m->preset, "Flat");
    strcpy(m->title, "");
}

/* ---- EQ import / export --------------------------------------------------------------- */

#include <ctype.h>
#include <stdlib.h>

/* the number that follows `key` on a line (case-insensitive), e.g. "fc " -> 1000 */
static int num_after(const char *line, const char *key, float *out)
{
    size_t kl = strlen(key), i, n = strlen(line);
    for (i = 0; i + kl <= n; i++) {
        size_t k;
        for (k = 0; k < kl && tolower((unsigned char)line[i + k]) == key[k]; k++) ;
        if (k == kl && (i == 0 || !isalnum((unsigned char)line[i - 1]))) {
            const char *p = line + i + kl;
            char *end;
            double v;
            while (*p == ' ' || *p == '\t') p++;
            v = strtod(p, &end);
            if (end != p) { *out = (float)v; return 1; }
        }
    }
    return 0;
}

static int has_word(const char *line, const char *word)
{
    size_t wl = strlen(word), i, n = strlen(line);
    for (i = 0; i + wl <= n; i++) {
        size_t k;
        for (k = 0; k < wl && toupper((unsigned char)line[i + k]) == word[k]; k++) ;
        if (k == wl && (i == 0 || !isalnum((unsigned char)line[i - 1])) && !isalnum((unsigned char)line[i + wl])) return 1;
    }
    return 0;
}

typedef struct { float f, q, g; int t; } band_t;

static int by_freq(const void *a, const void *b) { float d = ((const band_t *)a)->f - ((const band_t *)b)->f; return d < 0 ? -1 : d > 0; }

int ea_eq_import(ea_model *m, const char *text)
{
    static band_t b[512];
    float preamp = 0;
    int n = 0, i;
    const char *p = text, *geq = 0;
    { const char *s = text; for (; *s; s++) if ((s[0] == 'G' || s[0] == 'g') && strlen(s) > 10) {
          char head[11]; int k; for (k = 0; k < 10; k++) head[k] = (char)tolower((unsigned char)s[k]); head[10] = 0;
          if (!strcmp(head, "graphiceq:")) { geq = s + 10; break; } } }
    if (geq) {                                                   /* "f g; f g; ..." */
        while (*geq && *geq != '\n' && n < 512) {
            char *end;
            double f = strtod(geq, &end), g;
            if (end == geq) break;
            geq = end; g = strtod(geq, &end);
            if (end == geq) break;
            geq = end;
            b[n].f = (float)f; b[n].g = (float)g; b[n].q = EA_DEFAULT_Q; b[n].t = EA_PEAK; n++;
            while (*geq == ' ' || *geq == ';' || *geq == '\t') geq++;
        }
    } else {
        char line[512];
        while (*p) {
            size_t l = 0;
            while (*p && *p != '\n' && l < sizeof line - 1) line[l++] = *p++;
            while (*p && *p != '\n') p++;
            if (*p) p++;
            line[l] = 0;
            if (has_word(line, "PREAMP")) num_after(line, "preamp:", &preamp);
            else if (has_word(line, "FILTER") && has_word(line, "ON") && n < 512) {
                float f, g = 0, q = EA_DEFAULT_Q;
                if (!num_after(line, "fc", &f)) continue;
                num_after(line, "gain", &g); num_after(line, "q", &q);
                b[n].f = f; b[n].g = g; b[n].q = q;
                b[n].t = has_word(line, "LSC") || has_word(line, "LS") || has_word(line, "LSQ") ? EA_LOW_SHELF :
                         has_word(line, "HSC") || has_word(line, "HS") || has_word(line, "HSQ") ? EA_HIGH_SHELF : EA_PEAK;
                n++;
            }
        }
    }
    if (n == 0) return 0;
    if (n > EA_MAX_BANDS) {                                      /* e.g. a 127-point GraphicEQ: keep the ends, sample the rest */
        band_t keep[EA_MAX_BANDS];
        for (i = 0; i < EA_MAX_BANDS; i++) keep[i] = b[(int)((double)i * (n - 1) / (EA_MAX_BANDS - 1) + 0.5)];
        memcpy(b, keep, sizeof keep); n = EA_MAX_BANDS;
    }
    for (i = 0; n < EA_MIN_BANDS; i++) {                         /* fewer than the bank's minimum: pad with flat bands */
        int k, clash = 0;
        if (i >= EA_GRAPHIC_N) break;
        for (k = 0; k < n; k++) if (fabs(log(b[k].f / GRAPHIC_FREQS[i])) < 0.2) clash = 1;
        if (clash) continue;
        b[n].f = GRAPHIC_FREQS[i]; b[n].g = 0; b[n].q = EA_DEFAULT_Q; b[n].t = EA_PEAK; n++;
    }
    qsort(b, (size_t)n, sizeof b[0], by_freq);
    m->nbands = n;
    for (i = 0; i < n; i++) {
        m->freqs[i] = b[i].f < 10 ? 10 : (b[i].f > 22000 ? 22000 : b[i].f);
        m->gains[i] = b[i].g < EA_BAND_MIN ? EA_BAND_MIN : (b[i].g > EA_BAND_MAX ? EA_BAND_MAX : b[i].g);
        m->q[i] = b[i].q < 0.1f ? 0.1f : (b[i].q > 12 ? 12 : b[i].q);
        m->types[i] = b[i].t;
    }
    m->preamp = preamp < EA_PRE_MIN ? EA_PRE_MIN : (preamp > EA_PRE_MAX ? EA_PRE_MAX : preamp);
    if (m->selband >= n) m->selband = n - 1;
    strcpy(m->preset, "Imported");
    return 1;
}

int ea_eq_export_apo(const ea_model *m, char *out, int cap)
{
    static const char *tn[3] = { "PK", "LSC", "HSC" };
    int i, o = sprintf(out, "Preamp: %.1f dB\r\n", m->preamp);
    for (i = 0; i < m->nbands && o < cap - 96; i++)
        o += sprintf(out + o, "Filter %d: ON %s Fc %.0f Hz Gain %.1f dB Q %.2f\r\n", i + 1, tn[m->types[i] < 3 ? m->types[i] : 0],
                     m->freqs[i], m->gains[i], m->q[i]);
    return o;
}

int ea_eq_export_geq(const ea_model *m, char *out, int cap)
{
    int i, o = sprintf(out, "GraphicEQ: ");
    for (i = 0; i < m->nbands && o < cap - 32; i++) o += sprintf(out + o, "%s%.0f %.1f", i ? "; " : "", m->freqs[i], m->gains[i]);
    o += sprintf(out + o, "\r\n");
    return o;
}
