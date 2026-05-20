#include <windows.h>

#include "test-harness.h"
#include "ui/pane-layout.h"
#include "ui/splitter-ratios.h"

using fast_explorer::ui::computePaneRects;
using fast_explorer::ui::LayoutAction;
using fast_explorer::ui::LayoutOrientation;
using fast_explorer::ui::PaneLayoutRects;
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

FE_TEST_CASE(PaneLayout_PaneCountZero_ReturnsZeroedRects) {
  const auto out = computePaneRects(1280, 800, 28, 22, 0);
  FE_ASSERT_TRUE(rectZero(out.panes[0]));
  FE_ASSERT_TRUE(rectZero(out.panes[1]));
}

FE_TEST_CASE(PaneLayout_PaneCountThree_ReturnsZeroedRects) {
  const auto out = computePaneRects(1280, 800, 28, 22, 3);
  FE_ASSERT_TRUE(rectZero(out.panes[0]));
  FE_ASSERT_TRUE(rectZero(out.panes[1]));
}

FE_TEST_CASE(PaneLayout_Single_FullWidthBelowAddressAboveStatus) {
  const auto out = computePaneRects(1280, 800, 28, 22, 1);
  // listH = 800 - 28 - 22 = 750; pane[0] occupies (0, 28) to (1280, 778)
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 28, 1280, 778));
  FE_ASSERT_TRUE(rectZero(out.panes[1]));
}

FE_TEST_CASE(PaneLayout_Dual_SplitFiftyFifty) {
  const auto out = computePaneRects(1280, 800, 28, 22, 2);
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 28, 640, 778));
  FE_ASSERT_TRUE(rectEquals(out.panes[1], 640, 28, 1280, 778));
}

FE_TEST_CASE(PaneLayout_Dual_OddWidth_LeftAbsorbsTheLossOnRoundDown) {
  const auto out = computePaneRects(1281, 800, 28, 22, 2);
  // 1281 / 2 = 640; left = [0, 640), right = [640, 1281)
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 28, 640, 778));
  FE_ASSERT_TRUE(rectEquals(out.panes[1], 640, 28, 1281, 778));
}

FE_TEST_CASE(PaneLayout_ZeroClientHeight_ReturnsZeroedRects) {
  const auto out = computePaneRects(1280, 50, 28, 22, 1);
  // 50 - 28 - 22 = 0 → listH not positive
  FE_ASSERT_TRUE(rectZero(out.panes[0]));
}

FE_TEST_CASE(PaneLayout_OverflowBars_ReturnsZeroedRects) {
  // Address + status taller than client → listH would go negative,
  // clamped to 0 then rejected.
  const auto out = computePaneRects(1280, 30, 28, 22, 1);
  FE_ASSERT_TRUE(rectZero(out.panes[0]));
}

FE_TEST_CASE(PaneLayout_ZeroClientWidth_ReturnsZeroedRects) {
  const auto out = computePaneRects(0, 800, 28, 22, 1);
  FE_ASSERT_TRUE(rectZero(out.panes[0]));
}

FE_TEST_CASE(PaneLayout_NoAddressBar_ListStartsAtTop) {
  const auto out = computePaneRects(1280, 800, 0, 22, 1);
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 0, 1280, 778));
}

FE_TEST_CASE(PaneLayout_Dual_Vertical_ExplicitMatchesDefault) {
  const auto def = computePaneRects(1280, 800, 28, 22, 2);
  const auto exp = computePaneRects(1280, 800, 28, 22, 2,
                                    LayoutOrientation::Vertical);
  FE_ASSERT_TRUE(rectEquals(exp.panes[0], def.panes[0].left,
                            def.panes[0].top, def.panes[0].right,
                            def.panes[0].bottom));
  FE_ASSERT_TRUE(rectEquals(exp.panes[1], def.panes[1].left,
                            def.panes[1].top, def.panes[1].right,
                            def.panes[1].bottom));
}

FE_TEST_CASE(PaneLayout_Dual_Horizontal_SplitTopBottomFiftyFifty) {
  // listH = 800 - 28 - 22 = 750; midY = 28 + 375 = 403
  const auto out = computePaneRects(1280, 800, 28, 22, 2,
                                    LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 28,  1280, 403));
  FE_ASSERT_TRUE(rectEquals(out.panes[1], 0, 403, 1280, 778));
}

FE_TEST_CASE(PaneLayout_Dual_Horizontal_OddListH_TopAbsorbsLossOnRoundDown) {
  // listH = 801 - 28 - 22 = 751; 751/2 = 375; midY = 28 + 375 = 403
  // Top covers [28, 403) -> height 375, bottom covers [403, 779) -> 376
  const auto out = computePaneRects(1280, 801, 28, 22, 2,
                                    LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 28,  1280, 403));
  FE_ASSERT_TRUE(rectEquals(out.panes[1], 0, 403, 1280, 779));
}

FE_TEST_CASE(PaneLayout_Single_Horizontal_OrientationIgnored) {
  const auto out = computePaneRects(1280, 800, 28, 22, 1,
                                    LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 28, 1280, 778));
  FE_ASSERT_TRUE(rectZero(out.panes[1]));
}

FE_TEST_CASE(PaneLayout_Dual_Horizontal_ZeroListH_ReturnsZeroedRects) {
  const auto out = computePaneRects(1280, 50, 28, 22, 2,
                                    LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(rectZero(out.panes[0]));
  FE_ASSERT_TRUE(rectZero(out.panes[1]));
}

FE_TEST_CASE(PaneLayout_Dual_Horizontal_ZeroClientWidth_ReturnsZeroedRects) {
  const auto out = computePaneRects(0, 800, 28, 22, 2,
                                    LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(rectZero(out.panes[0]));
  FE_ASSERT_TRUE(rectZero(out.panes[1]));
}

FE_TEST_CASE(PaneLayout_Dual_Horizontal_NoAddressBar) {
  // listH = 800 - 0 - 22 = 778; midY = 0 + 389 = 389
  const auto out = computePaneRects(1280, 800, 0, 22, 2,
                                    LayoutOrientation::Horizontal);
  FE_ASSERT_TRUE(rectEquals(out.panes[0], 0, 0,   1280, 389));
  FE_ASSERT_TRUE(rectEquals(out.panes[1], 0, 389, 1280, 778));
}

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
