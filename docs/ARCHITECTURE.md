# Architecture

## Runtime

| File | Responsibility |
| --- | --- |
| `main.c` | SDL lifecycle, window, input events, fixed-step loop, gamepads, diagnostic CLI |
| `src/game.c` | Vehicle dynamics, AI, fuel, laps, ranking, collisions, particles and race state |
| `src/assets.c` | Checked binary loading, APK asset lookup, local profile persistence |
| `src/renderer.c` | OpenGL/GLES scene, shadow pass, follow camera, fog, postprocess, screenshot |
| `src/ui.c` | Batched vector/text UI, responsive layouts, multitouch tracking, screens |
| `src/audio.c` | Generated stereo music, engine and event audio through SDL_AudioStream |
| `tools/build_assets.py` | Blender-only modeling, scene saving and geometry/path export |

The runtime has no Python dependency and no network calls. On Android the
manifest requests no internet permission. Font atlases are baked in advance,
so SDL_ttf and FreeType are development tools rather than game dependencies.

## Simulation

Simulation advances at 120 Hz, independently of rendering. Frame deltas are
bounded and a frame performs at most 12 updates to avoid a backlog after
suspension. The renderer interpolates the previous and current car transforms.

Cars keep a planar velocity vector, heading, vertical velocity, fuel and
unwrapped distance around the circuit. Throttle, drag and braking alter forward
speed; a surface-specific grip coefficient damps lateral velocity. Drift lowers
lateral grip and changes steering response. The ramp lip applies vertical
velocity; gravity integrates flight and landing. The world is 3D, while tire
contact follows the rendered road, shoulders and terrain: this is an arcade vehicle model, not a
general-purpose rigid-body engine.

Driving surfaces are exported as collision triangles from the same Blender
meshes as the visible scene. A spatial grid limits height queries to nearby
triangles. Four tire samples set grounded height, pitch and roll at the car's
actual heading, including at spawn, recovery and after collision separation.
The terrain grades smoothly into the road bed; off-road contact does not extend
the road's elevation beyond its edges. Airborne cars still use gravity and
land on the surface below them.

Steering inputs use -1 for the driver's left and +1 for the driver's right.
With local +Z forward and +Y up, positive yaw turns left in the chase view,
so physics subtracts the steering input from yaw. AI heading errors are converted
to the same input convention.

Two overlapping circles approximate each car's oriented collision capsule.
Contact separates cars and applies a damped normal impulse. The road shoulder
slows cars, with resistance capped farther into the terrain. Off-road travel
follows the car's velocity without snapping its position to a lane boundary.
Drag uses physical distance to the road, searching beyond the cached progress
neighborhood when off-road. Leaving the island's driving surface recovers the
car without awarding progress or fuel. Scenery is visual dressing, not an
additional collision world.

Each AI driver uses a speed-dependent lookahead target. Upcoming curvature
sets a desired speed, nearby traffic changes the chosen lane, and low fuel
changes the target toward an available can. All drivers use the same vehicle
update and consume fuel. Pickups have a per-driver cooldown so a lead car
cannot deny the entire field access to fuel.

Track projection searches a bounded neighborhood around the previous segment.
Signed, unwrapped progress and sequential quarter-lap gates prevent finish-line
reversing from counting laps. Recovery preserves distance, checkpoints and fuel.
Lap numbers derive from completed gates and never decrease, so recrossing an
already completed lap cannot restart its timer. Landing reduces the velocity
vector itself and updates the displayed speed from that vector.
After the player finishes, rivals continue to settle the result table.

## Rendering and content

Blender evaluates bevel and weighted-normal modifiers and exports triangles.
Static environment geometry is merged into one vertex buffer per track. A race
uses a depth shadow pass, a colored scene pass and a final edge-smoothing pass.
The UI uses a separate batched triangle stream and single-channel glyph atlases.
The GL and GLES shader variants share their shader bodies.

On a graphics-device reset, the main loop pauses a running race and rebuilds
world and UI resources in SDL's replacement GL context. Names from the lost
context are discarded without deleting objects in the replacement context.
Race state and diagnostic preferences survive; held input and elapsed frame
time are cleared before an explicit resume.

The UI scales uniformly to keep at least 1280 logical units of width and 576
units of safe height. Wide, low windows therefore retain room for result rows,
statistics and buttons. The results panel is centered inside the safe area.
A portrait surface draws only the rotate prompt and blocks covered UI actions,
keyboard/gamepad shortcuts and driving controls.

World coordinates: +Y is up and a car's local +Z is forward. Blender coordinates
are converted from `(x, y, z)` to `(x, z, -y)` at export.

### Binary formats

All integers and IEEE754 floats are little endian. Files are checked for magic,
size, limits and finite geometry values before upload.

**TCM1 mesh:** four-byte magic; `uint32 vertex_count`; ten floats per vertex:
position XYZ, normal XYZ, material RGB and a body-paint interpolation mask.
Vertices are a triangle list. Runtime recoloring affects the marked body material.

**TCP1 track:** four-byte magic; `uint32 point_count`, `uint32 can_count`,
`uint32 ramp_count`, `float lap_length`; seven floats per path point (XYZ,
direction XZ, road half-width, cumulative distance); two floats per can
(point index, lane); four floats per ramp (point index, length, height, half-width).

The path data and visible ramp meshes come from the same Blender authoring step.
Start-grid bars follow the same track distances and lanes as the starting cars,
with each bar just ahead of its car. Grid bars and finish checkers are clipped
to the road's actual triangles and raised slightly above them, so they follow
curves, elevation and banking through the lap seam.

**TCS1 driving surface:** four-byte magic; `uint32 triangle_count`; nine floats
per triangle (three XYZ vertices). Terrain, road, shoulders, curbs and ramp
decks use their exact rendered triangles. Vertical faces, paint and scenery
are excluded. Loading validates size, finite coordinates, triangle and grid
capacity limits, then indexes triangle bounds in a 32 × 32 grid of eight-meter
cells. The files belong beside each track's TCM1 and TCP1 exports.

**TCF1 font:** four-byte magic; `uint32 width,height`; 96 records of seven floats
(UV bounds, bitmap width, bitmap height, advance); one byte per atlas pixel.
The bundled Barlow fonts cover the game's English ASCII interface.

## Local data and lifecycle

Settings, best race times and medals live in `SDL_GetPrefPath("ToyCars", "ToyCars")`.
The versioned text profile is validated on load. Saving writes a temporary file
and renames it into place, leaving defaults usable after a malformed profile.
Track cards display each saved best race time and highest medal. The results
screen also shows the personal best and medal for the current circuit.
Diagnostic races never modify the profile's records or medals, so a later
settings save cannot persist a demonstration's result. Normal player finishes
continue to update and save both records and medals.

Each active finger retains its touch-device ID, finger ID and press origin.
Completed clicks retain their own press/release pair until the next UI frame;
other pointers cannot overwrite them. Focus and screen transitions clear held
inputs and queued clicks, and releases without an active press are ignored.

A lifecycle event watch records background/foreground transitions atomically.
The main loop pauses simulation, clears held touches, and pauses audio. A return
to the foreground leaves a running race at its pause screen. Gameplay never
advances using elapsed time spent in the background.

## Current boundaries

There is one car model with six colors, three tracks, one two-lap race mode and
local records. There are no weapons, network mode, campaign, upgrades, track
editor UI, replay files or cloud storage. Real-device thermal, battery and
frame-pacing measurements are still required before a public mobile release.
