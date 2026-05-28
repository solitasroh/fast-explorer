#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <stdio.h>
#include <winsparkle.h>

#include <atomic>
#include <iterator>
#include <string>

#include "app/app-services.h"
#include "core/crash-handler.h"
#include "core/perf-tracker.h"
#include "core/process-memory.h"
#include "core/ring-logger.h"
#include "core/settings-store.h"
#include "ui/adapters/local-settings-store.h"
#include "winui_lite/chrome/dark-scrollbar-hook.h"
#include "winui_lite/chrome/dispinfo-histogram.h"
#include "winui_lite/chrome/theme-watcher.h"
#include "ui/main-window.h"
#include "ui/messages.h"
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

// Published by wWinMain after MainWindow::create returns. WinSparkle's
// shutdown callback (below) runs on its own background thread, so the
// HWND has to be read with atomic semantics. Nullptr means "no window
// yet" or "tearing down" — either way the callback is a no-op then.
std::atomic<HWND> g_mainWindowHwnd{nullptr};

// WinSparkle calls this on its worker thread when the user clicks
// "Install update" and it's about to launch the downloaded installer.
// Returning lets it spawn the installer; we use the call only as our
// cue to start tearing down the message loop so the .exe stops
// holding its own image file locked — otherwise the NSIS
// uninstall-before-install step fails to delete FastExplorer.exe and
// the install silently aborts. PostMessage is the documented
// thread-safe way to poke a window from a non-UI thread.
extern "C" void winSparkleShutdownRequest() {
  HWND hwnd = g_mainWindowHwnd.load(std::memory_order_acquire);
  if (hwnd != nullptr) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
}

// Pairs win_sparkle_init with win_sparkle_cleanup so the helper threads
// WinSparkle starts are joined deterministically before the COM apartment
// goes away or the process exits.
class WinSparkleScope {
 public:
  WinSparkleScope() {
    win_sparkle_set_appcast_url(FE_APPCAST_URL);
    win_sparkle_set_automatic_check_for_updates(1);
    if (FE_EDDSA_PUBLIC_KEY[0] != '\0') {
      win_sparkle_set_eddsa_public_key(FE_EDDSA_PUBLIC_KEY);
    }
    // Must be registered before win_sparkle_init so the worker thread
    // sees it from its first iteration.
    win_sparkle_set_shutdown_request_callback(&winSparkleShutdownRequest);
    win_sparkle_init();
    // Force a silent check on every launch. The automatic-check path
    // respects WinSparkle's 24h LastCheckTime debounce — fine for a
    // background heartbeat, but users expect to see a fresh release
    // dialog the next time they start the app, not 24h later. The
    // without_ui variant is silent on "no update", popping only when
    // a newer version is actually available.
    win_sparkle_check_update_without_ui();
  }
  ~WinSparkleScope() {
    g_mainWindowHwnd.store(nullptr, std::memory_order_release);
    win_sparkle_cleanup();
  }
  WinSparkleScope(const WinSparkleScope&) = delete;
  WinSparkleScope& operator=(const WinSparkleScope&) = delete;
};

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
                   fast_explorer::core::RingLogger& logger,
                   fast_explorer::ui::StallHistogram& histogram,
                   HWND hwnd, HACCEL accel) {
  using fast_explorer::core::PerfTracker;
  using fast_explorer::ui::classifyStall;
  MSG msg{};
  bool firstMessageSeen = false;
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  const uint64_t hz = static_cast<uint64_t>(freq.QuadPart);
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (accel && TranslateAcceleratorW(hwnd, accel, &msg)) {
      continue;
    }
    if (!firstMessageSeen) {
      perf.record(PerfTracker::EventId::AppInteractive);
      firstMessageSeen = true;
    }
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
    histogram.record(dispatchMicros);
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
  fast_explorer::core::recordMemoryProbe(services.perf());

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
      icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                  ICC_USEREX_CLASSES;
      InitCommonControlsEx(&icc);

      // Flip the process into "AllowDark" BEFORE the main window (and its
      // listviews) are created. Without this the LVS_OWNERDATA group
      // header band keeps the default light theme's dim caption colour
      // even after the listview itself has SetWindowTheme("DarkMode_*")
      // applied — group titles ("폴더" / "파일" / "오늘") become barely
      // readable on the dark background.
      fast_explorer::ui::enableProcessDarkMode();

      // Patch comctl32's delay-load thunk for uxtheme!OpenNcThemeData so
      // listview scrollbars render with the dark "Explorer::ScrollBar"
      // theme atlas instead of the white default that ships with
      // DarkMode_ItemsView. Must run after InitCommonControlsEx (so
      // comctl32 is loaded) and before any scrollbar HWND caches its
      // theme — i.e. before MainWindow::create instantiates listviews.
      // See ui/dark-scrollbar-hook.h for why this is the only known
      // path that does not regress hover/selection or paint artifacts.
      fast_explorer::ui::installDarkScrollBarHook();

      // WinSparkle scope must outlive the message loop; its dtor joins the
      // background update-check thread before COM teardown.
      WinSparkleScope sparkle;

      // Wire the SettingsStore port. The LocalSettingsStore adapter
      // binds to `initialState` so load() rehydrates that buffer in
      // place; save() (called below at shutdown) reads from
      // window.capturedSessionState() through the same adapter.
      const std::wstring settingsPath =
          fast_explorer::core::defaultSettingsPath();
      fast_explorer::core::SessionState initialState;
      fast_explorer::ui::adapters::LocalSettingsStore settingsAdapter(
          settingsPath, initialState);
      const bool settingsLoaded = settingsAdapter.load();
      if (settingsLoaded) {
        logger.info(L"settings loaded from %ls", settingsPath.c_str());
      }
      fast_explorer::ui::MainWindow window(services.memory(), services.perf());
      if (window.create(instance, showCommand)) {
        // Publish the HWND so WinSparkle's shutdown-request callback
        // (running on its worker thread) can PostMessage WM_CLOSE when
        // the user accepts an update.
        g_mainWindowHwnd.store(window.handle(), std::memory_order_release);
        window.applyInitialState(initialState);
        ACCEL accels[] = {
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'L',
             fast_explorer::ui::kAccelFocusAddress},
            {static_cast<BYTE>(FALT | FVIRTKEY), VK_LEFT,
             fast_explorer::ui::kAccelNavBack},
            {static_cast<BYTE>(FALT | FVIRTKEY), VK_RIGHT,
             fast_explorer::ui::kAccelNavForward},
            {static_cast<BYTE>(FALT | FVIRTKEY), VK_UP,
             fast_explorer::ui::kAccelNavUp},
            {static_cast<BYTE>(FVIRTKEY), VK_F5,
             fast_explorer::ui::kAccelRefresh},
            {static_cast<BYTE>(FVIRTKEY), VK_DELETE,
             fast_explorer::ui::kAccelDelete},
            {static_cast<BYTE>(FVIRTKEY), VK_F2,
             fast_explorer::ui::kAccelRename},
            {static_cast<BYTE>(FCONTROL | FSHIFT | FVIRTKEY), L'N',
             fast_explorer::ui::kAccelCreateFolder},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'1',
             fast_explorer::ui::kAccelLayoutSingle},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'2',
             fast_explorer::ui::kAccelLayoutDual},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'3',
             fast_explorer::ui::kAccelLayoutTri},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'4',
             fast_explorer::ui::kAccelLayoutQuad},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'F',
             fast_explorer::ui::kAccelFilter},
            {static_cast<BYTE>(FALT | FVIRTKEY), L'V',
             fast_explorer::ui::kAccelLayoutVerticalToggle},
            {static_cast<BYTE>(FALT | FVIRTKEY), L'H',
             fast_explorer::ui::kAccelLayoutHorizontalToggle},
            {static_cast<BYTE>(FVIRTKEY), VK_F6,
             fast_explorer::ui::kAccelPaneSwitch},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'C',
             fast_explorer::ui::kAccelCopy},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'X',
             fast_explorer::ui::kAccelCut},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'V',
             fast_explorer::ui::kAccelPaste},
            {static_cast<BYTE>(FCONTROL | FSHIFT | FVIRTKEY), L'C',
             fast_explorer::ui::kAccelCopyPath},
            {static_cast<BYTE>(FCONTROL | FVIRTKEY), L'A',
             fast_explorer::ui::kAccelSelectAll},
            {static_cast<BYTE>(FALT | FVIRTKEY), VK_RETURN,
             fast_explorer::ui::kAccelProperties},
            {static_cast<BYTE>(FALT | FVIRTKEY), L'M',
             fast_explorer::ui::kAccelToolMenu},
        };
        HACCEL hAccel =
            CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));
        // --open takes priority over a persisted last_path so a
        // diagnostic invocation does not silently land on the
        // user's previous folder.
        if (!openPath.empty()) {
          if (!window.openFolder(openPath)) {
            logger.error(L"--open path invalid: %ls", openPath.c_str());
          } else {
            logger.info(L"--open: enumerating %ls", openPath.c_str());
          }
        } else if (!initialState.panePaths[0].empty()) {
          if (!window.openFolder(initialState.panePaths[0])) {
            logger.warn(L"session restore: pane[0] path invalid (%ls)",
                        initialState.panePaths[0].c_str());
          }
        }
        // Layout restore runs after lastPath is open so the second
        // pane's fallback ("open on active pane's folder" when no
        // secondPath was persisted) lands on a meaningful location.
        window.restoreLayoutFromSession(initialState);
        fast_explorer::ui::StallHistogram stallHistogram;
        exitCode = runMessageLoop(services.perf(), logger, stallHistogram,
                                  window.handle(), hAccel);
        auto loggerSink =
            [](const wchar_t* line, void* userData) {
              auto* lg =
                  static_cast<fast_explorer::core::RingLogger*>(userData);
              lg->info(L"%ls", line);
            };
        fast_explorer::ui::dumpStallHistogram(stallHistogram, loggerSink,
                                              &logger);
        if (const auto* dh = window.dispInfoHistogram()) {
          fast_explorer::ui::dumpDispInfoHistogram(*dh, loggerSink, &logger);
        }
        if (hAccel) {
          DestroyAcceleratorTable(hAccel);
        }
        if (!settingsPath.empty()) {
          // Re-bind the adapter to the captured state for save.
          // capturedSessionState() returns const& to discourage host-
          // side mutation, but save() only reads through the
          // reference so the const_cast here is sound (and explicit
          // about the read-only intent).
          fast_explorer::ui::adapters::LocalSettingsStore saveAdapter(
              settingsPath,
              const_cast<fast_explorer::core::SessionState&>(
                  window.capturedSessionState()));
          if (!saveAdapter.save()) {
            logger.warn(L"settings save failed: %ls", settingsPath.c_str());
          }
        }
      } else {
        logger.error(L"MainWindow::create failed (lastError=%lu)", GetLastError());
      }
    } else {
      logger.error(L"OleInitialize failed (hr=0x%08lX)", ole.hr());
    }
    services.perf().record(PerfTracker::EventId::AppShutdownStart);
    fast_explorer::core::recordMemoryProbe(services.perf());
    services.perf().dumpToCallback(&perfLineToLogger, &logger);
    logger.info(L"working set @ shutdown = %zu KB",
                fast_explorer::core::ProcessMemoryService::workingSetBytes() / 1024);
    logger.info(L"app shutdown (exitCode=%d)", exitCode);
  }

  // AppServices dtor stops memory -> uninstall crash -> stop logger.
  return exitCode;
}
