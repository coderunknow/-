#include "win/executor.h"

#include <windows.h>

#include <cstdio>

#include "core/stats.h"
#include "win/platform.h"
#include "win/wsopt.h"

namespace wrg {

ActionRecord ActionExecutor::makeRecord(const PlanAction& a, bool ok, unsigned err,
                                        const std::string& detail) {
    ActionRecord rec;
    rec.timestampMs = nowWallMs();
    rec.type = a.type;
    rec.pid = a.pid;
    rec.exeName = a.exeName;
    rec.ok = ok;
    rec.win32Error = err;
    rec.detail = detail;
    return rec;
}

void ActionExecutor::push(ActionRecord rec) {
    if (history_.size() >= kMaxHistory) history_.erase(history_.begin());
    history_.push_back(std::move(rec));
}

ActionRecord ActionExecutor::execute(const Config& cfg, const PlanAction& action,
                                     const std::map<uint32_t, ProcSample>& byPid,
                                     EngineState& state) {
    (void)cfg;
    unsigned err = 0;
    auto pit = byPid.find(action.pid);
    const ProcSample sample = pit != byPid.end() ? pit->second : ProcSample{};

    switch (action.type) {
        case ActionType::SetPriorityBelowNormal: {
            int original = 0;
            const bool ok = cpu_.lowerPriority(action.pid, &original, &err);
            std::string detail = action.detail;
            if (ok) {
                Engine::markApplied(state, action, original != 0 ? original : kPriorityNormal);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "; priority class %d -> %d (restorable)",
                              original, kPriorityBelowNormal);
                detail += buf;
            } else {
                detail += "; failed: " + win32ErrorText(err);
            }
            ActionRecord rec = makeRecord(action, ok, err, detail);
            push(rec);
            return rec;
        }
        case ActionType::RestorePriority: {
            const bool ok = cpu_.restorePriority(action.pid, &err);
            std::string detail = action.detail;
            if (ok) {
                Engine::markRestored(state, action.pid);
                detail += "; priority class restored";
            } else {
                detail += "; failed: " + win32ErrorText(err);
            }
            ActionRecord rec = makeRecord(action, ok, err, detail);
            push(rec);
            return rec;
        }
        case ActionType::EnablePowerThrottle: {
            const bool ok = cpu_.enablePowerThrottle(action.pid, &err);
            std::string detail = action.detail;
            if (ok) {
                Engine::markApplied(state, action, 0);
                detail += "; execution-speed throttle active (restorable)";
            } else {
                detail += "; failed: " + win32ErrorText(err);
                if (err == ERROR_NOT_SUPPORTED)
                    detail = action.detail + "; power throttling API unavailable on this Windows build";
            }
            ActionRecord rec = makeRecord(action, ok, err, detail);
            push(rec);
            return rec;
        }
        case ActionType::DisablePowerThrottle: {
            const bool ok = cpu_.disablePowerThrottle(action.pid, &err);
            std::string detail = action.detail;
            if (ok) {
                Engine::markRestored(state, action.pid);
                detail += "; throttle removed";
            } else {
                detail += "; failed: " + win32ErrorText(err);
            }
            ActionRecord rec = makeRecord(action, ok, err, detail);
            push(rec);
            return rec;
        }
        case ActionType::TrimWorkingSet: {
            const TrimResult tr = WorkingSetOptimizer::trim(
                action.pid, sample.isProtected || sample.isExcluded, sample.isForegroundTree,
                sample.isSelf);
            std::string detail = action.detail + "; " + tr.detail;
            ActionRecord rec = makeRecord(action, tr.ok, tr.win32Error, detail);
            push(rec);
            return rec;
        }
        default: {
            ActionRecord rec = makeRecord(action, true, 0, action.detail);
            push(rec);
            return rec;
        }
    }
}

int ActionExecutor::executeAll(const Config& cfg, const std::vector<PlanAction>& actions,
                               const std::map<uint32_t, ProcSample>& byPid, EngineState& state) {
    int okCount = 0;
    for (const auto& a : actions) {
        const ActionRecord rec = execute(cfg, a, byPid, state);
        if (rec.ok) ++okCount;
    }
    return okCount;
}

int ActionExecutor::restoreAll(const Config& cfg, EngineState& state,
                               const std::map<uint32_t, ProcSample>& byPid) {
    const std::vector<PlanAction> plan = Engine::planRestoreAll(state);
    return executeAll(cfg, plan, byPid, state);
}

}  // namespace wrg
