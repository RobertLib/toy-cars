#include "music.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

static int failures, loads;
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL: %s\n", message);                                                \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void *load(const char *name, size_t *size) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/%s", TOYCARS_ASSET_DIR, name);
    loads++;
    return SDL_LoadFile(path, size);
}

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

/* Gated integrated loudness of interleaved stereo (ITU-R BS.1770-4), with the
   K-weighting filters derived for the soundtrack's sample rate. */
static double loudness(const float *pcm, int frames) {
    const double pi = 3.14159265358979323846;
    double a = pow(10, 3.999843853973347 / 40), w = 2 * pi * 1681.974450955533 / MUSIC_RATE;
    double alpha = sin(w) / (2 * .7071752369554196), root = 2 * sqrt(a) * alpha;
    double a0 = (a + 1) - (a - 1) * cos(w) + root;
    Biquad shelf = {a * ((a + 1) + (a - 1) * cos(w) + root) / a0,
                    -2 * a * ((a - 1) + (a + 1) * cos(w)) / a0,
                    a * ((a + 1) + (a - 1) * cos(w) - root) / a0,
                    2 * ((a - 1) - (a + 1) * cos(w)) / a0,
                    ((a + 1) - (a - 1) * cos(w) - root) / a0,
                    {0},
                    {0}};
    w = 2 * pi * 38.13547087602444 / MUSIC_RATE;
    alpha = sin(w) / (2 * .5003270373238773);
    a0 = 1 + alpha;
    Biquad high = {(1 + cos(w)) / 2 / a0,
                   -(1 + cos(w)) / a0,
                   (1 + cos(w)) / 2 / a0,
                   -2 * cos(w) / a0,
                   (1 - alpha) / a0,
                   {0},
                   {0}};
    // 400 ms blocks with 75% overlap; absolute gate at -70 LUFS, relative at -10 LU.
    int block = MUSIC_RATE * 4 / 10, hop = block / 4, blocks = (frames - block) / hop + 1;
    double *weighted = malloc(sizeof *weighted * (size_t)frames);
    double *energy = malloc(sizeof *energy * (size_t)(blocks > 0 ? blocks : 1));
    if (!weighted || !energy || blocks <= 0) {
        free(weighted);
        free(energy);
        return -INFINITY;
    }
    for (int i = 0; i < frames; i++) {
        weighted[i] = 0;
        for (int c = 0; c < 2; c++) {
            double y = filter(&high, c, filter(&shelf, c, pcm[i * 2 + c]));
            weighted[i] += y * y;
        }
    }
    for (int b = 0; b < blocks; b++) {
        energy[b] = 0;
        for (int i = b * hop; i < b * hop + block; i++)
            energy[b] += weighted[i];
        energy[b] /= block;
    }
    double gate = -70, result = -INFINITY;
    for (int pass = 0; pass < 2; pass++) {
        double sum = 0;
        int count = 0;
        for (int b = 0; b < blocks; b++)
            if (-.691 + 10 * log10(energy[b] + 1e-20) > gate) {
                sum += energy[b];
                count++;
            }
        result = count ? -.691 + 10 * log10(sum / count) : -INFINITY;
        gate = result - 10;
    }
    free(weighted);
    free(energy);
    return result;
}

static void test_cues(void) {
    for (int cue = 0; cue < MUSIC_CUES; cue++) {
        const MusicCue *info = music_cue(cue);
        size_t size = 0;
        void *data = info ? load(info->file, &size) : NULL;
        CHECK(data != NULL, "every music cue has its file");
        if (!data)
            continue;
        int error = 0;
        stb_vorbis *v = stb_vorbis_open_memory(data, (int)size, &error, NULL);
        CHECK(v != NULL, "every music file is valid Ogg Vorbis");
        if (!v) {
            SDL_free(data);
            continue;
        }
        stb_vorbis_info format = stb_vorbis_get_info(v);
        CHECK(format.sample_rate == MUSIC_RATE && format.channels == 2,
              "music is 44.1 kHz stereo, played without resampling");
        int frames = (int)stb_vorbis_stream_length_in_samples(v), decoded = 0;
        float *pcm = malloc(sizeof *pcm * 2 * (size_t)(frames + 4096));
        bool finite = true;
        float peak = 0;
        for (int got = 1; pcm && got > 0 && decoded <= frames;) {
            got = stb_vorbis_get_samples_float_interleaved(v, 2, pcm + decoded * 2, 4096 * 2);
            for (int i = decoded * 2; i < (decoded + got) * 2; i++) {
                finite &= isfinite(pcm[i]);
                peak = fmaxf(peak, fabsf(pcm[i]));
            }
            decoded += got > 0 ? got : 0;
        }
        double seconds = (double)decoded / MUSIC_RATE, lufs = pcm ? loudness(pcm, decoded) : 0;
        CHECK(pcm && decoded == frames, "every file decodes to its declared length");
        CHECK(finite && peak < 1, "decoded music is finite and unclipped");
        CHECK(info->loop ? seconds > 30 : (seconds > 2 && seconds < 15),
              "songs are full loops; jingles are short");
        CHECK(fabs(lufs - info->loudness) < .2, "loudness table matches the music file");
        printf("%-28s %6.1fs  %6.2f LUFS  peak %.3f\n", info->title, seconds, lufs, peak);
        free(pcm);
        stb_vorbis_close(v);
        SDL_free(data);
    }
}

typedef struct {
    double square, change;
    float peak, first;
    int frames;
    bool finite;
} Stats;

static FILE *preview_file;
static float previous[2];

static void little_endian(FILE *file, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; i++)
        fputc((int)((value >> (i * 8)) & 255), file);
}

static Stats render_frames(Music *m, int total) {
    Stats s = {.finite = true};
    float block[512 * 2];
    while (s.frames < total) {
        int n = total - s.frames < 512 ? total - s.frames : 512;
        music_render(m, block, n);
        for (int i = 0; i < n; i++, s.frames++) {
            float l = block[i * 2], r = block[i * 2 + 1];
            if (s.frames == 0)
                s.first = fmaxf(fabsf(l), fabsf(r));
            s.finite &= isfinite(l) && isfinite(r);
            s.peak = fmaxf(s.peak, fmaxf(fabsf(l), fabsf(r)));
            s.square += (l * l + r * r) * .5;
            s.change +=
                ((l - previous[0]) * (l - previous[0]) + (r - previous[1]) * (r - previous[1])) *
                .5;
            previous[0] = l;
            previous[1] = r;
            if (preview_file) {
                little_endian(preview_file, (uint16_t)(int16_t)lrintf(l * 32767), 2);
                little_endian(preview_file, (uint16_t)(int16_t)lrintf(r * 32767), 2);
            }
        }
    }
    return s;
}

static Stats render(Music *m, double seconds) {
    return render_frames(m, (int)lround(seconds * MUSIC_RATE));
}

static double rms(Stats s) {
    return s.frames ? sqrt(s.square / s.frames) : 0;
}

// Proportion of high-frequency content: RMS of the sample-to-sample change.
static double brightness(Stats s) {
    return s.square > 0 ? sqrt(s.change / s.square) : 0;
}

static const MusicDeck *lead(const Music *m) {
    for (int i = 0; i < MUSIC_DECKS; i++)
        if (m->decks[i].vorbis && m->decks[i].target > 0)
            return &m->decks[i];
    return NULL;
}

static int position(const Music *m) {
    const MusicDeck *d = lead(m);
    return d ? stb_vorbis_get_sample_offset(d->vorbis) : -1;
}

static int open_decks(const Music *m) {
    int count = 0;
    for (int i = 0; i < MUSIC_DECKS; i++)
        count += m->decks[i].vorbis != NULL;
    return count;
}

static void test_flow(void) {
    Music *m = malloc(sizeof *m);
    if (!m) {
        CHECK(false, "allocate music fixture");
        return;
    }
    music_init(m, load);
    MusicContext c = {MUSIC_MENU, 0, MUSIC_GOOD, false, true};
    music_context(m, c);
    Stats s = render(m, 2);
    CHECK(music_playing(m) == MUSIC_CUE_MENU && rms(s) > .02, "the menu plays its theme");

    c.scene = MUSIC_COUNTDOWN;
    music_context(m, c);
    CHECK(m->song == 0, "the first race on an environment selects its first song");
    int before = loads;
    for (int i = 0; i < 20; i++)
        music_context(m, c);
    CHECK(loads == before && m->visits[0] == 1,
          "repeated frame updates neither reload files nor reshuffle songs");
    render(m, .8);
    s = render(m, .2);
    CHECK(music_playing(m) == -1 && s.peak == 0 && open_decks(m) == 0,
          "the countdown fades the menu out and releases its decoder");

    c.scene = MUSIC_RACE;
    music_context(m, c);
    s = render(m, 3);
    CHECK(music_playing(m) == MUSIC_CUE_RACE && s.first < .002f && rms(s) > .02,
          "GO starts the race song without a click");
    double race_level = rms(s), race_brightness = brightness(s);

    c.scene = MUSIC_PAUSE;
    music_context(m, c);
    render(m, 1);
    s = render(m, 2);
    CHECK(music_playing(m) == MUSIC_CUE_RACE && rms(s) < race_level * .6 &&
              brightness(s) < race_brightness * .5,
          "pause lowers and muffles the race song");
    int paused_at = position(m);
    c.scene = MUSIC_RACE;
    music_context(m, c);
    render(m, 1);
    CHECK(music_playing(m) == MUSIC_CUE_RACE && position(m) > paused_at + MUSIC_RATE / 2,
          "resuming continues the song instead of restarting it");

    c.final_lap = true;
    music_context(m, c);
    render(m, .5);
    CHECK(music_playing(m) == MUSIC_CUE_FINAL_LAP, "the final lap opens with its jingle");
    render(m, 5);
    CHECK(music_playing(m) == MUSIC_CUE_CLIMAX && open_decks(m) == 1,
          "the faster climax version follows the jingle");

    c.scene = MUSIC_PAUSE;
    music_context(m, c);
    c.scene = MUSIC_COUNTDOWN;
    music_context(m, c);
    CHECK(m->song == 0 && m->visits[0] == 1, "restarting from pause keeps the song");
    render(m, 1);
    c.scene = MUSIC_RACE;
    c.final_lap = false;
    music_context(m, c);
    render(m, 1);
    CHECK(music_playing(m) == MUSIC_CUE_RACE, "the restarted race begins its song again");

    c.scene = MUSIC_RESULTS;
    c.finish = MUSIC_GOOD;
    music_context(m, c);
    render(m, .5);
    CHECK(music_playing(m) == MUSIC_CUE_FINISH + MUSIC_GOOD, "finishing plays the result jingle");
    render(m, 3.5);
    CHECK(music_playing(m) == MUSIC_CUE_FINISH_LOOP + MUSIC_GOOD,
          "the result jingle continues into its loop");
    c.finish = MUSIC_BAD;
    music_context(m, c);
    CHECK(music_playing(m) == MUSIC_CUE_FINISH_LOOP + MUSIC_GOOD,
          "the result stays fixed while rivals finish");
    c.finish = MUSIC_BEST;
    music_context(m, c);
    render(m, .5);
    CHECK(music_playing(m) == MUSIC_CUE_FINISH + MUSIC_BEST,
          "a settled championship medal upgrades to the celebration");
    render(m, 5);
    CHECK(music_playing(m) == MUSIC_CUE_FINISH_LOOP + MUSIC_BEST,
          "the celebration continues into its loop");

    c.scene = MUSIC_COUNTDOWN;
    music_context(m, c);
    CHECK(m->song == 1, "the next race alternates to the companion song");
    c.track = 2;
    music_context(m, c);
    c.scene = MUSIC_RACE;
    music_context(m, c);
    CHECK(m->song == 4 && music_playing(m) == MUSIC_CUE_RACE + 4,
          "changing environment selects its soundtrack");
    render(m, 2);

    c.scene = MUSIC_MENU;
    music_context(m, c);
    render(m, .3);
    CHECK(open_decks(m) == 2 && music_playing(m) == MUSIC_CUE_MENU,
          "returning to the menu crossfades");
    render(m, 1.5);
    CHECK(open_decks(m) == 1, "the outgoing song is released after the crossfade");

    c.enabled = false;
    music_context(m, c);
    render(m, 1);
    s = render(m, .2);
    CHECK(s.peak == 0, "the music switch fades to silence");
    before = loads;
    int muted_at = position(m);
    c.enabled = true;
    music_context(m, c);
    s = render(m, 1);
    CHECK(rms(s) > .02 && loads == before && position(m) > muted_at,
          "enabling music resumes the ongoing song");
    music_destroy(m);
    CHECK(open_decks(m) == 0, "destroying music releases every decoder");
    free(m);
}

static void test_seam(void) {
    Music *m = malloc(sizeof *m);
    size_t size = 0;
    void *data = load(music_cue(MUSIC_CUE_RACE)->file, &size);
    int error = 0;
    stb_vorbis *v = data ? stb_vorbis_open_memory(data, (int)size, &error, NULL) : NULL;
    if (!m || !v) {
        CHECK(false, "prepare loop seam fixture");
        free(m);
        SDL_free(data);
        return;
    }
    // Blossom Mountain's recording ends on a sustained note above the opening sample.
    int length = (int)stb_vorbis_stream_length_in_samples(v);
    float first[2], last[2], block[2 * 2];
    stb_vorbis_get_samples_float_interleaved(v, 2, first, 2);
    stb_vorbis_seek(v, (unsigned)length - 1);
    stb_vorbis_get_samples_float_interleaved(v, 2, last, 2);
    stb_vorbis_close(v);
    SDL_free(data);
    float recorded = fmaxf(fabsf(last[0] - first[0]), fabsf(last[1] - first[1]));
    music_init(m, load);
    music_context(m, (MusicContext){MUSIC_RACE, 0, MUSIC_GOOD, false, true});
    render_frames(m, length - 1);
    music_render(m, block, 2);
    float step = fmaxf(fabsf(block[2] - block[0]), fabsf(block[3] - block[1]));
    CHECK(recorded > .1f, "the seam fixture has a step in its recording");
    CHECK(music_playing(m) == MUSIC_CUE_RACE && step < recorded * .02f,
          "looping settles across the seam without a click");
    printf("Loop seam: recorded step %.4f, played step %.5f\n", recorded, step);
    music_destroy(m);
    free(m);
}

static void timeline(Music *m, int chunk, float *out, int frames) {
    static const struct {
        double at;
        MusicContext context;
    } events[] = {{0, {MUSIC_MENU, 1, MUSIC_GOOD, false, true}},
                  {1, {MUSIC_COUNTDOWN, 1, MUSIC_GOOD, false, true}},
                  {2, {MUSIC_RACE, 1, MUSIC_GOOD, false, true}},
                  {4, {MUSIC_RACE, 1, MUSIC_GOOD, true, true}},
                  {10, {MUSIC_RESULTS, 1, MUSIC_BAD, false, true}}};
    int next = 0;
    for (int done = 0; done < frames;) {
        while (next < 5 && events[next].at * MUSIC_RATE <= done)
            music_context(m, events[next++].context);
        int n = frames - done < chunk ? frames - done : chunk;
        if (next < 5 && (int)(events[next].at * MUSIC_RATE) - done < n)
            n = (int)(events[next].at * MUSIC_RATE) - done;
        music_render(m, out + done * 2, n);
        done += n;
    }
}

static void test_determinism(void) {
    int frames = 16 * MUSIC_RATE;
    Music *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    float *x = malloc(sizeof *x * 2 * (size_t)frames), *y = malloc(sizeof *y * 2 * (size_t)frames);
    if (!a || !b || !x || !y) {
        CHECK(false, "allocate determinism fixtures");
    } else {
        music_init(a, load);
        music_init(b, load);
        timeline(a, MUSIC_BLOCK, x, frames);
        timeline(b, 37, y, frames);
        CHECK(!memcmp(x, y, sizeof *x * 2 * (size_t)frames),
              "rendering is independent of the audio callback's block size");
        float peak = 0;
        for (int i = 0; i < frames * 2; i++)
            peak = fmaxf(peak, fabsf(x[i]));
        CHECK(peak > .05f && peak < .4f, "music leaves headroom for engine and effects");
        music_destroy(a);
        music_destroy(b);
    }
    free(a);
    free(b);
    free(x);
    free(y);

    Music *m = malloc(sizeof *m);
    if (!m)
        return;
    music_init(m, missing);
    music_context(m, (MusicContext){MUSIC_MENU, 0, MUSIC_GOOD, false, true});
    Stats s = render(m, .5);
    music_context(m, (MusicContext){MUSIC_RACE, 0, MUSIC_GOOD, false, true});
    Stats race = render(m, .5);
    CHECK(s.finite && s.peak == 0 && race.peak == 0 && music_playing(m) == -1,
          "missing music files leave silence");
    music_destroy(m);
    free(m);
}

static void write_preview(const char *directory) {
    const char *names[] = {"harvest-hills", "sunshine-coast", "alpine-rush"};
    for (int track = 0; track < 3; track++) {
        static const struct {
            double seconds;
            MusicScene scene;
            bool final_lap;
        } steps[] = {{6, MUSIC_MENU, false},     {3, MUSIC_COUNTDOWN, false},
                     {14, MUSIC_RACE, false},    {3, MUSIC_PAUSE, false},
                     {4, MUSIC_RACE, false},     {14, MUSIC_RACE, true},
                     {14, MUSIC_RESULTS, false}, {6, MUSIC_MENU, false}};
        double total = 0;
        for (int i = 0; i < 8; i++)
            total += steps[i].seconds;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.wav", directory, names[track]);
        preview_file = fopen(path, "wb");
        CHECK(preview_file != NULL, "open preview WAV");
        if (!preview_file)
            return;
        uint32_t bytes = (uint32_t)(total * MUSIC_RATE) * 4;
        fwrite("RIFF", 1, 4, preview_file);
        little_endian(preview_file, 36 + bytes, 4);
        fwrite("WAVEfmt ", 1, 8, preview_file);
        little_endian(preview_file, 16, 4);
        little_endian(preview_file, 1, 2);
        little_endian(preview_file, 2, 2);
        little_endian(preview_file, MUSIC_RATE, 4);
        little_endian(preview_file, MUSIC_RATE * 4, 4);
        little_endian(preview_file, 4, 2);
        little_endian(preview_file, 16, 2);
        fwrite("data", 1, 4, preview_file);
        little_endian(preview_file, bytes, 4);
        Music *m = malloc(sizeof *m);
        if (m) {
            music_init(m, load);
            for (int i = 0; i < 8; i++) {
                music_context(
                    m, (MusicContext){steps[i].scene, track, MUSIC_BEST, steps[i].final_lap, true});
                render(m, steps[i].seconds);
            }
            music_destroy(m);
            free(m);
        }
        CHECK(fclose(preview_file) == 0, "finish preview WAV");
        preview_file = NULL;
        printf("Preview: %s\n", path);
    }
}

int main(int argc, char **argv) {
    const char *directory = NULL;
    if (argc == 3 && strcmp(argv[1], "--render") == 0)
        directory = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--render existing-directory]\n", argv[0]);
        return 2;
    }
    clock_t start = clock();
    test_cues();
    test_flow();
    test_seam();
    test_determinism();
    if (directory)
        write_preview(directory);
    printf("Music checks: %d failures (%.2fs CPU)\n", failures,
           (double)(clock() - start) / CLOCKS_PER_SEC);
    return failures ? 1 : 0;
}
