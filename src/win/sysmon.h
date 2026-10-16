// System-wide resource monitoring (physical memory, commit, CPU).
#pragma once

#include <cstdint>

#include "core/stats.h"
#include "core/types.h"

namespace wrg {

// Samples system memory and raw CPU counters. CPU percent is filled from
// GetSystemTimes deltas over elapsed wall time, normalized across logical CPUs
// (100% == every logical processor busy).
class SystemMonitor {
public:
    SystemMonitor();

    SysSample sample();

    unsigned logicalProcessors() const { return lp_; }

private:
    bool hasLast_ = false;
    uint64_t prevBusy100ns_ = 0;  // kernel+user cumulative
    uint64_t prevIdle100ns_ = 0;
    uint64_t prevWall100ns_ = 0;
    unsigned lp_ = 1;
};

}  // namespace wrg
