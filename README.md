# Windows Resource Governor v0.1.0-dev1

Native Windows resource optimizer for browser workloads (Brave, Google Chrome,
Microsoft Edge) on Windows 10 x64. Pure Win32 C++17 — no browser extension, no
browser bridge, no cloud services, no runtime dependencies beyond Windows itself.

- **Version:** v0.1.0-dev1 (prefix-free form `0.1.0-dev1`)
- **Target platform:** Windows 10 x64 (x64 build; Windows 11 compatible)
- **Reference hardware:** Intel Core i5-7300U, 4 logical threads, 8 GB RAM
- **License:** MIT
- **Privileges:** runs as a standard user (asInvoker). Elevation is never
  requested. All operations work on same-user processes; processes owned by
  other users/elevated processes are skipped and recorded as inaccessible.

---

## 1. What it does

Windows Resource Governor watches real system and browser-process resource
usage and applies **bounded, reversible, observable** optimizations when real
resource pressure justifies them:

| Area | Mechanism (documented Win32 APIs) | Reversible |
|------|-----------------------------------|------------|
| CPU | `SetPriorityClass` (below-normal for *background browser work only*) | yes — original class saved and restored |
| Power | `SetProcessInformation` + `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` (EcoQoS, Windows 10 1709+) | yes — throttle removed on restore |
| RAM | `SetProcessWorkingSetSize` working-set trim (temporary; policy-gated) | n/a — memory regrows on access; documented as temporary |

Monitoring uses `GlobalMemoryStatusEx`, `GetSystemTimes`, `CreateToolhelp32Snapshot`,
`OpenProcess`, `GetProcessTimes`, `GetProcessMemoryInfo`, `QueryWorkingSet`,
`QueryFullProcessImageNameW`, `GetForegroundWindow` — all documented APIs.

**What it never does** (enforced in code): terminate browser processes, inject
code, use undocumented hooks, install drivers, disable security products or
services, touch firewall/hosts, modify browser profiles or user data, assign
real-time priority, or hide failures.

## 2. Native-only limitations (honest status)

The user selected a **native-only** architecture (no browser extension/bridge)
for this version. Two requested capabilities are therefore **NOT DONE /
BLOCKED** — with no fake UI controls shipped:

1. **Per-tab management (BLOCKED).** Ordinary process APIs cannot reliably map
   tabs to processes (Chromium process-per-site-instance is non-deterministic)
   and cannot see tab audio state, unsaved form data, or discard eligibility.
   Real *process-level* browser optimization is implemented instead. Minimum
   future architecture: an optional browser extension exposing tab metadata and
   `tabs.discard`, talking to the app over a local IPC channel with user opt-in.
2. **Network request filtering (BLOCKED).** Selectively blocking tracking /
   telemetry / redundant HTTPS requests from a separate process requires an
   extension (`declarativeNetRequest`), a MITM proxy, or a WFP driver — all
   unapproved, invasive, or privilege-heavy. Deferred; no fake switch exists.
   Minimum future architecture: opt-in extension with `declarativeNetRequest`.

## 3. Feature overview

### 3.1 Monitoring
- System: physical memory total/available, commit charge, memory load %,
  normalized CPU utilization (100% = all logical processors busy) from
  `GetSystemTimes` deltas.
- Per browser process: PID, parent PID, executable name/path, working set,
  **private working set** (via `QueryWorkingSet`, marked `unavailable`/`partial`
  when access is denied or the WS exceeds the measurement cap), private bytes
  (`PrivateUsage`), committed bytes (`PagefileUsage`), CPU time and normalized
  CPU %, priority class, accessibility.
- Browser tree aggregation: processes are grouped by family (Chrome/Brave/Edge)
  under their same-family tree root; every process is counted exactly once.
- Lifecycle: snapshots re-enumerate each tick; stale CPU-delta state is keyed by
  process creation time (PID-reuse safe) and pruned when processes exit.
- Self overhead: the governor's own CPU % and working set are measured and
  displayed.

### 3.2 Adaptive engine — three profiles
Pressure levels: `None / Low / Moderate / High / Critical`, computed from used
physical memory % and system CPU % against **per-profile thresholds**
(configurable in `config.ini`):

| Profile | mem Moderate | mem High | mem Critical | cpu High | Policy |
|---------|-------------|----------|--------------|----------|--------|
| **Saver** | 70% | 80% | 90% | 70% | throttle at Moderate+, lower priority at High+, trim at High+ (only with policy on) |
| **Balanced** (default) | 75% | 85% | 92% | 80% | throttle at Moderate+, lower priority at High+, trim at High+ (only with policy on) |
| **Performance** | 85% | 92% | 97% | 90% | throttle only at High+; never lowers priority, never trims |

Anti-oscillation: escalation must be sustained (`escalate_delay_ms`, default
2 s; Critical escalates immediately), de-escalation needs `hysteresis_pct`
(default 5) below the current level's entry threshold **and** `min_dwell_ms`
(default 15 s). Every action type has a per-PID cooldown (30–120 s).

Automation is **user-controlled and disabled by default** (`automation_enabled=0`).
"Optimize now" runs one manual pass on demand. Restoration of previously applied
changes runs automatically when pressure recovers even if automation is off
(undoing our own changes is a safety obligation).

### 3.3 Working-set policy (aggressive within safety boundaries)
Automatic trimming requires **all** of: the `ws_trim.enabled=1` policy gate
(default `0`), Saver/Balanced profile at High+ *memory* pressure, an eligible
target, and the per-PID trim cooldown (default 120 s). At most 3 trim
*candidates* per tick (biggest working sets first) — the governor never trims
every browser process on a timer. Foreground, protected, excluded, root and
self processes are never trimmed. Trims are **temporary**: pages are faulted
back when the process uses them again; before/after working-set values are
recorded in the action log.

### 3.4 Safety model
Hard-coded and configurable protections (dashboard shows both lists):
- **Never modified:** the governor itself, the foreground process, the
  foreground process's ancestors, the entire browser tree containing the
  foreground process, real-time-class processes, and the browser root (UI)
  process when `keep_browser_root_normal=1` (default).
- **Protected list** (default, editable): `csrss.exe`, `lsass.exe`, `services.exe`,
  `smss.exe`, `wininit.exe`, `winlogon.exe`, `svchost.exe`, `dwm.exe`,
  `explorer.exe`, `MsMpEng.exe`, … plus `winresgov.exe` (unconditional).
- **Exclusion list** (user-editable): any executable name listed here is never
  modified.
- All actions are logged (what/why/result/Win32 error) and capped in memory;
  "Restore all" (and exit) reverts priority and throttle changes.

### 3.5 Configuration
`%LOCALAPPDATA%\WinResGov\config.ini` (created on first run with documented
defaults; the app also works with no config at all). Every value is validated
and clamped on load; a corrupted file yields safe defaults plus a recorded
error — the file is never silently destroyed. Sections cover profiles and
thresholds, hysteresis, cooldowns, WS-trim policy gates, CPU-opt gates,
protection/exclusions, benchmark defaults, and dashboard preferences.

Startup ("Start with Windows") is **disabled by default**. Enabling writes
`HKCU\...\Run\WinResGov` (per-user, no elevation). Disabling deletes the value
only when it points at this executable.

### 3.6 Dashboard
Real Win32 dashboard (no frameworks): live system stats, optimizer overhead,
engine state/rationale, browser process table (PID/group/CPU/WS/private WS/
private bytes/priority/flags), profile selector, automation + WS-trim-policy +
startup toggles, **Optimize now / Restore all / Run benchmark / Cancel
benchmark / Export CSV / Open data folder / Save settings** — every control
executes a real operation. Action history with results and errors; protected
and excluded process lists; an explicit notice of the native-only limitations.
Rendering is paused when the window is minimized or hidden.

### 3.7 Benchmarking
Reproducible **baseline vs candidate** measurement:
- *Baseline* = monitoring only. *Candidate* = one manual optimize pass at the
  start + the adaptive engine with the user's automation setting.
- Samples: system CPU %, used memory %, available physical bytes, self CPU %,
  self WS, browser process count/CPU/WS/private bytes, actions and errors per
  window. Phases report avg + peak.
- CSV export (RFC-4180 rows, metadata header comments) with hardware string,
  durations, sample interval. The benchmark always ends with a **restore pass**
  and records how many settings were restored.
- Everything written is labeled *measured* on the machine that ran it. Results
  from other machines (including CI runners) are **not** reference-hardware
  results. No improvement percentages are promised.

## 4. Build

### 4.1 Windows (MSVC — supported primary path)
Prerequisites: Visual Studio 2022 (or Build Tools) with the C++ x64 toolset, CMake ≥ 3.16.

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Outputs: `build\Release\WinResGov.exe`, `build\Release\test_core.exe`.
The MSVC build links the CRT statically (`/MT`) and embeds `RT_VERSION` +
the application manifest (`src/app.manifest`: comctl32 v6, PerMonitorV2 DPI,
`asInvoker`).

MinGW-w64 also works through the same CMakeLists (adds `-static -municode`).

### 4.2 Cross-build from Linux/macOS (verification path)
Prerequisites: the Zig toolchain (e.g. the PyPI `ziglang` wheel) or any
MinGW-w64 toolchain.

```sh
ZIG=/path/to/zig ./scripts/build_mingw.sh   # -> build/win/WinResGov.exe
```

This build intentionally omits the `.rc` (zig ships no resource compiler);
version strings are compiled in from `src/core/version.h` and DPI awareness is
set programmatically. The CI/MSVC build is the authoritative artifact with full
resources (see `DESIGN_LOG.md` D13).

### 4.3 Tests
```sh
./scripts/run_unit_tests.sh        # portable core tests, any host with g++
# or, on Windows:
build\Release\test_core.exe
build\Release\WinResGov.exe --selftest
build\Release\WinResGov.exe --smoke
build\Release\WinResGov.exe --bench --seconds 30 --csv bench.csv
```

## 5. Running

Portable run: copy `WinResGov.exe` anywhere and double-click. Settings and
benchmark CSVs are stored under `%LOCALAPPDATA%\WinResGov\` (no installer, no
registry writes beyond the optional per-user startup value).

CLI modes (also usable from scripts; combine with `--out FILE` for capture):

| Command | Effect |
|---------|--------|
| `WinResGov.exe` | dashboard |
| `WinResGov.exe --version` | version metadata |
| `WinResGov.exe --selftest` | portable core test suite (exit 0/1) |
| `WinResGov.exe --smoke` | runtime smoke: monitoring, engine, safety, config (exit 0/1) |
| `WinResGov.exe --bench --seconds N --csv PATH` | benchmark run + CSV |

## 6. Benchmark methodology

1. Warm-up sampling so CPU deltas exist, then two equal phases.
2. Baseline: monitoring only (this is also the *idle overhead* measurement of
   the optimizer). Candidate: one manual pass + engine activity (the *active
   overhead* measurement).
3. Fixed sample interval (default 500 ms), comparable durations, same machine.
4. The benchmark ends by restoring every applied change and records it.
5. CSV metadata includes CPU model, logical processor count, durations and the
   label policy. Report only measured values; a CI-runner result is labeled as
   such (GitHub-hosted `windows-2022`: typically 2–4 vCPU, not the i5-7300U).

## 7. Test status summary

Test statuses use exactly: PASS / FAIL / BLOCKED / NOT RUN.

| # | Test | Status | Evidence |
|---|------|--------|----------|
| 1 | Portable core unit tests (ini/config/stats/engine/csv: 93 checks) | **PASS** | `scripts/run_unit_tests.sh` → `93 checks, 0 failures` (Linux g++ 12.2) **and** `test_core.exe` on Windows CI → `93 checks, 0 failures` |
| 2 | Cross-compile Win32 x64 (build correctness) | **PASS** | `zig c++ -target x86_64-windows-gnu` → `build/win/WinResGov.exe` (1,219,584 B) |
| 3 | MSVC Release build with static CRT (build correctness) | **PASS** | windows-ci `Build Release` step (run 38900712691) |
| 4 | Single-EXE runtime dependencies | **PASS** | PE imports verified locally (cross-build) and by `dumpbin /dependents` on the MSVC build (CI): `KERNEL32/USER32/GDI32/ADVAPI32/SHELL32/COMCTL32/COMDLG32` only — no VCRUNTIME/MSVCP/libstdc++ |
| 5 | In-app selftest on Windows | **PASS** | `WinResGov.exe --selftest` on windows-ci → `93 checks, 0 failures`, `SELFTEST: PASS` |
| 6 | Runtime smoke on Windows (discovery/metrics/engine/safety/config) | **PASS** | `WinResGov.exe --smoke` on windows-ci → `17 checks, 0 failures`, `SMOKE: PASS` (Windows 10.0 build 20348 x64, 130 processes enumerated, self found exactly once, no double counting) |
| 7 | Benchmark baseline vs candidate on Windows (real run) | **PASS** | `--bench --seconds 12` on windows-ci: baseline (monitor-only) self CPU avg 0.0% peak 0.0%, self WS avg 5.92 MB; candidate (optimizer active) self CPU avg 0.0%, self WS avg 6.00 MB; 0 actions/0 failures (runner has no browsers — honest zero); CSV + report mirrored to PR #1 comment |
| 8 | VERSIONINFO metadata in the executable | **PASS** | CI check: ProductVersion `0.1.0-dev1`, OriginalFilename `WinResGov.exe` |
| 9 | Windows runtime verification on the *reference* hardware (i5-7300U) | **BLOCKED** | dev environment is Linux; no Windows machine or Wine available |
| 10 | GUI dashboard manual interaction testing | **BLOCKED** | no interactive Windows session in this environment; CI validates headless modes only |
| 11 | Optimization actions against real browser processes | **NOT RUN** | CI runner has no Brave/Chrome/Edge processes; synthetic-process engine checks cover the decision logic (smoke §5) |
| 12 | Per-tab management | **BLOCKED** (by design) | native-only constraint; see §2 |
| 13 | Network request filtering | **BLOCKED** (by design) | native-only constraint; see §2 |

CI runs on every push/PR (`.github/workflows/windows-ci.yml`): MSVC Release
build with static CRT, unit tests, selftest, smoke, 12 s benchmark with CSV
artifact, `dumpbin /dependents` self-containment gate, and VERSIONINFO check.
The full machine-readable reports of the verified run (run id 38900712691) are
mirrored into a comment on PR #1 (queryable through the GitHub API).

## 8. Known defects and limitations (v0.1.0-dev1)

- Private working set is best-effort: `QueryWorkingSet` needs
  `PROCESS_QUERY_INFORMATION|PROCESS_VM_READ`; protected/elevated processes
  report `unavailable`, very large working sets report `priv-ws-partial`.
- Parent PID comes from toolhelp snapshots and can be stale after a parent
  exits; tree grouping detects same-family roots only within one snapshot.
- Working-set trims are temporary by nature (pages return on access); the
  governor measures and records before/after but cannot prevent regrowth.
- `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` requires Windows 10 1709+; on
  older builds the API call fails with `ERROR_NOT_SUPPORTED`, which is recorded
  honestly and skipped (no retry storm).
- Priority changes are not CPU caps: a below-normal process can still use full
  CPU when nothing else needs it (documented Windows behavior).
- The dashboard is single-window English-only UI at this stage.
- The zig cross-build artifact lacks the embedded VERSIONINFO/manifest
  resources (build-tool limitation, §4.2); the MSVC/CI artifact has them.

## 9. Deferred features

- Per-tab monitoring/discard (needs extension + local IPC; §2).
- Network request filtering (needs extension/proxy/driver; §2).
- Per-process CPU affinity / CPU sets (needs careful cgroup-like policy design).
- Localized UI, installer package, autoupdate.
- Historical telemetry graphs and long-term storage (intentionally avoided to
  minimize disk writes).

## 10. Repository layout

```
src/core/     portable logic: types, stats, ini, config, engine, csv (no Win32)
src/win/      Win32: platform, sysmon, procmon, cpuopt, wsopt, executor, bench, app, ui
src/main.cpp  entry: CLI modes + dashboard launcher
src/app.rc    VERSIONINFO + manifest (MSVC/MinGW builds)
tests/        portable test suite (also wired to --selftest)
scripts/      cross-build + host test helpers
.github/      windows-ci workflow
DESIGN_LOG.md append-only design/decision record
AGENT_LOG.md  append-only execution record
```

See `DESIGN_LOG.md` for the full requirements intake, decisions and status
board, and `AGENT_LOG.md` for commands executed and their real results.
