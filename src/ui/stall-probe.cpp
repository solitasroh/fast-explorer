#include "ui/stall-probe.h"

#include <stdio.h>
#include <wchar.h>

namespace fast_explorer::ui {

StallLevel classifyStall(uint64_t gapMicros) {
  if (gapMicros >= kStallErrorMicros) return StallLevel::Error;
  if (gapMicros >= kStallWarnMicros)  return StallLevel::Warn;
  if (gapMicros >= kStallInfoMicros)  return StallLevel::Info;
  return StallLevel::None;
}

void StallHistogram::record(uint64_t gapMicros) noexcept {
  ++total_;
  if (gapMicros > maxGap_) {
    maxGap_ = gapMicros;
  }
  for (std::size_t i = 0; i < kBucketUpperMicros.size(); ++i) {
    if (gapMicros < kBucketUpperMicros[i]) {
      ++buckets_[i];
      return;
    }
  }
  // gapMicros >= last boundary (500 ms) → final bucket.
  ++buckets_[kBucketCount - 1];
}

void dumpStallHistogram(const StallHistogram& hist,
                        StallHistogramLineSink sink, void* userData) {
  if (sink == nullptr) {
    return;
  }
  constexpr std::size_t kLineCap = 128;
  wchar_t header[kLineCap];
  _snwprintf_s(header, kLineCap, _TRUNCATE,
               L"[stall-histogram] dispatches=%llu  max=%llu us",
               static_cast<unsigned long long>(hist.totalDispatches()),
               static_cast<unsigned long long>(hist.maxGapMicros()));
  sink(header, userData);

  // Match the kBucketUpperMicros layout in the header.
  static const wchar_t* const kLabels[StallHistogram::kBucketCount] = {
      L"  <1ms     :",
      L"  1-5ms    :",
      L"  5-16ms   :",
      L"  16-50ms  :",
      L"  50-100ms :",
      L"  100-500ms:",
      L"  >=500ms  :",
  };
  for (std::size_t i = 0; i < StallHistogram::kBucketCount; ++i) {
    wchar_t line[kLineCap];
    _snwprintf_s(line, kLineCap, _TRUNCATE, L"%ls %llu", kLabels[i],
                 static_cast<unsigned long long>(hist.bucketCount(i)));
    sink(line, userData);
  }
}

}  // namespace fast_explorer::ui
