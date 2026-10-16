// Windows Resource Governor — single source of truth for version metadata.
// String form is "0.1.0-dev1" (prefix-free). Windows VERSIONINFO numeric form is
// the 4-part tuple 0.1.0.1 (see DESIGN_LOG.md entry D11); the mapping is:
//   0.1.0-dev1 -> 0.1.0.1 (dev1 encoded as the 4th revision component).
#pragma once

#define WRG_VERSION_PREFIX_FREE "0.1.0-dev1"
#define WRG_VERSION_STRING      "v0.1.0-dev1"
#define WRG_VERSION_MAJOR       0
#define WRG_VERSION_MINOR       1
#define WRG_VERSION_PATCH       0
#define WRG_VERSION_BUILD       1
#define WRG_VERSION_TUPLE       0,1,0,1

#define WRG_PRODUCT_NAME        "Windows Resource Governor"
#define WRG_EXE_NAME            "WinResGov.exe"
#define WRG_COMPANY_NAME        "coderunknow"
#define WRG_COPYRIGHT           "Copyright (c) 2026 coderunknow"
