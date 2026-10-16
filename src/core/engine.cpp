#include "core/engine.h"

#include <algorithm>
#include <cstdio>

#include "core/stats.h"

namespace wrg {
namespace {

double usedPct(const EngineInput& in) {
    return usedMemPercent(in.sys.totalPhysBytes, in.sys.availPhysBytes);
}

// Highest pressure level supported by the measured memory/CPU state.
// When `hyst` > 0 the entry thresholds are lowered by `hyst`, which expresses
// "still supported under exit thresholds" (thermostat-style hysteresis).
PressureLevel levelFromMetrics(const Config& cfg, const EngineInput& in, double hyst) {
    const ProfileThresholds& t = cfg.thresholds[static_cast<int>(in.profile)];
    const double used = usedPct(in);
    const double cpu = in.sys.cpuPercent;

    PressureLevel lvl = PressureLevel::None;
    if (used >= t.memModerateUsedPct - 10.0 - hyst) lvl = PressureLevel::Low;
    if (used >= t.memModerateUsedPct - hyst) lvl = PressureLevel::Moderate;
    if (used >= t.memHighUsedPct - hyst) lvl = PressureLevel::High;
    if (used >= t.memCriticalUsedPct - hyst) lvl = PressureLevel::Critical;

    // CPU pressure can force at least High (and Critical when extreme).
    if (cpu >= t.cpuHighPct - hyst && lvl < PressureLevel::High) {
        lvl = PressureLevel::High;
    }
    if (cpu >= t.cpuHighPct + 10.0 - hyst && lvl < PressureLevel::Critical) {
        lvl = PressureLevel::Critical;
    }
    return lvl;
}

bool isEligibleTarget(const Config& cfg, const ProcSample& p) {
    if (p.isSelf) return false;
    if (p.isProtected || p.isExcluded) return false;
    if (p.isForegroundTree) return false;
    if (!p.accessible) return false;
    if (p.priorityClass == kPriorityRealtime) return false;  // never modify RT processes
    if (p.browserFamily == 0) return false;  // v0.1 manages browser processes only
    if (p.isBrowserRoot && cfg.keepBrowserRootNormal) return false;
    return true;
}

}  // namespace

bool Engine::isActionEligible(const Config& cfg, const ProcSample& p, ActionType t) {
    switch (t) {
        case ActionType::SetPriorityBelowNormal:
            if (!isEligibleTarget(cfg, p)) return false;
            if (p.priorityClass == kPriorityRealtime) return false;  // never touch RT
            if (p.priorityClass == kPriorityBelowNormal || p.priorityClass == kPriorityIdle)
                return false;  // already low
            return true;
        case ActionType::EnablePowerThrottle:
        case ActionType::TrimWorkingSet:
            return isEligibleTarget(cfg, p);
        case ActionType::RestorePriority:
        case ActionType::DisablePowerThrottle:
            return !p.isSelf;  // restore membership is checked against engine state
        default:
            return false;
    }
}

std::string Engine::cooldownKey(ActionType t, uint32_t pid) {
    return std::string(actionTypeName(t)) + "|" + std::to_string(pid);
}

PressureLevel Engine::rawLevel(const Config& cfg, const EngineInput& in) {
    return levelFromMetrics(cfg, in, 0.0);
}

EngineDecision Engine::decide(const Config& cfgIn, const EngineInput& in, EngineState& state) {
    const Config cfg = normalizeConfig(cfgIn);
    EngineDecision d;
    const uint64_t now = in.nowMs;

    if (!in.sys.valid) {
        d.level = state.level;
        d.rationale = "system metrics unavailable; no automatic actions";
        return d;
    }

    const PressureLevel raw = levelFromMetrics(cfg, in, 0.0);
    const PressureLevel exitSupported = levelFromMetrics(cfg, in, cfg.hysteresisPct);

    bool levelChanged = false;

    if (raw > state.level) {
        // Escalation: Critical is immediate, otherwise require sustained pressure.
        if (state.pendingHigher != raw) {
            state.pendingHigher = raw;
            state.pendingHigherSinceMs = now;
        }
        const bool immediate = (raw == PressureLevel::Critical);
        const bool sustained =
            (now >= state.pendingHigherSinceMs) &&
            (now - state.pendingHigherSinceMs >= static_cast<uint64_t>(cfg.escalateDelayMs));
        if (immediate || sustained) {
            state.level = raw;
            state.levelSinceMs = now;
            state.pendingHigher = PressureLevel::None;
            levelChanged = true;
        }
    } else if (raw < state.level) {
        // De-escalation: hysteresis + minimum dwell to prevent oscillation.
        const bool exitOk = exitSupported < state.level;
        const bool dwellOk =
            (now >= state.levelSinceMs) &&
            (now - state.levelSinceMs >= static_cast<uint64_t>(cfg.minDwellMs));
        if (exitOk && dwellOk) {
            state.level = raw;
            state.levelSinceMs = now;
            state.pendingHigher = PressureLevel::None;
            levelChanged = true;
        }
    } else {
        state.pendingHigher = PressureLevel::None;
    }

    d.level = state.level;
    d.levelChanged = levelChanged;

    const double used = usedPct(in);
    char why[256];
    std::snprintf(why, sizeof(why),
                  "level=%s usedMem=%.1f%% cpu=%.1f%% profile=%s automation=%s",
                  pressureLevelName(state.level), used, in.sys.cpuPercent,
                  profileKey(in.profile), in.automationEnabled ? "on" : "off");
    d.rationale = why;

    if (!in.sys.valid) return d;

    // Recovery: when pressure has recovered, restore anything we changed earlier.
    // Restoration runs even when automation is disabled: undoing our own
    // modifications is a safety obligation, not an optimization (brief section C/D).
    if (state.level < PressureLevel::Moderate) {
        for (const auto& kv : state.savedPriorityClass) {
            PlanAction a;
            a.type = ActionType::RestorePriority;
            a.pid = kv.first;
            auto it = state.appliedExe.find(kv.first);
            a.exeName = it != state.appliedExe.end() ? it->second : "";
            a.detail = "pressure recovered; restoring previous priority class";
            const std::string key = cooldownKey(a.type, a.pid);
            auto last = state.lastPlannedMs.find(key);
            const uint64_t cool = static_cast<uint64_t>(cfg.restoreCooldownS) * 1000ull;
            if (last != state.lastPlannedMs.end() && now >= last->second && now - last->second < cool)
                continue;
            state.lastPlannedMs[key] = now;
            d.actions.push_back(a);
        }
        for (const auto& kv : state.throttledByUs) {
            if (!kv.second) continue;
            PlanAction a;
            a.type = ActionType::DisablePowerThrottle;
            a.pid = kv.first;
            auto it = state.appliedExe.find(kv.first);
            a.exeName = it != state.appliedExe.end() ? it->second : "";
            a.detail = "pressure recovered; removing execution-speed throttle";
            const std::string key = cooldownKey(a.type, a.pid);
            auto last = state.lastPlannedMs.find(key);
            const uint64_t cool = static_cast<uint64_t>(cfg.restoreCooldownS) * 1000ull;
            if (last != state.lastPlannedMs.end() && now >= last->second && now - last->second < cool)
                continue;
            state.lastPlannedMs[key] = now;
            d.actions.push_back(a);
        }
        std::sort(d.actions.begin(), d.actions.end(), [](const PlanAction& a, const PlanAction& b) {
            if (a.pid != b.pid) return a.pid < b.pid;
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        });
        return d;
    }

    // Automatic optimization actions require user-enabled automation.
    if (!in.automationEnabled) return d;

    // Build the eligible target list (browser processes, biggest first for trims).
    std::vector<const ProcSample*> targets;
    for (const auto& p : in.procs) {
        if (isEligibleTarget(cfg, p)) targets.push_back(&p);
    }
    std::vector<const ProcSample*> byWs = targets;
    std::sort(byWs.begin(), byWs.end(), [](const ProcSample* a, const ProcSample* b) {
        return a->wsBytes > b->wsBytes;
    });

    auto planIfReady = [&](ActionType type, const ProcSample& p, int cooldownS,
                           const std::string& detail) {
        if (!isActionEligible(cfg, p, type)) return;
        const std::string key = cooldownKey(type, p.pid);
        auto last = state.lastPlannedMs.find(key);
        const uint64_t cool = static_cast<uint64_t>(cooldownS) * 1000ull;
        if (last != state.lastPlannedMs.end() && now >= last->second && now - last->second < cool)
            return;
        if (type == ActionType::EnablePowerThrottle) {
            auto it = state.throttledByUs.find(p.pid);
            if (it != state.throttledByUs.end() && it->second) return;  // already throttled
        }
        state.lastPlannedMs[key] = now;
        PlanAction a;
        a.type = type;
        a.pid = p.pid;
        a.exeName = p.exeName;
        a.detail = detail;
        d.actions.push_back(a);
    };

    // Per-profile policy: which actions are justified at which level.
    const Profile prof = in.profile;
    const bool wantPriority =
        cfg.cpuOptEnabled &&
        ((prof == Profile::Saver && state.level >= PressureLevel::Moderate) ||
         (prof == Profile::Balanced && state.level >= PressureLevel::High));
    const bool wantThrottle =
        cfg.cpuOptEnabled && cfg.powerThrottleEnabled &&
        ((prof == Profile::Saver && state.level >= PressureLevel::Moderate) ||
         (prof == Profile::Balanced && state.level >= PressureLevel::Moderate) ||
         (prof == Profile::Performance && state.level >= PressureLevel::High));
    const bool wantTrim =
        cfg.wsTrimAutoEnabled && used >= cfg.thresholds[static_cast<int>(in.profile)].memHighUsedPct &&
        ((prof == Profile::Saver && state.level >= PressureLevel::High) ||
         (prof == Profile::Balanced && state.level >= PressureLevel::High));

    if (wantPriority) {
        for (const ProcSample* p : targets) {
            planIfReady(ActionType::SetPriorityBelowNormal, *p, cfg.priorityCooldownS,
                        "background browser work lowered to below-normal under memory/CPU pressure "
                        "(foreground and browser UI processes excluded)");
        }
    }
    if (wantThrottle) {
        for (const ProcSample* p : targets) {
            planIfReady(ActionType::EnablePowerThrottle, *p, cfg.throttleCooldownS,
                        "execution-speed power throttle enabled for background browser work");
        }
    }
    if (wantTrim) {
        // Consider at most 3 trim candidates per tick, biggest working sets first.
        // Candidates already in cooldown count against the budget: this prevents
        // "trim everything on a timer" behavior (see brief section D).
        int considered = 0;
        for (const ProcSample* p : byWs) {
            if (considered >= 3) break;
            ++considered;
            planIfReady(ActionType::TrimWorkingSet, *p, cfg.trimCooldownS,
                        "working-set trim under high memory pressure (temporary; may regrow)");
        }
    }

    std::sort(d.actions.begin(), d.actions.end(), [](const PlanAction& a, const PlanAction& b) {
        if (a.pid != b.pid) return a.pid < b.pid;
        return static_cast<int>(a.type) < static_cast<int>(b.type);
    });
    return d;
}

std::vector<PlanAction> Engine::planManual(const Config& cfgIn, const EngineInput& in,
                                           EngineState& state) {
    const Config cfg = normalizeConfig(cfgIn);
    std::vector<PlanAction> out;
    const uint64_t now = in.nowMs;

    std::vector<const ProcSample*> targets;
    std::vector<const ProcSample*> byWs;
    for (const auto& p : in.procs) {
        if (isEligibleTarget(cfg, p)) {
            targets.push_back(&p);
            byWs.push_back(&p);
        }
    }
    std::sort(byWs.begin(), byWs.end(), [](const ProcSample* a, const ProcSample* b) {
        return a->wsBytes > b->wsBytes;
    });

    auto add = [&](ActionType type, const ProcSample& p, int cooldownS, const std::string& detail) {
        if (!isActionEligible(cfg, p, type)) return;
        const std::string key = cooldownKey(type, p.pid);
        auto last = state.lastPlannedMs.find(key);
        const uint64_t cool = static_cast<uint64_t>(cooldownS) * 1000ull;
        // Manual passes honor cooldowns as well: repeated clicks must not spam
        // disruptive operations.
        if (last != state.lastPlannedMs.end() && now >= last->second && now - last->second < cool)
            return;
        if (type == ActionType::EnablePowerThrottle) {
            auto it = state.throttledByUs.find(p.pid);
            if (it != state.throttledByUs.end() && it->second) return;
        }
        state.lastPlannedMs[key] = now;
        PlanAction a;
        a.type = type;
        a.pid = p.pid;
        a.exeName = p.exeName;
        a.detail = detail;
        out.push_back(a);
    };

    if (cfg.cpuOptEnabled) {
        for (const ProcSample* p : targets) {
            add(ActionType::SetPriorityBelowNormal, *p, cfg.priorityCooldownS,
                "manual pass: background browser work set below-normal");
        }
        if (cfg.powerThrottleEnabled) {
            for (const ProcSample* p : targets) {
                add(ActionType::EnablePowerThrottle, *p, cfg.throttleCooldownS,
                    "manual pass: execution-speed throttle enabled");
            }
        }
    }
    if (cfg.wsTrimManualAllowed) {
        // Same bounded policy as automatic trims: at most 3 candidates per pass,
        // biggest working sets first; cooldown-skips still consume the budget.
        int considered = 0;
        for (const ProcSample* p : byWs) {
            if (considered >= 3) break;
            ++considered;
            add(ActionType::TrimWorkingSet, *p, cfg.trimCooldownS,
                "manual pass: working-set trim (temporary reduction; pages may be faulted back)");
        }
    }
    std::sort(out.begin(), out.end(), [](const PlanAction& a, const PlanAction& b) {
        if (a.pid != b.pid) return a.pid < b.pid;
        return static_cast<int>(a.type) < static_cast<int>(b.type);
    });
    return out;
}

std::vector<PlanAction> Engine::planRestoreAll(const EngineState& state) {
    std::vector<PlanAction> out;
    for (const auto& kv : state.savedPriorityClass) {
        PlanAction a;
        a.type = ActionType::RestorePriority;
        a.pid = kv.first;
        auto it = state.appliedExe.find(kv.first);
        a.exeName = it != state.appliedExe.end() ? it->second : "";
        a.detail = "restore-all: previous priority class " + std::to_string(kv.second);
        out.push_back(a);
    }
    for (const auto& kv : state.throttledByUs) {
        if (!kv.second) continue;
        PlanAction a;
        a.type = ActionType::DisablePowerThrottle;
        a.pid = kv.first;
        auto it = state.appliedExe.find(kv.first);
        a.exeName = it != state.appliedExe.end() ? it->second : "";
        a.detail = "restore-all: removing execution-speed throttle";
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const PlanAction& a, const PlanAction& b) {
        if (a.pid != b.pid) return a.pid < b.pid;
        return static_cast<int>(a.type) < static_cast<int>(b.type);
    });
    return out;
}

void Engine::markApplied(EngineState& state, const PlanAction& action,
                         int originalPriorityClass) {
    switch (action.type) {
        case ActionType::SetPriorityBelowNormal:
            state.savedPriorityClass[action.pid] = originalPriorityClass;
            state.appliedExe[action.pid] = action.exeName;
            break;
        case ActionType::EnablePowerThrottle:
            state.throttledByUs[action.pid] = true;
            state.appliedExe[action.pid] = action.exeName;
            break;
        default:
            break;
    }
}

void Engine::markRestored(EngineState& state, uint32_t pid) {
    state.savedPriorityClass.erase(pid);
    state.throttledByUs.erase(pid);
    if (state.savedPriorityClass.find(pid) == state.savedPriorityClass.end() &&
        state.throttledByUs.find(pid) == state.throttledByUs.end()) {
        state.appliedExe.erase(pid);
    }
}

}  // namespace wrg
