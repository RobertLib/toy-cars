#ifndef TOYCARS_MUSIC_H
#define TOYCARS_MUSIC_H

#include <stdbool.h>
#include <stdint.h>

#define MUSIC_RATE 48000
#define MUSIC_VOICES 64
#define MUSIC_DELAY 24000
#define MUSIC_SONGS 7

typedef enum { MUSIC_MENU, MUSIC_COUNTDOWN, MUSIC_RACE, MUSIC_PAUSE, MUSIC_RESULTS } MusicScene;
typedef struct {
    MusicScene scene;
    int track;
    float intensity;
    bool enabled;
} MusicContext;

typedef struct {
    float phase, increment, age, duration, attack, release, decay, envelope;
    float gain, left, right;
    int instrument, layer;
    bool active;
} MusicVoice;

/* Owned by the audio producer, with no allocation or shared state while rendering. */
typedef struct {
    MusicVoice voices[MUSIC_VOICES];
    float sine[2049], frequency[128];
    float delay[MUSIC_DELAY][2];
    float layers[5], volume, intensity, transition, noise_low, delay_low[2];
    double until_tick;
    uint32_t noise;
    unsigned tick, visits[3], variation;
    int song, pending_song, delay_pos;
    MusicContext context;
    MusicScene arranged_scene;
    bool initialized;
} Music;

void music_init(Music *m);
void music_context(Music *m, MusicContext context);
void music_sample(Music *m, float *left, float *right);
const char *music_song_name(int song);

#endif
