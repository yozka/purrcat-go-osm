#!/bin/bash
# Build and run PurrCat OSM tile server on macOS.
# Double-click or: ./cmake/build_server_mac.command

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build/server_mac"
DATA_DIR="${BUILD_DIR}/data"

mkdir -p "${BUILD_DIR}"
mkdir -p "${DATA_DIR}/tiles" "${DATA_DIR}/geometry"
cd "${BUILD_DIR}"

echo "==> Configuring purrcat-osm-tiles"
cmake "${ROOT}" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release

echo ""
echo "==> Building"
cmake --build . --parallel

BIN="${BUILD_DIR}/purrcat-osm-tiles"
if [[ ! -x "${BIN}" ]]; then
  echo "Binary not found: ${BIN}"
  exit 1
fi

echo ""
echo "==> Running ${BIN}"
echo "    data: ${DATA_DIR}"
echo "    http://127.0.0.1:8080"
exec "${BIN}" --data "${DATA_DIR}" --port 8080
