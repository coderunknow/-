// Core logic test suite for the Windows Resource Governor.
// Portable: compiled into tests/test_core.cpp (any host) and into the
// application's --selftest mode (Windows). Returns the number of failures.
#pragma once

#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/csv.h"
#include "core/engine.h"
#include "core/ini.h"
#include "core/stats.h"
#include "core/types.h"
#include "core/version.h"

namespace wrg {
namespace testing {

struct Counters {
    int checks = 0;
    int failures = 0;
};

inline void expect(Counters& c, bool cond, const std::string& name, const std::string& detail = "") {
    ++c.checks;
    if (!cond) {
        ++c.failures;
        std::cout << "FAIL: " << name;
        if (!detail.empty()) std::cout << " [" << detail << "]";
        std::cout << "\n";
    } else {
        std::cout << "PASS: " << name << "\n";
    }
}

inline void expectNear(Counters& c, double got, double want, double tol, const std::string& name) {
    std::ostringstream d;
    d << "got=" << got << " want=" << want << " tol=" << tol;
    expect(c, std::fabs(got - want) <= tol, name, d.str());
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

inline ProcSample makeBrowser(uint32_t pid, uint32_t root, const char* name,
                              uint64_t wsMB, int prio, bool foregroundTree = false) {
    ProcSample p;
    p.pid = pid;
    p.exeName = name;
    p.wsBytes = wsMB * 1024ull * 1024ull;
    p.privateBytes = p.wsBytes / 2;
    p.commitBytes = p.wsBytes;
    p.priorityClass = prio;
    p.browserFamily = 1;
    p.groupRootPid = root;
    p.isBrowserRoot = (pid == root);
    p.isForegroundTree = foregroundTree;
    p.accessible = true;
    return p;
}

inline EngineInput makeInput(Profile prof, double usedMemPct, double cpuPct,
                             std::vector<ProcSample> procs, bool automation = true,
                             uint64_t nowMs = 100000) {
    EngineInput in;
    in.profile = prof;
    in.automationEnabled = automation;
    in.nowMs = nowMs;
    in.sys.valid = true;
    in.sys.totalPhysBytes = 8ull * 1024 * 1024 * 1024;
    in.sys.availPhysBytes =
        static_cast<uint64_t>(in.sys.totalPhysBytes * (100.0 - usedMemPct) / 100.0);
    in.sys.cpuPercent = cpuPct;
    in.sys.memoryLoadPercent = static_cast<int>(usedMemPct);
    in.procs = std::move(procs);
    return in;
}

inline Config testConfig() {
    Config c = defaultConfig();
    c.automationEnabled = true;
    c.escalateDelayMs = 2000;
    c.minDwellMs = 15000;
    return normalizeConfig(c);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

inline void testIni(Counters& c) {
    IniFile ini;
    ini.parse("; comment\n[General]\nProfile = Saver\n\nbad line without equals\n[Thresholds.Saver]\nmem_high_pct=80.5\nmem_high_pct=81.5\n# another\nkey_with_empty=\n");
    expect(c, ini.get("general", "profile") == "Saver", "ini: value preserved (case of value)");
    expect(c, ini.get("thresholds.saver", "mem_high_pct") == "81.5", "ini: duplicate key last-wins");
    expect(c, ini.hasSection("general") && ini.hasKey("General", "PROFILE"), "ini: section/key case-insensitive");
    expect(c, ini.malformedLines() == 1, "ini: malformed line counted", std::to_string(ini.malformedLines()));
    expect(c, ini.get("missing", "key", "fb") == "fb", "ini: fallback works");
    expect(c, ini.serialize().find("[thresholds.saver]") != std::string::npos, "ini: serialize has sections");

    IniFile empty;
    empty.parse("");
    expect(c, empty.sections().empty() && empty.malformedLines() == 0, "ini: empty input is clean");

    IniFile broken;
    broken.parse("[no close\n=emptykey\n[normal]\nx=1\n");
    expect(c, broken.malformedLines() == 2, "ini: broken lines counted", std::to_string(broken.malformedLines()));
}

inline void testStats(Counters& c) {
    // Normalized CPU utilization: 100% == every logical processor busy.
    // 0.25 core-seconds over 1 s wall on 4 LP -> 6.25% normalized (25% of one core).
    const uint64_t wall0 = 0, wall1 = 10000000ull;
    expectNear(c, cpuPercentFromDeltas(wall0, wall1, 0, 2500000ull, 4), 6.25, 0.01,
               "stats: cpu 0.25 core on 4 LP -> 6.25%");
    // One full core busy on 4 cores -> 25%.
    expectNear(c, cpuPercentFromDeltas(0, 10000000ull, 0, 10000000ull, 4), 25.0, 0.01,
               "stats: cpu one core busy on 4 LP -> 25%");
    // Single logical processor, fully busy -> 100%.
    expectNear(c, cpuPercentFromDeltas(0, 10000000ull, 0, 10000000ull, 1), 100.0, 0.01,
               "stats: cpu percent single LP full -> 100%");
    expectNear(c, cpuPercentFromDeltas(0, 10000000ull, 0, 40000000ull, 4), 100.0, 0.01,
               "stats: cpu percent all cores full");
    expect(c, cpuPercentFromDeltas(100, 100, 0, 5, 4) == 0.0, "stats: zero wall delta -> 0");
    expect(c, cpuPercentFromDeltas(100, 50, 0, 5, 4) == 0.0, "stats: wall going backwards -> 0");
    expect(c, cpuPercentFromDeltas(0, 100, 50, 10, 4) == 0.0, "stats: cpu counter reset -> 0");
    expect(c, cpuPercentFromDeltas(0, 10000000ull, 0, 400000000ull, 4) == 100.0, "stats: clamp at 100");
    expectNear(c, usedMemPercent(1000, 250), 75.0, 0.01, "stats: used mem percent");
    expect(c, usedMemPercent(0, 0) == 0.0, "stats: zero total mem safe");
    expect(c, usedMemPercent(1000, 1500) == 0.0, "stats: avail > total clamps to 0");
    expect(c, formatBytes(512) == "512 B", "stats: formatBytes B");
    expect(c, formatBytes(2048) == "2.0 KB", "stats: formatBytes KB");
    expect(c, formatBytes(5ull * 1024 * 1024) == "5.00 MB", "stats: formatBytes MB");
    expect(c, formatPercent(12.34) == "12.3%", "stats: formatPercent");
}

inline void testConfig(Counters& c) {
    // Missing file -> safe defaults, flagged.
    ConfigLoadResult missing = loadConfig("/nonexistent/path/config.ini");
    expect(c, !missing.parsedClean && missing.fileExists == false, "config: missing file -> defaults + flag");
    expect(c, missing.config.profile == Profile::Balanced, "config: default profile balanced");
    expect(c, missing.config.startWithWindows == false, "config: startup disabled by default");
    expect(c, missing.config.wsTrimAutoEnabled == false, "config: auto ws trim disabled by default");
    expect(c, missing.config.automationEnabled == false, "config: automation disabled by default");

    // Corrupted file -> usable defaults, error reported.
    ConfigLoadResult bad = configFromIni("[general\nprofile=!!!\nrefresh_ms=abc\nautomation_enabled=maybe\n[thresholds.balanced]\nmem_high_pct=xyz\nmem_moderate_pct=200\n", true);
    expect(c, !bad.parsedClean, "config: corrupt values flagged");
    expect(c, bad.config.profile == Profile::Balanced, "config: invalid profile -> balanced");
    expect(c, bad.config.refreshMs == 1000, "config: invalid refresh_ms -> default");
    expect(c, bad.config.automationEnabled == false, "config: invalid bool -> default");
    // mem_moderate clamps to <=99 and ordering is enforced.
    const auto& tb = bad.config.thresholds[static_cast<int>(Profile::Balanced)];
    expect(c, tb.memModerateUsedPct < tb.memHighUsedPct && tb.memHighUsedPct < tb.memCriticalUsedPct,
           "config: threshold ordering enforced");
    expect(c, tb.memModerateUsedPct <= 99.0, "config: threshold clamped", std::to_string(tb.memModerateUsedPct));

    // Valid values round-trip.
    Config cfg = defaultConfig();
    cfg.profile = Profile::Saver;
    cfg.automationEnabled = true;
    cfg.startWithWindows = true;
    cfg.refreshMs = 2000;
    cfg.wsTrimAutoEnabled = true;
    cfg.excludedProcesses = {"MyApp.exe", "other.exe", "myapp.exe"};  // dup removed
    const std::string ini = configToIni(cfg);
    ConfigLoadResult round = configFromIni(ini, true);
    expect(c, round.parsedClean, "config: round-trip clean", round.error);
    expect(c, round.config.profile == Profile::Saver, "config: round-trip profile");
    expect(c, round.config.automationEnabled && round.config.startWithWindows, "config: round-trip bools");
    expect(c, round.config.refreshMs == 2000, "config: round-trip int");
    expect(c, round.config.wsTrimAutoEnabled, "config: round-trip ws policy");
    expect(c, round.config.excludedProcesses.size() == 2, "config: exclusion dedup",
           std::to_string(round.config.excludedProcesses.size()));

    // Self-protection is unconditional.
    Config bare;
    bare.protectedProcesses.clear();
    Config normalized = normalizeConfig(bare);
    bool hasSelf = false;
    for (const auto& p : normalized.protectedProcesses)
        if (p == "winresgov.exe") hasSelf = true;
    expect(c, hasSelf, "config: self always protected");

    // Profile string parsing.
    Profile p;
    expect(c, profileFromString("SAVER", &p) && p == Profile::Saver, "config: profile parse saver");
    expect(c, profileFromString(" Performance ", &p) && p == Profile::Performance, "config: profile parse perf");
    expect(c, !profileFromString("nonsense", &p), "config: profile parse rejects unknown");
}

inline void testEngineLevels(Counters& c) {
    Config cfg = testConfig();
    EngineState st;

    // Balanced thresholds: moderate 75, high 85, critical 92, cpuHigh 80.
    auto in = makeInput(Profile::Balanced, 50.0, 10.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::None, "engine: low load -> None");
    in = makeInput(Profile::Balanced, 78.0, 10.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::Moderate, "engine: 78% mem -> Moderate");
    in = makeInput(Profile::Balanced, 88.0, 10.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::High, "engine: 88% mem -> High");
    in = makeInput(Profile::Balanced, 95.0, 10.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::Critical, "engine: 95% mem -> Critical");
    in = makeInput(Profile::Balanced, 50.0, 85.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::High, "engine: cpu 85% -> at least High");
    in = makeInput(Profile::Balanced, 50.0, 95.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::Critical, "engine: cpu 95% -> Critical");

    // Profile sensitivity differs (all three profiles have distinct behavior).
    in = makeInput(Profile::Saver, 78.0, 10.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::Moderate, "engine: saver 78% -> Moderate");
    in = makeInput(Profile::Performance, 78.0, 10.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::Low,
           "engine: perf 78% -> Low only (below its 85% moderate threshold)");
    in = makeInput(Profile::Performance, 88.0, 10.0, {});
    expect(c, Engine::rawLevel(cfg, in) == PressureLevel::Moderate,
           "engine: perf 88% -> Moderate");
}

inline void testEngineHysteresis(Counters& c) {
    Config cfg = testConfig();  // escalate delay 2s, dwell 15s, hyst 5%
    EngineState st;

    // Escalation requires sustained pressure (not a one-tick spike).
    auto in = makeInput(Profile::Balanced, 88.0, 10.0, {}, true, 1000);
    auto d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::None, "engine: spike not escalated immediately (non-critical)");
    in = makeInput(Profile::Balanced, 88.0, 10.0, {}, true, 2000);
    d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::None, "engine: still waiting for escalate delay");
    in = makeInput(Profile::Balanced, 88.0, 10.0, {}, true, 3500);
    d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::High, "engine: sustained pressure escalates to High");
    expect(c, d.levelChanged, "engine: levelChanged set on escalation");

    // Spike away and back must NOT de-escalate then re-escalate instantly:
    // below high-hyst (80) but dwell (15s) not met -> stays High.
    in = makeInput(Profile::Balanced, 78.0, 10.0, {}, true, 5000);
    d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::High, "engine: dwell prevents instant de-escalation");

    // Still supported under exit threshold (88 >= 85-5) -> stays High regardless.
    in = makeInput(Profile::Balanced, 83.0, 10.0, {}, true, 30000);
    d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::High, "engine: hysteresis keeps High at 83% (>=85-5)");

    // Falls below exit threshold and dwell satisfied -> de-escalates.
    in = makeInput(Profile::Balanced, 78.0, 10.0, {}, true, 31000);
    d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::Moderate, "engine: de-escalates below exit threshold after dwell");

    // Critical escalates immediately (safety).
    EngineState st2;
    in = makeInput(Profile::Balanced, 95.0, 10.0, {}, true, 100);
    d = Engine::decide(cfg, in, st2);
    expect(c, d.level == PressureLevel::Critical, "engine: critical escalates immediately");
}

inline void testEngineActionsAndSafety(Counters& c) {
    Config cfg = testConfig();
    // Escalation/dwell delays are exercised in testEngineHysteresis; here we use
    // zero delays so a single tick reflects the steady-state level.
    cfg.escalateDelayMs = 0;
    cfg.minDwellMs = 0;
    // Eligible background renderer + protected/excluded/foreground/root/self.
    std::vector<ProcSample> procs;
    procs.push_back(makeBrowser(100, 100, "chrome.exe", 300, kPriorityNormal));          // root
    procs.push_back(makeBrowser(101, 100, "chrome.exe", 500, kPriorityNormal));          // bg renderer
    procs.push_back(makeBrowser(102, 100, "chrome.exe", 400, kPriorityNormal, true));    // foreground tree
    ProcSample prot = makeBrowser(103, 100, "chrome.exe", 200, kPriorityNormal);
    prot.isProtected = true;
    procs.push_back(prot);
    ProcSample excl = makeBrowser(104, 100, "chrome.exe", 200, kPriorityNormal);
    excl.isExcluded = true;
    procs.push_back(excl);
    ProcSample self = makeBrowser(105, 105, "winresgov.exe", 20, kPriorityNormal);
    self.isSelf = true;
    self.browserFamily = 0;
    procs.push_back(self);
    ProcSample other = makeBrowser(106, 106, "notepad.exe", 10, kPriorityNormal);
    other.browserFamily = 0;
    procs.push_back(other);
    ProcSample rt = makeBrowser(107, 100, "chrome.exe", 150, kPriorityRealtime);
    procs.push_back(rt);
    ProcSample alreadyLow = makeBrowser(108, 100, "chrome.exe", 150, kPriorityBelowNormal);
    procs.push_back(alreadyLow);
    ProcSample inaccessible = makeBrowser(109, 100, "chrome.exe", 150, kPriorityNormal);
    inaccessible.accessible = false;
    procs.push_back(inaccessible);

    EngineState st;
    // Automation OFF -> no actions even at critical pressure.
    auto in = makeInput(Profile::Saver, 95.0, 10.0, procs, false, 1000);
    auto d = Engine::decide(cfg, in, st);
    expect(c, d.actions.empty(), "engine: automation off -> no automatic actions");

    // Saver at Moderate: throttle eligible bg work; priority for normal-priority targets.
    st = EngineState();
    in = makeInput(Profile::Saver, 78.0, 10.0, procs, true, 1000);
    d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::Moderate, "engine: saver moderate level");
    bool sawThrottle101 = false, touchedUntouchable = false;
    for (const auto& a : d.actions) {
        if (a.pid == 101 && a.type == ActionType::EnablePowerThrottle) sawThrottle101 = true;
        // Untouchable: root(100), fg-tree(102), protected(103), excluded(104),
        // self(105), non-browser(106), realtime(107), inaccessible(109).
        if (a.pid == 100 || a.pid == 102 || a.pid == 103 || a.pid == 104 || a.pid == 105 ||
            a.pid == 106 || a.pid == 107 || a.pid == 109)
            touchedUntouchable = true;
    }
    expect(c, sawThrottle101, "engine: saver moderate throttles bg renderer 101");
    expect(c, !touchedUntouchable,
           "engine: safety - root/fg/protected/excluded/self/other/RT/inaccessible untouched");
    // pid 108 is already below-normal: throttle allowed, priority change refused.
    for (const auto& a : d.actions) {
        if (a.pid == 108)
            expect(c, a.type != ActionType::SetPriorityBelowNormal,
                   "engine: no priority change for already-low process");
    }

    // Saver at High also lowers priority and can trim (policy on).
    cfg.wsTrimAutoEnabled = true;
    st = EngineState();
    in = makeInput(Profile::Saver, 85.0, 10.0, procs, true, 1000);
    d = Engine::decide(cfg, in, st);
    bool sawPrio = false, sawTrim = false;
    for (const auto& a : d.actions) {
        if (a.pid == 101 && a.type == ActionType::SetPriorityBelowNormal) sawPrio = true;
        if (a.pid == 101 && a.type == ActionType::TrimWorkingSet) sawTrim = true;
    }
    expect(c, sawPrio, "engine: saver high lowers background priority");
    expect(c, sawTrim, "engine: saver high trims ws when policy enabled");
    cfg.wsTrimAutoEnabled = false;

    // WS trim policy OFF -> no automatic trims even at critical.
    st = EngineState();
    in = makeInput(Profile::Saver, 95.0, 10.0, procs, true, 1000);
    d = Engine::decide(cfg, in, st);
    bool anyTrim = false;
    for (const auto& a : d.actions)
        if (a.type == ActionType::TrimWorkingSet) anyTrim = true;
    expect(c, !anyTrim, "engine: no auto trim without policy (critical)");

    // Balanced at Moderate: throttle only (no priority lowering).
    st = EngineState();
    in = makeInput(Profile::Balanced, 78.0, 10.0, procs, true, 1000);
    d = Engine::decide(cfg, in, st);
    bool prioAtMod = false;
    for (const auto& a : d.actions)
        if (a.type == ActionType::SetPriorityBelowNormal) prioAtMod = true;
    expect(c, !prioAtMod, "engine: balanced moderate does not lower priority");
    expect(c, d.level == PressureLevel::Moderate, "engine: balanced moderate level");

    // Balanced at High: priority + throttle.
    st = EngineState();
    in = makeInput(Profile::Balanced, 88.0, 10.0, procs, true, 1000);
    d = Engine::decide(cfg, in, st);
    sawPrio = false;
    bool sawThrottle = false;
    for (const auto& a : d.actions) {
        if (a.pid == 101 && a.type == ActionType::SetPriorityBelowNormal) sawPrio = true;
        if (a.pid == 101 && a.type == ActionType::EnablePowerThrottle) sawThrottle = true;
    }
    expect(c, sawPrio && sawThrottle, "engine: balanced high lowers priority and throttles");

    // Performance profile: never lowers priority, never trims; throttle only at High.
    // Performance thresholds: moderate 85, high 92, critical 97 -> 95% is High.
    st = EngineState();
    in = makeInput(Profile::Performance, 95.0, 10.0, procs, true, 1000);
    d = Engine::decide(cfg, in, st);
    bool perfPrio = false, perfTrim = false, perfThrottle = false;
    for (const auto& a : d.actions) {
        if (a.type == ActionType::SetPriorityBelowNormal) perfPrio = true;
        if (a.type == ActionType::TrimWorkingSet) perfTrim = true;
        if (a.type == ActionType::EnablePowerThrottle) perfThrottle = true;
    }
    expect(c, !perfPrio && !perfTrim, "engine: performance never lowers priority or trims");
    expect(c, perfThrottle, "engine: performance throttles background work at high");
    // Performance at Moderate (88%): no automatic actions at all.
    st = EngineState();
    in = makeInput(Profile::Performance, 88.0, 10.0, procs, true, 1000);
    d = Engine::decide(cfg, in, st);
    expect(c, d.level == PressureLevel::Moderate, "engine: performance moderate level");
    expect(c, d.actions.empty(), "engine: performance moderate is action-free");
}

inline void testEngineCooldownsAndRestore(Counters& c) {
    Config cfg = testConfig();
    cfg.wsTrimAutoEnabled = true;
    cfg.minDwellMs = 0;
    cfg.escalateDelayMs = 0;
    std::vector<ProcSample> procs;
    procs.push_back(makeBrowser(200, 200, "brave.exe", 300, kPriorityNormal));
    procs.push_back(makeBrowser(201, 200, "brave.exe", 500, kPriorityNormal));

    EngineState st;
    auto in = makeInput(Profile::Saver, 88.0, 10.0, procs, true, 10000);
    auto d = Engine::decide(cfg, in, st);
    size_t firstCount = d.actions.size();
    expect(c, firstCount > 0, "engine: actions planned under pressure");

    // Second tick soon after: cooldowns suppress repeats.
    in = makeInput(Profile::Saver, 88.0, 10.0, procs, true, 11000);
    d = Engine::decide(cfg, in, st);
    expect(c, d.actions.empty(), "engine: cooldown suppresses repeated actions");

    // Trim cooldown is longer (120s); priority cooldown 30s.
    in = makeInput(Profile::Saver, 88.0, 10.0, procs, true, 11000 + 40000);
    d = Engine::decide(cfg, in, st);
    bool repeatTrim = false, repeatPrio = false;
    for (const auto& a : d.actions) {
        if (a.type == ActionType::TrimWorkingSet) repeatTrim = true;
        if (a.type == ActionType::SetPriorityBelowNormal) repeatPrio = true;
    }
    expect(c, repeatPrio, "engine: priority re-planned after 30s cooldown");
    expect(c, !repeatTrim, "engine: trim still cooled down at 51s (< 120s)");

    in = makeInput(Profile::Saver, 88.0, 10.0, procs, true, 10000 + 130000);
    d = Engine::decide(cfg, in, st);
    repeatTrim = false;
    for (const auto& a : d.actions)
        if (a.type == ActionType::TrimWorkingSet) repeatTrim = true;
    expect(c, repeatTrim, "engine: trim re-planned after 120s cooldown");

    // Simulate successful application, then recovery -> restore actions.
    PlanAction applied;
    applied.type = ActionType::SetPriorityBelowNormal;
    applied.pid = 201;
    applied.exeName = "brave.exe";
    Engine::markApplied(st, applied, kPriorityNormal);
    applied.type = ActionType::EnablePowerThrottle;
    Engine::markApplied(st, applied, 0);
    expect(c, st.savedPriorityClass.count(201) == 1 && st.throttledByUs[201], "engine: markApplied records state");

    // Healthy system with dwell 0 and hysteresis: raw None < Moderate.
    in = makeInput(Profile::Saver, 20.0, 5.0, procs, true, 300000);
    d = Engine::decide(cfg, in, st);
    bool restorePrio = false, restoreThrottle = false;
    for (const auto& a : d.actions) {
        if (a.type == ActionType::RestorePriority && a.pid == 201) restorePrio = true;
        if (a.type == ActionType::DisablePowerThrottle && a.pid == 201) restoreThrottle = true;
    }
    expect(c, restorePrio && restoreThrottle, "engine: recovery plans restore of priority and throttle");

    Engine::markRestored(st, 201);
    expect(c, st.savedPriorityClass.empty() && st.throttledByUs.empty() && st.appliedExe.empty(),
           "engine: markRestored clears bookkeeping");

    auto restores = Engine::planRestoreAll(st);
    expect(c, restores.empty(), "engine: planRestoreAll empty when nothing applied");
}

inline void testManualAndLimits(Counters& c) {
    Config cfg = testConfig();
    cfg.wsTrimManualAllowed = true;
    std::vector<ProcSample> procs;
    for (uint32_t i = 0; i < 8; ++i)
        procs.push_back(makeBrowser(300 + i, 300, "msedge.exe", 100 + i * 10, kPriorityNormal));
    EngineState st;
    auto in = makeInput(Profile::Balanced, 40.0, 5.0, procs, false, 1000);
    auto actions = Engine::planManual(cfg, in, st);
    int trims = 0, prios = 0;
    for (const auto& a : actions) {
        if (a.type == ActionType::TrimWorkingSet) ++trims;
        if (a.type == ActionType::SetPriorityBelowNormal) ++prios;
    }
    expect(c, trims == 3, "engine: manual trim limited to 3 per pass", std::to_string(trims));
    expect(c, prios == 7, "engine: manual lowers all eligible (root excluded)", std::to_string(prios));

    // Manual trims blocked when policy disallows.
    cfg.wsTrimManualAllowed = false;
    EngineState st2;
    actions = Engine::planManual(cfg, in, st2);
    trims = 0;
    for (const auto& a : actions)
        if (a.type == ActionType::TrimWorkingSet) ++trims;
    expect(c, trims == 0, "engine: manual trim respects policy off");

    // Cooldowns also throttle repeated manual passes.
    cfg.wsTrimManualAllowed = true;
    EngineState st3;
    (void)Engine::planManual(cfg, in, st3);
    actions = Engine::planManual(cfg, in, st3);
    expect(c, actions.empty(), "engine: repeated manual pass within cooldown is a no-op");
}

inline void testCsv(Counters& c) {
    expect(c, csvEscape("plain") == "plain", "csv: no quotes for plain");
    expect(c, csvEscape("a,b") == "\"a,b\"", "csv: quotes for comma");
    expect(c, csvEscape("say \"hi\"") == "\"say \"\"hi\"\"\"", "csv: doubles quotes");
    expect(c, csvRow({"a", "b,c", "d"}) == "a,\"b,c\",d", "csv: row join");
}

inline int runCoreTests(std::ostream& out) {
    Counters c;
    out << "=== Windows Resource Governor core tests (v" << WRG_VERSION_PREFIX_FREE << ") ===\n";
    testIni(c);
    testStats(c);
    testConfig(c);
    testEngineLevels(c);
    testEngineHysteresis(c);
    testEngineActionsAndSafety(c);
    testEngineCooldownsAndRestore(c);
    testManualAndLimits(c);
    testCsv(c);
    out << "=== " << c.checks << " checks, " << c.failures << " failures ===\n";
    return c.failures;
}

}  // namespace testing
}  // namespace wrg
