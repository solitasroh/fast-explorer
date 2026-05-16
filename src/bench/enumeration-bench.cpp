#include "bench/enumeration-bench.h"

#include <windows.h>

#include <algorithm>
#include <stop_token>

#include "core/directory-enumerator.h"
#include "core/file-model-store.h"
#include "core/fs-backend.h"
#include "core/path-utils.h"
#include "core/process-memory.h"
#include "core/win32-fs-backend.h"

namespace fast_explorer::bench {

namespace {

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
  // (delta * 1e6) / hz — order matters to avoid early overflow. delta
  // for a single run will not exceed minutes worth of QPC ticks, so the
  // 128-bit intermediate is not strictly required.
  return (deltaTicks * 1000000ULL) / hz;
}

struct RunOutcome {
  EnumerationRun run;
  uint64_t storeEntriesBytes = 0;
  uint64_t storeArenaCommittedBytes = 0;
  uint64_t workingSetBytesPostEnum = 0;
  fast_explorer::core::EnumerationError err =
      fast_explorer::core::EnumerationError::None;
};

RunOutcome runOnce(fast_explorer::core::IFsBackend& backend,
                   const std::wstring& displayPath) {
  using namespace fast_explorer::core;

  RunOutcome out;
  FileModelStore store(displayPath);
  DirectoryEnumerator enumerator;
  uint64_t observed = 0;
  auto onBatch = [&observed](std::size_t /*start*/, std::size_t count) {
    observed += count;
  };

  std::stop_source source;
  LARGE_INTEGER t0{};
  LARGE_INTEGER t1{};
  QueryPerformanceCounter(&t0);
  out.err =
      enumerator.run(backend, displayPath, source.get_token(), store, onBatch);
  QueryPerformanceCounter(&t1);

  out.run.microseconds = ticksToMicros(
      static_cast<uint64_t>(t1.QuadPart - t0.QuadPart));
  out.run.entriesObserved = observed;
  out.storeEntriesBytes = store.entriesBytes();
  out.storeArenaCommittedBytes = store.nameArenaCommittedBytes();
  // Sample working set while the store is still alive; on the next
  // line scope-exit destroys it and the post-destroy reading would
  // be lower than the actual enumeration cost.
  out.workingSetBytesPostEnum =
      fast_explorer::core::ProcessMemoryService::workingSetBytes();
  return out;
}

}  // namespace

const wchar_t* enumerationBenchErrorName(EnumerationBenchError e) {
  switch (e) {
    case EnumerationBenchError::None: return L"None";
    case EnumerationBenchError::PathInvalid: return L"PathInvalid";
    case EnumerationBenchError::OpenFailed: return L"OpenFailed";
    case EnumerationBenchError::Internal: return L"Internal";
  }
  return L"Unknown";
}

Percentiles computePercentiles(std::vector<uint64_t> samples) {
  Percentiles out{};
  if (samples.empty()) {
    return out;
  }
  std::sort(samples.begin(), samples.end());
  const size_t n = samples.size();
  if (n % 2 == 1) {
    out.median = samples[n / 2];
  } else {
    out.median = (samples[n / 2 - 1] + samples[n / 2]) / 2;
  }
  // NIST nearest-rank for p95: index = ceil(0.95 * n), 1-based.
  size_t rank = (n * 95 + 99) / 100;  // ceil(0.95 * n)
  if (rank == 0) {
    rank = 1;
  }
  if (rank > n) {
    rank = n;
  }
  out.p95 = samples[rank - 1];
  return out;
}

EnumerationBenchResult runEnumerationBench(const std::wstring& path,
                                           int runs) {
  EnumerationBenchResult result;
  if (path.empty() || runs <= 0) {
    result.error = EnumerationBenchError::PathInvalid;
    return result;
  }

  // Confirm the path is convertible to internal form before paying for
  // any backend setup. Real enumeration goes through the backend, which
  // re-validates and applies the \\?\ prefix on its own.
  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toInternal;
  std::wstring internalCheck;
  if (toInternal(path, internalCheck) != PathConvertError::None) {
    result.error = EnumerationBenchError::PathInvalid;
    result.errorDetail = path;
    return result;
  }

  fast_explorer::core::Win32FsBackend backend;
  result.workingSet.baselineBytes =
      fast_explorer::core::ProcessMemoryService::workingSetBytes();
  result.runs.reserve(static_cast<size_t>(runs));
  for (int i = 0; i < runs; ++i) {
    const RunOutcome outcome = runOnce(backend, path);
    // A partial result (e.g. access_denied after first batch) is
    // useful for benchmarking — keep it. Total failure with zero
    // entries is the only case that aborts.
    if (outcome.err != fast_explorer::core::EnumerationError::None &&
        outcome.run.entriesObserved == 0) {
      result.error = EnumerationBenchError::OpenFailed;
      result.errorDetail = path;
      return result;
    }
    result.runs.push_back(outcome.run);
    result.totalEntries = outcome.run.entriesObserved;
    result.lastRunEntriesBytes = outcome.storeEntriesBytes;
    result.lastRunArenaCommittedBytes = outcome.storeArenaCommittedBytes;
    if (outcome.workingSetBytesPostEnum > result.workingSet.peakBytes) {
      result.workingSet.peakBytes = outcome.workingSetBytesPostEnum;
    }
  }
  result.workingSet.finalBytes =
      fast_explorer::core::ProcessMemoryService::workingSetBytes();

  std::vector<uint64_t> samples;
  samples.reserve(result.runs.size());
  for (const auto& r : result.runs) {
    samples.push_back(r.microseconds);
  }
  const Percentiles p = computePercentiles(std::move(samples));
  result.medianMicroseconds = p.median;
  result.p95Microseconds = p.p95;
  return result;
}

}  // namespace fast_explorer::bench
