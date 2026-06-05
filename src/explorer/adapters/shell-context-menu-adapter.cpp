#include "explorer/adapters/shell-context-menu-adapter.h"

#include <memory>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/location.h"
#include "explorer/messages.h"
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
  for (ports::ItemId id : ids) {
    if (id == ports::kInvalidItemId) continue;
    const std::size_t row = static_cast<std::size_t>(id - 1);
    if (!store.rawIndexForVisibleRow(row).has_value()) continue;
    const auto& entry = store.visibleAt(row);
    if (entry.namePtr == nullptr || entry.nameLength == 0) continue;
    out.emplace_back(entry.namePtr, entry.nameLength);
  }
  return out;
}

// Returns the absolute path of the single selected item if it is a
// directory, or an empty string otherwise (multi-selection, file, or
// nothing selected).
std::wstring singleFolderPath(
    const PaneController& pane,
    const std::vector<ports::ItemId>& ids) {
  if (ids.size() != 1) return {};
  const ports::ItemId id = ids[0];
  if (id == ports::kInvalidItemId) return {};
  const auto& store = pane.store();
  const std::size_t row = static_cast<std::size_t>(id - 1);
  if (!store.rawIndexForVisibleRow(row).has_value()) return {};
  const auto& entry = store.visibleAt(row);
  if (!fast_explorer::core::isDirectory(entry)) return {};
  std::wstring out;
  return pane.sourcePathForVisibleRow(static_cast<std::uint32_t>(row), out)
             ? out
             : std::wstring{};
}

void showOpenInNewTabOnly(HWND ownerHwnd,
                          POINT screenPt,
                          const std::wstring& targetFolder,
                          const std::function<void(const std::wstring&)>& cb) {
  if (ownerHwnd == nullptr || targetFolder.empty() || !cb) {
    return;
  }
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    return;
  }
  AppendMenuW(menu, MF_STRING, kVerbOpenInNewTab, L"새 탭에서 열기");
  const UINT picked = TrackPopupMenuEx(
      menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
      screenPt.x, screenPt.y, ownerHwnd, nullptr);
  DestroyMenu(menu);
  if (picked == kVerbOpenInNewTab) {
    cb(targetFolder);
  }
}

}  // namespace

ShellContextMenuAdapter::ShellContextMenuAdapter(
    PaneController* const& activeCell, HWND ownerHwnd) noexcept
    : cell_(std::addressof(activeCell)), ownerHwnd_(ownerHwnd) {}

void ShellContextMenuAdapter::show(
    const std::vector<ports::ItemId>& ids, POINT screenPt) {
  PaneController* c = *cell_;
  if (!c) return;
  const std::wstring& folderPath = c->currentPath();
  if (folderPath.empty()) return;
  const auto leaves = resolveLeaves(*c, ids);

  // Detect single-folder selection: prepend "새 탭에서 열기" at the top
  // of the context menu.
  const std::wstring targetFolder = singleFolderPath(*c, ids);
  if (fast_explorer::core::isShellLocation(folderPath)) {
    showOpenInNewTabOnly(ownerHwnd_, screenPt, targetFolder, onOpenInNewTab);
    return;
  }

  ShellContextMenu::PrependItem prepend;
  if (!targetFolder.empty() && onOpenInNewTab) {
    prepend.id    = kVerbOpenInNewTab;
    prepend.label = L"새 탭에서 열기";
  }

  // leaves.empty() with non-empty ids means every id was invalid —
  // still safe to forward as a background-area click; the host
  // semantics expect empty leaves to mean "show folder menu".
  const UINT picked =
      fast_explorer::ui::ShellContextMenu::show(
          ownerHwnd_, folderPath, leaves, screenPt,
          /*extra=*/nullptr,
          prepend.id != 0 ? &prepend : nullptr);

  if (picked == kVerbOpenInNewTab && onOpenInNewTab) {
    onOpenInNewTab(targetFolder);
  }
}

}  // namespace fast_explorer::ui::adapters
