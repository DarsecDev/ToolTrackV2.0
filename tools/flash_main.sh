#!/usr/bin/env bash
# Compila y flashea la placa maestra.
# Uso: ./tools/flash_main.sh [--with-fs]
set -euo pipefail

cd "$(dirname "$0")/.."

pio run -e main
pio run -e main -t upload

if [[ "${1:-}" == "--with-fs" ]]; then
  pio run -e main -t buildfs
  pio run -e main -t uploadfs
fi

echo "Listo. Monitor: pio device monitor -e main"
