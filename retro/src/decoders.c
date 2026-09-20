#include "decoders.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dr_flac's own file layer calls _ftelli64, which Windows 98's msvcrt does not
 * export - linking it would stop the whole program loading there. It reads
 * through the callbacks below instead. No SIMD: the floor is a Pentium II. */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_WCHAR
#include "../third_party/dr_flac.h"

#define STB_VORBIS_NO_PUSHDATA_API
#include "../third_party/stb_vorbis.c"

struct ea_pcmdec {
    FILE *f;                 /* owned for FLAC; stb_vorbis owns its own */
    drflac *flac;
    stb_vorbis *ogg;
    unsigned long long total;
    int channels;
    float tmp[1152 * 8];
};

static size_t cb_read(void *u, void *out, size_t n) { return fread(out, 1, n, (FILE *)u); }

static drflac_bool32 cb_seek(void *u, int offset, drflac_seek_origin origin)
{
    return fseek((FILE *)u, offset, origin == DRFLAC_SEEK_SET ? SEEK_SET : origin == DRFLAC_SEEK_END ? SEEK_END : SEEK_CUR) == 0;
}

static drflac_bool32 cb_tell(void *u, drflac_int64 *cursor)
{
    long p = ftell((FILE *)u);
    if (p < 0) return DRFLAC_FALSE;
    *cursor = p;
    return DRFLAC_TRUE;
}

ea_pcmdec *pcmdec_open(const char *path, int *rate, int *channels, int *dur_ms, int *kbps)
{
    ea_pcmdec *d = (ea_pcmdec *)calloc(1, sizeof *d);
    unsigned char magic[4] = { 0 };
    long size;
    FILE *f;
    if (!d) return 0;
    f = fopen(path, "rb");
    if (!f) { free(d); return 0; }
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (fread(magic, 1, 4, f) != 4) { fclose(f); free(d); return 0; }
    fseek(f, 0, SEEK_SET);
    if (!memcmp(magic, "OggS", 4)) {
        int err = 0;
        stb_vorbis_info info;
        d->ogg = stb_vorbis_open_file(f, 1, &err, 0);          /* closes f itself */
        if (!d->ogg) { fclose(f); free(d); return 0; }
        info = stb_vorbis_get_info(d->ogg);
        *rate = (int)info.sample_rate; d->channels = info.channels;
        d->total = stb_vorbis_stream_length_in_samples(d->ogg);
    } else {                                                   /* FLAC, possibly behind an ID3v2 tag - dr_flac copes */
        d->f = f;
        d->flac = drflac_open(cb_read, cb_seek, cb_tell, f, 0);
        if (!d->flac) { fclose(f); free(d); return 0; }
        *rate = (int)d->flac->sampleRate; d->channels = d->flac->channels;
        d->total = d->flac->totalPCMFrameCount;
    }
    *channels = d->channels > 1 ? 2 : 1;
    *dur_ms = *rate > 0 ? (int)(d->total * 1000ull / (unsigned)*rate) : 0;
    *kbps = *dur_ms > 0 ? (int)((double)size * 8.0 / *dur_ms) : 0;
    return d;
}

int pcmdec_read(ea_pcmdec *d, float *out, int max_frames)
{
    int n, i, ch = d->channels;
    if (d->ogg) return stb_vorbis_get_samples_float_interleaved(d->ogg, 2, out, max_frames * 2);   /* it maps any layout to stereo */
    if (ch > 8) return 0;
    if (max_frames > 1152) max_frames = 1152;
    if (ch == 2) return (int)drflac_read_pcm_frames_f32(d->flac, (drflac_uint64)max_frames, out);
    n = (int)drflac_read_pcm_frames_f32(d->flac, (drflac_uint64)max_frames, d->tmp);
    for (i = 0; i < n; i++) {                                   /* mono doubles up; surround keeps front left / right */
        out[2 * i] = d->tmp[i * ch];
        out[2 * i + 1] = d->tmp[i * ch + (ch > 1 ? 1 : 0)];
    }
    return n;
}

int pcmdec_seek(ea_pcmdec *d, float fraction)
{
    unsigned long long at = (unsigned long long)((double)d->total * (fraction < 0 ? 0 : fraction > 1 ? 1 : fraction));
    if (d->ogg) return stb_vorbis_seek(d->ogg, (unsigned int)at);
    return (int)drflac_seek_to_pcm_frame(d->flac, at);
}

void pcmdec_close(ea_pcmdec *d)
{
    if (!d) return;
    if (d->ogg) stb_vorbis_close(d->ogg);
    if (d->flac) drflac_close(d->flac);
    if (d->f) fclose(d->f);
    free(d);
}
