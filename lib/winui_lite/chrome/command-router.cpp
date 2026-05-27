#include "winui_lite/chrome/command-router.h"

#include <utility>

namespace fast_explorer::ui {

void CommandRouter::registerCommand(WORD id, Handler handler) {
  by_id_[id] = std::move(handler);
}

bool CommandRouter::dispatch(WORD id) const {
  const auto it = by_id_.find(id);
  if (it == by_id_.end()) return false;
  it->second();
  return true;
}

void CommandRouter::unregister(WORD id) {
  by_id_.erase(id);
}

}  // namespace fast_explorer::ui
