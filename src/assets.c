#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *asset_read(const char *name, size_t *size) {
    char path[2048];
#ifdef SDL_PLATFORM_ANDROID
    // SDL maps relative paths directly into the APK's AssetManager root.
    void *apk = SDL_LoadFile(name, size);
    if (apk)
        return apk;
#endif
    const char *base = SDL_GetBasePath();
    if (base) {
        SDL_snprintf(path, sizeof path, "%sassets/%s", base, name);
        void *d = SDL_LoadFile(path, size);
        if (d)
            return d;
    }
    SDL_snprintf(path, sizeof path, "assets/%s", name);
    void *d = SDL_LoadFile(path, size);
    if (d)
        return d;
    SDL_snprintf(path, sizeof path, "../assets/%s", name);
    return SDL_LoadFile(path, size);
}

static uint32_t read_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float read_float(const unsigned char *p) {
    uint32_t n = read_u32(p);
    float v;
    memcpy(&v, &n, 4);
    return v;
}

static bool load_guardrails(Track *t) {
    char name[100];
    SDL_snprintf(name, sizeof name, "tracks/%s.tcg", t->id);
    size_t size;
    unsigned char *data = asset_read(name, &size);
    if (!data) return false;
    bool valid = size >= 8 && !memcmp(data, "TCG1", 4);
    uint32_t count = valid ? read_u32(data + 4) : 0;
    valid = valid && count <= MAX_POINTS && size == 8 + (size_t)count * 24;
    for (uint32_t i = 0; valid && i < count; i++) {
        float v[6];
        for (int j = 0; j < 6; j++) {
            v[j] = read_float(data + 8 + i * 24 + j * 4);
            if (!isfinite(v[j])) valid = false;
        }
        t->guardrails[i] = (Guardrail){v3(v[0], v[1], v[2]), v3(v[3], v[4], v[5])};
        if (hypotf(v[3] - v[0], v[5] - v[2]) < .001f) valid = false;
    }
    if (valid) t->guardrail_count = (int)count;
    SDL_free(data);
    return valid;
}

// TCR1: the crossing's route, its timetable stops and the barrier hardware.
static bool load_railway(Track *t) {
    char name[100];
    SDL_snprintf(name, sizeof name, "tracks/%s.tcr", t->id);
    size_t size;
    unsigned char *data = asset_read(name, &size);
    if (!data) return false;
    Railway *r = &t->rail;
    uint32_t v[7] = {0};
    bool valid = size >= 56 && !memcmp(data, "TCR1", 4);
    for (int i = 0; valid && i < 7; i++) v[i] = read_u32(data + 4 + i * 4);
    valid = valid && v[0] <= RAIL_FREIGHT && v[1] <= 1 && v[2] >= 8 && v[2] <= MAX_RAIL_POINTS &&
            v[3] >= 1 && v[3] <= MAX_RAIL_PASSES && v[4] <= MAX_RAIL_STOPS &&
            v[5] <= MAX_RAIL_ARMS && v[6] <= MAX_RAIL_LIGHTS &&
            size == 56 + (size_t)v[2] * 12 + (v[3] + v[4]) * 4 + (v[5] + v[6]) * 24;
    float f[6] = {0};
    for (int i = 0; valid && i < 6; i++) {
        f[i] = read_float(data + 32 + i * 4);
        valid = isfinite(f[i]);
    }
    size_t off = 56;
    for (uint32_t i = 0; valid && i < v[2]; i++, off += 12) {
        r->points[i] = v3(read_float(data + off), read_float(data + off + 4), read_float(data + off + 8));
        valid = isfinite(r->points[i].x) && isfinite(r->points[i].y) && isfinite(r->points[i].z) &&
                fabsf(r->points[i].x) < -SURFACE_ORIGIN && fabsf(r->points[i].z) < -SURFACE_ORIGIN;
        r->s[i] = i ? r->s[i - 1] + vlen(vsub(r->points[i], r->points[i - 1])) : 0;
    }
    if (valid) {
        r->count = (int)v[2];
        r->length = r->s[r->count - 1] +
                    (v[1] ? vlen(vsub(r->points[0], r->points[r->count - 1])) : 0);
    }
    float *lists[2] = {r->passes, r->stops};
    for (int list = 0; valid && list < 2; list++)
        for (uint32_t i = 0; valid && i < v[3 + list]; i++, off += 4) {
            lists[list][i] = read_float(data + off);
            valid = isfinite(lists[list][i]) && lists[list][i] >= 0 && lists[list][i] <= r->length;
        }
    for (uint32_t i = 0; valid && i < v[5] + v[6]; i++, off += 24) {
        float a[6];
        for (int j = 0; j < 6; j++) {
            a[j] = read_float(data + off + j * 4);
            valid = valid && isfinite(a[j]);
        }
        if (i < v[5])
            r->arms[i] = (RailArm){v3(a[0], a[1], a[2]), a[3], a[4], a[5]};
        else
            r->lights[i - v[5]] = (RailLight){v3(a[0], a[1], a[2]), a[3], a[4], a[5]};
    }
    valid = valid && f[3] > 0 && f[4] >= 0 && f[5] <= r->length && f[4] < f[5];
    if (valid) {
        r->kind = (RailKind)v[0];
        r->loop = v[1];
        r->pass_count = (int)v[3];
        r->stop_count = (int)v[4];
        r->arm_count = (int)v[5];
        r->light_count = (int)v[6];
        r->road_s = f[0];
        r->stop_s = f[1];
        r->clear_s = f[2];
        r->zone = f[3];
        r->visible_start = f[4];
        r->visible_end = f[5];
    } else {
        memset(r, 0, sizeof *r);
        SDL_Log("Invalid railway: %s", name);
    }
    SDL_free(data);
    return valid;
}

bool track_alloc_surface(Track *t, int triangles, int refs) {
    SDL_free(t->surface);
    SDL_free(t->surface_refs);
    t->surface = SDL_calloc((size_t)triangles, sizeof *t->surface);
    t->surface_refs = SDL_calloc((size_t)refs, sizeof *t->surface_refs);
    t->surface_count = 0;
    return t->surface && t->surface_refs;
}

static bool load_surface(Track *t, bool water) {
    uint32_t limit = water ? MAX_WATER_TRIANGLES : MAX_SURFACE_TRIANGLES;
    size_t stride = water ? 120 : 36;
    char name[100];
    SDL_snprintf(name, sizeof name, water ? "tracks/%s-water.tcm" : "tracks/%s.tcs", t->id);
    size_t size;
    unsigned char *data = asset_read(name, &size);
    if (!data || size < 8 || memcmp(data, water ? "TCM1" : "TCS1", 4)) {
        SDL_free(data);
        SDL_Log("Missing or invalid %s surface: %s", water ? "water" : "driving", name);
        return false;
    }
    uint32_t count = read_u32(data + 4);
    if (water) {
        if (count % 3) {
            SDL_free(data);
            return false;
        }
        count /= 3;
    }
    if (!count || count > limit || size != 8 + count * stride) {
        SDL_free(data);
        return false;
    }
    SurfaceTriangle *triangles;
    if (water) {
        SDL_free(t->water);
        t->water = triangles = SDL_calloc(count, sizeof *t->water);
    } else {
        triangles = track_alloc_surface(t, (int)count, 1) ? t->surface : NULL;
    }
    if (!triangles) {
        SDL_free(data);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        float v[9];
        for (int j = 0; j < 9; j++) {
            v[j] = read_float(data + 8 + i * stride + (water ? (j / 3) * 40 + (j % 3) * 4 : j * 4));
            if (!isfinite(v[j]) || fabsf(v[j]) > -SURFACE_ORIGIN) {
                SDL_free(data);
                return false;
            }
        }
        triangles[i] =
            (SurfaceTriangle){v3(v[0], v[1], v[2]), v3(v[3], v[4], v[5]), v3(v[6], v[7], v[8])};
    }
    SDL_free(data);
    if (water)
        t->water_count = (int)count;
    else
        t->surface_count = (int)count;
    uint32_t *cells = water ? t->water_cells : t->surface_cells;
    // Index triangles by their XZ bounds once, keeping tire queries local.
    // Ground queries also accept points within the seam tolerance of a triangle.
    static uint32_t cursor[SURFACE_GRID * SURFACE_GRID];
    memset(cursor, 0, sizeof cursor);
    float pad = water ? 0 : SURFACE_SEAM;
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < count; i++) {
            SurfaceTriangle f = triangles[i];
            int x0 = surface_cell(fminf(f.a.x, fminf(f.b.x, f.c.x)) - pad);
            int x1 = surface_cell(fmaxf(f.a.x, fmaxf(f.b.x, f.c.x)) + pad);
            int z0 = surface_cell(fminf(f.a.z, fminf(f.b.z, f.c.z)) - pad);
            int z1 = surface_cell(fmaxf(f.a.z, fmaxf(f.b.z, f.c.z)) + pad);
            for (int z = z0; z <= z1; z++)
                for (int x = x0; x <= x1; x++) {
                    int cell = z * SURFACE_GRID + x;
                    if (pass == 0)
                        cursor[cell]++;
                    else if (water)
                        t->water_refs[cursor[cell]++] = (uint16_t)i;
                    else
                        t->surface_refs[cursor[cell]++] = i;
                }
        }
        if (pass == 0) {
            cells[0] = 0;
            for (int cell = 0; cell < SURFACE_GRID * SURFACE_GRID; cell++) {
                cells[cell + 1] = cells[cell] + cursor[cell];
                cursor[cell] = cells[cell];
            }
            uint32_t refs = cells[SURFACE_GRID * SURFACE_GRID];
            if (refs > (water ? MAX_WATER_REFS : MAX_SURFACE_REFS))
                return false;
            if (water) {
                SDL_free(t->water_refs);
                t->water_refs = SDL_calloc(refs ? refs : 1, sizeof *t->water_refs);
                if (!t->water_refs)
                    return false;
            } else {
                SDL_free(t->surface_refs);
                t->surface_refs = SDL_calloc(refs ? refs : 1, sizeof *t->surface_refs);
                if (!t->surface_refs)
                    return false;
            }
        }
    }
    if (!water) {
        // The island's extent, for recovery off its edge and for tests.
        t->extent[0] = t->extent[2] = 1e9f;
        t->extent[1] = t->extent[3] = -1e9f;
        for (uint32_t i = 0; i < count; i++) {
            const Vec3 *v = &t->surface[i].a;
            for (int k = 0; k < 3; k++) {
                t->extent[0] = fminf(t->extent[0], v[k].x);
                t->extent[1] = fmaxf(t->extent[1], v[k].x);
                t->extent[2] = fminf(t->extent[2], v[k].z);
                t->extent[3] = fmaxf(t->extent[3], v[k].z);
            }
        }
    }
    return true;
}

void game_free(Game *g) {
    for (int k = 0; k < TRACK_COUNT; k++) {
        Track *t = &g->tracks[k];
        SDL_free(t->surface);
        SDL_free(t->surface_refs);
        SDL_free(t->water);
        SDL_free(t->water_refs);
        t->surface = t->water = NULL;
        t->surface_refs = NULL;
        t->water_refs = NULL;
    }
}

bool game_load(Game *g) {
    game_free(g);
    memset(g, 0, sizeof *g);
    g->rng = 81291;
    for (int k = 0; k < TRACK_COUNT; k++) {
        Track *t = &g->tracks[k];
        const TrackInfo *info = &TRACK_INFO[k];
        t->info = info;
        t->id = info->id;
        t->name = info->name;
        t->subtitle = info->subtitle;
        t->description = info->description;
        t->accent = info->accent;
        t->sky = info->sky;
        char name[100];
        SDL_snprintf(name, sizeof name, "tracks/%s.tcp", t->id);
        size_t size;
        unsigned char *d = asset_read(name, &size);
        if (!d || size < 20 || memcmp(d, "TCP1", 4)) {
            SDL_free(d);
            SDL_Log("Missing or invalid track: %s", name);
            return false;
        }
        uint32_t n = read_u32(d + 4), nc = read_u32(d + 8), nr = read_u32(d + 12);
        if (n < 32 || n > MAX_POINTS || nc > MAX_CANS || nr > MAX_RAMPS ||
            size != 20 + n * 28 + nc * 8 + nr * 16) {
            SDL_free(d);
            return false;
        }
        t->count = (int)n;
        t->can_count = (int)nc;
        t->ramp_count = (int)nr;
        t->length = read_float(d + 16);
        if (!isfinite(t->length) || t->length < 100 || t->length > 10000) {
            SDL_free(d);
            return false;
        }
        size_t off = 20;
        float lo = 10000, hi = -10000;
        for (int i = 0; i < t->count; i++) {
            float v[7];
            for (int j = 0; j < 7; j++, off += 4) {
                v[j] = read_float(d + off);
                if (!isfinite(v[j])) {
                    SDL_free(d);
                    return false;
                }
            }
            if (v[5] < 2 || v[5] > 30 || v[6] < 0 || v[6] >= t->length ||
                (i > 0 && v[6] <= t->points[i - 1].s)) {
                SDL_free(d);
                return false;
            }
            t->points[i] = (TrackPoint){v3(v[0], v[1], v[2]), v[3], v[4], v[5], v[6]};
            lo = fminf(lo, v[1]);
            hi = fmaxf(hi, v[1]);
        }
        t->elevation = hi - lo;
        for (int i = 0; i < t->can_count; i++, off += 8) {
            t->cans[i] = (Can){read_float(d + off), read_float(d + off + 4)};
            if (!isfinite(t->cans[i].index) || t->cans[i].index < 0 || t->cans[i].index >= n ||
                !isfinite(t->cans[i].lane)) {
                SDL_free(d);
                return false;
            }
        }
        for (int i = 0; i < t->ramp_count; i++, off += 16) {
            Ramp r = {read_float(d + off), read_float(d + off + 4), read_float(d + off + 8),
                      read_float(d + off + 12)};
            if (!isfinite(r.index) || r.index < 0 || r.index >= n || !isfinite(r.length) ||
                r.length <= 0 || !isfinite(r.height) || r.height < 0 || !isfinite(r.half) ||
                r.half <= 0) {
                SDL_free(d);
                return false;
            }
            t->ramps[i] = r;
        }
        SDL_free(d);
        if (!load_surface(t, false) || !load_surface(t, true) || !load_guardrails(t) ||
            !load_railway(t))
            return false;
        track_place_boosts(t);
    }
    profile_load(&g->profile);
    // Open on the most advanced cup the player has reached.
    g->selected = CUPS[track_cup(SDL_min(g->profile.championship_completed, TRACK_COUNT - 1))].first;
    game_set_mode(g, MODE_CHAMPIONSHIP);
    g->unlocked_track = -1;
    g->screen = SCREEN_MENU;
    return true;
}

static bool profile_path(char *out, size_t n) {
    char *base = SDL_GetPrefPath("ToyCars", "ToyCars");
    if (!base)
        return false;
    SDL_snprintf(out, n, "%sprofile.txt", base);
    SDL_free(base);
    return true;
}
static bool valid_initials(const char *s, bool legacy) {
    if (legacy && strcmp(s, "---") == 0)
        return true;
    return strlen(s) == 3 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z' &&
           s[2] >= 'A' && s[2] <= 'Z';
}

// Profiles before version 8 hold the three original circuits.
#define LEGACY_TRACKS 3
static bool load_records(Profile *p, const char *data, int version) {
    int tracks = version >= 8 ? TRACK_COUNT : LEGACY_TRACKS;
    int used = 0;
    int initials_set = 0;
    int fields = version >= 4
                     ? sscanf(data, " RECORDS %u %3s %d %n", &p->record_serial, p->initials,
                              &initials_set, &used)
                     : sscanf(data, " RECORDS %u %3s %n", &p->record_serial, p->initials, &used);
    if (fields != (version >= 4 ? 3 : 2) || initials_set < 0 || initials_set > 1 ||
        !used || p->record_serial > 1000000000 || !valid_initials(p->initials, false))
        return false;
    // Older profiles cannot distinguish the AAA placeholder from an explicit choice.
    p->initials_set = version >= 4 ? initials_set != 0 : strcmp(p->initials, "AAA") != 0;
    data += used;
    for (int track = 0; track < tracks; track++) {
        TrackRecords *table = &p->records[track];
        used = 0;
        if (sscanf(data, " %d %d %n", &table->top_count, &table->history_count, &used) != 2 ||
            !used || table->top_count < 0 || table->top_count > RECORD_TOP_COUNT ||
            table->history_count < 0 || table->history_count > RECORD_HISTORY_COUNT)
            return false;
        data += used;
        for (int list = 0; list < 2; list++) {
            RaceRecord *entries = list ? table->history : table->top;
            int count = list ? table->history_count : table->top_count;
            for (int i = 0; i < count; i++) {
                RaceRecord *e = &entries[i];
                int record = 0;
                used = 0;
                if (sscanf(data, " %u %3s %f %f %f %lld %d %d %d %d %n", &e->id,
                           e->initials, &e->time, &e->best_lap, &e->improvement, &e->date,
                           &e->difficulty, &e->mode, &e->rank, &record, &used) != 10 || !used ||
                    !e->id || e->id > p->record_serial || !valid_initials(e->initials, true) ||
                    !isfinite(e->time) || e->time <= 0 || e->time >= 86400 ||
                    !isfinite(e->best_lap) || e->best_lap < 0 || e->best_lap > e->time ||
                    !isfinite(e->improvement) || e->improvement < 0 || e->improvement >= 86400 ||
                    e->date < 0 || e->date > 253402300799LL || e->difficulty < -1 ||
                    e->difficulty > 2 || e->mode < -1 || e->mode > 1 || e->rank < 0 ||
                    e->rank > CAR_COUNT || record < 0 || record > 1)
                    return false;
                e->record = record;
                if (i && (list ? entries[i - 1].id <= e->id : entries[i - 1].time > e->time))
                    return false;
                for (int j = 0; j < i; j++)
                    if (entries[j].id == e->id)
                        return false;
                data += used;
            }
        }
    }
    while (*data && SDL_isspace((unsigned char)*data))
        data++;
    return !*data;
}

static void migrate_records(Profile *p) {
    memset(p->records, 0, sizeof p->records);
    p->record_serial = 0;
    p->initials_set = false;
    SDL_strlcpy(p->initials, "AAA", sizeof p->initials);
    for (int i = 0; i < TRACK_COUNT; i++) {
        if (p->best[i] <= 0 || p->best[i] >= 86400)
            continue;
        RaceRecord entry = {.id = ++p->record_serial, .initials = "---", .time = p->best[i],
                            .difficulty = -1, .mode = -1, .record = true};
        p->records[i].top[0] = p->records[i].history[0] = entry;
        p->records[i].top_count = p->records[i].history_count = 1;
    }
}

static void profile_defaults(Profile *p) {
    *p = (Profile){.color = 0,
                   .difficulty = 1,
                   .auto_accel = false,
                   .touch = true,
                   .sound = true,
                   .music = true,
                   .arcade_night = false,
                   .initials = "AAA"};
}
// Version 8 (ten circuits in three cups):
//   TOYCARS 8 / settings / ten best times / ten medals / unlock progress
//   CHAMPIONSHIP <cups>, then per cup: stages scored, best medal, eliminated
//   one line of eight places per track / OPTIONS / RECORDS ...
// Versions 1-7 describe the first three circuits, which form the Rookie Cup.
static bool load_settings(Profile *p, const char **cursor) {
    int c, level, a, t, s, m, used = 0;
    if (sscanf(*cursor, " %d %d %d %d %d %d %n", &c, &level, &a, &t, &s, &m, &used) != 6 || !used ||
        c < 0 || c >= 6 || level < 0 || level >= 3)
        return false;
    p->color = c;
    p->difficulty = level;
    p->auto_accel = !!a;
    p->touch = !!t;
    p->sound = !!s;
    p->music = !!m;
    *cursor += used;
    return true;
}

static bool load_bests(Profile *p, const char **cursor, int tracks) {
    for (int i = 0; i < tracks; i++) {
        float best = 0;
        int used = 0;
        if (sscanf(*cursor, " %f %n", &best, &used) != 1 || !used || !isfinite(best) || best < 0)
            return false;
        p->best[i] = best;
        *cursor += used;
    }
    for (int i = 0; i < tracks; i++) {
        int medal = 0, used = 0;
        if (sscanf(*cursor, " %d %n", &medal, &used) != 1 || !used)
            return false;
        p->medals[i] = (int)clampf((float)medal, 0, 3);
        *cursor += used;
    }
    return true;
}

static bool load_places(Profile *p, const char **cursor, int tracks) {
    for (int track = 0; track < tracks; track++) {
        const CupInfo *cup = &CUPS[track_cup(track)];
        unsigned seen = 0;
        for (int i = 0; i < CAR_COUNT; i++) {
            int place = 0, used = 0;
            if (sscanf(*cursor, " %d %n", &place, &used) != 1 || !used || place < 0 ||
                place > CAR_COUNT || (place && (seen & (1u << place))) ||
                (track - cup->first >= p->championship_stages[track_cup(track)] && place))
                return false;
            if (place)
                seen |= 1u << place;
            p->championship_places[track][i] = place;
            *cursor += used;
        }
    }
    return true;
}

// A scored series must lie within the unlocked tracks: every raced stage, and
// the next one after a qualifying finish, is already open.
static bool valid_cup(const Profile *p, int c) {
    int stages = p->championship_stages[c], first = CUPS[c].first, count = CUPS[c].count;
    bool eliminated = p->championship_eliminated[c];
    if (stages < 0 || stages > count || p->championship_medal[c] < 0 || p->championship_medal[c] > 3)
        return false;
    if (eliminated && (stages == 0 || stages == count))
        return false;
    return !stages || first + stages - (eliminated || stages == count ? 1 : 0) <= p->championship_completed;
}

void profile_load(Profile *p) {
    profile_defaults(p);
    char path[2048];
    if (!profile_path(path, sizeof path))
        return;
    size_t size;
    char *d = SDL_LoadFile(path, &size);
    if (!d)
        return;
    int version = 0, used = 0;
    const char *cursor = d;
    if (sscanf(cursor, "TOYCARS %d %n", &version, &used) != 1 || !used || version < 1 || version > 8) {
        SDL_free(d);
        return;
    }
    cursor += used;
    int tracks = version >= 8 ? TRACK_COUNT : LEGACY_TRACKS, completed = 0;
    bool valid = load_settings(p, &cursor) && load_bests(p, &cursor, tracks);
    if (valid && version >= 2) {
        used = 0;
        valid = sscanf(cursor, " %d %n", &completed, &used) == 1 && used && completed >= 0 &&
                completed <= tracks;
        cursor += used;
    }
    if (!valid) {
        // A damaged header leaves the defaults; nothing else can be trusted.
        profile_defaults(p);
        SDL_free(d);
        return;
    }
    if (version == 1) {
        // Credit consecutive podiums earned before Championship existed.
        completed = 0;
        while (completed < LEGACY_TRACKS && p->medals[completed] > 0)
            completed++;
    }
    bool legacy_complete = version < 8 && completed == LEGACY_TRACKS;
    p->championship_completed = version < 8 ? SDL_min(completed, LEGACY_TRACKS - 1) : completed;
    if (version >= 5) {
        int cups = 1;
        bool championship_valid = true;
        if (version >= 8) {
            used = 0;
            championship_valid = sscanf(cursor, " CHAMPIONSHIP %d %n", &cups, &used) == 1 && used &&
                                 cups == CUP_COUNT;
            cursor += used;
        }
        for (int c = 0; championship_valid && c < cups; c++) {
            int stages = 0, medal = 0, eliminated = 0;
            used = 0;
            int fields = version >= 8 ? sscanf(cursor, " %d %d %d %n", &stages, &medal, &eliminated, &used)
                         : version >= 6
                             ? sscanf(cursor, " CHAMPIONSHIP %d %d %d %n", &stages, &medal, &eliminated, &used)
                             : sscanf(cursor, " CHAMPIONSHIP %d %d %n", &stages, &medal, &used);
            championship_valid = fields == (version >= 6 ? 3 : 2) && used && eliminated >= 0 &&
                                 eliminated <= 1;
            p->championship_stages[c] = stages;
            p->championship_medal[c] = medal;
            p->championship_eliminated[c] = eliminated == 1;
            cursor += used;
        }
        if (championship_valid && legacy_complete && p->championship_medal[0] > 0)
            // An old completed series with a medal opens the Pro Cup.
            p->championship_completed = LEGACY_TRACKS;
        for (int c = 0; championship_valid && c < CUP_COUNT; c++)
            championship_valid = valid_cup(p, c);
        championship_valid = championship_valid && load_places(p, &cursor, tracks);
        if (!championship_valid) {
            profile_defaults(p);
            SDL_free(d);
            return;
        }
    }
    if (version >= 7) {
        int night = 0;
        used = 0;
        if (sscanf(cursor, " OPTIONS %d %n", &night, &used) != 1 || !used || night < 0 || night > 1) {
            profile_defaults(p);
            SDL_free(d);
            return;
        }
        p->arcade_night = night == 1;
        cursor += used;
    }
    // Legacy saves retain unlocks and records; start a new scored Championship.
    if (version < 3 || !load_records(p, cursor, version))
        migrate_records(p);
    SDL_free(d);
}
bool profile_save(const Profile *p) {
    char path[2048], temp[2060];
    if (!profile_path(path, sizeof path))
        return false;
    SDL_snprintf(temp, sizeof temp, "%s.tmp", path);
    // Ten tracks of forty records each need about 26 KB.
    size_t capacity = 98304;
    char *data = SDL_malloc(capacity);
    if (!data)
        return false;
    int len = SDL_snprintf(data, capacity, "TOYCARS 8\n%d %d %d %d %d %d\n", p->color, p->difficulty,
                           p->auto_accel, p->touch, p->sound, p->music);
    for (int i = 0; i < TRACK_COUNT; i++)
        len += SDL_snprintf(data + len, capacity - len, "%.3f%c", p->best[i], i == TRACK_COUNT - 1 ? '\n' : ' ');
    for (int i = 0; i < TRACK_COUNT; i++)
        len += SDL_snprintf(data + len, capacity - len, "%d%c", p->medals[i], i == TRACK_COUNT - 1 ? '\n' : ' ');
    len += SDL_snprintf(data + len, capacity - len, "%d\nCHAMPIONSHIP %d\n", p->championship_completed,
                        CUP_COUNT);
    for (int c = 0; c < CUP_COUNT; c++)
        len += SDL_snprintf(data + len, capacity - len, "%d %d %d\n", p->championship_stages[c],
                            p->championship_medal[c], p->championship_eliminated[c]);
    for (int track = 0; track < TRACK_COUNT; track++)
        for (int i = 0; i < CAR_COUNT; i++)
            len += SDL_snprintf(data + len, capacity - len, "%d%c",
                p->championship_places[track][i], i == CAR_COUNT - 1 ? '\n' : ' ');
    len += SDL_snprintf(data + len, capacity - len, "OPTIONS %d\n", p->arcade_night);
    len += SDL_snprintf(data + len, capacity - len, "RECORDS %u %s %d\n",
                        p->record_serial, p->initials, p->initials_set);
    for (int track = 0; track < TRACK_COUNT; track++) {
        const TrackRecords *table = &p->records[track];
        len += SDL_snprintf(data + len, capacity - len, "%d %d\n",
                            table->top_count, table->history_count);
        for (int list = 0; list < 2; list++) {
            const RaceRecord *entries = list ? table->history : table->top;
            int count = list ? table->history_count : table->top_count;
            for (int i = 0; i < count; i++) {
                const RaceRecord *e = &entries[i];
                int wrote = SDL_snprintf(data + len, capacity - len,
                    "%u %s %.3f %.3f %.3f %lld %d %d %d %d\n", e->id, e->initials,
                    e->time, e->best_lap, e->improvement, e->date, e->difficulty, e->mode,
                    e->rank, e->record);
                if (wrote < 0 || (size_t)wrote >= capacity - len) {
                    SDL_free(data);
                    return false;
                }
                len += wrote;
            }
        }
    }
    bool saved = SDL_SaveFile(temp, data, (size_t)len);
    SDL_free(data);
    if (saved) {
        if (rename(temp, path) == 0)
            return true;
        SDL_Log("Cannot replace profile: %s", path);
    } else
        SDL_Log("Cannot save profile: %s", SDL_GetError());
    return false;
}

bool profile_reset(Profile *p) {
    Profile fresh;
    profile_defaults(&fresh);
    // Commit to disk first; a failed write must preserve the active profile.
    if (!profile_save(&fresh))
        return false;
    *p = fresh;
    return true;
}
