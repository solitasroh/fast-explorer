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
  // `activeCell` is a reference to the host's cell pointer; the adapter
  // takes its address and dereferences on each call. `ownerHwnd` is
  // forwarded to ClipboardOps::paste as the parent for any progress
  // / conflict UI the shell raises during the paste.
  explicit ShellClipboard(PaneController* const& activeCell,
                          HWND ownerHwnd) noexcept;
  ~ShellClipboard() override = default;

  ShellClipboard(const ShellClipboard&) = delete;
  ShellClipboard& operator=(const ShellClipboard&) = delete;

  bool copyItems(const std::vector<ports::ItemId>& ids, bool cut) override;
  ports::PasteOutcome pasteInto(
      const std::wstring& targetLocation) override;

 private:
  PaneController* const* cell_;  // borrowed; written by host on tab switch
  HWND ownerHwnd_;
};

}  // namespace adapters
}  // namespace fast_explorer::ui
