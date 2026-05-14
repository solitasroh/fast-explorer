#include "core/crash-handler.h"

#include <dbghelp.h>
#include <shlobj.h>
#include <stdio.h>

#include <atomic>

#include "core/ring-logger.h"

namespace fast_explorer::core {

namespace {

std::atomic<bool> g_installed{false};
wchar_t g_dumpDirectory[MAX_PATH] = L"";
wchar_t g_lastDumpPath[MAX_PATH] = L"";

bool resolveDumpDirectory() {
  wchar_t portable[MAX_PATH];
  const DWORD portableLen = GetEnvironmentVariableW(
      L"FAST_EXPLORER_PORTABLE_ROOT", portable, _countof(portable));
  if (portableLen > 0 && portableLen < _countof(portable)) {
    swprintf_s(g_dumpDirectory, L"%s\\crashdumps", portable);
  } else {
    PWSTR localAppData = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
    if (FAILED(hr) || localAppData == nullptr) {
      if (localAppData) {
        CoTaskMemFree(localAppData);
      }
      return false;
    }
    swprintf_s(g_dumpDirectory, L"%s\\FastExplorer\\crashdumps", localAppData);
    CoTaskMemFree(localAppData);
  }
  return true;
}

bool ensureDirectory(const wchar_t* path) {
  if (CreateDirectoryW(path, nullptr)) {
    return true;
  }
  const DWORD err = GetLastError();
  if (err == ERROR_ALREADY_EXISTS) {
    return true;
  }
  if (err != ERROR_PATH_NOT_FOUND) {
    return false;
  }
  wchar_t parent[MAX_PATH];
  wcsncpy_s(parent, path, _TRUNCATE);
  for (size_t i = wcslen(parent); i > 0; --i) {
    if (parent[i - 1] == L'\\' || parent[i - 1] == L'/') {
      parent[i - 1] = L'\0';
      break;
    }
  }
  if (parent[0] == L'\0') {
    return false;
  }
  if (!ensureDirectory(parent)) {
    return false;
  }
  return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

void buildDumpFileName(wchar_t* dest, size_t cap) {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  swprintf_s(dest, cap,
             L"%s\\fast-explorer-%lu-%04u%02u%02u-%02u%02u%02u.dmp",
             g_dumpDirectory, GetCurrentProcessId(),
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
}

bool writeDumpInternal(EXCEPTION_POINTERS* exceptionInfo,
                      const wchar_t* reasonForLog) {
  if (g_dumpDirectory[0] == L'\0') {
    return false;
  }
  if (!ensureDirectory(g_dumpDirectory)) {
    return false;
  }
  wchar_t dumpPath[MAX_PATH];
  buildDumpFileName(dumpPath, _countof(dumpPath));

  HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  MINIDUMP_EXCEPTION_INFORMATION mei{};
  MINIDUMP_EXCEPTION_INFORMATION* meiPtr = nullptr;
  if (exceptionInfo) {
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = exceptionInfo;
    mei.ClientPointers = FALSE;
    meiPtr = &mei;
  }

  const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
      MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

  BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                   file, dumpType, meiPtr, nullptr, nullptr);
  CloseHandle(file);

  if (written) {
    wcsncpy_s(g_lastDumpPath, dumpPath, _TRUNCATE);
    RingLogger::instance().fatal(L"crash dump written: %s (reason=%s)",
                                 dumpPath, reasonForLog ? reasonForLog : L"unknown");
    return true;
  }
  return false;
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  writeDumpInternal(info, L"unhandled-exception");
  // Let the OS continue its default termination path so debuggers can still
  // attach and the exit code reflects the exception.
  return EXCEPTION_EXECUTE_HANDLER;
}

void __cdecl invalidParameterHandler(const wchar_t* expression,
                                     const wchar_t* function,
                                     const wchar_t* file,
                                     unsigned int line,
                                     uintptr_t /*reserved*/) {
  wchar_t reason[256];
  swprintf_s(reason, L"invalid-parameter expr=%s fn=%s file=%s line=%u",
             expression ? expression : L"<null>",
             function ? function : L"<null>",
             file ? file : L"<null>",
             line);
  writeDumpInternal(nullptr, reason);
  TerminateProcess(GetCurrentProcess(), 0xC000041Du);  // STATUS_ASSERTION_FAILURE
}

void __cdecl purecallHandler() {
  writeDumpInternal(nullptr, L"pure-virtual-call");
  TerminateProcess(GetCurrentProcess(), 0xC0000005u);  // STATUS_ACCESS_VIOLATION
}

}  // namespace

bool CrashHandler::install() noexcept {
  bool expected = false;
  if (!g_installed.compare_exchange_strong(expected, true)) {
    return true;
  }
  if (!resolveDumpDirectory()) {
    g_installed.store(false);
    return false;
  }
  SetUnhandledExceptionFilter(&unhandledExceptionFilter);
  _set_invalid_parameter_handler(&invalidParameterHandler);
  _set_purecall_handler(&purecallHandler);
  // Disable the legacy CRT abort dialog so the dump can run unattended.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  RingLogger::instance().info(L"crash handler installed (dir=%s)", g_dumpDirectory);
  return true;
}

const wchar_t* CrashHandler::writeManualDump(const wchar_t* reason) noexcept {
  if (!g_installed.load(std::memory_order_acquire)) {
    return L"";
  }
  if (writeDumpInternal(nullptr, reason ? reason : L"manual")) {
    return g_lastDumpPath;
  }
  return L"";
}

}  // namespace fast_explorer::core
