#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fast_explorer::ui {

enum class StallLevel : uint8_t {
  None,
  Info,
  Warn,
  Error,
};

inline constexpr uint64_t kStallInfoMicros  = 50'000;
inline constexpr uint64_t kStallWarnMicros  = 100'000;
inline constexpr uint64_t kStallErrorMicros = 500'000;

StallLevel classifyStall(uint64_t gapMicros);

// UI-thread-only aggregator over message-pump dispatch latencies.
// The message loop is single-threaded by Win32 contract so no
// synchronization is needed; the histogram is consumed once on
// shutdown for the §14.7 stall distribution gate.
//
// Bucket boundaries (upper-exclusive except the last):
//   [0, 1ms), [1, 5ms), [5, 16ms), [16, 50ms),
//   [50, 100ms), [100, 500ms), [500ms, inf)
// 50ms / 100ms / 500ms align with kStall{Info,Warn,Error}Micros so
// the buckets boundary-cross at the same levels classifyStall uses.
class StallHistogram {
 public:
  static constexpr std::size_t kBucketCount = 7;
  static constexpr std::array<uint64_t, kBucketCount - 1>
      kBucketUpperMicros = {1'000,  5'000,   16'000,  50'000,
                            100'000, 500'000};

  // Records one dispatch sample. Updates the bucket count for the
  // matching range and the running max-gap.
  void record(uint64_t gapMicros) noexcept;

  std::uint64_t totalDispatches() const noexcept { return total_; }
  std::uint64_t maxGapMicros() const noexcept { return maxGap_; }
  std::uint64_t bucketCount(std::size_t index) const noexcept {
    return index < kBucketCount ? buckets_[index] : 0;
  }

 private:
  std::array<std::uint64_t, kBucketCount> buckets_{};
  std::uint64_t total_ = 0;
  std::uint64_t maxGap_ = 0;
};

// Sink-style dump used so the probe stays free of logger / file
// deps. Callback receives one already-formatted UTF-16 line per
// histogram row, mirroring PerfTracker::LineSink.
using StallHistogramLineSink = void (*)(const wchar_t* line, void* userData);

void dumpStallHistogram(const StallHistogram& hist,
                        StallHistogramLineSink sink, void* userData);

}  // namespace fast_explorer::ui
