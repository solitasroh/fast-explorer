#pragma once

#include <windows.h>

#include <string>

namespace fast_explorer::ui {

class PaneController;

// Owns the LVS_EDITLABELS lifecycle for one pane: the F2 in-place
// rename, the Ctrl+Shift+N "create then auto-edit" handoff, and the
// LVN_BEGIN/ENDLABELEDIT routing into PaneController. The pending
// folder name is held here because beginCreateSubfolder() arms it
// and maybeStartPendingEdit() consumes it on the next onEnumComplete.
class LabelEditController {
 public:
  LabelEditController(HWND listView, PaneController& pane) noexcept;
  ~LabelEditController();

  LabelEditController(const LabelEditController&) = delete;
  LabelEditController& operator=(const LabelEditController&) = delete;
  LabelEditController(LabelEditController&&) = delete;
  LabelEditController& operator=(LabelEditController&&) = delete;

  // F2 accelerator entry. Gated on list-view focus so the key does
  // not hijack edits in the address bar.
  void beginRenameFocused();

  // Ctrl+Shift+N accelerator entry. Queues a uniquely-named "New
  // folder" create, then arms the pending name so the next
  // onEnumComplete starts an in-place rename on the created row.
  void beginCreateSubfolder();

  // Called from onEnumComplete. Locates the row whose leaf matches
  // the pending name and starts the in-place edit; the swap-and-clear
  // happens only after the listView_ + non-empty-pending gate passes,
  // so a null-listView call preserves the pending name for a later
  // call that does have a real HWND.
  void maybeStartPendingEdit();

  // LVN_BEGINLABELEDITW handler. Returning FALSE permits the edit.
  [[nodiscard]] LRESULT handleBeginEdit();

  // LVN_ENDLABELEDITW handler. Always returns FALSE under
  // LVS_OWNERDATA (the list-view stores no text of its own); commits
  // the rename through PaneController on a non-cancel.
  [[nodiscard]] LRESULT handleEndEdit(NMHDR* hdr);

  // Test affordance: lets tests observe the swap-and-clear handoff
  // between beginCreateSubfolder() and maybeStartPendingEdit() without
  // exposing the internal state to other production callers.
  const std::wstring& pendingFolderNameForTest() const noexcept {
    return pendingFolderName_;
  }

 private:
  HWND listView_;
  PaneController& pane_;
  std::wstring pendingFolderName_;
};

}  // namespace fast_explorer::ui
