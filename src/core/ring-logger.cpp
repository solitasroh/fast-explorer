#include "core/ring-logger.h"

#include <shlobj.h>
#include <stdio.h>
#include <stringapiset.h>

#include <chrono>
#include <cstdarg>
#include <string>

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

bool resolveLogDirectory(std::wstring& outPath) {
  wchar_t portable[MAX_PATH];
  const DWORD portableLen = GetEnvironmentVariableW(
      L"FAST_EXPLORER_PORTABLE_ROOT", portable, _countof(portable));
  if (portableLen > 0 && portableLen < _countof(portable)) {
    outPath.assign(portable, portableLen);
    outPath.append(L"\\logs");
    return true;
  }

  PWSTR localAppData = nullptr;
  HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
  if (FAILED(hr) || localAppData == nullptr) {
    if (localAppData) {
      CoTaskMemFree(localAppData);
    }
    return false;
  }
  outPath.assign(localAppData);
  CoTaskMemFree(localAppData);
  outPath.append(L"\\FastExplorer\\logs");
  return true;
}

bool ensureDirectory(const std::wstring& path) {
  if (CreateDirectoryW(path.c_str(), nullptr)) {
    return true;
  }
  const DWORD err = GetLastError();
  if (err == ERROR_ALREADY_EXISTS) {
    return true;
  }
  if (err != ERROR_PATH_NOT_FOUND) {
    return false;
  }
  // Recursively create parent.
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    return false;
  }
  if (!ensureDirectory(path.substr(0, slash))) {
    return false;
  }
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
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
                     "[%04u-%02u-%02uT%02u:%02u:%02u.%03u] [%s] [tid=%u] ",
                     local.wYear, local.wMonth, local.wDay,
                     local.wHour, local.wMinute, local.wSecond, local.wMilliseconds,
                     levelTag(lvl), tid);
}

}  // namespace

RingLogger& RingLogger::instance() {
  static RingLogger singleton;
  return singleton;
}

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
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  if (stopEvent_) {
    SetEvent(stopEvent_);
  }
  if (writer_.joinable()) {
    writer_.join();
  }
  if (stopEvent_)  { CloseHandle(stopEvent_);  stopEvent_  = nullptr; }
  if (flushEvent_) { CloseHandle(flushEvent_); flushEvent_ = nullptr; }
  if (fileHandle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(fileHandle_);
    fileHandle_ = INVALID_HANDLE_VALUE;
  }
}

bool RingLogger::openLogFile() {
  std::wstring dir;
  if (!resolveLogDirectory(dir)) {
    return false;
  }
  if (!ensureDirectory(dir)) {
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
  const int wideCount = wideLen < 0 ? static_cast<int>(_countof(wideBuf) - 1) : wideLen;

  char utf8Buf[kMaxMessageBytes];
  int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
                                    wideBuf, wideCount,
                                    utf8Buf, static_cast<int>(sizeof(utf8Buf)),
                                    nullptr, nullptr);
  if (utf8Len <= 0) {
    utf8Len = 0;
  }
  if (utf8Len > static_cast<int>(kMaxMessageBytes)) {
    utf8Len = static_cast<int>(kMaxMessageBytes);
  }

  if (!running_.load(std::memory_order_acquire)) {
    emitFallback(level, utf8Buf, static_cast<uint16_t>(utf8Len));
    return;
  }

  const uint64_t ticket = writeCursor_.fetch_add(1, std::memory_order_relaxed);
  Slot& slot = slots_[ticket % kSlotCount];
  const uint64_t generation = ticket / kSlotCount;
  const uint64_t inProgress = (generation * 2) + 1;
  const uint64_t published = (generation * 2) + 2;

  slot.seq.store(inProgress, std::memory_order_relaxed);
  GetSystemTimeAsFileTime(&slot.timestamp);
  slot.threadId = GetCurrentThreadId();
  slot.level = level;
  slot.messageLength = static_cast<uint16_t>(utf8Len);
  if (utf8Len > 0) {
    memcpy(slot.message, utf8Buf, static_cast<size_t>(utf8Len));
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

void RingLogger::info(const wchar_t* fmt, ...) noexcept {
  va_list args; va_start(args, fmt); publish(Level::Info,  fmt, args); va_end(args);
}
void RingLogger::warn(const wchar_t* fmt, ...) noexcept {
  va_list args; va_start(args, fmt); publish(Level::Warn,  fmt, args); va_end(args);
}
void RingLogger::error(const wchar_t* fmt, ...) noexcept {
  va_list args; va_start(args, fmt); publish(Level::Error, fmt, args); va_end(args);
}
void RingLogger::fatal(const wchar_t* fmt, ...) noexcept {
  va_list args; va_start(args, fmt); publish(Level::Fatal, fmt, args); va_end(args);
}

void RingLogger::writeSlot(const Slot& slot) {
  if (fileHandle_ == INVALID_HANDLE_VALUE) {
    return;
  }
  char header[96];
  const int headerLen = formatHeader(header, sizeof(header),
                                     slot.timestamp, slot.threadId, slot.level);
  if (headerLen <= 0) {
    return;
  }
  DWORD written = 0;
  WriteFile(fileHandle_, header, static_cast<DWORD>(headerLen), &written, nullptr);
  if (slot.messageLength > 0) {
    WriteFile(fileHandle_, slot.message, slot.messageLength, &written, nullptr);
  }
  static const char kNewline = '\n';
  WriteFile(fileHandle_, &kNewline, 1, &written, nullptr);
}

void RingLogger::writerLoop() {
  HANDLE waits[2] = { flushEvent_, stopEvent_ };
  for (;;) {
    const DWORD rc = WaitForMultipleObjects(2, waits, FALSE, kFlushIntervalMs);
    const bool stopping = (rc == WAIT_OBJECT_0 + 1);

    // Drain published slots.
    for (;;) {
      const uint64_t read = readCursor_.load(std::memory_order_relaxed);
      const uint64_t write = writeCursor_.load(std::memory_order_acquire);
      if (read >= write) {
        break;
      }
      Slot& slot = slots_[read % kSlotCount];
      const uint64_t generation = read / kSlotCount;
      const uint64_t expectedPublished = (generation * 2) + 2;
      const uint64_t seq = slot.seq.load(std::memory_order_acquire);
      if (seq != expectedPublished) {
        // Producer has not yet finished writing this slot; bail and retry
        // after the next signal/timeout.
        break;
      }
      writeSlot(slot);
      readCursor_.store(read + 1, std::memory_order_release);
    }

    if (fileHandle_ != INVALID_HANDLE_VALUE) {
      FlushFileBuffers(fileHandle_);
    }

    if (stopping) {
      break;
    }
  }
}

void RingLogger::emitFallback(Level level, const char* utf8, uint16_t length) noexcept {
  // Pre-startup or post-shutdown messages still surface to a debugger.
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
