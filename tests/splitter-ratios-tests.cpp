#include "test-harness.h"
#include "winui_lite/chrome/splitter-ratios.h"

using fast_explorer::core::LayoutPreset;
using fast_explorer::ui::defaultRatiosFor;
using fast_explorer::ui::SplitterRatios;

namespace {
bool approxEq(float a, float b) noexcept {
  return (a > b ? a - b : b - a) < 1e-4f;
}
}

FE_TEST_CASE(DefaultRatios_DualV_HalfHalf) {
  const auto r = defaultRatiosFor(LayoutPreset::Dual_V);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.5f));
}

FE_TEST_CASE(DefaultRatios_TriA_VerticalSeam40_RightInnerHorizontal50) {
  const auto r = defaultRatiosFor(LayoutPreset::Tri_A);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.4f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.5f));
}

FE_TEST_CASE(DefaultRatios_TriC_ThirdsApproximate) {
  const auto r = defaultRatiosFor(LayoutPreset::Tri_C);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 1.0f / 3.0f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 2.0f / 3.0f));
}

FE_TEST_CASE(DefaultRatios_QuadA_AllHalf) {
  const auto r = defaultRatiosFor(LayoutPreset::Quad_A);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.5f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.5f));
  FE_ASSERT_TRUE(approxEq(r.ratios[2], 0.5f));
}

FE_TEST_CASE(DefaultRatios_QuadB_QuartileColumns) {
  const auto r = defaultRatiosFor(LayoutPreset::Quad_B);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.25f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.5f));
  FE_ASSERT_TRUE(approxEq(r.ratios[2], 0.75f));
}

FE_TEST_CASE(DefaultRatios_Single_AllZero_Unused) {
  const auto r = defaultRatiosFor(LayoutPreset::Single);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.0f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.0f));
  FE_ASSERT_TRUE(approxEq(r.ratios[2], 0.0f));
}
