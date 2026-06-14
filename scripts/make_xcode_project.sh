#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-xcode}"
DEFAULT_QT_CMAKE_PREFIX_PATH="${HOME}/Qt/6.11.1/macos/lib/cmake"

if [[ -n "${QT_CMAKE_PREFIX_PATH:-}" ]]; then
  CMAKE_PREFIX_PATH="${QT_CMAKE_PREFIX_PATH}"
elif [[ -d "${DEFAULT_QT_CMAKE_PREFIX_PATH}" ]]; then
  CMAKE_PREFIX_PATH="${DEFAULT_QT_CMAKE_PREFIX_PATH}"
else
  CMAKE_PREFIX_PATH="$(ls -d "${HOME}"/Qt/*/macos/lib/cmake 2>/dev/null | sort -V | tail -n 1 || true)"
fi

if [[ -z "${CMAKE_PREFIX_PATH}" ]]; then
  echo "Failed to find Qt CMake path."
  echo ""
  echo "App Store builds require the official Qt installer (not Homebrew)."
  echo "Expected default path: ${DEFAULT_QT_CMAKE_PREFIX_PATH}"
  echo "Or override with:"
  echo "  QT_CMAKE_PREFIX_PATH=/path/to/qt/lib/cmake ./scripts/make_xcode_project.sh"
  exit 1
fi

if [[ "${CMAKE_PREFIX_PATH}" == /opt/homebrew/* ]]; then
  echo "Error: Homebrew Qt cannot be used for App Store builds."
  echo "Install Qt from https://www.qt.io/download-qt-installer and set QT_CMAKE_PREFIX_PATH."
  exit 1
fi

echo "Using Qt: ${CMAKE_PREFIX_PATH}"

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cached_qt="$(grep -m1 '^CMAKE_PREFIX_PATH:' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | sed 's/.*=//' || true)"
  if [[ -n "${cached_qt}" && "${cached_qt}" != "${CMAKE_PREFIX_PATH}" ]]; then
    echo "Qt path changed; removing stale ${BUILD_DIR} cache."
    rm -rf "${BUILD_DIR}"
  fi
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Xcode \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  -DMASSIVEEDIT_BUILD_TESTS=OFF

echo ""
echo "Xcode project ready: ${BUILD_DIR}/massiveedit.xcodeproj"
echo "Next: open ${BUILD_DIR}/massiveedit.xcodeproj → Product → Archive"
