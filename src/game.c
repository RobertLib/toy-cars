#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NO_SURFACE_HEIGHT -100.f
#define ROAD_EDGE_TOLERANCE .5f
#define SHORTCUT_MIN_GAIN 8.f
#define SHORTCUT_GAIN_RATIO .20f
#define SHORTCUT_GRACE_TIME 1.f
#define SHORTCUT_MAJOR_GAIN 24.f

const Color CAR_COLORS[6] = {{.94, .28, .10, 1}, {.12, .57, .60, 1}, {.94, .69, .16, 1},
                             {.40, .45, .76, 1}, {.86, .46, .59, 1}, {.82, .87, .77, 1}};
const char *CAR_COLOR_NAMES[6] = {"TANGERINE", "LAGOON", "HONEY", "BLUEBERRY", "GUAVA", "VANILLA"};
static float random01(Game *g) {
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return (g->rng & 0xffff) / 65535.f;
}
static float wrap(float s, float l) {
    s = fmodf(s, l);
    return s < 0 ? s + l : s;
}
static float weather_random(Weather *w) {
    w->rng ^= w->rng << 13;
    w->rng ^= w->rng >> 17;
    w->rng ^= w->rng << 5;
    return (w->rng & 0xffff) / 65535.f;
}
static void weather_start(Game *g) {
    // Keep weather independent of driving/dust randomness and vary each retry.
    uint32_t seed = g->weather.rng;
    if (!seed)
        seed = g->test_mode ? 92741u : (uint32_t)SDL_GetPerformanceCounter();
    g->weather = (Weather){.rng = seed ? seed : 92741u};
    g->weather.remaining = 12 + 18 * weather_random(&g->weather);
}
static void weather_tick(Game *g, float dt) {
    Weather *w = &g->weather;
    w->time += dt;
    w->remaining -= dt;
    if (w->remaining <= 0) {
        if (w->kind == WEATHER_CLEAR) {
            w->kind = g->selected == 2 ? WEATHER_SNOW : WEATHER_RAIN;
            if (weather_random(w) < .35f)
                w->kind = WEATHER_WIND;
            w->duration = 22 + 12 * weather_random(w);
            w->strength = .65f + .35f * weather_random(w);
            w->direction = 2 * PI * weather_random(w);
        } else {
            w->kind = WEATHER_CLEAR;
            w->duration = 35 + 25 * weather_random(w);
        }
        w->remaining = w->duration;
    }
    // Five-second envelopes reach zero before the next clear spell.
    float fade = clampf(fminf(w->duration - w->remaining, w->remaining) / 5, 0, 1);
    w->intensity = w->kind == WEATHER_CLEAR ? 0 : w->strength * fade * fade * (3 - 2 * fade);
    float gust = .7f + .2f * sinf(w->time * .83f) + .1f * sinf(w->time * 2.1f);
    float speed = (w->kind == WEATHER_WIND ? 12 : 5) * w->intensity * gust;
    w->wind = v3(cosf(w->direction) * speed, 0, sinf(w->direction) * speed);
    w->drift = vadd(w->drift, vmul(w->wind, dt));
}
Vec3 track_at(const Track *t, float distance, float lane, Vec3 *direction) {
    float s = wrap(distance, t->length);
    int lo = 0, hi = t->count - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (t->points[mid].s <= s)
            lo = mid;
        else
            hi = mid - 1;
    }
    const TrackPoint *a = &t->points[lo], *b = &t->points[(lo + 1) % t->count];
    float end = lo == t->count - 1 ? t->length : b->s;
    float f = clampf((s - a->s) / (end - a->s), 0, 1);
    Vec3 dir = vnorm(vsub(b->p, a->p)), p = vlerp(a->p, b->p, f);
    if (direction)
        *direction = dir;
    p.x += dir.z * lane;
    p.z -= dir.x * lane;
    return p;
}

float track_nearest(const Track *t, Vec3 p, int *index, Vec3 *center, float *lane) {
    float best = 1e20f, bf = 0;
    int bi = 0;
    int from = *index < 0 ? 0 : *index - 32, count = *index < 0 ? t->count : 65;
    for (int k = 0; k < count; k++) {
        int i = (from + k + t->count) % t->count;
        Vec3 a = t->points[i].p, b = t->points[(i + 1) % t->count].p;
        Vec3 d = vsub(b, a), q = vsub(p, a);
        float den = d.x * d.x + d.z * d.z;
        float f = den > .0001f ? clampf((q.x * d.x + q.z * d.z) / den, 0, 1) : 0;
        Vec3 c = vlerp(a, b, f);
        float dx = p.x - c.x, dz = p.z - c.z;
        float dist = dx * dx + dz * dz;
        if (dist < best) {
            best = dist;
            bi = i;
            bf = f;
        }
    }
    *index = bi;
    const TrackPoint *a = &t->points[bi], *b = &t->points[(bi + 1) % t->count];
    Vec3 c = vlerp(a->p, b->p, bf), dir = vnorm(vsub(b->p, a->p));
    if (center)
        *center = c;
    if (lane)
        *lane = (p.x - c.x) * dir.z - (p.z - c.z) * dir.x;
    return lerpf(a->s, bi == t->count - 1 ? t->length : b->s, bf);
}

// Water and ground use separate surfaces: the car still rides on the ford bed.
// This is the same triangle mesh used by the liquid shader, indexed by XZ cell.
bool track_water(const Track *t, Vec3 p, float *height) {
    int cell = surface_cell(p.z) * SURFACE_GRID + surface_cell(p.x);
    bool found = false;
    float y = -10000;
    for (uint32_t i = t->water_cells[cell]; i < t->water_cells[cell + 1]; i++) {
        SurfaceTriangle f = t->water[t->water_refs[i]];
        Vec3 ab = vsub(f.b, f.a), ac = vsub(f.c, f.a), q = vsub(p, f.a);
        float det = ab.x * ac.z - ab.z * ac.x;
        if (fabsf(det) < 1e-6f) continue;
        float u = (q.x * ac.z - q.z * ac.x) / det;
        float v = (ab.x * q.z - ab.z * q.x) / det;
        if (u >= 0 && v >= 0 && u + v <= 1) {
            y = fmaxf(y, f.a.y + u * ab.y + v * ac.y);
            found = true;
        }
    }
    if (height) *height = y;
    return found;
}

float track_height(const Track *t, Vec3 p, bool *on_ramp) {
    float y = NO_SURFACE_HEIGHT;
    int cell = surface_cell(p.z) * SURFACE_GRID + surface_cell(p.x);
    for (uint32_t i = t->surface_cells[cell]; i < t->surface_cells[cell + 1]; i++) {
        SurfaceTriangle f = t->surface[t->surface_refs[i]];
        Vec3 ab = vsub(f.b, f.a), ac = vsub(f.c, f.a), q = vsub(p, f.a);
        float det = ab.x * ac.z - ab.z * ac.x;
        if (fabsf(det) < 1e-6f)
            continue;
        float u = (q.x * ac.z - q.z * ac.x) / det;
        float v = (ab.x * q.z - ab.z * q.x) / det;
        if (u >= -.00001f && v >= -.00001f && u + v <= 1.00001f)
            y = fmaxf(y, f.a.y + u * ab.y + v * ac.y);
    }
    if (!on_ramp)
        return y;
    *on_ramp = false;
    for (int j = 0; j < t->ramp_count; j++) {
        Ramp r = t->ramps[j];
        const TrackPoint *rp = &t->points[(int)r.index];
        Vec3 q = vsub(p, rp->p);
        float forward = q.x * rp->dx + q.z * rp->dz, side = q.x * rp->dz - q.z * rp->dx;
        if (fabsf(side) < r.half && forward >= -r.length * .5f && forward <= r.length * .5f) {
            *on_ramp = true;
        }
    }
    return y;
}

void car_surface_pose(const Track *t, Vec3 *pos, float yaw, float *pitch, float *roll) {
    Vec3 forward = v3(sinf(yaw), 0, cosf(yaw)), right = v3(forward.z, 0, -forward.x);
    float heights[4];
    for (int i = 0; i < 4; i++) {
        Vec3 wheel = vadd(
            *pos, vadd(vmul(right, i % 2 ? .85f : -.85f), vmul(forward, i / 2 ? .94f : -.95f)));
        heights[i] = track_height(t, wheel, NULL);
        // An axle can straddle the island edge before the chassis leaves it.
        if (heights[i] == NO_SURFACE_HEIGHT)
            heights[i] = track_height(t, *pos, NULL);
        // While the chassis is on a ramp, keep following its deck through the
        // lip. The front axle leaving the lip must not pitch the nose down.
        for (int j = 0; j < t->ramp_count; j++) {
            Ramp r = t->ramps[j];
            TrackPoint rp = t->points[(int)r.index];
            Vec3 q = vsub(*pos, rp.p);
            float along = q.x * rp.dx + q.z * rp.dz, side = q.x * rp.dz - q.z * rp.dx;
            if (fabsf(side) < r.half && fabsf(along) <= r.length * .5f) {
                q = vsub(wheel, rp.p);
                float deck =
                    rp.p.y + (q.x * rp.dx + q.z * rp.dz + r.length * .5f) / r.length * r.height;
                heights[i] = fmaxf(heights[i], deck);
            }
        }
    }
    *pitch = -atan2f((heights[2] + heights[3] - heights[0] - heights[1]) * .5f, 1.89f);
    *roll = atan2f((heights[1] + heights[3] - heights[0] - heights[2]) * .5f * cosf(*pitch), 1.7f);
    pos->y = (heights[0] + heights[1] + heights[2] + heights[3]) * .25f;
}

bool game_track_unlocked(const Profile *p, int track) {
    return track >= 0 && track < TRACK_COUNT && track <= p->championship_completed;
}

void game_set_mode(Game *g, RaceMode mode) {
    g->mode = mode;
    if (mode == MODE_CHAMPIONSHIP)
        g->selected = !g->profile.championship_eliminated &&
                              g->profile.championship_stages < TRACK_COUNT
                          ? g->profile.championship_stages : 0;
}

const char *game_player_name(const Game *g) {
    return g->profile.initials_set ? g->profile.initials : "YOU";
}

bool game_reset_profile(Game *g) {
    if (!profile_reset(&g->profile))
        return false;
    game_set_mode(g, MODE_CHAMPIONSHIP);
    g->unlocked_track = -1;
    g->championship_advanced = false;
    g->championship_scored = false;
    g->championship_celebration = false;
    g->celebration_time = 0;
    g->result_id = 0;
    g->previous_best = 0;
    g->new_record = g->initials_pending = g->record_save_failed = false;
    g->save_failure_acknowledged = false;
    SDL_strlcpy(g->initials, g->profile.initials, sizeof g->initials);
    g->cars[0].name = game_player_name(g);
    g->initials_cursor = g->record_page = 0;
    g->record_history = false;
    g->records_return = SCREEN_MENU;
    g->toast_time = 0;
    g->screen = SCREEN_SETTINGS;
    return true;
}

static int championship_place(const Game *g, int stage, int driver) {
    if (stage < g->profile.championship_stages)
        return g->profile.championship_places[stage][driver];
    if (stage != g->selected || g->championship_scored ||
        (g->screen != SCREEN_RACE && g->screen != SCREEN_RESULTS && g->screen != SCREEN_PAUSE))
        return 0;
    if (g->cars[driver].dnf)
        return 0;
    int rank = 1;
    for (int i = 0; i < CAR_COUNT; i++)
        if (i != driver && game_car_precedes(g, i, driver))
            rank++;
    return rank;
}

static int championship_points_for_place(int place) {
    const int points[] = {0, 10, 8, 6, 5, 4, 3, 2, 1};
    return place >= 0 && place <= CAR_COUNT ? points[place] : 0;
}

int game_championship_points(const Game *g, int driver) {
    int points = 0;
    for (int stage = 0; stage < TRACK_COUNT; stage++)
        points += championship_points_for_place(championship_place(g, stage, driver));
    return points;
}

bool game_championship_precedes(const Game *g, int a, int b) {
    int pa = game_championship_points(g, a), pb = game_championship_points(g, b);
    if (pa != pb)
        return pa > pb;
    // Count back wins, then second places, etc.; exact ties keep driver order.
    for (int place = 1; place <= CAR_COUNT; place++) {
        int ca = 0, cb = 0;
        for (int stage = 0; stage < TRACK_COUNT; stage++) {
            ca += championship_place(g, stage, a) == place;
            cb += championship_place(g, stage, b) == place;
        }
        if (ca != cb)
            return ca > cb;
    }
    return a < b;
}

int game_championship_rank(const Game *g) {
    int rank = 1;
    for (int i = 1; i < CAR_COUNT; i++)
        rank += game_championship_precedes(g, i, 0);
    return rank;
}

bool game_championship_finished(const Game *g) {
    return g->mode == MODE_CHAMPIONSHIP && g->championship_scored &&
           (g->profile.championship_eliminated || g->profile.championship_stages == TRACK_COUNT);
}

int game_next_track(const Game *g) {
    if (g->mode == MODE_CHAMPIONSHIP) {
        int next = g->selected + 1;
        return g->championship_scored && !g->profile.championship_eliminated &&
                       next < TRACK_COUNT ? next : -1;
    }
    for (int step = 1; step < TRACK_COUNT; step++) {
        int next = (g->selected + step) % TRACK_COUNT;
        if (game_track_unlocked(&g->profile, next))
            return next;
    }
    return -1;
}

void game_start(Game *g) {
    if (game_save_warning(g))
        return;
    if (g->selected < 0 || g->selected >= TRACK_COUNT ||
        (!g->test_mode && !game_track_unlocked(&g->profile, g->selected)))
        return;
    if (g->mode == MODE_CHAMPIONSHIP && !g->test_mode) {
        if (g->profile.championship_eliminated || g->profile.championship_stages == TRACK_COUNT) {
            g->profile.championship_stages = 0;
            g->profile.championship_eliminated = false;
            memset(g->profile.championship_places, 0, sizeof g->profile.championship_places);
        }
        g->selected = g->profile.championship_stages;
    }
    g->screen = SCREEN_COUNTDOWN;
    g->result_id = 0;
    g->initials_pending = false;
    g->new_record = false;
    g->unlocked_track = -1;
    g->championship_advanced = false;
    g->championship_scored = false;
    g->championship_celebration = false;
    g->celebration_time = 0;
    g->time = 0;
    g->start_idle_time = 0;
    g->start_hint_dismissed = false;
    g->countdown = 3.f;
    g->rank = 8;
    g->finish_rank = 0;
    g->toast_time = 0;
    g->wrong_way = 0;
    g->shake = 0;
    g->last_lap = 0;
    memset(g->particles, 0, sizeof g->particles);
    g->particle_cursor = 0;
    g->rng = 81291 + g->selected;
    weather_start(g);
    const char *names[] = {game_player_name(g), "JUNO", "DASH", "PIP", "NOVA", "ACE", "RUSTY", "BEAN"};
    Track *t = &g->tracks[g->selected];
    for (int i = 0; i < CAR_COUNT; i++) {
        Car *c = &g->cars[i];
        memset(c, 0, sizeof *c);
        int grid = i == 0 ? 7 : i - 1;
        c->progress = -5 - (grid / 2) * 4.2f;
        float lane = grid % 2 ? 2.0f : -2.0f;
        Vec3 d;
        c->pos = track_at(t, c->progress, lane, &d);
        c->yaw = atan2f(d.x, d.z);
        car_surface_pose(t, &c->pos, c->yaw, &c->pitch, &c->roll);
        c->prev = c->pos;
        c->prev_yaw = c->yaw;
        c->grounded = true;
        c->fuel = 100;
        c->nearest = -1;
        c->last_s = track_nearest(t, c->pos, &c->nearest, NULL, NULL);
        c->route_pos = c->pos;
        c->lap = 1;
        c->target_lane = (i % 3 - 1) * 2.6f;
        c->color = i == 6   ? rgb(.24f, .30f, .33f)
                   : i == 7 ? rgb(.43f, .65f, .80f)
                            : CAR_COLORS[i == 0 ? g->profile.color : (i + g->profile.color) % 6];
        c->name = names[i];
    }
    props_reset(g);
    g->sound_event = 1;
}

void game_reset_car(Game *g, int index) {
    Car *c = &g->cars[index];
    if (c->finished)
        return;
    Track *t = &g->tracks[g->selected];
    Vec3 d;
    c->pos = track_at(t, c->progress, 0, &d);
    c->yaw = atan2f(d.x, d.z);
    car_surface_pose(t, &c->pos, c->yaw, &c->pitch, &c->roll);
    c->prev = c->pos;
    c->prev_yaw = c->yaw;
    c->velocity = v3(0, 0, 0);
    c->speed = 0;
    c->vy = 0;
    c->grounded = true;
    c->stuck_time = 0;
    c->offroad = false;
    c->offroad_delta = 0;
    c->offroad_distance = 0;
    c->offroad_time = 0;
    c->nearest = -1;
    c->last_s = track_nearest(t, c->pos, &c->nearest, NULL, NULL);
    c->route_pos = c->pos;
    if (index == 0) {
        SDL_strlcpy(g->toast, "BACK ON TRACK", sizeof g->toast);
        g->toast_time = 1.5f;
    }
}

// Maximum lateral acceleration in game meters/s^2, shared by tires and AI.
static float corner_grip(int track) {
    return track == 2 ? 20.f : track == 1 ? 24.f : 28.f;
}

void game_ai(const Game *g, int index, Controls *out) {
    const Track *t = &g->tracks[g->selected];
    const Car *c = &g->cars[index];
    *out = (Controls){0};
    float lane = c->finished ? 3.f : c->target_lane * .35f;
    float look = 5.f + c->speed * .50f;
    // Plan a fuel line early. Each driver has an independent respawn timer.
    float near = 1000;
    for (int j = 0; !c->finished && j < t->can_count; j++) {
        float d = wrap(t->points[(int)t->cans[j].index].s - c->last_s, t->length);
        float remaining = t->length * RACE_LAPS - c->progress;
        if (d < near && d < 60 && c->can_cooldown[j] <= 0 &&
            c->fuel < fminf(40, remaining * .12f + 5)) {
            near = d;
            lane = t->cans[j].lane;
        }
    }
    // At critical fuel, commit to the pickup line for the full approach;
    // overtaking must not undo the fuel plan until the last 30 metres on ice.
    if (!c->finished && near > (c->fuel < 22 ? 60 : 30)) {
        for (int j = 0; j < CAR_COUNT; j++)
            if (j != index) {
                const Car *other = &g->cars[j];
                float d = wrap(other->last_s - c->last_s, t->length);
                if (other->dnf || d < .5f || d > 16 ||
                    vlen(vsub(c->pos, other->pos)) > 18)
                    continue;
                int other_nearest = other->nearest;
                float other_lane;
                track_nearest(t, other->pos, &other_nearest, NULL, &other_lane);
                // Pass on the side the leading car leaves open, including
                // lapped traffic. Do not move into an occupied adjacent lane.
                float passing_lane = other_lane > 0 ? -2.8f : 2.8f;
                bool clear = true;
                for (int k = 0; k < CAR_COUNT; k++) {
                    const Car *adjacent = &g->cars[k];
                    if (k == index || k == j || adjacent->dnf)
                        continue;
                    float gap = wrap(adjacent->last_s - c->last_s + t->length * .5f, t->length) -
                                t->length * .5f;
                    if (gap < -5 || gap > 16)
                        continue;
                    int n = adjacent->nearest;
                    float adjacent_lane;
                    track_nearest(t, adjacent->pos, &n, NULL, &adjacent_lane);
                    if (fabsf(adjacent_lane - passing_lane) < 2.2f)
                        clear = false;
                }
                if (clear)
                    lane = passing_lane;
            }
    }
    // Aim at the can itself on approach instead of cutting across its lane
    // beyond the pickup. Keep the same fuel decisions for every driver.
    if (near < look)
        look = fmaxf(2, near);
    Vec3 target = track_at(t, c->last_s + look, lane, NULL);
    float angle = angle_delta(atan2f(target.x - c->pos.x, target.z - c->pos.z), c->yaw);
    if (c->speed > 3)
        angle += angle_delta(c->yaw, atan2f(c->velocity.x, c->velocity.z)) * .85f;
    out->steer = clampf(-angle * 3.6f, -1, 1);

    // Sample the whole braking horizon: maintain speed on straights, then
    // brake only as far ahead as needed to reach a corner's target speed.
    int difficulty = g->profile.difficulty;
    float pace = difficulty == 0 ? .74f : difficulty == 2 ? 1.08f : 1.f;
    pace *= 1.f - (index % 4) * .008f;
    float desired = 31.f * pace;
    for (float ahead = 0; ahead <= 48; ahead += 6) {
        Vec3 d0, d1;
        track_at(t, c->last_s + ahead, 0, &d0);
        track_at(t, c->last_s + ahead + 6, 0, &d1);
        float curvature = fabsf(angle_delta(atan2f(d1.x, d1.z), atan2f(d0.x, d0.z))) / 6;
        // Leave grip available for line corrections and overtaking.
        float corner_speed = sqrtf(corner_grip(g->selected) * .72f / fmaxf(curvature, .001f)) * pace;
        desired = fminf(desired, sqrtf(corner_speed * corner_speed + 2 * 16.f * ahead));
    }
    // Steering authority is much lower in the air. A jump into a sharp bend
    // needs a slower takeoff so the car can land on the road, especially on ice.
    for (int j = 0; j < t->ramp_count; j++) {
        const Ramp *ramp = &t->ramps[j];
        float ramp_s = t->points[(int)ramp->index].s;
        float ahead = wrap(ramp_s - c->last_s, t->length);
        if (ahead > 38)
            continue;
        Vec3 takeoff, landing;
        track_at(t, ramp_s, 0, &takeoff);
        track_at(t, ramp_s + 40, 0, &landing);
        float turn = fabsf(angle_delta(atan2f(landing.x, landing.z), atan2f(takeoff.x, takeoff.z)));
        float jump_speed = clampf(38 - turn * 12, 20, 33) * fminf(pace, 1);
        float braking_distance = fmaxf(0, ahead - ramp->length * .5f);
        desired = fminf(desired, sqrtf(jump_speed * jump_speed + 2 * 16.f * braking_distance));
    }
    // Leave enough steering authority to reach an edge pickup without
    // sliding past it when the aiming distance becomes short.
    if (near < 60) {
        // Keep extra steering reserve at critical fuel levels so traffic
        // cannot turn a missed edge pickup into an unrecoverable empty tank.
        float pickup_speed = c->fuel < 22 ? 18.f : 22.f;
        desired = fminf(desired, sqrtf(pickup_speed * pickup_speed +
                                      2 * 16.f * fmaxf(0, near - 12)));
    }
    if (c->finished) {
        // Clear the finish at a relaxed pace on the right, keeping a gap to
        // traffic ahead. The regular handling model supplies the deceleration.
        desired = fminf(desired, 10.f + (index % 3) * .4f);
        for (int j = 0; j < CAR_COUNT; j++) {
            if (j == index || g->cars[j].dnf)
                continue;
            const Car *other = &g->cars[j];
            float gap = wrap(other->last_s - c->last_s, t->length);
            int n = other->nearest;
            float other_lane;
            track_nearest(t, other->pos, &n, NULL, &other_lane);
            if (gap > 0 && gap < 20 && fabsf(other_lane - lane) < 2.4f &&
                vlen(vsub(c->pos, other->pos)) < 22)
                desired = fminf(desired, fmaxf(0, other->speed + (gap - 7) * .8f));
        }
    }
    out->throttle = clampf(.6f + (desired - c->speed) * .5f, 0, 1);
    out->brake = clampf((c->speed - desired - .6f) * .25f, 0, 1);
    if (c->finished)
        out->brake = fminf(out->brake, .22f);
}

static void particle(Game *g, Vec3 p, Vec3 vel, float life, float size, Color color) {
    Particle *q = &g->particles[g->particle_cursor++ % MAX_PARTICLES];
    *q = (Particle){p, vel, life, life, size, color, PARTICLE_DUST};
}
static bool wheel_spray(Game *g, const Car *c, float dt) {
    if (!c->grounded) return false;
    const Track *t = &g->tracks[g->selected];
    Vec3 forward = v3(sinf(c->yaw), 0, cosf(c->yaw));
    Vec3 right = v3(forward.z, 0, -forward.x);
    float speed = hypotf(c->velocity.x, c->velocity.z);
    bool wet = false;
    for (int i = 0; i < 4; i++) {
        float side = i % 2 ? 1 : -1;
        Vec3 wheel = vadd(c->pos, vadd(vmul(right, side * .85f),
                                     vmul(forward, i / 2 ? .94f : -.95f)));
        float level;
        if (!track_water(t, wheel, &level)) continue;
        float ground = track_height(t, wheel, NULL);
        if (level < ground + .025f || c->pos.y > level + .45f) continue;
        wet = true;
        if (speed < 1 || random01(g) >= dt * fminf(65, speed * 3)) continue;
        wheel.y = level + .045f;
        float spread = (1.3f + speed * .12f) * (.6f + random01(g));
        Vec3 vel = vadd(vmul(c->velocity, .22f), vmul(right, side * spread));
        vel.y = 1.0f + random01(g) * (1.2f + speed * .10f);
        particle(g, wheel, vel, .38f + random01(g) * .32f,
                 .07f + random01(g) * .09f, rgb(.72f, .88f, .89f));
        g->particles[(g->particle_cursor - 1) % MAX_PARTICLES].kind = PARTICLE_SPRAY;
        if (random01(g) < .12f) {
            particle(g, wheel, v3(0,0,0), .85f, .4f, rgb(.73f,.89f,.86f));
            g->particles[(g->particle_cursor - 1) % MAX_PARTICLES].kind = PARTICLE_RIPPLE;
        }
    }
    return wet;
}

static void toast(Game *g, const char *s, float seconds) {
    SDL_strlcpy(g->toast, s, sizeof g->toast);
    g->toast_time = seconds;
}

static void update_lap(Game *g, int index, float previous_progress, float dt) {
    Car *car = &g->cars[index];
    const Track *track = &g->tracks[g->selected];

    // Completed checkpoints and laps never go backwards. Crossing the same
    // finish line again must not restart the timer or award another best lap.
    while (car->checkpoints < RACE_LAPS * 4 &&
           car->progress >= (car->checkpoints + 1) * track->length * .25f) {
        car->checkpoints++;
    }

    int lap = car->checkpoints / 4 + 1;
    if (lap <= car->lap)
        return;

    float crossing = (lap - 1) * track->length;
    float fraction =
        clampf((crossing - previous_progress) / (car->progress - previous_progress), 0, 1);
    float crossing_time = g->time - dt + fraction * dt;
    car->lap = lap;
    car->lap_time = crossing_time - car->lap_start;
    car->lap_start = crossing_time;
    if (car->best_lap == 0 || car->lap_time < car->best_lap) {
        car->best_lap = car->lap_time;
    }
    if (index == 0 && lap <= RACE_LAPS) {
        g->last_lap = car->lap_time;
        toast(g, "FINAL LAP", 2);
        g->sound_event = 3;
    }
}

static void car_handling(Car *c, int track, Controls in, float off, float dt) {
    float grip = track == 2 ? 5.8f : track == 1 ? 7.4f : 9.2f;
    float lateral_limit = corner_grip(track);
    if (in.drift) {
        grip *= .43f;
        lateral_limit *= .55f;
        // Apply drift braking after every input source has been combined.
        in.brake = fmaxf(in.brake, .12f);
    }
    Vec3 forward = v3(sinf(c->yaw), 0, cosf(c->yaw)), right = v3(forward.z, 0, -forward.x);
    float longitudinal = vdot(c->velocity, forward), side = vdot(c->velocity, right);
    float throttle = c->fuel > 0 ? in.throttle : 0;
    float steer_rate = (.38f + clampf(fabsf(longitudinal), 0, 23) * .060f) * (in.drift ? 1.3f : 1);
    float yaw_rate = in.steer * steer_rate * clampf(longitudinal * .35f, -1, 1);
    float scrub = 0;
    if (c->grounded) {
        // a = v * yaw_rate: at speed, the front tires cannot hold a tight
        // turning radius. Braking restores steering as the car slows down.
        float demand = fabsf(yaw_rate) * fabsf(longitudinal);
        float steering_limit = corner_grip(track) * (in.drift ? 1.3f : 1);
        yaw_rate *= fminf(1, steering_limit / fmaxf(demand, .001f));
        scrub = fmaxf(0, demand - steering_limit) * .18f;
    }
    bool reversing = in.reverse && in.brake > 0 && throttle == 0;
    if (longitudinal < 0 || (longitudinal == 0 && reversing)) {
        // Resistance opposes backward motion. Releasing BRAKE lets manual
        // drivers coast to a stop; automatic acceleration drives forward again.
        float reverse_power = reversing && c->fuel > 0 ? in.brake * 8.f : 0;
        float resistance = .70f + longitudinal * longitudinal * .012f -
                           off * longitudinal * .40f + scrub +
                           (reversing ? 0 : in.brake * 24.f);
        float accel = throttle * (off > .5f ? 9.f : 14.2f) - reverse_power;
        if (!c->grounded) {
            resistance = .8f;
            accel = 0;
        }
        longitudinal = fminf(0, longitudinal + resistance * dt);
        longitudinal = fmaxf(-7.f, longitudinal + accel * dt);
    } else {
        float accel = throttle * (off > .5f ? 9.f : 14.2f) - in.brake * 24.f - .70f -
                      longitudinal * longitudinal * .012f - off * longitudinal * .40f - scrub;
        if (!c->grounded)
            accel = -.8f;
        // Reach a complete stop before the next step can engage reverse.
        longitudinal = fmaxf(0, longitudinal + accel * dt);
    }
    float contact = c->grounded ? 1 : .15f;
    float lateral_change = side * (1 - expf(-grip * dt * contact));
    side -= clampf(lateral_change, -lateral_limit * dt * contact, lateral_limit * dt * contact);
    // With +Z forward and +Y up, positive yaw turns left in the chase camera.
    c->yaw -= yaw_rate * dt * (c->grounded ? 1 : .23f);
    c->velocity = vadd(vmul(forward, longitudinal), vmul(right, side));
    c->speed = vlen(c->velocity);
}

static void update_car(Game *g, int index, Controls in, float dt) {
    Track *t = &g->tracks[g->selected];
    Car *c = &g->cars[index];
    c->prev = c->pos;
    c->prev_yaw = c->yaw;
    if (c->dnf)
        return;
    if (c->finished)
        game_ai(g, index, &in);
    c->ramp_cooldown = fmaxf(0, c->ramp_cooldown - dt);
    for (int j = 0; j < t->can_count; j++)
        c->can_cooldown[j] = fmaxf(0, c->can_cooldown[j] - dt);
    if (in.reset) {
        game_reset_car(g, index);
        return;
    }
    // Use physical road distance for drag, independently of race progress.
    // Off-road shortcuts can leave the cached segment's search neighborhood.
    int nearest = c->nearest;
    Vec3 center;
    track_nearest(t, c->pos, &nearest, &center, NULL);
    float distance = hypotf(c->pos.x - center.x, c->pos.z - center.z);
    if (distance > t->points[nearest].half + ROAD_EDGE_TOLERANCE) {
        nearest = -1;
        track_nearest(t, c->pos, &nearest, &center, NULL);
        distance = hypotf(c->pos.x - center.x, c->pos.z - center.z);
    }
    // Terrain slows the car without pulling it toward the road or making
    // distant ground progressively harder to drive across.
    float off = clampf(distance - t->points[nearest].half, 0, 4);
    float forward_speed = c->velocity.x * sinf(c->yaw) + c->velocity.z * cosf(c->yaw);
    float throttle = c->fuel > 0
                         ? fmaxf(in.throttle, in.reverse && forward_speed <= 0 ? in.brake : 0)
                         : 0;
    car_handling(c, g->selected, in, off, dt);
    c->pos = vadd(c->pos, vmul(c->velocity, dt));
    bool was_ramp = false, is_ramp = false;
    float old_ground = track_height(t, c->prev, &was_ramp);
    float ground = track_height(t, c->pos, &is_ramp);
    if (ground == NO_SURFACE_HEIGHT) {
        game_reset_car(g, index);
        return;
    }
    // Only crossing the forward lip earns an upward impulse. Driving off a
    // side or back edge starts a fall, including diagonal exits near a corner.
    if (c->grounded && was_ramp && !is_ramp) {
        float takeoff_speed = 0;
        for (int j = 0; j < t->ramp_count; j++) {
            Ramp ramp = t->ramps[j];
            TrackPoint rp = t->points[(int)ramp.index];
            Vec3 before = vsub(c->prev, rp.p), after = vsub(c->pos, rp.p);
            float a = before.x * rp.dx + before.z * rp.dz;
            float b = after.x * rp.dx + after.z * rp.dz;
            float side_a = before.x * rp.dz - before.z * rp.dx;
            float side_b = after.x * rp.dz - after.z * rp.dx;
            float lip = ramp.length * .5f;
            if (a < -lip || a > lip || fabsf(side_a) >= ramp.half || b <= lip || b <= a)
                continue;
            float side_at_lip = lerpf(side_a, side_b, (lip - a) / (b - a));
            if (fabsf(side_at_lip) < ramp.half)
                takeoff_speed = c->velocity.x * rp.dx + c->velocity.z * rp.dz;
        }
        c->grounded = false;
        c->vy = 0;
        c->pos.y = old_ground;
        c->air_time = 0;
        if (takeoff_speed > 9 && c->ramp_cooldown <= 0) {
            c->vy = takeoff_speed * .265f + 1.3f;
            if (!c->finished)
                c->jumps++;
            c->ramp_cooldown = 1.5f;
            if (index == 0 && !c->finished) {
                toast(g, "TAKE FLIGHT!", 1.2f);
                g->sound_event = 4;
            }
        }
    }
    if (!c->grounded) {
        c->air_time += dt;
        c->vy -= 20 * dt;
        c->pos.y += c->vy * dt;
        c->pitch = lerpf(c->pitch, -atan2f(c->vy, fmaxf(8, c->speed)), dt * 4);
        if (c->pos.y <= ground && c->vy < 0) {
            c->pos.y = ground;
            c->grounded = true;
            c->vy = 0;
            c->velocity = vmul(c->velocity, .97f);
            c->speed = vlen(c->velocity);
            for (int j = 0; j < 10; j++)
                particle(g, c->pos,
                         v3((random01(g) - .5f) * 4, 1 + random01(g) * 2, (random01(g) - .5f) * 4),
                         .55f, .3f, t->sky);
            if (index == 0 && !c->finished) {
                g->shake = .23f;
                g->sound_event = 5;
            }
        }
    }
    if (c->grounded)
        car_surface_pose(t, &c->pos, c->yaw, &c->pitch, &c->roll);
    else
        c->roll = lerpf(c->roll, 0, dt * 8);
    // Freeze race progress at the last legal position throughout an excursion.
    // Search the whole road off-track so a shortcut onto a distant segment
    // cannot be hidden by the cached projection's limited neighborhood.
    nearest = c->offroad ? -1 : c->nearest;
    float s = track_nearest(t, c->pos, &nearest, &center, NULL);
    distance = hypotf(c->pos.x - center.x, c->pos.z - center.z);
    if (distance > t->points[nearest].half + ROAD_EDGE_TOLERANCE && !c->offroad) {
        nearest = -1;
        s = track_nearest(t, c->pos, &nearest, &center, NULL);
        distance = hypotf(c->pos.x - center.x, c->pos.z - center.z);
    }
    bool on_road = distance <= t->points[nearest].half + ROAD_EDGE_TOLERANCE;
    c->nearest = nearest;
    if (c->finished) {
        // Continue driving while preserving every recorded race statistic.
        c->last_s = s;
        c->route_pos = c->pos;
        return;
    }
    float delta = s - c->last_s;
    if (delta > t->length * .5f)
        delta -= t->length;
    if (delta < -t->length * .5f)
        delta += t->length;
    float previous_progress = c->progress;
    c->last_s = s;
    float traveled = hypotf(c->pos.x - c->route_pos.x, c->pos.z - c->route_pos.z);
    c->route_pos = c->pos;
    if (!on_road || c->offroad) {
        c->offroad = true;
        c->offroad_delta += delta;
        c->offroad_distance += traveled;
        c->offroad_time += dt;
        if (on_road) {
            // Advancing alongside the road is not a shortcut. Penalize only
            // an appreciably shorter path: both an absolute and relative gain
            // leave room for ordinary shoulder excursions and shallow cuts.
            float saved = c->offroad_delta - c->offroad_distance;
            // A quick correction in a tight bend can appear to save distance.
            // Allow one second to rejoin, unless the cut skips a major distance.
            bool grace = c->offroad_time <= SHORTCUT_GRACE_TIME && saved <= SHORTCUT_MAJOR_GAIN;
            if (!grace && saved > SHORTCUT_MIN_GAIN &&
                saved > c->offroad_delta * SHORTCUT_GAIN_RATIO) {
                game_reset_car(g, index);
                if (index == 0)
                    toast(g, "SHORTCUT - BACK TO TRACK", 2);
                return;
            }
            // Legitimate excursions keep their progress; spent time and fuel
            // are never refunded.
            c->progress += c->offroad_delta;
            c->offroad = false;
            c->offroad_delta = 0;
            c->offroad_distance = 0;
            c->offroad_time = 0;
        }
    } else if (fabsf(delta) < 12) {
        c->progress += delta;
    }
    if (on_road)
        update_lap(g, index, previous_progress, dt);
    if (c->progress >= t->length * RACE_LAPS && c->checkpoints >= RACE_LAPS * 4) {
        c->finished = true;
        c->finish_time = c->lap_start;
        return;
    }
    c->fuel = fmaxf(0, c->fuel - (.80f + throttle * .65f + c->speed * .045f) * dt);
    for (int j = 0; j < t->can_count; j++)
        if (c->can_cooldown[j] <= 0) {
            const Can *can = &t->cans[j];
            Vec3 cp = track_at(t, t->points[(int)can->index].s, can->lane, NULL);
            float dx = c->pos.x - cp.x, dz = c->pos.z - cp.z;
            if (dx * dx + dz * dz < FUEL_PICKUP_RADIUS * FUEL_PICKUP_RADIUS &&
                fabsf(c->pos.y - cp.y) < 2.8f) {
                c->can_cooldown[j] = FUEL_PICKUP_COOLDOWN;
                c->fuel = fminf(100, c->fuel + FUEL_PICKUP_AMOUNT);
                c->collected++;
                if (index == 0) {
                    char message[32];
                    SDL_snprintf(message, sizeof message, "+%d FUEL", FUEL_PICKUP_AMOUNT);
                    toast(g, message, 1.3f);
                    g->sound_event = 2;
                    for (int k = 0; k < 20; k++)
                        particle(g, vadd(cp, v3(0, 1, 0)),
                                 v3((random01(g) - .5f) * 5, 2 + random01(g) * 4,
                                    (random01(g) - .5f) * 5),
                                 .8f, .16f, rgb(.51, .95, .62));
                }
            }
        }
    if (c->fuel <= 0) {
        c->empty_time += dt;
        if (index == 0 && c->empty_time < .03f) {
            toast(g, "OUT OF FUEL", 3);
            g->sound_event = 6;
        }
        if (c->speed < .5f && c->empty_time > 2) {
            c->dnf = true;
        }
    }
    if (c->speed < 1 && in.throttle > .5f && c->fuel > 0)
        c->stuck_time += dt;
    else
        c->stuck_time = 0;
    if (index > 0 && c->stuck_time > 3)
        game_reset_car(g, index);
    Vec3 forward = v3(sinf(c->yaw), 0, cosf(c->yaw));
    float side = c->velocity.x * forward.z - c->velocity.z * forward.x;
    float dust_rate = (off > .4f || fabsf(side) > 2) ? 24.f : g->selected == 0 ? 5.f : 12.f;
    bool wet = wheel_spray(g, c, dt);
    if (!wet && c->grounded && c->speed > 6 && random01(g) < dt * dust_rate) {
        Color color = g->selected == 2   ? rgb(.91, .95, .96)
                      : g->selected == 1 ? rgb(.80, .70, .48)
                                         : rgb(.58, .57, .38);
        particle(
            g, vadd(c->pos, vmul(forward, -1.2f)),
            vadd(vmul(forward, -1.4f), v3((random01(g) - .5f) * 2, .8f, (random01(g) - .5f) * 2)),
            .55f, .09f + fabsf(side) * .03f, color);
    }
}

bool game_car_precedes(const Game *g, int a, int b) {
    const Car *first = &g->cars[a], *second = &g->cars[b];
    if (first->dnf != second->dnf)
        return !first->dnf;
    if (first->finished != second->finished)
        return first->finished;
    if (first->finished) {
        if (first->finish_time != second->finish_time)
            return first->finish_time < second->finish_time;
    } else if (first->progress != second->progress)
        return first->progress > second->progress;
    // Use the same deterministic tie order for medals and the results table.
    return a < b;
}

static int player_rank(const Game *g) {
    int rank = 1;
    for (int i = 1; i < CAR_COUNT; i++)
        if (game_car_precedes(g, i, 0))
            rank++;
    return rank;
}

bool game_save_profile(Game *g) {
    g->record_save_failed = !profile_save(&g->profile);
    g->save_failure_acknowledged = false;
    if (g->record_save_failed && (g->screen == SCREEN_RACE || g->screen == SCREEN_COUNTDOWN))
        g->screen = SCREEN_PAUSE;
    return !g->record_save_failed;
}

bool game_save_warning(const Game *g) {
    return g->record_save_failed && !g->save_failure_acknowledged;
}

void game_continue(Game *g) {
    if (game_save_warning(g))
        return;
    if (g->screen == SCREEN_RESULTS && g->championship_celebration) {
        g->championship_celebration = false;
        return;
    }
    if (g->screen == SCREEN_RESULTS && g->mode == MODE_CHAMPIONSHIP) {
        if (!g->championship_scored && !g->test_mode)
            return;
        if (game_championship_finished(g)) {
            g->screen = SCREEN_MENU;
            game_set_mode(g, MODE_CHAMPIONSHIP);
            return;
        }
        int next = game_next_track(g);
        if (next >= 0)
            g->selected = next;
    }
    game_start(g);
}

void game_open_records(Game *g) {
    g->records_return = g->screen;
    g->record_history = false;
    g->record_page = 0;
    g->screen = SCREEN_RECORDS;
}

bool game_submit_initials(Game *g) {
    if (!g->initials_pending || g->test_mode || !g->result_id)
        return false;
    for (int i = 0; i < 3; i++)
        if (g->initials[i] < 'A' || g->initials[i] > 'Z')
            return false;
    g->initials[3] = 0;
    SDL_strlcpy(g->profile.initials, g->initials, sizeof g->profile.initials);
    g->profile.initials_set = true;
    g->cars[0].name = game_player_name(g);
    TrackRecords *table = &g->profile.records[g->selected];
    for (int i = 0; i < table->top_count; i++)
        if (table->top[i].id == g->result_id)
            SDL_strlcpy(table->top[i].initials, g->initials, 4);
    for (int i = 0; i < table->history_count; i++)
        if (table->history[i].id == g->result_id)
            SDL_strlcpy(table->history[i].initials, g->initials, 4);
    if (!game_save_profile(g))
        return false;
    g->initials_pending = false;
    if (g->mode == MODE_CHAMPIONSHIP)
        return true;
    game_open_records(g);
    bool in_top = false;
    for (int i = 0; i < table->top_count; i++)
        in_top |= table->top[i].id == g->result_id;
    g->record_history = !in_top;
    return true;
}

static void record_finish(Game *g) {
    Profile *p = &g->profile;
    TrackRecords *table = &p->records[g->selected];
    float seconds = roundf(g->cars[0].finish_time * 1000) / 1000;
    g->previous_best = p->best[g->selected];
    g->new_record = g->previous_best == 0 || seconds < g->previous_best;
    if (g->new_record)
        p->best[g->selected] = seconds;
    RaceRecord entry = {.id = ++p->record_serial,
                        .time = seconds,
                        .best_lap = g->cars[0].best_lap,
                        .improvement = g->new_record && g->previous_best > 0
                                           ? g->previous_best - seconds : 0,
                        .date = (long long)time(NULL),
                        .difficulty = p->difficulty,
                        .mode = g->mode,
                        .rank = g->finish_rank,
                        .record = g->new_record};
    SDL_strlcpy(entry.initials, p->initials, sizeof entry.initials);
    int at = 0;
    // Earlier entries keep their place when displayed millisecond times tie.
    while (at < table->top_count && table->top[at].time <= seconds)
        at++;
    if (at < RECORD_TOP_COUNT) {
        if (table->top_count < RECORD_TOP_COUNT)
            table->top_count++;
        for (int i = table->top_count - 1; i > at; i--)
            table->top[i] = table->top[i - 1];
        table->top[at] = entry;
    }
    if (table->history_count < RECORD_HISTORY_COUNT)
        table->history_count++;
    for (int i = table->history_count - 1; i > 0; i--)
        table->history[i] = table->history[i - 1];
    table->history[0] = entry;
    g->result_id = entry.id;
    // Ask on the opening Championship track, then reuse the initials for later stages.
    g->initials_pending = g->mode == MODE_ARCADE || g->selected == 0;
    g->initials_cursor = 0;
    SDL_strlcpy(g->initials, p->initials, sizeof g->initials);
}

static void championship_score(Game *g) {
    if (g->mode != MODE_CHAMPIONSHIP || g->test_mode || g->championship_scored ||
        g->selected != g->profile.championship_stages)
        return;
    for (int i = 0; i < CAR_COUNT; i++)
        if (!g->cars[i].finished && !g->cars[i].dnf)
            return;
    for (int i = 0; i < CAR_COUNT; i++)
        g->profile.championship_places[g->selected][i] = championship_place(g, g->selected, i);
    g->profile.championship_stages++;
    int place = g->profile.championship_places[g->selected][0];
    g->profile.championship_eliminated = g->profile.championship_stages < TRACK_COUNT &&
                                       (place == 0 || place > 4);
    g->championship_scored = true;
    g->championship_advanced = !g->profile.championship_eliminated;
    if (g->championship_advanced &&
        g->profile.championship_stages > g->profile.championship_completed) {
        g->profile.championship_completed = g->profile.championship_stages;
        if (g->profile.championship_completed < TRACK_COUNT)
            g->unlocked_track = g->profile.championship_completed;
    }
    if (game_championship_finished(g)) {
        int rank = game_championship_rank(g);
        int medal = !g->profile.championship_eliminated && rank <= 3 ? 4 - rank : 0;
        if (medal > g->profile.championship_medal)
            g->profile.championship_medal = medal;
        g->championship_celebration = medal > 0;
        g->celebration_time = 0;
        g->sound_event = medal > 0 ? 9 : 7;
    }
    game_save_profile(g);
}

void game_results(Game *g) {
    if (g->screen == SCREEN_RESULTS || g->screen == SCREEN_RECORDS)
        return;
    g->screen = SCREEN_RESULTS;
    Car *p = &g->cars[0];
    g->finish_rank = player_rank(g);
    // Demonstrations must not change the profile in memory either: changing a
    // setting later saves the entire profile, including its records.
    if (p->finished && !p->dnf && !g->test_mode && isfinite(p->finish_time) &&
        p->finish_time >= .001f && p->finish_time < 86400) {
        record_finish(g);
        int medal = g->finish_rank <= 3 ? 4 - g->finish_rank : 0;
        if (medal > g->profile.medals[g->selected])
            g->profile.medals[g->selected] = medal;
        game_save_profile(g);
    }
    championship_score(g);
    g->sound_event = g->championship_celebration ? 9 : 7;
}

bool game_start_hint_visible(const Game *g) {
    return g->screen == SCREEN_RACE && !g->profile.auto_accel && !g->start_hint_dismissed &&
           g->start_idle_time >= 4.f && g->cars[0].fuel > 0 && !g->cars[0].finished &&
           !g->cars[0].dnf;
}

static void resolve_car_collisions(Game *g) {
    for (int i = 0; i < CAR_COUNT; i++)
        for (int j = i + 1; j < CAR_COUNT; j++) {
            Car *a = &g->cars[i], *b = &g->cars[j];
            if (a->dnf || b->dnf || fabsf(a->pos.y - b->pos.y) > 1.5f)
                continue;
            // Two overlapping tire-width circles form an oriented vehicle capsule.
            // This prevents a car's hood from passing through another car's trunk.
            Vec3 af = v3(sinf(a->yaw) * .72f, 0, cosf(a->yaw) * .72f),
                 bf = v3(sinf(b->yaw) * .72f, 0, cosf(b->yaw) * .72f);
            Vec3 d = vsub(a->pos, b->pos);
            d.y = 0;
            float l = 100;
            for (int aa = -1; aa <= 1; aa += 2)
                for (int bb = -1; bb <= 1; bb += 2) {
                    Vec3 diff =
                        vsub(vadd(a->pos, vmul(af, (float)aa)), vadd(b->pos, vmul(bf, (float)bb)));
                    diff.y = 0;
                    float len = vlen(diff);
                    if (len < l) {
                        l = len;
                        d = diff;
                    }
                }
            if (l < 1.65f && l > .0001f) {
                Vec3 n = vmul(d, 1 / l);
                float push = (1.65f - l) * .51f;
                a->pos = vadd(a->pos, vmul(n, push));
                b->pos = vsub(b->pos, vmul(n, push));
                float speed = vdot(vsub(a->velocity, b->velocity), n);
                if (speed < 0) {
                    a->velocity = vsub(a->velocity, vmul(n, speed * .58f));
                    b->velocity = vadd(b->velocity, vmul(n, speed * .58f));
                }
                if (i == 0 && speed < -2)
                    g->shake = .12f;
            }
        }
    // Collision separation changes XZ after the driving step. Re-seat grounded
    // cars at that final position, including cars pushed onto the shoulder.
    for (int i = 0; i < CAR_COUNT; i++) {
        Car *c = &g->cars[i];
        if (c->grounded && !c->dnf)
            car_surface_pose(&g->tracks[g->selected], &c->pos, c->yaw, &c->pitch, &c->roll);
    }
}

void game_tick(Game *g, Controls controls, float dt, bool autopilot) {
    if (g->screen == SCREEN_PAUSE)
        return;
    if (g->screen == SCREEN_RACE || g->screen == SCREEN_COUNTDOWN || g->screen == SCREEN_RESULTS)
        weather_tick(g, dt);
    g->clock += dt;
    g->toast_time = fmaxf(0, g->toast_time - dt);
    g->shake = fmaxf(0, g->shake - dt);
    for (int j = 0; j < MAX_PARTICLES; j++) {
        Particle *p = &g->particles[j];
        if (p->life > 0) {
            p->life -= dt;
            p->pos = vadd(p->pos, vmul(p->vel, dt));
            if (p->kind != PARTICLE_RIPPLE)
                p->vel.y -= (p->kind == PARTICLE_SPRAY ? 9.8f : 2) * dt;
            if (p->kind == PARTICLE_SPRAY && p->vel.y < 0) {
                float level;
                if (track_water(&g->tracks[g->selected], p->pos, &level) && p->pos.y <= level) {
                    if (random01(g) > .25f) {
                        p->life = 0;
                        continue;
                    }
                    p->pos.y = level + .025f;
                    p->vel = v3(0, 0, 0);
                    p->kind = PARTICLE_RIPPLE;
                    p->life = p->max_life = .55f;
                    p->size = .18f;
                }
            }
        }
    }
    if (g->screen == SCREEN_COUNTDOWN) {
        int last = (int)ceilf(g->countdown);
        g->countdown -= dt;
        if ((int)ceilf(g->countdown) != last)
            g->sound_event = g->countdown <= 0 ? 3 : 1;
        if (g->countdown <= 0) {
            g->screen = SCREEN_RACE;
            toast(g, "GO!", .9f);
        }
        return;
    }
    if (g->screen == SCREEN_RESULTS) {
        if (g->championship_celebration && !g->initials_pending)
            g->celebration_time += dt;
        bool remaining = false;
        for (int i = 1; i < CAR_COUNT; i++)
            if (!g->cars[i].finished && !g->cars[i].dnf)
                remaining = true;
        if (remaining)
            g->time += dt;
        // Keep the player and every finisher moving even after the last rival
        // is classified. Race time only advances while somebody is racing.
        for (int i = 0; i < CAR_COUNT; i++) {
            Controls ai;
            game_ai(g, i, &ai);
            update_car(g, i, ai, dt);
            if (g->time > 220 && !g->cars[i].finished)
                g->cars[i].dnf = true;
        }
        props_tick(g, dt);
        resolve_car_collisions(g);
        g->finish_rank = player_rank(g);
        championship_score(g);
        return;
    }
    if (g->screen != SCREEN_RACE)
        return;
    // Only offer help before the first launch; countdown and pause never count.
    if (autopilot || g->profile.auto_accel || controls.throttle > .1f || g->cars[0].speed > 1.f)
        g->start_hint_dismissed = true;
    if (!g->start_hint_dismissed)
        g->start_idle_time += dt;
    g->time += dt;
    for (int i = 0; i < CAR_COUNT; i++) {
        Controls in = controls;
        if (i > 0 || autopilot)
            game_ai(g, i, &in);
        update_car(g, i, in, dt);
    }
    // A rival may cross earlier within this same step. Classify and save only
    // once every car has its interpolated crossing time.
    if (g->cars[0].finished || g->cars[0].dnf)
        game_results(g);
    props_tick(g, dt);
    resolve_car_collisions(g);
    g->rank = player_rank(g);
    Car *p = &g->cars[0];
    TrackPoint tp = g->tracks[g->selected].points[p->nearest];
    // Backing up is a maneuver, including coasting after releasing the brake.
    // Warn only when driving nose-first against the direction of the track.
    float forward_speed = p->velocity.x * sinf(p->yaw) + p->velocity.z * cosf(p->yaw);
    if (p->speed > 3 && forward_speed > 0 &&
        (p->velocity.x * tp.dx + p->velocity.z * tp.dz) < -1)
        g->wrong_way += dt;
    else
        g->wrong_way = 0;
}

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL: %s\n", m);                                                      \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_place_car(Game *g, int index, float progress, float speed, bool reverse) {
    Car *car = &g->cars[index];
    const Track *track = &g->tracks[g->selected];
    Vec3 direction;
    car->progress = progress;
    car->offroad = false;
    car->offroad_delta = 0;
    car->offroad_distance = 0;
    car->offroad_time = 0;
    car->pos = track_at(track, progress, 0, &direction);
    car->route_pos = car->pos;
    car->prev = car->pos;
    car->nearest = -1;
    car->last_s = track_nearest(track, car->pos, &car->nearest, NULL, NULL);
    car->yaw = atan2f(direction.x, direction.z) + (reverse ? PI : 0);
    car->prev_yaw = car->yaw;
    car->velocity = v3(sinf(car->yaw) * speed, 0, cosf(car->yaw) * speed);
    car->speed = speed;
}

static void test_place_player(Game *g, float progress, float speed, bool reverse) {
    test_place_car(g, 0, progress, speed, reverse);
}

static void test_steering_direction(Game *g) {
    const Track *track = &g->tracks[g->selected];
    for (int point = 10; point < track->count; point += 160) {
        for (int drift = 0; drift < 2; drift++) {
            for (int steer = -1; steer <= 1; steer += 2) {
                game_start(g);
                g->screen = SCREEN_RACE;
                for (int i = 1; i < CAR_COUNT; i++)
                    g->cars[i].dnf = true;
                test_place_player(g, track->points[point].s, 12, false);
                Car *car = &g->cars[0];
                Vec3 origin = car->pos;
                Vec3 forward = v3(sinf(car->yaw), 0, cosf(car->yaw));
                Mat4 view = lookat(vadd(origin, vadd(vmul(forward, -23), v3(0, 32, 0))),
                                   vadd(origin, vmul(forward, 9)), v3(0, 1, 0));
                Vec3 screen_right = v3(view.m[0], view.m[4], view.m[8]);
                for (int step = 0; step < 40; step++)
                    game_tick(g, (Controls){.steer = steer, .throttle = 1, .drift = drift},
                              FIXED_DT, false);
                Vec3 heading = v3(sinf(car->yaw), 0, cosf(car->yaw));
                CHECK(vdot(heading, screen_right) * steer > .1f,
                      "steering points the nose toward the requested screen side");
                CHECK(vdot(vsub(car->pos, origin), screen_right) * steer > .05f,
                      "steering moves the car toward the requested screen side");
            }
        }
    }
}

static void test_lap_timing(Game *g) {
    game_start(g);
    g->screen = SCREEN_RACE;
    for (int i = 1; i < CAR_COUNT; i++)
        g->cars[i].dnf = true;
    Track *track = &g->tracks[g->selected];
    Car *car = &g->cars[0];

    // Going backwards across the starting line must not count the grid as a lap.
    test_place_player(g, .05f, 12, true);
    game_tick(g, (Controls){0}, FIXED_DT, false);
    test_place_player(g, car->progress, 12, false);
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(car->lap == 1 && car->best_lap == 0, "recrossing the start does not award a lap");

    g->time = 40;
    car->checkpoints = 3;
    test_place_player(g, track->length - .05f, 12, false);
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(car->lap == 2 && car->checkpoints == 4, "a full lap advances once");
    float first_lap = car->lap_time;
    float lap_start = car->lap_start;
    CHECK(first_lap > 40 && first_lap < g->time,
          "first lap uses the crossing time within the simulation step");

    // Repeat a realistic boundary crossing in both directions. Changing the
    // heading here isolates lap accounting from the steering model.
    for (int i = 0; i < 3; i++) {
        test_place_player(g, track->length + .05f, 12, true);
        game_tick(g, (Controls){0}, FIXED_DT, false);
        CHECK(car->progress < track->length && car->lap == 2,
              "driving backwards does not undo a completed lap");

        g->time += 3;
        test_place_player(g, car->progress, 12, false);
        game_tick(g, (Controls){0}, FIXED_DT, false);
        CHECK(car->progress > track->length, "the regression car recrosses the finish line");
        CHECK(car->lap_start == lap_start && car->lap_time == first_lap &&
                  car->best_lap == first_lap && car->checkpoints == 4,
              "recrossing a completed lap does not overwrite timing or checkpoints");
        CHECK(!car->finished, "recrossing does not finish the race early");
    }

    game_reset_car(g, 0);
    CHECK(car->lap == 2 && car->lap_start == lap_start && car->best_lap == first_lap,
          "recovery preserves completed laps and their timing");

    g->time = 80;
    car->checkpoints = 7;
    test_place_player(g, track->length * RACE_LAPS - .05f, 12, false);
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(car->finished && car->checkpoints == 8 && g->screen == SCREEN_RESULTS,
          "the next complete lap still finishes the race");
    CHECK(fabsf(car->lap_time - (car->finish_time - lap_start)) < .001f && car->best_lap > 39 &&
              car->finish_time > 80 && car->finish_time < g->time,
          "the final lap includes time spent recrossing and recovering");
}

static void test_finish_rollout(Game *g) {
    game_start(g);
    g->screen = SCREEN_RACE;
    g->time = 60;
    const Track *track = &g->tracks[g->selected];
    for (int i = 2; i < CAR_COUNT; i++)
        g->cars[i].dnf = true;
    test_place_car(g, 0, track->length * RACE_LAPS - 150, 0, false);
    test_place_car(g, 1, track->length * RACE_LAPS - .05f, 25, false);
    g->cars[1].checkpoints = RACE_LAPS * 4 - 1;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(g->cars[1].finished && g->screen == SCREEN_RACE,
          "rival finishes while the player is still racing");
    Vec3 finish_pos = g->cars[1].pos;
    float finish_speed = g->cars[1].speed;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(vlen(vsub(g->cars[1].pos, finish_pos)) > .1f &&
              fabsf(g->cars[1].speed - finish_speed) < .2f,
          "crossing the finish preserves motion and brakes progressively");
    for (int step = 0; step < 120 * 3; step++)
        game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(vlen(vsub(g->cars[1].pos, finish_pos)) > 25 && g->cars[1].speed < 15,
          "finished rival clears the line and slows down during the race");

    test_place_car(g, 0, track->length * RACE_LAPS - .05f, 25, false);
    g->cars[0].checkpoints = RACE_LAPS * 4 - 1;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(g->screen == SCREEN_RESULTS && g->cars[0].finished && g->finish_rank == 2,
          "player is classified behind the rival already on its cooldown lap");
    Car classified[2] = {g->cars[0], g->cars[1]};
    float race_time = g->time, distance[2] = {0};
    bool continuous = true;
    // More than a whole cooldown lap: crossing the line again cannot award
    // new laps, change results, or return either car to its finish position.
    for (int step = 0; step < 120 * 140; step++) {
        Vec3 previous[2] = {g->cars[0].pos, g->cars[1].pos};
        game_tick(g, (Controls){.reset = true, .throttle = 1, .steer = 1}, FIXED_DT, false);
        for (int i = 0; i < 2; i++) {
            float moved = vlen(vsub(g->cars[i].pos, previous[i]));
            distance[i] += moved;
            continuous &= isfinite(moved) && moved < .5f;
        }
    }
    CHECK(continuous, "cooldown movement stays continuous without teleporting");
    for (int i = 0; i < 2; i++) {
        const Car *car = &g->cars[i], *saved = &classified[i];
        printf("Cooldown %s car=%d: distance=%.1f length=%.1f speed=%.1f\n", track->id, i,
               distance[i], track->length, car->speed);
        CHECK(distance[i] > track->length && car->speed > 5 && car->speed < 15,
              "every finisher keeps driving slowly after all results are final");
        CHECK(car->finish_time == saved->finish_time && car->progress == saved->progress &&
                  car->best_lap == saved->best_lap && car->lap_time == saved->lap_time &&
                  car->lap_start == saved->lap_start && car->lap == saved->lap &&
                  car->checkpoints == saved->checkpoints && car->fuel == saved->fuel &&
                  car->collected == saved->collected && car->jumps == saved->jumps && !car->dnf,
              "cooldown laps preserve all race statistics");
    }
    CHECK(g->time == race_time && g->finish_rank == 2,
          "cooldown motion does not advance the completed race clock or ranking");
}

static void test_close_finish(Game *g) {
    const Track *track = &g->tracks[g->selected];
    for (int winner = 0; winner < 2; winner++) {
        game_start(g);
        g->screen = SCREEN_RACE;
        g->time = 60;
        for (int i = 2; i < CAR_COUNT; i++)
            g->cars[i].dnf = true;
        // Both cars cross in one step. Swap their gaps to verify that either
        // driver can win regardless of the order in which cars are updated.
        for (int i = 0; i < 2; i++) {
            test_place_car(g, i, track->length * RACE_LAPS - .15f, 30, false);
            Car *car = &g->cars[i];
            car->progress = track->length * RACE_LAPS - (i == winner ? .02f : .20f);
            car->checkpoints = RACE_LAPS * 4 - 1;
            car->lap = RACE_LAPS;
            car->lap_start = 30;
        }
        float before[2] = {g->cars[0].progress, g->cars[1].progress};
        game_tick(g, (Controls){.throttle = 1}, FIXED_DT, false);
        CHECK(g->cars[0].finished && g->cars[1].finished,
              "close finish includes both cars in the same step");
        CHECK(g->screen == SCREEN_RESULTS && g->finish_rank == (winner == 0 ? 1 : 2),
              "close finish awards the position after updating every car");
        CHECK(g->cars[winner].finish_time < g->cars[1 - winner].finish_time &&
                  game_car_precedes(g, winner, 1 - winner),
              "classification agrees with the earlier crossing");
        for (int i = 0; i < 2; i++) {
            Car *car = &g->cars[i];
            float fraction = (track->length * RACE_LAPS - before[i]) / (car->progress - before[i]);
            CHECK(fabsf(car->finish_time - (60 + fraction * FIXED_DT)) < .00001f,
                  "finish times interpolate each car's crossing fraction");
            CHECK(fabsf(car->lap_time - (car->finish_time - 30)) < .00001f,
                  "final lap and race time use the same crossing");
        }
        g->cars[1].finish_time = g->cars[0].finish_time;
        g->screen = SCREEN_RACE;
        game_results(g);
        CHECK(g->finish_rank == 1 && game_car_precedes(g, 0, 1) && !game_car_precedes(g, 1, 0),
              "exact ties use the same order for results and medals");
    }
}

static void test_ramp_exits(Game *g) {
    const Track *track = &g->tracks[g->selected];
    for (int j = 0; j < track->ramp_count; j++) {
        Ramp ramp = track->ramps[j];
        TrackPoint rp = track->points[(int)ramp.index];
        float lip = ramp.length * .5f;
        struct {
            float along, side, forward_speed, side_speed;
            bool jump;
        } cases[] = {{lip - .01f, 0, 18, 0, true},
                     {-lip + .01f, 0, -18, 0, false},
                     {0, ramp.half - .01f, 0, 18, false},
                     {0, -ramp.half + .01f, 0, -18, false},
                     {lip - .06f, ramp.half - .01f, 18, 18, false},
                     {lip - .06f, -ramp.half + .01f, 18, -18, false},
                     {lip - .01f, ramp.half - .06f, 18, 18, true},
                     {lip - .01f, -ramp.half + .06f, 18, -18, true},
                     {lip - .01f, 0, 4, 0, false}};
        for (size_t k = 0; k < SDL_arraysize(cases); k++) {
            game_start(g);
            g->screen = SCREEN_RACE;
            g->time = FIXED_DT;
            Car *car = &g->cars[0];
            car->pos = vadd(rp.p, v3(rp.dx * cases[k].along + rp.dz * cases[k].side, 0,
                                     rp.dz * cases[k].along - rp.dx * cases[k].side));
            car->velocity = v3(rp.dx * cases[k].forward_speed + rp.dz * cases[k].side_speed, 0,
                               rp.dz * cases[k].forward_speed - rp.dx * cases[k].side_speed);
            car->speed = vlen(car->velocity);
            car->yaw = atan2f(car->velocity.x, car->velocity.z);
            car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
            car->nearest = -1;
            car->last_s = track_nearest(track, car->pos, &car->nearest, NULL, NULL);
            car->progress = car->last_s;
            bool on_ramp;
            track_height(track, car->pos, &on_ramp);
            CHECK(on_ramp, "ramp exit fixture starts on the deck");
            update_car(g, 0, (Controls){0}, FIXED_DT);
            track_height(track, car->pos, &on_ramp);
            CHECK(!on_ramp, "ramp exit fixture crosses an edge");
            CHECK(car->jumps == (int)cases[k].jump,
                  "only the forward lip at takeoff speed awards a jump");
            CHECK(cases[k].jump ? car->vy > 0 && !car->grounded : car->vy <= 0,
                  "side, back and slow exits never launch the car upward");
            if (k == 2 || k == 3) {
                CHECK(!car->grounded, "a side exit falls instead of snapping to the road");
                for (int step = 0; step < 240 && !car->grounded; step++) {
                    g->time += FIXED_DT;
                    update_car(g, 0, (Controls){0}, FIXED_DT);
                }
                CHECK(car->grounded && car->jumps == 0, "a side exit lands without earning a jump");
            }
        }
    }
}

static void test_drift_braking(Game *g) {
    game_start(g);
    g->screen = SCREEN_RACE;
    test_place_player(g, g->tracks[g->selected].points[10].s, 20, false);
    Car initial = g->cars[0];
    update_car(g, 0, (Controls){.throttle = 1, .drift = true}, FIXED_DT);
    float drift_speed = g->cars[0].speed;
    g->cars[0] = initial;
    update_car(g, 0, (Controls){.throttle = 1, .drift = true, .brake = .12f}, FIXED_DT);
    CHECK(fabsf(g->cars[0].speed - drift_speed) < .00001f,
          "drift applies the same braking with or without input-side braking");
    g->cars[0] = initial;
    update_car(g, 0, (Controls){.throttle = 1}, FIXED_DT);
    CHECK(g->cars[0].speed > drift_speed + .02f, "drift applies actual braking to the car");
    g->cars[0] = initial;
    update_car(g, 0, (Controls){.drift = true, .brake = 1}, FIXED_DT);
    CHECK(g->cars[0].speed < drift_speed - .1f, "drift preserves stronger driver braking");
}

static void test_reverse(Game *g) {
    Controls reverse = {.brake = 1, .reverse = true};
    Car car = {.velocity = v3(0, 0, 20), .speed = 20, .fuel = 100, .grounded = true};
    bool stopped = false;
    for (int step = 0; step < 360; step++) {
        car_handling(&car, g->selected, reverse, 0, FIXED_DT);
        if (car.velocity.z == 0)
            stopped = true;
        CHECK(car.velocity.z >= 0 || stopped, "touch brake stops before reversing");
    }
    CHECK(car.velocity.z < -6 && car.speed <= 7.001f,
          "held touch brake reverses with a low speed limit");
    Car backing = car;
    for (int step = 0; step < 120; step++)
        car_handling(&car, g->selected, (Controls){.throttle = 1}, 0, FIXED_DT);
    CHECK(car.velocity.z > 0, "gas resumes forward driving from reverse");
    car = backing;
    for (int step = 0; step < 1200; step++)
        car_handling(&car, g->selected, (Controls){0}, 0, FIXED_DT);
    CHECK(car.speed == 0, "releasing reverse without gas coasts to a stop");
    for (int sign = -1; sign <= 1; sign += 2) {
        car = backing;
        reverse.steer = sign;
        car_handling(&car, g->selected, reverse, 0, FIXED_DT);
        CHECK(car.yaw * sign > 0, "steering turns in the opposite direction while reversing");
    }
    reverse.steer = 0;
    car = (Car){.fuel = 0, .grounded = true};
    car_handling(&car, g->selected, reverse, 0, FIXED_DT);
    CHECK(car.speed == 0, "reverse requires fuel");
    car = (Car){.fuel = 100, .grounded = false};
    car_handling(&car, g->selected, reverse, 0, FIXED_DT);
    CHECK(car.speed == 0, "reverse cannot engage in midair");
    car.grounded = true;
    car_handling(&car, g->selected, (Controls){.brake = 1}, 0, FIXED_DT);
    CHECK(car.speed == 0, "ordinary AI braking does not engage reverse");
    car_handling(&car, g->selected, (Controls){.drift = true}, 0, FIXED_DT);
    CHECK(car.speed == 0, "drift braking alone does not engage reverse");
    game_start(g);
    test_place_player(g, g->tracks[g->selected].points[10].s, 0, false);
    Car initial = g->cars[0];
    for (int step = 0; step < 60; step++)
        update_car(g, 0, reverse, FIXED_DT);
    Vec3 forward = v3(sinf(initial.yaw), 0, cosf(initial.yaw));
    CHECK(vdot(vsub(g->cars[0].pos, initial.pos), forward) < -.5f,
          "reverse moves the player backward on the track");
    CHECK(g->cars[0].progress < initial.progress && g->cars[0].lap == initial.lap,
          "reverse reduces race progress without awarding a lap");
    CHECK(g->cars[0].fuel < initial.fuel - .65f, "reverse consumes driving fuel");
}

static void test_wrong_way(Game *g) {
    for (int mode = 0; mode < 3; mode++) {
        game_start(g);
        g->screen = SCREEN_RACE;
        for (int i = 1; i < CAR_COUNT; i++)
            g->cars[i].dnf = true;
        // A pre-existing warning must disappear as soon as the car backs up.
        g->wrong_way = mode < 2 ? 2 : 0;
        Controls controls = mode == 0 ? (Controls){.brake = 1, .reverse = true}
                                     : (Controls){0};
        for (int step = 0; step < 150; step++) {
            test_place_player(g, g->tracks[g->selected].points[10].s, 6, true);
            if (mode < 2)
                g->cars[0].yaw -= PI;
            game_tick(g, controls, FIXED_DT, false);
            if (mode < 2)
                CHECK(g->wrong_way == 0,
                      "reversing clears wrong-way warnings even after releasing the brake");
        }
        if (mode == 2)
            CHECK(g->wrong_way > 1, "driving nose-first against the track still warns");
    }
}

static Car test_corner(int track, float speed, Controls controls, float dt) {
    Car car = {.velocity = v3(0, 0, speed), .speed = speed, .fuel = 100, .grounded = true};
    // Open, level road isolates handling from barriers, jumps and recovery.
    for (int step = 0; step < (int)lroundf(1 / dt); step++) {
        car_handling(&car, track, controls, 0, dt);
        car.pos = vadd(car.pos, vmul(car.velocity, dt));
    }
    return car;
}

static void test_cornering(void) {
    float previous_turn = 10;
    for (int track = 0; track < TRACK_COUNT; track++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            Controls turn = {.steer = sign, .throttle = 1};
            Car fast = test_corner(track, 33, turn, FIXED_DT);
            Car slow = test_corner(track, 12, turn, FIXED_DT);
            Car straight = test_corner(track, 33, (Controls){.throttle = 1}, FIXED_DT);
            Car gentle =
                test_corner(track, 33, (Controls){.steer = sign * .1f, .throttle = 1}, FIXED_DT);
            Car braking = test_corner(track, 33, (Controls){.steer = sign, .brake = .65f}, FIXED_DT);
            Car drift = test_corner(
                track, 33, (Controls){.steer = sign, .throttle = 1, .drift = true}, FIXED_DT);
            float fast_turn = fabsf(atan2f(fast.velocity.x, fast.velocity.z));
            float slow_turn = fabsf(atan2f(slow.velocity.x, slow.velocity.z));
            float brake_turn = fabsf(atan2f(braking.velocity.x, braking.velocity.z));
            CHECK(fast.speed / fast_turn > 2 * slow.speed / slow_turn,
                  "full-speed corners have a substantially wider radius than slow corners");
            CHECK(fast.speed < straight.speed - 3,
                  "forcing a sharp corner scrubs speed even at full throttle");
            CHECK(gentle.speed > straight.speed - 1 && straight.speed > 32,
                  "straights and gentle corrections preserve top speed");
            CHECK(braking.speed / brake_turn < fast.speed / fast_turn * .65f,
                  "braking restores the ability to hold a tighter corner");
            CHECK(fast.pos.x * sign < 0 && slow.pos.x * sign < 0,
                  "both steering directions retain responsive movement");
            float fast_slip = fabsf(angle_delta(fast.yaw, atan2f(fast.velocity.x, fast.velocity.z)));
            float drift_slip = fabsf(angle_delta(drift.yaw, atan2f(drift.velocity.x, drift.velocity.z)));
            CHECK(drift_slip > fast_slip + .1f && drift.speed < fast.speed,
                  "drift trades grip and speed for rotation rather than bypassing corner limits");
            Car finer = test_corner(track, 33, turn, FIXED_DT * .5f);
            CHECK(vlen(vsub(finer.pos, fast.pos)) < .15f && fabsf(finer.speed - fast.speed) < .1f,
                  "cornering remains consistent at smaller simulation steps");
            if (sign == 1) {
                CHECK(fast_turn < previous_turn, "slipperier surfaces widen fast corners");
                previous_turn = fast_turn;
                printf("Cornering track=%d: straight=%.1f sharp=%.1f radius=%.1f braked=%.1f\n",
                       track, straight.speed, fast.speed, fast.speed / fast_turn,
                       braking.speed / brake_turn);
            }
        }
    }
}

static void test_landing_speed(Game *g) {
    game_start(g);
    g->screen = SCREEN_RACE;
    for (int i = 1; i < CAR_COUNT; i++)
        g->cars[i].dnf = true;
    const Track *track = &g->tracks[g->selected];
    Car *car = &g->cars[0];
    test_place_player(g, track->points[10].s, 20, false);
    car->grounded = false;
    car->pos.y += .005f;
    car->vy = -10;

    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(car->grounded, "the landing regression car reaches the road");
    CHECK(car->speed < 19.5f && fabsf(car->speed - vlen(car->velocity)) < .001f,
          "landing slows actual velocity as well as the speed display");
    float landing_speed = car->speed;
    Vec3 landing_position = car->pos;

    game_tick(g, (Controls){0}, FIXED_DT, false);
    Vec3 displacement = vsub(car->pos, landing_position);
    displacement.y = 0;
    CHECK(car->speed <= landing_speed && vlen(displacement) <= landing_speed * FIXED_DT + .001f,
          "landing speed loss persists in the next step without throttle");
}

static void test_offroad_motion(Game *g) {
    const Track *track = &g->tracks[g->selected];
    int samples = 0;
    for (int point = 0; point < track->count; point += 13) {
        for (int side = -1; side <= 1; side += 2) {
            for (int distance = 3; distance <= 9; distance += 3) {
                for (int moving = 0; moving < 2; moving++) {
                    game_start(g);
                    // Isolate free motion: fixtures may otherwise spawn inside a tire barrier.
                    g->prop_count = 0;
                    g->screen = SCREEN_RACE;
                    for (int i = 1; i < CAR_COUNT; i++)
                        g->cars[i].dnf = true;
                    test_place_player(g, track->points[point].s, 0, false);
                    Car *car = &g->cars[0];
                    car->pos = track_at(track, car->progress,
                                        side * (track->points[point].half + distance), NULL);
                    car->yaw += side * PI * .5f;
                    car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
                    Vec3 origin = car->pos;
                    Vec3 forward = v3(sinf(car->yaw), 0, cosf(car->yaw));
                    if (moving)
                        car->velocity = vmul(forward, 12);
                    for (int step = 0; step < 30; step++) {
                        game_tick(g, (Controls){0}, FIXED_DT, false);
                        Vec3 displacement = vsub(car->pos, car->prev);
                        displacement.y = 0;
                        CHECK(
                            vlen(vsub(displacement, vmul(car->velocity, FIXED_DT))) < .0001f,
                            "off-road movement follows velocity without pulling toward the track");
                    }
                    Vec3 displacement = vsub(car->pos, origin);
                    displacement.y = 0;
                    CHECK(fabsf(displacement.x * forward.z - displacement.z * forward.x) < .001f,
                          "coasting off-road keeps the direction chosen by the driver");
                    if (!moving)
                        CHECK(vlen(displacement) < .0001f,
                              "a parked off-road car stays in place beside bends");
                    else
                        CHECK(vdot(displacement, forward) > 1 && car->speed < 12,
                              "a car can leave the shoulder while terrain slows it down");
                    samples++;
                }
            }
        }
    }
    printf("%s off-road motion: %d scenarios\n", track->id, samples);
}

static void test_offroad_recovery(Game *g) {
    const Track *track = &g->tracks[g->selected];
    Car *car = &g->cars[0];
    float terrain_speed = 0;
    for (int z = 100; z <= 110; z += 10) {
        game_start(g);
        g->screen = SCREEN_RACE;
        for (int i = 1; i < CAR_COUNT; i++)
            g->cars[i].dnf = true;
        car->pos = v3(0, 0, z);
        car->yaw = 0;
        car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
        for (int step = 0; step < 240; step++)
            game_tick(g, (Controls){.throttle = 1}, FIXED_DT, false);
        CHECK(car->speed > 4 && car->pos.z > z + 6 && fabsf(car->pos.x) < .001f,
              "a stopped car accelerates freely even far from the road");
        if (z == 100)
            terrain_speed = car->speed;
        else
            CHECK(fabsf(car->speed - terrain_speed) < .001f,
                  "deep terrain resistance stays consistent with distance from the road");
    }

    // Cross each actual island edge with the chassis on the ground and in
    // flight. Recovery must preserve the race state and leave a valid pose.
    for (int axis = 0; axis < 2; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            for (int airborne = 0; airborne < 2; airborne++) {
                game_start(g);
                g->screen = SCREEN_RACE;
                for (int i = 1; i < CAR_COUNT; i++)
                    g->cars[i].dnf = true;
                test_place_player(g, track->length * .3f, 0, false);
                car->checkpoints = 1;
                car->fuel = 42;
                car->pos = axis ? v3(sign * 127.8f, 0, 0) : v3(0, 0, sign * 125.8f);
                car->yaw = axis ? sign * PI * .5f : sign < 0 ? PI : 0;
                car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
                CHECK(fabsf(car->pos.y - track_height(track, car->pos, NULL)) < 1,
                      "tires crossing the island edge do not drop the chassis below terrain");
                if (airborne) {
                    car->grounded = false;
                    car->pos.y += 5;
                }
                car->velocity = v3(sinf(car->yaw) * 12, 0, cosf(car->yaw) * 12);
                bool recovered = false;
                for (int step = 0; step < 10; step++) {
                    float progress = car->progress, fuel = car->fuel;
                    game_tick(g, (Controls){0}, FIXED_DT, false);
                    if (car->speed == 0) {
                        Vec3 expected = track_at(track, progress, 0, NULL);
                        CHECK(hypotf(car->pos.x - expected.x, car->pos.z - expected.z) < .001f &&
                                  car->grounded && vlen(car->velocity) == 0 &&
                                  vlen(vsub(car->pos, car->prev)) < .001f,
                              "leaving the island recovers the car safely onto the road");
                        CHECK(car->progress == progress && car->fuel == fuel &&
                                  car->checkpoints == 1 && car->lap == 1 && !car->finished,
                              "island recovery does not award race progress or replenish fuel");
                        recovered = true;
                        break;
                    }
                }
                CHECK(recovered, "crossing the island edge triggers recovery");
            }
        }
    }
}

// Feed sampled paths through the real driving step without changing the race
// projection, so these fixtures also exercise wraparound and cached searches.
static void test_sample_path(Game *g, int index, float s, float lane) {
    Car *car = &g->cars[index];
    const Track *track = &g->tracks[g->selected];
    car->pos = track_at(track, s, lane, NULL);
    car->velocity = v3(0, 0, 0);
    car->grounded = true;
    car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
    g->time += FIXED_DT;
    update_car(g, index, (Controls){0}, FIXED_DT);
}

// Find a modest cut that the distance-only rule would penalize. Use the
// projected off-road portion, since the ends of a chord can still be on asphalt.
static bool test_find_brief_excursion(const Track *track, float *start, float *end) {
    for (int i = 0; i < track->count; i += 4) {
        for (int j = i + 4; j < track->count; j += 4) {
            const TrackPoint *a = &track->points[i], *b = &track->points[j];
            float arc = b->s - a->s;
            if (arc > 100)
                break;
            float chord = hypotf(b->p.x - a->p.x, b->p.z - a->p.z);
            if (arc - chord < 12 || arc - chord > 24)
                continue;
            float legal_s = a->s;
            int legal_step = 0;
            bool offroad = false;
            for (int step = 1; step <= 90; step++) {
                Vec3 pos = vlerp(a->p, b->p, step / 90.f), center;
                int nearest = -1;
                float s = track_nearest(track, pos, &nearest, &center, NULL);
                bool on_road = hypotf(pos.x - center.x, pos.z - center.z) <=
                               track->points[nearest].half + ROAD_EDGE_TOLERANCE;
                if (on_road && offroad) {
                    float delta = s - legal_s;
                    float saved = delta - chord * (step - legal_step) / 90.f;
                    if (saved > 10 && saved < 20 && saved > delta * .25f) {
                        *start = a->s;
                        *end = b->s;
                        return true;
                    }
                    break;
                }
                if (on_road) {
                    legal_s = s;
                    legal_step = step;
                } else {
                    offroad = true;
                }
            }
        }
    }
    return false;
}

static void test_shortcuts(Game *g) {
    const Track *track = &g->tracks[g->selected];
    const int boundaries[] = {1, 4, 8};
    // A one-second excursion alongside the road must keep its progress,
    // including when it crosses a checkpoint or finish line.
    for (int index = 0; index < 2; index++) {
        for (int b = 0; b < 3; b++) {
            for (int side = -1; side <= 1; side += 2) {
                game_start(g);
                Car *car = &g->cars[index];
                float start = boundaries[b] * track->length * .25f - 10;
                test_place_car(g, index, start, 0, false);
                car->checkpoints = boundaries[b] - 1;
                car->lap = car->checkpoints / 4 + 1;
                g->time = 40;
                for (int step = 0; step <= 120; step++) {
                    test_sample_path(g, index, start + step * .2f, side * 12);
                    CHECK(car->offroad && car->progress == start && !car->finished,
                          "off-road progress stays provisional until the route is validated");
                }
                test_sample_path(g, index, start + 24, 0);
                CHECK(!car->offroad && fabsf(car->progress - start - 24) < .01f,
                      "a one-second excursion keeps all legitimate forward progress");
                CHECK(car->checkpoints == boundaries[b] &&
                          car->finished == (boundaries[b] == RACE_LAPS * 4),
                      "a legitimate excursion can cross checkpoints and finish lines");
                CHECK(strcmp(g->toast, "SHORTCUT - BACK TO TRACK") != 0,
                      "ordinary shoulder driving never displays a shortcut penalty");
            }
        }
    }

    float brief_start = 0, brief_end = 0;
    bool found_brief = test_find_brief_excursion(track, &brief_start, &brief_end);
    CHECK(found_brief, "the authored circuit contains a modest corner excursion fixture");
    if (found_brief) {
        for (int index = 0; index < 2; index++) {
            for (int slow = 0; slow < 2; slow++) {
                game_start(g);
                Car *car = &g->cars[index];
                test_place_car(g, index, brief_start, 0, false);
                Vec3 from = track_at(track, brief_start, 0, NULL);
                Vec3 to = track_at(track, brief_end, 0, NULL);
                int steps = slow ? 600 : 90;
                bool left_road = false, rejoined = false;
                float last_legal = brief_start;
                for (int step = 1; step <= steps; step++) {
                    if (!car->offroad)
                        last_legal = car->progress;
                    car->pos = vlerp(from, to, step / (float)steps);
                    car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
                    g->time += FIXED_DT;
                    update_car(g, index, (Controls){0}, FIXED_DT);
                    left_road |= car->offroad;
                    if (left_road && !car->offroad) {
                        CHECK(slow ? car->progress == last_legal : car->progress > last_legal + 8,
                              "brief corner excursions are forgiven; prolonged cuts are penalized");
                        CHECK(car->offroad_time == 0,
                              "rejoining or recovery clears the excursion timer");
                        if (index == 0)
                            CHECK((strcmp(g->toast, "SHORTCUT - BACK TO TRACK") == 0) == slow,
                                  "only prolonged modest cuts display a penalty");
                        rejoined = true;
                        break;
                    }
                }
                CHECK(left_road && rejoined, "modest corner fixture leaves and rejoins the road");
            }
        }
    }

    // Find a substantial bend in each authored circuit, then actually sample
    // the straight chord across it instead of following an offset road path.
    float best_gain = 0, start = 0, end = 0;
    for (int i = 0; i < track->count; i++) {
        for (int j = i + 1; j < track->count; j++) {
            const TrackPoint *a = &track->points[i], *b = &track->points[j];
            float arc = b->s - a->s;
            if (arc > 180)
                break;
            float gain = arc - hypotf(b->p.x - a->p.x, b->p.z - a->p.z);
            if (gain > best_gain) {
                best_gain = gain;
                start = a->s;
                end = b->s;
            }
        }
    }
    CHECK(best_gain > 40, "the authored circuit contains a real shortcut fixture");
    for (int index = 0; index < 2; index++) {
        for (int recovery = 0; recovery < 3; recovery++) {
            game_start(g);
            Car *car = &g->cars[index];
            test_place_car(g, index, start, 0, false);
            car->checkpoints = (int)(start / (track->length * .25f));
            car->fuel = 42;
            car->best_lap = 40;
            Vec3 from = track_at(track, start, 0, NULL);
            Vec3 to = track_at(track, end, 0, NULL);
            bool left_road = false, recovered = false;
            float last_legal = start;
            int checkpoints = car->checkpoints;
            // Even a sub-second excursion must not grant a major shortcut.
            int steps = recovery == 2 ? 90 : 600;
            for (int step = 1; step <= steps; step++) {
                if (!car->offroad) {
                    last_legal = car->progress;
                    checkpoints = car->checkpoints;
                }
                car->pos = vlerp(from, to, step / (float)steps);
                car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
                g->time += FIXED_DT;
                update_car(g, index, (Controls){0}, FIXED_DT);
                left_road |= car->offroad;
                if (recovery == 1 && car->offroad && step >= 300)
                    update_car(g, index, (Controls){.reset = true}, FIXED_DT);
                if (left_road && !car->offroad) {
                    Vec3 expected = track_at(track, last_legal, 0, NULL);
                    CHECK(hypotf(car->pos.x - expected.x, car->pos.z - expected.z) < .001f &&
                              car->progress == last_legal && car->speed == 0 &&
                              vlen(car->velocity) == 0 && car->grounded,
                          "a genuine corner shortcut returns to the last legal position");
                    CHECK(car->checkpoints == checkpoints && !car->finished &&
                              car->best_lap == 40 && car->fuel < 42,
                          "shortcut recovery preserves race history and spent fuel");
                    CHECK(car->offroad_time == 0, "shortcut recovery clears the excursion timer");
                    if (index == 0 && recovery != 1)
                        CHECK(strcmp(g->toast, "SHORTCUT - BACK TO TRACK") == 0,
                              "a genuine shortcut displays the penalty reason");
                    test_sample_path(g, index, last_legal + 1, 0);
                    CHECK(fabsf(car->progress - last_legal - 1) < .01f,
                          "normal progress resumes after the penalty");
                    recovered = true;
                    break;
                }
            }
            CHECK(left_road && recovered, "real bend shortcuts and manual resets are detected");
        }
    }
}

static void test_surface_contact(Game *g) {
    game_start(g);
    g->screen = SCREEN_RACE;
    for (int i = 1; i < CAR_COUNT; i++)
        g->cars[i].dnf = true;
    Track *t = &g->tracks[g->selected];
    Car *car = &g->cars[0];
    int below_road = 0;
    for (int i = 10; i < t->count; i += 19) {
        for (int side = -1; side <= 1; side += 2) {
            test_place_player(g, t->points[i].s, 0, false);
            car->pos = track_at(t, car->progress, side * (t->points[i].half + 3), NULL);
            car->yaw += PI * .5f;
            car->grounded = true;
            game_tick(g, (Controls){0}, FIXED_DT, false);
            // Check the rendered tire bottoms, not the ground at the chassis
            // center: axles can legitimately bridge a crease in the hillside.
            Mat4 rotation =
                mmul(rotate_y(car->yaw), mmul(rotate_x(car->pitch), rotate_z(car->roll)));
            for (int wheel = 0; wheel < 4; wheel++) {
                float x = wheel % 2 ? .85f : -.85f, z = wheel / 2 ? .94f : -.95f;
                Vec3 bottom =
                    vadd(car->pos,
                         v3(rotation.m[0] * x - rotation.m[4] * .04f + rotation.m[8] * z,
                            rotation.m[1] * x - rotation.m[5] * .04f + rotation.m[9] * z + .04f,
                            rotation.m[2] * x - rotation.m[6] * .04f + rotation.m[10] * z));
                float ground = track_height(t, bottom, NULL);
                if (fabsf(bottom.y - ground) >= .16f)
                    fprintf(stderr, "Contact %s point=%d side=%d wheel=%d gap=%.3f\n", t->id, i,
                            side, wheel, bottom.y - ground);
                CHECK(ground > -30 && fabsf(bottom.y - ground) < .16f,
                      "off-road tires stay in contact with both shoulders");
            }
            if (car->pos.y < t->points[i].p.y - .1f)
                below_road++;
            CHECK(car->grounded && car->jumps == 0, "resting on a shoulder does not cause a jump");
        }
    }
    CHECK(below_road > 20,
          "cars can settle below the road instead of riding an invisible extension");
    // Landing beyond the shoulder must use the terrain at the actual XZ.
    test_place_player(g, t->points[10].s, 0, false);
    car->pos = track_at(t, car->progress, t->points[10].half + 6, NULL);
    car->pos.y = track_height(t, car->pos, NULL) + 3;
    car->grounded = false;
    car->vy = -2;
    Vec3 landing_position = car->pos;
    for (int i = 0; i < 120; i++)
        game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(car->grounded && fabsf(car->pos.y - track_height(t, car->pos, NULL)) < .16f,
          "an airborne car lands on the terrain beyond the shoulder");
    CHECK(hypotf(car->pos.x - landing_position.x, car->pos.z - landing_position.z) < .0001f,
          "off-road flight and landing do not pull the car toward the road");
}

static void test_surface_slopes(void) {
    Track *t = calloc(1, sizeof *t);
    CHECK(t != NULL, "surface fixture allocation");
    if (!t)
        return;
    // A raised road over a sloped hillside. Known planes are independent of
    // the authored tracks and expose accidental extension of road collision.
    Vec3 a = v3(-40, -9, -40), b = v3(40, -2.6f, -40);
    Vec3 c = v3(40, 7, 40), d = v3(-40, .6f, 40);
    t->surface[0] = (SurfaceTriangle){a, b, c};
    t->surface[1] = (SurfaceTriangle){a, c, d};
    t->surface[2] = (SurfaceTriangle){v3(-5, 10, -40), v3(5, 10, -40), v3(5, 10, 40)};
    t->surface[3] = (SurfaceTriangle){v3(-5, 10, -40), v3(5, 10, 40), v3(-5, 10, 40)};
    t->surface_count = 4;
    for (int i = 0; i < SURFACE_GRID * SURFACE_GRID; i++) {
        t->surface_cells[i] = i * 4;
        for (int j = 0; j < 4; j++)
            t->surface_refs[i * 4 + j] = j;
    }
    t->surface_cells[SURFACE_GRID * SURFACE_GRID] = SURFACE_GRID * SURFACE_GRID * 4;
    CHECK(fabsf(track_height(t, v3(0, 100, 0), NULL) - 10) < .0001f,
          "road collision selects the visible upper surface");
    for (int side = -1; side <= 1; side += 2) {
        for (int heading = 0; heading < 8; heading++) {
            Vec3 pos = v3(side * 8, 100, 3);
            float yaw = heading * PI / 4, pitch, roll;
            CHECK(fabsf(track_height(t, pos, NULL) - (-1 + .08f * pos.x + .12f * pos.z)) < .0001f,
                  "outside road bounds collision follows the lower hillside plane");
            car_surface_pose(t, &pos, yaw, &pitch, &roll);
            Mat4 rotation = mmul(rotate_y(yaw), mmul(rotate_x(pitch), rotate_z(roll)));
            Vec3 up = v3(rotation.m[4], rotation.m[5], rotation.m[6]);
            CHECK(vdot(up, vnorm(v3(-.08f, 1, -.12f))) > .99999f,
                  "car pitch and roll follow the hillside at every heading");
            CHECK(fabsf(pos.y - (-1 + .08f * pos.x + .12f * pos.z)) < .002f,
                  "the car rests at the hillside height");
        }
    }
    free(t);
}

static void test_fuel_pickups(Game *g) {
    const Track *t = &g->tracks[g->selected];
    CHECK(t->can_count == 4, "four deliberate fuel stops per circuit");
    for (int j = 0; j < t->can_count; j++) {
        game_start(g);
        g->screen = SCREEN_RACE;
        float s = t->points[(int)t->cans[j].index].s;
        test_place_player(g, s, 0, false);
        Car *c = &g->cars[0];
        c->fuel = 50;
        update_car(g, 0, (Controls){0}, FIXED_DT);
        CHECK(c->collected == 0, "driving down the center does not collect roadside fuel");
        c->pos = track_at(t, s, t->cans[j].lane, NULL);
        update_car(g, 0, (Controls){0}, FIXED_DT);
        CHECK(c->collected == 1 && c->fuel > 67.9f && c->fuel < 68,
              "a precise pickup restores eighteen fuel points");
        CHECK(c->can_cooldown[j] == FUEL_PICKUP_COOLDOWN,
              "fuel pickup starts the longer respawn timer");
        update_car(g, 0, (Controls){0}, FIXED_DT);
        CHECK(c->collected == 1, "a collected can cannot immediately refill the tank again");
        Car *rival = &g->cars[1];
        rival->pos = c->pos;
        rival->fuel = 50;
        rival->nearest = -1;
        rival->last_s = track_nearest(t, rival->pos, &rival->nearest, NULL, NULL);
        update_car(g, 1, (Controls){0}, FIXED_DT);
        CHECK(rival->collected == 1 && rival->fuel > 67.9f && rival->fuel < 68,
              "rivals receive the same refill with independent availability");
        c->can_cooldown[j] = FIXED_DT;
        update_car(g, 0, (Controls){0}, FIXED_DT);
        CHECK(c->collected == 2, "fuel becomes collectible when its cooldown expires");

        game_start(g);
        test_place_player(g, s - 45, 18, false);
        c = &g->cars[0];
        c->fuel = 10;
        for (int i = 1; i < CAR_COUNT; i++) g->cars[i].dnf = true;
        Controls clear, traffic;
        game_ai(g, 0, &clear);
        rival = &g->cars[1];
        rival->dnf = false;
        rival->pos = track_at(t, s - 37, copysignf(.4f, t->cans[j].lane), NULL);
        rival->last_s = s - 37;
        rival->nearest = -1;
        game_ai(g, 0, &traffic);
        CHECK(fabsf(clear.steer - traffic.steer) < .0001f,
              "critical fuel approach keeps its pickup line instead of overtaking");
    }
}

static void test_ai_pace_and_range(Game *g) {
    float times[3];
    for (int difficulty = 0; difficulty < 3; difficulty++) {
        g->profile.difficulty = difficulty;
        game_start(g);
        g->screen = SCREEN_RACE;
        for (int i = 1; i < CAR_COUNT; i++)
            g->cars[i].dnf = true;
        float minimum_fuel = 100;
        for (int step = 0; step < 120 * 100 && g->screen != SCREEN_RESULTS; step++) {
            game_tick(g, (Controls){0}, FIXED_DT, true);
            minimum_fuel = fminf(minimum_fuel, g->cars[0].fuel);
        }
        times[difficulty] = g->time;
        CHECK(g->cars[0].finished, "solo AI finishes with the reduced fuel supply");
        CHECK(minimum_fuel < 30 && minimum_fuel > 0,
              "fuel falls into reserve without starving a competent driver");
    }
    printf("%s solo pace: easy=%.2fs normal=%.2fs hard=%.2fs\n", g->tracks[g->selected].id,
           times[0], times[1], times[2]);
    CHECK(times[1] < 65, "normal AI maintains competitive race pace with grip-limited corners");
    CHECK(times[1] < times[0] * .9f, "normal AI is substantially faster than easy AI");
    CHECK(times[2] < times[1], "hard AI is faster than normal AI within the same tire limits");

    g->profile.difficulty = 1;
    game_start(g);
    g->screen = SCREEN_RACE;
    for (int i = 1; i < CAR_COUNT; i++)
        g->cars[i].dnf = true;
    for (int j = 0; j < g->tracks[g->selected].can_count; j++)
        g->cars[0].can_cooldown[j] = 220;
    for (int step = 0; step < 120 * 100 && g->screen != SCREEN_RESULTS; step++)
        game_tick(g, (Controls){0}, FIXED_DT, true);
    CHECK(g->cars[0].collected == 0 && g->cars[0].dnf && !g->cars[0].finished,
          "skipping all fuel stops cannot complete a race");
}

static void test_weather(Game *g) {
    game_start(g);
    CHECK(g->weather.kind == WEATHER_CLEAR && g->weather.intensity == 0 &&
              g->weather.remaining >= 12,
          "races begin with a clear spell");
    uint32_t driving_rng = g->rng;
    bool seen_wind = false, seen_precipitation = false, smooth = true, climate = true;
    int clear_steps = 0;
    for (int step = 0; step < 120 * 600; step++) {
        float previous = g->weather.intensity;
        weather_tick(g, FIXED_DT);
        Weather *w = &g->weather;
        smooth &= isfinite(w->intensity) && w->intensity >= 0 && w->intensity <= 1 &&
                  fabsf(w->intensity - previous) < .004f && isfinite(vlen(w->wind));
        climate &= g->selected == 2 ? w->kind != WEATHER_RAIN : w->kind != WEATHER_SNOW;
        seen_wind |= w->kind == WEATHER_WIND && w->intensity > .5f;
        seen_precipitation |= (w->kind == WEATHER_RAIN || w->kind == WEATHER_SNOW) &&
                              w->intensity > .5f;
        clear_steps += w->kind == WEATHER_CLEAR;
    }
    CHECK(smooth, "weather fades smoothly with bounded intensity and finite wind");
    CHECK(climate && seen_wind && seen_precipitation,
          "each climate gets occasional precipitation and windy spells");
    CHECK(clear_steps > 120 * 300, "clear weather occupies most of the time");
    CHECK(g->rng == driving_rng, "weather does not consume driving randomness");
    g->weather = (Weather){.kind = WEATHER_WIND, .remaining = 10, .duration = 25,
                           .intensity = .8f, .strength = .8f, .rng = 123};
    Weather paused = g->weather;
    g->screen = SCREEN_PAUSE;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(memcmp(&paused, &g->weather, sizeof paused) == 0, "pause freezes all weather state");
    g->screen = SCREEN_COUNTDOWN;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    CHECK(g->weather.time > paused.time && vlen(g->weather.drift) > 0,
          "weather motion resumes with simulation");
    game_start(g);
    CHECK(g->weather.kind == WEATHER_CLEAR && g->weather.time == 0 &&
              vlen(g->weather.drift) == 0,
          "retry clears old precipitation and wind motion");
}

static void test_water(Game *g) {
    for (int k = 0; k < TRACK_COUNT; k++) {
        g->selected = k;
        game_start(g);
        const Track *t = &g->tracks[k];
        CHECK(t->water_count > 100, "each environment has authored water");
        int ford = -1, run = 0, longest = 0;
        for (int i = 0; i < t->count; i++) {
            float level;
            bool wet = track_water(t, t->points[i].p, &level) &&
                       level > track_height(t, t->points[i].p, NULL) + .04f;
            run = wet ? run + 1 : 0;
            if (run > longest) {
                longest = run;
                ford = i - run / 2;
            }
        }
        CHECK(ford >= 0, "terrain-led stream has a shallow road crossing");
        if (ford < 0) continue;
        Car *c = &g->cars[0];
        c->pos = t->points[ford].p;
        c->yaw = atan2f(t->points[ford].dx, t->points[ford].dz);
        car_surface_pose(t, &c->pos, c->yaw, &c->pitch, &c->roll);
        c->velocity = v3(sinf(c->yaw) * 20, 0, cosf(c->yaw) * 20);
        c->speed = 20;
        float level = 0;
        CHECK(track_water(t, c->pos, &level) && level > c->pos.y + .03f &&
                  level < c->pos.y + .65f, "ford is shallow water over the actual road");
        CHECK(!track_water(t, t->points[0].p, NULL), "start grid stays dry");
        for (int step = 0; step < 60; step++) wheel_spray(g, c, FIXED_DT);
        int drops = 0, rings = 0;
        for (int i = 0; i < MAX_PARTICLES; i++) {
            const Particle *p = &g->particles[i];
            drops += p->life > 0 && p->kind == PARTICLE_SPRAY;
            rings += p->life > 0 && p->kind == PARTICLE_RIPPLE;
            if (p->life > 0) {
                float local_level;
                CHECK(track_water(t, p->pos, &local_level) &&
                          fabsf(p->pos.y - local_level - .045f) < .005f,
                      "spray follows the local sloping waterline at each wheel");
            }
        }
        CHECK(drops > 20 && rings > 0, "moving wheels create spray and expanding ripples");
        int cursor = g->particle_cursor;
        c->grounded = false;
        CHECK(!wheel_spray(g, c, 1) && g->particle_cursor == cursor,
              "jumping over water produces no spray");
        c->grounded = true;
        c->velocity = v3(0,0,0);
        CHECK(wheel_spray(g, c, 1) && g->particle_cursor == cursor,
              "stationary wheels stay wet without spraying");
        c->pos = t->points[0].p;
        c->velocity = v3(0,0,20);
        CHECK(!wheel_spray(g, c, 1) && g->particle_cursor == cursor,
              "dry road does not create water particles");
        Particle before = g->particles[0];
        g->screen = SCREEN_PAUSE;
        game_tick(g, (Controls){0}, FIXED_DT, false);
        CHECK(memcmp(&before, &g->particles[0], sizeof before) == 0,
              "pause freezes spray and ripples");
        game_start(g);
        CHECK(g->particle_cursor == 0 && g->particles[0].life == 0,
              "retry clears water particles");
    }
}

int game_tests(void) {
    Game *g = calloc(1, sizeof *g);
    if (!g || !game_load(g)) {
        fprintf(stderr, "Cannot load test assets\n");
        free(g);
        return 1;
    }
    g->test_mode = true;
    test_water(g);
    test_surface_slopes();
    test_cornering();
    for (int k = 0; k < 3; k++) {
        g->selected = k;
        test_weather(g);
        Track *t = &g->tracks[k];
        CHECK(t->length > 600, "long track");
        CHECK(t->elevation > 6, "genuine elevation");
        CHECK(t->ramp_count == 3, "three ramps");
        for (int i = 0; i < t->count; i += 13) {
            int idx = -1;
            float lane;
            Vec3 cp;
            float s = track_nearest(t, t->points[i].p, &idx, &cp, &lane);
            CHECK(vlen(vsub(cp, t->points[i].p)) < .01f, "track projection round trip");
            CHECK(fabsf(s - t->points[i].s) < .01f, "track distance round trip");
        }
        for (int difficulty = 0; difficulty < 3; difficulty++) {
            g->profile.difficulty = difficulty;
            g->profile.best[k] = 1000;
            g->profile.medals[k] = 0;
            game_start(g);
            int steps = 0;
            while (g->screen != SCREEN_RESULTS && steps++ < 120 * 220)
                game_tick(g, (Controls){0}, FIXED_DT, true);
            printf("%s difficulty=%d: %.2fs, fuel=%0.1f, cans=%d, jumps=%d, rank=%d, finished=%d\n",
                   t->id, difficulty, g->time, g->cars[0].fuel, g->cars[0].collected,
                   g->cars[0].jumps, g->finish_rank, g->cars[0].finished);
            CHECK(g->cars[0].finished, "AI-driven full race finishes");
            CHECK(g->profile.best[k] == 1000 && g->profile.medals[k] == 0,
                  "demo finishes preserve records for subsequent settings saves");
            CHECK(g->cars[0].collected >= 2, "fuel pickup route works");
            CHECK(g->cars[0].fuel < 60, "races no longer end with a nearly full tank");
            CHECK(g->cars[0].jumps >= 3, "physical jumps work");
            CHECK(isfinite(g->cars[0].pos.x), "finite simulation");
            int rivals = 0;
            for (int i = 1; i < 8; i++) {
                CHECK(!g->cars[i].dnf, "AI manages fuel");
                if (g->cars[i].progress > t->length * 1.4f)
                    rivals++;
            }
            CHECK(rivals >= 5, "competitive AI field");
            float player_finish = g->time;
            for (int step = 0; step < 120 * 20; step++)
                game_tick(g, (Controls){0}, FIXED_DT, true);
            for (int i = 1; i < CAR_COUNT; i++) {
                if (!g->cars[i].finished || g->cars[i].dnf)
                    fprintf(stderr,
                            "%s difficulty=%d rival=%d: progress=%.1f fuel=%.1f cans=%d dnf=%d\n",
                            t->id, difficulty, i, g->cars[i].progress, g->cars[i].fuel,
                            g->cars[i].collected, g->cars[i].dnf);
                CHECK(g->cars[i].finished && !g->cars[i].dnf,
                      "every rival finishes the full race with available fuel");
                CHECK(g->cars[i].finish_time < player_finish + 12,
                      "rivals stay close instead of falling a lap behind");
            }
        }
        test_fuel_pickups(g);
        test_ai_pace_and_range(g);
        test_steering_direction(g);
        test_lap_timing(g);
        test_close_finish(g);
        test_finish_rollout(g);
        test_ramp_exits(g);
        test_drift_braking(g);
        test_reverse(g);
        test_wrong_way(g);
        test_landing_speed(g);
        test_offroad_motion(g);
        test_offroad_recovery(g);
        test_shortcuts(g);
        test_surface_contact(g);
    }
    g->selected = 0;
    game_start(g);
    g->screen = SCREEN_RACE;
    g->cars[0].fuel = 0;
    for (int i = 0; i < 120 * 5; i++)
        game_tick(g, (Controls){.throttle = 1}, FIXED_DT, false);
    CHECK(g->cars[0].dnf && g->screen == SCREEN_RESULTS, "fuel exhaustion resolves the race");
    game_start(g);
    g->screen = SCREEN_PAUSE;
    Vec3 p = g->cars[0].pos;
    float fuel = g->cars[0].fuel;
    for (int i = 0; i < 120; i++)
        game_tick(g, (Controls){.throttle = 1}, FIXED_DT, false);
    CHECK(g->time == 0 && vlen(vsub(p, g->cars[0].pos)) == 0 && g->cars[0].fuel == fuel,
          "pause freezes gameplay");
    game_start(g);
    g->screen = SCREEN_RACE;
    g->cars[0].progress = -2;
    g->cars[0].checkpoints = 0;
    game_reset_car(g, 0);
    CHECK(!g->cars[0].finished && g->cars[0].checkpoints == 0, "reset does not award checkpoints");
    game_start(g);
    g->screen = SCREEN_RACE;
    g->cars[0].pos = track_at(&g->tracks[0], -10, 8, NULL);
    g->cars[0].nearest = -1;
    g->cars[0].last_s =
        track_nearest(&g->tracks[0], g->cars[0].pos, &g->cars[0].nearest, NULL, NULL);
    for (int i = 0; i < 120; i++)
        game_tick(g, (Controls){.throttle = 1}, FIXED_DT, false);
    CHECK(g->cars[0].speed > 1, "a stopped car can accelerate off the road");
    printf("Simulation tests: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    free(g);
    return failures ? 1 : 0;
}
