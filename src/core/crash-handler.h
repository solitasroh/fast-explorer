#pragma once

#include <windows.h>

namespace fast_explorer::core {

class PerfTracker;

// Installs process-wide unhandled-exception, invalid-parameter, and purecall
// handlers that write a MiniDump to the crashdumps directory before the
// process terminates.
//
// The PerfTracker reference is captured (as a non-owning pointer) so the
// minidump can embed the event ring as a user stream. Lifetime contract:
// the caller must keep the tracker alive until CrashHandler::uninstall().
//
// Dump path resolution (in order):
//   1. %FAST_EXPLORER_PORTABLE_ROOT%\crashdumps\
//   2. %LOCALAPPDATA%\FastExplorer\crashdumps\
//
// Dump file name format:
//   fast-explorer-<PID>-YYYYMMDD-HHMMSS.dmp
class CrashHandler {
 public:
  // Returns true on success. install() is idempotent within the same process
  // (further calls reuse the existing handlers and replace the PerfTracker
  // pointer with the new one). Returns false only if the dump directory
  // cannot be resolved.
  static bool install(PerfTracker& perf) noexcept;

  // Clears the PerfTracker pointer and resets installed state so a subsequent
  // install() can re-arm. The OS-level handlers stay registered (Windows
  // exposes no clean "remove" path for these), but they short-circuit once
  // the tracker pointer is null.
  static void uninstall() noexcept;

  // Best-effort manual dump (useful for diagnostics or smoke testing).
  // Returns the dump path on success or empty wstring on failure.
  static const wchar_t* writeManualDump(const wchar_t* reason) noexcept;
};

}  // namespace fast_explorer::core
