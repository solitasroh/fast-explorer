// context-menu.h — "show a right-click menu for these items" port.
//
// The shell's context menu is deeply Win32 / COM machinery —
// IContextMenu draws its own owner-draw entries, dispatches verbs
// itself, and pulls owner-draw callbacks through WM_INITMENUPOPUP /
// WM_DRAWITEM forwarded from the host. The port stays narrow on
// purpose: chrome only knows how to ask "show a menu for these ids,
// anchored at this screen point" — everything past that is the
// adapter's responsibility.
//
// What is intentionally NOT here:
//   * Custom-augmented menus (e.g. the FastExplorer empty-area menu
//     that prepends a 'group by' submenu). Hosts that need that
//     stay on the direct ShellContextMenu API.
//   * Pre-flight queries ("what verbs are available?"). The shell
//     answers this on the fly while the menu is open; offloading
//     it to a pre-flight call would double the COM cost.

#pragma once

#include <windows.h>

#include <vector>

#include "winui_lite/ports/item-source.h"

namespace fast_explorer::ui::ports {

class ContextMenu {
 public:
  virtual ~ContextMenu() = default;

  // Shows the context menu for `ids` anchored at `screenPt`. Empty
  // `ids` is interpreted as a background / empty-area click and the
  // adapter routes accordingly (folder Open / Paste / New menu).
  // Synchronous: the call returns after the user picks an entry or
  // dismisses the menu.
  virtual void show(const std::vector<ItemId>& ids, POINT screenPt) = 0;
};

}  // namespace fast_explorer::ui::ports
