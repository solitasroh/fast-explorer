#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "bench/enumeration-bench.h"
#include "test-harness.h"

using fast_explorer::bench::computePercentiles;
using fast_explorer::bench::EnumerationBenchError;
using fast_explorer::bench::EnumerationBenchResult;
using fast_explorer::bench::generateDataset;
using fast_explorer::bench::GenerateError;
using fast_explorer::bench::Percentiles;
using fast_explorer::bench::PresetKind;
using fast_explorer::bench::runEnumerationBench;
using fast_explorer::tests::makeFreshTempDirPath;
using fast_explorer::tests::removeDirectoryRecursive;

FE_TEST_CASE(Percentiles_Empty_BothZero) {
  Percentiles p = computePercentiles({});
  FE_ASSERT_EQ(p.median, 0ULL);
  FE_ASSERT_EQ(p.p95, 0ULL);
}

FE_TEST_CASE(Percentiles_SingleSample_BothEqual) {
  Percentiles p = computePercentiles({42});
  FE_ASSERT_EQ(p.median, 42ULL);
  FE_ASSERT_EQ(p.p95, 42ULL);
}

FE_TEST_CASE(Percentiles_FiveSamples_MidpointAndMax) {
  Percentiles p = computePercentiles({10, 20, 30, 40, 50});
  FE_ASSERT_EQ(p.median, 30ULL);
  FE_ASSERT_EQ(p.p95, 50ULL);
}

FE_TEST_CASE(Percentiles_OutOfOrder_StillCorrect) {
  Percentiles p = computePercentiles({50, 10, 40, 20, 30});
  FE_ASSERT_EQ(p.median, 30ULL);
  FE_ASSERT_EQ(p.p95, 50ULL);
}

FE_TEST_CASE(Percentiles_TenSamples_MedianAverages) {
  Percentiles p = computePercentiles({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  // Median of even N = (5 + 6) / 2 = 5
  FE_ASSERT_EQ(p.median, 5ULL);
  // p95: ceil(0.95 * 10) = 10 → samples[9] = 10
  FE_ASSERT_EQ(p.p95, 10ULL);
}

FE_TEST_CASE(Percentiles_TwentySamples_Standard) {
  std::vector<uint64_t> s;
  for (uint64_t i = 1; i <= 20; ++i) {
    s.push_back(i);
  }
  Percentiles p = computePercentiles(std::move(s));
  // Median = (10 + 11) / 2 = 10
  FE_ASSERT_EQ(p.median, 10ULL);
  // p95: ceil(0.95 * 20) = 19 → samples[18] = 19
  FE_ASSERT_EQ(p.p95, 19ULL);
}

FE_TEST_CASE(EnumerationBench_EmptyPath_ReturnsPathInvalid) {
  EnumerationBenchResult r = runEnumerationBench(L"", 5);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::PathInvalid);
}

FE_TEST_CASE(EnumerationBench_ZeroRuns_ReturnsPathInvalid) {
  EnumerationBenchResult r = runEnumerationBench(L"C:\\tmp", 0);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::PathInvalid);
}

FE_TEST_CASE(EnumerationBench_NonexistentPath_ReturnsOpenFailed) {
  EnumerationBenchResult r =
      runEnumerationBench(L"C:\\__fe_no_such_path__99999", 2);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::OpenFailed);
}

FE_TEST_CASE(EnumerationBench_SmallDataset_ReportsTwoHundredEntries) {
  const std::wstring dir = makeFreshTempDirPath(L"enumbench");
  const auto gen = generateDataset(PresetKind::Small, dir, 1);
  if (gen.error != GenerateError::None) {
    removeDirectoryRecursive(dir);
    FE_ASSERT_TRUE(false);
  }
  EnumerationBenchResult r = runEnumerationBench(dir, 3);
  removeDirectoryRecursive(dir);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::None);
  FE_ASSERT_EQ(r.runs.size(), 3ULL);
  FE_ASSERT_EQ(r.totalEntries, 200ULL);
  FE_ASSERT_TRUE(r.medianMicroseconds > 0);
  FE_ASSERT_TRUE(r.p95Microseconds >= r.medianMicroseconds);
  FE_ASSERT_TRUE(r.lastRunEntriesBytes > 0);
  FE_ASSERT_TRUE(r.lastRunArenaCommittedBytes > 0);
}

FE_TEST_CASE(EnumerationBench_WorkingSet_BaselineNonZeroPeakGreaterOrEqual) {
  const std::wstring dir = makeFreshTempDirPath(L"enumbench-ws");
  const auto gen = generateDataset(PresetKind::Small, dir, 1);
  if (gen.error != GenerateError::None) {
    removeDirectoryRecursive(dir);
    FE_ASSERT_TRUE(false);
  }
  EnumerationBenchResult r = runEnumerationBench(dir, 2);
  removeDirectoryRecursive(dir);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::None);
  // GetProcessMemoryInfo returns non-zero for a live process.
  FE_ASSERT_TRUE(r.workingSet.baselineBytes > 0);
  // Peak is sampled after enumeration while the store is alive, so
  // it must be at least as large as the baseline (the store +
  // enumerator state never shrink the working set below the
  // pre-run reading).
  FE_ASSERT_TRUE(r.workingSet.peakBytes >= r.workingSet.baselineBytes);
  // Final is sampled after every store is destroyed; it may or may
  // not match baseline depending on OS reclaim, but it is a live
  // process so it remains positive.
  FE_ASSERT_TRUE(r.workingSet.finalBytes > 0);
}

FE_TEST_CASE(EnumerationBench_Soak_PostCyclePopulatedPerRun) {
  const std::wstring dir = makeFreshTempDirPath(L"enumbench-soak");
  const auto gen = generateDataset(PresetKind::Small, dir, 1);
  if (gen.error != GenerateError::None) {
    removeDirectoryRecursive(dir);
    FE_ASSERT_TRUE(false);
  }
  // 5 cycles is enough to verify the field shape without a long
  // soak; the §14.7 gate runs 10 cycles against the 100k preset
  // and is exercised through bench-cli rather than this unit test.
  constexpr int kCycles = 5;
  EnumerationBenchResult r = runEnumerationBench(dir, kCycles);
  removeDirectoryRecursive(dir);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::None);
  FE_ASSERT_EQ(r.workingSet.postCycleBytes.size(),
               static_cast<std::size_t>(kCycles));
  for (uint64_t cycleBytes : r.workingSet.postCycleBytes) {
    FE_ASSERT_TRUE(cycleBytes > 0);
  }
  // maxCycleDriftBytes is the max of (postCycle - baseline) clamped
  // to 0 for cycles below baseline. By construction it equals the
  // max over postCycleBytes minus baselineBytes, when positive.
  uint64_t expectedMax = 0;
  for (uint64_t cycleBytes : r.workingSet.postCycleBytes) {
    if (cycleBytes > r.workingSet.baselineBytes) {
      const uint64_t d = cycleBytes - r.workingSet.baselineBytes;
      if (d > expectedMax) {
        expectedMax = d;
      }
    }
  }
  FE_ASSERT_EQ(r.workingSet.maxCycleDriftBytes, expectedMax);
}
