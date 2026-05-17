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
  // the pending name (cleared either way) and starts the in-place
  // edit. No-op when nothing is pending.
  void maybeStartPendingEdit();

  // LVN_BEGINLABELEDITW handler. Returning FALSE permits the edit.
  [[nodiscard]] LRESULT handleBeginEdit();

  // LVN_ENDLABELEDITW handler. Always returns FALSE under
  // LVS_OWNERDATA (the list-view stores no text of its own); commits
  // the rename through PaneController on a non-cancel.
  [[nodiscard]] LRESULT handleEndEdit(NMHDR* hdr);

 private:
  HWND listView_;
  PaneController& pane_;
  std::wstring pendingFolderName_;
};

}  // namespace fast_explorer::ui
