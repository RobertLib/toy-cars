# Validation

Executed locally on 15–18 September 2026. These are observed results, not a claim
of App Store readiness or performance on untested physical devices.

## Distinct natural watercourses — 18 September 2026

- Replaced near-straight, constant-width channels with different erosion bends
  and bank profiles for meadow, coastal sand and alpine rock. The country pond
  now follows a smaller, indented contour around sediment shelves; the coastal
  stream broadens towards the sea instead of ending in a matching pond.
- Bank dressing is clustered and specific to each environment. Reeds, bare sand
  and rock constrictions replace the shared ring of evenly scattered pebbles.
- Grading seats each road cross-section below narrow, oblique streams and joins
  their beds to the terrain. Local soil support follows the ford pavement's
  topology so coarse shared terrain vertices do not leave voids under its bends.
  The layout test now checks channel sinuosity, width
  variation and non-convex pond shores, alongside downhill flow, continuous
  exported water, level pond surfaces and unobstructed fords.
- Changed traffic exposed a critical-fuel AI case: overtaking could override a
  planned fuel line until only 30 metres remained. Critical-fuel drivers now
  retain that line for the full 60-metre approach. A regression checks that
  nearby traffic does not change the fuel approach steering on every circuit.
- Final checks passed: all five Release CTests, ASan/UBSan simulation and
  progression, and Blender water-layout, ground-contact, marking and spectator
  checks. Exported collision counts remain within the existing 32,768 limit:
  country 30,207; beach 24,925; winter 24,699.
- Reviewed all three watercourses in-game; captures are `screenshots/watershed.png`,
  `screenshots/beach-waterway.png` and `screenshots/winter-waterway.png`.
  Rebuilt the macOS app bundle and iOS Simulator app with the final assets.

## Water material refinement — 18 September 2026

- Removed the regular white highlight lattice and constant milky reflection
  layer. Irregular, filtered wavelets distinguish still ponds from currents;
  depth absorption, clearer shallows and smaller sun glints replace the plastic
  appearance. Flowing-water foam and wheel splashes remain.
- All five Release CTest entries passed. The pond readback sampled 1,158
  positions: none had the old broad white sheen, and 463 changed between frames.
  Water animation and pause checks passed on all three tracks. The desktop
  build completed without warnings.
- Visually reviewed the three fords, whole country catchment and a closer pond
  view. Updated previews: `screenshots/water.png`, `screenshots/watershed.png`
  and `screenshots/pond.png`.
- Rebuilt `dist/ToyCars.app` and the iOS Simulator app. The updated shaders
  compiled successfully on the iPhone 17 simulator's OpenGL ES 3.0 renderer;
  checked race startup as a compatibility smoke test.

## Terrain-led water placement — 18 September 2026

- Replaced the repeated paired-pool layout with continuous terrain-gradient
  drainage. The country spring flows about 74 metres into a separate closed
  depression; its pond follows a connected elevation contour at one water level.
  Coastal and alpine channels run downhill to the landscape boundary.
- Each route has one short ford, at a different location. Road elevation and
  crossfall meet the channel bed; curbs and paint stop at the crossing. Ground
  refinement stays local to narrow channels and shores, within runtime limits.
- `tests/water_layout.py` checks downhill profiles, uninterrupted rendered
  channels, road-crossing count, unobstructed fords, level pond water and distance
  between the road, spring and pond/outlet. Ground-contact, marking and spectator
  geometry checks also passed on all three scenes.
- Simulation tests now find real fords from the exported geometry and check each
  wheel's local waterline, including cross-sloped roads. Full-field races at all
  difficulties passed. Critical-fuel rivals approach pickups more slowly to
  recover safely when the changed traffic order makes a pickup harder to reach.
- Release graphics and ASan/UBSan simulation/progression checks passed. Captures
  include `water-country-catchment.bmp` for reviewing the source/ford/pond layout.

## Water and fords — 18 September 2026

- Added separate liquid meshes to all three tracks, shaped beds, bank stones,
  reeds, shallow road crossings and an animated coastal sea. Water uses depth
  absorption, refraction, screen-space/sky reflections, waves and bank foam.
- All five Release CTest entries passed. Water regressions cover wet/dry road,
  moving/stopped/airborne wheels, spray and ripple emission, pause and restart.
  Simulation and progression also passed under ASan/UBSan.
- Desktop graphics readback verifies animated water pixels on each track, frozen
  paused frames and no OpenGL errors, alongside the existing context-recreation
  and viewport-resize checks. Captures: `build/graphics-test-profile/water-*.bmp`;
  the country ford preview is `screenshots/water.png`.
- Blender checks for ground contact, road markings and all 249 spectators passed
  after regenerating the three source scenes and runtime exports.
- The macOS application bundle and iOS Simulator build succeeded. A 120-frame
  race ran on the iPhone 17 simulator using OpenGL ES 3.0; the ford and ripples
  were visually checked. No physical phone performance measurements were made;
  reflections of off-screen geometry use the procedural sky fallback.

## Championship elimination — 18 September 2026

- Stages 1 and 2 require a top-four finish. Fifth through eighth and DNFs end
  the run without unlocking another track, even if the player still leads overall.
- Profile v6 saves elimination; v5 series remain resumable. A new run clears
  points while preserving unlocked tracks, previous medals and race records.
- Release build, progression, simulation and graphics tests passed. Regression
  coverage includes both qualifying stages, every losing place, DNFs, reload,
  restart, legacy saves and retrying a failed save after elimination.
- Desktop, short-window and mobile elimination screens were captured. Graphics
  checks required access to the macOS display outside the sandbox.

## Mobile interface — 17 September 2026

- Replaced scaled desktop compositions on mobile with dedicated race selection,
  garage, touch help, settings, four-row record pages and race results. Enlarged
  initials controls, pause actions, driving controls, recovery and the race HUD.
- All five Release CTest entries passed, including desktop regressions and new
  mobile graphics/input checks. Simulation and touch tests also passed under
  AddressSanitizer/UndefinedBehaviorSanitizer.
- Mobile input checks cover four phone sizes, asymmetric safe insets in both
  landscape directions, at least 44-point primary/navigation targets and large
  thumb controls. Opposite outer corners support simultaneous steering/drift,
  and canceling one finger leaves the other active.
- Graphics integration exercises locked-track previews, Championship/Arcade
  switching, start, navigation, color/difficulty/settings, pause/resume/recovery,
  initials, save-failure continuation, record pagination (touch and keyboard),
  replay and Championship continuation only after rivals finish.
- The iOS Simulator app built successfully and ran on the iPhone 17 simulator
  (2622 x 1206 landscape surface). Menu and racing safe areas were visually
  checked; simulator captures are under `build/iphone17-*.png`. Detailed mobile
  state captures are under `build/graphics-test-profile/mobile-*.bmp`.
- Follow-up navigation changes passed graphics and simulation regressions. Checks
  cover the timed page fade, a moving active-tab underline, rapid retargeting,
  settling and cancellation on entry into gameplay. The revised navbar,
  transition midpoint and safe-area-centered pause dialog were visually checked
  in `mobile-navbar.bmp`, `mobile-transition.bmp` and `mobile-pause.bmp` under the
  graphics test profile. The updated iOS Simulator build also succeeded.
- No physical-phone ergonomics or performance measurements were performed.
  Android and the signed physical-iPhone package were not rebuilt in this pass.

## Finish cooldown laps — 17 September 2026

- Cars preserve motion through the finish and gradually slow down on the right
  side of the track, with traffic following and collisions active during results.
- Release simulation, progression, music, asset-update and graphics tests passed.
  Simulation and progression also passed under ASan/UBSan. Graphics checks needed
  display access outside the sandbox.
- New simulations on all three tracks cover a rival finishing before the player,
  continuous deceleration, ongoing movement after the whole field is classified,
  and more than a full cooldown lap without changing race statistics or ranking.

## Cornering grip — 17 September 2026

- Grounded cars now have surface-specific lateral acceleration and steering
  limits. Excess steering at speed widens the turn and scrubs speed; braking
  restores tighter cornering. Drift increases slip and costs speed. AI uses the
  same grip limits, looks farther ahead and slows for precise fuel collection.
- Handling regressions cover both steering directions on all three surfaces,
  fast versus slow cornering, braking, drift, gentle corrections, straight-line
  top speed and consistency at 120 / 240 Hz. In the one-second full-throttle
  test, a sharp turn reduces speed from 33 to about 28 game meters per second;
  straight-line speed stays above 33.
- Release simulation and progression tests and their ASan/UBSan counterparts
  passed, including full eight-car races on every track at all difficulties.
  Music, asset updates and desktop graphics checks also passed. The graphics
  test required access to the macOS display outside the sandbox.
- The normal-AI race-time bound now permits the slower grip-limited corners;
  difficulty checks still require normal to beat easy by 10 percent and hard
  to beat normal. Fuel and full-field finishing assertions remain in place.
- The macOS app was rebuilt. Mobile packages were not rebuilt in this pass;
  handling feel still needs hands-on playtesting.

## Overall Championship standings — 17 September 2026

- Championship now accumulates points for all drivers across three stages;
  every stage advances, including off-podium finishes and DNFs. The HUD projects
  overall position and results show the series points table. Final overall
  places 1–3 win gold, silver and bronze; places 4–8 lose.
- Profile v5 persists each stage classification, series progress and best overall
  medal. Legacy profiles retain track unlocks and records and begin a new series.
- All five Release CTest entries passed. Progression and simulation also passed
  under ASan/UBSan. Coverage includes all eight final ranks, a final-race win
  without an overall medal, a final-race fourth place with an overall silver,
  DNFs, tie countback, pending rivals, reload, fresh series, demo isolation and
  save failures. Graphics checks cover touch, keyboard and gamepad continuation.
- Desktop and 1600 × 450 captures for gold, silver, bronze and failure are under
  `build/graphics-test-profile/championship-{gold,silver,bronze,lost}*.bmp`.
  The macOS app was rebuilt and its local signature verified. Mobile packages
  were not rebuilt in this pass.

## Lifecycle and settings-save fixes — 17 September 2026

- Background and foreground callbacks now retain a pending pause independently
  of the latest visibility state. The event loop consumes callbacks during and
  after event polling, discards input from the transition frame and continues
  polling even when the surface has no size. Returning to the foreground never
  implicitly resumes a race or countdown.
- Settings, difficulty, car colors and the keyboard sound shortcut now use the
  same retry/continue-unsaved warning as race results. The warning blocks the
  covered controls. A failed shortcut save during gameplay pauses the race;
  retrying saves without also resuming it. A later successful save includes
  previously acknowledged unsaved changes.
- All five Release CTest entries passed, including the expanded real-event-loop
  graphics checks. All four headless entries passed under ASan/UBSan. Static
  analysis of the changed C sources and `git diff --check` passed.
- New regressions cover coalesced lifecycle callbacks between frames and during
  event polling, countdown/race preservation, stale keyboard/gamepad/touch input,
  deliberate resume, every settings toggle, difficulty, garage colors, failed
  retries, warning input blocking, persistence after retry and the M shortcut.
  Captures include `settings-save-failed.bmp`, `garage-save-failed.bmp` and
  `mute-save-failed.bmp` under `build/graphics-test-profile/`.
- The macOS application was rebuilt and its local signature verified. The
  Android ARM64 debug APK was rebuilt offline, installed and launched in the
  16 KB-page-size emulator. A background/foreground round trip returned to the
  pause menu; `build/android-review-paused.png` captures that state. README now
  documents how to generate the APK rather than assuming an ignored build
  artifact is always present.
- The iOS ARM64 Simulator app was rebuilt and rendered a 120-frame diagnostic
  countdown using GLES 3.0 without OpenGL/presentation errors. The screenshot
  is `build/ios-review-race.png`. SDL's UIKit wrapper keeps the process alive
  after the finite-frame main function returns; the session log confirmed all
  120 frames completed before the test process was explicitly stopped.
- These automated checks and emulator runs do not replace physical-phone
  performance, battery, controller or interrupted-audio testing listed below.

## Review fixes — 17 September 2026

- Every Championship stage now reports failed result saves before continuation
  or the final celebration. Touch, keyboard and gamepad offer retry or explicit
  unsaved continuation. Retrying saves the existing result without duplicating
  records. Acknowledged unsaved data survives a new race, including a DNF, and
  is persisted by a later successful save.
- Classification and the live position indicator share the same ordering:
  finishers, active racers, then DNFs ordered by distance and stable driver order.
  Player result rank updates when remaining opponents stop racing. DNF results
  still earn neither medals, records nor Championship advancement.
- The countdown caption sits above the countdown ring, clear of the player
  marker. The Championship documentation consistently describes initials after
  the opening track and direct classification on later tracks.
- Release progression, music, simulation, asset-update and graphics tests passed;
  all four headless tests also passed under AddressSanitizer/UndefinedBehaviorSanitizer.
  Added regressions cover second/final-stage save failures, retry/reload, explicit
  unsaved continuation, multiple DNFs, tie ordering and remaining-rival timeouts.
  Graphics captures include `result-save-failed.bmp`, `final-save-failed-wide.bmp`
  and the updated `player-name-confirmed-race.bmp` under `build/graphics-test-profile/`.
- Static analysis of the changed game/UI code and `git diff --check` passed.
  The macOS application was rebuilt, its local signature verified, and its
  countdown launched in a 1600 x 450 window using profile-preserving diagnostics.
- These checks do not establish physical-phone performance, thermals, battery
  life or controller ergonomics. Mobile packages were not rebuilt in this pass.

## Arcade record tables — 17 September 2026

- Player-name regression: new and reset profiles display `YOU` until initials
  are confirmed. Version 4 persists that distinction, including an explicitly
  chosen `AAA`. Progression, simulation and graphics tests passed for settings
  saves before entry, drafts, invalid input, confirmation, reload, reset and
  legacy profile migration. Captures verify `YOU` before entry and `ROB` on the
  next stage after confirmation.
- Championship initials follow-up: a completed opening track now asks for
  initials and returns to classification without opening records. Later stages
  reuse the chosen initials. Progression, simulation and graphics tests passed,
  including first-stage save retry, persisted names, touch/Enter confirmation
  and continuation through the remaining stages.
- Later Championship stages open the player/rival classification directly,
  reusing the opening track's initials without showing the record table.
  Next Stage is the primary podium action. Progression and graphics regressions
  passed for touch continuation and real-event-loop Enter/gamepad A continuation
  through the next two stages; Arcade name entry remains covered separately.
  The macOS bundle was rebuilt and its signature verified after this correction.
- Added per-track Top 10 boards and the latest 30 finishes, three-letter initials,
  millisecond timing, race metadata and record improvements. Existing version 1/2
  best times migrate into version 3 without invented names or dates.
- Release progression, music, simulation and asset-update tests passed. Record
  regressions cover sorted insertion, ties, capacity limits, track separation,
  duplicate result handling, initials persistence, invalid initials, quit before
  confirmation, failed saves and retries, legacy migration, malformed extensions,
  and exclusion of diagnostic runs and unfinished races.
- Graphics integration passed with access to the macOS display. It covers touch
  letter selection, gamepad letter wrapping and confirmation, history pagination,
  keyboard name entry before global shortcuts, returning to results and reloading
  the typed initials. Screenshots of empty/populated tables, history and the
  initials form are in `build/graphics-test-profile/records-*.bmp` and
  `record-initials.bmp`; compact layouts are captured as `records-wide.bmp` and
  `initials-wide.bmp`.
- Progression and simulation passed under AddressSanitizer/UndefinedBehaviorSanitizer.
  The macOS application in `dist/ToyCars.app` was rebuilt and its signature verified.
  Mobile packages were not rebuilt in this pass.

## Close finishes, ramp exits and drift — 16 September 2026

- Race classification now waits for every car in the simulation step and uses
  interpolated crossing times. Lap timing uses the same crossing calculation;
  results and medals share a deterministic ordering for exact ties.
- Ramp takeoff requires crossing the forward lip. Side and rear exits fall
  under gravity; diagonal corner exits use the first edge crossed. Forward
  velocity determines takeoff strength. Drift braking is applied in the shared
  physics step for keyboard, touch and gamepad input.
- All four Release CTest entries passed. New simulation cases cover either
  winner in a same-step finish, exact ties, lap/finish timing, 81 ramp exits
  across all nine ramps, side-exit landings and drift braking. The graphics
  test verifies that an actual same-step finish saves silver and the player's
  interpolated record when the rival crosses first.
- Music and the complete simulation/input suite also passed under
  AddressSanitizer and UndefinedBehaviorSanitizer without diagnostics.
- Isolated copies with the old ramp launch, missing drift braking and premature
  result calculation restored fail 108, 6 and 3 assertions respectively. These
  checks leave production sources and player records untouched.
- Static analysis, formatting and `git diff --check` passed. The macOS bundle
  was rebuilt, its local signature verified, and its results screen launched
  successfully using the diagnostic mode that preserves player records.
- This pass does not add physical-phone performance, battery or controller
  measurements, and does not rebuild the mobile packages.

## AI and fuel balance — 16 September 2026

The Release build passes all three CTest entries: music, simulation and asset
updates. The complete simulation and touch input suite also passes under
AddressSanitizer/UndefinedBehaviorSanitizer. The simulation now checks all eight finishers in each of the nine
track/difficulty combinations, solo pace and difficulty ordering, and failure
to finish without fuel pickups on every track. Pickup checks cover precise
collection, independent driver availability and respawning; driving down the
center no longer collects roadside cans.

The same AI-controlled player in the full field produced these normal-difficulty
results before and after the balance changes (Release, two laps):

| Track | Previous time | New time | Previous finish fuel | New finish fuel |
| --- | --- | --- | --- | --- |
| Harvest Hills | 69.80 s | 55.22 s | 89.8% | 21.5% |
| Sunshine Coast | 66.73 s | 55.86 s | 96.2% | 14.8% |
| Alpine Rush | 64.65 s | 52.36 s | 96.7% | 22.9% |

Solo normal-difficulty runs finish in 50.43–51.02 seconds, with hard consistently
faster and easy slower. These are simulation measurements; human playtesting
is still needed to judge the subjective difficulty. Exported pickup positions
match the authoring script, and the road and ramp data are unchanged.

## Off-road motion regression

The off-road fix on 15 September 2026 passed the Release `simulation` and
`asset_updates` tests, the desktop `graphics` test, and the full simulation
under AddressSanitizer/UndefinedBehaviorSanitizer. The graphics test required
access to the macOS display outside the sandbox.

The new motion regression failed against the previous lane-boundary clamp.
With the fix, all 1,800 scenarios pass: stopped and coasting cars on both sides
of all three circuits, at three distances beyond the road, including tight
bends. They check that horizontal displacement follows velocity and stationary
cars do not move toward the track. Additional cases verify capped terrain
resistance, acceleration far from the road, off-road landing without horizontal
snapping, and recovery across all four island edges, both grounded and airborne,
without adding progress or fuel. All nine complete AI races still finish.

The macOS application bundle was rebuilt. Mobile applications were not rebuilt
as part of this fix.

## Builds

| Target | Result | Toolchain / runtime |
| --- | --- | --- |
| macOS ARM64 | Built and launched | Apple Clang 21, Homebrew SDL3 3.4.14, Apple M5, GL 4.1 |
| Android ARM64 debug APK | Built, installed and launched | SDK 36, NDK 28.2, Gradle 9.4.1, AGP 9.2.1 |
| iOS ARM64 Simulator app | Built and launched | Xcode 27, iOS 27 simulator, GLES 3.0 |
| AddressSanitizer + UndefinedBehaviorSanitizer | Passed | Debug C build, full simulation and touch tests |

The Android emulator is an ARM64 Medium Phone with a 2400 x 1080 display and
a 16 KB page-size system image. The iOS simulator is an iPhone 18 Pro with a
2622 x 1206 landscape surface. Both execute the bundled native C implementation.

## Automated simulation and input checks

`ctest --test-dir build --output-on-failure` and `ToyCars --test` passed.
The suite runs all three routes at all three difficulty settings using the
player's AI demonstration driver, then checks:

- every route exceeds 600 meters and has real elevation and three ramps;
- projection and distance round-trips over sampled path points;
- all nine full races reach a valid player finish;
- cans are collected and jumps produce airborne events;
- the rival field makes competitive progress and does not run out of fuel;
- empty fuel resolves to a DNF result;
- pause freezes the timer, car position and fuel;
- recovery does not award checkpoints;
- a car can accelerate from rest off the road;
- separate fingers can steer and drift simultaneously;
- canceling one finger leaves the other finger active;
- release and focus reset do not leave stuck steering or braking;
- braking overrides automatic throttle;
- the manual-mode touch accelerator works;
- paused gameplay ignores touch driving input.

The full race simulations take roughly 52–65 game seconds for the reference
driver, depending on circuit and difficulty. Small floating-point differences
between optimization/sanitizer builds can change close collision outcomes.
Tests assert behavior and completion rather than a compiler-specific podium.

## Review regression pass

The follow-up fixes on 15 September 2026 were checked with both Release and
AddressSanitizer/UndefinedBehaviorSanitizer builds. Both CTest entries,
`simulation` and `asset_updates`, passed. The added cases verify:

- reversing across the starting line does not award a lap;
- repeatedly recrossing a completed lap preserves its time, the next lap's
  start time and checkpoints, including after recovery;
- completing the remaining lap still ends the race with valid timing;
- landing reduces actual velocity and the reduction persists on the next step;
- pause taps survive another finger pressing or releasing a driving control;
- a dragged finger cannot use another finger's press to activate a button;
- queued clicks are consumed once and retain their own press/release positions;
- canceled, reset and untracked fingers cannot create clicks on release;
- mouse and touch input, and finger IDs from different devices, stay independent;
- editing an asset or adding a file refreshes the executable's assets directory
  on an ordinary build without modifying C sources. This test uses an isolated
  source/build fixture and never edits the working tree's assets.

Static analysis reported no diagnostics. The C formatting check and
`git diff --check` passed. Files outside the functional fixes were verified to
match their original contents after applying the same formatter.

Desktop graphics checks under the sanitizers covered an empty profile, all
three medal colors and saved records, desktop and compact menu layouts, and
race results. The compact check included simulated left/right safe areas and
a bottom inset. The displayed times and medals in those visual fixtures were
set in memory; the player's saved profile was not overwritten.

The updated Android ARM64 APK and iOS ARM64 Simulator app both built successfully.
The macOS application bundle was rebuilt and its local signature was verified.
These follow-up checks do not add physical-device or interrupted-audio testing
to the scope recorded below.

## Graphics recovery, profile and layout regressions

The additional review fixes on 15 September 2026 passed the `simulation`,
`asset_updates` and optional `graphics` CTest entries in both Release and
ASan/UBSan builds. Static analysis of the changed C sources and integration test,
the formatting check and `git diff --check` also passed.

The graphics test runs the actual main loop and uses its own profile directory
under the build directory. It verifies:

- replacing the GL context and delivering `SDL_EVENT_RENDER_DEVICE_RESET`
  rebuilds world buffers, shaders and font textures without a GL error;
- the running race pauses with unchanged time, position, progress and fuel,
  clears held input, retains diagnostics and resumes explicitly;
- a completed demo followed by a real settings click saves settings without
  changing personal records or medals, while a normal finish still saves both;
- portrait mode blocks menu keyboard shortcuts, gamepad actions, touches and
  queued clicks beneath the rotate prompt, and pauses gameplay;
- a 1600 x 450 aspect ratio retains usable result rows and a working retry button.

Headless tests also cover safe-area scaling at desktop, phone and ultrawide
aspect ratios, steering after scaling and releases across orientation changes.
The updated macOS bundle rendered a complete-race results screen in a real
1600 x 450 window with no overlap. Its local signature was verified. The Android
ARM64 debug APK and iOS ARM64 Simulator application were rebuilt successfully.

Context loss was injected on desktop; this does not establish physical-phone
GPU reset, thermal or battery behavior. The mobile builds still report existing
GLES/Gradle deprecation warnings; no graphics-backend migration is included.

## Steering and start-marking regressions

The steering fix passed the Release `simulation`, `asset_updates` and `graphics`
CTest entries. The new simulation cases check that both the car's heading and
its movement follow the requested screen direction at four headings on each
track, with and without drifting. Restoring the old yaw sign in an isolated
test executable fails all 96 direction assertions. All nine AI race cases still
finish successfully with the corrected input convention.

The Blender authoring test checks all 24 grid bars and 72 finish-checker tiles
across the three tracks. It raycasts vertices, edge midpoints and triangle
centers against the road's exported triangulation, checks each grid bar against
the starting car's lane and row, and verifies that the paint vertices occur in
the runtime mesh. Run it after rebuilding track assets:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 \
  --python tests/track_markings.py
```

All 3,192 surface samples passed on the regenerated scenes and exports. The
same test rejects the original assets for paint outside the road. The macOS
bundle was rebuilt, its signature verified, and starts on all three tracks
were rendered and visually checked (`build/start-{country,beach,winter}.png`).

## Visual and integration checks

- Blender 5.2.1 ran the authoring script and saved all five editable `.blend`
  scenes plus runtime exports. Each environment contains approximately 49–51k
  triangles before the separately drawn cars and fuel cans.
- Desktop menu, garage, country/beach/winter driving views and result table were
  rendered and captured. The result table continues updating as rivals finish.
- The Android menu, winter race, touch start, steering, recovery and pause were
  exercised through actual Android input events.
- A fresh Android race confirmed the RECOVER feedback, followed by a Home /
  reopen sequence that returned to the pause screen with driving input cleared.
- The iOS menu and beach race, including a fuel pickup, were rendered and captured.
- A repeated iOS presentation test completed without OpenGL errors after binding
  the UIKit screen framebuffer and presentation renderbuffer correctly.
- Android APK permission inspection reports no requested permissions, including
  no internet permission. All visual, font and audio content is local.
- The macOS package includes SDL3 and the asset directory; its loader path and
  local ad-hoc code signature are checked as part of packaging verification.
- The final native graphics/UI smoke run also completed under AddressSanitizer
  and UndefinedBehaviorSanitizer without a diagnostic.

Screenshots are in `screenshots/`, including `ios-menu.png`, `ios-race.png`,
`android-menu.png`, `android-race.png`, `android-pause.png`, `menu.png`,
`garage.png`, `beach.png`, `winter.png` and `results.png`.

## Issues found and corrected during verification

- Terrain intersected the road in steep areas: expanded the terrain cut and
  modeled a visible road foundation in Blender.
- Cars could not accelerate from rest on the outer shoulder: changed off-road
  resistance to depend on speed and added a mobile recovery control.
- A single collision circle allowed longitudinal overlap: replaced it with an
  oriented two-circle approximation of each car's body.
- Compact menus overlapped controls under mobile safe-area insets: based the
  track selector and compact screens on available height.
- iOS initially presented a black frame: bound SDL's UIKit-owned framebuffer
  and renderbuffer instead of assuming desktop GL presentation behavior.
- Redundant vertex-array binding generated emulator driver noise: cached world
  bindings and bound the UI vertex array once for each UI pass.

## Road and off-road ground contact

The ground-contact fix passed all three Release CTest entries (`simulation`,
`asset_updates`, `graphics`) and the headless ASan/UBSan entries. The new cases
check a raised road over a known sloped plane, car alignment at eight headings,
tire contact at 204 shoulder positions across the three tracks, and landing
after the outer driving boundary clamps an airborne car. All nine complete
AI races still finish and use the jump ramps.

`tests/ground_contact.py` passed in Blender on all three regenerated scenes.
It checks terrain clearance below the road, continuous shoulder contact and
exact agreement between each track's 20,294 exported collision triangles and
the visible driving meshes. `tests/track_markings.py` also passed. Static
analysis of the changed C sources, formatting and `git diff --check` passed.

Desktop screenshots checked the starting grid and an off-road car on the
winter hillside. The macOS app bundle was rebuilt and its local signature
verified. This pass does not add mobile-device validation.

## Adaptive soundtrack (16 September 2026)

- Release build and all three enabled CTest entries (`music`, `simulation`,
  `asset_updates`) passed. The asset-copy fixture now includes test sources
  required by the real CMake configuration.
- ASan/UBSan music and simulation checks passed. Music checks render all seven
  complete 64-bar forms, including transitions, and exercise mute, scene changes,
  playlist rotation, late requests and adaptive layers.
- Across the full-score renders, channel peaks were 0.097–0.169, RMS levels
  0.020–0.026, and the largest adjacent-sample change was 0.031. These measurements
  cover music alone; existing engine/effects share the mix afterward.
- A separate SDL dummy-device smoke check passed for the actual `audio_update`
  path: environment selection, racing intensity, event ducking, background pause,
  mute, results and return to menu. It used no display or physical audio device.
- Seven 45-second stereo WAV previews were exported under `build/music-preview`.
  A desktop gameplay launch in the sandbox could not open a display. This pass
  therefore does not claim in-game listening or physical speaker/headphone checks.

## Rally spectators (16 September 2026)

- All three Blender scenes and animated crowd exports were regenerated with 83
  spectators in 15 groups per circuit. Static track meshes, driving surfaces,
  paths, ramps and fuel metadata remain byte-identical to the pre-crowd assets.
- `tests/spectators.py` passed for all 249 figures: varied group sizes, all five
  body types and poses, spectators on both sides facing the road, separation,
  shoe contact with the triangulated ground, road clearance for every vertex,
  and presence of every figure in the runtime export. Animation parts and ranges
  are checked, including absence of static duplicates.
- All four enabled CTest checks (`music`, `simulation`, `asset_updates`,
  `graphics`) passed. GPU readbacks on every track confirm visible animation
  and changing shadows, unchanged images during pause, resumed animation, and
  restored crowd buffers after a graphics-context reset. Fixed-camera motion
  captures are under `build/graphics-test-profile/crowd-*.bmp`; an animated
  preview is `build/crowd-animation.gif`.
- The macOS app was rebuilt and its local signature verified. Crowds use one
  additional draw per pass. Total static plus crowd geometry is 88,702 / 88,184 /
  95,950 triangles for country / beach / winter. This pass does not include mobile
  rebuilds or device performance measurements.

## Still required before public release

- Playtesting with people on physical phones, particularly steering feel,
  difficulty progression, fuel balance, readability and accessible button sizes.
- Sustained device measurements for frame pacing, GPU time, memory, thermals and
  battery use. Simulator software rendering is not a phone performance measure.
- Physical-controller testing and interrupted audio-session testing across
  target device/OS combinations.
- Distribution signing, app icons/store artwork for all Apple device sizes,
  store metadata and the owner's store submission process.
- Longer-term iOS renderer planning: GLES is deprecated by Apple; SDL_GPU with
  Metal is the natural next backend.

There is no telemetry service, account system, store submission or deployment
hidden in this project. The APK is debug-signed; the macOS app is locally
ad-hoc-signed; the iOS bundle is a simulator build.
