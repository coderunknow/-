# Windows Resource Governor — convenience build entry points.
#
#   make test-host   Build and run the portable unit tests with the host g++.
#   make win         Cross-build build/win/WinResGov.exe (needs a zig on PATH
#                    or ZIG=/path/to/zig; target x86_64-windows-gnu).
#   make clean       Remove build outputs.
#
# The supported Windows build is CMake (see README.md / CMakeLists.txt); this
# Makefile exists for local cross-verification from non-Windows hosts.

ZIG ?= zig
CXX ?= g++
BUILD := build

CORE_SRCS := src/core/types.cpp src/core/stats.cpp src/core/ini.cpp \
             src/core/config.cpp src/core/engine.cpp src/core/csv.cpp
WIN_SRCS := src/win/platform.cpp src/win/sysmon.cpp src/win/procmon.cpp \
            src/win/cpuopt.cpp src/win/wsopt.cpp src/win/executor.cpp \
            src/win/bench.cpp src/win/app.cpp src/win/ui.cpp src/main.cpp

.PHONY: all test-host win clean

all: test-host

$(BUILD)/host/test_core: tests/test_core.cpp tests/core_tests.h $(CORE_SRCS)
	@mkdir -p $(BUILD)/host
	$(CXX) -std=c++17 -Wall -Wextra -O2 -I src -I . tests/test_core.cpp $(CORE_SRCS) -o $@

test-host: $(BUILD)/host/test_core
	$(BUILD)/host/test_core

$(BUILD)/win/WinResGov.exe: $(WIN_SRCS) $(CORE_SRCS)
	@mkdir -p $(BUILD)/win
	$(ZIG) c++ -target x86_64-windows-gnu -O2 -std=c++17 \
	  -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS \
	  -I src -I . -I src/win \
	  $(WIN_SRCS) $(CORE_SRCS) \
	  -municode -static \
	  -lpsapi -ladvapi32 -lshell32 -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -luuid -lgdi32 \
	  -o $@

win: $(BUILD)/win/WinResGov.exe

clean:
	rm -rf $(BUILD)
