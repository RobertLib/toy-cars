#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"$task_root/tools/fetch_sdl.sh"
cd "$task_root/platform/android"
./gradlew assembleDebug "$@"
mkdir -p "$task_root/dist"
cp app/build/outputs/apk/debug/app-debug.apk "$task_root/dist/ToyCars-android-arm64-debug.apk"
echo "Created dist/ToyCars-android-arm64-debug.apk"
