#include "ui/adapters/shell-item-dispatcher.h"

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "ui/column-formatter.h"
#include "ui/pane-controller.h"

namespace fast_explorer::ui::adapters {

ShellItemDispatcher::ShellItemDispatcher(const PaneController& pane) noexcept
    : pane_(&pane) {}

std::wstring ShellItemDispatcher::textFor(ports::ItemId id,
                                          ports::ItemField field) const {
  if (id == ports::kInvalidItemId) return {};
  const std::size_t visibleIndex = static_cast<std::size_t>(id - 1);
  const auto& store = pane_->store();
  // publishedCount() is the UI-safe bound while a worker may be
  // appending; visibleAt past that index would read mid-write data.
  if (visibleIndex >= store.publishedCount()) return {};
  const auto& entry = store.visibleAt(visibleIndex);
  switch (field) {
    case ports::ItemField::Name:
      return std::wstring(fast_explorer::core::nameView(entry));
    case ports::ItemField::SizeText:
      return formatSizeForEntry(entry);
    case ports::ItemField::ModifiedText:
      return formatModified(entry.modifiedTime100ns);
    case ports::ItemField::TypeText:
      return formatTypeForEntry(entry);
  }
  return {};
}

int ShellItemDispatcher::iconIndexFor(ports::ItemId /*id*/) const {
  // Icon plumbing still lives in icon-cache-coordinator and is read
  // directly by LVN_GETDISPINFO under LVIF_IMAGE. Routing it through
  // the port is part of step 12. Until then -1 means "use whatever
  // the list-view already has" (matches the port's documented
  // fallback semantics).
  return -1;
}

}  // namespace fast_explorer::ui::adapters
