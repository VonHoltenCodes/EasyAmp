/* dsp - the signal path and the analysis behind the meters. Portable C,
 * float math only (the floor is a Pentium II: x87, no SSE).
 *
 *   in-gain -> N parametric bands -> tone (BASS / LOUD shelves)
 *           -> balance -> preamp + out-gain -> soft limit
 */
#ifndef EA_DSP_H
#define EA_DSP_H

#include "app.h"

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1[2], z2[2];                     /* per-channel state */
    int active;                             /* 0 = unity, skipped entirely */
} ea_biquad;

typedef struct {
    int rate, eq_on;
    ea_biquad band[EA_MAX_BANDS], low, high;
    int nbands;
    float gain_l, gain_r;                   /* everything linear folded together */
} ea_chain;

void chain_init(ea_chain *c, int rate);
void chain_config(ea_chain *c, const ea_model *m);      /* new coefficients, state kept */
void chain_process(ea_chain *c, float *stereo, int frames);

#define EA_FFT 2048

typedef struct {
    int rate, bin_lo[EA_VIZ_BANDS], bin_hi[EA_VIZ_BANDS];
    float window[EA_FFT], cosv[EA_FFT / 2], sinv[EA_FFT / 2];
    int rev[EA_FFT];
    float smooth[EA_VIZ_BANDS], vu_l, vu_r;
} ea_analyzer;

void ana_init(ea_analyzer *a, int rate);
/* analyse EA_FFT stereo frames; writes levels / vu / wave into the model */
void ana_run(ea_analyzer *a, const short *stereo, ea_model *m);
void ana_decay(ea_analyzer *a, ea_model *m);            /* meters fall when idle */

#endif
