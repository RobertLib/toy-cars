// Exercise real result handling and persistence without touching player data.
#include "game.h"
#include <math.h>
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
    g->profile.arcade_night = true;
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

static void test_night_option(Game *g, const char *path) {
    // Championship stages keep their own time of day; Arcade follows the choice.
    profile_load(&g->profile);
    for (int track = 0; track < TRACK_COUNT; track++) {
        game_set_mode(g, MODE_CHAMPIONSHIP);
        g->selected = track;
        g->profile.arcade_night = !TRACK_INFO[track].night;
        EXPECT(game_night(g) == TRACK_INFO[track].night);
        game_set_mode(g, MODE_ARCADE);
        g->selected = track;
        EXPECT(game_night(g) == g->profile.arcade_night);
    }
    EXPECT(!TRACK_INFO[0].night && TRACK_INFO[TRACK_COUNT - 1].night);
    // Each cup's final runs at night.
    for (int c = 0; c < CUP_COUNT; c++)
        EXPECT(TRACK_INFO[CUPS[c].first + CUPS[c].count - 1].night);
    g->profile.arcade_night = false;
    g->force_night = true;
    EXPECT(game_night(g));
    g->force_night = false;
    // Version 6 predates the option and keeps racing by day.
    const char *v6 = "TOYCARS 6\n0 1 0 1 1 1\n82 0 0\n0 0 0\n1\n"
                     "CHAMPIONSHIP 0 0 0\n0 0 0 0 0 0 0 0\n0 0 0 0 0 0 0 0\n"
                     "0 0 0 0 0 0 0 0\nRECORDS 0 AAA 0\n0 0\n0 0\n0 0\n";
    EXPECT(SDL_SaveFile(path, v6, strlen(v6)));
    profile_load(&g->profile);
    EXPECT(!g->profile.arcade_night && g->profile.championship_completed == 1);
    EXPECT(g->profile.best[0] == 82);
    g->profile.arcade_night = true;
    EXPECT(profile_save(&g->profile));
    Profile saved;
    profile_load(&saved);
    EXPECT(saved.arcade_night && memcmp(&saved, &g->profile, sizeof saved) == 0);
    const char *invalid[] = {
        "TOYCARS 7\n0 1 0 1 1 1\n82 0 0\n0 0 0\n1\nCHAMPIONSHIP 0 0 0\n0 0 0 0 0 0 0 0\n"
        "0 0 0 0 0 0 0 0\n0 0 0 0 0 0 0 0\nOPTIONS 2\nRECORDS 0 AAA 0\n0 0\n0 0\n0 0\n",
        "TOYCARS 7\n0 1 0 1 1 1\n82 0 0\n0 0 0\n1\nCHAMPIONSHIP 0 0 0\n0 0 0 0 0 0 0 0\n"
        "0 0 0 0 0 0 0 0\n0 0 0 0 0 0 0 0\nRECORDS 0 AAA 0\n0 0\n0 0\n0 0\n"};
    for (size_t i = 0; i < SDL_arraysize(invalid); i++) {
        EXPECT(SDL_SaveFile(path, invalid[i], strlen(invalid[i])));
        profile_load(&saved);
        EXPECT(!saved.arcade_night && saved.best[0] == 0 && saved.championship_completed == 0);
    }
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
    for (int stage = 1; stage < CUPS[0].count; stage++) {
        EXPECT(game_reset_profile(g));
        g->test_mode = false;
        g->profile.championship_completed = stage;
        g->profile.championship_stages[0] = stage;
        EXPECT(profile_save(&g->profile));
        game_set_mode(g, MODE_CHAMPIONSHIP);
        EXPECT(SDL_CreateDirectory(temp));
        finish(g, 1, false);
        unsigned serial = g->profile.record_serial;
        EXPECT(game_save_warning(g) && !g->initials_pending);
        // Winning the final opens the next cup; earlier stages open the next track.
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
        EXPECT(stage == CUPS[0].count - 1 ? g->screen == SCREEN_RESULTS :
                   g->screen == SCREEN_COUNTDOWN && g->selected == stage + 1);
    }
    // Explicitly continuing unsaved retains pending data across a new race,
    // including a DNF, until a later successful save writes the whole profile.
    EXPECT(game_reset_profile(g));
    g->profile.championship_completed = 1;
    g->profile.championship_stages[0] = 1;
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
    // A DNF in the final leaves the Pro Cup locked unless the points still earn a medal.
    EXPECT(disk.championship_completed == (disk.championship_medal[0] ? 3 : 2));
    EXPECT(disk.records[1].history_count == 1);
    EXPECT(!g->record_save_failed && !g->save_failure_acknowledged);
    // Retrying an elimination save keeps previous unlocks, medals and records.
    EXPECT(game_reset_profile(g));
    g->profile.championship_completed = TRACK_COUNT - 1;
    g->profile.championship_medal[0] = 3;
    EXPECT(profile_save(&g->profile));
    EXPECT(SDL_CreateDirectory(temp));
    finish(g, 5, false);
    EXPECT(g->profile.championship_eliminated[0] && game_save_warning(g));
    EXPECT(!g->championship_celebration && g->profile.championship_medal[0] == 3);
    game_continue(g);
    EXPECT(g->screen == SCREEN_RESULTS);
    EXPECT(SDL_RemovePath(temp));
    EXPECT(game_submit_initials(g));
    profile_load(&disk);
    EXPECT(disk.championship_eliminated[0] && disk.championship_completed == TRACK_COUNT - 1);
    EXPECT(disk.championship_medal[0] == 3 && disk.records[0].history_count == 1);
    game_continue(g);
    game_continue(g);
    EXPECT(g->selected == 0 && !g->profile.championship_eliminated[0]);
    EXPECT(g->profile.championship_medal[0] == 3 &&
           g->profile.championship_completed == TRACK_COUNT - 1);
    EXPECT(g->profile.records[0].history_count == 1);
}

// The later cups open one by one with a medal, and each keeps its own series.
static void test_cups(Game *g, const char *path) {
    EXPECT(game_reset_profile(g));
    g->test_mode = false;
    EXPECT(!game_track_unlocked(&g->profile, CUPS[1].first));
    // A fourth place overall in the Rookie Cup completes it without a medal.
    for (int stage = 0; stage < CUPS[0].count; stage++) {
        finish(g, stage == CUPS[0].count - 1 ? 8 : 4, false);
        if (g->initials_pending)
            EXPECT(game_submit_initials(g));
        if (stage < CUPS[0].count - 1)
            game_continue(g);
    }
    EXPECT(game_championship_finished(g) && game_championship_rank(g) > 3);
    EXPECT(!game_track_unlocked(&g->profile, CUPS[1].first) && g->unlocked_track == -1);
    // A locked cup previews its first track but cannot start.
    game_select_cup(g, 1);
    EXPECT(g->selected == CUPS[1].first);
    g->screen = SCREEN_MENU;
    game_start(g);
    EXPECT(g->screen == SCREEN_MENU);
    // A Rookie Cup medal opens the Pro Cup.
    game_select_cup(g, 0);
    for (int stage = 0; stage < CUPS[0].count; stage++) {
        finish(g, 1, false);
        if (g->initials_pending)
            EXPECT(game_submit_initials(g));
        if (stage < CUPS[0].count - 1)
            game_continue(g);
    }
    EXPECT(g->profile.championship_medal[0] == 3 && g->championship_celebration);
    EXPECT(g->unlocked_track == CUPS[1].first && game_track_unlocked(&g->profile, CUPS[1].first));
    EXPECT(!game_track_unlocked(&g->profile, CUPS[1].first + 1));
    Profile disk;
    profile_load(&disk);
    EXPECT(disk.championship_completed == CUPS[1].first && disk.championship_medal[0] == 3);
    // The Pro Cup runs its own series; leaving it half way keeps both cups' state.
    game_select_cup(g, 1);
    EXPECT(g->selected == CUPS[1].first && game_cup(g) == 1);
    finish(g, 2, false);
    EXPECT(g->initials_pending); // A cup's opening stage asks for initials.
    EXPECT(game_submit_initials(g));
    EXPECT(g->profile.championship_stages[1] == 1 && g->profile.championship_stages[0] == CUPS[0].count);
    EXPECT(game_championship_points(g, 0) == 8 && game_next_track(g) == CUPS[1].first + 1);
    game_leave_results(g);
    game_select_cup(g, 0);
    EXPECT(g->selected == 0 && game_cup_fresh(g, 0)); // A completed cup starts over.
    game_select_cup(g, 1);
    EXPECT(g->selected == CUPS[1].first + 1);
    EXPECT(game_load(g));
    EXPECT(game_cup(g) == 1 && g->selected == CUPS[1].first + 1); // Opens on the furthest cup.
    // Eliminated from the Pro Cup: the Rookie Cup medal and unlocks remain.
    finish(g, 6, false);
    EXPECT(g->profile.championship_eliminated[1] && !g->profile.championship_eliminated[0]);
    EXPECT(g->profile.championship_medal[0] == 3 && !game_track_unlocked(&g->profile, CUPS[1].first + 2));
    game_continue(g);
    EXPECT(g->screen == SCREEN_MENU && g->selected == CUPS[1].first);
    // Win the Pro and Master Cups; the last final opens nothing further.
    for (int c = 1; c < CUP_COUNT; c++) {
        game_select_cup(g, c);
        for (int stage = 0; stage < CUPS[c].count; stage++) {
            EXPECT(g->selected == CUPS[c].first + stage);
            finish(g, 1, false);
            if (g->initials_pending)
                EXPECT(game_submit_initials(g));
            if (stage < CUPS[c].count - 1) {
                EXPECT(g->unlocked_track == g->selected + 1 || game_track_unlocked(&g->profile, g->selected + 1));
                game_continue(g);
            }
        }
        EXPECT(g->profile.championship_medal[c] == 3 && game_championship_finished(g));
        game_continue(g);
        game_continue(g);
    }
    EXPECT(g->profile.championship_completed == TRACK_COUNT - 1);
    for (int k = 0; k < TRACK_COUNT; k++)
        EXPECT(game_track_unlocked(&g->profile, k));
    // A saved profile reads back identically.
    EXPECT(profile_save(&g->profile));
    profile_load(&disk);
    EXPECT(memcmp(&disk, &g->profile, sizeof disk) == 0);
    // A version 7 series that finished with a medal opens the Pro Cup on import.
    for (int medal = 0; medal < 2; medal++) {
        char v7[400];
        SDL_snprintf(v7, sizeof v7, "TOYCARS 7\n0 1 0 1 1 1\n80 81 82\n1 2 %d\n3\nCHAMPIONSHIP 3 %d 0\n"
                     "1 2 3 4 5 6 7 8\n1 2 3 4 5 6 7 8\n%s\nOPTIONS 1\n"
                     "RECORDS 0 AAA 0\n0 0\n0 0\n0 0\n", medal ? 3 : 0, medal ? 3 : 0,
                     medal ? "1 2 3 4 5 6 7 8" : "8 2 3 4 5 6 7 1");
        EXPECT(SDL_SaveFile(path, v7, strlen(v7)));
        profile_load(&disk);
        EXPECT(disk.championship_completed == (medal ? 3 : 2) && disk.arcade_night);
        EXPECT(disk.championship_stages[0] == 3 && disk.championship_medal[0] == (medal ? 3 : 0));
        EXPECT(disk.championship_stages[1] == 0 && disk.best[2] == 82 && disk.best[3] == 0);
    }
    // Version 8 rejects places beyond a cup's scored stages and a short cup list.
    const char *invalid[] = {
        "TOYCARS 8\n0 1 0 1 1 1\n0 0 0 0 0 0 0 0 0 0\n0 0 0 0 0 0 0 0 0 0\n0\nCHAMPIONSHIP 2\n",
        "TOYCARS 8\n0 1 0 1 1 1\n0 0 0 0 0 0 0 0 0 0\n0 0 0 0 0 0 0 0 0 0\n0\nCHAMPIONSHIP 3\n"
        "0 0 0\n0 0 0\n0 0 0\n1 2 3 4 5 6 7 8\n"};
    for (size_t i = 0; i < SDL_arraysize(invalid); i++) {
        EXPECT(SDL_SaveFile(path, invalid[i], strlen(invalid[i])));
        profile_load(&disk);
        EXPECT(disk.championship_completed == 0 && disk.best[0] == 0);
    }
    EXPECT(game_reset_profile(g));
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

// The garage: prize money, upgrades, cars, their persistence and the rivals.
static void test_garage(Game *g, const char *path) {
    EXPECT(game_reset_profile(g));
    Profile *p = &g->profile;
    EXPECT(p->coins == 0 && p->car == CAR_TRAILBLAZER && p->owned[CAR_TRAILBLAZER]);
    for (int m = 1; m < CAR_MODEL_COUNT; m++)
        EXPECT(!p->owned[m] && CAR_INFO[m].price > CAR_INFO[m - 1].price);
    // Stock cars are slower than the reference car; upgrades take them past it.
    const int full[UPGRADE_COUNT] = {UPGRADE_LEVELS, UPGRADE_LEVELS, UPGRADE_LEVELS, UPGRADE_LEVELS};
    CarTune reference = {0};
    for (int m = 0; m < CAR_MODEL_COUNT; m++) {
        CarTune stock = car_tune(m, NULL), best = car_tune(m, full);
        EXPECT(car_top_speed(&stock) < car_top_speed(&reference));
        EXPECT(car_rating(&stock) < car_rating(&reference) && car_rating(&best) > car_rating(&reference));
        for (int level = 1; level < UPGRADE_LEVELS; level++)
            EXPECT(upgrade_price(m, UPGRADE_ENGINE, level) > upgrade_price(m, UPGRADE_ENGINE, level - 1));
        EXPECT(upgrade_price(m, UPGRADE_TIRES, UPGRADE_LEVELS) == 0);
    }
    CarTune comet = car_tune(CAR_COMET, full), trailblazer = car_tune(CAR_TRAILBLAZER, full);
    EXPECT(car_rating(&comet) > car_rating(&trailblazer));
    EXPECT(fabsf(car_top_speed(&reference) - 33.54f) < .01f);
    // Better places, later cups and harder rivals pay more; Arcade pays half.
    EXPECT(race_prize(0, 1, MODE_CHAMPIONSHIP, 1) > race_prize(0, 1, MODE_CHAMPIONSHIP, 2));
    EXPECT(race_prize(0, 1, MODE_CHAMPIONSHIP, CAR_COUNT) > 0 && race_prize(0, 1, MODE_CHAMPIONSHIP, 0) == 0);
    EXPECT(race_prize(TRACK_COUNT - 1, 1, MODE_CHAMPIONSHIP, 1) > race_prize(0, 1, MODE_CHAMPIONSHIP, 1));
    EXPECT(race_prize(0, 2, MODE_CHAMPIONSHIP, 1) > race_prize(0, 1, MODE_CHAMPIONSHIP, 1));
    EXPECT(race_prize(0, 1, MODE_CHAMPIONSHIP, 1) > race_prize(0, 0, MODE_CHAMPIONSHIP, 1));
    EXPECT(race_prize(0, 1, MODE_ARCADE, 1) * 2 == race_prize(0, 1, MODE_CHAMPIONSHIP, 1));
    EXPECT(cup_prize(0, 1, 3) > cup_prize(0, 1, 2) && cup_prize(0, 1, 0) == 0);
    game_set_mode(g, MODE_ARCADE);
    g->selected = 0;
    g->test_mode = false;
    finish(g, 3, false);
    EXPECT(g->prize == race_prize(0, p->difficulty, MODE_ARCADE, 3) && p->coins == g->prize);
    int before = p->coins;
    finish(g, 1, true);
    EXPECT(g->prize == 0 && p->coins == before); // A DNF earns nothing.
    g->test_mode = true;
    finish(g, 1, false);
    EXPECT(g->prize == 0 && p->coins == before); // Neither do demo races.
    g->test_mode = false;
    // A cup's final adds the medal's bonus to the race prize.
    EXPECT(game_reset_profile(g));
    game_set_mode(g, MODE_CHAMPIONSHIP);
    p->championship_completed = p->championship_stages[0] = 2;
    for (int track = 0; track < 2; track++)
        for (int i = 0; i < CAR_COUNT; i++)
            p->championship_places[track][i] = i + 1;
    g->selected = 2;
    finish(g, 1, false);
    EXPECT(p->championship_medal[0] == 3 && g->cup_prize == cup_prize(0, p->difficulty, 3));
    EXPECT(g->prize == race_prize(2, p->difficulty, MODE_CHAMPIONSHIP, 1));
    EXPECT(p->coins == g->prize + g->cup_prize);
    game_leave_results(g);
    // Upgrades cost coins, stop at the last level and reach the race.
    EXPECT(game_reset_profile(g));
    g->garage_car = CAR_TRAILBLAZER;
    p->coins = upgrade_price(CAR_TRAILBLAZER, UPGRADE_ENGINE, 0) - 1;
    EXPECT(!game_buy_upgrade(g, UPGRADE_ENGINE) && p->upgrades[CAR_TRAILBLAZER][UPGRADE_ENGINE] == 0);
    p->coins++;
    EXPECT(game_buy_upgrade(g, UPGRADE_ENGINE) && p->coins == 0);
    EXPECT(p->upgrades[CAR_TRAILBLAZER][UPGRADE_ENGINE] == 1);
    p->coins = 100000;
    while (game_buy_upgrade(g, UPGRADE_TIRES))
        ;
    EXPECT(p->upgrades[CAR_TRAILBLAZER][UPGRADE_TIRES] == UPGRADE_LEVELS);
    game_set_mode(g, MODE_ARCADE);
    game_start(g);
    CarTune tuned = car_tune(CAR_TRAILBLAZER, p->upgrades[CAR_TRAILBLAZER]);
    EXPECT(g->cars[0].model == CAR_TRAILBLAZER && g->cars[0].tune.grip == tuned.grip);
    EXPECT(g->cars[0].tune.power == tuned.power && tuned.grip > car_tune(CAR_TRAILBLAZER, NULL).grip);
    // The turntable shows any car; only an owned one becomes the player's ride.
    game_browse_car(g, CAR_COMET);
    EXPECT(g->garage_car == CAR_COMET && p->car == CAR_TRAILBLAZER);
    EXPECT(!game_buy_upgrade(g, UPGRADE_ENGINE));
    p->coins = CAR_INFO[CAR_COMET].price - 1;
    EXPECT(!game_buy_car(g, CAR_COMET) && !p->owned[CAR_COMET]);
    p->coins++;
    EXPECT(game_buy_car(g, CAR_COMET) && p->owned[CAR_COMET] && p->car == CAR_COMET && p->coins == 0);
    EXPECT(!game_buy_car(g, CAR_COMET));
    game_browse_car(g, CAR_COMET + 1);
    EXPECT(g->garage_car == CAR_TRAILBLAZER && p->car == CAR_TRAILBLAZER);
    game_browse_car(g, CAR_TRAILBLAZER - 1);
    EXPECT(g->garage_car == CAR_COMET && p->car == CAR_COMET);
    game_start(g);
    EXPECT(g->cars[0].model == CAR_COMET);
    // The garage survives a save, and a damaged garage is not trusted.
    p->coins = 1234;
    EXPECT(profile_save(p));
    Profile saved;
    profile_load(&saved);
    EXPECT(memcmp(&saved, p, sizeof saved) == 0);
    size_t size;
    char *text = SDL_LoadFile(path, &size);
    EXPECT(text && strstr(text, "GARAGE 1234 3\n"));
    const char *damaged[] = {"GARAGE 1234 2\n", "GARAGE -5 3\n"};
    for (size_t i = 0; text && i < SDL_arraysize(damaged); i++) {
        char *copy = SDL_strdup(text), *at = strstr(copy, "GARAGE 1234 3\n");
        memcpy(at, damaged[i], strlen(damaged[i]));
        EXPECT(SDL_SaveFile(path, copy, strlen(copy)));
        profile_load(&saved);
        EXPECT(saved.coins == 0 && saved.car == CAR_TRAILBLAZER && !saved.owned[CAR_COMET]);
        SDL_free(copy);
    }
    SDL_free(text);
    // A save from before the garage brings a third place's prize per stage reached.
    char v8[2048];
    int len = SDL_snprintf(v8, sizeof v8, "TOYCARS 8\n0 1 0 1 1 1\n0 0 0 0 0 0 0 0 0 0\n"
                                          "0 0 0 0 0 0 0 0 0 0\n4\nCHAMPIONSHIP 3\n3 1 0\n1 0 0\n0 0 0\n");
    for (int track = 0; track < TRACK_COUNT; track++)
        len += SDL_snprintf(v8 + len, sizeof v8 - len, "0 0 0 0 0 0 0 0\n");
    len += SDL_snprintf(v8 + len, sizeof v8 - len, "OPTIONS 0\nRECORDS 0 AAA 0\n");
    for (int track = 0; track < TRACK_COUNT; track++)
        len += SDL_snprintf(v8 + len, sizeof v8 - len, "0 0\n");
    EXPECT(SDL_SaveFile(path, v8, (size_t)len));
    profile_load(&saved);
    int purse = 0;
    for (int track = 0; track < 4; track++)
        purse += race_prize(track, 1, MODE_CHAMPIONSHIP, 3);
    EXPECT(saved.championship_completed == 4 && saved.coins == purse);
    EXPECT(saved.car == CAR_TRAILBLAZER && saved.owned[CAR_TRAILBLAZER] && !saved.owned[CAR_SPRITE]);
    // Rivals bring better cars to later circuits and to harder races.
    int levels[UPGRADE_COUNT];
    expected_upgrades(0, levels);
    EXPECT(!levels[0] && !levels[1] && !levels[2] && !levels[3]);
    for (int i = 1; i < CAR_COUNT; i++) {
        for (int track = 1; track < TRACK_COUNT; track++) {
            CarTune a = rival_tune(track - 1, 1, i), b = rival_tune(track, 1, i);
            EXPECT(b.power + b.speed + b.grip > a.power + a.speed + a.grip);
        }
        for (int d = 1; d < 3; d++) {
            CarTune a = rival_tune(4, d - 1, i), b = rival_tune(4, d, i);
            EXPECT(b.power + b.speed + b.grip > a.power + a.speed + a.grip);
        }
    }
    EXPECT(game_reset_profile(g));
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
    const CupInfo *rookie = &CUPS[0];
    for (int rank = 1; rank <= 4; rank++) {
        EXPECT(game_reset_profile(g));
        for (int stage = 0; stage < rookie->count; stage++) {
            game_set_mode(g, MODE_CHAMPIONSHIP);
            EXPECT(g->selected == stage);
            finish(g, rank, false);
            EXPECT(g->championship_scored && g->championship_advanced);
            EXPECT(g->profile.championship_stages[0] == stage + 1);
            bool final = stage == rookie->count - 1;
            // The final opens the next cup only with a medal.
            EXPECT(g->profile.championship_completed == (!final ? stage + 1 : rank <= 3 ? stage + 1 : stage));
            EXPECT(g->initials_pending == (stage == 0));
            if (g->initials_pending) {
                SDL_strlcpy(g->initials, "ROB", sizeof g->initials);
                EXPECT(game_submit_initials(g));
            }
            Profile saved;
            profile_load(&saved);
            EXPECT(saved.championship_stages[0] == stage + 1);
            EXPECT(memcmp(saved.championship_places, g->profile.championship_places,
                          sizeof saved.championship_places) == 0);
            EXPECT(game_championship_rank(g) == rank);
            EXPECT(game_next_track(g) == (!final ? stage + 1 : -1));
            Profile before = g->profile;
            game_results(g);
            game_tick(g, (Controls){0}, FIXED_DT, false);
            EXPECT(memcmp(&before, &g->profile, sizeof before) == 0);
            if (!final) {
                // Reload and resume the next stage, including after a non-podium finish.
                g->profile = saved;
                game_set_mode(g, MODE_CHAMPIONSHIP);
                EXPECT(g->selected == stage + 1);
            } else {
                EXPECT(game_championship_finished(g));
                EXPECT(g->championship_celebration == (rank <= 3));
                EXPECT(saved.championship_medal[0] == (rank <= 3 ? 4 - rank : 0));
                EXPECT(g->unlocked_track == (rank <= 3 ? CUPS[1].first : -1));
                if (rank <= 3) {
                    game_continue(g);
                    EXPECT(!g->championship_celebration && g->screen == SCREEN_RESULTS);
                }
                game_continue(g);
                EXPECT(g->screen == SCREEN_MENU);
                game_continue(g);
                EXPECT(g->selected == 0 && g->profile.championship_stages[0] == 0);
                EXPECT(g->profile.championship_completed == (rank <= 3 ? CUPS[1].first : rookie->count - 1));
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
    EXPECT(game_championship_finished(g) && !g->profile.championship_eliminated[0]);
    EXPECT(game_championship_points(g, 0) == 11 && g->profile.championship_medal[0] == 0);
    // Two wins secure silver even with fourth place in the finale.
    EXPECT(game_reset_profile(g));
    finish(g, 1, false);
    EXPECT(game_submit_initials(g));
    finish(g, 1, false);
    finish(g, 4, false);
    EXPECT(game_championship_rank(g) == 2 && g->championship_celebration);
    EXPECT(g->profile.championship_medal[0] == 2 && game_championship_points(g, 0) == 25);
    // Every non-qualifying place and DNF ends the run on either opening stage.
    for (int stage = 0; stage < rookie->count - 1; stage++) {
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
            EXPECT(g->profile.championship_eliminated[0] && game_championship_finished(g));
            EXPECT(g->profile.championship_stages[0] == stage + 1);
            EXPECT(g->profile.championship_completed == stage && g->unlocked_track == -1);
            EXPECT(!game_track_unlocked(&g->profile, stage + 1));
            EXPECT(game_next_track(g) == -1 && !g->championship_celebration);
            EXPECT(g->profile.championship_medal[0] == 0);
            if (!rank)
                EXPECT(game_championship_points(g, 0) == (stage ? 10 : 0));
            Profile before = g->profile;
            game_results(g);
            game_tick(g, (Controls){0}, FIXED_DT, false);
            EXPECT(memcmp(&before, &g->profile, sizeof before) == 0);
            game_continue(g);
            EXPECT(g->screen == SCREEN_MENU && g->selected == 0);
            EXPECT(game_load(g));
            EXPECT(g->profile.championship_eliminated[0] && g->selected == 0);
            EXPECT(g->profile.championship_completed == stage);
            unsigned serial = g->profile.record_serial;
            game_set_mode(g, MODE_ARCADE);
            game_set_mode(g, MODE_CHAMPIONSHIP);
            EXPECT(g->selected == 0 && g->profile.championship_eliminated[0]);
            game_continue(g);
            EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == 0);
            EXPECT(!g->profile.championship_eliminated[0] && g->profile.championship_stages[0] == 0);
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
    EXPECT(g->championship_scored && g->profile.championship_stages[0] == 2);
    // Leaving to the menu races the pending rivals out instead of losing the stage.
    EXPECT(game_reset_profile(g));
    finish(g, 4, false);
    EXPECT(game_submit_initials(g));
    game_continue(g);
    g->screen = SCREEN_RACE;
    g->cars[0].finished = true;
    g->cars[0].finish_time = 80;
    game_results(g);
    EXPECT(!g->championship_scored && g->profile.championship_stages[0] == 1);
    game_leave_results(g);
    EXPECT(g->screen == SCREEN_MENU && g->championship_scored);
    EXPECT(g->profile.championship_stages[0] == 2 && g->profile.championship_places[1][0] > 0);
    EXPECT(g->selected == (g->profile.championship_eliminated[0] ? 0 : 2));
    for (int i = 1; i < CAR_COUNT; i++)
        EXPECT(g->cars[i].finished || g->cars[i].dnf);
    // Ties count back wins before stable driver order.
    EXPECT(game_reset_profile(g));
    g->profile.championship_completed = g->profile.championship_stages[0] = 2;
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
    EXPECT(g->profile.championship_stages[0] == 1 && g->selected == 1);
    EXPECT(!g->profile.championship_eliminated[0] && g->profile.championship_medal[0] == 2);
    EXPECT(profile_save(&g->profile));
    EXPECT(game_load(g));
    EXPECT(g->profile.championship_places[0][0] == 8 && !g->profile.championship_eliminated[0]);
    const char *legacy = "TOYCARS 1\n2 0 1 1 0 1\n82 83 84\n1 2 0\n";
    EXPECT(SDL_SaveFile(path, legacy, strlen(legacy)));
    profile_load(&g->profile);
    EXPECT(g->profile.championship_completed == 2 && g->profile.color == 2);
    EXPECT(g->profile.championship_stages[0] == 0 && g->profile.championship_medal[0] == 0);
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
    test_night_option(g, path);
    test_records(g, path);
    test_reset(g, path);
    test_player_name(g, path);
    test_result_save_failure(g, path);
    test_cups(g, path);
    test_settings_save_failure(g, path);
    test_dnf_classification(g);
    test_garage(g, path);
    SDL_RemovePath(path);
    game_free(g);
    free(g);
    printf("Progression tests: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
