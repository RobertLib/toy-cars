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
contact follows the road surface: this is an arcade vehicle model, not a
general-purpose rigid-body engine.

Two overlapping circles approximate each car's oriented collision capsule.
Contact separates cars and applies a damped normal impulse. The road shoulder
slows cars and its outer boundary keeps the race recoverable. Scenery beyond
that boundary is visual dressing, not an additional collision world.

Each AI driver uses a speed-dependent lookahead target. Upcoming curvature
sets a desired speed, nearby traffic changes the chosen lane, and low fuel
changes the target toward an available can. All drivers use the same vehicle
update and consume fuel. Pickups have a per-driver cooldown so a lead car
cannot deny the entire field access to fuel.

Track projection searches a bounded neighborhood around the previous segment.
Signed, unwrapped progress and sequential quarter-lap gates prevent finish-line
reversing from counting laps. Recovery preserves distance, checkpoints and fuel.
After the player finishes, rivals continue to settle the result table.

## Rendering and content

Blender evaluates bevel and weighted-normal modifiers and exports triangles.
Static environment geometry is merged into one vertex buffer per track. A race
uses a depth shadow pass, a colored scene pass and a final edge-smoothing pass.
The UI uses a separate batched triangle stream and single-channel glyph atlases.
The GL and GLES shader variants share their shader bodies.

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

**TCF1 font:** four-byte magic; `uint32 width,height`; 96 records of seven floats
(UV bounds, bitmap width, bitmap height, advance); one byte per atlas pixel.
The bundled Barlow fonts cover the game's English ASCII interface.

## Local data and lifecycle

Settings, best race times and medals live in `SDL_GetPrefPath("ToyCars", "ToyCars")`.
The versioned text profile is validated on load. Saving writes a temporary file
and renames it into place, leaving defaults usable after a malformed profile.

A lifecycle event watch records background/foreground transitions atomically.
The main loop pauses simulation, clears held touches, and pauses audio. A return
to the foreground leaves a running race at its pause screen. Gameplay never
advances using elapsed time spent in the background.

## Current boundaries

There is one car model with six colors, three tracks, one two-lap race mode and
local records. There are no weapons, network mode, campaign, upgrades, track
editor UI, replay files or cloud storage. Real-device thermal, battery and
frame-pacing measurements are still required before a public mobile release.
