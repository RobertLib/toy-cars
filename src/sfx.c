#include "sfx.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

// An abruptly ended voice settles from its last sample over about 2 ms.
#define TAIL_DECAY .9887f

// Recorded sources and their credits are listed in assets/licenses/SOUNDS.txt.
static const SfxInfo sounds[SFX_COUNT] = {
    [SFX_ENGINE_IDLE] = {"sounds/engine-idle.ogg", -13.92f, true, false},
    [SFX_ENGINE_LOW] = {"sounds/engine-low.ogg", -13.63f, true, false},
    [SFX_ENGINE_MID] = {"sounds/engine-mid.ogg", -11.63f, true, false},
    [SFX_ENGINE_HIGH] = {"sounds/engine-high.ogg", -15.92f, true, false},
    [SFX_SKID] = {"sounds/skid.ogg", -12.54f, true, false},
    [SFX_DIRT] = {"sounds/dirt.ogg", -17.29f, true, false},
    [SFX_SAND] = {"sounds/sand.ogg", -17.56f, true, false},
    [SFX_SNOW] = {"sounds/snow.ogg", -21.14f, true, false},
    [SFX_WADE] = {"sounds/wade.ogg", -21.77f, true, false},
    [SFX_WIND] = {"sounds/wind.ogg", -16.02f, true, false},
    [SFX_HELICOPTER] = {"sounds/helicopter.ogg", -16.60f, true, false},
    [SFX_AMBIENCE_COUNTRY] = {"sounds/ambience-country.ogg", -17.82f, true, true},
    [SFX_AMBIENCE_BEACH] = {"sounds/ambience-beach.ogg", -18.00f, true, true},
    [SFX_AMBIENCE_WINTER] = {"sounds/ambience-winter.ogg", -23.71f, true, true},
    [SFX_AMBIENCE_NIGHT] = {"sounds/ambience-night.ogg", -10.37f, true, true},
    [SFX_RAIN] = {"sounds/rain.ogg", -23.65f, true, true},
    [SFX_GUSTS] = {"sounds/gusts.ogg", -17.42f, true, true},
    [SFX_BEEP] = {"sounds/beep.ogg", -11.48f, false, false},
    [SFX_GO] = {"sounds/go.ogg", -4.54f, false, false},
    [SFX_FUEL] = {"sounds/fuel.ogg", -19.25f, false, false},
    [SFX_BOOST] = {"sounds/boost.ogg", -22.28f, false, false},
    [SFX_TAKEOFF] = {"sounds/takeoff.ogg", -16.66f, false, false},
    [SFX_LANDING] = {"sounds/landing.ogg", -22.57f, false, false},
    [SFX_CAR_HIT] = {"sounds/car-hit.ogg", -11.05f, false, false},
    [SFX_CONE] = {"sounds/cone.ogg", -21.19f, false, false},
    [SFX_TIRE] = {"sounds/tire.ogg", -20.68f, false, false},
    [SFX_FENCE] = {"sounds/fence.ogg", -20.56f, false, false},
    [SFX_SPLASH] = {"sounds/splash.ogg", -18.83f, false, false},
    [SFX_SPUTTER] = {"sounds/sputter.ogg", -19.65f, false, false},
    [SFX_GEAR] = {"sounds/gear.ogg", -16.03f, false, false},
    [SFX_BACKFIRE] = {"sounds/backfire.ogg", -14.51f, false, false},
    [SFX_BELL] = {"sounds/bell.ogg", -22.56f, false, false},
    [SFX_CHEER] = {"sounds/cheer.ogg", -14.76f, false, false},
    [SFX_ROAR] = {"sounds/roar.ogg", -16.41f, false, false},
    [SFX_RECOVER] = {"sounds/recover.ogg", -15.97f, false, false},
    [SFX_CROSSING_BELL] = {"sounds/crossing-bell.ogg", -14.40f, true, false},
    [SFX_STEAM_WHISTLE] = {"sounds/steam-whistle.ogg", -12.18f, false, false},
    [SFX_TRAIN_HORN] = {"sounds/train-horn.ogg", -8.07f, false, false},
    [SFX_STEAM_TRAIN] = {"sounds/steam-train.ogg", -16.15f, true, false},
    [SFX_TRAIN] = {"sounds/train.ogg", -19.92f, true, false},
    [SFX_LAND_TRAIN] = {"sounds/land-train.ogg", -15.68f, true, false},
    [SFX_LAND_TRAIN_HORN] = {"sounds/land-train-horn.ogg", -11.05f, false, false},
    [SFX_UI_MOVE] = {"sounds/ui-move.ogg", -22.73f, false, false},
    [SFX_UI_SELECT] = {"sounds/ui-select.ogg", -23.29f, false, false},
};

const SfxInfo *sfx_info(int sound) {
    return sound >= 0 && sound < SFX_COUNT ? &sounds[sound] : NULL;
}

float sfx_cutoff(float hz) {
    return hz >= SFX_RATE * .5f ? 1 : 1 - expf(-2 * 3.14159265f * fmaxf(hz, 1) / SFX_RATE);
}

static float level(int sound) {
    return powf(10, (SFX_LOUDNESS - sounds[sound].loudness) / 20);
}

static bool decode(Sfx *s, int sound) {
    size_t size = 0;
    void *data = s->load ? s->load(sounds[sound].file, &size) : NULL;
    int channels = 0, rate = 0;
    short *pcm = NULL;
    int frames = data && size <= INT_MAX
                     ? stb_vorbis_decode_memory(data, (int)size, &channels, &rate, &pcm)
                     : -1;
    SDL_free(data);
    if (frames <= 0 || rate != SFX_RATE || channels < 1 || channels > 2) {
        free(pcm);
        SDL_Log("Sound unavailable: %s", sounds[sound].file);
        return false;
    }
    s->samples[sound] = (SfxSample){pcm, frames, channels};
    return true;
}

static void release(Sfx *s, SfxVoice *v) {
    // Hand the last emitted sample to the shared tail so the cut cannot click.
    s->tail[0] += v->last[0];
    s->tail[1] += v->last[1];
    if (v->vorbis)
        stb_vorbis_close(v->vorbis);
    SDL_free(v->data);
    SDL_free(v->block);
    *v = (SfxVoice){.sound = -1};
}

void sfx_init(Sfx *s, SfxLoader load) {
    memset(s, 0, sizeof *s);
    s->load = load;
    for (int i = 0; i < SFX_VOICES; i++)
        s->voices[i].sound = -1;
    for (int i = 0; i < SFX_COUNT; i++)
        if (!sounds[i].stream)
            decode(s, i);
    s->initialized = true;
}

void sfx_destroy(Sfx *s) {
    for (int i = 0; i < SFX_VOICES; i++)
        if (s->voices[i].sound >= 0)
            release(s, &s->voices[i]);
    for (int i = 0; i < SFX_COUNT; i++)
        free(s->samples[i].pcm);
    memset(s, 0, sizeof *s);
}

void sfx_stop_all(Sfx *s) {
    for (int i = 0; i < SFX_VOICES; i++)
        if (s->voices[i].sound >= 0)
            release(s, &s->voices[i]);
}

static SfxVoice *voice(const Sfx *s, uint32_t handle) {
    if (!handle)
        return NULL;
    SfxVoice *v = (SfxVoice *)&s->voices[(handle & 0xff) % SFX_VOICES];
    return v->sound >= 0 && v->serial == handle >> 8 ? v : NULL;
}

bool sfx_active(const Sfx *s, uint32_t handle) {
    return voice(s, handle) != NULL;
}

int sfx_voices(const Sfx *s) {
    int n = 0;
    for (int i = 0; i < SFX_VOICES; i++)
        n += s->voices[i].sound >= 0;
    return n;
}

static bool open_stream(Sfx *s, SfxVoice *v, int sound) {
    size_t size = 0;
    void *data = s->load ? s->load(sounds[sound].file, &size) : NULL;
    int error = 0;
    stb_vorbis *vorbis =
        data && size <= INT_MAX ? stb_vorbis_open_memory(data, (int)size, &error, NULL) : NULL;
    float *block = vorbis ? SDL_malloc(sizeof *block * SFX_BLOCK * 2) : NULL;
    if (vorbis && (stb_vorbis_get_info(vorbis).sample_rate != SFX_RATE ||
                   stb_vorbis_get_info(vorbis).channels != 2 || !block)) {
        stb_vorbis_close(vorbis);
        vorbis = NULL;
    }
    if (!vorbis) {
        SDL_Log("Sound unavailable: %s", sounds[sound].file);
        SDL_free(data);
        SDL_free(block);
        return false;
    }
    v->vorbis = vorbis;
    v->data = data;
    v->block = block;
    return true;
}

/* Takes a free voice, or the quietest one-shot when every voice is busy. */
static SfxVoice *claim(Sfx *s, int sound) {
    if (!s->initialized || sound < 0 || sound >= SFX_COUNT)
        return NULL;
    if (!sounds[sound].stream && !s->samples[sound].pcm)
        return NULL;
    SfxVoice *v = NULL;
    float quietest = INFINITY;
    for (int i = 0; i < SFX_VOICES; i++) {
        SfxVoice *c = &s->voices[i];
        if (c->sound < 0) {
            v = c;
            break;
        }
        if (!sounds[c->sound].loop && c->gain < quietest) {
            quietest = c->gain;
            v = c;
        }
    }
    if (!v)
        return NULL;
    if (v->sound >= 0)
        release(s, v);
    s->serial = (s->serial + 1) & 0xffffff;
    if (!s->serial)
        s->serial = 1;
    *v = (SfxVoice){.sound = sound,
                    .serial = s->serial,
                    .cutoff = 1,
                    .target_cutoff = 1,
                    .pitch = 1,
                    .target_pitch = 1};
    if (sounds[sound].stream && !open_stream(s, v, sound)) {
        *v = (SfxVoice){.sound = -1};
        return NULL;
    }
    return v;
}

static uint32_t handle_of(const Sfx *s, const SfxVoice *v) {
    return v->serial << 8 | (uint32_t)(v - s->voices);
}

uint32_t sfx_play(Sfx *s, int sound, float gain, float pitch, float pan) {
    if (gain <= 0)
        return 0;
    SfxVoice *v = claim(s, sound);
    if (!v)
        return 0;
    v->gain = v->target_gain = gain;
    v->pitch = v->target_pitch = fmaxf(pitch, .05f);
    v->pan = v->target_pan = fmaxf(-1, fminf(1, pan));
    return handle_of(s, v);
}

void sfx_hold(Sfx *s, uint32_t *handle, int sound, float gain, float pitch, float pan,
              float cutoff) {
    SfxVoice *v = voice(s, *handle);
    if (v && v->sound != sound) {
        v->target_gain = 0;
        v = NULL;
    }
    if (!v) {
        *handle = 0;
        if (gain <= 0)
            return;
        v = claim(s, sound);
        if (!v)
            return;
        // Loops of the same sound start at different points so they never phase.
        if (!sounds[sound].stream && sounds[sound].loop)
            v->position = (double)(s->serial * 2654435761u % (uint32_t)s->samples[sound].frames);
        v->pitch = fmaxf(pitch, .05f);
        v->pan = fmaxf(-1, fminf(1, pan));
        v->cutoff = fmaxf(.001f, fminf(1, cutoff));
        *handle = handle_of(s, v);
    }
    v->target_gain = fmaxf(0, gain);
    v->target_pitch = fmaxf(pitch, .05f);
    v->target_pan = fmaxf(-1, fminf(1, pan));
    v->target_cutoff = fmaxf(.001f, fminf(1, cutoff));
}

static float at(const SfxSample *smp, int frame, int channel, bool loop) {
    if (frame < 0 || frame >= smp->frames) {
        if (!loop)
            return 0;
        frame = (frame % smp->frames + smp->frames) % smp->frames;
    }
    return smp->pcm[frame * smp->channels + (smp->channels > 1 ? channel : 0)] * (1.f / 32768);
}

// Four-point Hermite interpolation keeps pitched loops free of zipper noise.
static float hermite(const SfxSample *smp, double position, int channel, bool loop) {
    int i = (int)floor(position);
    float t = (float)(position - i);
    float a = at(smp, i - 1, channel, loop), b = at(smp, i, channel, loop);
    float c = at(smp, i + 1, channel, loop), d = at(smp, i + 2, channel, loop);
    float c1 = .5f * (c - a), c2 = a - 2.5f * b + 2 * c - .5f * d;
    float c3 = .5f * (d - a) + 1.5f * (b - c);
    return ((c3 * t + c2) * t + c1) * t + b;
}

static bool stream_frame(SfxVoice *v, float *frame) {
    if (v->block_used >= v->block_frames) {
        int got = stb_vorbis_get_samples_float_interleaved(v->vorbis, 2, v->block, SFX_BLOCK * 2);
        if (got <= 0) {
            stb_vorbis_seek_start(v->vorbis);
            got = stb_vorbis_get_samples_float_interleaved(v->vorbis, 2, v->block, SFX_BLOCK * 2);
            if (got <= 0)
                return false;
        }
        v->block_frames = got;
        v->block_used = 0;
    }
    frame[0] = v->block[v->block_used * 2];
    frame[1] = v->block[v->block_used * 2 + 1];
    v->block_used++;
    return true;
}

static void render_voice(Sfx *s, SfxVoice *v, float *out, int frames) {
    const SfxInfo *info = &sounds[v->sound];
    const SfxSample *smp = &s->samples[v->sound];
    float norm = level(v->sound);
    float g0 = v->gain, p0 = v->pitch, n0 = v->pan, c0 = v->cutoff;
    float dg = (v->target_gain - g0) / frames, dp = (v->target_pitch - p0) / frames;
    float dc = (v->target_cutoff - c0) / frames;
    bool stereo = info->stream || smp->channels == 2;
    // Equal-power pan for mono sources, balance for stereo ones, eased across the block.
    float sides[2][2];
    for (int k = 0; k < 2; k++) {
        float angle = ((k ? v->target_pan : n0) + 1) * .785398163f;
        sides[k][0] = cosf(angle) * 1.41421356f;
        sides[k][1] = sinf(angle) * 1.41421356f;
        if (stereo) {
            sides[k][0] = fminf(1, sides[k][0]);
            sides[k][1] = fminf(1, sides[k][1]);
        }
    }
    for (int i = 0; i < frames; i++) {
        float gain = g0 + dg * (i + 1), pitch = p0 + dp * (i + 1), cutoff = c0 + dc * (i + 1);
        float blend = (i + 1.f) / frames;
        float x[2];
        if (info->stream) {
            if (!stream_frame(v, x)) {
                release(s, v);
                return;
            }
        } else {
            if (!info->loop && v->position >= smp->frames) {
                v->last[0] = v->last[1] = 0;
                release(s, v);
                return;
            }
            x[0] = hermite(smp, v->position, 0, info->loop);
            x[1] = stereo ? hermite(smp, v->position, 1, info->loop) : x[0];
            v->position += pitch;
            if (info->loop && v->position >= smp->frames)
                v->position -= smp->frames;
        }
        for (int c = 0; c < 2; c++) {
            v->low[c] += (x[c] - v->low[c]) * cutoff;
            float side = sides[0][c] + (sides[1][c] - sides[0][c]) * blend;
            float y = v->low[c] * gain * norm * side;
            v->last[c] = y;
            out[i * 2 + c] += y;
        }
    }
    v->gain = v->target_gain;
    v->pitch = v->target_pitch;
    v->pan = v->target_pan;
    v->cutoff = v->target_cutoff;
    if (v->gain <= 0) {
        v->last[0] = v->last[1] = 0;
        release(s, v);
    }
}

void sfx_render(Sfx *s, float *out, int frames) {
    if (!s->initialized || frames <= 0)
        return;
    for (int i = 0; i < SFX_VOICES; i++)
        if (s->voices[i].sound >= 0)
            render_voice(s, &s->voices[i], out, frames);
    for (int i = 0; i < frames && (s->tail[0] != 0 || s->tail[1] != 0); i++)
        for (int c = 0; c < 2; c++) {
            out[i * 2 + c] += s->tail[c];
            s->tail[c] = fabsf(s->tail[c]) > 1e-6f ? s->tail[c] * TAIL_DECAY : 0;
        }
}
