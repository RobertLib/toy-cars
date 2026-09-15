# Validation

Executed locally on 15 September 2026. These are observed results, not a claim
of App Store readiness or performance on untested physical devices.

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
