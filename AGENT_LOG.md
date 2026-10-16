# AGENT_LOG.md — Windows Resource Governor v0.1.0-dev1

Append-only execution history. Evidence-based: only commands actually executed and
results actually observed are recorded. No credentials or tokens are recorded.

---

## Session 2026-10-16

### A1 — Workspace and git reconnaissance
- `pwd` → `/home/user`; repository checkout at `/home/user/-`.
- Repository content: `.git/`, `LICENSE` (MIT, (c) 2026 coderunknow) only.
- `git status` → on branch `arena/6f917583-repo`, working tree clean.
- `git remote -v` → `origin https://github.com/coderunknow/-.git` (fetch/push).
- `git branch -a` → `arena/6f917583-repo` (current), `main`, `origin/main`,
  `origin/HEAD -> origin/main`.
- `git log --oneline -15` → single commit `ae17a5f Initial commit`.
- No AGENTS.md anywhere under `/home/user` (find). No DESIGN_LOG.md / AGENT_LOG.md →
  both created before substantive implementation.

### A2 — GitHub verification
- `gh repo view coderunknow/- --json ...` → success: public repo `coderunknow/-`,
  default branch `main`, empty description. URL matches the supplied repository URL.
- `gh auth status` → logged in to github.com as `coderunknow` (GH_TOKEN), HTTPS git ops
  configured. (Token value not recorded.)

### A3 — Environment/toolchain survey
- `uname -a` → Linux e2b.local 6.1.158+ x86_64; `/etc/os-release` → Debian GNU/Linux 12.
- `nproc` → 2; `free -m` → ~4 GB total RAM.
- Present: g++ 12.2.0, gcc, GNU make, python3 3.11.2, pip3 23.0.1.
- Absent: cmake, ninja, clang, mingw-w64, wine, MSVC.
- `apt-get install --dry-run mingw-w64` → `E: Unable to locate package mingw-w64`
  (apt repositories unreachable/unindexed). Windows toolchain cannot come from apt.
- curl probe `https://github.com/.../releases/download/...` (llvm-mingw) → HTTP 302 to
  `release-assets.githubusercontent.com`, then `curl: (35) OpenSSL SSL_connect:
  SSL_ERROR_SYSCALL` — release-asset host blocked by network policy. Prebuilt toolchain
  tarballs from GitHub Releases are not obtainable.
- pypi reachable. Downloaded `ziglang-0.17.0` wheel (101 MB) via pip (`pip3 download`).
  `pip3 install` refused (PEP 668 externally-managed); wheel unzipped to
  `/home/user/toolchain/zigpkg/ziglang/zig`; `zig version` → `0.17.0`.

### A4 — Windows cross-compilation probe
- Test file `hello.cpp` (GlobalMemoryStatusEx + printf) compiled with
  `/home/user/toolchain/zigpkg/ziglang/zig c++ -target x86_64-windows-gnu -O2 -static`
  → `hello.exe` 220,160 bytes, exit code 0.
- PE import table parsed with a custom Python PE parser (tool output):
  `Imported DLLs: ['api-ms-win-crt-heap-l1-1-0.dll', 'api-ms-win-crt-locale-l1-1-0.dll',
  'api-ms-win-crt-runtime-l1-1-0.dll', 'api-ms-win-crt-stdio-l1-1-0.dll',
  'KERNEL32.dll', 'api-ms-win-crt-environment-l1-1-0.dll', 'api-ms-win-crt-math-l1-1-0.dll',
  'api-ms-win-crt-string-l1-1-0.dll']` — OS components only (UCRT API-sets ship with
  Windows 10), no application-specific or redistributable DLLs. Confirms the feasibility
  of a genuinely self-contained single-EXE via this toolchain.

### A5 — Pre-implementation decisions recorded
- DESIGN_LOG.md Entry 1 written: full requirement intake, reconnaissance evidence,
  decisions D1–D14, native-only limitation assessment (tab management and network
  filtering → BLOCKED, documented), requirement status board R1–R15.

### A6 — Next steps planned (not yet executed at time of this entry)
1. Portable core modules (types, ini, config, stats, engine, csv) + unit tests.
2. Windows modules (sysmon, procmon, cpuopt, wsopt, bench, ui, platform, main).
3. Build system (CMake + zig/mingw script), resources, .gitignore.
4. Local: run unit tests (Linux g++), cross-build Win32 EXE, verify PE imports.
5. Windows CI (GitHub Actions): MSVC build + unit tests + smoke + short benchmark.
6. README + logs; commits on `arena/6f917583-repo`; push; PR to `main` (left OPEN).

---

### A7 — Portable core implementation + host tests (2026-10-16)
- Implemented `src/core/`: types, stats, ini, config, engine (profiles,
  pressure levels, hysteresis/dwell/cooldowns, safety rules, restore planning),
  csv; `tests/core_tests.h` + `tests/test_core.cpp`.
- `g++ -std=c++17 -Wall -Wextra -O2 -I src tests/test_core.cpp src/core/*.cpp -o build/host/test_core`
  then `./build/host/test_core`:
  - First run: 89 checks, 10 failures. Root causes found and fixed:
    (a) test arithmetic error on normalized CPU% (0.25 core on 4 LP is 6.25%,
    not 25%); (b) engine hysteresis only shifted the Low threshold — fixed to
    shift all entry thresholds; (c) realtime-class processes were eligible for
    throttling — now never modified in any way; (d) trim "top-3" loop rotated to
    smaller processes across ticks (trim-on-a-timer anti-pattern) — fixed to
    bound *candidates considered* per tick; (e) recovery restore was gated on
    automation being enabled — restore now runs regardless (safety obligation).
  - Final: **93 checks, 0 failures** (exit 0). Full log:
    `build/host/test_core_full.txt` (not committed; build/ is gitignored).

### A8 — Windows layer implementation + local cross-build (2026-10-16)
- Implemented `src/win/` (platform, sysmon, procmon, cpuopt, wsopt, executor,
  bench, app, ui), `src/main.cpp` (CLI modes + dashboard), `src/app.rc` +
  `src/app.manifest`, CMakeLists.txt, Makefile, scripts/, .gitignore.
- `ZIG=/home/user/toolchain/zigpkg/ziglang/zig ./scripts/build_mingw.sh`:
  - Compile errors fixed along the way: wrong API name
    `GetProcessPriorityClass` (correct: `GetPriorityClass`) in procmon.cpp and
    cpuopt.cpp; missing `<windows.h>` in executor.cpp;
    `CreateStatusWindowW` takes 4 arguments; `it->priorityClass` →
    `it->second.priorityClass` in main.cpp; missing `-lgdi32` at link.
  - Final: **built: build/win/WinResGov.exe (1,219,584 bytes)**, exit 0.
- PE import table parsed (custom Python PE parser): x64 PE32+; imports =
  ADVAPI32, SHELL32, COMCTL32, COMDLG32, GDI32, KERNEL32, USER32 +
  `api-ms-win-crt-*` (UCRT API-sets = OS components on Windows 10).
  Non-self-contained imports: **NONE**.

### A9 — Git workflow (2026-10-16)
- Commits (conventional messages, task files only):
  - `c8e7cad` docs: add DESIGN_LOG.md and AGENT_LOG.md
  - `677d825` feat(core): portable engine, config, INI, stats and CSV modules
  - `c223532` test(core): portable unit test suite (93 checks)
  - `c8c904d` feat(win): Win32 monitoring, optimization, benchmark, dashboard, CLI
  - `2537ade` build: CMake, zig cross-build script, resources
  - `8d43ae3` ci: windows-ci workflow
  - `84947fc` docs: README
  - `cf8bf73` fix(ci): smoke engine checks independent of installed browsers; PR report mirror
  - `c06447c` fix(ci): robust PR report publishing
- `git push -u origin arena/6f917583-repo` → success (verified from push output).
- PR opened via `gh pr create --base main --head arena/6f917583-repo` →
  **https://github.com/coderunknow/-/pull/1** (state: OPEN, to be left unmerged).

### A10 — Windows CI execution history (real results; never fabricated)
Runner: GitHub-hosted `windows-2022` (Windows 10.0 build 20348 x64,
AMD EPYC 7763, 4 LP presented, elevated service account — NOT reference
hardware). Note: `gh run view --log` and `gh run download` (artifact host
`productionresultssa*.blob.core.windows.net`) were unreachable from this
network (TLS EOF), so reports were mirrored into PR #1 comments via the
workflow (API-readable) after two reporting iterations.

1. **Run 38899670666 (push, first attempt): Build ✓, Unit tests ✓, Selftest ✓,
   Smoke ✗, Benchmark skipped.** Failure diagnosed from the smoke assertions:
   engine check asserted `actions > 0` under pressure, but the CI runner hosts
   no browser processes — zero eligible targets is *correct* behavior. The
   check was fixed to use synthetic processes (same approach as the unit
   tests); the metric-distinction check was made self-process-based (suspended
   system processes can legitimately report WS 0).
2. **Run 38900388487 (push, second attempt): Build ✓, Unit tests ✓,
   Selftest ✓, Smoke ✓, Benchmark ✓, dumpbin gate ✓, VERSIONINFO ✓;
   report-publish step ✗** (PowerShell quoting bug in the jq filter). Fixed by
   posting JSON via `gh api --input` with `ConvertFrom-Json` matching, and made
   the reporting step non-fatal so it can never mask verification results.
3. **Run 38900712691 (push, third attempt) / PR run 38900705777: ALL STEPS ✓
   (green).** Verified results (from the PR #1 report comment):
   - `test_core.exe`: 93 checks, 0 failures — **PASS**
   - `WinResGov.exe --selftest`: 93 checks, 0 failures, SELFTEST: PASS
   - `WinResGov.exe --smoke`: 17 checks, 0 failures, SMOKE: PASS
     (130 processes enumerated; self identified exactly once; 0 browsers, 1
     inaccessible process on the runner; ws/private/commit distinct; engine
     Saver/Balanced act at High (2 actions), Performance action-free at
     Moderate; safety: plans target only eligible synthetic background work;
     restore-all empty-state no-op; startup query non-mutating)
   - `WinResGov.exe --bench --seconds 12 --csv bench_ci.csv`: measured —
     baseline (12 samples): sysCpu avg 2.5% peak 25.0%, self CPU avg 0.0%
     peak 0.0%, self WS avg 5.92 MB peak 5.93 MB, usedMem avg 15.2%;
     candidate (12 samples): sysCpu avg 0.5% peak 2.3%, self CPU avg 0.0%
     peak 0.0%, self WS avg 6.00 MB peak 6.02 MB, usedMem avg 15.3%;
     actions 0 (no browsers present — honest zero), failures 0, restores 0.
   - `dumpbin /dependents WinResGov.exe` (MSVC /MT build): ADVAPI32, SHELL32,
     COMCTL32, COMDLG32, USER32, GDI32, KERNEL32 only — no VCRUNTIME/MSVCP —
     single-EXE gate **PASS**.
   - VERSIONINFO check: ProductVersion `0.1.0-dev1`, OriginalFilename
     `WinResGov.exe` — **PASS**.

### A11 — BLOCKED / NOT RUN (explicit, for the record)
- Reference-hardware (i5-7300U) runtime and benchmark: **BLOCKED** (Linux dev
  environment, no Windows machine/Wine).
- Interactive dashboard testing: **BLOCKED** (no interactive Windows session).
- Optimization actions against real browser processes (priority/throttle/trim):
  **NOT RUN** (CI runner hosts no Brave/Chrome/Edge). Engine decision logic is
  covered by synthetic-process unit tests and smoke checks.
- Per-tab management, network request filtering: **BLOCKED** by the native-only
  constraint (see DESIGN_LOG §1.4); deliberately not faked.

### A12 — Remaining work
- Re-run benchmarks on the reference hardware and with real browser sessions;
  record before/after CSVs (needs a Windows machine).
- Manual dashboard pass on an interactive Windows session.
- Post-0.1.0-dev1: extension + local IPC path for tab-level features (design in
  DESIGN_LOG §1.4) pending user approval of that architecture.

---

### A13 — Final audit (2026-10-16, pre-merge) — commands and results
- `git status --short` → empty (clean); `git rev-parse HEAD` →
  `aeaa9377296cf183f918e1320fe6948db89fac9a`; `gh pr view 1` headOid identical.
- `git ls-files` → 46 files; artifact-name filter (`*.exe/*.obj/*.pdb/build/...`)
  → none. `head -3 LICENSE` → MIT, (c) 2026 coderunknow, unmodified.
- `git grep` secrets patterns (ghp_/github_pat_/AKIA/private keys/password=)
  → no matches in tracked files. Credential patterns in DESIGN_LOG/AGENT_LOG →
  none. Machine paths (`/home/user`, toolchain) appear only in AGENT_LOG
  command evidence, not in code or build scripts.
- `git grep` prohibited APIs (TerminateProcess, CreateRemoteThread,
  WriteProcessMemory, VirtualAllocEx, SetWindowsHookEx, DebugActiveProcess) in
  src/ → none. Service/firewall/hosts patterns → none. Network APIs
  (WinHttp/InternetOpen/URLDownload/WSAStartup) in src/ → none.
- `src/app.manifest` → `requestedExecutionLevel level="asInvoker"`; no `runas`.
  REALTIME `SetPriorityClass` assignments → none. TODO/FIXME/fake/mock/stub in
  src/ → none.
- Version fields re-grepped: `0.1.0-dev1` / `0.1.0.1` consistent across
  `src/core/version.h`, `src/app.rc`, `src/app.manifest`, `README.md`, `ui.cpp`.
- `./scripts/run_unit_tests.sh` → `=== 93 checks, 0 failures ===` (exit 0).
- `ZIG=/home/user/toolchain/zigpkg/ziglang/zig ./scripts/build_mingw.sh` →
  `built: build/win/WinResGov.exe (1219584 bytes)`. PE import table re-parsed:
  ADVAPI32/COMCTL32/GDI32/KERNEL32/SHELL32/USER32/comdlg32 + api-ms-win-crt-*
  only; self-contained gate **PASS**.
- Log integrity: DESIGN_LOG `## Entry 1` (line 8) precedes `## Entry 2`
  (line 253); AGENT_LOG A1..A12 headers present in order — append-only intact.
- `gh api .../check-runs` on `aeaa937`: `build-test-bench: success` (both push
  and PR runs); `Kilo Code Review: action_required` (external bot lacks
  credits). `gh pr view 1` → `state: OPEN, mergeable: MERGEABLE,
  mergeStateStatus: UNSTABLE` (unstable solely due to the external bot).
- `gh api .../matching-refs/tags` → 0 tags; `gh release list` → empty.
  No `v0.1.0-dev1` collision.
- Audit verdict: **PASS (19/19 checks; 1 external note)**. Authorized to merge
  and tag per the user's latest instruction (policy change recorded at
  DESIGN_LOG Entry 3.1).

---
