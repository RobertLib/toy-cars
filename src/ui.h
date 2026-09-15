#ifndef TOYCARS_UI_H
#define TOYCARS_UI_H
#include "renderer.h"
#define UI_MAX_VERTS 65536
typedef struct {float x,y,u,v,r,g,b,a;} UIVertex;
typedef struct {GLuint texture;float glyphs[96][7];} UIFont;
typedef struct {float x,y,w,h;} Rect;
typedef struct {SDL_FingerID id;float x,y;bool active;} Finger;
typedef struct {
    GLuint program,vao,vbo,white,current_texture;UIFont fonts[2];UIVertex vertices[UI_MAX_VERTS];int count;
    float width,height,mx,my,press_x,press_y,safe_left,safe_right,safe_top,safe_bottom;
    bool mouse_down,clicked,click_pending;float click_x,click_y;Finger fingers[10];
    bool left,right,brake,gas;int hovered;float fps;bool show_stats;
} UI;
bool ui_init(UI *u);
void ui_size(UI *u,int w,int h,SDL_Window *window);
void ui_event(UI *u,const SDL_Event *e,int window_w,int window_h);
Controls ui_controls(UI *u,const Game *g);
void ui_draw(UI *u,Game *g,Renderer *r);
void ui_clear_input(UI *u);
void ui_destroy(UI *u);
int ui_tests(void);
#endif
