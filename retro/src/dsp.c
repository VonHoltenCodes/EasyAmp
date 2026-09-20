#include "dsp.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265358979f

/* RBJ audio-EQ-cookbook filters */
static void set_coefs(ea_biquad *b, float b0, float b1, float b2, float a0, float a1, float a2)
{
    b->b0 = b0 / a0; b->b1 = b1 / a0; b->b2 = b2 / a0; b->a1 = a1 / a0; b->a2 = a2 / a0;
}

static void peak(ea_biquad *b, float fs, float f0, float q, float db)
{
    float A = (float)pow(10.0, db / 40.0), w = 2 * PI * f0 / fs, al = (float)sin(w) / (2 * q), cs = (float)cos(w);
    b->active = fabs(db) > 0.05f && f0 < fs * 0.49f;
    if (b->active) set_coefs(b, 1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
}

static void shelf(ea_biquad *b, float fs, float f0, float db, int high)
{
    float A = (float)pow(10.0, db / 40.0), w = 2 * PI * f0 / fs, cs = (float)cos(w), sn = (float)sin(w);
    float al = sn / 2 * (float)sqrt(2.0), sq = 2 * (float)sqrt(A) * al;
    b->active = fabs(db) > 0.05f;
    if (!b->active) return;
    if (high) set_coefs(b, A * ((A + 1) + (A - 1) * cs + sq), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sq),
                        (A + 1) - (A - 1) * cs + sq, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sq);
    else      set_coefs(b, A * ((A + 1) - (A - 1) * cs + sq), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sq),
                        (A + 1) + (A - 1) * cs + sq, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sq);
}

void chain_init(ea_chain *c, int rate)
{
    memset(c, 0, sizeof *c);
    c->rate = rate;
    c->gain_l = c->gain_r = 1.0f;
}

void chain_config(ea_chain *c, const ea_model *m)
{
    float fs = (float)c->rate, lin, bal_l, bal_r;
    int i;
    c->eq_on = m->eq_on;
    c->nbands = m->nbands;
    for (i = 0; i < m->nbands; i++) peak(&c->band[i], fs, m->freqs[i], m->q[i] > 0.1f ? m->q[i] : 0.1f, m->gains[i]);
    for (; i < EA_MAX_BANDS; i++) { c->band[i].active = 0; memset(c->band[i].z1, 0, sizeof c->band[i].z1); memset(c->band[i].z2, 0, sizeof c->band[i].z2); }
    /* same voicing as the GTK app: BASS = +6 dB low, LOUD = +4 dB low and high */
    shelf(&c->low, fs, 110.0f, (m->bass ? 6.0f : 0.0f) + (m->loud ? 4.0f : 0.0f), 0);
    shelf(&c->high, fs, 8000.0f, m->loud ? 4.0f : 0.0f, 1);
    lin = (float)pow(10.0, (m->in_gain + m->out_gain + (m->eq_on ? m->preamp : 0.0f)) / 20.0);
    bal_l = m->balance > 0 ? 1.0f - m->balance : 1.0f;
    bal_r = m->balance < 0 ? 1.0f + m->balance : 1.0f;
    c->gain_l = lin * bal_l;
    c->gain_r = lin * bal_r;
}

/* transposed direct form II, both channels of an interleaved buffer */
static void run(ea_biquad *b, float *x, int frames)
{
    float b0 = b->b0, b1 = b->b1, b2 = b->b2, a1 = b->a1, a2 = b->a2;
    float l1 = b->z1[0], l2 = b->z2[0], r1 = b->z1[1], r2 = b->z2[1];
    int i;
    for (i = 0; i < frames; i++) {
        float in = x[0], out = b0 * in + l1;
        l1 = b1 * in - a1 * out + l2; l2 = b2 * in - a2 * out;
        x[0] = out;
        in = x[1]; out = b0 * in + r1;
        r1 = b1 * in - a1 * out + r2; r2 = b2 * in - a2 * out;
        x[1] = out;
        x += 2;
    }
    /* a decaying filter tail turns into denormals, and x87 denormals run an
     * order of magnitude slower - flush them rather than let playback stutter */
    if (fabs(l1) < 1e-20f) l1 = 0;
    if (fabs(l2) < 1e-20f) l2 = 0;
    if (fabs(r1) < 1e-20f) r1 = 0;
    if (fabs(r2) < 1e-20f) r2 = 0;
    b->z1[0] = l1; b->z2[0] = l2; b->z1[1] = r1; b->z2[1] = r2;
}

void chain_process(ea_chain *c, float *x, int frames)
{
    int i;
    if (c->eq_on)
        for (i = 0; i < c->nbands; i++) if (c->band[i].active) run(&c->band[i], x, frames);
    if (c->low.active) run(&c->low, x, frames);
    if (c->high.active) run(&c->high, x, frames);
    for (i = 0; i < frames; i++) {
        float l = x[2 * i] * c->gain_l, r = x[2 * i + 1] * c->gain_r;
        /* soft knee above -1 dBFS instead of hard wrap-around clipping */
        if (l > 0.89f) l = 0.89f + (l - 0.89f) / (1.0f + (l - 0.89f) * 9.0f);
        else if (l < -0.89f) l = -0.89f + (l + 0.89f) / (1.0f - (l + 0.89f) * 9.0f);
        if (r > 0.89f) r = 0.89f + (r - 0.89f) / (1.0f + (r - 0.89f) * 9.0f);
        else if (r < -0.89f) r = -0.89f + (r + 0.89f) / (1.0f - (r + 0.89f) * 9.0f);
        x[2 * i] = l; x[2 * i + 1] = r;
    }
}

/* ---- analysis: the numbers behind the spectrum, VU needles and scope ---- */

void ana_init(ea_analyzer *a, int rate)
{
    int i, j, bits = 0;
    double lo = 40.0, hi = rate / 2.0;
    memset(a, 0, sizeof *a);
    a->rate = rate;
    while ((1 << bits) < EA_FFT) bits++;
    for (i = 0; i < EA_FFT; i++) {
        int r = 0;
        for (j = 0; j < bits; j++) if (i & (1 << j)) r |= 1 << (bits - 1 - j);
        a->rev[i] = r;
        a->window[i] = 0.5f - 0.5f * (float)cos(2 * PI * i / (EA_FFT - 1));
    }
    for (i = 0; i < EA_FFT / 2; i++) { a->cosv[i] = (float)cos(2 * PI * i / EA_FFT); a->sinv[i] = (float)sin(2 * PI * i / EA_FFT); }
    for (i = 0; i < EA_VIZ_BANDS; i++) {     /* log-spaced 40 Hz .. Nyquist, as in the GTK app */
        double f0 = lo * pow(hi / lo, (double)i / EA_VIZ_BANDS), f1 = lo * pow(hi / lo, (double)(i + 1) / EA_VIZ_BANDS);
        a->bin_lo[i] = (int)ceil(f0 * EA_FFT / rate);
        a->bin_hi[i] = (int)ceil(f1 * EA_FFT / rate);
        if (a->bin_hi[i] <= a->bin_lo[i]) a->bin_hi[i] = a->bin_lo[i] + 1;
        if (a->bin_hi[i] > EA_FFT / 2) a->bin_hi[i] = EA_FFT / 2;
    }
}

static float level_of(double sumsq, int n, float floor_db)
{
    double rms = sqrt(sumsq / (n > 0 ? n : 1)), db = 20.0 * log10(rms + 1e-9);
    float v = (float)((db - floor_db) / -floor_db);
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

void ana_run(ea_analyzer *a, const short *pcm, ea_model *m)
{
    static float re[EA_FFT], im[EA_FFT];
    double sl = 0, sr = 0;
    int i, j, k, len, step;
    for (i = 0; i < EA_FFT; i++) {
        float l = pcm[2 * i] / 32768.0f, r = pcm[2 * i + 1] / 32768.0f;
        sl += l * l; sr += r * r;
        re[a->rev[i]] = (l + r) * 0.5f * a->window[i];
        im[a->rev[i]] = 0;
    }
    for (len = 2; len <= EA_FFT; len <<= 1) {
        int half = len / 2, tstep = EA_FFT / len;
        for (i = 0; i < EA_FFT; i += len)
            for (j = 0, k = 0; j < half; j++, k += tstep) {
                float wr = a->cosv[k], wi = -a->sinv[k];
                float xr = re[i + j + half] * wr - im[i + j + half] * wi, xi = re[i + j + half] * wi + im[i + j + half] * wr;
                re[i + j + half] = re[i + j] - xr; im[i + j + half] = im[i + j] - xi;
                re[i + j] += xr; im[i + j] += xi;
            }
    }
    for (i = 0; i < EA_VIZ_BANDS; i++) {
        double acc = 0;
        float v;
        for (j = a->bin_lo[i]; j < a->bin_hi[i]; j++) acc += sqrt(re[j] * re[j] + im[j] * im[j]) / (EA_FFT / 2);
        acc /= (a->bin_hi[i] - a->bin_lo[i]);
        v = (float)((20.0 * log10(acc + 1e-9) + 60.0) / 60.0);
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        a->smooth[i] = v > a->smooth[i] * 0.80f ? v : a->smooth[i] * 0.80f;
        m->levels[i] = a->smooth[i];
    }
    a->vu_l = a->vu_l * 0.7f + level_of(sl, EA_FFT, -50.0f) * 0.3f;
    a->vu_r = a->vu_r * 0.7f + level_of(sr, EA_FFT, -50.0f) * 0.3f;
    m->vu_l = a->vu_l; m->vu_r = a->vu_r;
    step = EA_FFT / EA_WAVE;
    for (i = 0; i < EA_WAVE; i++) m->wave[i] = (pcm[2 * i * step] + pcm[2 * i * step + 1]) / 65536.0f;
}

void ana_decay(ea_analyzer *a, ea_model *m)
{
    int i;
    for (i = 0; i < EA_VIZ_BANDS; i++) { a->smooth[i] *= 0.80f; if (a->smooth[i] < 0.004f) a->smooth[i] = 0; m->levels[i] = a->smooth[i]; }
    a->vu_l *= 0.7f; a->vu_r *= 0.7f;
    if (a->vu_l < 0.004f) a->vu_l = 0;
    if (a->vu_r < 0.004f) a->vu_r = 0;
    m->vu_l = a->vu_l; m->vu_r = a->vu_r;
    for (i = 0; i < EA_WAVE; i++) m->wave[i] *= 0.6f;
}
