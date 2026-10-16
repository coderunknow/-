// Working-set (RAM) management (brief section D).
//
// SetProcessWorkingSetSize trim: releases resident pages. This is a TEMPORARY
// reduction — the process still needs its memory and will fault pages back in
// when accessed, which can cost latency. Never used on a timer; bounded,
// policy-gated, cooldown-limited (enforced by the engine), and always recorded
// with measured before/after values.
#pragma once

#include <cstdint>
#include <string>

#include "core/types.h"

namespace wrg {

struct TrimResult {
    bool ok = false;
    unsigned win32Error = 0;
    uint64_t wsBefore = 0;
    uint64_t wsAfter = 0;
    std::string detail;  // measured before/after or the failure reason
};

class WorkingSetOptimizer {
public:
    // Trims the working set of one eligible process. Hard safety re-checks are
    // performed here as defense in depth (caller passes flags from the engine).
    static TrimResult trim(uint32_t pid, bool isProtectedOrExcluded, bool isForegroundTree,
                           bool isSelf);

    // Reports current working set of a process (0 when inaccessible).
    static uint64_t queryWorkingSet(uint32_t pid);
};

}  // namespace wrg
