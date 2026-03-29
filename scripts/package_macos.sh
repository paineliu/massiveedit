#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-qt}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
RUN_TESTS=1

if [[ "${1:-}" == "--skip-tests" ]]; then
  RUN_TESTS=0
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script only supports macOS."
  exit 1
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
  echo "  QT_CMAKE_PREFIX_PATH=\$HOME/Qt/6.11.0/macos/lib/cmake ./scripts/package_macos.sh"
  exit 1
fi

PKG_DIR="${BUILD_DIR}/packages"
mkdir -p "${PKG_DIR}"
rm -f "${PKG_DIR}"/MassiveEdit-*.dmg "${PKG_DIR}"/MassiveEdit-*.tar.gz

echo "[0/5] Generate App Icon"
"${ROOT_DIR}/scripts/generate_macos_icns.sh"

echo "[1/5] Configure"
cmake -S "${ROOT_DIR}" \
  -B "${BUILD_DIR}" \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DMASSIVEEDIT_BUILD_TESTS=ON

CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
echo "[2/5] Build (jobs=${CORES})"
cmake --build "${BUILD_DIR}" --clean-first -j"${CORES}"

if [[ "${RUN_TESTS}" -eq 1 ]]; then
  echo "[3/5] Test"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -C "${BUILD_TYPE}"
else
  echo "[3/5] Skip tests (--skip-tests)"
fi

echo "[4/5] Package DMG"
DMG_STATUS=0
set +e
cpack --config "${BUILD_DIR}/CPackConfig.cmake" -C "${BUILD_TYPE}" -G DragNDrop
DMG_STATUS=$?
set -e
if [[ "${DMG_STATUS}" -ne 0 ]]; then
  echo "Warning: DMG packaging failed (hdiutil/cpack)."
fi

echo "[5/5] Package TGZ"
cpack --config "${BUILD_DIR}/CPackConfig.cmake" -C "${BUILD_TYPE}" -G TGZ

echo "[Done] Package complete"
echo "Packages are in: ${PKG_DIR}"
ls -lh "${PKG_DIR}"

if [[ "${DMG_STATUS}" -ne 0 ]]; then
  echo "DMG was not generated in this run."
  echo "You can still distribute the TGZ package above."
fi
