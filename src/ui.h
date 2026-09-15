#ifndef TOYCARS_UI_H
#define TOYCARS_UI_H
#include "renderer.h"
#define UI_MAX_VERTS 65536
#define UI_MAX_FINGERS 10
#define UI_MAX_CLICKS 32
typedef struct {
    float x, y, u, v, r, g, b, a;
} UIVertex;
typedef struct {
    GLuint texture;
    float glyphs[96][7];
} UIFont;
typedef struct {
    float x, y, w, h;
} Rect;
typedef struct {
    SDL_TouchID touch_id;
    SDL_FingerID id;
    float x, y, press_x, press_y;
    bool active;
} Finger;
typedef struct {
    float press_x, press_y, x, y;
    bool consumed;
} UIClick;
typedef struct {
    GLuint program, vao, vbo, white, current_texture;
    UIFont fonts[2];
    UIVertex vertices[UI_MAX_VERTS];
    int count;
    float width, height, mx, my, safe_left, safe_right, safe_top, safe_bottom;
    float mouse_x, mouse_y, mouse_press_x, mouse_press_y;
    bool mouse_down;
    Finger fingers[UI_MAX_FINGERS];
    UIClick clicks[UI_MAX_CLICKS];
    int click_count;
    bool left, right, brake, gas;
    int hovered;
    float fps;
    bool show_stats;
} UI;
bool ui_init(UI *u);
void ui_size(UI *u, int w, int h, SDL_Window *window);
bool ui_input_blocked(const UI *u);
void ui_event(UI *u, const SDL_Event *e, int window_w, int window_h);
Controls ui_controls(UI *u, const Game *g);
void ui_draw(UI *u, Game *g, Renderer *r);
void ui_clear_input(UI *u);
void ui_destroy(UI *u);
int ui_tests(void);
#endif
