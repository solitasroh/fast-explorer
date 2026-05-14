#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace fast_explorer::core {

// PerfTracker records ordered timestamped events using QueryPerformanceCounter.
//
// Concurrency model: MPSC (multiple producers via record(), single consumer
// via dumpToDebugOutput() at process shutdown). Producers publish to a
// per-slot sequence counter so that a reader can distinguish a torn slot
// (write-in-progress) from a fully published slot. See record() / readSlot().
//
// MVP scope: ~10k event capacity, dump to OutputDebugString on shutdown.
// Async file writer integration is deferred until RingLogger lands.
class PerfTracker {
 public:
  static constexpr size_t kCapacity = 10'000;

  enum class EventId : uint16_t {
    AppLaunchStart = 1,
    AppInteractive = 2,
    AppShutdownStart = 3,
    PaneOpenStart = 4,
    PaneFirstBatch = 5,
    // sort/icon events allocated in later milestones
  };

  struct Event {
    int64_t qpcTicks;        // QueryPerformanceCounter value
    uint64_t auxiliary;      // event-specific: pane id, item count, etc.
    uint32_t threadId;       // GetCurrentThreadId
    EventId id;              // event type
    uint16_t reserved;
  };
  static_assert(sizeof(Event) == 24, "Event packed to 24 bytes for cache locality");

  PerfTracker() noexcept;
  ~PerfTracker() = default;
  PerfTracker(const PerfTracker&) = delete;
  PerfTracker& operator=(const PerfTracker&) = delete;

  // Lock-free emit. Safe from any thread. Producers publish ordering with
  // release stores so the consumer can observe a consistent slot.
  void record(EventId id, uint64_t auxiliary = 0) noexcept;

  // Convert QPC ticks to milliseconds using cached frequency.
  double ticksToMs(int64_t ticks) const noexcept;

  // Dump captured events to OutputDebugString. Caller must guarantee that
  // no producers are still running (e.g. after the message loop returns).
  void dumpToDebugOutput() const;

  // Sink-style dump used so the tracker stays free of logger / file deps.
  // Callback receives one already-formatted UTF-16 line per published slot.
  // Caller must guarantee no concurrent producers, same as dumpToDebugOutput.
  using LineSink = void (*)(const wchar_t* line, void* userData);
  void dumpToCallback(LineSink sink, void* userData) const;

  // Snapshot count of recorded events (clamped to kCapacity if wrapped).
  size_t recordedCount() const noexcept;

  // Raw access for crash dump user-stream attachment. Returns a pointer to
  // the per-slot ring array and its element count. The caller is expected
  // to copy as many bytes as needed during MiniDumpWriteDump.
  const void* rawSlotBuffer() const noexcept { return slots_; }
  size_t rawSlotBufferBytes() const noexcept { return sizeof(slots_); }

 private:
  // Per-slot publication sequence. record() reserves a slot N by incrementing
  // cursor_, then writes seq = N*2+1 (odd = in progress), payload, seq = N*2+2
  // (even = published) with release ordering. dumpToDebugOutput() reads with
  // acquire ordering and skips slots whose seq is still odd.
  struct PublishedSlot {
    std::atomic<uint64_t> seq{0};
    Event event{};
  };

  int64_t qpcFrequency_ = 0;
  std::atomic<uint64_t> cursor_{0};
  PublishedSlot slots_[kCapacity]{};
};

}  // namespace fast_explorer::core
