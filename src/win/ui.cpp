#include "win/ui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/stats.h"
#include "core/version.h"
#include "win/app.h"
#include "win/bench.h"
#include "win/executor.h"
#include "win/platform.h"

namespace wrg {

namespace {

enum : int {
    IDC_LBL_SYS = 1001,
    IDC_LBL_SELF,
    IDC_LBL_ENGINE,
    IDC_PROFILE,
    IDC_AUTOMATION,
    IDC_WSTRIM,
    IDC_STARTUP,
    IDC_SAVE,
    IDC_LBL_BROWSERS,
    IDC_LIST_PROCS,
    IDC_OPTIMIZE,
    IDC_RESTORE,
    IDC_RUNBENCH,
    IDC_CANCELBENCH,
    IDC_EXPORT,
    IDC_OPENFOLDER,
    IDC_LBL_BENCH,
    IDC_LBL_NOTE,
    IDC_LIST_LOG,
    IDC_LIST_PROT,
    IDC_STATUS,
};

constexpr UINT WM_APP_BENCH_DONE = WM_APP + 42;
constexpr UINT_PTR kTimerId = 1;

struct UiState {
    App app;
    HWND hwnd = nullptr;
    HWND lblSys = nullptr, lblSelf = nullptr, lblEngine = nullptr, lblBrowsers = nullptr,
         lblBench = nullptr, lblNote = nullptr;
    HWND profile = nullptr, automation = nullptr, wstrim = nullptr, startup = nullptr, save = nullptr;
    HWND listProcs = nullptr, listLog = nullptr, listProt = nullptr;
    HWND btnOptimize = nullptr, btnRestore = nullptr, btnRunBench = nullptr, btnCancelBench = nullptr,
         btnExport = nullptr, btnOpen = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;

    std::atomic<bool> benchRunning{false};
    std::atomic<bool> benchCancel{false};
    std::unique_ptr<BenchResult> lastBench;
    std::string benchStatus = "Benchmark: idle";
    uint64_t benchStartMs = 0;
    int lastProcRows = -1;
};

// ---- small helpers ---------------------------------------------------------

void setFont(HWND h, HFONT font) {
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND mkControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id,
               HFONT font) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             GetModuleHandleW(nullptr), nullptr);
    if (h && font) setFont(h, font);
    return h;
}

void addColumn(HWND list, int idx, const wchar_t* title, int width) {
    LVCOLUMNW col;
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = width;
    col.iSubItem = idx;
    ListView_InsertColumn(list, idx, &col);
}

void setCell(HWND list, int row, int col, const std::wstring& text) {
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text.c_str()));
}

int addRow(HWND list, const std::wstring& first) {
    LVITEMW item;
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(list);
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(first.c_str());
    const int row = ListView_InsertItem(list, &item);
    return row;
}

std::wstring ws(const std::string& s) { return widen(s); }

std::wstring pctW(double v) {
    return widen(formatPercent(v));
}

std::wstring bytesW(uint64_t b) { return widen(formatBytes(b)); }

std::wstring prioName(int pc) {
    switch (pc) {
        case kPriorityIdle: return L"idle";
        case kPriorityBelowNormal: return L"below-normal";
        case kPriorityNormal: return L"normal";
        case kPriorityAboveNormal: return L"above-normal";
        case kPriorityHigh: return L"high";
        case kPriorityRealtime: return L"realtime (never modified)";
        case 0: return L"unknown";
        default: return std::to_wstring(pc);
    }
}

std::wstring timeW(int64_t wallMs) {
    const time_t t = static_cast<time_t>(wallMs / 1000);
    struct tm tmv;
#if defined(_MSC_VER)
    gmtime_s(&tmv, &t);
#else
    const struct tm* p = gmtime(&t);
    if (p) tmv = *p; else return L"?";
#endif
    wchar_t buf[32];
    swprintf(buf, 32, L"%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

const wchar_t* familyName(int family) {
    switch (family) {
        case 1: return L"Chrome";
        case 2: return L"Brave";
        case 3: return L"Edge";
        default: return L"-";
    }
}

// ---- refresh ---------------------------------------------------------------

void refreshLists(UiState* ui) {
    const bool iconic = IsIconic(ui->hwnd) != 0;
    const bool visible = IsWindowVisible(ui->hwnd) != 0;
    if (ui->app.config().minimizeUpdatesWhenHidden && (iconic || !visible)) return;

    // Process list.
    const auto& procs = ui->app.procs();
    int browserRows = 0;
    for (const auto& p : procs)
        if (p.browserFamily != 0 || p.isSelf) ++browserRows;
    if (browserRows != ui->lastProcRows) {
        ListView_DeleteAllItems(ui->listProcs);
        ui->lastProcRows = 0;
    }
    int row = 0;
    uint64_t totalWs = 0;
    double totalCpu = 0.0;
    for (const auto& p : procs) {
        if (p.browserFamily == 0 && !p.isSelf) continue;
        totalWs += p.wsBytes;
        totalCpu += p.cpuPercent;
        if (row >= ListView_GetItemCount(ui->listProcs)) {
            addRow(ui->listProcs, L"");
            ++ui->lastProcRows;
        }
        std::wstring flags;
        if (p.isSelf) flags += L"self ";
        if (p.isForegroundTree) flags += L"foreground ";
        if (p.isProtected) flags += L"protected ";
        if (p.isExcluded) flags += L"excluded ";
        if (p.isBrowserRoot) flags += L"root ";
        if (!p.accessible) flags += L"inaccessible ";
        if (p.privateWsPartial) flags += L"priv-ws-partial ";
        if (flags.empty()) flags = L"-";

        std::wstring name = p.browserFamily ? familyName(p.browserFamily) : ws(p.exeName);
        name += L" ";
        name += ws(p.exeName);
        setCell(ui->listProcs, row, 0, name);
        setCell(ui->listProcs, row, 1, std::to_wstring(p.pid));
        setCell(ui->listProcs, row, 2, p.groupRootPid ? std::to_wstring(p.groupRootPid) : L"-");
        setCell(ui->listProcs, row, 3, pctW(p.cpuPercent));
        setCell(ui->listProcs, row, 4, bytesW(p.wsBytes));
        setCell(ui->listProcs, row, 5, p.privateWsBytes ? bytesW(p.privateWsBytes) : L"unavailable");
        setCell(ui->listProcs, row, 6, bytesW(p.privateBytes));
        setCell(ui->listProcs, row, 7, prioName(p.priorityClass));
        setCell(ui->listProcs, row, 8, flags);
        ++row;
    }

    // Log list (newest first, capped).
    ListView_DeleteAllItems(ui->listLog);
    const auto& hist = ui->app.executor().history();
    int shown = 0;
    for (auto it = hist.rbegin(); it != hist.rend() && shown < 200; ++it, ++shown) {
        const int r = addRow(ui->listLog, timeW(it->timestampMs));
        setCell(ui->listLog, r, 1, it->ok ? L"ok" : L"ERROR");
        setCell(ui->listLog, r, 2, ws(actionTypeName(it->type)));
        setCell(ui->listLog, r, 3, std::to_wstring(it->pid));
        setCell(ui->listLog, r, 4, ws(it->exeName));
        setCell(ui->listLog, r, 5, ws(it->detail));
    }

    // Protection lists.
    ListView_DeleteAllItems(ui->listProt);
    for (const auto& name : ui->app.config().protectedProcesses) {
        const int r = addRow(ui->listProt, ws(name));
        setCell(ui->listProt, r, 1, L"protected");
        setCell(ui->listProt, r, 2, L"never modified (built-in or config)");
    }
    for (const auto& name : ui->app.config().excludedProcesses) {
        const int r = addRow(ui->listProt, ws(name));
        setCell(ui->listProt, r, 1, L"excluded");
        setCell(ui->listProt, r, 2, L"user exclusion (never modified)");
    }

    // Labels.
    {
        const SysSample& s = ui->app.sys();
        std::wstring t = L"System: CPU " + pctW(s.cpuPercent) + L" | memory used " +
                         pctW(usedMemPercent(s.totalPhysBytes, s.availPhysBytes)) + L" (" +
                         bytesW(s.availPhysBytes) + L" free of " + bytesW(s.totalPhysBytes) +
                         L") | commit " + bytesW(s.commitTotalBytes) + L" of " +
                         bytesW(s.commitLimitBytes);
        SetWindowTextW(ui->lblSys, t.c_str());

        const auto self = ui->app.procmon().selfMetrics();
        t = L"Optimizer overhead: CPU " + pctW(self.cpuPercent) + L" | working set " +
            bytesW(self.wsBytes) + L" | private " + bytesW(self.privateBytes);
        SetWindowTextW(ui->lblSelf, t.c_str());

        const EngineDecision d = ui->app.lastDecision();
        t = std::wstring(L"Engine: pressure=") + ws(pressureLevelName(d.level)) + L" | profile=" +
            ws(profileName(ui->app.config().profile)) + L" | automation=" +
            (ui->app.config().automationEnabled ? L"on" : L"off") + L" | " + ws(d.rationale);
        SetWindowTextW(ui->lblEngine, t.c_str());

        const auto groups = ui->app.groups();
        int browserProcs = 0;
        for (const auto& p : ui->app.procs())
            if (p.browserFamily != 0) ++browserProcs;
        t = L"Browser processes: " + std::to_wstring(browserProcs) + L" process(es) in " +
            std::to_wstring(groups.size()) + L" group(s) | browser working set total " +
            bytesW(totalWs) + L" | CPU " + pctW(totalCpu);
        SetWindowTextW(ui->lblBrowsers, t.c_str());

        SetWindowTextW(ui->lblBench, ws(ui->benchStatus).c_str());
    }
}

void refreshAll(UiState* ui) {
    refreshLists(ui);
    std::wstring t = std::wstring(L"v") + widen(WRG_VERSION_PREFIX_FREE) + L" | data: " +
                     widen(ui->app.dataDirPath()) + L" | " +
                     (isElevated() ? L"elevated" : L"standard user (default)") +
                     L" | power-throttle API: " +
                     (CpuOptimizer::powerThrottleAvailable() ? L"available" : L"unavailable");
    SetWindowTextW(ui->status, t.c_str());
}

// ---- layout ----------------------------------------------------------------

void layout(UiState* ui) {
    RECT rc;
    GetClientRect(ui->hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int m = 8;
    const int col = w - 2 * m;

    int y = m;
    MoveWindow(ui->lblSys, m, y, col, 18, TRUE); y += 20;
    MoveWindow(ui->lblSelf, m, y, col, 18, TRUE); y += 20;
    MoveWindow(ui->lblEngine, m, y, col, 18, TRUE); y += 24;

    MoveWindow(ui->profile, m, y, 150, 24, TRUE);
    MoveWindow(ui->automation, m + 160, y, 170, 24, TRUE);
    MoveWindow(ui->wstrim, m + 340, y, 220, 24, TRUE);
    MoveWindow(ui->startup, m + 570, y, 180, 24, TRUE);
    MoveWindow(ui->save, w - m - 120, y, 120, 24, TRUE);
    y += 30;

    MoveWindow(ui->lblBrowsers, m, y, col, 18, TRUE); y += 20;

    const int listH = 250;
    MoveWindow(ui->listProcs, m, y, col, listH, TRUE);
    y += listH + 8;

    const int bw = (col - 5 * 6) / 6;
    MoveWindow(ui->btnOptimize, m, y, bw, 26, TRUE);
    MoveWindow(ui->btnRestore, m + (bw + 6), y, bw, 26, TRUE);
    MoveWindow(ui->btnRunBench, m + 2 * (bw + 6), y, bw, 26, TRUE);
    MoveWindow(ui->btnCancelBench, m + 3 * (bw + 6), y, bw, 26, TRUE);
    MoveWindow(ui->btnExport, m + 4 * (bw + 6), y, bw, 26, TRUE);
    MoveWindow(ui->btnOpen, m + 5 * (bw + 6), y, bw, 26, TRUE);
    y += 32;

    MoveWindow(ui->lblBench, m, y, col, 18, TRUE); y += 20;
    MoveWindow(ui->lblNote, m, y, col, 30, TRUE); y += 36;

    const int bottomH = h - y - 28 - m;
    if (bottomH > 60) {
        const int logW = (col * 58) / 100;
        MoveWindow(ui->listLog, m, y, logW - 4, bottomH, TRUE);
        MoveWindow(ui->listProt, m + logW, y, col - logW, bottomH, TRUE);
    }
    SendMessageW(ui->status, WM_SIZE, 0, 0);
}

// ---- benchmark thread ------------------------------------------------------

void startBenchmark(UiState* ui) {
    if (ui->benchRunning.exchange(true)) return;
    ui->benchCancel = false;
    ui->benchStartMs = nowMonotonicMs();
    ui->benchStatus = "Benchmark: running (baseline phase...)";
    EnableWindow(ui->btnOptimize, FALSE);
    EnableWindow(ui->btnRestore, FALSE);
    EnableWindow(ui->btnRunBench, FALSE);
    EnableWindow(ui->btnCancelBench, TRUE);

    const int totalSeconds = ui->app.config().benchWindowSeconds;
    std::thread([ui, totalSeconds]() {
        BenchmarkRunner runner(&ui->app.sysmon(), &ui->app.procmon(), &ui->app.executor(),
                               &ui->app.engineState());
        BenchResult r = runner.run(ui->app.config(), totalSeconds,
                                   ui->app.config().benchSampleMs, &ui->benchCancel);
        // Auto-save the CSV next to the config so results never vanish.
        std::string dir = ui->app.dataDirPath();
        std::string iso = r.startedIso;
        for (auto& ch : iso)
            if (ch == ':') ch = '-';
        r.csvPath = dir + "\\benchmark-" + iso + ".csv";
        std::string err;
        if (!writeBenchCsv(r, r.csvPath, &err)) r.csvPath.clear();
        auto* out = new BenchResult(std::move(r));
        PostMessageW(ui->hwnd, WM_APP_BENCH_DONE, 0, reinterpret_cast<LPARAM>(out));
    }).detach();
}

// ---- window proc -----------------------------------------------------------

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UiState* ui = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE: {
            ui = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            ui->hwnd = hwnd;
            ui->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            ui->lblSys = mkControl(hwnd, L"STATIC", L"System: ...", 0, IDC_LBL_SYS, ui->font);
            ui->lblSelf = mkControl(hwnd, L"STATIC", L"Optimizer: ...", 0, IDC_LBL_SELF, ui->font);
            ui->lblEngine = mkControl(hwnd, L"STATIC", L"Engine: ...", 0, IDC_LBL_ENGINE, ui->font);

            ui->profile = mkControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                    IDC_PROFILE, ui->font);
            SendMessageW(ui->profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Saver"));
            SendMessageW(ui->profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Balanced"));
            SendMessageW(ui->profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Performance"));
            SendMessageW(ui->profile, CB_SETCURSEL,
                         static_cast<WPARAM>(ui->app.config().profile), 0);

            ui->automation = mkControl(hwnd, L"BUTTON", L"Adaptive automation",
                                       BS_AUTOCHECKBOX | BS_TOP, IDC_AUTOMATION, ui->font);
            ui->wstrim = mkControl(hwnd, L"BUTTON", L"Allow automatic WS trim (policy)",
                                   BS_AUTOCHECKBOX | BS_TOP, IDC_WSTRIM, ui->font);
            ui->startup = mkControl(hwnd, L"BUTTON", L"Start with Windows",
                                    BS_AUTOCHECKBOX | BS_TOP, IDC_STARTUP, ui->font);
            ui->save = mkControl(hwnd, L"BUTTON", L"Save settings", BS_PUSHBUTTON, IDC_SAVE,
                                 ui->font);
            Button_SetCheck(ui->automation,
                            ui->app.config().automationEnabled ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(ui->wstrim,
                            ui->app.config().wsTrimAutoEnabled ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(ui->startup,
                            ui->app.config().startWithWindows ? BST_CHECKED : BST_UNCHECKED);

            ui->lblBrowsers = mkControl(hwnd, L"STATIC", L"Browser processes: ...", 0,
                                        IDC_LBL_BROWSERS, ui->font);

            DWORD listStyle = LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP;
            ui->listProcs = mkControl(hwnd, WC_LISTVIEWW, L"", listStyle, IDC_LIST_PROCS, ui->font);
            ListView_SetExtendedListViewStyle(ui->listProcs,
                                              LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            addColumn(ui->listProcs, 0, L"Process", 190);
            addColumn(ui->listProcs, 1, L"PID", 60);
            addColumn(ui->listProcs, 2, L"Group", 60);
            addColumn(ui->listProcs, 3, L"CPU%", 60);
            addColumn(ui->listProcs, 4, L"Working set", 95);
            addColumn(ui->listProcs, 5, L"Private WS", 95);
            addColumn(ui->listProcs, 6, L"Private bytes", 95);
            addColumn(ui->listProcs, 7, L"Priority", 100);
            addColumn(ui->listProcs, 8, L"Flags", 170);

            ui->btnOptimize = mkControl(hwnd, L"BUTTON", L"Optimize now", BS_PUSHBUTTON,
                                        IDC_OPTIMIZE, ui->font);
            ui->btnRestore = mkControl(hwnd, L"BUTTON", L"Restore all", BS_PUSHBUTTON,
                                       IDC_RESTORE, ui->font);
            ui->btnRunBench = mkControl(hwnd, L"BUTTON", L"Run benchmark", BS_PUSHBUTTON,
                                        IDC_RUNBENCH, ui->font);
            ui->btnCancelBench = mkControl(hwnd, L"BUTTON", L"Cancel benchmark", BS_PUSHBUTTON,
                                           IDC_CANCELBENCH, ui->font);
            EnableWindow(ui->btnCancelBench, FALSE);
            ui->btnExport = mkControl(hwnd, L"BUTTON", L"Export CSV", BS_PUSHBUTTON, IDC_EXPORT,
                                      ui->font);
            ui->btnOpen = mkControl(hwnd, L"BUTTON", L"Open data folder", BS_PUSHBUTTON,
                                    IDC_OPENFOLDER, ui->font);

            ui->lblBench = mkControl(hwnd, L"STATIC", L"Benchmark: idle", 0, IDC_LBL_BENCH, ui->font);
            ui->lblNote = mkControl(
                hwnd, L"STATIC",
                L"Native-only limits (v0.1.0-dev1): per-tab controls and network request "
                L"filtering are NOT available - a browser extension or proxy would be required. "
                L"Working-set trims are temporary; memory is reclaimed on access.",
                SS_LEFT, IDC_LBL_NOTE, ui->font);

            ui->listLog = mkControl(hwnd, WC_LISTVIEWW, L"", listStyle, IDC_LIST_LOG, ui->font);
            ListView_SetExtendedListViewStyle(ui->listLog, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            addColumn(ui->listLog, 0, L"Time", 70);
            addColumn(ui->listLog, 1, L"Result", 60);
            addColumn(ui->listLog, 2, L"Action", 150);
            addColumn(ui->listLog, 3, L"PID", 55);
            addColumn(ui->listLog, 4, L"Process", 110);
            addColumn(ui->listLog, 5, L"Detail", 320);

            ui->listProt = mkControl(hwnd, WC_LISTVIEWW, L"", listStyle, IDC_LIST_PROT, ui->font);
            ListView_SetExtendedListViewStyle(ui->listProt, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            addColumn(ui->listProt, 0, L"Process", 140);
            addColumn(ui->listProt, 1, L"Kind", 80);
            addColumn(ui->listProt, 2, L"Note", 170);

            ui->status = CreateStatusWindowW(WS_CHILD | WS_VISIBLE, L"", hwnd, IDC_STATUS);
            setFont(ui->status, ui->font);

            SetTimer(hwnd, kTimerId, static_cast<UINT>(ui->app.config().refreshMs), nullptr);
            layout(ui);
            // Prime monitors so the first tick has CPU deltas.
            ui->app.tick();
            refreshAll(ui);
            return 0;
        }
        case WM_SIZE:
            if (ui) layout(ui);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 920;
            mmi->ptMinTrackSize.y = 700;
            return 0;
        }
        case WM_TIMER:
            if (ui && wParam == kTimerId && !ui->benchRunning.load()) {
                ui->app.tick();
                refreshAll(ui);
            } else if (ui && ui->benchRunning.load()) {
                const uint64_t elapsed = (nowMonotonicMs() - ui->benchStartMs) / 1000ull;
                ui->benchStatus = "Benchmark: running... (" + std::to_string(elapsed) + "s elapsed)";
                SetWindowTextW(ui->lblBench, widen(ui->benchStatus).c_str());
            }
            return 0;
        case WM_COMMAND: {
            if (!ui) break;
            const int id = LOWORD(wParam);
            switch (id) {
                case IDC_PROFILE: {
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        const int sel = static_cast<int>(SendMessageW(ui->profile, CB_GETCURSEL, 0, 0));
                        if (sel >= 0 && sel <= 2) {
                            ui->app.setProfile(static_cast<Profile>(sel));
                            ui->benchStatus = "Profile set to " + std::string(profileName(static_cast<Profile>(sel))) + " (saved)";
                            refreshAll(ui);
                        }
                    }
                    break;
                }
                case IDC_AUTOMATION: {
                    const bool on = Button_GetCheck(ui->automation) == BST_CHECKED;
                    ui->app.setAutomation(on);
                    ui->benchStatus = std::string("Adaptive automation ") + (on ? "enabled" : "disabled") + " (saved)";
                    refreshAll(ui);
                    break;
                }
                case IDC_WSTRIM: {
                    const bool on = Button_GetCheck(ui->wstrim) == BST_CHECKED;
                    ui->app.setWsTrimAuto(on);
                    ui->benchStatus = std::string("Automatic working-set trim policy ") +
                                      (on ? "ENABLED (aggressive; bounded)" : "disabled (default)") + " (saved)";
                    refreshAll(ui);
                    break;
                }
                case IDC_STARTUP: {
                    const bool on = Button_GetCheck(ui->startup) == BST_CHECKED;
                    std::string err;
                    if (!ui->app.setStartupEnabled(on, &err)) {
                        Button_SetCheck(ui->startup, on ? BST_UNCHECKED : BST_CHECKED);
                        ui->benchStatus = "Startup registration failed: " + err;
                    } else {
                        ui->benchStatus = std::string("Start with Windows ") + (on ? "enabled" : "disabled") + " (saved)";
                    }
                    refreshAll(ui);
                    break;
                }
                case IDC_SAVE: {
                    std::string err;
                    ui->benchStatus = ui->app.saveSettings(&err) ? "Settings saved"
                                                                     : "Save failed: " + err;
                    refreshAll(ui);
                    break;
                }
                case IDC_OPTIMIZE: {
                    const EngineDecision d = ui->app.manualOptimize();
                    ui->benchStatus = "Manual optimize: " + d.rationale;
                    refreshAll(ui);
                    break;
                }
                case IDC_RESTORE: {
                    const int n = ui->app.restoreAll();
                    ui->benchStatus = "Restore all: " + std::to_string(n) + " setting(s) restored";
                    refreshAll(ui);
                    break;
                }
                case IDC_RUNBENCH:
                    startBenchmark(ui);
                    refreshAll(ui);
                    break;
                case IDC_CANCELBENCH:
                    ui->benchCancel = true;
                    ui->benchStatus = "Benchmark: cancelling...";
                    refreshAll(ui);
                    break;
                case IDC_EXPORT: {
                    if (!ui->lastBench || ui->lastBench->samples.empty()) {
                        MessageBoxW(hwnd,
                                    L"No benchmark result yet. Run a benchmark first (it is also "
                                    L"auto-saved to the data folder).",
                                    L"Export CSV", MB_ICONINFORMATION);
                        break;
                    }
                    wchar_t file[MAX_PATH] = L"benchmark.csv";
                    OPENFILENAMEW ofn;
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files\0*.*\0";
                    ofn.lpstrFile = file;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                    ofn.lpstrDefExt = L"csv";
                    if (GetSaveFileNameW(&ofn)) {
                        std::string err;
                        if (writeBenchCsv(*ui->lastBench, narrow(file), &err))
                            ui->benchStatus = "CSV exported: " + narrow(file);
                        else
                            ui->benchStatus = "CSV export failed: " + err;
                    }
                    refreshAll(ui);
                    break;
                }
                case IDC_OPENFOLDER: {
                    ShellExecuteW(hwnd, L"open", widen(ui->app.dataDirPath()).c_str(), nullptr,
                                  nullptr, SW_SHOWNORMAL);
                    break;
                }
            }
            return 0;
        }
        case WM_APP_BENCH_DONE: {
            if (!ui) break;
            ui->benchRunning = false;
            EnableWindow(ui->btnOptimize, TRUE);
            EnableWindow(ui->btnRestore, TRUE);
            EnableWindow(ui->btnRunBench, TRUE);
            EnableWindow(ui->btnCancelBench, FALSE);
            std::unique_ptr<BenchResult> result(reinterpret_cast<BenchResult*>(lParam));
            ui->lastBench = std::move(result);
            ui->benchStatus = "Benchmark: finished";
            if (!ui->lastBench->csvPath.empty())
                ui->benchStatus += " - CSV: " + ui->lastBench->csvPath;
            else
                ui->benchStatus += " - CSV auto-save FAILED";
            if (ui->lastBench->cancelled) ui->benchStatus += " (cancelled)";
            MessageBoxW(hwnd, widen(ui->lastBench->summaryText).c_str(), L"Benchmark results",
                        MB_ICONINFORMATION);
            refreshAll(ui);
            return 0;
        }
        case WM_DESTROY:
            if (ui) {
                KillTimer(hwnd, kTimerId);
                ui->benchCancel = true;
                // Best-effort restore on exit: never leave modified processes behind.
                if (!ui->benchRunning.load()) ui->app.restoreAll();
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int runDashboard(HINSTANCE hInstance, int nCmdShow) {
    // Programmatic DPI awareness: active even in builds without the embedded
    // manifest (the MSVC/CI build's manifest requests PerMonitorV2; this call
    // is a safe no-op there).
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    auto ui = std::make_unique<UiState>();
    std::string initErr;
    ui->app.init(&initErr);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WinResGovMainWindow";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    std::wstring title = L"" WRG_PRODUCT_NAME L" " WRG_VERSION_STRING;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(),
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                1060, 780, nullptr, nullptr, hInstance, ui.get());
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace wrg
