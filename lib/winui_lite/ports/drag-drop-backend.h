// drag-drop-backend.h — "begin an OLE drag of these items" port.
//
// Limited to the outgoing direction (drag-from) in step 9 of the
// refactor plan. Incoming drops still flow through host-side IDropTarget
// objects today; folding drop-receive into the port is a future step
// because it requires hover / effect-computation callbacks that don't
// fit a single-method interface.

#pragma once

#include <vector>

#include "winui_lite/ports/item-source.h"

namespace fast_explorer::ui::ports {

class DragDropBackend {
 public:
  virtual ~DragDropBackend() = default;

  // Synchronously initiates an OLE drag carrying `ids`. Blocks
  // until the drop completes or is cancelled by the user. Returns
  // true when a drag was actually initiated (false when `ids` is
  // empty or the payload could not be assembled).
  virtual bool beginDrag(const std::vector<ItemId>& ids) = 0;
};

}  // namespace fast_explorer::ui::ports
