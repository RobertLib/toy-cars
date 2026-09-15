# Mobile builds

The Android and iOS targets compile the same C11 game and GLES 3.0 shaders.
Landscape layout, independent fingers, safe areas, background pause and local
storage are handled through SDL3. Bundled assets keep gameplay entirely offline.

## Preview on macOS without a simulator

```sh
make run-mobile
```

This builds and runs the native macOS executable with `--touch --size 874x402`,
enabling the same mobile menu layouts and driving controls in a landscape window.
Use the mouse to interact with the controls. Additional options go in `ARGS`,
for example `make run-mobile ARGS="--screen settings"` or
`make run-mobile ARGS="--size 1000x460"`. Use `make run` for the desktop layout.

## SDL3 source

Homebrew's SDL3 binary is for macOS. A mobile target needs SDL3 compiled for its
own platform. Download the pinned, checksum-verified source once:

```sh
./tools/fetch_sdl.sh
```

The source is kept in the ignored `third_party/SDL/` directory. Internet is
needed only for missing development dependencies, not to play the game.

## Android

The project uses Gradle 9.4.1, Android Gradle Plugin 9.2.1, SDK 36,
NDK 28.2.13676358 and CMake 3.22.1. The debug APK targets ARM64, Android 7+
(API 24+) and GLES 3.0. It contains both SDL3 and the game library.

Set `ANDROID_HOME` to your Android SDK and `JAVA_HOME` to a compatible JDK
(the Android Studio bundled JDK works), then:

```sh
./tools/build_android.sh
adb install -r dist/ToyCars-android-arm64-debug.apk
adb shell am start -n com.toycars.game/org.libsdl.app.SDLActivity
```

For an already populated dependency cache, use `./tools/build_android.sh --offline`.
Alternatively, open `platform/android` in Android Studio and run the app.

The manifest has **no internet permission**. Files are read directly from the
APK with SDL's AssetManager support. Preferences stay in the app's own storage.
The APK uses a development signing key and is intended for local testing.

## iOS Simulator

On a Mac with Xcode and an installed iOS simulator runtime:

```sh
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DSDL3_SOURCE_DIR="$PWD/third_party/SDL" \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build build-ios --config Debug -j
xcrun simctl install booted build-ios/Debug-iphonesimulator/ToyCars.app
xcrun simctl launch booted com.toycars.game
```

The renderer binds the UIKit-owned framebuffer when drawing to the screen and
its renderbuffer before presentation. Those objects are not the desktop GL
default framebuffer. The window properties are read from SDL each frame.

## Physical iPhone / iPad

Generate a separate project for the physical device:

```sh
cmake -S . -B build-ios-device -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DSDL3_SOURCE_DIR="$PWD/third_party/SDL"
open build-ios-device/ToyCars.xcodeproj
```

The static-library option lets CMake check the compiler without signing a test
app. Signing remains enabled for ToyCars.

In Xcode, select the **ToyCars** scheme. Under the ToyCars target's **Signing &
Capabilities**, enable **Automatically manage signing** and select your
development team. Select your connected iPhone or iPad as the run destination
and press **Run**. The device must trust the Mac and have Developer Mode enabled.
A personal development team or distribution signing configuration must come
from the owner of the app.

If Xcode lists your iPhone as incompatible, check which project is open:
`build-ios` above is configured with `SDKROOT=iphonesimulator` and
`CODE_SIGNING_ALLOWED=NO`. Use `build-ios-device/ToyCars.xcodeproj` for a physical
phone. Both builds use ARM64, but simulator binaries cannot run on an iPhone.

The renderer currently uses Apple's deprecated but available GLES API.
An SDL_GPU/Metal backend and physical-device profiling are appropriate future
work before a long-term public iOS release.

## Mobile behavior

- Landscape left and right are supported; a portrait surface displays a rotate
  prompt, pauses gameplay and blocks input to the covered screens.
- Steering and brake/drift fingers are independent. Finger cancellation and
  focus loss clear held controls.
- Auto acceleration is on by default. In manual mode, the right button is GAS.
- Hold BRAKE to stop, then keep holding to reverse at low speed. Steering works
  in reverse too. Release BRAKE to accelerate forward in automatic mode, or
  press GAS in manual mode.
- RECOVER returns the car to its current progress without restoring fuel.
- Backgrounding pauses the race and audio; foregrounding requires a deliberate
  resume. Countdown pause retains the remaining countdown. Quick background/foreground
  transitions still pause, and input queued in that transition frame cannot resume
  the race. Lifecycle events continue to be processed while the surface has no size.
- Failed saves of settings or car colors show retry and explicit unsaved-continuation
  controls, just like race results. Unsaved changes remain in memory and are included
  in the next successful save.
- Native mobile builds use dedicated menu, garage, help, settings, records and
  results layouts. Body text is larger, navigation has 72-unit touch targets,
  and records show four entries per page. The touch-controls preference does
  not change the mobile layout. A contrasting navigation bar stays fixed while
  pages fade in over 240 ms and the active underline moves between tabs. Rapid
  navigation retargets the underline without delaying taps. The navbar is 72 UI
  units high; all four pages share equal visible gaps below the bar and at the
  bottom of the screen, including space for the home indicator. The pause dialog
  is centered inside the safe area with balanced content padding.
- Driving buttons are 144 x 132 UI units, with separate 72-unit pause/recovery
  targets and a simplified HUD. Layout uses safe-area width and height, keeping
  controls clear of the notch and home indicator in either landscape orientation.
- `make run-mobile` previews the mobile compositions on desktop using
  `--touch --size 874x402`; `make run` uses the desktop layouts.
- A lost GL context triggers a rebuild of models, shaders, render targets and
  fonts in SDL's replacement context. The current race stays paused with its
  progress and fuel preserved until explicitly resumed.

Emulator and simulator checks do not establish sustained performance or battery
consumption on a physical phone. See [Validation](VALIDATION.md) for the exact
verification performed in this workspace.
