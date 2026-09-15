# ToyCars

**Small cars. Wild places.** An offline 3D pocket rally, written in C11 and SDL3.

![ToyCars](screenshots/menu.png)

## Play on macOS

In this workspace, a ready-to-open application is in `dist/ToyCars.app`:

```sh
open dist/ToyCars.app
```

The Android test package is `dist/ToyCars-android-arm64-debug.apk`.

SDL3 is the only runtime library. CMake finds the Homebrew installation automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ToyCars
```

The generated `build/assets` folder belongs beside the executable. The game does
not download anything and does not require Blender, Python, SDL_ttf, a login, or
an internet connection to run.

## In the box

- Three individually shaped, elevated 3D tracks: **Harvest Hills**, **Sunshine
  Coast**, and **Alpine Rush**. Each loop is over 600 game meters long, with
  banking, three physical jump ramps and seven fuel pickups.
- Eight-car, two-lap races. Seven rivals steer, brake, select overtaking lines,
  plan fuel stops and recover if stuck. Three difficulty levels.
- Momentum, lateral tire grip, drifting, different surface grip, oriented car
  collisions, gravity, takeoff and landing. Leaving the road costs speed.
- Collectible fuel, fuel exhaustion, lap validation, race position, race time,
  lap time, final classification, podium medals and local personal records.
- A live 3D track selector, garage with six liveries, controls guide, settings,
  countdown, pause, restart, results and next-track flow. English throughout.
- Independent multitouch steering and brake/drift controls, automatic or manual
  acceleration, keyboard and hot-plugged gamepad input.
- Filtered real-time shadows, vertex-colored Blender models, smooth terrain,
  distance fog, edge smoothing, dust, pickup and landing particles.
- Original synthesized music, engine audio and event sounds. No external audio
  files or streaming service.

## Controls

| Action | Keyboard | Touch | Gamepad |
| --- | --- | --- | --- |
| Steer | A / D or left / right | Left / right thumb buttons | Left stick |
| Accelerate | Automatic; W / up in manual mode | Automatic; GAS in manual mode | Automatic; right trigger in manual mode |
| Brake | S / down | BRAKE | Left trigger |
| Drift | Space | DRIFT with auto accelerate enabled | A / south button |
| Recover | R | RECOVER | — |
| Pause / resume | Escape | Pause button | Start |
| Start / retry | Enter | Menu buttons | A / south button |
| Select track in menu | 1 / 2 / 3 | Track cards | Use touch or mouse |
| Diagnostics | F1 | — | — |
| Screenshot | F2 | — | — |
| Fullscreen | F11 | Automatic | — |

**Fuel:** a can restores 26 percentage points and respawns after 22 seconds for
each driver independently. A full tank cannot cover a complete race at race
speed without pickups. **R** recovers your car without adding fuel or race
progress. Backgrounding the app pauses the race; resume explicitly when ready.

## Blender source

The game meshes were actually modeled and exported in **Blender 5.2**. Open the
editable scenes in `art/`:

- `rally_car.blend` — beveled rally body, glazing, mirrors, striped roof, wheels,
  wing, lamps and bumpers.
- `fuel_can.blend` — the collectible jerrycan.
- `country.blend`, `beach.blend`, `winter.blend` — complete themed scenes with
  terrain, road, curbs, ramps, buildings, trees and props.

Reproduce all five scenes and their runtime exports:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python tools/build_assets.py
cmake -E copy_directory assets build/assets
```

The Blender script is an authoring tool. Gameplay and rendering run in C.
The custom mesh and track formats are documented in [Architecture](docs/ARCHITECTURE.md).

Font atlases are already bundled. To rebuild them, install SDL3_ttf for this
development-only step and run:

```sh
cmake -S . -B build -DTOYCARS_BUILD_FONT_BAKER=ON
cmake --build build --target bake_fonts
./build/bake_fonts assets/fonts/Barlow-Medium.ttf assets/fonts/body.tcf
./build/bake_fonts assets/fonts/BarlowCondensed-SemiBold.ttf assets/fonts/display.tcf
```

## Mobile builds

See [Mobile build and testing](docs/MOBILE.md). The Android project builds an
ARM64 APK. The iOS CMake target creates an Xcode project and an app bundle.
Both use the same C sources, bundled content and OpenGL ES 3.0 renderer.

## Verification and demos

```sh
ctest --test-dir build --output-on-failure
./build/ToyCars --test
./build/ToyCars --autoplay --track winter
./build/ToyCars --screen garage
./build/ToyCars --screen race --autoplay --benchmark --frames 600 \
  --no-audio --screenshot screenshots/demo.bmp
```

The headless tests drive complete races on every track at all three difficulty
levels and check fuel, AI progress, jumps, lap progression, recovery, pause,
fuel exhaustion and independent multitouch input. See [Validation](docs/VALIDATION.md)
for executed checks and remaining release work.

For memory checks:

```sh
cmake -S . -B build-asan -DTOYCARS_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j
./build-asan/ToyCars --test
```

## Design research and credits

[Research and design notes](docs/RESEARCH.md) describe the references and the
specific decisions drawn from them. ToyCars uses original environments and
models, not copied game assets. Barlow fonts are bundled under the SIL Open Font
License in `assets/fonts/`. SDL3 uses the zlib license; its source download keeps
that license in `third_party/SDL/LICENSE.txt`.

This is a playable first release candidate for iteration. Physical-device
performance profiling, broader playtesting, store signing and store submission
are separate release steps; see the validation document for the exact tested scope.
