#include "win/platform.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

namespace wrg {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                                      nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &out[0], n);
    return out;
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &out[0], n,
                        nullptr, nullptr);
    return out;
}

uint64_t nowMonotonicMs() { return GetTickCount64(); }

int64_t nowWallMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // FILETIME is 100ns since 1601-01-01; convert to Unix epoch ms.
    const uint64_t ms1601 = u.QuadPart / 10000ull;
    return static_cast<int64_t>(ms1601 - 11644473600000ull);
}

std::string nowIso8601() {
    const time_t t = static_cast<time_t>(nowWallMs() / 1000);
    struct tm tmv;
#if defined(_MSC_VER)
    gmtime_s(&tmv, &t);
#else
    const struct tm* p = gmtime(&t);
    if (p) tmv = *p; else std::memset(&tmv, 0, sizeof(tmv));
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tmv.tm_year + 1900,
                  tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

std::string exePath() {
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return std::string();
    return narrow(std::wstring(buf.data(), n));
}

bool ensureDir(const std::string& path) {
    const std::wstring w = widen(path);
    if (CreateDirectoryW(w.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool dataDir(std::string* outPath) {
    wchar_t base[MAX_PATH + 1] = {0};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return false;
    std::string dir = narrow(base);
    if (!dir.empty() && dir.back() != '\\') dir.push_back('\\');
    dir += "WinResGov";
    if (!ensureDir(dir)) return false;
    if (outPath) *outPath = dir;
    return true;
}

bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev;
    DWORD size = 0;
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size))
        elevated = elev.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

std::string cpuBrandString() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ,
                      &key) != ERROR_SUCCESS)
        return "unknown";
    wchar_t buf[256] = {0};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    std::string out = "unknown";
    if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        out = narrow(buf);
    }
    RegCloseKey(key);
    return out;
}

unsigned logicalProcessorCount() {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    return si.dwNumberOfProcessors == 0 ? 1u : si.dwNumberOfProcessors;
}

std::string osVersionString() {
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn) {
            OSVERSIONINFOW vi;
            std::memset(&vi, 0, sizeof(vi));
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) {
                SYSTEM_INFO si;
                GetNativeSystemInfo(&si);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Windows %lu.%lu build %lu (%s)",
                              vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber,
                              si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64"
                                                                                        : "x86");
                return std::string(buf);
            }
        }
    }
    return "Windows (version query unavailable)";
}

namespace {
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"WinResGov";
}  // namespace

bool setStartup(bool enable, std::string* error) {
    HKEY key = nullptr;
    const LSTATUS openSt = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                          KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
    if (openSt != ERROR_SUCCESS) {
        if (error) *error = "cannot open HKCU Run key (error " + std::to_string(openSt) + ")";
        return false;
    }
    bool ok = true;
    if (enable) {
        const std::string path = exePath();
        if (path.empty()) {
            if (error) *error = "cannot determine executable path";
            ok = false;
        } else {
            const std::wstring value = L"\"" + widen(path) + L"\"";
            const LSTATUS st = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                              reinterpret_cast<const BYTE*>(value.c_str()),
                                              static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
            if (st != ERROR_SUCCESS) {
                if (error) *error = "RegSetValueEx failed (error " + std::to_string(st) + ")";
                ok = false;
            }
        }
    } else {
        // Delete only when the value points at this executable.
        wchar_t buf[4096] = {0};
        DWORD size = sizeof(buf) - sizeof(wchar_t);
        DWORD type = 0;
        const LSTATUS q = RegQueryValueExW(key, kRunValue, nullptr, &type,
                                           reinterpret_cast<LPBYTE>(buf), &size);
        if (q == ERROR_SUCCESS && type == REG_SZ) {
            std::wstring value(buf);
            // Strip surrounding quotes for comparison.
            if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
                value = value.substr(1, value.size() - 2);
            const std::wstring self = widen(exePath());
            if (_wcsicmp(value.c_str(), self.c_str()) == 0) {
                const LSTATUS st = RegDeleteValueW(key, kRunValue);
                if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
                    if (error) *error = "RegDeleteValue failed (error " + std::to_string(st) + ")";
                    ok = false;
                }
            } else if (!self.empty()) {
                if (error)
                    *error = "startup value points at a different executable; left untouched";
            }
        }
        // Not found -> already disabled, success.
    }
    RegCloseKey(key);
    return ok;
}

bool getStartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buf[4096] = {0};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    bool enabled = false;
    if (RegQueryValueExW(key, kRunValue, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ) {
        std::wstring value(buf);
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            value = value.substr(1, value.size() - 2);
        enabled = _wcsicmp(value.c_str(), widen(exePath()).c_str()) == 0;
    }
    RegCloseKey(key);
    return enabled;
}

}  // namespace wrg
