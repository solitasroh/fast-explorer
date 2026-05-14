#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace fast_explorer::core {

// PerfTracker records ordered timestamped events using QueryPerformanceCounter.
// Lock-free SPMC ring (single writer per event-id is not enforced; multiple
// threads may emit, contention resolved via atomic fetch_add on cursor).
//
// MVP scope: ~10k event capacity, dump to OutputDebugString on shutdown.
// Async file writer integration: deferred until RingLogger lands.
class PerfTracker {
 public:
  static constexpr size_t kCapacity = 10'000;

  enum class EventId : uint16_t {
    AppLaunchStart = 1,
    AppInteractive = 2,
    AppShutdownStart = 3,
    // pane/sort/icon events allocated in later milestones
  };

  struct Event {
    int64_t qpcTicks;        // QueryPerformanceCounter value
    uint64_t auxiliary;      // event-specific: pane id, item count, etc.
    uint32_t threadId;       // GetCurrentThreadId
    EventId id;              // event type
    uint16_t reserved;
  };
  static_assert(sizeof(Event) == 24);

  static PerfTracker& instance();

  // Lock-free emit. Safe from any thread.
  void record(EventId id, uint64_t auxiliary = 0) noexcept;

  // Convert QPC ticks to milliseconds using cached frequency.
  double ticksToMs(int64_t ticks) const noexcept;

  // Dump captured events to OutputDebugString (called from main thread on shutdown).
  void dumpToDebugOutput() const;

  // Snapshot count of recorded events (clamped to kCapacity if wrapped).
  size_t recordedCount() const noexcept;

 private:
  PerfTracker();
  PerfTracker(const PerfTracker&) = delete;
  PerfTracker& operator=(const PerfTracker&) = delete;

  int64_t qpcFrequency_ = 0;
  std::atomic<uint64_t> cursor_{0};
  Event events_[kCapacity]{};
};

// Convenience inline helper to keep call sites short.
inline void recordPerf(PerfTracker::EventId id, uint64_t aux = 0) noexcept {
  PerfTracker::instance().record(id, aux);
}

}  // namespace fast_explorer::core
