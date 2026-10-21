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
#define BLOOM_LEVELS 5
typedef struct {
    GLuint water_shader, spray_shader, spray_vao, spray_vbo;
    GLuint opaque_fbo, opaque_texture, opaque_depth;
    Mesh water[TRACK_COUNT];
    GLuint shader, depth_shader, post_shader, weather_shader, empty_vao;
    GLuint shadow_fbo, shadow_texture, scene_fbo, scene_texture, scene_depth;
    // Post-processing: half-resolution occlusion and its blur, a bloom pyramid
    // starting at half resolution, and the graded image before antialiasing.
    GLuint sky_shader, ao_shader, ao_blur_shader, bloom_down_shader, bloom_up_shader, fxaa_shader;
    GLuint ao_fbo[2], ao_texture[2], bloom_fbo[BLOOM_LEVELS], bloom_texture[BLOOM_LEVELS];
    GLuint ldr_fbo, ldr_texture;
    // The menu broadcast's depth of field: focus distance, and the widest blur
    // as a fraction of the image height (zero turns it off).
    GLuint dof_down_shader, dof_blur_shader, dof_fbo[2], dof_texture[2];
    float focus, aperture;
    // The scene is floating point where the GPU can render to it, so that lamps
    // and glints can exceed white; GL_RGBA8 otherwise.
    GLenum scene_format;
    Vec3 horizon, zenith;
    float overcast, fog_far, fade, vignette, speed_blur;
    int viewport[4];
    Mesh wildlife[TRACK_COUNT], vegetation[TRACK_COUNT];
    Mesh tracks[3], crowds[3], car, fuel, plane, cube;
    Mesh cone, tire, fence, lamps;
    Mesh boosts[TRACK_COUNT];
    Mesh helicopter, helicopter_rotor, helicopter_tail_rotor;
    Mesh rail_models[RAIL_MODEL_COUNT];
    Mat4 view, projection, vp, light_vp;
    Vec3 eye, target, cam_pos;
    float camera_yaw;
    float crowd_time;
    Vec3 helicopter_pos;
    float helicopter_yaw, helicopter_race_time;
    bool helicopter_ready;
    // Car transforms shared by the shadow pass, color pass and night lamps.
    Mat4 car_models[CAR_COUNT];
    Color car_colors[CAR_COUNT];
    bool car_visible[CAR_COUNT];
    Color sky;
    float night;
    // The main menu films the selected track. Its layout sets hero_frame, a
    // sideways lens shift that keeps the cars clear of the menu panel.
    bool hero;
    float hero_frame;
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
// Grades the drawn scene onto the default framebuffer; renderer_draw ends with it.
void renderer_present(Renderer *r);
bool renderer_project(const Renderer *r, Vec3 p, float *x, float *y);
bool renderer_screenshot(const Renderer *r, const char *path);
void renderer_destroy(Renderer *r);
#endif
