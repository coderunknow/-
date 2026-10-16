// Application orchestrator: config + monitors + engine + executor lifecycle.
// Used by both the dashboard and the CLI modes (smoke/bench/selftest).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/engine.h"
#include "core/types.h"
#include "win/executor.h"
#include "win/procmon.h"
#include "win/sysmon.h"

namespace wrg {

class App {
public:
    App() = default;

    // Loads configuration (safe defaults on any failure) and prepares the data
    // directory. Never fails hard; *error carries a non-fatal description.
    bool init(std::string* error);

    // One monitoring/decision/execution cycle. Returns the engine decision.
    EngineDecision tick();

    // User actions.
    EngineDecision manualOptimize();      // one bounded pass, audited
    int restoreAll();                     // restore everything we changed
    bool saveSettings(std::string* error);
    bool setStartupEnabled(bool enable, std::string* error);

    // Accessors for the dashboard / CLI.
    Config& config() { return cfg_; }
    const Config& config() const { return cfg_; }
    void setProfile(Profile p);
    void setAutomation(bool enabled);
    void setWsTrimAuto(bool enabled);

    const SysSample& sys() const { return lastSys_; }
    const std::vector<ProcSample>& procs() const { return lastProcs_; }
    const std::vector<GroupAgg>& groups() const { return lastGroups_; }
    EngineState& engineState() { return engineState_; }
    const EngineState& engineState() const { return engineState_; }
    EngineDecision lastDecision() const { return lastDecision_; }
    ActionExecutor& executor() { return exec_; }
    ProcessMonitor& procmon() { return procmon_; }
    SystemMonitor& sysmon() { return sysmon_; }
    const std::string& dataDirPath() const { return dataDir_; }
    const std::string& configPath() const { return configPath_; }
    const std::string& configError() const { return configError_; }
    bool configLoadedCleanly() const { return configClean_; }

private:
    std::map<uint32_t, ProcSample> byPid() const;

    Config cfg_;
    SystemMonitor sysmon_;
    ProcessMonitor procmon_;
    EngineState engineState_;
    ActionExecutor exec_;
    SysSample lastSys_;
    std::vector<ProcSample> lastProcs_;
    std::vector<GroupAgg> lastGroups_;
    EngineDecision lastDecision_;
    std::string dataDir_;
    std::string configPath_;
    std::string configError_;
    bool configClean_ = false;
    bool inited_ = false;
};

}  // namespace wrg
