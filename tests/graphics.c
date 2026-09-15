// Exercise the real event loop with scripted input and a replacement GL context.
// Profile writes are redirected to the build directory, never the player's data.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "renderer.h"
#include "ui.h"

static char pref_directory[2048];
static int drawn_frames, failures;
static bool reset_sent, resume_sent, portrait_sent, retry_sent, portrait, wide;
static UI *current_ui;
static float reset_time, portrait_time;
static Car reset_car;
static Profile portrait_profile;

#define EXPECT(condition, message)                                                                 \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL: %s\n", message);                                                \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static char *test_pref_path(const char *org, const char *app) {
    (void)org;
    (void)app;
    return SDL_strdup(pref_directory);
}
#define SDL_GetPrefPath test_pref_path
#include "../src/assets.c"
#undef SDL_GetPrefPath

static void push_key(SDL_Keycode key) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    SDL_PushEvent(&event);
}

static void push_tap(float x, float y) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 1;
    event.tfinger.fingerID = 11;
    event.tfinger.x = x / current_ui->width;
    event.tfinger.y = y / current_ui->height;
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_FINGER_UP;
    SDL_PushEvent(&event);
}

static bool test_poll_event(SDL_Event *event) {
    if (drawn_frames == 5 && !reset_sent) {
        reset_sent = true;
        SDL_Window *window = SDL_GL_GetCurrentWindow();
        SDL_GL_DestroyContext(SDL_GL_GetCurrentContext());
        if (!SDL_GL_CreateContext(window)) {
            fprintf(stderr, "Cannot create replacement GL context: %s\n", SDL_GetError());
            exit(2);
        }
        SDL_GL_SetSwapInterval(0);
        SDL_Event reset = {0};
        reset.type = SDL_EVENT_RENDER_DEVICE_RESET;
        reset.render.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&reset);
    }
    if (drawn_frames == 6 && !resume_sent) {
        resume_sent = true;
        push_key(SDLK_RETURN);
    }
    if (drawn_frames == 8 && !portrait_sent) {
        portrait_sent = true;
        push_key(SDLK_RETURN);
        push_key(SDLK_3);
        push_key(SDLK_M);
        SDL_Event pad = {0};
        pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        pad.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
        SDL_PushEvent(&pad);
        push_tap(120, 410);
    }
    if (drawn_frames == 11 && !retry_sent) {
        retry_sent = true;
        push_tap((current_ui->width - 920) * .5f + 80, current_ui->height - 64);
    }
    return SDL_PollEvent(event);
}

static void test_ui_size(UI *u, int w, int h, SDL_Window *window) {
    // Simulate aspect ratios independently of the attached monitor's size.
    ui_size(u, portrait ? 720 : wide ? 1600 : w, portrait ? 1280 : wide ? 450 : h, window);
}
static void test_resize(Renderer *r, int w, int h) {
    renderer_resize(r, wide ? 960 : w, wide ? 270 : h);
}

static void test_ui_draw(UI *u, Game *g, Renderer *r) {
    current_ui = u;
    drawn_frames++;
    if (drawn_frames == 1) {
        u->show_stats = true;
        g->screen = SCREEN_RACE;
        g->countdown = 0;
        g->time = 10;
        g->cars[0].fuel = 48;
    }
    if (drawn_frames == 6) {
        EXPECT(g->screen == SCREEN_PAUSE && g->time == reset_time,
               "context loss pauses the race without advancing its timer");
        EXPECT(g->cars[0].fuel == reset_car.fuel &&
                   vlen(vsub(g->cars[0].pos, reset_car.pos)) == 0 &&
                   g->cars[0].progress == reset_car.progress,
               "context replacement preserves the car, progress and fuel");
        EXPECT(u->show_stats && !u->fingers[0].active && u->width > 0,
               "context recovery preserves diagnostics and clears held input");
        EXPECT(glIsProgram(r->shader) && glIsProgram(u->program) &&
                   glIsTexture(u->fonts[0].texture) && glIsBuffer(r->car.vbo),
               "world and UI resources exist in the replacement context");
    }
    if (drawn_frames == 7)
        EXPECT(g->screen == SCREEN_RACE && g->time > reset_time,
               "explicit resume continues the restored race");
    if (drawn_frames == 8) {
        g->profile.best[0] = 900;
        g->profile.medals[0] = 0;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 80;
        game_results(g);
        EXPECT(g->profile.best[0] == 900 && g->profile.medals[0] == 0,
               "demo results leave records unchanged in memory");
        g->screen = SCREEN_SETTINGS;
        u->click_count = 1;
        u->clicks[0] = (UIClick){100, 350, 100, 350, false};
    }
    if (drawn_frames == 9) {
        EXPECT(g->screen == SCREEN_MENU && g->selected == 0 &&
                   g->profile.music == portrait_profile.music &&
                   g->profile.sound == portrait_profile.sound,
               "portrait blocks keyboard, gamepad and touch actions in the event loop");
        // Also cover clicks queued before the portrait draw guard takes effect.
        u->click_count = 1;
        u->clicks[0] = (UIClick){120, 410, 120, 410, false};
    }
    if (drawn_frames == 10)
        EXPECT(g->screen == SCREEN_PAUSE && g->time == portrait_time,
               "portrait pauses gameplay before simulation");

    ui_draw(u, g, r);

    if (drawn_frames == 5) {
        reset_time = g->time;
        reset_car = g->cars[0];
        u->fingers[0] = (Finger){.active = true, .id = 99};
    }
    if (drawn_frames == 8) {
        Profile saved;
        profile_load(&saved);
        EXPECT(saved.auto_accel == g->profile.auto_accel && !saved.auto_accel &&
                   saved.best[0] == 900 && saved.medals[0] == 0,
               "a real settings click saves settings without persisting demo records");
        g->selected = 0;
        g->screen = SCREEN_MENU;
        portrait_profile = g->profile;
        portrait = true;
    }
    if (drawn_frames == 9) {
        EXPECT(g->screen == SCREEN_MENU && u->click_count == 0 && !u->fingers[0].active,
               "the rotate overlay never activates a covered menu button");
        g->screen = SCREEN_RACE;
        portrait_time = g->time;
    }
    if (drawn_frames == 10) {
        portrait = false;
        wide = true;
        g->screen = SCREEN_RESULTS;
        u->show_stats = false;
    }
    if (drawn_frames == 11) {
        EXPECT(u->height - u->safe_top - u->safe_bottom >= 575.99f,
               "wide results have sufficient usable height");
        char path[2200];
        SDL_snprintf(path, sizeof path, "%swide-results.bmp", pref_directory);
        EXPECT(renderer_screenshot(r, path), "wide results screenshot is saved");
    }
    if (drawn_frames == 12)
        EXPECT(g->screen == SCREEN_COUNTDOWN && g->time == 0,
               "the visible retry button works in a wide window");
    if (drawn_frames == 13) {
        // Ordinary player finishes must still persist records and medals.
        g->screen = SCREEN_RACE;
        g->test_mode = false;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 70;
        game_results(g);
        Profile saved;
        profile_load(&saved);
        EXPECT(saved.best[0] == 70 && saved.medals[0] == 3,
               "a normal finish still saves its personal record and gold medal");
        g->test_mode = true;
    }
}

#define SDL_PollEvent test_poll_event
#define ui_size test_ui_size
#define renderer_resize test_resize
#define ui_draw test_ui_draw
#define main toycars_main
#include "../main.c"
#undef main
#undef ui_draw
#undef renderer_resize
#undef ui_size
#undef SDL_PollEvent

int main(int argc, char **argv) {
    if (argc != 2 || strlen(argv[1]) > sizeof pref_directory - 2) {
        fprintf(stderr, "Usage: ToyCarsGraphicsTests isolated-profile-directory\n");
        return 2;
    }
    SDL_snprintf(pref_directory, sizeof pref_directory, "%s/", argv[1]);
    if (!SDL_CreateDirectory(pref_directory))
        return 2;
    char path[2200];
    SDL_snprintf(path, sizeof path, "%sprofile.txt", pref_directory);
    SDL_RemovePath(path);
    char *args[] = {"ToyCars",     "--screen",   "race",   "--frames", "16",
                    "--benchmark", "--no-audio", "--size", "1280x720", NULL};
    int result = toycars_main((int)SDL_arraysize(args) - 1, args);
    EXPECT(result == 0 && drawn_frames == 16, "the event loop survives reset and completes");
    printf("Graphics integration tests: %s (%d failures)\n", failures ? "FAILED" : "PASSED",
           failures);
    return result || failures ? 1 : 0;
}
