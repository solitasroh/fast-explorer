#include "test-harness.h"

#include <cstddef>
#include <string>
#include <vector>

#include "explorer/stall-probe.h"

using fast_explorer::ui::classifyStall;
using fast_explorer::ui::dumpStallHistogram;
using fast_explorer::ui::StallHistogram;
using fast_explorer::ui::StallLevel;

FE_TEST_CASE(StallProbe_Zero_None) {
  FE_ASSERT_EQ(classifyStall(0), StallLevel::None);
}

FE_TEST_CASE(StallProbe_BelowInfo_None) {
  FE_ASSERT_EQ(classifyStall(49'999), StallLevel::None);
}

FE_TEST_CASE(StallProbe_AtInfoThreshold_Info) {
  FE_ASSERT_EQ(classifyStall(50'000), StallLevel::Info);
  FE_ASSERT_EQ(classifyStall(99'999), StallLevel::Info);
}

FE_TEST_CASE(StallProbe_AtWarnThreshold_Warn) {
  FE_ASSERT_EQ(classifyStall(100'000), StallLevel::Warn);
  FE_ASSERT_EQ(classifyStall(499'999), StallLevel::Warn);
}

FE_TEST_CASE(StallProbe_AtErrorThreshold_Error) {
  FE_ASSERT_EQ(classifyStall(500'000), StallLevel::Error);
  FE_ASSERT_EQ(classifyStall(5'000'000), StallLevel::Error);
}

FE_TEST_CASE(StallHistogram_FreshState_AllZero) {
  StallHistogram h;
  FE_ASSERT_EQ(h.totalDispatches(), 0ULL);
  FE_ASSERT_EQ(h.maxGapMicros(), 0ULL);
  for (std::size_t i = 0; i < StallHistogram::kBucketCount; ++i) {
    FE_ASSERT_EQ(h.bucketCount(i), 0ULL);
  }
}

FE_TEST_CASE(StallHistogram_Record_BucketsByBoundary) {
  StallHistogram h;
  h.record(500);          // [0, 1ms)        → bucket 0
  h.record(2'500);         // [1ms, 5ms)      → bucket 1
  h.record(10'000);        // [5ms, 16ms)     → bucket 2
  h.record(30'000);        // [16ms, 50ms)    → bucket 3
  h.record(75'000);        // [50ms, 100ms)   → bucket 4
  h.record(250'000);       // [100ms, 500ms)  → bucket 5
  h.record(750'000);       // [500ms, ∞)      → bucket 6
  FE_ASSERT_EQ(h.totalDispatches(), 7ULL);
  for (std::size_t i = 0; i < StallHistogram::kBucketCount; ++i) {
    FE_ASSERT_EQ(h.bucketCount(i), 1ULL);
  }
}

FE_TEST_CASE(StallHistogram_BoundaryExactlyAtUpper_GoesToNextBucket) {
  // The upper boundary is exclusive: a sample equal to the
  // boundary value lands in the next bucket.
  StallHistogram h;
  h.record(1'000);    // == kBucketUpperMicros[0] (1ms boundary)
  h.record(5'000);    // == kBucketUpperMicros[1] (5ms boundary)
  FE_ASSERT_EQ(h.bucketCount(0), 0ULL);
  FE_ASSERT_EQ(h.bucketCount(1), 1ULL);
  FE_ASSERT_EQ(h.bucketCount(2), 1ULL);
}

FE_TEST_CASE(StallHistogram_MaxGap_TracksLargest) {
  StallHistogram h;
  h.record(1'000'000);
  h.record(500);
  h.record(2'000'000);
  h.record(700);
  FE_ASSERT_EQ(h.maxGapMicros(), 2'000'000ULL);
}

FE_TEST_CASE(StallHistogram_Dump_HeaderAndBuckets) {
  StallHistogram h;
  h.record(100);
  h.record(60'000);
  std::vector<std::wstring> lines;
  dumpStallHistogram(h, [](const wchar_t* line, void* userData) {
    auto* out = static_cast<std::vector<std::wstring>*>(userData);
    out->emplace_back(line);
  }, &lines);
  // 1 header + 7 bucket rows.
  FE_ASSERT_EQ(lines.size(), static_cast<std::size_t>(1 + StallHistogram::kBucketCount));
  // Header carries the totals.
  FE_ASSERT_TRUE(lines[0].find(L"dispatches=2") != std::wstring::npos);
  FE_ASSERT_TRUE(lines[0].find(L"max=60000") != std::wstring::npos);
}
