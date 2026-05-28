#include "test-harness.h"
#include "winui_lite/widgets/tab-strip-geometry.h"

using fast_explorer::ui::StripMetrics;
using fast_explorer::ui::TabStripGeometry;
using fast_explorer::ui::TabRect;

namespace {
StripMetrics m96() { return StripMetrics{}; }   // 96 DPI defaults
}  // namespace

FE_TEST_CASE(TabStripGeometry_NoTabs_LayoutEmpty) {
  TabStripGeometry g(m96(), 0);
  auto rects = g.layout(400, 0);
  FE_ASSERT_EQ(rects.size(), static_cast<size_t>(0));
}

FE_TEST_CASE(TabStripGeometry_ThreeTabs_FitsWithoutOverflow) {
  TabStripGeometry g(m96(), 3);
  FE_ASSERT_TRUE(!g.overflows(800));
  auto rects = g.layout(800, 0);
  FE_ASSERT_EQ(rects.size(), static_cast<size_t>(3));
  // tabs sit side-by-side, equal width, within [min, max]
  for (size_t i = 0; i + 1 < rects.size(); ++i) {
    FE_ASSERT_EQ(rects[i].right, rects[i+1].left);
  }
  FE_ASSERT_TRUE(rects[0].right - rects[0].left >= 64);
  FE_ASSERT_TRUE(rects[0].right - rects[0].left <= 200);
}

FE_TEST_CASE(TabStripGeometry_ManyTabs_OverflowReportsTrue) {
  TabStripGeometry g(m96(), 20);
  FE_ASSERT_TRUE(g.overflows(800));
}

FE_TEST_CASE(TabStripGeometry_HitTest_InsideTabReturnsIndex) {
  TabStripGeometry g(m96(), 3);
  auto rects = g.layout(800, 0);
  const int mid0 = (rects[0].left + rects[0].right) / 2;
  const int mid2 = (rects[2].left + rects[2].right) / 2;
  FE_ASSERT_EQ(g.hitTest(rects, mid0), 0);
  FE_ASSERT_EQ(g.hitTest(rects, mid2), 2);
  // background after last tab
  FE_ASSERT_EQ(g.hitTest(rects, rects.back().right + 50), -1);
  // before first tab
  FE_ASSERT_EQ(g.hitTest(rects, rects.front().left - 50), -1);
}

FE_TEST_CASE(TabStripGeometry_HitTestCloseX_HitsOnlyXRegion) {
  TabStripGeometry g(m96(), 3);
  auto rects = g.layout(800, 0);
  // far-left of tab 1: not on X region
  FE_ASSERT_EQ(g.hitTestCloseX(rects, rects[1].left + 4), -1);
  // inside the X region for tab 1
  const int xMid = (rects[1].closeXLeft + rects[1].closeXRight) / 2;
  FE_ASSERT_EQ(g.hitTestCloseX(rects, xMid), 1);
}

FE_TEST_CASE(TabStripGeometry_DropIndex_LeftOfFirstReturnsZero) {
  TabStripGeometry g(m96(), 3);
  auto rects = g.layout(800, 0);
  FE_ASSERT_EQ(g.dropIndex(rects, 2, rects.front().left - 50),
               static_cast<size_t>(0));
}

FE_TEST_CASE(TabStripGeometry_DropIndex_RightOfLastReturnsLast) {
  TabStripGeometry g(m96(), 3);
  auto rects = g.layout(800, 0);
  FE_ASSERT_EQ(g.dropIndex(rects, 0, rects.back().right + 50),
               static_cast<size_t>(2));
}

FE_TEST_CASE(TabStripGeometry_DropIndex_PastMidpoint_AdjustsByFromOffset) {
  // Drag tab 0 to the right past tab 1's midpoint -> drop at index 1.
  TabStripGeometry g(m96(), 3);
  auto rects = g.layout(800, 0);
  const int mid1 = (rects[1].left + rects[1].right) / 2;
  FE_ASSERT_EQ(g.dropIndex(rects, 0, mid1 + 1),
               static_cast<size_t>(1));
}

FE_TEST_CASE(TabStripGeometry_DragThreshold_BelowReturnsFalse) {
  TabStripGeometry g(m96(), 3);
  FE_ASSERT_TRUE(!g.exceedsDragThreshold(2));
  FE_ASSERT_TRUE( g.exceedsDragThreshold(6));
  FE_ASSERT_TRUE( g.exceedsDragThreshold(-6));
}
