#!/usr/bin/env bash
# Build and run datahaandtering from repo root (Linux / WSL).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${BUILD_DIR:-build}"
EXE="$BUILD_DIR/datahaandtering/datahaandtering"

pkill -x datahaandtering 2>/dev/null || true
if [[ ! -d "$BUILD_DIR" ]]; then
  cmake -S . -B "$BUILD_DIR" -DOPTIHUS_BUILD_DATAHAANDTERING=ON -DOPTIHUS_BUILD_MILJOESTYRING=OFF
fi
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)" --target datahaandtering
echo "Web UI: http://127.0.0.1:8080/"
exec "$EXE"
