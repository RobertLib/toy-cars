#include "audio.h"
#include <string.h>

void audio_init(Audio *a) {
    memset(a, 0, sizeof *a);
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
void audio_update(Audio *a, Game *g) {
    if (!a->stream || a->paused) {
        g->sound_event = 0;
        return;
    }
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
    const int chords[4][3] = {{48, 55, 60}, {45, 52, 57}, {41, 48, 53}, {43, 50, 55}};
    const int melody[] = {72, 0, 76, 79, 0, 76, 74, 0, 72, 0, 67, 0, 69, 72, 0, 0};
    for (int i = 0; i < frames; i++) {
        double time = a->music_time;
        float out = 0, right = 0;
        a->noise = a->noise * 1664525u + 1013904223u;
        float noise = ((a->noise >> 9) / 8388607.f) * 2 - 1;
        if (g->profile.music) {
            float beat = (float)(time * 106 / 60);
            int bar = (int)(beat / 4) % 4;
            float pulse = beat - floorf(beat);
            float pad = 0;
            for (int n = 0; n < 3; n++)
                pad += sinf((float)(time * midi(chords[bar][n]) * 2 * PI)) * .017f;
            float kick = sinf(pulse * (76 - pulse * 34) * 2 * PI) * expf(-pulse * 15) * .070f;
            float hat = noise * expf(-fmodf(beat * 2, 1) * 38) * .015f;
            int note = melody[(int)(beat * 2) % 16];
            float pluck = note ? sinf((float)(time * midi(note) * 2 * PI)) *
                                     expf(-fmodf(beat * 2, 1) * 5) * .036f
                               : 0;
            out += pad + kick + hat + pluck;
            right = pad + kick - hat + pluck * .9f;
        }
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
            }
            out += effect;
            right += effect;
        }
        data[i * 2] = clampf(out, -.8f, .8f);
        data[i * 2 + 1] = clampf(right, -.8f, .8f);
        a->music_time += 1. / 48000.;
        a->effect_time += 1.f / 48000;
    }
    SDL_PutAudioStreamData(a->stream, data, frames * 2 * (int)sizeof(float));
}
void audio_destroy(Audio *a) {
    SDL_DestroyAudioStream(a->stream);
    a->stream = NULL;
}
