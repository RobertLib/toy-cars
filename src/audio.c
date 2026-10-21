#include "audio.h"
#include <string.h>

#define AUDIO_RATE MUSIC_RATE
#define AUDIO_FRAMES (AUDIO_RATE / 20)
#define IDLE_RPM 900.f
#define REDLINE 7000.f
#define SHIFT_RPM 6500.f
#define SOUND_SPEED 343.f
/* Engine speed maps to playback pitch with this exponent, so the recorded
   layers cover idle to redline without sounding sped up. */
#define PITCH_RANGE .7f
#define LIMIT .89f
// Offsets every effect level below; the one knob for effects against the music.
#define MIX_TRIM -2.f

/* Recorded engine layers and the engine speed of each recording, from its
   firing frequency (two firings per revolution of the four-cylinder). */
static const struct {
    int sound;
    float rpm;
} LAYERS[4] = {{SFX_ENGINE_IDLE, 834},
               {SFX_ENGINE_LOW, 1490},
               {SFX_ENGINE_MID, 2020},
               {SFX_ENGINE_HIGH, 2550}};
// Road speed in m/s at which each of the four gears reaches 6800 rpm.
static const float GEAR_TOP[4] = {12.5f, 20.5f, 28.5f, 38};

static float ramp(float x, float a, float b) {
    return clampf((x - a) / (b - a), 0, 1);
}
// Linear gain that plays a normalized sound at this loudness.
static float lufs(float target) {
    return powf(10, (target + MIX_TRIM - SFX_LOUDNESS) / 20);
}
static float approach(float value, float target, float seconds, float dt) {
    return value + (target - value) * (1 - expf(-dt / fmaxf(seconds, 1e-4f)));
}

static void engine_reset(CarAudio *e, int index) {
    e->rpm = IDLE_RPM;
    e->load = e->throttle = 0;
    e->gear = e->pops = 0;
    e->shift = 0;
    e->running = true;
    // Rivals' engines sit a little apart in pitch; the player's plays as recorded.
    e->detune = index ? 1 + (index - 4) * .012f : 1;
}

void audio_init(Audio *a, bool open_device) {
    memset(a, 0, sizeof *a);
    music_init(&a->music, asset_read);
    sfx_init(&a->sfx, asset_read);
    a->limiter = 1;
    a->noise = 84131;
    a->screen = (Screen)-1;
    for (int i = 0; i < CAR_COUNT; i++)
        engine_reset(&a->cars[i], i);
    if (!open_device)
        return;
    SDL_AudioSpec spec = {.format = SDL_AUDIO_F32, .channels = 2, .freq = AUDIO_RATE};
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
    const Car *player = &g->cars[0];
    MusicFinish finish = MUSIC_BAD;
    if (g->championship_celebration || (!player->dnf && g->finish_rank == 1))
        finish = MUSIC_BEST;
    else if (!player->dnf && g->finish_rank > 0 && g->finish_rank <= 4)
        finish = MUSIC_GOOD;
    return (MusicContext){scene, g->selected, finish, player->lap >= RACE_LAPS, g->profile.music};
}

typedef struct {
    float gain, pan, pitch, cutoff;
} Place;

/* Distance, direction and Doppler shift of a source heard from the camera.
   Full level within reference meters, silent beyond range. */
static Place place(const Audio *a, const Listener *l, Vec3 pos, Vec3 velocity, float reference,
                   float range) {
    Vec3 d = vsub(pos, l->eye), f = vsub(l->target, l->eye);
    f.y = 0;
    f = vlen(f) > 1e-4f ? vnorm(f) : v3(0, 0, 1);
    Vec3 right = vnorm(vcross(f, v3(0, 1, 0)));
    float distance = vlen(d);
    Place p = {reference / fmaxf(reference, distance), 0, 1, 1};
    p.gain *= ramp(range - distance, 0, range * .25f);
    if (distance > .01f) {
        p.pan = vdot(d, right) / distance * .85f;
        // Sources behind the camera and far away lose some brightness.
        float behind = fminf(0, vdot(d, f) / distance);
        float hz = 18000 * (1 + behind * .55f) * fminf(1, 40 / distance);
        p.cutoff = sfx_cutoff(fmaxf(hz, 1500));
        float closing = -vdot(vsub(velocity, a->eye_velocity), d) / distance;
        p.pitch = SOUND_SPEED / (SOUND_SPEED - clampf(closing, -70, 70));
    }
    return p;
}

enum { ENGINE_UPSHIFT = 1, ENGINE_LIFT = 2 };

/* Four-speed gearbox and engine speed. The tires set the speed in gear; a slipping clutch
   at launch, wheelspin, free revving in the air and the grid add to it. */
static int engine_tick(CarAudio *e, const Car *c, float throttle, bool grid, float dt) {
    bool running = c->fuel > 0 && !c->dnf;
    if (!running) {
        e->rpm = approach(e->rpm, 0, .45f, dt);
        e->load = approach(e->load, 0, .1f, dt);
        e->running = false;
        return 0;
    }
    int events = 0;
    // Lifting off a revving engine pops the exhaust.
    if (e->throttle > .6f && throttle < .15f && e->rpm > 4300)
        events |= ENGINE_LIFT;
    e->throttle = throttle;
    if (!e->running) {
        e->running = true;
        e->rpm = IDLE_RPM;
    }
    float speed = hypotf(c->velocity.x, c->velocity.z);
    bool free = grid || !c->grounded || c->tumbling;
    e->shift = fmaxf(0, e->shift - dt);
    if (grid) {
        e->gear = 0;
    } else if (e->shift <= 0 && !free) {
        float rpm = speed * 6800 / GEAR_TOP[e->gear];
        if (e->gear < 3 && rpm > SHIFT_RPM) {
            e->gear++;
            e->shift = .13f;
            events |= ENGINE_UPSHIFT;
        } else if (e->gear > 0 && rpm < (throttle > .3f ? 3100 : 2300)) {
            e->gear--;
            e->shift = .09f;
        }
    }
    float wheel = speed * 6800 / GEAR_TOP[e->gear], target;
    if (free) {
        target = fmaxf(grid ? 0 : wheel * .8f, IDLE_RPM + throttle * (REDLINE * .96f - IDLE_RPM));
    } else {
        float clutch = e->gear == 0 ? throttle * 2400 * (1 - ramp(speed, 3, 8)) : 0;
        float spin = throttle * (ramp(c->slip, 2, 8) * 900 + ramp(c->verge, .3f, 1.5f) * 350);
        target = fmaxf(wheel, IDLE_RPM + clutch) + spin;
    }
    float load = throttle;
    if (e->shift > 0) {
        // The throttle closes while the next gear engages.
        load = 0;
        target = wheel;
    }
    target = clampf(target, IDLE_RPM, REDLINE);
    float seconds = e->shift > 0      ? .05f
                    : target > e->rpm ? (free ? .16f : .07f)
                                      : (free ? .35f : .15f);
    e->rpm = approach(e->rpm, target, seconds, dt);
    e->load = approach(e->load, load, load > e->load ? .05f : .12f, dt);
    return events;
}

/* Crossfades the two recorded layers around the engine speed. */
static void engine_voice(Audio *a, CarAudio *e, float gain, float pan, float pitch, float cutoff) {
    float sound = LAYERS[0].rpm * powf(fmaxf(e->rpm, 1) / IDLE_RPM, PITCH_RANGE);
    float weights[4] = {0};
    if (sound <= LAYERS[0].rpm) {
        weights[0] = 1;
    } else if (sound >= LAYERS[3].rpm) {
        weights[3] = 1;
    } else {
        int k = 0;
        while (sound > LAYERS[k + 1].rpm)
            k++;
        float t = logf(sound / LAYERS[k].rpm) / logf(LAYERS[k + 1].rpm / LAYERS[k].rpm);
        weights[k] = cosf(t * 1.5707963f);
        weights[k + 1] = sinf(t * 1.5707963f);
    }
    // A stalled engine fades out as it winds down.
    gain *= ramp(e->rpm, IDLE_RPM * .25f, IDLE_RPM * .8f);
    for (int k = 0; k < 4; k++)
        sfx_hold(&a->sfx, &e->layers[k], LAYERS[k].sound, gain * weights[k],
                 sound / LAYERS[k].rpm * pitch * e->detune, pan, cutoff);
}

static float engine_gain(const CarAudio *e) {
    float n = ramp(e->rpm, IDLE_RPM, REDLINE);
    return (.42f + .58f * e->load) * (.7f + .45f * n);
}
static float engine_cutoff(const CarAudio *e) {
    // Off the throttle the exhaust note turns darker, on it opens fully.
    float n = ramp(e->rpm, IDLE_RPM, REDLINE);
    return sfx_cutoff(2200 + 5000 * n + 16000 * e->load);
}

static int surface_sound(int track) {
    return track == 2 ? SFX_SNOW : track == 1 ? SFX_SAND : SFX_DIRT;
}
static int ambience_sound(const Game *g) {
    if (g->selected == 2)
        return SFX_AMBIENCE_WINTER;
    if (g->selected == 1)
        return SFX_AMBIENCE_BEACH;
    return game_night(g) ? SFX_AMBIENCE_NIGHT : SFX_AMBIENCE_COUNTRY;
}

static void play_at(Audio *a, const Listener *l, int sound, float gain, float pitch, Vec3 pos,
                    float reference) {
    Place p = place(a, l, pos, v3(0, 0, 0), reference, 90);
    sfx_play(&a->sfx, sound, gain * p.gain, pitch, p.pan);
}

static float jitter(Audio *a) {
    a->noise = a->noise * 1664525u + 1013904223u;
    return ((a->noise >> 9) / 8388607.f) * 2 - 1;
}

static void cue(Audio *a, const Game *g, const Listener *l, const SoundEvent *e) {
    bool player = e->car == 0;
    float pitch = 1 + jitter(a) * .04f;
    switch (e->kind) {
    case SOUND_COUNTDOWN:
        sfx_play(&a->sfx, SFX_BEEP, lufs(-24), 1, 0);
        break;
    case SOUND_GO:
        sfx_play(&a->sfx, SFX_GO, lufs(-23), 1, 0);
        break;
    case SOUND_FUEL:
        if (player)
            sfx_play(&a->sfx, SFX_FUEL, lufs(-24), 1, 0);
        break;
    case SOUND_FINAL_LAP:
        // With music on, the soundtrack's Final Lap jingle announces it.
        if (player && !g->profile.music)
            sfx_play(&a->sfx, SFX_BELL, lufs(-25), 1, 0);
        break;
    case SOUND_TAKEOFF:
        if (player)
            sfx_play(&a->sfx, SFX_TAKEOFF, lufs(-35 + 6 * ramp(e->strength, 9, 24)), pitch, 0);
        break;
    case SOUND_LANDING:
    case SOUND_CAR_HIT:
    case SOUND_RAIL_HIT: {
        // Settling over a crest is not a landing worth a thud.
        if (e->car < 0 || e->car >= CAR_COUNT || a->cars[e->car].quiet > 0 ||
            (e->kind == SOUND_LANDING && e->strength < 2.2f))
            break;
        a->cars[e->car].quiet = .12f;
        float hard = ramp(e->strength, e->kind == SOUND_LANDING ? 2 : 1, 10);
        int sound = e->kind == SOUND_LANDING ? SFX_LANDING : SFX_CAR_HIT;
        if (e->kind == SOUND_RAIL_HIT)
            pitch *= .8f;
        else if (e->kind == SOUND_LANDING)
            pitch *= 1.06f - .12f * hard;
        play_at(a, l, sound, lufs(-38 + 12 * hard), pitch, e->pos, player ? 12 : 6);
        break;
    }
    case SOUND_OUT_OF_FUEL:
        if (player)
            sfx_play(&a->sfx, SFX_SPUTTER, lufs(-26), 1, 0);
        break;
    case SOUND_FINISH:
        sfx_play(&a->sfx, SFX_CHEER, lufs(-31), 1, 0);
        break;
    case SOUND_CELEBRATION:
        sfx_play(&a->sfx, SFX_ROAR, lufs(-27), 1, 0);
        break;
    case SOUND_BOOST:
        if (player)
            sfx_play(&a->sfx, SFX_BOOST, lufs(-26), pitch, 0);
        else
            play_at(a, l, SFX_BOOST, lufs(-31), pitch, e->pos, 6);
        break;
    case SOUND_RECOVER:
        if (player)
            sfx_play(&a->sfx, SFX_RECOVER, lufs(-28), 1, 0);
        break;
    case SOUND_CONE_HIT:
    case SOUND_TIRE_HIT:
    case SOUND_FENCE_HIT: {
        if (a->quiet[e->kind] > 0)
            break;
        a->quiet[e->kind] = .07f;
        int sound = e->kind == SOUND_CONE_HIT   ? SFX_CONE
                    : e->kind == SOUND_TIRE_HIT ? SFX_TIRE
                                                : SFX_FENCE;
        float hard = ramp(e->strength, 1.2f, 9);
        play_at(a, l, sound, lufs(-37 + 9 * hard), pitch, e->pos, 7);
        break;
    }
    }
}

static bool menu_screen(Screen s) {
    return s == SCREEN_MENU || s == SCREEN_GARAGE || s == SCREEN_HELP || s == SCREEN_SETTINGS ||
           s == SCREEN_RECORDS || s == SCREEN_RESET_DATA;
}

/* Menus sound a click for every change the player makes, whatever the input. */
static void interface(Audio *a, const Game *g) {
    const Profile *p = &g->profile;
    bool settings[4] = {p->auto_accel, p->touch, p->sound, p->music};
    bool known = a->screen != (Screen)-1;
    int sound = -1;
    if (known && g->screen != a->screen) {
        bool race_flow = g->screen == SCREEN_COUNTDOWN || g->screen == SCREEN_RESULTS ||
                         (a->screen == SCREEN_COUNTDOWN && g->screen == SCREEN_RACE);
        if (!race_flow)
            sound = SFX_UI_SELECT;
    } else if (known && (menu_screen(g->screen) || g->screen == SCREEN_RESULTS)) {
        if (g->selected != a->selected || (int)g->mode != a->mode || p->color != a->color ||
            p->arcade_night != a->night || p->difficulty != a->difficulty ||
            memcmp(settings, a->settings, sizeof settings) || g->record_page != a->record_page ||
            g->record_history != a->record_history || g->initials_cursor != a->initials_cursor ||
            memcmp(g->initials, a->initials, 4))
            sound = SFX_UI_MOVE;
    }
    if (sound >= 0 && p->sound)
        sfx_play(&a->sfx, sound, lufs(sound == SFX_UI_SELECT ? -26 : -28), 1, 0);
    a->screen = g->screen;
    a->selected = g->selected;
    a->mode = (int)g->mode;
    a->color = p->color;
    a->night = p->arcade_night;
    a->difficulty = p->difficulty;
    memcpy(a->settings, settings, sizeof settings);
    a->record_page = g->record_page;
    a->record_history = g->record_history;
    a->initials_cursor = g->initials_cursor;
    memcpy(a->initials, g->initials, 4);
}

void audio_direct(Audio *a, Game *g, const Listener *l, float dt) {
    dt = clampf(dt, 0, .1f);
    a->time += dt;
    music_context(&a->music, soundtrack_context(g));
    Screen was = a->screen;
    interface(a, g);
    bool grid = g->screen == SCREEN_COUNTDOWN;
    bool racing = grid || g->screen == SCREEN_RACE || g->screen == SCREEN_RESULTS;
    // A new race starts every engine at idle; resuming from pause keeps them.
    if ((grid && was != SCREEN_COUNTDOWN && was != SCREEN_PAUSE) ||
        (racing && (menu_screen(was) || was == (Screen)-1)))
        for (int i = 0; i < CAR_COUNT; i++)
            engine_reset(&a->cars[i], i);
    // The camera's own motion feeds the Doppler shift; a cut is not motion.
    Vec3 step = vsub(l->eye, a->eye);
    Vec3 velocity = a->eye_known && dt > 0 && vlen(step) < 4 ? vmul(step, 1 / dt) : v3(0, 0, 0);
    a->eye_velocity = vlerp(a->eye_velocity, velocity, 1 - expf(-dt / .1f));
    a->eye = l->eye;
    a->eye_known = true;
    for (int i = 0; i < SOUND_FENCE_HIT + 1; i++)
        a->quiet[i] = fmaxf(0, a->quiet[i] - dt);

    bool on = g->profile.sound;
    // Pause, menus and the sound switch fade the race out over a moment.
    a->race = approach(a->race, on && racing ? g->screen == SCREEN_RESULTS ? .7f : 1 : 0, .12f, dt);
    if (a->race < .002f && !(on && racing))
        a->race = 0;
    float race = a->race, bed = race > 0 ? fminf(1, race / .7f) : 0;
    if (on && racing)
        for (int i = 0; i < g->sound_count; i++)
            cue(a, g, l, &g->sounds[i]);
    g->sound_count = 0;

    for (int i = 0; i < CAR_COUNT; i++) {
        const Car *c = &g->cars[i];
        CarAudio *e = &a->cars[i];
        e->quiet = fmaxf(0, e->quiet - dt);
        float throttle = c->throttle;
        if (grid && i > 0) {
            // Rivals blip their throttles on the grid, each at its own pace.
            float blip = sinf(a->time * (1.1f + i * .37f) + i * 2.1f);
            throttle = blip > .8f ? (blip - .8f) * 3.5f : 0;
        }
        int events = racing ? engine_tick(e, c, throttle, grid, dt) : 0;
        if (events & ENGINE_LIFT)
            e->pops = 1 + (int)(fabsf(jitter(a)) * 2.5f);
        else if (events & ENGINE_UPSHIFT && jitter(a) > .4f)
            e->pops = 1;
        e->pop_time -= dt;
        bool active = racing && !c->dnf;
        float speed = hypotf(c->velocity.x, c->velocity.z);
        float grip = c->grounded && !c->tumbling ? 1 : 0;
        float off = ramp(c->verge, .15f, .9f);
        float skid = grip * ramp(c->slip, 2.2f, 7) * ramp(speed, 3, 8);
        bool entered = c->wet && !e->wet && speed > 3;
        e->wet = c->wet;
        if (i == 0) {
            float gain = active ? race : 0;
            engine_voice(a, e, gain * lufs(-28.3f) * engine_gain(e), 0, 1, engine_cutoff(e));
            sfx_hold(&a->sfx, &e->skid, SFX_SKID, gain * lufs(-29) * skid * (1 - off * .7f),
                     .92f + .16f * ramp(c->slip, 3, 12), 0, 1);
            sfx_hold(&a->sfx, &e->surface, surface_sound(g->selected),
                     gain * lufs(-29) * grip * off * powf(ramp(speed, .5f, 16), .7f),
                     .8f + speed / 45, 0, 1);
            sfx_hold(&a->sfx, &e->wade, SFX_WADE,
                     gain * lufs(-29) * (c->wet ? 1 : 0) * ramp(speed, .5f, 12), .85f + speed / 40,
                     0, 1);
            float rush = powf(ramp(speed, 8, 40), 2) * .8f + (c->grounded ? 0 : .35f);
            sfx_hold(&a->sfx, &a->wind, SFX_WIND, gain * lufs(-36) * rush, .85f + speed / 90, 0, 1);
            if (entered && gain > 0)
                sfx_play(&a->sfx, SFX_SPLASH, gain * lufs(-27) * ramp(speed, 3, 14), 1, 0);
            if (events & ENGINE_UPSHIFT && gain > 0)
                sfx_play(&a->sfx, SFX_GEAR, gain * lufs(-34), 1 + jitter(a) * .05f, 0);
            if (e->pops > 0 && e->pop_time <= 0) {
                if (gain > 0)
                    sfx_play(&a->sfx, SFX_BACKFIRE, gain * lufs(-30 + jitter(a) * 2),
                             1 + jitter(a) * .1f, 0);
                e->pops--;
                e->pop_time = .07f + fabsf(jitter(a)) * .08f;
            }
        } else {
            Place p = place(a, l, c->pos, c->velocity, 6, 75);
            float gain = active ? race * p.gain : 0;
            engine_voice(a, e, gain * lufs(-33) * engine_gain(e), p.pan, p.pitch,
                         fminf(p.cutoff, engine_cutoff(e)));
            sfx_hold(&a->sfx, &e->skid, SFX_SKID, gain * lufs(-34) * skid * (1 - off * .7f),
                     (.92f + .16f * ramp(c->slip, 3, 12)) * p.pitch, p.pan, p.cutoff);
            sfx_hold(&a->sfx, &e->surface, surface_sound(g->selected),
                     gain * lufs(-36) * grip * off * powf(ramp(speed, .5f, 16), .7f),
                     (.8f + speed / 45) * p.pitch, p.pan, p.cutoff);
            sfx_hold(&a->sfx, &e->wade, SFX_WADE,
                     gain * lufs(-36) * (c->wet ? 1 : 0) * ramp(speed, .5f, 12),
                     (.85f + speed / 40) * p.pitch, p.pan, p.cutoff);
            if (entered && gain > .05f)
                sfx_play(&a->sfx, SFX_SPLASH, gain * lufs(-31) * ramp(speed, 3, 14), 1, p.pan);
            if (e->pops > 0 && e->pop_time <= 0) {
                if (gain > .05f)
                    sfx_play(&a->sfx, SFX_BACKFIRE, gain * lufs(-33 + jitter(a) * 2),
                             (1 + jitter(a) * .1f) * p.pitch, p.pan);
                e->pops--;
                e->pop_time = .07f + fabsf(jitter(a)) * .08f;
            }
        }
    }

    // Habitat, weather and the camera helicopter surround the race.
    sfx_hold(&a->sfx, &a->ambience, ambience_sound(g), bed * lufs(-38), 1, 0, 1);
    const Weather *w = &g->weather;
    int weather = w->kind == WEATHER_RAIN ? SFX_RAIN : SFX_GUSTS;
    float storm = w->kind == WEATHER_CLEAR ? 0 : w->intensity * (w->kind == WEATHER_SNOW ? .5f : 1);
    sfx_hold(&a->sfx, &a->weather, weather, bed * lufs(w->kind == WEATHER_RAIN ? -34 : -35) * storm,
             1, 0, 1);
    Place copter = place(a, l, l->helicopter, v3(0, 0, 0), 25, 160);
    sfx_hold(&a->sfx, &a->helicopter, SFX_HELICOPTER,
             l->helicopter_visible ? bed * lufs(-36) * copter.gain : 0, copter.pitch, copter.pan,
             copter.cutoff);
}

void audio_render(Audio *a, float *data, int frames) {
    music_render(&a->music, data, frames);
    sfx_render(&a->sfx, data, frames);
    // A peak limiter with instant attack and a 150 ms release keeps the mix clean.
    float release = 1 - expf(-1.f / (.15f * AUDIO_RATE));
    for (int i = 0; i < frames; i++) {
        float peak = fmaxf(fabsf(data[i * 2]), fabsf(data[i * 2 + 1]));
        if (peak * a->limiter > LIMIT)
            a->limiter = LIMIT / peak;
        else
            a->limiter += (1 - a->limiter) * release;
        data[i * 2] = clampf(data[i * 2] * a->limiter, -1, 1);
        data[i * 2 + 1] = clampf(data[i * 2 + 1] * a->limiter, -1, 1);
    }
}

void audio_update(Audio *a, Game *g, const Listener *listener, float dt) {
    if (!a->stream || a->paused) {
        g->sound_count = 0;
        return;
    }
    audio_direct(a, g, listener, dt);
    int queued = SDL_GetAudioStreamQueued(a->stream);
    int frames = AUDIO_FRAMES - queued / (int)(sizeof(float) * 2);
    if (frames <= 0)
        return;
    if (frames > AUDIO_FRAMES)
        frames = AUDIO_FRAMES;
    float data[AUDIO_FRAMES * 2];
    audio_render(a, data, frames);
    SDL_PutAudioStreamData(a->stream, data, frames * 2 * (int)sizeof(float));
}

void audio_destroy(Audio *a) {
    SDL_DestroyAudioStream(a->stream);
    music_destroy(&a->music);
    sfx_destroy(&a->sfx);
    a->stream = NULL;
}
