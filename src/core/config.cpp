#include "core/config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/ini.h"
#include "core/stats.h"

namespace wrg {
namespace {

std::vector<std::string> defaultProtected() {
    // Windows-critical processes, security software, the shell, and ourselves.
    return {
        "system", "registry", "smss.exe", "csrss.exe", "wininit.exe", "winlogon.exe",
        "services.exe", "lsass.exe", "spoolsv.exe", "svchost.exe", "dwm.exe",
        "explorer.exe", "fontdrvhost.exe", "conhost.exe", "wmiprvse.exe",
        "msmpeng.exe", "securityhealthservice.exe", "searchindexer.exe",
        "winresgov.exe",
    };
}

bool parseBool(const std::string& s, bool fallback, bool* ok) {
    const std::string v = IniFile::toLower(IniFile::trim(s));
    if (v == "1" || v == "true" || v == "yes" || v == "on") { *ok = true; return true; }
    if (v == "0" || v == "false" || v == "no" || v == "off") { *ok = true; return false; }
    *ok = false;
    return fallback;
}

bool parseInt(const std::string& s, int fallback, int* ok, int* out) {
    const std::string v = IniFile::trim(s);
    if (v.empty()) { *ok = 0; *out = fallback; return false; }
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || (end && *end != '\0')) { *ok = 0; *out = fallback; return false; }
    *ok = 1;
    *out = static_cast<int>(n);
    return true;
}

bool parseDouble(const std::string& s, double fallback, int* ok, double* out) {
    const std::string v = IniFile::trim(s);
    if (v.empty()) { *ok = 0; *out = fallback; return false; }
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || (end && *end != '\0')) { *ok = 0; *out = fallback; return false; }
    *ok = 1;
    *out = d;
    return true;
}

std::vector<std::string> parseList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ';') {
            const std::string t = IniFile::toLower(IniFile::trim(cur));
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    const std::string t = IniFile::toLower(IniFile::trim(cur));
    if (!t.empty()) out.push_back(t);
    return out;
}

std::string joinList(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += v[i];
    }
    return out;
}

std::string boolStr(bool b) { return b ? "1" : "0"; }

void noteError(std::string* err, const std::string& msg) {
    if (!err->empty()) *err += "; ";
    *err += msg;
}

}  // namespace

Config defaultConfig() {
    Config c;
    // Saver: more sensitive (acts earlier).
    c.thresholds[0].memModerateUsedPct = 70.0;
    c.thresholds[0].memHighUsedPct = 80.0;
    c.thresholds[0].memCriticalUsedPct = 90.0;
    c.thresholds[0].cpuHighPct = 70.0;
    // Balanced: defaults as declared in the struct.
    c.thresholds[1].memModerateUsedPct = 75.0;
    c.thresholds[1].memHighUsedPct = 85.0;
    c.thresholds[1].memCriticalUsedPct = 92.0;
    c.thresholds[1].cpuHighPct = 80.0;
    // Performance: least sensitive (intervenes only under real stress).
    c.thresholds[2].memModerateUsedPct = 85.0;
    c.thresholds[2].memHighUsedPct = 92.0;
    c.thresholds[2].memCriticalUsedPct = 97.0;
    c.thresholds[2].cpuHighPct = 90.0;
    c.protectedProcesses = defaultProtected();
    return c;
}

Config normalizeConfig(Config c) {
    c.refreshMs = static_cast<int>(clampd(c.refreshMs, 250, 60000));
    c.hysteresisPct = clampd(c.hysteresisPct, 0.0, 25.0);
    c.escalateDelayMs = static_cast<int>(clampd(c.escalateDelayMs, 0, 60000));
    c.minDwellMs = static_cast<int>(clampd(c.minDwellMs, 0, 600000));
    c.priorityCooldownS = static_cast<int>(clampd(c.priorityCooldownS, 5, 3600));
    c.throttleCooldownS = static_cast<int>(clampd(c.throttleCooldownS, 5, 3600));
    c.trimCooldownS = static_cast<int>(clampd(c.trimCooldownS, 5, 3600));
    c.restoreCooldownS = static_cast<int>(clampd(c.restoreCooldownS, 5, 3600));
    c.benchSampleMs = static_cast<int>(clampd(c.benchSampleMs, 100, 10000));
    c.benchWindowSeconds = static_cast<int>(clampd(c.benchWindowSeconds, 5, 3600));

    for (auto& t : c.thresholds) {
        t.memModerateUsedPct = clampd(t.memModerateUsedPct, 1.0, 99.0);
        t.memHighUsedPct = clampd(t.memHighUsedPct, 1.0, 99.0);
        t.memCriticalUsedPct = clampd(t.memCriticalUsedPct, 1.0, 99.0);
        t.cpuHighPct = clampd(t.cpuHighPct, 1.0, 100.0);
        // Enforce strict ordering: moderate < high < critical.
        if (t.memHighUsedPct <= t.memModerateUsedPct)
            t.memHighUsedPct = t.memModerateUsedPct + 1.0;
        if (t.memCriticalUsedPct <= t.memHighUsedPct)
            t.memCriticalUsedPct = t.memHighUsedPct + 1.0;
        if (t.memCriticalUsedPct > 99.0) {
            t.memCriticalUsedPct = 99.0;
            if (t.memHighUsedPct >= 99.0) t.memHighUsedPct = 98.0;
            if (t.memModerateUsedPct >= t.memHighUsedPct) t.memModerateUsedPct = 97.0;
        }
    }

    // Self-protection must always be present; keep lists lower-cased and unique.
    auto fixList = [](std::vector<std::string> list) {
        std::vector<std::string> out;
        for (auto& item : list) {
            std::string t = IniFile::toLower(IniFile::trim(item));
            if (t.empty()) continue;
            bool dup = false;
            for (const auto& o : out)
                if (o == t) { dup = true; break; }
            if (!dup) out.push_back(t);
        }
        return out;
    };
    c.protectedProcesses = fixList(std::move(c.protectedProcesses));
    c.excludedProcesses = fixList(std::move(c.excludedProcesses));
    bool hasSelf = false;
    for (const auto& p : c.protectedProcesses)
        if (p == "winresgov.exe") hasSelf = true;
    if (!hasSelf) c.protectedProcesses.push_back("winresgov.exe");
    return c;
}

ConfigLoadResult configFromIni(const std::string& text, bool fileExists) {
    ConfigLoadResult r;
    r.fileExists = fileExists;
    Config def = defaultConfig();
    r.config = def;

    IniFile ini;
    ini.parse(text);
    std::string err;
    if (ini.malformedLines() > 0) {
        noteError(&err, "malformed lines skipped: " + std::to_string(ini.malformedLines()));
    }

    // [general]
    {
        bool ok = true;
        const std::string prof = ini.get("general", "profile", profileKey(def.profile));
        Profile p = def.profile;
        if (!profileFromString(prof, &p)) {
            noteError(&err, "invalid profile '" + prof + "' (using balanced)");
            ok = false;
        }
        r.config.profile = p;
        ok = true;
        r.config.automationEnabled =
            parseBool(ini.get("general", "automation_enabled", "0"), def.automationEnabled, &ok);
        if (!ok) noteError(&err, "invalid automation_enabled");
        r.config.startWithWindows =
            parseBool(ini.get("general", "start_with_windows", "0"), def.startWithWindows, &ok);
        if (!ok) noteError(&err, "invalid start_with_windows");
    }

    // Typed integer helper lambda.
    auto getInt = [&](const char* sec, const char* key, int defVal, int* field) {
        int ok = 0, v = defVal;
        parseInt(ini.get(sec, key, std::to_string(defVal)), defVal, &ok, &v);
        if (!ok) noteError(&err, std::string("invalid ") + sec + "/" + key);
        *field = v;
    };
    auto getDbl = [&](const char* sec, const char* key, double defVal, double* field) {
        int ok = 0;
        double v = defVal;
        parseDouble(ini.get(sec, key, std::to_string(defVal)), defVal, &ok, &v);
        if (!ok) noteError(&err, std::string("invalid ") + sec + "/" + key);
        *field = v;
    };
    auto getBool = [&](const char* sec, const char* key, bool defVal, bool* field) {
        bool ok = true;
        *field = parseBool(ini.get(sec, key, boolStr(defVal)), defVal, &ok);
        if (!ok) noteError(&err, std::string("invalid ") + sec + "/" + key);
    };

    getInt("general", "refresh_ms", def.refreshMs, &r.config.refreshMs);

    // [thresholds.saver|balanced|performance]
    const char* profileSecs[3] = {"thresholds.saver", "thresholds.balanced", "thresholds.performance"};
    for (int i = 0; i < 3; ++i) {
        getDbl(profileSecs[i], "mem_moderate_pct", def.thresholds[i].memModerateUsedPct,
               &r.config.thresholds[i].memModerateUsedPct);
        getDbl(profileSecs[i], "mem_high_pct", def.thresholds[i].memHighUsedPct,
               &r.config.thresholds[i].memHighUsedPct);
        getDbl(profileSecs[i], "mem_critical_pct", def.thresholds[i].memCriticalUsedPct,
               &r.config.thresholds[i].memCriticalUsedPct);
        getDbl(profileSecs[i], "cpu_high_pct", def.thresholds[i].cpuHighPct,
               &r.config.thresholds[i].cpuHighPct);
    }

    // [hysteresis]
    getDbl("hysteresis", "hysteresis_pct", def.hysteresisPct, &r.config.hysteresisPct);
    getInt("hysteresis", "escalate_delay_ms", def.escalateDelayMs, &r.config.escalateDelayMs);
    getInt("hysteresis", "min_dwell_ms", def.minDwellMs, &r.config.minDwellMs);

    // [action_cooldowns]
    getInt("action_cooldowns", "priority_cooldown_s", def.priorityCooldownS, &r.config.priorityCooldownS);
    getInt("action_cooldowns", "throttle_cooldown_s", def.throttleCooldownS, &r.config.throttleCooldownS);
    getInt("action_cooldowns", "trim_cooldown_s", def.trimCooldownS, &r.config.trimCooldownS);
    getInt("action_cooldowns", "restore_cooldown_s", def.restoreCooldownS, &r.config.restoreCooldownS);

    // [ws_trim]
    getBool("ws_trim", "enabled", def.wsTrimAutoEnabled, &r.config.wsTrimAutoEnabled);
    getBool("ws_trim", "manual_allowed", def.wsTrimManualAllowed, &r.config.wsTrimManualAllowed);

    // [cpu_opt]
    getBool("cpu_opt", "enabled", def.cpuOptEnabled, &r.config.cpuOptEnabled);
    getBool("cpu_opt", "power_throttle", def.powerThrottleEnabled, &r.config.powerThrottleEnabled);
    getBool("cpu_opt", "keep_browser_root_normal", def.keepBrowserRootNormal,
            &r.config.keepBrowserRootNormal);

    // [protection]
    if (ini.hasKey("protection", "protected"))
        r.config.protectedProcesses = parseList(ini.get("protection", "protected", ""));
    if (ini.hasKey("protection", "excluded"))
        r.config.excludedProcesses = parseList(ini.get("protection", "excluded", ""));

    // [benchmark]
    getInt("benchmark", "sample_interval_ms", def.benchSampleMs, &r.config.benchSampleMs);
    getInt("benchmark", "window_seconds", def.benchWindowSeconds, &r.config.benchWindowSeconds);

    // [dashboard]
    getBool("dashboard", "topmost", def.dashboardTopmost, &r.config.dashboardTopmost);
    getBool("dashboard", "minimize_updates_when_hidden", def.minimizeUpdatesWhenHidden,
            &r.config.minimizeUpdatesWhenHidden);

    r.config = normalizeConfig(std::move(r.config));
    r.parsedClean = err.empty();
    r.error = err;
    return r;
}

std::string configToIni(const Config& cfgIn) {
    const Config c = normalizeConfig(cfgIn);
    IniFile ini;
    ini.set("general", "profile", profileKey(c.profile));
    ini.set("general", "automation_enabled", boolStr(c.automationEnabled));
    ini.set("general", "start_with_windows", boolStr(c.startWithWindows));
    ini.set("general", "refresh_ms", std::to_string(c.refreshMs));

    const char* profileSecs[3] = {"thresholds.saver", "thresholds.balanced", "thresholds.performance"};
    for (int i = 0; i < 3; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f", c.thresholds[i].memModerateUsedPct);
        ini.set(profileSecs[i], "mem_moderate_pct", buf);
        std::snprintf(buf, sizeof(buf), "%.1f", c.thresholds[i].memHighUsedPct);
        ini.set(profileSecs[i], "mem_high_pct", buf);
        std::snprintf(buf, sizeof(buf), "%.1f", c.thresholds[i].memCriticalUsedPct);
        ini.set(profileSecs[i], "mem_critical_pct", buf);
        std::snprintf(buf, sizeof(buf), "%.1f", c.thresholds[i].cpuHighPct);
        ini.set(profileSecs[i], "cpu_high_pct", buf);
    }

    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f", c.hysteresisPct);
        ini.set("hysteresis", "hysteresis_pct", buf);
    }
    ini.set("hysteresis", "escalate_delay_ms", std::to_string(c.escalateDelayMs));
    ini.set("hysteresis", "min_dwell_ms", std::to_string(c.minDwellMs));

    ini.set("action_cooldowns", "priority_cooldown_s", std::to_string(c.priorityCooldownS));
    ini.set("action_cooldowns", "throttle_cooldown_s", std::to_string(c.throttleCooldownS));
    ini.set("action_cooldowns", "trim_cooldown_s", std::to_string(c.trimCooldownS));
    ini.set("action_cooldowns", "restore_cooldown_s", std::to_string(c.restoreCooldownS));

    ini.set("ws_trim", "enabled", boolStr(c.wsTrimAutoEnabled));
    ini.set("ws_trim", "manual_allowed", boolStr(c.wsTrimManualAllowed));

    ini.set("cpu_opt", "enabled", boolStr(c.cpuOptEnabled));
    ini.set("cpu_opt", "power_throttle", boolStr(c.powerThrottleEnabled));
    ini.set("cpu_opt", "keep_browser_root_normal", boolStr(c.keepBrowserRootNormal));

    ini.set("protection", "protected", joinList(c.protectedProcesses));
    ini.set("protection", "excluded", joinList(c.excludedProcesses));

    ini.set("benchmark", "sample_interval_ms", std::to_string(c.benchSampleMs));
    ini.set("benchmark", "window_seconds", std::to_string(c.benchWindowSeconds));

    ini.set("dashboard", "topmost", boolStr(c.dashboardTopmost));
    ini.set("dashboard", "minimize_updates_when_hidden", boolStr(c.minimizeUpdatesWhenHidden));

    return ini.serialize();
}

ConfigLoadResult loadConfig(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ConfigLoadResult r = configFromIni("", false);
        r.parsedClean = false;
        r.error = "config file not found; using safe defaults";
        return r;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    ConfigLoadResult r = configFromIni(ss.str(), true);
    return r;
}

bool saveConfig(const std::string& path, const Config& cfg, std::string* error) {
    const std::string text = configToIni(cfg);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot open config file for writing: " + path;
            return false;
        }
        out << text;
        if (!out.good()) {
            if (error) *error = "write failed: " + path;
            return false;
        }
    }
    return true;
}

}  // namespace wrg
