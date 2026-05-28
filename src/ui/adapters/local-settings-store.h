// local-settings-store.h — explorer-side adapter for SettingsStore.
//
// Persists fast_explorer::core::SessionState to a JSON file. Named
// "local" rather than "shell" because the underlying storage is a
// plain file path (typically %LOCALAPPDATA%\FastExplorer\settings.json)
// rather than the Win32 shell; the plan's older "RegistrySettingsStore"
// label reflected an earlier design that never shipped.

#pragma once

#include <string>

#include "core/settings-store.h"
#include "winui_lite/ports/settings-store.h"

namespace fast_explorer::ui::adapters {

class LocalSettingsStore final : public ports::SettingsStore {
 public:
  // `path` is the JSON file location (empty disables persistence —
  // load() / save() both return false). `state` is the in-memory
  // session buffer the adapter reads from on save() and writes into
  // on load(); it must outlive the adapter.
  LocalSettingsStore(std::wstring path,
                     fast_explorer::core::SessionState& state) noexcept;
  ~LocalSettingsStore() override = default;

  LocalSettingsStore(const LocalSettingsStore&) = delete;
  LocalSettingsStore& operator=(const LocalSettingsStore&) = delete;

  bool load() override;
  bool save() override;

 private:
  std::wstring path_;
  fast_explorer::core::SessionState* state_;
};

}  // namespace fast_explorer::ui::adapters
