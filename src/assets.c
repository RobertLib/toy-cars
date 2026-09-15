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

static bool load_surface(Track *t, bool water) {
    SurfaceTriangle *triangles = water ? t->water : t->surface;
    uint32_t *cells = water ? t->water_cells : t->surface_cells;
    uint16_t *refs = water ? t->water_refs : t->surface_refs;
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
    for (uint32_t i = 0; i < count; i++) {
        float v[9];
        for (int j = 0; j < 9; j++) {
            v[j] = read_float(data + 8 + i * stride + (water ? (j / 3) * 40 + (j % 3) * 4 : j * 4));
            if (!isfinite(v[j]) || fabsf(v[j]) > (water ? 150 : 128)) {
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
    // Index triangles by their XZ bounds once, keeping tire queries local.
    uint32_t cursor[SURFACE_GRID * SURFACE_GRID] = {0};
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < count; i++) {
            SurfaceTriangle f = triangles[i];
            int x0 = surface_cell(fminf(f.a.x, fminf(f.b.x, f.c.x)));
            int x1 = surface_cell(fmaxf(f.a.x, fmaxf(f.b.x, f.c.x)));
            int z0 = surface_cell(fminf(f.a.z, fminf(f.b.z, f.c.z)));
            int z1 = surface_cell(fmaxf(f.a.z, fmaxf(f.b.z, f.c.z)));
            for (int z = z0; z <= z1; z++)
                for (int x = x0; x <= x1; x++) {
                    int cell = z * SURFACE_GRID + x;
                    if (pass == 0)
                        cursor[cell]++;
                    else
                        refs[cursor[cell]++] = (uint16_t)i;
                }
        }
        if (pass == 0) {
            cells[0] = 0;
            for (int cell = 0; cell < SURFACE_GRID * SURFACE_GRID; cell++) {
                cells[cell + 1] = cells[cell] + cursor[cell];
                cursor[cell] = cells[cell];
            }
            if (cells[SURFACE_GRID * SURFACE_GRID] > (water ? MAX_WATER_REFS : MAX_SURFACE_REFS))
                return false;
        }
    }
    return true;
}

bool game_load(Game *g) {
    memset(g, 0, sizeof *g);
    g->rng = 81291;
    const char *ids[] = {"country", "beach", "winter"};
    const char *names[] = {"Harvest Hills", "Sunshine Coast", "Alpine Rush"};
    const char *sub[] = {"COUNTRY RUN", "BEACH ESCAPE", "WINTER CIRCUIT"};
    const char *descs[] = {"Rolling hills. Red barns. Full throttle.",
                           "Salt in the air. Sand in your tires.",
                           "Fresh powder. Slippery turns. Big air."};
    Color accents[] = {rgb(.86, .30, .13), rgb(.04, .49, .51), rgb(.30, .45, .69)};
    Color skies[] = {rgb(.84, .87, .74), rgb(.63, .83, .85), rgb(.72, .81, .88)};
    for (int k = 0; k < TRACK_COUNT; k++) {
        Track *t = &g->tracks[k];
        t->id = ids[k];
        t->name = names[k];
        t->subtitle = sub[k];
        t->description = descs[k];
        t->accent = accents[k];
        t->sky = skies[k];
        char name[100];
        SDL_snprintf(name, sizeof name, "tracks/%s.tcp", ids[k]);
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
        if (!load_surface(t, false) || !load_surface(t, true) || !load_guardrails(t))
            return false;
        track_place_boosts(t);
    }
    profile_load(&g->profile);
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

static bool load_records(Profile *p, const char *data, int version) {
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
    for (int track = 0; track < TRACK_COUNT; track++) {
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
                   .initials = "AAA"};
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
    int offset = 0;
    int version = 0, completed = 0, c, level, a, t, s, m, med0, med1, med2;
    float b0, b1, b2;
    int n = sscanf(d, "TOYCARS %d\n%d %d %d %d %d %d\n%f %f %f\n%d %d %d\n%d %n", &version,
                   &c, &level, &a, &t, &s, &m, &b0, &b1, &b2, &med0, &med1, &med2, &completed, &offset);
    bool valid = (version == 1 && n >= 13) ||
                 ((version >= 2 && version <= 6) && n == 14 && completed >= 0 && completed <= TRACK_COUNT);
    if (valid && c >= 0 && c < 6 && level >= 0 && level < 3 && isfinite(b0) && isfinite(b1) &&
        isfinite(b2) && b0 >= 0 && b1 >= 0 && b2 >= 0) {
        p->color = c;
        p->difficulty = level;
        p->auto_accel = !!a;
        p->touch = !!t;
        p->sound = !!s;
        p->music = !!m;
        p->best[0] = b0;
        p->best[1] = b1;
        p->best[2] = b2;
        p->medals[0] = (int)clampf(med0, 0, 3);
        p->medals[1] = (int)clampf(med1, 0, 3);
        p->medals[2] = (int)clampf(med2, 0, 3);
        if (version == 1) {
            // Credit consecutive podiums earned before Championship existed.
            completed = 0;
            while (completed < TRACK_COUNT && p->medals[completed] > 0)
                completed++;
        }
        p->championship_completed = completed;
        if (version >= 5) {
            int used = 0, eliminated = 0;
            char *cursor = d + offset;
            int fields = version >= 6
                ? sscanf(cursor, " CHAMPIONSHIP %d %d %d %n", &p->championship_stages,
                         &p->championship_medal, &eliminated, &used)
                : sscanf(cursor, " CHAMPIONSHIP %d %d %n", &p->championship_stages,
                         &p->championship_medal, &used);
            p->championship_eliminated = eliminated == 1;
            bool championship_valid = fields == (version >= 6 ? 3 : 2) && used &&
                eliminated >= 0 && eliminated <= 1 &&
                (!eliminated || (p->championship_stages > 0 &&
                                 p->championship_stages < TRACK_COUNT)) &&
                p->championship_stages >= 0 && p->championship_stages <= TRACK_COUNT &&
                p->championship_stages <= completed + eliminated &&
                p->championship_medal >= 0 && p->championship_medal <= 3;
            cursor += used;
            for (int stage = 0; championship_valid && stage < TRACK_COUNT; stage++) {
                unsigned seen = 0;
                for (int i = 0; i < CAR_COUNT; i++) {
                    int place = 0;
                    used = 0;
                    if (sscanf(cursor, " %d %n", &place, &used) != 1 || !used ||
                        place < 0 || place > CAR_COUNT || (place && (seen & (1u << place))) ||
                        (stage >= p->championship_stages && place)) {
                        championship_valid = false;
                        break;
                    }
                    if (place)
                        seen |= 1u << place;
                    p->championship_places[stage][i] = place;
                    cursor += used;
                }
            }
            if (!championship_valid) {
                profile_defaults(p);
                SDL_free(d);
                return;
            }
            offset = (int)(cursor - d);
        }
        // Legacy saves retain unlocks and records; start a new scored Championship.
        if (version < 3 || !offset || !load_records(p, d + offset, version))
            migrate_records(p);
    }
    SDL_free(d);
}
bool profile_save(const Profile *p) {
    char path[2048], temp[2060], data[32768];
    if (!profile_path(path, sizeof path))
        return false;
    SDL_snprintf(temp, sizeof temp, "%s.tmp", path);
    int len =
        SDL_snprintf(data, sizeof data, "TOYCARS 6\n%d %d %d %d %d %d\n%.3f %.3f %.3f\n%d %d %d\n%d\n",
                     p->color, p->difficulty, p->auto_accel, p->touch, p->sound, p->music,
                     p->best[0], p->best[1], p->best[2], p->medals[0], p->medals[1], p->medals[2],
                     p->championship_completed);
    len += SDL_snprintf(data + len, sizeof data - len, "CHAMPIONSHIP %d %d %d\n",
                        p->championship_stages, p->championship_medal,
                        p->championship_eliminated);
    for (int stage = 0; stage < TRACK_COUNT; stage++)
        for (int i = 0; i < CAR_COUNT; i++)
            len += SDL_snprintf(data + len, sizeof data - len, "%d%c",
                p->championship_places[stage][i], i == CAR_COUNT - 1 ? '\n' : ' ');
    len += SDL_snprintf(data + len, sizeof data - len, "RECORDS %u %s %d\n",
                        p->record_serial, p->initials, p->initials_set);
    for (int track = 0; track < TRACK_COUNT; track++) {
        const TrackRecords *table = &p->records[track];
        len += SDL_snprintf(data + len, sizeof data - len, "%d %d\n",
                            table->top_count, table->history_count);
        for (int list = 0; list < 2; list++) {
            const RaceRecord *entries = list ? table->history : table->top;
            int count = list ? table->history_count : table->top_count;
            for (int i = 0; i < count; i++) {
                const RaceRecord *e = &entries[i];
                int wrote = SDL_snprintf(data + len, sizeof data - len,
                    "%u %s %.3f %.3f %.3f %lld %d %d %d %d\n", e->id, e->initials,
                    e->time, e->best_lap, e->improvement, e->date, e->difficulty, e->mode,
                    e->rank, e->record);
                if (wrote < 0 || (size_t)wrote >= sizeof data - len)
                    return false;
                len += wrote;
            }
        }
    }
    if (SDL_SaveFile(temp, data, (size_t)len)) {
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
