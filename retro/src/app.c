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
    for (i = 0; i < n; i++) { m->gains[i] = ng[i]; m->freqs[i] = nf[i]; m->q[i] = EA_DEFAULT_Q; }
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
    m->nbands = EA_GRAPHIC_N;
    ea_band_freqs(m->nbands, m->freqs);
    for (i = 0; i < EA_MAX_BANDS; i++) m->q[i] = EA_DEFAULT_Q;
    m->pitch = 1.0f;
    strcpy(m->preset, "Flat");
    strcpy(m->title, "");
}
