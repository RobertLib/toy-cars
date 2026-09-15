#ifndef TOYCARS_AUDIO_H
#define TOYCARS_AUDIO_H
#include "game.h"
typedef struct {SDL_AudioStream *stream;double phase,engine_phase,music_time;float effect_time;int effect;uint32_t noise;bool paused;} Audio;
void audio_init(Audio *a);
void audio_update(Audio *a,Game *g);
void audio_pause(Audio *a,bool pause);
void audio_destroy(Audio *a);
#endif
