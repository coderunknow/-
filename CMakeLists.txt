# Windows Resource Governor — CMake build (MSVC or MinGW on Windows).
#   cmake -B build -A x64
#   cmake --build build --config Release
# Produces: build/Release/WinResGov.exe and build/Release/test_core.exe
cmake_minimum_required(VERSION 3.16)
project(windows-resource-governor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  # Single-EXE: static CRT (no VCRUNTIME/MSVCP redistributable DLLs).
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
  # Static libgcc/libstdc++ for a self-contained executable.
  add_link_options(-static)
endif()

# ---- portable core (shared by app and tests) --------------------------------
add_library(wrg_core STATIC
  src/core/types.cpp
  src/core/stats.cpp
  src/core/ini.cpp
  src/core/config.cpp
  src/core/engine.cpp
  src/core/csv.cpp
)
target_include_directories(wrg_core PUBLIC src)

# ---- application ------------------------------------------------------------
add_executable(WinResGov WIN32
  src/main.cpp
  src/win/platform.cpp
  src/win/sysmon.cpp
  src/win/procmon.cpp
  src/win/cpuopt.cpp
  src/win/wsopt.cpp
  src/win/executor.cpp
  src/win/bench.cpp
  src/win/app.cpp
  src/win/ui.cpp
  src/app.rc
)
target_include_directories(WinResGov PRIVATE src . src/win)
target_compile_definitions(WinResGov PRIVATE UNICODE _UNICODE)
target_link_libraries(WinResGov PRIVATE wrg_core
  psapi advapi32 shell32 comctl32 comdlg32 user32 gdi32 ole32)

# MinGW needs -municode for the wWinMain entry point.
if(MINGW)
  target_link_options(WinResGov PRIVATE -municode)
endif()

# ---- unit tests (console subsystem) ----------------------------------------
add_executable(test_core tests/test_core.cpp)
target_include_directories(test_core PRIVATE src .)
target_link_libraries(test_core PRIVATE wrg_core)
if(MINGW)
  target_link_options(test_core PRIVATE -municode)
endif()

enable_testing()
add_test(NAME core_unit_tests COMMAND test_core)
