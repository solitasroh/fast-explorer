#include <windows.h>
#include <ole2.h>

#include "core/crash-handler.h"
#include "core/perf-tracker.h"
#include "core/process-memory.h"
#include "core/ring-logger.h"
#include "ui/main-window.h"

namespace {

// Pairs OleInitialize with OleUninitialize so every early-return path on the
// way through wWinMain releases the STA apartment correctly.
class OleScope {
 public:
  OleScope() noexcept : hr_(OleInitialize(nullptr)) {}
  ~OleScope() {
    if (SUCCEEDED(hr_)) {
      OleUninitialize();
    }
  }
  OleScope(const OleScope&) = delete;
  OleScope& operator=(const OleScope&) = delete;

  bool ok() const noexcept { return SUCCEEDED(hr_); }
  HRESULT hr() const noexcept { return hr_; }

 private:
  HRESULT hr_;
};

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

namespace {

void perfLineToLogger(const wchar_t* line, void* userData) {
  auto* logger = static_cast<fast_explorer::core::RingLogger*>(userData);
  logger->info(L"%s", line);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                     _In_opt_ HINSTANCE /*prev*/,
                     _In_ PWSTR cmdLine,
                     _In_ int showCommand) {
  using fast_explorer::core::PerfTracker;
  using fast_explorer::core::ProcessMemoryService;
  using fast_explorer::core::RingLogger;
  using fast_explorer::core::recordPerf;

  recordPerf(PerfTracker::EventId::AppLaunchStart);

  RingLogger& logger = RingLogger::instance();
  const bool loggerOk = logger.start();
  if (loggerOk) {
    logger.info(L"app launched (showCmd=%d)", showCommand);
  } else {
    OutputDebugStringW(L"[fast-explorer] RingLogger failed to start\n");
  }

  if (!fast_explorer::core::CrashHandler::install()) {
    logger.warn(L"crash handler failed to install");
  }

  ProcessMemoryService& memService = ProcessMemoryService::instance();
  if (!memService.start()) {
    logger.warn(L"process-memory service failed to start");
  } else {
    logger.info(L"working set @ start = %zu KB",
                ProcessMemoryService::workingSetBytes() / 1024);
  }

  // Diagnostic switch: --crash-test writes a manual dump and exits.
  if (cmdLine != nullptr && wcsstr(cmdLine, L"--crash-test") != nullptr) {
    const wchar_t* path = fast_explorer::core::CrashHandler::writeManualDump(L"--crash-test");
    if (path && path[0] != L'\0') {
      logger.info(L"manual dump created: %s", path);
    } else {
      logger.error(L"manual dump failed");
    }
    memService.stop();
    logger.stop();
    return 0;
  }

  int exitCode = 1;
  {
    OleScope ole;
    if (ole.ok()) {
      fast_explorer::ui::MainWindow window;
      if (window.create(instance, showCommand)) {
        exitCode = runMessageLoop();
      } else {
        logger.error(L"MainWindow::create failed (lastError=%lu)", GetLastError());
      }
    } else {
      logger.error(L"OleInitialize failed (hr=0x%08lX)", ole.hr());
    }
    recordPerf(PerfTracker::EventId::AppShutdownStart);
    PerfTracker::instance().dumpToCallback(&perfLineToLogger, &logger);
    logger.info(L"working set @ shutdown = %zu KB",
                ProcessMemoryService::workingSetBytes() / 1024);
    logger.info(L"app shutdown (exitCode=%d)", exitCode);
  }

  memService.stop();
  logger.stop();
  return exitCode;
}
