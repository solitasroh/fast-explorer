#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace fast_explorer::core {

// RingLogger writes diagnostic text to %LOCALAPPDATA%\FastExplorer\logs\fast-explorer-YYYYMMDD.log
// (or the portable-mode override) via a dedicated background thread.
//
// Concurrency model: MPSC ring with per-slot publication sequence. Producers
// call log() from any thread; the writer thread drains published slots and
// performs WriteFile. INFO+ messages also signal the writer to flush right
// away; lower levels are deferred until the timed flush tick.
//
// Backpressure: if the producer cursor moves more than kSlotCount ahead of
// the reader cursor, the message is routed to emitFallback() (drop + ODS)
// and the dropped counter is incremented. This prevents producers from
// silently overwriting in-flight slots.
//
// Shutdown ordering: stop() first signals the stop event so the writer drains
// all currently-published slots, then flips running_ off and joins. Producers
// observing running_=false after stop() route to the fallback sink.
class RingLogger {
 public:
  enum class Level : uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
  };

  static constexpr size_t kSlotCount = 512;
  static constexpr size_t kSlotBytes = 256;
  static constexpr size_t kMaxMessageBytes = kSlotBytes - 24;  // header overhead

  RingLogger();
  ~RingLogger();
  RingLogger(const RingLogger&) = delete;
  RingLogger& operator=(const RingLogger&) = delete;

  bool start();
  void stop();

  void log(Level level, const wchar_t* fmt, ...) noexcept;
  void info(const wchar_t* fmt, ...) noexcept;
  void warn(const wchar_t* fmt, ...) noexcept;
  void error(const wchar_t* fmt, ...) noexcept;
  void fatal(const wchar_t* fmt, ...) noexcept;

  // Number of messages dropped because the ring was full. For diagnostics.
  uint64_t droppedCount() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }

 private:
  struct Slot {
    std::atomic<uint64_t> seq{0};   // odd = in progress, even = published
    FILETIME timestamp{};
    uint32_t threadId{0};
    Level level{Level::Trace};
    uint8_t reserved[3]{};
    uint16_t messageLength{0};       // UTF-8 byte count (<= kMaxMessageBytes)
    char message[kMaxMessageBytes]{};
  };

  void writerLoop();
  bool openLogFile();
  void writeSlot(const Slot& slot);
  void writeAllBytes(const void* data, size_t bytes) noexcept;
  void emitFallback(Level level, const char* utf8, uint16_t length) noexcept;
  void publish(Level level, const wchar_t* fmt, va_list args) noexcept;

  HANDLE fileHandle_ = INVALID_HANDLE_VALUE;
  HANDLE flushEvent_ = nullptr;
  HANDLE stopEvent_ = nullptr;
  std::thread writer_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> writeCursor_{0};
  std::atomic<uint64_t> readCursor_{0};
  std::atomic<uint64_t> dropped_{0};
  Slot slots_[kSlotCount]{};
};

}  // namespace fast_explorer::core
