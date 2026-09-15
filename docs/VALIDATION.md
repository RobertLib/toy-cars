# Validation

Executed locally on 15 September 2026. These are observed results, not a claim
of App Store readiness or performance on untested physical devices.

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

The full race simulations take roughly 61–77 game seconds for the reference
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
