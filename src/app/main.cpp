#include <windows.h>
#include <ole2.h>

#include "core/perf-tracker.h"
#include "ui/main-window.h"

namespace {

int runMessageLoop() {
  MSG msg{};
  bool firstMessageSeen = false;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!firstMessageSeen) {
      fast_explorer::core::recordPerf(
          fast_explorer::core::PerfTracker::EventId::AppInteractive);
      firstMessageSeen = true;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                     _In_opt_ HINSTANCE /*prev*/,
                     _In_ PWSTR /*cmdLine*/,
                     _In_ int showCommand) {
  fast_explorer::core::recordPerf(
      fast_explorer::core::PerfTracker::EventId::AppLaunchStart);

  HRESULT hr = OleInitialize(nullptr);
  if (FAILED(hr)) {
    return 1;
  }

  int exitCode = 1;
  {
    fast_explorer::ui::MainWindow window;
    if (window.create(instance, showCommand)) {
      exitCode = runMessageLoop();
    }
  }

  fast_explorer::core::recordPerf(
      fast_explorer::core::PerfTracker::EventId::AppShutdownStart);
  fast_explorer::core::PerfTracker::instance().dumpToDebugOutput();

  OleUninitialize();
  return exitCode;
}
