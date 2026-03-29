#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-qt}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
RUN_APP=1

if [[ "${1:-}" == "--no-run" ]]; then
  RUN_APP=0
fi

if [[ -n "${QT_CMAKE_PREFIX_PATH:-}" ]]; then
  CMAKE_PREFIX_PATH="${QT_CMAKE_PREFIX_PATH}"
elif [[ -d "${HOME}/Qt/6.11.0/macos/lib/cmake" ]]; then
  CMAKE_PREFIX_PATH="${HOME}/Qt/6.11.0/macos/lib/cmake"
else
  LATEST_QT_CMAKE_PATH="$(ls -d "${HOME}"/Qt/*/macos/lib/cmake 2>/dev/null | sort -V | tail -n 1 || true)"
  CMAKE_PREFIX_PATH="${LATEST_QT_CMAKE_PATH}"
fi

if [[ -z "${CMAKE_PREFIX_PATH}" ]]; then
  echo "Failed to find Qt CMake path."
  echo "Set QT_CMAKE_PREFIX_PATH, for example:"
  echo "  QT_CMAKE_PREFIX_PATH=\$HOME/Qt/6.11.0/macos/lib/cmake ./scripts/build_and_run.sh"
  exit 1
fi

echo "[1/4] Configure"
cmake -S "${ROOT_DIR}" \
  -B "${BUILD_DIR}" \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
echo "[2/4] Build (jobs=${CORES})"
cmake --build "${BUILD_DIR}" -j"${CORES}"

echo "[3/4] Test"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

if [[ "${RUN_APP}" -eq 0 ]]; then
  echo "[4/4] Skip run (--no-run)"
  exit 0
fi

echo "[4/4] Run"
APP_BUNDLE_BIN="${BUILD_DIR}/src/MassiveEdit.app/Contents/MacOS/MassiveEdit"
APP_BUNDLE_BIN_LEGACY="${BUILD_DIR}/src/massiveedit.app/Contents/MacOS/massiveedit"
APP_BIN="${BUILD_DIR}/src/massiveedit"

if [[ -x "${APP_BUNDLE_BIN}" ]]; then
  exec "${APP_BUNDLE_BIN}"
fi

if [[ -x "${APP_BUNDLE_BIN_LEGACY}" ]]; then
  exec "${APP_BUNDLE_BIN_LEGACY}"
fi

exec "${APP_BIN}"
