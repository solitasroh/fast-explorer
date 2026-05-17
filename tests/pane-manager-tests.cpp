#include <windows.h>

#include "test-harness.h"
#include "ui/pane-controller.h"
#include "ui/pane-manager.h"

using fast_explorer::ui::PaneController;
using fast_explorer::ui::PaneManager;

FE_TEST_CASE(PaneManager_Default_ZeroCount) {
  PaneManager pm;
  FE_ASSERT_EQ(pm.count(), static_cast<std::size_t>(0));
  FE_ASSERT_FALSE(pm.isDual());
}

FE_TEST_CASE(PaneManager_OpenInitial_AssignsIndexZeroAndSingleMode) {
  PaneManager pm;
  const std::size_t idx = pm.openInitial(nullptr);
  FE_ASSERT_EQ(idx, static_cast<std::size_t>(0));
  FE_ASSERT_EQ(pm.count(), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(pm.activeIndex(), static_cast<std::size_t>(0));
  FE_ASSERT_FALSE(pm.isDual());
}

FE_TEST_CASE(PaneManager_ActiveReturnsThePane) {
  PaneManager pm;
  pm.openInitial(nullptr);
  PaneController& a = pm.active();
  PaneController& byIndex = pm.at(0);
  FE_ASSERT_TRUE(&a == &byIndex);
}

FE_TEST_CASE(PaneManager_ActivePaneReceivesOpenFolder) {
  PaneManager pm;
  pm.openInitial(nullptr);
  FE_ASSERT_TRUE(pm.active().openFolder(L"C:\\tmp"));
  FE_ASSERT_WSTREQ(pm.active().currentPath(), L"C:\\tmp");
  FE_ASSERT_WSTREQ(pm.at(0).currentPath(), L"C:\\tmp");
}
