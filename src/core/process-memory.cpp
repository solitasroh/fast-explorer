#include "core/process-memory.h"

#include <psapi.h>

#include "core/ring-logger.h"

namespace fast_explorer::core {

namespace {

constexpr SIZE_T kMinWorkingSet = 8 * 1024 * 1024;    // 8 MB hint
constexpr SIZE_T kMaxWorkingSet = 128 * 1024 * 1024;  // 128 MB hint
constexpr DWORD kPollIntervalMs = INFINITE;  // wake only on event

}  // namespace

ProcessMemoryService& ProcessMemoryService::instance() {
  static ProcessMemoryService singleton;
  return singleton;
}

ProcessMemoryService::~ProcessMemoryService() {
  stop();
}

bool ProcessMemoryService::start() {
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }

  // Working set hint: advisory only thanks to the HARDWS_*_DISABLE flags.
  const DWORD wsFlags = QUOTA_LIMITS_HARDWS_MIN_DISABLE
                      | QUOTA_LIMITS_HARDWS_MAX_DISABLE;
  if (!SetProcessWorkingSetSizeEx(GetCurrentProcess(),
                                  kMinWorkingSet,
                                  kMaxWorkingSet,
                                  wsFlags)) {
    RingLogger::instance().warn(
        L"SetProcessWorkingSetSizeEx failed lastError=%lu", GetLastError());
    // Continue; not fatal.
  }

  notification_ = CreateMemoryResourceNotification(LowMemoryResourceNotification);
  if (notification_ == nullptr) {
    RingLogger::instance().warn(
        L"CreateMemoryResourceNotification failed lastError=%lu", GetLastError());
    return false;
  }
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stopEvent_ == nullptr) {
    CloseHandle(notification_);
    notification_ = nullptr;
    return false;
  }
  running_.store(true, std::memory_order_release);
  notifier_ = std::thread(&ProcessMemoryService::notifierLoop, this);
  RingLogger::instance().info(
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
  // EmptyWorkingSet returns pages immediately. Throttling (1 Hz) is deferred
  // until we have multiple minimize events to coalesce.
  if (EmptyWorkingSet(GetCurrentProcess())) {
    RingLogger::instance().info(L"working set emptied on minimize");
  }
}

void ProcessMemoryService::notifyRestored() noexcept {
  // No-op for MVP; left here so the WM_SIZE hook has a paired call.
}

void ProcessMemoryService::setLowMemoryCallback(LowMemoryCallback cb) noexcept {
  callback_.store(cb, std::memory_order_release);
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
  HANDLE waits[2] = { notification_, stopEvent_ };
  while (running_.load(std::memory_order_acquire)) {
    const DWORD rc = WaitForMultipleObjects(2, waits, FALSE, kPollIntervalMs);
    if (rc == WAIT_OBJECT_0) {
      // Low memory: invoke callback and log.
      LowMemoryCallback cb = callback_.load(std::memory_order_acquire);
      RingLogger::instance().warn(
          L"low memory notification fired; working set=%zu KB",
          workingSetBytes() / 1024);
      if (cb) {
        cb();
      }
      // The notification handle is auto-reset after a successful wait; re-arm
      // happens automatically when the system leaves the low-memory state.
    } else if (rc == WAIT_OBJECT_0 + 1) {
      break;
    } else {
      break;
    }
  }
}

}  // namespace fast_explorer::core
