#include "music.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL: %s\n", message);                                                \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void render(Music *m, int frames) {
    float l, r;
    for (int i = 0; i < frames; i++)
        music_sample(m, &l, &r);
}

static void little_endian(FILE *file, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; i++)
        fputc((int)((value >> (i * 8)) & 255), file);
}

static FILE *preview(const char *directory, int song, int frames) {
    if (!directory)
        return NULL;
    char path[1024];
    snprintf(path, sizeof path, "%s/%02d-%s.wav", directory, song, music_song_name(song));
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL, "open preview WAV");
    if (!file)
        return NULL;
    fwrite("RIFF", 1, 4, file);
    little_endian(file, 36 + (uint32_t)frames * 4, 4);
    fwrite("WAVEfmt ", 1, 8, file);
    little_endian(file, 16, 4);
    little_endian(file, 1, 2);
    little_endian(file, 2, 2);
    little_endian(file, MUSIC_RATE, 4);
    little_endian(file, MUSIC_RATE * 4, 4);
    little_endian(file, 4, 2);
    little_endian(file, 16, 2);
    fwrite("data", 1, 4, file);
    little_endian(file, (uint32_t)frames * 4, 4);
    return file;
}

static void select_song(Music *m, int song, float intensity) {
    music_init(m);
    if (song > 0) {
        int track = (song - 1) / 2;
        m->visits[track] = (unsigned)((song - 1) % 2);
        music_context(m, (MusicContext){MUSIC_RACE, track, intensity, true});
    }
}

static void test_scores(const char *directory) {
    Music *m = malloc(sizeof *m);
    if (!m) {
        CHECK(false, "allocate renderer");
        return;
    }
    uint32_t hashes[MUSIC_SONGS] = {0};
    for (int song = 0; song < MUSIC_SONGS; song++) {
        select_song(m, song, .85f);
        int preview_frames = 45 * MUSIC_RATE;
        FILE *file = preview(directory, song, preview_frames);
        double square = 0, stereo = 0;
        float peak = 0, jump = 0, previous_l = 0, previous_r = 0;
        bool finite = true;
        int frames = 0;
        unsigned variation = m->variation;
        // At least one complete 64-bar arrangement and its transition to the next song.
        do {
            float l, r;
            music_sample(m, &l, &r);
            if (frames == 0)
                variation = m->variation;
            finite &= isfinite(l) && isfinite(r);
            square += (l * l + r * r) * .5;
            stereo += (l - r) * (l - r);
            peak = fmaxf(peak, fmaxf(fabsf(l), fabsf(r)));
            jump = fmaxf(jump, fmaxf(fabsf(l - previous_l), fabsf(r - previous_r)));
            previous_l = l;
            previous_r = r;
            if (frames % 480 == 0) {
                uint32_t bits;
                memcpy(&bits, &l, sizeof bits);
                hashes[song] = hashes[song] * 16777619u ^ bits;
            }
            if (file && frames < preview_frames) {
                little_endian(file, (uint16_t)(int16_t)(l * 32767), 2);
                little_endian(file, (uint16_t)(int16_t)(r * 32767), 2);
            }
            frames++;
        } while ((m->variation == variation || frames < preview_frames) &&
                 frames < MUSIC_RATE * 170);
        if (file)
            CHECK(fclose(file) == 0, "finish preview WAV");
        CHECK(finite, "all output samples are finite");
        CHECK(peak < .55f, "music leaves headroom for engine and effects");
        CHECK(jump < .09f, "no large sample discontinuities at note/track boundaries");
        CHECK(sqrt(square / frames) > .008, "every score produces audible music");
        CHECK(stereo / frames > .000001, "every score has stereo width");
        CHECK(m->variation != variation, "full arrangement reaches its next cycle");
        if (song > 0)
            CHECK(m->song == 1 + ((song - 1) / 2) * 2 + song % 2,
                  "long races rotate to the companion song");
        for (int i = 0; i < song; i++)
            CHECK(hashes[i] != hashes[song], "scores have distinct output");
        printf("%-20s %6.1fs  peak %.3f  RMS %.3f  max step %.3f\n", music_song_name(song),
               (double)frames / MUSIC_RATE, peak, sqrt(square / frames), jump);
    }
    free(m);
}

static void test_controls(void) {
    Music *m = malloc(sizeof *m);
    Music *other = malloc(sizeof *other);
    if (!m || !other) {
        CHECK(false, "allocate control fixtures");
        free(m);
        free(other);
        return;
    }
    select_song(m, 1, .2f);
    render(m, MUSIC_RATE);
    CHECK(m->song == 1, "country starts its first composition");
    music_context(m, (MusicContext){MUSIC_COUNTDOWN, 0, 0, true});
    CHECK(m->song == 1 && m->pending_song == 2, "retry queues the alternate without cutting a bar");
    for (int i = 0; i < 20; i++)
        music_context(m, m->context);
    CHECK(m->visits[0] == 2, "repeated frame updates do not reshuffle the playlist");
    render(m, MUSIC_RATE * 3);
    CHECK(m->song == 2, "queued change resolves within a bar");
    music_context(m, (MusicContext){MUSIC_RACE, 0, .9f, true});
    music_context(m, (MusicContext){MUSIC_PAUSE, 0, 0, true});
    render(m, MUSIC_RATE);
    music_context(m, (MusicContext){MUSIC_RACE, 0, .9f, true});
    CHECK(m->pending_song == -1 && m->visits[0] == 2, "pause and resume preserve composition");
    music_context(m, (MusicContext){MUSIC_RACE, 2, .9f, true});
    render(m, MUSIC_RATE * 3);
    CHECK(m->song == 5, "changing environment selects its soundtrack");
    music_context(m, (MusicContext){MUSIC_RESULTS, 2, 0, true});
    render(m, MUSIC_RATE * 4);
    CHECK(m->arranged_scene == MUSIC_RESULTS && m->layers[4] < .01f,
          "results resolve into the calm arrangement");
    music_context(m, (MusicContext){MUSIC_MENU, 0, 0, true});
    render(m, MUSIC_RATE * 3);
    CHECK(m->song == 0, "returning to menu restores the paddock score");

    *other = *m;
    bool deterministic = true;
    for (int i = 0; i < MUSIC_RATE; i++) {
        float l, r, l2, r2;
        music_sample(m, &l, &r);
        music_sample(other, &l2, &r2);
        deterministic &= l == l2 && r == r2;
    }
    CHECK(deterministic, "rendering is independent of frame batching and external RNG");
    music_context(m, (MusicContext){MUSIC_MENU, 0, 0, false});
    render(m, MUSIC_RATE);
    float l, r;
    music_sample(m, &l, &r);
    CHECK(fabsf(l) + fabsf(r) < .000001f, "music toggle fades all voices and echoes to silence");
    unsigned tick = m->tick;
    music_context(m, (MusicContext){MUSIC_MENU, 0, 0, true});
    render(m, MUSIC_RATE);
    CHECK(m->tick > tick && m->volume > .8f, "enabling music resumes the ongoing arrangement");

    select_song(m, 3, 0);
    select_song(other, 3, 1);
    render(m, MUSIC_RATE * 10);
    render(other, MUSIC_RATE * 10);
    CHECK(m->song == other->song && m->tick == other->tick,
          "gameplay intensity preserves musical timing");
    CHECK(other->layers[4] > m->layers[4] + .7f && other->layers[2] > m->layers[2] + .3f,
          "racing intensity adds percussion and answering layers");

    select_song(m, 1, .8f);
    do {
        music_sample(m, &l, &r);
    } while (m->tick < 16 || m->until_tick > 48);
    music_context(m, (MusicContext){MUSIC_MENU, 0, 0, true});
    render(m, 96);
    CHECK(m->song == 1, "last-millisecond requests do not abruptly clear live voices");
    render(m, MUSIC_RATE * 3);
    CHECK(m->song == 0, "late requests resolve on the following bar after fading");

    select_song(m, 1, .8f);
    render(m, MUSIC_RATE);
    music_context(m, (MusicContext){MUSIC_RESULTS, 0, 0, true});
    render(m, MUSIC_RATE * 3);
    // Put a results screen one sample before the form wraps; no outgoing fade
    // was scheduled, so a new environment must remain queued for the next bar.
    m->tick = 64 * 16;
    m->until_tick = 1;
    music_context(m, (MusicContext){MUSIC_RACE, 2, .8f, true});
    render(m, 2);
    CHECK(m->song == 1 && m->pending_song == 5,
          "automatic playlist rotation cannot replace a pending environment change");
    render(m, MUSIC_RATE * 3);
    CHECK(m->song == 5, "environment change survives the end of a full arrangement");
    free(other);
    free(m);
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
    test_scores(directory);
    test_controls();
    printf("Music checks: %d failures (%.2fs CPU)\n", failures,
           (double)(clock() - start) / CLOCKS_PER_SEC);
    return failures ? 1 : 0;
}
