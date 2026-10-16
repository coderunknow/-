// Shared, platform-neutral types for the Windows Resource Governor core.
// This header must stay free of Windows headers so the core builds on any host.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wrg {

// ---- Profiles ---------------------------------------------------------------

enum class Profile {
    Saver = 0,
    Balanced = 1,
    Performance = 2,
};

const char* profileName(Profile p);              // "Saver" / "Balanced" / "Performance"
const char* profileKey(Profile p);               // "saver" / "balanced" / "performance"
bool profileFromString(const std::string& s, Profile* out);  // case-insensitive

// ---- Pressure levels -------------------------------------------------------

enum class PressureLevel {
    None = 0,
    Low = 1,
    Moderate = 2,
    High = 3,
    Critical = 4,
};

const char* pressureLevelName(PressureLevel l);

// ---- Actions ---------------------------------------------------------------

enum class ActionType {
    None = 0,
    SetPriorityBelowNormal,   // lower eligible background work
    RestorePriority,          // restore previously saved priority class
    EnablePowerThrottle,      // PROCESS_POWER_THROTTLING_EXECUTION_SPEED
    DisablePowerThrottle,     // restore no-throttle state
    TrimWorkingSet,           // SetProcessWorkingSetSize trim (one-shot)
    ManualOptimize,           // user-initiated optimization pass marker
    RestoreAll,               // restore every action we applied
};

const char* actionTypeName(ActionType t);

// Windows priority class values (numeric values match the Win32 constants so the
// core can reason about them without including <windows.h>).
constexpr int kPriorityIdle        = 0x0040;
constexpr int kPriorityBelowNormal = 0x4000;
constexpr int kPriorityNormal      = 0x0020;
constexpr int kPriorityAboveNormal = 0x8000;
constexpr int kPriorityHigh        = 0x0080;
constexpr int kPriorityRealtime    = 0x0100;  // NEVER assigned by this application.

// ---- Process sample (produced by the Windows monitor, consumed by the engine)

struct ProcSample {
    uint32_t pid = 0;
    uint32_t ppid = 0;
    std::string exeName;          // lower-case ASCII executable name, e.g. "chrome.exe"
    std::string exePath;          // UTF-8 full path; empty when inaccessible
    uint64_t wsBytes = 0;         // working set
    uint64_t privateWsBytes = 0;  // private working set (0 when unavailable)
    bool privateWsPartial = false;// true when private WS covers only part of the WS
    uint64_t privateBytes = 0;    // PROCESS_MEMORY_COUNTERS_EX::PrivateUsage
    uint64_t commitBytes = 0;     // PagefileUsage (committed bytes)
    double cpuPercent = 0.0;      // normalized 0..100 across all logical processors
    int priorityClass = 0;        // kPriority* value, 0 when unknown
    int browserFamily = 0;        // 0 = not a browser, 1 = chrome, 2 = brave, 3 = edge
    uint32_t groupRootPid = 0;    // root of the same-family process tree (0 if n/a)
    bool isBrowserRoot = false;
    bool isForeground = false;
    bool isForegroundTree = false;// foreground process, its ancestors, or its tree
    bool isProtected = false;     // protected by default or by config list
    bool isExcluded = false;      // user exclusion
    bool isSelf = false;
    bool accessible = true;       // false when OpenProcess/metrics failed
};

// ---- System sample ---------------------------------------------------------

struct SysSample {
    uint64_t totalPhysBytes = 0;
    uint64_t availPhysBytes = 0;
    uint64_t commitTotalBytes = 0;
    uint64_t commitLimitBytes = 0;
    int memoryLoadPercent = 0;    // MEMORYSTATUSEX::dwMemoryLoad
    double cpuPercent = 0.0;      // normalized 0..100, computed from GetSystemTimes deltas
    bool valid = false;
};

// ---- Planned action (engine output, executed by the Windows layer) ----------

struct PlanAction {
    ActionType type = ActionType::None;
    uint32_t pid = 0;
    std::string exeName;
    std::string detail;           // human-readable rationale for the audit log
};

// ---- Applied action record (audit history shown in the dashboard) -----------

struct ActionRecord {
    int64_t timestampMs = 0;      // wall clock ms since epoch (UTC)
    ActionType type = ActionType::None;
    uint32_t pid = 0;
    std::string exeName;
    bool ok = false;
    unsigned win32Error = 0;
    std::string detail;           // includes before/after values when measured
};

}  // namespace wrg
