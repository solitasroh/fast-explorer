#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fast_explorer::bench {

enum class EnumerationBenchError : uint8_t {
  None,
  PathInvalid,
  OpenFailed,
  Internal,
};

struct EnumerationRun {
  uint64_t microseconds = 0;
  uint64_t entriesObserved = 0;
};

struct Percentiles {
  uint64_t median = 0;
  uint64_t p95 = 0;
};

// Process working-set sample envelope around the run loop.
// Headless scope: covers FileModelStore + name arena + win32
// backend + the bench's own stack. UI-thread caches (ImageList,
// ExtensionIconCache, IconProvider STA thread + COM apartment)
// are NOT included; the full-app cost is observed by running the
// UI build.
//
// The per-cycle vector + maxCycleDriftBytes are the soak signal:
// each cycle = one runOnce call where the FileModelStore is
// constructed, populated, then destroyed at scope exit. Sampling
// AFTER runOnce returns therefore captures the residual working
// set when no store is alive, and drift = postCycle - baseline
// shows whether successive open-close-open cycles reclaim back to
// the starting state (drift ≈ 0) or accumulate (drift > 0).
struct WorkingSetSamples {
  uint64_t baselineBytes = 0;   // before any run
  uint64_t peakBytes = 0;       // max post-enum across runs (store still alive)
  uint64_t finalBytes = 0;      // after all runs (stores destroyed)
  std::vector<uint64_t> postCycleBytes;  // one entry per cycle, store destroyed
  uint64_t maxCycleDriftBytes = 0;       // max(postCycleBytes[i] - baselineBytes)
};

struct EnumerationBenchResult {
  std::vector<EnumerationRun> runs;
  uint64_t medianMicroseconds = 0;
  uint64_t p95Microseconds = 0;
  uint64_t totalEntries = 0;
  // Last run's FileModelStore footprint.
  uint64_t lastRunEntriesBytes = 0;
  uint64_t lastRunArenaCommittedBytes = 0;
  WorkingSetSamples workingSet;
  EnumerationBenchError error = EnumerationBenchError::None;
  std::wstring errorDetail;
};

const wchar_t* enumerationBenchErrorName(EnumerationBenchError e);

// Pure helper. Caller passes microseconds samples; we sort a copy and
// return median + p95 using the NIST nearest-rank method.
Percentiles computePercentiles(std::vector<uint64_t> samples);

EnumerationBenchResult runEnumerationBench(const std::wstring& path, int runs);

}  // namespace fast_explorer::bench
