#ifndef TOYCARS_AUDIO_H
#define TOYCARS_AUDIO_H
#include "game.h"
#include "music.h"
typedef struct {
    SDL_AudioStream *stream;
    Music music;
    double engine_phase;
    float effect_time, music_duck;
    int effect;
    uint32_t noise;
    bool paused;
} Audio;
void audio_init(Audio *a);
void audio_update(Audio *a, Game *g);
void audio_pause(Audio *a, bool pause);
void audio_destroy(Audio *a);
#endif
