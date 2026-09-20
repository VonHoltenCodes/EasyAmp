/* round trips and real-world inputs for the EQ interchange formats */
#include "../src/app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    static char buf[8192];
    ea_model a, b;
    int i;
    /* an AutoEQ-style APO file as found in the wild: comments, an OFF filter, shelves, CRLF */
    const char *apo = "# Sennheiser HD 600\r\nPreamp: -6.2 dB\r\nFilter 1: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70\r\n"
                      "Filter 2: ON PK Fc 2100 Hz Gain -2.4 dB Q 1.80\r\nFilter 3: OFF PK Fc 3000 Hz Gain 9 dB Q 1\r\n"
                      "Filter 4: ON HSC Fc 10000 Hz Gain -3.0 dB Q 0.70\r\n";
    ea_model_init(&a);
    CHECK(ea_eq_import(&a, apo), "APO import");
    CHECK(fabs(a.preamp + 6.2f) < 0.01f, "preamp %.2f", a.preamp);
    CHECK(a.nbands == EA_MIN_BANDS, "3 filters pad to the bank minimum, got %d", a.nbands);
    for (i = 0; i < a.nbands; i++) {
        if (fabs(a.freqs[i] - 105) < 1) CHECK(a.types[i] == EA_LOW_SHELF && fabs(a.gains[i] - 5.5f) < 0.01f && fabs(a.q[i] - 0.7f) < 0.01f, "low shelf: t=%d g=%.1f q=%.2f", a.types[i], a.gains[i], a.q[i]);
        if (fabs(a.freqs[i] - 2100) < 1) CHECK(a.types[i] == EA_PEAK && fabs(a.gains[i] + 2.4f) < 0.01f && fabs(a.q[i] - 1.8f) < 0.01f, "peak: g=%.1f q=%.2f", a.gains[i], a.q[i]);
        if (fabs(a.freqs[i] - 10000) < 1) CHECK(a.types[i] == EA_HIGH_SHELF, "high shelf type %d", a.types[i]);
        CHECK(fabs(a.freqs[i] - 3000) > 1 || a.gains[i] == 0, "the OFF filter must not be imported as +9 dB");
        if (i) CHECK(a.freqs[i] > a.freqs[i - 1], "bands sorted by frequency");
    }
    /* export -> import must reproduce the bank exactly */
    ea_eq_export_apo(&a, buf, sizeof buf);
    ea_model_init(&b);
    CHECK(ea_eq_import(&b, buf), "re-import of our own APO export");
    CHECK(b.nbands == a.nbands && fabs(b.preamp - a.preamp) < 0.06f, "round trip: %d bands, preamp %.1f", b.nbands, b.preamp);
    for (i = 0; i < a.nbands; i++) CHECK(fabs(a.freqs[i] - b.freqs[i]) < 1 && fabs(a.gains[i] - b.gains[i]) < 0.06f && a.types[i] == b.types[i], "band %d differs after round trip", i);
    /* GraphicEQ: a long point list is thinned to the bank's maximum, ends kept */
    { int o = sprintf(buf, "GraphicEQ: "); for (i = 0; i < 127; i++) o += sprintf(buf + o, "%s%.0f %.1f", i ? "; " : "", 20.0 * pow(1000.0, i / 126.0), i == 0 ? -7.0 : i == 126 ? 4.0 : 0.5); }
    ea_model_init(&b);
    CHECK(ea_eq_import(&b, buf), "GraphicEQ import");
    CHECK(b.nbands == EA_MAX_BANDS, "127 points -> %d bands", b.nbands);
    CHECK(fabs(b.gains[0] + 7.0f) < 0.01f && fabs(b.gains[b.nbands - 1] - 4.0f) < 0.01f, "endpoints kept: %.1f .. %.1f", b.gains[0], b.gains[b.nbands - 1]);
    ea_eq_export_geq(&a, buf, sizeof buf);
    CHECK(!strncmp(buf, "GraphicEQ: ", 11) && strstr(buf, "2100 -2.4"), "GraphicEQ export: %.60s", buf);
    ea_model_init(&b);
    CHECK(!ea_eq_import(&b, "this is not an equalizer file\n"), "garbage must be refused");
    CHECK(b.nbands == EA_GRAPHIC_N, "a refused import must leave the bank alone");
    printf(fails ? "%d FAILED\n" : "all eqio checks passed\n", fails);
    return fails != 0;
}
