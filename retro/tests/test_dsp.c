/* known-answer checks for the signal path and the analyzer */
#include "../src/dsp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* steady-state gain (dB) of the chain for a sine at f */
static double gain_at(ea_chain *c, double f, int rate)
{
    static float buf[2 * 16384];
    double in = 0, out = 0;
    int i, n = 16384;
    for (i = 0; i < n; i++) buf[2 * i] = buf[2 * i + 1] = 0.1f * (float)sin(2 * 3.14159265358979 * f * i / rate);
    for (i = n / 2; i < n; i++) in += buf[2 * i] * buf[2 * i];
    chain_process(c, buf, n);
    for (i = n / 2; i < n; i++) out += buf[2 * i] * buf[2 * i];
    return 10.0 * log10(out / in);
}

int main(void)
{
    ea_model m;
    ea_chain c;
    static ea_analyzer a;
    static short pcm[2 * EA_FFT];
    int i, best = 0, rate = 44100;
    double g;

    ea_model_init(&m);
    chain_init(&c, rate); chain_config(&c, &m);
    g = gain_at(&c, 1000, rate);
    CHECK(fabs(g) < 0.05, "flat chain should be unity, got %+.2f dB", g);

    m.gains[5] = 9.0f;                                   /* 947 Hz band */
    chain_init(&c, rate); chain_config(&c, &m);
    g = gain_at(&c, m.freqs[5], rate);
    CHECK(fabs(g - 9.0) < 0.2, "+9 dB peak at centre, got %+.2f dB", g);
    g = gain_at(&c, 60, rate);
    CHECK(fabs(g) < 0.5, "+9 dB at 947 Hz must leave 60 Hz alone, got %+.2f dB", g);

    m.gains[5] = -12.0f;
    chain_init(&c, rate); chain_config(&c, &m);
    g = gain_at(&c, m.freqs[5], rate);
    CHECK(fabs(g + 12.0) < 0.2, "-12 dB cut at centre, got %+.2f dB", g);

    m.gains[5] = 9.0f; m.eq_on = 0;
    chain_init(&c, rate); chain_config(&c, &m);
    g = gain_at(&c, m.freqs[5], rate);
    CHECK(fabs(g) < 0.05, "EQ off must bypass the bands, got %+.2f dB", g);

    ea_model_init(&m); m.bass = 1;
    chain_init(&c, rate); chain_config(&c, &m);
    g = gain_at(&c, 40, rate);
    CHECK(g > 5.0 && g < 6.5, "BASS = about +6 dB at 40 Hz, got %+.2f dB", g);
    g = gain_at(&c, 5000, rate);
    CHECK(fabs(g) < 0.3, "BASS must leave 5 kHz alone, got %+.2f dB", g);

    ea_model_init(&m); m.balance = -1.0f;                /* hard left */
    chain_init(&c, rate); chain_config(&c, &m);
    CHECK(c.gain_l > 0.99f && c.gain_r < 0.01f, "balance hard left: L=%.2f R=%.2f", c.gain_l, c.gain_r);

    /* a 1 kHz tone must light the band that contains 1 kHz, and nothing far away */
    ea_model_init(&m);
    ana_init(&a, rate);
    for (i = 0; i < EA_FFT; i++) pcm[2 * i] = pcm[2 * i + 1] = (short)(12000 * sin(2 * 3.14159265358979 * 1000.0 * i / rate));
    for (i = 0; i < 8; i++) ana_run(&a, pcm, &m);
    for (i = 1; i < EA_VIZ_BANDS; i++) if (m.levels[i] > m.levels[best]) best = i;
    {
        double lo = 40.0 * pow(22050.0 / 40.0, (double)best / EA_VIZ_BANDS), hi = 40.0 * pow(22050.0 / 40.0, (double)(best + 1) / EA_VIZ_BANDS);
        CHECK(lo <= 1000 && 1000 < hi, "1 kHz tone peaked in band %d (%.0f-%.0f Hz)", best, lo, hi);
    }
    /* a band reports the MEAN of its bins (same as the GTK app), so one pure
     * tone in a ~15-bin band reads lower than broadband music would */
    CHECK(m.levels[best] > 0.35f, "1 kHz band level %.2f should be clearly lit", m.levels[best]);
    CHECK(m.levels[0] < 0.2f && m.levels[EA_VIZ_BANDS - 1] < 0.2f, "far bands should stay dark: %.2f / %.2f", m.levels[0], m.levels[EA_VIZ_BANDS - 1]);
    CHECK(fabs(m.vu_l - m.vu_r) < 0.01f && m.vu_l > 0.6f, "VU for a -8.7 dBFS tone: %.2f / %.2f", m.vu_l, m.vu_r);

    /* band count changes must resample the curve, not wipe it */
    ea_model_init(&m); ea_preset_apply(&m, 6);           /* Bass Boost */
    ea_set_nbands(&m, 24);
    CHECK(m.nbands == 24 && m.gains[0] > 6.0f && fabs(m.gains[23]) < 0.01f, "24-band resample: first %.1f last %.1f", m.gains[0], m.gains[23]);

    printf(fails ? "%d FAILED\n" : "all dsp checks passed\n", fails);
    return fails != 0;
}
