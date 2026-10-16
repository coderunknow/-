#include "win/app.h"

#include "win/platform.h"

namespace wrg {

bool App::init(std::string* error) {
    if (!dataDir(&dataDir_)) {
        dataDir_ = ".";
        if (error) *error = "data directory unavailable; using working directory";
    }
    configPath_ = dataDir_ + "\\config.ini";

    ConfigLoadResult loaded = loadConfig(configPath_);
    cfg_ = loaded.config;
    configClean_ = loaded.parsedClean;
    configError_ = loaded.error;
    if (!loaded.parsedClean && error && error->empty()) *error = loaded.error;

    // First run: write the initial configuration so users can find and edit it.
    if (!loaded.fileExists) {
        std::string saveErr;
        saveConfig(configPath_, cfg_, &saveErr);
    }

    inited_ = true;
    return true;
}

std::map<uint32_t, ProcSample> App::byPid() const {
    std::map<uint32_t, ProcSample> m;
    for (const auto& p : lastProcs_) m[p.pid] = p;
    return m;
}

EngineDecision App::tick() {
    lastSys_ = sysmon_.sample();
    lastProcs_ = procmon_.snapshot(cfg_);
    lastGroups_ = ProcessMonitor::aggregate(lastProcs_);

    EngineInput in;
    in.sys = lastSys_;
    in.procs = lastProcs_;
    in.profile = cfg_.profile;
    in.automationEnabled = cfg_.automationEnabled;
    in.nowMs = nowMonotonicMs();

    EngineDecision d = Engine::decide(cfg_, in, engineState_);
    if (!d.actions.empty()) {
        exec_.executeAll(cfg_, d.actions, byPid(), engineState_);
    }
    lastDecision_ = d;
    return d;
}

EngineDecision App::manualOptimize() {
    // Make sure we act on fresh data.
    lastSys_ = sysmon_.sample();
    lastProcs_ = procmon_.snapshot(cfg_);
    lastGroups_ = ProcessMonitor::aggregate(lastProcs_);

    EngineInput in;
    in.sys = lastSys_;
    in.procs = lastProcs_;
    in.profile = cfg_.profile;
    in.automationEnabled = cfg_.automationEnabled;
    in.nowMs = nowMonotonicMs();

    const auto plan = Engine::planManual(cfg_, in, engineState_);
    exec_.executeAll(cfg_, plan, byPid(), engineState_);

    EngineDecision d;
    d.level = engineState_.level;
    d.rationale = "manual optimize: " + std::to_string(plan.size()) + " action(s) planned";
    lastDecision_ = d;
    return d;
}

int App::restoreAll() {
    lastProcs_ = procmon_.snapshot(cfg_);
    return exec_.restoreAll(cfg_, engineState_, byPid());
}

bool App::saveSettings(std::string* error) {
    return saveConfig(configPath_, cfg_, error);
}

bool App::setStartupEnabled(bool enable, std::string* error) {
    const bool ok = setStartup(enable, error);
    if (ok) {
        cfg_.startWithWindows = enable;
        std::string saveErr;
        saveConfig(configPath_, cfg_, &saveErr);
    }
    return ok;
}

void App::setProfile(Profile p) {
    cfg_.profile = p;
    std::string err;
    saveConfig(configPath_, cfg_, &err);
}

void App::setAutomation(bool enabled) {
    cfg_.automationEnabled = enabled;
    std::string err;
    saveConfig(configPath_, cfg_, &err);
}

void App::setWsTrimAuto(bool enabled) {
    cfg_.wsTrimAutoEnabled = enabled;
    std::string err;
    saveConfig(configPath_, cfg_, &err);
}

}  // namespace wrg
