#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "game.h"
#include "renderer.h"
#include "ui.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SDL_AtomicInt pending;
} Lifecycle;
enum { LIFE_CHANGED = 1, LIFE_BACKGROUND = 2, LIFE_PAUSE = 4 };
static bool SDLCALL lifecycle_watch(void *data, SDL_Event *e) {
    Lifecycle *l = data;
    if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND || e->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        int old, next;
        do {
            old = SDL_GetAtomicInt(&l->pending);
            // Preserve the need to pause even if both transitions arrive before
            // the main thread runs. The latest event controls audio/rendering.
            next = LIFE_CHANGED | (old & LIFE_PAUSE);
            if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND)
                next |= LIFE_BACKGROUND | LIFE_PAUSE;
        } while (!SDL_CompareAndSwapAtomicInt(&l->pending, old, next));
    }
    return true;
}
static bool apply_lifecycle(Lifecycle *life, Game *g, UI *u, Audio *audio, bool *background) {
    int pending = SDL_SetAtomicInt(&life->pending, 0);
    if (!pending)
        return false;
    *background = (pending & LIFE_BACKGROUND) != 0;
    if ((pending & LIFE_PAUSE) && (g->screen == SCREEN_RACE || g->screen == SCREEN_COUNTDOWN))
        g->screen = SCREEN_PAUSE;
    ui_clear_input(u);
    audio_pause(audio, *background);
    return true;
}
static void show_error(const char *message) {
    SDL_Log("%s: %s", message, SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ToyCars", message, NULL);
}
static SDL_Gamepad *open_gamepad(void) {
    int count;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_Gamepad *p = NULL;
    for (int i = 0; i < count && !p; i++)
        p = SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
    return p;
}
static void toggle_pause(Game *g, UI *u) {
    if (g->screen == SCREEN_RESULTS)
        game_leave_results(g);
    else if (g->screen == SCREEN_RESET_DATA)
        g->screen = SCREEN_SETTINGS;
    else if (g->screen == SCREEN_RACE || g->screen == SCREEN_COUNTDOWN)
        g->screen = SCREEN_PAUSE;
    else if (g->screen == SCREEN_PAUSE)
        g->screen = g->countdown > 0 ? SCREEN_COUNTDOWN : SCREEN_RACE;
    else
        g->screen = SCREEN_MENU;
    ui_clear_input(u);
}

// Garage keys: browse the cars, pick an upgrade row, buy one directly by its
// number. Confirm buys the car on show or the picked upgrade, or else heads
// to the race menu.
static void garage_input(Game *g, UI *u, int browse, int focus, int buy, bool confirm) {
    if (browse) {
        game_browse_car(g, g->garage_car + browse);
        u->garage_focus = 0;
    }
    if (focus)
        u->garage_focus = u->garage_focus ? SDL_clamp(u->garage_focus + focus, 1, UPGRADE_COUNT)
                                          : focus > 0 ? 1 : UPGRADE_COUNT;
    if (buy >= 0)
        game_buy_upgrade(g, (UpgradeKind)buy);
    if (confirm) {
        if (!g->profile.owned[g->garage_car])
            game_buy_car(g, g->garage_car);
        else if (u->garage_focus)
            game_buy_upgrade(g, (UpgradeKind)(u->garage_focus - 1));
        else
            g->screen = SCREEN_MENU;
    }
    ui_clear_input(u);
}

static bool restore_graphics(Renderer *r, UI *u) {
    bool show_stats = u->show_stats;
    bool mobile = u->mobile;
    float fps = u->fps;
    // These names belong to the lost context. Deleting them in the replacement
    // context could delete unrelated objects with reused names.
    memset(r, 0, sizeof *r);
    memset(u, 0, sizeof *u);
    bool ok = renderer_init(r) && ui_init(u);
    u->show_stats = show_stats;
    u->mobile = mobile;
    u->fps = fps;
    return ok;
}

int main(int argc, char **argv) {
    bool test = false, autoplay = false, noaudio = false, benchmark = false, touch_override = false;
    bool no_vsync = false, show_fps = false, night = false, balance = false;
    float train_at = 0;
    int track = -1, frames = 0, width = 1440, height = 900, preview_car = -1;
    const char *screenshot = NULL, *screen = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test"))
            test = true;
        else if (!strcmp(argv[i], "--balance"))
            balance = true;
        else if (!strcmp(argv[i], "--autoplay"))
            autoplay = true;
        else if (!strcmp(argv[i], "--no-audio"))
            noaudio = true;
        else if (!strcmp(argv[i], "--benchmark"))
            benchmark = true;
        else if (!strcmp(argv[i], "--no-vsync"))
            no_vsync = true;
        else if (!strcmp(argv[i], "--fps"))
            show_fps = true;
        else if (!strcmp(argv[i], "--touch"))
            touch_override = true;
        else if (!strcmp(argv[i], "--night"))
            night = true;
        else if (!strcmp(argv[i], "--car") && i + 1 < argc) {
            i++;
            for (int k = 0; k < CAR_MODEL_COUNT; k++)
                if (!SDL_strcasecmp(argv[i], CAR_INFO[k].name))
                    preview_car = k;
            if (preview_car < 0) {
                fprintf(stderr, "Unknown car: %s\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "--train-at") && i + 1 < argc)
            train_at = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--track") && i + 1 < argc) {
            track = game_track_index(argv[++i]);
            if (track < 0) {
                fprintf(stderr, "Unknown track: %s\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc)
            screen = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            i++;
            if (sscanf(argv[i], "%dx%d", &width, &height) != 2 || width < 480 || height < 320) {
                fprintf(stderr, "Invalid window size\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--help")) {
            printf("ToyCars\n  --test                     Run deterministic simulation tests (no "
                   "window; with --track, only that circuit)\n"
                   "  --balance                  Race modelled players against the rivals and\n"
                   "                             print where they finish (with --track, one circuit)\n  --track ID                 country, beach, winter, canyon, autumn,\n"
                   "                             jungle, sakura, harbor, highland or volcano\n  --screen "
                   "menu|race|garage|help|settings|results|championship|records\n  --autoplay      "
                   "           Drive the "
                   "player with AI for a demo\n  --frames N                 Exit after N rendered "
                   "frames\n  --screenshot file.bmp      Capture the last frame\n  --size "
                   "WIDTHxHEIGHT        Window size\n  --benchmark                Disable vsync; "
                   "fixed frame delta\n  --no-vsync                 Disable vsync at normal game speed\n"
                   "  --fps                      Show FPS and rendering statistics\n"
                   "  --no-audio                 Do not open an audio device\n  "
                   "--touch                    Preview mobile layout and touch controls\n"
                   "  --night                    Race at night in every mode\n"
                   "  --car NAME                 Preview Trailblazer, Sprite, Bruiser or Comet\n"
                   "                             (garage and race screens; nothing is saved)\n"
                   "  --train-at SECONDS         Send the first train to the crossing at this race time\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }
    if (balance) {
        if (!SDL_Init(0))
            return 1;
        int code = game_balance(track);
        SDL_Quit();
        return code;
    }
    if (test) {
        if (!SDL_Init(0))
            return 1;
        int code = game_tests(track);
        if (track <= 0)
            code |= ui_tests();
        SDL_Quit();
        return code;
    }
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (noaudio ? 0 : SDL_INIT_AUDIO))) {
        SDL_Log("SDL: %s", SDL_GetError());
        return 1;
    }
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    SDL_Window *window = SDL_CreateWindow("ToyCars - The Little Rally Club", width, height, flags);
    if (!window) {
        show_error("Could not open the game window.");
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, touch_override ? 640 : 800, touch_override ? 320 : 450);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        show_error("ToyCars needs OpenGL 3.3 or OpenGL ES 3.0.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (!SDL_GL_SetSwapInterval((benchmark || no_vsync) ? 0 : 1))
        SDL_Log("Could not set swap interval: %s", SDL_GetError());
    int swap_interval = -2;
    if (SDL_GL_GetSwapInterval(&swap_interval))
        SDL_Log("OpenGL swap interval: %d (requested %d)", swap_interval,
                (benchmark || no_vsync) ? 0 : 1);
    else
        SDL_Log("Could not read swap interval: %s", SDL_GetError());
    Game *g = calloc(1, sizeof *g);
    Renderer *r = calloc(1, sizeof *r);
    UI *u = calloc(1, sizeof *u);
    Audio audio = {0};
    if (!g || !r || !u || !game_load(g) || !renderer_init(r) || !ui_init(u)) {
        show_error("The game assets could not be loaded. Keep the assets folder beside ToyCars.");
        free(g);
        free(r);
        free(u);
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    u->show_stats = show_fps;
    if (track >= 0)
        g->selected = track;
    if (touch_override) {
        g->profile.touch = true;
        u->mobile = true;
    }
    g->force_night = night;
    g->force_train = train_at;
    g->test_mode = autoplay || frames > 0 || screen != NULL;
    if (preview_car >= 0)
        g->garage_car = (CarModel)preview_car;
    if (screen) {
        if (!strcmp(screen, "race")) {
            game_start(g);
            if (preview_car >= 0) {
                // The previewed car need not be owned, so the profile keeps its own.
                g->cars[0].model = (CarModel)preview_car;
                g->cars[0].tune = car_tune(g->cars[0].model, NULL);
            }
        } else if (!strcmp(screen, "garage"))
            g->screen = SCREEN_GARAGE;
        else if (!strcmp(screen, "help"))
            g->screen = SCREEN_HELP;
        else if (!strcmp(screen, "settings"))
            g->screen = SCREEN_SETTINGS;
        else if (!strcmp(screen, "records"))
            game_open_records(g);
        else if (!strcmp(screen, "results") || !strcmp(screen, "championship")) {
            bool celebration = !strcmp(screen, "championship");
            if (celebration)
                g->selected = TRACK_COUNT - 1;
            g->test_mode = true;
            game_start(g);
            for (int i = 0; i < 120 * 200 && g->screen != SCREEN_RESULTS; i++)
                game_tick(g, (Controls){0}, FIXED_DT, true);
            // Preview only: leave the loaded profile and saved progression untouched.
            // Show what the finish would pay without banking it.
            if (g->cars[0].finished)
                g->prize = race_prize(g->selected, g->profile.difficulty, g->mode, g->finish_rank);
            if (celebration) {
                g->championship_celebration = true;
                g->celebration_time = 0;
                game_sound(g, SOUND_CELEBRATION, 0, 1, g->cars[0].pos);
            }
        }
    } else if (autoplay)
        game_start(g);
    if (!noaudio)
        audio_init(&audio, true);
    SDL_Gamepad *pad = open_gamepad();
    SDL_Cursor *pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER),
               *normal = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    Lifecycle life = {0};
    SDL_AddEventWatch(lifecycle_watch, &life);
    bool running = true, background = false, graphics_reset_pending = false;
    int exit_code = 0;
    double accumulator = 0;
    Uint64 previous = SDL_GetTicksNS();
    int frame = 0;
    double elapsed = 0;
    double wall_elapsed = 0, swap_elapsed = 0;
    float fps_acc = 0;
    int fps_frames = 0;
    SDL_Log("ToyCars | %s | %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    while (running) {
        Uint64 now = SDL_GetTicksNS();
        float dt = (float)((now - previous) / 1e9);
        float frame_dt = dt;
        wall_elapsed += frame_dt;
        previous = now;
        dt = clampf(dt, 0, .1f);
        if (benchmark)
            dt = 1 / 60.f;
        int w, h, ww, wh;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        SDL_GetWindowSize(window, &ww, &wh);
        ui_size(u, w, h, window);
        r->ui_width = u->width;
        r->ui_height = u->height;
        bool lifecycle_changed = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Pumping events can itself deliver lifecycle callbacks. Consume
            // them before input; old queued input must not resume the race.
            lifecycle_changed |= apply_lifecycle(&life, g, u, &audio, &background);
            if (!lifecycle_changed && !background && !graphics_reset_pending)
                ui_event(u, &e, ww, wh);
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (g->screen == SCREEN_RACE || g->screen == SCREEN_COUNTDOWN)
                    g->screen = SCREEN_PAUSE;
                ui_clear_input(u);
                break;
            case SDL_EVENT_RENDER_DEVICE_RESET:
                graphics_reset_pending = true;
                if (g->screen == SCREEN_RACE || g->screen == SCREEN_COUNTDOWN)
                    g->screen = SCREEN_PAUSE;
                ui_clear_input(u);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!pad)
                    pad = SDL_OpenGamepad(e.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
                    SDL_CloseGamepad(pad);
                    pad = NULL;
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (ui_input_blocked(u) || background || graphics_reset_pending ||
                    lifecycle_changed)
                    break;
                if (ui_record_event(u, g, &e))
                    break;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START)
                    toggle_pause(g, u);
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH &&
                    (g->screen == SCREEN_MENU || g->screen == SCREEN_RESULTS)) {
                    game_continue(g);
                    ui_clear_input(u);
                }
                if (g->screen == SCREEN_GARAGE) {
                    Uint8 b = e.gbutton.button;
                    garage_input(g, u,
                                 b == SDL_GAMEPAD_BUTTON_DPAD_LEFT    ? -1
                                 : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 1 : 0,
                                 b == SDL_GAMEPAD_BUTTON_DPAD_UP      ? -1
                                 : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN  ? 1 : 0,
                                 -1, b == SDL_GAMEPAD_BUTTON_SOUTH);
                }
                // The D-pad steps through the circuits, the shoulders through the cups.
                if (g->screen == SCREEN_MENU) {
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                        g->selected = (g->selected + TRACK_COUNT - 1) % TRACK_COUNT;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                        g->selected = (g->selected + 1) % TRACK_COUNT;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
                        game_select_cup(g, (game_cup(g) + CUP_COUNT - 1) % CUP_COUNT);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
                        game_select_cup(g, (game_cup(g) + 1) % CUP_COUNT);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST)
                        game_set_mode(g, g->mode == MODE_CHAMPIONSHIP ? MODE_ARCADE : MODE_CHAMPIONSHIP);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if ((ui_input_blocked(u) || background || graphics_reset_pending ||
                     lifecycle_changed) &&
                    e.key.key != SDLK_F11)
                    break;
                if (e.key.repeat)
                    break;
                if (ui_record_event(u, g, &e))
                    break;
                if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK)
                    toggle_pause(g, u);
                if (g->screen == SCREEN_GARAGE) {
                    SDL_Keycode k = e.key.key;
                    int browse = k == SDLK_LEFT ? -1 : k == SDLK_RIGHT ? 1 : 0;
                    int focus = k == SDLK_UP ? -1 : k == SDLK_DOWN ? 1 : 0;
                    int buy = k >= SDLK_1 && k < SDLK_1 + UPGRADE_COUNT ? (int)(k - SDLK_1) : -1;
                    if (browse || focus || buy >= 0 || k == SDLK_RETURN)
                        garage_input(g, u, browse, focus, buy, k == SDLK_RETURN);
                } else if (e.key.key == SDLK_RETURN) {
                    if (g->screen == SCREEN_MENU || g->screen == SCREEN_RESULTS)
                        game_continue(g);
                    else if (g->screen == SCREEN_PAUSE)
                        g->screen = g->countdown > 0 ? SCREEN_COUNTDOWN : SCREEN_RACE;
                    ui_clear_input(u);
                }
                if (e.key.key == SDLK_R && g->screen == SCREEN_RACE)
                    game_reset_car(g, 0);
                // 1-9 and 0 pick a circuit; the arrows step through all ten, C the cups.
                if (g->screen == SCREEN_MENU) {
                    int pick = e.key.key >= SDLK_1 && e.key.key <= SDLK_9 ? (int)(e.key.key - SDLK_1)
                               : e.key.key == SDLK_0                     ? 9
                                                                         : -1;
                    if (pick >= 0 && pick < TRACK_COUNT)
                        g->selected = pick;
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT)
                        g->selected = (g->selected + (e.key.key == SDLK_LEFT ? TRACK_COUNT - 1 : 1)) %
                                      TRACK_COUNT;
                    if (e.key.key == SDLK_C)
                        game_select_cup(g, (game_cup(g) + 1) % CUP_COUNT);
                }
                if (e.key.key == SDLK_TAB && g->screen == SCREEN_MENU)
                    game_set_mode(g,
                                  g->mode == MODE_CHAMPIONSHIP ? MODE_ARCADE : MODE_CHAMPIONSHIP);
                if (e.key.key == SDLK_N && g->screen == SCREEN_MENU && g->mode == MODE_ARCADE) {
                    g->profile.arcade_night = !g->profile.arcade_night;
                    game_save_profile(g);
                    ui_clear_input(u);
                }
                if (e.key.key == SDLK_F1)
                    u->show_stats = !u->show_stats;
                if (e.key.key == SDLK_F2)
                    renderer_screenshot(r, "toycars-capture.bmp");
                if (e.key.key == SDLK_M) {
                    g->profile.sound = !g->profile.sound;
                    g->profile.music = g->profile.sound;
                    game_save_profile(g);
                    ui_clear_input(u);
                }
                if (e.key.key == SDLK_F11)
                    SDL_SetWindowFullscreen(window,
                                            !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                break;
            default:
                break;
            }
        }
        lifecycle_changed |= apply_lifecycle(&life, g, u, &audio, &background);
        if (lifecycle_changed) {
            accumulator = 0;
            dt = 0;
        }
        if (!running)
            break;
        // Keep pumping events even when a background surface has no size.
        if (background || w <= 0 || h <= 0) {
            SDL_Delay(background ? 40 : 20);
            continue;
        }
        if (graphics_reset_pending) {
            // SDL's Android backend has already made its new context current.
            context = SDL_GL_GetCurrentContext();
            if (!restore_graphics(r, u)) {
                show_error("The graphics could not be restored. Please restart ToyCars.");
                exit_code = 1;
                break;
            }
            ui_size(u, w, h, window);
            r->ui_width = u->width;
            r->ui_height = u->height;
            accumulator = 0;
            dt = 0;
            previous = SDL_GetTicksNS();
            graphics_reset_pending = false;
            SDL_Log("Graphics restored");
        }
        if (ui_input_blocked(u) && (g->screen == SCREEN_RACE || g->screen == SCREEN_COUNTDOWN))
            g->screen = SCREEN_PAUSE;
        Controls controls = ui_controls(u, g);
        if (pad && g->screen == SCREEN_RACE) {
            float steer = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f;
            if (fabsf(steer) > .15f)
                controls.steer = (fabsf(steer) - .15f) / .85f * (steer < 0 ? -1 : 1);
            float gas = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.f,
                  brake = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.f;
            controls.throttle = fmaxf(controls.throttle, gas);
            controls.brake = fmaxf(controls.brake, brake);
            controls.drift |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
            if (controls.brake > .15f) {
                controls.throttle = 0;
                controls.reverse = true;
            }
        }
        Screen simulation_screen = g->screen;
        accumulator += dt;
        int steps = 0;
        while (accumulator >= FIXED_DT && steps++ < 12) {
            game_tick(g, controls, FIXED_DT, autoplay);
            accumulator -= FIXED_DT;
        }
        if (steps >= 12)
            accumulator = fmin(accumulator, FIXED_DT);
        if (g->screen != simulation_screen && g->screen == SCREEN_RESULTS)
            ui_clear_input(u);
        r->default_fbo = (GLuint)SDL_GetNumberProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0);
        renderer_resize(r, w, h);
        renderer_draw(r, g, dt, (float)(accumulator / FIXED_DT));
        ui_draw(u, g, r);
        Listener listener = {r->eye, r->target, r->helicopter_pos, r->helicopter_ready};
        audio_update(&audio, g, &listener, (float)dt);
        SDL_SetCursor(u->hovered ? pointer : normal);
        frame++;
        elapsed += dt;
        fps_acc += frame_dt;
        fps_frames++;
        if (fps_acc > .5f) {
            u->fps = fps_frames / fps_acc;
            fps_acc = 0;
            fps_frames = 0;
        }
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            SDL_Log("OpenGL error 0x%x at frame %d", error, frame);
            running = false;
            exit_code = 1;
        }
        if (frames > 0 && frame >= frames) {
            if (screenshot) {
                bool ok = renderer_screenshot(r, screenshot);
                SDL_Log("Screenshot %s: %s", screenshot, ok ? "saved" : "failed");
                if (!ok)
                    exit_code = 1;
            }
            running = false;
        }
#ifdef SDL_PLATFORM_IOS
        glBindRenderbuffer(
            GL_RENDERBUFFER,
            (GLuint)SDL_GetNumberProperty(SDL_GetWindowProperties(window),
                                          SDL_PROP_WINDOW_UIKIT_OPENGL_RENDERBUFFER_NUMBER, 0));
#endif
        Uint64 swap_start = SDL_GetTicksNS();
        if (!SDL_GL_SwapWindow(window)) {
            SDL_Log("Present failed: %s", SDL_GetError());
            exit_code = 1;
            running = false;
        }
        swap_elapsed += (SDL_GetTicksNS() - swap_start) / 1e9;
    }
    SDL_Log("Session: %d frames, %.2f game seconds, screen=%d, race=%.2fs, fuel=%.1f, position=%d",
            frame, elapsed, g->screen, g->time, g->cars[0].fuel, g->rank);
    if (frame > 0 && wall_elapsed > 0)
        SDL_Log("Timing: %.1f FPS, %.2f ms/frame, %.2f ms/frame in buffer swap",
                frame / wall_elapsed, wall_elapsed * 1000 / frame, swap_elapsed * 1000 / frame);
    SDL_RemoveEventWatch(lifecycle_watch, &life);
    if (pad)
        SDL_CloseGamepad(pad);
    SDL_DestroyCursor(pointer);
    SDL_DestroyCursor(normal);
    audio_destroy(&audio);
    ui_destroy(u);
    renderer_destroy(r);
    free(u);
    free(r);
    game_free(g);
    free(g);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
