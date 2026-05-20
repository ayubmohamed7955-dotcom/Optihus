#!/usr/bin/env bash
# Optihus smoke (items 1 + 4): build miljoestyring WITHOUT GPIO/I2C, run protocol vs local server.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-smoke-stub"
CFG_SRC="${ROOT}/miljoestyring_config.json.example"
CFG_DST="${BUILD}/miljoestyring_config.json"

cmake -S "${ROOT}" -B "${BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPTIHUS_MILJOESTYRING_HARDWARE=OFF \
  -DOPTIHUS_BUILD_DATAHAANDTERING=ON \
  -DOPTIHUS_BUILD_MILJOESTYRING=ON
cmake --build "${BUILD}" --parallel --target miljoestyring datahaandtering

mkdir -p "${BUILD}"
cp -f "${CFG_SRC}" "${CFG_DST}"
# Local loopback smoke: same default port as shared::kTcpPort (5000).
if command -v sed >/dev/null 2>&1; then
  sed -i.bak 's/"192\.168\.4\.1"/"127.0.0.1"/' "${CFG_DST}" && rm -f "${CFG_DST}.bak"
fi

echo ""
echo "=== Terminal A (server) ==="
echo "  cd \"${BUILD}\" && ./datahaandtering"
echo ""
echo "=== Terminal B (client, no GPIO; config: ${CFG_DST}) ==="
echo "  cd \"${BUILD}\" && ./miljoestyring"
echo ""
echo "Expect: TCP connect, Hello/Telemetry; no DHT/stepper hardware required."
