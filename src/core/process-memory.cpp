#include "core/process-memory.h"

#include <psapi.h>

#include "core/ring-logger.h"

namespace fast_explorer::core {

namespace {

constexpr SIZE_T kMinWorkingSet = 8 * 1024 * 1024;
constexpr SIZE_T kMaxWorkingSet = 128 * 1024 * 1024;
constexpr DWORD kEmptyWorkingSetThrottleMs = 1000;
constexpr DWORD kLowMemoryReArmIntervalMs = 5000;

LARGE_INTEGER qpcNow() noexcept {
  LARGE_INTEGER v{};
  QueryPerformanceCounter(&v);
  return v;
}

double qpcMs(LARGE_INTEGER start, LARGE_INTEGER end) noexcept {
  static LARGE_INTEGER freq{};
  if (freq.QuadPart == 0) {
    QueryPerformanceFrequency(&freq);
  }
  if (freq.QuadPart == 0) {
    return 0.0;
  }
  return (static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0)
         / static_cast<double>(freq.QuadPart);
}

}  // namespace

ProcessMemoryService::ProcessMemoryService(RingLogger& logger) noexcept
    : logger_(logger) {}

ProcessMemoryService::~ProcessMemoryService() {
  stop();
}

bool ProcessMemoryService::start() {
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }

  const DWORD wsFlags = QUOTA_LIMITS_HARDWS_MIN_DISABLE
                      | QUOTA_LIMITS_HARDWS_MAX_DISABLE;
  if (!SetProcessWorkingSetSizeEx(GetCurrentProcess(),
                                  kMinWorkingSet,
                                  kMaxWorkingSet,
                                  wsFlags)) {
    logger_.warn(
        L"SetProcessWorkingSetSizeEx failed lastError=%lu", GetLastError());
  }

  notification_ = CreateMemoryResourceNotification(LowMemoryResourceNotification);
  if (notification_ == nullptr) {
    logger_.warn(
        L"CreateMemoryResourceNotification failed lastError=%lu", GetLastError());
    return false;
  }
  stopEvent_ = CreateEventW(nullptr, /*manualReset=*/TRUE, FALSE, nullptr);
  if (stopEvent_ == nullptr) {
    CloseHandle(notification_);
    notification_ = nullptr;
    return false;
  }
  running_.store(true, std::memory_order_release);
  notifier_ = std::thread(&ProcessMemoryService::notifierLoop, this);
  logger_.info(
      L"process-memory hints applied (min=8MB max=128MB advisory)");
  return true;
}

void ProcessMemoryService::stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  if (stopEvent_) {
    SetEvent(stopEvent_);
  }
  if (notifier_.joinable()) {
    notifier_.join();
  }
  if (stopEvent_) {
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
  }
  if (notification_) {
    CloseHandle(notification_);
    notification_ = nullptr;
  }
}

void ProcessMemoryService::notifyMinimized() noexcept {
  const LARGE_INTEGER now = qpcNow();
  int64_t prev = lastEmptyTicks_.load(std::memory_order_relaxed);
  // Throttle: skip if the last EmptyWorkingSet was less than 1 s ago.
  if (prev != 0) {
    LARGE_INTEGER prevTs{};
    prevTs.QuadPart = prev;
    if (qpcMs(prevTs, now) < kEmptyWorkingSetThrottleMs) {
      return;
    }
  }
  if (!lastEmptyTicks_.compare_exchange_strong(prev, now.QuadPart,
                                               std::memory_order_acq_rel)) {
    return;
  }

  SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
  if (EmptyWorkingSet(GetCurrentProcess())) {
    logger_.info(L"working set emptied on minimize");
  }
}

void ProcessMemoryService::notifyRestored() noexcept {
  SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_END);
}

void ProcessMemoryService::setLowMemoryCallback(LowMemoryCallback cb) {
  std::lock_guard lk(callbackMutex_);
  callback_ = std::move(cb);
}

SIZE_T ProcessMemoryService::workingSetBytes() noexcept {
  PROCESS_MEMORY_COUNTERS_EX info{};
  info.cb = sizeof(info);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),
                           sizeof(info))) {
    return info.WorkingSetSize;
  }
  return 0;
}

SIZE_T ProcessMemoryService::privateBytes() noexcept {
  PROCESS_MEMORY_COUNTERS_EX info{};
  info.cb = sizeof(info);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),
                           sizeof(info))) {
    return info.PrivateUsage;
  }
  return 0;
}

void ProcessMemoryService::notifierLoop() {
  // LowMemoryResourceNotification is a state-based object: it stays signaled
  // while the OS reports low memory. Without throttling, WaitForMultipleObjects
  // would spin. We invoke the callback once per low-memory transition and
  // then sleep for kLowMemoryReArmIntervalMs (interruptible by stopEvent_)
  // before checking again. QueryMemoryResourceNotification gives the real
  // current state so we know when the system has recovered.
  HANDLE waits[2] = { notification_, stopEvent_ };
  while (running_.load(std::memory_order_acquire)) {
    const DWORD rc = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (rc == WAIT_OBJECT_0 + 1) {
      break;
    }
    if (rc != WAIT_OBJECT_0) {
      continue;
    }

    BOOL stillLow = TRUE;
    if (QueryMemoryResourceNotification(notification_, &stillLow) && stillLow) {
      // Copy the callback under the lock and release before invoking
      // so user code does not run under our mutex.
      LowMemoryCallback localCb;
      {
        std::lock_guard lk(callbackMutex_);
        localCb = callback_;
      }
      logger_.warn(
          L"low memory notification fired; working set=%zu KB",
          workingSetBytes() / 1024);
      if (localCb) {
        localCb();
      }
    }

    // Wait either for the stop signal or for the throttle interval before
    // re-arming. If the system is still low after the wait, we will react
    // again; if it recovered, the WaitForMultipleObjects above blocks.
    WaitForSingleObject(stopEvent_, kLowMemoryReArmIntervalMs);
  }
}

}  // namespace fast_explorer::core
