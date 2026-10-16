// Native Win32 dashboard (brief section 8). Every control performs a real,
// supported operation; unavailable capabilities are labeled explicitly.
#pragma once

#include <windows.h>

namespace wrg {

// Creates the dashboard window and runs the message loop. Returns the exit code.
int runDashboard(HINSTANCE hInstance, int nCmdShow);

}  // namespace wrg
