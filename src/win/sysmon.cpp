#include "win/sysmon.h"

#include <windows.h>

namespace wrg {

namespace {
uint64_t fileTimeToU64(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}
}  // namespace

SystemMonitor::SystemMonitor() {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    lp_ = si.dwNumberOfProcessors == 0 ? 1u : si.dwNumberOfProcessors;
}

SysSample SystemMonitor::sample() {
    SysSample s;

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        s.totalPhysBytes = ms.ullTotalPhys;
        s.availPhysBytes = ms.ullAvailPhys;
        s.commitTotalBytes = ms.ullTotalPageFile - ms.ullAvailPageFile;
        s.commitLimitBytes = ms.ullTotalPageFile;
        s.memoryLoadPercent = static_cast<int>(ms.dwMemoryLoad);
    }

    FILETIME idleFt, kernelFt, userFt, wallFt;
    GetSystemTimeAsFileTime(&wallFt);
    const uint64_t wall = fileTimeToU64(wallFt);
    if (GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        const uint64_t idle = fileTimeToU64(idleFt);
        const uint64_t kernel = fileTimeToU64(kernelFt);  // includes idle time
        const uint64_t user = fileTimeToU64(userFt);
        const uint64_t busy = kernel + user;  // cumulative across all processors
        if (hasLast_) {
            if (wall > prevWall100ns_ && busy >= prevBusy100ns_ && idle >= prevIdle100ns_) {
                const uint64_t dWall = wall - prevWall100ns_;
                const uint64_t dBusy = busy - prevBusy100ns_;
                const uint64_t dIdle = idle - prevIdle100ns_;
                if (dBusy >= dIdle) {
                    s.cpuPercent = cpuPercentFromDeltas(0, dWall, 0, dBusy - dIdle, lp_);
                }
            }
        }
        prevBusy100ns_ = busy;
        prevIdle100ns_ = idle;
        prevWall100ns_ = wall;
        hasLast_ = true;
    }

    s.valid = s.totalPhysBytes != 0;
    return s;
}

}  // namespace wrg
