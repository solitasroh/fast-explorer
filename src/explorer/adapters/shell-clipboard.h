// shell-clipboard.h — explorer-side adapter for ClipboardBackend.
//
// Thin wrapper around ClipboardOps + the borrowed PaneController.
// Resolves ItemIds (1-based visible row indices, matching
// ShellItemSource) to leaf names by reading the controller's store,
// then defers to ClipboardOps for the actual OLE clipboard call.
//
// What this adapter does NOT own:
//   * Cut-state visuals (LVIS_CUT painting). The host keeps a
//     CutStateTracker and refreshes each list-view itself — the
//     adapter only signals success/failure of the clipboard write.

#pragma once

#include <windows.h>

#include <vector>

#include "winui_lite/ports/clipboard-backend.h"

namespace fast_explorer::ui {

class PaneController;

namespace adapters {

class ShellClipboard final : public ports::ClipboardBackend {
 public:
  // `pane` is borrowed and must outlive the adapter. `ownerHwnd` is
  // forwarded to ClipboardOps::paste as the parent for any progress
  // / conflict UI the shell raises during the paste.
  explicit ShellClipboard(const PaneController& pane, HWND ownerHwnd) noexcept;
  ~ShellClipboard() override = default;

  ShellClipboard(const ShellClipboard&) = delete;
  ShellClipboard& operator=(const ShellClipboard&) = delete;

  bool copyItems(const std::vector<ports::ItemId>& ids, bool cut) override;
  ports::PasteOutcome pasteInto(
      const std::wstring& targetLocation) override;

 private:
  const PaneController* pane_;
  HWND ownerHwnd_;
};

}  // namespace adapters
}  // namespace fast_explorer::ui
