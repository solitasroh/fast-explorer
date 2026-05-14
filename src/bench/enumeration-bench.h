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

struct EnumerationBenchResult {
  std::vector<EnumerationRun> runs;
  uint64_t medianMicroseconds = 0;
  uint64_t p95Microseconds = 0;
  uint64_t totalEntries = 0;
  EnumerationBenchError error = EnumerationBenchError::None;
  std::wstring errorDetail;
};

const wchar_t* enumerationBenchErrorName(EnumerationBenchError e);

// Pure helper. Caller passes microseconds samples; we sort a copy and
// return median + p95 using the NIST nearest-rank method.
Percentiles computePercentiles(std::vector<uint64_t> samples);

EnumerationBenchResult runEnumerationBench(const std::wstring& path, int runs);

}  // namespace fast_explorer::bench
