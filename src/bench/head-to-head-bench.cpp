#include "bench/head-to-head-bench.h"

#include <windows.h>

#include <cwchar>

#include "core/path-utils.h"

namespace fast_explorer::bench {

namespace {

struct FindHandleGuard {
  HANDLE h = INVALID_HANDLE_VALUE;
  ~FindHandleGuard() {
    if (h != INVALID_HANDLE_VALUE) {
      FindClose(h);
    }
  }
  FindHandleGuard() = default;
  FindHandleGuard(const FindHandleGuard&) = delete;
  FindHandleGuard& operator=(const FindHandleGuard&) = delete;
};

struct FileHandleGuard {
  HANDLE h = INVALID_HANDLE_VALUE;
  ~FileHandleGuard() {
    if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
    }
  }
  FileHandleGuard() = default;
  FileHandleGuard(const FileHandleGuard&) = delete;
  FileHandleGuard& operator=(const FileHandleGuard&) = delete;
};

uint64_t qpcFrequencyHz() {
  static const uint64_t cached = []() noexcept {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<uint64_t>(f.QuadPart);
  }();
  return cached;
}

uint64_t ticksToMicros(uint64_t deltaTicks) {
  const uint64_t hz = qpcFrequencyHz();
  if (hz == 0) {
    return 0;
  }
  return (deltaTicks * 1000000ULL) / hz;
}

bool isDotEntry(const wchar_t* name, size_t lengthChars) {
  if (lengthChars == 1 && name[0] == L'.') {
    return true;
  }
  if (lengthChars == 2 && name[0] == L'.' && name[1] == L'.') {
    return true;
  }
  return false;
}

EnumerationRun timeRun(uint64_t (*fn)(const std::wstring&, bool*),
                       const std::wstring& internalPath, bool* okOut) {
  EnumerationRun out{};
  LARGE_INTEGER t0{};
  LARGE_INTEGER t1{};
  bool ok = true;
  QueryPerformanceCounter(&t0);
  const uint64_t count = fn(internalPath, &ok);
  QueryPerformanceCounter(&t1);
  if (!ok) {
    *okOut = false;
    return out;
  }
  out.microseconds =
      ticksToMicros(static_cast<uint64_t>(t1.QuadPart - t0.QuadPart));
  out.entriesObserved = count;
  return out;
}

}  // namespace

uint64_t enumerateFindFirstRaw(const std::wstring& internalPath, bool* okOut) {
  *okOut = true;
  std::wstring pattern(internalPath);
  pattern.push_back(L'\\');
  pattern.push_back(L'*');
  WIN32_FIND_DATAW fd{};
  FindHandleGuard guard;
  guard.h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                             FindExSearchNameMatch, nullptr,
                             FIND_FIRST_EX_LARGE_FETCH);
  if (guard.h == INVALID_HANDLE_VALUE) {
    *okOut = false;
    return 0;
  }
  uint64_t count = 0;
  do {
    if (!isDotEntry(fd.cFileName, wcslen(fd.cFileName))) {
      ++count;
    }
  } while (FindNextFileW(guard.h, &fd));
  if (GetLastError() != ERROR_NO_MORE_FILES) {
    *okOut = false;
  }
  return count;
}

uint64_t enumerateGfibheRaw(const std::wstring& internalPath, bool* okOut) {
  *okOut = true;
  FileHandleGuard guard;
  guard.h = CreateFileW(internalPath.c_str(), FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (guard.h == INVALID_HANDLE_VALUE) {
    *okOut = false;
    return 0;
  }
  alignas(8) BYTE buffer[64 * 1024];
  uint64_t count = 0;
  FILE_INFO_BY_HANDLE_CLASS infoClass = FileIdBothDirectoryRestartInfo;
  for (;;) {
    if (!GetFileInformationByHandleEx(guard.h, infoClass, buffer,
                                      sizeof(buffer))) {
      const DWORD err = GetLastError();
      if (err != ERROR_NO_MORE_FILES) {
        *okOut = false;
      }
      break;
    }
    infoClass = FileIdBothDirectoryInfo;
    const BYTE* p = buffer;
    for (;;) {
      const auto* entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(p);
      const size_t nameChars = entry->FileNameLength / sizeof(wchar_t);
      if (!isDotEntry(entry->FileName, nameChars)) {
        ++count;
      }
      if (entry->NextEntryOffset == 0) {
        break;
      }
      p += entry->NextEntryOffset;
    }
  }
  return count;
}

HeadToHeadResult runHeadToHeadBench(const std::wstring& path, int runs) {
  HeadToHeadResult result;
  if (path.empty() || runs <= 0) {
    result.error = EnumerationBenchError::PathInvalid;
    return result;
  }

  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toInternal;
  std::wstring internalPath;
  if (toInternal(path, internalPath) != PathConvertError::None) {
    result.error = EnumerationBenchError::PathInvalid;
    result.errorDetail = path;
    return result;
  }

  result.findRuns.reserve(static_cast<size_t>(runs));
  result.gfibheRuns.reserve(static_cast<size_t>(runs));

  // Interleave so neither method monopolizes the warm page cache.
  for (int i = 0; i < runs; ++i) {
    bool ok = true;
    EnumerationRun findRun =
        timeRun(&enumerateFindFirstRaw, internalPath, &ok);
    if (!ok && findRun.entriesObserved == 0) {
      result.error = EnumerationBenchError::OpenFailed;
      result.errorDetail = path;
      return result;
    }
    result.findRuns.push_back(findRun);

    ok = true;
    EnumerationRun gfibheRun =
        timeRun(&enumerateGfibheRaw, internalPath, &ok);
    if (!ok && gfibheRun.entriesObserved == 0) {
      result.error = EnumerationBenchError::OpenFailed;
      result.errorDetail = path;
      return result;
    }
    result.gfibheRuns.push_back(gfibheRun);
  }

  std::vector<uint64_t> findSamples;
  std::vector<uint64_t> gfibheSamples;
  findSamples.reserve(result.findRuns.size());
  gfibheSamples.reserve(result.gfibheRuns.size());
  for (const auto& r : result.findRuns) {
    findSamples.push_back(r.microseconds);
  }
  for (const auto& r : result.gfibheRuns) {
    gfibheSamples.push_back(r.microseconds);
  }
  const Percentiles findP = computePercentiles(std::move(findSamples));
  const Percentiles gfibheP = computePercentiles(std::move(gfibheSamples));
  result.findMedianUs = findP.median;
  result.findP95Us = findP.p95;
  result.gfibheMedianUs = gfibheP.median;
  result.gfibheP95Us = gfibheP.p95;

  if (result.findMedianUs > 0) {
    const int64_t findM = static_cast<int64_t>(result.findMedianUs);
    const int64_t gfibheM = static_cast<int64_t>(result.gfibheMedianUs);
    result.gfibhePercentFasterX100 =
        static_cast<int32_t>(((findM - gfibheM) * 10000) / findM);
  }
  return result;
}

}  // namespace fast_explorer::bench
