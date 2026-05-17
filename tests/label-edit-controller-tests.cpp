#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <string>

#include "bench-fs-helper.h"
#include "core/path-utils.h"
#include "test-harness.h"
#include "ui/label-edit-controller.h"
#include "ui/pane-controller.h"

using fast_explorer::tests::diskPathExists;
using fast_explorer::tests::TempDir;
using fast_explorer::tests::writeEmptyDiskFile;
using fast_explorer::ui::LabelEditController;
using fast_explorer::ui::PaneController;

namespace {

NMLVDISPINFOW makeEndEdit(int iItem, wchar_t* pszText) {
  NMLVDISPINFOW disp{};
  disp.item.iItem = iItem;
  disp.item.pszText = pszText;
  return disp;
}

NMHDR* asHdr(NMLVDISPINFOW& disp) {
  return reinterpret_cast<NMHDR*>(&disp);
}

}  // namespace

FE_TEST_CASE(LabelEditController_HandleBeginEdit_ReturnsFalse) {
  PaneController pane(nullptr);
  LabelEditController ctl(nullptr, pane);
  FE_ASSERT_EQ(ctl.handleBeginEdit(), static_cast<LRESULT>(FALSE));
}

FE_TEST_CASE(LabelEditController_HandleEndEdit_NullHdr_ReturnsFalse) {
  PaneController pane(nullptr);
  LabelEditController ctl(nullptr, pane);
  FE_ASSERT_EQ(ctl.handleEndEdit(nullptr), static_cast<LRESULT>(FALSE));
}

FE_TEST_CASE(LabelEditController_HandleEndEdit_CancelByEscape_ReturnsFalse) {
  PaneController pane(nullptr);
  LabelEditController ctl(nullptr, pane);
  NMLVDISPINFOW disp = makeEndEdit(0, nullptr);
  FE_ASSERT_EQ(ctl.handleEndEdit(asHdr(disp)), static_cast<LRESULT>(FALSE));
}

FE_TEST_CASE(LabelEditController_HandleEndEdit_NegativeItem_ReturnsFalse) {
  PaneController pane(nullptr);
  LabelEditController ctl(nullptr, pane);
  wchar_t text[] = L"anything";
  NMLVDISPINFOW disp = makeEndEdit(-1, text);
  FE_ASSERT_EQ(ctl.handleEndEdit(asHdr(disp)), static_cast<LRESULT>(FALSE));
}

FE_TEST_CASE(LabelEditController_HandleEndEdit_EmptyText_ReturnsFalse) {
  PaneController pane(nullptr);
  LabelEditController ctl(nullptr, pane);
  wchar_t text[] = L"";
  NMLVDISPINFOW disp = makeEndEdit(0, text);
  FE_ASSERT_EQ(ctl.handleEndEdit(asHdr(disp)), static_cast<LRESULT>(FALSE));
}

FE_TEST_CASE(LabelEditController_HandleEndEdit_ValidRename_RoutesToPaneAndReturnsFalse) {
  TempDir tmp(L"labeledit-rename");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring before =
      fast_explorer::core::joinPath(tmp.path(), L"before.txt");
  const std::wstring after =
      fast_explorer::core::joinPath(tmp.path(), L"after.txt");
  writeEmptyDiskFile(before);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  std::uint32_t row = UINT32_MAX;
  const auto& store = pane.store();
  for (std::uint32_t i = 0;
       i < static_cast<std::uint32_t>(store.publishedCount()); ++i) {
    const auto& entry = store.visibleAt(i);
    if (std::wstring_view(entry.namePtr, entry.nameLength) == L"before.txt") {
      row = i;
      break;
    }
  }
  FE_ASSERT_TRUE(row != UINT32_MAX);

  LabelEditController ctl(nullptr, pane);
  wchar_t text[] = L"after.txt";
  NMLVDISPINFOW disp = makeEndEdit(static_cast<int>(row), text);
  FE_ASSERT_EQ(ctl.handleEndEdit(asHdr(disp)), static_cast<LRESULT>(FALSE));
  pane.shellWorkerForTest().waitForProcessedForTest(1);
  FE_ASSERT_FALSE(diskPathExists(before));
  FE_ASSERT_TRUE(diskPathExists(after));
}

FE_TEST_CASE(LabelEditController_BeginCreateSubfolder_EmptyPath_NoPending) {
  PaneController pane(nullptr);
  LabelEditController ctl(nullptr, pane);
  ctl.beginCreateSubfolder();
  FE_ASSERT_TRUE(ctl.pendingFolderNameForTest().empty());
}

FE_TEST_CASE(LabelEditController_BeginCreateSubfolder_EmptyFolder_ArmsNewFolder) {
  TempDir tmp(L"labeledit-create-empty");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  LabelEditController ctl(nullptr, pane);
  ctl.beginCreateSubfolder();
  pane.shellWorkerForTest().waitForProcessedForTest(1);
  FE_ASSERT_WSTREQ(ctl.pendingFolderNameForTest(), L"New folder");
  FE_ASSERT_TRUE(diskPathExists(fast_explorer::core::joinPath(tmp.path(),
                                                          L"New folder")));
}

FE_TEST_CASE(LabelEditController_BeginCreateSubfolder_NameCollision_SuffixesNumber) {
  TempDir tmp(L"labeledit-create-collide");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring existing =
      fast_explorer::core::joinPath(tmp.path(), L"New folder");
  FE_ASSERT_TRUE(CreateDirectoryW(existing.c_str(), nullptr) != 0);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  LabelEditController ctl(nullptr, pane);
  ctl.beginCreateSubfolder();
  pane.shellWorkerForTest().waitForProcessedForTest(1);
  FE_ASSERT_WSTREQ(ctl.pendingFolderNameForTest(), L"New folder (2)");
  FE_ASSERT_TRUE(diskPathExists(fast_explorer::core::joinPath(tmp.path(),
                                                          L"New folder (2)")));
}

FE_TEST_CASE(LabelEditController_MaybeStartPendingEdit_EmptyPending_NoOp) {
  PaneController pane(nullptr);
  LabelEditController ctl(nullptr, pane);
  ctl.maybeStartPendingEdit();
  FE_ASSERT_TRUE(ctl.pendingFolderNameForTest().empty());
}

FE_TEST_CASE(LabelEditController_MaybeStartPendingEdit_NullListView_KeepsPending) {
  TempDir tmp(L"labeledit-pending-keep");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  LabelEditController ctl(nullptr, pane);
  ctl.beginCreateSubfolder();
  pane.shellWorkerForTest().waitForProcessedForTest(1);
  FE_ASSERT_FALSE(ctl.pendingFolderNameForTest().empty());

  // With nullptr listView, the early return at the top of
  // maybeStartPendingEdit fires before the swap-and-clear: pending
  // therefore stays armed for a future call that does have a real HWND.
  ctl.maybeStartPendingEdit();
  FE_ASSERT_WSTREQ(ctl.pendingFolderNameForTest(), L"New folder");
}
