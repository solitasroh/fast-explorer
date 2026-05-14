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
// MVP scope: 512 slots * 256 bytes = 128 KB ring, UTF-8 output, no rotation
// yet (M2 will add daily rotation + retention).
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

  static RingLogger& instance();

  // Initialize writer thread + open log file. Safe to call once at startup.
  // Returns false if the log directory cannot be created or the file cannot
  // be opened; callers should fall back to OutputDebugString-only mode.
  bool start();

  // Flush any pending entries and stop the writer thread. Called from the
  // destructor as well; explicit shutdown helps order operations during
  // wWinMain teardown.
  void stop();

  // Producer entry points. wide-char input is converted to UTF-8.
  void log(Level level, const wchar_t* fmt, ...) noexcept;
  void info(const wchar_t* fmt, ...) noexcept;
  void warn(const wchar_t* fmt, ...) noexcept;
  void error(const wchar_t* fmt, ...) noexcept;
  void fatal(const wchar_t* fmt, ...) noexcept;

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
  static_assert(sizeof(Slot) <= kSlotBytes + 16,
                "Slot must stay close to the 256-byte budget");

  RingLogger();
  ~RingLogger();
  RingLogger(const RingLogger&) = delete;
  RingLogger& operator=(const RingLogger&) = delete;

  void writerLoop();
  bool openLogFile();
  void writeSlot(const Slot& slot);
  void emitFallback(Level level, const char* utf8, uint16_t length) noexcept;

  void publish(Level level, const wchar_t* fmt, va_list args) noexcept;

  HANDLE fileHandle_ = INVALID_HANDLE_VALUE;
  HANDLE flushEvent_ = nullptr;
  HANDLE stopEvent_ = nullptr;
  std::thread writer_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> writeCursor_{0};
  std::atomic<uint64_t> readCursor_{0};
  Slot slots_[kSlotCount]{};
};

}  // namespace fast_explorer::core
