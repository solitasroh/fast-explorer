// pane-toolbar-row.h — child window that owns the row above each
// pane's primary content (custom-drawn nav toolbar + caller-owned
// address bar + optional hamburger and address-dropdown chevron
// slots). The row is a layout container plus the toolbar widget: the
// host creates the address-bar / list-view / etc. and hands the
// HWNDs to the row, which reparents and positions them on every
// WM_SIZE.
//
// What's in this header:
//   * NavButtonSpec / AuxSlotSpec / PaneToolbarRowConfig — caller-
//     supplied tables describing the buttons (id, Fluent/MDL2 glyph
//     codepoint, MSAA label, tooltip text). The lib does not embed
//     any application-specific glyph table or string.
//   * PaneToolbarRow — the row widget. Stateless w.r.t. command IDs;
//     packs them with packCmd from cmd-packing.h before issuing
//     WM_COMMAND.
//
// WM_COMMAND / WM_NOTIFY / WM_CTLCOLOR* messages are forwarded to
// the row's parent so the host's existing routing keeps working.

#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstddef>
#include <span>
#include <vector>

#include "winui_lite/chrome/cmd-packing.h"

namespace fast_explorer::ui {

// One nav-toolbar button. All pointer-typed strings are caller-owned
// and must outlive the PaneToolbarRow (typical: static const arrays
// in the host). Pass nullptr for label / tooltip / glyph to opt out
// of the respective channel.
struct NavButtonSpec {
  WORD id;                  // base id (paneIdx is packed in by the row)
  const wchar_t* glyph;     // 1-char Fluent / MDL2 codepoint string
  const wchar_t* label;     // MSAA / UIA accessible name
  const wchar_t* tooltip;   // "Label (Shortcut)" hint
};

// Optional auxiliary slots. The hamburger sits at the far right of
// the row, the address-dropdown chevron between the address bar and
// hamburger. Leave `id` == 0 to omit the slot.
struct AuxSlotSpec {
  WORD id = 0;
  const wchar_t* glyph = nullptr;
  const wchar_t* label = nullptr;
  const wchar_t* tooltip = nullptr;
};

// Caller-supplied composition of one row. The strings referenced by
// nested structs must outlive the row; spans typically wrap static
// const arrays declared at file scope in the host.
struct PaneToolbarRowConfig {
  std::span<const NavButtonSpec> navButtons;
  AuxSlotSpec hamburger;
  AuxSlotSpec addressDropdown;
};

class PaneToolbarRow {
 public:
  PaneToolbarRow() = default;
  ~PaneToolbarRow();

  PaneToolbarRow(const PaneToolbarRow&) = delete;
  PaneToolbarRow& operator=(const PaneToolbarRow&) = delete;

  bool create(HWND parent, HINSTANCE instance, std::size_t paneIdx,
              const PaneToolbarRowConfig& config);
  void destroy();

  HWND handle() const noexcept { return hwnd_; }
  std::size_t paneIndex() const noexcept { return paneIdx_; }

  // Adopts an existing child window (created with any parent) as the
  // address-bar slot. The child is reparented to this row. Pass
  // nullptr to detach. Triggers a layout if the row is sized.
  void setAddressBar(HWND addressBar);
  HWND addressBar() const noexcept { return addressBar_; }

  // Adopts the chevron button that sits next to the address bar.
  // Owner-drawn so we can paint a dark-mode glyph; click is routed
  // to the parent via the packed addressDropdown.id and is expected
  // to pop the host's address-bar popup.
  void setAddressDropdownBtn(HWND btn);
  HWND addressDropdownBtn() const noexcept { return addressDropdownBtn_; }

  HWND navToolbar() const noexcept { return navToolbar_; }
  HWND hamburger() const noexcept { return hamburger_; }

  // Reposition children based on the row's current client size.
  // Idempotent and safe to call before any children are attached.
  void layout();

  // Enable / disable a single nav button by SLOT INDEX into the
  // navButtons span passed in at create() time. Out-of-range calls
  // are no-ops.
  void setNavButtonEnabled(int slotIdx, bool enabled);

  // Recreates the icon + text fonts at `newDpi`, re-applies them to
  // every child that owns one, and triggers a layout pass. Call from
  // the host's WM_DPICHANGED handler when the row crosses a DPI
  // boundary.
  void onDpiChanged(UINT newDpi);

 private:
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);
  static bool registerClassOnce(HINSTANCE instance);
  bool createNavToolbar(HINSTANCE instance);
  bool createHamburger(HINSTANCE instance);
  LRESULT handleNavToolbarCustomDraw(LPARAM lParam);
  void drawHamburgerItem(LPARAM lParam);
  void fillToolbarTooltip(LPARAM lParam);
  void fillTooltipNeedText(LPARAM lParam);
  bool createHamburgerTooltip(HINSTANCE instance);

  const NavButtonSpec* findNavButton(WORD id) const noexcept;

  HWND hamburgerTip_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND addressBar_ = nullptr;
  HWND addressDropdownBtn_ = nullptr;
  HWND navToolbar_ = nullptr;
  HWND hamburger_ = nullptr;
  HFONT iconFont_ = nullptr;
  HFONT rowFont_ = nullptr;
  std::size_t paneIdx_ = 0;
  PaneToolbarRowConfig config_{};
};

}  // namespace fast_explorer::ui
