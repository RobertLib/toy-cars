#include "ui.h"
#include <string.h>
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
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(u->count * sizeof(UIVertex)), u->vertices);
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
static void arrow(UI *u, float x, float y, float s, int direction, Color c) {
    line(u, x - direction * s * .3f, y - s * .45f, x + direction * s * .2f, y, s * .12f, c);
    line(u, x + direction * s * .2f, y, x - direction * s * .3f, y + s * .45f, s * .12f, c);
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
    float size = 24;
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
    float width = w / scale, height = h / scale;
    float left = safe.x * width, top = safe.y * height;
    float right = (1 - safe.x - safe.w) * width;
    float bottom = (1 - safe.y - safe.h) * height;
    if (width != u->width || height != u->height || left != u->safe_left || top != u->safe_top ||
        right != u->safe_right || bottom != u->safe_bottom)
        ui_clear_input(u);
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
}
static void control_rects(UI *u, Rect *left, Rect *right, Rect *brake, Rect *gas) {
    float y = u->height - 126 - u->safe_bottom, x = 36 + u->safe_left;
    *left = (Rect){x, y, 92, 92};
    *right = (Rect){x + 108, y, 92, 92};
    *brake = (Rect){u->width - 240 - u->safe_right, y, 92, 92};
    *gas = (Rect){u->width - 132 - u->safe_right, y, 92, 92};
}
Controls ui_controls(UI *u, const Game *g) {
    Controls c = {0};
    u->left = u->right = u->brake = u->gas = false;
    if (ui_input_blocked(u) || g->screen != SCREEN_RACE)
        return c;
    const bool *keys = SDL_GetKeyboardState(NULL);
    c.steer = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) -
              (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
    c.brake = (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) ? 1 : 0;
    c.drift = keys[SDL_SCANCODE_SPACE];
    c.throttle = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] || g->profile.auto_accel) ? 1 : 0;
    Rect l, r, b, a;
    control_rects(u, &l, &r, &b, &a);
    if (g->profile.touch)
        for (int i = -1; i < UI_MAX_FINGERS; i++) {
            bool active = i < 0 ? u->mouse_down : u->fingers[i].active;
            if (!active)
                continue;
            float x = i < 0 ? u->mouse_x : u->fingers[i].x,
                  y = i < 0 ? u->mouse_y : u->fingers[i].y;
            if (inside(l, x, y))
                u->left = true;
            if (inside(r, x, y))
                u->right = true;
            if (inside(b, x, y))
                u->brake = true;
            if (inside(a, x, y))
                u->gas = true;
        }
    if (u->left || u->right)
        c.steer = (float)u->right - (float)u->left;
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
    if (c.drift)
        c.brake = fmaxf(c.brake, .12f);
    if (c.brake > .8f)
        c.throttle = 0;
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
    float big = tight ? clampf((cards_y - hero - 150) / 1.9f, 40, 60) : compact ? 63 : 91;
    caps(u, m, hero, t->accent,
         compact ? "POCKET-SIZED RACING. BIG ADVENTURE."
                 : "POCKET-SIZED RACING. FULL-SIZED ADVENTURE.");
    label(u, 1, big, m - 2, hero + 28, INK, "Small cars.");
    label(u, 1, big, m - 2, hero + 28 + big * .88f, INK, "Wild places.");
    float yy = hero + 35 + big * 1.9f;
    label(u, 0, 19, m, yy, MUTED, "Take the scenic route. At full speed.");
    if (!compact)
        label(u, 0, 19, m, yy + 26, MUTED, "Three wild worlds. Seven little rivals. You.");
    float by = yy + (compact ? 36 : 76), button_h = tight ? 50 : 58;
    if (button(u, (Rect){m, by, 231, button_h}, "LET'S RACE", true)) {
        game_start(g);
        ui_clear_input(u);
    }
    arrow(u, m + 202, by + button_h * .5f, 17, 1, WHITE);
    if (!compact)
        caps(u, m, by + 70, MUTED, "2 LAPS  /  8 RACERS  /  NO WIFI NEEDED");
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
        caps(u, m, cards_y - 32, INK, "PICK YOUR PLAYGROUND");
        caps(u, u->width - 260 - u->safe_right, cards_y - 32, MUTED, "ALL TRACKS READY TO RACE");
    }
    float cw = (u->width - m - (46 + u->safe_right) - 32) / 3;
    for (int i = 0; i < 3; i++) {
        Track *tr = &g->tracks[i];
        float x = m + i * (cw + 16);
        Rect card = {x, cards_y, cw, card_h};
        bool selected = i == g->selected;
        rounded(u, (Rect){x - 1.5f, cards_y - 1.5f, cw + 3, card_h + 3}, 14,
                selected ? tr->accent : rgb(.84, .85, .80));
        rounded(u, card, 12, WHITE);
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
            profile_save(&g->profile);
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
    const char *a[] = {"Steer left and right. The gas is", "Grab the green fuel cans to keep",
                       "Carry speed into the wooden"};
    const char *b[] = {"automatic. Hold DRIFT through", "going. Each can restores 26%.",
                       "ramps. Fly over the competition."};
    const char *c[] = {"tight corners. BRAKE to slow down.", "Run dry and your race is over.",
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
            profile_save(&g->profile);
        }
    }
    bool *values[] = {&g->profile.auto_accel, &g->profile.touch, &g->profile.sound,
                      &g->profile.music};
    const char *names[] = {"AUTO ACCELERATE", "TOUCH CONTROLS", "SOUND EFFECTS", "MUSIC"};
    const char *desc[] = {"Keep your thumbs on the steering.",
                          "Big, comfortable buttons for little screens.",
                          "Engine, tires, pickups and a perfect landing.",
                          "An original, laid-back electronic soundtrack."};
    for (int i = 0; i < 4; i++) {
        float pitch = compact ? (by - y - 84) / 4 : 72, yy = y + (compact ? 74 : 85) + i * pitch;
        line(u, x, yy + pitch - 3, x + w, yy + pitch - 3, 1, rgb(.84, .85, .80));
        label(u, 1, compact ? 23 : 25, x + 7, yy + 1, INK, names[i]);
        label(u, 0, compact ? 14 : 15, x + 280, yy + 8, MUTED, desc[i]);
        Rect b = {x + w - 77, yy + 2, 64, 30};
        rounded(u, b, 16, *values[i] ? ORANGE : rgb(.76, .78, .74));
        circle(u, b.x + (*values[i] ? 48 : 16), b.y + 15, 11, WHITE);
        if (hit(u, (Rect){x, yy, w, pitch - 3})) {
            *values[i] = !*values[i];
            profile_save(&g->profile);
        }
    }
    if (button(u, (Rect){u->width - 245 - u->safe_right, by, 199, 50}, "ALL SET", true))
        g->screen = SCREEN_MENU;
}

static void touch_button(UI *u, Rect r, bool active, const char *s, int direction) {
    Color c = active ? alpha(WHITE, .92f) : rgba(.08, .14, .17, .42f);
    rounded(u, r, 25, c);
    Color fg = active ? INK : WHITE;
    if (direction)
        arrow(u, r.x + r.w * .5f, r.y + r.h * .5f, 36, direction, fg);
    else
        center_label(u, 1, 25, r.x + r.w * .5f, r.y + 28, fg, s);
}
static void hud(UI *u, Game *g, Renderer *r) {
    float m = 32 + u->safe_left, y = 24 + u->safe_top, w = u->width;
    Car *c = &g->cars[0];
    Track *t = &g->tracks[g->selected];
    char s[100];
    rounded(u, (Rect){m, y, 153, 91}, 14, rgba(.08, .13, .16, .86f));
    caps(u, m + 17, y + 9, alpha(WHITE, .63f), "POSITION");
    SDL_snprintf(s, sizeof s, "%d", g->rank);
    label(u, 1, 53, m + 17, y + 29, WHITE, s);
    label(u, 0, 20, m + 58, y + 56, alpha(WHITE, .62f), "/ 8");
    rounded(u, (Rect){m + 165, y, 140, 91}, 14, rgba(.08, .13, .16, .72f));
    caps(u, m + 181, y + 9, alpha(WHITE, .63f), "LAP");
    SDL_snprintf(s, sizeof s, "%d / %d", (int)clampf(c->lap, 1, RACE_LAPS), RACE_LAPS);
    label(u, 1, 42, m + 182, y + 36, WHITE, s);
    center_label(u, 1, 23, w * .5f, y, WHITE, t->name);
    format_time(s, sizeof s, g->time);
    center_label(u, 0, 27, w * .5f, y + 36, WHITE, s);
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
    if (g->screen == SCREEN_COUNTDOWN) {
        float x = w * .5f, cy = u->height * .44f;
        circle(u, x, cy, 72, rgba(.07, .13, .15, .80f));
        float f = g->countdown - floorf(g->countdown);
        ring(u, x, cy, 77, 3, alpha(ORANGE, .7f + .3f * f));
        int n = (int)ceilf(fminf(3, g->countdown));
        SDL_snprintf(s, sizeof s, "%d", n);
        center_label(u, 1, 100, x, cy - 62, WHITE, s);
        center_label(u, 0, 17, x, cy + 94, WHITE, "GET READY TO MAKE A LITTLE TROUBLE");
    } else if (g->toast_time > 0) {
        float size = strcmp(g->toast, "GO!") == 0 ? 90 : 37;
        float tw = text_width(u, 1, size, g->toast);
        float yy = u->height * .30f;
        rounded(u, (Rect){w * .5f - tw * .5f - 24, yy - 5, tw + 48, size * 1.4f}, 12,
                rgba(.08, .14, .16, .78f));
        center_label(u, 1, size, w * .5f, yy, WHITE, g->toast);
    }
    if (g->wrong_way > 1)
        center_label(u, 1, 33, w * .5f, u->height * .46f, ORANGE, "WRONG WAY - TURN AROUND");
    if (c->fuel < 25 && g->screen == SCREEN_RACE)
        center_label(u, 0, 17, w * .5f, y + 102, WHITE, "LOW FUEL - FIND A GREEN CAN");
    // The player marker remains visible while jumping and among the AI pack.
    float px, py;
    if (renderer_project(r, vadd(c->pos, v3(0, 3.7f, 0)), &px, &py)) {
        rounded(u, (Rect){px - 20, py - 30, 40, 22}, 5, WHITE);
        center_label(u, 1, 16, px, py - 29, INK, "YOU");
        triangle(u, px - 5, py - 8, px + 5, py - 8, px, py - 2, WHITE);
    }
    float bottom = u->height - 54 - u->safe_bottom;
    SDL_snprintf(s, sizeof s, "%03d", (int)(c->speed * 3.6f));
    center_label(u, 1, 44, w * .5f, bottom - 53, WHITE, s);
    center_label(u, 0, 12, w * .5f, bottom - 1, alpha(WHITE, .75f), "KM / H");
    if (g->profile.touch) {
        Rect l, rr, b, a;
        control_rects(u, &l, &rr, &b, &a);
        touch_button(u, l, u->left, "", -1);
        touch_button(u, rr, u->right, "", 1);
        touch_button(u, b, u->brake, "BRAKE", 0);
        touch_button(u, a, u->gas, g->profile.auto_accel ? "DRIFT" : "GAS", 0);
    } else {
        caps(u, m, u->height - 38 - u->safe_bottom, alpha(WHITE, .8f),
             "A / D STEER    SPACE DRIFT    R RECOVER");
    }
    if (g->profile.touch && g->screen == SCREEN_RACE) {
        Rect recover = {36 + u->safe_left, u->height - 174 - u->safe_bottom, 118, 36};
        rounded(u, recover, 10, rgba(.08, .14, .17, .48f));
        center_label(u, 1, 19, recover.x + 59, recover.y + 5, WHITE, "RECOVER");
        if (hit(u, recover)) {
            game_reset_car(g, 0);
            ui_clear_input(u);
        }
    }
}
static void pause_menu(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, rgba(.045f, .085f, .105f, .63f));
    float w = 425, h = 408, x = (u->width - w) * .5f, y = (u->height - h) * .5f;
    rounded(u, (Rect){x, y, w, h}, 20, PAPER);
    center_label(u, 0, 15, x + w * .5f, y + 29, ORANGE, "TAKE A BREATHER");
    center_label(u, 1, 62, x + w * .5f, y + 58, INK, "Pit stop.");
    if (button(u, (Rect){x + 40, y + 160, w - 80, 58}, "BACK TO THE RACE", true)) {
        g->screen = g->countdown > 0 ? SCREEN_COUNTDOWN : SCREEN_RACE;
        ui_clear_input(u);
    }
    if (button(u, (Rect){x + 40, y + 231, w - 80, 55}, "START AGAIN", false)) {
        game_start(g);
        ui_clear_input(u);
    }
    if (button(u, (Rect){x + 40, y + 299, w - 80, 55}, "CHOOSE A TRACK", false)) {
        g->screen = SCREEN_MENU;
        ui_clear_input(u);
    }
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
    label(u, 1, 62, m, y + 64, INK, title);
    if (c->dnf) {
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
    caps(u, left + 23, top + 19, MUTED, "YOUR FINISH");
    char s[100];
    SDL_snprintf(s, sizeof s, c->dnf ? "DNF" : "P%d", g->finish_rank);
    label(u, 1, compact ? 60 : 71, left + 21, top + 44, ORANGE, s);
    float stat_y = top + (compact ? 124 : 141), stat_step = compact ? 28 : 32;
    const char *names[] = {"RACE TIME", "BEST LAP", "FUEL CANS", "AIR TIME"};
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
        } else if (i == 2)
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
            Car *a = &g->cars[order[i]], *b = &g->cars[order[j]];
            bool swap = (b->finished && !a->finished) ||
                        (b->finished && a->finished && b->finish_time < a->finish_time) ||
                        (!a->finished && !b->finished && b->progress > a->progress);
            if (swap) {
                int z = order[i];
                order[i] = order[j];
                order[j] = z;
            }
        }
    float rx = left + 325, rw = w - 397;
    caps(u, rx, top + 4, MUTED, "THE CLASSIFICATION");
    caps(u, rx + rw - 79, top + 4, MUTED, "FINISH");
    float rowh = fminf(36, (h - (compact ? 321 : 350)) / 8);
    for (int k = 0; k < 8; k++) {
        int i = order[k];
        Car *a = &g->cars[i];
        float yy = top + 37 + k * rowh;
        if (i == 0)
            rounded(u, (Rect){rx - 10, yy - 2, rw + 14, rowh}, 6, rgb(.94, .87, .79));
        SDL_snprintf(s, sizeof s, "%02d", k + 1);
        label(u, 0, 16, rx, yy, INK, s);
        circle(u, rx + 43, yy + 11, 5, a->color);
        label(u, 1, 22, rx + 61, yy - 3, INK, a->name);
        if (a->finished)
            format_time(s, sizeof s, a->finish_time);
        else if (a->dnf)
            SDL_strlcpy(s, "DNF", sizeof s);
        else
            SDL_strlcpy(s, "RACING", sizeof s);
        label(u, 0, 15, rx + rw - text_width(u, 0, 15, s), yy, MUTED, s);
    }
    float by = y + h - 75;
    if (button(u, (Rect){m, by, 229, 49}, "RACE AGAIN", true)) {
        game_start(g);
        ui_clear_input(u);
    }
    if (button(u, (Rect){m + 247, by, 229, 49}, "NEXT ADVENTURE", false)) {
        g->selected = (g->selected + 1) % 3;
        game_start(g);
        ui_clear_input(u);
    }
    if (button(u, (Rect){x + w - 225, by, 189, 49}, "BACK TO MENU", false)) {
        g->screen = SCREEN_MENU;
        ui_clear_input(u);
    }
}

void ui_draw(UI *u, Game *g, Renderer *r) {
    Screen screen = g->screen;
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
    switch (g->screen) {
    case SCREEN_MENU:
        menu(u, g);
        header(u, g);
        break;
    case SCREEN_GARAGE:
        garage(u, g);
        header(u, g);
        break;
    case SCREEN_HELP:
        help(u, g);
        header(u, g);
        break;
    case SCREEN_SETTINGS:
        settings(u, g);
        header(u, g);
        break;
    case SCREEN_COUNTDOWN:
    case SCREEN_RACE:
        hud(u, g, r);
        break;
    case SCREEN_PAUSE:
        hud(u, g, r);
        pause_menu(u, g);
        break;
    case SCREEN_RESULTS:
        results(u, g);
        break;
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
    Rect pause = {1192, 24, 56, 56};
    g->screen = SCREEN_RACE;
    g->profile.auto_accel = true;
    ui_clear_input(u);

    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 22, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    UI_CHECK(hit(u, pause), "another finger pressing steer does not invalidate a pause tap");
    UI_CHECK(ui_controls(u, g).steer == -1,
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
    UI_CHECK(controls.steer == -1 && controls.drift,
             "touch hover does not move a held mouse control");

    ui_clear_input(u);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 11, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 2, 11, 80, 496);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, 11, 1215, 50);
    UI_CHECK(hit(u, pause) && ui_controls(u, g).steer == -1,
             "identical finger IDs on different touch devices remain independent");

    ui_clear_input(u);
    for (int i = 0; i < UI_MAX_FINGERS; i++) {
        test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, i, 80, 496);
    }
    test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, UI_MAX_FINGERS, 1215, 50);
    test_finger_event(u, SDL_EVENT_FINGER_UP, 1, UI_MAX_FINGERS, 1215, 50);
    UI_CHECK(!hit(u, pause), "an untracked excess finger cannot click");
    UI_CHECK(ui_controls(u, g).steer == -1, "excess fingers do not release tracked controls");

    ui_clear_input(u);
    return failed;
}

static int test_layout_input(UI *u, Game *g) {
    int failed = 0;
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
    UI_CHECK(ui_controls(u, g).steer == -1, "touch steering follows the wider canvas");
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
    int failed = 0;
    if (c.steer != -1 || !c.drift || c.throttle != 1) {
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
    if (c.brake != 1 || c.throttle != 0) {
        fprintf(stderr, "FAIL: brake overrides automatic throttle\n");
        failed++;
    }
    ui_clear_input(u);
    c = ui_controls(u, g);
    if (c.brake != 0 || c.drift || c.steer != 0) {
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
    g->screen = SCREEN_PAUSE;
    c = ui_controls(u, g);
    if (c.throttle != 0 || c.brake != 0 || c.steer != 0) {
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
