#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_SURFACE_HEIGHT -100.f

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

void game_start(Game *g) {
    g->screen = SCREEN_COUNTDOWN;
    g->time = 0;
    g->countdown = 3.4f;
    g->rank = 8;
    g->finish_rank = 0;
    g->toast_time = 0;
    g->wrong_way = 0;
    g->shake = 0;
    g->last_lap = 0;
    memset(g->particles, 0, sizeof g->particles);
    g->particle_cursor = 0;
    g->rng = 81291 + g->selected;
    const char *names[] = {"YOU", "JUNO", "DASH", "PIP", "NOVA", "ACE", "RUSTY", "BEAN"};
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
        c->lap = 1;
        c->target_lane = (i % 3 - 1) * 2.6f;
        c->color = i == 6   ? rgb(.24f, .30f, .33f)
                   : i == 7 ? rgb(.43f, .65f, .80f)
                            : CAR_COLORS[i == 0 ? g->profile.color : (i + g->profile.color) % 6];
        c->name = names[i];
    }
    g->sound_event = 1;
}

void game_reset_car(Game *g, int index) {
    Car *c = &g->cars[index];
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
    c->nearest = -1;
    c->last_s = track_nearest(t, c->pos, &c->nearest, NULL, NULL);
    if (index == 0) {
        SDL_strlcpy(g->toast, "BACK ON TRACK", sizeof g->toast);
        g->toast_time = 1.5f;
    }
}

void game_ai(const Game *g, int index, Controls *out) {
    const Track *t = &g->tracks[g->selected];
    const Car *c = &g->cars[index];
    *out = (Controls){0};
    float lane = c->target_lane, look = 5.5f + c->speed * .34f;
    // Plan a fuel line early. Each driver has an independent respawn timer.
    float near = 1000;
    for (int j = 0; j < t->can_count; j++) {
        float d = wrap(t->points[(int)t->cans[j].index].s - c->last_s, t->length);
        if (d < near && d < 48 && c->can_cooldown[j] <= 0 && (c->fuel < 68 || index == 0)) {
            near = d;
            lane = t->cans[j].lane;
        }
    }
    if (near > 30) {
        for (int j = 0; j < CAR_COUNT; j++)
            if (j != index) {
                const Car *other = &g->cars[j];
                float d = other->progress - c->progress;
                if (d > 0 && d < 10 && vlen(vsub(c->pos, other->pos)) < 11)
                    lane = index % 2 ? 3.0f : -3.0f;
            }
    }
    Vec3 target = track_at(t, c->last_s + look, lane, NULL);
    float angle = angle_delta(atan2f(target.x - c->pos.x, target.z - c->pos.z), c->yaw);
    out->steer = clampf(-angle * 2.15f, -1, 1);
    Vec3 d0, d1;
    track_at(t, c->last_s + 4, 0, &d0);
    track_at(t, c->last_s + 22, 0, &d1);
    float curve = fabsf(angle_delta(atan2f(d1.x, d1.z), atan2f(d0.x, d0.z)));
    float desired = (27.5f - curve * 13) * (g->profile.difficulty == 0   ? .82f
                                            : g->profile.difficulty == 2 ? 1.07f
                                                                         : .96f);
    desired *= 1.0f - (index % 4) * .016f;
    desired = clampf(desired, 10, 30);
    out->throttle = c->speed < desired ? 1 : .15f;
    out->brake = c->speed > desired + 1.5f ? .55f : 0;
}

static void particle(Game *g, Vec3 p, Vec3 vel, float life, float size, Color color) {
    Particle *q = &g->particles[g->particle_cursor++ % MAX_PARTICLES];
    *q = (Particle){p, vel, life, life, size, color};
}
static void toast(Game *g, const char *s, float seconds) {
    SDL_strlcpy(g->toast, s, sizeof g->toast);
    g->toast_time = seconds;
}

static void update_lap(Game *g, int index) {
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

    car->lap = lap;
    car->lap_time = g->time - car->lap_start;
    car->lap_start = g->time;
    if (car->best_lap == 0 || car->lap_time < car->best_lap) {
        car->best_lap = car->lap_time;
    }
    if (index == 0 && lap <= RACE_LAPS) {
        g->last_lap = car->lap_time;
        toast(g, "FINAL LAP", 2);
        g->sound_event = 3;
    }
}

static void update_car(Game *g, int index, Controls in, float dt) {
    Track *t = &g->tracks[g->selected];
    Car *c = &g->cars[index];
    c->prev = c->pos;
    c->prev_yaw = c->yaw;
    if (c->finished || c->dnf)
        return;
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
    if (distance > t->points[nearest].half + .5f) {
        nearest = -1;
        track_nearest(t, c->pos, &nearest, &center, NULL);
        distance = hypotf(c->pos.x - center.x, c->pos.z - center.z);
    }
    // Terrain slows the car without pulling it toward the road or making
    // distant ground progressively harder to drive across.
    float off = clampf(distance - t->points[nearest].half, 0, 4);
    float grip = g->selected == 2 ? 5.8f : g->selected == 1 ? 7.4f : 9.2f;
    if (in.drift)
        grip *= .43f;
    Vec3 forward = v3(sinf(c->yaw), 0, cosf(c->yaw)), right = v3(forward.z, 0, -forward.x);
    float longitudinal = vdot(c->velocity, forward), side = vdot(c->velocity, right);
    float throttle = c->fuel > 0 ? in.throttle : 0;
    float accel = throttle * (off > .5f ? 9.f : 14.2f) - in.brake * 24.f - .70f -
                  longitudinal * longitudinal * .012f - off * longitudinal * .40f;
    if (!c->grounded)
        accel = -.8f;
    longitudinal = fmaxf(0, longitudinal + accel * dt);
    side *= expf(-grip * dt * (c->grounded ? 1 : .15f));
    float steer_rate = (.38f + fminf(longitudinal, 23) * .060f) * (in.drift ? 1.3f : 1);
    // With +Z forward and +Y up, positive yaw turns left in the chase camera.
    c->yaw -=
        in.steer * steer_rate * clampf(longitudinal * .35f, 0, 1) * dt * (c->grounded ? 1 : .23f);
    c->velocity = vadd(vmul(forward, longitudinal), vmul(right, side));
    c->speed = vlen(c->velocity);
    c->pos = vadd(c->pos, vmul(c->velocity, dt));
    bool was_ramp = false, is_ramp = false;
    float old_ground = track_height(t, c->prev, &was_ramp);
    float ground = track_height(t, c->pos, &is_ramp);
    if (ground == NO_SURFACE_HEIGHT) {
        game_reset_car(g, index);
        return;
    }
    // Leave the wedge's lip with velocity derived from its physical slope.
    if (c->grounded && was_ramp && !is_ramp && c->speed > 9 && c->ramp_cooldown <= 0) {
        c->grounded = false;
        c->vy = c->speed * .265f + 1.3f;
        c->pos.y = old_ground;
        c->jumps++;
        c->ramp_cooldown = 1.5f;
        c->air_time = 0;
        if (index == 0) {
            toast(g, "TAKE FLIGHT!", 1.2f);
            g->sound_event = 4;
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
            if (index == 0) {
                g->shake = .23f;
                g->sound_event = 5;
            }
        }
    }
    if (c->grounded)
        car_surface_pose(t, &c->pos, c->yaw, &c->pitch, &c->roll);
    else
        c->roll = lerpf(c->roll, 0, dt * 8);
    float s = track_nearest(t, c->pos, &c->nearest, NULL, NULL), delta = s - c->last_s;
    if (delta > t->length * .5f)
        delta -= t->length;
    if (delta < -t->length * .5f)
        delta += t->length;
    if (fabsf(delta) < 12)
        c->progress += delta;
    c->last_s = s;
    update_lap(g, index);
    if (c->progress >= t->length * RACE_LAPS && c->checkpoints >= RACE_LAPS * 4) {
        c->finished = true;
        c->finish_time = g->time;
        if (index == 0)
            game_results(g);
        return;
    }
    c->fuel = fmaxf(0, c->fuel - (.65f + throttle * .45f + c->speed * .038f) * dt);
    for (int j = 0; j < t->can_count; j++)
        if (c->can_cooldown[j] <= 0) {
            const Can *can = &t->cans[j];
            Vec3 cp = track_at(t, t->points[(int)can->index].s, can->lane, NULL);
            float dx = c->pos.x - cp.x, dz = c->pos.z - cp.z;
            if (dx * dx + dz * dz < 2.35f * 2.35f && fabsf(c->pos.y - cp.y) < 2.8f) {
                c->can_cooldown[j] = 22;
                c->fuel = fminf(100, c->fuel + 26);
                c->collected++;
                if (index == 0) {
                    toast(g, "+26 FUEL", 1.3f);
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
            if (index == 0)
                game_results(g);
        }
    }
    if (c->speed < 1 && in.throttle > .5f && c->fuel > 0)
        c->stuck_time += dt;
    else
        c->stuck_time = 0;
    if (index > 0 && c->stuck_time > 3)
        game_reset_car(g, index);
    float dust_rate = (off > .4f || fabsf(side) > 2) ? 24.f : g->selected == 0 ? 5.f : 12.f;
    if (c->grounded && c->speed > 6 && random01(g) < dt * dust_rate) {
        Color color = g->selected == 2   ? rgb(.91, .95, .96)
                      : g->selected == 1 ? rgb(.80, .70, .48)
                                         : rgb(.58, .57, .38);
        particle(
            g, vadd(c->pos, vmul(forward, -1.2f)),
            vadd(vmul(forward, -1.4f), v3((random01(g) - .5f) * 2, .8f, (random01(g) - .5f) * 2)),
            .55f, .09f + fabsf(side) * .03f, color);
    }
}

void game_results(Game *g) {
    if (g->screen == SCREEN_RESULTS)
        return;
    g->screen = SCREEN_RESULTS;
    Car *p = &g->cars[0];
    g->finish_rank = 1;
    for (int j = 1; j < CAR_COUNT; j++)
        if (g->cars[j].finished && (!p->finished || g->cars[j].finish_time <= p->finish_time))
            g->finish_rank++;
    if (p->dnf)
        g->finish_rank = 8;
    // Demonstrations must not change the profile in memory either: changing a
    // setting later saves the entire profile, including its records.
    if (p->finished && !g->test_mode) {
        float *best = &g->profile.best[g->selected];
        if (*best == 0 || p->finish_time < *best)
            *best = p->finish_time;
        int medal = g->finish_rank <= 3 ? 4 - g->finish_rank : 0;
        if (medal > g->profile.medals[g->selected])
            g->profile.medals[g->selected] = medal;
        profile_save(&g->profile);
    }
    g->sound_event = 7;
}

void game_tick(Game *g, Controls controls, float dt, bool autopilot) {
    g->clock += dt;
    g->toast_time = fmaxf(0, g->toast_time - dt);
    g->shake = fmaxf(0, g->shake - dt);
    for (int j = 0; j < MAX_PARTICLES; j++) {
        Particle *p = &g->particles[j];
        if (p->life > 0) {
            p->life -= dt;
            p->pos = vadd(p->pos, vmul(p->vel, dt));
            p->vel.y -= 2 * dt;
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
        bool remaining = false;
        for (int i = 1; i < CAR_COUNT; i++)
            if (!g->cars[i].finished && !g->cars[i].dnf)
                remaining = true;
        if (remaining) {
            g->time += dt;
            for (int i = 1; i < CAR_COUNT; i++) {
                Controls ai;
                game_ai(g, i, &ai);
                update_car(g, i, ai, dt);
                if (g->time > 220 && !g->cars[i].finished)
                    g->cars[i].dnf = true;
            }
        }
        return;
    }
    if (g->screen != SCREEN_RACE)
        return;
    g->time += dt;
    for (int i = 0; i < CAR_COUNT; i++) {
        Controls in = controls;
        if (i > 0 || autopilot)
            game_ai(g, i, &in);
        update_car(g, i, in, dt);
    }
    for (int i = 0; i < CAR_COUNT; i++)
        for (int j = i + 1; j < CAR_COUNT; j++) {
            Car *a = &g->cars[i], *b = &g->cars[j];
            if (a->finished || b->finished || a->dnf || b->dnf || fabsf(a->pos.y - b->pos.y) > 1.5f)
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
        if (c->grounded && !c->finished && !c->dnf)
            car_surface_pose(&g->tracks[g->selected], &c->pos, c->yaw, &c->pitch, &c->roll);
    }
    g->rank = 1;
    for (int j = 1; j < CAR_COUNT; j++)
        if (g->cars[j].progress > g->cars[0].progress)
            g->rank++;
    Car *p = &g->cars[0];
    TrackPoint tp = g->tracks[g->selected].points[p->nearest];
    if (p->speed > 3 && (p->velocity.x * tp.dx + p->velocity.z * tp.dz) < -1)
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

static void test_place_player(Game *g, float progress, float speed, bool reverse) {
    Car *car = &g->cars[0];
    const Track *track = &g->tracks[g->selected];
    Vec3 direction;
    car->progress = progress;
    car->pos = track_at(track, progress, 0, &direction);
    car->prev = car->pos;
    car->nearest = -1;
    car->last_s = track_nearest(track, car->pos, &car->nearest, NULL, NULL);
    car->yaw = atan2f(direction.x, direction.z) + (reverse ? PI : 0);
    car->prev_yaw = car->yaw;
    car->velocity = v3(sinf(car->yaw) * speed, 0, cosf(car->yaw) * speed);
    car->speed = speed;
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
    CHECK(fabsf(first_lap - g->time) < .001f, "first lap keeps its complete elapsed time");

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
    CHECK(fabsf(car->lap_time - (g->time - lap_start)) < .001f && car->best_lap > 39,
          "the final lap includes time spent recrossing and recovering");
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
                        CHECK(vlen(vsub(displacement, vmul(car->velocity, FIXED_DT))) < .0001f,
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

int game_tests(void) {
    Game *g = calloc(1, sizeof *g);
    if (!g || !game_load(g)) {
        fprintf(stderr, "Cannot load test assets\n");
        free(g);
        return 1;
    }
    g->test_mode = true;
    test_surface_slopes();
    for (int k = 0; k < 3; k++) {
        g->selected = k;
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
            CHECK(g->cars[0].collected >= 4, "fuel pickup route works");
            CHECK(g->cars[0].jumps >= 3, "physical jumps work");
            CHECK(isfinite(g->cars[0].pos.x), "finite simulation");
            int rivals = 0;
            for (int i = 1; i < 8; i++) {
                CHECK(!g->cars[i].dnf, "AI manages fuel");
                if (g->cars[i].progress > t->length * 1.4f)
                    rivals++;
            }
            CHECK(rivals >= 5, "competitive AI field");
        }
        test_steering_direction(g);
        test_lap_timing(g);
        test_landing_speed(g);
        test_offroad_motion(g);
        test_offroad_recovery(g);
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
