#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "explorer/shell-tree-controller.h"

namespace fast_explorer::ui {

inline constexpr int kNavigationTreeMinWidthDip = 180;
inline constexpr int kNavigationTreeDefaultWidthDip = 260;
inline constexpr int kNavigationTreeMaxWidthDip = 420;

[[nodiscard]] int clampNavigationTreeWidthPx(
    int requestedPx, int clientWidthPx, UINT dpi) noexcept;

class NavigationTreePane {
 public:
  using NavigateCallback = std::function<void(const std::wstring&)>;

  NavigationTreePane() = default;
  ~NavigationTreePane();

  NavigationTreePane(const NavigationTreePane&) = delete;
  NavigationTreePane& operator=(const NavigationTreePane&) = delete;

  [[nodiscard]] bool create(HWND parent, HINSTANCE instance);
  void destroy() noexcept;

  [[nodiscard]] HWND handle() const noexcept { return hwnd_; }
  [[nodiscard]] HWND treeHandle() const noexcept { return tree_; }
  [[nodiscard]] bool available() const noexcept {
    return hwnd_ != nullptr && tree_ != nullptr;
  }

  void setNavigateCallback(NavigateCallback callback);
  void setBounds(int x, int y, int width, int height);
  void show(bool visible) noexcept;
  void applyTheme() noexcept;
  void ensureRoots();
  void reflectLocation(const std::wstring& location);

 private:
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT handleTreeNotify(NMHDR* hdr);
  void applyVisualMetrics() noexcept;
  void layoutTree() noexcept;

  HWND hwnd_ = nullptr;
  HWND tree_ = nullptr;
  ShellTreeController controller_;
  NavigateCallback onNavigate_;
  UINT appliedDpi_ = 0;
  bool suppressSelectionCommit_ = false;
};

}  // namespace fast_explorer::ui
