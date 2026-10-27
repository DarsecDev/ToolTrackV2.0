#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
OUT="${TMPDIR:-/tmp}/tooltrack_native_tests"

"${CXX}" \
  -std=c++17 \
  -Wall \
  -Wextra \
  -Wpedantic \
  -I "${ROOT}/test/native/stubs" \
  -I "${ROOT}/src/main" \
  -I "${ROOT}/include" \
  "${ROOT}/test/native/test_main.cpp" \
  "${ROOT}/test/native/stubs.cpp" \
  "${ROOT}/src/main/ir_sensors.cpp" \
  "${ROOT}/src/main/state.cpp" \
  "${ROOT}/src/main/actuators.cpp" \
  -o "${OUT}"

"${OUT}"
