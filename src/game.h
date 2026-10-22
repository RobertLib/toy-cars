#ifndef TOYCARS_GAME_H
#define TOYCARS_GAME_H
#include "math3d.h"
#include <stdbool.h>
#include <SDL3/SDL.h>
#define TRACK_COUNT 10
#define CUP_COUNT 3
#define MAX_POINTS 2048
#define MAX_CANS 24
#define MAX_RAMPS 12
#define MAX_BOOSTS 3
#define BOOST_LENGTH 6.f
#define BOOST_HALF 2.6f
#define CAR_COUNT 8
#define RACE_LAPS 2
#define RECORD_TOP_COUNT 10
#define RECORD_HISTORY_COUNT 30
#define FUEL_PICKUP_AMOUNT 18
#define FUEL_PICKUP_RADIUS 1.5f
#define FUEL_PICKUP_COOLDOWN 35.f
#define QUIP_TIME 2.4f
#define QUIP_MEMORY 6
#define MAX_PARTICLES 1024
#define MAX_TRACK_PROPS 160
#define MAX_FENCE_PANELS 16
#define FENCE_COLS 7
#define FENCE_ROWS 3
#define FENCE_NODES (FENCE_COLS * FENCE_ROWS)
typedef struct {
    Vec3 pos, previous, render_prev;
} FenceNode;
typedef struct {
    int posts[2];
    FenceNode nodes[FENCE_NODES];
    float width, awake_time;
    bool touched; // A car pressed into the net during the previous step.
} FencePanel;
typedef enum { PROP_CONE, PROP_TIRE, PROP_POST } PropKind;
typedef struct {
    Vec3 pos, prev, velocity, omega;
    Vec3 anchor; // Fence stake foot, retained in the soil after bending.
    Mat4 rotation, prev_rotation;
    float inv_mass, inv_inertia;
    PropKind kind;
    bool asleep;
    float quiet_time;
} TrackProp;
#define MAX_WATER_TRIANGLES 32768
// The sea around the coastal islands, as authored in tools/hydrology.py. On a
// lava track only liquid clearly above it is lava; the sea stays water.
#define SEA_LEVEL -6.85f
#define LAVA_ABOVE (SEA_LEVEL + 1)
#define MAX_WATER_REFS 196608
#define FIXED_DT (1.0f / 120.0f)
// The menu's race broadcast: its pack leader drives BROADCAST_SPEED m/s from
// BROADCAST_START metres at clock zero.
#define BROADCAST_SPEED 19.f
#define BROADCAST_START 170.f
// Includes the road's cross-width tessellation. Each circuit's triangles and
// grid references are allocated to fit; these bound what a file may ask for.
#define MAX_SURFACE_TRIANGLES 262144
#define MAX_SURFACE_REFS 1048576
// The largest islands span about 460 m; the grid covers 512 m in 8 m cells.
#define SURFACE_GRID 64
#define SURFACE_ORIGIN -256.f
#define SURFACE_CELL 8.f
// Ground queries bridge slits between adjacent driving meshes up to this far
// from a triangle; road and curb edges leave seams several centimetres wide.
#define SURFACE_SEAM .08f

typedef struct {
    Vec3 a, b, c;
} SurfaceTriangle;
static inline int surface_cell(float coordinate) {
    return (int)clampf(floorf((coordinate - SURFACE_ORIGIN) / SURFACE_CELL), 0, SURFACE_GRID - 1);
}

typedef struct {
    Vec3 p;
    float dx, dz, half, s;
} TrackPoint;
typedef struct {
    float index, lane;
} Can;
typedef struct {
    float index, length, height, half;
} Ramp;
typedef struct {
    Vec3 a, b; // Deck-level endpoints of a visible bridge railing.
} Guardrail;
#define MAX_RAIL_POINTS 512
#define MAX_RAIL_PASSES 2
#define MAX_RAIL_STOPS 2
#define MAX_RAIL_ARMS 2
#define MAX_RAIL_LIGHTS 2
#define MAX_RAIL_VEHICLES 4
// Most circuits run trains between two tunnels; Sunshine Coast and Jungle Ruins
// have a road train on a closed promenade with a stop at each end.
typedef enum { RAIL_STEAM, RAIL_ROAD_TRAIN, RAIL_ALPINE, RAIL_FREIGHT } RailKind;
typedef struct {
    Vec3 pivot;    // Hinge of the barrier arm
    float dx, dz;  // Horizontal direction of the lowered arm
    float length;
} RailArm;
typedef struct {
    Vec3 pos;      // Centre between the two lamps of a flashing signal
    float dx, dz;  // Direction the lamps face
    float spread;  // Half the distance between the lamps
} RailLight;
typedef struct {
    RailKind kind;
    bool loop;     // A closed route; otherwise it runs from tunnel to tunnel
    int count;     // Path points; 0 when the circuit has no crossing
    Vec3 points[MAX_RAIL_POINTS];
    float s[MAX_RAIL_POINTS];
    float length, visible_start, visible_end;
    float passes[MAX_RAIL_PASSES]; // Path distance where the route crosses the road
    float stops[MAX_RAIL_STOPS];
    int pass_count, stop_count;
    float zone; // Half-length of the path either side of a pass that blocks the road
    // Race distances of the stop line, the crossing centre and its far side.
    float stop_s, road_s, clear_s;
    RailArm arms[MAX_RAIL_ARMS];
    RailLight lights[MAX_RAIL_LIGHTS];
    int arm_count, light_count;
} Railway;
// What sets each circuit apart: its place in the Championship, surface grip,
// climate, colours and sounds. TRACK_INFO in src/tracks.c lists all ten.
typedef enum { AMBIENCE_COUNTRY, AMBIENCE_BEACH, AMBIENCE_WINTER, AMBIENCE_DESERT, AMBIENCE_FOREST,
               AMBIENCE_JUNGLE, AMBIENCE_GARDEN, AMBIENCE_HARBOR, AMBIENCE_MOOR, AMBIENCE_VOLCANO } Ambience;
typedef enum { VERGE_DIRT, VERGE_SAND, VERGE_SNOW } VergeSound;
typedef struct {
    const char *id, *name, *subtitle, *description;
    int cup;
    bool night;              // The Championship stage runs at night
    Color accent, sky;       // Interface accent; horizon and fog colour
    Vec3 zenith, road;       // Sky overhead; the exported asphalt colour
    float grip, corner_grip; // Lateral grip and the lateral acceleration limit, m/s²
    bool slippery;           // Catch-up never raises a rival's corner pace here
    bool lava;               // Liquid above the sea is lava: shading, no spray, recovery
    int precipitation;       // WEATHER_RAIN or WEATHER_SNOW (snow or volcanic ash)
    float wind_share;        // Share of weather spells that are gusts
    Color dust, flakes;      // Tyre dust; snow or ash flakes
    Vec3 gust[2];            // Leaves, sand or petals carried by the gusts
    float dust_rate, clouds, fog;
    Vec3 water_tint, water_extinction;
    Ambience ambience;
    VergeSound verge;
    RailKind rail;
    Color livery;            // Body colour of the locomotive and coaches
    int crossings;           // Bridges where the road passes over itself
    const char *offroad[3], *weather[2];
} TrackInfo;
typedef struct {
    const char *name;
    int first, count; // Tracks of the cup, in Championship order
} CupInfo;
extern const TrackInfo TRACK_INFO[TRACK_COUNT];
extern const CupInfo CUPS[CUP_COUNT];
typedef struct {
    const TrackInfo *info;
    Guardrail guardrails[MAX_POINTS];
    int guardrail_count;
    Railway rail;
    // Lowered crossing barriers, refreshed every step; cars meet them like rails.
    Guardrail barriers[MAX_RAIL_ARMS];
    int barrier_count;
    TrackPoint points[MAX_POINTS];
    Can cans[MAX_CANS];
    Ramp ramps[MAX_RAMPS];
    float boosts[MAX_BOOSTS]; // Distance along the road, centered in the lane.
    int boost_count;
    SurfaceTriangle *surface;
    uint32_t surface_cells[SURFACE_GRID * SURFACE_GRID + 1];
    uint32_t *surface_refs;
    int surface_count;
    SurfaceTriangle *water;
    uint32_t water_cells[SURFACE_GRID * SURFACE_GRID + 1];
    uint16_t *water_refs;
    int water_count;
    int count, can_count, ramp_count;
    float length, elevation;
    float extent[4]; // Island bounds: minimum and maximum x, minimum and maximum z
    const char *id, *name, *subtitle, *description;
    Color accent, sky;
} Track;
// The railway's own clock and timetable. Trains run during the race and its
// results; a run starts deep inside one tunnel and ends inside the other.
typedef struct {
    float time;
    float run, next;        // Start of the current and the following run
    int direction, next_direction;
    float head, prev_head;  // Distance of the leading nose along its direction of travel
    float arm, prev_arm;    // Barrier arms: 0 raised, 1 lowered
    float phase;            // Offset of the road train's timetable
    float dwell[MAX_RAIL_STOPS]; // Its wait at each stop
    float speed;            // Of the train along its route, m/s
    int track;              // Circuit the broadcast timetable was set for
    bool warning, horn;
    uint32_t rng;
} RailState;
typedef struct {
    Vec3 pos, forward;      // Centre on the rails and direction of travel
    float pitch, length, half_width, front, back;
    int model;              // Vehicle model, see RAIL_MODEL_*
    bool visible;
} RailVehicle;
enum {
    RAIL_MODEL_STEAM_LOCOMOTIVE, RAIL_MODEL_STEAM_TENDER, RAIL_MODEL_COACH,
    RAIL_MODEL_ALPINE_LOCOMOTIVE, RAIL_MODEL_ALPINE_COACH,
    RAIL_MODEL_ROAD_TRAIN_TRACTOR, RAIL_MODEL_ROAD_TRAIN_CARRIAGE,
    RAIL_MODEL_DIESEL_LOCOMOTIVE, RAIL_MODEL_CONTAINER_WAGON, RAIL_MODEL_COUNT
};
// The garage: a few cars, each with four upgrades of five levels. Their
// performance is measured against the handling model's reference car; a
// zero tune drives exactly like it, +.1 is ten percent more.
#define CAR_MODEL_COUNT 4
#define UPGRADE_COUNT 4
#define UPGRADE_LEVELS 5
typedef enum { CAR_TRAILBLAZER, CAR_SPRITE, CAR_BRUISER, CAR_COMET } CarModel;
typedef enum { UPGRADE_ENGINE, UPGRADE_SPEED, UPGRADE_TIRES, UPGRADE_FUEL } UpgradeKind;
typedef struct {
    float power;   // Engine acceleration
    float speed;   // Top speed on asphalt
    float grip;    // Tire grip and the cornering limit
    float economy; // Share of fuel saved per second of driving
    float grit;    // Share of the off-road drag and power loss shrugged off
} CarTune;
typedef struct {
    const char *name, *kind, *tagline;
    int price;   // Coins; the first car is free
    int markup;  // Upgrade prices in percent of the base price list
    // Tune at level 0 and the gain per upgrade level, in UpgradeKind order.
    float base[UPGRADE_COUNT], step[UPGRADE_COUNT];
    float grit;
} CarInfo;
extern const CarInfo CAR_INFO[CAR_MODEL_COUNT];
extern const char *UPGRADE_NAMES[UPGRADE_COUNT];
typedef struct {
    // Driver-relative steering: -1 left, +1 right.
    // Throttle above 1 is catch-up power for rivals far behind the player.
    float steer, throttle, brake;
    bool drift, reset;
    // Holding the player's brake also requests reverse once stopped.
    bool reverse;
} Controls;
typedef struct {
    Vec3 pos, prev, velocity, route_pos;
    float yaw, prev_yaw, speed, vy, pitch, roll, fuel;
    // Free chassis motion uses a world-space angular velocity and orientation.
    Mat4 body_rotation, prev_body_rotation;
    Vec3 omega;
    float pitch_rate, roll_rate, load_roll_rate, rollover_rest;
    bool tumbling;
    float progress, last_s, finish_time, lap_start, best_lap, lap_time;
    float offroad_delta, offroad_distance, offroad_time;
    float target_lane, stuck_time, air_time, ramp_cooldown, empty_time;
    float boost_cooldown;
    float reset_protection; // Seconds of collision immunity after recovery.
    // Audio cues from the latest step: engine power (0..1), sideways tire speed
    // in m/s, depth beyond the road edge in meters, and tires in water.
    float throttle, slip, verge;
    bool wet;
    float can_cooldown[MAX_CANS];
    CarTune tune;
    float skill; // A rival's cornering margin above or below its difficulty's
    CarModel model;
    int nearest, lap, checkpoints, collected, jumps;
    bool grounded, finished, dnf, offroad;
    Color color;
    const char *name;
    // Marker bubble chatter about the road ahead, never about what is behind.
    char quip[40];
    float quip_time, quip_quiet;
    int quip_count, quip_topics[QUIP_MEMORY];
    float quip_marks[QUIP_MEMORY]; // Race progress when each recent topic was spoken.
} Car;
/* Sound cues raised by the simulation. A step can raise several; the audio
   producer consumes them once per frame. */
typedef enum {
    SOUND_COUNTDOWN = 1,
    SOUND_GO,
    SOUND_FUEL,
    SOUND_FINAL_LAP,
    SOUND_TAKEOFF,
    SOUND_LANDING,
    SOUND_OUT_OF_FUEL,
    SOUND_FINISH,
    SOUND_CELEBRATION,
    SOUND_BOOST,
    SOUND_RECOVER,
    SOUND_CAR_HIT,
    SOUND_RAIL_HIT,
    SOUND_CONE_HIT,
    SOUND_TIRE_HIT,
    SOUND_FENCE_HIT,
    SOUND_TRAIN_HORN, // A train or the road train announces itself
    SOUND_TRAIN_HIT   // A car struck by a passing train
} SoundKind;
#define SOUND_KIND_COUNT (SOUND_TRAIN_HIT + 1)
#define MAX_SOUND_EVENTS 24
typedef struct {
    SoundKind kind;
    int car;        // Car that caused it, or -1
    float strength; // Impact or landing speed in m/s; 1 for plain cues
    Vec3 pos;
} SoundEvent;
typedef enum { PARTICLE_DUST, PARTICLE_SPRAY, PARTICLE_RIPPLE, PARTICLE_SMOKE } ParticleKind;
typedef struct {
    Vec3 pos, vel;
    float life, max_life, size;
    Color color;
    ParticleKind kind;
} Particle;
typedef enum { WEATHER_CLEAR, WEATHER_RAIN, WEATHER_SNOW, WEATHER_WIND } WeatherKind;
typedef struct {
    WeatherKind kind;
    float time, remaining, duration, intensity, strength, direction;
    Vec3 wind, drift;
    uint32_t rng;
} Weather;
typedef enum {
    SCREEN_MENU,
    SCREEN_COUNTDOWN,
    SCREEN_RACE,
    SCREEN_PAUSE,
    SCREEN_RESULTS,
    SCREEN_GARAGE,
    SCREEN_HELP,
    SCREEN_SETTINGS,
    SCREEN_RECORDS,
    SCREEN_RESET_DATA
} Screen;
typedef enum { MODE_CHAMPIONSHIP, MODE_ARCADE } RaceMode;
typedef struct {
    unsigned id;
    char initials[4];
    float time, best_lap, improvement;
    long long date;
    int difficulty, mode, rank;
    bool record;
} RaceRecord;
typedef struct {
    RaceRecord top[RECORD_TOP_COUNT], history[RECORD_HISTORY_COUNT];
    int top_count, history_count;
} TrackRecords;
typedef struct {
    int color, difficulty;
    bool auto_accel, touch, sound, music;
    bool arcade_night; // Championship stages set their own time of day.
    float best[TRACK_COUNT];
    int medals[TRACK_COUNT];
    int championship_completed; // Permanent track unlock progress.
    // Each cup keeps its own series: stages scored, elimination and best medal.
    int championship_stages[CUP_COUNT], championship_medal[CUP_COUNT];
    bool championship_eliminated[CUP_COUNT];
    int championship_places[TRACK_COUNT][CAR_COUNT]; // By track; 0 = DNF, otherwise 1..8.
    char initials[4];
    bool initials_set;
    unsigned record_serial;
    TrackRecords records[TRACK_COUNT];
    int coins;
    CarModel car; // The car the player races, always one they own
    bool owned[CAR_MODEL_COUNT];
    int upgrades[CAR_MODEL_COUNT][UPGRADE_COUNT];
} Profile;
typedef struct {
    Track tracks[TRACK_COUNT];
    Car cars[CAR_COUNT];
    Particle particles[MAX_PARTICLES];
    Weather weather;
    TrackProp props[MAX_TRACK_PROPS];
    int prop_count;
    FencePanel fences[MAX_FENCE_PANELS];
    int fence_count;
    RailState rail;
    Vec3 roadwork;
    float roadwork_yaw;
    Profile profile;
    Screen screen;
    RaceMode mode;
    int unlocked_track;
    bool championship_advanced;
    bool championship_scored;
    bool championship_celebration;
    float celebration_time;
    int selected, particle_cursor, rank, finish_rank;
    float time, countdown, clock, toast_time, wrong_way, shake;
    float rival_quiet; // Pause between rival lines, which take turns.
    float start_idle_time;
    bool start_hint_dismissed;
    char toast[80];
    uint32_t rng;
    bool test_mode;
    bool force_night; // Diagnostic override for every mode.
    float force_train; // Diagnostic: race time the first train reaches the crossing.
    SoundEvent sounds[MAX_SOUND_EVENTS];
    int sound_count;
    float last_lap;
    unsigned result_id;
    float previous_best;
    bool new_record, initials_pending, record_history, record_save_failed;
    // Continuing unsaved dismisses the warning, not the pending save failure.
    bool save_failure_acknowledged;
    char initials[4];
    int initials_cursor, record_page;
    Screen records_return;
    // Coins the latest race paid, and the cup bonus when it was a cup's final.
    int prize, cup_prize;
    CarModel garage_car; // The car on the garage turntable, owned or not
} Game;
extern const Color CAR_COLORS[6];
extern const char *CAR_COLOR_NAMES[6];
bool game_load(Game *g);
void game_free(Game *g);
/* Allocates a track's ground triangles and grid references (tests build their own). */
bool track_alloc_surface(Track *t, int triangles, int refs);
int game_cup(const Game *g);
int track_cup(int track);
/* Stage of the selected cup's series, 0-based; the stage count when it is complete. */
int game_cup_stage(const Game *g, int cup);
bool game_cup_fresh(const Game *g, int cup);
void game_select_cup(Game *g, int cup);
int game_track_index(const char *id);
void track_place_boosts(Track *t);
void game_start(Game *g);
void props_reset(Game *g);
void props_tick(Game *g, float dt);
/* Railway: timetable, barriers, train collisions and the vehicles' poses. */
void rail_reset(Game *g);
void rail_tick(Game *g, float dt);
/* The menu broadcast's train: it clears the crossing just ahead of the pack. */
void rail_broadcast(Game *g);
int rail_vehicles(const Game *g, float alpha, RailVehicle *out);
bool rail_window(const Game *g, float after, float *close, float *open);
float rail_arm(const Game *g, float alpha);
void game_continue(Game *g);
void game_leave_results(Game *g);
bool game_track_unlocked(const Profile *p, int track);
void game_set_mode(Game *g, RaceMode mode);
bool game_night(const Game *g);
int game_next_track(const Game *g);
bool game_championship_finished(const Game *g);
int game_championship_points(const Game *g, int driver);
bool game_championship_precedes(const Game *g, int a, int b);
int game_championship_rank(const Game *g);
void game_tick(Game *g, Controls controls, float dt, bool autopilot);
/* Queues a sound cue; the oldest cue gives way when the queue is full. */
void game_sound(Game *g, SoundKind kind, int car, float strength, Vec3 pos);
/* A puff of locomotive smoke drifting up from a chimney. */
void game_smoke(Game *g, Vec3 pos, Vec3 velocity);
bool game_start_hint_visible(const Game *g);
void game_ai(const Game *g, int index, Controls *out);
void game_reset_car(Game *g, int index);
void game_results(Game *g);
bool game_submit_initials(Game *g);
bool game_save_profile(Game *g);
bool game_save_warning(const Game *g);
bool game_reset_profile(Game *g);
const char *game_player_name(const Game *g);
void game_open_records(Game *g);
bool game_car_precedes(const Game *g, int a, int b);
/* Garage and prize money, see src/cars.c. */
CarTune car_tune(CarModel model, const int levels[UPGRADE_COUNT]);
/* The upgrades a player is expected to own when reaching a circuit. */
void expected_upgrades(int track, int levels[UPGRADE_COUNT]);
CarTune rival_tune(int track, int difficulty, int rival);
CarModel rival_model(int rival);
float rival_skill(int rival);
float car_top_speed(const CarTune *tune);
/* A 0-100 summary of a tune's pace, for the garage and the menu. */
int car_rating(const CarTune *tune);
/* Price of the next level of an upgrade, or 0 once it is complete. */
int upgrade_price(CarModel model, UpgradeKind kind, int level);
int race_prize(int track, int difficulty, RaceMode mode, int place);
int cup_prize(int cup, int difficulty, int medal);
bool game_buy_upgrade(Game *g, UpgradeKind kind);
bool game_buy_car(Game *g, CarModel model);
void game_browse_car(Game *g, CarModel model);
/* Prints how a modelled player fares against the field; --balance. */
int game_balance(int track);
float track_nearest(const Track *t, Vec3 p, int *index, Vec3 *center, float *lane);
Vec3 track_at(const Track *t, float distance, float lane, Vec3 *direction);
bool track_water(const Track *t, Vec3 p, float *height);
float track_height(const Track *t, Vec3 p, bool *ramp);
void car_impact(Car *car, Vec3 delta_velocity, Vec3 arm);
void car_surface_pose(const Track *t, Vec3 *pos, float yaw, float *pitch, float *roll);
void profile_load(Profile *p);
bool profile_save(const Profile *p);
bool profile_reset(Profile *p);
/* Simulation tests on one circuit, or on all of them with -1. */
int game_tests(int track);
void *asset_read(const char *name, size_t *size);
#endif
