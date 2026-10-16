#include "core/types.h"

#include <algorithm>
#include <cctype>

namespace wrg {

const char* profileName(Profile p) {
    switch (p) {
        case Profile::Saver: return "Saver";
        case Profile::Balanced: return "Balanced";
        case Profile::Performance: return "Performance";
    }
    return "Balanced";
}

const char* profileKey(Profile p) {
    switch (p) {
        case Profile::Saver: return "saver";
        case Profile::Balanced: return "balanced";
        case Profile::Performance: return "performance";
    }
    return "balanced";
}

bool profileFromString(const std::string& s, Profile* out) {
    std::string v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // trim
    auto notSpace = [](char c) { return !std::isspace(static_cast<unsigned char>(c)); };
    v.erase(v.begin(), std::find_if(v.begin(), v.end(), notSpace));
    v.erase(std::find_if(v.rbegin(), v.rend(), notSpace).base(), v.end());
    if (v == "saver") { *out = Profile::Saver; return true; }
    if (v == "balanced" || v == "default") { *out = Profile::Balanced; return true; }
    if (v == "performance" || v == "perf") { *out = Profile::Performance; return true; }
    return false;
}

const char* pressureLevelName(PressureLevel l) {
    switch (l) {
        case PressureLevel::None: return "None";
        case PressureLevel::Low: return "Low";
        case PressureLevel::Moderate: return "Moderate";
        case PressureLevel::High: return "High";
        case PressureLevel::Critical: return "Critical";
    }
    return "None";
}

const char* actionTypeName(ActionType t) {
    switch (t) {
        case ActionType::None: return "none";
        case ActionType::SetPriorityBelowNormal: return "set-priority-below-normal";
        case ActionType::RestorePriority: return "restore-priority";
        case ActionType::EnablePowerThrottle: return "enable-power-throttle";
        case ActionType::DisablePowerThrottle: return "disable-power-throttle";
        case ActionType::TrimWorkingSet: return "trim-working-set";
        case ActionType::ManualOptimize: return "manual-optimize";
        case ActionType::RestoreAll: return "restore-all";
    }
    return "none";
}

}  // namespace wrg
