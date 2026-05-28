// clipboard-backend.h — "copy / cut / paste these items" port.
//
// Sits next to ItemSource: chrome surfaces a clipboard intent at the
// current selection without touching the OLE clipboard or knowing
// whether a "leaf name" is even the right identity for the item.
// Adapters map ItemIds back to whatever payload format the underlying
// system needs (CF_HDROP for shell, JSON for an in-memory store, …).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "winui_lite/ports/item-source.h"

namespace fast_explorer::ui::ports {

enum class PasteOutcome : std::uint8_t {
  Success,
  NoData,      // clipboard empty / wrong format
  Rejected,    // target refused (read-only, cross-volume policy, …)
};

class ClipboardBackend {
 public:
  virtual ~ClipboardBackend() = default;

  // Pushes `ids` (interpreted in the adapter's current source
  // location) onto the system clipboard. cut == true marks the
  // payload for move semantics; the host typically draws the
  // dimmed-ghost effect afterwards. Returns false when no payload
  // could be assembled (empty input, all ids invalid, …).
  virtual bool copyItems(const std::vector<ItemId>& ids, bool cut) = 0;

  // Pastes clipboard content into `targetLocation`. The adapter
  // owns any progress / confirmation UI it needs.
  virtual PasteOutcome pasteInto(const std::wstring& targetLocation) = 0;
};

}  // namespace fast_explorer::ui::ports
