#include "winui_lite/widgets/address-input.h"

#include <uxtheme.h>

namespace fast_explorer::ui {

HWND AddressInput::create(HWND parent, HINSTANCE instance) {
  // Plain Edit, not ComboBoxEx. v0.2.8 through v0.2.12 tried every
  // theme-strip + CTLCOLOR + WM_ERASEBKGND hack at the ComboBoxEx
  // and its inner ComboBox / Edit to dark-mode the textbox — each
  // attempt left the focused-state textbox white because the
  // ComboBox's themed renderer owns the focused-bg paint and
  // ignores CTLCOLOR / theme strip there. Plain Edit's bg comes
  // straight from WM_CTLCOLOREDIT sent to its parent (the row),
  // which the row handles directly. The dropdown chevron that
  // used to live on ComboBoxEx is a separate BS_OWNERDRAW button
  // owned by the host — Windows Explorer's own dark address bar
  // is built the same way.
  HWND edit = CreateWindowExW(
      0, WC_EDITW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          ES_AUTOHSCROLL | ES_LEFT,
      0, 0, 0, 0, parent, nullptr, instance, nullptr);
  if (edit != nullptr) {
    // Strip the theme so CTLCOLOREDIT actually drives the bg when
    // focused; themed Edits paint their own light bg in the focused
    // state and ignore the returned brush, same root cause as the
    // ComboBoxEx v0.2.12 ship that ate the original dark mode hacks.
    SetWindowTheme(edit, L"", L"");
    // No EM_SETMARGINS or WS_BORDER here — the NC padding subclass
    // (installed by PaneToolbarRow::setAddressBar) owns both the
    // horizontal margin and the 1-px border so they stay in sync
    // with the vertical-centring padding it computes.
  }
  return edit;
}

}  // namespace fast_explorer::ui
