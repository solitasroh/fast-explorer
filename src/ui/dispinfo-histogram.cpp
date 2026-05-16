#include "ui/dispinfo-histogram.h"

#include <stdio.h>
#include <wchar.h>

#include <climits>
#include <limits>

namespace fast_explorer::ui {

void DispInfoHistogram::record(std::uint64_t latencyMicros) noexcept {
  ++total_;
  if (latencyMicros > maxLatency_) {
    maxLatency_ = latencyMicros;
  }
  for (std::size_t i = 0; i < kBucketUpperMicros.size(); ++i) {
    if (latencyMicros < kBucketUpperMicros[i]) {
      ++buckets_[i];
      return;
    }
  }
  // latencyMicros >= 500 → final bucket
  ++buckets_[kBucketCount - 1];
}

void DispInfoHistogram::recordTicks(std::uint64_t deltaTicks,
                                    std::uint64_t qpcFrequencyHz) noexcept {
  if (qpcFrequencyHz == 0) {
    record(0);
    return;
  }
  // (delta * 1e6) / hz — delta for a single GETDISPINFO call is at
  // most milliseconds-worth of QPC ticks, so the multiplication
  // does not overflow a uint64_t even at the highest plausible
  // frequencies.
  record((deltaTicks * 1'000'000ULL) / qpcFrequencyHz);
}

std::uint64_t DispInfoHistogram::p99EstimateMicros() const noexcept {
  if (total_ == 0) {
    return 0;
  }
  // ceil(0.99 * total) — the smallest rank whose cumulative count
  // is >= 99% of the samples.
  const std::uint64_t target = (total_ * 99ULL + 99ULL) / 100ULL;
  std::uint64_t cumulative = 0;
  for (std::size_t i = 0; i < kBucketCount; ++i) {
    cumulative += buckets_[i];
    if (cumulative >= target) {
      if (i + 1 < kBucketCount) {
        return kBucketUpperMicros[i];
      }
      // Final bucket has no upper bound — report unbounded.
      return std::numeric_limits<std::uint64_t>::max();
    }
  }
  // Should be unreachable when total_ > 0, but stay defensive.
  return std::numeric_limits<std::uint64_t>::max();
}

void dumpDispInfoHistogram(const DispInfoHistogram& hist,
                           DispInfoHistogramLineSink sink, void* userData) {
  if (sink == nullptr) {
    return;
  }
  constexpr std::size_t kLineCap = 128;
  const std::uint64_t p99 = hist.p99EstimateMicros();
  wchar_t header[kLineCap];
  if (p99 == std::numeric_limits<std::uint64_t>::max()) {
    _snwprintf_s(header, kLineCap, _TRUNCATE,
                 L"[dispinfo-histogram] samples=%llu  max=%llu us  p99>=500us",
                 static_cast<unsigned long long>(hist.totalSamples()),
                 static_cast<unsigned long long>(hist.maxLatencyMicros()));
  } else {
    _snwprintf_s(header, kLineCap, _TRUNCATE,
                 L"[dispinfo-histogram] samples=%llu  max=%llu us  p99<=%llu us",
                 static_cast<unsigned long long>(hist.totalSamples()),
                 static_cast<unsigned long long>(hist.maxLatencyMicros()),
                 static_cast<unsigned long long>(p99));
  }
  sink(header, userData);

  static const wchar_t* const kLabels[DispInfoHistogram::kBucketCount] = {
      L"  <1us    :",
      L"  1-5us   :",
      L"  5-20us  :",
      L"  20-50us :",
      L"  50-100us:",
      L"  100-500us:",
      L"  >=500us :",
  };
  for (std::size_t i = 0; i < DispInfoHistogram::kBucketCount; ++i) {
    wchar_t line[kLineCap];
    _snwprintf_s(line, kLineCap, _TRUNCATE, L"%ls %llu", kLabels[i],
                 static_cast<unsigned long long>(hist.bucketCount(i)));
    sink(line, userData);
  }
}

}  // namespace fast_explorer::ui
