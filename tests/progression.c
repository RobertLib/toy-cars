// Exercise real result handling and persistence without touching player data.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char pref_directory[2048];
static int failures;
#define EXPECT(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)
static char *test_pref_path(const char *org, const char *app) {
    (void)org;
    (void)app;
    return SDL_strdup(pref_directory);
}
#define SDL_GetPrefPath test_pref_path
#include "../src/assets.c"
#undef SDL_GetPrefPath

static void finish(Game *g, int rank, bool dnf) {
    game_start(g);
    EXPECT(g->screen == SCREEN_COUNTDOWN);
    g->screen = SCREEN_RACE;
    g->cars[0].finished = !dnf;
    g->cars[0].dnf = dnf;
    g->cars[0].finish_time = 80;
    for (int i = 1; i < rank; i++) {
        g->cars[i].finished = true;
        g->cars[i].finish_time = 70 + i;
    }
    for (int i = rank; i < CAR_COUNT; i++) {
        g->cars[i].finished = true;
        g->cars[i].finish_time = 80 + i;
    }
    game_results(g);
}

static void record_race(Game *g, float seconds, const char *initials) {
    game_start(g);
    g->screen = SCREEN_RACE;
    g->cars[0].finished = true;
    g->cars[0].finish_time = seconds;
    g->cars[0].best_lap = 30.123f;
    game_results(g);
    EXPECT(g->initials_pending);
    SDL_strlcpy(g->initials, initials, sizeof g->initials);
    EXPECT(game_submit_initials(g));
    EXPECT(g->screen == SCREEN_RECORDS && !g->initials_pending);
}

static void test_records(Game *g, const char *path) {
    SDL_RemovePath(path);
    profile_load(&g->profile);
    g->selected = 0;
    g->test_mode = false;
    game_set_mode(g, MODE_ARCADE);
    record_race(g, 80.125f, "ROB");
    TrackRecords *table = &g->profile.records[0];
    EXPECT(table->top_count == 1 && table->history_count == 1);
    EXPECT(g->new_record && g->previous_best == 0);
    EXPECT(table->top[0].record && table->top[0].date > 0);
    EXPECT(strcmp(table->top[0].initials, "ROB") == 0);
    EXPECT(table->top[0].mode == MODE_ARCADE && table->top[0].difficulty == 1);
    EXPECT(table->top[0].best_lap == 30.123f);
    game_results(g);
    EXPECT(table->history_count == 1); // Opening the board never records the finish again.
    record_race(g, 85, "SLO");
    EXPECT(!g->new_record && !table->history[0].record);
    EXPECT(strcmp(table->top[0].initials, "ROB") == 0);
    record_race(g, 80.125f, "TIE");
    EXPECT(!g->new_record && strcmp(table->top[1].initials, "TIE") == 0);
    record_race(g, 79.025f, "ACE");
    EXPECT(g->new_record && fabsf(table->top[0].improvement - 1.1f) < .0001f);
    EXPECT(table->history[0].id == table->top[0].id);
    Profile saved;
    profile_load(&saved);
    EXPECT(saved.records[0].history_count == 4 && saved.records[0].top_count == 4);
    EXPECT(saved.best[0] == 79.025f && strcmp(saved.initials, "ACE") == 0);
    EXPECT(strcmp(saved.records[0].history[0].initials, "ACE") == 0);
    EXPECT(fabsf(saved.records[0].top[0].improvement - 1.1f) < .0001f);
    for (int i = 0; i < 35; i++)
        record_race(g, 90 + i, "END");
    EXPECT(table->top_count == 10 && table->history_count == 30);
    EXPECT(table->history[0].time == 124 && table->history[29].time == 95);
    EXPECT(table->top[0].time == 79.025f && table->top[9].time == 95);
    profile_load(&saved);
    EXPECT(saved.records[0].history_count == 30 && saved.records[0].top_count == 10);
    EXPECT(saved.records[0].top[0].time == 79.025f);
    Profile before = g->profile;
    finish(g, 1, true);
    EXPECT(!g->initials_pending && memcmp(&before, &g->profile, sizeof before) == 0);
    g->test_mode = true;
    finish(g, 1, false);
    EXPECT(!g->initials_pending && memcmp(&before, &g->profile, sizeof before) == 0);
    g->test_mode = false;
    g->profile.championship_completed = 3;
    g->selected = 1;
    record_race(g, 82, "TWO");
    EXPECT(g->profile.records[1].top_count == 1 && table->top_count == 10);
    // A quit before name confirmation still preserves the finish with remembered initials.
    finish(g, 1, false);
    profile_load(&saved);
    EXPECT(g->initials_pending && saved.records[1].history_count == 2);
    EXPECT(strcmp(saved.records[1].history[0].initials, "TWO") == 0);
    SDL_strlcpy(g->initials, "12!", 4);
    EXPECT(!game_submit_initials(g) && g->initials_pending);
    SDL_strlcpy(g->initials, "NEW", 4);
    char temp[2300];
    SDL_snprintf(temp, sizeof temp, "%s.tmp", path);
    EXPECT(SDL_CreateDirectory(temp));
    EXPECT(!game_submit_initials(g) && g->initials_pending && g->record_save_failed);
    EXPECT(SDL_RemovePath(temp));
    EXPECT(game_submit_initials(g) && !g->record_save_failed);
    EXPECT(g->profile.records[1].history_count == 2);
    profile_load(&saved);
    EXPECT(strcmp(saved.records[1].history[0].initials, "NEW") == 0);
    // Import old records without inventing initials, dates or race settings.
    const char *legacy = "TOYCARS 2\n0 1 1 1 1 1\n82.123 0 84\n3 0 0\n1\n";
    EXPECT(SDL_SaveFile(path, legacy, strlen(legacy)));
    profile_load(&saved);
    EXPECT(saved.records[0].top_count == 1 && saved.records[1].top_count == 0);
    EXPECT(strcmp(saved.records[0].top[0].initials, "---") == 0);
    EXPECT(saved.records[0].top[0].date == 0 && saved.records[0].top[0].mode == -1);
    EXPECT(profile_save(&saved));
    profile_load(&saved);
    EXPECT(saved.records[0].top[0].time == 82.123f && saved.championship_completed == 1);
    // Invalid/truncated extensions preserve the older core record and progression.
    const char *bad[] = {
        "RECORDS 1 AAA\n999 0\n", "RECORDS 1 AAA\n1 0\n1 ABC nan 0 0 0 1 1 1 1\n",
        "RECORDS 1 ABCD\n", "RECORDS 1 AAA\n1 0\n1 A1C 80 0 0 0 1 1 1 1\n",
        "RECORDS 1 AAA\n1 0\n1 ABC 80 0 0 0 9 1 1 1\n", "RECORDS 1 AAA\n1 0\n"
    };
    for (size_t i = 0; i < SDL_arraysize(bad); i++) {
        char corrupt[600];
        SDL_snprintf(corrupt, sizeof corrupt,
                     "TOYCARS 3\n0 1 1 1 1 1\n82 0 0\n3 0 0\n1\n%s", bad[i]);
        EXPECT(SDL_SaveFile(path, corrupt, strlen(corrupt)));
        profile_load(&saved);
        EXPECT(saved.best[0] == 82 && saved.championship_completed == 1);
        EXPECT(saved.records[0].top_count == 1 && saved.records[0].history_count == 1);
        EXPECT(strcmp(saved.initials, "AAA") == 0);
    }
}

static void test_reset(Game *g, const char *path) {
    SDL_RemovePath(path);
    Profile defaults;
    profile_load(&defaults);
    g->profile = defaults;
    g->test_mode = false;
    game_set_mode(g, MODE_ARCADE);
    g->selected = 0;
    record_race(g, 80, "ROB");
    g->profile.championship_completed = TRACK_COUNT;
    g->profile.color = 5;
    g->profile.difficulty = 2;
    g->profile.auto_accel = true;
    g->profile.touch = g->profile.sound = g->profile.music = false;
    EXPECT(profile_save(&g->profile));
    Profile before = g->profile;
    g->selected = 2;
    g->unlocked_track = 2;
    g->championship_advanced = g->initials_pending = g->new_record = true;
    g->screen = SCREEN_RESET_DATA;
    // An unwritable staging path must preserve the profile in memory and on disk.
    char temp[2300];
    SDL_snprintf(temp, sizeof temp, "%s.tmp", path);
    EXPECT(SDL_CreateDirectory(temp));
    EXPECT(!game_reset_profile(g));
    EXPECT(memcmp(&g->profile, &before, sizeof before) == 0);
    EXPECT(g->screen == SCREEN_RESET_DATA && g->selected == 2 && g->initials_pending);
    Profile saved;
    profile_load(&saved);
    EXPECT(saved.championship_completed == TRACK_COUNT && saved.color == 5);
    EXPECT(saved.records[0].history_count == 1 && strcmp(saved.initials, "ROB") == 0);
    EXPECT(SDL_RemovePath(temp));
    EXPECT(game_reset_profile(g));
    EXPECT(memcmp(&g->profile, &defaults, sizeof defaults) == 0);
    EXPECT(g->screen == SCREEN_SETTINGS && g->selected == 0 && g->mode == MODE_CHAMPIONSHIP);
    EXPECT(g->unlocked_track == -1 && !g->championship_advanced && !g->initials_pending);
    EXPECT(g->result_id == 0 && !g->new_record && strcmp(g->initials, "AAA") == 0);
    profile_load(&saved);
    EXPECT(memcmp(&saved, &defaults, sizeof defaults) == 0);
    EXPECT(!game_track_unlocked(&saved, 1));
    EXPECT(game_reset_profile(g)); // Resetting an already empty profile is harmless.
    EXPECT(SDL_RemovePath(path));
    EXPECT(game_reset_profile(g)); // Also works before a profile has ever been saved.
}

static void test_player_name(Game *g, const char *path) {
    EXPECT(game_reset_profile(g));
    EXPECT(!g->profile.initials_set && strcmp(game_player_name(g), "YOU") == 0);
    game_start(g);
    EXPECT(strcmp(g->cars[0].name, "YOU") == 0);
    // Saving ordinary settings and restarting must not turn the placeholder into a name.
    g->profile.sound = false;
    EXPECT(profile_save(&g->profile));
    EXPECT(game_load(g));
    EXPECT(!g->profile.initials_set && strcmp(game_player_name(g), "YOU") == 0);
    finish(g, 1, false);
    EXPECT(g->initials_pending && strcmp(game_player_name(g), "YOU") == 0);
    SDL_strlcpy(g->initials, "ROB", sizeof g->initials);
    EXPECT(strcmp(game_player_name(g), "YOU") == 0); // Typing is not confirmation.
    SDL_strlcpy(g->initials, "12!", sizeof g->initials);
    EXPECT(!game_submit_initials(g) && !g->profile.initials_set);
    SDL_strlcpy(g->initials, "ROB", sizeof g->initials);
    EXPECT(game_submit_initials(g));
    EXPECT(g->profile.initials_set && strcmp(game_player_name(g), "ROB") == 0);
    EXPECT(strcmp(g->cars[0].name, "ROB") == 0 && g->screen == SCREEN_RESULTS);
    EXPECT(game_load(g));
    game_start(g);
    EXPECT(g->profile.initials_set && strcmp(g->cars[0].name, "ROB") == 0);
    EXPECT(game_reset_profile(g));
    EXPECT(!g->profile.initials_set && strcmp(g->cars[0].name, "YOU") == 0);
    // AAA is a valid chosen name: only the explicit flag distinguishes it from defaults.
    finish(g, 1, false);
    EXPECT(game_submit_initials(g));
    EXPECT(g->profile.initials_set && strcmp(game_player_name(g), "AAA") == 0);
    EXPECT(game_load(g));
    game_start(g);
    EXPECT(g->profile.initials_set && strcmp(g->cars[0].name, "AAA") == 0);
    for (int named = 0; named < 2; named++) {
        char legacy[200];
        SDL_snprintf(legacy, sizeof legacy,
                     "TOYCARS 3\n0 1 1 1 1 1\n0 0 0\n0 0 0\n0\nRECORDS 0 %s\n0 0\n0 0\n0 0\n",
                     named ? "ROB" : "AAA");
        EXPECT(SDL_SaveFile(path, legacy, strlen(legacy)));
        EXPECT(game_load(g));
        EXPECT(g->profile.initials_set == (named != 0));
        EXPECT(strcmp(game_player_name(g), named ? "ROB" : "YOU") == 0);
        EXPECT(profile_save(&g->profile));
        EXPECT(game_load(g));
        EXPECT(strcmp(game_player_name(g), named ? "ROB" : "YOU") == 0);
    }
    const char *bad = "TOYCARS 4\n0 1 1 1 1 1\n82 0 0\n3 0 0\n1\n"
                      "RECORDS 1 ROB 2\n0 0\n0 0\n0 0\n";
    EXPECT(SDL_SaveFile(path, bad, strlen(bad)));
    EXPECT(game_load(g));
    EXPECT(!g->profile.initials_set && g->profile.best[0] == 82);
    EXPECT(strcmp(game_player_name(g), "YOU") == 0);
}

static void test_result_save_failure(Game *g, const char *path) {
    char temp[2300];
    SDL_snprintf(temp, sizeof temp, "%s.tmp", path);
    for (int stage = 1; stage < TRACK_COUNT; stage++) {
        EXPECT(game_reset_profile(g));
        g->test_mode = false;
        g->profile.championship_completed = stage;
        g->profile.championship_stages = stage;
        EXPECT(profile_save(&g->profile));
        game_set_mode(g, MODE_CHAMPIONSHIP);
        EXPECT(SDL_CreateDirectory(temp));
        finish(g, 1, false);
        unsigned serial = g->profile.record_serial;
        EXPECT(game_save_warning(g) && !g->initials_pending);
        EXPECT(g->profile.championship_completed == stage + 1);
        Profile disk;
        profile_load(&disk);
        EXPECT(disk.championship_completed == stage && disk.records[stage].history_count == 0);
        game_continue(g);
        game_start(g);
        EXPECT(g->screen == SCREEN_RESULTS && g->selected == stage);
        EXPECT(!game_save_profile(g) && game_save_warning(g));
        EXPECT(g->profile.record_serial == serial);
        EXPECT(SDL_RemovePath(temp));
        EXPECT(game_save_profile(g) && !game_save_warning(g));
        profile_load(&disk);
        EXPECT(disk.championship_completed == stage + 1 && disk.record_serial == serial);
        EXPECT(disk.records[stage].history_count == 1 && disk.medals[stage] == 3);
        game_continue(g);
        EXPECT(stage == TRACK_COUNT - 1 ? g->screen == SCREEN_RESULTS :
                   g->screen == SCREEN_COUNTDOWN && g->selected == stage + 1);
    }
    // Explicitly continuing unsaved retains pending data across a new race,
    // including a DNF, until a later successful save writes the whole profile.
    EXPECT(game_reset_profile(g));
    g->profile.championship_completed = 1;
    g->profile.championship_stages = 1;
    EXPECT(profile_save(&g->profile));
    game_set_mode(g, MODE_CHAMPIONSHIP);
    EXPECT(SDL_CreateDirectory(temp));
    finish(g, 1, false);
    g->save_failure_acknowledged = true;
    game_continue(g);
    EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == 2 && g->record_save_failed);
    finish(g, 1, true);
    EXPECT(g->record_save_failed && game_save_warning(g));
    EXPECT(SDL_RemovePath(temp));
    EXPECT(game_save_profile(g));
    Profile disk;
    profile_load(&disk);
    EXPECT(disk.championship_completed == 3 && disk.records[1].history_count == 1);
    EXPECT(!g->record_save_failed && !g->save_failure_acknowledged);
    // Retrying an elimination save keeps previous unlocks, medals and records.
    EXPECT(game_reset_profile(g));
    g->profile.championship_completed = TRACK_COUNT;
    g->profile.championship_medal = 3;
    EXPECT(profile_save(&g->profile));
    EXPECT(SDL_CreateDirectory(temp));
    finish(g, 5, false);
    EXPECT(g->profile.championship_eliminated && game_save_warning(g));
    EXPECT(!g->championship_celebration && g->profile.championship_medal == 3);
    game_continue(g);
    EXPECT(g->screen == SCREEN_RESULTS);
    EXPECT(SDL_RemovePath(temp));
    EXPECT(game_submit_initials(g));
    profile_load(&disk);
    EXPECT(disk.championship_eliminated && disk.championship_completed == TRACK_COUNT);
    EXPECT(disk.championship_medal == 3 && disk.records[0].history_count == 1);
    game_continue(g);
    game_continue(g);
    EXPECT(g->selected == 0 && !g->profile.championship_eliminated);
    EXPECT(g->profile.championship_medal == 3 && g->profile.championship_completed == TRACK_COUNT);
    EXPECT(g->profile.records[0].history_count == 1);
}

static void test_settings_save_failure(Game *g, const char *path) {
    char temp[2300];
    SDL_snprintf(temp, sizeof temp, "%s.tmp", path);
    const Screen screens[] = {SCREEN_SETTINGS, SCREEN_GARAGE, SCREEN_MENU,
                              SCREEN_COUNTDOWN, SCREEN_RACE, SCREEN_PAUSE};
    for (size_t i = 0; i < SDL_arraysize(screens); i++) {
        EXPECT(game_reset_profile(g));
        game_start(g);
        g->screen = screens[i];
        Profile before = g->profile;
        g->profile.color = 4;
        g->profile.auto_accel = !before.auto_accel;
        EXPECT(SDL_CreateDirectory(temp));
        EXPECT(!game_save_profile(g) && game_save_warning(g));
        Screen expected = screens[i] == SCREEN_RACE || screens[i] == SCREEN_COUNTDOWN
                              ? SCREEN_PAUSE : screens[i];
        EXPECT(g->screen == expected);
        Profile disk;
        profile_load(&disk);
        EXPECT(memcmp(&disk, &before, sizeof disk) == 0);
        game_start(g);
        game_continue(g);
        EXPECT(g->screen == expected); // Cannot bypass the pending warning.
        EXPECT(SDL_RemovePath(temp));
        EXPECT(game_save_profile(g) && !game_save_warning(g));
        profile_load(&disk);
        EXPECT(disk.color == 4 && disk.auto_accel != before.auto_accel && g->screen == expected);
    }
}

static void test_dnf_classification(Game *g) {
    EXPECT(game_reset_profile(g));
    game_set_mode(g, MODE_ARCADE);
    Profile before = g->profile;
    game_start(g);
    g->screen = SCREEN_RACE;
    for (int i = 0; i < CAR_COUNT; i++) {
        g->cars[i].dnf = true;
        g->cars[i].progress = 400 - i * 10;
    }
    // All DNFs use distance, rather than assigning the player last place.
    game_results(g);
    EXPECT(g->finish_rank == 1 && game_car_precedes(g, 0, 1));
    EXPECT(memcmp(&before, &g->profile, sizeof before) == 0);
    g->screen = SCREEN_RACE;
    g->cars[1].progress = 500;
    g->cars[2].progress = g->cars[0].progress;
    g->cars[3].dnf = false;
    g->cars[3].progress = 0;
    g->cars[4].dnf = false;
    g->cars[4].finished = true;
    g->cars[4].finish_time = 60;
    game_results(g);
    EXPECT(g->finish_rank == 4);
    EXPECT(game_car_precedes(g, 4, 3) && game_car_precedes(g, 3, 1));
    EXPECT(game_car_precedes(g, 1, 0) && game_car_precedes(g, 0, 2));
    // Results remain consistent when the remaining active rival times out.
    g->time = 221;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    EXPECT(g->cars[3].dnf && g->finish_rank == 3);
    EXPECT(memcmp(&before, &g->profile, sizeof before) == 0);
    EXPECT(game_next_track(g) == -1);
}

int main(int argc, char **argv) {
    if (argc != 2 || strlen(argv[1]) > sizeof pref_directory - 2)
        return 2;
    SDL_snprintf(pref_directory, sizeof pref_directory, "%s/", argv[1]);
    if (!SDL_CreateDirectory(pref_directory))
        return 2;
    char path[2200];
    SDL_snprintf(path, sizeof path, "%sprofile.txt", pref_directory);
    SDL_RemovePath(path);
    Game *g = calloc(1, sizeof *g);
    if (!g || !game_load(g))
        return 2;
    EXPECT(g->mode == MODE_CHAMPIONSHIP && g->selected == 0);
    EXPECT(game_track_unlocked(&g->profile, 0));
    EXPECT(!game_track_unlocked(&g->profile, 1));
    EXPECT(!game_track_unlocked(&g->profile, -1));
    EXPECT(!game_track_unlocked(&g->profile, TRACK_COUNT));
    for (int mode = 0; mode < 2; mode++) {
        game_set_mode(g, (RaceMode)mode);
        g->selected = 2;
        game_start(g);
        EXPECT(g->screen == SCREEN_MENU); // Enter, gamepad and UI share this guard.
    }
    g->selected = 0;
    finish(g, 1, false);
    EXPECT(g->profile.medals[0] == 3 && g->profile.championship_completed == 0);
    EXPECT(game_next_track(g) == -1); // Arcade victory cannot unlock the next stage.
    // Qualified finishes retain overall scoring and a saved result for each driver.
    for (int rank = 1; rank <= 4; rank++) {
        EXPECT(game_reset_profile(g));
        for (int stage = 0; stage < TRACK_COUNT; stage++) {
            game_set_mode(g, MODE_CHAMPIONSHIP);
            EXPECT(g->selected == stage);
            finish(g, rank, false);
            EXPECT(g->championship_scored && g->championship_advanced);
            EXPECT(g->profile.championship_stages == stage + 1);
            EXPECT(g->profile.championship_completed == stage + 1);
            EXPECT(g->initials_pending == (stage == 0));
            if (g->initials_pending) {
                SDL_strlcpy(g->initials, "ROB", sizeof g->initials);
                EXPECT(game_submit_initials(g));
            }
            Profile saved;
            profile_load(&saved);
            EXPECT(saved.championship_stages == stage + 1);
            EXPECT(memcmp(saved.championship_places, g->profile.championship_places,
                          sizeof saved.championship_places) == 0);
            EXPECT(game_championship_rank(g) == rank);
            EXPECT(game_next_track(g) == (stage < 2 ? stage + 1 : -1));
            Profile before = g->profile;
            game_results(g);
            game_tick(g, (Controls){0}, FIXED_DT, false);
            EXPECT(memcmp(&before, &g->profile, sizeof before) == 0);
            if (stage < 2) {
                // Reload and resume the next stage, including after a non-podium finish.
                g->profile = saved;
                game_set_mode(g, MODE_CHAMPIONSHIP);
                EXPECT(g->selected == stage + 1);
            } else {
                EXPECT(game_championship_finished(g));
                EXPECT(g->championship_celebration == (rank <= 3));
                EXPECT(saved.championship_medal == (rank <= 3 ? 4 - rank : 0));
                if (rank <= 3) {
                    game_continue(g);
                    EXPECT(!g->championship_celebration && g->screen == SCREEN_RESULTS);
                }
                game_continue(g);
                EXPECT(g->screen == SCREEN_MENU);
                game_continue(g);
                EXPECT(g->selected == 0 && g->profile.championship_stages == 0);
                EXPECT(g->profile.championship_completed == TRACK_COUNT);
            }
        }
    }
    // Qualifying for the finale does not guarantee a championship medal.
    EXPECT(game_reset_profile(g));
    finish(g, 4, false);
    EXPECT(game_submit_initials(g));
    finish(g, 4, false);
    finish(g, 8, false);
    EXPECT(game_championship_rank(g) > 3 && !g->championship_celebration);
    EXPECT(game_championship_finished(g) && !g->profile.championship_eliminated);
    EXPECT(game_championship_points(g, 0) == 11 && g->profile.championship_medal == 0);
    // Two wins secure silver even with fourth place in the finale.
    EXPECT(game_reset_profile(g));
    finish(g, 1, false);
    EXPECT(game_submit_initials(g));
    finish(g, 1, false);
    finish(g, 4, false);
    EXPECT(game_championship_rank(g) == 2 && g->championship_celebration);
    EXPECT(g->profile.championship_medal == 2 && game_championship_points(g, 0) == 25);
    // Every non-qualifying place and DNF ends the run on either opening stage.
    for (int stage = 0; stage < TRACK_COUNT - 1; stage++) {
        for (int rank = 0; rank <= CAR_COUNT; rank++) {
            if (rank >= 1 && rank <= 4)
                continue;
            EXPECT(game_reset_profile(g));
            if (stage) {
                finish(g, 1, false);
                EXPECT(game_submit_initials(g));
            }
            finish(g, rank ? rank : 1, rank == 0);
            if (g->initials_pending)
                EXPECT(game_submit_initials(g));
            EXPECT(g->championship_scored && !g->championship_advanced);
            EXPECT(g->profile.championship_eliminated && game_championship_finished(g));
            EXPECT(g->profile.championship_stages == stage + 1);
            EXPECT(g->profile.championship_completed == stage && g->unlocked_track == -1);
            EXPECT(!game_track_unlocked(&g->profile, stage + 1));
            EXPECT(game_next_track(g) == -1 && !g->championship_celebration);
            EXPECT(g->profile.championship_medal == 0);
            if (!rank)
                EXPECT(game_championship_points(g, 0) == (stage ? 10 : 0));
            Profile before = g->profile;
            game_results(g);
            game_tick(g, (Controls){0}, FIXED_DT, false);
            EXPECT(memcmp(&before, &g->profile, sizeof before) == 0);
            game_continue(g);
            EXPECT(g->screen == SCREEN_MENU && g->selected == 0);
            EXPECT(game_load(g));
            EXPECT(g->profile.championship_eliminated && g->selected == 0);
            EXPECT(g->profile.championship_completed == stage);
            unsigned serial = g->profile.record_serial;
            game_set_mode(g, MODE_ARCADE);
            game_set_mode(g, MODE_CHAMPIONSHIP);
            EXPECT(g->selected == 0 && g->profile.championship_eliminated);
            game_continue(g);
            EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == 0);
            EXPECT(!g->profile.championship_eliminated && g->profile.championship_stages == 0);
            EXPECT(game_championship_points(g, 0) == 0);
            EXPECT(g->profile.championship_completed == stage && g->profile.record_serial == serial);
        }
    }
    // Pending rivals cannot be skipped; the final score is saved exactly once.
    EXPECT(game_reset_profile(g));
    finish(g, 4, false);
    EXPECT(game_submit_initials(g));
    game_continue(g);
    g->screen = SCREEN_RACE;
    g->cars[0].finished = true;
    g->cars[0].finish_time = 80;
    game_results(g);
    EXPECT(!g->championship_scored && game_next_track(g) == -1);
    game_continue(g);
    EXPECT(g->screen == SCREEN_RESULTS && g->selected == 1);
    g->time = 221;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    EXPECT(g->championship_scored && g->profile.championship_stages == 2);
    // Ties count back wins before stable driver order.
    EXPECT(game_reset_profile(g));
    g->profile.championship_completed = g->profile.championship_stages = 2;
    g->profile.championship_places[0][0] = 1;
    g->profile.championship_places[1][0] = 7;
    g->profile.championship_places[0][1] = g->profile.championship_places[1][1] = 3;
    EXPECT(game_championship_points(g, 0) == game_championship_points(g, 1));
    EXPECT(game_championship_precedes(g, 0, 1) && !game_championship_precedes(g, 1, 0));
    g->profile.championship_places[1][0] = 2;
    g->profile.championship_places[0][1] = 2;
    g->profile.championship_places[1][1] = 1;
    EXPECT(game_championship_precedes(g, 0, 1) && !game_championship_precedes(g, 1, 0));
    // Demo runs cannot mutate standings or records.
    g->test_mode = true;
    Profile before = g->profile;
    finish(g, 1, false);
    EXPECT(memcmp(&before, &g->profile, sizeof before) == 0);
    g->test_mode = false;
    const char *v5 = "TOYCARS 5\n0 1 0 1 1 1\n82 0 0\n0 0 0\n1\n"
                     "CHAMPIONSHIP 1 2\n8 1 2 3 4 5 6 7\n0 0 0 0 0 0 0 0\n"
                     "0 0 0 0 0 0 0 0\nRECORDS 0 AAA 0\n0 0\n0 0\n0 0\n";
    EXPECT(SDL_SaveFile(path, v5, strlen(v5)));
    EXPECT(game_load(g));
    EXPECT(g->profile.championship_stages == 1 && g->selected == 1);
    EXPECT(!g->profile.championship_eliminated && g->profile.championship_medal == 2);
    EXPECT(profile_save(&g->profile));
    EXPECT(game_load(g));
    EXPECT(g->profile.championship_places[0][0] == 8 && !g->profile.championship_eliminated);
    const char *legacy = "TOYCARS 1\n2 0 1 1 0 1\n82 83 84\n1 2 0\n";
    EXPECT(SDL_SaveFile(path, legacy, strlen(legacy)));
    profile_load(&g->profile);
    EXPECT(g->profile.championship_completed == 2 && g->profile.color == 2);
    EXPECT(g->profile.championship_stages == 0 && g->profile.championship_medal == 0);
    EXPECT(g->profile.best[2] == 84 && g->profile.medals[1] == 2);
    legacy = "TOYCARS 1\n2 0 1 1 0 1\n82 83 84\n0 3 3\n";
    EXPECT(SDL_SaveFile(path, legacy, strlen(legacy)));
    profile_load(&g->profile);
    EXPECT(g->profile.championship_completed == 0); // No gaps in the route.
    const char *invalid[] = {
        "garbage", "TOYCARS 2\n0", "TOYCARS 5\n0 1 1 1 1 1\n0 0 0\n0 0 0\n3",
        "TOYCARS 2\n0 1 1 1 1 1\n0 0 0\n0 0 0\n4",
        "TOYCARS 2\n0 1 1 1 1 1\n0 0 0\n0 0 0\n-1",
        "TOYCARS 6\n0 1 1 1 1 1\n0 0 0\n0 0 0\n3\nCHAMPIONSHIP 1 0 2\n",
        "TOYCARS 6\n0 1 1 1 1 1\n0 0 0\n0 0 0\n3\nCHAMPIONSHIP 0 0 1\n",
        "TOYCARS 6\n0 1 1 1 1 1\n0 0 0\n0 0 0\n3\nCHAMPIONSHIP 3 0 1\n",
        "TOYCARS 2\n0 1 1 1 1 1\n0 0 0\n0 0 0"
    };
    for (size_t i = 0; i < SDL_arraysize(invalid); i++) {
        EXPECT(SDL_SaveFile(path, invalid[i], strlen(invalid[i])));
        profile_load(&g->profile);
        EXPECT(g->profile.championship_completed == 0 && g->profile.best[0] == 0);
    }
    test_records(g, path);
    test_reset(g, path);
    test_player_name(g, path);
    test_result_save_failure(g, path);
    test_settings_save_failure(g, path);
    test_dnf_classification(g);
    SDL_RemovePath(path);
    free(g);
    printf("Progression tests: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
