#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fast_explorer::ui {

// UI-thread-only aggregator over LVN_GETDISPINFO handler
// latencies. The list-view paints virtualized rows by calling
// the GETDISPINFO handler per visible row per repaint; the
// §14.3 gate (carry-forward to M7) targets a p99 ≤ 50 µs over
// a 100k scroll soak. The histogram buckets the per-call
// latencies so the gate can be observed without storing raw
// samples (a million-call soak would otherwise blow up the
// log).
//
// Bucket boundaries (upper-exclusive except the last) in μs:
//   [0,1) [1,5) [5,20) [20,50) [50,100) [100,500) [500,∞)
// 50 μs aligns with the §14.3 gate boundary so the p99 estimate
// crosses bucket lines at the gate.
class DispInfoHistogram {
 public:
  static constexpr std::size_t kBucketCount = 7;
  static constexpr std::array<std::uint64_t, kBucketCount - 1>
      kBucketUpperMicros = {1, 5, 20, 50, 100, 500};

  // Single sample. Caller is on the UI thread; no synchronization
  // applies.
  void record(std::uint64_t latencyMicros) noexcept;

  // Convenience: take a QPC tick delta + cached frequency in Hz
  // and convert before recording. Frequency 0 records as 0.
  void recordTicks(std::uint64_t deltaTicks,
                   std::uint64_t qpcFrequencyHz) noexcept;

  std::uint64_t totalSamples() const noexcept { return total_; }
  std::uint64_t maxLatencyMicros() const noexcept { return maxLatency_; }
  std::uint64_t bucketCount(std::size_t index) const noexcept {
    return index < kBucketCount ? buckets_[index] : 0;
  }

  // Returns the upper bound of the bucket containing the 99th
  // percentile sample (the smallest bucket whose cumulative count
  // is at least ceil(0.99 * total)). The last bucket has no upper
  // bound and reports as UINT64_MAX. Returns 0 when no samples.
  std::uint64_t p99EstimateMicros() const noexcept;

 private:
  std::array<std::uint64_t, kBucketCount> buckets_{};
  std::uint64_t total_ = 0;
  std::uint64_t maxLatency_ = 0;
};

using DispInfoHistogramLineSink = void (*)(const wchar_t* line, void* userData);

void dumpDispInfoHistogram(const DispInfoHistogram& hist,
                           DispInfoHistogramLineSink sink, void* userData);

}  // namespace fast_explorer::ui
