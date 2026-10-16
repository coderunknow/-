// Reproducible baseline-vs-candidate benchmark with CSV export (brief F).
//
// Phases: "baseline" measures monitoring-only overhead; "candidate" applies one
// manual optimization pass at the start and then runs the adaptive engine with
// the user's automation setting. Every value written is MEASURED on the machine
// that runs the benchmark; the hardware string is recorded in the CSV header.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/engine.h"
#include "core/types.h"

namespace wrg {

class SystemMonitor;
class ProcessMonitor;
class ActionExecutor;

struct BenchSample {
    std::string iso;
    std::string phase;  // "baseline" | "candidate"
    int idx = 0;
    double sysCpuPct = 0.0;
    double sysUsedMemPct = 0.0;
    uint64_t sysAvailPhys = 0;
    double selfCpuPct = 0.0;
    uint64_t selfWs = 0;
    int browserProcs = 0;
    double browserCpuPct = 0.0;
    uint64_t browserWs = 0;
    uint64_t browserPrivate = 0;
    int actionsInWindow = 0;
    int errorsInWindow = 0;
    std::string note;
};

struct PhaseStats {
    int samples = 0;
    double sysCpuAvg = 0.0, sysCpuPeak = 0.0;
    double selfCpuAvg = 0.0, selfCpuPeak = 0.0;
    uint64_t selfWsAvg = 0, selfWsPeak = 0;
    double usedMemAvg = 0.0, usedMemPeak = 0.0;
    uint64_t browserWsAvg = 0, browserWsPeak = 0;
    double browserCpuAvg = 0.0, browserCpuPeak = 0.0;
    int actions = 0;
    int errors = 0;
};

struct BenchResult {
    bool ok = false;
    bool cancelled = false;
    std::string startedIso, finishedIso;
    std::string hardware;
    unsigned logicalProcessors = 0;
    int baselineSeconds = 0, candidateSeconds = 0, sampleMs = 0;
    PhaseStats baseline, candidate;
    int actionsPerformed = 0, errorsTotal = 0;
    int trimsPerformed = 0, restoresPerformed = 0;
    std::string csvPath;
    std::string summaryText;
    std::vector<BenchSample> samples;
};

// Writes the benchmark CSV (metadata comment lines + header + sample rows).
bool writeBenchCsv(const BenchResult& r, const std::string& path, std::string* error);

// Formats a human-readable summary (also used by the CLI and the dashboard).
std::string formatBenchSummary(const BenchResult& r);

// Live benchmark runner: samples the system, applies the candidate treatment,
// and collects measurements. Cancellation via the atomic flag (best-effort).
class BenchmarkRunner {
public:
    BenchmarkRunner(SystemMonitor* sys, ProcessMonitor* proc, ActionExecutor* exec,
                    EngineState* state)
        : sys_(sys), proc_(proc), exec_(exec), state_(state) {}

    // totalSeconds is split evenly into baseline and candidate phases.
    BenchResult run(const Config& cfg, int totalSeconds, int sampleMs,
                    std::atomic<bool>* cancel);

private:
    SystemMonitor* sys_;
    ProcessMonitor* proc_;
    ActionExecutor* exec_;
    EngineState* state_;
};

}  // namespace wrg
