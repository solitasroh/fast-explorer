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

FE_TEST_CASE(ComputeNewCumulativeRatio_RespectsNeighborBands) {
  const float out = computeNewCumulativeRatio(0.5f, 640, 1100, 1280,
                                              0.25f, 0.75f);
  FE_ASSERT_TRUE(out < 0.75f);
  FE_ASSERT_TRUE(out > 0.74f);
}

FE_TEST_CASE(ComputeNewCumulativeRatio_LeftClamp) {
  const float out = computeNewCumulativeRatio(0.5f, 640, 100, 1280,
                                              0.25f, 0.75f);
  FE_ASSERT_TRUE(out > 0.25f);
  FE_ASSERT_TRUE(out < 0.26f);
}
