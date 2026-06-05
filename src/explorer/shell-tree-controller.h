#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <optional>
#include <string>

namespace fast_explorer::ui {

struct ShellTreeSelection {
  std::wstring location;
};

class ShellTreeController {
 public:
  ShellTreeController() = default;
  ~ShellTreeController();

  ShellTreeController(const ShellTreeController&) = delete;
  ShellTreeController& operator=(const ShellTreeController&) = delete;

  [[nodiscard]] bool attach(HWND tree);
  void detach() noexcept;

  void applyTheme() noexcept;
  void ensureRoots();
  void resetRoots();
  void reflectLocation(const std::wstring& location);

  [[nodiscard]] std::optional<ShellTreeSelection> selectionForItem(
      HTREEITEM item) const;

  LRESULT handleNotify(NMHDR* hdr);

 private:
  void populateRoots();
  void expandNode(HTREEITEM node);
  void onTreeExpanding(NMHDR* hdr);
  void onTreeDeleteItem(NMHDR* hdr);
  LRESULT onCustomDraw(NMHDR* hdr);
  void selectPath(const std::wstring& path);
  void selectShellLocation(const std::wstring& location);
  void selectNetworkRoot();
  HTREEITEM findChildByPath(HTREEITEM parent, const std::wstring& fsPath);
  HTREEITEM findItemByPidl(LPCITEMIDLIST target) const;

  HWND tree_ = nullptr;
  bool rootsLoaded_ = false;
  std::wstring lastReflectedLocation_;
};

}  // namespace fast_explorer::ui
