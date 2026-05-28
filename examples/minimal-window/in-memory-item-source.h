// in-memory-item-source.h — demo adapter for ItemSource + ItemDispatcher.
//
// Synthesises ten fake "items" so the winui_lite_demo executable can
// drive an LVS_OWNERDATA list-view through the same ports the
// FastExplorer shell adapters fill, with zero shell dependency.
//
// The demo intentionally pairs both ports on one class so the demo's
// main.cpp can register a single object — keeping the example's wiring
// short. Real adapters split the two implementations for the freedom
// to swap one independently of the other (see the shell-* adapters
// under src/ui/adapters/).

#pragma once

#include "winui_lite/ports/item-dispatcher.h"
#include "winui_lite/ports/item-source.h"

namespace winui_lite_demo {

class InMemoryItemSource final
    : public fast_explorer::ui::ports::ItemSource,
      public fast_explorer::ui::ports::ItemDispatcher {
 public:
  InMemoryItemSource();
  ~InMemoryItemSource() override = default;

  // ItemSource
  bool navigateTo(const std::wstring& location) override;
  const std::wstring& currentLocation() const override;
  std::size_t count() const override;
  fast_explorer::ui::ports::ItemId idAt(std::size_t index) const override;

  // ItemDispatcher
  std::wstring textFor(
      fast_explorer::ui::ports::ItemId id,
      fast_explorer::ui::ports::ItemField field) const override;
  int iconIndexFor(fast_explorer::ui::ports::ItemId id) const override;

 private:
  std::wstring location_;
};

}  // namespace winui_lite_demo
