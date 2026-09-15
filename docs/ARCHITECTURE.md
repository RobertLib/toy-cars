# Architecture

## Runtime

| File | Responsibility |
| --- | --- |
| `main.c` | SDL lifecycle, window, input events, fixed-step loop, gamepads, diagnostic CLI |
| `src/game.c` | Vehicle dynamics, AI, fuel, laps, ranking, collisions, particles and race state |
| `src/assets.c` | Checked binary loading, APK asset lookup, local profile persistence |
| `src/renderer.c` | OpenGL/GLES scene, shadow pass, follow camera, fog, postprocess, screenshot |
| `src/ui.c` | Batched vector/text UI, responsive layouts, multitouch tracking, screens |
| `src/audio.c` | Gameplay music direction, engine and event audio through SDL_AudioStream |
| `src/music.c` | Seven composed scores, bar-aligned sequencing and adaptive stereo synthesis |
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
speed; a surface-specific grip coefficient damps lateral velocity, with a cap on
lateral acceleration. Grounded steering is limited by speed and available grip
(28 / 24 / 20 game meters per second squared for country / beach / winter).
Excess steering demand scrubs speed and widens the turn; braking restores a
tighter radius. Drift lowers lateral grip and permits more chassis rotation,
so the car slides rather than gaining a tighter full-speed trajectory. AI corner
speeds use the same surface limits with a margin for line corrections.
The ramp lip applies vertical
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
neighborhood when off-road. Beyond a 0.5-meter road-edge tolerance, race progress
stays provisional at the last legal position. Separate counters track signed
road advancement, actual horizontal distance driven and excursion duration,
including leaving and rejoining the road. A rejoin triggers a penalty only when
the actual route saves more than 8 meters AND 20 percent of the bypassed road
distance. Excursions lasting at most one second are forgiven unless they save
more than 24 meters; this permits quick corrections in tight bends while still
catching substantial cuts at speed. The timer clears on rejoining or recovery.
A penalty returns the car to the last legal road distance, centered and stopped,
with a shortcut message.
Ordinary shoulder excursions, including a one-second exit and return, retain
their progress because forward advancement alone is not evidence of a shortcut.
The same rule applies to every driver; neither manual reset nor island recovery
can keep unvalidated shortcut progress. Checkpoints, laps and finishes are
validated on rejoining, and spent time and fuel are not refunded. Leaving the
island's driving surface recovers the car without awarding progress or fuel.
Scenery is visual dressing, not an
additional collision world.

Each AI driver uses a speed-dependent lookahead target and compensates for lateral
slip. Samples along the braking horizon set corner speeds and braking distances;
jumps into sharp bends get a lower takeoff speed. Drivers choose an unoccupied
passing side based on traffic position, including lapped cars. Low fuel changes
the target toward an available can, with a shorter lookahead and a slower
approach for precise collection within the tire grip limit.
All drivers, including the demonstration player, use the same vehicle update,
fuel consumption and pickup decisions. Four cans per circuit sit near alternating
road edges, restore 18 percentage points within a 1.5-meter collection radius,
and have a 35-second per-driver cooldown so a lead car cannot deny the entire
field access to fuel.

Track projection searches a bounded neighborhood around the previous segment.
Signed, unwrapped progress and sequential quarter-lap gates prevent finish-line
reversing from counting laps. Recovery preserves distance, checkpoints and fuel.
Lap numbers derive from completed gates and never decrease, so recrossing an
already completed lap cannot restart its timer. Landing reduces the velocity
vector itself and updates the displayed speed from that vector.
After the player finishes, rivals continue to settle the result table.
Finishers keep their velocity through the line, then use the regular steering
and tire physics to slow down for a cooldown lap along the right side of the
road. They keep gaps to traffic and remain part of collision handling, including
on the results screen. Their race progress, timing, fuel and pickup/jump counts
stay frozen; the race clock stops once all rivals are classified.

## Rendering and content

Blender evaluates bevel and weighted-normal modifiers and exports triangles.
Static environment geometry is merged into one vertex buffer per track. A race
uses a depth shadow pass, a colored scene pass and a final edge-smoothing pass.
The UI uses a separate batched triangle stream and single-channel glyph atlases.
The GL and GLES shader variants share their shader bodies.

Each environment includes 15 irregular spectator groups near the start, bends
and jumps. Blender authors low-poly figures with five body types, five cheering
or photography poses, varied skin/clothing colors and seasonal accessories.
Placement rejects scenery footprints and nearby road segments, and grounds each
shoe on the triangulated island. A separate crowd batch carries each figure's
origin, orientation, body type and arm/cloth assignments. Shared vertex-shader
animation drives waving, clapping, hopping, body sway and flag flutter in both
the color and shadow passes, with individual phases and tempos. One extra draw
per pass renders the entire crowd; no per-person draw calls or CPU vertex
uploads are needed. The animation clock stops on pause. Figures remain visual
scenery without collisions. A local random generator keeps crowd generation
independent of the other scenery.

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

**Water placement:** `tools/hydrology.py` traces valley drainage along the negative
terrain gradient from authored hillside springs, then authors nonperiodic erosion
bends without changing the descending water profile. The resulting channel is
excavated into the terrain, including road grading. Meadow, sand and rock channels
have different bend controls, width profiles and bank dressing; the coastal creek
widens towards its tidal outlet. The country pond floods a connected depression
with sediment shelves and promontories at one horizontal level. Its contour is
smaller and non-convex; coastal and alpine streams continue to the boundary.
Water layout does not depend on a fixed road index. Roads dip to meet the stream
bed where they intersect it, including crossfall, and curbs/dashes stop at the
ford. Ground refinement is local to narrow channels and banks. Scene metadata
records source-to-outlet profiles for independent geometry validation.
Ford approaches also have buried soil support matching the road triangles;
it is restricted to the water corridor to avoid overlapping distant hairpins.

**Water meshes:** `<track>-water.tcm` uses the TCM1 layout, with the color
slots storing shoreline depth and current XZ instead of RGB. Blender exports
water separately from opaque geometry and car-support surfaces. The game indexes
its triangles in the same 8-meter spatial grid as the ground. Each grounded
wheel checks water height against the road/terrain before emitting spray;
water never replaces the solid road height.

After opaque rendering, color and depth are blitted into separate textures.
The water pass combines depth absorption, offset refraction with foreground
rejection, Fresnel sky reflections, short screen-space reflection rays, sun
highlights, moving normals, small vertex waves, caustics and shallow edge foam.
Normals use independently advected noise slopes with subpixel filtering; still
ponds have gentler ripples than currents. Air/water Fresnel reflectance starts
at 2.04%, without an added white reflection layer. Light-path absorption reveals
shallow beds and gives deeper water its color. Sun glints and caustics are
restrained, and edge foam is limited to flowing water.
The saved opaque textures avoid framebuffer feedback on both GL and GLES.
Reflections fall back to sky where scene geometry is off screen. Spray and
surface rings use depth-tested, sorted instanced alpha quads. All GPU water
resources follow resize, destruction and context restoration paths.

**TCP1 track:** four-byte magic; `uint32 point_count`, `uint32 can_count`,
`uint32 ramp_count`, `float lap_length`; seven floats per path point (XYZ,
direction XZ, road half-width, cumulative distance); two floats per can
(point index, lane); four floats per ramp (point index, length, height, half-width).

The path data and visible ramp meshes come from the same Blender authoring step.
Start-grid bars follow the same track distances and lanes as the starting cars,
with each bar just ahead of its car. Grid bars and finish checkers are clipped
to the road's actual triangles and raised slightly above them, so they follow
curves, elevation and banking through the lap seam.

**TCA1 animated crowd:** four-byte magic; `uint32 vertex_count`; eighteen floats
per vertex: the ten TCM1 floats, world origin XYZ, yaw, pose (cheer/wave/clap/
camera/flag = 0–4), body part (body/left arm/right arm/flag cloth = 0–3), scale
and width. Each `<track>-crowd.tca` is separate from the static track mesh to
avoid duplicate figures. Loading validates finite values and animation ranges.

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

Settings, best race times, track leaderboards, finish history, medals and Championship progress live in
`SDL_GetPrefPath("ToyCars", "ToyCars")`.
The versioned text profile is validated on load. Saving writes a temporary file
and renames it into place, leaving defaults usable after a malformed profile.
Track cards display each saved best race time and highest medal. The results
screen also shows the personal best and medal for the current circuit.
Diagnostic races never modify the profile's records or medals, so a later
settings save cannot persist a demonstration's result. Normal player finishes
continue to update and save both records and medals.

Profile version 3 adds ten ranked entries and thirty recent finishes per track,
plus remembered A–Z initials and a monotonic entry ID. Each entry stores the race
and best-lap times, local-display timestamp, difficulty, mode, finishing position,
record flag and improvement over the preceding best. Times are ranked at
millisecond precision; ties retain the earlier entry. Both race modes and all
three difficulties share the board, with their settings visible in each row.
Versions 1 and 2 seed the tables with their existing best times (`---` initials,
unknown date/settings). Malformed record extensions fall back to these saved
best times while preserving valid settings and Championship progress.

Profile version 4 also stores whether initials were explicitly confirmed.
Player labels use `YOU` until confirmation, then use the chosen initials in
the race HUD and results. Reset clears this flag. Version 3 profiles with
non-default initials keep their name; ambiguous legacy `AAA` defaults to
unconfirmed. Explicitly choosing `AAA` in version 4 remains confirmed on reload.

A valid player finish is saved immediately with the last initials, protecting
it if the app quits during name entry. Championship asks for initials after a
completed opening track, updates both record copies by ID, and then shows the
player/rival classification. Later stages save quietly using the remembered
initials. Its primary action, Enter and gamepad A start the next unlocked stage
after a qualifying finish. Track-record buttons are shown only in Arcade.
Arcade asks for initials after every finish and then opens the board with the
run highlighted (recent history if it missed the Top 10). Failed saves offer
retry or continue without saving.
Keyboard and gamepad name-entry events are consumed before global shortcuts;
touch uses three letter selectors. The record screen keeps its return screen
and permits switching tracks only when entered from track selection.

Each active finger retains its touch-device ID, finger ID and press origin.
Completed clicks retain their own press/release pair until the next UI frame;
other pointers cannot overwrite them. Focus and screen transitions clear held
inputs and queued clicks, and releases without an active press are ignored.

A lifecycle event watch records background/foreground transitions atomically.
The main loop pauses simulation, clears held touches, and pauses audio. A return
to the foreground leaves a running race at its pause screen. Gameplay never
advances using elapsed time spent in the background.

## Current boundaries

There is one car model with six colors, three tracks, Championship progression,
Arcade single races and local records. Both modes use two-lap races. There are
no weapons, network mode, upgrades, track
editor UI, replay files or cloud storage. Real-device thermal, battery and
frame-pacing measurements are still required before a public mobile release.

## Championship progression

`Profile.championship_completed` retains permanent track unlocks (0–3).
`championship_stages` counts scored stages in the current series, while
`championship_places[stage][driver]` stores all classifications (0 means DNF).
Points are derived as 10 / 8 / 6 / 5 / 4 / 3 / 2 / 1, with zero for DNFs.
Ties compare the number of wins, second places and so on, then stable driver order.
The HUD projects the current race into the overall standings. After the player
finishes, rivals keep racing; advancement and final medals wait for every rival
to finish or retire. Scoring happens once and saves the full classification.
The first two stages require a top-four finish to advance. A lower finish or DNF
sets `championship_eliminated`, ends the series and does not unlock the next track.
The failed stage still saves its classification and any finish record, but cannot
award an overall medal even if the accumulated points place the player in the top three.
The final overall top three earn gold, silver or bronze; other places lose.
`championship_medal` retains the best overall medal across completed series.
Starting another series clears its classifications but preserves unlocks and records.
Championship always starts the next unscored stage; Arcade permits single-track replays.

Profile version 6 also persists elimination separately from permanent unlocks.
Version 5 retains its current series without retroactively applying elimination.
Versions 1–4 retain their existing records, settings and
unlocks, but start a fresh points series because rival classifications were not saved.
Version 1 imports consecutive podium medals as permanent unlocks.

`ToyCarsProgressionTests` uses an isolated profile directory to check results,
unlocks, retries, mode separation, persistence and migration. Graphics tests
exercise the mode buttons, locked previews and Next Stage flow and capture the
menu, unlock notification and completed championship.
