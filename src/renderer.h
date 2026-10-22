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
// A large static batch sorted into ground tiles, so that draws skip the tiles
// outside the camera or shadow frustum. Each tile is a contiguous vertex range.
typedef struct {
    int first, count;
    float box[6]; // Minimum x, y, z and maximum x, y, z, including animation reach
} MeshTile;
typedef struct {
    Mesh mesh;
    MeshTile *tiles;
    int tile_count;
} TiledMesh;
// The land beyond the island reaches the horizon. Its highest point in each
// cell of this grid, centred on the island, keeps cameras and the helicopter above it.
#define LAND_GRID 64
#define LAND_CELL 25.f
// The meshes of the circuit being shown; only one circuit is resident at a time.
typedef struct {
    int track; // -1 before the first load
    TiledMesh scene, vegetation, crowd, wildlife, land;
    Mesh water;
    float land_top[LAND_GRID * LAND_GRID];
} TrackScene;
#define BLOOM_LEVELS 5
typedef struct {
    GLuint water_shader, spray_shader, spray_vao, spray_vbo;
    GLuint opaque_fbo, opaque_texture, opaque_depth;
    TrackScene scene;
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
    Vec3 horizon, zenith, ember;
    // Scenery dissolves into the sky before the far clip plane cuts it off.
    float overcast, fog_far, clip_far, fade, vignette, speed_blur;
    int viewport[4];
    // Each car's body and one wheel, near and far levels of detail.
    Mesh cars[CAR_MODEL_COUNT], cars_far[CAR_MODEL_COUNT];
    Mesh wheels[CAR_MODEL_COUNT], wheels_far[CAR_MODEL_COUNT];
    Mesh fuel, cube, turntable, puff, barrier, barrier_hinge;
    Mesh cone, tire, fence;
    Mesh boosts[TRACK_COUNT];
    Mesh helicopter, helicopter_rotor, helicopter_tail_rotor;
    Mesh rail_models[RAIL_MODEL_COUNT];
    Mat4 view, projection, vp, light_vp;
    float frustum[6][4]; // Planes of the frustum being drawn, for tile culling
    Vec3 eye, target, cam_pos;
    float camera_yaw;
    float crowd_time;
    Vec3 helicopter_pos;
    float helicopter_yaw, helicopter_race_time;
    bool helicopter_ready;
    // Car transforms shared by the shadow pass, color pass and night lamps.
    Mat4 car_models[CAR_COUNT];
    Color car_colors[CAR_COUNT];
    CarModel car_meshes[CAR_COUNT];
    bool car_visible[CAR_COUNT];
    // Wheels roll with the distance each car covers and steer into its curve.
    float wheel_spin[CAR_COUNT], wheel_steer[CAR_COUNT], last_yaw[CAR_COUNT];
    // Brake lamps light while a car slows hard.
    float last_speed[CAR_COUNT], brake[CAR_COUNT];
    Vec3 last_pos[CAR_COUNT];
    bool wheels_ready[CAR_COUNT];
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
/* Loads the circuit's meshes unless they are already resident. */
bool renderer_load_track(Renderer *r, int track);
void renderer_resize(Renderer *r, int w, int h);
void renderer_draw(Renderer *r, const Game *g, float frame_dt, float alpha);
// Grades the drawn scene onto the default framebuffer; renderer_draw ends with it.
void renderer_present(Renderer *r);
bool renderer_project(const Renderer *r, Vec3 p, float *x, float *y);
bool renderer_screenshot(const Renderer *r, const char *path);
void renderer_destroy(Renderer *r);
#endif
