#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "test-harness.h"
#include "ui/pane-controller.h"

using fast_explorer::bench::generateDataset;
using fast_explorer::bench::GenerateError;
using fast_explorer::bench::PresetKind;
using fast_explorer::tests::TempDir;
using fast_explorer::ui::PaneController;

constexpr uint64_t kSmallPresetExpectedItems = 200;

FE_TEST_CASE(PaneController_Default_HasZeroGenerationAndEmptyPath) {
  PaneController pc(nullptr);
  FE_ASSERT_EQ(pc.generation(), 0u);
  FE_ASSERT_TRUE(pc.currentPath().empty());
  FE_ASSERT_EQ(pc.hostWindow(), static_cast<HWND>(nullptr));
}

FE_TEST_CASE(PaneController_OpenFolder_ValidPath_AcceptsAndBumpsGeneration) {
  PaneController pc(nullptr);
  const uint32_t before = pc.generation();
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\tmp"));
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\tmp");
  FE_ASSERT_TRUE(pc.generation() != before);
}

FE_TEST_CASE(PaneController_OpenFolder_EmptyPath_Rejected) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\initial"));
  const uint32_t before = pc.generation();
  const std::wstring beforePath = pc.currentPath();
  FE_ASSERT_FALSE(pc.openFolder(L""));
  FE_ASSERT_EQ(pc.generation(), before);
  FE_ASSERT_WSTREQ(pc.currentPath(), beforePath);
}

FE_TEST_CASE(PaneController_OpenFolder_RelativePath_Rejected) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.openFolder(L"some\\relative\\path"));
  FE_ASSERT_TRUE(pc.currentPath().empty());
}

FE_TEST_CASE(PaneController_OpenFolder_UncPath_Rejected) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.openFolder(L"\\\\server\\share"));
  FE_ASSERT_TRUE(pc.currentPath().empty());
}

FE_TEST_CASE(PaneController_OpenFolder_Twice_BumpsGenerationEachTime) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\a"));
  const uint32_t g1 = pc.generation();
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\b"));
  const uint32_t g2 = pc.generation();
  FE_ASSERT_TRUE(g2 != g1);
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\b");
}

FE_TEST_CASE(PaneController_OpenFolder_DrivesEnumerationOnRealFs) {
  TempDir tmp(L"pane-open");
  const auto gen = generateDataset(PresetKind::Small, tmp.path(), 1);
  FE_ASSERT_EQ(gen.error, GenerateError::None);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  FE_ASSERT_EQ(pane.store().itemCount(), kSmallPresetExpectedItems);
}

FE_TEST_CASE(PaneController_OpenFolder_Twice_CancelsAndReopens) {
  TempDir a(L"pane-a");
  TempDir b(L"pane-b");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, a.path(), 1).error,
               GenerateError::None);
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, b.path(), 1).error,
               GenerateError::None);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(a.path()));
  FE_ASSERT_TRUE(pane.openFolder(b.path()));
  pane.joinForTest();
  FE_ASSERT_WSTREQ(pane.currentPath(), b.path());
  FE_ASSERT_TRUE(pane.generation() >= 2u);
}
