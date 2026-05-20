#include "test-harness.h"
#include "ui/splitter-resize.h"

using fast_explorer::ui::computeNewRatio;
using fast_explorer::ui::computeNewCumulativeRatio;

namespace {
bool approxEq(float a, float b) noexcept {
  return (a > b ? a - b : b - a) < 1e-4f;
}
}

FE_TEST_CASE(ComputeNewRatio_NoMovement_ReturnsStart) {
  const float out = computeNewRatio(0.5f, 640, 640, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.5f));
}

FE_TEST_CASE(ComputeNewRatio_RightMove_IncreasesRatio) {
  const float out = computeNewRatio(0.5f, 640, 800, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.625f));
}

FE_TEST_CASE(ComputeNewRatio_ClampMin) {
  const float out = computeNewRatio(0.5f, 640, -2000, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.1f));
}

FE_TEST_CASE(ComputeNewRatio_ClampMax) {
  const float out = computeNewRatio(0.5f, 640, 9999, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.9f));
}

FE_TEST_CASE(ComputeNewRatio_AxisLengthZero_ReturnsStart) {
  const float out = computeNewRatio(0.3f, 100, 200, 0);
  FE_ASSERT_TRUE(approxEq(out, 0.3f));
}

FE_TEST_CASE(ComputeNewCumulativeRatio_RespectsNeighborBands_FivePercentGap) {
  // 4-column layout, dragging the middle splitter (ratios[1] start 0.5)
  // with neighbors at 0.25 and 0.75. Drag past neighbor must clamp to
  // nextRatio - 0.05 = 0.70 (the default 5% gap).
  const float out = computeNewCumulativeRatio(0.5f, 640, 1100, 1280,
                                              /*prevRatio*/ 0.25f,
                                              /*nextRatio*/ 0.75f);
  // 1100/1280 ~= 0.859 -> clamp at 0.75 - 0.05 = 0.70
  FE_ASSERT_TRUE(out > 0.699f && out < 0.701f);
}

FE_TEST_CASE(ComputeNewCumulativeRatio_LeftClamp_FivePercentGap) {
  // Drag toward left neighbor must clamp at prevRatio + 0.05 = 0.30.
  const float out = computeNewCumulativeRatio(0.5f, 640, 100, 1280,
                                              0.25f, 0.75f);
  FE_ASSERT_TRUE(out > 0.299f && out < 0.301f);
}

FE_TEST_CASE(ComputeNewCumulativeRatio_CustomMinGap_TightEpsilon) {
  // Explicit small minGap for tests that need precision near neighbors.
  const float out = computeNewCumulativeRatio(0.5f, 640, 1100, 1280,
                                              0.25f, 0.75f, /*minGap*/ 0.001f);
  FE_ASSERT_TRUE(out > 0.748f && out < 0.75f);
}
