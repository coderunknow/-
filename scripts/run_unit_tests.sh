#!/usr/bin/env bash
# Builds and runs the portable core unit tests on the host toolchain.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
g++ -std=c++17 -Wall -Wextra -O2 -I src -I . \
  tests/test_core.cpp \
  src/core/types.cpp src/core/stats.cpp src/core/ini.cpp \
  src/core/config.cpp src/core/engine.cpp src/core/csv.cpp \
  -o build/host/test_core
./build/host/test_core
