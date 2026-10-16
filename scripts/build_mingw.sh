#!/usr/bin/env bash
# Cross-builds WinResGov.exe for Windows 10 x64 from a non-Windows host.
#
# Toolchain resolution order:
#   1. $ZIG (explicit path)
#   2. `zig` on PATH
#   3. `python3 -m ziglang` (the PyPI ziglang wheel)
#
# This build intentionally omits the .rc resource (no windres/rc.exe in the
# zig toolchain); version strings are compiled in from src/core/version.h and
# the Windows CI/MSVC build embeds RT_VERSION + the manifest. See DESIGN_LOG D13.
set -euo pipefail
cd "$(dirname "$0")/.."

ZIG_BIN="${ZIG:-}"
if [ -z "$ZIG_BIN" ]; then
  if command -v zig >/dev/null 2>&1; then
    ZIG_BIN="zig"
  elif python3 -c "import ziglang" >/dev/null 2>&1; then
    ZIG_BIN="python3 -m ziglang"
  else
    echo "error: no zig toolchain found (set ZIG= or install the ziglang wheel)" >&2
    exit 1
  fi
fi

mkdir -p build/win
$ZIG_BIN c++ -target x86_64-windows-gnu -O2 -std=c++17 \
  -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS \
  -I src -I . -I src/win \
  src/win/platform.cpp src/win/sysmon.cpp src/win/procmon.cpp \
  src/win/cpuopt.cpp src/win/wsopt.cpp src/win/executor.cpp \
  src/win/bench.cpp src/win/app.cpp src/win/ui.cpp src/main.cpp \
  src/core/types.cpp src/core/stats.cpp src/core/ini.cpp \
  src/core/config.cpp src/core/engine.cpp src/core/csv.cpp \
  -municode -static \
  -lpsapi -ladvapi32 -lshell32 -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -luuid -lgdi32 \
  -o build/win/WinResGov.exe

echo "built: build/win/WinResGov.exe ($(stat -c%s build/win/WinResGov.exe) bytes)"
