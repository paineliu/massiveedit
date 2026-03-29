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
elif [[ -d "${HOME}/Qt/6.11.0/gcc_64/lib/cmake" ]]; then
  CMAKE_PREFIX_PATH="${HOME}/Qt/6.11.0/gcc_64/lib/cmake"
elif [[ -d "${HOME}/Qt/6.11.0/clang_64/lib/cmake" ]]; then
  CMAKE_PREFIX_PATH="${HOME}/Qt/6.11.0/clang_64/lib/cmake"
else
  LATEST_QT_CMAKE_PATH="$(
    {
      ls -d "${HOME}"/Qt/*/gcc_64/lib/cmake 2>/dev/null
      ls -d "${HOME}"/Qt/*/clang_64/lib/cmake 2>/dev/null
    } | sort -V | tail -n 1 || true
  )"
  CMAKE_PREFIX_PATH="${LATEST_QT_CMAKE_PATH}"
fi

echo "[1/4] Configure"
if [[ -n "${CMAKE_PREFIX_PATH}" ]]; then
  cmake -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
else
  cmake -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
fi

if command -v nproc >/dev/null 2>&1; then
  CORES="$(nproc)"
else
  CORES="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)"
fi

echo "[2/4] Build (jobs=${CORES})"
cmake --build "${BUILD_DIR}" -j"${CORES}"

echo "[3/4] Test"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

if [[ "${RUN_APP}" -eq 0 ]]; then
  echo "[4/4] Skip run (--no-run)"
  exit 0
fi

echo "[4/4] Run"
APP_BIN="${BUILD_DIR}/src/massiveedit"
if [[ ! -x "${APP_BIN}" ]]; then
  echo "Failed to find executable: ${APP_BIN}"
  exit 1
fi

exec "${APP_BIN}"

