#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ICON_DIR="${ROOT_DIR}/resources/icons"
SRC_PNG="${ICON_DIR}/app_icon_2048.png"
OUT_ICNS="${ICON_DIR}/AppIcon.icns"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script only supports macOS."
  exit 1
fi

if [[ ! -f "${SRC_PNG}" ]]; then
  echo "Source icon not found: ${SRC_PNG}"
  exit 1
fi

for cmd in python3; do
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "Required command not found: ${cmd}"
    exit 1
  fi
done

python3 - "${SRC_PNG}" "${OUT_ICNS}" <<'PY'
from PIL import Image
import sys

src_png, out_icns = sys.argv[1], sys.argv[2]
img = Image.open(src_png).convert("RGBA")
sizes = [(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)]
img.save(out_icns, format="ICNS", sizes=sizes)
PY

echo "Generated: ${OUT_ICNS}"
