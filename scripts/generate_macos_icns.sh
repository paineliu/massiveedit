#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ICON_DIR="${ROOT_DIR}/resources/icons"
SRC_PNG="${ICON_DIR}/app_icon_2048.png"
OUT_ICNS="${ICON_DIR}/AppIcon.icns"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script only supports macOS."
  exit 1
fi

if [[ ! -f "${SRC_PNG}" ]]; then
  echo "Source icon not found: ${SRC_PNG}"
  exit 1
fi

for cmd in sips tiffutil tiff2icns; do
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "Required command not found: ${cmd}"
    exit 1
  fi
done

sips -z 16 16 "${SRC_PNG}" --out "${WORK_DIR}/icon_16.tiff" >/dev/null
sips -z 32 32 "${SRC_PNG}" --out "${WORK_DIR}/icon_32.tiff" >/dev/null
sips -z 48 48 "${SRC_PNG}" --out "${WORK_DIR}/icon_48.tiff" >/dev/null
sips -z 128 128 "${SRC_PNG}" --out "${WORK_DIR}/icon_128.tiff" >/dev/null
sips -z 256 256 "${SRC_PNG}" --out "${WORK_DIR}/icon_256.tiff" >/dev/null
sips -z 512 512 "${SRC_PNG}" --out "${WORK_DIR}/icon_512.tiff" >/dev/null
sips -z 1024 1024 "${SRC_PNG}" --out "${WORK_DIR}/icon_1024.tiff" >/dev/null

tiffutil -cat \
  "${WORK_DIR}/icon_16.tiff" \
  "${WORK_DIR}/icon_32.tiff" \
  "${WORK_DIR}/icon_48.tiff" \
  "${WORK_DIR}/icon_128.tiff" \
  "${WORK_DIR}/icon_256.tiff" \
  "${WORK_DIR}/icon_512.tiff" \
  "${WORK_DIR}/icon_1024.tiff" \
  -out "${WORK_DIR}/all_icons.tiff" >/dev/null

tiff2icns "${WORK_DIR}/all_icons.tiff" "${OUT_ICNS}"

echo "Generated: ${OUT_ICNS}"
