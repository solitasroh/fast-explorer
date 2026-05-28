// change-notifier.h — "tell me when this location's contents change" port.
//
// Decoupled from ItemSource so the host can mix and match: a source
// might already poll on its own schedule, or a viewer might want a
// directory watcher that drives a thumbnail cache rather than a list.
// The port deliberately does not specify HOW notifications arrive —
// adapters choose between PostMessage, std::function callback, or an
// async signal.

#pragma once

#include <string>

namespace fast_explorer::ui::ports {

class ChangeNotifier {
 public:
  virtual ~ChangeNotifier() = default;

  // Arm a watch on `location`. Replaces any prior watch. Returns
  // false on validation / OS-level failure (path not openable, watch
  // primitive unavailable). Whether the adapter coalesces or
  // forwards every event is its own decision.
  virtual bool watch(const std::wstring& location) = 0;

  // Disarm any active watch. Idempotent — calling stop() with no
  // watch in flight is a no-op.
  virtual void stop() = 0;
};

}  // namespace fast_explorer::ui::ports
