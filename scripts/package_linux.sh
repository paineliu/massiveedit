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

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This script only supports Linux."
  exit 1
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

if [[ -z "${CMAKE_PREFIX_PATH}" ]]; then
  echo "Failed to find Qt CMake path."
  echo "Set QT_CMAKE_PREFIX_PATH, for example:"
  echo "  QT_CMAKE_PREFIX_PATH=\$HOME/Qt/6.11.0/gcc_64/lib/cmake ./scripts/package_linux.sh"
  exit 1
fi

PKG_DIR="${BUILD_DIR}/packages"
mkdir -p "${PKG_DIR}"
rm -f "${PKG_DIR}"/MassiveEdit-*.deb "${PKG_DIR}"/MassiveEdit-*.rpm "${PKG_DIR}"/MassiveEdit-*.tar.gz

echo "[1/5] Configure"
cmake -S "${ROOT_DIR}" \
  -B "${BUILD_DIR}" \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DMASSIVEEDIT_BUILD_TESTS=ON

if command -v nproc >/dev/null 2>&1; then
  CORES="$(nproc)"
else
  CORES="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)"
fi

echo "[2/5] Build (jobs=${CORES})"
cmake --build "${BUILD_DIR}" --clean-first -j"${CORES}"

if [[ "${RUN_TESTS}" -eq 1 ]]; then
  echo "[3/5] Test"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -C "${BUILD_TYPE}"
else
  echo "[3/5] Skip tests (--skip-tests)"
fi

DEB_STATUS=0
RPM_STATUS=0

echo "[4/5] Package DEB/RPM (if available)"
if command -v dpkg-deb >/dev/null 2>&1; then
  set +e
  cpack --config "${BUILD_DIR}/CPackConfig.cmake" -C "${BUILD_TYPE}" -G DEB
  DEB_STATUS=$?
  set -e
  if [[ "${DEB_STATUS}" -ne 0 ]]; then
    echo "Warning: DEB packaging failed."
  fi
else
  DEB_STATUS=127
  echo "Info: dpkg-deb not found, skip DEB."
fi

if command -v rpmbuild >/dev/null 2>&1; then
  set +e
  cpack --config "${BUILD_DIR}/CPackConfig.cmake" -C "${BUILD_TYPE}" -G RPM
  RPM_STATUS=$?
  set -e
  if [[ "${RPM_STATUS}" -ne 0 ]]; then
    echo "Warning: RPM packaging failed."
  fi
else
  RPM_STATUS=127
  echo "Info: rpmbuild not found, skip RPM."
fi

echo "[5/5] Package TGZ"
cpack --config "${BUILD_DIR}/CPackConfig.cmake" -C "${BUILD_TYPE}" -G TGZ

echo "[Done] Package complete"
echo "Packages are in: ${PKG_DIR}"
ls -lh "${PKG_DIR}"

if [[ "${DEB_STATUS}" -ne 0 && "${DEB_STATUS}" -ne 127 ]]; then
  echo "DEB was not generated in this run."
fi
if [[ "${RPM_STATUS}" -ne 0 && "${RPM_STATUS}" -ne 127 ]]; then
  echo "RPM was not generated in this run."
fi

