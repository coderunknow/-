#include "win/bench.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <windows.h>

#include "core/csv.h"
#include "core/stats.h"
#include "core/version.h"
#include "win/executor.h"
#include "win/platform.h"
#include "win/procmon.h"
#include "win/sysmon.h"

namespace wrg {

namespace {

void accumulate(PhaseStats& s, const BenchSample& b) {
    ++s.samples;
    s.sysCpuAvg += b.sysCpuPct;
    if (b.sysCpuPct > s.sysCpuPeak) s.sysCpuPeak = b.sysCpuPct;
    s.selfCpuAvg += b.selfCpuPct;
    if (b.selfCpuPct > s.selfCpuPeak) s.selfCpuPeak = b.selfCpuPct;
    s.selfWsAvg += b.selfWs;
    if (b.selfWs > s.selfWsPeak) s.selfWsPeak = b.selfWs;
    s.usedMemAvg += b.sysUsedMemPct;
    if (b.sysUsedMemPct > s.usedMemPeak) s.usedMemPeak = b.sysUsedMemPct;
    s.browserWsAvg += b.browserWs;
    if (b.browserWs > s.browserWsPeak) s.browserWsPeak = b.browserWs;
    s.browserCpuAvg += b.browserCpuPct;
    if (b.browserCpuPct > s.browserCpuPeak) s.browserCpuPeak = b.browserCpuPct;
    s.actions += b.actionsInWindow;
    s.errors += b.errorsInWindow;
}

void finalize(PhaseStats& s) {
    if (s.samples > 0) {
        s.sysCpuAvg /= s.samples;
        s.selfCpuAvg /= s.samples;
        s.usedMemAvg /= s.samples;
        s.browserCpuAvg /= s.samples;
        s.selfWsAvg /= static_cast<uint64_t>(s.samples);
        s.browserWsAvg /= static_cast<uint64_t>(s.samples);
    }
}

std::string f1(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

}  // namespace

BenchResult BenchmarkRunner::run(const Config& cfg, int totalSeconds, int sampleMs,
                                 std::atomic<bool>* cancel) {
    BenchResult r;
    r.startedIso = nowIso8601();
    r.hardware = cpuBrandString();
    r.logicalProcessors = logicalProcessorCount();
    r.sampleMs = sampleMs;
    if (totalSeconds < 2) totalSeconds = 2;
    r.baselineSeconds = totalSeconds / 2;
    r.candidateSeconds = totalSeconds - r.baselineSeconds;

    const uint64_t startMs = nowMonotonicMs();
    const uint64_t baselineEndMs = startMs + static_cast<uint64_t>(r.baselineSeconds) * 1000ull;
    const uint64_t endMs = startMs + static_cast<uint64_t>(totalSeconds) * 1000ull;

    int idx = 0;
    int actionsTotal = 0, errorsTotal = 0, trims = 0, restores = 0;
    bool candidateStarted = false;

    // Pre-sample so CPU deltas are valid on the first recorded row.
    sys_->sample();
    proc_->snapshot(cfg);

    while (true) {
        if (cancel && cancel->load()) {
            r.cancelled = true;
            break;
        }
        const uint64_t now = nowMonotonicMs();
        if (now >= endMs) break;

        const bool candidatePhase = now >= baselineEndMs;
        BenchSample b;
        b.iso = nowIso8601();
        b.phase = candidatePhase ? "candidate" : "baseline";
        b.idx = idx++;

        const int errorsBefore = proc_->lastErrors();
        (void)errorsBefore;
        const SysSample sys = sys_->sample();
        const std::vector<ProcSample> procs = proc_->snapshot(cfg);
        const ProcessMonitor::SelfMetrics self = proc_->selfMetrics();

        b.sysCpuPct = sys.cpuPercent;
        b.sysUsedMemPct = usedMemPercent(sys.totalPhysBytes, sys.availPhysBytes);
        b.sysAvailPhys = sys.availPhysBytes;
        b.selfCpuPct = self.cpuPercent;
        b.selfWs = self.wsBytes;
        b.browserProcs = 0;
        for (const auto& p : procs) {
            if (p.browserFamily == 0) continue;
            ++b.browserProcs;
            b.browserCpuPct += p.cpuPercent;
            b.browserWs += p.wsBytes;
            b.browserPrivate += p.privateBytes;
        }

        const size_t historyBefore = exec_->history().size();
        if (candidatePhase) {
            std::map<uint32_t, ProcSample> byPid;
            for (const auto& p : procs) byPid[p.pid] = p;

            if (!candidateStarted) {
                candidateStarted = true;
                // Candidate treatment: one manual optimization pass, bounded by
                // the same safety rules and cooldowns as any other pass.
                EngineInput in;
                in.sys = sys;
                in.procs = procs;
                in.profile = cfg.profile;
                in.automationEnabled = cfg.automationEnabled;
                in.nowMs = nowMonotonicMs();
                const auto plan = Engine::planManual(cfg, in, *state_);
                const int ok = exec_->executeAll(cfg, plan, byPid, *state_);
                actionsTotal += static_cast<int>(plan.size());
                b.note = "manual optimize pass: " + std::to_string(ok) + "/" +
                         std::to_string(plan.size()) + " actions ok";
            } else if (cfg.automationEnabled) {
                EngineInput in;
                in.sys = sys;
                in.procs = procs;
                in.profile = cfg.profile;
                in.automationEnabled = true;
                in.nowMs = nowMonotonicMs();
                EngineDecision d = Engine::decide(cfg, in, *state_);
                const int ok = exec_->executeAll(cfg, d.actions, byPid, *state_);
                actionsTotal += static_cast<int>(d.actions.size());
                if (!d.actions.empty())
                    b.note = "engine: " + d.rationale;
                (void)ok;
            }
        }

        const size_t historyAfter = exec_->history().size();
        b.actionsInWindow = static_cast<int>(historyAfter - historyBefore);
        {
            const auto& h = exec_->history();
            int fail = 0;
            for (size_t i = historyBefore; i < historyAfter && i < h.size(); ++i)
                if (!h[i].ok) ++fail;
            b.errorsInWindow = fail;
            errorsTotal += fail;
        }

        if (candidatePhase)
            accumulate(r.candidate, b);
        else
            accumulate(r.baseline, b);
        r.samples.push_back(b);

        // Sleep until the next sample boundary (bounded wait for cancellation).
        const uint64_t next = startMs + static_cast<uint64_t>(idx) * static_cast<uint64_t>(sampleMs);
        uint64_t t = nowMonotonicMs();
        while (t < next && t < endMs) {
            if (cancel && cancel->load()) break;
            const uint64_t remain = next - t;
            Sleep(remain > 100 ? 50 : static_cast<DWORD>(remain > 5 ? remain / 2 : 1));
            t = nowMonotonicMs();
        }
    }

    // Candidate treatment always ends with restoration (recorded, bounded).
    {
        std::map<uint32_t, ProcSample> byPid;
        for (const auto& p : proc_->snapshot(cfg)) byPid[p.pid] = p;
        restores = exec_->restoreAll(cfg, *state_, byPid);
        for (const auto& h : exec_->history())
            if (h.type == ActionType::TrimWorkingSet && h.ok) ++trims;
        r.trimsPerformed = trims;
        r.restoresPerformed = restores;
    }

    finalize(r.baseline);
    finalize(r.candidate);
    r.actionsPerformed = actionsTotal;
    r.errorsTotal = errorsTotal;
    r.finishedIso = nowIso8601();
    r.ok = !r.cancelled && !r.samples.empty();
    r.summaryText = formatBenchSummary(r);
    return r;
}

bool writeBenchCsv(const BenchResult& r, const std::string& path, std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot open CSV for writing: " + path;
        return false;
    }
    out << "# Windows Resource Governor benchmark\n";
    out << "# version=" << WRG_VERSION_PREFIX_FREE << "\n";
    out << "# started=" << r.startedIso << " finished=" << r.finishedIso << "\n";
    out << "# hardware=" << r.hardware << "\n";
    out << "# logical_processors=" << r.logicalProcessors << "\n";
    out << "# baseline_seconds=" << r.baselineSeconds << " candidate_seconds=" << r.candidateSeconds
        << " sample_ms=" << r.sampleMs << "\n";
    out << "# labels: all values are MEASURED on the machine above; browser_* columns are "
           "process-level aggregates (no per-tab data exists); cpu_pct is normalized "
           "(100=all logical processors busy)\n";
    out << csvRow({"ts_iso", "phase", "sample_idx", "sys_cpu_pct", "sys_used_mem_pct",
                   "sys_avail_phys_bytes", "self_cpu_pct", "self_ws_bytes", "browser_proc_count",
                   "browser_cpu_pct", "browser_ws_bytes", "browser_private_bytes",
                   "actions_in_window", "errors_in_window", "note"})
        << "\n";
    for (const auto& s : r.samples) {
        out << csvRow({s.iso, s.phase, std::to_string(s.idx), f1(s.sysCpuPct),
                       f1(s.sysUsedMemPct), std::to_string(s.sysAvailPhys), f1(s.selfCpuPct),
                       std::to_string(s.selfWs), std::to_string(s.browserProcs),
                       f1(s.browserCpuPct), std::to_string(s.browserWs),
                       std::to_string(s.browserPrivate), std::to_string(s.actionsInWindow),
                       std::to_string(s.errorsInWindow), s.note})
            << "\n";
    }
    if (!out.good()) {
        if (error) *error = "CSV write failed: " + path;
        return false;
    }
    return true;
}

std::string formatBenchSummary(const BenchResult& r) {
    std::ostringstream o;
    o << "Windows Resource Governor benchmark (v" << WRG_VERSION_PREFIX_FREE << ")\n";
    o << "hardware: " << r.hardware << " (" << r.logicalProcessors << " logical processors)\n";
    o << "started: " << r.startedIso << "   finished: " << r.finishedIso << "\n";
    o << "duration: baseline " << r.baselineSeconds << "s + candidate " << r.candidateSeconds
      << "s, sample interval " << r.sampleMs << " ms\n";
    o << "actions performed: " << r.actionsPerformed << "   failures: " << r.errorsTotal
      << "   restores at end: " << r.restoresPerformed << "\n";
    auto line = [&](const char* name, const PhaseStats& s) {
        o << "  " << name << " (" << s.samples << " samples): "
          << "sysCpu avg " << f1(s.sysCpuAvg) << "% peak " << f1(s.sysCpuPeak) << "%; "
          << "selfCpu avg " << f1(s.selfCpuAvg) << "% peak " << f1(s.selfCpuPeak) << "%; "
          << "selfWS avg " << formatBytes(s.selfWsAvg) << " peak " << formatBytes(s.selfWsPeak)
          << "; usedMem avg " << f1(s.usedMemAvg) << "% peak " << f1(s.usedMemPeak) << "%; "
          << "browserWS avg " << formatBytes(s.browserWsAvg) << " peak "
          << formatBytes(s.browserWsPeak) << "; browserCpu avg " << f1(s.browserCpuAvg)
          << "% peak " << f1(s.browserCpuPeak) << "%; actions " << s.actions << ", errors "
          << s.errors << "\n";
    };
    line("baseline (monitoring only)", r.baseline);
    line("candidate (optimizer active)", r.candidate);
    o << "labels: every number above is measured on the recorded hardware; results from "
         "other machines (including CI runners) are NOT reference-hardware results.\n";
    if (r.cancelled) o << "note: benchmark was cancelled before completion.\n";
    return o.str();
}

}  // namespace wrg
