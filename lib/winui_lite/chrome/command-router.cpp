#include "winui_lite/chrome/command-router.h"

#include <utility>

#include "winui_lite/chrome/cmd-packing.h"

namespace fast_explorer::ui {

void CommandRouter::registerCommand(WORD id, Handler handler) {
  by_id_[id] = std::move(handler);
}

void CommandRouter::registerPackedCommand(WORD buttonId,
                                           PackedHandler handler) {
  packed_[buttonId] = std::move(handler);
}

bool CommandRouter::dispatch(WORD id) const {
  // by_id_ wins on conflict. Accelerators and the host's group-by
  // submenu come through this path with the raw id.
  if (const auto it = by_id_.find(id); it != by_id_.end()) {
    it->second();
    return true;
  }
  // Fall back to packed lookup. unpackButton inverts the bit shift
  // that packCmd applied at the registration of the toolbar button
  // / hamburger menu item; unpackPane recovers the pane index for
  // the handler.
  const WORD button = unpackButton(id);
  if (const auto pit = packed_.find(button); pit != packed_.end()) {
    pit->second(unpackPane(id));
    return true;
  }
  return false;
}

void CommandRouter::unregister(WORD id) {
  by_id_.erase(id);
}

void CommandRouter::unregisterPacked(WORD buttonId) {
  packed_.erase(buttonId);
}

}  // namespace fast_explorer::ui
