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
static bool initials_sent, records_back_sent, initials_sound, championship_key_sent,
    championship_pad_sent;
static UI *current_ui;
static float reset_time, portrait_time;
static Car reset_car;
static Profile portrait_profile;
static bool lifecycle_resume_sent, lifecycle_race_sent, race_resume_sent, mute_sent,
    mute_retry_sent;
static float lifecycle_countdown, lifecycle_time;
static Car lifecycle_car;
static bool mute_sound;

static void push_lifecycle_pair(void) {
    SDL_Event event = {.type = SDL_EVENT_WILL_ENTER_BACKGROUND};
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
    SDL_PushEvent(&event);
}

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
    if (drawn_frames == 14 && !initials_sent) {
        initials_sent = true;
        push_key(SDLK_M);
        push_key(SDLK_A);
        push_key(SDLK_X);
        push_key(SDLK_RETURN);
    }
    if (drawn_frames == 15 && !records_back_sent) {
        records_back_sent = true;
        push_key(SDLK_ESCAPE);
    }
    if (drawn_frames == 16 && !championship_key_sent) {
        championship_key_sent = true;
        push_key(SDLK_RETURN);
    }
    if (drawn_frames == 17 && !championship_pad_sent) {
        championship_pad_sent = true;
        SDL_Event pad = {0};
        pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        pad.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
        SDL_PushEvent(&pad);
    }
    if (drawn_frames == 19 && !lifecycle_resume_sent) {
        lifecycle_resume_sent = true;
        push_key(SDLK_RETURN);
    }
    if (drawn_frames == 20 && !lifecycle_race_sent) {
        lifecycle_race_sent = true;
        // Delivered from inside PollEvent, after the frame has begun.
        push_lifecycle_pair();
        push_key(SDLK_RETURN);
        SDL_Event pad = {.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN};
        pad.gbutton.button = SDL_GAMEPAD_BUTTON_START;
        SDL_PushEvent(&pad);
        push_tap(current_ui->width * .5f, current_ui->height * .5f - 15);
    }
    if (drawn_frames == 21 && !race_resume_sent) {
        race_resume_sent = true;
        push_key(SDLK_RETURN);
    }
    if (drawn_frames == 22 && !mute_sent) {
        mute_sent = true;
        push_key(SDLK_M);
    }
    if (drawn_frames == 23 && !mute_retry_sent) {
        mute_retry_sent = true;
        push_key(SDLK_RETURN);
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

static void test_crowd_animation(Renderer *r, const Game *g, bool wildlife) {
    Renderer saved = *r;
    Game *scene = SDL_malloc(sizeof *scene);
    size_t bytes = (size_t)r->width * r->height * 4;
    unsigned char *first = SDL_malloc(bytes), *next = SDL_malloc(bytes);
    size_t shadow_bytes = 2048 * 2048 * sizeof(float);
    float *shadow_first = SDL_malloc(shadow_bytes), *shadow_next = SDL_malloc(shadow_bytes);
    EXPECT(scene && first && next && shadow_first && shadow_next, "animation readback allocation");
    if (!scene || !first || !next || !shadow_first || !shadow_next)
        goto done;
    memcpy(scene, g, sizeof *scene);
    for (int track = 0; track < TRACK_COUNT; track++) {
        scene->selected = track;
        if (wildlife) {
            r->crowds[track].count = 0;
            r->water[track].count = 0;
        } else r->wildlife[track].count = 0;
        game_start(scene);
        scene->screen = SCREEN_RACE;
        if (wildlife) {
            const char *names[] = {"country", "beach", "winter"};
            char file[96]; size_t size = 0;
            SDL_snprintf(file, sizeof file, "tracks/%s-wildlife.tcf", names[track]);
            unsigned char *data = asset_read(file, &size);
            EXPECT(data && size >= 80, "wildlife fixture loads");
            if (data) {
                for (size_t offset = 8; offset + 72 <= size; offset += 72) {
                    float row[18]; memcpy(row, data + offset, sizeof row);
                    for (int j = 0; j < 18; j++) row[j] = SDL_SwapFloatLE(row[j]);
                    if (row[14] != (track == 1 ? 3 : 0)) continue;
                    scene->cars[0].pos = scene->cars[0].prev = v3(row[10], row[11], row[12]-9);
                    scene->cars[0].yaw = scene->cars[0].speed = 0;
                    break;
                }
                SDL_free(data);
            }
        }
        r->camera_ready = false;
        r->crowd_time = 0;
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
        glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
        glReadPixels(0, 0, 2048, 2048, GL_DEPTH_COMPONENT, GL_FLOAT, shadow_first);
        r->crowd_time = .43f;
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
        glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
        glReadPixels(0, 0, 2048, 2048, GL_DEPTH_COMPONENT, GL_FLOAT, shadow_next);
        int changed = 0, shadows = 0;
        for (size_t i = 0; i < bytes; i += 4)
            changed += memcmp(first + i, next + i, 3) != 0;
        for (size_t i = 0; i < shadow_bytes / sizeof(float); i++)
            shadows += shadow_first[i] != shadow_next[i];
        EXPECT(changed > (wildlife ? 10 : 100), "isolated animal/crowd animation changes visible pixels");
        EXPECT(shadows > (wildlife ? 5 : 20), "isolated animal/crowd shadows follow animated geometry");
        scene->screen = SCREEN_PAUSE;
        renderer_draw(r, scene, .08f, 1);
        EXPECT(r->crowd_time == .43f, "pause freezes crowd animation");
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
        EXPECT(memcmp(first, next, bytes) == 0, "paused crowd remains visually unchanged");
        scene->screen = SCREEN_RACE;
        renderer_draw(r, scene, .016f, 1);
        EXPECT(r->crowd_time > .43f, "crowd animation resumes");
        if (track == 0 || wildlife) {
            // Captures show the complete habitat after isolated animation checks.
            if (wildlife) {
                r->crowds[track] = saved.crowds[track];
                r->water[track] = saved.water[track];
            }
            // Fixed camera and cars make these captures useful for motion review.
            for (int frame = 0; frame < 12; frame++) {
                char path[2200];
                r->crowd_time = frame * .1f;
                renderer_draw(r, scene, 0, 1);
                if (wildlife)
                    SDL_snprintf(path, sizeof path, "%swildlife-%d-%02d.bmp", pref_directory, track, frame);
                else
                    SDL_snprintf(path, sizeof path, "%scrowd-%02d.bmp", pref_directory, frame);
                EXPECT(renderer_screenshot(r, path), "crowd motion frame saved");
            }
        }
        printf("Track %d animation: %d changed pixels, %d changed shadow samples\n", track, changed,
               shadows);
    }
done:
    SDL_free(scene);
    SDL_free(first);
    SDL_free(next);
    SDL_free(shadow_first);
    SDL_free(shadow_next);
    *r = saved;
    r->bound_vao = 0;
    renderer_draw(r, g, 0, r->alpha);
    EXPECT(glGetError() == GL_NO_ERROR, "crowd rendering has no GL errors");
}

static void test_props_rendering(Renderer *r, const Game *g) {
    Renderer saved=*r;
    Game *scene=SDL_malloc(sizeof *scene);
    EXPECT(scene != NULL, "props scene allocation");
    if (!scene) return;
    memcpy(scene,g,sizeof *scene);
    scene->test_mode=true;
    for (int track=0;track<TRACK_COUNT;track++) {
        scene->selected=track;
        game_start(scene);
        EXPECT(scene->prop_count==116 && scene->fence_count==16,
               "each track has cones, tires and four five-stake rally fences");
        Vec3 original=scene->props[0].pos;
        scene->props[0].pos.y+=10;
        game_start(scene);
        EXPECT(vlen(vsub(scene->props[0].pos,original))<.001f, "restart restores props");
        scene->screen=SCREEN_PAUSE;
        game_tick(scene,(Controls){0},FIXED_DT,false);
        EXPECT(vlen(vsub(scene->props[0].pos,original))<.001f, "pause freezes props");
        scene->screen=SCREEN_RACE;
        for (int view=0;view<3;view++) {
            Car *c=&scene->cars[0];
            c->pos=c->prev=view==2 ? scene->props[scene->fences[0].posts[0]].pos :
                view ? scene->props[9].pos : scene->roadwork;
            c->yaw=c->prev_yaw=scene->roadwork_yaw;
            c->pos=vsub(c->pos,v3(sinf(c->yaw)*6,0,cosf(c->yaw)*6));
            c->pos.y=track_height(&scene->tracks[track],c->pos,NULL);
            c->prev=c->pos;
            c->speed=0;
            r->camera_ready=false;
            renderer_draw(r,scene,0,1);
            char path[2200];
            SDL_snprintf(path,sizeof path,"%sprops-%d-%d.bmp",pref_directory,track,view);
            EXPECT(renderer_screenshot(r,path), "props review capture saved");
            EXPECT(glGetError()==GL_NO_ERROR, "props color and shadow passes have no GL errors");
        }
    }
    SDL_free(scene);
    *r=saved;
    r->bound_vao=0;
    renderer_draw(r,g,0,r->alpha);
}

static void test_water_rendering(Renderer *r, const Game *g) {
    Renderer saved = *r;
    Game *scene = SDL_malloc(sizeof *scene);
    size_t bytes = (size_t)r->width * r->height * 4;
    unsigned char *first = SDL_malloc(bytes), *next = SDL_malloc(bytes);
    EXPECT(scene && first && next, "water readback allocation");
    if (!scene || !first || !next) goto done;
    memcpy(scene, g, sizeof *scene);
    for (int track = 0; track < TRACK_COUNT; track++) {
        scene->selected = track;
        game_start(scene);
        scene->screen = SCREEN_RACE;
        const Track *t = &scene->tracks[track];
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
        if (ford < 0) {
            // A bridge can carry the circuit over its former ford. Preview
            // the closest waterside road section in that environment instead.
            float best = 1e20f;
            for (int i = 0; i < t->count; i++)
                for (int j = 0; j < t->water_count; j++) {
                    Vec3 q = vsub(t->points[i].p, t->water[j].a);
                    float distance = q.x*q.x + q.z*q.z;
                    if (distance < best) { best = distance; ford = i; }
                }
        }
        EXPECT(ford >= 0, "water preview locates a ford or bridge crossing");
        if (ford < 0) continue;
        for (int i = 0; i < CAR_COUNT; i++) {
            Car *c = &scene->cars[i];
            Vec3 dir;
            c->pos = track_at(t, t->points[ford].s - .8f - i * 6, i % 2 ? -1.7f : 1.7f, &dir);
            c->yaw = c->prev_yaw = atan2f(dir.x, dir.z);
            car_surface_pose(t, &c->pos, c->yaw, &c->pitch, &c->roll);
            c->prev = c->pos;
            c->nearest = ford;
            c->progress = c->last_s = t->points[ford].s - .8f - i * 6;
            c->velocity = vmul(dir, 16); c->speed = 16;
        }
        for (int step = 0; step < 12; step++) game_tick(scene, (Controls){0}, FIXED_DT, true);
        r->camera_ready = false;
        r->crowd_time = 1;
        // Isolate the liquid animation from the independently animated fans.
        int wildlife_count = r->wildlife[track].count;
        r->wildlife[track].count = 0;
        int crowd_count = r->crowds[track].count;
        r->crowds[track].count = 0;
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
        r->crowd_time = 1.37f;
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
        int changed = 0;
        for (size_t i = 0; i < bytes; i += 4) changed += memcmp(first+i,next+i,3) != 0;
        EXPECT(changed > 200, "water has visibly moving reflections and ripples on every track");
        scene->screen = SCREEN_PAUSE;
        renderer_draw(r, scene, .05f, 1);
        EXPECT(r->crowd_time == 1.37f, "pause freezes water animation");
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
        EXPECT(memcmp(first,next,bytes) == 0, "paused water frame is stable");
        r->crowds[track].count = crowd_count;
        r->wildlife[track].count = wildlife_count;
        renderer_draw(r, scene, 0, 1);
        char path[2200];
        SDL_snprintf(path, sizeof path, "%swater-%s.bmp", pref_directory, t->id);
        EXPECT(renderer_screenshot(r,path), "water review capture saved");
        EXPECT(glGetError() == GL_NO_ERROR, "water refraction and spray render without GL errors");
        printf("Track %d water: %d animated pixels\n", track, changed);
        if (track == 0) {
            // Show the whole catchment: uphill source, ford and a separate
            // downstream pond. This catches the old paired-puddle composition.
            r->cam_pos = v3(-59, 5, -20);
            r->camera_yaw = 0;
            r->camera_ready = true;
            scene->cars[0].speed = 90;
            renderer_draw(r, scene, 0, 1);
            SDL_snprintf(path, sizeof path, "%swater-country-catchment.bmp", pref_directory);
            EXPECT(renderer_screenshot(r,path), "whole watershed review capture saved");
            glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
            glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
            r->crowd_time += .6f;
            renderer_draw(r, scene, 0, 1);
            glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
            glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
            // Sample the pond itself: moving highlights must not turn most of
            // its surface into the old opaque, white corrugated-sheet pattern.
            int samples = 0, white = 0, moving = 0;
            for (float z = -3; z <= 10; z += .5f) {
                for (float x = -104; x <= -80; x += .5f) {
                    Vec3 p = v3(x, 0, z);
                    float y;
                    if (!track_water(t, p, &y) || y - track_height(t, p, NULL) < .4f) continue;
                    const float *m = r->vp.m;
                    float w = m[3]*x + m[7]*y + m[11]*z + m[15];
                    if (w <= 0) continue;
                    int sx = (int)(((m[0]*x+m[4]*y+m[8]*z+m[12])/w*.5f+.5f)*r->width);
                    int sy = (int)(((m[1]*x+m[5]*y+m[9]*z+m[13])/w*.5f+.5f)*r->height);
                    if (sx < 0 || sx >= r->width || sy < 0 || sy >= r->height) continue;
                    size_t pixel = ((size_t)sy*r->width+sx)*4;
                    white += first[pixel] > 200 && first[pixel+1] > 200 && first[pixel+2] > 180;
                    moving += memcmp(first+pixel, next+pixel, 3) != 0;
                    samples++;
                }
            }
            EXPECT(samples > 100, "pond material samples are visible");
            EXPECT(white < samples / 20, "pond retains depth color without broad white sheen");
            EXPECT(moving > samples / 50, "calm pond still has visible animated wavelets");
            printf("Pond material: %d samples, %d white, %d moving\n", samples, white, moving);
            r->cam_pos = v3(-92, -2, -2);
            r->camera_yaw = -.93f;
            scene->cars[0].speed = 0;
            renderer_draw(r, scene, 0, 1);
            SDL_snprintf(path, sizeof path, "%swater-country-pond.bmp", pref_directory);
            EXPECT(renderer_screenshot(r,path), "pond material review capture saved");
        } else {
            // Review the entire, distinct watercourse as well as its ford.
            r->cam_pos = track == 1 ? v3(33, 2, 65) : v3(36, 7, -61);
            r->camera_yaw = track == 1 ? 0 : PI;
            r->camera_ready = true;
            scene->cars[0].speed = 70;
            renderer_draw(r, scene, 0, 1);
            SDL_snprintf(path, sizeof path, "%swater-%s-catchment.bmp", pref_directory, t->id);
            EXPECT(renderer_screenshot(r,path), "distinct watercourse review capture saved");
        }
    }
done:
    SDL_free(scene); SDL_free(first); SDL_free(next);
    *r = saved; r->bound_vao = 0;
    renderer_draw(r, g, 0, r->alpha);
}

static void test_vegetation_rendering(Renderer *r, const Game *g) {
    Renderer saved = *r;
    Game *scene = SDL_malloc(sizeof *scene);
    size_t bytes = (size_t)r->width * r->height * 4;
    unsigned char *first = SDL_malloc(bytes), *next = SDL_malloc(bytes);
    EXPECT(scene && first && next, "vegetation readback allocation");
    if (!scene || !first || !next) goto done;
    memcpy(scene, g, sizeof *scene);
    for (int track = 0; track < TRACK_COUNT; track++) {
        scene->selected = track;
        game_start(scene);
        if (track == 2) {
            // The alpine grid now faces the island edge. Review wind from the
            // wooded switchbacks instead, where vegetation fills the camera.
            Vec3 direction;
            Car *car = &scene->cars[0];
            car->pos = track_at(&scene->tracks[track], scene->tracks[track].length * .35f, 0, &direction);
            car->yaw = car->prev_yaw = atan2f(direction.x, direction.z);
            car_surface_pose(&scene->tracks[track], &car->pos, car->yaw, &car->pitch, &car->roll);
            car->prev = car->pos;
        }
        scene->screen = SCREEN_PAUSE;
        // No weather particles; only vegetation uses this clock in these frames.
        scene->weather = (Weather){.kind = WEATHER_CLEAR, .time = 17};
        r->camera_ready = false;
        for (int windy = 0; windy < 2; windy++) {
            scene->weather.wind = windy ? v3(10, 0, 4) : v3(0, 0, 0);
            renderer_draw(r, scene, 0, 1);
            glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
            glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
            renderer_draw(r, scene, 0, 1);
            glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
            glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
            EXPECT(memcmp(first, next, bytes) == 0, "paused vegetation is stable even in wind");
            scene->weather.time += .65f;
            renderer_draw(r, scene, 0, 1);
            glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
            glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
            int changed = 0;
            for (size_t pixel = 0; pixel < bytes; pixel += 4) {
                int difference = 0;
                for (int channel = 0; channel < 3; channel++)
                    difference += abs((int)first[pixel + channel] - next[pixel + channel]);
                changed += difference > 24;
            }
            EXPECT(changed > r->width * r->height / 1000,
                   "tree motion is visible in both the everyday breeze and weather gusts");
            char frame_path[2200];
            SDL_snprintf(frame_path, sizeof frame_path, "%svegetation-%d-%s.bmp",
                         pref_directory, track, windy ? "gust" : "breeze");
            glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
            EXPECT(renderer_screenshot(r, frame_path), "breeze and gust review capture saved");
            printf("Vegetation track %d, wind %d: %d moving pixels\n", track, windy, changed);
        }
        char path[2200];
        SDL_snprintf(path, sizeof path, "%svegetation-%d.bmp", pref_directory, track);
        glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
        EXPECT(renderer_screenshot(r, path), "vegetation review screenshot saved");
        EXPECT(glGetError() == GL_NO_ERROR, "vegetation rendering has no GL errors");
    }
done:
    SDL_free(scene); SDL_free(first); SDL_free(next);
    *r = saved; r->bound_vao = 0;
    renderer_draw(r, g, 0, r->alpha);
}

static void test_boost_rendering(Renderer *r, const Game *g) {
    Game *scene = SDL_malloc(sizeof *scene);
    EXPECT(scene != NULL, "boost scene allocation");
    if (!scene) return;
    memcpy(scene, g, sizeof *scene);
    for (int track = 0; track < TRACK_COUNT; track++) {
        scene->selected = track;
        game_start(scene);
        scene->screen = SCREEN_RACE;
        scene->weather = (Weather){.kind = WEATHER_CLEAR};
        Car *c = &scene->cars[0];
        c->progress = scene->tracks[track].boosts[0] - 9;
        game_reset_car(scene, 0);
        c->reset_protection = 0;
        r->camera_ready = false;
        renderer_draw(r, scene, 0, 1);
        EXPECT(r->boosts[track].count == MAX_BOOSTS * 12 * 16 * 6,
               "all boost pads are batched in one mesh");
        EXPECT(glGetError() == GL_NO_ERROR, "boost rendering has no GL errors");
        char path[2200];
        SDL_snprintf(path, sizeof path, "%sboost-%d.bmp", pref_directory, track);
        EXPECT(renderer_screenshot(r, path), "boost review capture saved");
        scene->weather.time += .65f;
        renderer_draw(r, scene, 0, 1);
        SDL_snprintf(path, sizeof path, "%sboost-animated-%d.bmp", pref_directory, track);
        EXPECT(renderer_screenshot(r, path), "animated boost review capture saved");
        scene->screen = SCREEN_PAUSE;
        renderer_draw(r, scene, .1f, 1);
        float paused_time = 0;
        glGetUniformfv(r->shader, glGetUniformLocation(r->shader, "uBoostTime"), &paused_time);
        EXPECT(fabsf(paused_time - scene->weather.time) < .001f,
               "pad animation uses the frozen simulation clock during pause");
        scene->screen = SCREEN_RACE;
        c->speed = 40;
        c->boost_cooldown = 2;
        renderer_draw(r, scene, 0, 1);
        size_t bytes = (size_t)r->width * r->height * 4;
        unsigned char *sharp = SDL_malloc(bytes), *blurred = SDL_malloc(bytes);
        EXPECT(sharp && blurred, "speed blur readback allocation");
        if (sharp && blurred) {
            glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, blurred);
            SDL_snprintf(path, sizeof path, "%sspeed-blur-%d.bmp", pref_directory, track);
            EXPECT(renderer_screenshot(r, path), "speed blur review capture saved");
            // Replay the same post pass with blur disabled, keeping camera and scene identical.
            glUniform1f(glGetUniformLocation(r->post_shader, "uSpeedBlur"), 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, sharp);
            int side_changes = 0, center_changes = 0;
            for (int y = 0; y < r->height; y++) for (int x = 0; x < r->width; x++) {
                size_t pixel = ((size_t)y * r->width + x) * 4;
                bool changed = memcmp(sharp + pixel, blurred + pixel, 3) != 0;
                if (x > r->width * .3f && x < r->width * .7f) center_changes += changed;
                if (x < r->width * .2f || x > r->width * .8f) side_changes += changed;
            }
            EXPECT(center_changes == 0, "speed blur keeps the central road pixel-exact");
            EXPECT(side_changes > r->width * r->height / 100,
                   "high speed visibly blurs the sides of the image");
        }
        SDL_free(sharp); SDL_free(blurred);
        c->speed = 0;
        renderer_draw(r, scene, 0, 1);
        float strength = -1;
        glGetUniformfv(r->post_shader, glGetUniformLocation(r->post_shader, "uSpeedBlur"), &strength);
        EXPECT(strength == 0, "stationary cars have no speed blur");
        c->speed = 40;
        scene->screen = SCREEN_PAUSE;
        renderer_draw(r, scene, 0, 1);
        glGetUniformfv(r->post_shader, glGetUniformLocation(r->post_shader, "uSpeedBlur"), &strength);
        EXPECT(strength == 0, "pause disables speed blur");
        EXPECT(glGetError() == GL_NO_ERROR, "animated pads and speed blur have no GL errors");
    }
    SDL_free(scene);
    r->camera_ready = false;
    renderer_draw(r, g, 0, 1);
}

// In clear weather, integrated drift changes only the cloud projection. Keep
// both animation clocks and the camera fixed to isolate lighting from motion.
static void test_cloud_lighting(Renderer *r, const Game *g) {
    Renderer saved = *r;
    Game *scene = SDL_malloc(sizeof *scene);
    size_t bytes = (size_t)r->width * r->height * 4;
    unsigned char *first = SDL_malloc(bytes), *next = SDL_malloc(bytes);
    EXPECT(scene && first && next, "cloud readback allocation");
    if (!scene || !first || !next) goto done;
    memcpy(scene, g, sizeof *scene);
    for (int track = 0; track < TRACK_COUNT; track++) {
        scene->selected = track;
        game_start(scene);
        scene->screen = SCREEN_PAUSE;
        scene->weather = (Weather){.kind = WEATHER_CLEAR};
        r->camera_ready = false;
        renderer_draw(r, scene, 0, 1);
        int calls = r->draw_calls;
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
        renderer_draw(r, scene, .05f, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
        EXPECT(memcmp(first, next, bytes) == 0, "cloud lighting freezes during pause");
        scene->weather.drift = v3(230, 0, 110);
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
        int changed = 0;
        for (size_t pixel = 0; pixel < bytes; pixel += 4) {
            int difference = 0;
            for (int channel = 0; channel < 3; channel++)
                difference += abs((int)first[pixel + channel] - next[pixel + channel]);
            changed += difference > 12;
        }
        EXPECT(changed > r->width * r->height / 50, "clouds visibly relight the track");
        EXPECT(r->draw_calls == calls, "cloud lighting adds no draw calls");
        EXPECT(glGetError() == GL_NO_ERROR, "cloud lighting has no GL errors");
        char path[2200];
        SDL_snprintf(path, sizeof path, "%scloud-lighting-%d.bmp", pref_directory, track);
        glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
        EXPECT(renderer_screenshot(r, path), "cloud lighting review capture saved");
    }
done:
    SDL_free(scene); SDL_free(first); SDL_free(next);
    *r = saved; r->bound_vao = 0;
    renderer_draw(r, g, 0, r->alpha);
}

static void test_weather_rendering(Renderer *r, const Game *g) {
    Renderer saved = *r;
    Game *scene = SDL_malloc(sizeof *scene);
    size_t bytes = (size_t)r->width * r->height * 4;
    unsigned char *first = SDL_malloc(bytes), *next = SDL_malloc(bytes);
    EXPECT(scene && first && next, "weather readback allocation");
    if (!scene || !first || !next)
        goto done;
    memcpy(scene, g, sizeof *scene);
    const WeatherKind kinds[] = {WEATHER_RAIN, WEATHER_WIND, WEATHER_SNOW, WEATHER_WIND,
                                 WEATHER_WIND};
    const int tracks[] = {0, 1, 2, 0, 2};
    for (int i = 0; i < 5; i++) {
        scene->selected = tracks[i];
        game_start(scene);
        scene->screen = SCREEN_PAUSE;
        scene->weather = (Weather){
            .kind = kinds[i], .time = 17, .intensity = .9f, .wind = {5, 0, 2}, .drift = {20, 0, 8}};
        r->camera_ready = false;
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, first);
        int weather_calls = r->draw_calls;
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
        EXPECT(memcmp(first, next, bytes) == 0, "paused weather is visually stable");
        scene->weather.time += .35f;
        scene->weather.drift = vadd(scene->weather.drift, vmul(scene->weather.wind, .35f));
        renderer_draw(r, scene, 0, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, next);
        int changed = 0;
        for (size_t pixel = 0; pixel < bytes; pixel += 4)
            changed += memcmp(first + pixel, next + pixel, 3) != 0;
        EXPECT(changed > 100, "rain, snow and wind particles visibly move");
        char path[2200];
        SDL_snprintf(path, sizeof path, "%sweather-%d.bmp", pref_directory, i);
        glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
        EXPECT(renderer_screenshot(r, path), "weather screenshot saved");
        scene->weather.intensity = 0;
        renderer_draw(r, scene, 0, 1);
        EXPECT(r->draw_calls == weather_calls - 1, "weather uses one batch and none when clear");
        EXPECT(glGetError() == GL_NO_ERROR, "weather rendering has no GL errors");
        printf("Weather %d on track %d: %d moving pixels\n", kinds[i], tracks[i], changed);
    }
done:
    SDL_free(scene);
    SDL_free(first);
    SDL_free(next);
    *r = saved;
    r->bound_vao = 0;
    renderer_draw(r, g, 0, r->alpha);
}

static void click_ui(UI *u, Game *g, Renderer *r, float x, float y) {
    u->click_count = 1;
    u->clicks[0] = (UIClick){x, y, x, y, false};
    ui_draw(u, g, r);
}

static void capture_ui(UI *u, Game *g, Renderer *r, const char *name) {
    if (u->mobile) {
        // Ordinary reference captures show settled pages. Transition frames are
        // captured explicitly in the mobile navigation regression below.
        ui_draw(u, g, r);
        g->clock += .25f;
    }
    renderer_draw(r, g, 0, 1);
    ui_draw(u, g, r);
    char path[2200];
    SDL_snprintf(path, sizeof path, "%s%s.bmp", pref_directory, name);
    EXPECT(renderer_screenshot(r, path), "progression UI screenshot saved");
}

static void test_progression_ui(UI *u, Game *g, Renderer *r) {
    g->test_mode = false;
    g->profile.initials_set = false;
    SDL_strlcpy(g->profile.initials, "AAA", sizeof g->profile.initials);
    g->profile.championship_completed = 0;
    g->profile.championship_stages = 0;
    memset(g->profile.championship_places, 0, sizeof g->profile.championship_places);
    g->screen = SCREEN_MENU;
    game_set_mode(g, MODE_CHAMPIONSHIP);
    float m = 46 + u->safe_left, hero = 105 + u->safe_top;
    float card_y = u->height - 116 - 54 - u->safe_bottom;
    float cw = (u->width - m - (46 + u->safe_right) - 32) / 3;
    bool compact = u->height - u->safe_top - u->safe_bottom < 690;
    float start_y = hero + (compact ? 151 + 48 : 205 + 65) + 20;
    click_ui(u, g, r, m + 290, hero + 50);
    EXPECT(g->mode == MODE_ARCADE, "mode button selects Arcade");
    click_ui(u, g, r, m + 2 * (cw + 16) + 40, card_y + 40);
    EXPECT(g->selected == 2, "locked cards allow a preview");
    capture_ui(u, g, r, "arcade-locked");
    click_ui(u, g, r, m + 80, start_y);
    EXPECT(g->screen == SCREEN_MENU, "locked track has no active start button");
    click_ui(u, g, r, m + 300, start_y);
    EXPECT(g->mode == MODE_CHAMPIONSHIP && g->selected == 0,
           "locked preview offers the current Championship stage");
    capture_ui(u, g, r, "championship-menu");
    click_ui(u, g, r, m + 80, start_y);
    EXPECT(g->screen == SCREEN_COUNTDOWN, "current stage starts from its menu button");
    EXPECT(strcmp(game_player_name(g), "YOU") == 0 && strcmp(g->cars[0].name, "YOU") == 0,
           "a new player's race label is YOU before initials are confirmed");
    capture_ui(u, g, r, "player-name-new-race");
    bool saved_touch = g->profile.touch;
    g->profile.touch = true;
    capture_ui(u, g, r, "desktop-touch-enabled");
    g->profile.touch = false;
    capture_ui(u, g, r, "desktop-touch-disabled");
    g->profile.touch = saved_touch;
    for (int stage = 0; stage < TRACK_COUNT; stage++) {
        g->screen = SCREEN_RACE;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 80;
        if (g->mode == MODE_CHAMPIONSHIP)
            for (int i = 1; i < CAR_COUNT; i++)
                if (!g->cars[i].finished)
                    g->cars[i].dnf = true;
        game_results(g);
        EXPECT(g->championship_advanced && g->profile.championship_completed == stage + 1,
               "a podium advances the Championship");
        EXPECT(g->initials_pending == (stage == 0), "only the opening stage asks for initials");
        if (stage == 0) {
            capture_ui(u, g, r, "championship-initials");
            SDL_strlcpy(g->initials, "ROB", sizeof g->initials);
            EXPECT(strcmp(game_player_name(g), "YOU") == 0,
                   "draft initials do not change the player label");
            float cx = u->safe_left + (u->width - u->safe_left - u->safe_right) * .5f;
            float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 510) * .5f;
            click_ui(u, g, r, cx, y + 438);
        }
        EXPECT(g->screen == SCREEN_RESULTS && !g->initials_pending,
               "Championship shows classification after initials without opening records");
        EXPECT(strcmp(game_player_name(g), "ROB") == 0 && strcmp(g->cars[0].name, "ROB") == 0,
               "confirmation immediately changes the player's results name");
        EXPECT(!game_submit_initials(g) && g->screen == SCREEN_RESULTS,
               "submitted Championship initials cannot reopen name entry");
        EXPECT(strcmp(g->profile.records[stage].history[0].initials, "ROB") == 0,
               "all Championship stages use the chosen initials");
        capture_ui(u, g, r, stage == TRACK_COUNT - 1 ? "championship-complete" : "track-unlocked");
        EXPECT(g->championship_celebration == (stage == TRACK_COUNT - 1),
               "only the final qualifying stage shows the celebration");
        if (stage == TRACK_COUNT - 1) {
            g->celebration_time = 1.5f;
            capture_ui(u, g, r, "championship-celebration");
            const SDL_Keycode keys[] = {SDLK_RETURN, SDLK_SPACE, SDLK_ESCAPE};
            for (size_t i = 0; i < SDL_arraysize(keys); i++) {
                g->championship_celebration = true;
                SDL_Event event = {0};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.key = keys[i];
                EXPECT(ui_record_event(u, g, &event) && !g->championship_celebration &&
                           g->screen == SCREEN_RESULTS,
                       "keyboard reveals results without replaying");
            }
            const int buttons[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_START,
                                   SDL_GAMEPAD_BUTTON_EAST};
            for (size_t i = 0; i < SDL_arraysize(buttons); i++) {
                g->championship_celebration = true;
                SDL_Event event = {0};
                event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                event.gbutton.button = buttons[i];
                EXPECT(ui_record_event(u, g, &event) && !g->championship_celebration &&
                           g->screen == SCREEN_RESULTS,
                       "gamepad reveals results without replaying");
            }
            float available = u->height - u->safe_top - u->safe_bottom;
            float panel_h = fminf(680, available - 32);
            float cx = u->safe_left + (u->width - u->safe_left - u->safe_right) * .5f;
            float by = u->safe_top + (available - panel_h) * .5f + panel_h - 60;
            g->championship_celebration = true;
            click_ui(u, g, r, cx - 100, by);
            EXPECT(!g->championship_celebration && g->screen == SCREEN_RESULTS,
                   "celebration results button reveals classification");
            g->championship_celebration = true;
            click_ui(u, g, r, cx + 100, by);
            EXPECT(!g->championship_celebration && g->screen == SCREEN_MENU,
                   "celebration menu button returns to track selection");
            g->screen = SCREEN_RESULTS;
            capture_ui(u, g, r, "championship-final-results");
            float results_h = fminf(638, available - 44);
            float results_x = cx - 460;
            float results_by = u->safe_top + (available - results_h) * .5f + results_h - 50;
            click_ui(u, g, r, results_x + 36 + 247 + 90, results_by);
            EXPECT(g->screen == SCREEN_RESULTS, "final results offer no replay action");
            click_ui(u, g, r, results_x + 36 + 100, results_by);
            EXPECT(g->screen == SCREEN_MENU, "final results primary action returns to the menu");
        }
        if (stage < TRACK_COUNT - 1) {
            float available = u->height - u->safe_top - u->safe_bottom;
            float panel_h = fminf(638, available - 44);
            float x = u->safe_left + (u->width - u->safe_left - u->safe_right - 920) * .5f;
            float y = u->safe_top + (available - panel_h) * .5f + panel_h - 50;
            click_ui(u, g, r, x + 36 + 80, y);
            EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == stage + 1,
                   "Next Stage starts the newly unlocked track");
            if (stage == 0)
                capture_ui(u, g, r, "player-name-confirmed-race");
        }
    }
    // Exercise the actual scoring path for all medal colors and failure, then
    // render both desktop and short-window layouts from those outcomes.
    const char *outcomes[] = {"championship-gold", "championship-silver", "championship-bronze",
                              "championship-lost"};
    for (int rank = 1; rank <= 4; rank++) {
        EXPECT(game_reset_profile(g), "reset overall-result fixture");
        for (int stage = 0; stage < TRACK_COUNT; stage++) {
            game_start(g);
            g->screen = SCREEN_RACE;
            for (int i = 0; i < CAR_COUNT; i++) {
                g->cars[i].finished = true;
                g->cars[i].finish_time = i == 0 ? 80 : i < rank ? 70 + i : 80 + i;
            }
            game_results(g);
            if (g->initials_pending)
                EXPECT(game_submit_initials(g), "save outcome fixture initials");
        }
        EXPECT(game_championship_rank(g) == rank && game_championship_finished(g),
               "final UI uses the total championship rank");
        EXPECT(g->championship_celebration == (rank <= 3), "only overall podiums celebrate");
        g->celebration_time = 2;
        capture_ui(u, g, r, outcomes[rank - 1]);
        ui_size(u, 1600, 450, SDL_GL_GetCurrentWindow());
        renderer_resize(r, 1600, 450);
        char name[80];
        SDL_snprintf(name, sizeof name, "%s-wide", outcomes[rank - 1]);
        capture_ui(u, g, r, name);
        ui_size(u, 1280, 800, SDL_GL_GetCurrentWindow());
        renderer_resize(r, 1280, 800);
    }
    for (int dnf = 0; dnf <= 1; dnf++) {
        EXPECT(game_reset_profile(g), "reset elimination fixture");
        game_start(g);
        g->screen = SCREEN_RACE;
        for (int i = 0; i < CAR_COUNT; i++) {
            g->cars[i].finished = true;
            g->cars[i].finish_time = i == 0 ? 80 : i < 5 ? 70 + i : 80 + i;
        }
        g->cars[0].finished = !dnf;
        g->cars[0].dnf = dnf;
        game_results(g);
        if (g->initials_pending)
            EXPECT(game_submit_initials(g), "save eliminated player's record initials");
        EXPECT(g->profile.championship_eliminated && game_next_track(g) == -1,
               "a fifth place or DNF cannot advance");
        capture_ui(u, g, r, dnf ? "championship-eliminated-dnf" : "championship-eliminated");
        ui_size(u, 1600, 450, SDL_GL_GetCurrentWindow());
        renderer_resize(r, 1600, 450);
        capture_ui(u, g, r, dnf ? "championship-eliminated-dnf-wide" : "championship-eliminated-wide");
        ui_size(u, 1280, 800, SDL_GL_GetCurrentWindow());
        renderer_resize(r, 1280, 800);
        float available = u->height - u->safe_top - u->safe_bottom;
        float panel_h = fminf(638, available - 44);
        float x = u->safe_left + (u->width - u->safe_left - u->safe_right - 920) * .5f;
        float y = u->safe_top + (available - panel_h) * .5f + panel_h - 50;
        click_ui(u, g, r, x + 36 + 80, y);
        EXPECT(g->screen == SCREEN_MENU && g->selected == 0,
               "elimination result returns to the opening track in the menu");
        capture_ui(u, g, r, "championship-eliminated-menu");
        click_ui(u, g, r, m + 80, start_y);
        EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == 0 &&
                   !g->profile.championship_eliminated && g->profile.championship_stages == 0,
               "new championship button clears the eliminated run");
    }
    g->test_mode = true;
}

static void test_record_ui(UI *u, Game *g, Renderer *r) {
    g->test_mode = false;
    game_set_mode(g, MODE_ARCADE);
    g->selected = 0;
    g->profile.records[0] = (TrackRecords){0};
    g->profile.best[0] = 0;
    g->screen = SCREEN_MENU;
    game_open_records(g);
    capture_ui(u, g, r, "records-empty");
    g->screen = SCREEN_MENU;
    for (int i = 0; i < 32; i++) {
        game_start(g);
        g->screen = SCREEN_RACE;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 90.123f - i * .25f;
        g->cars[0].best_lap = 40.012f;
        if (g->mode == MODE_CHAMPIONSHIP)
            for (int i = 1; i < CAR_COUNT; i++)
                if (!g->cars[i].finished)
                    g->cars[i].dnf = true;
        game_results(g);
        SDL_strlcpy(g->initials, i == 31 ? "ROB" : "ACE", 4);
        if (i == 31)
            capture_ui(u, g, r, "record-initials");
        EXPECT(game_submit_initials(g), "record initials persist");
    }
    capture_ui(u, g, r, "records-top10");
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_TAB;
    EXPECT(ui_record_event(u, g, &event) && g->record_history, "Tab opens record history");
    event.key.key = SDLK_RIGHT;
    ui_record_event(u, g, &event);
    EXPECT(g->record_page == 1, "keyboard pages history");
    capture_ui(u, g, r, "records-history");
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    ui_record_event(u, g, &event);
    EXPECT(g->record_page == 2, "gamepad pages history");
    ui_record_event(u, g, &event);
    EXPECT(g->record_page == 2, "history pagination stops at final page");
    ui_size(u, 1600, 450, NULL);
    capture_ui(u, g, r, "records-wide");
    float w = u->width, h = u->height;
    float panel_x = (w - 1120) * .5f, panel_y = (h - fminf(680, h - 40)) * .5f;
    click_ui(u, g, r, panel_x + 80, panel_y + 124);
    EXPECT(!g->record_history && g->record_page == 0, "touch switches to Top 10 and resets page");
    // Modal initials consume typing before global shortcuts (M toggles audio outside this form).
    game_start(g);
    g->screen = SCREEN_RACE;
    g->cars[0].finished = true;
    g->cars[0].finish_time = 75.012f;
    g->cars[0].best_lap = 35;
    game_results(g);
    SDL_strlcpy(g->initials, "AAA", 4);
    float cx = w * .5f, yy = (h - 510) * .5f;
    click_ui(u, g, r, cx - 104, yy + 345);
    EXPECT(g->initials[0] == 'Z', "touch letter decrement wraps A to Z");
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    ui_record_event(u, g, &event);
    EXPECT(g->initials[0] == 'A', "gamepad increment wraps Z to A");
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    ui_record_event(u, g, &event);
    ui_record_event(u, g, &event);
    EXPECT(g->initials_cursor == 2 && g->initials_pending, "gamepad confirms individual letters");
    ui_record_event(u, g, &event);
    EXPECT(g->screen == SCREEN_RECORDS && !g->initials_pending,
           "last gamepad letter saves initials");
    g->screen = SCREEN_RESULTS;
    game_start(g);
    g->screen = SCREEN_RACE;
    g->cars[0].finished = true;
    g->cars[0].finish_time = 74;
    g->cars[0].best_lap = 34;
    game_results(g);
    capture_ui(u, g, r, "initials-wide");
    initials_sound = g->profile.sound;
    EXPECT(glGetError() == GL_NO_ERROR, "record UI draws without GL errors");
}

static void test_save_failure_ui(UI *u, Game *g, Renderer *r) {
    char temp[2300];
    SDL_snprintf(temp, sizeof temp, "%sprofile.txt.tmp", pref_directory);
    for (int stage = 1; stage < TRACK_COUNT; stage++) {
        EXPECT(game_reset_profile(g), "reset save-failure fixture");
        g->test_mode = false;
        g->profile.championship_completed = stage;
        g->profile.championship_stages = stage;
        EXPECT(profile_save(&g->profile), "save starting stage");
        game_set_mode(g, MODE_CHAMPIONSHIP);
        game_start(g);
        EXPECT(SDL_CreateDirectory(temp), "block result save");
        g->screen = SCREEN_RACE;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 80;
        if (g->mode == MODE_CHAMPIONSHIP)
            for (int i = 1; i < CAR_COUNT; i++)
                if (!g->cars[i].finished)
                    g->cars[i].dnf = true;
        game_results(g);
        EXPECT(game_save_warning(g) && !g->initials_pending, "later stages warn on save failure");
        if (stage == 2) {
            ui_size(u, 1600, 450, SDL_GL_GetCurrentWindow());
            renderer_resize(r, 1600, 450);
        }
        capture_ui(u, g, r, stage == 1 ? "result-save-failed" : "final-save-failed-wide");
        float cx = (u->width + u->safe_left - u->safe_right) * .5f;
        float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 310) * .5f;
        click_ui(u, g, r, cx - 140, y + 224);
        EXPECT(game_save_warning(g) && g->selected == stage, "touch retry stays on failed save");
        SDL_Event event = {0};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_RETURN;
        EXPECT(ui_record_event(u, g, &event) && game_save_warning(g),
               "Enter retries without advancing");
        unsigned serial = g->profile.record_serial;
        EXPECT(SDL_RemovePath(temp), "unblock result save");
        if (stage == 1)
            click_ui(u, g, r, cx - 140, y + 224);
        else {
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            EXPECT(ui_record_event(u, g, &event), "gamepad A retries save");
        }
        Profile saved;
        profile_load(&saved);
        EXPECT(!game_save_warning(g) && g->screen == SCREEN_RESULTS && g->selected == stage,
               "successful retry reveals results without starting another race");
        EXPECT(saved.championship_completed == stage + 1 && saved.record_serial == serial &&
                   saved.records[stage].history_count == 1,
               "retry persists progress exactly once");
        EXPECT(g->championship_celebration == (stage == 2), "final celebration survives retry");
        // All three input methods can explicitly dismiss a persistent failure.
        for (int input = 0; input < 3; input++) {
            EXPECT(SDL_CreateDirectory(temp), "block save for unsaved continuation");
            EXPECT(!game_save_profile(g), "save fails again");
            if (input == 0)
                click_ui(u, g, r, cx + 140, y + 224);
            else {
                event.type = input == 1 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                if (input == 1)
                    event.key.key = SDLK_ESCAPE;
                else
                    event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
                EXPECT(ui_record_event(u, g, &event), "unsaved continuation consumes back action");
            }
            EXPECT(g->record_save_failed && !game_save_warning(g) && g->screen == SCREEN_RESULTS,
                   "continuation acknowledges failure and retains unsaved data");
            EXPECT(SDL_RemovePath(temp), "restore writable profile");
        }
        EXPECT(game_save_profile(g), "flush acknowledged changes");
    }
    for (int input = 0; input < 2; input++) {
        EXPECT(game_reset_profile(g), "reset failed-initials fixture");
        game_set_mode(g, MODE_ARCADE);
        game_start(g);
        EXPECT(SDL_CreateDirectory(temp), "block initials save");
        g->screen = SCREEN_RACE;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 80;
        if (g->mode == MODE_CHAMPIONSHIP)
            for (int i = 1; i < CAR_COUNT; i++)
                if (!g->cars[i].finished)
                    g->cars[i].dnf = true;
        game_results(g);
        EXPECT(g->initials_pending && game_save_warning(g),
               "failed Arcade save keeps initials form");
        SDL_Event event = {0};
        event.type = input == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (input == 0)
            event.key.key = SDLK_ESCAPE;
        else
            event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
        EXPECT(ui_record_event(u, g, &event) && !g->initials_pending && g->record_save_failed &&
                   !game_save_warning(g),
               "keyboard and gamepad can leave failed initials entry unsaved");
        EXPECT(SDL_RemovePath(temp), "unblock initials save");
        EXPECT(game_save_profile(g), "save retained Arcade result");
    }
    int w, h;
    SDL_GetWindowSizeInPixels(SDL_GL_GetCurrentWindow(), &w, &h);
    ui_size(u, w, h, SDL_GL_GetCurrentWindow());
    renderer_resize(r, w, h);
}

static void test_settings_save_ui(UI *u, Game *g, Renderer *r) {
    char temp[2300];
    SDL_snprintf(temp, sizeof temp, "%sprofile.txt.tmp", pref_directory);
    for (int setting = 0; setting < 5; setting++) {
        EXPECT(game_reset_profile(g), "reset settings-save fixture");
        Profile before = g->profile;
        bool compact = u->height - u->safe_top - u->safe_bottom < 690;
        float m = 46 + u->safe_left, w = u->width - m * 2;
        float y = compact ? 185 + u->safe_top : 255;
        float by = u->height - 77 - u->safe_bottom;
        EXPECT(SDL_CreateDirectory(temp), "block settings staging file");
        if (setting == 0) {
            g->screen = SCREEN_GARAGE;
            float cy = compact ? u->height - 183 - u->safe_bottom : 431;
            click_ui(u, g, r, m + 23 + 4 * 57, cy);
            EXPECT(g->profile.color == 4, "garage click applies requested color");
        } else if (setting == 1) {
            click_ui(u, g, r, m + w - 559 + 30, y + 30);
            EXPECT(g->profile.difficulty == 0, "difficulty click applies requested level");
        } else {
            int index = setting - 2;
            float pitch = compact ? (by - y - 84) / 3 : 72;
            click_ui(u, g, r, m + 20, y + (compact ? 74 : 85) + index * pitch + 10);
            const bool values[] = {g->profile.auto_accel, g->profile.sound,
                                   g->profile.music};
            const bool previous[] = {before.auto_accel, before.sound, before.music};
            EXPECT(values[index] != previous[index], "settings click applies requested toggle");
        }
        EXPECT(game_save_warning(g), "every settings and garage save failure is visible");
        Profile disk;
        profile_load(&disk);
        EXPECT(memcmp(&before, &disk, sizeof disk) == 0, "failed settings save preserves disk");
        Screen screen = g->screen;
        capture_ui(u, g, r, setting == 0 ? "garage-save-failed" : "settings-save-failed");
        // Covered navigation must remain inactive while the warning is open.
        click_ui(u, g, r, u->width - 145 - u->safe_right, by + 25);
        EXPECT(g->screen == screen && game_save_warning(g), "save warning blocks underlying UI");
        SDL_Event event = {.type = SDL_EVENT_KEY_DOWN};
        event.key.key = SDLK_RETURN;
        EXPECT(ui_record_event(u, g, &event) && game_save_warning(g), "failed retry stays modal");
        float cx = (u->width + u->safe_left - u->safe_right) * .5f;
        float wy = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 310) * .5f;
        if (setting % 2) {
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
            EXPECT(ui_record_event(u, g, &event) && !game_save_warning(g) && g->record_save_failed,
                   "gamepad can acknowledge unsaved settings");
            EXPECT(SDL_RemovePath(temp), "unblock acknowledged settings save");
            EXPECT(game_save_profile(g), "later save persists acknowledged changes");
        } else {
            EXPECT(SDL_RemovePath(temp), "unblock settings retry");
            click_ui(u, g, r, cx - 140, wy + 224);
            EXPECT(!game_save_warning(g), "touch retry clears settings warning");
        }
        profile_load(&disk);
        EXPECT(memcmp(&disk, &g->profile, sizeof disk) == 0 && g->screen == screen,
               "retry persists settings without navigating away");
    }
    EXPECT(game_reset_profile(g), "restore settings fixture");
}

static void test_reset_ui(UI *u, Game *g, Renderer *r) {
    g->screen = SCREEN_SETTINGS;
    Profile before = g->profile;
    float x = 46 + u->safe_left, by = u->height - 77 - u->safe_bottom;
    float cx = (u->width + u->safe_left - u->safe_right) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 310) * .5f;
    capture_ui(u, g, r, "settings-reset");
    click_ui(u, g, r, x + 80, by + 25);
    EXPECT(g->screen == SCREEN_RESET_DATA, "reset opens a confirmation screen");
    EXPECT(memcmp(&before, &g->profile, sizeof before) == 0, "opening reset preserves data");
    capture_ui(u, g, r, "reset-confirmation");
    click_ui(u, g, r, cx - 120, y + 280);
    EXPECT(g->screen == SCREEN_SETTINGS && memcmp(&before, &g->profile, sizeof before) == 0,
           "cancel preserves data and returns to settings");
    click_ui(u, g, r, x + 80, by + 25);
    char temp[2300];
    SDL_snprintf(temp, sizeof temp, "%sprofile.txt.tmp", pref_directory);
    EXPECT(SDL_CreateDirectory(temp), "block reset staging file");
    click_ui(u, g, r, cx + 120, y + 280);
    EXPECT(g->screen == SCREEN_RESET_DATA && u->reset_failed &&
               memcmp(&before, &g->profile, sizeof before) == 0,
           "failed reset stays on confirmation and preserves data");
    capture_ui(u, g, r, "reset-failed");
    EXPECT(SDL_RemovePath(temp), "unblock reset staging file");
    click_ui(u, g, r, cx + 120, y + 280);
    Profile saved;
    profile_load(&saved);
    EXPECT(g->screen == SCREEN_SETTINGS && !u->reset_failed && g->selected == 0 &&
               saved.championship_completed == 0 && saved.records[0].history_count == 0 &&
               saved.best[0] == 0 && strcmp(saved.initials, "AAA") == 0,
           "confirmed reset clears persisted progress and returns to default settings");
    capture_ui(u, g, r, "reset-success");
    EXPECT(!saved.initials_set && strcmp(game_player_name(g), "YOU") == 0,
           "reset clears confirmed initials and restores YOU");
}

static void test_mobile_ui(UI *u, Game *g, Renderer *r) {
    u->mobile = true;
    u->show_stats = false;
    // Phone-shaped safe canvas, including asymmetric notch and home-indicator insets.
    u->width = 1320;
    u->height = 612;
    u->safe_left = 96;
    u->safe_right = 56;
    u->safe_top = 0;
    u->safe_bottom = 36;
    r->ui_width = u->width;
    r->ui_height = u->height;
    renderer_resize(r, 1320, 612);
    float x = 120, w = 1120, bottom = 576;
    ui_clear_input(u);
    g->test_mode = true;
    g->profile.championship_completed = 0;
    g->profile.championship_stages = 0;
    memset(g->profile.championship_places, 0, sizeof g->profile.championship_places);
    g->screen = SCREEN_MENU;
    game_set_mode(g, MODE_CHAMPIONSHIP);
    capture_ui(u, g, r, "mobile-championship");
    click_ui(u, g, r, x + w * .48f, 138);
    EXPECT(g->mode == MODE_ARCADE, "mobile mode selector opens Arcade");
    click_ui(u, g, r, x + w - 50, bottom - 80);
    EXPECT(g->selected == 2, "mobile cards preview locked tracks");
    capture_ui(u, g, r, "mobile-locked");
    click_ui(u, g, r, x + 100, bottom - 192);
    EXPECT(g->screen == SCREEN_MENU && g->mode == MODE_CHAMPIONSHIP && g->selected == 0,
           "mobile locked track action returns to the unlocked championship stage");
    click_ui(u, g, r, x + 100, bottom - 192);
    EXPECT(g->screen == SCREEN_COUNTDOWN, "large mobile start button starts countdown");
    g->profile.touch = true;
    g->profile.auto_accel = true;
    capture_ui(u, g, r, "mobile-countdown");
    g->screen = SCREEN_RACE;
    g->countdown = 0;
    capture_ui(u, g, r, "mobile-race");
    g->profile.auto_accel = false;
    capture_ui(u, g, r, "mobile-pedals-rest");
    SDL_Event pedal = {.type = SDL_EVENT_FINGER_DOWN};
    pedal.tfinger.touchID = 1;
    pedal.tfinger.fingerID = 71;
    pedal.tfinger.x = (x + w - 72) / u->width;
    pedal.tfinger.y = (bottom - 86) / u->height;
    ui_event(u, &pedal, 1320, 612);
    Controls pedals = ui_controls(u, g);
    EXPECT(pedals.throttle == 1 && pedals.brake == 0, "gas pedal applies throttle immediately");
    ui_draw(u, g, r);
    SDL_Delay(70);
    capture_ui(u, g, r, "mobile-pedals-gas");
    EXPECT(u->gas_travel > .8f && u->brake_travel == 0,
           "gas depresses independently of brake");
    SDL_Event steering = {.type = SDL_EVENT_FINGER_DOWN};
    steering.tfinger.touchID = 1;
    steering.tfinger.fingerID = 73;
    float wheel_x = x + 152, wheel_y = bottom + 9;
    steering.tfinger.x = wheel_x / u->width;
    steering.tfinger.y = (wheel_y - 130) / u->height;
    ui_event(u, &steering, 1320, 612);
    pedals = ui_controls(u, g);
    EXPECT(pedals.steer == 0 && pedals.throttle == 1, "grabbing wheel does not snap steering");
    steering.type = SDL_EVENT_FINGER_MOTION;
    steering.tfinger.x = (wheel_x - sinf(.45f) * 130) / u->width;
    steering.tfinger.y = (wheel_y - cosf(.45f) * 130) / u->height;
    ui_event(u, &steering, 1320, 612);
    pedals = ui_controls(u, g);
    EXPECT(fabsf(pedals.steer + .5f) < .01f && pedals.throttle == 1,
           "half-turn produces half steering while holding gas");
    SDL_Delay(100);
    capture_ui(u, g, r, "mobile-wheel-left");
    EXPECT(u->steering_travel < -.35f && u->steering_travel > -.51f,
           "wheel displays proportional left steering");
    steering.type = SDL_EVENT_FINGER_MOTION;
    steering.tfinger.x = (wheel_x + sinf(.45f) * 130) / u->width;
    ui_event(u, &steering, 1320, 612);
    EXPECT(fabsf(ui_controls(u, g).steer - .5f) < .01f,
           "rotating across center gives proportional right steering");
    SDL_Delay(150);
    capture_ui(u, g, r, "mobile-wheel-right");
    EXPECT(u->steering_travel > .35f && u->steering_travel < .51f,
           "wheel displays proportional right steering");
    steering.type = SDL_EVENT_FINGER_CANCELED;
    ui_event(u, &steering, 1320, 612);
    pedals = ui_controls(u, g);
    EXPECT(pedals.steer == 0 && pedals.throttle == 1, "cancelled steering preserves gas touch");
    SDL_Delay(180);
    capture_ui(u, g, r, "mobile-wheel-centered");
    EXPECT(fabsf(u->steering_travel) < .1f, "wheel springs back to center after release");
    pedal.tfinger.fingerID = 72;
    pedal.tfinger.x = (x + w - 232) / u->width;
    ui_event(u, &pedal, 1320, 612);
    pedals = ui_controls(u, g);
    EXPECT(pedals.brake == 1 && u->gas && u->brake, "both pedal touches stay independent");
    SDL_Delay(70);
    capture_ui(u, g, r, "mobile-pedals-both");
    EXPECT(u->brake_travel > .8f, "brake pedal depresses while gas is held");
    pedal.type = SDL_EVENT_FINGER_CANCELED;
    ui_event(u, &pedal, 1320, 612);
    ui_controls(u, g);
    SDL_Delay(100);
    capture_ui(u, g, r, "mobile-pedals-release");
    EXPECT(u->brake_travel < .2f && u->gas_travel > .9f,
           "cancelled brake springs back without releasing gas");
    ui_clear_input(u);
    EXPECT(u->brake_travel == 0 && u->gas_travel == 0 && u->steering_travel == 0,
           "input reset releases pedals and centers the wheel");
    g->profile.auto_accel = true;
    click_ui(u, g, r, x + w - 36, 60);
    EXPECT(g->screen == SCREEN_PAUSE, "mobile pause target pauses the race");
    capture_ui(u, g, r, "mobile-pause");
    click_ui(u, g, r, (u->width + u->safe_left - u->safe_right) * .5f, (bottom - 384) * .5f + 148);
    EXPECT(g->screen == SCREEN_RACE, "mobile pause dialog resumes the race");
    click_ui(u, g, r, x + 60, bottom - 205);
    EXPECT(g->toast_time > 0, "mobile recovery target recovers the car");

    Screen screens[] = {SCREEN_GARAGE, SCREEN_HELP, SCREEN_SETTINGS};
    const char *names[] = {"mobile-garage", "mobile-help", "mobile-settings"};
    for (int i = 0; i < 3; i++) {
        g->screen = SCREEN_MENU;
        float tab_w = (w - 254) / 4;
        click_ui(u, g, r, x + 218 + (i + 1) * (tab_w + 12) + tab_w * .5f, 46);
        EXPECT(g->screen == screens[i], "mobile navigation selects the intended screen");
        capture_ui(u, g, r, names[i]);
    }
    bool automatic = g->profile.auto_accel;
    click_ui(u, g, r, x + w - 50, 140);
    EXPECT(g->profile.auto_accel != automatic, "entire mobile setting row toggles its value");
    EXPECT(u->mobile, "turning a setting off keeps mobile layout enabled");
    click_ui(u, g, r, x + 50, 382);
    EXPECT(g->profile.difficulty == 2, "mobile difficulty selection works");
    g->screen = SCREEN_GARAGE;
    click_ui(u, g, r, x + 40 + 5 * 84, bottom - 234);
    EXPECT(g->profile.color == 5, "mobile color swatches select a color");

    game_set_mode(g, MODE_ARCADE);
    game_start(g);
    g->screen = SCREEN_RACE;
    for (int i = 0; i < CAR_COUNT; i++) {
        g->cars[i].finished = true;
        g->cars[i].finish_time = 70 + i;
    }
    game_results(g);
    g->initials_pending = true;
    SDL_strlcpy(g->initials, "AAA", sizeof g->initials);
    capture_ui(u, g, r, "mobile-initials");
    float cx = (u->width + u->safe_left - u->safe_right) * .5f;
    float iy = (bottom - 536) * .5f;
    click_ui(u, g, r, cx - 124, iy + 218);
    EXPECT(g->initials[0] == 'B', "mobile initials plus target increments a letter");
    click_ui(u, g, r, cx - 124, iy + 374);
    EXPECT(g->initials[0] == 'A', "mobile initials minus target decrements a letter");
    g->initials_pending = false;
    capture_ui(u, g, r, "mobile-results");
    click_ui(u, g, r, x + w * .6f, bottom - 56);
    EXPECT(g->screen == SCREEN_RECORDS, "mobile results open track records");
    TrackRecords *table = &g->profile.records[g->selected];
    table->top_count = 10;
    for (int i = 0; i < 10; i++) {
        table->top[i] = (RaceRecord){.time = 70 + i,
                                     .best_lap = 34,
                                     .mode = MODE_ARCADE,
                                     .difficulty = 1,
                                     .rank = 1,
                                     .record = true,
                                     .improvement = 1.234f,
                                     .date = 1789600000};
        SDL_strlcpy(table->top[i].initials, "ROB", sizeof table->top[i].initials);
    }
    g->record_page = 0;
    capture_ui(u, g, r, "mobile-records");
    click_ui(u, g, r, x + w - 46, bottom - 56);
    EXPECT(g->record_page == 1, "mobile Top 10 paginates after four records");
    SDL_Event next = {.type = SDL_EVENT_KEY_DOWN};
    next.key.key = SDLK_RIGHT;
    ui_record_event(u, g, &next);
    EXPECT(g->record_page == 2, "keyboard pagination matches mobile record pages");
    capture_ui(u, g, r, "mobile-records-last");
    click_ui(u, g, r, x + 100, bottom - 56);
    EXPECT(g->screen == SCREEN_RESULTS, "mobile records return to results");
    click_ui(u, g, r, x + 80, bottom - 56);
    EXPECT(g->screen == SCREEN_COUNTDOWN, "mobile results replay the race");
    g->profile.championship_completed = 0;
    g->profile.championship_stages = 0;
    memset(g->profile.championship_places, 0, sizeof g->profile.championship_places);
    game_set_mode(g, MODE_CHAMPIONSHIP);
    game_start(g);
    g->test_mode = false;
    g->screen = SCREEN_RACE;
    g->cars[0].finished = true;
    g->cars[0].finish_time = 80;
    game_results(g);
    g->initials_pending = false;
    capture_ui(u, g, r, "mobile-results-waiting");
    click_ui(u, g, r, x + 100, bottom - 56);
    EXPECT(g->screen == SCREEN_RESULTS && !g->championship_scored,
           "mobile waiting state cannot advance before rivals finish");
    for (int i = 1; i < CAR_COUNT; i++)
        g->cars[i].dnf = true;
    game_tick(g, (Controls){0}, FIXED_DT, false);
    capture_ui(u, g, r, "mobile-standings");
    click_ui(u, g, r, x + 100, bottom - 56);
    EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == 1,
           "mobile championship results advance after scoring");
    g->screen = SCREEN_RACE;
    g->cars[0].dnf = true;
    for (int i = 1; i < CAR_COUNT; i++)
        g->cars[i].dnf = true;
    game_results(g);
    EXPECT(g->profile.championship_eliminated && game_championship_rank(g) == 1 &&
               !g->championship_celebration,
           "mobile DNF ends the run even when the player leads overall");
    capture_ui(u, g, r, "mobile-championship-eliminated");
    click_ui(u, g, r, x + 100, bottom - 56);
    EXPECT(g->screen == SCREEN_MENU && g->selected == 0,
           "mobile elimination returns to the new championship menu");
    capture_ui(u, g, r, "mobile-championship-eliminated-menu");
    game_continue(g);
    EXPECT(g->screen == SCREEN_COUNTDOWN && !g->profile.championship_eliminated,
           "new mobile championship clears elimination");
    g->test_mode = true;
    g->screen = SCREEN_RESULTS;
    g->initials_pending = true;
    g->record_save_failed = true;
    capture_ui(u, g, r, "mobile-initials-save-failed");
    click_ui(u, g, r, cx + 100, iy + 470);
    EXPECT(!g->initials_pending && g->save_failure_acknowledged,
           "mobile initials save failure offers explicit unsaved continuation");
    g->record_save_failed = false;
    g->screen = SCREEN_MENU;
    capture_ui(u, g, r, "mobile-navbar");
    float tab_w = (w - 254) / 4;
    click_ui(u, g, r, x + 218 + (tab_w + 12) + tab_w * .5f, 46);
    ui_draw(u, g, r);
    EXPECT(g->screen == SCREEN_GARAGE && u->menu_fade > .9f && u->nav_position < .01f,
           "mobile navigation starts a page fade and moves the active underline from its old tab");
    g->clock += .08f;
    renderer_draw(r, g, 0, 1);
    ui_draw(u, g, r);
    EXPECT(u->menu_fade > 0 && u->menu_fade < .9f && u->nav_position > 0 && u->nav_position < 1,
           "mobile transition progresses with time between fully rendered frames");
    char transition_path[2200];
    SDL_snprintf(transition_path, sizeof transition_path, "%smobile-transition.bmp",
                 pref_directory);
    EXPECT(renderer_screenshot(r, transition_path), "mobile transition midpoint captured");
    float position = u->nav_position;
    click_ui(u, g, r, x + 218 + 2 * (tab_w + 12) + tab_w * .5f, 46);
    ui_draw(u, g, r);
    EXPECT(g->screen == SCREEN_HELP && fabsf(u->nav_position - position) < .001f,
           "rapid navigation stays responsive and continues from the current underline position");
    g->clock += .3f;
    ui_draw(u, g, r);
    EXPECT(u->menu_fade == 0 && u->nav_position == 2,
           "mobile transition settles at the destination without a residual overlay");
    g->screen = SCREEN_RACE;
    ui_draw(u, g, r);
    EXPECT(!u->menu_seen && u->menu_fade == 0,
           "leaving menu pages clears the transition before gameplay");
    EXPECT(glGetError() == GL_NO_ERROR, "mobile compositions render without GL errors");
}

static void test_ui_draw(UI *u, Game *g, Renderer *r) {
    current_ui = u;
    drawn_frames++;
    if (drawn_frames == 1) {
        test_crowd_animation(r, g, false);
        test_crowd_animation(r, g, true);
        test_vegetation_rendering(r, g);
        test_boost_rendering(r, g);
        test_cloud_lighting(r, g);
        test_weather_rendering(r, g);
        test_water_rendering(r, g);
        test_props_rendering(r, g);
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
        EXPECT(glIsProgram(r->shader) && glIsProgram(r->weather_shader) &&
                   glIsProgram(u->program) && glIsTexture(u->fonts[0].texture) &&
                   glIsBuffer(r->car.vbo) && glIsBuffer(r->crowds[0].vbo) && r->crowds[0].count > 0,
               "world and UI resources exist in the replacement context");
    }
    if (drawn_frames == 7)
        EXPECT(g->screen == SCREEN_RACE && g->time > reset_time,
               "explicit resume continues the restored race");
    if (drawn_frames == 8) {
        game_set_mode(g, MODE_ARCADE);
        g->selected = 0;
        g->profile.best[0] = 900;
        g->profile.medals[0] = 0;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 80;
        if (g->mode == MODE_CHAMPIONSHIP)
            for (int i = 1; i < CAR_COUNT; i++)
                if (!g->cars[i].finished)
                    g->cars[i].dnf = true;
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
        EXPECT(saved.auto_accel == g->profile.auto_accel && saved.auto_accel &&
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
        if (g->mode == MODE_CHAMPIONSHIP)
            for (int i = 1; i < CAR_COUNT; i++)
                if (!g->cars[i].finished)
                    g->cars[i].dnf = true;
        game_results(g);
        Profile saved;
        profile_load(&saved);
        EXPECT(saved.best[0] == 70 && saved.medals[0] == 3,
               "a normal finish still saves its personal record and gold medal");

        // Both cross during one step, with the rival first. The actual finish
        // path must persist silver, without saving a premature player victory.
        game_set_mode(g, MODE_ARCADE);
        g->selected = 0;
        g->profile.best[0] = 900;
        g->profile.medals[0] = 0;
        game_start(g);
        g->screen = SCREEN_RACE;
        g->time = 60;
        const Track *track = &g->tracks[0];
        for (int i = 2; i < CAR_COUNT; i++)
            g->cars[i].dnf = true;
        for (int i = 0; i < 2; i++) {
            Car *car = &g->cars[i];
            Vec3 direction;
            car->pos = track_at(track, track->length * RACE_LAPS - .15f, 0, &direction);
            car->yaw = atan2f(direction.x, direction.z);
            car_surface_pose(track, &car->pos, car->yaw, &car->pitch, &car->roll);
            car->velocity = v3(sinf(car->yaw) * 30, 0, cosf(car->yaw) * 30);
            car->speed = 30;
            car->nearest = -1;
            car->last_s = track_nearest(track, car->pos, &car->nearest, NULL, NULL);
            car->progress = track->length * RACE_LAPS - (i == 0 ? .20f : .02f);
            car->lap = RACE_LAPS;
            car->checkpoints = RACE_LAPS * 4 - 1;
        }
        game_tick(g, (Controls){.throttle = 1}, FIXED_DT, false);
        profile_load(&saved);
        EXPECT(g->screen == SCREEN_RESULTS && g->cars[0].finished && g->cars[1].finished &&
                   g->finish_rank == 2 && game_car_precedes(g, 1, 0),
               "same-step finish ranks the earlier rival ahead of the player");
        EXPECT(saved.medals[0] == 2 && fabsf(saved.best[0] - g->cars[0].finish_time) < .00051f,
               "same-step finish persists the correct medal and crossing time");
        g->test_mode = true;
        wide = false;
    }
    if (drawn_frames == 14) {
        test_progression_ui(u, g, r);
        test_record_ui(u, g, r);
    }
    if (drawn_frames == 15) {
        EXPECT(g->screen == SCREEN_RECORDS && !g->initials_pending,
               "Enter confirms initials through the real event loop without restarting");
        EXPECT(strcmp(g->profile.initials, "MAX") == 0 && g->profile.sound == initials_sound,
               "typing initials consumes global shortcuts");
        Profile saved;
        profile_load(&saved);
        EXPECT(strcmp(saved.records[0].top[0].initials, "MAX") == 0,
               "typed initials are present after reloading the profile");
    }
    if (drawn_frames == 16) {
        EXPECT(g->screen == SCREEN_RESULTS, "Escape returns from records to race results");
        g->profile.championship_completed = 0;
        g->profile.championship_stages = 0;
        memset(g->profile.championship_places, 0, sizeof g->profile.championship_places);
        game_set_mode(g, MODE_CHAMPIONSHIP);
        game_start(g);
        g->screen = SCREEN_RACE;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 80;
        g->cars[1].finished = true;
        g->cars[1].finish_time = 75;
        game_results(g);
        EXPECT(g->finish_rank == 2 && g->initials_pending && g->screen == SCREEN_RESULTS,
               "the opening Championship podium asks for initials");
        SDL_Event confirm = {0};
        confirm.type = SDL_EVENT_KEY_DOWN;
        confirm.key.key = SDLK_RETURN;
        EXPECT(ui_record_event(u, g, &confirm) && !g->initials_pending &&
                   g->screen == SCREEN_RESULTS,
               "Enter confirms Championship initials and stays on classification");
        capture_ui(u, g, r, "championship-waiting");
        float rival_progress = g->cars[2].progress;
        for (int i = 0; i < 120; i++)
            game_tick(g, (Controls){0}, FIXED_DT, false);
        EXPECT(g->cars[2].progress > rival_progress && !g->championship_scored &&
                   g->profile.championship_stages == 0,
               "rivals keep racing on the results screen before the stage is saved");
        g->cars[2].finished = true;
        g->cars[2].finish_time = 85;
        g->cars[3].dnf = true;
        capture_ui(u, g, r, "championship-partial-results");
        for (int i = 4; i < CAR_COUNT; i++)
            g->cars[i].dnf = true;
        game_tick(g, (Controls){0}, FIXED_DT, false);
        EXPECT(g->championship_scored && g->profile.championship_stages == 1,
               "the overall standings are finalized once all rivals finish or retire");
        capture_ui(u, g, r, "championship-results-direct");
    }
    if (drawn_frames == 17) {
        EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == 1,
               "Enter starts the next Championship stage through the real event loop");
        g->screen = SCREEN_RACE;
        g->cars[0].finished = true;
        g->cars[0].finish_time = 80;
        if (g->mode == MODE_CHAMPIONSHIP)
            for (int i = 1; i < CAR_COUNT; i++)
                if (!g->cars[i].finished)
                    g->cars[i].dnf = true;
        game_results(g);
        EXPECT(g->screen == SCREEN_RESULTS && !g->initials_pending,
               "the next Championship podium also bypasses initials");
    }
    if (drawn_frames == 18) {
        EXPECT(g->screen == SCREEN_COUNTDOWN && g->selected == 2,
               "gamepad A starts the next Championship stage through the real event loop");
        test_save_failure_ui(u, g, r);
        test_settings_save_ui(u, g, r);
        test_reset_ui(u, g, r);
        g->test_mode = true;
        game_start(g);
        lifecycle_countdown = g->countdown;
        lifecycle_time = g->time;
        lifecycle_car = g->cars[0];
        u->fingers[0] = (Finger){.active = true, .id = 99};
        // Both callbacks run between frames, before the main loop sees either.
        push_lifecycle_pair();
        push_key(SDLK_RETURN);
    }
    if (drawn_frames == 19) {
        EXPECT(g->screen == SCREEN_PAUSE && g->countdown == lifecycle_countdown &&
                   g->time == lifecycle_time && !u->fingers[0].active,
               "rapid background/foreground preserves countdown pause and clears stale input");
    }
    if (drawn_frames == 20) {
        EXPECT(g->screen == SCREEN_COUNTDOWN && g->countdown < lifecycle_countdown,
               "countdown resumes only on a new explicit action");
        g->screen = SCREEN_RACE;
        g->countdown = 0;
        lifecycle_time = g->time;
        lifecycle_car = g->cars[0];
    }
    if (drawn_frames == 21) {
        EXPECT(
            g->screen == SCREEN_PAUSE && g->time == lifecycle_time &&
                g->cars[0].fuel == lifecycle_car.fuel &&
                vlen(vsub(g->cars[0].pos, lifecycle_car.pos)) == 0,
            "lifecycle events delivered during polling pause before simulation and queued input");
    }
    if (drawn_frames == 22) {
        EXPECT(g->screen == SCREEN_RACE && g->time > lifecycle_time,
               "explicit resume restarts the race after foregrounding");
        mute_sound = g->profile.sound;
        lifecycle_time = g->time;
        EXPECT(game_save_profile(g), "save baseline before mute shortcut");
        char temp[2300];
        SDL_snprintf(temp, sizeof temp, "%sprofile.txt.tmp", pref_directory);
        EXPECT(SDL_CreateDirectory(temp), "block mute shortcut save");
    }
    if (drawn_frames == 23) {
        EXPECT(g->screen == SCREEN_PAUSE && game_save_warning(g) && g->time == lifecycle_time &&
                   g->profile.sound != mute_sound,
               "mute save failure pauses the race and warns");
        Profile disk;
        profile_load(&disk);
        EXPECT(disk.sound == mute_sound, "failed mute shortcut leaves disk untouched");
        capture_ui(u, g, r, "mute-save-failed");
        char temp[2300];
        SDL_snprintf(temp, sizeof temp, "%sprofile.txt.tmp", pref_directory);
        EXPECT(SDL_RemovePath(temp), "unblock mute shortcut retry");
    }
    if (drawn_frames == 24) {
        Profile disk;
        profile_load(&disk);
        EXPECT(!game_save_warning(g) && g->screen == SCREEN_PAUSE && disk.sound != mute_sound &&
                   disk.music == disk.sound,
               "Enter retries mute save without also resuming gameplay");
        test_mobile_ui(u, g, r);
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
    char *args[] = {"ToyCars",     "--screen",   "race",   "--frames", "24",
                    "--benchmark", "--no-audio", "--size", "1280x720", NULL};
    int result = toycars_main((int)SDL_arraysize(args) - 1, args);
    EXPECT(result == 0 && drawn_frames == 24, "the event loop survives reset and completes");
    printf("Graphics integration tests: %s (%d failures)\n", failures ? "FAILED" : "PASSED",
           failures);
    return result || failures ? 1 : 0;
}
