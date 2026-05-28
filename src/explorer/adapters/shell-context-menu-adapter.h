// shell-context-menu-adapter.h — explorer-side adapter for
// ContextMenu port.
//
// Wraps the existing ShellContextMenu::show. Resolves ItemIds to leaf
// names against the borrowed PaneController, then delegates the rest
// of the shell verb machinery to the host-side ShellContextMenu
// helper. The bare 'ShellContextMenu' class name belongs to that
// helper in the fast_explorer::ui namespace — this adapter sits in
// fast_explorer::ui::adapters so the names do not collide, and the
// file is suffixed -adapter to keep the source tree readable.

#pragma once

#include <windows.h>

#include <vector>

#include "winui_lite/ports/context-menu.h"

namespace fast_explorer::ui {

class PaneController;

namespace adapters {

class ShellContextMenuAdapter final : public ports::ContextMenu {
 public:
  ShellContextMenuAdapter(PaneController* const& activeCell,
                          HWND ownerHwnd) noexcept;
  ~ShellContextMenuAdapter() override = default;

  ShellContextMenuAdapter(const ShellContextMenuAdapter&) = delete;
  ShellContextMenuAdapter& operator=(const ShellContextMenuAdapter&) = delete;

  void show(const std::vector<ports::ItemId>& ids,
            POINT screenPt) override;

 private:
  PaneController* const* cell_;  // borrowed; written by host on tab switch
  HWND ownerHwnd_;
};

}  // namespace adapters
}  // namespace fast_explorer::ui
