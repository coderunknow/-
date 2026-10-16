// Persistent application configuration: typed access, validation, safe defaults.
// A corrupted or missing configuration file always yields a usable Config
// (safe defaults + clamped values) and a non-fatal error description.
#pragma once

#include <string>
#include <vector>

#include "core/types.h"

namespace wrg {

struct ProfileThresholds {
    double memModerateUsedPct = 75.0;  // used physical memory % -> Moderate
    double memHighUsedPct = 85.0;      // -> High
    double memCriticalUsedPct = 92.0;  // -> Critical
    double cpuHighPct = 80.0;          // system CPU % -> at least High
};

struct Config {
    Profile profile = Profile::Balanced;
    bool automationEnabled = false;    // user-controlled automation, default OFF
    bool startWithWindows = false;     // startup, disabled by default
    int refreshMs = 1000;              // dashboard/engine sampling period

    ProfileThresholds thresholds[3];   // indexed by Profile

    // Hysteresis / anti-oscillation.
    double hysteresisPct = 5.0;        // must fall this far below to de-escalate
    int escalateDelayMs = 2000;        // sustained pressure required to escalate
    int minDwellMs = 15000;            // minimum time at a level before de-escalating

    // Action cooldowns (seconds).
    int priorityCooldownS = 30;
    int throttleCooldownS = 30;
    int trimCooldownS = 120;
    int restoreCooldownS = 20;

    // Working-set policy gates (see brief section D).
    bool wsTrimAutoEnabled = false;    // policy gate for AUTOMATIC trims
    bool wsTrimManualAllowed = true;   // user-initiated one-shot trims

    // CPU optimization gates (see brief section C).
    bool cpuOptEnabled = true;
    bool powerThrottleEnabled = true;
    bool keepBrowserRootNormal = true; // never lower the browser root process

    // Protection / exclusions (process exe names, case-insensitive).
    std::vector<std::string> protectedProcesses;
    std::vector<std::string> excludedProcesses;

    // Benchmark defaults.
    int benchSampleMs = 500;
    int benchWindowSeconds = 30;

    // Dashboard preferences.
    bool dashboardTopmost = false;
    bool minimizeUpdatesWhenHidden = true;
};

// The built-in safe defaults, including the default protected-process list.
Config defaultConfig();

// Clamps and normalizes every field; enforces threshold ordering
// (moderate < high < critical). Deterministic and pure.
Config normalizeConfig(Config c);

struct ConfigLoadResult {
    Config config;            // always usable (defaults if the file was unusable)
    bool fileExists = false;
    bool parsedClean = true;  // false when the file was missing/corrupt/invalid
    std::string error;        // human-readable description when parsedClean == false
};

ConfigLoadResult loadConfig(const std::string& path);
bool saveConfig(const std::string& path, const Config& cfg, std::string* error);

// Serializes to INI text (used by saveConfig and tests).
std::string configToIni(const Config& cfg);
// Parses INI text into a validated Config (used by loadConfig and tests).
ConfigLoadResult configFromIni(const std::string& text, bool fileExists);

}  // namespace wrg
