#ifndef TOYCARS_RENDERER_H
#define TOYCARS_RENDERER_H
#include "game.h"
#if defined(SDL_PLATFORM_IOS)
#include <OpenGLES/ES3/gl.h>
#elif defined(SDL_PLATFORM_ANDROID)
#include <GLES3/gl3.h>
#elif defined(SDL_PLATFORM_MACOS)
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#endif
typedef struct {
    GLuint vao, vbo;
    int count;
} Mesh;
typedef struct {
    GLuint water_shader, spray_shader, spray_vao, spray_vbo;
    GLuint opaque_fbo, opaque_texture, opaque_depth;
    Mesh water[TRACK_COUNT];
    GLuint shader, depth_shader, post_shader, weather_shader, empty_vao;
    GLuint shadow_fbo, shadow_texture, scene_fbo, scene_texture, scene_depth;
    Mesh wildlife[TRACK_COUNT], vegetation[TRACK_COUNT];
    Mesh tracks[3], crowds[3], car, fuel, plane, cube;
    Mesh cone, tire, fence;
    Mesh boosts[TRACK_COUNT];
    Mesh helicopter, helicopter_rotor, helicopter_tail_rotor;
    Mat4 view, projection, vp, light_vp;
    Vec3 eye, target, cam_pos;
    float camera_yaw;
    float crowd_time;
    Vec3 helicopter_pos;
    float helicopter_yaw, helicopter_race_time;
    bool helicopter_ready;
    int width, height, draw_calls, triangles;
    float ui_width, ui_height, alpha;
    GLuint bound_vao, default_fbo;
    bool camera_ready;
    Screen last_screen;
    int last_track;
} Renderer;
GLuint render_program(const char *vs, const char *fs);
bool renderer_init(Renderer *r);
void renderer_resize(Renderer *r, int w, int h);
void renderer_draw(Renderer *r, const Game *g, float frame_dt, float alpha);
bool renderer_project(const Renderer *r, Vec3 p, float *x, float *y);
bool renderer_screenshot(const Renderer *r, const char *path);
void renderer_destroy(Renderer *r);
#endif
