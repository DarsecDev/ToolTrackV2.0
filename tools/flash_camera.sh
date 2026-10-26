#!/usr/bin/env bash
# Compila y flashea el nodo de visión.
# Uso: ./tools/flash_camera.sh
set -euo pipefail

cd "$(dirname "$0")/.."

pio run -e camera
pio run -e camera -t upload

echo "Listo. Monitor: pio device monitor -e camera"
