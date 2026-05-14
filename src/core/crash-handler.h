#pragma once

#include <windows.h>

namespace fast_explorer::core {

// Installs process-wide unhandled-exception, invalid-parameter, and purecall
// handlers that write a MiniDump to the crashdumps directory before the
// process terminates. Safe to call once near the start of wWinMain.
//
// Dump path resolution (in order):
//   1. %FAST_EXPLORER_PORTABLE_ROOT%\crashdumps\
//   2. %LOCALAPPDATA%\FastExplorer\crashdumps\
//
// Dump file name format:
//   fast-explorer-<PID>-YYYYMMDD-HHMMSS.dmp
//
// Once installed the handlers stay active for the lifetime of the process.
class CrashHandler {
 public:
  // Returns true if the handlers were installed. False indicates dbghelp could
  // not be initialized; the app continues running but without dumps.
  static bool install() noexcept;

  // Best-effort manual dump (useful for diagnostics or smoke testing).
  // Returns the dump path on success or empty wstring on failure.
  static const wchar_t* writeManualDump(const wchar_t* reason) noexcept;
};

}  // namespace fast_explorer::core
