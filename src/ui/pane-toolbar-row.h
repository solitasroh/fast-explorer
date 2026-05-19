// pane-toolbar-row.h — child window that owns the row above each
// pane's list-view (nav toolbar + address bar + hamburger). The row
// is a layout container only: MainWindow still constructs each child
// (so creation order, subclassing, and lifetime stay there), then
// hands the child HWNDs to the row, which reparents and positions
// them on every WM_SIZE.
//
// WM_COMMAND / WM_NOTIFY / WM_CTLCOLOR* messages are forwarded to
// the row's parent so MainWindow's existing routing (address-bar
// dropdown, accelerators, etc.) keeps working without per-pane
// rewiring.

#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstddef>

namespace fast_explorer::ui {

// Bits used to pack (buttonId, paneIdx) into a single WORD for
// WM_COMMAND routing. WM_COMMAND only carries 16 bits of identifier
// (LOWORD of wParam), so the split is 12 bits for the original ID
// and 4 bits for the pane index — up to 16 panes (well past the
// current cap of 2) and IDs from 0..4095 fit cleanly. The previous
// 8/8 split silently truncated any ID >= 256, which broke every
// hamburger-menu item (kMenu* IDs are in the 300 range).
inline constexpr WORD kPaneIdxBits = 4;
inline constexpr WORD kPaneIdxMask = 0x000F;

constexpr WORD packCmd(WORD buttonId, std::size_t paneIdx) noexcept {
  return static_cast<WORD>((buttonId << kPaneIdxBits) |
                           (paneIdx & kPaneIdxMask));
}
constexpr WORD unpackButton(WORD packed) noexcept {
  return static_cast<WORD>(packed >> kPaneIdxBits);
}
constexpr std::size_t unpackPane(WORD packed) noexcept {
  return static_cast<std::size_t>(packed & kPaneIdxMask);
}

class PaneToolbarRow {
 public:
  PaneToolbarRow() = default;
  ~PaneToolbarRow();

  PaneToolbarRow(const PaneToolbarRow&) = delete;
  PaneToolbarRow& operator=(const PaneToolbarRow&) = delete;

  bool create(HWND parent, HINSTANCE instance, std::size_t paneIdx);
  void destroy();

  HWND handle() const noexcept { return hwnd_; }
  std::size_t paneIndex() const noexcept { return paneIdx_; }

  // Adopts an existing child window (created with any parent) as the
  // address-bar slot. The child is reparented to this row. Pass
  // nullptr to detach. Triggers a layout if the row is sized.
  void setAddressBar(HWND addressBar);
  HWND addressBar() const noexcept { return addressBar_; }

  HWND navToolbar() const noexcept { return navToolbar_; }
  HWND hamburger() const noexcept { return hamburger_; }

  // Reposition children based on the row's current client size.
  // Idempotent and safe to call before any children are attached.
  void layout();

  // T3 hook: set button enable state. `idx` is 0=Back, 1=Forward,
  // 2=Up. Refresh stays always-enabled so it has no setter.
  void setNavButtonEnabled(int slotIdx, bool enabled);

 private:
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);
  static bool registerClassOnce(HINSTANCE instance);
  bool createNavToolbar(HINSTANCE instance);
  bool createHamburger(HINSTANCE instance);

  HWND hwnd_ = nullptr;
  HWND addressBar_ = nullptr;
  HWND navToolbar_ = nullptr;
  HWND hamburger_ = nullptr;
  // Segoe MDL2 Assets font (Windows 10+). Owned by the row so it
  // outlives WM_SETFONT use; destroyed in ~PaneToolbarRow.
  HFONT mdl2Font_ = nullptr;
  std::size_t paneIdx_ = 0;
};

}  // namespace fast_explorer::ui
