/* decoders - FLAC (dr_flac) and Ogg Vorbis (stb_vorbis) for local files.
 * Both hand back interleaved stereo float, whatever the file holds.
 * Streams never come through here: servers are asked for MP3. */
#ifndef EA_DECODERS_H
#define EA_DECODERS_H

typedef struct ea_pcmdec ea_pcmdec;

/* NULL when the file is neither FLAC nor Ogg Vorbis (or is damaged) */
ea_pcmdec *pcmdec_open(const char *path, int *rate, int *channels, int *dur_ms, int *kbps);
int  pcmdec_read(ea_pcmdec *d, float *stereo, int max_frames);     /* frames written, 0 at the end */
int  pcmdec_seek(ea_pcmdec *d, float fraction);
void pcmdec_close(ea_pcmdec *d);

#endif
