#include "core/ring-logger.h"

#include <stdio.h>
#include <stringapiset.h>

#include <cstdarg>
#include <string>

#include "core/path-utils.h"

namespace fast_explorer::core {

namespace {

constexpr DWORD kFlushIntervalMs = 100;

const char* levelTag(RingLogger::Level level) noexcept {
  switch (level) {
    case RingLogger::Level::Trace: return "TRACE";
    case RingLogger::Level::Debug: return "DEBUG";
    case RingLogger::Level::Info:  return "INFO ";
    case RingLogger::Level::Warn:  return "WARN ";
    case RingLogger::Level::Error: return "ERROR";
    case RingLogger::Level::Fatal: return "FATAL";
  }
  return "?????";
}

void buildLogFileName(std::wstring& outPath) {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t name[64];
  swprintf_s(name, L"\\fast-explorer-%04u%02u%02u.log",
             st.wYear, st.wMonth, st.wDay);
  outPath.append(name);
}

int formatHeader(char* dst, size_t cap, const FILETIME& ft, uint32_t tid, RingLogger::Level lvl) {
  SYSTEMTIME utc{};
  FileTimeToSystemTime(&ft, &utc);
  SYSTEMTIME local{};
  SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
  return _snprintf_s(dst, cap, _TRUNCATE,
                     "[%04u-%02u-%02uT%02u:%02u:%02u.%03u] [%s] [tid=%lu] ",
                     local.wYear, local.wMonth, local.wDay,
                     local.wHour, local.wMinute, local.wSecond, local.wMilliseconds,
                     levelTag(lvl), static_cast<unsigned long>(tid));
}

}  // namespace

RingLogger::RingLogger() = default;

RingLogger::~RingLogger() {
  stop();
}

bool RingLogger::start() {
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }
  if (!openLogFile()) {
    return false;
  }
  flushEvent_ = CreateEventW(nullptr, /*manualReset=*/FALSE, FALSE, nullptr);
  stopEvent_ = CreateEventW(nullptr, /*manualReset=*/TRUE, FALSE, nullptr);
  if (flushEvent_ == nullptr || stopEvent_ == nullptr) {
    if (fileHandle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(fileHandle_);
      fileHandle_ = INVALID_HANDLE_VALUE;
    }
    if (flushEvent_) { CloseHandle(flushEvent_); flushEvent_ = nullptr; }
    if (stopEvent_)  { CloseHandle(stopEvent_);  stopEvent_  = nullptr; }
    return false;
  }
  running_.store(true, std::memory_order_release);
  writer_ = std::thread(&RingLogger::writerLoop, this);
  info(L"ring-logger started");
  return true;
}

void RingLogger::stop() {
  // Order matters:
  //   1. Signal stopEvent so the writer thread enters a final drain loop and
  //      writes every slot that producers have already published.
  //   2. Wait for the writer thread to finish that drain (join below sees it
  //      because the loop only exits after the post-stop drain completes).
  //   3. Only then flip running_ off so any later log() falls back to ODS.
  //   4. Close handles last.
  if (!writer_.joinable() && !running_.load(std::memory_order_acquire)) {
    return;
  }
  if (stopEvent_) {
    SetEvent(stopEvent_);
  }
  if (writer_.joinable()) {
    writer_.join();
  }
  running_.store(false, std::memory_order_release);
  if (stopEvent_)  { CloseHandle(stopEvent_);  stopEvent_  = nullptr; }
  if (flushEvent_) { CloseHandle(flushEvent_); flushEvent_ = nullptr; }
  if (fileHandle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(fileHandle_);
    fileHandle_ = INVALID_HANDLE_VALUE;
  }
}

bool RingLogger::openLogFile() {
  std::wstring dir;
  if (!resolveAppDataSubdir(L"logs", dir)) {
    return false;
  }
  if (!ensureDirectoryRecursive(dir.c_str())) {
    return false;
  }
  std::wstring path = dir;
  buildLogFileName(path);
  fileHandle_ = CreateFileW(path.c_str(),
                            FILE_APPEND_DATA, FILE_SHARE_READ,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  return fileHandle_ != INVALID_HANDLE_VALUE;
}

void RingLogger::publish(Level level, const wchar_t* fmt, va_list args) noexcept {
  wchar_t wideBuf[kMaxMessageBytes];
  const int wideLen = _vsnwprintf_s(wideBuf,
                                    _countof(wideBuf),
                                    _TRUNCATE,
                                    fmt, args);
  size_t wideCount;
  if (wideLen < 0) {
    wideCount = wcsnlen(wideBuf, _countof(wideBuf));
  } else {
    wideCount = static_cast<size_t>(wideLen);
  }

  char utf8Buf[kMaxMessageBytes];
  int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
                                    wideBuf, static_cast<int>(wideCount),
                                    utf8Buf, static_cast<int>(sizeof(utf8Buf)),
                                    nullptr, nullptr);
  if (utf8Len < 0) {
    utf8Len = 0;
  }
  size_t utf8Bytes = static_cast<size_t>(utf8Len);
  if (utf8Bytes > kMaxMessageBytes) {
    utf8Bytes = kMaxMessageBytes;
  }

  if (!running_.load(std::memory_order_acquire)) {
    emitFallback(level, utf8Buf, static_cast<uint16_t>(utf8Bytes));
    return;
  }

  // Overflow guard: if the producer would lap the reader, drop the message.
  const uint64_t writeNow = writeCursor_.load(std::memory_order_relaxed);
  const uint64_t readNow = readCursor_.load(std::memory_order_acquire);
  if (writeNow - readNow >= kSlotCount) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    emitFallback(level, utf8Buf, static_cast<uint16_t>(utf8Bytes));
    return;
  }

  const uint64_t ticket = writeCursor_.fetch_add(1, std::memory_order_acq_rel);
  // Re-check after we own the ticket; concurrent producers may have advanced
  // the cursor past safety.
  if (ticket - readCursor_.load(std::memory_order_acquire) >= kSlotCount) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    emitFallback(level, utf8Buf, static_cast<uint16_t>(utf8Bytes));
    return;
  }

  Slot& slot = slots_[ticket % kSlotCount];
  const uint64_t generation = ticket / kSlotCount;
  const uint64_t inProgress = (generation * 2) + 1;
  const uint64_t published = (generation * 2) + 2;

  slot.seq.store(inProgress, std::memory_order_release);
  GetSystemTimeAsFileTime(&slot.timestamp);
  slot.threadId = GetCurrentThreadId();
  slot.level = level;
  slot.messageLength = static_cast<uint16_t>(utf8Bytes);
  if (utf8Bytes > 0) {
    memcpy(slot.message, utf8Buf, utf8Bytes);
  }
  slot.seq.store(published, std::memory_order_release);

  if (level >= Level::Info && flushEvent_) {
    SetEvent(flushEvent_);
  }
}

void RingLogger::log(Level level, const wchar_t* fmt, ...) noexcept {
  va_list args;
  va_start(args, fmt);
  publish(level, fmt, args);
  va_end(args);
}

#define FE_DEFINE_LEVEL_HELPER(name, lvl)                            \
  void RingLogger::name(const wchar_t* fmt, ...) noexcept {          \
    va_list args; va_start(args, fmt);                               \
    publish(Level::lvl, fmt, args); va_end(args);                    \
  }

FE_DEFINE_LEVEL_HELPER(info,  Info)
FE_DEFINE_LEVEL_HELPER(warn,  Warn)
FE_DEFINE_LEVEL_HELPER(error, Error)
FE_DEFINE_LEVEL_HELPER(fatal, Fatal)

#undef FE_DEFINE_LEVEL_HELPER

void RingLogger::writeAllBytes(const void* data, size_t bytes) noexcept {
  if (fileHandle_ == INVALID_HANDLE_VALUE || bytes == 0) {
    return;
  }
  const char* cursor = static_cast<const char*>(data);
  size_t remaining = bytes;
  while (remaining > 0) {
    const DWORD chunk = remaining > 0xFFFFFFFFu
                          ? 0xFFFFFFFFu
                          : static_cast<DWORD>(remaining);
    DWORD written = 0;
    if (!WriteFile(fileHandle_, cursor, chunk, &written, nullptr) || written == 0) {
      OutputDebugStringW(L"[RingLogger] WriteFile failed; remaining bytes dropped\n");
      return;
    }
    cursor += written;
    remaining -= written;
  }
}

void RingLogger::writeSlot(const Slot& slot) {
  char header[96];
  const int headerLen = formatHeader(header, sizeof(header),
                                     slot.timestamp, slot.threadId, slot.level);
  if (headerLen <= 0) {
    return;
  }
  writeAllBytes(header, static_cast<size_t>(headerLen));
  if (slot.messageLength > 0) {
    writeAllBytes(slot.message, slot.messageLength);
  }
  static const char kNewline = '\n';
  writeAllBytes(&kNewline, 1);
}

void RingLogger::writerLoop() {
  HANDLE waits[2] = { flushEvent_, stopEvent_ };
  bool stopRequested = false;
  for (;;) {
    if (!stopRequested) {
      const DWORD rc = WaitForMultipleObjects(2, waits, FALSE, kFlushIntervalMs);
      stopRequested = (rc == WAIT_OBJECT_0 + 1);
    }

    // Drain published slots. On stop, spin-wait briefly for any in-flight
    // slots to publish so we do not lose the last few messages.
    for (;;) {
      const uint64_t read = readCursor_.load(std::memory_order_relaxed);
      const uint64_t write = writeCursor_.load(std::memory_order_acquire);
      if (read >= write) {
        break;
      }
      Slot& slot = slots_[read % kSlotCount];
      const uint64_t generation = read / kSlotCount;
      const uint64_t expectedPublished = (generation * 2) + 2;
      uint64_t seq = slot.seq.load(std::memory_order_acquire);
      if (seq != expectedPublished) {
        if (!stopRequested) {
          break;  // producer still writing; come back on next signal.
        }
        // During shutdown wait a short moment for the producer to finish.
        for (int spin = 0; spin < 100 && seq != expectedPublished; ++spin) {
          Sleep(0);
          seq = slot.seq.load(std::memory_order_acquire);
        }
        if (seq != expectedPublished) {
          break;  // give up on this slot; it was lost in flight.
        }
      }
      writeSlot(slot);
      readCursor_.store(read + 1, std::memory_order_release);
    }

    if (fileHandle_ != INVALID_HANDLE_VALUE) {
      FlushFileBuffers(fileHandle_);
    }

    if (stopRequested) {
      break;
    }
  }
}

void RingLogger::emitFallback(Level level, const char* utf8, uint16_t length) noexcept {
  char buffer[512];
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  const int headerLen = formatHeader(buffer, sizeof(buffer),
                                     ft, GetCurrentThreadId(), level);
  if (headerLen <= 0) {
    return;
  }
  const int remain = static_cast<int>(sizeof(buffer)) - headerLen - 2;
  const int copyLen = (length < static_cast<uint16_t>(remain)) ? length : remain;
  if (copyLen > 0) {
    memcpy(buffer + headerLen, utf8, static_cast<size_t>(copyLen));
  }
  buffer[headerLen + copyLen] = '\n';
  buffer[headerLen + copyLen + 1] = '\0';
  OutputDebugStringA(buffer);
}

}  // namespace fast_explorer::core
