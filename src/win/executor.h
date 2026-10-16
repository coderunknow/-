// Executes engine-planned actions on Windows and records an audit history.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/engine.h"
#include "core/types.h"
#include "win/cpuopt.h"

namespace wrg {

class ActionExecutor {
public:
    ActionExecutor() = default;

    // Executes one planned action and updates engine bookkeeping (markApplied/
    // markRestored) on success. Always returns a record (success or failure).
    ActionRecord execute(const Config& cfg, const PlanAction& action,
                         const std::map<uint32_t, ProcSample>& byPid, EngineState& state);

    // Executes a batch; returns how many succeeded.
    int executeAll(const Config& cfg, const std::vector<PlanAction>& actions,
                   const std::map<uint32_t, ProcSample>& byPid, EngineState& state);

    // Restores everything recorded as applied (priority + throttle).
    // Returns the number of successful restorations.
    int restoreAll(const Config& cfg, EngineState& state,
                   const std::map<uint32_t, ProcSample>& byPid);

    const std::vector<ActionRecord>& history() const { return history_; }
    void clearHistory() { history_.clear(); }

    static const size_t kMaxHistory = 500;  // in-memory ring bound

private:
    ActionRecord makeRecord(const PlanAction& a, bool ok, unsigned err, const std::string& detail);
    void push(ActionRecord rec);

    CpuOptimizer cpu_;
    std::vector<ActionRecord> history_;
};

}  // namespace wrg
