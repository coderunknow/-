#include "win/procmon.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "core/stats.h"
#include "win/platform.h"

namespace wrg {

namespace {

uint64_t ftToU64(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

std::string lowerAscii(const wchar_t* ws) {
    std::string out;
    for (const wchar_t* p = ws; *p; ++p) {
        wchar_t ch = *p;
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
        if (ch < 128) out.push_back(static_cast<char>(ch));
    }
    return out;
}

bool nameInList(const std::string& name, const std::vector<std::string>& list) {
    for (const auto& item : list)
        if (item == name) return true;
    return false;
}

// Private working set via the documented QueryWorkingSet API.
// Definition: working-set pages whose PSAPI_WORKING_SET_BLOCK::Shared bit is 0.
// Returns false when the API is unavailable/denied; *partial is set when the
// working set exceeded the measurement cap.
bool queryPrivateWs(HANDLE h, uint64_t* privateWs, bool* partial) {
    *privateWs = 0;
    *partial = false;
    const DWORD_PTR kMaxEntries = 500000;  // ~4 MB buffer on x64
    DWORD_PTR entries = 1 << 16;
    std::vector<unsigned char> buf(sizeof(ULONG_PTR) + entries * sizeof(PSAPI_WORKING_SET_BLOCK));

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (QueryWorkingSet(h, buf.data(), static_cast<DWORD>(buf.size()))) {
            auto* info = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(buf.data());
            uint64_t priv = 0;
            for (ULONG_PTR i = 0; i < info->NumberOfEntries; ++i) {
                if (info->WorkingSetInfo[i].Shared == 0) ++priv;
            }
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            *privateWs = priv * si.dwPageSize;
            return true;
        }
        const DWORD err = GetLastError();
        if (err == ERROR_BAD_LENGTH && attempt == 0) {
            auto* info = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(buf.data());
            DWORD_PTR need = info->NumberOfEntries + 4096;
            if (need > kMaxEntries) {
                need = kMaxEntries;
                *partial = true;
            }
            if (need <= entries) return false;
            entries = need;
            buf.resize(sizeof(ULONG_PTR) + entries * sizeof(PSAPI_WORKING_SET_BLOCK));
            continue;
        }
        return false;
    }
    return false;
}

}  // namespace

int browserFamilyFromName(const std::string& exeNameLower) {
    if (exeNameLower == "chrome.exe") return 1;
    if (exeNameLower == "brave.exe") return 2;
    if (exeNameLower == "msedge.exe") return 3;
    return 0;
}

ProcessMonitor::ProcessMonitor() {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    lp_ = si.dwNumberOfProcessors == 0 ? 1u : si.dwNumberOfProcessors;
}

uint32_t ProcessMonitor::foregroundPid() {
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return static_cast<uint32_t>(pid);
}

std::vector<ProcSample> ProcessMonitor::snapshot(const Config& cfg) {
    std::vector<ProcSample> out;
    lastErrors_ = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        lastErrors_ = 1;
        return out;
    }

    // ---- Pass 1: raw enumeration --------------------------------------------
    struct RawInfo {
        uint32_t pid = 0;
        uint32_t ppid = 0;
        std::string exeName;
    };
    std::map<uint32_t, RawInfo> raw;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            RawInfo r;
            r.pid = static_cast<uint32_t>(pe.th32ProcessID);
            r.ppid = static_cast<uint32_t>(pe.th32ParentProcessID);
            r.exeName = lowerAscii(pe.szExeFile);
            raw[r.pid] = r;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    const uint32_t selfPid = static_cast<uint32_t>(GetCurrentProcessId());
    const uint32_t fgPid = foregroundPid();
    FILETIME wallFt;
    GetSystemTimeAsFileTime(&wallFt);
    const uint64_t wall = ftToU64(wallFt);

    out.reserve(raw.size());
    std::map<uint32_t, size_t> index;  // pid -> index in out

    for (const auto& kv : raw) {
        const RawInfo& r = kv.second;
        ProcSample p;
        p.pid = r.pid;
        p.ppid = r.ppid;
        p.exeName = r.exeName;
        p.browserFamily = browserFamilyFromName(r.exeName);
        p.isSelf = (r.pid == selfPid);
        p.isProtected = nameInList(r.exeName, cfg.protectedProcesses);
        p.isExcluded = nameInList(r.exeName, cfg.excludedProcesses);
        if (p.isSelf) p.isProtected = true;  // hard self-protection

        if (r.pid == 0) {
            p.accessible = false;
            index[r.pid] = out.size();
            out.push_back(p);
            continue;
        }

        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_READ,
                               FALSE, r.pid);
        if (!h) {
            // Fall back to the lightest query right (still fails for protected
            // processes; that is recorded as an inaccessibility, not an error).
            h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.pid);
        }
        if (!h) {
            p.accessible = false;
            ++lastErrors_;
            index[r.pid] = out.size();
            out.push_back(p);
            continue;
        }

        // Executable path.
        {
            wchar_t path[4096];
            DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]) - 1);
            if (QueryFullProcessImageNameW(h, 0, path, &size)) {
                path[size] = 0;
                p.exePath = narrow(std::wstring(path, size));
            }
        }

        // CPU time + creation time (identity against PID reuse).
        FILETIME createFt, exitFt, kernelFt, userFt;
        uint64_t cpuTime = 0;
        uint64_t createTime = 0;
        if (GetProcessTimes(h, &createFt, &exitFt, &kernelFt, &userFt)) {
            cpuTime = ftToU64(kernelFt) + ftToU64(userFt);
            createTime = ftToU64(createFt);
        }

        // Memory counters (working set, private bytes, committed bytes).
        PROCESS_MEMORY_COUNTERS_EX pmc;
        std::memset(&pmc, 0, sizeof(pmc));
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            p.wsBytes = pmc.WorkingSetSize;
            p.privateBytes = pmc.PrivateUsage;
            p.commitBytes = pmc.PagefileUsage;
        } else {
            ++lastErrors_;
        }

        p.priorityClass = static_cast<int>(GetPriorityClass(h));

        // Private working set: browser processes and self only (bounded cost).
        if (p.browserFamily != 0 || p.isSelf) {
            uint64_t privWs = 0;
            bool partial = false;
            if (queryPrivateWs(h, &privWs, &partial)) {
                p.privateWsBytes = privWs;
                p.privateWsPartial = partial;
            }
        }

        CloseHandle(h);

        // Per-process CPU% from deltas (normalized across logical processors).
        auto it = cpuCache_.find(r.pid);
        if (it != cpuCache_.end() && it->second.valid && it->second.createTime == createTime &&
            createTime != 0) {
            if (wall > it->second.wall100ns && cpuTime >= it->second.cpu100ns) {
                p.cpuPercent = cpuPercentFromDeltas(it->second.wall100ns, wall,
                                                    it->second.cpu100ns, cpuTime, lp_);
            }
        }
        CpuCacheEntry e;
        e.createTime = createTime;
        e.cpu100ns = cpuTime;
        e.wall100ns = wall;
        e.valid = (createTime != 0);
        cpuCache_[r.pid] = e;

        if (p.isSelf) {
            self_.cpuPercent = p.cpuPercent;
            self_.wsBytes = p.wsBytes;
            self_.privateBytes = p.privateBytes;
        }

        index[r.pid] = out.size();
        out.push_back(p);
    }

    // ---- Pass 2: browser tree roots ----------------------------------------
    for (auto& p : out) {
        if (p.browserFamily == 0) continue;
        uint32_t cur = p.pid;
        uint32_t parent = p.ppid;
        std::set<uint32_t> seen;
        seen.insert(cur);
        while (true) {
            auto pit = raw.find(parent);
            if (pit == raw.end()) break;
            if (seen.count(parent)) break;  // cycle guard
            seen.insert(parent);
            if (browserFamilyFromName(pit->second.exeName) != p.browserFamily) break;
            cur = parent;
            parent = pit->second.ppid;
        }
        p.groupRootPid = cur;
        p.isBrowserRoot = (cur == p.pid);
    }

    // ---- Pass 3: foreground protection set ---------------------------------
    // Conservative rule (preserves foreground responsiveness): protect the
    // foreground process, its ancestors, and — when the foreground process is a
    // browser — the entire same-family tree it belongs to.
    std::set<uint32_t> fgSet;
    if (fgPid != 0) {
        fgSet.insert(fgPid);
        auto fit = raw.find(fgPid);
        uint32_t parent = fit != raw.end() ? fit->second.ppid : 0;
        int guard = 0;
        while (parent != 0 && guard++ < 64) {
            if (fgSet.count(parent)) break;
            fgSet.insert(parent);
            auto pit = raw.find(parent);
            if (pit == raw.end()) break;
            parent = pit->second.ppid;
        }
        // Same-family tree of the foreground process.
        int fgFamily = 0;
        for (const auto& p : out)
            if (p.pid == fgPid) fgFamily = p.browserFamily;
        if (fgFamily != 0) {
            uint32_t root = 0;
            for (const auto& p : out)
                if (p.pid == fgPid) root = p.groupRootPid;
            if (root != 0) {
                for (const auto& p : out)
                    if (p.groupRootPid == root && p.browserFamily == fgFamily) fgSet.insert(p.pid);
            }
        }
    }
    for (auto& p : out) {
        if (fgSet.count(p.pid)) p.isForegroundTree = true;
        if (p.pid == fgPid) p.isForeground = true;
    }

    // ---- Lifecycle: drop CPU cache entries for vanished processes ----------
    if (cpuCache_.size() > raw.size() * 2 + 64) {
        for (auto it = cpuCache_.begin(); it != cpuCache_.end();) {
            if (raw.find(it->first) == raw.end())
                it = cpuCache_.erase(it);
            else
                ++it;
        }
    }

    return out;
}

std::vector<GroupAgg> ProcessMonitor::aggregate(const std::vector<ProcSample>& procs) {
    std::map<std::pair<int, uint32_t>, GroupAgg> groups;
    for (const auto& p : procs) {
        if (p.browserFamily == 0) continue;
        const auto key = std::make_pair(p.browserFamily, p.groupRootPid ? p.groupRootPid : p.pid);
        GroupAgg& g = groups[key];
        if (g.procCount == 0) {
            g.rootPid = key.second;
            g.family = p.browserFamily;
            g.exeName = p.exeName;
        }
        ++g.procCount;
        g.cpuPercent += p.cpuPercent;
        g.wsBytes += p.wsBytes;
        g.privateBytes += p.privateBytes;
        g.commitBytes += p.commitBytes;
        if (p.isForeground) g.foregroundPid = p.pid;
    }
    std::vector<GroupAgg> out;
    out.reserve(groups.size());
    for (auto& kv : groups) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const GroupAgg& a, const GroupAgg& b) {
        if (a.family != b.family) return a.family < b.family;
        return a.rootPid < b.rootPid;
    });
    return out;
}

}  // namespace wrg
