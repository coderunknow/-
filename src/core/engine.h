// Adaptive resource decision engine (pure logic, deterministic, testable).
//
// The engine consumes measured samples and produces planned actions. It never
// calls OS APIs itself. It applies:
//   - per-profile thresholds (Saver / Balanced / Performance),
//   - escalation delay + de-escalation hysteresis + minimum dwell (anti-oscillation),
//   - per-action cooldowns,
//   - hard safety rules (foreground tree, protected/excluded processes, self,
//     browser roots never lowered by default, no real-time priority ever).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/types.h"

namespace wrg {

struct EngineInput {
    SysSample sys;
    std::vector<ProcSample> procs;
    Profile profile = Profile::Balanced;
    bool automationEnabled = false;   // when false, decide() plans nothing automatic
    uint64_t nowMs = 0;               // monotonic ms (caller supplies)
};

struct EngineDecision {
    PressureLevel level = PressureLevel::None;
    bool levelChanged = false;
    std::vector<PlanAction> actions;  // actions to execute now (may be empty)
    std::string rationale;            // one-line explanation for the dashboard
};

// Persistent engine state across ticks. Owned by the caller (App/CLI/bench).
struct EngineState {
    PressureLevel level = PressureLevel::None;
    uint64_t levelSinceMs = 0;
    uint64_t pendingHigherSinceMs = 0;   // escalation-delay tracking
    PressureLevel pendingHigher = PressureLevel::None;
    // Last time an action key ("type|pid") was planned.
    std::map<std::string, uint64_t> lastPlannedMs;
    // PIDs we have modified and that must be restored when pressure recovers
    // (filled by the executor when an action succeeds, via markApplied).
    std::map<uint32_t, int> savedPriorityClass;   // pid -> original priority class
    std::map<uint32_t, bool> throttledByUs;       // pid -> true when we enabled throttle
    std::map<uint32_t, std::string> appliedExe;   // pid -> exe name (for restore logs)
};

class Engine {
public:
    // Decides the pressure level and plans actions for this tick.
    // Side effects: updates `state` (level bookkeeping and cooldown stamps).
    static EngineDecision decide(const Config& cfg, const EngineInput& in, EngineState& state);

    // Plans a user-initiated optimization pass. Bypasses pressure thresholds but
    // NEVER bypasses the hard safety rules (protected/excluded/foreground/self)
    // and only trims working sets when cfg.wsTrimManualAllowed is true.
    static std::vector<PlanAction> planManual(const Config& cfg, const EngineInput& in,
                                              EngineState& state);

    // Plans restoration of every action recorded via markApplied.
    static std::vector<PlanAction> planRestoreAll(const EngineState& state);

    // Executor bookkeeping (called after an action actually succeeded).
    static void markApplied(EngineState& state, const PlanAction& action,
                            int originalPriorityClass);
    static void markRestored(EngineState& state, uint32_t pid);

    // Exposed for tests.
    static PressureLevel rawLevel(const Config& cfg, const EngineInput& in);
    static bool isActionEligible(const Config& cfg, const ProcSample& p, ActionType t);
    static std::string cooldownKey(ActionType t, uint32_t pid);
};

}  // namespace wrg
