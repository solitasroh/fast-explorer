#include <windows.h>

#include "test-harness.h"
#include "ui/pane-controller.h"
#include "ui/pane-manager.h"

using fast_explorer::ui::chooseSecondPaneInitialPath;
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

FE_TEST_CASE(PaneManager_OpenSecond_AssignsIndexOneAndDualMode) {
  PaneManager pm;
  pm.openInitial(nullptr);
  const std::size_t idx = pm.openSecond(nullptr);
  FE_ASSERT_EQ(idx, static_cast<std::size_t>(1));
  FE_ASSERT_EQ(pm.count(), static_cast<std::size_t>(2));
  FE_ASSERT_TRUE(pm.isDual());
  FE_ASSERT_EQ(pm.activeIndex(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(PaneManager_OpenSecond_Idempotent) {
  PaneManager pm;
  pm.openInitial(nullptr);
  pm.openSecond(nullptr);
  FE_ASSERT_EQ(pm.openSecond(nullptr), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(pm.count(), static_cast<std::size_t>(2));
}

FE_TEST_CASE(PaneManager_CloseSecond_RemovesPaneAndResetsActive) {
  PaneManager pm;
  pm.openInitial(nullptr);
  pm.openSecond(nullptr);
  pm.setActive(1);
  pm.closeSecond();
  FE_ASSERT_EQ(pm.count(), static_cast<std::size_t>(1));
  FE_ASSERT_FALSE(pm.isDual());
  FE_ASSERT_EQ(pm.activeIndex(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(PaneManager_CloseSecond_NoSecondPane_NoOp) {
  PaneManager pm;
  pm.openInitial(nullptr);
  pm.closeSecond();
  FE_ASSERT_EQ(pm.count(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(PaneManager_SetActive_OutOfRange_ReturnsFalse) {
  PaneManager pm;
  pm.openInitial(nullptr);
  FE_ASSERT_FALSE(pm.setActive(1));
  FE_ASSERT_EQ(pm.activeIndex(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(PaneManager_SetActive_DualMode_SwitchesBetweenPanes) {
  PaneManager pm;
  pm.openInitial(nullptr);
  pm.openSecond(nullptr);
  FE_ASSERT_TRUE(pm.setActive(1));
  FE_ASSERT_EQ(pm.activeIndex(), static_cast<std::size_t>(1));
  FE_ASSERT_TRUE(&pm.active() == &pm.at(1));
  FE_ASSERT_TRUE(pm.setActive(0));
  FE_ASSERT_EQ(pm.activeIndex(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(ChooseSecondPaneInitialPath_RequestedNonEmpty_TakesPrecedence) {
  const std::wstring requested = L"D:\\saved";
  const std::wstring fallback  = L"C:\\active";
  FE_ASSERT_WSTREQ(chooseSecondPaneInitialPath(requested, fallback),
                   requested);
}

FE_TEST_CASE(ChooseSecondPaneInitialPath_RequestedEmpty_UsesFallback) {
  const std::wstring requested;
  const std::wstring fallback = L"C:\\active";
  FE_ASSERT_WSTREQ(chooseSecondPaneInitialPath(requested, fallback),
                   fallback);
}

FE_TEST_CASE(ChooseSecondPaneInitialPath_BothEmpty_ReturnsEmpty) {
  const std::wstring requested;
  const std::wstring fallback;
  FE_ASSERT_TRUE(chooseSecondPaneInitialPath(requested, fallback).empty());
}
