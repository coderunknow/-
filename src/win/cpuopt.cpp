#include "win/cpuopt.h"

#include <windows.h>

#include <cstring>

namespace wrg {

namespace {

// PROCESS_POWER_THROTTLING_STATE (winbase.h, Windows 10 1709+ SDK). Defined
// here defensively so the code compiles with older MinGW headers.
#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
typedef struct _WRG_PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} WRG_PROCESS_POWER_THROTTLING_STATE;
#define WRG_PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define WRG_PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#define WRG_ProcessPowerThrottling 3
#else
typedef PROCESS_POWER_THROTTLING_STATE WRG_PROCESS_POWER_THROTTLING_STATE;
#define WRG_PROCESS_POWER_THROTTLING_CURRENT_VERSION PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define WRG_PROCESS_POWER_THROTTLING_EXECUTION_SPEED PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define WRG_ProcessPowerThrottling ProcessPowerThrottling
#endif

const DWORD kQueryRights = PROCESS_QUERY_LIMITED_INFORMATION;
const DWORD kModifyRights = PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION;

}  // namespace

std::string win32ErrorText(unsigned err) {
    if (err == 0) return "ok";
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string out;
    if (n && buf) {
        out.assign("Win32 error ");
        out += std::to_string(err);
        out += ": ";
        for (DWORD i = 0; i < n; ++i) {
            wchar_t ch = buf[i];
            if (ch == L'\r' || ch == L'\n') break;
            if (ch < 128) out.push_back(static_cast<char>(ch));
        }
        LocalFree(buf);
    } else {
        out = "Win32 error " + std::to_string(err);
    }
    return out;
}

bool CpuOptimizer::powerThrottleAvailable() {
    // SetProcessInformation is documented since Windows 8; the
    // ProcessPowerThrottling information class is honored on Windows 10 1709+.
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return false;
    return GetProcAddress(k32, "SetProcessInformation") != nullptr;
}

int CpuOptimizer::queryPriorityClass(uint32_t pid) {
    HANDLE h = OpenProcess(kQueryRights, FALSE, pid);
    if (!h) return 0;
    const DWORD pc = GetPriorityClass(h);
    CloseHandle(h);
    return static_cast<int>(pc);
}

bool CpuOptimizer::lowerPriority(uint32_t pid, int* originalPriorityClass, unsigned* win32Error) {
    if (win32Error) *win32Error = 0;
    HANDLE h = OpenProcess(kModifyRights, FALSE, pid);
    if (!h) {
        if (win32Error) *win32Error = GetLastError();
        return false;
    }
    const DWORD current = GetPriorityClass(h);
    if (current == 0) {
        if (win32Error) *win32Error = GetLastError();
        CloseHandle(h);
        return false;
    }
    if (current == REALTIME_PRIORITY_CLASS) {
        // Hard safety rule: never modify real-time processes.
        if (win32Error) *win32Error = ERROR_ACCESS_DENIED;
        CloseHandle(h);
        return false;
    }
    if (originalPriorityClass) *originalPriorityClass = static_cast<int>(current);
    savedPriority_[pid] = static_cast<int>(current);
    if (current == BELOW_NORMAL_PRIORITY_CLASS || current == IDLE_PRIORITY_CLASS) {
        CloseHandle(h);
        return true;  // already low; nothing to change, still recordable as ok
    }
    if (!SetPriorityClass(h, BELOW_NORMAL_PRIORITY_CLASS)) {
        if (win32Error) *win32Error = GetLastError();
        savedPriority_.erase(pid);
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    return true;
}

bool CpuOptimizer::restorePriority(uint32_t pid, unsigned* win32Error) {
    if (win32Error) *win32Error = 0;
    auto it = savedPriority_.find(pid);
    const int target = (it != savedPriority_.end()) ? it->second : kPriorityNormal;
    HANDLE h = OpenProcess(kModifyRights, FALSE, pid);
    if (!h) {
        if (win32Error) *win32Error = GetLastError();
        return false;
    }
    bool ok = SetPriorityClass(h, static_cast<DWORD>(target)) != 0;
    if (!ok) {
        if (win32Error) *win32Error = GetLastError();
    } else {
        savedPriority_.erase(pid);
    }
    CloseHandle(h);
    return ok;
}

bool CpuOptimizer::enablePowerThrottle(uint32_t pid, unsigned* win32Error) {
    if (win32Error) *win32Error = 0;
    if (!powerThrottleAvailable()) {
        if (win32Error) *win32Error = ERROR_NOT_SUPPORTED;
        return false;
    }
    HANDLE h = OpenProcess(kModifyRights, FALSE, pid);
    if (!h) {
        if (win32Error) *win32Error = GetLastError();
        return false;
    }
    WRG_PROCESS_POWER_THROTTLING_STATE st;
    std::memset(&st, 0, sizeof(st));
    st.Version = WRG_PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = WRG_PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask = WRG_PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    const BOOL ok = SetProcessInformation(h, static_cast<PROCESS_INFORMATION_CLASS>(WRG_ProcessPowerThrottling),
                                          &st, sizeof(st));
    if (!ok && win32Error) *win32Error = GetLastError();
    CloseHandle(h);
    return ok == TRUE;
}

bool CpuOptimizer::disablePowerThrottle(uint32_t pid, unsigned* win32Error) {
    if (win32Error) *win32Error = 0;
    if (!powerThrottleAvailable()) {
        if (win32Error) *win32Error = ERROR_NOT_SUPPORTED;
        return false;
    }
    HANDLE h = OpenProcess(kModifyRights, FALSE, pid);
    if (!h) {
        if (win32Error) *win32Error = GetLastError();
        return false;
    }
    WRG_PROCESS_POWER_THROTTLING_STATE st;
    std::memset(&st, 0, sizeof(st));
    st.Version = WRG_PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = WRG_PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask = 0;  // no throttling
    const BOOL ok = SetProcessInformation(h, static_cast<PROCESS_INFORMATION_CLASS>(WRG_ProcessPowerThrottling),
                                          &st, sizeof(st));
    if (!ok && win32Error) *win32Error = GetLastError();
    CloseHandle(h);
    return ok == TRUE;
}

}  // namespace wrg
