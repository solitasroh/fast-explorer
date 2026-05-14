#include "core/crash-handler.h"

#include <dbghelp.h>
#include <stdio.h>

#include <atomic>

#include "core/path-utils.h"
#include "core/perf-tracker.h"

// CrashHandler is intentionally free of RingLogger dependencies. A faulting
// process may have a corrupt heap or be mid-way through a logger write, so
// we limit ourselves to Win32 APIs that work from stack buffers. Diagnostics
// are emitted via OutputDebugStringW; the dump file itself is the durable
// record.

namespace fast_explorer::core {

namespace {

constexpr DWORD kCrashStatusAssertion = 0xC000041Du;  // STATUS_ASSERTION_FAILURE
constexpr DWORD kCrashStatusAccessVio = 0xC0000005u;  // STATUS_ACCESS_VIOLATION
constexpr ULONG32 kPerfTrackerStreamType = 0x46455031;  // 'FEP1'

struct CrashState {
  std::atomic<bool> installed{false};
  // Re-entrancy guard so a fault triggered while writing the dump does not
  // loop. thread_local would be cleaner but the variable must be reachable
  // from the static handlers without per-thread storage costs.
  std::atomic<bool> writing{false};
  wchar_t dumpDirectory[MAX_PATH * 2]{};
  wchar_t lastDumpPath[MAX_PATH * 2]{};
};

CrashState g_state;

bool resolveDumpDirectory() {
  std::wstring dir;
  if (!resolveAppDataSubdir(L"crashdumps", dir)) {
    return false;
  }
  if (dir.size() >= _countof(g_state.dumpDirectory)) {
    return false;
  }
  wcsncpy_s(g_state.dumpDirectory, _countof(g_state.dumpDirectory),
            dir.c_str(), _TRUNCATE);
  return true;
}

void buildDumpFileName(wchar_t* dest, size_t cap) {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  swprintf_s(dest, cap,
             L"%ls\\fast-explorer-%lu-%04u%02u%02u-%02u%02u%02u.dmp",
             g_state.dumpDirectory,
             static_cast<unsigned long>(GetCurrentProcessId()),
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
}

bool writeDumpInternal(EXCEPTION_POINTERS* exceptionInfo,
                      const wchar_t* reasonForLog) {
  // Reject re-entry. The atomic exchange returns the previous value: if it
  // was already true, another handler invocation is already running.
  bool expected = false;
  if (!g_state.writing.compare_exchange_strong(expected, true)) {
    OutputDebugStringW(L"[CrashHandler] re-entrant invocation suppressed\n");
    return false;
  }

  bool ok = false;
  do {
    if (g_state.dumpDirectory[0] == L'\0') {
      break;
    }
    if (!ensureDirectoryRecursive(g_state.dumpDirectory)) {
      break;
    }
    wchar_t dumpPath[MAX_PATH * 2];
    buildDumpFileName(dumpPath, _countof(dumpPath));

    HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      break;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    MINIDUMP_EXCEPTION_INFORMATION* meiPtr = nullptr;
    if (exceptionInfo) {
      mei.ThreadId = GetCurrentThreadId();
      mei.ExceptionPointers = exceptionInfo;
      mei.ClientPointers = FALSE;
      meiPtr = &mei;
    }

    // Attach the PerfTracker ring as a user stream so post-mortem analysis
    // can see what the app was doing just before the fault.
    MINIDUMP_USER_STREAM perfStream{};
    perfStream.Type = kPerfTrackerStreamType;
    perfStream.BufferSize = static_cast<ULONG>(
        PerfTracker::instance().rawSlotBufferBytes());
    perfStream.Buffer = const_cast<void*>(PerfTracker::instance().rawSlotBuffer());

    MINIDUMP_USER_STREAM streams[1] = { perfStream };
    MINIDUMP_USER_STREAM_INFORMATION streamInfo{};
    streamInfo.UserStreamCount = 1;
    streamInfo.UserStreamArray = streams;

    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

    const BOOL written = MiniDumpWriteDump(GetCurrentProcess(),
                                           GetCurrentProcessId(),
                                           file, dumpType,
                                           meiPtr, &streamInfo, nullptr);
    CloseHandle(file);
    if (!written) {
      break;
    }

    wcsncpy_s(g_state.lastDumpPath, _countof(g_state.lastDumpPath),
              dumpPath, _TRUNCATE);

    // Stack-buffer diagnostic; no heap allocation, no RingLogger.
    wchar_t line[MAX_PATH * 2 + 128];
    swprintf_s(line,
               L"[CrashHandler] dump written: %ls (reason=%ls)\n",
               dumpPath, reasonForLog ? reasonForLog : L"unknown");
    OutputDebugStringW(line);
    ok = true;
  } while (false);

  g_state.writing.store(false, std::memory_order_release);
  return ok;
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  writeDumpInternal(info, L"unhandled-exception");
  return EXCEPTION_EXECUTE_HANDLER;
}

void __cdecl invalidParameterHandler(const wchar_t* expression,
                                     const wchar_t* function,
                                     const wchar_t* file,
                                     unsigned int line,
                                     uintptr_t /*reserved*/) {
  wchar_t reason[256];
  // _vsnwprintf_s is intentionally avoided in this path because it can call
  // the same invalid-parameter handler. wsprintfW is signal-safer.
  wsprintfW(reason, L"invalid-parameter expr=%ls fn=%ls file=%ls line=%u",
            expression ? expression : L"<null>",
            function ? function : L"<null>",
            file ? file : L"<null>",
            line);
  writeDumpInternal(nullptr, reason);
  TerminateProcess(GetCurrentProcess(), kCrashStatusAssertion);
}

void __cdecl purecallHandler() {
  writeDumpInternal(nullptr, L"pure-virtual-call");
  TerminateProcess(GetCurrentProcess(), kCrashStatusAccessVio);
}

}  // namespace

bool CrashHandler::install() noexcept {
  bool expected = false;
  if (!g_state.installed.compare_exchange_strong(expected, true)) {
    return true;
  }
  if (!resolveDumpDirectory()) {
    // Roll back the install flag so callers can retry later if needed.
    g_state.installed.store(false, std::memory_order_release);
    return false;
  }
  SetUnhandledExceptionFilter(&unhandledExceptionFilter);
  _set_invalid_parameter_handler(&invalidParameterHandler);
  _set_purecall_handler(&purecallHandler);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

  wchar_t line[MAX_PATH * 2 + 64];
  swprintf_s(line, L"[CrashHandler] installed (dir=%ls)\n", g_state.dumpDirectory);
  OutputDebugStringW(line);
  return true;
}

const wchar_t* CrashHandler::writeManualDump(const wchar_t* reason) noexcept {
  if (!g_state.installed.load(std::memory_order_acquire)) {
    return L"";
  }
  if (writeDumpInternal(nullptr, reason ? reason : L"manual")) {
    return g_state.lastDumpPath;
  }
  return L"";
}

}  // namespace fast_explorer::core
