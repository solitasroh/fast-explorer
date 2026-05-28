#include "explorer/adapters/shell-clipboard.h"

#include <memory>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "explorer/clipboard-ops.h"
#include "explorer/pane-controller.h"

namespace fast_explorer::ui::adapters {

namespace {

// Resolves ItemIds against the controller's store and returns the
// matching leaf names. Mirrors collectSelectedLeaves in main-window
// but takes ids instead of reading LVNI_SELECTED rows — the caller
// (host) is the one that builds the id vector from the list-view.
std::vector<std::wstring> resolveLeaves(
    const PaneController& pane,
    const std::vector<ports::ItemId>& ids) {
  std::vector<std::wstring> out;
  out.reserve(ids.size());
  const auto& store = pane.store();
  const auto bound = store.publishedCount();
  for (ports::ItemId id : ids) {
    if (id == ports::kInvalidItemId) continue;
    const std::size_t row = static_cast<std::size_t>(id - 1);
    if (row >= bound) continue;
    const auto& entry = store.visibleAt(row);
    if (entry.namePtr == nullptr || entry.nameLength == 0) continue;
    out.emplace_back(entry.namePtr, entry.nameLength);
  }
  return out;
}

}  // namespace

ShellClipboard::ShellClipboard(PaneController* const& activeCell,
                                HWND ownerHwnd) noexcept
    : cell_(std::addressof(activeCell)), ownerHwnd_(ownerHwnd) {}

bool ShellClipboard::copyItems(const std::vector<ports::ItemId>& ids,
                                bool cut) {
  PaneController* c = *cell_;
  if (!c) return false;
  const auto leaves = resolveLeaves(*c, ids);
  if (leaves.empty()) return false;
  return ClipboardOps::copy(c->currentPath(), leaves, cut);
}

ports::PasteOutcome ShellClipboard::pasteInto(
    const std::wstring& targetLocation) {
  if (!*cell_) return ports::PasteOutcome::Rejected;
  if (targetLocation.empty()) return ports::PasteOutcome::Rejected;
  const auto result = ClipboardOps::paste(targetLocation, ownerHwnd_);
  switch (result) {
    case PasteResult::Success:  return ports::PasteOutcome::Success;
    case PasteResult::NoData:   return ports::PasteOutcome::NoData;
    case PasteResult::NoTarget:
    case PasteResult::Rejected:
      return ports::PasteOutcome::Rejected;
  }
  return ports::PasteOutcome::Rejected;
}

}  // namespace fast_explorer::ui::adapters
