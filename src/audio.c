#include "audio.h"
#include <string.h>

void audio_init(Audio *a) {
    memset(a, 0, sizeof *a);
    music_init(&a->music);
    a->music_duck = 1;
    a->noise = 84131;
    SDL_AudioSpec spec = {.format = SDL_AUDIO_F32, .channels = 2, .freq = 48000};
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (a->stream)
        SDL_ResumeAudioStreamDevice(a->stream);
    else
        SDL_Log("Audio unavailable: %s", SDL_GetError());
}
void audio_pause(Audio *a, bool pause) {
    if (!a->stream)
        return;
    a->paused = pause;
    SDL_ClearAudioStream(a->stream);
    if (pause)
        SDL_PauseAudioStreamDevice(a->stream);
    else
        SDL_ResumeAudioStreamDevice(a->stream);
}
static float midi(int n) {
    return 440.f * powf(2, (n - 69) / 12.f);
}
static MusicContext soundtrack_context(const Game *g) {
    MusicScene scene = MUSIC_MENU;
    switch (g->screen) {
    case SCREEN_COUNTDOWN:
        scene = MUSIC_COUNTDOWN;
        break;
    case SCREEN_RACE:
        scene = MUSIC_RACE;
        break;
    case SCREEN_PAUSE:
        scene = MUSIC_PAUSE;
        break;
    case SCREEN_RESULTS:
        scene = MUSIC_RESULTS;
        break;
    default:
        break;
    }
    float intensity = 0;
    if (scene == MUSIC_RACE) {
        const Car *player = &g->cars[0];
        intensity = .18f + .48f * clampf(player->speed / 28.f, 0, 1);
        if (player->lap == RACE_LAPS)
            intensity += .18f;
        for (int i = 1; i < CAR_COUNT; i++) {
            if (!g->cars[i].finished && !g->cars[i].dnf &&
                fabsf(g->cars[i].progress - player->progress) < 9.f) {
                intensity += .16f;
                break;
            }
        }
        if (player->fuel <= 0)
            intensity = .1f;
    }
    return (MusicContext){scene, g->selected, intensity, g->profile.music};
}
void audio_update(Audio *a, Game *g) {
    if (!a->stream || a->paused) {
        g->sound_event = 0;
        return;
    }
    music_context(&a->music, soundtrack_context(g));
    if (g->sound_event) {
        a->effect = g->sound_event;
        a->effect_time = 0;
        g->sound_event = 0;
    }
    int queued = SDL_GetAudioStreamQueued(a->stream);
    int frames = 2400 - queued / (int)(sizeof(float) * 2);
    if (frames <= 0)
        return;
    if (frames > 2400)
        frames = 2400;
    float data[4800];
    bool racing = g->screen == SCREEN_RACE;
    float speed = g->cars[0].speed;
    for (int i = 0; i < frames; i++) {
        float out = 0, right = 0;
        a->noise = a->noise * 1664525u + 1013904223u;
        float noise = ((a->noise >> 9) / 8388607.f) * 2 - 1;
        music_sample(&a->music, &out, &right);
        float duck = g->profile.sound && a->effect && a->effect_time < .28f ? .68f : 1;
        a->music_duck += (duck - a->music_duck) * .0004f;
        out *= a->music_duck;
        right *= a->music_duck;
        if (g->profile.sound) {
            if (racing && g->cars[0].fuel > 0) {
                float hz = 44 + speed * 5.4f;
                a->engine_phase += hz / 48000.;
                if (a->engine_phase >= 1)
                    a->engine_phase -= 1;
                float engine = (sinf((float)a->engine_phase * 2 * PI) +
                                .28f * sinf((float)a->engine_phase * 6 * PI)) *
                               (.027f + speed * .00065f);
                out += engine;
                right += engine;
            }
            float t = a->effect_time, effect = 0;
            switch (a->effect) {
            case 1:
                if (t < .15f)
                    effect = sinf(t * 660 * 2 * PI) * .13f * (1 - t / .15f);
                break;
            case 2:
                if (t < .33f)
                    effect = sinf(t *
                                  (t < .1f   ? 880
                                   : t < .2f ? 1108
                                             : 1320) *
                                  2 * PI) *
                             expf(-t * 7) * .16f;
                break;
            case 3:
                if (t < .4f)
                    effect = sinf(t * 1320 * 2 * PI) * .12f * expf(-t * 6);
                break;
            case 4:
                if (t < .35f)
                    effect = noise * .036f * (1 - t / .35f);
                break;
            case 5:
                if (t < .16f)
                    effect = (noise * .10f + sinf(t * 70 * 2 * PI) * .17f) * expf(-t * 23);
                break;
            case 6:
                if (t < .8f)
                    effect = sinf(t * 220 * 2 * PI) * .10f * expf(-t * 4);
                break;
            case 7:
                if (t < 1.5f) {
                    int notes[] = {72, 76, 79, 84, 79, 84};
                    int n = (int)(t * 4);
                    effect = sinf(t * midi(notes[n < 6 ? n : 5]) * 2 * PI) *
                             expf(-fmodf(t, .25f) * 10) * .095f;
                }
                break;
            case 8:
                if (t < .055f)
                    effect = sinf(t * 740 * 2 * PI) * expf(-t * 65) * .11f;
                break;
            case 9:
                if (t < 3.2f) {
                    const int notes[] = {60, 64, 67, 72, 67, 72, 76, 79};
                    int n = (int)(t / .22f);
                    float local = n < 7 ? fmodf(t, .22f) : t - 7 * .22f;
                    float hz = midi(notes[n < 7 ? n : 7]);
                    float envelope = fminf(local * 80, 1) * expf(-local * 3);
                    effect = (sinf(t * hz * 2 * PI) + .35f * sinf(t * hz * PI)) *
                             envelope * .085f;
                    if (n >= 7)
                        effect += (sinf(t * midi(72) * 2 * PI) +
                                   sinf(t * midi(76) * 2 * PI)) * envelope * .035f;
                }
                break;
            }
            out += effect;
            right += effect;
        }
        data[i * 2] = clampf(out, -.8f, .8f);
        data[i * 2 + 1] = clampf(right, -.8f, .8f);
        a->effect_time += 1.f / 48000;
    }
    SDL_PutAudioStreamData(a->stream, data, frames * 2 * (int)sizeof(float));
}
void audio_destroy(Audio *a) {
    SDL_DestroyAudioStream(a->stream);
    a->stream = NULL;
}
