// address-input.h — themed plain-Edit factory for path/address fields.
//
// A thin wrapper around CreateWindowExW(WC_EDITW, ...) with the
// uxtheme strip required for WM_CTLCOLOREDIT to drive the focused
// background. Used by chrome rows (PaneToolbarRow) to host any
// caller-owned address bar, but the widget itself knows nothing
// about paths, navigation, or completion — those live on the host
// side via a separate subclass.
//
// Why a separate widget rather than a free helper:
//   * Lets the demo example (in-memory items) create the same input
//     without copying the Edit-creation hack.
//   * Keeps the WC_EDITW + SetWindowTheme(L"", L"") incantation in
//     one place so the v0.2.x ComboBoxEx-dark-mode regression cannot
//     reappear by a half-copied factory.
//
// What is intentionally NOT here:
//   * Navigation / completion / clipboard handling — those are
//     installed on the returned HWND by the host's own subclass.
//   * NC padding / border drawing — PaneToolbarRow::setAddressBar
//     owns that subclass.
//   * Dropdown chevron button — separate factory inside the host.

#pragma once

#include <windows.h>

namespace fast_explorer::ui {

class AddressInput {
 public:
  // Creates an unparented-but-WS_CHILD plain Edit suitable for
  // hosting in a PaneToolbarRow address slot. Returns nullptr on
  // CreateWindowExW failure; non-null HWND has had its theme
  // stripped (SetWindowTheme(L"", L"")) so WM_CTLCOLOREDIT from
  // its parent actually drives the focused-state background.
  //
  // The HWND is owned by the standard Win32 parent/child lifetime
  // — the host destroys it via DestroyWindow or by destroying the
  // parent. There is no AddressInput object kept alive.
  static HWND create(HWND parent, HINSTANCE instance);
};

}  // namespace fast_explorer::ui
