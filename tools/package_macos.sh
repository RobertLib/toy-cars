#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$task_root" -B "$task_root/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$task_root/build" -j
task_app="$task_root/dist/ToyCars.app"
mkdir -p "$task_app/Contents/MacOS" "$task_app/Contents/Resources" "$task_app/Contents/Frameworks"
cp "$task_root/build/ToyCars" "$task_app/Contents/MacOS/ToyCars"
cmake -E copy_directory "$task_root/assets" "$task_app/Contents/Resources/assets"
task_sdl_lib=$(pkg-config --variable=libdir sdl3)/libSDL3.0.dylib
cp -f "$task_sdl_lib" "$task_app/Contents/Frameworks/libSDL3.0.dylib"
chmod u+w "$task_app/Contents/Frameworks/libSDL3.0.dylib"
cp "$task_root/assets/licenses/SDL3-LICENSE.txt" "$task_app/Contents/Resources/SDL3-LICENSE.txt"
task_sdl_link=$(otool -L "$task_app/Contents/MacOS/ToyCars" | awk '/libSDL3.*dylib/ {print $1; exit}')
install_name_tool -change "$task_sdl_link" '@executable_path/../Frameworks/libSDL3.0.dylib' "$task_app/Contents/MacOS/ToyCars"
cat > "$task_app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>ToyCars</string>
<key>CFBundleIdentifier</key><string>com.toycars.game</string>
<key>CFBundleName</key><string>ToyCars</string>
<key>CFBundleDisplayName</key><string>ToyCars</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.1.0</string>
<key>CFBundleVersion</key><string>1</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$task_app/Contents/Frameworks/libSDL3.0.dylib"
codesign --force --sign - "$task_app"
echo "Created dist/ToyCars.app (local ad-hoc signature)."
