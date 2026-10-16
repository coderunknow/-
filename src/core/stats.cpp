#include "core/stats.h"

#include <cmath>
#include <cstdio>

namespace wrg {

double cpuPercentFromDeltas(uint64_t prevWall100ns, uint64_t currWall100ns,
                            uint64_t prevCpu100ns, uint64_t currCpu100ns,
                            unsigned logicalProcessors) {
    if (currWall100ns <= prevWall100ns) return 0.0;
    if (currCpu100ns < prevCpu100ns) return 0.0;  // counter reset / PID reuse
    const uint64_t wall = currWall100ns - prevWall100ns;
    const uint64_t cpu = currCpu100ns - prevCpu100ns;
    if (wall == 0) return 0.0;
    const unsigned lp = logicalProcessors == 0 ? 1u : logicalProcessors;
    // cpu/wall is the average number of busy processors; normalize to 0..100.
    const double pct = (static_cast<double>(cpu) / static_cast<double>(wall)) *
                       (100.0 / static_cast<double>(lp));
    return clampd(pct, 0.0, 100.0);
}

double usedMemPercent(uint64_t totalBytes, uint64_t availBytes) {
    if (totalBytes == 0) return 0.0;
    if (availBytes >= totalBytes) return 0.0;
    return clampd(100.0 * (static_cast<double>(totalBytes - availBytes) /
                           static_cast<double>(totalBytes)),
                  0.0, 100.0);
}

std::string formatBytes(uint64_t bytes) {
    char buf[64];
    if (bytes < 1024ull) {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ull * 1024) {
        std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024ull * 1024 * 1024) {
        std::snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    return std::string(buf);
}

std::string formatPercent(double pct) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f%%", clampd(pct, 0.0, 100.0));
    return std::string(buf);
}

double clampd(double v, double lo, double hi) {
    if (!(v == v)) return lo;  // NaN -> lo
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace wrg
