#include "ui.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
#define GLSL "#version 300 es\nprecision highp float;\n"
#else
#define GLSL "#version 330 core\n"
#endif
static const Color INK = {.12f, .17f, .18f, 1}, MUTED = {.43f, .47f, .45f, 1},
                   PAPER = {.956f, .949f, .922f, 1}, ORANGE = {.89f, .29f, .12f, 1},
                   WHITE = {1, 1, 1, 1};
static const float WHEEL_MAX_ANGLE = .9f;
static const char *vs =
    GLSL "layout(location=0)in vec2 aPos;layout(location=1)in vec2 aUV;layout(location=2)in vec4 "
         "aColor;uniform vec2 uSize;out vec2 uv;out vec4 color;void "
         "main(){uv=aUV;color=aColor;gl_Position=vec4(aPos.x/uSize.x*2.-1.,1.-aPos.y/"
         "uSize.y*2.,0.,1.);}";
static const char *fs =
    GLSL "in vec2 uv;in vec4 color;out vec4 frag;uniform sampler2D uTexture;void "
         "main(){frag=vec4(color.rgb,color.a*texture(uTexture,uv).r);}";
static bool inside(Rect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static Color alpha(Color c, float a) {
    c.a = a;
    return c;
}
static void flush(UI *u) {
    if (!u->count)
        return;
    glBindTexture(GL_TEXTURE_2D, u->current_texture);
    // Replace storage so queued draws can keep reading the previous batch without
    // forcing this upload to wait for the GPU.
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(u->count * sizeof(UIVertex)), u->vertices,
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, u->count);
    u->count = 0;
}
static void texture(UI *u, GLuint t) {
    if (u->current_texture != t) {
        flush(u);
        u->current_texture = t;
    }
}
static void vertex(UI *u, float x, float y, float tx, float ty, Color c) {
    u->vertices[u->count++] = (UIVertex){x, y, tx, ty, c.r, c.g, c.b, c.a};
}
static void triangle(UI *u, float ax, float ay, float bx, float by, float cx, float cy,
                     Color color) {
    texture(u, u->white);
    if (u->count + 3 >= UI_MAX_VERTS)
        flush(u);
    vertex(u, ax, ay, 0, 0, color);
    vertex(u, bx, by, 0, 0, color);
    vertex(u, cx, cy, 0, 0, color);
}
static void rect(UI *u, Rect r, Color c) {
    triangle(u, r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, c);
    triangle(u, r.x, r.y, r.x + r.w, r.y + r.h, r.x, r.y + r.h, c);
}
static void line(UI *u, float x, float y, float xx, float yy, float width, Color c) {
    float dx = xx - x, dy = yy - y, l = sqrtf(dx * dx + dy * dy);
    if (l < .001f)
        return;
    float nx = -dy / l * width * .5f, ny = dx / l * width * .5f;
    triangle(u, x + nx, y + ny, x - nx, y - ny, xx - nx, yy - ny, c);
    triangle(u, x + nx, y + ny, xx - nx, yy - ny, xx + nx, yy + ny, c);
}
static void rounded(UI *u, Rect r, float radius, Color c) {
    radius = fminf(radius, fminf(r.w, r.h) * .5f);
    float cx = r.x + r.w * .5f, cy = r.y + r.h * .5f;
    float lastx = r.x + r.w - radius, lasty = r.y;
    for (int k = 0; k < 4; k++) {
        float x = k == 0 || k == 1 ? r.x + r.w - radius : r.x + radius,
              y = k < 2 ? r.y + radius : r.y + r.h - radius;
        // Clockwise: top-right, bottom-right, bottom-left, top-left.
        if (k == 1)
            y = r.y + r.h - radius;
        if (k == 2)
            x = r.x + radius;
        if (k == 3)
            y = r.y + radius;
        for (int j = 0; j <= 8; j++) {
            float a = (-.5f + k * .5f + j / 16.f) * PI;
            float px = x + cosf(a) * radius, py = y + sinf(a) * radius;
            triangle(u, cx, cy, lastx, lasty, px, py, c);
            lastx = px;
            lasty = py;
        }
    }
    triangle(u, cx, cy, lastx, lasty, r.x + r.w - radius, r.y, c);
}
static void circle(UI *u, float x, float y, float r, Color c) {
    for (int i = 0; i < 40; i++) {
        float a = i * 2 * PI / 40, b = (i + 1) * 2 * PI / 40;
        triangle(u, x, y, x + cosf(a) * r, y + sinf(a) * r, x + cosf(b) * r, y + sinf(b) * r, c);
    }
}
static void ring(UI *u, float x, float y, float radius, float w, Color c) {
    for (int i = 0; i < 48; i++) {
        float a = i * 2 * PI / 48, b = (i + 1) * 2 * PI / 48;
        line(u, x + cosf(a) * radius, y + sinf(a) * radius, x + cosf(b) * radius,
             y + sinf(b) * radius, w, c);
    }
}
static float text_width(UI *u, int font, float size, const char *s) {
    float w = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c >= 32 && c < 128)
            w += u->fonts[font].glyphs[c - 32][6] * size / 72;
    }
    return w;
}
static void label(UI *u, int font, float size, float x, float y, Color color, const char *s) {
    UIFont *f = &u->fonts[font];
    texture(u, f->texture);
    float scale = size / 72;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c < 32 || c > 127)
            continue;
        float *g = f->glyphs[c - 32];
        float w = g[4] * scale, h = g[5] * scale;
        if (u->count + 6 >= UI_MAX_VERTS)
            flush(u);
        vertex(u, x, y, g[0], g[1], color);
        vertex(u, x + w, y, g[2], g[1], color);
        vertex(u, x + w, y + h, g[2], g[3], color);
        vertex(u, x, y, g[0], g[1], color);
        vertex(u, x + w, y + h, g[2], g[3], color);
        vertex(u, x, y + h, g[0], g[3], color);
        x += g[6] * scale;
    }
}
static void center_label(UI *u, int font, float size, float x, float y, Color c, const char *s) {
    label(u, font, size, x - text_width(u, font, size, s) * .5f, y, c, s);
}
static void caps(UI *u, float x, float y, Color c, const char *s) {
    label(u, 0, 16, x, y, c, s);
}
static bool hit(UI *u, Rect rect) {
    if (inside(rect, u->mx, u->my))
        u->hovered = 1;
    for (int i = 0; i < u->click_count; i++) {
        UIClick *click = &u->clicks[i];
        if (!click->consumed && inside(rect, click->x, click->y) &&
            inside(rect, click->press_x, click->press_y)) {
            click->consumed = true;
            return true;
        }
    }
    return false;
}
static bool button(UI *u, Rect r, const char *s, bool primary) {
    bool hover = inside(r, u->mx, u->my), clicked = hit(u, r);
    Color bg = primary ? ORANGE : WHITE;
    if (hover) {
        bg.r *= .94f;
        bg.g *= .94f;
        bg.b *= .94f;
    }
    if (!primary)
        rounded(u, (Rect){r.x - 1, r.y - 1, r.w + 2, r.h + 2}, 11, rgb(.80, .81, .76));
    rounded(u, r, 10, bg);
    float size = u->mobile ? 30 : 24;
    center_label(u, 1, size, r.x + r.w * .5f, r.y + (r.h - size * 1.20f) * .5f,
                 primary ? WHITE : INK, s);
    return clicked;
}
static void checker(UI *u, float x, float y, float s, Color a, Color b) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            rect(u, (Rect){x + i * s, y + j * s, s, s}, (i + j) % 2 ? a : b);
}
static void fuel_icon(UI *u, float x, float y, float s, Color c) {
    rounded(u, (Rect){x, y + s * .17f, s * .65f, s * .80f}, s * .1f, c);
    line(u, x + s * .17f, y + s * .10f, x + s * .49f, y + s * .10f, s * .1f, c);
    line(u, x + s * .48f, y + s * .14f, x + s * .66f, y + s * .02f, s * .1f, c);
    line(u, x + s * .17f, y + s * .43f, x + s * .48f, y + s * .74f, s * .08f, PAPER);
    line(u, x + s * .48f, y + s * .43f, x + s * .17f, y + s * .74f, s * .08f, PAPER);
}
static void map(UI *u, const Track *t, Rect r, Color color, float thickness, const Game *g,
                bool markers) {
    float minx = 1e6f, maxx = -1e6f, minz = 1e6f, maxz = -1e6f;
    for (int i = 0; i < t->count; i++) {
        Vec3 p = t->points[i].p;
        minx = fminf(minx, p.x);
        maxx = fmaxf(maxx, p.x);
        minz = fminf(minz, p.z);
        maxz = fmaxf(maxz, p.z);
    }
    float s = fminf(r.w / (maxx - minx), r.h / (maxz - minz));
    float ox = r.x + (r.w - (maxx - minx) * s) * .5f, oy = r.y + (r.h - (maxz - minz) * s) * .5f;
    for (int i = 0; i < t->count; i += 3) {
        Vec3 a = t->points[i].p, b = t->points[(i + 3) % t->count].p;
        line(u, ox + (a.x - minx) * s, oy + (a.z - minz) * s, ox + (b.x - minx) * s,
             oy + (b.z - minz) * s, thickness, color);
    }
    Vec3 p = t->points[0].p;
    circle(u, ox + (p.x - minx) * s, oy + (p.z - minz) * s, thickness * .9f, ORANGE);
    if (markers) {
        for (int i = 7; i >= 0; i--) {
            p = g->cars[i].pos;
            float x = ox + (p.x - minx) * s, y = oy + (p.z - minz) * s;
            if (i == 0)
                circle(u, x, y, 6.5f, WHITE);
            circle(u, x, y, i == 0 ? 4.5f : 3, g->cars[i].color);
        }
    }
}
static void format_time(char *s, size_t n, float seconds) {
    int m = (int)seconds / 60;
    float sec = seconds - m * 60;
    SDL_snprintf(s, n, "%d:%05.2f", m, sec);
}

static const char *medal_name(int medal) {
    switch (medal) {
    case 3:
        return "GOLD";
    case 2:
        return "SILVER";
    case 1:
        return "BRONZE";
    default:
        return "NO MEDAL";
    }
}

static Color medal_color(int medal) {
    switch (medal) {
    case 3:
        return rgb(.60f, .40f, .08f);
    case 2:
        return rgb(.38f, .45f, .51f);
    case 1:
        return rgb(.63f, .34f, .18f);
    default:
        return MUTED;
    }
}

static void track_record(UI *u, const Profile *profile, int track, Rect row, float size) {
    char time[32] = "--:--", best[48];
    if (profile->best[track] > 0)
        format_time(time, sizeof time, profile->best[track]);
    SDL_snprintf(best, sizeof best, "BEST %s", time);
    label(u, 0, size, row.x, row.y, MUTED, best);

    int medal = profile->medals[track];
    const char *name = medal_name(medal);
    float x = row.x + row.w - text_width(u, 0, size, name);
    Color color = medal_color(medal);
    if (medal > 0)
        circle(u, x - 9, row.y + size * .65f, 3.5f, color);
    label(u, 0, size, x, row.y, color, name);
}

bool ui_init(UI *u) {
    memset(u, 0, sizeof *u);
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    u->mobile = true;
#endif
    u->program = render_program(vs, fs);
    if (!u->program)
        return false;
    glGenVertexArrays(1, &u->vao);
    glBindVertexArray(u->vao);
    glGenBuffers(1, &u->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, u->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof u->vertices, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void *)(4 * sizeof(float)));
    glGenTextures(1, &u->white);
    glBindTexture(GL_TEXTURE_2D, u->white);
    unsigned char white = 255;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const char *paths[] = {"fonts/body.tcf", "fonts/display.tcf"};
    for (int i = 0; i < 2; i++) {
        size_t size;
        unsigned char *d = asset_read(paths[i], &size);
        if (!d || size < 12 + sizeof u->fonts[i].glyphs || memcmp(d, "TCF1", 4)) {
            SDL_free(d);
            return false;
        }
        uint32_t w, h;
        memcpy(&w, d + 4, 4);
        memcpy(&h, d + 8, 4);
        w = SDL_Swap32LE(w);
        h = SDL_Swap32LE(h);
        if (w != 1536 || h != 768 || size != 12 + sizeof u->fonts[i].glyphs + (size_t)w * h) {
            SDL_free(d);
            return false;
        }
        memcpy(u->fonts[i].glyphs, d + 12, sizeof u->fonts[i].glyphs);
        glGenTextures(1, &u->fonts[i].texture);
        glBindTexture(GL_TEXTURE_2D, u->fonts[i].texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (int)w, (int)h, 0, GL_RED, GL_UNSIGNED_BYTE,
                     d + 12 + sizeof u->fonts[i].glyphs);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        SDL_free(d);
    }
    return true;
}
static void layout(UI *u, int w, int h, SDL_FRect safe) {
    // Fit a usable canvas into the safe height on wide screens. Keeping the
    // width fixed at 1280 can make even positive-sized windows too short for UI.
    float scale = fminf(w / 1280.f, h * safe.h / 576.f);
    if (u->mobile)
        scale = fminf(w * safe.w / 1120.f, h * safe.h / 576.f);
    float width = w / scale, height = h / scale;
    float left = safe.x * width, top = safe.y * height;
    float right = (1 - safe.x - safe.w) * width;
    float bottom = (1 - safe.y - safe.h) * height;
    if (width != u->width || height != u->height || left != u->safe_left || top != u->safe_top ||
        right != u->safe_right || bottom != u->safe_bottom) {
        ui_clear_input(u);
        u->menu_seen = false;
    }
    u->width = width;
    u->height = height;
    u->safe_left = left;
    u->safe_top = top;
    u->safe_right = right;
    u->safe_bottom = bottom;
}
void ui_size(UI *u, int w, int h, SDL_Window *window) {
    if (w <= 0 || h <= 0)
        return;
    SDL_Rect safe;
    SDL_FRect bounds = {0, 0, 1, 1};
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    if (SDL_GetWindowSafeArea(window, &safe) && ww > 0 && wh > 0 && safe.w > 0 && safe.h > 0)
        bounds = (SDL_FRect){(float)safe.x / ww, (float)safe.y / wh, (float)safe.w / ww,
                             (float)safe.h / wh};
    layout(u, w, h, bounds);
}
bool ui_input_blocked(const UI *u) {
    return u->height > u->width;
}
void ui_clear_input(UI *u) {
    memset(u->fingers, 0, sizeof u->fingers);
    u->mouse_down = false;
    u->mouse_press_x = u->mouse_press_y = 0;
    u->click_count = 0;
    u->left = u->right = u->gas = u->brake = false;
    u->brake_travel = u->gas_travel = u->steering_travel = 0;
    u->wheel_input = u->wheel_pointer_angle = 0;
    u->wheel_pointer = 0;
    u->control_frame_time = 0;
}

static void queue_click(UI *u, float press_x, float press_y, float x, float y) {
    // Keep separate press/release pairs so another finger cannot overwrite a
    // tap waiting for the next frame. Excess input must not overwrite old taps.
    if (u->click_count < UI_MAX_CLICKS) {
        u->clicks[u->click_count++] = (UIClick){press_x, press_y, x, y, false};
    }
}

void ui_event(UI *u, const SDL_Event *event, int window_width, int window_height) {
    if (ui_input_blocked(u)) {
        ui_clear_input(u);
        return;
    }
    if (window_width <= 0 || window_height <= 0)
        return;

    if (event->type == SDL_EVENT_MOUSE_MOTION && event->motion.which != SDL_TOUCH_MOUSEID) {
        u->mouse_x = u->mx = event->motion.x * u->width / window_width;
        u->mouse_y = u->my = event->motion.y * u->height / window_height;
    }
    if ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event->button.which != SDL_TOUCH_MOUSEID && event->button.button == SDL_BUTTON_LEFT) {
        u->mouse_x = u->mx = event->button.x * u->width / window_width;
        u->mouse_y = u->my = event->button.y * u->height / window_height;
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            u->mouse_down = true;
            u->mouse_press_x = u->mouse_x;
            u->mouse_press_y = u->mouse_y;
        } else if (u->mouse_down) {
            queue_click(u, u->mouse_press_x, u->mouse_press_y, u->mouse_x, u->mouse_y);
            u->mouse_down = false;
            if (u->wheel_pointer == 1) {
                u->wheel_pointer = 0;
                u->wheel_input = 0;
            }
        }
    }

    if (event->type != SDL_EVENT_FINGER_DOWN && event->type != SDL_EVENT_FINGER_MOTION &&
        event->type != SDL_EVENT_FINGER_UP && event->type != SDL_EVENT_FINGER_CANCELED)
        return;

    float x = event->tfinger.x * u->width;
    float y = event->tfinger.y * u->height;
    Finger *finger = NULL;
    for (int i = 0; i < UI_MAX_FINGERS; i++) {
        if (u->fingers[i].active && u->fingers[i].id == event->tfinger.fingerID &&
            u->fingers[i].touch_id == event->tfinger.touchID) {
            finger = &u->fingers[i];
            break;
        }
    }
    if (event->type == SDL_EVENT_FINGER_DOWN) {
        if (finger)
            return; // Ignore a duplicate down without losing the original press.
        for (int i = 0; i < UI_MAX_FINGERS; i++) {
            if (!u->fingers[i].active) {
                finger = &u->fingers[i];
                *finger = (Finger){.touch_id = event->tfinger.touchID,
                                   .id = event->tfinger.fingerID,
                                   .press_x = x,
                                   .press_y = y,
                                   .active = true};
                break;
            }
        }
    }
    // Unknown releases include fingers canceled by focus loss or a screen
    // transition. They must never become clicks on the new screen.
    if (!finger)
        return;

    finger->x = u->mx = x;
    finger->y = u->my = y;
    if (event->type == SDL_EVENT_FINGER_UP) {
        queue_click(u, finger->press_x, finger->press_y, x, y);
        finger->active = false;
    } else if (event->type == SDL_EVENT_FINGER_CANCELED) {
        finger->active = false;
    }
    if (!finger->active && u->wheel_pointer == (int)(finger - u->fingers) + 2) {
        u->wheel_pointer = 0;
        u->wheel_input = 0;
    }
}
static void control_rects(UI *u, Rect *left, Rect *right, Rect *brake, Rect *gas) {
    float y = u->height - u->safe_bottom - 152, x = u->safe_left + 24;
    float end = u->width - u->safe_right - 24;
    *left = (Rect){x, y, 144, 132};
    *right = (Rect){x + 160, y, 144, 132};
    *brake = (Rect){end - 304, y, 144, 132};
    *gas = (Rect){end - 144, y, 144, 132};
}
Controls ui_controls(UI *u, const Game *g) {
    Controls c = {0};
    u->left = u->right = u->brake = u->gas = false;
    if (ui_input_blocked(u) || g->screen != SCREEN_RACE) {
        u->wheel_pointer = 0;
        u->wheel_input = 0;
        return c;
    }
    const bool *keys = SDL_GetKeyboardState(NULL);
    c.steer = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) -
              (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
    c.brake = (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) ? 1 : 0;
    c.drift = keys[SDL_SCANCODE_SPACE];
    c.throttle = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] || g->profile.auto_accel) ? 1 : 0;
    Rect l, r, b, a;
    control_rects(u, &l, &r, &b, &a);
    Rect wheel = {l.x, l.y, r.x + r.w - l.x, u->height - l.y};
    float wheel_cx = wheel.x + wheel.w * .5f, wheel_cy = l.y + l.h + 29;
    if (!u->mobile || !g->profile.touch ||
        (u->wheel_pointer == 1 && !u->mouse_down) ||
        (u->wheel_pointer >= 2 && !u->fingers[u->wheel_pointer - 2].active)) {
        u->wheel_pointer = 0;
        u->wheel_input = 0;
    }
    if (u->mobile && g->profile.touch)
        for (int i = -1; i < UI_MAX_FINGERS; i++) {
            bool active = i < 0 ? u->mouse_down : u->fingers[i].active;
            if (!active)
                continue;
            float x = i < 0 ? u->mouse_x : u->fingers[i].x,
                  y = i < 0 ? u->mouse_y : u->fingers[i].y;
            float press_x = i < 0 ? u->mouse_press_x : u->fingers[i].press_x,
                  press_y = i < 0 ? u->mouse_press_y : u->fingers[i].press_y;
            if (u->mobile && inside(wheel, press_x, press_y)) {
                // Capture one pointer for the whole gesture, including outside
                // the wheel. Pedal fingers cannot take ownership or steer it.
                if (!u->wheel_pointer) {
                    u->wheel_pointer = i + 2;
                    u->wheel_pointer_angle = atan2f(press_y - wheel_cy, press_x - wheel_cx);
                    u->wheel_input = u->steering_travel;
                }
                if (u->wheel_pointer == i + 2 &&
                    hypotf(x - wheel_cx, y - wheel_cy) > 24) {
                    float angle = atan2f(y - wheel_cy, x - wheel_cx);
                    float delta = angle - u->wheel_pointer_angle;
                    delta = atan2f(sinf(delta), cosf(delta));
                    u->wheel_input = clampf(u->wheel_input + delta / WHEEL_MAX_ANGLE, -1, 1);
                    u->wheel_pointer_angle = angle;
                }
                continue;
            }
            if (inside(b, x, y))
                u->brake = true;
            if (inside(a, x, y))
                u->gas = true;
        }
    if (u->mobile && u->wheel_pointer) {
        c.steer = u->wheel_input;
        u->left = c.steer < 0;
        u->right = c.steer > 0;
    }
    if (u->brake) {
        c.brake = 1;
        c.throttle = 0;
    }
    if (u->gas) {
        if (g->profile.auto_accel)
            c.drift = true;
        else
            c.throttle = 1;
    }
    if (c.brake > .8f) {
        c.throttle = 0;
        c.reverse = true;
    }
    return c;
}

static void header(UI *u, Game *g) {
    float margin = 46 + u->safe_left;
    checker(u, margin, 29 + u->safe_top, 8, ORANGE, INK);
    label(u, 1, 34, margin + 36, 17 + u->safe_top, INK, "TOYCARS");
    float x = margin + 255, y = 35 + u->safe_top;
    const char *tabs[] = {"RACE", "GARAGE", "HOW TO PLAY"};
    Screen screens[] = {SCREEN_MENU, SCREEN_GARAGE, SCREEN_HELP};
    for (int i = 0; i < 3; i++) {
        float tw = text_width(u, 0, 15, tabs[i]);
        Rect r = {x - 8, y - 9, tw + 16, 39};
        bool current = g->screen == screens[i];
        caps(u, x, y - 7, current ? INK : MUTED, tabs[i]);
        if (current)
            rect(u, (Rect){x, y + 24, tw, 2}, ORANGE);
        if (hit(u, r))
            g->screen = screens[i];
        x += tw + 35;
    }
    float status = u->width - 290 - u->safe_right;
    circle(u, status, y + 3, 3.5f, rgb(.25, .56, .37));
    caps(u, status + 12, y - 8, MUTED, "OFFLINE / SOLO");
    Rect settings = {u->width - 87 - u->safe_right, 21 + u->safe_top, 42, 38};
    rounded(u, settings, 10, WHITE);
    for (int i = 0; i < 3; i++) {
        float yy = settings.y + 12 + i * 7;
        line(u, settings.x + 11, yy, settings.x + 31, yy, 1.5f, INK);
        circle(u, settings.x + (i % 2 ? 25 : 17), yy, 2.8f, INK);
    }
    if (hit(u, settings))
        g->screen = SCREEN_SETTINGS;
    line(u, margin, 79 + u->safe_top, u->width - 46 - u->safe_right, 79 + u->safe_top, 1,
         rgb(.83, .84, .79));
}

static void menu(UI *u, Game *g) {
    float m = 46 + u->safe_left, h = u->height;
    Track *t = &g->tracks[g->selected];
    float hero = 105 + u->safe_top;
    float available = h - u->safe_top - u->safe_bottom;
    bool compact = available < 690, tight = available < 560;
    float card_h = tight ? 94 : 116, cards_y = h - card_h - 54 - u->safe_bottom;
    if (tight)
        hero = 98 + u->safe_top;
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    caps(u, m, hero, t->accent, "CHOOSE YOUR RACE");
    if (button(u, (Rect){m, hero + 28, 228, 44}, "CHAMPIONSHIP", championship))
        game_set_mode(g, MODE_CHAMPIONSHIP);
    if (button(u, (Rect){m + 240, hero + 28, 158, 44}, "ARCADE", !championship))
        game_set_mode(g, MODE_ARCADE);
    bool unlocked = game_track_unlocked(&g->profile, g->selected);
    float big = compact ? 48 : 72;
    label(u, 1, big, m - 2, hero + 88, INK,
          championship ? "Chase the podium." : "Just one more race.");
    float yy = hero + (compact ? 151 : 205);
    char goal[100];
    if (!unlocked)
        SDL_snprintf(goal, sizeof goal, "Complete %s to unlock.", g->tracks[g->selected - 1].name);
    else if (championship && g->profile.championship_eliminated)
        SDL_snprintf(goal, sizeof goal, "Eliminated in stage %d. Start a new championship.",
                     g->profile.championship_stages);
    else if (championship && g->profile.championship_stages == TRACK_COUNT)
        SDL_snprintf(goal, sizeof goal, "Overall P%d / %s. Start a new championship.",
                     game_championship_rank(g),
                     game_championship_rank(g) <= 3 ? "Medal won" : "No medal");
    else if (championship)
        SDL_snprintf(goal, sizeof goal, "Overall P%d / %d points / Stage %d of %d",
                     game_championship_rank(g), game_championship_points(g, 0),
                     g->profile.championship_stages + 1, TRACK_COUNT);
    else
        SDL_strlcpy(goal, "Pick an unlocked track. Chase your personal best.", sizeof goal);
    label(u, 0, 18, m, yy, MUTED, goal);
    label(u, 0, 15, m, yy + 27, MUTED,
          championship ? "Stages 1-2: finish top 4 to advance. Final: overall top 3 wins a medal."
                       : "Single races. Unlock more tracks in Championship.");
    float by = yy + (compact ? 48 : 65), button_h = compact ? 50 : 58;
    if (unlocked) {
        if (button(u, (Rect){m, by, 231, button_h},
                   championship
                       ? (g->profile.championship_eliminated ||
                                  g->profile.championship_stages == TRACK_COUNT ? "NEW CHAMPIONSHIP"
                          : g->profile.championship_stages == 0         ? "START CHAMPIONSHIP"
                                                                        : "RACE NEXT STAGE")
                       : "LET'S RACE",
                   true)) {
            game_start(g);
            ui_clear_input(u);
        }
    } else {
        rounded(u, (Rect){m, by, 231, button_h}, 10, rgb(.83, .84, .79));
        center_label(u, 0, 16, m + 115, by + 17, MUTED, "TRACK LOCKED");
        if (button(u, (Rect){m + 245, by, 231, button_h}, "GO TO CHAMPIONSHIP", false))
            game_set_mode(g, MODE_CHAMPIONSHIP);
    }
    if (!championship &&
        button(u, (Rect){m + (unlocked ? 245 : 490), by, 176, button_h}, "TRACK RECORDS", false)) {
        game_open_records(g);
        ui_clear_input(u);
    }
    // Small race passport on the live Blender diorama.
    float tx = u->width * .56f;
    caps(u, tx, hero + 4, t->accent, t->subtitle);
    char s[100];
    SDL_snprintf(s, sizeof s, "0%d / 03", g->selected + 1);
    caps(u, u->width - 117 - u->safe_right, hero + 4, MUTED, s);
    float stats_y = cards_y - (tight ? 58 : 80);
    rounded(u, (Rect){u->width - 336 - u->safe_right, stats_y, 284, 44}, 12, alpha(WHITE, .82f));
    SDL_snprintf(s, sizeof s, "%.0f M LOOP    /    %.0f M CLIMB    /    3 JUMPS", t->length,
                 t->elevation);
    center_label(u, 0, 13, u->width - 194 - u->safe_right, stats_y + 12, INK, s);
    if (!tight) {
        caps(u, m, cards_y - 32, INK,
             championship ? "YOUR CHAMPIONSHIP ROUTE" : "PICK YOUR PLAYGROUND");
        SDL_snprintf(s, sizeof s, "%d / %d TRACKS UNLOCKED",
                     g->profile.championship_completed < TRACK_COUNT
                         ? g->profile.championship_completed + 1
                         : TRACK_COUNT,
                     TRACK_COUNT);
        caps(u, u->width - 260 - u->safe_right, cards_y - 32, MUTED, s);
    }
    float cw = (u->width - m - (46 + u->safe_right) - 32) / 3;
    for (int i = 0; i < 3; i++) {
        Track *tr = &g->tracks[i];
        float x = m + i * (cw + 16);
        Rect card = {x, cards_y, cw, card_h};
        bool selected = i == g->selected;
        rounded(u, (Rect){x - 1.5f, cards_y - 1.5f, cw + 3, card_h + 3}, 14,
                selected ? tr->accent : rgb(.84, .85, .80));
        bool open = game_track_unlocked(&g->profile, i);
        rounded(u, card, 12, open ? WHITE : rgb(.88, .89, .85));
        Color mapbg = i == 0   ? rgb(.86, .90, .77)
                      : i == 1 ? rgb(.80, .91, .88)
                               : rgb(.84, .89, .94);
        float map_w = tight ? 86 : 108, text_x = x + (tight ? 108 : 131);
        rounded(u, (Rect){x + 8, cards_y + 8, map_w, card_h - 16}, 9, mapbg);
        map(u, tr, (Rect){x + 20, cards_y + 17, map_w - 24, card_h - 34}, tr->accent, 3.5f, g,
            false);
        label(u, 0, tight ? 14 : 16, text_x, cards_y + (tight ? 10 : 14), tr->accent, tr->subtitle);
        label(u, 1, tight ? 28 : 30, text_x, cards_y + (tight ? 30 : 37), INK, tr->name);
        Rect record = {text_x, cards_y + (tight ? 66 : 80), x + cw - 14 - text_x, 20};
        if (!open)
            label(u, 0, 13, record.x, record.y, MUTED, "LOCKED / COMPLETE PREVIOUS STAGE");
        else if (championship)
            label(u, 0, 14, record.x, record.y, tr->accent,
                  i < g->profile.championship_stages ? "STAGE SCORED"
                                                     : "POINTS COUNT TOWARD OVERALL");
        else
            track_record(u, &g->profile, i, record, tight ? 13 : 14);
        if (selected) {
            circle(u, x + cw - 18, cards_y + 20, 6, tr->accent);
            circle(u, x + cw - 18, cards_y + 20, 2, WHITE);
        }
        if (hit(u, card)) {
            g->selected = i;
            g->sound_event = 8;
        }
    }
    label(u, 0, 13, m, h - 30 - u->safe_bottom, MUTED, "MADE FOR THE JOY OF THE DRIVE.");
    label(u, 0, 13, u->width - 243 - u->safe_right, h - 30 - u->safe_bottom, MUTED,
          "TOYCARS  /  THE LITTLE RALLY CLUB");
}

static void garage(UI *u, Game *g) {
    float m = 46 + u->safe_left;
    bool compact = u->height - u->safe_top - u->safe_bottom < 690;
    float cy = compact ? u->height - 183 - u->safe_bottom : 431;
    caps(u, m, compact ? 98 + u->safe_top : 117, ORANGE, "YOUR RIDE, YOUR RULES");
    label(u, 1, compact ? 57 : 81, m - 2, compact ? 125 + u->safe_top : 151, INK, "Meet the");
    label(u, 1, compact ? 57 : 81, m - 2, compact ? 176 + u->safe_top : 222, INK, "Trailblazer.");
    label(u, 0, 18, m, compact ? 251 + u->safe_top : 329, MUTED,
          "A little rally car with a lot of heart.");
    if (!compact)
        label(u, 0, 19, m, 355, MUTED, "Pick a color. Leave your mark.");
    for (int i = 0; i < 6; i++) {
        float x = m + 23 + i * 57;
        circle(u, x, cy, 21, CAR_COLORS[i]);
        if (g->profile.color == i) {
            ring(u, x, cy, 26, 2, INK);
            circle(u, x, cy, 5, WHITE);
        }
        if (hit(u, (Rect){x - 27, cy - 27, 54, 54})) {
            g->profile.color = i;
            game_save_profile(g);
            ui_clear_input(u);
            g->sound_event = 8;
        }
    }
    caps(u, m, cy + 44, INK, CAR_COLOR_NAMES[g->profile.color]);
    if (button(u, (Rect){m, u->height - 79 - u->safe_bottom, 238, 52}, "TAKE IT RACING", true))
        g->screen = SCREEN_MENU;
    float x = u->width * .58f, y = u->height - 151;
    caps(u, x, y, MUTED, "01 / TRAILBLAZER RALLY");
    const char *names[] = {"SPEED", "HANDLING", "GRIT"};
    float values[] = {.82f, .9f, .94f};
    for (int i = 0; i < 3; i++) {
        caps(u, x + i * 135, y + 35, INK, names[i]);
        rounded(u, (Rect){x + i * 135, y + 64, 105, 4}, 2, rgb(.81, .82, .77));
        rounded(u, (Rect){x + i * 135, y + 64, 105 * values[i], 4}, 2, ORANGE);
    }
}
static void help(UI *u, Game *g) {
    float m = 46 + u->safe_left;
    rect(u, (Rect){0, 81, u->width, u->height - 81}, PAPER);
    bool compact = u->height - u->safe_top - u->safe_bottom < 690;
    caps(u, m, compact ? 96 + u->safe_top : 111, ORANGE, "A LITTLE KNOW-HOW GOES A LONG WAY");
    label(u, 1, compact ? 48 : 69, m, compact ? 123 + u->safe_top : 145, INK, "Ready. Set. Tiny.");
    float y = compact ? 191 + u->safe_top : 249, cw = (u->width - m * 2 - 40) / 3;
    const char *titles[] = {"Find your flow.", "Feed the adventure.", "Take the high road."};
    const char *a[] = {g->profile.auto_accel ? "The gas is automatic."
                                             : "Hold W / UP to accelerate.",
                       "Grab the green fuel cans to keep", "Carry speed into the wooden"};
    char fuel_help[64];
    SDL_snprintf(fuel_help, sizeof fuel_help, "going. Each can restores %d%%.", FUEL_PICKUP_AMOUNT);
    const char *b[] = {"Steer with A / D. Brake with S before", fuel_help,
                       "ramps. Fly over the competition."};
    const char *c[] = {"tight corners to avoid sliding wide.", "Run dry and your race is over.",
                       "Race two laps. Make them count."};
    for (int i = 0; i < 3; i++) {
        float x = m + i * (cw + 20);
        rounded(u, (Rect){x, y, cw, compact ? 179 : 225}, 15, WHITE);
        char n[8];
        SDL_snprintf(n, sizeof n, "0%d", i + 1);
        label(u, 1, compact ? 28 : 45, x + 20, y + 11, ORANGE, n);
        label(u, 1, compact ? 28 : 32, x + 20, y + (compact ? 45 : 77), INK, titles[i]);
        label(u, 0, compact ? 15 : 17, x + 20, y + (compact ? 90 : 129), MUTED, a[i]);
        label(u, 0, compact ? 15 : 17, x + 20, y + (compact ? 113 : 155), MUTED, b[i]);
        label(u, 0, compact ? 15 : 17, x + 20, y + (compact ? 136 : 181), MUTED, c[i]);
    }
    float yy = y + (compact ? 190 : 258);
    if (!compact)
        caps(u, m, yy, INK, "ON A KEYBOARD");
    label(
        u, 0, compact ? 13 : 17, m, yy + (compact ? 0 : 31), MUTED,
        "A / D or arrows: steer    W: gas    S: brake    SPACE: drift    R: recover    ESC: pause");
    label(u, 0, compact ? 13 : 17, m, yy + (compact ? 23 : 61), MUTED,
          "Controller: left stick to steer, triggers for gas / brake, A to drift, Start to pause.");
    if (button(u, (Rect){u->width - 283 - u->safe_right, u->height - 79 - u->safe_bottom, 237, 52},
               "I'M READY", true))
        g->screen = SCREEN_MENU;
}
static void settings(UI *u, Game *g) {
    rect(u, (Rect){0, 81, u->width, u->height - 81}, PAPER);
    float m = 46 + u->safe_left;
    bool compact = u->height - u->safe_top - u->safe_bottom < 690;
    caps(u, m, compact ? 95 + u->safe_top : 112, ORANGE, "MAKE YOURSELF AT HOME");
    label(u, 1, compact ? 46 : 70, m, compact ? 121 + u->safe_top : 145, INK,
          "The little details.");
    float x = m, y = compact ? 185 + u->safe_top : 255, w = u->width - m * 2,
          by = u->height - 77 - u->safe_bottom;
    rounded(u, (Rect){x, y, w, compact ? 64 : 75}, 12, WHITE);
    label(u, 1, 28, x + 22, y + (compact ? 14 : 21), INK, "RIVAL DIFFICULTY");
    const char *levels[] = {"SUNDAY DRIVE", "CLUB RACE", "FULL SEND"};
    for (int i = 0; i < 3; i++) {
        Rect b = {x + w - 559 + i * 179, y + (compact ? 9 : 14), 168, 47};
        bool active = g->profile.difficulty == i;
        rounded(u, b, 8, active ? INK : PAPER);
        center_label(u, 0, 15, b.x + b.w * .5f, b.y + 13, active ? WHITE : INK, levels[i]);
        if (hit(u, b)) {
            g->profile.difficulty = i;
            game_save_profile(g);
            ui_clear_input(u);
        }
    }
    bool *values[] = {&g->profile.auto_accel, &g->profile.sound, &g->profile.music};
    const char *names[] = {"AUTO ACCELERATE", "SOUND EFFECTS", "MUSIC"};
    const char *desc[] = {"Accelerate automatically while you steer.",
                          "Engine, tires, pickups and a perfect landing.",
                          "An original, laid-back electronic soundtrack."};
    for (int i = 0; i < 3; i++) {
        float pitch = compact ? (by - y - 84) / 3 : 72, yy = y + (compact ? 74 : 85) + i * pitch;
        line(u, x, yy + pitch - 3, x + w, yy + pitch - 3, 1, rgb(.84, .85, .80));
        label(u, 1, compact ? 23 : 25, x + 7, yy + 1, INK, names[i]);
        label(u, 0, compact ? 14 : 15, x + 280, yy + 8, MUTED, desc[i]);
        Rect b = {x + w - 77, yy + 2, 64, 30};
        rounded(u, b, 16, *values[i] ? ORANGE : rgb(.76, .78, .74));
        circle(u, b.x + (*values[i] ? 48 : 16), b.y + 15, 11, WHITE);
        if (hit(u, (Rect){x, yy, w, pitch - 3})) {
            *values[i] = !*values[i];
            game_save_profile(g);
            ui_clear_input(u);
        }
    }
    if (button(u, (Rect){m, by, 199, 50}, "RESET DATA", false)) {
        u->reset_failed = false;
        g->screen = SCREEN_RESET_DATA;
    }
    if (g->toast_time > 0)
        label(u, 0, 17, m + 220, by + 15, MUTED, g->toast);
    if (button(u, (Rect){u->width - 245 - u->safe_right, by, 199, 50}, "ALL SET", true))
        g->screen = SCREEN_MENU;
}

static void reset_data(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, PAPER);
    float cx = (u->width + u->safe_left - u->safe_right) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 310) * .5f;
    center_label(u, 1, 54, cx, y, INK, "Reset all saved data?");
    center_label(u, 0, 19, cx, y + 80, INK,
                 "Championship progress, medals, records and race history will be erased.");
    center_label(u, 0, 19, cx, y + 112, MUTED,
                 "Your initials, car color and settings will return to their defaults.");
    center_label(u, 0, 19, cx, y + 159, ORANGE, "This cannot be undone.");
    if (u->reset_failed)
        center_label(u, 0, 17, cx, y + 203, ORANGE,
                     "Could not reset data. Your data is unchanged. Please try again.");
    if (button(u, (Rect){cx - 219, y + 255, 199, 50}, "CANCEL", false))
        g->screen = SCREEN_SETTINGS;
    if (button(u, (Rect){cx + 20, y + 255, 199, 50}, "RESET ALL DATA", true)) {
        u->reset_failed = !game_reset_profile(g);
        if (!u->reset_failed) {
            SDL_strlcpy(g->toast, "Saved data reset.", sizeof g->toast);
            g->toast_time = 3;
        }
    }
}

// Project every detail with the pedal face, around its fixed lower hinge.
static void pedal_detail(UI *u, Rect face, Rect detail, float radius, float travel, float depth,
                         Color color) {
    texture(u, u->white);
    if (u->count + 120 >= UI_MAX_VERTS)
        flush(u);
    int first = u->count;
    rounded(u, detail, radius, color);
    for (int i = first; i < u->count; i++) {
        UIVertex *v = &u->vertices[i];
        float height = (face.y + face.h - v->y) / face.h;
        v->x = face.x + face.w * .5f +
               (v->x - face.x - face.w * .5f) * (1 - height * (.08f + .07f * travel));
        v->y = face.y + face.h - height * face.h * (1 - .23f * travel) + depth;
    }
}

static void touch_pedal(UI *u, Rect target, float travel, bool brake, const char *caption,
                        float hint) {
    float cx = target.x + target.w * .5f;
    float hinge = target.y + target.h - 30;
    Rect face = {cx - (brake ? 59 : 34), hinge - (brake ? 72 : 100),
                 brake ? 118 : 68, brake ? 72 : 100};
    // The start hint follows the pedal itself, without a surrounding button panel.
    if (hint > 0)
        pedal_detail(u, face, (Rect){face.x - 5, face.y - 5, face.w + 10, face.h + 10},
                     13, travel, 0, alpha(ORANGE, hint));
    rounded(u, (Rect){cx - 23, hinge - 3, 46, 14}, 5, alpha(INK, .65f));
    if (brake) {
        rounded(u, (Rect){cx - 10, target.y + 6, 20, 72}, 5, rgb(.24f, .29f, .28f));
        rect(u, (Rect){cx - 6, target.y + 9, 4, 60}, rgb(.49f, .53f, .49f));
    }
    float depth = 7 - 5 * travel;
    pedal_detail(u, face, (Rect){face.x + 3, face.y + 3, face.w, face.h}, 11, travel,
                 11 - 8 * travel, rgba(.04f, .08f, .09f, .28f));
    pedal_detail(u, face, face, 10, travel, depth, rgb(.14f, .19f, .20f));
    pedal_detail(u, face, face, 10, travel, 0, rgb(.68f, .72f, .68f));
    pedal_detail(u, face, (Rect){face.x + 2, face.y + 2, face.w - 4, face.h - 6}, 8, travel, 0,
                 PAPER);
    pedal_detail(u, face, (Rect){face.x + 5, face.y + 5, face.w - 10, face.h - 11}, 6,
                 travel, 0, brake ? rgb(.23f, .28f, .28f) : rgb(.72f, .75f, .70f));
    if (brake) {
        for (int i = 0; i < 4; i++) {
            Rect rib = {face.x + 13, face.y + 13 + i * 11, face.w - 26, 6};
            pedal_detail(u, face, rib, 2, travel, 1, rgb(.10f, .15f, .16f));
            rib.h = 2;
            pedal_detail(u, face, rib, 1, travel, 0, rgb(.40f, .45f, .43f));
        }
    } else {
        for (int i = 0; i < 3; i++) {
            Rect rib = {face.x + 14 + i * 15, face.y + 18, 9, face.h - 36};
            pedal_detail(u, face, rib, 4, travel, 0, rgb(.26f, .32f, .31f));
            rib.w = 2;
            pedal_detail(u, face, rib, 1, travel, 0, rgb(.49f, .55f, .50f));
        }
        for (int i = 0; i < 2; i++) {
            Rect screw = {cx - 3, face.y + 8 + i * (face.h - 22), 6, 6};
            pedal_detail(u, face, screw, 3, travel, 0, rgb(.40f, .46f, .43f));
            screw.y += 2;
            screw.h = 1;
            pedal_detail(u, face, screw, 0, travel, 0, PAPER);
        }
    }
    // Small warm accents echo the cars and menu without recoloring the rubber.
    rounded(u, (Rect){cx - 15, hinge + 9, 30, 3}, 1.5f, alpha(ORANGE, .35f + .65f * travel));
    center_label(u, 1, 20, cx, target.y + target.h - 24, WHITE, caption);
}

static void wheel_arc(UI *u, float cx, float cy, float radius, float width,
                      float from, float to, Color color) {
    int segments = (int)ceilf((to - from) * 32);
    for (int i = 0; i < segments; i++) {
        float a = from + (to - from) * i / segments;
        float b = from + (to - from) * (i + 1) / segments;
        float outer = radius + width * .5f, inner = radius - width * .5f;
        triangle(u, cx + cosf(a) * inner, cy + sinf(a) * inner,
                 cx + cosf(a) * outer, cy + sinf(a) * outer,
                 cx + cosf(b) * outer, cy + sinf(b) * outer, color);
        triangle(u, cx + cosf(a) * inner, cy + sinf(a) * inner,
                 cx + cosf(b) * outer, cy + sinf(b) * outer,
                 cx + cosf(b) * inner, cy + sinf(b) * inner, color);
    }
}

static void touch_wheel(UI *u, Renderer *r, Rect left, Rect right) {
    float cx = (left.x + right.x + right.w) * .5f;
    float bottom = left.y + left.h, cy = bottom + 29;
    float angle = u->steering_travel * WHEEL_MAX_ANGLE;
    // Crop a real round wheel below its upper section. The rim, spokes and
    // orange center marker turn together; the generous touch zones stay fixed.
    flush(u);
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)(left.x * r->width / u->width),
              0,
              (int)((right.x + right.w - left.x) * r->width / u->width),
              (int)((u->height - left.y) * r->height / u->height));
    for (int i = 0; i < 3; i++) {
        float a = -.5f * PI + (i - 1) * 1.12f + angle;
        float x = cx + cosf(a) * 126, y = cy + sinf(a) * 126;
        line(u, cx, cy + 5, x, y + 5, 20, alpha(INK, .4f));
        line(u, cx, cy, x, y, 16, rgb(.28f, .34f, .33f));
        line(u, cx - 2, cy - 2, x - 2, y - 2, 10, rgb(.72f, .75f, .70f));
        line(u, cx, cy, cx + cosf(a) * 102, cy + sinf(a) * 102, 4,
             rgb(.39f, .45f, .42f));
    }
    wheel_arc(u, cx + 2, cy + 6, 130, 32, -PI, PI, alpha(INK, .35f));
    wheel_arc(u, cx, cy, 130, 30, -PI, PI, rgb(.09f, .14f, .15f));
    wheel_arc(u, cx, cy - 1, 130, 24, -PI, PI, rgb(.23f, .29f, .28f));
    wheel_arc(u, cx, cy - 2, 135, 4, -PI, PI, rgb(.43f, .49f, .45f));
    wheel_arc(u, cx, cy, 119, 2, -PI, PI, rgb(.58f, .62f, .56f));
    circle(u, cx, cy + 3, 27, INK);
    circle(u, cx, cy, 25, rgb(.58f, .62f, .56f));
    circle(u, cx, cy, 21, rgb(.23f, .29f, .28f));
    circle(u, cx, cy, 7, ORANGE);
    for (int side = -1; side <= 1; side += 2) {
        float grip = -.5f * PI + side * 1.03f + angle;
        wheel_arc(u, cx, cy, 130, 22, grip - .24f, grip + .24f, rgb(.14f, .20f, .20f));
        for (int i = -2; i <= 2; i++) {
            float a = grip + i * .085f;
            line(u, cx + cosf(a) * 123, cy + sinf(a) * 123,
                 cx + cosf(a) * 137, cy + sinf(a) * 137, 2, rgb(.32f, .38f, .35f));
        }
    }
    float marker = -.5f * PI + angle;
    wheel_arc(u, cx, cy, 130, 26, marker - .042f, marker + .042f, ORANGE);
    wheel_arc(u, cx, cy, 137, 3, marker - .042f, marker + .042f, PAPER);
    flush(u);
    glDisable(GL_SCISSOR_TEST);
}

static void animate_controls(UI *u, const Game *g) {
    Uint64 now = SDL_GetTicksNS();
    float dt = u->control_frame_time ? (float)(now - u->control_frame_time) / 1e9f : 1.f / 60;
    u->control_frame_time = now;
    bool enabled = u->mobile && g->screen == SCREEN_RACE && g->profile.touch &&
                   !ui_input_blocked(u);
    float brake = enabled && u->brake, gas = enabled && u->gas;
    // Fast depression, slightly softer spring return; independent of frame rate.
    u->brake_travel += (brake - u->brake_travel) * (1 - expf(-dt * (brake ? 32 : 19)));
    u->gas_travel += (gas - u->gas_travel) * (1 - expf(-dt * (gas ? 32 : 19)));
    float steer = enabled ? u->wheel_input : 0;
    u->steering_travel += (steer - u->steering_travel) * (1 - expf(-dt * 16));
}
static void mobile_hud_top(UI *u, Game *g) {
    float x = u->safe_left + 24, y = u->safe_top + 16;
    float end = u->width - u->safe_right - 24;
    float cx = (x + end) * .5f;
    char text[100];
    rounded(u, (Rect){x, y, 156, 88}, 14, rgba(.08, .13, .16, .86f));
    label(u, 0, 22, x + 14, y + 7, WHITE, g->mode == MODE_CHAMPIONSHIP ? "OVERALL" : "POSITION");
    SDL_snprintf(text, sizeof text, "%d / 8",
                 g->mode == MODE_CHAMPIONSHIP ? game_championship_rank(g) : g->rank);
    label(u, 1, 40, x + 14, y + 34, WHITE, text);
    rounded(u, (Rect){x + 168, y, 128, 88}, 14, rgba(.08, .13, .16, .86f));
    label(u, 0, 22, x + 182, y + 7, WHITE, "LAP");
    SDL_snprintf(text, sizeof text, "%d / %d", (int)clampf(g->cars[0].lap, 1, RACE_LAPS),
                 RACE_LAPS);
    label(u, 1, 40, x + 182, y + 34, WHITE, text);
    format_time(text, sizeof text, g->time);
    rounded(u, (Rect){cx - 150, y, 300, 88}, 14, rgba(.08, .13, .16, .72f));
    center_label(u, 1, 28, cx, y + 5, WHITE, g->tracks[g->selected].name);
    center_label(u, 0, 30, cx, y + 43, WHITE, text);
    float fuel_x = end - 312;
    float fuel = g->cars[0].fuel;
    Color color = fuel < 25 ? ORANGE : rgb(.49, .87, .61);
    rounded(u, (Rect){fuel_x, y, 224, 88}, 14, rgba(.08, .13, .16, .86f));
    fuel_icon(u, fuel_x + 16, y + 14, 26, color);
    SDL_snprintf(text, sizeof text, "%d%% FUEL", (int)fuel);
    label(u, 1, 30, fuel_x + 58, y + 9, WHITE, text);
    rounded(u, (Rect){fuel_x + 16, y + 60, 192, 12}, 6, rgba(1, 1, 1, .2f));
    if (fuel > 0)
        rounded(u, (Rect){fuel_x + 16, y + 60, 192 * fuel / 100, 12}, 6, color);
    Rect pause = {end - 72, y, 72, 88};
    rounded(u, pause, 14, rgba(.08, .13, .16, .86f));
    rect(u, (Rect){pause.x + 22, y + 28, 9, 32}, WHITE);
    rect(u, (Rect){pause.x + 41, y + 28, 9, 32}, WHITE);
    if (g->screen != SCREEN_PAUSE && hit(u, pause)) {
        g->screen = SCREEN_PAUSE;
        ui_clear_input(u);
    }
    if (g->mode == MODE_CHAMPIONSHIP) {
        SDL_snprintf(text, sizeof text, "STAGE %d / 3 / RACE P%d%s", g->selected + 1, g->rank,
                     g->selected < TRACK_COUNT - 1 ? " / TOP 4 ADVANCE" : "");
        center_label(u, 1, 26, cx, y + 100, WHITE, text);
    }
    rounded(u, (Rect){end - 154, y + 108, 154, 132}, 14, rgba(.08, .13, .16, .48f));
    map(u, &g->tracks[g->selected], (Rect){end - 140, y + 120, 126, 108}, WHITE, 3, g, true);
}
static void hud_notice(UI *u, float cx, float *y, int font, float size, Color color,
                       const char *text, bool panel) {
    // Keep notices in the central lane, clear of the minimap and side controls.
    float max_width = u->width - u->safe_left - u->safe_right - 440;
    float tw = text_width(u, font, size, text);
    if (tw > max_width) {
        size *= max_width / tw;
        tw = max_width;
    }
    float height = size * 1.4f;
    if (panel)
        rounded(u, (Rect){cx - tw * .5f - 24, *y, tw + 48, height}, 12,
                rgba(.08, .14, .16, .78f));
    center_label(u, font, size, cx, *y + (panel ? 5 : 0), color, text);
    *y += height + 12;
}

static void hud(UI *u, Game *g, Renderer *r) {
    float m = 32 + u->safe_left, y = 24 + u->safe_top, w = u->width;
    Car *c = &g->cars[0];
    Track *t = &g->tracks[g->selected];
    char s[100];
    if (u->mobile) {
        mobile_hud_top(u, g);
    } else {
        rounded(u, (Rect){m, y, 153, 91}, 14, rgba(.08, .13, .16, .86f));
        caps(u, m + 17, y + 9, alpha(WHITE, .63f),
             g->mode == MODE_CHAMPIONSHIP ? "OVERALL LIVE" : "POSITION");
        SDL_snprintf(s, sizeof s, "%d",
                     g->mode == MODE_CHAMPIONSHIP ? game_championship_rank(g) : g->rank);
        label(u, 1, 53, m + 17, y + 29, WHITE, s);
        label(u, 0, 20, m + 58, y + 56, alpha(WHITE, .62f), "/ 8");
        rounded(u, (Rect){m + 165, y, 140, 91}, 14, rgba(.08, .13, .16, .72f));
        caps(u, m + 181, y + 9, alpha(WHITE, .63f), "LAP");
        SDL_snprintf(s, sizeof s, "%d / %d", (int)clampf(c->lap, 1, RACE_LAPS), RACE_LAPS);
        label(u, 1, 42, m + 182, y + 36, WHITE, s);
        center_label(u, 1, 23, w * .5f, y, WHITE, t->name);
        format_time(s, sizeof s, g->time);
        center_label(u, 0, 27, w * .5f, y + 36, WHITE, s);
        SDL_snprintf(s, sizeof s, "STAGE %d / %d  |  RACE P%d  |  %d PTS LIVE%s", g->selected + 1,
                     TRACK_COUNT, g->rank, game_championship_points(g, 0),
                     g->selected < TRACK_COUNT - 1 ? "  |  TOP 4 ADVANCE" : "");
        center_label(u, 0, 13, w * .5f, y + 73, WHITE, g->mode == MODE_CHAMPIONSHIP ? s : "ARCADE");
        float fx = w - 329 - u->safe_right;
        rounded(u, (Rect){fx, y, 226, 91}, 14, rgba(.08, .13, .16, .82f));
        fuel_icon(u, fx + 18, y + 19, 23, c->fuel < 25 ? ORANGE : rgb(.52, .92, .65));
        caps(u, fx + 51, y + 12, alpha(WHITE, .68f), "FUEL");
        SDL_snprintf(s, sizeof s, "%03d%%", (int)c->fuel);
        label(u, 1, 29, fx + 140, y + 7, c->fuel < 25 ? ORANGE : WHITE, s);
        rounded(u, (Rect){fx + 19, y + 58, 188, 10}, 5, rgba(1, 1, 1, .16f));
        if (c->fuel > 0)
            rounded(u, (Rect){fx + 19, y + 58, 188 * c->fuel / 100, 10}, 5,
                    c->fuel < 25 ? ORANGE : rgb(.49, .87, .61));
        Rect pause = {w - 88 - u->safe_right, y, 56, 56};
        rounded(u, pause, 14, rgba(.08, .13, .16, .78f));
        rect(u, (Rect){pause.x + 18, pause.y + 17, 6, 22}, WHITE);
        rect(u, (Rect){pause.x + 32, pause.y + 17, 6, 22}, WHITE);
        if (hit(u, pause)) {
            g->screen = SCREEN_PAUSE;
            ui_clear_input(u);
        }
        float my = y + 119;
        rounded(u, (Rect){w - 209 - u->safe_right, my, 177, 173}, 15, rgba(.08, .13, .16, .48f));
        map(u, t, (Rect){w - 190 - u->safe_right, my + 16, 137, 130}, rgba(1, 1, 1, .56f), 2.8f, g,
            true);
        caps(u, w - 181 - u->safe_right, my + 147, alpha(WHITE, .7f), "THE SCENIC ROUTE");
    }
    bool start_hint = game_start_hint_visible(g);
    if (g->screen == SCREEN_COUNTDOWN) {
        float x = w * .5f, cy = u->height * .44f;
        circle(u, x, cy, 72, rgba(.07, .13, .15, .80f));
        float f = g->countdown - floorf(g->countdown);
        ring(u, x, cy, 77, 3, alpha(ORANGE, .7f + .3f * f));
        int n = (int)ceilf(fminf(3, g->countdown));
        SDL_snprintf(s, sizeof s, "%d", n);
        center_label(u, 1, 100, x, cy - 62, WHITE, s);
        center_label(u, 0, 17, x, cy - 113, WHITE, "GET READY TO MAKE A LITTLE TROUBLE");
    } else {
        float cx = (w + u->safe_left - u->safe_right) * .5f;
        float safe_height = u->height - u->safe_top - u->safe_bottom;
        // One cursor reserves space for every visible notice, including GO!'s
        // larger panel. Start below the HUD and the championship status line.
        float notice_y = u->safe_top +
                         (u->mobile ? (g->mode == MODE_CHAMPIONSHIP ? 164 : 116) : 127);
        if (c->fuel < 25 && g->screen == SCREEN_RACE)
            hud_notice(u, cx, &notice_y, 0, u->mobile ? 24 : 17, WHITE,
                       "LOW FUEL - FIND A GREEN CAN", false);
        notice_y = fmaxf(notice_y, u->safe_top + safe_height * .30f);
        if (g->toast_time > 0) {
            float size = strcmp(g->toast, "GO!") == 0 ? 90 : 37;
            hud_notice(u, cx, &notice_y, 1, size, WHITE, g->toast, true);
        } else if (start_hint) {
            rounded(u, (Rect){cx - 205, notice_y, 410, 88}, 14, rgba(.08, .14, .16, .88f));
            center_label(u, 1, 32, cx, notice_y + 9, ORANGE, "STEP ON THE GAS");
            center_label(u, 0, 18, cx, notice_y + 53, WHITE,
                         u->mobile && g->profile.touch ? "Hold the GAS pedal to get going."
                                          : "Hold W / UP or the right trigger.");
            notice_y += 100;
        }
        if (g->wrong_way > 1) {
            notice_y = fmaxf(notice_y, u->safe_top + safe_height * .46f);
            hud_notice(u, cx, &notice_y, 1, 33, ORANGE, "WRONG WAY - TURN AROUND", false);
        }
    }
    // The player marker remains visible while jumping and among the AI pack.
    float px, py;
    if (renderer_project(r, vadd(c->pos, v3(0, 3.7f, 0)), &px, &py)) {
        rounded(u, (Rect){px - 20, py - 30, 40, 22}, 5, WHITE);
        center_label(u, 1, 16, px, py - 29, INK, game_player_name(g));
        triangle(u, px - 5, py - 8, px + 5, py - 8, px, py - 2, WHITE);
    }
    float bottom = u->height - 54 - u->safe_bottom;
    SDL_snprintf(s, sizeof s, "%03d", (int)(c->speed * 3.6f));
    center_label(u, 1, 44, w * .5f, bottom - 53, WHITE, s);
    center_label(u, 0, u->mobile ? 22 : 12, w * .5f, bottom - 1, alpha(WHITE, .75f), "KM / H");
    if (u->mobile && g->profile.touch) {
        Rect l, rr, b, a;
        control_rects(u, &l, &rr, &b, &a);
        float forward_speed = c->velocity.x * sinf(c->yaw) + c->velocity.z * cosf(c->yaw);
        touch_wheel(u, r, l, rr);
        touch_pedal(u, b, u->brake_travel, true, forward_speed < -.1f ? "REV" : "BRAKE", 0);
        touch_pedal(u, a, u->gas_travel, false, g->profile.auto_accel ? "DRIFT" : "GAS",
                    start_hint ? .12f + .06f * sinf(g->clock * 4.f) : 0);
    } else {
        caps(u, m, u->height - 38 - u->safe_bottom, alpha(WHITE, .8f),
             "A / D STEER    SPACE DRIFT    R RECOVER");
    }
    if (u->mobile && g->profile.touch && g->screen == SCREEN_RACE) {
        Rect recover = {u->safe_left + 24, u->height - u->safe_bottom - 240, 176, 72};
        rounded(u, recover, 10, rgba(.08, .14, .17, .48f));
        center_label(u, 1, 28, recover.x + recover.w * .5f, recover.y + 19, WHITE, "RECOVER");
        if (hit(u, recover)) {
            game_reset_car(g, 0);
            ui_clear_input(u);
        }
    }
}
static void pause_menu(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, rgba(.045f, .085f, .105f, .63f));
    float w = 425, h = 408, x = (u->width - w) * .5f, y = (u->height - h) * .5f;
    if (u->mobile) {
        w = 480;
        h = 384;
        x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
        y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - h) * .5f;
    }
    rounded(u, (Rect){x, y, w, h}, 20, PAPER);
    if (!u->mobile)
        center_label(u, 0, 15, x + w * .5f, y + 29, ORANGE, "TAKE A BREATHER");
    center_label(u, 1, u->mobile ? 54 : 62, x + w * .5f, y + (u->mobile ? 24 : 58), INK,
                 "Pit stop.");
    float padding = u->mobile ? 32 : 40;
    if (button(
            u,
            (Rect){x + padding, y + (u->mobile ? 112 : 160), w - padding * 2, u->mobile ? 72 : 58},
            "BACK TO THE RACE", true)) {
        g->screen = g->countdown > 0 ? SCREEN_COUNTDOWN : SCREEN_RACE;
        ui_clear_input(u);
    }
    if (button(
            u,
            (Rect){x + padding, y + (u->mobile ? 196 : 231), w - padding * 2, u->mobile ? 72 : 55},
            "START AGAIN", false)) {
        game_start(g);
        ui_clear_input(u);
    }
    if (button(
            u,
            (Rect){x + padding, y + (u->mobile ? 280 : 299), w - padding * 2, u->mobile ? 72 : 55},
            "CHOOSE A TRACK", false)) {
        g->screen = SCREEN_MENU;
        ui_clear_input(u);
    }
}
static Color celebration_medal_color(int medal) {
    return medal == 3   ? rgb(1, .76f, .30f)
           : medal == 2 ? rgb(.76f, .84f, .91f)
                        : rgb(.91f, .57f, .34f);
}

static void championship_medal(UI *u, float x, float y, float s, float time, int medal) {
    Color gold = celebration_medal_color(medal);
    Color light = rgb(lerpf(gold.r, 1, .45f), lerpf(gold.g, 1, .45f), lerpf(gold.b, 1, .45f));
    Color shade = rgb(gold.r * .6f, gold.g * .6f, gold.b * .6f);
    // Soft concentric light and a slowly turning sunburst behind the medal.
    for (int i = 8; i > 0; i--)
        circle(u, x, y, (73 + i * 9) * s, alpha(gold, .018f));
    for (int i = 0; i < 16; i++) {
        float a = i * PI / 8 + time * .035f;
        triangle(u, x + cosf(a) * 79 * s, y + sinf(a) * 79 * s, x + cosf(a - .028f) * 144 * s,
                 y + sinf(a - .028f) * 144 * s, x + cosf(a + .028f) * 144 * s,
                 y + sinf(a + .028f) * 144 * s, alpha(gold, .10f));
    }
    // Laurel branches, with paired leaves along each curved stem.
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < 9; i++) {
            float a = (.15f + i * .115f) * PI;
            float xx = x + side * sinf(a) * 104 * s, yy = y + cosf(a) * 82 * s;
            float b = a + .115f * PI;
            if (i < 8)
                line(u, xx, yy, x + side * sinf(b) * 104 * s, y + cosf(b) * 82 * s, 2 * s, shade);
            triangle(u, xx, yy, xx + side * 22 * s, yy - 15 * s, xx + side * 16 * s, yy + 4 * s,
                     gold);
            triangle(u, xx, yy, xx - side * 4 * s, yy - 23 * s, xx - side * 13 * s, yy - 12 * s,
                     light);
        }
    }
    triangle(u, x - 40 * s, y - 88 * s, x - 2 * s, y - 88 * s, x + 12 * s, y - 8 * s, ORANGE);
    triangle(u, x + 2 * s, y - 88 * s, x + 40 * s, y - 88 * s, x - 12 * s, y - 8 * s,
             rgb(.25f, .52f, .65f));
    circle(u, x, y + 12 * s, 61 * s, shade);
    circle(u, x, y + 8 * s, 57 * s, gold);
    ring(u, x, y + 8 * s, 48 * s, 3 * s, light);
    char place[8];
    SDL_snprintf(place, sizeof place, "%d", 4 - medal);
    center_label(u, 1, 68 * s, x, y - 37 * s, shade, place);
}

static void championship_celebration(UI *u, Game *g) {
    int rank = game_championship_rank(g);
    int medal = rank <= 3 ? 4 - rank : 3;
    Color gold = celebration_medal_color(medal), cream = rgb(1, .96f, .83f),
          muted = rgb(.65f, .77f, .73f);
    float t = g->celebration_time;
    rect(u, (Rect){0, 0, u->width, u->height}, rgba(.025f, .08f, .085f, .97f));
    float available_h = u->height - u->safe_top - u->safe_bottom;
    // Keep the celebration, copy and stage cards on one vertical rhythm.
    // The fixed-height actions below retain comfortable touch targets.
    float w = 1020, h = fminf(680, available_h - 32), s = h / 680;
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (available_h - h) * .5f, cx = x + w * .5f;
    rounded(u, (Rect){x, y, w, h}, 24, rgb(.11f, .22f, .22f));
    rounded(u, (Rect){x + 1, y + 1, w - 2, h - 2}, 23, rgb(.045f, .125f, .13f));
    // Deterministic confetti stays outside the central copy and never affects race RNG.
    const Color colors[] = {
        {1, .79f, .38f, 1}, {1, .94f, .72f, 1}, {.34f, .72f, .65f, 1}, {.94f, .40f, .22f, 1}};
    for (int i = 0; i < 100; i++) {
        float phase = i * 2.39996f;
        float px = x - 70 + fmodf(i * 137.3f, w + 140) + sinf(t * .7f + phase) * 20;
        float py = y - 20 + fmodf(i * 73.7f + t * (24 + i % 29), h + 40);
        float visibility = clampf((fabsf(px - cx) - 290) / 90, 0, 1);
        float angle = phase + t * (i % 2 ? 1 : -1);
        line(u, px, py, px + cosf(angle) * 9, py + sinf(angle) * 9, 3 + 2 * fabsf(sinf(angle)),
             alpha(colors[i % 4], visibility * .8f));
    }
    center_label(u, 0, 14, cx, y + 24 * s, gold, "THE LITTLE RALLY CLUB / HALL OF FAME");
    float rise = (1 - powf(1 - clampf(t / .8f, 0, 1), 3));
    championship_medal(u, cx, y + (140 + (1 - rise) * 14) * s, s * (.67f + .15f * rise), t, medal);
    float title_y = y + 240 * s;
    float message_y = title_y + 96 * s;
    float story_y = message_y + 54 * s;
    center_label(u, 1, 68 * s, cx, title_y, cream, "Championship won!");
    char message[128];
    SDL_snprintf(message, sizeof message, "P%d OVERALL / %s MEDAL / %d POINTS", rank,
                 medal_name(medal), game_championship_points(g, 0));
    center_label(u, 1, 30 * s, cx, message_y, gold, message);
    center_label(u, 0, 17 * s, cx, story_y, muted,
                 "A podium across the whole championship. You earned it!");
    float cards_y = story_y + 55 * s;
    for (int i = 0; i < TRACK_COUNT; i++) {
        float card_x = x + 42 + i * 316;
        rounded(u, (Rect){card_x, cards_y, 304, 114 * s}, 12, rgb(.085f, .18f, .18f));
        map(u, &g->tracks[i], (Rect){card_x + 18, cards_y + 30 * s, 67, 54 * s}, gold, 2.4f, NULL,
            false);
        label(u, 0, 16 * s, card_x + 102, cards_y + 14 * s, muted, "STAGE CLEARED");
        label(u, 1, 27 * s, card_x + 102, cards_y + 40 * s, cream, g->tracks[i].name);
        line(u, card_x + 104, cards_y + 88 * s, card_x + 109, cards_y + 93 * s, 2, gold);
        line(u, card_x + 109, cards_y + 93 * s, card_x + 118, cards_y + 82 * s, 2, gold);
        label(u, 0, 14 * s, card_x + 128, cards_y + 80 * s, gold, "POINTS COUNTED");
    }
    float by = y + h - 83;
    if (button(u, (Rect){cx - 251, by, 244, 49}, "SEE FINAL STANDINGS", true)) {
        game_continue(g);
        ui_clear_input(u);
    }
    if (button(u, (Rect){cx + 7, by, 244, 49}, "BACK TO MENU", false)) {
        g->championship_celebration = false;
        g->screen = SCREEN_MENU;
        ui_clear_input(u);
    }
    center_label(u, 0, 13, cx, y + h - 25, muted,
                 "Thanks for racing with us. Your next adventure is a starting line away.");
}

static void results(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, rgba(.045f, .085f, .105f, .76f));
    float available_h = u->height - u->safe_top - u->safe_bottom;
    float w = 920, h = fminf(638, available_h - 44);
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (available_h - h) * .5f;
    Car *c = &g->cars[0];
    rounded(u, (Rect){x, y, w, h}, 20, PAPER);
    float m = x + 36;
    checker(u, m, y + 31, 7, ORANGE, INK);
    caps(u, m + 37, y + 30, ORANGE,
         c->dnf ? "THE ADVENTURE HIT EMPTY" : "THE LITTLE RALLY CLUB / RACE COMPLETE");
    const char *title = c->dnf                ? "Out of fuel."
                        : g->finish_rank == 1 ? "Little car. Big win."
                        : g->finish_rank <= 3 ? "That's a podium!"
                                              : "What a little ride.";
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool completed = game_championship_finished(g);
    bool eliminated = championship && g->profile.championship_eliminated;
    int settled = 0;
    for (int i = 0; i < CAR_COUNT; i++)
        settled += g->cars[i].finished || g->cars[i].dnf;
    bool pending = settled < CAR_COUNT;
    bool overall_standings = championship && !pending;
    int overall = game_championship_rank(g);
    if (championship)
        title = eliminated ? "Championship lost."
                : completed ? (overall <= 3 ? "Championship won!" : "Championship lost.")
                : pending ? "Stage results"
                          : "Championship standings";
    label(u, 1, 62, m, y + 64, INK, title);
    if (g->mode == MODE_CHAMPIONSHIP) {
        char progress[160];
        if (eliminated)
            SDL_snprintf(progress, sizeof progress,
                         "Eliminated in stage %d / %s Start a new championship.",
                         g->profile.championship_stages,
                         c->dnf ? "Did not finish." : "A top 4 finish was required.");
        else if (completed)
            SDL_snprintf(progress, sizeof progress, "Overall P%d / %d points / %s%s", overall,
                         game_championship_points(g, 0),
                         overall <= 3 ? medal_name(4 - overall)
                                      : "No medal. Try a new championship.",
                         overall <= 3 ? " MEDAL" : "");
        else if (g->test_mode)
            SDL_strlcpy(progress, "Demo race / Championship progress is not saved.",
                        sizeof progress);
        else
            SDL_snprintf(progress, sizeof progress, "Stage %d / %d: %s / %s", g->selected + 1,
                         TRACK_COUNT, c->dnf ? "DNF, 0 points" : "Race complete",
                         g->championship_scored ? "Points saved. Continue to the next stage."
                                                : "Waiting for rivals to finish.");
        label(u, 0, 17, m, y + 143, MUTED, progress);
    } else if (c->dnf) {
        label(u, 0, 17, m, y + 143, MUTED,
              "Those green cans are your ticket to the finish. Give it another go.");
    } else {
        char best[32] = "--:--", record[100];
        if (g->profile.best[g->selected] > 0)
            format_time(best, sizeof best, g->profile.best[g->selected]);
        int medal = g->profile.medals[g->selected];
        SDL_snprintf(record, sizeof record, "PERSONAL BEST %s  /  %s%s", best, medal_name(medal),
                     medal > 0 ? " MEDAL" : "");
        label(u, 0, 17, m, y + 143, MUTED, record);
    }
    bool compact = h < 600;
    float left = m, top = y + (compact ? 181 : 201);
    rounded(u, (Rect){left, top, 290, h - (compact ? 276 : 296)}, 13, WHITE);
    char s[100];
    SDL_snprintf(s, sizeof s, "%s / %s", game_player_name(g),
                 championship ? (pending ? "LIVE OVERALL" : "OVERALL") : "FINISH");
    caps(u, left + 23, top + 19, MUTED, s);
    SDL_snprintf(s, sizeof s,
                 championship ? "P%d"
                 : c->dnf     ? "DNF"
                              : "P%d",
                 championship ? overall : g->finish_rank);
    label(u, 1, compact ? 60 : 71, left + 21, top + 44, ORANGE, s);
    float stat_y = top + (compact ? 124 : 141), stat_step = compact ? 28 : 32;
    const char *names[] = {"RACE TIME", "BEST LAP", championship ? "STAGE RESULT" : "FUEL CANS",
                           championship ? (pending ? "LIVE POINTS" : "TOTAL POINTS") : "AIR TIME"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) {
            if (c->dnf)
                SDL_strlcpy(s, "--:--", sizeof s);
            else
                format_time(s, sizeof s, c->finish_time);
        } else if (i == 1) {
            if (c->best_lap > 0)
                format_time(s, sizeof s, c->best_lap);
            else
                SDL_strlcpy(s, "--:--", sizeof s);
        } else if (championship && i == 2)
            SDL_snprintf(s, sizeof s, c->dnf ? "DNF" : "P%d", g->finish_rank);
        else if (championship && i == 3)
            SDL_snprintf(s, sizeof s, "%d", game_championship_points(g, 0));
        else if (i == 2)
            SDL_snprintf(s, sizeof s, "%d COLLECTED", c->collected);
        else
            SDL_snprintf(s, sizeof s, "%d JUMPS", c->jumps);
        caps(u, left + 23, stat_y + i * stat_step, MUTED, names[i]);
        label(u, 0, 16, left + 266 - text_width(u, 0, 16, s), stat_y + i * stat_step, INK, s);
    }
    int order[8];
    for (int i = 0; i < 8; i++)
        order[i] = i;
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++) {
            if (overall_standings ? game_championship_precedes(g, order[j], order[i])
                                  : game_car_precedes(g, order[j], order[i])) {
                int z = order[i];
                order[i] = order[j];
                order[j] = z;
            }
        }
    float rx = left + 325, rw = w - 397;
    caps(u, rx, top + 4, MUTED,
         overall_standings ? "OVERALL STANDINGS"
         : championship    ? "STAGE RESULTS"
                           : "THE CLASSIFICATION");
    caps(u, rx + rw - 79, top + 4, MUTED, overall_standings ? "POINTS" : "FINISH");
    float rowh = fminf(36, (h - (compact ? 321 : 350)) / 8);
    for (int k = 0; k < 8; k++) {
        int i = order[k];
        Car *a = &g->cars[i];
        float yy = top + 37 + k * rowh;
        if (i == 0)
            rounded(u, (Rect){rx - 10, yy - 2, rw + 14, rowh}, 6, rgb(.94, .87, .79));
        if (overall_standings || (a->finished && !a->dnf))
            SDL_snprintf(s, sizeof s, "%02d", k + 1);
        else
            SDL_strlcpy(s, "--", sizeof s);
        label(u, 0, 16, rx, yy, INK, s);
        circle(u, rx + 43, yy + 11, 5, a->color);
        label(u, 1, 22, rx + 61, yy - 3, INK, a == c ? game_player_name(g) : a->name);
        if (overall_standings)
            SDL_snprintf(s, sizeof s, "%d", game_championship_points(g, i));
        else if (a->finished)
            format_time(s, sizeof s, a->finish_time);
        else if (a->dnf)
            SDL_strlcpy(s, "DNF", sizeof s);
        else
            SDL_strlcpy(s, "RACING", sizeof s);
        label(u, 0, 15, rx + rw - text_width(u, 0, 15, s), yy, MUTED, s);
    }
    float by = y + h - 75;
    int next = game_next_track(g);
    bool advance = g->mode == MODE_CHAMPIONSHIP && next >= 0;
    bool waiting = championship && !g->championship_scored && !g->test_mode;
    const char *primary = completed ? "BACK TO MENU" : advance ? "NEXT STAGE" : "RACE AGAIN";
    if (waiting) {
        // A status indicator, with no click target or hover response.
        rounded(u, (Rect){m, by, 229, 49}, 10, rgb(.84f, .85f, .81f));
        for (int i = 0; i < 10; i++) {
            float angle = g->clock * 5 + i * 2 * PI / 10;
            line(u, m + 24 + cosf(angle) * 6, by + 24.5f + sinf(angle) * 6,
                 m + 24 + cosf(angle) * 10, by + 24.5f + sinf(angle) * 10, 2,
                 alpha(MUTED, .2f + .08f * i));
        }
        label(u, 1, 20, m + 44, by + 12.5f, MUTED, "WAITING FOR RIVALS");
        SDL_snprintf(s, sizeof s, "%d / %d RESULTS IN", settled, CAR_COUNT);
        label(u, 0, 16, m + 247, by + 15, MUTED, s);
    } else if (button(u, (Rect){m, by, 229, 49}, primary, true)) {
        game_continue(g);
        ui_clear_input(u);
    }
    if (!championship && next >= 0 &&
        button(u, (Rect){m + 247, by, 180, 49}, advance ? "RACE AGAIN" : "NEXT TRACK", false)) {
        if (!advance)
            g->selected = next;
        game_start(g);
        ui_clear_input(u);
    }
    if (g->mode == MODE_ARCADE && button(u, (Rect){m + 445, by, 176, 49}, "TRACK RECORDS", false))
        game_open_records(g);
    if (!completed && (!championship || g->championship_scored || g->test_mode) &&
        button(u, (Rect){x + w - 225, by, 189, 49}, "BACK TO MENU", false)) {
        g->screen = SCREEN_MENU;
        ui_clear_input(u);
    }
}

static void save_warning(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, rgba(.045f, .085f, .105f, .88f));
    float cx = (u->width + u->safe_left - u->safe_right) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 310) * .5f;
    rounded(u, (Rect){cx - 350, y, 700, 310}, 20, PAPER);
    center_label(u, 1, 54, cx, y + 28, INK, "Changes could not be saved.");
    center_label(u, 0, u->mobile ? 24 : 18, cx, y + 111, MUTED,
                 "Your latest changes are only kept for this session.");
    center_label(u, 0, u->mobile ? 24 : 18, cx, y + 142, MUTED,
                 "Retry saving, or continue without saving.");
    if (button(u, (Rect){cx - 286, y + 199, 270, u->mobile ? 72 : 50}, "RETRY SAVE", true)) {
        game_save_profile(g);
        ui_clear_input(u);
    }
    if (button(u, (Rect){cx + 16, y + 199, 270, u->mobile ? 72 : 50}, "CONTINUE UNSAVED", false)) {
        g->save_failure_acknowledged = true;
        ui_clear_input(u);
    }
    if (!u->mobile)
        center_label(u, 0, 14, cx, y + 275, MUTED,
                     "ENTER / A: RETRY SAVE     ESC / B: CONTINUE UNSAVED");
}

static void record_time(char *out, size_t size, float seconds) {
    int ms = (int)lroundf(seconds * 1000);
    SDL_snprintf(out, size, "%d:%02d.%03d", ms / 60000, ms / 1000 % 60, ms % 1000);
}

static void change_initial(Game *g, int step) {
    char *letter = &g->initials[g->initials_cursor];
    *letter = (char)('A' + (*letter - 'A' + step + 26) % 26);
}

bool ui_record_event(UI *u, Game *g, const SDL_Event *event) {
    if (event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        return false;
    SDL_Keycode key = event->type == SDL_EVENT_KEY_DOWN ? event->key.key : 0;
    int pad = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? event->gbutton.button : -1;
    bool left = key == SDLK_LEFT || pad == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    bool right = key == SDLK_RIGHT || pad == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    bool back = key == SDLK_ESCAPE || key == SDLK_AC_BACK || pad == SDL_GAMEPAD_BUTTON_EAST;
    bool confirm = key == SDLK_RETURN || pad == SDL_GAMEPAD_BUTTON_START;
    if (game_save_warning(g) && !(g->screen == SCREEN_RESULTS && g->initials_pending)) {
        if (key == SDLK_F1 || key == SDLK_F2 || key == SDLK_F11)
            return false;
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat)
            return true;
        if (confirm || pad == SDL_GAMEPAD_BUTTON_SOUTH)
            game_save_profile(g);
        else if (back)
            g->save_failure_acknowledged = true;
        ui_clear_input(u);
        return true;
    }
    if (g->screen == SCREEN_RESULTS && g->mode == MODE_CHAMPIONSHIP && !g->championship_scored &&
        !g->test_mode && back)
        return true;
    if (g->screen == SCREEN_RESULTS && g->championship_celebration && !g->initials_pending &&
        (confirm || back || key == SDLK_SPACE || pad == SDL_GAMEPAD_BUTTON_SOUTH)) {
        if (event->type != SDL_EVENT_KEY_DOWN || !event->key.repeat) {
            game_continue(g);
            ui_clear_input(u);
        }
        return true;
    }
    if (g->screen == SCREEN_RESULTS && g->initials_pending) {
        if (key == SDLK_F1 || key == SDLK_F2 || key == SDLK_F11)
            return false;
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat)
            return true;
        if (key >= SDLK_A && key <= SDLK_Z) {
            g->initials[g->initials_cursor] = (char)('A' + key - SDLK_A);
            g->initials_cursor = (g->initials_cursor + 1) % 3;
        } else if (back && g->record_save_failed) {
            g->initials_pending = false;
            g->save_failure_acknowledged = true;
            ui_clear_input(u);
        } else if (left || key == SDLK_BACKSPACE || back)
            g->initials_cursor = (g->initials_cursor + 2) % 3;
        else if (right)
            g->initials_cursor = (g->initials_cursor + 1) % 3;
        else if (key == SDLK_UP || pad == SDL_GAMEPAD_BUTTON_DPAD_UP)
            change_initial(g, 1);
        else if (key == SDLK_DOWN || pad == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            change_initial(g, -1);
        else if (pad == SDL_GAMEPAD_BUTTON_SOUTH && g->initials_cursor < 2)
            g->initials_cursor++;
        else if (confirm || pad == SDL_GAMEPAD_BUTTON_SOUTH) {
            game_submit_initials(g);
            ui_clear_input(u);
        }
        return true;
    }
    if (g->screen == SCREEN_RECORDS) {
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat)
            return true;
        if (back || confirm || pad == SDL_GAMEPAD_BUTTON_SOUTH)
            g->screen = g->records_return;
        else if (key == SDLK_TAB || pad == SDL_GAMEPAD_BUTTON_NORTH) {
            g->record_history = !g->record_history;
            g->record_page = 0;
        } else if (left || right) {
            int count = g->record_history ? g->profile.records[g->selected].history_count
                                          : g->profile.records[g->selected].top_count;
            int last = count ? (count - 1) / (u->mobile ? 4 : 10) : 0;
            g->record_page = (int)clampf(g->record_page + (right ? 1 : -1), 0, last);
        } else if (key >= SDLK_1 && key <= SDLK_3 && g->records_return == SCREEN_MENU) {
            g->selected = (int)(key - SDLK_1);
            g->record_page = 0;
        } else
            return false;
        ui_clear_input(u);
        return true;
    }
    return false;
}

static void initials_entry(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, rgba(.045f, .085f, .105f, .88f));
    float w = 760, h = u->mobile ? 536 : 510;
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - h) * .5f;
    float cx = x + w * .5f;
    rounded(u, (Rect){x, y, w, h}, 20, PAPER);
    center_label(u, 0, u->mobile ? 22 : 16, cx, y + 24, ORANGE, g->tracks[g->selected].name);
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool in_top = false;
    const TrackRecords *table = &g->profile.records[g->selected];
    for (int i = 0; i < table->top_count; i++)
        in_top |= table->top[i].id == g->result_id;
    center_label(u, 1, 54, cx, y + 47, INK,
                 championship    ? "YOUR CHAMPIONSHIP INITIALS"
                 : g->new_record ? "NEW TRACK RECORD!"
                 : in_top        ? "YOU MADE THE TOP 10!"
                                 : "PUT YOUR NAME ON IT.");
    char time[32], summary[100];
    record_time(time, sizeof time, g->cars[0].finish_time);
    if (g->previous_best > 0)
        SDL_snprintf(summary, sizeof summary, "%s   /   %+.3f S VS PREVIOUS BEST", time,
                     roundf(g->cars[0].finish_time * 1000) / 1000 - g->previous_best);
    else
        SDL_snprintf(summary, sizeof summary, "%s   /   YOUR FIRST FINISH", time);
    center_label(u, 0, u->mobile ? 22 : 18, cx, y + 118, g->record_save_failed ? ORANGE : MUTED,
                 u->mobile && g->record_save_failed ? "Could not save. Retry, or continue unsaved."
                 : championship ? "We'll use these initials for your Championship records."
                                : summary);
    center_label(u, 0, u->mobile ? 22 : 15, cx, y + (u->mobile ? 150 : 154), INK,
                 "ENTER YOUR THREE INITIALS");
    for (int i = 0; i < 3; i++) {
        float xx = cx - (u->mobile ? 176 : 148) + i * (u->mobile ? 124 : 104);
        float letter_w = u->mobile ? 104 : 88;
        if (button(u, (Rect){xx, y + (u->mobile ? 184 : 190), letter_w, u->mobile ? 72 : 38}, "+",
                   false)) {
            g->initials_cursor = i;
            change_initial(g, 1);
        }
        Rect letter = {xx, y + (u->mobile ? 260 : 236), letter_w, u->mobile ? 72 : 86};
        rounded(u, letter, 10, g->initials_cursor == i ? ORANGE : WHITE);
        char text[2] = {g->initials[i], 0};
        center_label(u, 1, u->mobile ? 56 : 64, xx + letter_w * .5f, letter.y + 1,
                     g->initials_cursor == i ? WHITE : INK, text);
        if (hit(u, letter))
            g->initials_cursor = i;
        if (button(u, (Rect){xx, y + (u->mobile ? 340 : 330), letter_w, u->mobile ? 72 : 38}, "-",
                   false)) {
            g->initials_cursor = i;
            change_initial(g, -1);
        }
    }
    if (!u->mobile)
        center_label(u, 0, 14, cx, y + 382, MUTED,
                     "TYPE A-Z OR USE ARROWS / D-PAD. ENTER OR A TO CONFIRM.");
    bool failed = g->record_save_failed;
    if (button(u,
               (Rect){cx - (failed ? (u->mobile ? 286 : 226) : 150), y + (u->mobile ? 436 : 414),
                      failed ? (u->mobile ? 270 : 210) : 300, u->mobile ? 72 : 48},
               "SAVE MY INITIALS", true)) {
        game_submit_initials(g);
        ui_clear_input(u);
    }
    if (failed && button(u,
                         (Rect){cx + 16, y + (u->mobile ? 436 : 414), u->mobile ? 270 : 210,
                                u->mobile ? 72 : 48},
                         "CONTINUE UNSAVED", false)) {
        g->initials_pending = false;
        g->save_failure_acknowledged = true;
        ui_clear_input(u);
    }
    if (!u->mobile)
        center_label(u, 0, 13, cx, y + 477, g->record_save_failed ? ORANGE : MUTED,
                     g->record_save_failed
                         ? "Could not save. Retry, or Esc / B to continue unsaved."
                         : "Local records on this device. Your last initials are remembered.");
}

static void records(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, rgba(.045f, .085f, .105f, .85f));
    float w = 1120, h = fminf(680, u->height - u->safe_top - u->safe_bottom - 40);
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - h) * .5f;
    float m = x + 30;
    rounded(u, (Rect){x, y, w, h}, 20, PAPER);
    caps(u, m, y + 20, ORANGE, "THE LOCAL HALL OF FAME / TWO-LAP RACES");
    label(u, 1, 46, m, y + 42, INK, g->tracks[g->selected].name);
    if (g->records_return == SCREEN_MENU) {
        if (button(u, (Rect){x + w - 250, y + 39, 100, 44}, "< TRACK", false)) {
            g->selected = (g->selected + TRACK_COUNT - 1) % TRACK_COUNT;
            g->record_page = 0;
        }
        if (button(u, (Rect){x + w - 140, y + 39, 110, 44}, "TRACK >", false)) {
            g->selected = (g->selected + 1) % TRACK_COUNT;
            g->record_page = 0;
        }
    }
    if (button(u, (Rect){m, y + 105, 155, 42}, "TOP 10", !g->record_history)) {
        g->record_history = false;
        g->record_page = 0;
    }
    if (button(u, (Rect){m + 167, y + 105, 200, 42}, "RECENT HISTORY", g->record_history)) {
        g->record_history = true;
        g->record_page = 0;
    }
    label(u, 0, 14, m + 389, y + 117, MUTED,
          g->record_history ? "Last 30 finishes. NEW BEST marks each record you broke."
                            : "Fastest first. All difficulties and both race modes.");
    TrackRecords *table = &g->profile.records[g->selected];
    const RaceRecord *entries = g->record_history ? table->history : table->top;
    int count = g->record_history ? table->history_count : table->top_count;
    int pages = count ? (count + 9) / 10 : 1;
    g->record_page = (int)clampf(g->record_page, 0, pages - 1);
    const char *heads[] = {g->record_history ? "RUN" : "RANK",
                           "NAME",
                           "RACE TIME",
                           "BEST LAP",
                           "RACE / LEVEL",
                           "DATE",
                           "RECORD"};
    float cols[] = {0, 65, 156, 290, 423, 641, 816};
    for (int i = 0; i < 7; i++)
        caps(u, m + cols[i], y + 166, MUTED, heads[i]);
    float row_h = (h - 272) / 10;
    for (int row = 0; row < 10 && g->record_page * 10 + row < count; row++) {
        int index = g->record_page * 10 + row;
        const RaceRecord *e = &entries[index];
        float yy = y + 194 + row * row_h;
        bool current = g->records_return == SCREEN_RESULTS && e->id == g->result_id;
        rounded(u, (Rect){m - 9, yy - 2, w - 42, row_h - 2}, 5,
                current   ? rgb(.97f, .85f, .73f)
                : row % 2 ? PAPER
                          : WHITE);
        char text[120];
        SDL_snprintf(text, sizeof text, "%02d", g->record_history ? count - index : index + 1);
        label(u, 0, 15, m, yy, MUTED, text);
        label(u, 1, 21, m + cols[1], yy - 3, current ? ORANGE : INK, e->initials);
        record_time(text, sizeof text, e->time);
        label(u, 0, 16, m + cols[2], yy, INK, text);
        SDL_strlcpy(text, "--:--.---", sizeof text);
        if (e->best_lap > 0)
            record_time(text, sizeof text, e->best_lap);
        label(u, 0, 15, m + cols[3], yy, MUTED, text);
        const char *levels[] = {"EASY", "NORMAL", "HARD"};
        if (e->mode < 0 || e->difficulty < 0)
            SDL_strlcpy(text, "LEGACY RECORD", sizeof text);
        else
            SDL_snprintf(text, sizeof text, "%s / %s / P%d",
                         e->mode == MODE_ARCADE ? "ARC" : "CHAMP", levels[e->difficulty], e->rank);
        label(u, 0, 14, m + cols[4], yy, MUTED, text);
        SDL_strlcpy(text, "UNKNOWN", sizeof text);
        time_t date = (time_t)e->date;
        struct tm *local = e->date ? localtime(&date) : NULL;
        if (local)
            strftime(text, sizeof text, "%Y-%m-%d %H:%M", local);
        label(u, 0, 13, m + cols[5], yy + 1, MUTED, text);
        if (e->record) {
            if (e->improvement > 0)
                SDL_snprintf(text, sizeof text, "NEW BEST  -%.3f S", e->improvement);
            else
                SDL_strlcpy(text, e->mode < 0 ? "IMPORTED BEST" : "FIRST RECORD", sizeof text);
            label(u, 0, 13, m + cols[6], yy + 1, ORANGE, text);
        }
    }
    if (!count) {
        center_label(u, 1, 38, x + w * .5f, y + 245, INK, "THE FIRST NAME COULD BE YOURS.");
        center_label(u, 0, 18, x + w * .5f, y + 300, MUTED,
                     "Finish a race on this track to start its record book.");
    }
    float by = y + h - 60;
    if (button(u, (Rect){m, by, 210, 42},
               g->records_return == SCREEN_RESULTS ? "BACK TO RESULTS" : "BACK TO TRACKS", true))
        g->screen = g->records_return;
    label(u, 0, 13, m + 235, by + 13, MUTED, "TAB / Y: SWITCH VIEW   LEFT / RIGHT: PAGE");
    char page[40];
    SDL_snprintf(page, sizeof page, "%d / %d", g->record_page + 1, pages);
    center_label(u, 0, 16, x + w - 145, by + 11, INK, page);
    if (g->record_page > 0 && button(u, (Rect){x + w - 260, by, 64, 42}, "<", false))
        g->record_page--;
    if (g->record_page + 1 < pages && button(u, (Rect){x + w - 94, by, 64, 42}, ">", false))
        g->record_page++;
}

#include "ui_mobile.h"

void ui_draw(UI *u, Game *g, Renderer *r) {
    Screen screen = g->screen;
    animate_controls(u, g);
    mobile_transition(u, g, game_save_warning(g));
    u->hovered = 0;
    u->count = 0;
    u->current_texture = 0;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(u->program);
    glBindVertexArray(u->vao);
    glBindBuffer(GL_ARRAY_BUFFER, u->vbo);
    glUniform2f(glGetUniformLocation(u->program, "uSize"), u->width, u->height);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(u->program, "uTexture"), 0);
    if (ui_input_blocked(u)) {
        // The rotate prompt is modal. Do not draw or hit-test the covered UI.
        ui_clear_input(u);
        rect(u, (Rect){0, 0, u->width, u->height}, INK);
        center_label(u, 1, 72, u->width * .5f, u->height * .43f, WHITE, "Turn to the adventure.");
        center_label(u, 0, 28, u->width * .5f, u->height * .51f, WHITE,
                     "Rotate your phone to landscape.");
        flush(u);
        glDisable(GL_BLEND);
        return;
    }
    if (game_save_warning(g) && !(g->screen == SCREEN_RESULTS && g->initials_pending)) {
        save_warning(u, g);
    } else {
        switch (g->screen) {
        case SCREEN_MENU:
            if (u->mobile) {
                mobile_menu(u, g);
                mobile_header(u, g);
            } else {
                menu(u, g);
                header(u, g);
            }
            break;
        case SCREEN_GARAGE:
            if (u->mobile) {
                mobile_garage(u, g);
                mobile_header(u, g);
            } else {
                garage(u, g);
                header(u, g);
            }
            break;
        case SCREEN_HELP:
            if (u->mobile) {
                mobile_help(u, g);
                mobile_header(u, g);
            } else {
                help(u, g);
                header(u, g);
            }
            break;
        case SCREEN_SETTINGS:
            if (u->mobile) {
                mobile_settings(u, g);
                mobile_header(u, g);
            } else {
                settings(u, g);
                header(u, g);
            }
            break;
        case SCREEN_RESET_DATA:
            reset_data(u, g);
            break;
        case SCREEN_COUNTDOWN:
        case SCREEN_RACE:
            hud(u, g, r);
            break;
        case SCREEN_PAUSE:
            hud(u, g, r);
            pause_menu(u, g);
            break;
        case SCREEN_RECORDS:
            if (u->mobile)
                mobile_records(u, g);
            else
                records(u, g);
            break;
        case SCREEN_RESULTS:
            if (g->initials_pending)
                initials_entry(u, g);
            else if (g->championship_celebration)
                championship_celebration(u, g);
            else if (u->mobile)
                mobile_results(u, g);
            else
                results(u, g);
            break;
        }
    }
    if (u->show_stats) {
        char s[100];
        SDL_snprintf(s, sizeof s, "%.0f FPS | %d DRAWS | %.0fK TRIANGLES", u->fps, r->draw_calls,
                     r->triangles / 1000.f);
        rounded(u, (Rect){u->width * .5f - 164, u->height - 27, 328, 24}, 5, INK);
        center_label(u, 0, 12, u->width * .5f, u->height - 24, WHITE, s);
    }
    flush(u);
    glDisable(GL_BLEND);
    u->click_count = 0;
    if (g->screen != screen)
        ui_clear_input(u);
}
void ui_destroy(UI *u) {
    glDeleteProgram(u->program);
    glDeleteBuffers(1, &u->vbo);
    glDeleteVertexArrays(1, &u->vao);
    glDeleteTextures(1, &u->white);
    for (int i = 0; i < 2; i++)
        glDeleteTextures(1, &u->fonts[i].texture);
}

static void test_finger_event(UI *u, SDL_EventType type, SDL_TouchID touch, SDL_FingerID finger,
                              float x, float y) {
    SDL_Event event = {0};
    event.type = type;
    event.tfinger.touchID = touch;
    event.tfinger.fingerID = finger;
    event.tfinger.x = x / u->width;
    event.tfinger.y = y / u->height;
    ui_event(u, &event, 1280, 576);
}

static void test_mouse_event(UI *u, SDL_EventType type, float x, float y) {
    SDL_Event event = {0};
    event.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    ui_event(u, &event, 1280, 576);
}

#define UI_CHECK(condition, message)                                                               \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL: %s\n", message);                                                \
            failed++;                                                                              \
        }                                                                                          \
    } while (0)

static int test_pointer_clicks(UI *u, Game *g) {
    int failed = 0;
    u->mobile = true;
    Rect pause = {1192, 24, 56, 56};
    g->screen = SCREEN_RACE;
    g->profile.auto_accel = true;
    ui_clear_input(u);

    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 22, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    UI_CHECK(hit(u, pause), "another finger pressing steer does not invalidate a pause tap");
    UI_CHECK(ui_controls(u, g).steer == 0 && u->wheel_pointer != 0,
             "clicking pause leaves the steering finger independently held");
    UI_CHECK(!hit(u, pause), "a click is consumed only once");

    ui_clear_input(u);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 22, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_MOTION, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    UI_CHECK(!hit(u, pause), "dragging a steering finger cannot borrow the pause finger's press");
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 22, 1215, 50);
    UI_CHECK(hit(u, pause), "the original pause finger still completes its own tap");

    ui_clear_input(u);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 22, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 22, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 33, 1190, 496);
    UI_CHECK(hit(u, pause),
             "a queued tap survives other presses and releases before the next frame");

    ui_clear_input(u);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1215, 50);
    ui_clear_input(u);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    UI_CHECK(!hit(u, pause), "a release after a focus or screen reset cannot click");
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_CANCELED, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    UI_CHECK(!hit(u, pause), "a canceled finger cannot click on a later release");

    ui_clear_input(u);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_DOWN, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 80, 496);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_UP, 1215, 50);
    UI_CHECK(hit(u, pause), "touch input does not overwrite a mouse press");
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_DOWN, 1215, 50);
    ui_clear_input(u);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_UP, 1215, 50);
    UI_CHECK(!hit(u, pause), "a mouse release after reset cannot click");

    ui_clear_input(u);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_DOWN, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1190, 496);
    Controls controls = ui_controls(u, g);
    UI_CHECK(controls.steer == 0 && u->wheel_pointer == 1 && controls.drift,
             "touch hover does not move a held mouse control");

    ui_clear_input(u);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 2, 11, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    UI_CHECK(hit(u, pause) && ui_controls(u, g).steer == 0 && u->wheel_pointer != 0,
             "identical finger IDs on different touch devices remain independent");

    ui_clear_input(u);
    for (int i = 0; i < UI_MAX_FINGERS; i++) {
        test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, i, 80, 496);
    }
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, UI_MAX_FINGERS, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, UI_MAX_FINGERS, 1215, 50);
    UI_CHECK(!hit(u, pause), "an untracked excess finger cannot click");
    ui_controls(u, g);
    UI_CHECK(u->wheel_pointer != 0, "excess fingers do not release tracked controls");

    ui_clear_input(u);
    return failed;
}

static int test_layout_input(UI *u, Game *g) {
    int failed = 0;
    u->mobile = false;
    const int sizes[][2] = {{1600, 450}, {3440, 1440}, {2400, 1080}, {1440, 900}};
    for (size_t i = 0; i < SDL_arraysize(sizes); i++) {
        layout(u, sizes[i][0], sizes[i][1], (SDL_FRect){.04f, .03f, .91f, .93f});
        UI_CHECK(u->height - u->safe_top - u->safe_bottom >= 575.99f,
                 "wide windows retain enough safe height for all result rows and buttons");
        UI_CHECK(fabsf(u->width / u->height - (float)sizes[i][0] / sizes[i][1]) < .001f,
                 "wide layouts keep uniform scaling");
    }
    layout(u, 1600, 450, (SDL_FRect){0, 0, 1, 1});
    Rect l, r, b, a;
    control_rects(u, &l, &r, &b, &a);
    g->screen = SCREEN_RACE;
    g->profile.touch = true;
    g->profile.auto_accel = true;
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 1, l.x + 40, l.y + 40);
    UI_CHECK(ui_controls(u, g).steer == 0, "desktop ignores touch steering on a wider canvas");
    queue_click(u, 120, 410, 120, 410);

    layout(u, 720, 1280, (SDL_FRect){0, 0, 1, 1});
    UI_CHECK(ui_input_blocked(u) && u->click_count == 0 && !u->fingers[0].active,
             "rotating to portrait clears held input and pending clicks");
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 2, 120, 410);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 2, 120, 410);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_DOWN, 120, 410);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_UP, 120, 410);
    Controls controls = ui_controls(u, g);
    UI_CHECK(u->click_count == 0 && !u->mouse_down && !u->fingers[0].active,
             "portrait ignores mouse and touch presses");
    UI_CHECK(controls.throttle == 0 && controls.steer == 0 && !controls.drift,
             "portrait blocks automatic throttle and driving input");

    layout(u, 1600, 450, (SDL_FRect){0, 0, 1, 1});
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 2, 120, 410);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_UP, 120, 410);
    UI_CHECK(!ui_input_blocked(u) && u->click_count == 0,
             "releases from portrait cannot click after rotating back");
    u->mobile = true;
    // iPhone landscape sizes in points. Insets are intentionally asymmetric to
    // exercise both notch orientations independently of a device's pixel scale.
    const int phones[][2] = {{874, 402}, {852, 393}, {896, 414}, {956, 440}};
    for (size_t i = 0; i < SDL_arraysize(phones); i++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            float width = phones[i][0], height = phones[i][1];
            layout(u, (int)width, (int)height,
                   (SDL_FRect){(orientation ? 24 : 62) / width, 0, (width - 86) / width,
                               (height - 21) / height});
            float scale = width / u->width;
            control_rects(u, &l, &r, &b, &a);
            UI_CHECK(72 * scale >= 44, "phone menu targets are at least 44 points tall");
            UI_CHECK(l.w * scale >= 84 && l.h * scale >= 80,
                     "phone driving controls provide large thumb targets");
            UI_CHECK(l.x >= u->safe_left && a.x + a.w <= u->width - u->safe_right &&
                         l.y + l.h <= u->height - u->safe_bottom && l.x + l.w < r.x &&
                         r.x + r.w < b.x && b.x + b.w < a.x,
                     "phone controls are separated and stay inside both safe orientations");
            test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 51, l.x + 4, l.y + 4);
            test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 52, a.x + a.w - 4, a.y + a.h - 4);
            controls = ui_controls(u, g);
            UI_CHECK(controls.steer == 0 && controls.drift,
                     "grabbing the wheel edge keeps steering neutral alongside drift");
            float cx = (l.x + r.x + r.w) * .5f, cy = l.y + l.h + 29;
            float start = atan2f(l.y + 4 - cy, l.x + 4 - cx);
            test_finger_event(u, SDL_EVENT_FINGER_MOTION, 1, 51,
                              cx + cosf(start + .225f) * 130,
                              cy + sinf(start + .225f) * 130);
            controls = ui_controls(u, g);
            UI_CHECK(fabsf(controls.steer - .25f) < .001f && controls.drift,
                     "quarter steering is proportional on every phone and notch orientation");
            test_finger_event(u, SDL_EVENT_FINGER_MOTION, 1, 51,
                              cx + cosf(start - 1.2f) * 190,
                              cy + sinf(start - 1.2f) * 190);
            controls = ui_controls(u, g);
            UI_CHECK(controls.steer == -1 && controls.drift,
                     "captured wheel clamps full lock outside its target across the angle seam");
            test_finger_event(u, SDL_EVENT_FINGER_CANCELED, 1, 51, l.x + 4, l.y + 4);
            controls = ui_controls(u, g);
            UI_CHECK(controls.steer == 0 && controls.drift,
                     "enlarged controls retain independent fingers");
            ui_clear_input(u);
            test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_DOWN, cx * 1280 / u->width,
                             (cy - 130) * 576 / u->height);
            UI_CHECK(ui_controls(u, g).steer == 0, "mouse grab also starts neutral");
            u->mouse_x = cx + sinf(.45f) * 130;
            u->mouse_y = cy - cosf(.45f) * 130;
            UI_CHECK(fabsf(ui_controls(u, g).steer - .5f) < .001f,
                     "mouse preview supports proportional wheel rotation");
            ui_clear_input(u);
            test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 51, cx - 130, u->height - 2);
            ui_controls(u, g);
            UI_CHECK(u->wheel_pointer != 0, "wheel can be grabbed at the bottom screen edge");
            ui_clear_input(u);
        }
    }
    u->mobile = false;
    return failed;
}

int ui_tests(void) {
    UI *u = calloc(1, sizeof *u);
    Game *g = calloc(1, sizeof *g);
    if (!u || !g) {
        free(u);
        free(g);
        return 1;
    }
    u->width = 1280;
    u->height = 576;
    g->screen = SCREEN_RACE;
    g->profile.auto_accel = true;
    g->profile.touch = true;
    // A saved touch preference must never enable invisible controls on desktop.
    g->profile.auto_accel = false;
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 90, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 91, 1080, 496);
    test_mouse_event(u, SDL_EVENT_MOUSE_BUTTON_DOWN, 1190, 496);
    Controls desktop = ui_controls(u, g);
    int failed = 0;
    UI_CHECK(desktop.steer == 0 && desktop.brake == 0 && desktop.throttle == 0 &&
                 !desktop.drift && !desktop.reverse && !u->wheel_pointer,
             "desktop ignores wheel and pedal input even when saved touch controls are enabled");
    ui_clear_input(u);
    u->mobile = true;
    g->profile.auto_accel = true;
    SDL_Event e = {0};
    e.type = SDL_EVENT_FINGER_DOWN;
    e.tfinger.fingerID = 11;
    e.tfinger.x = 80.f / 1280;
    e.tfinger.y = 496.f / 576;
    ui_event(u, &e, 1280, 576);
    e.tfinger.fingerID = 22;
    e.tfinger.x = 1190.f / 1280;
    ui_event(u, &e, 1280, 576);
    Controls c = ui_controls(u, g);
    if (c.steer != 0 || !u->wheel_pointer || !c.drift || c.throttle != 1) {
        fprintf(stderr, "FAIL: simultaneous steering and drift\n");
        failed++;
    }
    e.type = SDL_EVENT_FINGER_CANCELED;
    e.tfinger.fingerID = 11;
    ui_event(u, &e, 1280, 576);
    c = ui_controls(u, g);
    if (c.steer != 0 || !c.drift) {
        fprintf(stderr, "FAIL: independent finger cancellation\n");
        failed++;
    }
    e.type = SDL_EVENT_FINGER_UP;
    e.tfinger.fingerID = 22;
    ui_event(u, &e, 1280, 576);
    c = ui_controls(u, g);
    if (c.steer != 0 || c.drift) {
        fprintf(stderr, "FAIL: no stuck touches\n");
        failed++;
    }
    e.type = SDL_EVENT_FINGER_DOWN;
    e.tfinger.fingerID = 33;
    e.tfinger.x = 1080.f / 1280;
    ui_event(u, &e, 1280, 576);
    c = ui_controls(u, g);
    if (c.brake != 1 || c.throttle != 0 || !c.reverse) {
        fprintf(stderr, "FAIL: touch brake requests reverse and overrides automatic throttle\n");
        failed++;
    }
    ui_clear_input(u);
    c = ui_controls(u, g);
    if (c.brake != 0 || c.drift || c.steer != 0 || c.reverse || c.throttle != 1) {
        fprintf(stderr, "FAIL: focus reset clears inputs\n");
        failed++;
    }
    g->profile.auto_accel = false;
    e.tfinger.fingerID = 44;
    e.tfinger.x = 1190.f / 1280;
    ui_event(u, &e, 1280, 576);
    c = ui_controls(u, g);
    if (c.throttle != 1 || c.drift) {
        fprintf(stderr, "FAIL: manual gas button\n");
        failed++;
    }
    e.tfinger.fingerID = 55;
    e.tfinger.x = 1080.f / 1280;
    ui_event(u, &e, 1280, 576);
    c = ui_controls(u, g);
    if (c.throttle != 0 || c.brake != 1 || !c.reverse) {
        fprintf(stderr, "FAIL: manual touch brake requests reverse even with gas held\n");
        failed++;
    }
    g->screen = SCREEN_PAUSE;
    c = ui_controls(u, g);
    if (c.throttle != 0 || c.brake != 0 || c.steer != 0 || c.reverse) {
        fprintf(stderr, "FAIL: paused touch input\n");
        failed++;
    }
    failed += test_pointer_clicks(u, g);
    failed += test_layout_input(u, g);
    printf("Touch input tests: %s (%d failures)\n", failed ? "FAILED" : "PASSED", failed);
    free(u);
    free(g);
    return failed ? 1 : 0;
}
