#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bench/enumeration-bench.h"

namespace fast_explorer::bench {

struct HeadToHeadResult {
  std::vector<EnumerationRun> findRuns;
  std::vector<EnumerationRun> gfibheRuns;
  uint64_t findMedianUs = 0;
  uint64_t findP95Us = 0;
  uint64_t gfibheMedianUs = 0;
  uint64_t gfibheP95Us = 0;
  // Percent * 100 (two-decimal fixed-point). Positive = gfibhe faster.
  int32_t gfibhePercentFasterX100 = 0;
  EnumerationBenchError error = EnumerationBenchError::None;
  std::wstring errorDetail;
};

// `internalPath` must be in \\?\ form. `*okOut` = false on open
// failure or mid-walk error; the count up to that point is returned.
uint64_t enumerateFindFirstRaw(const std::wstring& internalPath, bool* okOut);
uint64_t enumerateGfibheRaw(const std::wstring& internalPath, bool* okOut);

HeadToHeadResult runHeadToHeadBench(const std::wstring& path, int runs);

}  // namespace fast_explorer::bench
