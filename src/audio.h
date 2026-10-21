#ifndef TOYCARS_AUDIO_H
#define TOYCARS_AUDIO_H
#include "game.h"
#include "music.h"
#include "sfx.h"

/* Where the race is heard from: the camera, and the helicopter it may film. */
typedef struct {
    Vec3 eye, target, helicopter;
    bool helicopter_visible;
} Listener;

/* One car's engine: a four-speed gearbox drives the recorded engine layers. */
typedef struct {
    float rpm, load, shift, detune, throttle;
    int gear, pops;
    bool running, wet;
    uint32_t layers[4], skid, surface, wade;
    float quiet;    // Seconds before this car may sound another impact
    float pop_time; // Seconds until the next exhaust pop
} CarAudio;

typedef struct {
    SDL_AudioStream *stream;
    Music music;
    Sfx sfx;
    CarAudio cars[CAR_COUNT];
    uint32_t wind, ambience, weather, helicopter, crossing, train;
    Vec3 eye, eye_velocity;
    float limiter, race, quiet[SOUND_KIND_COUNT], time;
    uint32_t noise;
    bool paused, eye_known;
    // Menu state from the previous frame; a change sounds a click.
    Screen screen;
    int selected, mode, color, difficulty, initials_cursor, record_page;
    bool night, settings[4], record_history;
    char initials[4];
} Audio;
/* Loads the sounds and opens the device; open_device false prepares an
   offline mixer for tests and previews. */
void audio_init(Audio *a, bool open_device);
/* Directs the mix from the frame's game state and queues the next audio. */
void audio_update(Audio *a, Game *g, const Listener *listener, float dt);
void audio_pause(Audio *a, bool pause);
void audio_destroy(Audio *a);
/* The two halves of audio_update, usable without a device: direct consumes
   the frame's sound cues, render writes interleaved stereo. */
void audio_direct(Audio *a, Game *g, const Listener *listener, float dt);
void audio_render(Audio *a, float *stereo, int frames);
#endif
