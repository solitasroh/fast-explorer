#include <windows.h>

#include "test-harness.h"
#include "bench-fs-helper.h"
#include "explorer/filter-pattern.h"
#include "explorer/pane-controller.h"
#include "explorer/adapters/shell-item-source.h"
#include "explorer/adapters/shell-item-dispatcher.h"

using fast_explorer::ui::FilterMode;
using fast_explorer::ui::FilterPattern;
using fast_explorer::ui::PaneController;
using fast_explorer::ui::adapters::ShellItemSource;
using fast_explorer::ui::adapters::ShellItemDispatcher;

namespace {
HWND hiddenHostWindow() {
  // Tests run on a hidden message-only window so PaneController has
  // somewhere to post its WM_FE_* messages.
  return HWND_MESSAGE;
}
}  // namespace

FE_TEST_CASE(AdapterCell_NullCell_ItemSourceReturnsEmptyShape) {
  PaneController* cell = nullptr;
  ShellItemSource src(cell);
  FE_ASSERT_EQ(src.count(), static_cast<std::size_t>(0));
  FE_ASSERT_TRUE(src.currentLocation().empty());
}

FE_TEST_CASE(AdapterCell_CellPointsAtControllerA_DispatcherReadsA) {
  // Set up a temp dir holding one file with a known leaf name.
  fast_explorer::tests::TempDir tmp(L"adapter-cell-a");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(
      tmp.path() + L"\\hello.txt");

  PaneController a(hiddenHostWindow(), 0);
  PaneController* cell = &a;
  ShellItemDispatcher d(cell);

  FE_ASSERT_TRUE(a.openFolder(tmp.path()));
  a.joinForTest();

  // ItemId 1 maps to visible row 0 (per ShellItemSource convention).
  const std::wstring text =
      d.textFor(1, fast_explorer::ui::ports::ItemField::Name);
  FE_ASSERT_WSTREQ(text, L"hello.txt");
}

FE_TEST_CASE(AdapterCell_SwapPointsToB_DispatcherSwitchesReads) {
  fast_explorer::tests::TempDir tmpA(L"adapter-cell-swap-a");
  fast_explorer::tests::TempDir tmpB(L"adapter-cell-swap-b");
  FE_ASSERT_TRUE(CreateDirectoryW(tmpA.path().c_str(), nullptr) != 0);
  FE_ASSERT_TRUE(CreateDirectoryW(tmpB.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(tmpA.path() + L"\\alpha.txt");
  fast_explorer::tests::writeEmptyDiskFile(tmpB.path() + L"\\beta.txt");

  PaneController a(hiddenHostWindow(), 0);
  PaneController b(hiddenHostWindow(), 0);
  FE_ASSERT_TRUE(a.openFolder(tmpA.path()));
  FE_ASSERT_TRUE(b.openFolder(tmpB.path()));
  a.joinForTest();
  b.joinForTest();

  PaneController* cell = &a;
  ShellItemDispatcher d(cell);
  const auto textA =
      d.textFor(1, fast_explorer::ui::ports::ItemField::Name);

  cell = &b;
  const auto textB =
      d.textFor(1, fast_explorer::ui::ports::ItemField::Name);

  FE_ASSERT_WSTREQ(textA, L"alpha.txt");
  FE_ASSERT_WSTREQ(textB, L"beta.txt");
}

FE_TEST_CASE(AdapterCell_ItemSourceCountUsesDisplayedRowsWhenFiltered) {
  fast_explorer::tests::TempDir tmp(L"adapter-cell-filter-source");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\alpha.txt");
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\target.txt");

  PaneController pane(hiddenHostWindow(), 0);
  PaneController* cell = &pane;
  ShellItemSource src(cell);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  pane.setFilter(FilterPattern(L"target", FilterMode::Plain));

  FE_ASSERT_EQ(src.count(), static_cast<std::size_t>(1));
  FE_ASSERT_NE(src.idAt(0), fast_explorer::ui::ports::kInvalidItemId);
  FE_ASSERT_EQ(src.idAt(1), fast_explorer::ui::ports::kInvalidItemId);
}

FE_TEST_CASE(AdapterCell_DispatcherReadsDisplayedRowWhenFiltered) {
  fast_explorer::tests::TempDir tmp(L"adapter-cell-filter-dispatcher");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\alpha.txt");
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\target.txt");

  PaneController pane(hiddenHostWindow(), 0);
  PaneController* cell = &pane;
  ShellItemSource src(cell);
  ShellItemDispatcher dispatcher(cell);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  pane.setFilter(FilterPattern(L"target", FilterMode::Plain));

  const auto id = src.idAt(0);
  FE_ASSERT_NE(id, fast_explorer::ui::ports::kInvalidItemId);
  FE_ASSERT_WSTREQ(
      dispatcher.textFor(id, fast_explorer::ui::ports::ItemField::Name),
      L"target.txt");
}

FE_TEST_CASE(AdapterCell_ItemSourceNavigateToAcceptsShellDisplayAliases) {
  PaneController pane(hiddenHostWindow(), 0);
  PaneController* cell = &pane;
  ShellItemSource src(cell);

  FE_ASSERT_TRUE(src.navigateTo(L"This PC"));
  FE_ASSERT_WSTREQ(pane.currentPath(), L"shell:ThisPC");

  FE_ASSERT_TRUE(src.navigateTo(L"네트워크"));
  FE_ASSERT_WSTREQ(pane.currentPath(), L"shell:Network");
}
