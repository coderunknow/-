// Windows platform services: paths, elevation, startup registration, time.
#pragma once

#include <cstdint>
#include <string>

namespace wrg {

// UTF-8 helpers.
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);

// Monotonic milliseconds (GetTickCount64).
uint64_t nowMonotonicMs();

// Wall-clock milliseconds since Unix epoch (UTC).
int64_t nowWallMs();

// ISO-8601 UTC timestamp, e.g. "2026-10-16T11:45:00Z".
std::string nowIso8601();

// Full path of this executable (UTF-8), empty on failure.
std::string exePath();

// Data directory: %LOCALAPPDATA%\WinResGov (created on demand).
// Returns false when the directory cannot be created.
bool dataDir(std::string* outPath);
bool ensureDir(const std::string& path);

// True when the current process is elevated (Administrator).
bool isElevated();

// CPU brand string from the registry (e.g. "Intel(R) Core(TM) i5-7300U ...").
std::string cpuBrandString();

// OS version string, e.g. "Windows 10.0 build 19045 (x64)".
// Uses RtlGetVersion (ntdll) which reports the true version regardless of
// compatibility shims; falls back to "Windows (version query unavailable)".
std::string osVersionString();

// Number of logical processors.
unsigned logicalProcessorCount();

// Start-with-Windows registration under HKCU\Software\Microsoft\Windows\
// CurrentVersion\Run (per-user, no elevation required; disabled by default).
// setStartup(true) writes a value pointing at this executable; setStartup(false)
// deletes the value ONLY when it points at this executable.
bool setStartup(bool enable, std::string* error);
bool getStartupEnabled();

}  // namespace wrg
