#include <windows.h>
#include <commctrl.h>

#include "explorer/shell-tree-controller.h"
#include "test-harness.h"

namespace {

class OleScope {
 public:
  OleScope() noexcept : hr_(OleInitialize(nullptr)) {}
  ~OleScope() {
    if (SUCCEEDED(hr_)) {
      OleUninitialize();
    }
  }

  [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(hr_); }

 private:
  HRESULT hr_;
};

HWND createTreeForTest() {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_TREEVIEW_CLASSES;
  InitCommonControlsEx(&icc);

  HWND tree = CreateWindowExW(0, WC_TREEVIEWW, L"",
                              WS_POPUP | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
                              0, 0, 200, 200, nullptr, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
  FE_ASSERT_TRUE(tree != nullptr);
  return tree;
}

void assertReflectsLocation(const wchar_t* location) {
  OleScope ole;
  FE_ASSERT_TRUE(ole.ok());

  HWND tree = createTreeForTest();
  fast_explorer::ui::ShellTreeController controller;
  FE_ASSERT_TRUE(controller.attach(tree));
  controller.ensureRoots();
  controller.reflectLocation(location);

  HTREEITEM selected = TreeView_GetSelection(tree);
  FE_ASSERT_TRUE(selected != nullptr);
  const auto selection = controller.selectionForItem(selected);
  FE_ASSERT_TRUE(selection.has_value());
  FE_ASSERT_WSTREQ(selection->location, location);

  controller.detach();
  DestroyWindow(tree);
}

}  // namespace

FE_TEST_CASE(ShellTreeController_ThisPcSelection_MapsToShellAlias) {
  assertReflectsLocation(L"shell:ThisPC");
}

FE_TEST_CASE(ShellTreeController_RecycleBinSelection_MapsToShellAlias) {
  assertReflectsLocation(L"shell:RecycleBinFolder");
}
