#pragma once

namespace fast_explorer::ui {

// Re-derive a split-line ratio after the user drags the splitter from
// `startMouseAlongAxis` to `currentMouseAlongAxis` along an axis of
// `axisLength` pixels. `startRatio` is the ratio at drag start.
// Clamped to [minRatio, maxRatio] so no pane collapses to zero (and
// stays recoverable by drag-back). Pure, constexpr.
[[nodiscard]] constexpr float computeNewRatio(
    float startRatio,
    int startMouseAlongAxis,
    int currentMouseAlongAxis,
    int axisLength,
    float minRatio = 0.1f,
    float maxRatio = 0.9f) noexcept {
  if (axisLength <= 0) return startRatio;
  const float delta = static_cast<float>(currentMouseAlongAxis -
                                          startMouseAlongAxis) /
                      static_cast<float>(axisLength);
  float out = startRatio + delta;
  if (out < minRatio) out = minRatio;
  if (out > maxRatio) out = maxRatio;
  return out;
}

// Variant for cumulative-ratio presets (Quad_B / Quad_C / Quad_D):
// the splitter's ratio must stay strictly between its neighbors so
// columns/rows cannot invert. `prevRatio` and `nextRatio` are the
// ratios of the splitters immediately before and after this one;
// pass 0.0f / 1.0f when no neighbor exists on that side.
[[nodiscard]] constexpr float computeNewCumulativeRatio(
    float startRatio,
    int startMouseAlongAxis,
    int currentMouseAlongAxis,
    int axisLength,
    float prevRatio,
    float nextRatio,
    float minGap = 0.001f) noexcept {
  const float raw = computeNewRatio(startRatio, startMouseAlongAxis,
                                     currentMouseAlongAxis, axisLength,
                                     0.0f, 1.0f);
  const float lo = prevRatio + minGap;
  const float hi = nextRatio - minGap;
  if (raw < lo) return lo;
  if (raw > hi) return hi;
  return raw;
}

}  // namespace fast_explorer::ui
