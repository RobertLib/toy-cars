# Architecture

## Runtime

| File | Responsibility |
| --- | --- |
| `main.c` | SDL lifecycle, window, input events, fixed-step loop, gamepads, diagnostic CLI |
| `src/game.c` | Vehicle dynamics, AI, fuel, laps, ranking, collisions, particles and race state |
| `src/tracks.c` | The ten circuits and three cups: names, surfaces, climate, lighting, sounds, railway |
| `src/railway.c` | Level crossings: timetables, barriers, the trains' poses and their collisions with cars |
| `src/assets.c` | Checked binary loading, APK asset lookup, local profile persistence |
| `src/renderer.c` | OpenGL/GLES scene, shadow pass, follow camera, fog, postprocess, screenshot |
| `src/ui.c` | Batched vector/text UI, responsive layouts, multitouch tracking, screens |
| `src/audio.c` | Audio direction: music, engine model, positional effects, mix and limiter through SDL_AudioStream |
| `src/music.c` | Recorded soundtrack cues, race-aware sequencing, crossfades, loudness matching |
| `src/sfx.c` | Recorded sound effects: decoding, loudness table, voices, pitch, panning, streamed beds |
| `third_party/stb_vorbis` | Vendored public-domain Ogg Vorbis decoder used by the soundtrack and effects |
| `tools/build_assets.py` | Blender-only modeling, scene saving and geometry/path export |
| `tools/themes.py` | Every circuit's authored parameters: island, terrain, colours, counts, water, railway |
| `tools/layouts.py` | Corner polygons of the later circuits, rounded into spline knots |
| `tools/biomes.py` | Scenery, trees, landmarks and animals of the seven later environments |
| `tools/layout_svg.py` | Draws `screenshots/track-layouts.svg` from the exported tracks |
| `tools/railway.py` | Crossing sites, rail bed and tunnel hill grading, crossing hardware, rolling stock |
| `tools/build_sounds.py` | Rebuilds `assets/sounds` from recorded sources (numpy, scipy, oggenc) |

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
lateral acceleration. Grounded steering is limited by speed and available grip,
from 29 game meters per second squared on Highland Loch's asphalt to 20 on Alpine
Rush's ice (`TRACK_INFO[].corner_grip`).
Excess steering demand scrubs speed and widens the turn; braking restores a
tighter radius. Drift lowers lateral grip and permits more chassis rotation,
so the car slides rather than gaining a tighter full-speed trajectory. AI corner
speeds use the same surface limits with a margin for line corrections.
The ramp lip applies vertical
velocity; gravity integrates flight and landing. The world is 3D, while tire
contact follows the rendered road, shoulders and terrain: this is an arcade vehicle model, not a
general-purpose rigid-body engine.

Each environment uses its own layout. The Rookie Cup's three circuits are
spline knots (a country infield weave, three coastal lobes, alpine switchbacks
with a diagonal return; 0.96–1.09 km). The seven later ones are corner polygons
in `tools/layouts.py`, rounded with per-corner radii, resampled every 1.6 m by
arc length and smoothed vertically; each is longer than the one before:

| Cup | Circuit | Length | Crossings | Jumps | Corner grip | Time | Railway |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Rookie | Harvest Hills | 1.09 km | 3 | 3 | 28 | day | steam |
| Rookie | Sunshine Coast | 0.99 km | 3 | 3 | 24 | day | road train |
| Rookie | Alpine Rush | 0.96 km | 2 | 3 | 20 (ice) | night | mountain |
| Pro | Canyon Dash | 1.20 km | 1 | 3 | 23.5 | day | steam |
| Pro | Maple Woods | 1.33 km | 2 | 4 | 25.5 | day | mountain |
| Pro | Jungle Ruins | 1.58 km | 3 | 4 | 22 (mud) | night | steam |
| Master | Sakura Hills | 1.67 km | 1 | 4 | 27.5 | day | mountain |
| Master | Harbor Lights | 1.83 km | 2 | 4 | 29 | night | freight |
| Master | Highland Loch | 2.04 km | 2 | 5 | 24.5 | day | steam |
| Master | Volcano Peak | 2.47 km | 2 | 5 | 22.5 (ash) | night | steam |

The layout regression fits every pair under affine transforms, cyclic shifts
and reversed traversal, rejecting scaled, rotated or sheared duplicates, and
checks that the later circuits grow longer. Islands of the later circuits are
superellipses up to 450 × 454 m; their terrain is triangulated in rings about
3.4 m apart. Beside the road their cuttings and embankments are limited to
about 30 degrees, so a car leaving the road always meets drivable ground.
Bridge profiles use smooth approaches; their decks, undersides, railings and
piers are authored together. Terrain grading excludes elevated spans so the
lower route remains open. Piers stay clear of all lower carriageways.
Road projection includes vertical distance to distinguish crossing branches.
Ground queries select the highest support within two metres above the query,
ignoring overhead decks; the lowest surface remains a penetration fallback.
Cars and pickups on different levels cannot collide or collect across a bridge.

Driving surfaces are exported as collision triangles from the same Blender
meshes as the visible scene. A spatial grid limits height queries to nearby
triangles. Road, curb and shoulder meshes meet along slightly different
polylines; queries bridge the resulting slits up to 8 cm from a triangle, so a
tire never reaches the terrain or water beneath a seam. Four tire samples set
grounded height, pitch and roll at the car's
actual heading, including at spawn, recovery and after collision separation.
The terrain grades smoothly into the road bed; off-road contact does not extend
the road's elevation beyond its edges. Airborne cars still use gravity and
land on the surface below them.

A rollover needs rotation the ground actually imparts. Tires skid before
cornering load can lever the chassis over: friction carries at most the normal
load, so only a steep bank adds enough side load. Crossing a crest or dropping
off an edge, the suspension's pitch and roll rates are capped at about 1 rad/s,
since only gravity tips the body; a side that a ramp pushed up keeps its roll
rate where its support ends abruptly. A landing the suspension absorbs reseats
the chassis on the surface before wheel support is judged.

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
fuel consumption and pickup decisions. Six evenly spaced cans per circuit sit
near alternating road edges, restore 18 percentage points within a 1.5-meter
collection radius, and have a 35-second per-driver cooldown so a lead car
cannot deny the entire field access to fuel.

Rivals adapt to the player's race distance so neither side drives out of sight.
Within 15 meters they race at their difficulty pace; beyond that the adjustment
grows over the next 100 meters. A rival ahead lowers its pace by up to
40 / 30 / 15 percent on easy / normal / hard. A rival behind gains up to
12 / 14 / 18 percent engine power (throttle above 1, AI only) and corners at up
to 1.0 / 1.10 / 1.14 times normal pace, which stays below the tire limit. Ice,
mud and volcanic ash recover grip too slowly for that, so on Alpine Rush, Jungle
Ruins and Volcano Peak catch-up never raises corner pace above 1.0. Jump takeoff speeds are unchanged. Adaptation stops once the player
finishes, so the remaining classification settles at regular pace.

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

## Railways and level crossings

Each circuit crosses one railway route at grade. Harvest Hills and Alpine Rush
have a short line between two tunnel portals; a run starts deep inside one
tunnel and ends inside the other, and runs alternate direction. Sunshine Coast
has a closed promenade for a road train, which waits at a stop at each end and
crosses the road once on each leg. The route, the path distances of its passes
and stops, the stop line, the barrier hinges and the signals are authored with
the scene and exported as TCR1.

Everything follows from a timetable on the railway's own clock, which starts
with the countdown so that it reads race time. A train run lasts its route plus
its own length at a constant speed (15 m/s steam, 12 m/s mountain railway), and
the next one starts 28–48 / 30–50 s later; the first arrives 26–44 s into the
race, after the opening lap's field. The road train accelerates and brakes at
2.2 m/s² to 7 m/s and waits 34 s at each stop; its timetable starts at a random
phase. A window is the time any vehicle overlaps the road plus a margin along
the route. Around each window the signals flash 5.5 s ahead, the arms start to
fall 4 s ahead and take 3 s, stay down 0.6 s after the last vehicle and take
2.5 s to rise, so arm and signal states are pure functions of time. While the
arms are more than 70% down their segments join the bridge railings in the car
collision sweep. Train vehicles are oriented boxes placed by two bogie points on
the route; a car circle inside one is pushed out and given the train's velocity
plus 30% restitution, and the nose deflects a car sideways instead of pushing it
along. `car_impact` decides whether the blow rolls the car.

Rivals check the next window when the stop line is within 150 m. They go if
their current speed (at least 6 m/s) clears the crossing 0.4 s before the arms
are half-way down, if they would arrive after the arms are half-way up again,
or if they could not stop in front of the barrier anyway. Otherwise they brake
at 14 m/s² to the line or 5.4 m behind the car ahead in their lane and hold the
brake, so waiting never counts as being stuck. Near the crossing every car
follows slower traffic in its lane, so a queue drains in order.

The menu broadcast's pack never slows down, so its train is timed around it:
the arms are up again just before the leader's camera reaches the barrier, every
other lap (every lap for the road train, whose two stops are lengthened to fit).
`--train-at SECONDS` sends the first train to the crossing at a chosen race time
for demonstrations and screenshots.

## Rendering and content

Blender evaluates bevel and weighted-normal modifiers and exports triangles.
Static environment geometry is merged into one vertex buffer per track, sorted
into 40 m ground tiles with their bounds (`TiledMesh`): the color and shadow
passes draw only the tiles inside their frustum, joining neighbouring tiles into
single draws. Only the selected circuit's meshes are resident; changing tracks
replaces them (10–20 ms on a desktop). Static scenes hold 110–195 thousand
triangles, plus separate vegetation, crowd, wildlife and water batches. A frame
renders a depth shadow pass and a colored scene pass; the sky then fills the
remaining background at the far plane. The scene target is `R11F_G11F_B10F`, or
`RGBA16F`, where the GPU can render to floating point, so lamps and glints can
exceed white; OpenGL ES devices without the extension get an 8-bit scene with a
lower bloom threshold. The shadow frustum moves in whole shadow-map texels and
is sampled with hardware comparison filtering.

Post-processing then works from the scene's color and depth textures:

- Half-resolution scalable ambient obscurance with a depth-aware 4x4 blur.
  Depth is read with `texelFetch`: half-resolution pixel centers fall exactly
  between full-resolution ones, and filtered reads round differently from row
  to row, which broke the reconstructed normals into bands.
- A five-level dual-filter bloom pyramid that starts at half resolution.
- In the menu only, a half-resolution depth of field focused on the filmed cars.
- A composite pass with the speed blur, a filmic highlight shoulder, a light
  grade, vignette and ordered dither. It writes the luma that FXAA 3.11 needs
  before the result reaches the window. `renderer_present` replays these two
  passes, and the speed blur test uses it.

Glossy materials (paint, glazing, aluminum, rubber) and asphalt are recognized
by their exported vertex colors, so the mesh formats are unchanged. One warm
color (0.98, 0.80, 0.40) marks lanterns, torches and lit windows, which glow at
night. Terrain paint values -1 to -10 select each environment's ground shading
(meadow, sand, snow, red desert, leaf litter, jungle floor, spring lawn with petals,
paving, heather moor and volcanic ash). Phones take
8 occlusion samples and 16 depth-of-field taps; desktops take 12 and 24.
The UI uses a separate batched triangle stream and single-channel glyph atlases.
The GL and GLES shader variants share their shader bodies.

Each environment includes 15 to 24 irregular spectator groups near the start,
bends and jumps. Blender authors low-poly figures with five body types, five cheering
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

Night racing is a uniform switch on the existing passes, with no extra render
targets. The moon reuses the sun's direction and shadow map with a cool, dim
color and darker hemisphere ambient; sky, fog and overcast haze use night tints.
Car transforms are interpolated once per frame and shared by the shadow pass,
color pass and lamps. Each car contributes one forward low-beam cone (tipped
towards the road, with softened facing so grazing asphalt still lights) and a
short red tail-lamp spill, evaluated per fragment for all eight cars. Emissive
lens boxes add one draw per car, and lamp halos are one additive instanced draw
after the spray, using a reserved region of the spray buffer. Water, weather and
spray dim their fixed colors; the garage always renders by day. On a lava
track (Volcano Peak) an ember color lights the night: a red glow along the
horizon and in the haze, and warm bounce light on the ground. Only liquid above
the surrounding sea (`LAVA_ABOVE`) is lava: it is shaded as a glowing, crusted
melt brighter than white, throws no spray, and a tire touching it recovers the
car to its last legal road position.

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
Water layout does not depend on a fixed road index. Ground-level roads dip to
meet the stream bed, including crossfall, and curbs/dashes stop at the ford.
Elevated bridge spans leave the channel below them intact. Ground refinement
is local to narrow channels and banks. Scene metadata
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

**TCR1 railway:** four-byte magic; `uint32` kind (0 steam, 1 road train,
2 mountain railway, 3 container freight), closed-route flag, point, pass, stop, arm and light counts;
six floats: race distance of the crossing centre, stop line and far side, the
pass zone half-length and the visible part of the route; route points (XYZ, about
0.8 m apart, 0.6 m for the promenade); path distances of the passes and stops;
six floats per barrier arm (hinge XYZ, lowered direction XZ, length) and per
signal (lamp centre XYZ, facing XZ, half spacing). Lengths along the route are
measured at load time.

Tunnel hills are part of the island heightfield: the rail corridor is graded to
the formation with retaining walls by each portal, a dome rises over the hidden
track, and the step from the formation up to the dome, 1.2 m behind the portal
face, is split off and rendered black as the tunnel's interior. Hills ease down
towards roads and streams. The island mesh is subdivided along the corridor
(1.2 m, 0.6 m by the portals). Rails and panels on the pavement are clipped to
the road's triangles like the other paint.

**TCS1 driving surface:** four-byte magic; `uint32 triangle_count`; nine floats
per triangle (three XYZ vertices). Terrain, road, shoulders, curbs and ramp
decks use their exact rendered triangles. Vertical faces, paint and scenery
are excluded. Loading validates size, finite coordinates, triangle and grid
capacity limits (262 144 triangles, coordinates within ±256 m), then indexes
triangle bounds in a 64 × 64 grid of eight-meter cells. The arrays are
allocated per track to its actual size, and the extent of each island is
measured at load. The files belong beside each track's TCM1 and TCP1 exports.

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

There is one car model with six colors, ten tracks in three cups, Championship progression,
Arcade single races and local records. Both modes use two-lap races. There are
no weapons, network mode, upgrades, track
editor UI, replay files or cloud storage. Real-device thermal, battery and
frame-pacing measurements are still required before a public mobile release.

## Championship progression

The ten circuits form three cups in Championship order (`CUPS` in
`src/tracks.c`): the Rookie Cup (Harvest Hills, Sunshine Coast, Alpine Rush),
the Pro Cup (Canyon Dash, Maple Woods, Jungle Ruins) and the Master Cup (Sakura
Hills, Harbor Lights, Highland Loch, Volcano Peak). Each cup is its own series.

`Profile.championship_completed` retains permanent track unlocks (0–9): every
track up to that index is open. Per cup, `championship_stages[cup]` counts the
scored stages of its current series, `championship_eliminated[cup]` records an
elimination and `championship_medal[cup]` the best overall medal won.
`championship_places[track][driver]` stores all classifications (0 means DNF).
Points are derived as 10 / 8 / 6 / 5 / 4 / 3 / 2 / 1, with zero for DNFs.
Ties compare the number of wins, second places and so on, then stable driver order.
The HUD projects the current race into the cup's overall standings. After the
player finishes, rivals keep racing; advancement and final medals wait for every
rival to finish or retire. Scoring happens once and saves the full classification.
Every stage but a cup's final requires a top-four finish to advance and unlock the
next track. A lower finish or DNF eliminates the player from that cup's series.
The failed stage still saves its classification and any finish record, but cannot
award an overall medal even if the accumulated points place the player in the top three.
After a cup's final, the overall top three earn gold, silver or bronze; other
places lose. A medal in a cup unlocks the first track of the next cup. Starting a
finished or eliminated cup again clears its classifications but preserves unlocks,
records and medals; the other cups' series are untouched. Championship always
starts the next unscored stage of the selected cup; Arcade permits single-track
replays of every unlocked track.

Profile version 8 stores ten tracks: ten best times and medals, the unlock
index, a `CHAMPIONSHIP <cups>` line followed by one line per cup (stages,
medal, eliminated), one line of eight places per track, `OPTIONS` and the
records of all ten tracks. Loading checks that each cup's series lies within
the unlocked tracks. Versions 1–7 describe the first three circuits and import
as the Rookie Cup: their records, medals and series carry over, and a completed
championship with a medal unlocks Canyon Dash, the first track of the Pro Cup.

Profile version 7 adds an `OPTIONS` line with the Arcade time of day before the
records; a missing or out-of-range value is treated as a malformed profile.
Versions 1–6 load with daytime Arcade races. Championship lighting is not saved:
`TRACK_INFO[].night` fixes it per stage, and `game_night` resolves the current
race (or menu preview) from the mode, the selected track and that choice.

Profile version 6 also persists elimination separately from permanent unlocks.
Version 5 retains its current series without retroactively applying elimination.
Versions 1–4 retain their existing records, settings and
unlocks, but start a fresh points series because rival classifications were not saved.
Version 1 imports consecutive podium medals as permanent unlocks.

`ToyCarsProgressionTests` uses an isolated profile directory to check results,
unlocks, retries, mode separation, persistence and migration. Graphics tests
exercise the mode buttons, locked previews and Next Stage flow and capture the
menu, unlock notification and completed championship.
