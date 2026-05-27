// item-activator.h — "what does double-click on this item do" port.
//
// Decoupled from ItemSource so a single source can be paired with
// different activation policies — e.g. the explorer adapter opens
// folders / runs files, while a log viewer's activator might pop
// up a detail panel without ever navigating.
//
// Activation never directly mutates the ItemSource. If the policy
// implies a navigation, the activator reports the new location and
// the chrome caller forwards it to ItemSource::navigateTo. That
// keeps the source as the single source of truth for "where am I
// looking" state.

#pragma once

#include <string>

#include "winui_lite/ports/item-source.h"

namespace fast_explorer::ui::ports {

// Outcome of activating one item (double-click, Enter key, etc.).
struct ActivationResult {
  // True iff the activator consumed the event. Chrome may fall
  // back to a default behaviour (e.g. ignore) when false.
  bool handled = false;
  // Non-empty iff activation requests the source navigate to a
  // new location. Empty when the item runs an external action or
  // when no navigation is implied.
  std::wstring nextLocation;
};

class ItemActivator {
 public:
  virtual ~ItemActivator() = default;

  // Activate `id`. Implementations may run an external program,
  // request a sub-location navigation, or do nothing. See
  // ActivationResult for the response shape. Invalid ids should
  // return { handled=false, nextLocation="" }.
  virtual ActivationResult activate(ItemId id) = 0;
};

}  // namespace fast_explorer::ui::ports
