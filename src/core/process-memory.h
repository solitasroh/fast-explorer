#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace fast_explorer::core {

// ProcessMemoryService applies OS-level working-set hints and watches
// the system low-memory notification so the app can drop bounded caches
// when Windows is under pressure.
//
// Behavior:
//  - SetProcessWorkingSetSizeEx hint at startup (min 8 MB, max 128 MB, both
//    HARDWS_*_DISABLE so they are advisory).
//  - notifyMinimized() / notifyRestored() so the main window can hook
//    WM_SIZE and let us call EmptyWorkingSet at the right moments.
//  - background thread that waits on the LowMemoryResourceNotification
//    handle and invokes the registered low-memory callback.
//
// Cache providers should call setLowMemoryCallback() with their evict hook.
class RingLogger;

class ProcessMemoryService {
 public:
  using LowMemoryCallback = std::function<void()>;

  // Logger reference is required for diagnostic output. The service does
  // not extend the logger's lifetime; the owner (typically AppServices)
  // is responsible for ordering start/stop calls correctly.
  explicit ProcessMemoryService(RingLogger& logger) noexcept;
  ~ProcessMemoryService();
  ProcessMemoryService(const ProcessMemoryService&) = delete;
  ProcessMemoryService& operator=(const ProcessMemoryService&) = delete;

  // Idempotent. Returns true if hints/notifier are installed.
  bool start();

  // Stops the background thread and clears callbacks. Safe to call multiple
  // times.
  void stop();

  // Should be called from the UI thread when the main window enters/leaves
  // the minimized state.
  void notifyMinimized() noexcept;
  void notifyRestored() noexcept;

  // Cache hook. Replaces any previous callback. Pass an empty
  // std::function to clear. The notifier thread may already be
  // inside the previous callback when this returns; to quiesce
  // call stop() (which joins the notifier). The notifier copies
  // the current callback under the internal mutex before
  // releasing the lock and invoking it, so a captured shared
  // state (HWND, this pointer) stays valid for the duration of
  // the in-flight invocation even after this setter returns.
  void setLowMemoryCallback(LowMemoryCallback cb);

  // Snapshot helpers for diagnostics / bench gates.
  static SIZE_T workingSetBytes() noexcept;
  static SIZE_T privateBytes() noexcept;

 private:
  void notifierLoop();

  RingLogger& logger_;
  std::atomic<bool> running_{false};
  // callback_ is std::function which is not trivially atomic;
  // protect with callbackMutex_. Both setLowMemoryCallback and
  // the notifier loop acquire it briefly.
  std::mutex callbackMutex_;
  LowMemoryCallback callback_;
  std::atomic<int64_t> lastEmptyTicks_{0};  // QPC ticks; throttles EmptyWorkingSet
  HANDLE notification_ = nullptr;
  HANDLE stopEvent_ = nullptr;
  std::thread notifier_;
};

}  // namespace fast_explorer::core
