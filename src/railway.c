#include "game.h"
#include <string.h>

// Barrier timing around each window in which a train occupies the crossing.
#define RAIL_WARN 5.5f  // Signals flash this long before the train arrives
#define RAIL_LOWER 4.f // Arms start to fall
#define RAIL_FALL 3.f
#define RAIL_HOLD .6f // Arms stay down after the last vehicle has passed
#define RAIL_RISE 2.5f
#define RAIL_HORN 5.5f // The driver sounds the horn this long before arriving
// Arms below half-way close the road.
#define RAIL_BLOCK (RAIL_LOWER - RAIL_FALL * .5f)
#define RAIL_UNBLOCK (RAIL_HOLD + RAIL_RISE * .5f)

typedef struct {
    int model;
    float front, back, half_width; // Model extents along +Z and across
} RailCar;
typedef struct {
    float speed, accel, dwell, gap_min, gap_max;
    int count;
    RailCar cars[MAX_RAIL_VEHICLES];
} Consist;
// Extents measured from the exported models; buffers and drawbars touch.
static const Consist CONSISTS[3] = {
    [RAIL_STEAM] = {15.f, 0, 0, 28, 48, 4,
                    {{RAIL_MODEL_STEAM_LOCOMOTIVE, 4.05f, -3.75f, 1.45f},
                     {RAIL_MODEL_STEAM_TENDER, 2.88f, -2.88f, 1.37f},
                     {RAIL_MODEL_COACH, 5.38f, -5.38f, 1.48f},
                     {RAIL_MODEL_COACH, 5.38f, -5.38f, 1.48f}}},
    [RAIL_ROAD_TRAIN] = {7.f, 2.2f, 34.f, 0, 0, 4,
                         {{RAIL_MODEL_ROAD_TRAIN_TRACTOR, 2.23f, -2.1f, 1.12f},
                          {RAIL_MODEL_ROAD_TRAIN_CARRIAGE, 2.9f, -2.5f, 1.15f},
                          {RAIL_MODEL_ROAD_TRAIN_CARRIAGE, 2.9f, -2.5f, 1.15f},
                          {RAIL_MODEL_ROAD_TRAIN_CARRIAGE, 2.9f, -2.5f, 1.15f}}},
    [RAIL_ALPINE] = {12.f, 0, 0, 30, 50, 3,
                     {{RAIL_MODEL_ALPINE_LOCOMOTIVE, 5.23f, -5.23f, 1.33f},
                      {RAIL_MODEL_ALPINE_COACH, 5.68f, -5.68f, 1.33f},
                      {RAIL_MODEL_ALPINE_COACH, 5.68f, -5.68f, 1.33f}}},
};

static float consist_length(const Consist *c) {
    float total = 0;
    for (int i = 0; i < c->count; i++)
        total += c->cars[i].front - c->cars[i].back;
    return total;
}
static float rail_random(RailState *r) {
    r->rng ^= r->rng << 13;
    r->rng ^= r->rng >> 17;
    r->rng ^= r->rng << 5;
    return (r->rng & 0xffff) / 65535.f;
}
static float wrap_path(float s, float length) {
    s = fmodf(s, length);
    return s < 0 ? s + length : s;
}

// A point on the route at a path distance; closed routes wrap around.
static Vec3 path_point(const Railway *r, float s) {
    if (r->loop)
        s = wrap_path(s, r->length);
    else
        s = clampf(s, 0, r->s[r->count - 1]);
    int lo = 0, hi = r->count - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (r->s[mid] <= s)
            lo = mid;
        else
            hi = mid - 1;
    }
    int next = lo + 1 < r->count ? lo + 1 : r->loop ? 0 : lo;
    float end = lo + 1 < r->count ? r->s[lo + 1] : r->length;
    float f = end > r->s[lo] ? clampf((s - r->s[lo]) / (end - r->s[lo]), 0, 1) : 0;
    return vlerp(r->points[lo], r->points[next], f);
}

/* Road train timetable: it waits at each stop, then accelerates, cruises and
   brakes to the next one. Distances are measured from the first stop. */
typedef struct {
    float distance[MAX_RAIL_STOPS], travel[MAX_RAIL_STOPS], dwell[MAX_RAIL_STOPS], period;
} Timetable;
static Timetable timetable(const Railway *r, const Consist *c, const float *dwell) {
    Timetable t = {0};
    for (int i = 0; i < r->stop_count; i++) {
        float d = wrap_path(r->stops[(i + 1) % r->stop_count] - r->stops[i], r->length);
        if (d < .01f)
            d = r->length;
        float ramp = c->speed * c->speed / c->accel;
        t.distance[i] = d;
        t.travel[i] = d >= ramp ? d / c->speed + c->speed / c->accel : 2 * sqrtf(d / c->accel);
        t.dwell[i] = dwell[i];
        t.period += t.travel[i] + t.dwell[i];
    }
    return t;
}
static float leg_distance(const Consist *c, float d, float travel, float time) {
    float top = fminf(c->speed, sqrtf(d * c->accel)), ramp = top / c->accel;
    if (time < ramp)
        return .5f * c->accel * time * time;
    if (time > travel - ramp)
        return d - .5f * c->accel * (travel - time) * (travel - time);
    return .5f * c->accel * ramp * ramp + top * (time - ramp);
}
static float leg_time(const Consist *c, float d, float travel, float x) {
    float top = fminf(c->speed, sqrtf(d * c->accel)), ramp = top / c->accel;
    float d_ramp = .5f * c->accel * ramp * ramp;
    if (x < d_ramp)
        return sqrtf(2 * fmaxf(0, x) / c->accel);
    if (x > d - d_ramp)
        return travel - sqrtf(2 * fmaxf(0, d - x) / c->accel);
    return ramp + (x - d_ramp) / top;
}
// Distance of the road train's nose from the first stop, unwrapped over laps.
static float loop_head(const Railway *r, const Consist *c, const float *dwell, float time) {
    Timetable t = timetable(r, c, dwell);
    float laps = floorf(time / t.period), local = time - laps * t.period;
    float x = laps * r->length;
    for (int i = 0; i < r->stop_count; i++) {
        if (local < t.dwell[i])
            return x;
        local -= t.dwell[i];
        if (local < t.travel[i])
            return x + leg_distance(c, t.distance[i], t.travel[i], local);
        local -= t.travel[i];
        x += t.distance[i];
    }
    return x;
}
// The first time the nose reaches an unwrapped distance from the first stop.
static float loop_time(const Railway *r, const Consist *c, const float *dwell, float x) {
    Timetable t = timetable(r, c, dwell);
    float laps = floorf(x / r->length), local = x - laps * r->length;
    float time = laps * t.period;
    for (int i = 0; i < r->stop_count; i++) {
        time += t.dwell[i];
        if (local <= t.distance[i])
            return time + leg_time(c, t.distance[i], t.travel[i], local);
        local -= t.distance[i];
        time += t.travel[i];
    }
    return time;
}

static const Consist *rail_consist(const Railway *r) {
    return &CONSISTS[r->kind];
}

// Windows in which any vehicle overlaps a pass's zone, with arrival and departure.
#define MAX_WINDOWS 8
static int rail_windows(const Game *g, float from, float to, float out[][2]) {
    const Railway *r = &g->tracks[g->selected].rail;
    const RailState *s = &g->rail;
    const Consist *c = rail_consist(r);
    float total = consist_length(c);
    int n = 0;
    if (!r->count)
        return 0;
    if (r->loop) {
        Timetable t = timetable(r, c, s->dwell);
        float first = floorf((from - s->phase) / t.period) - 1;
        for (float k = first; k <= first + 3 && n < MAX_WINDOWS; k++)
            for (int p = 0; p < r->pass_count && n < MAX_WINDOWS; p++) {
                float x = wrap_path(r->passes[p] - r->stops[0], r->length) + k * r->length;
                float enter = s->phase + loop_time(r, c, s->dwell, x - r->zone);
                float leave = s->phase + loop_time(r, c, s->dwell, x + r->zone + total);
                if (leave >= from && enter <= to) {
                    out[n][0] = enter;
                    out[n][1] = leave;
                    n++;
                }
            }
    } else {
        float runs[2] = {s->run, s->next};
        int directions[2] = {s->direction, s->next_direction};
        for (int k = 0; k < 2; k++) {
            float pass = directions[k] > 0 ? r->passes[0] : r->length - r->passes[0];
            float enter = runs[k] + (pass - r->zone) / c->speed;
            float leave = runs[k] + (pass + r->zone + total) / c->speed;
            if (leave >= from && enter <= to) {
                out[n][0] = enter;
                out[n][1] = leave;
                n++;
            }
        }
    }
    // Sort by arrival; the timetables never overlap two windows.
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && out[j][0] < out[j - 1][0]; j--) {
            float a = out[j][0], b = out[j][1];
            out[j][0] = out[j - 1][0];
            out[j][1] = out[j - 1][1];
            out[j - 1][0] = a;
            out[j - 1][1] = b;
        }
    return n;
}

/* The next period in which the arms close the road, from the time they pass
   half-way down until they are half-way up again. */
bool rail_window(const Game *g, float after, float *close, float *open) {
    float windows[MAX_WINDOWS][2];
    int n = rail_windows(g, after - 20, after + 240, windows);
    for (int i = 0; i < n; i++) {
        float a = windows[i][0] - RAIL_BLOCK, b = windows[i][1] + RAIL_UNBLOCK;
        if (b <= after)
            continue;
        // Back-to-back passes keep the arms down between them.
        while (i + 1 < n && windows[i + 1][0] - RAIL_BLOCK <= b)
            b = windows[++i][1] + RAIL_UNBLOCK;
        *close = a;
        *open = b;
        return true;
    }
    return false;
}

static float arm_at(const Game *g, float time, bool *warning) {
    float windows[MAX_WINDOWS][2], arm = 0;
    int n = rail_windows(g, time - 10, time + 10, windows);
    if (warning)
        *warning = false;
    for (int i = 0; i < n; i++) {
        float enter = windows[i][0], leave = windows[i][1];
        float a = time < leave + RAIL_HOLD
                      ? clampf((time - enter + RAIL_LOWER) / RAIL_FALL, 0, 1)
                      : clampf(1 - (time - leave - RAIL_HOLD) / RAIL_RISE, 0, 1);
        arm = fmaxf(arm, a);
        if (warning && time > enter - RAIL_WARN && time < leave + RAIL_HOLD + RAIL_RISE)
            *warning = true;
    }
    return arm;
}

float rail_arm(const Game *g, float alpha) {
    return lerpf(g->rail.prev_arm, g->rail.arm, alpha);
}

// Distance of the nose along its direction of travel; negative when no train runs.
static float rail_head(const Game *g, float time) {
    const Railway *r = &g->tracks[g->selected].rail;
    const Consist *c = rail_consist(r);
    if (r->loop)
        return wrap_path(r->stops[0], r->length) + loop_head(r, c, g->rail.dwell, time - g->rail.phase);
    float head = (time - g->rail.run) * c->speed;
    return head >= 0 && head <= r->length + consist_length(c) ? head : -1;
}

static void schedule_next(RailState *s, const Railway *r) {
    const Consist *c = rail_consist(r);
    float duration = (r->length + consist_length(c)) / c->speed;
    s->next = s->run + duration + c->gap_min + (c->gap_max - c->gap_min) * rail_random(s);
    s->next_direction = -s->direction;
}

void rail_reset(Game *g) {
    Track *t = &g->tracks[g->selected];
    const Railway *r = &t->rail;
    RailState *s = &g->rail;
    memset(s, 0, sizeof *s);
    t->barrier_count = 0;
    if (!r->count)
        return;
    s->rng = g->test_mode ? 51637u + (uint32_t)g->selected
                          : (uint32_t)SDL_GetPerformanceCounter() | 1u;
    // The clock runs from the countdown, so race time and railway time agree.
    s->time = g->screen == SCREEN_COUNTDOWN ? -g->countdown : 0;
    const Consist *c = rail_consist(r);
    for (int i = 0; i < MAX_RAIL_STOPS; i++)
        s->dwell[i] = c->dwell;
    if (r->loop) {
        Timetable tt = timetable(r, c, s->dwell);
        s->phase = -tt.period * rail_random(s);
        if (g->force_train > 0)
            s->phase = g->force_train -
                       loop_time(r, c, s->dwell, wrap_path(r->passes[0] - r->stops[0], r->length) - r->zone);
    } else {
        // The first train arrives after the field has passed on the opening lap.
        s->direction = rail_random(s) < .5f ? 1 : -1;
        float pass = s->direction > 0 ? r->passes[0] : r->length - r->passes[0];
        float arrive = g->force_train > 0 ? g->force_train : 26 + 18 * rail_random(s);
        s->run = arrive - (pass - r->zone) / c->speed;
        schedule_next(s, r);
    }
    s->arm = s->prev_arm = arm_at(g, s->time, &s->warning);
    s->head = s->prev_head = rail_head(g, s->time);
}

/* Poses of the vehicles between the previous and the current step. The road
   train's carriages follow the route at their own distance from its nose. */
int rail_vehicles(const Game *g, float alpha, RailVehicle *out) {
    const Railway *r = &g->tracks[g->selected].rail;
    const RailState *s = &g->rail;
    if (!r->count || s->head < 0)
        return 0;
    const Consist *c = rail_consist(r);
    float head = s->prev_head >= 0 ? lerpf(s->prev_head, s->head, alpha) : s->head;
    int direction = r->loop ? 1 : s->direction;
    float offset = 0;
    for (int i = 0; i < c->count; i++) {
        const RailCar *car = &c->cars[i];
        float origin = head - offset - car->front, span = (car->front - car->back) * .32f;
        offset += car->front - car->back;
        float a = direction > 0 ? origin + span : r->length - origin - span;
        float b = direction > 0 ? origin - span : r->length - origin + span;
        Vec3 front = path_point(r, a), back = path_point(r, b);
        Vec3 d = vsub(front, back);
        float horizontal = hypotf(d.x, d.z);
        RailVehicle *v = &out[i];
        v->pos = vmul(vadd(front, back), .5f);
        v->forward = horizontal > 1e-4f ? v3(d.x / horizontal, 0, d.z / horizontal) : v3(0, 0, 1);
        v->pitch = -atan2f(d.y, fmaxf(horizontal, 1e-4f));
        v->front = car->front;
        v->back = car->back;
        v->length = car->front - car->back;
        v->half_width = car->half_width;
        v->model = car->model;
        // Beyond the dark end of each tunnel nothing of it can be seen.
        float near = direction > 0 ? origin + car->back : r->length - origin - car->front;
        float far = near + v->length;
        v->visible = r->loop || (far > r->visible_start && near < r->visible_end);
    }
    return c->count;
}

// Trains have right of way: a car in the way is shoved clear of the rails.
static void rail_collide(Game *g, const RailVehicle *vehicles, int count) {
    float speed = g->rail.speed;
    for (int i = 0; i < count; i++) {
        const RailVehicle *v = &vehicles[i];
        if (!v->visible)
            continue;
        Vec3 f = v->forward, right = v3(f.z, 0, -f.x);
        Vec3 center = vadd(v->pos, vmul(f, (v->front + v->back) * .5f));
        float half_length = v->length * .5f;
        Vec3 velocity = vmul(f, speed);
        for (int j = 0; j < CAR_COUNT; j++) {
            Car *c = &g->cars[j];
            if (c->dnf || c->reset_protection > 0 || fabsf(c->pos.y - v->pos.y) > 3.5f ||
                vlen(vsub(c->pos, center)) > half_length + 6)
                continue;
            Vec3 cf = v3(sinf(c->yaw) * .72f, 0, cosf(c->yaw) * .72f);
            for (int end = -1; end <= 1; end += 2) {
                Vec3 p = vadd(c->pos, vmul(cf, (float)end)), q = vsub(p, center);
                float x = vdot(q, right), z = vdot(q, f), radius = .85f;
                float cx = clampf(x, -v->half_width, v->half_width);
                float cz = clampf(z, -half_length, half_length);
                float dx = x - cx, dz = z - cz, distance = hypotf(dx, dz), depth;
                Vec3 n;
                if (distance >= radius)
                    continue;
                if (distance > 1e-4f) {
                    n = vadd(vmul(right, dx / distance), vmul(f, dz / distance));
                    depth = radius - distance;
                } else if (v->half_width - fabsf(x) < half_length - fabsf(z)) {
                    n = vmul(right, x < 0 ? -1.f : 1.f);
                    depth = v->half_width - fabsf(x) + radius;
                } else {
                    n = vmul(f, z < 0 ? -1.f : 1.f);
                    depth = half_length - fabsf(z) + radius;
                }
                // The nose sweeps a car aside rather than pushing it along.
                if (vdot(n, f) > .5f)
                    n = vnorm(vadd(n, vmul(right, x < 0 ? -.9f : .9f)));
                c->pos = vadd(c->pos, vmul(n, depth));
                Vec3 relative = vsub(c->velocity, velocity);
                relative.y = 0;
                float closing = vdot(relative, n);
                if (closing >= 0)
                    continue;
                Vec3 impulse = vmul(n, -closing * 1.3f);
                c->velocity = vadd(c->velocity, impulse);
                c->speed = hypotf(c->velocity.x, c->velocity.z);
                car_impact(c, impulse, v3(0, -.25f, 0));
                if (-closing > 2) {
                    game_sound(g, SOUND_TRAIN_HIT, j, -closing, p);
                    if (j == 0)
                        g->shake = fmaxf(g->shake, .25f);
                }
            }
        }
    }
}

// Arms, signals and the train's position at the railway clock.
static void rail_advance(Game *g, float dt) {
    RailState *s = &g->rail;
    s->prev_arm = s->arm;
    s->arm = arm_at(g, s->time, &s->warning);
    float head = rail_head(g, s->time);
    s->prev_head = s->head >= 0 && head >= 0 && head >= s->head ? s->head : head;
    s->head = head;
    s->speed = head >= 0 && dt > 0 ? (s->head - s->prev_head) / dt : 0;
    const Railway *r = &g->tracks[g->selected].rail;
    RailVehicle vehicles[MAX_RAIL_VEHICLES];
    if (r->kind == RAIL_STEAM && rail_vehicles(g, 1, vehicles) && vehicles[0].visible &&
        fmodf(s->time, .12f) < dt) {
        // Puffs from the chimney, carried along a little by the train.
        Vec3 f = vehicles[0].forward;
        Vec3 chimney = vadd(vehicles[0].pos, v3(f.x * 2.85f, 4.f, f.z * 2.85f));
        game_smoke(g, chimney, vmul(f, s->speed * .25f));
    }
}

/* The broadcast pack never slows down, so its train is timed around it: the
   arms are up again just before the leader's camera reaches the barrier, on
   every other lap (every lap for the road train, which crosses twice). */
void rail_broadcast(Game *g) {
    Track *t = &g->tracks[g->selected];
    const Railway *r = &t->rail;
    RailState *s = &g->rail;
    t->barrier_count = 0;
    if (!r->count)
        return;
    const Consist *c = rail_consist(r);
    float dt = g->clock - s->time, lap = t->length / BROADCAST_SPEED, total = consist_length(c);
    bool fresh = dt < 0 || dt > .5f || s->track != g->selected;
    if (fresh) {
        // A new broadcast or circuit: nothing to interpolate from.
        s->head = s->prev_head = -1;
        dt = 0;
    }
    s->time = g->clock;
    s->track = g->selected;
    // When the leader, with its camera 10 m ahead, reaches the barrier on lap k.
    float first = (r->stop_s + 2.2f - BROADCAST_START - 10) / BROADCAST_SPEED;
    float duration = (2 * r->zone + total) / c->speed, clear = 1 + RAIL_HOLD + RAIL_RISE;
    if (r->loop) {
        // Pass zero's window is centred one lap after pass one's.
        float zero[2] = {0, 0}, centre[2];
        for (int p = 0; p < 2; p++) {
            float x = wrap_path(r->passes[p] - r->stops[0], r->length);
            centre[p] = (loop_time(r, c, zero, x - r->zone) + loop_time(r, c, zero, x + r->zone + total)) * .5f;
        }
        Timetable travel = timetable(r, c, zero);
        s->dwell[1] = fmaxf(2, lap - (centre[1] - centre[0]));
        s->dwell[0] = fmaxf(2, 2 * lap - travel.period - s->dwell[1]);
        float target = first - clear - duration * .5f;
        s->phase = target - s->dwell[0] - centre[0];
    } else {
        // The first even lap whose arms have not yet risen, then the one after.
        float k = 2 * ceilf((g->clock + 1 - first) / (2 * lap));
        for (int i = 0; i < 2; i++, k += 2) {
            int direction = fmodf(k * .5f, 2) < 1 ? 1 : -1;
            float pass = direction > 0 ? r->passes[0] : r->length - r->passes[0];
            float run = first + k * lap - clear - duration - (pass - r->zone) / c->speed;
            if (i == 0) {
                s->run = run;
                s->direction = direction;
            } else {
                s->next = run;
                s->next_direction = direction;
            }
        }
    }
    rail_advance(g, dt);
    if (fresh)
        s->prev_arm = s->arm;
}

void rail_tick(Game *g, float dt) {
    Track *t = &g->tracks[g->selected];
    const Railway *r = &t->rail;
    RailState *s = &g->rail;
    if (!r->count)
        return;
    s->time += dt;
    if (!r->loop) {
        const Consist *c = rail_consist(r);
        // Wait for the arms to rise behind the train before the next run.
        float duration = (r->length + consist_length(c)) / c->speed + RAIL_HOLD + RAIL_RISE;
        if (s->time > s->run + duration) {
            s->run = s->next;
            s->direction = s->next_direction;
            s->horn = false;
            schedule_next(s, r);
        }
    }
    rail_advance(g, dt);
    // Lowered arms close the road to every car.
    t->barrier_count = 0;
    for (int i = 0; s->arm > .7f && i < r->arm_count; i++) {
        const RailArm *a = &r->arms[i];
        Vec3 base = v3(a->pivot.x, a->pivot.y - 1.f, a->pivot.z);
        Vec3 tip = vadd(base, v3(a->dx * a->length, 0, a->dz * a->length));
        base.y = track_height(t, vlerp(base, tip, .1f), NULL);
        tip.y = track_height(t, tip, NULL);
        t->barriers[t->barrier_count++] = (Guardrail){base, tip};
    }
    RailVehicle vehicles[MAX_RAIL_VEHICLES];
    int count = rail_vehicles(g, 1, vehicles);
    // The driver sounds the horn on the approach to the crossing, from inside
    // the tunnel if need be.
    float close, open;
    if (!s->horn && rail_window(g, s->time, &close, &open) &&
        close + RAIL_BLOCK - s->time < RAIL_HORN) {
        s->horn = true;
        int direction = r->loop ? 1 : s->direction;
        Vec3 at = count ? vehicles[0].pos : path_point(r, direction > 0 ? 0 : r->length);
        game_sound(g, SOUND_TRAIN_HORN, -1, 1, at);
    }
    if (r->loop && s->horn && s->arm <= 0 && !s->warning)
        s->horn = false;
    if (count)
        rail_collide(g, vehicles, count);
}
