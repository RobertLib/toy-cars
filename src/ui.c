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
static void arc(UI *u, float cx, float cy, float radius, float width, float from, float to,
                Color color) {
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
static float text_width(UI *u, int font, float size, const char *s) {
    float w = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c >= 32 && c < 128)
            w += u->fonts[font].glyphs[c - 32][6] * size / 72;
    }
    return w;
}
// Every glyph quad spans the same cell height, so leaning the tops gives an even italic.
static void text_run(UI *u, int font, float size, float x, float y, Color color, const char *s,
                     float lean) {
    UIFont *f = &u->fonts[font];
    texture(u, f->texture);
    float scale = size / 72;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c < 32 || c > 127)
            continue;
        float *g = f->glyphs[c - 32];
        float w = g[4] * scale, h = g[5] * scale, top = h * lean;
        if (u->count + 6 >= UI_MAX_VERTS)
            flush(u);
        vertex(u, x + top, y, g[0], g[1], color);
        vertex(u, x + w + top, y, g[2], g[1], color);
        vertex(u, x + w, y + h, g[2], g[3], color);
        vertex(u, x + top, y, g[0], g[1], color);
        vertex(u, x + w, y + h, g[2], g[3], color);
        vertex(u, x, y + h, g[0], g[3], color);
        x += g[6] * scale;
    }
}
static void label(UI *u, int font, float size, float x, float y, Color color, const char *s) {
    text_run(u, font, size, x, y, color, s, 0);
}
static void center_label(UI *u, int font, float size, float x, float y, Color c, const char *s) {
    label(u, font, size, x - text_width(u, font, size, s) * .5f, y, c, s);
}
static void caps(UI *u, float x, float y, Color c, const char *s) {
    label(u, 0, 16, x, y, c, s);
}
// Every page sits over a live broadcast or the dark garage, in a livery-inspired
// style: forward-leaning shapes and italic display type.
static const Color NIGHT = {.03f, .045f, .06f, 1}, LIVE_RED = {1, .22f, .16f, 1};
static const float LEAN = .24f;
static Color tint(Color c, float amount) {
    return rgb(lerpf(c.r, 1, amount), lerpf(c.g, 1, amount), lerpf(c.b, 1, amount));
}
static void gradient(UI *u, Rect r, Color from, Color to, bool vertical) {
    texture(u, u->white);
    if (u->count + 6 >= UI_MAX_VERTS)
        flush(u);
    Color corner_a = vertical ? from : to, corner_b = vertical ? to : from;
    vertex(u, r.x, r.y, 0, 0, from);
    vertex(u, r.x + r.w, r.y, 0, 0, corner_a);
    vertex(u, r.x + r.w, r.y + r.h, 0, 0, to);
    vertex(u, r.x, r.y, 0, 0, from);
    vertex(u, r.x + r.w, r.y + r.h, 0, 0, to);
    vertex(u, r.x, r.y + r.h, 0, 0, corner_b);
}
// A forward-leaning parallelogram that stays inside r, so r remains its hit target.
// Large panels pass a fixed lean; everything else leans in proportion to its height.
static void skew(UI *u, Rect r, float lean, Color c) {
    triangle(u, r.x + lean, r.y, r.x + r.w, r.y, r.x + r.w - lean, r.y + r.h, c);
    triangle(u, r.x + lean, r.y, r.x + r.w - lean, r.y + r.h, r.x, r.y + r.h, c);
}
static void skew_outline(UI *u, Rect r, float lean, float width, Color c) {
    float x[] = {r.x + lean, r.x + r.w, r.x + r.w - lean, r.x};
    float y[] = {r.y, r.y, r.y + r.h, r.y + r.h};
    for (int i = 0; i < 4; i++)
        line(u, x[i], y[i], x[(i + 1) % 4], y[(i + 1) % 4], width, c);
}
static void slant(UI *u, Rect r, Color c) {
    skew(u, r, r.h * LEAN, c);
}
static void slant_outline(UI *u, Rect r, float width, Color c) {
    skew_outline(u, r, r.h * LEAN, width, c);
}
// A dark glass panel for page content.
static void panel(UI *u, Rect r, float lean) {
    skew(u, r, lean, alpha(NIGHT, .72f));
    skew_outline(u, r, lean, 1, alpha(WHITE, .12f));
}
// The leaning left edge's x at height y, so stacked lines can follow it.
static float skew_left(Rect r, float lean, float y) {
    return r.x + lean * (1 - clampf((y - r.y) / r.h, 0, 1));
}
static float slant_left(Rect r, float y) {
    return skew_left(r, r.h * LEAN, y);
}
static void italic(UI *u, int font, float size, float x, float y, Color c, const char *s) {
    text_run(u, font, size, x, y, c, s, LEAN * .8f);
}
// Display italics thickened by a second pass; only for opaque colors.
static void headline(UI *u, float size, float x, float y, Color c, const char *s) {
    text_run(u, 1, size, x, y, c, s, LEAN * .8f);
    text_run(u, 1, size, x + size * .014f, y, c, s, LEAN * .8f);
}
// Centers the capitals of a display italic inside r.
static void italic_center(UI *u, float size, Rect r, Color c, const char *s, bool heavy) {
    float x = r.x + (r.w - text_width(u, 1, size, s)) * .5f - size * .2f * LEAN;
    float y = r.y + r.h * .5f - size * .65f;
    if (heavy)
        headline(u, size, x, y, c, s);
    else
        italic(u, 1, size, x, y, c, s);
}
static void upper(char *out, size_t size, const char *s) {
    size_t i = 0;
    for (; s[i] && i + 1 < size; i++)
        out[i] = (char)SDL_toupper((unsigned char)s[i]);
    out[i] = 0;
}
// A small livery stripe and caps line above a page title.
static void eyebrow(UI *u, float x, float y, float size, Color c, const char *s) {
    slant(u, (Rect){x, y + size * .15f, size * 2, size * .9f}, c);
    label(u, 0, size, x + size * 2.6f, y - size * .13f, c, s);
}
// An uppercase italic title; y is the top of its capitals. Returns their bottom.
static float title(UI *u, float x, float y, float size, const char *s) {
    char text[80];
    upper(text, sizeof text, s);
    headline(u, size, x - size * .05f, y - size * .3f, WHITE, text);
    return y + size * .7f;
}
// Pages over the broadcast dim it, leaving a little motion on the right.
static void page_backdrop(UI *u) {
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .84f));
    gradient(u, (Rect){0, 0, u->width * .65f, u->height}, alpha(NIGHT, .4f), alpha(NIGHT, 0),
             false);
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
static bool styled_button(UI *u, Rect r, const char *s, bool primary, float size) {
    bool hover = inside(r, u->mx, u->my), clicked = hit(u, r);
    if (primary) {
        slant(u, r, hover ? rgb(.97f, .37f, .17f) : ORANGE);
        // A darker lower half gives the paint some depth.
        slant(u, (Rect){r.x, r.y + r.h * .5f, r.w - r.h * LEAN * .5f, r.h * .5f},
              alpha(rgb(.45f, .08f, 0), .22f));
    } else {
        slant(u, r, alpha(NIGHT, hover ? .82f : .62f));
        slant_outline(u, r, 1.5f, alpha(WHITE, hover ? .6f : .32f));
    }
    italic_center(u, size, r, WHITE, s, primary);
    return clicked;
}
static bool button(UI *u, Rect r, const char *s, bool primary) {
    return styled_button(u, r, s, primary, u->mobile ? 30 : 22);
}
// A slanted switch with a sliding block for its knob.
static void toggle(UI *u, Rect r, bool on) {
    slant(u, r, on ? ORANGE : alpha(WHITE, .16f));
    float pad = 4, h = r.h - pad * 2, w = r.h * .95f;
    float x = on ? r.x + r.w - pad - pad * LEAN - w - h * LEAN : r.x + pad + pad * LEAN;
    slant(u, (Rect){x, r.y + pad, w + h * LEAN, h}, WHITE);
}
// A sun or a crescent moon from plain shapes; the moon's bite uses the backdrop.
static void time_icon(UI *u, float x, float y, float s, bool night, Color c, Color bg) {
    if (night) {
        circle(u, x, y, s, c);
        circle(u, x + s * .48f, y - s * .32f, s * .80f, bg);
        return;
    }
    circle(u, x, y, s * .52f, c);
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4;
        line(u, x + cosf(a) * s * .80f, y + sinf(a) * s * .80f, x + cosf(a) * s * 1.12f,
             y + sinf(a) * s * 1.12f, s * .20f, c);
    }
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
// Below the OVERALL panel, mirroring the minimap's gap on the right, so
// steering thumbs never press it.
static Rect recover_rect(UI *u) {
    return (Rect){u->safe_left + 24, u->safe_top + 124, 156, 72};
}
// Thumbs land loosely, so the wheel accepts presses across the lower-left
// corner, including the margin outside the safe area.
static Rect wheel_target(UI *u, Rect left, Rect right) {
    float top = left.y - 88;
    return (Rect){0, top, right.x + right.w + 96, u->height - top};
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
    Rect wheel = wheel_target(u, l, r);
    float wheel_cx = (l.x + r.x + r.w) * .5f, wheel_cy = l.y + l.h + 29;
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

static void lock_icon(UI *u, float x, float y, float s, Color c) {
    arc(u, x + s * .5f, y + s * .42f, s * .27f, s * .12f, PI, 2 * PI, c);
    rounded(u, (Rect){x + s * .1f, y + s * .42f, s * .8f, s * .58f}, s * .1f, c);
}
// A sun or a crescent drawn without a backdrop bite, for translucent panels.
static void sky_icon(UI *u, float x, float y, float s, bool night, Color c) {
    if (night)
        arc(u, x, y, s * .62f, s * .52f, PI * .4f, PI * 1.78f, c);
    else
        time_icon(u, x, y, s, false, c, c);
}
static const char *start_label(const Game *g) {
    if (g->mode == MODE_ARCADE)
        return "RACE NOW";
    if (g->profile.championship_eliminated || g->profile.championship_stages == TRACK_COUNT)
        return "NEW CHAMPIONSHIP";
    return g->profile.championship_stages ? "RACE NEXT STAGE" : "START CHAMPIONSHIP";
}
// Darken the side of the broadcast that carries the menu, plus the header and the cards.
static void menu_backdrop(UI *u, float panel, float cards_y) {
    rect(u, (Rect){0, 0, panel * .45f, u->height}, alpha(NIGHT, .80f));
    gradient(u, (Rect){panel * .45f, 0, panel * .55f, u->height}, alpha(NIGHT, .80f),
             alpha(NIGHT, 0), false);
    gradient(u, (Rect){0, 0, u->width, 170 + u->safe_top}, alpha(NIGHT, .78f), alpha(NIGHT, 0),
             true);
    gradient(u, (Rect){0, cards_y - 120, u->width, 120}, alpha(NIGHT, 0), alpha(NIGHT, .62f),
             true);
    rect(u, (Rect){0, cards_y, u->width, u->height - cards_y}, alpha(NIGHT, .62f));
}
static void mode_switch(UI *u, Game *g, Rect r, float size) {
    const char *names[] = {"CHAMPIONSHIP", "ARCADE"};
    const RaceMode modes[] = {MODE_CHAMPIONSHIP, MODE_ARCADE};
    float split = r.w * .58f;
    for (int i = 0; i < 2; i++) {
        Rect b = i ? (Rect){r.x + split + 6, r.y, r.w - split - 6, r.h}
                   : (Rect){r.x, r.y, split, r.h};
        bool active = g->mode == modes[i], hover = inside(b, u->mx, u->my);
        slant(u, b, active ? WHITE : alpha(WHITE, hover ? .22f : .12f));
        italic_center(u, size, b, active ? INK : WHITE, names[i], false);
        u->menu_layout.modes[i] = b;
        if (hit(u, b))
            game_set_mode(g, modes[i]);
    }
}
static bool race_button(UI *u, Rect r, const char *text, float size, float time) {
    bool hover = inside(r, u->mx, u->my), clicked = hit(u, r);
    float pulse = .5f + .5f * sinf(time * 3.2f), lean = r.h * LEAN;
    for (int k = 4; k >= 1; k--) {
        float grow = k * (2.5f + 2.5f * pulse);
        slant(u, (Rect){r.x - grow, r.y - grow * .5f, r.w + grow * 2, r.h + grow},
              alpha(ORANGE, .075f));
    }
    slant(u, r, hover ? rgb(.97f, .37f, .17f) : ORANGE);
    // A darker lower half gives the paint some depth.
    slant(u, (Rect){r.x, r.y + r.h * .5f, r.w - lean * .5f, r.h * .5f},
          alpha(rgb(.45f, .08f, 0), .22f));
    float phase = fmodf(time * .42f, 1);
    if (phase < .4f) {
        float band = r.h * .42f, x = r.x + (r.w - band) * phase / .4f;
        slant(u, (Rect){x, r.y, band, r.h}, alpha(WHITE, .22f * sinf(PI * phase / .4f)));
    }
    float chevrons = r.h * .62f;
    Rect face = {r.x + lean * .5f, r.y, r.w - lean - chevrons, r.h};
    italic_center(u, size, face, WHITE, text, true);
    float cy = r.y + r.h * .5f, arm = r.h * .15f;
    for (int i = 0; i < 3; i++) {
        float x = r.x + r.w - lean - chevrons * .78f + i * arm * .95f;
        Color c = alpha(WHITE, .35f + .65f * (.5f + .5f * sinf(time * 6 - i * 1.2f)));
        line(u, x, cy - arm, x + arm * .8f, cy, arm * .42f, c);
        line(u, x + arm * .8f, cy, x, cy + arm, arm * .42f, c);
    }
    return clicked;
}
static bool glass_button(UI *u, Rect r, const char *text, float size) {
    return styled_button(u, r, text, false, size);
}
// Arcade chooses the time of day; a Championship stage only shows its own.
static void time_switch(UI *u, Game *g, Rect r, float size) {
    bool night = game_night(g);
    if (g->mode == MODE_CHAMPIONSHIP) {
        slant(u, r, alpha(NIGHT, .62f));
        sky_icon(u, r.x + r.h * .75f, r.y + r.h * .5f, size * .5f, night, WHITE);
        italic(u, 1, size, r.x + r.h * 1.25f, r.y + r.h * .5f - size * .65f, WHITE,
               night ? "NIGHT STAGE" : "DAY STAGE");
        u->menu_layout.times[0] = u->menu_layout.times[1] = (Rect){0};
        return;
    }
    float w = (r.w - 6) * .5f;
    for (int i = 0; i < 2; i++) {
        Rect b = {r.x + i * (w + 6), r.y, w, r.h};
        bool active = night == (i == 1), hover = inside(b, u->mx, u->my);
        slant(u, b, active ? WHITE : alpha(NIGHT, hover ? .8f : .62f));
        Color fg = active ? INK : WHITE;
        float tw = text_width(u, 1, size, i ? "NIGHT" : "DAY"), icon = size * .5f;
        float x = b.x + (b.w - tw - icon * 3) * .5f;
        sky_icon(u, x + icon, b.y + b.h * .5f, icon, i == 1, fg);
        italic(u, 1, size, x + icon * 3, b.y + b.h * .5f - size * .65f, fg, i ? "NIGHT" : "DAY");
        u->menu_layout.times[i] = b;
        if (hit(u, b) && !active) {
            g->profile.arcade_night = i == 1;
            game_save_profile(g);
            ui_clear_input(u);
        }
    }
}
static void live_badge(UI *u, Game *g, float right, float y, float size) {
    char s[64];
    SDL_snprintf(s, sizeof s, "LIVE  /  %s", g->tracks[g->selected].subtitle);
    float h = size * 2.2f, w = text_width(u, 0, size, s) + h * LEAN * 2 + size * 3;
    Rect r = {right - w, y, w, h};
    slant(u, r, alpha(NIGHT, .62f));
    float dot = r.x + h * LEAN + size * 1.1f;
    circle(u, dot, y + h * .5f, size * .32f, alpha(LIVE_RED, .7f + .3f * sinf(g->clock * 4)));
    label(u, 0, size, dot + size * .9f, y + h * .5f - size * .65f, WHITE, s);
}
// Point out the player's car in the broadcast, while it is clear of the menu.
static void player_tag(UI *u, Game *g, Renderer *r, float left, float top, float bottom,
                       float size) {
    const Mat4 *m = &r->car_models[0];
    Vec3 roof = v3(m->m[12], m->m[13] + 2, m->m[14]);
    float x, y;
    // Up close in the chase shot the car needs no label.
    if (!r->hero || !r->car_visible[0] || vlen(vsub(roof, r->eye)) < 16 ||
        !renderer_project(r, roof, &x, &y))
        return;
    // Skip the label while a nearer car covers the player's.
    Vec3 body = v3(m->m[12], m->m[13] + .8f, m->m[14]);
    float own = vlen(vsub(body, r->eye)), bx, by;
    float focal = u->height * .5f * r->projection.m[5];
    if (!renderer_project(r, body, &bx, &by))
        return;
    for (int i = 1; i < CAR_COUNT; i++) {
        const Mat4 *c = &r->car_models[i];
        Vec3 p = v3(c->m[12], c->m[13] + .8f, c->m[14]);
        float d = vlen(vsub(p, r->eye)), cx, cy;
        if (d < own && renderer_project(r, p, &cx, &cy) &&
            hypotf(cx - bx, cy - by) < 2.4f * focal / d)
            return;
    }
    const char *name = game_player_name(g);
    float h = size * 1.45f, w = text_width(u, 1, size, name) + h * LEAN * 2 + size;
    if (x - w * .5f < left || x + w * .5f > u->width - u->safe_right - 12 || y - h < top ||
        y > bottom)
        return;
    Rect tag = {x - w * .5f, y - h - size * .42f, w, h};
    slant(u, tag, WHITE);
    triangle(u, x - size * .3f, tag.y + h - 1, x + size * .3f, tag.y + h - 1, x,
             tag.y + h + size * .38f, WHITE);
    italic_center(u, size, tag, INK, name, false);
}
static void track_cards(UI *u, Game *g, Rect area, float name_size, float info_size) {
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool fresh =
        g->profile.championship_eliminated || g->profile.championship_stages == TRACK_COUNT;
    float gap = area.h * .14f, cw = (area.w - gap * (TRACK_COUNT - 1)) / TRACK_COUNT;
    for (int i = 0; i < TRACK_COUNT; i++) {
        Track *tr = &g->tracks[i];
        Rect card = {area.x + i * (cw + gap), area.y, cw, area.h};
        bool selected = i == g->selected, open = game_track_unlocked(&g->profile, i);
        bool hover = inside(card, u->mx, u->my);
        Color accent = tint(tr->accent, .38f);
        slant(u, card, alpha(NIGHT, selected ? .9f : hover ? .78f : .6f));
        if (selected)
            slant_outline(u, card, 2.5f, accent);
        else
            slant_outline(u, card, 1, alpha(WHITE, .14f));
        float inset = card.h * .12f, lean = card.h * LEAN;
        Rect stripe = {card.x + lean * inset / card.h + inset * .6f, card.y + inset,
                       card.h * .07f + 2, card.h - inset * 2};
        slant(u, stripe, selected ? accent : alpha(accent, .55f));
        char text[80], name[40];
        upper(name, sizeof name, tr->name);
        float fade = open ? 1 : .55f, pad = inset * .6f + card.h * .07f + inset * 1.2f;
        float y1 = card.y + card.h * .14f, y2 = card.y + card.h * .33f, y3 = card.y + card.h * .67f;
        float name_x = slant_left(card, y2 + name_size * .65f) + pad;
        float name_w = text_width(u, 1, name_size, name);
        SDL_snprintf(text, sizeof text, "0%d  /  %s", i + 1, tr->subtitle);
        label(u, 0, info_size, slant_left(card, y1 + info_size * .6f) + pad, y1,
              alpha(accent, fade), text);
        Color white = alpha(WHITE, open ? 1 : .6f);
        if (selected && open)
            headline(u, name_size, name_x, y2, white, name);
        else
            italic(u, 1, name_size, name_x, y2, white, name);
        float sx = slant_left(card, y3 + info_size * .6f) + pad;
        // Text ends here at most; the layout outline fills the room that remains.
        float room = card.x + card.w - lean * .5f - inset, end = name_x + name_w;
        Color muted = alpha(WHITE, open ? .72f : .55f);
        if (!open) {
            lock_icon(u, sx, y3 + info_size * .12f, info_size * .95f, muted);
            SDL_snprintf(text, sizeof text, "CLEAR STAGE %d TO UNLOCK", i);
            float tx = sx + info_size * 1.4f;
            if (tx + text_width(u, 0, info_size, text) > room - card.h * .7f)
                SDL_strlcpy(text, "LOCKED", sizeof text);
            label(u, 0, info_size, tx, y3, muted, text);
            end = fmaxf(end, tx + text_width(u, 0, info_size, text));
        } else if (championship) {
            bool scored = !fresh && i < g->profile.championship_stages;
            SDL_snprintf(text, sizeof text, "STAGE %d  /  %s", i + 1,
                         scored ? "SCORED" : CHAMPIONSHIP_NIGHT[i] ? "NIGHT RACE" : "DAY RACE");
            label(u, 0, info_size, sx, y3, muted, text);
            end = fmaxf(end, sx + text_width(u, 0, info_size, text));
        } else {
            char best[32] = "--:--";
            if (g->profile.best[i] > 0)
                format_time(best, sizeof best, g->profile.best[i]);
            SDL_snprintf(text, sizeof text, "BEST %s", best);
            label(u, 0, info_size, sx, y3, muted, text);
            end = fmaxf(end, sx + text_width(u, 0, info_size, text));
            int medal = g->profile.medals[i];
            if (medal > 0) {
                float mx = end + info_size * 1.2f;
                circle(u, mx, y3 + info_size * .66f, info_size * .36f,
                       tint(medal_color(medal), .3f));
                label(u, 0, info_size, mx + info_size * .7f, y3, muted, medal_name(medal));
                end = mx + info_size * .7f + text_width(u, 0, info_size, medal_name(medal));
            }
        }
        float map_w = fminf(card.h * .9f, room - end - inset);
        if (map_w > card.h * .45f)
            map(u, tr, (Rect){room - map_w, card.y + inset, map_w, card.h - inset * 2},
                selected ? accent : alpha(WHITE, .32f), card.h * .03f, g, false);
        u->menu_layout.cards[i] = card;
        if (hit(u, card)) {
            g->selected = i;
        }
    }
}
// Big stat readouts under the title: overall standing, or a track's personal best.
static void menu_stats(UI *u, Game *g, float x, float y, float value_size, float label_size) {
    char values[3][32], names[3][24];
    bool open = game_track_unlocked(&g->profile, g->selected);
    if (g->mode == MODE_CHAMPIONSHIP) {
        int stage = g->profile.championship_stages;
        bool fresh = g->profile.championship_eliminated || stage == TRACK_COUNT;
        SDL_snprintf(values[0], 32, "P%d", fresh ? 1 : game_championship_rank(g));
        SDL_snprintf(values[1], 32, "%d", fresh ? 0 : game_championship_points(g, 0));
        SDL_snprintf(values[2], 32, "%d/%d", fresh ? 1 : stage + 1, TRACK_COUNT);
        SDL_strlcpy(names[0], "OVERALL", 24);
        SDL_strlcpy(names[1], "POINTS", 24);
        SDL_strlcpy(names[2], "STAGE", 24);
    } else {
        int track = g->selected;
        SDL_strlcpy(values[0], "--:--", 32);
        if (g->profile.best[track] > 0)
            format_time(values[0], 32, g->profile.best[track]);
        int medal = g->profile.medals[track];
        SDL_strlcpy(values[1], medal ? medal_name(medal) : "--", 32);
        SDL_snprintf(values[2], 32, "%d", RACE_LAPS);
        SDL_strlcpy(names[0], "BEST TIME", 24);
        SDL_strlcpy(names[1], "MEDAL", 24);
        SDL_strlcpy(names[2], "LAPS", 24);
    }
    if (!open) {
        lock_icon(u, x, y + value_size * .05f, value_size * .9f, WHITE);
        headline(u, value_size, x + value_size * 1.2f, y - value_size * .3f, WHITE, "LOCKED");
        char s[80];
        SDL_snprintf(s, sizeof s, "CLEAR %s IN CHAMPIONSHIP", g->tracks[g->selected - 1].name);
        upper(s, sizeof s, s);
        label(u, 0, label_size, x + value_size * 1.2f, y + value_size * .85f, alpha(WHITE, .6f), s);
        return;
    }
    for (int i = 0; i < 3; i++) {
        float w = fmaxf(text_width(u, 1, value_size, values[i]),
                        text_width(u, 0, label_size, names[i]));
        Color c = i == 1 && g->mode == MODE_ARCADE && g->profile.medals[g->selected]
                      ? tint(medal_color(g->profile.medals[g->selected]), .35f)
                      : WHITE;
        headline(u, value_size, x, y - value_size * .3f, c, values[i]);
        label(u, 0, label_size, x, y + value_size * .85f, alpha(WHITE, .6f), names[i]);
        x += w + value_size * .9f;
        if (i < 2)
            line(u, x - value_size * .25f, y - value_size * .05f, x - value_size * .55f,
                 y + value_size * 1.25f, 1.5f, alpha(WHITE, .22f));
    }
}
static void keycap(UI *u, float *x, float y, const char *key, const char *action) {
    float w = text_width(u, 0, 12, key) + 16;
    rounded(u, (Rect){*x, y, w, 22}, 5, alpha(WHITE, .16f));
    label(u, 0, 12, *x + 8, y + 3, WHITE, key);
    label(u, 0, 12, *x + w + 7, y + 3, alpha(WHITE, .55f), action);
    *x += w + 14 + text_width(u, 0, 12, action) + 18;
}

static void header(UI *u, Game *g) {
    float margin = 46 + u->safe_left;
    gradient(u, (Rect){0, 0, u->width, 130 + u->safe_top}, alpha(NIGHT, .7f), alpha(NIGHT, 0),
             true);
    checker(u, margin, 29 + u->safe_top, 8, ORANGE, WHITE);
    headline(u, 34, margin + 34, 17 + u->safe_top, WHITE, "TOYCARS");
    float x = margin + 255, y = 35 + u->safe_top;
    const char *tabs[] = {"RACE", "GARAGE", "HOW TO PLAY"};
    Screen screens[] = {SCREEN_MENU, SCREEN_GARAGE, SCREEN_HELP};
    for (int i = 0; i < 3; i++) {
        float tw = text_width(u, 0, 15, tabs[i]);
        Rect r = {x - 8, y - 9, tw + 16, 39};
        bool current = g->screen == screens[i];
        caps(u, x, y - 7, alpha(WHITE, current || inside(r, u->mx, u->my) ? 1 : .6f), tabs[i]);
        if (current)
            slant(u, (Rect){x - 2, y + 22, tw + 4, 4}, ORANGE);
        if (hit(u, r))
            g->screen = screens[i];
        x += tw + 35;
    }
    Rect settings = {u->width - 87 - u->safe_right, 21 + u->safe_top, 42, 38};
    bool current = g->screen == SCREEN_SETTINGS;
    rounded(u, settings, 10,
            current ? ORANGE : alpha(WHITE, inside(settings, u->mx, u->my) ? .24f : .14f));
    for (int i = 0; i < 3; i++) {
        float yy = settings.y + 12 + i * 7;
        line(u, settings.x + 11, yy, settings.x + 31, yy, 1.5f, WHITE);
        circle(u, settings.x + (i % 2 ? 25 : 17), yy, 2.8f, WHITE);
    }
    if (hit(u, settings))
        g->screen = SCREEN_SETTINGS;
}

static void menu(UI *u, Game *g, Renderer *r) {
    float m = 46 + u->safe_left, right = u->width - 46 - u->safe_right;
    float available = u->height - u->safe_top - u->safe_bottom;
    Track *t = &g->tracks[g->selected];
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool unlocked = game_track_unlocked(&g->profile, g->selected);
    float card_h = available < 600 ? 92 : 108;
    float cards_y = u->height - card_h - (available < 600 ? 22 : 30) - u->safe_bottom;
    // The left column needs about 510 units at full density and 435 when compact.
    float space = cards_y - 40 - (100 + u->safe_top);
    bool compact = space < 515, tight = space < 440;
    float panel = fmaxf(u->width * .58f, m + 640);
    r->hero_frame = .30f;
    menu_backdrop(u, panel, cards_y);
    float y = 100 + u->safe_top;
    mode_switch(u, g, (Rect){m, y, tight ? 300 : 330, tight ? 38 : 42}, tight ? 19 : 21);
    live_badge(u, g, right, y + 4, 14);
    y += tight ? 56 : compact ? 66 : 84;
    char s[128], name[48];
    if (championship)
        SDL_snprintf(s, sizeof s, "STAGE %d OF %d  /  %s", g->selected + 1, TRACK_COUNT,
                     t->subtitle);
    else
        SDL_snprintf(s, sizeof s, "ARCADE  /  %s", t->subtitle);
    Color accent = tint(t->accent, .42f);
    slant(u, (Rect){m, y + 2, 30, 14}, accent);
    label(u, 0, 15, m + 40, y - 2, accent, s);
    y += tight ? 24 : 30;
    float big = tight ? 64 : compact ? 82 : 104;
    upper(name, sizeof name, t->name);
    float width = text_width(u, 1, big, name) + big * .3f, limit = panel - m - 60;
    if (width > limit)
        big *= limit / width;
    headline(u, big, m - big * .05f, y - big * .3f, WHITE, name);
    y += big * .7f + (tight ? 12 : 18);
    label(u, 0, tight ? 17 : 20, m, y, alpha(WHITE, .84f), t->description);
    y += tight ? 38 : compact ? 48 : 62;
    float value = tight ? 30 : compact ? 34 : 38;
    menu_stats(u, g, m, y, value, tight ? 12 : 13);
    y += value * 1.6f + (tight ? 10 : 22);
    if (!tight) {
        const char *rules =
            !unlocked ? "Single races are open on tracks you have reached in Championship."
            : !championship ? "Single races for medals and records. Pick day or night."
            : g->profile.championship_eliminated
                ? "Eliminated last time. A new run starts at stage 1."
            : g->profile.championship_stages == TRACK_COUNT
                ? "Series complete. A new run starts at stage 1."
            : CHAMPIONSHIP_NIGHT[TRACK_COUNT - 1]
                ? "Finish top 4 to advance. Night final: overall top 3 wins a medal."
                : "Finish top 4 to advance. Final: overall top 3 wins a medal.";
        label(u, 0, 15, m, y, alpha(WHITE, .6f), rules);
        y += compact ? 30 : 38;
    }
    float bh = tight ? 62 : compact ? 68 : 78, bw = tight ? 360 : 410;
    Rect start = {m, y, bw, bh};
    u->menu_layout.start = start;
    u->menu_layout.secondary = (Rect){0};
    if (unlocked) {
        if (race_button(u, start, start_label(g), bh * .5f, g->clock)) {
            game_start(g);
            ui_clear_input(u);
        }
        if (!championship) {
            Rect records = {m + bw + 18, y + bh * .14f, 230, bh * .72f};
            u->menu_layout.secondary = records;
            if (glass_button(u, records, "TRACK RECORDS", bh * .3f)) {
                game_open_records(g);
                ui_clear_input(u);
            }
        }
    } else {
        slant(u, start, alpha(NIGHT, .7f));
        slant_outline(u, start, 1.5f, alpha(WHITE, .2f));
        lock_icon(u, m + bh * .55f, y + bh * .3f, bh * .38f, alpha(WHITE, .5f));
        italic_center(u, bh * .36f, (Rect){m + bh * .4f, y, bw - bh * .4f, bh}, alpha(WHITE, .5f),
                      "TRACK LOCKED", false);
        Rect go = {m + bw + 18, y + bh * .14f, 290, bh * .72f};
        u->menu_layout.secondary = go;
        if (glass_button(u, go, championship ? "CURRENT STAGE" : "GO TO CHAMPIONSHIP", bh * .3f))
            game_set_mode(g, MODE_CHAMPIONSHIP);
    }
    float kx = m + 4;
    y += bh + (tight ? 10 : 18);
    if (y + 22 <= cards_y - 40) {
        if (unlocked)
            keycap(u, &kx, y, "ENTER", "RACE");
        keycap(u, &kx, y, "TAB", "MODE");
        keycap(u, &kx, y, "1-3", "TRACK");
        if (!championship)
            keycap(u, &kx, y, "N", "DAY / NIGHT");
    }
    // Track facts and the time of day, over the broadcast above the cards.
    float chip_h = tight ? 38 : 44, chip_y = cards_y - chip_h - (tight ? 34 : 42);
    SDL_snprintf(s, sizeof s, "%.0f M LAP    /    %.0f M CLIMB    /    3 JUMPS", t->length,
                 t->elevation);
    float facts_w = text_width(u, 0, 14, s) + 64;
    Rect facts = {right - facts_w, chip_y, facts_w, chip_h};
    slant(u, facts, alpha(NIGHT, .62f));
    center_label(u, 0, 14, facts.x + facts.w * .5f, chip_y + chip_h * .5f - 9, WHITE, s);
    time_switch(u, g, (Rect){facts.x - 252, chip_y, 240, chip_h}, tight ? 18 : 20);
    caps(u, m, cards_y - 30, alpha(WHITE, .8f),
         championship ? "YOUR CHAMPIONSHIP ROUTE" : "PICK YOUR TRACK");
    SDL_snprintf(s, sizeof s, "%d / %d TRACKS UNLOCKED",
                 g->profile.championship_completed < TRACK_COUNT
                     ? g->profile.championship_completed + 1
                     : TRACK_COUNT,
                 TRACK_COUNT);
    caps(u, right - text_width(u, 0, 16, s), cards_y - 30, alpha(WHITE, .55f), s);
    track_cards(u, g, (Rect){m, cards_y, right - m, card_h}, tight ? 26 : 30, tight ? 12 : 13);
    player_tag(u, g, r, panel * .7f, 150 + u->safe_top, chip_y - 10, 17);
}

static void garage(UI *u, Game *g) {
    float m = 46 + u->safe_left;
    bool compact = u->height - u->safe_top - u->safe_bottom < 690;
    float cy = compact ? u->height - 183 - u->safe_bottom : 431;
    gradient(u, (Rect){0, 0, u->width * .55f, u->height}, alpha(NIGHT, .85f), alpha(NIGHT, 0),
             false);
    float size = compact ? 58 : 82, y = compact ? 98 + u->safe_top : 117;
    eyebrow(u, m, y, 15, tint(ORANGE, .25f), "YOUR RIDE, YOUR RULES");
    y = title(u, m, y + 32, size, "Meet the");
    y = title(u, m, y + size * .2f, size, "Trailblazer.");
    label(u, 0, 18, m, y + 24, alpha(WHITE, .78f), "A little rally car with a lot of heart.");
    if (!compact)
        label(u, 0, 19, m, y + 52, alpha(WHITE, .78f), "Pick a color. Leave your mark.");
    for (int i = 0; i < 6; i++) {
        float x = m + 23 + i * 57;
        if (g->profile.color == i)
            slant_outline(u, (Rect){x - 28, cy - 24, 56, 48}, 2.5f, WHITE);
        slant(u, (Rect){x - 23, cy - 18, 46, 36}, CAR_COLORS[i]);
        if (hit(u, (Rect){x - 27, cy - 27, 54, 54})) {
            g->profile.color = i;
            game_save_profile(g);
            ui_clear_input(u);
        }
    }
    italic(u, 1, 22, m, cy + 38, WHITE, CAR_COLOR_NAMES[g->profile.color]);
    if (race_button(u, (Rect){m, u->height - 92 - u->safe_bottom, 330, 62}, "TAKE IT RACING", 31,
                    g->clock))
        g->screen = SCREEN_MENU;
    float x = u->width * .58f;
    y = u->height - 151;
    caps(u, x, y, alpha(WHITE, .6f), "01 / TRAILBLAZER RALLY");
    const char *names[] = {"SPEED", "HANDLING", "GRIT"};
    float values[] = {.82f, .9f, .94f};
    for (int i = 0; i < 3; i++) {
        caps(u, x + i * 135, y + 35, WHITE, names[i]);
        for (int k = 0; k < 10; k++)
            slant(u, (Rect){x + i * 135 + k * 10.5f, y + 62, 9.5f, 9},
                  k < (int)lroundf(values[i] * 10) ? ORANGE : alpha(WHITE, .16f));
    }
}
static void help(UI *u, Game *g) {
    float m = 46 + u->safe_left;
    page_backdrop(u);
    bool compact = u->height - u->safe_top - u->safe_bottom < 690;
    eyebrow(u, m, compact ? 96 + u->safe_top : 111, 15, tint(ORANGE, .25f),
            "A LITTLE KNOW-HOW GOES A LONG WAY");
    title(u, m, compact ? 127 + u->safe_top : 151, compact ? 46 : 66, "Ready. Set. Tiny.");
    float y = compact ? 191 + u->safe_top : 249, cw = (u->width - m * 2 - 40) / 3;
    float ph = compact ? 179 : 225, lean = 18;
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
        Rect card = {m + i * (cw + 20), y, cw, ph};
        panel(u, card, lean);
        char n[8];
        SDL_snprintf(n, sizeof n, "0%d", i + 1);
        float ny = y + (compact ? 14 : 20), ty = y + (compact ? 52 : 84);
        headline(u, compact ? 30 : 44, skew_left(card, lean, ny + 20) + 18, ny - (compact ? 6 : 9),
                 ORANGE, n);
        italic(u, 1, compact ? 26 : 30, skew_left(card, lean, ty + 15) + 18, ty - 8, WHITE,
               titles[i]);
        const char *lines[] = {a[i], b[i], c[i]};
        float lx = skew_left(card, lean, y + ph) + 20;
        for (int k = 0; k < 3; k++)
            label(u, 0, compact ? 15 : 17, lx, y + (compact ? 90 : 129) + k * (compact ? 23 : 26),
                  alpha(WHITE, .74f), lines[k]);
    }
    float yy = y + ph + (compact ? 14 : 34), kx = m + 4;
    if (!compact) {
        caps(u, m, yy, WHITE, "ON A KEYBOARD");
        yy += 32;
    }
    const char *keys[][2] = {{"A / D", "STEER"},  {"W", "GAS"},        {"S", "BRAKE"},
                             {"SPACE", "DRIFT"}, {"R", "RECOVER"}, {"ESC", "PAUSE"}};
    for (int i = 0; i < 6; i++)
        keycap(u, &kx, yy, keys[i][0], keys[i][1]);
    label(u, 0, compact ? 13 : 15, m, yy + (compact ? 30 : 38), alpha(WHITE, .6f),
          "Controller: left stick to steer, triggers for gas / brake, A to drift, Start to pause.");
    if (button(u, (Rect){u->width - 283 - u->safe_right, u->height - 79 - u->safe_bottom, 237, 52},
               "I'M READY", true))
        g->screen = SCREEN_MENU;
}
static void settings(UI *u, Game *g) {
    page_backdrop(u);
    float m = 46 + u->safe_left;
    bool compact = u->height - u->safe_top - u->safe_bottom < 690;
    eyebrow(u, m, compact ? 95 + u->safe_top : 112, 15, tint(ORANGE, .25f),
            "MAKE YOURSELF AT HOME");
    title(u, m, compact ? 126 + u->safe_top : 152, compact ? 44 : 66, "The little details.");
    float x = m, y = compact ? 185 + u->safe_top : 255, w = u->width - m * 2,
          by = u->height - 77 - u->safe_bottom;
    float row = compact ? 64 : 75;
    panel(u, (Rect){x, y, w, row}, 14);
    italic(u, 1, 28, x + 26, y + row * .5f - 18, WHITE, "RIVAL DIFFICULTY");
    const char *levels[] = {"SUNDAY DRIVE", "CLUB RACE", "FULL SEND"};
    for (int i = 0; i < 3; i++) {
        Rect b = {x + w - 559 + i * 179, y + (compact ? 9 : 14), 168, 47};
        bool active = g->profile.difficulty == i, hover = inside(b, u->mx, u->my);
        slant(u, b, active ? WHITE : alpha(WHITE, hover ? .2f : .1f));
        italic_center(u, 21, b, active ? INK : WHITE, levels[i], false);
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
        Rect line_rect = {x, yy, w, pitch - 3};
        if (inside(line_rect, u->mx, u->my))
            rect(u, (Rect){x, yy - 6, w, pitch - 3}, alpha(WHITE, .04f));
        line(u, x, yy + pitch - 3, x + w, yy + pitch - 3, 1, alpha(WHITE, .1f));
        italic(u, 1, compact ? 23 : 25, x + 7, yy + 1, WHITE, names[i]);
        label(u, 0, compact ? 14 : 15, x + 280, yy + 8, alpha(WHITE, .6f), desc[i]);
        toggle(u, (Rect){x + w - 77, yy + 2, 64, 30}, *values[i]);
        if (hit(u, line_rect)) {
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
        label(u, 0, 17, m + 220, by + 15, alpha(WHITE, .7f), g->toast);
    if (button(u, (Rect){u->width - 245 - u->safe_right, by, 199, 50}, "ALL SET", true))
        g->screen = SCREEN_MENU;
}

static void reset_data(UI *u, Game *g) {
    page_backdrop(u);
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .4f));
    float cx = (u->width + u->safe_left - u->safe_right) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 310) * .5f;
    italic_center(u, 54, (Rect){cx - 500, y, 1000, 66}, WHITE, "RESET ALL SAVED DATA?", true);
    center_label(u, 0, 19, cx, y + 80, WHITE,
                 "Championship progress, medals, records and race history will be erased.");
    center_label(u, 0, 19, cx, y + 112, alpha(WHITE, .6f),
                 "Your initials, car color and settings will return to their defaults.");
    center_label(u, 0, 19, cx, y + 159, tint(ORANGE, .2f), "This cannot be undone.");
    if (u->reset_failed)
        center_label(u, 0, 17, cx, y + 203, tint(ORANGE, .2f),
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
    // A HUD-colored footplate grounds the pedal and keeps its caption readable
    // on snow and sand; it lights up like a primary button while pressed. The
    // pedal itself has no surrounding button panel.
    Color plate = {lerpf(.08f, ORANGE.r, travel), lerpf(.13f, ORANGE.g, travel),
                   lerpf(.16f, ORANGE.b, travel), lerpf(.86f, 1, travel)};
    rounded(u, (Rect){cx - face.w * .5f - 8, hinge + 2, face.w + 16, 30}, 10, plate);
    // The start hint and a press glow follow the tilting face.
    float glow = fmaxf(hint, .75f * travel);
    if (glow > 0)
        pedal_detail(u, face, (Rect){face.x - 7, face.y - 7, face.w + 14, face.h + 14}, 16,
                     travel, 0, alpha(ORANGE, glow));
    if (brake) {
        rounded(u, (Rect){cx - 9, target.y + 4, 18, 74}, 9, INK);
        rounded(u, (Rect){cx - 5, target.y + 8, 10, 68}, 5, rgb(.83f, .82f, .77f));
        rect(u, (Rect){cx - 1.5f, target.y + 11, 3, 62}, PAPER);
    }
    // Cream plastic around dark rubber with an ink outline: it reads against
    // dark night roads and bright snow alike.
    float depth = 7 - 5 * travel;
    Rect outline = {face.x - 2, face.y - 2, face.w + 4, face.h + 4};
    pedal_detail(u, face, (Rect){face.x + 4, face.y + 4, face.w, face.h}, 12, travel,
                 12 - 8 * travel, alpha(INK, .35f));
    pedal_detail(u, face, outline, 12, travel, depth, INK);
    pedal_detail(u, face, outline, 12, travel, 0, INK);
    pedal_detail(u, face, face, 10, travel, 0, PAPER);
    pedal_detail(u, face, (Rect){face.x + 2, face.y + face.h - 5, face.w - 4, 3}, 1, travel, 0,
                 rgb(.80f, .79f, .74f));
    Rect pad = {face.x + 6, face.y + 6, face.w - 12, face.h - 13};
    pedal_detail(u, face, pad, 6, travel, 0, rgb(.17f, .23f, .25f));
    if (brake) {
        for (int i = 0; i < 4; i++) {
            Rect rib = {face.x + 14, face.y + 14 + i * 11, face.w - 28, 6};
            pedal_detail(u, face, rib, 3, travel, 0, rgb(.31f, .38f, .40f));
            rib.h = 2;
            pedal_detail(u, face, rib, 1, travel, 0, rgb(.47f, .54f, .55f));
        }
    } else {
        for (int i = 0; i < 3; i++) {
            Rect rib = {face.x + 14 + i * 15, face.y + 20, 9, face.h - 40};
            pedal_detail(u, face, rib, 4.5f, travel, 0, rgb(.31f, .38f, .40f));
            rib.x += 2;
            rib.w = 2;
            pedal_detail(u, face, rib, 1, travel, 0, rgb(.47f, .54f, .55f));
        }
        for (int i = 0; i < 2; i++) {
            Rect screw = {cx - 3, face.y + 10 + i * (face.h - 27), 6, 6};
            pedal_detail(u, face, screw, 3, travel, 0, rgb(.80f, .79f, .74f));
        }
    }
    center_label(u, 1, 20, cx, hinge + 6, WHITE, caption);
}

static void touch_wheel(UI *u, Renderer *r, Rect left, Rect right) {
    float cx = (left.x + right.x + right.w) * .5f;
    float bottom = left.y + left.h, cy = bottom + 29;
    float angle = u->steering_travel * WHEEL_MAX_ANGLE;
    // Crop a real round wheel below its upper section. The rim, spokes and
    // orange center marker turn together; the generous touch zones stay fixed.
    flush(u);
    glEnable(GL_SCISSOR_TEST);
    float crop = left.x - 16;
    glScissor((int)(crop * r->width / u->width), 0,
              (int)((right.x + right.w + 16 - crop) * r->width / u->width),
              (int)((u->height - left.y) * r->height / u->height));
    // A soft shadow lifts the wheel off grass, sand and snow alike.
    arc(u, cx, cy + 7, 130, 52, -PI, PI, alpha(INK, .12f));
    arc(u, cx, cy + 5, 130, 40, -PI, PI, alpha(INK, .26f));
    for (int i = 0; i < 3; i++) {
        float a = -.5f * PI + (i - 1) * 1.12f + angle;
        float x = cx + cosf(a) * 128, y = cy + sinf(a) * 128;
        line(u, cx, cy + 5, x, y + 5, 24, alpha(INK, .3f));
        line(u, cx, cy, x, y, 22, INK);
        line(u, cx, cy, x, y, 16, rgb(.83f, .82f, .77f));
        line(u, cx, cy, x, y, 6, PAPER);
    }
    // Dark rubber with a cream keyline, the same pairing as the pedals.
    arc(u, cx, cy, 130, 36, -PI, PI, INK);
    arc(u, cx, cy, 130, 28, -PI, PI, rgb(.20f, .27f, .29f));
    for (int side = -1; side <= 1; side += 2) {
        float grip = -.5f * PI + side * 1.03f + angle;
        arc(u, cx, cy, 130, 40, grip - .25f, grip + .25f, INK);
        arc(u, cx, cy, 130, 32, grip - .235f, grip + .235f, rgb(.14f, .20f, .22f));
        for (int i = -3; i <= 3; i++) {
            float a = grip + i * .065f;
            line(u, cx + cosf(a) * 117, cy + sinf(a) * 117, cx + cosf(a) * 143,
                 cy + sinf(a) * 143, 3, rgb(.27f, .34f, .36f));
        }
    }
    // The light stays on top while the wheel turns.
    arc(u, cx, cy, 138, 5, -.86f * PI, -.14f * PI, alpha(WHITE, .14f));
    arc(u, cx, cy, 150, 3, -PI, PI, alpha(PAPER, .92f));
    float marker = -.5f * PI + angle;
    arc(u, cx, cy, 130, 40, marker - .062f, marker + .062f, INK);
    arc(u, cx, cy, 130, 33, marker - .046f, marker + .046f, ORANGE);
    circle(u, cx, cy + 4, 35, alpha(INK, .35f));
    circle(u, cx, cy, 33, INK);
    circle(u, cx, cy, 29, ORANGE);
    arc(u, cx, cy, 22, 4, -.8f * PI, -.2f * PI, alpha(WHITE, .4f));
    flush(u);
    glDisable(GL_SCISSOR_TEST);
}

// A clockwise arrow around a circle: put the car back on the road.
static void recover_icon(UI *u, float x, float y, float s, Color c) {
    float to = 1.4f * PI, head = s * .62f;
    arc(u, x, y, s, s * .4f, -.2f * PI, to, c);
    float ex = x + cosf(to) * s, ey = y + sinf(to) * s;
    float rx = cosf(to), ry = sinf(to), tx = -ry, ty = rx;
    triangle(u, ex + rx * head, ey + ry * head, ex - rx * head, ey - ry * head,
             ex + tx * head * 1.2f, ey + ty * head * 1.2f, c);
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

typedef struct {
    const char *text;
    Rect box;
    float tail, size;
    bool rival;
} Bubble;
// Marker bubbles carry each car's chatter about the road ahead. The player's
// falls back to the name between lines; a rival's appears only while talking.
static bool car_bubble(UI *u, Game *g, Renderer *r, int index, Bubble *b) {
    const Car *c = &g->cars[index];
    bool talking = c->quip_time > 0;
    b->text = talking ? c->quip : index == 0 ? game_player_name(g) : NULL;
    float x, y;
    if (!b->text || !renderer_project(r, vadd(c->pos, v3(0, 3.7f, 0)), &x, &y))
        return false;
    // A new line pops in slightly larger, then settles.
    float age = QUIP_TIME - c->quip_time;
    float pop = talking && age < .25f ? 1 + .18f * sinf(age * 4 * PI) : 1;
    b->rival = index > 0;
    b->size = (talking && u->mobile ? 19 : 16) * pop;
    float dot = b->rival ? b->size * .6f : 0, h = b->size * 1.375f;
    float w = fmaxf(40, text_width(u, 1, b->size, b->text) + b->size + dot);
    b->tail = x;
    b->box = (Rect){clampf(x - w * .5f, 8, u->width - 8 - w), y - 8 - h, w, h};
    return true;
}
static void draw_bubble(UI *u, const Bubble *b, Color speaker, float fade, bool tail) {
    Rect box = b->box;
    rounded(u, box, 5, alpha(WHITE, fade));
    if (tail) {
        float x = clampf(b->tail, box.x + 8, box.x + box.w - 8), y = box.y + box.h;
        triangle(u, x - 5, y, x + 5, y, x, y + 6, alpha(WHITE, fade));
    }
    float dot = 0;
    if (b->rival) {
        // The speaker's paint color tells rivals apart.
        dot = b->size * .6f;
        circle(u, box.x + b->size * .7f, box.y + box.h * .5f, b->size * .2f, alpha(speaker, fade));
    }
    float tw = text_width(u, 1, b->size, b->text);
    label(u, 1, b->size, box.x + dot + (box.w - dot - tw) * .5f, box.y + b->size / 16,
          alpha(INK, fade), b->text);
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
        // larger panel. Start below the HUD.
        float notice_y = u->safe_top + (u->mobile ? 116 : 127);
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
    // Rival lines move above it rather than covering it.
    Bubble mine, bubble;
    bool marker = car_bubble(u, g, r, 0, &mine);
    for (int i = 1; i < CAR_COUNT; i++) {
        if (!car_bubble(u, g, r, i, &bubble))
            continue;
        Rect a = bubble.box, b = mine.box;
        bool covered = marker && a.x < b.x + b.w + 4 && b.x < a.x + a.w + 4 &&
                       a.y < b.y + b.h + 4 && b.y < a.y + a.h + 4;
        if (covered)
            bubble.box.y = b.y - a.h - 6;
        draw_bubble(u, &bubble, g->cars[i].color, fminf(1, g->cars[i].quip_time / .2f),
                    !covered);
    }
    if (marker)
        draw_bubble(u, &mine, WHITE, 1, true);
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
        Rect recover = recover_rect(u);
        rounded(u, recover, 14, rgba(.08, .13, .16, .86f));
        float rx = recover.x + (recover.w - text_width(u, 1, 28, "RECOVER") - 34) * .5f;
        recover_icon(u, rx + 11, recover.y + 36, 10, ORANGE);
        label(u, 1, 28, rx + 34, recover.y + 19, WHITE, "RECOVER");
        if (hit(u, recover)) {
            game_reset_car(g, 0);
            ui_clear_input(u);
        }
    }
}
static void pause_menu(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .66f));
    float w = 425, h = 408, x = (u->width - w) * .5f, y = (u->height - h) * .5f;
    if (u->mobile) {
        w = 480;
        h = 384;
        x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
        y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - h) * .5f;
    }
    skew(u, (Rect){x, y, w, h}, 22, alpha(NIGHT, .94f));
    skew_outline(u, (Rect){x, y, w, h}, 22, 1, alpha(WHITE, .14f));
    skew(u, (Rect){x + 22 - 1.5f, y, w * .4f, 5}, 1.5f, ORANGE);
    if (!u->mobile)
        center_label(u, 0, 15, x + w * .5f, y + 29, tint(ORANGE, .25f), "TAKE A BREATHER");
    Rect heading = {x, y + (u->mobile ? 24 : 58), w, u->mobile ? 66 : 75};
    italic_center(u, u->mobile ? 54 : 62, heading, WHITE, "PIT STOP.", true);
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
          muted = alpha(WHITE, .6f);
    float t = g->celebration_time;
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .97f));
    float available_h = u->height - u->safe_top - u->safe_bottom;
    // Keep the celebration, copy and stage cards on one vertical rhythm.
    // The fixed-height actions below retain comfortable touch targets.
    float w = 1020, h = fminf(680, available_h - 32), s = h / 680;
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (available_h - h) * .5f, cx = x + w * .5f;
    skew(u, (Rect){x, y, w, h}, 26, rgb(.06f, .08f, .10f));
    skew_outline(u, (Rect){x, y, w, h}, 26, 1.5f, alpha(gold, .35f));
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
    italic_center(u, 68 * s, (Rect){x, title_y, w, 82 * s}, cream, "CHAMPIONSHIP WON!", true);
    char message[128];
    SDL_snprintf(message, sizeof message, "P%d OVERALL / %s MEDAL / %d POINTS", rank,
                 medal_name(medal), game_championship_points(g, 0));
    center_label(u, 1, 30 * s, cx, message_y, gold, message);
    center_label(u, 0, 17 * s, cx, story_y, muted,
                 "A podium across the whole championship. You earned it!");
    float cards_y = story_y + 55 * s;
    for (int i = 0; i < TRACK_COUNT; i++) {
        float card_x = x + 42 + i * 316;
        skew(u, (Rect){card_x, cards_y, 304, 114 * s}, 16, rgb(.10f, .125f, .15f));
        map(u, &g->tracks[i], (Rect){card_x + 18, cards_y + 30 * s, 67, 54 * s}, gold, 2.4f, NULL,
            false);
        label(u, 0, 16 * s, card_x + 102, cards_y + 14 * s, muted, "STAGE CLEARED");
        char name[48];
        upper(name, sizeof name, g->tracks[i].name);
        italic(u, 1, 27 * s, card_x + 102, cards_y + 40 * s, cream, name);
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
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .62f));
    float available_h = u->height - u->safe_top - u->safe_bottom;
    float w = 920, h = fminf(638, available_h - 44);
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (available_h - h) * .5f;
    Car *c = &g->cars[0];
    skew(u, (Rect){x, y, w, h}, 22, alpha(NIGHT, .93f));
    skew_outline(u, (Rect){x, y, w, h}, 22, 1, alpha(WHITE, .14f));
    skew(u, (Rect){x + 20.5f, y, w * .34f, 5}, 1.5f, ORANGE);
    float m = x + 36;
    checker(u, m, y + 31, 7, ORANGE, WHITE);
    caps(u, m + 37, y + 30, tint(ORANGE, .25f),
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
    char heading[48];
    upper(heading, sizeof heading, title);
    headline(u, 58, m - 3, y + 66, WHITE, heading);
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
        label(u, 0, 17, m, y + 143, alpha(WHITE, .62f), progress);
    } else if (c->dnf) {
        label(u, 0, 17, m, y + 143, alpha(WHITE, .62f),
              "Those green cans are your ticket to the finish. Give it another go.");
    } else {
        char best[32] = "--:--", record[100];
        if (g->profile.best[g->selected] > 0)
            format_time(best, sizeof best, g->profile.best[g->selected]);
        int medal = g->profile.medals[g->selected];
        SDL_snprintf(record, sizeof record, "PERSONAL BEST %s  /  %s%s", best, medal_name(medal),
                     medal > 0 ? " MEDAL" : "");
        label(u, 0, 17, m, y + 143, alpha(WHITE, .62f), record);
    }
    bool compact = h < 600;
    float left = m, top = y + (compact ? 181 : 201);
    Rect card = {left, top, 290, h - (compact ? 276 : 296)};
    skew(u, card, 14, alpha(WHITE, .06f));
    skew_outline(u, card, 14, 1, alpha(WHITE, .1f));
    char s[100];
    SDL_snprintf(s, sizeof s, "%s / %s", game_player_name(g),
                 championship ? (pending ? "LIVE OVERALL" : "OVERALL") : "FINISH");
    caps(u, left + 23, top + 19, alpha(WHITE, .6f), s);
    SDL_snprintf(s, sizeof s,
                 championship ? "P%d"
                 : c->dnf     ? "DNF"
                              : "P%d",
                 championship ? overall : g->finish_rank);
    headline(u, compact ? 60 : 71, left + 19, top + 44, ORANGE, s);
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
        caps(u, left + 23, stat_y + i * stat_step, alpha(WHITE, .6f), names[i]);
        label(u, 0, 16, left + 266 - text_width(u, 0, 16, s), stat_y + i * stat_step, WHITE, s);
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
    caps(u, rx, top + 4, alpha(WHITE, .55f),
         overall_standings ? "OVERALL STANDINGS"
         : championship    ? "STAGE RESULTS"
                           : "THE CLASSIFICATION");
    caps(u, rx + rw - 79, top + 4, alpha(WHITE, .55f), overall_standings ? "POINTS" : "FINISH");
    float rowh = fminf(36, (h - (compact ? 321 : 350)) / 8);
    for (int k = 0; k < 8; k++) {
        int i = order[k];
        Car *a = &g->cars[i];
        float yy = top + 37 + k * rowh;
        if (i == 0) {
            skew(u, (Rect){rx - 12, yy - 2, rw + 16, rowh}, 8, alpha(ORANGE, .26f));
            skew(u, (Rect){rx - 12, yy - 2, 12, rowh}, 8, ORANGE);
        }
        if (overall_standings || (a->finished && !a->dnf))
            SDL_snprintf(s, sizeof s, "%02d", k + 1);
        else
            SDL_strlcpy(s, "--", sizeof s);
        label(u, 0, 16, rx + 8, yy, WHITE, s);
        slant(u, (Rect){rx + 37, yy + 5, 14, 12}, a->color);
        italic(u, 1, 22, rx + 61, yy - 3, WHITE, a == c ? game_player_name(g) : a->name);
        if (overall_standings)
            SDL_snprintf(s, sizeof s, "%d", game_championship_points(g, i));
        else if (a->finished)
            format_time(s, sizeof s, a->finish_time);
        else if (a->dnf)
            SDL_strlcpy(s, "DNF", sizeof s);
        else
            SDL_strlcpy(s, "RACING", sizeof s);
        label(u, 0, 15, rx + rw - text_width(u, 0, 15, s), yy, alpha(WHITE, .72f), s);
    }
    float by = y + h - 75;
    int next = game_next_track(g);
    bool advance = g->mode == MODE_CHAMPIONSHIP && next >= 0;
    bool waiting = championship && !g->championship_scored && !g->test_mode;
    const char *primary = completed ? "BACK TO MENU" : advance ? "NEXT STAGE" : "RACE AGAIN";
    if (waiting) {
        // A status indicator, with no click target or hover response.
        slant(u, (Rect){m, by, 229, 49}, alpha(WHITE, .08f));
        for (int i = 0; i < 10; i++) {
            float angle = g->clock * 5 + i * 2 * PI / 10;
            line(u, m + 30 + cosf(angle) * 6, by + 24.5f + sinf(angle) * 6,
                 m + 30 + cosf(angle) * 10, by + 24.5f + sinf(angle) * 10, 2,
                 alpha(WHITE, .1f + .08f * i));
        }
        italic(u, 1, 20, m + 50, by + 12.5f, alpha(WHITE, .75f), "WAITING FOR RIVALS");
        SDL_snprintf(s, sizeof s, "%d / %d RESULTS IN", settled, CAR_COUNT);
        label(u, 0, 16, m + 247, by + 15, alpha(WHITE, .6f), s);
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
    if (!completed && button(u, (Rect){x + w - 225, by, 189, 49}, "BACK TO MENU", false)) {
        game_leave_results(g);
        ui_clear_input(u);
    }
}

static void save_warning(UI *u, Game *g) {
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .86f));
    float cx = (u->width + u->safe_left - u->safe_right) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - 310) * .5f;
    skew(u, (Rect){cx - 350, y, 700, 310}, 20, alpha(NIGHT, .95f));
    skew_outline(u, (Rect){cx - 350, y, 700, 310}, 20, 1, alpha(WHITE, .14f));
    skew(u, (Rect){cx - 350 + 18.5f, y, 240, 5}, 1.5f, ORANGE);
    italic_center(u, 50, (Rect){cx - 350, y + 28, 700, 66}, WHITE, "CHANGES COULD NOT BE SAVED.",
                  true);
    center_label(u, 0, u->mobile ? 24 : 18, cx, y + 111, alpha(WHITE, .7f),
                 "Your latest changes are only kept for this session.");
    center_label(u, 0, u->mobile ? 24 : 18, cx, y + 142, alpha(WHITE, .7f),
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
        center_label(u, 0, 14, cx, y + 275, alpha(WHITE, .5f),
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
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .82f));
    float w = 760, h = u->mobile ? 536 : 510;
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - h) * .5f;
    float cx = x + w * .5f;
    skew(u, (Rect){x, y, w, h}, 22, alpha(NIGHT, .95f));
    skew_outline(u, (Rect){x, y, w, h}, 22, 1, alpha(WHITE, .14f));
    skew(u, (Rect){x + 20.5f, y, 260, 5}, 1.5f, ORANGE);
    char track[48];
    upper(track, sizeof track, g->tracks[g->selected].name);
    center_label(u, 0, u->mobile ? 22 : 16, cx, y + 24, tint(ORANGE, .25f), track);
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool in_top = false;
    const TrackRecords *table = &g->profile.records[g->selected];
    for (int i = 0; i < table->top_count; i++)
        in_top |= table->top[i].id == g->result_id;
    italic_center(u, 54, (Rect){x, y + 47, w, 66}, WHITE,
                  championship    ? "YOUR CHAMPIONSHIP INITIALS"
                  : g->new_record ? "NEW TRACK RECORD!"
                  : in_top        ? "YOU MADE THE TOP 10!"
                                  : "PUT YOUR NAME ON IT.",
                  true);
    char time[32], summary[100];
    record_time(time, sizeof time, g->cars[0].finish_time);
    if (g->previous_best > 0)
        SDL_snprintf(summary, sizeof summary, "%s   /   %+.3f S VS PREVIOUS BEST", time,
                     roundf(g->cars[0].finish_time * 1000) / 1000 - g->previous_best);
    else
        SDL_snprintf(summary, sizeof summary, "%s   /   YOUR FIRST FINISH", time);
    center_label(u, 0, u->mobile ? 22 : 18, cx, y + 118,
                 g->record_save_failed ? tint(ORANGE, .2f) : alpha(WHITE, .65f),
                 u->mobile && g->record_save_failed ? "Could not save. Retry, or continue unsaved."
                 : championship ? "We'll use these initials for your Championship records."
                                : summary);
    center_label(u, 0, u->mobile ? 22 : 15, cx, y + (u->mobile ? 150 : 154), WHITE,
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
        bool current = g->initials_cursor == i;
        slant(u, letter, current ? ORANGE : alpha(WHITE, .1f));
        if (!current)
            slant_outline(u, letter, 1.5f, alpha(WHITE, .3f));
        char text[2] = {g->initials[i], 0};
        italic_center(u, u->mobile ? 56 : 64, letter, WHITE, text, true);
        if (hit(u, letter))
            g->initials_cursor = i;
        if (button(u, (Rect){xx, y + (u->mobile ? 340 : 330), letter_w, u->mobile ? 72 : 38}, "-",
                   false)) {
            g->initials_cursor = i;
            change_initial(g, -1);
        }
    }
    if (!u->mobile)
        center_label(u, 0, 14, cx, y + 382, alpha(WHITE, .5f),
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
        center_label(u, 0, 13, cx, y + 477,
                     g->record_save_failed ? tint(ORANGE, .2f) : alpha(WHITE, .5f),
                     g->record_save_failed
                         ? "Could not save. Retry, or Esc / B to continue unsaved."
                         : "Local records on this device. Your last initials are remembered.");
}

static void records(UI *u, Game *g) {
    page_backdrop(u);
    float w = 1120, h = fminf(680, u->height - u->safe_top - u->safe_bottom - 40);
    float x = u->safe_left + (u->width - u->safe_left - u->safe_right - w) * .5f;
    float y = u->safe_top + (u->height - u->safe_top - u->safe_bottom - h) * .5f;
    float m = x + 30;
    skew(u, (Rect){x, y, w, h}, 22, alpha(NIGHT, .9f));
    skew_outline(u, (Rect){x, y, w, h}, 22, 1, alpha(WHITE, .14f));
    skew(u, (Rect){x + 20.5f, y, 300, 5}, 1.5f, ORANGE);
    caps(u, m, y + 20, tint(ORANGE, .25f), "THE LOCAL HALL OF FAME / TWO-LAP RACES");
    title(u, m, y + 56, 44, g->tracks[g->selected].name);
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
    label(u, 0, 14, m + 389, y + 117, alpha(WHITE, .6f),
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
        caps(u, m + cols[i], y + 166, alpha(WHITE, .5f), heads[i]);
    float row_h = (h - 272) / 10;
    for (int row = 0; row < 10 && g->record_page * 10 + row < count; row++) {
        int index = g->record_page * 10 + row;
        const RaceRecord *e = &entries[index];
        float yy = y + 194 + row * row_h;
        bool current = g->records_return == SCREEN_RESULTS && e->id == g->result_id;
        Rect band = {m - 12, yy - 2, w - 42, row_h - 2};
        if (current) {
            skew(u, band, 6, alpha(ORANGE, .26f));
            skew(u, (Rect){band.x, band.y, 10, band.h}, 6, ORANGE);
        } else if (row % 2 == 0)
            skew(u, band, 6, alpha(WHITE, .05f));
        char text[120];
        SDL_snprintf(text, sizeof text, "%02d", g->record_history ? count - index : index + 1);
        label(u, 0, 15, m, yy, alpha(WHITE, .6f), text);
        italic(u, 1, 21, m + cols[1], yy - 3, current ? tint(ORANGE, .3f) : WHITE, e->initials);
        record_time(text, sizeof text, e->time);
        label(u, 0, 16, m + cols[2], yy, WHITE, text);
        SDL_strlcpy(text, "--:--.---", sizeof text);
        if (e->best_lap > 0)
            record_time(text, sizeof text, e->best_lap);
        label(u, 0, 15, m + cols[3], yy, alpha(WHITE, .6f), text);
        const char *levels[] = {"EASY", "NORMAL", "HARD"};
        if (e->mode < 0 || e->difficulty < 0)
            SDL_strlcpy(text, "LEGACY RECORD", sizeof text);
        else
            SDL_snprintf(text, sizeof text, "%s / %s / P%d",
                         e->mode == MODE_ARCADE ? "ARC" : "CHAMP", levels[e->difficulty], e->rank);
        label(u, 0, 14, m + cols[4], yy, alpha(WHITE, .6f), text);
        SDL_strlcpy(text, "UNKNOWN", sizeof text);
        time_t date = (time_t)e->date;
        struct tm *local = e->date ? localtime(&date) : NULL;
        if (local)
            strftime(text, sizeof text, "%Y-%m-%d %H:%M", local);
        label(u, 0, 13, m + cols[5], yy + 1, alpha(WHITE, .6f), text);
        if (e->record) {
            if (e->improvement > 0)
                SDL_snprintf(text, sizeof text, "NEW BEST  -%.3f S", e->improvement);
            else
                SDL_strlcpy(text, e->mode < 0 ? "IMPORTED BEST" : "FIRST RECORD", sizeof text);
            label(u, 0, 13, m + cols[6], yy + 1, tint(ORANGE, .25f), text);
        }
    }
    if (!count) {
        italic_center(u, 38, (Rect){x, y + 245, w, 46}, WHITE, "THE FIRST NAME COULD BE YOURS.",
                      true);
        center_label(u, 0, 18, x + w * .5f, y + 300, alpha(WHITE, .6f),
                     "Finish a race on this track to start its record book.");
    }
    float by = y + h - 60;
    if (button(u, (Rect){m, by, 210, 42},
               g->records_return == SCREEN_RESULTS ? "BACK TO RESULTS" : "BACK TO TRACKS", true))
        g->screen = g->records_return;
    label(u, 0, 13, m + 235, by + 13, alpha(WHITE, .5f),
          "TAB / Y: SWITCH VIEW   LEFT / RIGHT: PAGE");
    char page[40];
    SDL_snprintf(page, sizeof page, "%d / %d", g->record_page + 1, pages);
    center_label(u, 0, 16, x + w - 145, by + 11, WHITE, page);
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
    // Keep the framebuffer opaque under translucent panels, including in screenshots.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
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
                mobile_menu(u, g, r);
                mobile_header(u, g);
            } else {
                menu(u, g, r);
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
            Rect recover = recover_rect(u), wheel = wheel_target(u, l, r);
            UI_CHECK(recover.x >= u->safe_left && recover.y >= u->safe_top + 104 &&
                         recover.y + recover.h < wheel.y,
                     "RECOVER sits below the position panel, clear of the wheel");
            const float grabs[][2] = {{2, l.y + 40},
                                      {r.x + r.w + 80, l.y + 40},
                                      {r.x + r.w - 10, wheel.y + 6}};
            for (size_t k = 0; k < SDL_arraysize(grabs); k++) {
                test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 51, grabs[k][0], grabs[k][1]);
                ui_controls(u, g);
                UI_CHECK(u->wheel_pointer != 0 && ui_controls(u, g).steer == 0,
                         "wheel grabs neutrally beside and above its visible rim");
                ui_clear_input(u);
            }
            test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 51, recover.x + recover.w * .5f,
                              recover.y + recover.h * .5f);
            ui_controls(u, g);
            UI_CHECK(u->wheel_pointer == 0, "RECOVER presses never grab the wheel");
            ui_clear_input(u);
            test_finger_event(u, SDL_EVENT_FINGER_DOWN, 1, 51, b.x + 4, b.y + 4);
            controls = ui_controls(u, g);
            UI_CHECK(u->wheel_pointer == 0 && controls.brake > 0,
                     "the enlarged wheel target stays clear of the pedals");
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
