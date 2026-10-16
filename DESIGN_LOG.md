# DESIGN_LOG.md — Windows Resource Governor

Authoritative, append-only record of intent, design decisions, and implementation status.
Rule: never truncate, overwrite, reorder, or replace existing entries. Append only.

---

## Entry 1 — 2026-10-16 — Task intake and original requirements (pre-implementation)

### 1.1 Original user requirements (faithful record)

The user issued a build task: **Native Windows Resource Governor v0.1.0-dev1**. The full
requirement text is the task brief delivered with the assignment. It is recorded here in
structured, non-vague form; nothing material is omitted.

**Project frame**
- Repository: https://github.com/coderunknow/- (to be verified, not assumed).
- Application name: preserve existing repository project name if one exists; otherwise
  provisional name **"Windows Resource Governor"**.
- Target version: **v0.1.0-dev1** (prefix-free form `0.1.0-dev1` in version fields).
- Target platform: **Windows 10 x64**. Reference hardware: Intel Core i5-7300U,
  4 logical threads, 8 GB RAM, Intel HD Graphics 620, limited SSD.
- Target browsers: Brave, Google Chrome, Microsoft Edge.
- Architecture: **native Windows C++**, documented Windows APIs, **no mandatory browser
  extension or browser bridge** (native-only constraint explicitly selected by the user).
- UI: functional desktop dashboard. Distribution: prefer genuinely self-contained
  single-EXE. Startup: optional, **disabled by default**. Privileges: standard user by
  default; request elevation only for a specific operation that demonstrably requires it.
- Optimization policy: aggressive within explicit safety boundaries.
- Testing: strict testing, runtime verification, reproducible performance benchmarks.
- Git workflow: dedicated feature branch, commits, push, open PR. **Never merge the PR.**

**Reconnaissance required before modifying files**: workspace/repo root; git remotes,
branches, status; existing source/build/CI/dependencies/tests; naming and version
conventions; applicable repository instructions (AGENTS.md); DESIGN_LOG.md / AGENT_LOG.md
(create if absent); whether the environment can compile/run Windows binaries; verify the
supplied repository URL using workspace, git config, and GitHub tools. Distinguish invalid
URL / missing repo / permissions / auth / network failures. Do not guess another repository
or change remotes. Preserve unrelated modifications.

**Mandatory logs**: DESIGN_LOG.md (this file) and AGENT_LOG.md, both append-only,
evidence-based. Never fabricate commands, results, hashes, URLs, or measurements.
Record requirements as DONE / NOT DONE / BLOCKED with rationale.

**Architecture**: low-overhead native C++ with documented Windows APIs; lightweight Win32
preferred; separate components for: system resource monitoring; browser process discovery
and aggregation; adaptive decision engine; CPU and power-efficiency controls;
working-set management; optimization profiles/configuration; benchmarking and reporting;
safety policies/exclusions/restoration; dashboard/diagnostics; version metadata/build
config. Avoid unnecessary abstractions, threads, allocations, polling, dependencies, disk
writes. No cloud/remote AI services at runtime. Single-EXE must be verified for real
runtime dependencies (not claimed because compilation produced one file); if genuinely
standalone is impossible, document why and the smallest viable alternative.

**Feature requirements (A–G)**
- A. Real resource monitoring: system physical/available memory; browser CPU utilization;
  browser working set and other memory metrics; PIDs, exe paths, process relationships,
  lifecycle changes; optimizer's own CPU/memory overhead. Support Brave/Chrome/Edge.
  Handle multiple instances, terminations, inaccessible processes, missing browsers,
  tree changes, invalid measurements. No double-counting. Distinguish working set, private
  working set (when available), private bytes, committed memory, system available physical
  memory, CPU time vs normalized CPU utilization. Never invent per-tab metrics.
- B. Adaptive engine: profiles **Saver / Balanced / Performance**; configurable thresholds,
  hysteresis, cooldowns, exclusions; user-controlled automation; act only under real
  resource pressure; prevent oscillation from noise/spikes; preserve foreground
  responsiveness; observable actions; strongest strategy that is measurable, reversible
  where possible, and safe.
- C. CPU optimization: documented mechanisms (process priority, power-efficiency hints);
  lower priority of eligible background work only when justified; never real-time
  priority; never indiscriminately lower every browser process; verify API availability
  on Windows 10 (no Windows 11-only assumptions); restore previous settings; handle
  permission failures; record actions and results. Do not claim priority is a CPU limit.
- D. Working-set/RAM management: carefully controlled; trimming does not remove underlying
  memory requirements and can cause page faults/latency/regrowth; never trim every browser
  process on a timer; protect foreground and excluded processes by default; require an
  enabled policy before automatic aggressive actions; meaningful pressure thresholds and
  cooldowns; eligibility/permission checks; record errors and observed effects; measure
  subsequent growth where feasible; never describe trim as guaranteed permanent savings.
- E. Configuration/profiles: persistent settings for profiles, thresholds, excluded and
  protected processes, manual/automatic optimization, dashboard preferences, startup
  behavior. Validate all settings; corrupted/missing config → safe defaults. Never silently
  modify browser profiles, erase browsing data, or change browser launch arguments.
- F. Benchmarking: reproducible baseline-vs-candidate measurements; CPU utilization over
  defined windows; system and browser memory; idle and active optimizer overhead; peak and
  average values; duration and workload; operations and outcomes; errors and restoration.
  Comparable workloads/durations; distinguish CPU time vs normalized CPU utilization vs
  working set; CSV export where feasible; label measured/estimated/unavailable/unverified;
  never invent improvement percentages or promise universal gains.
- G. Safety/diagnostics: protected-process lists, exclusions, automation settings, action
  history, clear diagnostics. Must NOT: automatically terminate browser processes; inject
  code; use undocumented hooks; install kernel drivers; disable antivirus/security; disable
  unrelated Windows services; modify firewall/hosts; modify browser user data to
  manufacture gains; require Administrator unnecessarily; hide failures or retry disruptive
  operations indefinitely. Actions bounded, understandable, reversible where possible.

**Native-only constraints (explicit)**
- Tab management: ordinary process APIs cannot reliably know per-tab JS activity, audio,
  unsaved forms, discard eligibility. Implement real process-level optimizations only.
  If per-tab monitoring/discard requires an extension/bridge: do NOT add one silently, do
  NOT add fake controls, mark NOT DONE or BLOCKED, explain the exact limitation, document
  the minimum additional architecture for a later version.
- Network request optimization (redundant/tracking/telemetry requests): a native process
  monitor cannot selectively control individual HTTPS requests. Investigate; do not claim
  filtering unless implemented and verified. If it requires extension/bridge/proxy or other
  unapproved architecture, defer and document. No fake UI switches. Honest partial
  completion preferred over misleading implementation.

**Dashboard**: real system measurements; detected browser processes + aggregates; current
profile; adaptive-policy status; protected processes and exclusions; available actions and
actual outcomes; recent operations and errors; benchmark controls and recorded results;
configuration and optional startup settings. Every control executes a real operation.
Unavailable capabilities clearly identified. Minimize rendering/allocations/refresh/disk
writes; reduce UI activity when hidden/minimized; usable on reference hardware.

**Version/docs**: v0.1.0-dev1 everywhere; consistent source/executable/config/docs; document
supported Windows versions, browsers, architecture/dependencies, privileges, build
prerequisites/commands, install/portable run, profile behavior and exclusions, safety
controls/restoration, benchmark methodology, actual test results, known defects/limits,
deferred features. Do NOT create a version tag, GitHub release, or published assets.

**Strict testing**: run strongest relevant tests the environment supports; verify build
correctness; process discovery/lifecycle; CPU/memory math; thresholds/hysteresis/cooldowns/
profiles/exclusions; invalid settings recovery; WS eligibility/failures/side effects;
permission failures/restoration; dashboard controls/state; idle/active overhead;
reproducible before/after; single-EXE dependencies and launch; regression checks. Test
statuses exactly: **PASS / FAIL / BLOCKED / NOT RUN**. Compilation is not proof of runtime
correctness; unit tests are not proof of performance improvement. If Windows binaries cannot
run locally: no fabricated Windows results; complete possible checks; configure Windows CI
if feasible; report outstanding validation explicitly.

**Acceptance criteria**: confirmed repo/branch; instructions and unrelated changes
preserved; core features real; all three profiles testable; genuine measurement sources;
aggressive ops respect safety rules; dashboard reflects real state; native-only limits
documented; genuine test/benchmark results; single-EXE verified as far as possible;
logs append-only and evidence-based; version metadata v0.1.0-dev1; committed to dedicated
branch; pushed and PR opened if permitted. Unmet criteria marked BLOCKED or NOT DONE.

**Git/GitHub workflow**: inspect before changing git state; dedicated feature branch after
collision check (suggested: `feat/windows-resource-governor-v0.1.0-dev1`; follow existing
conventions if any); focused commits; stage only task files; review staged diff; push
without force; open PR against verified default branch if authorized. ABSOLUTE: never merge
the PR; never squash/rebase-merge; never auto-merge; never force-push; never release/tag;
never change repo permissions/visibility; never change remote to unverified destination;
never claim push/PR success without verification. If publication blocked, preserve local
work and report the exact blocker.

**Final report** must include: verified repository URL; branch and PR URL; implemented
functionality and important files; version metadata and verified executable path; actual
build/test commands; PASS/FAIL/BLOCKED/NOT RUN results; baseline vs candidate benchmark
measurements; methodology/environment/limitations; measured idle and active overhead where
execution was available; unimplemented features and reasons; actual commit hash, push
result, PR state. No claims without evidence.

### 1.2 Reconnaissance results (evidence)

- Workspace root: `/home/user/-` (git checkout of `coderunknow/-`). Working tree clean,
  untracked: none. Branch checked out: `arena/6f917583-repo` (Arena session branch,
  created from `main` at `ae17a5f`).
- Git remotes: `origin https://github.com/coderunknow/-.git` (fetch and push). Default
  branch: `main` (verified via `gh repo view` → `defaultBranchRef.name = main`).
- Repository state: single commit `ae17a5f Initial commit`; only file: `LICENSE`
  (MIT, Copyright (c) 2026 coderunknow). No source, build system, CI, tests, README,
  AGENTS.md, DESIGN_LOG.md, or AGENT_LOG.md existed.
- GitHub verification: `gh repo view coderunknow/-` succeeded → URL exists, public,
  owner `coderunknow`, name `-`, no description. `gh auth status`: authenticated as
  `coderunknow` (GH_TOKEN), HTTPS git operations configured. Permissions: token can read;
  push/PR authorization to be verified when exercised.
- Existing project naming: none (repository name is `-`, no metadata). Decision: use the
  provisional name **"Windows Resource Governor"** per the brief; short code name
  `wrg`; executable `WinResGov.exe`.
- Environment: Linux Debian 12 (x86_64), kernel 6.1.158, 2 vCPU, ~4 GB RAM. Native
  Windows execution is **not** available (no Windows, no Wine). Toolchain present:
  g++ 12.2.0, GNU make. Not present initially: cmake, ninja, MinGW-w64, MSVC, Wine.
- Network policy: outbound access limited to github.com, codeload.github.com,
  api.github.com, registry.npmjs.org, pypi.org, files.pythonhosted.org.
  - `apt` package installation: **not possible** (`mingw-w64` package not locatable;
    Debian mirrors unreachable through the proxy).
  - GitHub *release asset* downloads (host `release-assets.githubusercontent.com`):
    **blocked** (TLS connection killed by proxy — verified with curl: HTTP 302 then
    `SSL_ERROR_SYSCALL`). This blocks prebuilt toolchain tarballs from GitHub Releases.
  - pypi.org: reachable. `ziglang` 0.17.0 wheel (Zig toolchain, 101 MB) downloaded
    successfully and extracted; `zig` 0.17.0 runs.
- Cross-compilation probe: `zig c++ -target x86_64-windows-gnu -static hello.cpp` produced
  `hello.exe` (220 KB). PE import table inspected with a custom PE parser: imports only
  `KERNEL32.dll` and `api-ms-win-crt-*` UCRT API-set DLLs (OS components on Windows 10+),
  no third-party or redistributable DLLs. This validates the local build path for a
  genuinely self-contained Windows 10 x64 executable.

### 1.3 Major decisions and rationale (pre-implementation)

| # | Decision | Alternatives considered | Rationale / evidence |
|---|----------|------------------------|----------------------|
| D1 | Application name "Windows Resource Governor", code `wrg`, EXE `WinResGov.exe` | Reusing repo name `-` | Repo has no project metadata; brief mandates provisional name. |
| D2 | UI: raw Win32 + common controls (listviews, buttons, combo, status bar) | Qt, wxWidgets, Dear ImGui, WinUI | Brief prefers lightweight Win32; avoids runtime DLLs → supports single-EXE on 8 GB / 4-thread reference hardware. |
| D3 | Language standard: C++17, no exceptions in hot paths, no RTTI dependence | C++20 | Widely supported by MSVC and MinGW/zig; sufficient for the design. |
| D4 | Build systems: CMakeLists.txt (Windows MSVC/MinGW) + `scripts/build_mingw.sh` (local zig cross-build) + Makefile convenience | Only Makefile | CMake is the mainstream Windows path (used by CI); Makefile/script enables local cross-verification without cmake. |
| D5 | Local build toolchain: Zig 0.17.0 `zig c++ -target x86_64-windows-gnu` (MinGW-w64 ABI, UCRT) | llvm-mingw tarball; Debian mingw-w64; MSVC | Release-asset downloads blocked; apt blocked; pypi allowed. Zig verified to produce clean PE with only OS imports. UCRT is an OS component on Windows 10 → no redistributable. |
| D6 | Windows runtime verification via GitHub Actions `windows-2022`/`windows-latest` runners (build + unit tests + smoke + short benchmark, artifacts uploaded) | Claiming local results; no runtime tests | Local env cannot execute PE files. CI gives real Windows build/test/execution evidence. Not reference hardware — benchmarks labeled accordingly. |
| D7 | Portable core (engine, INI config, stats, CSV) free of Windows headers + `tests/test_core.cpp` runnable natively on Linux **and** Windows | Windows-only tests | Enables genuine local test execution (PASS/FAIL evidence) and regression testing on CI. |
| D8 | CLI test modes in the app itself: `--version`, `--selftest`, `--smoke`, `--bench [--seconds N --csv PATH]` | GUI-only app | Headless CI execution; reproducible benchmark entry point; dashboard still the primary UI. |
| D9 | Subsystem WINDOWS with best-effort `AttachConsole(ATTACH_PARENT_PROCESS)` for CLI output; CI captures redirected stdout/stderr | CONSOLE subsystem always | Double-click launch shows no console window; CLI output still capturable via redirection (inherited handles). |
| D10 | Branch naming: work is committed to `arena/6f917583-repo` (session-fixed) which serves as the dedicated feature branch; PR opened from it to `main` | `feat/windows-resource-governor-v0.1.0-dev1` | Arena session is fixed to `arena/6f917583-repo`; system constraint forbids creating/pushing other branches. Deviation from the *suggested* branch name recorded here per "document scope changes and deviations". The branch is still dedicated to this feature and carries the version in its PR title/body. |
| D11 | Version metadata: string `0.1.0-dev1` everywhere; Windows VERSIONINFO numeric 4-part `0.1.0.1` with `StringFileInfo` `0.1.0-dev1`; `version.h` is the single source of truth | Numeric `0.1.0.0`; `0.1.0.0-dev1` | Windows VERSIONINFO requires 4 numeric shorts; the mapping is documented in README and this log. |
| D12 | No binary artifacts committed to git (EXE/objects stay in `build/`, gitignored); CI uploads EXE as workflow artifact | Commit built EXE | Repo hygiene; brief says keep large/generated artifacts out of Git unless required. |
| D13 | Version/manifest resources via `app.rc` compiled by rc.exe (MSVC CI) / windres (MinGW); zig local build compiles without the `.res` (code carries identical version strings; DPI awareness set programmatically) | Hand-crafting .res in Python | Zig toolchain lacks a resource compiler. Functionally identical runtime; the CI build embeds full RT_VERSION + manifest. Documented as a build-variant difference. |
| D14 | Benchmark honesty: results labeled with hardware/runner identity; improvement percentages only when computed from captured CSV; "measured / estimated / unavailable / unverified" labels used | Presenting CI numbers as reference-hardware gains | Brief forbids invented improvements. CI hardware ≠ i5-7300U. |

### 1.4 Capability assessment for native-only constraints (pre-implementation)

- **Per-tab management (NOT DONE / BLOCKED for v0.1.0-dev1)**: Win32 process APIs expose
  process-level data only. Chromium-based browsers (Chrome/Brave/Edge) map tabs to renderer
  *processes* non-deterministically (process-per-site-instance), and tab identity, audio
  state, unsaved form state, and discard eligibility are not exposed through documented
  process APIs. Reliable per-tab monitoring/discard requires a browser extension (e.g.
  `chrome.tabs` / `tabs.discard`) or a browser-native bridge — architectures the user did
  not approve for this version. Process-tree aggregation is implemented instead; a fake
  tab UI is explicitly rejected. Minimum future architecture: an extension exposing tab
  metadata + optional discard, communicating with the app over a local IPC channel
  (named pipe/localhost with token), with an explicit user opt-in.
- **Network request filtering (NOT DONE / BLOCKED for v0.1.0-dev1)**: selective filtering
  of HTTPS requests from a separate native process would require an extension
  (`declarativeNetRequest`), a local proxy with MITM certificates, a WFP driver, or LSP
  injection — each unapproved, invasive, or privilege-heavy. Windows Defender/SmartScreen
  and browser security models make out-of-process request filtering without consent
  effectively unsupported. Deferred; no fake switch will be shipped. Minimum future
  architecture: optional extension using `declarativeNetRequest` (no MITM), or an
  explicitly consented local proxy profile.

### 1.5 Requirement status board (initial)

| Req | Description | Status |
|-----|-------------|--------|
| R1 | Verified repository + workflow (branch/commit/push/PR) | IN PROGRESS |
| R2 | Append-only DESIGN_LOG.md / AGENT_LOG.md | DONE (created pre-implementation) |
| R3 | Real resource monitoring (A) | PENDING |
| R4 | Adaptive engine with 3 profiles (B) | PENDING |
| R5 | CPU/priority/power optimization (C) | PENDING |
| R6 | Working-set management with policy gates (D) | PENDING |
| R7 | Persistent validated configuration (E) | PENDING |
| R8 | Benchmarking + CSV (F) | PENDING |
| R9 | Safety/diagnostics (G) | PENDING |
| R10 | Native dashboard | PENDING |
| R11 | Single-EXE verification | PENDING |
| R12 | Strict tests + real statuses | PENDING |
| R13 | v0.1.0-dev1 metadata + documentation | PENDING |
| R14 | Tab management (native-only) | BLOCKED (documented limitation, §1.4) |
| R15 | Network request filtering (native-only) | BLOCKED (documented limitation, §1.4) |

Statuses will be updated in later append-only entries (never edited in place above).

---
