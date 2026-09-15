#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -f "$task_root/third_party/SDL/CMakeLists.txt" ]; then
    echo "SDL3 source is already present."
    exit 0
fi
task_temp=$(mktemp -d)
trap 'rm -rf "$task_temp"' EXIT HUP INT TERM
curl -L --fail https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/SDL3-3.4.14.tar.gz -o "$task_temp/SDL3.tar.gz"
python3 - "$task_temp/SDL3.tar.gz" <<'PY'
import hashlib, sys
expected = '30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb'
with open(sys.argv[1], 'rb') as f:
    actual = hashlib.sha256(f.read()).hexdigest()
if actual != expected:
    raise SystemExit('SDL3 download checksum mismatch; refusing to extract.')
PY
tar -xzf "$task_temp/SDL3.tar.gz" -C "$task_temp"
mkdir -p "$task_root/third_party"
mv "$task_temp/SDL3-3.4.14" "$task_root/third_party/SDL"
echo "SDL3 3.4.14 is ready in third_party/SDL."
