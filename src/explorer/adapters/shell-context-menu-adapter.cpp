#include "explorer/adapters/shell-context-menu-adapter.h"

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "explorer/pane-controller.h"
#include "explorer/shell-context-menu.h"

namespace fast_explorer::ui::adapters {

namespace {

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

ShellContextMenuAdapter::ShellContextMenuAdapter(
    const PaneController& pane, HWND ownerHwnd) noexcept
    : pane_(&pane), ownerHwnd_(ownerHwnd) {}

void ShellContextMenuAdapter::show(
    const std::vector<ports::ItemId>& ids, POINT screenPt) {
  const std::wstring& folderPath = pane_->currentPath();
  if (folderPath.empty()) return;
  const auto leaves = resolveLeaves(*pane_, ids);
  // leaves.empty() with non-empty ids means every id was invalid —
  // still safe to forward as a background-area click; the host
  // semantics expect empty leaves to mean "show folder menu".
  fast_explorer::ui::ShellContextMenu::show(ownerHwnd_, folderPath, leaves,
                                            screenPt);
}

}  // namespace fast_explorer::ui::adapters
