#ifndef TOYCARS_GAME_H
#define TOYCARS_GAME_H
#include "math3d.h"
#include <stdbool.h>
#include <SDL3/SDL.h>
#define TRACK_COUNT 3
#define MAX_POINTS 2048
#define MAX_CANS 24
#define MAX_RAMPS 12
#define CAR_COUNT 8
#define RACE_LAPS 2
#define MAX_PARTICLES 512
#define FIXED_DT (1.0f/120.0f)

typedef struct { Vec3 p; float dx,dz,half,s; } TrackPoint;
typedef struct { float index,lane; } Can;
typedef struct { float index,length,height,half; } Ramp;
typedef struct {
    TrackPoint points[MAX_POINTS]; Can cans[MAX_CANS]; Ramp ramps[MAX_RAMPS];
    int count,can_count,ramp_count;float length,elevation;
    const char *id,*name,*subtitle,*description;Color accent,sky;
} Track;
typedef struct {float steer,throttle,brake;bool drift,reset;} Controls;
typedef struct {
    Vec3 pos,prev,velocity;float yaw,prev_yaw,speed,vy,pitch,roll,fuel;
    float progress,last_s,finish_time,lap_start,best_lap,lap_time;
    float target_lane,stuck_time,air_time,ramp_cooldown,empty_time;
    float can_cooldown[MAX_CANS];
    int nearest,lap,checkpoints,collected,jumps;bool grounded,finished,dnf;
    Color color;const char *name;
} Car;
typedef struct { Vec3 pos,vel;float life,max_life,size;Color color; } Particle;
typedef enum { SCREEN_MENU,SCREEN_COUNTDOWN,SCREEN_RACE,SCREEN_PAUSE,SCREEN_RESULTS,SCREEN_GARAGE,SCREEN_HELP,SCREEN_SETTINGS } Screen;
typedef struct {
    int color,difficulty;bool auto_accel,touch,sound,music;float best[TRACK_COUNT];int medals[TRACK_COUNT];
} Profile;
typedef struct {
    Track tracks[TRACK_COUNT];Car cars[CAR_COUNT];Particle particles[MAX_PARTICLES];
    Profile profile;Screen screen;int selected,particle_cursor,rank,finish_rank;
    float time,countdown,clock,toast_time,wrong_way,shake;char toast[80];
    uint32_t rng;bool test_mode;int sound_event;float last_lap;
} Game;
extern const Color CAR_COLORS[6];
extern const char *CAR_COLOR_NAMES[6];
bool game_load(Game *g);
void game_start(Game *g);
void game_tick(Game *g,Controls controls,float dt,bool autopilot);
void game_ai(const Game *g,int index,Controls *out);
void game_reset_car(Game *g,int index);
void game_results(Game *g);
float track_nearest(const Track *t,Vec3 p,int *index,Vec3 *center,float *lane);
Vec3 track_at(const Track *t,float distance,float lane,Vec3 *direction);
float track_height(const Track *t,int index,Vec3 p,float lane,bool *ramp);
void profile_load(Profile *p);
void profile_save(const Profile *p);
int game_tests(void);
void *asset_read(const char *name,size_t *size);
#endif
