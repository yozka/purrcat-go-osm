#!/bin/bash
# Build and run OsmDemo on macOS.
# Double-click in Finder or: ./cmake/build_demo_mac.command

set -euo pipefail

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="${PROJECT}/demo"
BUILD_DIR="${PROJECT}/build/demo_mac"

QT_MACOS="${QT_MACOS:-${HOME}/Qt/6.11.1/macos}"
QT_CMAKE="${QT_MACOS}/bin/qt-cmake"

if [[ ! -x "${QT_CMAKE}" ]]; then
  echo "qt-cmake not found: ${QT_CMAKE}"
  echo "Set QT_MACOS to your Qt macos kit, e.g.:"
  echo "  export QT_MACOS=\"\${HOME}/Qt/6.11.1/macos\""
  exit 1
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> Configuring OsmDemo"
echo "    ROOT=${ROOT}"
echo "    QT=${QT_MACOS}"
echo "    BUILD=${BUILD_DIR}"

"${QT_CMAKE}" "${ROOT}" \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug

echo ""
echo "==> Building"
cmake --build . --parallel

APP="${BUILD_DIR}/OsmDemo.app"
if [[ ! -d "${APP}" ]]; then
  echo "App not found: ${APP}"
  exit 1
fi

echo ""
echo "==> Running ${APP}"
# Run binary in foreground so QML/crash errors stay visible in Terminal
exec "${APP}/Contents/MacOS/OsmDemo"
