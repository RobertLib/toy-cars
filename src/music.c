#include "music.h"
#include <math.h>
#include <string.h>

#define TAU 6.2831853071795864769f
enum { KEYS, PLUCK, PAD, BASS, LEAD, KICK, SNARE, HAT, TOM };
enum { HARMONY, LOW, RHYTHM, MELODY, DRIVE };

typedef struct {
    const char *name;
    int tonic, mode, bpm, groove, timbre;
    float swing;
    int chords[2][8];
    /* Two composed eight-bar phrases, four possible entries per bar; -1 is a rest.
       Pitches are scale degrees, so the score and its extended chords share a key. */
    int melody[2][32];
} Song;

static const Song songs[MUSIC_SONGS] = {{"Pocket Paddock",
                                         48,
                                         0,
                                         96,
                                         0,
                                         KEYS,
                                         .10f,
                                         {{0, 5, 1, 4, 2, 5, 3, 4}, {3, 2, 1, 4, 5, 3, 1, 4}},
                                         {{4, -1, 6, 8,  7, 5,  -1, 4, 3, -1, 5,  8, 6, 4,  -1, -1,
                                           4, 6,  9, -1, 7, -1, 5,  4, 5, 3,  -1, 1, 2, -1, 1,  -1},
                                          {5, -1, 7, 9, 8, 6, -1, 4, 3, 5,  8, -1, 6, -1, 4, 2,
                                           7, -1, 9, 7, 5, 3, -1, 2, 3, -1, 5, 3,  2, -1, 1, -1}}},
                                        {"Orchard Run",
                                         50,
                                         0,
                                         116,
                                         0,
                                         PLUCK,
                                         .12f,
                                         {{0, 0, 5, 3, 1, 4, 3, 4}, {5, 2, 3, 0, 1, 5, 3, 4}},
                                         {{2, 4, -1, 7, 6, 4, 2, -1, 7, 5, 4, -1, 5, 3,  2, 0,
                                           3, 5, -1, 8, 6, 4, 2, -1, 5, 7, 5, 3,  4, -1, 2, -1},
                                          {7, 9, 7, 5, 6, -1, 4, 2, 5, 7, 9, -1, 8, 7, 4,  -1,
                                           3, 5, 8, 5, 7, -1, 5, 4, 5, 3, 2, -1, 4, 2, -1, -1}}},
                                        {"Golden Switchback",
                                         45,
                                         2,
                                         122,
                                         1,
                                         KEYS,
                                         .06f,
                                         {{0, 2, 3, 6, 0, 5, 3, 4}, {3, 6, 2, 5, 1, 3, 0, 4}},
                                         {{0, -1, 2, 4,  6, 4, 2,  -1, 5, -1, 7, 5, 6, 3,  -1, 1,
                                           4, 2,  0, -1, 7, 5, -1, 4,  5, 3,  2, 0, 1, -1, 4,  -1},
                                          {5, 7,  -1, 9, 8, 6, 3, -1, 6, 4, -1, 2, 7, 5,  4, -1,
                                           3, -1, 5,  8, 7, 5, 3, -1, 4, 2, 0,  2, 4, -1, 1, -1}}},
                                        {"Saltwater Skylines",
                                         48,
                                         0,
                                         112,
                                         2,
                                         KEYS,
                                         .16f,
                                         {{0, 2, 5, 4, 3, 2, 1, 4}, {3, 4, 2, 5, 1, 4, 0, 0}},
                                         {{4, -1, 7,  6, 4, 2, -1, 6,  7, -1, 5,  4, 6, 4, -1, 2,
                                           5, 7,  -1, 9, 8, 6, 4,  -1, 5, 3,  -1, 1, 2, 4, -1, -1},
                                          {9, 7, -1, 5,  6, -1, 8, 6, 8, 6, 4, -1, 7, 9,  -1, 7,
                                           5, 3, 1,  -1, 4, -1, 6, 8, 7, 4, 2, -1, 0, -1, -1, -1}}},
                                        {"Tidal Circuit",
                                         53,
                                         1,
                                         124,
                                         3,
                                         PLUCK,
                                         .02f,
                                         {{0, 3, 0, 6, 2, 3, 5, 6}, {3, 3, 0, 2, 5, 3, 1, 6}},
                                         {{0, 4, -1, 6,  5, 3,  -1, 2, 4, 2, 0, -1, 3, 6, -1, 8,
                                           6, 4, 2,  -1, 5, -1, 7,  5, 7, 5, 4, -1, 6, 3, 1,  -1},
                                          {5, -1, 7,  9, 8, 7, 5, -1, 7, 4,  2, 0, 6, 4,  -1, 2,
                                           7, 9,  -1, 7, 5, 3, 2, -1, 3, -1, 5, 3, 1, -1, 6,  -1}}},
                                        {"Aurora Apex",
                                         47,
                                         2,
                                         128,
                                         3,
                                         LEAD,
                                         .00f,
                                         {{0, 5, 2, 6, 3, 0, 5, 4}, {3, 5, 0, 6, 2, 3, 1, 4}},
                                         {{4, -1, 2, 0,  5, 7, -1, 9, 8, 6, 4, -1, 6, -1, 3, 1,
                                           5, 7,  5, -1, 4, 2, -1, 0, 7, 5, 4, 2,  1, -1, 4, -1},
                                          {5, 7, 9, -1, 7, -1, 5, 4, 7, 4, 2, -1, 6, 8, -1, 6,
                                           8, 6, 4, 2,  5, -1, 7, 5, 3, 5, 3, -1, 4, 1, -1, -1}}},
                                        {"Glacier Afterglow",
                                         50,
                                         1,
                                         118,
                                         1,
                                         KEYS,
                                         .04f,
                                         {{0, 3, 5, 2, 0, 6, 3, 4}, {5, 3, 0, 6, 1, 3, 2, 4}},
                                         {{2, -1, 4,  6, 7, 5, -1, 3,  7, -1, 5, 4, 6, 4, 2,  -1,
                                           4, 7,  -1, 6, 8, 6, 3,  -1, 5, -1, 3, 2, 4, 1, -1, -1},
                                          {7, 9, -1, 7, 5, -1, 7, 5,  7, 4, 2, -1, 6, 8,  6, -1,
                                           5, 3, -1, 1, 5, 7,  9, -1, 8, 6, 4, 2,  4, -1, 1, -1}}}};

static float limit(float value, float low, float high) {
    return fminf(high, fmaxf(low, value));
}

static float wave(const Music *m, float phase) {
    float index = (phase - floorf(phase)) * 2048.f;
    int i = (int)index;
    // Tiny negative phase modulation can round the fractional part up to 1.
    float a = m->sine[i & 2047], b = m->sine[(i + 1) & 2047];
    return a + (b - a) * (index - i);
}

static int pitch(const Song *s, int degree) {
    static const int scales[3][7] = {
        {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 10}};
    int octave = 0;
    while (degree < 0) {
        degree += 7;
        octave--;
    }
    return s->tonic + 12 * (octave + degree / 7) + scales[s->mode][degree % 7];
}

static void note(Music *m, int instrument, int layer, int midi, float beats, float gain,
                 float pan) {
    MusicVoice *v = NULL;
    for (int i = 0; i < MUSIC_VOICES; i++) {
        if (!m->voices[i].active) {
            v = &m->voices[i];
            break;
        }
    }
    /* The score peaks below 40 voices. Dropping an excess ornament is preferable
       to stealing a sustaining chord and introducing a discontinuity. */
    if (!v)
        return;
    static const float attacks[] = {.006f, .004f, .12f, .006f, .018f, .002f, .002f, .001f, .003f};
    static const float releases[] = {.30f, .20f, .45f, .06f, .16f, .08f, .10f, .025f, .10f};
    static const float decays[] = {2.4f, 5.0f, .12f, 1.9f, 1.1f, 19.f, 18.f, 65.f, 12.f};
    *v = (MusicVoice){.increment = m->frequency[(int)limit((float)midi, 0, 127)] / MUSIC_RATE,
                      .duration = beats * 60.f / songs[m->song].bpm,
                      .attack = attacks[instrument],
                      .release = releases[instrument],
                      .decay = expf(-decays[instrument] / MUSIC_RATE),
                      .envelope = 1,
                      .gain = gain,
                      .left = sqrtf((1 - pan) * .5f),
                      .right = sqrtf((1 + pan) * .5f),
                      .instrument = instrument,
                      .layer = layer,
                      .active = true};
}

static void start_song(Music *m, int song) {
    m->song = song;
    m->pending_song = -1;
    m->tick = 0;
    m->transition = 0;
    m->variation++;
    memset(m->voices, 0, sizeof m->voices);
    memset(m->delay, 0, sizeof m->delay);
    memset(m->delay_low, 0, sizeof m->delay_low);
}

void music_init(Music *m) {
    memset(m, 0, sizeof *m);
    m->noise = 0x6d757369;
    m->pending_song = -1;
    m->context = (MusicContext){.scene = MUSIC_MENU, .enabled = true};
    for (int i = 0; i <= 2048; i++)
        m->sine[i] = sinf(TAU * i / 2048.f);
    for (int i = 0; i < 128; i++)
        m->frequency[i] = 440.f * powf(2.f, (i - 69) / 12.f);
    m->initialized = true;
}

void music_context(Music *m, MusicContext context) {
    context.track = (int)limit((float)context.track, 0, 2);
    context.intensity = limit(context.intensity, 0, 1);
    bool racing = context.scene == MUSIC_RACE || context.scene == MUSIC_COUNTDOWN;
    bool was_racing = m->context.scene == MUSIC_RACE || m->context.scene == MUSIC_COUNTDOWN ||
                      m->context.scene == MUSIC_PAUSE;
    bool restart = context.scene == MUSIC_COUNTDOWN && m->context.scene != MUSIC_COUNTDOWN &&
                   m->context.scene != MUSIC_PAUSE;
    if (racing && (!was_racing || restart || context.track != m->context.track)) {
        int variant = (int)(m->visits[context.track]++ % 2);
        m->pending_song = 1 + context.track * 2 + variant;
    } else if (context.scene == MUSIC_MENU && m->context.scene != MUSIC_MENU) {
        m->pending_song = m->song == 0 ? -1 : 0;
    }
    m->context = context;
}

static void sequence(Music *m) {
    int step = (int)(m->tick % 16);
    int bar = (int)(m->tick / 16);
    if (step == 0) {
        // A request arriving just before the downbeat waits one more bar if the
        // outgoing music has not faded yet. Never cut live voices at full gain.
        if (m->pending_song >= 0 && (m->tick == 0 || m->transition < .003f)) {
            start_song(m, m->pending_song);
            bar = 0;
        } else if (bar == 64) {
            if (m->pending_song < 0 && m->song > 0 && m->context.scene != MUSIC_MENU &&
                m->context.scene != MUSIC_RESULTS)
                start_song(m, 1 + ((m->song - 1) / 2) * 2 + (m->song % 2));
            else {
                m->tick = 0;
                m->variation++;
            }
            bar = 0;
        }
        m->arranged_scene = m->context.scene;
    }
    const Song *s = &songs[m->song];
    /* 64 bars: opening, theme, answer, lift, development, breathing space and return.
       Four-bar sections keep short races moving; eight-bar phrases give melodies room. */
    static const int form[16] = {0, 1, 1, 2, 2, 3, 3, 4, 1, 1, 2, 2, 3, 3, 1, 0};
    int section = form[bar / 4];
    int section_start = bar / 4;
    while (section_start > 0 && form[section_start - 1] == section)
        section_start--;
    int phrase_bar = (bar - section_start * 4) % 8;
    bool quiet = section == 0 || section == 4;
    bool results = m->arranged_scene == MUSIC_RESULTS;
    int phrase = (section == 2 || section == 3) ? 1 : 0;
    int chord = results ? (bar % 4 == 3 ? 4 : 0) : s->chords[phrase][phrase_bar];
    float beat_seconds = 60.f / s->bpm;

    if (step == 0) {
        for (int n = 0; n < 4; n++) {
            int degree = chord + 2 + n * 2;
            while (degree > 13)
                degree -= 7;
            note(m, PAD, HARMONY, pitch(s, degree) + 12, 3.7f, .026f, -.65f + n * .43f);
        }
        if (section == 3 && bar % 4 == 0 && !results)
            note(m, HAT, DRIVE, 80, .7f, .035f, .4f);
    }

    /* Syncopated bass lines use the root, fifth and a diatonic approach to the next chord. */
    static const unsigned bass_masks[4] = {0x2949, 0x4545, 0x4949, 0x5555};
    unsigned bass_mask = quiet || results ? 0x0101 : bass_masks[s->groove];
    if (bass_mask & (1u << step)) {
        int degree = chord;
        if (step == 6 || step == 10)
            degree += 4;
        if (step >= 14)
            degree = s->chords[phrase][(phrase_bar + 1) % 8] - 1;
        note(m, BASS, LOW, pitch(s, degree) - 12, step >= 12 ? .35f : .65f, .095f, 0);
    }

    /* Offbeat keyboard/guitar comping leaves room for the bass and the lead. */
    static const unsigned comp_masks[4] = {0x0440, 0x0404, 0x2424, 0x4444};
    if ((comp_masks[s->groove] & (1u << step)) && !quiet && !results) {
        for (int n = 0; n < 3; n++)
            note(m, s->groove == 0 ? PLUCK : KEYS, HARMONY, pitch(s, chord + n * 2 + 2) + 12, .42f,
                 .025f, -.35f + n * .28f);
    }

    /* Composed calls and answers, with deliberate rests. No continuous lead ostinato. */
    static const int onsets[4][4] = {{0, 3, 8, 12}, {0, 6, 10, 14}, {2, 6, 8, 14}, {0, 3, 7, 10}};
    if (!quiet && !results && m->arranged_scene != MUSIC_COUNTDOWN) {
        for (int n = 0; n < 4; n++) {
            if (step != onsets[s->groove][n])
                continue;
            int degree = s->melody[phrase][phrase_bar * 4 + n];
            if (degree < 0 || (section == 1 && bar % 4 == 3 && n > 0))
                continue;
            // Alternate returns have a different register and a shortened answer.
            if ((m->variation & 1u) && section == 2 && n == 3)
                continue;
            int octave = section == 2 && (m->variation & 1u) ? 0 : 12;
            note(m, s->timbre, MELODY, pitch(s, degree) + octave, n == 3 ? .85f : .55f, .055f,
                 -.16f);
        }
    }

    /* A sparse answering arpeggio is an intensity layer, not a second competing lead. */
    if (section == 3 && (step == 2 || step == 6 || step == 14) && !results) {
        int degree = chord + (step == 2 ? 4 : step == 6 ? 6 : 2);
        note(m, PLUCK, DRIVE, pitch(s, degree) + 12, .35f, .029f, .48f);
    }
    if ((quiet || results) && step == 6 && bar % 2 == 0)
        note(m, KEYS, MELODY, pitch(s, chord + 4) + 12, 1.5f, .032f, .25f);

    if (!results) {
        static const unsigned kicks[4] = {0x0141, 0x0501, 0x0901, 0x1111};
        unsigned kick_mask = quiet ? 0x0001 : kicks[s->groove];
        if (kick_mask & (1u << step))
            note(m, KICK, RHYTHM, 36, .22f / beat_seconds, quiet ? .10f : .15f, 0);
        if (!quiet && (step == 4 || step == 12))
            note(m, SNARE, RHYTHM, 50, .14f / beat_seconds, .075f, -.1f);
        if ((!quiet && step % 2 == 0) || (quiet && step == 10))
            note(m, HAT, RHYTHM, 80, .035f / beat_seconds, step % 4 == 0 ? .021f : .034f, .38f);
        if (!quiet && step % 2 == 1)
            note(m, HAT, DRIVE, 80, .025f / beat_seconds, .013f, -.45f);
        // Fills only at phrase boundaries; two alternating endings rather than every bar.
        if (!quiet && phrase_bar == 7 && step >= 13)
            note(m, (bar / 8 + m->variation) % 2 ? TOM : SNARE, RHYTHM, 48 - (step - 13) * 3,
                 .13f / beat_seconds, .048f, (step - 14) * .35f);
    }
}

void music_sample(Music *m, float *left, float *right) {
    if (!m->initialized) {
        *left = *right = 0;
        return;
    }
    if (m->until_tick <= 0) {
        sequence(m);
        const Song *s = &songs[m->song];
        float swing = m->tick % 2 == 0 ? 1 + s->swing : 1 - s->swing;
        m->until_tick += MUSIC_RATE * 60.0 / s->bpm / 4 * swing;
        m->tick++;
    }
    m->until_tick--;

    m->intensity += (m->context.intensity - m->intensity) * .000025f;
    float energy = m->intensity;
    bool subdued = m->context.scene == MUSIC_MENU || m->context.scene == MUSIC_PAUSE ||
                   m->context.scene == MUSIC_RESULTS;
    float target[5] = {.78f, .75f, .55f + .35f * energy, .72f, energy * .8f};
    if (subdued) {
        target[RHYTHM] = m->context.scene == MUSIC_MENU ? .45f : .12f;
        target[DRIVE] = 0;
        target[MELODY] = .5f;
    }
    if (m->context.scene == MUSIC_COUNTDOWN) {
        target[MELODY] = .12f;
        target[DRIVE] = .15f;
    }
    for (int i = 0; i < 5; i++)
        m->layers[i] += (target[i] - m->layers[i]) * .00012f;
    float volume = m->context.enabled ? (m->context.scene == MUSIC_PAUSE ? .42f : .82f) : 0;
    m->volume += (volume - m->volume) * .0003f;
    /* Fade across the last beat before a bar-aligned change. The ending also
       prepares an automatic move to the companion track after 64 bars. */
    unsigned last_step = (m->tick - 1) % 16;
    bool ending = m->tick > 63 * 16 && m->song > 0 && m->context.scene != MUSIC_RESULTS &&
                  m->context.scene != MUSIC_MENU;
    float transition = (m->pending_song >= 0 || ending) && last_step >= 12 ? 0 : 1;
    m->transition += (transition - m->transition) * (transition == 0 ? .0004f : .00012f);

    m->noise = m->noise * 1664525u + 1013904223u;
    float noise = (m->noise >> 9) / 4194304.f - 1;
    m->noise_low += (noise - m->noise_low) * .22f;
    float l = 0, r = 0, send_l = 0, send_r = 0;
    for (int i = 0; i < MUSIC_VOICES; i++) {
        MusicVoice *v = &m->voices[i];
        if (!v->active)
            continue;
        v->age += 1.f / MUSIC_RATE;
        if (v->age >= v->duration + v->release) {
            v->active = false;
            continue;
        }
        v->envelope *= v->decay;
        float envelope = fminf(1, v->age / v->attack) * v->envelope;
        if (v->age > v->duration) {
            float release = 1 - (v->age - v->duration) / v->release;
            envelope *= release * release;
        }
        float p = v->phase, value = 0;
        switch (v->instrument) {
        case KEYS:
            value = wave(m, p + .16f * v->envelope * wave(m, p * 2)) * .8f +
                    wave(m, p * 3) * .12f * v->envelope;
            break;
        case PLUCK:
            value = wave(m, p) * .72f + wave(m, p * 2) * .2f * v->envelope +
                    wave(m, p * 3) * .08f * v->envelope;
            break;
        case PAD:
            value = wave(m, p) * .7f + wave(m, p + v->age * .32f) * .22f + wave(m, p * 3) * .06f;
            break;
        case BASS:
            value = wave(m, p) * .83f + wave(m, p * 2) * .14f;
            break;
        case LEAD:
            value = wave(m, p + .002f * wave(m, v->age * 4.8f)) * .8f +
                    wave(m, p * 3) * .13f * v->envelope;
            break;
        case KICK:
            value = wave(m, p);
            break;
        case SNARE:
            value = m->noise_low * 1.5f + wave(m, p) * .25f;
            break;
        case HAT:
            value = (noise - m->noise_low) * .65f;
            break;
        case TOM:
            value = wave(m, p) * .85f + m->noise_low * .12f;
            break;
        }
        v->phase += v->instrument == KICK ? (45 + 95 * v->envelope) / MUSIC_RATE : v->increment;
        v->phase -= floorf(v->phase);
        value *= envelope * v->gain * m->layers[v->layer];
        l += value * v->left;
        r += value * v->right;
        if (v->instrument <= PAD || v->instrument == LEAD) {
            send_l += value * v->left * .22f;
            send_r += value * v->right * .22f;
        }
    }
    // Dark, cross-fed dotted-eighth echo adds space without washing out the engine cues.
    int delay_frames = (int)(MUSIC_RATE * 60.f / songs[m->song].bpm * .75f);
    int read = (m->delay_pos + MUSIC_DELAY - delay_frames) % MUSIC_DELAY;
    m->delay_low[0] += (m->delay[read][0] - m->delay_low[0]) * .12f;
    m->delay_low[1] += (m->delay[read][1] - m->delay_low[1]) * .12f;
    m->delay[m->delay_pos][0] = send_l + m->delay_low[1] * .28f;
    m->delay[m->delay_pos][1] = send_r + m->delay_low[0] * .28f;
    m->delay_pos = (m->delay_pos + 1) % MUSIC_DELAY;
    float gain = m->volume * m->transition;
    *left = (l + m->delay_low[0]) * gain;
    *right = (r + m->delay_low[1]) * gain;
}

const char *music_song_name(int song) {
    return song >= 0 && song < MUSIC_SONGS ? songs[song].name : "";
}
