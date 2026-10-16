// CPU optimization: process priority and execution-speed power throttling.
//
// Scope (brief section C): lower priority of eligible background work only when
// justified, never assign real-time priority, never touch foreground/protected
// processes, restore previous settings, handle permission failures and
// unavailable APIs correctly. Process priority is NOT a CPU usage limit.
#pragma once

#include <map>
#include <string>

#include "core/types.h"

namespace wrg {

class CpuOptimizer {
public:
    // Lowers the process to BELOW_NORMAL. Saves the original priority class for
    // restorePriority(). Refuses realtime-class processes and any OpenProcess
    // failure (recorded in *win32Error).
    bool lowerPriority(uint32_t pid, int* originalPriorityClass, unsigned* win32Error);

    // Restores the saved priority class for pid.
    bool restorePriority(uint32_t pid, unsigned* win32Error);

    // Enables PROCESS_POWER_THROTTLING_EXECUTION_SPEED (documented on
    // Windows 10 1709+). On older builds the API reports unavailability —
    // that is surfaced honestly, not hidden.
    bool enablePowerThrottle(uint32_t pid, unsigned* win32Error);
    bool disablePowerThrottle(uint32_t pid, unsigned* win32Error);

    // True when the power-throttling API is present on this system.
    static bool powerThrottleAvailable();

    // Reads the current priority class (0 on failure).
    static int queryPriorityClass(uint32_t pid);

private:
    std::map<uint32_t, int> savedPriority_;
};

// Human-readable Win32 error text (UTF-8).
std::string win32ErrorText(unsigned err);

}  // namespace wrg
