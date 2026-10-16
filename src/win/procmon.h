// Browser process discovery, metrics, and tree aggregation.
//
// All measurements are process-level (documented Win32 APIs). The monitor never
// invents per-tab data: Chromium-based browsers do not expose tab identity
// through process APIs (see DESIGN_LOG.md native-only limitations).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/types.h"

namespace wrg {

struct GroupAgg {
    uint32_t rootPid = 0;
    int family = 0;               // 1 chrome, 2 brave, 3 edge
    std::string exeName;          // browser executable name
    uint32_t procCount = 0;
    double cpuPercent = 0.0;      // sum of member processes (normalized)
    uint64_t wsBytes = 0;
    uint64_t privateBytes = 0;
    uint64_t commitBytes = 0;
    uint32_t foregroundPid = 0;   // foreground member (0 if none)
};

class ProcessMonitor {
public:
    ProcessMonitor();

    // Enumerates all processes, measures browser and self metrics, computes
    // per-process CPU% from the previous snapshot, groups browser trees and
    // applies protection/exclusion/foreground flags from cfg.
    std::vector<ProcSample> snapshot(const Config& cfg);

    // Current foreground window's process id (0 if none/unknown).
    static uint32_t foregroundPid();

    // Aggregates samples into browser groups (no double counting: every
    // process belongs to at most one group).
    static std::vector<GroupAgg> aggregate(const std::vector<ProcSample>& procs);

    // Self metrics of this application (for overhead reporting).
    struct SelfMetrics {
        double cpuPercent = 0.0;
        uint64_t wsBytes = 0;
        uint64_t privateBytes = 0;
    };
    SelfMetrics selfMetrics() const { return self_; }

    int lastErrors() const { return lastErrors_; }  // inaccessible/failed processes

private:
    struct CpuCacheEntry {
        uint64_t createTime = 0;
        uint64_t cpu100ns = 0;
        uint64_t wall100ns = 0;
        bool valid = false;
    };
    std::map<uint32_t, CpuCacheEntry> cpuCache_;
    SelfMetrics self_;
    int lastErrors_ = 0;
    unsigned lp_ = 1;
};

// Browser family classification from a lower-case executable name.
int browserFamilyFromName(const std::string& exeNameLower);

}  // namespace wrg
