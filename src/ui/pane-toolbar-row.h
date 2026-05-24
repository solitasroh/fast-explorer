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

#include "winui_lite/chrome/cmd-packing.h"

namespace fast_explorer::ui {

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

  // Adopts the ˅ button that sits next to the address bar. Owner-
  // drawn so we can paint a dark chevron in dark mode; click is
  // routed to MainWindow via the packed kTbAddressDropdown id and
  // pops the AddressBarPopup TreeView.
  void setAddressDropdownBtn(HWND btn);
  HWND addressDropdownBtn() const noexcept { return addressDropdownBtn_; }

  HWND navToolbar() const noexcept { return navToolbar_; }
  HWND hamburger() const noexcept { return hamburger_; }

  // Reposition children based on the row's current client size.
  // Idempotent and safe to call before any children are attached.
  void layout();

  // T3 hook: set button enable state. `idx` is 0=Back, 1=Forward,
  // 2=Up. Refresh stays always-enabled so it has no setter.
  void setNavButtonEnabled(int slotIdx, bool enabled);

  // A4 hook: recreate the icon + text fonts at `newDpi`, re-apply
  // them to every child that owns one, and trigger a layout pass.
  // Called from MainWindow::onDpiChanged when the row's window
  // crosses a DPI boundary; without it the fonts stay frozen at
  // the DPI they were created at and the glyphs render too small
  // / too large on the new monitor.
  void onDpiChanged(UINT newDpi);

 private:
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);
  static bool registerClassOnce(HINSTANCE instance);
  bool createNavToolbar(HINSTANCE instance);
  bool createHamburger(HINSTANCE instance);
  // A2 custom-draw paths. The visible glyph is painted by us so the
  // underlying control text (iString for toolbar buttons, WindowText
  // for the hamburger) can hold the Korean accessible name that
  // MSAA / UIA picks up.
  LRESULT handleNavToolbarCustomDraw(LPARAM lParam);
  void drawHamburgerItem(LPARAM lParam);
  // A1: fill the toolbar's TBN_GETINFOTIP request with "라벨 (단축키)".
  // Toolbar already has TBSTYLE_TOOLTIPS so the tooltip window is
  // created automatically; this notification just supplies the text.
  void fillToolbarTooltip(LPARAM lParam);
  // Some tooltip configurations route via TTN_NEEDTEXTW from the
  // toolbar's internal TOOLTIPS_CLASS (NOT TBN_GETINFOTIP). Handle
  // both so the tooltip text appears regardless of which channel
  // the common-controls runtime ends up using.
  void fillTooltipNeedText(LPARAM lParam);
  // Hamburger isn't on the toolbar, so it needs its own TOOLTIPS_CLASS
  // window registered once via TTM_ADDTOOL.
  bool createHamburgerTooltip(HINSTANCE instance);

  HWND hamburgerTip_ = nullptr;

  HWND hwnd_ = nullptr;
  HWND addressBar_ = nullptr;
  HWND addressDropdownBtn_ = nullptr;
  HWND navToolbar_ = nullptr;
  HWND hamburger_ = nullptr;
  // iconFont_ holds the Lucide / Segoe Fluent Icons polyfill font used
  // for the navigation toolbar and hamburger glyphs.
  // rowFont_ is the system text font applied to the address-bar
  // ComboBoxEx so its visible textbox is a comfortable size — the
  // layout() measurement then uses that textbox height as the
  // common innerH for the rest of the row.
  HFONT iconFont_ = nullptr;
  HFONT rowFont_ = nullptr;
  std::size_t paneIdx_ = 0;
};

}  // namespace fast_explorer::ui
