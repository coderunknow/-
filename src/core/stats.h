// Portable statistics helpers. All functions are pure and testable on any host.
#pragma once

#include <cstdint>
#include <string>

namespace wrg {

// Computes normalized CPU utilization in percent (0..100) from two samples of
// cumulative CPU time and wall time, both in 100 ns units.
// cpuTime is (kernel+user) time; for per-process use pass the process CPU time.
// logicalProcessors scales the result (100% == every logical processor busy).
// Returns 0.0 for zero/negative/overflowing deltas. Result is clamped to [0,100].
double cpuPercentFromDeltas(uint64_t prevWall100ns, uint64_t currWall100ns,
                            uint64_t prevCpu100ns, uint64_t currCpu100ns,
                            unsigned logicalProcessors);

// Used-memory percent (0..100) from total/available physical bytes.
double usedMemPercent(uint64_t totalBytes, uint64_t availBytes);

// Human-readable byte formatting: "512 B", "12.3 KB", "1.23 MB", "4.56 GB".
std::string formatBytes(uint64_t bytes);

// Formats a percent with one decimal, e.g. "12.3%".
std::string formatPercent(double pct);

// Clamps a double into [lo, hi].
double clampd(double v, double lo, double hi);

}  // namespace wrg
