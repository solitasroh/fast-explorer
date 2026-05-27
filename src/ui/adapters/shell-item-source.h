// shell-item-source.h — explorer-side adapter for the ItemSource port.
//
// Wraps the existing PaneController so chrome can ask "what items are
// here?" without touching shell APIs. Lifecycle: one adapter per pane,
// borrowed PaneController* must outlive the adapter.
//
// ItemId mapping:
//   id = 0          -> kInvalidItemId (port reserves 0)
//   id = N (N >= 1) -> visible row index N - 1 in the store
// Adapters renumber on every navigateTo since the underlying store
// resets; chrome must not cache ids across navigations.
//
// This file is part of the explorer host, not winui_lite — it knows
// about FileModelStore and PaneController internals on purpose. The
// reverse direction (winui_lite -> src/ui/adapters) is forbidden by
// the lib isolation check.

#pragma once

#include <cstddef>
#include <string>

#include "winui_lite/ports/item-source.h"

namespace fast_explorer::ui {

class PaneController;

namespace adapters {

class ShellItemSource final : public ports::ItemSource {
 public:
  explicit ShellItemSource(PaneController& pane) noexcept;
  ~ShellItemSource() override = default;

  ShellItemSource(const ShellItemSource&) = delete;
  ShellItemSource& operator=(const ShellItemSource&) = delete;

  bool navigateTo(const std::wstring& location) override;
  const std::wstring& currentLocation() const override;
  std::size_t count() const override;
  ports::ItemId idAt(std::size_t index) const override;

 private:
  PaneController* pane_;  // borrowed; non-owning
};

}  // namespace adapters
}  // namespace fast_explorer::ui
