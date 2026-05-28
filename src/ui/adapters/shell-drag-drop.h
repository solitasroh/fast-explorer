// shell-drag-drop.h — explorer-side adapter for DragDropBackend.
//
// Wraps the shell-bind + FileDropSource + DoDragDrop sequence that
// MainWindow used to inline. The adapter owns its drag flow but is
// stateless between drags; the same instance can serve every drag
// originated from one pane.
//
// Scope: outgoing drag only. Incoming drops still go through the
// per-pane PaneDropTarget COM object registered via RegisterDragDrop
// during installPaneAt. Folding the drop-receive side into the port
// is a future refactor.

#pragma once

#include <windows.h>

#include <vector>

#include "winui_lite/ports/drag-drop-backend.h"

namespace fast_explorer::ui {

class PaneController;

namespace adapters {

class ShellDragDrop final : public ports::DragDropBackend {
 public:
  // `pane` is borrowed and must outlive the adapter. `listView` is
  // the HWND DoDragDrop is anchored on (used by GetUIObjectOf as
  // hwndOwner so the shell can position progress UI correctly).
  ShellDragDrop(const PaneController& pane, HWND listView) noexcept;
  ~ShellDragDrop() override = default;

  ShellDragDrop(const ShellDragDrop&) = delete;
  ShellDragDrop& operator=(const ShellDragDrop&) = delete;

  bool beginDrag(const std::vector<ports::ItemId>& ids) override;

 private:
  const PaneController* pane_;
  HWND listView_;
};

}  // namespace adapters
}  // namespace fast_explorer::ui
