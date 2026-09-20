/* engine - playback. One worker thread owns the decoder and the waveOut
 * device; the UI thread only posts requests and polls for state. */
#ifndef EA_ENGINE_H
#define EA_ENGINE_H

#include "app.h"

enum { ENG_EV_NONE = 0, ENG_EV_ENDED = 1, ENG_EV_ERROR = 2 };

typedef struct ea_engine ea_engine;

ea_engine *eng_create(void);
void eng_destroy(ea_engine *e);
/* stops, opens, starts playing. path is a file or an http:// URL; dur_hint_ms
 * is used when the stream cannot tell its own length (a live transcode). */
void eng_open(ea_engine *e, const char *path, int dur_hint_ms);
void eng_pause(ea_engine *e, int paused);
void eng_stop(ea_engine *e);
void eng_seek(ea_engine *e, float fraction);
void eng_set_dsp(ea_engine *e, const ea_model *m);
/* fills time / format / meter fields of the model; returns an ENG_EV_* once */
int  eng_poll(ea_engine *e, ea_model *m);

#endif
