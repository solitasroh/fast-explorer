#include "test-harness.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "winui_lite/chrome/dispinfo-histogram.h"

using fast_explorer::ui::DispInfoHistogram;
using fast_explorer::ui::dumpDispInfoHistogram;

FE_TEST_CASE(DispInfoHistogram_FreshState_ZeroSamples) {
  DispInfoHistogram h;
  FE_ASSERT_EQ(h.totalSamples(), 0ULL);
  FE_ASSERT_EQ(h.maxLatencyMicros(), 0ULL);
  FE_ASSERT_EQ(h.p99EstimateMicros(), 0ULL);
}

FE_TEST_CASE(DispInfoHistogram_Record_BucketsByBoundary) {
  DispInfoHistogram h;
  h.record(0);     // [0, 1)
  h.record(2);     // [1, 5)
  h.record(10);    // [5, 20)
  h.record(30);    // [20, 50)
  h.record(75);    // [50, 100)
  h.record(250);   // [100, 500)
  h.record(750);   // [500, ∞)
  FE_ASSERT_EQ(h.totalSamples(), 7ULL);
  for (std::size_t i = 0; i < DispInfoHistogram::kBucketCount; ++i) {
    FE_ASSERT_EQ(h.bucketCount(i), 1ULL);
  }
}

FE_TEST_CASE(DispInfoHistogram_BoundaryExactlyAtUpper_NextBucket) {
  DispInfoHistogram h;
  h.record(1);     // == kBucketUpperMicros[0] → bucket 1
  h.record(50);    // == kBucketUpperMicros[3] → bucket 4
  FE_ASSERT_EQ(h.bucketCount(0), 0ULL);
  FE_ASSERT_EQ(h.bucketCount(1), 1ULL);
  FE_ASSERT_EQ(h.bucketCount(3), 0ULL);
  FE_ASSERT_EQ(h.bucketCount(4), 1ULL);
}

FE_TEST_CASE(DispInfoHistogram_P99_AllUnder20us_ReturnsBucketUpper) {
  DispInfoHistogram h;
  // 100 samples in the [5, 20) bucket.
  for (int i = 0; i < 100; ++i) {
    h.record(10);
  }
  // p99 rank = ceil(0.99 * 100) = 99. Cumulative reaches 100 at
  // the first bucket containing any samples (here index 2 with
  // upper 20).
  FE_ASSERT_EQ(h.p99EstimateMicros(), 20ULL);
}

FE_TEST_CASE(DispInfoHistogram_P99_PoisonedByOneSlow_PushedToHighBucket) {
  DispInfoHistogram h;
  // 99 fast + 1 slow = 100 samples total.
  for (int i = 0; i < 99; ++i) {
    h.record(2);   // [1, 5)
  }
  h.record(800);   // [500, ∞)
  // ceil(0.99 * 100) = 99. Cumulative at bucket 1 = 99 → meets
  // target → returns kBucketUpperMicros[1] = 5.
  FE_ASSERT_EQ(h.p99EstimateMicros(), 5ULL);
}

FE_TEST_CASE(DispInfoHistogram_P99_HalfSlow_PushedAboveAll) {
  DispInfoHistogram h;
  // 50 fast + 50 slow. ceil(0.99 * 100) = 99. Need cumulative ≥ 99,
  // which only the last bucket reaches.
  for (int i = 0; i < 50; ++i) h.record(2);
  for (int i = 0; i < 50; ++i) h.record(800);
  FE_ASSERT_EQ(h.p99EstimateMicros(),
               std::numeric_limits<std::uint64_t>::max());
}

FE_TEST_CASE(DispInfoHistogram_RecordTicks_ZeroFrequency_RecordsZero) {
  DispInfoHistogram h;
  h.recordTicks(123456, 0);
  FE_ASSERT_EQ(h.totalSamples(), 1ULL);
  FE_ASSERT_EQ(h.bucketCount(0), 1ULL);  // 0 µs → <1us bucket
  FE_ASSERT_EQ(h.maxLatencyMicros(), 0ULL);
}

FE_TEST_CASE(DispInfoHistogram_Dump_EmitsHeaderAndBuckets) {
  DispInfoHistogram h;
  h.record(3);
  h.record(40);
  std::vector<std::wstring> lines;
  dumpDispInfoHistogram(h, [](const wchar_t* line, void* userData) {
    auto* out = static_cast<std::vector<std::wstring>*>(userData);
    out->emplace_back(line);
  }, &lines);
  // header + 7 buckets
  FE_ASSERT_EQ(lines.size(),
               static_cast<std::size_t>(1 + DispInfoHistogram::kBucketCount));
  FE_ASSERT_TRUE(lines[0].find(L"samples=2") != std::wstring::npos);
  FE_ASSERT_TRUE(lines[0].find(L"max=40") != std::wstring::npos);
}
