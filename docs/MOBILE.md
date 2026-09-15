# Mobile builds

The Android and iOS targets compile the same C11 game and GLES 3.0 shaders.
Landscape layout, independent fingers, safe areas, background pause and local
storage are handled through SDL3. Bundled assets keep gameplay entirely offline.

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

Create a separate device build using `-DCMAKE_OSX_SYSROOT=iphoneos` and omit
`CODE_SIGNING_ALLOWED=NO`. Open `build-ios-device/ToyCars.xcodeproj`, select a
development team and device, and build with Xcode. A personal development team
or distribution signing configuration must come from the owner of the app.

The renderer currently uses Apple's deprecated but available GLES API.
An SDL_GPU/Metal backend and physical-device profiling are appropriate future
work before a long-term public iOS release.

## Mobile behavior

- Landscape left and right are supported; a portrait surface displays a rotate
  prompt and pauses gameplay.
- Steering and brake/drift fingers are independent. Finger cancellation and
  focus loss clear held controls.
- Auto acceleration is on by default. In manual mode, the right button is GAS.
- RECOVER returns the car to its current progress without restoring fuel.
- Backgrounding pauses the race and audio; foregrounding requires a deliberate
  resume. Countdown pause retains the remaining countdown.
- Menus adapt to available height after safe-area insets.

Emulator and simulator checks do not establish sustained performance or battery
consumption on a physical phone. See [Validation](VALIDATION.md) for the exact
verification performed in this workspace.
