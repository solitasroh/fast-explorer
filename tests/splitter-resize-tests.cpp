#include "test-harness.h"
#include "ui/pane-splitter.h"
#include "ui/splitter-resize.h"

using fast_explorer::ui::computeNewRatio;
using fast_explorer::ui::computeNewCumulativeRatio;
using fast_explorer::ui::computeRatioFromCursor;

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

// computeRatioFromCursor — the helper used by the live splitter HWND on
// WM_LBUTTONUP. Locks the contract that the cursor's absolute position
// in parent-client coords maps to a ratio in [0.1, 0.9] *relative to
// the splitter's logical axis* (origin + length), so horizontal
// splitters interpret the cursor against `totalH = clientH - statusH`,
// not the full client height.

FE_TEST_CASE(ComputeRatioFromCursor_Vertical_HalfClient_ReturnsHalf) {
  // Vertical splitter: axisOrigin=0, axisLength=clientW=1280.
  // Cursor at x=640 → ratio=0.5 (no offset to subtract).
  const float r = computeRatioFromCursor(640, 0, 1280);
  FE_ASSERT_TRUE(approxEq(r, 0.5f));
}

FE_TEST_CASE(ComputeRatioFromCursor_Horizontal_RespectsAxisOrigin) {
  // Horizontal splitter in a window with reservedTop=40 (address bar)
  // and statusH=22: axisOrigin=40, axisLength=clientH-22-40=738.
  // Cursor at parent-y=409 (= 40 + 369) → ratio = 369/738 = 0.5.
  const float r = computeRatioFromCursor(409, 40, 738);
  FE_ASSERT_TRUE(approxEq(r, 0.5f));
}

FE_TEST_CASE(ComputeRatioFromCursor_Horizontal_OffByStatusBar_FixedShape) {
  // Regression: the v0.4.0 bug computed ratio against the FULL client
  // height (axisLength=clientH) for a horizontal splitter, which made
  // the drop position drift by ~statusH pixels. With the corrected
  // axisLength=totalH (=clientH-statusH=778) a cursor at y=389 maps
  // to exactly 389/778 = 0.5 — the visual centre of the pane region.
  const float r = computeRatioFromCursor(389, 0, 778);
  FE_ASSERT_TRUE(r > 0.499f && r < 0.501f);
}

FE_TEST_CASE(ComputeRatioFromCursor_ClampMin) {
  // Dragging well above the pane region must clamp at 0.1 so the
  // splitter never collapses a pane to zero.
  const float r = computeRatioFromCursor(0, 0, 778);
  FE_ASSERT_TRUE(approxEq(r, 0.1f));
}

FE_TEST_CASE(ComputeRatioFromCursor_ClampMax) {
  // Symmetric upper clamp at 0.9.
  const float r = computeRatioFromCursor(9999, 0, 778);
  FE_ASSERT_TRUE(approxEq(r, 0.9f));
}

FE_TEST_CASE(ComputeRatioFromCursor_AxisLengthZero_ReturnsHalf) {
  // Degenerate (window not yet sized): return a sane midpoint instead
  // of dividing by zero.
  const float r = computeRatioFromCursor(123, 0, 0);
  FE_ASSERT_TRUE(approxEq(r, 0.5f));
}
