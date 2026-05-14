#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <stdio.h>

#include <string>

#include "app/app-services.h"
#include "core/crash-handler.h"
#include "core/perf-tracker.h"
#include "core/process-memory.h"
#include "core/ring-logger.h"
#include "ui/main-window.h"
#include "ui/stall-probe.h"

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

void perfLineToLogger(const wchar_t* line, void* userData) {
  auto* logger = static_cast<fast_explorer::core::RingLogger*>(userData);
  logger->info(L"%ls", line);
}

void logStall(fast_explorer::core::RingLogger& logger,
              fast_explorer::core::PerfTracker& perf,
              fast_explorer::ui::StallLevel level,
              uint64_t gapMicros, UINT message) {
  using fast_explorer::core::RingLogger;
  using fast_explorer::ui::StallLevel;
  if (level == StallLevel::None) {
    return;
  }
  using LoggerFn = void (RingLogger::*)(const wchar_t*, ...) noexcept;
  const LoggerFn fn = (level == StallLevel::Info)   ? &RingLogger::info
                    : (level == StallLevel::Warn)   ? &RingLogger::warn
                    :                                  &RingLogger::error;
  (logger.*fn)(L"ui stall %llu us (msg=0x%04X)%ls",
               static_cast<unsigned long long>(gapMicros), message,
               level == StallLevel::Error ? L" -- perf dump" : L"");
  if (level == StallLevel::Error) {
    perf.dumpToCallback(&perfLineToLogger, &logger);
  }
}

int runMessageLoop(fast_explorer::core::PerfTracker& perf,
                   fast_explorer::core::RingLogger& logger) {
  using fast_explorer::core::PerfTracker;
  using fast_explorer::ui::classifyStall;
  MSG msg{};
  bool firstMessageSeen = false;
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  const uint64_t hz = static_cast<uint64_t>(freq.QuadPart);
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!firstMessageSeen) {
      perf.record(PerfTracker::EventId::AppInteractive);
      firstMessageSeen = true;
    }
    // Stall is measured per dispatch: anything outside the
    // Translate+Dispatch pair (the GetMessage blocking wait, our own
    // post-dispatch logging) should not count against the budget.
    LARGE_INTEGER t0{};
    QueryPerformanceCounter(&t0);
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    LARGE_INTEGER t1{};
    QueryPerformanceCounter(&t1);

    const uint64_t dispatchTicks =
        static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
    const uint64_t dispatchMicros =
        hz == 0 ? 0 : (dispatchTicks * 1'000'000ULL) / hz;
    logStall(logger, perf, classifyStall(dispatchMicros), dispatchMicros,
             msg.message);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                     _In_opt_ HINSTANCE /*prev*/,
                     _In_ PWSTR cmdLine,
                     _In_ int showCommand) {
  using fast_explorer::app::AppServices;
  using fast_explorer::core::CrashHandler;
  using fast_explorer::core::PerfTracker;

  AppServices services;
  services.perf().record(PerfTracker::EventId::AppLaunchStart);

  if (!services.start()) {
    OutputDebugStringW(L"[fast-explorer] AppServices::start failed\n");
  }

  auto& logger = services.logger();
  logger.info(L"app launched (showCmd=%d)", showCommand);
  if (services.memoryOk()) {
    logger.info(L"working set @ start = %zu KB",
                fast_explorer::core::ProcessMemoryService::workingSetBytes() / 1024);
  }

  // Diagnostic switches parsed via CommandLineToArgvW.
  std::wstring openPath;
  if (cmdLine != nullptr) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    bool manualTest = false;
    bool throwTest = false;
    bool invalidTest = false;
    for (int i = 0; argv && i < argc; ++i) {
      if (wcscmp(argv[i], L"--crash-test") == 0) {
        manualTest = true;
      } else if (wcscmp(argv[i], L"--crash-test=throw") == 0) {
        throwTest = true;
      } else if (wcscmp(argv[i], L"--crash-test=invalid") == 0) {
        invalidTest = true;
      } else if (wcscmp(argv[i], L"--open") == 0 && i + 1 < argc) {
        openPath = argv[i + 1];
        ++i;
      }
    }
    if (argv) {
      LocalFree(argv);
    }
    if (manualTest) {
      const wchar_t* path = CrashHandler::writeManualDump(L"--crash-test");
      if (path && path[0] != L'\0') {
        logger.info(L"manual dump created: %ls", path);
      } else {
        logger.error(L"manual dump failed");
      }
      return 0;  // AppServices dtor unwinds cleanly.
    }
    if (invalidTest) {
      printf(nullptr);  // exercises _set_invalid_parameter_handler
      return 0;
    }
    if (throwTest) {
      volatile int* p = nullptr;
      *p = 42;
      // Unreached; SetUnhandledExceptionFilter takes over.
    }
  }

  int exitCode = 1;
  {
    OleScope ole;
    if (ole.ok()) {
      INITCOMMONCONTROLSEX icc{};
      icc.dwSize = sizeof(icc);
      icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
      InitCommonControlsEx(&icc);

      fast_explorer::ui::MainWindow window(services.memory());
      if (window.create(instance, showCommand)) {
        if (!openPath.empty()) {
          if (!window.openFolder(openPath)) {
            logger.error(L"--open path invalid: %ls", openPath.c_str());
          } else {
            logger.info(L"--open: enumerating %ls", openPath.c_str());
          }
        }
        exitCode = runMessageLoop(services.perf(), logger);
      } else {
        logger.error(L"MainWindow::create failed (lastError=%lu)", GetLastError());
      }
    } else {
      logger.error(L"OleInitialize failed (hr=0x%08lX)", ole.hr());
    }
    services.perf().record(PerfTracker::EventId::AppShutdownStart);
    services.perf().dumpToCallback(&perfLineToLogger, &logger);
    logger.info(L"working set @ shutdown = %zu KB",
                fast_explorer::core::ProcessMemoryService::workingSetBytes() / 1024);
    logger.info(L"app shutdown (exitCode=%d)", exitCode);
  }

  // AppServices dtor stops memory -> uninstall crash -> stop logger.
  return exitCode;
}
