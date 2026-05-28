#include "ui/adapters/local-settings-store.h"

#include <utility>

namespace fast_explorer::ui::adapters {

LocalSettingsStore::LocalSettingsStore(
    std::wstring path,
    fast_explorer::core::SessionState& state) noexcept
    : path_(std::move(path)), state_(&state) {}

bool LocalSettingsStore::load() {
  if (path_.empty()) return false;
  return fast_explorer::core::loadSessionState(path_, *state_);
}

bool LocalSettingsStore::save() {
  if (path_.empty()) return false;
  return fast_explorer::core::saveSessionState(path_, *state_);
}

}  // namespace fast_explorer::ui::adapters
