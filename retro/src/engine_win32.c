#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "dsp.h"
#include "net.h"
#include "decoders.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD                 /* the floor is a Pentium II: no SSE */
#include "../third_party/minimp3.h"

#define NBUF        8
#define BUF_FRAMES  2048                /* ~46 ms at 44.1 kHz; 8 of them queued */
#define RING_FRAMES 65536               /* ~1.5 s of what was sent to the card */
#define INBUF       (32 * 1024)

enum { REQ_NONE, REQ_OPEN, REQ_STOP, REQ_QUIT };

/* where the bytes come from: a local file, or a plain-HTTP stream (a Plex
 * server on the LAN). A seek on a stream is a new request with a Range. */
typedef struct {
    FILE *f;
    ea_stream *hs;
    char url[1024];
    long pos, size;
} reader;

static void rd_close(reader *r)
{
    if (r->f) fclose(r->f);
    if (r->hs) net_stream_close(r->hs);
    r->f = 0; r->hs = 0;
}

static int rd_open(reader *r, const char *path)
{
    char err[128];
    memset(r, 0, sizeof *r);
    if (!strncmp(path, "http://", 7) || !strncmp(path, "https://", 8)) {
        strncpy(r->url, path, sizeof r->url - 1);
        r->hs = net_stream_open(path, 0, &r->size, err, (int)sizeof err);
        return r->hs != 0;
    }
    r->f = fopen(path, "rb");
    if (!r->f) return 0;
    fseek(r->f, 0, SEEK_END); r->size = ftell(r->f); fseek(r->f, 0, SEEK_SET);
    return 1;
}

static int rd_read(reader *r, void *buf, int n)
{
    int got = 0;
    if (r->f) got = (int)fread(buf, 1, (size_t)n, r->f);
    else if (r->hs)
        while (got < n) {                                   /* a stream hands back short reads */
            int k = net_stream_read(r->hs, (char *)buf + got, n - got);
            if (k <= 0) break;
            got += k;
        }
    r->pos += got;
    return got;
}

static int rd_seek(reader *r, long pos)
{
    char err[128];
    if (r->f) { r->pos = pos; return fseek(r->f, pos, SEEK_SET) == 0; }
    if (pos == r->pos && r->hs) return 1;
    /* a short hop forward on a stream: read and drop the bytes. A new request
     * would mean a new connection - and, for a transcode, a new transcoder
     * session on the server - just to step over an ID3 tag. */
    if (r->hs && pos > r->pos && pos - r->pos <= 512 * 1024) {
        char skip[4096];
        while (r->pos < pos) {
            long want = pos - r->pos;
            if (rd_read(r, skip, want > (long)sizeof skip ? (int)sizeof skip : (int)want) <= 0) return 0;
        }
        return 1;
    }
    if (r->hs) net_stream_close(r->hs);
    r->hs = net_stream_open(r->url, pos, 0, err, (int)sizeof err);
    r->pos = pos;
    return r->hs != 0;
}

static int rd_is_open(const reader *r) { return r->f || r->hs; }
enum { SRC_NONE, SRC_MP3, SRC_WAV, SRC_PCM };     /* SRC_PCM: FLAC / Ogg Vorbis via decoders.c */

struct ea_engine {
    HANDLE thread, wake;
    CRITICAL_SECTION lock;
    /* requests (UI -> worker), under lock */
    int req, req_pause, have_seek, dsp_dirty;
    float seek_frac;
    char req_path[1024];
    int req_dur_hint;
    ea_model dsp_model;
    /* status (worker -> UI), under lock */
    int state, event, kbps, rate, channels, dur_ms, base_ms;
    float pitch;
    DWORD out_written;                  /* frames handed to waveOut since the last reset */
    short ring[RING_FRAMES * 2];
    /* worker-only */
    HWAVEOUT wo;
    WAVEHDR hdr[NBUF];
    short *pcm[NBUF];
    int wo_rate, paused;
    reader rd;
    ea_pcmdec *dec;
    int src, eof;
    long data_start, data_len;
    mp3dec_t mp3;
    unsigned char in[INBUF];
    int in_len, in_pos;
    float stage[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];  /* decoded, not yet consumed */
    int stage_len, stage_pos;
    int wav_bytes_per_frame;
    double rs_pos;                      /* varispeed read position in `stage` */
    float rs_last[2];
    ea_chain chain;
    /* UI-thread analysis */
    ea_analyzer ana;
    int ana_rate;
};

/* ---- source: MP3 (minimp3) and 16-bit PCM WAV --------------------------------- */

static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static void src_close(ea_engine *e)
{
    rd_close(&e->rd);
    if (e->dec) { pcmdec_close(e->dec); e->dec = 0; }
    e->src = SRC_NONE;
}

static int refill(ea_engine *e)
{
    int keep = e->in_len - e->in_pos, got;
    if (keep > 0 && e->in_pos > 0) memmove(e->in, e->in + e->in_pos, (size_t)keep);
    e->in_pos = 0; e->in_len = keep > 0 ? keep : 0;
    got = rd_read(&e->rd, e->in + e->in_len, INBUF - e->in_len);
    e->in_len += got;
    return got;
}

/* decode one more MP3 frame into `stage`; 0 at end of stream */
static int mp3_more(ea_engine *e)
{
    mp3dec_frame_info_t info;
    short tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];
    for (;;) {
        int n, i;
        if (e->in_len - e->in_pos < 4096) refill(e);
        if (e->in_len - e->in_pos <= 0) return 0;
        n = mp3dec_decode_frame(&e->mp3, e->in + e->in_pos, e->in_len - e->in_pos, tmp, &info);
        if (info.frame_bytes == 0) { if (!refill(e)) return 0; continue; }
        e->in_pos += info.frame_bytes;
        if (n <= 0) continue;                           /* skipped junk / tag */
        for (i = 0; i < n; i++) {
            float l = tmp[i * info.channels] / 32768.0f;
            float r = info.channels > 1 ? tmp[i * info.channels + 1] / 32768.0f : l;
            e->stage[2 * i] = l; e->stage[2 * i + 1] = r;
        }
        e->stage_len = n; e->stage_pos = 0;
        EnterCriticalSection(&e->lock);
        e->kbps = info.bitrate_kbps;
        LeaveCriticalSection(&e->lock);
        return n;
    }
}

static int wav_more(ea_engine *e)
{
    unsigned char raw[1152 * 4];
    int want = 1152 * e->wav_bytes_per_frame, got, n, i;
    long left = e->data_start + e->data_len - e->rd.pos;
    if (left <= 0) return 0;
    if (want > left) want = (int)left;
    got = rd_read(&e->rd, raw, want);
    n = got / e->wav_bytes_per_frame;
    for (i = 0; i < n; i++) {
        const unsigned char *p = raw + i * e->wav_bytes_per_frame;
        float l = (short)(p[0] | (p[1] << 8)) / 32768.0f;
        float r = e->channels > 1 ? (short)(p[2] | (p[3] << 8)) / 32768.0f : l;
        e->stage[2 * i] = l; e->stage[2 * i + 1] = r;
    }
    e->stage_len = n; e->stage_pos = 0;
    return n;
}

static int pcm_more(ea_engine *e)
{
    int n = pcmdec_read(e->dec, e->stage, 1152);
    e->stage_len = n > 0 ? n : 0; e->stage_pos = 0;
    return e->stage_len;
}

static int src_more(ea_engine *e) { return e->src == SRC_MP3 ? mp3_more(e) : e->src == SRC_WAV ? wav_more(e) : e->src == SRC_PCM ? pcm_more(e) : 0; }

static int open_wav(ea_engine *e, const unsigned char *h, long size)
{
    long pos = 12;
    int fmt_ok = 0, bits = 0;
    (void)h;
    while (pos + 8 <= size) {
        unsigned char ck[8];
        unsigned len;
        if (!rd_seek(&e->rd, pos) || rd_read(&e->rd, ck, 8) != 8) break;
        len = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((unsigned)ck[7] << 24);
        if (!memcmp(ck, "fmt ", 4)) {
            unsigned char f[16];
            if (rd_read(&e->rd, f, 16) != 16) return 0;
            if ((f[0] | (f[1] << 8)) != 1) return 0;               /* PCM only */
            e->channels = f[2] | (f[3] << 8);
            e->rate = f[4] | (f[5] << 8) | (f[6] << 16) | ((unsigned)f[7] << 24);
            bits = f[14] | (f[15] << 8);
            fmt_ok = bits == 16 && (e->channels == 1 || e->channels == 2);
        } else if (!memcmp(ck, "data", 4)) {
            if (!fmt_ok) return 0;
            e->data_start = pos + 8; e->data_len = (long)len;
            if (e->data_start + e->data_len > size) e->data_len = size - e->data_start;
            e->wav_bytes_per_frame = e->channels * 2;
            e->dur_ms = (int)((double)e->data_len / e->wav_bytes_per_frame * 1000.0 / e->rate);
            e->kbps = e->rate * e->channels * 16 / 1000;
            return rd_seek(&e->rd, e->data_start);
        }
        pos += 8 + len + (len & 1);
    }
    return 0;
}

static int src_open(ea_engine *e, const char *path)
{
    unsigned char h[16];
    long size;
    mp3dec_frame_info_t info;
    short tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int n = 0, tries = 0;
    src_close(e);
    e->eof = 0; e->stage_len = e->stage_pos = 0; e->in_len = e->in_pos = 0; e->rs_pos = 0;
    e->rs_last[0] = e->rs_last[1] = 0;
    {   /* FLAC and Ogg Vorbis: local files only, by extension */
        const char *dot = strrchr(path, '.');
        if (dot && strncmp(path, "http", 4) && (!lstrcmpiA(dot, ".flac") || !lstrcmpiA(dot, ".ogg") || !lstrcmpiA(dot, ".oga"))) {
            e->dec = pcmdec_open(path, &e->rate, &e->channels, &e->dur_ms, &e->kbps);
            if (!e->dec) return 0;
            e->data_start = 0; e->data_len = 1;               /* non-zero: this source can seek */
            e->src = SRC_PCM;
            return 1;
        }
    }
    if (!rd_open(&e->rd, path)) return 0;
    size = e->rd.size;
    if (rd_read(&e->rd, h, 12) != 12) { src_close(e); return 0; }
    e->eof = 0; e->stage_len = e->stage_pos = 0; e->in_len = e->in_pos = 0; e->rs_pos = 0;
    e->rs_last[0] = e->rs_last[1] = 0;
    if (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) {
        if (!open_wav(e, h, size)) { src_close(e); return 0; }
        e->src = SRC_WAV;
        return 1;
    }
    /* MP3: step over an ID3v2 tag (album art can be hundreds of KB of junk) */
    e->data_start = 0;
    if (!memcmp(h, "ID3", 3)) e->data_start = 10 + (((long)h[6] & 127) << 21 | ((long)h[7] & 127) << 14 | ((long)h[8] & 127) << 7 | ((long)h[9] & 127));
    e->data_len = size > e->data_start ? size - e->data_start : 0;
    if (size > 128 && e->rd.f) {                        /* a trailing ID3v1; not worth a request on a stream */
        unsigned char t[3];
        if (rd_seek(&e->rd, size - 128) && rd_read(&e->rd, t, 3) == 3 && !memcmp(t, "TAG", 3)) e->data_len -= 128;
    }
    if (!rd_seek(&e->rd, e->data_start)) { src_close(e); return 0; }
    mp3dec_init(&e->mp3);
    refill(e);
    /* first frame gives the format; a Xing/Info header gives an exact length */
    while (tries++ < 64 && e->in_len - e->in_pos > 0) {
        n = mp3dec_decode_frame(&e->mp3, e->in + e->in_pos, e->in_len - e->in_pos, tmp, &info);
        if (info.frame_bytes == 0) break;
        if (n > 0 || info.hz) {
            const unsigned char *fr = e->in + e->in_pos;
            int i;
            e->rate = info.hz; e->channels = info.channels; e->kbps = info.bitrate_kbps;
            e->dur_ms = info.bitrate_kbps > 0 && e->data_len > 0 ? (int)((double)e->data_len * 8.0 / info.bitrate_kbps) : 0;   /* a live transcode has no length */
            for (i = 4; i + 12 < info.frame_bytes && i < 48; i++)
                if (!memcmp(fr + i, "Xing", 4) || !memcmp(fr + i, "Info", 4)) {
                    if (fr[i + 7] & 1) {
                        unsigned frames = be32(fr + i + 8);
                        int spf = info.hz >= 32000 ? 1152 : 576;
                        if (frames) e->dur_ms = (int)((double)frames * spf * 1000.0 / info.hz);
                    }
                    break;
                }
            break;
        }
        e->in_pos += info.frame_bytes;
    }
    if (!e->rate) { src_close(e); return 0; }
    mp3dec_init(&e->mp3);
    if (e->rd.f) {                                       /* a file: start clean from the top */
        if (!rd_seek(&e->rd, e->data_start)) { src_close(e); return 0; }
        e->in_len = e->in_pos = 0;
    }                                                    /* a stream: keep what is buffered, the probed
                                                          * frame was not consumed and decodes again */
    e->src = SRC_MP3;
    return 1;
}

static void src_seek(ea_engine *e, float frac)
{
    long off = (long)((double)e->data_len * frac);
    if (e->src == SRC_PCM) { pcmdec_seek(e->dec, frac); e->stage_len = e->stage_pos = 0; e->eof = 0; e->rs_pos = 0; return; }
    if (e->src == SRC_WAV) off -= off % e->wav_bytes_per_frame;
    if (!rd_seek(&e->rd, e->data_start + off)) e->eof = 1;
    if (e->src == SRC_MP3) mp3dec_init(&e->mp3);         /* it resyncs on the next header */
    e->in_len = e->in_pos = 0; e->stage_len = e->stage_pos = 0; e->rs_pos = 0;
    if (rd_is_open(&e->rd)) e->eof = 0;
}

/* ---- output ---------------------------------------------------------------------- */

static void out_close(ea_engine *e)
{
    int i;
    if (!e->wo) return;
    waveOutReset(e->wo);
    for (i = 0; i < NBUF; i++) if (e->hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(e->wo, &e->hdr[i], sizeof(WAVEHDR));
    waveOutClose(e->wo);
    e->wo = 0;
}

static int out_open(ea_engine *e, int rate)
{
    WAVEFORMATEX wf;
    int i;
    if (e->wo && e->wo_rate == rate) { waveOutReset(e->wo); return 1; }
    out_close(e);
    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = (DWORD)rate; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4; wf.nAvgBytesPerSec = (DWORD)rate * 4;
    if (waveOutOpen(&e->wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { e->wo = 0; return 0; }
    for (i = 0; i < NBUF; i++) {
        memset(&e->hdr[i], 0, sizeof(WAVEHDR));
        e->hdr[i].lpData = (LPSTR)e->pcm[i];
        e->hdr[i].dwBufferLength = BUF_FRAMES * 4;
        waveOutPrepareHeader(e->wo, &e->hdr[i], sizeof(WAVEHDR));
        e->hdr[i].dwFlags |= WHDR_DONE;
    }
    e->wo_rate = rate;
    return 1;
}

/* fill one output block: varispeed read of the source -> DSP -> 16-bit */
static int render(ea_engine *e, short *out, float pitch)
{
    static float blk[BUF_FRAMES * 2];
    int n = 0, i;
    while (n < BUF_FRAMES) {
        int ip;
        float fr, l0, r0, l1, r1;
        if (e->stage_pos >= e->stage_len) { if (!src_more(e)) { e->eof = 1; break; } }
        /* linear-interpolated read at `pitch` source frames per output frame:
         * speed and pitch move together, like a cassette deck's varispeed */
        ip = (int)e->rs_pos;
        if (ip >= e->stage_len) { e->rs_pos -= e->stage_len; e->stage_pos = e->stage_len;
                                  if (e->stage_len) { e->rs_last[0] = e->stage[2 * (e->stage_len - 1)]; e->rs_last[1] = e->stage[2 * (e->stage_len - 1) + 1]; }
                                  continue; }
        fr = (float)(e->rs_pos - ip);
        l0 = e->stage[2 * ip]; r0 = e->stage[2 * ip + 1];
        if (ip + 1 < e->stage_len) { l1 = e->stage[2 * ip + 2]; r1 = e->stage[2 * ip + 3]; } else { l1 = l0; r1 = r0; }
        blk[2 * n] = l0 + (l1 - l0) * fr; blk[2 * n + 1] = r0 + (r1 - r0) * fr;
        n++;
        e->rs_pos += pitch;
    }
    if (n == 0) return 0;
    chain_process(&e->chain, blk, n);
    for (i = 0; i < n * 2; i++) {
        float v = blk[i] * 32767.0f;
        out[i] = (short)(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : v));
    }
    for (i = n * 2; i < BUF_FRAMES * 2; i++) out[i] = 0;
    return n;
}

static DWORD WINAPI worker(LPVOID arg)
{
    ea_engine *e = (ea_engine *)arg;
    float pitch = 1.0f;
    for (;;) {
        int req, want_pause, seek, i, queued = 0, wrote = 0;
        float frac;
        char path[1024];
        int dur_hint;
        EnterCriticalSection(&e->lock);
        req = e->req; e->req = REQ_NONE;
        want_pause = e->req_pause;
        seek = e->have_seek; e->have_seek = 0; frac = e->seek_frac;
        strcpy(path, e->req_path); dur_hint = e->req_dur_hint;
        if (e->dsp_dirty) {
            e->dsp_dirty = 0;
            chain_config(&e->chain, &e->dsp_model);
            pitch = e->dsp_model.pitch > 0.5f ? e->dsp_model.pitch : 1.0f;
            e->pitch = pitch;
        }
        LeaveCriticalSection(&e->lock);

        if (req == REQ_QUIT) break;
        if (req == REQ_STOP || req == REQ_OPEN) {
            if (e->wo) waveOutReset(e->wo);
            for (i = 0; i < NBUF; i++) e->hdr[i].dwFlags |= WHDR_DONE;
            src_close(e);
            EnterCriticalSection(&e->lock);
            e->state = EA_STOPPED; e->base_ms = 0; e->out_written = 0; e->dur_ms = 0; e->kbps = 0;
            LeaveCriticalSection(&e->lock);
            e->paused = 0;
        }
        if (req == REQ_OPEN) {
            int ok = src_open(e, path) && out_open(e, e->rate);
            EnterCriticalSection(&e->lock);
            if (ok && dur_hint > 0 && (e->rd.size <= 0 || e->dur_ms <= 0)) e->dur_ms = dur_hint;
            if (ok) {
                chain_init(&e->chain, e->rate); chain_config(&e->chain, &e->dsp_model);
                e->state = EA_PLAYING; e->req_pause = 0;
            } else { e->event = ENG_EV_ERROR; src_close(e); }
            LeaveCriticalSection(&e->lock);
            want_pause = 0;
        }
        if (e->src != SRC_NONE && seek && e->data_len > 0) {
            waveOutReset(e->wo);
            for (i = 0; i < NBUF; i++) e->hdr[i].dwFlags |= WHDR_DONE;
            src_seek(e, frac);
            EnterCriticalSection(&e->lock);
            e->base_ms = (int)(e->dur_ms * frac); e->out_written = 0;
            LeaveCriticalSection(&e->lock);
            if (e->paused) waveOutPause(e->wo);
        }
        if (e->src != SRC_NONE && want_pause != e->paused) {
            e->paused = want_pause;
            if (e->paused) waveOutPause(e->wo); else waveOutRestart(e->wo);
            EnterCriticalSection(&e->lock);
            e->state = e->paused ? EA_PAUSED : EA_PLAYING;
            LeaveCriticalSection(&e->lock);
        }

        if (e->src != SRC_NONE && !e->paused) {
            for (i = 0; i < NBUF; i++) {
                int n;
                if (!(e->hdr[i].dwFlags & WHDR_DONE)) { queued++; continue; }
                if (e->eof) continue;
                n = render(e, e->pcm[i], pitch);
                if (n <= 0) continue;
                EnterCriticalSection(&e->lock);
                {   /* remember what the card is about to play, for the meters */
                    DWORD at = e->out_written % RING_FRAMES;
                    int first = (int)(RING_FRAMES - at) < BUF_FRAMES ? (int)(RING_FRAMES - at) : BUF_FRAMES;
                    memcpy(e->ring + at * 2, e->pcm[i], (size_t)first * 4);
                    if (first < BUF_FRAMES) memcpy(e->ring, e->pcm[i] + first * 2, (size_t)(BUF_FRAMES - first) * 4);
                    e->out_written += BUF_FRAMES;
                }
                LeaveCriticalSection(&e->lock);
                e->hdr[i].dwFlags &= ~WHDR_DONE;
                e->hdr[i].dwBufferLength = BUF_FRAMES * 4;
                waveOutWrite(e->wo, &e->hdr[i], sizeof(WAVEHDR));
                queued++; wrote++;
            }
            if (e->eof && queued == 0) {                 /* the last block has finished playing */
                src_close(e);
                EnterCriticalSection(&e->lock);
                e->state = EA_STOPPED; e->event = ENG_EV_ENDED;
                LeaveCriticalSection(&e->lock);
            }
        }
        WaitForSingleObject(e->wake, wrote ? 5 : 15);
    }
    out_close(e);
    src_close(e);
    return 0;
}

/* ---- public API (UI thread) ------------------------------------------------------ */

ea_engine *eng_create(void)
{
    ea_engine *e = (ea_engine *)calloc(1, sizeof *e);
    DWORD tid;
    int i;
    if (!e) return 0;
    for (i = 0; i < NBUF; i++) if (!(e->pcm[i] = (short *)malloc(BUF_FRAMES * 4))) return 0;
    InitializeCriticalSection(&e->lock);
    e->wake = CreateEventA(0, FALSE, FALSE, 0);
    e->pitch = 1.0f;
    ea_model_init(&e->dsp_model);
    chain_init(&e->chain, 44100);
    e->thread = CreateThread(0, 0, worker, e, 0, &tid);
    if (e->thread) SetThreadPriority(e->thread, THREAD_PRIORITY_ABOVE_NORMAL);
    return e;
}

static void post(ea_engine *e, int req)
{
    EnterCriticalSection(&e->lock);
    e->req = req;
    LeaveCriticalSection(&e->lock);
    SetEvent(e->wake);
}

void eng_destroy(ea_engine *e)
{
    int i;
    if (!e) return;
    post(e, REQ_QUIT);
    WaitForSingleObject(e->thread, 3000);
    CloseHandle(e->thread); CloseHandle(e->wake);
    DeleteCriticalSection(&e->lock);
    for (i = 0; i < NBUF; i++) free(e->pcm[i]);
    free(e);
}

void eng_open(ea_engine *e, const char *path, int dur_hint_ms)
{
    EnterCriticalSection(&e->lock);
    strncpy(e->req_path, path, sizeof e->req_path - 1);
    e->req_dur_hint = dur_hint_ms;
    e->req = REQ_OPEN; e->req_pause = 0; e->have_seek = 0; e->event = ENG_EV_NONE;
    LeaveCriticalSection(&e->lock);
    SetEvent(e->wake);
}

void eng_stop(ea_engine *e) { post(e, REQ_STOP); }

void eng_pause(ea_engine *e, int paused)
{
    EnterCriticalSection(&e->lock);
    e->req_pause = paused;
    LeaveCriticalSection(&e->lock);
    SetEvent(e->wake);
}

void eng_seek(ea_engine *e, float fraction)
{
    EnterCriticalSection(&e->lock);
    e->seek_frac = fraction < 0 ? 0 : (fraction > 0.999f ? 0.999f : fraction);
    e->have_seek = 1;
    LeaveCriticalSection(&e->lock);
    SetEvent(e->wake);
}

void eng_set_dsp(ea_engine *e, const ea_model *m)
{
    EnterCriticalSection(&e->lock);
    e->dsp_model = *m;
    e->dsp_model.tracks = 0;
    e->dsp_dirty = 1;
    LeaveCriticalSection(&e->lock);
    SetEvent(e->wake);
}

int eng_poll(ea_engine *e, ea_model *m)
{
    static short win[EA_FFT * 2];
    MMTIME t;
    DWORD played = 0, written;
    int ev, state, have = 0, rate;
    float pitch;
    t.wType = TIME_SAMPLES;
    EnterCriticalSection(&e->lock);
    state = e->state; ev = e->event; e->event = ENG_EV_NONE;
    written = e->out_written; rate = e->rate; pitch = e->pitch;
    if (state != EA_STOPPED && e->wo && waveOutGetPosition(e->wo, &t, sizeof t) == MMSYSERR_NOERROR && t.wType == TIME_SAMPLES)
        played = t.u.sample;
    if (played > written) played = written;
    if (state == EA_PLAYING && played >= EA_FFT && written - played < RING_FRAMES - EA_FFT) {
        /* the EA_FFT frames ending at what the speakers are playing right now */
        DWORD start = (played - EA_FFT) % RING_FRAMES, first = RING_FRAMES - start < EA_FFT ? RING_FRAMES - start : EA_FFT;
        memcpy(win, e->ring + start * 2, (size_t)first * 4);
        if (first < EA_FFT) memcpy(win + first * 2, e->ring, (size_t)(EA_FFT - first) * 4);
        have = 1;
    }
    m->state = state;
    m->kbps = e->kbps; m->khz = rate / 1000; m->stereo = e->channels > 1 && state != EA_STOPPED;
    m->dur_ms = e->dur_ms;
    m->pos_ms = state == EA_STOPPED ? 0 : e->base_ms + (rate ? (int)((double)played * 1000.0 * pitch / rate) : 0);
    LeaveCriticalSection(&e->lock);
    if (m->dur_ms > 0 && m->pos_ms > m->dur_ms) m->pos_ms = m->dur_ms;
    if (rate && rate != e->ana_rate) { ana_init(&e->ana, rate); e->ana_rate = rate; }
    if (have && e->ana_rate) ana_run(&e->ana, win, m); else ana_decay(&e->ana, m);
    return ev;
}
