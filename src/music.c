#include "music.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

/* Every cue is normalized to this loudness. The masters are around -11 LUFS;
   the player's engine is near -26 LUFS at full throttle, a whole race near -21. */
#define MUSIC_LOUDNESS -26.f
#define PAUSE_LEVEL .45f
#define ATTACK .005f
// A 2 ms settle from the last emitted sample removes steps at loop seams.
#define SEAM_DECAY .9887f

// Original recordings by MintoDog (CC0), see assets/licenses/MUSIC.txt.
static const MusicCue cues[MUSIC_CUES] = {
    {"music/racing-game-menu.ogg", "Racing Game Menu", -10.55f, true},
    // Harvest Hills, Sunshine Coast and Alpine Rush alternate between two songs.
    {"music/blossom-mountain.ogg", "Blossom Mountain", -10.50f, true},
    {"music/joyful-jungle.ogg", "Joyful Jungle", -10.91f, true},
    {"music/sky-blue-street.ogg", "Sky Blue Street", -12.93f, true},
    {"music/sunny-city.ogg", "Sunny City", -10.84f, true},
    {"music/fever-stadium.ogg", "Fever Stadium", -10.96f, true},
    {"music/cool-highway.ogg", "Cool Highway", -11.51f, true},
    // Faster final-lap versions, in the same order.
    {"music/blossom-mountain-climax.ogg", "Blossom Mountain (Climax)", -11.86f, true},
    {"music/joyful-jungle-climax.ogg", "Joyful Jungle (Climax)", -10.66f, true},
    {"music/sky-blue-street-climax.ogg", "Sky Blue Street (Climax)", -13.00f, true},
    {"music/sunny-city-climax.ogg", "Sunny City (Climax)", -10.98f, true},
    {"music/fever-stadium-climax.ogg", "Fever Stadium (Climax)", -12.10f, true},
    {"music/cool-highway-climax.ogg", "Cool Highway (Climax)", -12.38f, true},
    {"music/final-lap.ogg", "Final Lap", -12.54f, false},
    {"music/finish-best.ogg", "Race Finish Best", -12.05f, false},
    {"music/finish-good.ogg", "Race Finish Good", -11.30f, false},
    {"music/finish-bad.ogg", "Race Finish Bad", -10.57f, false},
    {"music/finish-best-loop.ogg", "Race Finish Best (Loop)", -11.80f, true},
    {"music/finish-good-loop.ogg", "Race Finish Good (Loop)", -12.56f, true},
    {"music/finish-bad-loop.ogg", "Race Finish Bad (Loop)", -11.34f, true}};

// The two songs each circuit alternates between, in Championship order. Moods
// follow the setting: harp and tambourine for the farm and the cherry blossom,
// saxophone for the coast and the harbour, Eurobeat for the volcano.
static const int TRACK_SONGS[MUSIC_TRACKS][2] = {
    {0, 1}, {2, 3}, {4, 5}, // Harvest Hills, Sunshine Coast, Alpine Rush
    {3, 4}, {0, 5}, {1, 4}, // Canyon Dash, Maple Woods, Jungle Ruins
    {0, 2}, {5, 3}, {1, 2}, // Sakura Hills, Harbor Lights, Highland Loch
    {4, 5},                 // Volcano Peak
};

int music_track_song(int track, int visit) {
    if (track < 0 || track >= MUSIC_TRACKS)
        track = 0;
    return TRACK_SONGS[track][visit % 2];
}

const MusicCue *music_cue(int cue) {
    return cue >= 0 && cue < MUSIC_CUES ? &cues[cue] : NULL;
}

static void close_deck(MusicDeck *d) {
    if (d->vorbis)
        stb_vorbis_close(d->vorbis);
    SDL_free(d->data);
    *d = (MusicDeck){.cue = -1, .then = -1};
}

// Replaces the deck's decoder and keeps its gain, so a jingle can hand over to a loop.
static bool open_deck(Music *m, MusicDeck *d, int cue) {
    size_t size = 0;
    void *data = m->load ? m->load(cues[cue].file, &size) : NULL;
    int error = 0;
    stb_vorbis *v =
        data && size <= INT_MAX ? stb_vorbis_open_memory(data, (int)size, &error, NULL) : NULL;
    if (v && (stb_vorbis_get_info(v).sample_rate != MUSIC_RATE ||
              stb_vorbis_get_info(v).channels != 2)) {
        stb_vorbis_close(v);
        v = NULL;
    }
    if (!v) {
        SDL_Log("Music unavailable: %s", cues[cue].file);
        SDL_free(data);
        return false;
    }
    if (d->vorbis)
        stb_vorbis_close(d->vorbis);
    SDL_free(d->data);
    d->vorbis = v;
    d->data = data;
    d->cue = cue;
    return true;
}

static void fade(Music *m, float seconds) {
    for (int i = 0; i < MUSIC_DECKS; i++) {
        MusicDeck *d = &m->decks[i];
        if (d->vorbis && d->target > 0) {
            d->target = 0;
            d->step = 1 / (seconds * MUSIC_RATE);
            d->then = -1;
        }
    }
}

/* Fades out everything audible and starts a cue on a free deck. A jingle names
   the loop that follows it without a gap. */
static void play(Music *m, int cue, int then, float fade_out, float fade_in) {
    fade(m, fade_out);
    MusicDeck *d = &m->decks[0];
    for (int i = 1; i < MUSIC_DECKS && d->vorbis; i++)
        if (!m->decks[i].vorbis || m->decks[i].gain < d->gain)
            d = &m->decks[i];
    close_deck(d);
    if (!open_deck(m, d, cue)) {
        if (then < 0 || !open_deck(m, d, then))
            return;
        then = -1;
    }
    d->then = then;
    d->target = 1;
    d->step = 1 / (fmaxf(fade_in, ATTACK) * MUSIC_RATE);
}

static float cue_level(int cue) {
    return powf(10, (MUSIC_LOUDNESS - cues[cue].loudness) / 20);
}

static void mix(Music *m, MusicDeck *d, float *out, int frames) {
    float level = d->vorbis ? cue_level(d->cue) : 0;
    bool wrapped = false;
    for (int done = 0; d->vorbis && done < frames;) {
        int want = frames - done < MUSIC_BLOCK ? frames - done : MUSIC_BLOCK;
        int got = stb_vorbis_get_samples_float_interleaved(d->vorbis, 2, m->block, want * 2);
        if (got <= 0) {
            // Loops wrap sample-accurately; a jingle continues into its loop.
            if (cues[d->cue].loop && !wrapped) {
                stb_vorbis_seek_start(d->vorbis);
                wrapped = true;
            } else if (d->then >= 0 && open_deck(m, d, d->then)) {
                d->then = -1;
                level = cue_level(d->cue);
            } else {
                close_deck(d);
                break;
            }
            d->seam = true;
            continue;
        }
        wrapped = false;
        if (d->seam) {
            // Continue from the last emitted sample and settle onto the new material.
            d->carry[0] = d->last[0] - m->block[0];
            d->carry[1] = d->last[1] - m->block[1];
            d->seam = false;
        }
        for (int i = 0; i < got; i++) {
            if (d->gain < d->target)
                d->gain = fminf(d->target, d->gain + d->step);
            else if (d->gain > d->target)
                d->gain = fmaxf(d->target, d->gain - d->step);
            for (int c = 0; c < 2; c++) {
                float x = m->block[i * 2 + c] + d->carry[c];
                d->carry[c] = fabsf(d->carry[c]) > 1e-6f ? d->carry[c] * SEAM_DECAY : 0;
                d->last[c] = x;
                out[(done + i) * 2 + c] += x * d->gain * level;
            }
        }
        done += got;
        if (d->target == 0 && d->gain == 0)
            close_deck(d);
    }
}

void music_init(Music *m, MusicLoader load) {
    memset(m, 0, sizeof *m);
    for (int i = 0; i < MUSIC_DECKS; i++)
        m->decks[i] = (MusicDeck){.cue = -1, .then = -1};
    m->load = load;
    m->program = -1;
    m->song_track = -1;
    m->cutoff = 1;
    m->context = (MusicContext){.scene = MUSIC_MENU, .enabled = true};
    m->initialized = true;
}

void music_destroy(Music *m) {
    for (int i = 0; i < MUSIC_DECKS; i++)
        close_deck(&m->decks[i]);
    m->initialized = false;
}

void music_context(Music *m, MusicContext c) {
    if (!m->initialized)
        return;
    if (c.track < 0 || c.track >= MUSIC_TRACKS)
        c.track = 0;
    MusicScene was = m->context.scene;
    // Each new race alternates the environment's two songs; restarting from pause keeps it.
    bool racing = c.scene == MUSIC_COUNTDOWN || c.scene == MUSIC_RACE;
    bool start = c.scene == MUSIC_COUNTDOWN && was != MUSIC_COUNTDOWN && was != MUSIC_PAUSE;
    if (racing && (start || c.track != m->song_track)) {
        m->song = music_track_song(c.track, m->visits[c.track]++);
        m->song_track = c.track;
    }
    // The result is kept for the screen. It only improves, to a celebration, once a
    // championship medal is settled after the rivals finish.
    if (c.scene == MUSIC_RESULTS && (was != MUSIC_RESULTS || c.finish == MUSIC_BEST))
        m->finish = c.finish;
    int want = m->program;
    if (c.scene == MUSIC_MENU)
        want = MUSIC_CUE_MENU;
    else if (c.scene == MUSIC_COUNTDOWN)
        want = -1;
    else if (c.scene == MUSIC_RACE)
        want = (c.final_lap ? MUSIC_CUE_CLIMAX : MUSIC_CUE_RACE) + m->song;
    else if (c.scene == MUSIC_RESULTS)
        want = MUSIC_CUE_FINISH_LOOP + m->finish;
    m->context = c;
    if (want == m->program)
        return;
    int from = m->program;
    m->program = want;
    if (want < 0)
        fade(m, .6f); // The start lights and their beeps have the countdown to themselves.
    else if (want >= MUSIC_CUE_FINISH_LOOP)
        play(m, MUSIC_CUE_FINISH + m->finish, want, .35f, 0);
    else if (want >= MUSIC_CUE_CLIMAX && from == want - MUSIC_SONGS)
        play(m, MUSIC_CUE_FINAL_LAP, want, .25f, 0);
    else if (from < 0)
        play(m, want, -1, .3f, 0); // On GO the song starts on its first downbeat.
    else
        play(m, want, -1, 1.2f, .8f);
}

void music_render(Music *m, float *out, int frames) {
    memset(out, 0, sizeof *out * 2 * (size_t)frames);
    if (!m->initialized)
        return;
    for (int i = 0; i < MUSIC_DECKS; i++)
        mix(m, &m->decks[i], out, frames);
    bool paused = m->context.scene == MUSIC_PAUSE;
    float volume = m->context.enabled ? paused ? PAUSE_LEVEL : 1 : 0;
    // Pause also muffles the music with two one-pole low-pass stages near 700 Hz.
    float cutoff = paused ? .095f : 1;
    for (int i = 0; i < frames; i++) {
        m->volume += (volume - m->volume) * .0003f;
        m->cutoff += (cutoff - m->cutoff) * .0002f;
        if (fabsf(volume - m->volume) < 1e-4f)
            m->volume = volume;
        if (fabsf(cutoff - m->cutoff) < 1e-4f)
            m->cutoff = cutoff;
        for (int c = 0; c < 2; c++) {
            m->low[c][0] += (out[i * 2 + c] - m->low[c][0]) * m->cutoff;
            m->low[c][1] += (m->low[c][0] - m->low[c][1]) * m->cutoff;
            out[i * 2 + c] = m->low[c][1] * m->volume;
        }
    }
}

int music_playing(const Music *m) {
    for (int i = 0; i < MUSIC_DECKS; i++)
        if (m->decks[i].vorbis && m->decks[i].target > 0)
            return m->decks[i].cue;
    return -1;
}
