#include <windows.h>

#include "test-harness.h"
#include "ui/pane-layout.h"

using fast_explorer::ui::computePaneRects;
using fast_explorer::ui::PaneLayoutRects;

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
