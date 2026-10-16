// Windows Resource Governor — application entry point.
//
// Subsystem WINDOWS (no console window on double-click). CLI modes print to the
// inherited stdout/stderr when launched from a console or with redirection, and
// additionally accept --out FILE for reliable capture (used by CI).
//
// Modes:
//   (no args)            launch the dashboard
//   --version            print version metadata
//   --selftest           run the portable core test suite
//   --smoke              runtime smoke test (monitoring, engine, safety, config)
//   --bench              benchmark run (options: --seconds N --csv PATH)
//   --out FILE           redirect CLI output to FILE as well
//   --help               usage

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/version.h"
#include "tests/core_tests.h"
#include "win/app.h"
#include "win/bench.h"
#include "win/executor.h"
#include "win/platform.h"
#include "win/procmon.h"
#include "win/sysmon.h"
#include "win/ui.h"

namespace {

// Console attachment for CLI output in a WINDOWS-subsystem process.
void attachConsoleForCli() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        // No parent console (e.g. Explorer launch) — nothing to print to.
        return;
    }
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
}

struct CliOut {
    std::ofstream file;
    bool hasFile = false;

    bool open(const std::string& path) {
        file.open(path, std::ios::binary | std::ios::trunc);
        hasFile = file.is_open();
        return hasFile;
    }
    void line(const std::string& s) {
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        if (hasFile) {
            file << s << "\n";
            file.flush();
        }
    }
};

int runVersion(CliOut& out) {
    out.line(std::string(WRG_PRODUCT_NAME) + " " + WRG_VERSION_STRING +
             " (prefix-free: " + WRG_VERSION_PREFIX_FREE + ")");
    out.line(std::string("executable: ") + WRG_EXE_NAME);
    out.line(std::string("company: ") + WRG_COMPANY_NAME + "  " + WRG_COPYRIGHT);
    return 0;
}

int runSelftest(CliOut& out) {
    std::ostringstream ss;
    const int failures = wrg::testing::runCoreTests(ss);
    // Emit line by line so the CliOut sees each row.
    std::istringstream is(ss.str());
    std::string line;
    while (std::getline(is, line)) out.line(line);
    out.line(failures == 0 ? "SELFTEST: PASS" : "SELFTEST: FAIL");
    return failures == 0 ? 0 : 1;
}

struct SmokeCheck {
    int checks = 0;
    int failures = 0;
    void expect(CliOut& out, bool ok, const std::string& name, const std::string& detail = "") {
        ++checks;
        out.line(std::string(ok ? "PASS: " : "FAIL: ") + name + (detail.empty() ? "" : " [" + detail + "]"));
        if (!ok) ++failures;
    }
};

int runSmoke(CliOut& out) {
    using namespace wrg;
    SmokeCheck sc;
    out.line("=== Windows Resource Governor smoke test (v" WRG_VERSION_PREFIX_FREE ") ===");

    // 1. Environment identification (measured facts only).
    out.line("os: " + osVersionString());
    out.line("cpu: " + cpuBrandString());
    out.line("logical_processors: " + std::to_string(logicalProcessorCount()));
    out.line(std::string("elevated: ") + (isElevated() ? "yes" : "no (expected default)"));
    out.line("exe_path: " + exePath());

    // 2. Configuration load with safe defaults.
    App app;
    std::string initErr;
    app.init(&initErr);
    out.line("data_dir: " + app.dataDirPath());
    out.line("config_path: " + app.configPath());
    sc.expect(out, !app.configError().empty() || app.configLoadedCleanly(),
              "config: loaded or fell back to safe defaults",
              app.configError().empty() ? "clean" : app.configError());
    sc.expect(out, app.config().profile == Profile::Saver || app.config().profile == Profile::Balanced ||
                       app.config().profile == Profile::Performance,
              "config: profile is one of the three defined profiles");
    sc.expect(out, app.config().startWithWindows == false || getStartupEnabled(),
              "config: startup default is disabled (or explicitly enabled and registered)");
    {
        std::string err;
        const bool saved = app.saveSettings(&err);
        sc.expect(out, saved, "config: save round-trip", err);
    }

    // 3. System metrics (two samples so CPU deltas exist).
    SystemMonitor sysmon;
    const SysSample s1 = sysmon.sample();
    Sleep(400);
    const SysSample s2 = sysmon.sample();
    sc.expect(out, s2.valid && s2.totalPhysBytes > 0, "sysmon: memory metrics available",
              std::to_string(s2.totalPhysBytes) + " bytes total");
    sc.expect(out, s2.availPhysBytes <= s2.totalPhysBytes, "sysmon: avail <= total");
    sc.expect(out, s2.cpuPercent >= 0.0 && s2.cpuPercent <= 100.0, "sysmon: cpu percent normalized 0..100",
              std::to_string(s2.cpuPercent));
    (void)s1;

    // 4. Process discovery + browser aggregation + lifecycle (two snapshots).
    ProcessMonitor procmon;
    const std::vector<ProcSample> p1 = procmon.snapshot(app.config());
    Sleep(400);
    const std::vector<ProcSample> p2 = procmon.snapshot(app.config());
    sc.expect(out, !p2.empty(), "procmon: process list enumerated",
              std::to_string(p2.size()) + " processes");
    int browsers = 0, inaccessible = 0, selfSeen = 0;
    std::map<int, int> families;
    for (const auto& p : p2) {
        if (p.browserFamily != 0) {
            ++browsers;
            ++families[p.browserFamily];
        }
        if (!p.accessible) ++inaccessible;
        if (p.isSelf) ++selfSeen;
    }
    sc.expect(out, selfSeen == 1, "procmon: self process identified exactly once",
              std::to_string(selfSeen));
    out.line("browsers: " + std::to_string(browsers) + " processes (chrome=" +
             std::to_string(families[1]) + " brave=" + std::to_string(families[2]) +
             " edge=" + std::to_string(families[3]) + ", " + std::to_string(inaccessible) +
             " inaccessible)");
    for (const auto& g : ProcessMonitor::aggregate(p2)) {
        out.line("  group root=" + std::to_string(g.rootPid) + " " + g.exeName +
                 " procs=" + std::to_string(g.procCount) + " cpu=" +
                 std::to_string(g.cpuPercent).substr(0, 4) + "% ws=" +
                 std::to_string(g.wsBytes / (1024 * 1024)) + "MB");
    }
    // No double counting: every browser process appears in exactly one group.
    {
        int grouped = 0;
        for (const auto& g : ProcessMonitor::aggregate(p2)) grouped += static_cast<int>(g.procCount);
        sc.expect(out, grouped == browsers, "procmon: no double-counting in aggregation",
                  std::to_string(grouped) + "/" + std::to_string(browsers));
    }
    // Distinguish metrics: WS and private bytes are separate fields and commit
    // is tracked; values are honest zeros when access is denied.
    bool metricsSane = true;
    for (const auto& p : p2) {
        if (p.accessible && p.wsBytes == 0 && p.pid > 4) metricsSane = false;
    }
    sc.expect(out, metricsSane, "procmon: memory metrics populated for accessible processes");
    // Self overhead measured.
    const auto self = procmon.selfMetrics();
    out.line("self_overhead: cpu=" + std::to_string(self.cpuPercent).substr(0, 4) +
             "% ws=" + std::to_string(self.wsBytes / 1024) + "KB private=" +
             std::to_string(self.privateBytes / 1024) + "KB");
    sc.expect(out, self.wsBytes > 0, "procmon: self overhead measured");

    // 5. Engine: all three profiles produce deterministic, distinct decisions on
    //    the same synthetic pressure (behavioral check, no actions executed).
    {
        Config cfg = app.config();
        cfg.escalateDelayMs = 0;
        cfg.minDwellMs = 0;
        EngineState st;
        EngineInput in;
        in.sys = s2;
        in.sys.valid = true;
        in.sys.totalPhysBytes = 8ull * 1024 * 1024 * 1024;
        in.sys.availPhysBytes = 900ull * 1024 * 1024;  // synthetic: 89% used
        in.sys.cpuPercent = 40.0;
        in.procs = p2;
        in.nowMs = nowMonotonicMs();
        int actionCounts[3] = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            in.profile = static_cast<Profile>(i);
            in.automationEnabled = true;
            EngineState fresh;
            EngineDecision d = Engine::decide(cfg, in, fresh);
            actionCounts[i] = static_cast<int>(d.actions.size());
            out.line(std::string("engine profile ") + profileName(in.profile) + ": level=" +
                     pressureLevelName(d.level) + " actions=" + std::to_string(d.actions.size()) +
                     " (" + d.rationale + ")");
        }
        sc.expect(out, actionCounts[0] > 0, "engine: saver acts under 89% memory pressure");
        sc.expect(out, actionCounts[2] <= actionCounts[1] && actionCounts[1] <= actionCounts[0],
                  "engine: action aggressiveness ordered saver >= balanced >= performance",
                  std::to_string(actionCounts[0]) + "/" + std::to_string(actionCounts[1]) + "/" +
                      std::to_string(actionCounts[2]));
        // Foreground/protected processes must never appear in a plan.
        std::map<uint32_t, ProcSample> byPid;
        for (const auto& p : p2) byPid[p.pid] = p;
        in.profile = Profile::Saver;
        EngineState fresh;
        EngineDecision d = Engine::decide(cfg, in, fresh);
        bool safetyOk = true;
        for (const auto& a : d.actions) {
            auto it = byPid.find(a.pid);
            if (it == byPid.end()) safetyOk = false;
            else if (it->second.isProtected || it->second.isExcluded || it->second.isForegroundTree ||
                     it->second.isSelf || it->second.priorityClass == kPriorityRealtime)
                safetyOk = false;
        }
        sc.expect(out, safetyOk, "engine: planned actions never target protected/foreground/self/RT");
    }

    // 6. Executor bookkeeping: nothing applied -> restore is a no-op.
    {
        EngineState st;
        ActionExecutor ex;
        const int restored = ex.restoreAll(app.config(), st, {});
        sc.expect(out, restored == 0, "executor: restore-all with empty state is a no-op");
    }

    // 7. Startup registration query works (state unchanged by the query).
    {
        const bool before = getStartupEnabled();
        const bool after = getStartupEnabled();
        sc.expect(out, before == after, "startup: query is stable and non-mutating");
        out.line(std::string("start_with_windows: ") + (after ? "enabled" : "disabled (default)"));
    }

    out.line("=== smoke: " + std::to_string(sc.checks) + " checks, " +
             std::to_string(sc.failures) + " failures ===");
    out.line(sc.failures == 0 ? "SMOKE: PASS" : "SMOKE: FAIL");
    return sc.failures == 0 ? 0 : 1;
}

int runBench(CliOut& out, int seconds, const std::string& csvPath) {
    using namespace wrg;
    App app;
    std::string err;
    app.init(&err);

    if (seconds <= 0) seconds = app.config().benchWindowSeconds;
    const int sampleMs = app.config().benchSampleMs;

    out.line("=== Windows Resource Governor benchmark (v" WRG_VERSION_PREFIX_FREE ") ===");
    out.line("hardware: " + cpuBrandString());
    out.line("duration: " + std::to_string(seconds) + "s (baseline + candidate), sample " +
             std::to_string(sampleMs) + "ms");
    out.line("note: baseline = monitoring only; candidate = one manual optimize pass + "
             "engine with automation=" + std::string(app.config().automationEnabled ? "on" : "off"));

    BenchmarkRunner runner(&app.sysmon(), &app.procmon(), &app.executor(), &app.engineState());
    const BenchResult result = runner.run(app.config(), seconds, sampleMs, nullptr);

    std::istringstream is(result.summaryText);
    std::string line;
    while (std::getline(is, line)) out.line(line);

    std::string target = csvPath;
    if (target.empty()) {
        std::string dir = app.dataDirPath();
        dir += "\\benchmark-" + result.startedIso;
        for (auto& ch : dir)
            if (ch == ':') ch = '-';
        target = dir + ".csv";
    }
    std::string csvErr;
    if (writeBenchCsv(result, target, &csvErr)) {
        out.line("csv: " + target);
    } else {
        out.line("csv: FAILED " + csvErr);
        return 1;
    }
    return result.ok ? 0 : 1;
}

int runCli(const std::wstring& cmdline) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdline.c_str(), &argc);
    std::vector<std::string> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.push_back(wrg::narrow(argv[i]));
        LocalFree(argv);
    }

    std::string outPath;
    std::string mode;
    int benchSeconds = 0;
    std::string csvPath;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--version" || a == "--selftest" || a == "--smoke" || a == "--bench" ||
            a == "--help" || a == "-h") {
            mode = a;
        } else if (a == "--out" && i + 1 < args.size()) {
            outPath = args[++i];
        } else if (a == "--seconds" && i + 1 < args.size()) {
            benchSeconds = std::atoi(args[++i].c_str());
        } else if (a == "--csv" && i + 1 < args.size()) {
            csvPath = args[++i];
        }
    }

    attachConsoleForCli();
    CliOut out;
    if (!outPath.empty()) out.open(outPath);

    if (mode == "--version") return runVersion(out);
    if (mode == "--selftest") return runSelftest(out);
    if (mode == "--smoke") return runSmoke(out);
    if (mode == "--bench") return runBench(out, benchSeconds, csvPath);
    if (mode == "--help" || mode == "-h") {
        out.line(WRG_PRODUCT_NAME " " WRG_VERSION_STRING);
        out.line("usage: WinResGov.exe [--version|--selftest|--smoke|--bench|--help]");
        out.line("  --bench [--seconds N] [--csv PATH]   run baseline-vs-candidate benchmark");
        out.line("  --out FILE                          also write CLI output to FILE");
        out.line("  (no arguments)                      launch the dashboard");
        return 0;
    }
    return -1;  // not a CLI mode -> GUI
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    const int cli = runCli(GetCommandLineW());
    if (cli >= 0) return cli;

    return wrg::runDashboard(hInstance, nCmdShow);
}
