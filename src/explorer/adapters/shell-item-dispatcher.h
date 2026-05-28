// shell-item-dispatcher.h — explorer-side adapter for ItemDispatcher.
//
// Pairs with ShellItemSource: takes the same PaneController, resolves
// ItemIds to FileEntry references in the controller's store, and
// formats each ItemField via the existing column-formatter helpers.
//
// iconIndexFor returns -1 today. The system imagelist mapping lives
// inside icon-cache-coordinator and folding it behind this port is a
// larger refactor — step 12 takes that on when LVN_GETDISPINFO moves
// to dispatcher-driven rendering. Until then this method is a stub.

#pragma once

#include <string>

#include "winui_lite/ports/item-dispatcher.h"

namespace fast_explorer::ui {

class PaneController;

namespace adapters {

class ShellItemDispatcher final : public ports::ItemDispatcher {
 public:
  explicit ShellItemDispatcher(PaneController* const& activeCell) noexcept;
  ~ShellItemDispatcher() override = default;

  ShellItemDispatcher(const ShellItemDispatcher&) = delete;
  ShellItemDispatcher& operator=(const ShellItemDispatcher&) = delete;

  std::wstring textFor(ports::ItemId id,
                       ports::ItemField field) const override;
  int iconIndexFor(ports::ItemId id) const override;

 private:
  PaneController* const* cell_;  // borrowed; written by host on tab switch
};

}  // namespace adapters
}  // namespace fast_explorer::ui
