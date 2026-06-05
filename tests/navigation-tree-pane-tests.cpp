#include "test-harness.h"

#include "explorer/navigation-tree-pane.h"

using fast_explorer::ui::clampNavigationTreeWidthPx;

FE_TEST_CASE(NavigationTree_ClampWidth_UsesDefaultWhenRequestedInvalid) {
  FE_ASSERT_EQ(clampNavigationTreeWidthPx(0, 1200, 96), 260);
  FE_ASSERT_EQ(clampNavigationTreeWidthPx(-10, 1200, 96), 260);
}

FE_TEST_CASE(NavigationTree_ClampWidth_RespectsMinAndMax) {
  FE_ASSERT_EQ(clampNavigationTreeWidthPx(120, 1200, 96), 180);
  FE_ASSERT_EQ(clampNavigationTreeWidthPx(900, 1200, 96), 420);
}

FE_TEST_CASE(NavigationTree_ClampWidth_LeavesNarrowClientSpace) {
  FE_ASSERT_EQ(clampNavigationTreeWidthPx(260, 400, 96), 180);
  FE_ASSERT_EQ(clampNavigationTreeWidthPx(420, 600, 96), 270);
}
