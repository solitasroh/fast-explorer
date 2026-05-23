#include <windows.h>

#include "test-harness.h"
#include "winui_lite/chrome/pane-layout.h"
#include "winui_lite/chrome/splitter-ratios.h"

using fast_explorer::ui::LayoutAction;
using fast_explorer::ui::LayoutOrientation;
using fast_explorer::ui::resolveLayoutToggle;

using fast_explorer::core::LayoutPreset;
using fast_explorer::ui::computePaneLayout;
using fast_explorer::ui::defaultRatiosFor;
using fast_explorer::ui::PaneLayoutResult;
using fast_explorer::ui::SplitterOrientation;

namespace {

bool rectEquals(const RECT& r, int l, int t, int rr, int b) noexcept {
  return r.left == l && r.top == t && r.right == rr && r.bottom == b;
}

bool rectZero(const RECT& r) noexcept {
  return r.left == 0 && r.top == 0 && r.right == 0 && r.bottom == 0;
}

}  // namespace

// Compile-time truth table: lock the 3-action policy at every input
// combination so a future edit to resolveLayoutToggle that breaks the
// invariant fails the build, not just the runtime test.
static_assert(resolveLayoutToggle(false, LayoutOrientation::Vertical,
                                  LayoutOrientation::Vertical).action ==
              LayoutAction::EnterDual);
static_assert(resolveLayoutToggle(false, LayoutOrientation::Vertical,
                                  LayoutOrientation::Horizontal).action ==
              LayoutAction::EnterDual);
static_assert(resolveLayoutToggle(true, LayoutOrientation::Vertical,
                                  LayoutOrientation::Vertical).action ==
              LayoutAction::ExitToSingle);
static_assert(resolveLayoutToggle(true, LayoutOrientation::Horizontal,
                                  LayoutOrientation::Horizontal).action ==
              LayoutAction::ExitToSingle);
static_assert(resolveLayoutToggle(true, LayoutOrientation::Vertical,
                                  LayoutOrientation::Horizontal).action ==
              LayoutAction::SwitchOrientation);
static_assert(resolveLayoutToggle(true, LayoutOrientation::Horizontal,
                                  LayoutOrientation::Vertical).action ==
              LayoutAction::SwitchOrientation);

FE_TEST_CASE(LayoutToggle_FromSingle_EnterDualInPressedOrientation) {
  const auto t = resolveLayoutToggle(false, LayoutOrientation::Vertical,
                                     LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(t.action == LayoutAction::EnterDual);
  FE_ASSERT_TRUE(t.target == LayoutOrientation::Horizontal);
}

FE_TEST_CASE(LayoutToggle_FromSingle_VerticalKey_EntersVertical) {
  const auto t = resolveLayoutToggle(false, LayoutOrientation::Horizontal,
                                     LayoutOrientation::Vertical);
  // currentOrientation is ignored when single — the persisted value
  // could carry the prior session's last seam but the press wins.
  FE_ASSERT_TRUE(t.action == LayoutAction::EnterDual);
  FE_ASSERT_TRUE(t.target == LayoutOrientation::Vertical);
}

FE_TEST_CASE(LayoutToggle_DualSameSeam_ExitsToSingle) {
  const auto t = resolveLayoutToggle(true, LayoutOrientation::Vertical,
                                     LayoutOrientation::Vertical);
  FE_ASSERT_TRUE(t.action == LayoutAction::ExitToSingle);
}

FE_TEST_CASE(LayoutToggle_DualOtherSeam_SwitchesOrientation) {
  const auto t = resolveLayoutToggle(true, LayoutOrientation::Vertical,
                                     LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(t.action == LayoutAction::SwitchOrientation);
  FE_ASSERT_TRUE(t.target == LayoutOrientation::Horizontal);
}

FE_TEST_CASE(LayoutToggle_DualHorizontalSameSeam_ExitsToSingle) {
  const auto t = resolveLayoutToggle(true, LayoutOrientation::Horizontal,
                                     LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(t.action == LayoutAction::ExitToSingle);
}

FE_TEST_CASE(ComputePaneLayout_Single_FullArea) {
  const auto out = computePaneLayout(LayoutPreset::Single,
                                     defaultRatiosFor(LayoutPreset::Single),
                                     1280, 800, /*top*/ 0, /*bottom*/ 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{1});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{0});
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0, 0, 1280, 778));
}

FE_TEST_CASE(ComputePaneLayout_DualV_HalfSplit_OneVerticalSplitter) {
  const auto out = computePaneLayout(LayoutPreset::Dual_V,
                                     defaultRatiosFor(LayoutPreset::Dual_V),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{2});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{1});
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0,   0, 640, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 640, 0, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
}

FE_TEST_CASE(ComputePaneLayout_DualH_HalfSplit_OneHorizontalSplitter) {
  const auto out = computePaneLayout(LayoutPreset::Dual_H,
                                     defaultRatiosFor(LayoutPreset::Dual_H),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{2});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{1});
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0, 0,   1280, 389));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 0, 389, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Horizontal);
}

FE_TEST_CASE(ComputePaneLayout_TriA_LeftFull_RightStacked_TwoSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Tri_A,
                                     defaultRatiosFor(LayoutPreset::Tri_A),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{3});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{2});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0, 512, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 512,   0, 1280, 389));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 512, 389, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
}

FE_TEST_CASE(ComputePaneLayout_TriB_TopFull_BottomSplit_TwoSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Tri_B,
                                     defaultRatiosFor(LayoutPreset::Tri_B),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{3});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{2});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0, 1280, 311));
  FE_ASSERT_TRUE(rectEquals(out.slots[1],   0, 311,  640, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 640, 311, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
}

FE_TEST_CASE(ComputePaneLayout_TriC_ThreeColumns_TwoVerticalSplitters) {
  // 1281 * (1.0f/3.0f) = 427, 1281 * (2.0f/3.0f) = 854
  const auto out = computePaneLayout(LayoutPreset::Tri_C,
                                     defaultRatiosFor(LayoutPreset::Tri_C),
                                     1281, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{3});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{2});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  427, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 427,   0,  854, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 854,   0, 1281, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
}

FE_TEST_CASE(ComputePaneLayout_QuadA_2x2Grid_ThreeSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_A,
                                     defaultRatiosFor(LayoutPreset::Quad_A),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  640, 389));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 640,   0, 1280, 389));
  FE_ASSERT_TRUE(rectEquals(out.slots[2],   0, 389,  640, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 640, 389, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[2].ratioId, std::uint8_t{2});
}

FE_TEST_CASE(ComputePaneLayout_QuadB_FourColumns_ThreeVerticalSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_B,
                                     defaultRatiosFor(LayoutPreset::Quad_B),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  320, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 320,   0,  640, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 640,   0,  960, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 960,   0, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[2].ratioId, std::uint8_t{2});
}

FE_TEST_CASE(ComputePaneLayout_QuadC_FourRows_ThreeHorizontalSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_C,
                                     defaultRatiosFor(LayoutPreset::Quad_C),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0,   0, 1280, 194));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 0, 194, 1280, 389));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 0, 389, 1280, 583));
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 0, 583, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
  FE_ASSERT_EQ(out.splitters[2].ratioId, std::uint8_t{2});
}

FE_TEST_CASE(ComputePaneLayout_QuadD_LeftFull_RightThreeStack_ThreeSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_D,
                                     defaultRatiosFor(LayoutPreset::Quad_D),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  640, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 640,   0, 1280, 259));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 640, 259, 1280, 518));
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 640, 518, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
  FE_ASSERT_EQ(out.splitters[2].ratioId, std::uint8_t{2});
}
