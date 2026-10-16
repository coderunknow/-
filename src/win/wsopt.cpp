#include "win/wsopt.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstring>

#include "core/stats.h"

namespace wrg {

uint64_t WorkingSetOptimizer::queryWorkingSet(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS pmc;
    std::memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    uint64_t ws = 0;
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) ws = pmc.WorkingSetSize;
    CloseHandle(h);
    return ws;
}

TrimResult WorkingSetOptimizer::trim(uint32_t pid, bool isProtectedOrExcluded,
                                     bool isForegroundTree, bool isSelf) {
    TrimResult r;
    // Defense in depth: the engine already filters, but never trim these.
    if (isSelf || isProtectedOrExcluded || isForegroundTree) {
        r.win32Error = ERROR_ACCESS_DENIED;
        r.detail = "refused: process is self/protected/excluded/foreground";
        return r;
    }

    HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION |
                               PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) {
        r.win32Error = GetLastError();
        r.detail = "OpenProcess failed";
        return r;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    std::memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) r.wsBefore = pmc.WorkingSetSize;

    // (SIZE_T)-1, (SIZE_T)-1 trims the working set to the minimum.
    if (!SetProcessWorkingSetSize(h, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1))) {
        r.win32Error = GetLastError();
        r.detail = "SetProcessWorkingSetSize failed";
        CloseHandle(h);
        return r;
    }

    std::memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) r.wsAfter = pmc.WorkingSetSize;
    CloseHandle(h);

    r.ok = true;
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "ws %llu -> %llu bytes (temporary; pages may be faulted back)",
                  static_cast<unsigned long long>(r.wsBefore),
                  static_cast<unsigned long long>(r.wsAfter));
    r.detail = buf;
    return r;
}

}  // namespace wrg
