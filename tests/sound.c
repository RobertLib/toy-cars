// Checks every sound file, the effects mixer and the audio direction of real
// races, without a window or an audio device.
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

static char pref_directory[2048];
static int failures;
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL: %s\n", message);                                                \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
static char *test_pref_path(const char *org, const char *app) {
    (void)org;
    (void)app;
    return SDL_strdup(pref_directory);
}
#define SDL_GetPrefPath test_pref_path
#include "../src/assets.c"
#undef SDL_GetPrefPath

static void *missing(const char *name, size_t *size) {
    (void)name;
    *size = 0;
    return NULL;
}

typedef struct {
    double b0, b1, b2, a1, a2, z1[2], z2[2];
} Biquad;

static double filter(Biquad *f, int channel, double x) {
    double y = f->b0 * x + f->z1[channel];
    f->z1[channel] = f->b1 * x - f->a1 * y + f->z2[channel];
    f->z2[channel] = f->b2 * x - f->a2 * y;
    return y;
}

/* Gated integrated loudness (ITU-R BS.1770-4) of interleaved audio. Sounds
   shorter than one 400 ms block are measured as a single block. */
static double loudness(const short *pcm, int frames, int channels) {
    const double pi = 3.14159265358979323846, rate = SFX_RATE;
    double k = tan(pi * 1681.974450955533 / rate), q = .7071752369554196;
    double vh = pow(10, 3.999843853973347 / 20), vb = pow(vh, .4996667741545416);
    double a0 = 1 + k / q + k * k;
    Biquad shelf = {(vh + vb * k / q + k * k) / a0,
                    2 * (k * k - vh) / a0,
                    (vh - vb * k / q + k * k) / a0,
                    2 * (k * k - 1) / a0,
                    (1 - k / q + k * k) / a0,
                    {0},
                    {0}};
    k = tan(pi * 38.13547087602444 / rate);
    q = .5003270373238773;
    a0 = 1 + k / q + k * k;
    Biquad high = {1, -2, 1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0, {0}, {0}};
    double *weighted = malloc(sizeof *weighted * (size_t)frames);
    int block = (int)rate * 4 / 10, hop = block / 4;
    if (frames < block)
        block = frames;
    int blocks = (frames - block) / hop + 1;
    double *energy = malloc(sizeof *energy * (size_t)blocks);
    if (!weighted || !energy) {
        free(weighted);
        free(energy);
        return -INFINITY;
    }
    for (int i = 0; i < frames; i++) {
        double sum = 0;
        for (int c = 0; c < channels; c++) {
            double y = filter(&high, c, filter(&shelf, c, pcm[i * channels + c] / 32768.));
            sum += y * y;
        }
        weighted[i] = sum;
    }
    for (int b = 0; b < blocks; b++) {
        double sum = 0;
        for (int i = 0; i < block; i++)
            sum += weighted[b * hop + i];
        energy[b] = sum / block;
    }
    double total = 0;
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        double gate = pass ? -.691 + 10 * log10(total / count) - 10 : -70;
        double sum = 0;
        int n = 0;
        for (int b = 0; b < blocks; b++)
            if (-.691 + 10 * log10(energy[b] + 1e-20) > gate) {
                sum += energy[b];
                n++;
            }
        if (!n)
            break;
        total = sum;
        count = n;
    }
    free(weighted);
    free(energy);
    return count ? -.691 + 10 * log10(total / count) : -INFINITY;
}

static void test_files(void) {
    for (int i = 0; i < SFX_COUNT; i++) {
        const SfxInfo *info = sfx_info(i);
        char message[256];
        size_t size = 0;
        void *data = asset_read(info->file, &size);
        int channels = 0, rate = 0;
        short *pcm = NULL;
        int frames = data ? stb_vorbis_decode_memory(data, (int)size, &channels, &rate, &pcm) : -1;
        SDL_free(data);
        SDL_snprintf(message, sizeof message, "%s decodes as 44.1 kHz audio", info->file);
        CHECK(frames > 0 && rate == SFX_RATE, message);
        if (frames <= 0) {
            free(pcm);
            continue;
        }
        SDL_snprintf(message, sizeof message, "%s is %s", info->file,
                     info->stream ? "a stereo bed" : "mono or stereo");
        CHECK(info->stream ? channels == 2 : channels == 1 || channels == 2, message);
        double seconds = (double)frames / SFX_RATE;
        SDL_snprintf(message, sizeof message, "%s length %.2f s suits its use", info->file,
                     seconds);
        CHECK(info->stream ? seconds >= 20 && seconds <= 150
              : info->loop ? seconds >= .8 && seconds <= 30
                           : seconds >= .03 && seconds <= 10,
              message);
        int peak = 0;
        for (int s = 0; s < frames * channels; s++)
            peak = abs(pcm[s]) > peak ? abs(pcm[s]) : peak;
        SDL_snprintf(message, sizeof message, "%s leaves headroom", info->file);
        CHECK(peak < 32700, message);
        double measured = loudness(pcm, frames, channels);
        SDL_snprintf(message, sizeof message, "%s loudness %.2f LUFS matches the table (%.2f)",
                     info->file, measured, info->loudness);
        CHECK(fabs(measured - info->loudness) <= .2, message);
        if (fabs(measured - info->loudness) > .2)
            printf("%s measures %.2f LUFS\n", info->file, measured);
        if (info->loop) {
            // The seam must be no larger a step than the sound makes anyway.
            int steps[64] = {0}, worst = 0;
            for (int s = 1; s < frames; s++) {
                int step = abs(pcm[s * channels] - pcm[(s - 1) * channels]);
                steps[step * 64 / 65536]++;
            }
            int seam = abs(pcm[0] - pcm[(frames - 1) * channels]);
            for (int b = 63, seen = 0; b >= 0; b--) {
                seen += steps[b];
                if (seen > frames / 1000) {
                    worst = (b + 1) * 65536 / 64;
                    break;
                }
            }
            SDL_snprintf(message, sizeof message, "%s loops without a click", info->file);
            CHECK(seam <= worst, message);
        }
        free(pcm);
    }
}

static float block_peak(const float *x, int frames) {
    float peak = 0;
    for (int i = 0; i < frames * 2; i++)
        peak = fmaxf(peak, fabsf(x[i]));
    return peak;
}

static float largest_step(const float *x, int frames, float *previous) {
    float worst = 0;
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            worst = fmaxf(worst, fabsf(x[i * 2 + c] - previous[c]));
            previous[c] = x[i * 2 + c];
        }
    return worst;
}

static void test_mixer(void) {
    static Sfx s;
    float block[512 * 2], previous[2] = {0};
    sfx_init(&s, asset_read);
    // A held loop rises from silence and is released once faded out.
    uint32_t handle = 0;
    float jump = 0;
    for (int frame = 0; frame < 60; frame++) {
        float gain = frame < 40 ? 1 : 0;
        sfx_hold(&s, &handle, SFX_ENGINE_MID, gain, 1 + frame * .01f, sinf(frame * .3f), 1);
        memset(block, 0, sizeof block);
        sfx_render(&s, block, 512);
        if (frame == 0 || frame == 40)
            jump = fmaxf(jump, largest_step(block, 8, previous));
        else
            largest_step(block, 512, previous);
    }
    CHECK(jump < .05f, "held loops start and stop without a click");
    CHECK(!sfx_active(&s, handle) && sfx_voices(&s) == 0, "a faded loop releases its voice");
    // Changing the sound of a held handle crossfades instead of cutting.
    handle = 0;
    sfx_hold(&s, &handle, SFX_ENGINE_LOW, 1, 1, 0, 1);
    memset(block, 0, sizeof block);
    sfx_render(&s, block, 512);
    uint32_t first = handle;
    sfx_hold(&s, &handle, SFX_ENGINE_HIGH, 1, 1, 0, 1);
    CHECK(handle != first && sfx_voices(&s) == 2, "a new sound fades in beside the old one");
    sfx_stop_all(&s);
    // One-shots beyond the voice budget replace the quietest ones.
    for (int i = 0; i < SFX_VOICES + 20; i++)
        sfx_play(&s, SFX_CONE, .1f + i * .01f, 1, 0);
    CHECK(sfx_voices(&s) == SFX_VOICES, "one-shots share a fixed voice budget");
    memset(block, 0, sizeof block);
    sfx_render(&s, block, 512);
    CHECK(isfinite(block_peak(block, 512)), "a full mix stays finite");
    sfx_destroy(&s);
    // Missing files leave the mixer silent rather than failing.
    sfx_init(&s, missing);
    handle = 0;
    sfx_hold(&s, &handle, SFX_ENGINE_IDLE, 1, 1, 0, 1);
    CHECK(!sfx_play(&s, SFX_GO, 1, 1, 0) && !handle, "missing sounds do not start");
    memset(block, 0, sizeof block);
    sfx_render(&s, block, 512);
    CHECK(block_peak(block, 512) == 0, "missing sounds stay silent");
    sfx_destroy(&s);
}

static FILE *preview;
static float previous_out[2];

static void little_endian(FILE *file, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; i++)
        fputc((int)(value >> (8 * i)) & 0xff, file);
}

// A chase camera behind the player's car, like the race view.
static Listener chase(const Game *g) {
    const Car *c = &g->cars[0];
    Vec3 forward = v3(sinf(c->yaw), 0, cosf(c->yaw));
    return (Listener){vadd(c->pos, vadd(vmul(forward, -7), v3(0, 3, 0))),
                      vadd(c->pos, vmul(forward, 5)), vadd(c->pos, v3(25, 14, 10)), true};
}

typedef struct {
    double energy, peak, step;
    long frames, limited;
    int gear_max;
    float rpm_min, rpm_max;
} RaceStats;

/* Plays the game in 60 fps frames: two simulation steps, then the frame's
   audio direction and 735 frames of mix. */
static void play(Audio *a, Game *g, float seconds, RaceStats *stats) {
    float data[735 * 2];
    for (int frame = 0; frame < (int)(seconds * 60); frame++) {
        for (int step = 0; step < 2; step++)
            game_tick(g, (Controls){0}, FIXED_DT, true);
        Listener l = chase(g);
        audio_direct(a, g, &l, 1 / 60.f);
        CHECK(g->sound_count == 0, "every sound cue is consumed each frame");
        audio_render(a, data, 735);
        for (int i = 0; i < 735 * 2; i++) {
            stats->energy += data[i] * data[i];
            stats->peak = fmax(stats->peak, fabsf(data[i]));
            stats->step = fmax(stats->step, fabsf(data[i] - previous_out[i & 1]));
            stats->limited += a->limiter < .9f;
            previous_out[i & 1] = data[i];
            if (preview) {
                int16_t v = (int16_t)lrintf(clampf(data[i], -1, 1) * 32767);
                little_endian(preview, (uint16_t)v, 2);
            }
        }
        stats->frames += 735;
        if (g->screen == SCREEN_RACE) {
            const CarAudio *e = &a->cars[0];
            stats->gear_max = e->gear > stats->gear_max ? e->gear : stats->gear_max;
            stats->rpm_min = fminf(stats->rpm_min, e->rpm);
            stats->rpm_max = fmaxf(stats->rpm_max, e->rpm);
        }
    }
}

static double rms_db(const RaceStats *s) {
    return 10 * log10(s->energy / fmax(1, (double)s->frames * 2) + 1e-20);
}

static void test_race(Game *g, const char *directory) {
    static Audio a;
    for (int track = 0; track < TRACK_COUNT; track++) {
        audio_init(&a, false);
        g->profile.championship_completed = TRACK_COUNT - 1;
        g->profile.sound = true;
        g->profile.music = true;
        g->test_mode = true;
        game_set_mode(g, MODE_ARCADE);
        g->selected = track;
        g->screen = SCREEN_MENU;
        if (directory) {
            char path[2400];
            SDL_snprintf(path, sizeof path, "%s/race-%d.wav", directory, track + 1);
            preview = fopen(path, "wb");
            if (preview) {
                fputs("RIFF", preview);
                little_endian(preview, 0, 4);
                fputs("WAVEfmt ", preview);
                little_endian(preview, 16, 4);
                little_endian(preview, 1, 2);
                little_endian(preview, 2, 2);
                little_endian(preview, SFX_RATE, 4);
                little_endian(preview, SFX_RATE * 4, 4);
                little_endian(preview, 4, 2);
                little_endian(preview, 16, 2);
                fputs("data", preview);
                little_endian(preview, 0, 4);
            }
        }
        RaceStats menu = {0}, race = {.rpm_min = 1e9f}, pause = {0}, back = {0};
        play(&a, g, 2, &menu);
        int menu_voices = sfx_voices(&a.sfx);
        game_start(g);
        CHECK(g->screen == SCREEN_COUNTDOWN, "the race starts with the countdown");
        play(&a, g, 40, &race);
        CHECK(race.gear_max >= 3, "the gearbox shifts up through the gears");
        CHECK(race.rpm_min >= 850 && race.rpm_max <= 7000, "engine speed stays in its range");
        CHECK(race.peak <= .9, "the limiter keeps the race mix below full scale");
        CHECK(race.limited < race.frames / 100, "the race mix rarely needs the limiter");
        CHECK(rms_db(&race) > -36, "the race is clearly audible");
        CHECK(menu_voices <= 1, "menus play no race sounds");
        g->screen = SCREEN_PAUSE;
        play(&a, g, 1.5f, &pause);
        CHECK(sfx_voices(&a.sfx) <= 1, "pausing silences the race");
        g->screen = SCREEN_RACE;
        play(&a, g, 4, &back);
        CHECK(sfx_voices(&a.sfx) >= 3, "resuming brings the race back");
        printf("%s: race %.1f dBFS RMS, peak %.2f, limited %.1f%%, top gear %d, rpm %.0f-%.0f\n",
               g->tracks[track].name, rms_db(&race), race.peak,
               100. * race.limited / (2 * race.frames), race.gear_max + 1, race.rpm_min,
               race.rpm_max);
        if (preview) {
            long bytes = ftell(preview);
            fseek(preview, 4, SEEK_SET);
            little_endian(preview, (uint32_t)(bytes - 8), 4);
            fseek(preview, 40, SEEK_SET);
            little_endian(preview, (uint32_t)(bytes - 44), 4);
            fclose(preview);
            preview = NULL;
        }
        audio_destroy(&a);
    }
}

int main(int argc, char **argv) {
    if (argc < 2 || strlen(argv[1]) > sizeof pref_directory - 2) {
        fprintf(stderr, "Usage: %s profile-directory [--render existing-directory]\n", argv[0]);
        return 2;
    }
    SDL_snprintf(pref_directory, sizeof pref_directory, "%s/", argv[1]);
    if (!SDL_CreateDirectory(pref_directory))
        return 2;
    const char *render = argc == 4 && !strcmp(argv[2], "--render") ? argv[3] : NULL;
    test_files();
    test_mixer();
    Game *g = calloc(1, sizeof *g);
    if (!g || !game_load(g))
        return 2;
    test_race(g, render);
    free(g);
    if (failures)
        fprintf(stderr, "%d sound check(s) failed\n", failures);
    else
        printf("All sound checks passed\n");
    return failures ? 1 : 0;
}
